#include "core/cells/dependency_graph.h"

#include <memory>
#include <string>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/named_ranges.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

class DependencyGraphTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a workbook with a sheet
        workbook_ = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheet_ = sheet.get();
        workbook_->addSheet(std::move(sheet));

        // Create columns A-E (positions 0-4)
        for (uint32_t i = 0; i < 5; ++i) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = i;
            colIds_[i] = col->id;
            sheet_->addColumn(std::move(col));
        }

        // Create rows 1-5 (positions 0-4)
        for (uint32_t i = 0; i < 5; ++i) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = i;
            rowIds_[i] = row->id;
            sheet_->addRow(std::move(row));
        }
    }

    // Parse and resolve a formula, returning the AST
    std::unique_ptr<ASTNode> parseFormula(const std::string& formula) {
        FormulaParser parser(formula);
        auto ast = parser.parse();
        if (ast) {
            FormulaResolver resolver(*workbook_, *sheet_, &registry_);
            resolver.resolve(ast.get(), false);  // legacy mode for tests
        }
        return ast;
    }

    // Create a cell at (col, row) with the given formula
    Cell* createFormulaCell(uint32_t col, uint32_t row, const std::string& formula) {
        auto ast = parseFormula(formula);
        Cell* cell = sheet_->getOrCreateCellAt(colIds_[col], rowIds_[row]);
        if (cell && ast) {
            graph_.addFormula(cell->id, ast.get());
        }
        return cell;
    }

    DependencyGraph graph_;
    NamedRangeRegistry registry_;
    std::unique_ptr<Workbook> workbook_;
    Sheet* sheet_{nullptr};
    ID colIds_[5];
    ID rowIds_[5];
};

// ============================================================================
// Basic Tests
// ============================================================================

TEST_F(DependencyGraphTest, EmptyGraph) {
    EXPECT_EQ(graph_.size(), 0u);
    EXPECT_TRUE(graph_.getVolatileCells().empty());
}

TEST_F(DependencyGraphTest, AddSingleCellFormula) {
    auto ast = parseFormula("=A1+B2");
    ASSERT_NE(ast, nullptr);

    Cell* cell = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[0]);  // C1
    graph_.addFormula(cell->id, ast.get());

    EXPECT_EQ(graph_.size(), 1u);

    auto deps = graph_.getDependencies(cell->id);
    EXPECT_EQ(deps.size(), 2u);  // A1 and B2
}

TEST_F(DependencyGraphTest, RemoveFormula) {
    auto ast = parseFormula("=A1+B2");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    EXPECT_EQ(graph_.size(), 1u);

    graph_.removeFormula(cell->id);
    EXPECT_EQ(graph_.size(), 0u);

    auto deps = graph_.getDependencies(cell->id);
    EXPECT_TRUE(deps.empty());
}

TEST_F(DependencyGraphTest, UpdateFormula) {
    auto ast1 = parseFormula("=A1");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[0]);
    graph_.addFormula(cell->id, ast1.get());

    auto deps1 = graph_.getDependencies(cell->id);
    EXPECT_EQ(deps1.size(), 1u);

    // Update to different formula
    auto ast2 = parseFormula("=B1+C1+D1");
    graph_.addFormula(cell->id, ast2.get());

    auto deps2 = graph_.getDependencies(cell->id);
    EXPECT_EQ(deps2.size(), 3u);  // New formula has 3 refs
}

// ============================================================================
// Reference Type Tests
// ============================================================================

TEST_F(DependencyGraphTest, CellReferences) {
    auto ast = parseFormula("=A1+A2+A3");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[1], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    auto deps = graph_.getDependencies(cell->id);
    EXPECT_EQ(deps.size(), 3u);
    for (const auto& dep : deps) {
        EXPECT_EQ(dep.type, DependencyRef::Type::CELL);
    }
}

TEST_F(DependencyGraphTest, RangeReferences) {
    auto ast = parseFormula("=SUM(A1:C3)");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[3], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    auto deps = graph_.getDependencies(cell->id);
    EXPECT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].type, DependencyRef::Type::RANGE);
    EXPECT_FALSE(deps[0].startCellId.isNull());
    EXPECT_FALSE(deps[0].endCellId.isNull());
}

TEST_F(DependencyGraphTest, MixedReferences) {
    auto ast = parseFormula("=A1+SUM(B1:B3)+C1");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[3], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    auto deps = graph_.getDependencies(cell->id);
    EXPECT_EQ(deps.size(), 3u);

    int cellRefs = 0;
    int rangeRefs = 0;
    for (const auto& dep : deps) {
        if (dep.type == DependencyRef::Type::CELL) {
            cellRefs++;
        }
        if (dep.type == DependencyRef::Type::RANGE) {
            rangeRefs++;
        }
    }
    EXPECT_EQ(cellRefs, 2);
    EXPECT_EQ(rangeRefs, 1);
}

// ============================================================================
// Volatile Function Tests
// ============================================================================

TEST_F(DependencyGraphTest, VolatileNowFunction) {
    auto ast = parseFormula("=NOW()");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    EXPECT_TRUE(graph_.isVolatile(cell->id));
    auto volatileCells = graph_.getVolatileCells();
    EXPECT_EQ(volatileCells.size(), 1u);
}

TEST_F(DependencyGraphTest, VolatileRandFunction) {
    auto ast = parseFormula("=RAND()");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    EXPECT_TRUE(graph_.isVolatile(cell->id));
}

TEST_F(DependencyGraphTest, VolatileTodayFunction) {
    auto ast = parseFormula("=TODAY()");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    EXPECT_TRUE(graph_.isVolatile(cell->id));
}

TEST_F(DependencyGraphTest, NestedVolatileFunction) {
    auto ast = parseFormula("=A1+NOW()+B1");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    EXPECT_TRUE(graph_.isVolatile(cell->id));
}

TEST_F(DependencyGraphTest, NonVolatileFunction) {
    auto ast = parseFormula("=SUM(A1:A5)");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    EXPECT_FALSE(graph_.isVolatile(cell->id));
}

TEST_F(DependencyGraphTest, MarkUnmarkVolatile) {
    ID cellId("TestCell");

    graph_.markVolatile(cellId);
    EXPECT_TRUE(graph_.isVolatile(cellId));

    graph_.unmarkVolatile(cellId);
    EXPECT_FALSE(graph_.isVolatile(cellId));
}

// ============================================================================
// Source Position Tests
// ============================================================================

TEST_F(DependencyGraphTest, SourcePositions) {
    auto ast = parseFormula("=A1+B2");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    auto deps = graph_.getDependencies(cell->id);
    EXPECT_EQ(deps.size(), 2u);

    // A1 starts at position 1 (after =)
    // B2 starts at position 4 (after =A1+)
    // Verify positions are captured
    bool foundA1 = false;
    bool foundB2 = false;
    for (const auto& dep : deps) {
        if (dep.sourceStart == 1) {
            foundA1 = true;
        }
        if (dep.sourceStart == 4) {
            foundB2 = true;
        }
    }
    EXPECT_TRUE(foundA1);
    EXPECT_TRUE(foundB2);
}

// ============================================================================
// Circular Reference Tests
// ============================================================================

TEST_F(DependencyGraphTest, NoCircularReference) {
    // A1 = B1 + 1
    // B1 = 10 (no formula)
    auto ast = parseFormula("=B1+1");
    Cell* cellA1 = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    graph_.addFormula(cellA1->id, ast.get());

    auto cycle = graph_.detectCycle(cellA1->id);
    EXPECT_TRUE(cycle.empty());
}

// ============================================================================
// Recalc Order Tests
// ============================================================================

TEST_F(DependencyGraphTest, RecalcOrderEmpty) {
    bool hasCycle = false;
    auto order = graph_.getRecalcOrder({}, &hasCycle);
    EXPECT_TRUE(order.empty());
    EXPECT_FALSE(hasCycle);
}

TEST_F(DependencyGraphTest, RecalcOrderSingleCell) {
    auto ast = parseFormula("=A1+1");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[1], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    bool hasCycle = false;
    auto order = graph_.getRecalcOrder({cell->id}, &hasCycle);
    EXPECT_FALSE(hasCycle);
    EXPECT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], cell->id);
}

TEST_F(DependencyGraphTest, RecalcOrderExcludesVolatileUnlessExplicit) {
    auto ast1 = parseFormula("=NOW()");
    Cell* volatileCell = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    graph_.addFormula(volatileCell->id, ast1.get());

    auto ast2 = parseFormula("=A1+1");
    Cell* normalCell = sheet_->getOrCreateCellAt(colIds_[1], rowIds_[0]);
    graph_.addFormula(normalCell->id, ast2.get());

    // When recalculating something unrelated, volatile cell should NOT be included
    // This is the correct behavior: RAND(), NOW(), etc. should not recalculate
    // on every cell change, only when explicitly requested via recalculateVolatile()
    ID otherId = generate_id();
    bool hasCycle = false;
    auto order = graph_.getRecalcOrder({otherId}, &hasCycle);
    EXPECT_FALSE(hasCycle);

    // Should NOT include the volatile cell (it's unrelated to the changed cell)
    bool foundVolatile = false;
    for (const auto& id : order) {
        if (id == volatileCell->id) {
            foundVolatile = true;
            break;
        }
    }
    EXPECT_FALSE(foundVolatile) << "Volatile cells should not be automatically included";

    // But when volatile cell is explicitly included, it should be in the order
    hasCycle = false;
    auto orderWithVolatile = graph_.getRecalcOrder({volatileCell->id}, &hasCycle);
    EXPECT_FALSE(hasCycle);
    bool foundVolatileExplicit = false;
    for (const auto& id : orderWithVolatile) {
        if (id == volatileCell->id) {
            foundVolatileExplicit = true;
            break;
        }
    }
    EXPECT_TRUE(foundVolatileExplicit) << "Volatile cell should be included when explicit";
}

// ============================================================================
// Clear Tests
// ============================================================================

TEST_F(DependencyGraphTest, Clear) {
    auto ast = parseFormula("=A1+NOW()");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    EXPECT_EQ(graph_.size(), 1u);
    EXPECT_EQ(graph_.getVolatileCells().size(), 1u);

    graph_.clear();

    EXPECT_EQ(graph_.size(), 0u);
    EXPECT_TRUE(graph_.getVolatileCells().empty());
    EXPECT_TRUE(graph_.getDependencies(cell->id).empty());
}

// ============================================================================
// Multiple Formulas Tests
// ============================================================================

TEST_F(DependencyGraphTest, MultipleFormulas) {
    // A1 = 10
    // B1 = A1 + 5
    // C1 = A1 + B1
    // D1 = C1 * 2

    auto astB1 = parseFormula("=A1+5");
    Cell* cellB1 = sheet_->getOrCreateCellAt(colIds_[1], rowIds_[0]);
    graph_.addFormula(cellB1->id, astB1.get());

    auto astC1 = parseFormula("=A1+B1");
    Cell* cellC1 = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[0]);
    graph_.addFormula(cellC1->id, astC1.get());

    auto astD1 = parseFormula("=C1*2");
    Cell* cellD1 = sheet_->getOrCreateCellAt(colIds_[3], rowIds_[0]);
    graph_.addFormula(cellD1->id, astD1.get());

    EXPECT_EQ(graph_.size(), 3u);

    // B1 depends on A1
    auto depsB1 = graph_.getDependencies(cellB1->id);
    EXPECT_EQ(depsB1.size(), 1u);

    // C1 depends on A1 and B1
    auto depsC1 = graph_.getDependencies(cellC1->id);
    EXPECT_EQ(depsC1.size(), 2u);

    // D1 depends on C1
    auto depsD1 = graph_.getDependencies(cellD1->id);
    EXPECT_EQ(depsD1.size(), 1u);
}

// ============================================================================
// Nested Function Tests
// ============================================================================

TEST_F(DependencyGraphTest, NestedFunctionCalls) {
    auto ast = parseFormula("=SUM(A1, IF(B1>0, C1, D1))");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[4], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    auto deps = graph_.getDependencies(cell->id);
    EXPECT_EQ(deps.size(), 4u);  // A1, B1, C1, D1
}

// ============================================================================
// Error Node Tests
// ============================================================================

TEST_F(DependencyGraphTest, ErrorNodeWithPartialChildren) {
    // Parse an incomplete formula that produces an error node
    // The parser should still capture valid references before the error
    auto ast = parseFormula("=A1+B1+");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    auto deps = graph_.getDependencies(cell->id);
    // Should still find A1 and B1 even though formula is incomplete
    EXPECT_GE(deps.size(), 2u);
}

// ============================================================================
// Null/Empty Input Tests
// ============================================================================

TEST_F(DependencyGraphTest, NullAst) {
    ID cellId = generate_id();
    graph_.addFormula(cellId, nullptr);
    EXPECT_EQ(graph_.size(), 0u);
}

TEST_F(DependencyGraphTest, LiteralOnlyFormula) {
    auto ast = parseFormula("=42");
    Cell* cell = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    graph_.addFormula(cell->id, ast.get());

    auto deps = graph_.getDependencies(cell->id);
    EXPECT_TRUE(deps.empty());  // No references in formula
}

// ============================================================================
// Named Reference Dependency Tests
// ============================================================================

TEST_F(DependencyGraphTest, NamedRefCell) {
    // Define a named range pointing to cell A1
    Cell* cellA1 = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    registry_.defineWorkbook("MyCell", NamedRangeTarget::cell(cellA1->id, sheet_->id));

    // Parse formula using the named reference
    auto ast = parseFormula("=MyCell+10");
    ASSERT_NE(ast, nullptr);

    // Add formula with named range resolution
    Cell* cellB1 = sheet_->getOrCreateCellAt(colIds_[1], rowIds_[0]);
    graph_.addFormula(cellB1->id, ast.get(), nullptr, &registry_, sheet_->id);

    auto deps = graph_.getDependencies(cellB1->id);
    EXPECT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].type, DependencyRef::Type::CELL);
    EXPECT_EQ(deps[0].cellId, cellA1->id);
}

TEST_F(DependencyGraphTest, NamedRefRange) {
    // Define a named range pointing to A1:C3
    Cell* cellA1 = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    Cell* cellC3 = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[2]);
    registry_.defineWorkbook("MyRange",
                             NamedRangeTarget::range(cellA1->id, cellC3->id, sheet_->id));

    // Parse formula using the named range
    auto ast = parseFormula("=SUM(MyRange)");
    ASSERT_NE(ast, nullptr);

    // Add formula with named range resolution
    Cell* cellD1 = sheet_->getOrCreateCellAt(colIds_[3], rowIds_[0]);
    graph_.addFormula(cellD1->id, ast.get(), nullptr, &registry_, sheet_->id);

    auto deps = graph_.getDependencies(cellD1->id);
    EXPECT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].type, DependencyRef::Type::RANGE);
    EXPECT_EQ(deps[0].startCellId, cellA1->id);
    EXPECT_EQ(deps[0].endCellId, cellC3->id);
}

TEST_F(DependencyGraphTest, NamedRefColumn) {
    // Define a named range pointing to column A
    registry_.defineWorkbook("ColA", NamedRangeTarget::column(colIds_[0], sheet_->id));

    // Parse formula using the named range
    auto ast = parseFormula("=SUM(ColA)");
    ASSERT_NE(ast, nullptr);

    // Add formula with named range resolution
    Cell* cellB1 = sheet_->getOrCreateCellAt(colIds_[1], rowIds_[0]);
    graph_.addFormula(cellB1->id, ast.get(), nullptr, &registry_, sheet_->id);

    auto deps = graph_.getDependencies(cellB1->id);
    EXPECT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].type, DependencyRef::Type::COLUMN);
    EXPECT_EQ(deps[0].columnId, colIds_[0]);
}

TEST_F(DependencyGraphTest, NamedRefRow) {
    // Define a named range pointing to row 1
    // Note: "Row1" is invalid because it looks like a cell reference (3 letters + digit)
    // Use "FirstRow" instead
    registry_.defineWorkbook("FirstRow", NamedRangeTarget::row(rowIds_[0], sheet_->id));

    // Parse formula using the named range
    auto ast = parseFormula("=SUM(FirstRow)");
    ASSERT_NE(ast, nullptr);

    // Add formula with named range resolution
    Cell* cellA2 = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[1]);
    graph_.addFormula(cellA2->id, ast.get(), nullptr, &registry_, sheet_->id);

    auto deps = graph_.getDependencies(cellA2->id);
    EXPECT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].type, DependencyRef::Type::ROW);
    EXPECT_EQ(deps[0].rowId, rowIds_[0]);
}

TEST_F(DependencyGraphTest, NamedRefColumnRange) {
    // Define a named range pointing to columns A:C
    registry_.defineWorkbook("ColsAC",
                             NamedRangeTarget::columnRange(colIds_[0], colIds_[2], sheet_->id));

    // Parse formula using the named range
    auto ast = parseFormula("=SUM(ColsAC)");
    ASSERT_NE(ast, nullptr);

    // Add formula with named range resolution
    Cell* cellD1 = sheet_->getOrCreateCellAt(colIds_[3], rowIds_[0]);
    graph_.addFormula(cellD1->id, ast.get(), nullptr, &registry_, sheet_->id);

    auto deps = graph_.getDependencies(cellD1->id);
    EXPECT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].type, DependencyRef::Type::COLUMN_RANGE);
    EXPECT_EQ(deps[0].startColumnId, colIds_[0]);
    EXPECT_EQ(deps[0].endColumnId, colIds_[2]);
}

TEST_F(DependencyGraphTest, NamedRefRowRange) {
    // Define a named range pointing to rows 1:3
    registry_.defineWorkbook("Rows13",
                             NamedRangeTarget::rowRange(rowIds_[0], rowIds_[2], sheet_->id));

    // Parse formula using the named range
    auto ast = parseFormula("=SUM(Rows13)");
    ASSERT_NE(ast, nullptr);

    // Add formula with named range resolution
    Cell* cellA4 = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[3]);
    graph_.addFormula(cellA4->id, ast.get(), nullptr, &registry_, sheet_->id);

    auto deps = graph_.getDependencies(cellA4->id);
    EXPECT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].type, DependencyRef::Type::ROW_RANGE);
    EXPECT_EQ(deps[0].startRowId, rowIds_[0]);
    EXPECT_EQ(deps[0].endRowId, rowIds_[2]);
}

TEST_F(DependencyGraphTest, NamedRefSheetScoped) {
    // Define a workbook-scoped named range
    Cell* cellA1 = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    Cell* cellB1 = sheet_->getOrCreateCellAt(colIds_[1], rowIds_[0]);
    registry_.defineWorkbook("Value", NamedRangeTarget::cell(cellA1->id, sheet_->id));

    // Define a sheet-scoped named range with the same name (should shadow)
    registry_.defineSheet("Value", sheet_->id, NamedRangeTarget::cell(cellB1->id, sheet_->id));

    // Parse formula using the named reference - sheet-scoped should take precedence
    auto ast = parseFormula("=Value*2");
    ASSERT_NE(ast, nullptr);

    Cell* cellC1 = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[0]);
    graph_.addFormula(cellC1->id, ast.get(), nullptr, &registry_, sheet_->id);

    auto deps = graph_.getDependencies(cellC1->id);
    EXPECT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].type, DependencyRef::Type::CELL);
    // Should resolve to B1 (sheet-scoped), not A1 (workbook-scoped)
    EXPECT_EQ(deps[0].cellId, cellB1->id);
}

TEST_F(DependencyGraphTest, NamedRefWorkbookScopedFallback) {
    // Define only a workbook-scoped named range (no sheet-scoped shadow)
    Cell* cellA1 = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    registry_.defineWorkbook("GlobalValue", NamedRangeTarget::cell(cellA1->id, sheet_->id));

    // Parse formula using the named reference
    auto ast = parseFormula("=GlobalValue+5");
    ASSERT_NE(ast, nullptr);

    Cell* cellB1 = sheet_->getOrCreateCellAt(colIds_[1], rowIds_[0]);
    graph_.addFormula(cellB1->id, ast.get(), nullptr, &registry_, sheet_->id);

    auto deps = graph_.getDependencies(cellB1->id);
    EXPECT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].type, DependencyRef::Type::CELL);
    EXPECT_EQ(deps[0].cellId, cellA1->id);
}

TEST_F(DependencyGraphTest, NamedRefUnresolved) {
    // Use a named reference that doesn't exist
    auto ast = parseFormula("=UndefinedName+1");
    ASSERT_NE(ast, nullptr);

    Cell* cellA1 = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    graph_.addFormula(cellA1->id, ast.get(), nullptr, &registry_, sheet_->id);

    // Unresolved named ref should not add any dependencies
    auto deps = graph_.getDependencies(cellA1->id);
    EXPECT_TRUE(deps.empty());
}

TEST_F(DependencyGraphTest, NamedRefWithoutRegistry) {
    // Define a named range but don't pass the registry
    Cell* cellA1 = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    registry_.defineWorkbook("MyCell", NamedRangeTarget::cell(cellA1->id, sheet_->id));

    // Parse formula using the named reference
    auto ast = parseFormula("=MyCell+10");
    ASSERT_NE(ast, nullptr);

    // Add formula WITHOUT named range resolution (null registry)
    Cell* cellB1 = sheet_->getOrCreateCellAt(colIds_[1], rowIds_[0]);
    graph_.addFormula(cellB1->id, ast.get());  // No registry passed

    // Without registry, named refs are not resolved
    auto deps = graph_.getDependencies(cellB1->id);
    EXPECT_TRUE(deps.empty());
}

TEST_F(DependencyGraphTest, NamedRefMixedWithCellRef) {
    // Define a named range
    Cell* cellA1 = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    registry_.defineWorkbook("Named", NamedRangeTarget::cell(cellA1->id, sheet_->id));

    // Parse formula with both named ref and regular cell ref
    auto ast = parseFormula("=Named+B1");
    ASSERT_NE(ast, nullptr);

    Cell* cellB1 = sheet_->getOrCreateCellAt(colIds_[1], rowIds_[0]);
    Cell* cellC1 = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[0]);
    graph_.addFormula(cellC1->id, ast.get(), nullptr, &registry_, sheet_->id);

    auto deps = graph_.getDependencies(cellC1->id);
    EXPECT_EQ(deps.size(), 2u);

    // Should have both A1 (from named ref) and B1 (from cell ref)
    bool foundA1 = false;
    bool foundB1 = false;
    for (const auto& dep : deps) {
        if (dep.cellId == cellA1->id) {
            foundA1 = true;
        }
        if (dep.cellId == cellB1->id) {
            foundB1 = true;
        }
    }
    EXPECT_TRUE(foundA1) << "Should find dependency on A1 (named ref)";
    EXPECT_TRUE(foundB1) << "Should find dependency on B1 (cell ref)";
}

TEST_F(DependencyGraphTest, NamedRefSourcePositionPreserved) {
    // Define a named range
    Cell* cellA1 = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    registry_.defineWorkbook("Price", NamedRangeTarget::cell(cellA1->id, sheet_->id));

    // Parse formula: =Price*2
    // Position: 01234567
    //           =Price*2
    auto ast = parseFormula("=Price*2");
    ASSERT_NE(ast, nullptr);

    Cell* cellB1 = sheet_->getOrCreateCellAt(colIds_[1], rowIds_[0]);
    graph_.addFormula(cellB1->id, ast.get(), nullptr, &registry_, sheet_->id);

    auto deps = graph_.getDependencies(cellB1->id);
    EXPECT_EQ(deps.size(), 1u);
    // Source position should be for "Price" (starts at 1, ends at 6)
    EXPECT_EQ(deps[0].sourceStart, 1);
    EXPECT_EQ(deps[0].sourceEnd, 6);
}

// Note on recursive/circular named references:
// The current NamedRangeRegistry architecture stores resolved cell/range IDs directly,
// not references to other named ranges. This means:
// - Named ranges always point to concrete cells/rows/columns, not to other named ranges
// - "Recursive" named references (NameA -> NameB -> Cell) are not possible in this design
// - Circular named references are also not possible at the data model level
//
// The depth limit (kMaxNamedRefDepth = 32) in ReferenceExtractor exists as defensive
// protection in case the architecture evolves to support formula-based named ranges.
//
// Therefore, tests 4e (recursive named refs) and 4f (circular named refs) from the plan
// are N/A - they cannot occur with the current architecture.

TEST_F(DependencyGraphTest, NamedRefMultipleInFormula) {
    // Define multiple named ranges
    Cell* cellA1 = sheet_->getOrCreateCellAt(colIds_[0], rowIds_[0]);
    Cell* cellB1 = sheet_->getOrCreateCellAt(colIds_[1], rowIds_[0]);
    registry_.defineWorkbook("Price", NamedRangeTarget::cell(cellA1->id, sheet_->id));
    registry_.defineWorkbook("Quantity", NamedRangeTarget::cell(cellB1->id, sheet_->id));

    // Parse formula using multiple named references
    auto ast = parseFormula("=Price*Quantity");
    ASSERT_NE(ast, nullptr);

    Cell* cellC1 = sheet_->getOrCreateCellAt(colIds_[2], rowIds_[0]);
    graph_.addFormula(cellC1->id, ast.get(), nullptr, &registry_, sheet_->id);

    auto deps = graph_.getDependencies(cellC1->id);
    EXPECT_EQ(deps.size(), 2u);

    // Should have both Price (A1) and Quantity (B1)
    bool foundPrice = false;
    bool foundQuantity = false;
    for (const auto& dep : deps) {
        if (dep.cellId == cellA1->id) {
            foundPrice = true;
        }
        if (dep.cellId == cellB1->id) {
            foundQuantity = true;
        }
    }
    EXPECT_TRUE(foundPrice) << "Should find dependency on Price (A1)";
    EXPECT_TRUE(foundQuantity) << "Should find dependency on Quantity (B1)";
}

}  // namespace
}  // namespace cells
