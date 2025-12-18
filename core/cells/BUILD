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
