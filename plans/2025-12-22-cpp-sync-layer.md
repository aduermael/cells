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

- [x] 1a: Create `core/net/` directory with BUILD file for Bazel
- [x] 1b: Create `core/net/include/` for public headers
- [x] 1c: Create `core/net/common/` for shared implementation
- [x] 1d: Create `core/net/web/` for emscripten/WASM implementations
- [x] 1e: Create `core/net/apple/` for iOS/macOS implementations (Objective-C++)

---

## Phase 2: HTTP Request Abstraction

Port the HttpRequest pattern from xptools. Abstract interface with platform-specific implementations.

- [x] 2a: Create `HttpRequest.h` interface in `core/net/include/`
  - Status enum (WAITING, PROCESSING, FAILED, CANCELLED, DONE)
  - Factory method `make(method, host, port, path, queryParams, secure)`
  - `sendAsync()`, `sendSync()`, `cancel()`
  - `getResponse()` returning HttpResponse
  - Callback mechanism (`setCallback(HttpRequestCallback)`)
  - Platform hooks: `_sendAsync()`, `_cancel()`, `_attachPlatformObject()`, `_detachPlatformObject()`

- [x] 2b: Create `HttpResponse.h` in `core/net/include/`
  - Status code, headers, body bytes
  - `appendBytes()`, `readAllBytes()`
  - Success/failure state

- [x] 2c: Create `URL.h` utility class for URL parsing
  - scheme, host, port, path, query components
  - URL string construction

- [x] 2d: Create common HttpRequest implementation in `core/net/common/HttpRequest.cc`
  - Shared logic (callback handling, status management)
  - Calls platform-specific `_sendAsync()`

- [x] 2e: Create Web implementation in `core/net/web/HttpRequest_web.cc`
  - Use `emscripten_fetch` API
  - Handle async callbacks via emscripten

- [x] 2f: Create Apple implementation in `core/net/apple/HttpRequest.mm`
  - Use NSURLSession with NSURLSessionDataDelegate
  - NetworkManager singleton pattern (like xptools)
  - ARC bridging with `__bridge_retained`/`__bridge`

---

## Phase 3: WebSocket Connection Abstraction

Port the WSConnection pattern from xptools for real-time sync.

- [x] 3a: Create `Connection.h` base class in `core/net/include/`
  - Status enum (IDLE, OK, CLOSED_ON_ERROR, CLOSED)
  - Payload class for framing (with metadata support)
  - ConnectionDelegate interface: `connectionDidEstablish()`, `connectionDidReceive()`, `connectionDidClose()`
  - Abstract methods: `connect()`, `reset()`, `close()`, `pushPayloadToWrite()`

- [x] 3b: Create `WSConnection.h` in `core/net/include/`
  - Extends Connection
  - Factory method `make(url)` or `make(scheme, addr, port, path)`
  - WebSocket-specific: `getURL()`, `getHost()`, `getPort()`, `getPath()`, `getSecure()`
  - Platform hooks: `_init()`, `_connect()`, `_writePayload()`, `_close()`, `_destroy()`

- [x] 3c: Create common WSConnection implementation in `core/net/common/WSConnection.cc`
  - Shared logic (payload queue, received bytes buffer, status management)
  - Write buffering with fragmentation support

- [x] 3d: Create Web implementation in `core/net/web/WSConnection_web.cc`
  - Use `emscripten_websocket_*` APIs
  - Callbacks: onopen, onerror, onclose, onmessage
  - Binary message support

- [x] 3e: Create Apple implementation in `core/net/apple/WSConnection.mm`
  - Use NSURLSessionWebSocketTask
  - WebSocketConnection Obj-C wrapper class
  - Handle open/close/message delegates
  - Dispatch to main queue for thread safety

---

## Phase 4: WebRTC Abstraction

Port WebRTC P2P from JS (`webrtc-manager.js`, `ice-config.js`).

- [x] 4a: Create `RTCPeerConnection.h` in `core/net/include/`
  - `createOffer()`, `createAnswer()`, `setLocalDescription()`, `setRemoteDescription()`
  - `addIceCandidate()`, `onIceCandidate` callback
  - `createDataChannel()`, `onDataChannel` callback
  - RTCPeerConnectionDelegate: `onConnectionStateChange()`, `onIceConnectionStateChange()`

- [x] 4b: Create `RTCDataChannel.h` in `core/net/include/`
  - `send(data)`, `close()`
  - DataChannelDelegate: `onOpen()`, `onClose()`, `onMessage()`, `onError()`
  - Binary and text message support

- [x] 4c: Create `ICEConfig.h` for STUN/TURN configuration
  - ICE server list (URLs, credentials)
  - Load from `ice-config.js` equivalent

- [x] 4d: Create Web implementation in `core/net/web/RTCPeerConnection_web.cc`
  - Use emscripten WebRTC bindings
  - Map to browser's RTCPeerConnection API

- [x] 4e: Create Apple implementation in `core/net/apple/RTCPeerConnection.mm`
  - Use [stasel/WebRTC](https://github.com/stasel/WebRTC) prebuilt XCFramework
  - Install via Swift Package Manager or CocoaPods (`pod 'WebRTC-lib'`)
  - Supports iOS 12+, macOS 10.11+, macOS Catalyst (arm64 + x86_64)
  - Actively maintained, tracks Chromium releases (currently M141)

- [x] 4f: Add WebRTC tests
  - Loopback test (connect to self)
  - DataChannel send/receive

---

## Phase 5: Signaling Protocol

Connect to existing Go server for WebRTC signaling (offer/answer/ICE exchange).

**Reference existing JS:** Study `signaling-client.js` to ensure wire-compatibility.

- [x] 5a: Create `SignalingClient.h` in `core/net/include/`
  - `connect(serverUrl)`, `disconnect()`
  - `joinRoom(roomId)`, `leaveRoom()`
  - `sendOffer(peerId, sdp)`, `sendAnswer(peerId, sdp)`, `sendIceCandidate(peerId, candidate)`
  - SignalingDelegate: `onPeerJoined()`, `onPeerLeft()`, `onOffer()`, `onAnswer()`, `onIceCandidate()`

- [x] 5b: Create `SignalingClient.cc` implementation in `core/net/common/`
  - Uses WSConnection for server communication
  - JSON message protocol matching existing JS
  - Reconnection logic with exponential backoff

- [x] 5c: Define signaling message protocol in `core/net/include/SignalingProtocol.h`
  - Message types: JOIN, LEAVE, OFFER, ANSWER, ICE_CANDIDATE, PEER_JOINED, PEER_LEFT
  - JSON serialization helpers

- [x] 5d: Add signaling tests in `core/net/signaling_test.cc`
  - Mock WSConnection for unit testing
  - Test join/leave flow, offer/answer exchange

---

## Phase 6: Sync Protocol Layer

Port sync protocol from JS (`collab-manager.js`). Build on CRDT docs.

**Reference existing JS:** Study `collab-manager.js` for message format and sync flow.

- [x] 6a: Create `SyncClient.h` in `core/net/include/`
  - `startSync(roomId, peerId)`, `stopSync()`
  - `broadcastOperations()` - queue local operations for sync
  - SyncClientDelegate: `syncClientStateDidChange()`, `syncClientPeerDidChange()`, `syncClientDataDidChange()`
  - Orchestrates SignalingClient + RTCPeerConnection + SyncManager

- [x] 6b: `Operation.h` already exists in `core/cells/operation.h`
  - HLC (Hybrid Logical Clock) for ordering
  - Operation types: CELL_SET_VALUE, CELL_CLEAR, DIM_INSERT_AXIS, etc.
  - JSON serialization for wire format

- [x] 6c: Create `SyncClient.cc` implementation in `core/net/common/`
  - Uses SignalingClient for connection setup
  - Uses RTCDataChannel for P2P data exchange
  - Uses SyncManager from `core/cells/` for sync protocol logic
  - Handles peer lifecycle (connect, ready, disconnect)

- [x] 6d: `OperationLog` already exists in `core/cells/oplog.h`
  - Append-only log of operations
  - Indexes by entity_id
  - Compaction support via `pruneOperationsBefore()`

- [x] 6e: HLC already integrated in `core/cells/`
  - `HLC` struct with wall_time, logical, node_id
  - Comparison operators, string serialization
  - Functions: `generate_hlc()`, `update_hlc()`, `generate_initial_hlc()`
  - SyncManager uses HLC for all operations

---

## Phase 6.5: Presence Abstraction

Handle cursor positions, selections, and user presence. Presence data is ephemeral and never affects the Workbook (only Operations can mutate Workbook state).

- [x] 6.5a: Create `Presence.h` in `core/net/include/`
  - `PresenceData` struct: user_id, cursor_cell, selection_range, color, name
  - `PresenceManager` class: tracks all peers' presence
  - `updateLocalPresence(PresenceData)` - called when local user moves cursor/selection
  - `getRemotePresences()` - returns map of peer_id -> PresenceData
  - PresenceDelegate: `onPresenceChanged(peer_id, PresenceData)`

- [x] 6.5b: Create `Presence.cc` implementation in `core/net/common/`
  - Broadcast local presence changes via DataChannel
  - Receive and store remote presence updates
  - Automatic cleanup when peer disconnects
  - Throttle outbound updates (e.g., max 30/sec)

- [x] 6.5c: Integrate with SyncClient
  - SyncClient owns PresenceManager
  - Presence messages use same DataChannel as operations
  - Separate message type (PRESENCE vs OPERATION)

- [ ] 6.5d: Wire JS presence events to C++ (deferred to Phase 8)
  - JS calls `updatePresence(cell, selection)` on cursor move
  - C++ broadcasts to peers, receives from peers
  - JS receives remote presence via listener callback
  - Existing `presence.js` renders remote cursors (unchanged)

---

## Phase 7: Integration with CellsEngine

Connect the sync layer to the existing engine.

- [x] 7a: Add SyncClient to CellsEngine
  - `enableSync(serverUrl, documentId)`, `disableSync()`
  - `getSyncState()` - connected, syncing, offline

- [x] 7b: Hook mutations to generate operations
  - In updateCell, createCell, resizeColumn, etc.
  - Create Operation with HLC timestamp
  - Push to SyncClient

- [x] 7c: Apply received operations to workbook
  - SyncClient calls back into engine with remote operations
  - Apply with conflict resolution (LWW for cells)
  - Skip if operation already applied (dedup by HLC)

- [x] 7d: Add sync-related callbacks to listener system
  - SYNC_STATE_CHANGED, PEER_JOINED, PEER_LEFT
  - Extend existing onDataChanged mechanism

- [x] 7e: Update WASM bindings (bindings.cc)
  - Expose sync methods: `enableSync()`, `disableSync()`, `getSyncState()`
  - Add sync event callbacks via existing listener pattern

---

## Phase 8: Update JavaScript Layer

Simplify JS to thin wrapper - sync happens in C++. Remove existing JS sync code.

**Reference existing JS:** `webrtc-manager.js`, `signaling-client.js`, `collab-manager.js`, `presence.js` - these will be replaced by C++ calls.

- [x] 8a: Remove JS sync implementation
  - Delete or deprecate `webrtc-manager.js`, `signaling-client.js`, `collab-manager.js`
  - Keep `presence.js` UI rendering, but source data from C++

- [x] 8b: Update worker.js to forward sync to C++ engine
  - Forward enableSync/disableSync to WASM
  - Relay sync state changes to main thread

- [x] 8c: Update client.js with thin sync wrapper
  - `enableSync(url)`, `disableSync()`
  - `onSyncStateChanged(callback)`
  - `onPeersChanged(callback)`
  - `onPresenceChanged(callback)`

- [x] 8d: Update presence UI to use C++ presence data
  - Created cpp-sync-adapter.js - thin JS adapter wrapping C++ SyncClient
  - Provides CollabManager-compatible interface for existing UI
  - Polls C++ for sync state and presence updates
  - Updated collab-ui.js to import CollabState from new adapter

- [x] 8e: Handle sync state in UI state machine
  - Added sync context to ui-state.js (enabled, state, peerCount)
  - Added setSyncContext(), getSyncContext(), isSyncEnabled(), isSyncing(), isSyncOnline()
  - Added onSyncChange() listener for sync context changes
  - Included sync in getContext() for full state snapshot
  - Visual indicator already handled by collab-ui.js status dot

---

## Phase 9: Build System Updates

Configure Bazel for cross-platform network code including WebRTC.

- [x] 9a: Create `core/net/BUILD` with platform-specific `select()`
  - Common sources always included
  - Web sources for WASM target via `@platforms//os:emscripten` select
  - Apple sources with TODO comments for when rules_apple is added
  - Default stub implementations for non-web/non-apple platforms

- [~] 9b: Add stasel/WebRTC as external dependency for Apple (DEFERRED)
  - Requires rules_apple for objc_library support
  - Use [stasel/WebRTC](https://github.com/stasel/WebRTC) prebuilt XCFramework
  - For Bazel: download xcframework and configure as `apple_static_xcframework_import`
  - License: BSD 3-Clause (compatible)
  - **Deferred**: Focus on web platform first; Apple can be added when building native apps

- [x] 9c: Update `apps/wasm/BUILD` to link net library
  - Added dependency on `//core/net:sync_client`
  - Enabled `-lwebsocket.js` and `-s FETCH=1` for networking

- [x] 9d: Add platform detection defines
  - Using standard `__APPLE__` define for iOS/macOS (already available)
  - Using `__EMSCRIPTEN__` define for web (already available from emscripten)
  - No need for custom `__CELLS_PLATFORM_*` defines - standard platform macros work

- [x] 9e: Add integration tests for network layer
  - `core/net/url_test.cc` - URL parsing tests
  - `core/net/http_response_test.cc` - HTTP response tests
  - `core/net/ice_config_test.cc` - ICE configuration tests
  - `core/net/rtc_test.cc` - WebRTC abstraction tests
  - `core/net/signaling_test.cc` - Signaling protocol tests
  - `core/net/presence_test.cc` - Presence manager tests

---

## Phase 10: Documentation

Document the new architecture.

- [x] 10a: Update `docs/networking.md` with C++ implementation status
  - Mark components as implemented in C++
  - Document C++ API (SyncClient, SyncClientDelegate, PresenceManager)

- [x] 10b: Create `docs/sync-protocol.md` with wire format details
  - Message types and JSON format for signaling and sync
  - Sync flow diagrams
  - Operation format and HLC documentation
  - Presence protocol

- [x] 10c: Update README.md with sync feature description
  - Added Real-time Collaboration section
  - Updated Not Yet Implemented section
  - Added networking/CRDT/presence to Implemented section

- [x] 10d: Add inline code documentation
  - Header files already have comprehensive documentation
  - All delegate methods documented
  - All enums documented with comments

---

## Phase 11: CLI Sync Observer

Add a CLI command to join a room and log all operations - pure C++ client without UI.

- [x] 11a: Add `sync` subcommand to CLI (`apps/cli/main.cc`)
  - `cells sync <url>` (HTTP URL copied from web UI, e.g., `https://cells.example.com/?room=abc123`)
  - Parse URL to extract host and room ID from query string or path
  - Construct WebSocket signaling URL internally (e.g., `wss://host/ws`)
  - Connect via WebRTC (using C++ implementation)
  - Print connection status to stdout

- [x] 11b: Implement operation logging callback
  - SyncLogger delegate class logs state changes and data changes
  - Tracks peers seen and operations received
  - Prints summary on exit

- [x] 11c: Add optional `--apply` flag to apply operations to a workbook
  - `cells sync <url> --apply <file.zcd>`
  - Load workbook, apply incoming operations, save on exit
  - Useful for testing conflict resolution

- [x] 11d: Add `--send` flag for testing outbound operations
  - `cells sync <url> --send <file.zcd>`
  - Load workbook, broadcast all cells as operations
  - Other clients receive and can verify

- [x] 11e: Handle graceful shutdown (Ctrl+C)
  - Leave room cleanly
  - Print summary: operations received, peers seen

**Note:** Currently uses stub networking implementations on native macOS. Full WebRTC functionality
will work once Apple-specific builds (rules_apple) are integrated or when running in the browser.

**Example usage:**
```bash
# Terminal 1: Start observer (copy URL from web UI address bar)
cells sync "https://cells.example.com/?room=abc123"
# Output:
# Connecting to wss://cells.example.com/ws...
# Joined room: abc123
# Peer joined: def456
# {"op":"CELL_SET_VALUE","hlc":"1703...","cell":"N3f8hJ2w","value":"Hello"}
# {"op":"CELL_SET_VALUE","hlc":"1703...","cell":"K9x2mP4q","value":"=A1+1"}

# Terminal 2: Web UI edits cells, observer logs them
```

---

## Phase 12: JavaScript Cleanup

Remove old JS sync implementation and properly wire C++ sync to web UI.

**Current problem:** Phase 8 created `cpp-sync-adapter.js` but didn't actually switch to C++ sync.
The old JS code (`collab-manager.js`, `webrtc-manager.js`, etc.) is still handling all sync.

- [x] 12a: Wire C++ sync in index.html
  - Remove old JS imports: `CollabManager`, `SignalingClient`, `WebRTCManager`, `PresenceManager`
  - Use C++ sync via CppSyncAdapter (which wraps worker messages)
  - syncAdapter replaces both collabManager and presenceManager

- [x] 12b: Simplify cpp-sync-adapter.js
  - Added architecture documentation explaining polling approach
  - Polling is intentional (10-20 Hz) - event-driven would require C++ push infrastructure
  - Removed unused USER_COLORS import

- [ ] 12c: Delete dead code
  - `peer-connector.js` (498 lines) - not imported anywhere
  - `operation-protocol.js` (256 lines) - not imported anywhere

- [ ] 12d: Delete replaced sync code
  - `collab-manager.js` (999 lines) - replaced by C++ SyncClient
  - `webrtc-manager.js` (599 lines) - replaced by C++ RTCPeerConnection
  - `signaling-client.js` (436 lines) - replaced by C++ SignalingClient
  - `ice-config.js` (294 lines) - replaced by C++ ICEConfig

- [ ] 12e: Refactor presence.js
  - Keep only `generateRandomName()` utility (move to utils.js)
  - Delete rest - replaced by C++ PresenceManager

- [ ] 12f: Test web sync end-to-end
  - Verify C++ sync works via WASM
  - Test multi-peer collaboration
  - Verify presence updates work

**Expected result:** ~3000+ lines of JS removed, sync handled by C++

---

## Phase 13: Native macOS Support

Enable the CLI sync command to work on native macOS using the existing Apple implementations.

**Current state:** Apple implementations exist in `core/net/apple/*.mm` but are not built because:
1. Bazel `cc_library` doesn't compile Objective-C++ (`.mm` files)
2. Need `rules_apple` for `objc_library` support
3. WebRTC requires stasel/WebRTC framework

- [ ] 13a: Add `rules_apple` to MODULE.bazel
  - Add `bazel_dep(name = "rules_apple", version = "...")`
  - This enables `objc_library`, `apple_static_xcframework_import`, etc.

- [ ] 13b: Add stasel/WebRTC as external dependency
  - Download prebuilt XCFramework from [stasel/WebRTC](https://github.com/stasel/WebRTC/releases)
  - Add to `third_party/webrtc/` or configure as http_archive
  - Create BUILD file with `apple_static_xcframework_import`

- [ ] 13c: Update `core/net/BUILD` with `objc_library` targets
  - Create `objc_library` for each Apple implementation:
    - `http_request_apple` (HttpRequest.mm)
    - `ws_connection_apple` (WSConnection.mm)
    - `rtc_peer_connection_apple` (RTCPeerConnection.mm)
    - `rtc_data_channel_apple` (RTCDataChannel.mm)
  - Link against Foundation, Network, and WebRTC frameworks
  - Use `select()` to choose Apple impl on `@platforms//os:macos`

- [ ] 13d: Update CLI BUILD for macOS
  - Add conditional dependency on Apple networking libraries
  - Ensure proper framework linking (Foundation, Network, WebRTC)

- [ ] 13e: Test CLI sync on native macOS
  - `cells sync "http://localhost:3000/?room=test"` should connect
  - Verify WebSocket connection works
  - Verify WebRTC peer connections work
  - Test with web UI running locally

**Reference:**
- stasel/WebRTC: BSD 3-Clause license, supports macOS 10.11+, arm64 + x86_64
- Current release: M141 (tracks Chromium)

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
| 8 | JavaScript Updates | Added cpp-sync-adapter.js (incomplete) |
| 9 | Build System | Bazel configs, stasel/WebRTC xcframework |
| 10 | Documentation | Updated docs |
| 11 | CLI Sync Observer | `apps/cli/main.cc` sync subcommand |
| 12 | JavaScript Cleanup | Delete old JS sync, wire C++ sync |
| 13 | Native macOS Support | `rules_apple`, `objc_library`, WebRTC framework |

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
- Phase 12 depends on Phase 7 (C++ sync must work), completes Phase 8
- Phase 13 depends on Phase 11 (CLI must exist), uses existing Apple code from Phase 2-4

**Out of Scope (for now):**
- Android support (can add later with JNI)
- Windows support (can add with WinHTTP/WinSock)
- Authentication (room passwords, user tokens)
- Binary wire format (using JSON initially)
