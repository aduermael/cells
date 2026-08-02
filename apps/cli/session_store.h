// Session registry on disk: paths, meta files, stale cleanup (pure + FS).

#ifndef APPS_CLI_SESSION_STORE_H_
#define APPS_CLI_SESSION_STORE_H_

#include <cstdint>

#include <optional>
#include <string>
#include <vector>

namespace cells::cli {

// On-disk metadata for a session daemon.
struct SessionMeta {
    std::string id;
    std::string url;
    std::string room;
    std::string name;
    double idle_minutes = 30.0;
    std::int64_t pid = 0;
    std::int64_t started_at_ms = 0;
};

// Resolve root directory for sessions.
// Order: CELLS_SESSION_DIR env, else $XDG_RUNTIME_DIR/cells/sessions,
// else $TMPDIR/cells-sessions-<uid> (Unix) / %TEMP%/cells-sessions (Windows).
std::string session_root_dir();

// Paths for a session id under root.
std::string session_dir(const std::string& root, const std::string& id);
std::string session_socket_path(const std::string& root, const std::string& id);
std::string session_meta_path(const std::string& root, const std::string& id);
std::string session_pid_path(const std::string& root, const std::string& id);

// Create session directory (0755). Returns false on failure.
bool create_session_dir(const std::string& root, const std::string& id);

// Write / read meta.json
bool write_session_meta(const std::string& root, const SessionMeta& meta);
std::optional<SessionMeta> read_session_meta(const std::string& root, const std::string& id);

// Encode/decode meta (pure; for tests).
std::string encode_session_meta(const SessionMeta& meta);
std::optional<SessionMeta> decode_session_meta(const std::string& json);

// Write pid file.
bool write_session_pid(const std::string& root, const std::string& id, std::int64_t pid);

// Check whether a process appears alive (kill(pid, 0)).
bool process_alive(std::int64_t pid);

// Remove session directory tree (best-effort).
void remove_session_dir(const std::string& root, const std::string& id);

// List session ids that have a meta file under root.
std::vector<std::string> list_session_ids(const std::string& root);

// One row for `session list` output.
struct SessionListEntry {
    SessionMeta meta;
    bool alive = false;
    std::string socket_path;
};

// Load all sessions; marks alive via pid; optionally prunes dead dirs when prune_dead.
std::vector<SessionListEntry> list_sessions(const std::string& root, bool prune_dead = true);

// Wall-clock milliseconds (for meta timestamps / idle).
std::int64_t now_unix_ms();

}  // namespace cells::cli

#endif  // APPS_CLI_SESSION_STORE_H_
