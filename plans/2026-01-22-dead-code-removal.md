# Dead Code Removal Plan

Remove unused code to keep the codebase lean during early development. This follows the previous cleanup of deprecated/legacy terminology.

## Summary of Dead Code Found

### TypeScript (apps/wasm/src/types.ts)
~20 exported types that are defined but never imported elsewhere:
- `ApplyOperationResult`, `ApplyOperationsResult`
- `ClientOptions`, `CollabStatus`
- `CppPeerInfo`, `CppPresence`
- `CRDTOperation`, `GridCell`
- `HitTestResult`, `HitTestType`
- `LoadResult`, `NamedRangeScope`, `NamedRangeTargetType`
- `OperationResult`, `OperationsResponse`
- `RenderedColumn`, `RenderedRow`
- `ScrollPosition`, `ViewportBounds`
- `WorkerMessageType`

### TypeScript (apps/wasm/src/style-controls.ts)
- `getSelectedCellData` callback in `StyleControlsCallbacks` - replaced by `getEffectiveCellStyle` but parameter not removed

### C++ (apps/wasm/hello.cc)
- Demo WASM hello world file - no longer needed for development

### Documentation Inconsistency
- `docs/file-format.md` and `docs/persistence.md` document `#zcd v1` as the version header
- All test files and parser tests use `#cells v1`
- Parser treats `#` lines as comments, so both work, but should be consistent

### ZCD Test Files (testdata/*.zcd)
- All files use `#cells v1` comment header
- Documentation says `#zcd v1`
- Need to decide on canonical format and update accordingly

---

## Phase 1: Remove unused TypeScript types
- [x] 1a: Remove unused type exports from `apps/wasm/src/types.ts` - Removed 19 unused types: LoadResult, OperationResult, ApplyOperationResult, ApplyOperationsResult, CRDTOperation, OperationsResponse, CppPresence, GridCell, RenderedColumn, RenderedRow, ScrollPosition, ViewportBounds, HitTestType, HitTestResult, CollabStatus, ClientOptions, WorkerMessageType, WorkerMessage, WorkerResponse. Kept NamedRangeScope, NamedRangeTargetType, CppPeerInfo as they're used by other interfaces.

## Phase 2: Clean up StyleControls dead callback
- [x] 2a: Remove `getSelectedCellData` from `StyleControlsCallbacks` interface in `apps/wasm/src/style-controls.ts` - Removed unused callback and its invocation in init-components.ts. The callback was replaced by getEffectiveCellStyle which resolves the full style hierarchy.

## Phase 3: Remove hello.cc demo file
- [x] 3a: Remove `apps/wasm/hello.cc` and its BUILD target `hello_wasm` - Deleted the demo hello world WASM file and removed hello_wasm_bin and hello_wasm targets from BUILD.

## Phase 4: Standardize ZCD format header
- [x] 4a: Update testdata/*.zcd files to use `#zcd v1` consistently (matching documentation) - Updated 9 test files from `#cells v1` to `#zcd v1`.
- [x] 4b: Add `#zcd v1` header to `testdata/named_ranges.zcd` (was missing)

## Phase 5: Final verification
- [ ] 5a: Run tests to verify no regressions
- [ ] 5b: Run TypeScript type check
