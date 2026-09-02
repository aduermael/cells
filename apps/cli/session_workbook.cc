#include "session_workbook.h"

#include <cctype>

#include "converter.h"
#include "options.h"

#include "core/cells/script_dispatch.h"

namespace cells::cli {

bool SessionWorkbook::load(const std::string& path, std::string& error_out) {
    workbook_ = loadWorkbookFromFile(path, error_out);
    if (!workbook_) {
        return false;
    }
    source_path_ = path;
    return true;
}

ScriptResult SessionWorkbook::exec(const std::string& code) {
    ScriptResult result;
    if (!workbook_) {
        result.success = false;
        result.error = "no workbook loaded";
        return result;
    }
    if (workbook_->sheets.empty()) {
        result.success = false;
        result.error = "no sheet available";
        return result;
    }
    Sheet* sheet = workbook_->getSheetByIndex(0);
    const ScriptKind kind = detectScriptKind("", code);
    return executeUserScript(*workbook_, sheet, code, kind);
}

bool SessionWorkbook::export_to(const std::string& path, std::string format,
                                std::string& error_out) {
    if (!workbook_) {
        error_out = "no workbook";
        return false;
    }
    std::string out_path = path;
    if (!format.empty()) {
        for (char& c : format) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        // If the caller passed a format override without a matching extension,
        // still honor the path; saveWorkbookToFile keys off the extension.
        Format detected = detect_format(out_path);
        Format wanted = Format::kUnknown;
        if (format == "zcd") {
            wanted = Format::kZcd;
        } else if (format == "csv" || format == "tsv") {
            wanted = Format::kCsv;
        } else if (format == "xlsx") {
            wanted = Format::kXlsx;
        }
        if (wanted != Format::kUnknown && detected != wanted) {
            error_out = "export format does not match path extension";
            return false;
        }
    }
    return saveWorkbookToFile(*workbook_, out_path, error_out);
}

std::uint64_t SessionWorkbook::cell_count() const {
    if (!workbook_) {
        return 0;
    }
    std::uint64_t n = 0;
    for (const auto& sheet : workbook_->sheets) {
        if (sheet) {
            n += sheet->cellCount();
        }
    }
    return n;
}

}  // namespace cells::cli
