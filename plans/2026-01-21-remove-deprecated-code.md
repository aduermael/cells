# Remove Deprecated and Legacy Code

Remove all deprecated code, backward compatibility shims, and legacy code paths from the codebase. The project is not released yet, so maintaining backward compatibility is unnecessary and adds complexity.

## Problem Statement

The codebase contains various deprecated patterns that should be removed:
1. Legacy DIM_* operations (replaced by COL_*/ROW_* operations)
2. Deprecated style system references (replaced by content-addressed styles)
3. Legacy formula resolver mode (`existingOnly=false`) used only for XLSX import
4. Deprecated Luau API methods (getRef() should be removed, use .ref property)
5. Deprecated bindings (createStyle returns deprecated message)
6. Legacy currency format constants (FMT_C0XX aliases)
7. Various "for backward compatibility" code paths and comments

## Implementation Phases

### Phase 1: Remove Legacy DIM_* Operations

The DIM_* operations (100-104) are deprecated, replaced by COL_*/ROW_* operations. Remove all code handling them.

- [x] 1a: Remove DIM_* enum values from `operation.h`
- [x] 1b: Remove DIM_* string conversions from `operation.cc`
- [x] 1c: Remove `applyDimInsertAxis`, `applyDimDeleteAxis`, `applyDimMoveAxis`, `applyDimResizeAxis`, `applyDimRenameAxis` from `crdt_axis.cc`
- [x] 1d: Remove DIM_* case statements from `crdt.cc` dispatch
- [x] 1e: Remove DIM_* declarations from `crdt_internal.h`
- [x] 1f: Update header comment in `crdt_axis.cc` to remove DIM_* reference
- [x] 1g: Run tests and fix any failures - updated `operation_test.cc` and `crdt_test.cc` to use COL_*/ROW_* operations

### Phase 2: Remove Formula Resolver Legacy Mode

The `existingOnly=false` parameter is only used for XLSX import. Replace with proper CRDT-compliant resolution.

- [x] 2a: Update `bindings_file.cc` XLSX import to use `getRequiredEntities()` + create entities + resolve instead of legacy mode
- [x] 2b: Updated 13 test files to use `existingOnly=true` with helper functions that create required entities before resolution
- [x] 2c: Remove the `existingOnly` parameter from `FormulaResolver::resolve()` - removed parameter, `_existingOnly` member, and all call sites using explicit `true`
- [x] 2d: Remove legacy entity auto-creation code from `formula_resolver.cc` - removed all `_existingOnly` branches that called `getOrCreate*` methods
- [x] 2e: Update `formula_resolver.h` documentation - removed "Legacy Resolution" section and updated comments
- [x] 2f: Run tests and fix any failures - all 313 E2E tests pass, all core unit tests pass

### Phase 3: Remove Deprecated Luau API Methods

- [x] 3a: Remove `luaCellGetRef()` method from `luau_types.cc` (users should use `.ref` property) - removed function definition and header declaration
- [x] 3b: Update Luau Cell metatable to not register `getRef` method - no change needed, method was never registered (only declared but unused)
- [x] 3c: Run tests and fix any failures - all tests pass, no tests were using getRef()

### Phase 4: Remove Deprecated Bindings and Dead Code

- [x] 4a: Remove or refactor `createStyle()` in `bindings_format.cc` (currently returns deprecated message) - removed from bindings_format.cc, bindings.h, bindings.cc, cells.d.ts, worker-types.ts, worker-handlers.ts, worker.ts, client.ts, wasm-data-source.ts, types.ts
- [x] 4b: Remove `exportToXLSX()` marked as deprecated in `cells.d.ts` (keep only `exportToXLSXPtr`) - removed from bindings_file.cc, bindings.h, bindings.cc, cells.d.ts, worker-types.ts
- [x] 4c: Remove "for backwards compatibility" re-exports in `grid-renderer.ts` - updated 10 files to import from grid-constants.js directly
- [x] 4d: Remove CollabState re-export "for backwards compatibility" in `cpp-sync-adapter.ts` and `collab-ui.ts` - renamed to use SyncState directly
- [x] 4e: Run tests and fix any failures - all 313 E2E tests pass, all core unit tests pass

### Phase 5: Remove Legacy Currency Format Aliases

- [x] 5a: Remove `CURRENCY_0` through `CURRENCY_4` legacy aliases from `number_format.h`
- [x] 5b: Remove legacy USD currency format ID constants from `number_format.cc` - also removed FMT_C0XX parsing from parseFormatId() and getFormatDetails()
- [x] 5c: Update any code still using the legacy aliases to use the new format IDs - updated input_parser.cc, number_format_test.cc, number_formatter_test.cc, input_parser_test.cc
- [x] 5d: Run tests and fix any failures - all 313 E2E tests pass, all core unit tests pass

### Phase 6: Clean Up Deprecated Comments and Documentation

- [x] 6a: Remove "for backward compatibility" comments from code that no longer has legacy paths - cleaned up operation.cc, formula_serializer.cc, types.ts, cells.d.ts, range_test.cc, helpers.mjs
- [x] 6b: Update `docs/networking.md` to remove "Legacy JS (deprecated)" row from the table
- [x] 6c: Remove "// legacy" comments from test files after updating them - updated formula_resolver_test.cc, number_format_test.cc
- [x] 6d: Update `formula_recalc.h` comment about backward compatibility - renamed to "convenience overload"
- [x] 6e: Remove deprecated flag from format_registry.h `findOrRegisterFormat` or remove the method entirely - replaced DEPRECATED with note about CRDT usage

### Phase 7: Remove Optional Fields Added for Backward Compatibility

- [x] 7a: Remove optional `border?` field comment "for backwards compatibility" from `types.ts` and `cells.d.ts` - no action needed, comment just says "(optional)" which is legitimate documentation
- [x] 7b: Remove `styleId?` deprecated field from `types.ts` (content-addressed styles) - removed from CellData in types.ts/cells.d.ts, StyleRangeInfo, clipboard.ts, client.ts, and wasm-data-source.ts return types
- [x] 7c: Clean up any remaining "backwards compat" code paths in operation parsing (`operation.cc` lines 268-293) - updated operation.h constructor comment from "backwards compatible" to "for workbook-level operations"

### Phase 8: Final Cleanup and Validation

- [x] 8a: Run full test suite (unit tests + E2E tests) - all 56 unit tests and 313 E2E tests pass
- [x] 8b: Run linter and fix any issues - lint check passed
- [x] 8c: Search for remaining "deprecated", "legacy", "backward" references and address any missed items - cleaned up format-controls.ts (removed dead FMT_C code), parser.cc/h (updated "legacy style ID" comments), crdt.h (updated "legacy payloads" comment), formula_display.cc (changed "Legacy" to "Fallback")
- [x] 8d: Build all targets to ensure no compilation errors - WASM dist build successful

## Files to Modify

**Core files:**
- `core/cells/operation.h` - Remove DIM_* enum values
- `core/cells/operation.cc` - Remove DIM_* string conversions, simplify parsing
- `core/cells/crdt.cc` - Remove DIM_* dispatch
- `core/cells/crdt_axis.cc` - Remove applyDim* functions
- `core/cells/crdt_internal.h` - Remove DIM_* declarations
- `core/cells/formula_resolver.h` - Remove existingOnly parameter
- `core/cells/formula_resolver.cc` - Remove legacy mode code
- `core/cells/formula_recalc.h` - Update comment
- `core/cells/luau_types.cc` - Remove luaCellGetRef
- `core/cells/number_format.h` - Remove legacy aliases
- `core/cells/number_format.cc` - Remove legacy format IDs
- `core/cells/format_registry.h` - Remove deprecated method or comment

**Bindings:**
- `apps/wasm/bindings_format.cc` - Remove createStyle deprecated stub
- `apps/wasm/bindings_file.cc` - Update to CRDT-compliant resolution
- `apps/wasm/cells.d.ts` - Remove deprecated exports, clean up comments
- `apps/wasm/src/types.ts` - Remove deprecated fields/comments
- `apps/wasm/src/grid-renderer.ts` - Remove backwards compat re-exports
- `apps/wasm/src/cpp-sync-adapter.ts` - Remove CollabState re-export
- `apps/wasm/src/collab-ui.ts` - Remove backwards compat comment

**Test files (update to use new APIs):**
- `core/cells/formula_resolver_test.cc` - Remove legacy mode usage
- `core/cells/formula_eval_test.cc` - Remove legacy mode usage
- `core/cells/formula_integration_test.cc` - Remove legacy mode usage
- `core/cells/formula_recalc_test.cc` - Remove legacy mode usage
- `core/cells/formula_serializer_test.cc` - Remove legacy mode usage
- `core/cells/formula_move_test.cc` - Remove legacy mode usage
- `core/cells/formula_functions_test.cc` - Remove legacy mode usage
- `core/cells/dependency_graph_test.cc` - Remove legacy mode usage
- `core/cells/fill_range_test.cc` - Remove legacy mode usage
- `core/cells/functions/fn_rand_test.cc` - Remove legacy mode usage
- `core/cells/functions/fn_array_test.cc` - Remove legacy mode usage
- `core/cells/functions/fn_stats_test.cc` - Remove legacy mode usage
- `core/cells/functions/fn_lookup_test.cc` - Remove legacy mode usage

**Documentation:**
- `docs/networking.md` - Remove deprecated JS reference

## Notes

- The deprecated JS files mentioned in `docs/networking.md` (`webrtc-manager.js`, `signaling-client.js`, `collab-manager.js`) do not exist in the repository - they were already removed
- The `deprecated` field in autocomplete suggestions (`luau_autocomplete.h/cc`) is intentional for marking Luau API methods as deprecated to users - this should NOT be removed
- Excel date leap year bug in `fn_datetime.cc` is for Excel compatibility, not backward compatibility - this should NOT be removed
- The `fn_text.cc` CONCATENATE "legacy" description refers to Excel's legacy function, not our code - this should NOT be removed
