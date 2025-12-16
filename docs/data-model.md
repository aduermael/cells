# Data Model

## Core Concepts

### Cell
The fundamental unit of data. Each cell is identified by a UUID, not coordinates.

```c
typedef struct Cell {
    uuid_t id;                    // Unique identifier
    uuid_t x;                     // Column axis UUID
    uuid_t y;                     // Row axis UUID

    // Value (pointer for recycle pool optimization)
    char* value;                  // Raw value as string (NULL = empty)
    CellValueType type;           // Interpreted type (or inferred from string)
    CellError error;              // Error state (CELL_OK = no error)

    // Formula (single pointer to combined struct)
    Formula* formula;             // Formula text + compiled bytecode (NULL = not a formula)

    // Metadata
    CellStyle* style;             // Formatting (optional, can be NULL)
    uint64_t modified_at;         // HLC timestamp for CRDT
} Cell;

typedef enum CellValueType {
    CELL_TYPE_AUTO,               // Infer from string content
    CELL_TYPE_NUMBER,
    CELL_TYPE_TEXT,
    CELL_TYPE_BOOLEAN,
    CELL_TYPE_DATE,
    CELL_TYPE_NULL
} CellValueType;

typedef enum CellError {
    CELL_OK = 0,
    CELL_ERR_VALUE,               // #VALUE!
    CELL_ERR_REF,                 // #REF!
    CELL_ERR_NAME,                // #NAME?
    CELL_ERR_DIV,                 // #DIV/0!
    CELL_ERR_NULL,                // #NULL!
    CELL_ERR_NUM,                 // #NUM!
    CELL_ERR_CIRCULAR             // Circular reference
} CellError;

typedef struct Formula {
    char* text;                   // Original formula text (e.g., "=SUM(A1:A10)")
    uint8_t* bytecode;            // Compiled Luau bytecode (NULL = not compiled)
    size_t bytecode_len;          // Bytecode length
} Formula;
```

**Design notes:**
- Type is optional; if `CELL_TYPE_AUTO`, value is cast dynamically from string
- x/y are hardcoded for 2D; UUIDs allow future N-D extension without changing Cell struct
- Formula combines source text and bytecode in one allocation

### Axis (Column or Row)
Represents a single column (x-axis) or row (y-axis).

```c
typedef struct Axis {
    uuid_t id;                    // Unique identifier
    bool is_column;               // true = column (x), false = row (y)

    // Doubly-linked list within dimension
    uuid_t prev;                  // Previous axis (or NULL_UUID)
    uuid_t next;                  // Next axis (or NULL_UUID)
    uint32_t gap;                 // Number of skipped positions to next (0 = adjacent)

    // Properties
    char* name;                   // Custom name only (NULL = compute A,B,C or 1,2,3)
    uint32_t size;                // Width (column) or height (row) in pixels
    bool hidden;                  // Visibility flag

    // Type constraints (columns only)
    ColumnConstraints* constraints;  // NULL = any type allowed (dynamic)

    // CRDT metadata
    uint64_t created_at;          // HLC timestamp
} Axis;
```

**Design notes:**
- `name` is only stored if user sets a custom name; "A", "B", "C" or "1", "2", "3" are computed dynamically by walking the linked list
- If display name caching is needed for performance, it lives outside the Axis struct (e.g., in a rendering layer cache)
- See [type-system.md](./type-system.md) for `ColumnConstraints` definition

### Sheet
A 2D grid containing cells.

```c
typedef struct Sheet {
    uuid_t id;
    char* name;

    // Column axis (x dimension)
    uuid_t first_col;             // Head of column linked list
    uuid_t last_col;              // Tail of column linked list
    uint32_t col_count;           // Number of defined columns
    uint32_t default_col_width;   // Default width for new columns

    // Row axis (y dimension)
    uuid_t first_row;             // Head of row linked list
    uuid_t last_row;              // Tail of row linked list
    uint32_t row_count;           // Number of defined rows
    uint32_t default_row_height;  // Default height for new rows

    // Cell storage (sharded hashmap by UUID)
    CellMap* cells;

    // Axis storage (hashmap by UUID)
    AxisMap* columns;             // Column axes
    AxisMap* rows;                // Row axes

    // CRDT metadata
    uint64_t modified_at;         // HLC timestamp
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
X-Axis (Columns)
    ┌────────────────────────────────────────────────────┐
    │  Col A ──(gap:0)──► Col B ──(gap:2)──► Col E       │
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
    Row 1 ───(gap:1)───► Row 3

    Y-Axis (Rows)
```

## Gap Encoding Example

Columns A, B exist. Columns C, D don't exist. Column E exists.

```
Column A:
  id: "aB3kM9xQ"
  is_column: true
  prev: NULL
  next: "fG7nP2wR"
  gap: 0                  // B is immediately after A

Column B:
  id: "fG7nP2wR"
  is_column: true
  prev: "aB3kM9xQ"
  next: "jK4sT8yL"
  gap: 2                  // 2 undefined columns (C, D) before E

Column E:
  id: "jK4sT8yL"
  is_column: true
  prev: "fG7nP2wR"
  next: NULL
  gap: 0
```

## Cell Lookup Strategies

### By UUID (Primary)
O(1) via hashmap - this is the canonical lookup.

```c
Cell* cell_get(Sheet* sheet, uuid_t id);
```

### By Coordinates (For UI)
O(1) with compound key lookup (x,y -> cell_id).

```c
// Primary: lookup by axis UUIDs
Cell* cell_at(Sheet* sheet, uuid_t x, uuid_t y);

// Convenience: lookup by position (requires axis traversal first)
Cell* cell_at_position(Sheet* sheet, uint32_t col_pos, uint32_t row_pos);
```

### By Range (For Formulas)
Iterator-based for efficiency.

```c
CellIterator* cells_in_range(Sheet* sheet,
                             uuid_t x_start, uuid_t x_end,
                             uuid_t y_start, uuid_t y_end);
```

## Memory Layout Considerations

For cache efficiency, consider structure-of-arrays for hot paths:

```c
// For rendering (hot path)
typedef struct CellRenderData {
    uuid_t* ids;
    char** values;                // String values
    CellValueType* types;         // Type tags
    CellStyle** styles;
    uint32_t count;
} CellRenderData;

// Viewport extraction
CellRenderData* extract_viewport(Sheet* sheet, Viewport* vp);
```

## Future: N-Dimensional Extension

The UUID-based x/y coordinates allow future extension to N dimensions without changing the Cell struct. This is a long-term consideration, not part of initial implementation.

Potential uses:
- Pivot table views (3D: sheet × row × column)
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

## Open Questions

1. **String interning**: Deduplicate repeated string values across cells?
2. **Style sharing**: Many cells share styles - use style IDs with registry?
3. **Recycle pools**: Implementation strategy for Cell and value pointer allocation?
4. **Coordinate index**: Secondary index for (x,y) -> cell_id lookups, or compute on demand?
