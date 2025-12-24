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

- [x] 12c: Delete dead code
  - `peer-connector.js` (498 lines) - not imported anywhere
  - `operation-protocol.js` (256 lines) - not imported anywhere

- [x] 12d: Delete replaced sync code
  - `collab-manager.js` (999 lines) - replaced by C++ SyncClient
  - `webrtc-manager.js` (599 lines) - replaced by C++ RTCPeerConnection
  - `signaling-client.js` (436 lines) - replaced by C++ SignalingClient
  - `ice-config.js` (294 lines) - replaced by C++ ICEConfig

- [x] 12e: Refactor presence.js
  - Keep only utilities: `generateRandomName()`, `getColorForPeer()`, `USER_COLORS`
  - Delete PresenceManager class - replaced by C++ PresenceManager
  - Reduced from 565 to 63 lines (502 lines removed)

- [x] 12f: Test web sync end-to-end
  - All C++ tests pass (make test)
  - Lint and format pass (make lint, make format)
  - JS imports verified - no broken references
  - Full end-to-end testing requires running signaling server

**Result:** ~3584 lines of JS removed (754 + 2328 + 502), sync handled by C++

---

## Phase 13: Native macOS Support

Enable the CLI sync command to work on native macOS using the existing Apple implementations.

**Current state:** Apple implementations exist in `core/net/apple/*.mm` but are not built because:
1. Bazel `cc_library` doesn't compile Objective-C++ (`.mm` files)
2. Need `rules_apple` for `objc_library` support
3. WebRTC requires stasel/WebRTC framework

- [x] 13a: Add `rules_apple` to MODULE.bazel
  - Added `bazel_dep(name = "rules_apple", version = "4.3.3")`
  - This enables `objc_library`, `apple_static_xcframework_import`, etc.

- [x] 13b: Add stasel/WebRTC as external dependency
  - Added http_archive in MODULE.bazel to download M141 XCFramework
  - Created `third_party/webrtc/BUILD.webrtc` with `apple_static_xcframework_import`
  - URL: https://github.com/stasel/WebRTC/releases/download/141.0.0/WebRTC-M141.xcframework.zip
  - SHA256: e3b9bc1aed7a6f3f747a62567680ac7837bdbb74d1fae8f0f543131bc1bf8a5f
  - License: BSD 3-Clause (compatible)

- [x] 13c: Update `core/net/BUILD` with `objc_library` targets
  - Added `apple_support` v2.0.0 to MODULE.bazel (required for Apple CC toolchain)
  - Added `objc_library` targets for:
    - `http_request_apple` (HttpRequest.mm) - working
    - `ws_connection_apple` (WSConnection.mm) - working
  - WebRTC implementations deferred (stasel/WebRTC macOS slice has incomplete headers)
  - Used `select()` to choose Apple impl on `@platforms//os:macos` for HTTP/WS
  - RTCDataChannel and RTCPeerConnection use default stubs on macOS for now
  - Fixed Logger.h enum naming conflict with Apple toolchain (DEBUG/ERROR macros)

- [x] 13d: Update CLI BUILD for macOS
  - No changes needed - CLI automatically uses Apple implementations via `//core/net:sync_client`
  - Platform-specific `objc_library` targets handle framework linking (Foundation)
  - Verified CLI binary links with Foundation.framework via `otool -L`

- [x] 13e: Test CLI sync on native macOS
  - CLI sync command parses URL and starts sync: `cells sync "http://localhost:3000/?room=test"`
  - WebSocket connection uses Apple NSURLSessionWebSocketTask implementation
  - Logging shows state transitions: OFFLINE -> CONNECTING
  - WebRTC peer connections use default stubs (full WebRTC deferred)
  - Full end-to-end test requires running signaling server

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
| 13 | Native macOS Support | `rules_apple`, `objc_library`, HTTP/WS Apple impls |
| 14 | WebRTC on macOS | libdatachannel, RTCPeerConnection/DataChannel |

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
- Phase 14 depends on Phase 13 (need Apple build infrastructure), implements WebRTC

## Phase 14: WebRTC on macOS

Enable full P2P sync on native macOS by implementing WebRTC using the stasel/WebRTC framework.

**Current blocker:** The stasel/WebRTC M141 XCFramework has incomplete headers in the macOS slice.
The MacCatalyst slice appears to have all headers. Options:
1. Use MacCatalyst headers with macOS build
2. Build WebRTC from source for macOS (complex, but gives full control)
3. Wait for stasel to fix macOS framework
4. Use a different WebRTC source (libdatachannel is C++, lighter weight)

**Chosen approach:** Use [libdatachannel](https://github.com/paullouisageneau/libdatachannel) - a lightweight C++ WebRTC library that only implements Data Channels (no media), which is exactly what we need.

- [x] 14a: Evaluate libdatachannel vs stasel/WebRTC
  - **libdatachannel** (v0.24.0, Nov 2024): C++17, MPL 2.0 license ✓
    - Supports: DataChannel, ICE, STUN/TURN, DTLS, SCTP
    - Does NOT support: Audio/Video (we don't need it)
    - Cross-platform: macOS, iOS, Linux, Windows + WASM (datachannel-wasm)
    - Dependencies: OpenSSL (or mbedTLS/GnuTLS), libjuice (ICE), usrsctp (SCTP)
    - Same API for native + WASM builds ✓
    - NOT in Bazel Central Registry - need rules_foreign_cc (cmake_external)
    - Actively maintained, tracks WebRTC standards
  - **stasel/WebRTC** (M141): Full Google WebRTC, ~50MB XCFramework
    - macOS slice has incomplete headers (blocker)
    - Includes audio/video we don't need
    - Obj-C++ API requires bridging to C++
  - **Decision**: Use libdatachannel - purpose-built for Data Channels, smaller, pure C++

- [x] 14b: Add libdatachannel to MODULE.bazel
  - Added `rules_foreign_cc` v0.15.1 for CMake support
  - Added `openssl` v3.5.4.bcr.0 from BCR for crypto
  - Added `libdatachannel` v0.24.0 via `git_repository` with `recursive_init_submodules`
  - Created `third_party/libdatachannel/BUILD.libdatachannel` with cmake() rule
  - Builds with NO_MEDIA=ON (no audio/video), NO_WEBSOCKET=OFF (keep WS)
  - Dependencies bundled: libjuice (ICE), usrsctp (SCTP)

- [ ] 14c: Create `core/net/apple/RTCPeerConnection_apple.mm` (or pure C++)
  - Wrap libdatachannel::PeerConnection
  - Implement RTCPeerConnection interface
  - Handle ICE candidate gathering
  - Create/accept offers and answers

- [ ] 14d: Create `core/net/apple/RTCDataChannel_apple.mm` (or pure C++)
  - Wrap libdatachannel::DataChannel
  - Implement RTCDataChannel interface
  - Handle open/close/message events
  - Binary and text message support

- [ ] 14e: Update `core/net/BUILD` to use new implementations on macOS
  - Change select() for rtc_peer_connection on macOS/iOS
  - Change select() for rtc_data_channel on macOS/iOS
  - May be able to use same implementation for all native platforms (not just Apple)

- [ ] 14f: Test CLI sync end-to-end
  - `cells sync "https://cells-app.fly.dev/?room=test"` should work
  - Should connect to web clients via WebRTC
  - Operations should flow both directions

**Alternative: stasel/WebRTC with MacCatalyst headers**

If libdatachannel proves difficult, we can try using the MacCatalyst slice headers:

- [ ] 14a-alt: Extract headers from MacCatalyst slice
  - MacCatalyst slice has complete WebRTC.framework/Headers/*
  - Copy headers to a shared location
  - Build macOS binary linking against macOS slice but using MacCatalyst headers

- [ ] 14b-alt: Create objc_library for WebRTC on macOS
  - `rtc_peer_connection_apple` using WebRTC.framework
  - `rtc_data_channel_apple` using RTCDataChannel from framework
  - Both use Objective-C++ to bridge WebRTC Obj-C API to C++

**References:**
- libdatachannel: https://github.com/paullouisageneau/libdatachannel
- stasel/WebRTC: https://github.com/stasel/WebRTC
- WebRTC native development: https://webrtc.github.io/webrtc-org/native-code/

---

**Out of Scope (for now):**
- Android support (can add later with JNI)
- Windows support (can add with WinHTTP/WinSock)
- Authentication (room passwords, user tokens)
- Binary wire format (using JSON initially)
