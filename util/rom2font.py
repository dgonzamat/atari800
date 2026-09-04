"""Convierte el juego de caracteres del 800XL en una fuente web de verdad.

La ROM del sistema lleva en $E000 las 128 formas de 8x8 puntos con las que la
maquina escribe todo: el READY del BASIC, los mensajes, los menus. Aqui se
sacan de ahi -no se imitan- y se arma con ellas un .woff, para que la pagina
escriba con la misma letra con la que escribia la maquina.

Cada punto encendido es un cuadrado. Los cuadrados vecinos comparten un lado;
si se dejaran los cuatro lados de cada uno, el contorno tendria costuras por
dentro. Por eso los lados repetidos se anulan de a pares y con los que quedan
se recorre el borde: sale un solo contorno limpio por mancha, con sus huecos.

Uso:  rom2font.py ATARIXL.ROM salida.woff [salida.ttf]

La ROM tiene que ser la del sistema operativo del XL/XE, de 16K, la misma
que atari800 recibe en -xlxe_rom.
"""
import pathlib, sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

CHARSET = 0x2000          # $E000 en una ROM de 16K que empieza en $C000

PX = 128                  # lo que mide un punto de la maquina
EM = 1024                 # 8 puntos de ancho
BASE = 6                  # la linea de escritura va bajo la fila 6


def interno(codigo):
    """El numero de forma en la ROM para un caracter ASCII."""
    if 0x20 <= codigo <= 0x5F:
        return codigo - 0x20
    if 0x60 <= codigo <= 0x7F:
        return codigo
    return None


def puntos(rom, forma):
    """Los puntos encendidos de una forma, como (columna, fila)."""
    ini = CHARSET + forma * 8
    fuera = set()
    for fila in range(8):
        b = rom[ini + fila]
        for col in range(8):
            if b & (1 << (7 - col)):
                fuera.add((col, fila))
    return fuera


def contornos(pts):
    """El borde de la mancha de puntos, ya sin costuras interiores.

    De cada cuadrado se apuntan sus cuatro lados con sentido; el lado que
    comparten dos cuadrados aparece dos veces en sentidos opuestos y los dos
    se van. Con los que sobran se encadena el recorrido.
    """
    cuenta = {}
    for col, fila in pts:
        x0, x1 = col * PX, (col + 1) * PX
        y1, y0 = (BASE - fila + 1) * PX, (BASE - fila) * PX
        for lado in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)),
                     ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
            cuenta[lado] = cuenta.get(lado, 0) + 1

    sueltos = {}
    for (a, b), n in cuenta.items():
        if cuenta.get((b, a)):          # el lado que comparten dos cuadrados
            continue
        for _ in range(n):
            sueltos.setdefault(a, []).append(b)

    salida = []
    while sueltos:
        a = next(iter(sueltos))
        camino = [a]
        while True:
            bs = sueltos.get(camino[-1])
            if not bs:
                break
            b = bs.pop()
            if not bs:
                del sueltos[camino[-1]]
            if b == camino[0]:
                break
            camino.append(b)
        if len(camino) >= 3:
            salida.append(recorta(camino))
    return salida


def recorta(camino):
    """Quita los puntos que estan en medio de una recta."""
    fuera = []
    n = len(camino)
    for i in range(n):
        ax, ay = camino[i - 1]
        bx, by = camino[i]
        cx, cy = camino[(i + 1) % n]
        if (bx - ax) * (cy - by) != (by - ay) * (cx - bx):
            fuera.append((bx, by))
    return fuera or camino


def main(argv):
    if len(argv) < 3:
        sys.exit("uso: rom2font.py ATARIXL.ROM salida.woff [salida.ttf]")
    rom = pathlib.Path(argv[1]).read_bytes()
    if len(rom) != 16384:
        sys.exit("la ROM no mide 16K: %s" % argv[1])

    orden = [".notdef"]
    cmap, dibujos, anchos = {}, {}, {}
    lapiz = TTGlyphPen(None)
    dibujos[".notdef"] = lapiz.glyph()
    anchos[".notdef"] = (EM, 0)

    for cod in range(0x20, 0x7F):
        forma = interno(cod)
        if forma is None:
            continue
        nombre = "uni%04X" % cod
        orden.append(nombre)
        cmap[cod] = nombre
        lapiz = TTGlyphPen(None)
        for c in contornos(puntos(rom, forma)):
            lapiz.moveTo(c[0])
            for p in c[1:]:
                lapiz.lineTo(p)
            lapiz.closePath()
        dibujos[nombre] = lapiz.glyph()
        anchos[nombre] = (EM, 0)

    fb = FontBuilder(EM, isTTF=True)
    fb.setupGlyphOrder(orden)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(dibujos)
    fb.setupHorizontalMetrics(anchos)
    fb.setupHorizontalHeader(ascent=(BASE + 1) * PX, descent=-(7 - BASE) * PX,
                             lineGap=0)
    fb.setupNameTable({
        "familyName": "Atari XL",
        "styleName": "Regular",
        "psName": "AtariXL-Regular",
        "version": "1.000",
        "uniqueFontIdentifier": "AtariXL-Regular;DAGA SOFT",
    })
    fb.setupOS2(sTypoAscender=(BASE + 1) * PX, sTypoDescender=-(7 - BASE) * PX,
                sTypoLineGap=0, usWinAscent=(BASE + 1) * PX,
                usWinDescent=(7 - BASE) * PX, achVendID="DAGA")
    fb.setupPost(isFixedPitch=1)

    if len(argv) > 3:                 # el .ttf sale solo si lo piden
        fb.save(argv[3])
        print("%s: %d bytes" % (argv[3], pathlib.Path(argv[3]).stat().st_size))
    fb.font.flavor = "woff"
    fb.save(argv[2])
    print("glifos: %d" % (len(orden) - 1))
    print("%s: %d bytes" % (argv[2], pathlib.Path(argv[2]).stat().st_size))


if __name__ == "__main__":
    main(sys.argv)
