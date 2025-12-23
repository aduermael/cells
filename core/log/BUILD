# Cross-platform logging library
# Platform-specific implementations selected via config

cc_library(
    name = "log",
    srcs = [
        "common/Logger.cc",
    ] + select({
        "@platforms//os:emscripten": ["web/Logger_web.cc"],
        "@platforms//os:macos": ["apple/Logger.mm"],
        "@platforms//os:ios": ["apple/Logger.mm"],
        "//conditions:default": ["common/Logger_default.cc"],
    }),
    hdrs = ["include/Logger.h"],
    includes = ["."],
    linkopts = select({
        "@platforms//os:macos": ["-framework Foundation"],
        "@platforms//os:ios": ["-framework Foundation"],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
