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

# TODO: Add libraries as they are implemented:
# - WSConnection: WebSocket connection abstraction
# - RTCPeerConnection: WebRTC peer connection
# - SignalingClient: WebRTC signaling protocol
# - SyncClient: Sync protocol over DataChannel
# - Presence: Cursor/selection presence
