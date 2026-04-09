$ErrorActionPreference = "Stop"

$projectRoot = $PSScriptRoot
$buildDir    = Join-Path $projectRoot "build"
$enginePath  = Join-Path (Join-Path $buildDir "Debug") "ZeriEngine.exe"
$yuumiUiDir  = Join-Path $projectRoot "ui"
$toolchainPath = $null
$vsPath = $null

# --- Pre-flight checks ---
function Assert-Command($name, $hint) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        Write-Host "ERRORE: '$name' non trovato in PATH." -ForegroundColor Red
        Write-Host "        $hint" -ForegroundColor Yellow
        exit 1
    }
}
Assert-Command "cmake"  "Installa CMake da https://cmake.org/download/ e aggiungilo al PATH."
Assert-Command "go"     "Installa Go da https://go.dev/dl/ e aggiungilo al PATH."
Assert-Command "git"    "Installa Git da https://git-scm.com/ (richiesto da vcpkg)."

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
$vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWhere)) {
    $vsWhere = Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe"
}
if (Test-Path $vsWhere) {
    $vsPath       = & $vsWhere -latest -property installationPath
    $devShellDll  = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    if (Test-Path $devShellDll) {
        Import-Module $devShellDll -ErrorAction SilentlyContinue
        Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -Arch x64 | Out-Null
        Write-Host "[0/4] VS environment attivo: $vsPath"
    }
}
# ---

$toolchainPath = Resolve-VcpkgToolchain $projectRoot $vsPath

Write-Host "[1/4] Configuring CMake project..."
cmake --fresh -B $buildDir -S $projectRoot "-DCMAKE_BUILD_TYPE=Debug" "-DVCPKG_TARGET_TRIPLET=x64-windows" "-DCMAKE_TOOLCHAIN_FILE=$toolchainPath"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

Write-Host "[2/4] Building ZeriEngine (Debug)..."
cmake --build $buildDir --config Debug
if ($LASTEXITCODE -ne 0) { throw "CMake build failed." }

if (-not (Test-Path $enginePath)) {
    # Ninja single-config: binary might be directly in build dir
    $enginePath = Join-Path $buildDir "ZeriEngine.exe"
    if (-not (Test-Path $enginePath)) {
        throw "Built executable not found."
    }
}

$resolvedEnginePath = (Resolve-Path $enginePath).Path
$env:ZERI_ENGINE_PATH = $resolvedEnginePath

Write-Host "[3/4] ZERI_ENGINE_PATH set to: $resolvedEnginePath"

Write-Host "[4/4] Running TUI from: $yuumiUiDir"
Push-Location $yuumiUiDir
try {
    go run ./cmd/zeri-tui/
    if ($LASTEXITCODE -ne 0) { throw "go run failed." }
}
finally {
    Pop-Location
}
