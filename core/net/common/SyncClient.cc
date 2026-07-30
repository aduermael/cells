// SyncClient implementation - orchestrates P2P synchronization

#include "core/net/include/SyncClient.h"

#include <algorithm>
#include <random>
#include <sstream>

#include "core/cells/crdt.h"
#include "core/cells/hlc.h"
#include "core/cells/model.h"
#include "core/cells/sync_manager.h"
#include "core/log/include/Logger.h"

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
        // Channel may already be OPEN before we set the delegate (common as answerer).
        // Without this, notifyPeerReady never runs → SyncManager has no peers →
        // broadcastOperations is a silent no-op (inbound still works).
        if (operations_channel && operations_channel->isOpen() && sync_client) {
            sync_client->handlePeerDataChannelOpen(id, label);
        }
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

    LOG_INFO("[Sync] Starting sync: room=%s peer=%s", room_id_.c_str(), peer_id_.c_str());

    // Set node ID on workbook for HLC generation before any bootstrap ops
    workbook_->setNodeId(cells::ID(peer_id_));

    // Shared join policy (CLI + WASM): empty shell → publish nothing and pull;
    // local content → bootstrap; already collab → leave state alone.
    {
        const cells::PrepareForSyncResult prep = cells::prepareWorkbookForSync(*workbook_);
        last_bootstrapped_ops_ = prep.bootstrappedOps;
    }

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
    expect_remote_peers_ = false;
    setState(SyncClientState::OFFLINE);
}

void SyncClient::broadcastOperations() {
    if (!sync_manager_) {
        return;
    }

    // Ensure every RTC peer with an open operations channel is in SyncManager.
    // Inbound ops can apply without addPeer; outbound broadcast requires peers.
    for (const auto& pair : peers_) {
        if (pair.second && pair.second->isReady()) {
            // addPeer is a no-op if already tracked (no double hello)
            if (!sync_manager_->hasPeer(cells::ID(pair.first))) {
                LOG_INFO("[Sync] broadcastOperations: late-tracking peer %s", pair.first.c_str());
                notifyPeerReady(pair.first);
            }
        }
    }

    // Delta broadcast (ops newer than peer watermarks) plus a full oplog push
    // so concurrent/local ops made before a peer joined are never stranded.
    // Receivers dedupe by HLC.
    sync_manager_->queueOperationsBroadcast();
    sync_manager_->queueFullSyncToAllPeers();
    processOutgoing();
}

void SyncClient::processOutgoing() {
    if (!sync_manager_) {
        return;
    }

    auto messages = sync_manager_->getOutgoingMessages();
    for (const auto& msg : messages) {
        if (msg.json.empty()) {
            continue;
        }
        if (msg.isBroadcast()) {
            broadcastToPeers(msg.json);
        } else {
            sendToPeer(msg.peerId.toString(), msg.json);
        }
        stats_.messages_sent++;
        // Count operation batches as operations_sent for agent observability
        if (msg.json.find("\"type\":\"operations\"") != std::string::npos ||
            msg.json.find("\"type\":\"sync-response\"") != std::string::npos) {
            stats_.operations_sent++;
        }
    }

    // Re-evaluate ONLINE/SYNCING even when no messages (stuck SYNCING fix:
    // previously updateSyncState only ran on peer messages/disconnect).
    updateSyncState();
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

bool SyncClient::shouldInitiateTo(const std::string& remote_peer_id) const {
    // Perfect negotiation (impolite = higher id always offers). Same rule on
    // every client so exactly one side creates the offer.
    return peer_id_ > remote_peer_id;
}

ConnectedPeer* SyncClient::createPeerConnection(const std::string& peer_id, bool we_initiate) {
    auto existing = peers_.find(peer_id);
    if (existing != peers_.end()) {
        if (we_initiate) {
            LOG_INFO("[Sync] Peer %s already connecting (skip re-init)", peer_id.c_str());
            return existing->second.get();
        }
        // Receiving an offer while we already initiated → glare
        if (existing->second->we_initiated) {
            if (shouldInitiateTo(peer_id)) {
                // We are impolite: ignore their offer, keep ours
                LOG_INFO("[Sync] Glare: ignoring offer from %s (we are impolite)", peer_id.c_str());
                return nullptr;
            }
            // We are polite: roll back our offer and accept theirs
            LOG_INFO("[Sync] Glare: rolling back our offer to %s (we are polite)", peer_id.c_str());
            removePeer(peer_id);
        } else {
            return existing->second.get();
        }
    }

    LOG_INFO("[Sync] Connecting to peer: %s (initiator=%s)", peer_id.c_str(),
             we_initiate ? "true" : "false");

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

void SyncClient::initiateConnectionToPeer(const std::string& peer_id) {
    if (peer_id.empty() || peer_id == peer_id_) {
        return;
    }
    ConnectedPeer* peer = createPeerConnection(peer_id, true);
    if (!peer || !peer->connection) {
        return;
    }
    if (!peer->we_initiated) {
        return;  // polite rollback path may have switched roles
    }

    peer->connection->createOffer(
        // NOLINTNEXTLINE(bugprone-exception-escape)
        [this, peer_id](bool success, const SessionDescription& sdp, const std::string& /*error*/) {
            if (!success) {
                LOG_INFO("[Sync] createOffer failed for %s", peer_id.c_str());
                return;
            }
            auto it = peers_.find(peer_id);
            if (it == peers_.end() || !it->second->connection) {
                return;
            }
            it->second->connection->setLocalDescription(
                sdp,
                // NOLINTNEXTLINE(bugprone-exception-escape)
                [this, peer_id, sdp](bool set_success, const std::string& /*error*/) {
                    if (set_success) {
                        LOG_INFO("[Sync] Sending offer to %s", peer_id.c_str());
                        signaling_client_->sendOffer(peer_id, sdp);
                    }
                });
        });
}

void SyncClient::removePeer(const std::string& peer_id) {
    auto it = peers_.find(peer_id);
    if (it == peers_.end()) {
        // Already removed (reentrancy guard)
        return;
    }

    LOG_INFO("[Sync] Peer disconnected: %s", peer_id.c_str());

    // Take ownership of the peer before erasing to prevent reentrancy issues
    // (closing the connection triggers callbacks which might call removePeer again)
    auto peer = std::move(it->second);
    peers_.erase(it);
    if (peers_.empty()) {
        // Room may be empty now; allow ONLINE alone again
        expect_remote_peers_ = false;
    }

    // Clear data channel delegates to prevent callbacks on destroyed objects
    if (peer->operations_channel) {
        peer->operations_channel->setDelegate(nullptr);
    }
    if (peer->presence_channel) {
        peer->presence_channel->setDelegate(nullptr);
    }

    // Now safe to close - if callbacks fire, the peer is already removed from map
    if (peer->connection) {
        peer->connection->setDelegate(nullptr);  // Prevent further callbacks
        peer->connection->close();
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

    // Track peer for outbound broadcast even if we never got dataChannelDidOpen
    // (answerer race) or never exchanged hello before the first ops batch.
    if (!sync_manager_->hasPeer(cells::ID(peer_id))) {
        sync_manager_->addPeer(cells::ID(peer_id));
    }

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

    // Flush anything handleMessage queued on _outgoing (not only result.messages)
    processOutgoing();

    // Notify delegate about received operations
    if (!result.receivedOperations.empty() && delegate_) {
        stats_.operations_received += result.receivedOperations.size();
        delegate_->syncClientDidReceiveOperations(*this, peer_id, result.receivedOperations);
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
        LOG_INFO("[Sync] sendToPeer: no RTC peer %s (drop %zu bytes)", peer_id.c_str(),
                 message.size());
        return;
    }

    if (it->second->operations_channel && it->second->operations_channel->isOpen()) {
        if (!it->second->operations_channel->send(message)) {
            LOG_INFO("[Sync] sendToPeer: send FAILED to %s (%zu bytes)", peer_id.c_str(),
                     message.size());
        }
    } else {
        LOG_INFO("[Sync] sendToPeer: channel not open for %s (drop %zu bytes)", peer_id.c_str(),
                 message.size());
    }
}

void SyncClient::broadcastToPeers(const std::string& message) {
    size_t sent = 0;
    for (auto& pair : peers_) {
        if (pair.second->operations_channel && pair.second->operations_channel->isOpen()) {
            if (pair.second->operations_channel->send(message)) {
                sent++;
            } else {
                LOG_INFO("[Sync] broadcastToPeers: send FAILED to %s", pair.first.c_str());
            }
        }
    }
    if (sent == 0 && !peers_.empty()) {
        LOG_INFO("[Sync] broadcastToPeers: 0/%zu peers accepted message (%zu bytes)", peers_.size(),
                 message.size());
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

void SyncClient::setCursor(int32_t col, int32_t row) {
    if (presence_manager_) {
        presence_manager_->setCursor(col, row);
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

void SyncClient::setEditing(int32_t col, int32_t row, const std::string& text) {
    if (presence_manager_) {
        presence_manager_->setEditing(col, row, text);
    }
}

void SyncClient::clearEditing() {
    if (presence_manager_) {
        presence_manager_->clearEditing();
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
        LOG_INFO("[Sync] State: %s -> %s", syncClientStateToString(state_),
                 syncClientStateToString(new_state));
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

    // Check if we have any ready peers (data channel open)
    const size_t ready_count = getPeerCount();
    const size_t connecting = peers_.size();

    if (ready_count == 0) {
        // No ready data channels. Stay SYNCING while:
        //  - WebRTC peers are mid-handshake (connecting > 0), or
        //  - we joined a room that already had peers (expect_remote_peers_)
        //    and are still waiting for their offer (polite path).
        // Going ONLINE alone here was minting a parallel Sheet1 on the CLI
        // that never shared IDs with the browser document.
        if (state_ == SyncClientState::SYNCING && connecting == 0 && !expect_remote_peers_) {
            setState(SyncClientState::ONLINE);
        }
        return;
    }

    // Check if all ready peers are CRDT-synced
    bool all_synced = true;
    size_t tracked = 0;
    for (const auto& peer_id : sync_manager_->getPeerIds()) {
        tracked++;
        const auto* peer_state = sync_manager_->getPeerSyncState(peer_id);
        if (peer_state && !peer_state->isSynced) {
            all_synced = false;
            break;
        }
    }

    // Also require every RTC-ready peer to be in SyncManager
    if (tracked < ready_count) {
        all_synced = false;
    }

    if (all_synced && state_ == SyncClientState::SYNCING) {
        LOG_INFO("[Sync] All %zu peer(s) CRDT-synced → ONLINE", ready_count);
        expect_remote_peers_ = false;
        setState(SyncClientState::ONLINE);
    }
}

void SyncClient::notifyPeerReady(const std::string& peer_id) {
    if (!sync_manager_) {
        return;
    }

    // Add peer to SyncManager (queues hello message → bidirectional full sync)
    sync_manager_->addPeer(cells::ID(peer_id));

    // Flush hello / sync-request / sync-response immediately
    processOutgoing();

    // Also push any local ops that predate the channel (belt and suspenders)
    sync_manager_->queueFullSyncToAllPeers();
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
        LOG_INFO("[Sync] Joined room alone → ONLINE");
        expect_remote_peers_ = false;
        setState(SyncClientState::ONLINE);
        return;
    }

    // Join with existing peers: do NOT wait for them to notice us. Previously
    // only "existing peers initiate via peer-joined" — if the browser missed
    // that event, the joiner sat in SYNCING forever. Perfect negotiation:
    // higher peer id offers to each existing peer.
    //
    // expect_remote_peers_: polite joiners have peers_.empty() until the first
    // offer arrives — without this flag updateSyncState would go ONLINE alone
    // and callers (CLI session) would mint a second empty Sheet1.
    LOG_INFO("[Sync] Joined room with %zu existing peer(s) → SYNCING", existing_peers.size());
    expect_remote_peers_ = true;
    setState(SyncClientState::SYNCING);
    for (const auto& other : existing_peers) {
        if (shouldInitiateTo(other)) {
            LOG_INFO("[Sync] Joiner initiating to existing peer %s", other.c_str());
            initiateConnectionToPeer(other);
        } else {
            LOG_INFO("[Sync] Waiting for offer from existing peer %s", other.c_str());
        }
    }
}

void SyncClient::signalingClientPeerDidJoin(SignalingClient& /*client*/,
                                            const std::string& peer_id) {
    // Perfect negotiation: only the higher id creates the offer
    if (!shouldInitiateTo(peer_id)) {
        LOG_INFO("[Sync] peer-joined %s — waiting for their offer (we are polite)",
                 peer_id.c_str());
        return;
    }
    LOG_INFO("[Sync] peer-joined %s — initiating offer", peer_id.c_str());
    initiateConnectionToPeer(peer_id);
}

void SyncClient::signalingClientPeerDidLeave(SignalingClient& /*client*/,
                                             const std::string& peer_id) {
    removePeer(peer_id);
}

void SyncClient::signalingClientDidReceiveOffer(SignalingClient& /*client*/,
                                                const std::string& from_peer,
                                                const SessionDescription& sdp) {
    LOG_INFO("[Sync] Received offer from %s", from_peer.c_str());
    // Accept peer connection (createPeerConnection handles glare)
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
