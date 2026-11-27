#!/usr/bin/env bash
# Arranca el PC 486 emulado (QEMU). Variables opcionales:
#   RAM=32  CPU=486  VGA=cirrus|std  AUDIO=sdl|pa|alsa|none  SOUND=sb16+adlib|sb16|adlib|none
#   DISPLAY_BACKEND=sdl|gtk|vnc=:0  IMG=dos486.img
cd "$(dirname "$0")"
IMG=${IMG:-dos486.img}
RAM=${RAM:-32}
CPU=${CPU:-486}
VGA=${VGA:-cirrus}
AUDIO=${AUDIO:-sdl}
SOUND=${SOUND:-sb16+adlib}
DISP=${DISPLAY_BACKEND:-sdl}
[ -f "$IMG" ] || { echo "No existe $IMG. Ejecuta primero ./build.sh"; exit 1; }
SND=(-audiodev "$AUDIO",id=snd0)
case "$SOUND" in *sb16*) SND+=(-device sb16,audiodev=snd0,iobase=0x220,irq=5,dma=1,dma16=5);; esac
case "$SOUND" in *adlib*) SND+=(-device adlib,audiodev=snd0);; esac
exec qemu-system-i386 \
  -machine pc -cpu "$CPU" -m "$RAM" \
  -vga "$VGA" \
  -drive file="$IMG",format=raw,if=none,id=hd0 \
  -device ide-hd,drive=hd0,cyls=260,heads=16,secs=63 \
  "${SND[@]}" \
  -rtc base=localtime \
  -display "$DISP" "$@"
