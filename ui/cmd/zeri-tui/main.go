package main

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"yuumi/internal/bridge"
	"yuumi/pkg/yuumi"

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

	if preflightErrs := RunPreflight(enginePath, pipeName); len(preflightErrs) > 0 {
		for _, e := range preflightErrs {
			fmt.Fprintln(os.Stderr, e.Error())
		}
		os.Exit(1)
	}

	runner := &yuumi.Runner{
		BinaryPath: enginePath,
		PipeName:   pipeName,
	}
	if err := runner.Start(ctx); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to start ZeriEngine: %v\n", err)
		os.Exit(1)
	}
	defer runner.Stop()

	client, err := yuumi.Connect(pipeName)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Failed to connect to ZeriEngine: %v\n", err)
		os.Exit(1)
	}
	defer client.Close()

	runner.SetClient(client)

	realBridge := bridge.NewRealYuumiClient(client)
	m := newAppModel(realBridge)

	p := tea.NewProgram(m)

	realBridge.SetProgram(p)
	realBridge.RegisterMessageHandler()

	runner.OnCrash = func(err error) {
		p.Send(bridge.DisconnectedMsg{Reason: err.Error()})
	}

	if _, err := p.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "TUI error: %v\n", err)
		os.Exit(1)
	}

	fmt.Println("Goodbye from Zeri.")
	os.Exit(0)
}

/*
 * CHANGES & RATIONALE
 * -------------------
 * [main.go]
 *
 * What changed:
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
