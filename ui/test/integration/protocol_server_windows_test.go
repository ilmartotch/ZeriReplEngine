//go:build windows

package integration

import (
	"strings"
	"testing"
	"time"

	"github.com/Microsoft/go-winio"
)

func startProtocolMismatchServer(t *testing.T, endpoint string) func() {
	t.Helper()

	pipePath := endpoint
	if !strings.HasPrefix(pipePath, `\\.\pipe\`) {
		pipePath = `\\.\pipe\` + endpoint
	}

	listener, err := winio.ListenPipe(pipePath, nil)
	if err != nil {
		t.Fatalf("failed starting protocol mismatch pipe listener: %v", err)
	}

	done := make(chan struct{})
	go func() {
		defer close(done)
		conn, acceptErr := listener.Accept()
		if acceptErr != nil {
			return
		}
		serveProtocolMismatchConn(t, conn)
	}()

	return func() {
		_ = listener.Close()
		waitForDoneOrTimeout(t, done, 2*time.Second)
	}
}
