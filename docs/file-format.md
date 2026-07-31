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

Files may optionally begin with a version header comment:
```
#zcd v1
```

This is recommended for human readability but not required by the parser (any line starting with `#` is treated as a comment).

## Document Structure

A ZCD file contains the following sections in order:

```
D <doc-id> "<name>"                   # Document declaration (required)
T "<name>" colors:<hex...> fonts:"<major>","<minor>"  # Workbook theme (optional)
N "<name>" <scope> <scope-sheet-id> <target-type> <target-data>  # Named ranges (optional)

S <sheet-id> "<name>"                 # Sheet declaration
V <key:value...>                      # Sheet view properties (optional)
C <id> <position> [props...]          # Column definitions
R <id> <position> [props...]          # Row definitions
X <id> <col> <row> <type> <value> [props...]  # Cell definitions
RG <id> <start_col> <start_row> <end_col> <end_row> <flags> [fmt:<base64>] [sty:<base64>]  # Range definitions

#oplog                                # Operation log section marker
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

### Theme (T)

Optional line defining the workbook theme. Appears after the document declaration and before named ranges.

**Format:** `T "<name>" colors:<12 hex values> fonts:"<majorFont>","<minorFont>"`

**Fields:**
- `name`: Theme name (quoted string, e.g. "Office Theme")
- `colors`: 12 comma-separated 6-character hex color values (no `#` prefix), in index order: lt1, dk1, lt2, dk2, accent1-6, hlink, folHlink
- `fonts`: Two quoted strings for major (headings) and minor (body) font names

**Example:**
```
T "Office Theme" colors:FFFFFF,000000,E7E6E6,44546A,4472C4,ED7D31,A5A5A5,FFC000,5B9BD5,70AD47,0563C1,954F72 fonts:"Calibri Light","Calibri"
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
- `sty:<base64>`: Default style for cells in this column (content-addressed)
- `fmt:<base64>`: Default number format for cells in this column (content-addressed)

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
- `sty:<base64>`: Default style for cells in this row (content-addressed)
- `fmt:<base64>`: Default number format for cells in this row (content-addressed)

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
- `fmt:<base64>`: Number format for this cell (content-addressed)
- `sty:<base64>`: Cell style (content-addressed)

See [Cell Types](#cell-types) for details.

## Content-Addressed Number Formats

Number formats use a **content-addressed** binary encoding, similar to styles. The format's content IS its identity.

### Format Property Encoding

Formats are encoded as binary data using a flag-based system:

```
+--------+--------+--------+...
| Flags  | Props  | Props  |...
+--------+--------+--------+
```

**Flag Byte:**
- Bit 0: category present (otherwise GENERAL)
- Bit 1: decimals present (otherwise 0)
- Bit 2: thousands separator flag (if set, use separator)
- Bit 3: currency symbol present
- Bit 4: custom format code present (raw Excel-style string)
- Bits 5-7: reserved

**Property Data** follows in flag order:
- Category: 1 byte (0=GENERAL, 1=NUMBER, 2=CURRENCY, 3=ACCOUNTING, 4=PERCENTAGE, 5=DATE, 6=TIME, 7=DATE_TIME, 8=SCIENTIFIC, 9=FRACTION, 10=TEXT)
- Decimals: 1 byte (0-15)
- Currency symbol: 1 byte length + UTF-8 string
- Custom format code: 2 bytes length + UTF-8 string

### Format Examples

```
# Percentage with 2 decimals
fmt:BAQC

# USD Currency with 2 decimals and thousands separator
fmt:DwIC+AEk

# Custom format "# BANANA"
fmt:ERoBCCMgQkFOQU5B
```

### Range (RG)

Defines a range spanning multiple cells. Ranges can serve multiple purposes based on their flags.

**Format:** `RG <id> <start_col> <start_row> <end_col> <end_row> <flags> [fmt:<base64>] [sty:<base64>]`

**Fields:**
- `id`: 8-character base62 range identifier
- `start_col`, `start_row`: Column/row IDs of top-left corner
- `end_col`, `end_row`: Column/row IDs of bottom-right corner
- `flags`: Range flags bitmap (see below)
- `fmt:<base64>`: Optional content-addressed number format (see [Content-Addressed Number Formats](#content-addressed-number-formats))
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
| 128 | FORMAT | Has number format (content-addressed FormatBuffer) |

Flags can be combined (e.g., `3` = MERGE + STYLE, `130` = STYLE + FORMAT).

**Examples:**
```
RG rGfH3jK2 cA1bC2dE rA1bC2dE cB3dE4fG rB3dE4fG 2 sty:BECA+78k
RG rMerge01 cC5fG6hJ rC5fG6hJ cD7hJ8kL rE9kL0mN 1
RG rFmt001 cA1bC2dE rA1bC2dE cB3dE4fG rC5fG6hJ 130 fmt:BAQC sty:BEAB
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

Lines beginning with `#` are comments. Some common conventions:

| Marker | Purpose |
|--------|---------|
| `#zcd v1` | Format version (optional, recommended for readability) |
| `#oplog` | Start of operation log section |
| `#peers` | Start of durable peer knowledge section |
| `#cols`, `#rows`, `#cells` | Section dividers (optional, for human readability) |

The parser recognizes lines by their prefix character (`D`, `N`, `S`, `V`, `C`, `R`, `X`, `RG`, `O`, `P`), so section markers are purely for human readability and not required.

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

Operations use a unified SET/DELETE pattern. Each SET operation creates or updates an entity with all its properties in a single operation.

| Type | Code | Description |
|------|------|-------------|
| **Cell Operations** | | |
| `CELL_SET` | 0 | Create/update cell (col, row, type, value, style, format) |
| `CELL_DELETE` | 1 | Delete/clear cell |
| **Column Operations** | | |
| `COL_SET` | 10 | Create/update column (position, size, name, style, format, hidden) |
| `COL_DELETE` | 11 | Delete column |
| **Row Operations** | | |
| `ROW_SET` | 20 | Create/update row (position, size, style, format, hidden) |
| `ROW_DELETE` | 21 | Delete row |
| **Sheet Operations** | | |
| `SHEET_SET` | 30 | Create/update sheet (name, position) |
| `SHEET_DELETE` | 31 | Delete sheet |
| **Workbook Operations** | | |
| `WORKBOOK_SET` | 40 | Update workbook properties (name) |
| **Named Range Operations** | | |
| `NAMED_RANGE_SET` | 50 | Create/update named range |
| `NAMED_RANGE_DELETE` | 51 | Delete named range |
| **Range Operations** | | |
| `RANGE_SET` | 60 | Create/update range (corners, flags, style, format) |
| `RANGE_DELETE` | 61 | Delete range

### Example

```
#oplog
O 1705312200000.0.N3f8hJ2w CELL_SET nP6kR2mW {"col":"kR7pN2wQ","row":"jH4sW8nF","t":"n","v":"42"}
O 1705312200001.0.N3f8hJ2w CELL_SET hT8sL4xQ {"col":"kR7pN2wQ","row":"qM2kL5pR","t":"s","v":"Hello"}
O 1705312200002.0.N3f8hJ2w CELL_SET xA1bC2dE {"col":"cA1bC2dE","row":"rA1bC2dE","t":"n","v":"10","sty":"BEAB"}
O 1705312200003.0.N3f8hJ2w COL_SET cA1bC2dE {"pos":0,"size":120,"fmt":"BAQC"}
O 1705312200004.0.N3f8hJ2w RANGE_SET rGfH3jK2 {"sc":"cA1","sr":"rA1","ec":"cB3","er":"rB3","flags":2,"sty":"BECA+78k"}
O 1705312200005.0.N3f8hJ2w CELL_DELETE nP6kR2mW {}
```

Operations use unified SET commands that include all entity properties. Style (`sty`) and format (`fmt`) use content-addressed base64-encoded binary data.

## Peer Knowledge

Durable peer frontiers (`#peers` section) record what each known peer has received so offline work and later rejoin can reconcile cleanly. Live connection state is separate; disconnect must not erase this map.

### Format

```
P <peer_id> <hlc>
```

- `peer_id`: 8-character peer/node ID
- `hlc`: Highest HLC known to have been applied/received by that peer

**Example:**
```
#peers
P AbCdEf12 1705312200000.0.N3f8hJ2w
P GhIjKl34 1705312200999.3.XyZaBcDe
```

Legacy files without `P` lines load with empty peer knowledge.

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
