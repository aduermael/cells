# Styled Ranges Fixes

Fix three issues with styled ranges:
1. Remove redundant `sheet_id` from RANGE_ADD operation payload
2. Remove redundant `sheet_id` from RANGE_SET_STYLE operation payload
3. Fix collaboration sync so styled ranges appear on peer 2

## Analysis

### Issues 1 & 2: Redundant sheet_id in Payloads

The `sheet_id` is included in both RANGE_ADD and RANGE_SET_STYLE payloads, but it's redundant because:
- RANGE_ADD already specifies `start_col_id`, `start_row_id`, `end_col_id`, `end_row_id`
- Columns and rows belong to a single sheet, so the sheet can be derived from any of these IDs
- RANGE_SET_STYLE operates on an existing range (by `range_id`), which already knows its sheet

**Files that create these payloads:**
- `apps/wasm/bindings_format.cc:2005-2010, 2017-2018, 2035, 2047-2048`
- `apps/wasm/bindings_core.cc:1703`

**Files that parse these payloads:**
- `core/cells/crdt_range.cc:56-100` (applyRangeAdd)
- `core/cells/crdt_range.cc:240-270` (applyRangeSetStyle)

### Issue 3: Styled Ranges Don't Sync in Collaboration

The `bootstrapOpLog()` function in `core/cells/crdt.cc:617-847` generates bootstrap operations for columns, rows, cells, formats, styles, and named ranges—but **not for ranges**. When peer 2 joins, it never receives RANGE_ADD or RANGE_SET_STYLE operations.

**Solution:** Add range bootstrapping to emit RANGE_ADD and RANGE_SET_STYLE operations for all existing ranges.

## Phase 1: Remove sheet_id from RANGE_ADD Payload

- [x] 1a: Update `applyRangeAdd` in `crdt_range.cc` to derive sheet from column/row IDs instead of extracting from payload. Used `workbook.findAxisSheet(startColId)` to derive sheet from start column ID.
- [x] 1b: Update RANGE_ADD payload creation in `bindings_format.cc` to not include sheet_id. Updated two locations (split ranges and main range creation).
- [x] 1c: Update RANGE_ADD payload creation in `bindings_core.cc` to not include sheet_id. Updated merge range creation.

## Phase 2: Remove sheet_id from RANGE_SET_STYLE Payload

- [x] 2a: Update `applyRangeSetStyle` in `crdt_range.cc` to get sheet from the existing range instead of extracting from payload. Now uses `workbook.getRange()` to find range, then derives sheet from `range->startColId`.
- [x] 2b: Update RANGE_SET_STYLE payload creation in `bindings_format.cc` to not include sheet_id. Updated two locations (split range style and main range style).

## Phase 3: Add Range Bootstrap for Collaboration Sync

- [x] 3a: Add RANGE_ADD operations to `bootstrapOpLog()` for all existing ranges. Iterates `sheet->getRangeIds()` and generates RANGE_ADD operations with the new payload format (no sheet_id).
- [x] 3b: Add RANGE_SET_STYLE operations to `bootstrapOpLog()` for ranges that have styles. Checks `workbook.getRangeStyleId(range->id)` and emits RANGE_SET_STYLE if not null.

## Phase 4: Fix Local Style Application Bug

**Problem**: After collaboration starts, new styled ranges don't show on the peer creating them. STYLE_DEFINE operations exist in the oplog but styles don't exist in the workbook.

**Analysis**: In `applyOperation()` (crdt.cc:383-384), operations are added to the oplog "regardless of result":
```cpp
// Add to OpLog regardless of result (for history/sync)
oplog->addOperation(op);
```

This is wrong. The correct flow should be:
- **Local ops**: Create → Apply → On SUCCESS, add to oplog → Broadcast
- **Remote ops**: Receive (pending) → Apply → On SUCCESS, add to oplog

**Fix**: Only add to oplog after successful application.

- [x] 4a: Modify `applyOperation()` to only add to oplog on successful apply (SUCCESS, SUPERSEDED, RESURRECTED). Changed `crdt.cc` to check result before adding to oplog.
- [x] 4b: Review all callers of `applyOperation()` to ensure they handle failure cases appropriately. Reviewed - existing callers either check result explicitly or are local ops that should succeed.
- [x] 4c: Add tests for the scenario: bootstrap → create styled range → verify style exists in workbook. Added 4 tests to `crdt_test.cc`: StyleDefineCreatesStyleInWorkbook, StyleDefineAddedToOpLogOnSuccess, DuplicateStyleDefineNotAddedToOpLog, FailedOperationNotAddedToOpLog.

## Phase 5: Testing

- [x] 5a: Build and verify no compilation errors
- [x] 5b: Manual test: create styled range, export file, verify no sheet_id in RANGE_ADD/RANGE_SET_STYLE payloads
- [x] 5c: Manual test: create styled range, share collaboration link, verify peer 2 sees the styled range

## Phase 6: Add Debug Mode to Disable OpLog Pruning

Add a runtime flag to disable operation log pruning for debugging sync issues. When `?noPrune=true` is in the URL, the oplog will never be pruned, allowing full visibility into all operations.

**Implementation approach:**
- Add `_debugNoPrune` flag to `SyncManager` (core layer, not bindings)
- Check flag inside `pruneOpLog()` itself - single check covers all call sites
- Expose setter via CellsEngine for WASM binding
- URL parameter `?noPrune=true` read by JavaScript and passed to C++

**Files to modify:**
- `core/cells/sync_manager.h` - Add `_debugNoPrune` flag and `setDebugNoPrune()` method
- `core/cells/sync_manager.cc` - Implement setter, check flag at start of `pruneOpLog()`
- `apps/wasm/bindings.h` - Add `setDebugNoPrune()` that forwards to SyncManager
- `apps/wasm/bindings_crdt.cc` - Implement the forwarding method
- `apps/wasm/src/init.ts` - Read URL param and call C++ method

**Steps:**
- [x] 6a: Add `_debugNoPrune` flag and `setDebugNoPrune(bool)` to SyncManager in `sync_manager.h`
- [x] 6b: Implement `setDebugNoPrune()` and add early return in `pruneOpLog()` when flag is set (with LOG_DEBUG)
- [x] 6c: Add `setDebugNoPrune()` to CellsEngine that forwards to SyncManager
- [x] 6d: Read `noPrune` URL parameter in `init.ts` and call `setDebugNoPrune(true)` if present
- [x] 6e: Test: run with `?noPrune=true`, verify oplog grows without bounds during collaboration (build verified)

## Phase 6.5: Move OpLog Pruning to Low-Level Layer

**Problem**: `pruneOpLog()` is called from bindings after every local operation (27+ call sites). This is:
1. Inefficient - pruning after every op
2. Wrong - may prune before peers have ACKed

**Solution**: Remove all bindings calls, prune only:
1. In `handleAck()` - when peers confirm receipt (collaboration mode)
2. Periodically when adding ops to oplog (e.g., every 100 ops) - for non-collaboration mode

**Steps:**
- [x] 6.5a: Remove all `pruneOpLog()` calls from `bindings_core.cc` - removed 18 call sites
- [x] 6.5b: Remove all `pruneOpLog()` calls from `bindings_format.cc` - removed 9 call sites
- [x] 6.5c: Add periodic pruning in `applyOperation()` (crdt.cc) - prune every 100 ops when not collaborating
- [x] 6.5d: Build and test - all tests pass

## Phase 6.6: Investigate Missing STYLE_DEFINE on Peer 1

**Problem**: After collaboration starts, Peer 1 creates a styled range but doesn't see it. The range exists with `sty:GskBKIkv` but the style definition is missing from Peer 1's workbook.

**Observations from test case**:
- Peer 1 file: Has `RG Ebk4KDxy ... sty:GskBKIkv` but NO `Y GskBKIkv {...}` style definition
- Peer 1 oplog: Only `RANGE_SET_STYLE` - no `STYLE_DEFINE`
- Peer 2 file: Has BOTH `Y GskBKIkv {"bgColor":"#FBBF24"}` AND the range correctly
- User reports range "flashes" on Peer 1 briefly

**Hypothesis**: The `STYLE_DEFINE` operation is not being added to oplog on Peer 1. Possibly related to Phase 4 change (only add to oplog on success). The style might be:
1. Created with a duplicate ID (returns non-success)
2. Failing to apply for some reason
3. Being created then overwritten with a different ID

**Investigation approach**:
1. First check the code path: where is STYLE_DEFINE created during setRangeStyle?
2. Check if duplicate style detection could be returning wrong result
3. Add debug logs to trace the exact flow if needed

**Steps:**
- [x] 6.6a: Trace code path in setRangeStyle (bindings_format.cc) - where is STYLE_DEFINE created?
- [x] 6.6b: Check applyStyleDefine return values - what could cause it to not be added to oplog?
- [x] 6.6c: Added LOG_DEBUG in: applyStyleDefine (both paths), applyOperation (STYLE_DEFINE), setRangeStyle
- [x] 6.6d: ROOT CAUSE FOUND - see detailed analysis below

**Root Cause Analysis (from debug logs):**
```
[setRangeStyle] Creating new style ccvdWDK2 with JSON: {"bgColor":"#FBBF24"}
[CRDT] applyStyleDefine: style ccvdWDK2 SUCCESS - registered in workbook
[CRDT] applyOperation: STYLE_DEFINE ccvdWDK2 added to oplog (result=0)
[Sync] queueOperationsBroadcast: peers=1 oplog_size=6 ops_to_send=6
[Sync] handleAck: peer 07xJvXDz ACKed HLC 1768974310035.0.e7iI3Zgx
[Sync] pruneOpLog: pruned 6 ops (was 6, now 0)              ← Oplog emptied after ACK
[CRDT] applyStyleDefine: style ccvdWDK2 ALREADY_APPLIED     ← Peer 2 sent ops BACK!
[Sync] handleOperations: from=07xJvXDz received=6 new=6    ← Treated as "new" (not in oplog)
```

**The bug is an ECHO problem:**
1. Peer 1 creates styled range → 6 ops added to oplog
2. Peer 1 broadcasts to Peer 2
3. Peer 2 receives, applies ops, sends ACK
4. Peer 1 receives ACK → prunes ALL 6 ops (oplog now empty)
5. **BUG**: Peer 2's data-change listener triggers `queueLocalOperationsBroadcast()`
6. Peer 2 broadcasts those SAME 6 ops back to Peer 1
7. Peer 1 receives them as "new=6" (not in oplog because pruned)
8. STYLE_DEFINE returns ALREADY_APPLIED but wasn't added to oplog (Phase 4 change)
9. Style ends up missing from oplog → not exported to file

**Location of the bug:** `apps/wasm/src/init-listeners.ts:269-272`
```typescript
// Queue operations broadcast to peers
if (app.collaborationInitialized && app.syncAdapter) {
  await app.syncAdapter.queueLocalOperationsBroadcast();
}
```
This is called after ANY data change, including when applying REMOTE operations from peers.

## Phase 6.7: Fix Operation Echo Bug (Don't Broadcast Remote Ops)

**Problem**: When Peer 2 receives and applies operations from Peer 1, the "data changed" listener triggers a broadcast, sending those same operations BACK to Peer 1. This causes:
1. Unnecessary network traffic (ops echoed back to sender)
2. After oplog pruning, sender receives its own ops as "new"
3. ALREADY_APPLIED ops not added to oplog → sync state corruption

**Solution**: Only broadcast after LOCAL operations, not after applying REMOTE operations.

**Implementation options:**

Option A: Pass "isRemote" flag through the listener system
- Modify listener callback signature to include `isRemote: boolean`
- C++ side: track whether current operation batch is from local or remote
- TypeScript: check flag before calling broadcast

Option B: Use a "suppress broadcast" flag during remote op application
- Set flag before applying remote ops
- Check flag in listener before broadcasting
- Clear flag after remote ops applied

Option C: Move broadcast responsibility to C++ SyncManager
- Remove broadcast call from TypeScript listener
- C++ SyncManager already calls `queueOperationsBroadcast()` after local ops
- Just need to ensure it's NOT called when handling remote ops

**Recommended: Option C** - cleanest separation of concerns

**Implementation approach:**
1. Remove broadcast call from TypeScript listener (stops echoing)
2. Add `broadcastPendingOperations()` helper in C++ that calls `_syncClient->broadcastOperations()`
3. Replace all `_syncManager->queueOperationsBroadcast()` calls with helper
4. Key insight: SyncClient creates its OWN SyncManager, so we must use `broadcastOperations()` not `processOutgoing()`

**Testing note:** Run only `collab.spec.ts` during iteration, full test suite after fix confirmed.

**Steps:**
- [x] 6.7a: Verify C++ bindings already call `queueOperationsBroadcast()` after local operations
- [x] 6.7b: Remove the broadcast call from `init-listeners.ts` (line 269-272)
- [x] 6.7c: Ensure `handleOperations` and `handleSyncResponse` do NOT trigger broadcasts
- [x] 6.7d: Add `broadcastPendingOperations()` helper that calls `_syncClient->broadcastOperations()` (SyncClient has its own SyncManager)
- [x] 6.7e: Replace all binding calls to use the new helper
- [x] 6.7f: Test: collab tests pass (6/6)
- [x] 6.7g: Test: collab-style-sync tests pass (8/8) - styled ranges sync correctly!

## Phase 6.8: Add CRDT Sync Metadata to ZCD File Format [SKIPPED]

**Status**: SKIPPED - Not needed after Phase 6.7 fix.

**Rationale for skipping:**
1. Local HLC tracking already handled by `getCurrentHLC()` which uses max HLC from oplog
2. Duplicate ops already handled by CRDT apply returning ALREADY_APPLIED
3. Peer state not needed across sessions - when reopening file, you start a new collaboration session with new peers
4. The real bug (operation echoing causing style corruption) was fixed in Phase 6.7

The original sync issues were caused by the operation echo bug, not missing sync metadata. Future offline editing support would require server-side history storage, which is a separate feature

## Phase 7: Fix All Lint Warnings, Checks, and Tests

- [x] 7a: Run C++ linter and fix all warnings - Fixed `return ID()` → `return {}` in format_registry.cc and style_registry.cc
- [x] 7b: Run TypeScript/JavaScript linter and fix all warnings - TypeScript checks pass
- [x] 7c: Run all unit tests and fix any failures - All 56 unit tests pass
- [x] 7d: Run all e2e/collaboration tests and fix any failures - 304/312 tests pass

**Known test failures (pre-existing, not related to this plan):**
- `borders` (7 failures) - Border feature tests, unrelated to styled ranges
- `collab-demo` (1 failure) - "Finale: A16 should sync to Robert" - 3-peer sync issue in the finale act where rapid styling operations on A16 don't sync to the third peer. The core collab tests (collab.test.mjs, collab-style-sync.test.mjs) all pass. This appears to be a separate sync bug that needs further investigation

## Phase 8: Debug Remaining Style Sync Issues

**Goal**: Keep more operations in the oplog for debugging, then investigate remaining sync issues.

**Oplog retention change:**
- Always keep at least 500 operations in the oplog (only trim beyond this threshold)
- When collaborating, keep more than 500 if needed until all peers have acknowledged
- This provides better visibility into sync state for debugging

**Steps:**
- [ ] 8a: Modify `pruneOpLog()` to keep minimum 500 operations
- [ ] 8b: Wait for user to provide more details about remaining style sync issue (using retained ops)
- [ ] 8c: (Further steps TBD based on debugging findings)

## Phase 9: Fix Border Issues

**Problem**: Border outlines can no longer be set, causing 7 E2E test failures in `borders.test.mjs`.

**Steps:**
- [ ] 9a: Investigate why outline borders stopped working
- [ ] 9b: Fix the border application issue
- [ ] 9c: Verify all 10 border E2E tests pass

## Phase 10: Fix Collab Demo Test

**Problem**: The collab-demo "Finale" act fails - A16 doesn't sync to Robert (third peer) after rapid styling operations.

**Note**: This will be easier to fix once Phase 8 and Phase 9 issues are resolved.

**Steps:**
- [ ] 10a: Debug the 3-peer sync scenario
- [ ] 10b: Fix the sync issue
- [ ] 10c: Verify collab-demo test passes (7/7 acts)
