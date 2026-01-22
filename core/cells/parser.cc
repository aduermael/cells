#include "core/cells/parser.h"

#include <cstdlib>

#include <charconv>
#include <sstream>

#include "core/cells/formula_parser.h"
#include "core/cells/named_ranges.h"
#include "core/cells/range.h"
#include "core/cells/style_buffer.h"

namespace cells {

// --- ParseError ---

std::string ParseError::toString() const {
    std::ostringstream ss;
    ss << "line " << line;
    if (column > 0) {
        ss << ", column " << column;
    }
    ss << ": " << message;
    return ss.str();
}

// --- Parser ---

Parser::Parser() = default;

void Parser::reset() {
    lineNum_ = 0;
    workbook_ = nullptr;
    currentSheet_ = nullptr;
    errorMsg_.clear();
    pendingSharedFormulas_.clear();
    cellsByIdForResolution_.clear();
}

bool Parser::setError(const std::string& message) {
    errorMsg_ = message;
    return false;
}

bool Parser::setError(int line, const std::string& message) {
    lineNum_ = line;
    errorMsg_ = message;
    return false;
}

ParseResult Parser::parse(const std::string& content) {
    return parse(std::string_view(content));
}

ParseResult Parser::parse(std::string_view content) {
    reset();

    auto wb = std::make_unique<Workbook>();
    workbook_ = wb.get();

    // Parse line by line
    size_t pos = 0;
    lineNum_ = 0;

    while (pos < content.size()) {
        lineNum_++;

        // Find end of line
        size_t lineEnd = content.find('\n', pos);
        if (lineEnd == std::string_view::npos) {
            lineEnd = content.size();
        }

        // Extract line (without newline)
        std::string_view line = content.substr(pos, lineEnd - pos);

        // Remove trailing \r if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line = line.substr(0, line.size() - 1);
        }

        // Parse the line
        if (!parseLine(line)) {
            ParseResult result;
            result.error = ParseError(lineNum_, errorMsg_);
            return result;
        }

        pos = lineEnd + 1;
    }

    // Resolve shared formula references after all cells are parsed
    if (!resolveSharedFormulas()) {
        ParseResult result;
        result.error = ParseError(lineNum_, errorMsg_);
        return result;
    }

    ParseResult result;
    result.workbook = std::move(wb);
    return result;
}

bool Parser::parseLine(std::string_view line) {
    // Skip empty lines
    if (line.empty()) {
        return true;
    }

    // Skip lines that are only whitespace
    const size_t firstNonSpace = line.find_first_not_of(" \t");
    if (firstNonSpace == std::string_view::npos) {
        return true;
    }

    const char prefix = line[firstNonSpace];

    switch (prefix) {
        case '#':  // Comment line - skip
            return true;

        case 'D':  // Document
            return parseDocument(line.substr(firstNonSpace));

        case 'F':  // Custom format
            return parseFormat(line.substr(firstNonSpace));

        case 'S':  // Sheet
            return parseSheet(line.substr(firstNonSpace));

        case 'C':  // Column
            return parseColumn(line.substr(firstNonSpace));

        case 'R':  // Row or Range
            // Check if it's "RG" (Range) or "R " (Row)
            if (line.size() > firstNonSpace + 1 && line[firstNonSpace + 1] == 'G') {
                return parseRange(line.substr(firstNonSpace));
            }
            return parseRow(line.substr(firstNonSpace));

        case 'X':  // Cell
            return parseCell(line.substr(firstNonSpace));

        case 'Y':  // Style definition
            return parseStyle(line.substr(firstNonSpace));

        case 'N':  // Named range
            return parseNamedRange(line.substr(firstNonSpace));

        case 'V':  // Sheet view properties
            return parseSheetView(line.substr(firstNonSpace));

        case 'O':  // OpLog entry
            return parseOperation(line.substr(firstNonSpace));

        default:
            // Unknown line type - ignore gracefully
            return true;
    }
}

bool Parser::parseDocument(std::string_view line) {
    // Format: D <id> "<name>"
    if (line.size() < 2 || line[0] != 'D' || line[1] != ' ') {
        return setError("Invalid document line");
    }

    line = line.substr(2);  // Skip "D "

    // Parse ID (8 characters)
    const size_t spacePos = line.find(' ');
    if (spacePos == std::string_view::npos || spacePos < 1) {
        return setError("Missing document ID");
    }

    const std::string idStr(line.substr(0, spacePos));
    workbook_->id = ID(idStr);

    // Parse quoted name
    line = line.substr(spacePos + 1);
    std::string name;
    size_t consumed = 0;
    if (!parseQuotedString(line, name, consumed)) {
        return setError("Invalid document name, expected quoted string");
    }

    workbook_->name = std::move(name);
    return true;
}

bool Parser::parseFormat(std::string_view line) {
    // Format: F <id> "<format-code>"
    if (line.size() < 2 || line[0] != 'F' || line[1] != ' ') {
        return setError("Invalid format line");
    }

    line = line.substr(2);  // Skip "F "

    // Parse ID (8 characters)
    const size_t spacePos = line.find(' ');
    if (spacePos == std::string_view::npos || spacePos < 1) {
        return setError("Missing format ID");
    }

    const std::string idStr(line.substr(0, spacePos));
    const ID formatId(idStr);

    // Parse quoted format code
    line = line.substr(spacePos + 1);
    std::string formatCode;
    size_t consumed = 0;
    if (!parseQuotedString(line, formatCode, consumed)) {
        return setError("Invalid format code, expected quoted string");
    }

    // Register the custom format in the workbook
    workbook_->registerCustomFormat(formatId, formatCode);
    return true;
}

bool Parser::parseStyle(std::string_view line) {
    // Y lines (old style definitions) are no longer used.
    // Styles are now content-addressed and embedded directly in entities as base64.
    // Ignore Y lines (return true to not fail the parse).
    (void)line;
    return true;
}

// Helper to parse token from string_view, updating the view
static std::string_view parseToken(std::string_view& input) {
    // Skip leading whitespace
    const size_t start = input.find_first_not_of(" \t");
    if (start == std::string_view::npos) {
        input = "";
        return "";
    }
    input = input.substr(start);

    // Find end of token
    const size_t end = input.find_first_of(" \t");
    if (end == std::string_view::npos) {
        const std::string_view token = input;
        input = "";
        return token;
    }

    const std::string_view token = input.substr(0, end);
    input = input.substr(end);
    return token;
}

bool Parser::parseNamedRange(std::string_view line) {
    // Format: N "<name>" <scope:W|S> <scope-sheet-id|-> <target-type> <target-data>
    // Target data depends on type:
    //   CELL: <id1> <sheet-id>
    //   RANGE: <id1> <id2> <sheet-id>
    //   COLUMN/ROW: <id1> <sheet-id>
    //   COLUMN_RANGE/ROW_RANGE: <id1> <id2> <sheet-id>

    if (line.size() < 2 || line[0] != 'N' || line[1] != ' ') {
        return setError("Invalid named range line");
    }

    line = line.substr(2);  // Skip "N "

    // Skip leading whitespace
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string_view::npos) {
        return setError("Missing named range name");
    }
    line = line.substr(start);

    // Parse quoted name
    std::string name;
    size_t consumed = 0;
    if (!parseQuotedString(line, name, consumed)) {
        return setError("Invalid named range name, expected quoted string");
    }
    line = line.substr(consumed);

    // Parse scope (W or S)
    const std::string_view scopeToken = parseToken(line);
    if (scopeToken.empty()) {
        return setError("Missing named range scope");
    }

    NamedRangeScope scope = NamedRangeScope::WORKBOOK;  // Default to workbook scope
    if (scopeToken == "W") {
        scope = NamedRangeScope::WORKBOOK;
    } else if (scopeToken == "S") {
        scope = NamedRangeScope::SHEET;
    } else {
        return setError("Invalid named range scope: " + std::string(scopeToken));
    }

    // Parse scope sheet ID (- for null)
    const std::string_view scopeSheetToken = parseToken(line);
    if (scopeSheetToken.empty()) {
        return setError("Missing named range scope sheet ID");
    }
    ID scopeSheetId;
    if (scopeSheetToken != "-") {
        scopeSheetId = ID(std::string(scopeSheetToken));
    }

    // Parse target type
    const std::string_view typeToken = parseToken(line);
    if (typeToken.empty()) {
        return setError("Missing named range target type");
    }

    NamedRangeTarget::Type targetType = NamedRangeTarget::Type::CELL;  // Default to cell
    if (typeToken == "CELL") {
        targetType = NamedRangeTarget::Type::CELL;
    } else if (typeToken == "RANGE") {
        targetType = NamedRangeTarget::Type::RANGE;
    } else if (typeToken == "COLUMN") {
        targetType = NamedRangeTarget::Type::COLUMN;
    } else if (typeToken == "ROW") {
        targetType = NamedRangeTarget::Type::ROW;
    } else if (typeToken == "COLUMN_RANGE") {
        targetType = NamedRangeTarget::Type::COLUMN_RANGE;
    } else if (typeToken == "ROW_RANGE") {
        targetType = NamedRangeTarget::Type::ROW_RANGE;
    } else {
        return setError("Invalid named range target type: " + std::string(typeToken));
    }

    // Parse target data based on type
    NamedRangeTarget target;
    target.type = targetType;

    // Parse id1
    const std::string_view id1Token = parseToken(line);
    if (id1Token.empty()) {
        return setError("Missing named range target id1");
    }
    target.id1 = ID(std::string(id1Token));

    // For RANGE, COLUMN_RANGE, ROW_RANGE: parse id2
    if (targetType == NamedRangeTarget::Type::RANGE ||
        targetType == NamedRangeTarget::Type::COLUMN_RANGE ||
        targetType == NamedRangeTarget::Type::ROW_RANGE) {
        const std::string_view id2Token = parseToken(line);
        if (id2Token.empty()) {
            return setError("Missing named range target id2");
        }
        target.id2 = ID(std::string(id2Token));
    }

    // Parse target sheet ID (- for null)
    const std::string_view targetSheetToken = parseToken(line);
    if (targetSheetToken.empty()) {
        return setError("Missing named range target sheet ID");
    }
    if (targetSheetToken != "-") {
        target.sheetId = ID(std::string(targetSheetToken));
    }

    // Register the named range in the workbook
    NamedRangeRegistry* registry = workbook_->getNamedRanges();
    if (registry == nullptr) {
        return setError("Named range registry not available");
    }

    if (scope == NamedRangeScope::WORKBOOK) {
        registry->defineWorkbook(name, target);
    } else {
        registry->defineSheet(name, scopeSheetId, target);
    }

    return true;
}

bool Parser::parseSheet(std::string_view line) {
    // Format: S <id> "<name>"
    if (line.size() < 2 || line[0] != 'S' || line[1] != ' ') {
        return setError("Invalid sheet line");
    }

    line = line.substr(2);  // Skip "S "

    // Parse ID
    const size_t spacePos = line.find(' ');
    if (spacePos == std::string_view::npos || spacePos < 1) {
        return setError("Missing sheet ID");
    }

    const std::string idStr(line.substr(0, spacePos));
    const ID sheetId(idStr);

    // Parse quoted name
    line = line.substr(spacePos + 1);
    std::string name;
    size_t consumed = 0;
    if (!parseQuotedString(line, name, consumed)) {
        return setError("Invalid sheet name, expected quoted string");
    }

    // Create sheet and add to workbook
    auto sheet = std::make_unique<Sheet>(sheetId, std::move(name));
    currentSheet_ = sheet.get();
    workbook_->addSheet(std::move(sheet));

    return true;
}

bool Parser::parseSheetView(std::string_view line) {
    // Format: V <properties...>
    // Properties: key:value pairs (e.g., showGridLines:0)
    if (currentSheet_ == nullptr) {
        return setError("Sheet view properties outside of sheet");
    }

    if (line.size() < 2 || line[0] != 'V' || line[1] != ' ') {
        return setError("Invalid sheet view line");
    }

    line = line.substr(2);  // Skip "V "

    // Parse key:value pairs
    while (!line.empty()) {
        // Skip leading whitespace
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string_view::npos) {
            break;
        }
        line = line.substr(start);

        // Find key:value pair
        const size_t colonPos = line.find(':');
        if (colonPos == std::string_view::npos) {
            break;
        }

        const std::string_view key = line.substr(0, colonPos);
        line = line.substr(colonPos + 1);

        if (line.empty()) {
            break;
        }

        // Parse value based on key
        if (key == "showGridLines") {
            // Boolean value: 0 or 1
            const size_t end = line.find_first_of(" \t");
            const std::string_view valueStr =
                (end == std::string_view::npos) ? line : line.substr(0, end);

            currentSheet_->showGridLines = (valueStr != "0");

            if (end == std::string_view::npos) {
                line = "";
            } else {
                line = line.substr(end);
            }
        } else if (key == "zoomScale") {
            // Integer value: 10-400
            const size_t end = line.find_first_of(" \t");
            const std::string_view valueStr =
                (end == std::string_view::npos) ? line : line.substr(0, end);

            int zoom = 100;
            auto result = std::from_chars(valueStr.data(), valueStr.data() + valueStr.size(), zoom);
            if (result.ec == std::errc{}) {
                // Clamp to valid range
                if (zoom < 10) {
                    zoom = 10;
                }
                if (zoom > 400) {
                    zoom = 400;
                }
                currentSheet_->zoomScale = static_cast<uint16_t>(zoom);
            }

            if (end == std::string_view::npos) {
                line = "";
            } else {
                line = line.substr(end);
            }
        } else if (key == "freezeCol") {
            // Integer value: number of frozen columns
            const size_t end = line.find_first_of(" \t");
            const std::string_view valueStr =
                (end == std::string_view::npos) ? line : line.substr(0, end);

            int freezeCol = 0;
            auto result =
                std::from_chars(valueStr.data(), valueStr.data() + valueStr.size(), freezeCol);
            if (result.ec == std::errc{} && freezeCol >= 0) {
                currentSheet_->freezeCol = static_cast<uint16_t>(freezeCol);
            }

            if (end == std::string_view::npos) {
                line = "";
            } else {
                line = line.substr(end);
            }
        } else if (key == "freezeRow") {
            // Integer value: number of frozen rows
            const size_t end = line.find_first_of(" \t");
            const std::string_view valueStr =
                (end == std::string_view::npos) ? line : line.substr(0, end);

            int freezeRow = 0;
            auto result =
                std::from_chars(valueStr.data(), valueStr.data() + valueStr.size(), freezeRow);
            if (result.ec == std::errc{} && freezeRow >= 0) {
                currentSheet_->freezeRow = static_cast<uint16_t>(freezeRow);
            }

            if (end == std::string_view::npos) {
                line = "";
            } else {
                line = line.substr(end);
            }
        } else {
            // Unknown property - skip value
            const size_t end = line.find_first_of(" \t");
            if (end == std::string_view::npos) {
                line = "";
            } else {
                line = line.substr(end);
            }
        }
    }

    return true;
}

bool Parser::parseQuotedString(std::string_view input, std::string& out, size_t& consumed) {
    // Find opening quote
    if (input.empty() || input[0] != '"') {
        return false;
    }

    out.clear();
    size_t pos = 1;  // Skip opening quote

    while (pos < input.size()) {
        const char c = input[pos];

        if (c == '"') {
            // End of string
            consumed = pos + 1;
            return true;
        }

        if (c == '\\' && pos + 1 < input.size()) {
            // Escape sequence
            const char next = input[pos + 1];
            switch (next) {
                case 'n':
                    out += '\n';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '"':
                    out += '"';
                    break;
                default:
                    out += next;
                    break;
            }
            pos += 2;
        } else {
            out += c;
            pos++;
        }
    }

    // Unterminated string
    return false;
}

std::optional<StyleBuffer> Parser::parseStyleValue(const std::string& value) const {
    // Decode base64 StyleBuffer (content-addressed format)
    return StyleBuffer::fromBase64(value);
}

bool Parser::parseAxisProps(std::string_view props, Axis& axis,
                            std::optional<StyleBuffer>* outStyle, ID* outFormatId) {
    // Format: key:value pairs separated by space
    // Examples: w:100 name:"Total" h:30 sty:<base64> fmt:FMT_C002

    while (!props.empty()) {
        // Skip leading whitespace
        const size_t start = props.find_first_not_of(" \t");
        if (start == std::string_view::npos) {
            break;
        }
        props = props.substr(start);

        // Find key:value pair
        const size_t colonPos = props.find(':');
        if (colonPos == std::string_view::npos) {
            break;
        }

        const std::string_view key = props.substr(0, colonPos);
        props = props.substr(colonPos + 1);

        if (props.empty()) {
            break;
        }

        // Parse value
        if (key == "w" || key == "h") {
            // Numeric value
            const size_t endPos = props.find_first_of(" \t");
            const std::string_view valueStr =
                (endPos == std::string_view::npos) ? props : props.substr(0, endPos);

            int value = 0;
            auto result =
                std::from_chars(valueStr.data(), valueStr.data() + valueStr.size(), value);
            if (result.ec != std::errc()) {
                return false;
            }

            axis.size = static_cast<uint32_t>(value);

            if (endPos == std::string_view::npos) {
                props = "";
            } else {
                props = props.substr(endPos);
            }
        } else if (key == "name") {
            // Quoted string value
            std::string name;
            size_t consumed = 0;
            if (!parseQuotedString(props, name, consumed)) {
                return false;
            }
            axis.name = std::move(name);
            props = props.substr(consumed);
        } else if (key == "hidden") {
            // Boolean value (0 or 1)
            const size_t endPos = props.find_first_of(" \t");
            const std::string_view valueStr =
                (endPos == std::string_view::npos) ? props : props.substr(0, endPos);

            axis.setHidden(valueStr == "1");

            if (endPos == std::string_view::npos) {
                props = "";
            } else {
                props = props.substr(endPos);
            }
        } else if (key == "sty") {
            // Style value - content-addressed base64
            const size_t endPos = props.find_first_of(" \t");
            const std::string_view valueStr =
                (endPos == std::string_view::npos) ? props : props.substr(0, endPos);

            if (outStyle != nullptr) {
                *outStyle = parseStyleValue(std::string(valueStr));
            }
            axis.setHasStyle(true);  // Set flag for hasStyle() accessor

            if (endPos == std::string_view::npos) {
                props = "";
            } else {
                props = props.substr(endPos);
            }
        } else if (key == "fmt") {
            // Format ID value - output via optional parameter (stored in workbook map by caller)
            const size_t endPos = props.find_first_of(" \t");
            const std::string_view valueStr =
                (endPos == std::string_view::npos) ? props : props.substr(0, endPos);

            if (outFormatId != nullptr) {
                *outFormatId = ID(std::string(valueStr));
            }
            axis.setHasFormat(true);  // Set flag for hasFormat() accessor

            if (endPos == std::string_view::npos) {
                props = "";
            } else {
                props = props.substr(endPos);
            }
        } else {
            // Unknown property - skip to next space
            const size_t endPos = props.find_first_of(" \t");
            if (endPos == std::string_view::npos) {
                props = "";
            } else {
                props = props.substr(endPos);
            }
        }
    }

    return true;
}

bool Parser::parseColumn(std::string_view line) {
    // Format: C <id> <position> [props...]
    if (currentSheet_ == nullptr) {
        return setError("Column outside of sheet");
    }

    if (line.size() < 2 || line[0] != 'C' || line[1] != ' ') {
        return setError("Invalid column line");
    }

    line = line.substr(2);  // Skip "C "

    // Tokenize: id, position
    std::string_view tokens[2];
    size_t tokenCount = 0;
    size_t propsStart = 0;

    size_t pos = 0;
    while (tokenCount < 2 && pos < line.size()) {
        // Skip whitespace
        const size_t start = line.find_first_not_of(" \t", pos);
        if (start == std::string_view::npos) {
            break;
        }

        // Find end of token
        size_t end = line.find_first_of(" \t", start);
        if (end == std::string_view::npos) {
            end = line.size();
        }

        tokens[tokenCount++] = line.substr(start, end - start);
        pos = end;
        propsStart = end;
    }

    if (tokenCount < 2) {
        return setError("Column requires id and position");
    }

    // Create axis
    auto col = std::make_unique<Axis>(ID(std::string(tokens[0])), true);

    // Parse position
    int position = 0;
    auto result = std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), position);
    if (result.ec != std::errc()) {
        return setError("Invalid column position");
    }
    col->position = static_cast<uint32_t>(position);

    // Parse optional properties (style stored as StyleBuffer, format as ID)
    std::optional<StyleBuffer> styleBuf;
    ID formatId;
    if (propsStart < line.size()) {
        if (!parseAxisProps(line.substr(propsStart), *col, &styleBuf, &formatId)) {
            return setError("Invalid column properties");
        }
    }

    // Store style/format in workbook (before moving col)
    const ID colId = col->id;
    currentSheet_->addColumn(std::move(col));
    if (workbook_ != nullptr && styleBuf.has_value()) {
        workbook_->setEntityStyle(colId, *styleBuf);
    }
    if (workbook_ != nullptr && !formatId.isNull()) {
        workbook_->setFormatId(colId, formatId);
    }
    return true;
}

bool Parser::parseRow(std::string_view line) {
    // Format: R <id> <position> [props...]
    if (currentSheet_ == nullptr) {
        return setError("Row outside of sheet");
    }

    if (line.size() < 2 || line[0] != 'R' || line[1] != ' ') {
        return setError("Invalid row line");
    }

    line = line.substr(2);  // Skip "R "

    // Tokenize: id, position
    std::string_view tokens[2];
    size_t tokenCount = 0;
    size_t propsStart = 0;

    size_t pos = 0;
    while (tokenCount < 2 && pos < line.size()) {
        const size_t start = line.find_first_not_of(" \t", pos);
        if (start == std::string_view::npos) {
            break;
        }

        size_t end = line.find_first_of(" \t", start);
        if (end == std::string_view::npos) {
            end = line.size();
        }

        tokens[tokenCount++] = line.substr(start, end - start);
        pos = end;
        propsStart = end;
    }

    if (tokenCount < 2) {
        return setError("Row requires id and position");
    }

    // Create axis
    auto row = std::make_unique<Axis>(ID(std::string(tokens[0])), false);

    // Parse position
    int position = 0;
    auto result = std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), position);
    if (result.ec != std::errc()) {
        return setError("Invalid row position");
    }
    row->position = static_cast<uint32_t>(position);

    // Parse optional properties (style stored as StyleBuffer, format as ID)
    std::optional<StyleBuffer> styleBuf;
    ID formatId;
    if (propsStart < line.size()) {
        if (!parseAxisProps(line.substr(propsStart), *row, &styleBuf, &formatId)) {
            return setError("Invalid row properties");
        }
    }

    // Store style/format in workbook (before moving row)
    const ID rowId = row->id;
    currentSheet_->addRow(std::move(row));
    if (workbook_ != nullptr && styleBuf.has_value()) {
        workbook_->setEntityStyle(rowId, *styleBuf);
    }
    if (workbook_ != nullptr && !formatId.isNull()) {
        workbook_->setFormatId(rowId, formatId);
    }
    return true;
}

bool Parser::parseCellValue(std::string_view value, char type, CellValue& out, size_t& consumed) {
    consumed = 0;
    switch (type) {
        case 'n': {
            // Number: 42, 3.14, -100 (ends at whitespace or end of string)
            size_t end = value.find_first_of(" \t");
            if (end == std::string_view::npos) {
                end = value.size();
            }
            const double num = std::strtod(std::string(value.substr(0, end)).c_str(), nullptr);
            out = CellValue(num);
            consumed = end;
            return true;
        }

        case 's': {
            // String: "Hello"
            std::string str;
            if (!parseQuotedString(value, str, consumed)) {
                return false;
            }
            out = CellValue(std::move(str));
            return true;
        }

        case 'f': {
            // Formula: "=$cA$r1+10"
            std::string formula;
            if (!parseQuotedString(value, formula, consumed)) {
                return false;
            }
            out.raw = std::move(formula);
            out.type = CellValueType::FORMULA;
            out.error = CellError::NONE;
            return true;
        }

        case 'b': {
            // Boolean: true or false
            if (value.substr(0, 4) == "true") {
                out = CellValue(true);
                consumed = 4;
            } else if (value.substr(0, 5) == "false") {
                out = CellValue(false);
                consumed = 5;
            } else {
                return false;
            }
            return true;
        }

        case 'e': {
            // Error: #DIV/0!, #REF!, etc. (ends at whitespace or end of string)
            size_t end = value.find_first_of(" \t");
            if (end == std::string_view::npos) {
                end = value.size();
            }
            out.type = CellValueType::ERROR;
            out.raw = std::string(value.substr(0, end));
            out.error = stringToError(out.raw);
            consumed = end;
            return true;
        }

        case 'd': {
            // Date: 2024-01-15 (ISO 8601)
            size_t end = value.find_first_of(" \t");
            if (end == std::string_view::npos) {
                end = value.size();
            }
            out.type = CellValueType::DATE;
            out.raw = std::string(value.substr(0, end));
            out.error = CellError::NONE;
            consumed = end;
            return true;
        }

        case 't': {
            // DateTime: 2024-01-15T10:30:00Z (ISO 8601)
            size_t end = value.find_first_of(" \t");
            if (end == std::string_view::npos) {
                end = value.size();
            }
            out.type = CellValueType::DATE_TIME;
            out.raw = std::string(value.substr(0, end));
            out.error = CellError::NONE;
            consumed = end;
            return true;
        }

        default:
            return false;
    }
}

bool Parser::parseCellProps(std::string_view props, Cell& cell) {
    // Parse optional properties: fmt:<formatId>
    // Format: key:value pairs separated by space

    while (!props.empty()) {
        // Skip leading whitespace
        const size_t start = props.find_first_not_of(" \t");
        if (start == std::string_view::npos) {
            break;
        }
        props = props.substr(start);

        // Find key:value pair
        const size_t colonPos = props.find(':');
        if (colonPos == std::string_view::npos) {
            break;
        }

        const std::string_view key = props.substr(0, colonPos);
        props = props.substr(colonPos + 1);

        if (props.empty()) {
            break;
        }

        // Parse value based on key
        if (key == "fmt") {
            // Format ID: 8 characters - store in workbook map, not cell
            size_t end = props.find_first_of(" \t");
            if (end == std::string_view::npos) {
                end = props.size();
            }
            const ID formatId(std::string(props.substr(0, end)));
            if (workbook_ != nullptr && !formatId.isNull()) {
                workbook_->setFormatId(cell.id, formatId);
                cell.markHasFormat();
            }
            props = (end < props.size()) ? props.substr(end) : "";
        } else if (key == "sty") {
            // Style value: content-addressed base64
            size_t end = props.find_first_of(" \t");
            if (end == std::string_view::npos) {
                end = props.size();
            }
            const std::string styleValue(props.substr(0, end));
            if (workbook_ != nullptr) {
                auto styleBuf = parseStyleValue(styleValue);
                if (styleBuf.has_value()) {
                    workbook_->setEntityStyle(cell.id, *styleBuf);
                    cell.markHasStyle();
                }
            }
            props = (end < props.size()) ? props.substr(end) : "";
        } else {
            // Unknown property - skip value
            const size_t endPos = props.find_first_of(" \t");
            if (endPos == std::string_view::npos) {
                props = "";
            } else {
                props = props.substr(endPos);
            }
        }
    }

    return true;
}

bool Parser::parseCell(std::string_view line) {
    // Format: X <id> <col> <row> <type> <value>
    if (currentSheet_ == nullptr) {
        return setError("Cell outside of sheet");
    }

    if (line.size() < 2 || line[0] != 'X' || line[1] != ' ') {
        return setError("Invalid cell line");
    }

    line = line.substr(2);  // Skip "X "

    // Tokenize: id, col, row, type
    std::string_view tokens[4];
    size_t tokenCount = 0;
    size_t valueStart = 0;

    size_t pos = 0;
    while (tokenCount < 4 && pos < line.size()) {
        const size_t start = line.find_first_not_of(" \t", pos);
        if (start == std::string_view::npos) {
            break;
        }

        size_t end = line.find_first_of(" \t", start);
        if (end == std::string_view::npos) {
            end = line.size();
        }

        tokens[tokenCount++] = line.substr(start, end - start);
        pos = end;
        valueStart = end;
    }

    if (tokenCount < 4) {
        return setError("Cell requires id, col, row, and type");
    }

    // Type is single char
    if (tokens[3].size() != 1) {
        return setError("Invalid cell type");
    }
    const char type = tokens[3][0];

    // Find value start (skip whitespace after type)
    if (valueStart < line.size()) {
        const size_t start = line.find_first_not_of(" \t", valueStart);
        if (start != std::string_view::npos) {
            valueStart = start;
        } else {
            valueStart = line.size();
        }
    }

    // Create cell
    auto cell = std::make_unique<Cell>(ID(std::string(tokens[0])), ID(std::string(tokens[1])),
                                       ID(std::string(tokens[2])));

    // Parse value (and consume it from valueStr)
    const std::string_view valueStr = (valueStart < line.size()) ? line.substr(valueStart) : "";
    size_t valueConsumed = 0;
    if (!parseCellValue(valueStr, type, cell->value, valueConsumed)) {
        return setError("Invalid cell value");
    }

    // After value, check for optional properties (e.g., fmt:FMT_C002)
    if (valueConsumed > 0 && valueConsumed < valueStr.size()) {
        const std::string_view propsStr = valueStr.substr(valueConsumed);
        parseCellProps(propsStr, *cell);
    }

    // If it's a formula, check for shared formula reference =@UUID
    if (type == 'f') {
        const std::string& formulaText = cell->value.raw;
        if (formulaText.size() >= 2 && formulaText[0] == '=' && formulaText[1] == '@') {
            // Shared formula reference: =@masterUUID
            // Extract the master UUID (8 characters after =@)
            const std::string masterUUID = formulaText.substr(2);
            if (masterUUID.size() == ID_LENGTH) {
                // Store for later resolution
                pendingSharedFormulas_[cell->id] = masterUUID;
            } else {
                return setError("Invalid shared formula reference: " + formulaText);
            }
        } else {
            // Regular formula - parse to create AST
            FormulaParser parser(formulaText);
            std::unique_ptr<ASTNode> ast = parser.parse();
            auto* formula = new Formula();
            formula->ast = ast.release();
            formula->dirty = true;
            cell->setFormula(formula);
        }
    }

    // Track cell for shared formula resolution
    Cell* cellPtr = cell.get();
    cellsByIdForResolution_[cell->id] = cellPtr;

    currentSheet_->addCell(std::move(cell));
    return true;
}

bool Parser::resolveSharedFormulas() {
    // Group pending shared formulas by master
    std::unordered_map<ID, std::vector<ID>, IDHash> masterToSubscribers;

    // Resolve all pending shared formula references
    for (const auto& [subscriberId, masterUUIDStr] : pendingSharedFormulas_) {
        // Find subscriber cell
        auto subscriberIt = cellsByIdForResolution_.find(subscriberId);
        if (subscriberIt == cellsByIdForResolution_.end()) {
            // This shouldn't happen, but handle gracefully
            return setError("Internal error: subscriber cell not found for shared formula");
        }
        Cell* subscriber = subscriberIt->second;

        // Find master cell by UUID
        const ID masterID(masterUUIDStr);
        auto masterIt = cellsByIdForResolution_.find(masterID);
        if (masterIt == cellsByIdForResolution_.end()) {
            return setError("Shared formula master not found: " + masterUUIDStr);
        }

        // Mark cell as a shared formula subscriber
        subscriber->setSharedFormulaSubscriber(true);

        // Group by master
        masterToSubscribers[masterID].push_back(subscriberId);
    }

    // Register shared formula groups at Sheet level
    for (const auto& [masterId, subscriberIds] : masterToSubscribers) {
        currentSheet_->registerSharedFormulaGroup(masterId, subscriberIds);
    }

    return true;
}

bool Parser::parseRange(std::string_view line) {
    // Format: RG <id> <start_col> <start_row> <end_col> <end_row> <flags> [sty:<styleId>]
    // Example: RG r8KjP2mN c1AbC2dE r1FgH2iJ c2KlM3nO r2PqR3sT 1 sty:s5WxY6zA

    if (line.size() < 3 || line[0] != 'R' || line[1] != 'G' || line[2] != ' ') {
        return setError("Invalid range line");
    }

    if (currentSheet_ == nullptr) {
        return setError("Range line before sheet definition");
    }

    line = line.substr(3);  // Skip "RG "

    // Parse range ID
    size_t spacePos = line.find(' ');
    if (spacePos == std::string_view::npos) {
        return setError("Missing range ID");
    }
    const ID rangeId(std::string(line.substr(0, spacePos)));
    line = line.substr(spacePos + 1);

    // Parse start column ID
    spacePos = line.find(' ');
    if (spacePos == std::string_view::npos) {
        return setError("Missing start column ID");
    }
    const ID startColId(std::string(line.substr(0, spacePos)));
    line = line.substr(spacePos + 1);

    // Parse start row ID
    spacePos = line.find(' ');
    if (spacePos == std::string_view::npos) {
        return setError("Missing start row ID");
    }
    const ID startRowId(std::string(line.substr(0, spacePos)));
    line = line.substr(spacePos + 1);

    // Parse end column ID
    spacePos = line.find(' ');
    if (spacePos == std::string_view::npos) {
        return setError("Missing end column ID");
    }
    const ID endColId(std::string(line.substr(0, spacePos)));
    line = line.substr(spacePos + 1);

    // Parse end row ID
    spacePos = line.find(' ');
    if (spacePos == std::string_view::npos) {
        return setError("Missing end row ID");
    }
    const ID endRowId(std::string(line.substr(0, spacePos)));
    line = line.substr(spacePos + 1);

    // Parse flags (integer)
    spacePos = line.find(' ');
    const std::string_view flagsStr =
        (spacePos == std::string_view::npos) ? line : line.substr(0, spacePos);
    int flagsInt = 0;
    auto [ptr, ec] = std::from_chars(flagsStr.data(), flagsStr.data() + flagsStr.size(), flagsInt);
    if (ec != std::errc()) {
        return setError("Invalid range flags");
    }
    const auto flags = static_cast<RangeFlags>(flagsInt);

    // Create the range
    auto range =
        std::make_unique<Range>(rangeId, startColId, startRowId, endColId, endRowId, flags);
    const Range* rangePtr = range.get();
    currentSheet_->addRange(std::move(range));

    // Parse optional style reference (content-addressed base64)
    if (spacePos != std::string_view::npos) {
        line = line.substr(spacePos + 1);
        // Look for "sty:" property
        if (line.substr(0, 4) == "sty:") {
            const std::string styleValue(line.substr(4));
            auto styleBuf = parseStyleValue(styleValue);
            if (styleBuf.has_value()) {
                currentSheet_->setRangeStyle(rangePtr->id, *styleBuf);
            }
        }
    }

    return true;
}

bool Parser::parseOperation(std::string_view line) {
    // Format: O <hlc> <op-type> <target-id> <payload-json>
    // Example: O 1705312200000.0.N3f8hJ2w CELL_SET_VALUE nP6kR2mW {"type":"n","value":"42"}

    if (line.size() < 2 || line[0] != 'O' || line[1] != ' ') {
        return setError("Invalid operation line");
    }

    line = line.substr(2);  // Skip "O "

    // Parse HLC (format: wall.logical.nodeid)
    size_t spacePos = line.find(' ');
    if (spacePos == std::string_view::npos) {
        return setError("Missing operation type");
    }

    const std::string hlcStr(line.substr(0, spacePos));
    const HLC hlc = HLC::fromString(hlcStr);
    // Note: we don't validate HLC strictly here, zero HLC is acceptable in some edge cases

    line = line.substr(spacePos + 1);

    // Parse operation type
    spacePos = line.find(' ');
    if (spacePos == std::string_view::npos) {
        return setError("Missing target ID");
    }

    const std::string opTypeStr(line.substr(0, spacePos));
    const OpType opType = stringToOpType(opTypeStr);

    line = line.substr(spacePos + 1);

    // Parse target ID (8 characters)
    spacePos = line.find(' ');
    if (spacePos == std::string_view::npos) {
        return setError("Missing payload");
    }

    const std::string targetStr(line.substr(0, spacePos));
    const ID targetId(targetStr);

    // Rest is payload (JSON)
    const std::string payload(line.substr(spacePos + 1));

    // Create operation and add to OpLog
    const Operation op(hlc, opType, targetId, payload);
    OpLog* oplog = workbook_->getOpLog();
    if (oplog != nullptr) {
        oplog->addOperation(op);
    }

    return true;
}

// --- Convenience functions ---

ParseResult parse(const std::string& content) {
    Parser parser;
    return parser.parse(content);
}

ParseResult parse(std::string_view content) {
    Parser parser;
    return parser.parse(content);
}

}  // namespace cells
