package main

import (
	"fmt"
	"strings"
	"time"
	"yuumi/internal/ui"

	lg "charm.land/lipgloss/v2"
)

type RuntimeCenterEntry struct {
	Name string
	Check string
	Required bool
	Status RuntimeValidationStatus
	DetectedVersion string
	MinVersion string
	Hint string
	SuggestedAction string
}

type RuntimeCenterState struct {
	GeneratedAt string
	Summary string
	StartupLogPath string
	Entries []RuntimeCenterEntry
	LoadError string
}

func buildRuntimeCenterState(startupLogPath string) RuntimeCenterState {
	state := RuntimeCenterState{
		GeneratedAt: time.Now().Format("2006-01-02 15:04:05"),
		StartupLogPath: strings.TrimSpace(startupLogPath),
	}

	manifest, err := loadRuntimeManifest()
	if err != nil {
		state.LoadError = err.Error()
		state.Summary = "Runtime Center could not load the runtime manifest."
		return state
	}

	results := validateRequiredRuntimes(manifest)
	okCount := 0
	missingCount := 0
	outdatedCount := 0
	for _, result := range results {
		entry := RuntimeCenterEntry{
			Name: result.Runtime.Name,
			Check: result.Runtime.Check,
			Required: result.Runtime.Required,
			Status: result.Status,
			DetectedVersion: result.DetectedVersion,
			MinVersion: result.Runtime.MinVersion,
			Hint: result.Runtime.InstallHint,
			SuggestedAction: runtimeSuggestedAction(result.Runtime),
		}
		if strings.TrimSpace(entry.DetectedVersion) == "" {
			entry.DetectedVersion = "not detected"
		}
		switch result.Status {
		case RuntimeStatusOK:
			okCount++
		case RuntimeStatusMissing:
			missingCount++
		case RuntimeStatusOutdated:
			outdatedCount++
		}
		state.Entries = append(state.Entries, entry)
	}

	state.Summary = fmt.Sprintf("Runtime health: %d OK, %d missing, %d outdated.", okCount, missingCount, outdatedCount)
	return state
}

func runtimeSuggestedAction(runtime RuntimeDefinition) string {
	installers := installersForCurrentPlatform(runtime)
	if len(installers) == 0 {
		return runtime.InstallHint
	}

	command, supported := commandForInstaller(installers[0])
	if !supported {
		return runtime.InstallHint
	}

	return "Run: " + formatInstallCommand(command) + " | " + runtime.InstallHint
}

func formatInstallCommand(command InstallCommand) string {
	parts := []string{command.Executable}
	for _, arg := range command.Args {
		if strings.Contains(arg, " ") {
			parts = append(parts, "\""+arg+"\"")
			continue
		}
		parts = append(parts, arg)
	}
	return strings.Join(parts, " ")
}

func (m AppModel) renderRuntimeCenterModal() string {
	titleStyle := lg.NewStyle().Foreground(ui.ColourVolt).Bold(true)
	summaryStyle := lg.NewStyle().Foreground(ui.ColourWhite).Bold(true)
	metaStyle := lg.NewStyle().Foreground(ui.ColourIndustrialGrey)
	hintStyle := lg.NewStyle().Foreground(ui.ColourWhite).Background(ui.ColourElectricBlue).Bold(true).Padding(0, 1)

	lines := []string{
		titleStyle.Render("Runtime Center"),
       hintStyle.Render("ESC close"),
		summaryStyle.Render(m.runtimeCenter.Summary),
		metaStyle.Render("Generated at: " + m.runtimeCenter.GeneratedAt),
	}

	if strings.TrimSpace(m.runtimeCenter.LoadError) != "" {
		lines = append(lines, lg.NewStyle().Foreground(ui.ColourErrorRed).Render("Manifest error: "+m.runtimeCenter.LoadError))
	}

	for _, entry := range m.runtimeCenter.Entries {
		statusLabel := string(entry.Status)
		statusStyle := lg.NewStyle().Foreground(ui.ColourAcidGreen).Bold(true)
		if entry.Status == RuntimeStatusMissing {
			statusStyle = lg.NewStyle().Foreground(ui.ColourErrorRed).Bold(true)
		}
		if entry.Status == RuntimeStatusOutdated {
			statusStyle = lg.NewStyle().Foreground(ui.ColourVolt).Bold(true)
		}

		header := statusStyle.Render(statusLabel) + "  " + lg.NewStyle().Foreground(ui.ColourWhite).Bold(true).Render(entry.Name)
		requiredText := "optional"
		if entry.Required {
			requiredText = "required"
		}
		detail := fmt.Sprintf("Scope: %s | Detected: %s | Required min: %s", requiredText, entry.DetectedVersion, entry.MinVersion)
		lines = append(lines, header)
		lines = append(lines, lg.NewStyle().Foreground(ui.ColourIndustrialGrey).Render(detail))
		lines = append(lines, lg.NewStyle().Foreground(ui.ColourIndustrialGrey).Render(entry.Hint))
		lines = append(lines, lg.NewStyle().Foreground(ui.ColourWhite).Render(entry.SuggestedAction))
	}

	if strings.TrimSpace(m.runtimeCenter.StartupLogPath) != "" {
		lines = append(lines, metaStyle.Render("Detailed startup diagnostics: "+m.runtimeCenter.StartupLogPath))
	}
	lines = append(lines, metaStyle.Render("Press ESC to close Runtime Center."))

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

/*
runtime_center.go
Provides runtime status snapshot generation and Runtime Center modal rendering.
The modal exposes user-friendly runtime diagnostics and guided remediation commands
without flooding the main chat with raw startup logs.
*/
