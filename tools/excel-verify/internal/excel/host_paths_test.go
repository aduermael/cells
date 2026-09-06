package excel

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestPreparePathsRequiresNames(t *testing.T) {
	_, _, err := preparePaths("", "out.xlsx")
	if err == nil {
		t.Fatal("expected error for empty input")
	}
}

func TestPreparePathsRequiresXlsx(t *testing.T) {
	dir := t.TempDir()
	in := filepath.Join(dir, "in.xlsx")
	if err := os.WriteFile(in, []byte("pk"), 0o644); err != nil {
		t.Fatal(err)
	}
	_, _, err := preparePaths(in, filepath.Join(dir, "out.xls"))
	if err == nil || !strings.Contains(err.Error(), ".xlsx") {
		t.Fatalf("error = %v, want .xlsx requirement", err)
	}
}

func TestPreparePathsAbsAndRemovesDest(t *testing.T) {
	dir := t.TempDir()
	in := filepath.Join(dir, "in.xlsx")
	out := filepath.Join(dir, "sub", "out.xlsx")
	if err := os.WriteFile(in, []byte("pk"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Dir(out), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(out, []byte("old"), 0o644); err != nil {
		t.Fatal(err)
	}
	absIn, absOut, err := preparePaths(in, out)
	if err != nil {
		t.Fatal(err)
	}
	if !filepath.IsAbs(absIn) || !filepath.IsAbs(absOut) {
		t.Fatalf("paths not absolute: %q %q", absIn, absOut)
	}
	if _, err := os.Stat(absOut); !os.IsNotExist(err) {
		t.Fatalf("destination should have been removed, stat err=%v", err)
	}
}

func TestPreparePathsMissingInput(t *testing.T) {
	dir := t.TempDir()
	_, _, err := preparePaths(filepath.Join(dir, "missing.xlsx"), filepath.Join(dir, "out.xlsx"))
	if err == nil {
		t.Fatal("expected missing input error")
	}
}
