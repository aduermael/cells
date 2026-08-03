# Cross-platform networking library
# Platform-specific implementations selected via config
#
# Structure:
#   include/  - Public headers
#   common/   - Shared implementation (all platforms)
#   web/      - Emscripten/WASM implementations
#   apple/    - iOS/macOS HTTP/WS (Objective-C++ / Foundation)
#   windows/  - Windows HTTP/WS (WinHTTP OS stack)
#   native/   - libcurl HTTP (Linux), libdatachannel WS (Linux),
#               libdatachannel WebRTC (all native: Linux, macOS, Windows)
#
# Platform matrix:
#   HTTP:       web | Apple Foundation | Linux curl | Windows WinHTTP
#   WebSocket:  web | Apple Foundation | Linux libdc | Windows WinHTTP
#   WebRTC:     web browser | native libdatachannel (//conditions:default)
#
# Apple objc_library targets are only selected on macOS/iOS.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test", "objc_library")

# Config settings for platform selection
config_setting(
    name = "is_macos",
    constraint_values = ["@platforms//os:macos"],
)

config_setting(
    name = "is_ios",
    constraint_values = ["@platforms//os:ios"],
)

config_setting(
    name = "is_linux",
    constraint_values = ["@platforms//os:linux"],
)

config_setting(
    name = "is_windows",
    constraint_values = ["@platforms//os:windows"],
)

# =============================================================================
# Apple platform implementations (Objective-C++)
# These objc_library targets compile .mm files and are only used on Apple platforms.
# =============================================================================

# Only analyzable/buildable on Apple platforms. Without this, `bazel test //core/...`
# on Linux fails analysis: objc_library requires the Apple CC toolchain.
_APPLE_ONLY = select({
    ":is_macos": [],
    ":is_ios": [],
    "//conditions:default": ["@platforms//:incompatible"],
})

objc_library(
    name = "http_request_apple",
    srcs = ["apple/HttpRequest.mm"],
    hdrs = ["include/HttpRequest.h"],
    copts = ["-std=c++17"],
    includes = ["."],
    sdk_frameworks = ["Foundation"],
    target_compatible_with = _APPLE_ONLY,
    deps = [
        ":http_request_common",
        ":http_response",
        ":url",
    ],
)

objc_library(
    name = "ws_connection_apple",
    srcs = ["apple/WSConnection.mm"],
    hdrs = ["include/WSConnection.h"],
    copts = ["-std=c++17"],
    includes = ["."],
    sdk_frameworks = [
        "Foundation",
    ],
    target_compatible_with = _APPLE_ONLY,
    deps = [
        ":connection",
        ":url",
        ":ws_connection_common",
    ],
)

# =============================================================================
# Native WebRTC implementations using libdatachannel
# For macOS, Linux, Windows, and other native platforms (not emscripten/WASM)
# =============================================================================
#
# libdatachannel provides lightweight C++ WebRTC Data Channels.
# Used on all native platforms instead of stasel/WebRTC XCFramework
# (Apple RTC*.mm sources exist but are not selected in this BUILD).
#
# Windows: HTTP/WS stay on WinHTTP; only RTC uses libdatachannel.

# GCC/Clang vs MSVC c++17 flag (cl.exe rejects -std=)
_LIBDC_COPTS = select({
    ":is_windows": ["/std:c++17"],
    "//conditions:default": ["-std=c++17"],
})

# System libs required when linking static libdatachannel + OpenSSL on Windows
_LIBDC_WINDOWS_LINKOPTS = select({
    ":is_windows": [
        "ws2_32.lib",
        "bcrypt.lib",
        "crypt32.lib",
        "iphlpapi.lib",
        "advapi32.lib",  # OpenSSL CryptoAPI / event log
        "user32.lib",  # OpenSSL OPENSSL_showfatal MessageBoxW
    ],
    "//conditions:default": [],
})

# RTCDataChannel using libdatachannel (native platforms)
cc_library(
    name = "rtc_data_channel_libdc",
    srcs = ["native/RTCDataChannel_libdc.cc"],
    hdrs = ["include/RTCDataChannel.h"],
    copts = _LIBDC_COPTS,
    defines = ["RTC_STATIC"],
    includes = ["."],
    linkopts = _LIBDC_WINDOWS_LINKOPTS,
    deps = [
        ":rtc_data_channel_common",
        "@libdatachannel",
        "@openssl//:ssl",  # libdatachannel links against OpenSSL
        "@openssl//:crypto",  # libdatachannel links against libcrypto
    ],
)

# HTTP request using libcurl (Linux)
cc_library(
    name = "http_request_curl",
    srcs = ["native/HttpRequest_curl.cc"],
    hdrs = ["include/HttpRequest.h"],
    copts = ["-std=c++17"],
    includes = ["."],
    deps = [
        ":http_request_common",
        ":http_response",
        ":url",
        "@curl",
    ],
)

# HTTP request using WinHTTP (Windows OS stack)
cc_library(
    name = "http_request_winhttp",
    srcs = ["windows/HttpRequest_winhttp.cc"],
    hdrs = ["include/HttpRequest.h"],
    # MSVC only (target is Windows-compatible); GCC-style -std= is rejected by cl.exe
    copts = ["/std:c++17"],
    includes = ["."],
    linkopts = ["winhttp.lib"],
    target_compatible_with = ["@platforms//os:windows"],
    deps = [
        ":http_request_common",
        ":http_response",
        ":url",
    ],
)

# WebSocket connection using libdatachannel (Linux)
cc_library(
    name = "ws_connection_libdc",
    srcs = ["native/WSConnection_libdc.cc"],
    hdrs = ["include/WSConnection.h"],
    # alwayslink needed because WSConnection::make(url) calls WSConnection::make(scheme, host, port, path)
    # which is defined here but not directly referenced from other translation units
    alwayslink = True,
    copts = _LIBDC_COPTS,
    defines = ["RTC_STATIC"],
    includes = ["."],
    linkopts = _LIBDC_WINDOWS_LINKOPTS,
    deps = [
        ":connection",
        ":url",
        ":ws_connection_common",
        "@libdatachannel",
        "@openssl//:ssl",
        "@openssl//:crypto",
    ],
)

# WebSocket connection using WinHTTP (Windows OS stack, Windows 8+)
cc_library(
    name = "ws_connection_winhttp",
    srcs = ["windows/WSConnection_winhttp.cc"],
    hdrs = ["include/WSConnection.h"],
    alwayslink = True,
    # MSVC only (target is Windows-compatible); GCC-style -std= is rejected by cl.exe
    copts = ["/std:c++17"],
    includes = ["."],
    linkopts = ["winhttp.lib"],
    target_compatible_with = ["@platforms//os:windows"],
    deps = [
        ":connection",
        ":url",
        ":ws_connection_common",
        ":ws_worker_state",
    ],
)

# RTCPeerConnection using libdatachannel (native platforms incl. Windows)
cc_library(
    name = "rtc_peer_connection_libdc",
    srcs = ["native/RTCPeerConnection_libdc.cc"],
    hdrs = ["include/RTCPeerConnection.h"],
    copts = _LIBDC_COPTS,
    defines = ["RTC_STATIC"],
    includes = ["."],
    linkopts = _LIBDC_WINDOWS_LINKOPTS,
    deps = [
        ":ice_config",
        ":rtc_data_channel_libdc",
        ":rtc_peer_connection_common",
        "//core/log",
        "@libdatachannel",
        "@openssl//:ssl",  # libdatachannel links against OpenSSL
        "@openssl//:crypto",  # libdatachannel links against libcrypto
    ],
)

# =============================================================================
# Common/shared implementations
# =============================================================================

# URL parsing utility
cc_library(
    name = "url",
    srcs = ["common/URL.cc"],
    hdrs = ["include/URL.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

# HTTP response container
cc_library(
    name = "http_response",
    srcs = ["common/HttpResponse.cc"],
    hdrs = ["include/HttpResponse.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

# SSE (Server-Sent Events) parser
cc_library(
    name = "sse_parser",
    srcs = ["common/SSEParser.cc"],
    hdrs = ["include/SSEParser.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

# HTTP request - common base implementation
cc_library(
    name = "http_request_common",
    srcs = ["common/HttpRequest.cc"],
    hdrs = ["include/HttpRequest.h"],
    includes = ["."],
    deps = [
        ":http_response",
        ":url",
    ],
)

# HTTP request abstraction
# Platform-specific implementations selected via config
cc_library(
    name = "http_request",
    hdrs = ["include/HttpRequest.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":http_response",
        ":url",
    ] + select({
        "@platforms//os:emscripten": [":http_request_web"],
        ":is_macos": [":http_request_apple"],
        ":is_ios": [":http_request_apple"],
        ":is_linux": [":http_request_curl"],
        ":is_windows": [":http_request_winhttp"],
        "//conditions:default": [":http_request_default"],
    }),
)

# HTTP request - web implementation
cc_library(
    name = "http_request_web",
    srcs = ["web/HttpRequest_web.cc"],
    hdrs = ["include/HttpRequest.h"],
    includes = ["."],
    deps = [
        ":http_request_common",
        ":http_response",
        ":url",
    ],
)

# HTTP request - default stub implementation
cc_library(
    name = "http_request_default",
    srcs = ["common/HttpRequest_default.cc"],
    hdrs = ["include/HttpRequest.h"],
    includes = ["."],
    deps = [
        ":http_request_common",
        ":http_response",
        ":url",
    ],
)

# Connection base class
cc_library(
    name = "connection",
    srcs = ["common/Connection.cc"],
    hdrs = ["include/Connection.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

# WebSocket connection - common base implementation
cc_library(
    name = "ws_connection_common",
    srcs = ["common/WSConnection.cc"],
    hdrs = ["include/WSConnection.h"],
    includes = ["."],
    deps = [
        ":connection",
        ":url",
    ],
)

# WebSocket connection abstraction
# Platform-specific implementations selected via config
cc_library(
    name = "ws_connection",
    hdrs = ["include/WSConnection.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":connection",
        ":url",
    ] + select({
        "@platforms//os:emscripten": [":ws_connection_web"],
        ":is_macos": [":ws_connection_apple"],
        ":is_ios": [":ws_connection_apple"],
        ":is_linux": [":ws_connection_libdc"],
        ":is_windows": [":ws_connection_winhttp"],
        "//conditions:default": [":ws_connection_default"],
    }),
)

# WebSocket connection - web implementation
cc_library(
    name = "ws_connection_web",
    srcs = ["web/WSConnection_web.cc"],
    hdrs = ["include/WSConnection.h"],
    includes = ["."],
    deps = [
        ":connection",
        ":url",
        ":ws_connection_common",
    ],
)

# WebSocket connection - default stub implementation
cc_library(
    name = "ws_connection_default",
    srcs = ["common/WSConnection_default.cc"],
    hdrs = ["include/WSConnection.h"],
    includes = ["."],
    deps = [
        ":connection",
        ":url",
        ":ws_connection_common",
    ],
)

# Tests

cc_test(
    name = "url_test",
    srcs = ["url_test.cc"],
    deps = [
        ":url",
        "@googletest//:gtest_main",
    ],
)

# Structural checks for Windows WinHTTP + WebRTC libdc wiring (runs on all hosts)
cc_test(
    name = "windows_backend_test",
    srcs = ["windows_backend_test.cc"],
    data = [
        "BUILD",
        "windows/HttpRequest_winhttp.cc",
        "windows/WSConnection_winhttp.cc",
        "//third_party/libdatachannel:BUILD.libdatachannel",
        "//third_party/libdatachannel:sctp-serialize-assoc.patch",
    ],
    deps = [
        "@googletest//:gtest_main",
    ],
)

# Lifecycle rules used by WinHTTP WebSocket (pure C++, all hosts)
cc_test(
    name = "ws_worker_state_test",
    srcs = ["ws_worker_state_test.cc"],
    deps = [
        ":ws_worker_state",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "ws_worker_state",
    hdrs = ["windows/ws_worker_state.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "http_response_test",
    srcs = ["http_response_test.cc"],
    deps = [
        ":http_response",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sse_parser_test",
    srcs = ["sse_parser_test.cc"],
    deps = [
        ":sse_parser",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "ice_config_test",
    srcs = ["ice_config_test.cc"],
    deps = [
        ":ice_config",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "rtc_test",
    srcs = ["rtc_test.cc"],
    deps = [
        ":ice_config",
        ":rtc_data_channel",
        ":rtc_peer_connection",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sync_client_test",
    srcs = ["sync_client_test.cc"],
    deps = [
        ":sync_client",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

# ICE configuration for STUN/TURN servers
cc_library(
    name = "ice_config",
    srcs = ["common/ICEConfig.cc"],
    hdrs = ["include/ICEConfig.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

# RTCDataChannel - common base implementation
cc_library(
    name = "rtc_data_channel_common",
    srcs = ["common/RTCDataChannel.cc"],
    hdrs = ["include/RTCDataChannel.h"],
    includes = ["."],
)

# RTCDataChannel abstraction
# Platform-specific implementations selected via config
# - Web (emscripten): Uses browser RTCDataChannel via JS interop
# - Native (macOS, Linux, etc.): Uses libdatachannel C++ library
cc_library(
    name = "rtc_data_channel",
    hdrs = ["include/RTCDataChannel.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = select({
        "@platforms//os:emscripten": [":rtc_data_channel_web"],
        "//conditions:default": [":rtc_data_channel_libdc"],
    }),
)

# RTCDataChannel - web implementation
cc_library(
    name = "rtc_data_channel_web",
    srcs = ["web/RTCDataChannel_web.cc"],
    hdrs = ["include/RTCDataChannel.h"],
    includes = ["."],
    deps = [":rtc_data_channel_common"],
)

# RTCDataChannel - default stub implementation
cc_library(
    name = "rtc_data_channel_default",
    srcs = ["common/RTCDataChannel_default.cc"],
    hdrs = ["include/RTCDataChannel.h"],
    includes = ["."],
    deps = [":rtc_data_channel_common"],
)

# RTCPeerConnection - common base implementation
cc_library(
    name = "rtc_peer_connection_common",
    srcs = ["common/RTCPeerConnection.cc"],
    hdrs = ["include/RTCPeerConnection.h"],
    includes = ["."],
    deps = [
        ":ice_config",
        ":rtc_data_channel",
    ],
)

# RTCPeerConnection abstraction
# Platform-specific implementations selected via config
# - Web (emscripten): Uses browser RTCPeerConnection via JS interop
# - Native (macOS, Linux, etc.): Uses libdatachannel C++ library
cc_library(
    name = "rtc_peer_connection",
    hdrs = ["include/RTCPeerConnection.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":ice_config",
        ":rtc_data_channel",
    ] + select({
        "@platforms//os:emscripten": [":rtc_peer_connection_web"],
        "//conditions:default": [":rtc_peer_connection_libdc"],
    }),
)

# RTCPeerConnection - web implementation
cc_library(
    name = "rtc_peer_connection_web",
    srcs = ["web/RTCPeerConnection_web.cc"],
    hdrs = ["include/RTCPeerConnection.h"],
    includes = ["."],
    deps = [
        ":ice_config",
        ":rtc_data_channel",
        ":rtc_peer_connection_common",
    ],
)

# RTCPeerConnection - default stub implementation
cc_library(
    name = "rtc_peer_connection_default",
    srcs = ["common/RTCPeerConnection_default.cc"],
    hdrs = ["include/RTCPeerConnection.h"],
    includes = ["."],
    deps = [
        ":ice_config",
        ":rtc_data_channel",
        ":rtc_peer_connection_common",
    ],
)

# Signaling protocol message builders/parsers
cc_library(
    name = "signaling_protocol",
    srcs = ["common/SignalingProtocol.cc"],
    hdrs = ["include/SignalingProtocol.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":rtc_peer_connection",
    ],
)

# Signaling client for WebRTC connection setup
cc_library(
    name = "signaling_client",
    srcs = ["common/SignalingClient.cc"],
    hdrs = ["include/SignalingClient.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":connection",
        ":rtc_peer_connection",
        ":signaling_protocol",
        ":ws_connection",
        "//core/log",
    ],
)

cc_test(
    name = "signaling_test",
    srcs = ["signaling_test.cc"],
    deps = [
        ":signaling_client",
        ":signaling_protocol",
        "@googletest//:gtest_main",
    ],
)

# Sync client - orchestrates P2P synchronization
cc_library(
    name = "sync_client",
    srcs = ["common/SyncClient.cc"],
    hdrs = ["include/SyncClient.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":ice_config",
        ":presence",
        ":rtc_data_channel",
        ":rtc_peer_connection",
        ":signaling_client",
        "//core/cells:crdt",
        "//core/cells:hlc",
        "//core/cells:model",
        "//core/cells:sync_manager",
        "//core/log",
    ],
)

# Presence - cursor/selection presence for collaboration
cc_library(
    name = "presence",
    srcs = ["common/Presence.cc"],
    hdrs = ["include/Presence.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "presence_test",
    srcs = ["presence_test.cc"],
    deps = [
        ":presence",
        "@googletest//:gtest_main",
    ],
)
