Status: COMPLETED
Created At: 2026-01-04 06:59 UTC
Updated At: 2026-01-05 06:30 UTC
Following plan management guidelines defined in AGENTS.md

## Commands

| Task | Command |
|------|---------|
| Build | `make build` |
| WASM Build | `make wasm-dist` |
| Unit tests | `make test` |
| E2E tests | `cd apps/wasm && npm run test:parallel -- stable` |
| Lint | `make lint` |
| Format | `make format` |
| Full check | `make check` |

---

# Format Management & Refresh Architecture Plan

## Problem Summary

### Format Issues
1. **Format selector not updating**: When typing "15%" in a cell, the format dropdown doesn't show "Percentage"
2. **Formula bar shows raw value**: Shows "0.15" instead of "15%" for percentage-formatted cells
3. **Top bar UI layout**: Needs redesign for better organization

### Refresh Issues
4. **Script-set formulas don't refresh dependents**: `setCell("A1", "=SUM(B1:B3)")` via Luau script doesn't trigger recalculation when B1 changes. This is a critical architecture bug.

---

## Architecture Analysis

### Current Format Flow
1. User types "15%" → `updateCellWithFormatDetection()`
2. C++ `input_parser.cc` detects percentage → stores `0.15` with `FMT_P000`
3. `notifyListeners()` → JS receives "cell" change event
4. `handleDataChanged()` → `fetchViewport()` → `updateFormulaBar()`
5. `formatControlsRef.updateForCurrentCell()` reads `cellData.formatId`

**Root Cause (Format)**: The viewport query returns cells, but we need to verify `formatId` is properly included in the viewport response and propagated to the UI.

### Current Refresh Flow - Script Formula Issue

**The Problem**: When `setCell("A1", "=SUM(B1:B3)")` is called via Luau script and B1:B3 don't exist:

1. `luau_sandbox.cc` calls `RefConverter::formulaToUuid(str)` (line 415)
2. `formulaToUuid()` tries to convert A1 refs to UUID refs
3. `formatUuidRef()` looks up column/row positions in `indexToColId_`/`indexToRowId_` (lines 244-245)
4. **If column/row doesn't exist, returns empty string** (line 247)
5. When UUID ref is empty, **the A1 notation is preserved as-is** in the formula
6. Formula is stored as `"=SUM(B1:B3)"` (A1 notation), not UUID format
7. **No cells are created for B1, B2, B3**
8. **No dependencies are registered** (dependencies require cell UUIDs)
9. Evaluation fails with #REF! because cells don't exist
10. Later creating B1, B2, B3 doesn't trigger recalculation (no dependency link)

**Contrast with UI path (updateCell in bindings.cc)**:
- Uses `FormulaResolver` which calls `getOrCreateColumnByPosition()` and `getOrCreateCellAt()`
- **Creates cells at referenced positions** with permanent UUIDs
- Dependencies ARE registered with those UUIDs
- But still has the UUID-based dependency issue if cells are recreated

**Root Cause (Refresh)**: Two issues:
1. `RefConverter::formulaToUuid()` doesn't create cells for non-existent references (unlike `FormulaResolver`)
2. Dependencies are tracked by cell UUID, not by position - so position-based A1 references don't create dependency links

---

## Design Decisions

### Format Architecture
1. **Formula bar display**: Show formatted value (like Google Sheets), not raw value (like Excel). When editing, show raw value with format suffix (e.g., editing "15%" shows "15%" not "0.15").
2. **Format selector update**: Ensure viewport response includes `formatId` and UI reads it correctly.
3. **Two-line formula bar**: Separate cell reference/format controls from the actual formula input for clarity.

### Refresh Architecture
1. **Centralize recalculation**: Move recalculation trigger from individual entry points (`updateCell`, `setCell`) to a lower level. Options:
   - **Option A**: Add recalculation to `applyOperation()` for cell value operations
   - **Option B**: Add recalculation to `setCell()` in LuauSandbox (minimal change)
   - **Chosen: Option B first**, then evaluate if a more centralized approach is needed

2. **Event-driven refresh**: UI refresh should be driven purely by `notifyListeners()` callbacks from C++. The UI should not independently decide when to refresh.

3. **Granular notifications**: Currently all changes send "cell" type. Consider adding cell ID to notifications for partial refreshes (future optimization).

---

## Phase 1: Fix Script-Set Formula Refresh (Critical Bug)

This is the most critical issue - formulas set via scripts don't update when dependencies change.

**The core problem**: `RefConverter::formulaToUuid()` doesn't create cells for non-existent references, so formulas with A1 notation are stored without UUID resolution, and no dependencies are registered.

**Solution**: Use `FormulaResolver` in Luau's setCell() instead of `RefConverter`, since FormulaResolver properly creates cells at referenced positions.

- [x] 1a: Refactor LuauSandbox setCell() to use FormulaResolver for formulas
  - When value starts with '=', parse the formula with `parseFormula()`
  - Use `FormulaResolver::resolve()` to convert A1 refs to UUIDs AND create cells
  - This ensures referenced cells exist and have UUIDs for dependency tracking
  - Serialize the resolved formula to UUID format for storage

- [x] 1b: Add recalculation after setCell() operations
  - After `applyOperation()`, call `markDirty()` and `cells::recalculate()` for formula cells
  - This matches the behavior in `bindings.cc` `updateCell()`
  - For non-formula cells, also trigger recalc of dependents (if the cell is referenced by other formulas)

- [x] 1c: Add dependency graph update after formula operations
  - After setting a formula, call `dependencyGraph.addFormula(cellId, ast)` to register dependencies
  - This is currently done in `bindings.cc` but missing from Luau path

- [x] 1d: Add C++ unit tests for script formula resolution
  - Test: `setCell("A1", "=B1+C1")` where B1, C1 don't exist → cells are created
  - Test: After creation, `setCell("B1", 10)` → A1 recalculates
  - Test: Dependency graph contains correct entries

- [x] 1e: Add E2E test for script-set formula refresh
  - Script sets formula `=SUM(B1:B3)`, verifies #REF! doesn't appear
  - Script then sets B1, B2, B3, verifies formula shows correct sum
  - Verify UI updates reflect the recalculation
  - Added tests/script-refresh.test.mjs with 7 tests

## Phase 2: Fix Format Selector Update

- [x] 2a: Verify formatId is included in viewport response
  - Added `formatId` field to viewport cell JSON output in `bindings.cc`
  - Cells now include formatId when a format is applied (e.g., `"formatId":"FMT_P000"`)

- [x] 2b: Fix format selector to read formatId correctly
  - Fixed `client.ts` `getAvailableFormats()` to extract `.formats` array from response
  - Fixed `format-controls.ts` to convert lowercase C++ categories to uppercase TypeScript types
  - Fixed `getCategoryForFormatId()` and `getFormatIdForCategory()` for case-insensitive comparison
  - Fixed `helpers.mjs` `getCellDisplayValue()` to prefer `display` over `value` for formatted cells

- [x] 2c: Add E2E test for format selector update
  - Added 4 tests to `format.test.mjs`:
    - Type "15%" → dropdown shows "Percent"
    - Type "$100" → dropdown shows "Currency"
    - Type "42" → dropdown shows "General"
    - Type "Hello" → dropdown shows "General"
  - All 12 format tests pass

## Phase 3: Formula Bar Shows Formatted Value

- [x] 3a: Add formatCellValue method to get formatted display
  - The C++ `formatCellById()` method already exists and works
  - Viewport response already includes `display` field for formatted cells
  - Returns "15%" for a cell with value 0.15 and PERCENTAGE format

- [x] 3b: Update formula bar to show formatted value when not editing
  - Added `getFormulaBarValue()` helper to `init.ts`
  - When viewing (not editing), shows formatted value via `cell.display`
  - Formula cells show formula, formatted cells show display, others show raw value

- [x] 3c: Handle formula cells specially
  - For formula cells, shows the formula (e.g., "=A1*2") via `cell.formula`
  - Only formatted non-formula cells show display value

- [x] 3d: Add E2E test for formula bar formatted display
  - Updated existing format tests to verify formula bar shows formatted value
  - Type "15%", press Enter → formula bar shows "15%"
  - Type "=A1+A2", press Enter → formula bar shows "=A1+A2" (formula)
  - All 12 format tests pass

## Phase 4: Top Bar UI Redesign

- [x] 4a: Restructure formula bar HTML for single-row layout
  - Left side: Cell reference (A1:A2), Formula input (flexible width)
  - Right side: Format controls (2x2 grid) + Script button
  - Removed % button (redundant with dropdown - select Percentage from dropdown)
  - Format controls arranged: [Format dropdown][Currency $] / [Decimal-][Decimal+]

- [x] 4b: Update CSS for new layout
  - Formula bar uses single-row flexbox layout
  - Format controls use CSS Grid (2x2: dropdown/$, decimal-/decimal+)
  - Format separator between format controls and script button

- [x] 4c: Update TypeScript references
  - Removed formatPercentBtn from DOMElements interface and getter
  - Removed percentBtn from FormatControls config
  - All format E2E tests pass (12/12)

- [x] 4d: Update format-controls.ts for new layout
  - Removed percentBtn member, event listener, and active state handling
  - Currency button active state still highlights when currency format applied

## Phase 5: Comprehensive Testing

- [x] 5a: Add format detection unit tests (C++)
  - Already covered in `input_parser_test.cc` (tests for %, $, 1.5E6, dates, numbers)
  - Verify correct format IDs are assigned

- [x] 5b: Add format display unit tests (C++)
  - Already covered in `number_formatter_test.cc` (tests formatting various values)
  - Verify output strings match expectations

- [x] 5c: Add comprehensive E2E format tests
  - Already covered in `format.test.mjs` (full workflow tests)
  - Test switching formats via dropdown
  - Test decimal increase/decrease

- [x] 5d: Add E2E refresh tests
  - Already covered in `script-refresh.test.mjs`
  - Test manual edit refreshes dependents
  - Test script edit refreshes dependents
  - Test chain of dependencies (A1 → B1 → C1)

## Phase 6: Percentage Literals in Formulas

Support percentage notation directly in formulas (e.g., `=1000*15%` evaluates to 150).

- [x] 6a: Update formula lexer to recognize percentage literals
  - Added `PERCENT_LITERAL` token type for numbers followed by `%`
  - Added `percentValue()` method to Token class
  - `15%` tokenizes as `PERCENT_LITERAL` with value 0.15

- [x] 6b: Update formula parser to handle percentage literals
  - Parse `PERCENT_LITERAL` as a number node with the decimal value
  - `15%` → NumberNode(0.15)

- [x] 6c: Add unit tests for percentage in formulas
  - Added 9 lexer tests in `formula_lexer_test.cc`
  - Added 6 parser tests in `formula_parser_test.cc`
  - Added 8 evaluation tests in `formula_eval_test.cc`
  - Tests cover: 15%, 12.5%, 100%, 0%, 1000*15%=150, 50%+25%=0.75, -15%, A1*15%

- [x] 6d: Add E2E tests for percentage formulas
  - Added 4 tests to `format.test.mjs`:
  - Enter `=1000*15%`, verify result is 150
  - Enter `=50%+25%`, verify result is 0.75
  - Enter `=15%`, verify result is 0.15
  - Enter `=A1*15%` with A1=100, verify result is 15

---

## Files to Modify

### C++ (Phase 1, 2a, 3a, 5a-b, 6a-c)
- `core/cells/luau_sandbox.cc` - Use FormulaResolver, add recalculation, add dependency registration
- `core/cells/luau_sandbox.h` - May need to add FormulaResolver/DependencyGraph access
- `core/cells/luau_sandbox_test.cc` - Tests for script formula resolution and recalculation
- `core/cells/bindings.cc` - Verify viewport includes formatId
- `core/cells/number_formatter_test.cc` - Format display tests
- `core/cells/input_parser_test.cc` - Format detection tests
- `core/cells/formula_lexer.cc` - Add percentage literal token (Phase 6)
- `core/cells/formula_parser.cc` - Parse percentage literals (Phase 6)
- `core/cells/formula_parser_test.cc` - Tests for percentage in formulas (Phase 6)

### TypeScript (Phase 2b, 3b-c, 4c-d)
- `apps/wasm/src/init.ts` - Formula bar formatted display
- `apps/wasm/src/format-controls.ts` - Format selector fix + UI updates
- `apps/wasm/src/types.ts` - Verify CellData.formatId type

### HTML/CSS (Phase 4a-b)
- `apps/wasm/static/index.html` - Two-line formula bar structure
- `apps/wasm/static/shared/styles.css` - New layout styles

### Tests (Phase 1b, 2c, 3d, 5c-d)
- `apps/wasm/tests/format.test.mjs` - New format E2E tests
- `apps/wasm/tests/script-refresh.test.mjs` - New script refresh E2E tests

---

## Risk Assessment

1. **Formula bar change (showing formatted vs raw)**: This changes user expectations. Users might expect to see "0.15" to know the actual stored value. Mitigated by showing raw value when editing.

2. **Script recalculation performance**: Adding recalculation to every `setCell()` could slow down scripts that set many cells. Mitigated: Consider batching - if multiple `setCell()` calls happen in one script execution, only recalculate once at the end.

3. **UI redesign**: Layout changes could break existing tests. All E2E tests should be run after Phase 4.

---

## Success Criteria

1. Typing "15%" shows "Percentage" in format dropdown
2. Formula bar shows "15%" for percentage cell (when not editing)
3. Scripts that set formulas correctly update when dependencies change
4. New two-line formula bar layout is clean and functional
5. All existing tests continue to pass
6. New tests provide coverage for the fixed behaviors
7. Formulas support percentage literals (e.g., `=1000*15%` → 150)
