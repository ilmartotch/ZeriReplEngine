package ui

type MessageRole int

const (
	RoleUser   MessageRole = iota
	RoleZeri
	RoleSystem
)

type ChatMessage struct {
	Role      MessageRole
	Content   string
	Timestamp string
}

/*
 * CHANGES & RATIONALE
 * -------------------
 * [message.go]
 *
 * What changed:
 *   - Defines the ChatMessage model and MessageRole enum.
 *   - Three roles: User (right-aligned bubbles), Zeri (left-aligned
 *     with blue border), System (centred italic).
 *   - Timestamp field stores "HH:MM" for display next to bubbles.
 *
 * Why:
 *   - Replaces the old HistoryEntry struct which lacked role-based
 *     rendering and timestamp support.
 *   - Separates data model from rendering (messages.go handles View).
 *
 * Impact on other components:
 *   - messages.go uses ChatMessage for all render functions.
 *   - model.go stores []ChatMessage as the chat history.
 *   - update.go creates ChatMessage instances on submit and bridge events.
 *
 * Future maintenance notes:
 *   - Add RoleError if error messages need distinct bubble styling.
 */
