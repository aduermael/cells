// Stub session command for builds without networking support

#include "session_command.h"

#include <iostream>

namespace cells::cli {

void print_session_help(const char* program_name) {
    (void)program_name;
}

int run_session_command(const SessionCliOptions& /*opts*/) {
    std::cout
        << "{\"ok\":false,\"error\":\"session command is not supported in this build\"}\n";
    return 1;
}

}  // namespace cells::cli
