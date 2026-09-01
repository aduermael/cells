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

TEST_F(FnStatsTest, LargeAndSmall) {
    setCellValue(0, 0, 10);
    setCellValue(0, 1, 30);
    setCellValue(0, 2, 20);
    EvalResult large = eval("=LARGE(A1:A3,2)");
    ASSERT_TRUE(large.isNumber());
    EXPECT_DOUBLE_EQ(large.getNumber(), 20.0);
    EvalResult small = eval("=SMALL(A1:A3,2)");
    ASSERT_TRUE(small.isNumber());
    EXPECT_DOUBLE_EQ(small.getNumber(), 20.0);
}

TEST_F(FnStatsTest, RankEq) {
    setCellValue(0, 0, 100);
    setCellValue(0, 1, 80);
    setCellValue(0, 2, 80);
    setCellValue(0, 3, 50);
    EvalResult r1 = eval("=RANK(100,A1:A4)");
    ASSERT_TRUE(r1.isNumber());
    EXPECT_DOUBLE_EQ(r1.getNumber(), 1.0);
    EvalResult r2 = eval("=RANK.EQ(80,A1:A4)");
    ASSERT_TRUE(r2.isNumber());
    EXPECT_DOUBLE_EQ(r2.getNumber(), 2.0);
    EvalResult r3 = eval("=RANK(50,A1:A4,1)");
    ASSERT_TRUE(r3.isNumber());
    EXPECT_DOUBLE_EQ(r3.getNumber(), 1.0);
}

TEST_F(FnStatsTest, ModeSngl) {
    EvalResult r = eval("=MODE.SNGL(1,2,2,3,3,3,4)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 3.0);
    EvalResult unique = eval("=MODE(1,2,3)");
    ASSERT_TRUE(unique.isError());
    EXPECT_EQ(unique.getError(), CellError::NA);
}

TEST_F(FnStatsTest, QuartileInc) {
    setCellValue(0, 0, 1);
    setCellValue(0, 1, 2);
    setCellValue(0, 2, 3);
    setCellValue(0, 3, 4);
    EvalResult q0 = eval("=QUARTILE.INC(A1:A4,0)");
    EvalResult q2 = eval("=QUARTILE.INC(A1:A4,2)");
    EvalResult q4 = eval("=QUARTILE.INC(A1:A4,4)");
    ASSERT_TRUE(q0.isNumber());
    ASSERT_TRUE(q2.isNumber());
    ASSERT_TRUE(q4.isNumber());
    EXPECT_DOUBLE_EQ(q0.getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(q2.getNumber(), 2.5);
    EXPECT_DOUBLE_EQ(q4.getNumber(), 4.0);
}

TEST_F(FnStatsTest, CountBlank) {
    setCellValue(0, 0, 1);
    EvalResult r = eval("=COUNTBLANK(A1:A3)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 2.0);
}

TEST_F(FnStatsTest, AvedevDevsqMeans) {
    EvalResult a = eval("=AVEDEV(1,2,3)");
    ASSERT_TRUE(a.isNumber());
    EXPECT_DOUBLE_EQ(a.getNumber(), 2.0 / 3.0);
    EvalResult d = eval("=DEVSQ(1,2,3)");
    ASSERT_TRUE(d.isNumber());
    EXPECT_DOUBLE_EQ(d.getNumber(), 2.0);
    EvalResult g = eval("=GEOMEAN(1,2,4)");
    ASSERT_TRUE(g.isNumber());
    EXPECT_DOUBLE_EQ(g.getNumber(), 2.0);
    EvalResult h = eval("=HARMEAN(1,2,4)");
    ASSERT_TRUE(h.isNumber());
    EXPECT_NEAR(h.getNumber(), 12.0 / 7.0, 1e-9);
    EvalResult z = eval("=GEOMEAN(-1,2)");
    ASSERT_TRUE(z.isError());
    EXPECT_EQ(z.getError(), CellError::NUM);
}

TEST_F(FnStatsTest, StandardizeForecastSlope) {
    EvalResult s = eval("=STANDARDIZE(5,3,2)");
    ASSERT_TRUE(s.isNumber());
    EXPECT_DOUBLE_EQ(s.getNumber(), 1.0);
    EvalResult sd0 = eval("=STANDARDIZE(1,0,0)");
    ASSERT_TRUE(sd0.isError());
    EXPECT_EQ(sd0.getError(), CellError::NUM);

    setCellValue(0, 0, 2.0);
    setCellValue(0, 1, 3.0);
    setCellValue(0, 2, 4.0);
    setCellValue(1, 0, 1.0);
    setCellValue(1, 1, 2.0);
    setCellValue(1, 2, 3.0);
    EvalResult sl = eval("=SLOPE(A1:A3,B1:B3)");
    ASSERT_TRUE(sl.isNumber());
    EXPECT_DOUBLE_EQ(sl.getNumber(), 1.0);
    EvalResult ic = eval("=INTERCEPT(A1:A3,B1:B3)");
    ASSERT_TRUE(ic.isNumber());
    EXPECT_DOUBLE_EQ(ic.getNumber(), 1.0);
    EvalResult fc = eval("=FORECAST(1,A1:A3,B1:B3)");
    ASSERT_TRUE(fc.isNumber());
    EXPECT_DOUBLE_EQ(fc.getNumber(), 2.0);
    EvalResult fl = eval("=FORECAST.LINEAR(1,A1:A3,B1:B3)");
    ASSERT_TRUE(fl.isNumber());
    EXPECT_DOUBLE_EQ(fl.getNumber(), 2.0);
    EvalResult cr = eval("=CORREL(A1:A3,B1:B3)");
    ASSERT_TRUE(cr.isNumber());
    EXPECT_DOUBLE_EQ(cr.getNumber(), 1.0);
    EvalResult rs = eval("=RSQ(A1:A3,B1:B3)");
    ASSERT_TRUE(rs.isNumber());
    EXPECT_DOUBLE_EQ(rs.getNumber(), 1.0);
}

TEST_F(FnStatsTest, CovarRankAvgPercentrank) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(1, 0, 1.0);
    setCellValue(1, 1, 2.0);
    setCellValue(1, 2, 3.0);
    EvalResult cv = eval("=COVAR(A1:A3,B1:B3)");
    ASSERT_TRUE(cv.isNumber());
    EXPECT_NEAR(cv.getNumber(), 2.0 / 3.0, 1e-9);
    EvalResult cvs = eval("=COVARIANCE.S(A1:A3,B1:B3)");
    ASSERT_TRUE(cvs.isNumber());
    EXPECT_NEAR(cvs.getNumber(), 1.0, 1e-9);

    setCellValue(2, 0, 1.0);
    setCellValue(2, 1, 2.0);
    setCellValue(2, 2, 2.0);
    setCellValue(2, 3, 4.0);
    EvalResult ra = eval("=RANK.AVG(2,C1:C4)");
    ASSERT_TRUE(ra.isNumber());
    EXPECT_DOUBLE_EQ(ra.getNumber(), 2.5);

    EvalResult pr = eval("=PERCENTRANK.INC(A1:A3,2)");
    ASSERT_TRUE(pr.isNumber());
    EXPECT_DOUBLE_EQ(pr.getNumber(), 0.5);
    EvalResult dotted = eval("=STDEV.S(1,2,3)");
    EvalResult alias = eval("=STDEVS(1,2,3)");
    ASSERT_TRUE(dotted.isNumber());
    ASSERT_TRUE(alias.isNumber());
    EXPECT_DOUBLE_EQ(dotted.getNumber(), alias.getNumber());
}

TEST_F(FnStatsTest, AverageAMinAMaxA) {
    EvalResult a = eval("=AVERAGEA(1,TRUE,\"x\")");
    ASSERT_TRUE(a.isNumber());
    EXPECT_DOUBLE_EQ(a.getNumber(), 2.0 / 3.0);
    EvalResult mn = eval("=MINA(1,TRUE)");
    ASSERT_TRUE(mn.isNumber());
    EXPECT_DOUBLE_EQ(mn.getNumber(), 1.0);
    EvalResult mx = eval("=MAXA(1,TRUE)");
    ASSERT_TRUE(mx.isNumber());
    EXPECT_DOUBLE_EQ(mx.getNumber(), 1.0);
}

TEST_F(FnStatsTest, FisherPhiGaussNorm) {
    EvalResult f = eval("=FISHER(0.75)");
    ASSERT_TRUE(f.isNumber());
    EXPECT_NEAR(f.getNumber(), 0.5 * std::log(1.75 / 0.25), 1e-12);
    EvalResult fi = eval("=FISHERINV(0.972955074527657)");
    ASSERT_TRUE(fi.isNumber());
    EXPECT_NEAR(fi.getNumber(), 0.75, 1e-9);
    EvalResult phi = eval("=PHI(0)");
    ASSERT_TRUE(phi.isNumber());
    EXPECT_NEAR(phi.getNumber(), 0.3989422804014327, 1e-12);
    EvalResult gauss = eval("=GAUSS(0)");
    ASSERT_TRUE(gauss.isNumber());
    EXPECT_DOUBLE_EQ(gauss.getNumber(), 0.0);
    EvalResult ns = eval("=NORMSDIST(0)");
    ASSERT_TRUE(ns.isNumber());
    EXPECT_DOUBLE_EQ(ns.getNumber(), 0.5);
    EvalResult nsd = eval("=NORM.S.DIST(0,TRUE)");
    ASSERT_TRUE(nsd.isNumber());
    EXPECT_DOUBLE_EQ(nsd.getNumber(), 0.5);
    EvalResult nsp = eval("=NORM.S.DIST(0,FALSE)");
    ASSERT_TRUE(nsp.isNumber());
    EXPECT_NEAR(nsp.getNumber(), phi.getNumber(), 1e-12);
    EvalResult nd = eval("=NORM.DIST(0,0,1,TRUE)");
    ASSERT_TRUE(nd.isNumber());
    EXPECT_DOUBLE_EQ(nd.getNumber(), 0.5);
    EvalResult ndl = eval("=NORMDIST(0,0,1,TRUE)");
    ASSERT_TRUE(ndl.isNumber());
    EXPECT_DOUBLE_EQ(ndl.getNumber(), 0.5);
    EXPECT_EQ(eval("=FISHER(1)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=FISHER()").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=PHI(0,1)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=NORM.DIST(0,0,0,TRUE)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=NORM.S.DIST(0)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=GAUSS(\"x\")").getError(), CellError::VALUE);
}

TEST_F(FnStatsTest, SkewKurtStdeva) {
    EvalResult skew = eval("=SKEW(3,4,5,2,3,4,5,6,4,7)");
    ASSERT_TRUE(skew.isNumber());
    EXPECT_NEAR(skew.getNumber(), 0.359543071407, 1e-9);
    EvalResult skewp = eval("=SKEW.P(3,4,5,2,3,4,5,6,4,7)");
    ASSERT_TRUE(skewp.isNumber());
    EXPECT_NEAR(skewp.getNumber(), 0.303193339354, 1e-9);
    EvalResult kurt = eval("=KURT(3,4,5,2,3,4,5,6,4,7)");
    ASSERT_TRUE(kurt.isNumber());
    EXPECT_NEAR(kurt.getNumber(), -0.151799637209, 1e-8);
    EvalResult vara = eval("=VARA(1,TRUE)");
    ASSERT_TRUE(vara.isNumber());
    EXPECT_DOUBLE_EQ(vara.getNumber(), 0.0);
    EvalResult stdeva = eval("=STDEVA(1,TRUE,0)");
    ASSERT_TRUE(stdeva.isNumber());
    EXPECT_NEAR(stdeva.getNumber(), std::sqrt(eval("=VARA(1,TRUE,0)").getNumber()), 1e-12);
    EvalResult varpa = eval("=VARPA(1,TRUE)");
    ASSERT_TRUE(varpa.isNumber());
    EXPECT_DOUBLE_EQ(varpa.getNumber(), 0.0);
    EvalResult stdevpa = eval("=STDEVPA(1,0)");
    ASSERT_TRUE(stdevpa.isNumber());
    EXPECT_DOUBLE_EQ(stdevpa.getNumber(), 0.5);
    EXPECT_EQ(eval("=SKEW(1,2)").getError(), CellError::DIV);
    EXPECT_EQ(eval("=KURT(1,2,3)").getError(), CellError::DIV);
    EXPECT_EQ(eval("=SKEW()").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=STDEVA()").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=VARA(1)").getError(), CellError::DIV);
}

TEST_F(FnStatsTest, Steyx) {
    setCellValue(0, 0, 2.0);
    setCellValue(0, 1, 3.0);
    setCellValue(0, 2, 9.0);
    setCellValue(0, 3, 1.0);
    setCellValue(0, 4, 8.0);
    setCellValue(1, 0, 6.0);
    setCellValue(1, 1, 5.0);
    setCellValue(1, 2, 11.0);
    setCellValue(1, 3, 7.0);
    setCellValue(1, 4, 5.0);
    EvalResult s = eval("=STEYX(A1:A5,B1:B5)");
    ASSERT_TRUE(s.isNumber());
    EXPECT_NEAR(s.getNumber(), 3.74560674557, 1e-8);
    EXPECT_EQ(eval("=STEYX(A1:A5)").getError(), CellError::VALUE);
    setCellValue(2, 0, 1.0);
    setCellValue(2, 1, 2.0);
    setCellValue(3, 0, 1.0);
    setCellValue(3, 1, 2.0);
    EXPECT_EQ(eval("=STEYX(C1:C2,D1:D2)").getError(), CellError::DIV);
}

TEST_F(FnStatsTest, NormInvPoissonExponConfidenceTrimmean) {
    EvalResult half = eval("=NORMSINV(0.5)");
    ASSERT_TRUE(half.isNumber());
    EXPECT_NEAR(half.getNumber(), 0.0, 1e-12);
    EvalResult alias = eval("=NORM.S.INV(0.5)");
    ASSERT_TRUE(alias.isNumber());
    EXPECT_NEAR(alias.getNumber(), 0.0, 1e-12);
    EvalResult loc = eval("=NORMINV(0.5,10,2)");
    ASSERT_TRUE(loc.isNumber());
    EXPECT_NEAR(loc.getNumber(), 10.0, 1e-12);
    EvalResult loc2 = eval("=NORM.INV(0.5,10,2)");
    ASSERT_TRUE(loc2.isNumber());
    EXPECT_NEAR(loc2.getNumber(), 10.0, 1e-12);
    EvalResult roundtrip = eval("=NORMSINV(NORMSDIST(1))");
    ASSERT_TRUE(roundtrip.isNumber());
    EXPECT_NEAR(roundtrip.getNumber(), 1.0, 1e-9);
    EXPECT_EQ(eval("=NORMSINV(0)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=NORMINV(0.5,0,0)").getError(), CellError::NUM);

    EvalResult pmf = eval("=POISSON(2,5,FALSE)");
    ASSERT_TRUE(pmf.isNumber());
    EXPECT_NEAR(pmf.getNumber(), 0.084224337488568, 1e-12);
    EvalResult pmf2 = eval("=POISSON.DIST(2,5,FALSE)");
    ASSERT_TRUE(pmf2.isNumber());
    EXPECT_NEAR(pmf2.getNumber(), pmf.getNumber(), 1e-15);
    EvalResult cdf = eval("=POISSON(0,1,TRUE)");
    ASSERT_TRUE(cdf.isNumber());
    EXPECT_NEAR(cdf.getNumber(), std::exp(-1.0), 1e-12);
    EXPECT_EQ(eval("=POISSON(-1,1,TRUE)").getError(), CellError::NUM);

    EvalResult expCdf = eval("=EXPONDIST(0.5,1,TRUE)");
    ASSERT_TRUE(expCdf.isNumber());
    EXPECT_NEAR(expCdf.getNumber(), 1.0 - std::exp(-0.5), 1e-12);
    EvalResult expPdf = eval("=EXPON.DIST(0,2,FALSE)");
    ASSERT_TRUE(expPdf.isNumber());
    EXPECT_NEAR(expPdf.getNumber(), 2.0, 1e-12);
    EXPECT_EQ(eval("=EXPONDIST(-0.1,1,TRUE)").getError(), CellError::NUM);

    EvalResult z = eval("=NORMSINV(0.975)");
    ASSERT_TRUE(z.isNumber());
    EvalResult conf = eval("=CONFIDENCE(0.05,2.5,50)");
    ASSERT_TRUE(conf.isNumber());
    EXPECT_NEAR(conf.getNumber(), z.getNumber() * 2.5 / std::sqrt(50.0), 1e-12);
    EvalResult conf2 = eval("=CONFIDENCE.NORM(0.05,2.5,50)");
    ASSERT_TRUE(conf2.isNumber());
    EXPECT_NEAR(conf2.getNumber(), conf.getNumber(), 1e-15);
    EXPECT_EQ(eval("=CONFIDENCE(0,1,10)").getError(), CellError::NUM);

    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 4.0);
    EvalResult trim = eval("=TRIMMEAN(A1:A4,0.5)");
    ASSERT_TRUE(trim.isNumber());
    EXPECT_DOUBLE_EQ(trim.getNumber(), 2.5);
    EXPECT_EQ(eval("=TRIMMEAN(A1:A4,1)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=TRIMMEAN(A1:A4)").getError(), CellError::VALUE);
}

TEST_F(FnStatsTest, ModeMult) {
    EvalResult r = eval("=MODE.MULT(1,2,2,3,3,4)");
    ASSERT_TRUE(r.isArray());
    EXPECT_EQ(r.getArrayRows(), 2u);
    EXPECT_EQ(r.getArrayCols(), 1u);
    EXPECT_DOUBLE_EQ(r.getArrayAt(0, 0).getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(r.getArrayAt(1, 0).getNumber(), 3.0);
    EXPECT_EQ(eval("=MODE.MULT(1,2,3)").getError(), CellError::NA);
    EvalResult one = eval("=MODE.MULT(1,1,1,2,2)");
    ASSERT_TRUE(one.isArray());
    EXPECT_EQ(one.getArrayRows(), 1u);
    EXPECT_DOUBLE_EQ(one.getArrayAt(0, 0).getNumber(), 1.0);
}

TEST_F(FnStatsTest, BinomWeibullLognormGammaHypgeomNegbinom) {
    EvalResult pmf = eval("=BINOM.DIST(2,5,0.5,FALSE)");
    ASSERT_TRUE(pmf.isNumber());
    EXPECT_DOUBLE_EQ(pmf.getNumber(), 0.3125);
    EvalResult cdf = eval("=BINOMDIST(2,5,0.5,TRUE)");
    ASSERT_TRUE(cdf.isNumber());
    EXPECT_DOUBLE_EQ(cdf.getNumber(), 0.5);
    EvalResult inv = eval("=BINOM.INV(6,0.5,0.75)");
    ASSERT_TRUE(inv.isNumber());
    EXPECT_DOUBLE_EQ(inv.getNumber(), 4.0);
    EvalResult crit = eval("=CRITBINOM(6,0.5,0.75)");
    ASSERT_TRUE(crit.isNumber());
    EXPECT_DOUBLE_EQ(crit.getNumber(), 4.0);
    EvalResult rng = eval("=BINOM.DIST.RANGE(5,0.5,1,2)");
    ASSERT_TRUE(rng.isNumber());
    EXPECT_DOUBLE_EQ(rng.getNumber(), 0.46875);
    EvalResult one = eval("=BINOM.DIST.RANGE(5,0.5,2)");
    ASSERT_TRUE(one.isNumber());
    EXPECT_DOUBLE_EQ(one.getNumber(), 0.3125);
    EXPECT_EQ(eval("=BINOM.DIST(3,2,0.5,FALSE)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=BINOM.DIST(1,5,1.5,TRUE)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=BINOM.INV(5,0.5,0)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=BINOM.DIST.RANGE(5,0.5,2,1)").getError(), CellError::NUM);

    EvalResult wCdf = eval("=WEIBULL.DIST(1,1,1,TRUE)");
    ASSERT_TRUE(wCdf.isNumber());
    EXPECT_NEAR(wCdf.getNumber(), 1.0 - std::exp(-1.0), 1e-12);
    EvalResult wPdf = eval("=WEIBULL(1,1,1,FALSE)");
    ASSERT_TRUE(wPdf.isNumber());
    EXPECT_NEAR(wPdf.getNumber(), std::exp(-1.0), 1e-12);
    EXPECT_EQ(eval("=WEIBULL.DIST(-1,1,1,TRUE)").getError(), CellError::NUM);

    EvalResult lnCdf = eval("=LOGNORM.DIST(1,0,1,TRUE)");
    ASSERT_TRUE(lnCdf.isNumber());
    EXPECT_NEAR(lnCdf.getNumber(), 0.5, 1e-12);
    EvalResult lnPdf = eval("=LOGNORM.DIST(1,0,1,FALSE)");
    ASSERT_TRUE(lnPdf.isNumber());
    EXPECT_NEAR(lnPdf.getNumber(), eval("=PHI(0)").getNumber(), 1e-12);
    EvalResult lnLegacy = eval("=LOGNORMDIST(1,0,1)");
    ASSERT_TRUE(lnLegacy.isNumber());
    EXPECT_NEAR(lnLegacy.getNumber(), 0.5, 1e-12);
    EvalResult lnInv = eval("=LOGNORM.INV(0.5,0,1)");
    ASSERT_TRUE(lnInv.isNumber());
    EXPECT_NEAR(lnInv.getNumber(), 1.0, 1e-12);
    EvalResult loginv = eval("=LOGINV(0.5,0,1)");
    ASSERT_TRUE(loginv.isNumber());
    EXPECT_NEAR(loginv.getNumber(), 1.0, 1e-12);
    EXPECT_EQ(eval("=LOGNORM.DIST(0,0,1,TRUE)").getError(), CellError::NUM);

    EvalResult gCdf = eval("=GAMMA.DIST(1,1,1,TRUE)");
    ASSERT_TRUE(gCdf.isNumber());
    EXPECT_NEAR(gCdf.getNumber(), 1.0 - std::exp(-1.0), 1e-10);
    EvalResult gPdf = eval("=GAMMADIST(1,1,1,FALSE)");
    ASSERT_TRUE(gPdf.isNumber());
    EXPECT_NEAR(gPdf.getNumber(), std::exp(-1.0), 1e-10);
    EvalResult gInv = eval("=GAMMA.INV(0.5,1,1)");
    ASSERT_TRUE(gInv.isNumber());
    EXPECT_NEAR(gInv.getNumber(), -std::log(0.5), 1e-8);
    EvalResult gInv2 = eval("=GAMMAINV(0.5,1,1)");
    ASSERT_TRUE(gInv2.isNumber());
    EXPECT_NEAR(gInv2.getNumber(), gInv.getNumber(), 1e-12);
    EvalResult roundtrip = eval("=GAMMA.DIST(GAMMA.INV(0.8,2,3),2,3,TRUE)");
    ASSERT_TRUE(roundtrip.isNumber());
    EXPECT_NEAR(roundtrip.getNumber(), 0.8, 1e-6);
    EXPECT_EQ(eval("=GAMMA.DIST(-1,1,1,TRUE)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=GAMMA.INV(1,1,1)").getError(), CellError::NUM);

    EvalResult hg = eval("=HYPGEOM.DIST(1,4,8,20,FALSE)");
    ASSERT_TRUE(hg.isNumber());
    EXPECT_NEAR(hg.getNumber(), 1760.0 / 4845.0, 1e-12);
    EvalResult hgCdf = eval("=HYPGEOM.DIST(1,4,8,20,TRUE)");
    ASSERT_TRUE(hgCdf.isNumber());
    EXPECT_GE(hgCdf.getNumber(), hg.getNumber());
    EvalResult hgLegacy = eval("=HYPGEOMDIST(1,4,8,20)");
    ASSERT_TRUE(hgLegacy.isNumber());
    EXPECT_NEAR(hgLegacy.getNumber(), hg.getNumber(), 1e-15);
    EXPECT_EQ(eval("=HYPGEOM.DIST(5,4,8,20,FALSE)").getError(), CellError::NUM);

    EvalResult nb = eval("=NEGBINOM.DIST(10,5,0.25,FALSE)");
    ASSERT_TRUE(nb.isNumber());
    EXPECT_NEAR(nb.getNumber(), 0.0550486603756, 1e-10);
    EvalResult nbCdf = eval("=NEGBINOM.DIST(0,5,0.25,TRUE)");
    ASSERT_TRUE(nbCdf.isNumber());
    EXPECT_NEAR(nbCdf.getNumber(), std::pow(0.25, 5.0), 1e-12);
    EvalResult nbLegacy = eval("=NEGBINOMDIST(10,5,0.25)");
    ASSERT_TRUE(nbLegacy.isNumber());
    EXPECT_NEAR(nbLegacy.getNumber(), nb.getNumber(), 1e-15);
    EXPECT_EQ(eval("=NEGBINOM.DIST(1,0,0.5,FALSE)").getError(), CellError::NUM);
}

TEST_F(FnStatsTest, ChiSqDistInvAndAliases) {
    EvalResult cdf = eval("=CHISQ.DIST(2,2,TRUE)");
    ASSERT_TRUE(cdf.isNumber());
    EXPECT_NEAR(cdf.getNumber(), 1.0 - std::exp(-1.0), 1e-12);
    EvalResult gamma = eval("=GAMMA.DIST(2,1,2,TRUE)");
    ASSERT_TRUE(gamma.isNumber());
    EXPECT_NEAR(cdf.getNumber(), gamma.getNumber(), 1e-12);
    EvalResult pdf = eval("=CHISQ.DIST(2,2,FALSE)");
    ASSERT_TRUE(pdf.isNumber());
    EXPECT_NEAR(pdf.getNumber(), eval("=GAMMA.DIST(2,1,2,FALSE)").getNumber(), 1e-12);
    EvalResult pdf0 = eval("=CHISQ.DIST(0,2,FALSE)");
    ASSERT_TRUE(pdf0.isNumber());
    EXPECT_NEAR(pdf0.getNumber(), 0.5, 1e-12);

    EvalResult rt = eval("=CHISQ.DIST.RT(2,2)");
    ASSERT_TRUE(rt.isNumber());
    EXPECT_NEAR(rt.getNumber(), std::exp(-1.0), 1e-12);
    EvalResult chidist = eval("=CHIDIST(2,2)");
    ASSERT_TRUE(chidist.isNumber());
    EXPECT_NEAR(chidist.getNumber(), rt.getNumber(), 1e-15);
    EXPECT_NEAR(rt.getNumber() + cdf.getNumber(), 1.0, 1e-12);

    EvalResult invRt = eval("=CHISQ.INV.RT(0.05,10)");
    ASSERT_TRUE(invRt.isNumber());
    EXPECT_NEAR(invRt.getNumber(), 18.307038053275146, 1e-8);
    EvalResult chiinv = eval("=CHIINV(0.05,10)");
    ASSERT_TRUE(chiinv.isNumber());
    EXPECT_NEAR(chiinv.getNumber(), invRt.getNumber(), 1e-12);
    EvalResult invLeft = eval("=CHISQ.INV(0.95,10)");
    ASSERT_TRUE(invLeft.isNumber());
    EXPECT_NEAR(invLeft.getNumber(), invRt.getNumber(), 1e-8);
    EvalResult median = eval("=CHISQ.INV(0.5,2)");
    ASSERT_TRUE(median.isNumber());
    EXPECT_NEAR(median.getNumber(), -2.0 * std::log(0.5), 1e-8);

    EvalResult roundtrip = eval("=CHISQ.DIST(CHISQ.INV(0.8,5),5,TRUE)");
    ASSERT_TRUE(roundtrip.isNumber());
    EXPECT_NEAR(roundtrip.getNumber(), 0.8, 1e-6);
    EvalResult roundtripRt = eval("=CHISQ.DIST.RT(CHISQ.INV.RT(0.2,8),8)");
    ASSERT_TRUE(roundtripRt.isNumber());
    EXPECT_NEAR(roundtripRt.getNumber(), 0.2, 1e-6);

    EvalResult zeroInv = eval("=CHISQ.INV(0,4)");
    ASSERT_TRUE(zeroInv.isNumber());
    EXPECT_NEAR(zeroInv.getNumber(), 0.0, 1e-12);
    EvalResult oneInvRt = eval("=CHISQ.INV.RT(1,4)");
    ASSERT_TRUE(oneInvRt.isNumber());
    EXPECT_NEAR(oneInvRt.getNumber(), 0.0, 1e-12);

    EvalResult truncDf = eval("=CHISQ.DIST(2,2.9,TRUE)");
    ASSERT_TRUE(truncDf.isNumber());
    EXPECT_NEAR(truncDf.getNumber(), cdf.getNumber(), 1e-12);

    EXPECT_EQ(eval("=CHISQ.DIST(-0.1,2,TRUE)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=CHISQ.DIST(1,0.5,TRUE)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=CHISQ.DIST(1,2)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=CHISQ.DIST.RT(-1,2)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=CHISQ.INV(1,5)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=CHISQ.INV.RT(0,5)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=CHISQ.INV(-0.1,5)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=CHISQ.DIST(0,1,FALSE)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=CHIINV()").getError(), CellError::VALUE);
}

TEST_F(FnStatsTest, ChiSqTestPearson) {
    setCellValue(0, 0, 16.0);
    setCellValue(0, 1, 4.0);
    setCellValue(1, 0, 10.0);
    setCellValue(1, 1, 10.0);
    EvalResult oneD = eval("=CHISQ.TEST(A1:A2,B1:B2)");
    ASSERT_TRUE(oneD.isNumber());
    EvalResult sf = eval("=CHISQ.DIST.RT(7.2,1)");
    ASSERT_TRUE(sf.isNumber());
    EXPECT_NEAR(oneD.getNumber(), sf.getNumber(), 1e-12);
    EvalResult alias = eval("=CHITEST(A1:A2,B1:B2)");
    ASSERT_TRUE(alias.isNumber());
    EXPECT_NEAR(alias.getNumber(), oneD.getNumber(), 1e-15);

    setCellValue(0, 0, 10.0);
    setCellValue(1, 0, 20.0);
    setCellValue(0, 1, 20.0);
    setCellValue(1, 1, 10.0);
    setCellValue(2, 0, 15.0);
    setCellValue(3, 0, 15.0);
    setCellValue(2, 1, 15.0);
    setCellValue(3, 1, 15.0);
    EvalResult grid = eval("=CHISQ.TEST(A1:B2,C1:D2)");
    ASSERT_TRUE(grid.isNumber());
    EvalResult gridSf = eval("=CHISQ.DIST.RT(20/3,1)");
    ASSERT_TRUE(gridSf.isNumber());
    EXPECT_NEAR(grid.getNumber(), gridSf.getNumber(), 1e-12);

    setCellValue(0, 0, 5.0);
    setCellValue(0, 1, 5.0);
    setCellValue(0, 2, 5.0);
    setCellValue(1, 0, 5.0);
    setCellValue(1, 1, 5.0);
    setCellValue(1, 2, 5.0);
    EvalResult match = eval("=CHISQ.TEST(A1:A3,B1:B3)");
    ASSERT_TRUE(match.isNumber());
    EXPECT_NEAR(match.getNumber(), 1.0, 1e-12);

    EXPECT_EQ(eval("=CHISQ.TEST(A1:A3,B1:B2)").getError(), CellError::NA);
    setCellValue(4, 0, 0.0);
    setCellValue(4, 1, 10.0);
    EXPECT_EQ(eval("=CHISQ.TEST(A1:A2,E1:E2)").getError(), CellError::DIV);
    setCellValue(4, 0, -1.0);
    EXPECT_EQ(eval("=CHISQ.TEST(A1:A2,E1:E2)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=CHISQ.TEST(A1:A2)").getError(), CellError::VALUE);
}

TEST_F(FnStatsTest, StudentTDistInvAndTails) {
    EvalResult pdf = eval("=T.DIST(0,1,FALSE)");
    ASSERT_TRUE(pdf.isNumber());
    EXPECT_NEAR(pdf.getNumber(), 1.0 / 3.14159265358979323846, 1e-12);
    EvalResult cdf = eval("=T.DIST(0,10,TRUE)");
    ASSERT_TRUE(cdf.isNumber());
    EXPECT_NEAR(cdf.getNumber(), 0.5, 1e-12);
    EvalResult cauchyCdf = eval("=T.DIST(1,1,TRUE)");
    ASSERT_TRUE(cauchyCdf.isNumber());
    EXPECT_NEAR(cauchyCdf.getNumber(), 0.75, 1e-12);

    EvalResult rt = eval("=T.DIST.RT(1,1)");
    ASSERT_TRUE(rt.isNumber());
    EXPECT_NEAR(rt.getNumber(), 0.25, 1e-12);
    EvalResult rtNeg = eval("=T.DIST.RT(-1,1)");
    ASSERT_TRUE(rtNeg.isNumber());
    EXPECT_NEAR(rtNeg.getNumber(), 0.75, 1e-12);
    EvalResult twoT = eval("=T.DIST.2T(1,1)");
    ASSERT_TRUE(twoT.isNumber());
    EXPECT_NEAR(twoT.getNumber(), 0.5, 1e-12);
    EXPECT_NEAR(twoT.getNumber(), 2.0 * rt.getNumber(), 1e-12);

    EvalResult tdist1 = eval("=TDIST(1,1,1)");
    ASSERT_TRUE(tdist1.isNumber());
    EXPECT_NEAR(tdist1.getNumber(), rt.getNumber(), 1e-15);
    EvalResult tdist2 = eval("=TDIST(1,1,2)");
    ASSERT_TRUE(tdist2.isNumber());
    EXPECT_NEAR(tdist2.getNumber(), twoT.getNumber(), 1e-15);
    EvalResult tdistTrunc = eval("=TDIST(1,1,2.9)");
    ASSERT_TRUE(tdistTrunc.isNumber());
    EXPECT_NEAR(tdistTrunc.getNumber(), twoT.getNumber(), 1e-15);

    EvalResult inv = eval("=T.INV(0.75,1)");
    ASSERT_TRUE(inv.isNumber());
    EXPECT_NEAR(inv.getNumber(), 1.0, 1e-9);
    EvalResult invLow = eval("=T.INV(0.25,1)");
    ASSERT_TRUE(invLow.isNumber());
    EXPECT_NEAR(invLow.getNumber(), -1.0, 1e-9);
    EvalResult inv2t = eval("=T.INV.2T(0.5,1)");
    ASSERT_TRUE(inv2t.isNumber());
    EXPECT_NEAR(inv2t.getNumber(), 1.0, 1e-9);
    EvalResult tinv = eval("=TINV(0.5,1)");
    ASSERT_TRUE(tinv.isNumber());
    EXPECT_NEAR(tinv.getNumber(), inv2t.getNumber(), 1e-15);
    EvalResult crit = eval("=T.INV.2T(0.05,10)");
    ASSERT_TRUE(crit.isNumber());
    EXPECT_NEAR(crit.getNumber(), 2.2281388519649385, 1e-8);
    EvalResult leftCrit = eval("=T.INV(0.975,10)");
    ASSERT_TRUE(leftCrit.isNumber());
    EXPECT_NEAR(leftCrit.getNumber(), crit.getNumber(), 1e-8);
    EvalResult half = eval("=T.INV(0.5,8)");
    ASSERT_TRUE(half.isNumber());
    EXPECT_NEAR(half.getNumber(), 0.0, 1e-12);
    EvalResult inv2tOne = eval("=T.INV.2T(1,5)");
    ASSERT_TRUE(inv2tOne.isNumber());
    EXPECT_NEAR(inv2tOne.getNumber(), 0.0, 1e-12);

    EvalResult roundtrip = eval("=T.DIST(T.INV(0.8,7),7,TRUE)");
    ASSERT_TRUE(roundtrip.isNumber());
    EXPECT_NEAR(roundtrip.getNumber(), 0.8, 1e-6);
    EvalResult roundtrip2t = eval("=T.DIST.2T(T.INV.2T(0.1,9),9)");
    ASSERT_TRUE(roundtrip2t.isNumber());
    EXPECT_NEAR(roundtrip2t.getNumber(), 0.1, 1e-6);

    EXPECT_EQ(eval("=T.DIST.2T(-1,5)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=TDIST(-1,5,1)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=TDIST(1,5,3)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=T.DIST(0,0.5,TRUE)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=T.DIST(0,10)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=T.INV(0,5)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=T.INV(1,5)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=T.INV.2T(0,5)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=T.DIST.RT(1)").getError(), CellError::VALUE);
}

TEST_F(FnStatsTest, TTestTypesAndTails) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 4.0);
    setCellValue(0, 4, 5.0);
    setCellValue(1, 0, 2.0);
    setCellValue(1, 1, 4.0);
    setCellValue(1, 2, 6.0);
    setCellValue(1, 3, 8.0);
    setCellValue(1, 4, 10.0);
    EvalResult paired2 = eval("=T.TEST(A1:A5,B1:B5,2,1)");
    ASSERT_TRUE(paired2.isNumber());
    EvalResult pairedSf = eval("=T.DIST.2T(4.242640687119285,4)");
    ASSERT_TRUE(pairedSf.isNumber());
    EXPECT_NEAR(paired2.getNumber(), pairedSf.getNumber(), 1e-9);
    EvalResult paired1 = eval("=T.TEST(A1:A5,B1:B5,1,1)");
    ASSERT_TRUE(paired1.isNumber());
    EXPECT_NEAR(paired1.getNumber(), paired2.getNumber() / 2.0, 1e-12);
    EvalResult pairedAlias = eval("=TTEST(A1:A5,B1:B5,2,1)");
    ASSERT_TRUE(pairedAlias.isNumber());
    EXPECT_NEAR(pairedAlias.getNumber(), paired2.getNumber(), 1e-15);

    setCellValue(2, 0, 1.0);
    setCellValue(2, 1, 2.0);
    setCellValue(2, 2, 3.0);
    setCellValue(2, 3, 4.0);
    setCellValue(3, 0, 2.0);
    setCellValue(3, 1, 4.0);
    setCellValue(3, 2, 6.0);
    EvalResult eqVar = eval("=T.TEST(C1:C4,D1:D3,2,2)");
    ASSERT_TRUE(eqVar.isNumber());
    EvalResult eqVarSf = eval("=T.DIST.2T(1.2179969144117253,5)");
    ASSERT_TRUE(eqVarSf.isNumber());
    EXPECT_NEAR(eqVar.getNumber(), eqVarSf.getNumber(), 1e-9);
    EvalResult eqVar1 = eval("=T.TEST(C1:C4,D1:D3,1,2)");
    ASSERT_TRUE(eqVar1.isNumber());
    EXPECT_NEAR(eqVar1.getNumber(), eqVar.getNumber() / 2.0, 1e-12);

    EvalResult welch = eval("=T.TEST(C1:C4,D1:D3,2,3)");
    ASSERT_TRUE(welch.isNumber());
    EXPECT_NEAR(welch.getNumber(), 0.3338237000775014, 1e-8);
    EvalResult welch1 = eval("=T.TEST(C1:C4,D1:D3,1,3)");
    ASSERT_TRUE(welch1.isNumber());
    EXPECT_NEAR(welch1.getNumber(), welch.getNumber() / 2.0, 1e-12);
    EXPECT_NE(welch.getNumber(), eqVar.getNumber());

    setCellValue(4, 0, 3.0);
    setCellValue(4, 1, 4.0);
    setCellValue(4, 2, 5.0);
    setCellValue(4, 3, 8.0);
    setCellValue(4, 4, 9.0);
    setCellValue(4, 5, 1.0);
    setCellValue(4, 6, 2.0);
    setCellValue(4, 7, 4.0);
    setCellValue(4, 8, 5.0);
    setCellValue(5, 0, 6.0);
    setCellValue(5, 1, 19.0);
    setCellValue(5, 2, 3.0);
    setCellValue(5, 3, 2.0);
    setCellValue(5, 4, 14.0);
    setCellValue(5, 5, 4.0);
    setCellValue(5, 6, 5.0);
    setCellValue(5, 7, 17.0);
    setCellValue(5, 8, 1.0);
    EvalResult msPaired = eval("=T.TEST(E1:E9,F1:F9,2,1)");
    ASSERT_TRUE(msPaired.isNumber());
    EXPECT_NEAR(msPaired.getNumber(), 0.19601578492528215, 1e-8);
    EvalResult msEq = eval("=T.TEST(E1:E9,F1:F9,2,2)");
    ASSERT_TRUE(msEq.isNumber());
    EXPECT_NEAR(msEq.getNumber(), 0.19199588676039628, 1e-8);
    EvalResult msWelch = eval("=T.TEST(E1:E9,F1:F9,2,3)");
    ASSERT_TRUE(msWelch.isNumber());
    EXPECT_NEAR(msWelch.getNumber(), 0.2022939233686779, 1e-8);

    EXPECT_EQ(eval("=T.TEST(A1:A5,B1:B4,2,1)").getError(), CellError::NA);
    EXPECT_EQ(eval("=T.TEST(A1:A5,B1:B5,3,1)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=T.TEST(A1:A5,B1:B5,2,4)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=T.TEST(A1:A5,B1:B5,2)").getError(), CellError::VALUE);
    setCellValue(6, 0, 1.0);
    setCellValue(6, 1, 2.0);
    setCellValue(7, 0, 1.0);
    setCellValue(7, 1, 2.0);
    EXPECT_EQ(eval("=T.TEST(G1:G2,H1:H2,2,1)").getError(), CellError::DIV);
}

TEST_F(FnStatsTest, ZTestOptionalSigma) {
    setCellValue(0, 0, 4.0);
    setCellValue(0, 1, 5.0);
    setCellValue(0, 2, 6.0);
    EvalResult center = eval("=Z.TEST(A1:A3,5)");
    ASSERT_TRUE(center.isNumber());
    EXPECT_NEAR(center.getNumber(), 0.5, 1e-12);
    EvalResult alias = eval("=ZTEST(A1:A3,5)");
    ASSERT_TRUE(alias.isNumber());
    EXPECT_NEAR(alias.getNumber(), 0.5, 1e-15);

    EvalResult vs4 = eval("=Z.TEST(A1:A3,4)");
    ASSERT_TRUE(vs4.isNumber());
    EXPECT_NEAR(vs4.getNumber(), 0.041632258331775196, 1e-12);
    EvalResult withSigma = eval("=Z.TEST(A1:A3,4,1)");
    ASSERT_TRUE(withSigma.isNumber());
    EXPECT_NEAR(withSigma.getNumber(), vs4.getNumber(), 1e-12);
    EvalResult sigma2 = eval("=Z.TEST(A1:A3,4,2)");
    ASSERT_TRUE(sigma2.isNumber());
    EXPECT_NEAR(sigma2.getNumber(), 0.19323811538561642, 1e-12);
    EvalResult norms = eval("=1-NORMSDIST(SQRT(3)/2)");
    ASSERT_TRUE(norms.isNumber());
    EXPECT_NEAR(sigma2.getNumber(), norms.getNumber(), 1e-12);

    EXPECT_EQ(eval("=Z.TEST(A1:A3,4,0)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=Z.TEST(A1:A3,4,-1)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=Z.TEST(A1,4)").getError(), CellError::DIV);
    EXPECT_EQ(eval("=Z.TEST()").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=Z.TEST(A1:A3)").getError(), CellError::VALUE);
}

}  // namespace
}  // namespace cells
