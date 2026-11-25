#!/bin/bash
# ReplayDeck Studio para Mac: instala (ou atualiza) sem os avisos de "não foi possível verificar".
# Use no Terminal:
#   curl -fsSL https://raw.githubusercontent.com/samuel2002amaral/replaydeck-studio/main/installer/instalar-mac.sh | bash
set -e
REPO="samuel2002amaral/replaydeck-studio"
APP="/Applications/ReplayDeck Studio.app"

echo "ReplayDeck Studio: procurando a versão mais nova para Mac…"
URL=$(curl -fsSL "https://api.github.com/repos/$REPO/releases?per_page=30" |
	grep -o '"browser_download_url": *"[^"]*-Mac\.dmg"' | head -1 | sed 's/.*"\(https[^"]*\)"$/\1/')
if [ -z "$URL" ]; then
	echo "Não achei o arquivo do Mac na página de versões."
	exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
echo "Baixando $(basename "$URL")…"
curl -fL --progress-bar "$URL" -o "$TMP/rds.dmg"

echo "Instalando em Aplicativos…"
MNT="$TMP/volume"
mkdir -p "$MNT"
hdiutil attach -nobrowse -readonly -mountpoint "$MNT" "$TMP/rds.dmg" >/dev/null
osascript -e 'quit app "ReplayDeck Studio"' >/dev/null 2>&1 || true
sleep 1
copiar() {
	rm -rf "$APP" && ditto "$MNT/ReplayDeck Studio.app" "$APP"
}
if ! copiar 2>/dev/null; then
	echo "Precisa da senha do Mac para gravar em Aplicativos:"
	sudo rm -rf "$APP"
	sudo ditto "$MNT/ReplayDeck Studio.app" "$APP"
fi
hdiutil detach "$MNT" -quiet || true
# baixado pelo Terminal não ganha a marca de "veio da internet"; tira por garantia
xattr -dr com.apple.quarantine "$APP" 2>/dev/null || sudo xattr -dr com.apple.quarantine "$APP" 2>/dev/null || true

echo "Pronto! Abrindo o ReplayDeck Studio…"
open "$APP"
