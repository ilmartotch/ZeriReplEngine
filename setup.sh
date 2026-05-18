#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

add_to_path_file() {
    local file="$1"
    local entry="export PATH=\"\$PATH:$SCRIPT_DIR\""
    if [ -f "$file" ] && grep -qF "$SCRIPT_DIR" "$file" 2>/dev/null; then
        return 0
    fi
    printf '\n%s\n' "$entry" >> "$file"
    return 1
}

ADDED=0
for rc in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
    if [ -f "$rc" ]; then
        if ! add_to_path_file "$rc"; then
            ADDED=1
            echo "Added Zeri to PATH in $rc"
        fi
    fi
done

if [ "$ADDED" -eq 0 ]; then
    echo "Zeri is already in PATH in your shell config files."
fi

echo "Open a new terminal and run 'zeri' from anywhere."

# New setup.sh for users who extract the release ZIP on Unix.
# Resolves its own directory via BASH_SOURCE (not CWD) and appends a PATH export
# to ~/.bashrc, ~/.zshrc, and ~/.profile if present. Idempotent: skips files that
# already reference the Zeri directory. Users run: ./setup.sh — then restart terminal.
