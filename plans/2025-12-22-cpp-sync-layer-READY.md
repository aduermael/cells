# C++ Synchronization Layer Plan

Move synchronization handling to the C++ level following the xptools pattern from bliporg/blip. This enables future non-web clients (iOS/macOS native, CLI) to collaborate using the same sync logic.

**Current State:** Networking is already implemented in JavaScript:
- `apps/wasm/static/shared/webrtc-manager.js` - WebRTC DataChannel
- `apps/wasm/static/shared/signaling-client.js` - Signaling
- `apps/wasm/static/shared/collab-manager.js` - Sync protocol
- `apps/wasm/static/shared/presence.js` - Presence/cursors

**Goals:**
1. Port sync logic from JS to C++ core
2. Abstract `HttpRequest` and `WSConnection` interfaces in C++
3. Platform-specific implementations for Web (emscripten) and iOS/macOS (NSURLSession)
4. JS becomes thin wrapper - only handles UI refresh
5. Initial platform support: Web + iOS/macOS

**Decisions:**
- **Signaling**: Use existing Go server (already serves WASM app)
- **Wire format**: JSON for simplicity (can optimize to binary later)
- **Auth**: Out of scope - no authentication for now
- **WebRTC**: Full port from JS - P2P DataChannel included
- **Threading**: All network callbacks marshal to main thread before invoking delegates
- **Browser APIs**: Web implementations call browser APIs directly via EM_ASM/embind (no emscripten wrappers)

**Reference:** `.blip-ref/deps/xptools/` (cloned from bliporg/blip)

---

## Phase 1: Create xptools-style Directory Structure

Set up the cross-platform networking infrastructure directory.

- [ ] 1a: Create `core/net/` directory with BUILD file for Bazel
- [ ] 1b: Create `core/net/include/` for public headers
- [ ] 1c: Create `core/net/common/` for shared implementation
- [ ] 1d: Create `core/net/web/` for emscripten/WASM implementations
- [ ] 1e: Create `core/net/apple/` for iOS/macOS implementations (Objective-C++)

---

## Phase 2: HTTP Request Abstraction

Port the HttpRequest pattern from xptools. Abstract interface with platform-specific implementations.

- [ ] 2a: Create `HttpRequest.h` interface in `core/net/include/`
  - Status enum (WAITING, PROCESSING, FAILED, CANCELLED, DONE)
  - Factory method `make(method, host, port, path, queryParams, secure)`
  - `sendAsync()`, `sendSync()`, `cancel()`
  - `getResponse()` returning HttpResponse
  - Callback mechanism (`setCallback(HttpRequestCallback)`)
  - Platform hooks: `_sendAsync()`, `_cancel()`, `_attachPlatformObject()`, `_detachPlatformObject()`

- [ ] 2b: Create `HttpResponse.h` in `core/net/include/`
  - Status code, headers, body bytes
  - `appendBytes()`, `readAllBytes()`
  - Success/failure state

- [ ] 2c: Create `URL.h` utility class for URL parsing
  - scheme, host, port, path, query components
  - URL string construction

- [ ] 2d: Create common HttpRequest implementation in `core/net/common/HttpRequest.cc`
  - Shared logic (callback handling, status management)
  - Calls platform-specific `_sendAsync()`

- [ ] 2e: Create Web implementation in `core/net/web/HttpRequest_web.cc`
  - Use `emscripten_fetch` API
  - Handle async callbacks via emscripten

- [ ] 2f: Create Apple implementation in `core/net/apple/HttpRequest.mm`
  - Use NSURLSession with NSURLSessionDataDelegate
  - NetworkManager singleton pattern (like xptools)
  - ARC bridging with `__bridge_retained`/`__bridge`

---

## Phase 3: WebSocket Connection Abstraction

Port the WSConnection pattern from xptools for real-time sync.

- [ ] 3a: Create `Connection.h` base class in `core/net/include/`
  - Status enum (IDLE, OK, CLOSED_ON_ERROR, CLOSED)
  - Payload class for framing (with metadata support)
  - ConnectionDelegate interface: `connectionDidEstablish()`, `connectionDidReceive()`, `connectionDidClose()`
  - Abstract methods: `connect()`, `reset()`, `close()`, `pushPayloadToWrite()`

- [ ] 3b: Create `WSConnection.h` in `core/net/include/`
  - Extends Connection
  - Factory method `make(url)` or `make(scheme, addr, port, path)`
  - WebSocket-specific: `getURL()`, `getHost()`, `getPort()`, `getPath()`, `getSecure()`
  - Platform hooks: `_init()`, `_connect()`, `_writePayload()`, `_close()`, `_destroy()`

- [ ] 3c: Create common WSConnection implementation in `core/net/common/WSConnection.cc`
  - Shared logic (payload queue, received bytes buffer, status management)
  - Write buffering with fragmentation support

- [ ] 3d: Create Web implementation in `core/net/web/WSConnection_web.cc`
  - Use `emscripten_websocket_*` APIs
  - Callbacks: onopen, onerror, onclose, onmessage
  - Binary message support

- [ ] 3e: Create Apple implementation in `core/net/apple/WSConnection.mm`
  - Use NSURLSessionWebSocketTask
  - WebSocketConnection Obj-C wrapper class
  - Handle open/close/message delegates
  - Dispatch to main queue for thread safety

---

## Phase 4: WebRTC Abstraction

Port WebRTC P2P from JS (`webrtc-manager.js`, `ice-config.js`).

- [ ] 4a: Create `RTCPeerConnection.h` in `core/net/include/`
  - `createOffer()`, `createAnswer()`, `setLocalDescription()`, `setRemoteDescription()`
  - `addIceCandidate()`, `onIceCandidate` callback
  - `createDataChannel()`, `onDataChannel` callback
  - RTCPeerConnectionDelegate: `onConnectionStateChange()`, `onIceConnectionStateChange()`

- [ ] 4b: Create `RTCDataChannel.h` in `core/net/include/`
  - `send(data)`, `close()`
  - DataChannelDelegate: `onOpen()`, `onClose()`, `onMessage()`, `onError()`
  - Binary and text message support

- [ ] 4c: Create `ICEConfig.h` for STUN/TURN configuration
  - ICE server list (URLs, credentials)
  - Load from `ice-config.js` equivalent

- [ ] 4d: Create Web implementation in `core/net/web/RTCPeerConnection_web.cc`
  - Use emscripten WebRTC bindings
  - Map to browser's RTCPeerConnection API

- [ ] 4e: Create Apple implementation in `core/net/apple/RTCPeerConnection.mm`
  - Use [stasel/WebRTC](https://github.com/stasel/WebRTC) prebuilt XCFramework
  - Install via Swift Package Manager or CocoaPods (`pod 'WebRTC-lib'`)
  - Supports iOS 12+, macOS 10.11+, macOS Catalyst (arm64 + x86_64)
  - Actively maintained, tracks Chromium releases (currently M141)

- [ ] 4f: Add WebRTC tests
  - Loopback test (connect to self)
  - DataChannel send/receive

---

## Phase 5: Signaling Protocol

Connect to existing Go server for WebRTC signaling (offer/answer/ICE exchange).

**Reference existing JS:** Study `signaling-client.js` to ensure wire-compatibility.

- [ ] 5a: Create `SignalingClient.h` in `core/net/include/`
  - `connect(serverUrl)`, `disconnect()`
  - `joinRoom(roomId)`, `leaveRoom()`
  - `sendOffer(peerId, sdp)`, `sendAnswer(peerId, sdp)`, `sendIceCandidate(peerId, candidate)`
  - SignalingDelegate: `onPeerJoined()`, `onPeerLeft()`, `onOffer()`, `onAnswer()`, `onIceCandidate()`

- [ ] 5b: Create `SignalingClient.cc` implementation in `core/net/common/`
  - Uses WSConnection for server communication
  - JSON message protocol matching existing JS
  - Reconnection logic with exponential backoff

- [ ] 5c: Define signaling message protocol in `core/net/include/SignalingProtocol.h`
  - Message types: JOIN, LEAVE, OFFER, ANSWER, ICE_CANDIDATE, PEER_JOINED, PEER_LEFT
  - JSON serialization helpers

- [ ] 5d: Add signaling tests in `core/net/signaling_test.cc`
  - Mock WSConnection for unit testing
  - Test join/leave flow, offer/answer exchange

---

## Phase 6: Sync Protocol Layer

Port sync protocol from JS (`collab-manager.js`). Build on CRDT docs.

**Reference existing JS:** Study `collab-manager.js` for message format and sync flow.

- [ ] 6a: Create `SyncClient.h` in `core/net/include/`
  - `startSync(documentId)`, `stopSync()`
  - `pushOperation(Operation)` - queue local operation for sync
  - `onOperationsReceived(callback)` - callback for remote operations
  - SyncDelegate: `onSyncStateChanged()`, `onPeersChanged()`

- [ ] 6b: Create `Operation.h` - CRDT operation structures
  - HLC (Hybrid Logical Clock) for ordering
  - Operation types: CELL_SET_VALUE, CELL_CLEAR, DIM_INSERT_AXIS, etc.
  - JSON serialization for wire format

- [ ] 6c: Create `SyncClient.cc` implementation in `core/net/common/`
  - Uses SignalingClient for connection setup
  - Uses RTCDataChannel for P2P data exchange
  - Operation batching and deduplication
  - Sync request/response protocol matching JS

- [ ] 6d: Create `OperationLog.h` and implementation
  - Append-only log of operations
  - Indexes by cell_id, axis_id
  - Compaction support (future)

- [ ] 6e: Integrate existing HLC from `core/cells/hlc.h`
  - Already implemented: `HLC` struct with wall_time, logical, node_id
  - Has comparison operators, string serialization
  - Functions: `generate_hlc()`, `update_hlc()`, `generate_initial_hlc()`
  - Just need to wire into SyncClient

---

## Phase 6.5: Presence Abstraction

Handle cursor positions, selections, and user presence. Presence data is ephemeral and never affects the Workbook (only Operations can mutate Workbook state).

- [ ] 6.5a: Create `Presence.h` in `core/net/include/`
  - `PresenceData` struct: user_id, cursor_cell, selection_range, color, name
  - `PresenceManager` class: tracks all peers' presence
  - `updateLocalPresence(PresenceData)` - called when local user moves cursor/selection
  - `getRemotePresences()` - returns map of peer_id -> PresenceData
  - PresenceDelegate: `onPresenceChanged(peer_id, PresenceData)`

- [ ] 6.5b: Create `Presence.cc` implementation in `core/net/common/`
  - Broadcast local presence changes via DataChannel
  - Receive and store remote presence updates
  - Automatic cleanup when peer disconnects
  - Throttle outbound updates (e.g., max 30/sec)

- [ ] 6.5c: Integrate with SyncClient
  - SyncClient owns PresenceManager
  - Presence messages use same DataChannel as operations
  - Separate message type (PRESENCE vs OPERATION)

- [ ] 6.5d: Wire JS presence events to C++
  - JS calls `updatePresence(cell, selection)` on cursor move
  - C++ broadcasts to peers, receives from peers
  - JS receives remote presence via listener callback
  - Existing `presence.js` renders remote cursors (unchanged)

---

## Phase 7: Integration with CellsEngine

Connect the sync layer to the existing engine.

- [ ] 7a: Add SyncClient to CellsEngine
  - `enableSync(serverUrl, documentId)`, `disableSync()`
  - `getSyncState()` - connected, syncing, offline

- [ ] 7b: Hook mutations to generate operations
  - In updateCell, createCell, resizeColumn, etc.
  - Create Operation with HLC timestamp
  - Push to SyncClient

- [ ] 7c: Apply received operations to workbook
  - SyncClient calls back into engine with remote operations
  - Apply with conflict resolution (LWW for cells)
  - Skip if operation already applied (dedup by HLC)

- [ ] 7d: Add sync-related callbacks to listener system
  - SYNC_STATE_CHANGED, PEER_JOINED, PEER_LEFT
  - Extend existing onDataChanged mechanism

- [ ] 7e: Update WASM bindings (bindings.cc)
  - Expose sync methods: `enableSync()`, `disableSync()`, `getSyncState()`
  - Add sync event callbacks via existing listener pattern

---

## Phase 8: Update JavaScript Layer

Simplify JS to thin wrapper - sync happens in C++. Remove existing JS sync code.

**Reference existing JS:** `webrtc-manager.js`, `signaling-client.js`, `collab-manager.js`, `presence.js` - these will be replaced by C++ calls.

- [ ] 8a: Remove JS sync implementation
  - Delete or deprecate `webrtc-manager.js`, `signaling-client.js`, `collab-manager.js`
  - Keep `presence.js` UI rendering, but source data from C++

- [ ] 8b: Update worker.js to forward sync to C++ engine
  - Forward enableSync/disableSync to WASM
  - Relay sync state changes to main thread

- [ ] 8c: Update client.js with thin sync wrapper
  - `enableSync(url)`, `disableSync()`
  - `onSyncStateChanged(callback)`
  - `onPeersChanged(callback)`
  - `onPresenceChanged(callback)`

- [ ] 8d: Update presence UI to use C++ presence data
  - Receive cursor/selection updates via listener
  - Render remote cursors (existing rendering code)

- [ ] 8e: Handle sync state in UI state machine
  - Add SYNCING context flag
  - Visual indicator for sync status

---

## Phase 9: Build System Updates

Configure Bazel for cross-platform network code including WebRTC.

- [ ] 9a: Create `core/net/BUILD` with platform-specific `select()`
  - Common sources always included
  - Web sources for WASM target (emscripten WebRTC bindings)
  - Apple sources with WebRTC.framework dependency

- [ ] 9b: Add stasel/WebRTC as external dependency for Apple
  - Use [stasel/WebRTC](https://github.com/stasel/WebRTC) prebuilt XCFramework
  - For Bazel: download xcframework and configure as `apple_static_xcframework_import`
  - Alternative: use CocoaPods/SPM in a separate Xcode project, link via Bazel
  - License: BSD 3-Clause (compatible)

- [ ] 9c: Update `apps/wasm/BUILD` to link net library
  - Add dependency on `//core/net:net`
  - Enable emscripten WebRTC and fetch flags

- [ ] 9d: Add platform detection defines
  - `__CELLS_PLATFORM_WASM` for web
  - `__CELLS_PLATFORM_APPLE` for iOS/macOS
  - Consistent with xptools pattern

- [ ] 9e: Add integration tests for network layer
  - Test targets for each platform
  - Mock signaling server for testing

---

## Phase 10: Documentation

Document the new architecture.

- [ ] 10a: Update `docs/networking.md` with C++ implementation status
  - Mark components as implemented in C++
  - Document C++ API

- [ ] 10b: Create `docs/sync-protocol.md` with wire format details
  - Message types and JSON format
  - Sync flow diagrams

- [ ] 10c: Update README.md with sync feature description
  - How to enable sync
  - Supported platforms

- [ ] 10d: Add inline code documentation
  - Header file doc comments
  - Key algorithm explanations

---

## Phase 11: CLI Sync Observer

Add a CLI command to join a room and log all operations - pure C++ client without UI.

- [ ] 11a: Add `sync` subcommand to CLI (`apps/cli/main.cc`)
  - `cells sync <url>` (full URL copied from web UI, includes room ID)
  - Parse URL to extract server and room ID
  - Connect via WebRTC (using C++ implementation)
  - Print connection status to stdout

- [ ] 11b: Implement operation logging callback
  - On each received operation, print JSON to stdout
  - Include: operation type, HLC timestamp, cell/axis ID, value
  - Format: one JSON object per line (NDJSON)

- [ ] 11c: Add optional `--apply` flag to apply operations to a workbook
  - `cells sync <url> --apply <file.zcd>`
  - Load workbook, apply incoming operations, save on exit
  - Useful for testing conflict resolution

- [ ] 11d: Add `--send` flag for testing outbound operations
  - `cells sync <url> --send <file.zcd>`
  - Load workbook, broadcast all cells as operations
  - Other clients receive and can verify

- [ ] 11e: Handle graceful shutdown (Ctrl+C)
  - Leave room cleanly
  - Print summary: operations received, peers seen

**Example usage:**
```bash
# Terminal 1: Start observer (copy URL from web UI share button)
cells sync "wss://cells.example.com/room/abc123"
# Output:
# Connected to wss://cells.example.com
# Joined room: abc123
# Peer joined: def456
# {"op":"CELL_SET_VALUE","hlc":"1703...","cell":"N3f8hJ2w","value":"Hello"}
# {"op":"CELL_SET_VALUE","hlc":"1703...","cell":"K9x2mP4q","value":"=A1+1"}

# Terminal 2: Web UI edits cells, observer logs them
```

---

## Summary

| Phase | Focus | Key Files |
|-------|-------|-----------|
| 1 | Directory Structure | `core/net/` tree |
| 2 | HTTP Abstraction | `HttpRequest.h`, platform `.cc`/`.mm` |
| 3 | WebSocket Abstraction | `Connection.h`, `WSConnection.h`, platform impls |
| 4 | WebRTC Abstraction | `RTCPeerConnection.h`, `RTCDataChannel.h`, platform impls |
| 5 | Signaling | `SignalingClient.h`, offer/answer/ICE protocol |
| 6 | Sync Protocol | `SyncClient.h`, `Operation.h`, existing `hlc.h` |
| 6.5 | Presence | `Presence.h`, cursor/selection broadcast |
| 7 | Engine Integration | `CellsEngine` sync methods, bindings |
| 8 | JavaScript Updates | Remove JS sync, thin wrapper to C++ |
| 9 | Build System | Bazel configs, stasel/WebRTC xcframework |
| 10 | Documentation | Updated docs |
| 11 | CLI Sync Observer | `apps/cli/main.cc` sync subcommand |

**Dependencies:**
- Phase 2-3 can be done in parallel
- Phase 4 depends on Phase 3 (signaling over WebSocket)
- Phase 5 depends on Phase 3-4
- Phase 6 depends on Phase 4-5 (uses DataChannel)
- Phase 6.5 depends on Phase 6 (uses SyncClient's DataChannel)
- Phase 7 depends on Phase 6 and 6.5
- Phase 8 depends on Phase 7
- Phase 9 can start after Phase 1, iterate as needed
- Phase 10 can be done incrementally
- Phase 11 depends on Phase 6 (needs sync protocol), can skip Phase 6.5-8

**Out of Scope (for now):**
- Android support (can add later with JNI)
- Windows support (can add with WinHTTP/WinSock)
- Authentication (room passwords, user tokens)
- Binary wire format (using JSON initially)
