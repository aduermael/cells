package main

/*
#include <stdlib.h>
#include <string.h>

// Cell type constants
typedef enum {
    XLSX_CELL_TYPE_EMPTY = 0,
    XLSX_CELL_TYPE_STRING = 1,
    XLSX_CELL_TYPE_NUMBER = 2,
    XLSX_CELL_TYPE_BOOL = 3,
    XLSX_CELL_TYPE_ERROR = 4,
    XLSX_CELL_TYPE_DATE = 5,
} XLSXCellType;

typedef struct {
    int row;
    int col;
    char* value;
    char* formula;
    XLSXCellType cell_type;
} XLSXCell;

typedef struct {
    int col;
    double width;
    int hidden;
} XLSXColDim;

typedef struct {
    int row;
    double height;
    int hidden;
} XLSXRowDim;

typedef struct {
    char* name;
    XLSXCell* cells;
    int cell_count;
    int row_count;
    int col_count;
    XLSXColDim* col_dims;
    int col_dim_count;
    XLSXRowDim* row_dims;
    int row_dim_count;
} XLSXSheet;

typedef struct {
    XLSXSheet* sheets;
    int sheet_count;
} XLSXData;
*/
import "C"
import (
	"strconv"
	"strings"
	"unsafe"

	"github.com/xuri/excelize/v2"
)

// ExcelizeParseXLSX parses an XLSX file and returns all data as C structs.
// The file is opened, data extracted, then closed immediately.
//
//export ExcelizeParseXLSX
func ExcelizeParseXLSX(path *C.char, errorOut **C.char) *C.XLSXData {
	goPath := C.GoString(path)

	f, err := excelize.OpenFile(goPath)
	if err != nil {
		*errorOut = C.CString(err.Error())
		return nil
	}
	defer f.Close()

	sheetList := f.GetSheetList()
	if len(sheetList) == 0 {
		*errorOut = C.CString("no sheets found in workbook")
		return nil
	}

	// Allocate XLSXData
	data := (*C.XLSXData)(C.malloc(C.sizeof_XLSXData))
	data.sheet_count = C.int(len(sheetList))
	data.sheets = (*C.XLSXSheet)(C.malloc(C.size_t(len(sheetList)) * C.sizeof_XLSXSheet))

	// Convert sheets pointer to Go slice for easier indexing
	sheets := unsafe.Slice(data.sheets, len(sheetList))

	for i, sheetName := range sheetList {
		sheet := &sheets[i]
		sheet.name = C.CString(sheetName)

		// Get all rows (includes merged cells and formulas)
		rows, err := f.GetRows(sheetName)
		if err != nil {
			// Clean up and return error
			freePartialData(data, i)
			*errorOut = C.CString("failed to get rows from sheet " + sheetName + ": " + err.Error())
			return nil
		}

		// Collect cells
		var cells []cellData
		maxRow := 0
		maxCol := 0

		for rowIdx, row := range rows {
			if rowIdx+1 > maxRow {
				maxRow = rowIdx + 1
			}
			for colIdx, cellValue := range row {
				if colIdx+1 > maxCol {
					maxCol = colIdx + 1
				}
				if cellValue == "" {
					continue // Skip empty cells
				}

				// Get cell reference (A1 notation)
				cellRef, _ := excelize.CoordinatesToCellName(colIdx+1, rowIdx+1)

				// Get formula if any
				formula, _ := f.GetCellFormula(sheetName, cellRef)

				// Determine cell type
				cellType := determineCellType(f, sheetName, cellRef, cellValue)

				cells = append(cells, cellData{
					row:      rowIdx,
					col:      colIdx,
					value:    cellValue,
					formula:  formula,
					cellType: cellType,
				})
			}
		}

		// Allocate and populate C cells array
		sheet.cell_count = C.int(len(cells))
		sheet.row_count = C.int(maxRow)
		sheet.col_count = C.int(maxCol)

		if len(cells) > 0 {
			sheet.cells = (*C.XLSXCell)(C.malloc(C.size_t(len(cells)) * C.sizeof_XLSXCell))
			cCells := unsafe.Slice(sheet.cells, len(cells))
			for j, cell := range cells {
				cCells[j].row = C.int(cell.row)
				cCells[j].col = C.int(cell.col)
				cCells[j].value = C.CString(cell.value)
				if cell.formula != "" {
					cCells[j].formula = C.CString(cell.formula)
				} else {
					cCells[j].formula = nil
				}
				cCells[j].cell_type = C.XLSXCellType(cell.cellType)
			}
		} else {
			sheet.cells = nil
		}

		// Get column dimensions
		colDims := getColumnDimensions(f, sheetName, maxCol)
		sheet.col_dim_count = C.int(len(colDims))
		if len(colDims) > 0 {
			sheet.col_dims = (*C.XLSXColDim)(C.malloc(C.size_t(len(colDims)) * C.sizeof_XLSXColDim))
			cColDims := unsafe.Slice(sheet.col_dims, len(colDims))
			for j, dim := range colDims {
				cColDims[j].col = C.int(dim.col)
				cColDims[j].width = C.double(dim.width)
				cColDims[j].hidden = boolToInt(dim.hidden)
			}
		} else {
			sheet.col_dims = nil
		}

		// Get row dimensions
		rowDims := getRowDimensions(f, sheetName, maxRow)
		sheet.row_dim_count = C.int(len(rowDims))
		if len(rowDims) > 0 {
			sheet.row_dims = (*C.XLSXRowDim)(C.malloc(C.size_t(len(rowDims)) * C.sizeof_XLSXRowDim))
			cRowDims := unsafe.Slice(sheet.row_dims, len(rowDims))
			for j, dim := range rowDims {
				cRowDims[j].row = C.int(dim.row)
				cRowDims[j].height = C.double(dim.height)
				cRowDims[j].hidden = boolToInt(dim.hidden)
			}
		} else {
			sheet.row_dims = nil
		}
	}

	return data
}

// Helper types
type cellData struct {
	row      int
	col      int
	value    string
	formula  string
	cellType int
}

type colDimData struct {
	col    int
	width  float64
	hidden bool
}

type rowDimData struct {
	row    int
	height float64
	hidden bool
}

// determineCellType determines the type of a cell value
func determineCellType(f *excelize.File, sheet, cellRef, value string) int {
	cellType, err := f.GetCellType(sheet, cellRef)
	if err != nil {
		return 0 // XLSX_CELL_TYPE_EMPTY
	}

	switch cellType {
	case excelize.CellTypeUnset, excelize.CellTypeInlineString, excelize.CellTypeSharedString:
		return 1 // XLSX_CELL_TYPE_STRING
	case excelize.CellTypeNumber:
		// Check if it's a date by looking at the cell style/format
		// For now, return number - date detection is complex
		return 2 // XLSX_CELL_TYPE_NUMBER
	case excelize.CellTypeBool:
		return 3 // XLSX_CELL_TYPE_BOOL
	case excelize.CellTypeError:
		return 4 // XLSX_CELL_TYPE_ERROR
	case excelize.CellTypeDate:
		return 5 // XLSX_CELL_TYPE_DATE
	case excelize.CellTypeFormula:
		// Formula cells - determine type from value
		if _, err := strconv.ParseFloat(value, 64); err == nil {
			return 2 // XLSX_CELL_TYPE_NUMBER
		}
		if strings.ToUpper(value) == "TRUE" || strings.ToUpper(value) == "FALSE" {
			return 3 // XLSX_CELL_TYPE_BOOL
		}
		if strings.HasPrefix(value, "#") {
			return 4 // XLSX_CELL_TYPE_ERROR
		}
		return 1 // XLSX_CELL_TYPE_STRING
	default:
		return 1 // XLSX_CELL_TYPE_STRING
	}
}

// getColumnDimensions gets column width info
func getColumnDimensions(f *excelize.File, sheet string, maxCol int) []colDimData {
	var dims []colDimData

	cols, err := f.GetCols(sheet)
	if err != nil {
		return dims
	}

	// Get column widths for columns that have data
	for colIdx := 0; colIdx < len(cols) && colIdx < maxCol; colIdx++ {
		colName, _ := excelize.ColumnNumberToName(colIdx + 1)
		width, err := f.GetColWidth(sheet, colName)
		if err != nil {
			continue
		}
		// Only record non-default widths (default is typically 8.43)
		if width != 0 && width != 8.43 {
			visible, _ := f.GetColVisible(sheet, colName)
			dims = append(dims, colDimData{
				col:    colIdx,
				width:  width,
				hidden: !visible,
			})
		}
	}

	return dims
}

// getRowDimensions gets row height info
func getRowDimensions(f *excelize.File, sheet string, maxRow int) []rowDimData {
	var dims []rowDimData

	for rowIdx := 1; rowIdx <= maxRow; rowIdx++ {
		height, err := f.GetRowHeight(sheet, rowIdx)
		if err != nil {
			continue
		}
		// Only record non-default heights (default is typically 15)
		if height != 0 && height != 15 {
			visible, _ := f.GetRowVisible(sheet, rowIdx)
			dims = append(dims, rowDimData{
				row:    rowIdx - 1, // Convert to 0-indexed
				height: height,
				hidden: !visible,
			})
		}
	}

	return dims
}

// boolToInt converts bool to C int
func boolToInt(b bool) C.int {
	if b {
		return 1
	}
	return 0
}

// freePartialData frees a partially constructed XLSXData on error
func freePartialData(data *C.XLSXData, sheetsAllocated int) {
	if data == nil {
		return
	}
	if data.sheets != nil {
		sheets := unsafe.Slice(data.sheets, sheetsAllocated)
		for i := 0; i < sheetsAllocated; i++ {
			freeSheet(&sheets[i])
		}
		C.free(unsafe.Pointer(data.sheets))
	}
	C.free(unsafe.Pointer(data))
}

// freeSheet frees a single XLSXSheet
func freeSheet(sheet *C.XLSXSheet) {
	if sheet.name != nil {
		C.free(unsafe.Pointer(sheet.name))
	}
	if sheet.cells != nil {
		cells := unsafe.Slice(sheet.cells, int(sheet.cell_count))
		for i := 0; i < int(sheet.cell_count); i++ {
			if cells[i].value != nil {
				C.free(unsafe.Pointer(cells[i].value))
			}
			if cells[i].formula != nil {
				C.free(unsafe.Pointer(cells[i].formula))
			}
		}
		C.free(unsafe.Pointer(sheet.cells))
	}
	if sheet.col_dims != nil {
		C.free(unsafe.Pointer(sheet.col_dims))
	}
	if sheet.row_dims != nil {
		C.free(unsafe.Pointer(sheet.row_dims))
	}
}

// XLSXDataFree frees XLSXData and all its contents.
// This is called from C++ after converting data to the native model.
//
//export XLSXDataFree
func XLSXDataFree(data *C.XLSXData) {
	if data == nil {
		return
	}
	if data.sheets != nil {
		sheets := unsafe.Slice(data.sheets, int(data.sheet_count))
		for i := 0; i < int(data.sheet_count); i++ {
			freeSheet(&sheets[i])
		}
		C.free(unsafe.Pointer(data.sheets))
	}
	C.free(unsafe.Pointer(data))
}

// XLSXErrorFree frees an error string.
//
//export XLSXErrorFree
func XLSXErrorFree(errStr *C.char) {
	if errStr != nil {
		C.free(unsafe.Pointer(errStr))
	}
}

// ExcelizeWriteXLSX writes an XLSX file from the provided C structs.
// A new file is created, populated, saved, and closed immediately.
//
//export ExcelizeWriteXLSX
func ExcelizeWriteXLSX(path *C.char, data *C.XLSXData, errorOut **C.char) C.int {
	if data == nil {
		*errorOut = C.CString("data is nil")
		return -1
	}

	goPath := C.GoString(path)
	f := excelize.NewFile()
	defer f.Close()

	sheetCount := int(data.sheet_count)
	if sheetCount == 0 {
		*errorOut = C.CString("no sheets in data")
		return -1
	}

	sheets := unsafe.Slice(data.sheets, sheetCount)

	// Create sheets and populate them
	for i := 0; i < sheetCount; i++ {
		sheet := &sheets[i]
		sheetName := C.GoString(sheet.name)

		// First sheet already exists as "Sheet1", rename it
		// Additional sheets need to be created
		if i == 0 {
			f.SetSheetName("Sheet1", sheetName)
		} else {
			_, err := f.NewSheet(sheetName)
			if err != nil {
				*errorOut = C.CString("failed to create sheet " + sheetName + ": " + err.Error())
				return -1
			}
		}

		// Write cells
		if sheet.cell_count > 0 && sheet.cells != nil {
			cells := unsafe.Slice(sheet.cells, int(sheet.cell_count))
			for j := 0; j < int(sheet.cell_count); j++ {
				cell := &cells[j]
				row := int(cell.row) + 1  // Convert to 1-indexed
				col := int(cell.col) + 1  // Convert to 1-indexed
				cellRef, _ := excelize.CoordinatesToCellName(col, row)

				// Check if this is a formula cell
				if cell.formula != nil {
					formula := C.GoString(cell.formula)
					if err := f.SetCellFormula(sheetName, cellRef, formula); err != nil {
						// Log error but continue - formula might be invalid
						continue
					}
				} else if cell.value != nil {
					value := C.GoString(cell.value)
					// Set value based on type
					switch cell.cell_type {
					case C.XLSX_CELL_TYPE_NUMBER:
						// Try to parse as number
						if floatVal, err := strconv.ParseFloat(value, 64); err == nil {
							f.SetCellValue(sheetName, cellRef, floatVal)
						} else if intVal, err := strconv.ParseInt(value, 10, 64); err == nil {
							f.SetCellValue(sheetName, cellRef, intVal)
						} else {
							f.SetCellValue(sheetName, cellRef, value)
						}
					case C.XLSX_CELL_TYPE_BOOL:
						boolVal := strings.ToUpper(value) == "TRUE" || value == "1"
						f.SetCellValue(sheetName, cellRef, boolVal)
					case C.XLSX_CELL_TYPE_ERROR:
						// Write error as string
						f.SetCellValue(sheetName, cellRef, value)
					case C.XLSX_CELL_TYPE_DATE:
						// Try to parse as date, fallback to string
						f.SetCellValue(sheetName, cellRef, value)
					default:
						// String or empty - write as is
						f.SetCellValue(sheetName, cellRef, value)
					}
				}
			}
		}

		// Set column widths
		if sheet.col_dim_count > 0 && sheet.col_dims != nil {
			colDims := unsafe.Slice(sheet.col_dims, int(sheet.col_dim_count))
			for j := 0; j < int(sheet.col_dim_count); j++ {
				dim := &colDims[j]
				colName, _ := excelize.ColumnNumberToName(int(dim.col) + 1)
				f.SetColWidth(sheetName, colName, colName, float64(dim.width))
				if dim.hidden != 0 {
					f.SetColVisible(sheetName, colName, false)
				}
			}
		}

		// Set row heights
		if sheet.row_dim_count > 0 && sheet.row_dims != nil {
			rowDims := unsafe.Slice(sheet.row_dims, int(sheet.row_dim_count))
			for j := 0; j < int(sheet.row_dim_count); j++ {
				dim := &rowDims[j]
				rowNum := int(dim.row) + 1 // Convert to 1-indexed
				f.SetRowHeight(sheetName, rowNum, float64(dim.height))
				if dim.hidden != 0 {
					f.SetRowVisible(sheetName, rowNum, false)
				}
			}
		}
	}

	// Save the file
	if err := f.SaveAs(goPath); err != nil {
		*errorOut = C.CString("failed to save file: " + err.Error())
		return -1
	}

	return 0
}

func main() {}
