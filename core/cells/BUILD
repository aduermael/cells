cc_library(
    name = "types",
    hdrs = ["types.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "id",
    srcs = ["id.cc"],
    hdrs = ["id.h"],
    visibility = ["//visibility:public"],
    deps = [":types"],
)

cc_test(
    name = "id_test",
    srcs = ["id_test.cc"],
    deps = [
        ":id",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "model",
    srcs = ["model.cc"],
    hdrs = ["model.h"],
    visibility = ["//visibility:public"],
    deps = [":types"],
)

cc_library(
    name = "parser",
    srcs = ["parser.cc"],
    hdrs = ["parser.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":model",
        ":types",
    ],
)

cc_library(
    name = "serializer",
    srcs = ["serializer.cc"],
    hdrs = ["serializer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":model",
        ":types",
    ],
)

cc_test(
    name = "parser_test",
    srcs = ["parser_test.cc"],
    data = ["//core/testdata:sample_cells_files"],
    deps = [
        ":parser",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "serializer_test",
    srcs = ["serializer_test.cc"],
    data = ["//core/testdata:sample_cells_files"],
    deps = [
        ":id",
        ":model",
        ":parser",
        ":serializer",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "roundtrip_test",
    srcs = ["roundtrip_test.cc"],
    deps = [
        ":parser",
        ":serializer",
    ],
)

cc_library(
    name = "csv_reader",
    srcs = ["csv_reader.cc"],
    hdrs = ["csv_reader.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":id",
        ":model",
    ],
)

cc_test(
    name = "csv_reader_test",
    srcs = ["csv_reader_test.cc"],
    data = ["//core/testdata:sample_csv_files"],
    deps = [
        ":csv_reader",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "csv_writer",
    srcs = ["csv_writer.cc"],
    hdrs = ["csv_writer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":model",
    ],
)

cc_test(
    name = "csv_writer_test",
    srcs = ["csv_writer_test.cc"],
    deps = [
        ":csv_reader",
        ":csv_writer",
        ":id",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "xlsx_reader",
    srcs = ["xlsx_reader.cc"],
    hdrs = ["xlsx_reader.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":id",
        ":model",
        "@openxlsx",
    ],
)

cc_test(
    name = "xlsx_reader_test",
    srcs = ["xlsx_reader_test.cc"],
    data = ["//core/testdata/xlsx:sample_xlsx_files"],
    deps = [
        ":xlsx_reader",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "xlsx_writer",
    srcs = ["xlsx_writer.cc"],
    hdrs = ["xlsx_writer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":model",
        "@openxlsx",
    ],
)

cc_test(
    name = "xlsx_writer_test",
    srcs = ["xlsx_writer_test.cc"],
    deps = [
        ":id",
        ":xlsx_reader",
        ":xlsx_writer",
        "@googletest//:gtest_main",
    ],
)
