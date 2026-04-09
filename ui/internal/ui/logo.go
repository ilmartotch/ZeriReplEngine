package ui

import (
	"fmt"
	"image/color"
	"strings"

	lg "charm.land/lipgloss/v2"
)

var logoLines = []string{
	`███████╗███████╗██████╗ ██╗`,
	`╚══███╔╝██╔════╝██╔══██╗██║`,
	`  ███╔╝ █████╗  ██████╔╝██║`,
	` ███╔╝  ██╔══╝  ██╔══██╗██║`,
	`███████╗███████╗██║  ██║██║`,
	`╚══════╝╚══════╝╚═╝  ╚═╝╚═╝`,
}

func interpolateColour(c, total int) color.Color {
	if total <= 1 {
		return ColourVolt
	}
	t := float64(c) / float64(total-1)
	r := uint8(float64(217) + t*float64(163-217))
	g := uint8(float64(217) + t*float64(230-217))
	b := uint8(float64(35) + t*float64(53-35))
	return lg.Color(fmt.Sprintf("#%02X%02X%02X", r, g, b))
}

func RenderLogo() string {
	var rendered []string
	for _, line := range logoLines {
		runes := []rune(line)
		var row strings.Builder
		for i, ch := range runes {
			if ch == ' ' {
				row.WriteRune(' ')
				continue
			}
			col := interpolateColour(i, len(runes))
			row.WriteString(lg.NewStyle().Foreground(col).Render(string(ch)))
		}
		rendered = append(rendered, row.String())
	}
	return strings.Join(rendered, "\n")
}

func RenderHeader(termWidth, termHeight int) string {
	subtitle := lg.NewStyle().
		Foreground(ColourIndustrialGrey).
		Render("v0.1.0-alpha  ·  modular REPL environment")

	if termHeight < 15 {
		return lg.NewStyle().
			Width(termWidth).
			Align(lg.Left).
			Render(subtitle)
	}

	logo := RenderLogo()

	alignedLogo := lg.NewStyle().
		Width(termWidth).
		Align(lg.Left).
		Render(logo)

	alignedSub := lg.NewStyle().
		Width(termWidth).
		Align(lg.Left).
		Render(subtitle)

	return alignedLogo + "\n" + alignedSub + "\n"
}

/*
 * CHANGES & RATIONALE
 * -------------------
 * [logo.go]
 *
 * What changed:
 *   - Renders the exact ZERI ASCII-art glyph (6 rows, 27 columns).
 *   - Per-column TrueColor gradient from ColourVolt (#D9D923) to
 *     ColourAcidGreen (#A3E635) via linear RGB interpolation.
 *   - RenderHeader composes logo + subtitle left-aligned.
 *   - Gracefully degrades: if terminal height < 15, shows only subtitle.
 *   - Spaces in the glyph are not coloured to avoid background artefacts.
 *   - Logo and subtitle aligned to left instead of centre per UI spec.
 *
 * Why:
 *   - Left-aligned header matches standard CLI tool convention.
 *   - Gradient gives brand identity without requiring image rendering.
 *
 * Impact on other components:
 *   - model.go View() calls RenderHeader as the first layout block.
 *
 * Future maintenance notes:
 *   - To change the gradient direction, swap the colour parameters in
 *     interpolateColour or iterate columns in reverse.
 *   - Logo glyph must not be altered without updating the column count.
 */
