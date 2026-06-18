package persistence

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"
	"yuumi/internal/ui"
)

type SessionSnapshot struct {
	Name string `json:"name"`
	SavedAt time.Time `json:"saved_at"`
	ActiveContext string `json:"active_context"`
	History []ui.ChatMessage `json:"history"`
	SessionVars map[string]string `json:"session_vars"`
}

type locationPointer struct {
	DataRoot string `json:"data_root"`
	OnboardingCompleted bool `json:"onboarding_completed"`
}

func ConfigHomeDir() (string, error) {
	if runtime.GOOS == "windows" {
		appData := strings.TrimSpace(os.Getenv("APPDATA"))
		if appData == "" {
			return "", fmt.Errorf("APPDATA is not set")
		}
		return filepath.Join(appData, "Zeri"), nil
	}

	if runtime.GOOS == "darwin" {
		home := strings.TrimSpace(os.Getenv("HOME"))
		if home == "" {
			return "", fmt.Errorf("HOME is not set")
		}
		return filepath.Join(home, "Library", "Application Support", "zeri"), nil
	}

	xdgConfigHome := strings.TrimSpace(os.Getenv("XDG_CONFIG_HOME"))
	if xdgConfigHome != "" {
		return filepath.Join(xdgConfigHome, "zeri"), nil
	}

	home := strings.TrimSpace(os.Getenv("HOME"))
	if home == "" {
		return "", fmt.Errorf("HOME is not set")
	}
	return filepath.Join(home, ".config", "zeri"), nil
}

func DefaultDataParent() (string, error) {
	home, err := ConfigHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Dir(home), nil
}

func locationPointerPath() (string, error) {
	home, err := ConfigHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, "location.json"), nil
}

func readLocationPointer() (locationPointer, bool, error) {
	path, err := locationPointerPath()
	if err != nil {
		return locationPointer{}, false, err
	}
	payload, err := os.ReadFile(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return locationPointer{}, false, nil
		}
		return locationPointer{}, false, err
	}
	var pointer locationPointer
	if err := json.Unmarshal(payload, &pointer); err != nil {
		return locationPointer{}, false, fmt.Errorf("corrupted location pointer %s: %w", path, err)
	}
	return pointer, true, nil
}

func ResolveBaseDir() (string, bool, error) {
	pointer, present, err := readLocationPointer()
	if err != nil || !present {
		return "", false, err
	}
	dataRoot := strings.TrimSpace(pointer.DataRoot)
	if dataRoot == "" {
		return "", false, nil
	}
	return dataRoot, true, nil
}

func ZeriBaseDir() (string, error) {
	dataRoot, ok, err := ResolveBaseDir()
	if err != nil {
		return "", err
	}
	if ok {
		return dataRoot, nil
	}
	return ConfigHomeDir()
}

func saveLocationPointer(pointer locationPointer) error {
	path, err := locationPointerPath()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	payload, err := json.MarshalIndent(pointer, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, payload, 0o644)
}

func writeLocationPointer(dataRoot string) error {
	current, _, err := readLocationPointer()
	if err != nil {
		return err
	}
	current.DataRoot = dataRoot
	return saveLocationPointer(current)
}

func OnboardingCompleted() (bool, error) {
	pointer, present, err := readLocationPointer()
	if err != nil || !present {
		return false, err
	}
	return pointer.OnboardingCompleted, nil
}

func SetOnboardingCompleted(completed bool) error {
	pointer, _, err := readLocationPointer()
	if err != nil {
		return err
	}
	pointer.OnboardingCompleted = completed
	return saveLocationPointer(pointer)
}

func ResetOnboarding() error {
	pointer, present, err := readLocationPointer()
	if err != nil {
		return err
	}
	if !present {
		return nil
	}
	pointer.OnboardingCompleted = false
	return saveLocationPointer(pointer)
}

func AdoptDataRoot(dataRoot string) error {
	clean := strings.TrimSpace(dataRoot)
	if clean == "" {
		return fmt.Errorf("data root cannot be empty")
	}
	if err := os.MkdirAll(clean, 0o755); err != nil {
		return fmt.Errorf("cannot create %s: %w", clean, err)
	}
	return writeLocationPointer(clean)
}

func SetDataRootUnderParent(parent string) (string, error) {
	expanded, err := expandUserPath(strings.TrimSpace(parent))
	if err != nil {
		return "", err
	}
	if expanded == "" {
		return "", fmt.Errorf("path cannot be empty")
	}
	absParent, err := filepath.Abs(expanded)
	if err != nil {
		return "", fmt.Errorf("invalid path %q: %w", parent, err)
	}
	dataRoot := filepath.Join(absParent, "zeri")
	if err := os.MkdirAll(dataRoot, 0o755); err != nil {
		return "", fmt.Errorf("cannot create %s: %w", dataRoot, err)
	}
	if err := writeLocationPointer(dataRoot); err != nil {
		return "", err
	}
	return dataRoot, nil
}

func InspectDataParent(parent string) (resolved string, alreadyHasData bool, alreadyChosen bool, err error) {
	expanded, err := expandUserPath(strings.TrimSpace(parent))
	if err != nil {
		return "", false, false, err
	}
	if expanded == "" {
		return "", false, false, fmt.Errorf("path cannot be empty")
	}
	absParent, err := filepath.Abs(expanded)
	if err != nil {
		return "", false, false, fmt.Errorf("invalid path %q: %w", parent, err)
	}
	dataRoot := filepath.Join(absParent, "zeri")

	entries, readErr := os.ReadDir(dataRoot)
	if readErr != nil {
		if errors.Is(readErr, os.ErrPermission) {
			return dataRoot, false, false, fmt.Errorf("permission denied reading %s: %w", dataRoot, readErr)
		}
		if !errors.Is(readErr, os.ErrNotExist) {
			return dataRoot, false, false, readErr
		}
	} else if len(entries) > 0 {
		alreadyHasData = true
	}

	if current, ok, resolveErr := ResolveBaseDir(); resolveErr == nil && ok {
		if filepath.Clean(current) == filepath.Clean(dataRoot) {
			alreadyChosen = true
		}
	}

	return dataRoot, alreadyHasData, alreadyChosen, nil
}

func expandUserPath(p string) (string, error) {
	if p == "~" || strings.HasPrefix(p, "~/") || strings.HasPrefix(p, `~\`) {
		home, err := os.UserHomeDir()
		if err != nil {
			return "", err
		}
		if p == "~" {
			return home, nil
		}
		return filepath.Join(home, p[2:]), nil
	}
	return p, nil
}

/*
persistence.go

Storage location is resolved through a fixed-location pointer so the user can
choose, at first run, where Zeri keeps its data without losing the reference
across sessions.

- ConfigHomeDir returns the fixed per-OS directory (the historical base
  location). It always holds location.json regardless of where data lives.
- ResolveBaseDir reads location.json and returns the user-chosen data_root.
  When the pointer is absent (genuine first run) it reports ok=false so the
  caller can drive the onboarding path-selection step.
- ZeriBaseDir keeps its previous contract for all data helpers: it returns the
  resolved data_root, falling back to ConfigHomeDir only while no choice has
  been recorded yet (pre-onboarding reads).
- SetDataRootUnderParent implements the user-facing choice: it creates
  <parent>/zeri, records it, and returns it. AdoptDataRoot records an explicit
  root without nesting and is used for silent default/legacy adoption.
- expandUserPath resolves a leading "~" cross-platform; absolute resolution is
  delegated to filepath.Abs so Windows, Linux and macOS behave consistently.
- location.json is the single canonical first-run state file. Besides data_root
  it carries onboarding_completed, so the completion flag no longer depends on
  any marker stored inside the data root. OnboardingCompleted/SetOnboardingCompleted/
  ResetOnboarding read and merge that flag without disturbing data_root, and
  writeLocationPointer preserves the flag when only the data root changes.
- InspectDataParent is a read-only probe used by the path-selection step: it
  computes <parent>/zeri the same way SetDataRootUnderParent does, reports
  whether that directory already holds data and whether location.json already
  points there, and distinguishes a permission error from a missing directory
  without producing any side effect.
*/
