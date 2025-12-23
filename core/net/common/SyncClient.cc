// SyncClient implementation - orchestrates P2P synchronization

#include "core/net/include/SyncClient.h"

#include <algorithm>
#include <random>
#include <sstream>

#include "core/cells/hlc.h"
#include "core/cells/model.h"
#include "core/cells/sync_manager.h"

namespace cells::net {

namespace {

// Data channel labels
constexpr const char* OPERATIONS_CHANNEL = "operations";
constexpr const char* PRESENCE_CHANNEL = "presence";

// Base62 characters for peer ID generation
const char kBase62Chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

// Simple JSON helpers
std::string extractJSONString(const std::string& json, const std::string& key) {
    const std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) {
        return "";
    }
    pos += searchKey.length();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }

    if (pos >= json.size() || json[pos] != '"') {
        return "";
    }

    pos++;  // Skip opening quote
    size_t end = pos;
    while (end < json.size() && json[end] != '"') {
        if (json[end] == '\\' && end + 1 < json.size()) {
            end++;  // Skip escaped char
        }
        end++;
    }

    return json.substr(pos, end - pos);
}

int64_t extractJSONInt(const std::string& json, const std::string& key, int64_t defaultValue = 0) {
    const std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) {
        return defaultValue;
    }
    pos += searchKey.length();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }

    if (pos >= json.size()) {
        return defaultValue;
    }

    // Parse integer
    int64_t value = 0;
    bool negative = false;
    if (json[pos] == '-') {
        negative = true;
        pos++;
    }
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        value = value * 10 + (json[pos] - '0');
        pos++;
    }
    return negative ? -value : value;
}

}  // namespace

// ============================================================================
// ConnectedPeer implementation
// ============================================================================

bool ConnectedPeer::isReady() const {
    return operations_channel && operations_channel->isOpen();
}

void ConnectedPeer::peerConnectionStateDidChange(RTCPeerConnection& /*pc*/,
                                                 PeerConnectionState state) {
    if (sync_client) {
        sync_client->handlePeerConnectionStateChange(id, state);
    }
}

void ConnectedPeer::peerConnectionICEStateDidChange(RTCPeerConnection& /*pc*/,
                                                    ICEConnectionState /*state*/) {
    // Handled via connection state changes
}

void ConnectedPeer::peerConnectionDidGatherICECandidate(RTCPeerConnection& /*pc*/,
                                                        const ICECandidate& candidate) {
    if (sync_client) {
        sync_client->handlePeerICECandidate(id, candidate);
    }
}

void ConnectedPeer::peerConnectionDidReceiveDataChannel(RTCPeerConnection& /*pc*/,
                                                        std::unique_ptr<RTCDataChannel> channel) {
    if (!channel) {
        return;
    }

    const std::string label = channel->getLabel();
    channel->setDelegate(this);

    if (label == OPERATIONS_CHANNEL) {
        operations_channel = std::move(channel);
    } else if (label == PRESENCE_CHANNEL) {
        presence_channel = std::move(channel);
    }
}

void ConnectedPeer::dataChannelDidOpen(RTCDataChannel& channel) {
    if (sync_client) {
        sync_client->handlePeerDataChannelOpen(id, channel.getLabel());
    }
}

void ConnectedPeer::dataChannelDidClose(RTCDataChannel& channel) {
    if (sync_client) {
        sync_client->handlePeerDataChannelClose(id, channel.getLabel());
    }
}

void ConnectedPeer::dataChannelDidReceiveMessage(RTCDataChannel& channel,
                                                 const std::string& message) {
    if (sync_client) {
        sync_client->handlePeerMessage(id, channel.getLabel(), message);
    }
}

void ConnectedPeer::dataChannelDidReceiveData(RTCDataChannel& /*channel*/,
                                              const std::vector<uint8_t>& /*data*/) {
    // Binary data not used for sync protocol
}

// ============================================================================
// SyncClient implementation
// ============================================================================

SyncClient::SyncClient(Workbook* workbook, SyncClientConfig config)
    : config_(std::move(config)), workbook_(workbook) {
    // Create signaling client
    SignalingClientConfig signaling_config;
    signaling_config.url = config_.signaling_url;
    signaling_config.reconnect_delay_ms = config_.reconnect_delay_ms;
    signaling_config.max_reconnect_delay_ms = config_.max_reconnect_delay_ms;
    signaling_config.reconnect_multiplier = config_.reconnect_multiplier;
    signaling_config.max_reconnect_attempts = config_.max_reconnect_attempts;

    signaling_client_ = std::make_unique<SignalingClient>(signaling_config);
    signaling_client_->setDelegate(this);

    // Create presence manager
    presence_manager_ = std::make_unique<PresenceManager>();
}

SyncClient::~SyncClient() {
    stopSync();
}

void SyncClient::startSync(const std::string& room_id, const std::string& peer_id) {
    if (state_ != SyncClientState::OFFLINE) {
        stopSync();
    }

    room_id_ = room_id;
    peer_id_ = peer_id.empty() ? generatePeerId() : peer_id;

    // Set node ID on workbook for HLC generation
    workbook_->setNodeId(cells::ID(peer_id_));

    // Start collaboration mode
    workbook_->startCollaboration();

    // Create SyncManager if not already created
    if (!sync_manager_) {
        sync_manager_ = new SyncManager(workbook_);
    }

    // Initialize presence manager with our peer ID
    presence_manager_->initialize(peer_id_);

    setState(SyncClientState::CONNECTING);
    signaling_client_->connect(room_id_, peer_id_);
}

void SyncClient::stopSync() {
    // Close all peer connections
    for (auto& pair : peers_) {
        if (pair.second->connection) {
            pair.second->connection->close();
        }
    }
    peers_.clear();

    // Disconnect signaling
    signaling_client_->disconnect();

    // Clean up SyncManager
    delete sync_manager_;
    sync_manager_ = nullptr;

    room_id_.clear();
    peer_id_.clear();
    setState(SyncClientState::OFFLINE);
}

void SyncClient::broadcastOperations() {
    if (!sync_manager_) {
        return;
    }

    sync_manager_->queueOperationsBroadcast();
    processOutgoing();
}

void SyncClient::processOutgoing() {
    if (!sync_manager_) {
        return;
    }

    auto messages = sync_manager_->getOutgoingMessages();
    for (const auto& msg : messages) {
        if (msg.isBroadcast()) {
            broadcastToPeers(msg.json);
        } else {
            sendToPeer(msg.peerId.toString(), msg.json);
        }
        stats_.messages_sent++;
    }
}

bool SyncClient::isConnected() const {
    return state_ == SyncClientState::ONLINE || state_ == SyncClientState::SYNCING;
}

size_t SyncClient::getPeerCount() const {
    size_t count = 0;
    for (const auto& pair : peers_) {
        if (pair.second->isReady()) {
            count++;
        }
    }
    return count;
}

std::vector<PeerInfo> SyncClient::getPeers() const {
    std::vector<PeerInfo> result;
    for (const auto& pair : peers_) {
        PeerInfo info;
        info.id = pair.first;
        info.is_connected = pair.second->isReady();
        info.latency_ms = pair.second->latency_ms;
        if (pair.second->connection) {
            info.webrtc_state = pair.second->connection->getConnectionState();
        }

        // Get sync state from SyncManager
        if (sync_manager_) {
            const auto* peer_state = sync_manager_->getPeerSyncState(cells::ID(info.id));
            if (peer_state) {
                info.is_synced = peer_state->isSynced;
            }
        }

        result.push_back(info);
    }
    return result;
}

PeerInfo SyncClient::getPeer(const std::string& peer_id) const {
    PeerInfo info;
    info.id = peer_id;

    auto it = peers_.find(peer_id);
    if (it != peers_.end()) {
        info.is_connected = it->second->isReady();
        info.latency_ms = it->second->latency_ms;
        if (it->second->connection) {
            info.webrtc_state = it->second->connection->getConnectionState();
        }
    }

    if (sync_manager_) {
        const auto* peer_state = sync_manager_->getPeerSyncState(cells::ID(peer_id));
        if (peer_state) {
            info.is_synced = peer_state->isSynced;
        }
    }

    return info;
}

int SyncClient::getAverageLatency() const {
    int total = 0;
    int count = 0;
    for (const auto& pair : peers_) {
        if (pair.second->latency_ms >= 0) {
            total += pair.second->latency_ms;
            count++;
        }
    }
    return count > 0 ? total / count : -1;
}

void SyncClient::reconnect() {
    if (room_id_.empty() || peer_id_.empty()) {
        return;
    }

    // Close existing connections
    for (auto& pair : peers_) {
        if (pair.second->connection) {
            pair.second->connection->close();
        }
    }
    peers_.clear();

    setState(SyncClientState::RECONNECTING);
    signaling_client_->disconnect();
    signaling_client_->connect(room_id_, peer_id_);
}

std::string SyncClient::generatePeerId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 61);

    std::string id;
    id.reserve(8);
    for (int i = 0; i < 8; i++) {
        id += kBase62Chars[dis(gen)];
    }
    return id;
}

ConnectedPeer* SyncClient::createPeerConnection(const std::string& peer_id, bool we_initiate) {
    auto peer = std::make_unique<ConnectedPeer>();
    peer->id = peer_id;
    peer->we_initiated = we_initiate;
    peer->sync_client = this;

    // Create RTCPeerConnection
    peer->connection = RTCPeerConnection::make(config_.rtc_config);
    if (!peer->connection) {
        return nullptr;
    }
    peer->connection->setDelegate(peer.get());

    // If we're initiating, create data channels
    if (we_initiate) {
        peer->operations_channel =
            peer->connection->createDataChannel(OPERATIONS_CHANNEL, DataChannelConfig::reliable());
        if (peer->operations_channel) {
            peer->operations_channel->setDelegate(peer.get());
        }

        peer->presence_channel =
            peer->connection->createDataChannel(PRESENCE_CHANNEL, DataChannelConfig::unreliable());
        if (peer->presence_channel) {
            peer->presence_channel->setDelegate(peer.get());
        }
    }

    // NOLINTNEXTLINE(misc-const-correctness)
    ConnectedPeer* ptr = peer.get();
    peers_[peer_id] = std::move(peer);
    return ptr;
}

void SyncClient::removePeer(const std::string& peer_id) {
    auto it = peers_.find(peer_id);
    if (it != peers_.end()) {
        if (it->second->connection) {
            it->second->connection->close();
        }
        peers_.erase(it);
    }

    // Remove from SyncManager
    if (sync_manager_) {
        sync_manager_->removePeer(cells::ID(peer_id));
    }

    // Remove presence and notify
    if (presence_manager_) {
        presence_manager_->removePeer(peer_id);
        if (delegate_) {
            delegate_->syncClientPresenceDidRemove(*this, peer_id);
        }
    }

    // Notify delegate
    if (delegate_) {
        delegate_->syncClientPeerDidDisconnect(*this, peer_id);
    }

    updateSyncState();
}

void SyncClient::handleSyncMessage(const std::string& peer_id, const std::string& message) {
    if (!sync_manager_) {
        return;
    }

    stats_.messages_received++;

    // Route message through SyncManager
    auto result = sync_manager_->handleMessage(cells::ID(peer_id), message);

    // Send any response messages
    for (const auto& msg : result.messages) {
        if (msg.isBroadcast()) {
            broadcastToPeers(msg.json);
        } else {
            sendToPeer(msg.peerId.toString(), msg.json);
        }
        stats_.messages_sent++;
    }

    // If data was modified, notify delegate
    if (result.dataModified && delegate_) {
        delegate_->syncClientDataDidChange(*this);
    }

    // Update sync state
    updateSyncState();
}

void SyncClient::handlePing(const std::string& peer_id, int64_t timestamp) {
    // Send pong response
    std::ostringstream oss;
    oss << "{\"type\":\"pong\",\"ts\":" << timestamp << "}";
    sendToPeer(peer_id, oss.str());
}

void SyncClient::handlePong(const std::string& peer_id, int64_t timestamp) {
    auto it = peers_.find(peer_id);
    if (it == peers_.end()) {
        return;
    }

    const int64_t now = cells::current_time_ms();
    const int latency = static_cast<int>(now - timestamp);
    it->second->latency_ms = latency;

    if (delegate_) {
        delegate_->syncClientLatencyDidUpdate(*this, peer_id, latency);
    }
}

void SyncClient::sendPingToAll() {
    const int64_t now = cells::current_time_ms();

    std::ostringstream oss;
    oss << "{\"type\":\"ping\",\"ts\":" << now << "}";
    const std::string ping_msg = oss.str();

    for (auto& pair : peers_) {
        if (pair.second->isReady()) {
            pair.second->last_ping_time_ms = now;
            sendToPeer(pair.first, ping_msg);
        }
    }
}

void SyncClient::sendToPeer(const std::string& peer_id, const std::string& message) {
    auto it = peers_.find(peer_id);
    if (it == peers_.end()) {
        return;
    }

    if (it->second->operations_channel && it->second->operations_channel->isOpen()) {
        it->second->operations_channel->send(message);
    }
}

void SyncClient::broadcastToPeers(const std::string& message) {
    for (auto& pair : peers_) {
        if (pair.second->operations_channel && pair.second->operations_channel->isOpen()) {
            pair.second->operations_channel->send(message);
        }
    }
}

void SyncClient::sendPresenceToPeer(const std::string& peer_id, const std::string& message) {
    auto it = peers_.find(peer_id);
    if (it == peers_.end()) {
        return;
    }

    if (it->second->presence_channel && it->second->presence_channel->isOpen()) {
        it->second->presence_channel->send(message);
    }
}

void SyncClient::broadcastPresence(const std::string& message) {
    for (auto& pair : peers_) {
        if (pair.second->presence_channel && pair.second->presence_channel->isOpen()) {
            pair.second->presence_channel->send(message);
        }
    }
}

// ============================================================================
// Presence methods
// ============================================================================

void SyncClient::setLocalName(const std::string& name) {
    if (presence_manager_) {
        presence_manager_->setLocalName(name);
    }
}

void SyncClient::setCurrentSheet(const std::string& sheet_id) {
    if (presence_manager_) {
        presence_manager_->setCurrentSheet(sheet_id);
    }
}

void SyncClient::setCursor(const std::string& col_id, const std::string& row_id) {
    if (presence_manager_) {
        presence_manager_->setCursor(col_id, row_id);
    }
}

void SyncClient::setSelection(const CursorPosition& start, const CursorPosition& end) {
    if (presence_manager_) {
        presence_manager_->setSelection(start, end);
    }
}

void SyncClient::setMousePosition(double x, double y) {
    if (presence_manager_) {
        presence_manager_->setMousePosition(x, y);
    }
}

void SyncClient::clearCursor() {
    if (presence_manager_) {
        presence_manager_->clearCursor();
    }
}

void SyncClient::clearSelection() {
    if (presence_manager_) {
        presence_manager_->clearSelection();
    }
}

void SyncClient::clearMousePosition() {
    if (presence_manager_) {
        presence_manager_->clearMousePosition();
    }
}

std::map<std::string, PresenceData> SyncClient::getRemotePeers() const {
    if (presence_manager_) {
        return presence_manager_->getRemotePeers();
    }
    return {};
}

std::vector<PresenceData> SyncClient::getPeersOnSheet(const std::string& sheet_id) const {
    if (presence_manager_) {
        return presence_manager_->getPeersOnSheet(sheet_id);
    }
    return {};
}

void SyncClient::handlePresenceMessage(const std::string& peer_id, const std::string& message) {
    if (!presence_manager_) {
        return;
    }

    presence_manager_->handlePresenceMessage(peer_id, message);

    // Notify delegate
    if (delegate_) {
        const PresenceData* presence = presence_manager_->getPeerPresence(peer_id);
        if (presence) {
            delegate_->syncClientPresenceDidUpdate(*this, peer_id, *presence);
        }
    }
}

void SyncClient::processPresenceUpdates() {
    if (!presence_manager_) {
        return;
    }

    std::string message;
    if (presence_manager_->processPendingUpdates(message)) {
        broadcastPresence(message);
    }
}

void SyncClient::setState(SyncClientState new_state) {
    if (state_ != new_state) {
        state_ = new_state;
        if (delegate_) {
            delegate_->syncClientStateDidChange(*this, new_state);
        }
    }
}

void SyncClient::updateSyncState() {
    if (!sync_manager_) {
        return;
    }

    // Check if we have any ready peers
    const size_t ready_count = getPeerCount();

    if (ready_count == 0) {
        // No peers - we're online (alone in the room)
        if (state_ == SyncClientState::SYNCING) {
            setState(SyncClientState::ONLINE);
        }
        return;
    }

    // Check if all peers are synced
    bool all_synced = true;
    for (const auto& peer_id : sync_manager_->getPeerIds()) {
        const auto* peer_state = sync_manager_->getPeerSyncState(peer_id);
        if (peer_state && !peer_state->isSynced) {
            all_synced = false;
            break;
        }
    }

    if (all_synced && state_ == SyncClientState::SYNCING) {
        setState(SyncClientState::ONLINE);
    }
}

void SyncClient::notifyPeerReady(const std::string& peer_id) {
    if (!sync_manager_) {
        return;
    }

    // Add peer to SyncManager (queues hello message)
    sync_manager_->addPeer(cells::ID(peer_id));

    // Flush outgoing messages
    processOutgoing();

    // Notify delegate
    if (delegate_) {
        delegate_->syncClientPeerDidChange(*this, getPeer(peer_id));
    }
}

// ============================================================================
// SignalingClientDelegate implementation
// ============================================================================

void SyncClient::signalingClientStateDidChange(SignalingClient& /*client*/,
                                               SignalingClientState state) {
    switch (state) {
        case SignalingClientState::DISCONNECTED:
            if (state_ != SyncClientState::OFFLINE) {
                // Unexpected disconnect
                if (delegate_) {
                    delegate_->syncClientDidError(*this, "Signaling disconnected");
                }
            }
            break;

        case SignalingClientState::CONNECTING:
        case SignalingClientState::CONNECTED:
            // Connection in progress or established, waiting for room join
            break;

        case SignalingClientState::RECONNECTING:
            setState(SyncClientState::RECONNECTING);
            break;

        case SignalingClientState::IN_ROOM:
            // Successfully joined room
            break;
    }
}

void SyncClient::signalingClientDidJoinRoom(SignalingClient& /*client*/,
                                            const std::string& /*room_id*/,
                                            const std::vector<std::string>& existing_peers) {
    if (existing_peers.empty()) {
        // We're the first/only peer - go online
        setState(SyncClientState::ONLINE);
    } else {
        // Existing peers will initiate connections to us via peer-joined
        setState(SyncClientState::SYNCING);
    }
}

void SyncClient::signalingClientPeerDidJoin(SignalingClient& /*client*/,
                                            const std::string& peer_id) {
    // We initiate connection to new peer
    ConnectedPeer* peer = createPeerConnection(peer_id, true);
    if (!peer || !peer->connection) {
        return;
    }

    // Create offer
    peer->connection->createOffer(
        // NOLINTNEXTLINE(bugprone-exception-escape) - std::string copy is acceptable
        [this, peer_id](bool success, const SessionDescription& sdp, const std::string& /*error*/) {
            if (!success) {
                return;
            }

            auto it = peers_.find(peer_id);
            if (it == peers_.end()) {
                return;
            }

            // Set local description
            it->second->connection->setLocalDescription(
                sdp,
                // NOLINTNEXTLINE(bugprone-exception-escape)
                [this, peer_id, sdp](bool set_success, const std::string& /*error*/) {
                    if (set_success) {
                        // Send offer to peer
                        signaling_client_->sendOffer(peer_id, sdp);
                    }
                });
        });
}

void SyncClient::signalingClientPeerDidLeave(SignalingClient& /*client*/,
                                             const std::string& peer_id) {
    removePeer(peer_id);
}

void SyncClient::signalingClientDidReceiveOffer(SignalingClient& /*client*/,
                                                const std::string& from_peer,
                                                const SessionDescription& sdp) {
    // Accept peer connection
    ConnectedPeer* peer = createPeerConnection(from_peer, false);
    if (!peer || !peer->connection) {
        return;
    }

    // Set remote description
    peer->connection->setRemoteDescription(
        sdp,
        // NOLINTNEXTLINE(bugprone-exception-escape)
        [this, from_peer](bool success, const std::string& /*error*/) {
            if (!success) {
                return;
            }

            auto it = peers_.find(from_peer);
            if (it == peers_.end()) {
                return;
            }

            // Create answer
            it->second->connection->createAnswer(
                // NOLINTNEXTLINE(bugprone-exception-escape)
                [this, from_peer](bool answer_success, const SessionDescription& answer,
                                  const std::string& /*error*/) {
                    if (!answer_success) {
                        return;
                    }

                    auto it2 = peers_.find(from_peer);
                    if (it2 == peers_.end()) {
                        return;
                    }

                    // Set local description
                    it2->second->connection->setLocalDescription(
                        answer,
                        // NOLINTNEXTLINE(bugprone-exception-escape)
                        [this, from_peer, answer](bool set_success, const std::string& /*err*/) {
                            if (set_success) {
                                // Send answer to peer
                                signaling_client_->sendAnswer(from_peer, answer);
                            }
                        });
                });
        });
}

void SyncClient::signalingClientDidReceiveAnswer(SignalingClient& /*client*/,
                                                 const std::string& from_peer,
                                                 const SessionDescription& sdp) {
    auto it = peers_.find(from_peer);
    if (it == peers_.end()) {
        return;
    }

    it->second->connection->setRemoteDescription(
        sdp, [](bool /*success*/, const std::string& /*error*/) {
            // Answer set - ICE connectivity will proceed
        });
}

void SyncClient::signalingClientDidReceiveICECandidate(SignalingClient& /*client*/,
                                                       const std::string& from_peer,
                                                       const ICECandidate& candidate) {
    auto it = peers_.find(from_peer);
    if (it == peers_.end()) {
        return;
    }

    it->second->connection->addIceCandidate(candidate,
                                            [](bool /*success*/, const std::string& /*error*/) {
                                                // ICE candidate added
                                            });
}

// ============================================================================
// Peer event handlers
// ============================================================================

void SyncClient::handlePeerConnectionStateChange(const std::string& peer_id,
                                                 PeerConnectionState state) {
    if (state == PeerConnectionState::FAILED || state == PeerConnectionState::CLOSED) {
        removePeer(peer_id);
    } else if (delegate_) {
        delegate_->syncClientPeerDidChange(*this, getPeer(peer_id));
    }
}

void SyncClient::handlePeerDataChannelOpen(const std::string& peer_id,
                                           const std::string& channel_label) {
    auto it = peers_.find(peer_id);
    if (it == peers_.end()) {
        return;
    }

    // Check if operations channel is now ready
    if (channel_label == OPERATIONS_CHANNEL && it->second->isReady()) {
        // Peer is now ready for sync
        setState(SyncClientState::SYNCING);
        notifyPeerReady(peer_id);
    }
}

void SyncClient::handlePeerDataChannelClose(const std::string& peer_id,
                                            const std::string& /*channel_label*/) {
    // Data channel closed - peer is no longer connected
    auto it = peers_.find(peer_id);
    if (it != peers_.end() && delegate_ != nullptr) {
        delegate_->syncClientPeerDidChange(*this, getPeer(peer_id));
    }
}

void SyncClient::handlePeerMessage(const std::string& peer_id, const std::string& channel_label,
                                   const std::string& message) {
    if (channel_label == OPERATIONS_CHANNEL) {
        // Check for ping/pong first
        const std::string type = extractJSONString(message, "type");
        if (type == "ping") {
            handlePing(peer_id, extractJSONInt(message, "ts"));
            return;
        }
        if (type == "pong") {
            handlePong(peer_id, extractJSONInt(message, "ts"));
            return;
        }

        // Handle sync message
        handleSyncMessage(peer_id, message);
    } else if (channel_label == PRESENCE_CHANNEL) {
        // Handle presence message
        handlePresenceMessage(peer_id, message);
    }
}

void SyncClient::handlePeerICECandidate(const std::string& peer_id, const ICECandidate& candidate) {
    // Send ICE candidate to peer via signaling
    signaling_client_->sendICECandidate(peer_id, candidate);
}

// ============================================================================
// Utility functions
// ============================================================================

const char* syncClientStateToString(SyncClientState state) {
    switch (state) {
        case SyncClientState::OFFLINE:
            return "OFFLINE";
        case SyncClientState::CONNECTING:
            return "CONNECTING";
        case SyncClientState::SYNCING:
            return "SYNCING";
        case SyncClientState::ONLINE:
            return "ONLINE";
        case SyncClientState::RECONNECTING:
            return "RECONNECTING";
        default:
            return "UNKNOWN";
    }
}

}  // namespace cells::net
