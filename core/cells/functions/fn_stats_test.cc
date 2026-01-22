#include "core/cells/functions/fn_stats.h"

#include <cmath>

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unordered_set>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_functions.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

namespace cells {
namespace {

class FnStatsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a workbook with one sheet
        workbook = std::make_unique<Workbook>(generate_id(), "Test");
        workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));
        sheet = workbook->getSheetByIndex(0);

        // Create columns A-Z (positions 0-25)
        for (uint32_t i = 0; i < 26; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = i;
            col->name = Sheet::positionToColumnName(i);
            colIds[i] = col->id;
            sheet->addColumn(std::move(col));
        }

        // Create rows 1-100 (positions 0-99)
        for (uint32_t i = 0; i < 100; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = i;
            rowIds[i] = row->id;
            sheet->addRow(std::move(row));
        }
    }

    // Parse and evaluate a formula
    EvalResult eval(const std::string& formula) {
        FormulaParser parser(formula);
        auto ast = parser.parse();
        if (!ast || parser.hasErrors()) {
            return EvalResult::Error(CellError::VALUE);
        }

        // Create missing entities and resolve references
        FormulaResolver resolver(*workbook, *sheet);
        createRequiredEntities(resolver, ast.get());
        resolver.resolve(ast.get());

        // Evaluate
        std::unordered_set<ID> evaluating;
        EvalContext ctx;
        ctx.sheet = sheet;
        ctx.workbook = workbook.get();
        ctx.evaluatingCells = &evaluating;
        ctx.recursionDepth = 0;

        return evaluate(ast.get(), ctx);
    }

    // Helper to create missing entities before resolution
    void createRequiredEntities(FormulaResolver& resolver, ASTNode* ast) {
        RequiredEntities required = resolver.getRequiredEntities(ast);
        for (const auto& pendingCell : required.cells) {
            auto findColPos = [&required, this](const ID& colId) -> uint32_t {
                for (const auto& c : required.columns) {
                    if (c.id == colId)
                        return c.position;
                }
                const Axis* axis = sheet->getColumn(colId);
                return axis ? axis->position : 0;
            };
            auto findRowPos = [&required, this](const ID& rowId) -> uint32_t {
                for (const auto& r : required.rows) {
                    if (r.id == rowId)
                        return r.position;
                }
                const Axis* axis = sheet->getRow(rowId);
                return axis ? axis->position : 0;
            };
            uint32_t colPos = findColPos(pendingCell.colId);
            uint32_t rowPos = findRowPos(pendingCell.rowId);
            const Axis* col = sheet->getColumnByPosition(colPos);
            const Axis* row = sheet->getRowByPosition(rowPos);
            if (col && row) {
                sheet->getOrCreateCellAt(col->id, row->id);
            }
        }
    }

    // Set a cell value
    Cell* setCellValue(uint32_t col, uint32_t row, double value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26];
    ID rowIds[100];
};

// =============================================================================
// MEDIAN Tests
// =============================================================================

TEST_F(FnStatsTest, MedianOddCount) {
    EvalResult result = eval("=MEDIAN(1, 2, 3)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FnStatsTest, MedianEvenCount) {
    EvalResult result = eval("=MEDIAN(1, 2, 3, 4)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.5);
}

TEST_F(FnStatsTest, MedianSingleValue) {
    EvalResult result = eval("=MEDIAN(5)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FnStatsTest, MedianUnsortedInput) {
    EvalResult result = eval("=MEDIAN(5, 1, 3, 2, 4)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FnStatsTest, MedianWithNegatives) {
    EvalResult result = eval("=MEDIAN(-5, -1, 0, 1, 5)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FnStatsTest, MedianRange) {
    setCellValue(0, 0, 1.0);  // A1
    setCellValue(0, 1, 2.0);  // A2
    setCellValue(0, 2, 3.0);  // A3
    setCellValue(0, 3, 4.0);  // A4
    setCellValue(0, 4, 5.0);  // A5

    EvalResult result = eval("=MEDIAN(A1:A5)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FnStatsTest, MedianNoArgs) {
    EvalResult result = eval("=MEDIAN()");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FnStatsTest, MedianDuplicateValues) {
    EvalResult result = eval("=MEDIAN(1, 1, 2, 2, 3)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

// =============================================================================
// STDEV Tests (Sample Standard Deviation)
// =============================================================================

TEST_F(FnStatsTest, StdevBasic) {
    // Sample: 2, 4, 4, 4, 5, 5, 7, 9
    // Mean = 5, variance = 32/7 = 4.571..., stdev = 2.138...
    EvalResult result = eval("=STDEV(2, 4, 4, 4, 5, 5, 7, 9)");
    ASSERT_TRUE(result.isNumber());
    // Excel gives approximately 2.138...
    EXPECT_NEAR(result.getNumber(), 2.138089935, 0.00001);
}

TEST_F(FnStatsTest, StdevSingleValue) {
    // Single value - can't compute sample stdev (needs n-1 >= 1)
    EvalResult result = eval("=STDEV(5)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

TEST_F(FnStatsTest, StdevTwoValues) {
    // Two values: 1, 3 -> mean = 2, variance = ((1-2)^2 + (3-2)^2) / 1 = 2
    // stdev = sqrt(2) = 1.414...
    EvalResult result = eval("=STDEV(1, 3)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_NEAR(result.getNumber(), std::sqrt(2.0), 0.00001);
}

TEST_F(FnStatsTest, StdevRange) {
    setCellValue(0, 0, 2.0);
    setCellValue(0, 1, 4.0);
    setCellValue(0, 2, 6.0);

    EvalResult result = eval("=STDEV(A1:A3)");
    ASSERT_TRUE(result.isNumber());
    // Mean = 4, variance = ((2-4)^2 + (4-4)^2 + (6-4)^2) / 2 = 4
    // stdev = 2
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FnStatsTest, StdevSameAsStdevS) {
    EvalResult result1 = eval("=STDEV(1, 2, 3, 4, 5)");
    EvalResult result2 = eval("=STDEVS(1, 2, 3, 4, 5)");
    ASSERT_TRUE(result1.isNumber());
    ASSERT_TRUE(result2.isNumber());
    EXPECT_DOUBLE_EQ(result1.getNumber(), result2.getNumber());
}

TEST_F(FnStatsTest, StdevNoArgs) {
    EvalResult result = eval("=STDEV()");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// =============================================================================
// STDEVP Tests (Population Standard Deviation)
// =============================================================================

TEST_F(FnStatsTest, StdevPBasic) {
    // Population stdev uses n denominator
    // Sample: 2, 4, 4, 4, 5, 5, 7, 9
    // Mean = 5, variance = 32/8 = 4, stdev = 2
    EvalResult result = eval("=STDEVP(2, 4, 4, 4, 5, 5, 7, 9)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FnStatsTest, StdevPSingleValue) {
    // Population stdev of single value is 0
    EvalResult result = eval("=STDEVP(5)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FnStatsTest, StdevPTwoValues) {
    // Two values: 1, 3 -> mean = 2, variance = ((1-2)^2 + (3-2)^2) / 2 = 1
    // stdev = 1
    EvalResult result = eval("=STDEVP(1, 3)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

// =============================================================================
// VAR Tests (Sample Variance)
// =============================================================================

TEST_F(FnStatsTest, VarBasic) {
    // Sample: 2, 4, 4, 4, 5, 5, 7, 9
    // Mean = 5, variance = 32/7 = 4.571...
    EvalResult result = eval("=VAR(2, 4, 4, 4, 5, 5, 7, 9)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_NEAR(result.getNumber(), 32.0 / 7.0, 0.00001);
}

TEST_F(FnStatsTest, VarSingleValue) {
    EvalResult result = eval("=VAR(5)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

TEST_F(FnStatsTest, VarSameAsVarS) {
    EvalResult result1 = eval("=VAR(1, 2, 3, 4, 5)");
    EvalResult result2 = eval("=VARS(1, 2, 3, 4, 5)");
    ASSERT_TRUE(result1.isNumber());
    ASSERT_TRUE(result2.isNumber());
    EXPECT_DOUBLE_EQ(result1.getNumber(), result2.getNumber());
}

TEST_F(FnStatsTest, VarRange) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    EvalResult result = eval("=VAR(A1:A3)");
    ASSERT_TRUE(result.isNumber());
    // Mean = 2, variance = ((1-2)^2 + (2-2)^2 + (3-2)^2) / 2 = 1
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

// =============================================================================
// VARP Tests (Population Variance)
// =============================================================================

TEST_F(FnStatsTest, VarPBasic) {
    // Population variance uses n denominator
    EvalResult result = eval("=VARP(2, 4, 4, 4, 5, 5, 7, 9)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 4.0);
}

TEST_F(FnStatsTest, VarPSingleValue) {
    // Population variance of single value is 0
    EvalResult result = eval("=VARP(5)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FnStatsTest, StdevSquaredEqualsVar) {
    // STDEV^2 should equal VAR
    EvalResult stdev = eval("=STDEV(1, 2, 3, 4, 5)");
    EvalResult var = eval("=VAR(1, 2, 3, 4, 5)");
    ASSERT_TRUE(stdev.isNumber());
    ASSERT_TRUE(var.isNumber());
    EXPECT_NEAR(stdev.getNumber() * stdev.getNumber(), var.getNumber(), 0.00001);
}

// =============================================================================
// PERCENTILE Tests
// =============================================================================

TEST_F(FnStatsTest, PercentileMedian) {
    // PERCENTILE at 0.5 should equal MEDIAN
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 4.0);
    setCellValue(0, 4, 5.0);

    EvalResult percentile = eval("=PERCENTILE(A1:A5, 0.5)");
    EvalResult median = eval("=MEDIAN(A1:A5)");

    ASSERT_TRUE(percentile.isNumber());
    ASSERT_TRUE(median.isNumber());
    EXPECT_DOUBLE_EQ(percentile.getNumber(), median.getNumber());
}

TEST_F(FnStatsTest, PercentileMin) {
    // PERCENTILE at 0 should return minimum
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 4.0);
    setCellValue(0, 4, 5.0);

    EvalResult result = eval("=PERCENTILE(A1:A5, 0)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FnStatsTest, PercentileMax) {
    // PERCENTILE at 1 should return maximum
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 4.0);
    setCellValue(0, 4, 5.0);

    EvalResult result = eval("=PERCENTILE(A1:A5, 1)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FnStatsTest, Percentile25) {
    // First quartile (25th percentile)
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 4.0);
    setCellValue(0, 4, 5.0);

    EvalResult result = eval("=PERCENTILE(A1:A5, 0.25)");
    ASSERT_TRUE(result.isNumber());
    // Excel's PERCENTILE.INC formula: 0.25 * (5-1) = 1, so value at index 1 = 2
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FnStatsTest, Percentile75) {
    // Third quartile (75th percentile)
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 4.0);
    setCellValue(0, 4, 5.0);

    EvalResult result = eval("=PERCENTILE(A1:A5, 0.75)");
    ASSERT_TRUE(result.isNumber());
    // 0.75 * (5-1) = 3, so value at index 3 = 4
    EXPECT_DOUBLE_EQ(result.getNumber(), 4.0);
}

TEST_F(FnStatsTest, PercentileInterpolation) {
    // Test interpolation
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 3.0);
    setCellValue(0, 2, 5.0);

    // 0.5 * (3-1) = 1, so value at index 1 = 3
    EvalResult result = eval("=PERCENTILE(A1:A3, 0.5)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);

    // 0.25 * (3-1) = 0.5, interpolate between index 0 and 1
    // 1 + 0.5 * (3 - 1) = 2
    result = eval("=PERCENTILE(A1:A3, 0.25)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FnStatsTest, PercentileInvalidKNegative) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    EvalResult result = eval("=PERCENTILE(A1:A3, -0.1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FnStatsTest, PercentileInvalidKGreaterThan1) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    EvalResult result = eval("=PERCENTILE(A1:A3, 1.1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FnStatsTest, PercentileSameAsPercentileInc) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    EvalResult result1 = eval("=PERCENTILE(A1:A3, 0.5)");
    EvalResult result2 = eval("=PERCENTILEINC(A1:A3, 0.5)");

    ASSERT_TRUE(result1.isNumber());
    ASSERT_TRUE(result2.isNumber());
    EXPECT_DOUBLE_EQ(result1.getNumber(), result2.getNumber());
}

TEST_F(FnStatsTest, PercentileExcBasic) {
    // PERCENTILEEXC uses (0, 1) range exclusive
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 4.0);

    EvalResult result = eval("=PERCENTILEEXC(A1:A4, 0.5)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.5);
}

TEST_F(FnStatsTest, PercentileExcInvalidKZero) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    EvalResult result = eval("=PERCENTILEEXC(A1:A3, 0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FnStatsTest, PercentileExcInvalidKOne) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    EvalResult result = eval("=PERCENTILEEXC(A1:A3, 1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FnStatsTest, PercentileSingleValue) {
    setCellValue(0, 0, 5.0);

    EvalResult result = eval("=PERCENTILE(A1:A1, 0.5)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FnStatsTest, PercentileTooFewArgs) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    EvalResult result = eval("=PERCENTILE(A1:A3)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnStatsTest, PercentileTooManyArgs) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    EvalResult result = eval("=PERCENTILE(A1:A3, 0.5, 0.75)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// Error Propagation Tests
// =============================================================================

TEST_F(FnStatsTest, MedianErrorPropagation) {
    EvalResult result = eval("=MEDIAN(1, 1/0, 3)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

TEST_F(FnStatsTest, StdevErrorPropagation) {
    EvalResult result = eval("=STDEV(1, 1/0, 3)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

TEST_F(FnStatsTest, VarErrorPropagation) {
    EvalResult result = eval("=VAR(1, 1/0, 3)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

TEST_F(FnStatsTest, PercentileErrorPropagation) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    EvalResult result = eval("=PERCENTILE(A1:A3, 1/0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

}  // namespace
}  // namespace cells
