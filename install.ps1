param(
    [switch]$Force,
    [switch]$System,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"
$Repo = "ilmartotch/ZeriReplEngine"
$BinDir = if ($System) { "C:\Program Files\Zeri" } else { "$env:LOCALAPPDATA\Zeri" }
$UserDataDir = "$env:APPDATA\Zeri"
$ManifestFile = "$BinDir\zeri-manifest.json"

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Add-PathEntry {
    param(
        [string]$PathToAdd,
        [string]$Target
    )

    $existing = [Environment]::GetEnvironmentVariable("PATH", $Target)
    if ([string]::IsNullOrWhiteSpace($existing)) {
        [Environment]::SetEnvironmentVariable("PATH", $PathToAdd, $Target)
        return $true
    }

    $parts = $existing.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries)
    if ($parts -contains $PathToAdd) {
        return $false
    }

    [Environment]::SetEnvironmentVariable("PATH", "$existing;$PathToAdd", $Target)
    return $true
}

function Remove-PathEntry {
    param(
        [string]$PathToRemove,
        [string]$Target
    )

    $existing = [Environment]::GetEnvironmentVariable("PATH", $Target)
    if ([string]::IsNullOrWhiteSpace($existing)) {
        return
    }

    $parts = $existing.Split(';', [System.StringSplitOptions]::RemoveEmptyEntries) |
        Where-Object { $_ -ne $PathToRemove }
    [Environment]::SetEnvironmentVariable("PATH", ($parts -join ';'), $Target)
}

if ($System -and -not (Test-IsAdministrator)) {
    Write-Error "System installation requires administrator privileges. Run this script as Administrator."
    exit 1
}

if ($Uninstall) {
    if (-not (Test-Path $ManifestFile)) {
        Write-Host "No Zeri installation found at $BinDir."
        exit 1
    }

    $manifest = Get-Content $ManifestFile -Raw | ConvertFrom-Json
    foreach ($fileName in $manifest.files_installed) {
        $targetFile = Join-Path $BinDir ($fileName.Replace('/', '\'))
        if (Test-Path $targetFile) {
            Remove-Item $targetFile -Force
        }
    }

    foreach ($dirName in $manifest.dirs_created) {
        $targetDir = Join-Path $BinDir ($dirName.Replace('/', '\'))
        if (Test-Path $targetDir) {
            Remove-Item $targetDir -Recurse -Force
        }
    }

    if (Test-Path $ManifestFile) {
        Remove-Item $ManifestFile -Force
    }

    $versionFile = Join-Path $BinDir "version.txt"
    if (Test-Path $versionFile) {
        Remove-Item $versionFile -Force
    }

    if (Test-Path $BinDir) {
        $remaining = Get-ChildItem $BinDir -Force
        if ($remaining.Count -eq 0) {
            Remove-Item $BinDir -Force
        }
    }

    $deleteUserData = Read-Host "This will permanently delete all user data in $UserDataDir. Are you sure? [y/N]"
    if ($deleteUserData -eq "y") {
        if (Test-Path $UserDataDir) {
            Remove-Item $UserDataDir -Recurse -Force
        }
    }

    $pathTarget = if ($System) { "Machine" } else { "User" }
    Remove-PathEntry -PathToRemove $BinDir -Target $pathTarget
    Write-Host "Zeri has been completely uninstalled."
    exit 0
}

Write-Host "Installing Zeri..."
$release = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest"
$latestVersion = $release.tag_name

if (Test-Path $ManifestFile) {
    $existingManifest = Get-Content $ManifestFile -Raw | ConvertFrom-Json
    if ($existingManifest.version -eq $latestVersion -and -not $Force) {
        Write-Host "Zeri $latestVersion is already installed. Use --force to reinstall."
        exit 0
    }
}

if ($Force) {
    $confirmation = Read-Host "Warning: reinstalling may affect custom commands, variables, and saved sessions. Proceed? [y/N]"
    if ($confirmation -ne "y") {
        exit 0
    }
}

$asset = $release.assets | Where-Object { $_.name -match "windows" } | Select-Object -First 1
if (-not $asset) {
    Write-Error "No Windows release asset found for $Repo."
    exit 1
}

$tempRoot = Join-Path $env:TEMP ("zeri-install-" + [Guid]::NewGuid().ToString("N"))
$tmpZip = Join-Path $tempRoot "zeri-install.zip"
$stagingDir = Join-Path $tempRoot "staging"
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

try {
    Write-Host "Downloading $($asset.browser_download_url)"
    Invoke-WebRequest $asset.browser_download_url -OutFile $tmpZip
    Expand-Archive $tmpZip -DestinationPath $stagingDir -Force

    New-Item -ItemType Directory -Path $BinDir -Force | Out-Null

    $installManifestFilename = "install_manifest.json"
    $manifestSchemaVersion = 1
    $archiveManifestFile = Get-ChildItem -Path $stagingDir -Recurse -File -Filter $installManifestFilename |
        Select-Object -First 1
    if (-not $archiveManifestFile) {
        Write-Error "$installManifestFilename not found in release archive. Cannot install."
        exit 1
    }
    $archiveRoot = $archiveManifestFile.DirectoryName
    $archiveManifest = Get-Content $archiveManifestFile.FullName -Raw | ConvertFrom-Json
    if ($archiveManifest.version -ne $manifestSchemaVersion) {
        Write-Warning "Manifest version $($archiveManifest.version) != expected $manifestSchemaVersion. Proceeding."
    }

    $installedFiles = @()
    $createdDirsSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($asset in $archiveManifest.assets) {
        $srcPath = Join-Path $archiveRoot ($asset.src.Replace('/', '\'))
        $destDir = if ($asset.dest -eq ".") { $BinDir } else { Join-Path $BinDir ($asset.dest.Replace('/', '\')) }
        if (-not (Test-Path $destDir)) {
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null
        }
        if (Test-Path $srcPath) {
            Copy-Item -Path $srcPath -Destination $destDir -Force
            $installedFiles += $asset.src
            if ($asset.dest -ne ".") { $createdDirsSet.Add($asset.dest) | Out-Null }
            Write-Verbose "Installed: $($asset.src) -> $destDir"
        } else {
            Write-Warning "Asset not found in archive: $($asset.src)"
        }
    }
    $createdDirs = @($createdDirsSet)

    $criticalFiles = @(
        (Join-Path $BinDir "zeri.exe"),
        (Join-Path $BinDir "zeri-engine.exe"),
        (Join-Path $BinDir "help\help_catalog.json"),
        (Join-Path $BinDir "runtime\runtime_manifest.json")
    )
    foreach ($f in $criticalFiles) {
        if (-not (Test-Path $f)) {
            Write-Error "Critical file missing after install: $f"
            exit 1
        }
    }

    New-Item -ItemType Directory -Path (Join-Path $UserDataDir "sessions") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $UserDataDir "scripts") -Force | Out-Null

    $pathTarget = if ($System) { "Machine" } else { "User" }
    $pathModified = Add-PathEntry -PathToAdd $BinDir -Target $pathTarget

    $manifest = [ordered]@{
        version = $latestVersion
        installed_at = (Get-Date).ToString("o")
        bin_dir = $BinDir
        user_data_dir = $UserDataDir
        system_install = [bool]$System
        path_modified = [bool]$pathModified
        files_installed = $installedFiles
        dirs_created = $createdDirs
    }

    $manifestJson = $manifest | ConvertTo-Json -Depth 4
    Set-Content -Path $ManifestFile -Value $manifestJson -Encoding UTF8

    Write-Host "Installation completed successfully."
} finally {
    if (Test-Path $tempRoot) {
        Remove-Item $tempRoot -Recurse -Force
    }
}

<#
--- CHANGES ---
A1/A2 (Prompt 01): Added DLL and help\ copy blocks — superseded by manifest-driven approach below.
Prompt 02: Replaced all hardcoded copy logic (zeri.exe, zeri-engine.exe, Runtime\, *.dll, help\)
with a manifest-driven loop that reads install_manifest.json from the release archive.
- $installManifestFilename / $manifestSchemaVersion: constants for the archive manifest.
- $archiveRoot: derived from the manifest's location in the extracted archive tree, so the
  install works regardless of whether the ZIP has a top-level directory.
- $installedFiles: dynamic list of all copied src paths fed into files_installed in the tracking
  manifest, so uninstall correctly removes every file regardless of future build additions.
- $createdDirs: unique set of dest subdirectories, fed into dirs_created for uninstall cleanup.
- Uninstall updated: files_installed loop calls .Replace('/','\') for path safety;
  hardcoded Runtime+help removal replaced by a dirs_created foreach loop.
- Critical-file verification added post-copy for the four files required for a working install.
#>

