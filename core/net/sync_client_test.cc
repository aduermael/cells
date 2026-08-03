// Tests for SyncClient join / ONLINE policy (shipped paths).

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "core/cells/model.h"
#include "core/net/include/SignalingClient.h"
#include "core/net/include/SyncClient.h"

namespace cells::net {
namespace {

class RecordingDelegate : public SyncClientDelegate {
public:
    void syncClientStateDidChange(SyncClient& /*client*/, SyncClientState state) override {
        states.push_back(state);
        last_state = state;
    }
    void syncClientPeerDidChange(SyncClient& /*client*/, const PeerInfo& /*peer*/) override {}
    void syncClientPeerDidDisconnect(SyncClient& /*client*/, const std::string& peer_id) override {
        disconnected.push_back(peer_id);
    }
    void syncClientDataDidChange(SyncClient& /*client*/) override {}
    void syncClientDidError(SyncClient& /*client*/, const std::string& error) override {
        errors.push_back(error);
    }

    std::vector<SyncClientState> states;
    SyncClientState last_state = SyncClientState::OFFLINE;
    std::vector<std::string> disconnected;
    std::vector<std::string> errors;
};

// Dummy signaling ref for injecting SignalingClientDelegate callbacks (unused body).
SignalingClient& dummySignaling() {
    static SignalingClientConfig cfg = [] {
        SignalingClientConfig c;
        c.url = "ws://127.0.0.1:1/ws";
        return c;
    }();
    static SignalingClient sig(cfg);
    return sig;
}

// Drive join-with-existing without a live signaling server by calling the
// public SignalingClientDelegate hooks on SyncClient (same entry points the
// real SignalingClient uses).

TEST(SyncClientJoinTest, FailedRtcJoinDoesNotGoOnlineEmpty) {
    Workbook wb;
    SyncClientConfig config;
    // Unreachable: we never wait on WS; we inject join/RTC events directly.
    config.signaling_url = "ws://127.0.0.1:1/ws";

    SyncClient client(&wb, config);
    RecordingDelegate delegate;
    client.setDelegate(&delegate);

    // Fixed peer id so we are the impolite initiator toward "AAAA0000".
    client.startSync("room-join-fail", "ZZZZ9999");
    EXPECT_EQ(client.getState(), SyncClientState::CONNECTING);

    // Simulate signaling: room already has one peer → SYNCING + expect remotes.
    // We initiate (ZZZZ9999 > AAAA0000) so createPeerConnection runs.
    client.signalingClientDidJoinRoom(dummySignaling(), "room-join-fail", {"AAAA0000"});
    EXPECT_EQ(client.getState(), SyncClientState::SYNCING);

    // A real RTC peer was created for AAAA0000. Force FAILED (same path as
    // ICE/DTLS failure) without ever opening a data channel.
    client.handlePeerConnectionStateChange("AAAA0000", PeerConnectionState::FAILED);

    // Must NOT settle ONLINE with an empty workbook (historical agent bug).
    EXPECT_EQ(client.getState(), SyncClientState::SYNCING);
    EXPECT_EQ(client.getPeerCount(), 0u);
    EXPECT_TRUE(wb.sheets.empty());
    ASSERT_FALSE(delegate.errors.empty());
    EXPECT_NE(delegate.errors.back().find("WebRTC connections failed"), std::string::npos);

    // processOutgoing must not flip ONLINE either.
    client.processOutgoing();
    EXPECT_EQ(client.getState(), SyncClientState::SYNCING);

    client.stopSync();
}

TEST(SyncClientJoinTest, AloneJoinGoesOnline) {
    Workbook wb;
    SyncClientConfig config;
    config.signaling_url = "ws://127.0.0.1:1/ws";

    SyncClient client(&wb, config);
    RecordingDelegate delegate;
    client.setDelegate(&delegate);

    client.startSync("room-alone", "peerAlone1");
    client.signalingClientDidJoinRoom(dummySignaling(), "room-alone", {});
    EXPECT_EQ(client.getState(), SyncClientState::ONLINE);
    EXPECT_EQ(client.getPeerCount(), 0u);

    client.stopSync();
}

TEST(SyncClientJoinTest, PeerLeftBeforeRtcAllowsOnlineAlone) {
    // Polite joiner waits for offer; existing peer leaves via signaling before
    // any RTC connection — room is empty, ONLINE alone is correct.
    Workbook wb;
    SyncClientConfig config;
    config.signaling_url = "ws://127.0.0.1:1/ws";

    SyncClient client(&wb, config);
    // Lower id than remote → polite (wait for offer)
    client.startSync("room-left", "AAAA0000");
    client.signalingClientDidJoinRoom(dummySignaling(), "room-left", {"ZZZZ9999"});
    EXPECT_EQ(client.getState(), SyncClientState::SYNCING);

    client.signalingClientPeerDidLeave(dummySignaling(), "ZZZZ9999");
    client.processOutgoing();

    EXPECT_EQ(client.getState(), SyncClientState::ONLINE);

    client.stopSync();
}

}  // namespace
}  // namespace cells::net
