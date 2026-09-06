load("@hedron_compile_commands//:refresh_compile_commands.bzl", "refresh_compile_commands")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")

# --define=CELLS_NO_COLLAB=1 / --config=no-collab
# Strips the operation ledger (OpLog) and lets CLI targets omit connectivity.
config_setting(
    name = "cells_no_collab",
    define_values = {"CELLS_NO_COLLAB": "1"},
    visibility = ["//visibility:public"],
)

# --define=CELLS_HEADLESS=1 / --config=headless
# Documents the CLI-only (no WASM UI) product cut. The :cli-headless wrapper
# builds the CLI binary and does not invoke WASM/UI targets.
config_setting(
    name = "cells_headless",
    define_values = {"CELLS_HEADLESS": "1"},
    visibility = ["//visibility:public"],
)

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
    data = ["tools/guard.sh"],
)

sh_binary(
    name = "cli-release",
    srcs = ["tools/cli-release.sh"],
    data = ["tools/guard.sh"],
)

# Headless engine (CLI only, no WASM/UI app)
sh_binary(
    name = "cli-headless",
    srcs = ["tools/cli-headless.sh"],
    data = ["tools/guard.sh"],
)

# Production trim: headless + no operation ledger + no connectivity
sh_binary(
    name = "cli-no-collab",
    srcs = ["tools/cli-no-collab.sh"],
    data = ["tools/guard.sh"],
)

# Same as :cli-no-collab — explicit name for combining both flags.
sh_binary(
    name = "cli-headless-no-collab",
    srcs = ["tools/cli-no-collab.sh"],
    data = ["tools/guard.sh"],
)

# Opt-in formula TODO report (mog-derived). Not part of :check.
sh_binary(
    name = "formula-todo",
    srcs = ["tools/formula-todo.sh"],
    data = [
        "tools/guard.sh",
        "//core/cells:formula_todo_report",
        "//testdata/formulas:mog_formula_cases",
    ],
)

# WASM build scripts
sh_binary(
    name = "wasm",
    srcs = ["tools/wasm.sh"],
    data = ["tools/guard.sh"],
)

sh_binary(
    name = "wasm-debug",
    srcs = ["tools/wasm-debug.sh"],
    data = ["tools/guard.sh"],
)

sh_binary(
    name = "wasm-dist",
    srcs = ["tools/wasm-dist.sh"],
    data = ["tools/guard.sh"],
)

# Development server
sh_binary(
    name = "serve",
    srcs = ["tools/serve.sh"],
    data = ["tools/guard.sh"],
)

# Test scripts
sh_binary(
    name = "test",
    srcs = ["tools/test.sh"],
    data = ["tools/guard.sh"],
)

sh_binary(
    name = "test-js",
    srcs = ["tools/test-js.sh"],
    data = ["tools/guard.sh"],
)

# Office.js (QuickJS) tests. Filter: bazel run :officejs -- WriteValues
sh_binary(
    name = "officejs",
    srcs = ["tools/officejs.sh"],
    data = ["tools/guard.sh"],
)

sh_binary(
    name = "release-test",
    srcs = ["tools/release-test.sh"],
    data = ["tools/guard.sh"],
)

sh_binary(
    name = "e2e",
    srcs = ["tools/e2e.sh"],
    data = ["tools/guard.sh"],
)

sh_binary(
    name = "e2e-headed",
    srcs = ["tools/e2e-headed.sh"],
    data = ["tools/guard.sh"],
)

sh_binary(
    name = "xlsx-roundtrip",
    srcs = ["tools/xlsx-roundtrip.sh"],
    data = ["tools/guard.sh"],
)

# Excel golden generator (Windows COM). excel-save errors off Windows.
sh_binary(
    name = "excel-verify",
    srcs = ["tools/excel-verify.sh"],
    data = ["tools/guard.sh"],
)

# Code quality scripts
sh_binary(
    name = "check",
    srcs = ["tools/check.sh"],
    data = ["tools/guard.sh"],
)

sh_binary(
    name = "format",
    srcs = ["tools/format.sh"],
    data = ["tools/guard.sh"],
)

sh_binary(
    name = "lint",
    srcs = ["tools/lint.sh"],
    data = ["tools/guard.sh"],
)

sh_binary(
    name = "vet",
    srcs = ["tools/vet.sh"],
)

sh_binary(
    name = "check-types",
    srcs = ["tools/check-types.sh"],
    data = ["tools/guard.sh"],
)
