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

// SyncManager handles CRDT synchronization with peers.
// All sync state and protocol logic lives here - JS is just a transport layer.
//
// Message types handled:
//   - hello: Peer announces presence with their HLC and op count
//   - sync-request: Peer requests operations since a given HLC
//   - sync-response: Response with requested operations
//   - operations: Batch of new operations from peer
//   - pending: Uncommitted operation (for live typing visibility)
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
    // Processes the message and returns any response messages to send.
    // The returned messages should be sent to their respective targets.
    std::vector<OutgoingMessage> handleMessage(const ID& peerId, const std::string& json);

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

    // Queue a pending operation broadcast for live typing visibility
    // Call this after creating a local pending operation to show to peers
    void queuePendingBroadcast(const Operation& op);

private:
    // Handle specific message types
    std::vector<OutgoingMessage> handleHello(const ID& peerId, const std::string& json);
    std::vector<OutgoingMessage> handleSyncRequest(const ID& peerId, const std::string& json);
    std::vector<OutgoingMessage> handleSyncResponse(const ID& peerId, const std::string& json);
    std::vector<OutgoingMessage> handleOperations(const ID& peerId, const std::string& json);
    std::vector<OutgoingMessage> handlePending(const ID& peerId, const std::string& json);

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
};

}  // namespace cells

#endif  // CELLS_SYNC_MANAGER_H_
