#!/usr/bin/env bash
set -euo pipefail

REPO="ilmartotch/ZeriReplEngine"
FORCE=false
SYSTEM_INSTALL=false
UNINSTALL=false
BIN_PATH=""
DATA_PATH=""

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
        --bin-path)
            shift
            if [ "$#" -eq 0 ] || [[ "$1" == --* ]]; then
                echo "Error: --bin-path requires a directory value."
                exit 1
            fi
            BIN_PATH="$1"
            ;;
        --data-path)
            shift
            if [ "$#" -eq 0 ] || [[ "$1" == --* ]]; then
                echo "Error: --data-path requires a directory value."
                exit 1
            fi
            DATA_PATH="$1"
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

if [ "$SYSTEM_INSTALL" = true ]; then
    echo "Warning: --system is deprecated and ignored. PATH is always updated at user scope." >&2
fi

if [ "$OS" != "darwin" ] && [ "$OS" != "linux" ]; then
    echo "Error: unsupported operating system '$OS'."
    exit 1
fi

is_interactive() {
    [ -t 0 ] && [ -t 1 ]
}

resolve_required_path() {
    local current_value="$1"
    local prompt_message="$2"
    local flag_name="$3"
    if [ -n "$current_value" ]; then
        printf '%s\n' "$current_value"
        return 0
    fi
    if is_interactive; then
        printf '%s ' "$prompt_message" >&2
        read -r entered
        if [ -z "$entered" ]; then
            echo "Error: missing required path for $flag_name." >&2
            exit 1
        fi
        printf '%s\n' "$entered"
        return 0
    fi
    echo "Error: non-interactive execution requires $flag_name." >&2
    exit 1
}

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

manifest_value() {
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
value = data.get(key, "")
if isinstance(value, str):
    print(value)
PY
    else
        grep -E "\"$key\"" "$file" | sed -E "s/.*\"$key\"[[:space:]]*:[[:space:]]*\"([^\"]*)\".*/\1/" | head -n1
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

BIN_DIR="$(resolve_required_path "$BIN_PATH" "Enter binary installation path (directory for zeri):" "--bin-path")"
MANIFEST_FILE="$BIN_DIR/zeri-manifest.json"

if [ "$UNINSTALL" = true ]; then
    if [ ! -f "$MANIFEST_FILE" ]; then
        echo "No Zeri installation found at $BIN_DIR."
        exit 1
    fi

    if [ -n "$DATA_PATH" ]; then
        USER_DATA_DIR="$DATA_PATH"
    else
        USER_DATA_DIR="$(manifest_value "user_data_dir" "$MANIFEST_FILE")"
        if [ -z "$USER_DATA_DIR" ]; then
            USER_DATA_DIR="$(resolve_required_path "" "Enter user data path to remove on uninstall:" "--data-path")"
        fi
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

    while IFS= read -r dir_name; do
        [ -z "$dir_name" ] && continue
        target_dir="$BIN_DIR/$dir_name"
        if [ -d "$target_dir" ]; then
            rm -rf "$target_dir"
        fi
    done < <(manifest_string_array "dirs_created" "$MANIFEST_FILE")

    rm -f "$MANIFEST_FILE"

    confirm_delete="$(printf 'This will permanently delete all user data in %s. Are you sure? [y/N] ' "$USER_DATA_DIR"; read -r v; printf '%s' "$v")"
    if [ "$confirm_delete" = "y" ]; then
        rm -rf "$USER_DATA_DIR"
    fi

    echo "Zeri has been completely uninstalled."
    exit 0
fi

USER_DATA_DIR="$(resolve_required_path "$DATA_PATH" "Enter user data path (sessions/scripts storage):" "--data-path")"

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

INSTALL_MANIFEST_FILENAME="install_manifest.json"
MANIFEST_SCHEMA_VERSION=1
ARCHIVE_MANIFEST="$(find "$STAGING_DIR" -type f -name "$INSTALL_MANIFEST_FILENAME" | head -n1)"
if [ -z "$ARCHIVE_MANIFEST" ]; then
    echo "Error: $INSTALL_MANIFEST_FILENAME not found in release archive. Cannot install." >&2
    exit 1
fi
ARCHIVE_ROOT="$(dirname "$ARCHIVE_MANIFEST")"

TRACK_TEMP="$(mktemp)"
python3 - "$ARCHIVE_MANIFEST" "$ARCHIVE_ROOT" "$BIN_DIR" "$TRACK_TEMP" "$MANIFEST_SCHEMA_VERSION" <<'PY'
import sys, json, os, shutil, stat

manifest_path = sys.argv[1]
archive_root = sys.argv[2]
bin_dir = sys.argv[3]
track_file = sys.argv[4]
expected_ver = int(sys.argv[5])

with open(manifest_path) as f:
    manifest = json.load(f)

if manifest.get("version") != expected_ver:
    print(f"Warning: manifest version {manifest.get('version')} != expected {expected_ver}", file=sys.stderr)

installed_files = []
created_dirs = set()

for asset in manifest["assets"]:
    src_path = os.path.join(archive_root, *asset["src"].split("/"))
    dest = asset["dest"]
    dest_dir = bin_dir if dest == "." else os.path.join(bin_dir, *dest.split("/"))
    if dest != ".":
        created_dirs.add(dest)
    os.makedirs(dest_dir, exist_ok=True)
    if os.path.exists(src_path):
        dest_path = os.path.join(dest_dir, os.path.basename(src_path))
        shutil.copy2(src_path, dest_path)
        if asset["type"] == "binary":
            st = os.stat(dest_path)
            os.chmod(dest_path, st.st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)
        installed_files.append(asset["src"])
        print(f"Installed: {asset['src']} -> {dest_dir}")
    else:
        print(f"Warning: {asset['src']} not found in archive", file=sys.stderr)

with open(track_file, "w") as f:
    json.dump({"files": installed_files, "dirs": sorted(created_dirs)}, f)
PY

for f in "$BIN_DIR/zeri" "$BIN_DIR/zeri-engine" "$BIN_DIR/help/help_catalog.json" "$BIN_DIR/runtime/runtime_manifest.json"; do
    if [ ! -f "$f" ]; then
        echo "Error: critical file missing after install: $f" >&2
        exit 1
    fi
done

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
    python3 - "$MANIFEST_FILE" "$LATEST_VERSION" "$INSTALLED_AT" "$BIN_DIR" "$USER_DATA_DIR" "$PATH_MODIFIED" "$TRACK_TEMP" "${MODIFIED_RC_FILES[@]}" <<'PY'
import json
import sys

manifest_file = sys.argv[1]
version = sys.argv[2]
installed_at = sys.argv[3]
bin_dir = sys.argv[4]
user_data_dir = sys.argv[5]
path_modified = sys.argv[6].lower() == 'true'
track_file = sys.argv[7]
rc_files = sys.argv[8:]

with open(track_file) as f:
    tracking = json.load(f)

data = {
    "version": version,
    "installed_at": installed_at,
    "bin_dir": bin_dir,
    "user_data_dir": user_data_dir,
    "system_install": False,
    "path_target": "user",
    "path_modified": path_modified,
    "shell_rc_files": rc_files,
    "files_installed": tracking["files"],
    "dirs_created": tracking["dirs"],
}

with open(manifest_file, 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2)
PY
else
    echo "Error: python3 is required for Zeri installation but was not found." >&2
    exit 1
fi

rm -f "$TRACK_TEMP"

echo "Installation completed successfully."



#Fixed RUNTIME_DIR (SHARE_DIR -> BIN_DIR), added help/ and shared-lib copy blocks, updated
#dirs_created in tracking manifest. Uninstall block updated to remove both runtime and help.

#Replaced all hardcoded copy logic with manifest-driven python3 loop reading
#install_manifest.json from the release archive. Key changes:

#- INSTALL_MANIFEST_FILENAME / MANIFEST_SCHEMA_VERSION: constants at the top of the install block.
#- ARCHIVE_ROOT: derived from the manifest's location so archives with a top-level subdirectory work.
#- TRACK_TEMP: temporary JSON file written by the copy step; consumed by the tracking-manifest writer
#  to populate files_installed and dirs_created dynamically, removing all hardcoded asset names.
#- Bash fallback for tracking manifest removed; python3 is now required and checked explicitly.
#- TRACK_TEMP cleaned up with rm -f before the success message.
#- Uninstall dirs_created loop replaces hardcoded rm -rf calls using manifest_string_array helper.
#- Binary type assets receive executable bit via chmod in the python3 copy loop.
#- Critical-files verification block added after copy for the four required files.