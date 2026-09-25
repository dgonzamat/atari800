#!/bin/sh
# Compila el emulador para el navegador: libatari800 + el puente a8web.c en
# un unico a8.js (SINGLE_FILE), que la pagina lleva incrustado.
#
# Dentro van solo las dos ROM del sistema, que a8_init() abre por ruta fija
# nada mas arrancar. Los juegos NO: la pagina los escribe ella misma en /data
# desde sus propios blobs, y embeberlos aqui los duplicaba.
#
# Uso:  util/wasm.sh <arbol-del-repo> <dir-de-trabajo>
#   <dir-de-trabajo>/romdata/ATARIXL.ROM y ATARIOSB.ROM tienen que existir.
#   Deja <dir-de-trabajo>/a8.js.
set -e
REPO=$(cd "${1:?falta el arbol del repo}" && pwd)
W=$(cd "${2:?falta el directorio de trabajo}" && pwd)
B="$W/build-wasm"

if [ ! -f "$B/src/libatari800.a" ]; then
    mkdir -p "$B"
    (cd "$REPO" && [ -f configure ] || ./autogen.sh)
    # El dispositivo R: y NETSIO hablan con puertos serie y red del sistema:
    # en el navegador no existen y ademas no compilan con emscripten.
    (cd "$B" && emconfigure "$REPO/configure" --host=wasm32-unknown-emscripten \
        --target=libatari800 --disable-pngcodec --without-readline --without-zlib \
        --disable-netsio --disable-riodevice --disable-rnetwork --disable-rserial)
    (cd "$B" && emmake make -j8)
fi

emcc "$REPO/util/a8web.c" "$B/src/libatari800.a" \
  -I"$REPO/src" -I"$B/src" -O2 -o "$W/a8.js" \
  -sSINGLE_FILE=1 -sMODULARIZE=1 -sEXPORT_NAME=A8 -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS \
  -sEXPORTED_FUNCTIONS=_a8_init,_a8_frame,_a8_pixels,_a8_input,_a8_frame_number,_a8_load,_a8_load_tape,_a8_ram,_a8_encender,_a8_basic,_a8_especial,_a8_peek,_a8_tape_pos,_a8_tape_size,_a8_snd_freq,_a8_snd_channels,_a8_snd_ptr,_a8_snd_fill,_malloc \
  --embed-file "$W/romdata@/data"
ls -l "$W/a8.js"
