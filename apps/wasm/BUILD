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
    "-s ENVIRONMENT=web,worker",  # Target web and web workers
    "-s EXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8']",
    "-s WASM=1",  # Emit .wasm file
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
