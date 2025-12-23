// RTCPeerConnection abstraction for WebRTC peer-to-peer connections
// Platform-specific implementations in web/ and apple/ directories

#ifndef CELLS_NET_RTC_PEER_CONNECTION_H
#define CELLS_NET_RTC_PEER_CONNECTION_H

#include <cstdint>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/net/include/ICEConfig.h"
#include "core/net/include/RTCDataChannel.h"

namespace cells::net {

// Forward declaration
class RTCPeerConnection;

// Peer connection state
enum class PeerConnectionState : std::uint8_t {
    NEW,           // Initial state
    CONNECTING,    // Connection is being established
    CONNECTED,     // Connection established
    DISCONNECTED,  // Temporarily disconnected
    FAILED,        // Connection failed
    CLOSED         // Connection closed
};

// ICE connection state
enum class ICEConnectionState : std::uint8_t {
    NEW,           // ICE agent is gathering candidates
    CHECKING,      // ICE agent is checking candidates
    CONNECTED,     // ICE agent has found a valid pair
    COMPLETED,     // ICE agent has finished gathering
    DISCONNECTED,  // ICE connectivity lost
    FAILED,        // ICE connectivity check failed
    CLOSED         // ICE agent has shut down
};

// ICE gathering state
enum class ICEGatheringState : std::uint8_t {
    NEW,        // ICE agent is waiting to start
    GATHERING,  // ICE agent is gathering candidates
    COMPLETE    // ICE agent has finished gathering
};

// Signaling state
enum class SignalingState : std::uint8_t {
    STABLE,                // No offer/answer exchange in progress
    HAVE_LOCAL_OFFER,      // Local offer set, waiting for answer
    HAVE_REMOTE_OFFER,     // Remote offer received, need to create answer
    HAVE_LOCAL_PRANSWER,   // Local provisional answer set
    HAVE_REMOTE_PRANSWER,  // Remote provisional answer received
    CLOSED                 // Connection closed
};

// Session Description type
enum class SDPType : std::uint8_t { OFFER, PRANSWER, ANSWER, ROLLBACK };

// Session Description (SDP)
struct SessionDescription {
    SDPType type = SDPType::OFFER;
    std::string sdp;

    SessionDescription() = default;
    SessionDescription(SDPType t, std::string s) : type(t), sdp(std::move(s)) {}

    // Convenience for creating offer/answer
    static SessionDescription offer(std::string sdp) { return {SDPType::OFFER, std::move(sdp)}; }

    static SessionDescription answer(std::string sdp) { return {SDPType::ANSWER, std::move(sdp)}; }
};

// ICE Candidate
struct ICECandidate {
    std::string candidate;    // Candidate string (SDP format)
    std::string sdp_mid;      // Media stream identifier
    int sdp_mline_index = 0;  // Index of the m= line

    ICECandidate() = default;
    ICECandidate(std::string cand, std::string mid, int index)
        : candidate(std::move(cand)), sdp_mid(std::move(mid)), sdp_mline_index(index) {}

    // Check if candidate is empty (end-of-candidates indicator)
    [[nodiscard]] bool isEmpty() const { return candidate.empty(); }
};

// Delegate interface for RTCPeerConnection events
class RTCPeerConnectionDelegate {
public:
    virtual ~RTCPeerConnectionDelegate() = default;

    // Connection state changed
    virtual void peerConnectionStateDidChange(RTCPeerConnection& pc, PeerConnectionState state) = 0;

    // ICE connection state changed
    virtual void peerConnectionICEStateDidChange(RTCPeerConnection& pc,
                                                 ICEConnectionState state) = 0;

    // ICE gathering state changed
    virtual void peerConnectionICEGatheringStateDidChange(RTCPeerConnection& pc,
                                                          ICEGatheringState state) {
        (void)pc;
        (void)state;
    }

    // New ICE candidate gathered (send to remote peer)
    virtual void peerConnectionDidGatherICECandidate(RTCPeerConnection& pc,
                                                     const ICECandidate& candidate) = 0;

    // Remote peer opened a DataChannel
    virtual void peerConnectionDidReceiveDataChannel(RTCPeerConnection& pc,
                                                     std::unique_ptr<RTCDataChannel> channel) = 0;

    // Signaling state changed
    virtual void peerConnectionSignalingStateDidChange(RTCPeerConnection& pc,
                                                       SignalingState state) {
        (void)pc;
        (void)state;
    }

    // Negotiation needed (should create offer)
    virtual void peerConnectionNegotiationNeeded(RTCPeerConnection& pc) { (void)pc; }
};

// Callbacks for async operations
using CreateSDPCallback =
    std::function<void(bool success, const SessionDescription& sdp, const std::string& error)>;
using SetSDPCallback = std::function<void(bool success, const std::string& error)>;

// RTCPeerConnection - WebRTC peer connection
// Manages ICE, SDP negotiation, and DataChannels
class RTCPeerConnection {
public:
    virtual ~RTCPeerConnection() = default;

    // Factory method - create platform-specific implementation
    static std::unique_ptr<RTCPeerConnection> make(
        const RTCConfiguration& config = RTCConfiguration::defaultConfig());

    // SDP offer/answer negotiation
    virtual void createOffer(CreateSDPCallback callback) = 0;
    virtual void createAnswer(CreateSDPCallback callback) = 0;
    virtual void setLocalDescription(const SessionDescription& sdp, SetSDPCallback callback) = 0;
    virtual void setRemoteDescription(const SessionDescription& sdp, SetSDPCallback callback) = 0;

    // ICE candidate handling
    virtual void addIceCandidate(const ICECandidate& candidate, SetSDPCallback callback) = 0;

    // DataChannel creation
    virtual std::unique_ptr<RTCDataChannel> createDataChannel(
        const std::string& label, const DataChannelConfig& config = {}) = 0;

    // Close the connection
    virtual void close() = 0;

    // State accessors
    [[nodiscard]] virtual PeerConnectionState getConnectionState() const = 0;
    [[nodiscard]] virtual ICEConnectionState getICEConnectionState() const = 0;
    [[nodiscard]] virtual ICEGatheringState getICEGatheringState() const = 0;
    [[nodiscard]] virtual SignalingState getSignalingState() const = 0;

    // Local/remote descriptions
    [[nodiscard]] virtual const SessionDescription* getLocalDescription() const = 0;
    [[nodiscard]] virtual const SessionDescription* getRemoteDescription() const = 0;

    // Delegate for events
    void setDelegate(RTCPeerConnectionDelegate* delegate) { delegate_ = delegate; }
    [[nodiscard]] RTCPeerConnectionDelegate* getDelegate() const { return delegate_; }

    // Convenience methods for checking state
    [[nodiscard]] bool isConnected() const {
        return getConnectionState() == PeerConnectionState::CONNECTED;
    }

    [[nodiscard]] bool isClosed() const {
        return getConnectionState() == PeerConnectionState::CLOSED;
    }

protected:
    RTCPeerConnection() = default;

    // Called by platform implementations to notify delegate
    void notifyConnectionStateChange(PeerConnectionState state);
    void notifyICEConnectionStateChange(ICEConnectionState state);
    void notifyICEGatheringStateChange(ICEGatheringState state);
    void notifySignalingStateChange(SignalingState state);
    void notifyICECandidate(const ICECandidate& candidate);
    void notifyDataChannel(std::unique_ptr<RTCDataChannel> channel);
    void notifyNegotiationNeeded();

    RTCPeerConnectionDelegate* delegate_ = nullptr;
};

// Convert enums to strings (for logging/debugging)
const char* peerConnectionStateToString(PeerConnectionState state);
const char* iceConnectionStateToString(ICEConnectionState state);
const char* iceGatheringStateToString(ICEGatheringState state);
const char* signalingStateToString(SignalingState state);
const char* sdpTypeToString(SDPType type);

// Parse SDP type from string
SDPType sdpTypeFromString(const std::string& type);

}  // namespace cells::net

#endif  // CELLS_NET_RTC_PEER_CONNECTION_H
