# Cross-platform logging library
# Platform-specific implementations selected via config
#
# Note: Apple-specific implementation (apple/Logger.mm) requires rules_apple.
# For now, macOS uses the default stderr-based logger. The .mm implementation
# can be enabled later when rules_apple is added to the workspace.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "log",
    srcs = [
        "common/Logger.cc",
    ] + select({
        "@platforms//os:emscripten": ["web/Logger_web.cc"],
        # TODO: Use apple/Logger.mm with objc_library when rules_apple is added
        "//conditions:default": ["common/Logger_default.cc"],
    }),
    hdrs = ["include/Logger.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)
