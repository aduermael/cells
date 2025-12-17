# Coding Guidelines

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

## Performance

- Prefer contiguous memory (arrays, vectors) over linked structures
- Avoid virtual functions in hot paths
- Use `const&` for read-only parameters to avoid copies
- Consider cache locality in data structure design
