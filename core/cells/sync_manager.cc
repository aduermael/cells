#include "core/cells/sync_manager.h"

#include <algorithm>
#include <sstream>

#include "core/cells/crdt.h"
#include "core/cells/model.h"
#include "core/cells/operation.h"
#include "core/cells/oplog.h"

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
            size_t start = pos;
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
            std::string opJson = json.substr(start, pos - start);
            Operation op = Operation::fromJSON(opJson);
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

// PeerSyncState implementation
PeerSyncState::PeerSyncState() : lastSyncedHLC(), isSynced(false), opCount(0) {}

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

std::vector<OutgoingMessage> SyncManager::handleMessage(const ID& peerId, const std::string& json) {
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
    if (type == "pending") {
        return handlePending(peerId, json);
    }

    // Unknown message type - no response
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

    // Create and queue the operations message
    std::string msg = makeOperationsMessage(minHLC);
    if (!msg.empty()) {
        queueBroadcast(msg);
    }
}

// Handle hello message from peer
std::vector<OutgoingMessage> SyncManager::handleHello(const ID& peerId, const std::string& json) {
    std::vector<OutgoingMessage> response;

    // Parse hello: {"type":"hello","peer_id":"...","hlc":"...","op_count":N}
    const std::string hlcStr = extractJSONString(json, "hlc");
    const int64_t opCount = extractJSONInt(json, "op_count", 0);

    HLC peerHLC = HLC::fromString(hlcStr);

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

    if (static_cast<size_t>(opCount) > localOpCount) {
        // They have more operations - request sync
        HLC localHLC = oplog->getCurrentHLC();
        response.emplace_back(peerId, makeSyncRequestMessage(localHLC));
    } else if (static_cast<size_t>(opCount) < localOpCount) {
        // We have more operations - send sync response
        response.emplace_back(peerId, makeSyncResponseMessage(peerHLC));
    } else {
        // Same op count - mark as synced
        it->second.isSynced = true;
    }

    return response;
}

// Handle sync-request from peer
std::vector<OutgoingMessage> SyncManager::handleSyncRequest(const ID& peerId,
                                                            const std::string& json) {
    std::vector<OutgoingMessage> response;

    // Parse: {"type":"sync-request","since_hlc":"..."}
    const std::string sinceHLCStr = extractJSONString(json, "since_hlc");
    HLC sinceHLC = HLC::fromString(sinceHLCStr);

    // Send operations since their HLC
    response.emplace_back(peerId, makeSyncResponseMessage(sinceHLC));

    return response;
}

// Handle sync-response from peer
std::vector<OutgoingMessage> SyncManager::handleSyncResponse(const ID& peerId,
                                                             const std::string& json) {
    // Parse: {"type":"sync-response","operations":[...],"complete":true}
    std::vector<Operation> ops = extractJSONOperations(json, "operations");

    // Apply operations to workbook
    if (!ops.empty()) {
        applyOperations(*_workbook, ops);
    }

    // Update peer sync state
    auto it = _peers.find(peerId);
    if (it != _peers.end()) {
        // Update their lastSyncedHLC to our current HLC
        it->second.lastSyncedHLC = _workbook->getOpLog()->getCurrentHLC();
        it->second.isSynced = true;
    }

    // No response needed
    return {};
}

// Handle operations batch from peer
std::vector<OutgoingMessage> SyncManager::handleOperations(const ID& peerId,
                                                           const std::string& json) {
    // Parse: {"type":"operations","batch":[...]}
    std::vector<Operation> ops = extractJSONOperations(json, "batch");

    // Apply operations to workbook
    if (!ops.empty()) {
        applyOperations(*_workbook, ops);

        // Update peer sync state with the latest HLC from received operations
        auto it = _peers.find(peerId);
        if (it != _peers.end()) {
            for (const auto& op : ops) {
                if (op.hlc > it->second.lastSyncedHLC) {
                    it->second.lastSyncedHLC = op.hlc;
                }
            }
        }
    }

    // No response needed
    return {};
}

// Handle pending operation (for live typing visibility)
std::vector<OutgoingMessage> SyncManager::handlePending(const ID& /*peerId*/,
                                                        const std::string& /*json*/) {
    // TODO: Implement pending operations for live typing
    // For now, just ignore pending messages
    // This will be implemented in Phase 3
    return {};
}

std::string SyncManager::makeHelloMessage() const {
    const OpLog* oplog = _workbook->getOpLog();
    HLC currentHLC = oplog->getCurrentHLC();
    size_t opCount = oplog->size();

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
