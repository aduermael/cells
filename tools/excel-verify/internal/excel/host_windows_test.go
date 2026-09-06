//go:build windows

package excel

import (
	"os"
	"path/filepath"
	"testing"
)

func TestWindowsAvailableMatchesProgID(t *testing.T) {
	h := NewHost()
	_ = h.Available() // must not panic without Excel
}

func TestWindowsOpenSave(t *testing.T) {
	h := NewHost()
	if !h.Available() {
		t.Skip("Excel.Application ProgID not registered")
	}

	dir := t.TempDir()
	in := filepath.Join(dir, "in.xlsx")
	out := filepath.Join(dir, "out.xlsx")
	// Minimal empty-ish xlsx is not valid enough for Excel; skip if no fixture.
	src := os.Getenv("CELLS_VERIFY_TEST_XLSX")
	if src == "" {
		t.Skip("set CELLS_VERIFY_TEST_XLSX to a real .xlsx to run this test")
	}
	data, err := os.ReadFile(src)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(in, data, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := h.OpenSave(in, out); err != nil {
		t.Fatal(err)
	}
	st, err := os.Stat(out)
	if err != nil {
		t.Fatal(err)
	}
	if st.Size() == 0 {
		t.Fatal("output is empty")
	}
	info, err := h.Info()
	if err != nil {
		t.Fatal(err)
	}
	if info.ExcelVersion == "" {
		t.Fatal("expected Excel version after OpenSave")
	}
}
