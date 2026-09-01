// =============================================================================
// UI mutation gateway (Luau-only)
// =============================================================================
//
// Local UI workbook writes MUST go through LuauSandbox::execute.
// There is no skip-sandbox helper on this API.
//
// Formula evaluation stays native C++ (FunctionRegistry / AST eval).
// Remote CRDT apply and file load/import are not UI and do not use this.
//
// =============================================================================

#ifndef CELLS_UI_MUTATION_H_
#define CELLS_UI_MUTATION_H_

#include <string>

#include "core/cells/crdt.h"
#include "core/cells/id.h"
#include "core/cells/luau_sandbox.h"

namespace cells {

struct Sheet;
struct Workbook;
struct Operation;

// A1 reference from 0-based column/row positions (A1, B12, AA1, ...).
std::string a1FromPosition(uint32_t col, uint32_t row);

// Execute `script` in the sandbox with workbook/sheet context set.
// This is the only UI mutation entry point.
[[nodiscard]] ScriptResult executeUiMutation(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                                             const std::string& script);

struct UiCellWriteResult {
    bool success{false};
    std::string error;
    ID cellId;
    std::string formatBase64;
};

// Set a cell via Luau `setCell` (creates axes/cell as needed).
// detectFormat: parse user input (%, $, dates) and apply number format via `setFormat`.
[[nodiscard]] UiCellWriteResult uiWriteCell(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                                            uint32_t col, uint32_t row, const std::string& value,
                                            bool detectFormat);

// Same as uiWriteCell, addressing an existing cell by id (looks up A1, then Luau).
[[nodiscard]] UiCellWriteResult uiWriteCellById(LuauSandbox& sandbox, Workbook& workbook,
                                                Sheet& sheet, const ID& cellId,
                                                const std::string& value, bool detectFormat);

// Ensure a cell exists via Luau `getCell(ref, {create=true})`.
[[nodiscard]] UiCellWriteResult uiEnsureCell(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                                             uint32_t col, uint32_t row);

// Delete a cell via Luau `setCell(ref, nil)`.
[[nodiscard]] bool uiDeleteCell(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                                const ID& cellId, std::string* error);

// Apply a CRDT op by executing Luau `_applyUiOp()` (still sandbox execution).
[[nodiscard]] ApplyResult uiApplyOperation(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                                           const Operation& op);

// View / workbook-level UI writes (always Luau execute).
[[nodiscard]] ScriptResult uiFreezePanes(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                                         int freezeCol, int freezeRow);
[[nodiscard]] ScriptResult uiSetDocumentTitle(LuauSandbox& sandbox, Workbook& workbook,
                                              Sheet& sheet, const std::string& title);
[[nodiscard]] ScriptResult uiMoveSheet(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                                       int fromIndex, int toIndex);
[[nodiscard]] ScriptResult uiSetTheme(LuauSandbox& sandbox, Workbook& workbook, Sheet& sheet,
                                      const std::string& themeJson);

}  // namespace cells

#endif  // CELLS_UI_MUTATION_H_
