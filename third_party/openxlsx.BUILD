load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

filegroup(
    name = "all_srcs",
    srcs = glob(["**"]),
)

cmake(
    name = "openxlsx",
    lib_source = ":all_srcs",
    out_static_libs = ["libOpenXLSX.a"],
    cache_entries = {
        "CMAKE_BUILD_TYPE": "Release",
        "OPENXLSX_CREATE_DOCS": "OFF",
        "OPENXLSX_BUILD_TESTS": "OFF",
        "OPENXLSX_BUILD_SAMPLES": "OFF",
        "OPENXLSX_BUILD_BENCHMARKS": "OFF",
    },
    visibility = ["//visibility:public"],
)
