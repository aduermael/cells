// =============================================================================
// CRDT Cell Operations
// =============================================================================
//
// Implementation of cell-level CRDT operations for the spreadsheet model.
// Cells are the atomic data units that store values, formulas, and formatting.
//
// Key responsibilities:
// - Apply CELL_SET operations (creates/updates cells with value, style, format)
// - Apply CELL_DELETE operations (cell deletion)
// - Handle conflict resolution via Last-Writer-Wins (LWW)
// - Manage formula dependencies and format inheritance
//
// All cell mutations go through these functions to ensure CRDT consistency.
// Remote operations are applied identically to local ones.
//
// =============================================================================

#include "core/cells/crdt_internal.h"
#include "core/cells/dependency_graph.h"
#include "core/cells/format_buffer.h"
#include "core/cells/formula_parser.h"
#include "core/cells/number_format.h"
#include "core/cells/style_buffer.h"

namespace cells {
namespace internal {

// Create a workbook-level position resolver for cross-sheet formula dependencies
// Returns (col, row) position for a cell ID from ANY sheet in the workbook
static PositionResolver makeWorkbookPositionResolver(Workbook* workbook) {
    return [workbook](const ID& cellId) -> std::pair<int32_t, int32_t> {
        if (workbook == nullptr) {
            return {-1, -1};
        }

        // Look up cell from workbook-level storage
        const Cell* cell = workbook->getCell(cellId);
        if (cell == nullptr) {
            // Maybe it's a column or row ID, not a cell ID
            const Axis* col = workbook->getColumn(cellId);
            if (col != nullptr) {
                return {static_cast<int32_t>(col->position), -1};
            }
            const Axis* row = workbook->getRow(cellId);
            if (row != nullptr) {
                return {-1, static_cast<int32_t>(row->position)};
            }
            return {-1, -1};
        }

        // Get column and row from workbook-level storage
        const Axis* col = workbook->getColumn(cell->colId);
        const Axis* row = workbook->getRow(cell->rowId);
        if (col == nullptr || row == nullptr) {
            return {-1, -1};
        }

        return {static_cast<int32_t>(col->position), static_cast<int32_t>(row->position)};
    };
}

ApplyResult applyCellSet(Workbook& workbook, const Operation& op) {
    // Find the target cell from workbook-level storage
    auto result = workbook.findCell(op.target_id);
    Cell* cell = result.cell;
    Sheet* targetSheet = result.sheet;

    // Check if there's a newer operation for this cell
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);

    if (!latest.isNull() && latest.hlc >= op.hlc) {
        // This operation is older than or equal to existing, skip it
        // (but still add to OpLog for history)
        return ApplyResult::SUPERSEDED;
    }

    // Parse payload - unified format:
    // {"col":"colId","row":"rowId","t":"n","v":"42","sty":"base64","fmt":"base64"}
    // Only include properties being set (sparse updates)
    const std::string col_id_str = extractJSONString(op.payload, "col");
    const std::string row_id_str = extractJSONString(op.payload, "row");
    const std::string type_str = extractJSONString(op.payload, "t");
    const std::string value_str = extractJSONString(op.payload, "v");
    const std::string style_str = extractJSONString(op.payload, "sty");
    const std::string format_str = extractJSONString(op.payload, "fmt");

    if (cell == nullptr) {
        // Cell doesn't exist - create it if we have col and row
        if (col_id_str.empty() || row_id_str.empty()) {
            return ApplyResult::INVALID_PAYLOAD;  // Missing required col/row for cell creation
        }

        targetSheet = ensureSheetForOp(workbook, op);
        if (targetSheet == nullptr) {
            return ApplyResult::INVALID_TARGET;
        }

        const ID colId(col_id_str);
        const ID rowId(row_id_str);

        // Verify the column and row exist
        if (targetSheet->getColumn(colId) == nullptr || targetSheet->getRow(rowId) == nullptr) {
            return ApplyResult::INVALID_TARGET;
        }

        // Create the cell with the SAME ID as in the operation
        auto newCell = std::make_unique<Cell>(op.target_id, colId, rowId);
        cell = newCell.get();
        targetSheet->addCell(std::move(newCell));
    }

    // Apply value if type is provided
    if (!type_str.empty()) {
        const CellValueType type = charToValueType(type_str[0]);
        cell->value.type = type;
        cell->value.error = CellError::NONE;

        if (type == CellValueType::FORMULA) {
            // Clear old formula dependencies before setting new formula
            DependencyGraph* depGraph = workbook.getDependencyGraph();
            if (depGraph != nullptr) {
                depGraph->removeFormula(cell->id);
            }

            // Parse the UUID formula text to create the AST
            FormulaParser parser(value_str);
            std::unique_ptr<ASTNode> ast = parser.parse();

            // Create the formula object with AST
            auto* formula = new Formula();
            formula->ast = ast.release();
            formula->dirty = true;

            // Add to dependency graph for recalculation tracking if we have valid AST
            if (formula->ast != nullptr && targetSheet != nullptr) {
                DependencyGraph* depGraph = workbook.getDependencyGraph();
                if (depGraph != nullptr) {
                    depGraph->addFormula(cell->id, formula->ast,
                                         makeWorkbookPositionResolver(&workbook));

                    if (formula->hasVolatile()) {
                        depGraph->markVolatile(cell->id);
                    }
                }
            }

            cell->setFormula(formula);
            cell->value.raw = "";

            // Format inheritance for formulas
            const bool hasFormat = workbook.hasEntityFormat(cell->id);
            if (!hasFormat && formula->ast != nullptr && format_str.empty()) {
                const FormatLookup formatLookup =
                    [&workbook](const std::string& cellIdStr) -> std::string {
                    const ID cellId(cellIdStr);
                    const Cell* refCell = workbook.getCell(cellId);
                    if (refCell == nullptr) {
                        return "";
                    }
                    const FormatBuffer* fmt = workbook.getEntityFormat(refCell->id);
                    if (fmt == nullptr || fmt->isEmpty()) {
                        return "";
                    }
                    return fmt->toFormatCode();
                };

                const std::string inheritedFormatCode =
                    inferFormatFromFormula(formula->ast, formatLookup);
                if (!inheritedFormatCode.empty()) {
                    auto maybeFormat = FormatBuffer::fromFormatCode(inheritedFormatCode);
                    if (maybeFormat.has_value() && !maybeFormat->isEmpty()) {
                        workbook.setEntityFormat(cell->id, *maybeFormat);
                        cell->markHasFormat();
                    }
                }
            }
        } else {
            // Clear formula if it was a formula cell
            if (cell->formula != nullptr) {
                DependencyGraph* depGraph = workbook.getDependencyGraph();
                if (depGraph != nullptr) {
                    depGraph->removeFormula(cell->id);
                    depGraph->unmarkVolatile(cell->id);
                }
                cell->clearFormula();
            }
            cell->value.raw = value_str;
        }
    }

    // Apply style if provided
    if (!style_str.empty()) {
        auto maybeStyle = StyleBuffer::fromBase64(style_str);
        if (maybeStyle.has_value() && !maybeStyle->isEmpty()) {
            workbook.setEntityStyle(cell->id, *maybeStyle);
            cell->markHasStyle();
        }
    } else if (op.payload.find("\"sty\":\"\"") != std::string::npos) {
        // Explicit clear style
        workbook.clearEntityStyle(cell->id);
        cell->clearHasStyle();
    }

    // Apply format if provided
    if (!format_str.empty()) {
        auto maybeFormat = FormatBuffer::fromBase64(format_str);
        if (maybeFormat.has_value() && !maybeFormat->isEmpty()) {
            workbook.setEntityFormat(cell->id, *maybeFormat);
            cell->markHasFormat();
        }
    } else if (op.payload.find("\"fmt\":\"\"") != std::string::npos) {
        // Explicit clear format
        workbook.clearEntityFormat(cell->id);
        cell->clearHasFormat();
    }

    return ApplyResult::SUCCESS;
}

ApplyResult applyCellDelete(Workbook& workbook, const Operation& op) {
    // Find the target cell from workbook-level storage
    auto result = workbook.findCell(op.target_id);
    const Cell* cell = result.cell;
    Sheet* targetSheet = result.sheet;

    if (cell == nullptr || targetSheet == nullptr) {
        // Cell doesn't exist - nothing to delete
        // Still return SUCCESS so the operation is recorded in OpLog
        return ApplyResult::SUCCESS;
    }

    // Check for newer operations
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);

    if (!latest.isNull() && latest.hlc > op.hlc) {
        // A newer operation exists - if it's a set, it resurrects
        if (latest.type == OpType::CELL_SET) {
            return ApplyResult::RESURRECTED;
        }
    }

    // Remove from dependency graph if it was a formula cell
    if (cell->isFormula()) {
        DependencyGraph* depGraph = workbook.getDependencyGraph();
        if (depGraph != nullptr) {
            depGraph->removeFormula(op.target_id);
            depGraph->unmarkVolatile(op.target_id);
        }
    }

    // Remove the cell from the sheet's position index and workbook storage
    targetSheet->removeCellFromIndex(op.target_id);
    workbook.removeCell(op.target_id);

    return ApplyResult::SUCCESS;
}

}  // namespace internal
}  // namespace cells
