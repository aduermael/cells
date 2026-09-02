package golden

import (
	"os"
	"path/filepath"
	"testing"
)

func TestWriteRead(t *testing.T) {
	dir := t.TempDir()
	xlsx := filepath.Join(dir, "golden.xlsx")
	if err := os.WriteFile(xlsx, []byte("pk"), 0o644); err != nil {
		t.Fatal(err)
	}
	m := New("excel-win", "windows", "16.0", "17628", "simple.xlsx")
	if err := Write(xlsx, m); err != nil {
		t.Fatal(err)
	}
	got, err := Read(xlsx)
	if err != nil {
		t.Fatal(err)
	}
	if got.Host != "excel-win" || got.ExcelVersion != "16.0" || got.ExcelBuild != "17628" {
		t.Fatalf("got %+v", got)
	}
	if got.Input != "simple.xlsx" || got.Tool != "cells-verify" || got.OS != "windows" {
		t.Fatalf("got %+v", got)
	}
	if got.GeneratedAt == "" {
		t.Fatal("missing generatedAt")
	}
	if _, err := os.Stat(PathFor(xlsx)); err != nil {
		t.Fatal(err)
	}
}
