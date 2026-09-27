//go:build !windows

package residue

import (
	"os"
	"path/filepath"
	"syscall"
)

func holdMarker(file *os.File) error {
	return syscall.Flock(int(file.Fd()), syscall.LOCK_EX)
}

func tryMarker(file *os.File) (bool, error) {
	err := syscall.Flock(int(file.Fd()), syscall.LOCK_EX|syscall.LOCK_NB)
	if err == syscall.EWOULDBLOCK || err == syscall.EAGAIN {
		return false, nil
	}
	return err == nil, err
}

func lockRoot(root string) (func(), error) {
	file, err := os.Open(root)
	if err != nil {
		return nil, err
	}
	if err = syscall.Flock(int(file.Fd()), syscall.LOCK_EX); err != nil {
		file.Close()
		return nil, err
	}
	return func() {
		syscall.Flock(int(file.Fd()), syscall.LOCK_UN)
		file.Close()
	}, nil
}

func unlinkShm(name string) error {
	if err := os.Remove(filepath.Join("/dev/shm", name)); err != nil && !os.IsNotExist(err) {
		return err
	}
	return nil
}
