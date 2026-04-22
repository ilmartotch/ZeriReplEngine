package main

import (
	"context"
	"fmt"
	"path/filepath"
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
	editorTriggerCommand = ":edit"
	editorAliasCommand = ":script"
	copyCommandPrefix = "/copy"
	copyCommandModeLast = "last"
	copyCommandModeAll = "all"
	defaultScriptLanguage = "js"
	scriptRunConfirmTemplate = "Code finished for (%s). Type 'y' to save+run, 's' to save only, 'n' to return to editor."
	newCommandPrefix = "/new"
	editCommandPrefix = "/edit"
	showCommandPrefix = "/show"
	runtimeStatusCommand = "/runtime-status"
	restartCoreCommand = "/restart core"
)

type coreRestartResultMsg struct {
	Runner *yuumi.Runner
	Client *yuumi.Client
	Err error
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
	Kind PendingBridgeRequestKind
	ScriptName string
	Language string
}

type CodePreviewState struct {
	Visible bool
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
	width int
	height int
	mode AppMode

	viewport viewport.Model
	input textarea.Model
	autocomplete ui.AutocompleteModel

	messages []ui.ChatMessage
	scriptEditor ui.ScriptEditor
	inputHistory []string
	historyIndex int
	draftBuffer string
	activeContext string
	activeContextPath string
	activeLanguage string
	sandboxProcessRunning bool
	pendingContextPath string
	pendingInputPrompt string
	isCommandMode bool
	pendingReset bool
	pendingScriptExecution bool
	pendingScriptLabel string
	pendingScriptConfirm bool
	pendingScriptCode string
	pendingScriptName string
	pendingScriptLanguage string
	pendingScriptIntent ScriptEditorIntent
	pendingBridgeRequest PendingBridgeRequest
	codePreview CodePreviewState
	runtimeCenterVisible bool
	runtimeCenter RuntimeCenterState
	startupLogPath string
	engineLogPath string
	enginePath string
	pipeName string

	bridge bridge.YuumiClient
	runner *yuumi.Runner
	client *yuumi.Client
	ready bool
	bridgeConnected bool
	memoryMB uint64
	lastStatusTick time.Time
	startupInProgress bool
	startupFailed bool
	startupStage string
	startupErrors []string
	startupSpinnerIndex int
	engineBatchTitle string
	engineBatchChunks []EngineBatchChunk
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
		width: 80,
		height: 24,
		viewport: vp,
		input: ta,
		bridge: b,
		historyIndex: -1,
		activeContext: "global",
		activeContextPath: "global",
		activeLanguage: "",
		enginePath: strings.TrimSpace(enginePath),
		pipeName: strings.TrimSpace(pipeName),
		startupInProgress: true,
		startupStage: "Initializing workspace...",
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

func (m *AppModel) recalculateLayout() {
	headerHeight := 9
	if m.height < 15 {
		headerHeight = 1
	}
	statusBarHeight := 1
	inputBorderV := 2
	inputHeight := m.input.Height() + inputBorderV
	acHeight := m.autocompleteHeight()
	paddingV := 2
	chatHeight := m.height - headerHeight - statusBarHeight - inputHeight - acHeight - paddingV
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
		if !m.ready {
			m.bridgeConnected = true
			m.ready = true
			m.addSystemMessage("Work environment ready")
		}
		return m, nil

	case bridge.DisconnectedMsg:
		m.flushEngineBatch(false)
		m.bridgeConnected = false
		m.sandboxProcessRunning = false
		m.pendingInputPrompt = ""
		m.addSystemMessage("Disconnected: " + msg.Reason)
		for _, tip := range m.disconnectionHints(msg.Reason) {
			m.addSystemMessage(tip)
		}
		return m, nil

	case coreRestartResultMsg:
		m.flushEngineBatch(false)
		m.pendingInputPrompt = ""
		if msg.Err != nil {
			m.bridgeConnected = false
			m.sandboxProcessRunning = false
			m.addErrorMessage("Core restart failed: " + msg.Err.Error())
			for _, tip := range m.disconnectionHints(msg.Err.Error()) {
				m.addSystemMessage(tip)
			}
			return m, nil
		}

		m.runner = msg.Runner
		m.client = msg.Client
		if msg.Runner != nil {
			m.engineLogPath = strings.TrimSpace(msg.Runner.EngineLogPath)
		}
		m.bridgeConnected = false
		if realBridge, ok := m.bridge.(*bridge.RealYuumiClient); ok {
			realBridge.SetClient(msg.Client)
			realBridge.RegisterMessageHandler()
		}
		m.addSystemMessage("Core restart completed. Waiting for engine handshake...")
		if m.bridge != nil {
			return m, m.bridge.ConnectCmd()
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
			m.activeContext = normaliseContextName(msg.ContextName)
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
		m.recalculateLayout()
		m.refreshViewport()
		if m.bridge != nil {
			return m, m.bridge.ConnectCmd()
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
			code := strings.TrimSpace(m.scriptEditor.Value())
			m.mode = ModeREPL
			if code == "" {
				m.addSystemMessage("Script editor closed: empty content.")
				return m, nil
			}
			scriptName := m.scriptEditor.Filename()
			if scriptName == "" {
				m.addSystemMessage("Script name missing. Use /new \"name\" or /edit \"name\".")
				return m, nil
			}
			m.pendingScriptConfirm = true
			m.pendingScriptCode = code
			m.pendingScriptName = scriptName
			m.pendingScriptLanguage = m.scriptEditor.Language()
			m.addSystemMessage(fmt.Sprintf(scriptRunConfirmTemplate, scriptName))
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
		if !m.ready {
			m.bridgeConnected = true
			m.ready = true
			m.addSystemMessage("Work environment ready")
		}
		return m, nil

	case bridge.DisconnectedMsg:
		m.flushEngineBatch(false)
		m.bridgeConnected = false
		m.pendingInputPrompt = ""
		m.addSystemMessage("Disconnected: " + msg.Reason)
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
			m.activeContext = normaliseContextName(msg.ContextName)
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
	if m.autocomplete.Visible {
		sections = append(sections, m.autocomplete.View(m.width-4))
	}
	sections = append(sections, statusBar)

	full := lg.JoinVertical(lg.Left, sections...)

	v := tea.NewView(pad.Render(full))
	v.AltScreen = true
	v.MouseMode = tea.MouseModeCellMotion
	return v
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
	v := tea.NewView(m.scriptEditor.View())
	v.AltScreen = true
	v.MouseMode = tea.MouseModeNone
	return v
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
	m.isCommandMode = strings.HasPrefix(val, "/") || strings.HasPrefix(val, "$")
	if m.isCommandMode {
		m.autocomplete.Filter(val)
	} else {
		m.autocomplete.Dismiss()
	}

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
		Role: ui.RoleUser,
		Title: m.currentMessageContextTitle(),
		Content: normalised,
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m *AppModel) addZeriMessage(content string, title string) {
	normalised := ui.NormaliseContent(content)
	msg := ui.ChatMessage{
		Role: ui.RoleZeri,
		Title: title,
		Content: normalised,
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m *AppModel) addSystemMessage(content string) {
	normalised := ui.NormaliseContent(content)
	msg := ui.ChatMessage{
		Role: ui.RoleSystem,
		Title: m.currentMessageContextTitle(),
		Content: normalised,
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
	m.isCommandMode = strings.HasPrefix(val, "/") || strings.HasPrefix(val, "$")
	if m.isCommandMode {
		m.autocomplete.Filter(val)
	} else {
		m.autocomplete.Dismiss()
	}
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

	if m.pendingScriptConfirm {
		m.pendingScriptConfirm = false
		lower := strings.ToLower(trimmed)
		if lower == "y" || lower == "yes" {
			return m, m.submitScript(m.pendingScriptCode, true)
		}
		if lower == "s" || lower == "save" {
			return m, m.submitScript(m.pendingScriptCode, false)
		}
		if lower == "n" || lower == "no" {
			m.mode = ModeScriptEditor
			m.addSystemMessage("Save-and-run cancelled. Returned to script editor.")
			return m, nil
		}
		m.pendingScriptCode = ""
		m.pendingScriptName = ""
		m.pendingScriptLanguage = ""
		m.addSystemMessage("Unknown confirmation option. Script workflow cancelled.")
		return m, nil
	}

	if trimmed == editorTriggerCommand || trimmed == editorAliasCommand {
		language := m.scriptLanguageFromContext()
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

func (m AppModel) submitScript(code string, runAfterSave bool) tea.Cmd {
	trimmed := strings.TrimSpace(code)
	if trimmed == "" {
		return nil
	}
	name := strings.TrimSpace(m.pendingScriptName)
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
	if runAfterSave {
		m.addSystemMessage("Script workflow dispatched: save + run.")
	} else {
		m.addSystemMessage("Script workflow dispatched: save only.")
	}
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
		Role: ui.RoleError,
		Title: m.currentMessageContextTitle(),
		Content: normalised,
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m *AppModel) addScriptExecutionMessage(label string, content string) {
	msg := ui.ChatMessage{
		Role: ui.RoleScriptExecution,
		Label: strings.TrimSpace(label),
		Title: m.currentMessageContextTitle(),
		Content: ui.NormaliseContent(content),
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
			Visible: true,
			ScriptName: req.ScriptName,
			Content: content,
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
	return strings.ToLower(trimmed)
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
	msg := ui.ChatMessage {
		Role: ui.RoleCodeView,
		Label: label,
		Title: m.currentMessageContextTitle(),
		Content: "closed",
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m AppModel) scriptLanguageFromContext() string {
	language := strings.TrimSpace(strings.TrimPrefix(m.activeContext, "$"))
	if language == "" || language == "global" || language == "code" {
		return defaultScriptLanguage
	}
	return language
}

func (m AppModel) isCodeContextActive() bool {
	switch m.scriptLanguageFromContext() {
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
		m.addSystemMessage("Restarting core engine and bridge...")
		m.startupInProgress = false
		m.startupFailed = false
		m.sandboxProcessRunning = false
		return m, m.restartCoreCmd()
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
		m.pendingBridgeRequest = PendingBridgeRequest{
			Kind: PendingBridgeRequestNewExistsCheck,
			ScriptName: scriptName,
			Language: m.scriptLanguageFromContext(),
		}
		return m, m.bridge.SendDataCmd(showCommandPrefix + " " + quoteScriptName(scriptName))
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
		m.pendingBridgeRequest = PendingBridgeRequest{
			Kind: PendingBridgeRequestEditLoad,
			ScriptName: scriptName,
			Language: m.scriptLanguageFromContext(),
		}
		return m, m.bridge.SendDataCmd(showCommandPrefix + " " + quoteScriptName(scriptName))
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
		m.pendingBridgeRequest = PendingBridgeRequest{
			Kind: PendingBridgeRequestShowPreview,
			ScriptName: scriptName,
			Language: m.scriptLanguageFromContext(),
		}
		return m, m.bridge.SendDataCmd(showCommandPrefix + " " + quoteScriptName(scriptName))
	default:
		m.addUserMessage(cmd)
		return m, m.bridge.SendDataCmd(cmd)
	}
}

func (m AppModel) restartCoreCmd() tea.Cmd {
	enginePath := strings.TrimSpace(m.enginePath)
	pipeName := strings.TrimSpace(m.pipeName)
	previousRunner := m.runner
	previousClient := m.client

	return func() tea.Msg {
		if previousClient != nil {
			_ = previousClient.Close()
		}
		if previousRunner != nil {
			previousRunner.Stop()
		}

		if enginePath == "" {
			return coreRestartResultMsg{Err: fmt.Errorf("engine path is not configured")}
		}
		if pipeName == "" {
			return coreRestartResultMsg{Err: fmt.Errorf("pipe name is not configured")}
		}

		runner := &yuumi.Runner{
			BinaryPath: enginePath,
			PipeName:   pipeName,
		}

		if err := runner.Start(context.Background()); err != nil {
			return coreRestartResultMsg{Err: err}
		}

		client, err := yuumi.Connect(pipeName)
		if err != nil {
			runner.Stop()
			return coreRestartResultMsg{Err: err}
		}

		runner.SetClient(client)
		return coreRestartResultMsg{Runner: runner, Client: client}
	}
}

func (m AppModel) disconnectionHints(reason string) []string {
	trimmedReason := strings.TrimSpace(reason)
	if trimmedReason == "" {
		trimmedReason = "unknown transport error"
	}

	hints := []string{"Use /restart core to restore the C++ engine connection without closing Zeri."}
	lowerReason := strings.ToLower(trimmedReason)
	if strings.Contains(lowerReason, "eof") || strings.Contains(lowerReason, "0xc0000409") {
		engineLogPath := strings.TrimSpace(m.engineLogPath)
		if engineLogPath == "" && strings.TrimSpace(m.enginePath) != "" {
			engineLogPath = filepath.Join(filepath.Dir(m.enginePath), "zeri-engine.log")
		}
		if engineLogPath != "" {
			hints = append(hints, "Engine crash diagnostics: "+engineLogPath)
		}
		hints = append(hints, "Crash signature detected (EOF/0xc0000409). Verify the executed script path and runtime environment, then retry with /restart core.")
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
