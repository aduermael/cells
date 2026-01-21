#include "core/cells/functions/fn_rand.h"

#include <cmath>

#include <gtest/gtest.h>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_functions.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

namespace cells {
namespace {

class FnRandTest : public ::testing::Test {
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

        // Resolve references
        FormulaResolver resolver(*workbook, *sheet);
        resolver.resolve(ast.get(), false);  // legacy mode for tests

        // Evaluate
        std::unordered_set<ID> evaluating;
        EvalContext ctx;
        ctx.sheet = sheet;
        ctx.workbook = workbook.get();
        ctx.evaluatingCells = &evaluating;
        ctx.recursionDepth = 0;

        return evaluate(ast.get(), ctx);
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26];
    ID rowIds[100];
};

// =============================================================================
// RAND Tests
// =============================================================================

TEST_F(FnRandTest, RANDReturnsValueBetween0And1) {
    // Test multiple times to check randomness
    for (int i = 0; i < 100; ++i) {
        EvalResult result = eval("=RAND()");
        ASSERT_TRUE(result.isNumber()) << "Iteration " << i;
        double value = result.getNumber();
        EXPECT_GE(value, 0.0) << "Value should be >= 0";
        EXPECT_LT(value, 1.0) << "Value should be < 1";
    }
}

TEST_F(FnRandTest, RANDProducesVariation) {
    // Generate 20 random values and verify we get some variation
    std::set<double> values;
    for (int i = 0; i < 20; ++i) {
        EvalResult result = eval("=RAND()");
        ASSERT_TRUE(result.isNumber());
        values.insert(result.getNumber());
    }
    // We should have at least 10 distinct values (very likely with good RNG)
    EXPECT_GE(values.size(), 10u) << "RAND should produce varied values";
}

TEST_F(FnRandTest, RANDWithArgumentsReturnsError) {
    EvalResult result = eval("=RAND(1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnRandTest, RANDIsVolatile) {
    EXPECT_TRUE(FunctionRegistry::instance().isVolatile("RAND"));
}

// =============================================================================
// RANDBETWEEN Tests
// =============================================================================

TEST_F(FnRandTest, RANDBETWEENBasic) {
    // Test 1 to 10 range
    for (int i = 0; i < 100; ++i) {
        EvalResult result = eval("=RANDBETWEEN(1, 10)");
        ASSERT_TRUE(result.isNumber()) << "Iteration " << i;
        double value = result.getNumber();
        EXPECT_GE(value, 1.0);
        EXPECT_LE(value, 10.0);
        // Should be integer
        EXPECT_EQ(value, std::floor(value));
    }
}

TEST_F(FnRandTest, RANDBETWEENNegativeRange) {
    for (int i = 0; i < 50; ++i) {
        EvalResult result = eval("=RANDBETWEEN(-10, -1)");
        ASSERT_TRUE(result.isNumber());
        double value = result.getNumber();
        EXPECT_GE(value, -10.0);
        EXPECT_LE(value, -1.0);
        EXPECT_EQ(value, std::floor(value));
    }
}

TEST_F(FnRandTest, RANDBETWEENSpanningZero) {
    for (int i = 0; i < 50; ++i) {
        EvalResult result = eval("=RANDBETWEEN(-5, 5)");
        ASSERT_TRUE(result.isNumber());
        double value = result.getNumber();
        EXPECT_GE(value, -5.0);
        EXPECT_LE(value, 5.0);
        EXPECT_EQ(value, std::floor(value));
    }
}

TEST_F(FnRandTest, RANDBETWEENSameValues) {
    // When bottom = top, should always return that value
    EvalResult result = eval("=RANDBETWEEN(5, 5)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_EQ(result.getNumber(), 5.0);
}

TEST_F(FnRandTest, RANDBETWEENInvalidRange) {
    // bottom > top should return #NUM!
    EvalResult result = eval("=RANDBETWEEN(10, 1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FnRandTest, RANDBETWEENDecimalArguments) {
    // Decimals are truncated: RANDBETWEEN(1.5, 3.9) should be same as RANDBETWEEN(2, 3)
    for (int i = 0; i < 50; ++i) {
        EvalResult result = eval("=RANDBETWEEN(1.5, 3.9)");
        ASSERT_TRUE(result.isNumber());
        double value = result.getNumber();
        EXPECT_GE(value, 2.0);
        EXPECT_LE(value, 3.0);
        EXPECT_EQ(value, std::floor(value));
    }
}

TEST_F(FnRandTest, RANDBETWEENLargeRange) {
    // Test a large range
    for (int i = 0; i < 20; ++i) {
        EvalResult result = eval("=RANDBETWEEN(1, 1000000)");
        ASSERT_TRUE(result.isNumber());
        double value = result.getNumber();
        EXPECT_GE(value, 1.0);
        EXPECT_LE(value, 1000000.0);
        EXPECT_EQ(value, std::floor(value));
    }
}

TEST_F(FnRandTest, RANDBETWEENProducesVariation) {
    std::set<double> values;
    for (int i = 0; i < 100; ++i) {
        EvalResult result = eval("=RANDBETWEEN(1, 100)");
        ASSERT_TRUE(result.isNumber());
        values.insert(result.getNumber());
    }
    // With range 1-100 and 100 trials, should get at least 30 distinct values
    EXPECT_GE(values.size(), 30u);
}

TEST_F(FnRandTest, RANDBETWEENTooFewArguments) {
    EvalResult result = eval("=RANDBETWEEN(1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnRandTest, RANDBETWEENTooManyArguments) {
    EvalResult result = eval("=RANDBETWEEN(1, 10, 100)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnRandTest, RANDBETWEENErrorPropagation) {
    // Test with explicit error input (division by zero)
    EvalResult result = eval("=RANDBETWEEN(1/0, 10)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

TEST_F(FnRandTest, RANDBETWEENIsVolatile) {
    EXPECT_TRUE(FunctionRegistry::instance().isVolatile("RANDBETWEEN"));
}

TEST_F(FnRandTest, RANDBETWEENZeroRange) {
    // 0 to 0 should return 0
    EvalResult result = eval("=RANDBETWEEN(0, 0)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_EQ(result.getNumber(), 0.0);
}

TEST_F(FnRandTest, RANDBETWEENIncludesEndpoints) {
    // Test that both endpoints can be returned
    std::set<double> values;
    for (int i = 0; i < 200; ++i) {
        EvalResult result = eval("=RANDBETWEEN(1, 3)");
        ASSERT_TRUE(result.isNumber());
        values.insert(result.getNumber());
    }
    // Should have all three values: 1, 2, 3
    EXPECT_TRUE(values.count(1.0) > 0) << "Should include lower bound";
    EXPECT_TRUE(values.count(2.0) > 0) << "Should include middle value";
    EXPECT_TRUE(values.count(3.0) > 0) << "Should include upper bound";
}

}  // namespace
}  // namespace cells
