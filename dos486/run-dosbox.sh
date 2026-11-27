#!/usr/bin/env bash
# Ejecuta los mismos juegos con DOSBox (CPU 486). Mejor compatibilidad grafica que QEMU
# (p. ej. Wolfenstein 3D, Duke Nukem II y Duke Nukem 3D). Requiere ./build.sh previo.
#   CYCLES="fixed 26800" (486DX2-66 aprox.)  CYCLES=max  DOSBOX=dosbox|dosbox-x
cd "$(dirname "$0")"
[ -d build/c ] || { echo "Falta build/c. Ejecuta primero ./build.sh"; exit 1; }
DOSBOX=${DOSBOX:-dosbox}
CYCLES=${CYCLES:-fixed 26800}
cat > build/dosbox.conf <<CONF
[sdl]
fullscreen=false
output=${OUTPUT:-surface}
[cpu]
core=normal
cputype=486
cycles=$CYCLES
[dosbox]
machine=svga_s3
memsize=32
[sblaster]
sbtype=sb16
sbbase=220
irq=5
dma=1
hdma=5
[serial]
# Modem emulado: ATDT host[:puerto] abre una conexion telnet real por Internet
serial1=modem
serial2=dummy
[autoexec]
mount c $PWD/build/c
c:
set PATH=Z:\;C:\;C:\\FREEDOS\\BIN
call C:\\MENU.BAT
CONF
exec "$DOSBOX" -conf build/dosbox.conf "$@"
