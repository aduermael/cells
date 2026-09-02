# Office.js host — known gaps

Cells hosts a subset of the Excel add-in API (Office.js) in QuickJS (`JsSandbox` +
`core/cells/officejs_api.cc`). The CLI selects this path for `.js` / `.mjs` and
for inline source that contains `Excel.run` / `Office.onReady`.

**Goal:** scripts written against Excel’s Office.js Excel API should run
unchanged and round-trip through `.xlsx`.

**Last reviewed:** 2026-09-02 (cap-table / CLI script work on
`aduermael/office-js-api`).

Fixture scripts that *should* already work live in [`testdata/officejs/`](../testdata/officejs/).
Use them when extending `bazel run :officejs` (today’s suite is in-memory C++
in `core/cells/officejs_test.cc`).

```bash
# In-memory host (no file)
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
| `Range.formulas` **read** | ✅ | `toDisplayString` already prefixes `=` |
| `Range.numberFormat` | ✅ | 2D array or scalar string (`"0.00%"`) |
| Fill color + font (bold/italic/underline/name/size/color) | ✅ | |
| Named items | ✅ | `names.getItem` + `names.add` + `getRange` |
| Same-sheet formulas in memory | ✅ | `SUM`, `SUMIF`, `$A$1`, etc. |
| Cross-sheet formulas in memory / `.zcd` | ✅ | `'Cap Table'!A1` evaluates |
| Cross-sheet formulas in **`.xlsx`** | ✅ | Sheet-qualified A1 on export |
| Sparse layout (skipped rows/cols) in **`.xlsx`** | ✅ | Cell `r=` uses axis position |
| Merge, copy, insert/delete, used range, borders | ✅ | Applied via CRDT flush |
| Column width, row height, alignment, wrap | ✅ | `RangeFormat` setters flush |
| Tables, charts, worksheet protection | 🟡 | Callable; `sync` returns `OfficeExtension.Error` (`NotImplemented`) |

---

## Historical bugs (fixed)

### 1. `range.formulas` getter prefixes `=` twice (fixed)

**Fixture:** [`testdata/officejs/formulas-getter.js`](../testdata/officejs/formulas-getter.js)

Excel returns `"=A1*2"`. Cells returns `"==A1*2"` both in-memory and after reload.

`FormulaDisplayConverter::toDisplayString` already adds `=`:

```62:67:core/cells/formula_display.cc
std::string FormulaDisplayConverter::toDisplayString(const ASTNode* ast) const {
    if (ast == nullptr) {
        return "";
    }
    return "=" + nodeToString(ast);
}
```

The Office.js host adds another:

```458:468:core/cells/officejs_api.cc
std::string cellFormulaDisplay(...) {
    // ...
    return "=" + conv.toDisplayString(f->ast);
}
```

`.xlsx` XML is fine (`<f>A1*2</f>`). The damage is the JS property: copying
`range.formulas` onto another range would write `==A1*2`.

**Fix:** `cellFormulaDisplay` should return `toDisplayString` as-is (or stop
prefixing in the display converter and keep one source of truth).

### 2. `.xlsx` export packs sparse rows; formula A1 text does not

**Fixture:** [`testdata/officejs/skipped-row-xlsx.js`](../testdata/officejs/skipped-row-xlsx.js)

Cells is sparse: a skipped Excel row never creates an axis. The writer then
numbers cells by **packed ordinal**, while formula text uses **axis position**.

Repro: `A1="title"`, skip `A2`, `A3=10`, `A4="=A3*2"`.

| Cell | In memory | After `.xlsx` reload |
|------|-----------|----------------------|
| A1 | title | title |
| A2 | empty | **10** (was A3) |
| A3 | 10 | **`#CIRCULAR!`** (`=A3*2` now self-ref) |
| A4 | 20 | empty |

Writer maps `rowId → i` (0, 1, 2, …) and emits `r="A{i+1}"`:

```1216:1221:core/cells/xlsx_writer.cc
    for (size_t i = 0; i < columns.size(); ++i) {
        colIdToIndex[columns[i].second.toString()] = i;
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        rowIdToIndex[rows[i].second.toString()] = i;
    }
```

`RefConverter::setContext` maps the same ids to `axis.position` and emits
`A{position+1}`. The cell lands at packed `A3` with formula text still `A3*2`.

**Fix:** write `r=` from **axis position** (and emit empty row/col records so
Excel keeps the gap), or make formula A1 conversion use the same packed index
as the cell `r` attribute — position-preserving is the Excel-compatible one.

### 3. Cross-sheet formulas become `#REF!` in `.xlsx` (`.zcd` is fine)

**Fixture:** [`testdata/officejs/cross-sheet-xlsx.js`](../testdata/officejs/cross-sheet-xlsx.js)

In memory and after `.zcd` reload: `=Data!A1` and `='Cap Table'!A1` evaluate.
The same workbook saved as `.xlsx` stores `<f>#REF!</f>` on the Summary sheet.

Two stacked causes:

1. UUID formula text has **no sheet prefix** for cell refs (by design for
   in-engine storage):

```84:90:core/cells/formula_serializer.cc
std::string FormulaSerializer::cellRefToUuidString(const CellRefNode* node) {
    // NOTE: We do NOT output sheet prefix for cell references anymore.
    // Cell UUIDs are globally unique, so no sheet context is needed for storage.
    // The sheet context is only needed for display ...
```

2. XLSX export builds a `RefConverter` for **the current sheet only** and never
   attaches the workbook:

```2173:2175:core/cells/xlsx_writer.cc
        RefConverter refConverter;
        refConverter.setContext(sheet);
```

`formulaToA1` then looks the cell UUID up in `cellIdToLocation_` for *this*
sheet, misses it, and writes `#REF!`. `formulaToA1` also concatenates
`sheet->name + "!"` **without quoting** when a `!<sheetId>` prefix *is*
present — names with spaces (`Cap Table`) would still be invalid Excel A1.

**Fix:** when converting UUID → A1 for export, resolve the cell via the
workbook (same approach as `FormulaDisplayConverter`), emit `Sheet!A1` or
`'Cap Table'!A1`, and call `refConverter.setWorkbook(&workbook)`.

---

## Remaining host limits

Layout and structure calls in the scorecard persist through `Excel.run` +
`sync`. `worksheet.tables.add`, `worksheet.charts.add`, and
`worksheet.protection.protect` are callable; the engine has no table/chart/
protection model, so `sync` rejects with `OfficeExtension.Error` (`code`:
`NotImplemented`) rather than a TypeError or a silent no-op.

**Fixture:** [`testdata/officejs/range-layout.js`](../testdata/officejs/range-layout.js)
(merge, column widths, alignment, wrap, row height).

---

## What already works (keep these green)

Covered by `core/cells/officejs_test.cc` and still valid after the cap-table
exercise:

- `Excel.run` + `sync`; `Office.onReady`
- `worksheets.add` / `getItem` / `getActiveWorksheet` / `activate` / `name`
- `range.values` write + `load("values")`
- Fill `#RRGGBB` / named colors; font bold, italic, underline, name, size, color
- `numberFormat` as a 2D array (`#,##0`, `$#,##0.00`, `0.00%`)
- Same-sheet formulas via `values` or `formulas` **write** (`SUM`, `SUMIF`,
  absolute `$D$13`)
- Cross-sheet formulas **in memory and `.zcd`**
- `clear` / `getSelectedRange` / `names.getItem().getRange()`

---

## Integration fixture

[`testdata/officejs/cap-table.js`](../testdata/officejs/cap-table.js) is the
workbook that *should* round-trip: two sheets, a blank layout row, `'Cap Table'!`
references, `range.formulas`, number formats, and fill/font.

```bash
dist/cli/cells --script testdata/officejs/cap-table.js /tmp/cap-table.xlsx -y
dist/cli/cells -i /tmp/cap-table.xlsx --eval /tmp/cap-table.csv -y
```

Today: in-memory numbers are correct; `.xlsx` reload breaks because of bugs 2
and 3 (and `formulas` load shows `==`). `.zcd` keeps cross-sheet values but
still shows `==` on the getter.

When the three bugs above are fixed, this script plus the focused fixtures
should move into `officejs_test.cc` (in-memory) and an xlsx round-trip test
(CLI or `XLSXWriter`/`XLSXReader`).
