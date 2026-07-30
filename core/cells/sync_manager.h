// =============================================================================
// Sync Manager
// =============================================================================
//
// Manages CRDT synchronization protocol with remote peers. Handles the sync
// handshake, operation exchange, and conflict resolution across the network.
//
// Key responsibilities:
// - Track connected peers and their sync state (last known HLC)
// - Handle incoming messages: hello, sync-request, sync-response, operations
// - Generate outgoing messages for local operations
// - Prune old operations when all peers have acknowledged them
//
// Sync protocol:
// 1. hello: Exchange HLC and operation count on connect
// 2. sync-request: Request operations since a specific HLC
// 3. sync-response: Send requested operations in HLC order
// 4. operations: Broadcast new operations as they occur
//
// Transport layer:
// The SyncManager is transport-agnostic. TypeScript provides WebRTC/WebSocket
// transport via cpp-sync-adapter.ts which calls into this C++ layer.
//
// Dependencies: hlc.h, operation.h, types.h, model.h (forward decl)
// Used by: bindings.cc (WASM bridge to JS sync layer)
//
// =============================================================================

#ifndef CELLS_SYNC_MANAGER_H_
#define CELLS_SYNC_MANAGER_H_

#include <map>
#include <string>
#include <vector>

#include "core/cells/hlc.h"
#include "core/cells/operation.h"
#include "core/cells/types.h"

namespace cells {

// Forward declaration
struct Workbook;

// State of synchronization with a peer
struct PeerSyncState {
    HLC lastSyncedHLC;     // Last HLC we know they have
    bool isSynced{false};  // True if fully synced (no pending operations)
    size_t opCount{0};     // Number of operations they reported in hello

    PeerSyncState() = default;
};

// Outgoing message to be sent to peers
struct OutgoingMessage {
    ID peerId;         // Target peer ID (empty = broadcast to all peers)
    std::string json;  // JSON message ready to send

    OutgoingMessage();
    OutgoingMessage(const ID& peer, std::string msg);

    // Check if this is a broadcast message (no specific target)
    [[nodiscard]] bool isBroadcast() const;
};

// Result of handling a peer message
struct HandleMessageResult {
    std::vector<OutgoingMessage> messages;      // Response messages to send
    std::vector<Operation> receivedOperations;  // Operations received from peer
    bool dataModified{false};                   // True if cell/structure data changed

    HandleMessageResult() = default;
    explicit HandleMessageResult(std::vector<OutgoingMessage> msgs, bool data = false)
        : messages(std::move(msgs)), dataModified(data) {}
    HandleMessageResult(std::vector<OutgoingMessage> msgs, std::vector<Operation> ops, bool data)
        : messages(std::move(msgs)), receivedOperations(std::move(ops)), dataModified(data) {}
};

// SyncManager handles CRDT synchronization with peers.
// All sync state and protocol logic lives here - JS is just a transport layer.
//
// Message types handled:
//   - hello: Peer announces presence with their HLC and op count
//   - sync-request: Peer requests operations since a given HLC
//   - sync-response: Response with requested operations
//   - operations: Batch of new operations from peer
class SyncManager {
public:
    explicit SyncManager(Workbook* workbook);

    // Peer management
    void addPeer(const ID& peerId);
    void removePeer(const ID& peerId);
    [[nodiscard]] std::vector<ID> getPeerIds() const;
    [[nodiscard]] bool hasPeer(const ID& peerId) const;
    [[nodiscard]] size_t peerCount() const;

    // Get sync state for a specific peer (returns nullptr if peer not found)
    [[nodiscard]] const PeerSyncState* getPeerSyncState(const ID& peerId) const;

    // Handle incoming message from a peer.
    // Processes the message and returns response messages plus flags indicating what changed.
    // - dataModified: true if cell/structure data was modified (needs rebuildQuadtree + notify)
    // - pendingModified: true if remote pending ops changed (needs notify only)
    HandleMessageResult handleMessage(const ID& peerId, const std::string& json);

    // Get and clear all pending outgoing messages.
    // Call this periodically or after local edits to send queued messages.
    std::vector<OutgoingMessage> getOutgoingMessages();

    // Queue a message for broadcast to all peers
    void queueBroadcast(const std::string& json);

    // Queue a message for a specific peer
    void queueToPeer(const ID& peerId, const std::string& json);

    // Queue operations broadcast (called after local edit)
    // Creates an "operations" message with ops newer than peers have seen
    void queueOperationsBroadcast();

    // Queue a full oplog sync-response to every known peer (receivers dedupe).
    // Use after local edits or when peers may have concurrent HLCs so that
    // ops made before a peer joined are never stranded.
    void queueFullSyncToAllPeers();

    // Prune old operations from the OpLog.
    // - Always keeps at least 500 operations for debugging visibility
    // - If no peers: prunes down to 500 ops
    // - If peers: prunes operations older than min HLC all peers have, keeping 500 minimum
    // Returns number of operations pruned.
    size_t pruneOpLog();

    // Debug mode to disable oplog pruning (useful for debugging sync issues)
    void setDebugNoPrune(bool noPrune);

private:
    // Handle specific message types (return result with appropriate flags)
    HandleMessageResult handleHello(const ID& peerId, const std::string& json);
    HandleMessageResult handleSyncRequest(const ID& peerId, const std::string& json);
    HandleMessageResult handleSyncResponse(const ID& peerId, const std::string& json);
    HandleMessageResult handleOperations(const ID& peerId, const std::string& json);
    HandleMessageResult handleAck(const ID& peerId, const std::string& json);

    // Create hello message for sending to new peer
    [[nodiscard]] std::string makeHelloMessage() const;

    // Create sync-request message
    [[nodiscard]] std::string makeSyncRequestMessage(const HLC& sinceHLC) const;

    // Create sync-response message
    [[nodiscard]] std::string makeSyncResponseMessage(const HLC& sinceHLC) const;

    // Create operations message
    [[nodiscard]] std::string makeOperationsMessage(const HLC& sinceHLC) const;

    // Back-reference to workbook (not owned)
    Workbook* _workbook;

    // Connected peers and their sync state
    std::map<ID, PeerSyncState> _peers;

    // Outgoing message queue
    std::vector<OutgoingMessage> _outgoing;

    // Debug flag to disable oplog pruning
    bool _debugNoPrune{false};
};

}  // namespace cells

#endif  // CELLS_SYNC_MANAGER_H_
