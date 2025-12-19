#include "core/cells/ref_converter.h"

#include <algorithm>
#include <cctype>

namespace cells {

// ============================================================================
// Context Management
// ============================================================================

void RefConverter::setContext(const Sheet& sheet) {
    // Build ordered column list
    std::vector<std::pair<uint32_t, ID>> columns;
    columns.reserve(sheet.columns.size());
    for (const auto& pair : sheet.columns) {
        columns.emplace_back(pair.second->position, pair.first);
    }
    std::sort(columns.begin(), columns.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Build ordered row list
    std::vector<std::pair<uint32_t, ID>> rows;
    rows.reserve(sheet.rows.size());
    for (const auto& pair : sheet.rows) {
        rows.emplace_back(pair.second->position, pair.first);
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Build lookup maps
    colIdToIndex_.clear();
    rowIdToIndex_.clear();
    indexToColId_.clear();
    indexToRowId_.clear();

    indexToColId_.reserve(columns.size());
    indexToRowId_.reserve(rows.size());

    for (size_t i = 0; i < columns.size(); ++i) {
        std::string idStr = columns[i].second.toString();
        colIdToIndex_[idStr] = i;
        indexToColId_.push_back(idStr);
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        std::string idStr = rows[i].second.toString();
        rowIdToIndex_[idStr] = i;
        indexToRowId_.push_back(idStr);
    }
}

void RefConverter::setContext(const std::vector<ID>& columnIds, const std::vector<ID>& rowIds) {
    colIdToIndex_.clear();
    rowIdToIndex_.clear();
    indexToColId_.clear();
    indexToRowId_.clear();

    indexToColId_.reserve(columnIds.size());
    indexToRowId_.reserve(rowIds.size());

    for (size_t i = 0; i < columnIds.size(); ++i) {
        std::string idStr = columnIds[i].toString();
        colIdToIndex_[idStr] = i;
        indexToColId_.push_back(idStr);
    }
    for (size_t i = 0; i < rowIds.size(); ++i) {
        std::string idStr = rowIds[i].toString();
        rowIdToIndex_[idStr] = i;
        indexToRowId_.push_back(idStr);
    }
}

void RefConverter::clearContext() {
    colIdToIndex_.clear();
    rowIdToIndex_.clear();
    indexToColId_.clear();
    indexToRowId_.clear();
}

// ============================================================================
// Utility Functions
// ============================================================================

std::string RefConverter::columnIndexToLetter(size_t index) {
    std::string result;
    size_t n = index + 1;  // Convert to 1-based (A=1, not A=0)
    while (n > 0) {
        n--;  // Adjust back for 0-based letter calculation
        result.insert(result.begin(), static_cast<char>('A' + (n % 26)));
        n = n / 26;
    }
    return result;
}

int RefConverter::columnLetterToIndex(const std::string& letter) {
    if (letter.empty()) {
        return -1;
    }

    int result = 0;
    for (char c : letter) {
        if (c >= 'A' && c <= 'Z') {
            result = result * 26 + (c - 'A' + 1);
        } else if (c >= 'a' && c <= 'z') {
            result = result * 26 + (c - 'a' + 1);
        } else {
            return -1;  // Invalid character
        }
    }
    return result - 1;  // Convert to 0-based
}

bool RefConverter::isColumnChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

CellRef RefConverter::parseA1Ref(const std::string& ref) {
    CellRef result;
    if (ref.empty()) {
        return result;
    }

    size_t pos = 0;

    // Check for absolute column marker
    if (pos < ref.size() && ref[pos] == '$') {
        result.type = ReferenceType::COL_ABS;
        ++pos;
    }

    // Parse column letters
    std::string colLetters;
    while (pos < ref.size() && isColumnChar(ref[pos])) {
        colLetters += static_cast<char>(std::toupper(ref[pos]));
        ++pos;
    }

    if (colLetters.empty()) {
        return result;  // No column letters found
    }

    // Check for absolute row marker
    if (pos < ref.size() && ref[pos] == '$') {
        if (result.type == ReferenceType::COL_ABS) {
            result.type = ReferenceType::ABSOLUTE;
        } else {
            result.type = ReferenceType::ROW_ABS;
        }
        ++pos;
    }

    // Parse row number
    std::string rowDigits;
    while (pos < ref.size() && std::isdigit(ref[pos])) {
        rowDigits += ref[pos];
        ++pos;
    }

    if (rowDigits.empty()) {
        return result;  // No row number found
    }

    // Convert to indices
    int colIdx = columnLetterToIndex(colLetters);
    if (colIdx < 0) {
        return result;
    }

    int rowNum = std::stoi(rowDigits);
    if (rowNum < 1) {
        return result;  // Excel rows are 1-based
    }

    result.colIndex = static_cast<size_t>(colIdx);
    result.rowIndex = static_cast<size_t>(rowNum - 1);  // Convert to 0-based
    result.valid = true;

    return result;
}

RangeRef RefConverter::parseRangeRef(const std::string& ref) {
    RangeRef result;

    // Find the colon separator
    size_t colonPos = ref.find(':');
    if (colonPos == std::string::npos) {
        // Not a range, treat as single cell
        result.start = parseA1Ref(ref);
        result.end = result.start;
        result.valid = result.start.valid;
        return result;
    }

    // Parse start and end references
    result.start = parseA1Ref(ref.substr(0, colonPos));
    result.end = parseA1Ref(ref.substr(colonPos + 1));
    result.valid = result.start.valid && result.end.valid;

    return result;
}

std::string RefConverter::formatA1Ref(const CellRef& ref) {
    if (!ref.valid) {
        return "";
    }

    std::string result;

    // Add absolute column marker if needed
    if (ref.isAbsoluteCol()) {
        result += '$';
    }

    // Add column letter
    result += columnIndexToLetter(ref.colIndex);

    // Add absolute row marker if needed
    if (ref.isAbsoluteRow()) {
        result += '$';
    }

    // Add row number (1-based)
    result += std::to_string(ref.rowIndex + 1);

    return result;
}

std::string RefConverter::formatUuidRef(const CellRef& ref) const {
    if (!ref.valid) {
        return "";
    }

    // Look up UUIDs from indices
    if (ref.colIndex >= indexToColId_.size() || ref.rowIndex >= indexToRowId_.size()) {
        return "";
    }

    return "$" + indexToColId_[ref.colIndex] + "$" + indexToRowId_[ref.rowIndex];
}

// ============================================================================
// UUID Ref Extraction
// ============================================================================

bool RefConverter::isUuidRefStart(const std::string& formula, size_t pos) {
    // Check for $<8-char-id>$<8-char-id> pattern
    // Minimum length: 1 + 8 + 1 + 8 = 18
    if (pos + 17 >= formula.size()) {
        return false;
    }
    if (formula[pos] != '$') {
        return false;
    }
    if (formula[pos + 9] != '$') {
        return false;
    }
    // Check that the 8 characters after first $ and second $ are valid ID characters
    // (alphanumeric for base62)
    for (size_t i = 1; i <= 8; ++i) {
        char c = formula[pos + i];
        if (!std::isalnum(c)) {
            return false;
        }
    }
    for (size_t i = 10; i <= 17; ++i) {
        char c = formula[pos + i];
        if (!std::isalnum(c)) {
            return false;
        }
    }
    return true;
}

size_t RefConverter::extractUuidRef(const std::string& formula, size_t pos, std::string& colId,
                                    std::string& rowId) {
    if (!isUuidRefStart(formula, pos)) {
        return 0;
    }

    colId = formula.substr(pos + 1, 8);
    rowId = formula.substr(pos + 10, 8);
    return 18;  // Total length consumed
}

// ============================================================================
// A1 Ref Extraction
// ============================================================================

bool RefConverter::isA1RefStart(const std::string& formula, size_t pos) {
    if (pos >= formula.size()) {
        return false;
    }

    // A1 ref can start with $ or a column letter
    char c = formula[pos];
    if (c == '$' || isColumnChar(c)) {
        return true;
    }
    return false;
}

size_t RefConverter::extractA1Ref(const std::string& formula, size_t pos, CellRef& ref) {
    size_t start = pos;

    // Reset ref
    ref = CellRef();

    // Check for absolute column marker
    if (pos < formula.size() && formula[pos] == '$') {
        ref.type = ReferenceType::COL_ABS;
        ++pos;
    }

    // Parse column letters
    std::string colLetters;
    while (pos < formula.size() && isColumnChar(formula[pos])) {
        colLetters += static_cast<char>(std::toupper(formula[pos]));
        ++pos;
    }

    if (colLetters.empty()) {
        return 0;  // No column letters found
    }

    // Check for absolute row marker
    if (pos < formula.size() && formula[pos] == '$') {
        if (ref.type == ReferenceType::COL_ABS) {
            ref.type = ReferenceType::ABSOLUTE;
        } else {
            ref.type = ReferenceType::ROW_ABS;
        }
        ++pos;
    }

    // Parse row number
    std::string rowDigits;
    while (pos < formula.size() && std::isdigit(formula[pos])) {
        rowDigits += formula[pos];
        ++pos;
    }

    if (rowDigits.empty()) {
        return 0;  // No row number found - not a valid cell ref
    }

    // Convert to indices
    int colIdx = columnLetterToIndex(colLetters);
    if (colIdx < 0) {
        return 0;
    }

    int rowNum = std::stoi(rowDigits);
    if (rowNum < 1) {
        return 0;
    }

    ref.colIndex = static_cast<size_t>(colIdx);
    ref.rowIndex = static_cast<size_t>(rowNum - 1);
    ref.valid = true;

    return pos - start;
}

// ============================================================================
// UUID to A1 Conversion
// ============================================================================

std::string RefConverter::uuidRefToA1(const std::string& ref) const {
    // Expected format: $colId$rowId (18 chars)
    if (ref.size() != 18 || ref[0] != '$' || ref[9] != '$') {
        return "";
    }

    std::string colId = ref.substr(1, 8);
    std::string rowId = ref.substr(10, 8);

    // Look up indices
    auto colIt = colIdToIndex_.find(colId);
    auto rowIt = rowIdToIndex_.find(rowId);

    if (colIt == colIdToIndex_.end() || rowIt == rowIdToIndex_.end()) {
        return "";
    }

    // Convert to A1 notation
    return columnIndexToLetter(colIt->second) + std::to_string(rowIt->second + 1);
}

std::string RefConverter::formulaToA1(const std::string& formula) const {
    std::string result;
    result.reserve(formula.size());

    size_t i = 0;
    while (i < formula.size()) {
        std::string colId;
        std::string rowId;
        size_t len = extractUuidRef(formula, i, colId, rowId);

        if (len > 0) {
            // Found a UUID ref, convert it
            auto colIt = colIdToIndex_.find(colId);
            auto rowIt = rowIdToIndex_.find(rowId);

            if (colIt != colIdToIndex_.end() && rowIt != rowIdToIndex_.end()) {
                // Successfully converted
                result += columnIndexToLetter(colIt->second);
                result += std::to_string(rowIt->second + 1);
            } else {
                // Couldn't find the IDs, keep original (or mark as error?)
                result += formula.substr(i, len);
            }
            i += len;
        } else {
            // Not a UUID ref, copy character as-is
            result += formula[i];
            ++i;
        }
    }

    return result;
}

// ============================================================================
// A1 to UUID Conversion
// ============================================================================

std::string RefConverter::a1RefToUuid(const std::string& ref) const {
    CellRef parsed = parseA1Ref(ref);
    if (!parsed.valid) {
        return "";
    }

    // Look up UUIDs
    if (parsed.colIndex >= indexToColId_.size() || parsed.rowIndex >= indexToRowId_.size()) {
        return "";
    }

    return "$" + indexToColId_[parsed.colIndex] + "$" + indexToRowId_[parsed.rowIndex];
}

std::string RefConverter::formulaToUuid(const std::string& formula) const {
    std::string result;
    result.reserve(formula.size() * 2);  // UUID refs are longer

    size_t i = 0;
    while (i < formula.size()) {
        // Skip if we're in a string literal
        if (formula[i] == '"') {
            result += formula[i++];
            while (i < formula.size() && formula[i] != '"') {
                if (formula[i] == '\\' && i + 1 < formula.size()) {
                    result += formula[i++];
                }
                result += formula[i++];
            }
            if (i < formula.size()) {
                result += formula[i++];  // Closing quote
            }
            continue;
        }

        // Check for A1 reference
        // But first, make sure we're not in the middle of a word (e.g., "SUM" shouldn't match "M")
        // An A1 ref should not be preceded by an alphanumeric character
        bool canStartRef = (i == 0) || !std::isalnum(formula[i - 1]);

        if (canStartRef && isA1RefStart(formula, i)) {
            CellRef ref;
            size_t len = extractA1Ref(formula, i, ref);

            if (len > 0 && ref.valid) {
                // Check if we're parsing a range (next char is ':')
                if (i + len < formula.size() && formula[i + len] == ':') {
                    // Range reference - convert both parts
                    std::string startRef = formatUuidRef(ref);

                    // Parse the end of the range
                    CellRef endRef;
                    size_t endLen = extractA1Ref(formula, i + len + 1, endRef);

                    if (endLen > 0 && endRef.valid) {
                        std::string endRefStr = formatUuidRef(endRef);
                        if (!startRef.empty() && !endRefStr.empty()) {
                            result += startRef + ":" + endRefStr;
                            i += len + 1 + endLen;
                            continue;
                        }
                    }
                }

                // Single cell reference
                std::string uuidRef = formatUuidRef(ref);
                if (!uuidRef.empty()) {
                    result += uuidRef;
                    i += len;
                    continue;
                }
            }
        }

        // Not a reference, copy character as-is
        result += formula[i];
        ++i;
    }

    return result;
}

}  // namespace cells
