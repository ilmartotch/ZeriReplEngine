package main

import (
	"fmt"
	"os"
	"path/filepath"
)

// PreflightError is a structured check failure with a user-facing hint.
type PreflightError struct {
	Check   string
	Message string
	Hint    string
}

func (e *PreflightError) Error() string {
	if e.Hint != "" {
		return fmt.Sprintf("[ZERI] Controllo: %s\n       %s\n       Suggerimento: %s",
			e.Check, e.Message, e.Hint)
	}
	return fmt.Sprintf("[ZERI] Controllo: %s\n       %s", e.Check, e.Message)
}

// socketPathFromPipe derives the UDS socket path from the pipe name,
// matching the logic in pkg/yuumi/transport_unix.go.
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

// RunPreflight runs all startup checks and returns any failures.
// enginePath: resolved path to ZeriEngine binary.
// pipeName:   socket identifier (e.g. "zeri-core").
//
// Checks are non-fatal individually — all are collected and returned so
// main() can print all problems at once instead of failing on the first.
// The stale socket cleanup is always performed regardless of other errors.
func RunPreflight(enginePath, pipeName string) []*PreflightError {
	var errs []*PreflightError

	// Platform-specific checks (Windows version, VS Redistributable).
	errs = append(errs, platformPreflight()...)

	// ZeriEngine binary present.
	if _, err := os.Stat(enginePath); os.IsNotExist(err) {
		errs = append(errs, &PreflightError{
			Check:   "ZeriEngine",
			Message: fmt.Sprintf("Eseguibile non trovato: %s", enginePath),
			Hint:    "Assicurati che zeri.exe e ZeriEngine.exe siano nella stessa cartella.",
		})
	}

	// TEMP directory writable (socket will be created here).
	if err := checkTempWritable(); err != nil {
		errs = append(errs, err)
	}

	// Remove stale socket from a previous crash — always run, not an error.
	cleanStaleSocket(socketPathFromPipe(pipeName))

	return errs
}

func checkTempWritable() *PreflightError {
	probe := filepath.Join(os.TempDir(), ".zeri_write_probe")
	if err := os.WriteFile(probe, []byte{}, 0600); err != nil {
		return &PreflightError{
			Check:   "Directory TEMP",
			Message: fmt.Sprintf("Impossibile scrivere in %s: %v", os.TempDir(), err),
			Hint:    "Controlla i permessi della directory temporanea di sistema.",
		}
	}
	_ = os.Remove(probe)
	return nil
}

// cleanStaleSocket removes a leftover socket file from a previous crashed session.
// If it exists and is not removed, the next ZeriEngine bind will fail silently.
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
*/
