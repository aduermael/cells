// Office.js: range layout APIs that Excel add-ins use for a titled sheet.
//
// Expected: merge A1:F1, set column widths, center-align the title, wrap
// the subtitle, set row height. None of this is applied today:
//   merge() throws; columnWidth / rowHeight / alignment / wrapText are
//   silent no-ops on RangeFormat.
//
// Run:
//   dist/cli/cells --script testdata/officejs/range-layout.js /tmp/layout.xlsx -y

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
  ws.getRange("C1").format.columnWidth = 18;
  ws.getRange("D1").format.columnWidth = 14;
  ws.getRange("E1").format.columnWidth = 14;
  ws.getRange("F1").format.columnWidth = 16;

  await context.sync();
  console.log("layout applied");
});
