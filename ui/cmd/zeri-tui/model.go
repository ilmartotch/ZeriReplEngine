package main

import (
	"context"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
	"yuumi/internal/bridge"
	"yuumi/internal/system"
	"yuumi/internal/ui"
	"yuumi/pkg/yuumi"

	"charm.land/bubbles/v2/textarea"
	"charm.land/bubbles/v2/viewport"
	tea "charm.land/bubbletea/v2"
	lg "charm.land/lipgloss/v2"
	"github.com/atotto/clipboard"
)

type statusTickMsg struct{}

// AppMode represents the active interaction mode of the Zeri TUI.
type AppMode int

const (
	ModeREPL AppMode = iota
	ModeScriptEditor
)

const (
	editorTriggerCommand  = ":edit"
	editorAliasCommand    = ":script"
	copyCommandPrefix     = "/copy"
	copyCommandModeLast   = "last"
	copyCommandModeAll    = "all"
	defaultScriptLanguage = "js"
	saveCommandPrefix     = "/save"
	loadCommandPrefix     = "/load"
	newCommandPrefix      = "/new"
	editCommandPrefix     = "/edit"
	showCommandPrefix     = "/show"
	runtimeStatusCommand  = "/runtime-status"
	restartCoreCommand    = "/restart core"
	restartCommand        = "restart"
)

const (
	engineConnectTimeout       = 5 * time.Second
	engineConnectRetryInterval = 200 * time.Millisecond
)

type SaveMenuState int

const (
	SaveMenuHidden SaveMenuState = iota
	SaveMenuVisible
)

type SaveMenuOption int

const (
	SaveAndExecute SaveMenuOption = iota
	SaveOnly
	ExitWithoutSaving
)

type SessionPromptKind int

const (
	SessionPromptNone SessionPromptKind = iota
	SessionPromptSave
	SessionPromptLoad
)

type SessionOverwriteOption int

const (
	SessionOverwriteConfirm SessionOverwriteOption = iota
	SessionOverwriteCancel
)

type ConnectionState int

const (
	ConnectionConnected ConnectionState = iota
	ConnectionDisconnected
	ConnectionReconnecting
)

type engineConnectedMsg struct {
	Runner *yuumi.Runner
	Client *yuumi.Client
}

type engineReconnectFailedMsg struct {
	Error string
}

type ScriptEditorIntent int

const (
	ScriptEditorIntentNew ScriptEditorIntent = iota
	ScriptEditorIntentEdit
)

type PendingBridgeRequestKind int

const (
	PendingBridgeRequestNone PendingBridgeRequestKind = iota
	PendingBridgeRequestNewExistsCheck
	PendingBridgeRequestEditLoad
	PendingBridgeRequestShowPreview
)

type PendingBridgeRequest struct {
	Kind       PendingBridgeRequestKind
	ScriptName string
	Language   string
}

type CodePreviewState struct {
	Visible    bool
	ScriptName string
	Content    string
}

type EngineBatchChunk struct {
	IsError bool
	Content string
}

func tickStatusCmd() tea.Cmd {
	return tea.Tick(500*time.Millisecond, func(t time.Time) tea.Msg {
		return statusTickMsg{}
	})
}

type AppModel struct {
	width  int
	height int
	mode   AppMode

	viewport     viewport.Model
	input        textarea.Model
	autocomplete ui.AutocompleteModel

	messages                    []ui.ChatMessage
	scriptEditor                ui.ScriptEditor
	inputHistory                []string
	historyIndex                int
	draftBuffer                 string
	activeContext               string
	activeContextPath           string
	activeLanguage              string
	sandboxProcessRunning       bool
	pendingContextPath          string
	pendingInputPrompt          string
	isCommandMode               bool
	pendingReset                bool
	pendingScriptExecution      bool
	pendingScriptLabel          string
	pendingScriptCode           string
	pendingScriptName           string
	pendingScriptLanguage       string
	pendingScriptIntent         ScriptEditorIntent
	saveMenu                    SaveMenuState
	saveMenuCursor              SaveMenuOption
	sessionVars                 map[string]string
	pendingSessionPrompt        SessionPromptKind
	sessionOverwriteVisible     bool
	sessionOverwriteCursor      SessionOverwriteOption
	pendingSessionOverwriteName string
	pendingBridgeRequest        PendingBridgeRequest
	codePreview                 CodePreviewState
	runtimeCenterVisible        bool
	runtimeCenter               RuntimeCenterState
	startupLogPath              string
	engineLogPath               string
	enginePath                  string
	pipeName                    string

	bridge               bridge.YuumiClient
	runner               *yuumi.Runner
	client               *yuumi.Client
	engineState          ConnectionState
	engineRestartCount   int
	ready                bool
	bridgeConnected      bool
	memoryMB             uint64
	lastStatusTick       time.Time
	startupInProgress    bool
	startupFailed        bool
	startupStage         string
	startupErrors        []string
	startupSpinnerIndex  int
	engineBatchTitle     string
	engineBatchChunks    []EngineBatchChunk
	engineBatchUpdatedAt time.Time
}

func newAppModel(b bridge.YuumiClient, enginePath string, pipeName string) AppModel {
	ta := textarea.New()
	ta.Placeholder = "waiting for..."
	ta.ShowLineNumbers = false
	ta.CharLimit = 4096
	ta.SetWidth(72)
	ta.SetHeight(1)

	s := textarea.DefaultStyles(true)
	s.Focused.Base = lg.NewStyle()
	s.Blurred.Base = lg.NewStyle()
	s.Focused.CursorLine = lg.NewStyle()
	s.Blurred.CursorLine = lg.NewStyle()
	s.Focused.Placeholder = lg.NewStyle().Foreground(ui.ColourIndustrialGrey)
	s.Focused.Text = lg.NewStyle().Foreground(ui.ColourWhite)
	s.Blurred.Text = lg.NewStyle().Foreground(ui.ColourIndustrialGrey)
	ta.SetStyles(s)
	ta.Focus()

	vp := viewport.New()
	vp.SetWidth(72)
	vp.SetHeight(10)

	return AppModel{
		width:             80,
		height:            24,
		viewport:          vp,
		input:             ta,
		bridge:            b,
		historyIndex:      -1,
		activeContext:     "global",
		activeContextPath: "global",
		activeLanguage:    "",
		engineState:       ConnectionConnected,
		sessionVars:       map[string]string{},
		enginePath:        strings.TrimSpace(enginePath),
		pipeName:          strings.TrimSpace(pipeName),
		startupInProgress: true,
		startupStage:      "Initializing workspace...",
	}
}

func previousContextPath(path string) (string, bool) {
	normalized := normaliseContextName(path)
	if normalized == "" || normalized == "global" {
		return "", false
	}

	segments := strings.Split(normalized, "::")
	if len(segments) <= 1 {
		return "global", true
	}

	return strings.Join(segments[:len(segments)-1], "::"), true
}

func leafContextFromPath(path string) string {
	normalized := normaliseContextName(path)
	if normalized == "" {
		return ""
	}
	segments := strings.Split(normalized, "::")
	return segments[len(segments)-1]
}

func (m AppModel) Init() tea.Cmd {
	cmds := []tea.Cmd{tickStatusCmd(), m.input.Focus()}
	if m.bridge != nil {
		cmds = append(cmds, m.bridge.ConnectCmd())
	}
	return tea.Batch(cmds...)
}

func (m *AppModel) autocompleteHeight() int {
	if !m.autocomplete.Visible || len(m.autocomplete.Filtered) == 0 {
		return 0
	}
	return len(m.autocomplete.Filtered) + 2
}

func (m *AppModel) replAuxiliaryPanelHeight() int {
	height := m.autocompleteHeight()

	if m.sessionOverwriteVisible {
		height += lg.Height(m.renderSessionOverwriteMenu())
	}

	if m.saveMenu == SaveMenuVisible {
		height += lg.Height(m.renderScriptSaveMenu())
	}

	return height
}

func (m *AppModel) recalculateLayout() {
	headerHeight := 9
	if m.height < 15 {
		headerHeight = 1
	}
	statusBarHeight := 1
	inputBorderV := 2
	inputHeight := m.input.Height() + inputBorderV
	auxHeight := m.replAuxiliaryPanelHeight()
	paddingV := 2
	chatHeight := m.height - headerHeight - statusBarHeight - inputHeight - auxHeight - paddingV
	if chatHeight < 1 {
		chatHeight = 1
	}
	contentWidth := m.width - 4
	if contentWidth < 10 {
		contentWidth = 10
	}
	m.viewport.SetWidth(contentWidth)
	m.viewport.SetHeight(chatHeight)
	m.input.SetWidth(contentWidth - 4)
}

func (m *AppModel) refreshViewport() {
	content := ui.RenderAllMessages(m.messages, m.width-4)
	if m.codePreview.Visible {
		panel := m.renderCodePreviewPanel()
		if content == "" {
			content = panel
		} else {
			content = content + "\n\n" + panel
		}
	}
	m.viewport.SetContent(content)
	m.viewport.GotoBottom()
}

func (m AppModel) renderInputArea() string {
	prompt := lg.NewStyle().Foreground(ui.ColourVolt).Render("›")
	inner := lg.JoinHorizontal(lg.Top, prompt+" ", m.input.View())
	return lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ui.ColourElectricBlue).
		Render(inner)
}

func (m AppModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch m.mode {
	case ModeScriptEditor:
		return m.updateScriptEditor(msg)
	default:
		return m.updateREPL(msg)
	}
}

func (m AppModel) updateREPL(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {

	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		m.recalculateLayout()
		m.refreshViewport()
		return m, nil

	case tea.KeyPressMsg:
		return m.handleKeyPress(msg)

	case tea.PasteMsg:
		return m, m.applyInputPaste(msg.Content)

	case tea.MouseMsg:
		if m.handleAutocompleteMouse(msg) {
			return m, nil
		}
		mouse := msg.Mouse()
		if mouse.Button == tea.MouseWheelUp || mouse.Button == tea.MouseWheelDown {
			var cmd tea.Cmd
			m.viewport, cmd = m.viewport.Update(msg)
			return m, cmd
		}
		return m, nil

	case statusTickMsg:
		m.memoryMB = system.GetProcessMemoryMB()
		if m.startupInProgress {
			m.startupSpinnerIndex = (m.startupSpinnerIndex + 1) % 4
		}
		m.flushEngineBatchIfSettled()
		return m, tickStatusCmd()

	case bridge.ConnectedMsg:
		m.engineState = ConnectionConnected
		if !m.ready {
			m.bridgeConnected = true
			m.ready = true
			m.addSystemMessage("Work environment ready")
		}
		m.bridgeConnected = true
		return m, nil

	case bridge.DisconnectedMsg:
		m.flushEngineBatch(false)
		m.engineState = ConnectionDisconnected
		m.bridgeConnected = false
		m.sandboxProcessRunning = false
		m.pendingInputPrompt = ""
		m.addSystemMessage("⚠ ZeriEngine disconnected. Type 'restart' to reconnect.")
		if reason := strings.TrimSpace(msg.Reason); reason != "" {
			m.addSystemMessage("Reason: " + reason)
		}
		for _, tip := range m.disconnectionHints(msg.Reason) {
			m.addSystemMessage(tip)
		}
		return m, nil

	case engineConnectedMsg:
		m.flushEngineBatch(false)
		m.pendingInputPrompt = ""
		m.runner = msg.Runner
		m.client = msg.Client
		if msg.Runner != nil {
			m.engineLogPath = strings.TrimSpace(msg.Runner.EngineLogPath)
		}
		m.engineState = ConnectionConnected
		m.engineRestartCount = 0
		m.bridgeConnected = true
		if realBridge, ok := m.bridge.(*bridge.RealYuumiClient); ok {
			realBridge.SetClient(msg.Client)
		}
		m.addSystemMessage("✓ ZeriEngine connected. You can continue.")
		if m.bridge != nil {
			return m, tea.Batch(m.startEngineReaderCmd(), m.bridge.ConnectCmd())
		}
		return m, nil

	case engineReconnectFailedMsg:
		m.flushEngineBatch(false)
		m.engineState = ConnectionDisconnected
		m.bridgeConnected = false
		m.sandboxProcessRunning = false
		m.pendingInputPrompt = ""
		m.addErrorMessage("✗ Reconnect failed: " + msg.Error + "\nType 'restart' to retry.")
		for _, tip := range m.disconnectionHints(msg.Error) {
			m.addSystemMessage(tip)
		}
		return m, nil

	case bridge.DataMsg:
		m.consumeEngineOutput(msg.Content)
		return m, nil

	case bridge.ErrorMsg:
		m.consumeEngineError(msg.Content)
		return m, nil

	case bridge.InputRequestMsg:
		m.flushEngineBatch(false)
		m.pendingInputPrompt = strings.TrimSpace(msg.Prompt)
		if m.pendingInputPrompt == "" {
			m.addSystemMessage("Input required by running process.")
		} else {
			m.addSystemMessage("Input required: " + m.pendingInputPrompt)
		}
		return m, nil

	case bridge.ContextChangedMsg:
		m.flushEngineBatch(false)
		m.pendingInputPrompt = ""
		if msg.Active {
			normalizedContext := normaliseContextName(msg.ContextName)
			m.activeContext = leafContextFromPath(normalizedContext)
			if strings.TrimSpace(m.activeContext) == "" {
				m.activeContext = "global"
			}
			m.activeContextPath = m.resolveDisplayContextPath(msg.ContextName)
			m.activeLanguage = m.resolveActiveLanguage(m.activeContextPath)
			if m.activeContext != "sandbox" {
				m.sandboxProcessRunning = false
			}
		} else {
			m.activeContext = ""
			m.activeContextPath = ""
			m.activeLanguage = ""
			m.sandboxProcessRunning = false
		}
		m.pendingContextPath = ""
		m.autocomplete.ActiveContext = m.activeContext
		m.refreshViewport()
		return m, nil

	case startupPhaseMsg:
		m.startupStage = msg.Title
		return m, nil

	case startupFailedMsg:
		m.flushEngineBatch(false)
		m.pendingInputPrompt = ""
		m.startupInProgress = false
		m.startupFailed = true
		m.startupErrors = append([]string{}, msg.Errors...)
		m.startupLogPath = strings.TrimSpace(msg.LogPath)
		if m.startupLogPath != "" {
			m.startupErrors = append(m.startupErrors, "Detailed startup diagnostics saved to: "+m.startupLogPath)
		}
		m.bridgeConnected = false
		return m, nil

	case startupReadyMsg:
		m.flushEngineBatch(false)
		m.pendingInputPrompt = ""
		m.runner = msg.Runner
		m.client = msg.Client
		if msg.Runner != nil {
			m.engineLogPath = strings.TrimSpace(msg.Runner.EngineLogPath)
		}
		m.startupInProgress = false
		m.startupFailed = false
		m.startupStage = "Environment ready"
		m.startupErrors = nil
		m.startupLogPath = strings.TrimSpace(msg.LogPath)
		_ = msg.Warnings
		m.engineState = ConnectionConnected
		m.bridgeConnected = true
		m.recalculateLayout()
		m.refreshViewport()
		if m.bridge != nil {
			return m, tea.Batch(m.startEngineReaderCmd(), m.bridge.ConnectCmd())
		}
		return m, nil

	}

	return m, nil
}

func (m AppModel) updateScriptEditor(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		m.recalculateLayout()
		m.scriptEditor.SetSize(msg.Width, msg.Height)
		return m, nil

	case tea.PasteMsg:
		return m, m.applyScriptEditorPaste(msg.Content)

	case tea.KeyPressMsg:
		normalizedKey := normalisedKeyPress(msg)

		switch normalizedKey {
		case "esc", "escape":
			m.mode = ModeREPL
			m.addSystemMessage("Script editor closed.")
			return m, nil
		case "alt+enter", "alt+return", "shift+enter", "shift+return":
			m.pendingScriptCode = m.scriptEditor.Value()
			m.pendingScriptName = m.scriptEditor.Filename()
			m.pendingScriptLanguage = m.scriptEditor.Language()
			m.saveMenu = SaveMenuVisible
			m.saveMenuCursor = SaveAndExecute
			m.mode = ModeREPL
			m.recalculateLayout()
			return m, nil
		}

		if normalizedKey == "ctrl+c" {
			current := strings.TrimSpace(m.scriptEditor.Value())
			if current == "" {
				m.addSystemMessage("Copy skipped: editor content is empty.")
				return m, nil
			}

			if err := clipboard.WriteAll(current); err != nil {
				m.addSystemMessage("Copy failed: " + err.Error())
				return m, nil
			}

			m.addSystemMessage("Copied editor content to clipboard.")
			return m, nil
		}

		if isPasteShortcut(normalizedKey) {
			pasted, err := readClipboardContent()
			if err != nil {
				m.addSystemMessage("Paste failed: " + err.Error() + ". Try Shift+Insert or your terminal paste action.")
				return m, nil
			}
			return m, m.applyScriptEditorPaste(pasted)
		}

		if normalizedKey == "ctrl+x" {
			current := strings.TrimSpace(m.scriptEditor.Value())
			if current == "" {
				m.addSystemMessage("Cut skipped: editor content is empty.")
				return m, nil
			}

			if err := clipboard.WriteAll(current); err != nil {
				m.addSystemMessage("Cut failed: " + err.Error())
				return m, nil
			}

			m.scriptEditor.SetValue("")
			m.addSystemMessage("Cut editor content to clipboard.")
			return m, nil
		}

	case statusTickMsg:
		m.memoryMB = system.GetProcessMemoryMB()
		if m.startupInProgress {
			m.startupSpinnerIndex = (m.startupSpinnerIndex + 1) % 4
		}
		m.flushEngineBatchIfSettled()
		return m, tickStatusCmd()

	case bridge.ConnectedMsg:
		m.engineState = ConnectionConnected
		if !m.ready {
			m.bridgeConnected = true
			m.ready = true
			m.addSystemMessage("Work environment ready")
		}
		m.bridgeConnected = true
		return m, nil

	case bridge.DisconnectedMsg:
		m.flushEngineBatch(false)
		m.engineState = ConnectionDisconnected
		m.bridgeConnected = false
		m.pendingInputPrompt = ""
		m.addSystemMessage("⚠ ZeriEngine disconnected. Type 'restart' to reconnect.")
		if reason := strings.TrimSpace(msg.Reason); reason != "" {
			m.addSystemMessage("Reason: " + reason)
		}
		return m, nil

	case bridge.DataMsg:
		m.consumeEngineOutput(msg.Content)
		return m, nil

	case bridge.ErrorMsg:
		m.consumeEngineError(msg.Content)
		return m, nil

	case bridge.InputRequestMsg:
		m.flushEngineBatch(false)
		m.pendingInputPrompt = strings.TrimSpace(msg.Prompt)
		if m.pendingInputPrompt == "" {
			m.addSystemMessage("Input required by running process.")
		} else {
			m.addSystemMessage("Input required: " + m.pendingInputPrompt)
		}
		return m, nil

	case bridge.ContextChangedMsg:
		m.flushEngineBatch(false)
		m.pendingInputPrompt = ""
		if msg.Active {
			normalizedContext := normaliseContextName(msg.ContextName)
			m.activeContext = leafContextFromPath(normalizedContext)
			if strings.TrimSpace(m.activeContext) == "" {
				m.activeContext = "global"
			}
			m.activeContextPath = m.resolveDisplayContextPath(msg.ContextName)
			m.activeLanguage = m.resolveActiveLanguage(m.activeContextPath)
		} else {
			m.activeContext = ""
			m.activeContextPath = ""
			m.activeLanguage = ""
		}
		m.pendingContextPath = ""
		m.autocomplete.ActiveContext = m.activeContext
		m.refreshViewport()
		return m, nil
	}

	var cmd tea.Cmd
	m.scriptEditor, cmd = m.scriptEditor.Update(msg)
	return m, cmd
}

func (m AppModel) View() tea.View {
	switch m.mode {
	case ModeScriptEditor:
		return m.viewScriptEditor()
	default:
		return m.viewREPL()
	}
}

func (m AppModel) viewREPL() tea.View {
	pad := lg.NewStyle().Padding(1, 2)
	displayContextPath := m.currentDisplayContextPath()

	header := ui.RenderHeader(m.width, m.height)
	if m.startupInProgress || m.startupFailed {
		statusBar := ui.RenderStatusBar(m.width, displayContextPath, m.bridgeConnected, m.memoryMB, m.sandboxProcessRunning)
		startupPanel := m.renderStartupPanel()
		full := lg.JoinVertical(lg.Left, header, startupPanel, statusBar)
		v := tea.NewView(pad.Render(full))
		v.AltScreen = true
		v.MouseMode = tea.MouseModeCellMotion
		return v
	}
	if m.runtimeCenterVisible {
		statusBar := ui.RenderStatusBar(m.width, displayContextPath, m.bridgeConnected, m.memoryMB, m.sandboxProcessRunning)
		runtimePanel := m.renderRuntimeCenterModal()
		full := lg.JoinVertical(lg.Left, header, runtimePanel, statusBar)
		v := tea.NewView(pad.Render(full))
		v.AltScreen = true
		v.MouseMode = tea.MouseModeCellMotion
		return v
	}

	chatArea := m.viewport.View()
	statusBar := ui.RenderStatusBar(m.width, displayContextPath, m.bridgeConnected, m.memoryMB, m.sandboxProcessRunning)
	inputSection := m.renderInputArea()

	sections := []string{header, chatArea, inputSection}
	sections = m.appendREPLAuxiliarySections(sections)
	sections = append(sections, statusBar)

	full := lg.JoinVertical(lg.Left, sections...)

	v := tea.NewView(pad.Render(full))
	v.AltScreen = true
	v.MouseMode = tea.MouseModeCellMotion
	return v
}

func (m AppModel) appendREPLAuxiliarySections(sections []string) []string {
	if m.autocomplete.Visible {
		sections = append(sections, m.autocomplete.View(m.width-4))
	}

	if m.sessionOverwriteVisible {
		sections = append(sections, m.renderSessionOverwriteMenu())
	}

	if m.saveMenu == SaveMenuVisible {
		sections = append(sections, m.renderScriptSaveMenu())
	}

	return sections
}

func (m AppModel) renderStartupPanel() string {
	spinnerFrames := []string{"⠋", "⠙", "⠹", "⠸"}
	frame := spinnerFrames[m.startupSpinnerIndex%len(spinnerFrames)]
	statusText := frame + " " + strings.TrimSpace(m.startupStage)

	if m.startupFailed {
		statusText = "✖ Startup failed"
	}

	titleStyle := lg.NewStyle().Foreground(ui.ColourVolt).Bold(true)
	statusStyle := lg.NewStyle().Foreground(ui.ColourWhite)
	if m.startupFailed {
		statusStyle = lg.NewStyle().Foreground(ui.ColourErrorRed).Bold(true)
	}

	lines := []string{titleStyle.Render("Zeri initialization"), statusStyle.Render(statusText)}
	for _, line := range m.startupErrors {
		lines = append(lines, lg.NewStyle().Foreground(ui.ColourErrorRed).Render(line))
	}
	if len(m.startupErrors) > 0 {
		lines = append(lines, lg.NewStyle().Foreground(ui.ColourIndustrialGrey).Render("Tip: run terminal as administrator or install missing runtimes manually, then restart Zeri."))
	}

	panelWidth := m.width - 4
	if panelWidth < 40 {
		panelWidth = 40
	}

	return lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ui.ColourElectricBlue).
		Padding(1, 2).
		Width(panelWidth).
		Render(strings.Join(lines, "\n"))
}

func (m *AppModel) CloseRuntimeResources() {
	if realBridge, ok := m.bridge.(*bridge.RealYuumiClient); ok {
		realBridge.StopMessageForwarding()
	}
	if m.client != nil {
		_ = m.client.Close()
		m.client = nil
	}
	if m.runner != nil {
		m.runner.Stop()
		m.runner = nil
	}
}

func (m AppModel) viewScriptEditor() tea.View {
	content := m.scriptEditor.View()
	if m.saveMenu == SaveMenuVisible {
		content = lg.JoinVertical(lg.Left, content, m.renderScriptSaveMenu())
	}
	v := tea.NewView(content)
	v.AltScreen = true
	v.MouseMode = tea.MouseModeNone
	return v
}

func (m AppModel) renderScriptSaveMenu() string {
	title := fmt.Sprintf("What do you want to do with %q?", strings.TrimSpace(m.pendingScriptName))
	if strings.TrimSpace(m.pendingScriptName) == "" {
		title = "What do you want to do with this script?"
	}

	options := []string{"Save and execute", "Save without execution", "Exit without saving"}
	rows := make([]string, 0, len(options))
	for idx, option := range options {
		prefix := "   "
		if SaveMenuOption(idx) == m.saveMenuCursor {
			prefix = " ▶ "
		}
		rows = append(rows, prefix+option)
	}

	panel := lg.JoinVertical(lg.Left, title, strings.Join(rows, "\n"))
	width := m.width - 6
	if width < 44 {
		width = 44
	}

	return lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ui.AzzurroElettrico).
		Padding(0, 1).
		Width(width).
		Render(panel)
}

func (m AppModel) renderSessionOverwriteMenu() string {
	name := strings.TrimSpace(m.pendingSessionOverwriteName)
	title := fmt.Sprintf("Session %q already exists", name)
	options := []string{"Overwrite", "Cancel"}

	rows := make([]string, 0, len(options))
	for idx, option := range options {
		prefix := "   "
		if SessionOverwriteOption(idx) == m.sessionOverwriteCursor {
			prefix = " ▶ "
		}
		rows = append(rows, prefix+option)
	}

	panel := lg.JoinVertical(lg.Left, title, strings.Join(rows, "\n"))
	width := m.width - 6
	if width < 44 {
		width = 44
	}

	return lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ui.AzzurroElettrico).
		Padding(0, 1).
		Width(width).
		Render(panel)
}

func normalisedKeyPress(msg tea.KeyPressMsg) string {
	key := strings.ToLower(strings.TrimSpace(msg.String()))
	return strings.ReplaceAll(key, " ", "")
}

func isPasteShortcut(normalizedKey string) bool {
	switch normalizedKey {
	case "ctrl+v", "ctrl+shift+v", "shift+insert", "meta+v", "cmd+v":
		return true
	default:
		return false
	}
}

func normalizePastedContent(content string) string {
	normalized := strings.ReplaceAll(content, "\r\n", "\n")
	normalized = strings.ReplaceAll(normalized, "\r", "\n")
	return strings.TrimRight(normalized, "\n")
}

func readClipboardContent() (string, error) {
	pasted, err := clipboard.ReadAll()
	if err != nil {
		return "", err
	}
	return normalizePastedContent(pasted), nil
}

func (m *AppModel) applyInputPaste(content string) tea.Cmd {
	normalized := normalizePastedContent(content)
	if normalized == "" {
		return nil
	}

	var cmd tea.Cmd
	m.input, cmd = m.input.Update(tea.PasteMsg{Content: normalized})
	lines := strings.Count(m.input.Value(), "\n") + 1
	if lines < 1 {
		lines = 1
	}
	if lines > 5 {
		lines = 5
	}
	if lines != m.input.Height() {
		m.input.SetHeight(lines)
		m.recalculateLayout()
	}

	val := m.input.Value()
	m.updateAutocompleteForInput(val)

	return cmd
}

func (m *AppModel) applyScriptEditorPaste(content string) tea.Cmd {
	normalized := normalizePastedContent(content)
	if normalized == "" {
		return nil
	}

	var cmd tea.Cmd
	m.scriptEditor, cmd = m.scriptEditor.Update(tea.PasteMsg{Content: normalized})
	return cmd
}

func (m AppModel) renderCodePreviewPanel() string {
	title := "CODE VIEW"
	if m.codePreview.ScriptName != "" {
		title = "CODE VIEW - \"" + m.codePreview.ScriptName + "\""
	}

	header := lg.NewStyle().
		Foreground(ui.ColourDarkViolet).
		Background(ui.ColourElectricBlue).
		Bold(true).
		Padding(0, 1).
		Render(title + " | ESC close")

	body := lg.NewStyle().
		Foreground(ui.ColourWhite).
		Padding(0, 1).
		Render(ui.NormaliseContent(m.codePreview.Content))

	return lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ui.ColourElectricBlue).
		Render(lg.JoinVertical(lg.Left, header, body))
}

func (m *AppModel) addUserMessage(content string) {
	normalised := ui.NormaliseContent(content)
	msg := ui.ChatMessage{
		Role:      ui.RoleUser,
		Title:     m.currentMessageContextTitle(),
		Content:   normalised,
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m *AppModel) addScriptSavedMessage(content string) {
	normalised := ui.NormaliseContent(content)
	msg := ui.ChatMessage{
		Role:      ui.RoleScriptSaved,
		Title:     m.currentMessageContextTitle(),
		Content:   normalised,
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m *AppModel) addZeriMessage(content string, title string) {
	normalised := ui.NormaliseContent(content)
	msg := ui.ChatMessage{
		Role:      ui.RoleZeri,
		Title:     title,
		Content:   normalised,
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m *AppModel) addSystemMessage(content string) {
	normalised := ui.NormaliseContent(content)
	msg := ui.ChatMessage{
		Role:      ui.RoleSystem,
		Title:     m.currentMessageContextTitle(),
		Content:   normalised,
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m AppModel) handleKeyPress(msg tea.KeyPressMsg) (tea.Model, tea.Cmd) {
	key := msg.String()
	normalizedKey := normalisedKeyPress(msg)
	if m.startupInProgress || m.startupFailed {
		if key == "ctrl+c" {
			return m, tea.Quit
		}
		return m, nil
	}

	if m.runtimeCenterVisible {
		if key == "esc" || key == "escape" {
			m.runtimeCenterVisible = false
			return m, nil
		}
		if key == "ctrl+c" {
			return m, tea.Quit
		}
		return m, nil
	}

	if m.sessionOverwriteVisible {
		switch normalizedKey {
		case "up":
			if m.sessionOverwriteCursor > SessionOverwriteConfirm {
				m.sessionOverwriteCursor--
			}
			return m, nil
		case "down":
			if m.sessionOverwriteCursor < SessionOverwriteCancel {
				m.sessionOverwriteCursor++
			}
			return m, nil
		case "esc", "escape":
			m.sessionOverwriteVisible = false
			m.pendingSessionOverwriteName = ""
			m.recalculateLayout()
			m.addSystemMessage("Session overwrite cancelled.")
			return m, nil
		case "enter", "return":
			if m.sessionOverwriteCursor == SessionOverwriteConfirm {
				name := m.pendingSessionOverwriteName
				err := saveSession(m, name, true)
				m.sessionOverwriteVisible = false
				m.pendingSessionOverwriteName = ""
				m.recalculateLayout()
				if err != nil {
					m.addErrorMessage("Session overwrite failed: " + err.Error())
					return m, nil
				}
				m.addSystemMessage(fmt.Sprintf("Session %q overwritten successfully.", name))
				return m, nil
			}
			m.sessionOverwriteVisible = false
			m.pendingSessionOverwriteName = ""
			m.recalculateLayout()
			m.addSystemMessage("Session overwrite cancelled.")
			return m, nil
		default:
			return m, nil
		}
	}

	if m.saveMenu == SaveMenuVisible {
		switch normalizedKey {
		case "up":
			if m.saveMenuCursor > SaveAndExecute {
				m.saveMenuCursor--
			}
			return m, nil
		case "down":
			if m.saveMenuCursor < ExitWithoutSaving {
				m.saveMenuCursor++
			}
			return m, nil
		case "enter", "return":
			return m.handleScriptSaveMenuConfirm()
		case "esc", "escape":
			m.saveMenu = SaveMenuHidden
			m.pendingScriptCode = ""
			m.pendingScriptName = ""
			m.pendingScriptLanguage = ""
			m.recalculateLayout()
			m.addSystemMessage("Script action cancelled.")
			return m, nil
		default:
			return m, nil
		}
	}

	if normalizedKey == "ctrl+c" || normalizedKey == "ctrl+insert" {
		current := strings.TrimSpace(m.input.Value())
		if current == "" {
			m.addSystemMessage("Copy skipped: input is empty.")
			return m, nil
		}

		if err := clipboard.WriteAll(current); err != nil {
			m.addSystemMessage("Copy failed: " + err.Error())
			return m, nil
		}

		m.addSystemMessage("Copied current input to clipboard.")
		return m, nil
	}

	if isPasteShortcut(normalizedKey) {
		pasted, err := readClipboardContent()
		if err != nil {
			m.addSystemMessage("Paste failed: " + err.Error() + ". Try Shift+Insert or your terminal paste action.")
			return m, nil
		}
		return m, m.applyInputPaste(pasted)
	}

	if normalizedKey == "ctrl+x" || normalizedKey == "shift+delete" {
		current := m.input.Value()
		if strings.TrimSpace(current) == "" {
			m.addSystemMessage("Cut skipped: input is empty.")
			return m, nil
		}

		if err := clipboard.WriteAll(current); err != nil {
			m.addSystemMessage("Cut failed: " + err.Error())
			return m, nil
		}

		m.input.SetValue("")
		m.input.SetHeight(1)
		m.recalculateLayout()
		m.addSystemMessage("Cut current input to clipboard.")
		return m, nil
	}

	if key == "enter" && !msg.Mod.Contains(tea.ModShift) {
		return m.handleSubmit()
	}

	if key == "escape" {
		if m.pendingSessionPrompt != SessionPromptNone {
			m.pendingSessionPrompt = SessionPromptNone
			m.autocomplete.Dismiss()
			m.recalculateLayout()
			m.addSystemMessage("Session input cancelled.")
			return m, nil
		}
		if m.codePreview.Visible {
			m.closeCodePreview()
			return m, nil
		}
		if m.autocomplete.Visible {
			m.autocomplete.Dismiss()
			m.recalculateLayout()
			return m, nil
		}
	}

	if m.autocomplete.Visible {
		switch key {
		case "up":
			m.autocomplete.MoveUp()
			if entry, ok := m.autocomplete.Selected(); ok {
				m.input.SetValue(entry.Command)
			}
			return m, nil
		case "down":
			m.autocomplete.MoveDown()
			if entry, ok := m.autocomplete.Selected(); ok {
				m.input.SetValue(entry.Command)
			}
			return m, nil
		case "tab", "enter":
			if entry, ok := m.autocomplete.Selected(); ok {
				m.input.SetValue(entry.Command)
				m.autocomplete.Dismiss()
				m.recalculateLayout()
			}
			return m, nil
		}
	}

	if !m.autocomplete.Visible {
		line := m.input.Line()
		lastLine := m.input.LineCount() - 1
		if lastLine < 0 {
			lastLine = 0
		}
		switch key {
		case "up":
			if line == 0 {
				return m.handleHistoryUp()
			}
		case "down":
			if line >= lastLine {
				return m.handleHistoryDown()
			}
		case "ctrl+up":
			return m.handleHistoryUp()
		case "ctrl+down":
			return m.handleHistoryDown()
		case "pgup":
			var cmd tea.Cmd
			m.viewport, cmd = m.viewport.Update(msg)
			return m, cmd
		case "pgdown":
			var cmd tea.Cmd
			m.viewport, cmd = m.viewport.Update(msg)
			return m, cmd
		}
	}

	var cmd tea.Cmd
	m.input, cmd = m.input.Update(msg)

	lines := strings.Count(m.input.Value(), "\n") + 1
	newH := lines
	if newH < 1 {
		newH = 1
	}
	if newH > 5 {
		newH = 5
	}
	if newH != m.input.Height() {
		m.input.SetHeight(newH)
		m.recalculateLayout()
	}

	val := m.input.Value()
	prevVisible := m.autocomplete.Visible
	m.updateAutocompleteForInput(val)
	if m.autocomplete.Visible != prevVisible {
		m.recalculateLayout()
	}

	return m, cmd
}

func (m *AppModel) handleAutocompleteMouse(msg tea.MouseMsg) bool {
	if !m.autocomplete.Visible || len(m.autocomplete.Filtered) == 0 {
		return false
	}

	mouse := msg.Mouse()
	row := m.autocompleteRowFromMouseY(mouse.Y)
	if row < 0 || row >= len(m.autocomplete.Filtered) {
		return false
	}

	m.autocomplete.SelectedIndex = row

	if click, ok := msg.(tea.MouseClickMsg); ok && click.Button == tea.MouseLeft {
		entry := m.autocomplete.Filtered[row]
		m.input.SetValue(entry.Command)
		m.autocomplete.Dismiss()
		m.recalculateLayout()
	}

	return true
}

func (m AppModel) autocompleteRowFromMouseY(mouseY int) int {
	if !m.autocomplete.Visible || len(m.autocomplete.Filtered) == 0 {
		return -1
	}

	topPadding := 1
	headerHeight := lg.Height(ui.RenderHeader(m.width, m.height))
	chatHeight := lg.Height(m.viewport.View())
	inputHeight := lg.Height(m.renderInputArea())
	autocompleteTop := topPadding + headerHeight + chatHeight + inputHeight
	firstRowY := autocompleteTop + 1
	return mouseY - firstRowY
}

func (m AppModel) handleSubmit() (tea.Model, tea.Cmd) {
	raw := m.input.Value()
	trimmed := strings.TrimSpace(raw)
	if trimmed == "" {
		return m, nil
	}

	m.flushEngineBatch(false)

	m.input.Reset()
	m.input.SetHeight(1)
	m.autocomplete.Dismiss()
	m.recalculateLayout()

	if strings.TrimSpace(m.pendingInputPrompt) != "" {
		m.addUserMessage(trimmed)
		m.pendingInputPrompt = ""
		return m, m.bridge.SendInputResponseCmd(trimmed)
	}

	if m.pendingSessionPrompt != SessionPromptNone {
		return m.handleSessionPromptSubmit(trimmed)
	}

	normalizedInput := strings.ToLower(strings.TrimSpace(trimmed))
	if m.engineState == ConnectionReconnecting {
		m.addUserMessage(trimmed)
		m.addSystemMessage("Engine reconnection in progress. Please wait.")
		return m, nil
	}
	if m.engineState == ConnectionDisconnected {
		if normalizedInput == restartCommand || normalizedInput == "/restart" || normalizedInput == strings.ToLower(restartCoreCommand) {
			m.addUserMessage(trimmed)
			return m.handleRestart()
		}
		m.addUserMessage(trimmed)
		m.addSystemMessage("⚠ Engine unavailable. Type 'restart' to reconnect.")
		return m, nil
	}

	if trimmed == editorTriggerCommand || trimmed == editorAliasCommand {
		language := m.scriptLanguageFromContext()
		if strings.TrimSpace(language) == "" {
			language = defaultScriptLanguage
		}
		m.scriptEditor = ui.NewScriptEditor(language, m.width, m.height)
		m.mode = ModeScriptEditor
		m.pendingScriptIntent = ScriptEditorIntentNew
		return m, nil
	}

	if m.pendingReset {
		m.pendingReset = false
		lower := strings.ToLower(trimmed)
		if lower == "y" || lower == "yes" {
			m.messages = m.messages[:0]
			m.activeContext = "global"
			m.activeContextPath = "global"
			m.activeLanguage = ""
			m.addSystemMessage("Session reset.")
			m.refreshViewport()
			return m, m.bridge.SendDataCmd("/reset")
		}
		m.addSystemMessage("Reset cancelled.")
		return m, nil
	}

	m.inputHistory = append([]string{trimmed}, m.inputHistory...)
	m.historyIndex = -1
	m.draftBuffer = ""

	if strings.HasPrefix(trimmed, "/") {
		return m.handleSlashCommand(trimmed)
	}

	m.queuePendingContextTitleIfSwitch(trimmed)

	m.addUserMessage(trimmed)
	return m, m.bridge.SendDataCmd(trimmed)
}

func (m AppModel) handleScriptSaveMenuConfirm() (tea.Model, tea.Cmd) {
	selection := m.saveMenuCursor
	m.saveMenu = SaveMenuHidden
	m.recalculateLayout()

	name := strings.TrimSpace(m.pendingScriptName)
	content := m.pendingScriptCode
	language := m.pendingScriptLanguage

	if selection == ExitWithoutSaving {
		m.pendingScriptCode = ""
		m.pendingScriptName = ""
		m.pendingScriptLanguage = ""
		m.mode = ModeREPL
		m.addSystemMessage("Script editor closed without saving.")
		return m, nil
	}

	if strings.TrimSpace(name) == "" {
		m.addSystemMessage("Script name missing. Use /new \"name\" or /edit \"name\".")
		return m, nil
	}

	if strings.TrimSpace(content) == "" {
		m.addSystemMessage("Script is empty. Nothing was saved.")
		return m, nil
	}

	if err := saveScript(name, language, content); err != nil {
		m.addErrorMessage("Script save failed: " + err.Error())
		return m, nil
	}

	m.addScriptSavedMessage(formatScriptConfirmation(name, language, content))
	m.mode = ModeREPL

	if selection == SaveAndExecute {
		return m, m.submitScript(name, content, true)
	}

	m.pendingScriptCode = ""
	m.pendingScriptName = ""
	m.pendingScriptLanguage = ""
	return m, nil
}

func (m AppModel) handleSessionPromptSubmit(name string) (tea.Model, tea.Cmd) {
	prompt := m.pendingSessionPrompt
	m.pendingSessionPrompt = SessionPromptNone
	m.autocomplete.Dismiss()
	m.recalculateLayout()

	if strings.TrimSpace(name) == "" {
		m.addSystemMessage("Session name is required.")
		return m, nil
	}

	if prompt == SessionPromptSave {
		err := saveSession(m, name, false)
		if err == nil {
			m.addSystemMessage(fmt.Sprintf("Session %q saved successfully.", name))
			return m, nil
		}

		var existsErr ErrSessionExists
		if errors.As(err, &existsErr) {
			m.pendingSessionOverwriteName = existsErr.Name
			m.sessionOverwriteVisible = true
			m.sessionOverwriteCursor = SessionOverwriteConfirm
			m.recalculateLayout()
			return m, nil
		}

		m.addErrorMessage("Session save failed: " + err.Error())
		return m, nil
	}

	snapshot, err := loadSession(name)
	if err != nil {
		m.addErrorMessage("Session load failed: " + err.Error())
		return m, nil
	}

	m.applySessionSnapshot(snapshot)
	m.addSystemMessage(fmt.Sprintf("Session %q loaded successfully.", snapshot.Name))
	return m, nil
}

func (m *AppModel) applySessionSnapshot(snapshot SessionSnapshot) {
	m.activeContext = "global"
	m.activeContextPath = "global"
	m.activeLanguage = ""

	m.messages = append([]ui.ChatMessage{}, snapshot.History...)
	m.sessionVars = cloneSessionVars(snapshot.SessionVars)

	targetPath := normaliseContextName(snapshot.ActiveContext)
	if targetPath == "" {
		targetPath = "global"
	}
	m.activeContextPath = targetPath
	m.activeContext = leafContextFromPath(targetPath)
	m.activeLanguage = m.resolveActiveLanguage(targetPath)
	m.autocomplete.ActiveContext = m.activeContext
	m.refreshViewport()
}

func parseCommandArgument(input string, command string) (string, bool) {
	remainder := strings.TrimSpace(strings.TrimPrefix(input, command))
	if remainder == "" {
		return "", false
	}
	if strings.HasPrefix(remainder, "\"") && strings.HasSuffix(remainder, "\"") && len(remainder) >= 2 {
		name := strings.TrimSpace(remainder[1 : len(remainder)-1])
		if name == "" {
			return "", false
		}
		return name, true
	}
	parts := strings.Fields(remainder)
	if len(parts) == 0 {
		return "", false
	}
	return strings.TrimSpace(parts[0]), true
}

func (m *AppModel) populateSessionNameAutocomplete(input string) {
	names, err := listSessionNames(input)
	if err != nil || len(names) == 0 {
		m.autocomplete.Dismiss()
		return
	}

	entries := make([]ui.CompletionEntry, 0, len(names))
	for _, name := range names {
		entries = append(entries, ui.CompletionEntry{Command: name, Synopsis: "saved session"})
	}
	m.autocomplete.Filtered = entries
	m.autocomplete.Visible = true
	m.autocomplete.SelectedIndex = 0
}

func (m *AppModel) populateSessionCommandAutocomplete(command string, prefix string) {
	names, err := listSessionNames(prefix)
	if err != nil || len(names) == 0 {
		m.autocomplete.Dismiss()
		return
	}

	entries := make([]ui.CompletionEntry, 0, len(names))
	for _, name := range names {
		entries = append(entries, ui.CompletionEntry{
			Command:  command + " " + quoteScriptName(name),
			Synopsis: "session name",
		})
	}
	m.autocomplete.Filtered = entries
	m.autocomplete.Visible = true
	m.autocomplete.SelectedIndex = 0
}

func (m *AppModel) populateScriptCommandAutocomplete(command string, prefix string) {
	names, err := listScriptNames(prefix, m.scriptLanguageFromContext())
	if err != nil || len(names) == 0 {
		m.autocomplete.Dismiss()
		return
	}

	entries := make([]ui.CompletionEntry, 0, len(names))
	for _, name := range names {
		entries = append(entries, ui.CompletionEntry{
			Command:  command + " " + quoteScriptName(name),
			Synopsis: "saved script",
		})
	}
	m.autocomplete.Filtered = entries
	m.autocomplete.Visible = true
	m.autocomplete.SelectedIndex = 0
}

func (m *AppModel) updateAutocompleteForInput(value string) {
	if m.pendingSessionPrompt != SessionPromptNone {
		m.populateSessionNameAutocomplete(strings.TrimSpace(value))
		return
	}

	trimmed := strings.TrimSpace(value)
	lower := strings.ToLower(trimmed)

	if strings.HasPrefix(lower, saveCommandPrefix+" ") {
		m.populateSessionCommandAutocomplete(saveCommandPrefix, strings.TrimSpace(trimmed[len(saveCommandPrefix):]))
		return
	}
	if strings.HasPrefix(lower, loadCommandPrefix+" ") {
		m.populateSessionCommandAutocomplete(loadCommandPrefix, strings.TrimSpace(trimmed[len(loadCommandPrefix):]))
		return
	}

	if strings.HasPrefix(lower, editCommandPrefix+" ") {
		m.populateScriptCommandAutocomplete(editCommandPrefix, strings.TrimSpace(trimmed[len(editCommandPrefix):]))
		return
	}
	if strings.HasPrefix(lower, showCommandPrefix+" ") {
		m.populateScriptCommandAutocomplete(showCommandPrefix, strings.TrimSpace(trimmed[len(showCommandPrefix):]))
		return
	}

	m.isCommandMode = strings.HasPrefix(value, "/") || strings.HasPrefix(value, "$")
	if m.isCommandMode {
		m.autocomplete.Filter(value)
	} else {
		m.autocomplete.Dismiss()
	}
}

func (m AppModel) submitScript(name string, code string, runAfterSave bool) tea.Cmd {
	trimmed := strings.TrimSpace(code)
	if trimmed == "" {
		return nil
	}
	name = strings.TrimSpace(name)
	if name == "" {
		m.addSystemMessage("Script name missing. Use /new \"name\" or /edit \"name\".")
		return nil
	}

	m.pendingScriptExecution = true
	m.pendingScriptLabel = "[" + name + "]"

	entryCommand := newCommandPrefix + " " + quoteScriptName(name)
	if m.pendingScriptIntent == ScriptEditorIntentEdit {
		entryCommand = editCommandPrefix + " " + quoteScriptName(name)
	}

	lines := strings.Split(trimmed, "\n")
	commands := make([]tea.Cmd, 0, len(lines)+3)
	commands = append(commands, m.bridge.SendDataCmd(entryCommand))
	for _, line := range lines {
		if strings.TrimSpace(line) == "" {
			continue
		}
		commands = append(commands, m.bridge.SendDataCmd(line))
	}
	commands = append(commands, m.bridge.SendDataCmd("/save"))
	if runAfterSave {
		commands = append(commands, m.bridge.SendDataCmd("/run "+quoteScriptName(name)))
	}

	m.pendingScriptCode = ""
	m.pendingScriptName = ""
	m.pendingScriptLanguage = ""
	return tea.Sequence(commands...)
}

func (m *AppModel) consumeEngineOutput(content string) {
	if m.updateSandboxProcessStatusFromContent(content) {
		return
	}
	if m.pendingBridgeRequest.Kind != PendingBridgeRequestNone {
		m.flushEngineBatch(false)
		m.handlePendingBridgeData(content)
		return
	}
	if m.pendingScriptExecution {
		m.flushEngineBatch(false)
		m.addScriptExecutionMessage(m.pendingScriptLabel, content)
		m.pendingScriptExecution = false
		m.pendingScriptLabel = ""
		return
	}
	m.bufferEngineChunk(false, content)
}

func (m *AppModel) consumeEngineError(content string) {
	if m.updateSandboxProcessStatusFromContent(content) {
		return
	}
	if m.pendingBridgeRequest.Kind != PendingBridgeRequestNone {
		m.flushEngineBatch(false)
		m.handlePendingBridgeError(content)
		return
	}
	if strings.TrimSpace(m.pendingContextPath) != "" {
		m.flushEngineBatch(false)
		m.pendingContextPath = ""
		m.autocomplete.ActiveContext = m.activeContext
	}
	m.bufferEngineChunk(true, content)
}

func (m *AppModel) updateSandboxProcessStatusFromContent(content string) bool {
	normalized := strings.ToLower(strings.TrimSpace(content))
	if normalized == "sandbox process status: running" {
		m.sandboxProcessRunning = true
		return true
	}
	if normalized == "sandbox process status: idle" {
		m.sandboxProcessRunning = false
		m.flushEngineBatch(false)
		return true
	}
	return false
}

func (m *AppModel) bufferEngineChunk(isError bool, content string) {
	normalized := ui.NormaliseContent(content)
	if strings.TrimSpace(normalized) == "" {
		return
	}
	if m.engineBatchTitle == "" {
		m.engineBatchTitle = m.currentMessageContextTitle()
	}
	m.engineBatchChunks = append(m.engineBatchChunks, EngineBatchChunk{
		IsError: isError,
		Content: normalized,
	})
	m.engineBatchUpdatedAt = time.Now()
}

func (m *AppModel) flushEngineBatchIfSettled() {
	if len(m.engineBatchChunks) == 0 {
		return
	}
	if m.sandboxProcessRunning {
		return
	}
	if strings.TrimSpace(m.pendingInputPrompt) != "" {
		return
	}
	if m.engineBatchUpdatedAt.IsZero() {
		m.flushEngineBatch(false)
		return
	}
	if time.Since(m.engineBatchUpdatedAt) >= 350*time.Millisecond {
		m.flushEngineBatch(false)
	}
}

func (m *AppModel) flushEngineBatch(forceError bool) {
	if len(m.engineBatchChunks) == 0 {
		return
	}

	title := m.engineBatchTitle
	if strings.TrimSpace(title) == "" {
		title = m.currentMessageContextTitle()
	}

	var builder strings.Builder
	hasError := forceError
	for idx, chunk := range m.engineBatchChunks {
		if idx > 0 {
			builder.WriteString("\n")
		}
		if chunk.IsError {
			hasError = true
			builder.WriteString("[stderr] ")
		}
		builder.WriteString(chunk.Content)
	}

	if hasError {
		msg := ui.ChatMessage{
			Role:      ui.RoleError,
			Title:     title,
			Content:   builder.String(),
			Timestamp: time.Now().Format("15:04"),
		}
		m.messages = append(m.messages, msg)
	} else {
		msg := ui.ChatMessage{
			Role:      ui.RoleZeri,
			Title:     title,
			Content:   builder.String(),
			Timestamp: time.Now().Format("15:04"),
		}
		m.messages = append(m.messages, msg)
	}

	m.engineBatchChunks = nil
	m.engineBatchTitle = ""
	m.engineBatchUpdatedAt = time.Time{}
	m.refreshViewport()
}

func (m *AppModel) addErrorMessage(content string) {
	normalised := ui.NormaliseContent(content)
	msg := ui.ChatMessage{
		Role:      ui.RoleError,
		Title:     m.currentMessageContextTitle(),
		Content:   normalised,
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m *AppModel) addScriptExecutionMessage(label string, content string) {
	msg := ui.ChatMessage{
		Role:      ui.RoleScriptExecution,
		Label:     strings.TrimSpace(label),
		Title:     m.currentMessageContextTitle(),
		Content:   ui.NormaliseContent(content),
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m AppModel) scriptExecutionLabel() string {
	filename := m.scriptEditor.Filename()
	if filename != "" {
		return "[" + filename + "]"
	}

	language := m.scriptEditor.Language()
	if language == "" {
		language = m.scriptLanguageFromContext()
	}
	return "[$" + strings.TrimPrefix(language, "$") + "]"
}

func (m *AppModel) handlePendingBridgeData(content string) {
	req := m.pendingBridgeRequest
	m.pendingBridgeRequest = PendingBridgeRequest{}

	switch req.Kind {
	case PendingBridgeRequestNewExistsCheck:
		m.addSystemMessage("Script \"" + req.ScriptName + "\" already exists. Use a different name.")
	case PendingBridgeRequestEditLoad:
		m.openScriptEditor(req.Language, req.ScriptName, content, ScriptEditorIntentEdit)
	case PendingBridgeRequestShowPreview:
		m.codePreview = CodePreviewState{
			Visible:    true,
			ScriptName: req.ScriptName,
			Content:    content,
		}
		m.refreshViewport()
	default:
		m.addZeriMessage(content, m.currentMessageContextTitle())
	}
}

func (m *AppModel) handlePendingBridgeError(content string) {
	req := m.pendingBridgeRequest
	m.pendingBridgeRequest = PendingBridgeRequest{}

	switch req.Kind {
	case PendingBridgeRequestNewExistsCheck:
		if isScriptNotFoundError(content) {
			m.openScriptEditor(req.Language, req.ScriptName, "", ScriptEditorIntentNew)
			return
		}
		m.addZeriMessage("Error: "+content, m.currentMessageContextTitle())
	case PendingBridgeRequestEditLoad:
		m.addZeriMessage("Error: "+content, m.currentMessageContextTitle())
	case PendingBridgeRequestShowPreview:
		m.addZeriMessage("Error: "+content, m.currentMessageContextTitle())
	default:
		m.addZeriMessage("Error: "+content, m.currentMessageContextTitle())
	}
}

func (m AppModel) currentMessageContextTitle() string {
	if strings.TrimSpace(m.pendingContextPath) != "" {
		return m.pendingContextPath
	}
	return m.activeContextPath
}

func (m AppModel) currentDisplayContextPath() string {
	if strings.TrimSpace(m.pendingContextPath) != "" {
		return m.composeCodeDisplayLabel(m.pendingContextPath)
	}
	return m.composeCodeDisplayLabel(m.activeContextPath)
}

func (m AppModel) composeCodeDisplayLabel(path string) string {
	normalizedPath := normaliseContextName(path)
	if normalizedPath == "" {
		return ""
	}
	if normalizedPath == "global" {
		return "global"
	}
	if normalizedPath == "code" {
		if m.activeLanguage == "" {
			return normalizedPath
		}
		return normalizedPath + "::" + m.activeLanguage
	}
	return normalizedPath
}

func (m AppModel) resolveActiveLanguage(contextPath string) string {
	normalized := normaliseContextName(contextPath)
	if normalized == "" {
		return ""
	}
	segments := strings.Split(normalized, "::")
	for i := 0; i < len(segments); i++ {
		if segments[i] != "code" {
			continue
		}
		if i+1 < len(segments) {
			return segments[i+1]
		}
		return ""
	}
	return ""
}

func normaliseContextName(name string) string {
	trimmed := strings.TrimSpace(strings.TrimPrefix(name, "$"))
	if trimmed == "" {
		return ""
	}
	lower := strings.ToLower(trimmed)
	lower = strings.TrimPrefix(lower, "zeri::")
	return lower
}

func contextParent(name string) (string, bool) {
	switch normaliseContextName(name) {
	case "js", "ts", "lua", "python", "ruby":
		return "code", true
	default:
		return "", false
	}
}

func contextPathPrefix(path string, context string) string {
	normalizedPath := normaliseContextName(path)
	normalizedContext := normaliseContextName(context)
	if normalizedPath == "" || normalizedContext == "" {
		return ""
	}

	segments := strings.Split(normalizedPath, "::")
	for idx := len(segments) - 1; idx >= 0; idx-- {
		if segments[idx] == normalizedContext {
			return strings.Join(segments[:idx+1], "::")
		}
	}

	return ""
}

func (m AppModel) resolveDisplayContextPath(nextContext string) string {
	normalized := normaliseContextName(nextContext)
	if normalized == "" {
		return ""
	}
	if normalized == "global" {
		return "global"
	}

	parent, hasParent := contextParent(normalized)
	if !hasParent {
		return normalized
	}

	parentPath := contextPathPrefix(m.activeContextPath, parent)
	if parentPath == "" {
		parentPath = parent
	}

	return parentPath + "::" + normalized
}

func parseDirectContextSwitchCommand(input string) (string, bool) {
	trimmed := strings.ToLower(strings.TrimSpace(input))
	if trimmed == "" || !strings.HasPrefix(trimmed, "$") {
		return "", false
	}
	if strings.ContainsAny(trimmed, " |\t\n\r") {
		return "", false
	}
	name := strings.TrimPrefix(trimmed, "$")
	if name == "" {
		return "", false
	}
	return name, true
}

func (m *AppModel) queuePendingContextTitleIfSwitch(input string) {
	target, ok := parseDirectContextSwitchCommand(input)
	if !ok {
		return
	}

	command := "$" + target
	for _, entry := range ui.ContextCommandsForContext(m.activeContext) {
		if strings.EqualFold(entry.Command, command) {
			m.pendingContextPath = m.resolveDisplayContextPath(target)
			m.autocomplete.ActiveContext = target
			return
		}
	}
}

func (m *AppModel) openScriptEditor(language string, scriptName string, content string, intent ScriptEditorIntent) {
	m.scriptEditor = ui.NewScriptEditorWithContent(language, m.width, m.height, scriptName, content)
	m.pendingScriptIntent = intent
	m.mode = ModeScriptEditor
}

func (m *AppModel) closeCodePreview() {
	if !m.codePreview.Visible {
		return
	}
	name := m.codePreview.ScriptName
	m.codePreview = CodePreviewState{}
	m.addCodeViewHistoryBlock(name)
}

func (m *AppModel) addCodeViewHistoryBlock(scriptName string) {
	label := "[code view - \"" + scriptName + "\"]"
	msg := ui.ChatMessage{
		Role:      ui.RoleCodeView,
		Label:     label,
		Title:     m.currentMessageContextTitle(),
		Content:   "closed",
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m AppModel) scriptLanguageFromContext() string {
	if strings.TrimSpace(m.activeLanguage) != "" {
		return strings.TrimSpace(m.activeLanguage)
	}

	pathLanguage := m.resolveActiveLanguage(m.activeContextPath)
	if strings.TrimSpace(pathLanguage) != "" {
		return pathLanguage
	}

	language := leafContextFromPath(normaliseContextName(m.activeContext))
	if language == "" || language == "global" || language == "code" {
		return ""
	}
	return language
}

func (m AppModel) isCodeContextActive() bool {
	language := m.scriptLanguageFromContext()
	if language == "" {
		return false
	}

	switch strings.ToLower(language) {
	case "js", "ts", "python", "lua", "ruby":
		return true
	default:
		return false
	}
}

func parseScriptNameArgument(input string, command string) (string, bool) {
	remainder := strings.TrimSpace(strings.TrimPrefix(input, command))
	if remainder == "" {
		return "", false
	}
	if strings.HasPrefix(remainder, "\"") && strings.HasSuffix(remainder, "\"") && len(remainder) >= 2 {
		name := strings.TrimSpace(remainder[1 : len(remainder)-1])
		return name, name != ""
	}
	parts := strings.Fields(remainder)
	if len(parts) == 0 {
		return "", false
	}
	return strings.Trim(parts[0], "\""), true
}

func quoteScriptName(name string) string {
	trimmed := strings.TrimSpace(name)
	if strings.Contains(trimmed, " ") {
		return "\"" + trimmed + "\""
	}
	return trimmed
}

func isScriptNotFoundError(content string) bool {
	lower := strings.ToLower(content)
	return strings.Contains(lower, "script_not_found") || strings.Contains(lower, "script not found")
}

func roleNameForClipboard(role ui.MessageRole) string {
	switch role {
	case ui.RoleUser:
		return "USER"
	case ui.RoleSystem:
		return "SYSTEM"
	case ui.RoleError:
		return "ERROR"
	case ui.RoleScriptExecution:
		return "SCRIPT"
	case ui.RoleCodeView:
		return "CODE_VIEW"
	case ui.RoleScriptSaved:
		return "SCRIPT_SAVED"
	default:
		return "ZERI"
	}
}

func (m AppModel) messageClipboardText(msg ui.ChatMessage) string {
	label := roleNameForClipboard(msg.Role)
	if strings.TrimSpace(msg.Label) != "" {
		label += " " + strings.TrimSpace(msg.Label)
	}
	if strings.TrimSpace(msg.Timestamp) != "" {
		label = "[" + strings.TrimSpace(msg.Timestamp) + "] " + label
	}

	content := strings.TrimSpace(msg.Content)
	if content == "" {
		return label
	}

	return label + "\n" + content
}

func (m AppModel) lastOutputClipboardText() string {
	for idx := len(m.messages) - 1; idx >= 0; idx-- {
		msg := m.messages[idx]
		if msg.Role == ui.RoleUser {
			continue
		}
		return m.messageClipboardText(msg)
	}
	return ""
}

func (m AppModel) transcriptClipboardText() string {
	if len(m.messages) == 0 {
		return ""
	}

	parts := make([]string, 0, len(m.messages))
	for _, msg := range m.messages {
		parts = append(parts, m.messageClipboardText(msg))
	}
	return strings.Join(parts, "\n\n")
}

func (m *AppModel) copyClipboardText(content string, emptyMessage string, successMessage string) {
	trimmed := strings.TrimSpace(content)
	if trimmed == "" {
		m.addSystemMessage(emptyMessage)
		return
	}

	if err := clipboard.WriteAll(trimmed); err != nil {
		m.addSystemMessage("Copy failed: " + err.Error())
		return
	}

	m.addSystemMessage(successMessage)
}

func (m AppModel) handleCopyCommand(cmd string) (tea.Model, tea.Cmd) {
	remainder := strings.TrimSpace(strings.TrimPrefix(strings.ToLower(cmd), copyCommandPrefix))
	if remainder == "" || remainder == copyCommandModeLast {
		m.copyClipboardText(m.lastOutputClipboardText(), "Copy skipped: no output messages available.", "Copied last output message to clipboard.")
		return m, nil
	}

	if remainder == copyCommandModeAll {
		m.copyClipboardText(m.transcriptClipboardText(), "Copy skipped: message history is empty.", "Copied full message history to clipboard.")
		return m, nil
	}

	m.addSystemMessage("Unknown copy option. Usage: /copy last | /copy all")
	return m, nil
}

func (m AppModel) handleSlashCommand(cmd string) (tea.Model, tea.Cmd) {
	m.flushEngineBatch(false)

	if strings.EqualFold(strings.TrimSpace(cmd), "/back") {
		if previous, ok := previousContextPath(m.activeContextPath); ok {
			m.pendingContextPath = previous
			m.activeLanguage = m.resolveActiveLanguage(previous)
			m.autocomplete.ActiveContext = leafContextFromPath(previous)
		}
	}

	switch {
	case cmd == "/exit":
		return m, tea.Sequence(
			m.bridge.SendShutdownCmd(),
			tea.Quit,
		)
	case cmd == "/clear":
		m.messages = m.messages[:0]
		m.refreshViewport()
		return m, nil
	case cmd == copyCommandPrefix || strings.HasPrefix(cmd, copyCommandPrefix+" "):
		m.addUserMessage(cmd)
		return m.handleCopyCommand(cmd)
	case cmd == "/reset":
		m.pendingReset = true
		m.addSystemMessage("Reset will clear all variables and return to global context.\nType 'y' to confirm, anything else to cancel.")
		return m, nil
	case cmd == runtimeStatusCommand:
		m.addUserMessage(cmd)
		m.runtimeCenter = buildRuntimeCenterState(m.startupLogPath)
		m.runtimeCenterVisible = true
		return m, nil
	case strings.EqualFold(strings.TrimSpace(cmd), restartCoreCommand):
		m.addUserMessage(cmd)
		return m.handleRestart()
	case cmd == saveCommandPrefix || strings.HasPrefix(cmd, saveCommandPrefix+" "):
		m.addUserMessage(cmd)
		if name, ok := parseCommandArgument(cmd, saveCommandPrefix); ok {
			m.pendingSessionPrompt = SessionPromptSave
			return m.handleSessionPromptSubmit(name)
		}
		m.pendingSessionPrompt = SessionPromptSave
		m.addSystemMessage("Enter a session name to save.")
		m.updateAutocompleteForInput("")
		m.recalculateLayout()
		return m, nil
	case cmd == loadCommandPrefix || strings.HasPrefix(cmd, loadCommandPrefix+" "):
		m.addUserMessage(cmd)
		if name, ok := parseCommandArgument(cmd, loadCommandPrefix); ok {
			m.pendingSessionPrompt = SessionPromptLoad
			return m.handleSessionPromptSubmit(name)
		}
		m.pendingSessionPrompt = SessionPromptLoad
		m.addSystemMessage("Enter a session name to load.")
		m.updateAutocompleteForInput("")
		m.recalculateLayout()
		return m, nil
	case strings.HasPrefix(cmd, newCommandPrefix):
		if !m.isCodeContextActive() {
			m.addUserMessage(cmd)
			return m, m.bridge.SendDataCmd(cmd)
		}
		scriptName, ok := parseScriptNameArgument(cmd, newCommandPrefix)
		if !ok {
			m.addSystemMessage("Missing script name. Usage: /new \"script-name\".")
			return m, nil
		}
		m.addUserMessage(cmd)
		if _, err := readScript(scriptName, m.scriptLanguageFromContext()); err == nil {
			m.addSystemMessage("Script already exists. Use /edit to modify it.")
			return m, nil
		}
		m.openScriptEditor(m.scriptLanguageFromContext(), scriptName, "", ScriptEditorIntentNew)
		return m, nil
	case strings.HasPrefix(cmd, editCommandPrefix):
		if !m.isCodeContextActive() {
			m.addUserMessage(cmd)
			return m, m.bridge.SendDataCmd(cmd)
		}
		scriptName, ok := parseScriptNameArgument(cmd, editCommandPrefix)
		if !ok {
			m.addSystemMessage("Missing script name. Usage: /edit \"script-name\".")
			return m, nil
		}
		m.addUserMessage(cmd)
		content, err := readScript(scriptName, m.scriptLanguageFromContext())
		if err != nil {
			m.addErrorMessage("Unable to open script: " + err.Error())
			return m, nil
		}
		m.openScriptEditor(m.scriptLanguageFromContext(), scriptName, content, ScriptEditorIntentEdit)
		return m, nil
	case strings.HasPrefix(cmd, showCommandPrefix):
		if !m.isCodeContextActive() {
			m.addUserMessage(cmd)
			return m, m.bridge.SendDataCmd(cmd)
		}
		scriptName, ok := parseScriptNameArgument(cmd, showCommandPrefix)
		if !ok {
			m.addSystemMessage("Missing script name. Usage: /show \"script-name\".")
			return m, nil
		}
		m.addUserMessage(cmd)
		content, err := readScript(scriptName, m.scriptLanguageFromContext())
		if err != nil {
			m.addErrorMessage("Unable to show script: " + err.Error())
			return m, nil
		}
		m.codePreview = CodePreviewState{
			Visible:    true,
			ScriptName: scriptName,
			Content:    content,
		}
		m.refreshViewport()
		return m, nil
	default:
		m.addUserMessage(cmd)
		return m, m.bridge.SendDataCmd(cmd)
	}
}

func (m AppModel) handleRestart() (tea.Model, tea.Cmd) {
	if realBridge, ok := m.bridge.(*bridge.RealYuumiClient); ok {
		realBridge.StopMessageForwarding()
	}
	if m.client != nil {
		_ = m.client.Close()
		m.client = nil
	}
	if m.runner != nil {
		m.runner.Stop()
		m.runner = nil
	}

	m.startupInProgress = false
	m.startupFailed = false
	m.sandboxProcessRunning = false
	m.pendingInputPrompt = ""
	m.bridgeConnected = false
	m.engineState = ConnectionReconnecting
	m.engineRestartCount++
	m.addSystemMessage(fmt.Sprintf("↻ Restarting ZeriEngine (attempt %d)...", m.engineRestartCount))

	return m, m.restartCoreCmd()
}

func (m AppModel) restartCoreCmd() tea.Cmd {
	modelSnapshot := m
	return func() tea.Msg {
		runner, client, err := modelSnapshot.launchAndConnectEngine()
		if err != nil {
			return engineReconnectFailedMsg{Error: err.Error()}
		}
		return engineConnectedMsg{Runner: runner, Client: client}
	}
}

func (m AppModel) startEngineReaderCmd() tea.Cmd {
	if realBridge, ok := m.bridge.(*bridge.RealYuumiClient); ok {
		return func() tea.Msg {
			realBridge.RegisterMessageHandler()
			return nil
		}
	}
	return nil
}

func (m AppModel) launchAndConnectEngine() (*yuumi.Runner, *yuumi.Client, error) {
	enginePath, err := m.resolveEnginePath()
	if err != nil {
		return nil, nil, err
	}
	pipeName := strings.TrimSpace(m.pipeName)
	if pipeName == "" {
		return nil, nil, fmt.Errorf("engine pipe name is not configured")
	}

	sessionTempDir := resolveSessionTempDir()
	runner := &yuumi.Runner{
		BinaryPath:     enginePath,
		PipeName:       pipeName,
		SessionTempDir: sessionTempDir,
	}

	crashCh := make(chan error, 1)
	runner.OnCrash = func(crashErr error) {
		if crashErr != nil {
			select {
			case crashCh <- crashErr:
			default:
			}
		}
		if realBridge, ok := m.bridge.(*bridge.RealYuumiClient); ok {
			reason := "engine process exited"
			if crashErr != nil {
				reason = crashErr.Error()
			}
			realBridge.Send(bridge.DisconnectedMsg{Reason: reason})
		}
	}

	if err = runner.Start(context.Background()); err != nil {
		return nil, nil, fmt.Errorf("unable to start ZeriEngine: %w", err)
	}

	timeout := time.NewTimer(engineConnectTimeout)
	defer timeout.Stop()
	ticker := time.NewTicker(engineConnectRetryInterval)
	defer ticker.Stop()

	connectOptions := yuumi.ConnectOptions{
		MaxRetries:  0,
		BaseDelay:   engineConnectRetryInterval,
		MaxDelay:    engineConnectRetryInterval,
		DialTimeout: engineConnectRetryInterval,
	}

	for {
		select {
		case crashErr := <-crashCh:
			runner.Stop()
			return nil, nil, fmt.Errorf("engine exited during startup: %w", crashErr)
		case <-timeout.C:
			runner.Stop()
			return nil, nil, fmt.Errorf("timeout: ZeriEngine did not respond after %s", engineConnectTimeout)
		case <-ticker.C:
			client, connectErr := yuumi.Connect(pipeName, connectOptions)
			if connectErr != nil {
				continue
			}
			runner.SetClient(client)
			return runner, client, nil
		}
	}
}

func (m AppModel) resolveEnginePath() (string, error) {
	configuredPath := strings.TrimSpace(m.enginePath)
	if configuredPath != "" {
		if _, err := os.Stat(configuredPath); err == nil {
			return configuredPath, nil
		}
	}

	execPath, err := os.Executable()
	if err == nil {
		candidate := filepath.Join(filepath.Dir(execPath), "ZeriEngine")
		if runtime.GOOS == "windows" {
			candidate += ".exe"
		}
		if _, statErr := os.Stat(candidate); statErr == nil {
			return candidate, nil
		}
	}

	if path := strings.TrimSpace(os.Getenv("ZERI_ENGINE_PATH")); path != "" {
		if _, statErr := os.Stat(path); statErr == nil {
			return path, nil
		}
	}

	lookupPath, lookupErr := exec.LookPath("ZeriEngine")
	if lookupErr == nil {
		return lookupPath, nil
	}

	return "", fmt.Errorf("ZeriEngine not found. Set ZERI_ENGINE_PATH or place the engine executable in the same directory as the TUI")
}

func (m AppModel) disconnectionHints(reason string) []string {
	trimmedReason := strings.TrimSpace(reason)
	if trimmedReason == "" {
		trimmedReason = "unknown transport error"
	}

	hints := []string{"Use 'restart' to restore the C++ engine connection without closing Zeri."}
	lowerReason := strings.ToLower(trimmedReason)
	if strings.Contains(lowerReason, "eof") || strings.Contains(lowerReason, "0xc0000409") {
		engineLogPath := strings.TrimSpace(m.engineLogPath)
		if engineLogPath == "" && strings.TrimSpace(m.enginePath) != "" {
			engineLogPath = filepath.Join(filepath.Dir(m.enginePath), "zeri-engine.log")
		}
		if engineLogPath != "" {
			hints = append(hints, "Engine crash diagnostics: "+engineLogPath)
		}
		hints = append(hints, "Crash signature detected (EOF/0xc0000409). Verify the executed script path and runtime environment, then retry with 'restart'.")
	}

	return hints
}

func (m AppModel) handleHistoryUp() (tea.Model, tea.Cmd) {
	if len(m.inputHistory) == 0 {
		return m, nil
	}
	if m.historyIndex == -1 {
		m.draftBuffer = m.input.Value()
	}
	next := m.historyIndex + 1
	if next >= len(m.inputHistory) {
		next = len(m.inputHistory) - 1
	}
	m.historyIndex = next
	m.input.SetValue(m.inputHistory[m.historyIndex])
	return m, nil
}

func (m AppModel) handleHistoryDown() (tea.Model, tea.Cmd) {
	m.historyIndex--
	if m.historyIndex < 0 {
		m.historyIndex = -1
		m.input.SetValue(m.draftBuffer)
	} else {
		m.input.SetValue(m.inputHistory[m.historyIndex])
	}
	return m, nil
}

/*
 * What:
 *   - Added full script workflow orchestration in AppModel for code contexts:
 *     `/new`, `/edit`, `/show` now drive dedicated TUI behavior.
 *   - Added async bridge request correlation for script existence checks,
 *     script load for editing, and temporary preview fetch.
 *   - Added explicit save-and-run confirmation prompt after Alt+Enter in
 *     editor mode before dispatching engine commands.
 *   - Added temporary code preview panel in REPL for `/show`, closable with
 *     ESC, with persistent history marker block on close.
 *   - Updated script execution dispatch to perform editor workflow commands
 *     through existing bridge transport: open editor context, push lines,
 *     save, and run named script.
 *
 * Why:
 *   - Aligns UI workflow with requested command semantics across all code
 *     contexts while preserving existing bridge protocol usage.
 *   - Keeps `/run`, `/list`, `/delete` as management commands and keeps
 *     `/show` in main REPL view without mode switch.
 *
 * Impact on other components:
 *   - ui/internal/ui/scripteditor.go is used for named editor sessions.
 *   - ui/internal/ui/message.go and messages.go render new history block types.
 */
