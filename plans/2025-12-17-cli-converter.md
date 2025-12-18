# CLI Converter Implementation Plan

**Status:** TODO
**Created:** 2025-12-17
**Goal:** Build a command-line tool for converting between spreadsheet formats (.cells, .csv, .xlsx).

## Overview

Inspired by ffmpeg's interface, the `cells` CLI will provide format conversion:

```bash
cells -i input.csv output.cells
cells -i data.cells report.xlsx
cells -i spreadsheet.xlsx data.csv
```

This serves multiple purposes:
1. **Testing our design** - Excel compatibility validates the data model
2. **Early adoption** - Users can try Cells with existing data
3. **Automation** - Script-friendly batch conversions
4. **Dogfooding** - We use the CLI to test parser/serializer

## Supported Formats

| Extension | Read | Write | Notes |
|-----------|------|-------|-------|
| `.cells` | Yes | Yes | Our native text format |
| `.csv` | Yes | Yes | Single sheet only |
| `.xlsx` | Yes | Yes | May lose some features |

### Feature Preservation Matrix

When converting between formats, some features may be lost:

| Feature | .cells | .csv | .xlsx |
|---------|--------|------|-------|
| Multiple sheets | Yes | No | Yes |
| Formulas | Yes | No (values only) | Yes |
| Cell styles | Yes | No | Yes |
| Column widths | Yes | No | Yes |
| UUID-based refs | Yes | No | No |
| Gap encoding | Yes | No | No |
| OpLog/history | Yes | No | No |
| Cell types | Yes | Limited | Yes |

When features are lost, the CLI will print warnings:

```
$ cells -i advanced.cells output.csv
Warning: CSV format doesn't support multiple sheets. Only "Sheet1" will be exported.
Warning: Formulas will be exported as computed values only.
Warning: Cell styles will not be preserved.
Converted: advanced.cells -> output.csv (1523 cells)
```

---

## CLI Interface

### Basic Usage

```bash
cells -i <input> <output>
```

The format is detected from file extension. Input and output formats can be overridden:

```bash
cells -i data.txt -f csv output.cells    # Force input format
cells -i data.cells -t xlsx output       # Force output format (no extension)
```

### Options

```
Usage: cells [options] -i <input> <output>

Input/Output:
  -i <file>           Input file (required)
  -f <format>         Force input format (cells, csv, xlsx)
  -t <format>         Force output format (cells, csv, xlsx)

CSV Options:
  --delimiter <char>  CSV delimiter (default: ,)
  --no-header         CSV has no header row
  --encoding <enc>    Character encoding (default: utf-8)

XLSX Options:
  --sheet <name>      Export only this sheet (for multi-sheet inputs)
  --all-sheets        Export all sheets (creates multiple files for CSV)

Output Options:
  -y                  Overwrite output without asking
  -q                  Quiet mode (no progress/warnings)
  -v                  Verbose output

Info:
  --version           Show version
  --help              Show this help
```

### Examples

```bash
# Basic conversions
cells -i budget.xlsx budget.cells
cells -i data.cells report.csv
cells -i legacy.csv modern.cells

# CSV with custom delimiter
cells -i data.tsv --delimiter '\t' output.cells

# Export specific sheet from Excel
cells -i workbook.xlsx --sheet "Q1 Report" q1.cells

# Export all sheets to separate CSVs
cells -i workbook.xlsx --all-sheets reports/
# Creates: reports/Sheet1.csv, reports/Sheet2.csv, ...

# Force formats when extensions are ambiguous
cells -i data.txt -f csv -t cells output.dat

# Quiet mode for scripts
cells -i input.xlsx output.cells -q -y
```

---

## Phase 1: CLI Framework

Set up the command-line parsing infrastructure.

- [ ] **1a:** Create `apps/cli/BUILD` with Bazel target
- [ ] **1b:** Create `apps/cli/main.cc` with argument parsing
- [ ] **1c:** Add option structs (InputOptions, OutputOptions, CSVOptions, etc.)
- [ ] **1d:** Implement format detection from file extension
- [ ] **1e:** Add `--help` and `--version` output

**Deliverables:**
- `apps/cli/main.cc` - entry point with arg parsing
- `apps/cli/options.h` - option structures
- CLI that parses arguments and prints help

---

## Phase 2: CSV Reader

Implement CSV import into our data model.

- [ ] **2a:** Create `core/cells/csv_reader.h` and `csv_reader.cc`
- [ ] **2b:** Implement RFC 4180 CSV parsing (quoted fields, escapes)
- [ ] **2c:** Handle different delimiters (comma, tab, semicolon)
- [ ] **2d:** Auto-detect numeric vs string values
- [ ] **2e:** Handle UTF-8 encoding (and BOM detection)
- [ ] **2f:** Create test files: `core/testdata/csv/simple.csv`, etc.
- [ ] **2g:** Add `csv_reader_test.cc`

**Deliverables:**
- `csv_reader.h` / `csv_reader.cc` - CSV to Workbook
- `csv_reader_test.cc` - unit tests
- Test CSV files in `core/testdata/csv/`

---

## Phase 3: CSV Writer

Implement CSV export from our data model.

- [ ] **3a:** Create `core/cells/csv_writer.h` and `csv_writer.cc`
- [ ] **3b:** Implement RFC 4180 CSV output (proper escaping)
- [ ] **3c:** Handle formula cells (output computed value)
- [ ] **3d:** Support custom delimiters
- [ ] **3e:** Add `csv_writer_test.cc`
- [ ] **3f:** Add roundtrip tests (csv -> cells -> csv)

**Deliverables:**
- `csv_writer.h` / `csv_writer.cc` - Workbook to CSV
- `csv_writer_test.cc` - unit tests

---

## Phase 4: XLSX Reader

Implement Excel import. Use a library for the heavy lifting.

### Library Choice

Options for reading XLSX:
1. **xlsxio** - lightweight, read-only, C
2. **libxlsxwriter** - write-only (not suitable)
3. **OpenXLSX** - C++17, read/write, header-only
4. **xlnt** - C++14, read/write, well-tested

**Recommendation:** OpenXLSX (C++17, matches our codebase, header-only is easy to integrate)

- [ ] **4a:** Add OpenXLSX as Bazel dependency (or xlnt)
- [ ] **4b:** Create `core/cells/xlsx_reader.h` and `xlsx_reader.cc`
- [ ] **4c:** Implement basic cell reading (numbers, strings)
- [ ] **4d:** Implement formula reading (as text)
- [ ] **4e:** Read cell styles (basic: bold, colors)
- [ ] **4f:** Read column widths and row heights
- [ ] **4g:** Handle multiple sheets
- [ ] **4h:** Create test files: `core/testdata/xlsx/simple.xlsx`, etc.
- [ ] **4i:** Add `xlsx_reader_test.cc`

**Deliverables:**
- `xlsx_reader.h` / `xlsx_reader.cc` - XLSX to Workbook
- `xlsx_reader_test.cc` - unit tests
- Test XLSX files in `core/testdata/xlsx/`

---

## Phase 5: XLSX Writer

Implement Excel export.

- [ ] **5a:** Create `core/cells/xlsx_writer.h` and `xlsx_writer.cc`
- [ ] **5b:** Implement basic cell writing (numbers, strings)
- [ ] **5c:** Implement formula writing (convert UUID refs to A1)
- [ ] **5d:** Write cell styles (basic)
- [ ] **5e:** Write column widths and row heights
- [ ] **5f:** Handle multiple sheets
- [ ] **5g:** Add warning system for feature loss
- [ ] **5h:** Add `xlsx_writer_test.cc`
- [ ] **5i:** Add roundtrip tests (xlsx -> cells -> xlsx)

**Deliverables:**
- `xlsx_writer.h` / `xlsx_writer.cc` - Workbook to XLSX
- `xlsx_writer_test.cc` - unit tests

---

## Phase 6: Formula Reference Conversion

Convert between UUID-based and A1 notation for Excel compatibility.

- [ ] **6a:** Create `core/cells/ref_converter.h` and `ref_converter.cc`
- [ ] **6b:** Implement UUID-to-A1 conversion (for export)
- [ ] **6c:** Implement A1-to-UUID conversion (for import)
- [ ] **6d:** Handle absolute vs relative references ($A$1 vs A1)
- [ ] **6e:** Handle range references (A1:C3)
- [ ] **6f:** Add `ref_converter_test.cc`

**Deliverables:**
- `ref_converter.h` / `ref_converter.cc` - reference conversion
- `ref_converter_test.cc` - unit tests

---

## Phase 7: Integration

Wire everything together in the CLI.

- [ ] **7a:** Implement conversion pipeline (read -> transform -> write)
- [ ] **7b:** Add progress reporting for large files
- [ ] **7c:** Add warning accumulation and reporting
- [ ] **7d:** Implement `-y` (overwrite) and `-q` (quiet) flags
- [ ] **7e:** Add error handling with helpful messages
- [ ] **7f:** Create end-to-end tests

**Deliverables:**
- Working `cells` CLI binary
- End-to-end conversion tests

---

## Phase 8: Polish

Final touches for release.

- [ ] **8a:** Add man page or `--help` documentation
- [ ] **8b:** Test on sample real-world files
- [ ] **8c:** Performance testing with large files (100K+ cells)
- [ ] **8d:** Memory profiling
- [ ] **8e:** Update GETTING_STARTED.md with CLI usage
- [ ] **8f:** Create example scripts in `examples/`

**Deliverables:**
- Complete, tested CLI tool
- Documentation and examples

---

## File Layout After Completion

```
cells/
├── apps/
│   └── cli/
│       ├── BUILD
│       ├── main.cc
│       └── options.h
├── core/
│   ├── cells/
│   │   ├── csv_reader.h
│   │   ├── csv_reader.cc
│   │   ├── csv_reader_test.cc
│   │   ├── csv_writer.h
│   │   ├── csv_writer.cc
│   │   ├── csv_writer_test.cc
│   │   ├── xlsx_reader.h
│   │   ├── xlsx_reader.cc
│   │   ├── xlsx_reader_test.cc
│   │   ├── xlsx_writer.h
│   │   ├── xlsx_writer.cc
│   │   ├── xlsx_writer_test.cc
│   │   ├── ref_converter.h
│   │   ├── ref_converter.cc
│   │   └── ref_converter_test.cc
│   └── testdata/
│       ├── csv/
│       │   ├── simple.csv
│       │   ├── quoted.csv
│       │   └── unicode.csv
│       └── xlsx/
│           ├── simple.xlsx
│           ├── formulas.xlsx
│           └── multi_sheet.xlsx
└── examples/
    └── convert_all.sh
```

---

## Technical Notes

### A1 Reference Conversion

UUID-based references in `.cells` files:
```
=$kR7pN2wQ$jH4sW8nF+10
```

Must be converted to A1 for Excel:
```
=$A$1+10
```

This requires:
1. Building a map: column_id -> column_index -> letter (A, B, ..., Z, AA, ...)
2. Building a map: row_id -> row_index -> number (1, 2, 3, ...)
3. Walking the formula string and replacing `$id$id` patterns

### Gap Handling for A1 Conversion

Gaps in our format represent empty columns/rows. When converting to A1:
- Column `kR7pN2wQ` (first) -> A
- Column `vT5mK9xL` (second, gap:2) -> D (skipping B, C)

The converter must walk the linked list to compute actual positions.

### Large File Streaming

For very large XLSX files (100K+ cells):
- Use streaming XML parser (SAX-style) rather than DOM
- Process cells in chunks
- Consider memory-mapped I/O for our format

### Excel Limitations

Be aware of Excel limits:
- Max rows: 1,048,576
- Max columns: 16,384 (XFD)
- Max characters per cell: 32,767

We should warn if our data exceeds these limits when exporting to XLSX.

---

## Dependencies

### Build Dependencies

- OpenXLSX (or xlnt) for XLSX support
- zlib (usually already available)

### Bazel Setup

```starlark
# In MODULE.bazel or WORKSPACE
http_archive(
    name = "openxlsx",
    urls = ["https://github.com/troldal/OpenXLSX/archive/refs/tags/v0.4.1.tar.gz"],
    strip_prefix = "OpenXLSX-0.4.1",
)
```

---

## Success Criteria

1. **Correctness:** Round-trip conversions preserve data
2. **Usability:** Clear error messages and warnings
3. **Performance:** Convert 100K cells in < 5 seconds
4. **Compatibility:** Successfully import/export real-world Excel files
