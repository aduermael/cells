# iOS / Swift-callable XLSX library
#
# Host-testable C ABI over core xlsx_reader/xlsx_writer, plus an Apple-gated
# static library packaging target for native iOS apps.

load("@rules_apple//apple:apple.bzl", "apple_static_library")
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

config_setting(
    name = "is_macos",
    constraint_values = ["@platforms//os:macos"],
)

config_setting(
    name = "is_ios",
    constraint_values = ["@platforms//os:ios"],
)

# iOS packaging is only analyzable/buildable on Apple platforms. On Linux CI,
# target_compatible_with skips these targets so //... and :check still work.
_APPLE_ONLY = select({
    ":is_macos": [],
    ":is_ios": [],
    "//conditions:default": ["@platforms//:incompatible"],
})

# Thin C ABI wrapper — same sources the iOS packaging target links.
cc_library(
    name = "cells_xlsx",
    srcs = ["cells_xlsx.cc"],
    hdrs = ["cells_xlsx.h"],
    includes = ["."],
    deps = [
        "//core/cells:id",
        "//core/cells:model",
        "//core/cells:xlsx_reader",
        "//core/cells:xlsx_writer",
    ],
)

cc_test(
    name = "cells_xlsx_test",
    srcs = ["cells_xlsx_test.cc"],
    data = ["//testdata/xlsx:sample_xlsx_files"],
    deps = [
        ":cells_xlsx",
        "@googletest//:gtest_main",
    ],
)

# Static library for linking into a native iOS (or macOS) app.
# Requires an Apple SDK/toolchain — not built on Linux; deferred to an Apple host.
apple_static_library(
    name = "cells_xlsx_ios",
    minimum_os_version = "26.5",
    platform_type = "ios",
    target_compatible_with = _APPLE_ONLY,
    deps = [":cells_xlsx"],
)
