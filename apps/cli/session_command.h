// Session command entry points: start/list/stop/exec/watch + daemon.

#ifndef APPS_CLI_SESSION_COMMAND_H_
#define APPS_CLI_SESSION_COMMAND_H_

#include "session_args.h"

namespace cells::cli {

// Run a session subcommand. Returns process exit code.
int run_session_command(const SessionCliOptions& opts);

// Print session help to stderr.
void print_session_help(const char* program_name);

}  // namespace cells::cli

#endif  // APPS_CLI_SESSION_COMMAND_H_
