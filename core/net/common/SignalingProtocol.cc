// Signaling protocol implementation
// Simple JSON builder/parser without external dependencies

#include "core/net/include/SignalingProtocol.h"

#include <algorithm>
#include <sstream>

namespace cells::net {

namespace SignalingProtocol {

std::string escapeJsonString(const std::string& input) {
    std::ostringstream ss;
    for (const char c : input) {
        switch (c) {
            case '"':
                ss << "\\\"";
                break;
            case '\\':
                ss << "\\\\";
                break;
            case '\b':
                ss << "\\b";
                break;
            case '\f':
                ss << "\\f";
                break;
            case '\n':
                ss << "\\n";
                break;
            case '\r':
                ss << "\\r";
                break;
            case '\t':
                ss << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Control character - use \u00xx format
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    ss << buf;
                } else {
                    ss << c;
                }
                break;
        }
    }
    return ss.str();
}

std::string unescapeJsonString(const std::string& input) {
    std::ostringstream ss;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            const char next = input[i + 1];
            switch (next) {
                case '"':
                    ss << '"';
                    ++i;
                    break;
                case '\\':
                    ss << '\\';
                    ++i;
                    break;
                case 'b':
                    ss << '\b';
                    ++i;
                    break;
                case 'f':
                    ss << '\f';
                    ++i;
                    break;
                case 'n':
                    ss << '\n';
                    ++i;
                    break;
                case 'r':
                    ss << '\r';
                    ++i;
                    break;
                case 't':
                    ss << '\t';
                    ++i;
                    break;
                case 'u':
                    if (i + 5 < input.size()) {
                        const std::string hex = input.substr(i + 2, 4);
                        const char* endptr = nullptr;
                        const unsigned long code =
                            strtoul(hex.c_str(), const_cast<char**>(&endptr), 16);
                        if (endptr != nullptr && *endptr == '\0') {
                            if (code < 0x80) {
                                ss << static_cast<char>(code);
                            } else if (code < 0x800) {
                                ss << static_cast<char>(0xC0 | (code >> 6));
                                ss << static_cast<char>(0x80 | (code & 0x3F));
                            } else {
                                ss << static_cast<char>(0xE0 | (code >> 12));
                                ss << static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                                ss << static_cast<char>(0x80 | (code & 0x3F));
                            }
                            i += 5;
                        } else {
                            ss << input[i];
                        }
                    } else {
                        ss << input[i];
                    }
                    break;
                default:
                    ss << input[i];
                    break;
            }
        } else {
            ss << input[i];
        }
    }
    return ss.str();
}

// Helper to find a string value in JSON: "key":"value"
static bool findStringValue(const std::string& json, const std::string& key, std::string& value) {
    const std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return false;
    }

    pos += search.size();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        ++pos;
    }

    if (pos >= json.size()) {
        return false;
    }

    // Check for string value
    if (json[pos] == '"') {
        ++pos;
        size_t end = pos;
        while (end < json.size()) {
            if (json[end] == '"' && (end == 0 || json[end - 1] != '\\')) {
                break;
            }
            ++end;
        }
        value = unescapeJsonString(json.substr(pos, end - pos));
        return true;
    }

    return false;
}

// Helper to find an integer value in JSON: "key":123
static bool findIntValue(const std::string& json, const std::string& key, int& value) {
    const std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return false;
    }

    pos += search.size();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        ++pos;
    }

    if (pos >= json.size()) {
        return false;
    }

    // Parse integer
    size_t end = pos;
    if (json[end] == '-') {
        ++end;
    }
    while (end < json.size() && json[end] >= '0' && json[end] <= '9') {
        ++end;
    }

    if (end > pos) {
        const std::string numStr = json.substr(pos, end - pos);
        const char* endptr = nullptr;
        const long parsed = strtol(numStr.c_str(), const_cast<char**>(&endptr), 10);
        if (endptr != nullptr && *endptr == '\0') {
            value = static_cast<int>(parsed);
            return true;
        }
        return false;
    }

    return false;
}

// Helper to find a string array in JSON: "key":["a","b","c"]
static bool findStringArray(const std::string& json, const std::string& key,
                            std::vector<std::string>& values) {
    const std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return false;
    }

    pos += search.size();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        ++pos;
    }

    if (pos >= json.size() || json[pos] != '[') {
        return false;
    }
    ++pos;

    values.clear();

    // Parse array elements
    while (pos < json.size()) {
        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) {
            ++pos;
        }

        if (pos >= json.size()) {
            break;
        }

        if (json[pos] == ']') {
            break;
        }

        if (json[pos] == '"') {
            ++pos;
            size_t end = pos;
            while (end < json.size()) {
                if (json[end] == '"' && (end == 0 || json[end - 1] != '\\')) {
                    break;
                }
                ++end;
            }
            values.push_back(unescapeJsonString(json.substr(pos, end - pos)));
            pos = end + 1;
        }

        // Skip comma
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ',' || json[pos] == '\n')) {
            ++pos;
        }
    }

    return true;
}

// Helper to find a nested object value (e.g., "sdp":{...})
static bool findNestedObject(const std::string& json, const std::string& key, std::string& nested) {
    const std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return false;
    }

    pos += search.size();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        ++pos;
    }

    if (pos >= json.size() || json[pos] != '{') {
        return false;
    }

    // Find matching closing brace (handle nesting)
    const size_t start = pos;
    int depth = 1;
    ++pos;
    bool in_string = false;

    while (pos < json.size() && depth > 0) {
        const char c = json[pos];
        if (in_string) {
            if (c == '"' && json[pos - 1] != '\\') {
                in_string = false;
            }
        } else {
            if (c == '"') {
                in_string = true;
            } else if (c == '{') {
                ++depth;
            } else if (c == '}') {
                --depth;
            }
        }
        ++pos;
    }

    if (depth == 0) {
        nested = json.substr(start, pos - start);
        return true;
    }

    return false;
}

std::string buildJoinMessage(const std::string& room_id, const std::string& peer_id) {
    std::ostringstream ss;
    ss << "{\"type\":\"" << SignalingMessageType::JOIN << "\"";
    ss << ",\"room\":\"" << escapeJsonString(room_id) << "\"";
    ss << ",\"peer_id\":\"" << escapeJsonString(peer_id) << "\"}";
    return ss.str();
}

std::string buildLeaveMessage(const std::string& room_id, const std::string& peer_id) {
    std::ostringstream ss;
    ss << "{\"type\":\"" << SignalingMessageType::LEAVE << "\"";
    ss << ",\"room\":\"" << escapeJsonString(room_id) << "\"";
    ss << ",\"peer_id\":\"" << escapeJsonString(peer_id) << "\"}";
    return ss.str();
}

std::string buildOfferMessage(const std::string& target_peer, const SessionDescription& sdp) {
    std::ostringstream ss;
    ss << "{\"type\":\"" << SignalingMessageType::OFFER << "\"";
    ss << ",\"target\":\"" << escapeJsonString(target_peer) << "\"";
    ss << ",\"sdp\":{\"type\":\"offer\",\"sdp\":\"" << escapeJsonString(sdp.sdp) << "\"}}";
    return ss.str();
}

std::string buildAnswerMessage(const std::string& target_peer, const SessionDescription& sdp) {
    std::ostringstream ss;
    ss << "{\"type\":\"" << SignalingMessageType::ANSWER << "\"";
    ss << ",\"target\":\"" << escapeJsonString(target_peer) << "\"";
    ss << ",\"sdp\":{\"type\":\"answer\",\"sdp\":\"" << escapeJsonString(sdp.sdp) << "\"}}";
    return ss.str();
}

std::string buildICECandidateMessage(const std::string& target_peer,
                                     const ICECandidate& candidate) {
    std::ostringstream ss;
    ss << "{\"type\":\"" << SignalingMessageType::ICE_CANDIDATE << "\"";
    ss << ",\"target\":\"" << escapeJsonString(target_peer) << "\"";
    ss << ",\"candidate\":{";
    ss << "\"candidate\":\"" << escapeJsonString(candidate.candidate) << "\"";
    ss << ",\"sdpMid\":\"" << escapeJsonString(candidate.sdp_mid) << "\"";
    ss << ",\"sdpMLineIndex\":" << candidate.sdp_mline_index;
    ss << "}}";
    return ss.str();
}

std::string parseMessageType(const std::string& json) {
    std::string type;
    if (findStringValue(json, "type", type)) {
        return type;
    }
    return "";
}

bool parseJoinedMessage(const std::string& json, std::string& room,
                        std::vector<std::string>& peers) {
    if (!findStringValue(json, "room", room)) {
        return false;
    }
    // peers array is optional
    findStringArray(json, "peers", peers);
    return true;
}

bool parsePeerJoinedMessage(const std::string& json, std::string& peer_id) {
    return findStringValue(json, "peer_id", peer_id);
}

bool parsePeerLeftMessage(const std::string& json, std::string& peer_id) {
    return findStringValue(json, "peer_id", peer_id);
}

bool parseOfferMessage(const std::string& json, std::string& from_peer, SessionDescription& sdp) {
    if (!findStringValue(json, "from", from_peer)) {
        return false;
    }

    std::string sdp_obj;
    if (!findNestedObject(json, "sdp", sdp_obj)) {
        return false;
    }

    std::string sdp_string;
    if (!findStringValue(sdp_obj, "sdp", sdp_string)) {
        return false;
    }

    sdp = SessionDescription::offer(std::move(sdp_string));
    return true;
}

bool parseAnswerMessage(const std::string& json, std::string& from_peer, SessionDescription& sdp) {
    if (!findStringValue(json, "from", from_peer)) {
        return false;
    }

    std::string sdp_obj;
    if (!findNestedObject(json, "sdp", sdp_obj)) {
        return false;
    }

    std::string sdp_string;
    if (!findStringValue(sdp_obj, "sdp", sdp_string)) {
        return false;
    }

    sdp = SessionDescription::answer(std::move(sdp_string));
    return true;
}

bool parseICECandidateMessage(const std::string& json, std::string& from_peer,
                              ICECandidate& candidate) {
    if (!findStringValue(json, "from", from_peer)) {
        return false;
    }

    std::string cand_obj;
    if (!findNestedObject(json, "candidate", cand_obj)) {
        return false;
    }

    std::string cand_string;
    if (!findStringValue(cand_obj, "candidate", cand_string)) {
        return false;
    }

    std::string sdp_mid;
    findStringValue(cand_obj, "sdpMid", sdp_mid);

    int sdp_mline_index = 0;
    findIntValue(cand_obj, "sdpMLineIndex", sdp_mline_index);

    candidate = ICECandidate(std::move(cand_string), std::move(sdp_mid), sdp_mline_index);
    return true;
}

bool parsePeerListMessage(const std::string& json, std::vector<std::string>& peers) {
    return findStringArray(json, "peers", peers);
}

bool parseErrorMessage(const std::string& json, std::string& error_message) {
    return findStringValue(json, "message", error_message);
}

}  // namespace SignalingProtocol

}  // namespace cells::net
