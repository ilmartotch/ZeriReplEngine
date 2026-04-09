#!/usr/bin/env bash
# dev.sh — Dev workflow for macOS and Linux (mirrors dev.ps1)
# Usage: ./dev.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
YUUMI_UI_DIR="$SCRIPT_DIR/ui"

# --- Pre-flight checks ---
check_cmd() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERRORE: '$1' non trovato in PATH."
        echo "        $2"
        exit 1
    fi
}
check_cmd cmake "Installa CMake: https://cmake.org/download/"
check_cmd go    "Installa Go: https://go.dev/dl/"
check_cmd git   "Installa Git: https://git-scm.com/ (richiesto da vcpkg)"
check_cmd cc    "Installa un compilatore C++: 'sudo apt install build-essential' oppure 'xcode-select --install' su macOS"

# Verifica vcpkg disponibile (locale o tramite VCPKG_ROOT)
if [ -z "${VCPKG_ROOT:-}" ]; then
    if [ -d "$SCRIPT_DIR/vcpkg" ]; then
        export VCPKG_ROOT="$SCRIPT_DIR/vcpkg"
    else
        echo "ERRORE: VCPKG_ROOT non impostato e vcpkg locale non trovato."
        echo "        Esegui: export VCPKG_ROOT=/path/to/vcpkg"
        exit 1
    fi
fi

echo "[1/4] Configuring CMake project..."
if [ "$(uname -s)" = "Darwin" ]; then
    if [ "${CMAKE_OSX_ARCHITECTURES:-}" = "arm64" ]; then
        VCPKG_TRIPLET="arm64-osx"
    else
        VCPKG_TRIPLET="x64-osx"
    fi
else
    VCPKG_TRIPLET="x64-linux"
fi
cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Debug -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET"

echo "[2/4] Building ZeriEngine (Debug)..."
cmake --build "$BUILD_DIR" --config Debug

ENGINE_PATH="$BUILD_DIR/ZeriEngine"
if [ ! -f "$ENGINE_PATH" ]; then
    echo "ERRORE: ZeriEngine non trovato in $ENGINE_PATH"
    exit 1
fi

export ZERI_ENGINE_PATH="$ENGINE_PATH"
echo "[3/4] ZERI_ENGINE_PATH=$ZERI_ENGINE_PATH"

echo "[4/4] Running TUI from: $YUUMI_UI_DIR"
cd "$YUUMI_UI_DIR"
go run ./cmd/zeri-tui/
