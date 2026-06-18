package main

import (
	"encoding/json"
	"os"
	"path/filepath"
)

type onboardingConfig struct {
	OnboardingCompleted bool `json:"onboarding_completed"`
}

func legacyOnboardingConfigPath() (string, error) {
	configDir, err := ZeriConfigDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(configDir, "config.json"), nil
}

func legacyOnboardingCompleted() bool {
	path, err := legacyOnboardingConfigPath()
	if err != nil {
		return false
	}
	payload, err := os.ReadFile(path)
	if err != nil {
		return false
	}
	var cfg onboardingConfig
	if err = json.Unmarshal(payload, &cfg); err != nil {
		return false
	}
	return cfg.OnboardingCompleted
}

func saveOnboardingCompleted() error {
	return SetOnboardingCompleted(true)
}

func initializeDataLocation(noOnboarding bool) error {
	_, ok, err := ResolveDataRoot()
	if err != nil {
		return err
	}
	if ok {
		return ensureZeriDirectories()
	}
	if legacyInstallPresent() || noOnboarding {
		home, homeErr := ConfigHomeDir()
		if homeErr != nil {
			return homeErr
		}
		if err := AdoptDataRoot(home); err != nil {
			return err
		}
		return ensureZeriDirectories()
	}
	return nil
}

func legacyInstallPresent() bool {
	home, err := ConfigHomeDir()
	if err != nil {
		return false
	}
	for _, sub := range []string{"config", "scripts", "sessions"} {
		entries, readErr := os.ReadDir(filepath.Join(home, sub))
		if readErr == nil && len(entries) > 0 {
			return true
		}
	}
	return false
}

func migrateLegacyOnboardingFlag() bool {
	completed, err := OnboardingCompleted()
	if err == nil && completed {
		return true
	}
	if _, ok, resolveErr := ResolveDataRoot(); resolveErr != nil || !ok {
		return false
	}
	if !legacyOnboardingCompleted() {
		return false
	}
	_ = SetOnboardingCompleted(true)
	return true
}

func needsOnboarding(skip bool) bool {
	if skip {
		return false
	}
	if _, ok, err := ResolveDataRoot(); err == nil && !ok {
		return true
	}
	completed, err := OnboardingCompleted()
	if err != nil {
		return false
	}
	if completed {
		return false
	}
	if migrateLegacyOnboardingFlag() {
		return false
	}
	return true
}

/*
onboarding_state.go

First-run completion state is consolidated into the single canonical
location.json pointer (ConfigHomeDir) through the persistence helpers
OnboardingCompleted / SetOnboardingCompleted. The legacy marker that used to
live at <dataRoot>/config/config.json is read once, only as a migration source:
when an existing install already recorded OnboardingCompleted=true there,
migrateLegacyOnboardingFlag copies the flag into location.json so upgrading
users are not forced back through onboarding.

needsOnboarding is deterministic and isolated: first run is true when the
pointer is absent, or when onboarding_completed is false and no legacy
completion marker can be migrated. It no longer inspects the script registry.
*/
