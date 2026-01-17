#include "core/cells/ref_converter.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include <algorithm>

// Logging macro (matches bindings.cc)
#ifdef ENABLE_DEBUG_LOGGING
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...) ((void)0)
#endif

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

    for (const auto& col : columns) {
        const std::string idStr = col.second.toString();
        const size_t position = col.first;
        colIdToIndex_[idStr] = position;
        indexToColId_[position] = idStr;  // Map position to ID for A1->UUID conversion
    }
    for (const auto& row : rows) {
        const std::string idStr = row.second.toString();
        const size_t position = row.first;
        rowIdToIndex_[idStr] = position;
        indexToRowId_[position] = idStr;  // Map position to ID for A1->UUID conversion
    }

    // Build cell lookup maps
    const std::vector<ID> cellIds = sheet.getCellIds();
    const Workbook* wb = sheet.getWorkbook();
    cellIdToLocation_.reserve(cellIds.size());
    locationToCellId_.reserve(cellIds.size());
    for (const ID& cellId : cellIds) {
        const Cell* cell = wb ? wb->getCell(cellId) : nullptr;
        if (!cell) {
            continue;
        }
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
    _workbook = nullptr;
}

void RefConverter::setWorkbook(const Workbook* workbook) {
    _workbook = workbook;
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

    // Look up col/row UUIDs from positions (using maps)
    auto colIt = indexToColId_.find(ref.colIndex);
    auto rowIt = indexToRowId_.find(ref.rowIndex);
    if (colIt == indexToColId_.end() || rowIt == indexToRowId_.end()) {
        return "";  // Column or row at this position doesn't exist
    }

    const std::string& colId = colIt->second;
    const std::string& rowId = rowIt->second;

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
            // Use ~~ prefix for relative refs so parser recognizes them as UUID_CELL_REF
            return "~~" + cellId;
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
        } else if (c == '~' && c2 == '~') {
            refType = ReferenceType::RELATIVE;
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
        // Make sure this isn't part of a function name (preceded by alphanumeric or underscore)
        // Function names like SEQUENCE, TRANSPOSE are 8 chars and would match otherwise
        if (pos > 0) {
            const char prev = formula[pos - 1];
            if (std::isalnum(prev) != 0 || prev == '_') {
                return 0;
            }
        }
        // Make sure this isn't a function call (followed by '(')
        // Cell IDs are never followed directly by '(' in valid formulas
        if (pos + 8 < formula.size() && formula[pos + 8] == '(') {
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
    const Sheet* currentSheetContext = nullptr;  // Track cross-sheet context

    while (i < formula.size()) {
        // Check for sheet UUID prefix: !<8-char-uuid>
        if (formula[i] == '!' && i + 9 <= formula.size()) {
            // Check if followed by 8 alphanumeric characters (sheet UUID)
            bool validSheetUuid = true;
            for (size_t j = 1; j <= 8 && validSheetUuid; ++j) {
                const char c = formula[i + j];
                if (std::isalnum(static_cast<unsigned char>(c)) == 0) {
                    validSheetUuid = false;
                }
            }

            if (validSheetUuid) {
                // Extract sheet UUID
                const std::string sheetId = formula.substr(i + 1, 8);

                // Look up sheet from workbook for both name and cell lookups
                if (_workbook != nullptr) {
                    const ID sheetIdObj(sheetId);
                    const Sheet* sheet = _workbook->getSheet(sheetIdObj);
                    if (sheet != nullptr) {
                        result += sheet->name + "!";
                        currentSheetContext = sheet;  // Use this sheet for subsequent cell lookups
                    } else {
                        // Sheet not found, keep the raw format
                        result += "!" + sheetId;
                    }
                } else {
                    result += "!" + sheetId;
                }

                i += 9;  // Skip !<sheetId>
                continue;
            }
        }

        // Check for column UUID ref: @$ or @~ followed by 8 alphanumeric chars
        if (formula[i] == '@' && i + 10 <= formula.size()) {
            const char absMarker = formula[i + 1];
            if (absMarker == '$' || absMarker == '~') {
                bool validColUuid = true;
                for (size_t j = 2; j < 10 && validColUuid; ++j) {
                    if (std::isalnum(static_cast<unsigned char>(formula[i + j])) == 0) {
                        validColUuid = false;
                    }
                }
                if (validColUuid) {
                    const std::string colId = formula.substr(i + 2, 8);
                    const bool isAbsolute = (absMarker == '$');
                    bool found = false;

                    // Look up column in cross-sheet context first
                    if (currentSheetContext != nullptr) {
                        const ID colIdObj(colId);
                        auto colIt = currentSheetContext->columns.find(colIdObj);
                        if (colIt != currentSheetContext->columns.end()) {
                            const std::string colName =
                                columnIndexToLetter(colIt->second->position);
                            if (isAbsolute) {
                                result += '$';
                            }
                            result += colName;
                            result += ':';
                            result += colName;
                            found = true;
                        }
                    }

                    // Fall back to current sheet context (using lookup maps)
                    if (!found) {
                        auto colIt = colIdToIndex_.find(colId);
                        if (colIt != colIdToIndex_.end()) {
                            const std::string colName = columnIndexToLetter(colIt->second);
                            if (isAbsolute) {
                                result += '$';
                            }
                            result += colName;
                            result += ':';
                            result += colName;
                            found = true;
                        }
                    }

                    if (!found) {
                        // Column not found - output #REF!
                        result += "#REF!";
                    }

                    i += 10;  // Skip @<abs><8-char-id>
                    // Clear cross-sheet context unless next is ':'
                    if (i >= formula.size() || formula[i] != ':') {
                        currentSheetContext = nullptr;
                    }
                    continue;
                }
            }
        }

        // Check for row UUID ref: #$ or #~ followed by 8 alphanumeric chars
        if (formula[i] == '#' && i + 10 <= formula.size()) {
            const char absMarker = formula[i + 1];
            if (absMarker == '$' || absMarker == '~') {
                bool validRowUuid = true;
                for (size_t j = 2; j < 10 && validRowUuid; ++j) {
                    if (std::isalnum(static_cast<unsigned char>(formula[i + j])) == 0) {
                        validRowUuid = false;
                    }
                }
                if (validRowUuid) {
                    const std::string rowId = formula.substr(i + 2, 8);
                    const bool isAbsolute = (absMarker == '$');
                    bool found = false;

                    // Look up row in cross-sheet context first
                    if (currentSheetContext != nullptr) {
                        const ID rowIdObj(rowId);
                        auto rowIt = currentSheetContext->rows.find(rowIdObj);
                        if (rowIt != currentSheetContext->rows.end()) {
                            const std::string rowNum = std::to_string(rowIt->second->position + 1);
                            if (isAbsolute) {
                                result += '$';
                            }
                            result += rowNum;
                            result += ':';
                            result += rowNum;
                            found = true;
                        }
                    }

                    // Fall back to current sheet context (using lookup maps)
                    if (!found) {
                        auto rowIt = rowIdToIndex_.find(rowId);
                        if (rowIt != rowIdToIndex_.end()) {
                            const std::string rowNum = std::to_string(rowIt->second + 1);
                            if (isAbsolute) {
                                result += '$';
                            }
                            result += rowNum;
                            result += ':';
                            result += rowNum;
                            found = true;
                        }
                    }

                    if (!found) {
                        // Row not found - output #REF!
                        result += "#REF!";
                    }

                    i += 10;  // Skip #<abs><8-char-id>
                    // Clear cross-sheet context unless next is ':'
                    if (i >= formula.size() || formula[i] != ':') {
                        currentSheetContext = nullptr;
                    }
                    continue;
                }
            }
        }

        std::string cellId;
        ReferenceType refType = ReferenceType::RELATIVE;
        const size_t len = extractCellRef(formula, i, cellId, refType);

        if (len > 0) {
            bool found = false;

            // If we have a cross-sheet context, look up in that sheet
            if (currentSheetContext != nullptr && _workbook != nullptr) {
                const ID cellIdObj(cellId);
                const Cell* cell = _workbook->getCell(cellIdObj);
                if (cell != nullptr) {
                    // Look up column and row positions from the cross-sheet
                    auto colIt = currentSheetContext->columns.find(cell->colId);
                    auto rowIt = currentSheetContext->rows.find(cell->rowId);

                    if (colIt != currentSheetContext->columns.end() &&
                        rowIt != currentSheetContext->rows.end()) {
                        // Build A1 notation with absolute markers
                        if (refType == ReferenceType::ABSOLUTE ||
                            refType == ReferenceType::COL_ABS) {
                            result += '$';
                        }
                        result += columnIndexToLetter(colIt->second->position);
                        if (refType == ReferenceType::ABSOLUTE ||
                            refType == ReferenceType::ROW_ABS) {
                            result += '$';
                        }
                        result += std::to_string(rowIt->second->position + 1);
                        found = true;
                    }
                }
                // Keep cross-sheet context if next char is ':' (for range second part)
                // Otherwise clear it
                if (i + len >= formula.size() || formula[i + len] != ':') {
                    currentSheetContext = nullptr;
                }
            }

            // Fall back to current sheet context
            if (!found) {
                auto cellIt = cellIdToLocation_.find(cellId);
                if (cellIt != cellIdToLocation_.end()) {
                    const CellLocation& loc = cellIt->second;
                    auto colIt = colIdToIndex_.find(loc.colId);
                    auto rowIt = rowIdToIndex_.find(loc.rowId);

                    if (colIt != colIdToIndex_.end() && rowIt != rowIdToIndex_.end()) {
                        // Build A1 notation with absolute markers
                        if (refType == ReferenceType::ABSOLUTE ||
                            refType == ReferenceType::COL_ABS) {
                            result += '$';
                        }
                        result += columnIndexToLetter(colIt->second);
                        if (refType == ReferenceType::ABSOLUTE ||
                            refType == ReferenceType::ROW_ABS) {
                            result += '$';
                        }
                        result += std::to_string(rowIt->second + 1);
                        found = true;
                    }
                }
            }

            if (!found) {
                // Cell not found - output #REF! (standard Excel error for broken references)
                result += "#REF!";
            }

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

// ============================================================================
// AST-Based Reference Adjustment
// ============================================================================

// Helper to adjust a CellRefNode and return a new node (either adjusted CellRefNode or ErrorNode)
static std::unique_ptr<ASTNode> adjustCellRefNode(const CellRefNode* node, int colOffset,
                                                  int rowOffset) {
    // Clone the node first
    auto cloned = std::unique_ptr<CellRefNode>(static_cast<CellRefNode*>(node->clone().release()));

    // Adjust column if relative
    if (!node->colAbsolute) {
        const int colIndex = RefConverter::columnLetterToIndex(node->column);
        if (colIndex < 0) {
            // Invalid column, return error
            return std::make_unique<ErrorNode>("#REF!", node->position);
        }
        const int newColIndex = colIndex + colOffset;
        if (newColIndex < 0) {
            return std::make_unique<ErrorNode>("#REF!", node->position);
        }
        cloned->column = RefConverter::columnIndexToLetter(static_cast<size_t>(newColIndex));
    }

    // Adjust row if relative
    if (!node->rowAbsolute) {
        const int newRow = node->row + rowOffset;
        if (newRow < 1) {
            return std::make_unique<ErrorNode>("#REF!", node->position);
        }
        cloned->row = newRow;
    }

    // Clear cellId since it's no longer valid after adjustment
    cloned->cellId.clear();

    return cloned;
}

std::unique_ptr<ASTNode> RefConverter::adjustASTReferences(const ASTNode* ast, int colOffset,
                                                           int rowOffset) {
    if (ast == nullptr) {
        return nullptr;
    }

    switch (ast->type) {
        case ASTNodeType::CELL_REF: {
            const auto* cellRef = static_cast<const CellRefNode*>(ast);
            return adjustCellRefNode(cellRef, colOffset, rowOffset);
        }

        case ASTNodeType::RANGE_REF: {
            const auto* rangeRef = static_cast<const RangeRefNode*>(ast);
            auto adjustedTopLeft = adjustCellRefNode(rangeRef->topLeft.get(), colOffset, rowOffset);
            auto adjustedBottomRight =
                adjustCellRefNode(rangeRef->bottomRight.get(), colOffset, rowOffset);

            // If either becomes an error, return an error node
            if (adjustedTopLeft->type == ASTNodeType::ERROR_NODE ||
                adjustedBottomRight->type == ASTNodeType::ERROR_NODE) {
                return std::make_unique<ErrorNode>("#REF!", ast->position);
            }

            // Both are valid CellRefNodes
            auto newRange = std::make_unique<RangeRefNode>(
                std::unique_ptr<CellRefNode>(static_cast<CellRefNode*>(adjustedTopLeft.release())),
                std::unique_ptr<CellRefNode>(
                    static_cast<CellRefNode*>(adjustedBottomRight.release())),
                ast->position);
            return newRange;
        }

        case ASTNodeType::COLUMN_REF: {
            const auto* colRef = static_cast<const ColumnRefNode*>(ast);
            auto cloned = std::unique_ptr<ColumnRefNode>(
                static_cast<ColumnRefNode*>(colRef->clone().release()));

            // Adjust column if relative
            if (!colRef->absolute) {
                const int colIndex = columnLetterToIndex(colRef->column);
                if (colIndex < 0) {
                    return std::make_unique<ErrorNode>("#REF!", ast->position);
                }
                const int newColIndex = colIndex + colOffset;
                if (newColIndex < 0) {
                    return std::make_unique<ErrorNode>("#REF!", ast->position);
                }
                cloned->column = columnIndexToLetter(static_cast<size_t>(newColIndex));
                cloned->columnId.clear();
            }

            return cloned;
        }

        case ASTNodeType::ROW_REF: {
            const auto* rowRef = static_cast<const RowRefNode*>(ast);
            auto cloned =
                std::unique_ptr<RowRefNode>(static_cast<RowRefNode*>(rowRef->clone().release()));

            // Adjust row if relative
            if (!rowRef->absolute) {
                const int newRow = rowRef->row + rowOffset;
                if (newRow < 1) {
                    return std::make_unique<ErrorNode>("#REF!", ast->position);
                }
                cloned->row = newRow;
                cloned->rowId.clear();
            }

            return cloned;
        }

        case ASTNodeType::COLUMN_RANGE_REF: {
            const auto* colRangeRef = static_cast<const ColumnRangeRefNode*>(ast);
            auto cloned = std::unique_ptr<ColumnRangeRefNode>(
                static_cast<ColumnRangeRefNode*>(colRangeRef->clone().release()));

            // Adjust start column if relative
            if (!colRangeRef->startAbsolute) {
                const int startIndex = columnLetterToIndex(colRangeRef->startColumn);
                if (startIndex < 0) {
                    return std::make_unique<ErrorNode>("#REF!", ast->position);
                }
                const int newStartIndex = startIndex + colOffset;
                if (newStartIndex < 0) {
                    return std::make_unique<ErrorNode>("#REF!", ast->position);
                }
                cloned->startColumn = columnIndexToLetter(static_cast<size_t>(newStartIndex));
                cloned->startColumnId.clear();
            }

            // Adjust end column if relative
            if (!colRangeRef->endAbsolute) {
                const int endIndex = columnLetterToIndex(colRangeRef->endColumn);
                if (endIndex < 0) {
                    return std::make_unique<ErrorNode>("#REF!", ast->position);
                }
                const int newEndIndex = endIndex + colOffset;
                if (newEndIndex < 0) {
                    return std::make_unique<ErrorNode>("#REF!", ast->position);
                }
                cloned->endColumn = columnIndexToLetter(static_cast<size_t>(newEndIndex));
                cloned->endColumnId.clear();
            }

            return cloned;
        }

        case ASTNodeType::ROW_RANGE_REF: {
            const auto* rowRangeRef = static_cast<const RowRangeRefNode*>(ast);
            auto cloned = std::unique_ptr<RowRangeRefNode>(
                static_cast<RowRangeRefNode*>(rowRangeRef->clone().release()));

            // Adjust start row if relative
            if (!rowRangeRef->startAbsolute) {
                const int newStartRow = rowRangeRef->startRow + rowOffset;
                if (newStartRow < 1) {
                    return std::make_unique<ErrorNode>("#REF!", ast->position);
                }
                cloned->startRow = newStartRow;
                cloned->startRowId.clear();
            }

            // Adjust end row if relative
            if (!rowRangeRef->endAbsolute) {
                const int newEndRow = rowRangeRef->endRow + rowOffset;
                if (newEndRow < 1) {
                    return std::make_unique<ErrorNode>("#REF!", ast->position);
                }
                cloned->endRow = newEndRow;
                cloned->endRowId.clear();
            }

            return cloned;
        }

        case ASTNodeType::BINARY_OP: {
            const auto* binOp = static_cast<const BinaryOpNode*>(ast);
            auto newLeft = adjustASTReferences(binOp->left.get(), colOffset, rowOffset);
            auto newRight = adjustASTReferences(binOp->right.get(), colOffset, rowOffset);
            auto newNode =
                std::make_unique<BinaryOpNode>(binOp->op, std::move(newLeft), std::move(newRight));
            newNode->position = ast->position;
            return newNode;
        }

        case ASTNodeType::UNARY_OP: {
            const auto* unaryOp = static_cast<const UnaryOpNode*>(ast);
            auto newOperand = adjustASTReferences(unaryOp->operand.get(), colOffset, rowOffset);
            auto newNode = std::make_unique<UnaryOpNode>(unaryOp->op, std::move(newOperand));
            newNode->position = ast->position;
            return newNode;
        }

        case ASTNodeType::FUNCTION_CALL: {
            const auto* funcCall = static_cast<const FunctionCallNode*>(ast);
            auto newNode = std::make_unique<FunctionCallNode>(funcCall->name);
            newNode->position = ast->position;
            newNode->isVolatile = funcCall->isVolatile;
            for (const auto& arg : funcCall->args) {
                newNode->args.push_back(adjustASTReferences(arg.get(), colOffset, rowOffset));
            }
            return newNode;
        }

        // Nodes that don't contain references - just clone
        case ASTNodeType::NUMBER_LITERAL:
        case ASTNodeType::STRING_LITERAL:
        case ASTNodeType::BOOLEAN_LITERAL:
        case ASTNodeType::NAMED_REF:
        case ASTNodeType::ERROR_NODE:
            return ast->clone();
    }

    // Unreachable, but return clone as fallback
    return ast->clone();
}

}  // namespace cells
