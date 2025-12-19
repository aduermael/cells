# Plan: Native XLSX Implementation

## Goals

1. **Remove Excelize completely** - eliminate Go/CGO dependency
2. **Support shared and array formulas** when reading XLSX
3. **Implement XLSX write** using pure C++ (miniz + pugixml)

---

## Background: Shared and Array Formulas in Excel

### Shared Formulas

Shared formulas are an optimization in XLSX files to avoid storing duplicate formula text. Instead of storing `=A1+B1` in C1, `=A2+B2` in C2, etc., Excel stores:

```xml
<!-- Master cell defines the formula and range -->
<c r="C1">
  <f t="shared" ref="C1:C100" si="0">A1+B1</f>
  <v>10</v>
</c>

<!-- Subsequent cells reference the shared index -->
<c r="C2">
  <f t="shared" si="0"/>
  <v>20</v>
</c>
```

The formula in C2 is **implicit** - it's derived by adjusting relative references from the master formula.

### Array Formulas

Array formulas compute over ranges and can return multiple values. Legacy syntax: `{=SUM(A1:A10*B1:B10)}`. Modern Excel uses dynamic arrays.

```xml
<c r="A1">
  <f t="array" ref="A1:A5" aca="false">TRANSPOSE(B1:F1)</f>
  <v>1</v>
</c>
```

- `t="array"` marks it as an array formula
- `ref` defines the spill range (where results go)
- `aca` (Always Calculate Array) forces recalc

### Our Strategy for Formulas

**For reading**: Preserve shared formulas in our model. The master cell stores the formula text, subscriber cells reference the master. This enables AST reuse (parse once, clone and adjust references per cell).

**For writing**: Export shared formulas back to XLSX format. Array formulas need the `t="array"` attribute preserved.

### Reference Locking Syntax (UUID-based)

In our `.cells` format, cell references use `<colUUID>:<rowUUID>` with optional locking prefix:

| Syntax | Meaning | Excel equivalent |
|--------|---------|------------------|
| `cXXX:rYYY` | Both relative (default) | `A1` |
| `$$cXXX:rYYY` | Both absolute | `$A$1` |
| `$~cXXX:rYYY` | Col absolute, row relative | `$A1` |
| `~$cXXX:rYYY` | Col relative, row absolute | `A$1` |

- `cXXX` = column UUID, `rYYY` = row UUID
- No prefix = implicit `~~` (both relative) - most common case
- When any dimension is locked, both markers are explicit for clarity
- The `:` separates column UUID from row UUID

---

## Phase 1: Remove Excelize Dependency

### 1.1 Delete Go Bridge Files

- [x] Delete `bindings/go/excelize_bridge.go`
- [x] Delete `bindings/go/excelize_types.h`
- [x] Delete `bindings/go/BUILD.bazel`
- [x] Remove `com_github_xuri_excelize_v2` from `MODULE.bazel`

### 1.2 Update Build Files

- [x] Update `core/cells/BUILD` to remove excelize_bridge dependency
- [x] Remove cgo_library and go_library rules

### 1.3 Clean Up Reader Comments

- [x] Remove "Uses Excelize" comments from `xlsx_reader.h`
- [x] Remove "Uses Excelize" comments from `xlsx_writer.h`

---

## Phase 2: Support Shared Formulas

- [x] 2a: Ensure .cells writer outputs UUIDs in alphabetical order (columns, rows, cells)
- [x] 2b: Add SharedFormulaGroup and sharedFormulaRef to Cell model
- [x] 2c: Implement XLSX reader shared formula parsing (master and subscriber cells)
- [x] 2d: Implement shared formula master deletion/promotion (done in 2b via SharedFormulaGroup::promoteMaster)
- [x] 2e: Update .cells format parser/writer for shared formulas
- [x] 2f: Add tests for shared formula round-trip

### Design Notes

**File Format**: Formulas are part of cell definitions. Shared formulas use `=@UUID` to reference another cell's formula:

```
# Cells written in UUID alphabetical order
# Master (c001) is first alphabetically, has the actual formula
c c001  ...  =cA01:rB02+$$cA01:rB03
c c002  ...  =@c001
c c003  ...  =@c001
```

**Key rules:**
- Cells are written in UUID alphabetical order
- Master cell = first cell alphabetically among the shared group (deterministic)
- Subscribers use `=@masterUUID` to reference the master's formula

**Reference Syntax**: Cell references use `colUUID:rowUUID` format with optional locking prefix:

| Syntax | Meaning | Excel equivalent |
|--------|---------|------------------|
| `cXXX:rYYY` | Both relative | `A1` |
| `$$cXXX:rYYY` | Both absolute | `$A$1` |
| `$~cXXX:rYYY` | Col absolute, row relative | `$A1` |
| `~$cXXX:rYYY` | Col relative, row absolute | `A$1` |

**Model Changes**:
```cpp
struct Cell {
    Formula* formula;           // Own formula, or nullptr
    Cell* sharedFormulaRef;     // Points to master if using shared formula
};

struct SharedFormulaGroup {
    Cell* master;                    // First alphabetically
    std::vector<Cell*> subscribers;  // Cells using =@master
};
```

**XLSX Reader**: When encountering `<f t="shared" ref="..." si="N">formula</f>` (master), parse and store. When encountering `<f t="shared" si="N"/>` (subscriber), link to master.

**Master Deletion**: When master is deleted, promote next subscriber (first alphabetically) to master, clone AST, update references.

**AST Evaluation**: For subscribers, get master's AST and adjust relative references by row/col offset. Absolute references (`$$`) unchanged

---

## Phase 3: Support Array Formulas (Reader) - POSTPONED

> **Status**: Postponed - array formulas are not urgent.

- [ ] 3a: Add isArrayFormula and arrayRange fields to Formula struct
- [ ] 3b: Implement XLSX reader array formula parsing
- [ ] 3c: Update .cells format for array formula preservation
- [ ] 3d: Add tests for array formula support

### Design Notes

**Model Changes** (Option A - flag on Formula struct):
```cpp
struct Formula {
    char* text;
    struct ASTNode* ast;
    bool dirty;
    bool isArrayFormula;     // NEW
    std::string arrayRange;  // NEW: "A1:A5" for spill range
};
```

**Reader**: When parsing `<f t="array" ref="..." aca="...">`, set `isArrayFormula = true` and store `arrayRange`. Mark master cell only.

**File Format**: Preserve array formula info in `.cells`:
```
f <cell_id>	=FORMULA	array:A1:A5
```

---

## Phase 4: Implement XLSX Write (Native)

- [ ] 4a: Set up ZIP archive creation with miniz
- [ ] 4b: Implement Content_Types.xml and root relationships (_rels/.rels)
- [ ] 4c: Implement workbook.xml and sheet relationships
- [ ] 4d: Implement worksheet XML generation (sheetData)
- [ ] 4e: Implement shared strings table (xl/sharedStrings.xml)
- [ ] 4f: Implement minimal styles.xml
- [ ] 4g: Implement formula conversion (UUID→A1 with locking markers)
- [ ] 4h: Implement shared formula export
- [ ] 4i: Implement array formula export
- [ ] 4j: Add comprehensive XLSX write tests

### Design Notes

**ZIP Creation with miniz**:
```cpp
mz_zip_archive archive;
mz_zip_writer_init_file(&archive, path.c_str(), 0);
mz_zip_writer_add_mem(&archive, "[Content_Types].xml", content, size, MZ_DEFAULT_COMPRESSION);
mz_zip_writer_finalize_archive(&archive);
mz_zip_writer_end(&archive);
```

**Required XLSX Parts**:

| File | Purpose |
|------|---------|
| `[Content_Types].xml` | MIME type registry |
| `_rels/.rels` | Root relationships |
| `xl/workbook.xml` | Workbook structure |
| `xl/_rels/workbook.xml.rels` | Sheet relationships |
| `xl/worksheets/sheet1.xml` | Sheet data |
| `xl/styles.xml` | Cell styles (minimal) |
| `xl/sharedStrings.xml` | String table |

**Shared String Table**:
```cpp
std::vector<std::string> sharedStrings;
std::unordered_map<std::string, size_t> stringIndex;

size_t getOrAddString(const std::string& str) {
    auto it = stringIndex.find(str);
    if (it != stringIndex.end()) return it->second;
    size_t idx = sharedStrings.size();
    sharedStrings.push_back(str);
    stringIndex[str] = idx;
    return idx;
}
```

**Formula Conversion**: Convert UUID-based formulas back to A1 notation using `RefConverter`. Locking markers: `UUID`→`A1`, `$$UUID`→`$A$1`, `$~UUID`→`$A1`, `~$UUID`→`A$1`.

**Shared Formula Export**: Group cells by master, assign sequential `si` indices, write master with `<f t="shared" ref="..." si="N">formula</f>`, subscribers with `<f t="shared" si="N"/>`.

**Array Formula Output**:
```xml
<c r="A1">
  <f t="array" ref="A1:A5">TRANSPOSE(B1:F1)</f>
  <v>1</v>
</c>
```

---

## Implementation Order

1. **Phase 1** (Remove Excelize) - Start here, unlocks clean builds
2. **Phase 4** (XLSX Write) - Most impactful, enables round-trip
3. **Phase 2** (Shared Formulas) - Needed for correct formula import
4. **Phase 3** (Array Formulas) - Lower priority, less common

---

## Testing Strategy

### Unit Tests
- Reference locking syntax parsing (`UUID`, `$$UUID`, `$~UUID`, `~$UUID`)
- Shared formula group management (master, subscribers)
- Master deletion and promotion
- AST offset evaluation for subscribers
- Array formula flag preservation
- String table deduplication
- A1 reference parsing edge cases (XFD1048576)

### Integration Tests
- Round-trip: XLSX → Cells → XLSX → compare
- Test files with shared formulas (export from Excel)
- Test files with array formulas
- Large file (100k+ cells) performance

### Test Files to Create
- `test_shared_formula.xlsx` - File with shared formulas
- `test_array_formula.xlsx` - File with array/spill formulas
- `test_mixed.xlsx` - Both types

---

## Open Questions

1. **Data tables**: Support `t="dataTable"` formulas? (rare, lower priority)
2. **Styles**: Minimal styles.xml vs full style support?
3. **Named ranges**: Import/export named range formulas?

---

## References

- [CellFormula Class - Microsoft Learn](https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.spreadsheet.cellformula)
- [Working with formulas - Microsoft Learn](https://learn.microsoft.com/en-us/office/open-xml/spreadsheet/working-with-formulas)
- [ECMA-376 SpreadsheetML Specification](https://www.ecma-international.org/publications-and-standards/standards/ecma-376/)
