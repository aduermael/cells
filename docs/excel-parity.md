# Excel Parity Tracker

Living status of Cells vs Microsoft Excel: formulas, spreadsheet features, file I/O, and UI.

**Last reviewed:** 2026-09-01  
**Function count:** **167** registered (Excel has ~484 worksheet functions ≈ **35%**)  
**Source of truth for implementations:** `core/cells/functions/fn_*.cc`

Update this document when adding functions, shipping features, or changing XLSX fidelity. Cross-check the function list against the registry if counts drift.

---

## Scorecard

| Area | Status | Notes |
|------|--------|-------|
| Formula engine (parse / eval / deps / spill) | ✅ Solid | AST-native C++; UUID-stable refs |
| Function library breadth | 🟡 Partial | 167 / ~484 Excel functions |
| Grid editing & selection | ✅ Solid | Fill handle, multi-range, formula bar |
| Cell / range formatting | ✅ Solid | Fonts, fills, borders, number formats, themes |
| XLSX import / export | 🟡 Partial | Values, formulas, styles, merges; fidelity still improving |
| Collaboration | ✅ Solid | CRDT + P2P WebRTC (beyond Excel) |
| Charts / pivots / tables / CF | ❌ Missing | Major Excel power features |
| Undo / redo | ❌ Missing | Planned (CRDT branch-based) |
| Scripting | 🟡 Different | Luau (not VBA); Python/TS planned |

Legend: ✅ implemented · 🟡 partial / in progress · ❌ not implemented · 🔵 Cells-only (not in Excel)

---

## Formula engine

| Component | Status |
|-----------|--------|
| Lexer / tokenizer | ✅ |
| AST parser | ✅ |
| Execution engine | ✅ |
| Dependency graph + recalc | ✅ |
| Circular reference detection | ✅ (`#CIRCULAR!`) |
| Volatile functions (`NOW`, `TODAY`, `RAND`, …) | ✅ |
| Dynamic arrays (spill) | ✅ |
| Spill range operator (`A1#`) | ✅ |
| Cross-sheet references | ✅ |
| Named range references | ✅ |
| Shared formulas (master/subscriber) | ✅ |
| Implicit intersection / `@` (legacy) | 🟡 Limited / evolving |
| Structured table references (`Table1[Col]`) | ❌ |
| 3D references (`Sheet1:Sheet3!A1`) | ❌ |
| External workbook links | ❌ |

Details: [Formula Engine](./formula-engine.md).

---

## Function library

### Summary by category

| Category | Implemented | Excel (approx.) | Coverage (rough) |
|----------|------------:|----------------:|------------------:|
| Math & trig | 73 | ~80 | Core + trig + conditional aggregates |
| Logic / information (subset) | 21 | ~40 | Strong modern set (`LET`, `LAMBDA`, `IFS`, …) |
| Text | 21 | ~50 | Core string ops + TEXTJOIN/CLEAN |
| Date / time | 20 | ~40 | Core extractors + workday/datedif helpers |
| Statistics | 17 | ~100 | Basic + LARGE/SMALL/RANK/MODE/QUARTILE |
| Array / dynamic | 5 | ~25 | Core spill set only |
| Lookup & reference | 10 | ~40 | Classic + ROW/COLUMN/ADDRESS/CHOOSE |
| Financial | 0 | ~55 | Entire category missing |
| Engineering | 0 | ~50 | Entire category missing |
| Database | 0 | ~12 | Entire category missing |
| Cube / web / other | 0 | many | Out of scope for now |
| **Total registered** | **167** | **~484** | **~35%** |

Excel totals vary by version (Microsoft 365 adds functions over time). Counts above are directional, not exact product marketing numbers.

### Roundtrip corpus coverage

Against unique functions found in `tests/excel-roundtrips/data/` (~182 functions after stripping Excel internal prefixes):

| Metric | Value |
|--------|------:|
| Present in Cells | ~101 |
| Missing | ~81 |
| Corpus coverage | ~55% |

Categories currently green in that harness: **logical**, **math-basic**, **math-trig**, **nested-formulas**.

---

### Implemented functions (167)

Registered names use underscores where Excel uses dots (e.g. `CEILING.MATH` → `CEILING_MATH` on XLSX import via `_xlfn.` stripping). Some aliases omit the underscore (`STDEVS` for `STDEV.S`, `PERCENTILEINC` for `PERCENTILE.INC`).

#### Math (73)

| Function | Description |
|----------|-------------|
| `SUM` | Adds all numbers in a range |
| `PRODUCT` | Multiplies all numbers |
| `SUMSQ` | Sum of squares |
| `AVERAGE` | Arithmetic mean |
| `COUNT` | Counts cells containing numbers |
| `COUNTA` | Counts non-empty cells |
| `MIN` / `MAX` | Smallest / largest value |
| `ABS` | Absolute value |
| `SQRT` / `SQRTPI` | Square root; √(n·π) |
| `POWER` | Raise to a power |
| `ROUND` / `ROUNDUP` / `ROUNDDOWN` | Rounding |
| `MROUND` / `EVEN` / `ODD` | Round to multiple / even / odd |
| `FLOOR` / `CEILING` | 1-arg: toward −∞ / +∞; 2-arg classic significance |
| `FLOOR_MATH` / `CEILING_MATH` | Round to significance (`FLOOR.MATH` / `CEILING.MATH`) |
| `FLOOR_PRECISE` / `CEILING_PRECISE` / `ISO_CEILING` | Precise/ISO significance (abs) |
| `MOD` / `INT` / `TRUNC` / `QUOTIENT` | Integer arithmetic |
| `GCD` / `LCM` | Greatest common divisor / least common multiple |
| `SIGN` | Sign of a number |
| `EXP` / `LN` / `LOG` / `LOG10` | Exponentials and logs |
| `FACT` / `FACTDOUBLE` | Factorial / double factorial |
| `PI` | π |
| `SIN` / `COS` / `TAN` / `ASIN` / `ACOS` / `ATAN` / `ATAN2` / `ACOT` | Trig |
| `CSC` / `SEC` / `COT` | Reciprocal trig |
| `SINH` / `COSH` / `TANH` / `ASINH` / `ACOSH` / `ATANH` / `ACOTH` | Hyperbolic |
| `CSCH` / `SECH` / `COTH` | Reciprocal hyperbolic |
| `RADIANS` / `DEGREES` | Angle conversion |
| `RAND` / `RANDBETWEEN` | Random (volatile) |
| `SUMIF` / `SUMIFS` | Conditional sum |
| `COUNTIF` / `COUNTIFS` | Conditional count |
| `AVERAGEIF` / `AVERAGEIFS` | Conditional average |
| `MINIFS` / `MAXIFS` | Conditional min / max |
| `SUMPRODUCT` | Sum of products |

#### Logic (21)

| Function | Description |
|----------|-------------|
| `IF` / `IFS` / `SWITCH` | Conditionals |
| `AND` / `OR` / `XOR` / `NOT` | Boolean combinators |
| `IFERROR` / `IFNA` / `NA` | Error handling |
| `LET` / `LAMBDA` | Named locals and anonymous functions |
| `TRUE` / `FALSE` | Boolean constants |
| `EXACT` | Case-sensitive string equality |
| `ISBLANK` / `ISNUMBER` / `ISTEXT` / `ISERROR` / `ISLOGICAL` / `ISNA` | Type tests |

#### Text (21)

| Function | Description |
|----------|-------------|
| `LEN` / `LEFT` / `RIGHT` / `MID` | Length and substrings |
| `TRIM` / `UPPER` / `LOWER` / `PROPER` | Normalize case / spaces |
| `FIND` / `SEARCH` | Locate substring |
| `SUBSTITUTE` / `REPLACE` | Replace text |
| `CONCAT` / `CONCATENATE` / `REPT` | Join / repeat |
| `TEXT` / `VALUE` | Format / parse numbers |
| `CHAR` / `CODE` | Character codes |
| `TEXTJOIN` | Join with delimiter |
| `CLEAN` | Strip non-printable characters |

#### Lookup (10)

| Function | Description |
|----------|-------------|
| `INDEX` | Value at position |
| `MATCH` | Position of value |
| `VLOOKUP` | Vertical lookup |
| `HLOOKUP` | Horizontal lookup |
| `ROW` / `ROWS` | Row number / height |
| `COLUMN` / `COLUMNS` | Column number / width |
| `ADDRESS` | Cell address as text |
| `CHOOSE` | Pick value by index |

#### Date / time (20)

| Function | Volatile | Description |
|----------|:--------:|-------------|
| `NOW` | ✅ | Current date and time |
| `TODAY` | ✅ | Current date |
| `DATE` / `TIME` | | Construct date / time |
| `DATEVALUE` / `TIMEVALUE` | | Parse text |
| `YEAR` / `MONTH` / `DAY` | | Date parts |
| `HOUR` / `MINUTE` / `SECOND` | | Time parts |
| `WEEKDAY` | | Day of week |
| `EOMONTH` | | End of month offset |
| `EDATE` | | Date offset by months |
| `DAYS` | | Day difference |
| `DATEDIF` | | Date difference by unit |
| `WEEKNUM` | | Week number |
| `NETWORKDAYS` | | Working days between dates |
| `WORKDAY` | | Date after N working days |

#### Statistics (17)

| Function | Description |
|----------|-------------|
| `MEDIAN` | Median |
| `STDEV` / `STDEVS` / `STDEVP` | Std. deviation (sample / sample alias / population) |
| `VAR` / `VARS` / `VARP` | Variance |
| `PERCENTILE` / `PERCENTILEINC` / `PERCENTILEEXC` | Percentiles |
| `LARGE` / `SMALL` | k-th largest / smallest |
| `RANK` / `RANK.EQ` | Rank in a list |
| `MODE` / `MODE.SNGL` | Most frequent number |
| `QUARTILE` / `QUARTILE.INC` | Inclusive quartile |
| `COUNTBLANK` | Empty cells |

#### Array / dynamic (5)

| Function | Description |
|----------|-------------|
| `UNIQUE` | Unique values |
| `SORT` | Sort array |
| `FILTER` | Filter by criteria |
| `SEQUENCE` | Number sequence |
| `TRANSPOSE` | Transpose |

---

### Missing functions (priority tracker)

Not exhaustive of all ~484 Excel functions — focused on high-impact gaps and functions already appearing in the Excel roundtrip corpus.

#### Lookup & reference — high priority

| Function | Notes |
|----------|-------|
| `XLOOKUP` / `XMATCH` | Modern Excel default |
| `OFFSET` / `INDIRECT` | Dynamic refs (volatile) |

#### Financial — entire category missing

| Function | Notes |
|----------|-------|
| `PV` / `FV` / `PMT` / `NPER` / `RATE` | Loan / annuity basics |
| `NPV` / `IRR` / `XNPV` / `XIRR` | Investment analysis |
| `DB` / `DDB` / `SLN` | Depreciation |

#### Dynamic arrays (beyond core spill set)

| Function | Notes |
|----------|-------|
| `SORTBY` / `RANDARRAY` | |
| `VSTACK` / `HSTACK` | Stack arrays |
| `TOCOL` / `TOROW` / `TAKE` / `DROP` | Shape |
| `CHOOSECOLS` / `CHOOSEROWS` | |
| `MAP` / `REDUCE` / `SCAN` | LAMBDA helpers |

#### Information

| Function | Notes |
|----------|-------|
| `ISERR` / `ISFORMULA` / `TYPE` / `N` / `CELL` / `ERROR.TYPE` | |

#### Database

| Function | Notes |
|----------|-------|
| `DSUM` / `DCOUNT` / `DCOUNTA` / `DAVERAGE` / `DMAX` / `DMIN` / `DGET` | |

#### Engineering

| Function | Notes |
|----------|-------|
| `CONVERT` | Unit conversion |
| `BIN2DEC` / `DEC2BIN` / `HEX2DEC` / `DEC2HEX` | Base conversion |
| `COMPLEX` / `DELTA` | |

---

## Spreadsheet features

### Implemented

| Feature | Status | Notes |
|---------|--------|-------|
| Multi-sheet workbooks | ✅ | Tabs, reorder, rename |
| Cell values (number, text, bool, date, error) | ✅ | |
| Formulas + formula bar | ✅ | Autocomplete, colored refs |
| Selection / multi-select | ✅ | |
| Copy / cut / paste | ✅ | Internal + TSV for Excel/Sheets |
| Fill handle | ✅ | |
| Insert / delete rows & columns | ✅ | UUID-stable formulas |
| Column width / row height | ✅ | Including XLSX import |
| Hide columns / rows | ✅ | Axis flags |
| Freeze panes | ✅ | |
| Zoom (10%–400%) | ✅ | |
| Merged cells | ✅ | Range flags |
| Named ranges | ✅ | Workbook- and sheet-scoped |
| Number formats | ✅ | Built-in + custom codes |
| Fonts / bold / italic / underline | ✅ | |
| Fill colors / font colors | ✅ | Theme colors supported |
| Borders | ✅ | |
| Alignment / text wrap | ✅ | |
| Range styles | ✅ | Content-addressed styles |
| Themes | 🟡 | Import/export improving |
| Real-time collaboration | 🔵 | CRDT + P2P (not Excel) |
| Presence cursors | 🔵 | |
| Luau scripting | 🔵 | CLI + web panel |
| Agent collab via CLI | ✅ | External agents join rooms with `cells session start <url>` (or one-shot `cells sync`) |
| `.zcd` native format | 🔵 | Git-friendly text CRDT log |

### Not implemented (Excel has these)

| Feature | Status | Notes |
|---------|--------|-------|
| Undo / redo | ❌ | Documented as planned in CRDT design |
| Charts / sparklines | ❌ | |
| Pivot tables / PivotCharts | ❌ | |
| Excel Tables (ListObjects) | ❌ | No structured refs |
| AutoFilter / sort UI | ❌ | Formula `SORT`/`FILTER` only |
| Conditional formatting | ❌ | `RangeFlags::CONDITIONAL_FORMAT` reserved |
| Data validation / dropdowns | ❌ | `RangeFlags::DATA_VALIDATION` reserved |
| Comments / notes / @mentions | ❌ | |
| Hyperlinks | ❌ | Theme slot exists; no first-class links |
| Find / replace | ❌ | |
| Print layout / page setup / print area | ❌ | `PRINT_AREA` flag reserved |
| Sheet / workbook protection | ❌ | |
| Data connections / Power Query | ❌ | |
| Power Pivot / data model | ❌ | |
| Slicers / timelines | ❌ | |
| What-if (Goal Seek, Scenario, Solver) | ❌ | |
| VBA / macros / ActiveX | ❌ | Use Luau instead |
| Office Scripts / JS automation | ❌ | TypeScript scripting planned |
| Python in Excel | ❌ | |
| Ribbon / full desktop chrome | ❌ | Web toolbar subset |
| Offline desktop app | ❌ | Web + CLI only |
| Mobile-optimized UI | ❌ | |
| Co-authoring via OneDrive protocol | ❌ | Own P2P/CRDT stack |

### Planned beyond Excel (Cells-specific)

| Feature | Status | Notes |
|---------|--------|-------|
| Optional column types | ❌ Planned | [Type system](./type-system.md) |
| Relations / select fields | ❌ Planned | |
| Multi-language scripting (Python, TS) | ❌ Planned | README |
| Agent-as-collaborator architecture | 🟡 Prototype | Separate instances via CRDT |

---

## XLSX import / export

| Capability | Import | Export | Notes |
|------------|:------:|:------:|-------|
| Cell values | ✅ | ✅ | |
| Formulas (A1 ↔ UUID) | ✅ | ✅ | `_xlfn.` / `_xlpm.` stripped on import |
| Cached formula results | ✅ | ✅ | CLI `--eval` recomputes with Cells engine |
| Multiple sheets | ✅ | ✅ | |
| Column widths / row heights | ✅ | ✅ | |
| Fonts, fills, borders, alignment | ✅ | ✅ | Fidelity still tightening |
| Number formats | ✅ | ✅ | Custom codes; some precision edge cases |
| Merged cells | ✅ | ✅ | |
| Named ranges | ✅ | ✅ | |
| Themes / theme colors | 🟡 | 🟡 | Active work |
| Shared formulas | ✅ | 🟡 | |
| Array / dynamic array formulas | 🟡 | 🟡 | Spill model differs from classic CSE |
| Charts | ❌ | ❌ | Dropped on round-trip |
| Pivot tables | ❌ | ❌ | |
| Tables / autofilter | ❌ | ❌ | |
| Conditional formatting | ❌ | ❌ | |
| Data validation | ❌ | ❌ | |
| Comments | ❌ | ❌ | |
| Images / drawings | ❌ | ❌ | |
| VBA / macros | ❌ | ❌ | |
| Pivot cache / connections | ❌ | ❌ | |
| Sheet protection | ❌ | ❌ | |
| Print settings | ❌ | ❌ | |

Roundtrip infrastructure: `tests/excel-roundtrips/` and `tools/xlsx-roundtrip.sh`.

### Roundtrip categories

| Category | Role |
|----------|------|
| `math-basic` | Core arithmetic functions |
| `math-trig` | Trigonometry / hyperbolic |
| `logical` | Logic + `LET`/`LAMBDA` family |
| `text` | Text functions |
| `date-and-time` | Date/time functions |
| `statistical` | Stats functions |
| `lookup-and-reference` | Lookup family |
| `conditional-aggregates` | `SUMIF(S)`, `COUNTIF(S)`, … |
| `dynamic-arrays` | Spill / array functions |
| `financial` | Finance functions |
| `database` | Database functions |
| `engineering` | Engineering functions |
| `information` | Info / type functions |
| `nested-formulas` | Nested expression stress |
| `i18n` | Locale / RTL samples |
| `legacy-indexed-colors` | Legacy palette colors |

---

## UI / product parity (web app)

| Excel-like UX | Status |
|---------------|--------|
| In-cell editing | ✅ |
| Formula bar | ✅ |
| Formula autocomplete | ✅ |
| Colored formula references | ✅ |
| Spill range highlight | ✅ |
| Grid lines / headers | ✅ |
| Sheet tabs | ✅ |
| Context menu | ✅ |
| Formatting toolbar | ✅ |
| Border picker | ✅ |
| Number format controls | ✅ |
| Named ranges UI | ✅ |
| Zoom controls | ✅ |
| Theme toggle (light/dark app chrome) | 🔵 |
| Script editor / console | 🔵 |
| Collab room URL / presence | 🔵 |
| Insert chart wizard | ❌ |
| Pivot UI | ❌ |
| Filter arrows on headers | ❌ |
| Status bar aggregates | ❌ |
| Ribbon customization | ❌ |

---

## How to update this document

1. **New function:** implement in `core/cells/functions/fn_*.cc`, add unit tests, move the function from *Missing* to *Implemented*, bump the count in the scorecard and README-linked blurb.
2. **New product feature:** update the feature tables and XLSX matrix.
3. **Roundtrip green:** note the category under Roundtrip corpus coverage.
4. **Keep formula-engine.md in sync** for engine architecture; keep **this file** as the parity tracker (function lists + product gaps).

Regenerate project size stats separately via `./tools/generate-stats.sh --update` → [Project Stats](./project-stats.md).
