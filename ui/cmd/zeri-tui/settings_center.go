package main

import (
	"errors"
	"fmt"
	"os"
	"runtime"
	"strings"
	"yuumi/internal/ui"

	tea "charm.land/bubbletea/v2"
	lg "charm.land/lipgloss/v2"
)

type SettingsCenterState struct {
	DataRoot string
	DataRootResolved bool
	DefaultDataParent string
	OnboardingDone bool
	AiEndpoint string
	AiModel string
	AiConfigured bool
	LoadError string
}

func buildSettingsCenterState() SettingsCenterState {
	state := SettingsCenterState{}

	dataRoot, resolved, err := ResolveDataRoot()
	if err != nil {
		state.LoadError = err.Error()
	}
	state.DataRoot = strings.TrimSpace(dataRoot)
	state.DataRootResolved = resolved

	if parent, parentErr := DefaultDataParent(); parentErr == nil {
		state.DefaultDataParent = strings.TrimSpace(parent)
	} else if state.LoadError == "" {
		state.LoadError = parentErr.Error()
	}

	if done, doneErr := OnboardingCompleted(); doneErr == nil {
		state.OnboardingDone = done
	} else if state.LoadError == "" {
		state.LoadError = doneErr.Error()
	}

	if cfg, cfgErr := loadAiContextConfig(); cfgErr == nil {
		state.AiEndpoint = strings.TrimSpace(cfg.Endpoint)
		state.AiModel = strings.TrimSpace(cfg.Model)
		state.AiConfigured = cfg.IsConfigured()
	}

	return state
}

func migrationCommandBlock(oldRoot string, newRoot string) string {
	oldRoot = strings.TrimSpace(oldRoot)
	newRoot = strings.TrimSpace(newRoot)
	if oldRoot == "" || newRoot == "" {
		return ""
	}
	if filepathEqual(oldRoot, newRoot) {
		return ""
	}

	oldQuoted := quoteShellPath(oldRoot)
	newQuoted := quoteShellPath(newRoot)

	var builder strings.Builder
	builder.WriteString("To copy your existing data to the new location, run one of the following from a terminal.\n\n")
	builder.WriteString("PowerShell (Windows):\n")
	builder.WriteString(fmt.Sprintf("robocopy %s %s /E\n", oldQuoted, newQuoted))
	builder.WriteString(fmt.Sprintf("Copy-Item -Path %s -Destination %s -Recurse -Force\n\n", oldQuoted, newQuoted))
	builder.WriteString("POSIX (macOS / Linux):\n")
	builder.WriteString(fmt.Sprintf("cp -a %s/. %s/\n", oldQuoted, newQuoted))
	builder.WriteString(fmt.Sprintf("rsync -a %s/ %s/", oldQuoted, newQuoted))

	return builder.String()
}

func quoteShellPath(path string) string {
	trimmed := strings.TrimSpace(path)
	if trimmed == "" {
		return "\"\""
	}
	return "\"" + trimmed + "\""
}

func filepathEqual(a string, b string) bool {
	a = strings.TrimRight(strings.TrimSpace(a), "/\\")
	b = strings.TrimRight(strings.TrimSpace(b), "/\\")
	if runtime.GOOS == "windows" {
		return strings.EqualFold(a, b)
	}
	return a == b
}

func (m AppModel) renderSettingsModal() string {
	titleStyle := lg.NewStyle().Foreground(ui.ColourVolt).Bold(true)
	labelStyle := lg.NewStyle().Foreground(ui.ColourIndustrialGrey)
	valueStyle := lg.NewStyle().Foreground(ui.ColourWhite).Bold(true)
	hintStyle := lg.NewStyle().Foreground(ui.ColourWhite).Background(ui.ColourElectricBlue).Bold(true).Padding(0, 1)

	state := m.settingsCenter

	dataRoot := state.DataRoot
	if dataRoot == "" {
		dataRoot = "not selected"
	}
	dataRootScope := "from location pointer"
	if !state.DataRootResolved {
		dataRootScope = "default fallback"
	}

	defaultParent := state.DefaultDataParent
	if defaultParent == "" {
		defaultParent = "unavailable"
	}

	onboardingLabel := "incomplete"
	if state.OnboardingDone {
		onboardingLabel = "completed"
	}

	aiLabel := "not configured"
	if state.AiConfigured {
		endpoint := state.AiEndpoint
		if endpoint == "" {
			endpoint = "unset"
		}
		model := state.AiModel
		if model == "" {
			model = "unset"
		}
		aiLabel = fmt.Sprintf("endpoint %s | model %s", endpoint, model)
	}

	lines := []string{
		titleStyle.Render("Settings"),
		hintStyle.Render("ESC close"),
		"",
		labelStyle.Render("Data root") + "  " + valueStyle.Render(dataRoot),
		labelStyle.Render("Source") + "  " + valueStyle.Render(dataRootScope),
		labelStyle.Render("Default data parent") + "  " + valueStyle.Render(defaultParent),
		labelStyle.Render("Onboarding") + "  " + valueStyle.Render(onboardingLabel),
		labelStyle.Render("AI config") + "  " + valueStyle.Render(aiLabel),
	}

	if strings.TrimSpace(state.LoadError) != "" {
		lines = append(lines, "")
		lines = append(lines, lg.NewStyle().Foreground(ui.ColourErrorRed).Render("Read error: "+state.LoadError))
	}

	lines = append(lines, "")
	lines = append(lines, labelStyle.Render("Change data location: /settings path <parent-folder>"))
	lines = append(lines, labelStyle.Render("Only the pointer is updated; existing data is not moved automatically."))
	lines = append(lines, labelStyle.Render("Press ESC to close Settings."))

	panelWidth := m.width - 4
	if panelWidth < 60 {
		panelWidth = 60
	}

	return lg.NewStyle().
		Border(lg.RoundedBorder()).
		BorderForeground(ui.ColourElectricBlue).
		Padding(1, 2).
		Width(panelWidth).
		Render(strings.Join(lines, "\n"))
}

func (m AppModel) handleSettingsPathChange(parent string) (tea.Model, tea.Cmd) {
	trimmed := strings.TrimSpace(parent)
	if trimmed == "" {
		m.addSystemMessage("Provide a parent folder. Usage: /settings path <parent-folder>.")
		return m, nil
	}

	oldRoot, _, _ := ResolveDataRoot()

	resolved, alreadyHasData, alreadyChosen, inspectErr := InspectDataParent(trimmed)
	if inspectErr != nil {
		if isPermissionError(inspectErr) {
			m.addErrorMessage("Permission denied for " + trimmed + ".\nChoose a folder you can write to, then run /settings path <parent-folder> again.")
			return m, nil
		}
		m.addErrorMessage("Cannot use " + trimmed + ".\n" + inspectErr.Error())
		return m, nil
	}

	if alreadyChosen {
		m.addSystemMessage("Data location is already set to " + resolved + ". No changes were made.")
		return m, nil
	}

	newRoot, setErr := SetDataRootUnderParent(trimmed)
	if setErr != nil {
		if isPermissionError(setErr) {
			m.addErrorMessage("Permission denied creating " + resolved + ".\nChoose a folder you can write to, then run /settings path <parent-folder> again.")
			return m, nil
		}
		m.addErrorMessage("Could not update the data location.\n" + setErr.Error())
		return m, nil
	}

	var builder strings.Builder
	builder.WriteString("Data location updated successfully.\n")
	builder.WriteString("New location: " + newRoot + "\n")
	builder.WriteString("Restart Zeri to use the new location.\n")
	if alreadyHasData {
		builder.WriteString("The target folder already contains data; review it before migrating.\n")
	}

	migration := migrationCommandBlock(oldRoot, newRoot)
	if migration != "" {
		builder.WriteString("\n")
		builder.WriteString(migration)
	}

	m.addSystemMessage(builder.String())
	if m.settingsVisible {
		m.settingsCenter = buildSettingsCenterState()
	}
	return m, nil
}

func isPermissionError(err error) bool {
	if err == nil {
		return false
	}
	return errors.Is(err, os.ErrPermission) || strings.Contains(strings.ToLower(err.Error()), "permission denied")
}

/*
settings_center.go
Implements the /settings command: a read-only Settings modal (modelled on the
Runtime Center) plus the /settings path <parent> subcommand for relocating the
data root. The path change updates only the location pointer via
SetDataRootUnderParent and never moves data; instead it emits ready-to-run,
cross-platform migration commands (PowerShell robocopy/Copy-Item and POSIX
cp/rsync) generated from the real old and new data roots. InspectDataParent is
used to detect an already-chosen target, a folder that already holds data, and
permission errors before any write. All emitted text passes through
NormaliseContent, so lines avoid leading indentation and multi-space alignment.
*/
