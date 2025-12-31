#include "core/cells/xlsx_writer.h"

#include <cstring>

#include <algorithm>
#include <sstream>
#include <unordered_map>

#include "core/cells/formula_serializer.h"
#include "core/cells/ref_converter.h"

#include "miniz.h"

namespace {

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

// Generate xl/workbook.xml
std::string generateWorkbook(const std::vector<std::string>& sheetNames) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
           "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">\n";
    xml << "  <sheets>\n";
    for (size_t i = 0; i < sheetNames.size(); ++i) {
        xml << "    <sheet name=\"" << sheetNames[i] << "\" sheetId=\"" << (i + 1)
            << "\" r:id=\"rId" << (i + 1) << "\"/>\n";
    }
    xml << "  </sheets>\n";
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
std::string generateWorksheet(const cells::Sheet& sheet, SharedStringTable& sst,
                              const cells::RefConverter& refConverter, bool writeFormulas) {
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
        // Map subscribers to their master's si
        for (const auto& pair : sheet.cells) {
            const cells::Cell* cell = pair.second.get();
            if (cell->isSharedFormula() && cell->sharedFormulaRef != nullptr) {
                auto masterIt = masterToSi.find(cell->sharedFormulaRef);
                if (masterIt != masterToSi.end()) {
                    subscriberToSi[cell] = masterIt->second;
                }
            }
        }
    }

    // Write dimension
    if (!columns.empty() && !rows.empty()) {
        xml << "  <dimension ref=\"A1:" << colIndexToLetter(columns.size() - 1) << rows.size()
            << "\"/>\n";
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

// Generate xl/styles.xml (minimal required styles)
std::string generateStyles() {
    // Minimal styles.xml required by Excel to open the file
    return R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <fonts count="1">
    <font>
      <sz val="11"/>
      <name val="Calibri"/>
    </font>
  </fonts>
  <fills count="2">
    <fill><patternFill patternType="none"/></fill>
    <fill><patternFill patternType="gray125"/></fill>
  </fills>
  <borders count="1">
    <border><left/><right/><top/><bottom/><diagonal/></border>
  </borders>
  <cellStyleXfs count="1">
    <xf numFmtId="0" fontId="0" fillId="0" borderId="0"/>
  </cellStyleXfs>
  <cellXfs count="1">
    <xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>
  </cellXfs>
</styleSheet>)";
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

    // Collect sheet names
    std::vector<std::string> sheetNames;
    sheetNames.reserve(workbook.sheets.size());
    for (const auto& sheet : workbook.sheets) {
        sheetNames.push_back(sheet->name);
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
    if (!zip.addFile("xl/workbook.xml", generateWorkbook(sheetNames))) {
        result.error = XLSXWriteError("Failed to write xl/workbook.xml");
        return result;
    }

    if (!zip.addFile("xl/_rels/workbook.xml.rels", generateWorkbookRels(workbook.sheets.size()))) {
        result.error = XLSXWriteError("Failed to write xl/_rels/workbook.xml.rels");
        return result;
    }

    // Write styles
    if (!zip.addFile("xl/styles.xml", generateStyles())) {
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
            generateWorksheet(sheet, sst, refConverter, options_.writeFormulas);

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
