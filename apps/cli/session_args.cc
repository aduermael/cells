#include "session_args.h"

#include <sstream>
#include <string_view>

namespace cells::cli {

namespace {

bool is_flag(std::string_view arg) { return !arg.empty() && arg[0] == '-'; }

bool is_help_flag(std::string_view arg) {
    return arg == "--help" || arg == "-h" || arg == "help";
}

}  // namespace

SessionParseResult parse_session_args(int argc, char* argv[]) {
    SessionParseResult result;
    if (argc < 2 || std::string_view(argv[1]) != "session") {
        return result;
    }
    result.is_session = true;

    if (argc < 3) {
        result.options.kind = SessionCommandKind::kHelp;
        return result;
    }

    std::string_view sub = argv[2];
    if (is_help_flag(sub)) {
        result.options.kind = SessionCommandKind::kHelp;
        return result;
    }
    if (sub == "start") {
        result.options.kind = SessionCommandKind::kStart;
    } else if (sub == "list") {
        result.options.kind = SessionCommandKind::kList;
    } else if (sub == "stop") {
        result.options.kind = SessionCommandKind::kStop;
    } else if (sub == "exec") {
        result.options.kind = SessionCommandKind::kExec;
    } else if (sub == "watch") {
        result.options.kind = SessionCommandKind::kWatch;
    } else if (sub == "status") {
        result.options.kind = SessionCommandKind::kStatus;
    } else if (sub == "export") {
        result.options.kind = SessionCommandKind::kExport;
    } else if (sub == "help") {
        result.options.kind = SessionCommandKind::kHelp;
    } else if (sub == "_run") {
        result.options.kind = SessionCommandKind::kDaemon;
    } else {
        result.ok = false;
        result.error = "Unknown session subcommand: " + std::string(sub) +
                       " (try: cells session --help)";
        return result;
    }

    for (int i = 3; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (is_help_flag(arg)) {
            result.options.kind = SessionCommandKind::kHelp;
            return result;
        }
        if (arg == "-q") {
            result.options.quiet = true;
            continue;
        }
        if (arg == "-v") {
            result.options.verbose = true;
            continue;
        }
        if (arg == "--force") {
            result.options.force = true;
            continue;
        }
        if ((arg == "--idle-minutes" || arg == "--idle") && i + 1 < argc) {
            auto m = parse_idle_minutes(argv[++i]);
            if (!m) {
                result.ok = false;
                result.error = "Invalid --idle-minutes value";
                return result;
            }
            result.options.idle_minutes = *m;
            continue;
        }
        if ((arg == "--wait-seconds" || arg == "--wait") && i + 1 < argc) {
            auto m = parse_idle_minutes(argv[++i]);
            if (!m || *m < 0) {
                result.ok = false;
                result.error = "Invalid --wait-seconds value";
                return result;
            }
            result.options.wait_seconds = *m;
            continue;
        }
        if (arg == "--name" && i + 1 < argc) {
            result.options.name = argv[++i];
            continue;
        }
        if ((arg == "--server" || arg == "--url") && i + 1 < argc) {
            result.options.url = argv[++i];
            continue;
        }
        if ((arg == "--file" || arg == "-i") && i + 1 < argc) {
            result.options.input_file = argv[++i];
            result.options.local = true;
            continue;
        }
        if (arg == "--local") {
            result.options.local = true;
            continue;
        }
        if ((arg == "--session" || arg == "--id") && i + 1 < argc) {
            result.options.session_id = argv[++i];
            continue;
        }
        if (arg == "--script" && i + 1 < argc) {
            result.options.script_file = argv[++i];
            continue;
        }
        if (arg == "-e" && i + 1 < argc) {
            result.options.script_inline = argv[++i];
            continue;
        }
        if (arg == "--format" && i + 1 < argc) {
            result.options.export_format = argv[++i];
            continue;
        }
        if ((arg == "--duration" || arg == "--timeout") && i + 1 < argc) {
            auto m = parse_idle_minutes(argv[++i]);
            if (!m || *m < 0) {
                result.ok = false;
                result.error = "Invalid --duration value (seconds)";
                return result;
            }
            result.options.watch_duration_sec = *m;
            continue;
        }
        if (arg == "--socket" && i + 1 < argc) {
            result.options.socket_path = argv[++i];
            continue;
        }
        if (arg == "--root" && i + 1 < argc) {
            result.options.root_dir = argv[++i];
            continue;
        }
        if (is_flag(arg)) {
            result.ok = false;
            result.error = "Unknown option: " + std::string(arg) + " (try: cells session --help)";
            return result;
        }

        // Positionals
        if (result.options.kind == SessionCommandKind::kStart) {
            if (result.options.url.empty() && result.options.input_file.empty()) {
                std::string value(arg);
                if (value.find("://") != std::string::npos) {
                    result.options.url = std::move(value);
                } else {
                    result.options.input_file = std::move(value);
                    result.options.local = true;
                }
            } else {
                result.ok = false;
                result.error = "Unexpected argument: " + std::string(arg);
                return result;
            }
        } else if (result.options.kind == SessionCommandKind::kDaemon) {
            // Prefer flags; allow positional id then url for convenience
            if (result.options.session_id.empty()) {
                result.options.session_id = std::string(arg);
            } else if (result.options.url.empty() && result.options.input_file.empty()) {
                result.options.url = std::string(arg);
            } else {
                result.ok = false;
                result.error = "Unexpected argument: " + std::string(arg);
                return result;
            }
        } else if (result.options.kind == SessionCommandKind::kExport) {
            if (result.options.session_id.empty()) {
                result.options.session_id = std::string(arg);
            } else if (result.options.export_path.empty()) {
                result.options.export_path = std::string(arg);
            } else {
                result.ok = false;
                result.error = "Unexpected argument: " + std::string(arg);
                return result;
            }
        } else if (result.options.kind == SessionCommandKind::kStop ||
                   result.options.kind == SessionCommandKind::kExec ||
                   result.options.kind == SessionCommandKind::kWatch ||
                   result.options.kind == SessionCommandKind::kStatus) {
            if (result.options.session_id.empty()) {
                result.options.session_id = std::string(arg);
            } else {
                result.ok = false;
                result.error = "Unexpected argument: " + std::string(arg);
                return result;
            }
        } else if (result.options.kind == SessionCommandKind::kList ||
                   result.options.kind == SessionCommandKind::kHelp) {
            result.ok = false;
            result.error = "Unexpected argument: " + std::string(arg);
            return result;
        }
    }

    return result;
}

void validate_session_options(SessionParseResult& result) {
    if (!result.is_session || !result.ok) {
        return;
    }
    auto& o = result.options;
    switch (o.kind) {
        case SessionCommandKind::kStart:
            // URL, -i <file>, --local, or neither (empty local workbook) are all valid.
            if (!o.url.empty() && !o.input_file.empty()) {
                result.ok = false;
                result.error = "Pass either a collab URL or -i <file>, not both";
            }
            break;
        case SessionCommandKind::kStop:
        case SessionCommandKind::kStatus:
        case SessionCommandKind::kWatch:
            if (o.session_id.empty()) {
                result.ok = false;
                result.error = "Session id required";
            }
            break;
        case SessionCommandKind::kExec:
            if (o.session_id.empty()) {
                result.ok = false;
                result.error = "Session id required: cells session exec <id> -e '...'";
            } else if (o.script_file.empty() && o.script_inline.empty()) {
                result.ok = false;
                result.error = "Script required: -e '<code>' or --script <file>";
            }
            break;
        case SessionCommandKind::kExport:
            if (o.session_id.empty() || o.export_path.empty()) {
                result.ok = false;
                result.error = "Usage: cells session export <id> <path.(zcd|xlsx|csv)>";
            }
            break;
        case SessionCommandKind::kDaemon:
            if (o.session_id.empty() || o.socket_path.empty()) {
                result.ok = false;
                result.error = "Internal daemon args incomplete";
            }
            break;
        case SessionCommandKind::kList:
        case SessionCommandKind::kHelp:
        case SessionCommandKind::kNone:
            break;
    }
}

std::string session_usage(const char* program_name) {
    std::ostringstream o;
    o << "Session commands (long-running peer for agents; stdout is pure JSON/JSONL):\n"
      << "  " << program_name
      << " session start [-i <file>|--local|<url>] [--idle-minutes N] [--wait-seconds N] "
         "[--name NAME]\n"
      << "  " << program_name << " session list\n"
      << "  " << program_name << " session status <id>\n"
      << "  " << program_name << " session stop <id>\n"
      << "  " << program_name << " session exec <id> -e '<code>' | --script <file> [--force]\n"
      << "  " << program_name << " session export <id> <path.(zcd|xlsx|csv)> [--format FMT]\n"
      << "  " << program_name << " session watch <id> [--duration SECS]\n"
      << "  " << program_name << " session --help\n"
      << "\n"
      << "  -i, --file PATH    Local workbook (xlsx/csv/zcd); no collab URL or network\n"
      << "  --local            Empty local workbook (no file, no collab)\n"
      << "  --idle-minutes N   Auto-stop after N minutes idle (default " << kDefaultIdleMinutes
      << "; fractions ok)\n"
      << "  --wait-seconds N   start: wait for ONLINE (peer sync done) before success (default "
      << kDefaultWaitSeconds << "; 0=no wait). Local sessions are ready immediately.\n"
      << "  --name NAME        Presence display name (default \"CLI Agent\")\n"
      << "  --duration SECS    Watch for at most SECS seconds then exit\n"
      << "  --force            exec even if session is still CONNECTING\n"
      << "\n"
      << "Local sessions: load a file once, exec multiple scripts, export, stop.\n"
      << "Collab start keeps the daemon running even if not ONLINE yet (ready:false + warning).\n"
      << "export writes the live workbook (zcd/xlsx/csv by extension).\n"
      << "Debug: CELLS_SESSION_DEBUG=1 or session dir daemon.log for state transitions.\n";
    return o.str();
}

}  // namespace cells::cli
