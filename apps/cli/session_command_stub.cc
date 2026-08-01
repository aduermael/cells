// Stub session command for builds without session daemon support.
// Used for Alpine/converter-only builds and Windows (session IPC not ported yet).

#include "session_command.h"

#include <iostream>

namespace cells::cli {

void print_session_help(const char* program_name) {
    (void)program_name;
}

int run_session_command(const SessionCliOptions& /*opts*/) {
    std::cout << "{\"ok\":false,\"error\":\"session command is not supported in this build"
#ifdef _WIN32
                 " (Windows session IPC not yet implemented; use cells sync for one-shot collab)"
#endif
                 "\"}\n";
    return 1;
}

}  // namespace cells::cli
