#!/usr/bin/env bash
set -e

REPO="ilmartotch/ReplZeriEmgine"
INSTALL_DIR="$HOME/.local/bin"
mkdir -p "$INSTALL_DIR"

OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

case "$OS-$ARCH" in
    linux-x86_64)  ASSET_PATTERN="linux-amd64" ;;
    darwin-arm64)  ASSET_PATTERN="macos-arm64" ;;
    darwin-x86_64) ASSET_PATTERN="macos-amd64" ;;
    *)
        echo "Architettura non supportata: $OS-$ARCH"
        exit 1
        ;;
esac

echo "Installazione Zeri per $OS-$ARCH..."

DOWNLOAD_URL=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" \
    | grep browser_download_url \
    | grep "$ASSET_PATTERN" \
    | cut -d'"' -f4)

if [ -z "$DOWNLOAD_URL" ]; then
    echo "Errore: nessuna release trovata per $ASSET_PATTERN"
    exit 1
fi

echo "Download da $DOWNLOAD_URL"
TMP=$(mktemp -d)
curl -fsSL "$DOWNLOAD_URL" | tar -xz -C "$TMP"

cp "$TMP/zeri" "$INSTALL_DIR/"
cp "$TMP/ZeriEngine" "$INSTALL_DIR/"
cp -r "$TMP/runtime" "$INSTALL_DIR/"
chmod +x "$INSTALL_DIR/zeri" "$INSTALL_DIR/ZeriEngine"
rm -rf "$TMP"

PROFILE_LINE="export PATH=\"\$PATH:$INSTALL_DIR\""
for RC in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
    if [ -f "$RC" ] && ! grep -q "$INSTALL_DIR" "$RC"; then
        echo "$PROFILE_LINE" >> "$RC"
    fi
done

echo ""
echo "Installazione completata in $INSTALL_DIR"
echo "Riapri il terminale e scrivi: zeri"
