filegroup(
    name = "sample_xlsx_files",
    srcs = glob(["*.xlsx"], allow_empty = True),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "xml_roundtrip_extra",
    srcs = glob(["xml-roundtrip/*.xlsx"], allow_empty = True) +
           glob(["corpus/**/*.xlsx"], allow_empty = True),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "xml_roundtrip_corpus",
    srcs = [
        ":sample_xlsx_files",
        ":xml_roundtrip_extra",
        "//tests/excel-roundtrips/data:xlsx_files",
    ],
    visibility = ["//visibility:public"],
)
