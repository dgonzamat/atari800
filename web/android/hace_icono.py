"""Dibuja el icono de la aplicacion en las cinco densidades de Android.

Nada de logos ajenos: el fondo es el plastico oscuro de la mesa, abajo va la
banda de cinco franjas que el 800XL lleva serigrafiada junto a las teclas de
funcion, y las letras "XL" salen de la ROM del sistema -las mismas formas de
8x8 puntos con las que la maquina escribe-, agrandadas sin suavizar.

Uso:  hace_icono.py ATARIXL.ROM res/
"""
import pathlib, sys
from PIL import Image, ImageDraw

FONDO = (35, 32, 29)
CREMA = (236, 228, 214)
FRANJAS = [(200, 64, 47), (224, 123, 44), (223, 178, 58), (79, 154, 94), (59, 120, 189)]
DENSIDADES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
CHARSET = 0x2000                    # $E000 en la ROM de 16K


def glifo(rom, letra):
    forma = ord(letra) - 0x20       # ASCII $20-$5F -> codigo interno $00-$3F
    return rom[CHARSET + forma * 8: CHARSET + forma * 8 + 8]


def icono(rom, lado):
    g = 4                           # se dibuja grande y se reduce al final
    L = lado * g
    im = Image.new("RGBA", (L, L), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, L - 1, L - 1], radius=L // 6, fill=FONDO)
    # las cinco franjas, abajo
    alto = L // 16
    y0 = L - L // 5
    for k, c in enumerate(FRANJAS):
        d.rectangle([L // 8, y0 + k * alto, L - L // 8, y0 + (k + 1) * alto - 1], fill=c)
    # "XL" con la letra de la ROM: dos caracteres de 8 puntos
    punto = (L * 3 // 4) // 16
    x0 = (L - punto * 16) // 2
    ytop = L // 7
    for n, letra in enumerate("XL"):
        for fila, byte in enumerate(glifo(rom, letra)):
            for col in range(8):
                if byte & (0x80 >> col):
                    x = x0 + (n * 8 + col) * punto
                    y = ytop + fila * punto
                    d.rectangle([x, y, x + punto - 1, y + punto - 1], fill=CREMA)
    return im.resize((lado, lado), Image.LANCZOS)


def main(ruta_rom, res):
    rom = pathlib.Path(ruta_rom).read_bytes()
    if len(rom) != 16384:
        sys.exit("la ROM tiene que ser la del XL/XE, de 16K")
    for dens, lado in DENSIDADES.items():
        dest = pathlib.Path(res) / ("mipmap-" + dens)
        dest.mkdir(parents=True, exist_ok=True)
        icono(rom, lado).save(dest / "ic_launcher.png")
        print(dens, lado)


if __name__ == "__main__":
    main(*sys.argv[1:3])
