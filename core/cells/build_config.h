// Compile-time product cuts.
//
// CELLS_NO_COLLAB: skip the operation ledger (OpLog) and connectivity. Set by
// Bazel --config=no-collab (--define=CELLS_NO_COLLAB=1).
// CELLS_HEADLESS: CLI-only product (no WASM/UI). Set by --config=headless.

#ifndef CELLS_BUILD_CONFIG_H_
#define CELLS_BUILD_CONFIG_H_

namespace cells {

inline constexpr bool kCollabBuilt =
#if defined(CELLS_NO_COLLAB)
    false
#else
    true
#endif
    ;

inline constexpr bool kHeadlessBuild =
#if defined(CELLS_HEADLESS)
    true
#else
    false
#endif
    ;

}  // namespace cells

#endif  // CELLS_BUILD_CONFIG_H_
