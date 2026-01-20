# Collab Demo Styling Improvements

## Problem Statement

The collab demo (`bazel run :e2e-headed -- collab-demo`) has several issues:
1. Background colors may not be visually appearing (possible style sync issue)
2. Styling happens too late in the demo - should appear earlier to make it more visually impressive
3. Different fonts are not being demonstrated
4. No dedicated test exists to verify that styles (background colors, text colors, fonts, borders, formats) sync correctly between peers

## Investigation Findings

- Styles ARE synced via CRDT operations (CELL_SET_STYLE, STYLE_DEFINE, RANGE_SET_STYLE)
- The collab.test.mjs has no style sync tests - only tests cell value sync
- Font family functionality exists in style-controls.ts but no helper function for it in tests
- The demo currently applies styles late (Act 2 onwards), missing opportunity to show Robert styling early

## Phase 1: Add Dedicated Style Sync Test

Create a focused collaboration test that verifies all style properties sync correctly.
This will be a separate file that runs with all E2E tests but can also be run standalone.

- [x] 1a: Create `collab-style-sync.test.mjs` with tests for:
  - Background color sync between 2 peers
  - Text color sync between 2 peers
  - Bold/italic/underline sync between 2 peers
  - Font family sync between 2 peers (e.g., Arial vs Times New Roman)
  - Font size sync between 2 peers
  - Border sync between 2 peers
  - Number format sync between 2 peers (currency, percentage)
  - Bidirectional style sync test

- [x] 1b: Add the new test to:
  - The `all` collection in `test-parallel.mjs` (runs with all E2E tests)
  - The `collab` collection in `test-parallel.mjs` (runs with collab tests)
  - Can be run standalone via `bazel run :e2e-headed -- collab-style-sync`

### Phase 1 Test Results (Bug Found)

**Test Status:** 3/8 tests pass

| Test | Status | Verification Method |
|------|--------|---------------------|
| Background color sync | ❌ FAIL | Visual pixel check |
| Text color sync | ❌ FAIL | Visual pixel check |
| Bold/italic/underline sync | ✅ PASS | `cell.style` object check |
| Amber background sync | ❌ FAIL | Visual pixel check |
| Purple background sync | ❌ FAIL | Visual pixel check |
| Border sync | ✅ PASS | `cell.style` object check |
| Number format sync | ✅ PASS | Value sync verification |
| Bidirectional sync | ❌ FAIL | Visual pixel check |

**Run the test:** `bazel run :e2e -- collab-style-sync` (or `bazel run :e2e-headed -- collab-style-sync` for visible browser)

**Bug Discovered:** Background color styles are NOT being included in viewport data.
- Debug shows that even on local peer, after applying background color, the cell has NO `styleId` or `style` property
- Yet the cell renders with the color visually - the renderer must be getting style info from somewhere else
- Bold/italic/underline DO get stored in `cell.style` and sync correctly
- Background colors fail to sync because the style data isn't in the viewport response

**Root Cause Hypothesis:** Background colors may be using a different style storage path that doesn't get included in viewport queries. The renderer might access styles directly from WASM/CRDT oplog rather than from the viewport cell data.

**Next Steps:** Before continuing to Phase 2-5, the viewport data must be fixed to include all style properties (bgColor, textColor, fontFamily, fontSize) in the cell style object. This is a core engine bug, not a test issue.

## Phase 2: Add Font Family Helper Function

- [ ] 2a: Add `applyFontFamily(page, fontName)` helper to collab-demo.test.mjs that:
  - Clicks the font-family-btn dropdown
  - Selects the font from the dropdown menu
  - Handles the dropdown toggle correctly

## Phase 3: Restructure Demo - Robert as Styling Lead

Reorganize the demo so Robert focuses on styling existing content while others add data.

**Font choices (visually distinct):**
- Title: **Georgia** (serif, elegant)
- Headers: **Helvetica** (sans-serif, clean)
- Data cells: **Arial** (default, readable)
- Credits/Motto: **Courier New** (monospace, distinctive)

- [ ] 3a: Modify Act 2 - After Nico creates title, Robert immediately styles it (blue background, white text, bold, Georgia font) before Nico continues with headers
- [ ] 3b: Modify Act 2 - After Nico adds headers, Robert styles them (gray background, bold, borders, Helvetica font) before Nico adds features
- [ ] 3c: Modify Act 4 - While Shuying adds estimates, Robert styles feature list with alternating row colors
- [ ] 3d: Modify Finale - Apply Courier New font to the motto/credits section
- [ ] 3e: Ensure all styling happens with clear sync verification to other peers

## Phase 4: Debug and Fix Style Visibility Issues

- [ ] 4a: Add explicit verification that styled cells render correctly on all peers (check computed background color via JavaScript evaluation)
- [ ] 4b: Add sleep/sync waits after style operations to ensure sync completes before moving on
- [ ] 4c: If colors don't appear, investigate the color popup selector matching (case sensitivity, color format)

## Phase 5: Polish Demo Output

- [ ] 5a: Update console log messages to clearly narrate the styling as it happens ("Robert applies blue background to title...")
- [ ] 5b: Add font changes to the final summary output
- [ ] 5c: Ensure SLOWMO mode allows watching all style changes visually
