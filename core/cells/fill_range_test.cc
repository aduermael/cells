#include "core/cells/fill_range.h"

#include <cstdio>

#include <optional>
#include <string>
#include <vector>

#include "core/cells/crdt.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/operation.h"

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
        createRequiredEntities(resolver, ast.get());
        resolver.resolve(ast.get());

        // Create and set the formula
        auto* formula = new Formula();
        formula->ast = ast.release();
        cell->setFormula(formula);
    }

    // Helper to create missing entities before resolution
    void createRequiredEntities(FormulaResolver& resolver, ASTNode* ast) {
        RequiredEntities required = resolver.getRequiredEntities(ast);
        for (const auto& pendingCell : required.cells) {
            auto findColPos = [&required, this](const ID& colId) -> uint32_t {
                for (const auto& c : required.columns) {
                    if (c.id == colId)
                        return c.position;
                }
                const Axis* axis = sheet->getColumn(colId);
                return axis ? axis->position : 0;
            };
            auto findRowPos = [&required, this](const ID& rowId) -> uint32_t {
                for (const auto& r : required.rows) {
                    if (r.id == rowId)
                        return r.position;
                }
                const Axis* axis = sheet->getRow(rowId);
                return axis ? axis->position : 0;
            };
            uint32_t colPos = findColPos(pendingCell.colId);
            uint32_t rowPos = findRowPos(pendingCell.rowId);
            const Axis* c = sheet->getColumnByPosition(colPos);
            const Axis* r = sheet->getRowByPosition(rowPos);
            if (c && r) {
                sheet->getOrCreateCellAt(c->id, r->id);
            }
        }
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

// =============================================================================
// Multi-peer fill sync (reproduces debug/fill-issue)
// =============================================================================
// Peer A sets a seed cell and fill-extends into empty rows/cols. Peer B must
// receive filled values after exchanging real OpLog ops (not a reimplementation).
// Root cause was local getOrCreate* minting axes/cells without COL/ROW_SET.

namespace {

// Look up cell value by grid position (0-based). Returns nullopt if missing.
std::optional<std::string> cellRawAt(Sheet* s, uint32_t colPos, uint32_t rowPos) {
    const Axis* col = s->getColumnByPosition(colPos);
    const Axis* row = s->getRowByPosition(rowPos);
    if (col == nullptr || row == nullptr) {
        return std::nullopt;
    }
    // Count live cells at this grid position (dual-entity failure mode)
    int count = 0;
    std::string raw;
    for (const auto& cellId : s->getCellIds()) {
        const Cell* c = s->getCell(cellId);
        if (c == nullptr) {
            continue;
        }
        const Axis* cCol = s->getColumn(c->colId);
        const Axis* cRow = s->getRow(c->rowId);
        if (cCol != nullptr && cRow != nullptr && cCol->position == colPos &&
            cRow->position == rowPos) {
            ++count;
            raw = c->value.raw;
        }
    }
    if (count == 0) {
        return std::nullopt;
    }
    EXPECT_EQ(count, 1) << "expected single cell entity at (" << colPos << "," << rowPos << ")";
    return raw;
}

int countCellsAtPosition(Sheet* s, uint32_t colPos, uint32_t rowPos) {
    int count = 0;
    for (const auto& cellId : s->getCellIds()) {
        const Cell* c = s->getCell(cellId);
        if (c == nullptr) {
            continue;
        }
        const Axis* cCol = s->getColumn(c->colId);
        const Axis* cRow = s->getRow(c->rowId);
        if (cCol != nullptr && cRow != nullptr && cCol->position == colPos &&
            cRow->position == rowPos) {
            ++count;
        }
    }
    return count;
}

int countRowsAtPosition(Sheet* s, uint32_t rowPos) {
    int count = 0;
    for (const auto& rowId : s->getRowIds()) {
        const Axis* r = s->getRow(rowId);
        if (r != nullptr && r->position == rowPos) {
            ++count;
        }
    }
    return count;
}

// Shared-sheet two-peer fixture: same sheet id; structure minted only via CRDT.
struct PeerPair {
    std::unique_ptr<Workbook> wb_a;
    std::unique_ptr<Workbook> wb_b;
    Sheet* sheet_a{nullptr};
    Sheet* sheet_b{nullptr};
    ID sheet_id;
    size_t ops_before_a{0};
    size_t ops_before_b{0};

    static PeerPair create() {
        PeerPair p;
        p.sheet_id = generate_id();

        p.wb_a = std::make_unique<Workbook>(generate_id(), "PeerA");
        p.wb_a->setNodeId(generate_id());
        p.wb_a->startCollaboration();
        auto sa = std::make_unique<Sheet>(p.sheet_id, "Sheet1");
        sa->setWorkbook(p.wb_a.get());
        p.wb_a->addSheet(std::move(sa));
        p.sheet_a = p.wb_a->getSheetByIndex(0);

        p.wb_b = std::make_unique<Workbook>(generate_id(), "PeerB");
        p.wb_b->setNodeId(generate_id());
        p.wb_b->startCollaboration();
        auto sb = std::make_unique<Sheet>(p.sheet_id, "Sheet1");
        sb->setWorkbook(p.wb_b.get());
        p.wb_b->addSheet(std::move(sb));
        p.sheet_b = p.wb_b->getSheetByIndex(0);

        // Seed empty SHEET is already present; no axes until CRDT ensure/set.
        p.ops_before_a = p.wb_a->getOpLog()->size();
        p.ops_before_b = p.wb_b->getOpLog()->size();
        return p;
    }

    std::vector<Operation> newOpsA() const {
        const auto& all = wb_a->getOpLog()->getAllOperations();
        return std::vector<Operation>(all.begin() + static_cast<std::ptrdiff_t>(ops_before_a),
                                      all.end());
    }

    std::vector<Operation> newOpsB() const {
        const auto& all = wb_b->getOpLog()->getAllOperations();
        return std::vector<Operation>(all.begin() + static_cast<std::ptrdiff_t>(ops_before_b),
                                      all.end());
    }

    void markOpsConsumed() {
        ops_before_a = wb_a->getOpLog()->size();
        ops_before_b = wb_b->getOpLog()->size();
    }

    // Apply peer A's new ops to B (returns applied count).
    size_t exchangeAtoB() {
        const auto ops = newOpsA();
        const size_t n = applyOperations(*wb_b, ops);
        ops_before_a = wb_a->getOpLog()->size();
        return n;
    }

    size_t exchangeBtoA() {
        const auto ops = newOpsB();
        const size_t n = applyOperations(*wb_a, ops);
        ops_before_b = wb_b->getOpLog()->size();
        return n;
    }

    // Set numeric value at position via CRDT (mirrors product setCell path).
    void setNumberA(uint32_t colPos, uint32_t rowPos, double value) {
        Cell* cell = ensureCellAtPositionViaCrdt(*wb_a, *sheet_a, colPos, rowPos);
        ASSERT_NE(cell, nullptr);
        const Axis* col = sheet_a->getColumnByPosition(colPos);
        const Axis* row = sheet_a->getRowByPosition(rowPos);
        ASSERT_NE(col, nullptr);
        ASSERT_NE(row, nullptr);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", value);
        const std::string payload = std::string(R"({"t":"n","v":")") + buf + R"(","col":")" +
                                    col->id.toString() + R"(","row":")" + row->id.toString() +
                                    R"("})";
        applyOperation(*wb_a, makeCellSetOp(*wb_a, cell->id, sheet_a->id, payload));
    }

    void setNumberB(uint32_t colPos, uint32_t rowPos, double value) {
        Cell* cell = ensureCellAtPositionViaCrdt(*wb_b, *sheet_b, colPos, rowPos);
        ASSERT_NE(cell, nullptr);
        const Axis* col = sheet_b->getColumnByPosition(colPos);
        const Axis* row = sheet_b->getRowByPosition(rowPos);
        ASSERT_NE(col, nullptr);
        ASSERT_NE(row, nullptr);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", value);
        const std::string payload = std::string(R"({"t":"n","v":")") + buf + R"(","col":")" +
                                    col->id.toString() + R"(","row":")" + row->id.toString() +
                                    R"("})";
        applyOperation(*wb_b, makeCellSetOp(*wb_b, cell->id, sheet_b->id, payload));
    }
};

}  // namespace

// Story: peer A sets B2=10, fills B2→B10; peer B must see filled values (not only seed).
TEST(FillRangeCollabTest, FillDown_SyncsFilledCellsToPeer) {
    PeerPair peers = PeerPair::create();

    // Peer A: B2 = 10 (col 1, row 1)
    peers.setNumberA(1, 1, 10.0);
    size_t applied = peers.exchangeAtoB();
    EXPECT_GT(applied, 0u);

    // Peer B sees seed
    auto seed = cellRawAt(peers.sheet_b, 1, 1);
    ASSERT_TRUE(seed.has_value());
    EXPECT_EQ(*seed, "10");

    // Peer A fills B2 → B10 (source B2, target B2:B10)
    FillResult fill = fillRange(peers.wb_a.get(), peers.sheet_a, 1, 1, 1, 1, 1, 1, 1, 9);
    ASSERT_TRUE(fill.success) << fill.error;
    EXPECT_EQ(fill.cellsFilled, 8);  // B3..B10

    // Fill into empty rows must emit ROW_SET (not local-only mint)
    const auto fillOps = peers.newOpsA();
    bool saw_row_set = false;
    for (const auto& op : fillOps) {
        if (op.type == OpType::ROW_SET) {
            saw_row_set = true;
            break;
        }
    }
    EXPECT_TRUE(saw_row_set) << "fill into new rows must emit ROW_SET for peers";
    EXPECT_GE(fillOps.size(), 8u);

    // Exchange fill ops A → B — every op must apply (no INVALID_TARGET)
    applied = applyOperations(*peers.wb_b, fillOps);
    EXPECT_EQ(applied, fillOps.size()) << "peer B must apply all fill ops (ROW_SET + CELL_SET)";
    peers.markOpsConsumed();

    // After exchange, peer B must have filled values at B2..B10
    for (uint32_t row = 1; row <= 9; ++row) {
        auto val = cellRawAt(peers.sheet_b, 1, row);
        ASSERT_TRUE(val.has_value()) << "peer B missing cell at B" << (row + 1);
        EXPECT_EQ(*val, "10") << "peer B B" << (row + 1);
        EXPECT_EQ(countCellsAtPosition(peers.sheet_b, 1, row), 1);
        EXPECT_EQ(countRowsAtPosition(peers.sheet_b, row), 1);
    }
}

// Concurrent pattern: A fills B2→B10; after fill ops reach B, B sets B9=42;
// after full exchange each peer has exactly one value at B9 (no dual overlay).
TEST(FillRangeCollabTest, FillThenPeerSet_SingleValueAtPosition) {
    PeerPair peers = PeerPair::create();

    peers.setNumberA(1, 1, 10.0);
    EXPECT_GT(peers.exchangeAtoB(), 0u);

    FillResult fill = fillRange(peers.wb_a.get(), peers.sheet_a, 1, 1, 1, 1, 1, 1, 1, 9);
    ASSERT_TRUE(fill.success);
    EXPECT_EQ(fill.cellsFilled, 8);

    // Fill must fully apply on B (including axes)
    const auto fillOps = peers.newOpsA();
    const size_t appliedFill = applyOperations(*peers.wb_b, fillOps);
    EXPECT_EQ(appliedFill, fillOps.size()) << "peer B must apply all fill ops (ROW_SET + CELL_SET)";
    peers.markOpsConsumed();

    // B9 (col 1, row 8) is 10 on both after fill
    ASSERT_EQ(cellRawAt(peers.sheet_b, 1, 8).value_or(""), "10");

    // Peer B sets B9 = 42 (on the shared axis/cell identity)
    peers.setNumberB(1, 8, 42.0);
    const auto bOps = peers.newOpsB();
    const size_t appliedB = applyOperations(*peers.wb_a, bOps);
    EXPECT_EQ(appliedB, bOps.size());

    // Single entity at B9 on both peers
    EXPECT_EQ(countCellsAtPosition(peers.sheet_a, 1, 8), 1);
    EXPECT_EQ(countCellsAtPosition(peers.sheet_b, 1, 8), 1);
    EXPECT_EQ(countRowsAtPosition(peers.sheet_a, 8), 1);
    EXPECT_EQ(countRowsAtPosition(peers.sheet_b, 8), 1);

    // LWW: both agree on one value (42 wins if B's set is later HLC)
    auto aVal = cellRawAt(peers.sheet_a, 1, 8);
    auto bVal = cellRawAt(peers.sheet_b, 1, 8);
    ASSERT_TRUE(aVal.has_value());
    ASSERT_TRUE(bVal.has_value());
    EXPECT_EQ(*aVal, *bVal);
    EXPECT_EQ(*aVal, "42");
}

// Fill right into missing columns must sync COL_SET + values.
TEST(FillRangeCollabTest, FillRight_SyncsFilledCellsToPeer) {
    PeerPair peers = PeerPair::create();

    peers.setNumberA(0, 0, 5.0);  // A1 = 5
    EXPECT_GT(peers.exchangeAtoB(), 0u);

    // Fill A1 → E1
    FillResult fill = fillRange(peers.wb_a.get(), peers.sheet_a, 0, 0, 0, 0, 0, 0, 4, 0);
    ASSERT_TRUE(fill.success);
    EXPECT_EQ(fill.cellsFilled, 4);

    const auto ops = peers.newOpsA();
    bool saw_col_set = false;
    for (const auto& op : ops) {
        if (op.type == OpType::COL_SET) {
            saw_col_set = true;
            break;
        }
    }
    EXPECT_TRUE(saw_col_set) << "fill right into new cols must emit COL_SET";

    EXPECT_EQ(applyOperations(*peers.wb_b, ops), ops.size());

    for (uint32_t col = 0; col <= 4; ++col) {
        auto val = cellRawAt(peers.sheet_b, col, 0);
        ASSERT_TRUE(val.has_value()) << "peer B missing col " << col;
        EXPECT_EQ(*val, "5");
        EXPECT_EQ(countCellsAtPosition(peers.sheet_b, col, 0), 1);
    }
}

// Multi-cell source fill (linear) replicates to peer.
TEST(FillRangeCollabTest, FillDown_LinearSequence_SyncsToPeer) {
    PeerPair peers = PeerPair::create();

    peers.setNumberA(0, 0, 1.0);  // A1
    peers.setNumberA(0, 1, 2.0);  // A2
    EXPECT_GT(peers.exchangeAtoB(), 0u);

    FillResult fill = fillRange(peers.wb_a.get(), peers.sheet_a, 0, 0, 0, 1, 0, 0, 0, 4);
    ASSERT_TRUE(fill.success);
    EXPECT_EQ(fill.cellsFilled, 3);  // A3,A4,A5

    const auto ops = peers.newOpsA();
    EXPECT_EQ(applyOperations(*peers.wb_b, ops), ops.size());

    EXPECT_EQ(cellRawAt(peers.sheet_b, 0, 0).value_or(""), "1");
    EXPECT_EQ(cellRawAt(peers.sheet_b, 0, 1).value_or(""), "2");
    EXPECT_EQ(cellRawAt(peers.sheet_b, 0, 2).value_or(""), "3");
    EXPECT_EQ(cellRawAt(peers.sheet_b, 0, 3).value_or(""), "4");
    EXPECT_EQ(cellRawAt(peers.sheet_b, 0, 4).value_or(""), "5");
}

// Fill up into missing higher rows (toward row 1 from a mid seed).
TEST(FillRangeCollabTest, FillUp_SyncsFilledCellsToPeer) {
    PeerPair peers = PeerPair::create();

    peers.setNumberA(0, 4, 7.0);  // A5 = 7
    EXPECT_GT(peers.exchangeAtoB(), 0u);

    // Source A5, target A1:A5 → fill up into A1..A4
    FillResult fill = fillRange(peers.wb_a.get(), peers.sheet_a, 0, 4, 0, 4, 0, 0, 0, 4);
    ASSERT_TRUE(fill.success);
    EXPECT_EQ(fill.cellsFilled, 4);

    const auto ops = peers.newOpsA();
    EXPECT_EQ(applyOperations(*peers.wb_b, ops), ops.size());

    for (uint32_t row = 0; row <= 4; ++row) {
        auto val = cellRawAt(peers.sheet_b, 0, row);
        ASSERT_TRUE(val.has_value()) << "missing A" << (row + 1);
        EXPECT_EQ(*val, "7");
    }
}

}  // namespace
}  // namespace cells
