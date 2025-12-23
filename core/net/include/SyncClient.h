// SyncClient - High-level synchronization client
// Orchestrates signaling, WebRTC connections, and sync protocol
// Uses SyncManager from core/cells/ for CRDT sync logic

#ifndef CELLS_NET_SYNC_CLIENT_H
#define CELLS_NET_SYNC_CLIENT_H

#include <cstdint>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/net/include/ICEConfig.h"
#include "core/net/include/Presence.h"
#include "core/net/include/RTCDataChannel.h"
#include "core/net/include/RTCPeerConnection.h"
#include "core/net/include/SignalingClient.h"

namespace cells {
// Forward declarations from cells namespace
struct Workbook;
class SyncManager;
}  // namespace cells

namespace cells::net {

// Forward declaration
class SyncClient;

// Sync client state
enum class SyncClientState : std::uint8_t {
    OFFLINE,      // Not connected
    CONNECTING,   // Connecting to signaling server
    SYNCING,      // Initial sync with peers
    ONLINE,       // Connected and synced
    RECONNECTING  // Reconnecting after disconnect
};

// Information about a connected peer
struct PeerInfo {
    std::string id;                      // Peer ID
    bool is_connected{false};            // WebRTC connection established
    bool is_synced{false};               // Initial sync completed
    int latency_ms{-1};                  // Round-trip latency (-1 = unknown)
    PeerConnectionState webrtc_state{};  // WebRTC connection state
};

// Delegate interface for SyncClient events
class SyncClientDelegate {
public:
    virtual ~SyncClientDelegate() = default;

    // Sync client state changed
    virtual void syncClientStateDidChange(SyncClient& client, SyncClientState state) = 0;

    // Peer connection state changed
    virtual void syncClientPeerDidChange(SyncClient& client, const PeerInfo& peer) = 0;

    // Peer disconnected
    virtual void syncClientPeerDidDisconnect(SyncClient& client, const std::string& peer_id) = 0;

    // Data was modified by remote operations (trigger UI refresh)
    virtual void syncClientDataDidChange(SyncClient& client) = 0;

    // Optional: Error occurred
    virtual void syncClientDidError(SyncClient& client, const std::string& error) {
        (void)client;
        (void)error;
    }

    // Optional: Latency updated for a peer
    virtual void syncClientLatencyDidUpdate(SyncClient& client, const std::string& peer_id,
                                            int latency_ms) {
        (void)client;
        (void)peer_id;
        (void)latency_ms;
    }

    // Optional: Remote presence updated
    virtual void syncClientPresenceDidUpdate(SyncClient& client, const std::string& peer_id,
                                             const PresenceData& presence) {
        (void)client;
        (void)peer_id;
        (void)presence;
    }

    // Optional: Remote presence removed
    virtual void syncClientPresenceDidRemove(SyncClient& client, const std::string& peer_id) {
        (void)client;
        (void)peer_id;
    }
};

// Configuration for SyncClient
struct SyncClientConfig {
    std::string signaling_url;  // WebSocket URL for signaling server
    RTCConfiguration rtc_config = RTCConfiguration::defaultConfig();

    // Reconnection settings
    int reconnect_delay_ms = 1000;       // Initial reconnect delay
    int max_reconnect_delay_ms = 30000;  // Maximum reconnect delay
    double reconnect_multiplier = 1.5;   // Exponential backoff multiplier
    int max_reconnect_attempts = 10;     // Maximum reconnect attempts

    // Ping/latency settings
    int ping_interval_ms = 5000;  // Interval for latency pings

    // Message pump interval
    int message_pump_interval_ms = 50;  // How often to flush outgoing messages
};

// Connected peer with WebRTC resources
struct ConnectedPeer : public RTCPeerConnectionDelegate, public DataChannelDelegate {
    std::string id;
    std::unique_ptr<RTCPeerConnection> connection;
    std::unique_ptr<RTCDataChannel> operations_channel;  // For sync operations
    std::unique_ptr<RTCDataChannel> presence_channel;    // For presence data
    bool we_initiated{false};                            // True if we created the offer
    int64_t last_ping_time_ms{0};                        // For latency calculation
    int latency_ms{-1};

    // Reference back to SyncClient for callbacks
    SyncClient* sync_client{nullptr};

    // RTCPeerConnectionDelegate
    void peerConnectionStateDidChange(RTCPeerConnection& pc, PeerConnectionState state) override;
    void peerConnectionICEStateDidChange(RTCPeerConnection& pc, ICEConnectionState state) override;
    void peerConnectionDidGatherICECandidate(RTCPeerConnection& pc,
                                             const ICECandidate& candidate) override;
    void peerConnectionDidReceiveDataChannel(RTCPeerConnection& pc,
                                             std::unique_ptr<RTCDataChannel> channel) override;

    // DataChannelDelegate
    void dataChannelDidOpen(RTCDataChannel& channel) override;
    void dataChannelDidClose(RTCDataChannel& channel) override;
    void dataChannelDidReceiveMessage(RTCDataChannel& channel, const std::string& message) override;
    void dataChannelDidReceiveData(RTCDataChannel& channel,
                                   const std::vector<uint8_t>& data) override;

    // Check if peer is ready for sync (both channels open)
    [[nodiscard]] bool isReady() const;
};

// SyncClient - orchestrates P2P synchronization
// Thread-safety: All methods must be called from main thread
class SyncClient : public SignalingClientDelegate {
public:
    // Create a SyncClient for a workbook
    // workbook: The workbook to sync (not owned, must outlive SyncClient)
    explicit SyncClient(Workbook* workbook, SyncClientConfig config);
    ~SyncClient() override;

    // Non-copyable, non-movable
    SyncClient(const SyncClient&) = delete;
    SyncClient& operator=(const SyncClient&) = delete;
    SyncClient(SyncClient&&) = delete;
    SyncClient& operator=(SyncClient&&) = delete;

    // Start sync for a document
    // room_id: Room/document ID to join
    // peer_id: Local peer ID (generated if empty)
    void startSync(const std::string& room_id, const std::string& peer_id = "");

    // Stop sync and disconnect
    void stopSync();

    // Queue local operations for broadcast to peers
    // Call this after local edits to propagate changes
    void broadcastOperations();

    // Process pending messages (call periodically or use internal timer)
    void processOutgoing();

    // State accessors
    [[nodiscard]] SyncClientState getState() const { return state_; }
    [[nodiscard]] const std::string& getRoomId() const { return room_id_; }
    [[nodiscard]] const std::string& getPeerId() const { return peer_id_; }
    [[nodiscard]] bool isConnected() const;

    // Peer information
    [[nodiscard]] size_t getPeerCount() const;
    [[nodiscard]] std::vector<PeerInfo> getPeers() const;
    [[nodiscard]] PeerInfo getPeer(const std::string& peer_id) const;
    [[nodiscard]] int getAverageLatency() const;

    // Statistics
    struct Stats {
        uint64_t operations_sent{0};
        uint64_t operations_received{0};
        uint64_t messages_sent{0};
        uint64_t messages_received{0};
    };
    [[nodiscard]] Stats getStats() const { return stats_; }

    // Delegate
    void setDelegate(SyncClientDelegate* delegate) { delegate_ = delegate; }
    [[nodiscard]] SyncClientDelegate* getDelegate() const { return delegate_; }

    // Force reconnect (e.g., after network change)
    void reconnect();

    // Presence management
    // Get the presence manager for this sync client
    [[nodiscard]] PresenceManager* getPresenceManager() { return presence_manager_.get(); }

    // Update local presence (convenience methods that forward to PresenceManager)
    void setLocalName(const std::string& name);
    void setCurrentSheet(const std::string& sheet_id);
    void setCursor(const std::string& col_id, const std::string& row_id);
    void setSelection(const CursorPosition& start, const CursorPosition& end);
    void setMousePosition(double x, double y);
    void clearCursor();
    void clearSelection();
    void clearMousePosition();

    // Get remote presence info
    [[nodiscard]] std::map<std::string, PresenceData> getRemotePeers() const;
    [[nodiscard]] std::vector<PresenceData> getPeersOnSheet(const std::string& sheet_id) const;

    // SignalingClientDelegate interface
    void signalingClientStateDidChange(SignalingClient& client,
                                       SignalingClientState state) override;
    void signalingClientDidJoinRoom(SignalingClient& client, const std::string& room_id,
                                    const std::vector<std::string>& peers) override;
    void signalingClientPeerDidJoin(SignalingClient& client, const std::string& peer_id) override;
    void signalingClientPeerDidLeave(SignalingClient& client, const std::string& peer_id) override;
    void signalingClientDidReceiveOffer(SignalingClient& client, const std::string& from_peer,
                                        const SessionDescription& sdp) override;
    void signalingClientDidReceiveAnswer(SignalingClient& client, const std::string& from_peer,
                                         const SessionDescription& sdp) override;
    void signalingClientDidReceiveICECandidate(SignalingClient& client,
                                               const std::string& from_peer,
                                               const ICECandidate& candidate) override;

    // Called by ConnectedPeer to handle events
    void handlePeerConnectionStateChange(const std::string& peer_id, PeerConnectionState state);
    void handlePeerDataChannelOpen(const std::string& peer_id, const std::string& channel_label);
    void handlePeerDataChannelClose(const std::string& peer_id, const std::string& channel_label);
    void handlePeerMessage(const std::string& peer_id, const std::string& channel_label,
                           const std::string& message);
    void handlePeerICECandidate(const std::string& peer_id, const ICECandidate& candidate);

private:
    // Generate a random peer ID
    static std::string generatePeerId();

    // Create peer connection to remote peer
    ConnectedPeer* createPeerConnection(const std::string& peer_id, bool we_initiate);

    // Remove peer
    void removePeer(const std::string& peer_id);

    // Handle incoming sync message
    void handleSyncMessage(const std::string& peer_id, const std::string& message);

    // Handle incoming presence message
    void handlePresenceMessage(const std::string& peer_id, const std::string& message);

    // Broadcast presence updates
    void processPresenceUpdates();

    // Handle ping/pong for latency
    void handlePing(const std::string& peer_id, int64_t timestamp);
    void handlePong(const std::string& peer_id, int64_t timestamp);
    void sendPingToAll();

    // Send message to specific peer
    void sendToPeer(const std::string& peer_id, const std::string& message);

    // Send message to all connected peers
    void broadcastToPeers(const std::string& message);

    // Send presence to specific peer (via presence channel)
    void sendPresenceToPeer(const std::string& peer_id, const std::string& message);

    // Broadcast presence to all peers (via presence channel)
    void broadcastPresence(const std::string& message);

    // Update sync state and notify delegate
    void setState(SyncClientState new_state);

    // Check if all peers are synced
    void updateSyncState();

    // Notify peer was ready (add to SyncManager)
    void notifyPeerReady(const std::string& peer_id);

    // Config
    SyncClientConfig config_;

    // Workbook and SyncManager (from core/cells/)
    Workbook* workbook_;
    SyncManager* sync_manager_{nullptr};  // Created by SyncClient, not owned by Workbook

    // Signaling
    std::unique_ptr<SignalingClient> signaling_client_;

    // Presence
    std::unique_ptr<PresenceManager> presence_manager_;

    // Connected peers
    std::map<std::string, std::unique_ptr<ConnectedPeer>> peers_;

    // State
    SyncClientState state_ = SyncClientState::OFFLINE;
    std::string room_id_;
    std::string peer_id_;

    // Delegate
    SyncClientDelegate* delegate_ = nullptr;

    // Statistics
    Stats stats_;
};

// Convert SyncClientState to string (for logging)
const char* syncClientStateToString(SyncClientState state);

}  // namespace cells::net

#endif  // CELLS_NET_SYNC_CLIENT_H
