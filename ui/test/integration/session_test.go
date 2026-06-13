package integration

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
	"time"

	"yuumi/test/integration/harness"
)

func TestSessionSaveCreatesStateFile(t *testing.T) {
	env, base := isolatedUserEnv(t)
	ep := harness.SpawnEngineWithEnv(t, env)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	response := harness.Send(client, "/save")
	if len(response.Errors) > 0 {
		t.Fatalf("unexpected /save errors: %s", responseDump(response.Output, response.Errors))
	}

	statePath := expectedSessionStatePath(base)
	if _, err := os.Stat(statePath); err != nil {
		t.Fatalf("expected session state file at %s: %v", statePath, err)
	}
}

func TestSessionMissingStateNoCrash(t *testing.T) {
	env, base := isolatedUserEnv(t)
	statePath := expectedSessionStatePath(base)
	_ = os.Remove(statePath)

	ep := harness.SpawnEngineWithEnv(t, env)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	response := harness.Send(client, "/help")
	if len(response.Output) == 0 {
		t.Fatalf("engine did not respond after missing state file: %s", responseDump(response.Output, response.Errors))
	}
}

func TestSessionCorruptedStateNoCrash(t *testing.T) {
	env, base := isolatedUserEnv(t)
	statePath := expectedSessionStatePath(base)
	if err := os.MkdirAll(filepath.Dir(statePath), 0o755); err != nil {
		t.Fatalf("failed creating session dir: %v", err)
	}
	if err := os.WriteFile(statePath, []byte("{ broken json"), 0o644); err != nil {
		t.Fatalf("failed writing corrupted state file: %v", err)
	}

	ep := harness.SpawnEngineWithEnv(t, env)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	response := harness.Send(client, "/help")
	if len(response.Output) == 0 {
		t.Fatalf("engine did not respond with corrupted state file: %s", responseDump(response.Output, response.Errors))
	}
}

func TestSessionVariableRoundTripAcrossRestart(t *testing.T) {
	env, _ := isolatedUserEnv(t)

	ep1 := harness.SpawnEngineWithEnv(t, env)
	client1 := harness.Connect(ep1)
	switchResponse := harness.Send(client1, "$math")
	if switchResponse.Reason != "context_transition" {
		t.Fatalf("expected context transition to math, got %q", switchResponse.Reason)
	}

	assignResponse := harness.Send(client1, "x = 42")
	if len(assignResponse.Errors) > 0 {
		t.Fatalf("failed assigning variable in math context: %s", responseDump(assignResponse.Output, assignResponse.Errors))
	}

	promoteResponse := harness.Send(client1, "/promote x persisted")
	if len(promoteResponse.Errors) > 0 {
		t.Fatalf("failed promoting variable to persisted scope: %s", responseDump(promoteResponse.Output, promoteResponse.Errors))
	}

	saveResponse := harness.Send(client1, "/save")
	if len(saveResponse.Errors) > 0 {
		t.Fatalf("failed saving session state: %s", responseDump(saveResponse.Output, saveResponse.Errors))
	}
	harness.Cleanup(t, ep1)

	ep2 := harness.SpawnEngineWithEnv(t, env)
	defer harness.Cleanup(t, ep2)
	client2 := harness.Connect(ep2)
	switchResponse2 := harness.Send(client2, "$math")
	if switchResponse2.Reason != "context_transition" {
		t.Fatalf("expected context transition to math after restart, got %q", switchResponse2.Reason)
	}

	valueResponse := harness.Send(client2, "x+0")
	if !responseContains(valueResponse.Output, "42") {
		t.Fatalf("expected persisted variable value after restart, got reason=%q %s", valueResponse.Reason, responseDump(valueResponse.Output, valueResponse.Errors))
	}
}

func TestSessionCrashDuringSaveDoesNotCorruptStateFile(t *testing.T) {
	env, base := isolatedUserEnv(t)
	env["ZERI_TEST_SAVE_PAUSE_MS"] = "500"

	ep := harness.SpawnEngineWithEnv(t, env)
	client := harness.Connect(ep)

	switchResponse := harness.Send(client, "$math")
	if switchResponse.Reason != "context_transition" {
		t.Fatalf("expected context transition to math, got %q", switchResponse.Reason)
	}
	_ = harness.Send(client, "x = 7")
	_ = harness.Send(client, "/promote x persisted")
	initialSave := harness.Send(client, "/save")
	if len(initialSave.Errors) > 0 {
		t.Fatalf("failed initial save before crash test: %s", responseDump(initialSave.Output, initialSave.Errors))
	}

	statePath := expectedSessionStatePath(base)
	initialData, err := os.ReadFile(statePath)
	if err != nil {
		t.Fatalf("expected baseline state file before crash save test: %v", err)
	}
	if !json.Valid(initialData) {
		t.Fatalf("baseline session state file is not valid JSON before crash: %s", string(initialData))
	}

	if err := harness.SendFireAndForget(client, "/save"); err != nil {
		t.Fatalf("failed to send /save: %v", err)
	}
	time.Sleep(100 * time.Millisecond)
	harness.KillProcess(t, ep)
	harness.Cleanup(t, ep)

	data, err := os.ReadFile(statePath)
	if err != nil {
		t.Fatalf("expected state file after crash save test: %v", err)
	}
	if !json.Valid(data) {
		t.Fatalf("session state file is not valid JSON after crash: %s", string(data))
	}

	ep2 := harness.SpawnEngineWithEnv(t, env)
	defer harness.Cleanup(t, ep2)
	client2 := harness.Connect(ep2)
	helpResponse := harness.Send(client2, "/help")
	if len(helpResponse.Output) == 0 {
		t.Fatalf("engine did not respond after crash-recovery save: %s", responseDump(helpResponse.Output, helpResponse.Errors))
	}
}
