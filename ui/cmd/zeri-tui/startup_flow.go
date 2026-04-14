package main

import (
	"context"
	"fmt"
	"os"
   "path/filepath"
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
	Errors []string
   LogPath string
}

type startupReadyMsg struct {
	Runner *yuumi.Runner
	Client *yuumi.Client
   Warnings []string
	LogPath string
}

func runStartupFlowAsync(ctx context.Context, p *tea.Program, bridgeClient *bridge.RealYuumiClient, enginePath string, pipeName string) {
	go func() {
        warnings := make([]string, 0)
		logPath := startupLogFilePath(enginePath)
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
			BinaryPath: enginePath,
			PipeName:   pipeName,
		}
		if err := runner.Start(ctx); err != nil {
          appendStartupLog(logPath, "ENGINE_START", []string{err.Error()})
          p.Send(startupFailedMsg{Errors: []string{"Engine startup failed. Check required permissions and local dependencies."}, LogPath: logPath})
			return
		}

		p.Send(startupPhaseMsg{Title: "Connecting bridge..."})
		client, err := yuumi.Connect(pipeName)
		if err != nil {
			runner.Stop()
         appendStartupLog(logPath, "ENGINE_CONNECT", []string{err.Error()})
            p.Send(startupFailedMsg{Errors: []string{"Bridge connection failed. Engine may be blocked by security policy."}, LogPath: logPath})
			return
		}

		runner.SetClient(client)
		bridgeClient.SetClient(client)
		bridgeClient.RegisterMessageHandler()
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

func startupLogFilePath(enginePath string) string {
	baseDir := filepath.Dir(enginePath)
	return filepath.Join(baseDir, "zeri-startup.log")
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
*/
