#!/usr/bin/env bash
# Crea los dos discos para la version web (DOSBox en WebAssembly) a partir de ../build.
# Cada disco se guarda como texto base64 de la imagen gzip (el visor de artefactos no sirve .zip).
#   C: FreeDOS + juegos   D: Duke Nukem 3D + Heretic
# Requiere ejecutar antes ../build.sh. Salida: web/out/
set -euo pipefail
cd "$(dirname "$0")"
ST=../build
OUT=out
export MTOOLS_SKIP_CHECK=1
[ -d "$ST/fd/FREEDOS" ] || { echo "Ejecuta primero ../build.sh"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/tmp"

# make_disk <imagen> <MB> <etiqueta> <copiar-codigo-de-arranque:0|1>
make_disk() {
  local img=$1 mb=$2 label=$3 boot=$4
  truncate -s "${mb}M" "$img"
  python3 - "$img" "$ST/lite/FD13LITE.img" "$boot" <<'PY'
import os, struct, sys
img, lite, boot = sys.argv[1], sys.argv[2], sys.argv[3] == "1"
H, S, START = 16, 63, 63
total = os.path.getsize(img) // 512
usable = (total // (H * S)) * H * S
def chs(lba):
    c, r = divmod(lba, H * S); h, s = divmod(r, S)
    return bytes([h, ((c >> 2) & 0xC0) | (s + 1), c & 0xFF])
mbr = bytearray(512)
if boot:
    mbr[:446] = open(lite, "rb").read(446)
mbr[446:462] = (b"\x80" if boot else b"\x00") + chs(START) + b"\x06" + chs(usable - 1) + struct.pack("<II", START, usable - START)
mbr[510:512] = b"\x55\xAA"
open(img, "r+b").write(mbr)
PY
  local plen=$(( ($(stat -c%s "$img")/512/1008)*1008 - 63 ))
  mkfs.fat -F 16 -S 512 -s 4 -h 63 --offset 63 -n "$label" "$img" $((plen/2)) >/dev/null 2>&1
  python3 - "$img" "$ST/lite/FD13LITE.img" <<'PY'
import struct, sys
img, lite = sys.argv[1], sys.argv[2]
OFF = 63 * 512
src = open(lite, "rb"); src.seek(OFF); bs = bytearray(src.read(512))
with open(img, "r+b") as f:
    f.seek(OFF); mine = bytearray(f.read(512))
    bs[3:0x3E] = mine[3:0x3E]
    struct.pack_into("<HH", bs, 0x18, 63, 16)
    struct.pack_into("<I", bs, 0x1C, 63)
    f.seek(OFF); f.write(bs)
PY
  echo "$(( $(stat -c%s "$img")/512/1008 ))"   # cilindros
}

echo "== Disco C: (arranque)"
CC=$(make_disk "$OUT/tmp/c.img" 32 DOS486 1)
D="$OUT/tmp/c.img@@32256"
# Version web: sin menu de arranque (modo 1 directo) y sin menu de juegos automatico
grep -v -e '^MENU' -e '^2?' ../config/FDCONFIG.SYS | sed -E 's/^1?2?\?//; s/$/\r/' > "$OUT/tmp/FDCONFIG.SYS"
{ grep -v -e '^vdelay' -e '^call C:.MENU.BAT' -e '^echo' ../config/FDAUTO.BAT | sed 's/^SET TMP=C:\\TEMP$/&\nSET G2=D:/'
  echo 'cls'
  # barra solida (79 x CP437 0xDB): la pagina la detecta para saber que DOS esta listo
  printf 'echo '; printf '\333%.0s' $(seq 79); printf '\n'
  echo 'echo  DOS 486 - FreeDOS 1.3 - CPU 486, Sound Blaster 16, VGA'
  echo 'echo  Escribe DOOM, WOLF3D, KEEN1... para cargar un programa. JUEGOS abre el menu.'
  echo 'echo.'
} | sed 's/$/\r/' > "$OUT/tmp/FDAUTO.BAT"
sed 's/$/\r/' ../config/MENU.BAT > "$OUT/tmp/MENU.BAT"
mcopy -m -i "$D" "$ST/fd/KERNEL.SYS" "$ST/fd/COMMAND.COM" "$OUT/tmp/FDCONFIG.SYS" "$OUT/tmp/FDAUTO.BAT" "$OUT/tmp/MENU.BAT" "$ST"/cfg/cmds/*.BAT ::
mmd -i "$D" ::/GAMES ::/PROGRAMS ::/TEMP
mcopy -s -m -i "$D" "$ST/fd/FREEDOS" ::
for dir in "$ST"/GAMES/* "$ST"/PROGRAMS/*; do
  rel=${dir#"$ST"/}
  case "$rel" in GAMES/DUKE3D|GAMES/HERETIC) continue;; esac
  mmd -i "$D" "::/$rel"; mcopy -s -m -i "$D" "$dir"/* "::/$rel/"
done

echo "== Disco D: (datos)"
CD=$(make_disk "$OUT/tmp/d.img" 32 JUEGOS2 0)
D="$OUT/tmp/d.img@@32256"
mmd -i "$D" ::/GAMES
for g in DUKE3D HERETIC; do mmd -i "$D" "::/GAMES/$g"; mcopy -s -m -i "$D" "$ST/GAMES/$g"/* "::/GAMES/$g/"; done

echo "== Discos como texto base64 (gzip) + dosbox.conf"
cat > "$OUT/dosbox.conf.txt" <<CONF
[dosbox]
machine=svga_s3
memsize=32
startbanner=false
[cpu]
core=normal
cputype=486
cycles=max limit 30000
[mixer]
rate=44100
[sblaster]
sbtype=sb16
sbbase=220
irq=5
dma=1
hdma=5
[speaker]
pcspeaker=true
[autoexec]
@echo off
imgmount 2 c.img -size 512,63,16,$CC -t hdd -fs none > nul
imgmount 3 d.img -size 512,63,16,$CD -t hdd -fs none > nul
cls
boot -l c
CONF
gzip -9 -n -c "$OUT/tmp/c.img" | base64 -w0 > "$OUT/disk-c.img.gz.txt"
gzip -9 -n -c "$OUT/tmp/d.img" | base64 -w0 > "$OUT/disk-d.img.gz.txt"
rm -rf "$OUT/tmp"

echo "== Emulador (paquete npm emulators 8.5.3, nucleo DOSBox-X)"
EMU_TGZ=../downloads/emulators-8.5.3.tgz
EMU_SHA=800538dce814cbeed75a54e98ba6db9248ab5d9fb598b1b0cf1e6fe82b6f9c58
[ -f "$EMU_TGZ" ] || curl -fsSL -o "$EMU_TGZ" https://registry.npmjs.org/emulators/-/emulators-8.5.3.tgz
[ "$(sha256sum "$EMU_TGZ" | cut -d' ' -f1)" = "$EMU_SHA" ] || { echo "SHA-256 distinto en emulators"; exit 1; }
mkdir -p "$OUT/emulators" "$OUT/npm"
tar xzf "$EMU_TGZ" -C "$OUT/npm"
for f in emulators.js wdosbox-x.js wdosbox-x.wasm wlibzip.js wlibzip.wasm; do cp "$OUT/npm/package/dist/$f" "$OUT/emulators/"; done
rm -rf "$OUT/npm"
cp index.html "$OUT/index.html"
ls -la "$OUT"
echo "Prueba local:  cd web/out && python3 -m http.server 8000   y abre http://localhost:8000"
