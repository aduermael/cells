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
| `.zcdz` | Text + zstd | Compressed but still git-diffable |
| `.zcdb` | Binary | Large files, fast random access |
| `.zcdbz` | Binary + zstd | Maximum compression |

## Format Version

Current version: **v1**

Files begin with a version header:
```
#zcd v1
```

## Document Structure

A ZCD file contains the following sections in order:

```
#zcd v1                               # Version header
D <doc-id> "<name>"                   # Document declaration
F <format-id> "<format-code>"         # Custom number format definitions (optional)
N "<name>" <scope> <scope-sheet-id> <target-type> <target-data>  # Named ranges (optional)

S <sheet-id> "<name>"                 # Sheet declaration
V <key:value...>                      # Sheet view properties (optional)
C <id> <position> [props...]          # Column definitions
R <id> <position> [props...]          # Row definitions
X <id> <col> <row> <type> <value> [props...]  # Cell definitions
RG <id> <start_col> <start_row> <end_col> <end_row> <flags> [sty:<base64>]  # Range definitions

#oplog                                # Operation log section
O <hlc> <op-type> <target-id> <payload>
```

Multiple sheets repeat the `S`, `V`, `C`, `R`, `X`, `RG` pattern.

## Entity Types

### Document (D)

Declares the workbook with a unique ID and name.

**Format:** `D <id> "<name>"`

**Example:**
```
D tY8pL3mK "Budget 2024"
```

### Custom Format (F)

Defines a custom number format that can be referenced by cells and axes.

**Format:** `F <id> "<format-code>"`

**Fields:**
- `id`: 8-character base62 identifier
- `format-code`: Excel-compatible number format string

**Examples:**
```
F fMt12345 "#,##0.00"
F fMtCurr1 "$#,##0.00;($#,##0.00)"
F fMtPct01 "0.00%"
```

### Named Range (N)

Defines a named range for use in formulas. Named ranges can be workbook-scoped (accessible from all sheets) or sheet-scoped (accessible only within a specific sheet).

**Format:** `N "<name>" <scope> <scope-sheet-id> <target-type> <target-data>`

**Fields:**
- `name`: The name to use in formulas (quoted string)
- `scope`: `W` for workbook scope, `S` for sheet scope
- `scope-sheet-id`: Sheet ID for sheet-scoped names, `-` for workbook-scoped
- `target-type`: One of `CELL`, `RANGE`, `COLUMN`, `ROW`, `COLUMN_RANGE`, `ROW_RANGE`
- `target-data`: IDs based on target type, plus target sheet ID

**Target data format by type:**
- `CELL`: `<cell-id> <sheet-id>`
- `RANGE`: `<id1> <id2> <sheet-id>`
- `COLUMN` / `ROW`: `<axis-id> <sheet-id>`
- `COLUMN_RANGE` / `ROW_RANGE`: `<id1> <id2> <sheet-id>`

**Examples:**
```
N "TotalSales" W - CELL xA1bC2dE sH1jK2mN
N "DataRange" W - RANGE cA1bC2dE cB3dE4fG sH1jK2mN
N "LocalValue" S sH1jK2mN CELL xC5fG6hJ sH1jK2mN
```

### Sheet (S)

Declares a worksheet within the workbook.

**Format:** `S <id> "<name>"`

**Example:**
```
S qR5sW2xN "Summary"
```

### Sheet View (V)

Optional line defining sheet view properties. Appears after the sheet declaration and before columns.

**Format:** `V <key:value...>`

**Properties:**
- `showGridLines:0|1`: Show or hide grid lines (default: 1)
- `zoomScale:<10-400>`: Zoom percentage (default: 100)
- `freezeCol:<n>`: Number of frozen columns (default: 0)
- `freezeRow:<n>`: Number of frozen rows (default: 0)

Only non-default values are serialized.

**Examples:**
```
V showGridLines:0 zoomScale:125
V freezeCol:1 freezeRow:2
```

### Column (C)

Defines a column with optional properties.

**Format:** `C <id> <position> [props...]`

**Fields:**
- `id`: 8-character base62 identifier
- `position`: 0-indexed column position

**Optional Properties:**
- `w:<width>`: Width in pixels (default: 100)
- `name:"<name>"`: Column name
- `hidden:1`: Column is hidden
- `sty:<base64>`: Default style for cells in this column
- `fmt:<format-id>`: Default number format for cells in this column

**Examples:**
```
C cA1bC2dE 0                    # Column at position 0, default width
C cB3dE4fG 1 w:150             # Column at position 1, 150px wide
C cC5fG6hJ 2 name:"Total"      # Column at position 2 with name
C cD7hJ8kL 3 w:200 hidden:1    # Hidden column, 200px wide
```

### Row (R)

Defines a row with optional properties.

**Format:** `R <id> <position> [props...]`

**Fields:**
- `id`: 8-character base62 identifier
- `position`: 0-indexed row position

**Optional Properties:**
- `h:<height>`: Height in pixels (default: 24)
- `name:"<name>"`: Row name
- `hidden:1`: Row is hidden
- `sty:<base64>`: Default style for cells in this row
- `fmt:<format-id>`: Default number format for cells in this row

**Examples:**
```
R rA1bC2dE 0                    # Row at position 0, default height
R rB3dE4fG 1 h:30              # Row at position 1, 30px tall
R rC5fG6hJ 2 name:"Header"     # Row at position 2 with name
R rD7hJ8kL 3 hidden:1          # Hidden row
```

### Cell (X)

Defines a cell value at a specific column/row intersection.

**Format:** `X <id> <col-id> <row-id> <type> <value> [props...]`

**Fields:**
- `id`: 8-character base62 cell identifier
- `col-id`: ID of the column containing this cell
- `row-id`: ID of the row containing this cell
- `type`: Single character indicating value type
- `value`: Type-specific value representation

**Optional Properties:**
- `fmt:<format-id>`: Number format for this cell
- `sty:<base64>`: Cell style (content-addressed)

See [Cell Types](#cell-types) for details.

### Range (RG)

Defines a range spanning multiple cells. Ranges can serve multiple purposes based on their flags.

**Format:** `RG <id> <start_col> <start_row> <end_col> <end_row> <flags> [sty:<base64>]`

**Fields:**
- `id`: 8-character base62 range identifier
- `start_col`, `start_row`: Column/row IDs of top-left corner
- `end_col`, `end_row`: Column/row IDs of bottom-right corner
- `flags`: Range flags bitmap (see below)
- `sty:<base64>`: Optional content-addressed style (see [Content-Addressed Styles](#content-addressed-styles))

**Range Flags:**

| Value | Name | Description |
|-------|------|-------------|
| 1 | MERGE | Cells are merged (anchor at top-left) |
| 2 | STYLE | Has style metadata (background, borders, etc.) |
| 4 | CONDITIONAL_FORMAT | Has conditional format rules |
| 8 | DATA_VALIDATION | Has data validation rules |
| 16 | NAMED | Is a named range |
| 32 | PRINT_AREA | Defines print area |
| 64 | FILTER | Has auto-filter |

Flags can be combined (e.g., `3` = MERGE + STYLE).

**Examples:**
```
RG rGfH3jK2 cA1bC2dE rA1bC2dE cB3dE4fG rB3dE4fG 2 sty:BECA+78k
RG rMerge01 cC5fG6hJ rC5fG6hJ cD7hJ8kL rE9kL0mN 1
```

## Content-Addressed Styles

Styles use a **content-addressed** binary encoding where the style's content IS its identity. This eliminates sync issues with style IDs and enables efficient storage.

### Style Property Encoding

Styles are encoded as binary data using a flag-based system:

```
+--------+--------+--------+...
| Flag 0 | Flag 1 | Props  |
+--------+--------+--------+...
```

**Flag Byte 0:**
- Bits 0-1: Flag byte count (always 0b00 = 2 bytes currently)
- Bit 2: bold present
- Bit 3: italic present
- Bit 4: underline present
- Bit 5: strikethrough present
- Bit 6: bgColor present
- Bit 7: textColor present

**Flag Byte 1:**
- Bit 0: fontSize present
- Bit 1: fontFamily present
- Bit 2: horizontalAlign present
- Bit 3: verticalAlign present
- Bit 4: textWrap present
- Bit 5: numberFormat present
- Bit 6: border present
- Bit 7: reserved

**Property Data** follows flags in order:
- Booleans: 1 byte packed (bold, italic, underline, strikethrough, textWrap)
- Colors: 3 bytes RGB each
- Font size: 1 byte (value - 6, supports 6-261pt)
- Font family: 1 byte length + UTF-8 string
- Alignment: 1 byte (3 bits hAlign, 3 bits vAlign)
- Number format: 8 bytes format ID
- Border: 1 byte sides mask + 4 bytes per side (style + RGB)

### Style Examples

```
# Bold with yellow background
sty:BECx+78k

# Font size 14, center aligned
sty:AQUBAQ==

# All four borders with different colors
sty:QEAPAAAAAAABwP8AAp//AADP/wA=
```

The base64-encoded style is deterministic: same properties always produce identical bytes, enabling natural deduplication.

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
| `#N/A` | Value not available (e.g., lookup not found) |
| `#CIRCULAR!` | Circular reference detected |
| `#SPILL!` | Array formula blocked by existing data |
| `#CALC!` | Calculation error (e.g., FILTER with no results) |

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
| `#zcd v1` | File format version header (required, first line) |
| `#oplog` | Start of operation log section |

Any other line starting with `#` is treated as a comment and ignored. The parser recognizes lines by their prefix character (`D`, `F`, `N`, `S`, `V`, `C`, `R`, `X`, `RG`, `O`), so section markers for columns, rows, cells, and ranges are not required.

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
| **Cell Operations** | | |
| `CELL_SET_VALUE` | 0 | Set cell value |
| `CELL_CLEAR` | 1 | Clear cell contents |
| `CELL_SET_STYLE` | 2 | Set cell style (content-addressed) |
| `CELL_SET_FORMAT` | 3 | Set cell number format |
| **Column Operations** | | |
| `COL_INSERT` | 10 | Insert new column |
| `COL_DELETE` | 11 | Delete column |
| `COL_MOVE` | 12 | Move column to new position |
| `COL_RESIZE` | 13 | Resize column width |
| `COL_RENAME` | 14 | Rename column |
| **Row Operations** | | |
| `ROW_INSERT` | 15 | Insert new row |
| `ROW_DELETE` | 16 | Delete row |
| `ROW_MOVE` | 17 | Move row to new position |
| `ROW_RESIZE` | 18 | Resize row height |
| **Axis Operations** | | |
| `AXIS_SET_HIDDEN` | 19 | Set axis hidden state |
| `AXIS_SET_STYLE` | 52 | Set axis default style (content-addressed) |
| `AXIS_SET_FORMAT` | 53 | Set axis default format |
| **Sheet Operations** | | |
| `SHEET_CREATE` | 20 | Create new sheet |
| `SHEET_DELETE` | 21 | Delete sheet |
| `SHEET_RENAME` | 22 | Rename sheet |
| **Workbook Operations** | | |
| `WORKBOOK_RENAME` | 30 | Rename workbook |
| **Format Operations** | | |
| `FORMAT_DEFINE` | 40 | Define a custom number format |
| **Named Range Operations** | | |
| `NAMED_RANGE_DEFINE` | 50 | Define a named range |
| `NAMED_RANGE_DELETE` | 51 | Delete a named range |
| **Range Operations** | | |
| `RANGE_ADD` | 60 | Add a new range |
| `RANGE_REMOVE` | 61 | Remove a range by ID |
| `RANGE_UPDATE_CORNERS` | 62 | Update range corners (resize) |
| `RANGE_UPDATE_FLAGS` | 63 | Update range flags bitmask |
| `RANGE_SET_STYLE` | 64 | Set range style (content-addressed) |

### Example

```
#oplog
O 1705312200000.0.N3f8hJ2w CELL_SET_VALUE nP6kR2mW {"type":"n","value":"42"}
O 1705312200001.0.N3f8hJ2w CELL_SET_VALUE hT8sL4xQ {"type":"s","value":"Hello"}
O 1705312200002.0.N3f8hJ2w CELL_SET_STYLE xA1bC2dE {"style":"BEAB"}
O 1705312200003.0.N3f8hJ2w RANGE_SET_STYLE rGfH3jK2 {"style":"BECA+78k"}
```

Style operations use the content-addressed `"style"` field containing base64-encoded binary style data.

## Complete Example

A simple spreadsheet with numbers, text, a formula, and styles:

```
#zcd v1
D aB3cD4eF "Example"

S gH5jK6mN "Sheet1"
C cA1bC2dE 0
C cB3dE4fG 1 w:120 sty:BEAB
R rA1bC2dE 0 sty:BECx+78k
R rB3dE4fG 1
R rC5fG6hJ 2
X xA1aB2cD cA1bC2dE rA1bC2dE n 10 sty:BEAB
X xA2aB3cD cA1bC2dE rB3dE4fG n 20
X xA3aB4cD cA1bC2dE rC5fG6hJ f "=$cA1bC2dE$rA1bC2dE+$cA1bC2dE$rB3dE4fG"
X xB1bC2dE cB3dE4fG rA1bC2dE s "Values"
X xB2bC3dE cB3dE4fG rB3dE4fG s "below"
RG rGaB1cD2 cA1bC2dE rA1bC2dE cB3dE4fG rB3dE4fG 2 sty:BECA+78k
```

This represents:

|   | A  | B      |
|---|-------|--------|
| 1 | 10 (bold)   | Values |
| 2 | 20    | below  |
| 3 | =A1+A2 |       |

With:
- Row 1 has a yellow background (sty:BECx+78k)
- Cell A1 is bold (sty:BEAB)
- Column B is 120px wide and bold
- Range A1:B2 has a yellow background

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
