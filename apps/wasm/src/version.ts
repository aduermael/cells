// =============================================================================
// Cells version (web UI)
// =============================================================================
// Default for unstamped source / unit tests. WASM/TS builds inject the real
// product version via esbuild --define:__CELLS_VERSION__='"x.y.z"' (resolved
// from CELLS_VERSION env, else nearest git semver tag, else this default).
// Keep the fallback in sync with scripts/release/common.sh CELLS_DEFAULT_VERSION
// and apps/cli/cli_version.h (enforced by release_test.sh "default version sync").

/** Injected at bundle time; may be undeclared in unstamped source. */
declare const __CELLS_VERSION__: string | undefined;
declare const __CELLS_BUILD_ID__: string | undefined;

/** Product version string (no leading "v"). */
export const CELLS_VERSION: string =
  typeof __CELLS_VERSION__ !== "undefined" ? __CELLS_VERSION__ : "0.0.1";

/** Per-build stamp so worker.js / wasm glue are not served from a stale cache. */
export const CELLS_BUILD_ID: string =
  typeof __CELLS_BUILD_ID__ !== "undefined" ? __CELLS_BUILD_ID__ : "dev";
