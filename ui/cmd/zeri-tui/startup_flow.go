package main

import (
	"context"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"
	"yuumi/internal/bridge"
	"yuumi/pkg/yuumi"

	tea "charm.land/bubbletea/v2"
)

type startupPhaseMsg struct {
	Title string
}

type startupFailedMsg struct {
	Errors  []string
	LogPath string
}

type startupReadyMsg struct {
	Runner   *yuumi.Runner
	Client   *yuumi.Client
	Warnings []string
	LogPath  string
}

func runStartupFlowAsync(ctx context.Context, p *tea.Program, bridgeClient *bridge.RealYuumiClient, enginePath string, pipeName string, profiler *startupProfiler) {
	go func() {
		warnings := make([]string, 0)
		sessionTempDir := resolveSessionTempDir()
		logPath := startupLogFilePath(sessionTempDir)
		_ = os.MkdirAll(filepath.Dir(logPath), 0755)

		p.Send(startupPhaseMsg{Title: "Bootstrapping required runtimes..."})
		_ = os.Setenv(bootstrapModeEnv, bootstrapModeValidate)

		bootstrapErrs := RunBootstrapManager()
		if len(bootstrapErrs) > 0 {
			appendStartupLog(logPath, "BOOTSTRAP", preflightErrorsToLines(bootstrapErrs))
			warnings = append(warnings, summarizeIssues("Bootstrap diagnostics", bootstrapErrs)...)
		}

		p.Send(startupPhaseMsg{Title: "Running environment checks..."})
		preflightErrs := RunPreflight(enginePath, pipeName)
		fatalErrs, nonFatalErrs := splitPreflightErrors(preflightErrs)
		if len(nonFatalErrs) > 0 {
			appendStartupLog(logPath, "PREFLIGHT_NON_FATAL", preflightErrorsToLines(nonFatalErrs))
			warnings = append(warnings, summarizeIssues("Optional tools need attention", nonFatalErrs)...)
		}
		if len(fatalErrs) > 0 {
			appendStartupLog(logPath, "PREFLIGHT_FATAL", preflightErrorsToLines(fatalErrs))
			p.Send(startupFailedMsg{Errors: summarizeIssues("Environment blocking issues", fatalErrs), LogPath: logPath})
			return
		}

		p.Send(startupPhaseMsg{Title: "Starting ZeriEngine..."})
		runner := &yuumi.Runner{
			BinaryPath:     enginePath,
			PipeName:       pipeName,
			SessionTempDir: sessionTempDir,
		}
		if err := runner.Start(ctx); err != nil {
			appendStartupLog(logPath, "ENGINE_START", []string{err.Error()})
			p.Send(startupFailedMsg{Errors: []string{"Engine startup failed. Check required permissions and local dependencies."}, LogPath: logPath})
			return
		}
		if profiler != nil {
			profiler.Mark("engine spawn → IPC socket available")
		}

		p.Send(startupPhaseMsg{Title: "Connecting bridge..."})
		client, err := yuumi.Connect(pipeName)
		if err != nil {
			appendStartupLog(logPath, "ENGINE_CONNECT", []string{err.Error()})
			appendStartupLog(logPath, "EXECUTION_CONTEXT", collectExecutionContextDiagnostics(enginePath, pipeName, sessionTempDir))
			engineLogLines := readEngineLogLines(runner.EngineLogPath)
			if len(engineLogLines) > 0 {
				appendStartupLog(logPath, "ENGINE_STDERR", engineLogLines)
			}
			runner.PreserveSessionTempDir = true
			runner.Stop()
			userErrors := buildBridgeFailureErrors(err, engineLogLines, enginePath, logPath)
			if runner.EngineLogPath != "" {
				userErrors = append(userErrors, "Engine stderr log: "+runner.EngineLogPath)
			}
			p.Send(startupFailedMsg{Errors: userErrors, LogPath: logPath})
			return
		}
		if profiler != nil {
			profiler.Mark("IPC connect → handshake validated")
		}

		runner.SetClient(client)
		bridgeClient.SetClient(client)
		runner.OnCrash = func(err error) {
			p.Send(bridge.DisconnectedMsg{Reason: err.Error()})
		}

		p.Send(startupReadyMsg{Runner: runner, Client: client, Warnings: warnings, LogPath: logPath})
	}()
}

func splitPreflightErrors(errs []*PreflightError) ([]*PreflightError, []*PreflightError) {
	fatal := make([]*PreflightError, 0)
	nonFatal := make([]*PreflightError, 0)
	for _, err := range errs {
		if isFatalStartupCode(err.Code) {
			fatal = append(fatal, err)
			continue
		}
		nonFatal = append(nonFatal, err)
	}
	return fatal, nonFatal
}

func isFatalStartupCode(code string) bool {
	switch strings.TrimSpace(strings.ToUpper(code)) {
	case "ENGINE_NOT_FOUND", "VC_REDIST_MISSING", "TEMP_DIR_NOT_WRITABLE":
		return true
	default:
		return false
	}
}

func summarizeIssues(title string, errs []*PreflightError) []string {
	if len(errs) == 0 {
		return nil
	}
	lines := []string{title + ":"}
	for _, err := range errs {
		message := strings.TrimSpace(err.Message)
		hint := strings.TrimSpace(err.Hint)
		if message == "" {
			message = "Unknown issue"
		}
		if hint != "" {
			lines = append(lines, "- "+message+" | "+hint)
		} else {
			lines = append(lines, "- "+message)
		}
	}
	return lines
}

func resolveSessionTempDir() string {
	fromEnv := strings.TrimSpace(os.Getenv("ZERI_SESSION_TEMP_DIR"))
	if fromEnv != "" {
		return fromEnv
	}
	dir, err := os.MkdirTemp(os.TempDir(), "zeri-project-session-")
	if err != nil {
		return filepath.Join(os.TempDir(), "zeri-project-session-fallback")
	}
	return dir
}

func startupLogFilePath(sessionTempDir string) string {
	baseDir := strings.TrimSpace(sessionTempDir)
	if baseDir == "" {
		baseDir = filepath.Join(os.TempDir(), "zeri-project-session-fallback")
	}
	return filepath.Join(baseDir, "logs", "zeri-startup.log")
}

func appendStartupLog(path string, section string, lines []string) {
	if strings.TrimSpace(path) == "" || len(lines) == 0 {
		return
	}
	file, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0600)
	if err != nil {
		return
	}
	defer file.Close()

	timestamp := time.Now().UTC().Format(time.RFC3339)
	_, _ = fmt.Fprintf(file, "[%s][%s]\n", timestamp, section)
	for _, line := range lines {
		_, _ = fmt.Fprintln(file, line)
	}
	_, _ = fmt.Fprintln(file)
}

func readEngineLogLines(engineLogPath string) []string {
	if strings.TrimSpace(engineLogPath) == "" {
		return nil
	}
	engineLogBytes, err := os.ReadFile(engineLogPath)
	if err != nil || len(engineLogBytes) == 0 {
		return nil
	}
	lines := strings.Split(strings.TrimRight(string(engineLogBytes), "\r\n"), "\n")
	filtered := make([]string, 0, len(lines))
	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" {
			continue
		}
		filtered = append(filtered, trimmed)
	}
	return filtered
}

func buildBridgeFailureErrors(connectErr error, engineLogLines []string, enginePath string, logPath string) []string {
	var protocolMismatchErr *yuumi.AppProtocolVersionMismatchError
	if errors.As(connectErr, &protocolMismatchErr) {
		return []string{protocolMismatchErr.Error()}
	}

	errors := []string{
		"Bridge connection failed: " + strings.TrimSpace(connectErr.Error()),
	}
	if len(engineLogLines) > 0 {
		errors = append(errors, "Engine error: "+engineLogLines[len(engineLogLines)-1])
	} else {
		errors = append(errors, "Engine error: no stderr output captured.")
	}
	errors = append(errors, "Troubleshooting:")
	errors = append(errors, "- If running from ZIP, unblock files and run setup script (setup.ps1 or setup.sh).")
	errors = append(errors, "- If running from another volume, verify execute/read permissions on that volume and folder.")
	if runtime.GOOS == "windows" {
		errors = append(errors, "- Check SmartScreen/Defender or WDAC/AppLocker policies for this executable path.")
		errors = append(errors, "- PowerShell check for Mark-of-the-Web: Get-Item -Path '"+enginePath+"' -Stream Zone.Identifier")
	}
	if strings.TrimSpace(logPath) != "" {
		errors = append(errors, "- Inspect startup log and ENGINE_STDERR sections for root cause.")
	}
	return errors
}

func collectExecutionContextDiagnostics(enginePath string, pipeName string, sessionTempDir string) []string {
	cwd, cwdErr := os.Getwd()
	lines := []string{
		"engine_path=" + strings.TrimSpace(enginePath),
		"engine_dir=" + filepath.Dir(enginePath),
		"engine_volume=" + filepath.VolumeName(enginePath),
		"ipc_endpoint=" + strings.TrimSpace(pipeName),
		"temp_dir=" + os.TempDir(),
		"temp_volume=" + filepath.VolumeName(os.TempDir()),
		"session_temp_dir=" + strings.TrimSpace(sessionTempDir),
	}
	if cwdErr != nil {
		lines = append(lines, "cwd=<unavailable>: "+cwdErr.Error())
	} else {
		lines = append(lines, "cwd="+cwd)
		lines = append(lines, "cwd_volume="+filepath.VolumeName(cwd))
	}
	if runtime.GOOS == "windows" {
		zoneStreamPath := enginePath + ":Zone.Identifier"
		if _, err := os.Stat(zoneStreamPath); err == nil {
			lines = append(lines, "motw_zone_identifier=present")
		} else {
			lines = append(lines, "motw_zone_identifier=not-detected")
		}
	}
	return lines
}

func preflightErrorsToLines(errs []*PreflightError) []string {
	lines := make([]string, 0)
	for _, err := range errs {
		for _, line := range strings.Split(err.Error(), "\n") {
			trimmed := strings.TrimSpace(line)
			if trimmed == "" {
				continue
			}
			lines = append(lines, trimmed)
		}
	}
	return lines
}

/*
startup_flow.go
Runs asynchronous startup initialization after the TUI is already visible.
The flow emits live phase messages and final readiness/failure messages so the UI
can render progress and clear diagnostics while keeping startup responsive.

When yuumi.Connect fails, diagnostics are now persisted before cleanup:
- ENGINE_CONNECT with the concrete connect error.
- EXECUTION_CONTEXT with engine path, cwd, volume info, temp paths, and Windows MOTW signal.
- ENGINE_STDERR with captured engine stderr lines.

The UI failure message now includes the concrete bridge error and latest engine stderr line
instead of a generic "security policy" statement.

On startup failure we set runner.PreserveSessionTempDir=true before Stop(), so logs are not
deleted and the user can inspect the exact startup evidence from the reported path.
*/
