# Persistence & File Format

## Design Goals

1. **Git-friendly**: One entity per line, sorted by creation time, clean diffs
2. **Human-readable**: Compact but understandable, editable if needed
3. **Efficient**: Fast to parse, reasonable file size
4. **Complete**: Preserves all data including history (for CRDT)
5. **Merge-friendly**: Concurrent edits produce minimal conflicts

## Key Design Decisions

### 1. Short IDs (8-char Base62)

Full UUIDs (36 chars) are too verbose. We use 8-character base62 IDs:

```
Full UUID:  550e8400-e29b-41d4-a716-446655440000  (36 chars)
Short ID:   Kj7mXp2Q                               (8 chars)
```

8 chars of base62 = 62^8 = **218 trillion** combinations.

### 2. One Entity Per Line

Each cell, column, or row is a single line:
- Editing a cell changes exactly one line
- Adding a cell adds exactly one line
- Git diffs are minimal and readable

### 3. Sorted by Creation Time (HLC)

Entities are sorted by their HLC timestamp, not by visual position:
- New cells append to the end of the section
- Inserts don't shift other lines
- Concurrent edits to different cells don't conflict

### 4. Position-Based Ordering

Visual order of columns/rows is stored via explicit positions (0-indexed integers).
Each axis knows its own position, making it simple to sort and serialize.

## Text Format (`.zcd`)

### File Structure

```
#zcd v1
D <doc-id> "<name>"
N "<name>" <scope> <sheet-id> <type> <target-data>  # Named ranges (optional)

S <sheet-id> "<name>"
V <key:value...>                      # Sheet view properties (optional)
C <id> <position> [props...]
R <id> <position> [props...]
X <id> <col-id> <row-id> <type> <value> [props...]
RG <id> <corners> <flags> [sty:<base64>] [fmt:<base64>]  # Ranges (merged cells, styles, formats)

#oplog
O <hlc> <op-type> <target-id> <payload>
```

### Example

A simple spreadsheet with: A1=2, A2="foo", D4==A1+10

```
#zcd v1
D Qx7mXp2L "Untitled"

S bF3hL8mN "Sheet1"
C kR7pN2wQ 0
C vT5mK9xL 3
R jH4sW8nF 0
R qM2kL5pR 1
R yB9tX3wN 3
X nP6kR2mW kR7pN2wQ jH4sW8nF n 2
X hT8sL4xQ kR7pN2wQ qM2kL5pR s "foo"
X wK3nJ7pM vT5mK9xL yB9tX3wN f "=$kR7pN2wQ$jH4sW8nF+10"
```

### Line Prefixes

| Prefix | Meaning | Format |
|--------|---------|--------|
| `#zcd` | Format version | `#zcd v1` |
| `D` | Document | `D <id> "<name>"` |
| `N` | Named range | `N "<name>" <scope> <sheet-id> <type> <data>` |
| `S` | Sheet | `S <id> "<name>"` |
| `V` | Sheet view | `V <key:value...>` |
| `C` | Column | `C <id> <position> [props...]` |
| `R` | Row | `R <id> <position> [props...]` |
| `X` | Cell | `X <id> <col> <row> <type> <value> [props...]` |
| `RG` | Range | `RG <id> <corners> <flags> [sty:<base64>] [fmt:<base64>]` |
| `O` | Operation | `O <hlc> <op-type> <target-id> <payload>` |

Styles and formats are content-addressed and stored inline as `sty:<base64>` and `fmt:<base64>` properties on columns, rows, cells, and ranges.

### Position Notation

- Position is a 0-indexed integer
- Sparse positions are allowed (e.g., columns at 0, 3, 10)

**Optional Properties:**
- `w:<width>` / `h:<height>`: Size in pixels
- `name:"<name>"`: Axis name
- `hidden:1`: Hidden axis
- `sty:<base64>`: Default style (content-addressed)
- `fmt:<base64>`: Default number format (content-addressed)

### Cell Types

| Code | Type | Value Format |
|------|------|--------------|
| `n` | Number | `42`, `3.14`, `-100` |
| `s` | String | `"Hello"` (quoted) |
| `f` | Formula | `"=$cA$r1+10"` (quoted, ID-based refs) |
| `b` | Boolean | `true` or `false` |
| `e` | Error | `#DIV/0!`, `#REF!`, etc. |
| `d` | Date | `2024-01-15` (ISO 8601) |
| `t` | DateTime | `2024-01-15T10:30:00Z` (ISO 8601) |

### Formula References

Formulas use stable ID-based references, not A1 notation:

```
User sees:    =SUM(B2:D2)
File stores:  =SUM($cB$r2:$cD$r2)
```

## Git Diff Examples

### Editing a Cell Value

User changes A1 from 2 to 42:

```diff
-X nP6kR2mW kR7pN2wQ jH4sW8nF n 2
+X nP6kR2mW kR7pN2wQ jH4sW8nF n 42
```

**Result**: 1 line changed.

### Insert Column

```diff
 C kR7pN2wQ 0
+C mT3xK8pW 1
 C vT5mK9xL 3
```

**Result**: 1 line added. No section markers needed.

### Concurrent Edits (No Conflict)

Alice edits A1, Bob edits A2 (different cells) - Git auto-merges.

### Concurrent Edits (Same Cell = Conflict)

```
<<<<<<< HEAD
X nP6kR2mW kR7pN2wQ jH4sW8nF n 100
=======
X nP6kR2mW kR7pN2wQ jH4sW8nF n 200
>>>>>>> bob
```

Clear 1-line conflict. User picks one.

## Comparison with Other Formats

| Format | Edit Cell | Insert Column | Merge Quality |
|--------|-----------|---------------|---------------|
| CSV | 1 line | All lines shift | Poor |
| JSON | ~3 lines | ~5 lines | Poor |
| XLSX | Binary | Binary | Impossible |
| **.zcd** | 1 line | 1 line | Excellent |

## File Format Variants

| Extension | Format | Use Case |
|-----------|--------|----------|
| `.zcd` | Raw text | Git repos, human editing |
| `.zcdz` | Text + zstd | Smaller, still diffable |
| `.zcdb` | Binary | Large files, fast loading |
| `.zcdbz` | Binary + zstd | Maximum compression |

For detailed format specification, see [docs/file-format.md](file-format.md).

## Binary Format (`.zcdb`)

Section-based structure with header, section table, and compressed sections. Supports random access and streaming. See implementation for details.

## Implementation

- Parser: `core/cells/parser.cc`
- Serializer: `core/cells/serializer.cc`
- Test files: `testdata/*.zcd`
