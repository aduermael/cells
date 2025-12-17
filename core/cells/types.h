#ifndef CELLS_TYPES_H_
#define CELLS_TYPES_H_

#include <cstdint>
#include <cstring>

#include <functional>
#include <string>

namespace cells {

// ID length constant
constexpr size_t ID_LENGTH = 8;

// ID is a fixed 8-character base62 identifier (62^8 = 218 trillion combinations)
// Examples: "Kj7mXp2Q", "fR3pK7wN"
// Null ID has data[0] == '\0', serialized as "~" in file format
// Valid IDs never start with '\0' (base62 chars are 0-9, A-Z, a-z)
struct ID {
    char data[ID_LENGTH];

    // Default constructor creates null ID
    constexpr ID() : data{0} {}

    // Construct from string (must be exactly 8 chars, or "~" for null)
    explicit ID(const char* str) {
        if (str == nullptr || str[0] == '~' || str[0] == '\0') {
            data[0] = '\0';
        } else {
            std::memcpy(data, str, ID_LENGTH);
        }
    }

    explicit ID(const std::string& str) : ID(str.c_str()) {}

    // Check if this is a null ID (single byte check)
    bool isNull() const { return data[0] == '\0'; }

    // Convert to string (for display/debugging)
    std::string toString() const {
        if (isNull())
            return "~";
        return std::string(data, ID_LENGTH);
    }

    // Equality comparison
    bool operator==(const ID& other) const { return std::memcmp(data, other.data, ID_LENGTH) == 0; }

    bool operator!=(const ID& other) const { return !(*this == other); }

    // Ordering (for use in sorted containers)
    bool operator<(const ID& other) const { return std::memcmp(data, other.data, ID_LENGTH) < 0; }
};

// Hash function for ID (for use in unordered_map/unordered_set)
struct IDHash {
    std::size_t operator()(const ID& id) const {
        // FNV-1a hash
        std::size_t hash = 14695981039346656037ULL;
        for (size_t i = 0; i < ID_LENGTH; ++i) {
            hash ^= static_cast<unsigned char>(id.data[i]);
            hash *= 1099511628211ULL;
        }
        return hash;
    }
};

// Cell value types - stored as single char in file format
enum class CellValueType {
    NUMBER,     // 'n' - numeric value (42, 3.14, -100)
    STRING,     // 's' - quoted string ("Hello")
    FORMULA,    // 'f' - formula with ID-based refs ("=$cA$r1+10")
    BOOLEAN,    // 'b' - true or false
    ERROR,      // 'e' - error value (#DIV/0!, #REF!, etc.)
    DATE,       // 'd' - ISO 8601 date (2024-01-15)
    DATE_TIME,  // 't' - ISO 8601 datetime (2024-01-15T10:30:00Z)
};

// Convert file format char to CellValueType
inline CellValueType charToValueType(char c) {
    switch (c) {
        case 'n':
            return CellValueType::NUMBER;
        case 's':
            return CellValueType::STRING;
        case 'f':
            return CellValueType::FORMULA;
        case 'b':
            return CellValueType::BOOLEAN;
        case 'e':
            return CellValueType::ERROR;
        case 'd':
            return CellValueType::DATE;
        case 't':
            return CellValueType::DATE_TIME;
        default:
            return CellValueType::STRING;  // Default to string
    }
}

// Convert CellValueType to file format char
inline char valueTypeToChar(CellValueType type) {
    switch (type) {
        case CellValueType::NUMBER:
            return 'n';
        case CellValueType::STRING:
            return 's';
        case CellValueType::FORMULA:
            return 'f';
        case CellValueType::BOOLEAN:
            return 'b';
        case CellValueType::ERROR:
            return 'e';
        case CellValueType::DATE:
            return 'd';
        case CellValueType::DATE_TIME:
            return 't';
    }
    return 's';  // Default to string
}

// Cell error types - stored as strings in file format
enum class CellError {
    NONE,      // No error
    VALUE,     // #VALUE! - wrong type of argument
    REF,       // #REF! - invalid cell reference
    NAME,      // #NAME? - unrecognized formula name
    DIV,       // #DIV/0! - division by zero
    NULL_REF,  // #NULL! - incorrect range
    NUM,       // #NUM! - invalid numeric value
    CIRCULAR,  // Circular reference detected
};

// Error strings as they appear in file format
constexpr const char* ERROR_VALUE_STR = "#VALUE!";
constexpr const char* ERROR_REF_STR = "#REF!";
constexpr const char* ERROR_NAME_STR = "#NAME?";
constexpr const char* ERROR_DIV_STR = "#DIV/0!";
constexpr const char* ERROR_NULL_STR = "#NULL!";
constexpr const char* ERROR_NUM_STR = "#NUM!";
constexpr const char* ERROR_CIRCULAR_STR = "#CIRCULAR!";

// Convert error string to CellError
inline CellError stringToError(const std::string& s) {
    if (s == ERROR_VALUE_STR)
        return CellError::VALUE;
    if (s == ERROR_REF_STR)
        return CellError::REF;
    if (s == ERROR_NAME_STR)
        return CellError::NAME;
    if (s == ERROR_DIV_STR)
        return CellError::DIV;
    if (s == ERROR_NULL_STR)
        return CellError::NULL_REF;
    if (s == ERROR_NUM_STR)
        return CellError::NUM;
    if (s == ERROR_CIRCULAR_STR)
        return CellError::CIRCULAR;
    return CellError::NONE;
}

// Convert CellError to error string
inline const char* errorToString(CellError error) {
    switch (error) {
        case CellError::NONE:
            return "";
        case CellError::VALUE:
            return ERROR_VALUE_STR;
        case CellError::REF:
            return ERROR_REF_STR;
        case CellError::NAME:
            return ERROR_NAME_STR;
        case CellError::DIV:
            return ERROR_DIV_STR;
        case CellError::NULL_REF:
            return ERROR_NULL_STR;
        case CellError::NUM:
            return ERROR_NUM_STR;
        case CellError::CIRCULAR:
            return ERROR_CIRCULAR_STR;
    }
    return "";
}

// Default sizes for axes
constexpr uint32_t DEFAULT_COLUMN_WIDTH = 100;
constexpr uint32_t DEFAULT_ROW_HEIGHT = 24;

}  // namespace cells

#endif  // CELLS_TYPES_H_
