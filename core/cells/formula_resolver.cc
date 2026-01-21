#include "core/cells/formula_resolver.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "core/cells/id.h"

namespace cells {

// ===========================================================================
// FormulaResolver
// ===========================================================================

FormulaResolver::FormulaResolver(Workbook& workbook, Sheet& sheet, NamedRangeRegistry* namedRanges)
    : _workbook(workbook), _sheet(sheet), _namedRanges(namedRanges) {}

ResolveResult FormulaResolver::resolve(ASTNode* ast, bool existingOnly) {
    if (ast == nullptr) {
        return ResolveResult::error("Null AST");
    }
    _existingOnly = existingOnly;
    return resolveNode(ast);
}

std::vector<ReferenceInfo> FormulaResolver::extractReferences(const ASTNode* ast) const {
    std::vector<ReferenceInfo> refs;
    if (ast != nullptr) {
        extractReferencesFromNode(ast, refs);
    }
    return refs;
}

bool FormulaResolver::containsVolatileFunction(const ASTNode* ast) {
    if (ast == nullptr) {
        return false;
    }

    switch (ast->type) {
        case ASTNodeType::FUNCTION_CALL: {
            auto* func = static_cast<const FunctionCallNode*>(ast);
            if (func->isVolatile || FunctionCallNode::isVolatileFunction(func->name)) {
                return true;
            }
            for (const auto& arg : func->args) {
                if (containsVolatileFunction(arg.get())) {
                    return true;
                }
            }
            return false;
        }
        case ASTNodeType::BINARY_OP: {
            auto* binOp = static_cast<const BinaryOpNode*>(ast);
            return containsVolatileFunction(binOp->left.get()) ||
                   containsVolatileFunction(binOp->right.get());
        }
        case ASTNodeType::UNARY_OP: {
            auto* unaryOp = static_cast<const UnaryOpNode*>(ast);
            return containsVolatileFunction(unaryOp->operand.get());
        }
        default:
            return false;
    }
}

ResolveResult FormulaResolver::resolveNode(ASTNode* node) {
    if (node == nullptr) {
        return ResolveResult::ok();
    }

    switch (node->type) {
        case ASTNodeType::NUMBER_LITERAL:
        case ASTNodeType::STRING_LITERAL:
        case ASTNodeType::BOOLEAN_LITERAL:
            return ResolveResult::ok();

        case ASTNodeType::CELL_REF:
            return resolveCellRef(static_cast<CellRefNode*>(node));

        case ASTNodeType::RANGE_REF:
            return resolveRangeRef(static_cast<RangeRefNode*>(node));

        case ASTNodeType::COLUMN_REF:
            return resolveColumnRef(static_cast<ColumnRefNode*>(node));

        case ASTNodeType::ROW_REF:
            return resolveRowRef(static_cast<RowRefNode*>(node));

        case ASTNodeType::COLUMN_RANGE_REF:
            return resolveColumnRangeRef(static_cast<ColumnRangeRefNode*>(node));

        case ASTNodeType::ROW_RANGE_REF:
            return resolveRowRangeRef(static_cast<RowRangeRefNode*>(node));

        case ASTNodeType::NAMED_REF:
            return resolveNamedRef(static_cast<NamedRefNode*>(node));

        case ASTNodeType::SPILL_RANGE_REF:
            // Resolve the anchor cell reference
            return resolveCellRef(static_cast<SpillRangeRefNode*>(node)->anchor.get());

        case ASTNodeType::BINARY_OP:
            return resolveBinaryOp(static_cast<BinaryOpNode*>(node));

        case ASTNodeType::UNARY_OP:
            return resolveUnaryOp(static_cast<UnaryOpNode*>(node));

        case ASTNodeType::FUNCTION_CALL:
            return resolveFunctionCall(static_cast<FunctionCallNode*>(node));

        case ASTNodeType::ERROR_NODE:
            // Error nodes are OK - they just represent parse errors
            return ResolveResult::ok();
    }

    return ResolveResult::error("Unknown AST node type", node->position);
}

ResolveResult FormulaResolver::resolveCellRef(CellRefNode* node) {
    // Get the target sheet
    Sheet* targetSheet = getTargetSheet(node->sheetName);
    if (targetSheet == nullptr) {
        return ResolveResult::error("Sheet not found: " + node->sheetName, node->position);
    }

    // Convert column name to position (0-indexed)
    const int32_t colPos = Sheet::columnNameToPosition(node->column);
    if (colPos < 0) {
        return ResolveResult::error("Invalid column: " + node->column, node->position);
    }

    // Row is 1-indexed in the AST, convert to 0-indexed position
    if (node->row < 1) {
        return ResolveResult::error("Invalid row: " + std::to_string(node->row), node->position);
    }
    const auto rowPos = static_cast<uint32_t>(node->row - 1);

    const Axis* col = nullptr;
    const Axis* row = nullptr;
    const Cell* cell = nullptr;

    if (_existingOnly) {
        // CRDT mode: only use existing entities, return error if not found
        col = targetSheet->getColumnByPosition(static_cast<uint32_t>(colPos));
        if (col == nullptr) {
            return ResolveResult::error(
                "Column not found at position " + std::to_string(colPos) +
                    ". Use getRequiredEntities() and create via CRDT first.",
                node->position);
        }
        row = targetSheet->getRowByPosition(rowPos);
        if (row == nullptr) {
            return ResolveResult::error(
                "Row not found at position " + std::to_string(rowPos) +
                    ". Use getRequiredEntities() and create via CRDT first.",
                node->position);
        }
        cell = targetSheet->getCellAtPosition(static_cast<uint32_t>(colPos), rowPos);
        if (cell == nullptr) {
            return ResolveResult::error(
                "Cell not found at " + node->column + std::to_string(node->row) +
                    ". Use getRequiredEntities() and create via CRDT first.",
                node->position);
        }
    } else {
        // Legacy mode: auto-create entities (bypasses CRDT - for file loading only)
        col = targetSheet->getOrCreateColumnByPosition(static_cast<uint32_t>(colPos));
        row = targetSheet->getOrCreateRowByPosition(rowPos);
        cell = targetSheet->getOrCreateCellAt(col->id, row->id);
    }

    // Store the cell ID
    node->cellId = cell->id.toString();

    // NOTE: We do NOT store sheetId for cell references anymore.
    // Cell UUIDs are globally unique, so the cell can be looked up directly
    // via workbook.findCell(). The sheet context is only needed for display
    // (derived dynamically from cell's column's sheetId).

    return ResolveResult::ok();
}

ResolveResult FormulaResolver::resolveRangeRef(RangeRefNode* node) {
    // Resolve both corner cells
    auto result = resolveCellRef(node->topLeft.get());
    if (!result.success) {
        return result;
    }

    return resolveCellRef(node->bottomRight.get());
}

ResolveResult FormulaResolver::resolveColumnRef(ColumnRefNode* node) {
    // Get the target sheet
    Sheet* targetSheet = getTargetSheet(node->sheetName);
    if (targetSheet == nullptr) {
        return ResolveResult::error("Sheet not found: " + node->sheetName, node->position);
    }

    // Convert column name to position (0-indexed)
    const int32_t colPos = Sheet::columnNameToPosition(node->column);
    if (colPos < 0) {
        return ResolveResult::error("Invalid column: " + node->column, node->position);
    }

    const Axis* col = nullptr;
    if (_existingOnly) {
        col = targetSheet->getColumnByPosition(static_cast<uint32_t>(colPos));
        if (col == nullptr) {
            return ResolveResult::error(
                "Column not found: " + node->column +
                    ". Use getRequiredEntities() and create via CRDT first.",
                node->position);
        }
    } else {
        col = targetSheet->getOrCreateColumnByPosition(static_cast<uint32_t>(colPos));
    }

    // Store the column ID
    node->columnId = col->id.toString();

    // Store the sheet ID for cross-sheet references
    if (!node->sheetName.empty() && targetSheet != &_sheet) {
        node->sheetId = targetSheet->id.toString();
    }

    return ResolveResult::ok();
}

ResolveResult FormulaResolver::resolveRowRef(RowRefNode* node) {
    // Get the target sheet
    Sheet* targetSheet = getTargetSheet(node->sheetName);
    if (targetSheet == nullptr) {
        return ResolveResult::error("Sheet not found: " + node->sheetName, node->position);
    }

    // Row is 1-indexed in the AST, convert to 0-indexed position
    if (node->row < 1) {
        return ResolveResult::error("Invalid row: " + std::to_string(node->row), node->position);
    }
    const auto rowPos = static_cast<uint32_t>(node->row - 1);

    const Axis* row = nullptr;
    if (_existingOnly) {
        row = targetSheet->getRowByPosition(rowPos);
        if (row == nullptr) {
            return ResolveResult::error(
                "Row not found: " + std::to_string(node->row) +
                    ". Use getRequiredEntities() and create via CRDT first.",
                node->position);
        }
    } else {
        row = targetSheet->getOrCreateRowByPosition(rowPos);
    }

    // Store the row ID
    node->rowId = row->id.toString();

    // Store the sheet ID for cross-sheet references
    if (!node->sheetName.empty() && targetSheet != &_sheet) {
        node->sheetId = targetSheet->id.toString();
    }

    return ResolveResult::ok();
}

ResolveResult FormulaResolver::resolveColumnRangeRef(ColumnRangeRefNode* node) {
    // Get the target sheet
    Sheet* targetSheet = getTargetSheet(node->sheetName);
    if (targetSheet == nullptr) {
        return ResolveResult::error("Sheet not found: " + node->sheetName, node->position);
    }

    // Convert column names to positions
    const int32_t startColPos = Sheet::columnNameToPosition(node->startColumn);
    const int32_t endColPos = Sheet::columnNameToPosition(node->endColumn);

    if (startColPos < 0) {
        return ResolveResult::error("Invalid column: " + node->startColumn, node->position);
    }
    if (endColPos < 0) {
        return ResolveResult::error("Invalid column: " + node->endColumn, node->position);
    }

    const Axis* startCol = nullptr;
    const Axis* endCol = nullptr;
    if (_existingOnly) {
        startCol = targetSheet->getColumnByPosition(static_cast<uint32_t>(startColPos));
        if (startCol == nullptr) {
            return ResolveResult::error(
                "Column not found: " + node->startColumn +
                    ". Use getRequiredEntities() and create via CRDT first.",
                node->position);
        }
        endCol = targetSheet->getColumnByPosition(static_cast<uint32_t>(endColPos));
        if (endCol == nullptr) {
            return ResolveResult::error(
                "Column not found: " + node->endColumn +
                    ". Use getRequiredEntities() and create via CRDT first.",
                node->position);
        }
    } else {
        startCol = targetSheet->getOrCreateColumnByPosition(static_cast<uint32_t>(startColPos));
        endCol = targetSheet->getOrCreateColumnByPosition(static_cast<uint32_t>(endColPos));
    }

    // Store the column IDs
    node->startColumnId = startCol->id.toString();
    node->endColumnId = endCol->id.toString();

    // Store the sheet ID for cross-sheet references
    if (!node->sheetName.empty() && targetSheet != &_sheet) {
        node->sheetId = targetSheet->id.toString();
    }

    return ResolveResult::ok();
}

ResolveResult FormulaResolver::resolveRowRangeRef(RowRangeRefNode* node) {
    // Get the target sheet
    Sheet* targetSheet = getTargetSheet(node->sheetName);
    if (targetSheet == nullptr) {
        return ResolveResult::error("Sheet not found: " + node->sheetName, node->position);
    }

    // Rows are 1-indexed in the AST
    if (node->startRow < 1 || node->endRow < 1) {
        return ResolveResult::error("Invalid row number", node->position);
    }
    const auto startRowPos = static_cast<uint32_t>(node->startRow - 1);
    const auto endRowPos = static_cast<uint32_t>(node->endRow - 1);

    const Axis* startRow = nullptr;
    const Axis* endRow = nullptr;
    if (_existingOnly) {
        startRow = targetSheet->getRowByPosition(startRowPos);
        if (startRow == nullptr) {
            return ResolveResult::error(
                "Row not found: " + std::to_string(node->startRow) +
                    ". Use getRequiredEntities() and create via CRDT first.",
                node->position);
        }
        endRow = targetSheet->getRowByPosition(endRowPos);
        if (endRow == nullptr) {
            return ResolveResult::error(
                "Row not found: " + std::to_string(node->endRow) +
                    ". Use getRequiredEntities() and create via CRDT first.",
                node->position);
        }
    } else {
        startRow = targetSheet->getOrCreateRowByPosition(startRowPos);
        endRow = targetSheet->getOrCreateRowByPosition(endRowPos);
    }

    // Store the row IDs
    node->startRowId = startRow->id.toString();
    node->endRowId = endRow->id.toString();

    // Store the sheet ID for cross-sheet references
    if (!node->sheetName.empty() && targetSheet != &_sheet) {
        node->sheetId = targetSheet->id.toString();
    }

    return ResolveResult::ok();
}

ResolveResult FormulaResolver::resolveNamedRef(NamedRefNode* node) {
    if (_namedRanges == nullptr) {
        return ResolveResult::error("Named range not found: " + node->name, node->position);
    }

    // Resolve the named range using the registry
    const NamedRange* nr = _namedRanges->resolve(node->name, _sheet.id);
    if (nr == nullptr) {
        return ResolveResult::error("Named range not found: " + node->name, node->position);
    }

    // Update scope based on what was found
    node->scope = (nr->scope == NamedRangeScope::WORKBOOK) ? ASTNamedRangeScope::WORKBOOK
                                                           : ASTNamedRangeScope::SHEET;

    return ResolveResult::ok();
}

ResolveResult FormulaResolver::resolveBinaryOp(BinaryOpNode* node) {
    auto result = resolveNode(node->left.get());
    if (!result.success) {
        return result;
    }
    return resolveNode(node->right.get());
}

ResolveResult FormulaResolver::resolveUnaryOp(UnaryOpNode* node) {
    return resolveNode(node->operand.get());
}

ResolveResult FormulaResolver::resolveFunctionCall(FunctionCallNode* node) {
    // Mark if this is a volatile function
    node->isVolatile = FunctionCallNode::isVolatileFunction(node->name);

    // Resolve all arguments
    for (auto& arg : node->args) {
        auto result = resolveNode(arg.get());
        if (!result.success) {
            return result;
        }
    }
    return ResolveResult::ok();
}

Sheet* FormulaResolver::getTargetSheet(const std::string& sheetName) {
    if (sheetName.empty()) {
        return &_sheet;
    }

    // Look up sheet by name in the workbook
    for (const auto& sheet : _workbook.sheets) {
        if (sheet->name == sheetName) {
            return sheet.get();
        }
    }

    return nullptr;
}

void FormulaResolver::extractReferencesFromNode(const ASTNode* node,
                                                std::vector<ReferenceInfo>& refs) const {
    if (node == nullptr) {
        return;
    }

    switch (node->type) {
        case ASTNodeType::CELL_REF: {
            auto* cellRef = static_cast<const CellRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::CELL;
            info.sourcePosition = cellRef->position;
            info.cellId = ID(cellRef->cellId);
            refs.push_back(info);
            break;
        }
        case ASTNodeType::RANGE_REF: {
            auto* rangeRef = static_cast<const RangeRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::RANGE;
            info.sourcePosition = rangeRef->position;
            info.topLeftCellId = ID(rangeRef->topLeft->cellId);
            info.bottomRightCellId = ID(rangeRef->bottomRight->cellId);
            refs.push_back(info);
            break;
        }
        case ASTNodeType::COLUMN_REF: {
            auto* colRef = static_cast<const ColumnRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::COLUMN;
            info.sourcePosition = colRef->position;
            info.axisId = ID(colRef->columnId);
            refs.push_back(info);
            break;
        }
        case ASTNodeType::ROW_REF: {
            auto* rowRef = static_cast<const RowRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::ROW;
            info.sourcePosition = rowRef->position;
            info.axisId = ID(rowRef->rowId);
            refs.push_back(info);
            break;
        }
        case ASTNodeType::COLUMN_RANGE_REF: {
            auto* colRangeRef = static_cast<const ColumnRangeRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::COLUMN_RANGE;
            info.sourcePosition = colRangeRef->position;
            info.startAxisId = ID(colRangeRef->startColumnId);
            info.endAxisId = ID(colRangeRef->endColumnId);
            refs.push_back(info);
            break;
        }
        case ASTNodeType::ROW_RANGE_REF: {
            auto* rowRangeRef = static_cast<const RowRangeRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::ROW_RANGE;
            info.sourcePosition = rowRangeRef->position;
            info.startAxisId = ID(rowRangeRef->startRowId);
            info.endAxisId = ID(rowRangeRef->endRowId);
            refs.push_back(info);
            break;
        }
        case ASTNodeType::NAMED_REF: {
            auto* namedRef = static_cast<const NamedRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::NAMED;
            info.sourcePosition = namedRef->position;
            info.namedRangeName = namedRef->name;
            refs.push_back(info);
            break;
        }
        case ASTNodeType::BINARY_OP: {
            auto* binOp = static_cast<const BinaryOpNode*>(node);
            extractReferencesFromNode(binOp->left.get(), refs);
            extractReferencesFromNode(binOp->right.get(), refs);
            break;
        }
        case ASTNodeType::UNARY_OP: {
            auto* unaryOp = static_cast<const UnaryOpNode*>(node);
            extractReferencesFromNode(unaryOp->operand.get(), refs);
            break;
        }
        case ASTNodeType::FUNCTION_CALL: {
            auto* funcCall = static_cast<const FunctionCallNode*>(node);
            for (const auto& arg : funcCall->args) {
                extractReferencesFromNode(arg.get(), refs);
            }
            break;
        }
        case ASTNodeType::ERROR_NODE: {
            auto* errorNode = static_cast<const ErrorNode*>(node);
            for (const auto& child : errorNode->partialChildren) {
                extractReferencesFromNode(child.get(), refs);
            }
            break;
        }
        default:
            break;
    }
}

// Note: FormulaDisplayConverter implementation is now in formula_display.cc

// ===========================================================================
// CRDT-Compatible Resolution: getRequiredEntities
// ===========================================================================
//
// Walks the AST to identify entities that need to be created via CRDT operations.
// This enables a two-phase approach:
// 1. Discover what entities are needed (this method)
// 2. Create entities via applyOperation (caller responsibility)
// 3. Resolve AST with existing entities
//

RequiredEntities FormulaResolver::getRequiredEntities(const ASTNode* ast) const {
    RequiredEntities required;
    if (ast != nullptr) {
        collectRequiredEntitiesFromNode(ast, required);
    }
    return required;
}

void FormulaResolver::collectRequiredEntitiesFromNode(const ASTNode* node,
                                                      RequiredEntities& required) const {
    if (node == nullptr) {
        return;
    }

    switch (node->type) {
        case ASTNodeType::CELL_REF:
            collectRequiredEntitiesFromCellRef(static_cast<const CellRefNode*>(node), required);
            break;

        case ASTNodeType::RANGE_REF: {
            auto* rangeRef = static_cast<const RangeRefNode*>(node);
            collectRequiredEntitiesFromCellRef(rangeRef->topLeft.get(), required);
            collectRequiredEntitiesFromCellRef(rangeRef->bottomRight.get(), required);
            break;
        }

        case ASTNodeType::COLUMN_REF:
            collectRequiredEntitiesFromColumnRef(static_cast<const ColumnRefNode*>(node), required);
            break;

        case ASTNodeType::ROW_REF:
            collectRequiredEntitiesFromRowRef(static_cast<const RowRefNode*>(node), required);
            break;

        case ASTNodeType::COLUMN_RANGE_REF:
            collectRequiredEntitiesFromColumnRangeRef(static_cast<const ColumnRangeRefNode*>(node),
                                                      required);
            break;

        case ASTNodeType::ROW_RANGE_REF:
            collectRequiredEntitiesFromRowRangeRef(static_cast<const RowRangeRefNode*>(node),
                                                   required);
            break;

        case ASTNodeType::SPILL_RANGE_REF: {
            auto* spillRef = static_cast<const SpillRangeRefNode*>(node);
            collectRequiredEntitiesFromCellRef(spillRef->anchor.get(), required);
            break;
        }

        case ASTNodeType::BINARY_OP: {
            auto* binOp = static_cast<const BinaryOpNode*>(node);
            collectRequiredEntitiesFromNode(binOp->left.get(), required);
            collectRequiredEntitiesFromNode(binOp->right.get(), required);
            break;
        }

        case ASTNodeType::UNARY_OP: {
            auto* unaryOp = static_cast<const UnaryOpNode*>(node);
            collectRequiredEntitiesFromNode(unaryOp->operand.get(), required);
            break;
        }

        case ASTNodeType::FUNCTION_CALL: {
            auto* funcCall = static_cast<const FunctionCallNode*>(node);
            for (const auto& arg : funcCall->args) {
                collectRequiredEntitiesFromNode(arg.get(), required);
            }
            break;
        }

        case ASTNodeType::ERROR_NODE: {
            auto* errorNode = static_cast<const ErrorNode*>(node);
            for (const auto& child : errorNode->partialChildren) {
                collectRequiredEntitiesFromNode(child.get(), required);
            }
            break;
        }

        default:
            // Literals, named refs, etc. don't require entity creation
            break;
    }
}

void FormulaResolver::collectRequiredEntitiesFromCellRef(const CellRefNode* node,
                                                         RequiredEntities& required) const {
    // Get target sheet (cross-sheet reference handling)
    // Note: getTargetSheet is non-const, so we need to use a workaround
    Sheet* targetSheet = nullptr;
    if (node->sheetName.empty()) {
        targetSheet = &_sheet;
    } else {
        for (const auto& sheet : _workbook.sheets) {
            if (sheet->name == node->sheetName) {
                targetSheet = sheet.get();
                break;
            }
        }
    }
    if (targetSheet == nullptr) {
        return;  // Sheet not found - resolve() will report the error
    }

    // Convert column name to position (0-indexed)
    const int32_t colPos = Sheet::columnNameToPosition(node->column);
    if (colPos < 0) {
        return;  // Invalid column - resolve() will report the error
    }

    // Row is 1-indexed in the AST, convert to 0-indexed position
    if (node->row < 1) {
        return;  // Invalid row - resolve() will report the error
    }
    const auto rowPos = static_cast<uint32_t>(node->row - 1);

    // Check if column exists, add to pending if not
    ID colId;
    const Axis* existingCol = targetSheet->getColumnByPosition(static_cast<uint32_t>(colPos));
    if (existingCol != nullptr) {
        colId = existingCol->id;
    } else {
        // Check if we've already queued this column for creation
        bool found = false;
        for (const auto& pending : required.columns) {
            if (pending.sheetId == targetSheet->id &&
                pending.position == static_cast<uint32_t>(colPos) && pending.isColumn) {
                colId = pending.id;
                found = true;
                break;
            }
        }
        if (!found) {
            PendingAxis pendingCol;
            pendingCol.id = generate_id();
            pendingCol.sheetId = targetSheet->id;
            pendingCol.position = static_cast<uint32_t>(colPos);
            pendingCol.isColumn = true;
            colId = pendingCol.id;
            required.columns.push_back(pendingCol);
        }
    }

    // Check if row exists, add to pending if not
    ID rowId;
    const Axis* existingRow = targetSheet->getRowByPosition(rowPos);
    if (existingRow != nullptr) {
        rowId = existingRow->id;
    } else {
        // Check if we've already queued this row for creation
        bool found = false;
        for (const auto& pending : required.rows) {
            if (pending.sheetId == targetSheet->id && pending.position == rowPos &&
                !pending.isColumn) {
                rowId = pending.id;
                found = true;
                break;
            }
        }
        if (!found) {
            PendingAxis pendingRow;
            pendingRow.id = generate_id();
            pendingRow.sheetId = targetSheet->id;
            pendingRow.position = rowPos;
            pendingRow.isColumn = false;
            rowId = pendingRow.id;
            required.rows.push_back(pendingRow);
        }
    }

    // Check if cell exists, add to pending if not
    // Use lookup-only method if both axes exist
    if (existingCol != nullptr && existingRow != nullptr) {
        const Cell* existingCell =
            targetSheet->getCellAtPosition(static_cast<uint32_t>(colPos), rowPos);
        if (existingCell != nullptr) {
            return;  // Cell already exists
        }
    }

    // Check if we've already queued this cell for creation
    for (const auto& pending : required.cells) {
        if (pending.colId == colId && pending.rowId == rowId) {
            return;  // Already queued
        }
    }

    // Queue cell for creation
    PendingCell pendingCell;
    pendingCell.id = generate_id();
    pendingCell.colId = colId;
    pendingCell.rowId = rowId;
    required.cells.push_back(pendingCell);
}

void FormulaResolver::collectRequiredEntitiesFromColumnRef(const ColumnRefNode* node,
                                                           RequiredEntities& required) const {
    // Get target sheet
    Sheet* targetSheet = nullptr;
    if (node->sheetName.empty()) {
        targetSheet = &_sheet;
    } else {
        for (const auto& sheet : _workbook.sheets) {
            if (sheet->name == node->sheetName) {
                targetSheet = sheet.get();
                break;
            }
        }
    }
    if (targetSheet == nullptr) {
        return;
    }

    const int32_t colPos = Sheet::columnNameToPosition(node->column);
    if (colPos < 0) {
        return;
    }

    // Check if column exists
    const Axis* existingCol = targetSheet->getColumnByPosition(static_cast<uint32_t>(colPos));
    if (existingCol != nullptr) {
        return;  // Already exists
    }

    // Check if already queued
    for (const auto& pending : required.columns) {
        if (pending.sheetId == targetSheet->id &&
            pending.position == static_cast<uint32_t>(colPos) && pending.isColumn) {
            return;  // Already queued
        }
    }

    // Queue for creation
    PendingAxis pendingCol;
    pendingCol.id = generate_id();
    pendingCol.sheetId = targetSheet->id;
    pendingCol.position = static_cast<uint32_t>(colPos);
    pendingCol.isColumn = true;
    required.columns.push_back(pendingCol);
}

void FormulaResolver::collectRequiredEntitiesFromRowRef(const RowRefNode* node,
                                                        RequiredEntities& required) const {
    // Get target sheet
    Sheet* targetSheet = nullptr;
    if (node->sheetName.empty()) {
        targetSheet = &_sheet;
    } else {
        for (const auto& sheet : _workbook.sheets) {
            if (sheet->name == node->sheetName) {
                targetSheet = sheet.get();
                break;
            }
        }
    }
    if (targetSheet == nullptr) {
        return;
    }

    if (node->row < 1) {
        return;
    }
    const auto rowPos = static_cast<uint32_t>(node->row - 1);

    // Check if row exists
    const Axis* existingRow = targetSheet->getRowByPosition(rowPos);
    if (existingRow != nullptr) {
        return;  // Already exists
    }

    // Check if already queued
    for (const auto& pending : required.rows) {
        if (pending.sheetId == targetSheet->id && pending.position == rowPos && !pending.isColumn) {
            return;  // Already queued
        }
    }

    // Queue for creation
    PendingAxis pendingRow;
    pendingRow.id = generate_id();
    pendingRow.sheetId = targetSheet->id;
    pendingRow.position = rowPos;
    pendingRow.isColumn = false;
    required.rows.push_back(pendingRow);
}

void FormulaResolver::collectRequiredEntitiesFromColumnRangeRef(const ColumnRangeRefNode* node,
                                                                RequiredEntities& required) const {
    // Get target sheet
    Sheet* targetSheet = nullptr;
    if (node->sheetName.empty()) {
        targetSheet = &_sheet;
    } else {
        for (const auto& sheet : _workbook.sheets) {
            if (sheet->name == node->sheetName) {
                targetSheet = sheet.get();
                break;
            }
        }
    }
    if (targetSheet == nullptr) {
        return;
    }

    const int32_t startColPos = Sheet::columnNameToPosition(node->startColumn);
    const int32_t endColPos = Sheet::columnNameToPosition(node->endColumn);
    if (startColPos < 0 || endColPos < 0) {
        return;
    }

    // Check and queue both columns
    for (int32_t colPos : {startColPos, endColPos}) {
        const Axis* existingCol = targetSheet->getColumnByPosition(static_cast<uint32_t>(colPos));
        if (existingCol != nullptr) {
            continue;  // Already exists
        }

        // Check if already queued
        bool found = false;
        for (const auto& pending : required.columns) {
            if (pending.sheetId == targetSheet->id &&
                pending.position == static_cast<uint32_t>(colPos) && pending.isColumn) {
                found = true;
                break;
            }
        }
        if (!found) {
            PendingAxis pendingCol;
            pendingCol.id = generate_id();
            pendingCol.sheetId = targetSheet->id;
            pendingCol.position = static_cast<uint32_t>(colPos);
            pendingCol.isColumn = true;
            required.columns.push_back(pendingCol);
        }
    }
}

void FormulaResolver::collectRequiredEntitiesFromRowRangeRef(const RowRangeRefNode* node,
                                                             RequiredEntities& required) const {
    // Get target sheet
    Sheet* targetSheet = nullptr;
    if (node->sheetName.empty()) {
        targetSheet = &_sheet;
    } else {
        for (const auto& sheet : _workbook.sheets) {
            if (sheet->name == node->sheetName) {
                targetSheet = sheet.get();
                break;
            }
        }
    }
    if (targetSheet == nullptr) {
        return;
    }

    if (node->startRow < 1 || node->endRow < 1) {
        return;
    }
    const auto startRowPos = static_cast<uint32_t>(node->startRow - 1);
    const auto endRowPos = static_cast<uint32_t>(node->endRow - 1);

    // Check and queue both rows
    for (uint32_t rowPos : {startRowPos, endRowPos}) {
        const Axis* existingRow = targetSheet->getRowByPosition(rowPos);
        if (existingRow != nullptr) {
            continue;  // Already exists
        }

        // Check if already queued
        bool found = false;
        for (const auto& pending : required.rows) {
            if (pending.sheetId == targetSheet->id && pending.position == rowPos &&
                !pending.isColumn) {
                found = true;
                break;
            }
        }
        if (!found) {
            PendingAxis pendingRow;
            pendingRow.id = generate_id();
            pendingRow.sheetId = targetSheet->id;
            pendingRow.position = rowPos;
            pendingRow.isColumn = false;
            required.rows.push_back(pendingRow);
        }
    }
}

}  // namespace cells
