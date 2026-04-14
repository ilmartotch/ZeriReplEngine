package main

import (
	"fmt"
	"os"
	"path/filepath"
)

type PreflightError struct {
    Code string
	Check string
	Message string
	Hint string
}

func (e *PreflightError) Error() string {
   code := e.Code
	if code == "" {
		code = "PREFLIGHT_ERROR"
	}
	if e.Hint != "" {
        return fmt.Sprintf("[ZERI][%s] Check: %s\n %s\n Hint: %s", code,
			e.Check, e.Message, e.Hint)
	}
  return fmt.Sprintf("[ZERI][%s] Check: %s\n %s", code, e.Check, e.Message)
}

func socketPathFromPipe(pipeName string) string {
	path := pipeName
	if !filepath.IsAbs(path) {
		path = filepath.Join(os.TempDir(), path)
	}
	if filepath.Ext(path) == "" {
		path += ".sock"
	}
	return path
}

func RunPreflight(enginePath, pipeName string) []*PreflightError {
	var errs []*PreflightError
	errs = append(errs, platformPreflight()...)

	if _, err := os.Stat(enginePath); os.IsNotExist(err) {
		errs = append(errs, &PreflightError{
            Code: "ENGINE_NOT_FOUND",
			Check: "ZeriEngine",
          Message: fmt.Sprintf("Executable not found: %s", enginePath),
			Hint: "Ensure zeri and ZeriEngine are in the same directory.",
		})
	}

  manifest, err := loadRuntimeManifest()
	if err != nil {
		errs = append(errs, &PreflightError{
			Code: "BOOTSTRAP_MANIFEST_INVALID",
			Check: "Runtime Manifest",
			Message: err.Error(),
			Hint: "Fix runtime/runtime_manifest.json and retry.",
		})
	} else {
		for _, result := range validateRequiredRuntimes(manifest) {
           if result.Status == RuntimeStatusOK || !result.Runtime.Required {
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
	}

   if writableErr := checkTempWritable(); writableErr != nil {
		errs = append(errs, writableErr)
	}

	cleanStaleSocket(socketPathFromPipe(pipeName))
	return errs
}

func checkTempWritable() *PreflightError {
	probe := filepath.Join(os.TempDir(), ".zeri_write_probe")
	if err := os.WriteFile(probe, []byte{}, 0600); err != nil {
		return &PreflightError{
            Code: "TEMP_DIR_NOT_WRITABLE",
			Check: "Directory TEMP",
            Message: fmt.Sprintf("Cannot write in %s: %v", os.TempDir(), err),
			Hint: "Check permissions for the system temporary directory.",
		}
	}
	_ = os.Remove(probe)
	return nil
}
func cleanStaleSocket(sockPath string) {
	if _, err := os.Stat(sockPath); err == nil {
		_ = os.Remove(sockPath)
	}
}

/*
preflight.go — Runtime environment validation.

RunPreflight is called once at startup, before launching ZeriEngine.
All checks are collected into a slice so the user sees every problem
at once rather than a single blocking error.

cleanStaleSocket is unconditional: if a previous session crashed, the
socket file at %TEMP%\zeri-core.sock will block ZeriEngine from binding.
Removing it here ensures a clean startup every time.

Platform-specific checks (Windows version, VS Redistributable) are
split into preflight_windows.go / preflight_other.go to keep build
tags isolated from the common logic.

User-facing diagnostics are standardized as [ZERI][CODE].
*/
