package yuumi

import (
   "encoding/binary"
	"fmt"
	"io"
	"math/rand"
	"net"
	"os"
	"sync"
	"time"
)

type MessageHandler func(data map[string]interface{}, ch Channel)

type ConnectOptions struct {
	MaxRetries int
	BaseDelay time.Duration
	MaxDelay time.Duration
	DialTimeout time.Duration
}

func DefaultConnectOptions() ConnectOptions {
	return ConnectOptions{
		MaxRetries: 12,
		BaseDelay: 100 * time.Millisecond,
		MaxDelay: 4 * time.Second,
		DialTimeout: 2 * time.Second,
	}
}

type Client struct {
	conn net.Conn
	mu sync.Mutex
	handler MessageHandler
	onDisconnect func(error)
	running bool
}

func Connect(endpoint string, opts ...ConnectOptions) (*Client, error) {
	cfg := DefaultConnectOptions()
	if len(opts) > 0 {
		cfg = opts[0]
	}

	var conn net.Conn
	var lastErr error
	delay := cfg.BaseDelay

	for attempt := 0; attempt <= cfg.MaxRetries; attempt++ {
		if attempt > 0 {
			jitter := time.Duration(rand.Int63n(int64(delay) / 2))
			time.Sleep(delay + jitter)
			delay *= 2
			if delay > cfg.MaxDelay {
				delay = cfg.MaxDelay
			}
		}

		var err error
		conn, err = dialTransport(endpoint, cfg.DialTimeout)

		if err != nil {
			lastErr = err
			continue
		}

		c := &Client{conn: conn, running: true}

		if err := c.performHandshake(); err != nil {
			conn.Close()
			lastErr = err
			continue
		}

		go c.readLoop()
		return c, nil
	}

	return nil, NewError(ErrPipeFailed, fmt.Sprintf(
		"All %d connection attempts failed (last: %v)", cfg.MaxRetries+1, lastErr))
}

func (c *Client) OnMessage(h MessageHandler) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.handler = h
}

func (c *Client) OnDisconnect(fn func(error)) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.onDisconnect = fn
}

func (c *Client) Shutdown() error {
	_ = c.Send(map[string]interface{}{"type": "shutdown"}, ChannelControl)
	c.mu.Lock()
	c.running = false
	c.mu.Unlock()
	return c.conn.Close()
}

func (c *Client) Close() error {
	c.mu.Lock()
	c.running = false
	c.mu.Unlock()
	return c.conn.Close()
}

func (c *Client) readLoop() {
	defer c.conn.Close()

	var exitErr error

	for {
		c.mu.Lock()
		if !c.running {
			c.mu.Unlock()
			break
		}
		c.mu.Unlock()

		data, ch, err := c.receive()
		if err != nil {
			if err != io.EOF {
                fmt.Fprintf(os.Stderr, "[YUUMI_ERR][%d] Connection lost: %v\n", ErrReadTimeout, err)
			}
			exitErr = err
			break
		}

		c.mu.Lock()
		h := c.handler
		c.mu.Unlock()

		if h != nil {
			h(data, ch)
		}
	}

	c.mu.Lock()
	fn := c.onDisconnect
	wasRunning := c.running
	c.mu.Unlock()

	if fn != nil && wasRunning {
		fn(exitErr)
	}
}

func (c *Client) performHandshake() error {
	buf := make([]byte, 12)
	binary.BigEndian.PutUint32(buf[0:4], 0x59554D49)
	binary.BigEndian.PutUint32(buf[4:8], ProtocolVersion)
	binary.BigEndian.PutUint32(buf[8:12], uint32(os.Getpid()))

	c.conn.SetWriteDeadline(time.Now().Add(2 * time.Second))
	if _, err := c.conn.Write(buf); err != nil {
		return NewError(ErrWriteFailed, "Handshake: Write failed")
	}

	ack := make([]byte, 4)
	c.conn.SetReadDeadline(time.Now().Add(3 * time.Second))
	if _, err := io.ReadFull(c.conn, ack); err != nil {
		return NewError(ErrMagicMismatch, "Handshake: ACK not received")
	}

	c.conn.SetDeadline(time.Time{})
	return nil
}

func (c *Client) Send(data interface{}, ch Channel) error {
   frame, err := encodeFrame(data, ch)
	if err != nil {
		return NewError(ErrInternal, "Serialization failure")
	}

	c.mu.Lock()
	defer c.mu.Unlock()

   if _, err := c.conn.Write(frame); err != nil {
		return NewError(ErrWriteFailed, "Write failure")
	}
	return nil
}

func (c *Client) receive() (map[string]interface{}, Channel, error) {
   return decodeFrame(c.conn)
}

/*
 Client: Main IPC transport for the Go side.

 Connect: Establishes a resilient connection to the C++ server using
 exponential backoff with jitter. Retries both dial and handshake failures
 up to MaxRetries times (default 12), with delay doubling from BaseDelay
 (100ms) to MaxDelay (4s). ConnectOptions allows callers to override defaults.
 This eliminates the need for a hardcoded sleep after spawning ZeriEngine.

 Shutdown: Sends an explicit {"type":"shutdown"} message on ChannelControl
 before closing the connection. Used by Runner.Stop() for coordinated
 child-process teardown.

 Close: Raw connection teardown without sending a shutdown signal.
 Used for cleanup when the transport is already disconnected.

 OnMessage: Registers a callback for incoming data packets. The client
 performs only deserialization (JSON to map[string]interface{}) and
 forwards the raw data without interpreting any fields.

 OnDisconnect: Registers a callback invoked when readLoop exits due to
 a transport error (broken pipe, EOF, timeout). Only fires if the client
 was still in running state (not an intentional Close/Shutdown). This
 enables the TUI to reactively update the connected indicator when the
 engine process dies or the pipe breaks unexpectedly.

 performHandshake: Verifies protocol integrity (magic number, version)
 and process identification (PID).

 readLoop: Background goroutine that reads framed messages and dispatches
 to the registered handler. On exit, invokes the onDisconnect callback
 if the client was still running.

 Send/receive: Core methods for framed transport using protocol.go JSON codec.
*/
