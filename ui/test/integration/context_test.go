package integration

import (
	"testing"

	"yuumi/test/integration/harness"
)

func TestContextCodeSwitch(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	response := harness.Send(client, "$code")
	if response.Reason != "context_transition" {
		t.Fatalf("expected context_transition reason, got %q (%s)", response.Reason, responseDump(response.Output, response.Errors))
	}
}

func TestContextSandboxSwitch(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	response := harness.Send(client, "$sandbox")
	if response.Reason != "context_transition" {
		t.Fatalf("expected context_transition reason for $sandbox, got %q (%s)", response.Reason, responseDump(response.Output, response.Errors))
	}
}

func TestContextInvalidSwitch(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	response := harness.Send(client, "$invalid-context")
	if len(response.Errors) == 0 {
		t.Fatalf("expected error on invalid context switch, got %s", responseDump(response.Output, response.Errors))
	}
}

func TestContextBackFromNonRoot(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	switchResponse := harness.Send(client, "$code")
	if switchResponse.Reason != "context_transition" {
		t.Fatalf("expected context switch to code, got %q", switchResponse.Reason)
	}

	backResponse := harness.Send(client, "/back")
	if backResponse.Reason != "execution_complete" && backResponse.Reason != "context_transition" {
		t.Fatalf("expected /back to complete successfully, got %q (%s)", backResponse.Reason, responseDump(backResponse.Output, backResponse.Errors))
	}
}

func TestContextGlobalExpressionRejected(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	response := harness.Send(client, "1+1")
	if len(response.Errors) == 0 {
		t.Fatalf("expected global expression rejection error, got %s", responseDump(response.Output, response.Errors))
	}
}
