# Type System

## Implementation Status

**Current state (December 2024):** Basic cell value types only.

| Component | Status |
|-----------|--------|
| Cell value types (number, string, boolean, date) | ✅ Implemented |
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

## Type Enforcement Layers

```
┌─────────────────────────────────────────────────────────────────┐
│ Layer 1: Cell Values (Runtime)                                   │
│ - Always stored with their actual type                          │
│ - No implicit coercion on storage                               │
├─────────────────────────────────────────────────────────────────┤
│ Layer 2: Column Constraints (Validation)                         │
│ - Optional type + validation rules per column                   │
│ - Enforced on input, import, API writes                         │
├─────────────────────────────────────────────────────────────────┤
│ Layer 3: Formula Type Hints (Static Analysis)                    │
│ - Type inference from column constraints                        │
│ - Warnings at formula edit time, not runtime errors             │
└─────────────────────────────────────────────────────────────────┘
```

## Cell Value Types

| Type | Description |
|------|-------------|
| `NULL` | Empty cell |
| `NUMBER` | IEEE 754 double |
| `STRING` | UTF-8 text |
| `BOOLEAN` | true/false |
| `ERROR` | #REF!, #VALUE!, etc. |
| `DATE` | Stored as number, displayed as date |
| `DURATION` | Stored as number (seconds) |
| `RICH_TEXT` | String with inline formatting |
| `ARRAY` | For array formulas / spill ranges |

## Column Type Constraints

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

### Constraint Properties

- `required`: NULL not allowed
- `unique`: Values must be unique in column
- Type-specific: `min/max`, `regex`, `options`, `target_sheet`

## Validation Flow

1. **Parse value**: `"42"` → Number(42)
2. **Check constraints**: If column has type, validate
3. **Coerce if needed**: Try to convert to expected type
4. **Store or reject**: Valid values stored, invalid rejected with error

### Type Coercion Rules

| Column Type | Input | Result |
|-------------|-------|--------|
| NUMBER | `"42"` | `42` (coerced) |
| NUMBER | `"hello"` | Error |
| TEXT | `42` | `"42"` (coerced) |
| DATE | `"2024-01-15"` | Date (parsed) |
| BOOLEAN | `"yes"` | `true` (coerced) |
| SELECT | `"Option A"` | Valid if in options |

## Relations (Foreign Keys)

Link cells to rows in other sheets via UUID:
- Display value looked up dynamically
- If linked row is renamed, all references update
- Stable across row moves

## Formula Type Hints

Type hints operate at **analysis time only**:
- Help catch errors during formula editing
- Type errors are warnings, not runtime failures
- At runtime, formulas still handle dynamic values

## XLSX Export

### Feature Preservation

| Feature | XLSX Export |
|---------|-------------|
| Cell values | Preserved |
| Formulas | Preserved (converted to A1) |
| Basic formatting | Preserved |
| Column types | Lost |
| Select options | Partial (dropdown, no colors) |
| Relations | Lost (becomes plain text) |
| Validation rules | Partial |

### Export Warning

Before export, show users which features will be lost:
- Data is always preserved
- Only metadata/features may be lost
- User confirms with clear understanding

### Stickiness Strategy

Features Excel can't represent create natural stickiness:
- Relations, select options, validation
- Data is never held hostage - only features are lost
- This builds trust while creating value
