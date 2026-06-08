package integration

import (
	"runtime"
	"sync"
	"testing"

	"yuumi/test/integration/harness"
)

func TestCrossLangPythonToLua(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("runtime not available")
	}
	if !hasAnyBinary("python3", "python") || !hasAnyBinary("luajit", "lua") {
		t.Skip("runtime not available")
	}

	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	_ = harness.Send(client, "$code")
	_ = harness.Send(client, "$python")
	_ = harness.Send(client, `zeri.set("shared_x", 42)`)

	_ = harness.Send(client, "/back")
	_ = harness.Send(client, "$lua")
	response := harness.Send(client, `assert(zeri.get("shared_x") == 42); print("ok")`)
	if len(response.Errors) > 0 {
		t.Fatalf("unexpected errors: %s", responseDump(response.Output, response.Errors))
	}
	if !responseContains(response.Output, "ok") {
		t.Fatalf("missing success output: %s", responseDump(response.Output, response.Errors))
	}
}

func TestCrossLangJsToRuby(t *testing.T) {
	if !hasAnyBinary("bun", "deno", "node") || !hasAnyBinary("ruby") {
		t.Skip("runtime not available")
	}

	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	_ = harness.Send(client, "$code")
	_ = harness.Send(client, "$js")
	jsSet := harness.Send(client, `await zeri.set("items", [1, 2, 3])`)
	if len(jsSet.Errors) > 0 {
		t.Fatalf("unexpected JS errors: %s", responseDump(jsSet.Output, jsSet.Errors))
	}

	_ = harness.Send(client, "/back")
	_ = harness.Send(client, "$ruby")
	rubyProbe := harness.Send(client, `puts "ruby-ready"`)
	if responseContains(rubyProbe.Errors, "RUNTIME-033") {
		t.Skip("runtime not available")
	}
	if len(rubyProbe.Errors) > 0 {
		t.Fatalf("unexpected Ruby probe errors: %s", responseDump(rubyProbe.Output, rubyProbe.Errors))
	}
	rbGet := harness.Send(client, `p Zeri.get("items")`)
	if len(rbGet.Errors) > 0 {
		t.Fatalf("unexpected Ruby errors: %s", responseDump(rbGet.Output, rbGet.Errors))
	}
	if !responseContains(rbGet.Output, "[1, 2, 3]") {
		t.Fatalf("expected shared array output: %s", responseDump(rbGet.Output, rbGet.Errors))
	}
}

func TestCrossLangMissingKey(t *testing.T) {
	if !hasAnyBinary("python3", "python") {
		t.Skip("runtime not available")
	}

	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	_ = harness.Send(client, "$code")
	_ = harness.Send(client, "$python")
	response := harness.Send(client, `assert zeri.get("no_such_key") is None; print("ok")`)
	if len(response.Errors) > 0 {
		t.Fatalf("unexpected errors: %s", responseDump(response.Output, response.Errors))
	}
	if !responseContains(response.Output, "ok") {
		t.Fatalf("missing success output: %s", responseDump(response.Output, response.Errors))
	}
}

func TestCrossLangConcurrentSet(t *testing.T) {
	if !hasAnyBinary("python3", "python") {
		t.Skip("runtime not available")
	}
	if runtime.GOOS == "windows" {
		t.Skip("runtime not available")
	}

	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	clientA := harness.Connect(ep)
	clientB := harness.Connect(ep)
	verifier := harness.Connect(ep)

	var wg sync.WaitGroup
	wg.Add(2)
	go func() {
		defer wg.Done()
		_ = harness.Send(clientA, "$code")
		_ = harness.Send(clientA, "$python")
		_ = harness.Send(clientA, `zeri.set("concurrent_a", 1)`)
	}()
	go func() {
		defer wg.Done()
		_ = harness.Send(clientB, "$code")
		_ = harness.Send(clientB, "$python")
		_ = harness.Send(clientB, `zeri.set("concurrent_b", 2)`)
	}()
	wg.Wait()

	_ = harness.Send(verifier, "$code")
	_ = harness.Send(verifier, "$python")
	response := harness.Send(verifier, `assert zeri.get("concurrent_a") == 1; assert zeri.get("concurrent_b") == 2; print("ok")`)
	if len(response.Errors) > 0 {
		t.Fatalf("unexpected errors: %s", responseDump(response.Output, response.Errors))
	}
	if !responseContains(response.Output, "ok") {
		t.Fatalf("missing success output: %s", responseDump(response.Output, response.Errors))
	}
}
