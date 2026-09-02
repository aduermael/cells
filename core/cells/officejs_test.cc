#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>
#include <vector>

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
#include "core/cells/xlsx_reader.h"
#include "core/cells/xlsx_writer.h"

#include "miniz.h"

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

std::string officeJsFixturePath(const std::string& name) {
    const std::string rel = "testdata/officejs/" + name;
    std::vector<std::string> candidates = {rel};
    if (const char* src = std::getenv("TEST_SRCDIR")) {
        candidates.push_back(std::string(src) + "/_main/" + rel);
        candidates.push_back(std::string(src) + "/" + rel);
    }
    if (const char* ws = std::getenv("BUILD_WORKSPACE_DIRECTORY")) {
        candidates.push_back(std::string(ws) + "/" + rel);
    }
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return rel;
}

std::string loadOfficeJsFixture(const std::string& name) {
    const std::string path = officeJsFixturePath(name);
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "missing fixture " << path;
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int gOfficeJsXlsxCounter = 0;

std::string uniqueXlsxPath(const std::string& stem) {
    return "/tmp/officejs_rt_" + std::to_string(++gOfficeJsXlsxCounter) + "_" + stem + ".xlsx";
}

class TempXlsxGuard {
public:
    explicit TempXlsxGuard(std::string path) : path_(std::move(path)) {}
    ~TempXlsxGuard() { std::remove(path_.c_str()); }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

std::string zipEntry(const std::string& path, const std::string& name) {
    mz_zip_archive archive{};
    if (mz_zip_reader_init_file(&archive, path.c_str(), 0) == 0) {
        return {};
    }
    const int index = mz_zip_reader_locate_file(&archive, name.c_str(), nullptr, 0);
    if (index < 0) {
        mz_zip_reader_end(&archive);
        return {};
    }
    mz_zip_archive_file_stat stat;
    if (mz_zip_reader_file_stat(&archive, index, &stat) == 0) {
        mz_zip_reader_end(&archive);
        return {};
    }
    std::string content;
    content.resize(stat.m_uncomp_size);
    if (mz_zip_reader_extract_to_mem(&archive, index, content.data(), content.size(), 0) == 0) {
        mz_zip_reader_end(&archive);
        return {};
    }
    mz_zip_reader_end(&archive);
    return content;
}

ScriptResult runOfficeJs(Workbook& workbook, Sheet* sheet, const std::string& src) {
    JsSandbox sandbox;
    sandbox.setContext(&workbook, sheet);
    return sandbox.execute(src);
}

std::unique_ptr<Workbook> writeAndReadXlsx(const Workbook& workbook, const std::string& path) {
    auto written = writeXLSX(workbook, path);
    EXPECT_TRUE(written.ok()) << (written.error ? written.error->toString() : "write failed");
    if (!written.ok()) {
        return nullptr;
    }
    auto read = readXLSX(path);
    EXPECT_TRUE(read.ok()) << (read.error ? read.error->toString() : "read failed");
    return std::move(read.workbook);
}

std::string allWorksheetXml(const std::string& path) {
    std::string out;
    for (int i = 1; i <= 8; ++i) {
        const std::string xml = zipEntry(path, "xl/worksheets/sheet" + std::to_string(i) + ".xml");
        if (!xml.empty()) {
            out += xml;
        }
    }
    return out;
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
    EXPECT_EQ(colA->size, 210u);  // Excel columnWidth 28 chars → px (* 7.5)
    Axis* row2 = sheet->getRowByPosition(1);
    ASSERT_NE(row2, nullptr);
    EXPECT_EQ(row2->size, 43u);  // Excel rowHeight 32 pt → px (* 96/72)
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
    EXPECT_EQ(sheet->getColumnByPosition(0)->size, 210u);  // 28 chars * 7.5
    EXPECT_EQ(sheet->getRowByPosition(0)->size, 37u);      // 28 pt * 96/72
    Cell* a2 = cellAt(sheet, 0, 1);
    ASSERT_NE(a2, nullptr);
    const StyleBuffer* a2Style = workbook->getEntityStyle(a2->id);
    ASSERT_NE(a2Style, nullptr);
    EXPECT_TRUE(a2Style->toCellStyle().wrapText);
    EXPECT_EQ(sheet->getRowByPosition(1)->size, 43u);  // 32 pt * 96/72
}

TEST(OfficeJsTest, ScriptPanelLanguageDispatch) {
    EXPECT_EQ(scriptKindFromLanguage(""), ScriptKind::Luau);
    EXPECT_EQ(scriptKindFromLanguage("luau"), ScriptKind::Luau);
    EXPECT_EQ(scriptKindFromLanguage("javascript"), ScriptKind::JavaScript);
    EXPECT_EQ(scriptKindFromLanguage("JS"), ScriptKind::JavaScript);
    EXPECT_EQ(resolveScriptKind("luau", "setCell('A1', 1)"), ScriptKind::Luau);
    EXPECT_EQ(resolveScriptKind("", "setCell('A1', 1)"), ScriptKind::Luau);
    EXPECT_EQ(resolveScriptKind("luau", "await Excel.run(async (context) => {})"),
              ScriptKind::Luau);
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

TEST(OfficeJsTest, FormulasGetterFixtureXlsxRoundTrip) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();
    const auto live = runOfficeJs(*workbook, sheet, loadOfficeJsFixture("formulas-getter.js"));
    ASSERT_TRUE(live.success) << live.error;
    EXPECT_NE(live.output.find("[[10,\"=A1*2\",\"=A1+1\"]]"), std::string::npos) << live.output;
    EXPECT_EQ(live.output.find("==A1"), std::string::npos) << live.output;

    const std::string path = uniqueXlsxPath("formulas_getter");
    TempXlsxGuard guard(path);
    auto reloaded = writeAndReadXlsx(*workbook, path);
    ASSERT_NE(reloaded, nullptr);
    Sheet* rs = reloaded->getSheetByIndex(0);
    ASSERT_NE(rs, nullptr);
    const auto after = runOfficeJs(*reloaded, rs, R"JS(
await Excel.run(async (context) => {
  const r = context.workbook.worksheets.getActiveWorksheet().getRange("A1:C1");
  r.load(["values", "formulas"]);
  await context.sync();
  console.log("reload values " + JSON.stringify(r.values));
  console.log("reload formulas " + JSON.stringify(r.formulas));
});
)JS");
    ASSERT_TRUE(after.success) << after.error;
    EXPECT_NE(after.output.find("reload values [[10,20,11]]"), std::string::npos) << after.output;
    EXPECT_NE(after.output.find("reload formulas [[10,\"=A1*2\",\"=A1+1\"]]"), std::string::npos)
        << after.output;
    EXPECT_EQ(after.output.find("=="), std::string::npos) << after.output;
}

TEST(OfficeJsTest, SkippedRowFixtureXlsxRoundTrip) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();
    const auto live = runOfficeJs(*workbook, sheet, loadOfficeJsFixture("skipped-row-xlsx.js"));
    ASSERT_TRUE(live.success) << live.error;
    EXPECT_NE(live.output.find("[[\"title\"],[\"\"],[10],[20]]"), std::string::npos) << live.output;
    EXPECT_NE(live.output.find("[\"=A3*2\"]"), std::string::npos) << live.output;
    EXPECT_EQ(sheet->getRowByPosition(1), nullptr);

    const std::string path = uniqueXlsxPath("skipped_row");
    TempXlsxGuard guard(path);
    auto written = writeXLSX(*workbook, path);
    ASSERT_TRUE(written.ok()) << (written.error ? written.error->toString() : "write failed");
    const std::string xml = zipEntry(path, "xl/worksheets/sheet1.xml");
    EXPECT_NE(xml.find("r=\"A3\""), std::string::npos) << xml;
    EXPECT_NE(xml.find("r=\"A4\""), std::string::npos) << xml;
    EXPECT_NE(xml.find(">A3*2</f>"), std::string::npos) << xml;
    EXPECT_EQ(xml.find("<row r=\"2\""), std::string::npos) << xml;

    auto read = readXLSX(path);
    ASSERT_TRUE(read.ok()) << (read.error ? read.error->toString() : "read failed");
    Sheet* rs = read.workbook->getSheetByIndex(0);
    ASSERT_NE(rs, nullptr);
    EXPECT_EQ(cellAt(rs, 0, 1), nullptr);
    const auto after = runOfficeJs(*read.workbook, rs, R"JS(
await Excel.run(async (context) => {
  const r = context.workbook.worksheets.getActiveWorksheet().getRange("A1:A4");
  r.load(["values", "formulas"]);
  await context.sync();
  console.log("reload values " + JSON.stringify(r.values));
  console.log("reload formulas " + JSON.stringify(r.formulas));
});
)JS");
    ASSERT_TRUE(after.success) << after.error;
    EXPECT_NE(after.output.find("reload values [[\"title\"],[\"\"],[10],[20]]"), std::string::npos)
        << after.output;
    EXPECT_NE(after.output.find("[\"=A3*2\"]"), std::string::npos) << after.output;
    EXPECT_EQ(after.output.find("#CIRCULAR"), std::string::npos) << after.output;
}

TEST(OfficeJsTest, CrossSheetFixtureXlsxRoundTrip) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();
    const auto live = runOfficeJs(*workbook, sheet, loadOfficeJsFixture("cross-sheet-xlsx.js"));
    ASSERT_TRUE(live.success) << live.error;
    EXPECT_NE(live.output.find("in-memory values [[42],[42],[99]]"), std::string::npos)
        << live.output;
    EXPECT_NE(live.output.find("=Data!A1"), std::string::npos) << live.output;
    EXPECT_NE(live.output.find("='Cap Table'!A1"), std::string::npos) << live.output;
    EXPECT_EQ(live.output.find("#REF!"), std::string::npos) << live.output;

    const std::string path = uniqueXlsxPath("cross_sheet");
    TempXlsxGuard guard(path);
    auto written = writeXLSX(*workbook, path);
    ASSERT_TRUE(written.ok()) << (written.error ? written.error->toString() : "write failed");
    const std::string xml = allWorksheetXml(path);
    EXPECT_NE(xml.find(">Data!A1</f>"), std::string::npos) << xml;
    EXPECT_TRUE(xml.find("'Cap Table'!A1") != std::string::npos ||
                xml.find("&apos;Cap Table&apos;!A1") != std::string::npos)
        << xml;
    EXPECT_EQ(xml.find("#REF!"), std::string::npos) << xml;

    auto read = readXLSX(path);
    ASSERT_TRUE(read.ok()) << (read.error ? read.error->toString() : "read failed");
    Sheet* summary = read.workbook->getSheetByName("Summary");
    ASSERT_NE(summary, nullptr);
    const auto after = runOfficeJs(*read.workbook, summary, R"JS(
await Excel.run(async (context) => {
  const r = context.workbook.worksheets.getItem("Summary").getRange("A1:A3");
  r.load(["values", "formulas"]);
  await context.sync();
  console.log("reload values " + JSON.stringify(r.values));
  console.log("reload formulas " + JSON.stringify(r.formulas));
});
)JS");
    ASSERT_TRUE(after.success) << after.error;
    EXPECT_NE(after.output.find("reload values [[42],[42],[99]]"), std::string::npos)
        << after.output;
    EXPECT_NE(after.output.find("=Data!A1"), std::string::npos) << after.output;
    EXPECT_NE(after.output.find("='Cap Table'!A1"), std::string::npos) << after.output;
    EXPECT_EQ(after.output.find("#REF!"), std::string::npos) << after.output;
}

TEST(OfficeJsTest, RangeLayoutFixtureXlsxRoundTrip) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();
    const auto live = runOfficeJs(*workbook, sheet, loadOfficeJsFixture("range-layout.js"));
    ASSERT_TRUE(live.success) << live.error;
    EXPECT_NE(live.output.find("layout applied"), std::string::npos) << live.output;

    const std::string path = uniqueXlsxPath("range_layout");
    TempXlsxGuard guard(path);
    auto reloaded = writeAndReadXlsx(*workbook, path);
    ASSERT_NE(reloaded, nullptr);
    Sheet* rs = reloaded->getSheetByIndex(0);
    ASSERT_NE(rs, nullptr);
    EXPECT_FALSE(rs->getRangesAt(0, 0, RangeFlags::MERGE).empty());
    Cell* a1 = cellAt(rs, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_EQ(a1->value.asString(), "Helios Robotics, Inc.");
    const StyleBuffer* styleBuf = reloaded->getEntityStyle(a1->id);
    ASSERT_NE(styleBuf, nullptr);
    EXPECT_EQ(styleBuf->toCellStyle().hAlign, TextAlign::CENTER);
    ASSERT_NE(rs->getColumnByPosition(0), nullptr);
    EXPECT_EQ(rs->getColumnByPosition(0)->size, 210u);
    ASSERT_NE(rs->getRowByPosition(0), nullptr);
    EXPECT_EQ(rs->getRowByPosition(0)->size, 37u);
    Cell* a2 = cellAt(rs, 0, 1);
    ASSERT_NE(a2, nullptr);
    const StyleBuffer* a2Style = reloaded->getEntityStyle(a2->id);
    ASSERT_NE(a2Style, nullptr);
    EXPECT_TRUE(a2Style->toCellStyle().wrapText);
    ASSERT_NE(rs->getRowByPosition(1), nullptr);
    EXPECT_EQ(rs->getRowByPosition(1)->size, 43u);
}

TEST(OfficeJsTest, CapTableFixtureXlsxRoundTrip) {
    auto workbook = createEmptyWorkbook();
    Sheet* sheet = workbook->sheets[0].get();
    const auto live = runOfficeJs(*workbook, sheet, loadOfficeJsFixture("cap-table.js"));
    ASSERT_TRUE(live.success) << live.error;
    EXPECT_NE(live.output.find("Issued shares: 13950000"), std::string::npos) << live.output;
    EXPECT_NE(live.output.find("Fully diluted shares: 15150000"), std::string::npos) << live.output;
    EXPECT_NE(live.output.find("Capital raised: 15500900"), std::string::npos) << live.output;

    Sheet* cap = workbook->getSheetByName("Cap Table");
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(cap->getRowByPosition(3), nullptr) << "blank layout row 4 must stay sparse";
    Cell* issued = cellAt(cap, 3, 13);
    Cell* fd = cellAt(cap, 3, 15);
    Cell* capital = cellAt(cap, 5, 13);
    ASSERT_NE(issued, nullptr);
    ASSERT_NE(fd, nullptr);
    ASSERT_NE(capital, nullptr);
    EXPECT_DOUBLE_EQ(issued->value.asNumber(), 13950000.0);
    EXPECT_DOUBLE_EQ(fd->value.asNumber(), 15150000.0);
    EXPECT_DOUBLE_EQ(capital->value.asNumber(), 15500900.0);

    const std::string path = uniqueXlsxPath("cap_table");
    TempXlsxGuard guard(path);
    auto written = writeXLSX(*workbook, path);
    ASSERT_TRUE(written.ok()) << (written.error ? written.error->toString() : "write failed");
    const std::string xml = allWorksheetXml(path);
    EXPECT_TRUE(xml.find("'Cap Table'!D14") != std::string::npos ||
                xml.find("&apos;Cap Table&apos;!D14") != std::string::npos)
        << xml;
    EXPECT_TRUE(xml.find("'Cap Table'!D16") != std::string::npos ||
                xml.find("&apos;Cap Table&apos;!D16") != std::string::npos)
        << xml;
    EXPECT_TRUE(xml.find("C6:C13") != std::string::npos) << xml;
    EXPECT_EQ(xml.find("#REF!"), std::string::npos) << xml;

    auto read = readXLSX(path);
    ASSERT_TRUE(read.ok()) << (read.error ? read.error->toString() : "read failed");
    Sheet* cap2 = read.workbook->getSheetByName("Cap Table");
    Sheet* summary = read.workbook->getSheetByName("Summary");
    ASSERT_NE(cap2, nullptr);
    ASSERT_NE(summary, nullptr);
    EXPECT_EQ(cellAt(cap2, 0, 3), nullptr) << "blank layout row 4 must stay empty";

    const auto after = runOfficeJs(*read.workbook, summary, R"JS(
await Excel.run(async (context) => {
  const cap = context.workbook.worksheets.getItem("Cap Table");
  const summary = context.workbook.worksheets.getItem("Summary");
  const issued = cap.getRange("D14");
  const fd = cap.getRange("D16");
  const capital = cap.getRange("F14");
  const metrics = summary.getRange("B5:B7");
  const common = summary.getRange("B15");
  issued.load("values");
  fd.load("values");
  capital.load("values");
  metrics.load(["values", "formulas"]);
  common.load(["values", "formulas"]);
  await context.sync();
  console.log("reload issued " + issued.values[0][0]);
  console.log("reload fd " + fd.values[0][0]);
  console.log("reload capital " + capital.values[0][0]);
  console.log("reload metrics " + JSON.stringify(metrics.values));
  console.log("reload metric formulas " + JSON.stringify(metrics.formulas));
  console.log("reload common " + common.values[0][0]);
  console.log("reload common formula " + JSON.stringify(common.formulas));
});
)JS");
    ASSERT_TRUE(after.success) << after.error;
    EXPECT_NE(after.output.find("reload issued 13950000"), std::string::npos) << after.output;
    EXPECT_NE(after.output.find("reload fd 15150000"), std::string::npos) << after.output;
    EXPECT_NE(after.output.find("reload capital 15500900"), std::string::npos) << after.output;
    EXPECT_NE(after.output.find("='Cap Table'!D14"), std::string::npos) << after.output;
    EXPECT_NE(after.output.find("='Cap Table'!D16"), std::string::npos) << after.output;
    EXPECT_NE(after.output.find("'Cap Table'!"), std::string::npos) << after.output;
    EXPECT_EQ(after.output.find("#REF!"), std::string::npos) << after.output;
}

}  // namespace
}  // namespace cells
