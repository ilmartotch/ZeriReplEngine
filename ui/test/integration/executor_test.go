package integration

import (
	"os"
	"path/filepath"
	"runtime"
	"testing"

	"yuumi/test/integration/harness"
)

func TestExecutorLuaRoundTrip(t *testing.T) {
	if !hasAnyBinary("luajit", "lua") {
		t.Skip("Lua/LuaJIT runtime not available in PATH")
	}
	runLanguageHelloTest(t, "$lua", "print('hello')", "hello")
}

func TestExecutorPythonRoundTrip(t *testing.T) {
	if !hasAnyBinary("python3", "python") {
		t.Skip("Python runtime not available in PATH")
	}
	runLanguageHelloTest(t, "$python", "print('hello')", "hello")
}

func TestExecutorJsRoundTrip(t *testing.T) {
	if !hasAnyBinary("bun", "deno", "node") {
		t.Skip("JS runtime not available in PATH")
	}
	runLanguageHelloTest(t, "$js", "console.log('hello')", "hello")
}

func TestExecutorRubyRoundTrip(t *testing.T) {
	if !hasAnyBinary("ruby") {
		t.Skip("Ruby runtime not available in PATH")
	}
	runLanguageHelloTest(t, "$ruby", "puts 'hello'", "hello")
}

func TestExecutorRuntimeMissingReturnsError(t *testing.T) {
	env, _ := isolatedUserEnv(t)
	if runtime.GOOS == "windows" {
		systemRoot := os.Getenv("SystemRoot")
		if systemRoot == "" {
			systemRoot = `C:\Windows`
		}
		env["PATH"] = filepath.Join(systemRoot, "System32")
	} else {
		env["PATH"] = "/usr/sbin:/sbin"
	}

	ep := harness.SpawnEngineWithEnv(t, env)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	_ = harness.Send(client, "$code")
	_ = harness.Send(client, "$python")
	response := harness.Send(client, "print('hello')")
	if len(response.Errors) == 0 {
		t.Fatalf("expected runtime-missing error, got %s", responseDump(response.Output, response.Errors))
	}
}

func runLanguageHelloTest(t *testing.T, langContext string, code string, expected string) {
	t.Helper()

	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	codeHub := harness.Send(client, "$code")
	if codeHub.Reason != "context_transition" {
		t.Fatalf("expected transition to code context, got %q", codeHub.Reason)
	}

	langSwitch := harness.Send(client, langContext)
	if langSwitch.Reason != "context_transition" {
		t.Fatalf("expected language context transition for %s, got %q", langContext, langSwitch.Reason)
	}

	response := harness.Send(client, code)
	if len(response.Errors) > 0 {
		t.Fatalf("unexpected executor errors in %s: %s", langContext, responseDump(response.Output, response.Errors))
	}
	if !responseContains(response.Output, expected) {
		t.Fatalf("expected %q in output for %s, got %s", expected, langContext, responseDump(response.Output, response.Errors))
	}
}
