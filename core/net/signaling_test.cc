// Tests for SignalingProtocol and SignalingClient

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "core/net/include/SignalingClient.h"
#include "core/net/include/SignalingProtocol.h"

namespace cells::net {
namespace {

// ========================================
// SignalingProtocol Tests
// ========================================

class SignalingProtocolTest : public ::testing::Test {};

TEST_F(SignalingProtocolTest, EscapeJsonString_Simple) {
    EXPECT_EQ(SignalingProtocol::escapeJsonString("hello"), "hello");
    EXPECT_EQ(SignalingProtocol::escapeJsonString(""), "");
}

TEST_F(SignalingProtocolTest, EscapeJsonString_Quotes) {
    EXPECT_EQ(SignalingProtocol::escapeJsonString("say \"hello\""), "say \\\"hello\\\"");
}

TEST_F(SignalingProtocolTest, EscapeJsonString_Backslash) {
    EXPECT_EQ(SignalingProtocol::escapeJsonString("path\\to\\file"), "path\\\\to\\\\file");
}

TEST_F(SignalingProtocolTest, EscapeJsonString_ControlChars) {
    EXPECT_EQ(SignalingProtocol::escapeJsonString("line1\nline2"), "line1\\nline2");
    EXPECT_EQ(SignalingProtocol::escapeJsonString("col1\tcol2"), "col1\\tcol2");
    EXPECT_EQ(SignalingProtocol::escapeJsonString("a\rb"), "a\\rb");
}

TEST_F(SignalingProtocolTest, UnescapeJsonString_Simple) {
    EXPECT_EQ(SignalingProtocol::unescapeJsonString("hello"), "hello");
}

TEST_F(SignalingProtocolTest, UnescapeJsonString_EscapeSequences) {
    EXPECT_EQ(SignalingProtocol::unescapeJsonString("say \\\"hello\\\""), "say \"hello\"");
    EXPECT_EQ(SignalingProtocol::unescapeJsonString("path\\\\to\\\\file"), "path\\to\\file");
    EXPECT_EQ(SignalingProtocol::unescapeJsonString("line1\\nline2"), "line1\nline2");
}

TEST_F(SignalingProtocolTest, BuildJoinMessage) {
    std::string msg = SignalingProtocol::buildJoinMessage("room123", "peer456");
    EXPECT_NE(msg.find("\"type\":\"join\""), std::string::npos);
    EXPECT_NE(msg.find("\"room\":\"room123\""), std::string::npos);
    EXPECT_NE(msg.find("\"peer_id\":\"peer456\""), std::string::npos);
}

TEST_F(SignalingProtocolTest, BuildLeaveMessage) {
    std::string msg = SignalingProtocol::buildLeaveMessage("room123", "peer456");
    EXPECT_NE(msg.find("\"type\":\"leave\""), std::string::npos);
    EXPECT_NE(msg.find("\"room\":\"room123\""), std::string::npos);
}

TEST_F(SignalingProtocolTest, BuildOfferMessage) {
    SessionDescription sdp = SessionDescription::offer("v=0\r\no=- ...");
    std::string msg = SignalingProtocol::buildOfferMessage("peer789", sdp);
    EXPECT_NE(msg.find("\"type\":\"offer\""), std::string::npos);
    EXPECT_NE(msg.find("\"target\":\"peer789\""), std::string::npos);
    EXPECT_NE(msg.find("\"sdp\":{"), std::string::npos);
}

TEST_F(SignalingProtocolTest, BuildAnswerMessage) {
    SessionDescription sdp = SessionDescription::answer("v=0\r\no=- ...");
    std::string msg = SignalingProtocol::buildAnswerMessage("peer789", sdp);
    EXPECT_NE(msg.find("\"type\":\"answer\""), std::string::npos);
    EXPECT_NE(msg.find("\"target\":\"peer789\""), std::string::npos);
}

TEST_F(SignalingProtocolTest, BuildICECandidateMessage) {
    ICECandidate candidate("candidate:123 1 udp 2130706431 ...", "0", 0);
    std::string msg = SignalingProtocol::buildICECandidateMessage("peer789", candidate);
    EXPECT_NE(msg.find("\"type\":\"ice-candidate\""), std::string::npos);
    EXPECT_NE(msg.find("\"target\":\"peer789\""), std::string::npos);
    EXPECT_NE(msg.find("\"candidate\":{"), std::string::npos);
    EXPECT_NE(msg.find("\"sdpMid\":\"0\""), std::string::npos);
    EXPECT_NE(msg.find("\"sdpMLineIndex\":0"), std::string::npos);
}

TEST_F(SignalingProtocolTest, ParseMessageType) {
    EXPECT_EQ(SignalingProtocol::parseMessageType("{\"type\":\"join\"}"), "join");
    EXPECT_EQ(SignalingProtocol::parseMessageType("{\"type\":\"offer\",\"data\":{}}"), "offer");
    EXPECT_EQ(SignalingProtocol::parseMessageType("{\"data\":\"test\"}"), "");
}

TEST_F(SignalingProtocolTest, ParseJoinedMessage) {
    std::string json = R"({"type":"joined","room":"room123","peers":["peer1","peer2"]})";
    std::string room;
    std::vector<std::string> peers;

    ASSERT_TRUE(SignalingProtocol::parseJoinedMessage(json, room, peers));
    EXPECT_EQ(room, "room123");
    ASSERT_EQ(peers.size(), 2u);
    EXPECT_EQ(peers[0], "peer1");
    EXPECT_EQ(peers[1], "peer2");
}

TEST_F(SignalingProtocolTest, ParseJoinedMessage_NoPeers) {
    std::string json = R"({"type":"joined","room":"room123"})";
    std::string room;
    std::vector<std::string> peers;

    ASSERT_TRUE(SignalingProtocol::parseJoinedMessage(json, room, peers));
    EXPECT_EQ(room, "room123");
    EXPECT_TRUE(peers.empty());
}

TEST_F(SignalingProtocolTest, ParsePeerJoinedMessage) {
    std::string json = R"({"type":"peer-joined","peer_id":"new_peer"})";
    std::string peer_id;

    ASSERT_TRUE(SignalingProtocol::parsePeerJoinedMessage(json, peer_id));
    EXPECT_EQ(peer_id, "new_peer");
}

TEST_F(SignalingProtocolTest, ParsePeerLeftMessage) {
    std::string json = R"({"type":"peer-left","peer_id":"old_peer"})";
    std::string peer_id;

    ASSERT_TRUE(SignalingProtocol::parsePeerLeftMessage(json, peer_id));
    EXPECT_EQ(peer_id, "old_peer");
}

TEST_F(SignalingProtocolTest, ParseOfferMessage) {
    std::string json =
        R"({"type":"offer","from":"peer123","sdp":{"type":"offer","sdp":"v=0\r\n"}})";
    std::string from_peer;
    SessionDescription sdp;

    ASSERT_TRUE(SignalingProtocol::parseOfferMessage(json, from_peer, sdp));
    EXPECT_EQ(from_peer, "peer123");
    EXPECT_EQ(sdp.type, SDPType::OFFER);
    EXPECT_EQ(sdp.sdp, "v=0\r\n");
}

TEST_F(SignalingProtocolTest, ParseAnswerMessage) {
    std::string json =
        R"({"type":"answer","from":"peer456","sdp":{"type":"answer","sdp":"v=0\r\n"}})";
    std::string from_peer;
    SessionDescription sdp;

    ASSERT_TRUE(SignalingProtocol::parseAnswerMessage(json, from_peer, sdp));
    EXPECT_EQ(from_peer, "peer456");
    EXPECT_EQ(sdp.type, SDPType::ANSWER);
}

TEST_F(SignalingProtocolTest, ParseICECandidateMessage) {
    std::string json = R"({
        "type":"ice-candidate",
        "from":"peer789",
        "candidate":{
            "candidate":"candidate:1 1 udp 2130706431 ...",
            "sdpMid":"0",
            "sdpMLineIndex":0
        }
    })";
    std::string from_peer;
    ICECandidate candidate;

    ASSERT_TRUE(SignalingProtocol::parseICECandidateMessage(json, from_peer, candidate));
    EXPECT_EQ(from_peer, "peer789");
    EXPECT_EQ(candidate.candidate, "candidate:1 1 udp 2130706431 ...");
    EXPECT_EQ(candidate.sdp_mid, "0");
    EXPECT_EQ(candidate.sdp_mline_index, 0);
}

TEST_F(SignalingProtocolTest, ParsePeerListMessage) {
    std::string json = R"({"type":"peer-list","peers":["a","b","c"]})";
    std::vector<std::string> peers;

    ASSERT_TRUE(SignalingProtocol::parsePeerListMessage(json, peers));
    ASSERT_EQ(peers.size(), 3u);
    EXPECT_EQ(peers[0], "a");
    EXPECT_EQ(peers[1], "b");
    EXPECT_EQ(peers[2], "c");
}

TEST_F(SignalingProtocolTest, ParseErrorMessage) {
    std::string json = R"({"type":"error","message":"Room not found"})";
    std::string error;

    ASSERT_TRUE(SignalingProtocol::parseErrorMessage(json, error));
    EXPECT_EQ(error, "Room not found");
}

TEST_F(SignalingProtocolTest, EscapedStringsRoundtrip) {
    // Test that escaped strings in SDP work correctly
    std::string original_sdp = "v=0\r\no=- 123\r\nm=application ...\r\n";
    SessionDescription sdp = SessionDescription::offer(original_sdp);
    std::string msg = SignalingProtocol::buildOfferMessage("peer", sdp);

    // Parse it back - we need to simulate the server side
    // For now, just verify the escape/unescape roundtrip
    std::string escaped = SignalingProtocol::escapeJsonString(original_sdp);
    std::string unescaped = SignalingProtocol::unescapeJsonString(escaped);
    EXPECT_EQ(original_sdp, unescaped);
}

// ========================================
// SignalingClient Tests
// ========================================

class SignalingClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        SignalingClientConfig config;
        config.url = "wss://example.com/ws";
        config.reconnect_delay_ms = 100;
        config.max_reconnect_attempts = 3;

        client_ = std::make_unique<SignalingClient>(config);
    }

    std::unique_ptr<SignalingClient> client_;
};

TEST_F(SignalingClientTest, InitialState) {
    EXPECT_EQ(client_->getState(), SignalingClientState::DISCONNECTED);
    EXPECT_TRUE(client_->getRoomId().empty());
    EXPECT_TRUE(client_->getPeerId().empty());
    EXPECT_FALSE(client_->isConnected());
}

TEST_F(SignalingClientTest, StateToString) {
    EXPECT_STREQ(signalingClientStateToString(SignalingClientState::DISCONNECTED), "DISCONNECTED");
    EXPECT_STREQ(signalingClientStateToString(SignalingClientState::CONNECTING), "CONNECTING");
    EXPECT_STREQ(signalingClientStateToString(SignalingClientState::CONNECTED), "CONNECTED");
    EXPECT_STREQ(signalingClientStateToString(SignalingClientState::RECONNECTING), "RECONNECTING");
    EXPECT_STREQ(signalingClientStateToString(SignalingClientState::IN_ROOM), "IN_ROOM");
}

// Note: Testing actual connection behavior requires a mock WebSocket
// or integration test with a real server. These tests focus on protocol
// handling and state management.

// ========================================
// Mock-based tests (for more complex scenarios)
// ========================================

// A simple mock delegate to track callbacks
class MockSignalingDelegate : public SignalingClientDelegate {
public:
    // Track state changes
    std::vector<SignalingClientState> state_changes;

    // Track events
    std::string last_joined_room;
    std::vector<std::string> last_joined_peers;
    std::string last_peer_joined;
    std::string last_peer_left;
    std::string last_offer_from;
    SessionDescription last_offer_sdp;
    std::string last_answer_from;
    SessionDescription last_answer_sdp;
    std::string last_ice_from;
    ICECandidate last_ice_candidate;
    std::string last_error;

    void signalingClientStateDidChange(SignalingClient& /*client*/,
                                       SignalingClientState state) override {
        state_changes.push_back(state);
    }

    void signalingClientDidJoinRoom(SignalingClient& /*client*/, const std::string& room_id,
                                    const std::vector<std::string>& peers) override {
        last_joined_room = room_id;
        last_joined_peers = peers;
    }

    void signalingClientPeerDidJoin(SignalingClient& /*client*/,
                                    const std::string& peer_id) override {
        last_peer_joined = peer_id;
    }

    void signalingClientPeerDidLeave(SignalingClient& /*client*/,
                                     const std::string& peer_id) override {
        last_peer_left = peer_id;
    }

    void signalingClientDidReceiveOffer(SignalingClient& /*client*/, const std::string& from_peer,
                                        const SessionDescription& sdp) override {
        last_offer_from = from_peer;
        last_offer_sdp = sdp;
    }

    void signalingClientDidReceiveAnswer(SignalingClient& /*client*/, const std::string& from_peer,
                                         const SessionDescription& sdp) override {
        last_answer_from = from_peer;
        last_answer_sdp = sdp;
    }

    void signalingClientDidReceiveICECandidate(SignalingClient& /*client*/,
                                               const std::string& from_peer,
                                               const ICECandidate& candidate) override {
        last_ice_from = from_peer;
        last_ice_candidate = candidate;
    }

    void signalingClientDidReceiveError(SignalingClient& /*client*/,
                                        const std::string& error) override {
        last_error = error;
    }
};

TEST_F(SignalingClientTest, DelegateSetGet) {
    MockSignalingDelegate delegate;
    EXPECT_EQ(client_->getDelegate(), nullptr);

    client_->setDelegate(&delegate);
    EXPECT_EQ(client_->getDelegate(), &delegate);

    client_->setDelegate(nullptr);
    EXPECT_EQ(client_->getDelegate(), nullptr);
}

}  // namespace
}  // namespace cells::net
