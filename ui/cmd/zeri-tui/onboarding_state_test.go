package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

func isolateOnboardingHome(t *testing.T) {
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
}

func TestNeedsOnboardingFirstRun(t *testing.T) {
	isolateOnboardingHome(t)

	if !needsOnboarding(false) {
		t.Fatalf("expected onboarding required with absent pointer")
	}
	if needsOnboarding(true) {
		t.Fatalf("expected skip to bypass onboarding")
	}
}

func TestNeedsOnboardingAfterCompletion(t *testing.T) {
	isolateOnboardingHome(t)

	home, err := ConfigHomeDir()
	if err != nil {
		t.Fatalf("ConfigHomeDir returned error: %v", err)
	}
	if err := AdoptDataRoot(home); err != nil {
		t.Fatalf("AdoptDataRoot returned error: %v", err)
	}
	if err := saveOnboardingCompleted(); err != nil {
		t.Fatalf("saveOnboardingCompleted returned error: %v", err)
	}
	if needsOnboarding(false) {
		t.Fatalf("expected onboarding not required after completion")
	}
}

func TestNeedsOnboardingMigratesLegacyMarker(t *testing.T) {
	isolateOnboardingHome(t)

	home, err := ConfigHomeDir()
	if err != nil {
		t.Fatalf("ConfigHomeDir returned error: %v", err)
	}
	if err := AdoptDataRoot(home); err != nil {
		t.Fatalf("AdoptDataRoot returned error: %v", err)
	}

	legacyPath, err := legacyOnboardingConfigPath()
	if err != nil {
		t.Fatalf("legacyOnboardingConfigPath returned error: %v", err)
	}
	if err := os.MkdirAll(filepath.Dir(legacyPath), 0o755); err != nil {
		t.Fatalf("mkdir failed: %v", err)
	}
	payload, _ := json.Marshal(onboardingConfig{OnboardingCompleted: true})
	if err := os.WriteFile(legacyPath, payload, 0o644); err != nil {
		t.Fatalf("write legacy marker failed: %v", err)
	}

	if needsOnboarding(false) {
		t.Fatalf("expected legacy completion to be honored")
	}

	completed, err := OnboardingCompleted()
	if err != nil || !completed {
		t.Fatalf("expected legacy flag migrated into location.json, got completed=%v err=%v", completed, err)
	}
}
