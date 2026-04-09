package main

import (
	"strings"
	"time"
	"yuumi/internal/bridge"
	"yuumi/internal/system"
	"yuumi/internal/ui"

	tea "charm.land/bubbletea/v2"
	lg "charm.land/lipgloss/v2"
	"charm.land/bubbles/v2/textarea"
	"charm.land/bubbles/v2/viewport"
   "github.com/atotto/clipboard"
)

type statusTickMsg struct{}

func tickStatusCmd() tea.Cmd {
	return tea.Tick(500*time.Millisecond, func(t time.Time) tea.Msg {
		return statusTickMsg{}
	})
}

type AppModel struct {
	width  int
	height int

	viewport viewport.Model
	input textarea.Model
	autocomplete ui.AutocompleteModel

	messages []ui.ChatMessage
	inputHistory []string
	historyIndex int
	draftBuffer string
	activeContext string
	isCommandMode bool
	pendingReset bool

	bridge bridge.YuumiClient
	ready bool
	bridgeConnected bool
	memoryMB uint64
	lastStatusTick time.Time
}

func newAppModel(b bridge.YuumiClient) AppModel {
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
	}
}

func (m AppModel) Init() tea.Cmd {
	return tea.Batch(
		m.bridge.ConnectCmd(),
		tickStatusCmd(),
		m.input.Focus(),
	)
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
	content := ui.RenderAllMessages(m.messages, m.width-4, m.activeContext)
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
	switch msg := msg.(type) {

	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		m.recalculateLayout()
		m.refreshViewport()
		return m, nil

	case tea.KeyPressMsg:
		return m.handleKeyPress(msg)

	case tea.MouseMsg:
		var cmd tea.Cmd
		m.viewport, cmd = m.viewport.Update(msg)
		return m, cmd

	case statusTickMsg:
		m.memoryMB = system.GetProcessMemoryMB()
		return m, tickStatusCmd()

	case bridge.ConnectedMsg:
		if !m.ready {
			m.bridgeConnected = true
			m.ready = true
			m.addSystemMessage("Work environment ready")
		}
		return m, nil

	case bridge.DisconnectedMsg:
		m.bridgeConnected = false
		m.addSystemMessage("Disconnected: " + msg.Reason)
		return m, nil

	case bridge.DataMsg:
		m.addZeriMessage(msg.Content)
		return m, nil

	case bridge.ErrorMsg:
		m.addZeriMessage("Error: " + msg.Content)
		return m, nil

	case bridge.ContextChangedMsg:
       if msg.Active {
			m.activeContext = msg.ContextName
		} else {
			m.activeContext = ""
		}
		m.autocomplete.ActiveContext = m.activeContext
		m.refreshViewport()
		return m, nil

	}

	return m, nil
}

func (m AppModel) View() tea.View {
	pad := lg.NewStyle().Padding(1, 2)

	header := ui.RenderHeader(m.width, m.height)
	chatArea := m.viewport.View()
	statusBar := ui.RenderStatusBar(m.width, m.activeContext, m.bridgeConnected, m.memoryMB)
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

func (m *AppModel) addUserMessage(content string) {
	normalised := ui.NormaliseContent(content)
	msg := ui.ChatMessage{
		Role: ui.RoleUser,
		Content: normalised,
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m *AppModel) addZeriMessage(content string) {
	normalised := ui.NormaliseContent(content)
	msg := ui.ChatMessage{
		Role: ui.RoleZeri,
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
		Content: normalised,
		Timestamp: time.Now().Format("15:04"),
	}
	m.messages = append(m.messages, msg)
	m.refreshViewport()
}

func (m AppModel) handleKeyPress(msg tea.KeyPressMsg) (tea.Model, tea.Cmd) {
	key := msg.String()

	if key == "ctrl+c" {
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

	if key == "ctrl+v" {
		pasted, err := clipboard.ReadAll()
		if err != nil {
			m.addSystemMessage("Paste failed: " + err.Error())
			return m, nil
		}

		if pasted != "" {
			m.input.SetValue(m.input.Value() + pasted)
		}
	}

	if key == "ctrl+x" {
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
		switch key {
		case "up":
			return m.handleHistoryUp()
		case "down":
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

func (m AppModel) handleSubmit() (tea.Model, tea.Cmd) {
	raw := m.input.Value()
	trimmed := strings.TrimSpace(raw)
	if trimmed == "" {
		return m, nil
	}

	m.input.Reset()
	m.input.SetHeight(1)
	m.autocomplete.Dismiss()
	m.recalculateLayout()

	if m.pendingReset {
		m.pendingReset = false
		lower := strings.ToLower(trimmed)
		if lower == "y" || lower == "yes" {
			m.messages = m.messages[:0]
			m.activeContext = "global"
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

	m.addUserMessage(trimmed)
	return m, m.bridge.SendDataCmd(trimmed)
}

func (m AppModel) handleSlashCommand(cmd string) (tea.Model, tea.Cmd) {
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
	case cmd == "/reset":
		m.pendingReset = true
		m.addSystemMessage("Reset will clear all variables and return to global context.\nType 'y' to confirm, anything else to cancel.")
		return m, nil
	default:
		m.addUserMessage(cmd)
		return m, m.bridge.SendDataCmd(cmd)
	}
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
 *   - ContextChangedMsg handling now follows generic context push/pop.
 *     When Active=true, activeContext becomes ContextName; when false,
 *     activeContext is cleared.
 *   - Autocomplete context binding now follows activeContext directly.
 *
 * Why:
 *   - Session 2 code-mode state has been removed; TUI now tracks only
 *     active logical context from engine notifications.
 *
 * Impact on other components:
 *   - ui/internal/bridge/client.go: ContextChangedMsg fields changed.
 *   - ui/internal/bridge/yuumi_client.go: emits unified context events.
 */
