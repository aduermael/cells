load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")

package(default_visibility = ["//visibility:public"])

# Common Emscripten linker options for the cells WASM build
EMSCRIPTEN_LINKOPTS = [
    "--bind",  # Enable Embind for C++/JavaScript interop
    "-s MODULARIZE=1",  # Export as module for manual initialization
    "-s EXPORT_NAME=createCellsModule",  # Module factory name
    "-s ALLOW_MEMORY_GROWTH=1",  # Allow heap to grow for large spreadsheets
    "-s INITIAL_MEMORY=33554432",  # 32MB initial memory
    "-s MAXIMUM_MEMORY=536870912",  # 512MB max memory
    "-s ENVIRONMENT=web,worker,node",  # Target web, web workers, and node (for testing)
    "-s EXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8']",
    "-s WASM=1",  # Emit .wasm file
    "-fexceptions",  # Enable C++ exceptions (needed for pugixml XPath)
    "-s DISABLE_EXCEPTION_CATCHING=0",  # Enable exception catching in WASM
]

# Hello world test binary
cc_binary(
    name = "hello_wasm_bin",
    srcs = ["hello.cc"],
    linkopts = EMSCRIPTEN_LINKOPTS,
)

wasm_cc_binary(
    name = "hello_wasm",
    cc_target = ":hello_wasm_bin",
)

# Main cells WASM binary with full bindings (no XLSX - see cells_wasm_full)
# XLSX support requires C++ exceptions which adds ~1MB to binary size
cc_binary(
    name = "cells_wasm_bin",
    srcs = ["bindings.cc"],
    copts = ["-DCELLS_WASM_NO_XLSX"],  # Disable XLSX in bindings
    linkopts = EMSCRIPTEN_LINKOPTS + [
        "-s SINGLE_FILE=0",  # Keep .wasm separate for better caching
    ],
    deps = [
        "//core/cells:csv_reader",
        "//core/cells:csv_writer",
        "//core/cells:id",
        "//core/cells:model",
        "//core/cells:parser",
        "//core/cells:quadtree",
        "//core/cells:ref_converter",
        "//core/cells:serializer",
    ],
)

wasm_cc_binary(
    name = "cells_wasm",
    cc_target = ":cells_wasm_bin",
)

# Full WASM binary with XLSX support (requires exceptions, larger binary)
cc_binary(
    name = "cells_wasm_full_bin",
    srcs = ["bindings.cc"],
    copts = ["-fexceptions"],  # Enable exceptions for pugixml
    linkopts = EMSCRIPTEN_LINKOPTS + [
        "-s SINGLE_FILE=0",
        "-fexceptions",
        "-s DISABLE_EXCEPTION_CATCHING=0",
    ],
    deps = [
        "//core/cells:csv_reader",
        "//core/cells:csv_writer",
        "//core/cells:id",
        "//core/cells:model",
        "//core/cells:parser",
        "//core/cells:quadtree",
        "//core/cells:ref_converter",
        "//core/cells:serializer",
        "//core/cells:xlsx_reader",
        "//core/cells:xlsx_writer",
    ],
)

wasm_cc_binary(
    name = "cells_wasm_full",
    cc_target = ":cells_wasm_full_bin",
)
