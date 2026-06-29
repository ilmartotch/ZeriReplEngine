package persistence

import (
	"encoding/json"
	"os"
	"runtime"
	"strings"
	"testing"
	"time"
	"yuumi/internal/ui"
)

func TestZeriBaseDirReturnsValidPath(t *testing.T) {
	root := t.TempDir()
	switch runtime.GOOS {
	case "windows":
		t.Setenv("APPDATA", `C:\Temp\AppData`)
	case "darwin":
		t.Setenv("HOME", "/tmp/zeri-home")
	default:
		t.Setenv("XDG_CONFIG_HOME", "/tmp/zeri-config")
	}
	t.Setenv("ZERI_HOME", root)
	if err := os.MkdirAll(root, 0o755); err != nil {
		t.Fatalf("mkdir failed: %v", err)
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
		EngineState: map[string]interface{}{
			"schema_version":   float64(2),
			"global_variables": map[string]interface{}{"alpha": float64(5)},
		},
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
	if decoded.EngineState["schema_version"] != float64(2) {
		t.Fatalf("engine state schema mismatch: got=%v", decoded.EngineState["schema_version"])
	}
}

func TestSessionSnapshotCorruptedJSONReturnsError(t *testing.T) {
	var decoded SessionSnapshot
	err := json.Unmarshal([]byte(`{"name":"x",`), &decoded)
	if err == nil {
		t.Fatalf("expected corrupted JSON unmarshal error")
	}
}
