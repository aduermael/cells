cc_binary(
    name = "cells",
    srcs = [
        "converter.cc",
        "converter.h",
        "main.cc",
        "options.h",
    ],
    deps = [
        "//core/cells:csv_reader",
        "//core/cells:csv_writer",
        "//core/cells:model",
        "//core/cells:parser",
        "//core/cells:ref_converter",
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
