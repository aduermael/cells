// CLI version string. Release builds stamp this via -DCELLS_VERSION=\"x.y.z\".
// Unstamped default must stay in sync with scripts/release/common.sh
// CELLS_DEFAULT_VERSION and apps/wasm/src/version.ts (release_test.sh).

#ifndef APPS_CLI_CLI_VERSION_H_
#define APPS_CLI_CLI_VERSION_H_

#ifndef CELLS_VERSION
#define CELLS_VERSION "0.0.1"
#endif

namespace cells::cli {

inline const char* cli_version() { return CELLS_VERSION; }

}  // namespace cells::cli

#endif  // APPS_CLI_CLI_VERSION_H_
