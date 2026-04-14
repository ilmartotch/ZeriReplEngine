//go:build !windows

package main

import "runtime"


func installersForCurrentPlatform(runtime RuntimeDefinition) []RuntimeInstaller {
	platformKey := "linux"
	if isDarwin() {
		platformKey = "darwin"
	}

	if installers, ok := runtime.Installers[platformKey]; ok {
		return installers
	}
	if installers, ok := runtime.Installers["all"]; ok {
		return installers
	}
	return nil
}

func commandForInstaller(installer RuntimeInstaller) (InstallCommand, bool) {
	switch installer.Manager {
	case "brew":
		return InstallCommand{Executable: "brew", Args: []string{"install", installer.Package}}, true
	case "apt-get":
		return InstallCommand{Executable: "apt-get", Args: []string{"install", "-y", installer.Package}}, true
	case "dnf":
		return InstallCommand{Executable: "dnf", Args: []string{"install", "-y", installer.Package}}, true
	case "yum":
		return InstallCommand{Executable: "yum", Args: []string{"install", "-y", installer.Package}}, true
	case "pacman":
		return InstallCommand{Executable: "pacman", Args: []string{"-S", "--noconfirm", installer.Package}}, true
	case "zypper":
		return InstallCommand{Executable: "zypper", Args: []string{"--non-interactive", "install", installer.Package}}, true
	case "apk":
		return InstallCommand{Executable: "apk", Args: []string{"add", "--no-cache", installer.Package}}, true
	default:
      return InstallCommand{}, false
	}
}

func isDarwin() bool {
  return runtime.GOOS == "darwin"
}

/*
bootstrap_manager_other.go
Provides deterministic Linux/macOS installer command resolution from the runtime manifest.
*/
