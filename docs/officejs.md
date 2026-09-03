# Office.js host — known gaps

Cells hosts a subset of the Excel add-in API (Office.js) in QuickJS (`JsSandbox` +
`core/cells/officejs_api.cc`). The CLI selects this path for `.js` / `.mjs` and
for inline source that contains `Excel.run` / `Office.onReady`.

**Goal:** scripts written against Excel’s Office.js Excel API should run
unchanged and round-trip through `.xlsx`.

**Last reviewed:** 2026-09-02.

Fixture scripts live in [`testdata/officejs/`](../testdata/officejs/).
`bazel run :officejs` runs the in-memory host plus `.xlsx` write/read round-trips
in `core/cells/officejs_test.cc`.

```bash
# In-memory host + xlsx round-trips
bazel run :officejs

# Same scripts via CLI
dist/cli/cells --script testdata/officejs/formulas-getter.js /tmp/out.xlsx -y
dist/cli/cells --script testdata/officejs/cross-sheet-xlsx.js /tmp/x.xlsx -y
dist/cli/cells -i /tmp/x.xlsx --eval /tmp/x.csv -y   # inspect after reload
```

Legend: **bug** = implemented surface, wrong result · **gap** = API missing.

---

## Scorecard

| Area | Status | Notes |
|------|--------|-------|
| `Excel.run` / `context.sync` / `Office.onReady` | ✅ | Queue + flush; reads empty until `load` + `sync` |
| Worksheet add / get / rename / activate | ✅ | |
| `Range.values` write + load | ✅ | `=`-prefixed strings are stored as formulas |
| `Range.formulas` write | ✅ | Same host path as `values` (`setFormulas` → `setCellFromJs`) |
| `Range.formulas` **read** | ✅ | Single leading `=` from `FormulaDisplayConverter` |
| `Range.numberFormat` | ✅ | 2D array or scalar string (`"0.00%"`) |
| Fill color + font (bold/italic/underline/name/size/color) | ✅ | |
| Named items | ✅ | `names.getItem` + `names.add` + `getRange` |
| Same-sheet formulas in memory | ✅ | `SUM`, `SUMIF`, `$A$1`, etc. |
| Cross-sheet formulas in memory / `.zcd` | ✅ | `'Cap Table'!A1` evaluates |
| Cross-sheet formulas in **`.xlsx`** | ✅ | Sheet-qualified A1 on export; quoted when the name has spaces |
| Sparse layout (skipped rows/cols) in **`.xlsx`** | ✅ | Cell `r=` uses axis position |
| Merge, copy, insert/delete, used range, borders | ✅ | Applied via CRDT flush |
| Column width, row height, alignment, wrap | ✅ | `RangeFormat` setters flush |
| Tables, charts, worksheet protection | 🟡 | Callable; `sync` returns `OfficeExtension.Error` (`NotImplemented`) |

---

## Historical bugs (fixed)

### 1. `range.formulas` getter prefixed `=` twice (fixed)

**Fixture:** [`testdata/officejs/formulas-getter.js`](../testdata/officejs/formulas-getter.js)

Excel returns `"=A1*2"`. The host used to return `"==A1*2"` because both
`FormulaDisplayConverter::toDisplayString` and `cellFormulaDisplay` added `=`.

**Fix:** `cellFormulaDisplay` returns `toDisplayString` as-is.

### 2. `.xlsx` export packed sparse rows; formula A1 text did not (fixed)

**Fixture:** [`testdata/officejs/skipped-row-xlsx.js`](../testdata/officejs/skipped-row-xlsx.js)

A skipped Excel row never created an axis. The writer numbered cells by packed
ordinal (`r="A3"` for the third stored row) while formula text used axis
position, so `A1` / skip / `A3=10` / `A4=A3*2` became a circular self-ref on
reload.

**Fix:** write `r=` from axis position. Empty Excel rows stay gaps.

### 3. Cross-sheet formulas became `#REF!` in `.xlsx` (fixed)

**Fixture:** [`testdata/officejs/cross-sheet-xlsx.js`](../testdata/officejs/cross-sheet-xlsx.js)

In memory and after `.zcd` reload, `=Data!A1` and `='Cap Table'!A1` evaluated.
`.xlsx` export stored `<f>#REF!</f>`: UUID formula text has no sheet prefix (by
design for in-engine storage), and export converted UUID→A1 with a sheet-local
`RefConverter` that could not see other sheets.

**Fix:** export uses `FormulaDisplayConverter` (workbook-wide UUID lookup, quoted
sheet names). Load resolves A1 ASTs back to UUIDs via `resolveWorkbookFormulas`.

---

## Remaining host limits

`worksheet.tables.add`, `worksheet.charts.add`, and
`worksheet.protection.protect` are callable so scripts do not throw TypeError.
The engine has no table, chart, or worksheet-protection model, so those APIs
cannot be implemented here. `sync` rejects with `OfficeExtension.Error`
(`code`: `NotImplemented`) rather than a silent no-op.

Covered by `OfficeJsTest.TablesChartsProtectionAreCallableAndCleanError`.

**Layout fixture:** [`testdata/officejs/range-layout.js`](../testdata/officejs/range-layout.js)
(merge, column widths, alignment, wrap, row height) — these *are* implemented
and round-trip through `.xlsx`.

---

## What already works (keep these green)

Covered by `core/cells/officejs_test.cc`:

- `Excel.run` + `sync`; `Office.onReady`
- `worksheets.add` / `getItem` / `getActiveWorksheet` / `activate` / `name`
- `range.values` write + `load("values")`
- Fill `#RRGGBB` / named colors; font bold, italic, underline, name, size, color
- `numberFormat` as a 2D array (`#,##0`, `$#,##0.00`, `0.00%`)
- Same-sheet formulas via `values` or `formulas` **write** (`SUM`, `SUMIF`,
  absolute `$D$13`)
- Cross-sheet formulas in memory, `.zcd`, and `.xlsx`
- `clear` / `getSelectedRange` / `names.getItem().getRange()`

---

## Integration fixture

[`testdata/officejs/cap-table.js`](../testdata/officejs/cap-table.js) round-trips:
two sheets, a blank layout row, `'Cap Table'!` references, `range.formulas`,
number formats, and fill/font.

```bash
dist/cli/cells --script testdata/officejs/cap-table.js /tmp/cap-table.xlsx -y
dist/cli/cells -i /tmp/cap-table.xlsx --eval /tmp/cap-table.csv -y
```

In memory and after `.xlsx` reload: issued 13,950,000; fully diluted
15,150,000; capital 15,500,900. Summary-sheet cross-sheet formulas keep
sheet-qualified A1 (quoted `Cap Table`) and evaluate.

All five fixtures under `testdata/officejs/` are exercised by
`officejs_test.cc` (host + `writeXLSX` / `readXLSX`).
