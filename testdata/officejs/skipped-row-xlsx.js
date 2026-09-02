// Office.js + xlsx: skipped rows must keep their Excel positions.
//
// Layout (do not write A2 — a true blank row):
//   A1 = "title"
//   A3 = 10
//   A4 = =A3*2
//
// After save/load as .xlsx, A3 must still be 10 and A4 must still be 20.
// Cells today packs A3→A2 and writes <f>A3*2</f> on packed A3 → #CIRCULAR!.
//
// Run:
//   dist/cli/cells --script testdata/officejs/skipped-row-xlsx.js /tmp/gap.xlsx -y
//   dist/cli/cells -i /tmp/gap.xlsx -e 'await Excel.run(async (c) => { const r = c.workbook.worksheets.getActiveWorksheet().getRange("A1:A4"); r.load(["values","formulas"]); await c.sync(); console.log(JSON.stringify(r.values)); console.log(JSON.stringify(r.formulas)); });'

await Excel.run(async (context) => {
  const ws = context.workbook.worksheets.getActiveWorksheet();
  ws.getRange("A1").values = [["title"]];
  ws.getRange("A3").values = [[10]];
  ws.getRange("A4").formulas = [["=A3*2"]];
  await context.sync();

  const range = ws.getRange("A1:A4");
  range.load(["values", "formulas"]);
  await context.sync();

  console.log("in-memory values", JSON.stringify(range.values));
  console.log("in-memory formulas", JSON.stringify(range.formulas));
  // Expected values:   [["title"],[""],[10],[20]]
  // Expected formulas: [["title"],[""],[10],["=A3*2"]]
});
