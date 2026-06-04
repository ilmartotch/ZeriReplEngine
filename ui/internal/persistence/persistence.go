package persistence

import (
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

func ZeriBaseDir() (string, error) {
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
