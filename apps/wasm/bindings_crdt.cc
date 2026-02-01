// =============================================================================
// WASM Bindings - CRDT and Sync Operations
// =============================================================================
//
// Implementation of CRDT collaboration CellsEngine methods:
// - setNodeId/getNodeId/getCurrentHLC: Node identity and HLC
// - getOperationsSince/applyRemoteOperation(s): OpLog operations
// - initSyncManager/addPeer/removePeer: SyncManager peer management
// - handlePeerMessage/getOutgoingMessages: Message handling
// - startCollaboration/setCollabMode: Collaboration mode
// - enableSync/disableSync/getSyncState: C++ SyncClient
// - setSyncCursor/setSyncSelection/etc: Presence tracking
// - SyncClientDelegate implementation: Callbacks from SyncClient
//
// =============================================================================

#include "apps/wasm/bindings.h"

#include <sstream>

#include "core/cells/crdt.h"
#include "core/cells/hlc.h"
#include "core/cells/operation.h"
#include "core/cells/oplog.h"
#include "core/log/include/Logger.h"

namespace cells::wasm {

// ============================================================================
// CRDT collaboration methods
// ============================================================================

std::string CellsEngine::setNodeId(const std::string& nodeIdStr) {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    if (nodeIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid node ID length\"}";
    }

    ID nodeId(nodeIdStr);
    _workbook->setNodeId(nodeId);

    return "{\"success\":true}";
}

std::string CellsEngine::getNodeId() {
    if (!_workbook) {
        return "";
    }

    const ID& nodeId = _workbook->getNodeId();
    if (nodeId.isNull()) {
        return "";
    }

    return nodeId.toString();
}

std::string CellsEngine::getCurrentHLC() {
    if (!_workbook) {
        return "";
    }

    HLC hlc = _workbook->getCurrentHLC();
    return hlc.toString();
}

std::string CellsEngine::getOperationsSince(const std::string& sinceHLCStr) {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    OpLog* oplog = _workbook->getOpLog();
    if (!oplog) {
        return "{\"error\":\"No oplog\"}";
    }

    HLC sinceHLC;
    if (!sinceHLCStr.empty()) {
        sinceHLC = HLC::fromString(sinceHLCStr);
    }

    std::vector<Operation> ops = oplog->getOperationsSince(sinceHLC);

    std::ostringstream json;
    json << "{\"operations\":[";

    bool first = true;
    for (const auto& op : ops) {
        if (!first) {
            json << ",";
        }
        first = false;
        json << op.toJSON();
    }

    json << "]}";
    return json.str();
}

std::string CellsEngine::applyRemoteOperation(const std::string& opJson) {
    if (!_workbook) {
        return "{\"error\":\"No workbook\",\"result\":\"error\"}";
    }

    Operation op = Operation::fromJSON(opJson);
    if (op.isNull()) {
        return "{\"error\":\"Invalid operation JSON\",\"result\":\"error\"}";
    }

    ApplyResult result = applyOperation(*_workbook, op);

    // Determine notification type based on operation type
    // Axis and sheet operations need STRUCTURE_CHANGED so viewport gets refreshed
    auto getNotificationType = [](OpType type) -> ChangeType {
        switch (type) {
            case OpType::COL_SET:
            case OpType::COL_DELETE:
            case OpType::ROW_SET:
            case OpType::ROW_DELETE:
            case OpType::SHEET_SET:
            case OpType::SHEET_DELETE:
                return ChangeType::STRUCTURE_CHANGED;
            default:
                return ChangeType::CELL_CHANGED;
        }
    };

    std::string resultStr;
    switch (result) {
        case ApplyResult::SUCCESS:
            resultStr = "success";
            rebuildViewportIndex();
            notifyListeners(getNotificationType(op.type));
            break;
        case ApplyResult::ALREADY_APPLIED:
            resultStr = "already_applied";
            break;
        case ApplyResult::SUPERSEDED:
            resultStr = "superseded";
            break;
        case ApplyResult::INVALID_TARGET:
            resultStr = "invalid_target";
            break;
        case ApplyResult::INVALID_PAYLOAD:
            resultStr = "invalid_payload";
            break;
        case ApplyResult::RESURRECTED:
            resultStr = "resurrected";
            rebuildViewportIndex();
            notifyListeners(getNotificationType(op.type));
            break;
    }

    std::ostringstream json;
    json << "{\"result\":\"" << resultStr << "\"}";
    return json.str();
}

std::string CellsEngine::applyRemoteOperations(const std::string& opsJson) {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    std::vector<Operation> ops;

    size_t arrStart = opsJson.find("\"operations\":[");
    if (arrStart == std::string::npos) {
        return "{\"error\":\"Invalid format, expected operations array\"}";
    }
    arrStart += 14;

    size_t arrEnd = opsJson.rfind(']');
    if (arrEnd == std::string::npos || arrEnd <= arrStart) {
        return "{\"error\":\"Invalid format, malformed operations array\"}";
    }

    std::string arrContent = opsJson.substr(arrStart, arrEnd - arrStart);

    size_t pos = 0;
    while (pos < arrContent.size()) {
        while (pos < arrContent.size() &&
               (arrContent[pos] == ' ' || arrContent[pos] == ',' || arrContent[pos] == '\n' ||
                arrContent[pos] == '\r' || arrContent[pos] == '\t')) {
            pos++;
        }
        if (pos >= arrContent.size()) {
            break;
        }

        if (arrContent[pos] != '{') {
            break;
        }

        size_t objStart = pos;
        int braceCount = 1;
        pos++;
        while (pos < arrContent.size() && braceCount > 0) {
            if (arrContent[pos] == '{') {
                braceCount++;
            } else if (arrContent[pos] == '}') {
                braceCount--;
            } else if (arrContent[pos] == '"') {
                pos++;
                while (pos < arrContent.size() && arrContent[pos] != '"') {
                    if (arrContent[pos] == '\\' && pos + 1 < arrContent.size()) {
                        pos++;
                    }
                    pos++;
                }
            }
            pos++;
        }

        if (braceCount == 0) {
            std::string opJson = arrContent.substr(objStart, pos - objStart);
            Operation op = Operation::fromJSON(opJson);
            if (!op.isNull()) {
                ops.push_back(op);
            }
        }
    }

    // Check if any operations affect structure (axes, sheets)
    bool hasStructureOps = false;
    for (const auto& op : ops) {
        switch (op.type) {
            case OpType::COL_SET:
            case OpType::COL_DELETE:
            case OpType::ROW_SET:
            case OpType::ROW_DELETE:
            case OpType::SHEET_SET:
            case OpType::SHEET_DELETE:
                hasStructureOps = true;
                break;
            default:
                break;
        }
        if (hasStructureOps) break;
    }

    size_t applied = applyOperations(*_workbook, ops);

    if (applied > 0) {
        rebuildViewportIndex();
        notifyListeners(hasStructureOps ? ChangeType::STRUCTURE_CHANGED : ChangeType::CELL_CHANGED);
    }

    std::ostringstream json;
    json << "{\"applied\":" << applied << ",\"total\":" << ops.size() << "}";
    return json.str();
}

int CellsEngine::getOpLogSize() {
    if (!_workbook) {
        return 0;
    }

    OpLog* oplog = _workbook->getOpLog();
    if (!oplog) {
        return 0;
    }

    return static_cast<int>(oplog->size());
}

bool CellsEngine::hasOperation(const std::string& hlcStr) {
    if (!_workbook) {
        return false;
    }

    OpLog* oplog = _workbook->getOpLog();
    if (!oplog) {
        return false;
    }

    HLC hlc = HLC::fromString(hlcStr);
    return oplog->hasOperation(hlc);
}

// ============================================================================
// SyncManager methods
// ============================================================================

std::string CellsEngine::initSyncManager() {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    _syncManager = std::make_unique<SyncManager>(_workbook.get());
    return "{\"success\":true}";
}

std::string CellsEngine::addPeer(const std::string& peerIdStr) {
    if (!_syncManager) {
        return "{\"error\":\"SyncManager not initialized\"}";
    }

    if (peerIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid peer ID length\"}";
    }

    ID peerId(peerIdStr);
    _syncManager->addPeer(peerId);
    return "{\"success\":true}";
}

std::string CellsEngine::removePeer(const std::string& peerIdStr) {
    if (!_syncManager) {
        return "{\"error\":\"SyncManager not initialized\"}";
    }

    if (peerIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid peer ID length\"}";
    }

    ID peerId(peerIdStr);
    _syncManager->removePeer(peerId);
    return "{\"success\":true}";
}

std::string CellsEngine::getPeerIds() {
    if (!_syncManager) {
        return "{\"error\":\"SyncManager not initialized\"}";
    }

    std::vector<ID> peers = _syncManager->getPeerIds();

    std::ostringstream json;
    json << "{\"peers\":[";
    for (size_t i = 0; i < peers.size(); i++) {
        if (i > 0) {
            json << ",";
        }
        json << "\"" << peers[i].toString() << "\"";
    }
    json << "]}";
    return json.str();
}

int CellsEngine::getPeerCount() {
    if (!_syncManager) {
        return 0;
    }
    return static_cast<int>(_syncManager->peerCount());
}

std::string CellsEngine::handlePeerMessage(const std::string& peerIdStr,
                                            const std::string& messageJson) {
    if (!_syncManager) {
        return "{\"error\":\"SyncManager not initialized\"}";
    }

    if (peerIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid peer ID length\"}";
    }

    ID peerId(peerIdStr);
    HandleMessageResult result = _syncManager->handleMessage(peerId, messageJson);

    if (result.dataModified) {
        rebuildViewportIndex();
        notifyListeners(ChangeType::CELL_CHANGED);
    }

    std::ostringstream json;
    json << "{\"messages\":[";
    for (size_t i = 0; i < result.messages.size(); i++) {
        if (i > 0) {
            json << ",";
        }
        json << "{";
        if (result.messages[i].isBroadcast()) {
            json << "\"peerId\":null,";
        } else {
            json << "\"peerId\":\"" << result.messages[i].peerId.toString() << "\",";
        }
        json << "\"json\":"
             << "\"" << jsonEscape(result.messages[i].json) << "\"";
        json << "}";
    }
    json << "]}";
    return json.str();
}

std::string CellsEngine::getOutgoingMessages() {
    if (!_syncManager) {
        return "{\"error\":\"SyncManager not initialized\"}";
    }

    std::vector<OutgoingMessage> messages = _syncManager->getOutgoingMessages();

    std::ostringstream json;
    json << "{\"messages\":[";
    for (size_t i = 0; i < messages.size(); i++) {
        if (i > 0) {
            json << ",";
        }
        json << "{";
        if (messages[i].isBroadcast()) {
            json << "\"peerId\":null,";
        } else {
            json << "\"peerId\":\"" << messages[i].peerId.toString() << "\",";
        }
        json << "\"json\":"
             << "\"" << jsonEscape(messages[i].json) << "\"";
        json << "}";
    }
    json << "]}";
    return json.str();
}

std::string CellsEngine::queueOperationsBroadcast() {
    if (!_syncManager) {
        return "{\"error\":\"SyncManager not initialized\"}";
    }

    _syncManager->queueOperationsBroadcast();
    return "{\"success\":true}";
}

void CellsEngine::setDebugNoPrune(bool noPrune) {
    if (_syncManager) {
        _syncManager->setDebugNoPrune(noPrune);
    }
}

// ============================================================================
// Collaboration mode methods
// ============================================================================

std::string CellsEngine::getCollabMode() {
    if (!_workbook) {
        return "offline";
    }
    return _workbook->isCollaborating() ? "collaborating" : "offline";
}

bool CellsEngine::isCollaborating() {
    if (!_workbook) {
        return false;
    }
    return _workbook->isCollaborating();
}

std::string CellsEngine::startCollaboration() {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    if (_workbook->isCollaborating()) {
        return "{\"success\":true,\"mode\":\"collaborating\",\"bootstrapped\":0}";
    }

    _workbook->startCollaboration();
    size_t opCount = bootstrapOpLog(*_workbook);

    std::ostringstream json;
    json << "{\"success\":true,\"mode\":\"collaborating\",\"bootstrapped\":" << opCount << "}";
    return json.str();
}

std::string CellsEngine::setCollabMode(const std::string& mode) {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    if (mode == "offline") {
        _workbook->setCollabMode(cells::CollabMode::OFFLINE);
    } else if (mode == "collaborating") {
        _workbook->setCollabMode(cells::CollabMode::COLLABORATING);
    } else {
        return "{\"error\":\"Invalid mode. Use 'offline' or 'collaborating'\"}";
    }

    return "{\"success\":true,\"mode\":\"" + mode + "\"}";
}

// ============================================================================
// C++ SyncClient methods
// ============================================================================

std::string CellsEngine::enableSync(const std::string& url, const std::string& roomId) {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    if (_syncClient) {
        _syncClient->stopSync();
        _syncClient.reset();
    }

    size_t bootstrappedOps = 0;
    if (!_workbook->isCollaborating()) {
        _workbook->startCollaboration();
        bootstrappedOps = bootstrapOpLog(*_workbook);
    }

    cells::net::SyncClientConfig config;
    config.signaling_url = url;

    _syncClient = std::make_unique<cells::net::SyncClient>(_workbook.get(), config);
    _syncClient->setDelegate(this);

    _syncClient->startSync(roomId, "");

    std::ostringstream json;
    json << "{\"success\":true,\"peerId\":\"" << _syncClient->getPeerId()
         << "\",\"bootstrapped\":" << bootstrappedOps << "}";
    return json.str();
}

void CellsEngine::disableSync() {
    if (_syncClient) {
        _syncClient->stopSync();
        _syncClient.reset();
    }
}

std::string CellsEngine::getSyncState() {
    if (!_syncClient) {
        return "{\"state\":\"offline\",\"peerId\":\"\",\"roomId\":\"\",\"peerCount\":0,"
               "\"oplogSize\":0,\"peers\":[]}";
    }

    size_t oplogSize = 0;
    if (_workbook) {
        OpLog* oplog = _workbook->getOpLog();
        if (oplog) {
            oplogSize = oplog->size();
        }
    }

    std::ostringstream json;
    json << "{";
    json << "\"state\":\"" << cells::net::syncClientStateToString(_syncClient->getState()) << "\",";
    json << "\"peerId\":\"" << _syncClient->getPeerId() << "\",";
    json << "\"roomId\":\"" << _syncClient->getRoomId() << "\",";
    json << "\"peerCount\":" << _syncClient->getPeerCount() << ",";
    json << "\"oplogSize\":" << oplogSize << ",";
    json << "\"peers\":[";

    auto peers = _syncClient->getPeers();
    for (size_t i = 0; i < peers.size(); i++) {
        if (i > 0) json << ",";
        json << "{";
        json << "\"id\":\"" << peers[i].id << "\",";
        json << "\"connected\":" << (peers[i].is_connected ? "true" : "false") << ",";
        json << "\"synced\":" << (peers[i].is_synced ? "true" : "false") << ",";
        json << "\"latency\":" << peers[i].latency_ms;
        json << "}";
    }

    json << "]";
    json << "}";
    return json.str();
}

bool CellsEngine::isSyncEnabled() {
    return _syncClient != nullptr && _syncClient->isConnected();
}

void CellsEngine::processSyncOutgoing() {
    if (_syncClient) {
        _syncClient->processOutgoing();
    }
}

void CellsEngine::processSyncPresence() {
    if (_syncClient) {
        _syncClient->processPresenceUpdates();
    }
}

void CellsEngine::broadcastSyncOperations() {
    if (_syncClient) {
        _syncClient->broadcastOperations();
    }
}

// ============================================================================
// C++ SyncClient presence methods
// ============================================================================

void CellsEngine::setSyncLocalName(const std::string& name) {
    if (_syncClient) {
        _syncClient->setLocalName(name);
    }
}

void CellsEngine::setSyncCurrentSheet(const std::string& sheetId) {
    if (_syncClient) {
        _syncClient->setCurrentSheet(sheetId);
    }
}

void CellsEngine::setSyncCursor(int col, int row) {
    if (_syncClient) {
        _syncClient->setCursor(col, row);
    }
}

void CellsEngine::clearSyncCursor() {
    if (_syncClient) {
        _syncClient->clearCursor();
    }
}

void CellsEngine::setSyncSelection(int startCol, int startRow, int endCol, int endRow) {
    if (_syncClient) {
        cells::net::CursorPosition start;
        start.col = startCol;
        start.row = startRow;
        cells::net::CursorPosition end;
        end.col = endCol;
        end.row = endRow;
        _syncClient->setSelection(start, end);
    }
}

void CellsEngine::clearSyncSelection() {
    if (_syncClient) {
        _syncClient->clearSelection();
    }
}

void CellsEngine::setSyncMousePosition(double x, double y) {
    if (_syncClient) {
        _syncClient->setMousePosition(x, y);
    }
}

void CellsEngine::clearSyncMousePosition() {
    if (_syncClient) {
        _syncClient->clearMousePosition();
    }
}

void CellsEngine::setSyncEditing(int32_t col, int32_t row, const std::string& text) {
    if (_syncClient) {
        _syncClient->setEditing(col, row, text);
    }
}

void CellsEngine::clearSyncEditing() {
    if (_syncClient) {
        _syncClient->clearEditing();
    }
}

std::string CellsEngine::getRemotePresences() {
    if (!_syncClient) {
        return "{\"peers\":{}}";
    }

    std::ostringstream json;
    json << "{\"peers\":{";

    auto remotePeers = _syncClient->getRemotePeers();
    bool first = true;
    for (const auto& [peerId, presence] : remotePeers) {
        if (!first) json << ",";
        first = false;

        json << "\"" << peerId << "\":{";
        json << "\"name\":\"" << jsonEscape(presence.name) << "\",";
        json << "\"color\":\"" << jsonEscape(presence.color) << "\",";
        json << "\"sheetId\":\"" << jsonEscape(presence.sheet_id) << "\",";

        if (presence.has_cursor) {
            json << "\"cursor\":{\"col\":" << presence.cursor.col
                 << ",\"row\":" << presence.cursor.row << "},";
        } else {
            json << "\"cursor\":null,";
        }

        if (presence.has_selection) {
            json << "\"selection\":{\"startCol\":" << presence.selection.start.col
                 << ",\"startRow\":" << presence.selection.start.row
                 << ",\"endCol\":" << presence.selection.end.col
                 << ",\"endRow\":" << presence.selection.end.row << "},";
        } else {
            json << "\"selection\":null,";
        }

        if (presence.has_mouse) {
            json << "\"mouse\":{\"x\":" << presence.mouse.x << ",\"y\":" << presence.mouse.y
                 << "},";
        } else {
            json << "\"mouse\":null,";
        }

        if (presence.is_editing) {
            json << "\"editing\":{\"col\":" << presence.editing_cell.col
                 << ",\"row\":" << presence.editing_cell.row << ",\"text\":\""
                 << jsonEscape(presence.editing_text) << "\"}";
        } else {
            json << "\"editing\":null";
        }

        json << "}";
    }

    json << "}}";
    return json.str();
}

// ============================================================================
// SyncClientDelegate implementation
// ============================================================================

void CellsEngine::syncClientStateDidChange(cells::net::SyncClient& /*client*/,
                                            cells::net::SyncClientState /*newState*/) {
    notifyListeners(ChangeType::SYNC_STATE_CHANGED);
}

void CellsEngine::syncClientPeerDidChange(cells::net::SyncClient& /*client*/,
                                           const cells::net::PeerInfo& peer) {
    notifyListenersWithData(ChangeType::PEER_JOINED, peer.id);
}

void CellsEngine::syncClientPeerDidDisconnect(cells::net::SyncClient& /*client*/,
                                               const std::string& peerId) {
    notifyListenersWithData(ChangeType::PEER_LEFT, peerId);
}

void CellsEngine::syncClientDataDidChange(cells::net::SyncClient& /*client*/) {
    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);
}

void CellsEngine::syncClientDidError(cells::net::SyncClient& /*client*/,
                                      const std::string& error) {
    LOG_ERROR("SyncClient error: %s", error.c_str());
}

void CellsEngine::syncClientLatencyDidUpdate(cells::net::SyncClient& /*client*/,
                                              const std::string& /*peer_id*/,
                                              int /*latency_ms*/) {
    // Latency updates - not currently notified to JS
}

void CellsEngine::syncClientPresenceDidUpdate(cells::net::SyncClient& /*client*/,
                                               const std::string& peerId,
                                               const cells::net::PresenceData& /*presence*/) {
    notifyListenersWithData(ChangeType::PRESENCE_CHANGED, peerId);
}

void CellsEngine::syncClientPresenceDidRemove(cells::net::SyncClient& /*client*/,
                                               const std::string& peerId) {
    notifyListenersWithData(ChangeType::PRESENCE_CHANGED, peerId);
}

}  // namespace cells::wasm
