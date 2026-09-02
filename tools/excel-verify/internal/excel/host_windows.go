//go:build windows

package excel

import (
	"fmt"
	"os"
	"runtime"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	ole "github.com/go-ole/go-ole"
	"github.com/go-ole/go-ole/oleutil"
	"golang.org/x/sys/windows"
)

// xlOpenXMLWorkbook is Excel's FileFormat for .xlsx (no macros).
const xlOpenXMLWorkbook int32 = 51

func newPlatformHost() Host {
	return &windowsHost{}
}

type windowsHost struct {
	mu   sync.Mutex
	last HostInfo
}

func (h *windowsHost) Available() bool {
	_, err := ole.ClassIDFrom("Excel.Application")
	return err == nil
}

func (h *windowsHost) Info() (HostInfo, error) {
	h.mu.Lock()
	last := h.last
	h.mu.Unlock()
	if last.ExcelVersion != "" {
		return last, nil
	}
	info, err := queryExcelInfo(DefaultTimeout)
	if err != nil {
		return HostInfo{ID: HostID, OS: runtime.GOOS}, err
	}
	h.mu.Lock()
	h.last = info
	h.mu.Unlock()
	return info, nil
}

func (h *windowsHost) OpenSave(inputPath, outputPath string) error {
	absIn, absOut, err := preparePaths(inputPath, outputPath)
	if err != nil {
		return err
	}
	info, err := runExcelSTA(DefaultTimeout, func(excel *ole.IDispatch) (HostInfo, error) {
		return openSaveWithExcel(excel, absIn, absOut)
	})
	if err != nil {
		return err
	}
	h.mu.Lock()
	h.last = info
	h.mu.Unlock()
	return nil
}

func queryExcelInfo(timeout time.Duration) (HostInfo, error) {
	return runExcelSTA(timeout, func(excel *ole.IDispatch) (HostInfo, error) {
		return readExcelInfo(excel), nil
	})
}

type excelJob func(excel *ole.IDispatch) (HostInfo, error)

func runExcelSTA(timeout time.Duration, job excelJob) (HostInfo, error) {
	type result struct {
		info HostInfo
		err  error
	}
	done := make(chan result, 1)
	var pid atomic.Uint32

	go func() {
		runtime.LockOSThread()
		defer runtime.UnlockOSThread()
		info, err := withExcelApplication(&pid, job)
		done <- result{info: info, err: err}
	}()

	timer := time.NewTimer(timeout)
	defer timer.Stop()
	select {
	case r := <-done:
		return r.info, r.err
	case <-timer.C:
		if p := pid.Load(); p != 0 {
			killPID(p)
		}
		return HostInfo{}, fmt.Errorf("excel-save timed out after %s", timeout)
	}
}

func withExcelApplication(pid *atomic.Uint32, job excelJob) (info HostInfo, err error) {
	if e := ole.CoInitializeEx(0, ole.COINIT_APARTMENTTHREADED); e != nil {
		// S_FALSE (already initialized) is reported as error by some wrappers;
		// continue if CreateObject still works.
	}
	defer ole.CoUninitialize()

	unknown, err := oleutil.CreateObject("Excel.Application")
	if err != nil {
		return HostInfo{ID: HostID, OS: runtime.GOOS},
			fmt.Errorf("Excel.Application: %w (is Microsoft Excel installed?)", err)
	}
	defer unknown.Release()

	excel, err := unknown.QueryInterface(ole.IID_IDispatch)
	if err != nil {
		return HostInfo{ID: HostID, OS: runtime.GOOS}, fmt.Errorf("Excel IDispatch: %w", err)
	}
	defer excel.Release()

	if p := pidFromExcel(excel); p != 0 {
		pid.Store(p)
	}

	defer func() {
		_, _ = oleutil.CallMethod(excel, "Quit")
		if p := pid.Load(); p != 0 {
			waitOrKill(p, 10*time.Second)
		}
	}()

	if _, err := oleutil.PutProperty(excel, "Visible", false); err != nil {
		return info, fmt.Errorf("Excel Visible=false: %w", err)
	}
	if _, err := oleutil.PutProperty(excel, "DisplayAlerts", false); err != nil {
		return info, fmt.Errorf("Excel DisplayAlerts=false: %w", err)
	}
	_, _ = oleutil.PutProperty(excel, "AskToUpdateLinks", false)
	_, _ = oleutil.PutProperty(excel, "AlertBeforeOverwriting", false)
	_, _ = oleutil.PutProperty(excel, "ScreenUpdating", false)

	return job(excel)
}

func openSaveWithExcel(excel *ole.IDispatch, absIn, absOut string) (HostInfo, error) {
	info := readExcelInfo(excel)

	wbProp, err := oleutil.GetProperty(excel, "Workbooks")
	if err != nil {
		return info, fmt.Errorf("Excel Workbooks: %w", err)
	}
	defer wbProp.Clear()
	workbooks := wbProp.ToIDispatch()
	if workbooks == nil {
		return info, fmt.Errorf("Excel Workbooks is nil")
	}

	// UpdateLinks=0 (don't update), ReadOnly=false. int32 so COM gets VT_I4.
	opened, err := oleutil.CallMethod(workbooks, "Open", absIn, int32(0), false)
	if err != nil {
		return info, fmt.Errorf("Excel Workbooks.Open %s: %w", absIn, err)
	}
	defer opened.Clear()
	wb := opened.ToIDispatch()
	if wb == nil {
		return info, fmt.Errorf("Excel Workbooks.Open returned nil for %s", absIn)
	}
	defer func() {
		_, _ = oleutil.CallMethod(wb, "Close", false)
	}()

	if sameFilePath(absIn, absOut) {
		if _, err := oleutil.CallMethod(wb, "Save"); err != nil {
			return info, fmt.Errorf("Excel Workbook.Save: %w", err)
		}
	} else {
		if _, err := oleutil.CallMethod(wb, "SaveAs", absOut, xlOpenXMLWorkbook); err != nil {
			return info, fmt.Errorf("Excel Workbook.SaveAs %s: %w", absOut, err)
		}
	}
	return info, nil
}

func readExcelInfo(excel *ole.IDispatch) HostInfo {
	info := HostInfo{ID: HostID, OS: runtime.GOOS}
	if v, err := oleutil.GetProperty(excel, "Version"); err == nil {
		info.ExcelVersion = variantString(v)
		_ = v.Clear()
	}
	if v, err := oleutil.GetProperty(excel, "Build"); err == nil {
		info.ExcelBuild = variantString(v)
		_ = v.Clear()
	}
	return info
}

func variantString(v *ole.VARIANT) string {
	if v == nil {
		return ""
	}
	if s := v.ToString(); s != "" {
		return s
	}
	if val := v.Value(); val != nil {
		return fmt.Sprint(val)
	}
	return ""
}

func pidFromExcel(excel *ole.IDispatch) uint32 {
	v, err := oleutil.GetProperty(excel, "Hwnd")
	if err != nil {
		return 0
	}
	defer v.Clear()
	hwnd := uintptr(v.Val)
	if hwnd == 0 {
		return 0
	}
	var pid uint32
	getWindowThreadProcessId.Call(hwnd, uintptr(unsafe.Pointer(&pid)))
	return pid
}

var (
	user32                   = windows.NewLazySystemDLL("user32.dll")
	getWindowThreadProcessId = user32.NewProc("GetWindowThreadProcessId")
)

func waitOrKill(pid uint32, wait time.Duration) {
	deadline := time.Now().Add(wait)
	for time.Now().Before(deadline) {
		if !pidAlive(pid) {
			return
		}
		time.Sleep(100 * time.Millisecond)
	}
	killPID(pid)
}

func pidAlive(pid uint32) bool {
	const stillActive = 259 // STILL_ACTIVE
	h, err := windows.OpenProcess(windows.PROCESS_QUERY_LIMITED_INFORMATION, false, pid)
	if err != nil {
		return false
	}
	defer windows.CloseHandle(h)
	var code uint32
	if err := windows.GetExitCodeProcess(h, &code); err != nil {
		return false
	}
	return code == stillActive
}

func killPID(pid uint32) {
	proc, err := os.FindProcess(int(pid))
	if err != nil {
		return
	}
	_ = proc.Kill()
}
