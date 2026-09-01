#include "core/cells/functions/fn_text.h"

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

class FnTextExtraTest : public ::testing::Test {
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

TEST_F(FnTextExtraTest, UnicharUnicode) {
    EvalResult a = eval("=UNICHAR(65)");
    ASSERT_TRUE(a.isString());
    EXPECT_EQ(a.getString(), "A");
    EvalResult u = eval("=UNICODE(\"A\")");
    ASSERT_TRUE(u.isNumber());
    EXPECT_DOUBLE_EQ(u.getNumber(), 65.0);
    EvalResult round = eval("=UNICODE(UNICHAR(8364))");
    ASSERT_TRUE(round.isNumber());
    EXPECT_DOUBLE_EQ(round.getNumber(), 8364.0);
    EvalResult bad = eval("=UNICHAR(0)");
    ASSERT_TRUE(bad.isError());
    EXPECT_EQ(bad.getError(), CellError::VALUE);
    EvalResult empty = eval("=UNICODE(\"\")");
    ASSERT_TRUE(empty.isError());
    EXPECT_EQ(empty.getError(), CellError::VALUE);
}

TEST_F(FnTextExtraTest, DollarFixedNumbervalue) {
    EvalResult d = eval("=DOLLAR(1234.567,2)");
    ASSERT_TRUE(d.isString());
    EXPECT_EQ(d.getString(), "$1,234.57");
    EvalResult dn = eval("=DOLLAR(-1.5,1)");
    ASSERT_TRUE(dn.isString());
    EXPECT_EQ(dn.getString(), "($1.5)");
    EvalResult f = eval("=FIXED(1234.567,2)");
    ASSERT_TRUE(f.isString());
    EXPECT_EQ(f.getString(), "1,234.57");
    EvalResult fn = eval("=FIXED(1234.567,2,TRUE)");
    ASSERT_TRUE(fn.isString());
    EXPECT_EQ(fn.getString(), "1234.57");
    EvalResult nv = eval("=NUMBERVALUE(\"1,234.56\")");
    ASSERT_TRUE(nv.isNumber());
    EXPECT_DOUBLE_EQ(nv.getNumber(), 1234.56);
    EvalResult nve = eval("=NUMBERVALUE(\"1.234,56\",\",\",\".\")");
    ASSERT_TRUE(nve.isNumber());
    EXPECT_DOUBLE_EQ(nve.getNumber(), 1234.56);
    EvalResult pct = eval("=NUMBERVALUE(\"50%\")");
    ASSERT_TRUE(pct.isNumber());
    EXPECT_DOUBLE_EQ(pct.getNumber(), 0.5);
}

}  // namespace
}  // namespace cells
