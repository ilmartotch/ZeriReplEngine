//go:build windows

package harness

import (
	"context"
	"net"
	"strings"
	"time"

	"github.com/Microsoft/go-winio"
)

func tryDial(endpoint string, timeout time.Duration) (net.Conn, error) {
	pipePath := endpoint
	if !strings.HasPrefix(pipePath, `\\.\pipe\`) {
		pipePath = `\\.\pipe\` + endpoint
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	return winio.DialPipeContext(ctx, pipePath)
}
