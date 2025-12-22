# ZCD File Format Specification

## Overview

**ZCD** (Zero-Conflict Document) is a text-based file format designed for spreadsheets with:
- Git-friendly structure (one entity per line, minimal merge conflicts)
- Human-readable content (editable in any text editor)
- CRDT-compatible operations (for real-time collaboration)
- Efficient parsing and serialization

## File Extension

| Extension | Format | Description |
|-----------|--------|-------------|
| `.zcd` | Plain text | Git-friendly, human-readable (primary format) |
| `.zcz` | Text + zstd | Compressed but still git-diffable |
| `.zcb` | Binary | Large files, fast random access |

## Format Version

Current version: **v1**

Files begin with a version header:
```
#cells v1
```

## Document Structure

A ZCD file contains the following sections in order:

```
#cells v1                           # Version header
D <doc-id> "<name>"                 # Document declaration

S <sheet-id> "<name>"               # Sheet declaration
#cols                               # Columns section marker
C <id> <position> [props...]        # Column definitions
#rows                               # Rows section marker
R <id> <position> [props...]        # Row definitions
#cells                              # Cells section marker
X <id> <col> <row> <type> <value>   # Cell definitions

#styles                             # Style definitions (future)
T <id> <properties...>
#cell-styles                        # Cell-style mappings (future)
Y <cell-id> <style-ids>

#oplog                              # Operation log section
O <hlc> <op-type> <target-id> <payload>
```

Multiple sheets repeat the `S`, `#cols`, `C`, `#rows`, `R`, `#cells`, `X` pattern.

## Entity Types

### Document (D)

Declares the workbook with a unique ID and name.

**Format:** `D <id> "<name>"`

**Example:**
```
D tY8pL3mK "Budget 2024"
```

### Sheet (S)

Declares a worksheet within the workbook.

**Format:** `S <id> "<name>"`

**Example:**
```
S qR5sW2xN "Summary"
```

### Column (C)

Defines a column with optional properties.

**Format:** `C <id> <position> [w:<width>] [name:"<name>"]`

**Fields:**
- `id`: 8-character base62 identifier
- `position`: 0-indexed column position
- `w:<width>`: Optional width in pixels (default: 100)
- `name:"<name>"`: Optional column name

**Examples:**
```
C cA1bC2dE 0                    # Column at position 0, default width
C cB3dE4fG 1 w:150             # Column at position 1, 150px wide
C cC5fG6hJ 2 name:"Total"      # Column at position 2 with name
C cD7hJ8kL 3 w:200 name:"Rev"  # Both width and name
```

### Row (R)

Defines a row with optional properties.

**Format:** `R <id> <position> [h:<height>] [name:"<name>"]`

**Fields:**
- `id`: 8-character base62 identifier
- `position`: 0-indexed row position
- `h:<height>`: Optional height in pixels (default: 24)
- `name:"<name>"`: Optional row name

**Examples:**
```
R rA1bC2dE 0                    # Row at position 0, default height
R rB3dE4fG 1 h:30              # Row at position 1, 30px tall
R rC5fG6hJ 2 name:"Header"     # Row at position 2 with name
```

### Cell (X)

Defines a cell value at a specific column/row intersection.

**Format:** `X <id> <col-id> <row-id> <type> <value>`

**Fields:**
- `id`: 8-character base62 cell identifier
- `col-id`: ID of the column containing this cell
- `row-id`: ID of the row containing this cell
- `type`: Single character indicating value type
- `value`: Type-specific value representation

See [Cell Types](#cell-types) for details.

### Style (T) - Future

Defines a reusable style.

**Format:** `T <id> <properties...>`

**Status:** Reserved for future implementation.

### Cell-Style Mapping (Y) - Future

Maps a cell to one or more styles.

**Format:** `Y <cell-id> <style-ids>`

**Status:** Reserved for future implementation.

### Operation (O)

Records a CRDT operation in the operation log.

**Format:** `O <hlc> <op-type> <target-id> <payload>`

**Fields:**
- `hlc`: Hybrid Logical Clock timestamp (`wall_time.logical.node_id`)
- `op-type`: Operation type string
- `target-id`: ID of the entity being modified
- `payload`: JSON object with operation-specific data

See [Operation Log](#operation-log) for details.

## Cell Types

| Code | Type | Value Format | Example |
|------|------|--------------|---------|
| `n` | Number | Decimal number | `42`, `3.14`, `-100.5` |
| `s` | String | Quoted string | `"Hello, World!"` |
| `f` | Formula | Quoted formula | `"=$cA$r1+10"`, `"=@xB1cD2eF"` |
| `b` | Boolean | Literal | `true`, `false` |
| `e` | Error | Error code | `#DIV/0!`, `#REF!`, `#VALUE!` |
| `d` | Date | ISO 8601 date | `2024-01-15` |
| `t` | DateTime | ISO 8601 datetime | `2024-01-15T10:30:00Z` |

### Number (n)

Numbers are stored as decimal values with full precision.

```
X xA1bC2dE cA1bC2dE rA1bC2dE n 42
X xB3dE4fG cA1bC2dE rB3dE4fG n 3.14159265358979
X xC5fG6hJ cA1bC2dE rC5fG6hJ n -100.5
```

### String (s)

Strings are quoted and support escape sequences:
- `\"` - Double quote
- `\\` - Backslash
- `\n` - Newline
- `\r` - Carriage return
- `\t` - Tab

```
X xA1bC2dE cA1bC2dE rA1bC2dE s "Hello, World!"
X xB3dE4fG cA1bC2dE rB3dE4fG s "Line 1\nLine 2"
X xC5fG6hJ cA1bC2dE rC5fG6hJ s "She said \"Hi\""
```

### Formula (f)

Formulas are quoted strings using ID-based cell references:

```
X xA1bC2dE cA1bC2dE rA1bC2dE f "=$cB3dE4fG$rB3dE4fG*2"
```

**Reference Format:** `$<col-id>$<row-id>`

The user sees traditional A1 notation (e.g., `=B2*2`), but the file stores stable ID references (e.g., `=$cB3dE4fG$rB3dE4fG*2`). This ensures formulas remain valid when columns/rows are inserted, deleted, or reordered.

**Shared Formula References:**

For formulas that are shared across multiple cells (like dragging a formula), subscribers reference the master cell:

```
X xMaster cA1 rA1 f "=B1+1"           # Master formula
X xSub1   cA1 rA2 f "=@xMaster"       # Subscriber references master
X xSub2   cA1 rA3 f "=@xMaster"       # Another subscriber
```

The `=@<cell-id>` syntax indicates the cell derives its formula from the referenced master cell, with appropriate offset adjustments applied at runtime.

### Boolean (b)

Boolean values are stored as `true` or `false` (lowercase).

```
X xA1bC2dE cA1bC2dE rA1bC2dE b true
X xB3dE4fG cA1bC2dE rB3dE4fG b false
```

### Error (e)

Error values are stored as unquoted error codes:

| Error Code | Meaning |
|------------|---------|
| `#VALUE!` | Wrong type of argument |
| `#REF!` | Invalid cell reference |
| `#NAME?` | Unrecognized formula name |
| `#DIV/0!` | Division by zero |
| `#NULL!` | Incorrect range reference |
| `#NUM!` | Invalid numeric value |
| `#CIRCULAR!` | Circular reference detected |

```
X xA1bC2dE cA1bC2dE rA1bC2dE e #DIV/0!
X xB3dE4fG cA1bC2dE rB3dE4fG e #REF!
```

### Date (d)

Dates are stored in ISO 8601 format (unquoted):

```
X xA1bC2dE cA1bC2dE rA1bC2dE d 2024-01-15
```

### DateTime (t)

DateTimes are stored in ISO 8601 format with time zone (unquoted):

```
X xA1bC2dE cA1bC2dE rA1bC2dE t 2024-01-15T10:30:00Z
```

## Identifiers

All entities use 8-character base62 identifiers:
- Character set: `0-9`, `A-Z`, `a-z` (62 characters)
- Combinations: 62^8 = **218 trillion** unique IDs
- Null ID: Represented as `~` (single tilde character)

**Examples:**
```
Kj7mXp2Q    # Valid ID
fR3pK7wN    # Valid ID
~           # Null ID
```

IDs are generated randomly and are immutable once assigned. They provide stable references that survive structural changes (insertions, deletions, reordering).

## Section Markers

Lines beginning with `#` are section markers or comments:

| Marker | Purpose |
|--------|---------|
| `#cells v1` | File format version header |
| `#cols` | Start of columns section |
| `#rows` | Start of rows section |
| `#cells` | Start of cells section |
| `#styles` | Start of styles section |
| `#cell-styles` | Start of cell-style mappings |
| `#oplog` | Start of operation log |

Any other line starting with `#` is treated as a comment and ignored.

## Operation Log

The operation log (`#oplog` section) stores CRDT operations for collaboration and undo/redo.

### Format

```
O <hlc> <op-type> <target-id> <payload>
```

### HLC (Hybrid Logical Clock)

Format: `<wall_time>.<logical>.<node_id>`

- `wall_time`: Unix timestamp in milliseconds
- `logical`: Logical counter for causality ordering
- `node_id`: 8-character ID of the originating node

**Example:** `1705312200000.0.N3f8hJ2w`

### Operation Types

| Type | Code | Description |
|------|------|-------------|
| Cell Operations | | |
| `CELL_SET_VALUE` | 0 | Set cell value |
| `CELL_CLEAR` | 1 | Clear cell contents |
| `CELL_SET_STYLE` | 2 | Set cell style |
| Dimension Operations | | |
| `DIM_INSERT_AXIS` | 10 | Insert column or row |
| `DIM_DELETE_AXIS` | 11 | Delete column or row |
| `DIM_MOVE_AXIS` | 12 | Move column or row |
| `DIM_RESIZE_AXIS` | 13 | Resize column/row |
| `DIM_RENAME_AXIS` | 14 | Rename column/row |
| Sheet Operations | | |
| `SHEET_CREATE` | 20 | Create sheet |
| `SHEET_DELETE` | 21 | Delete sheet |
| `SHEET_RENAME` | 22 | Rename sheet |

### Example

```
#oplog
O 1705312200000.0.N3f8hJ2w CELL_SET_VALUE nP6kR2mW {"type":"n","value":"42"}
O 1705312200001.0.N3f8hJ2w CELL_SET_VALUE hT8sL4xQ {"type":"s","value":"Hello"}
```

## Complete Example

A simple spreadsheet with numbers, text, and a formula:

```
#cells v1
D aB3cD4eF "Example"

S gH5jK6mN "Sheet1"

#cols
C cA1bC2dE 0
C cB3dE4fG 1

#rows
R rA1bC2dE 0
R rB3dE4fG 1
R rC5fG6hJ 2

#cells
X xA1aB2cD cA1bC2dE rA1bC2dE n 10
X xA2aB3cD cA1bC2dE rB3dE4fG n 20
X xA3aB4cD cA1bC2dE rC5fG6hJ f "=$cA1bC2dE$rA1bC2dE+$cA1bC2dE$rB3dE4fG"
X xB1bC2dE cB3dE4fG rA1bC2dE s "Values"
X xB2bC3dE cB3dE4fG rB3dE4fG s "below"
```

This represents:

|   | A  | B      |
|---|-------|--------|
| 1 | 10    | Values |
| 2 | 20    | below  |
| 3 | =A1+A2 |       |

## Git Merge Behavior

The format is designed for clean git merges:

### Single Cell Edit
```diff
-X xA1bC2dE cA1bC2dE rA1bC2dE n 10
+X xA1bC2dE cA1bC2dE rA1bC2dE n 42
```
Result: 1 line changed

### Add Cell
```diff
 X xA1bC2dE cA1bC2dE rA1bC2dE n 10
+X xA2bC3dE cA1bC2dE rB3dE4fG n 20
```
Result: 1 line added

### Concurrent Edits (Different Cells)
Alice edits A1, Bob edits B1 - Git auto-merges successfully.

### Concurrent Edits (Same Cell)
```
<<<<<<< HEAD
X xA1bC2dE cA1bC2dE rA1bC2dE n 100
=======
X xA1bC2dE cA1bC2dE rA1bC2dE n 200
>>>>>>> feature
```
Clear 1-line conflict that's easy to resolve.

## Migration from .cells

The `.zcd` format is identical to the previous `.cells` format in structure and content. Migration is simply a file rename:

```bash
mv document.cells document.zcd
```

No content changes are required.

## Implementation

| Component | File |
|-----------|------|
| Parser | `core/cells/parser.cc` |
| Serializer | `core/cells/serializer.cc` |
| Types | `core/cells/types.h` |
| Operations | `core/cells/operation.h` |
| HLC | `core/cells/hlc.h` |
| Test files | `testdata/*.zcd` |
