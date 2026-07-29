#include "sync_args.h"

#include <string_view>

namespace cells::cli {

SyncParseResult parse_sync_args(int argc, char* argv[]) {
    SyncParseResult result;
    if (argc < 2 || std::string_view(argv[1]) != "sync") {
        return result;
    }
    result.is_sync = true;

    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-q") {
            result.options.quiet = true;
            continue;
        }
        if (arg == "-v") {
            result.options.verbose = true;
            continue;
        }
        if (arg == "--ops-only") {
            result.options.ops_only = true;
            continue;
        }
        if ((arg == "--server" || arg == "--url") && i + 1 < argc) {
            result.options.url = argv[++i];
            continue;
        }
        if (arg == "--apply" && i + 1 < argc) {
            result.options.apply = true;
            result.options.workbook_file = argv[++i];
            continue;
        }
        if (arg == "--send" && i + 1 < argc) {
            result.options.send = true;
            result.options.workbook_file = argv[++i];
            continue;
        }
        if (!arg.empty() && arg[0] != '-') {
            if (result.options.url.empty()) {
                result.options.url = std::string(arg);
            } else {
                result.ok = false;
                result.error = "Unexpected argument: " + std::string(arg);
                return result;
            }
            continue;
        }
        result.ok = false;
        result.error = "Unknown option: " + std::string(arg);
        return result;
    }

    return result;
}

}  // namespace cells::cli
