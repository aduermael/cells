// Language detection and dispatch for CLI/session script execution.
// Luau remains the default; JavaScript is selected by extension or Office.js
// content (Excel.run / Office.onReady / Office.initialize).

#ifndef CELLS_SCRIPT_DISPATCH_H_
#define CELLS_SCRIPT_DISPATCH_H_

#include <cstdint>

#include <string>

#include "core/cells/luau_sandbox.h"

namespace cells {

struct Workbook;
struct Sheet;

enum class ScriptKind : std::uint8_t { Luau, JavaScript };

[[nodiscard]] ScriptKind detectScriptKind(const std::string& path, const std::string& source);

// Run source in the matching sandbox against workbook/sheet.
[[nodiscard]] ScriptResult executeUserScript(Workbook& workbook, Sheet* sheet,
                                             const std::string& source, ScriptKind kind);

}  // namespace cells

#endif  // CELLS_SCRIPT_DISPATCH_H_
