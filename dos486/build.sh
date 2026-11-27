#!/usr/bin/env bash
# Construye dos486.img: disco duro FAT16 de 128 MB con FreeDOS 1.3 + juegos
# y programas shareware/freeware de archive.org. Uso: ./build.sh
set -euo pipefail
cd "$(dirname "$0")"

IMG=${IMG:-dos486.img}
SIZE_MB=${SIZE_MB:-128}
DL=downloads
STAGE=build
export MTOOLS_SKIP_CHECK=1

for t in curl unzip python3 mcopy mmd mkfs.fat sha256sum; do
  command -v "$t" >/dev/null || { echo "Falta '$t'. En Debian/Ubuntu: sudo apt install mtools dosfstools unzip curl python3"; exit 1; }
done

A=https://archive.org/download
FD=https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.3/official

# nombre-local | URL | sha256 | carpeta destino en C:\ ('-' = no copiar)
SOURCES=$(cat <<LIST
FD13-LiteUSB.zip|$FD/FD13-LiteUSB.zip|64a934585087ccd91a18c55e20ee01f5f6762be712eeaa5f456be543778f9f7e|-
doom19.zip|$A/DoomsharewareEpisode/DoomV1.9sw1995idSoftwareInc.action.zip|63ad7609f2e951fb2198f682e1226f003946c75c00b9785fa967ffb12c6745f7|GAMES/DOOM
wolf3d.zip|$A/wolf3dsw/wolf3dsw.zip|76ee5e73e7d6341aefff620989bb5f828e9d295982afd5415b62dee7fe54eb64|GAMES/WOLF3D
duke3d.zip|$A/duke-3d-sw/duke-3d-sw.zip|e59596f32aad78f1e4e78966148de488f429d4ec6540a4cd2788c75ce43ef0f6|GAMES/DUKE3D
duke2.zip|$A/msdos_DUKE2_shareware/DUKE2.zip|b5d4a0bface1e90cb93b32addca65d50bfd862807163696ab1708670703bc118|GAMES/DUKE2
keen1.zip|$A/keen1-sw/keen1.zip|a823802afb03ab3878122370de43ff25dd721df3da2538a4d54951787d63be10|GAMES/KEEN1
keen4.zip|$A/Keen4e-sw/keen4.zip|d7834a9b3265a997951743b58282886966e555bc6bf71d92df4de17b6129c36b|GAMES/KEEN4
dave.zip|$A/msdos_DDAVEVGA_shareware/DDAVEVGA.zip|5ec9fcfd3f080baf85dae989b55802a899d97e07d26e40d0029e5007f8040f32|GAMES/DAVE
scorch.zip|$A/msdos_SCORCH15_shareware/SCORCH15.zip|002117403b50071639e293a9005658e4a51c0b0740965fb157a03db38caca395|GAMES/SCORCH
epicpin.zip|$A/epic_pinball_11/epic_pinball.zip|b9965ed6a7a85e7746c57cdb89107fbfba6dc7c58b6cc6856be26756fa59d560|GAMES/EPICPIN
skyroads.zip|$A/SkyRoads/skyroads.zip|24762fe14196a642224288fe2063c9d0c0033be9c5b4b2f97fded09390803ee4|GAMES/SKYROADS
zzt.zip|$A/ZztByEpicMegagames/ZztV3.2sw1991epicMegagamesInc.actionAdventureRole-playingrpg.zip|9b2b9504d76d0156404a2721824ee53c240abf3d874ac2655df3ade9bd205ff1|GAMES/ZZT
heretic.zip|$A/hereticsw/hereticsw.zip|6333fc7e4d9c742c04da7469169643949d10d3d624ec270ed9347d6081381d98|GAMES/HERETIC
lemmings.zip|$A/Lemmings_1020/lemmings.zip|85799eaf5dcfc80ef86900f9aa4567adbc9cc4e60111a96522b84ab2aa3e2d45|GAMES/LEMMINGS
prince.zip|$A/PrinceOfPersia_479/pop1.ZIP|c2482998c6e893594166a9b0b4f9ac9335251fe4d5e38939315997a308906723|GAMES/PRINCE
stunts.zip|$A/stunts_demo_disk/stunts_demo_disk.zip|f502478813f572b0027b52d81c908ff0fadd956517b54d15ce7be560525ed613|GAMES/STUNTS
carma.zip|$A/carmdemo/carmdemo.zip|b11f81f57fcd657f00af308e61dd5e5d601069a19b82c0262362773f389576b1|GAMES/CARMA
ootw.zip|$A/OutOfThisWorldDemo/ootwdemo.zip|84631d1af62a6a4f50861fba740813773330a663498a3245ebea6b6afa68465a|GAMES/ANOTHER
telix.zip|$A/TLX3211_ZIP/TLX3211.ZIP|965b5b11b1f42ef4589ea7b34c1c46fc23ea679b9a915914687a903a02da83ac|PROGRAMS/TELIX
tp55.zip|$A/TurboPascal55/tp55.zip|a04ff65555f88cd7714a11362c6f625f254932679ad5623b207015893ef638b6|PROGRAMS/TP55
LIST
)

mkdir -p "$DL"
rm -rf "$STAGE"; mkdir -p "$STAGE"

echo "== Descargando y verificando (SHA-256)"
while IFS='|' read -r name url sha dest; do
  [ -f "$DL/$name" ] || { echo "  descargando $name"; curl -fsSL --retry 3 -o "$DL/$name" "$url"; }
  got=$(sha256sum "$DL/$name" | cut -d' ' -f1)
  [ "$got" = "$sha" ] || { echo "  SHA-256 distinto en $name (esperado $sha, obtenido $got)"; exit 1; }
  if [ "$dest" != "-" ]; then
    mkdir -p "$STAGE/$dest"
    unzip -qo "$DL/$name" -d "$STAGE/$dest"
    # Duke Nukem II viene dentro de una subcarpeta
    if [ "$dest" = "GAMES/DUKE2" ] && [ -d "$STAGE/$dest/DUKE2" ]; then
      mv "$STAGE/$dest"/DUKE2/* "$STAGE/$dest"/ && rmdir "$STAGE/$dest/DUKE2"
    fi
  fi
done <<< "$SOURCES"

# Duke Nukem 3D viene configurado con IRQ 7; la tarjeta emulada usa IRQ 5
sed -i 's/^BlasterInterrupt = 7/BlasterInterrupt = 5/' "$STAGE/GAMES/DUKE3D/DUKE3D.CFG"
# Heretic igual: IRQ 7 -> 5
sed -i 's/^snd_sbirq\([[:space:]]*\)7/snd_sbirq\15/' "$STAGE/GAMES/HERETIC/HERETIC.CFG"

# Lemmings viene dentro de una subcarpeta
if [ -d "$STAGE/GAMES/LEMMINGS/LEMMINGS" ]; then mv "$STAGE/GAMES/LEMMINGS/LEMMINGS"/* "$STAGE/GAMES/LEMMINGS/" && rmdir "$STAGE/GAMES/LEMMINGS/LEMMINGS"; fi

# Stunts viene dentro de dos subcarpetas
if [ -d "$STAGE/GAMES/STUNTS/stunts_demo_disk/stunts_demo_disk" ]; then
  mv "$STAGE/GAMES/STUNTS/stunts_demo_disk/stunts_demo_disk"/* "$STAGE/GAMES/STUNTS/" && rm -rf "$STAGE/GAMES/STUNTS/stunts_demo_disk"
fi

# Carmageddon: sonido configurado para la Sound Blaster 16 emulada (A220 I5 D1)
printf '[DIGITAL]\r\nDeviceName  = Sound Blaster 16/AWE32\r\nDeviceIRQ   = 5\r\nDeviceDMA   = 1\r\nDevicePort  = 0x220\r\nDeviceID    = 0xe015\r\n\r\n[MIDI]\r\nDeviceName  = No MIDI Device\r\nDevicePort  = 0xffffffff\r\nDeviceID    = 0xffffffff\r\n' > "$STAGE/GAMES/CARMA/S3.INI"

# Another World (Out of This World): quitar el COMMAND.COM de MS-DOS que trae el zip
rm -f "$STAGE/GAMES/ANOTHER/COMMAND.COM"

# Telix: el zip trae cuatro zips internos
for z in "$STAGE"/PROGRAMS/TELIX/TLX321*.ZIP; do unzip -qo "$z" -d "$STAGE/PROGRAMS/TELIX" && rm -f "$z"; done

# Turbo Pascal: Disk1 va en TP55, Disk2 en TP55\DISK2
mv "$STAGE/PROGRAMS/TP55/Disk1"/* "$STAGE/PROGRAMS/TP55/" && rmdir "$STAGE/PROGRAMS/TP55/Disk1"
mv "$STAGE/PROGRAMS/TP55/Disk2" "$STAGE/PROGRAMS/TP55/DISK2"

echo "== Extrayendo FreeDOS 1.3"
unzip -qo "$DL/FD13-LiteUSB.zip" -d "$STAGE/lite"
LITE="$STAGE/lite/FD13LITE.img@@32256"
mkdir -p "$STAGE/fd" "$STAGE/pk"
mcopy -s -n -i "$LITE" ::FREEDOS "$STAGE/fd/"
mcopy -n -i "$LITE" ::KERNEL.SYS ::COMMAND.COM "$STAGE/fd/"
for p in BASE/ctmouse BASE/choice BASE/mode BASE/tree BASE/defrag BASE/nansi UTIL/cwsdpmi; do
  mcopy -n -i "$LITE" "::PACKAGES/$p.zip" "$STAGE/pk/"
done
for z in "$STAGE"/pk/*.zip; do
  unzip -qo -j "$z" 'BIN/*.EXE' 'BIN/*.COM' 'BIN/*.SYS' -d "$STAGE/fd/FREEDOS/BIN" 2>/dev/null || true
done

# LOADFIX propio (tools/loadfix.asm): evita "Packed file is corrupt" en EXE con EXEPACK
cp tools/LOADFIX.COM "$STAGE/fd/FREEDOS/BIN/LOADFIX.COM"

echo "== Configuracion de arranque (fin de linea DOS)"
mkdir -p "$STAGE/cfg"
for f in FDCONFIG.SYS FDAUTO.BAT MENU.BAT; do sed 's/$/\r/' "config/$f" > "$STAGE/cfg/$f"; done
# Un .BAT por programa en C:\ (DOOM, WOLF3D, KEEN1...): escribir el nombre lo carga
mkdir -p "$STAGE/cfg/cmds"
grep -v '^#' config/COMMANDS.LST | while IFS='|' read -r cmd drv dir exe title; do
  { printf '@echo off\r\nC:\r\n'; [ "$drv" = G2 ] && printf '%%G2%%\r\n'
    printf 'cd \\%s\r\n%s\r\nC:\r\ncd \\\r\n' "$dir" "$exe"; } > "$STAGE/cfg/cmds/$cmd.BAT"
done

echo "== Creando disco $IMG (${SIZE_MB} MB, geometria 16 cabezas x 63 sectores)"
rm -f "$IMG"
truncate -s "${SIZE_MB}M" "$IMG"
python3 - "$IMG" "$STAGE/lite/FD13LITE.img" <<'PY'
import struct, sys
img, lite = sys.argv[1], sys.argv[2]
H, S, START = 16, 63, 63
total = __import__("os").path.getsize(img) // 512
cyl = total // (H * S)
usable = cyl * H * S
plen = usable - START
def chs(lba):
    c, r = divmod(lba, H * S); h, s = divmod(r, S)
    return bytes([h, ((c >> 2) & 0xC0) | (s + 1), c & 0xFF])
src = open(lite, "rb").read(512)
mbr = bytearray(512)
mbr[:446] = src[:446]                      # codigo MBR de FreeDOS
mbr[446:462] = b"\x80" + chs(START) + b"\x06" + chs(usable - 1) + struct.pack("<II", START, plen)
mbr[510:512] = b"\x55\xAA"
with open(img, "r+b") as f:
    f.write(mbr)
print(f"  cilindros={cyl} particion={plen} sectores")
PY
PLEN=$(python3 -c "import os;t=os.path.getsize('$IMG')//512;print((t//(16*63))*16*63-63)")
mkfs.fat -F 16 -S 512 -s 4 -h 63 --offset 63 -n DOS486 "$IMG" $((PLEN/2)) >/dev/null
python3 - "$IMG" "$STAGE/lite/FD13LITE.img" <<'PY'
import struct, sys
img, lite = sys.argv[1], sys.argv[2]
OFF = 63 * 512
fs = open(lite, "rb"); fs.seek(OFF); boot = bytearray(fs.read(512))
with open(img, "r+b") as f:
    f.seek(OFF); mine = bytearray(f.read(512))
    boot[3:0x3E] = mine[3:0x3E]            # conservar BPB propio
    struct.pack_into("<HH", boot, 0x18, 63, 16)   # sectores/pista, cabezas
    struct.pack_into("<I", boot, 0x1C, 63)        # sectores ocultos
    f.seek(OFF); f.write(boot)
PY

echo "== Copiando archivos al disco"
D="$IMG@@32256"
mcopy -m -i "$D" "$STAGE/fd/KERNEL.SYS" "$STAGE/fd/COMMAND.COM" ::
mmd -i "$D" ::/GAMES ::/PROGRAMS ::/TEMP
mcopy -s -m -i "$D" "$STAGE/fd/FREEDOS" ::
mcopy -m -i "$D" "$STAGE/cfg/FDCONFIG.SYS" "$STAGE/cfg/FDAUTO.BAT" "$STAGE/cfg/MENU.BAT" "$STAGE"/cfg/cmds/*.BAT ::
for dir in "$STAGE"/GAMES/* "$STAGE"/PROGRAMS/*; do
  rel=${dir#"$STAGE"/}
  mmd -i "$D" "::/$rel"
  mcopy -s -m -i "$D" "$dir"/* "::/$rel/"
done
echo "== Arbol de carpetas para DOSBox (mismo contenido que el disco)"
rm -rf "$STAGE/c"; mkdir -p "$STAGE/c"
ln -s ../GAMES "$STAGE/c/GAMES"; ln -s ../PROGRAMS "$STAGE/c/PROGRAMS"
ln -s ../fd/FREEDOS "$STAGE/c/FREEDOS"; mkdir -p "$STAGE/c/TEMP"
cp "$STAGE/cfg/MENU.BAT" "$STAGE"/cfg/cmds/*.BAT "$STAGE/c/"

echo "== Listo: $IMG"
echo "   Arranca con: ./run.sh   (QEMU, PC 486 completo con FreeDOS)"
echo "           o:   ./run-dosbox.sh   (DOSBox 486, mayor compatibilidad de juegos)"
