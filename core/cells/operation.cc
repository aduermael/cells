#include "core/cells/operation.h"

#include <cstring>

#include <algorithm>
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
        case OpType::CELL_SET_FORMAT:
            return "CELL_SET_FORMAT";
        // Column operations
        case OpType::COL_INSERT:
            return "COL_INSERT";
        case OpType::COL_DELETE:
            return "COL_DELETE";
        case OpType::COL_MOVE:
            return "COL_MOVE";
        case OpType::COL_RESIZE:
            return "COL_RESIZE";
        case OpType::COL_RENAME:
            return "COL_RENAME";
        // Row operations
        case OpType::ROW_INSERT:
            return "ROW_INSERT";
        case OpType::ROW_DELETE:
            return "ROW_DELETE";
        case OpType::ROW_MOVE:
            return "ROW_MOVE";
        case OpType::ROW_RESIZE:
            return "ROW_RESIZE";
        // Axis operations
        case OpType::AXIS_SET_HIDDEN:
            return "AXIS_SET_HIDDEN";
        case OpType::AXIS_SET_STYLE:
            return "AXIS_SET_STYLE";
        case OpType::AXIS_SET_FORMAT:
            return "AXIS_SET_FORMAT";
        // Sheet operations
        case OpType::SHEET_CREATE:
            return "SHEET_CREATE";
        case OpType::SHEET_DELETE:
            return "SHEET_DELETE";
        case OpType::SHEET_RENAME:
            return "SHEET_RENAME";
        // Workbook operations
        case OpType::WORKBOOK_RENAME:
            return "WORKBOOK_RENAME";
        // Format operations
        case OpType::FORMAT_DEFINE:
            return "FORMAT_DEFINE";
        // Named range operations
        case OpType::NAMED_RANGE_DEFINE:
            return "NAMED_RANGE_DEFINE";
        case OpType::NAMED_RANGE_DELETE:
            return "NAMED_RANGE_DELETE";
        // Range operations
        case OpType::RANGE_ADD:
            return "RANGE_ADD";
        case OpType::RANGE_REMOVE:
            return "RANGE_REMOVE";
        case OpType::RANGE_UPDATE_CORNERS:
            return "RANGE_UPDATE_CORNERS";
        case OpType::RANGE_UPDATE_FLAGS:
            return "RANGE_UPDATE_FLAGS";
        case OpType::RANGE_SET_STYLE:
            return "RANGE_SET_STYLE";
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
    if (str == "CELL_SET_FORMAT") {
        return OpType::CELL_SET_FORMAT;
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
    if (str == "WORKBOOK_RENAME") {
        return OpType::WORKBOOK_RENAME;
    }
    // Format operations
    if (str == "FORMAT_DEFINE") {
        return OpType::FORMAT_DEFINE;
    }
    // Named range operations
    if (str == "NAMED_RANGE_DEFINE") {
        return OpType::NAMED_RANGE_DEFINE;
    }
    if (str == "NAMED_RANGE_DELETE") {
        return OpType::NAMED_RANGE_DELETE;
    }
    // Column operations
    if (str == "COL_INSERT") {
        return OpType::COL_INSERT;
    }
    if (str == "COL_DELETE") {
        return OpType::COL_DELETE;
    }
    if (str == "COL_MOVE") {
        return OpType::COL_MOVE;
    }
    if (str == "COL_RESIZE") {
        return OpType::COL_RESIZE;
    }
    if (str == "COL_RENAME") {
        return OpType::COL_RENAME;
    }
    // Row operations
    if (str == "ROW_INSERT") {
        return OpType::ROW_INSERT;
    }
    if (str == "ROW_DELETE") {
        return OpType::ROW_DELETE;
    }
    if (str == "ROW_MOVE") {
        return OpType::ROW_MOVE;
    }
    if (str == "ROW_RESIZE") {
        return OpType::ROW_RESIZE;
    }
    // Axis operations
    if (str == "AXIS_SET_HIDDEN") {
        return OpType::AXIS_SET_HIDDEN;
    }
    if (str == "AXIS_SET_STYLE") {
        return OpType::AXIS_SET_STYLE;
    }
    if (str == "AXIS_SET_FORMAT") {
        return OpType::AXIS_SET_FORMAT;
    }
    // Range operations
    if (str == "RANGE_ADD") {
        return OpType::RANGE_ADD;
    }
    if (str == "RANGE_REMOVE") {
        return OpType::RANGE_REMOVE;
    }
    if (str == "RANGE_UPDATE_CORNERS") {
        return OpType::RANGE_UPDATE_CORNERS;
    }
    if (str == "RANGE_UPDATE_FLAGS") {
        return OpType::RANGE_UPDATE_FLAGS;
    }
    if (str == "RANGE_SET_STYLE") {
        return OpType::RANGE_SET_STYLE;
    }
    return OpType::CELL_SET_VALUE;  // Default
}

Operation::Operation() : hlc(), type(OpType::CELL_SET_VALUE), target_id(), sheetId(), payload() {}

Operation::Operation(const HLC& hlc, OpType type, const ID& target, std::string payload)
    : hlc(hlc), type(type), target_id(target), sheetId(), payload(std::move(payload)) {}

Operation::Operation(const HLC& hlc, OpType type, const ID& target, const ID& sheetId,
                     std::string payload)
    : hlc(hlc), type(type), target_id(target), sheetId(sheetId), payload(std::move(payload)) {}

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
    // Format: "wall.logical.node OP_TYPE target_id sheetId payload"
    // sheetId uses "~" for null ID (same as ID::toString())
    std::ostringstream oss;
    oss << hlc.toString() << " " << opTypeToString(type) << " " << target_id.toString() << " "
        << sheetId.toString() << " " << payload;
    return oss.str();
}

Operation Operation::fromString(const std::string& str) {
    // Parse: "wall.logical.node OP_TYPE target_id sheetId payload"
    // Also supports legacy format without sheetId: "wall.logical.node OP_TYPE target_id payload"

    // Find first space (after HLC)
    const size_t first_space = str.find(' ');
    if (first_space == std::string::npos) {
        return {};
    }

    // Find second space (after OpType)
    const size_t second_space = str.find(' ', first_space + 1);
    if (second_space == std::string::npos) {
        return {};
    }

    // Find third space (after target_id)
    const size_t third_space = str.find(' ', second_space + 1);
    if (third_space == std::string::npos) {
        return {};
    }

    // Parse HLC
    const std::string hlc_str = str.substr(0, first_space);
    const HLC hlc = HLC::fromString(hlc_str);
    if (hlc.isZero() && hlc_str != "0.0.~") {
        return {};  // Parse failed
    }

    // Parse OpType
    const std::string op_str = str.substr(first_space + 1, second_space - first_space - 1);
    const OpType type = stringToOpType(op_str);

    // Parse target_id
    const std::string target_str = str.substr(second_space + 1, third_space - second_space - 1);
    const ID target(target_str);

    // Check if next field is sheetId or payload (for backwards compatibility)
    // sheetId is 8 alphanumeric chars or "~" for null
    // payload starts with "{" or is empty
    const size_t fourth_space = str.find(' ', third_space + 1);
    if (fourth_space == std::string::npos) {
        // No fourth space - rest is payload (legacy format without sheetId)
        const std::string payload = str.substr(third_space + 1);
        return {hlc, type, target, payload};
    }

    // Check what's between third and fourth space
    const std::string maybe_sheet = str.substr(third_space + 1, fourth_space - third_space - 1);

    // If it looks like a sheetId (8 chars alphanumeric or "~"), parse it as such
    // Otherwise treat as legacy format where this is the start of payload
    const bool is_sheet_id =
        (maybe_sheet == "~") ||
        (maybe_sheet.size() == 8 && std::all_of(maybe_sheet.begin(), maybe_sheet.end(), ::isalnum));
    if (is_sheet_id) {
        // New format with sheetId
        const ID sheetId(maybe_sheet);
        const std::string payload = str.substr(fourth_space + 1);
        return {hlc, type, target, sheetId, payload};
    }

    // Legacy format - treat everything after target_id as payload
    const std::string payload = str.substr(third_space + 1);
    return {hlc, type, target, payload};
}

namespace {

// Simple JSON string unescaping
std::string unescapeJSON(const std::string& str) {
    std::ostringstream oss;
    size_t i = 0;
    while (i < str.size()) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            const char next = str[i + 1];
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
                        const int code = static_cast<int>(strtol(hex, nullptr, 16));
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
    const std::string searchKey = "\"" + key + "\":";
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
    const bool isObject = (json[pos] == '{');
    const bool isArray = (json[pos] == '[');
    while (end < json.size()) {
        const char c = json[end];
        if (c == '"') {
            // Skip string content (don't count braces/brackets inside strings)
            end++;
            while (end < json.size() && json[end] != '"') {
                if (json[end] == '\\' && end + 1 < json.size()) {
                    end++;  // Skip escaped char
                }
                end++;
            }
            // end now points to closing quote (or past the end)
        } else if (c == '{') {
            braceCount++;
        } else if (c == '}') {
            braceCount--;
            // If we're extracting an object and just closed it, include this brace and stop
            if (isObject && braceCount == 0) {
                end++;
                break;
            }
        } else if (c == '[') {
            bracketCount++;
        } else if (c == ']') {
            bracketCount--;
            // If we're extracting an array and just closed it, include this bracket and stop
            if (isArray && bracketCount == 0) {
                end++;
                break;
            }
        }
        // Stop at delimiter when not inside nested structure
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
    oss << "\"sheet\":\"" << sheetId.toString() << "\",";
    oss << "\"payload\":" << payload;  // payload is already JSON
    oss << "}";
    return oss.str();
}

Operation Operation::fromJSON(const std::string& json) {
    const std::string hlc_str = findJSONValue(json, "hlc");
    const std::string op_str = findJSONValue(json, "op");
    const std::string target_str = findJSONValue(json, "target");
    const std::string sheet_str = findJSONValue(json, "sheet");  // Optional, may be empty
    const std::string payload_str = findJSONValue(json, "payload");

    if (hlc_str.empty() || op_str.empty() || target_str.empty()) {
        return {};
    }

    const HLC hlc = HLC::fromString(hlc_str);
    const OpType type = stringToOpType(op_str);
    const ID target(target_str);
    const ID sheet(sheet_str);  // Will be null ID if empty

    return {hlc, type, target, sheet, payload_str};
}

}  // namespace cells
