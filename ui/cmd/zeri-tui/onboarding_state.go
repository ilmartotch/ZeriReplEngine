package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

func saveOnboardingCompleted() error {
	return SetOnboardingCompleted(true)
}

func initializeDataLocation(noOnboarding bool) error {
	dataRoot, ok, err := ResolveDataRoot()
	if err != nil {
		return err
	}
	if ok {
		if err := EnsureBootstrapPointerFiles(); err != nil {
			return err
		}
		warnConfigHomeResidue(dataRoot)
		return ensureZeriDirectories()
	}
	if legacyInstallPresent() || noOnboarding {
		home, homeErr := ConfigHomeDir()
		if homeErr != nil {
			return homeErr
		}
		if err := AdoptDataRoot(home); err != nil {
			return err
		}
		warnConfigHomeResidue(home)
		return nil
	}
	return nil
}

func legacyInstallPresent() bool {
	home, err := ConfigHomeDir()
	if err != nil {
		return false
	}
	for _, sub := range []string{"config", "scripts", "sessions"} {
		entries, readErr := os.ReadDir(filepath.Join(home, sub))
		if readErr == nil && len(entries) > 0 {
			return true
		}
	}
	return false
}

func needsOnboarding(skip bool) bool {
	if skip {
		return false
	}
	if _, ok, err := ResolveDataRoot(); err == nil && !ok {
		return true
	}
	completed, err := OnboardingCompleted()
	if err != nil {
		return false
	}
	return !completed
}

func warnConfigHomeResidue(activeDataRoot string) {
	trimmedRoot := strings.TrimSpace(activeDataRoot)
	if trimmedRoot == "" {
		return
	}
	residue, err := DetectConfigHomeResidue(trimmedRoot)
	if err != nil || len(residue) == 0 {
		return
	}
	home, homeErr := ConfigHomeDir()
	if homeErr != nil {
		return
	}
	fmt.Fprintf(
		os.Stderr,
		"[ZERI][SESSION-013] Found legacy data directories in %s (%s) while active data root is %s. Files were left untouched.\n",
		home,
		strings.Join(residue, ", "),
		trimmedRoot,
	)
}

/*
onboarding_state.go

Data-location bootstrap is resolved before the UI starts:
- if data_root is already configured, startup keeps using it and warns (non
  blocking) when residual data directories exist under ConfigHomeDir while the
  active root points elsewhere.
- if running with --no-onboarding (or migrating a legacy install), startup
  explicitly commits the default data_root to ConfigHomeDir.

Onboarding completion state is read from the data root itself (config/config.json)
through OnboardingCompleted / SetOnboardingCompleted.
*/
