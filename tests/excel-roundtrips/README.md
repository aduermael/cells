# Excel Test Sets

Test data and evaluation tools for Excel file processing.

## Structure

```
excel-test-sets/
├── data/           # Test Excel files and expected outputs
│   ├── math-basic/ # Basic math operations
│   └── math-trig/  # Trigonometric functions
└── evaluator/      # C# tool to compare/extract Excel cell properties
```

## Evaluator

A containerized C# tool using Open XML SDK to compare Excel files or extract cell properties to JSON format.

### Build

```bash
cd evaluator
docker build -t excel-evaluator .
```

### Compare Mode (Primary Use)

Compare two Excel files for cell-level equality:

```bash
./compare.sh data/math-basic/init.xlsx data/math-basic/values.xlsx
```

**Output on match (exit 0):**
```
MATCH: Files are identical
MD5: a1b2c3d4e5f6...
Cells: 42
```

**Output on mismatch (exit 1):**
```
MISMATCH: Files differ
MD5 file1: a1b2c3d4...
MD5 file2: e5f6g7h8...
Cells in file1: 42
Cells in file2: 43

First difference: Cell Sheet1!B5 differs:
  file1: {"address":"B5","sheet":"Sheet1","type":"number","value":"10"}
  file2: {"address":"B5","sheet":"Sheet1","type":"number","value":"20"}
```

### Extract Mode

Extract cell properties to a JSON-per-line file:

```bash
docker run --rm \
  -v $(pwd)/data/math-basic/init.xlsx:/data/input.xlsx:ro \
  -v $(pwd)/data/math-basic:/output \
  excel-evaluator --extract /data/input.xlsx /output/init.txt
```

### Output Format

Each line is a JSON object representing one cell with non-default properties:

```json
{"address":"A1","sheet":"Sheet1","value":"Hello","type":"string","font":{"bold":true},"fill":{"fgColor":"#FFFF00"}}
```

Properties included (when non-default):
- `address`: Cell reference (e.g., "A1")
- `sheet`: Sheet name
- `value`: Cell value (string, number, boolean, or error)
- `type`: Value type (string, number, boolean, error, date, sharedString)
- `formula`: Cell formula if present
- `font`: Font properties (name, size, bold, italic, underline, color, etc.)
- `fill`: Fill properties (pattern, fgColor, bgColor)
- `border`: Border properties (left, right, top, bottom, diagonal)
- `alignment`: Alignment properties (horizontal, vertical, wrapText, textRotation)
- `numberFormat`: Number format code
- `protection`: Protection properties (locked, hidden)

### Determinism

Output is fully deterministic for hash comparison:
- Cells sorted by: sheet index (workbook order) → row number → column number
- JSON keys sorted alphabetically within each object
- Empty cells and default property values omitted
- Same input file always produces byte-identical output

### Read-Only Mounts

All input files are mounted read-only (`:ro`) in the container. The tool cannot modify source Excel files.
