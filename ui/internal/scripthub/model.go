package scripthub

import (
	"embed"
	"fmt"
	"sort"
	"strings"
	"yuumi/internal/bridge"
	"yuumi/internal/ui"

	"charm.land/bubbles/v2/viewport"
	tea "charm.land/bubbletea/v2"
	lg "charm.land/lipgloss/v2"
)

//go:embed examples/*
var exampleFS embed.FS

type ScriptEntry struct {
	Name     string
	Lang     string
	Modified string
	Size     int
	Content  string
}

type CloseMsg struct{}

type OpenEditorMsg struct {
	Entry ScriptEntry
}

type ScriptHubModel struct {
	bridge          bridge.YuumiClient
	availableLangs  map[string]bool
	scripts         []ScriptEntry
	filtered        []ScriptEntry
	cursor          int
	searchQuery     string
	langFilter      string
	searchFocus     bool
	width           int
	height          int
	loading         bool
	err             error
	deleteConfirm   bool
	seededExamples  bool
	previewViewport viewport.Model
}

func New(client bridge.YuumiClient, availableRuntimes []string, width int, height int) ScriptHubModel {
	langs := make(map[string]bool, len(availableRuntimes))
	for _, runtime := range availableRuntimes {
		langs[strings.ToLower(strings.TrimSpace(runtime))] = true
	}
	vp := viewport.New()
	m := ScriptHubModel{
		bridge:          client,
		availableLangs:  langs,
		langFilter:      "all",
		width:           width,
		height:          height,
		loading:         true,
		previewViewport: vp,
	}
	m.recalculateLayout()
	return m
}

func (m ScriptHubModel) Init() tea.Cmd {
	if m.bridge == nil {
		return nil
	}
	return m.bridge.SendCommandPayloadCmd(map[string]interface{}{
		"type": "list_scripts_with_content",
	})
}

func (m *ScriptHubModel) SetWidth(width int) {
	m.width = width
	m.recalculateLayout()
}

func (m *ScriptHubModel) SetHeight(height int) {
	m.height = height
	m.recalculateLayout()
}

func (m *ScriptHubModel) recalculateLayout() {
	contentWidth := m.width - 6
	if contentWidth < 20 {
		contentWidth = 20
	}
	bodyHeight := m.height - 8
	if bodyHeight < 6 {
		bodyHeight = 6
	}
	rightWidth := contentWidth - ((contentWidth * 35) / 100)
	if rightWidth < 12 {
		rightWidth = 12
	}
	m.previewViewport.SetWidth(rightWidth - 2)
	m.previewViewport.SetHeight(bodyHeight - 2)
}

func (m ScriptHubModel) View() string {
	title := lg.NewStyle().Foreground(ui.ColourVolt).Bold(true).Render("Script Hub")
	searchLabel := "/"
	if m.searchFocus {
		searchLabel = ">"
	}
	search := fmt.Sprintf("%s %s", searchLabel, m.searchQuery)
	searchBar := lg.NewStyle().Foreground(ui.ColourWhite).Render(search)

	filters := []string{"all", "py", "lua", "js", "ts", "ruby"}
	filterParts := make([]string, 0, len(filters))
	for _, filter := range filters {
		style := lg.NewStyle().Foreground(ui.ColourIndustrialGrey)
		if m.langFilter == filter {
			style = lg.NewStyle().Foreground(ui.ColourVolt).Bold(true)
		}
		filterParts = append(filterParts, style.Render("["+filter+"]"))
	}
	top := lg.JoinHorizontal(lg.Left, title, "  ", searchBar, "  ", strings.Join(filterParts, " "))

	leftWidth := (m.width - 6) * 35 / 100
	if leftWidth < 18 {
		leftWidth = 18
	}
	rightWidth := (m.width - 6) - leftWidth
	if rightWidth < 18 {
		rightWidth = 18
	}

	listRows := make([]string, 0, len(m.filtered))
	for idx, script := range m.filtered {
		prefix := "  "
		if idx == m.cursor {
			prefix = "> "
		}
		row := fmt.Sprintf("%s[%s] %s  %s", prefix, strings.ToUpper(script.Lang), script.Name, script.Modified)
		listRows = append(listRows, row)
	}
	if len(listRows) == 0 {
		listRows = append(listRows, "No scripts")
	}
	leftPanel := lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ui.ColourElectricBlue).
		Width(leftWidth).
		Render(strings.Join(listRows, "\n"))

	rightPanel := lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ui.ColourElectricBlue).
		Width(rightWidth).
		Render(m.previewViewport.View())

	body := lg.JoinHorizontal(lg.Top, leftPanel, rightPanel)

	hints := "[Enter] Run  [E] Edit  [D] Delete  [/] Search  [Q] Close"
	if m.deleteConfirm && m.selected().Name != "" {
		hints = fmt.Sprintf("Delete \"%s\"? [Y/N]", m.selected().Name)
	}
	bottom := lg.NewStyle().Foreground(ui.ColourIndustrialGrey).Render(hints)

	panel := lg.JoinVertical(lg.Left, top, body, bottom)
	return lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ui.ColourElectricBlue).
		Padding(1, 2).
		Width(m.width - 2).
		Render(panel)
}

func (m *ScriptHubModel) selected() ScriptEntry {
	if len(m.filtered) == 0 || m.cursor < 0 || m.cursor >= len(m.filtered) {
		return ScriptEntry{}
	}
	return m.filtered[m.cursor]
}

func (m *ScriptHubModel) refreshPreview() {
	selected := m.selected()
	m.previewViewport.SetContent(selected.Content)
	m.previewViewport.GotoTop()
}

func (m *ScriptHubModel) filterScripts() {
	query := strings.ToLower(strings.TrimSpace(m.searchQuery))
	filter := strings.ToLower(strings.TrimSpace(m.langFilter))
	type rankedScript struct {
		entry ScriptEntry
		rank  int
	}
	ranked := make([]rankedScript, 0, len(m.scripts))
	for _, entry := range m.scripts {
		entryLang := strings.ToLower(entry.Lang)
		if filter != "" && filter != "all" && entryLang != filter {
			continue
		}
		if query == "" {
			ranked = append(ranked, rankedScript{entry: entry, rank: 0})
			continue
		}

		nameMatch := strings.Contains(strings.ToLower(entry.Name), query)
		langMatch := !nameMatch && strings.Contains(entryLang, query)
		contentMatch := !nameMatch && !langMatch && strings.Contains(strings.ToLower(entry.Content), query)
		if !nameMatch && !langMatch && !contentMatch {
			continue
		}
		rank := 100
		if nameMatch {
			rank = 300
		} else if langMatch {
			rank = 200
		}
		ranked = append(ranked, rankedScript{entry: entry, rank: rank})
	}
	if query != "" {
		sort.SliceStable(ranked, func(i int, j int) bool {
			if ranked[i].rank != ranked[j].rank {
				return ranked[i].rank > ranked[j].rank
			}
			if ranked[i].entry.Modified != ranked[j].entry.Modified {
				return ranked[i].entry.Modified > ranked[j].entry.Modified
			}
			if ranked[i].entry.Name != ranked[j].entry.Name {
				return ranked[i].entry.Name < ranked[j].entry.Name
			}
			return ranked[i].entry.Lang < ranked[j].entry.Lang
		})
	}
	filtered := make([]ScriptEntry, 0, len(ranked))
	for _, item := range ranked {
		filtered = append(filtered, item.entry)
	}
	m.filtered = filtered
	if m.cursor < 0 {
		m.cursor = 0
	}
	if m.cursor >= len(m.filtered) {
		m.cursor = 0
	}
	m.refreshPreview()
}

func (m ScriptHubModel) Update(msg tea.Msg) (ScriptHubModel, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.SetWidth(msg.Width)
		m.SetHeight(msg.Height)
		return m, nil

	case bridge.ScriptListResponseMsg:
		m.loading = false
		m.scripts = make([]ScriptEntry, 0, len(msg.Scripts))
		for _, item := range msg.Scripts {
			m.scripts = append(m.scripts, ScriptEntry{
				Name:     item.Name,
				Lang:     item.Lang,
				Modified: item.Modified,
				Size:     item.Size,
				Content:  item.Content,
			})
		}
		if len(m.scripts) == 0 && !m.seededExamples {
			m.seededExamples = true
			return m, tea.Sequence(m.seedExamplesCmd(), m.bridge.SendCommandPayloadCmd(map[string]interface{}{
				"type": "list_scripts_with_content",
			}))
		}
		m.filterScripts()
		return m, nil

	case bridge.ScriptActionResponseMsg:
		if msg.Ok && (msg.Action == "save_script" || msg.Action == "delete_script") {
			if m.bridge != nil {
				return m, m.bridge.SendCommandPayloadCmd(map[string]interface{}{
					"type": "list_scripts_with_content",
				})
			}
		}
		if !msg.Ok {
			m.err = fmt.Errorf("%s", msg.Error)
		}
		return m, nil

	case tea.KeyPressMsg:
		key := strings.ToLower(strings.TrimSpace(msg.String()))
		key = strings.ReplaceAll(key, " ", "")
		if m.deleteConfirm {
			if key == "y" {
				selected := m.selected()
				m.deleteConfirm = false
				if selected.Name == "" || selected.Lang == "" || m.bridge == nil {
					return m, nil
				}
				return m, m.bridge.SendCommandPayloadCmd(map[string]interface{}{
					"type": "delete_script",
					"name": selected.Name,
					"lang": selected.Lang,
				})
			}
			if key == "n" || key == "esc" || key == "escape" {
				m.deleteConfirm = false
				return m, nil
			}
			return m, nil
		}

		switch key {
		case "q", "esc", "escape":
			return m, func() tea.Msg { return CloseMsg{} }
		case "/":
			m.searchFocus = true
			m.searchQuery = ""
			m.filterScripts()
			return m, nil
		case "tab":
			switch m.langFilter {
			case "all":
				m.langFilter = "py"
			case "py":
				m.langFilter = "lua"
			case "lua":
				m.langFilter = "js"
			case "js":
				m.langFilter = "ts"
			case "ts":
				m.langFilter = "ruby"
			default:
				m.langFilter = "all"
			}
			m.filterScripts()
			return m, nil
		}

		if m.searchFocus {
			if key == "esc" || key == "escape" {
				m.searchFocus = false
				return m, nil
			}
			if key == "backspace" {
				if len(m.searchQuery) > 0 {
					m.searchQuery = m.searchQuery[:len(m.searchQuery)-1]
				}
				m.filterScripts()
				return m, nil
			}
			if len(msg.Text) > 0 {
				m.searchQuery += msg.Text
				m.filterScripts()
				return m, nil
			}
			return m, nil
		}

		switch key {
		case "up":
			if m.cursor > 0 {
				m.cursor--
			}
			m.refreshPreview()
			return m, nil
		case "down":
			if m.cursor < len(m.filtered)-1 {
				m.cursor++
			}
			m.refreshPreview()
			return m, nil
		case "enter":
			selected := m.selected()
			if selected.Name == "" || m.bridge == nil {
				return m, nil
			}
			return m, m.bridge.SendCommandPayloadCmd(map[string]interface{}{
				"type": "run_script",
				"name": selected.Name,
				"lang": selected.Lang,
			})
		case "e":
			selected := m.selected()
			if selected.Name == "" {
				return m, nil
			}
			return m, func() tea.Msg { return OpenEditorMsg{Entry: selected} }
		case "d":
			if m.selected().Name != "" {
				m.deleteConfirm = true
			}
			return m, nil
		}
	}

	var cmd tea.Cmd
	m.previewViewport, cmd = m.previewViewport.Update(msg)
	return m, cmd
}

func (m ScriptHubModel) seedExamplesCmd() tea.Cmd {
	if m.bridge == nil {
		return nil
	}
	commands := make([]tea.Cmd, 0)
	add := func(lang string, file string) {
		if !m.availableLangs[lang] {
			return
		}
		contentBytes, err := exampleFS.ReadFile("examples/" + file)
		if err != nil {
			return
		}
		commands = append(commands, m.bridge.SendCommandPayloadCmd(map[string]interface{}{
			"type":    "save_script",
			"name":    "example-" + lang,
			"lang":    lang,
			"content": string(contentBytes),
		}))
	}
	add("py", "example_python.py")
	add("lua", "example_lua.lua")
	add("js", "example_js.js")
	add("ruby", "example_ruby.rb")
	if len(commands) == 0 {
		return nil
	}
	return tea.Sequence(commands...)
}
