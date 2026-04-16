package ui

import (
	"strings"

	lg "charm.land/lipgloss/v2"
)

func wordwrap(s string, limit int) string {
	if limit <= 0 {
		return s
	}
	var result strings.Builder
	for _, line := range strings.Split(s, "\n") {
		if result.Len() > 0 {
			result.WriteRune('\n')
		}
		col := 0
		words := strings.Fields(line)
		for i, w := range words {
			wLen := len([]rune(w))
			if col+wLen > limit && col > 0 {
				result.WriteRune('\n')
				col = 0
			}
			if col > 0 && i > 0 {
				result.WriteRune(' ')
				col++
			}
			result.WriteString(w)
			col += wLen
		}
	}
	return result.String()
}

func RenderCodeViewHistoryMessage(msg ChatMessage, maxWidth int) string {
	contentWidth := maxWidth - 10
	if contentWidth < 10 {
		contentWidth = 10
	}

	label := msg.Label
	if strings.TrimSpace(label) == "" {
		label = "[code view]"
	}

	body := lg.NewStyle().
		Foreground(ColourIndustrialGrey).
		Render(wordwrap(msg.Content, contentWidth))

	return lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ColourDarkViolet).
		Padding(0, 1).
		Render(lg.JoinVertical(lg.Left, lg.NewStyle().Foreground(ColourVolt).Bold(true).Render(label), body))
}

func RenderZeriMessage(msg ChatMessage, maxWidth int) string {
	labelText := "◆ Zeri"
   if msg.Title != "" && msg.Title != "global" {
		labelText = "◆ Zeri::" + msg.Title
	}
	label := lg.NewStyle().Foreground(ColourAcidGreen).Render(labelText)
	ts := lg.NewStyle().Foreground(ColourIndustrialGrey).Render(msg.Timestamp)

	contentWidth := maxWidth - 4
	if contentWidth < 10 {
		contentWidth = 10
	}

	content := lg.NewStyle().
		Foreground(ColourWhite).
		Render(wordwrap(msg.Content, contentWidth))

	return lg.JoinVertical(lg.Left,
		label+" "+ts,
		content,
	)
}

func RenderUserMessage(msg ChatMessage, maxWidth int) string {
	ts := lg.NewStyle().Foreground(ColourIndustrialGrey).Render(msg.Timestamp)

	contentWidth := maxWidth - 4
	if contentWidth < 10 {
		contentWidth = 10
	}

	content := lg.NewStyle().
		Foreground(ColourIndustrialGrey).
		Render(wordwrap(msg.Content, contentWidth))

	return lg.JoinVertical(lg.Left,
		ts,
		content,
	)
}

func RenderSystemMessage(msg ChatMessage, termWidth int) string {
	line := "── " + msg.Content + " ──"
	return lg.NewStyle().
		Foreground(ColourIndustrialGrey).
		Width(termWidth - 4).
		Align(lg.Left).
		Render(line)
}

func RenderScriptExecutionMessage(msg ChatMessage, maxWidth int) string {
	contentWidth := maxWidth - 10
	if contentWidth < 10 {
		contentWidth = 10
	}

	label := msg.Label
	if strings.TrimSpace(label) == "" {
		label = "[$script]"
	}

	output := msg.Content
	if strings.TrimSpace(output) == "" {
		output = "(no output)"
	}

	header := lg.NewStyle().
		Foreground(ColourWhite).
		Background(ColourElectricBlue).
		Bold(true).
		Padding(0, 1).
		Render(label)

	body := lg.NewStyle().
		Foreground(ColourWhite).
		Render(wordwrap(output, contentWidth))

	ts := lg.NewStyle().Foreground(ColourIndustrialGrey).Render(msg.Timestamp)

	panel := lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ColourElectricBlue).
		Padding(0, 1).
		Render(lg.JoinVertical(lg.Left, header+" "+ts, body))

	return panel
}

func RenderAllMessages(messages []ChatMessage, maxWidth int) string {
	if len(messages) == 0 {
		return ""
	}
	var parts []string
	for _, msg := range messages {
		switch msg.Role {
		case RoleZeri:
          parts = append(parts, RenderZeriMessage(msg, maxWidth))
		case RoleUser:
			parts = append(parts, RenderUserMessage(msg, maxWidth))
		case RoleSystem:
			parts = append(parts, RenderSystemMessage(msg, maxWidth))
		case RoleScriptExecution:
			parts = append(parts, RenderScriptExecutionMessage(msg, maxWidth))
		case RoleCodeView:
			parts = append(parts, RenderCodeViewHistoryMessage(msg, maxWidth))
		}
	}
	return strings.Join(parts, "\n\n")
}

/*
 * CHANGES & RATIONALE
 * -------------------
 * [messages.go]
 *
 * What changed:
 *   - RenderZeriMessage now renders the header from msg.Title snapshot.
 *     Label shows "◆ Zeri::math" when msg.Title != "global".
 *   - RenderAllMessages no longer accepts activeContext and renders each
 *     history entry using only data stored inside that entry.
 *   - Added RenderScriptExecutionMessage for dedicated script execution
 *     history blocks with prominent labels and bordered output content.
 *   - Added RenderCodeViewHistoryMessage for persistent markers generated
 *     when temporary code preview panels are closed in REPL mode.
 *   - RenderAllMessages now handles RoleScriptExecution separately from
 *     normal RoleZeri output to keep script runs visually distinct.
 *   - RenderAllMessages now handles RoleCodeView to display code-view
 *     lifecycle markers in dedicated styled blocks.
 *
 * Why:
 *   - When the user switches context ($math, $sandbox), the Zeri
 *     message label must reflect the active workspace so the user
 *     always knows which engine produced the output.
 *
 * Impact on other components:
 *   - model.go refreshViewport now calls RenderAllMessages without a
 *     current-context argument.
 *   - palette.go ColourAcidGreen drives the label colour.
 */
