// Sync subcommand argument parsing (testable pure logic)

#ifndef APPS_CLI_SYNC_ARGS_H_
#define APPS_CLI_SYNC_ARGS_H_

#include "sync_command.h"

#include <string>

namespace cells::cli {

// Result of parsing argv for the `sync` subcommand.
struct SyncParseResult {
    bool is_sync = false;     // true when argv[1] is "sync"
    bool ok = true;           // false on parse errors (unknown option, etc.)
    std::string error;        // set when ok is false
    SyncOptions options;
};

// Parse sync subcommand arguments from argv.
// If argv does not start a sync command, returns is_sync=false.
// Does not require a URL to be present (caller validates that).
SyncParseResult parse_sync_args(int argc, char* argv[]);

}  // namespace cells::cli

#endif  // APPS_CLI_SYNC_ARGS_H_
