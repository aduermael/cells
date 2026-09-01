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

TEST_F(FnTextExtraTest, ByteAliasesShareUnicodeImpl) {
    EvalResult lenb = eval("=LENB(\"abc\")");
    EvalResult len = eval("=LEN(\"abc\")");
    ASSERT_TRUE(lenb.isNumber());
    ASSERT_TRUE(len.isNumber());
    EXPECT_DOUBLE_EQ(lenb.getNumber(), len.getNumber());
    EvalResult leftb = eval("=LEFTB(\"hello\",2)");
    ASSERT_TRUE(leftb.isString());
    EXPECT_EQ(leftb.getString(), "he");
    EvalResult rightb = eval("=RIGHTB(\"hello\",2)");
    ASSERT_TRUE(rightb.isString());
    EXPECT_EQ(rightb.getString(), "lo");
    EvalResult midb = eval("=MIDB(\"hello\",2,3)");
    ASSERT_TRUE(midb.isString());
    EXPECT_EQ(midb.getString(), "ell");
    EvalResult findb = eval("=FINDB(\"l\",\"hello\")");
    ASSERT_TRUE(findb.isNumber());
    EXPECT_DOUBLE_EQ(findb.getNumber(), 3.0);
    EvalResult searchb = eval("=SEARCHB(\"L\",\"hello\")");
    ASSERT_TRUE(searchb.isNumber());
    EXPECT_DOUBLE_EQ(searchb.getNumber(), 3.0);
    EvalResult replaceb = eval("=REPLACEB(\"hello\",2,3,\"y\")");
    ASSERT_TRUE(replaceb.isString());
    EXPECT_EQ(replaceb.getString(), "hyo");
    EXPECT_EQ(eval("=LENB()").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=LEFTB(\"ab\",-1)").getError(), CellError::VALUE);
    FunctionRegistry& reg = FunctionRegistry::instance();
    EXPECT_TRUE(reg.exists("LENB"));
    EXPECT_TRUE(reg.exists("LEFTB"));
    EXPECT_TRUE(reg.exists("RIGHTB"));
    EXPECT_TRUE(reg.exists("MIDB"));
    EXPECT_TRUE(reg.exists("FINDB"));
    EXPECT_TRUE(reg.exists("SEARCHB"));
    EXPECT_TRUE(reg.exists("REPLACEB"));
}

TEST_F(FnTextExtraTest, TextAfterBeforeSplitValueToText) {
    EvalResult after = eval("=TEXTAFTER(\"abc-def-ghi\",\"-\")");
    ASSERT_TRUE(after.isString());
    EXPECT_EQ(after.getString(), "def-ghi");
    EvalResult after2 = eval("=TEXTAFTER(\"abc-def-ghi\",\"-\",2)");
    ASSERT_TRUE(after2.isString());
    EXPECT_EQ(after2.getString(), "ghi");
    EvalResult afterLast = eval("=TEXTAFTER(\"abc-def-ghi\",\"-\",-1)");
    ASSERT_TRUE(afterLast.isString());
    EXPECT_EQ(afterLast.getString(), "ghi");
    EvalResult afterCI = eval("=TEXTAFTER(\"abcXdef\",\"x\",1,1)");
    ASSERT_TRUE(afterCI.isString());
    EXPECT_EQ(afterCI.getString(), "def");
    EvalResult missing = eval("=TEXTAFTER(\"abc\",\"-\")");
    ASSERT_TRUE(missing.isError());
    EXPECT_EQ(missing.getError(), CellError::NA);
    EvalResult fallback = eval("=TEXTAFTER(\"abc\",\"-\",1,0,0,\"none\")");
    ASSERT_TRUE(fallback.isString());
    EXPECT_EQ(fallback.getString(), "none");

    EvalResult before = eval("=TEXTBEFORE(\"abc-def-ghi\",\"-\")");
    ASSERT_TRUE(before.isString());
    EXPECT_EQ(before.getString(), "abc");
    EvalResult before2 = eval("=TEXTBEFORE(\"abc-def-ghi\",\"-\",2)");
    ASSERT_TRUE(before2.isString());
    EXPECT_EQ(before2.getString(), "abc-def");
    EvalResult beforeEnd = eval("=TEXTBEFORE(\"abc\",\"-\",1,0,TRUE)");
    ASSERT_TRUE(beforeEnd.isString());
    EXPECT_EQ(beforeEnd.getString(), "abc");
    EXPECT_EQ(eval("=TEXTBEFORE(\"abc\",\"-\",0)").getError(), CellError::VALUE);

    EvalResult split = eval("=TEXTSPLIT(\"a,b,c\",\",\")");
    ASSERT_TRUE(split.isArray());
    EXPECT_EQ(split.getArrayRows(), 1u);
    EXPECT_EQ(split.getArrayCols(), 3u);
    EXPECT_EQ(split.getArrayAt(0, 0).getString(), "a");
    EXPECT_EQ(split.getArrayAt(0, 2).getString(), "c");

    EvalResult grid = eval("=TEXTSPLIT(\"a,b;c\",\",\",\";\")");
    ASSERT_TRUE(grid.isArray());
    EXPECT_EQ(grid.getArrayRows(), 2u);
    EXPECT_EQ(grid.getArrayCols(), 2u);
    EXPECT_EQ(grid.getArrayAt(0, 1).getString(), "b");
    EXPECT_EQ(grid.getArrayAt(1, 0).getString(), "c");
    ASSERT_TRUE(grid.getArrayAt(1, 1).isError());
    EXPECT_EQ(grid.getArrayAt(1, 1).getError(), CellError::NA);

    EvalResult pad = eval("=TEXTSPLIT(\"a,b;c\",\",\",\";\",FALSE,0,\"x\")");
    ASSERT_TRUE(pad.isArray());
    EXPECT_EQ(pad.getArrayAt(1, 1).getString(), "x");
    EXPECT_EQ(eval("=TEXTSPLIT(\"a\",\"\")").getError(), CellError::VALUE);

    EvalResult v0 = eval("=VALUETOTEXT(12)");
    ASSERT_TRUE(v0.isString());
    EXPECT_EQ(v0.getString(), "12");
    EvalResult v1 = eval("=VALUETOTEXT(\"hi\",1)");
    ASSERT_TRUE(v1.isString());
    EXPECT_EQ(v1.getString(), "\"hi\"");
    EvalResult vq = eval("=VALUETOTEXT(\"a\"\"b\",1)");
    ASSERT_TRUE(vq.isString());
    EXPECT_EQ(vq.getString(), "\"a\"\"b\"");
    EXPECT_EQ(eval("=VALUETOTEXT(1,2)").getError(), CellError::VALUE);
}

TEST_F(FnTextExtraTest, AscEncodeurlJoinSplit) {
    EvalResult ascii = eval("=ASC(UNICHAR(65313)&UNICHAR(65314))");
    ASSERT_TRUE(ascii.isString());
    EXPECT_EQ(ascii.getString(), "AB");
    EvalResult space = eval("=ASC(UNICHAR(12288)&\"x\")");
    ASSERT_TRUE(space.isString());
    EXPECT_EQ(space.getString(), " x");
    EvalResult kana = eval("=ASC(UNICHAR(12450))");  // ア → ｱ
    ASSERT_TRUE(kana.isString());
    EXPECT_EQ(kana.getString(), eval("=UNICHAR(65393)").getString());
    EXPECT_EQ(eval("=ASC()").getError(), CellError::VALUE);

    EvalResult url = eval("=ENCODEURL(\"a b\")");
    ASSERT_TRUE(url.isString());
    EXPECT_EQ(url.getString(), "a%20b");
    EvalResult tilde = eval("=ENCODEURL(\"A-._~\")");
    ASSERT_TRUE(tilde.isString());
    EXPECT_EQ(tilde.getString(), "A-._~");
    EvalResult utf = eval("=ENCODEURL(UNICHAR(233))");  // é
    ASSERT_TRUE(utf.isString());
    EXPECT_EQ(utf.getString(), "%C3%A9");
    EXPECT_EQ(eval("=ENCODEURL()").getError(), CellError::VALUE);

    EvalResult join = eval("=JOIN(\"-\",\"a\",\"b\",\"c\")");
    ASSERT_TRUE(join.isString());
    EXPECT_EQ(join.getString(), "a-b-c");
    EvalResult joinRange = eval("=JOIN(\",\",SEQUENCE(3))");
    ASSERT_TRUE(joinRange.isString());
    EXPECT_EQ(joinRange.getString(), "1,2,3");
    EXPECT_EQ(eval("=JOIN(\",\")").getError(), CellError::VALUE);

    EvalResult split = eval("=SPLIT(\"a-b-c\",\"-\")");
    ASSERT_TRUE(split.isArray());
    EXPECT_EQ(split.getArrayRows(), 1u);
    EXPECT_EQ(split.getArrayCols(), 3u);
    EXPECT_EQ(split.getArrayAt(0, 0).getString(), "a");
    EXPECT_EQ(split.getArrayAt(0, 2).getString(), "c");
    EvalResult each = eval("=SPLIT(\"a,b;c\",\",;\",TRUE)");
    ASSERT_TRUE(each.isArray());
    EXPECT_EQ(each.getArrayCols(), 3u);
    EXPECT_EQ(each.getArrayAt(0, 1).getString(), "b");
    EvalResult whole = eval("=SPLIT(\"a::b::c\",\"::\",FALSE)");
    ASSERT_TRUE(whole.isArray());
    EXPECT_EQ(whole.getArrayCols(), 3u);
    EXPECT_EQ(whole.getArrayAt(0, 1).getString(), "b");
    EvalResult dropEmpty = eval("=SPLIT(\"a--b\",\"-\",TRUE,TRUE)");
    ASSERT_TRUE(dropEmpty.isArray());
    EXPECT_EQ(dropEmpty.getArrayCols(), 2u);
    EXPECT_EQ(eval("=SPLIT(\"abc\",\"\")").getError(), CellError::VALUE);
}

}  // namespace
}  // namespace cells
