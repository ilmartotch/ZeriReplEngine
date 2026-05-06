#!/usr/bin/env bash
set -euo pipefail

CONFIG="${1:-Release}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST="$SCRIPT_DIR/dist"
BUILD_DIR="$SCRIPT_DIR/build-release"
YUUMI_UI_DIR="$SCRIPT_DIR/ui"

check_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "ERROR: '$1' not found in PATH."
        echo "       $2"
        exit 1
    fi
}

check_cmd cmake "Install CMake."
check_cmd go "Install Go 1.25 or newer."
check_cmd git "Install Git."
check_cmd cc "Install a C/C++ compiler."

if [ -z "${VCPKG_ROOT:-}" ]; then
    if [ -d "$SCRIPT_DIR/vcpkg" ]; then
        export VCPKG_ROOT="$SCRIPT_DIR/vcpkg"
    else
        echo "ERROR: VCPKG_ROOT is not set and local vcpkg was not found."
        exit 1
    fi
fi

echo "Cleaning dist/"
rm -rf "$DIST"
mkdir -p "$DIST/runtime"

if [ "$(uname -s)" = "Darwin" ]; then
    if [ "${CMAKE_OSX_ARCHITECTURES:-}" = "arm64" ]; then
        VCPKG_TRIPLET="arm64-osx"
    else
        VCPKG_TRIPLET="x64-osx"
    fi
else
    VCPKG_TRIPLET="x64-linux"
fi

echo "Building zeri-engine ($CONFIG, $VCPKG_TRIPLET)"
cmake --fresh -B "$BUILD_DIR" -S "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET"

cmake --build "$BUILD_DIR" --config "$CONFIG"

ENGINE_SRC="$BUILD_DIR/zeri-engine"
if [ ! -f "$ENGINE_SRC" ]; then
    ENGINE_SRC="$BUILD_DIR/$CONFIG/zeri-engine"
fi

if [ ! -f "$ENGINE_SRC" ]; then
    echo "ERROR: zeri-engine was not produced by the CMake build."
    exit 1
fi

cp "$ENGINE_SRC" "$DIST/zeri-engine"
chmod +x "$DIST/zeri-engine"

VERSION="unknown"
CACHE_FILE="$BUILD_DIR/CMakeCache.txt"
if [ -f "$CACHE_FILE" ]; then
    VERSION_LINE="$(grep 'ZeriEngine_VERSION:STATIC' "$CACHE_FILE" || true)"
    if [ -n "$VERSION_LINE" ]; then
        VERSION="$(printf '%s' "$VERSION_LINE" | sed -E 's/.*=([0-9]+\.[0-9]+\.[0-9]+).*/\1/')"
    fi
fi
printf '%s\n' "$VERSION" > "$DIST/version.txt"
echo "Wrote dist/version.txt with version: $VERSION"

VCPKG_LIB_DIR="$BUILD_DIR/vcpkg_installed/$VCPKG_TRIPLET/lib"
if [ -d "$VCPKG_LIB_DIR" ]; then
    while IFS= read -r -d '' library; do
        cp "$library" "$DIST/"
        echo "Copied library: $(basename "$library")"
    done < <(find "$VCPKG_LIB_DIR" -type f \( -name '*.so' -o -name '*.so.*' -o -name '*.dylib' \) -print0)
fi

echo "Building zeri TUI"
cd "$YUUMI_UI_DIR"
go build -o "$DIST/zeri" ./cmd/zeri-tui/
chmod +x "$DIST/zeri"

echo "Copying sidecar runtime"
RUNTIME_SRC="$SCRIPT_DIR/src/ZeriLink/Runtime"
if [ -d "$RUNTIME_SRC" ]; then
    cp -r "$RUNTIME_SRC/." "$DIST/runtime/"
fi

MANIFEST_SRC="$SCRIPT_DIR/runtime/runtime_manifest.json"
if [ -f "$MANIFEST_SRC" ]; then
    cp "$MANIFEST_SRC" "$DIST/runtime/"
    echo "Copied runtime_manifest.json"
else
    echo "WARNING: runtime/runtime_manifest.json not found in project root."
fi

echo ""
echo "Build completed."
find "$DIST" -type f | sort
