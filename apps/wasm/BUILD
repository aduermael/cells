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
    "-s EXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8','HEAPU8']",
    "-s EXPORTED_FUNCTIONS=['_malloc','_free']",
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

# Main cells WASM binary with full bindings including XLSX support
# Requires --config=wasm to enable exceptions for pugixml dependency
cc_binary(
    name = "cells_wasm_bin",
    srcs = ["bindings.cc"],
    linkopts = EMSCRIPTEN_LINKOPTS + [
        "-s SINGLE_FILE=0",  # Keep .wasm separate for better caching
        "-lwebsocket.js",  # WebSocket support for networking
        "-s FETCH=1",  # HTTP fetch support for networking
    ],
    deps = [
        "//core/cells:crdt",
        "//core/cells:csv_reader",
        "//core/cells:csv_writer",
        "//core/cells:hlc",
        "//core/cells:id",
        "//core/cells:model",
        "//core/cells:operation",
        "//core/cells:oplog",
        "//core/cells:parser",
        "//core/cells:quadtree",
        "//core/cells:ref_converter",
        "//core/cells:serializer",
        "//core/cells:sync_manager",
        "//core/cells:xlsx_reader",
        "//core/cells:xlsx_writer",
        "//core/log",
        "//core/net:sync_client",
    ],
)

wasm_cc_binary(
    name = "cells_wasm",
    cc_target = ":cells_wasm_bin",
)

# JavaScript worker files (for packaging)
filegroup(
    name = "worker_files",
    srcs = [
        "client.js",
        "worker.js",
    ],
)

# Test HTML files
filegroup(
    name = "test_files",
    srcs = [
        "test.html",
        "test_worker.html",
    ],
)

# TypeScript type definitions
filegroup(
    name = "types",
    srcs = ["cells.d.ts"],
)

# Standalone HTML for distribution
filegroup(
    name = "static_files",
    srcs = ["static/index.html"],
)

# All files needed for standalone distribution
# Use: bazel build //apps/wasm:dist_files
filegroup(
    name = "dist_files",
    srcs = [
        ":static_files",
        ":worker_files",
        ":types",
    ],
)
