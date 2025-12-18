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
    # Copy external headers (pugixml, zippy, nowide) to the include directory
    # so that <external/pugixml/pugixml.hpp> includes work
    postfix_script = """
        mkdir -p $INSTALLDIR/include/external
        cp -r $EXT_BUILD_ROOT/external/+_repo_rules+openxlsx/OpenXLSX/external/pugixml $INSTALLDIR/include/external/
        cp -r $EXT_BUILD_ROOT/external/+_repo_rules+openxlsx/OpenXLSX/external/zippy $INSTALLDIR/include/external/
        cp -r $EXT_BUILD_ROOT/external/+_repo_rules+openxlsx/OpenXLSX/external/nowide $INSTALLDIR/include/external/
    """,
    visibility = ["//visibility:public"],
)
