# Rename .cells to .zcd (Zero-Conflict Document)

## Overview

Rename the native file format extension from `.cells` to `.zcd` (Zero-Conflict Document), move testdata to repo root, and ensure all documentation accurately reflects the current project state.

## Phase 1: Rename Test Data Files

- [x] 1a: Rename all `.cells` test files to `.zcd` in `core/testdata/`
- [x] 1b: Update `core/testdata/BUILD` to reference `.zcd` files
- [x] 1c: Move `core/testdata/` to `/testdata/` (repo root)

## Phase 2: Update C++ Code

- [x] 2a: Update `parser.h` - change extension constant and comments
- [x] 2b: Update `serializer.h` and `serializer.cc` - extension references
- [x] 2c: Update `serializer_test.cc` - test file paths
- [x] 2d: Update `parser_test.cc` - test file paths
- [x] 2e: Update `roundtrip_test.cc` - test file paths
- [x] 2f: Update `ref_converter_test.cc` - test file paths (N/A - only has sheet.cells member refs)
- [x] 2g: Update `ref_converter.cc` - any extension references (N/A - only has sheet.cells member refs)
- [x] 2h: Update `quadtree.cc` - any extension references (N/A - only has sheet.cells member refs)
- [x] 2i: Update `xlsx_writer.cc` and `xlsx_writer_test.cc` - extension references (N/A - only has sheet.cells member refs)

## Phase 3: Update CLI Application

- [x] 3a: Update `apps/cli/main.cc` - extension handling
- [x] 3b: Update `apps/cli/options.h` - help text and extension constants
- [x] 3c: Update `apps/cli/converter.cc` - format detection
- [x] 3d: Update `apps/cli/converter_test.cc` - test file paths

## Phase 4: Update WASM/Web Application

- [x] 4a: Update `apps/wasm/bindings.cc` - extension references
- [x] 4b: Update `apps/wasm/worker.js` - file type handling
- [x] 4c: Update `apps/wasm/client.js` - extension references
- [x] 4d: Update `apps/wasm/cells.d.ts` - TypeScript definitions
- [x] 4e: Update `apps/wasm/static/index.html` - file input accept types
- [x] 4f: Update `apps/wasm/static/shared/grid-renderer.js` - any references (N/A - only has data property refs)
- [x] 4g: Update test files (`test.html`, `test_worker.html`, `test_node.js`)

## Phase 5: Update Scripts and Examples

- [x] 5a: Update `scripts/generate_large_cells.py` - output filename/extension
- [x] 5b: Update `examples/convert_all.sh` - extension references
- [x] 5c: Update `examples/cli_examples.sh` - extension references

## Phase 6: Create File Format Documentation

- [x] 6a: Create `docs/file-format.md` documenting the ZCD format specification
  - File header and version
  - Section markers and line prefixes
  - Entity types (D, S, C, R, X, T, Y, O)
  - Cell types and value formats
  - Formula reference format
  - Compressed variants (.zcz for zstd, .zcb for binary)
  - Migration notes from .cells to .zcd

## Phase 7: Update Documentation for Accuracy

Update all docs to accurately reflect current implementation status:

- [x] 7a: Update `docs/crdt.md`
  - Change status: HLC ✅ Implemented, Operation types ✅ Implemented, OpLog ✅ Implemented
  - Add implementation references (hlc.h, crdt.h, operation.h, oplog.h, sync_manager.h)
  - Note what's still not implemented (branch-based undo/redo, presence/cursors)

- [x] 7b: Update `docs/networking.md`
  - Verify current status matches documentation
  - Add any new implementation details

- [ ] 7c: Update `docs/persistence.md`
  - Change all `.cells` references to `.zcd`
  - Update format variants table (.zcd, .zcz, .zcb)
  - Add reference to new file-format.md

- [ ] 7d: Update `docs/data-model.md`
  - Change testdata path from `core/testdata/` to `testdata/`
  - Verify implementation references are accurate

- [ ] 7e: Update `docs/formula-engine.md`
  - Verify status is accurate (currently shows as not implemented)

- [ ] 7f: Update `docs/rendering.md`
  - Verify current implementation status is accurate

- [ ] 7g: Update `docs/cross-platform.md`
  - Verify current implementation status is accurate

- [ ] 7h: Update `docs/type-system.md`
  - Verify current implementation status is accurate

## Phase 8: Update Root Documentation

- [ ] 8a: Update `README.md`
  - Change all `.cells` references to `.zcd`
  - Update directory structure to show `testdata/` at root
  - Update testdata path in core/ section
  - Add link to `docs/file-format.md` in Core Components section
  - Update "Current Implementation Status" if needed

- [ ] 8b: Update `GETTING_STARTED.md`
  - Change all `.cells` references to `.zcd`
  - Update CLI usage examples
  - Update supported formats table

- [ ] 8c: Update `AGENTS.md`
  - Change `core/testdata/` to `testdata/`
  - Update sample file references

- [ ] 8d: Update `.gitattributes`
  - Change `.cells` patterns to `.zcd`

## Phase 9: Update Existing Plans (Historical Accuracy)

- [ ] 9a: Add note to completed plans that `.cells` was renamed to `.zcd`

## Phase 10: Verification

- [ ] 10a: Run `bazel test //core/...` - ensure all tests pass
- [ ] 10b: Run `bazel build //apps/cli:cells` - ensure CLI builds
- [ ] 10c: Run `bazel build --config=wasm //apps/wasm:cells_wasm` - ensure WASM builds
- [ ] 10d: Grep for remaining `.cells` references to ensure none were missed

## Notes

### Why ".zcd"?
- **Z**ero-**C**onflict **D**ocument
- Reflects the CRDT-based conflict-free nature of the format
- Short, memorable, unique extension
- No conflicts with existing file formats

### File Format Variants
| Extension | Format | Description |
|-----------|--------|-------------|
| `.zcd` | Raw text | Git-friendly, human-readable |
| `.zcz` | Text + zstd | Compressed, still git-diffable |
| `.zcb` | Binary | Large files, fast loading |

### Backward Compatibility
- No automatic migration needed (format content unchanged)
- Users can simply rename their `.cells` files to `.zcd`
- CLI could optionally accept `.cells` with deprecation warning (out of scope for this plan)
