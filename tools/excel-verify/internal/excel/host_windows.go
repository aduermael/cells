//go:build windows

package excel

import (
	"fmt"
	"runtime"
)

func newPlatformHost() Host {
	return windowsHost{}
}

// Placeholder until the COM implementation lands. Compiles on Windows so
// `GOOS=windows go build` works; OpenSave is wired in the next step.
type windowsHost struct{}

func (windowsHost) OpenSave(_, _ string) error {
	return fmt.Errorf("excel COM open+save is not implemented")
}

func (windowsHost) Info() (HostInfo, error) {
	return HostInfo{ID: HostID, OS: runtime.GOOS}, fmt.Errorf("excel COM host is not implemented")
}

func (windowsHost) Available() bool {
	return false
}
