// Helios Robotics cap table — Office.js as an Excel add-in would write it.
//
// Produces two sheets (Cap Table + Summary), a blank layout row,
// formulas via Range.formulas, quoted cross-sheet refs, number formats,
// and fill/font. In memory and after .xlsx reload the numbers match.
//
// Expected after eval:
//   issued 13,950,000 | FD 15,150,000 | capital 15,500,900
//   Ava Chen FD 26.40% | Series A post-money $75,750,000
//
// Run:
//   dist/cli/cells --script testdata/officejs/cap-table.js /tmp/cap-table.xlsx -y
//   dist/cli/cells -i /tmp/cap-table.xlsx --eval /tmp/cap-table.csv -y

await Excel.run(async (context) => {
  const sheets = context.workbook.worksheets;
  const cap = sheets.getActiveWorksheet();
  cap.name = "Cap Table";
  const summary = sheets.add("Summary");

  cap.getRange("A1").values = [["Helios Robotics, Inc."]];
  cap.getRange("A1").format.font.bold = true;
  cap.getRange("A1").format.font.size = 18;
  cap.getRange("A1").format.font.color = "#1B365D";
  cap.getRange("A1").format.font.name = "Arial";

  cap.getRange("A2").values = [["Fully Diluted Capitalization Table"]];
  cap.getRange("A2").format.font.italic = true;
  cap.getRange("A2").format.font.size = 12;
  cap.getRange("A2").format.font.color = "#4A5568";

  cap.getRange("A3").values = [[
    "As of September 2, 2026  ·  Delaware C-Corp  ·  Authorized common: 20,000,000  ·  Hypothetical sample"
  ]];
  cap.getRange("A3").format.font.size = 10;
  cap.getRange("A3").format.font.color = "#718096";

  // Intentionally skip row 4 (blank layout row). Must survive .xlsx export.

  const headerRange = cap.getRange("A5:I5");
  headerRange.values = [[
    "Stakeholder",
    "Type",
    "Share Class",
    "Shares",
    "Price / Share",
    "Capital",
    "% Outstanding",
    "% Fully Diluted",
    "Notes"
  ]];
  headerRange.format.fill.color = "#1B365D";
  headerRange.format.font.bold = true;
  headerRange.format.font.color = "#FFFFFF";
  headerRange.format.font.name = "Arial";
  headerRange.format.font.size = 10;

  cap.getRange("A6:E13").values = [
    ["Ava Chen", "Founder", "Common", 4000000, 0.0001],
    ["Marcus Okonkwo", "Founder", "Common", 3500000, 0.0001],
    ["Priya Shah", "Founder", "Common", 1500000, 0.0001],
    ["Employee option grants", "Employee", "Common (ISO)", 800000, 0.50],
    ["Sequoia Capital", "Investor", "Seed Preferred", 1250000, 2.00],
    ["Northstar Angels", "Investor", "Seed Preferred", 500000, 2.00],
    ["Lightspeed Venture Partners", "Investor", "Series A Preferred", 2000000, 5.00],
    ["Sequoia Capital", "Investor", "Series A Preferred", 400000, 5.00]
  ];
  cap.getRange("I6:I13").values = [
    ["CEO; co-founder"],
    ["CTO; co-founder"],
    ["COO; co-founder"],
    ["Granted; $0.50 strike; 4-year vest, 1-year cliff"],
    ["Lead Seed; 1x non-participating"],
    ["Seed syndicate"],
    ["Lead Series A; 1x non-participating"],
    ["Pro-rata in Series A"]
  ];

  cap.getRange("F6:H13").formulas = [
    ["=D6*E6", "=D6/$D$14", "=D6/$D$16"],
    ["=D7*E7", "=D7/$D$14", "=D7/$D$16"],
    ["=D8*E8", "=D8/$D$14", "=D8/$D$16"],
    ["0", "=D9/$D$14", "=D9/$D$16"],
    ["=D10*E10", "=D10/$D$14", "=D10/$D$16"],
    ["=D11*E11", "=D11/$D$14", "=D11/$D$16"],
    ["=D12*E12", "=D12/$D$14", "=D12/$D$16"],
    ["=D13*E13", "=D13/$D$14", "=D13/$D$16"]
  ];

  cap.getRange("A14:C14").values = [["Total issued & outstanding", "", ""]];
  cap.getRange("D14").formulas = [["=SUM(D6:D13)"]];
  cap.getRange("F14").formulas = [["=SUM(F6:F13)"]];
  cap.getRange("G14").formulas = [["=D14/$D$14"]];
  cap.getRange("H14").formulas = [["=D14/$D$16"]];
  cap.getRange("I14").values = [["Excludes unallocated option pool"]];
  cap.getRange("A14:I14").format.fill.color = "#E8EEF4";
  cap.getRange("A14:I14").format.font.bold = true;

  cap.getRange("A15:E15").values = [
    ["Unallocated option pool", "Reserve", "Common (options)", 1200000, 0]
  ];
  cap.getRange("F15").values = [[0]];
  cap.getRange("G15").values = [[0]];
  cap.getRange("H15").formulas = [["=D15/$D$16"]];
  cap.getRange("I15").values = [["Reserved for future hires; not outstanding"]];
  cap.getRange("A15:I15").format.fill.color = "#FFF8E7";

  cap.getRange("A16").values = [["Total fully diluted"]];
  cap.getRange("D16").formulas = [["=D14+D15"]];
  cap.getRange("F16").formulas = [["=F14"]];
  cap.getRange("H16").formulas = [["=D16/$D$16"]];
  cap.getRange("I16").values = [["Issued + unallocated pool"]];
  cap.getRange("A16:I16").format.fill.color = "#1B365D";
  cap.getRange("A16:I16").format.font.bold = true;
  cap.getRange("A16:I16").format.font.color = "#FFFFFF";

  applyNumberFormat(cap.getRange("D6:D16"), "#,##0");
  applyNumberFormat(cap.getRange("E6:E13"), "$#,##0.0000");
  applyNumberFormat(cap.getRange("F6:F16"), "$#,##0.00");
  applyNumberFormat(cap.getRange("G6:G16"), "0.00%");
  applyNumberFormat(cap.getRange("H6:H16"), "0.00%");

  cap.getRange("A18").values = [["Notes"]];
  cap.getRange("A18").format.font.bold = true;
  cap.getRange("A18").format.font.color = "#1B365D";
  cap.getRange("A19:A22").values = [
    ["All figures are fictional and for Office.js / cells demo purposes only."],
    ["Preferred stock is 1x non-participating, non-cumulative, converting 1:1 into common."],
    ["Series A price of $5.00/share implies a fully diluted post-money valuation of $5.00 × total FD shares."],
    ["Option grants vest over 4 years with a 1-year cliff. Unallocated pool is included in fully diluted % only."]
  ];
  cap.getRange("A19:A22").format.font.size = 10;
  cap.getRange("A19:A22").format.font.color = "#4A5568";

  summary.getRange("A1").values = [["Helios Robotics — Cap Table Summary"]];
  summary.getRange("A1").format.font.bold = true;
  summary.getRange("A1").format.font.size = 16;
  summary.getRange("A1").format.font.color = "#1B365D";
  summary.getRange("A1").format.font.name = "Arial";

  summary.getRange("A3").values = [["Key metrics"]];
  summary.getRange("A3").format.font.bold = true;
  summary.getRange("A3").format.font.color = "#1B365D";

  const metricHeaders = summary.getRange("A4:B4");
  metricHeaders.values = [["Metric", "Value"]];
  metricHeaders.format.fill.color = "#1B365D";
  metricHeaders.format.font.bold = true;
  metricHeaders.format.font.color = "#FFFFFF";

  summary.getRange("A5:A11").values = [
    ["Issued & outstanding shares"],
    ["Fully diluted shares"],
    ["Cash capital raised"],
    ["Series A price / share"],
    ["Post-money FD valuation"],
    ["Series A cash raised"],
    ["Implied pre-money (FD)"]
  ];
  summary.getRange("B5:B11").formulas = [
    ["='Cap Table'!D14"],
    ["='Cap Table'!D16"],
    ["='Cap Table'!F14"],
    ["5"],
    ["=B8*B6"],
    ["12000000"],
    ["=B9-B10"]
  ];
  applyNumberFormat(summary.getRange("B5:B6"), "#,##0");
  applyNumberFormat(summary.getRange("B7:B11"), "$#,##0.00");

  summary.getRange("A13").values = [["Ownership by share class"]];
  summary.getRange("A13").format.font.bold = true;
  summary.getRange("A13").format.font.color = "#1B365D";

  const classHeaders = summary.getRange("A14:D14");
  classHeaders.values = [["Share class", "Shares", "% Outstanding", "% Fully Diluted"]];
  classHeaders.format.fill.color = "#1B365D";
  classHeaders.format.font.bold = true;
  classHeaders.format.font.color = "#FFFFFF";

  summary.getRange("A15:A19").values = [
    ["Common (founders + ISOs)"],
    ["Seed Preferred"],
    ["Series A Preferred"],
    ["Unallocated option pool"],
    ["Total fully diluted"]
  ];
  summary.getRange("B15:D19").formulas = [
    [
      "=SUMIF('Cap Table'!C6:C13,\"Common\",'Cap Table'!D6:D13)+SUMIF('Cap Table'!C6:C13,\"Common (ISO)\",'Cap Table'!D6:D13)",
      "=B15/'Cap Table'!D14",
      "=B15/'Cap Table'!D16"
    ],
    [
      "=SUMIF('Cap Table'!C6:C13,\"Seed Preferred\",'Cap Table'!D6:D13)",
      "=B16/'Cap Table'!D14",
      "=B16/'Cap Table'!D16"
    ],
    [
      "=SUMIF('Cap Table'!C6:C13,\"Series A Preferred\",'Cap Table'!D6:D13)",
      "=B17/'Cap Table'!D14",
      "=B17/'Cap Table'!D16"
    ],
    ["='Cap Table'!D15", "0", "=B18/'Cap Table'!D16"],
    ["=SUM(B15:B18)", "", "=B19/'Cap Table'!D16"]
  ];
  summary.getRange("A19:D19").format.font.bold = true;
  summary.getRange("A19:D19").format.fill.color = "#E8EEF4";
  applyNumberFormat(summary.getRange("B15:B19"), "#,##0");
  applyNumberFormat(summary.getRange("C15:D19"), "0.00%");

  summary.getRange("A21").values = [["Financing history"]];
  summary.getRange("A21").format.font.bold = true;
  summary.getRange("A21").format.font.color = "#1B365D";

  const roundHeaders = summary.getRange("A22:F22");
  roundHeaders.values = [[
    "Round", "Close date", "Price / share", "New shares", "Amount raised", "Post-money (priced)"
  ]];
  roundHeaders.format.fill.color = "#1B365D";
  roundHeaders.format.font.bold = true;
  roundHeaders.format.font.color = "#FFFFFF";

  summary.getRange("A23:D25").values = [
    ["Founding", "2023-01-15", 0.0001, 9000000],
    ["Seed", "2024-06-12", 2.00, 1750000],
    ["Series A", "2026-03-20", 5.00, 2400000]
  ];
  summary.getRange("E23:E25").formulas = [
    ["=C23*D23"],
    ["=C24*D24"],
    ["=C25*D25"]
  ];
  summary.getRange("F23").values = [["n/a (par)"]];
  summary.getRange("F24").values = [[14000000]];
  summary.getRange("F25").formulas = [["=C25*'Cap Table'!D16"]];
  applyNumberFormat(summary.getRange("C23:C25"), "$#,##0.0000");
  applyNumberFormat(summary.getRange("D23:D25"), "#,##0");
  applyNumberFormat(summary.getRange("E23:E25"), "$#,##0.00");
  applyNumberFormat(summary.getRange("F24:F25"), "$#,##0");

  summary.getRange("A27").values = [[
    "Seed post-money shown as a negotiated headline ($14M). Series A post-money is price × fully diluted shares after the round."
  ]];
  summary.getRange("A27").format.font.size = 10;
  summary.getRange("A27").format.font.italic = true;
  summary.getRange("A27").format.font.color = "#718096";

  cap.activate();
  await context.sync();

  const issued = cap.getRange("D14");
  const fd = cap.getRange("D16");
  const capital = cap.getRange("F14");
  const avaFd = cap.getRange("H6");
  const postMoney = summary.getRange("B9");
  const commonShares = summary.getRange("B15");
  issued.load("values");
  fd.load("values");
  capital.load("values");
  avaFd.load("values");
  postMoney.load("values");
  commonShares.load("values");
  await context.sync();

  console.log("Issued shares:", issued.values[0][0]);
  console.log("Fully diluted shares:", fd.values[0][0]);
  console.log("Capital raised:", capital.values[0][0]);
  console.log("Ava Chen FD %:", avaFd.values[0][0]);
  console.log("Post-money FD valuation:", postMoney.values[0][0]);
  console.log("Common + ISO shares:", commonShares.values[0][0]);
});

function applyNumberFormat(range, code) {
  const rows = [];
  for (let r = 0; r < range.rowCount; r++) {
    const row = [];
    for (let c = 0; c < range.columnCount; c++) row.push(code);
    rows.push(row);
  }
  range.numberFormat = rows;
}
