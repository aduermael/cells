# XLSX Round-Trip Corruption Fix

## Problem

When editing and exporting XLSX files, the exported file becomes corrupted and cannot be opened by Excel. The corruption is caused by binary data being mishandled during the WASM ↔ JavaScript boundary transfer.

### Root Cause Analysis

1. **Binary-to-String Corruption**: The C++ `exportToXLSX()` function in `bindings_file.cc` returns a `std::string` containing raw binary data (the ZIP file bytes). When Emscripten's Embind passes this to JavaScript, it may interpret the bytes as UTF-8, corrupting bytes with values ≥ 128.

2. **Evidence**: The corrupted test file shows `0xfd` bytes scattered throughout (UTF-8 replacement character), confirming UTF-8 encoding corruption.

3. **ZIP Structure Damage**: The `unzip` command reports "missing 1002 bytes" and "start of central directory not found" - the ZIP's central directory (at the end of the file) is corrupted because it contains compressed binary data with high byte values.

### Current Export Flow (Broken)

```
C++ writeXLSX() → writes to /tmp/export.xlsx
C++ reads file back as std::string
Embind transfers std::string → JS string (UTF-8 corruption here!)
JS charCodeAt() → Uint8Array (already corrupted)
JS creates Blob and downloads
```

## Solution

Fix the binary data transfer between WASM and JavaScript by using proper binary transfer mechanisms instead of treating binary data as strings.

---

## Phase 1: Fix Binary Export from C++ to JS

The core issue is the transfer of binary data across the WASM boundary.

- [x] 1a: Create `exportToXLSXPtr()` C++ function that returns binary data via WASM heap pointer
  - Added `exportToXLSXPtr()` in `bindings_file.cc` - stores binary data in `_exportBuffer` member, returns JSON `{ptr, size}`
  - Added `_exportBuffer` member variable in `bindings.h`
  - Registered in Embind bindings

- [x] 1b: Update TypeScript worker handler to use pointer-based export
  - Updated `handleExport()` in `worker-handlers.ts` to use `Module.HEAPU8.slice(ptr, ptr + size)`
  - Handler now passes `Module` parameter for WASM heap access

- [x] 1c: Add cleanup method for exported binary data
  - Added `freeExportBuffer()` in `bindings_file.cc` - clears and shrinks `_exportBuffer`
  - Called in worker handler after copying data from WASM heap

---

## Phase 2: Add XLSX Round-Trip Tests

Create comprehensive tests to verify files can be opened by Excel after editing.

- [ ] 2a: Add C++ round-trip test for XLSX
  - In `xlsx_writer_test.cc`, add test that: reads XLSX → modifies → writes → reads again → verifies
  - Test with: numbers, strings, formulas, styles, multi-sheet workbooks

- [ ] 2b: Add WASM integration test for XLSX export
  - In `apps/wasm/tests/`, create `xlsx-export.test.mjs`
  - Test: load XLSX → export → verify ZIP structure is valid
  - Use `jszip` or similar to validate the exported file structure

- [ ] 2c: Add test with the corrupted file
  - Verify the fix by ensuring `testdata/corrupted.xlsx` case is addressed
  - Test exports can be re-imported successfully

---

## Phase 3: Validate ZIP Structure

Add validation to ensure exported files are structurally valid.

- [ ] 3a: Add basic ZIP validation in C++ after write
  - After `zip.finalize()`, optionally verify the file can be opened by miniz
  - Add optional debug mode to log ZIP entry sizes

- [ ] 3b: Ensure all required XLSX parts are present
  - Validate: `[Content_Types].xml`, `_rels/.rels`, `xl/workbook.xml`, etc.
  - Check XML is well-formed (basic validation)

---

## Phase 4: Documentation and Edge Cases

- [ ] 4a: Document the binary transfer pattern
  - Add comments explaining why pointer-based transfer is necessary
  - Document the export flow for future maintainers

- [ ] 4b: Test edge cases
  - Large files (stress_test.xlsx)
  - Files with many styles
  - Files with shared formulas
  - Files with unicode content in strings
