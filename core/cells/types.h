#ifndef CELLS_TYPES_H_
#define CELLS_TYPES_H_

#include <cstdint>
#include <string>

namespace cells {

// ID is an 8-character base62 string (62^8 = 218 trillion combinations)
// Examples: "Kj7mXp2Q", "fR3pK7wN"
using ID = std::string;

// Null ID represented as "~" in file format
constexpr const char* kNullID = "~";

// Check if an ID is null/empty
inline bool IsNullID(const ID& id) {
    return id.empty() || id == kNullID;
}

// Cell value types - stored as single char in file format
enum class CellValueType {
    kNumber,    // 'n' - numeric value (42, 3.14, -100)
    kString,    // 's' - quoted string ("Hello")
    kFormula,   // 'f' - formula with ID-based refs ("=$cA$r1+10")
    kBoolean,   // 'b' - true or false
    kError,     // 'e' - error value (#DIV/0!, #REF!, etc.)
    kDate,      // 'd' - ISO 8601 date (2024-01-15)
    kDateTime,  // 't' - ISO 8601 datetime (2024-01-15T10:30:00Z)
};

// Convert file format char to CellValueType
inline CellValueType CharToValueType(char c) {
    switch (c) {
        case 'n': return CellValueType::kNumber;
        case 's': return CellValueType::kString;
        case 'f': return CellValueType::kFormula;
        case 'b': return CellValueType::kBoolean;
        case 'e': return CellValueType::kError;
        case 'd': return CellValueType::kDate;
        case 't': return CellValueType::kDateTime;
        default:  return CellValueType::kString;  // Default to string
    }
}

// Convert CellValueType to file format char
inline char ValueTypeToChar(CellValueType type) {
    switch (type) {
        case CellValueType::kNumber:   return 'n';
        case CellValueType::kString:   return 's';
        case CellValueType::kFormula:  return 'f';
        case CellValueType::kBoolean:  return 'b';
        case CellValueType::kError:    return 'e';
        case CellValueType::kDate:     return 'd';
        case CellValueType::kDateTime: return 't';
    }
    return 's';  // Default to string
}

// Cell error types - stored as strings in file format
enum class CellError {
    kNone,      // No error
    kValue,     // #VALUE! - wrong type of argument
    kRef,       // #REF! - invalid cell reference
    kName,      // #NAME? - unrecognized formula name
    kDiv,       // #DIV/0! - division by zero
    kNull,      // #NULL! - incorrect range
    kNum,       // #NUM! - invalid numeric value
    kCircular,  // Circular reference detected
};

// Error strings as they appear in file format
constexpr const char* kErrorValue = "#VALUE!";
constexpr const char* kErrorRef = "#REF!";
constexpr const char* kErrorName = "#NAME?";
constexpr const char* kErrorDiv = "#DIV/0!";
constexpr const char* kErrorNull = "#NULL!";
constexpr const char* kErrorNum = "#NUM!";
constexpr const char* kErrorCircular = "#CIRCULAR!";

// Convert error string to CellError
inline CellError StringToError(const std::string& s) {
    if (s == kErrorValue) return CellError::kValue;
    if (s == kErrorRef) return CellError::kRef;
    if (s == kErrorName) return CellError::kName;
    if (s == kErrorDiv) return CellError::kDiv;
    if (s == kErrorNull) return CellError::kNull;
    if (s == kErrorNum) return CellError::kNum;
    if (s == kErrorCircular) return CellError::kCircular;
    return CellError::kNone;
}

// Convert CellError to error string
inline const char* ErrorToString(CellError error) {
    switch (error) {
        case CellError::kNone:     return "";
        case CellError::kValue:    return kErrorValue;
        case CellError::kRef:      return kErrorRef;
        case CellError::kName:     return kErrorName;
        case CellError::kDiv:      return kErrorDiv;
        case CellError::kNull:     return kErrorNull;
        case CellError::kNum:      return kErrorNum;
        case CellError::kCircular: return kErrorCircular;
    }
    return "";
}

// Default sizes for axes
constexpr uint32_t kDefaultColumnWidth = 100;
constexpr uint32_t kDefaultRowHeight = 24;

// ID length constant
constexpr size_t kIDLength = 8;

}  // namespace cells

#endif  // CELLS_TYPES_H_
