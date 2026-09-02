# Excel vs Cells verification

Goal: keep Cells’ workbook behavior (XLSX I/O now, Office.js later) aligned with **desktop Microsoft Excel**. Oracle is Excel itself, not LibreOffice or another library.

## First test: open + save

1. Open an `.xlsx`, save it again — once in **Excel**, once in **Cells**.
2. Compare the two outputs (not the original vs Cells). Excel rewrite is the golden.

```
cells-verify excel-save <init.xlsx> <golden.xlsx>   # Windows + Excel COM only
cells -i <init.xlsx> -y <cells.xlsx>                 # any OS
```

Goldens are generated on **Windows Excel via COM** (`tools/excel-verify`). Do not generate goldens on Mac — Excel for Mac serializes OOXML differently. CI never runs Excel; it compares Cells output to committed goldens.

`excel-save` also writes `<golden.xlsx>.meta.json` (`host=excel-win`, Excel version/build, OS). Refuse to mix `excel-win` and `excel-mac` goldens.

## What to compare

Excel-saved vs Cells-saved is **never** byte-identical (timestamps, `calcId`, style indexes, relationship ids, extra Excel parts).

| Layer | Meaning | Gate |
|-------|---------|------|
| **Semantic** | Cells, types, formulas, styles, sheets, names, merges, freeze, `date1904` | Pass/fail once a case is green |
| **Package** | Canonical OOXML after stripping volatile bits | Report now; tighten later |

Always ignore: ZIP mtimes, `docProps` creator/dates, `Application`/`AppVersion`, `workbookPr@calcId`, `xl/calcChain.xml`, printer settings.

Never ignore: `date1904`, sheet order, values, formulas, styles, merges, names.

## Hosts (reuse, don’t fork)

```
IWorkbookHost.OpenSave(in, out)     # v1: Excel-on-Windows + later Cells CLI
IWorkbookHost.RunScript(in, js, out)  # later: same Office.js in both hosts
```

Cells already runs Office.js in-process (QuickJS, `origin/aduermael/office-js-api`). Excel does **not** execute Office.js through COM. A later sidecar add-in is required to run the same script inside Excel. Until then, Excel `RunScript` is unsupported.

## Init corpus (`init_files/`)

Inputs only — goldens are produced on Windows, not stored here yet. **91 files** (`tier_a_` 57, `tier_b_` 26, `tier_c_` 8).

| Prefix | Role |
|--------|------|
| `tier_a_` | Cells claims support (values, formulas, styles, sheets, merges, names, freeze, unicode, themes, number formats). First goldens. |
| `tier_b_` | Known remaining work (charts, pivot, tables/autofilter, CF, validation, comments, drawings, hyperlinks, protection, print). Goldens still useful: Excel keeps these, Cells drops them. |
| `tier_c_` | Later / hostile (strict OOXML, password, XML bomb, huge stress, ATP, OLE embed). **Do not** run in the default golden pass. |

Sources (do not vendor FUSE/SpreadsheetBench — 16k unlabeled real-world files):

- This repo: `testdata/xlsx/`, `tests/excel-roundtrips/data/`
- [SheetJS test_files](http://oss.sheetjs.com/test_files/) (Apache 2.0)
- [Apache POI test-data/spreadsheet](https://github.com/apache/poi/tree/trunk/test-data/spreadsheet) (Apache 2.0)

Skip `.xlsm` for v1: COM `SaveAs` format 51 writes non-macro xlsx. `tier_c_password.xlsx` is OLE-encrypted (not a zip); `tier_c_xmlbomb.xlsx` is a parser stress file.

## Later tests

- Same Office.js script in Cells and Excel → compare workbooks (needs Excel add-in).
- Formula eval vs Excel cached results (existing `tests/excel-roundtrips/` stays until the comparer is shared).
- Live `run --live` (Excel + Cells, no golden) on a Windows box.

## Commands

```bash
# Windows + Excel
bazel run :excel-verify -- excel-save verification/init_files/tier_a_simple.xlsx golden.xlsx

# Off Windows (expected)
bazel run :excel-verify -- excel-save …   # errors: requires Windows + Excel (COM)
bazel run :excel-verify -- version
```
