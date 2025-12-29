Status: IN_PROGRESS
Created At: 2025-12-28 09:55 UTC
Updated At: 2025-12-29 12:00 UTC
Following plan management guidelines defined in AGENTS.md

# Clipboard, Collaboration Tests, and UX Improvements

This plan addresses four user-requested improvements:

1. **Clipboard operations** - Copy/Paste/Cut for cells (keyboard + context menu)
2. **Collaboration E2E tests** - Make them reliable and add to stable test suite
3. **Document name from D line** - Parse workbook name from .cells format, fallback to filename
4. **CSV export disclaimer** - Replace native confirm() with styled modal dialog

---

## Phase 1: Document Name from D Line

**Goal**: When loading a .cells/.zcd file, extract the workbook name from the 'D' line rather than deriving it from the filename.

### Analysis

Current flow (file-loader.ts:191):
```typescript
const baseName = getBaseName(file.name);
dataSource.setWorkbookName(baseName);
```

The C++ parser already extracts the name from the D line and stores it in `workbook->name` (parser.cc:171). The WASM binding `getWorkbookName()` exists (bindings.cc:1635). We just need to use it after loading.

### Tasks

- [x] 1a: After loading ZCD format, call `getWorkbookName()` from WASM to get parsed name
- [x] 1b: Use parsed name if non-empty, otherwise fall back to filename base
- [x] 1c: Apply same logic for auto-loaded persisted files
- [x] 1d: Add test case for workbook name extraction

---

## Phase 2: CSV Export Modal Dialog

**Goal**: Replace the native `confirm()` dialog with a styled, integrated modal for the CSV export warning.

### Analysis

Current implementation (file-loader.ts:374-387):
```typescript
const proceed = confirm(
  "This workbook contains formulas. CSV format only saves computed values..."
);
```

Need to create a reusable modal component matching the app's visual style (dark theme, rounded corners, etc.)

### Tasks

- [x] 2a: Create `modal.ts` module with Modal class (show/hide, backdrop, close handlers)
- [x] 2b: Add CSS styles for modal (overlay, content box, buttons) in styles.css
- [x] 2c: Create `confirm()` helper that returns a Promise<boolean>
- [x] 2d: Replace native `confirm()` in exportAs with styled modal
- [x] 2e: Add keyboard support (Enter to confirm, Escape to cancel)

### Design

Modal should include:
- Semi-transparent backdrop (click outside to cancel)
- Warning icon
- Title: "Export to CSV"
- Body text explaining formula limitation
- Two buttons: "Cancel" (secondary) and "Export Anyway" (primary)

---

## Phase 3: Clipboard Operations (Copy/Paste/Cut)

**Goal**: Enable copy, paste, and cut operations for cells via keyboard shortcuts and context menu.

### Analysis

Current state:
- Context menu items exist but are disabled (app-events.ts:1193-1212)
- No keyboard shortcut handlers for Cmd/Ctrl+C/V/X
- Paste handler exists for text input (cell-editor.ts:742-746) but not for cells

### Data Format

For clipboard, use a custom MIME type + text fallback:
- `application/x-cells-clipboard`: JSON with cell data (values, formulas, types)
- `text/plain`: Tab-separated values for paste into other apps

### Tasks

- [ ] 3a: Create `clipboard.ts` module with ClipboardManager class
- [ ] 3b: Implement `copy()` - serialize selected cells to clipboard formats
- [ ] 3c: Implement `cut()` - copy + delete selected cells
- [ ] 3d: Implement `paste()` - read clipboard, insert cells at selection
- [ ] 3e: Add keyboard shortcuts (Cmd/Ctrl+C/V/X) in app-events.ts
- [ ] 3f: Enable and connect context menu items to clipboard operations
- [ ] 3g: Handle multi-cell selection for copy/paste (rectangular regions)
- [ ] 3h: Add paste from external sources (parse tab-separated text)

### Implementation Notes

1. **Copy selection**: Get all cells in selected range, serialize to JSON:
   ```typescript
   {
     rows: 2,
     cols: 3,
     cells: [
       { row: 0, col: 0, value: "Hello", type: "string" },
       { row: 0, col: 1, formula: "=A1+1", type: "formula" },
       ...
     ]
   }
   ```

2. **Paste**: Read JSON if available, otherwise parse text/plain as TSV

3. **Cut**: Copy + delete (via CRDT CELL_SET with empty value or CELL_DELETE)

---

## Phase 4: Collaboration E2E Test Fixes

**Goal**: Make collaboration tests reliable enough to add to the stable test suite.

### Analysis

Current issues (collab.test.mjs):
1. WebRTC limitations in headless Chrome with shared browser contexts
2. Long sleep() waits (5 seconds) for connection establishment
3. Data channel timing is flaky

### Approach

Instead of fighting WebRTC headless limitations, use a more reliable approach:
1. Use headed Chrome for collab tests (HEADED=1 env var)
2. Add proper wait conditions instead of fixed sleeps
3. Increase test robustness with retry logic

### Tasks

- [ ] 4a: Add `waitForCollabReady()` helper that waits for data channel, not just sleep
- [ ] 4b: Add retry logic for flaky assertions (3 attempts with backoff)
- [ ] 4c: Reduce fixed sleep times where possible, replace with event-based waits
- [ ] 4d: Add collab UI indicator for "data channel open" state
- [ ] 4e: Run collab tests in CI with headed Chrome (xvfb-run)
- [ ] 4f: Add collab tests to `test:stable` once passing reliably

### Alternative Approach (if WebRTC remains problematic)

If peer-to-peer tests remain flaky, consider:
- Testing CRDT sync logic directly (unit tests in C++)
- Mock WebRTC layer for E2E tests
- Accept collab tests as "experimental" with separate CI job

---

## Summary

| Phase | Focus | Files Affected |
|-------|-------|----------------|
| 1 | Document Name | file-loader.ts, client.ts, worker.ts |
| 2 | CSV Modal | modal.ts (new), styles.css, file-loader.ts |
| 3 | Clipboard | clipboard.ts (new), app-events.ts, context-menu.ts |
| 4 | Collab Tests | tests/collab.test.mjs, tests/helpers.mjs, package.json |

---

## Execution Order Rationale

1. **Phase 1** (Document Name) - Quick win, isolated change, low risk
2. **Phase 2** (CSV Modal) - Creates reusable modal component needed later
3. **Phase 3** (Clipboard) - Core feature, may use modal for paste conflicts
4. **Phase 4** (Collab Tests) - Most complex, may require iteration
