Status: IN_PROGRESS
Created At: 2025-12-28 06:49 UTC
Updated At: 2025-12-28 14:35 UTC
Following plan management guidelines defined in AGENTS.md

# Export, CRDT Operations, and UX Fixes

This plan addresses six interconnected issues, ordered by increasing complexity (with testing infrastructure first):

1. **Programmatic testing** - Add Lightpanda integration for automated web UI testing
2. **Formula preservation in exports** - Formulas exported as values instead of formula text
3. **Document title editing** - Allow editing Sheet1 title, use snake_case for export filename
4. **Collaboration lag** - Operations potentially piling up during sync
5. **XLSX loading issues** - stress_test.xlsx not displayed correctly (empty rows on scroll)
6. **Axis operation refactoring** - Split DIM_INSERT_AXIS → COL_INSERT/ROW_INSERT (and similar)

---

## Phase 1: Programmatic Testing with Lightpanda ✅ COMPLETE

**Goal**: Enable automated testing of the web UI to iterate faster on features and catch regressions.

### Why Lightpanda?

- **10x faster** than Chrome headless
- **10x less memory** than Chrome
- **Instant startup** - no browser spinup delay
- **CDP compatible** - works with Puppeteer and Playwright
- **JavaScript execution** - can interact with dynamic web apps

### Tasks

- [x] 1a: Install Lightpanda npm package (`@lightpanda/browser`) and puppeteer-core
- [x] 1b: Create test harness that starts local dev server + Lightpanda
- [x] 1c: Add helper functions for common operations (setCellValue, getCellValue, selectCell, etc.)
- [x] 1d: Write basic smoke test: create workbook, set values, export, verify
- [x] 1e: Add formula test: enter formula, verify computed result
- [x] 1f: Add collaboration test: two browser contexts, verify sync
- [x] 1g: Document how to run programmatic tests in README

### Implementation Notes

- Switched from Lightpanda to Chrome headless (Lightpanda has limited canvas 2D support)
- Smoke tests: 6/6 PASS
- Formula tests: 5/5 PASS
- Collaboration tests: 1/4 PASS (WebRTC limitations in headless Chrome - marked experimental)
- Added `HEADED=1` and `SLOWMO=100` env vars for debugging

### Example Test Structure

```typescript
// tests/e2e/smoke.test.ts
import { lightpanda } from '@lightpanda/browser';
import puppeteer from 'puppeteer-core';

describe('Cells Smoke Test', () => {
  let browser, page;

  beforeAll(async () => {
    const proc = await lightpanda.serve({ host: '127.0.0.1', port: 9222 });
    browser = await puppeteer.connect({
      browserWSEndpoint: 'ws://127.0.0.1:9222'
    });
  });

  it('should set and read cell values', async () => {
    const page = await browser.newPage();
    await page.goto('http://localhost:8080');

    // Click cell A1, type value
    await page.click('[data-cell="A1"]');
    await page.type('42');
    await page.keyboard.press('Enter');

    // Verify value
    const value = await page.$eval('[data-cell="A1"]', el => el.textContent);
    expect(value).toBe('42');
  });
});
```

---

## Phase 2: Investigate and Fix Formula Export

**Goal**: Ensure formulas are preserved when exporting to ZCD and XLSX formats. Show warning when exporting to CSV.

### Analysis

Looking at `serializer.cc:192-202`, formulas ARE serialized correctly with `cell.formula->text`. The issue is likely:
- Formula AST is created but the original text isn't preserved
- Or computed `value.raw` is being used instead of formula text

### Tasks

- [x] 2a: Add test case to verify formula round-trip (export ZCD → parse → verify formula preserved)
- [x] 2b: Fix formula text preservation in CellValue when formula is evaluated (ensure raw keeps formula text, not computed result)
- [x] 2c: Add CSV export warning in UI when workbook contains formulas
- [x] 2d: Verify XLSX formula export uses formula text (not computed value) via RefConverter

### Implementation Notes (2a, 2b)

Introduced FORMULA_* result types in CellValueType enum to preserve formula identity after evaluation:
- `FORMULA_NUMBER` - formula that evaluates to a number
- `FORMULA_STRING` - formula that evaluates to a string
- `FORMULA_BOOLEAN` - formula that evaluates to a boolean
- `FORMULA_ERROR` - formula that evaluates to an error
- `FORMULA_EMPTY` - formula that evaluates to empty

Key changes:
1. `types.h`: Added new enum values and `isFormulaType()` helper
2. `formula_recalc.cc`: Set appropriate FORMULA_* type after evaluation
3. `formula_eval.cc`: Same fix for evaluate() function
4. `serializer.cc`: All FORMULA_* types serialize as 'f' with formula text
5. `csv_writer.cc`: Handle all FORMULA_* types for value output
6. `model.cc`: Updated `asNumber()`/`asBoolean()` to accept FORMULA_* types
7. Test added in `serializer_test.cc`: FormulaRoundtripTest.FormulaPreservedAfterEvaluation

### Implementation Notes (2c)

Added `hasFormulas()` method to check if workbook contains any formula cells:
1. `bindings.cc`: Added `hasFormulas()` method that iterates all sheets/cells checking `cell->isFormula()`
2. `worker.ts`: Added `hasFormulas()` to CellsEngine interface and message handler
3. `client.ts`: Added `hasFormulas()` client method
4. `wasm-data-source.ts`: Added `hasFormulas()` wrapper method
5. `file-loader.ts`: Updated `exportAs()` to show confirmation dialog when exporting CSV with formulas

### Implementation Notes (2d)

Verified and fixed XLSX formula export to handle FORMULA_* result types:
1. `xlsx_writer.cc`: Updated cached value writing to handle FORMULA_NUMBER, FORMULA_STRING, FORMULA_BOOLEAN, FORMULA_ERROR types
2. `xlsx_writer.cc`: Updated value cell branch (when writeFormulas=false) to handle FORMULA_* types
3. Added tests:
   - `WriteFormulasWithEvaluatedTypes`: Tests formula cells with FORMULA_* types export correctly with formula text and cached values
   - `WriteFormulasDisabledWithDifferentTypes`: Tests that when writeFormulas=false, only cached values are exported (no formulas)

The xlsx_writer already used RefConverter.formulaToA1() to convert UUID-based formula references to A1 notation - verified this is working correctly.

---

## Phase 3: Document Title Editing ✅ COMPLETE

**Goal**: Allow users to edit document title, use snake_case for export filename.

### Current State

- `Workbook::name` exists in model.h
- `setWorkbookName`/`getWorkbookName` exist in bindings.cc
- Default is "Sheet1" for new files, filename base for loaded files
- Export uses `_workbookName` directly (wasm-data-source.ts:296)

### Tasks

- [x] 3a: Add title display element in header (show workbook name)
- [x] 3b: Make title clickable/editable (contenteditable or input field)
- [x] 3c: Connect title changes to setWorkbookName via client
- [x] 3d: Add toSnakeCase utility function for filename generation
- [x] 3e: Update exportAs to use snake_case version of workbook name as filename
- [x] 3f: Add WORKBOOK_RENAME CRDT operation for title changes (sync across peers)

### Implementation Notes

1. **HTML/CSS Changes**:
   - Added `#workbook-title` contenteditable span in header
   - Added CSS for hover/focus states and empty placeholder
   - Kept hidden `#sheet-name` span for backwards compatibility

2. **TypeScript Changes**:
   - Created `WorkbookTitleEditor` class (`workbook-title-editor.ts`)
   - Handles focus, blur, Enter/Escape key events
   - Commits changes to WASM via `setWorkbookName`
   - Integrated into `init.ts` and `AppContext`

3. **Utility Function**:
   - Added `toSnakeCase()` in `utils.ts`
   - Converts "My Document" → "my_document"
   - Used in `wasm-data-source.ts` for export filenames

4. **CRDT Operation**:
   - Added `WORKBOOK_RENAME = 30` to `OpType` enum
   - Added `applyWorkbookRename()` in `crdt.cc`
   - Added `makeWorkbookRenameOp()` helper function
   - Updated `bindings.cc` to create CRDT op when name changes
   - Payload format: `{"name":"NewName"}`

---

## Phase 4: Fix Collaboration Lag (Operation Pruning)

**Goal**: Ensure operations don't pile up during collaboration, implement efficient sync.

### Analysis

Current sync_manager.cc:241-262 shows `queueOperationsBroadcast()` finds minimum HLC across peers and sends ops newer than that. The issue could be:

1. `lastSyncedHLC` not being updated correctly when receiving operations
2. `pruneOpLog()` not called frequently enough
3. Large operation payloads being re-broadcast

### Tasks

- [ ] 4a: Add debug logging for operation counts and HLC values during sync
- [ ] 4b: Verify lastSyncedHLC updates correctly in handleOperations (line 396-404)
- [ ] 4c: Call pruneOpLog more aggressively after sync-response received
- [ ] 4d: Add operation deduplication check before broadcast (skip if peer already has op based on HLC)
- [ ] 4e: Add sync status indicator in UI (show "synced" vs "X pending ops")

---

## Phase 5: Fix XLSX Stress Test Loading

**Goal**: Ensure stress_test.xlsx (4.1MB) loads and displays correctly with proper scrolling.

### Analysis

The file exists at `testdata/xlsx/stress_test.xlsx`. Issues could be:
- Virtualization not working correctly for large datasets
- Quadtree not indexed properly for all rows
- Memory issues with large cell counts

### Tasks

- [ ] 5a: Add CLI test to load stress_test.xlsx and print statistics (row count, cell count, memory)
- [ ] 5b: Verify quadtree contains all cells after loading large XLSX
- [ ] 5c: Add viewport query test for bottom rows of large spreadsheet
- [ ] 5d: Fix any identified issues with large file handling

---

## Phase 6: Split Axis Operations (Remove isCol)

**Goal**: Replace generic DIM_* operations with specific COL_* and ROW_* operations.

### Current State

```cpp
enum class OpType : uint8_t {
    DIM_INSERT_AXIS = 10,  // Uses isCol in payload
    DIM_DELETE_AXIS = 11,
    DIM_MOVE_AXIS = 12,
    DIM_RESIZE_AXIS = 13,
    DIM_RENAME_AXIS = 14,
}
```

### Target State

```cpp
enum class OpType : uint8_t {
    COL_INSERT = 10,
    COL_DELETE = 11,
    COL_MOVE = 12,
    COL_RESIZE = 13,
    COL_RENAME = 14,

    ROW_INSERT = 15,
    ROW_DELETE = 16,
    ROW_MOVE = 17,
    ROW_RESIZE = 18,
    // Note: ROW_RENAME intentionally omitted - rows cannot be renamed
}
```

### Tasks

- [ ] 6a: Update OpType enum with new COL_* and ROW_* constants (keep DIM_* for backwards compatibility parsing)
- [ ] 6b: Add new opTypeToString/stringToOpType mappings for COL_*/ROW_* operations
- [ ] 6c: Create separate apply functions: applyColInsert, applyRowInsert, applyColDelete, applyRowDelete, etc. (no applyRowRename - rows cannot be renamed)
- [ ] 6d: Update CRDT dispatcher to route COL_* and ROW_* operations to appropriate handlers
- [ ] 6e: Update bindings.cc to generate COL_*/ROW_* operations instead of DIM_*
- [ ] 6f: Update bootstrapOpLog to generate COL_*/ROW_* operations
- [ ] 6g: Add backwards compatibility: parse old DIM_* operations and convert based on isCol payload
- [ ] 6h: Update serializer to use new operation types (remove isCol from payload)
- [ ] 6i: Remove any row rename logic from applyDimRenameAxis (verify it only handles columns)

---

## Summary

| Phase | Focus | Files Affected |
|-------|-------|----------------|
| 1 | Programmatic Testing | tests/e2e/*, package.json |
| 2 | Formula Export | serializer.cc, csv_writer.cc, xlsx_writer.cc, UI |
| 3 | Document Title | model.h, bindings.cc, client.ts, UI |
| 4 | Collaboration | sync_manager.cc, bindings.cc, UI |
| 5 | XLSX Loading | xlsx_reader.cc, quadtree.cc, bindings.cc |
| 6 | Axis Operations | operation.h/cc, crdt.cc, bindings.cc, serializer.cc |
