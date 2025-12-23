// Presence tests

#include "core/net/include/Presence.h"

#include <gtest/gtest.h>

namespace cells::net {
namespace {

TEST(PresenceDataTest, ToJSONBasic) {
    PresenceData data;
    data.peer_id = "peer123";
    data.name = "Test User";
    data.color = "#E53935";
    data.sheet_id = "sheet1";
    data.timestamp = 1234567890;

    std::string json = data.toJSON();

    EXPECT_NE(json.find("\"type\":\"presence\""), std::string::npos);
    EXPECT_NE(json.find("\"peer_id\":\"peer123\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"Test User\""), std::string::npos);
    EXPECT_NE(json.find("\"color\":\"#E53935\""), std::string::npos);
    EXPECT_NE(json.find("\"sheet_id\":\"sheet1\""), std::string::npos);
    EXPECT_NE(json.find("\"timestamp\":1234567890"), std::string::npos);
    EXPECT_NE(json.find("\"cursor\":null"), std::string::npos);
    EXPECT_NE(json.find("\"selection\":null"), std::string::npos);
    EXPECT_NE(json.find("\"mouse\":null"), std::string::npos);
}

TEST(PresenceDataTest, ToJSONWithCursor) {
    PresenceData data;
    data.peer_id = "peer123";
    data.name = "Test User";
    data.color = "#E53935";
    data.has_cursor = true;
    data.cursor.col = "col_A";
    data.cursor.row = "row_1";
    data.timestamp = 1234567890;

    std::string json = data.toJSON();

    EXPECT_NE(json.find("\"cursor\":{\"col\":\"col_A\",\"row\":\"row_1\"}"), std::string::npos);
}

TEST(PresenceDataTest, ToJSONWithSelection) {
    PresenceData data;
    data.peer_id = "peer123";
    data.name = "Test User";
    data.color = "#E53935";
    data.has_selection = true;
    data.selection.start.col = "col_A";
    data.selection.start.row = "row_1";
    data.selection.end.col = "col_C";
    data.selection.end.row = "row_5";
    data.timestamp = 1234567890;

    std::string json = data.toJSON();

    EXPECT_NE(json.find("\"selection\":{"), std::string::npos);
    EXPECT_NE(json.find("\"start\":{\"col\":\"col_A\",\"row\":\"row_1\"}"), std::string::npos);
    EXPECT_NE(json.find("\"end\":{\"col\":\"col_C\",\"row\":\"row_5\"}"), std::string::npos);
}

TEST(PresenceDataTest, ToJSONWithMouse) {
    PresenceData data;
    data.peer_id = "peer123";
    data.name = "Test User";
    data.color = "#E53935";
    data.has_mouse = true;
    data.mouse.x = 100.5;
    data.mouse.y = 200.25;
    data.timestamp = 1234567890;

    std::string json = data.toJSON();

    EXPECT_NE(json.find("\"mouse\":{\"x\":100.5,\"y\":200.25}"), std::string::npos);
}

TEST(PresenceDataTest, FromJSONBasic) {
    std::string json = R"({
        "type": "presence",
        "peer_id": "peer456",
        "name": "Another User",
        "color": "#1E88E5",
        "sheet_id": "sheet2",
        "cursor": null,
        "selection": null,
        "mouse": null,
        "timestamp": 9876543210
    })";

    PresenceData data;
    EXPECT_TRUE(PresenceData::fromJSON(json, data));
    EXPECT_EQ(data.peer_id, "peer456");
    EXPECT_EQ(data.name, "Another User");
    EXPECT_EQ(data.color, "#1E88E5");
    EXPECT_EQ(data.sheet_id, "sheet2");
    EXPECT_EQ(data.timestamp, 9876543210);
    EXPECT_FALSE(data.has_cursor);
    EXPECT_FALSE(data.has_selection);
    EXPECT_FALSE(data.has_mouse);
}

TEST(PresenceDataTest, FromJSONWithCursor) {
    std::string json = R"({
        "type": "presence",
        "peer_id": "peer789",
        "name": "User",
        "color": "#8E24AA",
        "sheet_id": "s1",
        "cursor": {"col": "ABC", "row": "XYZ"},
        "selection": null,
        "mouse": null,
        "timestamp": 123
    })";

    PresenceData data;
    EXPECT_TRUE(PresenceData::fromJSON(json, data));
    EXPECT_TRUE(data.has_cursor);
    EXPECT_EQ(data.cursor.col, "ABC");
    EXPECT_EQ(data.cursor.row, "XYZ");
}

TEST(PresenceDataTest, FromJSONWithSelection) {
    std::string json = R"({
        "type": "presence",
        "peer_id": "peerSel",
        "name": "Selector",
        "color": "#00897B",
        "sheet_id": "s2",
        "cursor": null,
        "selection": {
            "start": {"col": "A", "row": "1"},
            "end": {"col": "D", "row": "10"}
        },
        "mouse": null,
        "timestamp": 456
    })";

    PresenceData data;
    EXPECT_TRUE(PresenceData::fromJSON(json, data));
    EXPECT_TRUE(data.has_selection);
    EXPECT_EQ(data.selection.start.col, "A");
    EXPECT_EQ(data.selection.start.row, "1");
    EXPECT_EQ(data.selection.end.col, "D");
    EXPECT_EQ(data.selection.end.row, "10");
}

TEST(PresenceDataTest, FromJSONWithMouse) {
    std::string json = R"({
        "type": "presence",
        "peer_id": "peerMouse",
        "name": "Mover",
        "color": "#F57C00",
        "sheet_id": "s3",
        "cursor": null,
        "selection": null,
        "mouse": {"x": 50.5, "y": 100.75},
        "timestamp": 789
    })";

    PresenceData data;
    EXPECT_TRUE(PresenceData::fromJSON(json, data));
    EXPECT_TRUE(data.has_mouse);
    EXPECT_DOUBLE_EQ(data.mouse.x, 50.5);
    EXPECT_DOUBLE_EQ(data.mouse.y, 100.75);
}

TEST(PresenceDataTest, FromJSONInvalidType) {
    std::string json = R"({"type": "not_presence", "peer_id": "x"})";

    PresenceData data;
    EXPECT_FALSE(PresenceData::fromJSON(json, data));
}

TEST(PresenceDataTest, Roundtrip) {
    PresenceData original;
    original.peer_id = "roundtrip_peer";
    original.name = "Roundtrip User";
    original.color = "#5E35B1";
    original.sheet_id = "test_sheet";
    original.has_cursor = true;
    original.cursor.col = "col123";
    original.cursor.row = "row456";
    original.has_selection = true;
    original.selection.start.col = "sA";
    original.selection.start.row = "s1";
    original.selection.end.col = "eB";
    original.selection.end.row = "e2";
    original.has_mouse = true;
    original.mouse.x = 123.456;
    original.mouse.y = 789.012;
    original.timestamp = 1111222233334444;

    std::string json = original.toJSON();

    PresenceData parsed;
    EXPECT_TRUE(PresenceData::fromJSON(json, parsed));

    EXPECT_EQ(parsed.peer_id, original.peer_id);
    EXPECT_EQ(parsed.name, original.name);
    EXPECT_EQ(parsed.color, original.color);
    EXPECT_EQ(parsed.sheet_id, original.sheet_id);
    EXPECT_EQ(parsed.has_cursor, original.has_cursor);
    EXPECT_EQ(parsed.cursor, original.cursor);
    EXPECT_EQ(parsed.has_selection, original.has_selection);
    EXPECT_EQ(parsed.selection, original.selection);
    EXPECT_EQ(parsed.has_mouse, original.has_mouse);
    EXPECT_DOUBLE_EQ(parsed.mouse.x, original.mouse.x);
    EXPECT_DOUBLE_EQ(parsed.mouse.y, original.mouse.y);
    EXPECT_EQ(parsed.timestamp, original.timestamp);
}

// ============================================================================
// PresenceManager tests
// ============================================================================

TEST(PresenceManagerTest, Initialize) {
    PresenceManager manager;
    manager.initialize("test_peer", "Test Name");

    EXPECT_EQ(manager.getLocalPeerId(), "test_peer");
    EXPECT_EQ(manager.getLocalName(), "Test Name");
    EXPECT_FALSE(manager.getLocalColor().empty());
}

TEST(PresenceManagerTest, InitializeWithAutoName) {
    PresenceManager manager;
    manager.initialize("auto_name_peer");

    EXPECT_EQ(manager.getLocalPeerId(), "auto_name_peer");
    EXPECT_FALSE(manager.getLocalName().empty());
    // Name should be "Adjective Animal" format
    EXPECT_NE(manager.getLocalName().find(' '), std::string::npos);
}

TEST(PresenceManagerTest, SetLocalPresence) {
    PresenceManager manager;
    manager.initialize("local_peer", "Local User");

    manager.setCurrentSheet("sheet_A");
    manager.setCursor("col_B", "row_3");
    manager.setSelection({"col_A", "row_1"}, {"col_C", "row_5"});
    manager.setMousePosition(100.0, 200.0);

    PresenceData presence = manager.getLocalPresence();
    EXPECT_EQ(presence.peer_id, "local_peer");
    EXPECT_EQ(presence.name, "Local User");
    EXPECT_EQ(presence.sheet_id, "sheet_A");
    EXPECT_TRUE(presence.has_cursor);
    EXPECT_EQ(presence.cursor.col, "col_B");
    EXPECT_EQ(presence.cursor.row, "row_3");
    EXPECT_TRUE(presence.has_selection);
    EXPECT_EQ(presence.selection.start.col, "col_A");
    EXPECT_EQ(presence.selection.end.row, "row_5");
    EXPECT_TRUE(presence.has_mouse);
    EXPECT_DOUBLE_EQ(presence.mouse.x, 100.0);
    EXPECT_DOUBLE_EQ(presence.mouse.y, 200.0);
}

TEST(PresenceManagerTest, ClearLocalPresence) {
    PresenceManager manager;
    manager.initialize("clear_peer", "Clear User");

    manager.setCursor("col", "row");
    manager.setSelection({"a", "1"}, {"b", "2"});
    manager.setMousePosition(50.0, 50.0);

    // Verify set
    PresenceData p1 = manager.getLocalPresence();
    EXPECT_TRUE(p1.has_cursor);
    EXPECT_TRUE(p1.has_selection);
    EXPECT_TRUE(p1.has_mouse);

    // Clear
    manager.clearCursor();
    manager.clearSelection();
    manager.clearMousePosition();

    PresenceData p2 = manager.getLocalPresence();
    EXPECT_FALSE(p2.has_cursor);
    EXPECT_FALSE(p2.has_selection);
    EXPECT_FALSE(p2.has_mouse);
}

TEST(PresenceManagerTest, HandleRemotePresence) {
    PresenceManager manager;
    manager.initialize("local", "Local");

    std::string remote_json = R"({
        "type": "presence",
        "peer_id": "remote1",
        "name": "Remote User",
        "color": "#E53935",
        "sheet_id": "sheet1",
        "cursor": {"col": "X", "row": "Y"},
        "selection": null,
        "mouse": null,
        "timestamp": 123456
    })";

    manager.handlePresenceMessage("remote1", remote_json);

    EXPECT_EQ(manager.getRemotePeerCount(), 1);

    const PresenceData* remote = manager.getPeerPresence("remote1");
    ASSERT_NE(remote, nullptr);
    EXPECT_EQ(remote->peer_id, "remote1");
    EXPECT_EQ(remote->name, "Remote User");
    EXPECT_TRUE(remote->has_cursor);
    EXPECT_EQ(remote->cursor.col, "X");
}

TEST(PresenceManagerTest, HandleRemotePresenceMismatchedPeerId) {
    PresenceManager manager;
    manager.initialize("local", "Local");

    // peer_id in message doesn't match the from peer
    std::string remote_json = R"({
        "type": "presence",
        "peer_id": "wrong_peer",
        "name": "Attacker",
        "color": "#000000",
        "sheet_id": "s",
        "cursor": null,
        "selection": null,
        "mouse": null,
        "timestamp": 0
    })";

    manager.handlePresenceMessage("actual_peer", remote_json);

    // Should be rejected
    EXPECT_EQ(manager.getRemotePeerCount(), 0);
}

TEST(PresenceManagerTest, RemovePeer) {
    PresenceManager manager;
    manager.initialize("local", "Local");

    std::string remote_json = R"({
        "type": "presence",
        "peer_id": "to_remove",
        "name": "Leaving User",
        "color": "#123456",
        "sheet_id": "s",
        "cursor": null,
        "selection": null,
        "mouse": null,
        "timestamp": 999
    })";

    manager.handlePresenceMessage("to_remove", remote_json);
    EXPECT_EQ(manager.getRemotePeerCount(), 1);

    manager.removePeer("to_remove");
    EXPECT_EQ(manager.getRemotePeerCount(), 0);
    EXPECT_EQ(manager.getPeerPresence("to_remove"), nullptr);
}

TEST(PresenceManagerTest, GetPeersOnSheet) {
    PresenceManager manager;
    manager.initialize("local", "Local");

    // Add peers on different sheets
    std::string peer1_json = R"({
        "type": "presence",
        "peer_id": "peer1",
        "name": "User 1",
        "color": "#111111",
        "sheet_id": "sheet_A",
        "cursor": null,
        "selection": null,
        "mouse": null,
        "timestamp": 1
    })";

    std::string peer2_json = R"({
        "type": "presence",
        "peer_id": "peer2",
        "name": "User 2",
        "color": "#222222",
        "sheet_id": "sheet_B",
        "cursor": null,
        "selection": null,
        "mouse": null,
        "timestamp": 2
    })";

    std::string peer3_json = R"({
        "type": "presence",
        "peer_id": "peer3",
        "name": "User 3",
        "color": "#333333",
        "sheet_id": "sheet_A",
        "cursor": null,
        "selection": null,
        "mouse": null,
        "timestamp": 3
    })";

    manager.handlePresenceMessage("peer1", peer1_json);
    manager.handlePresenceMessage("peer2", peer2_json);
    manager.handlePresenceMessage("peer3", peer3_json);

    auto sheet_a_peers = manager.getPeersOnSheet("sheet_A");
    EXPECT_EQ(sheet_a_peers.size(), 2);

    auto sheet_b_peers = manager.getPeersOnSheet("sheet_B");
    EXPECT_EQ(sheet_b_peers.size(), 1);
    EXPECT_EQ(sheet_b_peers[0].peer_id, "peer2");

    auto sheet_c_peers = manager.getPeersOnSheet("sheet_C");
    EXPECT_EQ(sheet_c_peers.size(), 0);
}

TEST(PresenceManagerTest, GenerateRandomName) {
    std::string name1 = PresenceManager::generateRandomName();
    std::string name2 = PresenceManager::generateRandomName();

    // Names should be "Adjective Animal" format
    EXPECT_NE(name1.find(' '), std::string::npos);
    EXPECT_NE(name2.find(' '), std::string::npos);

    // Note: There's a small chance they could be the same, but unlikely
    // We're mainly checking the format here
}

TEST(PresenceManagerTest, GetColorForPeer) {
    std::string color1 = PresenceManager::getColorForPeer("peer_abc");
    std::string color2 = PresenceManager::getColorForPeer("peer_xyz");
    std::string color1_again = PresenceManager::getColorForPeer("peer_abc");

    // Same peer should get same color
    EXPECT_EQ(color1, color1_again);

    // Colors should be hex format
    EXPECT_EQ(color1[0], '#');
    EXPECT_EQ(color1.size(), 7);
}

TEST(PresenceManagerTest, ProcessPendingUpdates) {
    PresenceManager manager;
    manager.initialize("active_peer", "Active User");

    // Initially not active
    std::string msg;
    EXPECT_FALSE(manager.processPendingUpdates(msg));

    // Setting cursor marks activity
    manager.setCursor("A", "1");
    EXPECT_TRUE(manager.isBroadcastingActive());

    // Should have a pending update
    EXPECT_TRUE(manager.processPendingUpdates(msg));
    EXPECT_FALSE(msg.empty());
    EXPECT_NE(msg.find("\"type\":\"presence\""), std::string::npos);
}

// ============================================================================
// Delegate tests
// ============================================================================

class TestPresenceDelegate : public PresenceDelegate {
public:
    int update_count = 0;
    int remove_count = 0;
    std::string last_updated_peer_id;
    std::string last_removed_peer_id;

    void presenceDidUpdate(PresenceManager& /*manager*/, const std::string& peer_id,
                           const PresenceData& /*presence*/) override {
        update_count++;
        last_updated_peer_id = peer_id;
    }

    void presenceDidRemove(PresenceManager& /*manager*/, const std::string& peer_id) override {
        remove_count++;
        last_removed_peer_id = peer_id;
    }
};

TEST(PresenceManagerTest, DelegateCallbacks) {
    PresenceManager manager;
    TestPresenceDelegate delegate;
    manager.setDelegate(&delegate);
    manager.initialize("local", "Local");

    std::string remote_json = R"({
        "type": "presence",
        "peer_id": "delegate_test_peer",
        "name": "Delegate Test",
        "color": "#ABCDEF",
        "sheet_id": "s",
        "cursor": null,
        "selection": null,
        "mouse": null,
        "timestamp": 100
    })";

    manager.handlePresenceMessage("delegate_test_peer", remote_json);
    EXPECT_EQ(delegate.update_count, 1);
    EXPECT_EQ(delegate.last_updated_peer_id, "delegate_test_peer");

    manager.removePeer("delegate_test_peer");
    EXPECT_EQ(delegate.remove_count, 1);
    EXPECT_EQ(delegate.last_removed_peer_id, "delegate_test_peer");
}

}  // namespace
}  // namespace cells::net
