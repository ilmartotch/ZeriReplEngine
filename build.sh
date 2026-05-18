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
        echo " $2"
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
mkdir -p "$DIST/help"

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
    -G Ninja \
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
RUNTIME_CANDIDATES=(
    "$SCRIPT_DIR/runtime"
    "$SCRIPT_DIR/src/ZeriLink/Runtime"
)
RUNTIME_SRC=""
for candidate in "${RUNTIME_CANDIDATES[@]}"; do
    if [ -d "$candidate" ]; then
        RUNTIME_SRC="$candidate"
        break
    fi
done

if [ -z "$RUNTIME_SRC" ]; then
    echo "ERROR: runtime directory not found."
    exit 1
fi
cp -r "$RUNTIME_SRC/." "$DIST/runtime/"

MANIFEST_SRC="$SCRIPT_DIR/runtime/runtime_manifest.json"
if [ -f "$MANIFEST_SRC" ]; then
    cp "$MANIFEST_SRC" "$DIST/runtime/"
    echo "Copied runtime_manifest.json"
else
    echo "WARNING: runtime/runtime_manifest.json not found in project root."
fi

HELP_SRC="$SCRIPT_DIR/help"
if [ ! -d "$HELP_SRC" ]; then
    echo "ERROR: help directory not found in project root."
    exit 1
fi
cp -r "$HELP_SRC/." "$DIST/help/"

# Copy install script into dist
INSTALL_SCRIPT="$SCRIPT_DIR/install.sh"
if [ -f "$INSTALL_SCRIPT" ]; then
    cp "$INSTALL_SCRIPT" "$DIST/install.sh"
    chmod +x "$DIST/install.sh"
    echo "Copied install.sh to dist/"
else
    echo "ERROR: install.sh not found in repo root."
    exit 1
fi

INSTALL_MANIFEST_FILENAME="install_manifest.json"
MANIFEST_SCHEMA_VERSION=1

{
    printf '{\n  "version": %d,\n  "generated_by": "build.sh",\n  "assets": [' "$MANIFEST_SCHEMA_VERSION"
    first=true
    while IFS= read -r -d '' fpath; do
        rel="${fpath#$DIST/}"
        [ "$rel" = "$INSTALL_MANIFEST_FILENAME" ] && continue
        rel_dir="$(dirname "$rel")"
        [ "$rel_dir" = "." ] && dest="." || dest="$rel_dir"
        fname="$(basename "$rel")"
        case "$fname" in
            *.so.*) atype="lib" ;;
            *.so) atype="lib" ;;
            *.dylib) atype="lib" ;;
            *.exe) atype="binary" ;;
            *.dll) atype="dll" ;;
            *)
                if [ "${fname%.*}" = "$fname" ]; then
                    atype="binary"
                else
                    atype="asset"
                fi ;;
        esac
        [ "$first" = true ] && { printf '\n'; first=false; } || printf ',\n'
        printf '    {"src": "%s", "dest": "%s", "type": "%s"}' "$rel" "$dest" "$atype"
    done < <(find "$DIST" -type f -print0 | sort -z)
    printf '\n  ]\n}\n'
} > "$DIST/$INSTALL_MANIFEST_FILENAME"
echo "Generated $INSTALL_MANIFEST_FILENAME"

echo ""
echo "Build completed."
if [ ! -f "$DIST/help/help_catalog.json" ]; then
    echo "ERROR: dist/help/help_catalog.json is missing."
    exit 1
fi
if [ ! -f "$DIST/runtime/runtime_manifest.json" ]; then
    echo "ERROR: dist/runtime/runtime_manifest.json is missing."
    exit 1
fi
if [ ! -f "$DIST/$INSTALL_MANIFEST_FILENAME" ]; then
    echo "ERROR: dist/$INSTALL_MANIFEST_FILENAME is missing."
    exit 1
fi
find "$DIST" -type f | sort


# Added install_manifest.json generation after all dist/ assets are assembled. The manifest
# enumerates every file with its src path (dist/-relative, forward-slash), dest directory
# (install-root-relative, "."=root), and type (binary/dll/lib/asset). Pure bash implementation
# chosen to avoid adding a python3 dependency at build time; build.sh currently only requires
# cmake, go, git, cc.

# - INSTALL_MANIFEST_FILENAME: filename constant used in generation and verification.
# - MANIFEST_SCHEMA_VERSION: integer version constant for forward-compat checks in install scripts.
# - Type heuristic: *.so.*=lib, *.so=lib, *.dylib=lib, *.exe=binary, *.dll=dll,
#   no-extension=binary (zeri, zeri-engine), everything else=asset.
# - Added install_manifest.json presence check to the verification block.

