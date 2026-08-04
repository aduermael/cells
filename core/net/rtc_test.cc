// Tests for RTCPeerConnection and RTCDataChannel
// Note: Full WebRTC tests require a browser environment or native WebRTC library.
// These tests verify the interface and basic state management.

#include <cctype>

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/net/include/ICEConfig.h"
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

// Browser addIceCandidate rejects the SDP line form "a=candidate:...".
// Native libdatachannel must export Candidate::candidate() (no "a=" prefix).
#if !defined(__EMSCRIPTEN__)
class CandidateCaptureDelegate : public RTCPeerConnectionDelegate {
public:
    void peerConnectionStateDidChange(RTCPeerConnection& /*pc*/,
                                      PeerConnectionState /*state*/) override {}
    void peerConnectionICEStateDidChange(RTCPeerConnection& /*pc*/,
                                         ICEConnectionState /*state*/) override {}
    void peerConnectionDidGatherICECandidate(RTCPeerConnection& /*pc*/,
                                             const ICECandidate& candidate) override {
        if (!candidate.isEmpty()) {
            // ICE gathering runs on a libjuice/libdc thread — protect the vector.
            std::lock_guard<std::mutex> lock(mu);
            candidates.push_back(candidate.candidate);
        }
    }
    void peerConnectionDidReceiveDataChannel(RTCPeerConnection& /*pc*/,
                                             std::unique_ptr<RTCDataChannel> /*channel*/) override {
    }

    size_t candidateCount() const {
        std::lock_guard<std::mutex> lock(mu);
        return candidates.size();
    }

    std::vector<std::string> snapshot() const {
        std::lock_guard<std::mutex> lock(mu);
        return candidates;
    }

private:
    mutable std::mutex mu;
    std::vector<std::string> candidates;
};

TEST(RTCPeerConnectionTest, LocalIceCandidatesHaveNoAPrefix) {
    auto pc = RTCPeerConnection::make(RTCConfiguration::defaultConfig());
    ASSERT_NE(pc, nullptr);

    CandidateCaptureDelegate delegate;
    pc->setDelegate(&delegate);

    // Creating a data channel starts negotiation and ICE gathering.
    auto channel = pc->createDataChannel("operations", DataChannelConfig::reliable());
    ASSERT_NE(channel, nullptr);

    bool offer_ok = false;
    pc->createOffer([&](bool success, const SessionDescription& sdp, const std::string& /*err*/) {
        offer_ok = success;
        if (success) {
            pc->setLocalDescription(sdp, [](bool /*s*/, const std::string& /*e*/) {});
        }
    });
    EXPECT_TRUE(offer_ok);

    // Pump briefly for async ICE gathering (libjuice thread).
    for (int i = 0; i < 50 && delegate.candidateCount() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Detach delegate before inspecting results so a late ICE callback cannot
    // race the vector (or free the stack delegate after close).
    pc->setDelegate(nullptr);
    // Give in-flight callbacks a beat to finish before we close.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const auto gathered = delegate.snapshot();

    // On some restricted networks STUN may yield nothing; host candidates should
    // still appear. If none arrive, skip rather than fail the suite.
    if (gathered.empty()) {
        pc->close();
        GTEST_SKIP() << "No ICE candidates gathered in time (environment)";
    }

    for (const auto& c : gathered) {
        EXPECT_TRUE(c.rfind("a=", 0) != 0) << "browser-incompatible candidate: " << c;
        EXPECT_TRUE(c.rfind("candidate:", 0) == 0) << "expected candidate: prefix, got: " << c;
        // Transport protocol must be lowercase for browser interop (libdc emits UDP).
        // candidate:<f> <comp> <proto> ...
        const size_t sp1 = c.find(' ');
        const size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : c.find(' ', sp1 + 1);
        const size_t sp3 = (sp2 == std::string::npos) ? std::string::npos : c.find(' ', sp2 + 1);
        ASSERT_NE(sp2, std::string::npos);
        ASSERT_NE(sp3, std::string::npos);
        const std::string proto = c.substr(sp2 + 1, sp3 - sp2 - 1);
        for (char ch : proto) {
            EXPECT_TRUE(std::islower(static_cast<unsigned char>(ch)) ||
                        !std::isalpha(static_cast<unsigned char>(ch)))
                << "protocol should be lowercase, got: " << proto << " in " << c;
        }
    }

    pc->close();
}

TEST(RTCPeerConnectionTest, BuffersIceCandidatesUntilRemoteDescription) {
    // Two peer connections: offerer + answerer. Send answerer's trickle
    // candidates to offerer BEFORE setRemoteDescription(answer), then set
    // answer and ensure the connection can still complete (buffering works).
    auto offerer = RTCPeerConnection::make(RTCConfiguration::defaultConfig());
    auto answerer = RTCPeerConnection::make(RTCConfiguration::defaultConfig());
    ASSERT_NE(offerer, nullptr);
    ASSERT_NE(answerer, nullptr);

    struct PairDelegate : public RTCPeerConnectionDelegate {
        RTCPeerConnection* remote = nullptr;
        bool buffer_before_remote = false;
        std::vector<ICECandidate> early;
        std::atomic<bool> dc_open{false};

        void peerConnectionStateDidChange(RTCPeerConnection& /*pc*/,
                                          PeerConnectionState /*state*/) override {}
        void peerConnectionICEStateDidChange(RTCPeerConnection& /*pc*/,
                                             ICEConnectionState /*state*/) override {}
        void peerConnectionDidGatherICECandidate(RTCPeerConnection& /*pc*/,
                                                 const ICECandidate& candidate) override {
            if (candidate.isEmpty() || remote == nullptr) {
                return;
            }
            if (buffer_before_remote) {
                early.push_back(candidate);
                // Deliver early to remote (must be buffered there)
                remote->addIceCandidate(candidate, [](bool /*s*/, const std::string& /*e*/) {});
            } else {
                remote->addIceCandidate(candidate, [](bool /*s*/, const std::string& /*e*/) {});
            }
        }
        void peerConnectionDidReceiveDataChannel(RTCPeerConnection& /*pc*/,
                                                 std::unique_ptr<RTCDataChannel> channel) override {
            if (channel) {
                channel->setDelegate(nullptr);
            }
        }
    };

    class OpenDelegate : public DataChannelDelegate {
    public:
        explicit OpenDelegate(std::atomic<bool>* flag) : flag_(flag) {}
        void dataChannelDidOpen(RTCDataChannel& /*channel*/) override {
            if (flag_) {
                *flag_ = true;
            }
        }
        void dataChannelDidClose(RTCDataChannel& /*channel*/) override {}
        void dataChannelDidReceiveMessage(RTCDataChannel& /*channel*/,
                                          const std::string& /*message*/) override {}
        void dataChannelDidReceiveData(RTCDataChannel& /*channel*/,
                                       const std::vector<uint8_t>& /*data*/) override {}
        std::atomic<bool>* flag_ = nullptr;
    };

    PairDelegate off_del;
    PairDelegate ans_del;
    off_del.remote = answerer.get();
    ans_del.remote = offerer.get();
    // Answerer gathers before offerer has remote answer set
    ans_del.buffer_before_remote = true;

    offerer->setDelegate(&off_del);
    answerer->setDelegate(&ans_del);

    std::atomic<bool> local_open{false};
    OpenDelegate open_del(&local_open);
    auto dc = offerer->createDataChannel("operations", DataChannelConfig::reliable());
    ASSERT_NE(dc, nullptr);
    dc->setDelegate(&open_del);

    SessionDescription offer_sdp;
    offerer->createOffer([&](bool ok, const SessionDescription& sdp, const std::string& /*e*/) {
        ASSERT_TRUE(ok);
        offer_sdp = sdp;
        offerer->setLocalDescription(sdp, [](bool /*s*/, const std::string& /*e*/) {});
    });

    // Answerer: set remote offer (starts its ICE); candidates go to offerer early
    answerer->setRemoteDescription(offer_sdp, [](bool /*s*/, const std::string& /*e*/) {});
    SessionDescription answer_sdp;
    answerer->createAnswer([&](bool ok, const SessionDescription& sdp, const std::string& /*e*/) {
        ASSERT_TRUE(ok);
        answer_sdp = sdp;
        answerer->setLocalDescription(sdp, [](bool /*s*/, const std::string& /*e*/) {});
    });

    // Give answerer time to emit at least one candidate while offerer still
    // has no remote description.
    for (int i = 0; i < 30 && ans_del.early.empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Now apply answer on offerer — pending remote candidates must flush.
    offerer->setRemoteDescription(answer_sdp, [](bool /*s*/, const std::string& /*e*/) {});
    // Continue trickle both ways after remote is set
    ans_del.buffer_before_remote = false;
    off_del.buffer_before_remote = false;

    for (int i = 0; i < 100 && !local_open.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_TRUE(local_open.load()) << "DataChannel should open when early ICE candidates "
                                      "were buffered until setRemoteDescription";

    offerer->setDelegate(nullptr);
    answerer->setDelegate(nullptr);
    offerer->close();
    answerer->close();
}
#endif  // !__EMSCRIPTEN__

}  // namespace
}  // namespace cells::net
