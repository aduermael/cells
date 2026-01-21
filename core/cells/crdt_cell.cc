// =============================================================================
// CRDT Cell Operations
// =============================================================================
//
// Implementation of cell-level CRDT operations for the spreadsheet model.
// Cells are the atomic data units that store values, formulas, and formatting.
//
// Key responsibilities:
// - Apply CELL_SET_VALUE operations (numbers, strings, formulas)
// - Apply CELL_SET_FORMAT operations (number format assignment)
// - Apply CELL_CLEAR operations (cell deletion)
// - Handle conflict resolution via Last-Writer-Wins (LWW)
// - Manage formula dependencies and format inheritance
//
// All cell mutations go through these functions to ensure CRDT consistency.
// Remote operations are applied identically to local ones.
//
// =============================================================================

#include "core/cells/crdt_internal.h"
#include "core/cells/dependency_graph.h"
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

ApplyResult applyCellSetValue(Workbook& workbook, const Operation& op) {
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

    // Parse payload: {"type":"n","value":"42","col_id":"abc123","row_id":"def456"}
    const std::string type_str = extractJSONString(op.payload, "type");
    const std::string value_str = extractJSONString(op.payload, "value");
    const std::string col_id_str = extractJSONString(op.payload, "col_id");
    const std::string row_id_str = extractJSONString(op.payload, "row_id");

    if (type_str.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    if (cell == nullptr) {
        // Cell doesn't exist - create it if we have col_id and row_id
        if (col_id_str.empty() || row_id_str.empty()) {
            return ApplyResult::INVALID_TARGET;
        }

        if (workbook.sheets.empty()) {
            return ApplyResult::INVALID_TARGET;
        }

        // Use sheetId from operation if available, otherwise fall back to first sheet
        if (!op.sheetId.isNull()) {
            targetSheet = workbook.getSheet(op.sheetId);
        }
        if (targetSheet == nullptr) {
            targetSheet = workbook.sheets[0].get();
        }

        const ID colId(col_id_str);
        const ID rowId(row_id_str);

        // Verify the column and row exist (they should have been created by COL_INSERT/ROW_INSERT)
        if (targetSheet->getColumn(colId) == nullptr || targetSheet->getRow(rowId) == nullptr) {
            return ApplyResult::INVALID_TARGET;
        }

        // Create the cell with the SAME ID as in the operation
        auto newCell = std::make_unique<Cell>(op.target_id, colId, rowId);
        cell = newCell.get();
        targetSheet->addCell(std::move(newCell));
    }

    // Apply the value based on type
    const CellValueType type = charToValueType(type_str[0]);
    cell->value.type = type;
    cell->value.error = CellError::NONE;

    if (type == CellValueType::FORMULA) {
        // For formulas: value_str contains UUID formula, display contains A1 formula (ignored)
        // Note: display field is ignored - we generate display strings from AST

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
                // Use workbook-level position resolver to handle cross-sheet range deps
                // This allows the R-tree to be populated with positions from ANY sheet
                depGraph->addFormula(cell->id, formula->ast,
                                     makeWorkbookPositionResolver(&workbook));

                // Track volatile functions
                if (formula->hasVolatile()) {
                    depGraph->markVolatile(cell->id);
                }
            }

            // NOTE: Cross-sheet dependency tracking via extractCrossSheetRefs() is no longer
            // needed. With Phase 13 changes, formula storage no longer includes sheet prefixes,
            // so extractCrossSheetRefs() returns empty. All dependencies (including cross-sheet)
            // are now tracked through the global dep graph:
            // - Direct cell refs: via reverseDeps_ (cell ID -> dependent formulas)
            // - Range refs: via R-tree (positions come from workbook-level axis storage)
        }

        cell->setFormula(formula);

        // Store result value in raw for display (not the formula text)
        cell->value.raw = "";

        // Phase 7: Format inheritance
        // If cell has GENERAL format, inherit format from referenced cells
        const ID currentFormatId = workbook.getFormatId(cell->id);
        const std::string currentFormat = currentFormatId.toString();
        const bool isGeneralFormat =
            currentFormat.empty() || currentFormat == "~" || currentFormat == "FMT_GEN0";

        if (isGeneralFormat && formula->ast != nullptr) {
            // Create a format lookup using workbook-level cell storage
            const FormatLookup formatLookup =
                [&workbook](const std::string& cellIdStr) -> std::string {
                const ID cellId(cellIdStr);
                const Cell* refCell = workbook.getCell(cellId);
                if (refCell == nullptr) {
                    return "";
                }
                return workbook.getFormatId(refCell->id).toString();
            };

            const std::string inheritedFormat = inferFormatFromFormula(formula->ast, formatLookup);
            if (!inheritedFormat.empty()) {
                const ID inheritedFormatId(inheritedFormat);
                workbook.setFormatId(cell->id, inheritedFormatId);
                cell->markHasFormat();
            }
        }
    } else {
        // Clear formula if it was a formula cell
        if (cell->formula != nullptr) {
            // Remove from dependency graph first
            DependencyGraph* depGraph = workbook.getDependencyGraph();
            if (depGraph != nullptr) {
                depGraph->removeFormula(cell->id);
                depGraph->unmarkVolatile(cell->id);
            }
            cell->clearFormula();
        }
        cell->value.raw = value_str;
    }

    return ApplyResult::SUCCESS;
}

ApplyResult applyCellSetFormat(Workbook& workbook, const Operation& op) {
    // Find the target cell from workbook-level storage
    Cell* cell = workbook.getCell(op.target_id);
    if (cell == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer format operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if (existing.type == OpType::CELL_SET_FORMAT && existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    // Parse payload: {"format_id":"FMT_C002"}
    const std::string formatIdStr = extractJSONString(op.payload, "format_id");
    if (formatIdStr.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const ID formatId(formatIdStr);

    // Store format in workbook-level map and update cell flag
    if (formatId.isNull()) {
        // Clear format - remove from map and clear flag
        workbook.setFormatId(cell->id, formatId);
        cell->clearHasFormat();
    } else {
        // Set format - store in map and set flag
        workbook.setFormatId(cell->id, formatId);
        cell->markHasFormat();
    }

    return ApplyResult::SUCCESS;
}

ApplyResult applyCellSetStyle(Workbook& workbook, const Operation& op) {
    // Find the target cell from workbook-level storage
    Cell* cell = workbook.getCell(op.target_id);
    if (cell == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }

    // Check for newer style operations
    const OpLog* oplog = workbook.getOpLog();
    auto ops = oplog->getOperationsForEntity(op.target_id);
    for (const auto& existing : ops) {
        if (existing.type == OpType::CELL_SET_STYLE && existing.hlc > op.hlc) {
            return ApplyResult::SUPERSEDED;
        }
    }

    // Parse payload: {"style":"<base64>"} (content-addressed)
    // Empty string clears the style
    const std::string styleBase64 = extractJSONString(op.payload, "style");
    if (styleBase64.empty()) {
        // Clear style
        workbook.clearEntityStyle(cell->id);
        cell->clearHasStyle();
    } else {
        // Parse and store content-addressed style
        auto maybeStyle = StyleBuffer::fromBase64(styleBase64);
        if (!maybeStyle.has_value() || maybeStyle->isEmpty()) {
            return ApplyResult::INVALID_PAYLOAD;
        }
        workbook.setEntityStyle(cell->id, *maybeStyle);
        cell->markHasStyle();
    }

    return ApplyResult::SUCCESS;
}

ApplyResult applyCellClear(Workbook& workbook, const Operation& op) {
    // Find the target cell from workbook-level storage
    auto result = workbook.findCell(op.target_id);
    const Cell* cell = result.cell;
    Sheet* targetSheet = result.sheet;

    if (cell == nullptr || targetSheet == nullptr) {
        // Cell doesn't exist - nothing to clear
        // Still return SUCCESS so the operation is recorded in OpLog
        return ApplyResult::SUCCESS;
    }

    // Check for newer operations
    const OpLog* oplog = workbook.getOpLog();
    const Operation latest = oplog->getLatestOperationForEntity(op.target_id);

    if (!latest.isNull() && latest.hlc > op.hlc) {
        // A newer operation exists - if it's an edit, it resurrects
        if (latest.type == OpType::CELL_SET_VALUE) {
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
