# Logical Roundtrip Tests

Pass `./run-test.sh logical` by implementing missing functions and fixing existing logic bugs. Currently 546 differences across 1628 cells.

## Observed Differences

Running the test produces 546 differences from these root causes:

### 1. Missing function: NA() (~49 diffs cascading)
C13 uses `NA()` to generate #N/A error. Without it, C13 evaluates to #NAME?, and all formulas referencing C13 (AND/OR/XOR/IF with #N/A input) produce wrong results.

### 2. AND/OR bugs with text, empty, and error propagation (~30 diffs)
Two bugs in `fn_AND` and `fn_OR`:
- **Text/empty skipping**: Excel's AND/OR skip text values and empty cells in direct args (same as ranges). Our code only skips them in the range branch — direct args try `toBoolean()` on text → #VALUE!, and on empty → FALSE.
- **Short-circuit before error check**: Our AND returns FALSE immediately when it finds a FALSE arg, without evaluating remaining args. Excel evaluates all args and propagates errors even when short-circuit value is found. e.g. `AND(FALSE, #N/A)` should return #N/A, not FALSE. Similarly OR(TRUE, #N/A) should return #N/A, not TRUE.

### 3. Missing function: XOR (~121 diffs)
`_xlfn.XOR(a, b, ...)` — exclusive OR. Returns TRUE if an odd number of args are TRUE. Same text/empty/error handling as AND/OR.

### 4. Missing function: SWITCH (~16 formulas + cascading verification cells)
`_xlfn.SWITCH(expr, val1, result1, val2, result2, ..., [default])` — matches expression against values and returns corresponding result. If no match and no default, returns #N/A.

### 5. Missing function: IFS (~14 formulas + cascading)
`_xlfn.IFS(cond1, val1, cond2, val2, ...)` — returns value for first TRUE condition. If no condition is TRUE, returns #N/A.

### 6. Missing _xlpm. prefix handling + LET/LAMBDA (~30 formulas + cascading)
XLSX formulas use `_xlpm.varname` for LET/LAMBDA parameter names. The XLSX reader strips `_xlfn.` but not `_xlpm.`, causing formulas like `LET(_xlpm.x, 10, _xlpm.x*2)` to parse incorrectly (dot terminates identifier). Need to strip `_xlpm.` prefix during import, then implement:
- **LET(name1, val1, [name2, val2, ...], calculation)** — defines named variables, returns calculation result
- **LAMBDA([param1, ...], body)(args)** — creates anonymous function, immediately invoked

---

## Phase 1: Fix AND/OR Behavior

Fix the two bugs in `fn_AND` and `fn_OR` in `core/cells/functions/fn_logic.cc`:

- [x] 1a: Fix text/empty skipping in direct args AND short-circuit error propagation. Rewrote both fn_AND and fn_OR to evaluate all args first, skip text/empty in both direct and range args, collect first error, and propagate errors over boolean results.

## Phase 2: Implement NA() and XOR

- [x] 2a: Implement `NA()` (zero args, returns #N/A) and `XOR(logical1, ...)` (exclusive OR — returns TRUE if odd count of TRUE values, same text/empty/error handling as fixed AND/OR). Register both in `fn_logic.cc`. Added unit tests for both functions in formula_error_test.cc.

## Phase 3: Implement SWITCH and IFS

- [x] 3a: Implement `SWITCH(expression, value1, result1, ..., [default])` — evaluates expression, compares against each value (case-insensitive for strings), returns matching result. With default (odd remaining args after expression), returns default when no match; without default, returns #N/A. If expression or case value is error, propagate it.
- [x] 3b: Implement `IFS(condition1, value1, condition2, value2, ...)` — evaluates conditions in order, returns value for first TRUE condition. Must have even number of args (pairs). Returns #N/A if no condition is TRUE. Error in condition propagates. Added unit tests for both SWITCH and IFS.

## Phase 4: Strip _xlpm. prefix and implement LET + LAMBDA

- [x] 4a: Extend `stripXlfnPrefix` in `xlsx_reader.cc` to also strip `_xlpm.` prefix from parameter names (e.g., `_xlpm.x` → `x`, `_xlpm.name` → `name`). Strip _xlpm. before _xlfn. since parameter names don't need dot normalization.
- [ ] 4b: Implement `LET(name1, value1, [name2, value2, ...], calculation)` — binds named variables and evaluates the final expression with those bindings. Requires a variable scope mechanism in the eval context.
- [ ] 4c: Implement `LAMBDA([param1, param2, ...], body)(args...)` — creates a callable that binds args to params and evaluates body. Invoked immediately with trailing `(args)`.

## Phase 5: Run Roundtrip Test and Fix Remaining Issues

- [ ] 5a: Run `./run-test.sh logical` and investigate any remaining differences.
- [ ] 5b: Fix any remaining differences.
- [ ] 5c: Add `logical` to `ENABLED_CATEGORIES` in `tools/xlsx-roundtrip.sh` and verify `bazel run :xlsx-roundtrip` passes all three categories.
