// =============================================================================
// Luau Type Wrappers
// =============================================================================
//
// Implements Lua object wrappers for Cells types (Cell, Sheet). These wrappers
// use metatables to provide property access (__index), mutation (__newindex),
// and string representation (__tostring).
//
// Key responsibilities:
// - Cell object: exposes .ref, .value, .formula, .format, .style, .dependents, .dependencies
// - Sheet object: exposes .name property with read/write support
// - Object caching: maintains identity (cellA == cellB if same UUID)
// - Type coercion: Lua values ↔ CellValue types
//
// Cell properties:
// - ref: A1 reference string (read-only)
// - value: number/string/boolean/nil (read/write)
// - formula: formula string if cell has formula (read-only)
// - format: format base64 string (content-addressed FormatBuffer) (read/write)
// - style: style table {bold, italic, underline, bgColor, textColor, ...} (read/write)
// - dependents: array of cells that depend on this cell
// - dependencies: array of cells this cell depends on
//
// Sheet properties:
// - name: sheet name string (read/write)
//
// Dependencies: luau_sandbox.h, model.h, ref_converter.h
// Used by: luau_sandbox.cc (registerCellsAPI)
//
// =============================================================================

#include <cstring>

#include "core/cells/crdt.h"
#include "core/cells/dependency_graph.h"
#include "core/cells/format_buffer.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_eval.h"
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

// Helper: Escape a string for JSON (duplicated from luau_api.cc for independence)
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

// ============================================================================
// Cell __index metamethod: handles property access (e.g., cell.ref, cell.value)
// All properties are fetched dynamically from the Workbook (source of truth)
// ============================================================================
int LuauSandbox::luaCellIndex(lua_State* L) {
    // Stack: [1] = cell table, [2] = key (string)
    const char* key = lua_tostring(L, 2);
    if (key == nullptr) {
        lua_pushnil(L);
        return 1;
    }

    // Get the cell UUID from the table (always needed for property access)
    lua_getfield(L, 1, "_uuid");
    if (lua_isstring(L, -1) == 0) {
        lua_pop(L, 1);
        // Not a cell object, fall back to raw table access
        lua_rawget(L, 1);
        return 1;
    }
    const char* uuidStr = lua_tostring(L, -1);
    lua_pop(L, 1);

    // Get context
    Sheet* sheet = getSheet(L);
    if (sheet == nullptr) {
        luaL_error(L, "%s: no context set", key);
    }

    const ID cellId(uuidStr);
    // Non-const: .value may need to evaluate a dirty formula in place
    Cell* cell = sheet->getCell(cellId);
    if (cell == nullptr) {
        luaL_error(L, "%s: cell not found", key);
    }

    // Handle .ref property
    if (strcmp(key, "ref") == 0) {
        const Axis* col = sheet->getColumn(cell->colId);
        const Axis* row = sheet->getRow(cell->rowId);
        if (col == nullptr || row == nullptr) {
            luaL_error(L, "ref: cell position not found");
        }
        const std::string a1Ref =
            RefConverter::columnIndexToLetter(col->position) + std::to_string(row->position + 1);
        lua_pushstring(L, a1Ref.c_str());
        return 1;
    }

    // Handle .value property - always fetch from model
    if (strcmp(key, "value") == 0) {
        // Evaluate dirty formulas on read so agents see computed values even if
        // a prior style/format full-state op (or remote apply) left the formula dirty.
        Formula* formula = cell->getFormula();
        if (formula != nullptr && formula->dirty && formula->ast != nullptr) {
            evaluateCell(sheet, cell);
        }

        const CellValue& value = cell->value;
        switch (value.type) {
            case CellValueType::NUMBER:
            case CellValueType::FORMULA_NUMBER:
                lua_pushnumber(L, value.asNumber());
                break;
            case CellValueType::STRING:
            case CellValueType::FORMULA_STRING:
                // Empty string is treated as empty cell (returns nil)
                if (value.raw.empty()) {
                    lua_pushnil(L);
                } else {
                    lua_pushstring(L, value.asString().c_str());
                }
                break;
            case CellValueType::BOOLEAN:
            case CellValueType::FORMULA_BOOLEAN:
                lua_pushboolean(L, value.asBoolean() ? 1 : 0);
                break;
            default:
                lua_pushnil(L);
        }
        return 1;
    }

    // Handle .formula property - always fetch from model
    if (strcmp(key, "formula") == 0) {
        if (cell->isFormula()) {
            const Formula* f = cell->getFormula();
            if (f != nullptr && f->ast != nullptr) {
                // Workbook-aware display so cross-sheet refs (e.g. Sheet1!E1) stay
                // readable. Sheet-local RefConverter.formulaToA1 emits #REF! for
                // cell UUIDs that live on other sheets.
                const Workbook* workbook = getWorkbook(L);
                FormulaDisplayConverter converter(*sheet, workbook);
                const std::string a1Formula = converter.toDisplayString(f->ast);
                lua_pushstring(L, a1Formula.c_str());
                return 1;
            }
        }
        lua_pushnil(L);
        return 1;
    }

    // Handle .format property - returns format base64 string or nil (content-addressed)
    if (strcmp(key, "format") == 0) {
        const Workbook* workbook = getWorkbook(L);
        if (workbook != nullptr) {
            const FormatBuffer* format = workbook->getEntityFormat(cell->id);
            if (format != nullptr && !format->isEmpty()) {
                lua_pushstring(L, format->toBase64().c_str());
                return 1;
            }
        }
        lua_pushnil(L);
        return 1;
    }

    // Handle .style property - returns style table or nil (read from workbook entity styles)
    if (strcmp(key, "style") == 0) {
        // Get workbook to look up style
        const Workbook* workbook = getWorkbook(L);
        if (workbook == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        const StyleBuffer* styleBuf = workbook->getEntityStyle(cell->id);
        if (styleBuf == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        // Convert to CellStyle for Lua table
        const CellStyle style = styleBuf->toCellStyle();
        // Return style as Lua table
        lua_newtable(L);
        lua_pushboolean(L, style.bold ? 1 : 0);
        lua_setfield(L, -2, "bold");
        lua_pushboolean(L, style.italic ? 1 : 0);
        lua_setfield(L, -2, "italic");
        lua_pushboolean(L, style.underline ? 1 : 0);
        lua_setfield(L, -2, "underline");
        lua_pushboolean(L, style.wrapText ? 1 : 0);
        lua_setfield(L, -2, "wrapText");
        if (!style.bgColor.empty()) {
            lua_pushstring(L, style.bgColor.c_str());
            lua_setfield(L, -2, "bgColor");
        }
        if (!style.textColor.empty()) {
            lua_pushstring(L, style.textColor.c_str());
            lua_setfield(L, -2, "textColor");
        }
        if (!style.fontFamily.empty()) {
            lua_pushstring(L, style.fontFamily.c_str());
            lua_setfield(L, -2, "fontFamily");
        }
        if (style.fontSize > 0) {
            lua_pushnumber(L, style.fontSize);
            lua_setfield(L, -2, "fontSize");
        }
        // Horizontal alignment
        const char* hAlignStr = "left";
        switch (style.hAlign) {
            case TextAlign::CENTER:
                hAlignStr = "center";
                break;
            case TextAlign::RIGHT:
                hAlignStr = "right";
                break;
            case TextAlign::JUSTIFY:
                hAlignStr = "justify";
                break;
            default:
                break;
        }
        lua_pushstring(L, hAlignStr);
        lua_setfield(L, -2, "hAlign");
        // Vertical alignment
        const char* vAlignStr = "bottom";
        switch (style.vAlign) {
            case VerticalAlign::TOP:
                vAlignStr = "top";
                break;
            case VerticalAlign::MIDDLE:
                vAlignStr = "middle";
                break;
            default:
                break;
        }
        lua_pushstring(L, vAlignStr);
        lua_setfield(L, -2, "vAlign");
        return 1;
    }

    // Handle .dependents property - returns array of cells that depend on this cell
    // (cells whose formulas reference this cell)
    if (strcmp(key, "dependents") == 0) {
        const Axis* col = sheet->getColumn(cell->colId);
        const Axis* row = sheet->getRow(cell->rowId);
        if (col == nullptr || row == nullptr) {
            // Cell has no position, return empty array
            lua_newtable(L);
            return 1;
        }

        // Get dependency graph and query for dependents
        const DependencyGraph* depGraph = sheet->getDependencyGraph();
        if (depGraph == nullptr) {
            lua_newtable(L);
            return 1;
        }

        const std::vector<ID> dependentIds =
            depGraph->getDependentsForCell(cellId, col->position, row->position);

        // Create Lua array of cell objects
        const LuauSandbox* sandbox = getSandbox(L);
        lua_newtable(L);
        int idx = 1;
        for (const ID& depId : dependentIds) {
            Cell* depCell = sheet->getCell(depId);
            if (depCell != nullptr) {
                sandbox->pushCellObject(L, depCell);
                lua_rawseti(L, -2, idx++);
            }
        }
        return 1;
    }

    // Handle .dependencies property - returns array of cells this cell's formula reads from
    // (the cells highlighted in the UI when this cell is selected)
    if (strcmp(key, "dependencies") == 0) {
        // Get dependency graph and query for dependencies
        const DependencyGraph* depGraph = sheet->getDependencyGraph();
        if (depGraph == nullptr) {
            lua_newtable(L);
            return 1;
        }

        const std::vector<DependencyRef> deps = depGraph->getDependencies(cellId);

        // Create Lua array of cell objects (only CELL type refs, not ranges)
        const LuauSandbox* sandbox = getSandbox(L);
        lua_newtable(L);
        int idx = 1;
        for (const DependencyRef& dep : deps) {
            if (dep.type == DependencyRef::Type::CELL) {
                Cell* depCell = sheet->getCell(dep.cellId);
                if (depCell != nullptr) {
                    sandbox->pushCellObject(L, depCell);
                    lua_rawseti(L, -2, idx++);
                }
            }
            // TODO: Could expand to handle RANGE refs by iterating cells in range
        }
        return 1;
    }

    // For other keys, look up in the table itself
    lua_rawget(L, 1);
    return 1;
}

// ============================================================================
// Cell __newindex metamethod: handles property assignment (e.g., cell.value = x)
// ============================================================================
int LuauSandbox::luaCellNewIndex(lua_State* L) {
    // Stack: [1] = cell table, [2] = key (string), [3] = value
    const char* key = lua_tostring(L, 2);
    if (key == nullptr) {
        luaL_error(L, "invalid property name");
    }

    // Handle cell.value = x assignment
    if (strcmp(key, "value") == 0) {
        // Get the cell UUID from the table
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "value: invalid cell object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        // Get context
        Sheet* sheet = getSheet(L);
        Workbook* workbook = getWorkbook(L);
        if (sheet == nullptr || workbook == nullptr) {
            luaL_error(L, "value: no context set");
        }

        const ID cellId(uuidStr);
        const Cell* cell = sheet->getCell(cellId);
        if (cell == nullptr) {
            luaL_error(L, "value: cell not found");
        }

        // Get col/row IDs for the payload
        const std::string colIdStr = cell->colId.toString();
        const std::string rowIdStr = cell->rowId.toString();

        // Build payload based on value type (same logic as luaCellSet)
        std::string payload;

        // Helper to append existing style/format for full-state resurrection correctness
        auto appendStyleFormat = [&payload, &workbook, &cell]() {
            if (cell->hasStyle()) {
                const StyleBuffer* sty = workbook->getEntityStyle(cell->id);
                if (sty) {
                    payload += R"(,"sty":")" + sty->toBase64() + R"(")";
                }
            }
            if (cell->hasFormat()) {
                const FormatBuffer* fmt = workbook->getEntityFormat(cell->id);
                if (fmt) {
                    payload += R"(,"fmt":")" + fmt->toBase64() + R"(")";
                }
            }
        };

        if (lua_isnumber(L, 3) != 0) {
            const double num = lua_tonumber(L, 3);
            char buf[64];
            snprintf(buf, sizeof(buf), "%.15g", num);
            payload = R"({"t":"n","v":")" + std::string(buf) + R"(","col":")" + colIdStr +
                      R"(","row":")" + rowIdStr + R"(")";
            appendStyleFormat();
            payload += "}";
        } else if (lua_isstring(L, 3) != 0) {
            const char* str = lua_tostring(L, 3);
            if (str[0] == '=') {
                // Parse formula with FormulaParser to get AST
                FormulaParser parser(str);
                auto ast = parser.parse();
                if (ast == nullptr || parser.hasErrors()) {
                    // If parse fails, still store the formula as-is
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

                    // Phase 1: Discover what entities need to be created
                    const RequiredEntities required = resolver.getRequiredEntities(ast.get());

                    // Create required columns via CRDT operations
                    for (const auto& pending : required.columns) {
                        const std::string colPayload = R"({"pos":)" +
                                                       std::to_string(pending.position) +
                                                       R"(,"size":100})";  // Default column width
                        const Operation colOp =
                            makeColSetOp(*workbook, pending.id, pending.sheetId, colPayload);
                        applyOperation(*workbook, colOp);
                    }

                    // Create required rows via CRDT operations
                    for (const auto& pending : required.rows) {
                        const std::string rowPayload = R"({"pos":)" +
                                                       std::to_string(pending.position) +
                                                       R"(,"size":21})";  // Default row height
                        const Operation rowOp =
                            makeRowSetOp(*workbook, pending.id, pending.sheetId, rowPayload);
                        applyOperation(*workbook, rowOp);
                    }

                    // Create required cells via CRDT operations (empty cells for references)
                    for (const auto& pending : required.cells) {
                        const std::string cellPayload = R"({"t":"s","v":"","col":")" +
                                                        pending.colId.toString() + R"(","row":")" +
                                                        pending.rowId.toString() + R"("})";
                        const Operation cellOp =
                            makeCellSetOp(*workbook, pending.id, pending.sheetId, cellPayload);
                        applyOperation(*workbook, cellOp);
                    }

                    // Phase 2: Resolve with existing entities
                    const ResolveResult resolveRes = resolver.resolve(ast.get());
                    if (!resolveRes.success) {
                        // Resolution failed (e.g., sheet not found) - use fallback
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
                            depGraph->removeFormula(cell->id);

                            // Add new dependencies with position resolver
                            depGraph->addFormula(cell->id, ast.get(), [sheet](const ID& cellIdArg) {
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
                                depGraph->markVolatile(cell->id);
                            } else {
                                depGraph->unmarkVolatile(cell->id);
                            }
                        }
                    }
                }
            } else {
                // Literal string
                payload = R"({"t":"s","v":")" + jsonEscape(str) + R"(","col":")" + colIdStr +
                          R"(","row":")" + rowIdStr + R"("})";
            }
        } else if (lua_isboolean(L, 3) != 0) {
            const bool val = lua_toboolean(L, 3) != 0;
            payload = R"({"t":"b","v":")" + std::string(val ? "true" : "false") + R"(","col":")" +
                      colIdStr + R"(","row":")" + rowIdStr + R"("})";
        } else if (lua_isnil(L, 3) != 0) {
            // Clear the cell - remove from dependency graph first
            DependencyGraph* depGraph = sheet->getDependencyGraph();
            if (depGraph != nullptr) {
                depGraph->removeFormula(cell->id);
                depGraph->unmarkVolatile(cell->id);
            }

            const Operation op = makeCellDeleteOp(*workbook, cell->id);
            applyOperation(*workbook, op);

            // Trigger recalculation for dependents
            markDirty(sheet, cell->id);
            const std::vector<ID> changed = {cell->id};
            cells::recalculate(workbook, changed);
            cells::recalculateVolatile(sheet);
            return 0;
        } else {
            luaL_error(L, "cell.value: unsupported value type");
        }

        // Apply the operation via CRDT
        const Operation op = makeCellSetOp(*workbook, cell->id, payload);
        applyOperation(*workbook, op);

        // Trigger recalculation: mark dependents dirty and recalculate
        markDirty(sheet, cell->id);
        const std::vector<ID> changed = {cell->id};
        cells::recalculate(workbook, changed);
        cells::recalculateVolatile(sheet);
        return 0;
    }

    // Handle cell.ref = x (read-only)
    if (strcmp(key, "ref") == 0) {
        luaL_error(L, "cell.ref is read-only");
    }

    // Handle cell.formula = x (read-only)
    if (strcmp(key, "formula") == 0) {
        luaL_error(L, "cell.formula is read-only (use setCell with = prefix)");
    }

    // Handle cell.format = "FMT_C002" or cell.format = "<base64>" or cell.format = nil
    if (strcmp(key, "format") == 0) {
        // Get the cell UUID from the table
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "format: invalid cell object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        // Get context
        Workbook* workbook = getWorkbook(L);
        Sheet* sheet = getSheet(L);
        if (sheet == nullptr || workbook == nullptr) {
            luaL_error(L, "format: no context set");
        }

        const ID cellId(uuidStr);
        const Cell* cell = sheet->getCell(cellId);
        if (cell == nullptr) {
            luaL_error(L, "format: cell not found");
        }

        // Handle nil to clear format
        std::string formatBase64;
        if (lua_isstring(L, 3) != 0) {
            const std::string formatStr = lua_tostring(L, 3);

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
                    luaL_error(L, "cell.format: invalid format string");
                    return 0;
                }
            }
        } else if (lua_isnil(L, 3) == 0) {
            luaL_error(L, "cell.format: expected string or nil");
            return 0;
        }
        // nil clears format (empty base64)

        if (formatBase64.empty()) {
            const Operation op = makeCellClearFormatOp(*workbook, cell->id);
            applyOperation(*workbook, op);
        } else {
            auto maybeFormat = FormatBuffer::fromBase64(formatBase64);
            if (maybeFormat.has_value()) {
                const Operation op = makeCellSetFormatOp(*workbook, cell->id, *maybeFormat);
                applyOperation(*workbook, op);
            }
        }
        return 0;
    }

    // Handle cell.style = {bold=true, ...} or cell.style = nil
    if (strcmp(key, "style") == 0) {
        // Get the cell UUID from the table
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "style: invalid cell object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        // Get context
        Workbook* workbook = getWorkbook(L);
        Sheet* sheet = getSheet(L);
        if (sheet == nullptr || workbook == nullptr) {
            luaL_error(L, "style: no context set");
        }

        const ID cellId(uuidStr);
        const Cell* cell = sheet->getCell(cellId);
        if (cell == nullptr) {
            luaL_error(L, "style: cell not found");
        }

        // Handle nil - clear style
        if (lua_isnil(L, 3) != 0) {
            const Operation op = makeCellClearStyleOp(*workbook, cell->id);
            applyOperation(*workbook, op);
            return 0;
        }

        // Expect a table with style properties
        if (lua_istable(L, 3) == 0) {
            luaL_error(L, "cell.style: expected table or nil");
        }

        // Build style from table
        CellStyle style;

        // Get existing style to merge with (if any) - read from workbook entity styles
        const StyleBuffer* existingStyle = workbook->getEntityStyle(cell->id);
        if (existingStyle != nullptr) {
            style = existingStyle->toCellStyle();
        }

        // Merge provided properties, setting defined flags for provided properties
        lua_getfield(L, 3, "bold");
        if (lua_isboolean(L, -1) != 0) {
            style.bold = lua_toboolean(L, -1) != 0;
            style.setDefined(DEFINED_BOLD);
        }
        lua_pop(L, 1);

        lua_getfield(L, 3, "italic");
        if (lua_isboolean(L, -1) != 0) {
            style.italic = lua_toboolean(L, -1) != 0;
            style.setDefined(DEFINED_ITALIC);
        }
        lua_pop(L, 1);

        lua_getfield(L, 3, "underline");
        if (lua_isboolean(L, -1) != 0) {
            style.underline = lua_toboolean(L, -1) != 0;
            style.setDefined(DEFINED_UNDERLINE);
        }
        lua_pop(L, 1);

        lua_getfield(L, 3, "wrapText");
        if (lua_isboolean(L, -1) != 0) {
            style.wrapText = lua_toboolean(L, -1) != 0;
            style.setDefined(DEFINED_WRAPTEXT);
        }
        lua_pop(L, 1);

        lua_getfield(L, 3, "bgColor");
        if (lua_isstring(L, -1) != 0) {
            style.bgColor = lua_tostring(L, -1);
            style.setDefined(DEFINED_BGCOLOR);
        } else if (lua_isnil(L, -1) != 0) {
            style.bgColor.clear();
            style.setDefined(DEFINED_BGCOLOR);  // Explicitly cleared
        }
        lua_pop(L, 1);

        lua_getfield(L, 3, "textColor");
        if (lua_isstring(L, -1) != 0) {
            style.textColor = lua_tostring(L, -1);
            style.setDefined(DEFINED_TEXTCOLOR);
        } else if (lua_isnil(L, -1) != 0) {
            style.textColor.clear();
            style.setDefined(DEFINED_TEXTCOLOR);  // Explicitly cleared
        }
        lua_pop(L, 1);

        lua_getfield(L, 3, "fontFamily");
        if (lua_isstring(L, -1) != 0) {
            style.fontFamily = lua_tostring(L, -1);
            style.setDefined(DEFINED_FONTFAMILY);
        } else if (lua_isnil(L, -1) != 0) {
            style.fontFamily.clear();
            style.setDefined(DEFINED_FONTFAMILY);  // Explicitly cleared
        }
        lua_pop(L, 1);

        lua_getfield(L, 3, "fontSize");
        if (lua_isnumber(L, -1) != 0) {
            style.fontSize = static_cast<uint8_t>(lua_tonumber(L, -1));
            style.setDefined(DEFINED_FONTSIZE);
        }
        lua_pop(L, 1);

        lua_getfield(L, 3, "hAlign");
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

        lua_getfield(L, 3, "vAlign");
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
            const Operation op = makeCellClearStyleOp(*workbook, cell->id);
            applyOperation(*workbook, op);
            return 0;
        }

        // Convert CellStyle to content-addressed StyleBuffer
        const StyleBuffer styleBuf = StyleBuffer::fromCellStyle(style);

        // Apply CELL_SET_STYLE operation with content-addressed style
        const Operation styleOp = makeCellSetStyleOp(*workbook, cell->id, styleBuf);
        applyOperation(*workbook, styleOp);

        return 0;
    }

    // For other keys, set in the table itself
    lua_rawset(L, 1);
    return 0;
}

// ============================================================================
// Cell __tostring metamethod: returns "Cell<A1>" format
// ============================================================================
int LuauSandbox::luaCellToString(lua_State* L) {
    // Stack: [1] = cell table
    // Get the cell UUID from the table
    lua_getfield(L, 1, "_uuid");
    if (lua_isstring(L, -1) == 0) {
        lua_pushstring(L, "Cell<invalid>");
        return 1;
    }
    const char* uuidStr = lua_tostring(L, -1);
    lua_pop(L, 1);

    // Get context
    Sheet* sheet = getSheet(L);
    if (sheet == nullptr) {
        lua_pushstring(L, "Cell<no context>");
        return 1;
    }

    const ID cellId(uuidStr);
    const Cell* cell = sheet->getCell(cellId);
    if (cell == nullptr) {
        lua_pushstring(L, "Cell<not found>");
        return 1;
    }

    // Get the cell's current position
    const Axis* col = sheet->getColumn(cell->colId);
    const Axis* row = sheet->getRow(cell->rowId);
    if (col == nullptr || row == nullptr) {
        lua_pushstring(L, "Cell<no position>");
        return 1;
    }

    // Convert to A1 notation
    const std::string a1Ref =
        RefConverter::columnIndexToLetter(col->position) + std::to_string(row->position + 1);
    const std::string result = "Cell<" + a1Ref + ">";
    lua_pushstring(L, result.c_str());
    return 1;
}

// ============================================================================
// Sheet __tostring metamethod: returns "Sheet<SheetName>" format
// ============================================================================
int LuauSandbox::luaSheetToString(lua_State* L) {
    // Stack: [1] = sheet table
    // Get the sheet UUID from the table
    lua_getfield(L, 1, "_uuid");
    if (lua_isstring(L, -1) == 0) {
        lua_pushstring(L, "Sheet<invalid>");
        return 1;
    }
    const char* uuidStr = lua_tostring(L, -1);
    lua_pop(L, 1);

    // Get context
    Workbook* workbook = getWorkbook(L);
    if (workbook == nullptr) {
        lua_pushstring(L, "Sheet<no context>");
        return 1;
    }

    const ID sheetId(uuidStr);
    const Sheet* sheet = workbook->getSheet(sheetId);
    if (sheet == nullptr) {
        lua_pushstring(L, "Sheet<not found>");
        return 1;
    }

    const std::string result = "Sheet<" + sheet->name + ">";
    lua_pushstring(L, result.c_str());
    return 1;
}

// ============================================================================
// Sheet __index metamethod: handles property access (e.g., sheet.name)
// ============================================================================
int LuauSandbox::luaSheetIndex(lua_State* L) {
    // Stack: [1] = sheet table, [2] = key (string)
    const char* key = lua_tostring(L, 2);
    if (key == nullptr) {
        lua_pushnil(L);
        return 1;
    }

    // Handle .name property
    if (strcmp(key, "name") == 0) {
        // Get the sheet UUID from the table
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "name: invalid sheet object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        Workbook* workbook = getWorkbook(L);
        if (workbook == nullptr) {
            luaL_error(L, "name: no context set");
        }

        const ID sheetId(uuidStr);
        const Sheet* sheet = workbook->getSheet(sheetId);
        if (sheet == nullptr) {
            luaL_error(L, "name: sheet not found");
        }

        lua_pushstring(L, sheet->name.c_str());
        return 1;
    }

    // Handle .gridLines property (showGridLines)
    if (strcmp(key, "gridLines") == 0) {
        // Get the sheet UUID from the table
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "gridLines: invalid sheet object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        Workbook* workbook = getWorkbook(L);
        if (workbook == nullptr) {
            luaL_error(L, "gridLines: no context set");
        }

        const ID sheetId(uuidStr);
        const Sheet* sheet = workbook->getSheet(sheetId);
        if (sheet == nullptr) {
            luaL_error(L, "gridLines: sheet not found");
        }

        lua_pushboolean(L, sheet->showGridLines ? 1 : 0);
        return 1;
    }

    // Handle .zoomScale property
    if (strcmp(key, "zoomScale") == 0) {
        // Get the sheet UUID from the table
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "zoomScale: invalid sheet object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        Workbook* workbook = getWorkbook(L);
        if (workbook == nullptr) {
            luaL_error(L, "zoomScale: no context set");
        }

        const ID sheetId(uuidStr);
        const Sheet* sheet = workbook->getSheet(sheetId);
        if (sheet == nullptr) {
            luaL_error(L, "zoomScale: sheet not found");
        }

        lua_pushinteger(L, sheet->zoomScale);
        return 1;
    }

    // For other keys, look up in the table itself
    lua_rawget(L, 1);
    return 1;
}

// ============================================================================
// Sheet __newindex metamethod: handles property assignment (e.g., sheet.name = "x")
// ============================================================================
int LuauSandbox::luaSheetNewIndex(lua_State* L) {
    // Stack: [1] = sheet table, [2] = key (string), [3] = value
    const char* key = lua_tostring(L, 2);
    if (key == nullptr) {
        luaL_error(L, "invalid property name");
    }

    // Handle .name = "x" assignment
    if (strcmp(key, "name") == 0) {
        if (lua_isstring(L, 3) == 0) {
            luaL_error(L, "sheet.name must be a string");
        }
        const char* newName = lua_tostring(L, 3);

        // Get the sheet UUID from the table
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "name: invalid sheet object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        Workbook* workbook = getWorkbook(L);
        if (workbook == nullptr) {
            luaL_error(L, "name: no context set");
        }

        const ID sheetId(uuidStr);
        const Sheet* sheet = workbook->getSheet(sheetId);
        if (sheet == nullptr) {
            luaL_error(L, "name: sheet not found");
        }

        // Apply rename operation
        const std::string payload = R"({"name":")" + jsonEscape(newName) + R"("})";
        const Operation op = makeSheetSetOp(*workbook, sheet->id, payload);
        applyOperation(*workbook, op);

        return 0;
    }

    // Handle .gridLines = true/false assignment
    if (strcmp(key, "gridLines") == 0) {
        if (lua_isboolean(L, 3) == 0) {
            luaL_error(L, "sheet.gridLines must be a boolean");
        }
        const bool showGridLines = lua_toboolean(L, 3) != 0;

        // Get the sheet UUID from the table
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "gridLines: invalid sheet object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        Workbook* workbook = getWorkbook(L);
        if (workbook == nullptr) {
            luaL_error(L, "gridLines: no context set");
        }

        const ID sheetId(uuidStr);
        Sheet* sheet = workbook->getSheet(sheetId);
        if (sheet == nullptr) {
            luaL_error(L, "gridLines: sheet not found");
        }

        // Directly set the property (no CRDT operation for view properties yet)
        sheet->showGridLines = showGridLines;

        return 0;
    }

    // Handle .zoomScale = number assignment
    if (strcmp(key, "zoomScale") == 0) {
        if (lua_isnumber(L, 3) == 0) {
            luaL_error(L, "sheet.zoomScale must be a number (10-400)");
        }
        int zoom = static_cast<int>(lua_tonumber(L, 3));

        // Clamp to valid range
        if (zoom < 10) {
            zoom = 10;
        }
        if (zoom > 400) {
            zoom = 400;
        }

        // Get the sheet UUID from the table
        lua_getfield(L, 1, "_uuid");
        if (lua_isstring(L, -1) == 0) {
            luaL_error(L, "zoomScale: invalid sheet object");
        }
        const char* uuidStr = lua_tostring(L, -1);
        lua_pop(L, 1);

        Workbook* workbook = getWorkbook(L);
        if (workbook == nullptr) {
            luaL_error(L, "zoomScale: no context set");
        }

        const ID sheetId(uuidStr);
        Sheet* sheet = workbook->getSheet(sheetId);
        if (sheet == nullptr) {
            luaL_error(L, "zoomScale: sheet not found");
        }

        // Directly set the property (no CRDT operation for view properties yet)
        sheet->zoomScale = static_cast<uint16_t>(zoom);

        return 0;
    }

    // For other keys, set in the table itself
    lua_rawset(L, 1);
    return 0;
}

// ============================================================================
// Helper: Create and push a sheet object
// ============================================================================
void LuauSandbox::pushSheetObject(lua_State* L, Sheet* sheet) const {
    if (sheet == nullptr) {
        lua_pushnil(L);
        return;
    }

    const std::string sheetUuid = sheet->id.toString();

    // Create new sheet object table
    lua_newtable(L);

    // Store UUID for identity tracking (hidden field)
    lua_pushstring(L, sheetUuid.c_str());
    lua_setfield(L, -2, "_uuid");

    // Apply Sheet metatable for .name property access
    if (sheetMetatableRef_ != -1) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, sheetMetatableRef_);  // Get Sheet metatable
        lua_setmetatable(L, -2);                                // setmetatable(sheet, Sheet)
    }
}

// ============================================================================
// Helper: Create and push a cell object
// Cell objects only store _uuid - all properties (value, formula, ref) are
// fetched dynamically from the Workbook via __index metamethod
// ============================================================================
void LuauSandbox::pushCellObject(lua_State* L, Cell* cell) const {
    if (cell == nullptr) {
        lua_pushnil(L);
        return;
    }

    const std::string cellUuid = cell->id.toString();

    // Check if we have a cached object (for identity: a == b)
    if (cellCacheRef_ != -1) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cellCacheRef_);  // Get cache table
        lua_getfield(L, -1, cellUuid.c_str());             // Get cached object
        if (lua_istable(L, -1) != 0) {
            // Return cached object (properties fetched dynamically via __index)
            lua_remove(L, -2);  // Remove cache table, keep cell object
            return;
        }
        lua_pop(L, 2);  // Pop nil and cache table
    }

    // Create new cell object table
    lua_newtable(L);

    // Store UUID for identity tracking (only persistent field)
    lua_pushstring(L, cellUuid.c_str());
    lua_setfield(L, -2, "_uuid");

    // Apply Cell metatable for property access via __index/__newindex
    if (cellMetatableRef_ != -1) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cellMetatableRef_);  // Get Cell metatable
        lua_setmetatable(L, -2);                               // setmetatable(cell, Cell)
    }

    // Cache the object for identity (if cache exists)
    if (cellCacheRef_ != -1) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cellCacheRef_);  // Get cache table
        lua_pushvalue(L, -2);                              // Copy cell object
        lua_setfield(L, -2, cellUuid.c_str());             // cache[uuid] = cell
        lua_pop(L, 1);                                     // Pop cache table
    }
}

}  // namespace cells
