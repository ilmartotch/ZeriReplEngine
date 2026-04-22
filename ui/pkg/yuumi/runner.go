package yuumi

import (
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"time"
)

type Runner struct {
	BinaryPath string
	PipeName string
	OnCrash func(error)
	SessionTempDir string
	EngineLogPath string
	client *Client
	cmd *exec.Cmd
	logFile *os.File
	cancel context.CancelFunc
	stopOnce sync.Once
}

func (r *Runner) SetClient(c *Client) {
	r.client = c
}

func (r *Runner) Start(ctx context.Context) error {
	if strings.TrimSpace(r.SessionTempDir) == "" {
		dir, err := os.MkdirTemp(os.TempDir(), "zeri-project-session-")
		if err != nil {
			return err
		}
		r.SessionTempDir = dir
	}

	logsDir := filepath.Join(r.SessionTempDir, "logs")
	if err := os.MkdirAll(logsDir, 0755); err != nil {
		return err
	}

	ctx, r.cancel = context.WithCancel(ctx)
	r.cmd = exec.CommandContext(ctx, r.BinaryPath, "--yuumi-pipe", r.PipeName)
	r.cmd.Env = append(os.Environ(), "ZERI_SESSION_TEMP_DIR="+r.SessionTempDir)

	r.cmd.Stdout = io.Discard
	r.cmd.Stdin = nil

	r.EngineLogPath = filepath.Join(logsDir, "zeri-engine.log")
	if logFile, err := os.Create(r.EngineLogPath); err == nil {
		r.logFile = logFile
		r.cmd.Stderr = logFile
		fmt.Fprintf(logFile, "[RUNNER] Launching: %s --yuumi-pipe %s\n", r.BinaryPath, r.PipeName)
	} else {
		r.cmd.Stderr = io.Discard
	}

	if err := r.cmd.Start(); err != nil {
		return err
	}

	go r.watchProcess()
	go r.trapSignals()

	return nil
}

func (r *Runner) Stop() {
	r.stopOnce.Do(func() {
		if r.client != nil {
			_ = r.client.Shutdown()
			time.Sleep(150 * time.Millisecond)
		}

		if r.cmd != nil && r.cmd.Process != nil {
			_ = r.cmd.Process.Signal(os.Interrupt)
			done := make(chan struct{})
			go func() {
				_ = r.cmd.Wait()
				close(done)
			}()
			select {
			case <-done:
			case <-time.After(3 * time.Second):
				_ = r.cmd.Process.Kill()
			}
		}

		if r.cancel != nil {
			r.cancel()
		}

		if r.logFile != nil {
			_ = r.logFile.Close()
			r.logFile = nil
		}

		if strings.TrimSpace(r.SessionTempDir) != "" {
			_ = os.RemoveAll(r.SessionTempDir)
		}
	})
}

func (r *Runner) GetPID() int {
	if r.cmd != nil && r.cmd.Process != nil {
		return r.cmd.Process.Pid
	}
	return 0
}

func (r *Runner) watchProcess() {
	err := r.cmd.Wait()
	if err != nil && r.OnCrash != nil {
		r.OnCrash(err)
	}
}

func (r *Runner) trapSignals() {
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh
	signal.Stop(sigCh)
	r.Stop()
}

/*
 Runner: Child-process lifecycle manager for ZeriEngine.

 Start: Launches the C++ binary with --yuumi-pipe flag, spawns two
 background goroutines: watchProcess (monitors exit/crash) and
 trapSignals (intercepts SIGINT/SIGTERM for graceful shutdown).

 Stop: Idempotent shutdown sequence protected by sync.Once:
  1. Sends explicit {"type":"shutdown"} via pipe (Client.Shutdown).
  2. Waits 150ms for the engine to process the shutdown message.
  3. Sends os.Interrupt to the child process.
  4. Waits up to 3 seconds for graceful exit.
  5. Force-kills if the process does not exit within the grace period.
  6. Cancels the context to unblock any remaining goroutines.

 SetClient: Registers the IPC client so Stop() can send the shutdown
 signal via pipe before killing the process.

 trapSignals: Dedicated goroutine that blocks on SIGINT/SIGTERM and
 calls Stop() when received. Ensures the child process is cleaned up
 even if the TUI crashes or the user presses Ctrl+C.

 watchProcess: Monitors cmd.Wait() and invokes OnCrash callback if
 the child exits with an error.
*/
