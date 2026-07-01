package main

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"
	"yuumi/internal/aicontext"
	"yuumi/internal/bridge"
	"yuumi/internal/onboarding"
	persistence "yuumi/internal/persistence"
	"yuumi/internal/scripthub"
	"yuumi/internal/system"
	"yuumi/internal/ui"
	"yuumi/pkg/catalog"
	"yuumi/pkg/yuumi"

	"charm.land/bubbles/v2/spinner"
	"charm.land/bubbles/v2/textarea"
	"charm.land/bubbles/v2/viewport"
	tea "charm.land/bubbletea/v2"
	lg "charm.land/lipgloss/v2"
	"github.com/atotto/clipboard"
)

type statusTickMsg struct{}
type executionSpinnerDelayMsg struct{}
type executionSpinnerTickMsg struct{}

// AppMode represents the active interaction mode of the Zeri TUI.
type AppMode int

const (
	ModeREPL AppMode = iota
	ModeScriptEditor
	ModeScriptHub
	ModeOnboarding
)

const (
	editorTriggerCommand  = ":edit"
	editorAliasCommand = ":script"
	copyCommandPrefix = "/copy"
	copyCommandModeLast = "last"
	copyCommandModeAll = "all"
	defaultScriptLanguage = "js"
	saveCommandPrefix = "/save"
	loadCommandPrefix = "/load"
	newCommandPrefix = "/new"
	editCommandPrefix = "/edit"
	showCommandPrefix = "/show"
	runCommandPrefix = "/run"
	deleteCommandPrefix = "/delete"
	runtimeStatusCommand = "/runtime-status"
	settingsCommand = "/settings"
	settingsPathSubcommand = "path"
	restartCoreCommand = "/restart core"
	restartCommand = "restart"
)

const (
	engineConnectTimeout = 5 * time.Second
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

type DeleteConfirmOption int

const (
	DeleteConfirmYes DeleteConfirmOption = iota
	DeleteConfirmCancel
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
	Kind PendingBridgeRequestKind
	ScriptName string
	Language string
}

type SessionBridgeOperationKind int

const (
	SessionBridgeOperationNone SessionBridgeOperationKind = iota
	SessionBridgeOperationSave
	SessionBridgeOperationLoad
)

type SessionBridgeOperation struct {
	Kind SessionBridgeOperationKind
	SnapshotName string
	Snapshot SessionSnapshot
	Overwrite bool
}

type CodePreviewState struct {
	Visible bool
	ScriptName string
	Content string
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

func executionSpinnerDelayCmd() tea.Cmd {
	return tea.Tick(500*time.Millisecond, func(time.Time) tea.Msg {
		return executionSpinnerDelayMsg{}
	})
}

const escClearWindow = 700 * time.Millisecond

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
	selectionMenuVisible bool
	selectionMenuTitle string
	selectionMenuOptions []string
	selectionMenuCursor int
	isCommandMode bool
	pendingReset bool
	pendingScriptExecution bool
	pendingScriptLabel string
	pendingScriptCode string
	pendingScriptName string
	pendingScriptLanguage string
	pendingScriptIntent ScriptEditorIntent
	saveMenu SaveMenuState
	saveMenuCursor SaveMenuOption
	pendingSessionPrompt SessionPromptKind
	sessionOverwriteVisible bool
	sessionOverwriteCursor SessionOverwriteOption
	pendingSessionOverwriteName string
	pendingSessionBridgeOp SessionBridgeOperation
	pendingLoadContextSync string
	deleteConfirmVisible bool
	deleteConfirmCursor DeleteConfirmOption
	pendingDeleteScriptName string
	pendingDeleteLanguage string
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
	engineState ConnectionState
	engineRestartCount int
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
	scriptHub scripthub.ScriptHubModel
	aiContext aicontext.AiContextModel
	aiSharedScope map[string]interface{}
	aiModeActive bool
	aiPreviousContext string
	aiPreviousContextPath string
	aiPreviousLanguage string
	aiActiveLanguageHint string
	aiConfigured bool
	aiWelcomeShown bool
	pendingAiPrompt string
	pendingAiPromptSystem string
	executionSpinner spinner.Model
	executionInFlight bool
	executionSpinnerVisible bool
	executionSpinnerDelayPending bool
	onboardingModel onboarding.TutorialModel
	onboardingActive bool
	onboardingHubOpen bool
	startupOptions appOptions
	startupProfiler *startupProfiler
	lastEscAt time.Time
	settingsVisible bool
	pendingSettingsRequest SettingsRequestKind
	pendingSettingsUpdate PendingSettingsUpdate
	settingsCenter SettingsCenterState
}

func newAppModel(b bridge.YuumiClient, enginePath string, pipeName string, opts appOptions, profiler *startupProfiler) AppModel {
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

	aiModel := aicontext.New("", "")
	pythonDetected := false
	if manifest, err := loadRuntimeManifest(); err == nil {
		for _, runtimeEntry := range manifest.Runtimes {
			if strings.EqualFold(strings.TrimSpace(runtimeEntry.Name), "python") {
				pythonDetected = true
				break
			}
		}
	}
	onboardingRequired := needsOnboarding(opts.noOnboarding)
	defaultDataParent, _ := DefaultDataParent()
	onboardingModel := onboarding.New(pythonDetected, defaultDataParent)
	execSpinner := spinner.New()
	execSpinner.Spinner = spinner.Dot
	aiCfg := buildSettingsCenterState(SettingsCenterState{})

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
		engineState: ConnectionConnected,
		enginePath: strings.TrimSpace(enginePath),
		pipeName: strings.TrimSpace(pipeName),
		startupInProgress: true,
		startupStage: "Initializing workspace...",
		aiContext: aiModel,
		aiConfigured: aiCfg.AiConfigured,
		aiSharedScope: map[string]interface{}{},
		onboardingModel: onboardingModel,
		onboardingActive: onboardingRequired,
		startupOptions: opts,
		startupProfiler: profiler,
		executionSpinner: execSpinner,
		settingsCenter: aiCfg,
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

	if m.selectionMenuVisible {
		height += lg.Height(m.renderSelectionMenu())
	}

	if m.sessionOverwriteVisible {
		height += lg.Height(m.renderSessionOverwriteMenu())
	}

	if m.deleteConfirmVisible {
		height += lg.Height(m.renderDeleteConfirmMenu())
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
	if m.aiModeActive && m.aiContext.IsStreaming() {
		panel := m.renderAiStreamingPanel()
		if content == "" {
			content = panel
		} else {
			content = content + "\n\n" + panel
		}
	}
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

func (m AppModel) renderOnboardingInput(width int) string {
	inner := width - 2
	if m.width < 62 {
		inner = m.width - 4
	}
	if inner < 10 {
		inner = 10
	}
	m.input.SetWidth(inner)
	prompt := lg.NewStyle().Foreground(ui.ColourVolt).Render("›")
	return lg.JoinHorizontal(lg.Top, prompt+" ", m.input.View())
}

func (m AppModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch m.mode {
	case ModeScriptEditor:
		return m.updateScriptEditor(msg)
	case ModeScriptHub:
		return m.updateScriptHub(msg)
	case ModeOnboarding:
		return m.updateOnboarding(msg)
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
		if m.settingsVisible {
			key := msg.String()
			if key == "esc" || key == "escape" {
				m.settingsVisible = false
				return m, nil
			}
			if key == "ctrl+c" {
				return m, tea.Quit
			}
			return m, nil
		}
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
		return m, tickStatusCmd()
	case executionSpinnerDelayMsg:
		if m.executionInFlight && m.executionSpinnerDelayPending {
			m.executionSpinnerVisible = true
			m.executionSpinnerDelayPending = false
			return m, m.executionSpinner.Tick
		}
		return m, nil
	case spinner.TickMsg:
		if m.executionSpinnerVisible {
			var cmd tea.Cmd
			m.executionSpinner, cmd = m.executionSpinner.Update(msg)
			return m, cmd
		}
		return m, nil

	case aicontext.AiConnectedMsg, aicontext.AiErrorMsg, aicontext.TokenMsg, aicontext.StreamDoneMsg, aicontext.StreamCancelledMsg, aicontext.AiStreamStartedMsg:
		cmd := m.aiContext.ApplyMsg(msg)
		switch typed := msg.(type) {
		case aicontext.AiConnectedMsg:
			m.addSystemMessage("AI context ready.")
		case aicontext.AiErrorMsg:
			m.addErrorMessage(strings.TrimSpace(typed.Err))
		case aicontext.TokenMsg:
			m.refreshViewport()
		case aicontext.StreamDoneMsg:
			content := strings.TrimSpace(m.aiContext.CurrentResponse())
			if content != "" {
				if m.aiContext.ShowActions() {
					lang := m.aiActionLanguage()
					content += "\n\n[R] Run in $" + lang + "  [E] Edit in Script Editor  [S] Save to Hub"
				}
				m.addZeriMessage(content, m.currentMessageContextTitle())
			}
		case aicontext.StreamCancelledMsg:
			content := strings.TrimSpace(m.aiContext.CurrentResponse())
			if content != "" {
				m.addZeriMessage(content, m.currentMessageContextTitle())
			}
		}
		return m, cmd

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
		m.stopExecutionSpinner()
		m.engineState = ConnectionDisconnected
		m.bridgeConnected = false
		m.sandboxProcessRunning = false
		m.pendingInputPrompt = ""
		m.clearSelectionMenu()
		if !m.startupInProgress {
			m.addSystemMessage("ZeriEngine disconnected. Type '/restart' to reconnect.")
			if reason := strings.TrimSpace(msg.Reason); reason != "" {
				m.addSystemMessage("Reason: " + reason)
			}
			for _, tip := range m.disconnectionHints(msg.Reason) {
				m.addSystemMessage(tip)
			}
		}
		return m, nil

	case engineConnectedMsg:
		m.flushEngineBatch(false)
		m.stopExecutionSpinner()
		m.pendingInputPrompt = ""
		m.clearSelectionMenu()
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
		m.stopExecutionSpinner()
		m.engineState = ConnectionDisconnected
		m.bridgeConnected = false
		m.sandboxProcessRunning = false
		m.pendingInputPrompt = ""
		m.clearSelectionMenu()
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

	case bridge.StreamBatchEndMsg:
		m.flushEngineBatch(false)
		if strings.EqualFold(strings.TrimSpace(msg.Reason), "execution_cancelled") {
			m.addSystemMessage("^C  (execution cancelled)")
		}
		if normaliseContextName(m.pendingLoadContextSync) == "global" {
			m.pendingLoadContextSync = ""
			m.pendingContextPath = ""
		}
		m.stopExecutionSpinner()
		return m, nil

	case bridge.ScriptActionResponseMsg:
		if !msg.Ok {
			m.addErrorMessage("Script action failed: " + strings.TrimSpace(msg.Error))
		}
		return m, nil

	case bridge.SessionStateResponseMsg:
		return m.handleSessionStateResponse(msg)

	case bridge.SharedScopeSnapshotMsg:
		m.aiSharedScope = map[string]interface{}{}
		for key, value := range msg.Entries {
			m.aiSharedScope[key] = value
		}
		if strings.TrimSpace(m.pendingAiPrompt) != "" {
			prompt := m.pendingAiPrompt
			systemPrompt := m.pendingAiPromptSystem
			m.pendingAiPrompt = ""
			m.pendingAiPromptSystem = ""
			return m, m.aiContext.BeginRequest(prompt, systemPrompt)
		}
		return m, nil

	case bridge.SettingsSnapshotResponseMsg:
		return m.handleSettingsSnapshotResponse(msg)

	case bridge.SettingsUpdateResponseMsg:
		return m.handleSettingsUpdateResponse(msg)

	case bridge.InputRequestMsg:
		m.flushEngineBatch(false)
		m.clearSelectionMenu()
		m.pendingInputPrompt = strings.TrimSpace(msg.Prompt)
		if m.pendingInputPrompt == "" {
			m.addSystemMessage("Input required by running process.")
		} else {
			m.addSystemMessage("Input required: " + m.pendingInputPrompt)
		}
		return m, nil

	case bridge.SelectionRequestMsg:
		m.flushEngineBatch(false)
		m.pendingInputPrompt = ""
		m.selectionMenuTitle = strings.TrimSpace(msg.Title)
		m.selectionMenuOptions = append([]string{}, msg.Options...)
		if len(m.selectionMenuOptions) == 0 {
			m.selectionMenuOptions = []string{"Cancel"}
		}
		m.selectionMenuCursor = 0
		m.selectionMenuVisible = true
		m.recalculateLayout()
		return m, nil

	case bridge.ContextChangedMsg:
		m.flushEngineBatch(false)
		m.pendingInputPrompt = ""
		m.clearSelectionMenu()
		if m.aiModeActive {
			return m, nil
		}
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
		if strings.TrimSpace(m.pendingLoadContextSync) != "" &&
			normaliseContextName(m.activeContextPath) == normaliseContextName(m.pendingLoadContextSync) {
			m.pendingLoadContextSync = ""
		}
		m.autocomplete.ActiveContext = m.activeContext
		m.refreshViewport()
		return m, nil

	case startupPhaseMsg:
		m.startupStage = msg.Title
		return m, nil

	case startupFailedMsg:
		m.flushEngineBatch(false)
		m.stopExecutionSpinner()
		m.pendingInputPrompt = ""
		m.clearSelectionMenu()
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
		m.stopExecutionSpinner()
		m.pendingInputPrompt = ""
		m.clearSelectionMenu()
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
		m.engineState = ConnectionConnected
		m.bridgeConnected = true
		if m.onboardingActive {
			m.mode = ModeOnboarding
		}
		if m.startupProfiler != nil {
			m.startupProfiler.Mark("handshake → first prompt rendered")
			if m.startupOptions.profileStartup {
				m.startupProfiler.Print()
			}
		}
		if m.startupOptions.profileStartup || m.startupOptions.exitAfterReady {
			return m, tea.Quit
		}
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
		case "alt+enter", "alt+return", "shift+enter", "shift+return", "alt+shift+enter":
			m.pendingScriptCode = m.scriptEditor.Value()
			m.pendingScriptName = m.scriptEditor.Filename()
			m.pendingScriptLanguage = m.scriptEditor.Language()
			m.saveMenu = SaveMenuVisible
			m.saveMenuCursor = SaveAndExecute
			m.mode = ModeREPL
			m.recalculateLayout()
			return m, nil
		case "tab":
			return m, m.applyScriptEditorPaste("\t")
		}

		if isEditorCopyShortcut(normalizedKey) {
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
				m.addSystemMessage("Paste failed: " + err.Error() + ". Try Ctrl+Shift+V, Shift+Insert, or your terminal paste action.")
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
		return m, tickStatusCmd()

	case bridge.ConnectedMsg:
		m.engineState = ConnectionConnected
		if !m.ready {
			m.bridgeConnected = true
			m.ready = true
		}
		m.bridgeConnected = true
		return m, nil

	case bridge.DataMsg:
		m.consumeEngineOutput(msg.Content)
		return m, nil

	case bridge.ErrorMsg:
		m.consumeEngineError(msg.Content)
		return m, nil

	case bridge.StreamBatchEndMsg:
		m.flushEngineBatch(false)
		if strings.EqualFold(strings.TrimSpace(msg.Reason), "execution_cancelled") {
			m.addSystemMessage("^C  (execution cancelled)")
		}
		m.stopExecutionSpinner()
		return m, nil

	case bridge.ScriptActionResponseMsg:
		if !msg.Ok {
			m.addErrorMessage("Script action failed: " + strings.TrimSpace(msg.Error))
		}
		return m, nil

	case bridge.InputRequestMsg:
		m.flushEngineBatch(false)
		m.clearSelectionMenu()
		m.pendingInputPrompt = strings.TrimSpace(msg.Prompt)
		if m.pendingInputPrompt == "" {
			m.addSystemMessage("Input required by running process.")
		} else {
			m.addSystemMessage("Input required: " + m.pendingInputPrompt)
		}
		return m, nil

	case bridge.SelectionRequestMsg:
		m.flushEngineBatch(false)
		m.pendingInputPrompt = ""
		m.selectionMenuTitle = strings.TrimSpace(msg.Title)
		m.selectionMenuOptions = append([]string{}, msg.Options...)
		if len(m.selectionMenuOptions) == 0 {
			m.selectionMenuOptions = []string{"Cancel"}
		}
		m.selectionMenuCursor = 0
		m.selectionMenuVisible = true
		m.recalculateLayout()
		return m, nil

	case bridge.ContextChangedMsg:
		m.flushEngineBatch(false)
		m.pendingInputPrompt = ""
		m.clearSelectionMenu()
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

func (m AppModel) updateScriptHub(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		m.recalculateLayout()
		m.scriptHub.SetWidth(msg.Width)
		m.scriptHub.SetHeight(msg.Height)
		return m, nil

	case scripthub.CloseMsg:
		if m.onboardingHubOpen {
			m.onboardingHubOpen = false
			m.mode = ModeOnboarding
			return m, nil
		}
		m.mode = ModeREPL
		return m, nil

	case scripthub.OpenEditorMsg:
		m.mode = ModeScriptEditor
		m.pendingScriptIntent = ScriptEditorIntentEdit
		m.scriptEditor = ui.NewScriptEditorWithContent(msg.Entry.Lang, m.width, m.height, msg.Entry.Name, msg.Entry.Content)
		return m, nil
	case onboarding.TutorialHubAutoCloseMsg:
		if m.onboardingHubOpen {
			m.onboardingHubOpen = false
			m.mode = ModeOnboarding
			var action onboarding.TutorialAction
			m.onboardingModel, action = m.onboardingModel.Update(onboarding.TutorialHubAutoCloseMsg{})
			return m, m.applyOnboardingAction(action)
		}
		return m, nil
	}

	var cmd tea.Cmd
	m.scriptHub, cmd = m.scriptHub.Update(msg)
	return m, cmd
}

func (m AppModel) updateOnboarding(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch typed := msg.(type) {
	case statusTickMsg:
		m.memoryMB = system.GetProcessMemoryMB()
		if m.startupInProgress {
			m.startupSpinnerIndex = (m.startupSpinnerIndex + 1) % 4
		}
		return m, tickStatusCmd()
	case tea.WindowSizeMsg:
		m.width = typed.Width
		m.height = typed.Height
		m.recalculateLayout()
		m.onboardingModel.SetSize(typed.Width, typed.Height)
		return m, nil
	case tea.PasteMsg:
		return m, m.applyInputPaste(typed.Content)
	case bridge.StreamBatchEndMsg:
		if strings.EqualFold(strings.TrimSpace(typed.Reason), "execution_cancelled") {
			m.addSystemMessage("^C  (execution cancelled)")
		}
		m.stopExecutionSpinner()
		if m.onboardingModel.Step() == onboarding.StepRunCode {
			var action onboarding.TutorialAction
			m.onboardingModel, action = m.onboardingModel.Update(onboarding.TutorialRunCompletedMsg())
			return m, m.applyOnboardingAction(action)
		}
		if m.onboardingModel.Step() == onboarding.StepSaveScript {
			var action onboarding.TutorialAction
			m.onboardingModel, action = m.onboardingModel.Update(onboarding.TutorialSaveCompletedMsg())
			return m, m.applyOnboardingAction(action)
		}
		return m, nil
	case executionSpinnerDelayMsg:
		if m.executionInFlight && m.executionSpinnerDelayPending {
			m.executionSpinnerVisible = true
			m.executionSpinnerDelayPending = false
			return m, m.executionSpinner.Tick
		}
		return m, nil
	case spinner.TickMsg:
		if m.executionSpinnerVisible {
			var cmd tea.Cmd
			m.executionSpinner, cmd = m.executionSpinner.Update(typed)
			return m, cmd
		}
		return m, nil
	case onboarding.TutorialCommandMsg:
		var action onboarding.TutorialAction
		m.onboardingModel, action = m.onboardingModel.Update(typed)
		return m, m.applyOnboardingAction(action)
	case onboarding.DataRootCompletedMsg, onboarding.DataRootFailedMsg:
		var action onboarding.TutorialAction
		m.onboardingModel, action = m.onboardingModel.Update(typed)
		return m, m.applyOnboardingAction(action)
	case tea.KeyPressMsg:
		key := normalisedKeyPress(typed)
		switch key {
		case "ctrl+c", "esc", "escape", "ctrl+b", "left":
			var action onboarding.TutorialAction
			m.onboardingModel, action = m.onboardingModel.Update(typed)
			return m, m.applyOnboardingAction(action)
		case "ctrl+h":
			if m.onboardingModel.Step() == onboarding.StepScriptHub {
				var action onboarding.TutorialAction
				m.onboardingModel, action = m.onboardingModel.Update(typed)
				return m, m.applyOnboardingAction(action)
			}
			return m, nil
		}
		if (typed.String() == "enter" || typed.String() == "return") && !typed.Mod.Contains(tea.ModShift) {
			if m.onboardingModel.Step() == onboarding.StepWelcome {
				var action onboarding.TutorialAction
				m.onboardingModel, action = m.onboardingModel.Update(typed)
				return m, m.applyOnboardingAction(action)
			}
			raw := strings.TrimSpace(m.input.Value())
			if raw == "" && m.onboardingModel.Step() != onboarding.StepChoosePath {
				return m, nil
			}
			m.input.Reset()
			m.input.SetHeight(1)
			var action onboarding.TutorialAction
			m.onboardingModel, action = m.onboardingModel.Update(onboarding.TutorialCommandMsg{Input: raw})
			return m, m.applyOnboardingAction(action)
		}
		var cmd tea.Cmd
		m.input, cmd = m.input.Update(typed)
		return m, cmd
	case scripthub.CloseMsg:
		if m.onboardingHubOpen {
			m.onboardingHubOpen = false
			m.mode = ModeOnboarding
			return m, nil
		}
		return m, nil
	}

	return m, nil
}

func (m *AppModel) applyOnboardingAction(action onboarding.TutorialAction) tea.Cmd {
	switch action.Kind {
	case onboarding.TutorialActionSendCommand:
		command := strings.TrimSpace(action.Payload)
		if command == "" {
			return nil
		}
		m.addUserMessage(command)
		m.executionSubmitted()
		return tea.Batch(m.bridge.SendDataCmd(command), executionSpinnerDelayCmd())
	case onboarding.TutorialActionOpenScriptHub:
		runtimes := availableScriptHubRuntimes()
		m.scriptHub = scripthub.New(m.bridge, runtimes, m.width, m.height)
		m.onboardingHubOpen = true
		m.mode = ModeScriptHub
		return tea.Batch(m.scriptHub.Init(), onboarding.TutorialHubAutoCloseCmd())
	case onboarding.TutorialActionSetDataRoot:
		parent := strings.TrimSpace(action.Payload)
		return func() tea.Msg {
			notice := ""
			if _, alreadyHasData, alreadyChosen, inspectErr := persistence.InspectDataParent(parent); inspectErr != nil {
				return onboarding.DataRootFailedMsg{Reason: inspectErr.Error()}
			} else if alreadyHasData || alreadyChosen {
				notice = "That folder already contains a Zeri data directory; it will be reused."
			}
			dataRoot, err := SetDataRootUnderParent(parent)
			if err != nil {
				return onboarding.DataRootFailedMsg{Reason: err.Error()}
			}
			return onboarding.DataRootCompletedMsg{Path: dataRoot, Notice: notice}
		}
	case onboarding.TutorialActionMarkCompleted:
		if _, err := ensureCommittedDataRoot(); err != nil {
			m.addErrorMessage("Could not finalize onboarding.\n" + err.Error())
			return nil
		}
		if err := saveOnboardingCompleted(); err != nil {
			m.addErrorMessage("Could not save onboarding state.\n" + err.Error())
			return nil
		}
		m.onboardingActive = false
		m.mode = ModeREPL
		m.addSystemMessage("Onboarding completed. Welcome to Zeri.")
		return nil
	case onboarding.TutorialActionExitToRepl:
		dataRoot, err := ensureCommittedDataRoot()
		if err != nil {
			m.addErrorMessage("Onboarding was skipped, but data location setup failed.\n" + err.Error())
			return nil
		}
		m.onboardingActive = false
		m.mode = ModeREPL
		m.addSystemMessage("Onboarding skipped. Data location set to: " + dataRoot + ".\nUse /settings path <parent-folder> to change it later.")
		return nil
	}
	return nil
}

func ensureCommittedDataRoot() (string, error) {
	dataRoot, ok, err := ResolveDataRoot()
	if err != nil {
		return "", err
	}
	if ok {
		return dataRoot, nil
	}
	home, err := ConfigHomeDir()
	if err != nil {
		return "", err
	}
	if err := AdoptDataRoot(home); err != nil {
		return "", err
	}
	dataRoot, ok, err = ResolveDataRoot()
	if err != nil {
		return "", err
	}
	if !ok {
		return "", fmt.Errorf("data root is not configured after default adoption")
	}
	return dataRoot, nil
}

func (m AppModel) View() tea.View {
	switch m.mode {
	case ModeScriptEditor:
		return m.viewScriptEditor()
	case ModeScriptHub:
		return m.viewScriptHub()
	case ModeOnboarding:
		return m.viewOnboarding()
	default:
		return m.viewREPL()
	}
}

func (m AppModel) viewREPL() tea.View {
	pad := lg.NewStyle().Padding(1, 2)
	displayContextPath := m.currentDisplayContextPath()

	header := ui.RenderHeader(m.width, m.height)
	if m.startupInProgress || m.startupFailed {
		statusBar := ui.RenderStatusBar(m.width, displayContextPath, m.bridgeConnected, m.memoryMB, m.sandboxProcessRunning, m.statusActivityLabel())
		startupPanel := m.renderStartupPanel()
		full := lg.JoinVertical(lg.Left, header, startupPanel, statusBar)
		v := tea.NewView(pad.Render(full))
		v.AltScreen = true
		v.MouseMode = tea.MouseModeCellMotion
		return v
	}

	if m.runtimeCenterVisible {
		statusBar := ui.RenderStatusBar(m.width, displayContextPath, m.bridgeConnected, m.memoryMB, m.sandboxProcessRunning, m.statusActivityLabel())
		runtimePanel := m.renderRuntimeCenterModal()
		full := lg.JoinVertical(lg.Left, header, runtimePanel, statusBar)
		v := tea.NewView(pad.Render(full))
		v.AltScreen = true
		v.MouseMode = tea.MouseModeCellMotion
		return v
	}

	if m.settingsVisible {
		statusBar := ui.RenderStatusBar(m.width, displayContextPath, m.bridgeConnected, m.memoryMB, m.sandboxProcessRunning, m.statusActivityLabel())
		settingsPanel := m.renderSettingsModal()
		full := lg.JoinVertical(lg.Left, header, settingsPanel, statusBar)
		v := tea.NewView(pad.Render(full))
		v.AltScreen = true
		v.MouseMode = tea.MouseModeCellMotion
		return v
	}

	chatArea := m.viewport.View()
	statusBar := ui.RenderStatusBar(m.width, displayContextPath, m.bridgeConnected, m.memoryMB, m.sandboxProcessRunning, m.statusActivityLabel())
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

func (m AppModel) viewScriptHub() tea.View {
	v := tea.NewView(m.scriptHub.View())
	v.AltScreen = true
	v.MouseMode = tea.MouseModeCellMotion
	return v
}

func (m AppModel) viewOnboarding() tea.View {
	pad := lg.NewStyle().Padding(1, 2)
	header := ui.RenderHeader(m.width, m.height)
	statusBar := ui.RenderOnboardingStatusBar(m.width, m.onboardingModel.Legend())
	panelWidth := max(56, m.width-6)
	innerWidth := panelWidth - 6
	if innerWidth < 10 {
		innerWidth = 10
	}
	title := lg.NewStyle().Foreground(ui.ColourVolt).Bold(true).Render("Getting started")
	instruction := lg.NewStyle().Foreground(ui.ColourWhite).Render(ui.NormaliseContent(m.onboardingModel.Instruction()))
	feedbackStyle := ui.ColourAcidGreen
	if m.onboardingModel.ConfirmingExit() {
		feedbackStyle = ui.ColourVolt
	}
	feedback := ""
	if strings.TrimSpace(m.onboardingModel.Feedback()) != "" {
		feedback = lg.NewStyle().Foreground(feedbackStyle).Render(ui.NormaliseContent(m.onboardingModel.Feedback()))
	}
	input := m.renderOnboardingInput(innerWidth)
	legend := lg.NewStyle().Foreground(ui.ColourIndustrialGrey).Render(ui.NormaliseContent(m.onboardingModel.Legend()))
	content := lg.JoinVertical(lg.Left, title, instruction, feedback, input, legend)
	panel := lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ui.ColourElectricBlue).
		Padding(1, 2).
		Width(panelWidth).
		Render(content)
	full := lg.JoinVertical(lg.Left, header, panel, statusBar)
	v := tea.NewView(pad.Render(full))
	v.AltScreen = true
	v.MouseMode = tea.MouseModeCellMotion
	return v
}

func (m AppModel) appendREPLAuxiliarySections(sections []string) []string {
	if m.autocomplete.Visible {
		sections = append(sections, m.autocomplete.View(m.width-4))
	}

	if m.selectionMenuVisible {
		sections = append(sections, m.renderSelectionMenu())
	}

	if m.sessionOverwriteVisible {
		sections = append(sections, m.renderSessionOverwriteMenu())
	}

	if m.deleteConfirmVisible {
		sections = append(sections, m.renderDeleteConfirmMenu())
	}

	if m.saveMenu == SaveMenuVisible {
		sections = append(sections, m.renderScriptSaveMenu())
	}

	return sections
}

func (m AppModel) renderSelectionMenu() string {
	title := strings.TrimSpace(m.selectionMenuTitle)
	if title == "" {
		title = "Select an option"
	}
	options := m.selectionMenuOptions
	if len(options) == 0 {
		options = []string{"(no options available)"}
	}

	rows := make([]string, 0, len(options))
	for idx, option := range options {
		prefix := "   "
		if idx == m.selectionMenuCursor {
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

func (m *AppModel) clearSelectionMenu() {
	wasVisible := m.selectionMenuVisible
	m.selectionMenuVisible = false
	m.selectionMenuTitle = ""
	m.selectionMenuOptions = nil
	m.selectionMenuCursor = 0
	if wasVisible {
		m.recalculateLayout()
	}
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

func (m AppModel) renderDeleteConfirmMenu() string {
	ext := languageExtension(m.pendingDeleteLanguage)
	filename := m.pendingDeleteScriptName + "." + ext
	title := fmt.Sprintf("Delete %q? This action cannot be undone.", filename)

	options := []string{"Yes, delete", "Cancel"}
	rows := make([]string, 0, len(options))
	for idx, option := range options {
		prefix := "   "
		if DeleteConfirmOption(idx) == m.deleteConfirmCursor {
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
		BorderForeground(ui.ColourErrorRed).
		Padding(0, 1).
		Width(width).
		Render(panel)
}

func (m AppModel) handleDeleteConfirm() (tea.Model, tea.Cmd) {
	cursor := m.deleteConfirmCursor
	name := m.pendingDeleteScriptName
	language := m.pendingDeleteLanguage

	m.deleteConfirmVisible = false
	m.pendingDeleteScriptName = ""
	m.pendingDeleteLanguage = ""
	m.recalculateLayout()

	if cursor == DeleteConfirmCancel {
		m.addSystemMessage("Deletion cancelled.")
		return m, nil
	}

	deleteCmd := m.bridge.SendCommandPayloadCmd(map[string]interface{}{
		"type": catalog.BridgeTypeValue(catalog.BridgeTypeDeleteScriptID),
		"name": name,
		"lang": language,
	})
	if deleteCmd == nil {
		m.addErrorMessage("Delete failed: bridge is unavailable.")
		return m, nil
	}
	m.addSystemMessage(fmt.Sprintf("Delete requested for script %q.", name))
	return m, deleteCmd
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
	case "ctrl+shift+v", "shift+insert", "meta+v", "cmd+v":
		return true
	default:
		return false
	}
}

func isInputCopyShortcut(normalizedKey string) bool {
	switch normalizedKey {
	case "ctrl+shift+c", "ctrl+insert":
		return true
	default:
		return false
	}
}

func isEditorCopyShortcut(normalizedKey string) bool {
	switch normalizedKey {
	case "ctrl+c", "ctrl+shift+c", "ctrl+insert":
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

func (m AppModel) renderAiStreamingPanel() string {
	frames := []string{"⠋", "⠙", "⠹", "⠸"}
	frame := frames[m.startupSpinnerIndex%len(frames)]
	header := lg.NewStyle().
		Foreground(ui.ColourWhite).
		Background(ui.ColourElectricBlue).
		Bold(true).
		Padding(0, 1).
		Render(frame + " AI generating...")
	body := lg.NewStyle().
		Foreground(ui.ColourWhite).
		Padding(0, 1).
		Render(ui.NormaliseContent(m.aiContext.CurrentResponse()))
	return lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ui.ColourElectricBlue).
		Render(lg.JoinVertical(lg.Left, header, body))
}

func (m AppModel) statusActivityLabel() string {
	if m.executionSpinnerVisible {
		return m.executionSpinner.View() + " Running command..."
	}
	if m.aiContext.Checking() {
		frames := []string{"⠋", "⠙", "⠹", "⠸"}
		frame := frames[m.startupSpinnerIndex%len(frames)]
		return frame + " AI endpoint check"
	}
	if m.aiContext.IsStreaming() {
		frames := []string{"⠋", "⠙", "⠹", "⠸"}
		frame := frames[m.startupSpinnerIndex%len(frames)]
		return frame + " AI generating"
	}
	return ""
}

func (m *AppModel) executionSubmitted() {
	m.executionInFlight = true
	m.executionSpinnerVisible = false
	m.executionSpinnerDelayPending = true
}

func (m *AppModel) stopExecutionSpinner() {
	m.executionInFlight = false
	m.executionSpinnerVisible = false
	m.executionSpinnerDelayPending = false
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

func (m *AppModel) addScriptSavedMessage(content string) {
	normalised := ui.NormaliseContent(content)
	msg := ui.ChatMessage{
		Role: ui.RoleScriptSaved,
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

	if m.selectionMenuVisible {
		switch normalizedKey {
		case "up":
			if m.selectionMenuCursor > 0 {
				m.selectionMenuCursor--
			}
			return m, nil
		case "down":
			if m.selectionMenuCursor < len(m.selectionMenuOptions)-1 {
				m.selectionMenuCursor++
			}
			return m, nil
		case "esc", "escape":
			cancelIndex := len(m.selectionMenuOptions) - 1
			if cancelIndex < 0 {
				cancelIndex = 0
			}
			m.clearSelectionMenu()
			m.recalculateLayout()
			return m, m.bridge.SendInputResponseCmd(strconv.Itoa(cancelIndex))
		case "enter", "return":
			selectedIndex := m.selectionMenuCursor
			if selectedIndex < 0 || selectedIndex >= len(m.selectionMenuOptions) {
				selectedIndex = 0
			}
			m.clearSelectionMenu()
			m.recalculateLayout()
			return m, m.bridge.SendInputResponseCmd(strconv.Itoa(selectedIndex))
		default:
			return m, nil
		}
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
				m.sessionOverwriteVisible = false
				m.pendingSessionOverwriteName = ""
				m.recalculateLayout()
				return m.requestSessionStateSave(name, true)
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

	if m.deleteConfirmVisible {
		switch normalizedKey {
		case "up":
			if m.deleteConfirmCursor > DeleteConfirmYes {
				m.deleteConfirmCursor--
			}
			return m, nil
		case "down":
			if m.deleteConfirmCursor < DeleteConfirmCancel {
				m.deleteConfirmCursor++
			}
			return m, nil
		case "esc", "escape":
			m.deleteConfirmVisible = false
			m.pendingDeleteScriptName = ""
			m.pendingDeleteLanguage = ""
			m.recalculateLayout()
			m.addSystemMessage("Deletion cancelled.")
			return m, nil
		case "enter", "return":
			return m.handleDeleteConfirm()
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

	if m.aiModeActive && m.aiContext.IsStreaming() && normalizedKey == "ctrl+c" {
		m.aiContext.CancelStreaming()
		m.addSystemMessage("AI generation cancelled. Partial output preserved.")
		return m, nil
	}

	if m.aiModeActive && m.aiContext.ShowActions() && strings.TrimSpace(m.input.Value()) == "" {
		switch normalizedKey {
		case "r":
			return m, m.runAiCodeBlock()
		case "e":
			m.editAiCodeBlock()
			return m, nil
		case "s":
			return m, m.saveAiCodeBlock()
		}
	}

	if normalizedKey == "ctrl+c" {
		if m.executionInFlight {
			m.executionSpinnerDelayPending = false
			m.executionSpinnerVisible = true
			return m, m.bridge.SendCommandPayloadCmd(map[string]interface{}{"type": catalog.BridgeTypeValue(catalog.BridgeTypeCancelExecutionID)})
		}
		return m, tea.Quit
	}

	if normalizedKey == "ctrl+h" {
		runtimes := availableScriptHubRuntimes()
		m.scriptHub = scripthub.New(m.bridge, runtimes, m.width, m.height)
		m.mode = ModeScriptHub
		return m, m.scriptHub.Init()
	}

	if isInputCopyShortcut(normalizedKey) {
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
			m.addSystemMessage("Paste failed: " + err.Error() + ". Try Ctrl+Shift+V, Shift+Insert, or your terminal paste action.")
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

	if normalizedKey == "esc" || normalizedKey == "escape" {
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
		if strings.TrimSpace(m.input.Value()) != "" {
			now := time.Now()
			if !m.lastEscAt.IsZero() && now.Sub(m.lastEscAt) <= escClearWindow {
				m.input.SetValue("")
				m.input.SetHeight(1)
				m.recalculateLayout()
				m.updateAutocompleteForInput("")
				m.lastEscAt = time.Time{}
				return m, nil
			}
			m.lastEscAt = now
			m.addSystemMessage("Press Esc again to clear input.")
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
		m.addSystemMessage("Engine unavailable. Type 'restart' to reconnect.")
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
			m.executionSubmitted()
			return m, tea.Batch(m.bridge.SendDataCmd("/reset"), executionSpinnerDelayCmd())
		}
		m.addSystemMessage("Reset cancelled.")
		return m, nil
	}

	m.inputHistory = append([]string{trimmed}, m.inputHistory...)
	m.historyIndex = -1
	m.draftBuffer = ""

	if strings.EqualFold(trimmed, "$ai") {
		m.addUserMessage(trimmed)
		if m.bridge != nil {
			return m, m.requestSettingsSnapshot(SettingsRequestAiEntry)
		}
		return m.enterAiContext()
	}

	if m.aiModeActive {
		if target, ok := parseDirectContextSwitchCommand(trimmed); ok && target != "ai" {
			m.addUserMessage(trimmed)
			m.exitAiContext()
			m.queuePendingContextTitleIfSwitch(trimmed)
			m.executionSubmitted()
			return m, tea.Batch(m.bridge.SendDataCmd(trimmed), executionSpinnerDelayCmd())
		}
	}

	if strings.HasPrefix(trimmed, "/") {
		return m.handleSlashCommand(trimmed)
	}

	if normalizedInput == restartCommand {
		m.addUserMessage(trimmed)
		return m.handleRestart()
	}

	m.queuePendingContextTitleIfSwitch(trimmed)

	m.addUserMessage(trimmed)
	if m.aiModeActive {
		systemPrompt := aicontext.BuildSystemPrompt(m.aiPromptLanguage(), m.aiSharedScope, m.aiContext.SystemPromptOverride())
		if m.bridge != nil {
			m.pendingAiPrompt = trimmed
			m.pendingAiPromptSystem = systemPrompt
			m.executionSubmitted()
			return m, tea.Batch(m.bridge.SendCommandPayloadCmd(map[string]interface{}{"type": catalog.BridgeTypeValue(catalog.BridgeTypeSharedScopeSnapshotID)}), executionSpinnerDelayCmd())
		}
		return m, m.aiContext.BeginRequest(trimmed, systemPrompt)
	}
	m.executionSubmitted()
	return m, tea.Batch(m.bridge.SendDataCmd(trimmed), executionSpinnerDelayCmd())
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

	if strings.TrimSpace(language) == "" {
		language = m.scriptLanguageFromContext()
	}
	if strings.TrimSpace(language) == "" {
		m.addErrorMessage("Script save failed: language context is missing.")
		return m, nil
	}

	saveCmd := m.bridge.SendCommandPayloadCmd(map[string]interface{}{
		"type": catalog.BridgeTypeValue(catalog.BridgeTypeSaveScriptID),
		"name": name,
		"lang": language,
		"content": content,
	})
	if saveCmd == nil {
		m.addErrorMessage("Script save failed: bridge is unavailable.")
		return m, nil
	}

	m.addScriptSavedMessage(formatScriptConfirmation(name, language, content))
	m.mode = ModeREPL

	if selection == SaveAndExecute {
		runCmd := m.bridge.SendCommandPayloadCmd(map[string]interface{}{
			"type": catalog.BridgeTypeValue(catalog.BridgeTypeRunScriptID),
			"name": name,
			"lang": language,
		})
		if runCmd == nil {
			return m, saveCmd
		}
		return m, tea.Sequence(saveCmd, runCmd)
	}

	m.pendingScriptCode = ""
	m.pendingScriptName = ""
	m.pendingScriptLanguage = ""
	return m, saveCmd
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
		safeName, exists, err := sessionSnapshotExists(name)
		if err != nil {
			m.addErrorMessage("Session save failed: " + err.Error())
			return m, nil
		}
		if exists {
			m.pendingSessionOverwriteName = safeName
			m.sessionOverwriteVisible = true
			m.sessionOverwriteCursor = SessionOverwriteConfirm
			m.recalculateLayout()
			return m, nil
		}
		return m.requestSessionStateSave(safeName, false)
	}

	snapshot, err := loadSession(name)
	if err != nil {
		m.addErrorMessage("Session load failed: " + err.Error())
		return m, nil
	}
	if len(snapshot.EngineState) == 0 {
		m.addErrorMessage("Session load failed: snapshot is missing engine state.")
		return m, nil
	}

	return m.requestSessionStateLoad(snapshot)
}

func (m *AppModel) applySessionSnapshot(snapshot SessionSnapshot) {
	m.activeContext = "global"
	m.activeContextPath = "global"
	m.activeLanguage = ""

	m.messages = append([]ui.ChatMessage{}, snapshot.History...)

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

func contextSwitchCommandsForPath(path string) []string {
	target := normaliseContextName(path)
	if target == "" || target == "global" {
		return []string{"$global"}
	}
	segments := strings.Split(target, "::")
	commands := make([]string, 0, len(segments))
	for _, segment := range segments {
		segment = strings.TrimSpace(segment)
		if segment == "" || segment == "global" {
			continue
		}
		commands = append(commands, "$"+segment)
	}
	if len(commands) == 0 {
		return []string{"$global"}
	}
	return commands
}

func (m AppModel) requestSessionStateSave(name string, overwrite bool) (tea.Model, tea.Cmd) {
	if m.bridge == nil {
		m.addErrorMessage("Session save failed: bridge is unavailable.")
		return m, nil
	}
	m.pendingSessionBridgeOp = SessionBridgeOperation{
		Kind: SessionBridgeOperationSave,
		SnapshotName: strings.TrimSpace(name),
		Overwrite: overwrite,
	}
	m.executionSubmitted()
	return m, tea.Batch(
		m.bridge.SendCommandPayloadCmd(map[string]interface{}{"type": catalog.BridgeTypeValue(catalog.BridgeTypeSessionSaveStateID)}),
		executionSpinnerDelayCmd(),
	)
}

func (m AppModel) requestSessionStateLoad(snapshot SessionSnapshot) (tea.Model, tea.Cmd) {
	if m.bridge == nil {
		m.addErrorMessage("Session load failed: bridge is unavailable.")
		return m, nil
	}
	m.pendingSessionBridgeOp = SessionBridgeOperation{
		Kind: SessionBridgeOperationLoad,
		SnapshotName: strings.TrimSpace(snapshot.Name),
		Snapshot: snapshot,
	}
	m.executionSubmitted()
	return m, tea.Batch(
		m.bridge.SendCommandPayloadCmd(map[string]interface{}{
			"type":  catalog.BridgeTypeValue(catalog.BridgeTypeSessionLoadStateID),
			"state": snapshot.EngineState,
		}),
		executionSpinnerDelayCmd(),
	)
}

func (m AppModel) handleSessionStateResponse(msg bridge.SessionStateResponseMsg) (tea.Model, tea.Cmd) {
	m.stopExecutionSpinner()
	operation := m.pendingSessionBridgeOp
	m.pendingSessionBridgeOp = SessionBridgeOperation{}
	if operation.Kind == SessionBridgeOperationNone {
		if !msg.Ok {
			m.addErrorMessage("Session operation failed: " + strings.TrimSpace(msg.Error))
		}
		return m, nil
	}

	if !msg.Ok {
		switch operation.Kind {
		case SessionBridgeOperationSave:
			m.addErrorMessage("Session save failed: " + strings.TrimSpace(msg.Error))
		case SessionBridgeOperationLoad:
			m.addErrorMessage("Session load failed: " + strings.TrimSpace(msg.Error))
		}
		return m, nil
	}

	switch operation.Kind {
	case SessionBridgeOperationSave:
		if msg.Action != "session_save_state" {
			m.addErrorMessage("Session save failed: unexpected bridge response action.")
			return m, nil
		}
		if err := saveSession(m, operation.SnapshotName, operation.Overwrite, msg.State); err != nil {
			m.addErrorMessage("Session save failed: " + err.Error())
			return m, nil
		}
		if operation.Overwrite {
			m.addSystemMessage(fmt.Sprintf("Session %q overwritten successfully.", operation.SnapshotName))
		} else {
			m.addSystemMessage(fmt.Sprintf("Session %q saved successfully.", operation.SnapshotName))
		}
		return m, nil
	case SessionBridgeOperationLoad:
		if msg.Action != "session_load_state" {
			m.addErrorMessage("Session load failed: unexpected bridge response action.")
			return m, nil
		}
		snapshot := operation.Snapshot
		m.applySessionSnapshot(snapshot)
		m.addSystemMessage(fmt.Sprintf("Session %q loaded successfully.", snapshot.Name))
		targetPath := normaliseContextName(snapshot.ActiveContext)
		if targetPath == "" {
			targetPath = "global"
		}
		m.pendingLoadContextSync = targetPath
		m.pendingContextPath = targetPath
		commands := contextSwitchCommandsForPath(targetPath)
		cmds := make([]tea.Cmd, 0, len(commands)+1)
		for _, command := range commands {
			cmds = append(cmds, m.bridge.SendDataCmd(command))
		}
		cmds = append(cmds, executionSpinnerDelayCmd())
		m.executionSubmitted()
		return m, tea.Sequence(cmds...)
	default:
		return m, nil
	}
}

func slashCommandBase(input string) string {
	trimmed := strings.TrimSpace(input)
	if !strings.HasPrefix(trimmed, "/") {
		return ""
	}
	fields := strings.Fields(trimmed)
	if len(fields) == 0 {
		return ""
	}
	return strings.ToLower(fields[0])
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
	_ = command
	_ = prefix
	m.autocomplete.Dismiss()
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
	if shared := aicontext.ParseSharedScopeTable(content); shared != nil {
		m.aiSharedScope = shared
	}
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
	if strings.TrimSpace(m.pendingLoadContextSync) != "" && strings.Contains(content, "[ZERI][CONTEXT-002]") {
		m.flushEngineBatch(false)
		m.pendingLoadContextSync = ""
		m.pendingContextPath = ""
		m.autocomplete.ActiveContext = m.activeContext
		m.addErrorMessage("Session load failed: context synchronization requires the identity-switch no-op fix (F11-T01).")
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
		m.addErrorMessageWithTitle(builder.String(), title)
	} else {
		msg := ui.ChatMessage{
			Role: ui.RoleZeri,
			Title: title,
			Content: builder.String(),
			Timestamp: time.Now().Format("15:04"),
		}
		m.messages = append(m.messages, msg)
	}

	m.engineBatchChunks = nil
	m.engineBatchTitle = ""
	if !hasError {
		m.refreshViewport()
	}
}

func (m *AppModel) addErrorMessage(content string) {
	m.addErrorMessageWithTitle(content, m.currentMessageContextTitle())
}

func (m *AppModel) addErrorMessageWithTitle(content string, title string) {
	enriched := enrichReactiveErrorMessage(content, m.activeContext)
	msg := ui.ChatMessage{
		Role: ui.RoleError,
		Title: title,
		Content: ui.NormaliseContent(enriched),
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	if shouldPrefillTypedSetTemplate(enriched) && strings.TrimSpace(m.input.Value()) == "" {
		m.input.SetValue("/set myVar = 42 --number")
		m.input.SetHeight(1)
		m.updateAutocompleteForInput(m.input.Value())
		m.recalculateLayout()
	}
	m.refreshViewport()
}

func (m *AppModel) addScriptExecutionMessage(label string, content string) {
	cleaned := sanitizeScriptExecutionContent(content)
	msg := ui.ChatMessage{
		Role: ui.RoleScriptExecution,
		Label: strings.TrimSpace(label),
		Title: m.currentMessageContextTitle(),
		Content: ui.NormaliseContent(cleaned),
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func sanitizeScriptExecutionContent(content string) string {
	normalized := strings.ReplaceAll(content, "\r\n", "\n")
	normalized = strings.ReplaceAll(normalized, "\r", "\n")
	lines := strings.Split(normalized, "\n")
	filtered := make([]string, 0, len(lines))

	for _, raw := range lines {
		trimmed := strings.TrimSpace(raw)
		if trimmed == "" {
			filtered = append(filtered, raw)
			continue
		}
		if strings.EqualFold(trimmed, "(ok)") {
			continue
		}
		if strings.EqualFold(trimmed, "-- Editor active. /run to execute, /save to save, /cancel to exit.") {
			continue
		}
		filtered = append(filtered, raw)
	}

	result := strings.Join(filtered, "\n")
	result = strings.Trim(result, "\n")
	return result
}

func enrichReactiveErrorMessage(content string, activeContext string) string {
	base := strings.TrimSpace(content)
	if base == "" {
		return content
	}

	lower := strings.ToLower(base)
	hints := make([]string, 0, 2)

	if strings.Contains(lower, "[invalidvariabletype]") || strings.Contains(lower, "non-numeric variable") {
		hints = append(hints,
			"How to fix: in $math assign numeric variables directly (example: x = 5), then retry the expression (example: x + 6).",
		)
	}

	if strings.Contains(lower, "[script_not_found]") || strings.Contains(lower, "script not found") {
		hints = append(hints,
			"How to fix: run /list in the current language context, then use /edit \"name\" or /run \"name\" without file extension.",
		)
	}

	if strings.Contains(lower, "missing script name") {
		hints = append(hints,
			"How to fix: provide a quoted name (example: /new \"my-script\" or /edit \"my-script\").",
		)
	}

	if strings.Contains(lower, "context switch not allowed") {
		hints = append(hints,
			"How to fix: use /context to list reachable targets from your current context, then switch with $<context>.",
		)
	}

	if strings.Contains(lower, "unknown copy option") {
		hints = append(hints,
			"How to fix: use /copy last to copy the latest output or /copy all to copy the full transcript.",
		)
	}

	if strings.Contains(lower, "[settyperequired]") || strings.Contains(lower, "[settypeconflict]") {
		hints = append(hints,
			"How to fix: use exactly one type flag.",
			"Examples:",
			"/set x = 5 --number",
			"/set home = Milan --string",
			"/set featureEnabled = t --bool",
			"Template was copied to input: /set myVar = 42 --number",
		)
	}

	if strings.Contains(lower, "usage: /set") {
		hints = append(hints,
			"How to fix: use /set <key> [=] <value> with one type flag (--number, --string, --bool).",
		)
	}

	if strings.Contains(lower, "usage: /get") {
		hints = append(hints,
			"How to fix: provide a key name (example: /get myVar).",
		)
	}

	if len(hints) == 0 {
		contextLeaf := strings.TrimSpace(strings.ToLower(strings.TrimPrefix(activeContext, "zeri::")))
		if contextLeaf == "" {
			contextLeaf = "global"
		}
		hints = append(hints,
			"How to fix: run /help in $"+contextLeaf+" and retry with the exact command syntax shown there.",
		)
	}

	return base + "\n" + strings.Join(hints, "\n")
}

func shouldPrefillTypedSetTemplate(content string) bool {
	lower := strings.ToLower(content)
	return strings.Contains(lower, "[settyperequired]") || strings.Contains(lower, "[settypeconflict]")
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
		m.addErrorMessage(content)
	case PendingBridgeRequestEditLoad:
		m.addErrorMessage(content)
	case PendingBridgeRequestShowPreview:
		m.addErrorMessage(content)
	default:
		m.addErrorMessage(content)
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
	if catalog.IsLanguageContext(normaliseContextName(name)) {
		return "code", true
	}
	return "", false
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

func (m *AppModel) appendShowBlock(scriptName string, language string, content string) {
	ext := languageExtension(language)
	header := fmt.Sprintf("[%s] %s.%s", strings.ToUpper(language), scriptName, ext)
	numbered := addLineNumbers(content)
	body := header + "\n" + numbered
	msg := ui.ChatMessage{
		Role: ui.RoleCodeView,
		Label: "[show - \"" + scriptName + "\"]",
		Title: m.currentMessageContextTitle(),
		Content: body,
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func addLineNumbers(content string) string {
	lines := strings.Split(content, "\n")
	width := len(fmt.Sprintf("%d", len(lines)))
	var builder strings.Builder
	for i, line := range lines {
		fmt.Fprintf(&builder, "%*d  %s\n", width, i+1, line)
	}
	return strings.TrimRight(builder.String(), "\n")
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

	return catalog.IsLanguageContext(strings.ToLower(language))
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
	name := strings.Join(strings.Fields(remainder), " ")
	if name == "" {
		return "", false
	}
	return name, true
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

func availableScriptHubRuntimes() []string {
	manifest, err := loadRuntimeManifest()
	if err != nil {
		return catalog.LanguageFolders()
	}

	available := map[string]bool{}
	for _, result := range validateRequiredRuntimes(manifest) {
		if result.Status != RuntimeStatusOK {
			continue
		}
		for _, folder := range catalog.RuntimeLanguageFolders(result.Runtime.Name) {
			available[folder] = true
		}
	}

	runtimes := make([]string, 0, len(available))
	for runtime := range available {
		runtimes = append(runtimes, runtime)
	}
	return runtimes
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
	if strings.EqualFold(strings.TrimSpace(cmd), "/bug") {
		cmd = "/bug report"
	}

	if strings.EqualFold(strings.TrimSpace(cmd), "/back") {
		if m.aiModeActive {
			m.addUserMessage(cmd)
			m.exitAiContext()
			return m, nil
		}
		if previous, ok := previousContextPath(m.activeContextPath); ok {
			m.pendingContextPath = previous
			m.activeLanguage = m.resolveActiveLanguage(previous)
			m.autocomplete.ActiveContext = leafContextFromPath(previous)
		}
	}

	baseCmd := slashCommandBase(cmd)

	if m.aiModeActive {
		switch baseCmd {
		case "/set":
			m.addUserMessage(cmd)
			return m.handleAiSetCommand(cmd)
		case "/help":
			m.addUserMessage(cmd)
			m.addSystemMessage(m.aiHelpText())
			return m, nil
		case "/setup":
			m.addUserMessage(cmd)
			return m.handleAiSetupCommand()
		}
	}

	if baseCmd == "/errors" {
		m.addUserMessage(cmd)
		filter := strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(cmd), "/errors"))
		m.addSystemMessage(ui.RenderErrorCatalog(filter))
		return m, nil
	}

	scope := ui.ValidateSlashCommandForContext(m.activeContext, cmd)
	if !scope.Allowed {
		current := scope.CurrentGroup
		if strings.TrimSpace(current) == "" {
			current = "global"
		}
		allowed := ui.CommandScopeDescription(scope.AllowedGroups)
		m.addUserMessage(cmd)
		m.addErrorMessage(
			"[COMMAND_SCOPE] Command " + scope.Command + " is not available in $" + current + ".\n" +
				"Allowed contexts: " + allowed + "\n" +
				"How to fix: run /context, switch with $<context>, then retry " + scope.Command + ".",
		)
		return m, nil
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
	case cmd == settingsCommand || strings.HasPrefix(cmd, settingsCommand+" "):
		m.addUserMessage(cmd)
		remainder := strings.TrimSpace(strings.TrimPrefix(cmd, settingsCommand))
		if remainder == "" {
			m.settingsCenter = buildSettingsCenterState(m.settingsCenter)
			m.settingsVisible = true
			if m.bridge != nil {
				return m, m.requestSettingsSnapshot(SettingsRequestOpenModal)
			}
			return m, nil
		}
		if parent, ok := parseCommandArgument(remainder, settingsPathSubcommand); ok {
			return m.handleSettingsPathChange(parent)
		}
		if strings.EqualFold(remainder, settingsPathSubcommand) {
			return m.handleSettingsPathChange("")
		}
		if handledModel, cmdOut, handled := m.handleSettingsCommand(remainder); handled {
			return handledModel, cmdOut
		}
		m.addSystemMessage("Unknown /settings option. Usage: /settings | /settings path <parent-folder> | /settings ide <name> | /settings ai endpoint <url> | /settings ai model <name> | /settings ai key <key|clear>.")
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
	case strings.HasPrefix(cmd, deleteCommandPrefix):
		if !m.isCodeContextActive() {
			m.addUserMessage(cmd)
			return m, m.bridge.SendDataCmd(cmd)
		}
		if strings.Contains(strings.ToLower(cmd), "--hard") {
			m.addUserMessage(cmd)
			return m, m.bridge.SendDataCmd(cmd)
		}
		scriptName, ok := parseScriptNameArgument(cmd, deleteCommandPrefix)
		if !ok {
			m.addSystemMessage("Missing script name. Usage: /delete \"script-name\".")
			return m, nil
		}
		m.addUserMessage(cmd)
		language := m.scriptLanguageFromContext()
		m.pendingDeleteScriptName = scriptName
		m.pendingDeleteLanguage = language
		m.deleteConfirmVisible = true
		m.deleteConfirmCursor = DeleteConfirmCancel
		m.recalculateLayout()
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
		BinaryPath: enginePath,
		PipeName: pipeName,
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
		BaseDelay: engineConnectRetryInterval,
		MaxDelay: engineConnectRetryInterval,
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

func (m *AppModel) enterAiContext() (tea.Model, tea.Cmd) {
	if !m.aiModeActive {
		m.aiPreviousContext = m.activeContext
		m.aiPreviousContextPath = m.activeContextPath
		m.aiPreviousLanguage = m.activeLanguage
		m.aiActiveLanguageHint = m.scriptLanguageFromContext()
	}
	m.aiModeActive = true
	m.activeContext = "ai"
	m.activeContextPath = "ai"
	m.activeLanguage = ""
	m.autocomplete.ActiveContext = "ai"

	if !m.aiWelcomeShown {
		m.aiWelcomeShown = true
		if m.aiConfigured {
			m.addSystemMessage(m.aiWelcomeText())
		} else {
			m.addSystemMessage(m.aiOnboardingText())
		}
	}
	return *m, m.aiContext.StartConnectivityCheck()
}

func (m AppModel) aiStatusSummary() string {
	endpoint := m.aiContext.Endpoint()
	if endpoint == "" {
		endpoint = aicontext.DefaultEndpoint + " (default)"
	}
	model := m.aiContext.ModelName()
	if model == "" {
		model = aicontext.DefaultModel + " (default)"
	}
	apiKey := "not set (fine for local Ollama)"
	if m.aiContext.HasApiKey() {
		apiKey = "set"
	}
	connection := "not checked yet"
	switch {
	case m.aiContext.Checking():
		connection = "checking..."
	case m.aiContext.Connected():
		connection = "connected"
	case m.aiContext.LastError() != "":
		connection = "unreachable"
	}
	return "Current AI configuration:\n" +
		"endpoint: " + endpoint + "\n" +
		"model: " + model + "\n" +
		"API key: " + apiKey + "\n" +
		"connection: " + connection
}

func (m AppModel) aiWelcomeText() string {
	return "Welcome to $ai — the AI-assisted code generation context.\n\n" +
		m.aiStatusSummary() + "\n\n" +
		"Type a request in plain language to generate code, or run /help for the $ai commands.\n" +
		"Reconfigure anytime with /setup."
}

func (m AppModel) aiOnboardingText() string {
	return "Welcome to $ai. This context generates code with an AI model, but it isn't set up yet.\n\n" +
		"Pick one of the two paths:\n\n" +
		"A) Local model with Ollama (no API key, runs on your machine):\n" +
		"1. Install Ollama and run: ollama serve\n" +
		"2. Pull a model, e.g.: ollama pull codellama\n" +
		"3. Here, run: /set model codellama:latest\n" +
		"(Default endpoint " + aicontext.DefaultEndpoint + " is used automatically.)\n\n" +
		"B) Remote, OpenAI-compatible endpoint (needs an API key):\n" +
		"1. /set endpoint <url> (the base URL exposing)\n" +
		"2. /set apikey <key>\n" +
		"3. /set model <name>\n\n" +
		"Run /setup anytime to see this again with your current status, or /help for all $ai commands."
}

func (m AppModel) aiHelpText() string {
	return "$ai — AI-assisted code generation.\n\n" +
		m.aiStatusSummary() + "\n\n" +
		"Commands:\n" +
		"<plain text> — send a prompt and stream generated code\n" +
		"/setup — guided setup (endpoint, model, API key)\n" +
		"/set endpoint <url> — set the AI endpoint URL\n" +
		"/set model <name> — set the model name\n" +
		"/set apikey <key> — set the API key (/set apikey clear to remove)\n" +
		"/set system-prompt <text> — override the system prompt for this session\n" +
		"/errors [family] — show the error code catalog\n" +
		"/back — return to the previous context\n\n" +
		"Note: in $ai, /set configures the assistant — it does not store typed variables."
}

func (m *AppModel) handleAiSetupCommand() (tea.Model, tea.Cmd) {
	if m.aiConfigured {
		m.addSystemMessage(m.aiWelcomeText())
	} else {
		m.addSystemMessage(m.aiOnboardingText())
	}
	return *m, m.aiContext.StartConnectivityCheck()
}

func (m *AppModel) exitAiContext() {
	if !m.aiModeActive {
		return
	}
	m.aiModeActive = false
	m.aiContext.CancelStreaming()
	restoredPath := strings.TrimSpace(m.aiPreviousContextPath)
	if restoredPath == "" {
		restoredPath = "global"
	}
	m.activeContextPath = restoredPath
	restoredContext := strings.TrimSpace(m.aiPreviousContext)
	if restoredContext == "" {
		restoredContext = leafContextFromPath(restoredPath)
	}
	if restoredContext == "" {
		restoredContext = "global"
	}
	m.activeContext = restoredContext
	m.activeLanguage = strings.TrimSpace(m.aiPreviousLanguage)
	m.pendingContextPath = ""
	m.autocomplete.ActiveContext = m.activeContext
	m.aiContext.ClearActions()
	m.pendingAiPrompt = ""
	m.pendingAiPromptSystem = ""
}

func (m AppModel) aiPromptLanguage() string {
	if lang := strings.TrimSpace(m.aiActiveLanguageHint); lang != "" {
		return lang
	}
	if lang := strings.TrimSpace(m.aiPreviousLanguage); lang != "" {
		return lang
	}
	return "python"
}

func (m AppModel) aiActionLanguage() string {
	blocks := m.aiContext.CodeBlocks()
	if len(blocks) == 0 {
		return aicontext.NormalizeCodeLang("", m.aiPromptLanguage())
	}
	return aicontext.NormalizeCodeLang(blocks[0].Lang, m.aiPromptLanguage())
}

func (m AppModel) aiPrimaryCodeBlock() (aicontext.CodeBlock, bool) {
	blocks := m.aiContext.CodeBlocks()
	if len(blocks) == 0 {
		return aicontext.CodeBlock{}, false
	}
	return blocks[0], true
}

func (m AppModel) aiScriptName() string {
	return "ai-" + time.Now().Format("20060102-150405")
}

func (m *AppModel) handleAiSetCommand(cmd string) (tea.Model, tea.Cmd) {
	if m.bridge == nil {
		m.addErrorMessage("AI configuration is unavailable: engine bridge is not connected.")
		return *m, nil
	}

	trimmed := strings.TrimSpace(cmd)
	lower := strings.ToLower(trimmed)
	switch {
	case strings.HasPrefix(lower, "/set endpoint "):
		value := strings.TrimSpace(trimmed[len("/set endpoint "):])
		if value == "" {
			m.addErrorMessage("Usage: /set endpoint <url>")
			return *m, nil
		}
		return *m, m.requestSettingsUpdate(SettingsUpdateFieldAiEndpoint, SettingsUpdateOriginAiContext, map[string]interface{}{
			"type": catalog.BridgeTypeValue(catalog.BridgeTypeSettingsUpdateID),
			"ai_endpoint": value,
		})
	case strings.HasPrefix(lower, "/set model "):
		value := strings.TrimSpace(trimmed[len("/set model "):])
		if value == "" {
			m.addErrorMessage("Usage: /set model <name>")
			return *m, nil
		}
		return *m, m.requestSettingsUpdate(SettingsUpdateFieldAiModel, SettingsUpdateOriginAiContext, map[string]interface{}{
			"type": catalog.BridgeTypeValue(catalog.BridgeTypeSettingsUpdateID),
			"ai_model": value,
		})
	case strings.HasPrefix(lower, "/set apikey ") || strings.HasPrefix(lower, "/set api-key "):
		prefixLen := len("/set apikey ")
		if strings.HasPrefix(lower, "/set api-key ") {
			prefixLen = len("/set api-key ")
		}
		value := strings.TrimSpace(trimmed[prefixLen:])
		if value == "" {
			m.addErrorMessage("Usage: /set apikey <key>  (use /set apikey clear to remove it)")
			return *m, nil
		}
		if strings.EqualFold(value, "clear") || strings.EqualFold(value, "none") {
			value = ""
		}
		return *m, m.requestSettingsUpdate(SettingsUpdateFieldAiKey, SettingsUpdateOriginAiContext, map[string]interface{}{
			"type": catalog.BridgeTypeValue(catalog.BridgeTypeSettingsUpdateID),
			"ai_key": value,
		})
	case strings.HasPrefix(lower, "/set system-prompt "):
		value := strings.TrimSpace(trimmed[len("/set system-prompt "):])
		if value == "" {
			m.addErrorMessage("Usage: /set system-prompt <text>")
			return *m, nil
		}
		m.aiContext.SetSystemPromptOverride(value)
		m.addSystemMessage("AI system prompt override updated for this session.")
		return *m, nil
	default:
		m.addErrorMessage(
			"In $ai, /set configures the assistant — not typed variables.\n" +
				"Use one of:\n" +
				" /set endpoint <url> (example: /set endpoint http://localhost:11434)\n" +
				" /set model <name> (example: /set model codellama:latest)\n" +
				" /set apikey <key> (for remote endpoints; /set apikey clear to remove)\n" + " /set system-prompt <text> (override the system prompt for this session)\n" +
				"Or run /setup for a guided walkthrough.")
		return *m, nil
	}
}

func (m *AppModel) runAiCodeBlock() tea.Cmd {
	block, ok := m.aiPrimaryCodeBlock()
	if !ok {
		m.addErrorMessage("No AI code block available to run.")
		return nil
	}
	lang := aicontext.NormalizeCodeLang(block.Lang, m.aiPromptLanguage())
	name := m.aiScriptName()
	m.aiContext.ClearActions()
	saveCmd := m.bridge.SendCommandPayloadCmd(map[string]interface{}{
		"type": catalog.BridgeTypeValue(catalog.BridgeTypeSaveScriptID),
		"name": name,
		"lang": lang,
		"content": block.Content,
	})
	runCmd := m.bridge.SendCommandPayloadCmd(map[string]interface{}{
		"type": catalog.BridgeTypeValue(catalog.BridgeTypeRunScriptID),
		"name": name,
		"lang": lang,
	})
	m.addSystemMessage("Running AI-generated code in $" + lang + ".")
	if saveCmd == nil {
		return runCmd
	}
	if runCmd == nil {
		return saveCmd
	}
	return tea.Sequence(saveCmd, runCmd)
}

func (m *AppModel) editAiCodeBlock() {
	block, ok := m.aiPrimaryCodeBlock()
	if !ok {
		m.addErrorMessage("No AI code block available to edit.")
		return
	}
	lang := aicontext.NormalizeCodeLang(block.Lang, m.aiPromptLanguage())
	m.aiContext.ClearActions()
	m.openScriptEditor(lang, m.aiScriptName(), block.Content, ScriptEditorIntentNew)
}

func (m *AppModel) saveAiCodeBlock() tea.Cmd {
	block, ok := m.aiPrimaryCodeBlock()
	if !ok {
		m.addErrorMessage("No AI code block available to save.")
		return nil
	}
	lang := aicontext.NormalizeCodeLang(block.Lang, m.aiPromptLanguage())
	name := m.aiScriptName()
	m.aiContext.ClearActions()
	m.addSystemMessage("Saving AI-generated script as " + name + ".")
	return m.bridge.SendCommandPayloadCmd(map[string]interface{}{
		"type": catalog.BridgeTypeValue(catalog.BridgeTypeSaveScriptID),
		"name": name,
		"lang": lang,
		"content": block.Content,
	})
}

/*
 * What:
 *   - Added full script workflow orchestration in AppModel for code contexts:
 *     `/new`, `/edit`, `/show` now drive dedicated TUI behavior.
 *   - Added async bridge request correlation for script existence checks,
 *     script load for editing, and temporary preview fetch.
 *   - Added explicit save-and-run confirmation prompt after Alt+Enter in
 *     editor mode before dispatching engine commands.
 *   - Added "alt+shift+enter" to the run-trigger key case in
 *     updateScriptEditor, alongside the existing alt+enter/shift+enter variants,
 *     so the shortcut works consistently across terminals that remap the key.
 *   - Added explicit "tab" case in updateScriptEditor that routes
 *     the key through applyScriptEditorPaste("\t"), which the textarea sanitizer
 *     converts to 4 spaces at the current cursor position. Tab in the editor
 *     was previously a no-op because bubbletea v2 sets KeyPressMsg.Text to ""
 *     for special keys, causing the textarea default handler to insert nothing.
 *   - Shortcut policy aligned for terminal usage:
 *     - REPL mode: Ctrl+C now always performs immediate exit (tea.Quit).
 *     - REPL copy moved to Ctrl+Shift+C (Ctrl+Insert still supported).
 *     - Paste shortcut normalized to Ctrl+Shift+V (plus Shift+Insert/Cmd+V/Meta+V).
 *     - Script editor keeps Ctrl+C copy behavior and also accepts Ctrl+Shift+C.
 *   - Added reactive error enrichment:
 *     error messages now append context-aware "How to fix" guidance with concrete
 *     command examples for common failures (invalid math variable type, script not
 *     found, missing script name, invalid context switch, copy usage and set/get usage).
 *   - Pending bridge request failures now flow through addErrorMessage,
 *     so they are rendered consistently as errors and receive the same guidance.
 *   - Added strict command hierarchy enforcement at slash-command entry:
 *     out-of-scope commands are blocked before bridge dispatch, with explicit
 *     allowed-context guidance and retry steps.
 *   - `/show` now appends a static numbered code block to the chat stream
 *     (RoleCodeView message) instead of a floating overlay panel.
 *   - `/delete` intercept: shows inline confirmation menu (Yes/Cancel) before
 *     permanent deletion; skips menu and returns error if script does not exist.
 *   - `restart` bare command intercepted before bridge dispatch in all
 *     connection states (connected, disconnected, reconnecting).
 *   - parseScriptNameArgument fixed to capture the full remainder after the
 *     command keyword as the script name (multi-word names now work correctly).
 *   - DeleteConfirmOption enum and deleteConfirmVisible/pendingDeleteScriptName/
 *     pendingDeleteLanguage fields added to AppModel.
 *   - Non-fatal startup warnings (e.g. RUNTIME_OUTDATED) are now displayed as
 *     system messages in the chat stream after startup completes.
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
 *   - ui/cmd/zeri-tui/persistence.go provides deleteScript.
 *
 * First-run onboarding: if the $ai context has never been configured, guide
 * the user through setup instead of dropping them into a bare AI-001 error.
 * Otherwise show a one-time welcome with the current configuration.
 *
 */
