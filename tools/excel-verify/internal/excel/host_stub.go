//go:build !windows

package excel

import "runtime"

func newPlatformHost() Host {
	return stubHost{}
}

type stubHost struct{}

func (stubHost) OpenSave(_, _ string) error {
	return ErrNotWindows
}

func (stubHost) Info() (HostInfo, error) {
	return HostInfo{ID: HostID, OS: runtime.GOOS}, ErrNotWindows
}

func (stubHost) Available() bool {
	return false
}
