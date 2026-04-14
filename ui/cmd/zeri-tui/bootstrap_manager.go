package main

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
  "regexp"
	"sort"
   "strconv"
	"strings"
	"time"
)

type BootstrapState struct {
	Version int `json:"version"`
	Completed bool `json:"completed"`
	CompletedAtUTC string `json:"completedAtUtc,omitempty"`
	Runtimes []string `json:"runtimes,omitempty"`
}

type RuntimeManifest struct {
	Version int `json:"version"`
	Runtimes []RuntimeDefinition `json:"runtimes"`
}

type RuntimeDefinition struct {
	Name string `json:"name"`
	Check string `json:"check"`
 Required bool `json:"required"`
	Candidates []string `json:"candidates"`
	VersionArgs []string `json:"versionArgs"`
	MinVersion string `json:"minVersion"`
	InstallHint string `json:"installHint"`
	Installers map[string][]RuntimeInstaller `json:"installers"`
}

type RuntimeInstaller struct {
	Manager string `json:"manager"`
	Package string `json:"package"`
}

type InstallCommand struct {
	Executable string
	Args []string
}

type RuntimeValidationStatus string

const (
	RuntimeStatusOK RuntimeValidationStatus = "RUNTIME_OK"
	RuntimeStatusMissing RuntimeValidationStatus = "RUNTIME_MISSING"
	RuntimeStatusOutdated RuntimeValidationStatus = "RUNTIME_OUTDATED"
)

type RuntimeValidationResult struct {
	Runtime RuntimeDefinition
	Status RuntimeValidationStatus
	Binary string
	DetectedVersion string
}

const bootstrapStateVersion = 1
const runtimeManifestVersion = 1
const runtimeManifestRelativePath = "runtime/runtime_manifest.json"
const bootstrapModeEnv = "ZERI_BOOTSTRAP_MODE"
const bootstrapModeInstall = "install"
const bootstrapModeValidate = "validate"

func RunBootstrapManager() []*PreflightError {
   manifest, err := loadRuntimeManifest()
	if err != nil {
		return []*PreflightError{{
			Code: "BOOTSTRAP_MANIFEST_INVALID",
			Check: "Bootstrap Manifest",
			Message: err.Error(),
			Hint: "Fix runtime/runtime_manifest.json before startup.",
		}}
	}

	results := validateRequiredRuntimes(manifest)
    if countBlockingRequired(results) == 0 {
		persistBootstrapState(manifest, results)
		return nil
	}

   var errs []*PreflightError
    if shouldAttemptInstall() {
		for _, result := range results {
			if !isBlockingRequired(result) {
				continue
			}

			if err := installRuntime(result.Runtime); err != nil {
				errs = append(errs, &PreflightError{
					Code: "BOOTSTRAP_INSTALL_FAILED",
					Check: result.Runtime.Check,
					Message: fmt.Sprintf("Runtime %s bootstrap failed: %v", result.Runtime.Name, err),
					Hint: result.Runtime.InstallHint,
				})
			}
		}
	}

	results = validateRequiredRuntimes(manifest)
	persistBootstrapState(manifest, results)

	if countBlockingRequired(results) == 0 {
		return nil
	}

	for _, result := range results {
		if !isBlockingRequired(result) {
			continue
		}

		code := "RUNTIME_MISSING"
		message := fmt.Sprintf("Runtime %s not found in PATH.", result.Runtime.Name)
		if result.Status == RuntimeStatusOutdated {
			code = "RUNTIME_OUTDATED"
			message = fmt.Sprintf("Runtime %s is outdated. Required >= %s, detected %s.", result.Runtime.Name, result.Runtime.MinVersion, result.DetectedVersion)
		}

		errs = append(errs, &PreflightError{
			Code: code,
			Check: result.Runtime.Check,
			Message: message,
			Hint: result.Runtime.InstallHint,
		})
	}

   if len(errs) == 0 {
		errs = append(errs, &PreflightError{
			Code: "BOOTSTRAP_UNKNOWN_FAILURE",
			Check: "Bootstrap Manager",
			Message: "Bootstrap did not complete and no runtime diagnostics were produced.",
			Hint: "Inspect bootstrap manager logs and runtime manifest integrity.",
		})
	}

	return errs
}

func loadRuntimeManifest() (RuntimeManifest, error) {
	var manifest RuntimeManifest
	path, ok := resolveRuntimeManifestPath()
	if !ok {
		return manifest, fmt.Errorf("manifest not found: %s", runtimeManifestRelativePath)
	}

	raw, err := os.ReadFile(path)
	if err != nil {
		return manifest, fmt.Errorf("cannot read manifest: %w", err)
	}

	if err = json.Unmarshal(raw, &manifest); err != nil {
		return manifest, fmt.Errorf("invalid manifest JSON: %w", err)
	}

	if err = validateManifest(manifest); err != nil {
		return manifest, err
	}

	return manifest, nil
}


func resolveRuntimeManifestPath() (string, bool) {
	start, err := os.Getwd()
	if err != nil {
		return "", false
	}

	current := start
	for {
		candidate := filepath.Join(current, runtimeManifestRelativePath)
		if _, err = os.Stat(candidate); err == nil {
			return candidate, true
		}

		parent := filepath.Dir(current)
		if parent == current {
			break
		}
		current = parent
	}

	return "", false
}

func validateManifest(manifest RuntimeManifest) error {
	if manifest.Version != runtimeManifestVersion {
		return fmt.Errorf("unsupported manifest version %d, expected %d", manifest.Version, runtimeManifestVersion)
	}

	if len(manifest.Runtimes) == 0 {
		return fmt.Errorf("manifest has no runtimes")
	}

	for _, runtime := range manifest.Runtimes {
		if strings.TrimSpace(runtime.Name) == "" || strings.TrimSpace(runtime.Check) == "" {
			return fmt.Errorf("runtime entry has empty name/check")
		}
		if len(runtime.Candidates) == 0 {
			return fmt.Errorf("runtime %s has no command candidates", runtime.Name)
		}
		if strings.TrimSpace(runtime.InstallHint) == "" {
			return fmt.Errorf("runtime %s has empty installHint", runtime.Name)
		}
       if runtime.Required && len(runtime.Installers) == 0 {
			return fmt.Errorf("runtime %s has no installers mapping", runtime.Name)
		}
	}

	return nil
}

func countBlockingRequired(results []RuntimeValidationResult) int {
	count := 0
	for _, result := range results {
		if isBlockingRequired(result) {
			count++
		}
	}
	return count
}

func isBlockingRequired(result RuntimeValidationResult) bool {
	return result.Runtime.Required && result.Status != RuntimeStatusOK
}

func shouldAttemptInstall() bool {
	mode := strings.TrimSpace(strings.ToLower(os.Getenv(bootstrapModeEnv)))
 if mode == "" {
		return true
	}
	if mode == bootstrapModeValidate {
		return false
	}
	return mode == bootstrapModeInstall
}

func validateRequiredRuntimes(manifest RuntimeManifest) []RuntimeValidationResult {
	results := make([]RuntimeValidationResult, 0, len(manifest.Runtimes))
	for _, runtime := range manifest.Runtimes {
		resolved, ok := resolveBinary(runtime.Candidates)
		if !ok {
			results = append(results, RuntimeValidationResult{Runtime: runtime, Status: RuntimeStatusMissing})
			continue
		}

		if strings.TrimSpace(runtime.MinVersion) != "" {
			version, ok := detectVersion(resolved, runtime.VersionArgs)
			if !ok {
				results = append(results, RuntimeValidationResult{Runtime: runtime, Status: RuntimeStatusOutdated, Binary: resolved, DetectedVersion: "unknown"})
				continue
			}

			if !isVersionAtLeast(version, runtime.MinVersion) {
				results = append(results, RuntimeValidationResult{Runtime: runtime, Status: RuntimeStatusOutdated, Binary: resolved, DetectedVersion: version})
				continue
			}

			results = append(results, RuntimeValidationResult{Runtime: runtime, Status: RuntimeStatusOK, Binary: resolved, DetectedVersion: version})
			continue
		}

		results = append(results, RuntimeValidationResult{Runtime: runtime, Status: RuntimeStatusOK, Binary: resolved})
	}
  return results
}

func countBlocking(results []RuntimeValidationResult) int {
	count := 0
	for _, result := range results {
		if result.Status != RuntimeStatusOK {
			count++
		}
	}
	return count
}

func resolveBinary(candidates []string) (string, bool) {
	for _, candidate := range candidates {
     if path, err := exec.LookPath(candidate); err == nil {
			return path, true
		}
	}
    return "", false
}

func isAnyCommandAvailable(candidates []string) bool {
	_, ok := resolveBinary(candidates)
	return ok
}

func installRuntime(runtime RuntimeDefinition) error {
	installers := installersForCurrentPlatform(runtime)
	if len(installers) == 0 {
		return fmt.Errorf("runtime %s has no installers for current platform", runtime.Name)
	}

	var lastErr error
  for _, installer := range installers {
		command, supported := commandForInstaller(installer)
		if !supported {
			lastErr = fmt.Errorf("unsupported installer manager %s", installer.Manager)
			continue
		}

		if _, err := exec.LookPath(command.Executable); err != nil {
			lastErr = fmt.Errorf("installer manager %s not found", installer.Manager)
			continue
		}

		if err := runInstallCommand(command); err != nil {
			lastErr = err
			continue
		}

      if isAnyCommandAvailable(runtime.Candidates) {
			return nil
		}
	}

	if lastErr != nil {
      return lastErr
	}

   return fmt.Errorf("no supported installer manager was available")
}

func runInstallCommand(command InstallCommand) error {
	cmd := exec.Command(command.Executable, command.Args...)
	output, err := cmd.CombinedOutput()
	if err != nil {
		trimmed := strings.TrimSpace(string(output))
		if trimmed == "" {
			return err
		}
		return fmt.Errorf("%w: %s", err, trimmed)
	}
	return nil
}

func detectVersion(binary string, versionArgs []string) (string, bool) {
	cmd := exec.Command(binary, versionArgs...)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", false
	}

	matcher := regexp.MustCompile(`\d+\.\d+(?:\.\d+)?`)
	found := matcher.FindString(string(output))
	if found == "" {
		return "", false
	}
	return found, true
}

func isVersionAtLeast(found string, required string) bool {
	foundParts := parseVersion(found)
	requiredParts := parseVersion(required)
	maxLen := len(foundParts)
	if len(requiredParts) > maxLen {
		maxLen = len(requiredParts)
	}

	for i := 0; i < maxLen; i++ {
		foundValue := 0
		requiredValue := 0
		if i < len(foundParts) {
			foundValue = foundParts[i]
		}
		if i < len(requiredParts) {
			requiredValue = requiredParts[i]
		}

		if foundValue > requiredValue {
			return true
		}
		if foundValue < requiredValue {
			return false
		}
	}

	return true
}

func parseVersion(value string) []int {
	parts := strings.Split(value, ".")
	result := make([]int, 0, len(parts))
	for _, part := range parts {
		num, err := strconv.Atoi(strings.TrimSpace(part))
		if err != nil {
			continue
		}
		result = append(result, num)
	}
	if len(result) == 0 {
		return []int{0}
	}
	return result
}

func persistBootstrapState(manifest RuntimeManifest, results []RuntimeValidationResult) {
	available := make([]string, 0, len(results))
	for _, result := range results {
		if result.Status == RuntimeStatusOK {
			available = append(available, result.Runtime.Name)
		}
	}
	sort.Strings(available)
 totalRequired := 0
	for _, runtime := range manifest.Runtimes {
		if runtime.Required {
			totalRequired++
		}
	}

	state := BootstrapState{
		Version: bootstrapStateVersion,
        Completed: len(available) >= totalRequired,
		CompletedAtUTC: time.Now().UTC().Format(time.RFC3339),
		Runtimes: available,
	}

	configDir, err := bootstrapConfigDirectory()
	if err != nil {
		return
	}

	if err = os.MkdirAll(configDir, 0755); err != nil {
		return
	}

	payload, err := json.MarshalIndent(state, "", "  ")
	if err != nil {
		return
	}

	_ = os.WriteFile(filepath.Join(configDir, "bootstrap_state.json"), payload, 0600)
}
func bootstrapConfigDirectory() (string, error) {
	baseDir, err := os.UserConfigDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(baseDir, "zeri"), nil
}

/*
bootstrap_manager.go
Implements manifest-driven runtime bootstrap and validation.
The runtime manifest is the single source of truth for required runtimes,
version checks, deterministic installer routing, and user-facing hints.
*/
