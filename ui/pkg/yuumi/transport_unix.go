//go:build !windows

package yuumi

import (
	"net"
	"os"
	"path/filepath"
	"time"
)

func dialTransport(endpoint string, timeout time.Duration) (net.Conn, error) {
	address := endpoint
	if !filepath.IsAbs(address) {
		address = filepath.Join(os.TempDir(), address)
		if filepath.Ext(address) == "" {
			address += ".sock"
		}
	}

	return net.DialTimeout("unix", address, timeout)
}

/*
 transport_unix:
  - Build-tag non-Windows transport adapter.
  - Translates logical endpoint names into absolute Unix socket paths.
  - Uses net.DialTimeout over unix domain sockets.
*/
