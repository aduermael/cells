// Presence implementation
// Manages ephemeral presence data for collaboration

#include "core/net/include/Presence.h"

#include <cmath>

#include <chrono>
#include <random>
#include <sstream>

namespace cells::net {

// Color palette for user cursors - matches presence.js
const char* const USER_COLORS[] = {
    "#E53935",  // Red
    "#1E88E5",  // Blue
    "#8E24AA",  // Purple
    "#00897B",  // Teal
    "#F57C00",  // Orange
    "#5E35B1",  // Deep Purple
    "#00ACC1",  // Cyan
    "#D81B60",  // Pink
    "#43A047",  // Green
    "#6D4C41"   // Brown
};
const size_t USER_COLORS_COUNT = sizeof(USER_COLORS) / sizeof(USER_COLORS[0]);

// Adjectives and animals for random name generation - matches presence.js
const char* const NAME_ADJECTIVES[] = {"Swift", "Happy", "Clever", "Bold",  "Bright",
                                       "Quick", "Wise",  "Noble",  "Brave", "Kind"};
const size_t NAME_ADJECTIVES_COUNT = sizeof(NAME_ADJECTIVES) / sizeof(NAME_ADJECTIVES[0]);

const char* const NAME_ANIMALS[] = {"Fox",  "Bear", "Eagle", "Wolf",  "Owl",
                                    "Lion", "Hawk", "Deer",  "Tiger", "Panda"};
const size_t NAME_ANIMALS_COUNT = sizeof(NAME_ANIMALS) / sizeof(NAME_ANIMALS[0]);

namespace {

// Simple JSON string escaping
std::string escapeJSONString(const std::string& s) {
    std::ostringstream oss;
    for (const char c : s) {
        switch (c) {
            case '"':
                oss << "\\\"";
                break;
            case '\\':
                oss << "\\\\";
                break;
            case '\b':
                oss << "\\b";
                break;
            case '\f':
                oss << "\\f";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                oss << c;
                break;
        }
    }
    return oss.str();
}

// Extract a string value from JSON
std::string extractJSONString(const std::string& json, const std::string& key) {
    const std::string search_key = "\"" + key + "\":";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) {
        return "";
    }
    pos += search_key.length();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }

    if (pos >= json.size()) {
        return "";
    }

    // Check for null
    if (json.substr(pos, 4) == "null") {
        return "";
    }

    if (json[pos] != '"') {
        return "";
    }

    pos++;  // Skip opening quote
    std::ostringstream result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            switch (json[pos]) {
                case '"':
                    result << '"';
                    break;
                case '\\':
                    result << '\\';
                    break;
                case 'b':
                    result << '\b';
                    break;
                case 'f':
                    result << '\f';
                    break;
                case 'n':
                    result << '\n';
                    break;
                case 'r':
                    result << '\r';
                    break;
                case 't':
                    result << '\t';
                    break;
                default:
                    result << json[pos];
                    break;
            }
        } else {
            result << json[pos];
        }
        pos++;
    }

    return result.str();
}

// Extract a double value from JSON
double extractJSONDouble(const std::string& json, const std::string& key,
                         double default_value = 0.0) {
    const std::string search_key = "\"" + key + "\":";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) {
        return default_value;
    }
    pos += search_key.length();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }

    if (pos >= json.size()) {
        return default_value;
    }

    // Check for null
    if (json.substr(pos, 4) == "null") {
        return default_value;
    }

    // Parse number
    std::string num_str;
    while (pos < json.size() &&
           (std::isdigit(static_cast<unsigned char>(json[pos])) != 0 || json[pos] == '-' ||
            json[pos] == '.' || json[pos] == 'e' || json[pos] == 'E' || json[pos] == '+')) {
        num_str += json[pos];
        pos++;
    }

    if (num_str.empty()) {
        return default_value;
    }

    return std::stod(num_str);
}

// Extract an int64 value from JSON
int64_t extractJSONInt64(const std::string& json, const std::string& key,
                         int64_t default_value = 0) {
    return static_cast<int64_t>(extractJSONDouble(json, key, static_cast<double>(default_value)));
}

// Extract a nested object as raw JSON string
std::string extractJSONObject(const std::string& json, const std::string& key) {
    const std::string search_key = "\"" + key + "\":";
    size_t pos = json.find(search_key);
    if (pos == std::string::npos) {
        return "";
    }
    pos += search_key.length();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }

    if (pos >= json.size()) {
        return "";
    }

    // Check for null
    if (json.substr(pos, 4) == "null") {
        return "";
    }

    if (json[pos] != '{') {
        return "";
    }

    // Find matching closing brace
    int brace_count = 1;
    const size_t start = pos;
    pos++;
    while (pos < json.size() && brace_count > 0) {
        if (json[pos] == '{') {
            brace_count++;
        } else if (json[pos] == '}') {
            brace_count--;
        } else if (json[pos] == '"') {
            // Skip string content
            pos++;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    pos++;
                }
                pos++;
            }
        }
        pos++;
    }

    return json.substr(start, pos - start);
}

}  // namespace

// ============================================================================
// PresenceData implementation
// ============================================================================

std::string PresenceData::toJSON() const {
    std::ostringstream oss;
    oss << "{\"type\":\"presence\"";
    oss << ",\"peer_id\":\"" << escapeJSONString(peer_id) << "\"";
    oss << ",\"name\":\"" << escapeJSONString(name) << "\"";
    oss << ",\"color\":\"" << escapeJSONString(color) << "\"";
    oss << ",\"sheet_id\":\"" << escapeJSONString(sheet_id) << "\"";

    if (has_cursor) {
        oss << ",\"cursor\":{\"col\":\"" << escapeJSONString(cursor.col) << "\",\"row\":\""
            << escapeJSONString(cursor.row) << "\"}";
    } else {
        oss << ",\"cursor\":null";
    }

    if (has_selection) {
        oss << ",\"selection\":{\"start\":{\"col\":\"" << escapeJSONString(selection.start.col)
            << "\",\"row\":\"" << escapeJSONString(selection.start.row) << "\"},\"end\":{\"col\":\""
            << escapeJSONString(selection.end.col) << "\",\"row\":\""
            << escapeJSONString(selection.end.row) << "\"}}";
    } else {
        oss << ",\"selection\":null";
    }

    if (has_mouse) {
        oss << ",\"mouse\":{\"x\":" << mouse.x << ",\"y\":" << mouse.y << "}";
    } else {
        oss << ",\"mouse\":null";
    }

    oss << ",\"timestamp\":" << timestamp << "}";
    return oss.str();
}

bool PresenceData::fromJSON(const std::string& json, PresenceData& out) {
    // Verify this is a presence message
    const std::string type = extractJSONString(json, "type");
    if (type != "presence") {
        return false;
    }

    out.peer_id = extractJSONString(json, "peer_id");
    out.name = extractJSONString(json, "name");
    out.color = extractJSONString(json, "color");
    out.sheet_id = extractJSONString(json, "sheet_id");
    out.timestamp = extractJSONInt64(json, "timestamp");

    // Parse cursor
    const std::string cursor_json = extractJSONObject(json, "cursor");
    if (!cursor_json.empty()) {
        out.has_cursor = true;
        out.cursor.col = extractJSONString(cursor_json, "col");
        out.cursor.row = extractJSONString(cursor_json, "row");
    } else {
        out.has_cursor = false;
    }

    // Parse selection
    const std::string selection_json = extractJSONObject(json, "selection");
    if (!selection_json.empty()) {
        out.has_selection = true;
        const std::string start_json = extractJSONObject(selection_json, "start");
        const std::string end_json = extractJSONObject(selection_json, "end");
        if (!start_json.empty() && !end_json.empty()) {
            out.selection.start.col = extractJSONString(start_json, "col");
            out.selection.start.row = extractJSONString(start_json, "row");
            out.selection.end.col = extractJSONString(end_json, "col");
            out.selection.end.row = extractJSONString(end_json, "row");
        } else {
            out.has_selection = false;
        }
    } else {
        out.has_selection = false;
    }

    // Parse mouse
    const std::string mouse_json = extractJSONObject(json, "mouse");
    if (!mouse_json.empty()) {
        out.has_mouse = true;
        out.mouse.x = extractJSONDouble(mouse_json, "x");
        out.mouse.y = extractJSONDouble(mouse_json, "y");
    } else {
        out.has_mouse = false;
    }

    return true;
}

// ============================================================================
// PresenceManager implementation
// ============================================================================

PresenceManager::PresenceManager() : local_cursor_{}, local_selection_{}, local_mouse_{} {}

PresenceManager::~PresenceManager() = default;

void PresenceManager::initialize(const std::string& peer_id, const std::string& name) {
    local_peer_id_ = peer_id;
    local_name_ = name.empty() ? generateRandomName() : name;
    local_color_ = getColorForPeer(peer_id);
}

void PresenceManager::setLocalName(const std::string& name) {
    local_name_ = name;
    markActivity();
}

void PresenceManager::setCurrentSheet(const std::string& sheet_id) {
    local_sheet_id_ = sheet_id;
    markActivity();
}

void PresenceManager::setCursor(const std::string& col_id, const std::string& row_id) {
    local_has_cursor_ = true;
    local_cursor_.col = col_id;
    local_cursor_.row = row_id;
    markActivity();
}

void PresenceManager::setSelection(const CursorPosition& start, const CursorPosition& end) {
    local_has_selection_ = true;
    local_selection_.start = start;
    local_selection_.end = end;
    markActivity();
}

void PresenceManager::setMousePosition(double x, double y) {
    local_has_mouse_ = true;
    local_mouse_.x = x;
    local_mouse_.y = y;
    markActivity();
}

void PresenceManager::clearCursor() {
    local_has_cursor_ = false;
    markActivity();
}

void PresenceManager::clearSelection() {
    local_has_selection_ = false;
    markActivity();
}

void PresenceManager::clearMousePosition() {
    local_has_mouse_ = false;
    markActivity();
}

PresenceData PresenceManager::getLocalPresence() const {
    PresenceData data;
    data.peer_id = local_peer_id_;
    data.name = local_name_;
    data.color = local_color_;
    data.sheet_id = local_sheet_id_;
    data.has_cursor = local_has_cursor_;
    data.cursor = local_cursor_;
    data.has_selection = local_has_selection_;
    data.selection = local_selection_;
    data.has_mouse = local_has_mouse_;
    data.mouse = local_mouse_;
    data.timestamp = currentTimeMs();
    return data;
}

std::map<std::string, PresenceData> PresenceManager::getRemotePeers() const {
    return remote_peers_;
}

const PresenceData* PresenceManager::getPeerPresence(const std::string& peer_id) const {
    auto it = remote_peers_.find(peer_id);
    if (it != remote_peers_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<PresenceData> PresenceManager::getPeersOnSheet(const std::string& sheet_id) const {
    std::vector<PresenceData> result;
    for (const auto& pair : remote_peers_) {
        if (pair.second.sheet_id == sheet_id) {
            result.push_back(pair.second);
        }
    }
    return result;
}

size_t PresenceManager::getRemotePeerCount() const {
    return remote_peers_.size();
}

bool PresenceManager::isPresenceStale(const std::string& peer_id) const {
    return getPresenceAge(peer_id) >= FADE_TIMEOUT_MS;
}

int64_t PresenceManager::getPresenceAge(const std::string& peer_id) const {
    auto it = remote_peers_.find(peer_id);
    if (it == remote_peers_.end()) {
        return INT64_MAX;
    }
    return currentTimeMs() - it->second.timestamp;
}

double PresenceManager::getPresenceOpacity(const std::string& peer_id) const {
    auto it = remote_peers_.find(peer_id);
    if (it == remote_peers_.end()) {
        return 0.0;
    }

    const int64_t age = currentTimeMs() - it->second.timestamp;
    if (age < LINGER_TIME_MS) {
        return 1.0;  // Fully visible during active period
    }

    // Fade out during the fade timeout period
    const double fade_progress = static_cast<double>(age - LINGER_TIME_MS) /
                                 static_cast<double>(FADE_TIMEOUT_MS - LINGER_TIME_MS);
    return std::max(0.0, 1.0 - fade_progress);
}

void PresenceManager::handlePresenceMessage(const std::string& peer_id,
                                            const std::string& json_message) {
    PresenceData presence;
    if (!PresenceData::fromJSON(json_message, presence)) {
        return;
    }

    // Verify peer_id matches
    if (presence.peer_id != peer_id) {
        return;
    }

    // Fill in color if not provided
    if (presence.color.empty()) {
        presence.color = getColorForPeer(peer_id);
    }

    // Fill in name if not provided
    if (presence.name.empty()) {
        presence.name = "Unknown";
    }

    // Update timestamp if not provided
    if (presence.timestamp == 0) {
        presence.timestamp = currentTimeMs();
    }

    // Store presence
    remote_peers_[peer_id] = presence;

    // Notify delegate
    if (delegate_) {
        delegate_->presenceDidUpdate(*this, peer_id, presence);
    }
}

void PresenceManager::removePeer(const std::string& peer_id) {
    auto it = remote_peers_.find(peer_id);
    if (it != remote_peers_.end()) {
        remote_peers_.erase(it);

        if (delegate_) {
            delegate_->presenceDidRemove(*this, peer_id);
        }
    }
}

bool PresenceManager::processPendingUpdates(std::string& out_message) {
    if (!is_active_) {
        return false;
    }

    const int64_t now = currentTimeMs();
    const int64_t time_since_activity = now - last_activity_time_;

    // Check if activity has stopped and linger time passed
    if (time_since_activity >= LINGER_TIME_MS) {
        is_active_ = false;
        return false;
    }

    // Throttle broadcasts
    const int64_t min_interval = 1000 / MAX_UPDATES_PER_SEC;
    if (now - last_broadcast_time_ < min_interval) {
        return false;
    }

    // Generate presence message
    out_message = getLocalPresence().toJSON();
    last_broadcast_time_ = now;
    needs_broadcast_ = false;

    return true;
}

void PresenceManager::markActivity() {
    last_activity_time_ = currentTimeMs();
    is_active_ = true;
    needs_broadcast_ = true;
}

int64_t PresenceManager::currentTimeMs() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

std::string PresenceManager::generateRandomName() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> adj_dis(0, NAME_ADJECTIVES_COUNT - 1);
    std::uniform_int_distribution<size_t> animal_dis(0, NAME_ANIMALS_COUNT - 1);

    const std::string adjective = NAME_ADJECTIVES[adj_dis(gen)];
    const std::string animal = NAME_ANIMALS[animal_dis(gen)];
    return adjective + " " + animal;
}

std::string PresenceManager::getColorForPeer(const std::string& peer_id) {
    // Simple hash to consistently assign colors
    uint32_t hash = 0;
    for (const char c : peer_id) {
        hash = ((hash << 5) - hash) + static_cast<uint32_t>(c);
    }
    return USER_COLORS[hash % USER_COLORS_COUNT];
}

}  // namespace cells::net
