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
| CRDT operations | Implemented | `core/cells/crdt.h`, `crdt.cc` |
| Operation types | Implemented | `core/cells/operation.h`, `operation.cc` |
| Operation log (OpLog) | Implemented | `core/cells/oplog.h`, `oplog.cc` |
| Sync manager | Implemented | `core/cells/sync_manager.h`, `sync_manager.cc` |
| WebRTC P2P sync | Implemented | `core/net/` |
| Presence/cursors | Implemented | `core/net/common/Presence.cc` |
| Branch-based undo/redo | Planned | — |

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

### Cell Operations

| Operation | Description |
|-----------|-------------|
| `CELL_SET_VALUE` | Set cell value/formula |
| `CELL_CLEAR` | Clear cell |
| `CELL_SET_STYLE` | Change formatting |

### Dimension Operations

| Operation | Description |
|-----------|-------------|
| `DIM_INSERT_AXIS` | Insert column/row |
| `DIM_DELETE_AXIS` | Delete column/row |
| `DIM_MOVE_AXIS` | Reorder |
| `DIM_RESIZE_AXIS` | Change width/height |

### Sheet Operations

| Operation | Description |
|-----------|-------------|
| `SHEET_CREATE` | Create sheet |
| `SHEET_DELETE` | Delete sheet |
| `SHEET_RENAME` | Rename sheet |

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

All operations are stored in an append-only log:

| Field | Purpose |
|-------|---------|
| `log_file` | Append-only persistence |
| `by_cell_id` | Index for cell history |
| `by_axis_id` | Index for axis history |
| `last_synced` | Last operation sent to peers |

### OpLog Compaction

Over time, compress old operations:
- Only compact operations all peers have seen
- Merge consecutive operations on same cell
- Snapshot old state

## Undo/Redo: Branch-Based History

We use branch-based undo/redo - undo operations are just more operations that get synced.

### Why Branch-Based?

1. **Git-friendly**: Undo operations visible in file diffs
2. **CRDT-native**: No special "inverse" logic
3. **Collaborative**: Other users see the undo
4. **Time-travel**: Can view state at any point
5. **No data loss**: All states preserved

### How It Works

1. Track checkpoints (HLC timestamps) after each user action
2. Undo: Generate reverse operations for everything since last checkpoint
3. Redo: Replay the operations that were undone
4. Reverse operations are broadcast to all peers

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
