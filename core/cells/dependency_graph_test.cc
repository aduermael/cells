#include "core/cells/dependency_graph.h"

#include <memory>
#include <string>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

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
            resolver.resolve(ast.get());
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

}  // namespace
}  // namespace cells
