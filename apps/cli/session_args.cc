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
            if (result.options.url.empty()) {
                result.options.url = std::string(arg);
            } else {
                result.ok = false;
                result.error = "Unexpected argument: " + std::string(arg);
                return result;
            }
        } else if (result.options.kind == SessionCommandKind::kDaemon) {
            // Prefer flags; allow positional id then url for convenience
            if (result.options.session_id.empty()) {
                result.options.session_id = std::string(arg);
            } else if (result.options.url.empty()) {
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
            if (o.url.empty()) {
                result.ok = false;
                result.error = "URL required: cells session start <url>";
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
            if (o.session_id.empty() || o.url.empty() || o.socket_path.empty()) {
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
    o << "Session commands (long-running collab peer for agents; stdout is pure JSON/JSONL):\n"
      << "  " << program_name
      << " session start <url> [--idle-minutes N] [--wait-seconds N] [--name NAME]\n"
      << "  " << program_name << " session list\n"
      << "  " << program_name << " session status <id>\n"
      << "  " << program_name << " session stop <id>\n"
      << "  " << program_name << " session exec <id> -e '<code>' | --script <file> [--force]\n"
      << "  " << program_name << " session export <id> <path.(zcd|xlsx|csv)> [--format FMT]\n"
      << "  " << program_name << " session watch <id> [--duration SECS]\n"
      << "  " << program_name << " session --help\n"
      << "\n"
      << "  --idle-minutes N   Auto-stop after N minutes idle (default " << kDefaultIdleMinutes
      << "; fractions ok)\n"
      << "  --wait-seconds N   start: wait for ONLINE/SYNCING before success (default "
      << kDefaultWaitSeconds << "; 0=no wait)\n"
      << "  --name NAME        Presence display name (default \"CLI Agent\")\n"
      << "  --duration SECS    Watch for at most SECS seconds then exit\n"
      << "  --force            exec even if session is still CONNECTING\n"
      << "\n"
      << "start fails if still CONNECTING after --wait-seconds (daemon stopped).\n"
      << "export writes the live workbook (zcd/xlsx/csv by extension).\n"
      << "Prefer session over one-shot `cells sync` for multi-step agent work.\n";
    return o.str();
}

}  // namespace cells::cli
