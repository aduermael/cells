// WebSocket signaling client for WebRTC connection setup
// Connects to the Go signaling server for offer/answer/ICE exchange

#ifndef CELLS_NET_SIGNALING_CLIENT_H
#define CELLS_NET_SIGNALING_CLIENT_H

#include <cstdint>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/net/include/Connection.h"
#include "core/net/include/RTCPeerConnection.h"
#include "core/net/include/WSConnection.h"

namespace cells::net {

// Forward declaration
class SignalingClient;

// Signaling client connection state
enum class SignalingClientState : std::uint8_t {
    DISCONNECTED,  // Not connected to server
    CONNECTING,    // Connection in progress
    CONNECTED,     // Connected to server
    RECONNECTING,  // Reconnecting after disconnect
    IN_ROOM        // Connected and joined a room
};

// Delegate interface for signaling events
class SignalingClientDelegate {
public:
    virtual ~SignalingClientDelegate() = default;

    // Connection state changed
    virtual void signalingClientStateDidChange(SignalingClient& client,
                                               SignalingClientState state) = 0;

    // Successfully joined a room, with list of existing peers
    virtual void signalingClientDidJoinRoom(SignalingClient& client, const std::string& room_id,
                                            const std::vector<std::string>& peers) = 0;

    // A new peer joined the room
    virtual void signalingClientPeerDidJoin(SignalingClient& client,
                                            const std::string& peer_id) = 0;

    // A peer left the room
    virtual void signalingClientPeerDidLeave(SignalingClient& client,
                                             const std::string& peer_id) = 0;

    // Received an SDP offer from a peer
    virtual void signalingClientDidReceiveOffer(SignalingClient& client,
                                                const std::string& from_peer,
                                                const SessionDescription& sdp) = 0;

    // Received an SDP answer from a peer
    virtual void signalingClientDidReceiveAnswer(SignalingClient& client,
                                                 const std::string& from_peer,
                                                 const SessionDescription& sdp) = 0;

    // Received an ICE candidate from a peer
    virtual void signalingClientDidReceiveICECandidate(SignalingClient& client,
                                                       const std::string& from_peer,
                                                       const ICECandidate& candidate) = 0;

    // Optional: Server error
    virtual void signalingClientDidReceiveError(SignalingClient& client, const std::string& error) {
        (void)client;
        (void)error;
    }

    // Optional: Reconnection events
    virtual void signalingClientWillReconnect(SignalingClient& client, int attempt, int delay_ms) {
        (void)client;
        (void)attempt;
        (void)delay_ms;
    }

    virtual void signalingClientDidReconnect(SignalingClient& client) { (void)client; }

    virtual void signalingClientMaxReconnectsReached(SignalingClient& client) { (void)client; }
};

// Configuration for SignalingClient
struct SignalingClientConfig {
    std::string url;                     // WebSocket URL (e.g., "wss://example.com/ws")
    int reconnect_delay_ms = 1000;       // Initial reconnect delay
    int max_reconnect_delay_ms = 30000;  // Maximum reconnect delay
    double reconnect_multiplier = 1.5;   // Exponential backoff multiplier
    int max_reconnect_attempts = 10;     // Maximum reconnect attempts
};

// WebSocket signaling client for WebRTC setup
// Protocol-compatible with existing JS signaling-client.js
class SignalingClient : public ConnectionDelegate {
public:
    explicit SignalingClient(SignalingClientConfig config);
    ~SignalingClient() override;

    // Non-copyable, non-movable (due to WebSocket ownership)
    SignalingClient(const SignalingClient&) = delete;
    SignalingClient& operator=(const SignalingClient&) = delete;
    SignalingClient(SignalingClient&&) = delete;
    SignalingClient& operator=(SignalingClient&&) = delete;

    // Connect to server and join a room
    void connect(const std::string& room_id, const std::string& peer_id);

    // Disconnect from server
    void disconnect();

    // Leave current room (stays connected)
    void leaveRoom();

    // Send SDP offer to a specific peer
    void sendOffer(const std::string& target_peer, const SessionDescription& sdp);

    // Send SDP answer to a specific peer
    void sendAnswer(const std::string& target_peer, const SessionDescription& sdp);

    // Send ICE candidate to a specific peer
    void sendICECandidate(const std::string& target_peer, const ICECandidate& candidate);

    // State accessors
    [[nodiscard]] SignalingClientState getState() const { return state_; }
    [[nodiscard]] const std::string& getRoomId() const { return room_id_; }
    [[nodiscard]] const std::string& getPeerId() const { return peer_id_; }
    [[nodiscard]] bool isConnected() const;

    // Delegate
    void setDelegate(SignalingClientDelegate* delegate) { delegate_ = delegate; }
    [[nodiscard]] SignalingClientDelegate* getDelegate() const { return delegate_; }

    // ConnectionDelegate interface
    void connectionDidEstablish(Connection& connection) override;
    void connectionDidReceive(Connection& connection, const Payload& payload) override;
    void connectionDidClose(Connection& connection) override;
    void connectionDidError(Connection& connection, const std::string& error) override;

private:
    // Send a JSON message
    void sendMessage(const std::string& json);

    // Parse and handle incoming message
    void handleMessage(const std::string& json);

    // Send join message
    void sendJoin();

    // Reconnection logic
    void scheduleReconnect();
    void attemptReconnect();
    void cancelReconnect();

    // Notify delegate of state change
    void setState(SignalingClientState new_state);

    SignalingClientConfig config_;
    std::unique_ptr<WSConnection> ws_;
    SignalingClientDelegate* delegate_ = nullptr;

    SignalingClientState state_ = SignalingClientState::DISCONNECTED;
    std::string room_id_;
    std::string peer_id_;

    // Reconnection state
    bool should_reconnect_ = false;
    int reconnect_attempts_ = 0;
    int current_reconnect_delay_ms_;
    // Timer for reconnection (platform-specific, null when not scheduled)
    // For now, we use a simple polling approach - can be enhanced later
};

// Convert SignalingClientState to string (for logging)
const char* signalingClientStateToString(SignalingClientState state);

}  // namespace cells::net

#endif  // CELLS_NET_SIGNALING_CLIENT_H
