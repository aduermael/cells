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
        "//core/cells:serializer",
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
    deps = [
        "//core/cells:csv_reader",
        "//core/cells:csv_writer",
        "//core/cells:model",
        "//core/cells:parser",
        "//core/cells:serializer",
        "@googletest//:gtest_main",
    ],
)
