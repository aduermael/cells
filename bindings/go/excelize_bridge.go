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

typedef struct {
    int read_formulas;
    int read_dimensions;
} XLSXParseOptions;
*/
import "C"
import (
	"archive/zip"
	"encoding/xml"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
	"time"
	"unsafe"

	"github.com/xuri/excelize/v2"
)

// Set to true to enable timing output
var debugTiming = os.Getenv("CELLS_DEBUG_TIMING") != ""

func logTiming(stage string, start time.Time) {
	if debugTiming {
		fmt.Fprintf(os.Stderr, "[timing] %s: %v\n", stage, time.Since(start))
	}
}

// ExcelizeParseXLSX parses an XLSX file and returns all data as C structs.
// The file is opened, data extracted, then closed immediately.
//
//export ExcelizeParseXLSX
func ExcelizeParseXLSX(path *C.char, errorOut **C.char) *C.XLSXData {
	// Default options: read everything
	opts := C.XLSXParseOptions{
		read_formulas:   1,
		read_dimensions: 1,
	}
	return ExcelizeParseXLSXWithOptions(path, &opts, errorOut)
}

// ExcelizeParseXLSXWithOptions parses an XLSX file with performance options.
// Set read_formulas=0 to skip formula reading (much faster for large files).
// Set read_dimensions=0 to skip row/column dimension reading.
//
//export ExcelizeParseXLSXWithOptions
func ExcelizeParseXLSXWithOptions(path *C.char, options *C.XLSXParseOptions, errorOut **C.char) *C.XLSXData {
	totalStart := time.Now()
	goPath := C.GoString(path)

	// Parse options
	readFormulas := options == nil || options.read_formulas != 0
	readDimensions := options == nil || options.read_dimensions != 0

	// Open ZIP once and keep it open for all operations
	start := time.Now()
	zipReader, err := zip.OpenReader(goPath)
	if err != nil {
		*errorOut = C.CString(err.Error())
		return nil
	}
	defer zipReader.Close()
	logTiming("zip.OpenReader", start)

	// Get sheet info from workbook.xml (no excelize needed!)
	start = time.Now()
	sheetInfos, err := parseWorkbook(zipReader)
	if err != nil {
		*errorOut = C.CString("failed to parse workbook: " + err.Error())
		return nil
	}
	if len(sheetInfos) == 0 {
		*errorOut = C.CString("no sheets found in workbook")
		return nil
	}
	logTiming("parseWorkbook", start)

	// Load shared strings once for the entire workbook
	start = time.Now()
	sharedStrings, err := parseSharedStringsFromZip(zipReader)
	if err != nil {
		*errorOut = C.CString("failed to read shared strings: " + err.Error())
		return nil
	}
	logTiming("parseSharedStrings", start)

	// Allocate XLSXData
	data := (*C.XLSXData)(C.malloc(C.sizeof_XLSXData))
	data.sheet_count = C.int(len(sheetInfos))
	data.sheets = (*C.XLSXSheet)(C.malloc(C.size_t(len(sheetInfos)) * C.sizeof_XLSXSheet))

	// Convert sheets pointer to Go slice for easier indexing
	xlsxSheets := unsafe.Slice(data.sheets, len(sheetInfos))

	for i, info := range sheetInfos {
		sheet := &xlsxSheets[i]
		sheet.name = C.CString(info.name)

		// Parse worksheet XML directly from the already-open ZIP
		start = time.Now()
		cells, maxRow, maxCol, err := parseSheetFromZipReader(zipReader, info.xmlPath, sharedStrings, readFormulas)
		logTiming("parseSheet", start)
		if err != nil {
			freePartialData(data, i)
			*errorOut = C.CString("failed to parse sheet " + info.name + ": " + err.Error())
			return nil
		}

		// Allocate and populate C cells array
		start = time.Now()
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
		logTiming("buildCStructs", start)

		// Dimensions not supported in fast path (would need excelize)
		if readDimensions {
			// TODO: Parse dimensions from worksheet XML if needed
		}
		sheet.col_dims = nil
		sheet.col_dim_count = 0
		sheet.row_dims = nil
		sheet.row_dim_count = 0
	}

	logTiming("TOTAL", totalStart)
	return data
}

// XML structures for direct worksheet parsing
type xlsxWorksheetData struct {
	SheetData xlsxSheetData `xml:"sheetData"`
}

type xlsxSheetData struct {
	Row []xlsxRow `xml:"row"`
}

type xlsxRow struct {
	R int      `xml:"r,attr"` // Row number (1-indexed)
	C []xlsxC  `xml:"c"`      // Cells
}

type xlsxC struct {
	R string  `xml:"r,attr"` // Cell reference (A1)
	T string  `xml:"t,attr"` // Type: s=shared string, n=number, b=bool, e=error, str=formula string
	S int     `xml:"s,attr"` // Style index
	V string  `xml:"v"`      // Value
	F *xlsxF  `xml:"f"`      // Formula
}

type xlsxF struct {
	Content string `xml:",chardata"`
	T       string `xml:"t,attr"` // shared, array, dataTable
	Ref     string `xml:"ref,attr"`
	Si      int    `xml:"si,attr"`
}

// sheetInfo holds sheet name and XML path
type sheetInfo struct {
	name    string
	xmlPath string
}

// parseWorkbook extracts sheet names and their XML paths from workbook.xml
func parseWorkbook(zipReader *zip.ReadCloser) ([]sheetInfo, error) {
	// First, get sheet-to-file mappings from _rels/workbook.xml.rels
	relMap := make(map[string]string) // rId -> target path
	for _, f := range zipReader.File {
		if f.Name == "xl/_rels/workbook.xml.rels" {
			rc, err := f.Open()
			if err != nil {
				return nil, err
			}
			decoder := xml.NewDecoder(rc)
			for {
				token, err := decoder.Token()
				if err == io.EOF {
					break
				}
				if err != nil {
					rc.Close()
					return nil, err
				}
				if se, ok := token.(xml.StartElement); ok && se.Name.Local == "Relationship" {
					var id, target string
					for _, attr := range se.Attr {
						if attr.Name.Local == "Id" {
							id = attr.Value
						} else if attr.Name.Local == "Target" {
							target = attr.Value
						}
					}
					if id != "" && target != "" {
						relMap[id] = target
					}
				}
			}
			rc.Close()
			break
		}
	}

	// Now parse workbook.xml to get sheet names and their rIds
	var sheets []sheetInfo
	for _, f := range zipReader.File {
		if f.Name == "xl/workbook.xml" {
			rc, err := f.Open()
			if err != nil {
				return nil, err
			}
			decoder := xml.NewDecoder(rc)
			for {
				token, err := decoder.Token()
				if err == io.EOF {
					break
				}
				if err != nil {
					rc.Close()
					return nil, err
				}
				if se, ok := token.(xml.StartElement); ok && se.Name.Local == "sheet" {
					var name, rId string
					for _, attr := range se.Attr {
						if attr.Name.Local == "name" {
							name = attr.Value
						} else if attr.Name.Local == "id" {
							rId = attr.Value
						}
					}
					if name != "" {
						xmlPath := "xl/worksheets/sheet1.xml" // Default
						if target, ok := relMap[rId]; ok {
							// Target is relative to xl/ directory
							if strings.HasPrefix(target, "/") {
								xmlPath = target[1:]
							} else {
								xmlPath = "xl/" + target
							}
						}
						sheets = append(sheets, sheetInfo{name: name, xmlPath: xmlPath})
					}
				}
			}
			rc.Close()
			break
		}
	}

	return sheets, nil
}

// parseSharedStringsFromZip parses shared strings from an open ZIP reader
func parseSharedStringsFromZip(zipReader *zip.ReadCloser) ([]string, error) {
	for _, f := range zipReader.File {
		if f.Name == "xl/sharedStrings.xml" {
			rc, err := f.Open()
			if err != nil {
				return nil, err
			}
			defer rc.Close()

			var result []string
			decoder := xml.NewDecoder(rc)
			var inSI, inT bool
			var currentString strings.Builder

			for {
				token, err := decoder.Token()
				if err == io.EOF {
					break
				}
				if err != nil {
					return nil, err
				}

				switch t := token.(type) {
				case xml.StartElement:
					if t.Name.Local == "si" {
						inSI = true
						currentString.Reset()
					} else if inSI && t.Name.Local == "t" {
						inT = true
					}
				case xml.EndElement:
					if t.Name.Local == "si" {
						result = append(result, currentString.String())
						inSI = false
					} else if t.Name.Local == "t" {
						inT = false
					}
				case xml.CharData:
					if inT {
						currentString.Write(t)
					}
				}
			}
			return result, nil
		}
	}
	return nil, nil // No shared strings file
}

// parseSheetFromZipReader parses a worksheet from an already-open ZIP reader
// Uses manual token-by-token parsing for maximum performance
func parseSheetFromZipReader(zipReader *zip.ReadCloser, sheetXMLPath string, sharedStrings []string, readFormulas bool) ([]cellData, int, int, error) {
	// Find the worksheet file
	var sheetFile *zip.File
	for _, f := range zipReader.File {
		if f.Name == sheetXMLPath {
			sheetFile = f
			break
		}
	}
	if sheetFile == nil {
		return nil, 0, 0, nil // No data
	}

	rc, err := sheetFile.Open()
	if err != nil {
		return nil, 0, 0, err
	}
	defer rc.Close()

	// Pre-allocate cells slice (estimate based on file size)
	estimatedCells := int(sheetFile.UncompressedSize64 / 100)
	if estimatedCells < 1000 {
		estimatedCells = 1000
	}
	cells := make([]cellData, 0, estimatedCells)
	maxRow := 0
	maxCol := 0

	// Parse using streaming XML decoder - manual token parsing (faster than DecodeElement)
	decoder := xml.NewDecoder(rc)

	// State machine for parsing
	inSheetData := false
	inRow := false
	inCell := false
	inValue := false
	inFormula := false

	var currentRow int
	var cellRef, cellType, cellValue, cellFormula string

	for {
		token, err := decoder.Token()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, 0, 0, err
		}

		switch t := token.(type) {
		case xml.StartElement:
			switch t.Name.Local {
			case "sheetData":
				inSheetData = true
			case "row":
				if inSheetData {
					inRow = true
					currentRow = 0
					for _, attr := range t.Attr {
						if attr.Name.Local == "r" {
							currentRow, _ = strconv.Atoi(attr.Value)
						}
					}
					if currentRow > maxRow {
						maxRow = currentRow
					}
				}
			case "c":
				if inRow {
					inCell = true
					cellRef = ""
					cellType = ""
					cellValue = ""
					cellFormula = ""
					for _, attr := range t.Attr {
						switch attr.Name.Local {
						case "r":
							cellRef = attr.Value
						case "t":
							cellType = attr.Value
						}
					}
				}
			case "v":
				if inCell {
					inValue = true
				}
			case "f":
				if inCell && readFormulas {
					inFormula = true
				}
			}
		case xml.EndElement:
			switch t.Name.Local {
			case "sheetData":
				inSheetData = false
			case "row":
				inRow = false
			case "c":
				if inCell {
					// Process completed cell
					col, rowNum := parseCellRef(cellRef)
					if col+1 > maxCol {
						maxCol = col + 1
					}

					value := cellValue
					if cellType == "s" && value != "" {
						idx, err := strconv.Atoi(value)
						if err == nil && idx >= 0 && idx < len(sharedStrings) {
							value = sharedStrings[idx]
						}
					}

					if value != "" || cellFormula != "" {
						cType := mapXMLCellType(cellType, value)
						cells = append(cells, cellData{
							row:      rowNum,
							col:      col,
							value:    value,
							formula:  cellFormula,
							cellType: cType,
						})
					}
					inCell = false
				}
			case "v":
				inValue = false
			case "f":
				inFormula = false
			}
		case xml.CharData:
			if inValue {
				cellValue = string(t)
			} else if inFormula {
				cellFormula = string(t)
			}
		}
	}

	return cells, maxRow, maxCol, nil
}

// parseSheetFromZip parses a worksheet XML directly from a ZIP file (legacy)
// Returns cells, maxRow, maxCol, error
func parseSheetFromZip(zipPath, sheetXMLPath string, sharedStrings []string, readFormulas bool) ([]cellData, int, int, error) {
	r, err := zip.OpenReader(zipPath)
	if err != nil {
		return nil, 0, 0, err
	}
	defer r.Close()

	// Find the worksheet file
	var sheetFile *zip.File
	for _, f := range r.File {
		if f.Name == sheetXMLPath {
			sheetFile = f
			break
		}
	}
	if sheetFile == nil {
		return nil, 0, 0, nil // No data
	}

	rc, err := sheetFile.Open()
	if err != nil {
		return nil, 0, 0, err
	}
	defer rc.Close()

	// Parse only sheetData section using streaming XML decoder
	decoder := xml.NewDecoder(rc)
	var cells []cellData
	maxRow := 0
	maxCol := 0

	// Find sheetData element
	for {
		token, err := decoder.Token()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, 0, 0, err
		}

		if se, ok := token.(xml.StartElement); ok && se.Name.Local == "sheetData" {
			// Parse rows within sheetData
			for {
				token, err := decoder.Token()
				if err != nil {
					return nil, 0, 0, err
				}

				// Check for end of sheetData
				if ee, ok := token.(xml.EndElement); ok && ee.Name.Local == "sheetData" {
					break
				}

				// Process row elements
				if se, ok := token.(xml.StartElement); ok && se.Name.Local == "row" {
					var row xlsxRow
					if err := decoder.DecodeElement(&row, &se); err != nil {
						return nil, 0, 0, err
					}

					if row.R > maxRow {
						maxRow = row.R
					}

					// Process cells in this row
					for _, c := range row.C {
						// Parse cell reference to get column
						col, rowNum := parseCellRef(c.R)
						if col+1 > maxCol {
							maxCol = col + 1
						}

						// Get value (resolve shared strings)
						value := c.V
						if c.T == "s" && value != "" {
							// Shared string index
							idx, err := strconv.Atoi(value)
							if err == nil && idx >= 0 && idx < len(sharedStrings) {
								value = sharedStrings[idx]
							}
						}

						// Skip empty cells
						if value == "" && c.F == nil {
							continue
						}

						// Determine cell type
						cellType := mapXMLCellType(c.T, value)

						// Get formula if requested
						var formula string
						if readFormulas && c.F != nil {
							formula = c.F.Content
						}

						cells = append(cells, cellData{
							row:      rowNum,
							col:      col,
							value:    value,
							formula:  formula,
							cellType: cellType,
						})
					}
				}
			}
			break // Done with sheetData
		}
	}

	return cells, maxRow, maxCol, nil
}

// parseCellRef parses "A1" -> (col=0, row=0)
// Optimized for hot path - minimal allocations
func parseCellRef(ref string) (col, row int) {
	// Fast path for common cases (A-Z columns)
	if len(ref) >= 2 {
		c := ref[0]
		if c >= 'A' && c <= 'Z' {
			col = int(c - 'A')
			// Check if second char is a digit (single letter column)
			if ref[1] >= '0' && ref[1] <= '9' {
				// Parse row from position 1
				for i := 1; i < len(ref); i++ {
					row = row*10 + int(ref[i]-'0')
				}
				return col, row - 1
			}
			// Two letter column (AA-ZZ)
			if ref[1] >= 'A' && ref[1] <= 'Z' {
				col = (col+1)*26 + int(ref[1]-'A')
				// Parse row from position 2
				for i := 2; i < len(ref); i++ {
					row = row*10 + int(ref[i]-'0')
				}
				return col, row - 1
			}
		}
	}

	// Fallback for edge cases
	col = 0
	row = 0
	i := 0
	for i < len(ref) && ref[i] >= 'A' && ref[i] <= 'Z' {
		col = col*26 + int(ref[i]-'A') + 1
		i++
	}
	col--
	for i < len(ref) && ref[i] >= '0' && ref[i] <= '9' {
		row = row*10 + int(ref[i]-'0')
		i++
	}
	row--
	return col, row
}

// mapXMLCellType maps XML type attribute to our cell type enum
func mapXMLCellType(t, value string) int {
	switch t {
	case "s": // Shared string
		return 1 // XLSX_CELL_TYPE_STRING
	case "str": // Formula string
		return 1 // XLSX_CELL_TYPE_STRING
	case "inlineStr": // Inline string
		return 1 // XLSX_CELL_TYPE_STRING
	case "b": // Boolean
		return 3 // XLSX_CELL_TYPE_BOOL
	case "e": // Error
		return 4 // XLSX_CELL_TYPE_ERROR
	case "n", "": // Number (default type is number)
		// Check if value looks like a number
		if value != "" {
			if _, err := strconv.ParseFloat(value, 64); err == nil {
				return 2 // XLSX_CELL_TYPE_NUMBER
			}
		}
		return 2 // XLSX_CELL_TYPE_NUMBER
	default:
		return 0 // XLSX_CELL_TYPE_EMPTY
	}
}

// getSharedStrings extracts shared strings from an XLSX file using excelize
func getSharedStrings(f *excelize.File, zipPath string) ([]string, error) {
	// Open the ZIP to read shared strings directly
	r, err := zip.OpenReader(zipPath)
	if err != nil {
		return nil, err
	}
	defer r.Close()

	// Find sharedStrings.xml
	var ssFile *zip.File
	for _, file := range r.File {
		if file.Name == "xl/sharedStrings.xml" {
			ssFile = file
			break
		}
	}
	if ssFile == nil {
		return nil, nil // No shared strings
	}

	rc, err := ssFile.Open()
	if err != nil {
		return nil, err
	}
	defer rc.Close()

	// Parse shared strings using streaming
	var result []string
	decoder := xml.NewDecoder(rc)
	var inSI, inT bool
	var currentString strings.Builder

	for {
		token, err := decoder.Token()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, err
		}

		switch t := token.(type) {
		case xml.StartElement:
			if t.Name.Local == "si" {
				inSI = true
				currentString.Reset()
			} else if inSI && t.Name.Local == "t" {
				inT = true
			}
		case xml.EndElement:
			if t.Name.Local == "si" {
				result = append(result, currentString.String())
				inSI = false
			} else if t.Name.Local == "t" {
				inT = false
			}
		case xml.CharData:
			if inT {
				currentString.Write(t)
			}
		}
	}

	return result, nil
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

// inferTypeFromValue attempts to determine cell type from the string value
func inferTypeFromValue(value string) int {
	// Check for boolean
	upper := strings.ToUpper(value)
	if upper == "TRUE" || upper == "FALSE" {
		return 3 // XLSX_CELL_TYPE_BOOL
	}

	// Check for error
	if strings.HasPrefix(value, "#") {
		return 4 // XLSX_CELL_TYPE_ERROR
	}

	// Try to parse as number
	if _, err := strconv.ParseFloat(value, 64); err == nil {
		return 2 // XLSX_CELL_TYPE_NUMBER
	}

	return 1 // XLSX_CELL_TYPE_STRING
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
