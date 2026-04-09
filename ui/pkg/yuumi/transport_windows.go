//go:build windows

package yuumi

import (
	"context"
	"net"
	"time"

	"github.com/Microsoft/go-winio"
)

func dialTransport(endpoint string, timeout time.Duration) (net.Conn, error) {
	pipePath := endpoint
	if len(pipePath) < 10 || pipePath[:9] != `\\.\pipe\` {
		pipePath = `\\.\pipe\` + endpoint
	}

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	return winio.DialPipeContext(ctx, pipePath)
}

/*
 transport_windows:
  - Build-tag windows-only transport adapter.
  - Uses Windows Named Pipes (\\.\pipe\<name>) via go-winio to match the
    C++ engine's CreateNamedPipeA transport.
  - If the endpoint is already a full pipe path (\\.\pipe\...), it is used as-is.
  - Otherwise the endpoint name is prefixed with \\.\pipe\ automatically.
*/
