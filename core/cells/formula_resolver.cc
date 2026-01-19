#include "core/cells/formula_resolver.h"

#include <algorithm>
#include <sstream>

namespace cells {

// ===========================================================================
// FormulaResolver
// ===========================================================================

FormulaResolver::FormulaResolver(Workbook& workbook, Sheet& sheet, NamedRangeRegistry* namedRanges)
    : _workbook(workbook), _sheet(sheet), _namedRanges(namedRanges) {}

ResolveResult FormulaResolver::resolve(ASTNode* ast) {
    if (ast == nullptr) {
        return ResolveResult::error("Null AST");
    }
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

    // Get or create the column and row axes
    const Axis* col = targetSheet->getOrCreateColumnByPosition(static_cast<uint32_t>(colPos));
    const Axis* row = targetSheet->getOrCreateRowByPosition(rowPos);

    // Get or create the cell at this position
    const Cell* cell = targetSheet->getOrCreateCellAt(col->id, row->id);

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

    // Get or create the column axis
    const Axis* col = targetSheet->getOrCreateColumnByPosition(static_cast<uint32_t>(colPos));

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

    // Get or create the row axis
    const Axis* row = targetSheet->getOrCreateRowByPosition(rowPos);

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

    // Get or create the column axes
    const Axis* startCol =
        targetSheet->getOrCreateColumnByPosition(static_cast<uint32_t>(startColPos));
    const Axis* endCol = targetSheet->getOrCreateColumnByPosition(static_cast<uint32_t>(endColPos));

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

    // Get or create the row axes
    const Axis* startRow = targetSheet->getOrCreateRowByPosition(startRowPos);
    const Axis* endRow = targetSheet->getOrCreateRowByPosition(endRowPos);

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

}  // namespace cells
