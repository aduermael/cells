#include "core/cells/functions/fn_conditional.h"

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

}  // namespace
}  // namespace cells
