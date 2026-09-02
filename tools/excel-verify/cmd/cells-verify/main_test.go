package main

import (
	"errors"
	"os"
	"strings"
	"testing"

	"cells-verify/internal/excel"
)

func TestRunHelp(t *testing.T) {
	if err := run(nil); err != nil {
		t.Fatalf("run(nil) = %v", err)
	}
	if err := run([]string{"--help"}); err != nil {
		t.Fatalf("run(--help) = %v", err)
	}
}

func TestRunVersion(t *testing.T) {
	if err := run([]string{"version"}); err != nil {
		t.Fatalf("run(version) = %v", err)
	}
}

func TestRunUnknown(t *testing.T) {
	err := run([]string{"nope"})
	if err == nil || !strings.Contains(err.Error(), "unknown command") {
		t.Fatalf("run(nope) = %v, want unknown command", err)
	}
}

func TestRunExcelSaveUsage(t *testing.T) {
	err := run([]string{"excel-save"})
	if err == nil || !strings.Contains(err.Error(), "usage:") {
		t.Fatalf("run(excel-save) = %v, want usage error", err)
	}
}

func TestRunExcelSaveOffWindows(t *testing.T) {
	if os.Getenv("GOOS_FORCE") == "windows" {
		t.Skip("forced windows")
	}
	err := run([]string{"excel-save", "in.xlsx", "out.xlsx"})
	if err == nil {
		t.Fatal("expected error off Windows")
	}
	if !errors.Is(err, excel.ErrNotWindows) && !strings.Contains(err.Error(), "not implemented") {
		t.Fatalf("excel-save error = %v", err)
	}
}
