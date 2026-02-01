# Type System

## Implementation Status

**Current state:** Basic cell value types only.

| Component | Status |
|-----------|--------|
| Cell value types (number, string, boolean, date, datetime, formula, error) | ✅ Implemented |
| Column type constraints | ❌ Not implemented |
| Type validation/coercion | ❌ Not implemented |
| Relations (foreign keys) | ❌ Not implemented |
| Select/multi-select | ❌ Not implemented |
| Formula type hints | ❌ Not implemented |

Cells are currently dynamically typed with no optional constraints. This document describes the planned type system architecture.

---

## Overview

Cells uses **completely optional typing**: cells are dynamically typed by default (exactly like Excel), and columns can optionally have type constraints.

### Philosophy: Excel-First, Not AirTable

- **Default behavior**: No types, no constraints, just like Excel
- **Opt-in gradually**: Add column types when/if you need them
- **Never enforced**: Constraints warn, not block
- **Always exportable**: XLSX export always works

## Type Enforcement Layers (Planned)

```
┌─────────────────────────────────────────────────────────────────┐
│ Layer 1: Cell Values (Runtime)                    ✅ Implemented │
│ - Always stored with their actual type                          │
│ - No implicit coercion on storage                               │
├─────────────────────────────────────────────────────────────────┤
│ Layer 2: Column Constraints (Validation)        ❌ Not implemented│
│ - Optional type + validation rules per column                   │
│ - Enforced on input, import, API writes                         │
├─────────────────────────────────────────────────────────────────┤
│ Layer 3: Formula Type Hints (Static Analysis)   ❌ Not implemented│
│ - Type inference from column constraints                        │
│ - Warnings at formula edit time, not runtime errors             │
└─────────────────────────────────────────────────────────────────┘
```

## Cell Value Types

The following types are implemented in `core/cells/types.h`:

| Type | File Code | Description |
|------|-----------|-------------|
| `NUMBER` | `n` | IEEE 754 double |
| `STRING` | `s` | UTF-8 text |
| `BOOLEAN` | `b` | true/false |
| `ERROR` | `e` | #REF!, #VALUE!, etc. |
| `DATE` | `d` | ISO 8601 date (2024-01-15) |
| `DATE_TIME` | `t` | ISO 8601 datetime (2024-01-15T10:30:00Z) |
| `FORMULA` | `f` | Formula expression (=A1+B1) |

Empty cells have no entry in the cell storage (null by absence).

Formula result types (`FORMULA_NUMBER`, `FORMULA_STRING`, etc.) are used internally to track both "this is a formula" and "the computed result type" but serialize as `f` in the file format.

### Error Types

Error values are stored with type `e` and one of these error codes:

| Error | String | Description |
|-------|--------|-------------|
| `VALUE` | `#VALUE!` | Wrong type of argument |
| `REF` | `#REF!` | Invalid cell reference |
| `NAME` | `#NAME?` | Unrecognized formula name |
| `DIV` | `#DIV/0!` | Division by zero |
| `NULL_REF` | `#NULL!` | Incorrect range |
| `NUM` | `#NUM!` | Invalid numeric value |
| `CIRCULAR` | `#CIRCULAR!` | Circular reference detected |
| `NA` | `#N/A` | Value not available |
| `SPILL` | `#SPILL!` | Array formula blocked by data |
| `CALC` | `#CALC!` | Calculation error |

## Column Type Constraints (Not Implemented)

| Type | Description |
|------|-------------|
| `ANY` | No constraint (default) |
| `NUMBER` | Numeric values |
| `TEXT` | Text only |
| `BOOLEAN` | Checkbox |
| `DATE` / `DATETIME` | Date picker |
| `SELECT` | Single select from options |
| `MULTI_SELECT` | Multiple selections |
| `RELATION` | Link to another sheet |
| `URL` / `EMAIL` / `PHONE` | Validated formats |
| `CURRENCY` / `PERCENT` | Number with formatting |

### Constraint Properties (Planned)

- `required`: NULL not allowed
- `unique`: Values must be unique in column
- Type-specific: `min/max`, `regex`, `options`, `target_sheet`

## Validation Flow (Not Implemented)

1. **Parse value**: `"42"` → Number(42)
2. **Check constraints**: If column has type, validate
3. **Coerce if needed**: Try to convert to expected type
4. **Store or reject**: Valid values stored, invalid rejected with error

### Type Coercion Rules (Planned)

| Column Type | Input | Result |
|-------------|-------|--------|
| NUMBER | `"42"` | `42` (coerced) |
| NUMBER | `"hello"` | Error |
| TEXT | `42` | `"42"` (coerced) |
| DATE | `"2024-01-15"` | Date (parsed) |
| BOOLEAN | `"yes"` | `true` (coerced) |
| SELECT | `"Option A"` | Valid if in options |

## Relations (Not Implemented)

Link cells to rows in other sheets via UUID:
- Display value looked up dynamically
- If linked row is renamed, all references update
- Stable across row moves

## Formula Type Hints (Not Implemented)

Type hints operate at **analysis time only**:
- Help catch errors during formula editing
- Type errors are warnings, not runtime failures
- At runtime, formulas still handle dynamic values

## XLSX Export (Partial)

### Feature Preservation

| Feature | XLSX Export |
|---------|-------------|
| Cell values | ✅ Preserved |
| Formulas | ✅ Preserved (converted to A1) |
| Basic formatting (fonts, colors, borders) | ✅ Preserved |
| Column widths, row heights | ✅ Preserved |
| Column types | N/A (not implemented) |
| Select options | N/A (not implemented) |
| Relations | N/A (not implemented) |
| Validation rules | N/A (not implemented) |

### Export Warning (Not Implemented)

Before export, show users which features will be lost:
- Data is always preserved
- Only metadata/features may be lost
- User confirms with clear understanding

### Stickiness Strategy (Planned)

Features Excel can't represent create natural stickiness:
- Relations, select options, validation
- Data is never held hostage - only features are lost
- This builds trust while creating value
