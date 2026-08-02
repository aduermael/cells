// Stub session command for builds without session daemon support.
// Used for converter-only / Alpine lightweight builds (full CLI uses session_command.cc).

#include "session_command.h"

#include <iostream>

namespace cells::cli {

void print_session_help(const char* program_name) {
    (void)program_name;
}

int run_session_command(const SessionCliOptions& /*opts*/) {
    std::cout << "{\"ok\":false,\"error\":\"session command is not supported in this build "
                 "(converter-only); use the full cells binary for collab sessions\"}\n";
    return 1;
}

}  // namespace cells::cli
