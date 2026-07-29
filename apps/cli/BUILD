# Config setting for macOS platform detection
config_setting(
    name = "is_macos",
    constraint_values = ["@platforms//os:macos"],
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

# Full CLI binary with sync support (requires OpenSSL/libdatachannel)
cc_binary(
    name = "cells",
    srcs = [
        "converter.cc",
        "converter.h",
        "main.cc",
        "options.h",
        "sync_command.cc",
        "sync_command.h",
    ],
    # Link CoreFoundation on macOS for CFRunLoop in sync_command
    linkopts = select({
        ":is_macos": [
            "-framework",
            "CoreFoundation",
        ],
        "//conditions:default": [],
    }),
    deps = [
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
        "sync_command.h",
        "sync_command_stub.cc",
    ],
    deps = [
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
