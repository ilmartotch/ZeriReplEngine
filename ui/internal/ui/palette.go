package ui

import (
	"image/color"

	lg "charm.land/lipgloss/v2"
)

var (
	ColourVolt color.Color = lg.Color("#D9D923")
	ColourAcidGreen color.Color = lg.Color("#A3E635")
	ColourElectricBlue color.Color = lg.Color("#2C92F0")
	AzzurroElettrico color.Color = ColourElectricBlue
	ColourDarkViolet color.Color = lg.Color("#221F40")
	ColourIndustrialGrey color.Color = lg.Color("#C4C4A5")

	ColourErrorRed color.Color = lg.Color("#FF4444")
	ColourWhite color.Color = lg.Color("#FFFFFF")
)

/*
 *
 * What changed:
 *   - Created dedicated palette file with Zeri brand colours.
 *   - All hex colour strings are centralised here; no other file
 *     may contain inline hex colour values.
 *   - ColourErrorRed added for disconnection / error indicators.
 *
 * Why:
 *   - Enforces single-source-of-truth for the colour palette.
 *   - Matches the v3 TUI spec: Volt (neon yellow), AcidGreen (lime),
 *     ElectricBlue (focused borders), DarkViolet (dark backgrounds),
 *     IndustrialGrey (secondary text).
 *
 * Impact on other components:
 *   - Every ui/ file imports colours from here.
 *   - statusbar.go, messages.go, logo.go, autocomplete.go all depend on this.
 *
 * Future maintenance notes:
 *   - To add a theme system, replace these vars with a Theme struct.
 */
