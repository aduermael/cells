// =============================================================================
// Cells XLSX C API (Swift-callable)
// =============================================================================
//
// Thin C ABI over the core XLSX reader/writer for use from native iOS apps
// (Swift/Objective-C). Excel format logic lives in //core/cells only.
//
// Minimal Swift usage (link the static library / framework that exposes this):
//
//   let wb = cells_xlsx_open("/path/to/file.xlsx")
//   guard wb != nil else { print(String(cString: cells_xlsx_last_error()!)); return }
//   defer { cells_xlsx_close(wb) }
//   let n = cells_xlsx_sheet_count(wb)
//   var buf = [CChar](repeating: 0, count: 256)
//   cells_xlsx_sheet_name(wb, 0, &buf, buf.count)
//   let name = String(cString: buf)
//   if cells_xlsx_get_type(wb, 0, 0, 0) == CELLS_XLSX_VALUE_STRING {
//       if let p = cells_xlsx_get_string(wb, 0, 0, 0) {
//           print(String(cString: p))
//       }
//   }
//   cells_xlsx_write(wb, "/path/to/out.xlsx")
//
// Positions are 0-based (col 0 = A, row 0 = first row).
//
// =============================================================================

#ifndef CELLS_XLSX_C_API_H_
#define CELLS_XLSX_C_API_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque workbook handle owned by the caller; free with cells_xlsx_close.
typedef struct CellsXlsxWorkbook CellsXlsxWorkbook;

// Cell value kinds returned by cells_xlsx_get_type.
typedef enum CellsXlsxValueType {
    CELLS_XLSX_VALUE_EMPTY = 0,
    CELLS_XLSX_VALUE_NUMBER = 1,
    CELLS_XLSX_VALUE_STRING = 2,
    CELLS_XLSX_VALUE_BOOL = 3,
    CELLS_XLSX_VALUE_OTHER = 4,  // error, formula unevaluated, date, etc.
} CellsXlsxValueType;

// Thread-local last error message (never NULL; empty string if no error).
const char* cells_xlsx_last_error(void);

// Open an existing .xlsx from a filesystem path. Returns NULL on failure.
CellsXlsxWorkbook* cells_xlsx_open(const char* path);

// Open an existing .xlsx from an in-memory buffer. Returns NULL on failure.
CellsXlsxWorkbook* cells_xlsx_open_bytes(const char* data, size_t size);

// Create an empty workbook with one sheet named "Sheet1". Returns NULL on failure.
CellsXlsxWorkbook* cells_xlsx_create(void);

// Release a workbook handle (no-op if NULL).
void cells_xlsx_close(CellsXlsxWorkbook* wb);

// Number of sheets, or -1 on invalid handle.
int cells_xlsx_sheet_count(const CellsXlsxWorkbook* wb);

// Copy sheet name into buf (NUL-terminated if buf_size > 0).
// Returns bytes written excluding NUL, or -1 on error.
int cells_xlsx_sheet_name(const CellsXlsxWorkbook* wb, int sheet_index, char* buf,
                          size_t buf_size);

// Value type at (sheet_index, col, row). EMPTY if missing. OTHER on bad args.
int cells_xlsx_get_type(const CellsXlsxWorkbook* wb, int sheet_index, int col, int row);

// Number value (0 if empty/non-numeric). Check type first for real zeros.
double cells_xlsx_get_number(const CellsXlsxWorkbook* wb, int sheet_index, int col, int row);

// Boolean value (0/1). Non-bool cells return 0.
int cells_xlsx_get_bool(const CellsXlsxWorkbook* wb, int sheet_index, int col, int row);

// String (or raw text) for the cell. Pointer valid until the next get_string /
// mutating call on this workbook, or close. Empty string for empty cells.
// NULL on invalid handle/args.
const char* cells_xlsx_get_string(CellsXlsxWorkbook* wb, int sheet_index, int col, int row);

// Set cell values (creates axes/cells as needed). Returns 0 on success, -1 on error.
int cells_xlsx_set_number(CellsXlsxWorkbook* wb, int sheet_index, int col, int row, double value);
int cells_xlsx_set_string(CellsXlsxWorkbook* wb, int sheet_index, int col, int row,
                          const char* value);
int cells_xlsx_set_bool(CellsXlsxWorkbook* wb, int sheet_index, int col, int row, int value);

// Write workbook to path as .xlsx. Returns 0 on success, -1 on failure.
int cells_xlsx_write(const CellsXlsxWorkbook* wb, const char* path);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CELLS_XLSX_C_API_H_
