//go:build !windows

package harness

import (
	"net"
	"time"
)

func tryDial(endpoint string, timeout time.Duration) (net.Conn, error) {
	return net.DialTimeout("unix", endpointSocketPath(endpoint), timeout)
}
