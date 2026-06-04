package persistence

import (
	"encoding/json"
	"runtime"
	"strings"
	"testing"
	"time"
	"yuumi/internal/ui"
)

func TestZeriBaseDirReturnsValidPath(t *testing.T) {
	switch runtime.GOOS {
	case "windows":
		t.Setenv("APPDATA", `C:\Temp\AppData`)
	case "darwin":
		t.Setenv("HOME", "/tmp/zeri-home")
	default:
		t.Setenv("XDG_CONFIG_HOME", "/tmp/zeri-config")
	}

	path, err := ZeriBaseDir()
	if err != nil {
		t.Fatalf("ZeriBaseDir returned error: %v", err)
	}
	if strings.TrimSpace(path) == "" {
		t.Fatalf("ZeriBaseDir returned empty path")
	}
}

func TestSessionSnapshotMarshalUnmarshal(t *testing.T) {
	input := SessionSnapshot{
		Name:          "session-a",
		SavedAt:       time.Now().UTC().Round(time.Second),
		ActiveContext: "global",
		History: []ui.ChatMessage{
			{Role: ui.RoleUser, Content: "hello"},
			{Role: ui.RoleZeri, Content: "world"},
		},
		SessionVars: map[string]string{"k": "v"},
	}

	raw, err := json.Marshal(input)
	if err != nil {
		t.Fatalf("marshal failed: %v", err)
	}

	var decoded SessionSnapshot
	if err := json.Unmarshal(raw, &decoded); err != nil {
		t.Fatalf("unmarshal failed: %v", err)
	}

	if decoded.Name != input.Name || decoded.ActiveContext != input.ActiveContext {
		t.Fatalf("decoded snapshot mismatch: got=%+v want=%+v", decoded, input)
	}
	if len(decoded.History) != len(input.History) {
		t.Fatalf("history length mismatch: got=%d want=%d", len(decoded.History), len(input.History))
	}
	if decoded.SessionVars["k"] != "v" {
		t.Fatalf("session vars mismatch: got=%v", decoded.SessionVars)
	}
}

func TestSessionSnapshotCorruptedJSONReturnsError(t *testing.T) {
	var decoded SessionSnapshot
	err := json.Unmarshal([]byte(`{"name":"x",`), &decoded)
	if err == nil {
		t.Fatalf("expected corrupted JSON unmarshal error")
	}
}
