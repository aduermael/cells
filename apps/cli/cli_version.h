// CLI version string. Release builds stamp this via -DCELLS_VERSION=\"x.y.z\".

#ifndef APPS_CLI_CLI_VERSION_H_
#define APPS_CLI_CLI_VERSION_H_

#ifndef CELLS_VERSION
#define CELLS_VERSION "0.0.1"
#endif

namespace cells::cli {

inline const char* cli_version() { return CELLS_VERSION; }

}  // namespace cells::cli

#endif  // APPS_CLI_CLI_VERSION_H_
