#include "core/cells/sync_manager.h"

#include "core/cells/crdt.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

class SyncManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create two workbooks simulating two peers with shared entity IDs
        node_a = generate_id();
        node_b = generate_id();

        // Generate shared IDs first so both workbooks have the same entities
        sheet_id = generate_id();
        col_id = generate_id();
        row_id = generate_id();
        cell_id = generate_id();

        workbook_a = createWorkbook(node_a);
        workbook_b = createWorkbook(node_b);

        sync_a = std::make_unique<SyncManager>(workbook_a.get());
        sync_b = std::make_unique<SyncManager>(workbook_b.get());
    }

    std::unique_ptr<Workbook> createWorkbook(const ID& node_id) {
        auto wb = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        wb->setNodeId(node_id);

        auto sheet = std::make_unique<Sheet>(sheet_id, "Sheet1");
        sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

        // Create a column and row using shared IDs
        auto col = std::make_unique<Axis>(col_id, true);
        auto row = std::make_unique<Axis>(row_id, false);
        sheet->addColumn(std::move(col));
        sheet->addRow(std::move(row));

        // Create a cell using shared ID
        auto cell = std::make_unique<Cell>(cell_id, col_id, row_id);
        cell->value = CellValue(0.0);
        sheet->addCell(std::move(cell));

        wb->addSheet(std::move(sheet));
        return wb;
    }

    std::unique_ptr<Workbook> workbook_a;
    std::unique_ptr<Workbook> workbook_b;
    std::unique_ptr<SyncManager> sync_a;
    std::unique_ptr<SyncManager> sync_b;
    ID node_a, node_b;
    ID sheet_id, col_id, row_id, cell_id;
};

TEST_F(SyncManagerTest, AddAndRemovePeer) {
    EXPECT_EQ(sync_a->peerCount(), 0);
    EXPECT_FALSE(sync_a->hasPeer(node_b));

    sync_a->addPeer(node_b);
    EXPECT_EQ(sync_a->peerCount(), 1);
    EXPECT_TRUE(sync_a->hasPeer(node_b));

    auto peers = sync_a->getPeerIds();
    EXPECT_EQ(peers.size(), 1);
    EXPECT_EQ(peers[0], node_b);

    sync_a->removePeer(node_b);
    EXPECT_EQ(sync_a->peerCount(), 0);
    EXPECT_FALSE(sync_a->hasPeer(node_b));
}

TEST_F(SyncManagerTest, AddPeerQueuesHello) {
    sync_a->addPeer(node_b);

    auto messages = sync_a->getOutgoingMessages();
    EXPECT_EQ(messages.size(), 1);

    // Should be targeted to node_b
    EXPECT_EQ(messages[0].peerId, node_b);
    EXPECT_FALSE(messages[0].isBroadcast());

    // Should contain "hello" type
    EXPECT_NE(messages[0].json.find("\"type\":\"hello\""), std::string::npos);
    EXPECT_NE(messages[0].json.find("\"peer_id\":\""), std::string::npos);
    EXPECT_NE(messages[0].json.find("\"hlc\":\""), std::string::npos);
    EXPECT_NE(messages[0].json.find("\"op_count\":"), std::string::npos);
}

TEST_F(SyncManagerTest, AddPeerTwiceNoDoubleHello) {
    sync_a->addPeer(node_b);
    sync_a->addPeer(node_b);  // Add again

    auto messages = sync_a->getOutgoingMessages();
    EXPECT_EQ(messages.size(), 1);  // Only one hello
}

TEST_F(SyncManagerTest, HandleHelloFromPeerWithSameOpCount) {
    // Both have 0 operations — still bidirectional exchange (request + response)
    sync_a->addPeer(node_b);
    auto hello_messages = sync_a->getOutgoingMessages();
    EXPECT_EQ(hello_messages.size(), 1);

    auto result = sync_b->handleMessage(node_a, hello_messages[0].json);

    ASSERT_EQ(result.messages.size(), 2);
    EXPECT_NE(result.messages[0].json.find("\"type\":\"sync-request\""), std::string::npos);
    EXPECT_NE(result.messages[1].json.find("\"type\":\"sync-response\""), std::string::npos);
    EXPECT_FALSE(result.dataModified);
}

TEST_F(SyncManagerTest, HandleHelloFromPeerWithMoreOps) {
    // A has 0 operations, B has operations
    Operation op = makeCellSetOp(*workbook_b, cell_id, R"({"t":"n","v":"42"})");
    applyOperation(*workbook_b, op);

    sync_a->addPeer(node_b);
    auto hello_messages = sync_a->getOutgoingMessages();

    // B receives hello from A → always sync-request + full sync-response
    auto result = sync_b->handleMessage(node_a, hello_messages[0].json);

    ASSERT_EQ(result.messages.size(), 2);
    EXPECT_NE(result.messages[0].json.find("\"type\":\"sync-request\""), std::string::npos);
    EXPECT_NE(result.messages[1].json.find("\"type\":\"sync-response\""), std::string::npos);
    EXPECT_NE(result.messages[1].json.find("\"operations\":"), std::string::npos);
    EXPECT_NE(result.messages[1].json.find("CELL_SET"), std::string::npos);
    EXPECT_FALSE(result.dataModified);
}

TEST_F(SyncManagerTest, HandleHelloFromPeerWithFewerOps) {
    // A has operations, B has 0 operations
    Operation op = makeCellSetOp(*workbook_a, cell_id, R"({"t":"n","v":"42"})");
    applyOperation(*workbook_a, op);

    sync_b->addPeer(node_a);
    auto hello_messages = sync_b->getOutgoingMessages();

    // A receives hello from B → always sync-request + full sync-response (includes A's ops)
    auto result = sync_a->handleMessage(node_b, hello_messages[0].json);

    ASSERT_EQ(result.messages.size(), 2);
    EXPECT_NE(result.messages[0].json.find("\"type\":\"sync-request\""), std::string::npos);
    EXPECT_NE(result.messages[1].json.find("\"type\":\"sync-response\""), std::string::npos);
    EXPECT_NE(result.messages[1].json.find("CELL_SET"), std::string::npos);
    EXPECT_FALSE(result.dataModified);
}

TEST_F(SyncManagerTest, ConcurrentLocalOpsExchangedOnHello) {
    // Divergent histories with same length: A and B each have one different CELL_SET
    Operation op_a = makeCellSetOp(*workbook_a, cell_id, R"({"t":"n","v":"1"})");
    applyOperation(*workbook_a, op_a);
    Operation op_b = makeCellSetOp(*workbook_b, cell_id, R"({"t":"n","v":"2"})");
    applyOperation(*workbook_b, op_b);

    sync_a->addPeer(node_b);
    auto hello_a = sync_a->getOutgoingMessages();
    ASSERT_EQ(hello_a.size(), 1);

    // B handles A's hello → sends full log including op_b
    auto from_b = sync_b->handleMessage(node_a, hello_a[0].json);
    ASSERT_EQ(from_b.messages.size(), 2);
    EXPECT_NE(from_b.messages[1].json.find("\"v\":\"2\""), std::string::npos);

    // A applies B's sync-response
    auto applied = sync_a->handleMessage(node_b, from_b.messages[1].json);
    EXPECT_TRUE(applied.dataModified || !applied.receivedOperations.empty());
}

TEST_F(SyncManagerTest, HandleSyncRequest) {
    // A has operations
    Operation op = makeCellSetOp(*workbook_a, cell_id, R"({"t":"n","v":"42"})");
    applyOperation(*workbook_a, op);

    // B sends sync-request
    std::string syncRequest = R"({"type":"sync-request","since_hlc":"0.0.~"})";
    auto result = sync_a->handleMessage(node_b, syncRequest);

    EXPECT_EQ(result.messages.size(), 1);
    EXPECT_NE(result.messages[0].json.find("\"type\":\"sync-response\""), std::string::npos);
    EXPECT_NE(result.messages[0].json.find("\"operations\":"), std::string::npos);
    EXPECT_NE(result.messages[0].json.find("CELL_SET"), std::string::npos);
    EXPECT_FALSE(result.dataModified);  // Sync-request doesn't modify our data
}

TEST_F(SyncManagerTest, HandleSyncResponse) {
    // Create an operation on A
    Operation op = makeCellSetOp(*workbook_a, cell_id, R"({"t":"n","v":"99"})");
    applyOperation(*workbook_a, op);

    // Get operations from A as JSON
    std::string syncResponse =
        R"({"type":"sync-response","operations":[)" + op.toJSON() + R"(],"complete":true})";

    // B receives sync-response
    sync_b->addPeer(node_a);
    sync_b->getOutgoingMessages();  // Clear hello

    auto result = sync_b->handleMessage(node_a, syncResponse);

    // No response needed for sync-response
    EXPECT_EQ(result.messages.size(), 0);
    // Data WAS modified (operations applied)
    EXPECT_TRUE(result.dataModified);

    // B applied the operation (oplog is pruned after sync because peer A is now synced)
    // The cell value should be correctly updated
    EXPECT_EQ(workbook_b->getSheet(sheet_id)->getCell(cell_id)->value.raw, "99");
}

TEST_F(SyncManagerTest, HandleOperationsBatch) {
    // Create operations on A
    Operation op1 = makeCellSetOp(*workbook_a, cell_id, R"({"t":"n","v":"1"})");
    applyOperation(*workbook_a, op1);
    Operation op2 = makeCellSetOp(*workbook_a, cell_id, R"({"t":"n","v":"2"})");
    applyOperation(*workbook_a, op2);

    // Send as operations batch
    std::string opsBatch =
        R"({"type":"operations","batch":[)" + op1.toJSON() + "," + op2.toJSON() + R"(]})";

    auto result = sync_b->handleMessage(node_a, opsBatch);

    // Should return an ACK message
    EXPECT_EQ(result.messages.size(), 1);
    EXPECT_NE(result.messages[0].json.find("\"type\":\"ack\""), std::string::npos);
    // Data WAS modified (operations applied)
    EXPECT_TRUE(result.dataModified);

    // B should now have both operations
    EXPECT_EQ(workbook_b->getOpLog()->size(), 2);
}

TEST_F(SyncManagerTest, QueueBroadcast) {
    sync_a->addPeer(node_b);
    sync_a->getOutgoingMessages();  // Clear hello

    sync_a->queueBroadcast(R"({"type":"test"})");

    auto messages = sync_a->getOutgoingMessages();
    EXPECT_EQ(messages.size(), 1);
    EXPECT_TRUE(messages[0].isBroadcast());
    EXPECT_EQ(messages[0].json, R"({"type":"test"})");
}

TEST_F(SyncManagerTest, QueueToPeer) {
    sync_a->addPeer(node_b);
    sync_a->getOutgoingMessages();  // Clear hello

    sync_a->queueToPeer(node_b, R"({"type":"test"})");

    auto messages = sync_a->getOutgoingMessages();
    EXPECT_EQ(messages.size(), 1);
    EXPECT_FALSE(messages[0].isBroadcast());
    EXPECT_EQ(messages[0].peerId, node_b);
}

TEST_F(SyncManagerTest, GetPeerSyncState) {
    EXPECT_EQ(sync_a->getPeerSyncState(node_b), nullptr);

    sync_a->addPeer(node_b);

    const PeerSyncState* state = sync_a->getPeerSyncState(node_b);
    EXPECT_NE(state, nullptr);
    EXPECT_FALSE(state->isSynced);
    EXPECT_EQ(state->opCount, 0);
}

TEST_F(SyncManagerTest, GetOutgoingMessagesClearsQueue) {
    sync_a->addPeer(node_b);

    auto messages1 = sync_a->getOutgoingMessages();
    EXPECT_EQ(messages1.size(), 1);

    auto messages2 = sync_a->getOutgoingMessages();
    EXPECT_EQ(messages2.size(), 0);  // Queue was cleared
}

TEST_F(SyncManagerTest, QueueOperationsBroadcast) {
    sync_a->addPeer(node_b);
    sync_a->getOutgoingMessages();  // Clear hello

    // Add an operation
    Operation op = makeCellSetOp(*workbook_a, cell_id, R"({"t":"n","v":"42"})");
    applyOperation(*workbook_a, op);

    // Queue broadcast
    sync_a->queueOperationsBroadcast();

    auto messages = sync_a->getOutgoingMessages();
    EXPECT_EQ(messages.size(), 1);
    EXPECT_TRUE(messages[0].isBroadcast());
    EXPECT_NE(messages[0].json.find("\"type\":\"operations\""), std::string::npos);
    EXPECT_NE(messages[0].json.find("\"batch\":"), std::string::npos);
}

TEST_F(SyncManagerTest, AckUpdatesLastSyncedHLC) {
    // A adds B as peer
    sync_a->addPeer(node_b);
    sync_a->getOutgoingMessages();  // Clear hello

    // Create an operation on A
    Operation op = makeCellSetOp(*workbook_a, cell_id, R"({"t":"n","v":"100"})");
    applyOperation(*workbook_a, op);

    // A broadcasts to B
    sync_a->queueOperationsBroadcast();
    auto ops_msg = sync_a->getOutgoingMessages();
    ASSERT_EQ(ops_msg.size(), 1);

    // Before ACK, A's view of B's lastSyncedHLC should still be low
    const auto* peerState = sync_a->getPeerSyncState(node_b);
    ASSERT_NE(peerState, nullptr);
    HLC beforeAck = peerState->lastSyncedHLC;

    // B receives operations and sends ACK
    sync_b->addPeer(node_a);
    sync_b->getOutgoingMessages();  // Clear hello
    auto result = sync_b->handleMessage(node_a, ops_msg[0].json);
    ASSERT_EQ(result.messages.size(), 1);  // ACK message

    // A receives ACK from B
    auto ackResult = sync_a->handleMessage(node_b, result.messages[0].json);
    EXPECT_FALSE(ackResult.dataModified);  // ACK doesn't modify data

    // After ACK, A's view of B's lastSyncedHLC should be updated
    peerState = sync_a->getPeerSyncState(node_b);
    ASSERT_NE(peerState, nullptr);
    EXPECT_GT(peerState->lastSyncedHLC, beforeAck);
}

TEST_F(SyncManagerTest, NullPeerIdIgnored) {
    ID null_id;
    sync_a->addPeer(null_id);
    EXPECT_EQ(sync_a->peerCount(), 0);
}

TEST_F(SyncManagerTest, UnknownMessageTypeIgnored) {
    auto result = sync_a->handleMessage(node_b, R"({"type":"unknown"})");
    EXPECT_EQ(result.messages.size(), 0);
    EXPECT_FALSE(result.dataModified);
}

TEST_F(SyncManagerTest, InvalidJSONHandledGracefully) {
    auto result = sync_a->handleMessage(node_b, "not json at all");
    EXPECT_EQ(result.messages.size(), 0);
    EXPECT_FALSE(result.dataModified);
}

// Test full sync flow between two peers
TEST_F(SyncManagerTest, FullSyncFlow) {
    // A creates some operations
    Operation op1 = makeCellSetOp(*workbook_a, cell_id, R"({"t":"n","v":"10"})");
    applyOperation(*workbook_a, op1);
    Operation op2 = makeCellSetOp(*workbook_a, cell_id, R"({"t":"n","v":"20"})");
    applyOperation(*workbook_a, op2);

    EXPECT_EQ(workbook_a->getOpLog()->size(), 2);
    EXPECT_EQ(workbook_b->getOpLog()->size(), 0);

    // A connects to B (sends hello)
    sync_a->addPeer(node_b);
    auto hello_from_a = sync_a->getOutgoingMessages();
    EXPECT_EQ(hello_from_a.size(), 1);

    // B receives hello → bidirectional sync-request + full sync-response
    sync_b->addPeer(node_a);
    auto result_from_b = sync_b->handleMessage(node_a, hello_from_a[0].json);
    ASSERT_EQ(result_from_b.messages.size(), 2);
    EXPECT_NE(result_from_b.messages[0].json.find("\"type\":\"sync-request\""), std::string::npos);
    EXPECT_NE(result_from_b.messages[1].json.find("\"type\":\"sync-response\""), std::string::npos);
    EXPECT_FALSE(result_from_b.dataModified);

    // A receives B's full sync-response (empty ops) — no data change
    auto apply_empty = sync_a->handleMessage(node_b, result_from_b.messages[1].json);
    EXPECT_FALSE(apply_empty.dataModified);

    // A receives B's sync-request and responds with full oplog
    auto result_from_a = sync_a->handleMessage(node_b, result_from_b.messages[0].json);
    ASSERT_EQ(result_from_a.messages.size(), 1);
    EXPECT_NE(result_from_a.messages[0].json.find("\"type\":\"sync-response\""), std::string::npos);
    EXPECT_FALSE(result_from_a.dataModified);

    // B receives sync-response with A's operations
    auto final_result = sync_b->handleMessage(node_a, result_from_a.messages[0].json);
    EXPECT_TRUE(final_result.dataModified);

    EXPECT_EQ(workbook_b->getSheet(sheet_id)->getCell(cell_id)->value.raw, "20");
}

}  // namespace
}  // namespace cells
