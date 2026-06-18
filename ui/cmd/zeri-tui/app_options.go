package main

import (
	"fmt"
	"os"
	"strings"
	"time"
)

type appOptions struct {
	noOnboarding bool
	profileStartup bool
	exitAfterReady bool
	resetOnboarding bool
}

type startupProfiler struct {
	enabled  bool
	start    time.Time
	lastMark time.Time
	lines    []string
}

func parseAppOptions(args []string) (appOptions, error) {
	opts := appOptions{}
	for _, arg := range args {
		switch strings.TrimSpace(arg) {
		case "--no-onboarding":
			opts.noOnboarding = true
		case "--profile-startup":
			opts.profileStartup = true
		case "--exit-after-ready":
			opts.exitAfterReady = true
		case "--reset-onboarding":
			opts.resetOnboarding = true
		case "--version", "-v":
		default:
			if strings.HasPrefix(arg, "-") {
				return appOptions{}, fmt.Errorf("unknown option: %s", arg)
			}
		}
	}
	return opts, nil
}

func newStartupProfiler(enabled bool) *startupProfiler {
	now := time.Now()
	return &startupProfiler{
		enabled: enabled,
		start: now,
		lastMark: now,
		lines: make([]string, 0, 4),
	}
}

func (p *startupProfiler) Mark(segment string) {
	if p == nil || !p.enabled {
		return
	}
	now := time.Now()
	elapsed := now.Sub(p.lastMark)
	p.lastMark = now
	p.lines = append(p.lines, fmt.Sprintf("[startup] %s: %dms", segment, elapsed.Milliseconds()))
}

func (p *startupProfiler) Print() {
	if p == nil || !p.enabled {
		return
	}
	for _, line := range p.lines {
		_, _ = fmt.Fprintln(os.Stderr, line)
	}
}
