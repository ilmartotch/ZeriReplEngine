package main

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"strings"
)

type onboardingConfig struct {
	OnboardingCompleted bool `json:"onboarding_completed"`
}

func onboardingConfigPath() (string, error) {
	configDir, err := ZeriConfigDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(configDir, "config.json"), nil
}

func loadOnboardingConfig() (onboardingConfig, error) {
	path, err := onboardingConfigPath()
	if err != nil {
		return onboardingConfig{}, err
	}
	payload, err := os.ReadFile(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return onboardingConfig{}, nil
		}
		return onboardingConfig{}, err
	}
	var cfg onboardingConfig
	if err = json.Unmarshal(payload, &cfg); err != nil {
		return onboardingConfig{}, err
	}
	return cfg, nil
}

func saveOnboardingCompleted() error {
	path, err := onboardingConfigPath()
	if err != nil {
		return err
	}
	if err = os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	payload, err := json.MarshalIndent(onboardingConfig{OnboardingCompleted: true}, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, payload, 0o644)
}

func scriptRegistryIsEmpty() bool {
	scriptsDir, dirErr := ZeriScriptsDir("python")
	if dirErr != nil {
		return true
	}
	entries, readErr := os.ReadDir(filepath.Dir(scriptsDir))
	if readErr != nil {
		return true
	}
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		name := strings.TrimSpace(entry.Name())
		if name == "" {
			continue
		}
		dirEntries, nestedErr := os.ReadDir(filepath.Join(filepath.Dir(scriptsDir), name))
		if nestedErr != nil {
			continue
		}
		if len(dirEntries) > 0 {
			return false
		}
	}
	return true
}

func needsOnboarding(skip bool) bool {
	if skip {
		return false
	}
	cfg, err := loadOnboardingConfig()
	if err != nil {
		return false
	}
	if cfg.OnboardingCompleted {
		return false
	}
	return scriptRegistryIsEmpty()
}
