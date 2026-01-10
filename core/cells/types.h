// =============================================================================
// Core Type Definitions
// =============================================================================
//
// Fundamental types used throughout the Cells engine: ID (8-char base62 UUID),
// CellValueType (number, string, formula, etc.), CellError, and related utilities.
//
// Key responsibilities:
// - Define the ID struct: 8-character base62 identifier (62^8 = 218 trillion)
// - Provide IDHash for use in unordered containers
// - Define cell value types and formula result types
// - Define cell error types and their string representations
// - Provide type conversion utilities for serialization
//
// Design notes:
// - ID is fixed-size (8 chars) for efficient storage and comparison
// - Null ID is represented by data[0] == '\0', serialized as "~"
// - Formula types (FORMULA_*) encode both "is formula" and "result type"
// - Error strings match Excel conventions (#DIV/0!, #REF!, etc.)
//
// Dependencies: None (leaf module)
// Used by: Nearly all modules in the Cells engine
//
// =============================================================================

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
    char data[ID_LENGTH]{};

    // Default constructor creates null ID (data already zero-initialized above)
    constexpr ID() = default;

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
    [[nodiscard]] bool isNull() const { return data[0] == '\0'; }

    // Convert to string (for display/debugging)
    [[nodiscard]] std::string toString() const {
        if (isNull()) {
            return "~";
        }
        return {data, ID_LENGTH};
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
        // FNV-1a hash with platform-appropriate constants
        // 32-bit: offset=2166136261, prime=16777619
        // 64-bit: offset=14695981039346656037, prime=1099511628211
        constexpr std::size_t fnv_offset = sizeof(std::size_t) == 8
                                               ? static_cast<std::size_t>(14695981039346656037ULL)
                                               : static_cast<std::size_t>(2166136261UL);
        constexpr std::size_t fnv_prime = sizeof(std::size_t) == 8
                                              ? static_cast<std::size_t>(1099511628211ULL)
                                              : static_cast<std::size_t>(16777619UL);
        std::size_t hash = fnv_offset;
        for (const char i : id.data) {
            hash ^= static_cast<unsigned char>(i);
            hash *= fnv_prime;
        }
        return hash;
    }
};

}  // namespace cells

// std::hash specialization for ID (allows use in std::unordered_map/set without custom hasher)
template <>
struct std::hash<cells::ID> {
    std::size_t operator()(const cells::ID& id) const {
        // FNV-1a hash with platform-appropriate constants
        constexpr std::size_t fnv_offset = sizeof(std::size_t) == 8
                                               ? static_cast<std::size_t>(14695981039346656037ULL)
                                               : static_cast<std::size_t>(2166136261UL);
        constexpr std::size_t fnv_prime = sizeof(std::size_t) == 8
                                              ? static_cast<std::size_t>(1099511628211ULL)
                                              : static_cast<std::size_t>(16777619UL);
        std::size_t hash = fnv_offset;
        for (const char i : id.data) {
            hash ^= static_cast<unsigned char>(i);
            hash *= fnv_prime;
        }
        return hash;
    }
};

namespace cells {

// Cell value types - stored as single char in file format
// Formula result types (FORMULA_*) indicate both "this is a formula" AND "the computed result type"
enum class CellValueType : std::uint8_t {
    NUMBER,     // 'n' - numeric value (42, 3.14, -100)
    STRING,     // 's' - quoted string ("Hello")
    FORMULA,    // 'f' - formula (unevaluated or pre-evaluation)
    BOOLEAN,    // 'b' - true or false
    ERROR,      // 'e' - error value (#DIV/0!, #REF!, etc.)
    DATE,       // 'd' - ISO 8601 date (2024-01-15)
    DATE_TIME,  // 't' - ISO 8601 datetime (2024-01-15T10:30:00Z)

    // Formula result types - formula that has been evaluated
    // These are never serialized to file (serializer outputs 'f' for all)
    // but allow code to know both "it's a formula" and "the result type"
    FORMULA_NUMBER,   // Formula that evaluates to a number
    FORMULA_STRING,   // Formula that evaluates to a string
    FORMULA_BOOLEAN,  // Formula that evaluates to a boolean
    FORMULA_ERROR,    // Formula that evaluates to an error
    FORMULA_EMPTY,    // Formula that evaluates to empty
};

// Check if a type represents a formula (either unevaluated or with result)
inline bool isFormulaType(CellValueType type) {
    switch (type) {
        case CellValueType::FORMULA:
        case CellValueType::FORMULA_NUMBER:
        case CellValueType::FORMULA_STRING:
        case CellValueType::FORMULA_BOOLEAN:
        case CellValueType::FORMULA_ERROR:
        case CellValueType::FORMULA_EMPTY:
            return true;
        default:
            return false;
    }
}

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
        case CellValueType::FORMULA_NUMBER:
        case CellValueType::FORMULA_STRING:
        case CellValueType::FORMULA_BOOLEAN:
        case CellValueType::FORMULA_ERROR:
        case CellValueType::FORMULA_EMPTY:
            return 'f';  // All formula types serialize as 'f'
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
enum class CellError : std::uint8_t {
    NONE,      // No error
    VALUE,     // #VALUE! - wrong type of argument
    REF,       // #REF! - invalid cell reference
    NAME,      // #NAME? - unrecognized formula name
    DIV,       // #DIV/0! - division by zero
    NULL_REF,  // #NULL! - incorrect range
    NUM,       // #NUM! - invalid numeric value
    CIRCULAR,  // Circular reference detected
    NA,        // #N/A - value not available (e.g., lookup not found)
    SPILL,     // #SPILL! - array formula blocked by existing data
    CALC,      // #CALC! - calculation error (e.g., FILTER with no results)
};

// Error strings as they appear in file format
constexpr const char* ERROR_VALUE_STR = "#VALUE!";
constexpr const char* ERROR_REF_STR = "#REF!";
constexpr const char* ERROR_NAME_STR = "#NAME?";
constexpr const char* ERROR_DIV_STR = "#DIV/0!";
constexpr const char* ERROR_NULL_STR = "#NULL!";
constexpr const char* ERROR_NUM_STR = "#NUM!";
constexpr const char* ERROR_CIRCULAR_STR = "#CIRCULAR!";
constexpr const char* ERROR_NA_STR = "#N/A";
constexpr const char* ERROR_SPILL_STR = "#SPILL!";
constexpr const char* ERROR_CALC_STR = "#CALC!";

// Convert error string to CellError
inline CellError stringToError(const std::string& s) {
    if (s == ERROR_VALUE_STR) {
        return CellError::VALUE;
    }
    if (s == ERROR_REF_STR) {
        return CellError::REF;
    }
    if (s == ERROR_NAME_STR) {
        return CellError::NAME;
    }
    if (s == ERROR_DIV_STR) {
        return CellError::DIV;
    }
    if (s == ERROR_NULL_STR) {
        return CellError::NULL_REF;
    }
    if (s == ERROR_NUM_STR) {
        return CellError::NUM;
    }
    if (s == ERROR_CIRCULAR_STR) {
        return CellError::CIRCULAR;
    }
    if (s == ERROR_NA_STR) {
        return CellError::NA;
    }
    if (s == ERROR_SPILL_STR) {
        return CellError::SPILL;
    }
    if (s == ERROR_CALC_STR) {
        return CellError::CALC;
    }
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
        case CellError::NA:
            return ERROR_NA_STR;
        case CellError::SPILL:
            return ERROR_SPILL_STR;
        case CellError::CALC:
            return ERROR_CALC_STR;
    }
    return "";
}

// Default sizes for axes
constexpr uint32_t DEFAULT_COLUMN_WIDTH = 100;
constexpr uint32_t DEFAULT_ROW_HEIGHT = 24;

}  // namespace cells

#endif  // CELLS_TYPES_H_
