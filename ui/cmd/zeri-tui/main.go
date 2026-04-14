package main

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"yuumi/internal/bridge"

	tea "charm.land/bubbletea/v2"
)

func main() {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	engineName := "ZeriEngine"
	if runtime.GOOS == "windows" {
		engineName = "ZeriEngine.exe"
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
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}
		execDir := filepath.Dir(execPath)
		enginePath = filepath.Join(execDir, engineName)
	}

	pipeName := "zeri-core"

	realBridge := bridge.NewRealYuumiClient(nil)
	m := newAppModel(realBridge)

	p := tea.NewProgram(m)

	realBridge.SetProgram(p)
 runStartupFlowAsync(ctx, p, realBridge, enginePath, pipeName)

  finalModel, err := p.Run()
	if err != nil {
		fmt.Fprintf(os.Stderr, "TUI error: %v\n", err)
		os.Exit(1)
	}

	if model, ok := finalModel.(AppModel); ok {
		model.CloseRuntimeResources()
	}

	fmt.Println("Goodbye from Zeri.")
	os.Exit(0)
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
 *
 * Future maintenance notes:
 *   - To support --no-engine flag for UI-only development, skip
 *     runner.Start and use a mock YuumiClient.
 */
