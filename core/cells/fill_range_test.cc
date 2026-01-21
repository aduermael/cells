#include "core/cells/fill_range.h"

#include "core/cells/formula_display.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

class FillRangeTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        workbook->setNodeId(generate_id());

        auto s = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheet = s.get();
        s->setWorkbook(workbook.get());  // Set workbook early for axis/cell storage

        // Create columns A-E (positions 0-4)
        for (int i = 0; i < 5; ++i) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = static_cast<uint32_t>(i);
            sheet->addColumn(std::move(col));
        }

        // Create rows 1-10 (positions 0-9)
        for (int i = 0; i < 10; ++i) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = static_cast<uint32_t>(i);
            sheet->addRow(std::move(row));
        }

        workbook->addSheet(std::move(s));
    }

    // Helper to set a cell value at position
    void setCellValue(int col, int row, double value) {
        Axis* colAxis = sheet->getColumnByPosition(static_cast<uint32_t>(col));
        Axis* rowAxis = sheet->getRowByPosition(static_cast<uint32_t>(row));
        Cell* cell = sheet->getOrCreateCellAt(colAxis->id, rowAxis->id);
        cell->value = CellValue(value);
    }

    void setCellValue(int col, int row, const std::string& value) {
        Axis* colAxis = sheet->getColumnByPosition(static_cast<uint32_t>(col));
        Axis* rowAxis = sheet->getRowByPosition(static_cast<uint32_t>(row));
        Cell* cell = sheet->getOrCreateCellAt(colAxis->id, rowAxis->id);
        cell->value = CellValue(value);
    }

    // Helper to get cell value at position
    double getCellNumber(int col, int row) {
        Axis* colAxis = sheet->getColumnByPosition(static_cast<uint32_t>(col));
        Axis* rowAxis = sheet->getRowByPosition(static_cast<uint32_t>(row));
        Cell* cell = sheet->getCellAt(colAxis->id, rowAxis->id);
        return cell ? cell->value.asNumber() : 0.0;
    }

    std::string getCellString(int col, int row) {
        Axis* colAxis = sheet->getColumnByPosition(static_cast<uint32_t>(col));
        Axis* rowAxis = sheet->getRowByPosition(static_cast<uint32_t>(row));
        Cell* cell = sheet->getCellAt(colAxis->id, rowAxis->id);
        return cell ? cell->value.raw : "";
    }

    // Helper to set a formula and resolve it
    void setCellFormula(int col, int row, const std::string& formulaText) {
        Axis* colAxis = sheet->getColumnByPosition(static_cast<uint32_t>(col));
        Axis* rowAxis = sheet->getRowByPosition(static_cast<uint32_t>(row));
        Cell* cell = sheet->getOrCreateCellAt(colAxis->id, rowAxis->id);

        // Parse and resolve the formula
        FormulaParser parser(formulaText);
        auto ast = parser.parse();
        FormulaResolver resolver(*workbook, *sheet);
        resolver.resolve(ast.get(), false);  // legacy mode for tests

        // Create and set the formula
        auto* formula = new Formula();
        formula->ast = ast.release();
        cell->setFormula(formula);
    }

    // Helper to get the display form of a cell's formula
    std::string getCellFormulaDisplay(int col, int row) {
        Axis* colAxis = sheet->getColumnByPosition(static_cast<uint32_t>(col));
        Axis* rowAxis = sheet->getRowByPosition(static_cast<uint32_t>(row));
        Cell* cell = sheet->getCellAt(colAxis->id, rowAxis->id);
        if (!cell || !cell->isFormula()) {
            return "";
        }
        FormulaDisplayConverter converter(*sheet, workbook.get());
        return converter.toDisplayString(cell->getFormula()->ast);
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet;
};

// =============================================================================
// Pattern Detection Tests
// =============================================================================

TEST_F(FillRangeTest, DetectConstantPattern_SingleValue) {
    setCellValue(0, 0, 42.0);

    DetectedPattern pattern = detectPattern(sheet, 0, 0, 0, 0, FillDirection::DOWN);

    EXPECT_EQ(pattern.type, PatternType::CONSTANT);
    EXPECT_DOUBLE_EQ(pattern.start, 42.0);
    EXPECT_DOUBLE_EQ(pattern.step, 0.0);
}

TEST_F(FillRangeTest, DetectLinearPattern_TwoValues) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);

    DetectedPattern pattern = detectPattern(sheet, 0, 0, 0, 1, FillDirection::DOWN);

    EXPECT_EQ(pattern.type, PatternType::LINEAR);
    EXPECT_DOUBLE_EQ(pattern.start, 2.0);  // Last value
    EXPECT_DOUBLE_EQ(pattern.step, 1.0);
}

TEST_F(FillRangeTest, DetectLinearPattern_ThreeValues) {
    setCellValue(0, 0, 5.0);
    setCellValue(0, 1, 10.0);
    setCellValue(0, 2, 15.0);

    DetectedPattern pattern = detectPattern(sheet, 0, 0, 0, 2, FillDirection::DOWN);

    EXPECT_EQ(pattern.type, PatternType::LINEAR);
    EXPECT_DOUBLE_EQ(pattern.start, 15.0);
    EXPECT_DOUBLE_EQ(pattern.step, 5.0);
}

TEST_F(FillRangeTest, DetectConstantPattern_SameValues) {
    setCellValue(0, 0, 7.0);
    setCellValue(0, 1, 7.0);
    setCellValue(0, 2, 7.0);

    DetectedPattern pattern = detectPattern(sheet, 0, 0, 0, 2, FillDirection::DOWN);

    EXPECT_EQ(pattern.type, PatternType::CONSTANT);
    EXPECT_DOUBLE_EQ(pattern.start, 7.0);
    EXPECT_DOUBLE_EQ(pattern.step, 0.0);
}

TEST_F(FillRangeTest, DetectStringPattern) {
    setCellValue(0, 0, "hello");
    setCellValue(0, 1, "world");

    DetectedPattern pattern = detectPattern(sheet, 0, 0, 0, 1, FillDirection::DOWN);

    EXPECT_EQ(pattern.type, PatternType::STRING);
    ASSERT_EQ(pattern.stringValues.size(), 2u);
    EXPECT_EQ(pattern.stringValues[0], "hello");
    EXPECT_EQ(pattern.stringValues[1], "world");
}

// =============================================================================
// Fill Direction Tests
// =============================================================================

TEST_F(FillRangeTest, GetFillDirection_Down) {
    FillDirection dir = getFillDirection(0, 0, 0, 1, 0, 0, 0, 5);
    EXPECT_EQ(dir, FillDirection::DOWN);
}

TEST_F(FillRangeTest, GetFillDirection_Up) {
    FillDirection dir = getFillDirection(0, 3, 0, 4, 0, 0, 0, 4);
    EXPECT_EQ(dir, FillDirection::UP);
}

TEST_F(FillRangeTest, GetFillDirection_Right) {
    FillDirection dir = getFillDirection(0, 0, 1, 0, 0, 0, 5, 0);
    EXPECT_EQ(dir, FillDirection::RIGHT);
}

TEST_F(FillRangeTest, GetFillDirection_Left) {
    FillDirection dir = getFillDirection(3, 0, 4, 0, 0, 0, 4, 0);
    EXPECT_EQ(dir, FillDirection::LEFT);
}

// =============================================================================
// Extrapolate Value Tests
// =============================================================================

TEST_F(FillRangeTest, ExtrapolateLinear) {
    DetectedPattern pattern;
    pattern.type = PatternType::LINEAR;
    pattern.start = 2.0;
    pattern.step = 1.0;

    EXPECT_DOUBLE_EQ(extrapolateValue(pattern, 1), 3.0);
    EXPECT_DOUBLE_EQ(extrapolateValue(pattern, 2), 4.0);
    EXPECT_DOUBLE_EQ(extrapolateValue(pattern, 3), 5.0);
}

TEST_F(FillRangeTest, ExtrapolateConstant) {
    DetectedPattern pattern;
    pattern.type = PatternType::CONSTANT;
    pattern.start = 42.0;

    EXPECT_DOUBLE_EQ(extrapolateValue(pattern, 1), 42.0);
    EXPECT_DOUBLE_EQ(extrapolateValue(pattern, 5), 42.0);
}

// =============================================================================
// Fill Range Tests - Down Direction
// =============================================================================

TEST_F(FillRangeTest, FillDown_SingleConstant) {
    setCellValue(0, 0, 42.0);

    FillResult result = fillRange(workbook.get(), sheet, 0, 0, 0, 0,  // source: A1
                                  0, 0, 0, 3);                        // target: A1:A4

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 3);

    EXPECT_DOUBLE_EQ(getCellNumber(0, 0), 42.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 1), 42.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 2), 42.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 3), 42.0);
}

TEST_F(FillRangeTest, FillDown_LinearSequence) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);

    FillResult result = fillRange(workbook.get(), sheet, 0, 0, 0, 1,  // source: A1:A2 (1, 2)
                                  0, 0, 0, 4);                        // target: A1:A5

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 3);

    EXPECT_DOUBLE_EQ(getCellNumber(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 1), 2.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 2), 3.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 3), 4.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 4), 5.0);
}

TEST_F(FillRangeTest, FillDown_LinearSequence_Step5) {
    setCellValue(0, 0, 5.0);
    setCellValue(0, 1, 10.0);

    FillResult result = fillRange(workbook.get(), sheet, 0, 0, 0, 1,  // source: A1:A2 (5, 10)
                                  0, 0, 0, 4);                        // target: A1:A5

    EXPECT_TRUE(result.success);

    EXPECT_DOUBLE_EQ(getCellNumber(0, 2), 15.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 3), 20.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 4), 25.0);
}

// =============================================================================
// Fill Range Tests - Right Direction
// =============================================================================

TEST_F(FillRangeTest, FillRight_LinearSequence) {
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);

    FillResult result = fillRange(workbook.get(), sheet, 0, 0, 1, 0,  // source: A1:B1 (1, 2)
                                  0, 0, 4, 0);                        // target: A1:E1

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 3);

    EXPECT_DOUBLE_EQ(getCellNumber(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 2.0);
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 3.0);
    EXPECT_DOUBLE_EQ(getCellNumber(3, 0), 4.0);
    EXPECT_DOUBLE_EQ(getCellNumber(4, 0), 5.0);
}

// =============================================================================
// Fill Range Tests - Up Direction
// =============================================================================

TEST_F(FillRangeTest, FillUp_LinearSequence) {
    setCellValue(0, 3, 1.0);
    setCellValue(0, 4, 2.0);

    FillResult result = fillRange(workbook.get(), sheet, 0, 3, 0, 4,  // source: A4:A5 (1, 2)
                                  0, 0, 0, 4);                        // target: A1:A5

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 3);

    // Filling up should extrapolate backwards: 1, 2 -> 0, -1, -2
    EXPECT_DOUBLE_EQ(getCellNumber(0, 2), 0.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 1), -1.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 0), -2.0);
}

// =============================================================================
// Fill Range Tests - Multiple Columns
// =============================================================================

TEST_F(FillRangeTest, FillDown_MultipleColumns) {
    // Column A: 1, 2
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    // Column B: 10, 20
    setCellValue(1, 0, 10.0);
    setCellValue(1, 1, 20.0);

    FillResult result = fillRange(workbook.get(), sheet, 0, 0, 1, 1,  // source: A1:B2
                                  0, 0, 1, 3);                        // target: A1:B4

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 4);  // 2 columns * 2 new rows

    // Column A should be 1, 2, 3, 4
    EXPECT_DOUBLE_EQ(getCellNumber(0, 2), 3.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 3), 4.0);

    // Column B should be 10, 20, 30, 40
    EXPECT_DOUBLE_EQ(getCellNumber(1, 2), 30.0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 3), 40.0);
}

// =============================================================================
// Formula Fill Tests
// =============================================================================

TEST_F(FillRangeTest, FillDown_SimpleFormula) {
    // Set up: A1=1, B1=A1
    setCellValue(0, 0, 1.0);
    setCellFormula(1, 0, "=A1");

    // Fill B1 down to B4
    FillResult result = fillRange(workbook.get(), sheet, 1, 0, 1, 0,  // source: B1
                                  1, 0, 1, 3);                        // target: B1:B4

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 3);

    // B1 should still be =A1
    EXPECT_EQ(getCellFormulaDisplay(1, 0), "=A1");

    // B2 should be =A2 (adjusted from =A1)
    EXPECT_EQ(getCellFormulaDisplay(1, 1), "=A2");

    // B3 should be =A3
    EXPECT_EQ(getCellFormulaDisplay(1, 2), "=A3");

    // B4 should be =A4
    EXPECT_EQ(getCellFormulaDisplay(1, 3), "=A4");
}

TEST_F(FillRangeTest, FillDown_FormulaWithMath) {
    // Set up: A1=10, B1=A1*2
    setCellValue(0, 0, 10.0);
    setCellFormula(1, 0, "=A1*2");

    // Fill B1 down to B3
    FillResult result = fillRange(workbook.get(), sheet, 1, 0, 1, 0,  // source: B1
                                  1, 0, 1, 2);                        // target: B1:B3

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 2);

    // Verify formula references adjusted correctly
    EXPECT_EQ(getCellFormulaDisplay(1, 0), "=A1*2");
    EXPECT_EQ(getCellFormulaDisplay(1, 1), "=A2*2");
    EXPECT_EQ(getCellFormulaDisplay(1, 2), "=A3*2");
}

TEST_F(FillRangeTest, FillRight_SimpleFormula) {
    // Set up: A1=1, A2=A1+1 (in row 0, column 0 and row 1, column 0)
    // Actually let's set it up as: A1=1, B1=A1
    setCellValue(0, 0, 1.0);
    setCellFormula(1, 0, "=A1");

    // Fill B1 right to E1
    FillResult result = fillRange(workbook.get(), sheet, 1, 0, 1, 0,  // source: B1
                                  1, 0, 4, 0);                        // target: B1:E1

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 3);

    // B1 should still be =A1
    EXPECT_EQ(getCellFormulaDisplay(1, 0), "=A1");

    // C1 should be =B1 (col shifted +1)
    EXPECT_EQ(getCellFormulaDisplay(2, 0), "=B1");

    // D1 should be =C1
    EXPECT_EQ(getCellFormulaDisplay(3, 0), "=C1");

    // E1 should be =D1
    EXPECT_EQ(getCellFormulaDisplay(4, 0), "=D1");
}

TEST_F(FillRangeTest, FillDown_FormulaWithSum) {
    // Set up: A1=1, A2=2, B1=SUM(A1:A2)
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellFormula(1, 0, "=SUM(A1:A2)");

    // Fill B1 down to B3
    FillResult result = fillRange(workbook.get(), sheet, 1, 0, 1, 0,  // source: B1
                                  1, 0, 1, 2);                        // target: B1:B3

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 2);

    // Verify formula references adjusted correctly
    EXPECT_EQ(getCellFormulaDisplay(1, 0), "=SUM(A1:A2)");
    EXPECT_EQ(getCellFormulaDisplay(1, 1), "=SUM(A2:A3)");
    EXPECT_EQ(getCellFormulaDisplay(1, 2), "=SUM(A3:A4)");
}

TEST_F(FillRangeTest, FillDown_FormulaWithAbsoluteRef) {
    // Set up: A1=100, B1=$A$1*2 (absolute reference shouldn't change)
    setCellValue(0, 0, 100.0);
    setCellFormula(1, 0, "=$A$1*2");

    // Fill B1 down to B3
    FillResult result = fillRange(workbook.get(), sheet, 1, 0, 1, 0,  // source: B1
                                  1, 0, 1, 2);                        // target: B1:B3

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 2);

    // Absolute references should not change
    EXPECT_EQ(getCellFormulaDisplay(1, 0), "=$A$1*2");
    EXPECT_EQ(getCellFormulaDisplay(1, 1), "=$A$1*2");
    EXPECT_EQ(getCellFormulaDisplay(1, 2), "=$A$1*2");
}

TEST_F(FillRangeTest, FillDown_FormulaMixedRefs) {
    // Set up: A1=10, B1=$A1+A$1 (mixed absolute/relative)
    setCellValue(0, 0, 10.0);
    setCellFormula(1, 0, "=$A1+A$1");

    // Fill B1 down to B3
    FillResult result = fillRange(workbook.get(), sheet, 1, 0, 1, 0,  // source: B1
                                  1, 0, 1, 2);                        // target: B1:B3

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 2);

    // $A1 -> col absolute (doesn't change), row relative (changes)
    // A$1 -> col relative (changes), row absolute (doesn't change)
    // But since we're filling down (not right), col doesn't matter
    // Row in $A1 should increment: $A1, $A2, $A3
    // Row in A$1 should stay same: A$1, A$1, A$1
    EXPECT_EQ(getCellFormulaDisplay(1, 0), "=$A1+A$1");
    EXPECT_EQ(getCellFormulaDisplay(1, 1), "=$A2+A$1");
    EXPECT_EQ(getCellFormulaDisplay(1, 2), "=$A3+A$1");
}

TEST_F(FillRangeTest, DetectPattern_Formula) {
    // Set up a formula cell
    setCellValue(0, 0, 1.0);
    setCellFormula(1, 0, "=A1");

    // Detect pattern
    DetectedPattern pattern = detectPattern(sheet, 1, 0, 1, 0, FillDirection::DOWN);

    EXPECT_EQ(pattern.type, PatternType::FORMULA);
    EXPECT_FALSE(pattern.formulaASTs.empty());
    EXPECT_NE(pattern.formulaASTs[0], nullptr);
}

}  // namespace
}  // namespace cells
