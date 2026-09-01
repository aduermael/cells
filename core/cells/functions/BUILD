# Individual formula function implementations
# Source files exported for inclusion in //core/cells:formula_eval

load("@rules_cc//cc:defs.bzl", "cc_test")

package(default_visibility = ["//core/cells:__pkg__"])

# Export source files for inclusion in formula_eval
exports_files([
    "fn_array.cc",
    "fn_array.h",
    "fn_conditional.cc",
    "fn_conditional.h",
    "fn_datetime.cc",
    "fn_datetime.h",
    "fn_engineering.cc",
    "fn_engineering.h",
    "fn_logic.cc",
    "fn_logic.h",
    "fn_lookup.cc",
    "fn_lookup.h",
    "fn_math.cc",
    "fn_math.h",
    "fn_rand.cc",
    "fn_rand.h",
    "fn_stats.cc",
    "fn_stats.h",
    "fn_text.cc",
    "fn_text.h",
])

cc_test(
    name = "fn_conditional_test",
    srcs = ["fn_conditional_test.cc"],
    deps = [
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fn_array_test",
    srcs = ["fn_array_test.cc"],
    shard_count = 4,
    deps = [
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fn_rand_test",
    srcs = ["fn_rand_test.cc"],
    deps = [
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fn_math_test",
    srcs = ["fn_math_test.cc"],
    deps = [
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fn_datetime_test",
    srcs = ["fn_datetime_test.cc"],
    deps = [
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fn_stats_test",
    srcs = ["fn_stats_test.cc"],
    deps = [
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fn_engineering_test",
    srcs = ["fn_engineering_test.cc"],
    deps = [
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fn_logic_test",
    srcs = ["fn_logic_test.cc"],
    deps = [
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fn_text_test",
    srcs = ["fn_text_test.cc"],
    deps = [
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fn_lookup_test",
    srcs = ["fn_lookup_test.cc"],
    deps = [
        "//core/cells:formula_eval",
        "//core/cells:formula_parser",
        "//core/cells:formula_resolver",
        "//core/cells:id",
        "//core/cells:model",
        "@googletest//:gtest_main",
    ],
)
