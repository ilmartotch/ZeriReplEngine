package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
	persistence "yuumi/internal/persistence"
	"yuumi/internal/ui"
)

type ErrSessionExists struct {
	Name string
}

func (e ErrSessionExists) Error() string {
	return fmt.Sprintf("session %q already exists", strings.TrimSpace(e.Name))
}

type SessionSnapshot = persistence.SessionSnapshot

type AiContextConfig struct {
	Endpoint string `json:"endpoint"`
	Model string `json:"model"`
	ApiKey string `json:"apiKey,omitempty"`
}

func (c AiContextConfig) IsConfigured() bool {
	return strings.TrimSpace(c.Endpoint) != "" ||
		strings.TrimSpace(c.Model) != "" ||
		strings.TrimSpace(c.ApiKey) != ""
}

func ZeriBaseDir() (string, error) {
	return persistence.ZeriBaseDir()
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

func ZeriConfigDir() (string, error) {
	base, err := ZeriBaseDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(base, "config"), nil
}

func ZeriAiConfigPath() (string, error) {
	dir, err := ZeriConfigDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "ai.json"), nil
}

func ensureZeriDirectories() error {
	base, err := ZeriBaseDir()
	if err != nil {
		return err
	}

	dirs := []string{
		filepath.Join(base, "scripts"),
		filepath.Join(base, "sessions"),
		filepath.Join(base, "config"),
	}

	for _, dir := range dirs {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return fmt.Errorf("cannot create %s: %w", dir, err)
		}
	}

	return nil
}

func loadAiContextConfig() (AiContextConfig, error) {
	path, err := ZeriAiConfigPath()
	if err != nil {
		return AiContextConfig{}, err
	}
	data, err := os.ReadFile(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return AiContextConfig{}, nil
		}
		return AiContextConfig{}, err
	}
	var cfg AiContextConfig
	if err = json.Unmarshal(data, &cfg); err != nil {
		return AiContextConfig{}, err
	}
	return cfg, nil
}

func saveAiContextConfig(cfg AiContextConfig) error {
	path, err := ZeriAiConfigPath()
	if err != nil {
		return err
	}
	if err = os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, data, 0o644)
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

func stripScriptExtension(name, language string) string {
	suffix := "." + languageExtension(language)
	if strings.HasSuffix(strings.ToLower(name), strings.ToLower(suffix)) {
		return name[:len(name)-len(suffix)]
	}
	return name
}

func saveScript(name string, language string, content string) error {
	safeName, err := sanitizeStorageName(name)
	if err != nil {
		return err
	}
	safeName = stripScriptExtension(safeName, language)
	dir, err := ZeriScriptsDir(language)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return fmt.Errorf("cannot create script directory: %w", err)
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
	safeName = stripScriptExtension(safeName, language)
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
	safeName = stripScriptExtension(safeName, language)
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
 *   - [fix #4] saveScript now calls os.MkdirAll on the language script directory before
 *     writing, so scripts/js, scripts/py, etc. are created on demand without requiring a
 *     prior explicit first-run setup step.
 *   - [fix #5] Added stripScriptExtension helper; saveScript, readScript, and deleteScript
 *     now strip the language extension from the name before appending it, preventing the
 *     double-extension bug (prova.js -> prova.js.js) when the user includes the extension
 *     in the script name argument.
 */
