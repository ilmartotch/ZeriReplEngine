//go:build windows

package main


func installersForCurrentPlatform(runtime RuntimeDefinition) []RuntimeInstaller {
	if installers, ok := runtime.Installers["windows"]; ok {
		return installers
	}
	if installers, ok := runtime.Installers["all"]; ok {
		return installers
	}
   return nil
}

func commandForInstaller(installer RuntimeInstaller) (InstallCommand, bool) {
	switch installer.Manager {
	case "winget":
		return InstallCommand{
			Executable: "winget",
			Args: []string{"install", "--id", installer.Package, "-e", "--silent", "--accept-package-agreements", "--accept-source-agreements"},
		}, true
	case "choco":
		return InstallCommand{Executable: "choco", Args: []string{"install", installer.Package, "-y"}}, true
	case "scoop":
		return InstallCommand{Executable: "scoop", Args: []string{"install", installer.Package}}, true
	default:
		return InstallCommand{}, false
	}
}

/*
bootstrap_manager_windows.go
Provides deterministic Windows installer command resolution from the runtime manifest.
*/
