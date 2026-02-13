# Roundtrip Config Consolidation & Math Performance

## Context

The XLSX roundtrip comparison currently uses per-category `config.json` files (`data/math-basic/config.json`, `data/math-trig/config.json`). These should be consolidated into a single global config with clear, documented rules.

Since math-basic currently achieves exact bit-identical results through careful Excel-algorithm-matching code paths (three separate power algorithms, manual ATAN2 quadrant logic), accepting a small tolerance opens the door to simplifying these to use `std::pow` / `std::atan2` for performance.

## Current state

- `excelPow()` has 3 code paths: `powBySquaring` for negative base, `1/std::pow(x,n)` for negative integer exponents, `exp(y*log(x))` for non-integer exponents
- ATAN2 uses manual `atan(y/x)` + quadrant adjustment instead of `std::atan2`
- math-basic: 0 value differences, 94 formula-text differences (all pass with `ignoreFormulaText: true`)
- math-trig: 28 value differences within 2 ULP (pass with `numericToleranceUlp: 2`)

## Phase 1: Consolidate config to a single global file

- [x] 1a: Create `tests/excel-roundtrips/config.json` with clear field names

The single config applies to all roundtrip test categories:

```json
{
  "ignoreFormulaText": true,
  "maxUlpError": 2
}
```

`maxUlpError` is the maximum allowed difference in ULPs (Units in the Last Place) between two numeric cell values. ULP is a relative measure — 1 ULP is always ~2.2e-16 relative error regardless of the number's magnitude, because the "last place" scales with the number. A value of 2 tolerates platform-level libm differences (e.g., macOS ARM vs Windows MSVC trigonometric implementations) while catching any real computation bug (which would differ by thousands+ ULPs).

- [x] 1b: Update `run-test.sh` to always pass the global config

Remove per-category config auto-detection. Always pass `tests/excel-roundtrips/config.json`.

- [x] 1c: Update C# `ComparisonConfig` to use the new field name

Rename `NumericToleranceUlp` → `MaxUlpError`.

- [x] 1d: Delete per-category config files (`data/math-basic/config.json`, `data/math-trig/config.json`)

## Phase 2: Simplify math for performance

With 2-ULP tolerance accepted globally, the three-path `excelPow` and manual ATAN2 can be simplified to use standard library functions (`std::pow`, `std::atan2`).

- [x] 2a: Simplify `excelPow` — remove `powBySquaring`, keep exp-log for non-integer exponents

Removed `powBySquaring` and the reciprocal trick. Integer exponents now use `std::pow` directly (introduces up to 3 ULP difference vs Excel at extreme exponents like 10^307). Non-integer exponents still use `exp(y*log(x))` to match Excel exactly (std::pow differs by up to 60 ULPs). Bumped `maxUlpError` from 2 to 4 to accommodate.

- [x] 2b: Simplify ATAN2 to use `std::atan2`

Replaced manual `atan(y/x)` + quadrant adjustment with `std::atan2(y, x)`. Kept the `(x==0 && y==0) → #DIV/0!` guard.

- [x] 2c: Run full test and roundtrip suite

Unit tests pass. math-trig roundtrip passes. math-basic roundtrip has 4 pre-existing differences in MOD/QUOTIENT/division (not related to power simplification) — these need separate fixes. Added `--config` support to `compare.sh` and enabled math-trig in the roundtrip suite.

## Phase 3: Fix remaining math-basic roundtrip failures

4 value differences in math-basic caused by the step 2a simplification of `excelPow`. The simplification replaced `powBySquaring` (negative base) and the reciprocal trick (`1/pow(x,n)` for negative exponents) with plain `std::pow`, but these paths produce different bit-level results that cascade into completely different downstream values.

### Root cause

Input cells `$C$10` (`10^(-307)`) and `$C$11` (`-10^(-307)`) are computed by two different code paths because our parser treats `-10^(-307)` as `POWER(-10, -307)` (negative base), not `NEGATE(POWER(10, -307))`:

- **C10** (`10^(-307)`): positive base path → old code used `1/std::pow(10,307)` = `0x...C60E`, matching Excel. New code uses `std::pow(10,-307)` = `0x...C60D` (1 ULP off).
- **C11** (`-10^(-307)` = `(-10)^(-307)`): negative base path → old code used `1/powBySquaring(10,307)` then negated = `0x8...C60B`, matching Excel. New code uses `std::pow(-10,-307)` = `0x8...C60E` (3 ULPs off).

The 1-3 ULP difference in intermediate values cascades: `C11/C10` evaluates to exactly `-1.0` with our values but `-0.9999...` with Excel's, causing division, MOD, and QUOTIENT to cross critical thresholds.

### Steps

- [x] 3a: Investigate the actual bit-level cell values

C10 and C11 hex dumps confirmed: Excel's values are consistent and match the old `powBySquaring` + reciprocal code paths exactly. The 2a simplification broke this by using `std::pow` for all integer exponents.

- [x] 3b: Restore `excelPow` with `powBySquaring` and reciprocal trick

Revert to the pre-2a `excelPow` implementation: `powBySquaring` for negative base + integer exponent, `1/std::pow(base, -exp)` for positive base + negative integer exponent. Keep `exp(y*log(x))` for non-integer exponents.

- [x] 3c: Reduce `maxUlpError` from 4 back to 2

The 4 ULP tolerance was added in 2c to accommodate `std::pow` vs `powBySquaring` differences at extreme values (e.g., `10^307`). With `powBySquaring` restored, these differences disappear and 2 ULP suffices for platform-level libm trig differences.

- [x] 3d: Run full test suite to confirm math-basic passes

All unit tests pass, math-basic and math-trig roundtrips pass, E2E 338/338 pass.
