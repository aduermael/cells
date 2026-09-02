#include <gtest/gtest.h>

#include "core/cells/crdt.h"
#include "core/cells/format_buffer.h"
#include "core/cells/id.h"
#include "core/cells/js_sandbox.h"
#include "core/cells/luau_sandbox.h"
#include "core/cells/model.h"
#include "core/cells/named_ranges.h"
#include "core/cells/range.h"
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

// Same source as the web script panel. Native QuickJS (this suite) has an 8MB
// C stack; the WASM build needs STACK_SIZE >= QuickJS's stack cap or the
// browser reports "Maximum call stack size exceeded".
TEST(OfficeJsTest, ExcelRunSingleCellValues) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
    const ws = context.workbook.worksheets.getActiveWorksheet();
    ws.getRange("A2").values = [[2]];
    await context.sync();
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    Cell* a2 = cellAt(sheet, 0, 1);
    ASSERT_NE(a2, nullptr);
    EXPECT_DOUBLE_EQ(a2->value.asNumber(), 2.0);
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

TEST(OfficeJsTest, MergeUnmergeCopyInsertDeleteAndUsedRange) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const ws = context.workbook.worksheets.getActiveWorksheet();
  const title = ws.getRange("A1:C1");
  title.merge();
  title.values = [["Title"]];
  ws.getRange("A3").values = [[10]];
  ws.getRange("B3").copyFrom(ws.getRange("A3"));
  ws.getRange("A5").values = [[1]];
  ws.getRange("A6").values = [[2]];
  ws.getRange("A5").insert(Excel.InsertShiftDirection.Down);
  ws.getRange("D1").values = [[7]];
  ws.getRange("D2").values = [[8]];
  ws.getRange("D3").values = [[9]];
  ws.getRange("D2").delete(Excel.DeleteShiftDirection.Up);
  const used = ws.getUsedRange();
  used.load("address,rowIndex,columnIndex,rowCount,columnCount");
  await context.sync();
  console.log("used=" + used.address + " r" + used.rowIndex + "c" + used.columnIndex +
              " " + used.rowCount + "x" + used.columnCount);
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    const std::vector<Range*> merges = sheet->getRangesAt(0, 0, RangeFlags::MERGE);
    ASSERT_FALSE(merges.empty());
    Cell* a1 = cellAt(sheet, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_EQ(a1->value.asString(), "Title");
    Cell* b3 = cellAt(sheet, 1, 2);
    ASSERT_NE(b3, nullptr);
    EXPECT_DOUBLE_EQ(b3->value.asNumber(), 10.0);
    EXPECT_TRUE(cellAt(sheet, 0, 4) == nullptr || cellAt(sheet, 0, 4)->value.raw.empty());
    Cell* a6 = cellAt(sheet, 0, 5);
    ASSERT_NE(a6, nullptr);
    EXPECT_DOUBLE_EQ(a6->value.asNumber(), 1.0);
    Cell* a7 = cellAt(sheet, 0, 6);
    ASSERT_NE(a7, nullptr);
    EXPECT_DOUBLE_EQ(a7->value.asNumber(), 2.0);
    Cell* d1 = cellAt(sheet, 3, 0);
    ASSERT_NE(d1, nullptr);
    EXPECT_DOUBLE_EQ(d1->value.asNumber(), 7.0);
    Cell* d2 = cellAt(sheet, 3, 1);
    ASSERT_NE(d2, nullptr);
    EXPECT_DOUBLE_EQ(d2->value.asNumber(), 9.0);
    EXPECT_TRUE(cellAt(sheet, 3, 2) == nullptr || cellAt(sheet, 3, 2)->value.raw.empty());
    EXPECT_NE(result.output.find("A1:D7"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("r0c0 7x4"), std::string::npos) << result.output;

    JsSandbox unmerge;
    unmerge.setContext(workbook.get(), sheet);
    const auto unmerged = unmerge.execute(R"JS(
await Excel.run(async (context) => {
  context.workbook.worksheets.getActiveWorksheet().getRange("A1:C1").unmerge();
  await context.sync();
});
)JS");
    ASSERT_TRUE(unmerged.success) << unmerged.error;
    EXPECT_TRUE(sheet->getRangesAt(0, 0, RangeFlags::MERGE).empty());
}

TEST(OfficeJsTest, NamesAddRelativeGetRangeScalarFormatAndLayout) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const ws = context.workbook.worksheets.getActiveWorksheet();
  const named = context.workbook.names.add("Total", ws.getRange("A1"));
  named.getRange().values = [[42]];
  const inner = ws.getRange("C5:F10").getRange("B2");
  inner.values = [[99]];
  const pct = ws.getRange("B1");
  pct.values = [[0.25]];
  pct.numberFormat = "0.00%";
  const title = ws.getRange("A2:C2");
  title.format.horizontalAlignment = Excel.HorizontalAlignment.Center;
  title.format.verticalAlignment = Excel.VerticalAlignment.Center;
  title.format.wrapText = true;
  title.format.rowHeight = 32;
  ws.getRange("A2").format.columnWidth = 28;
  ws.getRange("A3").format.borders.getItem(Excel.BorderIndex.EdgeBottom).style =
      Excel.BorderLineStyle.Continuous;
  ws.getRange("A3").format.borders.getItem("EdgeBottom").color = "black";
  await context.sync();
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    Cell* a1 = cellAt(sheet, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 42.0);
    const NamedRange* nr = workbook->getNamedRanges()->resolve("Total", sheet->id);
    ASSERT_NE(nr, nullptr);
    Cell* d6 = cellAt(sheet, 3, 5);
    ASSERT_NE(d6, nullptr);
    EXPECT_DOUBLE_EQ(d6->value.asNumber(), 99.0);
    Cell* b1 = cellAt(sheet, 1, 0);
    ASSERT_NE(b1, nullptr);
    const FormatBuffer* fmt = workbook->getEntityFormat(b1->id);
    ASSERT_NE(fmt, nullptr);
    EXPECT_NE(fmt->toFormatCode().find("%"), std::string::npos);
    Cell* a2 = cellAt(sheet, 0, 1);
    ASSERT_NE(a2, nullptr);
    const StyleBuffer* styleBuf = workbook->getEntityStyle(a2->id);
    ASSERT_NE(styleBuf, nullptr);
    const CellStyle style = styleBuf->toCellStyle();
    EXPECT_EQ(style.hAlign, TextAlign::CENTER);
    EXPECT_EQ(style.vAlign, VerticalAlign::MIDDLE);
    EXPECT_TRUE(style.wrapText);
    Axis* colA = sheet->getColumnByPosition(0);
    ASSERT_NE(colA, nullptr);
    EXPECT_EQ(colA->size, 28u);
    Axis* row2 = sheet->getRowByPosition(1);
    ASSERT_NE(row2, nullptr);
    EXPECT_EQ(row2->size, 32u);
    Cell* a3 = cellAt(sheet, 0, 2);
    ASSERT_NE(a3, nullptr);
    const StyleBuffer* borderBuf = workbook->getEntityStyle(a3->id);
    ASSERT_NE(borderBuf, nullptr);
    const CellStyle borderStyle = borderBuf->toCellStyle();
    EXPECT_NE(borderStyle.border.bottom.style, BorderStyle::NONE);
}

TEST(OfficeJsTest, TablesChartsProtectionAreCallableAndCleanError) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const ws = context.workbook.worksheets.getActiveWorksheet();
  console.log("tables=" + typeof ws.tables.add);
  console.log("charts=" + typeof ws.charts.add);
  console.log("protect=" + typeof ws.protection.protect);
  ws.tables.add("A1:B2", true);
  ws.charts.add("Line", ws.getRange("A1:B2"));
  ws.protection.protect();
  await context.sync();
});
)JS");

    ASSERT_FALSE(result.success);
    EXPECT_NE(result.output.find("tables=function"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("charts=function"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("protect=function"), std::string::npos) << result.output;
    EXPECT_NE(result.error.find("OfficeExtension.Error"), std::string::npos) << result.error;
    EXPECT_NE(result.error.find("NotImplemented"), std::string::npos) << result.error;
    EXPECT_EQ(result.error.find("is not a function"), std::string::npos) << result.error;
    EXPECT_EQ(result.error.find("Cannot read"), std::string::npos) << result.error;
}

TEST(OfficeJsTest, HostedObjectStubsReturnOfficeExtensionError) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const range = context.workbook.worksheets.getActiveWorksheet().getRange("A1");
  console.log("autoFill=" + typeof range.autoFill);
  console.log("autofit=" + typeof range.format.autofitColumns);
  console.log("getIntersection=" + typeof range.getIntersection);
  range.getIntersection(range);
  await context.sync();
});
)JS");

    ASSERT_FALSE(result.success);
    EXPECT_NE(result.output.find("autoFill=function"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("autofit=function"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("getIntersection=function"), std::string::npos) << result.output;
    EXPECT_NE(result.error.find("OfficeExtension.Error"), std::string::npos) << result.error;
    EXPECT_NE(result.error.find("NotImplemented"), std::string::npos) << result.error;
    EXPECT_NE(result.error.find("getIntersection"), std::string::npos) << result.error;
    EXPECT_EQ(result.error.find("is not a function"), std::string::npos) << result.error;
}

TEST(OfficeJsTest, RangeLayoutFixtureAppliesMergeAlignAndSizes) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const ws = context.workbook.worksheets.getActiveWorksheet();
  const title = ws.getRange("A1:F1");
  title.merge();
  title.values = [["Helios Robotics, Inc."]];
  title.format.font.bold = true;
  title.format.font.size = 18;
  title.format.horizontalAlignment = "Center";
  title.format.rowHeight = 28;
  ws.getRange("A2").values = [["Fully diluted cap table — hypothetical sample"]];
  ws.getRange("A2").format.wrapText = true;
  ws.getRange("A2").format.rowHeight = 32;
  ws.getRange("A1").format.columnWidth = 28;
  ws.getRange("B1").format.columnWidth = 14;
  await context.sync();
  console.log("layout applied");
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_NE(result.output.find("layout applied"), std::string::npos);
    EXPECT_FALSE(sheet->getRangesAt(0, 0, RangeFlags::MERGE).empty());
    Cell* a1 = cellAt(sheet, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_EQ(a1->value.asString(), "Helios Robotics, Inc.");
    const StyleBuffer* styleBuf = workbook->getEntityStyle(a1->id);
    ASSERT_NE(styleBuf, nullptr);
    EXPECT_EQ(styleBuf->toCellStyle().hAlign, TextAlign::CENTER);
    EXPECT_EQ(sheet->getColumnByPosition(0)->size, 28u);
    EXPECT_EQ(sheet->getRowByPosition(0)->size, 28u);
    Cell* a2 = cellAt(sheet, 0, 1);
    ASSERT_NE(a2, nullptr);
    const StyleBuffer* a2Style = workbook->getEntityStyle(a2->id);
    ASSERT_NE(a2Style, nullptr);
    EXPECT_TRUE(a2Style->toCellStyle().wrapText);
    EXPECT_EQ(sheet->getRowByPosition(1)->size, 32u);
}

TEST(OfficeJsTest, ScriptPanelLanguageDispatch) {
    EXPECT_EQ(scriptKindFromLanguage(""), ScriptKind::Luau);
    EXPECT_EQ(scriptKindFromLanguage("luau"), ScriptKind::Luau);
    EXPECT_EQ(scriptKindFromLanguage("javascript"), ScriptKind::JavaScript);
    EXPECT_EQ(scriptKindFromLanguage("JS"), ScriptKind::JavaScript);
    EXPECT_EQ(resolveScriptKind("luau", "setCell('A1', 1)"), ScriptKind::Luau);
    EXPECT_EQ(resolveScriptKind("", "setCell('A1', 1)"), ScriptKind::Luau);
    EXPECT_EQ(resolveScriptKind("luau", "await Excel.run(async (context) => {})"),
              ScriptKind::JavaScript);
    EXPECT_EQ(resolveScriptKind("", "await Excel.run(async (context) => {})"),
              ScriptKind::JavaScript);
    EXPECT_EQ(resolveScriptKind("javascript", "setCell('A1', 1)"), ScriptKind::JavaScript);

    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();
    const auto js = executeUserScript(*workbook, sheet, R"JS(
await Excel.run(async (context) => {
  context.workbook.worksheets.getActiveWorksheet().getRange("A1").values = [[11]];
  await context.sync();
});
)JS",
                                      "javascript");
    ASSERT_TRUE(js.success) << js.error;
    ASSERT_NE(cellAt(sheet, 0, 0), nullptr);
    EXPECT_DOUBLE_EQ(cellAt(sheet, 0, 0)->value.asNumber(), 11.0);

    const auto luau = executeUserScript(*workbook, sheet, "setCell('B1', 22)", "");
    ASSERT_TRUE(luau.success) << luau.error;
    ASSERT_NE(cellAt(sheet, 1, 0), nullptr);
    EXPECT_DOUBLE_EQ(cellAt(sheet, 1, 0)->value.asNumber(), 22.0);

    // Web panel defaults to Luau; Excel.run source must still hit the JS host.
    auto workbook2 = createEmptyWorkbook();
    Sheet* sheet2 = workbook2->sheets[0].get();
    const auto fromLuauToggle = executeUserScript(*workbook2, sheet2, R"JS(
await Excel.run(async (context) => {
    const ws = context.workbook.worksheets.getActiveWorksheet();
    ws.getRange("A2").values = [[2]];
    await context.sync();
});
)JS",
                                                  "luau");
    ASSERT_TRUE(fromLuauToggle.success) << fromLuauToggle.error;
    ASSERT_NE(cellAt(sheet2, 0, 1), nullptr);
    EXPECT_DOUBLE_EQ(cellAt(sheet2, 0, 1)->value.asNumber(), 2.0);
}

TEST(OfficeJsTest, FormulasGetterSingleEquals) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();

    JsSandbox sandbox;
    sandbox.setContext(workbook.get(), sheet);
    const auto result = sandbox.execute(R"JS(
await Excel.run(async (context) => {
  const ws = context.workbook.worksheets.getActiveWorksheet();
  ws.getRange("A1").values = [[2]];
  ws.getRange("A2").formulas = [["=A1*2"]];
  const r = ws.getRange("A2");
  r.load("formulas");
  await context.sync();
  console.log("formula=" + JSON.stringify(r.formulas));
});
)JS");

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_NE(result.output.find("formula=[[\"=A1*2\"]]"), std::string::npos) << result.output;
    EXPECT_EQ(result.output.find("==A1"), std::string::npos) << result.output;
}

}  // namespace
}  // namespace cells
