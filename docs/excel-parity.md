# Excel Parity Tracker

Living status of Cells vs Microsoft Excel: formulas, spreadsheet features, file I/O, and UI.

**Last reviewed:** 2026-09-01  
**Function count:** **348** registered (Excel has ~484 worksheet functions ≈ **72%**; dotted/underscore aliases share one implementation)  
**Source of truth for implementations:** `core/cells/functions/fn_*.cc`

Update this document when adding functions, shipping features, or changing XLSX fidelity. Cross-check the function list against the registry if counts drift.

---

## Scorecard

| Area | Status | Notes |
|------|--------|-------|
| Formula engine (parse / eval / deps / spill) | ✅ Solid | AST-native C++; UUID-stable refs |
| Function library breadth | 🟡 Partial | 348 / ~484 Excel functions (aliases included) |
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
| Math & trig | 89 | ~80 | Core + trig + combinatorics + SUMX* + GAMMA |
| Logic / information (subset) | 29 | ~40 | Strong modern set (`LET`, `LAMBDA`, `IFS`, …) plus ISERR/TYPE/N |
| Text | 33 | ~50 | Core string ops + UNICHAR/DOLLAR/FIXED + byte aliases |
| Date / time | 23 | ~40 | Core extractors + DAYS360/YEARFRAC/ISOWEEKNUM |
| Statistics | 56 | ~100 | Basic + regression/correlation + percentrank + skew/kurt/normal |
| Array / dynamic | 5 | ~25 | Core spill set only |
| Lookup & reference | 14 | ~40 | Classic + ROW/COLUMN/ADDRESS/CHOOSE + SHEET/AREAS/HYPERLINK |
| Financial | 24 | ~55 | Closed-form annuity, depreciation, T-bill (no RATE/IRR) |
| Engineering | 34 | ~50 | Bitwise, base conversion, ERF, COMPLEX, IM* |
| Database | 0 | ~12 | Entire category missing |
| Cube / web / other | 0 | many | Out of scope for now |
| **Total registered** | **348** | **~484** | **~72%** (includes dotted/underscore aliases) |

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

### Implemented functions (348 registered names)

Both Excel dotted names and underscore/concat aliases resolve to one implementation. Registered names historically used underscores where Excel uses dots (e.g. `CEILING.MATH` → `CEILING_MATH` on XLSX import via `_xlfn.` stripping). Some aliases omit the underscore (`STDEVS` for `STDEV.S`, `PERCENTILEINC` for `PERCENTILE.INC`).

#### Math (89 unique + dotted rounding aliases)

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
| `FLOOR_MATH` / `CEILING_MATH` / `FLOOR.MATH` / `CEILING.MATH` | Round to significance |
| `FLOOR_PRECISE` / `CEILING_PRECISE` / `ISO_CEILING` / dotted forms | Precise/ISO significance (abs) |
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
| `COMBIN` / `COMBINA` / `PERMUT` / `PERMUTATIONA` | Combinatorics |
| `BASE` / `DECIMAL` | Radix conversion |
| `ARABIC` / `ROMAN` | Roman numerals |
| `MULTINOMIAL` / `SERIESSUM` | Multinomial coefficient / power series |
| `SUMX2MY2` / `SUMX2PY2` / `SUMXMY2` | Paired-array square sums |
| `GAMMA` / `GAMMALN` / `GAMMALN.PRECISE` | Gamma function and ln(Γ) |

#### Logic (29)

| Function | Description |
|----------|-------------|
| `IF` / `IFS` / `SWITCH` | Conditionals |
| `AND` / `OR` / `XOR` / `NOT` | Boolean combinators |
| `IFERROR` / `IFNA` / `NA` | Error handling |
| `LET` / `LAMBDA` | Named locals and anonymous functions |
| `TRUE` / `FALSE` | Boolean constants |
| `EXACT` | Case-sensitive string equality |
| `ISBLANK` / `ISNUMBER` / `ISTEXT` / `ISERROR` / `ISLOGICAL` / `ISNA` | Type tests |
| `ISERR` / `ISNONTEXT` / `ISEVEN` / `ISODD` / `ISREF` | Additional predicates |
| `TYPE` / `N` / `ERROR.TYPE` | Type code, numeric coerce, error code |

#### Text (33 registered, including LENB/LEFTB/… Unicode aliases)

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
| `UNICHAR` / `UNICODE` | Unicode code points |
| `DOLLAR` / `FIXED` / `NUMBERVALUE` | Numeric text formatting / locale parse |
| `LENB` / `LEFTB` / `RIGHTB` / `MIDB` / `FINDB` / `SEARCHB` / `REPLACEB` | Byte-count aliases of the Unicode implementations |

#### Lookup (14)

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
| `AREAS` | Number of areas in a reference |
| `SHEET` / `SHEETS` | Sheet index / sheet count |
| `HYPERLINK` | Display text (or URL) of a hyperlink |

#### Date / time (23)

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
| `DAYS360` | | Days on a 360-day year |
| `YEARFRAC` | | Fraction of a year between dates |
| `ISOWEEKNUM` | | ISO 8601 week number |

#### Statistics (56)

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
| `AVEDEV` / `DEVSQ` / `GEOMEAN` / `HARMEAN` | Deviation and mean variants |
| `STANDARDIZE` / `FORECAST` / `FORECAST.LINEAR` | Z-score and linear forecast |
| `SLOPE` / `INTERCEPT` / `PEARSON` / `CORREL` / `RSQ` | Linear regression |
| `COVAR` / `COVARIANCE.P` / `COVARIANCE.S` | Covariance |
| `QUARTILE.EXC` / `RANK.AVG` | Exclusive quartile / average rank |
| `AVERAGEA` / `MINA` / `MAXA` | Aggregates treating text/logicals |
| `PERCENTRANK` / `PERCENTRANK.INC` / `PERCENTRANK.EXC` | Percent rank |
| `FISHER` / `FISHERINV` | Fisher transformation |
| `PHI` / `GAUSS` / `NORMSDIST` / `NORM.S.DIST` / `NORMDIST` / `NORM.DIST` | Standard/normal PDF and CDF |
| `SKEW` / `SKEW.P` / `KURT` | Skewness and excess kurtosis |
| `STDEVA` / `STDEVPA` / `VARA` / `VARPA` | Stdev/variance treating text/logicals |
| `STEYX` | Standard error of linear regression |

#### Engineering (34)

| Function | Description |
|----------|-------------|
| `BITAND` / `BITOR` / `BITXOR` / `BITLSHIFT` / `BITRSHIFT` | 48-bit integer bitwise ops |
| `BIN2DEC` / `DEC2BIN` / `HEX2DEC` / `DEC2HEX` / `OCT2DEC` / `DEC2OCT` | Base conversion |
| `BIN2HEX` / `BIN2OCT` / `HEX2BIN` / `HEX2OCT` / `OCT2BIN` / `OCT2HEX` | Cross-base conversion |
| `DELTA` / `GESTEP` | Kronecker delta / step |
| `ERF` / `ERFC` / `ERF.PRECISE` / `ERFC.PRECISE` | Error function |
| `COMPLEX` | Real/imaginary to complex text |
| `IMABS` / `IMREAL` / `IMAGINARY` / `IMARGUMENT` / `IMCONJUGATE` | Complex parts |
| `IMSUM` / `IMSUB` / `IMPRODUCT` / `IMDIV` / `IMPOWER` | Complex arithmetic |

#### Financial (24)

| Function | Description |
|----------|-------------|
| `PV` / `FV` / `PMT` / `NPER` | Annuity present/future value, payment, periods |
| `NPV` | Net present value of cash flows |
| `SLN` / `SYD` / `DB` / `DDB` | Depreciation |
| `IPMT` / `PPMT` / `CUMIPMT` / `CUMPRINC` / `ISPMT` | Interest vs principal |
| `EFFECT` / `NOMINAL` | Effective / nominal rate conversion |
| `DOLLARDE` / `DOLLARFR` | Fractional dollar prices |
| `FVSCHEDULE` / `PDURATION` / `RRI` | Compounding helpers |
| `TBILLEQ` / `TBILLPRICE` / `TBILLYIELD` | Treasury bills |

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

#### Financial — remaining iterative / coupon / yield work

Closed-form annuity, depreciation, dollar-fraction, and T-bill functions are implemented (`PV`/`FV`/`PMT`/`NPER`/`NPV`, `SLN`/`SYD`/`DB`/`DDB`, `IPMT`/`PPMT`/`CUMIPMT`/`CUMPRINC`, `EFFECT`/`NOMINAL`, `TBILL*`, …). Still missing:

| Function | Notes |
|----------|-------|
| `RATE` / `IRR` / `MIRR` / `XIRR` | Iterative solvers |
| `XNPV` | Dated cash flows |
| `ACCRINT` / `COUP*` / `PRICE` / `YIELD` / `DURATION` | Bonds / coupons |

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
| `ISFORMULA` / `CELL` / `INFO` | Need extra cell/workbook metadata |

#### Database

| Function | Notes |
|----------|-------|
| `DSUM` / `DCOUNT` / `DCOUNTA` / `DAVERAGE` / `DMAX` / `DMIN` / `DGET` | |

#### Engineering

| Function | Notes |
|----------|-------|
| `CONVERT` | Unit conversion tables |
| `BESSEL*` | Special functions |
| Remaining `IM*` (`IMCOS`, `IMEXP`, …) | Trig/exp on complex text |

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
