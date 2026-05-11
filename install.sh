#!/usr/bin/env bash
set -euo pipefail

REPO="ilmartotch/ZeriReplEngine"
FORCE=false
SYSTEM_INSTALL=false
UNINSTALL=false

while [ "$#" -gt 0 ]; do
    case "$1" in
        --force)
            FORCE=true
            ;;
        --system)
            SYSTEM_INSTALL=true
            ;;
        --uninstall)
            UNINSTALL=true
            ;;
        *)
            echo "Error: unknown argument '$1'."
            exit 1
            ;;
    esac
    shift
done

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

if [ "$SYSTEM_INSTALL" = true ] && [ "${EUID}" -ne 0 ]; then
    echo "Error: --system requires root. Run with sudo."
    exit 1
fi

if [ "$SYSTEM_INSTALL" = true ]; then
    BIN_DIR="/usr/local/bin"
    SHARE_DIR="/usr/local/share/zeri"
else
    BIN_DIR="$HOME/.local/bin"
    SHARE_DIR="$HOME/.local/share/zeri"
fi

if [ "$OS" = "darwin" ]; then
    USER_DATA_DIR="$HOME/Library/Application Support/zeri"
elif [ "$OS" = "linux" ]; then
    USER_DATA_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/zeri"
else
    echo "Error: unsupported operating system '$OS'."
    exit 1
fi

RUNTIME_DIR="$SHARE_DIR/runtime"
MANIFEST_FILE="$SHARE_DIR/zeri-manifest.json"

manifest_version() {
    if command -v python3 >/dev/null 2>&1; then
        python3 -c "import json,sys; print(json.load(sys.stdin).get('version',''))" < "$1"
    else
        grep -E '"version"' "$1" | sed -E 's/.*"version"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/' | head -n1
    fi
}

manifest_string_array() {
    key="$1"
    file="$2"
    if command -v python3 >/dev/null 2>&1; then
        python3 - "$key" "$file" <<'PY'
import json
import sys

key = sys.argv[1]
path = sys.argv[2]
with open(path, 'r', encoding='utf-8') as f:
    data = json.load(f)
for item in data.get(key, []):
    print(item)
PY
    else
        grep -E "\"$key\"" "$file" \
            | sed -E "s/.*\"$key\"[[:space:]]*:[[:space:]]*\[(.*)\].*/\1/" \
            | tr ',' '\n' \
            | sed -E 's/^[[:space:]]*"([^"]*)"[[:space:]]*$/\1/' \
            | sed '/^$/d'
    fi
}

release_tag() {
    if command -v python3 >/dev/null 2>&1; then
        python3 -c "import json,sys; print(json.load(sys.stdin).get('tag_name',''))"
    else
        grep -E '"tag_name"' | sed -E 's/.*"tag_name"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/' | head -n1
    fi
}

release_asset_url() {
    pattern="$1"
    if command -v python3 >/dev/null 2>&1; then
        python3 - "$pattern" <<'PY'
import json
import sys

pattern = sys.argv[1]
data = json.load(sys.stdin)
for asset in data.get('assets', []):
    name = asset.get('name', '')
    if pattern in name:
        print(asset.get('browser_download_url', ''))
        break
PY
    else
        grep browser_download_url | grep "$pattern" | cut -d'"' -f4 | head -n1
    fi
}

if [ "$UNINSTALL" = true ]; then
    if [ ! -f "$MANIFEST_FILE" ]; then
        echo "No Zeri installation found at $SHARE_DIR."
        exit 1
    fi

    while IFS= read -r file_name; do
        [ -z "$file_name" ] && continue
        target_file="$BIN_DIR/$file_name"
        if [ -f "$target_file" ]; then
            rm -f "$target_file"
        fi
    done < <(manifest_string_array "files_installed" "$MANIFEST_FILE")

    bin_dir_escaped="$(printf '%s' "$BIN_DIR" | sed 's/[.[\*^$()+?{}|/]/\\&/g')"
    while IFS= read -r rc_file; do
        [ -z "$rc_file" ] && continue
        if [ -f "$rc_file" ]; then
            if [ "$OS" = "darwin" ]; then
                sed -i.bak "/$bin_dir_escaped/d" "$rc_file"
                rm -f "$rc_file.bak"
            else
                sed -i "/$bin_dir_escaped/d" "$rc_file"
            fi
        fi
    done < <(manifest_string_array "shell_rc_files" "$MANIFEST_FILE")

    rm -rf "$RUNTIME_DIR"
    rm -f "$MANIFEST_FILE"

    if [ -d "$SHARE_DIR" ] && [ -z "$(ls -A "$SHARE_DIR")" ]; then
        rmdir "$SHARE_DIR"
    fi

    confirm_delete="$(printf 'This will permanently delete all user data in %s. Are you sure? [y/N] ' "$USER_DATA_DIR"; read -r v; printf '%s' "$v")"
    if [ "$confirm_delete" = "y" ]; then
        rm -rf "$USER_DATA_DIR"
    fi

    echo "Zeri has been completely uninstalled."
    exit 0
fi

case "$OS-$ARCH" in
    linux-x86_64)
        ASSET_PATTERN="linux-amd64"
        ;;
    linux-arm64|linux-aarch64)
        ASSET_PATTERN="linux-arm64"
        ;;
    darwin-arm64)
        ASSET_PATTERN="macos-arm64"
        ;;
    darwin-x86_64)
        ASSET_PATTERN="macos-amd64"
        ;;
    *)
        echo "Error: unsupported architecture $OS-$ARCH"
        exit 1
        ;;
esac

echo "Installing Zeri for $OS-$ARCH..."
RELEASE_JSON="$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest")"
LATEST_VERSION="$(printf '%s' "$RELEASE_JSON" | release_tag)"
DOWNLOAD_URL="$(printf '%s' "$RELEASE_JSON" | release_asset_url "$ASSET_PATTERN")"

if [ -z "$DOWNLOAD_URL" ]; then
    echo "Error: no release asset found for $ASSET_PATTERN"
    exit 1
fi

if [ -f "$MANIFEST_FILE" ]; then
    INSTALLED_VERSION="$(manifest_version "$MANIFEST_FILE")"
    if [ "$INSTALLED_VERSION" = "$LATEST_VERSION" ] && [ "$FORCE" = false ]; then
        echo "Zeri $LATEST_VERSION is already installed. Use --force to reinstall."
        exit 0
    fi
fi

if [ "$FORCE" = true ]; then
    confirm_force="$(printf 'Warning: reinstalling may affect custom commands, variables, and saved sessions. Proceed? [y/N] '; read -r v; printf '%s' "$v")"
    if [ "$confirm_force" != "y" ]; then
        exit 0
    fi
fi

TMP_DIR="$(mktemp -d)"
ARCHIVE_FILE="$TMP_DIR/asset"
STAGING_DIR="$TMP_DIR/staging"
mkdir -p "$STAGING_DIR"

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

echo "Downloading $DOWNLOAD_URL"
curl -fsSL "$DOWNLOAD_URL" -o "$ARCHIVE_FILE"

if file "$ARCHIVE_FILE" | grep -qi 'zip'; then
    unzip -q "$ARCHIVE_FILE" -d "$STAGING_DIR"
else
    tar -xf "$ARCHIVE_FILE" -C "$STAGING_DIR"
fi

mkdir -p "$BIN_DIR"
mkdir -p "$SHARE_DIR"
mkdir -p "$RUNTIME_DIR"

ZERI_BIN="$(find "$STAGING_DIR" -type f -name 'zeri' | head -n1)"
ZERI_ENGINE_BIN="$(find "$STAGING_DIR" -type f -name 'zeri-engine' | head -n1)"
RUNTIME_SRC="$(find "$STAGING_DIR" -type d \( -name 'runtime' -o -name 'Runtime' \) | head -n1)"

if [ -z "$ZERI_BIN" ] || [ -z "$ZERI_ENGINE_BIN" ] || [ -z "$RUNTIME_SRC" ]; then
    echo "Error: release package is missing zeri, zeri-engine, or runtime directory."
    exit 1
fi

cp "$ZERI_BIN" "$BIN_DIR/zeri"
cp "$ZERI_ENGINE_BIN" "$BIN_DIR/zeri-engine"
chmod +x "$BIN_DIR/zeri" "$BIN_DIR/zeri-engine"

rm -rf "$RUNTIME_DIR"
mkdir -p "$RUNTIME_DIR"
cp -R "$RUNTIME_SRC"/. "$RUNTIME_DIR"/

mkdir -p "$USER_DATA_DIR/sessions"
mkdir -p "$USER_DATA_DIR/scripts"

PATH_MODIFIED=false
declare -a MODIFIED_RC_FILES=()
if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
    PROFILE_LINE="export PATH=\"\$PATH:$BIN_DIR\""
    for rc in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
        if [ ! -f "$rc" ]; then
            touch "$rc"
        fi
        if ! grep -Fq "$BIN_DIR" "$rc"; then
            echo "$PROFILE_LINE" >> "$rc"
            MODIFIED_RC_FILES+=("$rc")
            PATH_MODIFIED=true
        fi
    done
fi

INSTALLED_AT="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
if command -v python3 >/dev/null 2>&1; then
    python3 - "$MANIFEST_FILE" "$LATEST_VERSION" "$INSTALLED_AT" "$BIN_DIR" "$SHARE_DIR" "$USER_DATA_DIR" "$SYSTEM_INSTALL" "$PATH_MODIFIED" "${MODIFIED_RC_FILES[@]}" <<'PY'
import json
import sys

manifest_file = sys.argv[1]
version = sys.argv[2]
installed_at = sys.argv[3]
bin_dir = sys.argv[4]
share_dir = sys.argv[5]
user_data_dir = sys.argv[6]
system_install = sys.argv[7].lower() == 'true'
path_modified = sys.argv[8].lower() == 'true'
rc_files = sys.argv[9:]

data = {
    "version": version,
    "installed_at": installed_at,
    "bin_dir": bin_dir,
    "share_dir": share_dir,
    "user_data_dir": user_data_dir,
    "system_install": system_install,
    "path_modified": path_modified,
    "shell_rc_files": rc_files,
    "files_installed": ["zeri", "zeri-engine"],
    "dirs_created": ["runtime"],
}

with open(manifest_file, 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2)
PY
else
    rc_json=""
    for rc in "${MODIFIED_RC_FILES[@]}"; do
        if [ -n "$rc_json" ]; then
            rc_json="$rc_json, "
        fi
        rc_json="$rc_json\"$rc\""
    done
    cat > "$MANIFEST_FILE" <<EOF
{
  "version": "$LATEST_VERSION",
  "installed_at": "$INSTALLED_AT",
  "bin_dir": "$BIN_DIR",
  "share_dir": "$SHARE_DIR",
  "user_data_dir": "$USER_DATA_DIR",
  "system_install": $SYSTEM_INSTALL,
  "path_modified": $PATH_MODIFIED,
  "shell_rc_files": [$rc_json],
  "files_installed": ["zeri", "zeri-engine"],
  "dirs_created": ["runtime"]
}
EOF
fi

echo "Installation completed successfully."

: <<'ZERI_INSTALLER_NOTE'
/*
This script now supports install and uninstall modes with --force and --system flags.
It keeps binaries and shared runtime under dedicated install locations, keeps user data separate,
tracks installation metadata in a manifest, updates shell PATH entries when needed,
and removes installed artifacts and recorded shell entries during uninstall.
*/
ZERI_INSTALLER_NOTE
