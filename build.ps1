param([string]$Config = "Release")
$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Dist = Join-Path $Root "dist"
$toolchainPath = $null

# --- Pre-flight checks ---
function Assert-Command($name, $hint) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        Write-Host "ERRORE: '$name' non trovato in PATH." -ForegroundColor Red
        Write-Host "        $hint" -ForegroundColor Yellow
        exit 1
    }
}
Assert-Command "cmake" "Installa CMake da https://cmake.org/download/ e aggiungilo al PATH."
Assert-Command "go"    "Installa Go da https://go.dev/dl/ e aggiungilo al PATH."
Assert-Command "git"   "Installa Git da https://git-scm.com/ (richiesto da vcpkg)."

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
    throw "Toolchain vcpkg non trovata. Imposta VCPKG_ROOT o clona vcpkg in $root\vcpkg."
}

# --- VS Environment Setup ---
# MSVC (cl.exe) requires the VC Developer environment to resolve STL headers.
# We use vswhere to find the latest VS installation and Enter-VsDevShell to activate it.
$vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWhere)) {
    $vsWhere = Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe"
}
if (-not (Test-Path $vsWhere)) {
    throw "vswhere.exe non trovato. Installa Visual Studio Build Tools."
}
$vsPath = & $vsWhere -latest -property installationPath
$devShellDll = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"

# Se cl.exe è già nel PATH (es. ilammy/msvc-dev-cmd in CI), salta VsDevShell.
if (Get-Command "cl.exe" -ErrorAction SilentlyContinue) {
    Write-Host "VS environment già attivo (cl.exe trovato in PATH), salto VsDevShell."
} else {
    if (-not (Test-Path $devShellDll)) {
        throw "Microsoft.VisualStudio.DevShell.dll non trovato in: $devShellDll"
    }
    Import-Module $devShellDll -ErrorAction Stop
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -Arch x64 | Out-Null
    Write-Host "VS environment attivo: $vsPath"
}
# ---

$toolchainPath = Resolve-VcpkgToolchain $Root $vsPath

Write-Host "Pulizia e creazione dist/"
if (Test-Path $Dist) { Remove-Item $Dist -Recurse -Force }
New-Item -ItemType Directory -Path $Dist | Out-Null
New-Item -ItemType Directory -Path (Join-Path $Dist "runtime") | Out-Null

Write-Host "Build ZeriEngine (C++, $Config)"
$BuildDir = Join-Path $Root "build-release"
cmake --fresh -B $BuildDir -S $Root "-DCMAKE_BUILD_TYPE=$Config" "-DVCPKG_TARGET_TRIPLET=x64-windows" "-DCMAKE_TOOLCHAIN_FILE=$toolchainPath"
if ($LASTEXITCODE -ne 0) { throw "CMake configure fallito." }
cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "CMake build fallito." }
$EngineSource = Join-Path (Join-Path (Join-Path $Root "build-release") $Config) "ZeriEngine.exe"
if (-not (Test-Path $EngineSource)) {
    # Ninja single-config: binary is directly in build dir, not in a Config subfolder
    $EngineSource = Join-Path $BuildDir "ZeriEngine.exe"
}
Copy-Item $EngineSource -Destination $Dist

# Copy vcpkg runtime DLLs required by ZeriEngine
$VcpkgBin = Join-Path $BuildDir "vcpkg_installed"
$VcpkgBin = Join-Path $VcpkgBin "x64-windows"
$VcpkgBin = Join-Path $VcpkgBin "bin"
if (Test-Path $VcpkgBin) {
    Get-ChildItem $VcpkgBin -Filter "*.dll" | ForEach-Object {
        Copy-Item $_.FullName -Destination $Dist
        Write-Host "Copiata DLL: $($_.Name)"
    }
} else {
    Write-Warning "vcpkg bin non trovato in $VcpkgBin DLL non copiate."
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
            Write-Host "Copiata runtime MSVC: $($_.Name)"
        }
    } else {
        Write-Warning "Cartella runtime MSVC x64 CRT non trovata sotto $MsvcRedistRoot"
    }
} else {
    Write-Warning "Directory redist MSVC non trovata: $MsvcRedistRoot"
}

Write-Host "Build TUI Go (zeri.exe)"
$YuumiUi = Join-Path $Root "ui"
Push-Location $YuumiUi
$env:GOOS = "windows"; $env:GOARCH = "amd64"
go build -o (Join-Path $Dist "zeri.exe") ./cmd/zeri-tui/
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "go build TUI fallito." }
Pop-Location

Write-Host "Copia runtime sidecar"
$RuntimeSrc = Join-Path $Root "src"
$RuntimeSrc = Join-Path $RuntimeSrc "ZeriLink"
$RuntimeSrc = Join-Path $RuntimeSrc "Runtime"
Copy-Item (Join-Path $RuntimeSrc "*") -Destination (Join-Path $Dist "runtime") -Recurse

$ManifestSrc = Join-Path $Root "runtime\runtime_manifest.json"
if (Test-Path $ManifestSrc) {
    Copy-Item $ManifestSrc -Destination (Join-Path $Dist "runtime")
    Write-Host "Copiato runtime_manifest.json"
} else {
    Write-Warning "runtime\runtime_manifest.json non trovato in $Root"
}

Write-Host ""
Write-Host "Build complete."
Get-ChildItem $Dist -Recurse | ForEach-Object { Write-Host $_.FullName }