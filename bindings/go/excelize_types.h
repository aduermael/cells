// excelize_types.h - C-compatible data structures for XLSX parsing
// This file defines the interface between Go (excelize) and C++ (cells)

#ifndef CELLS_BINDINGS_GO_EXCELIZE_TYPES_H_
#define CELLS_BINDINGS_GO_EXCELIZE_TYPES_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Cell type constants
typedef enum {
    XLSX_CELL_TYPE_EMPTY = 0,
    XLSX_CELL_TYPE_STRING = 1,
    XLSX_CELL_TYPE_NUMBER = 2,
    XLSX_CELL_TYPE_BOOL = 3,
    XLSX_CELL_TYPE_ERROR = 4,
    XLSX_CELL_TYPE_DATE = 5,
} XLSXCellType;

// A single cell in a spreadsheet
typedef struct {
    int row;                // 0-indexed row number
    int col;                // 0-indexed column number
    char* value;            // String representation of the value
    char* formula;          // Formula text (without leading =), NULL if not a formula
    XLSXCellType cell_type; // Type of the cell value
} XLSXCell;

// Column dimension info
typedef struct {
    int col;                // 0-indexed column number
    double width;           // Column width in Excel units
    int hidden;             // 1 if column is hidden, 0 otherwise
} XLSXColDim;

// Row dimension info
typedef struct {
    int row;                // 0-indexed row number
    double height;          // Row height in points
    int hidden;             // 1 if row is hidden, 0 otherwise
} XLSXRowDim;

// A single sheet in a workbook
typedef struct {
    char* name;             // Sheet name (owned)
    XLSXCell* cells;        // Array of cells (owned)
    int cell_count;         // Number of cells in the array
    int row_count;          // Total rows (max row index + 1)
    int col_count;          // Total columns (max col index + 1)
    XLSXColDim* col_dims;   // Column dimensions (owned), NULL if none
    int col_dim_count;      // Number of column dimension entries
    XLSXRowDim* row_dims;   // Row dimensions (owned), NULL if none
    int row_dim_count;      // Number of row dimension entries
} XLSXSheet;

// A complete workbook
typedef struct {
    XLSXSheet* sheets;      // Array of sheets (owned)
    int sheet_count;        // Number of sheets
} XLSXData;

// ============================================================================
// C API Functions (implemented in Go with CGO exports)
// ============================================================================

// Parse an XLSX file and return all data.
// The file is opened, all data is extracted, then the file is closed.
// Returns NULL on error, and sets *error_out to an error message.
// The caller must call XLSXDataFree() on the returned data when done.
// The caller must call XLSXErrorFree() on the error message if not NULL.
XLSXData* ExcelizeParseXLSX(const char* path, char** error_out);

// Write an XLSX file from the provided data.
// A new file is created, populated with the data, saved, and closed.
// Returns 0 on success, -1 on error with *error_out set to an error message.
// The caller must call XLSXErrorFree() on the error message if not NULL.
int ExcelizeWriteXLSX(const char* path, const XLSXData* data, char** error_out);

// Free XLSXData and all its contents
void XLSXDataFree(XLSXData* data);

// Free an error string returned by parse/write functions
void XLSXErrorFree(char* error);

#ifdef __cplusplus
}
#endif

#endif  // CELLS_BINDINGS_GO_EXCELIZE_TYPES_H_
