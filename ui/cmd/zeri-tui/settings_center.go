package main

import (
	"errors"
	"fmt"
	"os"
	"runtime"
	"strings"
	"yuumi/internal/aicontext"
	"yuumi/internal/bridge"
	"yuumi/internal/ui"

	tea "charm.land/bubbletea/v2"
	lg "charm.land/lipgloss/v2"
)

type SettingsCenterState struct {
	DataRoot string
	DataRootResolved bool
	DefaultDataParent string
	OnboardingDone bool
	SandboxIde string
	AiEndpoint string
	AiModel string
	AiKeySet bool
	AiConfigured bool
	LoadError string
}

type SettingsRequestKind int

const (
	SettingsRequestNone SettingsRequestKind = iota
	SettingsRequestOpenModal
	SettingsRequestAiEntry
)

type SettingsUpdateOrigin int

const (
	SettingsUpdateOriginNone SettingsUpdateOrigin = iota
	SettingsUpdateOriginSettingsHub
	SettingsUpdateOriginAiContext
)

type SettingsUpdateField int

const (
	SettingsUpdateFieldNone SettingsUpdateField = iota
	SettingsUpdateFieldSandboxIde
	SettingsUpdateFieldAiEndpoint
	SettingsUpdateFieldAiModel
	SettingsUpdateFieldAiKey
)

type PendingSettingsUpdate struct {
	Field SettingsUpdateField
	Origin SettingsUpdateOrigin
	AiKeyCleared bool
}

func buildSettingsCenterState(previous SettingsCenterState) SettingsCenterState {
	state := previous
	state.LoadError = ""

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

	if strings.TrimSpace(state.SandboxIde) == "" {
		state.SandboxIde = "code"
	}
	if strings.TrimSpace(state.AiEndpoint) == "" {
		state.AiEndpoint = aicontext.DefaultEndpoint
	}
	if strings.TrimSpace(state.AiModel) == "" {
		state.AiModel = aicontext.DefaultModel
	}

	return state
}

func (m *AppModel) requestSettingsSnapshot(kind SettingsRequestKind) tea.Cmd {
	m.pendingSettingsRequest = kind
	if m.bridge == nil {
		return nil
	}
	return m.bridge.SendCommandPayloadCmd(map[string]interface{}{"type": "settings_snapshot"})
}

func (m *AppModel) requestSettingsUpdate(
	field SettingsUpdateField,
	origin SettingsUpdateOrigin,
	payload map[string]interface{},
) tea.Cmd {
	update := PendingSettingsUpdate{
		Field:  field,
		Origin: origin,
	}
	if field == SettingsUpdateFieldAiKey {
		keyValue, _ := payload["ai_key"].(string)
		update.AiKeyCleared = strings.TrimSpace(keyValue) == ""
	}
	m.pendingSettingsUpdate = update
	if m.bridge == nil {
		return nil
	}
	return m.bridge.SendCommandPayloadCmd(payload)
}

func (m *AppModel) applySettingsSnapshot(snapshot bridge.SettingsSnapshot, applyAiRuntime bool) {
	next := m.settingsCenter
	next.SandboxIde = strings.TrimSpace(snapshot.SandboxIde)
	next.AiEndpoint = strings.TrimSpace(snapshot.AiEndpoint)
	next.AiModel = strings.TrimSpace(snapshot.AiModel)
	next.AiKeySet = snapshot.AiKeyPresent
	next.AiConfigured = snapshot.AiConfigured
	m.settingsCenter = buildSettingsCenterState(next)

	if !applyAiRuntime {
		return
	}
	m.aiContext.SetEndpoint(snapshot.AiEndpoint)
	m.aiContext.SetModelName(snapshot.AiModel)
	m.aiContext.SetApiKey(snapshot.AiKey)
	m.aiConfigured = snapshot.AiConfigured
}

func (m AppModel) handleSettingsSnapshotResponse(msg bridge.SettingsSnapshotResponseMsg) (tea.Model, tea.Cmd) {
	requestKind := m.pendingSettingsRequest
	m.pendingSettingsRequest = SettingsRequestNone

	if !msg.Ok {
		m.settingsCenter.LoadError = strings.TrimSpace(msg.Error)
		m.settingsCenter = buildSettingsCenterState(m.settingsCenter)
		m.addErrorMessage("Settings load failed: " + strings.TrimSpace(msg.Error))
		if requestKind == SettingsRequestAiEntry {
			return m, nil
		}
		return m, nil
	}

	applyAiRuntime := requestKind == SettingsRequestAiEntry || !m.aiModeActive
	m.applySettingsSnapshot(msg.Snapshot, applyAiRuntime)

	if requestKind == SettingsRequestAiEntry {
		return m.enterAiContext()
	}
	return m, nil
}

func (m AppModel) handleSettingsUpdateResponse(msg bridge.SettingsUpdateResponseMsg) (tea.Model, tea.Cmd) {
	update := m.pendingSettingsUpdate
	m.pendingSettingsUpdate = PendingSettingsUpdate{}

	if update.Field == SettingsUpdateFieldNone {
		return m, nil
	}

	if !msg.Ok {
		label := settingsFieldLabel(update.Field)
		errorText := strings.TrimSpace(msg.Error)
		if errorText == "" {
			errorText = "unknown engine error"
		}
		m.addErrorMessage("Settings update failed (" + label + "): " + errorText)
		return m, nil
	}

	applyAiRuntime := update.Origin == SettingsUpdateOriginAiContext || !m.aiModeActive
	m.applySettingsSnapshot(msg.Snapshot, applyAiRuntime)

	switch update.Origin {
	case SettingsUpdateOriginAiContext:
		switch update.Field {
		case SettingsUpdateFieldAiEndpoint:
			m.addSystemMessage("AI endpoint set to " + m.aiContext.Endpoint() + ". Re-checking endpoint...")
			return m, m.aiContext.StartConnectivityCheck()
		case SettingsUpdateFieldAiModel:
			m.addSystemMessage("AI model set to " + m.aiContext.ModelName() + ".")
		case SettingsUpdateFieldAiKey:
			if update.AiKeyCleared {
				m.addSystemMessage("AI API key cleared.")
			} else {
				m.addSystemMessage("AI API key saved (hidden). Re-checking endpoint...")
			}
			return m, m.aiContext.StartConnectivityCheck()
		}
	case SettingsUpdateOriginSettingsHub:
		switch update.Field {
		case SettingsUpdateFieldSandboxIde:
			m.addSystemMessage("Sandbox IDE updated to " + m.settingsCenter.SandboxIde + ".")
		case SettingsUpdateFieldAiEndpoint, SettingsUpdateFieldAiModel, SettingsUpdateFieldAiKey:
			if m.aiModeActive {
				m.addSystemMessage("AI settings updated. Exit and re-enter $ai to apply changes in the active AI session.")
			} else {
				m.addSystemMessage("AI settings updated in /settings.")
			}
		}
	}

	return m, nil
}

func settingsFieldLabel(field SettingsUpdateField) string {
	switch field {
	case SettingsUpdateFieldSandboxIde:
		return "sandbox::ide"
	case SettingsUpdateFieldAiEndpoint:
		return "AI endpoint"
	case SettingsUpdateFieldAiModel:
		return "AI model"
	case SettingsUpdateFieldAiKey:
		return "AI key"
	default:
		return "unknown"
	}
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
	if IsDataRootEnvOverrideActive() && state.DataRootResolved {
		dataRootScope = "from ZERI_HOME override"
	}
	if !state.DataRootResolved {
		dataRootScope = "not configured"
	}

	defaultParent := state.DefaultDataParent
	if defaultParent == "" {
		defaultParent = "unavailable"
	}

	onboardingLabel := "incomplete"
	if state.OnboardingDone {
		onboardingLabel = "completed"
	}

	aiKeyLabel := "not set"
	if state.AiKeySet {
		aiKeyLabel = "set"
	}

	aiConfiguredLabel := "no"
	if state.AiConfigured {
		aiConfiguredLabel = "yes"
	}

	lines := []string{
		titleStyle.Render("Settings"),
		hintStyle.Render("ESC close"),
		"",
		labelStyle.Render("Editable configuration hub (/settings is the only write path):"),
		labelStyle.Render("1. IDE command (sandbox::ide): ") + valueStyle.Render(state.SandboxIde),
		labelStyle.Render(" Update: /settings ide <name>"),
		labelStyle.Render("2. Data location pointer (location.json -> data_root): ") + valueStyle.Render(dataRoot),
		labelStyle.Render(" Update: /settings path <parent-folder>"),
		labelStyle.Render("3. AI endpoint: ") + valueStyle.Render(state.AiEndpoint),
		labelStyle.Render(" Update: /settings ai endpoint <url>"),
		labelStyle.Render("4. AI model: ") + valueStyle.Render(state.AiModel),
		labelStyle.Render(" Update: /settings ai model <name>"),
		labelStyle.Render("5. AI key: ") + valueStyle.Render(aiKeyLabel),
		labelStyle.Render(" Update: /settings ai key <key>   (use clear to remove)"),
		"",
		labelStyle.Render("Read-only diagnostics:"),
		labelStyle.Render("Source") + "  " + valueStyle.Render(dataRootScope),
		labelStyle.Render("Default data parent") + "  " + valueStyle.Render(defaultParent),
		labelStyle.Render("Onboarding done") + "  " + valueStyle.Render(onboardingLabel),
		labelStyle.Render("AI configured") + "  " + valueStyle.Render(aiConfiguredLabel),
		labelStyle.Render("version/_comment/source fields are system-managed and read-only."),
	}

	if strings.TrimSpace(state.LoadError) != "" {
		lines = append(lines, "")
		lines = append(lines, lg.NewStyle().Foreground(ui.ColourErrorRed).Render("Read error: "+state.LoadError))
	}

	lines = append(lines, "")
	lines = append(lines, labelStyle.Render("Data-location update only rewrites the pointer. Existing data is never moved automatically."))
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
		m.settingsCenter = buildSettingsCenterState(m.settingsCenter)
	}
	return m, nil
}

func (m AppModel) handleSettingsCommand(remainder string) (tea.Model, tea.Cmd, bool) {
	if value, ok := parseCommandArgument(remainder, "ide"); ok {
		if m.bridge == nil {
			m.addErrorMessage("Cannot update IDE settings: engine bridge is not connected.")
			return m, nil, true
		}
		return m, m.requestSettingsUpdate(SettingsUpdateFieldSandboxIde, SettingsUpdateOriginSettingsHub, map[string]interface{}{
			"type":        "settings_update",
			"sandbox_ide": value,
		}), true
	}
	if strings.EqualFold(strings.TrimSpace(remainder), "ide") {
		m.addSystemMessage("Usage: /settings ide <name>")
		return m, nil, true
	}

	field, value, matched, usage := parseSettingsAiCommand(remainder)
	if matched {
		if usage != "" {
			m.addSystemMessage(usage)
			return m, nil, true
		}
		if m.bridge == nil {
			m.addErrorMessage("Cannot update AI settings: engine bridge is not connected.")
			return m, nil, true
		}

		payload := map[string]interface{}{"type": "settings_update"}
		switch field {
		case SettingsUpdateFieldAiEndpoint:
			payload["ai_endpoint"] = value
		case SettingsUpdateFieldAiModel:
			payload["ai_model"] = value
		case SettingsUpdateFieldAiKey:
			payload["ai_key"] = value
		default:
			m.addSystemMessage("Unknown /settings ai option.")
			return m, nil, true
		}

		return m, m.requestSettingsUpdate(field, SettingsUpdateOriginSettingsHub, payload), true
	}

	return m, nil, false
}

func parseSettingsAiCommand(remainder string) (SettingsUpdateField, string, bool, string) {
	trimmed := strings.TrimSpace(remainder)
	lower := strings.ToLower(trimmed)
	if !strings.HasPrefix(lower, "ai") {
		return SettingsUpdateFieldNone, "", false, ""
	}
	if lower == "ai" {
		return SettingsUpdateFieldNone, "", true, "Usage: /settings ai endpoint <url> | /settings ai model <name> | /settings ai key <key|clear>"
	}
	if !strings.HasPrefix(lower, "ai ") {
		return SettingsUpdateFieldNone, "", true, "Usage: /settings ai endpoint <url> | /settings ai model <name> | /settings ai key <key|clear>"
	}

	tail := strings.TrimSpace(trimmed[len("ai"):])

	if value, ok := parseCommandArgument(tail, "endpoint"); ok {
		return SettingsUpdateFieldAiEndpoint, strings.TrimSpace(value), true, ""
	}
	if strings.EqualFold(strings.TrimSpace(tail), "endpoint") {
		return SettingsUpdateFieldNone, "", true, "Usage: /settings ai endpoint <url>"
	}

	if value, ok := parseCommandArgument(tail, "model"); ok {
		return SettingsUpdateFieldAiModel, strings.TrimSpace(value), true, ""
	}
	if strings.EqualFold(strings.TrimSpace(tail), "model") {
		return SettingsUpdateFieldNone, "", true, "Usage: /settings ai model <name>"
	}

	if value, ok := parseCommandArgument(tail, "key"); ok {
		normalized := strings.TrimSpace(value)
		if strings.EqualFold(normalized, "clear") || strings.EqualFold(normalized, "none") {
			normalized = ""
		}
		return SettingsUpdateFieldAiKey, normalized, true, ""
	}
	if strings.EqualFold(strings.TrimSpace(tail), "key") {
		return SettingsUpdateFieldNone, "", true, "Usage: /settings ai key <key>  (use clear to remove)"
	}

	return SettingsUpdateFieldNone, "", true, "Usage: /settings ai endpoint <url> | /settings ai model <name> | /settings ai key <key|clear>"
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

func isPermissionError(err error) bool {
	if err == nil {
		return false
	}
	return errors.Is(err, os.ErrPermission) || strings.Contains(strings.ToLower(err.Error()), "permission denied")
}

/*
settings_center.go
Implements /settings as the single configuration hub for:
  - sandbox::ide (engine persisted key),
  - data-location pointer updates (location.json),
  - AI config (endpoint/model/key in engine persisted keys).

Data-location updates remain direct pointer writes (tmp+rename), while IDE/AI
writes are performed through bridge requests so engine state stays authoritative.
*/
