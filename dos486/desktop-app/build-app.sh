#!/usr/bin/env bash
# Empaqueta DOS 486 como app de escritorio (Electron). Requiere ../web/out (web/build-web.sh) y Node.js.
#   ./build-app.sh            -> Windows x64
#   PLATFORM=linux ./build-app.sh   (o darwin)
set -euo pipefail
cd "$(dirname "$0")"
ELECTRON_VERSION=45.3.1
PLATFORM=${PLATFORM:-win32}
ARCH=${ARCH:-x64}
[ -f ../web/out/index.html ] || { echo "Falta ../web/out: ejecuta ../web/build-web.sh"; exit 1; }
rm -rf web dist && mkdir -p web
cp -r ../web/out/* web/
# la pagina publicada no trae <html>/<head>: se envuelve para abrirla fuera de claude.ai
{ printf '<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">'
  printf '<style>:root{color-scheme:dark;padding-block:env(safe-area-inset-top,0px) env(safe-area-inset-bottom,0px)}body{margin:0}img{max-width:100%%}[hidden]{display:none!important}</style></head><body>'
  cat ../web/out/index.html; printf '</body></html>'; } > web/index.html
npx --yes @electron/packager@20.3.0 . "DOS 486" --platform="$PLATFORM" --arch="$ARCH" \
  --electron-version="$ELECTRON_VERSION" --out=dist --overwrite --asar --ignore='^/dist' --ignore='^/build-app\.sh$'
cd dist && for d in */; do zip -qr -9 "${d%/}.zip" "$d"; done && ls -la
