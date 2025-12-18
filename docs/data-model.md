# Data Model

## Core Concepts

### Cell

The fundamental unit of data. Each cell is identified by a UUID, not coordinates.

| Field | Description |
|-------|-------------|
| `id` | Unique identifier (8-char base62) |
| `x` | Column axis UUID |
| `y` | Row axis UUID |
| `value` | Raw value as string (NULL = empty) |
| `type` | Value type (auto-inferred or explicit) |
| `error` | Error state (CELL_OK = no error) |
| `formula` | Formula text + parsed AST (NULL = not a formula) |
| `style` | Formatting (optional) |
| `modified_at` | HLC timestamp for CRDT |

### Cell Value Types

| Type | Description |
|------|-------------|
| `AUTO` | Infer from string content |
| `NUMBER` | Numeric |
| `TEXT` | String |
| `BOOLEAN` | true/false |
| `DATE` | Date value |
| `NULL` | Empty |

### Cell Errors

| Error | Excel Equivalent |
|-------|------------------|
| `VALUE` | #VALUE! |
| `REF` | #REF! |
| `NAME` | #NAME? |
| `DIV` | #DIV/0! |
| `NULL` | #NULL! |
| `NUM` | #NUM! |
| `CIRCULAR` | Circular reference |

### Axis (Column or Row)

Represents a single column or row as a doubly-linked list node.

| Field | Description |
|-------|-------------|
| `id` | Unique identifier |
| `is_column` | true = column (x), false = row (y) |
| `prev` / `next` | Linked list pointers |
| `gap` | Skipped positions to next (0 = adjacent) |
| `name` | Custom name (NULL = compute A,B,C or 1,2,3) |
| `size` | Width (column) or height (row) in pixels |
| `hidden` | Visibility flag |
| `constraints` | Type constraints (columns only) |

**Note**: Display names (A, B, C or 1, 2, 3) are computed by walking the linked list, not stored.

### Sheet

A 2D grid containing cells.

| Field | Description |
|-------|-------------|
| `id` | Unique identifier |
| `name` | Sheet name |
| `first_col` / `last_col` | Column linked list head/tail |
| `first_row` / `last_row` | Row linked list head/tail |
| `col_count` / `row_count` | Number of defined axes |
| `cells` | Cell storage (sharded hashmap) |
| `columns` / `rows` | Axis storage (hashmap by UUID) |

### Workbook

Top-level container.

| Field | Description |
|-------|-------------|
| `id` | Unique identifier |
| `name` | Workbook name |
| `sheets` | List of sheets |
| `named_ranges` | Named range definitions |
| `styles` | Style registry |
| `clock` | CRDT vector clock |

## Visual Representation

```
X-Axis (Columns)
    ┌────────────────────────────────────────────────────┐
    │  Col A ──(gap:0)──► Col B ──(gap:2)──► Col E       │
    └────────────────────────────────────────────────────┘
         │                   │                    │
         ▼                   ▼                    ▼
    ┌────────┐          ┌────────┐          ┌────────┐
    │Cell    │          │Cell    │          │Cell    │
    │(A,1)   │          │(B,1)   │          │(E,1)   │
    └────────┘          └────────┘          └────────┘
         │                   │                    │
         ▼                   ▼                    ▼
    Row 1 ───(gap:1)───► Row 3

    Y-Axis (Rows)
```

## Gap Encoding

Columns A, B exist. C, D don't exist. E exists.

- Column A: `prev=~, next=B, gap=0` (B is adjacent)
- Column B: `prev=A, next=E, gap=2` (2 empty columns C,D before E)
- Column E: `prev=B, next=~, gap=0`

## Cell Lookup Strategies

| Method | Complexity | Use Case |
|--------|------------|----------|
| By UUID | O(1) | Primary lookup |
| By (col_id, row_id) | O(1) | UI coordinate lookup |
| By position (col_pos, row_pos) | O(n) | Requires axis traversal |
| Range iterator | O(k) | Formula evaluation |

## Cell Storage

Sharded hashmap for O(1) access with parallelization:
- 64 shards (power of 2 for fast modulo)
- Per-shard read-write locks
- UUID hash determines shard
- Enables parallel iteration for recalc/rendering

## Design Decisions

### Why UUID-based cells instead of (row, col)?

1. **CRDT-friendly**: No coordinate conflicts during concurrent edits
2. **Stable references**: Moving cells doesn't break formulas
3. **Sparse by nature**: Only allocated cells consume memory
4. **Multi-dimensional**: Generalizes beyond 2D trivially

### Why doubly-linked dimensions with gaps?

1. **O(1) insert/delete**: No array shifting
2. **Sparse-friendly**: Gap encoding avoids empty nodes
3. **CRDT-compatible**: Each link is independently addressable
4. **Flexible iteration**: Forward/backward traversal

## Implementation

- Types: `core/cells/types.h`
- Model: `core/cells/model.h`, `core/cells/model.cc`
- ID generation: `core/cells/id.h`, `core/cells/id.cc`
