package harness

import (
	"bytes"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"testing"
	"time"
)

const engineStartupTimeout = 5 * time.Second

type EngineProcess struct {
	t *testing.T
	cmd *exec.Cmd
	enginePath string
	endpoint string
	socketPath string
	sessionID string
	stderrBuf bytes.Buffer
	preConn net.Conn
	client *ZeriClient
	exitCh chan error
	cleanupOnce sync.Once
}

func SpawnEngine(t *testing.T) *EngineProcess {
	return spawnEngine(t, nil)
}

func SpawnEngineWithEnv(t *testing.T, env map[string]string) *EngineProcess {
	return spawnEngine(t, env)
}

func spawnEngine(t *testing.T, env map[string]string) *EngineProcess {
	t.Helper()

	enginePath := resolveEnginePath(t)
	sessionID := randomToken(t, 8)
	endpoint := fmt.Sprintf("zeri-integration-%d-%s", os.Getpid(), sessionID)
	socketPath := endpointSocketPath(endpoint)

	ep := &EngineProcess{
		t: t,
		enginePath: enginePath,
		endpoint: endpoint,
		socketPath: socketPath,
		sessionID: sessionID,
	}

	cmd := exec.Command(enginePath, "--yuumi-pipe", endpoint)
	cmd.Dir = filepath.Dir(enginePath)
	cmd.Stdout = io.Discard
	cmd.Stdin = nil
	cmd.Stderr = io.MultiWriter(&ep.stderrBuf)
	cmd.Env = buildEnvironment(env, sessionID)

	if err := cmd.Start(); err != nil {
		t.Fatalf("failed to start zeri-engine (%s): %v", enginePath, err)
	}
	ep.cmd = cmd
	ep.exitCh = make(chan error, 1)
	go func() {
		ep.exitCh <- cmd.Wait()
	}()

	start := time.Now()
	for time.Since(start) < engineStartupTimeout {
		select {
		case waitErr := <-ep.exitCh:
			if waitErr == nil {
				t.Fatalf("zeri-engine exited before becoming ready. stderr:\n%s", strings.TrimSpace(ep.stderrBuf.String()))
			}
			t.Fatalf("zeri-engine exited before becoming ready: %v. stderr:\n%s", waitErr, strings.TrimSpace(ep.stderrBuf.String()))
		default:
		}

		if ep.cmd.Process == nil {
			t.Fatalf("zeri-engine exited before becoming ready. stderr:\n%s", strings.TrimSpace(ep.stderrBuf.String()))
		}

		conn, err := tryDial(endpoint, 150*time.Millisecond)
		if err == nil {
			ep.preConn = conn
			t.Cleanup(func() {
				Cleanup(t, ep)
			})
			return ep
		}

		time.Sleep(100 * time.Millisecond)
	}

	_ = ep.cmd.Process.Kill()
	<-ep.exitCh
	t.Fatalf("zeri-engine did not expose IPC endpoint within %s (endpoint: %s). stderr:\n%s", engineStartupTimeout, endpoint, strings.TrimSpace(ep.stderrBuf.String()))
	return nil
}

func buildEnvironment(overrides map[string]string, sessionID string) []string {
	envMap := map[string]string{}
	for _, entry := range os.Environ() {
		parts := strings.SplitN(entry, "=", 2)
		if len(parts) != 2 {
			continue
		}
		envMap[parts[0]] = parts[1]
	}
	envMap["ZERI_SESSION_TOKEN"] = sessionID
	for key, value := range overrides {
		envMap[key] = value
	}

	entries := make([]string, 0, len(envMap))
	for key, value := range envMap {
		entries = append(entries, key+"="+value)
	}
	return entries
}

func resolveEnginePath(t *testing.T) string {
	t.Helper()

	if configured := strings.TrimSpace(os.Getenv("ZERI_ENGINE_PATH")); configured != "" {
		candidate := withPlatformExecutableSuffix(configured)
		if fileExists(candidate) {
			return candidate
		}
		t.Fatalf("ZERI_ENGINE_PATH is set but target does not exist: %s", candidate)
	}

	repoRoot := findRepoRoot(t)
	executableName := withPlatformExecutableSuffix("zeri-engine")
	candidates := []string{
		filepath.Join(repoRoot, "dist", executableName),
		filepath.Join(repoRoot, "build-release", "Release", executableName),
		filepath.Join(repoRoot, "build-release", executableName),
		filepath.Join(repoRoot, "build", "Release", executableName),
		filepath.Join(repoRoot, "build", executableName),
	}

	for _, candidate := range candidates {
		if fileExists(candidate) {
			return candidate
		}
	}

	if lookup, err := exec.LookPath(executableName); err == nil {
		return lookup
	}

	t.Fatalf("unable to locate zeri-engine. Set ZERI_ENGINE_PATH or build engine into dist/build-release. Checked: %s", strings.Join(candidates, ", "))
	return ""
}

func findRepoRoot(t *testing.T) string {
	t.Helper()

	wd, err := os.Getwd()
	if err != nil {
		t.Fatalf("unable to resolve working directory: %v", err)
	}

	current := wd
	for {
		cmakePath := filepath.Join(current, "CMakeLists.txt")
		uiModulePath := filepath.Join(current, "ui", "go.mod")
		if fileExists(cmakePath) && fileExists(uiModulePath) {
			return current
		}

		parent := filepath.Dir(current)
		if parent == current {
			break
		}
		current = parent
	}

	t.Fatalf("unable to locate repository root from %s", wd)
	return ""
}

func endpointSocketPath(endpoint string) string {
	if runtime.GOOS == "windows" {
		return ""
	}
	if filepath.IsAbs(endpoint) {
		if filepath.Ext(endpoint) == "" {
			return endpoint + ".sock"
		}
		return endpoint
	}
	path := filepath.Join(os.TempDir(), endpoint)
	if filepath.Ext(path) == "" {
		path += ".sock"
	}
	return path
}

func withPlatformExecutableSuffix(path string) string {
	if runtime.GOOS == "windows" && filepath.Ext(path) == "" {
		return path + ".exe"
	}
	return path
}

func fileExists(path string) bool {
	info, err := os.Stat(path)
	if err != nil {
		return false
	}
	return !info.IsDir()
}

func randomToken(t *testing.T, bytesCount int) string {
	t.Helper()

	raw := make([]byte, bytesCount)
	if _, err := rand.Read(raw); err != nil {
		t.Fatalf("failed generating session token: %v", err)
	}
	return hex.EncodeToString(raw)
}

func KillProcess(t *testing.T, ep *EngineProcess) {
	t.Helper()
	if ep == nil || ep.cmd == nil || ep.cmd.Process == nil {
		return
	}
	_ = ep.cmd.Process.Kill()
}
