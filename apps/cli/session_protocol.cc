#include "session_protocol.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

#include <chrono>
#include <random>
#include <sstream>

#include "output_spill.h"

namespace cells::cli {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

// Find "key" then : then value start. Very small JSON helper (no nested objects).
std::size_t find_key(std::string_view json, std::string_view key) {
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    std::size_t pos = 0;
    while (pos < json.size()) {
        std::size_t k = json.find(needle, pos);
        if (k == std::string_view::npos) {
            return std::string_view::npos;
        }
        // Ensure not a substring of a longer key: quote already bounds it.
        std::size_t after = k + needle.size();
        while (after < json.size() && std::isspace(static_cast<unsigned char>(json[after]))) {
            ++after;
        }
        if (after < json.size() && json[after] == ':') {
            return after + 1;
        }
        pos = k + 1;
    }
    return std::string_view::npos;
}

std::string unescape_json_string(std::string_view body) {
    std::string out;
    out.reserve(body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '\\' && i + 1 < body.size()) {
            char n = body[i + 1];
            switch (n) {
                case '"':
                case '\\':
                case '/':
                    out += n;
                    ++i;
                    break;
                case 'n':
                    out += '\n';
                    ++i;
                    break;
                case 'r':
                    out += '\r';
                    ++i;
                    break;
                case 't':
                    out += '\t';
                    ++i;
                    break;
                case 'u':
                    // skip \uXXXX as literal best-effort (4 hex)
                    if (i + 5 < body.size()) {
                        out += '?';
                        i += 5;
                    } else {
                        out += n;
                        ++i;
                    }
                    break;
                default:
                    out += n;
                    ++i;
                    break;
            }
        } else {
            out += body[i];
        }
    }
    return out;
}

bool parse_http_url(std::string_view url, std::string& scheme, std::string& host, int& port,
                    std::string& path_query) {
    // scheme://host[:port][/path][?query]
    auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos || scheme_end == 0) {
        return false;
    }
    scheme = std::string(url.substr(0, scheme_end));
    for (char& c : scheme) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (scheme != "http" && scheme != "https") {
        return false;
    }

    std::string_view rest = url.substr(scheme_end + 3);
    if (rest.empty()) {
        return false;
    }

    std::size_t path_pos = rest.find('/');
    std::string_view authority =
        path_pos == std::string_view::npos ? rest : rest.substr(0, path_pos);
    path_query =
        path_pos == std::string_view::npos ? std::string("/") : std::string(rest.substr(path_pos));

    if (authority.empty()) {
        return false;
    }

    // host[:port] — ignore userinfo
    auto at = authority.find('@');
    if (at != std::string_view::npos) {
        authority = authority.substr(at + 1);
    }

    // IPv6 not required for agents; skip bracketed form complexity if absent
    auto colon = authority.rfind(':');
    if (colon != std::string_view::npos && authority.find(':') == colon) {
        host = std::string(authority.substr(0, colon));
        std::string port_str(authority.substr(colon + 1));
        if (port_str.empty()) {
            return false;
        }
        char* end = nullptr;
        long p = std::strtol(port_str.c_str(), &end, 10);
        if (end == port_str.c_str() || *end != '\0' || p <= 0 || p > 65535) {
            return false;
        }
        port = static_cast<int>(p);
    } else {
        host = std::string(authority);
        port = (scheme == "https") ? 443 : 80;
    }
    return !host.empty();
}

std::string query_param(std::string_view path_query, std::string_view key) {
    auto q = path_query.find('?');
    if (q == std::string_view::npos) {
        return {};
    }
    std::string_view query = path_query.substr(q + 1);
    std::string prefix = std::string(key) + "=";
    std::size_t pos = 0;
    while (pos < query.size()) {
        std::size_t amp = query.find('&', pos);
        std::string_view part =
            amp == std::string_view::npos ? query.substr(pos) : query.substr(pos, amp - pos);
        if (part.size() >= prefix.size() && part.substr(0, prefix.size()) == prefix) {
            return std::string(part.substr(prefix.size()));
        }
        if (amp == std::string_view::npos) {
            break;
        }
        pos = amp + 1;
    }
    return {};
}

std::string path_room_id(std::string_view path_query) {
    auto q = path_query.find('?');
    std::string_view path = q == std::string_view::npos ? path_query : path_query.substr(0, q);
    if (path.size() > 1 && path[0] == '/') {
        std::string_view rest = path.substr(1);
        // single path segment only
        if (rest.find('/') == std::string_view::npos && !rest.empty()) {
            return std::string(rest);
        }
    }
    return {};
}

}  // namespace

RoomTarget parse_room_target(std::string_view url_in) {
    RoomTarget t;
    t.url = std::string(trim(url_in));
    if (t.url.empty()) {
        t.error = "URL is empty";
        return t;
    }

    std::string scheme;
    std::string path_query;
    if (!parse_http_url(t.url, scheme, t.host, t.port, path_query)) {
        t.error = "Invalid URL (expected http:// or https://)";
        return t;
    }
    t.secure = (scheme == "https");

    t.room_id = query_param(path_query, "room");
    if (t.room_id.empty()) {
        t.room_id = path_room_id(path_query);
    }
    if (t.room_id.empty()) {
        t.error = "No room ID found in URL. Use ?room=<id> or /<room-id>";
        return t;
    }

    std::ostringstream ws;
    ws << (t.secure ? "wss" : "ws") << "://" << t.host;
    bool default_port = (t.secure && t.port == 443) || (!t.secure && t.port == 80);
    if (!default_port) {
        ws << ":" << t.port;
    }
    ws << "/ws";
    t.signaling_ws = ws.str();
    t.ok = true;
    return t;
}

bool idle_expired(std::int64_t last_activity_ms, std::int64_t now_ms, double idle_minutes) {
    if (idle_minutes <= 0.0) {
        return false;
    }
    std::int64_t limit = idle_minutes_to_ms(idle_minutes);
    if (limit <= 0) {
        return false;
    }
    if (now_ms < last_activity_ms) {
        return false;
    }
    return (now_ms - last_activity_ms) >= limit;
}

std::int64_t idle_minutes_to_ms(double idle_minutes) {
    if (idle_minutes <= 0.0) {
        return 0;
    }
    double ms = idle_minutes * 60.0 * 1000.0;
    if (ms < 1.0) {
        return 1;  // minimum 1 ms so very small values still expire
    }
    return static_cast<std::int64_t>(std::llround(ms));
}

std::string json_get_string(std::string_view json, std::string_view key) {
    std::size_t val = find_key(json, key);
    if (val == std::string_view::npos) {
        return {};
    }
    while (val < json.size() && std::isspace(static_cast<unsigned char>(json[val]))) {
        ++val;
    }
    if (val >= json.size() || json[val] != '"') {
        return {};
    }
    ++val;
    std::string body;
    for (std::size_t i = val; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            body += json[i];
            body += json[i + 1];
            ++i;
            continue;
        }
        if (json[i] == '"') {
            return unescape_json_string(body);
        }
        body += json[i];
    }
    return {};
}

std::optional<double> json_get_number(std::string_view json, std::string_view key) {
    std::size_t val = find_key(json, key);
    if (val == std::string_view::npos) {
        return std::nullopt;
    }
    while (val < json.size() && std::isspace(static_cast<unsigned char>(json[val]))) {
        ++val;
    }
    if (val >= json.size()) {
        return std::nullopt;
    }
    std::size_t end = val;
    if (json[end] == '-' || json[end] == '+') {
        ++end;
    }
    bool any = false;
    while (end < json.size() &&
           (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '.' ||
            json[end] == 'e' || json[end] == 'E' || json[end] == '+' || json[end] == '-')) {
        any = true;
        ++end;
    }
    if (!any) {
        return std::nullopt;
    }
    std::string num(json.substr(val, end - val));
    char* e = nullptr;
    double d = std::strtod(num.c_str(), &e);
    if (e == num.c_str()) {
        return std::nullopt;
    }
    return d;
}

std::optional<bool> json_get_bool(std::string_view json, std::string_view key) {
    std::size_t val = find_key(json, key);
    if (val == std::string_view::npos) {
        return std::nullopt;
    }
    while (val < json.size() && std::isspace(static_cast<unsigned char>(json[val]))) {
        ++val;
    }
    if (json.substr(val, 4) == "true") {
        return true;
    }
    if (json.substr(val, 5) == "false") {
        return false;
    }
    return std::nullopt;
}

std::optional<SessionRequest> parse_session_request(std::string_view line) {
    line = trim(line);
    if (line.empty() || line.front() != '{') {
        return std::nullopt;
    }
    SessionRequest req;
    req.raw = std::string(line);
    std::string op = json_get_string(line, "op");
    if (op.empty()) {
        // also accept "cmd"
        op = json_get_string(line, "cmd");
    }
    if (op == "ping") {
        req.op = SessionOp::kPing;
    } else if (op == "status") {
        req.op = SessionOp::kStatus;
    } else if (op == "stop") {
        req.op = SessionOp::kStop;
    } else if (op == "exec") {
        req.op = SessionOp::kExec;
        req.code = json_get_string(line, "code");
        req.script_path = json_get_string(line, "script");
        if (req.script_path.empty()) {
            req.script_path = json_get_string(line, "script_path");
        }
    } else if (op == "watch") {
        req.op = SessionOp::kWatch;
    } else if (op == "touch") {
        req.op = SessionOp::kTouch;
    } else if (op == "export") {
        req.op = SessionOp::kExport;
        req.export_path = json_get_string(line, "path");
        if (req.export_path.empty()) {
            req.export_path = json_get_string(line, "file");
        }
        req.format = json_get_string(line, "format");
    } else {
        req.op = SessionOp::kUnknown;
    }
    return req;
}

bool session_state_is_ready(std::string_view state) {
    return state == "ONLINE" || state == "SYNCING";
}

std::string encode_session_response(const SessionResponse& r) {
    std::ostringstream o;
    o << "{\"ok\":" << (r.ok ? "true" : "false");
    if (!r.error.empty()) {
        o << ",\"error\":\"" << json_escape(r.error) << "\"";
    }
    if (r.pong) {
        o << ",\"pong\":true";
    }
    if (r.stopped) {
        o << ",\"stopped\":true";
    }
    if (r.watch_end) {
        o << ",\"watch_end\":true";
    }
    if (!r.output.empty()) {
        o << ",\"output\":\"" << json_escape(r.output) << "\"";
    }
    if (!r.id.empty()) {
        o << ",\"id\":\"" << json_escape(r.id) << "\"";
    }
    if (!r.url.empty()) {
        o << ",\"url\":\"" << json_escape(r.url) << "\"";
    }
    if (!r.room.empty()) {
        o << ",\"room\":\"" << json_escape(r.room) << "\"";
    }
    if (!r.state.empty()) {
        o << ",\"state\":\"" << json_escape(r.state) << "\"";
    }
    if (!r.name.empty()) {
        o << ",\"name\":\"" << json_escape(r.name) << "\"";
    }
    if (!r.peer_id.empty()) {
        o << ",\"peer_id\":\"" << json_escape(r.peer_id) << "\"";
    }
    if (!r.last_error.empty()) {
        o << ",\"last_error\":\"" << json_escape(r.last_error) << "\"";
    }
    if (!r.id.empty() || r.ready) {
        o << ",\"ready\":" << (r.ready ? "true" : "false");
    }
    if (r.peers != 0 || !r.id.empty()) {
        o << ",\"peers\":" << r.peers;
    }
    if (r.ops_sent != 0 || r.ops_received != 0 || !r.id.empty()) {
        o << ",\"ops_sent\":" << r.ops_sent << ",\"ops_received\":" << r.ops_received;
    }
    if (r.cells != 0 || !r.id.empty()) {
        o << ",\"cells\":" << r.cells;
    }
    if (r.idle_minutes > 0) {
        o << ",\"idle_minutes\":" << r.idle_minutes;
    }
    if (r.last_activity_ms > 0) {
        o << ",\"last_activity_ms\":" << r.last_activity_ms;
    }
    if (!r.path.empty()) {
        o << ",\"path\":\"" << json_escape(r.path) << "\"";
    }
    if (!r.format.empty()) {
        o << ",\"format\":\"" << json_escape(r.format) << "\"";
    }
    if (r.event.has_value()) {
        o << ",\"event\":" << encode_session_event(*r.event);
    }
    o << "}";
    return o.str();
}

std::string encode_session_event(const SessionEvent& e) {
    std::ostringstream o;
    o << "{\"type\":\"" << json_escape(e.type) << "\"";
    if (!e.message.empty()) {
        o << ",\"message\":\"" << json_escape(e.message) << "\"";
    }
    if (!e.op_type.empty()) {
        o << ",\"op_type\":\"" << json_escape(e.op_type) << "\"";
    }
    if (!e.peer_id.empty()) {
        o << ",\"peer_id\":\"" << json_escape(e.peer_id) << "\"";
    }
    if (!e.target.empty()) {
        o << ",\"target\":\"" << json_escape(e.target) << "\"";
    }
    if (!e.payload.empty()) {
        o << ",\"payload\":\"" << json_escape(e.payload) << "\"";
    }
    o << "}";
    return o.str();
}

std::string generate_session_id() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 15);
    std::string id;
    id.reserve(8);
    const char* hex = "0123456789abcdef";
    for (int i = 0; i < 8; ++i) {
        id += hex[dist(rng)];
    }
    return id;
}

std::optional<double> parse_idle_minutes(std::string_view s) {
    s = trim(s);
    if (s.empty()) {
        return std::nullopt;
    }
    std::string tmp(s);
    char* end = nullptr;
    double v = std::strtod(tmp.c_str(), &end);
    if (end == tmp.c_str() || *end != '\0' || !std::isfinite(v) || v < 0.0) {
        return std::nullopt;
    }
    return v;
}

}  // namespace cells::cli
