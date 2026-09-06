// Package excel drives Microsoft Excel as a workbook host.
//
// OpenSave is implemented on Windows via COM (Excel.Application). Other
// platforms return ErrNotWindows. This is the v1 host used to generate
// Excel goldens; Cells and later test kinds will share the same Host shape.
package excel

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

// DefaultTimeout is the max time for one Excel open+save session.
const DefaultTimeout = 2 * time.Minute

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

// preparePaths resolves absolute paths, requires a .xlsx output, creates the
// output directory, and removes an existing destination (Excel overwrite
// dialogs are unreliable even with DisplayAlerts=false).
func preparePaths(inputPath, outputPath string) (absIn, absOut string, err error) {
	if strings.TrimSpace(inputPath) == "" || strings.TrimSpace(outputPath) == "" {
		return "", "", fmt.Errorf("input and output paths are required")
	}
	absIn, err = filepath.Abs(inputPath)
	if err != nil {
		return "", "", fmt.Errorf("input path: %w", err)
	}
	absOut, err = filepath.Abs(outputPath)
	if err != nil {
		return "", "", fmt.Errorf("output path: %w", err)
	}
	if !strings.EqualFold(filepath.Ext(absOut), ".xlsx") {
		return "", "", fmt.Errorf("output must be .xlsx, got %q", absOut)
	}
	if _, err := os.Stat(absIn); err != nil {
		return "", "", fmt.Errorf("input: %w", err)
	}
	if err := os.MkdirAll(filepath.Dir(absOut), 0o755); err != nil {
		return "", "", fmt.Errorf("output directory: %w", err)
	}
	if !sameFilePath(absIn, absOut) {
		if err := os.Remove(absOut); err != nil && !errors.Is(err, os.ErrNotExist) {
			return "", "", fmt.Errorf("remove existing output: %w", err)
		}
	}
	return absIn, absOut, nil
}

func sameFilePath(a, b string) bool {
	a, b = filepath.Clean(a), filepath.Clean(b)
	if runtime.GOOS == "windows" {
		return strings.EqualFold(a, b)
	}
	return a == b
}
