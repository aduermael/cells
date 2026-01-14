# Fix/Improve Copy-Paste Functionality

## Problem Statement

Two issues with the current clipboard implementation:

1. **External paste shows metadata**: When copying a cell and pasting in an external text editor, users see both the internal JSON metadata AND the cell value:
   ```
   CELLS_CLIPBOARD::{"rows":1,"cols":1,"cells":[...],"sourceCol":2,"sourceRow":9}

   Share Price:
   ```
   External apps should only see the clean TSV/text value.

2. **Can't paste in AI input**: Pressing Cmd/Ctrl+V while the agent panel textarea is focused pastes into the spreadsheet cell instead of the textarea. The keyboard event handler intercepts the paste shortcut before checking if focus is on an input/textarea element.

## Solution Approach

### Issue 1: Use Clipboard API with Multiple MIME Types

The modern Clipboard API supports writing multiple formats simultaneously via `ClipboardItem`:
- `text/plain` - Contains only the clean TSV/text for external apps
- Custom web MIME type (e.g., `web application/x-cells-clipboard`) - Contains the JSON metadata for internal use

On paste:
1. First try to read the custom MIME type for internal paste with full formatting
2. Fall back to `text/plain` for external paste (TSV parsing)

**Note**: Custom MIME types must use the `web ` prefix for security (Async Clipboard API spec).

### Issue 2: Check Active Element Before Clipboard Shortcuts

The keyboard handler should check if the active element is an input/textarea BEFORE handling clipboard shortcuts, not after.

---

## Phase 1: Fix Clipboard MIME Type Handling
- [ ] 1a: Update `ClipboardManager.copy()` to use `navigator.clipboard.write()` with `ClipboardItem` containing both `text/plain` (TSV only) and `web application/x-cells-clipboard` (JSON metadata)
- [ ] 1b: Update `ClipboardManager.paste()` to first try reading `web application/x-cells-clipboard`, falling back to `text/plain` for external content
- [ ] 1c: Remove the `CELLS_CLIPBOARD::` marker prefix system (no longer needed)

## Phase 2: Fix AI Input Paste Interception
- [ ] 2a: Move the active element input/textarea check to occur BEFORE clipboard shortcut handling in `keyboard-events.ts`

## Phase 3: Testing & Edge Cases
- [ ] 3a: Test internal copy/paste preserves all formatting and formulas
- [ ] 3b: Test external paste (from other apps) works correctly via TSV parsing
- [ ] 3c: Test copying cell and pasting in external text editor shows clean text only
- [ ] 3d: Test pasting in agent panel textarea works correctly
- [ ] 3e: Test pasting in formula bar still works correctly
