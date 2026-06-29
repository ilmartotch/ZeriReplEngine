package integration

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"yuumi/test/integration/harness"
)

func switchContextOrFail(t *testing.T, client *harness.ZeriClient, ctx string) {
	t.Helper()
	response := harness.Send(client, ctx)
	if response.Reason != "context_transition" {
		t.Fatalf("expected context_transition for %s, got %q (%s)", ctx, response.Reason, responseDump(response.Output, response.Errors))
	}
}

func TestT35SandboxHelpAndUnknownCommand(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$sandbox")

	help := harness.Send(client, "/help")
	if !responseContains(help.Output, "/open") || !responseContains(help.Output, "/watch") || !responseContains(help.Output, "/list") || !responseContains(help.Output, "/build") || !responseContains(help.Output, "/run") {
		t.Fatalf("sandbox help is missing expected commands: %s", responseDump(help.Output, help.Errors))
	}

	unknown := harness.Send(client, "/unknown")
	if !responseContains(unknown.Errors, "SANDBOX_UNKNOWN") {
		t.Fatalf("expected SANDBOX_UNKNOWN for unknown command: %s", responseDump(unknown.Output, unknown.Errors))
	}
}

func TestT36SandboxWatchStatus(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$sandbox")

	response := harness.Send(client, "/watch")
	if !responseContains(response.Output, "sandbox process monitor:") {
		t.Fatalf("expected sandbox process monitor status: %s", responseDump(response.Output, response.Errors))
	}
}

func TestT37SandboxOpenUsesConfiguredIde(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$sandbox")

	open := harness.Send(client, "/open .")
	openSucceeded := responseContains(open.Output, "Open command dispatched.") || responseContains(open.Output, "Opened in IDE:")
	openFailedWithExplicitError := len(open.Errors) > 0 && (responseContains(open.Errors, "SANDBOX_OPEN_FAIL") || responseContains(open.Errors, "Failed to open target in IDE."))
	if !openSucceeded && !openFailedWithExplicitError {
		t.Fatalf("expected /open to either succeed or return explicit open error: %s", responseDump(open.Output, open.Errors))
	}
}

func TestT38SandboxBlockingExternalRun(t *testing.T) {
	if !hasAnyBinary("python3", "python") {
		t.Skip("Python runtime not available in PATH")
	}

	scriptDir := t.TempDir()
	scriptPath := filepath.Join(scriptDir, "sandbox_t38.py")
	if err := os.WriteFile(scriptPath, []byte("print('sandbox-run-ok')\n"), 0o644); err != nil {
		t.Fatalf("failed writing sandbox script: %v", err)
	}

	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$sandbox")

	response := harness.Send(client, scriptPath)
	if len(response.Errors) > 0 {
		t.Fatalf("unexpected sandbox execution error: %s", responseDump(response.Output, response.Errors))
	}
	if !responseContains(response.Output, "sandbox-run-ok") {
		t.Fatalf("expected sandbox script output, got: %s", responseDump(response.Output, response.Errors))
	}
}

func TestSandboxBuildMissingArgsReturnsError(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$sandbox")

	response := harness.Send(client, "/build")
	if !responseContains(response.Errors, "SANDBOX_MISSING_ARGS") {
		t.Fatalf("expected SANDBOX_MISSING_ARGS for /build without module name: %s", responseDump(response.Output, response.Errors))
	}
}

func TestSandboxRunMissingArgsReturnsError(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$sandbox")

	response := harness.Send(client, "/run")
	if !responseContains(response.Errors, "SANDBOX_MISSING_ARGS") {
		t.Fatalf("expected SANDBOX_MISSING_ARGS for /run without target: %s", responseDump(response.Output, response.Errors))
	}
}

func TestCustomCommandDefineListRunShowDelete(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$customCommand")

	define := harness.Send(client, "/define greet \"echo hello\"")
	if !responseContains(define.Output, "Custom command defined: greet") {
		t.Fatalf("expected define success, got: %s", responseDump(define.Output, define.Errors))
	}

	list := harness.Send(client, "/list")
	if !responseContains(list.Output, "greet") {
		t.Fatalf("expected greet in /list output: %s", responseDump(list.Output, list.Errors))
	}

	show := harness.Send(client, "/show greet")
	if !responseContains(show.Output, "echo hello") {
		t.Fatalf("expected body in /show output: %s", responseDump(show.Output, show.Errors))
	}

	run := harness.Send(client, "/run greet")
	if !responseContains(run.Output, "echo hello") {
		t.Fatalf("expected body in /run output: %s", responseDump(run.Output, run.Errors))
	}

	del := harness.Send(client, "/delete greet")
	if !responseContains(del.Output, "Custom command deleted: greet") {
		t.Fatalf("expected delete success: %s", responseDump(del.Output, del.Errors))
	}

	runDeleted := harness.Send(client, "/run greet")
	if !responseContains(runDeleted.Errors, "CUSTOM_NOT_FOUND") {
		t.Fatalf("expected CUSTOM_NOT_FOUND after delete: %s", responseDump(runDeleted.Output, runDeleted.Errors))
	}
}

func TestCustomCommandDefineMissingArgsError(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$customCommand")

	response := harness.Send(client, "/define onlyName")
	if !responseContains(response.Errors, "CUSTOM_DEFINE_MISSING_ARGS") {
		t.Fatalf("expected CUSTOM_DEFINE_MISSING_ARGS: %s", responseDump(response.Output, response.Errors))
	}
}

func TestCustomCommandRunMissingNameError(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$customCommand")

	response := harness.Send(client, "/run")
	if !responseContains(response.Errors, "CUSTOM_RUN_MISSING_NAME") {
		t.Fatalf("expected CUSTOM_RUN_MISSING_NAME: %s", responseDump(response.Output, response.Errors))
	}
}

func TestCustomCommandDeleteMissingNameError(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$customCommand")

	response := harness.Send(client, "/delete")
	if !responseContains(response.Errors, "CUSTOM_DELETE_MISSING_NAME") {
		t.Fatalf("expected CUSTOM_DELETE_MISSING_NAME: %s", responseDump(response.Output, response.Errors))
	}
}

func TestMathContextOperations(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$math")

	assign := harness.Send(client, "x = 42")
	if len(assign.Errors) > 0 {
		t.Fatalf("unexpected assignment error: %s", responseDump(assign.Output, assign.Errors))
	}

	vars := harness.Send(client, "/vars")
	if !responseContains(vars.Output, "x = 42") {
		t.Fatalf("expected x in /vars output: %s", responseDump(vars.Output, vars.Errors))
	}

	fn := harness.Send(client, "/fn f(x)=x*sin(x)")
	if !responseContains(fn.Output, "[FunctionDefined]") {
		t.Fatalf("expected function defined response: %s", responseDump(fn.Output, fn.Errors))
	}

	call := harness.Send(client, "f(1.2)")
	if len(call.Errors) > 0 {
		t.Fatalf("unexpected function call error: %s", responseDump(call.Output, call.Errors))
	}

	fns := harness.Send(client, "/fns")
	if !responseContains(fns.Output, "f(x)") {
		t.Fatalf("expected f(x) in /fns output: %s", responseDump(fns.Output, fns.Errors))
	}

	promote := harness.Send(client, "/promote x session")
	if !responseContains(promote.Output, "[Promoted] x -> session") {
		t.Fatalf("expected promote success: %s", responseDump(promote.Output, promote.Errors))
	}
}

func TestMathCalcOperation(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$math")

	response := harness.Send(client, "/calc 2 * 8")
	if !responseContains(response.Output, "2 * 8 = 16") {
		t.Fatalf("expected /calc result output: %s", responseDump(response.Output, response.Errors))
	}
}

func TestMathLogicOperation(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$math")

	response := harness.Send(client, "/logic and true false")
	if !responseContains(response.Output, "false") {
		t.Fatalf("expected /logic result output: %s", responseDump(response.Output, response.Errors))
	}
}

func TestMathInvalidLogicOperatorError(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)
	switchContextOrFail(t, client, "$math")

	response := harness.Send(client, "/logic nand true false")
	if !responseContains(response.Errors, "LogicOperator") {
		t.Fatalf("expected LogicOperator error: %s", responseDump(response.Output, response.Errors))
	}
}

func TestErrorMalformedInputPath(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)

	response := harness.Send(client, "/set \"unterminated")
	if len(response.Errors) == 0 {
		t.Fatalf("expected parse error for malformed quoted input: %s", responseDump(response.Output, response.Errors))
	}
}

func TestT50EnglishOnlyUserMessages(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)

	globalHelp := harness.Send(client, "/help")
	allText := strings.ToLower(strings.Join(globalHelp.Output, "\n") + "\n" + strings.Join(globalHelp.Errors, "\n"))
	for _, token := range []string{
		"perc" + "hé",
		"ques" + "to",
		"del" + "la",
		"de" + "gli",
		"conte" + "sto",
		"coma" + "ndo",
		"funzi" + "one",
		"ese" + "gui",
	} {
		if strings.Contains(allText, token) {
			t.Fatalf("found non-English token %q in output: %s", token, responseDump(globalHelp.Output, globalHelp.Errors))
		}
	}
}

func TestT51ErrorFormatConsistency(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)

	switchContextOrFail(t, client, "$sandbox")
	response := harness.Send(client, "/unknown")
	if len(response.Errors) == 0 {
		t.Fatalf("expected error output from /unknown command")
	}
	if !responseContains(response.Errors, "[ZERI][") {
		t.Fatalf("expected [ZERI][ prefix in sandbox error output: %s", responseDump(response.Output, response.Errors))
	}
	if !responseContains(response.Errors, "Hint:") {
		t.Fatalf("expected actionable hint in sandbox error output: %s", responseDump(response.Output, response.Errors))
	}
}

func TestT52NoCppOrRustReferencesInHelpOutput(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)
	client := harness.Connect(ep)

	checkNoCppRust := func(response harness.Response) {
		text := strings.ToLower(strings.Join(response.Output, "\n") + "\n" + strings.Join(response.Errors, "\n"))
		if strings.Contains(text, "c++") || strings.Contains(text, "rust") {
			t.Fatalf("found C++/Rust reference in user-facing output: %s", responseDump(response.Output, response.Errors))
		}
	}

	checkNoCppRust(harness.Send(client, "/help"))
	switchContextOrFail(t, client, "$code")
	checkNoCppRust(harness.Send(client, "/help"))
	switchContextOrFail(t, client, "$global")
	switchContextOrFail(t, client, "$sandbox")
	checkNoCppRust(harness.Send(client, "/help"))
}
