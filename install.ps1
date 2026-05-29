param(
    [switch]$Force,
    [switch]$System,
    [switch]$Uninstall,
    [string]$BinPath,
    [string]$DataPath
)

<#
USAGE
  Direct invocation (recommended):
    .\install.ps1 [-Force] [-Uninstall] -BinPath "<dir>" -DataPath "<dir>"

  One-liner install:
    irm https://github.com/ilmartotch/ZeriReplEngine/releases/latest/download/install.ps1 | iex

  One-liner uninstall (parameters cannot be passed via iex; use scriptblock syntax):
    & ([scriptblock]::Create((irm https://github.com/ilmartotch/ZeriReplEngine/releases/latest/download/install.ps1))) -Uninstall -BinPath "<dir>"
#>

$ErrorActionPreference = "Stop"
$Repo = "ilmartotch/ZeriReplEngine"
$PathTarget = "User"

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-IsInteractiveSession {
    try {
        return -not [Console]::IsInputRedirected -and -not [Console]::IsOutputRedirected
    } catch {
        return $Host.Name -notlike "*ServerRemoteHost*"
    }
}

function Resolve-RequiredPath {
    param(
        [string]$CurrentValue,
        [string]$PromptMessage,
        [string]$ParameterName
    )

    if (-not [string]::IsNullOrWhiteSpace($CurrentValue)) {
        return [System.IO.Path]::GetFullPath($CurrentValue)
    }

    if (Test-IsInteractiveSession) {
        $entered = Read-Host $PromptMessage
        if ([string]::IsNullOrWhiteSpace($entered)) {
            throw "Missing required path for -$ParameterName."
        }
        return [System.IO.Path]::GetFullPath($entered)
    }

    throw "Non-interactive execution requires -$ParameterName."
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

if ($System) {
    Write-Warning "-System is deprecated and ignored. PATH is always updated at user scope."
}

$BinDir = Resolve-RequiredPath -CurrentValue $BinPath -PromptMessage "Enter binary installation path (directory for zeri.exe):" -ParameterName "BinPath"
$ManifestFile = Join-Path $BinDir "zeri-manifest.json"

if ($Uninstall) {
    if (-not (Test-Path $ManifestFile)) {
        throw "No Zeri installation found at $BinDir."
    }

    $manifest = Get-Content $ManifestFile -Raw | ConvertFrom-Json
    if (-not [string]::IsNullOrWhiteSpace($DataPath)) {
        $UserDataDir = [System.IO.Path]::GetFullPath($DataPath)
    } elseif ($manifest.PSObject.Properties.Name -contains "user_data_dir" -and -not [string]::IsNullOrWhiteSpace($manifest.user_data_dir)) {
        $UserDataDir = [System.IO.Path]::GetFullPath([string]$manifest.user_data_dir)
    } else {
        $UserDataDir = Resolve-RequiredPath -CurrentValue "" -PromptMessage "Enter user data path to remove on uninstall:" -ParameterName "DataPath"
    }

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

    Remove-PathEntry -PathToRemove $BinDir -Target $PathTarget
    Write-Host "Zeri has been completely uninstalled."
    return
}

$UserDataDir = Resolve-RequiredPath -CurrentValue $DataPath -PromptMessage "Enter user data path (sessions/scripts storage):" -ParameterName "DataPath"

Write-Host "Installing Zeri..."
$release = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest"
$latestVersion = $release.tag_name

if (Test-Path $ManifestFile) {
    $existingManifest = Get-Content $ManifestFile -Raw | ConvertFrom-Json
    if ($existingManifest.version -eq $latestVersion -and -not $Force) {
        Write-Host "Zeri $latestVersion is already installed. Use -Force to reinstall."
        return
    }
}

if ($Force) {
    $confirmation = Read-Host "Warning: reinstalling may affect custom commands, variables, and saved sessions. Proceed? [y/N]"
    if ($confirmation -ne "y") {
        return
    }
}

$asset = $release.assets | Where-Object { $_.name -match "windows" } | Select-Object -First 1
if (-not $asset) {
    Write-Error "No Windows release asset found for $Repo."
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
        }
    }

    New-Item -ItemType Directory -Path (Join-Path $UserDataDir "sessions") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $UserDataDir "scripts") -Force | Out-Null

    $pathModified = Add-PathEntry -PathToAdd $BinDir -Target $PathTarget

    $manifest = [ordered]@{
        version = $latestVersion
        installed_at = (Get-Date).ToString("o")
        bin_dir = $BinDir
        user_data_dir = $UserDataDir
        system_install = $false
        path_target = $PathTarget
        requested_system_install = [bool]$System
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

Issues 1+2 (irm | iex compatibility):
- All `exit 0` replaced with `return` so the PowerShell host process is NOT terminated when
  the script is piped through Invoke-Expression (irm URL | iex). The terminal stays open.
- All `exit 1` after Write-Error removed: with $ErrorActionPreference="Stop", Write-Error
  already throws a terminating error — exit 1 was unreachable dead code.
- Uninstall "not found" case now uses `throw` instead of Write-Host+exit so the error is
  visible as a proper PowerShell error in all invocation contexts.
- USAGE comment block added at the top documenting the correct irm|iex one-liner and the
  scriptblock syntax required to pass -Uninstall when piping from the web.
- Fixed `-Force` hint: was "--force" (bash style), now "-Force" (correct PowerShell style).
#>
