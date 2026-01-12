#include "core/cells/xlsx_writer.h"

#include <cstring>

#include <algorithm>
#include <sstream>
#include <unordered_map>

#include "core/cells/formula_serializer.h"
#include "core/cells/named_ranges.h"
#include "core/cells/ref_converter.h"

#include "miniz.h"

namespace {

// Forward declarations
std::string escapeXml(const std::string& str);
std::string colIndexToLetter(size_t index);

// ZIP file writing using miniz
class ZipWriter {
public:
    ZipWriter() = default;

    ~ZipWriter() {
        if (opened_) {
            mz_zip_writer_end(&archive_);
        }
    }

    bool open(const std::string& path) {
        memset(&archive_, 0, sizeof(archive_));
        if (mz_zip_writer_init_file(&archive_, path.c_str(), 0) == 0) {
            return false;
        }
        opened_ = true;
        return true;
    }

    // Add content to archive
    bool addFile(const std::string& name, const std::string& content) {
        return mz_zip_writer_add_mem(&archive_, name.c_str(), content.data(), content.size(),
                                     MZ_DEFAULT_COMPRESSION) != 0;
    }

    // Finalize and close archive
    bool finalize() {
        if (!opened_) {
            return false;
        }
        if (mz_zip_writer_finalize_archive(&archive_) == 0) {
            return false;
        }
        if (mz_zip_writer_end(&archive_) == 0) {
            return false;
        }
        opened_ = false;
        return true;
    }

private:
    mz_zip_archive archive_{};
    bool opened_{false};
};

// Generate [Content_Types].xml
std::string generateContentTypes(size_t sheetCount) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n";
    xml << "  <Default Extension=\"rels\" "
           "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n";
    xml << "  <Default Extension=\"xml\" ContentType=\"application/xml\"/>\n";
    xml << "  <Override PartName=\"/xl/workbook.xml\" "
           "ContentType=\"application/"
           "vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>\n";
    xml << "  <Override PartName=\"/xl/styles.xml\" "
           "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/"
           ">\n";
    xml << "  <Override PartName=\"/xl/sharedStrings.xml\" "
           "ContentType=\"application/"
           "vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>\n";
    for (size_t i = 0; i < sheetCount; ++i) {
        xml << "  <Override PartName=\"/xl/worksheets/sheet" << (i + 1)
            << ".xml\" "
               "ContentType=\"application/"
               "vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>\n";
    }
    xml << "</Types>";
    return xml.str();
}

// Generate _rels/.rels (root relationships)
std::string generateRootRels() {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<Relationships "
           "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
    xml << "  <Relationship Id=\"rId1\" "
           "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
           "officeDocument\" Target=\"xl/workbook.xml\"/>\n";
    xml << "</Relationships>";
    return xml.str();
}

// Convert a cell's position to A1 reference with sheet name
// Output format: 'SheetName'!$A$1 (always absolute for named ranges)
std::string cellIdToXlsxRef(const cells::Cell* cell, const cells::Sheet* sheet) {
    if (cell == nullptr || sheet == nullptr) {
        return "";
    }

    // Find column and row positions
    auto colIt = sheet->columns.find(cell->colId);
    auto rowIt = sheet->rows.find(cell->rowId);
    if (colIt == sheet->columns.end() || rowIt == sheet->rows.end()) {
        return "";
    }

    const uint32_t colPos = colIt->second->position;
    const uint32_t rowPos = rowIt->second->position;

    // Convert to A1 notation (1-indexed row)
    const std::string colLetter = colIndexToLetter(colPos);
    const std::string rowNum = std::to_string(rowPos + 1);

    // Format with sheet name (single quotes if needed)
    std::ostringstream ref;
    // Use single quotes around sheet name if it contains spaces or special chars
    const std::string& sheetName = sheet->name;
    const bool needsQuotes =
        sheetName.find(' ') != std::string::npos || sheetName.find('\'') != std::string::npos ||
        sheetName.find('!') != std::string::npos || sheetName.find('[') != std::string::npos;

    if (needsQuotes) {
        ref << "'";
        // Escape single quotes by doubling them
        for (const char c : sheetName) {
            if (c == '\'') {
                ref << "''";
            } else {
                ref << c;
            }
        }
        ref << "'";
    } else {
        ref << sheetName;
    }
    ref << "!$" << colLetter << "$" << rowNum;
    return ref.str();
}

// Convert a range (two cell IDs) to XLSX reference: 'SheetName'!$A$1:$C$3
std::string rangeToXlsxRef(const cells::Cell* startCell, const cells::Cell* endCell,
                           const cells::Sheet* sheet) {
    if (startCell == nullptr || endCell == nullptr || sheet == nullptr) {
        return "";
    }

    // Find positions for start cell
    auto startColIt = sheet->columns.find(startCell->colId);
    auto startRowIt = sheet->rows.find(startCell->rowId);
    if (startColIt == sheet->columns.end() || startRowIt == sheet->rows.end()) {
        return "";
    }

    // Find positions for end cell
    auto endColIt = sheet->columns.find(endCell->colId);
    auto endRowIt = sheet->rows.find(endCell->rowId);
    if (endColIt == sheet->columns.end() || endRowIt == sheet->rows.end()) {
        return "";
    }

    const uint32_t startColPos = startColIt->second->position;
    const uint32_t startRowPos = startRowIt->second->position;
    const uint32_t endColPos = endColIt->second->position;
    const uint32_t endRowPos = endRowIt->second->position;

    // Format with sheet name
    std::ostringstream ref;
    const std::string& sheetName = sheet->name;
    const bool needsQuotes =
        sheetName.find(' ') != std::string::npos || sheetName.find('\'') != std::string::npos ||
        sheetName.find('!') != std::string::npos || sheetName.find('[') != std::string::npos;

    if (needsQuotes) {
        ref << "'";
        for (const char c : sheetName) {
            if (c == '\'') {
                ref << "''";
            } else {
                ref << c;
            }
        }
        ref << "'";
    } else {
        ref << sheetName;
    }

    ref << "!$" << colIndexToLetter(startColPos) << "$" << (startRowPos + 1) << ":$"
        << colIndexToLetter(endColPos) << "$" << (endRowPos + 1);
    return ref.str();
}

// Convert a column range to XLSX reference: 'SheetName'!$A:$C
std::string columnRangeToXlsxRef(const cells::Axis* startCol, const cells::Axis* endCol,
                                 const cells::Sheet* sheet) {
    if (startCol == nullptr || sheet == nullptr) {
        return "";
    }

    // Format with sheet name
    std::ostringstream ref;
    const std::string& sheetName = sheet->name;
    const bool needsQuotes =
        sheetName.find(' ') != std::string::npos || sheetName.find('\'') != std::string::npos ||
        sheetName.find('!') != std::string::npos || sheetName.find('[') != std::string::npos;

    if (needsQuotes) {
        ref << "'";
        for (const char c : sheetName) {
            if (c == '\'') {
                ref << "''";
            } else {
                ref << c;
            }
        }
        ref << "'";
    } else {
        ref << sheetName;
    }

    ref << "!$" << colIndexToLetter(startCol->position);
    if (endCol != nullptr && endCol->id != startCol->id) {
        ref << ":$" << colIndexToLetter(endCol->position);
    } else {
        // Single column - format as $A:$A
        ref << ":$" << colIndexToLetter(startCol->position);
    }
    return ref.str();
}

// Convert a row range to XLSX reference: 'SheetName'!$1:$3
std::string rowRangeToXlsxRef(const cells::Axis* startRow, const cells::Axis* endRow,
                              const cells::Sheet* sheet) {
    if (startRow == nullptr || sheet == nullptr) {
        return "";
    }

    // Format with sheet name
    std::ostringstream ref;
    const std::string& sheetName = sheet->name;
    const bool needsQuotes =
        sheetName.find(' ') != std::string::npos || sheetName.find('\'') != std::string::npos ||
        sheetName.find('!') != std::string::npos || sheetName.find('[') != std::string::npos;

    if (needsQuotes) {
        ref << "'";
        for (const char c : sheetName) {
            if (c == '\'') {
                ref << "''";
            } else {
                ref << c;
            }
        }
        ref << "'";
    } else {
        ref << sheetName;
    }

    ref << "!$" << (startRow->position + 1);
    if (endRow != nullptr && endRow->id != startRow->id) {
        ref << ":$" << (endRow->position + 1);
    } else {
        // Single row - format as $1:$1
        ref << ":$" << (startRow->position + 1);
    }
    return ref.str();
}

// Generate xl/workbook.xml
std::string generateWorkbook(const cells::Workbook& workbook) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
           "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">\n";
    xml << "  <sheets>\n";
    for (size_t i = 0; i < workbook.sheets.size(); ++i) {
        xml << "    <sheet name=\"" << escapeXml(workbook.sheets[i]->name) << "\" sheetId=\""
            << (i + 1) << "\" r:id=\"rId" << (i + 1) << "\"/>\n";
    }
    xml << "  </sheets>\n";

    // Export named ranges (definedNames)
    const cells::NamedRangeRegistry* registry = workbook.getNamedRanges();
    if (registry != nullptr) {
        const std::vector<const cells::NamedRange*> allRanges = registry->getAll();
        if (!allRanges.empty()) {
            xml << "  <definedNames>\n";

            // Build sheet ID to index map for localSheetId
            std::unordered_map<std::string, size_t> sheetIdToIndex;
            for (size_t i = 0; i < workbook.sheets.size(); ++i) {
                sheetIdToIndex[workbook.sheets[i]->id.toString()] = i;
            }

            for (const cells::NamedRange* namedRange : allRanges) {
                // Convert target to XLSX reference
                std::string xlsxRef;
                const cells::NamedRangeTarget& target = namedRange->target;

                // Find the target sheet
                const cells::Sheet* targetSheet = workbook.getSheet(target.sheetId);
                if (targetSheet == nullptr) {
                    continue;  // Skip if sheet not found
                }

                switch (target.type) {
                    case cells::NamedRangeTarget::Type::CELL: {
                        const cells::Cell* cell = targetSheet->cells.count(target.id1) != 0
                                                      ? targetSheet->cells.at(target.id1).get()
                                                      : nullptr;
                        xlsxRef = cellIdToXlsxRef(cell, targetSheet);
                        break;
                    }
                    case cells::NamedRangeTarget::Type::RANGE: {
                        const cells::Cell* startCell = targetSheet->cells.count(target.id1) != 0
                                                           ? targetSheet->cells.at(target.id1).get()
                                                           : nullptr;
                        const cells::Cell* endCell = targetSheet->cells.count(target.id2) != 0
                                                         ? targetSheet->cells.at(target.id2).get()
                                                         : nullptr;
                        xlsxRef = rangeToXlsxRef(startCell, endCell, targetSheet);
                        break;
                    }
                    case cells::NamedRangeTarget::Type::COLUMN: {
                        const cells::Axis* col = targetSheet->columns.count(target.id1) != 0
                                                     ? targetSheet->columns.at(target.id1).get()
                                                     : nullptr;
                        xlsxRef = columnRangeToXlsxRef(col, col, targetSheet);
                        break;
                    }
                    case cells::NamedRangeTarget::Type::ROW: {
                        const cells::Axis* row = targetSheet->rows.count(target.id1) != 0
                                                     ? targetSheet->rows.at(target.id1).get()
                                                     : nullptr;
                        xlsxRef = rowRangeToXlsxRef(row, row, targetSheet);
                        break;
                    }
                    case cells::NamedRangeTarget::Type::COLUMN_RANGE: {
                        const cells::Axis* startCol =
                            targetSheet->columns.count(target.id1) != 0
                                ? targetSheet->columns.at(target.id1).get()
                                : nullptr;
                        const cells::Axis* endCol = targetSheet->columns.count(target.id2) != 0
                                                        ? targetSheet->columns.at(target.id2).get()
                                                        : nullptr;
                        xlsxRef = columnRangeToXlsxRef(startCol, endCol, targetSheet);
                        break;
                    }
                    case cells::NamedRangeTarget::Type::ROW_RANGE: {
                        const cells::Axis* startRow = targetSheet->rows.count(target.id1) != 0
                                                          ? targetSheet->rows.at(target.id1).get()
                                                          : nullptr;
                        const cells::Axis* endRow = targetSheet->rows.count(target.id2) != 0
                                                        ? targetSheet->rows.at(target.id2).get()
                                                        : nullptr;
                        xlsxRef = rowRangeToXlsxRef(startRow, endRow, targetSheet);
                        break;
                    }
                }

                if (xlsxRef.empty()) {
                    continue;  // Skip if conversion failed
                }

                xml << "    <definedName name=\"" << escapeXml(namedRange->name) << "\"";

                // Add localSheetId for sheet-scoped names
                if (namedRange->scope == cells::NamedRangeScope::SHEET &&
                    !namedRange->scopeSheetId.isNull()) {
                    auto scopeIt = sheetIdToIndex.find(namedRange->scopeSheetId.toString());
                    if (scopeIt != sheetIdToIndex.end()) {
                        xml << " localSheetId=\"" << scopeIt->second << "\"";
                    }
                }

                xml << ">" << escapeXml(xlsxRef) << "</definedName>\n";
            }

            xml << "  </definedNames>\n";
        }
    }

    xml << "</workbook>";
    return xml.str();
}

// Generate xl/_rels/workbook.xml.rels
std::string generateWorkbookRels(size_t sheetCount) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<Relationships "
           "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
    // Sheet relationships
    for (size_t i = 0; i < sheetCount; ++i) {
        xml << "  <Relationship Id=\"rId" << (i + 1)
            << "\" "
               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
               "worksheet\" "
               "Target=\"worksheets/sheet"
            << (i + 1) << ".xml\"/>\n";
    }
    // Styles relationship
    xml << "  <Relationship Id=\"rId" << (sheetCount + 1)
        << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
           "Target=\"styles.xml\"/>\n";
    // Shared strings relationship
    xml << "  <Relationship Id=\"rId" << (sheetCount + 2)
        << "\" "
           "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
           "sharedStrings\" "
           "Target=\"sharedStrings.xml\"/>\n";
    xml << "</Relationships>";
    return xml.str();
}

// Shared string table for deduplication
class SharedStringTable {
public:
    // Get or add a string, returns index
    size_t getOrAdd(const std::string& str) {
        auto it = index_.find(str);
        if (it != index_.end()) {
            return it->second;
        }
        const size_t idx = strings_.size();
        strings_.push_back(str);
        index_[str] = idx;
        return idx;
    }

    [[nodiscard]] const std::vector<std::string>& strings() const { return strings_; }
    [[nodiscard]] size_t size() const { return strings_.size(); }

private:
    std::vector<std::string> strings_;
    std::unordered_map<std::string, size_t> index_;
};

// ---------------------------------------------------------------------------
// Style Table for XLSX export
// ---------------------------------------------------------------------------

// Font entry for styles.xml
struct XLSXFontEntry {
    bool bold{false};
    bool italic{false};
    bool underline{false};
    std::string name{"Calibri"};
    double size{11};
    std::string color;  // Empty = default, otherwise ARGB

    bool operator==(const XLSXFontEntry& other) const {
        return bold == other.bold && italic == other.italic && underline == other.underline &&
               name == other.name && size == other.size && color == other.color;
    }
};

// Fill entry for styles.xml
struct XLSXFillEntry {
    std::string fgColor;  // ARGB hex, empty = none

    bool operator==(const XLSXFillEntry& other) const { return fgColor == other.fgColor; }
};

// Cell format entry (cellXfs)
struct XLSXCellFormatEntry {
    size_t fontId{0};
    size_t fillId{0};
    cells::TextAlign hAlign{cells::TextAlign::LEFT};
    cells::VerticalAlign vAlign{cells::VerticalAlign::BOTTOM};
    bool hasAlignment{false};

    bool operator==(const XLSXCellFormatEntry& other) const {
        return fontId == other.fontId && fillId == other.fillId && hAlign == other.hAlign &&
               vAlign == other.vAlign && hasAlignment == other.hasAlignment;
    }
};

// Convert #RRGGBB to FFRRGGBB (ARGB)
std::string rgbToArgb(const std::string& rgb) {
    if (rgb.empty()) {
        return {};
    }
    if (rgb.size() == 7 && rgb[0] == '#') {
        return "FF" + rgb.substr(1);
    }
    return rgb;
}

// Style table that collects fonts, fills, and cell formats for XLSX export
class StyleTable {
public:
    StyleTable() {
        // Add default font (index 0) - required by Excel
        fonts_.push_back(XLSXFontEntry{});
        fontIndex_[fontKey(fonts_[0])] = 0;

        // Add required fills (indices 0 and 1) - required by Excel
        fills_.emplace_back();                          // none (index 0)
        fills_.emplace_back(XLSXFillEntry{"gray125"});  // gray125 (required placeholder)
        fillIndex_[""] = 0;
        fillIndex_["gray125"] = 1;

        // Add default cell format (index 0)
        formats_.push_back(XLSXCellFormatEntry{});
        formatIndex_[formatKey(formats_[0])] = 0;
    }

    // Get or add a cell format for a given CellStyle
    // Returns the cellXfs index for this style
    size_t getOrAddFormat(const cells::CellStyle& style) {
        // First, get or add font
        XLSXFontEntry font;
        font.bold = style.bold;
        font.italic = style.italic;
        font.underline = style.underline;
        font.name = style.fontFamily.empty() ? "Calibri" : style.fontFamily;
        font.size = style.fontSize > 0 ? style.fontSize : 11;
        font.color = rgbToArgb(style.textColor);
        const size_t fontId = getOrAddFont(font);

        // Get or add fill (background)
        XLSXFillEntry fill;
        fill.fgColor = rgbToArgb(style.bgColor);
        const size_t fillId = getOrAddFill(fill);

        // Create cell format
        XLSXCellFormatEntry xf;
        xf.fontId = fontId;
        xf.fillId = fillId;
        xf.hAlign = style.hAlign;
        xf.vAlign = style.vAlign;
        xf.hasAlignment = (style.hAlign != cells::TextAlign::LEFT ||
                           style.vAlign != cells::VerticalAlign::BOTTOM);

        return getOrAddCellFormat(xf);
    }

    [[nodiscard]] const std::vector<XLSXFontEntry>& fonts() const { return fonts_; }
    [[nodiscard]] const std::vector<XLSXFillEntry>& fills() const { return fills_; }
    [[nodiscard]] const std::vector<XLSXCellFormatEntry>& formats() const { return formats_; }

private:
    std::vector<XLSXFontEntry> fonts_;
    std::vector<XLSXFillEntry> fills_;
    std::vector<XLSXCellFormatEntry> formats_;
    std::unordered_map<std::string, size_t> fontIndex_;
    std::unordered_map<std::string, size_t> fillIndex_;
    std::unordered_map<std::string, size_t> formatIndex_;

    static std::string fontKey(const XLSXFontEntry& f) {
        std::ostringstream oss;
        oss << (f.bold ? "B" : "b") << (f.italic ? "I" : "i") << (f.underline ? "U" : "u") << "|"
            << f.name << "|" << f.size << "|" << f.color;
        return oss.str();
    }

    static std::string formatKey(const XLSXCellFormatEntry& xf) {
        std::ostringstream oss;
        oss << xf.fontId << "|" << xf.fillId << "|" << static_cast<int>(xf.hAlign) << "|"
            << static_cast<int>(xf.vAlign) << "|" << xf.hasAlignment;
        return oss.str();
    }

    size_t getOrAddFont(const XLSXFontEntry& font) {
        const std::string key = fontKey(font);
        auto it = fontIndex_.find(key);
        if (it != fontIndex_.end()) {
            return it->second;
        }
        const size_t idx = fonts_.size();
        fonts_.push_back(font);
        fontIndex_[key] = idx;
        return idx;
    }

    size_t getOrAddFill(const XLSXFillEntry& fill) {
        // Empty fill uses index 0
        if (fill.fgColor.empty()) {
            return 0;
        }
        auto it = fillIndex_.find(fill.fgColor);
        if (it != fillIndex_.end()) {
            return it->second;
        }
        const size_t idx = fills_.size();
        fills_.push_back(fill);
        fillIndex_[fill.fgColor] = idx;
        return idx;
    }

    size_t getOrAddCellFormat(const XLSXCellFormatEntry& xf) {
        const std::string key = formatKey(xf);
        auto it = formatIndex_.find(key);
        if (it != formatIndex_.end()) {
            return it->second;
        }
        const size_t idx = formats_.size();
        formats_.push_back(xf);
        formatIndex_[key] = idx;
        return idx;
    }
};

// Escape XML special characters
std::string escapeXml(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (const char c : str) {
        switch (c) {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            case '\'':
                result += "&apos;";
                break;
            default:
                result += c;
                break;
        }
    }
    return result;
}

// Convert column index (0-based) to Excel letter (A, B, ..., Z, AA, ...)
std::string colIndexToLetter(size_t index) {
    std::string result;
    size_t n = index + 1;  // Convert to 1-based
    while (n > 0) {
        n--;
        result.insert(result.begin(), static_cast<char>('A' + (n % 26)));
        n = n / 26;
    }
    return result;
}

// Helper to get cell position in grid
struct CellPosition {
    size_t colIdx;
    size_t rowIdx;
};

// Generate worksheet XML
// cellStyleIndices maps cell pointer to XLSX style index (s attribute)
std::string generateWorksheet(
    const cells::Sheet& sheet, SharedStringTable& sst, const cells::RefConverter& refConverter,
    bool writeFormulas, const std::unordered_map<const cells::Cell*, size_t>& cellStyleIndices) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n";

    // Get ordered columns and rows
    std::vector<std::pair<uint32_t, cells::ID>> columns;
    columns.reserve(sheet.columns.size());
    for (const auto& pair : sheet.columns) {
        columns.emplace_back(pair.second->position, pair.first);
    }
    std::sort(columns.begin(), columns.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<std::pair<uint32_t, cells::ID>> rows;
    rows.reserve(sheet.rows.size());
    for (const auto& pair : sheet.rows) {
        rows.emplace_back(pair.second->position, pair.first);
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Build column/row ID to index maps
    std::unordered_map<std::string, size_t> colIdToIndex;
    std::unordered_map<std::string, size_t> rowIdToIndex;
    for (size_t i = 0; i < columns.size(); ++i) {
        colIdToIndex[columns[i].second.toString()] = i;
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        rowIdToIndex[rows[i].second.toString()] = i;
    }

    // Build cell lookup: (colIdx, rowIdx) -> Cell*
    std::unordered_map<uint64_t, const cells::Cell*> cellGrid;
    // Also track cell positions
    std::unordered_map<const cells::Cell*, CellPosition> cellPositions;
    for (const auto& pair : sheet.cells) {
        const cells::Cell* cell = pair.second.get();
        auto colIt = colIdToIndex.find(cell->colId.toString());
        auto rowIt = rowIdToIndex.find(cell->rowId.toString());
        if (colIt != colIdToIndex.end() && rowIt != rowIdToIndex.end()) {
            const uint64_t key = (static_cast<uint64_t>(rowIt->second) << 32) | colIt->second;
            cellGrid[key] = cell;
            cellPositions[cell] = {colIt->second, rowIt->second};
        }
    }

    // Build shared formula index map: master cell -> si (shared index)
    std::unordered_map<const cells::Cell*, int> masterToSi;
    // Also map subscriber cells to their master's si
    std::unordered_map<const cells::Cell*, int> subscriberToSi;
    int nextSi = 0;

    if (writeFormulas) {
        for (const auto& pair : sheet.cells) {
            const cells::Cell* cell = pair.second.get();
            // Check if this cell is a shared formula master
            if (cell->isSharedFormulaMaster()) {
                masterToSi[cell] = nextSi++;
            }
        }
        // Build master cell ID -> cell pointer map for reverse lookup
        std::unordered_map<std::string, const cells::Cell*> cellIdToCell;
        for (const auto& pair : sheet.cells) {
            const cells::Cell* cell = pair.second.get();
            cellIdToCell[cell->id.toString()] = cell;
        }

        // Map subscribers to their master's si using Sheet-level tracking
        for (const auto& pair : sheet.cells) {
            const cells::Cell* cell = pair.second.get();
            if (cell->isSharedFormula()) {
                const cells::ID masterId = sheet.getSharedFormulaMaster(cell->id);
                if (!masterId.isNull()) {
                    auto masterCellIt = cellIdToCell.find(masterId.toString());
                    if (masterCellIt != cellIdToCell.end()) {
                        auto masterSiIt = masterToSi.find(masterCellIt->second);
                        if (masterSiIt != masterToSi.end()) {
                            subscriberToSi[cell] = masterSiIt->second;
                        }
                    }
                }
            }
        }
    }

    // Write dimension
    if (!columns.empty() && !rows.empty()) {
        xml << "  <dimension ref=\"A1:" << colIndexToLetter(columns.size() - 1) << rows.size()
            << "\"/>\n";
    }

    // Write sheetViews (including showGridLines and zoomScale)
    xml << "  <sheetViews>\n";
    xml << "    <sheetView workbookViewId=\"0\"";
    if (!sheet.showGridLines) {
        xml << " showGridLines=\"0\"";
    }
    if (sheet.zoomScale != 100) {
        xml << " zoomScale=\"" << sheet.zoomScale << "\"";
    }
    xml << "/>\n";
    xml << "  </sheetViews>\n";

    xml << "  <sheetData>\n";

    // Write rows
    for (size_t rowIdx = 0; rowIdx < rows.size(); ++rowIdx) {
        bool hasAnyCells = false;

        // Check if this row has any cells
        for (size_t colIdx = 0; colIdx < columns.size(); ++colIdx) {
            const uint64_t key = (static_cast<uint64_t>(rowIdx) << 32) | colIdx;
            if (cellGrid.count(key) > 0) {
                hasAnyCells = true;
                break;
            }
        }

        if (!hasAnyCells) {
            continue;
        }

        xml << "    <row r=\"" << (rowIdx + 1) << "\">\n";

        for (size_t colIdx = 0; colIdx < columns.size(); ++colIdx) {
            const uint64_t key = (static_cast<uint64_t>(rowIdx) << 32) | colIdx;
            auto it = cellGrid.find(key);
            if (it == cellGrid.end()) {
                continue;
            }

            const cells::Cell* cell = it->second;
            const std::string cellRef = colIndexToLetter(colIdx) + std::to_string(rowIdx + 1);

            // Determine cell type and value
            const cells::CellValue& value = cell->value;
            const cells::Formula* formula = cell->getFormula();

            xml << "      <c r=\"" << cellRef << "\"";

            // Add style index if cell has a style
            auto styleIt = cellStyleIndices.find(cell);
            if (styleIt != cellStyleIndices.end() && styleIt->second > 0) {
                xml << " s=\"" << styleIt->second << "\"";
            }

            // Handle formula cells
            if (writeFormulas && formula != nullptr && formula->ast != nullptr) {
                xml << ">\n";

                // Generate UUID-format formula text from AST, then convert to A1
                const std::string uuidFormula = cells::FormulaSerializer::serialize(formula->ast);
                // Skip the leading '=' for refConverter
                const std::string uuidBody = uuidFormula.empty() ? "" : uuidFormula.substr(1);
                const std::string a1Formula = refConverter.formulaToA1(uuidBody);

                // Check if this is a shared formula
                auto masterIt = masterToSi.find(cell);
                auto subscriberIt = subscriberToSi.find(cell);

                if (masterIt != masterToSi.end()) {
                    // This is a shared formula master - calculate the range
                    // For now, just use the master cell as the ref (Excel accepts this)
                    const int si = masterIt->second;
                    xml << "        <f t=\"shared\" ref=\"" << cellRef << "\" si=\"" << si << "\">"
                        << escapeXml(a1Formula) << "</f>\n";
                } else if (subscriberIt != subscriberToSi.end()) {
                    // This is a shared formula subscriber
                    const int si = subscriberIt->second;
                    xml << "        <f t=\"shared\" si=\"" << si << "\"/>\n";
                } else {
                    // Regular formula (not shared)
                    xml << "        <f>" << escapeXml(a1Formula) << "</f>\n";
                }

                // Write cached value if available
                // Check for both regular types and FORMULA_* result types
                if (value.type == cells::CellValueType::NUMBER ||
                    value.type == cells::CellValueType::FORMULA_NUMBER) {
                    xml << "        <v>" << value.raw << "</v>\n";
                } else if (value.type == cells::CellValueType::BOOLEAN ||
                           value.type == cells::CellValueType::FORMULA_BOOLEAN) {
                    xml << "        <v>" << (value.raw == "true" || value.raw == "1" ? "1" : "0")
                        << "</v>\n";
                } else if (!value.raw.empty()) {
                    // String, error, and other types - escape XML special characters
                    xml << "        <v>" << escapeXml(value.raw) << "</v>\n";
                }
                xml << "      </c>\n";
            } else {
                // Value cell (or formula cell when writeFormulas is false)
                switch (value.type) {
                    case cells::CellValueType::NUMBER:
                    case cells::CellValueType::FORMULA_NUMBER:
                        xml << ">\n";
                        xml << "        <v>" << value.raw << "</v>\n";
                        xml << "      </c>\n";
                        break;

                    case cells::CellValueType::STRING:
                    case cells::CellValueType::FORMULA_STRING:
                        if (!value.raw.empty()) {
                            const size_t sstIndex = sst.getOrAdd(value.raw);
                            xml << " t=\"s\">\n";
                            xml << "        <v>" << sstIndex << "</v>\n";
                            xml << "      </c>\n";
                        } else {
                            xml << "/>\n";
                        }
                        break;

                    case cells::CellValueType::BOOLEAN:
                    case cells::CellValueType::FORMULA_BOOLEAN:
                        xml << " t=\"b\">\n";
                        xml << "        <v>"
                            << (value.raw == "true" || value.raw == "1" ? "1" : "0") << "</v>\n";
                        xml << "      </c>\n";
                        break;

                    case cells::CellValueType::ERROR:
                    case cells::CellValueType::FORMULA_ERROR:
                        xml << " t=\"e\">\n";
                        xml << "        <v>" << escapeXml(value.raw) << "</v>\n";
                        xml << "      </c>\n";
                        break;

                    default:
                        xml << "/>\n";
                        break;
                }
            }
        }

        xml << "    </row>\n";
    }

    xml << "  </sheetData>\n";
    xml << "</worksheet>";

    return xml.str();
}

// Generate xl/sharedStrings.xml
std::string generateSharedStrings(const SharedStringTable& sst) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\""
        << sst.size() << "\" uniqueCount=\"" << sst.size() << "\">\n";

    for (const auto& str : sst.strings()) {
        xml << "  <si><t>" << escapeXml(str) << "</t></si>\n";
    }

    xml << "</sst>";
    return xml.str();
}

// Horizontal alignment enum to XLSX string
const char* hAlignToXlsx(cells::TextAlign align) {
    switch (align) {
        case cells::TextAlign::CENTER:
            return "center";
        case cells::TextAlign::RIGHT:
            return "right";
        case cells::TextAlign::JUSTIFY:
            return "justify";
        default:
            return "left";
    }
}

// Vertical alignment enum to XLSX string
const char* vAlignToXlsx(cells::VerticalAlign align) {
    switch (align) {
        case cells::VerticalAlign::TOP:
            return "top";
        case cells::VerticalAlign::MIDDLE:
            return "center";
        default:
            return "bottom";
    }
}

// Generate xl/styles.xml from collected styles
std::string generateStyles(const StyleTable& styles) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n";

    // Fonts
    const auto& fonts = styles.fonts();
    xml << "  <fonts count=\"" << fonts.size() << "\">\n";
    for (const auto& font : fonts) {
        xml << "    <font>\n";
        if (font.bold) {
            xml << "      <b/>\n";
        }
        if (font.italic) {
            xml << "      <i/>\n";
        }
        if (font.underline) {
            xml << "      <u/>\n";
        }
        xml << "      <sz val=\"" << font.size << "\"/>\n";
        if (!font.color.empty()) {
            xml << "      <color rgb=\"" << font.color << "\"/>\n";
        }
        xml << "      <name val=\"" << escapeXml(font.name) << "\"/>\n";
        xml << "    </font>\n";
    }
    xml << "  </fonts>\n";

    // Fills
    const auto& fills = styles.fills();
    xml << "  <fills count=\"" << fills.size() << "\">\n";
    for (size_t i = 0; i < fills.size(); ++i) {
        const auto& fill = fills[i];
        if (i == 1 && fill.fgColor == "gray125") {
            // Second fill is required gray125 placeholder
            xml << "    <fill><patternFill patternType=\"gray125\"/></fill>\n";
        } else if (!fill.fgColor.empty() && fill.fgColor != "gray125") {
            // Solid fill with color
            xml << "    <fill><patternFill patternType=\"solid\"><fgColor rgb=\"" << fill.fgColor
                << "\"/></patternFill></fill>\n";
        } else {
            // Empty fill or index 0 - use none pattern
            xml << "    <fill><patternFill patternType=\"none\"/></fill>\n";
        }
    }
    xml << "  </fills>\n";

    // Borders (just default for now)
    xml << "  <borders count=\"1\">\n";
    xml << "    <border><left/><right/><top/><bottom/><diagonal/></border>\n";
    xml << "  </borders>\n";

    // Cell style formats (just default)
    xml << "  <cellStyleXfs count=\"1\">\n";
    xml << "    <xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/>\n";
    xml << "  </cellStyleXfs>\n";

    // Cell formats (cellXfs)
    const auto& formats = styles.formats();
    xml << "  <cellXfs count=\"" << formats.size() << "\">\n";
    for (const auto& xf : formats) {
        xml << "    <xf numFmtId=\"0\" fontId=\"" << xf.fontId << "\" fillId=\"" << xf.fillId
            << "\" borderId=\"0\" xfId=\"0\"";

        // Apply flags
        if (xf.fontId > 0) {
            xml << " applyFont=\"1\"";
        }
        if (xf.fillId > 0) {
            xml << " applyFill=\"1\"";
        }
        if (xf.hasAlignment) {
            xml << " applyAlignment=\"1\"";
        }

        if (xf.hasAlignment) {
            xml << ">\n";
            xml << "      <alignment";
            if (xf.hAlign != cells::TextAlign::LEFT) {
                xml << " horizontal=\"" << hAlignToXlsx(xf.hAlign) << "\"";
            }
            if (xf.vAlign != cells::VerticalAlign::BOTTOM) {
                xml << " vertical=\"" << vAlignToXlsx(xf.vAlign) << "\"";
            }
            xml << "/>\n";
            xml << "    </xf>\n";
        } else {
            xml << "/>\n";
        }
    }
    xml << "  </cellXfs>\n";

    xml << "</styleSheet>";
    return xml.str();
}

}  // namespace

namespace cells {

// ============================================================================
// XLSXWriteError
// ============================================================================

std::string XLSXWriteError::toString() const {
    std::ostringstream oss;
    if (!sheet.empty()) {
        oss << "Sheet \"" << sheet << "\": ";
    }
    oss << message;
    return oss.str();
}

// ============================================================================
// XLSXWriter
// ============================================================================

XLSXWriter::XLSXWriter() = default;

XLSXWriter::XLSXWriter(XLSXWriteOptions options) : options_(std::move(options)) {}

void XLSXWriter::reset() {
    warnings_.clear();
}

void XLSXWriter::addWarning(const std::string& msg) {
    warnings_.push_back(msg);
}

std::vector<ID> XLSXWriter::getOrderedColumns(const Sheet& sheet) const {
    std::vector<std::pair<uint32_t, ID>> columns;
    columns.reserve(sheet.columns.size());

    for (const auto& pair : sheet.columns) {
        columns.emplace_back(pair.second->position, pair.first);
    }

    std::sort(columns.begin(), columns.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<ID> result;
    result.reserve(columns.size());
    for (const auto& col : columns) {
        result.push_back(col.second);
    }
    return result;
}

std::vector<ID> XLSXWriter::getOrderedRows(const Sheet& sheet) const {
    std::vector<std::pair<uint32_t, ID>> rows;
    rows.reserve(sheet.rows.size());

    for (const auto& pair : sheet.rows) {
        rows.emplace_back(pair.second->position, pair.first);
    }

    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<ID> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        result.push_back(row.second);
    }
    return result;
}

std::string XLSXWriter::columnIndexToLetter(size_t index) {
    std::string result;
    size_t n = index;
    do {
        result.insert(result.begin(), static_cast<char>('A' + (n % 26)));
        n = n / 26;
        if (n > 0) {
            n--;  // Adjust for 1-based "digits"
        }
    } while (n > 0);
    return result;
}

std::string XLSXWriter::convertFormula(const std::string& formula, const Sheet& /*sheet*/,
                                       const std::vector<ID>& /*columnIds*/,
                                       const std::vector<ID>& /*rowIds*/) const {
    // Stub - just return the formula as-is
    return formula;
}

XLSXWriteResult XLSXWriter::writeFile(const Workbook& workbook, const std::string& path) {
    reset();
    XLSXWriteResult result;

    if (workbook.sheets.empty()) {
        result.error = XLSXWriteError("Workbook has no sheets");
        return result;
    }

    // Open ZIP archive
    ZipWriter zip;
    if (!zip.open(path)) {
        result.error = XLSXWriteError("Failed to create XLSX file: " + path);
        return result;
    }

    // Write root files
    if (!zip.addFile("[Content_Types].xml", generateContentTypes(workbook.sheets.size()))) {
        result.error = XLSXWriteError("Failed to write [Content_Types].xml");
        return result;
    }

    if (!zip.addFile("_rels/.rels", generateRootRels())) {
        result.error = XLSXWriteError("Failed to write _rels/.rels");
        return result;
    }

    // Write workbook files
    if (!zip.addFile("xl/workbook.xml", generateWorkbook(workbook))) {
        result.error = XLSXWriteError("Failed to write xl/workbook.xml");
        return result;
    }

    if (!zip.addFile("xl/_rels/workbook.xml.rels", generateWorkbookRels(workbook.sheets.size()))) {
        result.error = XLSXWriteError("Failed to write xl/_rels/workbook.xml.rels");
        return result;
    }

    // Collect styles from all sheets
    StyleTable styleTable;
    std::unordered_map<const Cell*, size_t> cellStyleIndices;

    for (const auto& sheet : workbook.sheets) {
        for (const auto& [id, cell] : sheet->cells) {
            if (!cell->styleId.isNull()) {
                // Look up the CellStyle in the workbook
                const CellStyle* style = workbook.getStyle(cell->styleId);
                if (style != nullptr) {
                    const size_t styleIdx = styleTable.getOrAddFormat(*style);
                    cellStyleIndices[cell.get()] = styleIdx;
                }
            }
        }
    }

    // Write styles
    if (!zip.addFile("xl/styles.xml", generateStyles(styleTable))) {
        result.error = XLSXWriteError("Failed to write xl/styles.xml");
        return result;
    }

    // Shared string table (populated during worksheet generation)
    SharedStringTable sst;

    // Write worksheets
    size_t totalCells = 0;
    for (size_t i = 0; i < workbook.sheets.size(); ++i) {
        const Sheet& sheet = *workbook.sheets[i];

        // Set up ref converter for this sheet
        RefConverter refConverter;
        refConverter.setContext(sheet);

        // Generate worksheet XML
        const std::string sheetXml =
            generateWorksheet(sheet, sst, refConverter, options_.writeFormulas, cellStyleIndices);

        const std::string sheetPath = "xl/worksheets/sheet" + std::to_string(i + 1) + ".xml";
        if (!zip.addFile(sheetPath, sheetXml)) {
            result.error = XLSXWriteError("Failed to write " + sheetPath, sheet.name);
            return result;
        }

        totalCells += sheet.cells.size();
    }

    // Write shared strings (after all worksheets so all strings are collected)
    if (!zip.addFile("xl/sharedStrings.xml", generateSharedStrings(sst))) {
        result.error = XLSXWriteError("Failed to write xl/sharedStrings.xml");
        return result;
    }

    // Finalize archive
    if (!zip.finalize()) {
        result.error = XLSXWriteError("Failed to finalize XLSX archive");
        return result;
    }

    result.cellsWritten = totalCells;
    result.warnings = std::move(warnings_);
    return result;
}

// ============================================================================
// Convenience functions
// ============================================================================

XLSXWriteResult writeXLSX(const Workbook& workbook, const std::string& path) {
    XLSXWriter writer;
    return writer.writeFile(workbook, path);
}

XLSXWriteResult writeXLSX(const Workbook& workbook, const std::string& path,
                          const XLSXWriteOptions& options) {
    XLSXWriter writer(options);
    return writer.writeFile(workbook, path);
}

}  // namespace cells
