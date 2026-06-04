package integration

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func findRepoRoot(t *testing.T) string {
	t.Helper()

	wd, err := os.Getwd()
	if err != nil {
		t.Fatalf("unable to resolve working directory: %v", err)
	}

	current := wd
	for {
		if fileExists(filepath.Join(current, "CMakeLists.txt")) && fileExists(filepath.Join(current, "ui", "go.mod")) {
			return current
		}
		parent := filepath.Dir(current)
		if parent == current {
			break
		}
		current = parent
	}

	t.Fatalf("unable to locate repository root from %s", wd)
	return ""
}

func fileExists(path string) bool {
	info, err := os.Stat(path)
	if err != nil {
		return false
	}
	return !info.IsDir()
}

func resolveZeriBinaryPath(t *testing.T) (string, bool) {
	t.Helper()

	executableName := "zeri"
	if runtime.GOOS == "windows" {
		executableName = "zeri.exe"
	}

	root := findRepoRoot(t)
	candidates := []string{
		filepath.Join(root, "dist", executableName),
		filepath.Join(root, "build-release", "Release", executableName),
		filepath.Join(root, "build-release", executableName),
	}
	for _, candidate := range candidates {
		if fileExists(candidate) {
			return candidate, true
		}
	}
	return "", false
}

func hasAnyBinary(candidates ...string) bool {
	for _, candidate := range candidates {
		if _, err := exec.LookPath(candidate); err == nil {
			return true
		}
	}
	return false
}

func isolatedUserEnv(t *testing.T) (map[string]string, string) {
	t.Helper()

	base := t.TempDir()
	env := map[string]string{
		"APPDATA": base,
		"USERPROFILE": base,
		"HOME": base,
		"XDG_CONFIG_HOME": base,
	}
	return env, base
}

func expectedSessionStatePath(base string) string {
	if runtime.GOOS == "windows" {
		return filepath.Join(base, "Zeri", "sessions", "state.json")
	}
	return filepath.Join(base, "zeri", "sessions", "state.json")
}

func responseContains(values []string, needle string) bool {
	lowerNeedle := strings.ToLower(strings.TrimSpace(needle))
	for _, value := range values {
		if strings.Contains(strings.ToLower(value), lowerNeedle) {
			return true
		}
	}
	return false
}

func responseDump(outputs []string, errors []string) string {
	return fmt.Sprintf("output=%v errors=%v", outputs, errors)
}
