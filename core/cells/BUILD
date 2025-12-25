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
    name = "hlc",
    srcs = ["hlc.cc"],
    hdrs = ["hlc.h"],
    visibility = ["//visibility:public"],
    deps = [":types"],
)

cc_test(
    name = "hlc_test",
    srcs = ["hlc_test.cc"],
    deps = [
        ":hlc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "operation",
    srcs = ["operation.cc"],
    hdrs = ["operation.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":hlc",
        ":types",
    ],
)

cc_test(
    name = "operation_test",
    srcs = ["operation_test.cc"],
    deps = [
        ":operation",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "oplog",
    srcs = ["oplog.cc"],
    hdrs = ["oplog.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":operation",
        ":types",
    ],
)

cc_test(
    name = "oplog_test",
    srcs = ["oplog_test.cc"],
    deps = [
        ":oplog",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "model",
    srcs = ["model.cc"],
    hdrs = ["model.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":formula_ast",
        ":formula_parser",
        ":id",
        ":oplog",
        ":types",
    ],
)

cc_library(
    name = "crdt",
    srcs = ["crdt.cc"],
    hdrs = ["crdt.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":model",
        ":operation",
    ],
)

cc_library(
    name = "sync_manager",
    srcs = ["sync_manager.cc"],
    hdrs = ["sync_manager.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":crdt",
        ":hlc",
        ":model",
        ":operation",
        ":oplog",
        ":types",
    ],
)

cc_test(
    name = "sync_manager_test",
    srcs = ["sync_manager_test.cc"],
    deps = [
        ":id",
        ":sync_manager",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "crdt_test",
    srcs = ["crdt_test.cc"],
    deps = [
        ":crdt",
        ":id",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "parser",
    srcs = ["parser.cc"],
    hdrs = ["parser.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":model",
        ":oplog",
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
        ":oplog",
        ":types",
    ],
)

cc_test(
    name = "parser_test",
    srcs = ["parser_test.cc"],
    data = ["//testdata:sample_zcd_files"],
    deps = [
        ":parser",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "serializer_test",
    srcs = ["serializer_test.cc"],
    data = ["//testdata:sample_zcd_files"],
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
    data = ["//testdata:sample_csv_files"],
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
        "//third_party/miniz",
        "@pugixml",
    ],
)

cc_test(
    name = "xlsx_reader_test",
    srcs = ["xlsx_reader_test.cc"],
    data = ["//testdata/xlsx:sample_xlsx_files"],
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
        ":id",
        ":model",
        ":ref_converter",
        "//third_party/miniz",
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

cc_library(
    name = "ref_converter",
    srcs = ["ref_converter.cc"],
    hdrs = ["ref_converter.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":model",
        ":types",
    ],
)

cc_test(
    name = "ref_converter_test",
    srcs = ["ref_converter_test.cc"],
    deps = [
        ":id",
        ":ref_converter",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "quadtree",
    srcs = ["quadtree.cc"],
    hdrs = ["quadtree.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":model",
        ":types",
    ],
)

cc_test(
    name = "quadtree_test",
    srcs = ["quadtree_test.cc"],
    deps = [
        ":id",
        ":quadtree",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "formula_lexer",
    srcs = ["formula_lexer.cc"],
    hdrs = ["formula_lexer.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "formula_lexer_test",
    srcs = ["formula_lexer_test.cc"],
    deps = [
        ":formula_lexer",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "formula_ast",
    srcs = ["formula_ast.cc"],
    hdrs = ["formula_ast.h"],
    visibility = ["//visibility:public"],
    deps = [":formula_lexer"],
)

cc_test(
    name = "formula_ast_test",
    srcs = ["formula_ast_test.cc"],
    deps = [
        ":formula_ast",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "formula_parser",
    srcs = ["formula_parser.cc"],
    hdrs = ["formula_parser.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":formula_ast",
        ":formula_lexer",
    ],
)

cc_test(
    name = "formula_parser_test",
    srcs = ["formula_parser_test.cc"],
    deps = [
        ":formula_parser",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "named_ranges",
    srcs = ["named_ranges.cc"],
    hdrs = ["named_ranges.h"],
    visibility = ["//visibility:public"],
    deps = [":types"],
)

cc_test(
    name = "named_ranges_test",
    srcs = ["named_ranges_test.cc"],
    deps = [
        ":id",
        ":named_ranges",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "formula_resolver",
    srcs = ["formula_resolver.cc"],
    hdrs = ["formula_resolver.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":formula_ast",
        ":model",
        ":named_ranges",
        ":types",
    ],
)

cc_test(
    name = "formula_resolver_test",
    srcs = ["formula_resolver_test.cc"],
    deps = [
        ":formula_parser",
        ":formula_resolver",
        ":id",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "rtree",
    srcs = ["rtree.cc"],
    hdrs = ["rtree.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "rtree_test",
    srcs = ["rtree_test.cc"],
    deps = [
        ":rtree",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "dependency_graph",
    srcs = ["dependency_graph.cc"],
    hdrs = ["dependency_graph.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":formula_ast",
        ":rtree",
        ":types",
    ],
)

cc_test(
    name = "dependency_graph_test",
    srcs = ["dependency_graph_test.cc"],
    deps = [
        ":dependency_graph",
        ":formula_ast",
        ":formula_parser",
        ":formula_resolver",
        ":id",
        ":model",
        "@googletest//:gtest_main",
    ],
)
