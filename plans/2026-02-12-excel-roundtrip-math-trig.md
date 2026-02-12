# Fix Excel Round-Trip for math-trig

Fix all issues preventing `./run-test.sh math-trig` from passing. The test reads an XLSX file (with formulas, no cached results), evaluates formulas via the CLI, writes a new XLSX, and compares it cell-by-cell against the original (which has Excel-cached results).

## Observed Differences

Running the test produces 349 differences, all `#NAME?` errors from unimplemented functions. One cell (C151) also has a `numberFormat` difference (`"0.000000000000000"` → `"0.00"`).

### 1. Unimplemented trigonometric functions (349 cells)

All 349 diffs are `#NAME?` errors from functions not yet registered. The functions needed (by formula count):

- **ATAN2** — 81 cells — two-argument arctangent (note: Excel's arg order is `ATAN2(x, y)`, opposite of C's `atan2(y, x)`)
- **RADIANS** — 18 cells — convert degrees to radians
- **DEGREES** — 18 cells — convert radians to degrees
- **ATANH** — 18 cells — inverse hyperbolic tangent
- **ASINH** — 18 cells — inverse hyperbolic sine
- **ASIN** — 18 cells — inverse sine
- **ACOSH** — 18 cells — inverse hyperbolic cosine
- **ACOS** — 18 cells — inverse cosine
- **ATAN** — 18 cells — inverse tangent
- **TANH** — 9 cells — hyperbolic tangent
- **TAN** — 9 cells — tangent
- **SINH** — 9 cells — hyperbolic sine
- **SIN** — 9 cells — sine
- **SEC** — 9 cells — secant (`_xlfn.SEC`, needs `_xlfn.` prefix handling)
- **CSC** — 9 cells — cosecant (`_xlfn.CSC`, needs `_xlfn.` prefix handling)
- **COT** — 9 cells — cotangent (`_xlfn.COT`, needs `_xlfn.` prefix handling)
- **COSH** — 9 cells — hyperbolic cosine (also used in `1/COSH(...)` expressions)
- **COS** — 9 cells — cosine
- **PI** — 6 cells — returns π (also used in input value formulas like `PI()/6`)

### 2. Number format precision loss (1 cell)

Cell C151 has format `"0.000000000000000"` (15 decimal places). The writer maps all NUMBER category formats to built-in format IDs (1=`0`, 2=`0.00`), losing decimal precision beyond 2. Should generate a custom format code when decimals don't match a built-in format.

---

## Phase 1: Implement PI() and Core Trig Functions

Implement the foundational trig functions. PI is needed first since many input cells use `PI()/6`, `PI()/4`, etc.

- [x] 1a: Implement `PI()` — returns `M_PI` (3.14159265358979323846). Zero arguments required.
- [x] 1b: Implement `SIN`, `COS`, `TAN` — basic trig functions using `std::sin`, `std::cos`, `std::tan`. Single argument (radians). Apply `excelNormalize` to results. TAN returns #NUM! on overflow (inf).

## Phase 2: Implement Inverse and Reciprocal Trig Functions

- [ ] 2a: Implement `ASIN`, `ACOS`, `ATAN` — inverse trig using `std::asin`, `std::acos`, `std::atan`. Domain errors (e.g., `ASIN(2)`) return `#NUM!`.
- [ ] 2b: Implement `ATAN2(x_num, y_num)` — two-argument arctangent. Note: Excel's `ATAN2(x, y)` computes `atan2(y, x)` (args are reversed compared to C). `ATAN2(0, 0)` returns `#DIV/0!` in Excel.
- [ ] 2c: Implement `CSC`, `SEC`, `COT` — reciprocal trig functions: `1/sin(x)`, `1/cos(x)`, `cos(x)/sin(x)`. Return `#DIV/0!` when the denominator is zero. These arrive from XLSX as `_xlfn.CSC`, `_xlfn.SEC`, `_xlfn.COT` — the `_xlfn.` prefix stripping already handles removing the prefix (from math-basic work).

## Phase 3: Implement Hyperbolic Functions

- [ ] 3a: Implement `SINH`, `COSH`, `TANH` — hyperbolic functions using `std::sinh`, `std::cosh`, `std::tanh`. Overflow returns `#NUM!`.
- [ ] 3b: Implement `ASINH`, `ACOSH`, `ATANH` — inverse hyperbolic functions using `std::asinh`, `std::acosh`, `std::atanh`. Domain errors return `#NUM!` (`ACOSH` requires x >= 1, `ATANH` requires |x| < 1).

## Phase 4: Implement RADIANS and DEGREES

- [ ] 4a: Implement `RADIANS(angle)` and `DEGREES(angle)` — conversion between degrees and radians. `RADIANS(x) = x * PI / 180`, `DEGREES(x) = x * 180 / PI`.

## Phase 5: Fix Number Format Precision for High Decimal Counts

Cell C151 uses format `"0.000000000000000"` (15 decimal places). The writer maps NUMBER with any decimals >= 2 to built-in format ID 2 (`0.00`), losing the actual precision.

- [ ] 5a: Fix `getNumFmtId` in `xlsx_writer.cc` — for NUMBER category, only use built-in IDs when decimals exactly match (0 → ID 1, 2 → ID 2 with no thousands; 0 → ID 3, 2 → ID 4 with thousands). For other decimal counts, generate a custom format code (e.g., `"0.000000000000000"` for 15 decimals).

## Phase 6: Run Roundtrip Test and Fix Remaining Differences

- [ ] 6a: Run roundtrip test and investigate any remaining differences (precision, edge cases, etc.).
- [ ] 6b: Fix any remaining differences discovered.
- [ ] 6c: Add `math-trig` to `ENABLED_CATEGORIES` in `tools/xlsx-roundtrip.sh` and verify `bazel run :xlsx-roundtrip` passes both `math-basic` and `math-trig`.
