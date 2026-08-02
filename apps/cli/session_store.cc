#include "session_store.h"

#include <cstdlib>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "output_spill.h"
#include "session_protocol.h"

namespace cells::cli {
namespace {

namespace fs = std::filesystem;

bool mkdir_p(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        return true;
    }
    fs::create_directories(path, ec);
    return fs::is_directory(path, ec);
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << content;
    return static_cast<bool>(out);
}

const char* default_temp_dir() {
#ifdef _WIN32
    if (const char* t = std::getenv("TEMP"); t != nullptr && t[0] != '\0') {
        return t;
    }
    if (const char* t = std::getenv("TMP"); t != nullptr && t[0] != '\0') {
        return t;
    }
    return ".";
#else
    if (const char* t = std::getenv("TMPDIR"); t != nullptr && t[0] != '\0') {
        return t;
    }
    return "/tmp";
#endif
}

}  // namespace

std::string session_root_dir() {
    if (const char* env = std::getenv("CELLS_SESSION_DIR")) {
        if (env[0] != '\0') {
            return std::string(env);
        }
    }
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR")) {
        if (xdg[0] != '\0') {
            return std::string(xdg) + "/cells/sessions";
        }
    }
    const char* tmp = default_temp_dir();
#ifdef _WIN32
    return std::string(tmp) + "/cells-sessions";
#else
    uid_t uid = ::getuid();
    return std::string(tmp) + "/cells-sessions-" + std::to_string(static_cast<unsigned long>(uid));
#endif
}

std::string session_dir(const std::string& root, const std::string& id) {
    return root + "/" + id;
}

std::string session_socket_path(const std::string& root, const std::string& id) {
    return session_dir(root, id) + "/socket";
}

std::string session_meta_path(const std::string& root, const std::string& id) {
    return session_dir(root, id) + "/meta.json";
}

std::string session_pid_path(const std::string& root, const std::string& id) {
    return session_dir(root, id) + "/pid";
}

bool create_session_dir(const std::string& root, const std::string& id) {
    if (!mkdir_p(root)) {
        return false;
    }
    return mkdir_p(session_dir(root, id));
}

std::string encode_session_meta(const SessionMeta& meta) {
    std::ostringstream o;
    o << "{"
      << "\"id\":\"" << json_escape(meta.id) << "\","
      << "\"url\":\"" << json_escape(meta.url) << "\","
      << "\"room\":\"" << json_escape(meta.room) << "\","
      << "\"name\":\"" << json_escape(meta.name) << "\","
      << "\"idle_minutes\":" << meta.idle_minutes << ","
      << "\"pid\":" << meta.pid << ","
      << "\"started_at_ms\":" << meta.started_at_ms << "}";
    return o.str();
}

std::optional<SessionMeta> decode_session_meta(const std::string& json) {
    if (json.empty() || json.front() != '{') {
        return std::nullopt;
    }
    SessionMeta m;
    m.id = json_get_string(json, "id");
    m.url = json_get_string(json, "url");
    m.room = json_get_string(json, "room");
    m.name = json_get_string(json, "name");
    if (auto idle = json_get_number(json, "idle_minutes")) {
        m.idle_minutes = *idle;
    }
    if (auto pid = json_get_number(json, "pid")) {
        m.pid = static_cast<std::int64_t>(*pid);
    }
    if (auto started = json_get_number(json, "started_at_ms")) {
        m.started_at_ms = static_cast<std::int64_t>(*started);
    }
    if (m.id.empty()) {
        return std::nullopt;
    }
    return m;
}

bool write_session_meta(const std::string& root, const SessionMeta& meta) {
    return write_file(session_meta_path(root, meta.id), encode_session_meta(meta));
}

std::optional<SessionMeta> read_session_meta(const std::string& root, const std::string& id) {
    std::string content = read_file(session_meta_path(root, id));
    if (content.empty()) {
        return std::nullopt;
    }
    return decode_session_meta(content);
}

bool write_session_pid(const std::string& root, const std::string& id, std::int64_t pid) {
    return write_file(session_pid_path(root, id), std::to_string(pid) + "\n");
}

bool process_alive(std::int64_t pid) {
    if (pid <= 0) {
        return false;
    }
#ifdef _WIN32
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (h == nullptr) {
        return false;
    }
    DWORD exit_code = 0;
    const bool alive = ::GetExitCodeProcess(h, &exit_code) && exit_code == STILL_ACTIVE;
    ::CloseHandle(h);
    return alive;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

void remove_session_dir(const std::string& root, const std::string& id) {
    std::error_code ec;
    fs::remove(session_socket_path(root, id), ec);
    fs::remove(session_meta_path(root, id), ec);
    fs::remove(session_pid_path(root, id), ec);
    fs::remove(session_dir(root, id), ec);
}

std::vector<std::string> list_session_ids(const std::string& root) {
    std::vector<std::string> ids;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return ids;
    }
    for (const auto& ent : fs::directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (!ent.is_directory(ec)) {
            continue;
        }
        const std::string id = ent.path().filename().string();
        if (id.empty() || id[0] == '.') {
            continue;
        }
        std::error_code meta_ec;
        if (fs::is_regular_file(session_meta_path(root, id), meta_ec)) {
            ids.push_back(id);
        }
    }
    return ids;
}

std::vector<SessionListEntry> list_sessions(const std::string& root, bool prune_dead) {
    std::vector<SessionListEntry> out;
    for (const auto& id : list_session_ids(root)) {
        auto meta = read_session_meta(root, id);
        if (!meta) {
            if (prune_dead) {
                remove_session_dir(root, id);
            }
            continue;
        }
        SessionListEntry e;
        e.meta = *meta;
        e.socket_path = session_socket_path(root, id);
        e.alive = process_alive(meta->pid);
        if (!e.alive && prune_dead) {
            remove_session_dir(root, id);
            continue;
        }
        out.push_back(std::move(e));
    }
    return out;
}

std::int64_t now_unix_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace cells::cli
