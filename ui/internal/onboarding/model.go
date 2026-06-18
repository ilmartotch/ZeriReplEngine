package onboarding

import (
	"strings"
	"time"

	tea "charm.land/bubbletea/v2"
)

type TutorialStep int

const (
	StepWelcome TutorialStep = iota
	StepChoosePath
	StepSwitchContext
	StepRunCode
	StepSaveScript
	StepScriptHub
	StepComplete
)

type TutorialCommandMsg struct {
	Input string
}

type TutorialActionKind int

const (
	TutorialActionNone TutorialActionKind = iota
	TutorialActionSendCommand
	TutorialActionOpenScriptHub
	TutorialActionMarkCompleted
	TutorialActionExitToRepl
	TutorialActionSetDataRoot
)

type DataRootCompletedMsg struct {
	Path string
}

type DataRootFailedMsg struct {
	Reason string
}

type TutorialAction struct {
	Kind TutorialActionKind
	Payload string
}

type TutorialModel struct {
	step TutorialStep
	instruction string
	feedback string
	done bool
	width int
	height int
	pythonAvailable bool
	defaultDataParent string
	waitingDataRoot bool
	waitingRunResult bool
	waitingSaveAck bool
	waitingHubClose bool
}

func New(pythonAvailable bool, defaultDataParent string) TutorialModel {
	m := TutorialModel{
		step: StepWelcome,
		pythonAvailable: pythonAvailable,
		defaultDataParent: defaultDataParent,
	}
	m.syncInstruction()
	return m
}

func (m TutorialModel) Step() TutorialStep {
	return m.step
}

func (m TutorialModel) Instruction() string {
	return m.instruction
}

func (m TutorialModel) Feedback() string {
	return m.feedback
}

func (m TutorialModel) Done() bool {
	return m.done
}

func (m *TutorialModel) SetSize(width int, height int) {
	m.width = width
	m.height = height
}

func (m *TutorialModel) SetPythonAvailable(available bool) {
	m.pythonAvailable = available
}

type TutorialHubAutoCloseMsg struct{}

func TutorialHubAutoCloseCmd() tea.Cmd {
	return tea.Tick(2*time.Second, func(time.Time) tea.Msg {
		return TutorialHubAutoCloseMsg{}
	})
}

type tutorialRunCompletedMsg struct{}
type tutorialSaveCompletedMsg struct{}

func TutorialRunCompletedMsg() tea.Msg {
	return tutorialRunCompletedMsg{}
}

func TutorialSaveCompletedMsg() tea.Msg {
	return tutorialSaveCompletedMsg{}
}

func (m TutorialModel) Update(msg tea.Msg) (TutorialModel, TutorialAction) {
	switch typed := msg.(type) {
	case tea.KeyPressMsg:
		if normaliseKey(typed) == "ctrl+c" {
			m.done = true
			return m, TutorialAction{Kind: TutorialActionMarkCompleted}
		}
		if m.step == StepWelcome && keyIsEnter(typed) {
			m.step = StepChoosePath
			m.feedback = ""
			m.syncInstruction()
			return m, TutorialAction{}
		}
		if m.step == StepScriptHub && normaliseKey(typed) == "ctrl+h" {
			m.waitingHubClose = true
			m.feedback = "Opening Script Hub..."
			return m, TutorialAction{Kind: TutorialActionOpenScriptHub}
		}
	case TutorialCommandMsg:
		input := strings.TrimSpace(typed.Input)
		switch m.step {
		case StepChoosePath:
			if m.waitingDataRoot {
				return m, TutorialAction{}
			}
			parent := input
			if parent == "" {
				parent = m.defaultDataParent
			}
			m.waitingDataRoot = true
			m.feedback = "Preparing your data folder..."
			return m, TutorialAction{Kind: TutorialActionSetDataRoot, Payload: parent}
		case StepSwitchContext:
			if input == "$py" || input == "$python" {
				m.feedback = "✓ You're in the Python context."
				m.step = StepRunCode
				m.syncInstruction()
				return m, TutorialAction{Kind: TutorialActionSendCommand, Payload: "$py"}
			}
			m.feedback = "Please type $py and press Enter."
		case StepRunCode:
			if !m.pythonAvailable {
				m.feedback = "Python isn't installed — skipping this step."
				m.step = StepSaveScript
				m.syncInstruction()
				return m, TutorialAction{}
			}
			if input == `print("hello, zeri")` || input == "print('hello, zeri')" {
				m.waitingRunResult = true
				m.feedback = "Running your first script..."
				return m, TutorialAction{Kind: TutorialActionSendCommand, Payload: input}
			}
			m.feedback = `Type print("hello, zeri") and press Enter.`
		case StepSaveScript:
			lower := strings.ToLower(input)
			if strings.HasPrefix(lower, "/save ") && strings.TrimSpace(strings.TrimPrefix(input, "/save")) != "" {
				m.waitingSaveAck = true
				m.feedback = "Saving your script..."
				return m, TutorialAction{Kind: TutorialActionSendCommand, Payload: input}
			}
			m.feedback = "Type /save hello-zeri and press Enter."
		}
	case DataRootCompletedMsg:
		if m.step == StepChoosePath && m.waitingDataRoot {
			m.waitingDataRoot = false
			m.feedback = "Data folder ready: " + strings.TrimSpace(typed.Path)
			m.step = StepSwitchContext
			m.syncInstruction()
		}
	case DataRootFailedMsg:
		if m.step == StepChoosePath && m.waitingDataRoot {
			m.waitingDataRoot = false
			m.feedback = "Couldn't use that path: " + strings.TrimSpace(typed.Reason) + ". Try another folder."
		}
	case tutorialRunCompletedMsg:
		if m.step == StepRunCode && m.waitingRunResult {
			m.waitingRunResult = false
			m.feedback = "✓ Your first script ran!"
			m.step = StepSaveScript
			m.syncInstruction()
		}
	case tutorialSaveCompletedMsg:
		if m.step == StepSaveScript && m.waitingSaveAck {
			m.waitingSaveAck = false
			m.feedback = "✓ Script saved to your Script Hub."
			m.step = StepScriptHub
			m.syncInstruction()
		}
	case TutorialHubAutoCloseMsg:
		if m.step == StepScriptHub && m.waitingHubClose {
			m.waitingHubClose = false
			m.step = StepComplete
			m.syncInstruction()
		}
	}

	if m.step == StepComplete {
		m.done = true
		return m, TutorialAction{Kind: TutorialActionMarkCompleted}
	}

	return m, TutorialAction{}
}

func keyIsEnter(msg tea.KeyPressMsg) bool {
	key := strings.ToLower(strings.TrimSpace(msg.String()))
	return key == "enter" || key == "return"
}

func normaliseKey(msg tea.KeyPressMsg) string {
	return strings.ToLower(strings.ReplaceAll(strings.TrimSpace(msg.String()), " ", ""))
}

func (m *TutorialModel) syncInstruction() {
	switch m.step {
	case StepWelcome:
		m.instruction = "Welcome to Zeri. In the next few minutes you will pick where your data lives, switch context, run code, save a script, and open Script Hub.\n\nPress Enter to start."
	case StepChoosePath:
		m.instruction = "Choose where Zeri stores your data. A \"zeri\" folder will be created inside it.\n\nPress Enter to accept the default:\n" + m.defaultDataParent + "\n\nor type another folder path and press Enter."
	case StepSwitchContext:
		m.instruction = "Type $py and press Enter."
	case StepRunCode:
		m.instruction = `Type print("hello, zeri") and press Enter.`
	case StepSaveScript:
		m.instruction = "Type /save hello-zeri and press Enter."
	case StepScriptHub:
		m.instruction = "Press Ctrl+H to open the Script Hub."
	case StepComplete:
		m.instruction = "Onboarding complete:\n• You switched execution context.\n• You executed your first script.\n• You saved and browsed scripts in Script Hub."
	}
}
