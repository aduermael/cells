cc_binary(
    name = "generate_test_files",
    srcs = ["generate_test_files.cc"],
    deps = ["@openxlsx"],
)

filegroup(
    name = "sample_xlsx_files",
    srcs = glob(["*.xlsx"], allow_empty = True),
    visibility = ["//visibility:public"],
)
