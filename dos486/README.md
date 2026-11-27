# DOS 486 – FreeDOS para juegos y programas de los años 80 y 90

Sistema DOS listo para arrancar en un PC **486 emulado**, con menú de juegos y
programas. Usa **FreeDOS 1.3** (DOS real y libre) como sistema operativo; no se
escribe un kernel nuevo.

> Este directorio es independiente del emulador Atari 800 del resto del repositorio.

## Qué construye

`./build.sh` crea `dos486.img`: disco de 128 MB (FAT16, MBR, geometría 16×63) con
FreeDOS 1.3, controladores (HIMEMX, JEMMEX, CTMOUSE, CWSDPMI) y los programas
de abajo. Descarga y **verifica SHA-256** de cada archivo.

| Arranque (menú FDCONFIG.SYS) | Memoria |
|---|---|
| 1. Modo juegos (por defecto) | XMS + UMB (HIMEMX) |
| 2. Modo EMS | JEMMEX: XMS + EMS + VCPI + UMB |
| 3. Modo mínimo | sin controladores de memoria |

Hardware emulado: CPU 486, 32 MB RAM, VGA Cirrus (QEMU), Sound Blaster 16
(`A220 I5 D1 H5`) + AdLib, ratón (CTMOUSE).

## Catálogo (shareware / freeware descargado de archive.org)

| Opción | Título | Fuente (archive.org) |
|---|---|---|
| 1 | DOOM 1.9 (shareware) | `DoomsharewareEpisode` |
| 2 | Wolfenstein 3D 1.4 (shareware) | `wolf3dsw` |
| 3 | Duke Nukem 3D 1.3D (shareware) | `duke-3d-sw` |
| 4 | Duke Nukem II (shareware) | `msdos_DUKE2_shareware` |
| 5 | Commander Keen 1 | `keen1-sw` |
| 6 | Commander Keen 4 (shareware) | `Keen4e-sw` |
| 7 | Dangerous Dave | `msdos_DDAVEVGA_shareware` |
| 8 | Scorched Earth 1.5 (shareware) | `msdos_SCORCH15_shareware` |
| 9 | Epic Pinball (shareware) | `epic_pinball_11` |
| A | SkyRoads | `SkyRoads` |
| B | ZZT 3.2 (shareware) | `ZztByEpicMegagames` |
| C | Heretic (shareware) | `hereticsw` |
| F | Lemmings (demo oficial, 4 niveles; introducción en alemán) | `Lemmings_1020` |
| G | Prince of Persia (demo oficial) | `PrinceOfPersia_479` |
| H | Stunts (demo oficial) | `stunts_demo_disk` |
| D | Turbo Pascal 5.5 | `TurboPascal55` |
| E | EDIT (editor de FreeDOS) | incluido en FreeDOS |

**Licencias:** el repositorio no contiene ningún juego; se bajan al ejecutar
`build.sh`. Se eligieron títulos publicados como shareware/freeware según su
ficha en archive.org, dato que no está verificado de forma independiente: revisa
la licencia de cada uno antes de redistribuir la imagen resultante.
**Carmageddon no está incluido:** su demo exige un Pentium 90 con 8–16 MB de RAM y
ocupa 40 MB; en el 486 emulado no es jugable.

## Uso

Dependencias (Debian/Ubuntu):

```bash
sudo apt install mtools dosfstools unzip curl python3 qemu-system-x86 dosbox
```

```bash
./build.sh          # descarga, verifica y crea dos486.img
./run.sh            # PC 486 completo con FreeDOS (QEMU)
./run-dosbox.sh     # mismos juegos con DOSBox (CPU 486)
```

Opciones: `RAM=16 ./run.sh`, `AUDIO=pa ./run.sh`, `CYCLES=max ./run-dosbox.sh`.
Cambia la velocidad en DOSBox con `Ctrl+F11` / `Ctrl+F12`. Dentro de DOS,
`JUEGOS` vuelve al menú y cada programa tiene su comando en `C:\` (`DOOM`,
`WOLF3D`, `DUKE3D`, `KEEN1`, `TP`...; lista en `config/COMMANDS.LST`).

## Versión web (navegador)

`web/build-web.sh` (después de `./build.sh`) genera `web/out/`: dos discos de 32 MB
(C: FreeDOS + juegos, D: Duke Nukem 3D + Heretic) guardados como texto base64 de
la imagen gzip, la configuración de DOSBox y el emulador (paquete npm `emulators`
8.5.3, núcleo DOSBox-X en WebAssembly, verificado por SHA-256).

La página `web/index.html` tiene solo la pantalla y los comandos preseteados.
Al pulsar Encender suenan, sintetizados con Web Audio (no son grabaciones), el relé
de la fuente, el ventilador, el disco duro acelerando, la disquetera buscando y el
beep del POST; la pantalla muestra un POST de BIOS simulado. Luego arranca FreeDOS
real desde el disco C: (sin menús); el disco duro suena al cargar cada programa y cada botón escribe su comando
en DOS (`DOOM`, `WOLF3D`, `KEEN1`, `TP`...). Los botones se activan cuando
`FDAUTO.BAT` dibuja una barra sólida que la página detecta en la imagen.

```bash
web/build-web.sh
```

```bash
cd web/out && python3 -m http.server 8000
```

Probado en Chromium (Playwright), pantalla inicial de cada uno: los 12 juegos,
Turbo Pascal 5.5 y EDIT cargan desde su botón. En una de siete pruebas de ZZT el
emulador se cayó ("null function") y no se repitió; causa no identificada.
El núcleo DOSBox clásico (`wdosbox`) no arranca el disco ("read error" en el MBR,
causa probable: sin soporte de INT 13h extendida); por eso se usa DOSBox-X.

## Estado de pruebas

Probado en este entorno (arranque y pantalla inicial de cada juego, sin jugar
partidas completas):

- **QEMU (`run.sh`)**: arranque FreeDOS, menú, Doom, Heretic, Commander Keen 1 y 4,
  Dangerous Dave, SkyRoads, Scorched Earth y ZZT muestran su pantalla. Epic Pinball
  se lanzaba con el ejecutable equivocado (corregido a `PINBALL.EXE`) y no se volvió
  a probar en QEMU.
- **Limitación de QEMU 8.2:** Wolfenstein 3D, Duke Nukem 3D y Duke Nukem II
  quedan en pantalla negra (el programa espera en el puerto `0x3DA`; QEMU emula
  el retrazo vertical de forma simplificada, **causa probable, no confirmada**).
  Esos tres funcionan con `./run-dosbox.sh`.
- **No probados en QEMU:** Turbo Pascal 5.5, EDIT, sonido real (se usó un backend de audio nulo),
  ratón y la opción de arranque 2 y 3 con los juegos.
- La velocidad en QEMU (TCG) no es la de un 486 real; en DOSBox se fija en
  26 800 ciclos, valor aproximado de un 486DX2‑66 (estimación).
- Duke Nukem 3D y Heretic traían configurada la IRQ 7 de la tarjeta de sonido;
  `build.sh` la cambia a IRQ 5 para coincidir con la tarjeta emulada.
