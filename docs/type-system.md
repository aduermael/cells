# Type System

## Overview

Cells uses **optional/gradual typing**: cells are dynamically typed by default (Excel-compatible), but columns can optionally enforce type constraints (AirTable-inspired).

```
┌─────────────────────────────────────────────────────────────────┐
│                    Type Enforcement Layers                       │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Layer 1: Cell Values (Runtime)                              ││
│  │ - Always stored with their actual type                      ││
│  │ - No implicit coercion on storage                           ││
│  └─────────────────────────────────────────────────────────────┘│
│                              │                                   │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Layer 2: Column Constraints (Validation)                    ││
│  │ - Optional type + validation rules per column               ││
│  │ - Enforced on input, import, API writes                     ││
│  └─────────────────────────────────────────────────────────────┘│
│                              │                                   │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Layer 3: Formula Type Hints (Static Analysis / Compile)     ││
│  │ - Luau type annotations for optimization                    ││
│  │ - Warnings at formula edit time, not runtime errors         ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Cell Value Types

The fundamental types a cell can hold:

```c
typedef enum CellValueType {
    CELL_NULL,          // Empty cell
    CELL_NUMBER,        // IEEE 754 double (like Excel)
    CELL_STRING,        // UTF-8 text
    CELL_BOOLEAN,       // true/false
    CELL_ERROR,         // #REF!, #VALUE!, #DIV/0!, etc.
    CELL_DATE,          // Stored as number (days since epoch), displayed as date
    CELL_DURATION,      // Stored as number (seconds), displayed as duration
    CELL_RICH_TEXT,     // String with inline formatting
    CELL_ARRAY,         // For array formulas / spill ranges
} CellValueType;

typedef struct CellValue {
    CellValueType type;
    union {
        double number;          // NUMBER, DATE, DURATION
        char* string;           // STRING
        bool boolean;           // BOOLEAN
        CellError error;        // ERROR
        RichText* rich_text;    // RICH_TEXT
        CellArray* array;       // ARRAY
    };
} CellValue;
```

## Column Type Constraints

Columns can optionally specify a type schema:

```c
typedef enum ColumnType {
    COL_TYPE_ANY,           // No constraint (default)
    COL_TYPE_NUMBER,        // Numeric values only
    COL_TYPE_TEXT,          // Text only
    COL_TYPE_BOOLEAN,       // Checkbox
    COL_TYPE_DATE,          // Date picker
    COL_TYPE_DATETIME,      // Date + time
    COL_TYPE_DURATION,      // Time duration
    COL_TYPE_SELECT,        // Single select from options
    COL_TYPE_MULTI_SELECT,  // Multiple selections
    COL_TYPE_RELATION,      // Link to another sheet
    COL_TYPE_FORMULA,       // Computed (read-only)
    COL_TYPE_ATTACHMENT,    // File attachments
    COL_TYPE_URL,           // Validated URL
    COL_TYPE_EMAIL,         // Validated email
    COL_TYPE_PHONE,         // Phone number
    COL_TYPE_CURRENCY,      // Number with currency formatting
    COL_TYPE_PERCENT,       // Number displayed as percentage
    COL_TYPE_RATING,        // Star rating (1-5)
    COL_TYPE_BARCODE,       // Barcode/QR code
} ColumnType;

typedef struct ColumnConstraints {
    ColumnType type;
    bool required;              // NULL not allowed
    bool unique;                // Values must be unique in column

    union {
        struct {                // NUMBER, CURRENCY, PERCENT
            double min;
            double max;
            int precision;      // Decimal places
        } number;

        struct {                // TEXT
            int min_length;
            int max_length;
            char* regex;        // Validation pattern
        } text;

        struct {                // SELECT, MULTI_SELECT
            char** options;
            int option_count;
            uint32_t* colors;   // Color per option
        } select;

        struct {                // RELATION
            uuid_t target_sheet;
            uuid_t target_column;  // Display column
            bool allow_multiple;   // One-to-many
        } relation;

        struct {                // DATE, DATETIME
            int64_t min_date;   // Unix timestamp
            int64_t max_date;
            bool include_time;
        } date;
    };
} ColumnConstraints;
```

## Validation Flow

```
User Input / API Write / Import
            │
            ▼
    ┌───────────────┐
    │ Parse Value   │  "42" → Number(42)
    └───────┬───────┘  "hello" → String("hello")
            │
            ▼
    ┌───────────────┐
    │ Column Has    │───No───► Store as-is (dynamic)
    │ Constraints?  │
    └───────┬───────┘
            │Yes
            ▼
    ┌───────────────┐
    │ Type Match?   │───No───► Attempt coercion
    └───────┬───────┘              │
            │Yes                   ▼
            │              ┌───────────────┐
            │              │ Coercion OK?  │───No───► Reject with error
            │              └───────┬───────┘
            │                      │Yes
            ▼                      ▼
    ┌───────────────┐      ┌───────────────┐
    │ Validate      │      │ Store coerced │
    │ Constraints   │      │ value         │
    └───────┬───────┘      └───────────────┘
            │
            ▼
    ┌───────────────┐
    │ Valid?        │───No───► Reject with error
    └───────┬───────┘
            │Yes
            ▼
    Store value
```

### Type Coercion Rules

When a column has a type constraint, input is coerced if possible:

| Column Type | Input | Result |
|-------------|-------|--------|
| NUMBER | `"42"` | `42` (coerced) |
| NUMBER | `"hello"` | Error: not a number |
| NUMBER | `true` | `1` (coerced) |
| TEXT | `42` | `"42"` (coerced) |
| TEXT | `null` | Error if required, else `null` |
| DATE | `"2024-01-15"` | Date (parsed) |
| DATE | `45678` | Date (Excel serial) |
| BOOLEAN | `"yes"`, `"1"`, `"true"` | `true` (coerced) |
| SELECT | `"Option A"` | `"Option A"` if in options, else Error |

```c
typedef struct ValidationResult {
    bool valid;
    CellValue coerced_value;    // Value after coercion (if valid)
    char* error_message;        // Human-readable error (if invalid)
    char* error_code;           // Machine-readable code
} ValidationResult;

ValidationResult validate_cell_value(CellValue input,
                                      ColumnConstraints* constraints) {
    if (constraints == NULL || constraints->type == COL_TYPE_ANY) {
        return (ValidationResult){.valid = true, .coerced_value = input};
    }

    // Type coercion
    CellValue coerced = coerce_value(input, constraints->type);
    if (coerced.type == CELL_ERROR) {
        return (ValidationResult){
            .valid = false,
            .error_message = "Value cannot be converted to required type",
            .error_code = "TYPE_MISMATCH"
        };
    }

    // Required check
    if (constraints->required && coerced.type == CELL_NULL) {
        return (ValidationResult){
            .valid = false,
            .error_message = "This field is required",
            .error_code = "REQUIRED"
        };
    }

    // Type-specific validation
    switch (constraints->type) {
        case COL_TYPE_NUMBER:
            if (coerced.number < constraints->number.min ||
                coerced.number > constraints->number.max) {
                return (ValidationResult){
                    .valid = false,
                    .error_message = "Value out of range",
                    .error_code = "OUT_OF_RANGE"
                };
            }
            break;

        case COL_TYPE_TEXT:
            if (constraints->text.regex) {
                if (!regex_match(constraints->text.regex, coerced.string)) {
                    return (ValidationResult){
                        .valid = false,
                        .error_message = "Value doesn't match pattern",
                        .error_code = "PATTERN_MISMATCH"
                    };
                }
            }
            break;

        // ... other types
    }

    return (ValidationResult){.valid = true, .coerced_value = coerced};
}
```

## Formula Type Hints (Luau Static Analysis)

Luau's type system operates at **compile time only**, not runtime. This means:

1. Type annotations improve **bytecode generation** (faster execution)
2. Type errors are **warnings during formula editing**, not runtime failures
3. At runtime, formulas still handle dynamic values (Excel-compatible)

### Generated Luau with Types

When a formula references typed columns, we generate type hints:

```lua
-- Formula: =A1 * B1 + C1
-- Column A: NUMBER, Column B: NUMBER, Column C: ANY

local function formula(): number?
    local a: number = cell("uuid-a1") :: number   -- Known to be number
    local b: number = cell("uuid-b1") :: number   -- Known to be number
    local c = cell("uuid-c1")                     -- Unknown type

    return a * b + (tonumber(c) or 0)             -- Safe coercion for C
end
return formula()
```

### Type Inference in Formula Editor

The formula editor can show type information:

```
=SUM(A1:A10)
     ^^^^^^^^
     ✓ Column A is NUMBER - SUM will work correctly

=SUM(B1:B10)
     ^^^^^^^^
     ⚠ Column B is TEXT - SUM may return unexpected results
```

### Luau Type Definitions for Excel Functions

```lua
-- types.luau (loaded into analysis environment)

type CellValue = number | string | boolean | nil
type Range = {CellValue}

declare excel: {
    SUM: (Range) -> number,
    AVERAGE: (Range) -> number,
    COUNT: (Range) -> number,
    CONCAT: (...string) -> string,
    IF: <T>(boolean, T, T) -> T,
    VLOOKUP: <T>(CellValue, Range, number, boolean?) -> T,
    -- ... more functions
}

declare cell: (string) -> CellValue
declare range: (string, string) -> Range
```

## Relations (Foreign Keys)

A powerful feature enabled by UUID-based cells:

```c
typedef struct RelationValue {
    uuid_t target_sheet;
    uuid_t target_row;        // Links to a specific row (by first cell in row)
    // Display value is looked up dynamically from target
} RelationValue;
```

### Example: Projects & Tasks

**Projects Sheet:**
| ID (auto) | Name | Status |
|-----------|------|--------|
| `uuid-p1` | Website Redesign | Active |
| `uuid-p2` | Mobile App | Planning |

**Tasks Sheet:**
| ID (auto) | Task | Project (relation) | Due Date |
|-----------|------|-------------------|----------|
| `uuid-t1` | Design mockups | → `uuid-p1` | 2024-02-01 |
| `uuid-t2` | API spec | → `uuid-p2` | 2024-02-15 |

The relation stores the UUID, displays the linked Name. If "Website Redesign" is renamed, all linked tasks update automatically.

### Relation Formulas

```
=LOOKUP(Tasks.Project, Projects.Name)     -- Get project name
=COUNTIF(Tasks.Project, @row)             -- Count tasks for this project
=SUMIF(Tasks.Hours, Tasks.Project, @row)  -- Sum hours for this project
```

## File Format for Types

Column constraints are stored in the `.cells` file:

```
[dimensions:sheet-uuid]
d0:axis:col-uuid-a
  prev=
  next=col-uuid-b
  gap=0
  name=Amount
  size=100
  type=number
  type.min=0
  type.max=1000000
  type.precision=2
  type.required=true

d0:axis:col-uuid-b
  prev=col-uuid-a
  next=col-uuid-c
  gap=0
  name=Status
  size=120
  type=select
  type.options=Pending,Approved,Rejected
  type.colors=#FFA500,#00FF00,#FF0000

d0:axis:col-uuid-c
  prev=col-uuid-b
  next=
  gap=0
  name=Project
  size=150
  type=relation
  type.target_sheet=projects-sheet-uuid
  type.target_column=name-col-uuid
```

## API Type Handling

The API respects column types:

```json
POST /api/sheets/{id}/rows
{
  "cells": {
    "Amount": "42",        // Coerced to number 42
    "Status": "Approved",  // Validated against options
    "Project": "uuid-p1"   // Validated as existing relation
  }
}

Response (success):
{
  "row_id": "new-row-uuid",
  "cells": {
    "Amount": 42,          // Stored as number
    "Status": "Approved",
    "Project": {
      "id": "uuid-p1",
      "display": "Website Redesign"
    }
  }
}

Response (validation error):
{
  "error": "VALIDATION_FAILED",
  "details": {
    "Amount": {"code": "OUT_OF_RANGE", "message": "Must be between 0 and 1000000"},
    "Status": {"code": "INVALID_OPTION", "message": "Must be one of: Pending, Approved, Rejected"}
  }
}
```

## Migration: Adding Types to Existing Columns

When a type constraint is added to an existing column with data:

```c
typedef struct MigrationResult {
    int total_cells;
    int valid_cells;
    int coerced_cells;
    int invalid_cells;
    CellError* errors;        // Details of invalid cells
} MigrationResult;

MigrationResult migrate_column_type(Sheet* sheet, uuid_t col_id,
                                     ColumnConstraints* new_constraints) {
    MigrationResult result = {0};

    CellIterator* it = column_cells(sheet, col_id);
    Cell* cell;
    while ((cell = cell_iterator_next(it))) {
        result.total_cells++;

        ValidationResult vr = validate_cell_value(cell->value, new_constraints);
        if (vr.valid) {
            if (vr.coerced_value.type != cell->value.type) {
                // Value was coerced - update it
                cell_set_value(sheet, cell->id, vr.coerced_value);
                result.coerced_cells++;
            }
            result.valid_cells++;
        } else {
            // Invalid - don't change, record error
            result.invalid_cells++;
            record_error(&result, cell->id, vr.error_message);
        }
    }

    return result;
}
```

UI flow:
1. User sets column type to NUMBER
2. System scans column: "47 cells valid, 3 cells cannot be converted"
3. User reviews invalid cells, fixes or proceeds
4. If proceeding with invalid cells: they become #TYPE! errors or are cleared

## Summary

| Layer | When | What |
|-------|------|------|
| Cell storage | Always | Raw value with actual type |
| Column validation | On write | Type checking + constraints |
| Formula hints | On compile | Luau type annotations for optimization |
| Runtime formulas | On execute | Dynamic, Excel-compatible behavior |

This gives us:
- **Excel compatibility**: Formulas work as expected
- **AirTable power**: Structured data with validation
- **Performance**: Luau type hints enable faster bytecode
- **Safety**: Optional constraints catch errors early
