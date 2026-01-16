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
#include "core/cells/style_registry.h"

namespace cells {
namespace internal {

// Create a position resolver for a Sheet
// Returns (col, row) position for a cell ID, or (-1, -1) if not found
static PositionResolver makePositionResolver(Sheet* sheet) {
    return [sheet](const ID& cellId) -> std::pair<int32_t, int32_t> {
        if (sheet == nullptr) {
            return {-1, -1};
        }

        const Cell* cell = sheet->getCell(cellId);
        if (cell == nullptr) {
            // Maybe it's a column or row ID, not a cell ID
            const Axis* col = sheet->getColumn(cellId);
            if (col != nullptr) {
                return {static_cast<int32_t>(col->position), -1};
            }
            const Axis* row = sheet->getRow(cellId);
            if (row != nullptr) {
                return {-1, static_cast<int32_t>(row->position)};
            }
            return {-1, -1};
        }

        const Axis* col = sheet->getColumn(cell->colId);
        const Axis* row = sheet->getRow(cell->rowId);
        if (col == nullptr || row == nullptr) {
            return {-1, -1};
        }

        return {static_cast<int32_t>(col->position), static_cast<int32_t>(row->position)};
    };
}

ApplyResult applyCellSetValue(Workbook& workbook, const Operation& op) {
    // Find the target cell across all sheets
    Cell* cell = nullptr;
    Sheet* targetSheet = nullptr;

    for (auto& s : workbook.sheets) {
        cell = s->getCell(op.target_id);
        if (cell != nullptr) {
            targetSheet = s.get();
            break;
        }
    }

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
        if (targetSheet != nullptr) {
            DependencyGraph* depGraph = targetSheet->getDependencyGraph();
            if (depGraph != nullptr) {
                depGraph->removeFormula(cell->id);
            }
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
            DependencyGraph* depGraph = targetSheet->getDependencyGraph();
            if (depGraph != nullptr) {
                depGraph->addFormula(cell->id, formula->ast, makePositionResolver(targetSheet));

                // Track volatile functions
                if (formula->hasVolatile()) {
                    depGraph->markVolatile(cell->id);
                }
            }
        }

        cell->setFormula(formula);

        // Store result value in raw for display (not the formula text)
        cell->value.raw = "";

        // Phase 7: Format inheritance
        // If cell has GENERAL format, inherit format from referenced cells
        const std::string currentFormat = cell->formatId.toString();
        const bool isGeneralFormat =
            currentFormat.empty() || currentFormat == "~" || currentFormat == "FMT_GEN0";

        if (isGeneralFormat && formula->ast != nullptr && targetSheet != nullptr) {
            // Create a format lookup for this sheet
            const FormatLookup formatLookup =
                [targetSheet](const std::string& cellIdStr) -> std::string {
                const ID cellId(cellIdStr);
                const Cell* refCell = targetSheet->getCell(cellId);
                if (refCell == nullptr) {
                    return "";
                }
                return refCell->formatId.toString();
            };

            const std::string inheritedFormat = inferFormatFromFormula(formula->ast, formatLookup);
            if (!inheritedFormat.empty()) {
                cell->formatId = ID(inheritedFormat);
            }
        }
    } else {
        // Clear formula if it was a formula cell
        if (cell->formula != nullptr) {
            // Remove from dependency graph first
            if (targetSheet != nullptr) {
                DependencyGraph* depGraph = targetSheet->getDependencyGraph();
                if (depGraph != nullptr) {
                    depGraph->removeFormula(cell->id);
                    depGraph->unmarkVolatile(cell->id);
                }
            }
            cell->clearFormula();
        }
        cell->value.raw = value_str;
    }

    return ApplyResult::SUCCESS;
}

ApplyResult applyCellSetFormat(Workbook& workbook, const Operation& op) {
    // Find the target cell across all sheets
    Cell* cell = nullptr;

    for (auto& s : workbook.sheets) {
        cell = s->getCell(op.target_id);
        if (cell != nullptr) {
            break;
        }
    }

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

    // Set the format ID (null ID "~" means clear format / use default)
    cell->formatId = ID(formatIdStr);

    return ApplyResult::SUCCESS;
}

ApplyResult applyCellSetStyle(Workbook& workbook, const Operation& op) {
    // Find the target cell across all sheets
    Cell* cell = nullptr;

    for (auto& s : workbook.sheets) {
        cell = s->getCell(op.target_id);
        if (cell != nullptr) {
            break;
        }
    }

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

    // Parse payload: {"style_id":"STY_abc123"}
    const std::string styleIdStr = extractJSONString(op.payload, "style_id");
    if (styleIdStr.empty()) {
        return ApplyResult::INVALID_PAYLOAD;
    }

    const ID newStyleId(styleIdStr);
    StyleRegistry* registry = workbook.getStyleRegistry();

    // Release old style reference (if any)
    if (!cell->styleId.isNull() && registry != nullptr) {
        registry->release(cell->styleId);
    }

    // Set the style ID (null ID "~" means clear style / use default)
    cell->styleId = newStyleId;

    // Add reference to new style (if not null)
    if (!newStyleId.isNull() && registry != nullptr) {
        registry->addRef(newStyleId);
    }

    return ApplyResult::SUCCESS;
}

ApplyResult applyCellClear(Workbook& workbook, const Operation& op) {
    Sheet* targetSheet = nullptr;
    const Cell* cell = nullptr;

    for (auto& s : workbook.sheets) {
        cell = s->getCell(op.target_id);
        if (cell != nullptr) {
            targetSheet = s.get();
            break;
        }
    }

    if (targetSheet == nullptr) {
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
        DependencyGraph* depGraph = targetSheet->getDependencyGraph();
        if (depGraph != nullptr) {
            depGraph->removeFormula(op.target_id);
            depGraph->unmarkVolatile(op.target_id);
        }
    }

    // Remove the cell from the sheet entirely
    targetSheet->cells.erase(op.target_id);

    return ApplyResult::SUCCESS;
}

}  // namespace internal
}  // namespace cells
