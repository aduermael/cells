#include "core/cells/xlsx_reader.h"

#include <cstdlib>
#include <cstring>

#include <chrono>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "core/cells/formula_parser.h"
#include "core/cells/id.h"
#include "core/cells/types.h"

#include "miniz.h"
#include "pugixml.hpp"

namespace {

// Debug timing - set via environment variable
bool debugTiming() {
    static const bool enabled = std::getenv("CELLS_DEBUG_TIMING") != nullptr;
    return enabled;
}

void logTiming(const char* stage, std::chrono::steady_clock::time_point start) {
    if (debugTiming()) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        std::cerr << "[timing] " << stage << ": " << duration.count() / 1000.0 << "ms\n";
    }
}

// Parse cell reference "A1" -> (col=0, row=0)
void parseCellRef(const char* ref, int& col, int& row) {
    col = 0;
    row = 0;

    // Parse column letters (A-Z, AA-ZZ, etc.)
    while (*ref >= 'A' && *ref <= 'Z') {
        col = col * 26 + (*ref - 'A' + 1);
        ref++;
    }
    col--;  // Convert to 0-indexed

    // Parse row number
    while (*ref >= '0' && *ref <= '9') {
        row = row * 10 + (*ref - '0');
        ref++;
    }
    row--;  // Convert to 0-indexed
}

// Map XML cell type to our enum
int mapCellType(const char* type) {
    if (type == nullptr || *type == '\0') {
        return 2;  // Default is number
    }

    switch (type[0]) {
        case 's':
            return 1;  // Shared string -> STRING
        case 'b':
            return 3;  // Boolean
        case 'e':
            return 4;  // Error
        case 'n':
            return 2;  // Number
        case 'd':
            return 5;  // Date
        case 'i':
            // "inlineStr" - inline string
            return 1;  // STRING
        default:
            return 2;  // Default to number
    }
}

// ---------------------------------------------------------------------------
// XLSX Style parsing helpers
// ---------------------------------------------------------------------------

// Parsed font from styles.xml
struct XLSXFont {
    bool bold{false};
    bool italic{false};
    bool underline{false};
    std::string name;   // Font family name
    double size{0};     // Font size in points
    std::string color;  // Text color as #RRGGBB
};

// Parsed fill (background) from styles.xml
struct XLSXFill {
    std::string fgColor;  // Foreground color as #RRGGBB (used for solid fills)
    std::string bgColor;  // Background color as #RRGGBB
};

// Parsed alignment from styles.xml
struct XLSXAlignment {
    cells::TextAlign horizontal{cells::TextAlign::LEFT};
    cells::VerticalAlign vertical{cells::VerticalAlign::BOTTOM};
};

// Cell format record (cellXfs entry) - combines font, fill, alignment
struct XLSXCellFormat {
    int fontId{0};
    int fillId{0};
    int numFmtId{0};
    bool applyFont{false};
    bool applyFill{false};
    bool applyAlignment{false};
    XLSXAlignment alignment;
};

// Convert ARGB hex "FFRRGGBB" to "#RRGGBB"
std::string argbToRgb(const char* argb) {
    if (argb == nullptr || argb[0] == '\0') {
        return {};
    }
    // XLSX colors can be ARGB (8 chars) or RGB (6 chars)
    const size_t len = std::strlen(argb);
    if (len == 8) {
        // Skip alpha, take RGB
        return "#" + std::string(argb + 2, 6);
    }
    if (len == 6) {
        return "#" + std::string(argb, 6);
    }
    return {};
}

// Parse horizontal alignment string to enum
cells::TextAlign parseHorizontalAlign(const char* align) {
    if (align == nullptr) {
        return cells::TextAlign::LEFT;
    }
    if (std::strcmp(align, "center") == 0 || std::strcmp(align, "centerContinuous") == 0) {
        return cells::TextAlign::CENTER;
    }
    if (std::strcmp(align, "right") == 0) {
        return cells::TextAlign::RIGHT;
    }
    if (std::strcmp(align, "justify") == 0 || std::strcmp(align, "distributed") == 0) {
        return cells::TextAlign::JUSTIFY;
    }
    return cells::TextAlign::LEFT;
}

// Parse vertical alignment string to enum
cells::VerticalAlign parseVerticalAlign(const char* align) {
    if (align == nullptr) {
        return cells::VerticalAlign::BOTTOM;
    }
    if (std::strcmp(align, "top") == 0) {
        return cells::VerticalAlign::TOP;
    }
    if (std::strcmp(align, "center") == 0) {
        return cells::VerticalAlign::MIDDLE;
    }
    // Default is bottom
    return cells::VerticalAlign::BOTTOM;
}

// Container for all parsed styles from styles.xml
struct XLSXStyles {
    std::vector<XLSXFont> fonts;
    std::vector<XLSXFill> fills;
    std::vector<XLSXCellFormat> cellFormats;  // cellXfs entries

    // Convert an XLSX cell format index to our CellStyle
    // Returns true if any non-default style properties were found
    bool getCellStyle(int styleIndex, cells::CellStyle& outStyle) const {
        if (styleIndex < 0 || styleIndex >= static_cast<int>(cellFormats.size())) {
            return false;
        }

        const XLSXCellFormat& xf = cellFormats[styleIndex];
        bool hasStyle = false;

        // Apply font properties
        if (xf.applyFont && xf.fontId >= 0 && xf.fontId < static_cast<int>(fonts.size())) {
            const XLSXFont& font = fonts[xf.fontId];
            if (font.bold) {
                outStyle.bold = true;
                hasStyle = true;
            }
            if (font.italic) {
                outStyle.italic = true;
                hasStyle = true;
            }
            if (font.underline) {
                outStyle.underline = true;
                hasStyle = true;
            }
            if (!font.name.empty()) {
                outStyle.fontFamily = font.name;
                hasStyle = true;
            }
            if (font.size > 0) {
                outStyle.fontSize = static_cast<uint8_t>(font.size);
                hasStyle = true;
            }
            if (!font.color.empty()) {
                outStyle.textColor = font.color;
                hasStyle = true;
            }
        }

        // Apply fill properties (background color)
        if (xf.applyFill && xf.fillId >= 0 && xf.fillId < static_cast<int>(fills.size())) {
            const XLSXFill& fill = fills[xf.fillId];
            if (!fill.fgColor.empty()) {
                outStyle.bgColor = fill.fgColor;
                hasStyle = true;
            }
        }

        // Apply alignment
        if (xf.applyAlignment) {
            if (xf.alignment.horizontal != cells::TextAlign::LEFT) {
                outStyle.hAlign = xf.alignment.horizontal;
                hasStyle = true;
            }
            if (xf.alignment.vertical != cells::VerticalAlign::BOTTOM) {
                outStyle.vAlign = xf.alignment.vertical;
                hasStyle = true;
            }
        }

        return hasStyle;
    }
};

// Create a unique key string from a CellStyle (for deduplication)
std::string cellStyleToKey(const cells::CellStyle& style) {
    std::ostringstream oss;
    oss << (style.bold ? "B" : "b") << (style.italic ? "I" : "i") << (style.underline ? "U" : "u")
        << "|" << style.bgColor << "|" << style.textColor << "|" << style.fontFamily << "|"
        << static_cast<int>(style.fontSize) << "|" << static_cast<int>(style.hAlign) << "|"
        << static_cast<int>(style.vAlign);
    return oss.str();
}

// Parse xl/styles.xml into XLSXStyles struct
XLSXStyles parseStylesXml(const std::string& content) {
    XLSXStyles styles;

    if (content.empty()) {
        return styles;
    }

    pugi::xml_document doc;
    if (!doc.load_buffer(content.data(), content.size())) {
        return styles;
    }

    auto styleSheet = doc.child("styleSheet");

    // Parse fonts
    auto fontsNode = styleSheet.child("fonts");
    for (auto fontNode : fontsNode.children("font")) {
        XLSXFont font;

        // Bold: <b/> or <b val="true"/>
        auto bNode = fontNode.child("b");
        if (bNode) {
            const char* val = bNode.attribute("val").value();
            font.bold = (val == nullptr || val[0] == '\0' || std::strcmp(val, "1") == 0 ||
                         std::strcmp(val, "true") == 0);
        }

        // Italic: <i/> or <i val="true"/>
        auto iNode = fontNode.child("i");
        if (iNode) {
            const char* val = iNode.attribute("val").value();
            font.italic = (val == nullptr || val[0] == '\0' || std::strcmp(val, "1") == 0 ||
                           std::strcmp(val, "true") == 0);
        }

        // Underline: <u/> or <u val="single"/>
        auto uNode = fontNode.child("u");
        if (uNode) {
            const char* val = uNode.attribute("val").value();
            // Any underline value counts as underline (single, double, etc.)
            font.underline = (val == nullptr || val[0] == '\0' || std::strcmp(val, "none") != 0);
        }

        // Font name: <name val="Arial"/>
        auto nameNode = fontNode.child("name");
        if (nameNode) {
            font.name = nameNode.attribute("val").value();
        }

        // Font size: <sz val="11"/>
        auto szNode = fontNode.child("sz");
        if (szNode) {
            font.size = szNode.attribute("val").as_double(0);
        }

        // Font color: <color rgb="FF000000"/> or <color theme="1"/>
        auto colorNode = fontNode.child("color");
        if (colorNode) {
            const char* rgb = colorNode.attribute("rgb").value();
            if (rgb && rgb[0] != '\0') {
                font.color = argbToRgb(rgb);
            }
            // TODO: Support theme colors (requires parsing theme.xml)
        }

        styles.fonts.push_back(font);
    }

    // Parse fills
    auto fillsNode = styleSheet.child("fills");
    for (auto fillNode : fillsNode.children("fill")) {
        XLSXFill fill;

        auto patternFill = fillNode.child("patternFill");
        if (patternFill) {
            const char* patternType = patternFill.attribute("patternType").value();
            // Only extract color for solid fills
            if (patternType && std::strcmp(patternType, "solid") == 0) {
                auto fgColorNode = patternFill.child("fgColor");
                if (fgColorNode) {
                    const char* rgb = fgColorNode.attribute("rgb").value();
                    if (rgb && rgb[0] != '\0') {
                        fill.fgColor = argbToRgb(rgb);
                    }
                    // TODO: Support theme colors
                }
            }
        }

        styles.fills.push_back(fill);
    }

    // Parse cellXfs (cell format records)
    auto cellXfsNode = styleSheet.child("cellXfs");
    for (auto xfNode : cellXfsNode.children("xf")) {
        XLSXCellFormat xf;

        xf.fontId = xfNode.attribute("fontId").as_int(0);
        xf.fillId = xfNode.attribute("fillId").as_int(0);
        xf.numFmtId = xfNode.attribute("numFmtId").as_int(0);

        // Check apply* attributes
        xf.applyFont = xfNode.attribute("applyFont").as_bool(false);
        xf.applyFill = xfNode.attribute("applyFill").as_bool(false);
        xf.applyAlignment = xfNode.attribute("applyAlignment").as_bool(false);

        // If fontId > 0 but applyFont is not explicitly set, still apply font
        // Many XLSX files omit applyFont when font should be applied
        if (xf.fontId > 0 && !xf.applyFont) {
            xf.applyFont = true;
        }
        if (xf.fillId > 0 && !xf.applyFill) {
            xf.applyFill = true;
        }

        // Parse alignment
        auto alignmentNode = xfNode.child("alignment");
        if (alignmentNode) {
            xf.applyAlignment = true;
            xf.alignment.horizontal =
                parseHorizontalAlign(alignmentNode.attribute("horizontal").value());
            xf.alignment.vertical = parseVerticalAlign(alignmentNode.attribute("vertical").value());
        }

        styles.cellFormats.push_back(xf);
    }

    return styles;
}

}  // namespace

namespace cells {

// Internal ZIP reader class (needs to be in cells namespace to be used by XLSXReader)
namespace detail {

class ZipReader {
public:
    ZipReader() = default;

    ~ZipReader() {
        if (opened_) {
            mz_zip_reader_end(&archive_);
        }
    }

    bool open(const std::string& path) {
        if (mz_zip_reader_init_file(&archive_, path.c_str(), 0) == 0) {
            return false;
        }
        opened_ = true;
        return true;
    }

    bool openFromMemory(const char* data, size_t size) {
        if (mz_zip_reader_init_mem(&archive_, data, size, 0) == 0) {
            lastError_ = mz_zip_get_last_error(&archive_);
            return false;
        }
        opened_ = true;
        return true;
    }

    [[nodiscard]] mz_zip_error getLastError() const { return lastError_; }

    // Read entire file from archive into string
    std::string readFile(const std::string& name) {
        const int index = mz_zip_reader_locate_file(&archive_, name.c_str(), nullptr, 0);
        if (index < 0) {
            return {};
        }

        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&archive_, index, &stat) == 0) {
            return {};
        }

        std::string content;
        content.resize(stat.m_uncomp_size);

        if (mz_zip_reader_extract_to_mem(&archive_, index, content.data(), content.size(), 0) ==
            0) {
            return {};
        }

        return content;
    }

private:
    mz_zip_archive archive_{};
    bool opened_{false};
    mz_zip_error lastError_{MZ_ZIP_NO_ERROR};
};

}  // namespace detail

// ============================================================================
// XLSXReadError implementation
// ============================================================================

std::string XLSXReadError::toString() const {
    std::ostringstream oss;
    oss << message;
    if (!sheet.empty()) {
        oss << " (sheet: " << sheet;
        if (row > 0 && col > 0) {
            oss << ", row: " << row << ", col: " << col;
        }
        oss << ")";
    }
    return oss.str();
}

// ============================================================================
// XLSXReader implementation
// ============================================================================

XLSXReader::XLSXReader() = default;

XLSXReader::XLSXReader(XLSXReadOptions options) : options_(std::move(options)) {}

void XLSXReader::addWarning(const std::string& msg) {
    warnings_.push_back(msg);
}

void XLSXReader::reset() {
    warnings_.clear();
}

// Static helper to parse XLSX from an already-opened ZipReader
static XLSXReadResult parseXLSXFromZip(detail::ZipReader& zip, const XLSXReadOptions& options,
                                       std::vector<std::string>& warnings) {
    auto totalStart = std::chrono::steady_clock::now();
    XLSXReadResult result;
    auto start = std::chrono::steady_clock::now();

    // Progress tracking
    size_t cellsLoaded = 0;
    size_t totalCellEstimate = 0;
    size_t lastProgressReport = 0;

    // Helper lambda to add warnings
    auto addWarning = [&warnings](const std::string& msg) { warnings.push_back(msg); };

    // Parse workbook relationships to get sheet paths
    start = std::chrono::steady_clock::now();
    std::string relsContent = zip.readFile("xl/_rels/workbook.xml.rels");
    std::unordered_map<std::string, std::string> sheetPaths;  // rId -> path

    if (!relsContent.empty()) {
        pugi::xml_document relsDoc;
        if (relsDoc.load_buffer(relsContent.data(), relsContent.size())) {
            for (auto rel : relsDoc.child("Relationships").children("Relationship")) {
                const char* id = rel.attribute("Id").value();
                const char* target = rel.attribute("Target").value();
                if (id && target) {
                    // Target can be absolute (/xl/worksheets/sheet1.xml) or relative
                    // (worksheets/sheet1.xml)
                    std::string fullPath;
                    if (target[0] == '/') {
                        // Absolute path - strip leading slash
                        fullPath = std::string(target + 1);
                    } else {
                        // Relative path - prepend xl/
                        fullPath = "xl/" + std::string(target);
                    }
                    sheetPaths[id] = fullPath;
                }
            }
        }
    }
    logTiming("parse rels", start);

    // Parse workbook to get sheet names and rIds
    start = std::chrono::steady_clock::now();
    std::string workbookContent = zip.readFile("xl/workbook.xml");
    std::vector<std::pair<std::string, std::string>> sheetInfo;  // name, path

    if (workbookContent.empty()) {
        result.error = XLSXReadError("Failed to read workbook.xml");
        return result;
    }

    pugi::xml_document wbDoc;
    if (!wbDoc.load_buffer(workbookContent.data(), workbookContent.size())) {
        result.error = XLSXReadError("Failed to parse workbook.xml");
        return result;
    }

    for (auto sheet : wbDoc.child("workbook").child("sheets").children("sheet")) {
        const char* name = sheet.attribute("name").value();
        const char* rId = sheet.attribute("r:id").value();
        if (name && rId) {
            auto it = sheetPaths.find(rId);
            if (it != sheetPaths.end()) {
                sheetInfo.emplace_back(name, it->second);
            }
        }
    }

    if (sheetInfo.empty()) {
        result.error = XLSXReadError("No sheets found in workbook");
        return result;
    }
    logTiming("parse workbook", start);

    // Parse shared strings
    start = std::chrono::steady_clock::now();
    std::vector<std::string> sharedStrings;
    std::string ssContent = zip.readFile("xl/sharedStrings.xml");

    if (!ssContent.empty()) {
        pugi::xml_document ssDoc;
        if (ssDoc.load_buffer(ssContent.data(), ssContent.size())) {
            for (auto si : ssDoc.child("sst").children("si")) {
                // Handle both <t> and <r> (rich text) elements
                auto t = si.child("t");
                if (t) {
                    sharedStrings.emplace_back(t.text().get());
                } else {
                    // Rich text: concatenate all <t> elements within <r> elements
                    std::string text;
                    for (auto r : si.children("r")) {
                        auto rt = r.child("t");
                        if (rt) {
                            text += rt.text().get();
                        }
                    }
                    sharedStrings.push_back(text);
                }
            }
        }
    }
    logTiming("parse sharedStrings", start);

    // Parse styles if requested
    start = std::chrono::steady_clock::now();
    XLSXStyles xlsxStyles;
    // Map from CellStyle to our style ID (for deduplication)
    std::unordered_map<std::string, ID> styleToId;

    if (options.readStyles) {
        const std::string stylesContent = zip.readFile("xl/styles.xml");
        if (!stylesContent.empty()) {
            xlsxStyles = parseStylesXml(stylesContent);
        }
    }
    logTiming("parse styles", start);

    // Create workbook
    auto workbook = std::make_unique<Workbook>(generate_id(), "Imported");

    // Process each sheet
    for (const auto& [sheetName, sheetPath] : sheetInfo) {
        // Filter sheets if specific sheet requested
        if (!options.sheetName.empty() && sheetName != options.sheetName) {
            continue;
        }

        start = std::chrono::steady_clock::now();
        std::string sheetContent = zip.readFile(sheetPath);
        if (sheetContent.empty()) {
            addWarning("Failed to read sheet: " + sheetName);
            continue;
        }
        logTiming("read sheet XML", start);

        start = std::chrono::steady_clock::now();
        pugi::xml_document sheetDoc;
        if (!sheetDoc.load_buffer(sheetContent.data(), sheetContent.size())) {
            addWarning("Failed to parse sheet: " + sheetName);
            continue;
        }
        logTiming("parse sheet XML", start);

        // Create our Sheet
        auto sheet = std::make_unique<Sheet>(generate_id(), sheetName);

        // First pass: find dimensions
        start = std::chrono::steady_clock::now();
        int maxRow = 0, maxCol = 0;
        auto sheetData = sheetDoc.child("worksheet").child("sheetData");

        for (auto row : sheetData.children("row")) {
            const int rowNum = row.attribute("r").as_int() - 1;  // 0-indexed
            if (rowNum >= maxRow) {
                maxRow = rowNum + 1;
            }

            for (auto cell : row.children("c")) {
                int col = 0, r = 0;
                parseCellRef(cell.attribute("r").value(), col, r);
                if (col >= maxCol) {
                    maxCol = col + 1;
                }
            }
        }
        logTiming("find dimensions", start);

        // Create columns and rows
        start = std::chrono::steady_clock::now();
        std::vector<ID> columnIds;
        std::vector<ID> rowIds;
        columnIds.reserve(maxCol);
        rowIds.reserve(maxRow);

        for (int c = 0; c < maxCol; ++c) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = static_cast<uint32_t>(c);
            col->size = DEFAULT_COLUMN_WIDTH;
            columnIds.push_back(col->id);
            sheet->addColumn(std::move(col));
        }

        for (int r = 0; r < maxRow; ++r) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = static_cast<uint32_t>(r);
            row->size = DEFAULT_ROW_HEIGHT;
            rowIds.push_back(row->id);
            sheet->addRow(std::move(row));
        }
        logTiming("create rows/cols", start);

        // Second pass: create cells
        start = std::chrono::steady_clock::now();
        int cellCount = 0;

        // Count cells for reservation
        for (auto row : sheetData.children("row")) {
            for ([[maybe_unused]] auto c : row.children("c")) {
                cellCount++;
            }
        }
        sheet->reserveCells(cellCount);
        totalCellEstimate += cellCount;

        // Track shared formulas: si index -> master cell
        std::unordered_map<int, Cell*> sharedFormulaMasters;
        // Track subscribers that need to be linked: si index -> list of subscriber cells
        std::unordered_map<int, std::vector<Cell*>> sharedFormulaSubscribers;

        // Progress reporting helper
        auto reportProgress = [&]() {
            if (options.progressCallback &&
                cellsLoaded - lastProgressReport >= options.progressInterval) {
                options.progressCallback(cellsLoaded, totalCellEstimate);
                lastProgressReport = cellsLoaded;
            }
        };

        // Style application helper - gets or creates a style ID for an XLSX style index
        auto getOrCreateStyleId = [&](int xlsxStyleIndex) -> ID {
            if (!options.readStyles || xlsxStyleIndex <= 0) {
                return {};  // Null ID - no style or default style
            }

            CellStyle cellStyle;
            if (!xlsxStyles.getCellStyle(xlsxStyleIndex, cellStyle)) {
                return {};  // Failed to convert style
            }

            // Check if this style already exists (deduplication)
            const std::string styleKey = cellStyleToKey(cellStyle);
            auto it = styleToId.find(styleKey);
            if (it != styleToId.end()) {
                return it->second;
            }

            // Register new style
            const ID styleId = generate_id();
            workbook->registerStyle(styleId, cellStyle);
            styleToId[styleKey] = styleId;
            return styleId;
        };

        for (auto row : sheetData.children("row")) {
            for (auto cellNode : row.children("c")) {
                int col = 0, rowNum = 0;
                parseCellRef(cellNode.attribute("r").value(), col, rowNum);

                if (col < 0 || col >= maxCol || rowNum < 0 || rowNum >= maxRow) {
                    continue;
                }

                // Get value
                std::string value;
                const char* type = cellNode.attribute("t").value();
                auto vNode = cellNode.child("v");

                if (vNode) {
                    const char* rawValue = vNode.text().get();

                    if (type && type[0] == 's') {
                        // Shared string
                        const int idx = std::atoi(rawValue);
                        if (idx >= 0 && idx < static_cast<int>(sharedStrings.size())) {
                            value = sharedStrings[idx];
                        }
                    } else {
                        value = rawValue;
                    }
                } else if (type && std::strcmp(type, "inlineStr") == 0) {
                    // Inline string: <c t="inlineStr"><is><t>text</t></is></c>
                    auto isNode = cellNode.child("is");
                    if (isNode) {
                        auto tNode = isNode.child("t");
                        if (tNode) {
                            value = tNode.text().get();
                        } else {
                            // Rich text: concatenate all <t> elements within <r> elements
                            for (auto rNode : isNode.children("r")) {
                                auto rtNode = rNode.child("t");
                                if (rtNode) {
                                    value += rtNode.text().get();
                                }
                            }
                        }
                    }
                }

                // Skip empty cells
                if (value.empty() && !cellNode.child("f")) {
                    continue;
                }

                // Create cell
                auto cell = std::make_unique<Cell>(generate_id(), columnIds[col], rowIds[rowNum]);

                // Apply style if present
                const int styleIndex = cellNode.attribute("s").as_int(0);
                const ID styleId = getOrCreateStyleId(styleIndex);
                if (!styleId.isNull()) {
                    cell->styleId = styleId;
                }

                // Parse value based on type (type was read earlier)
                const int cellType = mapCellType(type);

                switch (cellType) {
                    case 2:  // Number
                        if (!value.empty()) {
                            // Use strtod instead of std::stod to avoid exceptions
                            cell->value = CellValue(strtod(value.c_str(), nullptr));
                        }
                        break;
                    case 3:  // Boolean
                        cell->value = CellValue(value == "1" || value == "true");
                        break;
                    case 4:  // Error
                        cell->value = CellValue(stringToError(value));
                        break;
                    default:  // String
                        cell->value = CellValue(value);
                        break;
                }

                // Read formula if present and requested
                if (options.readFormulas) {
                    auto fNode = cellNode.child("f");
                    if (fNode) {
                        const char* formulaType = fNode.attribute("t").value();
                        const bool isShared =
                            (formulaType != nullptr) && std::strcmp(formulaType, "shared") == 0;

                        if (isShared) {
                            const int si = fNode.attribute("si").as_int(-1);
                            const char* ref = fNode.attribute("ref").value();
                            const char* formulaText = fNode.text().get();

                            if (si >= 0) {
                                // Master cell has ref attribute and formula text
                                if (ref && ref[0] != '\0' && formulaText &&
                                    formulaText[0] != '\0') {
                                    if (options.readFormulaText) {
                                        // Parse formula text to create AST
                                        const std::string fullFormula =
                                            "=" + std::string(formulaText);
                                        cells::FormulaParser parser(fullFormula);
                                        std::unique_ptr<cells::ASTNode> ast = parser.parse();
                                        auto* formula = new cells::Formula();
                                        formula->ast = ast.release();
                                        formula->dirty = true;
                                        cell->setFormula(formula);
                                    } else {
                                        // Empty formula placeholder
                                        auto* formula = new cells::Formula();
                                        formula->dirty = true;
                                        cell->setFormula(formula);
                                    }
                                    Cell* rawPtr = cell.get();
                                    sheet->addCell(std::move(cell));
                                    sharedFormulaMasters[si] = rawPtr;
                                    cellsLoaded++;
                                    reportProgress();
                                    continue;
                                }
                                // Subscriber cell has only si attribute (rawPtr used for
                                // setSharedFormulaRef)
                                Cell* rawPtr = cell.get();  // NOLINT(misc-const-correctness)
                                sheet->addCell(std::move(cell));
                                sharedFormulaSubscribers[si].push_back(rawPtr);
                                cellsLoaded++;
                                reportProgress();
                                continue;
                            }
                        }

                        // Regular formula (not shared)
                        if (options.readFormulaText) {
                            const std::string formulaTextStr = fNode.text().get();
                            const std::string fullFormula = "=" + formulaTextStr;
                            cells::FormulaParser parser(fullFormula);
                            std::unique_ptr<cells::ASTNode> ast = parser.parse();
                            auto* formula = new cells::Formula();
                            formula->ast = ast.release();
                            formula->dirty = true;
                            cell->setFormula(formula);
                        } else {
                            // Empty formula placeholder
                            auto* formula = new cells::Formula();
                            formula->dirty = true;
                            cell->setFormula(formula);
                        }
                    }
                }

                sheet->addCell(std::move(cell));
                cellsLoaded++;
                reportProgress();
            }
        }

        // Link shared formula subscribers to their masters using Sheet-level tracking
        for (const auto& [si, subscribers] : sharedFormulaSubscribers) {
            auto masterIt = sharedFormulaMasters.find(si);
            if (masterIt != sharedFormulaMasters.end()) {
                const Cell* master = masterIt->second;

                // Collect subscriber IDs
                std::vector<ID> subscriberIds;
                subscriberIds.reserve(subscribers.size());
                for (Cell* subscriber : subscribers) {
                    subscriberIds.push_back(subscriber->id);
                    // Mark cell as a shared formula subscriber
                    subscriber->setSharedFormulaSubscriber(true);
                }

                // Register the shared formula group at Sheet level
                sheet->registerSharedFormulaGroup(master->id, subscriberIds);
            } else {
                // Master not found - add warning and leave subscriber without formula
                addWarning("Shared formula master not found for si=" + std::to_string(si));
            }
        }
        logTiming("create cells", start);

        workbook->addSheet(std::move(sheet));
    }

    // Check if requested sheet was found
    if (!options.sheetName.empty() && workbook->sheets.empty()) {
        result.error = XLSXReadError("Sheet \"" + options.sheetName + "\" not found");
        return result;
    }

    // Final progress report (100% complete)
    if (options.progressCallback && cellsLoaded > lastProgressReport) {
        options.progressCallback(cellsLoaded, cellsLoaded);
    }

    logTiming("TOTAL", totalStart);

    result.workbook = std::move(workbook);
    result.warnings = std::move(warnings);
    return result;
}

// ============================================================================
// XLSXReader public methods
// ============================================================================

XLSXReadResult XLSXReader::readFile(const std::string& path) {
    reset();
    detail::ZipReader zip;
    if (!zip.open(path)) {
        XLSXReadResult result;
        result.error = XLSXReadError("Failed to open XLSX file: " + path);
        return result;
    }
    return parseXLSXFromZip(zip, options_, warnings_);
}

XLSXReadResult XLSXReader::readFromMemory(const char* data, size_t size) {
    reset();
    detail::ZipReader zip;
    if (!zip.openFromMemory(data, size)) {
        XLSXReadResult result;
        std::string errorMsg = "Failed to read XLSX data from memory";
        errorMsg += " (size=" + std::to_string(size) + ", miniz error: ";
        errorMsg += mz_zip_get_error_string(zip.getLastError());
        errorMsg += ")";
        result.error = XLSXReadError(errorMsg);
        return result;
    }
    return parseXLSXFromZip(zip, options_, warnings_);
}

// ============================================================================
// Convenience functions
// ============================================================================

XLSXReadResult readXLSX(const std::string& path) {
    XLSXReader reader;
    return reader.readFile(path);
}

XLSXReadResult readXLSX(const std::string& path, const XLSXReadOptions& options) {
    XLSXReader reader(options);
    return reader.readFile(path);
}

XLSXReadResult readXLSXFromMemory(const char* data, size_t size) {
    XLSXReader reader;
    return reader.readFromMemory(data, size);
}

XLSXReadResult readXLSXFromMemory(const char* data, size_t size, const XLSXReadOptions& options) {
    XLSXReader reader(options);
    return reader.readFromMemory(data, size);
}

}  // namespace cells
