// =============================================================================
// Luau API Functions
// =============================================================================
//
// Implements the Cells API functions exposed to Luau scripts. These functions
// allow scripts to manipulate workbook data through the CRDT layer.
//
// API functions provided:
// - getCell(ref, opts): Get cell object by A1 reference
// - setCell(ref, value): Set cell value (number, string, formula, boolean)
// - getSheet(index|name|opts): Get sheet object
// - addSheet(name?): Create new sheet
// - selectSheet(sheet|name|index): Change active sheet context
// - setColumnWidth(col, opts): Resize column
// - setRowHeight(row, opts): Resize row
// - moveColumn(col, opts): Move column to new position
// - deleteRange(opts): Clear cells in range
// - fillRange(opts): Fill range with pattern from source
// - print(...): Output to script console
//
// Dependencies: luau_sandbox.h, model.h, crdt.h, formula_*.h
// Used by: luau_sandbox.cc (registerCellsAPI)
//
// =============================================================================

#include <cstring>

#include "core/cells/crdt.h"
#include "core/cells/dependency_graph.h"
#include "core/cells/fill_range.h"
#include "core/cells/format_buffer.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/luau_sandbox.h"
#include "core/cells/model.h"
#include "core/cells/number_format.h"
#include "core/cells/ref_converter.h"
#include "core/cells/style_buffer.h"

#include "lua.h"     // NOLINT(build/include_subdir)
#include "lualib.h"  // NOLINT(build/include_subdir)

namespace cells {

// ============================================================================
// Helper functions (shared between API implementations)
// ============================================================================

// Ensure column/row exist at a grid position via CRDT ops (not local-only
// getOrCreate). Local-only axis minting makes CELL_SET unapplyable on peers
// (INVALID_TARGET: unknown col/row ids) — the CLI-writes-invisible-in-browser bug.
inline Axis* ensureColumnViaCrdt(Workbook& workbook, Sheet& sheet, uint32_t position) {
    Axis* existing = sheet.getColumnByPosition(position);
    if (existing != nullptr) {
        return existing;
    }
    const ID colId = generate_id();
    const std::string payload = "{\"pos\":" + std::to_string(position) + "}";
    const Operation op = makeColSetOp(workbook, colId, sheet.id, payload);
    applyOperation(workbook, op);
    return sheet.getColumn(colId);
}

inline Axis* ensureRowViaCrdt(Workbook& workbook, Sheet& sheet, uint32_t position) {
    Axis* existing = sheet.getRowByPosition(position);
    if (existing != nullptr) {
        return existing;
    }
    const ID rowId = generate_id();
    const std::string payload = "{\"pos\":" + std::to_string(position) + "}";
    const Operation op = makeRowSetOp(workbook, rowId, sheet.id, payload);
    applyOperation(workbook, op);
    return sheet.getRow(rowId);
}



// Helper: Escape a string for JSON
static std::string jsonEscape(const std::string& str) {
    std::string result;
    result.reserve(str.size() + 16);
    for (const char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
        }
    }
    return result;
}

// Helper: Convert a legacy format ID (e.g., "FMT_C002", "FMT_P002", "CUSD_002") to FormatBuffer
// Returns empty optional if not a recognized legacy format ID
static std::optional<FormatBuffer> formatIdToBuffer(const std::string& formatIdStr) {
    // Try to parse as a legacy format ID using parseFormatId (handles FMT_P, FMT_N, CXXX patterns)
    ParsedFormatId parsed = parseFormatId(formatIdStr);

    // Also handle FMT_C0XX pattern (currency with default USD, XX decimals)
    // These are used in the Luau API documentation but not parsed by parseFormatId
    if (!parsed.valid && formatIdStr.size() == 8 && formatIdStr.substr(0, 5) == "FMT_C" &&
        formatIdStr[5] == '0') {
        if (std::isdigit(static_cast<unsigned char>(formatIdStr[6])) != 0 &&
            std::isdigit(static_cast<unsigned char>(formatIdStr[7])) != 0) {
            const int decimals = (formatIdStr[6] - '0') * 10 + (formatIdStr[7] - '0');
            if (decimals <= 15) {
                parsed.category = NumberFormatCategory::CURRENCY;
                parsed.decimalPlaces = static_cast<uint8_t>(decimals);
                parsed.useThousandsSeparator = true;
                parsed.currencyCode = "USD";
                parsed.currencySymbol = "$";
                parsed.valid = true;
            }
        }
    }

    if (!parsed.valid) {
        return std::nullopt;
    }

    // Create FormatBuffer from parsed components
    FormatBuffer format;
    format.setCategory(parsed.category);
    if (parsed.decimalPlaces > 0) {
        format.setDecimals(parsed.decimalPlaces);
    }
    if (parsed.useThousandsSeparator) {
        format.setThousandsSeparator(true);
    }
    if (!parsed.currencySymbol.empty()) {
        format.setCurrencySymbol(parsed.currencySymbol);
    }
    return format;
}

// Helper: Parse column letter (A, B, ..., AA, ...) to 0-based index
static int parseColumnLetter(const char* ref, size_t* endPos) {
    int col = 0;
    size_t pos = 0;

    // Skip leading $
    if (ref[pos] == '$') {
        pos++;
    }

    while (ref[pos] != '\0' &&
           ((ref[pos] >= 'A' && ref[pos] <= 'Z') || (ref[pos] >= 'a' && ref[pos] <= 'z'))) {
        const char c = ref[pos];
        const int digit = (c >= 'A' && c <= 'Z') ? (c - 'A') : (c - 'a');
        col = col * 26 + digit + 1;
        pos++;
    }

    if (endPos != nullptr) {
        *endPos = pos;
    }
    return col - 1;  // Convert to 0-based
}

// Helper: Parse an A1 reference string
// Returns true if valid, sets colIdx and rowIdx (0-based)
static bool parseA1Ref(const char* ref, int* colIdx, int* rowIdx) {
    if (ref == nullptr || ref[0] == '\0') {
        return false;
    }

    size_t pos = 0;

    // Skip leading $
    if (ref[pos] == '$') {
        pos++;
    }

    // Parse column letters
    int col = 0;
    while (ref[pos] != '\0' &&
           ((ref[pos] >= 'A' && ref[pos] <= 'Z') || (ref[pos] >= 'a' && ref[pos] <= 'z'))) {
        const char c = ref[pos];
        const int digit = (c >= 'A' && c <= 'Z') ? (c - 'A') : (c - 'a');
        col = col * 26 + digit + 1;
        pos++;
    }

    if (col == 0) {
        return false;  // No column letter found
    }

    // Skip $ before row
    if (ref[pos] == '$') {
        pos++;
    }

    // Parse row number (1-based in A1 notation)
    int row = 0;
    while (ref[pos] >= '0' && ref[pos] <= '9') {
        row = row * 10 + (ref[pos] - '0');
        pos++;
    }

    if (row == 0) {
        return false;  // No row number found
    }

    *colIdx = col - 1;  // Convert to 0-based
    *rowIdx = row - 1;  // Convert to 0-based
    return true;
}

// Helper: Parse an A1 range string like "A1:B2" or "A1" (single cell)
// Returns true if valid, sets fromCol, fromRow, toCol, toRow (all 0-based)
static bool parseA1Range(const char* range, int* fromCol, int* fromRow, int* toCol, int* toRow) {
    if (range == nullptr || range[0] == '\0') {
        return false;
    }

    // Find the colon separator
    const char* colon = strchr(range, ':');

    if (colon == nullptr) {
        // Single cell reference (e.g., "A1")
        if (!parseA1Ref(range, fromCol, fromRow)) {
            return false;
        }
        *toCol = *fromCol;
        *toRow = *fromRow;
        return true;
    }

    // Range reference (e.g., "A1:B2")
    // Parse the first part
    const std::string firstPart(range, colon - range);
    if (!parseA1Ref(firstPart.c_str(), fromCol, fromRow)) {
        return false;
    }

    // Parse the second part (after the colon)
    if (!parseA1Ref(colon + 1, toCol, toRow)) {
        return false;
    }

    return true;
}

// ============================================================================
// Cells API: getCell(ref, options?)
// Returns cell object or nil if empty
// ============================================================================
int LuauSandbox::luaCellGet(lua_State* L) {
    // Get the ref argument
    const char* ref = luaL_checkstring(L, 1);

    // Parse options (optional second argument)
    bool createIfEmpty = false;
    if (lua_istable(L, 2) != 0) {
        lua_getfield(L, 2, "create");
        if (lua_isboolean(L, -1) != 0) {
            createIfEmpty = lua_toboolean(L, -1) != 0;
        }
        lua_pop(L, 1);
    }

    // Get context
    Sheet* sheet = getSheet(L);
    if (sheet == nullptr || getWorkbook(L) == nullptr) {
        luaL_error(L, "getCell: no context set");
    }

    // Parse A1 reference
    int colIdx = 0;
    int rowIdx = 0;
    if (!parseA1Ref(ref, &colIdx, &rowIdx)) {
        luaL_error(L, "getCell: invalid reference '%s'", ref);
    }

    // Get or create the column and row
    // NOLINTBEGIN(misc-const-correctness)
    Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(colIdx));
    Axis* row = sheet->getRowByPosition(static_cast<uint32_t>(rowIdx));

    // If column or row doesn't exist and we're not creating, return nil
    if ((col == nullptr || row == nullptr) && !createIfEmpty) {
        lua_pushnil(L);
        return 1;
    }

    // Create column/row if needed
    if (col == nullptr) {
        col = sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(colIdx));
    }
    if (row == nullptr) {
        row = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(rowIdx));
    }

    // Get the cell
    Cell* cell = sheet->getCellAt(col->id, row->id);

    // If cell doesn't exist and we're not creating, return nil
    if (cell == nullptr && !createIfEmpty) {
        lua_pushnil(L);
        return 1;
    }

    // Create cell if needed
    if (cell == nullptr) {
        cell = sheet->getOrCreateCellAt(col->id, row->id);
    }
    // NOLINTEND(misc-const-correctness)

    // Get sandbox to push cell object
    const LuauSandbox* sandbox = getSandbox(L);
    if (sandbox == nullptr) {
        luaL_error(L, "getCell: sandbox not found");
    }

    sandbox->pushCellObject(L, cell);
    return 1;
}

// ============================================================================
// Cells API: setCell(ref, value)
// Sets cell value (creates cell if needed)
// ============================================================================
int LuauSandbox::luaCellSet(lua_State* L) {
    // Get the ref argument
    const char* ref = luaL_checkstring(L, 1);

    // Get context
    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "setCell: no context set");
    }

    // Parse A1 reference
    int colIdx = 0;
    int rowIdx = 0;
    if (!parseA1Ref(ref, &colIdx, &rowIdx)) {
        luaL_error(L, "setCell: invalid reference '%s'", ref);
    }

    // Same as WASM CellsEngine::createCell: missing axes must be CRDT ops.
    // getOrCreateColumnByPosition only mutates local state — peers then reject
    // CELL_SET with INVALID_TARGET (empty A1 fails; updating existing B1 works).
    Axis* col = ensureColumnViaCrdt(*workbook, *sheet, static_cast<uint32_t>(colIdx));
    Axis* row = ensureRowViaCrdt(*workbook, *sheet, static_cast<uint32_t>(rowIdx));
    if (col == nullptr || row == nullptr) {
        luaL_error(L, "setCell: failed to ensure column/row for '%s'", ref);
    }

    // Prefer existing cell id; otherwise mint id (cell is created by applyOperation)
    Cell* existingCell = sheet->getCellAt(col->id, row->id);
    const ID cellId = existingCell != nullptr ? existingCell->id : generate_id();

    // Build payload based on value type
    std::string payload;
    const std::string colIdStr = col->id.toString();
    const std::string rowIdStr = row->id.toString();

    // Helper to append existing style/format for full-state resurrection correctness
    auto appendStyleFormat = [&payload, &workbook, &cellId, existingCell]() {
        if (existingCell == nullptr) {
            return;
        }
        if (existingCell->hasStyle()) {
            const StyleBuffer* sty = workbook->getEntityStyle(cellId);
            if (sty) {
                payload += R"(,"sty":")" + sty->toBase64() + R"(")";
            }
        }
        if (existingCell->hasFormat()) {
            const FormatBuffer* fmt = workbook->getEntityFormat(cellId);
            if (fmt) {
                payload += R"(,"fmt":")" + fmt->toBase64() + R"(")";
            }
        }
    };

    if (lua_isnumber(L, 2) != 0) {
        const double num = lua_tonumber(L, 2);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", num);
        payload = R"({"t":"n","v":")" + std::string(buf) + R"(","col":")" + colIdStr +
                  R"(","row":")" + rowIdStr + R"(")";
        appendStyleFormat();
        payload += "}";
    } else if (lua_isstring(L, 2) != 0) {
        const char* str = lua_tostring(L, 2);
        if (str[0] == '=') {
            // Parse formula with FormulaParser to get AST
            FormulaParser parser(str);
            auto ast = parser.parse();
            if (ast == nullptr || parser.hasErrors()) {
                // If parse fails, still store the formula as-is (it will show error on eval)
                RefConverter conv;
                conv.setContext(*sheet);
                const std::string uuidFormula = conv.formulaToUuid(str);
                payload = R"({"t":"f","v":")" + jsonEscape(uuidFormula) + R"(","col":")" +
                          colIdStr + R"(","row":")" + rowIdStr + R"(")";
                appendStyleFormat();
                payload += "}";
            } else {
                // CRDT-compliant resolution: discover and create entities first
                FormulaResolver resolver(*workbook, *sheet);
                const RequiredEntities required = resolver.getRequiredEntities(ast.get());

                // Create required columns via CRDT operations
                // Note: size is omitted to use local default (sizeSet=false)
                for (const auto& pending : required.columns) {
                    const std::string colPayload =
                        "{\"pos\":" + std::to_string(pending.position) + "}";
                    const Operation colOp =
                        makeColSetOp(*workbook, pending.id, pending.sheetId, colPayload);
                    applyOperation(*workbook, colOp);
                }

                // Create required rows via CRDT operations
                // Note: size is omitted to use local default (sizeSet=false)
                for (const auto& pending : required.rows) {
                    const std::string rowPayload =
                        "{\"pos\":" + std::to_string(pending.position) + "}";
                    const Operation rowOp =
                        makeRowSetOp(*workbook, pending.id, pending.sheetId, rowPayload);
                    applyOperation(*workbook, rowOp);
                }

                // Create required cells via CRDT operations (empty cells for references)
                for (const auto& pending : required.cells) {
                    const std::string cellPayload = "{\"t\":\"s\",\"v\":\"\",\"col\":\"" +
                                                    pending.colId.toString() + "\",\"row\":\"" +
                                                    pending.rowId.toString() + "\"}";
                    const Operation cellOp =
                        makeCellSetOp(*workbook, pending.id, pending.sheetId, cellPayload);
                    applyOperation(*workbook, cellOp);
                }

                // Now resolve (all entities should exist)
                const ResolveResult resolveRes = resolver.resolve(ast.get());
                if (!resolveRes.success) {
                    // Resolution failed - use fallback
                    RefConverter conv;
                    conv.setContext(*sheet);
                    const std::string uuidFormula = conv.formulaToUuid(str);
                    payload = R"({"t":"f","v":")" + jsonEscape(uuidFormula) + R"(","col":")" +
                              colIdStr + R"(","row":")" + rowIdStr + R"(")";
                    appendStyleFormat();
                    payload += "}";
                } else {
                    // Serialize the resolved AST to UUID format
                    const std::string uuidFormula = FormulaSerializer::serialize(ast.get());
                    payload = R"({"t":"f","v":")" + jsonEscape(uuidFormula) + R"(","col":")" +
                              colIdStr + R"(","row":")" + rowIdStr + R"(")";
                    appendStyleFormat();
                    payload += "}";

                    // Add dependencies to the dependency graph
                    DependencyGraph* depGraph = sheet->getDependencyGraph();
                    if (depGraph != nullptr) {
                        // First remove any existing dependencies for this cell
                        depGraph->removeFormula(cellId);

                        // Add new dependencies with position resolver
                        depGraph->addFormula(cellId, ast.get(), [sheet](const ID& cellIdArg) {
                            const Cell* depCell = sheet->getCell(cellIdArg);
                            if (depCell == nullptr) {
                                return std::make_pair(static_cast<int32_t>(-1),
                                                      static_cast<int32_t>(-1));
                            }
                            const Axis* depCol = sheet->getColumn(depCell->colId);
                            const Axis* depRow = sheet->getRow(depCell->rowId);
                            if (depCol == nullptr || depRow == nullptr) {
                                return std::make_pair(static_cast<int32_t>(-1),
                                                      static_cast<int32_t>(-1));
                            }
                            return std::make_pair(static_cast<int32_t>(depCol->position),
                                                  static_cast<int32_t>(depRow->position));
                        });

                        // Mark as volatile if needed
                        if (FormulaResolver::containsVolatileFunction(ast.get())) {
                            depGraph->markVolatile(cellId);
                        } else {
                            depGraph->unmarkVolatile(cellId);
                        }
                    }
                }
            }
        } else {
            // Literal string
            payload = R"({"t":"s","v":")" + jsonEscape(str) + R"(","col":")" + colIdStr +
                      R"(","row":")" + rowIdStr + R"(")";
            appendStyleFormat();
            payload += "}";
        }
    } else if (lua_isboolean(L, 2) != 0) {
        const bool val = lua_toboolean(L, 2) != 0;
        payload = R"({"t":"b","v":")" + std::string(val ? "true" : "false") + R"(","col":")" +
                  colIdStr + R"(","row":")" + rowIdStr + R"(")";
        appendStyleFormat();
        payload += "}";
    } else if (lua_isnil(L, 2) != 0) {
        // Clear the cell - remove from dependency graph first
        if (existingCell == nullptr) {
            return 0;  // nothing to clear
        }
        DependencyGraph* depGraph = sheet->getDependencyGraph();
        if (depGraph != nullptr) {
            depGraph->removeFormula(cellId);
            depGraph->unmarkVolatile(cellId);
        }

        const Operation op = makeCellDeleteOp(*workbook, cellId);
        applyOperation(*workbook, op);

        // Trigger recalculation for dependents
        markDirty(sheet, cellId);
        const std::vector<ID> changed = {cellId};
        cells::recalculate(workbook, changed);
        cells::recalculateVolatile(sheet);
        return 0;
    } else {
        luaL_error(L, "setCell: unsupported value type");
    }

    // Apply via CRDT with sheet id (matches WASM createCell) so peers apply on the
    // correct sheet and create the cell if missing.
    const Operation op = makeCellSetOp(*workbook, cellId, sheet->id, payload);
    applyOperation(*workbook, op);

    // Trigger recalculation: mark dependents dirty and recalculate
    // This ensures formulas that depend on this cell get updated
    markDirty(sheet, cellId);
    const std::vector<ID> changed = {cellId};
    cells::recalculate(workbook, changed);
    cells::recalculateVolatile(sheet);

    return 0;
}

// ============================================================================
// Cells API: setDocumentTitle(title)
// ============================================================================
int LuauSandbox::luaDocumentSetTitle(lua_State* L) {
    const char* title = luaL_checkstring(L, 1);

    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "setDocumentTitle: no context set");
    }

    const std::string payload = R"({"name":")" + jsonEscape(title) + R"("})";
    const Operation op = makeWorkbookSetOp(*workbook, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: getDocumentTitle()
// Returns the current workbook name/title
// ============================================================================
int LuauSandbox::luaDocumentGetTitle(lua_State* L) {
    const Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "getDocumentTitle: no context set");
    }
    lua_pushstring(L, workbook->name.c_str());
    return 1;
}

// ============================================================================
// Cells API: setColumnWidth(col, options)
// options.width: number (pixels)
// ============================================================================
int LuauSandbox::luaColumnSetWidth(lua_State* L) {
    const char* colRef = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "width");
    if (lua_isnumber(L, -1) == 0) {
        luaL_error(L, "setColumnWidth: options.width required");
    }
    const int width = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "setColumnWidth: no context set");
    }

    // Parse column reference (just the letter part)
    const int colIdx = parseColumnLetter(colRef, nullptr);
    if (colIdx < 0) {
        luaL_error(L, "setColumnWidth: invalid column '%s'", colRef);
    }

    const Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(colIdx));
    if (col == nullptr) {
        luaL_error(L, "setColumnWidth: column '%s' not found", colRef);
    }

    const std::string payload = R"({"size":)" + std::to_string(width) + "}";
    const Operation op = makeColSetOp(*workbook, col->id, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: setRowHeight(row, options)
// options.height: number (pixels)
// ============================================================================
int LuauSandbox::luaRowSetHeight(lua_State* L) {
    const int rowNum = static_cast<int>(luaL_checknumber(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "height");
    if (lua_isnumber(L, -1) == 0) {
        luaL_error(L, "setRowHeight: options.height required");
    }
    const int height = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "setRowHeight: no context set");
    }

    const int rowIdx = rowNum - 1;  // Convert 1-based to 0-based
    if (rowIdx < 0) {
        luaL_error(L, "setRowHeight: invalid row number %d", rowNum);
    }

    const Axis* row = sheet->getRowByPosition(static_cast<uint32_t>(rowIdx));
    if (row == nullptr) {
        luaL_error(L, "setRowHeight: row %d not found", rowNum);
    }

    const std::string payload = R"({"size":)" + std::to_string(height) + "}";
    const Operation op = makeRowSetOp(*workbook, row->id, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Cells API: moveColumn(col, options)
// options.to: number (target position, 0-based)
// ============================================================================
int LuauSandbox::luaColumnMove(lua_State* L) {
    const char* colRef = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "to");
    if (lua_isnumber(L, -1) == 0) {
        luaL_error(L, "moveColumn: options.to required");
    }
    const int toPos = static_cast<int>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "moveColumn: no context set");
    }

    // Parse column reference
    const int colIdx = parseColumnLetter(colRef, nullptr);
    if (colIdx < 0) {
        luaL_error(L, "moveColumn: invalid column '%s'", colRef);
    }

    const Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(colIdx));
    if (col == nullptr) {
        luaL_error(L, "moveColumn: column '%s' not found", colRef);
    }

    const std::string payload = R"({"position":)" + std::to_string(toPos) + "}";
    const Operation op = makeColSetOp(*workbook, col->id, payload);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Axis API: hideColumn(col)
// Hide a column by letter (e.g., "A", "B", "AB")
// ============================================================================
int LuauSandbox::luaHideColumn(lua_State* L) {
    const char* colRef = luaL_checkstring(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "hideColumn: no context set");
    }

    // Parse column reference
    const int colIdx = parseColumnLetter(colRef, nullptr);
    if (colIdx < 0) {
        luaL_error(L, "hideColumn: invalid column '%s'", colRef);
    }

    const Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(colIdx));
    if (col == nullptr) {
        luaL_error(L, "hideColumn: column '%s' not found", colRef);
    }

    const Operation op = makeAxisSetHiddenOp(*workbook, col->id, true);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Axis API: showColumn(col)
// Show a hidden column by letter (e.g., "A", "B", "AB")
// ============================================================================
int LuauSandbox::luaShowColumn(lua_State* L) {
    const char* colRef = luaL_checkstring(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "showColumn: no context set");
    }

    // Parse column reference
    const int colIdx = parseColumnLetter(colRef, nullptr);
    if (colIdx < 0) {
        luaL_error(L, "showColumn: invalid column '%s'", colRef);
    }

    const Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(colIdx));
    if (col == nullptr) {
        luaL_error(L, "showColumn: column '%s' not found", colRef);
    }

    const Operation op = makeAxisSetHiddenOp(*workbook, col->id, false);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Axis API: hideRow(row)
// Hide a row by number (1-based)
// ============================================================================
int LuauSandbox::luaHideRow(lua_State* L) {
    const int rowNum = static_cast<int>(luaL_checknumber(L, 1));

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "hideRow: no context set");
    }

    if (rowNum < 1) {
        luaL_error(L, "hideRow: row number must be >= 1");
    }

    const int rowIdx = rowNum - 1;  // Convert to 0-based
    const Axis* row = sheet->getRowByPosition(static_cast<uint32_t>(rowIdx));
    if (row == nullptr) {
        luaL_error(L, "hideRow: row %d not found", rowNum);
    }

    const Operation op = makeAxisSetHiddenOp(*workbook, row->id, true);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Axis API: showRow(row)
// Show a hidden row by number (1-based)
// ============================================================================
int LuauSandbox::luaShowRow(lua_State* L) {
    const int rowNum = static_cast<int>(luaL_checknumber(L, 1));

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "showRow: no context set");
    }

    if (rowNum < 1) {
        luaL_error(L, "showRow: row number must be >= 1");
    }

    const int rowIdx = rowNum - 1;  // Convert to 0-based
    const Axis* row = sheet->getRowByPosition(static_cast<uint32_t>(rowIdx));
    if (row == nullptr) {
        luaL_error(L, "showRow: row %d not found", rowNum);
    }

    const Operation op = makeAxisSetHiddenOp(*workbook, row->id, false);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Axis API: setColumnStyle(col, style)
// Set default style for a column. Style is a table with style properties.
// e.g., setColumnStyle("A", {bold=true, bgColor="#FF0000"})
// ============================================================================
int LuauSandbox::luaSetColumnStyle(lua_State* L) {
    const char* colRef = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "setColumnStyle: no context set");
    }

    // Parse column reference
    const int colIdx = parseColumnLetter(colRef, nullptr);
    if (colIdx < 0) {
        luaL_error(L, "setColumnStyle: invalid column '%s'", colRef);
    }

    const Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(colIdx));
    if (col == nullptr) {
        luaL_error(L, "setColumnStyle: column '%s' not found", colRef);
    }

    // Parse style table into CellStyle
    CellStyle style;
    lua_getfield(L, 2, "bold");
    if (lua_isboolean(L, -1) != 0) {
        style.bold = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "italic");
    if (lua_isboolean(L, -1) != 0) {
        style.italic = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "underline");
    if (lua_isboolean(L, -1) != 0) {
        style.underline = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "wrapText");
    if (lua_isboolean(L, -1) != 0) {
        style.wrapText = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "bgColor");
    if (lua_isstring(L, -1) != 0) {
        style.bgColor = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "textColor");
    if (lua_isstring(L, -1) != 0) {
        style.textColor = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "fontFamily");
    if (lua_isstring(L, -1) != 0) {
        style.fontFamily = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "fontSize");
    if (lua_isnumber(L, -1) != 0) {
        style.fontSize = static_cast<uint8_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);

    // Convert to content-addressed StyleBuffer and apply to column
    const StyleBuffer styleBuf = StyleBuffer::fromCellStyle(style);
    const Operation op = makeAxisSetStyleOp(*workbook, col->id, styleBuf);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Axis API: setRowStyle(row, style)
// Set default style for a row. Style is a table with style properties.
// e.g., setRowStyle(1, {bold=true, bgColor="#FF0000"})
// ============================================================================
int LuauSandbox::luaSetRowStyle(lua_State* L) {
    const int rowNum = static_cast<int>(luaL_checknumber(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "setRowStyle: no context set");
    }

    if (rowNum < 1) {
        luaL_error(L, "setRowStyle: row number must be >= 1");
    }

    const int rowIdx = rowNum - 1;  // Convert to 0-based
    const Axis* row = sheet->getRowByPosition(static_cast<uint32_t>(rowIdx));
    if (row == nullptr) {
        luaL_error(L, "setRowStyle: row %d not found", rowNum);
    }

    // Parse style table into CellStyle
    CellStyle style;
    lua_getfield(L, 2, "bold");
    if (lua_isboolean(L, -1) != 0) {
        style.bold = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "italic");
    if (lua_isboolean(L, -1) != 0) {
        style.italic = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "underline");
    if (lua_isboolean(L, -1) != 0) {
        style.underline = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "wrapText");
    if (lua_isboolean(L, -1) != 0) {
        style.wrapText = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "bgColor");
    if (lua_isstring(L, -1) != 0) {
        style.bgColor = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "textColor");
    if (lua_isstring(L, -1) != 0) {
        style.textColor = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "fontFamily");
    if (lua_isstring(L, -1) != 0) {
        style.fontFamily = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "fontSize");
    if (lua_isnumber(L, -1) != 0) {
        style.fontSize = static_cast<uint8_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);

    // Convert to content-addressed StyleBuffer and apply to row
    const StyleBuffer styleBuf = StyleBuffer::fromCellStyle(style);
    const Operation op = makeAxisSetStyleOp(*workbook, row->id, styleBuf);
    applyOperation(*workbook, op);

    return 0;
}

// ============================================================================
// Freeze Panes API: freezePanes(col, row)
// Freeze the specified number of columns and rows
// col: number of columns to freeze (0 = none, 1 = column A, etc.)
// row: number of rows to freeze (0 = none, 1 = row 1, etc.)
// e.g., freezePanes(1, 2) freezes column A and rows 1-2
// ============================================================================
int LuauSandbox::luaFreezePanes(lua_State* L) {
    const int freezeCol = static_cast<int>(luaL_optnumber(L, 1, 0));
    const int freezeRow = static_cast<int>(luaL_optnumber(L, 2, 0));

    Sheet* sheet = getSheet(L);
    if (sheet == nullptr) {
        luaL_error(L, "freezePanes: no context set");
    }

    if (freezeCol < 0) {
        luaL_error(L, "freezePanes: col must be >= 0");
    }
    if (freezeRow < 0) {
        luaL_error(L, "freezePanes: row must be >= 0");
    }

    // Directly set the sheet properties (no CRDT operation for view properties yet)
    sheet->freezeCol = static_cast<uint16_t>(freezeCol);
    sheet->freezeRow = static_cast<uint16_t>(freezeRow);

    return 0;
}

// ============================================================================
// Freeze Panes API: getFreezePanes()
// Returns a table with col and row fields indicating frozen panes
// e.g., {col=1, row=2} means column A and rows 1-2 are frozen
// ============================================================================
int LuauSandbox::luaGetFreezePanes(lua_State* L) {
    const Sheet* sheet = getSheet(L);
    if (sheet == nullptr) {
        luaL_error(L, "getFreezePanes: no context set");
    }

    lua_newtable(L);
    lua_pushinteger(L, sheet->freezeCol);
    lua_setfield(L, -2, "col");
    lua_pushinteger(L, sheet->freezeRow);
    lua_setfield(L, -2, "row");

    return 1;
}

// ============================================================================
// Cells API: selectSheet(sheet|name|index)
// Accepts: sheet object, name string, or 1-based index number
// ============================================================================
int LuauSandbox::luaSelectSheet(lua_State* L) {
    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "selectSheet: no context set");
    }

    Sheet* newSheet = nullptr;

    if (lua_isnumber(L, 1) != 0) {
        // Select by index (1-based)
        const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;  // Convert to 0-based
        if (index < 0 || static_cast<size_t>(index) >= workbook->sheetCount()) {
            luaL_error(L, "selectSheet: index %d out of range",
                       static_cast<int>(lua_tonumber(L, 1)));
        }
        newSheet = workbook->getSheetByIndex(static_cast<size_t>(index));
    } else if (lua_isstring(L, 1) != 0) {
        // Select by name
        const char* name = lua_tostring(L, 1);
        newSheet = workbook->getSheetByName(name);
        if (newSheet == nullptr) {
            luaL_error(L, "selectSheet: sheet '%s' not found", name);
        }
    } else if (lua_istable(L, 1) != 0) {
        // Select by sheet object
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "selectSheet: invalid sheet object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        const ID sheetId(uuidStr);
        newSheet = workbook->getSheet(sheetId);
        if (newSheet == nullptr) {
            luaL_error(L, "selectSheet: sheet not found");
        }
    } else {
        luaL_error(L, "selectSheet: expected sheet object, name string, or index number");
    }

    // Update the context
    LuauSandbox* sandbox = getSandbox(L);
    if (sandbox != nullptr) {
        sandbox->setContext(workbook, newSheet);
    }

    return 0;
}

// ============================================================================
// Cells API: getSheet(arg)
// Accepts: number (1-based index), string (name), or table {index=N, name="..."}
// Returns: sheet object or nil if not found
// ============================================================================
int LuauSandbox::luaGetSheet(lua_State* L) {
    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "getSheet: no context set");
    }

    // NOLINTBEGIN(misc-const-correctness) - Sheet lookup returns non-const
    Sheet* sheet = nullptr;

    if (lua_isnumber(L, 1) != 0) {
        // getSheet(1) - direct number, 1-based
        const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;  // Convert to 0-based
        if (index < 0 || static_cast<size_t>(index) >= workbook->sheetCount()) {
            lua_pushnil(L);
            return 1;
        }
        sheet = workbook->getSheetByIndex(static_cast<size_t>(index));
    } else if (lua_isstring(L, 1) != 0) {
        // getSheet("Sheet1") - direct name
        const char* name = lua_tostring(L, 1);
        sheet = workbook->getSheetByName(name);
    } else if (lua_istable(L, 1) != 0) {
        // getSheet({index = 1}) or getSheet({name = "Sheet1"})
        lua_getfield(L, 1, "index");
        if (lua_isnumber(L, -1) != 0) {
            const int index = static_cast<int>(lua_tonumber(L, -1)) - 1;  // 1-based to 0-based
            lua_pop(L, 1);

            if (index < 0 || static_cast<size_t>(index) >= workbook->sheetCount()) {
                lua_pushnil(L);
                return 1;
            }
            sheet = workbook->getSheetByIndex(static_cast<size_t>(index));
        } else {
            lua_pop(L, 1);

            // Check for name parameter
            lua_getfield(L, 1, "name");
            if (lua_isstring(L, -1) != 0) {
                const char* name = lua_tostring(L, -1);
                lua_pop(L, 1);

                sheet = workbook->getSheetByName(name);
            } else {
                lua_pop(L, 1);
                luaL_error(L, "getSheet: requires index, name, {index = N}, or {name = \"...\"}");
            }
        }
    } else {
        luaL_error(L, "getSheet: requires index, name, {index = N}, or {name = \"...\"}");
    }
    // NOLINTEND(misc-const-correctness)

    if (sheet == nullptr) {
        lua_pushnil(L);
        return 1;
    }

    // Get sandbox to push sheet object
    const LuauSandbox* sandbox = getSandbox(L);
    if (sandbox == nullptr) {
        luaL_error(L, "getSheet: sandbox not found");
    }

    sandbox->pushSheetObject(L, sheet);
    return 1;
}

// ============================================================================
// Cells API: addSheet(name?)
// Creates a new sheet, optionally with a name
// Returns: sheet object
// ============================================================================
int LuauSandbox::luaAddSheet(lua_State* L) {
    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        luaL_error(L, "addSheet: no context set");
    }

    // Get name argument (optional)
    std::string sheetName;
    if (lua_isstring(L, 1) != 0) {
        sheetName = lua_tostring(L, 1);
    } else {
        // Generate default name: "Sheet" + (count + 1)
        sheetName = "Sheet" + std::to_string(workbook->sheetCount() + 1);
    }

    // Generate a new ID for the sheet
    const ID sheetId = generate_id();

    // Create the sheet operation payload
    const std::string payload = R"({"name":")" + jsonEscape(sheetName) + R"("})";

    // Apply the operation
    const Operation op = makeSheetSetOp(*workbook, sheetId, payload);
    applyOperation(*workbook, op);

    // Get the created sheet and return it
    Sheet* sheet = workbook->getSheet(sheetId);
    if (sheet == nullptr) {
        luaL_error(L, "addSheet: failed to create sheet");
    }

    // Get sandbox to push sheet object
    const LuauSandbox* sandbox = getSandbox(L);
    if (sandbox == nullptr) {
        luaL_error(L, "addSheet: sandbox not found");
    }

    sandbox->pushSheetObject(L, sheet);
    return 1;
}

// ============================================================================
// Cells API: selectRange(options)
// options.from: string (start cell ref)
// options.to: string (end cell ref)
// ============================================================================
int LuauSandbox::luaRangeSelect(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "from");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "selectRange: options.from required");
    }
    // const char* fromRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "to");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "selectRange: options.to required");
    }
    // const char* toRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    // Note: Selection is a UI concept, not a model concept
    // This function would need to communicate with the UI layer
    // For now, we just validate the input and succeed silently
    // The actual selection would be handled by the TypeScript layer

    return 0;
}

// ============================================================================
// Cells API: deleteRange(options)
// options.from: string (start cell ref)
// options.to: string (end cell ref)
// ============================================================================
int LuauSandbox::luaRangeDelete(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "from");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "deleteRange: options.from required");
    }
    const char* fromRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "to");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "deleteRange: options.to required");
    }
    const char* toRef = lua_tostring(L, -1);
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "deleteRange: no context set");
    }

    // Parse range references
    int fromCol = 0;
    int fromRow = 0;
    int toCol = 0;
    int toRow = 0;
    if (!parseA1Ref(fromRef, &fromCol, &fromRow) || !parseA1Ref(toRef, &toCol, &toRow)) {
        luaL_error(L, "deleteRange: invalid range");
    }

    // Normalize range (ensure from <= to)
    if (fromCol > toCol) {
        std::swap(fromCol, toCol);
    }
    if (fromRow > toRow) {
        std::swap(fromRow, toRow);
    }

    // Delete cells in range
    for (int c = fromCol; c <= toCol; c++) {
        const Axis* col = sheet->getColumnByPosition(static_cast<uint32_t>(c));
        if (col == nullptr) {
            continue;
        }
        for (int r = fromRow; r <= toRow; r++) {
            const Axis* row = sheet->getRowByPosition(static_cast<uint32_t>(r));
            if (row == nullptr) {
                continue;
            }
            const Cell* cell = sheet->getCellAt(col->id, row->id);
            if (cell != nullptr) {
                const Operation op = makeCellDeleteOp(*workbook, cell->id);
                applyOperation(*workbook, op);
            }
        }
    }

    return 0;
}

// ============================================================================
// Cells API: fillRange(options)
// options.from: string (source range, e.g., "A1:A2" or "A1" for single cell)
// options.to: string (full target range including source, e.g., "A1:A10")
// Returns: {success: boolean, cellsFilled: number, error?: string}
// ============================================================================
int LuauSandbox::luaRangeFill(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    // Get source range (from)
    lua_getfield(L, 1, "from");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "fillRange: options.from required");
    }
    const char* fromRange = lua_tostring(L, -1);
    lua_pop(L, 1);

    // Get target range (to)
    lua_getfield(L, 1, "to");
    if (lua_isstring(L, -1) == 0) {
        luaL_error(L, "fillRange: options.to required");
    }
    const char* toRange = lua_tostring(L, -1);
    lua_pop(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "fillRange: no context set");
    }

    // Parse source range (e.g., "A1:A2" or "A1")
    int fromCol = 0;
    int fromRow = 0;
    int toCol = 0;
    int toRow = 0;
    if (!parseA1Range(fromRange, &fromCol, &fromRow, &toCol, &toRow)) {
        luaL_error(L, "fillRange: invalid source range '%s'", fromRange);
    }

    // Parse target range (e.g., "A1:A10")
    int targetFromCol = 0;
    int targetFromRow = 0;
    int targetToCol = 0;
    int targetToRow = 0;
    if (!parseA1Range(toRange, &targetFromCol, &targetFromRow, &targetToCol, &targetToRow)) {
        luaL_error(L, "fillRange: invalid target range '%s'", toRange);
    }

    // Normalize ranges (ensure from <= to)
    if (fromCol > toCol) {
        std::swap(fromCol, toCol);
    }
    if (fromRow > toRow) {
        std::swap(fromRow, toRow);
    }
    if (targetFromCol > targetToCol) {
        std::swap(targetFromCol, targetToCol);
    }
    if (targetFromRow > targetToRow) {
        std::swap(targetFromRow, targetToRow);
    }

    // Call the fill function
    const FillResult result = fillRange(workbook, sheet, fromCol, fromRow, toCol, toRow,
                                        targetFromCol, targetFromRow, targetToCol, targetToRow);

    // Return result table
    lua_newtable(L);
    lua_pushboolean(L, result.success ? 1 : 0);
    lua_setfield(L, -2, "success");
    lua_pushnumber(L, result.cellsFilled);
    lua_setfield(L, -2, "cellsFilled");
    if (!result.error.empty()) {
        lua_pushstring(L, result.error.c_str());
        lua_setfield(L, -2, "error");
    }

    return 1;
}

// ============================================================================
// Output API: print(...)
// Captures output to printBuffer_ for display in console
// ============================================================================
int LuauSandbox::luaPrint(lua_State* L) {
    LuauSandbox* sandbox = getSandbox(L);
    if (sandbox == nullptr) {
        return 0;
    }

    const int nargs = lua_gettop(L);
    for (int i = 1; i <= nargs; i++) {
        if (i > 1) {
            sandbox->printBuffer_ += " ";
        }
        const char* s = luaL_tolstring(L, i, nullptr);
        if (s != nullptr) {
            sandbox->printBuffer_ += s;
        }
        lua_pop(L, 1);  // Pop the string returned by luaL_tolstring
    }
    sandbox->printBuffer_ += "\n";

    return 0;
}

// ============================================================================
// Cells API: setFormat(range, formatStr)
// Apply format to all cells in range
// range: A1 notation like "A1:B10" or "A1" for single cell
// formatStr: format ID (e.g., "FMT_C002"), base64 encoded format, or nil to clear
// ============================================================================
int LuauSandbox::luaSetFormat(lua_State* L) {
    const char* range = luaL_checkstring(L, 1);

    // Get format string (can be string or nil)
    std::string formatBase64;  // Empty = clear format
    if (lua_isstring(L, 2) != 0) {
        const std::string formatStr = lua_tostring(L, 2);

        // Try to parse as legacy format ID first (e.g., "FMT_C002")
        auto maybeFormat = formatIdToBuffer(formatStr);
        if (maybeFormat.has_value()) {
            formatBase64 = maybeFormat->toBase64();
        } else {
            // Try as base64 directly
            auto maybeBase64 = FormatBuffer::fromBase64(formatStr);
            if (maybeBase64.has_value()) {
                formatBase64 = formatStr;
            } else {
                luaL_error(L, "setFormat: invalid format string '%s'", formatStr.c_str());
                return 0;
            }
        }
    } else if (lua_isnil(L, 2) == 0) {
        luaL_error(L, "setFormat: second argument must be format string or nil");
        return 0;
    }
    // nil clears format (empty base64)

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "setFormat: no context set");
        return 0;
    }

    // Parse range
    int fromCol = 0;
    int fromRow = 0;
    int toCol = 0;
    int toRow = 0;
    if (!parseA1Range(range, &fromCol, &fromRow, &toCol, &toRow)) {
        luaL_error(L, "setFormat: invalid range '%s'", range);
        return 0;
    }

    // Normalize range
    if (fromCol > toCol) {
        std::swap(fromCol, toCol);
    }
    if (fromRow > toRow) {
        std::swap(fromRow, toRow);
    }

    // Apply format to all cells in range
    auto maybeFormat = FormatBuffer::fromBase64(formatBase64);

    for (int c = fromCol; c <= toCol; c++) {
        for (int r = fromRow; r <= toRow; r++) {
            // Get or create cell at this position
            const Axis* col = sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(c));
            const Axis* row = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(r));
            const Cell* cell = sheet->getOrCreateCellAt(col->id, row->id);

            if (formatBase64.empty()) {
                const Operation op = makeCellClearFormatOp(*workbook, cell->id);
                applyOperation(*workbook, op);
            } else if (maybeFormat.has_value()) {
                const Operation op = makeCellSetFormatOp(*workbook, cell->id, *maybeFormat);
                applyOperation(*workbook, op);
            }
        }
    }

    return 0;
}

// ============================================================================
// Cells API: setStyle(range, styleTable)
// Apply style to all cells in range
// range: A1 notation like "A1:B10" or "A1" for single cell
// styleTable: {bold=true, italic=false, bgColor="#FF0000", ...} or nil to clear
// ============================================================================
int LuauSandbox::luaSetStyle(lua_State* L) {
    const char* range = luaL_checkstring(L, 1);

    Sheet* sheet = getSheet(L);
    Workbook* workbook = getWorkbook(L);
    if (sheet == nullptr || workbook == nullptr) {
        luaL_error(L, "setStyle: no context set");
    }

    // Parse range
    int fromCol = 0;
    int fromRow = 0;
    int toCol = 0;
    int toRow = 0;
    if (!parseA1Range(range, &fromCol, &fromRow, &toCol, &toRow)) {
        luaL_error(L, "setStyle: invalid range '%s'", range);
    }

    // Normalize range
    if (fromCol > toCol) {
        std::swap(fromCol, toCol);
    }
    if (fromRow > toRow) {
        std::swap(fromRow, toRow);
    }

    // Handle nil - clear style
    if (lua_isnil(L, 2) != 0) {
        for (int c = fromCol; c <= toCol; c++) {
            for (int r = fromRow; r <= toRow; r++) {
                const Axis* col = sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(c));
                const Axis* row = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(r));
                const Cell* cell = sheet->getOrCreateCellAt(col->id, row->id);

                const Operation op = makeCellClearStyleOp(*workbook, cell->id);
                applyOperation(*workbook, op);
            }
        }
        return 0;
    }

    // Expect a table
    if (lua_istable(L, 2) == 0) {
        luaL_error(L, "setStyle: second argument must be style table or nil");
    }

    // Build style from table, setting defined flags for provided properties
    CellStyle style;

    lua_getfield(L, 2, "bold");
    if (lua_isboolean(L, -1) != 0) {
        style.bold = lua_toboolean(L, -1) != 0;
        style.setDefined(DEFINED_BOLD);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "italic");
    if (lua_isboolean(L, -1) != 0) {
        style.italic = lua_toboolean(L, -1) != 0;
        style.setDefined(DEFINED_ITALIC);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "underline");
    if (lua_isboolean(L, -1) != 0) {
        style.underline = lua_toboolean(L, -1) != 0;
        style.setDefined(DEFINED_UNDERLINE);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "wrapText");
    if (lua_isboolean(L, -1) != 0) {
        style.wrapText = lua_toboolean(L, -1) != 0;
        style.setDefined(DEFINED_WRAPTEXT);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "bgColor");
    if (lua_isstring(L, -1) != 0) {
        style.bgColor = lua_tostring(L, -1);
        style.setDefined(DEFINED_BGCOLOR);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "textColor");
    if (lua_isstring(L, -1) != 0) {
        style.textColor = lua_tostring(L, -1);
        style.setDefined(DEFINED_TEXTCOLOR);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "fontFamily");
    if (lua_isstring(L, -1) != 0) {
        style.fontFamily = lua_tostring(L, -1);
        style.setDefined(DEFINED_FONTFAMILY);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "fontSize");
    if (lua_isnumber(L, -1) != 0) {
        style.fontSize = static_cast<uint8_t>(lua_tonumber(L, -1));
        style.setDefined(DEFINED_FONTSIZE);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "hAlign");
    if (lua_isstring(L, -1) != 0) {
        const char* hAlignStr = lua_tostring(L, -1);
        if (strcmp(hAlignStr, "center") == 0) {
            style.hAlign = TextAlign::CENTER;
        } else if (strcmp(hAlignStr, "right") == 0) {
            style.hAlign = TextAlign::RIGHT;
        } else if (strcmp(hAlignStr, "justify") == 0) {
            style.hAlign = TextAlign::JUSTIFY;
        } else {
            style.hAlign = TextAlign::LEFT;
        }
        style.setDefined(DEFINED_HALIGN);
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "vAlign");
    if (lua_isstring(L, -1) != 0) {
        const char* vAlignStr = lua_tostring(L, -1);
        if (strcmp(vAlignStr, "top") == 0) {
            style.vAlign = VerticalAlign::TOP;
        } else if (strcmp(vAlignStr, "middle") == 0) {
            style.vAlign = VerticalAlign::MIDDLE;
        } else {
            style.vAlign = VerticalAlign::BOTTOM;
        }
        style.setDefined(DEFINED_VALIGN);
    }
    lua_pop(L, 1);

    // If style is empty, clear it
    if (style.isEmpty()) {
        for (int c = fromCol; c <= toCol; c++) {
            for (int r = fromRow; r <= toRow; r++) {
                const Axis* col = sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(c));
                const Axis* row = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(r));
                const Cell* cell = sheet->getOrCreateCellAt(col->id, row->id);

                const Operation op = makeCellClearStyleOp(*workbook, cell->id);
                applyOperation(*workbook, op);
            }
        }
        return 0;
    }

    // Convert CellStyle to content-addressed StyleBuffer
    const StyleBuffer styleBuf = StyleBuffer::fromCellStyle(style);

    // Apply CELL_SET_STYLE to all cells in range using content-addressed style
    for (int c = fromCol; c <= toCol; c++) {
        for (int r = fromRow; r <= toRow; r++) {
            const Axis* col = sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(c));
            const Axis* row = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(r));
            const Cell* cell = sheet->getOrCreateCellAt(col->id, row->id);

            const Operation op = makeCellSetStyleOp(*workbook, cell->id, styleBuf);
            applyOperation(*workbook, op);
        }
    }

    return 0;
}

// ============================================================================
// Cells API: getFormats()
// Returns array of available format IDs with descriptions
// ============================================================================
int LuauSandbox::luaGetFormats(lua_State* L) {
    // Return a table of built-in format IDs with descriptions
    // Based on the format system defined in format_code_parser.h
    lua_newtable(L);
    int idx = 1;

    // Helper to add format entry
    auto addFormat = [L, &idx](const char* formatId, const char* description) {
        lua_newtable(L);
        lua_pushstring(L, formatId);
        lua_setfield(L, -2, "id");
        lua_pushstring(L, description);
        lua_setfield(L, -2, "description");
        lua_rawseti(L, -2, idx++);
    };

    // Number formats (General, Number)
    addFormat("FMT_0000", "General - no specific format");
    addFormat("FMT_N000", "Number - no decimals");
    addFormat("FMT_N001", "Number - 1 decimal");
    addFormat("FMT_N002", "Number - 2 decimals");
    addFormat("FMT_N003", "Number - 3 decimals");
    addFormat("FMT_N004", "Number - 4 decimals");

    // Currency formats
    addFormat("FMT_C000", "Currency - no decimals");
    addFormat("FMT_C001", "Currency - 1 decimal");
    addFormat("FMT_C002", "Currency - 2 decimals");

    // Percentage formats
    addFormat("FMT_P000", "Percentage - no decimals");
    addFormat("FMT_P001", "Percentage - 1 decimal");
    addFormat("FMT_P002", "Percentage - 2 decimals");

    // Scientific notation
    addFormat("FMT_E002", "Scientific - 2 decimals");

    // Date formats
    addFormat("FMT_D001", "Date - MM/DD/YYYY");
    addFormat("FMT_D002", "Date - DD/MM/YYYY");
    addFormat("FMT_D003", "Date - YYYY-MM-DD");

    // Time formats
    addFormat("FMT_T001", "Time - HH:MM:SS");
    addFormat("FMT_T002", "Time - HH:MM");

    // Note: Custom formats are now content-addressed via FormatBuffer.
    // They are embedded directly on entities and don't need a central registry.

    return 1;
}

}  // namespace cells
