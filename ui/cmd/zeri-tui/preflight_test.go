package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestPreflightEngineMissing(t *testing.T) {
	manifestPath := writeTestRuntimeManifest(t)
	t.Setenv(runtimeManifestPathEnv, manifestPath)

	enginePath := filepath.Join(t.TempDir(), "missing-engine")
	errs := RunPreflight(enginePath, "preflight-test-pipe")
	if len(errs) == 0 {
		t.Fatalf("expected preflight errors for missing engine")
	}

	found := false
	for _, err := range errs {
		if err.Code == "ENGINE_NOT_FOUND" {
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("expected ENGINE_NOT_FOUND in preflight errors, got %+v", errs)
	}
}

func TestPreflightValidEnvironment(t *testing.T) {
	manifestPath := writeTestRuntimeManifest(t)
	t.Setenv(runtimeManifestPathEnv, manifestPath)

	enginePath := filepath.Join(t.TempDir(), "zeri-engine-test")
	if err := os.WriteFile(enginePath, []byte(""), 0o755); err != nil {
		t.Fatalf("failed creating fake engine file: %v", err)
	}

	errs := RunPreflight(enginePath, "preflight-test-pipe")
	for _, err := range errs {
		if err.Code == "ENGINE_NOT_FOUND" || err.Code == "BOOTSTRAP_MANIFEST_INVALID" {
			t.Fatalf("unexpected fatal preflight error in valid env: %+v", err)
		}
	}
}

func writeTestRuntimeManifest(t *testing.T) string {
	t.Helper()

	type runtimeDef struct {
		Name        string                        `json:"name"`
		Check       string                        `json:"check"`
		Required    bool                          `json:"required"`
		Candidates  []string                      `json:"candidates"`
		VersionArgs []string                      `json:"versionArgs"`
		MinVersion  string                        `json:"minVersion"`
		InstallHint string                        `json:"installHint"`
		Installers  map[string][]RuntimeInstaller `json:"installers"`
	}
	type manifest struct {
		Version  int          `json:"version"`
		Runtimes []runtimeDef `json:"runtimes"`
	}

	payload := manifest{
		Version: runtimeManifestVersion,
		Runtimes: []runtimeDef{
			{
				Name:        "optional-test-runtime",
				Check:       "Optional Runtime",
				Required:    false,
				Candidates:  []string{"optional-runtime-binary"},
				VersionArgs: []string{"--version"},
				MinVersion:  "",
				InstallHint: "No-op for tests.",
				Installers: map[string][]RuntimeInstaller{
					"all": {
						{Manager: "brew", Package: "optional-runtime"},
					},
				},
			},
		},
	}

	raw, err := json.Marshal(payload)
	if err != nil {
		t.Fatalf("failed marshaling runtime manifest fixture: %v", err)
	}
	path := filepath.Join(t.TempDir(), "runtime_manifest.json")
	if err := os.WriteFile(path, raw, 0o644); err != nil {
		t.Fatalf("failed writing runtime manifest fixture: %v", err)
	}
	return path
}
