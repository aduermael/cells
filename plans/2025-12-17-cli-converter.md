# CLI Converter Implementation Plan

**Status:** IN-PROGRESS
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
| Cell types | Yes | Limited | Yes |
| Edit history | Yes | No | No |

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

- [x] **1a:** Create `apps/cli/BUILD` with Bazel target
- [x] **1b:** Create `apps/cli/main.cc` with argument parsing
- [x] **1c:** Add option structs (InputOptions, OutputOptions, CSVOptions, etc.)
- [x] **1d:** Implement format detection from file extension
- [x] **1e:** Add `--help` and `--version` output

**Deliverables:**
- `apps/cli/main.cc` - entry point with arg parsing
- `apps/cli/options.h` - option structures
- CLI that parses arguments and prints help

---

## Phase 2: File Info Command

Add an info mode to inspect .cells files before implementing conversion.

```bash
cells --info data.cells
# or
cells -I data.cells
```

Example output:
```
File: data.cells
Format: cells v1
Sheets: 2
  - Sheet1: 150 rows × 12 columns (1,423 cells)
  - Sheet2: 45 rows × 8 columns (289 cells)
Total cells: 1,712
Formulas: 23
```

- [x] **2a:** Add `--info` / `-I` flag to options
- [x] **2b:** Implement .cells file reading using existing parser
- [x] **2c:** Calculate and display file statistics (sheets, rows, columns, cells)
- [x] **2d:** Show formula count

**Deliverables:**
- `cells --info` command working for .cells files
- Validates parser integration before conversion work

---

## Phase 3: CSV Reader

Implement CSV import into our data model.

- [x] **3a:** Create `core/cells/csv_reader.h` and `csv_reader.cc`
- [x] **3b:** Implement RFC 4180 CSV parsing (quoted fields, escapes)
- [x] **3c:** Handle different delimiters (comma, tab, semicolon)
- [x] **3d:** Auto-detect numeric vs string values
- [x] **3e:** Handle UTF-8 encoding (and BOM detection)
- [x] **3f:** Create test files: `core/testdata/csv/simple.csv`, etc.
- [x] **3g:** Add `csv_reader_test.cc`
- [x] **3h:** Wire up CSV support to CLI `--info` command

**Deliverables:**
- `csv_reader.h` / `csv_reader.cc` - CSV to Workbook
- `csv_reader_test.cc` - unit tests (32 tests)
- Test CSV files in `core/testdata/csv/` (8 files)
- CLI `--info` command works with `.csv` and `.tsv` files

---

## Phase 4: CSV Writer

Implement CSV export from our data model.

- [ ] **4a:** Create `core/cells/csv_writer.h` and `csv_writer.cc`
- [ ] **4b:** Implement RFC 4180 CSV output (proper escaping)
- [ ] **4c:** Handle formula cells (output computed value)
- [ ] **4d:** Support custom delimiters
- [ ] **4e:** Add `csv_writer_test.cc`
- [ ] **4f:** Add roundtrip tests (csv -> cells -> csv)

**Deliverables:**
- `csv_writer.h` / `csv_writer.cc` - Workbook to CSV
- `csv_writer_test.cc` - unit tests

---

## Phase 5: CSV ↔ CELLS Integration

Wire up the CLI with CSV and .cells support before adding XLSX complexity.

- [ ] **5a:** Implement conversion pipeline (read -> transform -> write)
- [ ] **5b:** Wire CSV reader/writer into CLI
- [ ] **5c:** Add progress reporting
- [ ] **5d:** Add warning accumulation and reporting
- [ ] **5e:** Implement `-y` (overwrite) and `-q` (quiet) flags
- [ ] **5f:** Add error handling with helpful messages
- [ ] **5g:** Create end-to-end tests (csv → cells, cells → csv, roundtrips)

**Deliverables:**
- Working `cells` CLI binary (CSV + .cells only)
- End-to-end conversion tests
- Usable tool for basic spreadsheet conversion

---

## Phase 6: XLSX Reader

Implement Excel import. Use a library for the heavy lifting.

### Library Choice

Options for reading XLSX:
1. **xlsxio** - lightweight, read-only, C
2. **libxlsxwriter** - write-only (not suitable)
3. **OpenXLSX** - C++17, read/write, header-only
4. **xlnt** - C++14, read/write, well-tested

**Recommendation:** OpenXLSX (C++17, matches our codebase, header-only is easy to integrate)

- [ ] **6a:** Add OpenXLSX as Bazel dependency (or xlnt)
- [ ] **6b:** Create `core/cells/xlsx_reader.h` and `xlsx_reader.cc`
- [ ] **6c:** Implement basic cell reading (numbers, strings)
- [ ] **6d:** Implement formula reading (as text)
- [ ] **6e:** Read cell styles (basic: bold, colors)
- [ ] **6f:** Read column widths and row heights
- [ ] **6g:** Handle multiple sheets
- [ ] **6h:** Create test files: `core/testdata/xlsx/simple.xlsx`, etc.
- [ ] **6i:** Add `xlsx_reader_test.cc`

**Deliverables:**
- `xlsx_reader.h` / `xlsx_reader.cc` - XLSX to Workbook
- `xlsx_reader_test.cc` - unit tests
- Test XLSX files in `core/testdata/xlsx/`

---

## Phase 7: XLSX Writer

Implement Excel export.

- [ ] **7a:** Create `core/cells/xlsx_writer.h` and `xlsx_writer.cc`
- [ ] **7b:** Implement basic cell writing (numbers, strings)
- [ ] **7c:** Implement formula writing (convert UUID refs to A1)
- [ ] **7d:** Write cell styles (basic)
- [ ] **7e:** Write column widths and row heights
- [ ] **7f:** Handle multiple sheets
- [ ] **7g:** Add warning system for feature loss
- [ ] **7h:** Add `xlsx_writer_test.cc`
- [ ] **7i:** Add roundtrip tests (xlsx -> cells -> xlsx)

**Deliverables:**
- `xlsx_writer.h` / `xlsx_writer.cc` - Workbook to XLSX
- `xlsx_writer_test.cc` - unit tests

---

## Phase 8: Formula Reference Conversion

Convert between UUID-based and A1 notation for Excel compatibility.

- [ ] **8a:** Create `core/cells/ref_converter.h` and `ref_converter.cc`
- [ ] **8b:** Implement UUID-to-A1 conversion (for export)
- [ ] **8c:** Implement A1-to-UUID conversion (for import)
- [ ] **8d:** Handle absolute vs relative references ($A$1 vs A1)
- [ ] **8e:** Handle range references (A1:C3)
- [ ] **8f:** Add `ref_converter_test.cc`

**Deliverables:**
- `ref_converter.h` / `ref_converter.cc` - reference conversion
- `ref_converter_test.cc` - unit tests

---

## Phase 9: XLSX Integration

Wire XLSX support into the CLI.

- [ ] **9a:** Add XLSX reader/writer to CLI conversion pipeline
- [ ] **9b:** Integrate formula reference conversion
- [ ] **9c:** Add XLSX-specific CLI options (--sheet, --all-sheets)
- [ ] **9d:** Create end-to-end tests (xlsx → cells, cells → xlsx, csv ↔ xlsx)

**Deliverables:**
- Full `cells` CLI with XLSX support
- Complete format conversion matrix tested

---

## Phase 10: Polish

Final touches for release.

- [ ] **10a:** Add man page or `--help` documentation
- [ ] **10b:** Test on sample real-world files
- [ ] **10c:** Performance testing with large files (100K+ cells)
- [ ] **10d:** Memory profiling
- [ ] **10e:** Update GETTING_STARTED.md with CLI usage
- [ ] **10f:** Create example scripts in `examples/`

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
