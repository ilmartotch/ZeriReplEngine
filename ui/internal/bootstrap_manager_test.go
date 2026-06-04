package internal_test

import (
	"os"
	"path/filepath"
	"runtime"
	"testing"

	bootstrap "yuumi/internal/bootstrap"
)

func TestBootstrapResolveBinaryKnownCandidate(t *testing.T) {
	dir := t.TempDir()
	binaryName := "known-runtime"
	if runtime.GOOS == "windows" {
		binaryName += ".exe"
	}
	fullPath := filepath.Join(dir, binaryName)
	if err := os.WriteFile(fullPath, []byte(""), 0o755); err != nil {
		t.Fatalf("failed creating fake runtime binary: %v", err)
	}

	pathSep := string(os.PathListSeparator)
	t.Setenv("PATH", dir+pathSep+os.Getenv("PATH"))

	resolved, ok := bootstrap.ResolveBinary([]string{binaryName})
	if !ok {
		t.Fatalf("expected known candidate to resolve")
	}
	if resolved == "" {
		t.Fatalf("resolved path should not be empty")
	}
}

func TestBootstrapResolveBinaryUnknownCandidate(t *testing.T) {
	resolved, ok := bootstrap.ResolveBinary([]string{"definitely-not-a-runtime-binary"})
	if ok {
		t.Fatalf("expected unknown candidate to fail resolution, got %s", resolved)
	}
}
