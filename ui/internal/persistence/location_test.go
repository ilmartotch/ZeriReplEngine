package persistence

import (
	"os"
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

func TestOnboardingFlagRoundTrip(t *testing.T) {
	isolateConfigHome(t)

	if completed, err := OnboardingCompleted(); err != nil || completed {
		t.Fatalf("expected absent pointer to report not completed, got completed=%v err=%v", completed, err)
	}

	if err := SetOnboardingCompleted(true); err != nil {
		t.Fatalf("SetOnboardingCompleted returned error: %v", err)
	}
	if completed, err := OnboardingCompleted(); err != nil || !completed {
		t.Fatalf("expected completed after set, got completed=%v err=%v", completed, err)
	}

	if err := ResetOnboarding(); err != nil {
		t.Fatalf("ResetOnboarding returned error: %v", err)
	}
	if completed, err := OnboardingCompleted(); err != nil || completed {
		t.Fatalf("expected not completed after reset, got completed=%v err=%v", completed, err)
	}
}

func TestSetOnboardingPreservesDataRoot(t *testing.T) {
	isolateConfigHome(t)
	parent := t.TempDir()

	dataRoot, err := SetDataRootUnderParent(parent)
	if err != nil {
		t.Fatalf("SetDataRootUnderParent returned error: %v", err)
	}

	if err := SetOnboardingCompleted(true); err != nil {
		t.Fatalf("SetOnboardingCompleted returned error: %v", err)
	}

	resolved, ok, err := ResolveBaseDir()
	if err != nil || !ok {
		t.Fatalf("expected pointer present, got ok=%v err=%v", ok, err)
	}
	if resolved != dataRoot {
		t.Fatalf("data root not preserved: got %q want %q", resolved, dataRoot)
	}
	if completed, err := OnboardingCompleted(); err != nil || !completed {
		t.Fatalf("expected completed flag preserved, got completed=%v err=%v", completed, err)
	}
}

func TestInspectDataParent(t *testing.T) {
	isolateConfigHome(t)
	parent := t.TempDir()

	resolved, hasData, chosen, err := InspectDataParent(parent)
	if err != nil {
		t.Fatalf("InspectDataParent returned error: %v", err)
	}
	if want := filepath.Join(parent, "zeri"); resolved != want {
		t.Fatalf("expected resolved %q, got %q", want, resolved)
	}
	if hasData || chosen {
		t.Fatalf("expected empty/unchosen parent, got hasData=%v chosen=%v", hasData, chosen)
	}

	if err := os.MkdirAll(resolved, 0o755); err != nil {
		t.Fatalf("mkdir failed: %v", err)
	}
	if err := os.WriteFile(filepath.Join(resolved, "marker"), []byte("x"), 0o644); err != nil {
		t.Fatalf("write failed: %v", err)
	}

	_, hasData, chosen, err = InspectDataParent(parent)
	if err != nil {
		t.Fatalf("InspectDataParent returned error: %v", err)
	}
	if !hasData {
		t.Fatalf("expected hasData=true for non-empty dir")
	}
	if chosen {
		t.Fatalf("expected chosen=false before adoption")
	}

	if _, err := SetDataRootUnderParent(parent); err != nil {
		t.Fatalf("SetDataRootUnderParent returned error: %v", err)
	}
	if _, _, chosen, err = InspectDataParent(parent); err != nil || !chosen {
		t.Fatalf("expected chosen=true after adoption, got chosen=%v err=%v", chosen, err)
	}
}
