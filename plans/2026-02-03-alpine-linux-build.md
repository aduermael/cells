# Alpine Linux CLI Build

Build the Cells CLI binary for Alpine Linux (musl libc).

## Background

The current Bazel setup builds native binaries for macOS and standard Linux (glibc). Alpine Linux uses musl libc, which requires either:
1. Cross-compilation with a musl toolchain
2. Building inside an Alpine container

**Recommendation:** Use a Docker-based build approach. Cross-compiling C++ with Bazel for musl is complex (requires custom toolchain configuration, musl sysroot, etc.). A container-based build is simpler, more reliable, and mirrors the production environment.

## Phase 1: Create Alpine Build Container
- [ ] 1a: Create `Dockerfile.alpine-build` with Alpine + Bazel + build dependencies
- [ ] 1b: Add build script `tools/cli-alpine.sh` to build CLI inside the container

## Phase 2: Test and Verify
- [ ] 2a: Build the Alpine CLI binary and verify it works on Alpine
- [ ] 2b: Verify the binary is statically linked or properly linked against musl
