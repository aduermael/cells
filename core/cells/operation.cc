#include "core/cells/operation.h"

#include <cstring>
#include <sstream>

namespace cells {

const char* opTypeToString(OpType type) {
    switch (type) {
        case OpType::CELL_SET_VALUE:
            return "CELL_SET_VALUE";
        case OpType::CELL_CLEAR:
            return "CELL_CLEAR";
        case OpType::CELL_SET_STYLE:
            return "CELL_SET_STYLE";
        case OpType::DIM_INSERT_AXIS:
            return "DIM_INSERT_AXIS";
        case OpType::DIM_DELETE_AXIS:
            return "DIM_DELETE_AXIS";
        case OpType::DIM_MOVE_AXIS:
            return "DIM_MOVE_AXIS";
        case OpType::DIM_RESIZE_AXIS:
            return "DIM_RESIZE_AXIS";
        case OpType::SHEET_CREATE:
            return "SHEET_CREATE";
        case OpType::SHEET_DELETE:
            return "SHEET_DELETE";
        case OpType::SHEET_RENAME:
            return "SHEET_RENAME";
    }
    return "CELL_SET_VALUE";
}

OpType stringToOpType(const std::string& str) {
    if (str == "CELL_SET_VALUE") {
        return OpType::CELL_SET_VALUE;
    }
    if (str == "CELL_CLEAR") {
        return OpType::CELL_CLEAR;
    }
    if (str == "CELL_SET_STYLE") {
        return OpType::CELL_SET_STYLE;
    }
    if (str == "DIM_INSERT_AXIS") {
        return OpType::DIM_INSERT_AXIS;
    }
    if (str == "DIM_DELETE_AXIS") {
        return OpType::DIM_DELETE_AXIS;
    }
    if (str == "DIM_MOVE_AXIS") {
        return OpType::DIM_MOVE_AXIS;
    }
    if (str == "DIM_RESIZE_AXIS") {
        return OpType::DIM_RESIZE_AXIS;
    }
    if (str == "SHEET_CREATE") {
        return OpType::SHEET_CREATE;
    }
    if (str == "SHEET_DELETE") {
        return OpType::SHEET_DELETE;
    }
    if (str == "SHEET_RENAME") {
        return OpType::SHEET_RENAME;
    }
    return OpType::CELL_SET_VALUE;  // Default
}

Operation::Operation() : hlc(), type(OpType::CELL_SET_VALUE), target_id(), payload() {}

Operation::Operation(const HLC& hlc, OpType type, const ID& target, std::string payload)
    : hlc(hlc), type(type), target_id(target), payload(std::move(payload)) {}

bool Operation::isNull() const {
    return hlc.isZero() && target_id.isNull();
}

bool Operation::operator<(const Operation& other) const {
    return hlc < other.hlc;
}

bool Operation::operator==(const Operation& other) const {
    return hlc == other.hlc && type == other.type && target_id == other.target_id &&
           payload == other.payload;
}

std::string Operation::toString() const {
    // Format: "wall.logical.node OP_TYPE target_id payload"
    std::ostringstream oss;
    oss << hlc.toString() << " " << opTypeToString(type) << " " << target_id.toString() << " "
        << payload;
    return oss.str();
}

Operation Operation::fromString(const std::string& str) {
    // Parse: "wall.logical.node OP_TYPE target_id payload"

    // Find first space (after HLC)
    size_t first_space = str.find(' ');
    if (first_space == std::string::npos) {
        return Operation();
    }

    // Find second space (after OpType)
    size_t second_space = str.find(' ', first_space + 1);
    if (second_space == std::string::npos) {
        return Operation();
    }

    // Find third space (after target_id)
    size_t third_space = str.find(' ', second_space + 1);
    if (third_space == std::string::npos) {
        return Operation();
    }

    // Parse HLC
    std::string hlc_str = str.substr(0, first_space);
    HLC hlc = HLC::fromString(hlc_str);
    if (hlc.isZero() && hlc_str != "0.0.~") {
        return Operation();  // Parse failed
    }

    // Parse OpType
    std::string op_str = str.substr(first_space + 1, second_space - first_space - 1);
    OpType type = stringToOpType(op_str);

    // Parse target_id
    std::string target_str = str.substr(second_space + 1, third_space - second_space - 1);
    ID target(target_str);

    // Rest is payload
    std::string payload = str.substr(third_space + 1);

    return Operation(hlc, type, target, payload);
}

namespace {

// Simple JSON string unescaping
std::string unescapeJSON(const std::string& str) {
    std::ostringstream oss;
    size_t i = 0;
    while (i < str.size()) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            char next = str[i + 1];
            switch (next) {
                case '"':
                    oss << '"';
                    i += 2;
                    break;
                case '\\':
                    oss << '\\';
                    i += 2;
                    break;
                case 'b':
                    oss << '\b';
                    i += 2;
                    break;
                case 'f':
                    oss << '\f';
                    i += 2;
                    break;
                case 'n':
                    oss << '\n';
                    i += 2;
                    break;
                case 'r':
                    oss << '\r';
                    i += 2;
                    break;
                case 't':
                    oss << '\t';
                    i += 2;
                    break;
                case 'u':
                    // Unicode escape - simplified handling
                    if (i + 5 < str.size()) {
                        char hex[5] = {str[i + 2], str[i + 3], str[i + 4], str[i + 5], 0};
                        int code = static_cast<int>(strtol(hex, nullptr, 16));
                        if (code < 128) {
                            oss << static_cast<char>(code);
                        }
                        i += 6;
                    } else {
                        oss << str[i];
                        i++;
                    }
                    break;
                default:
                    oss << str[i];
                    i++;
            }
        } else {
            oss << str[i];
            i++;
        }
    }
    return oss.str();
}

// Find value after a JSON key
std::string findJSONValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) {
        return "";
    }
    pos += searchKey.length();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }

    if (pos >= json.size()) {
        return "";
    }

    if (json[pos] == '"') {
        // String value
        pos++;  // Skip opening quote
        size_t end = pos;
        while (end < json.size()) {
            if (json[end] == '"' && json[end - 1] != '\\') {
                break;
            }
            end++;
        }
        return unescapeJSON(json.substr(pos, end - pos));
    }

    // Non-string value (number, boolean, null, or nested object/array)
    size_t end = pos;
    int braceCount = 0;
    int bracketCount = 0;
    while (end < json.size()) {
        char c = json[end];
        if (c == '{') {
            braceCount++;
        }
        if (c == '}') {
            braceCount--;
        }
        if (c == '[') {
            bracketCount++;
        }
        if (c == ']') {
            bracketCount--;
        }
        if (braceCount == 0 && bracketCount == 0 && (c == ',' || c == '}' || c == ']')) {
            break;
        }
        end++;
    }
    return json.substr(pos, end - pos);
}

}  // namespace

std::string Operation::toJSON() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"hlc\":\"" << hlc.toString() << "\",";
    oss << "\"op\":\"" << opTypeToString(type) << "\",";
    oss << "\"target\":\"" << target_id.toString() << "\",";
    oss << "\"payload\":" << payload;  // payload is already JSON
    oss << "}";
    return oss.str();
}

Operation Operation::fromJSON(const std::string& json) {
    std::string hlc_str = findJSONValue(json, "hlc");
    std::string op_str = findJSONValue(json, "op");
    std::string target_str = findJSONValue(json, "target");
    std::string payload_str = findJSONValue(json, "payload");

    if (hlc_str.empty() || op_str.empty() || target_str.empty()) {
        return Operation();
    }

    HLC hlc = HLC::fromString(hlc_str);
    OpType type = stringToOpType(op_str);
    ID target(target_str);

    return Operation(hlc, type, target, payload_str);
}

}  // namespace cells
