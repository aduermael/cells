#include "core/cells/xlsx_reader.h"

#include <cstdlib>
#include <cstring>

#include <chrono>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

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
    if (type == nullptr || *type == '\0')
        return 2;  // Default is number

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
        default:
            return 2;  // Default to number
    }
}

// ZIP file reading using miniz
class ZipReader {
public:
    ZipReader() = default;

    ~ZipReader() {
        if (opened_) {
            mz_zip_reader_end(&archive_);
        }
    }

    bool open(const std::string& path) {
        if (!mz_zip_reader_init_file(&archive_, path.c_str(), 0)) {
            return false;
        }
        opened_ = true;
        return true;
    }

    // Read entire file into string
    std::string readFile(const std::string& name) {
        int index = mz_zip_reader_locate_file(&archive_, name.c_str(), nullptr, 0);
        if (index < 0) {
            return {};
        }

        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&archive_, index, &stat)) {
            return {};
        }

        std::string content;
        content.resize(stat.m_uncomp_size);

        if (!mz_zip_reader_extract_to_mem(&archive_, index, content.data(), content.size(), 0)) {
            return {};
        }

        return content;
    }

private:
    mz_zip_archive archive_{};
    bool opened_{false};
};

}  // namespace

namespace cells {

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

XLSXReadResult XLSXReader::readFile(const std::string& path) {
    auto totalStart = std::chrono::steady_clock::now();
    reset();
    XLSXReadResult result;

    // Open ZIP archive
    auto start = std::chrono::steady_clock::now();
    ZipReader zip;
    if (!zip.open(path)) {
        result.error = XLSXReadError("Failed to open XLSX file: " + path);
        return result;
    }
    logTiming("zip.open", start);

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
                    // Target is relative to xl/, e.g., "worksheets/sheet1.xml"
                    std::string fullPath = "xl/" + std::string(target);
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
                    sharedStrings.push_back(t.text().get());
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

    // Create workbook
    auto workbook = std::make_unique<Workbook>(generate_sequential_id(), "Imported");

    // Process each sheet
    for (const auto& [sheetName, sheetPath] : sheetInfo) {
        // Filter sheets if specific sheet requested
        if (!options_.sheetName.empty() && sheetName != options_.sheetName) {
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
        auto sheet = std::make_unique<Sheet>(generate_sequential_id(), sheetName);

        // First pass: find dimensions
        start = std::chrono::steady_clock::now();
        int maxRow = 0, maxCol = 0;
        auto sheetData = sheetDoc.child("worksheet").child("sheetData");

        for (auto row : sheetData.children("row")) {
            int rowNum = row.attribute("r").as_int() - 1;  // 0-indexed
            if (rowNum >= maxRow)
                maxRow = rowNum + 1;

            for (auto cell : row.children("c")) {
                int col, r;
                parseCellRef(cell.attribute("r").value(), col, r);
                if (col >= maxCol)
                    maxCol = col + 1;
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
            auto col = std::make_unique<Axis>(generate_sequential_id(), true);
            col->position = static_cast<uint32_t>(c);
            col->size = 64;
            columnIds.push_back(col->id);
            sheet->addColumn(std::move(col));
        }

        for (int r = 0; r < maxRow; ++r) {
            auto row = std::make_unique<Axis>(generate_sequential_id(), false);
            row->position = static_cast<uint32_t>(r);
            row->size = 20;
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

        for (auto row : sheetData.children("row")) {
            for (auto cellNode : row.children("c")) {
                int col, rowNum;
                parseCellRef(cellNode.attribute("r").value(), col, rowNum);

                if (col < 0 || col >= maxCol || rowNum < 0 || rowNum >= maxRow) {
                    continue;
                }

                // Get value
                std::string value;
                auto vNode = cellNode.child("v");
                if (vNode) {
                    const char* type = cellNode.attribute("t").value();
                    const char* rawValue = vNode.text().get();

                    if (type && type[0] == 's') {
                        // Shared string
                        int idx = std::atoi(rawValue);
                        if (idx >= 0 && idx < static_cast<int>(sharedStrings.size())) {
                            value = sharedStrings[idx];
                        }
                    } else {
                        value = rawValue;
                    }
                }

                // Skip empty cells
                if (value.empty() && !cellNode.child("f")) {
                    continue;
                }

                // Create cell
                auto cell = std::make_unique<Cell>(generate_sequential_id(), columnIds[col],
                                                   rowIds[rowNum]);

                // Parse value based on type
                const char* type = cellNode.attribute("t").value();
                int cellType = mapCellType(type);

                switch (cellType) {
                    case 2:  // Number
                        if (!value.empty()) {
                            cell->value = CellValue(std::stod(value));
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
                if (options_.readFormulas) {
                    auto fNode = cellNode.child("f");
                    if (fNode) {
                        if (options_.readFormulaText) {
                            std::string formulaText = fNode.text().get();
                            cell->setFormula(new Formula(("=" + formulaText).c_str()));
                        } else {
                            cell->setFormula(new Formula("="));
                        }
                    }
                }

                sheet->addCell(std::move(cell));
            }
        }
        logTiming("create cells", start);

        workbook->addSheet(std::move(sheet));
    }

    // Check if requested sheet was found
    if (!options_.sheetName.empty() && workbook->sheets.empty()) {
        result.error = XLSXReadError("Sheet \"" + options_.sheetName + "\" not found");
        return result;
    }

    logTiming("TOTAL", totalStart);

    result.workbook = std::move(workbook);
    result.warnings = std::move(warnings_);
    return result;
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

}  // namespace cells
