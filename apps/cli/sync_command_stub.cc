// Stub sync command for builds without networking support
// Used for Alpine Linux builds where libdatachannel is not available

#include "sync_command.h"

#include <iostream>

namespace cells::cli {

int run_sync_command(const SyncOptions& /* opts */) {
    std::cerr << "Error: sync command is not supported in this build\n";
    std::cerr << "This binary was built without networking support.\n";
    return 1;
}

}  // namespace cells::cli
