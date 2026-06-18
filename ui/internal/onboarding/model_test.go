package onboarding

import (
	"testing"

	tea "charm.land/bubbletea/v2"
)

func key(code rune, mod tea.KeyMod) tea.KeyPressMsg {
	return tea.KeyPressMsg(tea.Key{Code: code, Mod: mod})
}

func advanceToChoosePath(t *testing.T, m TutorialModel) TutorialModel {
	t.Helper()
	m, _ = m.Update(key(tea.KeyEnter, 0))
	if m.Step() != StepChoosePath {
		t.Fatalf("expected StepChoosePath after Enter, got %v", m.Step())
	}
	return m
}

func TestGoBackFromChoosePathReturnsToWelcome(t *testing.T) {
	m := New(true, "/tmp/parent")
	m = advanceToChoosePath(t, m)

	m, action := m.Update(key(tea.KeyLeft, 0))
	if action.Kind != TutorialActionNone {
		t.Fatalf("expected no action on goBack, got %v", action.Kind)
	}
	if m.Step() != StepWelcome {
		t.Fatalf("expected StepWelcome after Left, got %v", m.Step())
	}
}

func TestGoBackGuardedAtFirstStep(t *testing.T) {
	m := New(true, "/tmp/parent")
	m, _ = m.Update(key('b', tea.ModCtrl))
	if m.Step() != StepWelcome {
		t.Fatalf("expected to stay on StepWelcome, got %v", m.Step())
	}
}

func TestGoBackBlockedWhileWaitingDataRoot(t *testing.T) {
	m := New(true, "/tmp/parent")
	m = advanceToChoosePath(t, m)

	m, action := m.Update(TutorialCommandMsg{Input: ""})
	if action.Kind != TutorialActionSetDataRoot {
		t.Fatalf("expected TutorialActionSetDataRoot, got %v", action.Kind)
	}
	if !m.waitingDataRoot {
		t.Fatalf("expected waitingDataRoot to be true")
	}

	m, _ = m.Update(key(tea.KeyLeft, 0))
	if m.Step() != StepChoosePath {
		t.Fatalf("expected to stay on StepChoosePath while waiting, got %v", m.Step())
	}
}

func TestEscConfirmsExitThenExits(t *testing.T) {
	m := New(true, "/tmp/parent")
	m = advanceToChoosePath(t, m)

	m, action := m.Update(key(tea.KeyEsc, 0))
	if action.Kind != TutorialActionNone {
		t.Fatalf("expected no action on first Esc, got %v", action.Kind)
	}
	if !m.ConfirmingExit() {
		t.Fatalf("expected confirmingExit true after first Esc")
	}

	m, action = m.Update(key(tea.KeyEsc, 0))
	if action.Kind != TutorialActionExitToRepl {
		t.Fatalf("expected TutorialActionExitToRepl on second Esc, got %v", action.Kind)
	}
	if !m.Done() {
		t.Fatalf("expected Done true after exit")
	}
}

func TestEscThenEnterCancelsExit(t *testing.T) {
	m := New(true, "/tmp/parent")
	m = advanceToChoosePath(t, m)

	m, _ = m.Update(key(tea.KeyEsc, 0))
	if !m.ConfirmingExit() {
		t.Fatalf("expected confirmingExit true")
	}

	m, action := m.Update(key(tea.KeyEnter, 0))
	if action.Kind != TutorialActionNone {
		t.Fatalf("expected no action when cancelling exit, got %v", action.Kind)
	}
	if m.ConfirmingExit() {
		t.Fatalf("expected confirmingExit cleared after Enter")
	}
	if m.Step() != StepChoosePath {
		t.Fatalf("expected to remain on StepChoosePath, got %v", m.Step())
	}
}

func TestCtrlCOpensExitConfirmationNotCompletion(t *testing.T) {
	m := New(true, "/tmp/parent")
	m = advanceToChoosePath(t, m)

	m, action := m.Update(key('c', tea.ModCtrl))
	if action.Kind != TutorialActionNone {
		t.Fatalf("expected no action on Ctrl+C, got %v", action.Kind)
	}
	if !m.ConfirmingExit() {
		t.Fatalf("expected Ctrl+C to open exit confirmation")
	}
	if m.Done() {
		t.Fatalf("Ctrl+C must not complete onboarding")
	}

	m, action = m.Update(key('c', tea.ModCtrl))
	if action.Kind != TutorialActionExitToRepl {
		t.Fatalf("expected TutorialActionExitToRepl on second Ctrl+C, got %v", action.Kind)
	}
}

func TestDataRootCompletedNoticeIsSurfaced(t *testing.T) {
	m := New(true, "/tmp/parent")
	m = advanceToChoosePath(t, m)
	m, _ = m.Update(TutorialCommandMsg{Input: ""})

	m, _ = m.Update(DataRootCompletedMsg{Path: "/tmp/parent/zeri", Notice: "reused"})
	if m.Step() != StepSwitchContext {
		t.Fatalf("expected StepSwitchContext after completion, got %v", m.Step())
	}
	feedback := m.Feedback()
	if feedback == "" {
		t.Fatalf("expected feedback to be populated")
	}
}
