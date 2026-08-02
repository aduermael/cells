// =============================================================================
// Cells version (web UI)
// =============================================================================
// Default for unstamped source / unit tests. WASM/TS builds inject the real
// product version via esbuild --define:__CELLS_VERSION__='"x.y.z"' (resolved
// from CELLS_VERSION env, else nearest git semver tag, else this default).
// Keep the fallback in sync with apps/cli/cli_version.h when bumping manually.

/** Injected at bundle time; may be undeclared in unstamped source. */
declare const __CELLS_VERSION__: string | undefined;

/** Product version string (no leading "v"). */
export const CELLS_VERSION: string =
  typeof __CELLS_VERSION__ !== "undefined" ? __CELLS_VERSION__ : "0.0.1";
