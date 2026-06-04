package integration

import (
	"context"
	"os/exec"
	"strings"
	"testing"
	"time"

	"yuumi/test/integration/harness"
)

func TestStartupEngineAndHelp(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	response := harness.Send(client, "/help")
	if len(response.Output) == 0 {
		t.Fatalf("expected non-empty help output, got %s", responseDump(response.Output, response.Errors))
	}
}

func TestStartupZeriVersionOutput(t *testing.T) {
	zeriPath, ok := resolveZeriBinaryPath(t)
	if !ok {
		t.Skip("zeri binary not found in dist/build outputs")
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	output, err := exec.CommandContext(ctx, zeriPath, "--version").CombinedOutput()
	if ctx.Err() == context.DeadlineExceeded && len(strings.TrimSpace(string(output))) == 0 {
		t.Fatalf("version command timed out with empty output")
	}
	if err != nil && len(strings.TrimSpace(string(output))) == 0 {
		t.Fatalf("version command failed with empty output: %v", err)
	}

	if len(strings.TrimSpace(string(output))) == 0 {
		t.Fatalf("expected non-empty --version output")
	}
}
