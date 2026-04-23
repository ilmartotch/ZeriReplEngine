package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"time"
	"yuumi/internal/ui"
)

type ErrSessionExists struct {
	Name string
}

func (e ErrSessionExists) Error() string {
	return fmt.Sprintf("session %q already exists", strings.TrimSpace(e.Name))
}

type SessionSnapshot struct {
	Name string `json:"name"`
	SavedAt time.Time `json:"saved_at"`
	ActiveContext string `json:"active_context"`
	History []ui.ChatMessage  `json:"history"`
	SessionVars map[string]string `json:"session_vars"`
}

func ZeriBaseDir() (string, error) {
	if runtime.GOOS == "windows" {
		appData := strings.TrimSpace(os.Getenv("APPDATA"))
		if appData == "" {
			return "", fmt.Errorf("APPDATA is not set")
		}
		return filepath.Join(appData, "Zeri"), nil
	}

	home, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("cannot resolve home directory: %w", err)
	}
	return filepath.Join(home, ".zeri"), nil
}

func ZeriScriptsDir(language string) (string, error) {
	base, err := ZeriBaseDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(base, "scripts", scriptFolderName(language)), nil
}

func ZeriSessionsDir() (string, error) {
	base, err := ZeriBaseDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(base, "sessions"), nil
}

func ensureZeriDirectories() error {
	base, err := ZeriBaseDir()
	if err != nil {
		return err
	}

	dirs := []string{
		filepath.Join(base, "scripts", "lua"),
		filepath.Join(base, "scripts", "py"),
		filepath.Join(base, "scripts", "js"),
		filepath.Join(base, "scripts", "ts"),
		filepath.Join(base, "scripts", "ruby"),
		filepath.Join(base, "sessions"),
	}

	for _, dir := range dirs {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return fmt.Errorf("cannot create %s: %w", dir, err)
		}
	}

	return nil
}

func scriptFolderName(language string) string {
	switch strings.ToLower(strings.TrimSpace(language)) {
	case "python", "py":
		return "py"
	case "javascript", "js":
		return "js"
	case "typescript", "ts":
		return "ts"
	case "ruby", "rb":
		return "ruby"
	case "lua":
		return "lua"
	default:
		return strings.ToLower(strings.TrimSpace(language))
	}
}

func languageExtension(language string) string {
	switch scriptFolderName(language) {
	case "lua":
		return "lua"
	case "py":
		return "py"
	case "js":
		return "js"
	case "ts":
		return "ts"
	case "ruby":
		return "rb"
	default:
		return "txt"
	}
}

func sanitizeStorageName(name string) (string, error) {
	trimmed := strings.TrimSpace(name)
	if trimmed == "" {
		return "", fmt.Errorf("name cannot be empty")
	}
	if strings.ContainsAny(trimmed, `/\\`) {
		return "", fmt.Errorf("name cannot contain path separators")
	}
	if trimmed == "." || trimmed == ".." {
		return "", fmt.Errorf("name is not valid")
	}
	return trimmed, nil
}

func saveScript(name string, language string, content string) error {
	safeName, err := sanitizeStorageName(name)
	if err != nil {
		return err
	}
	dir, err := ZeriScriptsDir(language)
	if err != nil {
		return err
	}
	ext := languageExtension(language)
	path := filepath.Join(dir, safeName+"."+ext)
	return os.WriteFile(path, []byte(content), 0o644)
}

func readScript(name string, language string) (string, error) {
	safeName, err := sanitizeStorageName(name)
	if err != nil {
		return "", err
	}
	dir, err := ZeriScriptsDir(language)
	if err != nil {
		return "", err
	}
	ext := languageExtension(language)
	path := filepath.Join(dir, safeName+"."+ext)
	data, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	return string(data), nil
}

func deleteScript(name string, language string) error {
	safeName, err := sanitizeStorageName(name)
	if err != nil {
		return err
	}
	dir, err := ZeriScriptsDir(language)
	if err != nil {
		return err
	}
	ext := languageExtension(language)
	path := filepath.Join(dir, safeName+"."+ext)
	if _, statErr := os.Stat(path); os.IsNotExist(statErr) {
		return os.ErrNotExist
	}
	return os.Remove(path)
}

func saveSession(model AppModel, name string, overwrite bool) error {
	safeName, err := sanitizeStorageName(name)
	if err != nil {
		return err
	}
	dir, err := ZeriSessionsDir()
	if err != nil {
		return err
	}
	path := filepath.Join(dir, safeName+".json")

	if !overwrite {
		if _, err = os.Stat(path); err == nil {
			return ErrSessionExists{Name: safeName}
		}
	}

	historyCopy := make([]ui.ChatMessage, len(model.messages))
	copy(historyCopy, model.messages)
	sessionVars := cloneSessionVars(model.sessionVars)

	snapshot := SessionSnapshot{
		Name: safeName,
		SavedAt: time.Now(),
		ActiveContext: model.activeContextPath,
		History: historyCopy,
		SessionVars: sessionVars,
	}

	data, err := json.MarshalIndent(snapshot, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(path, data, 0o644)
}

func loadSession(name string) (SessionSnapshot, error) {
	safeName, err := sanitizeStorageName(name)
	if err != nil {
		return SessionSnapshot{}, err
	}
	dir, err := ZeriSessionsDir()
	if err != nil {
		return SessionSnapshot{}, err
	}
	path := filepath.Join(dir, safeName+".json")
	data, err := os.ReadFile(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return SessionSnapshot{}, fmt.Errorf("session %q not found", safeName)
		}
		return SessionSnapshot{}, err
	}

	var snapshot SessionSnapshot
	if err = json.Unmarshal(data, &snapshot); err != nil {
		return SessionSnapshot{}, fmt.Errorf("corrupted session file: %w", err)
	}
	if snapshot.Name == "" {
		snapshot.Name = safeName
	}
	if snapshot.SessionVars == nil {
		snapshot.SessionVars = map[string]string{}
	}

	return snapshot, nil
}

func listSessionNames(prefix string) ([]string, error) {
	dir, err := ZeriSessionsDir()
	if err != nil {
		return nil, err
	}
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}

	lowerPrefix := strings.ToLower(strings.TrimSpace(prefix))
	names := make([]string, 0, len(entries))
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := strings.TrimSuffix(entry.Name(), ".json")
		if lowerPrefix != "" && !strings.HasPrefix(strings.ToLower(name), lowerPrefix) {
			continue
		}
		names = append(names, name)
	}
	sort.Strings(names)
	return names, nil
}

func listScriptNames(prefix string, language string) ([]string, error) {
	dir, err := ZeriScriptsDir(language)
	if err != nil {
		return nil, err
	}
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}

	ext := "." + languageExtension(language)
	lowerPrefix := strings.ToLower(strings.TrimSpace(prefix))
	names := make([]string, 0, len(entries))
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := entry.Name()
		if ext != ".txt" && !strings.HasSuffix(strings.ToLower(name), strings.ToLower(ext)) {
			continue
		}
		name = strings.TrimSuffix(name, ext)
		if lowerPrefix != "" && !strings.HasPrefix(strings.ToLower(name), lowerPrefix) {
			continue
		}
		names = append(names, name)
	}
	sort.Strings(names)
	return names, nil
}

func cloneSessionVars(input map[string]string) map[string]string {
	if len(input) == 0 {
		return map[string]string{}
	}
	result := make(map[string]string, len(input))
	for key, value := range input {
		result[key] = value
	}
	return result
}

func formatScriptConfirmation(name string, language string, content string) string {
	lines := strings.Count(content, "\n") + 1
	size := len([]byte(content))
	ext := languageExtension(language)
	sizeLabel := formatBytes(size)

	return fmt.Sprintf(
		"✓ Script %q saved successfully\n   %d lines  ·  %s",
		name+"."+ext,
		lines,
		sizeLabel,
	)
}

func formatBytes(size int) string {
	switch {
	case size < 1024:
		return fmt.Sprintf("%d B", size)
	case size < 1024*1024:
		return fmt.Sprintf("%.1f KB", float64(size)/1024)
	default:
		return fmt.Sprintf("%.1f MB", float64(size)/(1024*1024))
	}
}

/*
 * What changed:
 *   - Added centralized persistence helpers for .zeri base/scripts/sessions paths.
 *   - Added first-run directory bootstrap creation for scripts and sessions.
 *   - Added script save/read/delete utilities and language folder/extension normalization.
 *   - Added deleteScript: resolves path via sanitizeStorageName, returns os.ErrNotExist
 *     if not found, otherwise removes the file.
 *   - Added named session snapshot save/load with JSON schema and overwrite guard.
 *   - Added directory-backed session/script name listing utilities for autocomplete.
 *   - Added script save confirmation format utilities for history rendering.
 */
