package ui

import (
	"strings"

	lg "charm.land/lipgloss/v2"
)

type AutocompleteModel struct {
	Visible       bool
	Filtered      []CompletionEntry
	SelectedIndex int
	ActiveContext  string
}

func (a *AutocompleteModel) Filter(partial string) {
	lower := strings.ToLower(partial)
	a.Filtered = a.Filtered[:0]

	if strings.HasPrefix(partial, "/") {
      pool := SlashCommandsForContext(a.ActiveContext)

		if partial == "/" {
			a.Filtered = make([]CompletionEntry, len(pool))
			copy(a.Filtered, pool)
		} else {
			for _, cmd := range pool {
				if strings.HasPrefix(strings.ToLower(cmd.Command), lower) {
					a.Filtered = append(a.Filtered, cmd)
				}
			}
		}
	} else if strings.HasPrefix(partial, "$") {
     contextCommands := ContextCommandsForContext(a.ActiveContext)
		if partial == "$" {
          a.Filtered = make([]CompletionEntry, len(contextCommands))
			copy(a.Filtered, contextCommands)
		} else {
           for _, cmd := range contextCommands {
				if strings.HasPrefix(strings.ToLower(cmd.Command), lower) {
					a.Filtered = append(a.Filtered, cmd)
				}
			}
		}
	}

	a.Visible = len(a.Filtered) > 0
	a.SelectedIndex = 0
}

func (a *AutocompleteModel) MoveUp() {
	if a.SelectedIndex > 0 {
		a.SelectedIndex--
	}
}

func (a *AutocompleteModel) MoveDown() {
	if a.SelectedIndex < len(a.Filtered)-1 {
		a.SelectedIndex++
	}
}

func (a *AutocompleteModel) Selected() (CompletionEntry, bool) {
	if !a.Visible || len(a.Filtered) == 0 {
		return CompletionEntry{}, false
	}
	return a.Filtered[a.SelectedIndex], true
}

func (a *AutocompleteModel) Dismiss() {
	a.Visible = false
	a.Filtered = a.Filtered[:0]
	a.SelectedIndex = 0
}

func (a AutocompleteModel) View(termWidth int) string {
	if !a.Visible || len(a.Filtered) == 0 {
		return ""
	}

	var rows []string

	for i, cmd := range a.Filtered {
		cmdText := lg.NewStyle().Foreground(ColourVolt).Render(cmd.Command)
		synopsis := lg.NewStyle().Foreground(ColourIndustrialGrey).Render(" · " + cmd.Synopsis)
		entry := "  " + cmdText + synopsis + "  "

		if i == a.SelectedIndex {
			entry = lg.NewStyle().
				Background(ColourDarkViolet).
				Bold(true).
				Render(entry)
		}
		rows = append(rows, entry)
	}

	menu := lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ColourDarkViolet).
		Render(strings.Join(rows, "\n"))

	return menu
}

/*
 * CHANGES & RATIONALE
 *
 * What changed:
 *   - Internal types updated from SlashCommand to CompletionEntry.
 *   - Filter() now handles both "/" and "$" prefixes:
 *     "/" filters SlashCommands, "$" filters ContextCommands.
 *   - Selected() returns CompletionEntry for consistent typing.
 *   - Full list rendered without sliding window.
 *
 * Why:
 *   - Context switches use $ prefix per meta-language spec. Users
 *     need autocomplete for both / commands and $ context switches.
 *   - Separate source lists keep concerns clear while sharing the
 *     same UI overlay.
 *
 * Impact on other components:
 *   - model.go triggers autocomplete on both "/" and "$" prefixes.
 *   - commands.go provides context-specific pools (SandboxCommands,
 *     CodeCommands) selected via ActiveContext.
 */
