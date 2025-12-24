// Tests for RTCPeerConnection and RTCDataChannel
// Note: Full WebRTC tests require a browser environment or native WebRTC library.
// These tests verify the interface and basic state management.

#include <gtest/gtest.h>

#include "core/net/include/RTCDataChannel.h"
#include "core/net/include/RTCPeerConnection.h"

namespace cells::net {
namespace {

// Test state enum conversions
TEST(RTCStateTest, DataChannelStateToString) {
    EXPECT_STREQ(dataChannelStateToString(DataChannelState::CONNECTING), "connecting");
    EXPECT_STREQ(dataChannelStateToString(DataChannelState::OPEN), "open");
    EXPECT_STREQ(dataChannelStateToString(DataChannelState::CLOSING), "closing");
    EXPECT_STREQ(dataChannelStateToString(DataChannelState::CLOSED), "closed");
}

TEST(RTCStateTest, PeerConnectionStateToString) {
    EXPECT_STREQ(peerConnectionStateToString(PeerConnectionState::NEW), "new");
    EXPECT_STREQ(peerConnectionStateToString(PeerConnectionState::CONNECTING), "connecting");
    EXPECT_STREQ(peerConnectionStateToString(PeerConnectionState::CONNECTED), "connected");
    EXPECT_STREQ(peerConnectionStateToString(PeerConnectionState::DISCONNECTED), "disconnected");
    EXPECT_STREQ(peerConnectionStateToString(PeerConnectionState::FAILED), "failed");
    EXPECT_STREQ(peerConnectionStateToString(PeerConnectionState::CLOSED), "closed");
}

TEST(RTCStateTest, ICEConnectionStateToString) {
    EXPECT_STREQ(iceConnectionStateToString(ICEConnectionState::NEW), "new");
    EXPECT_STREQ(iceConnectionStateToString(ICEConnectionState::CHECKING), "checking");
    EXPECT_STREQ(iceConnectionStateToString(ICEConnectionState::CONNECTED), "connected");
    EXPECT_STREQ(iceConnectionStateToString(ICEConnectionState::COMPLETED), "completed");
    EXPECT_STREQ(iceConnectionStateToString(ICEConnectionState::DISCONNECTED), "disconnected");
    EXPECT_STREQ(iceConnectionStateToString(ICEConnectionState::FAILED), "failed");
    EXPECT_STREQ(iceConnectionStateToString(ICEConnectionState::CLOSED), "closed");
}

TEST(RTCStateTest, ICEGatheringStateToString) {
    EXPECT_STREQ(iceGatheringStateToString(ICEGatheringState::NEW), "new");
    EXPECT_STREQ(iceGatheringStateToString(ICEGatheringState::GATHERING), "gathering");
    EXPECT_STREQ(iceGatheringStateToString(ICEGatheringState::COMPLETE), "complete");
}

TEST(RTCStateTest, SignalingStateToString) {
    EXPECT_STREQ(signalingStateToString(SignalingState::STABLE), "stable");
    EXPECT_STREQ(signalingStateToString(SignalingState::HAVE_LOCAL_OFFER), "have-local-offer");
    EXPECT_STREQ(signalingStateToString(SignalingState::HAVE_REMOTE_OFFER), "have-remote-offer");
    EXPECT_STREQ(signalingStateToString(SignalingState::HAVE_LOCAL_PRANSWER),
                 "have-local-pranswer");
    EXPECT_STREQ(signalingStateToString(SignalingState::HAVE_REMOTE_PRANSWER),
                 "have-remote-pranswer");
    EXPECT_STREQ(signalingStateToString(SignalingState::CLOSED), "closed");
}

TEST(RTCStateTest, SDPTypeToString) {
    EXPECT_STREQ(sdpTypeToString(SDPType::OFFER), "offer");
    EXPECT_STREQ(sdpTypeToString(SDPType::PRANSWER), "pranswer");
    EXPECT_STREQ(sdpTypeToString(SDPType::ANSWER), "answer");
    EXPECT_STREQ(sdpTypeToString(SDPType::ROLLBACK), "rollback");
}

TEST(RTCStateTest, SDPTypeFromString) {
    EXPECT_EQ(sdpTypeFromString("offer"), SDPType::OFFER);
    EXPECT_EQ(sdpTypeFromString("pranswer"), SDPType::PRANSWER);
    EXPECT_EQ(sdpTypeFromString("answer"), SDPType::ANSWER);
    EXPECT_EQ(sdpTypeFromString("rollback"), SDPType::ROLLBACK);
    // Unknown strings default to OFFER
    EXPECT_EQ(sdpTypeFromString("unknown"), SDPType::OFFER);
}

// Test SessionDescription
TEST(SessionDescriptionTest, Construction) {
    SessionDescription desc;
    EXPECT_EQ(desc.type, SDPType::OFFER);
    EXPECT_TRUE(desc.sdp.empty());
}

TEST(SessionDescriptionTest, OfferFactory) {
    auto offer = SessionDescription::offer("v=0\r\n...");
    EXPECT_EQ(offer.type, SDPType::OFFER);
    EXPECT_EQ(offer.sdp, "v=0\r\n...");
}

TEST(SessionDescriptionTest, AnswerFactory) {
    auto answer = SessionDescription::answer("v=0\r\n...");
    EXPECT_EQ(answer.type, SDPType::ANSWER);
    EXPECT_EQ(answer.sdp, "v=0\r\n...");
}

// Test ICECandidate
TEST(ICECandidateTest, Construction) {
    ICECandidate candidate;
    EXPECT_TRUE(candidate.isEmpty());
    EXPECT_EQ(candidate.sdp_mline_index, 0);
}

TEST(ICECandidateTest, WithValues) {
    ICECandidate candidate("candidate:123 1 udp 2130706431 192.168.1.1 54400 typ host", "audio", 0);
    EXPECT_FALSE(candidate.isEmpty());
    EXPECT_EQ(candidate.sdp_mid, "audio");
    EXPECT_EQ(candidate.sdp_mline_index, 0);
}

// Test DataChannelConfig
TEST(DataChannelConfigTest, DefaultConfig) {
    DataChannelConfig config;
    EXPECT_TRUE(config.ordered);
    EXPECT_EQ(config.max_retransmits, -1);
    EXPECT_EQ(config.max_packet_life_time, -1);
    EXPECT_TRUE(config.protocol.empty());
    EXPECT_FALSE(config.negotiated);
    EXPECT_EQ(config.id, -1);
}

TEST(DataChannelConfigTest, UnreliableConfig) {
    auto config = DataChannelConfig::unreliable();
    EXPECT_FALSE(config.ordered);
    EXPECT_EQ(config.max_retransmits, 0);
}

TEST(DataChannelConfigTest, ReliableConfig) {
    auto config = DataChannelConfig::reliable();
    EXPECT_TRUE(config.ordered);
    EXPECT_EQ(config.max_retransmits, -1);
}

// Test RTCPeerConnection factory (stub implementation on non-web platforms)
TEST(RTCPeerConnectionTest, Create) {
    auto pc = RTCPeerConnection::make();
    EXPECT_NE(pc, nullptr);
}

TEST(RTCPeerConnectionTest, CreateWithConfig) {
    auto config = RTCConfiguration::defaultConfig();
    auto pc = RTCPeerConnection::make(config);
    EXPECT_NE(pc, nullptr);
}

// On non-WebRTC platforms, operations should fail gracefully
TEST(RTCPeerConnectionTest, CreateOfferOnStub) {
    auto pc = RTCPeerConnection::make();

    bool callback_called = false;
    pc->createOffer([&](bool success, const SessionDescription& sdp, const std::string& error) {
        callback_called = true;
// On stub platforms, this should fail
// On real platforms (WASM with browser), this might succeed
#if !defined(__EMSCRIPTEN__)
        // Stub implementation - expect failure
        EXPECT_FALSE(success);
        EXPECT_FALSE(error.empty());
#else
        // Real implementation - just verify callback was called
        (void)success;
        (void)sdp;
        (void)error;
#endif
    });

    EXPECT_TRUE(callback_called);
}

TEST(RTCPeerConnectionTest, CreateDataChannel) {
    auto pc = RTCPeerConnection::make();

    auto channel = pc->createDataChannel("test", DataChannelConfig::reliable());

    // With libdatachannel implementation (native), this returns a valid channel
    // On WASM (browser), this also returns a channel via JS interop
    // On stub platforms (if any), this might return nullptr
    if (channel) {
        EXPECT_EQ(channel->getLabel(), "test");
    }
    // Either way, the test passes - we're just checking it doesn't crash
}

TEST(RTCPeerConnectionTest, StateAccessors) {
    auto pc = RTCPeerConnection::make();

    // Initial state checks
    EXPECT_FALSE(pc->isConnected());

    // State accessors should not crash
    auto conn_state = pc->getConnectionState();
    auto ice_state = pc->getICEConnectionState();
    auto gathering_state = pc->getICEGatheringState();
    auto signaling_state = pc->getSignalingState();

    // Convert to strings (should not crash)
    peerConnectionStateToString(conn_state);
    iceConnectionStateToString(ice_state);
    iceGatheringStateToString(gathering_state);
    signalingStateToString(signaling_state);
}

TEST(RTCPeerConnectionTest, Delegate) {
    auto pc = RTCPeerConnection::make();

    EXPECT_EQ(pc->getDelegate(), nullptr);

    // We can't easily test delegate callbacks without a full WebRTC implementation,
    // but we can verify the interface works
    class TestDelegate : public RTCPeerConnectionDelegate {
    public:
        void peerConnectionStateDidChange(RTCPeerConnection& /*pc*/,
                                          PeerConnectionState /*state*/) override {}

        void peerConnectionICEStateDidChange(RTCPeerConnection& /*pc*/,
                                             ICEConnectionState /*state*/) override {}

        void peerConnectionDidGatherICECandidate(RTCPeerConnection& /*pc*/,
                                                 const ICECandidate& /*candidate*/) override {}

        void peerConnectionDidReceiveDataChannel(
            RTCPeerConnection& /*pc*/, std::unique_ptr<RTCDataChannel> /*channel*/) override {}
    };

    TestDelegate delegate;
    pc->setDelegate(&delegate);
    EXPECT_EQ(pc->getDelegate(), &delegate);

    pc->setDelegate(nullptr);
    EXPECT_EQ(pc->getDelegate(), nullptr);
}

TEST(RTCPeerConnectionTest, Close) {
    auto pc = RTCPeerConnection::make();

    // Close should not crash
    pc->close();

    // After close, should report closed state
    EXPECT_TRUE(pc->isClosed());
}

}  // namespace
}  // namespace cells::net
