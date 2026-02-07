# Alpine Linux CLI Build

Build the Cells CLI binary for Alpine Linux (musl libc).

## Background

The current Bazel setup builds native binaries for macOS and standard Linux (glibc). Alpine Linux uses musl libc, which requires either:
1. Cross-compilation with a musl toolchain
2. Building inside an Alpine container

**Recommendation:** Use a Docker-based build approach. Cross-compiling C++ with Bazel for musl is complex (requires custom toolchain configuration, musl sysroot, etc.). A container-based build is simpler, more reliable, and mirrors the production environment.

## Phase 1: Create Alpine Build Container
- [x] 1a: Create `Dockerfile.alpine-build` with Alpine + Bazel + build dependencies
- [x] 1b: Add build script `tools/cli-alpine.sh` to build CLI inside the container

## Phase 2: Test and Verify
- [x] 2a: Build the Alpine CLI binary and verify it works on Alpine
- [x] 2b: Verify the binary is statically linked or properly linked against musl

## Implementation Notes

### Build Target
The Alpine build uses the `cells-converter` target instead of the full `cells` target. This avoids the OpenSSL/libdatachannel dependencies which have incompatible Bazel rules (rules_perl downloads glibc-based Perl binaries that don't work on musl).

The `cells-converter` target provides all conversion functionality (CSV, XLSX, ZCD) and Luau scripting, but the `sync` command returns an error message explaining it's not available in this build.

### Runtime Requirements
The Alpine binary requires these packages to be installed:
```sh
apk add --no-cache libstdc++ libgcc
```

### Usage
Build the Alpine binary:
```sh
./tools/cli-alpine.sh
```

This creates `dist/cli/cells-alpine` which is linked against musl libc.

### Code Fixes Applied
During implementation, two missing includes were fixed for compatibility with GCC 15.2.0 in Alpine Edge:
1. `core/cells/xlsx_reader.cc` - Added `#include <algorithm>` for initializer list `std::min`/`std::max`
2. `core/cells/formula_eval.h` - Added `#include <cmath>` for `std::floor`/`std::abs`
