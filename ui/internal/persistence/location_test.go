package persistence

import (
	"path/filepath"
	"runtime"
	"testing"
)

func isolateConfigHome(t *testing.T) string {
	t.Helper()
	root := t.TempDir()
	switch runtime.GOOS {
	case "windows":
		t.Setenv("APPDATA", root)
	case "darwin":
		t.Setenv("HOME", root)
	default:
		t.Setenv("XDG_CONFIG_HOME", root)
	}
	home, err := ConfigHomeDir()
	if err != nil {
		t.Fatalf("ConfigHomeDir returned error: %v", err)
	}
	return home
}

func TestResolveBaseDirAbsentPointer(t *testing.T) {
	home := isolateConfigHome(t)

	if _, ok, err := ResolveBaseDir(); err != nil || ok {
		t.Fatalf("expected no pointer, got ok=%v err=%v", ok, err)
	}

	base, err := ZeriBaseDir()
	if err != nil {
		t.Fatalf("ZeriBaseDir returned error: %v", err)
	}
	if base != home {
		t.Fatalf("expected fallback to config home %q, got %q", home, base)
	}
}

func TestSetDataRootUnderParentRecordsChoice(t *testing.T) {
	isolateConfigHome(t)
	parent := t.TempDir()

	dataRoot, err := SetDataRootUnderParent(parent)
	if err != nil {
		t.Fatalf("SetDataRootUnderParent returned error: %v", err)
	}
	if want := filepath.Join(parent, "zeri"); dataRoot != want {
		t.Fatalf("expected data root %q, got %q", want, dataRoot)
	}

	resolved, ok, err := ResolveBaseDir()
	if err != nil || !ok {
		t.Fatalf("expected pointer present, got ok=%v err=%v", ok, err)
	}
	if resolved != dataRoot {
		t.Fatalf("expected resolved %q, got %q", dataRoot, resolved)
	}

	base, err := ZeriBaseDir()
	if err != nil {
		t.Fatalf("ZeriBaseDir returned error: %v", err)
	}
	if base != dataRoot {
		t.Fatalf("expected ZeriBaseDir %q, got %q", dataRoot, base)
	}
}

func TestAdoptDataRootRecordsExplicitRoot(t *testing.T) {
	isolateConfigHome(t)
	explicit := filepath.Join(t.TempDir(), "existing-install")

	if err := AdoptDataRoot(explicit); err != nil {
		t.Fatalf("AdoptDataRoot returned error: %v", err)
	}

	resolved, ok, err := ResolveBaseDir()
	if err != nil || !ok {
		t.Fatalf("expected pointer present, got ok=%v err=%v", ok, err)
	}
	if resolved != explicit {
		t.Fatalf("expected resolved %q, got %q", explicit, resolved)
	}
}
