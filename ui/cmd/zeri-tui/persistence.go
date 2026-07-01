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
	"yuumi/pkg/catalog"
)

type ErrSessionExists struct {
	Name string
}

func (e ErrSessionExists) Error() string {
	return fmt.Sprintf("session %q already exists", strings.TrimSpace(e.Name))
}

type SessionSnapshot = persistence.SessionSnapshot

func ZeriBaseDir() (string, error) {
	return persistence.ZeriBaseDir()
}

func ResolveDataRoot() (string, bool, error) {
	return persistence.ResolveBaseDir()
}

func ConfigHomeDir() (string, error) {
	return persistence.ConfigHomeDir()
}

func DefaultDataParent() (string, error) {
	return persistence.DefaultDataParent()
}

func AdoptDataRoot(dataRoot string) error {
	return persistence.AdoptDataRoot(dataRoot)
}

func SetDataRootUnderParent(parent string) (string, error) {
	return persistence.SetDataRootUnderParent(parent)
}

func InspectDataParent(parent string) (string, bool, bool, error) {
	return persistence.InspectDataParent(parent)
}

func OnboardingCompleted() (bool, error) {
	return persistence.OnboardingCompleted()
}

func SetOnboardingCompleted(completed bool) error {
	return persistence.SetOnboardingCompleted(completed)
}

func ResetOnboarding() error {
	return persistence.ResetOnboarding()
}

func EnsureBootstrapPointerFiles() error {
	return persistence.EnsureBootstrapPointerFiles()
}

func DetectConfigHomeResidue(activeDataRoot string) ([]string, error) {
	return persistence.DetectConfigHomeResidue(activeDataRoot)
}

func IsDataRootEnvOverrideActive() bool {
	return persistence.IsDataRootEnvOverrideActive()
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

func scriptFolderName(language string) string {
	if resolved, ok := catalog.ResolveLanguage(language); ok {
		return resolved.Folder
	}
	return strings.ToLower(strings.TrimSpace(language))
}

func languageExtension(language string) string {
	if resolved, ok := catalog.ResolveLanguage(language); ok {
		return resolved.Extension
	}
	return "txt"
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

func sessionSnapshotPath(name string) (string, string, error) {
	safeName, err := sanitizeStorageName(name)
	if err != nil {
		return "", "", err
	}
	dir, err := ZeriSessionsDir()
	if err != nil {
		return "", "", err
	}
	path := filepath.Join(dir, safeName+".json")
	return safeName, path, nil
}

func sessionSnapshotExists(name string) (string, bool, error) {
	safeName, path, err := sessionSnapshotPath(name)
	if err != nil {
		return "", false, err
	}
	_, err = os.Stat(path)
	if err == nil {
		return safeName, true, nil
	}
	if errors.Is(err, os.ErrNotExist) {
		return safeName, false, nil
	}
	return "", false, err
}

func saveSession(model AppModel, name string, overwrite bool, engineState map[string]interface{}) error {
	safeName, path, err := sessionSnapshotPath(name)
	if err != nil {
		return err
	}

	if !overwrite {
		if _, err = os.Stat(path); err == nil {
			return ErrSessionExists{Name: safeName}
		}
	}
	historyCopy := make([]ui.ChatMessage, len(model.messages))
	copy(historyCopy, model.messages)
	encodedEngineState, err := cloneEngineState(engineState)
	if err != nil {
		return err
	}

	snapshot := SessionSnapshot{
		Name:          safeName,
		SavedAt:       time.Now(),
		ActiveContext: model.activeContextPath,
		History:       historyCopy,
		EngineState:   encodedEngineState,
	}

	data, err := json.MarshalIndent(snapshot, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(path, data, 0o644)
}

func loadSession(name string) (SessionSnapshot, error) {
	safeName, path, err := sessionSnapshotPath(name)
	if err != nil {
		return SessionSnapshot{}, err
	}
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
	if snapshot.EngineState == nil {
		snapshot.EngineState = map[string]interface{}{}
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

func cloneEngineState(input map[string]interface{}) (map[string]interface{}, error) {
	if len(input) == 0 {
		return map[string]interface{}{}, nil
	}
	encoded, err := json.Marshal(input)
	if err != nil {
		return nil, fmt.Errorf("failed to encode engine state snapshot: %w", err)
	}
	var output map[string]interface{}
	if err := json.Unmarshal(encoded, &output); err != nil {
		return nil, fmt.Errorf("failed to decode engine state snapshot: %w", err)
	}
	return output, nil
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
