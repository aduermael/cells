// Office.js + xlsx: cross-sheet refs must survive export.
//
//   Data!A1 = 42
//   'Cap Table'!A1 = 99
//   Summary!A1 = =Data!A1
//   Summary!A2 = ='Data'!A1
//   Summary!A3 = ='Cap Table'!A1
//
// In memory, .zcd, and .xlsx these evaluate as sheet-qualified A1
// (quoted when the sheet name has spaces).
//
// Run:
//   dist/cli/cells --script testdata/officejs/cross-sheet-xlsx.js /tmp/xsheet.xlsx -y
//   dist/cli/cells -i /tmp/xsheet.xlsx -e 'await Excel.run(async (c) => { const r = c.workbook.worksheets.getItem("Summary").getRange("A1:A3"); r.load(["values","formulas"]); await c.sync(); console.log(JSON.stringify(r.values)); console.log(JSON.stringify(r.formulas)); });'
//   dist/cli/cells --script testdata/officejs/cross-sheet-xlsx.js /tmp/xsheet.zcd -y

await Excel.run(async (context) => {
  const sheets = context.workbook.worksheets;
  const data = sheets.getActiveWorksheet();
  data.name = "Data";
  const summary = sheets.add("Summary");
  const cap = sheets.add("Cap Table");

  data.getRange("A1").values = [[42]];
  cap.getRange("A1").values = [[99]];
  summary.getRange("A1").formulas = [["=Data!A1"]];
  summary.getRange("A2").formulas = [["='Data'!A1"]];
  summary.getRange("A3").formulas = [["='Cap Table'!A1"]];
  await context.sync();

  const range = summary.getRange("A1:A3");
  range.load(["values", "formulas"]);
  await context.sync();

  console.log("in-memory values", JSON.stringify(range.values));
  console.log("in-memory formulas", JSON.stringify(range.formulas));
  // Expected values:   [[42],[42],[99]]
  // Expected formulas: [["=Data!A1"],["=Data!A1"],["='Cap Table'!A1"]]
});
