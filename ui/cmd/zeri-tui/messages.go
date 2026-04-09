package main

/*
 * CHANGES & RATIONALE
 * -------------------
 * [messages.go]
 *
 * What changed:
 *   - All custom message types moved to internal/bridge/client.go:
 *     DataMsg, ErrorMsg, ConnectedMsg, DisconnectedMsg.
 *   - statusTickMsg defined in model.go alongside the tick command.
 *   - This file is retained as a placeholder for any future TUI-internal
 *     messages that don't belong in the bridge package.
 *
 * Why:
 *   - Bridge messages are owned by the bridge package for clean separation.
 *   - The old MsgCoreData raw-map approach is replaced by typed messages.
 *
 * Impact on other components:
 *   - model.go imports bridge.DataMsg, bridge.ErrorMsg, etc. directly.
 *
 * Future maintenance notes:
 *   - Add TUI-specific messages here (e.g. theme change, modal open).
 */
