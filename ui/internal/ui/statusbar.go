package ui

import (
	"fmt"
	"strings"

	lg "charm.land/lipgloss/v2"
)

func RenderStatusBar(termWidth int, context string, connected bool, memMB uint64, sandboxRunning bool, activity string) string {
	_ = sandboxRunning
	volt := lg.NewStyle().Foreground(ColourVolt)
	grey := lg.NewStyle().Foreground(ColourIndustrialGrey)
	sep := grey.Render(" │ ")
	normalizedContext := strings.ToLower(strings.TrimSpace(context))

	var parts []string
	parts = append(parts, volt.Render("◆ Zeri"))

	if memMB == 0 {
		parts = append(parts, grey.Render("RAM: —"))
	} else {
		parts = append(parts, grey.Render(fmt.Sprintf("RAM: %d MB", memMB)))
	}

	if context != "" && context != "global" && normalizedContext != "sandbox" && !strings.HasPrefix(normalizedContext, "sandbox::") {
		ctxDisplay := contextLabel(context)
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

	if strings.TrimSpace(activity) != "" {
		parts = append(parts, grey.Render(strings.TrimSpace(activity)))
	}

	parts = append(parts, grey.Render("Use /help for commands and shortcuts"))

	content := " " + strings.Join(parts, sep) + " "

	bar := lg.NewStyle().
		Width(termWidth).
		Render(content)

	return bar
}

func RenderOnboardingStatusBar(termWidth int, legend string) string {
	volt := lg.NewStyle().Foreground(ColourVolt)
	grey := lg.NewStyle().Foreground(ColourIndustrialGrey)
	sep := grey.Render(" │ ")

	parts := []string{volt.Render("◆ Zeri"), grey.Render("Onboarding")}
	if trimmed := strings.TrimSpace(legend); trimmed != "" {
		parts = append(parts, grey.Render(trimmed))
	}

	content := " " + strings.Join(parts, sep) + " "

	return lg.NewStyle().
		Width(termWidth).
		Render(content)
}

func contextLabel(context string) string {
	normalized := strings.ToLower(strings.TrimSpace(context))
	if normalized == "" {
		return ""
	}
	if strings.HasPrefix(normalized, "zeri::") {
		return normalized
	}
	return "zeri::" + normalized
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
 *   - RenderOnboardingStatusBar renders a context-specific legend during the
 *     onboarding flow; it reuses the same volt/grey styling but replaces the
 *     /help hint with the step-aware key legend supplied by the caller.
 */
