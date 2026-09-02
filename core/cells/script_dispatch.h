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

// Map a language label from the web script panel (or WASM) to a ScriptKind.
// Empty / "luau" / "lua" → Luau (default). "javascript" / "js" / "officejs" → JavaScript.
[[nodiscard]] ScriptKind scriptKindFromLanguage(const std::string& language);

// Toggle override plus CLI-style content detection: explicit JS wins; otherwise
// Excel.run / Office.onReady in source still selects the JavaScript host so a
// Luau-default panel does not Luau-compile Office.js.
[[nodiscard]] ScriptKind resolveScriptKind(const std::string& language, const std::string& source);

// Run source in the matching sandbox against workbook/sheet.
[[nodiscard]] ScriptResult executeUserScript(Workbook& workbook, Sheet* sheet,
                                             const std::string& source, ScriptKind kind);
[[nodiscard]] ScriptResult executeUserScript(Workbook& workbook, Sheet* sheet,
                                             const std::string& source,
                                             const std::string& language);

}  // namespace cells

#endif  // CELLS_SCRIPT_DISPATCH_H_
