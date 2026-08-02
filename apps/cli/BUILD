load("@bazel_skylib//rules:build_test.bzl", "build_test")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

# Config settings for platform detection
config_setting(
    name = "is_macos",
    constraint_values = ["@platforms//os:macos"],
)

config_setting(
    name = "is_windows",
    constraint_values = ["@platforms//os:windows"],
)

# Ensures full + converter CLIs link successfully (used by Windows CI).
build_test(
    name = "cells_cli_build_test",
    targets = [
        ":cells",
        ":cells-converter",
    ],
)

# Sync argument parsing (shared by full CLI and converter-only)
cc_library(
    name = "sync_args",
    srcs = ["sync_args.cc"],
    hdrs = [
        "cli_version.h",
        "sync_args.h",
        "sync_command.h",
    ],
)

# Large stdout spill helper (agent-friendly CLI output)
cc_library(
    name = "output_spill",
    srcs = ["output_spill.cc"],
    hdrs = ["output_spill.h"],
)

cc_test(
    name = "output_spill_test",
    srcs = ["output_spill_test.cc"],
    deps = [
        ":output_spill",
        "@googletest//:gtest_main",
    ],
)

# Session protocol (pure JSON IPC + room URL + idle timeout)
cc_library(
    name = "session_protocol",
    srcs = ["session_protocol.cc"],
    hdrs = ["session_protocol.h"],
    deps = [":output_spill"],
)

# Session on-disk registry
cc_library(
    name = "session_store",
    srcs = ["session_store.cc"],
    hdrs = ["session_store.h"],
    deps = [
        ":output_spill",
        ":session_protocol",
    ],
)

# Session CLI arg parsing
cc_library(
    name = "session_args",
    srcs = ["session_args.cc"],
    hdrs = ["session_args.h"],
    deps = [":session_protocol"],
)

cc_test(
    name = "session_protocol_test",
    srcs = ["session_protocol_test.cc"],
    deps = [
        ":session_protocol",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "session_args_test",
    srcs = ["session_args_test.cc"],
    deps = [
        ":session_args",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "session_store_test",
    srcs = ["session_store_test.cc"],
    deps = [
        ":session_store",
        "@googletest//:gtest_main",
    ],
)

# Full CLI binary with sync (+ session on Unix).
# Windows: session daemon uses POSIX sockets/fork — stub until Windows IPC lands.
# Sync/WebRTC on Windows uses WinHTTP (signaling) + libdatachannel (RTC).
cc_binary(
    name = "cells",
    srcs = [
        "converter.cc",
        "converter.h",
        "main.cc",
        "options.h",
        "session_command.h",
        "sync_command.cc",
        "sync_command.h",
    ] + select({
        # Session daemon: Unix domain sockets + fork (not yet ported).
        ":is_windows": ["session_command_stub.cc"],
        "//conditions:default": ["session_command.cc"],
    }),
    # Link CoreFoundation on macOS for CFRunLoop in sync_command
    linkopts = select({
        ":is_macos": [
            "-framework",
            "CoreFoundation",
        ],
        "//conditions:default": [],
    }),
    deps = [
        ":output_spill",
        ":session_args",
        ":session_protocol",
        ":session_store",
        ":sync_args",
        "//core/cells:csv_reader",
        "//core/cells:csv_writer",
        "//core/cells:formula_eval",
        "//core/cells:formula_recalc",
        "//core/cells:id",
        "//core/cells:luau_sandbox",
        "//core/cells:model",
        "//core/cells:parser",
        "//core/cells:serializer",
        "//core/cells:sync_manager",
        "//core/cells:xlsx_reader",
        "//core/cells:xlsx_writer",
        "//core/net:sync_client",
        "//core/net:url",
    ],
)

# Converter-only CLI binary (no sync/network support)
# Useful for lightweight deployments that only need file conversion
cc_binary(
    name = "cells-converter",
    srcs = [
        "converter.cc",
        "converter.h",
        "main.cc",
        "options.h",
        "session_command.h",
        "session_command_stub.cc",
        "sync_command.h",
        "sync_command_stub.cc",
    ],
    deps = [
        ":output_spill",
        ":session_args",
        ":session_protocol",
        ":session_store",
        ":sync_args",
        "//core/cells:csv_reader",
        "//core/cells:csv_writer",
        "//core/cells:formula_eval",
        "//core/cells:formula_recalc",
        "//core/cells:id",
        "//core/cells:luau_sandbox",
        "//core/cells:model",
        "//core/cells:parser",
        "//core/cells:serializer",
        "//core/cells:xlsx_reader",
        "//core/cells:xlsx_writer",
    ],
)

cc_test(
    name = "converter_test",
    srcs = [
        "converter.cc",
        "converter.h",
        "converter_test.cc",
        "options.h",
    ],
    data = ["//testdata/xlsx:sample_xlsx_files"],
    deps = [
        ":output_spill",
        "//core/cells:csv_reader",
        "//core/cells:csv_writer",
        "//core/cells:formula_eval",
        "//core/cells:formula_recalc",
        "//core/cells:luau_sandbox",
        "//core/cells:model",
        "//core/cells:parser",
        "//core/cells:ref_converter",
        "//core/cells:serializer",
        "//core/cells:xlsx_reader",
        "//core/cells:xlsx_writer",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sync_args_test",
    srcs = ["sync_args_test.cc"],
    deps = [
        ":sync_args",
        "@googletest//:gtest_main",
    ],
)
