# CRDT & Collaboration

## Overview

CRDTs (Conflict-free Replicated Data Types) enable real-time collaboration without a central server arbitrating conflicts. Each client can make changes locally, and changes merge deterministically.

## Why CRDTs for Spreadsheets?

1. **Offline-first**: Users can edit without connectivity
2. **Low latency**: No round-trip to server for each edit
3. **Conflict resolution**: Concurrent edits merge automatically
4. **History**: All operations are preserved (undo/time-travel)

## CRDT Architecture

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
                      │ Sync Server   │  (or P2P)
                      │ (relay only)  │
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

## Hybrid Logical Clock (HLC)

For operation ordering:

```c
typedef struct HLC {
    uint64_t wall_time;    // Physical time (ms since epoch)
    uint32_t logical;      // Logical counter for same wall_time
    uuid_t node_id;        // Tie-breaker for same HLC
} HLC;

HLC hlc_now(HLC* last) {
    uint64_t now = get_wall_time_ms();
    if (now > last->wall_time) {
        return (HLC){.wall_time = now, .logical = 0, .node_id = NODE_ID};
    } else {
        return (HLC){
            .wall_time = last->wall_time,
            .logical = last->logical + 1,
            .node_id = NODE_ID
        };
    }
}

int hlc_compare(HLC a, HLC b) {
    if (a.wall_time != b.wall_time) return a.wall_time - b.wall_time;
    if (a.logical != b.logical) return a.logical - b.logical;
    return uuid_compare(a.node_id, b.node_id);
}
```

## Operation Types

### Cell Operations

```c
typedef enum CellOpType {
    CELL_OP_SET_VALUE,       // Set cell value/formula
    CELL_OP_CLEAR,           // Clear cell
    CELL_OP_SET_STYLE,       // Change formatting
} CellOpType;

typedef struct CellOp {
    uuid_t cell_id;          // Target cell (created if doesn't exist)
    CellOpType type;
    HLC timestamp;

    union {
        struct {
            CellValueType value_type;
            CellValue value;
            char* formula;        // NULL if not a formula
        } set_value;

        struct {
            StyleDelta* delta;    // Only changed properties
        } set_style;
    };
} CellOp;
```

### Dimension Operations

```c
typedef enum DimOpType {
    DIM_OP_INSERT_AXIS,      // Insert column/row
    DIM_OP_DELETE_AXIS,      // Delete column/row
    DIM_OP_MOVE_AXIS,        // Reorder
    DIM_OP_RESIZE_AXIS,      // Change width/height
    DIM_OP_RENAME_AXIS,      // Change name
    DIM_OP_HIDE_AXIS,        // Toggle visibility
} DimOpType;

typedef struct DimOp {
    uint32_t dimension;      // 0=col, 1=row, ...
    DimOpType type;
    HLC timestamp;

    union {
        struct {
            uuid_t new_axis_id;
            uuid_t after_axis;   // Insert after this (NULL = start)
        } insert;

        struct {
            uuid_t axis_id;
            // Tombstone - keeps ID for conflict resolution
        } delete;

        struct {
            uuid_t axis_id;
            uuid_t after_axis;
        } move;

        struct {
            uuid_t axis_id;
            uint32_t new_size;
        } resize;
    };
} DimOp;
```

### Sheet Operations

```c
typedef enum SheetOpType {
    SHEET_OP_CREATE,
    SHEET_OP_DELETE,
    SHEET_OP_RENAME,
    SHEET_OP_REORDER,
} SheetOpType;
```

## Conflict Resolution Rules

### Cell Value Conflicts (Last-Writer-Wins)

When two clients edit the same cell concurrently:

```c
CellValue resolve_cell_conflict(CellOp* op_a, CellOp* op_b) {
    // Higher HLC wins
    if (hlc_compare(op_a->timestamp, op_b->timestamp) > 0) {
        return op_a->set_value.value;
    } else {
        return op_b->set_value.value;
    }
}
```

### Axis Insert Conflicts (Interleaving)

When two clients insert at the same position:

```c
void resolve_insert_conflict(DimOp* op_a, DimOp* op_b, Dimension* dim) {
    // Both insert after same axis - order by HLC
    // Lower HLC comes first (stable ordering)
    if (hlc_compare(op_a->timestamp, op_b->timestamp) < 0) {
        // A, then B
        insert_axis(dim, op_a->insert.new_axis_id, op_a->insert.after_axis);
        insert_axis(dim, op_b->insert.new_axis_id, op_a->insert.new_axis_id);
    } else {
        // B, then A
        insert_axis(dim, op_b->insert.new_axis_id, op_b->insert.after_axis);
        insert_axis(dim, op_a->insert.new_axis_id, op_b->insert.new_axis_id);
    }
}
```

### Delete vs Edit Conflicts

When one client deletes an axis while another edits a cell in it:

**Option A**: Delete wins (data loss)
**Option B**: Edit resurrects (no data loss, might confuse user)
**Option C**: Move to "orphaned" area (preserves data, shows conflict)

Recommended: **Option B** with visual indicator that axis was "undeleted"

```c
void resolve_delete_edit_conflict(DimOp* delete_op, CellOp* edit_op,
                                  Sheet* sheet) {
    if (hlc_compare(edit_op->timestamp, delete_op->timestamp) > 0) {
        // Edit is newer - resurrect the axis
        resurrect_axis(sheet, delete_op->delete.axis_id);
        apply_cell_op(sheet, edit_op);
    } else {
        // Delete is newer - cell edit becomes orphaned
        // Store in orphan list for potential recovery
        orphan_cell_op(sheet, edit_op);
    }
}
```

## Operation Log (OpLog)

All operations are stored in an append-only log:

```c
typedef struct OpLog {
    // Persistent storage
    FILE* log_file;           // Append-only file

    // In-memory index
    HashMap* by_cell_id;      // cell_id -> list of ops
    HashMap* by_axis_id;      // axis_id -> list of ops

    // Sync state
    HLC last_synced;          // Last operation sent to server
    uint64_t local_seq;       // Local sequence number
} OpLog;

void oplog_append(OpLog* log, Operation* op) {
    // 1. Write to file (for persistence)
    serialize_op(log->log_file, op);
    fflush(log->log_file);

    // 2. Update index
    index_operation(log, op);

    // 3. Mark for sync
    queue_for_sync(op);
}
```

### OpLog Compaction

Over time, compress old operations:

```c
void oplog_compact(OpLog* log, HLC before) {
    // Only compact operations that all peers have seen
    if (!all_peers_synced_to(before)) return;

    // Merge consecutive operations on same cell
    // Remove delete-then-insert pairs
    // Snapshot old state
}
```

## State Reconstruction

Build current state from operations:

```c
Sheet* reconstruct_from_oplog(OpLog* log) {
    Sheet* sheet = sheet_new();

    // Replay all operations in HLC order
    Operation** ops = oplog_get_all_sorted(log);
    for (int i = 0; i < array_len(ops); i++) {
        apply_operation(sheet, ops[i]);
    }

    return sheet;
}
```

### Incremental Application

For real-time updates, apply single operations:

```c
void apply_operation(Sheet* sheet, Operation* op) {
    switch (op->type) {
        case OP_CELL:
            apply_cell_op(sheet, &op->cell);
            break;
        case OP_DIMENSION:
            apply_dim_op(sheet, &op->dim);
            break;
        // ...
    }
}
```

## Sync Protocol

### Message Types

```c
typedef enum SyncMsgType {
    SYNC_HELLO,              // Initial handshake
    SYNC_OPERATIONS,         // Batch of operations
    SYNC_ACK,                // Acknowledge receipt
    SYNC_REQUEST_RANGE,      // Request ops in HLC range
    SYNC_SNAPSHOT,           // Full state snapshot
} SyncMsgType;

typedef struct SyncMsg {
    SyncMsgType type;
    uuid_t sender_id;
    HLC sender_clock;

    union {
        struct {
            Operation** ops;
            int count;
        } operations;

        struct {
            HLC from;
            HLC to;
        } request_range;
    };
} SyncMsg;
```

### Sync Flow

```
Client A                    Server                    Client B
    │                          │                          │
    │──── OPERATIONS [op1] ───►│                          │
    │                          │──── OPERATIONS [op1] ───►│
    │                          │                          │
    │                          │◄─── OPERATIONS [op2] ────│
    │◄─── OPERATIONS [op2] ────│                          │
    │                          │                          │
    │──── ACK (hlc_a) ────────►│                          │
    │                          │◄──── ACK (hlc_b) ────────│
```

## Undo/Redo: Branch-Based History

Decision: Use branch-based undo/redo. This aligns with our git-friendly philosophy - undo is just another set of operations that can be diffed, merged, and synced.

### Why Branch-Based?

1. **Git-friendly**: Undo operations are visible in file diffs
2. **CRDT-native**: No special "inverse" logic, just more operations
3. **Collaborative**: Other users see the undo and can react
4. **Time-travel**: Can view state at any point in history
5. **No data loss**: All states are preserved

### Implementation

```c
typedef struct UndoManager {
    HLC* checkpoints;         // HLC timestamps of user actions
    int checkpoint_count;
    int current_index;        // For redo support
} UndoManager;

// Called after each user action (not each micro-op)
void undo_checkpoint(UndoManager* mgr, HLC timestamp) {
    // Truncate any redo history
    mgr->checkpoint_count = mgr->current_index + 1;

    // Add new checkpoint
    mgr->checkpoints[mgr->checkpoint_count++] = timestamp;
    mgr->current_index = mgr->checkpoint_count - 1;
}

void undo(Sheet* sheet, UndoManager* mgr) {
    if (mgr->current_index <= 0) return;

    HLC target = mgr->checkpoints[mgr->current_index - 1];
    HLC current = mgr->checkpoints[mgr->current_index];

    // Find all operations between target and current
    Operation** ops_to_reverse = oplog_range(sheet->oplog, target, current);

    // Generate reverse operations (these become new operations!)
    for (int i = array_len(ops_to_reverse) - 1; i >= 0; i--) {
        Operation* reverse = create_reverse_op(ops_to_reverse[i], sheet);
        apply_and_broadcast(sheet, reverse);  // Syncs to other clients
    }

    mgr->current_index--;
}

void redo(Sheet* sheet, UndoManager* mgr) {
    if (mgr->current_index >= mgr->checkpoint_count - 1) return;

    // Similar logic - replay the operations that were undone
    HLC target = mgr->checkpoints[mgr->current_index + 1];
    HLC current = mgr->checkpoints[mgr->current_index];

    Operation** ops_to_replay = oplog_range(sheet->oplog, current, target);
    // ... apply and broadcast
    mgr->current_index++;
}

// Create reverse operation (simple for LWW cells)
Operation* create_reverse_op(Operation* op, Sheet* sheet) {
    switch (op->type) {
        case OP_CELL_SET: {
            // Get current value (which will become "old" after this)
            Cell* cell = cell_get(sheet, op->cell.cell_id);
            return cell_set_op(op->cell.cell_id,
                              cell ? cell->value : NULL_VALUE,
                              cell ? cell->formula : NULL);
        }
        case OP_DIM_INSERT:
            return dim_delete_op(op->dim.dimension, op->dim.insert.new_axis_id);
        case OP_DIM_DELETE:
            return dim_insert_op(op->dim.dimension, op->dim.delete.axis_data);
        // ...
    }
}
```

### Git Diff for Undo

When user undoes changing A1 from "hello" to "world", the diff shows:

```diff
 op:1705312200000:0:node-a
   type=cell_set
   cell=cell-uuid-001
   value=hello

 op:1705312200001:0:node-a
   type=cell_set
   cell=cell-uuid-001
   value=world

+op:1705312200002:0:node-a
+  type=cell_set
+  cell=cell-uuid-001
+  value=hello
```

The undo is explicit in history - reviewable, revertable, mergeable.

## Presence & Cursors

For showing other users' selections:

```c
typedef struct Presence {
    uuid_t user_id;
    char* user_name;
    uint32_t color;           // For cursor color

    uuid_t sheet_id;          // Which sheet they're viewing
    Selection selection;       // Their current selection
    HLC last_update;
} Presence;

// Presence is ephemeral - uses separate channel
// Not persisted to OpLog
```

## Performance Considerations

1. **Operation batching**: Group rapid changes (typing) into single op
2. **Selective sync**: Only sync visible sheet's ops first
3. **Compression**: Ops are highly compressible (gzip)
4. **Delta sync**: After initial sync, only send new ops
5. **Peer-to-peer**: For local network, skip server

## Security Considerations

1. **Operation validation**: Server validates ops before relay
2. **Access control**: Per-sheet permissions
3. **Rate limiting**: Prevent op flooding
4. **Signatures**: Optional op signing for audit

## Libraries to Consider

Instead of building CRDT from scratch:

| Library | Language | Pros | Cons |
|---------|----------|------|------|
| **Automerge** | Rust/JS | Mature, JSON CRDT, C FFI | Heavier, generic |
| **Yjs** | JS | Fast, great docs | JS-centric |
| **Diamond Types** | Rust | Very fast, C FFI | Text-focused |
| **cr-sqlite** | SQLite ext | SQL queries on CRDT | Different paradigm |

**Recommendation**: Start with custom implementation for cells (simpler than text CRDT), evaluate Automerge-rs later if complexity grows. Cell-level LWW with operation log is straightforward to implement correctly.

## Integration with Data Model

```c
// Cell mutation goes through CRDT layer
void cell_set_value(Sheet* sheet, uuid_t cell_id, CellValue value) {
    // 1. Create operation
    CellOp op = {
        .cell_id = cell_id,
        .type = CELL_OP_SET_VALUE,
        .timestamp = hlc_now(&sheet->clock),
        .set_value = {.value = value}
    };

    // 2. Apply locally
    apply_cell_op(sheet, &op);

    // 3. Append to log
    oplog_append(sheet->oplog, (Operation*)&op);

    // 4. Queue for sync
    sync_queue_add(&op);

    // 5. Trigger recalculation
    recalculate(sheet, cell_id);
}
```
