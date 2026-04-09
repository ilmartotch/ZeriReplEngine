package yuumi

import "fmt"

const ProtocolVersion = 2

type StatusCode uint32

const (
	StatusHandshakeStart StatusCode = 100
	StatusConnecting StatusCode = 101

	StatusOK StatusCode = 200
	StatusMessageReceived StatusCode = 201
	StatusHeartbeat StatusCode = 202

	ErrMagicMismatch StatusCode = 400
	ErrVersionMismatch StatusCode = 401
	ErrPidMismatch StatusCode = 402
	ErrProtocolViolation StatusCode = 403

	ErrPipeFailed StatusCode = 500
	ErrReadTimeout StatusCode = 501
	ErrWriteFailed StatusCode = 502
	ErrConnectionLost StatusCode = 503
	ErrInternal StatusCode = 599
)

type Channel byte

const (
	ChannelControl Channel = 0
	ChannelCommand Channel = 1
	ChannelLog Channel = 2
	ChannelData Channel = 3
)

type Error struct {
	Code StatusCode
	Message string
}

func (e *Error) Error() string {
	return fmt.Sprintf("[YUUMI_ERR][%d] %s", e.Code, e.Message)
}

func NewError(code StatusCode, msg string) *Error {
	return &Error{Code: code, Message: msg}
}

/*
 * types.go: Protocol-level definitions for the Go client.
 * - ProtocolVersion: Versioning for backward compatibility checks.
 * - StatusCode: Numeric identifiers for cross-process event tracking.
 * - Channel: Port-like identifiers for data multiplexing.
 * - Error: Structured error type wrapping protocol-specific codes.
 */
