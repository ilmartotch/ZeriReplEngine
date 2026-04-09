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

func RenderZeriMessage(msg ChatMessage, maxWidth int, activeContext string) string {
	labelText := "◆ Zeri"
	if activeContext != "" && activeContext != "global" {
		labelText = "◆ Zeri::" + activeContext
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

func RenderAllMessages(messages []ChatMessage, maxWidth int, activeContext string) string {
	if len(messages) == 0 {
		return ""
	}
	var parts []string
	for _, msg := range messages {
		switch msg.Role {
		case RoleZeri:
			parts = append(parts, RenderZeriMessage(msg, maxWidth, activeContext))
		case RoleUser:
			parts = append(parts, RenderUserMessage(msg, maxWidth))
		case RoleSystem:
			parts = append(parts, RenderSystemMessage(msg, maxWidth))
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
 *   - RenderZeriMessage now accepts activeContext parameter.
 *     Label shows "◆ Zeri::math" when context != "global".
 *   - RenderAllMessages accepts activeContext and forwards it to
 *     RenderZeriMessage for context-aware label rendering.
 *
 * Why:
 *   - When the user switches context ($math, $sandbox), the Zeri
 *     message label must reflect the active workspace so the user
 *     always knows which engine produced the output.
 *
 * Impact on other components:
 *   - model.go refreshViewport passes activeContext to RenderAllMessages.
 *   - palette.go ColourAcidGreen drives the label colour.
 */
