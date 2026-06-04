//go:build !windows

package integration

import (
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func startProtocolMismatchServer(t *testing.T, endpoint string) func() {
	t.Helper()

	socketPath := endpoint
	if !filepath.IsAbs(socketPath) {
		socketPath = filepath.Join(os.TempDir(), endpoint)
	}
	if filepath.Ext(socketPath) == "" {
		socketPath += ".sock"
	}
	_ = os.Remove(socketPath)

	listener, err := net.Listen("unix", socketPath)
	if err != nil {
		t.Fatalf("failed starting protocol mismatch unix socket listener: %v", err)
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
		_ = os.Remove(socketPath)
	}
}
