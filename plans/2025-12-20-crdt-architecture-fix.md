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

- [x] 3b: Modify cell edit flow
  - `updateCell()` creates pending operation for cell value
  - Pending op immediately queued for broadcast (peers see typing)
  - Pending op NOT in OpLog yet
  - Value applied directly to cell for immediate visual feedback

- [x] 3c: Structure operations commit immediately
  - DIM_INSERT_AXIS (column/row creation) → immediate commit (already done)
  - DIM_RESIZE_AXIS → immediate commit (resizeColumn, resizeRow, resizeColumnByPos, resizeRowByPos)
  - DIM_MOVE_AXIS, rename → deferred to Phase 6 (requires more work)
  - This prevents conflicts when multiple users edit same area

- [x] 3d: Implement commit logic for cell values
  - `commitPendingOps()` moves pending → OpLog - exposed in WASM
  - `commitPendingOpsForCell(cellId)` commits just one cell
  - `hasPendingOps()` / `getPendingOpsCount()` for querying state
  - JS should call on: blur, Enter, navigation (no timeout)
  - Same target pending ops replace previous (debounce) - handled in addPendingOp()

- [x] 3e: Render pending operations
  - Query viewport now includes `pending: true` flag for cells with pending ops
  - Cell values are already showing pending data (applied in updateCell)
  - **Only ONE value per cell** - pending replaces committed in display

- [x] 3f: Handle remote pending operations
  - SyncManager.handlePending() parses and stores remote pending ops
  - SyncManager.queuePendingBroadcast(op) broadcasts local pending ops
  - queuePendingBroadcast(cellId) exposed in WASM for JS to call after updateCell()
  - Remote pending ops cleaned up when: peer disconnects, committed op arrives
  - Replace on next pending or commit from same peer
  - **Never show multiple values** - latest pending wins

## Phase 4: Collaboration Mode & Offline

Implement proper offline vs collaborating mode.

- [x] 4a: Add collaboration mode to Workbook
  - `enum class CollabMode { OFFLINE, COLLABORATING }`
  - Default: OFFLINE
  - Switch to COLLABORATING on first Share/Join

- [x] 4b: Bypass OpLog in offline mode
  - When OFFLINE: direct cell mutation, no operations
  - No HLC overhead, no OpLog storage
  - File save excludes `#oplog` section

- [x] 4c: Bootstrap OpLog on collaboration start
  - On "Share" click: switch to COLLABORATING
  - Generate operations for current state (all axes, all cells)
  - Future edits go through operation path

- [x] 4d: Handle returning to offline
  - "Leave Room" → stay COLLABORATING (ops still tracked)
  - "New Document" → reset to OFFLINE (createEmptyWorkbook defaults to OFFLINE)
  - OpLog persists for rejoining same room
  - Loading files (loadFromCells, loadFromCSV, loadFromXLSX) creates new workbooks in OFFLINE mode

## Phase 5: UI State Machine Refactor

Refactor the JavaScript UI state machine for cleaner architecture and better separation of concerns.

### Design Principles

1. **State machine owns all editing context** - selectedCell, selectionStart, selectionEnd, editValue, etc. live in state machine context
2. **Transitions return success/failure** - No separate validation before calling transition
3. **Listeners handle side effects** - Render, updateFormulaBar, etc. triggered by state change listeners, not inline after transition calls
4. **Cell creation commits immediately** - Reserve UUID for coords ASAP to avoid conflicts; display edited value from editing context, not cell state

### Implementation

- [x] 5a: Add `createCellIfNeeded(col, row)` to engine
  - Single function that creates cell if it doesn't exist
  - Also creates column/row if needed (via DIM_INSERT_AXIS)
  - Operations committed immediately (state updated + added to history)
  - Returns cell UUID (existing or newly created)
  - Eliminates getCellAt/createCell dance with viewport refresh

- [x] 5b: Refactor `transition()` to return boolean
  - Return `false` if transition is not valid from current state
  - Remove separate validation checks at start of event handlers
  - Callers can check: `if (!uiStateMachine.transition(event, context)) return;`
  - NOTE: Already implemented in ui-state.js; usage patterns will be updated in 5e/5f

- [x] 5c: Add context parameter to transitions
  - `transition(event, context)` where context contains relevant data
  - Example: `uiStateMachine.transition(UIEvent.START_SELECTING, { selectedCell: {col, row}, selectionStart: {col, row}, selectionEnd: {col, row} })`
  - State machine merges context into current state on valid transition
  - Getters: `uiStateMachine.getSelectedCell()`, `uiStateMachine.getSelectionStart()`, etc.

- [x] 5d: Add state change listener for refreshes
  - Register listener: `uiStateMachine.onStateChange((oldState, newState, context) => { ... })`
  - Listener calls appropriate refresh functions based on state change
  - Centralized refresh logic - no more scattered render()/updateFormulaBar() calls
  - Refreshes only execute if transition succeeds
  - NOTE: API implemented in ui-state.js; actual listener registration will be added in 5e/5f

- [x] 5e: Update `startEditing` to use new architecture
  - Call `createCellIfNeeded(col, row)` to get cell UUID
  - Transition to editing state with context: `{ cellId, col, row, initialValue }`
  - Cell display: editing context value takes priority, then workbook cell value
  - No pending operation for cell creation - committed immediately
  - Added `createCellIfNeeded()` to WasmDataSource
  - `startEditing` now uses `editingCellId` to track the cell being edited
  - `confirmEditing` uses `editingCellId` directly instead of re-looking up the cell

- [x] 5f: Update event handlers to use new pattern
  - Mouse handlers: `transition(event, context)` pattern - DONE for START_SELECTING
  - Keyboard handlers: same pattern (already using inline approach, works well)
  - Registered state change listener placeholder for future centralization
  - Note: Inline render()/updateFormulaBar() calls retained for precise timing control
  - The listener pattern is set up but not actively removing inline calls yet (risk of timing issues)

## Phase 6: Export Feature

Add ability to export .cells file with operations.

- [ ] 6a: Add "Export .cells" button to UI
  - Add button in header toolbar (near Share)
  - Trigger download of current workbook as .cells file
  - Include full OpLog section if collaborating

- [ ] 6b: Verify serializer includes OpLog
  - Check `exportToCells()` includes `#oplog` section
  - Operations in HLC order
  - Format: `O <hlc> <op-type> <target-id> <payload-json>`

- [ ] 6c: Add download trigger in JS
  - Call `engine.exportToCells()`
  - Create Blob and trigger download
  - Filename: `<workbook-name>.cells`

## Phase 7: Bug Fixes & Structure Sync

Address specific bugs and ensure all operations sync.

- [ ] 7a: Fix initial file not loading on room join
  - Ensure workbook loads from IndexedDB BEFORE sync
  - Add "full state" message for new joiners with no local data
  - Sync protocol handles empty vs populated workbook

- [ ] 7b: Fix duplicate values in concurrent edits
  - **Invariant: It must be IMPOSSIBLE to see 2 values in same cell**
  - Ensure only ONE value per cell in viewport query
  - Priority: pending (local) > pending (remote) > committed
  - Last-Writer-Wins properly supersedes old values
  - If this invariant is violated, there's a bug in the architecture

- [ ] 7c: Verify operation ordering
  - DIM_INSERT_AXIS ops always applied before CELL_SET_VALUE
  - Sort incoming batches by HLC
  - Validate target exists before applying

- [ ] 7d: Ensure all structure operations sync
  - Column/row **move** (DIM_MOVE_AXIS) → sync to peers
  - Column/row **resize** (DIM_RESIZE_AXIS) → sync to peers
  - Column/row **rename** → sync to peers (add new op type if needed)
  - All structure changes must reflect on all connected clients

## Phase 8: Testing & Validation

Verify the architecture fixes work correctly.

- [ ] 8a: Test offline editing (no room)
  - Create new document, edit cells
  - Verify no OpLog (CollabMode::OFFLINE)
  - Export file, verify no `#oplog` section

- [ ] 8b: Test collaboration start
  - Click Share, verify mode switches to COLLABORATING
  - Edit cell, verify operation created
  - Export file, verify `#oplog` section present

- [ ] 8c: Test concurrent editing
  - Two clients, same cell
  - Verify only one value shows (LWW)
  - Export both files, compare OpLogs

- [ ] 8d: Test room join with existing file
  - Client A creates room, edits
  - Client B joins with URL
  - Verify B receives A's state
  - Verify B's edits sync to A

- [ ] 8e: Test live typing visibility
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
| OpLog storage | C++ | C++ | ✅ Done |
| HLC generation | C++ | C++ | ✅ Done |
| applyOperation | C++ | C++ | ✅ Done |
| Peer sync state | C++ | C++ | ✅ Done (Phase 1) |
| Sync protocol | C++ | C++ | ✅ Done (Phase 1) |
| Outgoing queue | C++ | C++ | ✅ Done (Phase 1) |
| JS transport layer | Mixed | JS only | ✅ Done (Phase 2) |
| Pending ops | C++ | C++ | ✅ Done (Phase 3) |
| Collab mode | C++ | C++ | ✅ Done (Phase 4) |
| UI State Machine | Scattered | Centralized | **Phase 5** |
| Export feature | None | JS+C++ | Phase 6 |
| Bug fixes | TBD | Fixed | Phase 7 |
| Testing | TBD | Validated | Phase 8 |

Phases 1-4 complete. Phase 5 is a UI architecture refactor for cleaner state management.
