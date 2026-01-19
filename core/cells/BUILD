cc_library(
    name = "types",
    hdrs = ["types.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "style_types",
    hdrs = ["style_types.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "range",
    hdrs = ["range.h"],
    visibility = ["//visibility:public"],
    deps = [":types"],
)

cc_test(
    name = "range_test",
    srcs = ["range_test.cc"],
    deps = [
        ":range",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "range_index",
    srcs = ["range_index.cc"],
    hdrs = ["range_index.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":range",
        ":rtree",
        ":types",
    ],
)

cc_library(
    name = "style_registry",
    srcs = ["style_registry.cc"],
    hdrs = ["style_registry.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":id",
        ":style_types",
        ":types",
    ],
)

cc_test(
    name = "style_registry_test",
    srcs = ["style_registry_test.cc"],
    deps = [
        ":id",
        ":style_registry",
        ":style_types",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "format_registry",
    srcs = ["format_registry.cc"],
    hdrs = ["format_registry.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":id",
        ":types",
    ],
)

cc_test(
    name = "format_registry_test",
    srcs = ["format_registry_test.cc"],
    deps = [
        ":format_registry",
        ":id",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "range_index_test",
    srcs = ["range_index_test.cc"],
    deps = [
        ":range_index",
        "@googletest//:gtest_main",
    ],
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
    name = "number_format",
    srcs = ["number_format.cc"],
    hdrs = ["number_format.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":format_code_parser",
        ":formula_ast",
        ":id",
        ":types",
    ],
)

cc_test(
    name = "number_format_test",
    srcs = ["number_format_test.cc"],
    deps = [
        ":formula_ast",
        ":number_format",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "format_code_parser",
    srcs = ["format_code_parser.cc"],
    hdrs = ["format_code_parser.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "format_code_parser_test",
    srcs = ["format_code_parser_test.cc"],
    deps = [
        ":format_code_parser",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "format_code_formatter",
    srcs = ["format_code_formatter.cc"],
    hdrs = ["format_code_formatter.h"],
    visibility = ["//visibility:public"],
    deps = [":format_code_parser"],
)

cc_test(
    name = "format_code_formatter_test",
    srcs = ["format_code_formatter_test.cc"],
    deps = [
        ":format_code_formatter",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "input_parser",
    srcs = ["input_parser.cc"],
    hdrs = ["input_parser.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":number_format",
        ":types",
    ],
)

cc_test(
    name = "input_parser_test",
    srcs = ["input_parser_test.cc"],
    deps = [
        ":input_parser",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "number_formatter",
    srcs = ["number_formatter.cc"],
    hdrs = ["number_formatter.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":format_code_formatter",
        ":format_code_parser",
        ":id",
        ":input_parser",
        ":number_format",
        ":types",
    ],
)

cc_test(
    name = "number_formatter_test",
    srcs = ["number_formatter_test.cc"],
    deps = [
        ":input_parser",
        ":number_formatter",
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
    srcs = [
        "model.cc",
        "sheet.cc",
    ],
    hdrs = ["model.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dependency_graph",
        ":format_registry",
        ":formula_ast",
        ":formula_parser",
        ":formula_serializer",
        ":id",
        ":named_ranges",
        ":oplog",
        ":range",
        ":range_index",
        ":style_registry",
        ":style_types",
        ":types",
    ],
)

cc_library(
    name = "crdt",
    srcs = [
        "crdt.cc",
        "crdt_axis.cc",
        "crdt_cell.cc",
        "crdt_range.cc",
    ],
    hdrs = [
        "crdt.h",
        "crdt_internal.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":format_code_parser",
        ":model",
        ":number_format",
        ":operation",
        ":range",
        ":style_registry",
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
        "//core/log",
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

cc_test(
    name = "crdt_range_test",
    srcs = ["crdt_range_test.cc"],
    deps = [
        ":crdt",
        ":id",
        ":range",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sync_formula_test",
    srcs = ["sync_formula_test.cc"],
    deps = [
        ":crdt",
        ":id",
        ":model",
        ":ref_converter",
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
        ":formula_eval",
        ":formula_recalc",
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

cc_test(
    name = "large_file_test",
    srcs = ["large_file_test.cc"],
    data = ["//testdata/xlsx:sample_xlsx_files"],
    deps = [
        ":viewport_index",
        ":xlsx_reader",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "workbook_benchmark_test",
    srcs = ["workbook_benchmark_test.cc"],
    deps = [
        ":crdt",
        ":formula_eval",
        ":formula_parser",
        ":formula_recalc",
        ":formula_resolver",
        ":id",
        ":model",
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
    data = ["//testdata/xlsx:sample_xlsx_files"],
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
        ":formula_display",
        ":formula_parser",
        ":id",
        ":ref_converter",
        "@googletest//:gtest_main",
    ],
)


cc_library(
    name = "ostree",
    srcs = ["ostree.cc"],
    hdrs = ["ostree.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":types",
    ],
)

cc_test(
    name = "ostree_test",
    srcs = ["ostree_test.cc"],
    deps = [
        ":id",
        ":ostree",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "axis_index",
    srcs = ["axis_index.cc"],
    hdrs = ["axis_index.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":ostree",
        ":types",
    ],
)

cc_test(
    name = "axis_index_test",
    srcs = ["axis_index_test.cc"],
    deps = [
        ":axis_index",
        ":id",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "viewport_index",
    srcs = ["viewport_index.cc"],
    hdrs = ["viewport_index.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":axis_index",
        ":model",
        ":types",
    ],
)

cc_test(
    name = "viewport_index_test",
    srcs = ["viewport_index_test.cc"],
    deps = [
        ":id",
        ":viewport_index",
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
        ":formula_display",
        ":model",
        ":named_ranges",
        ":types",
    ],
)

cc_library(
    name = "formula_serializer",
    srcs = ["formula_serializer.cc"],
    hdrs = ["formula_serializer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":formula_ast",
    ],
)

cc_library(
    name = "formula_display",
    srcs = ["formula_display.cc"],
    hdrs = ["formula_display.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":formula_ast",
        ":model",
    ],
)

cc_test(
    name = "formula_serializer_test",
    srcs = ["formula_serializer_test.cc"],
    deps = [
        ":formula_parser",
        ":formula_resolver",
        ":formula_serializer",
        ":id",
        ":model",
        "@googletest//:gtest_main",
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

cc_test(
    name = "formula_integration_test",
    srcs = ["formula_integration_test.cc"],
    deps = [
        ":dependency_graph",
        ":formula_ast",
        ":formula_display",
        ":formula_parser",
        ":formula_resolver",
        ":formula_serializer",
        ":model",
        ":named_ranges",
        ":parser",
        ":serializer",
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
        ":named_ranges",
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

cc_test(
    name = "formula_move_test",
    srcs = ["formula_move_test.cc"],
    deps = [
        ":dependency_graph",
        ":formula_ast",
        ":formula_display",
        ":formula_parser",
        ":formula_resolver",
        ":id",
        ":model",
        ":named_ranges",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "formula_eval",
    srcs = [
        "formula_eval.cc",
        "formula_functions.cc",
        "//core/cells/functions:fn_array.cc",
        "//core/cells/functions:fn_datetime.cc",
        "//core/cells/functions:fn_logic.cc",
        "//core/cells/functions:fn_lookup.cc",
        "//core/cells/functions:fn_math.cc",
        "//core/cells/functions:fn_rand.cc",
        "//core/cells/functions:fn_stats.cc",
        "//core/cells/functions:fn_text.cc",
    ],
    hdrs = [
        "formula_eval.h",
        "formula_functions.h",
        "//core/cells/functions:fn_array.h",
        "//core/cells/functions:fn_datetime.h",
        "//core/cells/functions:fn_logic.h",
        "//core/cells/functions:fn_lookup.h",
        "//core/cells/functions:fn_math.h",
        "//core/cells/functions:fn_rand.h",
        "//core/cells/functions:fn_stats.h",
        "//core/cells/functions:fn_text.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":formula_ast",
        ":model",
        ":types",
    ],
)

cc_test(
    name = "formula_eval_test",
    srcs = ["formula_eval_test.cc"],
    deps = [
        ":formula_eval",
        ":formula_parser",
        ":formula_resolver",
        ":id",
        ":model",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "formula_functions_test",
    srcs = ["formula_functions_test.cc"],
    deps = [
        ":formula_eval",
        ":formula_parser",
        ":formula_resolver",
        ":id",
        ":model",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "formula_recalc",
    srcs = ["formula_recalc.cc"],
    hdrs = ["formula_recalc.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dependency_graph",
        ":formula_eval",
        ":model",
        ":types",
    ],
)

cc_test(
    name = "formula_recalc_test",
    srcs = ["formula_recalc_test.cc"],
    deps = [
        ":formula_eval",
        ":formula_parser",
        ":formula_recalc",
        ":formula_resolver",
        ":id",
        ":model",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "fill_range",
    srcs = ["fill_range.cc"],
    hdrs = ["fill_range.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":crdt",
        ":formula_display",
        ":formula_resolver",
        ":formula_serializer",
        ":id",
        ":model",
        ":ref_converter",
        ":types",
    ],
)

cc_test(
    name = "fill_range_test",
    srcs = ["fill_range_test.cc"],
    deps = [
        ":fill_range",
        ":formula_display",
        ":formula_parser",
        ":formula_resolver",
        ":id",
        ":model",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "luau_sandbox",
    srcs = [
        "luau_api.cc",
        "luau_sandbox.cc",
        "luau_types.cc",
    ],
    hdrs = ["luau_sandbox.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":crdt",
        ":fill_range",
        ":formula_parser",
        ":formula_recalc",
        ":formula_resolver",
        ":formula_serializer",
        ":id",
        ":model",
        ":ref_converter",
        "@luau//:luau",
    ],
)

cc_test(
    name = "luau_sandbox_test",
    srcs = ["luau_sandbox_test.cc"],
    deps = [
        ":id",
        ":luau_sandbox",
        ":model",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "luau_autocomplete",
    srcs = ["luau_autocomplete.cc"],
    hdrs = ["luau_autocomplete.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@luau//:luau_analysis",
    ],
)

cc_test(
    name = "luau_autocomplete_test",
    srcs = ["luau_autocomplete_test.cc"],
    deps = [
        ":luau_autocomplete",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "agent_client",
    srcs = ["agent_client.cc"],
    hdrs = ["agent_client.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":luau_sandbox",
        ":model",
        "//core/net:http_request",
        "//core/net:sse_parser",
        "//core/net:url",
    ],
)
