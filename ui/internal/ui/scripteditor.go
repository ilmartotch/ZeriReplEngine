package ui

import (
	"fmt"
	"strings"

	"charm.land/bubbles/v2/textarea"
	tea "charm.land/bubbletea/v2"
	lg "charm.land/lipgloss/v2"
)

const (
	headerHeight          = 1
	statusBarHeight       = 1
	minEditorBodyHeight   = 3
	headerHorizontalPad   = 1
	statusHorizontalPad   = 1
	defaultScriptFileName = "untitled"
)

// ScriptEditor holds the state for the full-screen code editing mode.
type ScriptEditor struct {
	textarea textarea.Model
	language string
	filename string
	width    int
	height   int
}

func NewScriptEditorWithContent(language string, width, height int, filename string, content string) ScriptEditor {
	e := NewScriptEditor(language, width, height)
	e.filename = strings.TrimSpace(filename)
	e.SetValue(content)
	return e
}

// NewScriptEditor initialises a ScriptEditor for the given language tag.
func NewScriptEditor(language string, width, height int) ScriptEditor {
	ta := textarea.New()
	ta.ShowLineNumbers = true
	ta.Focus()

	e := ScriptEditor{
		textarea: ta,
		language: language,
		width:    width,
		height:   height,
	}
	e.applySize()
	return e
}

func (e ScriptEditor) Update(msg tea.Msg) (ScriptEditor, tea.Cmd) {
	var cmd tea.Cmd
	e.textarea, cmd = e.textarea.Update(msg)
	return e, cmd
}

func (e ScriptEditor) View() string {
	header := e.renderHeader()
	body := e.textarea.View()
	status := e.renderStatusBar()
	return lg.JoinVertical(lg.Left, header, body, status)
}

func (e *ScriptEditor) SetSize(width, height int) {
	e.width = width
	e.height = height
	e.applySize()
}

func (e ScriptEditor) Value() string {
	return e.textarea.Value()
}

func (e ScriptEditor) Language() string {
	return strings.TrimSpace(e.language)
}

func (e ScriptEditor) Filename() string {
	return strings.TrimSpace(e.filename)
}

func (e *ScriptEditor) SetFilename(filename string) {
	e.filename = strings.TrimSpace(filename)
}

func (e *ScriptEditor) SetValue(content string) {
	e.textarea.SetValue(content)
}

func (e *ScriptEditor) applySize() {
	bodyHeight := e.height - headerHeight - statusBarHeight
	if bodyHeight < minEditorBodyHeight {
		bodyHeight = minEditorBodyHeight
	}
	if e.width < 1 {
		e.width = 1
	}
	e.textarea.SetWidth(e.width)
	e.textarea.SetHeight(bodyHeight)
}

func (e ScriptEditor) renderHeader() string {
	tag := "$" + strings.TrimPrefix(strings.TrimSpace(e.language), "$")
	if strings.TrimSpace(tag) == "$" {
		tag = "$script"
	}

	filename := e.filename
	if strings.TrimSpace(filename) == "" {
		filename = defaultScriptFileName
	}

	headerText := fmt.Sprintf("SCRIPT EDITOR | %s | %s | ESC cancel", tag, filename)
	return lg.NewStyle().
		Foreground(ColourDarkViolet).
		Background(ColourElectricBlue).
		Padding(0, headerHorizontalPad).
		Width(e.width).
		Render(headerText)
}

func (e ScriptEditor) renderStatusBar() string {
	line := 1
	column := 1
	value := e.textarea.Value()
	if value != "" {
		lines := strings.Split(value, "\n")
		line = len(lines)
		column = len([]rune(lines[len(lines)-1])) + 1
	}

	statusText := fmt.Sprintf("Alt+Enter run | ESC cancel | Ln %d, Col %d", line, column)
	return lg.NewStyle().
		Foreground(ColourWhite).
		Background(ColourDarkViolet).
		Padding(0, statusHorizontalPad).
		Width(e.width).
		Render(statusText)
}

/*
 * What changed:
 *   - Added ScriptEditor component for full-screen script editing mode.
 *   - Added explicit layout constants for header and status chrome, avoiding
 *     inline magic numbers.
 *   - Implemented NewScriptEditor, Update, View, SetSize, Value, Language,
 *     Filename methods.
 *   - Added NewScriptEditorWithContent, SetFilename and SetValue helpers to
 *     support workflow-driven named editor sessions and preloaded script text.
 *   - Implemented contextual header and status bar rendering using palette.go
 *     colour tokens only.
 *
 * Why:
 *   - The top-level TUI model needs a dedicated modal editor state to support
 *     multi-line script authoring and execution without altering REPL flow.
 *   - Isolating editor behavior in a sub-model keeps AppModel modular.
 *
 * Impact on other components:
 *   - cmd/zeri-tui/model.go can switch to ModeScriptEditor and delegate update
 *     and view rendering to this component.
 */
