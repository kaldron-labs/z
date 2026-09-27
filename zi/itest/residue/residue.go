// Package residue owns files and shared memory created by Go integration tests.
package residue

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"testing"
)

const Age = 8

type Residue struct {
	Root      string
	Directory string
	name      string
	shm       []string
	marker    *os.File
	once      sync.Once
}

func valid(name string) bool {
	if name == "" || strings.Contains(name, "..") {
		return false
	}
	for _, c := range name {
		if c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' ||
			c >= '0' && c <= '9' || c == '.' || c == '_' || c == '-' {
			continue
		}
		return false
	}
	return name[0] != '.'
}

func New(t *testing.T, name string) *Residue {
	t.Helper()
	if !valid(name) {
		t.Fatalf("invalid residue name %q", name)
	}
	root := os.Getenv("ZI_LOGDIR")
	if root == "" {
		root = "."
	}
	root, err := filepath.Abs(root)
	if err != nil {
		t.Fatal(err)
	}
	if err = os.MkdirAll(root, 0755); err != nil {
		t.Fatal(err)
	}
	root, err = filepath.EvalSymlinks(root)
	if err != nil {
		t.Fatal(err)
	}
	dir, err := os.MkdirTemp(root, name+".")
	if err != nil {
		t.Fatal(err)
	}
	r := &Residue{Root: root, Directory: dir, name: name}
	t.Cleanup(func() {
		if err := r.Finish(!t.Failed()); err != nil {
			t.Errorf("residue cleanup: %v", err)
		}
	})
	marker, err := os.OpenFile(filepath.Join(dir, ".active"), os.O_CREATE|os.O_EXCL|os.O_RDWR, 0600)
	if err != nil {
		t.Fatal(err)
	}
	r.marker = marker
	if _, err = marker.WriteString(fmt.Sprint(os.Getpid())); err != nil {
		t.Fatal(err)
	}
	if err = holdMarker(marker); err != nil {
		t.Fatal(err)
	}
	if err = r.age(); err != nil {
		t.Fatal(err)
	}
	return r
}

func (r *Residue) Path(name string) string {
	if !valid(name) {
		panic("invalid residue path name")
	}
	return filepath.Join(r.Directory, name)
}

func (r *Residue) Dir(name string) string {
	path := r.Path(name)
	if err := os.Mkdir(path, 0700); err != nil {
		panic(err)
	}
	return path
}

func (r *Residue) Shm(name string) string {
	if !valid(name) {
		panic("invalid shared memory name")
	}
	for _, existing := range r.shm {
		if existing == name {
			panic("duplicate shared memory name")
		}
	}
	r.shm = append(r.shm, name)
	return name
}

func (r *Residue) Finish(passed bool) error {
	var result error
	r.once.Do(func() {
		for _, name := range r.shm {
			for _, suffix := range []string{".ctrl", ".data"} {
				if err := unlinkShm(name + suffix); err != nil && result == nil {
					result = err
				}
			}
		}
		unlock, err := lockRoot(r.Root)
		if err != nil && result == nil {
			result = err
		}
		if r.marker != nil {
			r.marker.Close()
			r.marker = nil
		}
		if passed {
			if err := os.RemoveAll(r.Directory); err != nil && result == nil {
				result = err
			}
		} else {
			if err := os.Rename(filepath.Join(r.Directory, ".active"), filepath.Join(r.Directory, ".failed")); err != nil && result == nil {
				result = err
			}
			fmt.Printf("# residue: %s\n", r.Directory)
		}
		if unlock != nil {
			unlock()
		}
		if !passed {
			if err := r.age(); err != nil && result == nil {
				result = err
			}
		}
	})
	return result
}

func (r *Residue) age() error {
	unlock, err := lockRoot(r.Root)
	if err != nil {
		return err
	}
	defer unlock()
	entries, err := os.ReadDir(r.Root)
	if err != nil {
		return err
	}
	var failed []string
	for _, entry := range entries {
		if !entry.IsDir() || !strings.HasPrefix(entry.Name(), r.name+".") {
			continue
		}
		path := filepath.Join(r.Root, entry.Name())
		active := filepath.Join(path, ".active")
		if marker, err := os.OpenFile(active, os.O_RDWR, 0); err == nil {
			locked, lockErr := tryMarker(marker)
			marker.Close()
			if lockErr != nil {
				return lockErr
			}
			if !locked {
				continue
			}
			if err := os.Rename(active, filepath.Join(path, ".failed")); err != nil {
				return err
			}
		}
		if _, err := os.Stat(filepath.Join(path, ".failed")); err == nil {
			failed = append(failed, path)
		}
	}
	sort.Slice(failed, func(i, j int) bool {
		a, _ := os.Stat(failed[i])
		b, _ := os.Stat(failed[j])
		return a.ModTime().After(b.ModTime())
	})
	if len(failed) <= Age {
		return nil
	}
	for _, path := range failed[Age:] {
		if err := os.RemoveAll(path); err != nil {
			return err
		}
	}
	return nil
}
