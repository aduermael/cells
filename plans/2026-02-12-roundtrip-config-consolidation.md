# Roundtrip Config Consolidation & Math Performance

## Context

The XLSX roundtrip comparison currently uses per-category `config.json` files (`data/math-basic/config.json`, `data/math-trig/config.json`). These should be consolidated into a single global config with clear, documented rules. The ULP tolerance should also specify the magnitude range where tolerance applies, since small ULP differences at large magnitudes are meaningless while at small magnitudes they could mask real bugs.

Additionally, since math-basic currently achieves exact bit-identical results through careful Excel-algorithm-matching code paths (three separate power algorithms, manual ATAN2 quadrant logic), accepting a small tolerance opens the door to simplifying these for performance.

## Current state

- `excelPow()` has 3 code paths: `powBySquaring` for negative base, `1/std::pow(x,n)` for negative integer exponents, `exp(y*log(x))` for non-integer exponents
- ATAN2 uses manual `atan(y/x)` + quadrant adjustment instead of `std::atan2`
- math-basic: 0 value differences, 94 formula-text differences (all pass with `ignoreFormulaText: true`)
- math-trig: 28 value differences within 2 ULP (pass with `numericToleranceUlp: 2`)

## Phase 1: Consolidate config to a single global file

- [ ] 1a: Move config to `tests/excel-roundtrips/config.json` with magnitude-aware tolerance

The single config applies to all roundtrip test categories:

```json
{
  "ignoreFormulaText": true,
  "numericTolerance": {
    "maxUlp": 2,
    "minMagnitude": 1e-100
  }
}
```

`numericTolerance.minMagnitude` means: only apply ULP tolerance to values whose absolute value is >= `minMagnitude`. Below that threshold, require exact equality. This prevents tolerating differences in small numbers where a 2-ULP gap could represent a proportionally large error.

Rationale for `1e-100`: all observed ULP differences are in trig/power results at normal magnitudes (e.g., `SIN(PI/6)` ≈ 0.5, `42.5^42.5` ≈ 1e69). Values near zero (like subnormals near 1e-308) are already handled by `excelNormalize` flushing to zero.

- [ ] 1b: Update `run-test.sh` to use global config from `tests/excel-roundtrips/config.json`

Remove per-category config auto-detection. Always pass the single global config.

- [ ] 1c: Update `compare.sh` to accept config path (already done, no changes needed — just verify)

- [ ] 1d: Update C# `ComparisonConfig` and `AreCellsEqualWithinUlp` to support `minMagnitude`

Add `NumericToleranceMinMagnitude` (double, default 0) to the config class. In `AreCellsEqualWithinUlp`, check `Math.Abs(d1) >= minMagnitude && Math.Abs(d2) >= minMagnitude` before applying ULP tolerance.

- [ ] 1e: Delete per-category config files (`data/math-basic/config.json`, `data/math-trig/config.json`)

## Phase 2: Simplify math for performance

With 2-ULP tolerance accepted globally, the three-path `excelPow` and manual ATAN2 can be simplified to use standard library functions.

- [ ] 2a: Simplify `excelPow` to use `std::pow` for all cases

Replace the three-path logic with:
```cpp
inline double excelPow(double base, double exponent) {
    return std::pow(base, exponent);
}
```

Keep the edge-case handling in the `BinaryOp::POWER` caller (0^0, 0^-n, base=1, extreme exponents, overflow→#NUM!) — only the core computation changes.

Verify against math-basic and math-trig test suites. If any differences exceed 2 ULP, investigate individually.

- [ ] 2b: Simplify ATAN2 to use `std::atan2`

Replace the manual quadrant logic with:
```cpp
result = std::atan2(y, x);
```

Keep the `(x==0 && y==0) → #DIV/0!` guard.

- [ ] 2c: Run full roundtrip suite to verify all tests pass within tolerance

Run `bazel run :xlsx-roundtrip` and confirm math-basic and math-trig both pass.
