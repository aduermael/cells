# Parser/Serializer Implementation Plan

**Status:** READY
**Created:** 2025-12-16
**Goal:** Implement the `.cells` text format parser and serializer with sample files and tests.

## Scope

Implement parsing and serialization for the `.cells` v1 text format as specified in `docs/persistence.md`. This includes:
- Document header (`#cells v1`, `D`, `S`)
- Columns and rows with doubly-linked list structure (`C`, `R`)
- Cells with all value types (`X`)
- Styles and cell-style mappings (`T`, `Y`) - deferred to later plan
- OpLog entries (`O`) - deferred to later plan

**Out of scope for this plan:**
- Binary format (`.cellsb`)
- Compressed format (`.cellsz`)
- Styles and cell-styles sections
- OpLog section
- XLSX/CSV import/export

---

## Phase 1: Data Structures

Define the core C++ types needed for the parser.

- [ ] **1a:** Create `core/cells/types.h` with basic types (ID, CellValueType, CellError)
- [ ] **1b:** Create `core/cells/model.h` with Cell, Axis, Sheet, Workbook structs
- [ ] **1c:** Create `core/cells/model.cc` with constructors and methods

**Deliverables:**
- `types.h` - enums, typedefs, ID type
- `model.h` / `model.cc` - struct definitions with constructors

---

## Phase 2: Parser Implementation

Implement the text format parser.

- [ ] **2a:** Create `core/cells/parser.h` with parser API
- [ ] **2b:** Create `core/cells/parser.cc` with line-by-line parser
- [ ] **2c:** Implement section parsing (#cols, #rows, #cells)
- [ ] **2d:** Implement value type parsing (n, s, f, b, e, d, t)
- [ ] **2e:** Implement linked-list parsing with gap notation (prev:gap next:gap)
- [ ] **2f:** Implement axis properties parsing (w:, h:, name:)
- [ ] **2g:** Add parser error handling with line numbers

**Deliverables:**
- `parser.h` - `parse()` function, `ParseError` struct
- `parser.cc` - full parser implementation

---

## Phase 3: Serializer Implementation

Implement the text format serializer.

- [ ] **3a:** Create `core/cells/serializer.h` with serializer API
- [ ] **3b:** Create `core/cells/serializer.cc` with basic structure
- [ ] **3c:** Implement axis serialization with gap notation
- [ ] **3d:** Implement cell serialization for all value types
- [ ] **3e:** Implement string escaping for quoted values
- [ ] **3f:** Add serialization to file and to string

**Deliverables:**
- `serializer.h` - `serialize()` function
- `serializer.cc` - full serializer implementation

---

## Phase 4: Sample Files

Create test data files covering various scenarios.

- [ ] **4a:** Create `core/testdata/minimal.cells` - simplest valid file (1 cell)
- [ ] **4b:** Create `core/testdata/simple.cells` - basic file from docs (A1=2, A2="foo", D4=formula)
- [ ] **4c:** Create `core/testdata/budget.cells` - larger example from docs (budget spreadsheet)
- [ ] **4d:** Create `core/testdata/all_types.cells` - all cell value types (n, s, f, b, e, d, t)
- [ ] **4e:** Create `core/testdata/gaps.cells` - sparse grid with various gap encodings
- [ ] **4f:** Create `core/testdata/unicode.cells` - unicode strings and sheet names
- [ ] **4g:** Create `core/testdata/empty.cells` - valid file with no cells

**Deliverables:**
- 7 sample `.cells` files in `core/testdata/`

---

## Phase 5: Tests

Implement unit tests for parser and serializer.

- [ ] **5a:** Create `core/cells/parser_test.cc` - parser unit tests (Google Test)
- [ ] **5b:** Add tests for each sample file (parse succeeds)
- [ ] **5c:** Add tests for malformed files (parse fails with correct error)
- [ ] **5d:** Create `core/cells/serializer_test.cc` - serializer unit tests
- [ ] **5e:** Add roundtrip tests (parse → serialize → parse, compare)
- [ ] **5f:** Set up Bazel BUILD files with test targets

**Deliverables:**
- `parser_test.cc` - parser tests
- `serializer_test.cc` - serializer tests
- `BUILD` files with `bazel test` targets

---

## Phase 6: ID Generation

Implement base62 ID generation for creating new entities.

- [ ] **6a:** Create `core/cells/id.h` and `core/cells/id.cc` with ID generation
- [ ] **6b:** Implement rejection sampling to avoid modulo bias
- [ ] **6c:** Add `core/cells/id_test.cc` for ID generation tests (length, charset, uniqueness)

**Deliverables:**
- `id.h` / `id.cc` - `generate_id()` function
- `id_test.cc` - tests for ID generation

---

## File Layout After Completion

```
WORKSPACE                    # Bazel workspace
core/
├── BUILD                    # Bazel build file
├── cells/
│   ├── BUILD                # Bazel build file for cells library
│   ├── types.h
│   ├── model.h
│   ├── model.cc
│   ├── id.h
│   ├── id.cc
│   ├── id_test.cc
│   ├── parser.h
│   ├── parser.cc
│   ├── parser_test.cc
│   ├── serializer.h
│   ├── serializer.cc
│   └── serializer_test.cc
└── testdata/
    ├── minimal.cells
    ├── simple.cells
    ├── budget.cells
    ├── all_types.cells
    ├── gaps.cells
    ├── unicode.cells
    └── empty.cells
```

---

## Notes

- IDs are 8-character base62 strings (as per `docs/persistence.md`)
- Gap notation: `prev:2` means 2 empty positions between prev and this axis
- `~` represents null (no prev/next)
- Strings are double-quoted, formulas stored as quoted ID-based references
- Parser should handle blank lines and ignore unknown section headers gracefully
