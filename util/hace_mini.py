"""Convierte una foto de maquina en una miniatura del mismo formato que las
que ya estan en la pagina: 264x150, la maquina recortada sobre blanco.

Las dos que ya habia -el 65XE y el 800XL- estan encuadradas asi: el aparato
ocupa casi todo el cuadro, con un margen blanco fino alrededor. Para que las
cuatro fichas se lean como una familia, las nuevas tienen que salir igual, y
eso no se consigue redimensionando la foto entera: hay que recortar primero
el blanco que sobra y despues encajar.

Uso:  hace_mini.py foto.jpg salida.jpg
"""
import pathlib, sys
from PIL import Image

ANCHO, ALTO = 264, 150
MARGEN = 4          # el aire que dejan las miniaturas que ya estaban
UMBRAL = 246        # por debajo de esto ya no es fondo blanco


def recorta(im):
    """El rectangulo que ocupa la maquina, sin el blanco de alrededor."""
    gris = im.convert("L")
    ancho, alto = gris.size
    px = gris.load()
    # se mira una de cada cuatro filas y columnas: sobra para encontrar el
    # borde y evita recorrer 11 millones de pixeles
    izq, der, arr, aba = ancho, 0, alto, 0
    for y in range(0, alto, 4):
        for x in range(0, ancho, 4):
            if px[x, y] < UMBRAL:
                if x < izq: izq = x
                if x > der: der = x
                if y < arr: arr = y
                if y > aba: aba = y
    if der <= izq or aba <= arr:
        return im
    return im.crop((max(0, izq - 8), max(0, arr - 8),
                    min(ancho, der + 8), min(alto, aba + 8)))


def main(entrada, salida):
    im = Image.open(entrada).convert("RGB")
    im = recorta(im)
    hueco = (ANCHO - MARGEN * 2, ALTO - MARGEN * 2)
    im.thumbnail(hueco, Image.LANCZOS)
    lienzo = Image.new("RGB", (ANCHO, ALTO), (255, 255, 255))
    lienzo.paste(im, ((ANCHO - im.width) // 2, (ALTO - im.height) // 2))
    lienzo.save(salida, "JPEG", quality=78, optimize=True)
    print("%s -> %s  %d bytes" % (entrada, salida,
                                  pathlib.Path(salida).stat().st_size))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit("uso: hace_mini.py foto.jpg salida.jpg")
    main(sys.argv[1], sys.argv[2])
