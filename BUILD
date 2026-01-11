load("@hedron_compile_commands//:refresh_compile_commands.bzl", "refresh_compile_commands")

refresh_compile_commands(
    name = "refresh_compile_commands",
    targets = {
        "//core/...": "",
    },
)

# CLI build scripts
sh_binary(
    name = "cli",
    srcs = ["tools/cli.sh"],
)

sh_binary(
    name = "cli-release",
    srcs = ["tools/cli-release.sh"],
)

# WASM build scripts
sh_binary(
    name = "wasm",
    srcs = ["tools/wasm.sh"],
)

sh_binary(
    name = "wasm-debug",
    srcs = ["tools/wasm-debug.sh"],
)

sh_binary(
    name = "wasm-dist",
    srcs = ["tools/wasm-dist.sh"],
)

# Development server
sh_binary(
    name = "serve",
    srcs = ["tools/serve.sh"],
)

# Test scripts
sh_binary(
    name = "test",
    srcs = ["tools/test.sh"],
)

sh_binary(
    name = "e2e",
    srcs = ["tools/e2e.sh"],
)

sh_binary(
    name = "e2e-headed",
    srcs = ["tools/e2e-headed.sh"],
)

# Code quality scripts
sh_binary(
    name = "check",
    srcs = ["tools/check.sh"],
)

sh_binary(
    name = "format",
    srcs = ["tools/format.sh"],
)

sh_binary(
    name = "lint",
    srcs = ["tools/lint.sh"],
)

sh_binary(
    name = "check-types",
    srcs = ["tools/check-types.sh"],
)
