#include "core/cells/functions/fn_logic.h"

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

class FnLogicTest : public ::testing::Test {
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

TEST_F(FnLogicTest, IsErr) {
    EvalResult t = eval("=ISERR(1/0)");
    ASSERT_TRUE(t.isBoolean());
    EXPECT_TRUE(t.getBoolean());
    EvalResult f = eval("=ISERR(NA())");
    ASSERT_TRUE(f.isBoolean());
    EXPECT_FALSE(f.getBoolean());
    EvalResult n = eval("=ISERR(5)");
    ASSERT_TRUE(n.isBoolean());
    EXPECT_FALSE(n.getBoolean());
}

TEST_F(FnLogicTest, IsNonText) {
    EvalResult num = eval("=ISNONTEXT(1)");
    ASSERT_TRUE(num.isBoolean());
    EXPECT_TRUE(num.getBoolean());
    EvalResult txt = eval("=ISNONTEXT(\"hi\")");
    ASSERT_TRUE(txt.isBoolean());
    EXPECT_FALSE(txt.getBoolean());
    EvalResult err = eval("=ISNONTEXT(1/0)");
    ASSERT_TRUE(err.isBoolean());
    EXPECT_TRUE(err.getBoolean());
}

TEST_F(FnLogicTest, IsEvenOdd) {
    EvalResult e = eval("=ISEVEN(2.9)");
    ASSERT_TRUE(e.isBoolean());
    EXPECT_TRUE(e.getBoolean());
    EvalResult o = eval("=ISODD(3.1)");
    ASSERT_TRUE(o.isBoolean());
    EXPECT_TRUE(o.getBoolean());
    EvalResult neg = eval("=ISEVEN(-2.5)");
    ASSERT_TRUE(neg.isBoolean());
    EXPECT_TRUE(neg.getBoolean());
    EvalResult bad = eval("=ISEVEN(\"x\")");
    ASSERT_TRUE(bad.isError());
    EXPECT_EQ(bad.getError(), CellError::VALUE);
    EvalResult arity = eval("=ISEVEN()");
    ASSERT_TRUE(arity.isError());
    EXPECT_EQ(arity.getError(), CellError::VALUE);
}

TEST_F(FnLogicTest, TypeAndN) {
    EvalResult n = eval("=TYPE(42)");
    ASSERT_TRUE(n.isNumber());
    EXPECT_DOUBLE_EQ(n.getNumber(), 1.0);
    EvalResult t = eval("=TYPE(\"a\")");
    ASSERT_TRUE(t.isNumber());
    EXPECT_DOUBLE_EQ(t.getNumber(), 2.0);
    EvalResult b = eval("=TYPE(TRUE)");
    ASSERT_TRUE(b.isNumber());
    EXPECT_DOUBLE_EQ(b.getNumber(), 4.0);
    EvalResult e = eval("=TYPE(1/0)");
    ASSERT_TRUE(e.isNumber());
    EXPECT_DOUBLE_EQ(e.getNumber(), 16.0);

    EvalResult nNum = eval("=N(5)");
    ASSERT_TRUE(nNum.isNumber());
    EXPECT_DOUBLE_EQ(nNum.getNumber(), 5.0);
    EvalResult nTrue = eval("=N(TRUE)");
    ASSERT_TRUE(nTrue.isNumber());
    EXPECT_DOUBLE_EQ(nTrue.getNumber(), 1.0);
    EvalResult nText = eval("=N(\"x\")");
    ASSERT_TRUE(nText.isNumber());
    EXPECT_DOUBLE_EQ(nText.getNumber(), 0.0);
    EvalResult nErr = eval("=N(1/0)");
    ASSERT_TRUE(nErr.isError());
    EXPECT_EQ(nErr.getError(), CellError::DIV);
}

TEST_F(FnLogicTest, ErrorType) {
    EvalResult d = eval("=ERROR.TYPE(1/0)");
    ASSERT_TRUE(d.isNumber());
    EXPECT_DOUBLE_EQ(d.getNumber(), 2.0);
    EvalResult v = eval("=ERROR.TYPE(VALUE(\"x\"))");
    ASSERT_TRUE(v.isNumber());
    EXPECT_DOUBLE_EQ(v.getNumber(), 3.0);
    EvalResult na = eval("=ERROR.TYPE(NA())");
    ASSERT_TRUE(na.isNumber());
    EXPECT_DOUBLE_EQ(na.getNumber(), 7.0);
    EvalResult ok = eval("=ERROR.TYPE(1)");
    ASSERT_TRUE(ok.isError());
    EXPECT_EQ(ok.getError(), CellError::NA);
}

TEST_F(FnLogicTest, IsRef) {
    EvalResult r = eval("=ISREF(A1)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
    EvalResult rng = eval("=ISREF(A1:B2)");
    ASSERT_TRUE(rng.isBoolean());
    EXPECT_TRUE(rng.getBoolean());
    EvalResult lit = eval("=ISREF(1)");
    ASSERT_TRUE(lit.isBoolean());
    EXPECT_FALSE(lit.getBoolean());
    EvalResult txt = eval("=ISREF(\"A1\")");
    ASSERT_TRUE(txt.isBoolean());
    EXPECT_FALSE(txt.getBoolean());
}

TEST_F(FnLogicTest, IsBetween) {
    EvalResult yes = eval("=ISBETWEEN(5,1,10)");
    ASSERT_TRUE(yes.isBoolean());
    EXPECT_TRUE(yes.getBoolean());
    EvalResult lo = eval("=ISBETWEEN(1,1,10)");
    ASSERT_TRUE(lo.isBoolean());
    EXPECT_TRUE(lo.getBoolean());
    EvalResult exclusive = eval("=ISBETWEEN(1,1,10,FALSE,TRUE)");
    ASSERT_TRUE(exclusive.isBoolean());
    EXPECT_FALSE(exclusive.getBoolean());
    EvalResult hiEx = eval("=ISBETWEEN(10,1,10,TRUE,FALSE)");
    ASSERT_TRUE(hiEx.isBoolean());
    EXPECT_FALSE(hiEx.getBoolean());
    EvalResult outside = eval("=ISBETWEEN(11,1,10)");
    ASSERT_TRUE(outside.isBoolean());
    EXPECT_FALSE(outside.getBoolean());
    EXPECT_EQ(eval("=ISBETWEEN(1,2)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=ISBETWEEN(\"x\",1,2)").getError(), CellError::VALUE);
}

}  // namespace
}  // namespace cells
