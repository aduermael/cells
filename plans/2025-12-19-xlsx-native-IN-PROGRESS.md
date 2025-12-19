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

**For reading**: Expand shared formulas at import time into our model (each cell gets its own formula). This is simpler and matches our UUID-based reference model. We don't need to preserve the optimization - we have our own format.

**For writing**: We won't generate shared formulas (Excel will optimize on load). Array formulas need the `t="array"` attribute preserved.

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

## Phase 2: Support Shared Formulas (Reader)

### 2.1 Parse Shared Formula Master

When encountering `<f t="shared" ref="..." si="N">formula</f>`:
- Store in a map: `shared_formulas[si] = {master_ref, master_formula, ref_range}`

### 2.2 Expand Shared Formula References

When encountering `<f t="shared" si="N"/>` (no formula text):
1. Look up master formula from `shared_formulas[si]`
2. Calculate offset from master cell to current cell
3. Adjust relative references in formula (A1 style)
4. Convert to our UUID-based formula

### 2.3 Reference Adjustment Logic

For a formula like `=A1+B1` in cell C1, referenced from C5:
- Row offset = 5 - 1 = 4
- `A1` becomes `A5`, `B1` becomes `B5`
- Absolute refs (`$A$1`) don't change
- Mixed refs (`$A1`, `A$1`) adjust appropriately

Implementation approach:
```cpp
struct SharedFormulaInfo {
    int masterRow, masterCol;
    std::string formulaText;
    std::string refRange;
};

std::string expandSharedFormula(
    const SharedFormulaInfo& master,
    int currentRow, int currentCol);
```

---

## Phase 3: Support Array Formulas (Reader)

### 3.1 Model Changes

Option A: **Flag on Formula struct**
```cpp
struct Formula {
    char* text;
    struct ASTNode* ast;
    bool dirty;
    bool isArrayFormula;     // NEW
    std::string arrayRange;  // NEW: "A1:A5" for spill range
};
```

Option B: **Separate ArrayFormula type** (more explicit but more complex)

**Recommendation**: Option A (simpler, array formulas are relatively rare)

### 3.2 Reader Changes

When parsing `<f t="array" ref="..." aca="...">`:
- Set `formula->isArrayFormula = true`
- Store `formula->arrayRange` (needed for write round-trip)
- Mark the master cell only; other cells in range don't have formulas

### 3.3 File Format Update

If we want to preserve array formula info in `.cells` format:
```
f <cell_id>	=FORMULA	array:A1:A5
```

---

## Phase 4: Implement XLSX Write (Native)

### 4.1 ZIP Creation with miniz

```cpp
mz_zip_archive archive;
mz_zip_writer_init_file(&archive, path.c_str(), 0);

// Add files to archive
mz_zip_writer_add_mem(&archive, "[Content_Types].xml", content, size, MZ_DEFAULT_COMPRESSION);

mz_zip_writer_finalize_archive(&archive);
mz_zip_writer_end(&archive);
```

### 4.2 Required XLSX Parts

| File | Purpose |
|------|---------|
| `[Content_Types].xml` | MIME type registry |
| `_rels/.rels` | Root relationships |
| `xl/workbook.xml` | Workbook structure |
| `xl/_rels/workbook.xml.rels` | Sheet relationships |
| `xl/worksheets/sheet1.xml` | Sheet data |
| `xl/styles.xml` | Cell styles (minimal) |
| `xl/sharedStrings.xml` | String table |

### 4.3 XML Generation with pugixml

```cpp
pugi::xml_document doc;
auto decl = doc.append_child(pugi::node_declaration);
decl.append_attribute("version") = "1.0";
decl.append_attribute("encoding") = "UTF-8";

auto worksheet = doc.append_child("worksheet");
worksheet.append_attribute("xmlns") = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";

// Build sheetData...
```

### 4.4 Shared String Table

Build string table during cell enumeration:
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

### 4.5 Formula Conversion

Convert UUID-based formulas back to A1 notation:
- Build column/row position maps
- Replace `$<colUUID>$<rowUUID>` with `$A$1` style refs
- Use existing `RefConverter` class

### 4.6 Array Formula Output

For cells with `isArrayFormula`:
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
- Shared formula expansion with various reference types
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
