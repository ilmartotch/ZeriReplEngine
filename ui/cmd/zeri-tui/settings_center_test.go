package main

import (
	"path/filepath"
	"strings"
	"testing"
)

func TestSettingsPathChangeUpdatesPointer(t *testing.T) {
	isolateOnboardingHome(t)

	home, err := ConfigHomeDir()
	if err != nil {
		t.Fatalf("ConfigHomeDir returned error: %v", err)
	}
	if err := AdoptDataRoot(home); err != nil {
		t.Fatalf("AdoptDataRoot returned error: %v", err)
	}
	if err := SetOnboardingCompleted(true); err != nil {
		t.Fatalf("SetOnboardingCompleted returned error: %v", err)
	}

	parent := t.TempDir()
	m := AppModel{}
	m.settingsVisible = true

	updated, _ := m.handleSettingsPathChange(parent)
	model, ok := updated.(AppModel)
	if !ok {
		t.Fatalf("expected AppModel from handleSettingsPathChange")
	}

	root, resolved, err := ResolveDataRoot()
	if err != nil {
		t.Fatalf("ResolveDataRoot returned error: %v", err)
	}
	if !resolved {
		t.Fatalf("expected data root to resolve from pointer after change")
	}
	expected := filepath.Join(parent, "zeri")
	if filepath.Clean(root) != filepath.Clean(expected) {
		t.Fatalf("expected data root %q, got %q", expected, root)
	}

	completed, err := OnboardingCompleted()
	if err != nil || !completed {
		t.Fatalf("expected onboarding flag preserved, got completed=%v err=%v", completed, err)
	}

	if !model.settingsVisible {
		t.Fatalf("expected settings modal to stay open")
	}
	if len(model.messages) == 0 {
		t.Fatalf("expected a confirmation message to be appended")
	}
	last := model.messages[len(model.messages)-1].Content
	if !strings.Contains(last, "Data location updated successfully.") {
		t.Fatalf("expected success confirmation, got %q", last)
	}
	if !strings.Contains(last, "Restart Zeri to use the new location.") {
		t.Fatalf("expected restart hint, got %q", last)
	}
}

func TestSettingsPathChangeAlreadyChosen(t *testing.T) {
	isolateOnboardingHome(t)

	parent := t.TempDir()
	if _, err := SetDataRootUnderParent(parent); err != nil {
		t.Fatalf("SetDataRootUnderParent returned error: %v", err)
	}

	m := AppModel{}
	updated, _ := m.handleSettingsPathChange(parent)
	model := updated.(AppModel)
	last := model.messages[len(model.messages)-1].Content
	if !strings.Contains(last, "already set") {
		t.Fatalf("expected already-set notice, got %q", last)
	}
}

func TestMigrationCommandBlockContainsBothPlatforms(t *testing.T) {
	block := migrationCommandBlock("/old/zeri", "/new/zeri")
	if !strings.Contains(block, "robocopy") {
		t.Fatalf("expected PowerShell robocopy command, got %q", block)
	}
	if !strings.Contains(block, "Copy-Item") {
		t.Fatalf("expected PowerShell Copy-Item command, got %q", block)
	}
	if !strings.Contains(block, "cp -a") {
		t.Fatalf("expected POSIX cp command, got %q", block)
	}
	if !strings.Contains(block, "rsync -a") {
		t.Fatalf("expected POSIX rsync command, got %q", block)
	}
	if !strings.Contains(block, "\"/old/zeri\"") || !strings.Contains(block, "\"/new/zeri\"") {
		t.Fatalf("expected real quoted paths in commands, got %q", block)
	}
}

func TestMigrationCommandBlockSamePathEmpty(t *testing.T) {
	if block := migrationCommandBlock("/same/zeri", "/same/zeri"); block != "" {
		t.Fatalf("expected empty block when paths match, got %q", block)
	}
}
