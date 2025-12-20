#include "core/cells/ref_converter.h"

#include <cctype>
#include <cstdlib>

#include <algorithm>

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
    cellIdToLocation_.clear();
    locationToCellId_.clear();

    indexToColId_.reserve(columns.size());
    indexToRowId_.reserve(rows.size());

    for (size_t i = 0; i < columns.size(); ++i) {
        const std::string idStr = columns[i].second.toString();
        colIdToIndex_[idStr] = i;
        indexToColId_.push_back(idStr);
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        const std::string idStr = rows[i].second.toString();
        rowIdToIndex_[idStr] = i;
        indexToRowId_.push_back(idStr);
    }

    // Build cell lookup maps
    cellIdToLocation_.reserve(sheet.cells.size());
    locationToCellId_.reserve(sheet.cells.size());
    for (const auto& pair : sheet.cells) {
        const Cell* cell = pair.second.get();
        const std::string cellIdStr = cell->id.toString();
        const std::string colIdStr = cell->colId.toString();
        const std::string rowIdStr = cell->rowId.toString();

        cellIdToLocation_[cellIdStr] = CellLocation{colIdStr, rowIdStr};
        locationToCellId_[makeLocationKey(colIdStr, rowIdStr)] = cellIdStr;
    }
}

void RefConverter::clearContext() {
    colIdToIndex_.clear();
    rowIdToIndex_.clear();
    indexToColId_.clear();
    indexToRowId_.clear();
    cellIdToLocation_.clear();
    locationToCellId_.clear();
}

std::string RefConverter::makeLocationKey(const std::string& colId, const std::string& rowId) {
    return colId + ":" + rowId;
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
    for (const char c : letter) {
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
    while (pos < ref.size() && (std::isdigit(ref[pos]) != 0)) {
        rowDigits += ref[pos];
        ++pos;
    }

    if (rowDigits.empty()) {
        return result;  // No row number found
    }

    // Convert to indices
    const int colIdx = columnLetterToIndex(colLetters);
    if (colIdx < 0) {
        return result;
    }

    // Use strtol instead of std::stoi to avoid exceptions
    const int rowNum = static_cast<int>(strtol(rowDigits.c_str(), nullptr, 10));
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
    const size_t colonPos = ref.find(':');
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

    // Look up col/row UUIDs from indices
    if (ref.colIndex >= indexToColId_.size() || ref.rowIndex >= indexToRowId_.size()) {
        return "";
    }

    const std::string& colId = indexToColId_[ref.colIndex];
    const std::string& rowId = indexToRowId_[ref.rowIndex];

    // Look up cell ID from (colId, rowId)
    auto cellIt = locationToCellId_.find(makeLocationKey(colId, rowId));
    if (cellIt == locationToCellId_.end()) {
        return "";  // Cell doesn't exist at this location
    }

    const std::string& cellId = cellIt->second;
    switch (ref.type) {
        case ReferenceType::ABSOLUTE:
            return "$$" + cellId;
        case ReferenceType::COL_ABS:
            return "$~" + cellId;
        case ReferenceType::ROW_ABS:
            return "~$" + cellId;
        case ReferenceType::RELATIVE:
        default:
            return cellId;
    }
}

// ============================================================================
// Cell UUID Ref Extraction (new format)
// ============================================================================

bool RefConverter::isCellRefStart(const std::string& formula, size_t pos) {
    // New format patterns:
    //   $$cellId  (10 chars) - both absolute
    //   $~cellId  (10 chars) - col absolute, row relative
    //   ~$cellId  (10 chars) - col relative, row absolute
    //   cellId    (8 chars)  - both relative (bare ID)
    //
    // cellId is 8 alphanumeric characters (base62)

    if (pos >= formula.size()) {
        return false;
    }

    const char c = formula[pos];

    // Check for prefix patterns
    if (c == '$' || c == '~') {
        // Need at least 2 prefix chars + 8 ID chars
        if (pos + 9 >= formula.size()) {
            return false;
        }
        const char c2 = formula[pos + 1];
        // Valid prefixes: $$, $~, ~$
        const bool validPrefix =
            (c == '$' && c2 == '$') || (c == '$' && c2 == '~') || (c == '~' && c2 == '$');
        if (!validPrefix) {
            return false;
        }
        // Check 8 alphanumeric chars after prefix
        for (size_t i = 2; i < 10; ++i) {
            if (std::isalnum(formula[pos + i]) == 0) {
                return false;
            }
        }
        return true;
    }

    // Check for bare cellId (8 alphanumeric chars, both relative)
    if (std::isalnum(c) != 0) {
        if (pos + 7 >= formula.size()) {
            return false;
        }
        // All 8 chars must be alphanumeric
        for (size_t i = 0; i < 8; ++i) {
            if (std::isalnum(formula[pos + i]) == 0) {
                return false;
            }
        }
        // Make sure we're not at the start of a longer identifier
        // (e.g., in "SUM123456" we don't want "M1234567" to match)
        const bool hasMoreAlnum = pos + 8 < formula.size() && std::isalnum(formula[pos + 8]) != 0;
        return !hasMoreAlnum;
    }

    return false;
}

size_t RefConverter::extractCellRef(const std::string& formula, size_t pos, std::string& cellId,
                                    ReferenceType& refType) {
    if (pos >= formula.size()) {
        return 0;
    }

    const char c = formula[pos];

    // Check for prefix patterns
    if (c == '$' || c == '~') {
        if (pos + 9 >= formula.size()) {
            return 0;
        }
        const char c2 = formula[pos + 1];

        if (c == '$' && c2 == '$') {
            refType = ReferenceType::ABSOLUTE;
        } else if (c == '$' && c2 == '~') {
            refType = ReferenceType::COL_ABS;
        } else if (c == '~' && c2 == '$') {
            refType = ReferenceType::ROW_ABS;
        } else {
            return 0;  // Invalid prefix
        }

        // Verify 8 alphanumeric chars
        for (size_t i = 2; i < 10; ++i) {
            if (std::isalnum(formula[pos + i]) == 0) {
                return 0;
            }
        }
        cellId = formula.substr(pos + 2, 8);
        return 10;  // 2 prefix chars + 8 ID chars
    }

    // Check for bare cellId (both relative)
    if (std::isalnum(c) != 0) {
        if (pos + 7 >= formula.size()) {
            return 0;
        }
        for (size_t i = 0; i < 8; ++i) {
            if (std::isalnum(formula[pos + i]) == 0) {
                return 0;
            }
        }
        // Make sure we're not in the middle of a longer identifier
        if (pos + 8 < formula.size() && std::isalnum(formula[pos + 8]) != 0) {
            return 0;
        }
        refType = ReferenceType::RELATIVE;
        cellId = formula.substr(pos, 8);
        return 8;
    }

    return 0;
}

// ============================================================================
// A1 Ref Extraction
// ============================================================================

bool RefConverter::isA1RefStart(const std::string& formula, size_t pos) {
    if (pos >= formula.size()) {
        return false;
    }

    // A1 ref can start with $ or a column letter
    const char c = formula[pos];
    return c == '$' || isColumnChar(c);
}

size_t RefConverter::extractA1Ref(const std::string& formula, size_t pos, CellRef& ref) {
    const size_t start = pos;

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
    while (pos < formula.size() && (std::isdigit(formula[pos]) != 0)) {
        rowDigits += formula[pos];
        ++pos;
    }

    if (rowDigits.empty()) {
        return 0;  // No row number found - not a valid cell ref
    }

    // Convert to indices
    const int colIdx = columnLetterToIndex(colLetters);
    if (colIdx < 0) {
        return 0;
    }

    // Use strtol instead of std::stoi to avoid exceptions
    const int rowNum = static_cast<int>(strtol(rowDigits.c_str(), nullptr, 10));
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
    // Format: $$cellId, $~cellId, ~$cellId, or cellId
    std::string cellId;
    ReferenceType refType = ReferenceType::RELATIVE;
    const size_t len = extractCellRef(ref, 0, cellId, refType);

    if (len == 0 || len != ref.size()) {
        return "";
    }

    // Look up the cell
    auto cellIt = cellIdToLocation_.find(cellId);
    if (cellIt == cellIdToLocation_.end()) {
        return "";
    }

    const CellLocation& loc = cellIt->second;
    auto colIt = colIdToIndex_.find(loc.colId);
    auto rowIt = rowIdToIndex_.find(loc.rowId);

    if (colIt == colIdToIndex_.end() || rowIt == rowIdToIndex_.end()) {
        return "";
    }

    // Build A1 notation with absolute markers
    std::string result;
    if (refType == ReferenceType::ABSOLUTE || refType == ReferenceType::COL_ABS) {
        result += '$';
    }
    result += columnIndexToLetter(colIt->second);
    if (refType == ReferenceType::ABSOLUTE || refType == ReferenceType::ROW_ABS) {
        result += '$';
    }
    result += std::to_string(rowIt->second + 1);
    return result;
}

std::string RefConverter::formulaToA1(const std::string& formula) const {
    std::string result;
    result.reserve(formula.size());

    size_t i = 0;
    while (i < formula.size()) {
        std::string cellId;
        ReferenceType refType = ReferenceType::RELATIVE;
        const size_t len = extractCellRef(formula, i, cellId, refType);

        if (len > 0) {
            // Found a cell UUID ref, look up the cell
            auto cellIt = cellIdToLocation_.find(cellId);
            if (cellIt != cellIdToLocation_.end()) {
                const CellLocation& loc = cellIt->second;
                auto colIt = colIdToIndex_.find(loc.colId);
                auto rowIt = rowIdToIndex_.find(loc.rowId);

                if (colIt != colIdToIndex_.end() && rowIt != rowIdToIndex_.end()) {
                    // Build A1 notation with absolute markers
                    if (refType == ReferenceType::ABSOLUTE || refType == ReferenceType::COL_ABS) {
                        result += '$';
                    }
                    result += columnIndexToLetter(colIt->second);
                    if (refType == ReferenceType::ABSOLUTE || refType == ReferenceType::ROW_ABS) {
                        result += '$';
                    }
                    result += std::to_string(rowIt->second + 1);
                    i += len;
                    continue;
                }
            }
            // Cell not found, keep original
            result += formula.substr(i, len);
            i += len;
            continue;
        }

        // Not a cell ref, copy character as-is
        result += formula[i];
        ++i;
    }

    return result;
}

// ============================================================================
// A1 to UUID Conversion
// ============================================================================

std::string RefConverter::a1RefToUuid(const std::string& ref) const {
    const CellRef parsed = parseA1Ref(ref);
    if (!parsed.valid) {
        return "";
    }

    return formatUuidRef(parsed);
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
        const bool canStartRef = (i == 0) || (std::isalnum(formula[i - 1]) == 0);

        if (canStartRef && isA1RefStart(formula, i)) {
            CellRef ref;
            const size_t len = extractA1Ref(formula, i, ref);

            if (len > 0 && ref.valid) {
                // Check if we're parsing a range (next char is ':')
                if (i + len < formula.size() && formula[i + len] == ':') {
                    // Range reference - convert both parts
                    const std::string startRef = formatUuidRef(ref);

                    // Parse the end of the range
                    CellRef endRef;
                    const size_t endLen = extractA1Ref(formula, i + len + 1, endRef);

                    if (endLen > 0 && endRef.valid) {
                        const std::string endRefStr = formatUuidRef(endRef);
                        if (!startRef.empty() && !endRefStr.empty()) {
                            result += startRef;
                            result += ':';
                            result += endRefStr;
                            i += len + 1 + endLen;
                            continue;
                        }
                    }
                }

                // Single cell reference
                const std::string uuidRef = formatUuidRef(ref);
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
