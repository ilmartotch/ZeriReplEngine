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
	Name          string            `json:"name"`
	SavedAt       time.Time         `json:"saved_at"`
	ActiveContext string            `json:"active_context"`
	History       []ui.ChatMessage  `json:"history"`
	SessionVars   map[string]string `json:"session_vars"`
}

type locationPointer struct {
	DataRoot string `json:"data_root"`
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

func ResolveBaseDir() (string, bool, error) {
	path, err := locationPointerPath()
	if err != nil {
		return "", false, err
	}
	payload, err := os.ReadFile(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return "", false, nil
		}
		return "", false, err
	}
	var pointer locationPointer
	if err := json.Unmarshal(payload, &pointer); err != nil {
		return "", false, fmt.Errorf("corrupted location pointer %s: %w", path, err)
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

func writeLocationPointer(dataRoot string) error {
	path, err := locationPointerPath()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	payload, err := json.MarshalIndent(locationPointer{DataRoot: dataRoot}, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, payload, 0o644)
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
*/
