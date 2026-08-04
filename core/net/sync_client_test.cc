// Tests for SyncClient join / ONLINE policy (shipped paths).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/cells/model.h"
#include "core/net/include/SignalingClient.h"
#include "core/net/include/SyncClient.h"

namespace cells::net {
namespace {

class RecordingDelegate : public SyncClientDelegate {
public:
    void syncClientStateDidChange(SyncClient& /*client*/, SyncClientState state) override {
        std::lock_guard<std::mutex> lock(mu);
        states.push_back(state);
        last_state = state;
    }
    void syncClientPeerDidChange(SyncClient& /*client*/, const PeerInfo& /*peer*/) override {}
    void syncClientPeerDidDisconnect(SyncClient& /*client*/, const std::string& peer_id) override {
        std::lock_guard<std::mutex> lock(mu);
        disconnected.push_back(peer_id);
        disconnect_count.fetch_add(1);
    }
    void syncClientDataDidChange(SyncClient& /*client*/) override {}
    void syncClientDidError(SyncClient& /*client*/, const std::string& error) override {
        std::lock_guard<std::mutex> lock(mu);
        errors.push_back(error);
        error_count.fetch_add(1);
    }

    std::mutex mu;
    std::vector<SyncClientState> states;
    SyncClientState last_state = SyncClientState::OFFLINE;
    std::vector<std::string> disconnected;
    std::vector<std::string> errors;
    std::atomic<int> disconnect_count{0};
    std::atomic<int> error_count{0};
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

    auto webrtc_join_errors = [&]() {
        std::lock_guard<std::mutex> lock(delegate.mu);
        return std::count_if(delegate.errors.begin(), delegate.errors.end(),
                             [](const std::string& e) {
                                 return e.find("WebRTC connections failed") != std::string::npos;
                             });
    };

    // A real RTC peer was created for AAAA0000. Real libdc may also fire FAILED
    // quickly (no remote). Join-time logic retries a few times before permanent
    // error — keep forcing FAILED until the join-failure error is reported.
    for (int i = 0; i < 8 && webrtc_join_errors() == 0; ++i) {
        client.handlePeerConnectionStateChange("AAAA0000", PeerConnectionState::FAILED);
        // Allow scheduled join retries to re-create the PC.
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        client.processOutgoing();
    }

    // Must NOT settle ONLINE with an empty workbook (historical agent bug).
    EXPECT_EQ(client.getState(), SyncClientState::SYNCING);
    EXPECT_EQ(client.getPeerCount(), 0u);
    EXPECT_TRUE(wb.sheets.empty());
    EXPECT_GE(webrtc_join_errors(), 1);

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

// Early trickle ICE from the initiator often arrives before the offer. Those
// must be buffered (not dropped) so host candidates are not lost — browser↔CLI
// local repro where CLI candidates precede the offer on the wire.
TEST(SyncClientJoinTest, EarlyRemoteIceBufferedWithoutPeer) {
    Workbook wb;
    SyncClientConfig config;
    // Unreachable on purpose: we inject join/ICE via SignalingClientDelegate.
    // Real WS may async-error ("Signaling disconnected"); that is expected.
    config.signaling_url = "ws://127.0.0.1:1/ws";

    SyncClient client(&wb, config);
    RecordingDelegate delegate;
    client.setDelegate(&delegate);

    // Polite: wait for offer from higher id (no PC yet).
    client.startSync("room-ice", "AAAA0000");
    client.signalingClientDidJoinRoom(dummySignaling(), "room-ice", {"ZZZZ9999"});
    EXPECT_EQ(client.getState(), SyncClientState::SYNCING);
    EXPECT_TRUE(client.getPeers().empty());

    // Initiator trickles host candidates before the offer hits us — must not
    // throw / crash / mark join failed (no RTC attempt yet).
    ICECandidate early("candidate:1 1 udp 2130706431 10.0.0.2 54321 typ host", "0", 0);
    client.signalingClientDidReceiveICECandidate(dummySignaling(), "ZZZZ9999", early);
    client.signalingClientDidReceiveICECandidate(
        dummySignaling(), "ZZZZ9999",
        ICECandidate("candidate:2 1 udp 1694498815 203.0.113.5 54322 typ srflx raddr 10.0.0.2 "
                     "rport 54321",
                     "0", 0));

    EXPECT_EQ(client.getState(), SyncClientState::SYNCING);
    EXPECT_TRUE(client.getPeers().empty());
    // Early ICE with no peer must not report WebRTC join failure or peer leave.
    // Signaling TCP errors against the dummy URL are unrelated and may race in.
    {
        std::lock_guard<std::mutex> lock(delegate.mu);
        const int webrtc_join_errors = static_cast<int>(std::count_if(
            delegate.errors.begin(), delegate.errors.end(), [](const std::string& e) {
                return e.find("WebRTC connections failed") != std::string::npos;
            }));
        EXPECT_EQ(webrtc_join_errors, 0);
    }
    EXPECT_EQ(delegate.disconnect_count.load(), 0);

    // Real flush of early ICE is covered by native two-PC rtc_test + two-CLI
    // session join; createPeerConnection calls flushEarlyRemoteIce.

    client.stopSync();
}

}  // namespace
}  // namespace cells::net
