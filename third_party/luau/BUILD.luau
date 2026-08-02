# BUILD file for Luau scripting language
# https://github.com/luau-lang/luau - MIT License
#
# Luau is a fast, safe, gradually typed scripting language derived from Lua.
# This BUILD file defines cc_library targets for the VM, Compiler, and AST.

load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

# MSVC rejects GCC-style -std= and -Wno-* flags; use platform selects.
_CXX17 = select({
    "@platforms//os:windows": ["/std:c++17"],
    "//conditions:default": ["-std=c++17"],
})

_CXX11 = select({
    "@platforms//os:windows": ["/std:c++11"],
    "//conditions:default": ["-std=c++11"],
})

# GCC/Clang-only warning suppressions for upstream Luau sources.
_GCC_WARN_UNUSED = select({
    "@platforms//os:windows": [],
    "//conditions:default": [
        "-Wno-unused-variable",
        "-Wno-unused-but-set-variable",
    ],
})

# Common utilities shared across Luau components
cc_library(
    name = "luau_common",
    srcs = [
        "Common/src/StringUtils.cpp",
        "Common/src/TimeTrace.cpp",
    ],
    hdrs = glob(["Common/include/Luau/*.h"]),
    copts = _CXX17,
    includes = ["Common/include"],
)

# Abstract Syntax Tree representation
cc_library(
    name = "luau_ast",
    srcs = [
        "Ast/src/Allocator.cpp",
        "Ast/src/Ast.cpp",
        "Ast/src/Confusables.cpp",
        "Ast/src/Cst.cpp",
        "Ast/src/Lexer.cpp",
        "Ast/src/Location.cpp",
        "Ast/src/Parser.cpp",
        "Ast/src/PrettyPrinter.cpp",
    ],
    hdrs = glob(["Ast/include/Luau/*.h"]),
    copts = _CXX17,
    includes = ["Ast/include"],
    deps = [":luau_common"],
)

# Compiler: transforms source code into bytecode
cc_library(
    name = "luau_compiler",
    srcs = [
        "Compiler/src/Builtins.cpp",
        "Compiler/src/BuiltinFolding.cpp",
        "Compiler/src/BytecodeBuilder.cpp",
        "Compiler/src/Compiler.cpp",
        "Compiler/src/ConstantFolding.cpp",
        "Compiler/src/CostModel.cpp",
        "Compiler/src/lcode.cpp",
        "Compiler/src/TableShape.cpp",
        "Compiler/src/Types.cpp",
        "Compiler/src/ValueTracking.cpp",
    ],
    hdrs = glob([
        "Compiler/include/Luau/*.h",
        "Compiler/include/luacode.h",
        "Compiler/src/*.h",
    ]),
    copts = _CXX17 + _GCC_WARN_UNUSED,
    includes = [
        "Compiler/include",
        "Compiler/src",
    ],
    deps = [":luau_ast"],
)

# Virtual Machine: executes compiled bytecode
# Uses C++11 for maximum compatibility (can run in constrained environments)
cc_library(
    name = "luau_vm",
    srcs = [
        "VM/src/lapi.cpp",
        "VM/src/laux.cpp",
        "VM/src/lbaselib.cpp",
        "VM/src/lbitlib.cpp",
        "VM/src/lbuffer.cpp",
        "VM/src/lbuflib.cpp",
        "VM/src/lbuiltins.cpp",
        "VM/src/lcorolib.cpp",
        "VM/src/ldblib.cpp",
        "VM/src/ldebug.cpp",
        "VM/src/ldo.cpp",
        "VM/src/lfunc.cpp",
        "VM/src/lgc.cpp",
        "VM/src/lgcdebug.cpp",
        "VM/src/linit.cpp",
        "VM/src/lmathlib.cpp",
        "VM/src/lmem.cpp",
        "VM/src/lnumprint.cpp",
        "VM/src/lobject.cpp",
        "VM/src/loslib.cpp",
        "VM/src/lperf.cpp",
        "VM/src/lstate.cpp",
        "VM/src/lstring.cpp",
        "VM/src/lstrlib.cpp",
        "VM/src/ltable.cpp",
        "VM/src/ltablib.cpp",
        "VM/src/ltm.cpp",
        "VM/src/ludata.cpp",
        "VM/src/lutf8lib.cpp",
        "VM/src/lveclib.cpp",
        "VM/src/lvmexecute.cpp",
        "VM/src/lvmload.cpp",
        "VM/src/lvmutils.cpp",
    ],
    hdrs = glob([
        "VM/include/*.h",
        "VM/src/*.h",
    ]),
    copts = _CXX11,
    includes = [
        "VM/include",
        "VM/src",
    ],
    deps = [":luau_common"],
)

# Configuration (required for Analysis)
cc_library(
    name = "luau_config",
    srcs = [
        "Config/src/Config.cpp",
        "Config/src/LinterConfig.cpp",
        "Config/src/LuauConfig.cpp",
    ],
    hdrs = glob(["Config/include/Luau/*.h"]),
    copts = _CXX17,
    includes = ["Config/include"],
    deps = [
        ":luau_ast",
        ":luau_compiler",
        ":luau_vm",
    ],
)

# Analysis: type checking, linting, and autocomplete
# NativeStackGuard.cpp uses GetCurrentThreadStackLimits (Windows 8+).
# Bazel's default MSVC _WIN32_WINNT can be too low; pin to Win10.
_WIN32_STACK_API = select({
    "@platforms//os:windows": [
        "_WIN32_WINNT=0x0A00",
        "WINVER=0x0A00",
    ],
    "//conditions:default": [],
})

cc_library(
    name = "luau_analysis",
    srcs = glob([
        "Analysis/src/*.cpp",
        "Analysis/src/*.h",
    ]),
    hdrs = glob(["Analysis/include/Luau/*.h"]),
    copts = _CXX17 + _GCC_WARN_UNUSED,
    includes = ["Analysis/include"],
    local_defines = _WIN32_STACK_API,
    deps = [
        ":luau_ast",
        ":luau_compiler",
        ":luau_config",
        ":luau_vm",
    ],
)

# Combined target for easy linking: VM + Compiler + AST
# This provides everything needed to compile and execute Luau scripts
cc_library(
    name = "luau",
    deps = [
        ":luau_ast",
        ":luau_compiler",
        ":luau_vm",
    ],
)
