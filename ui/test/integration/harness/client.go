package harness

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"runtime"
	"strings"
	"testing"
	"time"
)

const (
	yuumiMagic uint32 = 0x59554D49
	yuumiProtocolVersion uint32 = 2
	appProtocolVersion = 1
	frameHeaderSize = 6
	channelControl byte = 0
	channelCommand byte = 1
	defaultSendTimeout = 10 * time.Second
	processExitTimeout = 3 * time.Second
)

type ZeriClient struct {
	t *testing.T
	conn net.Conn
	engine *EngineProcess
	sendTimeout time.Duration
}

func Connect(ep *EngineProcess) *ZeriClient {
	ep.t.Helper()

	conn := ep.preConn
	ep.preConn = nil
	if conn == nil {
		dialConn, err := tryDial(ep.endpoint, 2*time.Second)
		if err != nil {
			ep.t.Fatalf("failed to connect to engine endpoint %s: %v", ep.endpoint, err)
		}
		conn = dialConn
	}

	client := &ZeriClient{
		t: ep.t,
		conn: conn,
		engine: ep,
		sendTimeout: defaultSendTimeout,
	}

	if err := client.performTransportHandshake(); err != nil {
		_ = conn.Close()
		ep.t.Fatalf("yuumi handshake failed: %v", err)
	}

	hello, err := readFrame(conn)
	if err != nil {
		_ = conn.Close()
		ep.t.Fatalf("failed reading app handshake frame: %v", err)
	}

	msgType, _ := hello["type"].(string)
	if msgType != "handshake" {
		_ = conn.Close()
		ep.t.Fatalf("expected first app frame type=handshake, got %q", msgType)
	}

	protocolVersion, err := extractInt(hello["protocol_version"])
	if err != nil {
		_ = conn.Close()
		ep.t.Fatalf("handshake frame missing valid protocol_version: %v", err)
	}
	if protocolVersion != appProtocolVersion {
		_ = conn.Close()
		ep.t.Fatalf("protocol version mismatch: got %d expected %d", protocolVersion, appProtocolVersion)
	}

	if err := client.consumeStartupBatch(2 * time.Second); err != nil {
		_ = conn.Close()
		ep.t.Fatalf("failed to consume startup stream: %v", err)
	}

	ep.client = client
	return client
}

func Send(client *ZeriClient, command string) Response {
	client.t.Helper()

	if err := client.drainPendingFrames(100 * time.Millisecond); err != nil {
		client.t.Fatalf("failed draining pending frames before command %q: %v", command, err)
	}

	deadline := time.Now().Add(client.sendTimeout)
	if err := client.conn.SetDeadline(deadline); err != nil {
		client.t.Fatalf("failed to set send deadline: %v", err)
	}
	defer func() {
		_ = client.conn.SetDeadline(time.Time{})
	}()

	commandFrame := map[string]interface{}{
		"type":    "command",
		"payload": command,
	}
	if err := writeFrame(client.conn, commandFrame, channelCommand); err != nil {
		client.t.Fatalf("failed sending command %q: %v", command, err)
	}

	response := Response{
		Output: make([]string, 0),
		Errors: make([]string, 0),
	}
	for {
		frame, err := readFrame(client.conn)
		if err != nil {
			client.t.Fatalf("failed reading response frame for command %q: %v", command, err)
		}

		msgType, _ := frame["type"].(string)
		payload := payloadToString(frame["payload"])

		switch msgType {
		case "output", "info", "success":
			if payload != "" {
				response.Output = append(response.Output, payload)
			}
		case "error":
			if payload != "" {
				response.Errors = append(response.Errors, payload)
			}
		case "stream_batch_end":
			reason, _ := frame["reason"].(string)
			response.Reason = reason
			return response
		default:
		}
	}
}

func Cleanup(t *testing.T, ep *EngineProcess) {
	t.Helper()

	ep.cleanupOnce.Do(func() {
		if ep.client != nil && ep.client.conn != nil {
			_ = ep.client.conn.SetWriteDeadline(time.Now().Add(750 * time.Millisecond))
			_ = writeFrame(ep.client.conn, map[string]interface{}{"type": "quit"}, channelControl)
			_ = writeFrame(ep.client.conn, map[string]interface{}{"type": "command", "payload": "/exit"}, channelCommand)
			_ = ep.client.conn.Close()
		} else if ep.preConn != nil {
			_ = ep.preConn.Close()
		}

		if ep.exitCh != nil {
			select {
			case <-ep.exitCh:
			case <-time.After(processExitTimeout):
				if ep.cmd != nil && ep.cmd.Process != nil {
					_ = ep.cmd.Process.Kill()
				}
				<-ep.exitCh
			}
		}

		if runtime.GOOS != "windows" && strings.TrimSpace(ep.socketPath) != "" {
			_ = os.Remove(ep.socketPath)
		}

		if t.Failed() {
			stderr := strings.TrimSpace(ep.stderrBuf.String())
			if stderr != "" {
				t.Logf("zeri-engine stderr:\n%s", stderr)
			}
		}
	})
}

func (c *ZeriClient) performTransportHandshake() error {
	request := make([]byte, 12)
	binary.BigEndian.PutUint32(request[0:4], yuumiMagic)
	binary.BigEndian.PutUint32(request[4:8], yuumiProtocolVersion)
	binary.BigEndian.PutUint32(request[8:12], uint32(os.Getpid()))

	_ = c.conn.SetWriteDeadline(time.Now().Add(2 * time.Second))
	if _, err := c.conn.Write(request); err != nil {
		return fmt.Errorf("unable to write yuumi handshake: %w", err)
	}

	ack := make([]byte, 4)
	_ = c.conn.SetReadDeadline(time.Now().Add(3 * time.Second))
	if _, err := io.ReadFull(c.conn, ack); err != nil {
		return fmt.Errorf("unable to read yuumi handshake ack: %w", err)
	}

	_ = c.conn.SetDeadline(time.Time{})
	return nil
}

func (c *ZeriClient) consumeStartupBatch(timeout time.Duration) error {
	if timeout <= 0 {
		return nil
	}
	if err := c.conn.SetReadDeadline(time.Now().Add(timeout)); err != nil {
		return err
	}
	defer func() {
		_ = c.conn.SetReadDeadline(time.Time{})
	}()

	for {
		frame, err := readFrame(c.conn)
		if err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				return nil
			}
			return err
		}
		msgType, _ := frame["type"].(string)
		if msgType == "stream_batch_end" {
			return nil
		}
	}
}

func (c *ZeriClient) drainPendingFrames(timeout time.Duration) error {
	if timeout <= 0 {
		return nil
	}
	if err := c.conn.SetReadDeadline(time.Now().Add(timeout)); err != nil {
		return err
	}
	defer func() {
		_ = c.conn.SetReadDeadline(time.Time{})
	}()

	for {
		_, err := readFrame(c.conn)
		if err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				return nil
			}
			return err
		}
	}
}

func writeFrame(w io.Writer, payload interface{}, channel byte) error {
	body, err := json.Marshal(payload)
	if err != nil {
		return err
	}

	frame := make([]byte, frameHeaderSize+len(body))
	binary.LittleEndian.PutUint32(frame[0:4], uint32(len(body)))
	frame[4] = channel
	frame[5] = 0
	copy(frame[frameHeaderSize:], body)

	_, err = w.Write(frame)
	return err
}

func readFrame(r io.Reader) (map[string]interface{}, error) {
	header := make([]byte, frameHeaderSize)
	if _, err := io.ReadFull(r, header); err != nil {
		return nil, err
	}

	length := binary.LittleEndian.Uint32(header[0:4])
	body := make([]byte, length)
	if _, err := io.ReadFull(r, body); err != nil {
		return nil, err
	}

	var frame map[string]interface{}
	if err := json.Unmarshal(body, &frame); err != nil {
		return nil, err
	}
	return frame, nil
}

func extractInt(value interface{}) (int, error) {
	switch v := value.(type) {
	case float64:
		return int(v), nil
	case int:
		return v, nil
	case int32:
		return int(v), nil
	case int64:
		return int(v), nil
	default:
		return 0, fmt.Errorf("unsupported numeric type %T", value)
	}
}

func payloadToString(value interface{}) string {
	switch v := value.(type) {
	case nil:
		return ""
	case string:
		return v
	default:
		return fmt.Sprint(v)
	}
}
