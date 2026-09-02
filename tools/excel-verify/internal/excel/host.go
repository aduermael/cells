// Package excel drives Microsoft Excel as a workbook host.
//
// OpenSave is implemented on Windows via COM (Excel.Application). Other
// platforms return ErrNotWindows. This is the v1 host used to generate
// Excel goldens; Cells and later test kinds will share the same Host shape.
package excel

import "errors"

// HostID is the golden-metadata host flavor for Windows Excel.
const HostID = "excel-win"

// ErrNotWindows is returned when Excel COM is required off Windows.
var ErrNotWindows = errors.New("excel-save requires Windows + Excel (COM)")

// HostInfo is recorded in golden metadata and printed by `cells-verify version`.
type HostInfo struct {
	ID           string // "excel-win"
	OS           string
	ExcelVersion string
	ExcelBuild   string
}

// Host opens a workbook in Excel and writes it back out.
type Host interface {
	// OpenSave opens inputPath in Excel and SaveAs's an xlsx to outputPath.
	OpenSave(inputPath, outputPath string) error
	// Info reports OS and, when COM works, Excel version/build.
	Info() (HostInfo, error)
	// Available is true when this process can talk to Excel (Windows + ProgID).
	Available() bool
}

// NewHost returns the platform Excel host.
func NewHost() Host {
	return newPlatformHost()
}
