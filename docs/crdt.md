# CRDT & Collaboration

## Core Principle: All Mutations Through CRDT Operations

**ALL workbook mutations MUST go through CRDT operations.** This is not optional.

This architectural constraint ensures:
1. **Collaboration by design** - Every change is automatically syncable
2. **No special cases** - Same code path for local edits and remote syncs
3. **Deterministic state** - State is always derivable from operation log
4. **Clean abstractions** - UI layer never directly mutates the model

```
❌ WRONG: sheet->setCellValue(cellId, "hello");
✅ RIGHT: applyOperation(SetValueOp{cellId, "hello", hlc});
```

## Implementation Status

| Component | Status | Source Files |
|-----------|--------|--------------|
| Hybrid Logical Clock | Implemented | `core/cells/hlc.h`, `hlc.cc` |
| CRDT operations | Implemented | `core/cells/crdt.h`, `crdt.cc`, `crdt_cell.cc`, `crdt_axis.cc`, `crdt_range.cc` |
| Operation types | Implemented | `core/cells/operation.h`, `operation.cc` |
| Operation log (OpLog) | Implemented | `core/cells/oplog.h`, `oplog.cc` |
| Sync manager | Implemented | `core/cells/sync_manager.h`, `sync_manager.cc` |
| WebRTC P2P sync | Implemented | `core/net/` |
| Presence/cursors | Implemented | `core/net/include/Presence.h`, `core/net/common/Presence.cc` |
| Undo/redo | Not implemented | — |

## Overview

CRDTs (Conflict-free Replicated Data Types) enable real-time collaboration without a central server arbitrating conflicts. Each client can make changes locally, and changes merge deterministically.

```
┌─────────────────────────────────────────────────────────────────┐
│                         Client A                                 │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────────┐  │
│  │ Local State │◄──►│ CRDT Engine │◄──►│ Operation Log       │  │
│  └─────────────┘    └─────────────┘    └─────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼ Sync operations
                      ┌───────────────┐
                      │ Sync (P2P or  │
                      │    server)    │
                      └───────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                         Client B                                 │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────────┐  │
│  │ Local State │◄──►│ CRDT Engine │◄──►│ Operation Log       │  │
│  └─────────────┘    └─────────────┘    └─────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## Why CRDTs for Spreadsheets?

1. **Offline-first**: Users can edit without connectivity
2. **Low latency**: No round-trip to server for each edit
3. **Conflict resolution**: Concurrent edits merge automatically
4. **History**: All operations are preserved (undo/time-travel)

## Hybrid Logical Clock (HLC)

For operation ordering, we use HLC which combines physical time with a logical counter:

```
HLC: 1705312200000.0.N3f8hJ2w
     └─timestamp──┘ │ └─node─┘
                    └─logical counter
```

- `wall_time`: Physical time in milliseconds
- `logical`: Counter for same wall_time
- `node_id`: Tie-breaker for identical HLC

Comparison: wall_time → logical → node_id (lexicographic)

## Operation Types

Operations follow a unified **SET + DELETE** pattern:
- **SET** creates the entity if it doesn't exist, updates only the properties provided in the payload
- **DELETE** marks the entity as deleted (can be resurrected by a later SET)

### Cell Operations

| Operation | Code | Description |
|-----------|------|-------------|
| `CELL_SET` | 0 | Create/update cell (col, row, type, value, style, format) |
| `CELL_DELETE` | 1 | Delete/clear cell |

### Column Operations

| Operation | Code | Description |
|-----------|------|-------------|
| `COL_SET` | 10 | Create/update column (position, size, name, style, format, hidden) |
| `COL_DELETE` | 11 | Delete column |

### Row Operations

| Operation | Code | Description |
|-----------|------|-------------|
| `ROW_SET` | 20 | Create/update row (position, size, style, format, hidden) |
| `ROW_DELETE` | 21 | Delete row |

### Sheet Operations

| Operation | Code | Description |
|-----------|------|-------------|
| `SHEET_SET` | 30 | Create/update sheet (name, position) |
| `SHEET_DELETE` | 31 | Delete sheet |

### Workbook Operations

| Operation | Code | Description |
|-----------|------|-------------|
| `WORKBOOK_SET` | 40 | Update workbook properties (name) |

### Named Range Operations

| Operation | Code | Description |
|-----------|------|-------------|
| `NAMED_RANGE_SET` | 50 | Create/update named range |
| `NAMED_RANGE_DELETE` | 51 | Delete named range |

### Range Operations (style/format ranges)

| Operation | Code | Description |
|-----------|------|-------------|
| `RANGE_SET` | 60 | Create/update range (corners, flags, style, format) |
| `RANGE_DELETE` | 61 | Delete range

## Conflict Resolution Rules

### Cell Value Conflicts (Last-Writer-Wins)

When two clients edit the same cell concurrently, higher HLC wins.

### Axis Insert Conflicts (Interleaving)

When two clients insert at the same position, order by HLC. Lower HLC comes first (stable ordering).

### Delete vs Edit Conflicts

When one client deletes an axis while another edits a cell in it:
- **Recommendation**: Edit resurrects the axis (no data loss)
- Show visual indicator that axis was "undeleted"

## Operation Log (OpLog)

All operations are stored in an append-only log with efficient indexes:

| Field | Type | Purpose |
|-------|------|---------|
| `_operations` | `vector<Operation>` | Operations in HLC order |
| `_by_entity` | `map<ID, vector<size_t>>` | Entity ID → operation indices |
| `_hlc_index` | `map<string, size_t>` | HLC string → index (deduplication) |

### Key Operations

| Method | Description |
|--------|-------------|
| `addOperation(op)` | Add operation, returns false if duplicate |
| `getOperationsSince(hlc)` | Get all ops after given HLC |
| `getOperationsForEntity(id)` | Get all ops for an entity |
| `getLatestOperationForEntity(id)` | Get most recent op for entity |
| `pruneOperationsBefore(hlc)` | GC old acknowledged ops |

### OpLog Garbage Collection

Prune old operations that all peers have acknowledged:
- `pruneOperationsBefore(threshold)` - remove ops with HLC ≤ threshold
- `pruneKeeping(minToKeep)` - keep at least N most recent ops
- `pruneBeforeKeeping(threshold, min)` - combine both strategies

## Undo/Redo

Undo/redo is not yet implemented. The planned approach is branch-based history where undo operations are regular CRDT operations that get synced to peers.

## Presence & Cursors

Presence is ephemeral (not persisted):
- User ID, name, color
- Current sheet
- Selection/cursor position
- Viewport center (for "follow" feature)

Sent frequently (~10/sec), separate from OpLog.

## Performance Considerations

1. **Operation batching**: Group rapid changes into single op
2. **Selective sync**: Sync visible sheet's ops first
3. **Compression**: Ops are highly compressible
4. **Delta sync**: After initial sync, only send new ops
5. **Peer-to-peer**: Skip server for local network

## Security Considerations

1. **Operation validation**: Validate ops before applying
2. **Access control**: Per-sheet permissions
3. **Rate limiting**: Prevent op flooding
4. **Signatures**: Optional op signing for audit

## Implementation Notes

The CRDT implementation is **custom-built** for spreadsheet operations:

- **Cell-level LWW** (Last-Writer-Wins) - simpler than text CRDTs
- **Operation log** - append-only, indexes by cell/axis ID
- **HLC ordering** - deterministic conflict resolution
- **P2P sync** - WebRTC with signaling server for setup only

This approach is simpler and more efficient than general-purpose CRDT libraries (Automerge, Yjs) because spreadsheet operations are discrete cell/axis mutations, not character-by-character text edits.
