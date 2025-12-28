Status: READY
Created At: 2025-12-28 06:49 UTC
Updated At: 2025-12-28 07:05 UTC
Following plan management guidelines defined in AGENTS.md

# Export, CRDT Operations, and UX Fixes

This plan addresses six interconnected issues:

1. **Formula preservation in exports** - Formulas exported as values instead of formula text
2. **Axis operation refactoring** - Split DIM_INSERT_AXIS → COL_INSERT/ROW_INSERT (and similar)
3. **XLSX loading issues** - stress_test.xlsx not displayed correctly (empty rows on scroll)
4. **Collaboration lag** - Operations potentially piling up during sync
5. **Document title editing** - Allow editing Sheet1 title, use snake_case for export filename
6. **Programmatic testing** - Add Lightpanda integration for automated web UI testing

---

## Phase 1: Investigate and Fix Formula Export

**Goal**: Ensure formulas are preserved when exporting to ZCD and XLSX formats. Show warning when exporting to CSV.

### Analysis

Looking at `serializer.cc:192-202`, formulas ARE serialized correctly with `cell.formula->text`. The issue is likely:
- Formula AST is created but the original text isn't preserved
- Or computed `value.raw` is being used instead of formula text

### Tasks

- [ ] 1a: Add test case to verify formula round-trip (export ZCD → parse → verify formula preserved)
- [ ] 1b: Fix formula text preservation in CellValue when formula is evaluated (ensure raw keeps formula text, not computed result)
- [ ] 1c: Add CSV export warning in UI when workbook contains formulas
- [ ] 1d: Verify XLSX formula export uses formula text (not computed value) via RefConverter

---

## Phase 2: Split Axis Operations (Remove isCol)

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

- [ ] 2a: Update OpType enum with new COL_* and ROW_* constants (keep DIM_* for backwards compatibility parsing)
- [ ] 2b: Add new opTypeToString/stringToOpType mappings for COL_*/ROW_* operations
- [ ] 2c: Create separate apply functions: applyColInsert, applyRowInsert, applyColDelete, applyRowDelete, etc. (no applyRowRename - rows cannot be renamed)
- [ ] 2d: Update CRDT dispatcher to route COL_* and ROW_* operations to appropriate handlers
- [ ] 2e: Update bindings.cc to generate COL_*/ROW_* operations instead of DIM_*
- [ ] 2f: Update bootstrapOpLog to generate COL_*/ROW_* operations
- [ ] 2g: Add backwards compatibility: parse old DIM_* operations and convert based on isCol payload
- [ ] 2h: Update serializer to use new operation types (remove isCol from payload)
- [ ] 2i: Remove any row rename logic from applyDimRenameAxis (verify it only handles columns)

---

## Phase 3: Fix XLSX Stress Test Loading

**Goal**: Ensure stress_test.xlsx (4.1MB) loads and displays correctly with proper scrolling.

### Analysis

The file exists at `testdata/xlsx/stress_test.xlsx`. Issues could be:
- Virtualization not working correctly for large datasets
- Quadtree not indexed properly for all rows
- Memory issues with large cell counts

### Tasks

- [ ] 3a: Add CLI test to load stress_test.xlsx and print statistics (row count, cell count, memory)
- [ ] 3b: Verify quadtree contains all cells after loading large XLSX
- [ ] 3c: Add viewport query test for bottom rows of large spreadsheet
- [ ] 3d: Fix any identified issues with large file handling

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

## Phase 5: Document Title Editing

**Goal**: Allow users to edit document title, use snake_case for export filename.

### Current State

- `Workbook::name` exists in model.h
- `setWorkbookName`/`getWorkbookName` exist in bindings.cc
- Default is "Sheet1" for new files, filename base for loaded files
- Export uses `_workbookName` directly (wasm-data-source.ts:296)

### Tasks

- [ ] 5a: Add title display element in header (show workbook name)
- [ ] 5b: Make title clickable/editable (contenteditable or input field)
- [ ] 5c: Connect title changes to setWorkbookName via client
- [ ] 5d: Add toSnakeCase utility function for filename generation
- [ ] 5e: Update exportAs to use snake_case version of workbook name as filename
- [ ] 5f: Add WORKBOOK_RENAME CRDT operation for title changes (sync across peers)

---

## Phase 6: Programmatic Testing with Lightpanda

**Goal**: Enable automated testing of the web UI to iterate faster on features and catch regressions.

### Why Lightpanda?

- **10x faster** than Chrome headless
- **10x less memory** than Chrome
- **Instant startup** - no browser spinup delay
- **CDP compatible** - works with Puppeteer and Playwright
- **JavaScript execution** - can interact with dynamic web apps

### Tasks

- [ ] 6a: Install Lightpanda npm package (`@lightpanda/browser`) and puppeteer-core
- [ ] 6b: Create test harness that starts local dev server + Lightpanda
- [ ] 6c: Add helper functions for common operations (setCellValue, getCellValue, selectCell, etc.)
- [ ] 6d: Write basic smoke test: create workbook, set values, export, verify
- [ ] 6e: Add formula test: enter formula, verify computed result
- [ ] 6f: Add collaboration test: two browser contexts, verify sync
- [ ] 6g: Document how to run programmatic tests in README

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

## Summary

| Phase | Focus | Files Affected |
|-------|-------|----------------|
| 1 | Formula Export | serializer.cc, csv_writer.cc, xlsx_writer.cc, UI |
| 2 | Axis Operations | operation.h/cc, crdt.cc, bindings.cc, serializer.cc |
| 3 | XLSX Loading | xlsx_reader.cc, quadtree.cc, bindings.cc |
| 4 | Collaboration | sync_manager.cc, bindings.cc, UI |
| 5 | Document Title | model.h, bindings.cc, client.ts, UI |
| 6 | Programmatic Testing | tests/e2e/*, package.json |

**Recommended order**: Phase 6 first (enables faster iteration), then 1→5→4→3→2 (increasing complexity).
