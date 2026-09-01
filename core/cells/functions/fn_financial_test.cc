#include "core/cells/functions/fn_financial.h"

#include <cmath>

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unordered_set>

#include "core/cells/formula_eval.h"
#include "core/cells/formula_functions.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

namespace cells {
namespace {

class FnFinancialTest : public ::testing::Test {
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

    Cell* setCellValue(uint32_t col, uint32_t row, double value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26];
    ID rowIds[20];
};

TEST_F(FnFinancialTest, SlnSyd) {
    EvalResult sln = eval("=SLN(30000,7500,10)");
    ASSERT_TRUE(sln.isNumber());
    EXPECT_DOUBLE_EQ(sln.getNumber(), 2250.0);
    EvalResult syd = eval("=SYD(30000,7500,10,1)");
    ASSERT_TRUE(syd.isNumber());
    EXPECT_NEAR(syd.getNumber(), 4090.909090909, 1e-8);
    EXPECT_EQ(eval("=SLN(1,0)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=SLN(1,0,0)").getError(), CellError::DIV);
    EXPECT_EQ(eval("=SYD(1,0,5,6)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=SYD(1,0,5)").getError(), CellError::VALUE);
}

TEST_F(FnFinancialTest, AnnuityPvFvPmtNper) {
    EvalResult pv = eval("=PV(0.1,10,100)");
    ASSERT_TRUE(pv.isNumber());
    EXPECT_NEAR(pv.getNumber(), -614.456710570, 1e-9);
    EvalResult fv = eval("=FV(0.1,10,-100)");
    ASSERT_TRUE(fv.isNumber());
    EXPECT_NEAR(fv.getNumber(), 1593.742460100, 1e-9);
    EvalResult pmt = eval("=PMT(0.1,10,1000)");
    ASSERT_TRUE(pmt.isNumber());
    EXPECT_NEAR(pmt.getNumber(), -162.745394883, 1e-9);
    EvalResult nper = eval("=NPER(0.1,-200,1000)");
    ASSERT_TRUE(nper.isNumber());
    EXPECT_NEAR(nper.getNumber(), 7.272540897, 1e-8);
    EvalResult zeroRate = eval("=PMT(0,10,1000)");
    ASSERT_TRUE(zeroRate.isNumber());
    EXPECT_DOUBLE_EQ(zeroRate.getNumber(), -100.0);
    EXPECT_EQ(eval("=PV(0.1,10)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=PMT(0.1,0,1000)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=PV(0.1,10,100,0,2)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=FV(\"x\",10,1)").getError(), CellError::VALUE);
}

TEST_F(FnFinancialTest, NpvEffectNominal) {
    EvalResult npv = eval("=NPV(0.1,100,200,300)");
    ASSERT_TRUE(npv.isNumber());
    EXPECT_NEAR(npv.getNumber(), 481.592787378, 1e-9);
    EvalResult effect = eval("=EFFECT(0.0525,4)");
    ASSERT_TRUE(effect.isNumber());
    EXPECT_NEAR(effect.getNumber(), 0.053542667370256, 1e-12);
    EvalResult nom = eval("=NOMINAL(0.053542667370256,4)");
    ASSERT_TRUE(nom.isNumber());
    EXPECT_NEAR(nom.getNumber(), 0.0525, 1e-12);
    EXPECT_EQ(eval("=NPV(0.1)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=NPV(-1,100)").getError(), CellError::DIV);
    EXPECT_EQ(eval("=EFFECT(0.1,0)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=EFFECT(-0.1,4)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=NOMINAL(0.1)").getError(), CellError::VALUE);
}

TEST_F(FnFinancialTest, DollarDeFr) {
    EvalResult de = eval("=DOLLARDE(1.02,16)");
    ASSERT_TRUE(de.isNumber());
    EXPECT_DOUBLE_EQ(de.getNumber(), 1.125);
    EvalResult fr = eval("=DOLLARFR(1.125,16)");
    ASSERT_TRUE(fr.isNumber());
    EXPECT_DOUBLE_EQ(fr.getNumber(), 1.02);
    EXPECT_EQ(eval("=DOLLARDE(1.02,0)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=DOLLARFR(1.125,-1)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=DOLLARDE(1.02)").getError(), CellError::VALUE);
}

TEST_F(FnFinancialTest, FvSchedulePdurationRriIspmt) {
    setCellValue(0, 0, 0.09);
    setCellValue(0, 1, 0.11);
    setCellValue(0, 2, 0.1);
    EvalResult fv = eval("=FVSCHEDULE(1,A1:A3)");
    ASSERT_TRUE(fv.isNumber());
    EXPECT_NEAR(fv.getNumber(), 1.33089, 1e-10);
    EvalResult pd = eval("=PDURATION(0.025,2000,2200)");
    ASSERT_TRUE(pd.isNumber());
    EXPECT_NEAR(pd.getNumber(), 3.859866, 1e-6);
    EvalResult rri = eval("=RRI(96,10000,11000)");
    ASSERT_TRUE(rri.isNumber());
    EXPECT_NEAR(rri.getNumber(), 0.000993307, 1e-9);
    EvalResult ispmt = eval("=ISPMT(0.1/12,1,24,100000)");
    ASSERT_TRUE(ispmt.isNumber());
    EXPECT_NEAR(ispmt.getNumber(), 100000.0 * (0.1 / 12.0) * (1.0 / 24.0 - 1.0), 1e-8);
    EXPECT_EQ(eval("=PDURATION(0,2000,2200)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=RRI(0,10000,11000)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=FVSCHEDULE(1)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=ISPMT(0.1,1,0,100)").getError(), CellError::DIV);
}

TEST_F(FnFinancialTest, DdbDb) {
    EvalResult ddb = eval("=DDB(2400,300,10,1)");
    ASSERT_TRUE(ddb.isNumber());
    EXPECT_DOUBLE_EQ(ddb.getNumber(), 480.0);
    EvalResult ddb2 = eval("=DDB(2400,300,10,2)");
    ASSERT_TRUE(ddb2.isNumber());
    EXPECT_DOUBLE_EQ(ddb2.getNumber(), 384.0);
    EvalResult db = eval("=DB(1000000,100000,6,1)");
    ASSERT_TRUE(db.isNumber());
    EXPECT_DOUBLE_EQ(db.getNumber(), 319000.0);
    EXPECT_EQ(eval("=DDB(2400,300,10,0)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=DDB(2400,300,10)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=DB(1000000,100000,6,8)").getError(), CellError::NUM);
}

TEST_F(FnFinancialTest, IpmtPpmtCum) {
    EvalResult ipmt = eval("=IPMT(0.1/12,1,24,10000)");
    ASSERT_TRUE(ipmt.isNumber());
    EXPECT_NEAR(ipmt.getNumber(), -10000.0 * (0.1 / 12.0), 1e-8);
    EvalResult pmt = eval("=PMT(0.1/12,24,10000)");
    EvalResult ppmt = eval("=PPMT(0.1/12,1,24,10000)");
    ASSERT_TRUE(pmt.isNumber());
    ASSERT_TRUE(ppmt.isNumber());
    EXPECT_NEAR(ppmt.getNumber(), pmt.getNumber() - ipmt.getNumber(), 1e-8);
    EvalResult cum = eval("=CUMIPMT(0.1/12,24,10000,1,1,0)");
    ASSERT_TRUE(cum.isNumber());
    EXPECT_NEAR(cum.getNumber(), ipmt.getNumber(), 1e-8);
    EvalResult princ = eval("=CUMPRINC(0.1/12,24,10000,1,1,0)");
    ASSERT_TRUE(princ.isNumber());
    EXPECT_NEAR(princ.getNumber(), ppmt.getNumber(), 1e-8);
    EXPECT_EQ(eval("=IPMT(0.1,1,10)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=CUMIPMT(0.1,10,1000,1,2)").getError(), CellError::VALUE);
    EXPECT_EQ(eval("=CUMIPMT(0.1,10,1000,5,2,0)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=PPMT(0.1,0,10,1000)").getError(), CellError::NUM);
}

TEST_F(FnFinancialTest, TBill) {
    EvalResult price = eval("=TBILLPRICE(DATE(2008,3,31),DATE(2008,6,1),0.09)");
    ASSERT_TRUE(price.isNumber());
    EXPECT_NEAR(price.getNumber(), 98.45, 1e-8);
    EvalResult yld = eval("=TBILLYIELD(DATE(2008,3,31),DATE(2008,6,1),98.45)");
    ASSERT_TRUE(yld.isNumber());
    EXPECT_NEAR(yld.getNumber(), (100.0 - 98.45) / 98.45 * 360.0 / 62.0, 1e-10);
    EvalResult eq = eval("=TBILLEQ(DATE(2008,3,31),DATE(2008,6,1),0.09)");
    ASSERT_TRUE(eq.isNumber());
    EXPECT_NEAR(eq.getNumber(), 365.0 * 0.09 / (360.0 - 0.09 * 62.0), 1e-12);
    EXPECT_EQ(eval("=TBILLPRICE(1,1,0.09)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=TBILLYIELD(1,10,0)").getError(), CellError::NUM);
    EXPECT_EQ(eval("=TBILLEQ(1,10)").getError(), CellError::VALUE);
}

TEST_F(FnFinancialTest, AllRegistered) {
    const char* names[] = {
        "SLN",       "SYD",     "PV",       "FV",       "PMT",        "NPER",
        "NPV",       "EFFECT",  "NOMINAL",  "DOLLARDE", "DOLLARFR",   "FVSCHEDULE",
        "PDURATION", "RRI",     "ISPMT",    "DDB",      "DB",         "IPMT",
        "PPMT",      "CUMIPMT", "CUMPRINC", "TBILLEQ",  "TBILLPRICE", "TBILLYIELD",
    };
    FunctionRegistry& reg = FunctionRegistry::instance();
    for (const char* name : names) {
        EXPECT_TRUE(reg.exists(name)) << name;
    }
}

}  // namespace
}  // namespace cells
