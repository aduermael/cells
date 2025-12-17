# Naming Conventions

## Types

| Kind | Convention | Example |
|------|------------|---------|
| Structs, classes | PascalCase | `Cell`, `CellValue`, `Workbook` |
| Type aliases | PascalCase | `using NodeId = uint64_t;` |
| Enums (type name) | PascalCase | `enum class CellError { ... }` |
| Template parameters | PascalCase | `template<typename T>` |

## Values

| Kind | Convention | Example |
|------|------------|---------|
| Functions, methods | camelCase | `isNull()`, `getValue()`, `addCell()` |
| Variables (local) | camelCase | `cellCount`, `firstCol` |
| Variables (member) | camelCase | `this->prevId`, `this->gapBefore` |
| Parameters | camelCase | `void setName(const std::string& newName)` |
| Constants | SCREAMING_SNAKE_CASE | `ID_LENGTH`, `DEFAULT_COL_WIDTH` |
| Enum values | SCREAMING_SNAKE_CASE | `CELL_ERROR_DIV`, `VALUE_TYPE_NUMBER` |

## Files

| Kind | Convention | Example |
|------|------------|---------|
| Source files | snake_case | `cell_value.cc`, `parser_test.cc` |
| Header files | snake_case | `cell_value.h`, `types.h` |
| Test files | `*_test.cc` | `parser_test.cc` |

## Namespaces

| Kind | Convention | Example |
|------|------------|---------|
| Namespaces | lowercase | `namespace cells { }` |

## Prefixes/Suffixes

| Kind | Convention | Example |
|------|------------|---------|
| Private members | `_` prefix | `_value`, `_cellCount` |
| Pointer params | No suffix | `cell` not `cellPtr` |
| Boolean getters | `is`/`has` prefix | `isNull()`, `hasError()` |
