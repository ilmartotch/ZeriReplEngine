package system

import (
	"os"
	"runtime"
)

func GetProcessMemoryMB() uint64 {
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	return m.Sys / (1024 * 1024)
}

func GetPID() int {
	return os.Getpid()
}

/*
 * CHANGES & RATIONALE
 * -------------------
 * [memory.go]
 *
 * What changed:
 *   - Provides GetProcessMemoryMB() for the status bar RAM indicator.
 *   - Uses runtime.MemStats.Sys which reports total bytes of memory
 *     obtained from the OS (includes heap, stack, GC metadata).
 *   - GetPID() is a convenience wrapper for the status bar.
 *
 * Why:
 *   - The status bar refreshes every 500ms and needs a fast, cross-platform
 *     memory reading. runtime.ReadMemStats is always available in Go.
 *
 * Impact on other components:
 *   - model.go calls GetProcessMemoryMB() on every statusTickMsg.
 *
 * Future maintenance notes:
 *   - For engine memory (C++ process), query via Yuumi STATUS frames
 *     instead of Go runtime stats.
 *   - On Windows, consider using Process.MemoryInfo for RSS if needed.
 */
