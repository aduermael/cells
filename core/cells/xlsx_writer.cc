#include "core/cells/xlsx_writer.h"

#include <cstring>

#include <algorithm>
#include <sstream>
#include <unordered_map>

#include "core/cells/format_buffer.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/named_ranges.h"
#include "core/cells/number_format.h"
#include "core/cells/range.h"
#include "core/cells/ref_converter.h"

#include "miniz.h"

namespace {

// Forward declarations
std::string escapeXml(const std::string& str);
std::string colIndexToLetter(size_t index);

// Format a double for XML attributes, preserving full precision without trailing zeros
std::string formatDouble(double value) {
    // Use snprintf with enough precision to represent all significant digits
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", value);
    return buf;
}

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
    const cells::Axis* col = sheet->getColumn(cell->colId);
    const cells::Axis* row = sheet->getRow(cell->rowId);
    if (col == nullptr || row == nullptr) {
        return "";
    }

    const uint32_t colPos = col->position;
    const uint32_t rowPos = row->position;

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
    const cells::Axis* startCol = sheet->getColumn(startCell->colId);
    const cells::Axis* startRow = sheet->getRow(startCell->rowId);
    if (startCol == nullptr || startRow == nullptr) {
        return "";
    }

    // Find positions for end cell
    const cells::Axis* endCol = sheet->getColumn(endCell->colId);
    const cells::Axis* endRow = sheet->getRow(endCell->rowId);
    if (endCol == nullptr || endRow == nullptr) {
        return "";
    }

    const uint32_t startColPos = startCol->position;
    const uint32_t startRowPos = startRow->position;
    const uint32_t endColPos = endCol->position;
    const uint32_t endRowPos = endRow->position;

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
                        const cells::Cell* cell = workbook.getCell(target.id1);
                        xlsxRef = cellIdToXlsxRef(cell, targetSheet);
                        break;
                    }
                    case cells::NamedRangeTarget::Type::RANGE: {
                        const cells::Cell* startCell = workbook.getCell(target.id1);
                        const cells::Cell* endCell = workbook.getCell(target.id2);
                        xlsxRef = rangeToXlsxRef(startCell, endCell, targetSheet);
                        break;
                    }
                    case cells::NamedRangeTarget::Type::COLUMN: {
                        const cells::Axis* col = targetSheet->getColumn(target.id1);
                        xlsxRef = columnRangeToXlsxRef(col, col, targetSheet);
                        break;
                    }
                    case cells::NamedRangeTarget::Type::ROW: {
                        const cells::Axis* row = targetSheet->getRow(target.id1);
                        xlsxRef = rowRangeToXlsxRef(row, row, targetSheet);
                        break;
                    }
                    case cells::NamedRangeTarget::Type::COLUMN_RANGE: {
                        const cells::Axis* startCol = targetSheet->getColumn(target.id1);
                        const cells::Axis* endCol = targetSheet->getColumn(target.id2);
                        xlsxRef = columnRangeToXlsxRef(startCol, endCol, targetSheet);
                        break;
                    }
                    case cells::NamedRangeTarget::Type::ROW_RANGE: {
                        const cells::Axis* startRow = targetSheet->getRow(target.id1);
                        const cells::Axis* endRow = targetSheet->getRow(target.id2);
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

// Helper to strip '#' prefix from a hex color string for XML output
std::string hexVal(const std::string& color) {
    if (!color.empty() && color[0] == '#') {
        return color.substr(1);
    }
    return color;
}

// Generate xl/theme/theme1.xml
// If theme is provided, writes its colors and fonts; otherwise writes minimal Office defaults.
std::string generateTheme(const cells::Theme* theme) {
    // DrawingML color scheme element names in XML order (dk1, lt1, dk2, lt2, then accents)
    // Note: XML order differs from index order (lt1=0, dk1=1, lt2=2, dk2=3)
    struct ColorSlot {
        const char* xmlTag;
        int themeIndex;
    };
    static constexpr ColorSlot kSlots[] = {
        {"a:dk1", 1},     {"a:lt1", 0},     {"a:dk2", 3},     {"a:lt2", 2},
        {"a:accent1", 4}, {"a:accent2", 5}, {"a:accent3", 6}, {"a:accent4", 7},
        {"a:accent5", 8}, {"a:accent6", 9}, {"a:hlink", 10},  {"a:folHlink", 11},
    };

    // Default colors (Office theme)
    static const char* const kDefaultColors[] = {
        "FFFFFF",  // 0: lt1
        "000000",  // 1: dk1
        "E7E6E6",  // 2: lt2
        "44546A",  // 3: dk2
        "4472C4",  // 4: accent1
        "ED7D31",  // 5: accent2
        "A5A5A5",  // 6: accent3
        "FFC000",  // 7: accent4
        "5B9BD5",  // 8: accent5
        "70AD47",  // 9: accent6
        "0563C1",  // 10: hlink
        "954F72",  // 11: folHlink
    };

    const std::string themeName = theme ? theme->name : "Office Theme";
    const std::string schemeName = theme ? theme->name : "Office";
    const std::string majorFont = theme ? theme->fontScheme.majorFont : "Calibri Light";
    const std::string minorFont = theme ? theme->fontScheme.minorFont : "Calibri";

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
           "name=\""
        << escapeXml(themeName) << "\">\n";
    xml << "  <a:themeElements>\n";

    // Color scheme - required
    xml << "    <a:clrScheme name=\"" << escapeXml(schemeName) << "\">\n";
    for (const auto& slot : kSlots) {
        std::string color;
        if (theme) {
            color = hexVal(theme->colorScheme.getColor(slot.themeIndex));
        }
        if (color.empty()) {
            color = kDefaultColors[slot.themeIndex];
        }
        xml << "      <" << slot.xmlTag << "><a:srgbClr val=\"" << color << "\"/></" << slot.xmlTag
            << ">\n";
    }
    xml << "    </a:clrScheme>\n";

    // Font scheme - required
    xml << "    <a:fontScheme name=\"" << escapeXml(schemeName) << "\">\n";
    xml << "      <a:majorFont>\n";
    xml << "        <a:latin typeface=\"" << escapeXml(majorFont) << "\"/>\n";
    xml << "        <a:ea typeface=\"\"/>\n";
    xml << "        <a:cs typeface=\"\"/>\n";
    xml << "      </a:majorFont>\n";
    xml << "      <a:minorFont>\n";
    xml << "        <a:latin typeface=\"" << escapeXml(minorFont) << "\"/>\n";
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

    // Theme/indexed color references (-1 = not set)
    int8_t colorThemeIndex{-1};
    double colorThemeTint{0.0};
    int8_t colorIndexed{-1};

    // Font scheme reference (-1 = direct, 0 = major, 1 = minor)
    int8_t fontSchemeIndex{-1};

    bool operator==(const XLSXFontEntry& other) const {
        return bold == other.bold && italic == other.italic && underline == other.underline &&
               name == other.name && size == other.size && color == other.color &&
               colorThemeIndex == other.colorThemeIndex && colorThemeTint == other.colorThemeTint &&
               colorIndexed == other.colorIndexed && fontSchemeIndex == other.fontSchemeIndex;
    }
};

// Fill entry for styles.xml
struct XLSXFillEntry {
    std::string fgColor;  // ARGB hex, empty = none

    // Theme/indexed color references for fgColor (-1 = not set)
    int8_t fgThemeIndex{-1};
    double fgThemeTint{0.0};
    int8_t fgIndexed{-1};

    bool operator==(const XLSXFillEntry& other) const {
        return fgColor == other.fgColor && fgThemeIndex == other.fgThemeIndex &&
               fgThemeTint == other.fgThemeTint && fgIndexed == other.fgIndexed;
    }
};

// Single border edge entry for styles.xml
struct XLSXBorderEdgeEntry {
    cells::BorderStyle style{cells::BorderStyle::NONE};
    std::string color;  // ARGB hex, empty = use default (auto)

    // Theme/indexed color references (-1 = not set)
    int8_t colorThemeIndex{-1};
    double colorThemeTint{0.0};
    int8_t colorIndexed{-1};

    bool operator==(const XLSXBorderEdgeEntry& other) const {
        return style == other.style && color == other.color &&
               colorThemeIndex == other.colorThemeIndex && colorThemeTint == other.colorThemeTint &&
               colorIndexed == other.colorIndexed;
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
    size_t numFmtId{0};  // Number format ID (0 = General)
    cells::TextAlign hAlign{cells::TextAlign::GENERAL};
    cells::VerticalAlign vAlign{cells::VerticalAlign::BOTTOM};
    bool wrapText{false};
    bool hasAlignment{false};

    bool operator==(const XLSXCellFormatEntry& other) const {
        return fontId == other.fontId && fillId == other.fillId && borderId == other.borderId &&
               numFmtId == other.numFmtId && hAlign == other.hAlign && vAlign == other.vAlign &&
               wrapText == other.wrapText && hasAlignment == other.hasAlignment;
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

// Custom number format entry for XLSX export
struct XLSXNumFmtEntry {
    size_t numFmtId{0};
    std::string formatCode;
};

// Style table that collects fonts, fills, borders, numFmts, and cell formats for XLSX export
class StyleTable {
public:
    StyleTable() {
        // Add default font (index 0) - required by Excel
        fonts_.push_back(XLSXFontEntry{});
        fontIndex_[fontKey(fonts_[0])] = 0;

        // Add required fills (indices 0 and 1) - required by Excel
        fills_.emplace_back();                          // none (index 0)
        fills_.emplace_back(XLSXFillEntry{"gray125"});  // gray125 (required placeholder)
        fillIndex_[fillKey(fills_[0])] = 0;
        fillIndex_[fillKey(fills_[1])] = 1;

        // Add default border (index 0) - required by Excel
        borders_.emplace_back();  // Empty border (no edges)
        borderIndex_[borderKey(borders_[0])] = 0;

        // Add default cell format (index 0)
        formats_.push_back(XLSXCellFormatEntry{});
        formatIndex_[formatKey(formats_[0])] = 0;
    }

    // Get or add a cell format for a given CellStyle and optional FormatBuffer
    // Returns the cellXfs index for this style
    size_t getOrAddFormat(const cells::CellStyle& style,
                          const cells::FormatBuffer* formatBuf = nullptr) {
        // First, get or add font
        XLSXFontEntry font;
        font.bold = style.bold;
        font.italic = style.italic;
        font.underline = style.underline;
        font.name = style.fontFamily.empty() ? "Calibri" : style.fontFamily;
        font.size = style.fontSize > 0 ? style.fontSize : 11;
        font.color = rgbToArgb(style.textColor);
        font.colorThemeIndex = style.textThemeIndex;
        font.colorThemeTint = style.textThemeTint;
        font.colorIndexed = style.textIndexedColor;
        font.fontSchemeIndex = style.fontThemeIndex;
        const size_t fontId = getOrAddFont(font);

        // Get or add fill (background)
        XLSXFillEntry fill;
        fill.fgColor = rgbToArgb(style.bgColor);
        fill.fgThemeIndex = style.bgThemeIndex;
        fill.fgThemeTint = style.bgThemeTint;
        fill.fgIndexed = style.bgIndexedColor;
        const size_t fillId = getOrAddFill(fill);

        // Get or add border
        XLSXBorderEntry border;
        border.left.style = style.border.left.style;
        border.left.color = rgbToArgb(style.border.left.color);
        border.left.colorThemeIndex = style.border.left.themeIndex;
        border.left.colorThemeTint = style.border.left.themeTint;
        border.left.colorIndexed = style.border.left.indexedColor;
        border.right.style = style.border.right.style;
        border.right.color = rgbToArgb(style.border.right.color);
        border.right.colorThemeIndex = style.border.right.themeIndex;
        border.right.colorThemeTint = style.border.right.themeTint;
        border.right.colorIndexed = style.border.right.indexedColor;
        border.top.style = style.border.top.style;
        border.top.color = rgbToArgb(style.border.top.color);
        border.top.colorThemeIndex = style.border.top.themeIndex;
        border.top.colorThemeTint = style.border.top.themeTint;
        border.top.colorIndexed = style.border.top.indexedColor;
        border.bottom.style = style.border.bottom.style;
        border.bottom.color = rgbToArgb(style.border.bottom.color);
        border.bottom.colorThemeIndex = style.border.bottom.themeIndex;
        border.bottom.colorThemeTint = style.border.bottom.themeTint;
        border.bottom.colorIndexed = style.border.bottom.indexedColor;
        const size_t borderId = getOrAddBorder(border);

        // Get number format ID
        const size_t numFmtId = getNumFmtId(formatBuf);

        // Create cell format
        XLSXCellFormatEntry xf;
        xf.fontId = fontId;
        xf.fillId = fillId;
        xf.borderId = borderId;
        xf.numFmtId = numFmtId;
        xf.hAlign = style.hAlign;
        xf.vAlign = style.vAlign;
        xf.wrapText = style.wrapText;
        xf.hasAlignment = (style.hAlign != cells::TextAlign::GENERAL ||
                           style.vAlign != cells::VerticalAlign::BOTTOM || style.wrapText);

        return getOrAddCellFormat(xf);
    }

    // Get or add a cell format for FormatBuffer only (no visual style)
    size_t getOrAddFormatOnly(const cells::FormatBuffer* formatBuf) {
        if (formatBuf == nullptr || formatBuf->isEmpty()) {
            return 0;  // Default format
        }

        const size_t numFmtId = getNumFmtId(formatBuf);
        if (numFmtId == 0) {
            return 0;  // General format, use default
        }

        // Create cell format with just number format
        XLSXCellFormatEntry xf;
        xf.numFmtId = numFmtId;
        return getOrAddCellFormat(xf);
    }

    [[nodiscard]] const std::vector<XLSXFontEntry>& fonts() const { return fonts_; }
    [[nodiscard]] const std::vector<XLSXFillEntry>& fills() const { return fills_; }
    [[nodiscard]] const std::vector<XLSXBorderEntry>& borders() const { return borders_; }
    [[nodiscard]] const std::vector<XLSXCellFormatEntry>& formats() const { return formats_; }
    [[nodiscard]] const std::vector<XLSXNumFmtEntry>& numFmts() const { return numFmts_; }

private:
    std::vector<XLSXFontEntry> fonts_;
    std::vector<XLSXFillEntry> fills_;
    std::vector<XLSXBorderEntry> borders_;
    std::vector<XLSXCellFormatEntry> formats_;
    std::vector<XLSXNumFmtEntry> numFmts_;  // Custom number formats (IDs >= 164)
    std::unordered_map<std::string, size_t> fontIndex_;
    std::unordered_map<std::string, size_t> fillIndex_;
    std::unordered_map<std::string, size_t> borderIndex_;
    std::unordered_map<std::string, size_t> formatIndex_;
    std::unordered_map<std::string, size_t> numFmtIndex_;  // formatCode -> numFmtId

    // Convert FormatBuffer to XLSX numFmtId
    // Returns 0 for General, built-in IDs (1-49) for standard formats,
    // or custom IDs (>= 164) for custom format codes
    size_t getNumFmtId(const cells::FormatBuffer* formatBuf) {
        if (formatBuf == nullptr || formatBuf->isEmpty()) {
            return 0;  // General
        }

        // Check for custom format code first
        if (formatBuf->hasCustomFormatCode()) {
            const std::string formatCode = formatBuf->getCustomFormatCode();
            return getOrAddCustomNumFmt(formatCode);
        }

        // Map category + properties to built-in XLSX format ID
        if (!formatBuf->hasCategory()) {
            return 0;  // General
        }

        const cells::NumberFormatCategory cat = formatBuf->getCategory();
        const uint8_t decimals = formatBuf->hasDecimals() ? formatBuf->getDecimals() : 0;
        const bool hasThousands = formatBuf->hasThousandsSeparator();
        const bool hasCurrency = formatBuf->hasCurrencySymbol();

        switch (cat) {
            case cells::NumberFormatCategory::GENERAL:
                return 0;

            case cells::NumberFormatCategory::NUMBER:
                if (hasThousands) {
                    // #,##0 or #,##0.00
                    return decimals >= 2 ? 4 : 3;
                }
                // 0 or 0.00
                return decimals >= 2 ? 2 : 1;

            case cells::NumberFormatCategory::CURRENCY:
                // Use built-in USD formats if no specific symbol or $ symbol
                if (!hasCurrency || formatBuf->getCurrencySymbol() == "$") {
                    return decimals >= 2 ? 8 : 6;  // $#,##0.00 or $#,##0
                }
                // For other currencies, generate custom format code
                return getOrAddCustomNumFmt(formatBuf->toFormatCode());

            case cells::NumberFormatCategory::ACCOUNTING:
                return decimals >= 2 ? 44 : 42;  // _($*#,##0.00_) or _($*#,##0_)

            case cells::NumberFormatCategory::PERCENTAGE:
                return decimals >= 2 ? 10 : 9;  // 0.00% or 0%

            case cells::NumberFormatCategory::DATE:
                return 14;  // mm-dd-yy

            case cells::NumberFormatCategory::TIME:
                return 20;  // h:mm

            case cells::NumberFormatCategory::DATE_TIME:
                return 22;  // m/d/yy h:mm

            case cells::NumberFormatCategory::SCIENTIFIC:
                return 11;  // 0.00E+00

            case cells::NumberFormatCategory::FRACTION:
                return 12;  // # ?/?

            case cells::NumberFormatCategory::TEXT:
                return 49;  // @

            case cells::NumberFormatCategory::CUSTOM:
                // Generate format code and register as custom
                return getOrAddCustomNumFmt(formatBuf->toFormatCode());
        }

        return 0;  // Default to General
    }

    // Get or add a custom number format
    // Returns the numFmtId (>= 164) for this format code
    size_t getOrAddCustomNumFmt(const std::string& formatCode) {
        if (formatCode.empty()) {
            return 0;
        }

        auto it = numFmtIndex_.find(formatCode);
        if (it != numFmtIndex_.end()) {
            return it->second;
        }

        // Custom numFmtIds start at 164 in XLSX
        const size_t numFmtId = 164 + numFmts_.size();
        numFmts_.push_back({numFmtId, formatCode});
        numFmtIndex_[formatCode] = numFmtId;
        return numFmtId;
    }

    static std::string fontKey(const XLSXFontEntry& f) {
        std::ostringstream oss;
        oss << (f.bold ? "B" : "b") << (f.italic ? "I" : "i") << (f.underline ? "U" : "u") << "|"
            << f.name << "|" << f.size << "|" << f.color << "|"
            << static_cast<int>(f.colorThemeIndex) << ":" << f.colorThemeTint << ":"
            << static_cast<int>(f.colorIndexed) << "|" << static_cast<int>(f.fontSchemeIndex);
        return oss.str();
    }

    static std::string borderEdgeKey(const XLSXBorderEdgeEntry& e) {
        std::ostringstream oss;
        oss << static_cast<int>(e.style) << ":" << e.color << ":"
            << static_cast<int>(e.colorThemeIndex) << ":" << e.colorThemeTint << ":"
            << static_cast<int>(e.colorIndexed);
        return oss.str();
    }

    static std::string borderKey(const XLSXBorderEntry& b) {
        return borderEdgeKey(b.left) + "|" + borderEdgeKey(b.right) + "|" + borderEdgeKey(b.top) +
               "|" + borderEdgeKey(b.bottom);
    }

    static std::string formatKey(const XLSXCellFormatEntry& xf) {
        std::ostringstream oss;
        oss << xf.fontId << "|" << xf.fillId << "|" << xf.borderId << "|" << xf.numFmtId << "|"
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

    static std::string fillKey(const XLSXFillEntry& f) {
        std::ostringstream oss;
        oss << f.fgColor << "|" << static_cast<int>(f.fgThemeIndex) << ":" << f.fgThemeTint << ":"
            << static_cast<int>(f.fgIndexed);
        return oss.str();
    }

    size_t getOrAddFill(const XLSXFillEntry& fill) {
        // Empty fill uses index 0 (no color refs of any kind)
        if (fill.fgColor.empty() && fill.fgThemeIndex < 0 && fill.fgIndexed < 0) {
            return 0;
        }
        const std::string key = fillKey(fill);
        auto it = fillIndex_.find(key);
        if (it != fillIndex_.end()) {
            return it->second;
        }
        const size_t idx = fills_.size();
        fills_.push_back(fill);
        fillIndex_[key] = idx;
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
    const cells::Sheet& sheet, const cells::Workbook& workbook, SharedStringTable& sst,
    const cells::RefConverter& refConverter, bool writeFormulas, bool writeDimensions,
    const std::unordered_map<const cells::Cell*, size_t>& cellStyleIndices,
    const std::unordered_map<const cells::Axis*, size_t>& axisStyleIndices) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n";

    // Get ordered columns and rows
    std::vector<std::pair<uint32_t, cells::ID>> columns;
    columns.reserve(sheet.columnCount());
    for (const cells::ID& colId : sheet.getColumnIds()) {
        const cells::Axis* col = sheet.getColumn(colId);
        if (col) {
            columns.emplace_back(col->position, colId);
        }
    }
    std::sort(columns.begin(), columns.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<std::pair<uint32_t, cells::ID>> rows;
    rows.reserve(sheet.rowCount());
    for (const cells::ID& rowId : sheet.getRowIds()) {
        const cells::Axis* row = sheet.getRow(rowId);
        if (row) {
            rows.emplace_back(row->position, rowId);
        }
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
    for (const auto& cellId : sheet.getCellIds()) {
        const cells::Cell* cell = workbook.getCell(cellId);
        if (!cell) {
            continue;
        }
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
        for (const auto& cellId : sheet.getCellIds()) {
            const cells::Cell* cell = workbook.getCell(cellId);
            if (!cell) {
                continue;
            }
            // Check if this cell is a shared formula master
            if (cell->isSharedFormulaMaster()) {
                masterToSi[cell] = nextSi++;
            }
        }
        // Build master cell ID -> cell pointer map for reverse lookup
        std::unordered_map<std::string, const cells::Cell*> cellIdToCell;
        for (const auto& cellId : sheet.getCellIds()) {
            const cells::Cell* cell = workbook.getCell(cellId);
            if (!cell) {
                continue;
            }
            cellIdToCell[cell->id.toString()] = cell;
        }

        // Map subscribers to their master's si using Sheet-level tracking
        for (const auto& cellId : sheet.getCellIds()) {
            const cells::Cell* cell = workbook.getCell(cellId);
            if (!cell) {
                continue;
            }
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

    // Write cols element if any columns have hidden, style, or custom width attributes
    bool needColsElement = false;
    for (const auto& colPair : columns) {
        const cells::Axis* col = sheet.getColumn(colPair.second);
        if (col != nullptr) {
            if (col->hidden() || axisStyleIndices.count(col) > 0 ||
                (writeDimensions && col->sizeSet())) {
                needColsElement = true;
                break;
            }
        }
    }
    if (needColsElement) {
        xml << "  <cols>\n";
        for (size_t i = 0; i < columns.size(); ++i) {
            const cells::Axis* col = sheet.getColumn(columns[i].second);
            if (col != nullptr) {
                const bool hidden = col->hidden();
                auto styleIt = axisStyleIndices.find(col);
                const bool hasStyle = styleIt != axisStyleIndices.end() && styleIt->second > 0;
                const bool hasCustomWidth = writeDimensions && col->sizeSet();

                if (hidden || hasStyle || hasCustomWidth) {
                    // Excel uses 1-based column indices
                    xml << "    <col min=\"" << (i + 1) << "\" max=\"" << (i + 1) << "\"";
                    if (hasCustomWidth) {
                        // Use original Excel value if available (avoids lossy pixel conversion)
                        // Otherwise convert pixels back: width = pixels / 7.5
                        const double width = col->sizeOriginal > 0
                                                 ? col->sizeOriginal
                                                 : static_cast<double>(col->size) / 7.5;
                        xml << " width=\"" << formatDouble(width) << "\" customWidth=\"1\"";
                    }
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

        // Check if row is hidden, has style, or has custom height
        bool rowHidden = false;
        bool hasCustomHeight = false;
        size_t rowStyleIdx = 0;
        const cells::Axis* row = sheet.getRow(rows[rowIdx].second);
        if (row != nullptr) {
            if (row->hidden()) {
                rowHidden = true;
            }
            if (writeDimensions && row->sizeSet()) {
                hasCustomHeight = true;
            }
            auto styleIt = axisStyleIndices.find(row);
            if (styleIt != axisStyleIndices.end()) {
                rowStyleIdx = styleIt->second;
            }
        }

        // Skip rows with no cells, not hidden, no style, and no custom height
        if (!hasAnyCells && !rowHidden && rowStyleIdx == 0 && !hasCustomHeight) {
            continue;
        }

        xml << "    <row r=\"" << (rowIdx + 1) << "\"";
        if (hasCustomHeight) {
            // Use original Excel value if available (avoids lossy pixel conversion)
            // Otherwise convert pixels back: points = pixels * 72.0 / 96.0
            const double ht = row->sizeOriginal > 0 ? row->sizeOriginal
                                                    : static_cast<double>(row->size) * 72.0 / 96.0;
            xml << " ht=\"" << formatDouble(ht) << "\" customHeight=\"1\"";
        }
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
    for (const cells::ID& rangeId : sheet.getRangeIds()) {
        const cells::Range* range = sheet.getRange(rangeId);
        if (range != nullptr && range->hasFlag(cells::RangeFlags::MERGE)) {
            mergeRanges.push_back(range);
        }
    }

    if (!mergeRanges.empty()) {
        xml << "  <mergeCells count=\"" << mergeRanges.size() << "\">\n";
        for (const auto* range : mergeRanges) {
            // Find corner column and row positions
            const cells::Axis* startCol = sheet.getColumn(range->startColId);
            const cells::Axis* startRow = sheet.getRow(range->startRowId);
            const cells::Axis* endCol = sheet.getColumn(range->endColId);
            const cells::Axis* endRow = sheet.getRow(range->endRowId);
            if (startCol == nullptr || startRow == nullptr || endCol == nullptr ||
                endRow == nullptr) {
                continue;  // Skip invalid merge ranges
            }

            const uint32_t startColPos = startCol->position;
            const uint32_t startRowPos = startRow->position;
            const uint32_t endColPos = endCol->position;
            const uint32_t endRowPos = endRow->position;

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

// Write a color element with theme/indexed/RGB support
// tag: XML element name (e.g. "color", "fgColor", "bgColor")
void writeColorElement(std::ostringstream& xml, const char* tag, const std::string& argbColor,
                       int8_t themeIndex, double themeTint, int8_t indexedColor) {
    if (themeIndex >= 0) {
        xml << "<" << tag << " theme=\"" << static_cast<int>(themeIndex) << "\"";
        if (themeTint != 0.0) {
            xml << " tint=\"" << themeTint << "\"";
        }
        xml << "/>";
    } else if (indexedColor >= 0) {
        xml << "<" << tag << " indexed=\"" << static_cast<int>(indexedColor) << "\"/>";
    } else if (!argbColor.empty()) {
        xml << "<" << tag << " rgb=\"" << argbColor << "\"/>";
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
        if (edge.colorThemeIndex >= 0 || edge.colorIndexed >= 0 || !edge.color.empty()) {
            writeColorElement(xml, "color", edge.color, edge.colorThemeIndex, edge.colorThemeTint,
                              edge.colorIndexed);
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

    // Custom number formats (numFmts) - only if we have any
    const auto& numFmts = styles.numFmts();
    if (!numFmts.empty()) {
        xml << "  <numFmts count=\"" << numFmts.size() << "\">\n";
        for (const auto& numFmt : numFmts) {
            xml << "    <numFmt numFmtId=\"" << numFmt.numFmtId << "\" formatCode=\""
                << escapeXml(numFmt.formatCode) << "\"/>\n";
        }
        xml << "  </numFmts>\n";
    }

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
        if (font.colorThemeIndex >= 0 || font.colorIndexed >= 0 || !font.color.empty()) {
            xml << "      ";
            writeColorElement(xml, "color", font.color, font.colorThemeIndex, font.colorThemeTint,
                              font.colorIndexed);
            xml << "\n";
        }
        xml << "      <name val=\"" << escapeXml(font.name) << "\"/>\n";
        if (font.fontSchemeIndex == 0) {
            xml << "      <scheme val=\"major\"/>\n";
        } else if (font.fontSchemeIndex == 1) {
            xml << "      <scheme val=\"minor\"/>\n";
        }
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
        } else if (fill.fgThemeIndex >= 0 || fill.fgIndexed >= 0 ||
                   (!fill.fgColor.empty() && fill.fgColor != "gray125")) {
            // Solid fill with color (theme, indexed, or direct RGB)
            xml << "    <fill><patternFill patternType=\"solid\">";
            writeColorElement(xml, "fgColor", fill.fgColor, fill.fgThemeIndex, fill.fgThemeTint,
                              fill.fgIndexed);
            xml << "</patternFill></fill>\n";
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
        xml << "    <xf numFmtId=\"" << xf.numFmtId << "\" fontId=\"" << xf.fontId << "\" fillId=\""
            << xf.fillId << "\" borderId=\"" << xf.borderId << "\" xfId=\"0\"";

        // Apply flags
        if (xf.numFmtId > 0) {
            xml << " applyNumberFormat=\"1\"";
        }
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
    columns.reserve(sheet.columnCount());

    for (const ID& colId : sheet.getColumnIds()) {
        const Axis* col = sheet.getColumn(colId);
        if (col) {
            columns.emplace_back(col->position, colId);
        }
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
    rows.reserve(sheet.rowCount());

    for (const ID& rowId : sheet.getRowIds()) {
        const Axis* row = sheet.getRow(rowId);
        if (row) {
            rows.emplace_back(row->position, rowId);
        }
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
    if (!zip.addFile("xl/theme/theme1.xml", generateTheme(workbook.getTheme()))) {
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

    // Collect styles and formats from all sheets (cells and axes)
    StyleTable styleTable;
    std::unordered_map<const Cell*, size_t> cellStyleIndices;
    std::unordered_map<const Axis*, size_t> axisStyleIndices;

    for (const auto& sheet : workbook.sheets) {
        // Collect cell styles and formats from content-addressed storage
        for (const auto& cellId : sheet->getCellIds()) {
            const Cell* cell = workbook.getCell(cellId);
            if (!cell) {
                continue;
            }
            const StyleBuffer* styleBuf = workbook.getEntityStyle(cellId);
            const FormatBuffer* formatBuf = workbook.getEntityFormat(cellId);

            if (styleBuf != nullptr || formatBuf != nullptr) {
                if (styleBuf != nullptr) {
                    // Has visual style (and possibly format)
                    const CellStyle style = styleBuf->toCellStyle();
                    const size_t styleIdx = styleTable.getOrAddFormat(style, formatBuf);
                    cellStyleIndices[cell] = styleIdx;
                } else {
                    // Has format only, no visual style
                    const size_t styleIdx = styleTable.getOrAddFormatOnly(formatBuf);
                    if (styleIdx > 0) {
                        cellStyleIndices[cell] = styleIdx;
                    }
                }
            }
        }
        // Collect column default styles and formats
        for (const ID& colId : sheet->getColumnIds()) {
            const Axis* col = sheet->getColumn(colId);
            if (col == nullptr) {
                continue;
            }
            const StyleBuffer* styleBuf =
                col->hasStyle() ? workbook.getEntityStyle(col->id) : nullptr;
            const FormatBuffer* formatBuf =
                col->hasFormat() ? workbook.getEntityFormat(col->id) : nullptr;

            if (styleBuf != nullptr || formatBuf != nullptr) {
                if (styleBuf != nullptr) {
                    const CellStyle style = styleBuf->toCellStyle();
                    const size_t styleIdx = styleTable.getOrAddFormat(style, formatBuf);
                    axisStyleIndices[col] = styleIdx;
                } else {
                    const size_t styleIdx = styleTable.getOrAddFormatOnly(formatBuf);
                    if (styleIdx > 0) {
                        axisStyleIndices[col] = styleIdx;
                    }
                }
            }
        }
        // Collect row default styles and formats
        for (const ID& rowId : sheet->getRowIds()) {
            const Axis* row = sheet->getRow(rowId);
            if (row == nullptr) {
                continue;
            }
            const StyleBuffer* styleBuf =
                row->hasStyle() ? workbook.getEntityStyle(row->id) : nullptr;
            const FormatBuffer* formatBuf =
                row->hasFormat() ? workbook.getEntityFormat(row->id) : nullptr;

            if (styleBuf != nullptr || formatBuf != nullptr) {
                if (styleBuf != nullptr) {
                    const CellStyle style = styleBuf->toCellStyle();
                    const size_t styleIdx = styleTable.getOrAddFormat(style, formatBuf);
                    axisStyleIndices[row] = styleIdx;
                } else {
                    const size_t styleIdx = styleTable.getOrAddFormatOnly(formatBuf);
                    if (styleIdx > 0) {
                        axisStyleIndices[row] = styleIdx;
                    }
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
            generateWorksheet(sheet, workbook, sst, refConverter, options_.writeFormulas,
                              options_.writeDimensions, cellStyleIndices, axisStyleIndices);

        const std::string sheetPath = "xl/worksheets/sheet" + std::to_string(i + 1) + ".xml";
        if (!zip.addFile(sheetPath, sheetXml)) {
            result.error = XLSXWriteError("Failed to write " + sheetPath, sheet.name);
            return result;
        }

        totalCells += sheet.cellCount();
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
