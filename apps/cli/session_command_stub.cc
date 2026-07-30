// Stub session command for builds without networking support

#include <iostream>

#include "session_command.h"

namespace cells::cli {

void print_session_help(const char* program_name) {
    std::cerr << "Session commands are not supported in this build.\n"
              << "Use the full cells binary (with sync/network support).\n"
              << "Program: " << program_name << "\n";
}

int run_session_command(const SessionCliOptions& /*opts*/) {
    std::cerr << "Error: session command is not supported in this build\n";
    return 1;
}

}  // namespace cells::cli
