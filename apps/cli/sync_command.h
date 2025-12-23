// Sync command for CLI
// Joins a room and logs all operations received from peers

#ifndef APPS_CLI_SYNC_COMMAND_H_
#define APPS_CLI_SYNC_COMMAND_H_

#include <string>

namespace cells::cli {

// Options for the sync command
struct SyncOptions {
    std::string url;           // URL to sync with (http:// or https://)
    std::string workbook_file; // Optional: workbook file for --apply or --send
    bool apply = false;        // Apply incoming operations to workbook
    bool send = false;         // Broadcast workbook cells as operations
    bool quiet = false;        // Suppress status messages
    bool verbose = false;      // Verbose output
};

// Run the sync command
// Returns 0 on success, 1 on error
int run_sync_command(const SyncOptions& opts);

}  // namespace cells::cli

#endif  // APPS_CLI_SYNC_COMMAND_H_
