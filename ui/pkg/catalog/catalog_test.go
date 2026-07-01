package catalog

import (
	"strings"
	"testing"
)

func TestCommandsScopeResolvesKnownContexts(t *testing.T) {
	commandEntries := Commands()
	if len(commandEntries) == 0 {
		t.Fatal("expected commands catalog to contain entries")
	}
	contextEntries := Contexts()
	if len(contextEntries) == 0 {
		t.Fatal("expected contexts catalog to contain entries")
	}
}

func TestCLI001ContainsResetOnboarding(t *testing.T) {
	entry, ok := ErrorByCode("CLI-001")
	if !ok {
		t.Fatal("CLI-001 must exist in error catalog")
	}
	if entry.Hint == "" {
		t.Fatal("CLI-001 hint must not be empty")
	}
	if want := "--reset-onboarding"; !strings.Contains(entry.Hint, want) {
		t.Fatalf("CLI-001 hint must contain %q, got %q", want, entry.Hint)
	}
}

func TestRuntimeLanguageMappings(t *testing.T) {
	bunFolders := RuntimeLanguageFolders("bun")
	if len(bunFolders) < 2 {
		t.Fatalf("expected bun runtime to map at least js/ts folders, got %v", bunFolders)
	}
	if _, ok := ResolveLanguage("py"); !ok {
		t.Fatal("expected language alias 'py' to resolve")
	}
}

func TestBridgeTypesContainCoreEntries(t *testing.T) {
	if BridgeTypeValue(BridgeTypeHandshakeID) == "" {
		t.Fatal("missing bridge handshake type")
	}
	if BridgeTypeValue(BridgeTypeCommandID) == "" {
		t.Fatal("missing bridge command type")
	}
	if BridgeTypeValue(BridgeTypeStreamBatchEndID) == "" {
		t.Fatal("missing bridge stream_batch_end type")
	}
}
