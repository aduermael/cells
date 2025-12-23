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
    deps = [
        "//core/cells:csv_reader",
        "//core/cells:csv_writer",
        "//core/cells:id",
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

cc_test(
    name = "converter_test",
    srcs = [
        "converter.cc",
        "converter.h",
        "converter_test.cc",
        "options.h",
    ],
    data = ["//core/testdata/xlsx:sample_xlsx_files"],
    deps = [
        "//core/cells:csv_reader",
        "//core/cells:csv_writer",
        "//core/cells:model",
        "//core/cells:parser",
        "//core/cells:ref_converter",
        "//core/cells:serializer",
        "//core/cells:xlsx_reader",
        "//core/cells:xlsx_writer",
        "@googletest//:gtest_main",
    ],
)
