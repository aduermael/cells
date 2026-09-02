// Local (file-backed) session workbook: load once, exec scripts, export.
// Independent of collab URL / SyncClient. Used by the session daemon and tests.

#ifndef APPS_CLI_SESSION_WORKBOOK_H_
#define APPS_CLI_SESSION_WORKBOOK_H_

#include <cstdint>
#include <memory>
#include <string>

#include "core/cells/luau_sandbox.h"
#include "core/cells/model.h"

namespace cells::cli {

class SessionWorkbook {
public:
    // Load path (xlsx/csv/tsv/zcd) or create an empty workbook when path is empty.
    bool load(const std::string& path, std::string& error_out);

    // Run a Luau or Office.js script against the loaded workbook.
    ScriptResult exec(const std::string& code);

    // Export to path. format empty → detect from extension (zcd|xlsx|csv).
    bool export_to(const std::string& path, std::string format, std::string& error_out);

    Workbook* workbook() { return workbook_.get(); }
    [[nodiscard]] const Workbook* workbook() const { return workbook_.get(); }
    std::unique_ptr<Workbook> release() { return std::move(workbook_); }

    [[nodiscard]] std::uint64_t cell_count() const;
    [[nodiscard]] const std::string& source_path() const { return source_path_; }

private:
    std::unique_ptr<Workbook> workbook_;
    std::string source_path_;
};

}  // namespace cells::cli

#endif  // APPS_CLI_SESSION_WORKBOOK_H_
