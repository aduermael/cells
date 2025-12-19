#include "core/cells/xlsx_writer.h"

#include <sstream>

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

XLSXWriteResult XLSXWriter::writeFile(const Workbook& /*workbook*/, const std::string& /*path*/) {
    reset();
    XLSXWriteResult result;
    // TODO: Implement native XLSX writing with miniz + pugixml
    result.error = XLSXWriteError("XLSX writing not yet implemented in native mode");
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
