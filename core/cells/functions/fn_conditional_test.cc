#include "core/cells/functions/fn_conditional.h"

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

class FnConditionalTest : public ::testing::Test {
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

    Cell* setNum(uint32_t col, uint32_t row, double value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    Cell* setText(uint32_t col, uint32_t row, const std::string& value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26];
    ID rowIds[100];
};

TEST_F(FnConditionalTest, SumIfGreaterThan) {
    setNum(0, 0, 1);
    setNum(0, 1, 2);
    setNum(0, 2, 3);
    setNum(1, 0, 10);
    setNum(1, 1, 20);
    setNum(1, 2, 30);
    EvalResult r = eval("=SUMIF(A1:A3,\">1\",B1:B3)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 50.0);
}

TEST_F(FnConditionalTest, SumIfSameRange) {
    setNum(0, 0, 1);
    setNum(0, 1, 2);
    setNum(0, 2, 3);
    EvalResult r = eval("=SUMIF(A1:A3,\">1\")");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 5.0);
}

TEST_F(FnConditionalTest, SumIfExactNumber) {
    setNum(0, 0, 2);
    setNum(0, 1, 2);
    setNum(0, 2, 3);
    setNum(1, 0, 10);
    setNum(1, 1, 20);
    setNum(1, 2, 30);
    EvalResult r = eval("=SUMIF(A1:A3,2,B1:B3)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 30.0);
}

TEST_F(FnConditionalTest, SumIfTextAndWildcard) {
    setText(0, 0, "apple");
    setText(0, 1, "apricot");
    setText(0, 2, "banana");
    setNum(1, 0, 1);
    setNum(1, 1, 2);
    setNum(1, 2, 4);
    EvalResult r = eval("=SUMIF(A1:A3,\"ap*\",B1:B3)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 3.0);
}

TEST_F(FnConditionalTest, CountIfNotEqual) {
    setNum(0, 0, 1);
    setNum(0, 1, 2);
    setNum(0, 2, 2);
    EvalResult r = eval("=COUNTIF(A1:A3,\"<>2\")");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 1.0);
}

TEST_F(FnConditionalTest, CountIfsAnd) {
    setText(0, 0, "east");
    setText(0, 1, "west");
    setText(0, 2, "east");
    setNum(1, 0, 10);
    setNum(1, 1, 20);
    setNum(1, 2, 5);
    EvalResult r = eval("=COUNTIFS(A1:A3,\"east\",B1:B3,\">6\")");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 1.0);
}

TEST_F(FnConditionalTest, SumIfs) {
    setText(0, 0, "east");
    setText(0, 1, "east");
    setText(0, 2, "west");
    setNum(1, 0, 10);
    setNum(1, 1, 20);
    setNum(1, 2, 30);
    EvalResult r = eval("=SUMIFS(B1:B3,A1:A3,\"east\")");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 30.0);
}

TEST_F(FnConditionalTest, AverageIf) {
    setNum(0, 0, 10);
    setNum(0, 1, 20);
    setNum(0, 2, 30);
    EvalResult r = eval("=AVERAGEIF(A1:A3,\">15\")");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 25.0);
}

TEST_F(FnConditionalTest, AverageIfsNoMatch) {
    setNum(0, 0, 1);
    EvalResult r = eval("=AVERAGEIFS(A1:A1,A1:A1,\">9\")");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::DIV);
}

TEST_F(FnConditionalTest, MinIfsMaxIfs) {
    setText(0, 0, "a");
    setText(0, 1, "a");
    setText(0, 2, "b");
    setNum(1, 0, 5);
    setNum(1, 1, 2);
    setNum(1, 2, 9);
    EvalResult mn = eval("=MINIFS(B1:B3,A1:A3,\"a\")");
    ASSERT_TRUE(mn.isNumber());
    EXPECT_DOUBLE_EQ(mn.getNumber(), 2.0);
    EvalResult mx = eval("=MAXIFS(B1:B3,A1:A3,\"a\")");
    ASSERT_TRUE(mx.isNumber());
    EXPECT_DOUBLE_EQ(mx.getNumber(), 5.0);
}

TEST_F(FnConditionalTest, SumProduct) {
    setNum(0, 0, 2);
    setNum(0, 1, 3);
    setNum(1, 0, 4);
    setNum(1, 1, 5);
    EvalResult r = eval("=SUMPRODUCT(A1:A2,B1:B2)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(r.getNumber(), 23.0);  // 2*4 + 3*5
}

TEST_F(FnConditionalTest, UnknownArityIsValueNotName) {
    EvalResult r = eval("=SUMIF()");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.getError(), CellError::VALUE);
    EXPECT_TRUE(FunctionRegistry::instance().exists("SUMIF"));
    EXPECT_TRUE(FunctionRegistry::instance().exists("COUNTIFS"));
    EXPECT_TRUE(FunctionRegistry::instance().exists("SUMPRODUCT"));
}

TEST_F(FnConditionalTest, DatabaseFunctionsFieldNameIndexAndCriteria) {
    setText(0, 0, "Tree");
    setText(1, 0, "Height");
    setText(2, 0, "Age");
    setText(0, 1, "Apple");
    setNum(1, 1, 18);
    setNum(2, 1, 20);
    setText(0, 2, "Pear");
    setNum(1, 2, 12);
    setNum(2, 2, 12);
    setText(0, 3, "Cherry");
    setNum(1, 3, 13);
    setNum(2, 3, 14);
    setText(0, 4, "Apple");
    setNum(1, 4, 14);
    setNum(2, 4, 15);
    setText(0, 5, "Pear");
    setNum(1, 5, 9);
    setNum(2, 5, 8);

    setText(0, 7, "Tree");
    setText(0, 8, "Apple");

    EvalResult sumName = eval("=DSUM(A1:C6,\"Height\",A8:A9)");
    ASSERT_TRUE(sumName.isNumber());
    EXPECT_DOUBLE_EQ(sumName.getNumber(), 32.0);
    EvalResult sumIdx = eval("=DSUM(A1:C6,2,A8:A9)");
    ASSERT_TRUE(sumIdx.isNumber());
    EXPECT_DOUBLE_EQ(sumIdx.getNumber(), 32.0);

    EvalResult count = eval("=DCOUNT(A1:C6,\"Height\",A8:A9)");
    ASSERT_TRUE(count.isNumber());
    EXPECT_DOUBLE_EQ(count.getNumber(), 2.0);
    EvalResult counta = eval("=DCOUNTA(A1:C6,\"Tree\",A8:A9)");
    ASSERT_TRUE(counta.isNumber());
    EXPECT_DOUBLE_EQ(counta.getNumber(), 2.0);
    EvalResult countAll = eval("=DCOUNT(A1:C6,A8:A9)");
    ASSERT_TRUE(countAll.isNumber());
    EXPECT_DOUBLE_EQ(countAll.getNumber(), 2.0);

    EvalResult avg = eval("=DAVERAGE(A1:C6,\"Height\",A8:A9)");
    ASSERT_TRUE(avg.isNumber());
    EXPECT_DOUBLE_EQ(avg.getNumber(), 16.0);
    EvalResult mx = eval("=DMAX(A1:C6,\"Height\",A8:A9)");
    ASSERT_TRUE(mx.isNumber());
    EXPECT_DOUBLE_EQ(mx.getNumber(), 18.0);
    EvalResult mn = eval("=DMIN(A1:C6,\"Height\",A8:A9)");
    ASSERT_TRUE(mn.isNumber());
    EXPECT_DOUBLE_EQ(mn.getNumber(), 14.0);
    EvalResult prod = eval("=DPRODUCT(A1:C6,\"Height\",A8:A9)");
    ASSERT_TRUE(prod.isNumber());
    EXPECT_DOUBLE_EQ(prod.getNumber(), 252.0);

    EvalResult stdev = eval("=DSTDEV(A1:C6,\"Height\",A8:A9)");
    ASSERT_TRUE(stdev.isNumber());
    EXPECT_NEAR(stdev.getNumber(), std::sqrt(8.0), 1e-12);
    EvalResult varp = eval("=DVARP(A1:C6,\"Height\",A8:A9)");
    ASSERT_TRUE(varp.isNumber());
    EXPECT_DOUBLE_EQ(varp.getNumber(), 4.0);
    EvalResult stdevp = eval("=DSTDEVP(A1:C6,\"Height\",A8:A9)");
    ASSERT_TRUE(stdevp.isNumber());
    EXPECT_DOUBLE_EQ(stdevp.getNumber(), 2.0);
    EvalResult var = eval("=DVAR(A1:C6,\"Height\",A8:A9)");
    ASSERT_TRUE(var.isNumber());
    EXPECT_DOUBLE_EQ(var.getNumber(), 8.0);

    setText(3, 7, "Tree");
    setText(3, 8, "Cherry");
    EvalResult get = eval("=DGET(A1:C6,\"Height\",D8:D9)");
    ASSERT_TRUE(get.isNumber());
    EXPECT_DOUBLE_EQ(get.getNumber(), 13.0);
    EXPECT_EQ(eval("=DGET(A1:C6,\"Height\",A8:A9)").getError(), CellError::NUM);
    setText(4, 7, "Tree");
    setText(4, 8, "Fig");
    EXPECT_EQ(eval("=DGET(A1:C6,\"Height\",E8:E9)").getError(), CellError::NA);

    setText(0, 10, "Tree");
    setText(1, 10, "Height");
    setText(0, 11, "P*");
    setText(1, 11, ">10");
    EvalResult andRow = eval("=DSUM(A1:C6,\"Height\",A11:B12)");
    ASSERT_TRUE(andRow.isNumber());
    EXPECT_DOUBLE_EQ(andRow.getNumber(), 12.0);

    setText(0, 13, "Tree");
    setText(0, 14, "Cherry");
    setText(0, 15, "Apple");
    EvalResult orRows = eval("=DSUM(A1:C6,\"Height\",A14:A16)");
    ASSERT_TRUE(orRows.isNumber());
    EXPECT_DOUBLE_EQ(orRows.getNumber(), 45.0);

    EXPECT_EQ(eval("=DSUM(A1:C6,\"Nope\",A8:A9)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=DSUM(A1:C6,0,A8:A9)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=DAVERAGE(A1:C6,\"Height\",E8:E9)").getError(), CellError::DIV);
}

}  // namespace
}  // namespace cells
