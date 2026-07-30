// Session subcommand argument parsing (pure, testable).

#ifndef APPS_CLI_SESSION_ARGS_H_
#define APPS_CLI_SESSION_ARGS_H_

#include <string>
#include <vector>

#include "session_protocol.h"

namespace cells::cli {

enum class SessionCommandKind {
    kNone = 0,  // argv is not a session command
    kStart,
    kList,
    kStop,
    kExec,
    kWatch,
    kStatus,
    kDaemon,  // internal: cells session _run ...
};

struct SessionCliOptions {
    SessionCommandKind kind = SessionCommandKind::kNone;
    std::string url;                 // start / daemon
    std::string session_id;          // stop / exec / watch / status / daemon
    std::string script_file;         // exec --script
    std::string script_inline;       // exec -e
    std::string name = "CLI Agent";  // presence name
    double idle_minutes = kDefaultIdleMinutes;
    // watch: optional max duration in seconds (0 = until interrupted)
    double watch_duration_sec = 0;
    bool quiet = false;
    bool verbose = false;
    // daemon-only
    std::string socket_path;
    std::string root_dir;
};

struct SessionParseResult {
    bool is_session = false;  // true when argv[1] == "session"
    bool ok = true;
    std::string error;
    SessionCliOptions options;
};

// Parse `cells session ...` argv. If not session, is_session=false.
// Does not validate required fields for each kind (caller does).
SessionParseResult parse_session_args(int argc, char* argv[]);

// Validate required fields for the parsed kind. Sets ok/error.
void validate_session_options(SessionParseResult& result);

// Usage text for session subcommands.
std::string session_usage(const char* program_name);

}  // namespace cells::cli

#endif  // APPS_CLI_SESSION_ARGS_H_
