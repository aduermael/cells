# Data Model

## Core Concepts

### Cell
The fundamental unit of data. Each cell is identified by a UUID, not coordinates.

```c
typedef struct Cell {
    uuid_t id;                    // Unique identifier
    CellValue value;              // Raw value (number, string, etc.)
    char* formula;                // Original formula text (if any)
    CompiledFormula* compiled;    // Lua bytecode (if formula)
    DimensionLink* dim_links;     // Array of links, one per dimension
    uint32_t dim_count;           // Number of dimensions this cell participates in

    // Metadata
    CellStyle* style;             // Formatting (optional, can be NULL)
    uint64_t modified_at;         // Lamport timestamp for CRDT
    uuid_t modified_by;           // Node ID for CRDT
} Cell;

typedef union CellValue {
    double number;
    char* string;
    bool boolean;
    CellError error;
    // NULL represented by tag
} CellValue;

typedef enum CellValueType {
    CELL_NULL,
    CELL_NUMBER,
    CELL_STRING,
    CELL_BOOLEAN,
    CELL_ERROR
} CellValueType;
```

### DimensionLink
Links a cell to its position within a dimension (column, row, or higher).

```c
typedef struct DimensionLink {
    uint32_t dimension;           // Which dimension (0=col, 1=row, ...)
    uuid_t axis_id;               // Which axis node this cell belongs to

    // For ordering within the axis
    uuid_t prev_cell;             // Previous cell in this axis (or NULL_UUID)
    uuid_t next_cell;             // Next cell in this axis (or NULL_UUID)
} DimensionLink;
```

### Axis (Column/Row/etc.)
Represents a single column, row, or higher-dimensional axis.

```c
typedef struct Axis {
    uuid_t id;                    // Unique identifier
    uint32_t dimension;           // Which dimension this axis belongs to

    // Doubly-linked list within the dimension
    uuid_t prev_axis;             // Previous axis (or NULL_UUID)
    uuid_t next_axis;             // Next axis (or NULL_UUID)
    uint32_t gap_to_next;         // Visual gap to next axis (0 = adjacent)

    // Axis properties
    char* name;                   // Display name ("A", "B", "1", "2", custom)
    uint32_t size;                // Width (dim 0) or height (dim 1) in pixels
    bool hidden;                  // Visibility flag

    // Type constraints (optional, primarily for dimension 0 / columns)
    ColumnConstraints* constraints;  // NULL = any type allowed (dynamic)

    // First/last cell in this axis for traversal
    uuid_t first_cell;
    uuid_t last_cell;

    // CRDT metadata
    uint64_t created_at;          // Lamport timestamp
    uuid_t created_by;            // Node ID
} Axis;
```

See [type-system.md](./type-system.md) for `ColumnConstraints` definition and validation logic.

### Dimension
Container for all axes of a given dimension type.

```c
typedef struct Dimension {
    uint32_t index;               // 0, 1, 2, ...
    char* name;                   // "columns", "rows", or custom

    uuid_t first_axis;            // Head of the linked list
    uuid_t last_axis;             // Tail of the linked list
    uint32_t axis_count;          // Number of defined axes

    // Default properties for new axes
    uint32_t default_size;        // Default width/height
} Dimension;
```

### Sheet
A 2D (or N-D) view containing cells.

```c
typedef struct Sheet {
    uuid_t id;
    char* name;

    Dimension* dimensions;        // Array of dimensions
    uint32_t dim_count;           // Number of dimensions (typically 2)

    // Cell storage (hashmap by UUID)
    CellMap* cells;

    // CRDT metadata
    uint64_t modified_at;
    uuid_t modified_by;
} Sheet;
```

### Workbook
Top-level container.

```c
typedef struct Workbook {
    uuid_t id;
    char* name;

    Sheet** sheets;
    uint32_t sheet_count;

    // Named ranges, styles, etc.
    NamedRangeMap* named_ranges;
    StyleRegistry* styles;

    // CRDT vector clock
    VectorClock* clock;
} Workbook;
```

## Visual Representation

```
Dimension 0 (Columns)
    ┌────────────────────────────────────────────────────┐
    │  Axis A ──(gap:0)──► Axis B ──(gap:2)──► Axis E    │
    │    │                   │                    │      │
    └────┼───────────────────┼────────────────────┼──────┘
         │                   │                    │
         ▼                   ▼                    ▼
    ┌────────┐          ┌────────┐          ┌────────┐
    │Cell α  │          │Cell β  │          │Cell γ  │
    │(A,1)   │          │(B,1)   │          │(E,1)   │
    └────────┘          └────────┘          └────────┘
         │                   │                    │
         ▼                   ▼                    ▼
    ┌────────┐          ┌────────┐
    │Cell δ  │          │Cell ε  │          (no cell at E,3)
    │(A,3)   │          │(B,3)   │
    └────────┘          └────────┘
         ▲                   ▲                    ▲
         │                   │                    │
    ─────┼───────────────────┼────────────────────┼──────
         │                   │                    │
    Axis 1 ───(gap:1)───► Axis 3
    (Row 1)               (Row 3)

    Dimension 1 (Rows)
```

## Gap Encoding Example

Columns A, B exist. Columns C, D don't exist. Column E exists.

```
Axis A:
  id: "uuid-col-a"
  prev_axis: NULL
  next_axis: "uuid-col-b"
  gap_to_next: 0          // B is immediately after A

Axis B:
  id: "uuid-col-b"
  prev_axis: "uuid-col-a"
  next_axis: "uuid-col-e"
  gap_to_next: 2          // 2 undefined columns (C, D) before E

Axis E:
  id: "uuid-col-e"
  prev_axis: "uuid-col-b"
  next_axis: NULL
  gap_to_next: 0
```

## Cell Lookup Strategies

### By UUID (Primary)
O(1) via hashmap - this is the canonical lookup.

```c
Cell* cell_get(Sheet* sheet, uuid_t id);
```

### By Coordinates (For UI)
O(n) worst case, but typically O(1) with axis index cache.

```c
Cell* cell_at(Sheet* sheet, uuid_t col_id, uuid_t row_id);
// Or with positional index (requires traversing linked list):
Cell* cell_at_position(Sheet* sheet, uint32_t col_pos, uint32_t row_pos);
```

### By Range (For Formulas)
Iterator-based for efficiency.

```c
CellIterator* cells_in_range(Sheet* sheet,
                             uuid_t start_col, uuid_t end_col,
                             uuid_t start_row, uuid_t end_row);
```

## Memory Layout Considerations

For cache efficiency, consider structure-of-arrays for hot paths:

```c
// For rendering (hot path)
typedef struct CellRenderData {
    uuid_t* ids;
    CellValue* values;
    CellStyle** styles;
    uint32_t count;
} CellRenderData;

// Viewport extraction
CellRenderData* extract_viewport(Sheet* sheet, Viewport* vp);
```

## Multi-Dimensional Generalization

For N-dimensional data:

```c
// 3D example: Sheet × Row × Column
Cell* cell = cell_at_nd(workbook, (uuid_t[]){
    sheet_axis_id,   // Dimension 0: which sheet
    row_axis_id,     // Dimension 1: which row
    col_axis_id      // Dimension 2: which column
}, 3);
```

This allows:
- Pivot table views
- Multi-sheet formulas
- Time-series dimensions
- Custom hierarchies

## Cell Storage: Sharded Hashmap

Decision: Use sharded hashmap for O(1) cell access with parallelization support.

```c
#define CELL_MAP_SHARDS 64  // Power of 2 for fast modulo

typedef struct CellMapShard {
    HashMap* map;           // UUID -> Cell*
    pthread_rwlock_t lock;  // Per-shard lock for concurrent access
} CellMapShard;

typedef struct CellMap {
    CellMapShard shards[CELL_MAP_SHARDS];
    atomic_uint64_t count;  // Total cell count
} CellMap;

// Shard selection based on UUID
static inline uint32_t cell_shard_index(uuid_t id) {
    // Use first 6 bits of UUID hash
    uint32_t hash = uuid_hash(id);
    return hash & (CELL_MAP_SHARDS - 1);
}

Cell* cellmap_get(CellMap* map, uuid_t id) {
    uint32_t shard = cell_shard_index(id);
    pthread_rwlock_rdlock(&map->shards[shard].lock);
    Cell* cell = hashmap_get(map->shards[shard].map, id);
    pthread_rwlock_unlock(&map->shards[shard].lock);
    return cell;
}

void cellmap_set(CellMap* map, uuid_t id, Cell* cell) {
    uint32_t shard = cell_shard_index(id);
    pthread_rwlock_wrlock(&map->shards[shard].lock);
    hashmap_set(map->shards[shard].map, id, cell);
    pthread_rwlock_unlock(&map->shards[shard].lock);
    atomic_fetch_add(&map->count, 1);
}

// Parallel iteration (for batch operations)
void cellmap_foreach_parallel(CellMap* map,
                               void (*fn)(Cell*, void*),
                               void* ctx) {
    #pragma omp parallel for
    for (int s = 0; s < CELL_MAP_SHARDS; s++) {
        pthread_rwlock_rdlock(&map->shards[s].lock);
        hashmap_foreach(map->shards[s].map, fn, ctx);
        pthread_rwlock_unlock(&map->shards[s].lock);
    }
}
```

Benefits:
- **O(1)** lookup by UUID
- **Lock contention reduced** by 64x vs single lock
- **Parallel iteration** for recalc, rendering
- **Memory efficient** for sparse data

## Open Questions for Data Model

1. **String interning**: Deduplicate repeated string values?
2. **Style sharing**: Many cells share styles - use style IDs with registry?
3. **Computed values**: Store cached formula results, or always recompute?
4. **Sparse dimension iteration**: For very sparse sheets, use skip-list overlay?
