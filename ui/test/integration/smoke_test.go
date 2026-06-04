package integration

import (
	"testing"

	"yuumi/test/integration/harness"
)

func TestSmoke(t *testing.T) {
	ep := harness.SpawnEngine(t)
	defer harness.Cleanup(t, ep)

	client := harness.Connect(ep)
	response := harness.Send(client, "/help")

	if len(response.Output) == 0 {
		t.Fatalf("expected non-empty output for help command, got reason=%q errors=%v", response.Reason, response.Errors)
	}
}
