// Session IPC protocol helpers (pure, testable).
// Line-delimited JSON over a Unix domain socket.

#ifndef APPS_CLI_SESSION_PROTOCOL_H_
#define APPS_CLI_SESSION_PROTOCOL_H_

#include <cstdint>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cells::cli {

// Default idle timeout for agent sessions (30 minutes).
inline constexpr double kDefaultIdleMinutes = 30.0;

// ---------------------------------------------------------------------------
// Room target parsing (no network I/O)
// ---------------------------------------------------------------------------

struct RoomTarget {
    bool ok = false;
    std::string error;
    std::string url;      // original input
    std::string room_id;  // from ?room= or path
    std::string host;
    int port = 0;
    bool secure = false;
    std::string signaling_ws;  // ws(s)://host:port/ws
};

// Parse an HTTP(S) collab URL into room id + signaling WebSocket URL.
// Accepts ?room=<id> or path /<room-id>.
RoomTarget parse_room_target(std::string_view url);

// ---------------------------------------------------------------------------
// Idle timeout pure logic
// ---------------------------------------------------------------------------

// Returns true when idle duration (now - last_activity) exceeds idle_minutes.
// idle_minutes must be > 0; zero/negative never expires (caller should avoid).
bool idle_expired(std::int64_t last_activity_ms, std::int64_t now_ms, double idle_minutes);

// Convert idle minutes to milliseconds (rounded). Returns 0 if idle_minutes <= 0.
std::int64_t idle_minutes_to_ms(double idle_minutes);

// ---------------------------------------------------------------------------
// IPC request / response
// ---------------------------------------------------------------------------

enum class SessionOp {
    kUnknown = 0,
    kPing,
    kStatus,
    kStop,
    kExec,
    kWatch,
    kTouch,
    kExport,
};

struct SessionRequest {
    SessionOp op = SessionOp::kUnknown;
    std::string code;         // exec: inline script
    std::string script_path;  // exec: path to script file
    std::string export_path;  // export: destination file
    std::string format;       // export: optional format override (zcd|csv|xlsx)
    std::string raw;          // original JSON line (debug)
};

struct SessionEvent {
    std::string type;  // state|peer|peer_left|error|op|presence
    std::string message;
    // Optional structured fields for ops
    std::string op_type;
    std::string peer_id;
    std::string target;
    std::string payload;
};

struct SessionResponse {
    bool ok = false;
    std::string error;
    std::string output;  // exec stdout
    bool pong = false;
    bool stopped = false;
    bool watch_end = false;
    // status fields
    std::string id;
    std::string url;
    std::string room;
    std::string state;
    std::string peer_id;
    std::string last_error;
    bool ready = false;  // signaling joined: ONLINE or SYNCING
    int peers = 0;
    std::uint64_t ops_sent = 0;
    std::uint64_t ops_received = 0;
    std::uint64_t cells = 0;
    std::string name;
    double idle_minutes = 0;
    std::int64_t last_activity_ms = 0;
    std::string path;    // export path written
    std::string format;  // export format used
    // single event (watch stream line)
    std::optional<SessionEvent> event;
};

// True when SyncClient has joined the room (ONLINE or SYNCING).
// CONNECTING / OFFLINE / RECONNECTING are not ready.
bool session_state_is_ready(std::string_view state);

// Default seconds to wait for ready after session start.
inline constexpr double kDefaultWaitSeconds = 15.0;

// Parse one JSON request line. Returns nullopt if not valid JSON object.
std::optional<SessionRequest> parse_session_request(std::string_view line);

// Encode responses / events as a single JSON line (no trailing newline).
std::string encode_session_response(const SessionResponse& r);
std::string encode_session_event(const SessionEvent& e);

// Session id: 8 lowercase hex characters.
std::string generate_session_id();

// Parse a double from string (idle minutes). Returns nullopt on failure.
std::optional<double> parse_idle_minutes(std::string_view s);

// Extract a simple JSON string field value: "key":"value" (no nested objects).
// Returns empty if missing. Handles basic escapes.
std::string json_get_string(std::string_view json, std::string_view key);

// Extract a JSON number field (integer or float). Returns nullopt if missing.
std::optional<double> json_get_number(std::string_view json, std::string_view key);

// Extract a JSON bool field.
std::optional<bool> json_get_bool(std::string_view json, std::string_view key);

}  // namespace cells::cli

#endif  // APPS_CLI_SESSION_PROTOCOL_H_
