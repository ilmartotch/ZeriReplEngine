//go:build windows

package main

import (
	"golang.org/x/sys/windows"
)

func platformPreflight() []*PreflightError {
	var errs []*PreflightError

	if err := checkVSRedist(); err != nil {
		errs = append(errs, err)
	}
	return errs
}

func checkVSRedist() *PreflightError {
	for _, dll := range []string{"vcruntime140.dll", "msvcp140.dll"} {
		lazy := windows.NewLazySystemDLL(dll)
		if err := lazy.Load(); err != nil {
			return &PreflightError{
                Code: "VC_REDIST_MISSING",
				Check: "Visual C++ Redistributable",
                Message: dll + " was not found on the system.",
				Hint: "Install Microsoft Visual C++ Redistributable: https://aka.ms/vs/17/release/vc_redist.x64.exe",
			}
		}
	}
	return nil
}

/*
preflight_windows.go — Windows-specific runtime checks.

checkVSRedist: ZeriEngine.exe links against the MSVC runtime dynamically.
If vcruntime140.dll or msvcp140.dll are absent, the engine process will
start and immediately crash with exit code 0xC0000139 (STATUS_ENTRYPOINT_NOT_FOUND),
which looks to the user like a silent connection failure. This check surfaces
the real cause before launching the engine.
*/
