#include <gtest/gtest.h>

#include "core/cells/crdt.h"
#include "core/cells/id.h"
#include "core/cells/js_sandbox.h"
#include "core/cells/luau_sandbox.h"
#include "core/cells/model.h"
#include "core/cells/named_ranges.h"
#include "core/cells/script_dispatch.h"
#include "core/cells/style_buffer.h"
#include "core/cells/style_types.h"

namespace cells {
namespace {

std::unique_ptr<Workbook> createEmptyWorkbook() {
    auto workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());
    workbook->addSheet(std::move(sheet));
    return workbook;
}

Cell* cellAt(Sheet* sheet, uint32_t col, uint32_t row) {
    return sheet->getCellAtPosition(col, row);
}

TEST(OfficeJsTest, WriteValues) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const ws = context.workbook.worksheets.getActiveWorksheet();
  const range = ws.getRange("A1:B2");
  range.values = [[1, 2], [3, 4]];
  await context.sync();
  range.load("values");
  await context.sync();
  console.log(JSON.stringify(range.values));
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    Cell* a1 = cellAt(sheet, 0, 0);
    Cell* b1 = cellAt(sheet, 1, 0);
    Cell* a2 = cellAt(sheet, 0, 1);
    Cell* b2 = cellAt(sheet, 1, 1);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(a2, nullptr);
    ASSERT_NE(b2, nullptr);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 1.0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 2.0);
    EXPECT_DOUBLE_EQ(a2->value.asNumber(), 3.0);
    EXPECT_DOUBLE_EQ(b2->value.asNumber(), 4.0);
    EXPECT_NE(result.output.find("[[1,2],[3,4]]"), std::string::npos);
}

TEST(OfficeJsTest, ReadsEmptyUntilLoadAndSync) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const range = context.workbook.worksheets.getActiveWorksheet().getRange("A1");
  range.values = [["hello"]];
  await context.sync();
  console.log("before=" + JSON.stringify(range.values));
  range.load("values");
  await context.sync();
  console.log("after=" + JSON.stringify(range.values));
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_NE(result.output.find("before="), std::string::npos);
    EXPECT_NE(result.output.find("after=[[\"hello\"]]"), std::string::npos);
    Cell* a1 = cellAt(sheet, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_EQ(a1->value.asString(), "hello");
}

TEST(OfficeJsTest, WorksheetsAddAndGetItem) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const added = context.workbook.worksheets.add("Data");
  added.getRange("A1").values = [["from-new"]];
  const existing = context.workbook.worksheets.getItem("Sheet1");
  existing.getRange("A1").values = [["from-old"]];
  await context.sync();
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(workbook->sheetCount(), 2u);
    Sheet* data = workbook->getSheetByName("Data");
    ASSERT_NE(data, nullptr);
    Cell* dataA1 = cellAt(data, 0, 0);
    ASSERT_NE(dataA1, nullptr);
    EXPECT_EQ(dataA1->value.asString(), "from-new");
    Cell* sheetA1 = cellAt(workbook->getSheetByName("Sheet1"), 0, 0);
    ASSERT_NE(sheetA1, nullptr);
    EXPECT_EQ(sheetA1->value.asString(), "from-old");
}

TEST(OfficeJsTest, FillAndFontAfterSync) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const range = context.workbook.worksheets.getActiveWorksheet().getRange("A1");
  range.values = [["styled"]];
  range.format.fill.color = "yellow";
  range.format.font.bold = true;
  range.format.font.name = "Arial";
  range.format.font.size = 14;
  range.format.font.color = "#0000FF";
  await context.sync();
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    Cell* a1 = cellAt(sheet, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_EQ(a1->value.asString(), "styled");
    const StyleBuffer* styleBuf = workbook->getEntityStyle(a1->id);
    ASSERT_NE(styleBuf, nullptr);
    const CellStyle style = styleBuf->toCellStyle();
    EXPECT_EQ(style.bgColor, "#FFFF00");
    EXPECT_TRUE(style.bold);
    EXPECT_EQ(style.fontFamily, "Arial");
    EXPECT_EQ(style.fontSize, 14);
    EXPECT_EQ(style.textColor, "#0000FF");
}

TEST(OfficeJsTest, LuauSetCellStillWorks) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    LuauSandbox luau;
    luau.setContext(workbook.get(), sheet);
    const auto luauResult = luau.execute(R"LUAU(setCell("B1", 99))LUAU");
    ASSERT_TRUE(luauResult.success) << luauResult.error;

    Cell* b1 = cellAt(sheet, 1, 0);
    ASSERT_NE(b1, nullptr);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 99.0);

    JsSandbox js;
    js.setContext(workbook.get(), sheet);
    const auto jsResult = js.execute(R"JS(
await Excel.run(async (context) => {
  const range = context.workbook.worksheets.getActiveWorksheet().getRange("C1");
  range.values = [[100]];
  await context.sync();
});
)JS");
    ASSERT_TRUE(jsResult.success) << jsResult.error;
    Cell* c1 = cellAt(sheet, 2, 0);
    ASSERT_NE(c1, nullptr);
    EXPECT_DOUBLE_EQ(c1->value.asNumber(), 100.0);
    // Luau write is still present
    EXPECT_DOUBLE_EQ(cellAt(sheet, 1, 0)->value.asNumber(), 99.0);
}

TEST(OfficeJsTest, GetCellAndNumberFormat) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const cell = context.workbook.worksheets.getActiveWorksheet().getCell(0, 0);
  cell.values = [[12.5]];
  cell.numberFormat = [["0.00"]];
  await context.sync();
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    Cell* a1 = cellAt(sheet, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 12.5);
    const FormatBuffer* fmt = workbook->getEntityFormat(a1->id);
    ASSERT_NE(fmt, nullptr);
    EXPECT_FALSE(fmt->toFormatCode().empty());
}

TEST(OfficeJsTest, NamedItemGetRange) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();
    Axis* col = ensureColumnViaCrdt(*workbook, *sheet, 0);
    Axis* row = ensureRowViaCrdt(*workbook, *sheet, 0);
    ASSERT_NE(col, nullptr);
    ASSERT_NE(row, nullptr);
    Cell* seeded = ensureCellViaCrdt(*workbook, *sheet, col->id, row->id);
    ASSERT_NE(seeded, nullptr);
    {
        const std::string payload = R"({"t":"n","v":"7","col":")" + col->id.toString() +
                                    R"(","row":")" + row->id.toString() + R"("})";
        applyOperation(*workbook, makeCellSetOp(*workbook, seeded->id, sheet->id, payload));
    }
    workbook->getNamedRanges()->defineWorkbook("MyName",
                                               NamedRangeTarget::cell(seeded->id, sheet->id));

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const named = context.workbook.names.getItem("MyName");
  const range = named.getRange();
  range.load("values");
  await context.sync();
  console.log(JSON.stringify(range.values));
  range.values = [[42]];
  await context.sync();
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    Cell* a1 = cellAt(sheet, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 42.0);
}

TEST(OfficeJsTest, OfficeOnReadyAndInitialize) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
Office.onReady(async (info) => {
  if (info.host !== Office.HostType.Excel) {
    throw new Error("expected Excel host");
  }
  await Excel.run(async (context) => {
    context.workbook.worksheets.getActiveWorksheet().getRange("A1").values = [["ready"]];
    await context.sync();
  });
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    Cell* a1 = cellAt(sheet, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_EQ(a1->value.asString(), "ready");
}

TEST(OfficeJsTest, GetSelectedRangeAndClear) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const sel = context.workbook.getSelectedRange();
  sel.values = [["tmp"]];
  await context.sync();
  sel.clear(Excel.ClearApplyTo.Contents);
  await context.sync();
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    Cell* a1 = cellAt(sheet, 0, 0);
    EXPECT_TRUE(a1 == nullptr || a1->value.raw.empty());
}

TEST(OfficeJsTest, DetectsJavaScriptByContentAndExtension) {
    EXPECT_EQ(detectScriptKind("transform.js", "print(1)"), ScriptKind::JavaScript);
    EXPECT_EQ(detectScriptKind("transform.luau", "Excel.run(function(){})"), ScriptKind::Luau);
    EXPECT_EQ(detectScriptKind("", "setCell('A1', 1)"), ScriptKind::Luau);
    EXPECT_EQ(detectScriptKind("", "await Excel.run(async (context) => {})"),
              ScriptKind::JavaScript);
}

TEST(OfficeJsTest, DispatchRunsOfficeJsAndLuau) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    const auto js = executeUserScript(*workbook, sheet, R"JS(
await Excel.run(async (context) => {
  context.workbook.worksheets.getActiveWorksheet().getRange("A1").values = [[5]];
  await context.sync();
});
)JS",
                                      ScriptKind::JavaScript);
    ASSERT_TRUE(js.success) << js.error;
    ASSERT_NE(cellAt(sheet, 0, 0), nullptr);
    EXPECT_DOUBLE_EQ(cellAt(sheet, 0, 0)->value.asNumber(), 5.0);

    const auto luau = executeUserScript(*workbook, sheet, "setCell('B1', 8)", ScriptKind::Luau);
    ASSERT_TRUE(luau.success) << luau.error;
    ASSERT_NE(cellAt(sheet, 1, 0), nullptr);
    EXPECT_DOUBLE_EQ(cellAt(sheet, 1, 0)->value.asNumber(), 8.0);
}

TEST(OfficeJsTest, WorksheetActivateAndItems) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const ws = context.workbook.worksheets;
  const other = ws.add("Other");
  other.activate();
  other.getRange("A1").values = [["active"]];
  ws.load("items");
  await context.sync();
  console.log(ws.items.map(function (s) { return s.name; }).join(","));
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    Sheet* other = workbook->getSheetByName("Other");
    ASSERT_NE(other, nullptr);
    Cell* a1 = cellAt(other, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_EQ(a1->value.asString(), "active");
}

}  // namespace
}  // namespace cells
