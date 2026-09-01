# Cells scripting API

Scripts run via the CLI:

```bash
cells -i input.xlsx output.xlsx --script my.luau
cells -i input.csv -e 'setCell("A1", 42)'
```

Language is Luau. The globals below are registered by the engine — do not invent names.

## Cell access

### `getCell(ref [, opts]) → Cell?`

- `ref`: A1 reference (`"A1"`, `"B12"`)
- `opts`: optional `{ create = true }` to create an empty cell if missing
- Returns a Cell object, or `nil` if missing and not created

```luau
local c = getCell("A1")
if c then
  print(c.value, c.formula, c.ref)
end

local d = getCell("Z99", { create = true })
d.value = 10
```

### `setCell(ref, value)`

Sets a cell. `value` may be number, string, boolean, or `nil` (clear).

- Strings starting with `=` are formulas: `setCell("B1", "=A1*2")`

```luau
setCell("A1", 100)
setCell("B1", "hello")
setCell("C1", "=A1+10")
setCell("D1", true)
```

### Cell object

| Field | Type | Notes |
|-------|------|--------|
| `value` | number \| string \| boolean \| nil | Read/write; writing updates the cell |
| `formula` | string? | Read-only; set formulas via `setCell` with `=` |
| `ref` | string | A1 reference |
| `dependents` | {Cell} | Cells that depend on this one |
| `dependencies` | {Cell} | Cells this formula depends on |

## Document

```luau
setDocumentTitle("Q1 Report")
local title = getDocumentTitle()
```

## Sheets

```luau
-- Get by name, 1-based index, or options table
local s = getSheet("Sheet1")
local s2 = getSheet(1)
local s3 = getSheet({ name = "Data" })
local s4 = getSheet({ index = 2 })

selectSheet("Sheet1")   -- name, index, or Sheet object
local news = addSheet("Summary")  -- optional name
moveSheet(0, 2)         -- 0-based fromIndex, insert-before toIndex
```

Sheet object: `name` (string).

## Columns and rows

```luau
setColumnWidth("A", { width = 120 })   -- pixels
setRowHeight(1, { height = 24 })       -- 1-based row, pixels
moveColumn("C", { to = 0 })            -- 0-based target position

hideColumn("B")
showColumn("B")
hideRow(3)
showRow(3)

setColumnStyle("A", { bold = true })
setRowStyle(1, { bgColor = "#EEEEEE" })
```

## Ranges

```luau
selectRange({ from = "A1", to = "C10" })   -- selection (UI-oriented)
deleteRange({ from = "A1", to = "B2" })    -- clear cells
fillRange({ from = "A1:A2", to = "A1:A10" })  -- fill pattern from source
```

## Format and style

```luau
-- Number format: legacy ID (e.g. FMT_C002), base64, or nil to clear
setFormat("A1:A10", "FMT_C002")
setFormat("B1", nil)

-- Style table (or nil to clear)
setStyle("A1:C1", {
  bold = true,
  italic = false,
  bgColor = "#FFFF00",
  textColor = COLOR_BLACK,
  hAlign = ALIGN_CENTER,
  vAlign = VALIGN_MIDDLE,
})

local formats = getFormats()  -- available format metadata
```

### Style / color constants

Horizontal: `ALIGN_LEFT`, `ALIGN_CENTER`, `ALIGN_RIGHT`, `ALIGN_JUSTIFY`  
Vertical: `VALIGN_TOP`, `VALIGN_MIDDLE`, `VALIGN_BOTTOM`  
Colors: `COLOR_RED`, `COLOR_GREEN`, `COLOR_BLUE`, `COLOR_YELLOW`, `COLOR_MAGENTA`, `COLOR_CYAN`, `COLOR_WHITE`, `COLOR_BLACK`, `COLOR_GRAY`, `COLOR_ORANGE`

## Freeze panes

```luau
freezePanes(1, 1)  -- freeze first column and first row (0 = none)
local fp = getFreezePanes()  -- { col = n, row = n }

-- Theme JSON (same shape as the UI theme picker)
setTheme('{"name":"Office","colorScheme":{"colors":["#FFFFFF"]},"fontScheme":{"majorFont":"Calibri Light","minorFont":"Calibri"}}')
```

## Output

```luau
print("value:", getCell("A1").value)
```

`print` writes to the CLI console (stdout/script log).

## CLI integration notes

- With `-i` and an output path, the script runs after load and before save.
- With `-i` and no output path, script-only mode: load, run, do not write a file.
- With only an output path (no `-i`), create an empty workbook, run any `-e`/`--script`, then save.
- Use `--eval` when exporting calculated formula results (e.g. to CSV).
