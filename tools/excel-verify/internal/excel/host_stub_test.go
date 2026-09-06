//go:build !windows

package excel

import (
	"errors"
	"runtime"
	"testing"
)

func TestStubOpenSave(t *testing.T) {
	h := NewHost()
	if h.Available() {
		t.Fatal("stub host must not report Available")
	}
	err := h.OpenSave("in.xlsx", "out.xlsx")
	if !errors.Is(err, ErrNotWindows) {
		t.Fatalf("OpenSave error = %v, want ErrNotWindows", err)
	}
	info, err := h.Info()
	if !errors.Is(err, ErrNotWindows) {
		t.Fatalf("Info error = %v, want ErrNotWindows", err)
	}
	if info.ID != HostID {
		t.Fatalf("Info.ID = %q, want %q", info.ID, HostID)
	}
	if info.OS != runtime.GOOS {
		t.Fatalf("Info.OS = %q, want %q", info.OS, runtime.GOOS)
	}
}
