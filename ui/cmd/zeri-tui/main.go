package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"
	"yuumi/internal/bridge"
	"yuumi/internal/ui"

	tea "charm.land/bubbletea/v2"
)

var version = "dev"

func main() {
	ui.Version = version
	if shouldPrintVersion(os.Args[1:]) {
		fmt.Println(formatVersionOutput())
		os.Exit(0)
	}
	opts, err := parseAppOptions(os.Args[1:])
	if err != nil {
		fmt.Fprintf(os.Stderr, "[ZERI][CLI-001] %v. Hint: run zeri --version or use supported options --no-onboarding --reset-onboarding --profile-startup --exit-after-ready.\n", err)
		os.Exit(1)
	}

	if opts.resetOnboarding {
		if err := ResetOnboarding(); err != nil {
			fmt.Fprintf(os.Stderr, "[ZERI][SESSION-012] Unable to reset onboarding state: %v. Hint: verify write permissions for the Zeri config home.\n", err)
			os.Exit(1)
		}
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	profiler := newStartupProfiler(opts.profileStartup)

	if err := initializeDataLocation(opts.noOnboarding); err != nil {
		fmt.Fprintf(os.Stderr, "[ZERI][SESSION-010] Storage initialization failed: %v. Hint: verify write permissions for user data directories.\n", err)
		os.Exit(1)
	}
	profiler.Mark("binary entry → preflight complete")

	engineName := "zeri-engine"
	if runtime.GOOS == "windows" {
		engineName = "zeri-engine.exe"
	}

	envEnginePath, hasEnvEnginePath := os.LookupEnv("ZERI_ENGINE_PATH")
	enginePath := ""

	if hasEnvEnginePath {
		enginePath = envEnginePath
		if runtime.GOOS == "windows" && filepath.Ext(enginePath) == "" {
			enginePath += ".exe"
		}
	} else {
		execPath, err := os.Executable()
		if err != nil {
			fmt.Fprintf(os.Stderr, "[ZERI][SESSION-011] Unable to resolve executable path: %v. Hint: reinstall zeri or launch it from a valid installation directory.\n", err)
			os.Exit(1)
		}
		execDir := filepath.Dir(execPath)
		enginePath = filepath.Join(execDir, engineName)
	}

	pipeName := resolvePipeName()

	realBridge := bridge.NewRealYuumiClient(nil)
	m := newAppModel(realBridge, enginePath, pipeName, opts, profiler)

	p := tea.NewProgram(m)

	realBridge.SetProgram(p)
	runStartupFlowAsync(ctx, p, realBridge, enginePath, pipeName, profiler)

	finalModel, err := p.Run()
	if err != nil {
		fmt.Fprintf(os.Stderr, "[ZERI][IPC-010] TUI runtime error: %v. Hint: restart zeri and check engine startup diagnostics with /runtime-status.\n", err)
		os.Exit(1)
	}

	if model, ok := finalModel.(AppModel); ok {
		model.CloseRuntimeResources()
	}

	if !opts.profileStartup && !opts.exitAfterReady {
		fmt.Println("Goodbye from Zeri.")
	}
	os.Exit(0)
}

func shouldPrintVersion(args []string) bool {
	for _, arg := range args {
		if arg == "--version" || arg == "-v" {
			return true
		}
	}
	return false
}

func formatVersionOutput() string {
	normalizedVersion := strings.TrimSpace(version)
	if normalizedVersion == "" {
		normalizedVersion = "dev"
	}
	normalizedVersion = strings.TrimPrefix(normalizedVersion, "v")
	return fmt.Sprintf("zeri version %s (%s/%s)", normalizedVersion, runtime.GOOS, runtime.GOARCH)
}

func resolvePipeName() string {
	fromEnv := strings.TrimSpace(os.Getenv("ZERI_PIPE_NAME"))
	if fromEnv != "" {
		return fromEnv
	}
	sessionToken, err := generateSessionToken(8)
	if err != nil {
		return fmt.Sprintf("zeri-core-%d-%d", os.Getpid(), time.Now().UnixNano())
	}
	return fmt.Sprintf("zeri-core-%d-%s", os.Getpid(), sessionToken)
}

func generateSessionToken(byteCount int) (string, error) {
	if byteCount <= 0 {
		return "", fmt.Errorf("byteCount must be positive")
	}
	buffer := make([]byte, byteCount)
	if _, err := rand.Read(buffer); err != nil {
		return "", err
	}
	return hex.EncodeToString(buffer), nil
}

/*
 * What:
 *   - Rewritten for Bubble Tea v2 with the new bridge architecture.
 *   - Uses bridge.RealYuumiClient instead of raw yuumi.Client in model.
 *   - Pending message queue removed — bridge handles message forwarding
 *     via RegisterMessageHandler + p.Send().
 *   - Alt-screen mode via tea.WithAltScreen().
 *   - Prints "Goodbye from Zeri." after p.Run() returns.
 *   - Error messages in English (was Italian).
 *   - IPC endpoint is now per-process (unique pipe/socket name) so multiple
 *     zeri instances can run concurrently without bridge collisions.
 *
 * Why:
 *   - Aligns with v3 spec: bridge interface decouples TUI from IPC.
 *   - Simplified startup: no manual pending queue, no double OnMessage.
 *   - Clean shutdown via bridge.SendShutdownCmd + runner.Stop.
 *
 * Impact on other components:
 *   - model.go receives a bridge.YuumiClient interface.
 *   - bridge/yuumi_client.go handles raw message dispatch.
 *   - preflight.go unchanged — still validates environment.
 *   - Each session uses its own transport endpoint unless ZERI_PIPE_NAME
 *     is explicitly provided.
 *
 * Future maintenance notes:
 *   - To support --no-engine flag for UI-only development, skip
 *     runner.Start and use a mock YuumiClient.
 */
