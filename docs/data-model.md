# Data Model

## Overview

The data model is **UUID-based** at its core. All cells, columns, and rows are identified by UUIDs, not coordinates. The 2D grid view that users see is derived from this sparse UUID representation.

**Key Principle**: UUIDs are the source of truth. Grid coordinates (A1, B2) are computed views.

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

Represents a single column or row with an explicit visual position.

| Field | Description |
|-------|-------------|
| `id` | Unique identifier |
| `is_column` | true = column (x), false = row (y) |
| `position` | Visual position (0-indexed integer) |
| `name` | Custom name (NULL = compute A,B,C or 1,2,3) |
| `size` | Width (column) or height (row) in pixels |
| `hidden` | Visibility flag |
| `constraints` | Type constraints (columns only) |

**Note**: Display names (A, B, C or 1, 2, 3) are computed from position, not stored.

### Sheet

A 2D grid containing cells.

| Field | Description |
|-------|-------------|
| `id` | Unique identifier |
| `name` | Sheet name |
| `cells` | Cell storage (sharded hashmap) |
| `columns` | Axis storage (hashmap by UUID) |
| `rows` | Axis storage (hashmap by UUID) |

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
    │  Col A (pos:0)    Col B (pos:1)    Col E (pos:4)   │
    └────────────────────────────────────────────────────┘
         │                   │                    │
         ▼                   ▼                    ▼
    ┌────────┐          ┌────────┐          ┌────────┐
    │Cell    │          │Cell    │          │Cell    │
    │(A,1)   │          │(B,1)   │          │(E,1)   │
    └────────┘          └────────┘          └────────┘
         │                   │                    │
         ▼                   ▼                    ▼
    Row 1 (pos:0)    Row 3 (pos:2)

    Y-Axis (Rows)
```

## Sparse Positions

Columns A, B exist. C, D don't exist. E exists.

- Column A: `position=0`
- Column B: `position=1`
- Column E: `position=4`

Positions don't need to be contiguous. Empty positions are simply not stored.

## Cell Lookup Strategies

| Method | Complexity | Use Case |
|--------|------------|----------|
| By UUID | O(1) | Primary lookup (hashmap) |
| By (col_id, row_id) | O(1) | Cell grid lookup (hashmap) |
| By pixel coordinate | O(log n) | UI viewport queries (Order Statistic Tree) |
| Range iterator | O(k) | Formula evaluation |

## Order Statistic Tree

The Order Statistic Tree (OSTree) bridges the gap between pixel coordinates (what the UI needs) and UUID identifiers (what the data model uses).

### The Problem

The UI needs to answer: "Which columns/rows are visible in pixels 500-1000?"

With sparse UUID-based data, we can't just divide by column width. We need a data structure that:
1. Maps pixel offsets to axis UUIDs efficiently
2. Updates incrementally as axes are inserted/deleted/resized
3. Supports efficient range queries for viewports

### The Solution

Two Order Statistic Trees per sheet:
- **Column OSTree**: Maps x-pixel → column UUID
- **Row OSTree**: Maps y-pixel → row UUID

```
Pixel offset:     0      100    200    300    400    500
                  │       │      │      │      │      │
                  ▼       ▼      ▼      ▼      ▼      ▼
OSTree:       ┌───────┬───────┬───────┬───────┬───────┐
              │Col A  │Col B  │ (gap) │Col E  │Col F  │
              │100px  │100px  │ 0px   │100px  │100px  │
              └───────┴───────┴───────┴───────┴───────┘
                  │       │              │       │
                  ▼       ▼              ▼       ▼
UUID:         aB3kQ2x  cD5mN8y        fH2pR4t  gK7sT1w
```

### Operations

| Operation | Complexity | Description |
|-----------|------------|-------------|
| `findAtOffset(px)` | O(log n) | Get axis UUID at pixel offset |
| `getOffsetForAxis(uuid)` | O(log n) | Get pixel offset for axis |
| `queryRange(start, end)` | O(log n + k) | Get all axes in pixel range |
| `insert(axis)` | O(log n) | Add axis (updates subtree sizes) |
| `remove(axis)` | O(log n) | Remove axis |
| `resize(axis, newSize)` | O(log n) | Change axis width/height |

### Key Properties

- **Subtree size tracking**: Each node stores the cumulative pixel size of its subtree
- **Augmented BST**: Standard BST operations with size aggregation
- **Incremental updates**: Insert/delete/resize update only O(log n) nodes

### Implementation

- Header: `core/cells/ostree.h`
- Implementation: `core/cells/ostree.cc`
- Viewport queries: `core/cells/viewport_index.h`

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

### Why position-based ordering?

1. **Simple**: Each axis knows its own position
2. **Git-friendly**: Insert = 1 line added (vs. 3 lines with linked lists)
3. **CRDT-compatible**: Position conflicts resolved via LWW
4. **Easy sorting**: Just sort by position field

## Range Formatting

Formatting (background colors, borders, etc.) is stored as ranges, not per-cell. When ranges with colliding properties overlap, we split them to maintain consistency.

### Range Storage

When a user applies a background color to range A1:C3, we store a single range object with that property. This is more compact than per-cell storage and preserves the user's intent.

**Note:** Excel internally stores styles per-cell (each `<c>` element references a style). When importing XLSX files, we observe the per-cell styles and could reconstruct ranges, but the canonical representation in XLSX is per-cell.

### Overlapping Ranges and Property Collision

When two ranges overlap, the behavior depends on whether their properties **collide**:

**Colliding properties** (same attribute): The earlier range gets split to avoid overlap.

Example: Blue background on C4:F13, then green background on E7:H18:
```
     C   D   E   F   G   H
4   [  BLUE   ]
5   [  BLUE   ]
6   [  BLUE   ]
7   [BLU][ GREEN        ]
... [BLU][ GREEN        ]
13  [BLU][ GREEN        ]
14       [ GREEN        ]
...
18       [ GREEN        ]
```

The original blue range (C4:F13) is split into non-overlapping parts (C4:F6 and C7:D13) so that the green range can occupy E7:H18 without conflict.

**Non-colliding properties** (different attributes): Ranges can overlap freely.

Example: Blue background on A1:C3, then thick border on B2:D4:
```
     A   B   C   D
1   [BLUE      ]
2   [BLUE┏━━━━━━━━━┓
3   [BLUE┃   ]    ┃
4        ┗━━━━━━━━━┛
```

Cells B2:C3 are covered by both ranges - the background range and the border range coexist without splitting.

### Design Rationale

1. **Compact storage**: One range object vs. many per-cell entries
2. **No z-order complexity**: Splitting eliminates the need to track layering
3. **Predictable rendering**: No runtime overlap resolution needed
4. **CRDT-friendly**: Range operations have clear conflict resolution semantics

## Implementation

- Types: `core/cells/types.h`
- Model: `core/cells/model.h`, `core/cells/model.cc`
- ID generation: `core/cells/id.h`, `core/cells/id.cc`
