# Cross-platform networking library
# Platform-specific implementations selected via config
#
# Structure:
#   include/  - Public headers
#   common/   - Shared implementation (all platforms)
#   web/      - Emscripten/WASM implementations
#   apple/    - iOS/macOS implementations (Objective-C++)
#
# Apple implementations use objc_library for Objective-C++ support.
# On macOS/iOS, we select the objc_library targets; on other platforms
# we use the default stub implementations.

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

# =============================================================================
# Apple platform implementations (Objective-C++)
# These objc_library targets compile .mm files and are only used on Apple platforms.
# =============================================================================

objc_library(
    name = "http_request_apple",
    srcs = ["apple/HttpRequest.mm"],
    hdrs = ["include/HttpRequest.h"],
    copts = ["-std=c++17"],
    includes = ["."],
    sdk_frameworks = ["Foundation"],
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
    deps = [
        ":connection",
        ":url",
        ":ws_connection_common",
    ],
)

# =============================================================================
# Native WebRTC implementations using libdatachannel
# For macOS, Linux, and other native platforms (not emscripten/WASM)
# =============================================================================
#
# libdatachannel provides lightweight C++ WebRTC Data Channels implementation.
# It's used for native platforms instead of the stasel/WebRTC XCFramework
# which had incomplete headers in the macOS slice.

# RTCDataChannel using libdatachannel (native platforms)
cc_library(
    name = "rtc_data_channel_libdc",
    srcs = ["native/RTCDataChannel_libdc.cc"],
    hdrs = ["include/RTCDataChannel.h"],
    copts = ["-std=c++17"],
    includes = ["."],
    deps = [
        ":rtc_data_channel_common",
        "@libdatachannel",
        "@openssl//:ssl",     # libdatachannel links against OpenSSL
        "@openssl//:crypto",  # libdatachannel links against libcrypto
    ],
)

# HTTP request using libcurl (Linux/native platforms)
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

# WebSocket connection using libdatachannel (Linux/native platforms)
cc_library(
    name = "ws_connection_libdc",
    srcs = ["native/WSConnection_libdc.cc"],
    hdrs = ["include/WSConnection.h"],
    copts = ["-std=c++17"],
    includes = ["."],
    deps = [
        ":connection",
        ":url",
        ":ws_connection_common",
        "@libdatachannel",
        "@openssl//:ssl",
        "@openssl//:crypto",
    ],
)

# RTCPeerConnection using libdatachannel (native platforms)
cc_library(
    name = "rtc_peer_connection_libdc",
    srcs = ["native/RTCPeerConnection_libdc.cc"],
    hdrs = ["include/RTCPeerConnection.h"],
    copts = ["-std=c++17"],
    includes = ["."],
    deps = [
        ":ice_config",
        ":rtc_data_channel_libdc",
        ":rtc_peer_connection_common",
        "@libdatachannel",
        "@openssl//:ssl",     # libdatachannel links against OpenSSL
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
        ":rtc_data_channel",
        ":rtc_peer_connection",
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
