#include "session_args.h"

#include <sstream>
#include <string_view>

namespace cells::cli {

namespace {

bool is_flag(std::string_view arg) {
    return !arg.empty() && arg[0] == '-';
}

}  // namespace

SessionParseResult parse_session_args(int argc, char* argv[]) {
    SessionParseResult result;
    if (argc < 2 || std::string_view(argv[1]) != "session") {
        return result;
    }
    result.is_session = true;

    if (argc < 3) {
        result.ok = false;
        result.error = "Missing session subcommand (start|list|stop|exec|watch|status)";
        return result;
    }

    std::string_view sub = argv[2];
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
    } else if (sub == "_run") {
        // Internal daemon entry
        result.options.kind = SessionCommandKind::kDaemon;
    } else {
        result.ok = false;
        result.error = "Unknown session subcommand: " + std::string(sub);
        return result;
    }

    for (int i = 3; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-q") {
            result.options.quiet = true;
            continue;
        }
        if (arg == "-v") {
            result.options.verbose = true;
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
        if ((arg == "--duration" || arg == "--timeout") && i + 1 < argc) {
            auto m = parse_idle_minutes(argv[++i]);  // reuse double parse
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
            result.error = "Unknown option: " + std::string(arg);
            return result;
        }

        // Positional: for start → url; for stop/exec/watch/status → session id
        if (result.options.kind == SessionCommandKind::kStart ||
            result.options.kind == SessionCommandKind::kDaemon) {
            if (result.options.url.empty()) {
                result.options.url = std::string(arg);
            } else if (result.options.session_id.empty() &&
                       result.options.kind == SessionCommandKind::kDaemon) {
                result.options.session_id = std::string(arg);
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
        } else if (result.options.kind == SessionCommandKind::kList) {
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
        case SessionCommandKind::kDaemon:
            if (o.session_id.empty() || o.url.empty() || o.socket_path.empty()) {
                result.ok = false;
                result.error = "Internal daemon args incomplete";
            }
            break;
        case SessionCommandKind::kList:
        case SessionCommandKind::kNone:
            break;
    }
}

std::string session_usage(const char* program_name) {
    std::ostringstream o;
    o << "Session commands (long-running collab peer for agents):\n"
      << "  " << program_name << " session start <url> [--idle-minutes N] [--name NAME]\n"
      << "  " << program_name << " session list\n"
      << "  " << program_name << " session status <id>\n"
      << "  " << program_name << " session stop <id>\n"
      << "  " << program_name << " session exec <id> -e '<code>' | --script <file>\n"
      << "  " << program_name << " session watch <id> [--duration SECS]\n"
      << "\n"
      << "  --idle-minutes N   Auto-stop after N minutes with no action (default "
      << kDefaultIdleMinutes << "; fractions allowed, e.g. 0.05)\n"
      << "  --name NAME        Presence display name (default \"CLI Agent\")\n"
      << "  --duration SECS    Watch for at most SECS seconds then exit\n"
      << "\n"
      << "Start prints JSON: {\"id\",\"url\",\"room\",...}. Pass id to exec/watch/stop.\n"
      << "Prefer session over one-shot `cells sync` for multi-step agent work.\n";
    return o.str();
}

}  // namespace cells::cli
