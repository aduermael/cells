#include "core/cells/functions/fn_engineering.h"

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

class FnEngineeringTest : public ::testing::Test {
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
        for (uint32_t i = 0; i < 20; i++) {
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
        resolver.resolve(ast.get());
        std::unordered_set<ID> evaluating;
        EvalContext ctx;
        ctx.sheet = sheet;
        ctx.workbook = workbook.get();
        ctx.evaluatingCells = &evaluating;
        return evaluate(ast.get(), ctx);
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26];
    ID rowIds[20];
};

TEST_F(FnEngineeringTest, Bitwise) {
    EvalResult a = eval("=BITAND(1,5)");
    ASSERT_TRUE(a.isNumber());
    EXPECT_DOUBLE_EQ(a.getNumber(), 1.0);
    EvalResult o = eval("=BITOR(1,5)");
    ASSERT_TRUE(o.isNumber());
    EXPECT_DOUBLE_EQ(o.getNumber(), 5.0);
    EvalResult x = eval("=BITXOR(1,5)");
    ASSERT_TRUE(x.isNumber());
    EXPECT_DOUBLE_EQ(x.getNumber(), 4.0);
    EvalResult l = eval("=BITLSHIFT(2,2)");
    ASSERT_TRUE(l.isNumber());
    EXPECT_DOUBLE_EQ(l.getNumber(), 8.0);
    EvalResult r = eval("=BITRSHIFT(8,2)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 2.0);
    EvalResult neg = eval("=BITAND(-1,1)");
    ASSERT_TRUE(neg.isError());
    EXPECT_EQ(neg.getError(), CellError::NUM);
    EvalResult arity = eval("=BITAND(1)");
    ASSERT_TRUE(arity.isError());
    EXPECT_EQ(arity.getError(), CellError::VALUE);
}

TEST_F(FnEngineeringTest, BinDecHexOct) {
    EvalResult d = eval("=BIN2DEC(\"1010\")");
    ASSERT_TRUE(d.isNumber());
    EXPECT_DOUBLE_EQ(d.getNumber(), 10.0);
    EvalResult neg = eval("=BIN2DEC(\"1111111111\")");
    ASSERT_TRUE(neg.isNumber());
    EXPECT_DOUBLE_EQ(neg.getNumber(), -1.0);
    EvalResult b = eval("=DEC2BIN(10)");
    ASSERT_TRUE(b.isString());
    EXPECT_EQ(b.getString(), "1010");
    EvalResult pad = eval("=DEC2BIN(10,8)");
    ASSERT_TRUE(pad.isString());
    EXPECT_EQ(pad.getString(), "00001010");
    EvalResult hx = eval("=DEC2HEX(255)");
    ASSERT_TRUE(hx.isString());
    EXPECT_EQ(hx.getString(), "FF");
    EvalResult hd = eval("=HEX2DEC(\"FF\")");
    ASSERT_TRUE(hd.isNumber());
    EXPECT_DOUBLE_EQ(hd.getNumber(), 255.0);
    EvalResult oct = eval("=DEC2OCT(8)");
    ASSERT_TRUE(oct.isString());
    EXPECT_EQ(oct.getString(), "10");
    EvalResult od = eval("=OCT2DEC(\"10\")");
    ASSERT_TRUE(od.isNumber());
    EXPECT_DOUBLE_EQ(od.getNumber(), 8.0);
    EvalResult bh = eval("=BIN2HEX(\"1010\")");
    ASSERT_TRUE(bh.isString());
    EXPECT_EQ(bh.getString(), "A");
    EvalResult bad = eval("=BIN2DEC(\"2\")");
    ASSERT_TRUE(bad.isError());
    EXPECT_EQ(bad.getError(), CellError::NUM);
    EvalResult overflow = eval("=DEC2BIN(512)");
    ASSERT_TRUE(overflow.isError());
    EXPECT_EQ(overflow.getError(), CellError::NUM);
}

TEST_F(FnEngineeringTest, DeltaGeStepErfComplex) {
    EvalResult d = eval("=DELTA(5,5)");
    ASSERT_TRUE(d.isNumber());
    EXPECT_DOUBLE_EQ(d.getNumber(), 1.0);
    EvalResult d0 = eval("=DELTA(5)");
    ASSERT_TRUE(d0.isNumber());
    EXPECT_DOUBLE_EQ(d0.getNumber(), 0.0);
    EvalResult g = eval("=GESTEP(5,4)");
    ASSERT_TRUE(g.isNumber());
    EXPECT_DOUBLE_EQ(g.getNumber(), 1.0);
    EvalResult g0 = eval("=GESTEP(4,5)");
    ASSERT_TRUE(g0.isNumber());
    EXPECT_DOUBLE_EQ(g0.getNumber(), 0.0);
    EvalResult erf = eval("=ERF(1)");
    ASSERT_TRUE(erf.isNumber());
    EXPECT_NEAR(erf.getNumber(), 0.8427007929, 1e-9);
    EvalResult erfc = eval("=ERFC(1)");
    ASSERT_TRUE(erfc.isNumber());
    EXPECT_NEAR(erfc.getNumber(), 0.15729920705, 1e-9);
    EvalResult prec = eval("=ERF.PRECISE(1)");
    ASSERT_TRUE(prec.isNumber());
    EXPECT_NEAR(prec.getNumber(), erf.getNumber(), 1e-12);
    EvalResult c = eval("=COMPLEX(3,4)");
    ASSERT_TRUE(c.isString());
    EXPECT_EQ(c.getString(), "3+4i");
    EvalResult cj = eval("=COMPLEX(3,-4,\"j\")");
    ASSERT_TRUE(cj.isString());
    EXPECT_EQ(cj.getString(), "3-4j");
}

TEST_F(FnEngineeringTest, ImArithmetic) {
    EvalResult absv = eval("=IMABS(\"3+4i\")");
    ASSERT_TRUE(absv.isNumber());
    EXPECT_DOUBLE_EQ(absv.getNumber(), 5.0);
    EvalResult re = eval("=IMREAL(\"3+4i\")");
    ASSERT_TRUE(re.isNumber());
    EXPECT_DOUBLE_EQ(re.getNumber(), 3.0);
    EvalResult im = eval("=IMAGINARY(\"3+4i\")");
    ASSERT_TRUE(im.isNumber());
    EXPECT_DOUBLE_EQ(im.getNumber(), 4.0);
    EvalResult arg = eval("=IMARGUMENT(\"0+1i\")");
    ASSERT_TRUE(arg.isNumber());
    EXPECT_NEAR(arg.getNumber(), 1.5707963267948966, 1e-12);
    EvalResult conj = eval("=IMCONJUGATE(\"3+4i\")");
    ASSERT_TRUE(conj.isString());
    EXPECT_EQ(conj.getString(), "3-4i");
    EvalResult sum = eval("=IMSUM(\"3+4i\",\"1+2i\")");
    ASSERT_TRUE(sum.isString());
    EXPECT_EQ(sum.getString(), "4+6i");
    EvalResult sub = eval("=IMSUB(\"3+4i\",\"1+2i\")");
    ASSERT_TRUE(sub.isString());
    EXPECT_EQ(sub.getString(), "2+2i");
    EvalResult prod = eval("=IMPRODUCT(\"3+4i\",\"1+2i\")");
    ASSERT_TRUE(prod.isString());
    EXPECT_EQ(prod.getString(), "-5+10i");
    EvalResult div = eval("=IMDIV(\"1+i\",\"1+i\")");
    ASSERT_TRUE(div.isString());
    EXPECT_EQ(div.getString(), "1");
    EvalResult powv = eval("=IMPOWER(\"2+2i\",2)");
    ASSERT_TRUE(powv.isString());
    EXPECT_EQ(powv.getString(), "8i");
    EvalResult fromComplex = eval("=IMSUM(COMPLEX(3,4),COMPLEX(1,2))");
    ASSERT_TRUE(fromComplex.isString());
    EXPECT_EQ(fromComplex.getString(), "4+6i");
    EXPECT_EQ(eval("=IMABS()").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=IMARGUMENT(0)").getError(), CellError::DIV);
    EXPECT_EQ(eval("=IMDIV(\"1+i\",0)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=IMPOWER(0,-1)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=IMSUB(\"1+i\")").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=IMSUM()").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=IMABS(\"nope\")").getError(), CellError::NUM);
}

}  // namespace
}  // namespace cells
