# Individual formula function implementations
# Each function is in its own file for maintainability

cc_library(
    name = "fn_rand",
    srcs = ["fn_rand.cc"],
    hdrs = ["fn_rand.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//core/cells:formula_ast",
        "//core/cells:formula_eval",
    ],
)

cc_test(
    name = "fn_rand_test",
    srcs = ["fn_rand_test.cc"],
    deps = [
        ":fn_rand",
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "fn_stats",
    srcs = ["fn_stats.cc"],
    hdrs = ["fn_stats.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//core/cells:formula_ast",
        "//core/cells:formula_eval",
    ],
)

cc_test(
    name = "fn_stats_test",
    srcs = ["fn_stats_test.cc"],
    deps = [
        ":fn_stats",
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "fn_lookup",
    srcs = ["fn_lookup.cc"],
    hdrs = ["fn_lookup.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//core/cells:formula_ast",
        "//core/cells:formula_eval",
        "//core/cells:model",
    ],
)

cc_test(
    name = "fn_lookup_test",
    srcs = ["fn_lookup_test.cc"],
    deps = [
        ":fn_lookup",
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

# Aggregate library that includes all functions
cc_library(
    name = "functions",
    visibility = ["//visibility:public"],
    deps = [
        ":fn_lookup",
        ":fn_rand",
        ":fn_stats",
    ],
)
