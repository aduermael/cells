#include "core/cells/sync_manager.h"

#include <algorithm>
#include <sstream>

#include "core/cells/crdt.h"
#include "core/cells/model.h"
#include "core/cells/operation.h"
#include "core/cells/oplog.h"
#include "core/log/include/Logger.h"

namespace cells {

namespace {

// Simple JSON string extraction (reused pattern from operation.cc)
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

// Extract integer from JSON
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

// Extract JSON array of operations
std::vector<Operation> extractJSONOperations(const std::string& json, const std::string& key) {
    std::vector<Operation> ops;

    const std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) {
        return ops;
    }
    pos += searchKey.length();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) {
        pos++;
    }

    if (pos >= json.size() || json[pos] != '[') {
        return ops;
    }

    pos++;  // Skip '['

    // Parse each operation object
    while (pos < json.size()) {
        // Skip whitespace and commas
        while (pos < json.size() &&
               (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == ',')) {
            pos++;
        }

        if (pos >= json.size() || json[pos] == ']') {
            break;
        }

        if (json[pos] == '{') {
            // Find matching closing brace
            const size_t start = pos;
            int braceCount = 1;
            pos++;
            while (pos < json.size() && braceCount > 0) {
                if (json[pos] == '{') {
                    braceCount++;
                } else if (json[pos] == '}') {
                    braceCount--;
                } else if (json[pos] == '"') {
                    // Skip string content
                    pos++;
                    while (pos < json.size() && json[pos] != '"') {
                        if (json[pos] == '\\' && pos + 1 < json.size()) {
                            pos++;
                        }
                        pos++;
                    }
                }
                pos++;
            }

            // Extract and parse the operation
            const std::string opJson = json.substr(start, pos - start);
            const Operation op = Operation::fromJSON(opJson);
            if (!op.isNull()) {
                ops.push_back(op);
            }
        } else {
            pos++;
        }
    }

    return ops;
}

}  // namespace

// OutgoingMessage implementation
OutgoingMessage::OutgoingMessage() : peerId(), json() {}

OutgoingMessage::OutgoingMessage(const ID& peer, std::string msg)
    : peerId(peer), json(std::move(msg)) {}

bool OutgoingMessage::isBroadcast() const {
    return peerId.isNull();
}

// SyncManager implementation
SyncManager::SyncManager(Workbook* workbook) : _workbook(workbook) {}

void SyncManager::addPeer(const ID& peerId) {
    if (peerId.isNull()) {
        return;
    }

    // Add peer if not already tracked
    if (_peers.find(peerId) == _peers.end()) {
        _peers[peerId] = PeerSyncState();

        // Queue hello message for this peer
        queueToPeer(peerId, makeHelloMessage());
    }
}

void SyncManager::removePeer(const ID& peerId) {
    _peers.erase(peerId);
}

std::vector<ID> SyncManager::getPeerIds() const {
    std::vector<ID> ids;
    ids.reserve(_peers.size());
    for (const auto& pair : _peers) {
        ids.push_back(pair.first);
    }
    return ids;
}

bool SyncManager::hasPeer(const ID& peerId) const {
    return _peers.find(peerId) != _peers.end();
}

size_t SyncManager::peerCount() const {
    return _peers.size();
}

const PeerSyncState* SyncManager::getPeerSyncState(const ID& peerId) const {
    auto it = _peers.find(peerId);
    if (it == _peers.end()) {
        return nullptr;
    }
    return &it->second;
}

HandleMessageResult SyncManager::handleMessage(const ID& peerId, const std::string& json) {
    // Extract message type
    const std::string type = extractJSONString(json, "type");

    if (type == "hello") {
        return handleHello(peerId, json);
    }
    if (type == "sync-request") {
        return handleSyncRequest(peerId, json);
    }
    if (type == "sync-response") {
        return handleSyncResponse(peerId, json);
    }
    if (type == "operations") {
        return handleOperations(peerId, json);
    }
    if (type == "ack") {
        return handleAck(peerId, json);
    }

    // Unknown message type - no response, no changes
    return {};
}

std::vector<OutgoingMessage> SyncManager::getOutgoingMessages() {
    std::vector<OutgoingMessage> result;
    result.swap(_outgoing);
    return result;
}

void SyncManager::queueBroadcast(const std::string& json) {
    _outgoing.emplace_back(ID(), json);
}

void SyncManager::queueToPeer(const ID& peerId, const std::string& json) {
    _outgoing.emplace_back(peerId, json);
}

void SyncManager::queueOperationsBroadcast() {
    if (_peers.empty() || _workbook == nullptr) {
        return;
    }

    // Find the minimum HLC that all peers have synced
    // We broadcast operations newer than this
    HLC minHLC;
    bool first = true;
    for (const auto& pair : _peers) {
        if (first || pair.second.lastSyncedHLC < minHLC) {
            minHLC = pair.second.lastSyncedHLC;
            first = false;
        }
    }

    const OpLog* oplog = _workbook->getOpLog();
    const size_t totalOps = oplog->size();
    const std::vector<Operation> opsToSend = oplog->getOperationsSince(minHLC);

    if (!opsToSend.empty()) {
        LOG_DEBUG(
            "[Sync] queueOperationsBroadcast: peers=%zu oplog_size=%zu min_hlc=%s ops_to_send=%zu",
            _peers.size(), totalOps, minHLC.toString().c_str(), opsToSend.size());
    }

    // Create and queue the operations message
    const std::string msg = makeOperationsMessage(minHLC);
    if (!msg.empty()) {
        queueBroadcast(msg);
        // Note: We do NOT update lastSyncedHLC here optimistically.
        // Instead, peers send an ACK when they receive operations, and we
        // update lastSyncedHLC when we receive the ACK. This ensures we don't
        // prune operations that peers haven't actually received yet.
        // If a peer doesn't ACK, we'll keep re-sending on the next broadcast.
    }
}

void SyncManager::setDebugNoPrune(bool noPrune) {
    _debugNoPrune = noPrune;
    if (noPrune) {
        LOG_DEBUG("[Sync] Debug mode: oplog pruning DISABLED");
    }
}

size_t SyncManager::pruneOpLog() {
    if (_debugNoPrune) {
        LOG_DEBUG("[Sync] pruneOpLog: skipped (debug noPrune mode)");
        return 0;
    }

    if (_workbook == nullptr) {
        return 0;
    }

    OpLog* oplog = _workbook->getOpLog();
    if (oplog == nullptr || oplog->empty()) {
        return 0;
    }

    const size_t sizeBefore = oplog->size();

    if (_peers.empty()) {
        // No peers - prune everything (use current HLC as threshold)
        const HLC currentHLC = oplog->getCurrentHLC();
        const size_t pruned = oplog->pruneOperationsBefore(currentHLC);
        if (pruned > 0) {
            LOG_DEBUG("[Sync] pruneOpLog: no peers, pruned %zu ops (was %zu)", pruned, sizeBefore);
        }
        return pruned;
    }

    // Find the minimum HLC that all peers have synced
    HLC minHLC;
    bool first = true;
    for (const auto& pair : _peers) {
        if (first || pair.second.lastSyncedHLC < minHLC) {
            minHLC = pair.second.lastSyncedHLC;
            first = false;
        }
    }

    // If minHLC is zero (no synced operations), don't prune anything
    if (minHLC.wall_time == 0 && minHLC.logical == 0) {
        LOG_DEBUG("[Sync] pruneOpLog: min_hlc is zero, not pruning (oplog_size=%zu)", sizeBefore);
        return 0;
    }

    const size_t pruned = oplog->pruneOperationsBefore(minHLC);
    if (pruned > 0) {
        LOG_DEBUG("[Sync] pruneOpLog: pruned %zu ops (was %zu, now %zu) threshold=%s", pruned,
                  sizeBefore, oplog->size(), minHLC.toString().c_str());
    }
    return pruned;
}

// Handle hello message from peer
// No data modification - just peer state management
HandleMessageResult SyncManager::handleHello(const ID& peerId, const std::string& json) {
    std::vector<OutgoingMessage> response;

    // Parse hello: {"type":"hello","peer_id":"...","hlc":"...","op_count":N}
    const std::string hlcStr = extractJSONString(json, "hlc");
    const int64_t opCount = extractJSONInt(json, "op_count", 0);

    const HLC peerHLC = HLC::fromString(hlcStr);

    // Update peer state
    auto it = _peers.find(peerId);
    if (it == _peers.end()) {
        // Auto-add peer if not tracked
        _peers[peerId] = PeerSyncState();
        it = _peers.find(peerId);
    }

    it->second.lastSyncedHLC = peerHLC;
    it->second.opCount = static_cast<size_t>(opCount);
    it->second.isSynced = false;

    // Determine if we need to request operations from them
    const OpLog* oplog = _workbook->getOpLog();
    const size_t localOpCount = oplog->size();

    LOG_DEBUG("[Sync] handleHello: peer=%s peer_hlc=%s peer_ops=%lld local_ops=%zu",
              peerId.toString().c_str(), peerHLC.toString().c_str(), opCount, localOpCount);

    if (static_cast<size_t>(opCount) > localOpCount) {
        // They have more operations - request sync
        const HLC localHLC = oplog->getCurrentHLC();
        LOG_DEBUG("[Sync] handleHello: peer has more ops, requesting sync from %s",
                  localHLC.toString().c_str());
        response.emplace_back(peerId, makeSyncRequestMessage(localHLC));
    } else if (static_cast<size_t>(opCount) < localOpCount) {
        // We have more operations - send sync response
        LOG_DEBUG("[Sync] handleHello: we have more ops, sending sync response since %s",
                  peerHLC.toString().c_str());
        response.emplace_back(peerId, makeSyncResponseMessage(peerHLC));
    } else {
        // Same op count - mark as synced
        LOG_DEBUG("[Sync] handleHello: same op count, marking peer %s as synced",
                  peerId.toString().c_str());
        it->second.isSynced = true;
    }

    // hello doesn't modify data, just peer state
    return HandleMessageResult(std::move(response), false);
}

// Handle sync-request from peer
// No data modification - just sends our operations to peer
HandleMessageResult SyncManager::handleSyncRequest(const ID& peerId, const std::string& json) {
    std::vector<OutgoingMessage> response;

    // Parse: {"type":"sync-request","since_hlc":"..."}
    const std::string sinceHLCStr = extractJSONString(json, "since_hlc");
    const HLC sinceHLC = HLC::fromString(sinceHLCStr);

    // Send operations since their HLC
    response.emplace_back(peerId, makeSyncResponseMessage(sinceHLC));

    // sync-request doesn't modify our data
    return HandleMessageResult(std::move(response), false);
}

// Handle sync-response from peer
// DATA MODIFIED if operations applied successfully
HandleMessageResult SyncManager::handleSyncResponse(const ID& peerId, const std::string& json) {
    // Parse: {"type":"sync-response","operations":[...],"complete":true}
    std::vector<Operation> ops = extractJSONOperations(json, "operations");

    bool dataModified = false;

    // Apply operations to workbook
    if (!ops.empty()) {
        // Filter out operations we already have (deduplication)
        const OpLog* oplog = _workbook->getOpLog();
        std::vector<Operation> newOps;
        newOps.reserve(ops.size());
        size_t duplicates = 0;

        for (const auto& op : ops) {
            if (!oplog->hasOperation(op.hlc)) {
                newOps.push_back(op);
            } else {
                duplicates++;
            }
        }

        if (duplicates > 0) {
            LOG_DEBUG("[Sync] handleSyncResponse: from=%s skipped %zu duplicate ops",
                      peerId.toString().c_str(), duplicates);
        }

        if (!newOps.empty()) {
            const size_t applied = applyOperations(*_workbook, newOps);
            dataModified = (applied > 0);
            LOG_DEBUG("[Sync] handleSyncResponse: from=%s received=%zu new=%zu applied=%zu",
                      peerId.toString().c_str(), ops.size(), newOps.size(), applied);
        }
    }

    // Update peer sync state
    auto it = _peers.find(peerId);
    if (it != _peers.end()) {
        const HLC oldHLC = it->second.lastSyncedHLC;
        // Update their lastSyncedHLC to our current HLC
        it->second.lastSyncedHLC = _workbook->getOpLog()->getCurrentHLC();
        it->second.isSynced = true;
        LOG_DEBUG("[Sync] handleSyncResponse: peer %s marked synced, HLC %s -> %s",
                  peerId.toString().c_str(), oldHLC.toString().c_str(),
                  it->second.lastSyncedHLC.toString().c_str());
    }

    // Note: Do NOT prune oplog here. When we receive a sync-response, we've just
    // added operations to our oplog. Pruning immediately would remove them because
    // lastSyncedHLC equals getCurrentHLC() (the max HLC of received ops).
    // Pruning should only happen when all peers have confirmed receipt via ACK.

    // Return operations for delegate notification
    return {{}, std::move(ops), dataModified};
}

// Handle operations batch from peer
// DATA MODIFIED if operations applied successfully
HandleMessageResult SyncManager::handleOperations(const ID& peerId, const std::string& json) {
    // Parse: {"type":"operations","batch":[...]}
    std::vector<Operation> ops = extractJSONOperations(json, "batch");

    bool dataModified = false;
    std::vector<OutgoingMessage> response;

    // Apply operations to workbook
    if (!ops.empty()) {
        // Filter out operations we already have (deduplication)
        // This is a safety check - normally we shouldn't receive duplicates
        const OpLog* oplog = _workbook->getOpLog();
        std::vector<Operation> newOps;
        newOps.reserve(ops.size());
        size_t duplicates = 0;

        // Track max HLC for ACK
        HLC maxHLC;
        for (const auto& op : ops) {
            if (op.hlc > maxHLC) {
                maxHLC = op.hlc;
            }
            if (!oplog->hasOperation(op.hlc)) {
                newOps.push_back(op);
            } else {
                duplicates++;
            }
        }

        if (duplicates > 0) {
            LOG_DEBUG("[Sync] handleOperations: from=%s skipped %zu duplicate ops",
                      peerId.toString().c_str(), duplicates);
        }

        if (!newOps.empty()) {
            const size_t applied = applyOperations(*_workbook, newOps);
            dataModified = (applied > 0);

            LOG_DEBUG("[Sync] handleOperations: from=%s received=%zu new=%zu applied=%zu",
                      peerId.toString().c_str(), ops.size(), newOps.size(), applied);
        }

        // Send ACK with the max HLC we received
        // This tells the sender we have all ops up to this HLC
        std::ostringstream ack;
        ack << "{\"type\":\"ack\",\"hlc\":\"" << maxHLC.toString() << "\"}";
        response.emplace_back(peerId, ack.str());
        LOG_DEBUG("[Sync] handleOperations: sending ACK to %s for HLC %s",
                  peerId.toString().c_str(), maxHLC.toString().c_str());
    }

    // Return operations for delegate notification
    return {std::move(response), std::move(ops), dataModified};
}

// Handle ACK from peer - confirms they received our operations
// No data modification - just updates peer sync state
HandleMessageResult SyncManager::handleAck(const ID& peerId, const std::string& json) {
    // Parse: {"type":"ack","hlc":"..."}
    const std::string hlcStr = extractJSONString(json, "hlc");
    const HLC ackedHLC = HLC::fromString(hlcStr);

    // Update peer's lastSyncedHLC - they've confirmed receipt up to this HLC
    auto it = _peers.find(peerId);
    if (it != _peers.end()) {
        if (ackedHLC > it->second.lastSyncedHLC) {
            LOG_DEBUG("[Sync] handleAck: peer %s ACKed HLC %s (was %s)", peerId.toString().c_str(),
                      ackedHLC.toString().c_str(), it->second.lastSyncedHLC.toString().c_str());
            it->second.lastSyncedHLC = ackedHLC;

            // Now that peer has confirmed receipt, try to prune old operations
            pruneOpLog();
        }
    }

    // ACK doesn't modify data
    return {};
}

std::string SyncManager::makeHelloMessage() const {
    const OpLog* oplog = _workbook->getOpLog();
    const HLC currentHLC = oplog->getCurrentHLC();
    const size_t opCount = oplog->size();

    std::ostringstream oss;
    oss << "{";
    oss << "\"type\":\"hello\",";
    oss << "\"peer_id\":\"" << _workbook->getNodeId().toString() << "\",";
    oss << "\"hlc\":\"" << currentHLC.toString() << "\",";
    oss << "\"op_count\":" << opCount;
    oss << "}";
    return oss.str();
}

std::string SyncManager::makeSyncRequestMessage(const HLC& sinceHLC) const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"type\":\"sync-request\",";
    oss << "\"since_hlc\":\"" << sinceHLC.toString() << "\"";
    oss << "}";
    return oss.str();
}

std::string SyncManager::makeSyncResponseMessage(const HLC& sinceHLC) const {
    const OpLog* oplog = _workbook->getOpLog();
    std::vector<Operation> ops = oplog->getOperationsSince(sinceHLC);

    std::ostringstream oss;
    oss << "{";
    oss << "\"type\":\"sync-response\",";
    oss << "\"operations\":[";

    for (size_t i = 0; i < ops.size(); i++) {
        if (i > 0) {
            oss << ",";
        }
        oss << ops[i].toJSON();
    }

    oss << "],";
    oss << "\"complete\":true";
    oss << "}";
    return oss.str();
}

std::string SyncManager::makeOperationsMessage(const HLC& sinceHLC) const {
    const OpLog* oplog = _workbook->getOpLog();
    std::vector<Operation> ops = oplog->getOperationsSince(sinceHLC);

    if (ops.empty()) {
        return "";  // Nothing to send
    }

    std::ostringstream oss;
    oss << "{";
    oss << "\"type\":\"operations\",";
    oss << "\"batch\":[";

    for (size_t i = 0; i < ops.size(); i++) {
        if (i > 0) {
            oss << ",";
        }
        oss << ops[i].toJSON();
    }

    oss << "]";
    oss << "}";
    return oss.str();
}

}  // namespace cells
