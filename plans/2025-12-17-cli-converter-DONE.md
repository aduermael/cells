# CLI Converter Implementation Plan

**Status:** DONE
**Created:** 2025-12-17
**Goal:** Build a command-line tool for converting between spreadsheet formats (.cells, .csv, .xlsx).

> **Note (2025-12-21):** The `.cells` extension was renamed to `.zcd` (Zero-Conflict Document).
> All references to `.cells` in this plan now refer to `.zcd` files.

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

- [x] **4a:** Create `core/cells/csv_writer.h` and `csv_writer.cc`
- [x] **4b:** Implement RFC 4180 CSV output (proper escaping)
- [x] **4c:** Handle formula cells (output computed value)
- [x] **4d:** Support custom delimiters
- [x] **4e:** Add `csv_writer_test.cc`
- [x] **4f:** Add roundtrip tests (csv -> cells -> csv)

**Deliverables:**
- `csv_writer.h` / `csv_writer.cc` - Workbook to CSV (28 tests passing)
- `csv_writer_test.cc` - unit tests including 7 roundtrip tests

---

## Phase 5: CSV ↔ CELLS Integration

Wire up the CLI with CSV and .cells support before adding XLSX complexity.

- [x] **5a:** Implement conversion pipeline (read -> transform -> write)
- [x] **5b:** Wire CSV reader/writer into CLI
- [x] **5c:** Add progress reporting
- [x] **5d:** Add warning accumulation and reporting
- [x] **5e:** Implement `-y` (overwrite) and `-q` (quiet) flags
- [x] **5f:** Add error handling with helpful messages
- [x] **5g:** Create end-to-end tests (csv → cells, cells → csv, roundtrips)

**Deliverables:**
- Working `cells` CLI binary (CSV + .cells only)
- `converter.h` / `converter.cc` - conversion pipeline using Workbook as intermediary
- `converter_test.cc` - 15 end-to-end tests covering all scenarios
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

- [x] **6a:** Add OpenXLSX as Bazel dependency (or xlnt)
- [x] **6b:** Create `core/cells/xlsx_reader.h` and `xlsx_reader.cc`
- [x] **6c:** Implement basic cell reading (numbers, strings)
- [x] **6d:** Implement formula reading (as text)
- [ ] **6e:** Read cell styles (basic: bold, colors) *(deferred - not critical for MVP)*
- [x] **6f:** Read column widths and row heights
- [x] **6g:** Handle multiple sheets
- [x] **6h:** Create test files: `core/testdata/xlsx/simple.xlsx`, etc.
- [x] **6i:** Add `xlsx_reader_test.cc`

**Deliverables:**
- `xlsx_reader.h` / `xlsx_reader.cc` - XLSX to Workbook (20 tests passing)
- `xlsx_reader_test.cc` - unit tests
- Test XLSX files in `core/testdata/xlsx/` (6 files: simple, formulas, multi_sheet, types, empty, unicode)

---

## Phase 7: XLSX Writer

Implement Excel export.

- [x] **7a:** Create `core/cells/xlsx_writer.h` and `xlsx_writer.cc`
- [x] **7b:** Implement basic cell writing (numbers, strings)
- [x] **7c:** Implement formula writing (convert UUID refs to A1)
- [ ] **7d:** Write cell styles (basic) *(deferred - not critical for MVP)*
- [x] **7e:** Write column widths and row heights
- [x] **7f:** Handle multiple sheets
- [x] **7g:** Add warning system for feature loss
- [x] **7h:** Add `xlsx_writer_test.cc`
- [x] **7i:** Add roundtrip tests (xlsx -> cells -> xlsx)

**Deliverables:**
- `xlsx_writer.h` / `xlsx_writer.cc` - Workbook to XLSX (17 tests passing)
- `xlsx_writer_test.cc` - unit tests including roundtrip tests

---

## Phase 8: Formula Reference Conversion

Convert between UUID-based and A1 notation for Excel compatibility.

- [x] **8a:** Create `core/cells/ref_converter.h` and `ref_converter.cc`
- [x] **8b:** Implement UUID-to-A1 conversion (for export)
- [x] **8c:** Implement A1-to-UUID conversion (for import)
- [x] **8d:** Handle absolute vs relative references ($A$1 vs A1)
- [x] **8e:** Handle range references (A1:C3)
- [x] **8f:** Add `ref_converter_test.cc`

**Deliverables:**
- `ref_converter.h` / `ref_converter.cc` - reference conversion (45 tests passing)
- `ref_converter_test.cc` - unit tests

---

## Phase 9: XLSX Integration ✅

Wire XLSX support into the CLI.

- [x] **9a:** Add XLSX reader/writer to CLI conversion pipeline
- [x] **9b:** Integrate formula reference conversion
- [x] **9c:** Add XLSX-specific CLI options (--sheet, --all-sheets)
- [x] **9d:** Create end-to-end tests (xlsx → cells, cells → xlsx, csv ↔ xlsx)

**Deliverables:**
- Full `cells` CLI with XLSX support (23 tests passing)
- Complete format conversion matrix tested
- --sheet filter and --all-sheets export working

---

## Phase 10: Polish ✅

Final touches for release.

- [x] **10a:** Add man page or `--help` documentation
- [x] **10b:** Test on sample real-world files
- [x] **10c:** Performance testing with large files (100K+ cells)
- [x] **10d:** Memory profiling
- [x] **10e:** Update GETTING_STARTED.md with CLI usage
- [x] **10f:** Create example scripts in `examples/`

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

---

## Phase 11: Replace OpenXLSX with Excelize

> **Note:** This phase is superseded by the native XLSX implementation plan
> (`2025-12-19-xlsx-native-IN-PROGRESS.md`). The Excelize/Go dependency has been
> removed in favor of a pure C++ implementation using miniz + pugixml.

**Motivation:** OpenXLSX doesn't support shared or array formulas, which are very common in real Excel files. Excelize (Go) has better Excel parity and is actively maintained.

### Design Principle: Transient Codec

Excelize is used **only** as a transient parser/serializer. The document is never held open in excelize - our `Workbook` model is the single source of truth.

```
Read:   XLSX ──► excelize (parse) ──► Workbook ──► excelize freed immediately
Write:  Workbook ──► excelize (build) ──► XLSX ──► excelize freed immediately
```

**Benefits:**
- No handle management or lifecycle complexity
- Go's GC can reclaim memory immediately after read/write
- Simpler CGO interface - single function calls, not stateful APIs
- Clear ownership: C++ owns all data after parsing

**Trade-offs (acceptable for now):**
- Excel features not in our model are discarded on read (charts, pivot tables, etc.)
- These features won't round-trip through our format
- We can add model support for these features later

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    C++ Code (existing)                       │
│                    xlsx_reader.cc / xlsx_writer.cc           │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    C API (single-call functions)             │
│      ExcelizeParseXLSX(path) -> XLSXData*                   │
│      ExcelizeWriteXLSX(path, XLSXData*) -> error            │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Go Library (transient)                    │
│                    Open file, extract data, close, return    │
└─────────────────────────────────────────────────────────────┘
```

---

### Phase 11a: Bazel Go Toolchain Setup ✅

Set up rules_go for building Go code in Bazel.

- [x] **11a-1:** Add rules_go to MODULE.bazel
- [x] **11a-2:** Configure Go toolchain
- [x] **11a-3:** Create `bindings/go/BUILD.bazel`
- [x] **11a-4:** Add excelize as a Go dependency via gazelle
- [x] **11a-5:** Verify Go builds work: `bazel build //bindings/go:excelize_bridge`

**Deliverables:**
- Go toolchain working in Bazel
- excelize dependency resolved

---

### Phase 11b: C Data Structures ✅

Define C-compatible data structures for transferring spreadsheet data across the CGO boundary.

- [x] **11b-1:** Create `bindings/go/excelize_types.h` with C structs
- [x] **11b-2:** Define `XLSXCell` struct (row, col, value, formula, type)
- [x] **11b-3:** Define `XLSXSheet` struct (name, cells array, row/col dimensions)
- [x] **11b-4:** Define `XLSXData` struct (sheets array, sheet count)
- [x] **11b-5:** Define `XLSXError` struct (code, message)
- [x] **11b-6:** Add memory management functions (`XLSXDataFree`, etc.)

**Data structures:**
```c
// excelize_types.h
typedef struct {
    int row;
    int col;
    char* value;      // String representation
    char* formula;    // NULL if not a formula
    int cell_type;    // 0=empty, 1=string, 2=number, 3=bool, 4=error
} XLSXCell;

typedef struct {
    char* name;
    XLSXCell* cells;
    int cell_count;
    int row_count;
    int col_count;
    float* col_widths;   // NULL or array of col_count widths
    float* row_heights;  // NULL or array of row_count heights
} XLSXSheet;

typedef struct {
    XLSXSheet* sheets;
    int sheet_count;
} XLSXData;

// Single-call API
XLSXData* ExcelizeParseXLSX(const char* path, char** error_out);
int ExcelizeWriteXLSX(const char* path, const XLSXData* data, char** error_out);
void XLSXDataFree(XLSXData* data);
void XLSXErrorFree(char* error);
```

**Deliverables:**
- `excelize_types.h` - C-compatible data structures
- Clean memory ownership model (Go allocates, C++ frees via provided functions)

---

### Phase 11c: Go Parser Implementation ✅

Implement the Go side that parses XLSX and returns C structs.

- [x] **11c-1:** Create `bindings/go/excelize_bridge.go`
- [x] **11c-2:** Implement `ExcelizeParseXLSX` - open, extract all data, close, return
- [x] **11c-3:** Handle shared formulas (excelize expands them automatically)
- [x] **11c-4:** Handle array formulas
- [x] **11c-5:** Extract cell types correctly (number, string, bool, error)
- [x] **11c-6:** Extract column widths and row heights
- [x] **11c-7:** Implement proper C memory allocation for returned data
- [x] **11c-8:** Build as C-archive and verify header generation

**Key pattern - transient parse:**
```go
//export ExcelizeParseXLSX
func ExcelizeParseXLSX(path *C.char, errorOut **C.char) *C.XLSXData {
    f, err := excelize.OpenFile(C.GoString(path))
    if err != nil {
        *errorOut = C.CString(err.Error())
        return nil
    }
    defer f.Close()  // Always close!

    // Extract all data into C structs
    data := extractAllData(f)
    return data
}
```

**Deliverables:**
- `excelize_bridge.go` - Go parser with CGO exports
- `libexcelize.a` + `excelize.h` - C-compatible library

---

### Phase 11d: Go Writer Implementation ✅

Implement the Go side that writes XLSX from C structs.

- [x] **11d-1:** Implement `ExcelizeWriteXLSX` - create file, populate, save, close
- [x] **11d-2:** Write cell values with correct types
- [x] **11d-3:** Write formulas
- [x] **11d-4:** Write column widths and row heights
- [x] **11d-5:** Handle multiple sheets
- [x] **11d-6:** Add Go-side tests

**Key pattern - transient write:**
```go
//export ExcelizeWriteXLSX
func ExcelizeWriteXLSX(path *C.char, data *C.XLSXData, errorOut **C.char) C.int {
    f := excelize.NewFile()
    defer f.Close()  // Cleanup even on error

    // Populate from C structs
    populateFromData(f, data)

    if err := f.SaveAs(C.GoString(path)); err != nil {
        *errorOut = C.CString(err.Error())
        return -1
    }
    return 0
}
```

**Deliverables:**
- Complete read/write support
- Go-side tests for both directions

---

### Phase 11e: C++ Integration ✅

Update the C++ xlsx_reader/writer to use the excelize C API.

- [x] **11e-1:** Update `xlsx_reader.cc` to call `ExcelizeParseXLSX`
- [x] **11e-2:** Convert `XLSXData*` to our `Workbook` model
- [x] **11e-3:** Call `XLSXDataFree` after conversion
- [x] **11e-4:** Update `xlsx_writer.cc` to build `XLSXData*` from `Workbook`
- [x] **11e-5:** Call `ExcelizeWriteXLSX` and free the data
- [x] **11e-6:** Update BUILD files to link against libexcelize.a
- [x] **11e-7:** Keep the public `XLSXReader`/`XLSXWriter` interface unchanged

**C++ integration pattern:**
```cpp
XLSXReadResult XLSXReader::readFile(const std::string& path) {
    char* error = nullptr;
    XLSXData* data = ExcelizeParseXLSX(path.c_str(), &error);

    if (error) {
        std::string msg(error);
        XLSXErrorFree(error);
        return XLSXReadResult{nullptr, XLSXReadError(msg)};
    }

    // Convert to our Workbook model
    auto workbook = convertToWorkbook(data);
    XLSXDataFree(data);  // Free immediately after conversion

    return XLSXReadResult{std::move(workbook), std::nullopt};
}
```

**Deliverables:**
- `xlsx_reader.cc` / `xlsx_writer.cc` updated
- All existing tests pass
- Shared/array formulas now work

---

### Phase 11f: Cleanup & Validation ✅

Remove OpenXLSX and validate the migration.

- [x] **11f-1:** Remove OpenXLSX from MODULE.bazel/WORKSPACE
- [x] **11f-2:** Remove any OpenXLSX-specific includes/code
- [ ] **11f-3:** Add tests for shared formula reading *(deferred)*
- [ ] **11f-4:** Add tests for array formula reading *(deferred)*
- [ ] **11f-5:** Test with real-world Excel files (calendars, financial models, etc.) *(deferred)*
- [ ] **11f-6:** Performance benchmark: excelize vs OpenXLSX *(deferred)*
- [x] **11f-7:** Update documentation

**Deliverables:**
- Clean codebase with single XLSX implementation
- Better Excel parity verified with real files
- Performance comparison documented

---

## Phase 11 File Layout

```
cells/
├── bindings/
│   └── go/
│       ├── BUILD.bazel
│       ├── go.mod                    # Go module (excelize dependency)
│       ├── excelize_types.h          # C-compatible data structures
│       └── excelize_bridge.go        # Go codec with CGO exports
├── core/
│   └── cells/
│       ├── xlsx_reader.cc            # Updated to use excelize
│       └── xlsx_writer.cc            # Updated to use excelize
```

---

## Phase 11 Technical Notes

### Design Pattern: Transient Codec

All format codecs (CSV, XLSX, .cells) follow the same pattern:

```
┌─────────────┐      ┌─────────────┐      ┌─────────────┐
│  File on    │ ───► │  Transient  │ ───► │  Workbook   │
│  disk       │      │  Parser     │      │  (in memory)│
└─────────────┘      └─────────────┘      └─────────────┘
                           │
                           ▼
                     Parser closed,
                     memory freed

┌─────────────┐      ┌─────────────┐      ┌─────────────┐
│  Workbook   │ ───► │  Transient  │ ───► │  File on    │
│  (in memory)│      │  Serializer │      │  disk       │
└─────────────┘      └─────────────┘      └─────────────┘
                           │
                           ▼
                     Serializer closed,
                     memory freed
```

**Benefits:**
- Single source of truth (Workbook model)
- No codec-specific state to manage
- Clear memory ownership
- Easy to add new formats

### CGO Memory Management

Since we use transient parsing, memory management is simple:

1. **Go allocates** - `C.malloc` for structs, `C.CString` for strings
2. **C++ converts** - Copy data into Workbook model
3. **C++ frees** - Call `XLSXDataFree()` immediately after conversion

```go
// Go side - allocate C memory
func allocXLSXCell(row, col int, value, formula string, cellType int) *C.XLSXCell {
    cell := (*C.XLSXCell)(C.malloc(C.sizeof_XLSXCell))
    cell.row = C.int(row)
    cell.col = C.int(col)
    cell.value = C.CString(value)
    if formula != "" {
        cell.formula = C.CString(formula)
    } else {
        cell.formula = nil
    }
    cell.cell_type = C.int(cellType)
    return cell
}
```

```cpp
// C++ side - free after use
void XLSXDataFree(XLSXData* data) {
    for (int i = 0; i < data->sheet_count; i++) {
        XLSXSheet* sheet = &data->sheets[i];
        free(sheet->name);
        for (int j = 0; j < sheet->cell_count; j++) {
            free(sheet->cells[j].value);
            free(sheet->cells[j].formula);  // NULL-safe
        }
        free(sheet->cells);
        free(sheet->col_widths);
        free(sheet->row_heights);
    }
    free(data->sheets);
    free(data);
}
```

### Bazel CGO Integration

```starlark
# bindings/go/BUILD.bazel
load("@rules_go//go:def.bzl", "go_binary")

go_binary(
    name = "libexcelize",
    srcs = ["excelize_bridge.go"],
    cgo = True,
    linkmode = "c-archive",
    deps = ["@com_github_xuri_excelize_v2//:excelize"],
    visibility = ["//visibility:public"],
)

# This produces:
#   bazel-bin/bindings/go/libexcelize.a  (static library)
#   bazel-bin/bindings/go/libexcelize.h  (generated header)
```

```starlark
# core/cells/BUILD
cc_library(
    name = "xlsx_reader",
    srcs = ["xlsx_reader.cc"],
    hdrs = ["xlsx_reader.h"],
    deps = [
        ":model",
        "//bindings/go:libexcelize",  # Link the Go library
    ],
)
```

---

## Excelize Feature Comparison

| Feature | OpenXLSX | Excelize |
|---------|----------|----------|
| Shared formulas | ❌ Throws | ✅ Expands automatically |
| Array formulas | ❌ Throws | ✅ Supported |
| Streaming read | ❌ | ✅ For large files |
| Streaming write | ❌ | ✅ For large files |
| Charts | Partial | ✅ Full support |
| Pivot tables | ❌ | ✅ Supported |
| Conditional formatting | ❌ | ✅ Supported |
| Data validation | Partial | ✅ Full support |
| Images | ✅ | ✅ |
| Comments | ✅ | ✅ |
| Maintenance | Active | Very active (18k+ stars) |
