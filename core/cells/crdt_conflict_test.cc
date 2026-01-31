// CRDT Conflict Resolution Verification Tests
// Phase 15 of comprehensive unit test coverage plan
//
// This file tests CRDT conflict resolution guarantees:
// - Last-Writer-Wins for concurrent cell edits
// - Concurrent axis operations resolve deterministically
// - Concurrent range modifications merge correctly
// - HLC ordering guarantees across peers
// - Operation replay produces identical state
// - Late-joining peer catches up correctly

#include <algorithm>
#include <sstream>
#include <vector>

#include "core/cells/crdt.h"
#include "core/cells/hlc.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/oplog.h"
#include "core/cells/range.h"
#include "core/cells/style_buffer.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Test Fixture
// =============================================================================

class CRDTConflictTest : public ::testing::Test {
protected:
    void SetUp() override {
        node_a_ = ID("NodeAAAA");
        node_b_ = ID("NodeBBBB");
        node_c_ = ID("NodeCCCC");
    }

    // Create a workbook with basic structure
    std::unique_ptr<Workbook> createWorkbook(const ID& nodeId, const std::string& name = "TestWB") {
        auto wb = std::make_unique<Workbook>(generate_id(), name);
        wb->setNodeId(nodeId);

        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheet->setWorkbook(wb.get());

        // Create 3 columns
        for (int i = 0; i < 3; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = static_cast<uint32_t>(i);
            sheet->addColumn(std::move(col));
        }

        // Create 3 rows
        for (int i = 0; i < 3; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = static_cast<uint32_t>(i);
            sheet->addRow(std::move(row));
        }

        wb->addSheet(std::move(sheet));
        return wb;
    }

    // Create a workbook with shared structure (same IDs for columns/rows/cells)
    struct SharedStructure {
        ID sheetId;
        std::vector<ID> colIds;
        std::vector<ID> rowIds;
        std::vector<ID> cellIds;  // cellIds[col*3 + row]
    };

    SharedStructure createSharedStructure() {
        SharedStructure s;
        s.sheetId = generate_id();
        for (int i = 0; i < 3; i++) {
            s.colIds.push_back(generate_id());
            s.rowIds.push_back(generate_id());
        }
        for (int i = 0; i < 9; i++) {
            s.cellIds.push_back(generate_id());
        }
        return s;
    }

    std::unique_ptr<Workbook> createWorkbookWithSharedStructure(
        const ID& nodeId, const SharedStructure& shared, const std::string& name = "TestWB") {
        auto wb = std::make_unique<Workbook>(generate_id(), name);
        wb->setNodeId(nodeId);

        auto sheet = std::make_unique<Sheet>(shared.sheetId, "Sheet1");
        sheet->setWorkbook(wb.get());

        // Add columns with shared IDs
        for (size_t i = 0; i < shared.colIds.size(); i++) {
            auto col = std::make_unique<Axis>(shared.colIds[i], true);
            col->position = static_cast<uint32_t>(i);
            sheet->addColumn(std::move(col));
        }

        // Add rows with shared IDs
        for (size_t i = 0; i < shared.rowIds.size(); i++) {
            auto row = std::make_unique<Axis>(shared.rowIds[i], false);
            row->position = static_cast<uint32_t>(i);
            sheet->addRow(std::move(row));
        }

        wb->addSheet(std::move(sheet));
        return wb;
    }

    // Add a cell with shared ID to workbook
    void addSharedCell(Workbook& wb, const SharedStructure& shared, int col, int row,
                       double value) {
        auto* sheet = wb.getSheetByIndex(0);
        ID cellId = shared.cellIds[static_cast<size_t>(col * 3 + row)];
        auto cell = std::make_unique<Cell>(cellId, shared.colIds[static_cast<size_t>(col)],
                                           shared.rowIds[static_cast<size_t>(row)]);
        cell->value = CellValue(value);
        sheet->addCell(std::move(cell));
    }

    // Get cell value from workbook
    double getCellValue(Workbook& wb, const SharedStructure& shared, int col, int row) {
        auto* sheet = wb.getSheetByIndex(0);
        ID cellId = shared.cellIds[static_cast<size_t>(col * 3 + row)];
        Cell* cell = sheet->getCell(cellId);
        if (!cell)
            return -999999.0;  // Sentinel for missing cell
        return cell->value.asNumber();
    }

    ID node_a_;
    ID node_b_;
    ID node_c_;
};

// =============================================================================
// 15a: Last-Writer-Wins for concurrent cell edits
// =============================================================================

TEST_F(CRDTConflictTest, LWW_HigherWallTimeWins) {
    // Test that higher wall_time wins over lower wall_time
    auto shared = createSharedStructure();
    auto wb = createWorkbookWithSharedStructure(node_a_, shared);
    addSharedCell(*wb, shared, 0, 0, 0.0);

    ID cellId = shared.cellIds[0];

    // Create operations with different wall times
    HLC hlc_old(1000, 0, node_a_);
    HLC hlc_new(2000, 0, node_b_);

    Operation op_old(hlc_old, OpType::CELL_SET, cellId, R"({"t":"n","v":"100"})");
    Operation op_new(hlc_new, OpType::CELL_SET, cellId, R"({"t":"n","v":"200"})");

    // Apply newer first, then older
    applyOperation(*wb, op_new);
    ApplyResult result = applyOperation(*wb, op_old);

    EXPECT_EQ(result, ApplyResult::SUPERSEDED);
    EXPECT_EQ(getCellValue(*wb, shared, 0, 0), 200);
}

TEST_F(CRDTConflictTest, LWW_HigherLogicalCounterWins) {
    // Test that higher logical counter wins when wall_time is equal
    auto shared = createSharedStructure();
    auto wb = createWorkbookWithSharedStructure(node_a_, shared);
    addSharedCell(*wb, shared, 0, 0, 0.0);

    ID cellId = shared.cellIds[0];

    // Same wall time, different logical counters
    HLC hlc_low(1000, 0, node_a_);
    HLC hlc_high(1000, 5, node_a_);

    Operation op_low(hlc_low, OpType::CELL_SET, cellId, R"({"t":"n","v":"100"})");
    Operation op_high(hlc_high, OpType::CELL_SET, cellId, R"({"t":"n","v":"200"})");

    // Apply higher logical first, then lower
    applyOperation(*wb, op_high);
    ApplyResult result = applyOperation(*wb, op_low);

    EXPECT_EQ(result, ApplyResult::SUPERSEDED);
    EXPECT_EQ(getCellValue(*wb, shared, 0, 0), 200);
}

TEST_F(CRDTConflictTest, LWW_NodeIdBreaksTie) {
    // Test that node_id breaks tie when wall_time and logical are equal
    auto shared = createSharedStructure();
    auto wb = createWorkbookWithSharedStructure(node_a_, shared);
    addSharedCell(*wb, shared, 0, 0, 0.0);

    ID cellId = shared.cellIds[0];

    // Same wall time and logical, different nodes
    // NodeBBBB > NodeAAAA lexicographically
    HLC hlc_a(1000, 0, node_a_);
    HLC hlc_b(1000, 0, node_b_);

    Operation op_a(hlc_a, OpType::CELL_SET, cellId, R"({"t":"n","v":"100"})");
    Operation op_b(hlc_b, OpType::CELL_SET, cellId, R"({"t":"n","v":"200"})");

    // Apply B first, then A
    applyOperation(*wb, op_b);
    ApplyResult result = applyOperation(*wb, op_a);

    EXPECT_EQ(result, ApplyResult::SUPERSEDED);
    EXPECT_EQ(getCellValue(*wb, shared, 0, 0), 200);  // B wins (higher node_id)
}

TEST_F(CRDTConflictTest, LWW_OrderIndependence) {
    // Test that final state is the same regardless of application order
    auto shared = createSharedStructure();

    ID cellId = shared.cellIds[0];

    // Three concurrent operations
    HLC hlc_a(1000, 0, node_a_);
    HLC hlc_b(1000, 0, node_b_);
    HLC hlc_c(1000, 0, node_c_);

    Operation op_a(hlc_a, OpType::CELL_SET, cellId, R"({"t":"n","v":"100"})");
    Operation op_b(hlc_b, OpType::CELL_SET, cellId, R"({"t":"n","v":"200"})");
    Operation op_c(hlc_c, OpType::CELL_SET, cellId, R"({"t":"n","v":"300"})");

    // Test all permutations
    std::vector<Operation> ops = {op_a, op_b, op_c};
    std::vector<double> results;

    // Generate all permutations
    std::sort(ops.begin(), ops.end(),
              [](const Operation& a, const Operation& b) { return a.hlc < b.hlc; });

    do {
        auto wb = createWorkbookWithSharedStructure(node_a_, shared);
        addSharedCell(*wb, shared, 0, 0, 0.0);

        for (const auto& op : ops) {
            applyOperation(*wb, op);
        }

        results.push_back(getCellValue(*wb, shared, 0, 0));
    } while (std::next_permutation(
        ops.begin(), ops.end(),
        [](const Operation& a, const Operation& b) { return a.hlc < b.hlc; }));

    // All results should be the same (node_c wins: NodeCCCC > NodeBBBB > NodeAAAA)
    ASSERT_FALSE(results.empty());
    for (double result : results) {
        EXPECT_EQ(result, 300) << "All permutations should converge to same value";
    }
}

TEST_F(CRDTConflictTest, LWW_DuplicateOperationRejected) {
    // Test that applying the same operation twice returns ALREADY_APPLIED
    auto shared = createSharedStructure();
    auto wb = createWorkbookWithSharedStructure(node_a_, shared);
    addSharedCell(*wb, shared, 0, 0, 0.0);

    ID cellId = shared.cellIds[0];
    HLC hlc(1000, 0, node_a_);
    Operation op(hlc, OpType::CELL_SET, cellId, R"({"t":"n","v":"100"})");

    ApplyResult result1 = applyOperation(*wb, op);
    ApplyResult result2 = applyOperation(*wb, op);

    EXPECT_EQ(result1, ApplyResult::SUCCESS);
    EXPECT_EQ(result2, ApplyResult::ALREADY_APPLIED);
    EXPECT_EQ(getCellValue(*wb, shared, 0, 0), 100);
}

// =============================================================================
// 15b: Concurrent axis operations resolve deterministically
// =============================================================================

TEST_F(CRDTConflictTest, ConcurrentColumnResize_LWWApplies) {
    // Two peers resize the same column concurrently
    auto shared = createSharedStructure();
    auto wb = createWorkbookWithSharedStructure(node_a_, shared);

    ID colId = shared.colIds[0];

    // Peer A resizes to 100, Peer B resizes to 200
    HLC hlc_a(1000, 0, node_a_);
    HLC hlc_b(1000, 0, node_b_);

    std::string payload_a = R"({"size":100})";
    std::string payload_b = R"({"size":200})";

    Operation op_a(hlc_a, OpType::COL_SET, colId, payload_a);
    Operation op_b(hlc_b, OpType::COL_SET, colId, payload_b);

    // Apply in both orders and verify same result
    auto wb1 = createWorkbookWithSharedStructure(node_a_, shared);
    applyOperation(*wb1, op_a);
    applyOperation(*wb1, op_b);

    auto wb2 = createWorkbookWithSharedStructure(node_b_, shared);
    applyOperation(*wb2, op_b);
    applyOperation(*wb2, op_a);

    Axis* col1 = wb1->getSheetByIndex(0)->getColumn(colId);
    Axis* col2 = wb2->getSheetByIndex(0)->getColumn(colId);

    EXPECT_EQ(col1->size, col2->size);
    EXPECT_EQ(col1->size, 200);  // NodeBBBB wins
}

TEST_F(CRDTConflictTest, ConcurrentRowResize_LWWApplies) {
    // Two peers resize the same row concurrently
    auto shared = createSharedStructure();
    auto wb = createWorkbookWithSharedStructure(node_a_, shared);

    ID rowId = shared.rowIds[0];

    HLC hlc_a(1000, 0, node_a_);
    HLC hlc_b(1000, 0, node_b_);

    std::string payload_a = R"({"size":50})";
    std::string payload_b = R"({"size":80})";

    Operation op_a(hlc_a, OpType::ROW_SET, rowId, payload_a);
    Operation op_b(hlc_b, OpType::ROW_SET, rowId, payload_b);

    applyOperation(*wb, op_a);
    applyOperation(*wb, op_b);

    Axis* row = wb->getSheetByIndex(0)->getRow(rowId);
    EXPECT_EQ(row->size, 80);  // NodeBBBB wins
}

TEST_F(CRDTConflictTest, ConcurrentColumnPositionChange_LWWApplies) {
    // Two peers move the same column to different positions
    auto shared = createSharedStructure();

    ID colId = shared.colIds[1];  // Middle column

    HLC hlc_a(1000, 0, node_a_);
    HLC hlc_b(1000, 0, node_b_);

    std::string payload_a = R"({"pos":0})";
    std::string payload_b = R"({"pos":2})";

    Operation op_a(hlc_a, OpType::COL_SET, colId, payload_a);
    Operation op_b(hlc_b, OpType::COL_SET, colId, payload_b);

    // Test both orderings converge
    auto wb1 = createWorkbookWithSharedStructure(node_a_, shared);
    applyOperation(*wb1, op_a);
    applyOperation(*wb1, op_b);

    auto wb2 = createWorkbookWithSharedStructure(node_b_, shared);
    applyOperation(*wb2, op_b);
    applyOperation(*wb2, op_a);

    Axis* col1 = wb1->getSheetByIndex(0)->getColumn(colId);
    Axis* col2 = wb2->getSheetByIndex(0)->getColumn(colId);

    EXPECT_EQ(col1->position, col2->position);
    EXPECT_EQ(col1->position, 2u);  // NodeBBBB wins
}

TEST_F(CRDTConflictTest, ConcurrentHideShowColumn_LWWApplies) {
    // One peer hides column, another shows it
    auto shared = createSharedStructure();

    ID colId = shared.colIds[0];

    HLC hlc_a(1000, 0, node_a_);
    HLC hlc_b(1000, 0, node_b_);

    std::string payload_hide = R"({"hidden":true})";
    std::string payload_show = R"({"hidden":false})";

    Operation op_hide(hlc_a, OpType::COL_SET, colId, payload_hide);
    Operation op_show(hlc_b, OpType::COL_SET, colId, payload_show);

    auto wb = createWorkbookWithSharedStructure(node_a_, shared);
    applyOperation(*wb, op_hide);
    applyOperation(*wb, op_show);

    Axis* col = wb->getSheetByIndex(0)->getColumn(colId);
    EXPECT_FALSE(col->hidden());  // NodeBBBB wins (show operation)
}

// =============================================================================
// 15c: Concurrent range modifications merge correctly
// =============================================================================

TEST_F(CRDTConflictTest, ConcurrentRangeStyleUpdate_LWWApplies) {
    // Two peers update the same range's style concurrently
    auto shared = createSharedStructure();
    auto wb = createWorkbookWithSharedStructure(node_a_, shared);

    // Create a range with low HLC
    ID rangeId = generate_id();
    std::string rangePayload = "{\"startCol\":\"" + shared.colIds[0].toString() + "\",";
    rangePayload += "\"startRow\":\"" + shared.rowIds[0].toString() + "\",";
    rangePayload += "\"endCol\":\"" + shared.colIds[1].toString() + "\",";
    rangePayload += "\"endRow\":\"" + shared.rowIds[1].toString() + "\",";
    rangePayload += "\"flags\":2}";  // STYLE flag

    // Use fixed HLCs to ensure proper ordering
    HLC hlc_create(1000, 0, node_a_);
    Operation createOp(hlc_create, OpType::RANGE_SET, rangeId, rangePayload);
    createOp.sheetId = shared.sheetId;
    ApplyResult create_result = applyOperation(*wb, createOp);
    ASSERT_EQ(create_result, ApplyResult::SUCCESS) << "Range creation should succeed";

    // Create style payloads with different colors
    StyleBuffer styleRed;
    styleRed.setBgColorHex("#FF0000");
    StyleBuffer styleBlue;
    styleBlue.setBgColorHex("#0000FF");

    // Create operations with higher HLCs for LWW testing
    // Both have same wall_time=2000, node_b_ wins lexicographically
    HLC hlc_a(2000, 0, node_a_);
    HLC hlc_b(2000, 0, node_b_);

    std::string payloadRed = "{\"sty\":\"" + styleRed.toBase64() + "\"}";
    std::string payloadBlue = "{\"sty\":\"" + styleBlue.toBase64() + "\"}";

    Operation op_red(hlc_a, OpType::RANGE_SET, rangeId, payloadRed);
    op_red.sheetId = shared.sheetId;

    Operation op_blue(hlc_b, OpType::RANGE_SET, rangeId, payloadBlue);
    op_blue.sheetId = shared.sheetId;

    ApplyResult result_red = applyOperation(*wb, op_red);
    ApplyResult result_blue = applyOperation(*wb, op_blue);

    EXPECT_EQ(result_red, ApplyResult::SUCCESS) << "Red style should apply successfully";
    EXPECT_EQ(result_blue, ApplyResult::SUCCESS) << "Blue style should apply (higher node ID)";

    const Range* range = wb->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    ASSERT_TRUE(range->style.has_value()) << "Range should have style after applying style ops";
    EXPECT_EQ(range->style->getBgColorHex(), "#0000FF");  // NodeBBBB wins
}

TEST_F(CRDTConflictTest, ConcurrentRangeCreationDifferentIds) {
    // Two peers create ranges with different IDs (both should exist)
    auto shared = createSharedStructure();
    auto wb = createWorkbookWithSharedStructure(node_a_, shared);

    ID rangeId_a = generate_id();
    ID rangeId_b = generate_id();

    std::string rangePayload_a = "{\"startCol\":\"" + shared.colIds[0].toString() + "\",";
    rangePayload_a += "\"startRow\":\"" + shared.rowIds[0].toString() + "\",";
    rangePayload_a += "\"endCol\":\"" + shared.colIds[0].toString() + "\",";
    rangePayload_a += "\"endRow\":\"" + shared.rowIds[0].toString() + "\",";
    rangePayload_a += "\"flags\":2}";

    std::string rangePayload_b = "{\"startCol\":\"" + shared.colIds[1].toString() + "\",";
    rangePayload_b += "\"startRow\":\"" + shared.rowIds[1].toString() + "\",";
    rangePayload_b += "\"endCol\":\"" + shared.colIds[1].toString() + "\",";
    rangePayload_b += "\"endRow\":\"" + shared.rowIds[1].toString() + "\",";
    rangePayload_b += "\"flags\":2}";

    HLC hlc_a(1000, 0, node_a_);
    HLC hlc_b(1000, 0, node_b_);

    Operation op_a(hlc_a, OpType::RANGE_SET, rangeId_a, rangePayload_a);
    op_a.sheetId = shared.sheetId;
    Operation op_b(hlc_b, OpType::RANGE_SET, rangeId_b, rangePayload_b);
    op_b.sheetId = shared.sheetId;

    applyOperation(*wb, op_a);
    applyOperation(*wb, op_b);

    // Both ranges should exist
    EXPECT_NE(wb->getRange(rangeId_a), nullptr);
    EXPECT_NE(wb->getRange(rangeId_b), nullptr);
}

TEST_F(CRDTConflictTest, ConcurrentRangeDeleteAndModify_LWWOrder) {
    // Test LWW semantics for range delete vs modify operations
    // Delete with lower HLC, modify with higher HLC
    auto shared = createSharedStructure();
    auto wb = createWorkbookWithSharedStructure(node_a_, shared);

    // Create a range first
    ID rangeId = generate_id();
    std::string createPayload = "{\"startCol\":\"" + shared.colIds[0].toString() + "\",";
    createPayload += "\"startRow\":\"" + shared.rowIds[0].toString() + "\",";
    createPayload += "\"endCol\":\"" + shared.colIds[1].toString() + "\",";
    createPayload += "\"endRow\":\"" + shared.rowIds[1].toString() + "\",";
    createPayload += "\"flags\":2}";

    HLC hlc_create(500, 0, node_a_);
    Operation createOp(hlc_create, OpType::RANGE_SET, rangeId, createPayload);
    createOp.sheetId = shared.sheetId;
    applyOperation(*wb, createOp);

    // Delete with lower HLC, SET with higher HLC
    HLC hlc_delete(1000, 0, node_a_);
    HLC hlc_set(2000, 0, node_b_);  // Higher HLC wins

    std::string deletePayload = "{\"sheet\":\"" + shared.sheetId.toString() + "\"}";
    Operation op_delete(hlc_delete, OpType::RANGE_DELETE, rangeId, deletePayload);
    op_delete.sheetId = shared.sheetId;

    // Full SET operation with corners to allow recreation
    StyleBuffer style;
    style.setBold(true);
    std::string setPayload = "{\"startCol\":\"" + shared.colIds[0].toString() + "\",";
    setPayload += "\"startRow\":\"" + shared.rowIds[0].toString() + "\",";
    setPayload += "\"endCol\":\"" + shared.colIds[1].toString() + "\",";
    setPayload += "\"endRow\":\"" + shared.rowIds[1].toString() + "\",";
    setPayload += "\"flags\":2,\"sty\":\"" + style.toBase64() + "\"}";

    Operation op_set(hlc_set, OpType::RANGE_SET, rangeId, setPayload);
    op_set.sheetId = shared.sheetId;

    // Apply SET first (higher HLC)
    ApplyResult result1 = applyOperation(*wb, op_set);
    EXPECT_EQ(result1, ApplyResult::SUCCESS);
    EXPECT_NE(wb->getRange(rangeId), nullptr);

    // Apply delete (lower HLC) - should report that SET resurrected it
    ApplyResult result2 = applyOperation(*wb, op_delete);
    EXPECT_EQ(result2, ApplyResult::RESURRECTED);

    // Range should still exist (SET with higher HLC wins)
    EXPECT_NE(wb->getRange(rangeId), nullptr);
}

// =============================================================================
// 15d: HLC ordering guarantees across peers
// =============================================================================

TEST_F(CRDTConflictTest, HLC_MonotonicWithinPeer) {
    // HLC should always increase within a single peer
    auto wb = createWorkbook(node_a_);

    std::vector<HLC> hlcs;
    for (int i = 0; i < 100; i++) {
        hlcs.push_back(wb->getCurrentHLC());
    }

    for (size_t i = 1; i < hlcs.size(); i++) {
        EXPECT_LT(hlcs[i - 1], hlcs[i]) << "HLC should strictly increase at index " << i;
    }
}

TEST_F(CRDTConflictTest, HLC_UpdateOnReceive) {
    // Local HLC should advance when receiving a higher remote HLC
    auto wb_local = createWorkbook(node_a_);
    auto wb_remote = createWorkbook(node_b_);

    // Generate some HLCs on remote (simulating time passing)
    HLC remote_hlc;
    for (int i = 0; i < 10; i++) {
        remote_hlc = wb_remote->getCurrentHLC();
    }

    // Apply an operation from remote
    auto shared = createSharedStructure();
    ID cellId = shared.cellIds[0];

    // Add the cell structure to local workbook
    auto* sheet = wb_local->getSheetByIndex(0);
    auto col = std::make_unique<Axis>(shared.colIds[0], true);
    auto row = std::make_unique<Axis>(shared.rowIds[0], false);
    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));
    auto cell = std::make_unique<Cell>(cellId, shared.colIds[0], shared.rowIds[0]);
    cell->value = CellValue(0.0);
    sheet->addCell(std::move(cell));

    Operation op(remote_hlc, OpType::CELL_SET, cellId, R"({"t":"n","v":"100"})");
    applyOperation(*wb_local, op);

    // Local HLC should have advanced
    HLC local_after = wb_local->getOpLog()->getCurrentHLC();
    EXPECT_GE(local_after, remote_hlc) << "Local HLC should be >= received HLC";
}

TEST_F(CRDTConflictTest, HLC_TotalOrdering) {
    // Any two HLCs should be comparable (total ordering)
    std::vector<HLC> hlcs;

    // Generate HLCs from different nodes at various times
    auto wb_a = createWorkbook(node_a_);
    auto wb_b = createWorkbook(node_b_);
    auto wb_c = createWorkbook(node_c_);

    for (int i = 0; i < 5; i++) {
        hlcs.push_back(wb_a->getCurrentHLC());
        hlcs.push_back(wb_b->getCurrentHLC());
        hlcs.push_back(wb_c->getCurrentHLC());
    }

    // Verify total ordering: for any two HLCs, exactly one of <, ==, > holds
    for (size_t i = 0; i < hlcs.size(); i++) {
        for (size_t j = 0; j < hlcs.size(); j++) {
            int lt = (hlcs[i] < hlcs[j]) ? 1 : 0;
            int eq = (hlcs[i] == hlcs[j]) ? 1 : 0;
            int gt = (hlcs[i] > hlcs[j]) ? 1 : 0;

            EXPECT_EQ(lt + eq + gt, 1)
                << "Exactly one comparison should be true for i=" << i << ", j=" << j;
        }
    }
}

TEST_F(CRDTConflictTest, HLC_CausalOrdering) {
    // If operation A is applied before B on the same peer, HLC(A) < HLC(B)
    auto shared = createSharedStructure();
    auto wb = createWorkbookWithSharedStructure(node_a_, shared);
    addSharedCell(*wb, shared, 0, 0, 0.0);

    ID cellId = shared.cellIds[0];

    Operation op1 = makeCellSetOp(*wb, cellId, R"({"t":"n","v":"1"})");
    applyOperation(*wb, op1);

    Operation op2 = makeCellSetOp(*wb, cellId, R"({"t":"n","v":"2"})");
    applyOperation(*wb, op2);

    Operation op3 = makeCellSetOp(*wb, cellId, R"({"t":"n","v":"3"})");
    applyOperation(*wb, op3);

    EXPECT_LT(op1.hlc, op2.hlc);
    EXPECT_LT(op2.hlc, op3.hlc);
}

// =============================================================================
// 15e: Operation replay produces identical state
// =============================================================================

TEST_F(CRDTConflictTest, Replay_SameOrderProducesSameState) {
    // Replaying operations in the same order produces identical state
    auto shared = createSharedStructure();
    auto wb1 = createWorkbookWithSharedStructure(node_a_, shared);
    auto wb2 = createWorkbookWithSharedStructure(node_a_, shared);

    // Add initial cells
    addSharedCell(*wb1, shared, 0, 0, 0.0);
    addSharedCell(*wb1, shared, 1, 1, 0.0);
    addSharedCell(*wb2, shared, 0, 0, 0.0);
    addSharedCell(*wb2, shared, 1, 1, 0.0);

    // Generate operations on wb1
    std::vector<Operation> ops;
    ops.push_back(makeCellSetOp(*wb1, shared.cellIds[0], R"({"t":"n","v":"10"})"));
    ops.push_back(makeCellSetOp(*wb1, shared.cellIds[4], R"({"t":"n","v":"20"})"));
    ops.push_back(makeCellSetOp(*wb1, shared.cellIds[0], R"({"t":"n","v":"30"})"));

    // Apply to wb1
    for (const auto& op : ops) {
        applyOperation(*wb1, op);
    }

    // Replay on wb2
    for (const auto& op : ops) {
        applyOperation(*wb2, op);
    }

    // Both should have identical cell values
    EXPECT_EQ(getCellValue(*wb1, shared, 0, 0), getCellValue(*wb2, shared, 0, 0));
    EXPECT_EQ(getCellValue(*wb1, shared, 1, 1), getCellValue(*wb2, shared, 1, 1));
    EXPECT_EQ(getCellValue(*wb1, shared, 0, 0), 30);
    EXPECT_EQ(getCellValue(*wb1, shared, 1, 1), 20);
}

TEST_F(CRDTConflictTest, Replay_DifferentOrderProducesSameState) {
    // Replaying operations in different order still produces same final state
    auto shared = createSharedStructure();

    // Create operations with explicit HLCs for deterministic ordering
    ID cellId = shared.cellIds[0];

    HLC hlc1(1000, 0, node_a_);
    HLC hlc2(2000, 0, node_a_);
    HLC hlc3(3000, 0, node_a_);

    Operation op1(hlc1, OpType::CELL_SET, cellId, R"({"t":"n","v":"10"})");
    Operation op2(hlc2, OpType::CELL_SET, cellId, R"({"t":"n","v":"20"})");
    Operation op3(hlc3, OpType::CELL_SET, cellId, R"({"t":"n","v":"30"})");

    std::vector<Operation> ops = {op1, op2, op3};

    // Apply in all permutations
    std::vector<double> results;
    std::sort(ops.begin(), ops.end(),
              [](const Operation& a, const Operation& b) { return a.hlc < b.hlc; });

    do {
        auto wb = createWorkbookWithSharedStructure(node_a_, shared);
        addSharedCell(*wb, shared, 0, 0, 0.0);

        for (const auto& op : ops) {
            applyOperation(*wb, op);
        }

        results.push_back(getCellValue(*wb, shared, 0, 0));
    } while (std::next_permutation(
        ops.begin(), ops.end(),
        [](const Operation& a, const Operation& b) { return a.hlc < b.hlc; }));

    // All results should be 30 (op3 is the latest)
    for (double result : results) {
        EXPECT_EQ(result, 30);
    }
}

TEST_F(CRDTConflictTest, Replay_WithMixedOperationTypes) {
    // Replay with cell SET, axis SET, and range SET operations
    auto shared = createSharedStructure();

    std::vector<Operation> ops;

    // Cell operation
    HLC hlc1(1000, 0, node_a_);
    ops.emplace_back(hlc1, OpType::CELL_SET, shared.cellIds[0],
                     "{\"col\":\"" + shared.colIds[0].toString() + "\",\"row\":\"" +
                         shared.rowIds[0].toString() + "\",\"t\":\"n\",\"v\":\"42\"}");
    ops.back().sheetId = shared.sheetId;

    // Column resize operation
    HLC hlc2(2000, 0, node_a_);
    ops.emplace_back(hlc2, OpType::COL_SET, shared.colIds[0], R"({"size":150})");

    // Row resize operation
    HLC hlc3(3000, 0, node_a_);
    ops.emplace_back(hlc3, OpType::ROW_SET, shared.rowIds[0], R"({"size":40})");

    // Apply in forward order
    auto wb1 = createWorkbookWithSharedStructure(node_a_, shared);
    for (const auto& op : ops) {
        applyOperation(*wb1, op);
    }

    // Apply in reverse order
    auto wb2 = createWorkbookWithSharedStructure(node_b_, shared);
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        applyOperation(*wb2, *it);
    }

    // Both should have same final state
    Cell* cell1 = wb1->getSheetByIndex(0)->getCell(shared.cellIds[0]);
    Cell* cell2 = wb2->getSheetByIndex(0)->getCell(shared.cellIds[0]);
    ASSERT_NE(cell1, nullptr);
    ASSERT_NE(cell2, nullptr);
    EXPECT_EQ(cell1->value.asNumber(), cell2->value.asNumber());

    Axis* col1 = wb1->getSheetByIndex(0)->getColumn(shared.colIds[0]);
    Axis* col2 = wb2->getSheetByIndex(0)->getColumn(shared.colIds[0]);
    EXPECT_EQ(col1->size, col2->size);

    Axis* row1 = wb1->getSheetByIndex(0)->getRow(shared.rowIds[0]);
    Axis* row2 = wb2->getSheetByIndex(0)->getRow(shared.rowIds[0]);
    EXPECT_EQ(row1->size, row2->size);
}

// =============================================================================
// 15f: Late-joining peer catches up correctly
// =============================================================================

TEST_F(CRDTConflictTest, LateJoin_ReceivesAllOperations) {
    // A peer that joins late should be able to catch up by receiving all operations
    auto shared = createSharedStructure();

    // Peer A makes several edits
    auto wb_a = createWorkbookWithSharedStructure(node_a_, shared);
    addSharedCell(*wb_a, shared, 0, 0, 0.0);
    addSharedCell(*wb_a, shared, 1, 1, 0.0);

    std::vector<Operation> ops;
    ops.push_back(makeCellSetOp(*wb_a, shared.cellIds[0], R"({"t":"n","v":"10"})"));
    ops.push_back(makeCellSetOp(*wb_a, shared.cellIds[4], R"({"t":"n","v":"20"})"));
    ops.push_back(makeCellSetOp(*wb_a, shared.cellIds[0], R"({"t":"n","v":"30"})"));

    for (const auto& op : ops) {
        applyOperation(*wb_a, op);
    }

    // Peer B joins late with empty state
    auto wb_b = createWorkbookWithSharedStructure(node_b_, shared);
    addSharedCell(*wb_b, shared, 0, 0, 0.0);
    addSharedCell(*wb_b, shared, 1, 1, 0.0);

    // B receives all operations from A's oplog
    auto allOps = wb_a->getOpLog()->getAllOperations();
    for (const auto& op : allOps) {
        applyOperation(*wb_b, op);
    }

    // B should now have the same state as A
    EXPECT_EQ(getCellValue(*wb_a, shared, 0, 0), getCellValue(*wb_b, shared, 0, 0));
    EXPECT_EQ(getCellValue(*wb_a, shared, 1, 1), getCellValue(*wb_b, shared, 1, 1));
}

TEST_F(CRDTConflictTest, LateJoin_OperationsSinceHLC) {
    // Late-joining peer can request only operations since their last known HLC
    auto shared = createSharedStructure();

    auto wb_a = createWorkbookWithSharedStructure(node_a_, shared);
    addSharedCell(*wb_a, shared, 0, 0, 0.0);

    // First batch of operations
    std::vector<Operation> batch1;
    batch1.push_back(makeCellSetOp(*wb_a, shared.cellIds[0], R"({"t":"n","v":"10"})"));
    batch1.push_back(makeCellSetOp(*wb_a, shared.cellIds[0], R"({"t":"n","v":"20"})"));

    for (const auto& op : batch1) {
        applyOperation(*wb_a, op);
    }

    // Capture HLC after first batch
    HLC syncPoint = wb_a->getOpLog()->getCurrentHLC();

    // Second batch of operations
    std::vector<Operation> batch2;
    batch2.push_back(makeCellSetOp(*wb_a, shared.cellIds[0], R"({"t":"n","v":"30"})"));
    batch2.push_back(makeCellSetOp(*wb_a, shared.cellIds[0], R"({"t":"n","v":"40"})"));

    for (const auto& op : batch2) {
        applyOperation(*wb_a, op);
    }

    // Peer B already has batch1, requests only operations since syncPoint
    auto wb_b = createWorkbookWithSharedStructure(node_b_, shared);
    addSharedCell(*wb_b, shared, 0, 0, 0.0);

    // Apply batch1
    for (const auto& op : batch1) {
        applyOperation(*wb_b, op);
    }

    // Get operations since syncPoint
    auto newOps = wb_a->getOpLog()->getOperationsSince(syncPoint);
    EXPECT_EQ(newOps.size(), 2u);  // Should only get batch2

    for (const auto& op : newOps) {
        applyOperation(*wb_b, op);
    }

    // B should have the same final state as A
    EXPECT_EQ(getCellValue(*wb_a, shared, 0, 0), getCellValue(*wb_b, shared, 0, 0));
    EXPECT_EQ(getCellValue(*wb_a, shared, 0, 0), 40);
}

TEST_F(CRDTConflictTest, LateJoin_ConcurrentEditsWhileJoining) {
    // Late-joining peer receives operations while existing peer continues editing
    auto shared = createSharedStructure();

    auto wb_a = createWorkbookWithSharedStructure(node_a_, shared);
    addSharedCell(*wb_a, shared, 0, 0, 0.0);

    // Initial operations
    Operation op1 = makeCellSetOp(*wb_a, shared.cellIds[0], R"({"t":"n","v":"10"})");
    applyOperation(*wb_a, op1);

    // B joins and starts receiving
    auto wb_b = createWorkbookWithSharedStructure(node_b_, shared);
    addSharedCell(*wb_b, shared, 0, 0, 0.0);

    // B receives op1
    applyOperation(*wb_b, op1);

    // Meanwhile, A makes more edits
    Operation op2 = makeCellSetOp(*wb_a, shared.cellIds[0], R"({"t":"n","v":"20"})");
    applyOperation(*wb_a, op2);

    // B also makes an edit (concurrent with A's op2)
    Operation op_b = makeCellSetOp(*wb_b, shared.cellIds[0], R"({"t":"n","v":"15"})");
    applyOperation(*wb_b, op_b);

    // Sync: B receives A's op2, A receives B's op_b
    applyOperation(*wb_b, op2);
    applyOperation(*wb_a, op_b);

    // Both should converge to the same value
    EXPECT_EQ(getCellValue(*wb_a, shared, 0, 0), getCellValue(*wb_b, shared, 0, 0));
}

TEST_F(CRDTConflictTest, LateJoin_ThreePeersWithStaggeredJoin) {
    // Three peers join at different times and all converge
    auto shared = createSharedStructure();

    // Peer A starts
    auto wb_a = createWorkbookWithSharedStructure(node_a_, shared);
    addSharedCell(*wb_a, shared, 0, 0, 0.0);

    Operation op_a1 = makeCellSetOp(*wb_a, shared.cellIds[0], R"({"t":"n","v":"100"})");
    applyOperation(*wb_a, op_a1);

    // Peer B joins, receives A's operations
    auto wb_b = createWorkbookWithSharedStructure(node_b_, shared);
    addSharedCell(*wb_b, shared, 0, 0, 0.0);
    applyOperation(*wb_b, op_a1);

    // B makes an edit
    Operation op_b1 = makeCellSetOp(*wb_b, shared.cellIds[0], R"({"t":"n","v":"200"})");
    applyOperation(*wb_b, op_b1);
    applyOperation(*wb_a, op_b1);  // Sync to A

    // Peer C joins late, receives all operations
    auto wb_c = createWorkbookWithSharedStructure(node_c_, shared);
    addSharedCell(*wb_c, shared, 0, 0, 0.0);
    applyOperation(*wb_c, op_a1);
    applyOperation(*wb_c, op_b1);

    // C makes an edit
    Operation op_c1 = makeCellSetOp(*wb_c, shared.cellIds[0], R"({"t":"n","v":"300"})");
    applyOperation(*wb_c, op_c1);
    applyOperation(*wb_a, op_c1);
    applyOperation(*wb_b, op_c1);

    // All three should converge
    double val_a = getCellValue(*wb_a, shared, 0, 0);
    double val_b = getCellValue(*wb_b, shared, 0, 0);
    double val_c = getCellValue(*wb_c, shared, 0, 0);

    EXPECT_EQ(val_a, val_b);
    EXPECT_EQ(val_b, val_c);
    EXPECT_EQ(val_c, 300);  // C's edit was the latest
}

TEST_F(CRDTConflictTest, LateJoin_StructureCreationOperations) {
    // Late-joining peer receives column/row creation operations
    auto shared = createSharedStructure();

    // Peer A creates additional structure
    auto wb_a = createWorkbookWithSharedStructure(node_a_, shared);

    ID newColId = generate_id();
    ID newRowId = generate_id();

    std::string colPayload = R"({"pos":3})";
    std::string rowPayload = R"({"pos":3})";

    Operation op_col = makeColSetOp(*wb_a, newColId, shared.sheetId, colPayload);
    Operation op_row = makeRowSetOp(*wb_a, newRowId, shared.sheetId, rowPayload);

    applyOperation(*wb_a, op_col);
    applyOperation(*wb_a, op_row);

    // Peer B joins and receives the operations
    auto wb_b = createWorkbookWithSharedStructure(node_b_, shared);

    applyOperation(*wb_b, op_col);
    applyOperation(*wb_b, op_row);

    // Both should have the new column and row
    Sheet* sheet_a = wb_a->getSheetByIndex(0);
    Sheet* sheet_b = wb_b->getSheetByIndex(0);

    EXPECT_NE(sheet_a->getColumn(newColId), nullptr);
    EXPECT_NE(sheet_b->getColumn(newColId), nullptr);
    EXPECT_NE(sheet_a->getRow(newRowId), nullptr);
    EXPECT_NE(sheet_b->getRow(newRowId), nullptr);

    EXPECT_EQ(sheet_a->getColumn(newColId)->position, sheet_b->getColumn(newColId)->position);
    EXPECT_EQ(sheet_a->getRow(newRowId)->position, sheet_b->getRow(newRowId)->position);
}

TEST_F(CRDTConflictTest, LateJoin_EmptyOpLogForNewWorkbook) {
    // A brand new workbook has empty oplog, late joiner starts fresh
    auto wb = createWorkbook(node_a_);

    const OpLog* oplog = wb->getOpLog();
    EXPECT_EQ(oplog->size(), 0u);
    EXPECT_TRUE(oplog->empty());

    // Getting operations since zero HLC returns nothing
    auto ops = oplog->getOperationsSince(HLC());
    EXPECT_EQ(ops.size(), 0u);
}

}  // namespace
}  // namespace cells
