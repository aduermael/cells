#include "core/cells/xlsx_writer.h"

#include <cstring>

#include <algorithm>
#include <sstream>
#include <unordered_map>

#include "core/cells/formula_serializer.h"
#include "core/cells/named_ranges.h"
#include "core/cells/range.h"
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
    // Core document properties
    xml << "  <Override PartName=\"/docProps/core.xml\" "
           "ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>\n";
    xml << "  <Override PartName=\"/docProps/app.xml\" "
           "ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/"
           ">\n";
    // Workbook parts
    xml << "  <Override PartName=\"/xl/workbook.xml\" "
           "ContentType=\"application/"
           "vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>\n";
    xml << "  <Override PartName=\"/xl/theme/theme1.xml\" "
           "ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>\n";
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
    xml << "  <Relationship Id=\"rId2\" "
           "Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/"
           "core-properties\" Target=\"docProps/core.xml\"/>\n";
    xml << "  <Relationship Id=\"rId3\" "
           "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
           "extended-properties\" Target=\"docProps/app.xml\"/>\n";
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
    // Theme relationship
    xml << "  <Relationship Id=\"rId" << (sheetCount + 3)
        << "\" "
           "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
           "theme\" "
           "Target=\"theme/theme1.xml\"/>\n";
    xml << "</Relationships>";
    return xml.str();
}

// Generate docProps/core.xml (core document properties)
std::string generateCoreProps() {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<cp:coreProperties "
           "xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
           "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
           "xmlns:dcterms=\"http://purl.org/dc/terms/\" "
           "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n";
    xml << "  <dc:creator>Cells</dc:creator>\n";
    xml << "  <cp:lastModifiedBy>Cells</cp:lastModifiedBy>\n";
    xml << "</cp:coreProperties>";
    return xml.str();
}

// Generate docProps/app.xml (application properties)
std::string generateAppProps() {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<Properties "
           "xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">\n";
    xml << "  <Application>Cells</Application>\n";
    xml << "</Properties>";
    return xml.str();
}

// Generate xl/theme/theme1.xml (minimal Office theme required by Excel)
std::string generateTheme() {
    // This is a minimal theme that Excel requires to open files without errors
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
           "name=\"Office Theme\">\n";
    xml << "  <a:themeElements>\n";
    // Color scheme - required
    xml << "    <a:clrScheme name=\"Office\">\n";
    xml << "      <a:dk1><a:sysClr val=\"windowText\" lastClr=\"000000\"/></a:dk1>\n";
    xml << "      <a:lt1><a:sysClr val=\"window\" lastClr=\"FFFFFF\"/></a:lt1>\n";
    xml << "      <a:dk2><a:srgbClr val=\"44546A\"/></a:dk2>\n";
    xml << "      <a:lt2><a:srgbClr val=\"E7E6E6\"/></a:lt2>\n";
    xml << "      <a:accent1><a:srgbClr val=\"4472C4\"/></a:accent1>\n";
    xml << "      <a:accent2><a:srgbClr val=\"ED7D31\"/></a:accent2>\n";
    xml << "      <a:accent3><a:srgbClr val=\"A5A5A5\"/></a:accent3>\n";
    xml << "      <a:accent4><a:srgbClr val=\"FFC000\"/></a:accent4>\n";
    xml << "      <a:accent5><a:srgbClr val=\"5B9BD5\"/></a:accent5>\n";
    xml << "      <a:accent6><a:srgbClr val=\"70AD47\"/></a:accent6>\n";
    xml << "      <a:hlink><a:srgbClr val=\"0563C1\"/></a:hlink>\n";
    xml << "      <a:folHlink><a:srgbClr val=\"954F72\"/></a:folHlink>\n";
    xml << "    </a:clrScheme>\n";
    // Font scheme - required
    xml << "    <a:fontScheme name=\"Office\">\n";
    xml << "      <a:majorFont>\n";
    xml << "        <a:latin typeface=\"Calibri Light\"/>\n";
    xml << "        <a:ea typeface=\"\"/>\n";
    xml << "        <a:cs typeface=\"\"/>\n";
    xml << "      </a:majorFont>\n";
    xml << "      <a:minorFont>\n";
    xml << "        <a:latin typeface=\"Calibri\"/>\n";
    xml << "        <a:ea typeface=\"\"/>\n";
    xml << "        <a:cs typeface=\"\"/>\n";
    xml << "      </a:minorFont>\n";
    xml << "    </a:fontScheme>\n";
    // Format scheme - required
    xml << "    <a:fmtScheme name=\"Office\">\n";
    xml << "      <a:fillStyleLst>\n";
    xml << "        <a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>\n";
    xml << "        <a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>\n";
    xml << "        <a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>\n";
    xml << "      </a:fillStyleLst>\n";
    xml << "      <a:lnStyleLst>\n";
    xml << "        <a:ln w=\"6350\"><a:solidFill><a:schemeClr "
           "val=\"phClr\"/></a:solidFill></a:ln>\n";
    xml << "        <a:ln w=\"12700\"><a:solidFill><a:schemeClr "
           "val=\"phClr\"/></a:solidFill></a:ln>\n";
    xml << "        <a:ln w=\"19050\"><a:solidFill><a:schemeClr "
           "val=\"phClr\"/></a:solidFill></a:ln>\n";
    xml << "      </a:lnStyleLst>\n";
    xml << "      <a:effectStyleLst>\n";
    xml << "        <a:effectStyle><a:effectLst/></a:effectStyle>\n";
    xml << "        <a:effectStyle><a:effectLst/></a:effectStyle>\n";
    xml << "        <a:effectStyle><a:effectLst/></a:effectStyle>\n";
    xml << "      </a:effectStyleLst>\n";
    xml << "      <a:bgFillStyleLst>\n";
    xml << "        <a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>\n";
    xml << "        <a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>\n";
    xml << "        <a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>\n";
    xml << "      </a:bgFillStyleLst>\n";
    xml << "    </a:fmtScheme>\n";
    xml << "  </a:themeElements>\n";
    xml << "  <a:objectDefaults/>\n";
    xml << "  <a:extraClrSchemeLst/>\n";
    xml << "</a:theme>";
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

// Single border edge entry for styles.xml
struct XLSXBorderEdgeEntry {
    cells::BorderStyle style{cells::BorderStyle::NONE};
    std::string color;  // ARGB hex, empty = use default (auto)

    bool operator==(const XLSXBorderEdgeEntry& other) const {
        return style == other.style && color == other.color;
    }

    [[nodiscard]] bool hasValue() const { return style != cells::BorderStyle::NONE; }
};

// Border entry for styles.xml
struct XLSXBorderEntry {
    XLSXBorderEdgeEntry left;
    XLSXBorderEdgeEntry right;
    XLSXBorderEdgeEntry top;
    XLSXBorderEdgeEntry bottom;

    bool operator==(const XLSXBorderEntry& other) const {
        return left == other.left && right == other.right && top == other.top &&
               bottom == other.bottom;
    }

    [[nodiscard]] bool hasValue() const {
        return left.hasValue() || right.hasValue() || top.hasValue() || bottom.hasValue();
    }
};

// Cell format entry (cellXfs)
struct XLSXCellFormatEntry {
    size_t fontId{0};
    size_t fillId{0};
    size_t borderId{0};
    cells::TextAlign hAlign{cells::TextAlign::GENERAL};
    cells::VerticalAlign vAlign{cells::VerticalAlign::BOTTOM};
    bool wrapText{false};
    bool hasAlignment{false};

    bool operator==(const XLSXCellFormatEntry& other) const {
        return fontId == other.fontId && fillId == other.fillId && borderId == other.borderId &&
               hAlign == other.hAlign && vAlign == other.vAlign && wrapText == other.wrapText &&
               hasAlignment == other.hasAlignment;
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

// Style table that collects fonts, fills, borders, and cell formats for XLSX export
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

        // Add default border (index 0) - required by Excel
        borders_.emplace_back();  // Empty border (no edges)
        borderIndex_[borderKey(borders_[0])] = 0;

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

        // Get or add border
        XLSXBorderEntry border;
        border.left.style = style.border.left.style;
        border.left.color = rgbToArgb(style.border.left.color);
        border.right.style = style.border.right.style;
        border.right.color = rgbToArgb(style.border.right.color);
        border.top.style = style.border.top.style;
        border.top.color = rgbToArgb(style.border.top.color);
        border.bottom.style = style.border.bottom.style;
        border.bottom.color = rgbToArgb(style.border.bottom.color);
        const size_t borderId = getOrAddBorder(border);

        // Create cell format
        XLSXCellFormatEntry xf;
        xf.fontId = fontId;
        xf.fillId = fillId;
        xf.borderId = borderId;
        xf.hAlign = style.hAlign;
        xf.vAlign = style.vAlign;
        xf.wrapText = style.wrapText;
        xf.hasAlignment = (style.hAlign != cells::TextAlign::GENERAL ||
                           style.vAlign != cells::VerticalAlign::BOTTOM || style.wrapText);

        return getOrAddCellFormat(xf);
    }

    [[nodiscard]] const std::vector<XLSXFontEntry>& fonts() const { return fonts_; }
    [[nodiscard]] const std::vector<XLSXFillEntry>& fills() const { return fills_; }
    [[nodiscard]] const std::vector<XLSXBorderEntry>& borders() const { return borders_; }
    [[nodiscard]] const std::vector<XLSXCellFormatEntry>& formats() const { return formats_; }

private:
    std::vector<XLSXFontEntry> fonts_;
    std::vector<XLSXFillEntry> fills_;
    std::vector<XLSXBorderEntry> borders_;
    std::vector<XLSXCellFormatEntry> formats_;
    std::unordered_map<std::string, size_t> fontIndex_;
    std::unordered_map<std::string, size_t> fillIndex_;
    std::unordered_map<std::string, size_t> borderIndex_;
    std::unordered_map<std::string, size_t> formatIndex_;

    static std::string fontKey(const XLSXFontEntry& f) {
        std::ostringstream oss;
        oss << (f.bold ? "B" : "b") << (f.italic ? "I" : "i") << (f.underline ? "U" : "u") << "|"
            << f.name << "|" << f.size << "|" << f.color;
        return oss.str();
    }

    static std::string borderEdgeKey(const XLSXBorderEdgeEntry& e) {
        return std::to_string(static_cast<int>(e.style)) + ":" + e.color;
    }

    static std::string borderKey(const XLSXBorderEntry& b) {
        return borderEdgeKey(b.left) + "|" + borderEdgeKey(b.right) + "|" + borderEdgeKey(b.top) +
               "|" + borderEdgeKey(b.bottom);
    }

    static std::string formatKey(const XLSXCellFormatEntry& xf) {
        std::ostringstream oss;
        oss << xf.fontId << "|" << xf.fillId << "|" << xf.borderId << "|"
            << static_cast<int>(xf.hAlign) << "|" << static_cast<int>(xf.vAlign) << "|"
            << xf.wrapText << "|" << xf.hasAlignment;
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

    size_t getOrAddBorder(const XLSXBorderEntry& border) {
        // Empty border uses index 0
        if (!border.hasValue()) {
            return 0;
        }
        const std::string key = borderKey(border);
        auto it = borderIndex_.find(key);
        if (it != borderIndex_.end()) {
            return it->second;
        }
        const size_t idx = borders_.size();
        borders_.push_back(border);
        borderIndex_[key] = idx;
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
// axisStyleIndices maps axis pointer to XLSX style index (style attribute for cols, s for rows)
std::string generateWorksheet(
    const cells::Sheet& sheet, SharedStringTable& sst, const cells::RefConverter& refConverter,
    bool writeFormulas, const std::unordered_map<const cells::Cell*, size_t>& cellStyleIndices,
    const std::unordered_map<const cells::Axis*, size_t>& axisStyleIndices) {
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

    // Write sheetViews (including showGridLines, zoomScale, and freeze panes)
    xml << "  <sheetViews>\n";
    xml << "    <sheetView workbookViewId=\"0\"";
    if (!sheet.showGridLines) {
        xml << " showGridLines=\"0\"";
    }
    if (sheet.zoomScale != 100) {
        xml << " zoomScale=\"" << sheet.zoomScale << "\"";
    }

    // Add freeze panes if either freezeCol or freezeRow is set
    if (sheet.freezeCol > 0 || sheet.freezeRow > 0) {
        xml << ">\n";
        xml << "      <pane";
        if (sheet.freezeCol > 0) {
            xml << " xSplit=\"" << sheet.freezeCol << "\"";
        }
        if (sheet.freezeRow > 0) {
            xml << " ySplit=\"" << sheet.freezeRow << "\"";
        }
        // topLeftCell is the first unfrozen cell (e.g., B2 if col A and row 1 are frozen)
        const std::string topLeftCol = colIndexToLetter(sheet.freezeCol);
        const int topLeftRow = sheet.freezeRow + 1;  // 1-indexed for XLSX
        xml << " topLeftCell=\"" << topLeftCol << topLeftRow << "\"";
        // Determine activePane based on which dimensions are frozen
        if (sheet.freezeCol > 0 && sheet.freezeRow > 0) {
            xml << " activePane=\"bottomRight\"";
        } else if (sheet.freezeCol > 0) {
            xml << " activePane=\"topRight\"";
        } else {
            xml << " activePane=\"bottomLeft\"";
        }
        xml << " state=\"frozen\"/>\n";
        xml << "    </sheetView>\n";
    } else {
        xml << "/>\n";
    }
    xml << "  </sheetViews>\n";

    // Write cols element if any columns have hidden or style attributes
    bool needColsElement = false;
    for (const auto& colPair : columns) {
        auto it = sheet.columns.find(colPair.second);
        if (it != sheet.columns.end()) {
            if (it->second->hidden || axisStyleIndices.count(it->second.get()) > 0) {
                needColsElement = true;
                break;
            }
        }
    }
    if (needColsElement) {
        xml << "  <cols>\n";
        for (size_t i = 0; i < columns.size(); ++i) {
            auto it = sheet.columns.find(columns[i].second);
            if (it != sheet.columns.end()) {
                const bool hidden = it->second->hidden;
                auto styleIt = axisStyleIndices.find(it->second.get());
                const bool hasStyle = styleIt != axisStyleIndices.end() && styleIt->second > 0;

                if (hidden || hasStyle) {
                    // Excel uses 1-based column indices
                    xml << "    <col min=\"" << (i + 1) << "\" max=\"" << (i + 1) << "\"";
                    if (hasStyle) {
                        xml << " style=\"" << styleIt->second << "\"";
                    }
                    if (hidden) {
                        xml << " hidden=\"1\"";
                    }
                    xml << "/>\n";
                }
            }
        }
        xml << "  </cols>\n";
    }

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

        // Check if row is hidden and/or has style
        bool rowHidden = false;
        size_t rowStyleIdx = 0;
        auto rowIt = sheet.rows.find(rows[rowIdx].second);
        if (rowIt != sheet.rows.end()) {
            if (rowIt->second->hidden) {
                rowHidden = true;
            }
            auto styleIt = axisStyleIndices.find(rowIt->second.get());
            if (styleIt != axisStyleIndices.end()) {
                rowStyleIdx = styleIt->second;
            }
        }

        // Skip rows with no cells, not hidden, and no style
        if (!hasAnyCells && !rowHidden && rowStyleIdx == 0) {
            continue;
        }

        xml << "    <row r=\"" << (rowIdx + 1) << "\"";
        if (rowStyleIdx > 0) {
            xml << " s=\"" << rowStyleIdx << "\" customFormat=\"1\"";
        }
        if (rowHidden) {
            xml << " hidden=\"1\"";
        }
        xml << ">\n";

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
                // Set cell type based on cached value type
                // Formula cells need t="str" for string results, t="e" for errors, etc.
                if (value.type == cells::CellValueType::FORMULA_STRING ||
                    value.type == cells::CellValueType::STRING) {
                    xml << " t=\"str\"";
                } else if (value.type == cells::CellValueType::FORMULA_ERROR ||
                           value.type == cells::CellValueType::ERROR) {
                    xml << " t=\"e\"";
                } else if (value.type == cells::CellValueType::FORMULA_BOOLEAN ||
                           value.type == cells::CellValueType::BOOLEAN) {
                    xml << " t=\"b\"";
                }
                // NUMBER types don't need a t attribute (it's the default)
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

    // Write merged cells from the unified Range system
    // Collect all ranges with MERGE flag
    std::vector<const cells::Range*> mergeRanges;
    for (const auto& [rangeId, range] : sheet.getRanges()) {
        if (range->hasFlag(cells::RangeFlags::MERGE)) {
            mergeRanges.push_back(range.get());
        }
    }

    if (!mergeRanges.empty()) {
        xml << "  <mergeCells count=\"" << mergeRanges.size() << "\">\n";
        for (const auto* range : mergeRanges) {
            // Find corner column and row positions
            auto startColIt = sheet.columns.find(range->startColId);
            auto startRowIt = sheet.rows.find(range->startRowId);
            auto endColIt = sheet.columns.find(range->endColId);
            auto endRowIt = sheet.rows.find(range->endRowId);
            if (startColIt == sheet.columns.end() || startRowIt == sheet.rows.end() ||
                endColIt == sheet.columns.end() || endRowIt == sheet.rows.end()) {
                continue;  // Skip invalid merge ranges
            }

            const uint32_t startColPos = startColIt->second->position;
            const uint32_t startRowPos = startRowIt->second->position;
            const uint32_t endColPos = endColIt->second->position;
            const uint32_t endRowPos = endRowIt->second->position;

            // Convert to A1 notation (1-indexed rows)
            const std::string startRef =
                colIndexToLetter(startColPos) + std::to_string(startRowPos + 1);
            const std::string endRef = colIndexToLetter(endColPos) + std::to_string(endRowPos + 1);

            xml << "    <mergeCell ref=\"" << startRef << ":" << endRef << "\"/>\n";
        }
        xml << "  </mergeCells>\n";
    }

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
        case cells::TextAlign::LEFT:
            return "left";
        case cells::TextAlign::CENTER:
            return "center";
        case cells::TextAlign::RIGHT:
            return "right";
        case cells::TextAlign::JUSTIFY:
            return "justify";
        case cells::TextAlign::GENERAL:
        default:
            return "general";
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

// Border style enum to XLSX string
const char* borderStyleToXlsx(cells::BorderStyle style) {
    switch (style) {
        case cells::BorderStyle::THIN:
            return "thin";
        case cells::BorderStyle::MEDIUM:
            return "medium";
        case cells::BorderStyle::THICK:
            return "thick";
        case cells::BorderStyle::DASHED:
            return "dashed";
        case cells::BorderStyle::DOTTED:
            return "dotted";
        case cells::BorderStyle::DOUBLE:
            return "double";
        case cells::BorderStyle::HAIR:
            return "hair";
        case cells::BorderStyle::MEDIUM_DASHED:
            return "mediumDashed";
        case cells::BorderStyle::DASH_DOT:
            return "dashDot";
        case cells::BorderStyle::MEDIUM_DASH_DOT:
            return "mediumDashDot";
        case cells::BorderStyle::DASH_DOT_DOT:
            return "dashDotDot";
        case cells::BorderStyle::MEDIUM_DASH_DOT_DOT:
            return "mediumDashDotDot";
        case cells::BorderStyle::SLANT_DASH_DOT:
            return "slantDashDot";
        default:
            return nullptr;  // NONE - don't output style attribute
    }
}

// Output a border edge element (left, right, top, bottom)
void writeBorderEdge(std::ostringstream& xml, const char* name, const XLSXBorderEdgeEntry& edge) {
    const char* styleStr = borderStyleToXlsx(edge.style);
    if (styleStr == nullptr) {
        // No border style - write empty element
        xml << "      <" << name << "/>\n";
    } else {
        xml << "      <" << name << " style=\"" << styleStr << "\">";
        if (!edge.color.empty()) {
            xml << "<color rgb=\"" << edge.color << "\"/>";
        } else {
            // Use auto color (black)
            xml << "<color auto=\"1\"/>";
        }
        xml << "</" << name << ">\n";
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

    // Borders
    const auto& borders = styles.borders();
    xml << "  <borders count=\"" << borders.size() << "\">\n";
    for (const auto& border : borders) {
        xml << "    <border>\n";
        writeBorderEdge(xml, "left", border.left);
        writeBorderEdge(xml, "right", border.right);
        writeBorderEdge(xml, "top", border.top);
        writeBorderEdge(xml, "bottom", border.bottom);
        xml << "      <diagonal/>\n";
        xml << "    </border>\n";
    }
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
            << "\" borderId=\"" << xf.borderId << "\" xfId=\"0\"";

        // Apply flags
        if (xf.fontId > 0) {
            xml << " applyFont=\"1\"";
        }
        if (xf.fillId > 0) {
            xml << " applyFill=\"1\"";
        }
        if (xf.borderId > 0) {
            xml << " applyBorder=\"1\"";
        }
        if (xf.hasAlignment) {
            xml << " applyAlignment=\"1\"";
        }

        if (xf.hasAlignment) {
            xml << ">\n";
            xml << "      <alignment";
            // Only write horizontal if not GENERAL (GENERAL is Excel's default)
            if (xf.hAlign != cells::TextAlign::GENERAL) {
                xml << " horizontal=\"" << hAlignToXlsx(xf.hAlign) << "\"";
            }
            if (xf.vAlign != cells::VerticalAlign::BOTTOM) {
                xml << " vertical=\"" << vAlignToXlsx(xf.vAlign) << "\"";
            }
            if (xf.wrapText) {
                xml << " wrapText=\"1\"";
            }
            xml << "/>\n";
            xml << "    </xf>\n";
        } else {
            xml << "/>\n";
        }
    }
    xml << "  </cellXfs>\n";

    // Cell styles - defines the "Normal" style that Excel requires
    xml << "  <cellStyles count=\"1\">\n";
    xml << "    <cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/>\n";
    xml << "  </cellStyles>\n";

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

    // Write document properties
    if (!zip.addFile("docProps/core.xml", generateCoreProps())) {
        result.error = XLSXWriteError("Failed to write docProps/core.xml");
        return result;
    }

    if (!zip.addFile("docProps/app.xml", generateAppProps())) {
        result.error = XLSXWriteError("Failed to write docProps/app.xml");
        return result;
    }

    // Write theme
    if (!zip.addFile("xl/theme/theme1.xml", generateTheme())) {
        result.error = XLSXWriteError("Failed to write xl/theme/theme1.xml");
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

    // Collect styles from all sheets (cells and axes)
    StyleTable styleTable;
    std::unordered_map<const Cell*, size_t> cellStyleIndices;
    std::unordered_map<const Axis*, size_t> axisStyleIndices;

    for (const auto& sheet : workbook.sheets) {
        // Collect cell styles
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
        // Collect column default styles
        for (const auto& [id, col] : sheet->columns) {
            if (!col->defaultStyleId.isNull()) {
                const CellStyle* style = workbook.getStyle(col->defaultStyleId);
                if (style != nullptr) {
                    const size_t styleIdx = styleTable.getOrAddFormat(*style);
                    axisStyleIndices[col.get()] = styleIdx;
                }
            }
        }
        // Collect row default styles
        for (const auto& [id, row] : sheet->rows) {
            if (!row->defaultStyleId.isNull()) {
                const CellStyle* style = workbook.getStyle(row->defaultStyleId);
                if (style != nullptr) {
                    const size_t styleIdx = styleTable.getOrAddFormat(*style);
                    axisStyleIndices[row.get()] = styleIdx;
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
        const std::string sheetXml = generateWorksheet(
            sheet, sst, refConverter, options_.writeFormulas, cellStyleIndices, axisStyleIndices);

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
