#include "core/cells/ui_mutation.h"

#include <gtest/gtest.h>

#include "core/cells/crdt.h"
#include "core/cells/id.h"
#include "core/cells/luau_sandbox.h"
#include "core/cells/model.h"
#include "core/cells/operation.h"

namespace cells {
namespace {

std::unique_ptr<Workbook> createTestWorkbook() {
    auto workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());
    for (uint32_t i = 0; i < 3; i++) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        col->size = 100;
        sheet->addColumn(std::move(col));
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        row->size = 24;
        sheet->addRow(std::move(row));
    }
    Axis* colA = sheet->getColumnByPosition(0);
    Axis* row1 = sheet->getRowByPosition(0);
    auto cell = std::make_unique<Cell>(generate_id(), colA->id, row1->id);
    cell->value = CellValue(42.0);
    sheet->addCell(std::move(cell));
    workbook->addSheet(std::move(sheet));
    return workbook;
}

TEST(UiMutationTest, A1FromPosition) {
    EXPECT_EQ(a1FromPosition(0, 0), "A1");
    EXPECT_EQ(a1FromPosition(1, 11), "B12");
    EXPECT_EQ(a1FromPosition(26, 0), "AA1");
}

TEST(UiMutationTest, SetCellExecutesLuauAndMutatesModel) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    LuauSandbox sandbox;

    const int64_t before = sandbox.executionCount();
    UiCellWriteResult wr = uiWriteCell(sandbox, *workbook, *sheet, 1, 1, "99", false);

    ASSERT_TRUE(wr.success) << wr.error;
    EXPECT_GT(sandbox.executionCount(), before);

    Axis* colB = sheet->getColumnByPosition(1);
    Axis* row2 = sheet->getRowByPosition(1);
    ASSERT_NE(colB, nullptr);
    ASSERT_NE(row2, nullptr);
    Cell* cell = sheet->getCellAt(colB->id, row2->id);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->id, wr.cellId);
    EXPECT_DOUBLE_EQ(cell->value.asNumber(), 99.0);
}

TEST(UiMutationTest, SetCellFormulaGoesThroughSandbox) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    LuauSandbox sandbox;

    UiCellWriteResult wr = uiWriteCell(sandbox, *workbook, *sheet, 0, 1, "=A1*2", false);
    ASSERT_TRUE(wr.success) << wr.error;
    EXPECT_GT(sandbox.executionCount(), 0);

    Cell* cell = sheet->getCell(wr.cellId);
    ASSERT_NE(cell, nullptr);
    EXPECT_TRUE(cell->isFormula());
}

TEST(UiMutationTest, UpdateExistingCellById) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    Axis* colA = sheet->getColumnByPosition(0);
    Axis* row1 = sheet->getRowByPosition(0);
    Cell* existing = sheet->getCellAt(colA->id, row1->id);
    ASSERT_NE(existing, nullptr);

    LuauSandbox sandbox;
    UiCellWriteResult wr = uiWriteCellById(sandbox, *workbook, *sheet, existing->id, "7", false);
    ASSERT_TRUE(wr.success) << wr.error;
    EXPECT_EQ(wr.cellId, existing->id);
    EXPECT_DOUBLE_EQ(existing->value.asNumber(), 7.0);
}

TEST(UiMutationTest, DeleteCellViaLuau) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    Axis* colA = sheet->getColumnByPosition(0);
    Axis* row1 = sheet->getRowByPosition(0);
    Cell* existing = sheet->getCellAt(colA->id, row1->id);
    ASSERT_NE(existing, nullptr);
    const ID id = existing->id;

    LuauSandbox sandbox;
    std::string error;
    EXPECT_TRUE(uiDeleteCell(sandbox, *workbook, *sheet, id, &error)) << error;
    EXPECT_GT(sandbox.executionCount(), 0);
    EXPECT_EQ(sheet->getCell(id), nullptr);
}

TEST(UiMutationTest, EnsureCellCreatesViaLuau) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    LuauSandbox sandbox;

    UiCellWriteResult wr = uiEnsureCell(sandbox, *workbook, *sheet, 2, 2);
    ASSERT_TRUE(wr.success) << wr.error;
    EXPECT_GT(sandbox.executionCount(), 0);
    EXPECT_NE(sheet->getCell(wr.cellId), nullptr);
}

TEST(UiMutationTest, ApplyOperationExecutesLuau) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    LuauSandbox sandbox;

    ID colId = generate_id();
    std::string payload = "{\"pos\":5}";
    Operation op = makeColSetOp(*workbook, colId, sheet->id, payload);

    const int64_t before = sandbox.executionCount();
    ApplyResult result = uiApplyOperation(sandbox, *workbook, *sheet, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);
    EXPECT_GT(sandbox.executionCount(), before);
    EXPECT_NE(sheet->getColumn(colId), nullptr);
}

TEST(UiMutationTest, ApplyUiOpWithoutQueueFails) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    LuauSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);

    ScriptResult sr = sandbox.execute("_applyUiOp()");
    EXPECT_FALSE(sr.success);
}

TEST(UiMutationTest, MissingCellIdIsNotASkipPath) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    LuauSandbox sandbox;
    const int64_t before = sandbox.executionCount();

    UiCellWriteResult wr = uiWriteCellById(sandbox, *workbook, *sheet, generate_id(), "1", false);
    EXPECT_FALSE(wr.success);
    EXPECT_EQ(sandbox.executionCount(), before);
}

TEST(UiMutationTest, FreezePanesExecutesLuau) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    LuauSandbox sandbox;
    const int64_t before = sandbox.executionCount();

    ScriptResult sr = uiFreezePanes(sandbox, *workbook, *sheet, 1, 2);
    ASSERT_TRUE(sr.success) << sr.error;
    EXPECT_GT(sandbox.executionCount(), before);
    EXPECT_EQ(sheet->freezeCol, static_cast<uint16_t>(1));
    EXPECT_EQ(sheet->freezeRow, static_cast<uint16_t>(2));
}

TEST(UiMutationTest, SetDocumentTitleExecutesLuau) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    LuauSandbox sandbox;
    const int64_t before = sandbox.executionCount();

    ScriptResult sr = uiSetDocumentTitle(sandbox, *workbook, *sheet, "Q1 Report");
    ASSERT_TRUE(sr.success) << sr.error;
    EXPECT_GT(sandbox.executionCount(), before);
    EXPECT_EQ(workbook->name, "Q1 Report");
}

TEST(UiMutationTest, MoveSheetExecutesLuau) {
    auto workbook = createTestWorkbook();
    auto sheet2 = std::make_unique<Sheet>(generate_id(), "Sheet2");
    sheet2->setWorkbook(workbook.get());
    workbook->addSheet(std::move(sheet2));
    Sheet* sheet = workbook->getSheetByIndex(0);
    ASSERT_EQ(workbook->sheetCount(), 2u);
    const std::string first = workbook->getSheetByIndex(0)->name;
    const std::string second = workbook->getSheetByIndex(1)->name;

    LuauSandbox sandbox;
    const int64_t before = sandbox.executionCount();
    ScriptResult sr = uiMoveSheet(sandbox, *workbook, *sheet, 0, 2);
    ASSERT_TRUE(sr.success) << sr.error;
    EXPECT_GT(sandbox.executionCount(), before);
    EXPECT_EQ(workbook->getSheetByIndex(0)->name, second);
    EXPECT_EQ(workbook->getSheetByIndex(1)->name, first);
}

TEST(UiMutationTest, SetThemeExecutesLuau) {
    auto workbook = createTestWorkbook();
    Sheet* sheet = workbook->getSheetByIndex(0);
    LuauSandbox sandbox;
    const int64_t before = sandbox.executionCount();

    const std::string json =
        R"({"name":"TestTheme","colorScheme":{"colors":["#111111","#222222"]},"fontScheme":{"majorFont":"Arial","minorFont":"Calibri"}})";
    ScriptResult sr = uiSetTheme(sandbox, *workbook, *sheet, json);
    ASSERT_TRUE(sr.success) << sr.error;
    EXPECT_GT(sandbox.executionCount(), before);
    ASSERT_TRUE(workbook->hasTheme());
    EXPECT_EQ(workbook->getTheme()->name, "TestTheme");
    EXPECT_EQ(workbook->getTheme()->fontScheme.majorFont, "Arial");
    EXPECT_EQ(workbook->getTheme()->colorScheme.getColor(0), "#111111");
}

}  // namespace
}  // namespace cells
