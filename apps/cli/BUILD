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

# Ensures full + converter CLIs link successfully (used by Linux/Windows/macOS PR CI).
build_test(
    name = "cells_cli_build_test",
    targets = [
        ":cells",
        ":cells-converter",
        ":cells-no-collab",
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

# Shared converter (read/write xlsx/csv/zcd + scripts)
cc_library(
    name = "converter_lib",
    srcs = ["converter.cc"],
    hdrs = [
        "converter.h",
        "options.h",
    ],
    deps = [
        ":output_spill",
        "//core/cells:csv_reader",
        "//core/cells:csv_writer",
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_recalc",
        "//core/cells:formula_serializer",
        "//core/cells:id",
        "//core/cells:luau_sandbox",
        "//core/cells:model",
        "//core/cells:parser",
        "//core/cells:ref_converter",
        "//core/cells:serializer",
        "//core/cells:xlsx_reader",
        "//core/cells:xlsx_writer",
    ],
)

# Local file-backed session workbook (load / exec / export; no collab)
cc_library(
    name = "session_workbook",
    srcs = ["session_workbook.cc"],
    hdrs = ["session_workbook.h"],
    deps = [
        ":converter_lib",
        "//core/cells:luau_sandbox",
        "//core/cells:model",
    ],
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

# Full CLI binary with sync + session daemon.
# Session IPC: AF_UNIX sockets (POSIX + Windows 10+). Daemon: fork+exec / CreateProcess.
# Sync/WebRTC on Windows uses WinHTTP (signaling) + libdatachannel (RTC).
cc_binary(
    name = "cells",
    srcs = [
        "main.cc",
        "session_command.cc",
        "session_command.h",
        "sync_command.cc",
        "sync_command.h",
    ],
    # Link CoreFoundation on macOS for CFRunLoop in sync_command;
    # ws2_32 for AF_UNIX / WSAPoll session IPC on Windows.
    linkopts = select({
        ":is_macos": [
            "-framework",
            "CoreFoundation",
        ],
        ":is_windows": [
            "ws2_32.lib",
        ],
        "//conditions:default": [],
    }),
    deps = [
        ":converter_lib",
        ":output_spill",
        ":session_args",
        ":session_protocol",
        ":session_store",
        ":session_workbook",
        ":sync_args",
        "//core/cells:crdt",
        "//core/cells:csv_writer",
        "//core/cells:id",
        "//core/cells:luau_sandbox",
        "//core/cells:model",
        "//core/cells:serializer",
        "//core/cells:sync_manager",
        "//core/cells:xlsx_writer",
        "//core/net:sync_client",
        "//core/net:url",
    ],
)

# Production-trimmed CLI: local sessions, no operation ledger (with --config=no-collab),
# no connectivity. Sync is stubbed. Used by bazel run :cli-no-collab.
cc_binary(
    name = "cells-no-collab",
    srcs = [
        "main.cc",
        "session_command.cc",
        "session_command.h",
        "sync_command.h",
        "sync_command_stub.cc",
    ],
    defines = ["CELLS_NO_COLLAB"],
    linkopts = select({
        ":is_windows": [
            "ws2_32.lib",
        ],
        "//conditions:default": [],
    }),
    deps = [
        ":converter_lib",
        ":output_spill",
        ":session_args",
        ":session_protocol",
        ":session_store",
        ":session_workbook",
        ":sync_args",
        "//core/cells:crdt",
        "//core/cells:csv_writer",
        "//core/cells:id",
        "//core/cells:luau_sandbox",
        "//core/cells:model",
        "//core/cells:serializer",
        "//core/cells:xlsx_writer",
    ],
)

# Alias kept for existing CI / Alpine converter-only jobs.
# Same trimmed binary as cells-no-collab (local sessions, no network).
cc_binary(
    name = "cells-converter",
    srcs = [
        "main.cc",
        "session_command.cc",
        "session_command.h",
        "sync_command.h",
        "sync_command_stub.cc",
    ],
    defines = ["CELLS_NO_COLLAB"],
    linkopts = select({
        ":is_windows": [
            "ws2_32.lib",
        ],
        "//conditions:default": [],
    }),
    deps = [
        ":converter_lib",
        ":output_spill",
        ":session_args",
        ":session_protocol",
        ":session_store",
        ":session_workbook",
        ":sync_args",
        "//core/cells:crdt",
        "//core/cells:csv_writer",
        "//core/cells:id",
        "//core/cells:luau_sandbox",
        "//core/cells:model",
        "//core/cells:serializer",
        "//core/cells:xlsx_writer",
    ],
)

cc_test(
    name = "converter_test",
    srcs = ["converter_test.cc"],
    data = ["//testdata/xlsx:sample_xlsx_files"],
    deps = [
        ":converter_lib",
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

cc_test(
    name = "session_workbook_test",
    srcs = ["session_workbook_test.cc"],
    deps = [
        ":session_workbook",
        "//core/cells:xlsx_reader",
        "@googletest//:gtest_main",
    ],
)
