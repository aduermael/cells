# Persistence & File Format

## Design Goals

1. **Git-friendly**: One entity per line, sorted by creation time, clean diffs
2. **Human-readable**: Compact but understandable, editable if needed
3. **Efficient**: Fast to parse, reasonable file size
4. **Complete**: Preserves all data including history (for CRDT)
5. **Merge-friendly**: Concurrent edits produce minimal conflicts

## Key Design Decisions

### 1. Short IDs (Not Full UUIDs)

Full UUIDs (36 chars) are too verbose. We use **8-character base62 IDs**:

```
Full UUID:  550e8400-e29b-41d4-a716-446655440000  (36 chars)
Short ID:   Kj7mXp2Q                               (8 chars)
```

8 chars of base62 = 62^8 = **218 trillion** combinations. More than enough for any document, and still globally unique enough in practice.

```c
// ID generation (with rejection sampling to avoid modulo bias)
char* generate_id() {
    static const char base62[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    char id[9];
    for (int i = 0; i < 8; i++) {
        uint8_t r;
        do {
            r = random_byte();
        } while (r >= 248);  // 248 = 62*4, reject to avoid bias
        id[i] = base62[r % 62];
    }
    id[8] = '\0';
    return strdup(id);
}
```

### 2. One Entity Per Line

Each cell, column, or row is a single line. This means:
- Editing a cell changes exactly one line
- Adding a cell adds exactly one line
- Git diffs are minimal and readable

### 3. Sorted by Creation Time (HLC)

Entities are sorted by their HLC timestamp, not by visual position. This means:
- **New cells append** to the end of the section (usually)
- **Inserts don't shift** other lines
- **Concurrent edits** to different cells don't conflict

### 4. Order Stored Separately from Content

The visual order of columns/rows is stored in an **order list**, separate from the axis definitions. This way:
- Reordering columns = one line changes (the order list)
- Column properties are independent of position

## Text Format (`.cells`)

### File Structure

```
#cells v1
D <doc-id> "<name>"

S <sheet-id> "<name>"

#cols
C <id> <prev>[:<gap>] <next>[:<gap>] [props...]

#rows
R <id> <prev>[:<gap>] <next>[:<gap>] [props...]

#cells
X <id> <col-id> <row-id> <type> <value>

#styles
T <id> <properties...>

#cell-styles
Y <cell-id> <style-ids>

#oplog
O <hlc> <op-type> <args...>
```

### Complete Example

A simple spreadsheet with: A1=2, A2="foo", D4==A1+10

```
#cells v1
D Qx7mXp2L "Untitled"

S bF3hL8mN "Sheet1"

#cols
C kR7pN2wQ ~ vT5mK9xL:2
C vT5mK9xL kR7pN2wQ:2 ~

#rows
R jH4sW8nF ~ qM2kL5pR
R qM2kL5pR jH4sW8nF yB9tX3wN:1
R yB9tX3wN qM2kL5pR:1 ~

#cells
X nP6kR2mW kR7pN2wQ jH4sW8nF n 2
X hT8sL4xQ kR7pN2wQ qM2kL5pR s "foo"
X wK3nJ7pM vT5mK9xL yB9tX3wN f "=$kR7pN2wQ$jH4sW8nF+10"
```

**Reading this file:**

| ID | Type | Meaning |
|----|------|---------|
| `Qx7mXp2L` | Doc | Document ID |
| `bF3hL8mN` | Sheet | Sheet ID |
| `kR7pN2wQ` | Column | First column → displays as **A** |
| `vT5mK9xL` | Column | Second column, gap:2 before it → displays as **D** |
| `jH4sW8nF` | Row | First row → displays as **1** |
| `qM2kL5pR` | Row | Second row → displays as **2** |
| `yB9tX3wN` | Row | Third row, gap:1 before it → displays as **4** |

The formula `=$kR7pN2wQ$jH4sW8nF+10` references column `kR7pN2wQ` (A) and row `jH4sW8nF` (1), so the UI displays it as `=A1+10`.

### A Larger Example (Budget Spreadsheet)

```
#cells v1
D Kj7mXp2Q "Budget 2024"

S tH2kM6xN "Q1 Expenses"

#cols
C fR3pK7wN ~ gT8mL2sQ w:120 name:"Category"
C gT8mL2sQ fR3pK7wN hW4nJ9xR w:80 name:"Jan"
C hW4nJ9xR gT8mL2sQ jK6pM3wT w:80 name:"Feb"
C jK6pM3wT hW4nJ9xR kN8sR4xW w:80 name:"Mar"
C kN8sR4xW jK6pM3wT ~ w:100 name:"Total"

#rows
R mP2wK7nL ~ nQ4xL8pM h:30 name:"Header"
R nQ4xL8pM mP2wK7nL pR6yM9qN
R pR6yM9qN nQ4xL8pM qS8zN2rP:6
R qS8zN2rP pR6yM9qN:6 ~ name:"Total"

#cells
X aB3cD4eF fR3pK7wN mP2wK7nL s "Category"
X bC4dE5fG gT8mL2sQ mP2wK7nL s "Jan"
X cD5eF6gH hW4nJ9xR mP2wK7nL s "Feb"
X dE6fG7hJ jK6pM3wT mP2wK7nL s "Mar"
X eF7gH8jK kN8sR4xW mP2wK7nL s "Total"
X fG8hJ9kL fR3pK7wN nQ4xL8pM s "Rent"
X gH9jK2lM gT8mL2sQ nQ4xL8pM n 2000
X hJ2kL3mN hW4nJ9xR nQ4xL8pM n 2000
X jK3lM4nP jK6pM3wT nQ4xL8pM n 2000
X kL4mN5pQ kN8sR4xW nQ4xL8pM f "=SUM($gT8mL2sQ$nQ4xL8pM:$jK6pM3wT$nQ4xL8pM)"
X lM5nP6qR fR3pK7wN pR6yM9qN s "Utilities"
X mN6pQ7rS gT8mL2sQ pR6yM9qN n 150
X nP7qR8sT hW4nJ9xR pR6yM9qN n 180
X pQ8rS9tW jK6pM3wT pR6yM9qN n 165
X qR9sT2wX kN8sR4xW pR6yM9qN f "=SUM($gT8mL2sQ$pR6yM9qN:$jK6pM3wT$pR6yM9qN)"
X rS2tW3xY fR3pK7wN qS8zN2rP s "Total"
X sT3wX4yZ gT8mL2sQ qS8zN2rP f "=SUM($gT8mL2sQ$nQ4xL8pM:$gT8mL2sQ$pR6yM9qN)"
X tW4xY5zA hW4nJ9xR qS8zN2rP f "=SUM($hW4nJ9xR$nQ4xL8pM:$hW4nJ9xR$pR6yM9qN)"
X wX5yZ6aB jK6pM3wT qS8zN2rP f "=SUM($jK6pM3wT$nQ4xL8pM:$jK6pM3wT$pR6yM9qN)"
X xY6zA7bC kN8sR4xW qS8zN2rP f "=SUM($kN8sR4xW$nQ4xL8pM:$kN8sR4xW$pR6yM9qN)"

#styles
T sB3kL7mN B
T sH4nP8qR B bg:#4472C4 fg:#FFFFFF
T sM5rT9wX fmt:$#,##0.00

#cell-styles
Y aB3cD4eF sH4nP8qR
Y bC4dE5fG sH4nP8qR
Y cD5eF6gH sH4nP8qR
Y dE6fG7hJ sH4nP8qR
Y eF7gH8jK sH4nP8qR
Y kL4mN5pQ sB3kL7mN,sM5rT9wX
Y sT3wX4yZ sB3kL7mN,sM5rT9wX
Y tW4xY5zA sB3kL7mN,sM5rT9wX
Y wX5yZ6aB sB3kL7mN,sM5rT9wX
Y xY6zA7bC sB3kL7mN,sM5rT9wX
```

All IDs are random 8-char base62 strings. The UI computes display names (A, B, C... and 1, 2, 3...) from the linked list structure.

### Format Specification

#### Line Prefixes

| Prefix | Meaning | Format |
|--------|---------|--------|
| `#cells` | Format version | `#cells v1` |
| `D` | Document ID & name | `D <id> "<name>"` |
| `S` | Sheet definition | `S <id> "<name>"` |
| `#cols` | Start columns section | |
| `C` | Column (doubly-linked) | `C <id> <prev>[:<gap>] <next>[:<gap>] [props...]` |
| `#rows` | Start rows section | |
| `R` | Row (doubly-linked) | `R <id> <prev>[:<gap>] <next>[:<gap>] [props...]` |
| `#cells` | Start cells section | |
| `X` | Cell | `X <id> <col> <row> <type> <value>` |
| `#styles` | Start styles section | |
| `T` | Style definition | `T <id> <properties>` |
| `#cell-styles` | Cell-style mappings | |
| `Y` | Cell-style mapping | `Y <cell-id> <style-ids>` |
| `#oplog` | Start operation log | |
| `O` | Operation entry | `O <hlc> <op-type> <args...>` |

#### Column/Row Linked List Format

```
C <id> <prev>[:<gap-before>] <next>[:<gap-after>] [key:value...]
```

- `~` = null (no prev/next)
- `:<gap>` = number of empty columns/rows in between (default 0, can be omitted)
- Props: `w:<width>`, `h:<height>`, `name:"<name>"`, etc.

**Examples:**

| Line | Meaning |
|------|---------|
| `C cA ~ cD:2` | Column A: no prev, next is cD with 2 empty cols (B,C) between |
| `C cD cA:2 ~` | Column D: prev is cA (gap 2), no next |
| `R r1 ~ r2` | Row 1: no prev (gap 0), next is r2 (gap 0) |
| `R r2 r1 r4:1` | Row 2: prev is r1 (gap 0), next is r4 (gap 1 = row 3 empty) |
| `C cX ~ cY w:200 name:"Total"` | Column with custom width and name |

#### Node ID (Runtime, Not Stored)

Node IDs identify replicas in the CRDT system but are **not stored in document files**. They are a runtime concern:

- **Stored in engine config** (e.g., `~/.cells/node_id` or app preferences)
- **Generated once per install** and reused across all documents
- **Used when creating new operations** (the engine stamps its node ID on edits)

Node IDs appear in **OpLog entries** as historical attribution (who made each change):

```
HLC: 1705312200000.0.N3f8hJ2w
     └─timestamp──┘ │ └─node─┘
                    └─logical counter
```

When two users edit the same cell at the exact same millisecond, node ID breaks the tie deterministically.

This separation means:
- Files contain document data, not instance identity
- Sharing files doesn't cause node ID conflicts
- No unnecessary diffs when different users save

#### Cell Types

| Code | Type | Value Format |
|------|------|--------------|
| `n` | Number | `42`, `3.14`, `-100` |
| `s` | String | `"Hello"` (quoted) |
| `f` | Formula | `"=$cA$r1+10"` (quoted, ID-based refs) |
| `b` | Boolean | `true` or `false` |
| `e` | Error | `#DIV/0!`, `#REF!`, etc. |
| `d` | Date | `2024-01-15` (ISO 8601) |
| `t` | DateTime | `2024-01-15T10:30:00Z` (ISO 8601) |

#### Formula References

Formulas use stable ID-based references, not A1 notation:

```
User sees:    =SUM(B2:D2)
File stores:  =SUM($cB$r2:$cD$r2)
```

The `$` prefix indicates an ID reference. Benefits:
- **Stable**: Moving columns doesn't break formulas
- **Unambiguous**: No confusion with A1 notation
- **Convertible**: UI shows A1, file stores IDs

### Git Diff Examples

Using the simple example (A1=2, A2="foo", D4==A1+10):
- Column A = `kR7pN2wQ`
- Column D = `vT5mK9xL`
- Row 1 = `jH4sW8nF`
- Row 2 = `qM2kL5pR`
- Row 4 = `yB9tX3wN`

#### Editing a Cell Value

User changes A1 from 2 to 42:

```diff
-X nP6kR2mW kR7pN2wQ jH4sW8nF n 2
+X nP6kR2mW kR7pN2wQ jH4sW8nF n 42
```

**Result**: 1 line changed.

#### Insert Column B Between A and D

New column gets ID `mT3xK8pW`:

```diff
 #cols
-C kR7pN2wQ ~ vT5mK9xL:2
+C kR7pN2wQ ~ mT3xK8pW
+C mT3xK8pW kR7pN2wQ vT5mK9xL:1
-C vT5mK9xL kR7pN2wQ:2 ~
+C vT5mK9xL mT3xK8pW:1 ~
```

**Result**: 3 lines changed (new column + update both neighbors). Gap split from 2 to 1+1.

#### Reorder: Move Column D Before A

```diff
 #cols
-C kR7pN2wQ ~ vT5mK9xL:2
+C kR7pN2wQ vT5mK9xL ~:2
-C vT5mK9xL kR7pN2wQ:2 ~
+C vT5mK9xL ~ kR7pN2wQ
```

**Result**: 2 lines changed. All cells unchanged - they still reference the same column IDs.

#### Insert Row 3 Between Row 2 and Row 4

New row gets ID `hN5wR9kL`:

```diff
 #rows
 R jH4sW8nF ~ qM2kL5pR
-R qM2kL5pR jH4sW8nF yB9tX3wN:1
+R qM2kL5pR jH4sW8nF hN5wR9kL
+R hN5wR9kL qM2kL5pR yB9tX3wN
-R yB9tX3wN qM2kL5pR:1 ~
+R yB9tX3wN hN5wR9kL ~
```

**Result**: 3 lines changed. Gap consumed by new row.

#### Concurrent Edits (No Conflict)

Alice edits cell A1, Bob edits cell A2 (different cells):

```diff
# Alice's change
-X nP6kR2mW kR7pN2wQ jH4sW8nF n 2
+X nP6kR2mW kR7pN2wQ jH4sW8nF n 100

# Bob's change
-X hT8sL4xQ kR7pN2wQ qM2kL5pR s "foo"
+X hT8sL4xQ kR7pN2wQ qM2kL5pR s "bar"
```

**Result**: Git auto-merges. No conflict.

#### Concurrent Edits (Same Cell = Conflict)

Alice and Bob both edit cell A1:

```
<<<<<<< HEAD
X nP6kR2mW kR7pN2wQ jH4sW8nF n 100
=======
X nP6kR2mW kR7pN2wQ jH4sW8nF n 200
>>>>>>> bob
```

**Result**: Clear 1-line conflict. User picks one.

#### Concurrent Column Insert (CRDT Resolution)

Alice inserts column B after A, Bob inserts column C after A (same position):

Alice creates: `C aL2mN4pQ kR7pN2wQ vT5mK9xL:1`
Bob creates: `C bM3nP5qR kR7pN2wQ vT5mK9xL:1`

Both claim to be between A and D. CRDT resolution by HLC timestamp determines order.
Result: A → B → C → D or A → C → B → D (deterministic based on timestamps).

### Why This Format Works Well with Git

| Operation | Lines Changed | Conflicts With |
|-----------|---------------|----------------|
| Edit cell value | 1 | Same cell only |
| Add cell | 1 | Nothing |
| Delete cell | 1 | Same cell only |
| Insert column | 3 | Adjacent columns |
| Insert row | 3 | Adjacent rows |
| Reorder column | 2 | Same column's neighbors |
| Rename/resize column | 1 | Same column only |
| Add style | 1 | Nothing |
| Apply style to cell | 1 | Same cell only |

### Comparison with Other Formats

| Format | Edit Cell | Insert Row | Reorder | Merge Quality |
|--------|-----------|------------|---------|---------------|
| **CSV** | 1 line | All lines shift | All lines | Poor |
| **JSON** | ~3 lines | ~5 lines | Many lines | Poor |
| **XLSX** | Binary | Binary | Binary | Impossible |
| **This format** | 1 line | 3 lines | 2 lines | Excellent |

### CRDT OpLog Format

For sync and history, include operation log:

```
#oplog
O 1705312200000.0.N1 cell.set x007 n 2000
O 1705312200001.0.N1 cell.set x008 n 2000
O 1705312205000.0.N2 cell.set x007 n 2100
O 1705312210000.0.N1 row.insert r3 after:r2
O 1705312215000.0.N1 col.resize cA w:150
```

Format: `O <hlc> <op-type> <args...>`

HLC format: `<wall_ms>.<logical>.<node_id>`

Enables:
- Full history reconstruction
- CRDT sync between peers
- Time-travel / undo

The oplog is optional - a file without `#oplog` is a snapshot (current state only).

## Streaming & Progressive Loading

### Section Order for Streaming

File sections are ordered for progressive loading:

```
#cells v1
D ...                # 1. Document metadata (instant)
S ...                # 2. Sheet structure
#cols / #rows        # 3. Grid structure (render empty grid)
#cells               # 4. Cell data (progressive fill)
#styles              # 5. Styles (apply after cells visible)
#cell-styles         # 6. Style mappings
#oplog               # 7. History (background, optional)
```

**Streaming behavior:**
1. Parse document/sheet metadata → show loading UI
2. Parse columns/rows → render empty grid skeleton
3. Stream cells → fill grid progressively, evaluate formulas on-the-fly
4. Apply styles → visual polish

The formula engine is fast enough to evaluate in real-time as cells stream in.

## File Format Variants

Like glTF/GLB for 3D, we support multiple formats:

| Extension | Format | Use Case |
|-----------|--------|----------|
| `.cells` | Raw text | Git repos, human editing |
| `.cellsz` | Text + zstd | Smaller, still diffable with git filter |
| `.cellsb` | Binary | Large files, fast loading |

### Size Comparison (10K cells)

| Format | Size | Parse Time |
|--------|------|------------|
| `.cells` | ~400 KB | ~40ms |
| `.cellsz` | ~80 KB | ~45ms |
| `.cellsb` | ~120 KB | ~5ms |
| `.cellsb` + zstd | ~50 KB | ~8ms |

## File Size Analysis

### Per-Cell Overhead

**Our .cells format (text):**
```
X nP6kR2mW kR7pN2wQ jH4sW8nF n 42
```
- Cell ID: 8 chars
- Col ID: 8 chars
- Row ID: 8 chars
- Structure: ~10 chars (prefixes, spaces, newline)
- **Fixed overhead: ~34 bytes + value**

**XLSX (uncompressed XML inside zip):**
```xml
<c r="A1"><v>42</v></c>
```
- Cell ref: 2-5 chars (A1 to XFD1048576)
- Structure: ~15 chars
- **Fixed overhead: ~17-20 bytes + value**

XLSX is always zip-compressed; our format has .cellsz and .cellsb options.

### Comparison by Scenario

| Scenario | .cells | .cellsz | .cellsb+zstd | XLSX |
|----------|--------|---------|--------------|------|
| 10K numeric cells | ~400 KB | ~80 KB | ~50 KB | ~60 KB |
| 10K text cells | ~600 KB | ~120 KB | ~80 KB | ~80 KB |
| 10K formula cells | ~800 KB | ~150 KB | ~90 KB | ~100 KB |
| Sparse: 100 cells in 1M grid | ~4 KB | ~1 KB | ~1 KB | ~8 KB |
| 1M cells (numbers) | ~40 MB | ~8 MB | ~5 MB | ~6 MB |

### Where UUIDs Cost Most

Formulas have the highest UUID overhead since each cell reference uses two 8-char IDs:

```
Excel:  =SUM(B2:D10)
Ours:   =SUM($fG7nP2wR$qM2kL5pR:$jK4sT8yL$yB9tX3wN)
```

A formula-heavy sheet can be 2-3x larger in uncompressed text. Compression largely eliminates this difference.

### Why UUIDs Are Worth It

1. **Compression closes the gap** - .cellsz is within 20-30% of XLSX
2. **Binary beats XLSX** - .cellsb+zstd is often smaller than XLSX
3. **Sparse data wins** - we only store existing cells
4. **Git-friendliness** - text overhead enables clean diffs
5. **CRDT stability** - UUIDs enable conflict-free collaboration

### Recommended Format by Use Case

| Use Case | Format | Rationale |
|----------|--------|-----------|
| Git repos, < 10K cells | `.cells` | Human-readable, clean diffs |
| Git repos, 10K-100K cells | `.cellsz` | Compressed, diffable with git filter |
| Large files, fast loading | `.cellsb` | Binary, random access |
| Sharing, archival | `.cellsb` + zstd | Smallest size |

The 8-char base62 IDs (62^8 = 218 trillion combinations) are the minimum viable size for collision resistance in large collaborative documents.

## Binary Format (`.cellsb`)

### Structure

```
┌─────────────────────────────────────────────────────────────┐
│ Header (32 bytes)                                           │
│   Magic: "CELLSB" (6 bytes)                                 │
│   Version: u16                                              │
│   Flags: u32 (compression, etc.)                            │
│   Section count: u16                                        │
│   Reserved: 16 bytes                                        │
├─────────────────────────────────────────────────────────────┤
│ Section Table (24 bytes × section count)                    │
│   Type: u8                                                  │
│   Flags: u8 (compressed, etc.)                              │
│   Reserved: u16                                             │
│   Offset: u64                                               │
│   Compressed size: u32                                      │
│   Uncompressed size: u32                                    │
├─────────────────────────────────────────────────────────────┤
│ Section: Document Meta                                      │
├─────────────────────────────────────────────────────────────┤
│ Section: Sheets                                             │
├─────────────────────────────────────────────────────────────┤
│ Section: Columns (per sheet)                                │
├─────────────────────────────────────────────────────────────┤
│ Section: Rows (per sheet)                                   │
├─────────────────────────────────────────────────────────────┤
│ Section: Cells (per sheet, optionally compressed)           │
├─────────────────────────────────────────────────────────────┤
│ Section: Styles                                             │
├─────────────────────────────────────────────────────────────┤
│ Section: Cell-Styles                                        │
├─────────────────────────────────────────────────────────────┤
│ Section: OpLog (optional, compressed)                       │
└─────────────────────────────────────────────────────────────┘
```

### Section Types

```c
enum SectionType {
    SECTION_DOC_META    = 0x01,
    SECTION_SHEETS      = 0x02,
    SECTION_COLUMNS     = 0x10,  // + sheet index
    SECTION_ROWS        = 0x11,  // + sheet index
    SECTION_CELLS       = 0x12,  // + sheet index
    SECTION_STRINGS     = 0x20,  // String table (deduped)
    SECTION_STYLES      = 0x30,
    SECTION_CELL_STYLES = 0x31,
    SECTION_OPLOG       = 0x40,
};
```

### Binary Cell Encoding

```c
struct BinaryCell {
    u64 id;           // 8-byte ID (base62 decoded)
    u64 col_id;       // Column reference
    u64 row_id;       // Row reference
    u8  type;         // CELL_NUMBER, CELL_STRING, etc.
    u8  flags;        // Reserved
    u16 style_idx;    // Index into styles (0 = none)

    union {
        f64 number;
        u32 string_idx;   // Index into string table
        u32 formula_idx;  // Index into string table (for formulas)
    } value;

    u32 _padding;     // Align to 32 bytes
};
// 32 bytes per cell (fixed size for random access)
```

### String Table (Deduplication)

Strings are stored once in a string table:

```
┌─────────────────────────────────────┐
│ String Table Section                │
│   Count: u32                        │
│   Offsets: [u32 × count]            │
│   Data: [length-prefixed strings]   │
└─────────────────────────────────────┘
```

Cell values and formulas reference strings by index → massive size reduction for repeated strings.

### Streaming Binary Loading

```c
// 1. Read header (32 bytes)
Header header = read_header(file);

// 2. Read section table (know where everything is)
Section* sections = read_section_table(file, header.section_count);

// 3. Load sections progressively
load_section(file, sections[SECTION_DOC_META]);     // Instant
load_section(file, sections[SECTION_SHEETS]);       // Grid structure
load_section(file, sections[SECTION_COLUMNS]);      // Can render headers
load_section(file, sections[SECTION_ROWS]);

// 4. Stream cells (largest section) with progress callback
stream_cells(file, sections[SECTION_CELLS], on_cell_loaded);

// 5. Background: styles, oplog
load_section_async(file, sections[SECTION_STYLES]);
load_section_async(file, sections[SECTION_OPLOG]);
```

### Random Access

Binary format enables loading specific sheets without parsing entire file:

```c
// Load only sheet 2's cells
Section cell_section = find_section(sections, SECTION_CELLS, sheet_idx=2);
seek(file, cell_section.offset);
Cell* cells = load_cells(file, cell_section.size);
```

### Compression

Per-section compression with zstd:

```c
if (section.flags & FLAG_COMPRESSED) {
    void* compressed = read(file, section.compressed_size);
    void* data = zstd_decompress(compressed, section.uncompressed_size);
} else {
    void* data = read(file, section.uncompressed_size);
}
```

Cells section compresses well due to:
- Repeated column/row IDs
- Similar numeric values
- Formula patterns

## Format Detection

```c
FileFormat detect_format(const char* path) {
    uint8_t magic[6];
    read(file, magic, 6);

    if (memcmp(magic, "CELLSB", 6) == 0) return FORMAT_BINARY;
    if (memcmp(magic, "#cells", 6) == 0) return FORMAT_TEXT;
    if (magic[0] == 0x28 && magic[1] == 0xB5) return FORMAT_TEXT_ZSTD; // zstd magic

    return FORMAT_UNKNOWN;
}
```

## Parser Implementation

### Text Format Parser

```c
typedef enum Section {
    SECTION_NONE,
    SECTION_COLS,
    SECTION_ROWS,
    SECTION_CELLS,
    SECTION_STYLES,
    SECTION_CELL_STYLES,
    SECTION_OPLOG,
} Section;

typedef struct Parser {
    const char* input;
    size_t pos;
    size_t len;
    int line_num;
    Section section;
    Workbook* workbook;
    Sheet* current_sheet;
    char error[256];
} Parser;

Workbook* parse_cells_file(const char* content, size_t len) {
    Parser p = {
        .input = content,
        .len = len,
        .workbook = workbook_new(),
        .section = SECTION_NONE
    };

    char line[4096];
    while (read_line(&p, line, sizeof(line)) && !p.error[0]) {
        p.line_num++;
        if (line[0] == '\0') continue;  // Skip empty lines

        char prefix = line[0];
        switch (prefix) {
            case '#':  // Section header or version
                if (strncmp(line, "#cells", 6) == 0) {
                    parse_version(&p, line);
                } else {
                    parse_section_header(&p, line);
                }
                break;
            case 'D':  // Document
                parse_document(&p, line);
                break;
            case 'S':  // Sheet
                parse_sheet(&p, line);
                break;
            case 'C':  // Column
                parse_column(&p, line);
                break;
            case 'R':  // Row
                parse_row(&p, line);
                break;
            case 'X':  // Cell
                parse_cell(&p, line);
                break;
            case 'T':  // Style
                parse_style(&p, line);
                break;
            case 'Y':  // Cell-style mapping
                parse_cell_style(&p, line);
                break;
            case 'O':  // OpLog entry
                parse_oplog_entry(&p, line);
                break;
        }
    }

    if (p.error[0]) {
        fprintf(stderr, "Parse error line %d: %s\n", p.line_num, p.error);
        workbook_free(p.workbook);
        return NULL;
    }

    return p.workbook;
}

// Parse link with optional gap: "cA" or "cA:2" or "~" or "~:1"
void parse_link(const char* token, char* id_out, int* gap_out) {
    *gap_out = 0;  // Default gap
    char* colon = strchr(token, ':');
    if (colon) {
        size_t id_len = colon - token;
        strncpy(id_out, token, id_len);
        id_out[id_len] = '\0';
        *gap_out = atoi(colon + 1);
    } else {
        strcpy(id_out, token);
    }
}

// Parse column: C <id> <prev>[:<gap>] <next>[:<gap>] [props...]
void parse_column(Parser* p, const char* line) {
    char id[16], prev_tok[24], next_tok[24];

    if (sscanf(line, "C %15s %23s %23s", id, prev_tok, next_tok) < 3) {
        snprintf(p->error, sizeof(p->error), "Invalid column: %s", line);
        return;
    }

    Axis* col = axis_new(id);
    parse_link(prev_tok, col->prev_id, &col->gap_before);
    parse_link(next_tok, col->next_id, &col->gap_after);

    // Parse optional props: w:100 name:"Foo"
    const char* props = strstr(line, next_tok) + strlen(next_tok);
    parse_axis_props(col, props);

    sheet_add_column(p->current_sheet, col);
}

// Parse row: R <id> <prev>[:<gap>] <next>[:<gap>] [props...]
void parse_row(Parser* p, const char* line) {
    char id[16], prev_tok[24], next_tok[24];

    if (sscanf(line, "R %15s %23s %23s", id, prev_tok, next_tok) < 3) {
        snprintf(p->error, sizeof(p->error), "Invalid row: %s", line);
        return;
    }

    Axis* row = axis_new(id);
    parse_link(prev_tok, row->prev_id, &row->gap_before);
    parse_link(next_tok, row->next_id, &row->gap_after);

    const char* props = strstr(line, next_tok) + strlen(next_tok);
    parse_axis_props(row, props);

    sheet_add_row(p->current_sheet, row);
}

// Parse cell: X <id> <col> <row> <type> <value>
void parse_cell(Parser* p, const char* line) {
    char id[16], col[16], row[16], type;
    int offset;

    if (sscanf(line, "X %15s %15s %15s %c %n", id, col, row, &type, &offset) < 4) {
        snprintf(p->error, sizeof(p->error), "Invalid cell: %s", line);
        return;
    }

    Cell* cell = cell_new(id);
    cell->col_id = intern_id(col);
    cell->row_id = intern_id(row);

    const char* value = line + offset;
    switch (type) {
        case 'n':
            cell->type = CELL_NUMBER;
            cell->value.number = strtod(value, NULL);
            break;
        case 's':
            cell->type = CELL_STRING;
            cell->value.string = parse_quoted_string(value);
            break;
        case 'f':
            cell->type = CELL_FORMULA;
            cell->formula = parse_quoted_string(value);
            break;
        case 'b':
            cell->type = CELL_BOOLEAN;
            cell->value.boolean = strcmp(value, "true") == 0;
            break;
        case 'e':
            cell->type = CELL_ERROR;
            cell->value.error = strdup(value);
            break;
        case 'd':
            cell->type = CELL_DATE;
            cell->value.date = parse_iso_date(value);
            break;
        case 't':
            cell->type = CELL_DATETIME;
            cell->value.datetime = parse_iso_datetime(value);
            break;
    }

    sheet_add_cell(p->current_sheet, cell);
}
```

### Streaming Parser

For large files, parse incrementally:

```c
typedef void (*CellCallback)(Cell* cell, void* ctx);
typedef void (*AxisCallback)(Axis* axis, int dimension, void* ctx);

void parse_streaming(FILE* file, CellCallback on_cell,
                     AxisCallback on_axis, void* ctx) {
    char line[4096];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';

        if (line[0] == 'X') {
            Cell* cell = parse_cell_line(line);
            on_cell(cell, ctx);
        } else if (line[0] == 'C') {
            Axis* col = parse_axis_line(line);
            on_axis(col, 0, ctx);
        } else if (line[0] == 'R') {
            Axis* row = parse_axis_line(line);
            on_axis(row, 1, ctx);
        }
    }
}
```

## Serializer Implementation

```c
void serialize_workbook(Workbook* wb, FILE* out) {
    // Header
    fprintf(out, "#cells v1\n");
    fprintf(out, "D %s \"%s\"\n", wb->id, escape_string(wb->name));
    fprintf(out, "\n");

    // Each sheet
    for (int s = 0; s < wb->sheet_count; s++) {
        serialize_sheet(wb->sheets[s], out);
    }

    // Styles
    if (wb->style_count > 0) {
        fprintf(out, "#styles\n");
        for (int i = 0; i < wb->style_count; i++) {
            serialize_style(wb->styles[i], out);
        }
        fprintf(out, "\n");
    }

    // Cell-style mappings
    if (wb->cell_style_count > 0) {
        fprintf(out, "#cell-styles\n");
        for (int i = 0; i < wb->cell_style_count; i++) {
            fprintf(out, "Y %s %s\n",
                    wb->cell_styles[i].cell_id,
                    wb->cell_styles[i].style_ids);
        }
        fprintf(out, "\n");
    }

    // OpLog (optional)
    if (wb->include_oplog && wb->oplog_count > 0) {
        fprintf(out, "#oplog\n");
        for (int i = 0; i < wb->oplog_count; i++) {
            serialize_op(wb->oplog[i], out);
        }
    }
}

void serialize_sheet(Sheet* sheet, FILE* out) {
    fprintf(out, "S %s \"%s\"\n\n", sheet->id, escape_string(sheet->name));

    // Columns (linked list order)
    fprintf(out, "#cols\n");
    Axis* col = sheet->first_col;
    while (col) {
        serialize_axis(col, 'C', out);
        col = col->next;
    }
    fprintf(out, "\n");

    // Rows (linked list order)
    fprintf(out, "#rows\n");
    Axis* row = sheet->first_row;
    while (row) {
        serialize_axis(row, 'R', out);
        row = row->next;
    }
    fprintf(out, "\n");

    // Cells (sorted by HLC for stable output)
    fprintf(out, "#cells\n");
    Cell** cells = sheet_cells_sorted_by_hlc(sheet);
    for (int i = 0; i < sheet->cell_count; i++) {
        serialize_cell(cells[i], out);
    }
    free(cells);
    fprintf(out, "\n");
}

// Serialize link with gap (omit :0)
void serialize_link(FILE* out, const char* id, int gap) {
    if (id == NULL || id[0] == '\0') {
        fprintf(out, "~");
    } else {
        fprintf(out, "%s", id);
    }
    if (gap > 0) {
        fprintf(out, ":%d", gap);
    }
}

void serialize_axis(Axis* axis, char prefix, FILE* out) {
    fprintf(out, "%c %s ", prefix, axis->id);
    serialize_link(out, axis->prev_id, axis->gap_before);
    fprintf(out, " ");
    serialize_link(out, axis->next_id, axis->gap_after);

    // Optional props (only if non-default)
    if (prefix == 'C' && axis->width != DEFAULT_COL_WIDTH) {
        fprintf(out, " w:%d", axis->width);
    }
    if (prefix == 'R' && axis->height != DEFAULT_ROW_HEIGHT) {
        fprintf(out, " h:%d", axis->height);
    }
    if (axis->name && axis->name[0]) {
        fprintf(out, " name:\"%s\"", escape_string(axis->name));
    }

    fprintf(out, "\n");
}

void serialize_cell(Cell* cell, FILE* out) {
    fprintf(out, "X %s %s %s ", cell->id, cell->col_id, cell->row_id);

    switch (cell->type) {
        case CELL_NUMBER:
            fprintf(out, "n %g\n", cell->value.number);
            break;
        case CELL_STRING:
            fprintf(out, "s \"%s\"\n", escape_string(cell->value.string));
            break;
        case CELL_FORMULA:
            fprintf(out, "f \"%s\"\n", escape_string(cell->formula));
            break;
        case CELL_BOOLEAN:
            fprintf(out, "b %s\n", cell->value.boolean ? "true" : "false");
            break;
        case CELL_ERROR:
            fprintf(out, "e %s\n", cell->value.error);
            break;
        case CELL_DATE:
            fprintf(out, "d %s\n", format_iso_date(cell->value.date));
            break;
        case CELL_DATETIME:
            fprintf(out, "t %s\n", format_iso_datetime(cell->value.datetime));
            break;
    }
}

void serialize_op(Operation* op, FILE* out) {
    fprintf(out, "O %llu.%u.%s ", op->hlc.wall_time, op->hlc.logical, op->hlc.node_id);

    switch (op->type) {
        case OP_CELL_SET:
            fprintf(out, "cell.set %s ", op->cell_id);
            serialize_cell_value(out, op->value_type, &op->value);
            break;
        case OP_CELL_CLEAR:
            fprintf(out, "cell.clear %s", op->cell_id);
            break;
        case OP_COL_INSERT:
            fprintf(out, "col.insert %s after:%s", op->axis_id, op->after_id);
            break;
        case OP_COL_DELETE:
            fprintf(out, "col.delete %s", op->axis_id);
            break;
        case OP_COL_RESIZE:
            fprintf(out, "col.resize %s w:%d", op->axis_id, op->size);
            break;
        case OP_ROW_INSERT:
            fprintf(out, "row.insert %s after:%s", op->axis_id, op->after_id);
            break;
        case OP_ROW_DELETE:
            fprintf(out, "row.delete %s", op->axis_id);
            break;
        // ... etc
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
