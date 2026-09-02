// Office.js: Range.formulas getter must return a single leading "=".
//
// Excel: after range.formulas = [["=A1*2"]] and load("formulas"),
// range.formulas[0][0] === "=A1*2"
//
// Cells today: "==A1*2" (FormulaDisplayConverter and officejs_api both prefix "=").
//
// Run:
//   dist/cli/cells --script testdata/officejs/formulas-getter.js /tmp/formulas-getter.xlsx -y

await Excel.run(async (context) => {
  const ws = context.workbook.worksheets.getActiveWorksheet();
  ws.getRange("A1").values = [[10]];
  ws.getRange("B1").formulas = [["=A1*2"]];
  ws.getRange("C1").values = [["=A1+1"]];
  await context.sync();

  const range = ws.getRange("A1:C1");
  range.load(["values", "formulas"]);
  await context.sync();

  console.log("values", JSON.stringify(range.values));
  console.log("formulas", JSON.stringify(range.formulas));
  // Expected formulas: [[10,"=A1*2","=A1+1"]]
  // Expected values:   [[10,20,11]]
});
