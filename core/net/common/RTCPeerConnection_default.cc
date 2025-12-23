// Default (stub) RTCPeerConnection implementation
// Used when no platform-specific implementation is available

// This default implementation is used when:
// - Not building for Emscripten (which has its own implementation)
// - Apple platforms without WebRTC.framework linked
// The BUILD file selects this for all non-Emscripten targets until
// rules_apple is integrated for native Apple builds with WebRTC.framework.
#if !defined(__EMSCRIPTEN__)

#include "core/net/include/RTCPeerConnection.h"

namespace cells::net {

class DefaultRTCPeerConnection : public RTCPeerConnection {
public:
    explicit DefaultRTCPeerConnection(const RTCConfiguration& /*config*/) {}

    void createOffer(CreateSDPCallback callback) override {
        callback(false, SessionDescription(), "WebRTC not available on this platform");
    }

    void createAnswer(CreateSDPCallback callback) override {
        callback(false, SessionDescription(), "WebRTC not available on this platform");
    }

    void setLocalDescription(const SessionDescription& /*sdp*/, SetSDPCallback callback) override {
        callback(false, "WebRTC not available on this platform");
    }

    void setRemoteDescription(const SessionDescription& /*sdp*/, SetSDPCallback callback) override {
        callback(false, "WebRTC not available on this platform");
    }

    void addIceCandidate(const ICECandidate& /*candidate*/, SetSDPCallback callback) override {
        callback(false, "WebRTC not available on this platform");
    }

    std::unique_ptr<RTCDataChannel> createDataChannel(
        const std::string& /*label*/, const DataChannelConfig& /*config*/) override {
        return nullptr;
    }

    void close() override {}

    [[nodiscard]] PeerConnectionState getConnectionState() const override {
        return PeerConnectionState::CLOSED;
    }

    [[nodiscard]] ICEConnectionState getICEConnectionState() const override {
        return ICEConnectionState::CLOSED;
    }

    [[nodiscard]] ICEGatheringState getICEGatheringState() const override {
        return ICEGatheringState::COMPLETE;
    }

    [[nodiscard]] SignalingState getSignalingState() const override {
        return SignalingState::CLOSED;
    }

    [[nodiscard]] const SessionDescription* getLocalDescription() const override { return nullptr; }

    [[nodiscard]] const SessionDescription* getRemoteDescription() const override {
        return nullptr;
    }
};

// Factory implementation (stub)
std::unique_ptr<RTCPeerConnection> RTCPeerConnection::make(const RTCConfiguration& config) {
    return std::make_unique<DefaultRTCPeerConnection>(config);
}

}  // namespace cells::net

#endif  // !__EMSCRIPTEN__
