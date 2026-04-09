//go:build !windows

package main

func platformPreflight() []*PreflightError {
	return nil
}

/*
preflight_other.go — Non-Windows preflight stub.

platformPreflight() returns nil on Unix/macOS platforms because
Unix Domain Sockets are natively available and no specific runtime
DLL dependencies are required.
*/
