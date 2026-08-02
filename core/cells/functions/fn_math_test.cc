#include "core/cells/functions/fn_math.h"

// MSVC only exposes M_PI / M_PI_2 / M_PI_4 when this is set before <cmath>.
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
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

class FnMathTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "Test");
        workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));
        sheet = workbook->getSheetByIndex(0);

        for (uint32_t i = 0; i < 26; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = i;
            col->name = Sheet::positionToColumnName(i);
            colIds[i] = col->id;
            sheet->addColumn(std::move(col));
        }

        for (uint32_t i = 0; i < 100; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = i;
            rowIds[i] = row->id;
            sheet->addRow(std::move(row));
        }
    }

    EvalResult eval(const std::string& formula) {
        FormulaParser parser(formula);
        auto ast = parser.parse();
        if (!ast || parser.hasErrors()) {
            return EvalResult::Error(CellError::VALUE);
        }

        FormulaResolver resolver(*workbook, *sheet);
        createRequiredEntities(resolver, ast.get());
        resolver.resolve(ast.get());

        std::unordered_set<ID> evaluating;
        EvalContext ctx;
        ctx.sheet = sheet;
        ctx.workbook = workbook.get();
        ctx.evaluatingCells = &evaluating;
        ctx.recursionDepth = 0;

        return evaluate(ast.get(), ctx);
    }

    void createRequiredEntities(FormulaResolver& resolver, ASTNode* ast) {
        RequiredEntities required = resolver.getRequiredEntities(ast);
        for (const auto& pendingCell : required.cells) {
            auto findColPos = [&required, this](const ID& colId) -> uint32_t {
                for (const auto& c : required.columns) {
                    if (c.id == colId) {
                        return c.position;
                    }
                }
                const Axis* axis = sheet->getColumn(colId);
                return axis ? axis->position : 0;
            };
            auto findRowPos = [&required, this](const ID& rowId) -> uint32_t {
                for (const auto& r : required.rows) {
                    if (r.id == rowId) {
                        return r.position;
                    }
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

    Cell* setCellValue(uint32_t col, uint32_t row, double value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    Cell* setCellError(uint32_t col, uint32_t row, CellError error) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(error);
        return cell;
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26];
    ID rowIds[100];
};

// -----------------------------------------------------------------------------
// PRODUCT / SUMSQ
// -----------------------------------------------------------------------------

TEST_F(FnMathTest, ProductMultiArg) {
    EvalResult r = eval("=PRODUCT(2,3,4)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 24.0);
}

TEST_F(FnMathTest, ProductRange) {
    setCellValue(0, 0, 2.0);
    setCellValue(0, 1, 3.0);
    setCellValue(0, 2, 5.0);
    EvalResult r = eval("=PRODUCT(A1:A3)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 30.0);
}

TEST_F(FnMathTest, ProductEmptyReturnsZero) {
    EvalResult r = eval("=PRODUCT()");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 0.0);
}

TEST_F(FnMathTest, ProductPropagatesError) {
    setCellError(0, 0, CellError::DIV);
    EvalResult r = eval("=PRODUCT(A1,2)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::DIV);
}

TEST_F(FnMathTest, SumsqBasic) {
    EvalResult r = eval("=SUMSQ(3,4)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 25.0);
}

TEST_F(FnMathTest, SumsqRange) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    EvalResult r = eval("=SUMSQ(A1:A3)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 14.0);
}

TEST_F(FnMathTest, SumsqEmptyReturnsZero) {
    EvalResult r = eval("=SUMSQ()");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 0.0);
}

// -----------------------------------------------------------------------------
// EVEN / ODD / MROUND / SQRTPI
// -----------------------------------------------------------------------------

TEST_F(FnMathTest, EvenPositive) {
    EXPECT_DOUBLE_EQ(eval("=EVEN(1.5)").getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(eval("=EVEN(3)").getNumber(), 4.0);
    EXPECT_DOUBLE_EQ(eval("=EVEN(2)").getNumber(), 2.0);
}

TEST_F(FnMathTest, EvenNegative) {
    EXPECT_DOUBLE_EQ(eval("=EVEN(-1)").getNumber(), -2.0);
    EXPECT_DOUBLE_EQ(eval("=EVEN(-1.5)").getNumber(), -2.0);
}

TEST_F(FnMathTest, OddPositive) {
    EXPECT_DOUBLE_EQ(eval("=ODD(1.5)").getNumber(), 3.0);
    EXPECT_DOUBLE_EQ(eval("=ODD(2)").getNumber(), 3.0);
    EXPECT_DOUBLE_EQ(eval("=ODD(1)").getNumber(), 1.0);
}

TEST_F(FnMathTest, OddNegative) {
    EXPECT_DOUBLE_EQ(eval("=ODD(-1)").getNumber(), -1.0);
    EXPECT_DOUBLE_EQ(eval("=ODD(-2)").getNumber(), -3.0);
}

TEST_F(FnMathTest, MroundBasic) {
    EXPECT_DOUBLE_EQ(eval("=MROUND(10,3)").getNumber(), 9.0);
    EXPECT_DOUBLE_EQ(eval("=MROUND(11,3)").getNumber(), 12.0);
    EXPECT_DOUBLE_EQ(eval("=MROUND(10,0)").getNumber(), 0.0);
}

TEST_F(FnMathTest, MroundDifferentSignsNumError) {
    EvalResult r = eval("=MROUND(10,-3)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::NUM);
}

TEST_F(FnMathTest, MroundNegativeBoth) {
    EXPECT_DOUBLE_EQ(eval("=MROUND(-10,-3)").getNumber(), -9.0);
}

TEST_F(FnMathTest, SqrtpiBasic) {
    EvalResult r = eval("=SQRTPI(1)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_NEAR(r.getNumber(), std::sqrt(M_PI), 1e-12);
}

TEST_F(FnMathTest, SqrtpiNegativeNumError) {
    EvalResult r = eval("=SQRTPI(-1)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::NUM);
}

// -----------------------------------------------------------------------------
// CSCH / SECH / COTH / ACOT / ACOTH
// -----------------------------------------------------------------------------

TEST_F(FnMathTest, CschBasic) {
    // CSCH(1) = 1/sinh(1)
    EvalResult r = eval("=CSCH(1)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_NEAR(r.getNumber(), 1.0 / std::sinh(1.0), 1e-12);
}

TEST_F(FnMathTest, CschZeroDivError) {
    EvalResult r = eval("=CSCH(0)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::DIV);
}

TEST_F(FnMathTest, SechBasic) {
    EvalResult r = eval("=SECH(0)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 1.0);
}

TEST_F(FnMathTest, CothBasic) {
    EvalResult r = eval("=COTH(1)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_NEAR(r.getNumber(), std::cosh(1.0) / std::sinh(1.0), 1e-12);
}

TEST_F(FnMathTest, CothZeroDivError) {
    EvalResult r = eval("=COTH(0)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::DIV);
}

TEST_F(FnMathTest, AcotZero) {
    EvalResult r = eval("=ACOT(0)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_NEAR(r.getNumber(), M_PI_2, 1e-12);
}

TEST_F(FnMathTest, AcotPositive) {
    EvalResult r = eval("=ACOT(1)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_NEAR(r.getNumber(), M_PI_4, 1e-12);
}

TEST_F(FnMathTest, AcothBasic) {
    EvalResult r = eval("=ACOTH(2)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_NEAR(r.getNumber(), std::atanh(0.5), 1e-12);
}

TEST_F(FnMathTest, AcothDomainError) {
    EvalResult r = eval("=ACOTH(0.5)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::NUM);
}

// -----------------------------------------------------------------------------
// GCD / LCM / FACTDOUBLE
// -----------------------------------------------------------------------------

TEST_F(FnMathTest, GcdBasic) {
    EXPECT_DOUBLE_EQ(eval("=GCD(24,36)").getNumber(), 12.0);
    EXPECT_DOUBLE_EQ(eval("=GCD(24,36,60)").getNumber(), 12.0);
}

TEST_F(FnMathTest, GcdTruncatesNonIntegers) {
    // Excel truncates toward zero for non-integers (non-negative only).
    EXPECT_DOUBLE_EQ(eval("=GCD(24.9,36.1)").getNumber(), 12.0);
}

TEST_F(FnMathTest, GcdNegativeNumError) {
    // Excel: any argument < 0 → #NUM!
    EvalResult r = eval("=GCD(-24,36)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::NUM);
}

TEST_F(FnMathTest, GcdZero) {
    EXPECT_DOUBLE_EQ(eval("=GCD(0,0)").getNumber(), 0.0);
    EXPECT_DOUBLE_EQ(eval("=GCD(0,5)").getNumber(), 5.0);
}

TEST_F(FnMathTest, LcmBasic) {
    EXPECT_DOUBLE_EQ(eval("=LCM(4,6)").getNumber(), 12.0);
    EXPECT_DOUBLE_EQ(eval("=LCM(4,6,8)").getNumber(), 24.0);
}

TEST_F(FnMathTest, LcmTruncatesNonIntegers) {
    EXPECT_DOUBLE_EQ(eval("=LCM(4.9,6.1)").getNumber(), 12.0);
}

TEST_F(FnMathTest, LcmNegativeNumError) {
    EvalResult r = eval("=LCM(-4,6)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::NUM);
}

TEST_F(FnMathTest, LcmWithZero) {
    EXPECT_DOUBLE_EQ(eval("=LCM(0,5)").getNumber(), 0.0);
}

TEST_F(FnMathTest, FactDoubleOdd) {
    // 5!! = 5*3*1 = 15
    EXPECT_DOUBLE_EQ(eval("=FACTDOUBLE(5)").getNumber(), 15.0);
}

TEST_F(FnMathTest, FactDoubleEven) {
    // 6!! = 6*4*2 = 48
    EXPECT_DOUBLE_EQ(eval("=FACTDOUBLE(6)").getNumber(), 48.0);
}

TEST_F(FnMathTest, FactDoubleZero) {
    EXPECT_DOUBLE_EQ(eval("=FACTDOUBLE(0)").getNumber(), 1.0);
}

TEST_F(FnMathTest, FactDoubleNegativeNumError) {
    EvalResult r = eval("=FACTDOUBLE(-1)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::NUM);
}

// -----------------------------------------------------------------------------
// FLOOR / CEILING significance + PRECISE / ISO
// -----------------------------------------------------------------------------

TEST_F(FnMathTest, FloorOneArgUnchanged) {
    EXPECT_DOUBLE_EQ(eval("=FLOOR(2.9)").getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(eval("=FLOOR(-2.9)").getNumber(), -3.0);
}

TEST_F(FnMathTest, FloorWithSignificance) {
    EXPECT_DOUBLE_EQ(eval("=FLOOR(3.7,2)").getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(eval("=FLOOR(-3.7,-2)").getNumber(), -2.0);
}

TEST_F(FnMathTest, FloorMixedSignsNumError) {
    EvalResult r = eval("=FLOOR(-3.7,2)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::NUM);
}

TEST_F(FnMathTest, FloorZeroSignificanceDivError) {
    EvalResult r = eval("=FLOOR(3,0)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::DIV);
}

TEST_F(FnMathTest, CeilingOneArgUnchanged) {
    EXPECT_DOUBLE_EQ(eval("=CEILING(2.1)").getNumber(), 3.0);
    EXPECT_DOUBLE_EQ(eval("=CEILING(-2.1)").getNumber(), -2.0);
}

TEST_F(FnMathTest, CeilingWithSignificance) {
    EXPECT_DOUBLE_EQ(eval("=CEILING(2.5,2)").getNumber(), 4.0);
    EXPECT_DOUBLE_EQ(eval("=CEILING(-2.5,-2)").getNumber(), -4.0);
}

TEST_F(FnMathTest, CeilingMixedSignsNumError) {
    EvalResult r = eval("=CEILING(-2.5,2)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::NUM);
}

TEST_F(FnMathTest, FloorPreciseBasic) {
    EXPECT_DOUBLE_EQ(eval("=FLOOR_PRECISE(3.2)").getNumber(), 3.0);
    EXPECT_DOUBLE_EQ(eval("=FLOOR_PRECISE(-3.2)").getNumber(), -4.0);
    EXPECT_DOUBLE_EQ(eval("=FLOOR_PRECISE(3.2,2)").getNumber(), 2.0);
    // Uses abs(significance)
    EXPECT_DOUBLE_EQ(eval("=FLOOR_PRECISE(3.2,-2)").getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(eval("=FLOOR_PRECISE(-3.2,2)").getNumber(), -4.0);
}

TEST_F(FnMathTest, CeilingPreciseBasic) {
    EXPECT_DOUBLE_EQ(eval("=CEILING_PRECISE(3.2)").getNumber(), 4.0);
    EXPECT_DOUBLE_EQ(eval("=CEILING_PRECISE(-3.2)").getNumber(), -3.0);
    EXPECT_DOUBLE_EQ(eval("=CEILING_PRECISE(3.2,2)").getNumber(), 4.0);
    EXPECT_DOUBLE_EQ(eval("=CEILING_PRECISE(-3.2,2)").getNumber(), -2.0);
}

TEST_F(FnMathTest, IsoCeilingMatchesCeilingPrecise) {
    EXPECT_DOUBLE_EQ(eval("=ISO_CEILING(3.2,2)").getNumber(), 4.0);
    EXPECT_DOUBLE_EQ(eval("=ISO_CEILING(-3.2,2)").getNumber(), -2.0);
}

// -----------------------------------------------------------------------------
// Registry: no #NAME? for any new function
// -----------------------------------------------------------------------------

TEST_F(FnMathTest, AllNewFunctionsRegistered) {
    const char* names[] = {
        "PRODUCT",         "SUMSQ",       "EVEN", "ODD",        "MROUND",
        "SQRTPI",          "CSCH",        "SECH", "COTH",       "ACOT",
        "ACOTH",           "GCD",         "LCM",  "FACTDOUBLE", "FLOOR_PRECISE",
        "CEILING_PRECISE", "ISO_CEILING",
    };
    FunctionRegistry& reg = FunctionRegistry::instance();
    for (const char* name : names) {
        EXPECT_TRUE(reg.exists(name)) << name << " should be registered";
    }
}

TEST_F(FnMathTest, SmokeNoNameErrorForAll) {
    // Each formula must evaluate without #NAME?
    struct Case {
        const char* formula;
        bool expectNumber;
    };
    const Case cases[] = {
        {"=PRODUCT(1,2)", true},
        {"=SUMSQ(1,2)", true},
        {"=EVEN(1)", true},
        {"=ODD(2)", true},
        {"=MROUND(5,2)", true},
        {"=SQRTPI(1)", true},
        {"=CSCH(1)", true},
        {"=SECH(0)", true},
        {"=COTH(1)", true},
        {"=ACOT(0)", true},
        {"=ACOTH(2)", true},
        {"=GCD(8,12)", true},
        {"=LCM(4,6)", true},
        {"=FACTDOUBLE(5)", true},
        {"=FLOOR(5,2)", true},
        {"=CEILING(5,2)", true},
        {"=FLOOR_PRECISE(5.1,2)", true},
        {"=CEILING_PRECISE(5.1,2)", true},
        {"=ISO_CEILING(5.1,2)", true},
    };
    for (const Case& c : cases) {
        EvalResult r = eval(c.formula);
        EXPECT_FALSE(r.isError() && r.getError() == CellError::NAME)
            << c.formula << " returned #NAME?";
        if (c.expectNumber) {
            EXPECT_TRUE(r.isNumber()) << c.formula << " expected number, got error "
                                      << (r.isError() ? static_cast<int>(r.getError()) : -1);
        }
    }
}

}  // namespace
}  // namespace cells
