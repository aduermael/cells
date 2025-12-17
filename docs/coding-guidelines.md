# Coding Guidelines

## Code Quality Tools

### Formatting (clang-format)

All C++ code must be formatted using clang-format. Configuration is in `.clang-format`.

```bash
# Format all files
./scripts/format.sh

# Check formatting (CI mode)
./scripts/format.sh --check

# Format specific files
./scripts/format.sh core/cells/model.cc
```

### Linting (clang-tidy)

Static analysis using clang-tidy. Configuration is in `.clang-tidy`.

```bash
# Lint all files
./scripts/lint.sh

# Lint and auto-fix
./scripts/lint.sh --fix

# Lint specific files
./scripts/lint.sh core/cells/model.cc
```

### Combined Check

Run all checks (format + lint + build):
```bash
./scripts/check.sh          # Check mode
./scripts/check.sh --fix    # Fix mode
```

### Pre-commit Hook (Optional)

Install to run format checks before each commit:
```bash
ln -sf ../../scripts/pre-commit .git/hooks/pre-commit
```

### Requirements

Install the tools:
```bash
brew install clang-format llvm
export PATH="$(brew --prefix llvm)/bin:$PATH"  # Add to .zshrc
```

### Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Structs/Classes | PascalCase | `CellValue`, `Sheet` |
| Enums | PascalCase | `CellValueType` |
| Enum constants | UPPER_CASE | `NUMBER`, `STRING` |
| Functions | camelCase | `getValue`, `addCell` |
| Variables | camelCase | `cellId`, `rowCount` |
| Private members | _camelCase | `_cellIndex` |
| Constants | UPPER_CASE | `ID_LENGTH`, `DEFAULT_ROW_HEIGHT` |
| Namespaces | snake_case | `cells` |

## Standard Library Usage

Avoid `std::` when avoidable. Prefer simpler, more explicit alternatives:

| Avoid | Prefer | Reason |
|-------|--------|--------|
| `std::variant` | Tagged union or pointer | Simpler, no template bloat |
| `std::optional` | Pointer or sentinel value | More explicit |
| `std::function` | Function pointer or virtual | Less overhead |
| `std::shared_ptr` | Raw pointer with clear ownership | Simpler lifetime |

**Acceptable std:: usage:**
- `std::string` - No practical alternative for dynamic strings
- `std::vector` - No practical alternative for dynamic arrays
- `std::unordered_map` - Hash maps are complex to implement correctly
- `std::unique_ptr` - RAII for owned heap objects (prevents leaks)
- `<cstdint>` types - `uint32_t`, `size_t`, etc.
- `<cstring>` functions - `memcpy`, `memset`, etc.

## Memory Management

- Prefer stack allocation over heap when size is known
- Use raw pointers for non-owning references
- Use `std::unique_ptr` for owning single objects (acceptable std:: usage)
- Document ownership clearly in comments
- Avoid `std::shared_ptr` - if ownership is unclear, redesign

## Error Handling

- Return error codes or use out parameters for expected failures
- Use `nullptr` returns for "not found" cases
- Reserve exceptions for truly exceptional cases (memory exhaustion, etc.)

## Struct Design

Member functions (including constructors) **do not increase struct size**. They're stored once in the code segment, not per-instance.

```cpp
struct A { int x; char* y; };
struct B { int x; char* y; B(); void foo(); ~B(); };
// Both are exactly 16 bytes - functions add no per-instance overhead
```

**What adds to struct size:**
- Data members
- `virtual` functions (add vtable pointer, +8 bytes on 64-bit)

**What does NOT add to struct size:**
- Constructors, destructors
- Regular member functions
- `static` members

**Guideline:** Use constructors and member functions freely for cleaner code. Only avoid `virtual` in memory-critical structs.

## Struct Layout and Padding

C++ compilers insert padding bytes to satisfy alignment requirements. To minimize wasted space:

**Order members by alignment (largest first):**
```cpp
// BAD - 7 bytes padding
struct Bad {
    char a;          // 1 byte, offset 0
                     // 7 bytes padding
    double b;        // 8 bytes, offset 8
};                   // Total: 16 bytes (only 9 useful)

// GOOD - no padding
struct Good {
    double b;        // 8 bytes, offset 0
    char a;          // 1 byte, offset 8
};                   // Total: 16 bytes (9 useful, 7 tail padding unavoidable)
```

**Alignment requirements (64-bit):**
| Type | Size | Alignment |
|------|------|-----------|
| `char`, `bool` | 1 | 1 |
| `uint16_t` | 2 | 2 |
| `uint32_t`, `float` | 4 | 4 |
| `uint64_t`, `double`, pointers, `std::string` | 8 | 8 |

**Guideline:** Sort struct members by decreasing alignment. Group same-alignment fields together. Put `bool` and `char` fields at the end.

## Performance

- Prefer contiguous memory (arrays, vectors) over linked structures
- Avoid virtual functions in hot paths
- Use `const&` for read-only parameters to avoid copies
- Consider cache locality in data structure design
