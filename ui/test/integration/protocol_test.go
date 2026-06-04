package integration

import (
	"context"
	"crypto/rand"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"io"
	"testing"
	"time"

	"yuumi/pkg/yuumi"
)

func TestProtocolMismatch(t *testing.T) {
	endpoint := "zeri-protocol-mismatch-" + randomToken(t, 6)
	stopServer := startProtocolMismatchServer(t, endpoint)
	defer stopServer()

	options := yuumi.ConnectOptions{
		MaxRetries: 0,
		BaseDelay: 10 * time.Millisecond,
		MaxDelay: 10 * time.Millisecond,
		DialTimeout: 1 * time.Second,
	}
	_, err := yuumi.Connect(endpoint, options)
	if err == nil {
		t.Fatalf("expected protocol mismatch error, got nil")
	}
	if !responseContains([]string{err.Error()}, "[ZERI][IPC-001]") {
		t.Fatalf("expected IPC-001 mismatch error, got: %v", err)
	}
}

func serveProtocolMismatchConn(t *testing.T, conn io.ReadWriteCloser) {
	t.Helper()
	defer conn.Close()

	handshake := make([]byte, 12)
	if _, err := io.ReadFull(conn, handshake); err != nil {
		t.Fatalf("failed reading client handshake: %v", err)
	}

	ack := make([]byte, 4)
	if _, err := conn.Write(ack); err != nil {
		t.Fatalf("failed writing transport handshake ack: %v", err)
	}

	appHandshake := map[string]interface{}{
		"type": "handshake",
		"protocol_version": 999,
	}
	if err := writeProtocolFrame(conn, appHandshake, 1); err != nil {
		t.Fatalf("failed writing app handshake frame: %v", err)
	}
}

func writeProtocolFrame(w io.Writer, payload interface{}, channel byte) error {
	body, err := json.Marshal(payload)
	if err != nil {
		return err
	}

	frame := make([]byte, 6+len(body))
	binary.LittleEndian.PutUint32(frame[0:4], uint32(len(body)))
	frame[4] = channel
	frame[5] = 0
	copy(frame[6:], body)

	_, err = w.Write(frame)
	return err
}

func randomToken(t *testing.T, bytesCount int) string {
	t.Helper()

	token := make([]byte, bytesCount)
	if _, err := rand.Read(token); err != nil {
		t.Fatalf("failed generating random token: %v", err)
	}
	return hex.EncodeToString(token)
}

func waitForDoneOrTimeout(t *testing.T, done <-chan struct{}, timeout time.Duration) {
	t.Helper()

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	select {
	case <-done:
	case <-ctx.Done():
		t.Fatalf("protocol mismatch server timeout after %s", timeout)
	}
}
