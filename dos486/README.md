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
| I | Carmageddon (demo oficial; pide Pentium, en la web va a toda la CPU) | `carmdemo` |
| J | Another World / Out of This World (demo oficial) | `OutOfThisWorldDemo` |
| D | Turbo Pascal 5.5 | `TurboPascal55` |
| K | Telix 3.21 shareware (terminal para BBS; 45 días de evaluación) | `TLX3211_ZIP` |
| E | EDIT (editor de FreeDOS) | incluido en FreeDOS |

**Licencias:** el repositorio no contiene ningún juego; se bajan al ejecutar
`build.sh`. Se eligieron títulos publicados como shareware/freeware según su
ficha en archive.org, dato que no está verificado de forma independiente: revisa
la licencia de cada uno antes de redistribuir la imagen resultante.
Las licencias de las demos (por ejemplo la de Out of This World) permiten copiarlas
gratis para uso personal y no comercial; no redistribuyas la imagen con fines comerciales.

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
de la fuente, el ventilador, el disco duro acelerando y la disquetera. La pantalla
muestra la BIOS real del emulador (DOSBox-X, `fastbioslogo=false`) y los mensajes
reales del núcleo de FreeDOS. Luego arranca FreeDOS
real desde el disco C: (sin menús); el disco duro suena al cargar cada programa y cada botón escribe su comando
en DOS (`DOOM`, `WOLF3D`, `KEEN1`, `TP`...). Los botones se activan cuando
`FDAUTO.BAT` dibuja una barra sólida que la página detecta en la imagen.

```bash
web/build-web.sh
```

```bash
cd web/out && python3 -m http.server 8000
```

Probado en Chromium (Playwright) el 27-11-2026, pantalla inicial de cada uno:
los 18 comandos (juegos, demos, Turbo Pascal y EDIT) cargan desde su botón. En una de siete pruebas de ZZT el
emulador se cayó ("null function") y no se repitió; causa no identificada.
El núcleo DOSBox clásico (`wdosbox`) no arranca el disco ("read error" en el MBR,
causa probable: sin soporte de INT 13h extendida); por eso se usa DOSBox-X.

## BBS reales por módem (versión de escritorio)

`./run-dosbox.sh` activa el módem emulado de DOSBox en COM1 (`serial1=modem`).
Al marcar `ATDT host` (o `ATDT host:puerto`) abre una conexión telnet real por
Internet. Escribe `TELIX` en DOS, responde las preguntas de configuración la
primera vez y, en el modo terminal, marca por ejemplo:

```
ATDT vert.synchro.net
```

BBS probados el 27-11-2026 a través del puente WebSocket de fTelnet (todos
respondieron con su pantalla de entrada):

| Grupo | BBS | Dirección |
|---|---|---|
| Internacional | Vertrauen | `vert.synchro.net` |
| Internacional | RetroBattlestations | `bbs.fozztexx.com` |
| Internacional | Electronic Chicken | `bbs.electronicchicken.com` |
| Internacional | Bottomless Abyss | `bbs.bottomlessabyss.net:2023` |
| Uruguay (.uy) | Undernet BBS | `undernet.uy` |
| Argentina (.ar) | Ragnarok's BBS | `crm.bertonmoreno.com.ar:23021` |
| Brasil (.br) | Mutiny Brazil | `bbs.retronet.com.br:2323` |
| Brasil (.br) | Soft Solutions | `bbs.softsolutions.net.br:2023` |
| Brasil (.br) | Clube da Insonia | `bbs.conf.eti.br` |
| España (castellano) | HispaMSX BBS | `bbs.hispamsx.org` |
| España (castellano) | Zruspa's BBS | `bbs.zruspas.org` |

El país de los latinoamericanos se deduce del dominio. No se encontró ningún BBS
chileno activo: el Telnet BBS Guide no lista ninguno y el único hallado (Araucaria
BBS, Ñuñoa) aparece en una lista de 2002 solo con número telefónico. `blackflag.acid.org` rechazó la
conexión ("BLOCKED"). La conexión directa por el módem de DOSBox no se pudo
probar desde el entorno de construcción, que bloquea telnet. Comprobado: Telix responde `OK` a `ATZ` con el módem
emulado y `ATDT` intenta la conexión.

En la versión web el botón `BBS` muestra un directorio de esos mismos BBS y conecta
de verdad por telnet a través del puente WebSocket público de fTelnet
(`wss://p-us-east.ftelnet.ca/<host>/<puerto>`, subprotocolo `binary`).

- **Página abierta directamente** (servidor propio o local): la sesión se ve dentro
  de la misma pantalla de DOS, con un terminal ANSI/telnet propio (CP437, colores,
  posición del cursor, negociación telnet con TTYPE=ANSI y NAWS 80x25). Probado
  el 27-11-2026 contra `vert.synchro.net`: el BBS lo reconoce como "Telnet,
  80x25 CP437 / ANSI" y se entra como Guest. `?proxy=ws://...` cambia el puente.
- **Dentro del visor de claude.ai**: el visor bloquea las conexiones externas, así
  que cada entrada abre la sesión en una pestaña nueva con el terminal de fTelnet. La página no puede conectarse por sí misma: el visor bloquea
las conexiones externas y el emulador en WebAssembly no tiene red. El sonido del
módem y la línea `CONNECT` de la página son solo ambientación del marcado.
Probado: el puente de fTelnet conectó con los cuatro BBS del directorio y devolvió
sus pantallas de entrada reales.

## Copihue BBS (BBS chileno dentro de DOS 486)

Tecla `C` del directorio de BBS. Un BBS propio que vive en la base de datos
compartida del artefacto (capacidad `db`), no un servidor telnet:

- alias por persona (`handles/<id>`; cada quien escribe solo el suyo),
- cuatro foros (General, Juegos, Computación retro, Cosas de Chile) en `msgs`,
  con borrado para el autor y el SysOp (dueño del artefacto),
- quién está conectado y chat en vivo con la capacidad `room` (el chat no se guarda),
- usuarios registrados y una pantalla "Acerca de".

Parte vacío: no trae mensajes ni usuarios inventados. Pueden escribir quienes
tengan el artefacto compartido como Contribuidor o más; con acceso de solo
lectura se entra como visitante. Probado con base de datos y sala simuladas.

## App instalada desde Claude (conexión directa)

El artefacto declara la capacidad `desktop` y el permiso `full-access`. Si se
instala como app desde Claude de escritorio y la persona acepta ese permiso, el
botón `BBS` abre la conexión telnet **directamente desde el PC** con un proceso
pequeño (PowerShell en Windows, Python 3 en Mac/Linux) que pasa los bytes en
líneas base64, sin pestaña ni puente de terceros. Probado el puente en Python
contra Vertrauen (con la API de escritorio simulada); el de PowerShell **no se
pudo probar** en el entorno de construcción.

La barra tiene además **Pantalla completa** (en Chrome, Esc llega al juego y se
mantiene presionado para salir) y **Reiniciar** (reinicio en frío del 486).

## App de escritorio (Electron)

`desktop-app/` empaqueta la versión web como aplicación con su propia ventana
(Electron 45.3.1). Un servidor local en 127.0.0.1 sirve los archivos y, como la
página no está dentro del visor de claude.ai, el BBS se conecta dentro de la misma
pantalla de DOS. F11 alterna pantalla completa.

```bash
web/build-web.sh
```

```bash
desktop-app/build-app.sh
```

`PLATFORM=linux` o `PLATFORM=darwin` generan las otras plataformas. El ejecutable
no está firmado: Windows SmartScreen pedirá "Más información → Ejecutar de todas
formas". Probado (versión Linux, en Xvfb): DOOM, Commander Keen 4 y BBS reales
(Vertrauen, Electronic Chicken) dentro de la ventana.

## LOADFIX

Another World (`AWDEMO.EXE`) viene comprimido con EXEPACK y en QEMU fallaba con
"Packed file is corrupt" al cargarse en los primeros 64 KB. FreeDOS Lite no trae
`LOADFIX`, así que `tools/loadfix.asm` es uno mínimo (ensamblado en
`tools/LOADFIX.COM` con `nasm -f bin`) que se copia a `C:\FREEDOS\BIN`.

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
