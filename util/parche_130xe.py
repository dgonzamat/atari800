"""Parche del SELF TEST: que la prueba de memoria recorra tambien los 64K
extendidos del 130XE, en 7 filas de cuadros en vez de 3.

Como lo hace (sin copiar la prueba: reutiliza la del original)
  - $5295  entrada a la prueba: pone la fase en 0 (memoria base).
  - $530D  antes de probar cada bloque el original escribe en PORTB los
           bits de los LEDs del 1200XL. En la fase de bancos, ahi se pone el
           banco que toca, con la CPU mirando la memoria extendida.
  - $5371  fin de pasada: en vez de terminar al llegar a RAMSIZ, si hay
           memoria extendida vuelve a recorrer la ventana $4000-$7FFF con
           cada uno de los cuatro bancos, y recien ahi termina.
  - $51E7  la lista de display: tres lineas en blanco y un salto al byte
           siguiente (que no hacia nada) pasan a ser tres filas mas de
           cuadros. En 16K y 64K esas filas quedan vacias: no se ven.
Los bloques $5000-$57FF de cada banco quedan tapados por la ROM del propio
autochequeo, igual que en la memoria base: se marcan como el original marca
los suyos, sin probarlos.

Al final se rehacen las dos sumas de control de la ROM, que la prueba de ROM
compara: ya venian sin cuadrar por parches anteriores.

Uso:  parche_130xe.py ROM-entrada ROM-salida
"""
import sys

LIBRE = 0xCB65                  # hueco de ceros hasta $CBFF ($CC00 es el
FIN_LIBRE = 0xCC00              # juego de caracteres internacional)

OPS = {  # (mnemonico, modo) -> codigo
    ("LDA", "#"): 0xA9, ("LDA", "zp"): 0xA5, ("LDA", "abs"): 0xAD,
    ("STA", "zp"): 0x85, ("STA", "abs"): 0x8D,
    ("LDX", "zp"): 0xA6, ("STX", "zp"): 0x86,
    ("ORA", "#"): 0x09, ("ORA", "zp"): 0x05, ("AND", "#"): 0x29,
    ("CMP", "#"): 0xC9, ("CMP", "abs"): 0xCD, ("CPX", "#"): 0xE0,
    ("PHA", ""): 0x48, ("PLA", ""): 0x68, ("INX", ""): 0xE8, ("DEX", ""): 0xCA,
    ("TXA", ""): 0x8A, ("ASL", ""): 0x0A, ("RTS", ""): 0x60,
    ("JSR", "abs"): 0x20, ("JMP", "abs"): 0x4C,
    ("BNE", "rel"): 0xD0, ("BEQ", "rel"): 0xF0, ("BCS", "rel"): 0xB0, ("BCC", "rel"): 0x90,
}
LARGO = {"": 1, "#": 2, "zp": 2, "rel": 2, "abs": 3}

PORTB, RAMSIZ, FASE = 0xD301, 0x02E4, 0xA6

# (etiqueta, mnemonico, modo, operando)  - operando: numero o nombre de etiqueta
CODIGO = [
    # --- a la entrada de la prueba: fase 0, y lo que hacia el original ---
    ("ENTRA", "LDA", "#", 0x00),
    (None, "STA", "zp", FASE),
    (None, "JMP", "abs", 0x509E),

    # --- hay memoria extendida? C=0 si la hay ---------------------------
    # Se escribe 0 en $4000 de la memoria base, se pone el banco 0 a la
    # vista de la CPU y se escribe $FF; con la base otra vez a la vista, si
    # $4000 sigue en 0 es que el $FF fue a parar a un banco.
    ("HAYXE", "LDA", "abs", PORTB),
    (None, "ORA", "#", 0x10),
    (None, "STA", "abs", PORTB),          # la base a la vista
    (None, "PHA", "", None),
    (None, "LDA", "#", 0x00),
    (None, "STA", "abs", 0x4000),
    (None, "PLA", "", None),
    (None, "PHA", "", None),
    (None, "AND", "#", 0xE3),
    (None, "STA", "abs", PORTB),          # banco 0 para la CPU
    (None, "LDA", "#", 0xFF),
    (None, "STA", "abs", 0x4000),
    (None, "PLA", "", None),
    (None, "STA", "abs", PORTB),          # la base otra vez
    (None, "LDA", "abs", 0x4000),
    (None, "CMP", "#", 0x01),
    (None, "RTS", "", None),

    # --- en vez de CMP RAMSIZ al final de cada bloque -------------------
    # Entra con A = la pagina siguiente. Devuelve Z=0 para seguir, Z=1
    # cuando se termino la pasada entera.
    ("FIN", "LDX", "zp", FASE),
    (None, "BNE", "rel", "BANCO"),
    (None, "CMP", "abs", RAMSIZ),         # memoria base: lo del original
    (None, "BNE", "rel", "VUELVE"),
    (None, "JSR", "abs", "HAYXE"),
    (None, "BCS", "rel", "TERMINA"),      # sin memoria extendida: fin
    (None, "INX", "", None),              # fase 1: el primer banco
    ("NUEVO", "STX", "zp", FASE),
    (None, "LDA", "#", 0x40),             # otra vez desde $4000
    (None, "STA", "zp", 0x8F),
    (None, "STA", "zp", 0x91),
    ("VUELVE", "RTS", "", None),
    ("BANCO", "CMP", "#", 0x80),          # fin de la ventana $4000-$7FFF?
    (None, "BNE", "rel", "VUELVE"),
    (None, "INX", "", None),
    (None, "CPX", "#", 0x05),             # cuatro bancos
    (None, "BNE", "rel", "NUEVO"),
    ("TERMINA", "LDA", "abs", PORTB),
    (None, "ORA", "#", 0x10),
    (None, "STA", "abs", PORTB),          # la memoria base a la vista
    (None, "LDA", "#", 0x00),
    (None, "STA", "zp", FASE),            # Z=1: se termino
    (None, "RTS", "", None),

    # --- en vez de los LEDs, en la fase de bancos: el banco que toca ----
    ("LEDS", "LDX", "zp", FASE),
    (None, "BEQ", "rel", "ORIGINAL"),
    (None, "DEX", "", None),
    (None, "TXA", "", None),
    (None, "ASL", "", None),
    (None, "ASL", "", None),
    (None, "STA", "zp", 0xA5),
    (None, "LDA", "abs", PORTB),
    (None, "AND", "#", 0xE3),             # bit 4 en 0: la CPU ve el banco
    (None, "ORA", "zp", 0xA5),
    (None, "STA", "abs", PORTB),
    (None, "RTS", "", None),
    ("ORIGINAL", "JMP", "abs", 0x53A4),
]


def ensambla(codigo, origen):
    etiquetas, pc = {}, origen
    for et, op, modo, _ in codigo:
        if et: etiquetas[et] = pc
        pc += LARGO[modo]
    salida, pc = bytearray(), origen
    for et, op, modo, arg in codigo:
        v = etiquetas[arg] if isinstance(arg, str) else arg
        salida.append(OPS[(op, modo)])
        if modo in ("#", "zp"):
            salida.append(v & 0xFF)
        elif modo == "abs":
            salida += bytes((v & 0xFF, v >> 8))
        elif modo == "rel":
            d = v - (pc + 2)
            if not -128 <= d <= 127: sys.exit("salto fuera de alcance en %s" % op)
            salida.append(d & 0xFF)
        pc += LARGO[modo]
    return bytes(salida), etiquetas


def off(a):
    """De direccion de CPU a posicion en el fichero de 16K."""
    if 0x5000 <= a < 0x5800: return 0x1000 + a - 0x5000
    if 0xC000 <= a <= 0xFFFF: return a - 0xC000
    raise ValueError(hex(a))


def pisa(rom, dir_, esperado, nuevo):
    o = off(dir_)
    if bytes(rom[o:o + len(esperado)]) != bytes(esperado):
        sys.exit("en $%04X esperaba %s y hay %s: no es la ROM que se estudio"
                 % (dir_, bytes(esperado).hex(), bytes(rom[o:o + len(esperado)]).hex()))
    rom[o:o + len(nuevo)] = nuevo


def suma(rom, tramos):
    s = 0
    for d, h in tramos:
        for a in range(d, h):
            s += rom[off(a)]
    return s & 0xFFFF


def main(entrada, salida):
    rom = bytearray(open(entrada, "rb").read())
    assert len(rom) == 16384
    cod, et = ensambla(CODIGO, LIBRE)
    if LIBRE + len(cod) > FIN_LIBRE:
        sys.exit("no entra: %d bytes" % len(cod))
    if any(rom[off(LIBRE):off(LIBRE) + len(cod)]):
        sys.exit("el hueco no esta vacio")
    rom[off(LIBRE):off(LIBRE) + len(cod)] = cod

    j = lambda n: bytes((0x20, et[n] & 0xFF, et[n] >> 8))
    pisa(rom, 0x5295, b"\xA9\x00\x20\x9E\x50", j("ENTRA") + b"\xEA\xEA")
    pisa(rom, 0x530D, b"\x20\xA4\x53", j("LEDS"))
    pisa(rom, 0x5371, b"\xCD\xE4\x02\xD0\x81", j("FIN") + b"\xD0\x81")
    pisa(rom, 0x51E7, b"\x70\x70\x70\x01\xED\x51", b"\x70\x08\x70\x08\x70\x08")

    s1 = suma(rom, [(0xC002, 0xD000), (0x5000, 0x5800), (0xD800, 0xE000)])
    s2 = suma(rom, [(0xE000, 0xFFF8), (0xFFFA, 0x10000)])
    rom[off(0xC000)], rom[off(0xC001)] = s1 & 0xFF, s1 >> 8
    rom[off(0xFFF8)], rom[off(0xFFF9)] = s2 & 0xFF, s2 >> 8
    open(salida, "wb").write(rom)
    print("codigo: %d bytes en $%04X-$%04X (quedan %d libres)"
          % (len(cod), LIBRE, LIBRE + len(cod) - 1, FIN_LIBRE - LIBRE - len(cod)))
    for n in ("ENTRA", "HAYXE", "FIN", "LEDS"):
        print("  %-6s $%04X" % (n, et[n]))
    print("sumas de control: $C000=%04X  $FFF8=%04X" % (s1, s2))


if __name__ == "__main__":
    main(*sys.argv[1:3])
