# Linux Networking Implementation ✅

Implement HTTP and WebSocket support for Linux/native platforms.

**Status: Complete.** HTTP (libcurl) and WebSocket (libdatachannel) are working on Linux/glibc. End-to-end sync verified. Alpine/musl networking not feasible due to `rules_perl` glibc dependency.

## Background

The networking abstraction in `/core/net/` has platform-specific implementations:
- **Emscripten**: Uses browser APIs (`emscripten_fetch`, native WebSocket)
- **Apple (iOS/macOS)**: Uses `NSURLSession` and `NSURLSessionWebSocketTask`
- **Linux/Native**: HTTP via libcurl, WebSocket/WebRTC via libdatachannel

## Phase 1: HTTP Implementation with libcurl

- [x] 1a: Add libcurl as a Bazel dependency (http_archive or system library) - Added `curl` 8.11.0.bcr.4 from BCR
- [x] 1b: Create `core/net/native/HttpRequest_curl.cc` implementing the HttpRequest interface
  - Implement `_sendAsync()` for regular requests
  - Implement `_sendAsyncStreaming()` for streaming responses
  - Implement `_cancel()` for request cancellation
  - Handle headers, body, and response codes
- [x] 1c: Update `core/net/BUILD` to use curl implementation on Linux
  - Add config_setting for Linux if not present
  - Update http_request deps select() to use curl on Linux

## Phase 2: WebSocket Implementation

- [x] 2a: Evaluate and add WebSocket library - Using libdatachannel which is already a dependency (for WebRTC). It includes WebSocket support via `rtc::WebSocket`.
- [x] 2b: Create `core/net/native/WSConnection_libdc.cc` implementing the WSConnection interface
  - Implement `_connect()` for establishing connection
  - Implement `send()` for sending messages
  - Implement `close()` for graceful disconnect
  - Handle `onOpen`, `onMessage`, `onError`, `onClose` callbacks
- [x] 2c: Update `core/net/BUILD` to use WebSocket implementation on Linux - Added `ws_connection_libdc` target and updated select() to use it on Linux

## Phase 3: Integration and Testing

- [x] 3a: Build and test on Linux (glibc) - Successfully built using Docker with Debian testing (trixie). Fixed several Linux-specific issues: missing `<cstddef>` in oplog.cc, missing `<cstring>` in libdatachannel implementations, `std::byte` to `uint8_t` conversion, and `alwayslink` for ws_connection_libdc linking.
- [x] 3b: Verify sync command works end-to-end on Linux - Created `scripts/linux-sync-test.sh` which builds the CLI, starts a signaling server, and verifies the CLI can connect and reach ONLINE state. Test passes: WebSocket connects, WebRTC signaling works.
- [x] 3c: (Optional) Evaluate Alpine/musl compatibility - **Not currently feasible.** The full `cells` target fails to build on Alpine/musl because the OpenSSL BCR package uses `rules_perl` to generate assembly files, and `rules_perl` downloads glibc-linked Perl binaries from [relocatable-perl](https://github.com/skaji/relocatable-perl) that cannot execute on musl (error: "cannot execute: required file not found" due to missing glibc dynamic linker). Workarounds considered:
  - **Use system Perl**: Would require patching the OpenSSL BCR overlay's `perl_genrule.bzl` to use system Perl instead of the `rules_perl` toolchain — invasive and fragile.
  - **Use system OpenSSL (`openssl-dev`)**: Would require replacing `@openssl` BCR dependency with a `cc_library` wrapping system headers/libs via `rules_foreign_cc` — significant build system changes.
  - **Build OpenSSL with `no-asm`**: Would skip Perl-based assembly generation entirely, but the BCR package doesn't expose this option and would need a custom overlay.
  - **Conclusion**: The Alpine build continues to use `cells-converter` (no networking). Full networking on Alpine would require either upstream `rules_perl` musl support or switching to a system OpenSSL approach. Neither is worth the complexity for now.

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
