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
	Version int `json:"version"`
	DataRoot string `json:"data_root"`
	Comment string `json:"_comment"`
}

type onboardingConfig struct {
	OnboardingCompleted bool `json:"onboarding_completed"`
}

const (
	locationPointerVersion = 1
	locationPointerComment = "Zeri bootstrap pointer. This file only tells Zeri where your data lives (data_root). All data (scripts/, sessions/, config/, state.json) is stored under data_root, NOT here. Override with the ZERI_HOME environment variable. See README.txt."
	locationReadmeBody = `Zeri bootstrap pointer directory.

location.json is only a bootstrap pointer that tells Zeri where data_root is.
All runtime data (scripts/, sessions/, config/, state.json) is stored under data_root, not in this folder.

If ZERI_HOME is set, Zeri ignores location.json and uses ZERI_HOME directly as data_root.
`
)

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

func locationReadmePath() (string, error) {
	home, err := ConfigHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, "README.txt"), nil
}

func IsDataRootEnvOverrideActive() bool {
	return strings.TrimSpace(os.Getenv("ZERI_HOME")) != ""
}

func resolveDataRootFromEnv() (string, bool, error) {
	raw := strings.TrimSpace(os.Getenv("ZERI_HOME"))
	if raw == "" {
		return "", false, nil
	}
	if !filepath.IsAbs(raw) {
		return "", false, fmt.Errorf("ZERI_HOME must be an absolute path, got %q", raw)
	}
	normalized, err := normalizeDataRootPath(raw)
	if err != nil {
		return "", false, fmt.Errorf("invalid ZERI_HOME path %q: %w", raw, err)
	}
	info, err := os.Stat(normalized)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return "", false, fmt.Errorf("ZERI_HOME directory does not exist: %s", normalized)
		}
		return "", false, fmt.Errorf("cannot access ZERI_HOME %s: %w", normalized, err)
	}
	if !info.IsDir() {
		return "", false, fmt.Errorf("ZERI_HOME must point to a directory, got file: %s", normalized)
	}
	if err := assertWritableDir(normalized); err != nil {
		return "", false, fmt.Errorf("ZERI_HOME is not writable: %w", err)
	}
	return normalized, true, nil
}

func assertWritableDir(dir string) error {
	probe, err := os.CreateTemp(dir, "zeri-write-check-*")
	if err != nil {
		return err
	}
	name := probe.Name()
	if closeErr := probe.Close(); closeErr != nil {
		_ = os.Remove(name)
		return closeErr
	}
	if removeErr := os.Remove(name); removeErr != nil {
		return removeErr
	}
	return nil
}

func normalizeDataRootPath(path string) (string, error) {
	trimmed := strings.TrimSpace(path)
	if trimmed == "" {
		return "", fmt.Errorf("path cannot be empty")
	}
	absPath, err := filepath.Abs(trimmed)
	if err != nil {
		return "", err
	}
	return filepath.Clean(absPath), nil
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
	if pointer.Version == 0 {
		pointer.Version = locationPointerVersion
	}
	if pointer.Version != locationPointerVersion {
		return locationPointer{}, false, fmt.Errorf("unsupported location pointer version %d in %s", pointer.Version, path)
	}
	return pointer, true, nil
}

func ResolveBaseDir() (string, bool, error) {
	if dataRoot, hasOverride, err := resolveDataRootFromEnv(); err != nil {
		return "", false, err
	} else if hasOverride {
		return dataRoot, true, nil
	}
	pointer, present, err := readLocationPointer()
	if err != nil || !present {
		return "", false, err
	}
	dataRoot, err := normalizeDataRootPath(pointer.DataRoot)
	if err != nil {
		return "", false, fmt.Errorf("invalid data_root in location pointer: %w", err)
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
	return "", fmt.Errorf("data root is not configured yet")
}

func saveLocationPointer(pointer locationPointer) error {
	if _, hasOverride, err := resolveDataRootFromEnv(); err != nil {
		return err
	} else if hasOverride {
		return fmt.Errorf("cannot update location pointer while ZERI_HOME is set")
	}

	dataRoot, err := normalizeDataRootPath(pointer.DataRoot)
	if err != nil {
		return fmt.Errorf("invalid data_root: %w", err)
	}
	pointer = locationPointer{
		Version: locationPointerVersion,
		DataRoot: dataRoot,
		Comment: locationPointerComment,
	}

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
	if err := writeFileAtomically(path, payload, 0o644); err != nil {
		return err
	}
	return writeLocationReadme()
}

func writeLocationReadme() error {
	path, err := locationReadmePath()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	return writeFileAtomically(path, []byte(locationReadmeBody), 0o644)
}

func writeFileAtomically(path string, payload []byte, mode os.FileMode) error {
	dir := filepath.Dir(path)
	tempFile, err := os.CreateTemp(dir, filepath.Base(path)+".tmp-*")
	if err != nil {
		return err
	}
	tempPath := tempFile.Name()
	cleanupTemp := func() {
		_ = os.Remove(tempPath)
	}
	if _, err := tempFile.Write(payload); err != nil {
		_ = tempFile.Close()
		cleanupTemp()
		return err
	}
	if err := tempFile.Chmod(mode); err != nil {
		_ = tempFile.Close()
		cleanupTemp()
		return err
	}
	if err := tempFile.Close(); err != nil {
		cleanupTemp()
		return err
	}
	if err := os.Rename(tempPath, path); err != nil {
		cleanupTemp()
		return err
	}
	return nil
}

func writeLocationPointer(dataRoot string) error {
	return saveLocationPointer(locationPointer{DataRoot: dataRoot})
}

func OnboardingCompleted() (bool, error) {
	dataRoot, ok, err := ResolveBaseDir()
	if err != nil || !ok {
		return false, err
	}
	return readOnboardingCompletedAtRoot(dataRoot)
}

func SetOnboardingCompleted(completed bool) error {
	dataRoot, ok, err := ResolveBaseDir()
	if err != nil {
		return err
	}
	if !ok {
		return fmt.Errorf("cannot set onboarding state: data root is not configured")
	}
	return writeOnboardingCompletedAtRoot(dataRoot, completed)
}

func ResetOnboarding() error {
	dataRoot, ok, err := ResolveBaseDir()
	if err != nil {
		return err
	}
	if !ok {
		return nil
	}
	return writeOnboardingCompletedAtRoot(dataRoot, false)
}

func AdoptDataRoot(dataRoot string) error {
	if _, hasOverride, err := resolveDataRootFromEnv(); err != nil {
		return err
	} else if hasOverride {
		return fmt.Errorf("cannot change data root while ZERI_HOME is set")
	}
	currentCompleted, err := currentOnboardingCompleted()
	if err != nil {
		return err
	}
	clean, err := normalizeDataRootPath(dataRoot)
	if err != nil {
		return fmt.Errorf("invalid data root: %w", err)
	}
	if err := ensureDataRootLayout(clean); err != nil {
		return err
	}
	if err := writeLocationPointer(clean); err != nil {
		return err
	}
	if currentCompleted {
		return writeOnboardingCompletedAtRoot(clean, true)
	}
	return nil
}

func SetDataRootUnderParent(parent string) (string, error) {
	if _, hasOverride, err := resolveDataRootFromEnv(); err != nil {
		return "", err
	} else if hasOverride {
		return "", fmt.Errorf("cannot change data root while ZERI_HOME is set")
	}
	currentCompleted, err := currentOnboardingCompleted()
	if err != nil {
		return "", err
	}
	expanded, err := expandUserPath(strings.TrimSpace(parent))
	if err != nil {
		return "", err
	}
	if expanded == "" {
		return "", fmt.Errorf("path cannot be empty")
	}
	absParent, err := normalizeDataRootPath(expanded)
	if err != nil {
		return "", fmt.Errorf("invalid path %q: %w", parent, err)
	}
	dataRoot := filepath.Join(absParent, "zeri")
	if err := ensureDataRootLayout(dataRoot); err != nil {
		return "", err
	}
	if err := writeLocationPointer(dataRoot); err != nil {
		return "", err
	}
	if currentCompleted {
		if err := writeOnboardingCompletedAtRoot(dataRoot, true); err != nil {
			return "", err
		}
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
	absParent, err := normalizeDataRootPath(expanded)
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

func ensureDataRootLayout(dataRoot string) error {
	dirs := []string{
		dataRoot,
		filepath.Join(dataRoot, "scripts"),
		filepath.Join(dataRoot, "sessions"),
		filepath.Join(dataRoot, "config"),
	}
	for _, dir := range dirs {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return fmt.Errorf("cannot create %s: %w", dir, err)
		}
	}
	return nil
}

func onboardingConfigPath(dataRoot string) string {
	return filepath.Join(dataRoot, "config", "config.json")
}

func readOnboardingCompletedAtRoot(dataRoot string) (bool, error) {
	path := onboardingConfigPath(dataRoot)
	payload, err := os.ReadFile(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return false, nil
		}
		return false, err
	}
	var cfg onboardingConfig
	if err := json.Unmarshal(payload, &cfg); err != nil {
		return false, fmt.Errorf("corrupted onboarding state %s: %w", path, err)
	}
	return cfg.OnboardingCompleted, nil
}

func writeOnboardingCompletedAtRoot(dataRoot string, completed bool) error {
	path := onboardingConfigPath(dataRoot)
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	cfg := onboardingConfig{}
	payload, err := os.ReadFile(path)
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	if err == nil {
		if unmarshalErr := json.Unmarshal(payload, &cfg); unmarshalErr != nil {
			return fmt.Errorf("corrupted onboarding state %s: %w", path, unmarshalErr)
		}
	}
	cfg.OnboardingCompleted = completed
	encoded, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, encoded, 0o644)
}

func currentOnboardingCompleted() (bool, error) {
	currentRoot, ok, err := ResolveBaseDir()
	if err != nil || !ok {
		return false, err
	}
	return readOnboardingCompletedAtRoot(currentRoot)
}

func EnsureBootstrapPointerFiles() error {
	if IsDataRootEnvOverrideActive() {
		return nil
	}
	pointer, present, err := readLocationPointer()
	if err != nil || !present {
		return err
	}
	return saveLocationPointer(pointer)
}

func DetectConfigHomeResidue(activeDataRoot string) ([]string, error) {
	home, err := ConfigHomeDir()
	if err != nil {
		return nil, err
	}
	normalizedActive, err := normalizeDataRootPath(activeDataRoot)
	if err != nil {
		return nil, err
	}
	if samePath(normalizedActive, home) {
		return nil, nil
	}

	residue := make([]string, 0, 3)
	for _, sub := range []string{"scripts", "sessions", "config"} {
		path := filepath.Join(home, sub)
		info, statErr := os.Stat(path)
		if statErr != nil {
			if errors.Is(statErr, os.ErrNotExist) {
				continue
			}
			return nil, statErr
		}
		if info.IsDir() {
			residue = append(residue, sub)
		}
	}
	return residue, nil
}

func samePath(a string, b string) bool {
	trimmedA := strings.TrimRight(strings.TrimSpace(a), "/\\")
	trimmedB := strings.TrimRight(strings.TrimSpace(b), "/\\")
	if runtime.GOOS == "windows" {
		return strings.EqualFold(trimmedA, trimmedB)
	}
	return trimmedA == trimmedB
}

/*
persistence.go

Storage location is resolved through a fixed-location pointer so the user can
choose, at first run, where Zeri keeps its data without losing the reference
across sessions.

- ConfigHomeDir returns the fixed per-OS directory (the historical base
  location). It holds the bootstrap pointer (location.json) and README.txt only.
- ResolveBaseDir resolves data_root from ZERI_HOME (if set) or from location.json.
  If no pointer exists and no override is set, it reports ok=false.
- ZeriBaseDir requires an explicit data_root and never falls back implicitly.
- SetDataRootUnderParent implements the user-facing choice: it creates
  <parent>/zeri (scripts/sessions/config), records it, and returns it.
  AdoptDataRoot records an explicit root without nesting and also creates
  the standard data layout immediately at commit time.
- expandUserPath resolves a leading "~" cross-platform; absolute resolution is
  delegated to filepath.Abs so Windows, Linux and macOS behave consistently.
- location.json uses schema version=1 with mandatory fields: version, data_root,
  _comment. Pointer writes are atomic (tmp + rename) and refresh README.txt.
- OnboardingCompleted/SetOnboardingCompleted/ResetOnboarding are stored inside
  data_root/config/config.json, so pointer data remains bootstrap-only.
- When ZERI_HOME is set, pointer reads/writes are bypassed. Invalid ZERI_HOME
  (non-absolute, missing, or not writable) returns a hard error.
- InspectDataParent is a read-only probe used by the path-selection step: it
  computes <parent>/zeri the same way SetDataRootUnderParent does, reports
  whether that directory already holds data and whether location.json already
  points there, and distinguishes a permission error from a missing directory
  without producing any side effect.
*/
