# Cross-platform networking library
# Platform-specific implementations selected via config
#
# Structure:
#   include/  - Public headers
#   common/   - Shared implementation (all platforms)
#   web/      - Emscripten/WASM implementations
#   apple/    - iOS/macOS implementations (Objective-C++)

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

# HTTP request abstraction
# Note: Apple implementation (apple/HttpRequest.mm) requires rules_apple.
# For now, non-WASM platforms use the default stub implementation.
cc_library(
    name = "http_request",
    srcs = [
        "common/HttpRequest.cc",
    ] + select({
        "@platforms//os:emscripten": ["web/HttpRequest_web.cc"],
        # TODO: Use apple/HttpRequest.mm with objc_library when rules_apple is added
        "//conditions:default": ["common/HttpRequest_default.cc"],
    }),
    hdrs = ["include/HttpRequest.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
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

# WebSocket connection abstraction
# Note: Apple implementation (apple/WSConnection.mm) requires rules_apple.
# For now, non-WASM platforms use the default stub implementation.
cc_library(
    name = "ws_connection",
    srcs = [
        "common/WSConnection.cc",
    ] + select({
        "@platforms//os:emscripten": ["web/WSConnection_web.cc"],
        # TODO: Use apple/WSConnection.mm with objc_library when rules_apple is added
        "//conditions:default": ["common/WSConnection_default.cc"],
    }),
    hdrs = ["include/WSConnection.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":connection",
        ":url",
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

# RTCDataChannel abstraction
cc_library(
    name = "rtc_data_channel",
    srcs = [
        "common/RTCDataChannel.cc",
    ] + select({
        "@platforms//os:emscripten": ["web/RTCDataChannel_web.cc"],
        # TODO: Use apple/RTCDataChannel.mm with objc_library when rules_apple is added
        "//conditions:default": ["common/RTCDataChannel_default.cc"],
    }),
    hdrs = ["include/RTCDataChannel.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

# RTCPeerConnection abstraction
cc_library(
    name = "rtc_peer_connection",
    srcs = [
        "common/RTCPeerConnection.cc",
    ] + select({
        "@platforms//os:emscripten": ["web/RTCPeerConnection_web.cc"],
        # TODO: Use apple/RTCPeerConnection.mm with objc_library when rules_apple is added
        "//conditions:default": ["common/RTCPeerConnection_default.cc"],
    }),
    hdrs = ["include/RTCPeerConnection.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":ice_config",
        ":rtc_data_channel",
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
