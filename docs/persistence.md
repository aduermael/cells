# Persistence & File Format

## Design Goals

1. **Git-friendly**: Line-based, deterministic ordering, meaningful diffs
2. **Human-readable**: Editable in a text editor if needed
3. **Efficient**: Fast to parse, reasonable file size
4. **Complete**: Preserves all data including history (for CRDT)
5. **Streamable**: Can load incrementally for large files

## File Format Overview

Two formats supported:

| Format | Extension | Use Case |
|--------|-----------|----------|
| Text | `.cells` | Primary, git-friendly |
| Binary | `.cellsb` | Large files, faster loading |

## Text Format (`.cells`)

### Structure

```
# Cells Document v1
# Generated: 2024-01-15T10:30:00Z

[meta]
id=550e8400-e29b-41d4-a716-446655440000
name=Budget 2024
created=2024-01-01T00:00:00Z
modified=2024-01-15T10:30:00Z

[sheets]
sheet:3fa85f64-5717-4562-b3fc-2c963f66afa6
  name=Q1
  dim0_name=columns
  dim1_name=rows

[dimensions:3fa85f64-5717-4562-b3fc-2c963f66afa6]
# Dimension 0 (columns)
d0:axis:a1b2c3d4-0000-0000-0000-000000000001
  prev=
  next=a1b2c3d4-0000-0000-0000-000000000002
  gap=0
  name=A
  size=100

d0:axis:a1b2c3d4-0000-0000-0000-000000000002
  prev=a1b2c3d4-0000-0000-0000-000000000001
  next=a1b2c3d4-0000-0000-0000-000000000005
  gap=2
  name=B
  size=100

d0:axis:a1b2c3d4-0000-0000-0000-000000000005
  prev=a1b2c3d4-0000-0000-0000-000000000002
  next=
  gap=0
  name=E
  size=120

# Dimension 1 (rows)
d1:axis:row-uuid-1
  prev=
  next=row-uuid-2
  gap=0
  name=1
  size=24

d1:axis:row-uuid-2
  prev=row-uuid-1
  next=
  gap=0
  name=2
  size=24

[cells:3fa85f64-5717-4562-b3fc-2c963f66afa6]
cell:cell-uuid-001
  d0=a1b2c3d4-0000-0000-0000-000000000001
  d1=row-uuid-1
  type=number
  value=42

cell:cell-uuid-002
  d0=a1b2c3d4-0000-0000-0000-000000000002
  d1=row-uuid-1
  type=formula
  formula==A1*2
  cached=84

cell:cell-uuid-003
  d0=a1b2c3d4-0000-0000-0000-000000000001
  d1=row-uuid-2
  type=string
  value=Hello, World!

[styles]
style:style-uuid-001
  bold=true
  fg=#FF0000

[cell_styles:3fa85f64-5717-4562-b3fc-2c963f66afa6]
cell-uuid-003=style-uuid-001

[named_ranges]
range:MyRange
  sheet=3fa85f64-5717-4562-b3fc-2c963f66afa6
  start_d0=a1b2c3d4-0000-0000-0000-000000000001
  start_d1=row-uuid-1
  end_d0=a1b2c3d4-0000-0000-0000-000000000002
  end_d1=row-uuid-2

[oplog]
# Optional: include operation history for CRDT sync
op:1705312200000:0:node-uuid-a
  type=cell_set
  cell=cell-uuid-001
  value=42

op:1705312200001:0:node-uuid-b
  type=cell_set
  cell=cell-uuid-002
  formula==A1*2
```

### Design Decisions

**Deterministic Ordering**:
- Sections in fixed order
- Within sections, entries sorted by UUID
- Produces identical output for identical state

**One Logical Item Per Block**:
- Each cell, axis, etc. is a distinct block
- Git can diff/merge at block level

**Human Editable**:
- Simple key=value within blocks
- Comments with `#`
- Whitespace tolerant

### Git Diff Example

When cell A1 changes from 42 to 100:

```diff
 [cells:3fa85f64-5717-4562-b3fc-2c963f66afa6]
 cell:cell-uuid-001
   d0=a1b2c3d4-0000-0000-0000-000000000001
   d1=row-uuid-1
   type=number
-  value=42
+  value=100
```

When a column is inserted:

```diff
 [dimensions:3fa85f64-5717-4562-b3fc-2c963f66afa6]
 d0:axis:a1b2c3d4-0000-0000-0000-000000000001
   prev=
-  next=a1b2c3d4-0000-0000-0000-000000000002
+  next=a1b2c3d4-0000-0000-0000-NEW-COL-UUID
   gap=0
   name=A
   size=100

+d0:axis:a1b2c3d4-0000-0000-0000-NEW-COL-UUID
+  prev=a1b2c3d4-0000-0000-0000-000000000001
+  next=a1b2c3d4-0000-0000-0000-000000000002
+  gap=0
+  name=A.5
+  size=100

 d0:axis:a1b2c3d4-0000-0000-0000-000000000002
-  prev=a1b2c3d4-0000-0000-0000-000000000001
+  prev=a1b2c3d4-0000-0000-0000-NEW-COL-UUID
   next=a1b2c3d4-0000-0000-0000-000000000005
```

## Binary Format (`.cellsb`)

For large files, use a binary format:

```
┌────────────────────────────────────────┐
│ Header (32 bytes)                      │
│   Magic: "CELLS\x00\x01\x00"           │
│   Version: uint32                      │
│   Flags: uint32                        │
│   Section count: uint32                │
│   ... padding ...                      │
├────────────────────────────────────────┤
│ Section Table                          │
│   [type: u8, offset: u64, len: u64]    │
│   ... per section ...                  │
├────────────────────────────────────────┤
│ Meta Section (msgpack)                 │
├────────────────────────────────────────┤
│ Sheets Section (msgpack)               │
├────────────────────────────────────────┤
│ Dimensions Section (msgpack)           │
├────────────────────────────────────────┤
│ Cells Section (msgpack, compressed)    │
├────────────────────────────────────────┤
│ Styles Section (msgpack)               │
├────────────────────────────────────────┤
│ OpLog Section (optional, compressed)   │
└────────────────────────────────────────┘
```

Use MessagePack for sections - compact, fast, schema-flexible.

## Parser Implementation

### Text Format Parser

```c
typedef enum ParseState {
    PARSE_ROOT,
    PARSE_META,
    PARSE_SHEETS,
    PARSE_DIMENSIONS,
    PARSE_CELLS,
    PARSE_STYLES,
    PARSE_OPLOG,
} ParseState;

typedef struct Parser {
    const char* input;
    size_t pos;
    size_t len;
    int line;
    ParseState state;
    Workbook* workbook;
    Sheet* current_sheet;
    char* error;
} Parser;

Workbook* parse_cells_file(const char* content, size_t len) {
    Parser p = {
        .input = content,
        .len = len,
        .workbook = workbook_new()
    };

    while (p.pos < p.len && !p.error) {
        skip_whitespace_and_comments(&p);

        if (peek(&p) == '[') {
            parse_section_header(&p);
        } else if (is_identifier_start(peek(&p))) {
            parse_entry(&p);
        }
    }

    if (p.error) {
        workbook_free(p.workbook);
        return NULL;
    }

    return p.workbook;
}
```

### Streaming Parser

For large files, parse incrementally:

```c
typedef struct StreamParser {
    Parser base;
    void (*on_cell)(Cell* cell, void* ctx);
    void (*on_axis)(Axis* axis, void* ctx);
    void* ctx;
} StreamParser;

// Parse and emit callbacks without building full in-memory structure
void parse_streaming(FILE* file, StreamParser* parser);
```

## Serializer Implementation

```c
void serialize_workbook(Workbook* wb, FILE* out) {
    fprintf(out, "# Cells Document v1\n");
    fprintf(out, "# Generated: %s\n\n", iso8601_now());

    // Meta section
    fprintf(out, "[meta]\n");
    fprintf(out, "id=%s\n", uuid_str(wb->id));
    fprintf(out, "name=%s\n", escape_string(wb->name));
    fprintf(out, "\n");

    // Sheets section
    fprintf(out, "[sheets]\n");
    for (int i = 0; i < wb->sheet_count; i++) {
        serialize_sheet_header(wb->sheets[i], out);
    }

    // Per-sheet sections
    for (int i = 0; i < wb->sheet_count; i++) {
        serialize_sheet_dimensions(wb->sheets[i], out);
        serialize_sheet_cells(wb->sheets[i], out);
    }

    // Styles
    serialize_styles(wb->styles, out);

    // OpLog (optional)
    if (wb->include_history) {
        serialize_oplog(wb->oplog, out);
    }
}

void serialize_cell(Cell* cell, FILE* out) {
    fprintf(out, "cell:%s\n", uuid_str(cell->id));
    for (int d = 0; d < cell->dim_count; d++) {
        fprintf(out, "  d%d=%s\n", d, uuid_str(cell->dim_links[d].axis_id));
    }
    fprintf(out, "  type=%s\n", cell_type_name(cell->value_type));

    if (cell->formula) {
        fprintf(out, "  formula=%s\n", escape_string(cell->formula));
        fprintf(out, "  cached=%s\n", cell_value_str(cell->value));
    } else {
        fprintf(out, "  value=%s\n", cell_value_str(cell->value));
    }
    fprintf(out, "\n");
}
```

## Import/Export

### Excel Import (.xlsx)

```c
// Using libxlsxwriter or similar
Workbook* import_xlsx(const char* path) {
    xlsxio_read* xlsx = xlsxio_read_open(path);
    Workbook* wb = workbook_new();

    // Iterate sheets
    char* sheet_name;
    while ((sheet_name = xlsxio_read_get_next_sheetname(xlsx))) {
        Sheet* sheet = sheet_new(sheet_name);

        // Read cells
        xlsxio_read_sheet* xs = xlsxio_read_open_sheet(xlsx, sheet_name);
        int row = 0;
        while (xlsxio_read_next_row(xs)) {
            int col = 0;
            char* value;
            while ((value = xlsxio_read_next_cell(xs))) {
                // Create axis if needed, create cell
                import_cell(sheet, col, row, value);
                col++;
            }
            row++;
        }

        workbook_add_sheet(wb, sheet);
    }

    return wb;
}
```

### Excel Export (.xlsx)

```c
void export_xlsx(Workbook* wb, const char* path) {
    lxw_workbook* xlsx = workbook_new(path);

    for (int s = 0; s < wb->sheet_count; s++) {
        Sheet* sheet = wb->sheets[s];
        lxw_worksheet* ws = workbook_add_worksheet(xlsx, sheet->name);

        // Iterate cells and write
        CellIterator* it = sheet_cell_iterator(sheet);
        Cell* cell;
        while ((cell = cell_iterator_next(it))) {
            int row, col;
            cell_get_position(sheet, cell, &col, &row);

            switch (cell->value_type) {
                case CELL_NUMBER:
                    worksheet_write_number(ws, row, col, cell->value.number, NULL);
                    break;
                case CELL_STRING:
                    worksheet_write_string(ws, row, col, cell->value.string, NULL);
                    break;
                // ...
            }

            if (cell->formula) {
                // Convert UUID refs back to A1 notation
                char* excel_formula = convert_to_a1(cell->formula, sheet);
                worksheet_write_formula(ws, row, col, excel_formula, NULL);
            }
        }
    }

    workbook_close(xlsx);
}
```

### CSV Import/Export

Simpler, single-sheet:

```c
Sheet* import_csv(const char* path, char delimiter) {
    Sheet* sheet = sheet_new("Sheet1");
    FILE* f = fopen(path, "r");

    char line[MAX_LINE];
    int row = 0;
    while (fgets(line, sizeof(line), f)) {
        int col = 0;
        char* token = strtok(line, &delimiter);
        while (token) {
            Cell* cell = sheet_set_cell(sheet, col, row, parse_value(token));
            token = strtok(NULL, &delimiter);
            col++;
        }
        row++;
    }

    return sheet;
}
```

## Autosave & Recovery

```c
typedef struct AutosaveManager {
    Workbook* workbook;
    char* autosave_path;
    uint64_t last_save_op;        // OpLog sequence of last save
    int autosave_interval_ms;
    bool dirty;
} AutosaveManager;

void autosave_tick(AutosaveManager* mgr) {
    if (!mgr->dirty) return;

    // Write to temp file, then atomic rename
    char temp_path[PATH_MAX];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", mgr->autosave_path);

    FILE* f = fopen(temp_path, "w");
    serialize_workbook(mgr->workbook, f);
    fclose(f);

    rename(temp_path, mgr->autosave_path);
    mgr->dirty = false;
}

Workbook* recover_autosave(const char* path) {
    char autosave_path[PATH_MAX];
    snprintf(autosave_path, sizeof(autosave_path), "%s.autosave", path);

    if (file_exists(autosave_path)) {
        // Check if autosave is newer than main file
        if (file_mtime(autosave_path) > file_mtime(path)) {
            return load_workbook(autosave_path);
        }
    }

    return load_workbook(path);
}
```

## Version Migration

Handle format changes:

```c
Workbook* load_with_migration(const char* content) {
    int version = detect_version(content);

    switch (version) {
        case 1:
            return parse_v1(content);
        case 2:
            return parse_v2(content);
        default:
            // Unknown version
            return NULL;
    }
}

// Migration functions
Workbook* migrate_v1_to_v2(Workbook* v1) {
    // Apply schema changes
    // ...
}
```

## Compression Options

For binary format or large text files:

| Algorithm | Ratio | Speed | Notes |
|-----------|-------|-------|-------|
| zstd | Excellent | Fast | Recommended |
| lz4 | Good | Very fast | For realtime |
| gzip | Good | Medium | Universal compat |

```c
void serialize_compressed(Workbook* wb, const char* path) {
    // Serialize to memory
    char* uncompressed = serialize_to_string(wb);
    size_t len = strlen(uncompressed);

    // Compress
    size_t compressed_size = ZSTD_compressBound(len);
    char* compressed = malloc(compressed_size);
    compressed_size = ZSTD_compress(compressed, compressed_size,
                                     uncompressed, len, 3);

    // Write header + compressed data
    FILE* f = fopen(path, "wb");
    write_compression_header(f, len, compressed_size);
    fwrite(compressed, 1, compressed_size, f);
    fclose(f);
}
```
