# Linux Networking Implementation

Implement HTTP and WebSocket support for Linux/native platforms.

## Background

The networking abstraction in `/core/net/` has platform-specific implementations:
- **Emscripten**: Uses browser APIs (`emscripten_fetch`, native WebSocket)
- **Apple (iOS/macOS)**: Uses `NSURLSession` and `NSURLSessionWebSocketTask`
- **Linux/Native**: Currently uses stubs that return errors

WebRTC (RTCPeerConnection, RTCDataChannel) already works on Linux via libdatachannel. The missing pieces are HTTP and WebSocket, which are needed for the sync feature to work on Linux.

## Phase 1: HTTP Implementation with libcurl

- [x] 1a: Add libcurl as a Bazel dependency (http_archive or system library) - Added `curl` 8.11.0.bcr.4 from BCR
- [ ] 1b: Create `core/net/native/HttpRequest_curl.cc` implementing the HttpRequest interface
  - Implement `_sendAsync()` for regular requests
  - Implement `_sendAsyncStreaming()` for streaming responses
  - Implement `_cancel()` for request cancellation
  - Handle headers, body, and response codes
- [ ] 1c: Update `core/net/BUILD` to use curl implementation on Linux
  - Add config_setting for Linux if not present
  - Update http_request deps select() to use curl on Linux

## Phase 2: WebSocket Implementation

- [ ] 2a: Evaluate and add WebSocket library (libwebsockets, websocketpp, or Boost.Beast)
- [ ] 2b: Create `core/net/native/WSConnection_<lib>.cc` implementing the WSConnection interface
  - Implement `_connect()` for establishing connection
  - Implement `send()` for sending messages
  - Implement `close()` for graceful disconnect
  - Handle `onOpen`, `onMessage`, `onError`, `onClose` callbacks
- [ ] 2c: Update `core/net/BUILD` to use WebSocket implementation on Linux

## Phase 3: Integration and Testing

- [ ] 3a: Build and test on Linux (glibc)
- [ ] 3b: Verify sync command works end-to-end on Linux
- [ ] 3c: (Optional) Evaluate Alpine/musl compatibility

## Technical Notes

### HttpRequest Interface (from `include/HttpRequest.h`)
Key methods to implement:
- `_sendAsync()` - Send request asynchronously
- `_sendAsyncStreaming()` - Send with streaming response callback
- `_cancel()` - Cancel in-flight request
- Call `completeWithSuccess()` or `completeWithError()` when done
- Call `onStreamData()` for streaming responses

### WSConnection Interface (from `include/WSConnection.h`)
Key methods to implement:
- `_connect()` - Establish WebSocket connection
- `send(data)` - Send binary/text message
- `close()` - Close connection
- Callbacks: `onOpen()`, `onMessage()`, `onError()`, `onClose()`

### Library Options

**HTTP:**
- libcurl (recommended) - Most portable, async via multi interface

**WebSocket:**
- libwebsockets - Lightweight C library, widely used
- websocketpp - Header-only C++, depends on Boost.Asio
- Boost.Beast - Full-featured, part of Boost

### Threading Considerations
The networking callbacks need to be thread-safe. The Apple implementation uses `dispatch_async` for main thread handling. The Linux implementation will need similar consideration (possibly using the existing task scheduler in the codebase).
