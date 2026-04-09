#!/usr/bin/env bash
# build.sh — Release build for macOS and Linux (mirrors build.ps1)
# Usage: ./build.sh [Debug|Release]
set -euo pipefail

CONFIG="${1:-Release}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST="$SCRIPT_DIR/dist"
BUILD_DIR="$SCRIPT_DIR/build-release"
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

if [ -z "${VCPKG_ROOT:-}" ]; then
    if [ -d "$SCRIPT_DIR/vcpkg" ]; then
        export VCPKG_ROOT="$SCRIPT_DIR/vcpkg"
    else
        echo "ERRORE: VCPKG_ROOT non impostato e vcpkg locale non trovato."
        echo "        Esegui: export VCPKG_ROOT=/path/to/vcpkg"
        exit 1
    fi
fi

# --- Pulizia dist ---
echo "Pulizia e creazione dist/"
rm -rf "$DIST"
mkdir -p "$DIST/runtime"

# --- Build ZeriEngine (C++) ---
echo "Build ZeriEngine (C++, $CONFIG)"
if [ "$(uname -s)" = "Darwin" ]; then
    if [ "${CMAKE_OSX_ARCHITECTURES:-}" = "arm64" ]; then
        VCPKG_TRIPLET="arm64-osx"
    else
        VCPKG_TRIPLET="x64-osx"
    fi
else
    VCPKG_TRIPLET="x64-linux"
fi

cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE="$CONFIG" -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET"
cmake --build "$BUILD_DIR" --config "$CONFIG"

ENGINE_SRC="$BUILD_DIR/ZeriEngine"
if [ ! -f "$ENGINE_SRC" ]; then
    ENGINE_SRC="$BUILD_DIR/$CONFIG/ZeriEngine"
fi
if [ ! -f "$ENGINE_SRC" ]; then
    echo "ERRORE: ZeriEngine non trovato dopo la build."
    exit 1
fi
cp "$ENGINE_SRC" "$DIST/ZeriEngine"
chmod +x "$DIST/ZeriEngine"

# Copy vcpkg runtime shared libraries required by ZeriEngine
VCPKG_LIB="$BUILD_DIR/vcpkg_installed/$VCPKG_TRIPLET/lib"
if [ -d "$VCPKG_LIB" ]; then
    find "$VCPKG_LIB" -name "*.so*" -o -name "*.dylib" | while read -r lib; do
        cp "$lib" "$DIST/"
        echo "  Copiata lib: $(basename "$lib")"
    done
fi

# --- Build TUI Go ---
echo "Build TUI Go (zeri)"
cd "$YUUMI_UI_DIR"
go build -o "$DIST/zeri" ./cmd/zeri-tui/
chmod +x "$DIST/zeri"

# --- Copia runtime sidecar ---
echo "Copia runtime sidecar"
RUNTIME_SRC="$SCRIPT_DIR/src/ZeriLink/Runtime"
if [ -d "$RUNTIME_SRC" ]; then
    cp -r "$RUNTIME_SRC/." "$DIST/runtime/"
fi

echo ""
echo "=== Build completata ==="
find "$DIST" -type f | sort
