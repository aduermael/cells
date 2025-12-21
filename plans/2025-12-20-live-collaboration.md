# Live Collaboration Implementation Plan

This plan implements real-time collaboration for Cells using custom CRDTs, WebRTC P2P, and a Go-based signaling server.

## Overview

- **CRDT**: Custom cell-level implementation with Hybrid Logical Clock (HLC) timestamps
- **Transport**: WebRTC DataChannel for peer-to-peer communication
- **Signaling**: WebSocket endpoints in existing Go server (`tools/serve/main.go`)
- **Platform**: Web (WASM) initially, architecture extensible for future platforms
- **User Identity**: Random session IDs, no account creation required
- **Sharing**: Link-based invitations (`cells://doc/<id>` or `https://.../?room=<id>`)
- **UI**: Temporary "Online/Offline" indicator for sync status

## Architecture

```
┌─────────────┐                              ┌─────────────┐
│  Client A   │◄────── WebRTC DataChannel ──►│  Client B   │
│  (Browser)  │         (encrypted P2P)      │  (Browser)  │
└─────────────┘                              └─────────────┘
       │                                            │
       │         ┌─────────────────────┐           │
       └────────►│   Go Server         │◄──────────┘
                 │   WebSocket         │
                 │   Signaling         │
                 └─────────────────────┘
```

## Phase 1: CRDT Foundation (C++)

Implement the core CRDT data structures and operation log in C++ for the engine.

- [x] 1a: Add HLC (Hybrid Logical Clock) struct and implementation
  - `struct HLC { int64_t wall_time; uint32_t logical; ID node_id; }`
  - Comparison operators (wall_time → logical → node_id)
  - `generate_hlc()` function using system time
  - HLC serialization/deserialization for `.cells` format
  - Add tests in `core/cells/hlc_test.cc`

- [x] 1b: Define operation types enum and Operation struct
  - `enum class OpType` with values: CELL_SET_VALUE, CELL_CLEAR, CELL_SET_STYLE, DIM_INSERT_AXIS, DIM_DELETE_AXIS, DIM_MOVE_AXIS, DIM_RESIZE_AXIS, SHEET_CREATE, SHEET_DELETE, SHEET_RENAME
  - `struct Operation { HLC hlc; OpType type; ID target_id; std::string payload; }`
  - Operation serialization to/from JSON for network transport
  - Add tests in `core/cells/operation_test.cc`

- [x] 1c: Implement OpLog (operation log) data structure
  - `struct OpLog` with append-only vector of operations
  - Index by entity ID for quick lookup (`by_cell_id`, `by_axis_id`)
  - `addOperation(Operation op)` method
  - `getOperationsSince(HLC since)` for sync
  - `getOperationsForEntity(ID entity_id)` for history
  - Add tests in `core/cells/oplog_test.cc`

- [x] 1d: Add OpLog to Workbook model
  - Add `std::unique_ptr<OpLog> oplog` field to `Workbook` struct
  - Initialize in constructor
  - Expose via `getOpLog()` method
  - Ensure proper cleanup in destructor

- [x] 1e: Implement operation application and conflict resolution
  - `applyOperation(const Operation& op)` method in Workbook
  - Cell value conflicts: Last-Writer-Wins (higher HLC wins)
  - Axis insert conflicts: Interleave by HLC (lower HLC comes first)
  - Delete vs edit conflicts: Edit resurrects (no data loss)
  - Add comprehensive tests in `core/cells/crdt_test.cc`

- [x] 1f: Update serializer to include OpLog section
  - Add `#oplog` section to `.cells` format
  - Format: `O <hlc> <op-type> <target-id> <payload-json>`
  - Serialize operations in HLC order
  - Add roundtrip test with operations

## Phase 2: Go Signaling Server

Extend the existing Go server with WebSocket signaling for WebRTC connection setup.

- [x] 2a: Add WebSocket dependencies to Go module
  - Add `github.com/gorilla/websocket` to `go.mod`
  - Run `go mod tidy`
  - Update `tools/serve/README.md` with new dependency

- [x] 2b: Implement room management in Go server
  - Create `tools/serve/rooms.go` with `Room` and `RoomManager` structs
  - `Room` tracks connected peers (peer ID → websocket connection)
  - `RoomManager` manages multiple rooms (room ID → Room)
  - Thread-safe operations using `sync.RWMutex`
  - Add unit tests in `tools/serve/rooms_test.go`

- [x] 2c: Add WebSocket signaling endpoint `/ws`
  - Upgrade HTTP to WebSocket in `tools/serve/main.go`
  - Handle WebSocket messages: `join`, `leave`, `offer`, `answer`, `ice-candidate`
  - Parse room ID from query parameter or message body
  - Broadcast peer events to room members
  - Add connection timeout and cleanup

- [x] 2d: Implement signaling message relay
  - Relay SDP offers/answers between peers
  - Relay ICE candidates (trickle ICE)
  - Notify peers when new peer joins (`peer-joined` message)
  - Notify peers when peer leaves (`peer-left` message)
  - Add message validation and error handling

- [x] 2e: Add connection monitoring and cleanup
  - Ping/pong for connection health check
  - Remove disconnected peers from rooms
  - Clean up empty rooms after timeout
  - Log connection events for debugging

- [x] 2f: Update Go server with configuration options
  - Add `-enable-collab` flag to enable collaboration features
  - Add `-max-room-size` flag (default: 10 peers)
  - Add `-room-timeout` flag (default: 1 hour of inactivity)
  - Update help text and README

## Phase 3: WebRTC P2P Layer (JavaScript)

Implement WebRTC peer-to-peer connections in the WASM web app.

- [x] 3a: Create WebRTC connection manager module
  - Create `apps/wasm/static/shared/webrtc-manager.js`
  - `class WebRTCManager` to manage multiple peer connections
  - `createPeerConnection(peerId)` using RTCPeerConnection API
  - Handle ICE candidate gathering and connection state changes
  - Add event emitter for connection events

- [x] 3b: Implement WebSocket signaling client
  - Create `apps/wasm/static/shared/signaling-client.js`
  - `class SignalingClient` to manage WebSocket to Go server
  - Connect to `/ws?room=<roomId>` endpoint
  - Send/receive signaling messages (offer, answer, ICE candidates)
  - Auto-reconnect with exponential backoff
  - Emit events for peer join/leave

- [x] 3c: Implement WebRTC DataChannel for operations
  - Create DataChannel labeled "operations" on each peer connection
  - Configure ordered, reliable delivery for operation messages
  - Handle DataChannel open/close events
  - Implement message serialization (JSON or MessagePack)
  - Add error handling and reconnection logic

- [x] 3d: Implement WebRTC connection flow
  - Generate local peer ID (8-char base62, same as document IDs)
  - Join room via signaling server
  - Create offer when new peer joins
  - Handle incoming offer, create answer
  - Exchange ICE candidates (trickle ICE)
  - Establish DataChannel once connection is ready

- [x] 3e: Add STUN/TURN server configuration
  - Configure ICE servers in RTCPeerConnection
  - Use free public STUN servers: `stun:stun.l.google.com:19302`
  - Add configuration option for custom TURN servers
  - Fallback to multiple STUN servers for redundancy

- [x] 3f: Implement mesh topology for small groups
  - Connect to all peers in room (full mesh)
  - Track active connections in WebRTCManager
  - Handle peer join: establish new connection
  - Handle peer leave: close and clean up connection
  - Limit to 10 peers initially (configurable)

## Phase 4: CRDT Integration & Sync Protocol

Connect the CRDT engine to the WebRTC layer and implement the sync protocol.

- [x] 4a: Add WASM bindings for CRDT operations
  - Export `workbook_get_oplog_operations_since(hlc)` in `apps/wasm/bindings.cc`
  - Export `workbook_apply_operation(op_json)` for remote operations
  - Export `workbook_get_current_hlc()` to get local HLC
  - Export `workbook_set_node_id(node_id)` to set local peer ID
  - Add error handling and validation

- [x] 4b: Implement operation capture in WASM client
  - Create `apps/wasm/static/shared/collab-manager.js`
  - `class CollabManager` to manage collaboration state
  - Intercept local edits (cell value, style, axis operations)
  - Generate operations with HLC timestamp
  - Add to local OpLog via WASM
  - Broadcast to all connected peers via DataChannel

- [x] 4c: Implement operation reception and application
  - Receive operations from peer DataChannels
  - Parse and validate incoming operations
  - Apply to local workbook via WASM `workbook_apply_operation`
  - Update HLC based on received operation timestamp
  - Deduplicate operations (skip if already in OpLog)
  - Handle application errors gracefully

- [x] 4d: Implement sync protocol on connection
  - On peer connection established, exchange `hello` messages
  - `hello` includes: peer ID, current HLC, document ID
  - Request missing operations: `syncRequest(since_hlc)`
  - Respond with operations: `syncResponse([operations])`
  - Apply received operations in HLC order
  - Mark sync complete when caught up

- [x] 4e: Implement incremental sync
  - Track last synced HLC per peer
  - Send only new operations since last sync
  - Batch operations for efficiency (max 100 ops per message)
  - Compress operation payloads using GZIP or similar
  - Add flow control to prevent overwhelming slow peers

- [x] 4f: Add conflict resolution testing
  - Create test scenarios for concurrent edits
  - Test same-cell conflicts (Last-Writer-Wins)
  - Test concurrent axis inserts (interleaving)
  - Test delete vs edit conflicts (resurrection)
  - Verify CRDT convergence across multiple peers

## Phase 5: Presence & UI Features

Implement user presence, online indicators, and collaboration UI elements.

- [x] 5a: Implement presence data structure
  - `struct Presence { ID peer_id; string name; string color; ID sheet_id; CellPos cursor; CellRange selection; }`
  - Generate random names: "User <number>" or random adjective+animal
  - Assign random colors from palette (10+ distinct colors)
  - Track current sheet, cursor position, and selection range
  - Presence is ephemeral: NOT stored in OpLog or persisted to file

- [ ] 5b: Add presence broadcasting via DataChannel
  - Create separate DataChannel labeled "presence" (unreliable, unordered)
  - Broadcast presence updates at 5 Hz (200ms interval) while user is active
  - Include: peer name, color, sheet, cursor position, selection range, viewport
  - Send updates continuously while cursor/selection is moving
  - Continue broadcasting for 3 seconds after movement stops (keep cursor visible)
  - Throttle updates to avoid flooding (max 5 per second)
  - Presence messages are ephemeral events, never persisted

- [ ] 5c: Add UI for online/offline indicator
  - Add status badge to header toolbar (top-right)
  - Show "Online" (green) when connected to ≥1 peer
  - Show "Offline" (gray) when no connections
  - Show "Connecting..." (yellow) during connection setup
  - Add click to show connection details (# of peers, latency)

- [ ] 5d: Add UI for collaborator cursors and selections
  - Render remote user cursor position (active cell) with colored border
  - Render remote user selection ranges as colored overlays (semi-transparent)
  - Show remote user name label next to cursor
  - Update positions in real-time as presence events arrive
  - Keep cursor/selection visible while user is moving
  - Continue showing for 3 seconds after last movement (fade out gradually)
  - Purely visual: NOT stored in state, OpLog, or file
  - Limit to current sheet (hide cursors/selections on other sheets)

- [ ] 5e: Implement share link generation
  - Generate room ID based on document ID or random
  - Create shareable URL format: `https://<host>/?room=<roomId>`
  - Add "Share" button to header toolbar
  - Copy link to clipboard on click
  - Show tooltip: "Link copied! Share with collaborators"

- [ ] 5f: Add room join from URL
  - Parse `?room=<roomId>` from URL query parameters
  - Auto-join room on page load if room parameter present
  - Show "Joining room..." message during connection
  - Handle invalid room ID gracefully (show error)
  - Update URL when creating new room (without reload)

## Phase 6: Session Management & Polish

Add session persistence, random user identity, and final polish.

- [ ] 6a: Implement session identity
  - Generate random peer ID on first visit (8-char base62)
  - Store in localStorage: `session.peerId`
  - Reuse same peer ID across sessions
  - Generate random display name: "User <random_number>"
  - Store in sessionStorage: `session.displayName` (reset each session)

- [ ] 6b: Add session name customization
  - Add "Set Name" option in UI (click status badge)
  - Show modal/popup with input field
  - Update local display name in sessionStorage
  - Broadcast updated presence to all peers
  - Persist choice only for current session (reset on refresh)

- [ ] 6c: Add "New Document" option for clean session
  - Add "New Document" button to header toolbar
  - Clear current workbook and IndexedDB
  - Generate new random document ID
  - Create new room (not join existing)
  - Clear URL room parameter

- [ ] 6d: Implement graceful offline handling
  - Detect when all peers disconnect
  - Keep local workbook editable (offline-first)
  - Queue operations for sync when peer reconnects
  - Show clear UI indication: "Working offline"
  - Auto-reconnect when network restored

- [ ] 6e: Add connection quality indicators
  - Measure round-trip latency via ping/pong
  - Show latency in status badge details
  - Warn if latency >500ms (connection quality poor)
  - Show data transfer stats (ops sent/received)
  - Add "Force Reconnect" option if stuck

- [ ] 6f: Add debugging and diagnostics
  - Add dev mode toggle (localStorage flag)
  - Show detailed connection logs in console
  - Show operation log viewer (all operations in order)
  - Export OpLog for debugging
  - Add "Reset Sync State" option for recovery

## Phase 7: Testing & Documentation

Comprehensive testing and documentation for the collaboration system.

- [ ] 7a: Write unit tests for CRDT operations
  - Test HLC generation and comparison
  - Test operation serialization/deserialization
  - Test OpLog append and query operations
  - Test conflict resolution rules
  - Achieve >90% code coverage for core/cells/crdt.cc

- [ ] 7b: Write integration tests for sync protocol
  - Simulate two-peer scenario in JavaScript
  - Test initial sync (hello → syncRequest → syncResponse)
  - Test incremental sync (new operations only)
  - Test operation deduplication
  - Test reconnection and catchup

- [ ] 7c: Write end-to-end collaboration tests
  - Set up automated browser testing (Playwright or Puppeteer)
  - Open two browser windows, join same room
  - Perform concurrent edits, verify convergence
  - Test presence updates across peers
  - Test offline→online sync

- [ ] 7d: Update documentation for CRDT implementation
  - Document HLC format and generation in `docs/crdt.md`
  - Add operation type reference table
  - Document conflict resolution rules with examples
  - Add OpLog file format specification
  - Include merge examples and edge cases

- [ ] 7e: Update documentation for networking
  - Document WebSocket signaling protocol in `docs/networking.md`
  - Add WebRTC connection flow diagrams
  - Document presence message format
  - Add troubleshooting guide (NAT, firewalls, TURN)
  - Include performance tuning tips

- [ ] 7f: Create user guide for collaboration
  - Write user-facing guide: `docs/collaboration-guide.md`
  - How to share a document (copy link)
  - How to join a shared session
  - Explain online/offline indicator
  - How to set display name
  - Privacy considerations (P2P, no server storage)

## Phase 8: Performance & Optimization

Optimize for production use with large documents and multiple peers.

- [ ] 8a: Implement operation batching
  - Batch rapid edits into single operation
  - Debounce typing (merge character-by-character changes)
  - Flush batch on navigation or 100ms idle
  - Reduce network overhead for typing
  - Test with rapid cell editing

- [ ] 8b: Implement OpLog compaction
  - Compact old operations in OpLog
  - Merge consecutive operations on same cell
  - Only compact operations all peers have seen
  - Create periodic snapshots of state
  - Add compaction trigger (OpLog size threshold)

- [ ] 8c: Optimize presence message size
  - Use binary format instead of JSON (MessagePack)
  - Delta compression (send only changes from previous update)
  - Reduce broadcast frequency when idle (after 3 second visible period)
  - Presence remains ephemeral: NEVER persisted to OpLog or file
  - Test bandwidth usage with 10 peers (target <10 KB/s per peer)

- [ ] 8d: Add selective sheet sync
  - Prioritize operations for active sheet
  - Lazy-load operations for inactive sheets
  - Reduce initial sync payload size
  - Test with multi-sheet workbooks (10+ sheets)

- [ ] 8e: Add connection quality adaptation
  - Detect slow connections (high latency/packet loss)
  - Reduce presence update frequency on slow connections
  - Increase operation batching on slow connections
  - Show warning if connection too slow for real-time

- [ ] 8f: Profile and optimize hot paths
  - Profile operation application performance
  - Optimize OpLog indexing and lookup
  - Optimize DataChannel message parsing
  - Target <16ms (60 FPS) for local operations
  - Target <100ms for remote operation application

---

## Technical Notes

### HLC Format in `.cells` File

```
O 1705312200000.0.N3f8hJ2w CELL_SET_VALUE nP6kR2mW {"type":"n","value":"42"}
  └─────timestamp────┘│└─node─┘
                      └─logical
```

### Operation Message Format (JSON over DataChannel)

```json
{
  "type": "operations",
  "batch": [
    {
      "hlc": "1705312200000.0.N3f8hJ2w",
      "op": "CELL_SET_VALUE",
      "target": "nP6kR2mW",
      "payload": {"type": "n", "value": "42"}
    }
  ]
}
```

### Presence Message Format (JSON)

**Note:** Presence messages are ephemeral events sent continuously (5 Hz) and never persisted to OpLog or file.

```json
{
  "type": "presence",
  "peer_id": "N3f8hJ2w",
  "name": "Swift Fox",
  "color": "#FF5733",
  "sheet_id": "bF3hL8mN",
  "cursor": {"col": "kR7pN2wQ", "row": "jH4sW8nF"},
  "selection": {
    "start": {"col": "kR7pN2wQ", "row": "jH4sW8nF"},
    "end": {"col": "vT5mK9xL", "row": "qM2kL5pR"}
  },
  "timestamp": 1705312200000
}
```

**Fields:**
- `cursor`: Active cell position (where user is typing/focused)
- `selection`: Selected range (may be same as cursor for single cell)
- `timestamp`: Client timestamp for fade-out calculation (3 seconds after last update)

### Operations vs Presence: Key Distinctions

| Aspect | Operations | Presence |
|--------|-----------|----------|
| **Purpose** | Modify document state (cell values, formulas, etc.) | Show real-time user activity (cursors, selections) |
| **Persistence** | Stored in OpLog, persisted to `.cells` file | Ephemeral, never stored |
| **Transport** | Reliable DataChannel (guaranteed delivery) | Unreliable DataChannel (best-effort) |
| **Frequency** | On-demand (when user edits) | Continuous (5 Hz while active, 3s after) |
| **CRDT** | Yes (HLC timestamps, conflict resolution) | No (last-seen wins) |
| **History** | Full history preserved for undo/sync | No history, immediate only |

**Why separate channels?**
- Operations require guaranteed delivery and ordering (CRDT correctness)
- Presence can tolerate packet loss (next update arrives in 200ms)
- Different delivery guarantees optimize network usage

### WebSocket Signaling Messages

**Client → Server:**
```json
{"type": "join", "room": "abc123", "peer_id": "N3f8hJ2w"}
{"type": "offer", "target": "M2g7kL3x", "sdp": "..."}
{"type": "answer", "target": "M2g7kL3x", "sdp": "..."}
{"type": "ice-candidate", "target": "M2g7kL3x", "candidate": "..."}
```

**Server → Client:**
```json
{"type": "peer-joined", "peer_id": "M2g7kL3x"}
{"type": "peer-left", "peer_id": "M2g7kL3x"}
{"type": "offer", "from": "M2g7kL3x", "sdp": "..."}
{"type": "answer", "from": "M2g7kL3x", "sdp": "..."}
{"type": "ice-candidate", "from": "M2g7kL3x", "candidate": "..."}
```

### Random Name Generation

Use pattern: `<Adjective> <Animal>` from predefined lists:

**Adjectives:** Swift, Happy, Clever, Bold, Bright, Quick, Wise, Noble, Brave, Kind
**Animals:** Fox, Bear, Eagle, Wolf, Owl, Lion, Hawk, Deer, Tiger, Panda

Example: "Swift Fox", "Happy Bear", "Clever Eagle"

### Color Palette for User Cursors

Use distinct colors with good contrast:
```javascript
const USER_COLORS = [
  '#FF5733', '#33FF57', '#3357FF', '#FF33F5',
  '#33FFF5', '#F5FF33', '#FF8C33', '#8C33FF',
  '#33FF8C', '#FF338C'
];
```

### STUN Server Configuration

```javascript
const ICE_SERVERS = [
  { urls: 'stun:stun.l.google.com:19302' },
  { urls: 'stun:stun1.l.google.com:19302' },
  { urls: 'stun:stun.cloudflare.com:3478' }
];
```

### File Structure After Implementation

```
core/cells/
├── hlc.h              # HLC timestamp implementation
├── hlc.cc
├── hlc_test.cc
├── operation.h        # Operation types and struct
├── operation.cc
├── operation_test.cc
├── oplog.h            # Operation log
├── oplog.cc
├── oplog_test.cc
├── crdt.h             # CRDT conflict resolution
├── crdt.cc
└── crdt_test.cc

tools/serve/
├── main.go            # HTTP + WebSocket server
├── rooms.go           # Room management
├── rooms_test.go
└── README.md

apps/wasm/static/shared/
├── webrtc-manager.js      # WebRTC P2P layer
├── signaling-client.js    # WebSocket signaling
├── collab-manager.js      # CRDT integration
└── presence.js            # Presence tracking
```

### Development Commands

**Build WASM with collaboration:**
```bash
make wasm-dist
```

**Run Go server with collaboration enabled:**
```bash
go run tools/serve/main.go -enable-collab -port 8081
```

**Run tests:**
```bash
# C++ tests
bazel test //core/...

# Go tests
cd tools/serve && go test ./...

# E2E tests (after implementation)
npm test
```

### Security Considerations

1. **Operation Validation**: Validate all incoming operations before applying
2. **Rate Limiting**: Limit operations per second per peer (100 ops/sec)
3. **HLC Bounds**: Reject operations with future timestamps >5min
4. **Payload Size**: Limit operation payload size (1MB max)
5. **Peer Limit**: Limit peers per room (10 initially, configurable)
6. **Room Timeout**: Clean up inactive rooms after 1 hour

### Future Enhancements (Not in This Plan)

- Access control levels (owner, editor, viewer)
- Operation signatures for audit log
- Application-level encryption (AES-GCM)
- Star topology for larger groups (6-20 peers)
- Native desktop/mobile support
- Persistent server-side storage option
- Conflict resolution UI (manual merge)
- Time-travel / history viewer
- Branch-based undo/redo
