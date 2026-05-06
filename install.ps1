param(
    [switch]$Force,
    [switch]$System,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"
$Repo = "ilmartotch/ReplZeriEmgine"
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
        $targetFile = Join-Path $BinDir $fileName
        if (Test-Path $targetFile) {
            Remove-Item $targetFile -Force
        }
    }

    $runtimeDir = Join-Path $BinDir "Runtime"
    if (Test-Path $runtimeDir) {
        Remove-Item $runtimeDir -Recurse -Force
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

    $zeriExe = Get-ChildItem $stagingDir -Recurse -File -Filter "zeri.exe" | Select-Object -First 1
    $engineExe = Get-ChildItem $stagingDir -Recurse -File -Filter "zeri-engine.exe" | Select-Object -First 1
    $runtimeSource = Get-ChildItem $stagingDir -Recurse -Directory | Where-Object { $_.Name -eq "Runtime" } | Select-Object -First 1

    if (-not $zeriExe -or -not $engineExe -or -not $runtimeSource) {
        Write-Error "Release package is missing required files (zeri.exe, zeri-engine.exe, Runtime)."
        exit 1
    }

    Copy-Item $zeriExe.FullName -Destination (Join-Path $BinDir "zeri.exe") -Force
    Copy-Item $engineExe.FullName -Destination (Join-Path $BinDir "zeri-engine.exe") -Force
    Copy-Item $runtimeSource.FullName -Destination (Join-Path $BinDir "Runtime") -Recurse -Force

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
        files_installed = @("zeri.exe", "zeri-engine.exe")
        dirs_created = @("Runtime")
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
Install script now supports install, force reinstall, system-wide install, and complete uninstall.
It separates binary deployment from user data, writes a manifest with installation metadata,
maintains PATH for user or machine scope, and preserves controlled prompts for destructive actions.
Release assets are fetched from GitHub, staged in a temporary directory, and only required runtime files
are copied into the binary directory while user sessions/scripts directories are created under APPDATA.
#>
