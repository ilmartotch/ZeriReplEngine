package ui

import (
	"fmt"
   "runtime"
	"strings"

	lg "charm.land/lipgloss/v2"
)

func RenderStatusBar(termWidth int, context string, connected bool, memMB uint64) string {
	volt := lg.NewStyle().Foreground(ColourVolt)
	grey := lg.NewStyle().Foreground(ColourIndustrialGrey)
	sep := grey.Render(" │ ")

	var parts []string
	parts = append(parts, volt.Render("◆ Zeri"))

 if memMB == 0 {
		parts = append(parts, grey.Render("RAM: —"))
	} else {
		parts = append(parts, grey.Render(fmt.Sprintf("RAM: %d MB", memMB)))
	}

	if context != "" && context != "global" {
		ctxDisplay := strings.ToUpper(context)
		ctxTag := lg.NewStyle().
			Foreground(ColourWhite).
			Background(ColourElectricBlue).
			Padding(0, 1).
			Render(ctxDisplay)
		parts = append(parts, ctxTag)
	}

	if connected {
		connStr := lg.NewStyle().Foreground(ColourAcidGreen).Render("●") +
			grey.Render(" connected")
		parts = append(parts, connStr)
	} else {
		connStr := lg.NewStyle().Foreground(ColourErrorRed).Render("✕") +
			grey.Render(" disconnected")
		parts = append(parts, connStr)
	}

  parts = append(parts, grey.Render(platformShortcutHint()))

	content := " " + strings.Join(parts, sep) + " "

	bar := lg.NewStyle().
		Width(termWidth).
		Render(content)

	return bar
}

func platformShortcutHint() string {
	switch runtime.GOOS {
	case "linux":
		return "Ctrl+C copy • Ctrl+X cut • Ctrl+V paste • /exit to quit (wl-clipboard/xclip/xsel required)"
	case "darwin":
		return "Ctrl+C copy • Ctrl+X cut • Ctrl+V paste • /exit to quit"
	default:
		return "Ctrl+C copy • Ctrl+X cut • Ctrl+V paste • /exit to quit"
	}
}

/*
 * What:
 *   - Parameter renamed from `lang` to `context` to reflect the new
 *     context-driven architecture. The status bar now displays the
 *     active context name (e.g. "MATH", "SANDBOX") instead of a
 *     language tag.
 *   - Context tag is hidden when the active context is "global" or
 *     empty, keeping the bar clean at root level.
 *   - Removed all references to /lang and language-based sandbox.
 *
 * Why:
 *   - The project replaced language-based sandboxes (/lang js|py|rb)
 *     with context-based environments ($math, $sandbox, $setup, $global).
 *     The status bar must reflect the active context, not a language.
 *
 * Impact on other components:
 *   - model.go passes `activeContext` (default "global") to this function.
 *   - ContextChangedMsg from the engine updates activeContext on switch.
 *
 * Future maintenance notes:
 *   - To re-add a background, set Background() on the bar style.
 *   - To add more indicators, append to the parts slice before the hint.
 */
