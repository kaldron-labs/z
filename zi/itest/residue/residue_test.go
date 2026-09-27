package residue

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"testing"
)

func TestPass(t *testing.T) {
	root := t.TempDir()
	t.Setenv("ZI_LOGDIR", root)
	res := New(t, "go-pass")
	if err := os.WriteFile(res.Path("output"), []byte("pass"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := res.Finish(true); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(res.Directory); !os.IsNotExist(err) {
		t.Fatalf("passing run left directory: %v", err)
	}
}

func TestFailureChild(t *testing.T) {
	if os.Getenv("GO_WANT_RESIDUE_FAIL") != "1" {
		return
	}
	res := New(t, "go-fail")
	if err := os.WriteFile(res.Path("output"), []byte("diagnostic"), 0600); err != nil {
		t.Fatal(err)
	}
	t.Fatal("expected failure")
}

func TestFailureRetention(t *testing.T) {
	root := t.TempDir()
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(exe, "-test.run=^TestFailureChild$")
	cmd.Env = append(os.Environ(), "GO_WANT_RESIDUE_FAIL=1", "ZI_LOGDIR="+root)
	out, err := cmd.CombinedOutput()
	if err == nil {
		t.Fatal("failure child unexpectedly passed")
	}
	if !strings.Contains(string(out), "# residue:") {
		t.Fatalf("missing retained-path diagnostic: %s", out)
	}
	entries, err := os.ReadDir(root)
	if err != nil || len(entries) != 1 {
		t.Fatalf("retained directories: %v, %v", entries, err)
	}
	path := filepath.Join(root, entries[0].Name())
	if data, err := os.ReadFile(filepath.Join(path, "output")); err != nil || string(data) != "diagnostic" {
		t.Fatalf("retained output: %q, %v", data, err)
	}
}

func TestAgingAndLiveRun(t *testing.T) {
	root := t.TempDir()
	t.Setenv("ZI_LOGDIR", root)
	live := New(t, "go-age")
	for i := 0; i < Age+2; i++ {
		run := New(t, "go-age")
		if err := run.Finish(false); err != nil {
			t.Fatal(err)
		}
	}
	entries, err := os.ReadDir(root)
	if err != nil {
		t.Fatal(err)
	}
	var failed int
	for _, entry := range entries {
		if entry.IsDir() {
			if _, err := os.Stat(filepath.Join(root, entry.Name(), ".failed")); err == nil {
				failed++
			}
		}
	}
	if failed != Age {
		t.Fatalf("retained %d failures, want %d", failed, Age)
	}
	if _, err := os.Stat(live.Directory); err != nil {
		t.Fatalf("live run deleted: %v", err)
	}
	if err := live.Finish(true); err != nil {
		t.Fatal(err)
	}
}

func TestSharedMemory(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("POSIX shared memory location is Linux-specific")
	}
	root := t.TempDir()
	t.Setenv("ZI_LOGDIR", root)
	run := New(t, "go-shm")
	name := run.Shm("go-shm-" + strconv.Itoa(os.Getpid()))
	path := filepath.Join("/dev/shm", name+".ctrl")
	if err := os.WriteFile(path, []byte("test"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := run.Finish(false); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(path); !os.IsNotExist(err) {
		t.Fatalf("shared memory retained: %v", err)
	}
}

func TestRelativeRoot(t *testing.T) {
	cwd, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	relative, err := filepath.Rel(cwd, t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	t.Setenv("ZI_LOGDIR", relative)
	run := New(t, "go-relative")
	if !filepath.IsAbs(run.Root) {
		t.Fatalf("root is not absolute: %s", run.Root)
	}
	if err := run.Finish(true); err != nil {
		t.Fatal(err)
	}
}
