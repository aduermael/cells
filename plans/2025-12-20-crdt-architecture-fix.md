# CRDT Architecture Fix Plan

This plan addresses fundamental architecture issues in the collaboration system where the Workbook should be the sole source of truth, with Operations as the primary mutation mechanism.

## Problem Summary

1. **Dual mutation paths**: Cells are modified directly AND via operations, causing inconsistency
2. **Operation ordering**: Column/row creation operations not guaranteed before cell operations
3. **No pending state**: Operations apply immediately, no visibility into "typing in progress"
4. **Offline mode confusion**: Operations ledger active even when never collaborating
5. **Sync bugs**: Initial file not loading on room join, duplicate values in concurrent edits
6. **JS has CRDT state**: Sync logic, peer tracking, HLC tracking duplicated in JavaScript

## Core Principle: Engine as Single Source of Truth

**ALL CRDT state must live in C++ engine. JS is only a transport layer.**

### Current State (WRONG)

```
┌─────────────────────────────────────────────────────────────────┐
│                    JS (collab-manager.js)                        │
│  - _pendingOperations (waiting to send)                          │
│  - _peerSyncState (sync progress per peer)                       │
│  - lastKnownHLC (what's been broadcast)                          │
│  - Sync protocol (hello/request/response)                        │
│  - Calls engine.getOperationsSince() to find new ops             │
└─────────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│                    C++ ENGINE                                    │
│  - OpLog (operations)                                            │
│  - applyOperation()                                              │
│  - getOperationsSince()                                          │
└─────────────────────────────────────────────────────────────────┘
```

### Target State (CORRECT)

```
┌─────────────────────────────────────────────────────────────────┐
│                    C++ ENGINE                                    │
│  ┌─────────────┐  ┌─────────────┐  ┌──────────────────────────┐ │
│  │   OpLog     │  │ PendingOps  │  │   SyncManager            │ │
│  │ (committed) │  │ (uncommitted)│  │ - peer sync state        │ │
│  └─────────────┘  └─────────────┘  │ - outgoing msg queue     │ │
│                                     │ - handles sync protocol  │ │
│                                     └──────────────────────────┘ │
│                                                                  │
│  API:                                                            │
│    createOperation(type, target, payload) → pending op           │
│    commitPendingOps() → moves pending → OpLog                    │
│    handlePeerMessage(peerId, msg) → process incoming             │
│    getOutgoingMessages() → messages to send to peers             │
│    addPeer(peerId) / removePeer(peerId)                          │
└─────────────────────────────────────────────────────────────────┘
                           │
                           │ listener("sync") callback
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│                    JS TRANSPORT LAYER                            │
│  - Establish WebRTC/WebSocket connections                        │
│  - On message: engine.handlePeerMessage(peerId, msg)             │
│  - On timer: msgs = engine.getOutgoingMessages(); send(msgs)     │
│  - On listener: refresh viewport                                 │
│  - NO CRDT STATE - just connection management                    │
└─────────────────────────────────────────────────────────────────┘
```

## Architecture Principles

```
┌─────────────────────────────────────────────────────────────────┐
│                         USER ACTION                              │
│                    (type in cell, resize, etc.)                  │
└─────────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                      CREATE OPERATION                            │
│  - Generate cell UUID (new) or use existing                      │
│  - Prepend DIM_INSERT_AXIS ops if new col/row needed            │
│  - Operation is PENDING (not yet committed)                      │
└─────────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                      PENDING OPERATIONS                          │
│  - Stored separately from committed OpLog                        │
│  - Rendered with priority over committed cell values             │
│  - Broadcast to peers immediately (for live typing visibility)   │
│  - Committed on: cell blur, Enter key, or 500ms timeout          │
└─────────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                      COMMIT OPERATION                            │
│  - Move from pending to committed OpLog                          │
│  - Apply to Workbook state (cells, axes, etc.)                   │
│  - If collaborating: already broadcast, just confirm             │
│  - If offline-only: bypass OpLog entirely (direct mutation)      │
└─────────────────────────────────────────────────────────────────┘
```

## Collaboration Modes

| Mode | Description | OpLog Behavior |
|------|-------------|----------------|
| **Offline-only** | Never created/joined room | Operations bypass OpLog, direct mutation |
| **Collaborating** | Active room (even if alone) | Full OpLog, operations broadcast |
| **Disconnected** | Was collaborating, now offline | OpLog active, queue for reconnect |

## Phase 1: Move Sync State to C++ Engine

Move all CRDT sync state from JavaScript to C++ engine.

- [x] 1a: Add SyncManager class to engine
  - Create `core/cells/sync_manager.h` and `sync_manager.cc`
  - `class SyncManager` owned by Workbook
  - Track peer sync state: `std::map<ID, PeerSyncState>` where PeerSyncState = {lastSyncedHLC, isSynced}
  - Track outgoing message queue: `std::vector<OutgoingMessage>` where OutgoingMessage = {peerId (or broadcast), messageJson}

- [x] 1b: Move sync protocol to C++
  - `handlePeerMessage(peerId, messageJson)` → parses and processes:
    - `hello` → stores peer HLC, queues sync-request
    - `sync-request` → queries OpLog, queues sync-response
    - `sync-response` → applies operations
    - `operations` → applies batch
  - Returns list of outgoing messages to send
  - All sync logic in C++, no JS decision-making

- [x] 1c: Add peer management API
  - `addPeer(peerId)` → register new peer, queue hello message
  - `removePeer(peerId)` → cleanup peer state
  - `getPeerIds()` → list connected peers
  - `getPeerSyncState(peerId)` → check if synced

- [x] 1d: Add outgoing message queue
  - `getOutgoingMessages()` → returns and clears queue
  - Messages are JSON strings ready to send
  - Queue is filled by: local operations, sync responses, hello messages

- [x] 1e: Expose new API in WASM bindings
  - `handlePeerMessage(peerId, msg)` → returns JSON with outgoing messages
  - `addPeer(peerId)` / `removePeer(peerId)` / `getPeerIds()` / `getPeerCount()`
  - `getOutgoingMessages()` → returns and clears outgoing queue
  - `initSyncManager()` → initialize sync manager before use
  - `queueOperationsBroadcast()` → queue local ops for broadcast
  - Existing listener callback notifies on state changes

## Phase 2: Simplify JS to Transport Layer

Refactor JavaScript to only handle connections, not CRDT logic.

- [x] 2a: Refactor collab-manager.js
  - Remove `_pendingOperations` (moved to engine)
  - Remove `_peerSyncState` (moved to engine)
  - Remove sync protocol handling (hello/request/response)
  - Keep only: WebRTC setup, message routing

- [x] 2b: Implement message routing
  - On WebRTC message received: `engine.handlePeerMessage(peerId, msg)`
  - Get response messages and send them
  - On peer connect: `engine.addPeer(peerId)`
  - On peer disconnect: `engine.removePeer(peerId)`

- [x] 2c: Implement outgoing message pump
  - Poll `engine.getOutgoingMessages()` on timer (50ms)
  - Or trigger after local edit
  - Send messages to appropriate peers

- [x] 2d: Remove lastKnownHLC from index.html
  - Engine tracks what needs broadcasting per peer
  - No more `broadcastNewOperations()` polling
  - Local edits automatically queue outgoing messages

## Phase 3: Pending Operations & Live Typing

Add pending operation support for live typing visibility.

**Important distinctions:**
- **Cell values**: Can be pending (for live typing debounce)
- **Structure operations** (column/row create, move, resize, rename): Commit immediately to avoid conflicts

- [x] 3a: Add pending operations to Workbook
  - `std::vector<PendingOperation> _pendingOps` with `PendingOperation` struct (op + sourcePeerId)
  - Separate from committed OpLog
  - Pending ops have HLC but not in OpLog yet
  - **Only CELL_SET_VALUE can be pending** - structure ops commit immediately
  - Added methods: `addPendingOp()`, `addRemotePendingOp()`, `removePendingOpsForTarget()`,
    `removePendingOpsFromPeer()`, `commitPendingOps()`, `commitPendingOpsForTarget()`,
    `getPendingOps()`, `getPendingOpForTarget()`, `hasPendingOps()`, `pendingOpsCount()`

- [ ] 3b: Modify cell edit flow
  - `updateCell()` creates pending operation for cell value
  - Pending op immediately queued for broadcast (peers see typing)
  - Pending op NOT in OpLog yet

- [ ] 3c: Structure operations commit immediately
  - DIM_INSERT_AXIS (column/row creation) → immediate commit
  - DIM_MOVE_AXIS, DIM_RESIZE_AXIS → immediate commit
  - Column/row rename → immediate commit
  - This prevents conflicts when multiple users edit same area

- [ ] 3d: Implement commit logic for cell values
  - `commitPendingOps()` moves pending → OpLog
  - Triggers on: blur, Enter, navigation, 500ms timeout
  - Same target pending ops replace previous (debounce)

- [ ] 3e: Render pending operations
  - Query viewport returns pending op values too
  - Pending value shown over committed value
  - **Only ONE value per cell** - pending replaces committed in display

- [ ] 3f: Handle remote pending operations
  - Receive "pending" message type from peers
  - Show in UI but don't add to OpLog
  - Replace on next pending or commit from same peer
  - **Never show multiple values** - latest pending wins

## Phase 4: Collaboration Mode & Offline

Implement proper offline vs collaborating mode.

- [ ] 4a: Add collaboration mode to Workbook
  - `enum class CollabMode { OFFLINE, COLLABORATING }`
  - Default: OFFLINE
  - Switch to COLLABORATING on first Share/Join

- [ ] 4b: Bypass OpLog in offline mode
  - When OFFLINE: direct cell mutation, no operations
  - No HLC overhead, no OpLog storage
  - File save excludes `#oplog` section

- [ ] 4c: Bootstrap OpLog on collaboration start
  - On "Share" click: switch to COLLABORATING
  - Generate operations for current state (all axes, all cells)
  - Future edits go through operation path

- [ ] 4d: Handle returning to offline
  - "Leave Room" → stay COLLABORATING (ops still tracked)
  - "New Document" → reset to OFFLINE
  - OpLog persists for rejoining same room

## Phase 5: Export Feature

Add ability to export .cells file with operations.

- [ ] 5a: Add "Export .cells" button to UI
  - Add button in header toolbar (near Share)
  - Trigger download of current workbook as .cells file
  - Include full OpLog section if collaborating

- [ ] 5b: Verify serializer includes OpLog
  - Check `exportToCells()` includes `#oplog` section
  - Operations in HLC order
  - Format: `O <hlc> <op-type> <target-id> <payload-json>`

- [ ] 5c: Add download trigger in JS
  - Call `engine.exportToCells()`
  - Create Blob and trigger download
  - Filename: `<workbook-name>.cells`

## Phase 6: Bug Fixes & Structure Sync

Address specific bugs and ensure all operations sync.

- [ ] 6a: Fix initial file not loading on room join
  - Ensure workbook loads from IndexedDB BEFORE sync
  - Add "full state" message for new joiners with no local data
  - Sync protocol handles empty vs populated workbook

- [ ] 6b: Fix duplicate values in concurrent edits
  - **Invariant: It must be IMPOSSIBLE to see 2 values in same cell**
  - Ensure only ONE value per cell in viewport query
  - Priority: pending (local) > pending (remote) > committed
  - Last-Writer-Wins properly supersedes old values
  - If this invariant is violated, there's a bug in the architecture

- [ ] 6c: Verify operation ordering
  - DIM_INSERT_AXIS ops always applied before CELL_SET_VALUE
  - Sort incoming batches by HLC
  - Validate target exists before applying

- [ ] 6d: Ensure all structure operations sync
  - Column/row **move** (DIM_MOVE_AXIS) → sync to peers
  - Column/row **resize** (DIM_RESIZE_AXIS) → sync to peers
  - Column/row **rename** → sync to peers (add new op type if needed)
  - All structure changes must reflect on all connected clients

## Phase 7: Testing & Validation

Verify the architecture fixes work correctly.

- [ ] 7a: Test offline editing (no room)
  - Create new document, edit cells
  - Verify no OpLog (CollabMode::OFFLINE)
  - Export file, verify no `#oplog` section

- [ ] 7b: Test collaboration start
  - Click Share, verify mode switches to COLLABORATING
  - Edit cell, verify operation created
  - Export file, verify `#oplog` section present

- [ ] 7c: Test concurrent editing
  - Two clients, same cell
  - Verify only one value shows (LWW)
  - Export both files, compare OpLogs

- [ ] 7d: Test room join with existing file
  - Client A creates room, edits
  - Client B joins with URL
  - Verify B receives A's state
  - Verify B's edits sync to A

- [ ] 7e: Test live typing visibility
  - Client A types slowly in cell
  - Client B sees pending value updating
  - A commits (blur/enter), B sees final value

---

## Technical Notes

### New C++ Classes

**SyncManager** (`core/cells/sync_manager.h`):
```cpp
struct PeerSyncState {
    HLC lastSyncedHLC;
    bool isSynced;
};

struct OutgoingMessage {
    ID peerId;        // Empty = broadcast to all
    std::string json; // Ready to send
};

class SyncManager {
public:
    // Peer management
    void addPeer(const ID& peerId);
    void removePeer(const ID& peerId);
    std::vector<ID> getPeerIds() const;

    // Message handling (returns messages to send)
    std::vector<OutgoingMessage> handleMessage(const ID& peerId, const std::string& json);

    // Outgoing queue
    std::vector<OutgoingMessage> getOutgoingMessages();
    void queueBroadcast(const std::string& json);
    void queueToPeer(const ID& peerId, const std::string& json);

private:
    std::map<ID, PeerSyncState> _peers;
    std::vector<OutgoingMessage> _outgoing;
    Workbook* _workbook;  // Back-reference
};
```

### Pending vs Committed Operations

```
Pending Operations:
- Created immediately on user input
- Broadcast to peers for live visibility (as "pending" message)
- NOT in committed OpLog yet
- Can be replaced (same target = replace previous pending)
- Rendered with priority over committed values

Committed Operations:
- In OpLog permanently
- Applied to Workbook state
- Cannot be replaced (only superseded by newer op)
- Used for sync, persistence, conflict resolution
```

### Message Types (Engine Handles All)

```json
// Handled by SyncManager.handleMessage():
{"type": "hello", "peer_id": "...", "hlc": "...", "op_count": 0}
{"type": "sync-request", "since_hlc": "..."}
{"type": "sync-response", "operations": [...], "complete": true}
{"type": "operations", "batch": [...]}
{"type": "pending", "operation": {...}}  // For live typing
```

### JS Transport Layer (Minimal)

```javascript
// collab-manager.js becomes thin:
class CollabManager {
    // On WebRTC message:
    async onMessage(peerId, data) {
        const responses = await this._engine.handlePeerMessage(peerId, data);
        for (const msg of responses) {
            this._sendToPeer(msg.peerId, msg.json);
        }
    }

    // On peer connect:
    onPeerReady(peerId) {
        this._engine.addPeer(peerId);
        this._flushOutgoing();
    }

    // Outgoing pump (called on timer or after edit):
    async _flushOutgoing() {
        const messages = await this._engine.getOutgoingMessages();
        for (const msg of messages) {
            if (msg.peerId) {
                this._sendToPeer(msg.peerId, msg.json);
            } else {
                this._broadcast(msg.json);
            }
        }
    }
}
```

### File Format with OpLog

```
#cells v1
#sheet <id> <name>
#columns
...
#rows
...
#cells
...
#oplog
O 1705312200000.0.N3f8hJ2w CELL_SET_VALUE nP6kR2mW {"type":"n","value":"42","col_id":"abc","row_id":"def"}
O 1705312200001.0.N3f8hJ2w CELL_SET_VALUE nP6kR2mW {"type":"n","value":"43","col_id":"abc","row_id":"def"}
```

### Bootstrap OpLog (existing document → collaboration)

When switching from OFFLINE to COLLABORATING:
1. Generate DIM_INSERT_AXIS for all existing columns (in position order)
2. Generate DIM_INSERT_AXIS for all existing rows (in position order)
3. Generate CELL_SET_VALUE for all existing cells (any order)
4. All with same base HLC (sequential logical counter)

This creates a "genesis" OpLog that represents current state.

### How Far Are We?

Current state vs target:
| Component | Current | Target | Gap |
|-----------|---------|--------|-----|
| OpLog storage | C++ | C++ | Done |
| HLC generation | C++ | C++ | Done |
| applyOperation | C++ | C++ | Done |
| Peer sync state | JS | C++ | **Move** |
| Sync protocol | JS | C++ | **Move** |
| Outgoing queue | JS | C++ | **Move** |
| Pending ops | None | C++ | **Add** |
| Collab mode | None | C++ | **Add** |

The core CRDT logic is in C++. Main work is moving sync orchestration to C++ and simplifying JS.
