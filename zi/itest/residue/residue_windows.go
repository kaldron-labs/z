//go:build windows

package residue

import (
	"os"
	"path/filepath"
	"syscall"
	"unsafe"
)

var kernel32 = syscall.NewLazyDLL("kernel32.dll")
var lockFileEx = kernel32.NewProc("LockFileEx")
var unlockFileEx = kernel32.NewProc("UnlockFileEx")

func lockRoot(root string) (func(), error) {
	file, err := os.OpenFile(filepath.Join(root, ".zi-residue.lock"),
		os.O_CREATE|os.O_RDWR, 0600)
	if err != nil {
		return nil, err
	}
	var overlap syscall.Overlapped
	handle := uintptr(file.Fd())
	ok, _, callErr := lockFileEx.Call(handle, 0x2, 0, 1, 0,
		uintptr(unsafe.Pointer(&overlap)))
	if ok == 0 {
		file.Close()
		return nil, callErr
	}
	return func() {
		unlockFileEx.Call(handle, 0, 1, 0, uintptr(unsafe.Pointer(&overlap)))
		file.Close()
	}, nil
}

func holdMarker(file *os.File) error {
	var overlap syscall.Overlapped
	ok, _, err := lockFileEx.Call(uintptr(file.Fd()), 0x2, 0, 1, 0,
		uintptr(unsafe.Pointer(&overlap)))
	if ok == 0 {
		return err
	}
	return nil
}

func tryMarker(file *os.File) (bool, error) {
	var overlap syscall.Overlapped
	ok, _, _ := lockFileEx.Call(uintptr(file.Fd()), 0x3, 0, 1, 0,
		uintptr(unsafe.Pointer(&overlap)))
	return ok != 0, nil
}
func unlinkShm(name string) error { return nil }
