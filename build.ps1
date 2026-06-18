param([string]$Config = "Release")
$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Dist = Join-Path $Root "dist"
$toolchainPath = $null

# Pre-flight checks
function Assert-Command($name, $hint) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        Write-Host "ERROR: '$name' was not found in PATH." -ForegroundColor Red
        Write-Host " $hint" -ForegroundColor Yellow
        exit 1
    }
}
Assert-Command "cmake" "Install CMake from https://cmake.org/download/ and add it to PATH."
Assert-Command "go" "Install Go from https://go.dev/dl/ and add it to PATH."
Assert-Command "git" "Install Git from https://git-scm.com/ (required by vcpkg)."

function Resolve-VcpkgToolchain([string]$root, [string]$vsInstallPath) {
    $candidates = @()
    if ($env:VCPKG_ROOT) {
        $candidates += Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
    }
    $candidates += Join-Path $root "vcpkg\scripts\buildsystems\vcpkg.cmake"
    if ($vsInstallPath) {
        $candidates += Join-Path $vsInstallPath "VC\vcpkg\scripts\buildsystems\vcpkg.cmake"
    }
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    throw "vcpkg toolchain was not found. Set VCPKG_ROOT or clone vcpkg into $root\vcpkg."
}
$vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWhere)) {
    $vsWhere = Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe"
}
if (-not (Test-Path $vsWhere)) {
    throw "vswhere.exe was not found. Install Visual Studio Build Tools."
}
$vsPath = & $vsWhere -latest -property installationPath
$devShellDll = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"

# If cl.exe is already in PATH (for example ilammy/msvc-dev-cmd in CI), skip VsDevShell.
if (Get-Command "cl.exe" -ErrorAction SilentlyContinue) {
    Write-Host "Visual Studio environment is already active (cl.exe found in PATH), skipping VsDevShell."
} else {
    if (-not (Test-Path $devShellDll)) {
        throw "Microsoft.VisualStudio.DevShell.dll was not found at: $devShellDll"
    }
    Import-Module $devShellDll -ErrorAction Stop
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -Arch x64 | Out-Null
    Write-Host "Visual Studio environment active: $vsPath"
}

$toolchainPath = Resolve-VcpkgToolchain $Root $vsPath

Write-Host "Preparing dist/"
if (-not (Test-Path $Dist)) {
    New-Item -ItemType Directory -Path $Dist | Out-Null
}

$cleanupTargets = @(
    "runtime",
    "help",
    "version.txt",
    "zeri.exe",
    "zeri-engine.exe",
    "install.ps1"
)
foreach ($target in $cleanupTargets) {
    $path = Join-Path $Dist $target
    if (-not (Test-Path $path)) {
        continue
    }
    try {
        Remove-Item $path -Recurse -Force -ErrorAction Stop
    } catch {
        Write-Warning "Could not remove $path because it is in use. Continuing build and overwriting what is available."
    }
}

if (-not (Test-Path (Join-Path $Dist "runtime"))) {
    New-Item -ItemType Directory -Path (Join-Path $Dist "runtime") | Out-Null
}
if (-not (Test-Path (Join-Path $Dist "help"))) {
    New-Item -ItemType Directory -Path (Join-Path $Dist "help") | Out-Null
}

Write-Host "Building zeri-engine (C++, $Config)"
$BuildDir = Join-Path $Root "build-release"
cmake --fresh -G Ninja -B $BuildDir -S $Root "-DCMAKE_BUILD_TYPE=$Config" "-DVCPKG_TARGET_TRIPLET=x64-windows" "-DCMAKE_TOOLCHAIN_FILE=$toolchainPath"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "CMake build failed." }
$EngineSource = Join-Path (Join-Path (Join-Path $Root "build-release") $Config) "zeri-engine.exe"
if (-not (Test-Path $EngineSource)) {
    # Ninja single-config: binary is directly in build dir, not in a Config subfolder
    $EngineSource = Join-Path $BuildDir "zeri-engine.exe"
}
Copy-Item $EngineSource -Destination $Dist

# Copy vcpkg runtime DLLs required by zeri-engine
$VcpkgBin = Join-Path $BuildDir "vcpkg_installed"
$VcpkgBin = Join-Path $VcpkgBin "x64-windows"
$VcpkgBin = Join-Path $VcpkgBin "bin"
if (-not (Test-Path $VcpkgBin)) {
    $altBin = Get-ChildItem "$BuildDir" -Recurse -Directory -Filter "bin" |
        Where-Object { $_.FullName -match "vcpkg_installed" } |
        Select-Object -First 1
    if ($altBin) {
        $VcpkgBin = $altBin.FullName
        Write-Host "Found vcpkg bin at alternate path: $VcpkgBin"
    } else {
        throw "vcpkg bin directory not found. DLLs cannot be copied. Build aborted."
    }
}

Get-ChildItem $VcpkgBin -Filter "*.dll" | ForEach-Object {
    Copy-Item $_.FullName -Destination $Dist
    Write-Host "Copied DLL: $($_.Name)"
}

# Copy MSVC runtime DLLs if available in Visual Studio redist folder.
$MsvcRedistRoot = Join-Path $vsPath "VC\Redist\MSVC"
if (Test-Path $MsvcRedistRoot) {
    $crtDir = Get-ChildItem $MsvcRedistRoot -Recurse -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' -and $_.FullName -match '\\x64\\' -and $_.FullName -notmatch '\\onecore\\' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($crtDir) {
        Get-ChildItem $crtDir.FullName -Filter "*.dll" | ForEach-Object {
            Copy-Item $_.FullName -Destination $Dist
            Write-Host "Copied MSVC runtime: $($_.Name)"
        }
    } else {
        Write-Warning "MSVC x64 CRT runtime directory was not found under $MsvcRedistRoot"
    }
} else {
    Write-Warning "MSVC redist directory was not found: $MsvcRedistRoot"
}

$Version = "unknown"
$CachePath = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $CachePath) {
    $CacheContent = Get-Content $CachePath -Raw
    $VersionMatch = [regex]::Match($CacheContent, 'ZeriEngine_VERSION:STATIC=([0-9]+\.[0-9]+\.[0-9]+)')
    if ($VersionMatch.Success) {
        $Version = $VersionMatch.Groups[1].Value
    }
}
Set-Content -Path (Join-Path $Dist "version.txt") -Value $Version
Write-Host "Wrote dist/version.txt with version: $Version"

Write-Host "Build TUI Go (zeri.exe)"
$YuumiUi = Join-Path $Root "ui"
Push-Location $YuumiUi
$GoVersion = $env:ZERI_VERSION
if ([string]::IsNullOrWhiteSpace($GoVersion)) {
    $GoVersion = (& git -C $Root describe --tags --always 2>$null)
}
if ([string]::IsNullOrWhiteSpace($GoVersion)) {
    $GoVersion = $Version
}
$GoVersion = $GoVersion.Trim()
if ($GoVersion.StartsWith("v")) {
    $GoVersion = $GoVersion.Substring(1)
}
if ([string]::IsNullOrWhiteSpace($GoVersion)) {
    $GoVersion = "unknown"
}
$env:GOOS = "windows"; $env:GOARCH = "amd64"
go build -ldflags "-X main.version=$GoVersion" -o (Join-Path $Dist "zeri.exe") ./cmd/zeri-tui/
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "go build TUI failed." }
Pop-Location

Write-Host "Copying sidecar runtime"
$RuntimeCandidates = @(
    (Join-Path $Root "runtime"),
    (Join-Path (Join-Path (Join-Path $Root "src") "ZeriLink") "Runtime")
)
$RuntimeSrc = $RuntimeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $RuntimeSrc) {
    throw "Runtime directory was not found. Checked: $($RuntimeCandidates -join ', ')"
}
Copy-Item (Join-Path $RuntimeSrc "*") -Destination (Join-Path $Dist "runtime") -Recurse

$HelpSrc = Join-Path $Root "help"
if (-not (Test-Path $HelpSrc)) {
    throw "help directory not found in repo root. Build aborted."
}
Copy-Item (Join-Path $HelpSrc "*") -Destination (Join-Path $Dist "help") -Recurse

$ManifestSrc = Join-Path $Root "runtime\runtime_manifest.json"
if (Test-Path $ManifestSrc) {
    Copy-Item $ManifestSrc -Destination (Join-Path $Dist "runtime")
    Write-Host "Copied runtime_manifest.json"
} else {
    Write-Warning "runtime\runtime_manifest.json was not found in $Root"
}

# Copy install script into dist
$InstallScript = Join-Path $Root "install.ps1"
if (Test-Path $InstallScript) {
    Copy-Item $InstallScript -Destination (Join-Path $Dist "install.ps1")
    Write-Host "Copied install.ps1 to dist/"
} else {
    throw "install.ps1 not found in repo root. Cannot include in release package."
}

# Copy ZIP-extraction setup script into dist
$SetupScript = Join-Path $Root "setup.ps1"
if (Test-Path $SetupScript) {
    Copy-Item $SetupScript -Destination (Join-Path $Dist "setup.ps1")
    Write-Host "Copied setup.ps1 to dist/"
} else {
    throw "setup.ps1 not found in repo root. Cannot include ZIP PATH setup helper in release package."
}

$installManifestFilename = "install_manifest.json"
$manifestSchemaVersion = 1
$assets = @()
Get-ChildItem -Path $Dist -Recurse -File | ForEach-Object {
    $relPath = $_.FullName.Substring($Dist.Length).TrimStart('\', '/').Replace('\', '/')
    if ($relPath -eq $installManifestFilename) { return }
    $relDir = ($relPath | Split-Path -Parent).Replace('\', '/')
    if ([string]::IsNullOrEmpty($relDir)) { $relDir = "." }
    $assetType = if ($_.Extension -eq ".exe") { "binary" }
                 elseif ($_.Extension -eq ".dll") { "dll" }
                 elseif ($_.Name -match '\.so(\.\d+)*$' -or $_.Extension -eq ".dylib") { "lib" }
                 elseif ([string]::IsNullOrEmpty($_.Extension)) { "binary" }
                 else { "asset" }
    $assets += [ordered]@{
        src = $relPath
        dest = $relDir
        type = $assetType
    }
}
$installManifest = [ordered]@{
    version = $manifestSchemaVersion
    generated_by = "build.ps1"
    assets = $assets
}
$installManifest | ConvertTo-Json -Depth 5 |
    Set-Content -Path (Join-Path $Dist $installManifestFilename) -Encoding UTF8
Write-Host "Generated $installManifestFilename ($($assets.Count) assets)"

$required = @(
    "zeri.exe",
    "zeri-engine.exe",
    "vcruntime140.dll",
    "msvcp140.dll",
    "runtime\runtime_manifest.json",
    "help\help_catalog.json",
    "install_manifest.json"
)
foreach ($f in $required) {
    if (-not (Test-Path (Join-Path $Dist $f))) {
        throw "Required file missing from dist: $f"
    }
}
Write-Host "All required files verified in dist/."

Write-Host ""
Write-Host "Build complete."
Get-ChildItem $Dist -Recurse | ForEach-Object { Write-Host $_.FullName }

<#

Added install_manifest.json generation after all dist/ assets are assembled. The manifest
enumerates every file in dist/ with its src path (relative to dist/ root, forward-slash),
dest directory (relative to install root, "." = root), and type (binary/dll/lib/asset).

Install scripts read this file instead of hardcoding filenames, eliminating the class of
bug where new build assets silently break installation.

- $manifestSchemaVersion: integer version constant for schema forward-compat checks.

- $installManifestFilename: filename constant referenced in generation and verification.

- Added "install_manifest.json" to the required-files verification block so a manifest
  generation failure surfaces as a hard build error immediately.
- Type heuristic: .exe=binary, .dll=dll, .so/.so.N/.dylib=lib, no-ext=binary, else=asset.
#>
