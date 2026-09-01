#include "core/cells/functions/fn_datetime.h"

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

class FnDateTimeExtraTest : public ::testing::Test {
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

TEST_F(FnDateTimeExtraTest, DaysAndEdate) {
    EvalResult days = eval("=DAYS(DATE(2024,1,11),DATE(2024,1,1))");
    ASSERT_TRUE(days.isNumber());
    EXPECT_DOUBLE_EQ(days.getNumber(), 10.0);

    EvalResult ed = eval("=EDATE(DATE(2024,1,31),1)");
    ASSERT_TRUE(ed.isNumber());
    EvalResult eom = eval("=DATE(2024,2,29)");
    ASSERT_TRUE(eom.isNumber());
    EXPECT_DOUBLE_EQ(ed.getNumber(), eom.getNumber());
}

TEST_F(FnDateTimeExtraTest, DateDif) {
    EvalResult y = eval("=DATEDIF(DATE(2020,6,15),DATE(2024,6,15),\"Y\")");
    ASSERT_TRUE(y.isNumber());
    EXPECT_DOUBLE_EQ(y.getNumber(), 4.0);
    EvalResult m = eval("=DATEDIF(DATE(2024,1,15),DATE(2024,4,15),\"M\")");
    ASSERT_TRUE(m.isNumber());
    EXPECT_DOUBLE_EQ(m.getNumber(), 3.0);
    EvalResult d = eval("=DATEDIF(DATE(2024,1,1),DATE(2024,1,11),\"D\")");
    ASSERT_TRUE(d.isNumber());
    EXPECT_DOUBLE_EQ(d.getNumber(), 10.0);
}

TEST_F(FnDateTimeExtraTest, Weeknum) {
    // 2024-01-01 is Monday
    EvalResult sun = eval("=WEEKNUM(DATE(2024,1,1),1)");
    ASSERT_TRUE(sun.isNumber());
    EXPECT_DOUBLE_EQ(sun.getNumber(), 1.0);
    EvalResult iso = eval("=WEEKNUM(DATE(2024,1,1),21)");
    ASSERT_TRUE(iso.isNumber());
    EXPECT_DOUBLE_EQ(iso.getNumber(), 1.0);
}

TEST_F(FnDateTimeExtraTest, NetworkdaysAndWorkday) {
    // 2024-01-01 Monday through 2024-01-05 Friday = 5 workdays
    EvalResult n = eval("=NETWORKDAYS(DATE(2024,1,1),DATE(2024,1,5))");
    ASSERT_TRUE(n.isNumber());
    EXPECT_DOUBLE_EQ(n.getNumber(), 5.0);
    // Weekend span
    EvalResult w = eval("=NETWORKDAYS(DATE(2024,1,5),DATE(2024,1,8))");
    ASSERT_TRUE(w.isNumber());
    EXPECT_DOUBLE_EQ(w.getNumber(), 2.0);  // Fri + Mon

    EvalResult next = eval("=WORKDAY(DATE(2024,1,5),1)");
    EvalResult monday = eval("=DATE(2024,1,8)");
    ASSERT_TRUE(next.isNumber());
    ASSERT_TRUE(monday.isNumber());
    EXPECT_DOUBLE_EQ(next.getNumber(), monday.getNumber());
}

TEST_F(FnDateTimeExtraTest, TextJoinAndCleanViaRegistry) {
    EXPECT_TRUE(FunctionRegistry::instance().exists("NETWORKDAYS"));
    EXPECT_TRUE(FunctionRegistry::instance().exists("WORKDAY"));
    EXPECT_TRUE(FunctionRegistry::instance().exists("EDATE"));
}

}  // namespace
}  // namespace cells
