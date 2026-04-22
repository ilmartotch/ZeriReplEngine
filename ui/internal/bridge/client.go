package bridge

import tea "charm.land/bubbletea/v2"

type DataMsg struct{ Content string }
type ErrorMsg struct{ Content string }
type StatusMsg struct {
	Connected bool
	Lang string
	MemMB uint64
}
type ConnectedMsg struct{}
type DisconnectedMsg struct{ Reason string }
type InputRequestMsg struct{ Prompt string }
type ContextChangedMsg struct {
	ContextName string
	Active bool
}

type YuumiClient interface {
	ConnectCmd() tea.Cmd
	SendDataCmd(s string) tea.Cmd
	SendInputResponseCmd(value string) tea.Cmd
	SendControlCmd(cmd string) tea.Cmd
	SendShutdownCmd() tea.Cmd
}

/*
 * CHANGES & RATIONALE
 * -------------------
 * [client.go]
 *
 * What changed:
 *   - ContextChangedMsg now carries ContextName + Active only.
 *     This reflects the generic context push/pop model used by the
 *     engine and removes coupling to the old Session 2 code-mode flow.
 *
 * Why:
 *   - The TUI must update its prompt label and status bar reactively
 *     when the engine switches context.
 *   - The TUI must react to generic context transitions independent of
 *     specific command families.
 *   - Using typed messages keeps the Elm-architecture contract clean.
 *
 * Impact on other components:
 *   - model.go Update() uses Active/ContextName to update activeContext.
 *   - yuumi_client.go maps both context_changed and code_mode to this
 *     unified context transition message shape.
 */
