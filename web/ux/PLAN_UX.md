# Plan de mejoras de UI/UX — Biblioteca Atari XL/XE

Este documento es la orden de trabajo para la sesión que ejecute las mejoras.
Está escrito para alguien que no vio la conversación anterior: trae el contexto,
lo que se midió, qué hacer en qué orden y cómo saber que quedó bien.

## 1. Qué es y dónde está

- **La aplicación** es una sola página HTML (12 MB) con un emulador Atari 8 bits
  (libatari800 compilado a WASM) adentro. Se publica como artifact en
  `https://claude.ai/artifact/P8rnK8Psbuh7hRRex8j8Yy` y se empaqueta como APK
  Android (`web/android/`, un WebView que abre la misma página).
- **Quién la usa:** una persona, en Chile, que escribe en español de Chile y la
  usa en el computador (dentro de claude.ai) y en el teléfono (APK).
- **La página no está en el repositorio.** La única copia es el artifact. Los
  scripts que la armaban por partes se perdieron en un reinicio del entorno, así
  que **desde ahora se edita el HTML directamente**.

### Cómo recuperar la página para trabajar

1. `Artifact` con `action: "read"` y la URL de arriba. Guarda el HTML completo
   en un archivo de `tool-results` y dice la ruta.
2. Quitarle el envoltorio: lo que hay hasta el primer `<body>\n` inclusive y el
   `</body></html>` final. Lo que queda es la página (`atari.html`).
3. Servirla: `python3.13 -m http.server 8920` (en segundo plano).
4. Probar con Playwright: **`python3.13`** es el intérprete que lo tiene
   (`python3` a secas no). Chromium está en `/opt/pw-browsers/chromium`; lanzar
   con `--use-gl=swiftshader --enable-unsafe-swiftshader`.

### Reglas que no se rompen

- **Solo ASCII en el HTML.** Los acentos van como entidades (`&#225;`) en HTML y
  como `á` en JS. Se verifica con
  `LC_ALL=C grep -c '[^ -~\t]' atari.html`, que debe imprimir `0`.
- **No tocar** el bloque del emulador (de `var A8 = (() => {` a
  `exports["A8"] = A8;`) ni `EMBEDDED_FILES`. Este plan no necesita recompilar
  el WASM: es solo HTML, CSS y JS de la página.
- **Publicar siempre al mismo artifact** (parámetro `url`), **sin pasar
  `capabilities`**: así se conservan `assets` y `downloads`. Ahí están guardados
  148 PDF (983 MB) que usa el visor; no borrar ninguno.
- **El mundo visual se conserva.** La página es un objeto: la mesa clara, el
  televisor, la carcasa del Atari, las fichas oscuras, la letra de la ROM. Las
  mejoras son de orden, tamaño y flujo, no un rediseño.
- **Español de Chile, de tú.** Nada de voseo.
- Las tareas de abajo se hacen **por fases, publicando al final de cada una**,
  para que el usuario pueda probar y corregir el rumbo.

## 2. Cómo se midió

`web/ux/audita.py` recorre cada estado de la página en computador (1280×800) y
en teléfono (390×844, táctil), saca una foto de cada uno y mide alto de página,
blancos de toque menores de 40 px y tamaños de letra. Volver a correrlo después
de cada fase y comparar con esta tabla.

| Estado | Computador | Teléfono |
|---|---|---|
| 1 Modelo | 2,0 pantallas | 1,7 pantallas |
| 2 Joystick | 1,3 | 1,2 |
| 3 Drive | 1,5 | 1,4 |
| 4 Juego (discos, 115 títulos) | 10,6 | 17,3 |
| 4 Juego (cintas) | 11,6 | 23,6 |
| Jugando | 2,4 | 1,4 |
| BASIC | 14,7 | **31,4** |

En BASIC, en el teléfono, "Manuales originales" mide 7.242 px y "Revistas de la
época" 16.899 px.

## 3. Lo que se encontró

Ordenado por cuánto estorba. Cada punto está medido o visto en una captura.

### A. Errores (se arreglan sin decidir nada)

1. **La ayuda de los pasos queda pegada en el teléfono.** Al tocar un paso (por
   ejemplo BASIC), su cartel azul de ayuda queda visible encima del televisor y
   tapa las primeras líneas de la pantalla, justo donde aparece `Ready`. En
   táctil no hay "pasar por encima": el cartel solo debe salir con mouse
   (`@media (hover:hover)`) o con foco de teclado.
2. **El botón FUEGO se sale de la pantalla** en el teléfono: su aro queda 4 px
   fuera del borde derecho.
3. **El rótulo de la disquetera dice "D1 D1 · River Raid"** (el "D1" repetido).
4. **Voseo:** "Tocá para encender el sonido", "Arrastrá el dedo sobre la
   palanca", "elegí una cinta". Pasar a "Toca", "Arrastra", "elige". Revisar
   todo el texto visible por si hay más.

### B. Jugar en el teléfono

5. **START, SELECT, OPTION y RESET miden 27×15 px y quedan 560 px debajo del
   televisor** (en y=916 con la pantalla terminando en y=357; el teléfono mide
   844). Muchos juegos no empiezan sin START. Es el problema más grave para
   jugar en el teléfono.
6. **La foto de la disquetera (239 px de alto) se queda mientras se juega**,
   entre el mando y el teclado. Ahí es decoración que empuja lo útil.
7. **Los botones bajo el televisor miden 21 px de alto** ("Líneas", "Sonido:
   on", "Mando", "Pantalla completa") y los pasos, 30 px. El mínimo cómodo para
   un dedo es 44 px.
8. **Esos cuatro botones muestran su estado de tres maneras distintas:** uno se
   pinta amarillo, otro lo dice en el texto ("Sonido: on") y otro no lo dice.

### C. Programar en BASIC en el teléfono

9. **La línea para escribir queda al fondo de la pantalla** (y=799 de 844),
   separada del televisor por el teclado del Atari. Con el teclado del teléfono
   abierto no se ven a la vez lo que se escribe y la pantalla del Atari.
10. **El teclado del Atari en pantalla no se puede usar en el teléfono:** teclas
    de 26×27 px con letra de 5 px.
11. **Tocar "Escribirlo en el Atari" no muestra nada.** El botón puede estar 30
    pantallas más abajo que el televisor (medido: scroll en 25.656 px, televisor
    fuera de la vista). El programa se escribe, pero el usuario no lo ve ni
    recibe aviso.

### D. Páginas demasiado largas

12. **BASIC mide 31 pantallas en el teléfono** (15 en el computador): el
    disquete, los ejemplos, la chuleta, 9 manuales, 19 programas de manuales, 55
    de revistas y una hemeroteca de 126 números, todo en una sola columna y cada
    programa con su descripción completa desplegada.
13. **La estantería de juegos mide 17 a 24 pantallas en el teléfono:** 115
    fichas de disquete grandes, de a dos por fila. El buscador y el abecedario
    quedan arriba y se pierden al bajar.

### E. El recorrido

14. **Para llegar a un juego hay que pasar siempre por los tres pasos
    anteriores.** Los pasos 2, 3 y 4 están bloqueados al abrir la página, aunque
    los valores por defecto sirven. El modelo se recuerda entre visitas; el
    joystick y el drive, no.
15. **En los pasos 2, 3 y 4 el televisor ocupa la primera pantalla** mostrando
    el SELF TEST, que no tiene que ver con lo que se está eligiendo. Lo que hay
    que elegir queda debajo: en el teléfono el buscador de juegos empieza en
    y≈560 de 844, y en el computador las fotos de cinta y disco empiezan en
    y≈770 de 800.

### F. Legibilidad

16. **Texto demasiado chico:** 5 a 7 px en las teclas, 9 px en las etiquetas de
    los disquetes (115 veces), 10 px en la regleta de estado.
17. **Texto con poco contraste sobre el papel claro:** los rótulos "CINTA sin
    cinta" y "D1 sin disco" están en 2,7:1 (lo legible empieza en 4,5:1).

### G. En el computador

18. **En BASIC, el teclado dibujado ocupa media pantalla** aunque se escribe
    con el teclado de verdad. Los paneles de programas empiezan recién en y=820
    de 800.

## 4. El plan

Cinco fases. Cada una se publica y se le avisa al usuario qué probar.

### Fase 1 — Errores y tamaños de dedo (rápida, sin decisiones)

- Arreglar los puntos 1 a 4.
- En táctil (`body.touch`), llevar a **44 px de alto mínimo** los botones bajo
  el televisor, los pasos, "Volver", y los botones de las fichas.
- Unificar los cuatro botones bajo el televisor (punto 8): todos como
  interruptor con `aria-pressed`, el mismo aspecto para "encendido", y el texto
  sin el estado adentro ("Sonido", no "Sonido: on").
- Subir el contraste de los rótulos sobre papel a 4,5:1 o más (punto 17).

**Queda bien si:** en el teléfono no hay blancos de toque menores de 44 px fuera
del teclado del Atari; el cartel de ayuda no aparece al tocar; FUEGO entero
dentro de la pantalla.

### Fase 2 — Jugar en el teléfono

- **Las teclas de consola (START, SELECT, OPTION, RESET, HELP) van en una fila
  justo debajo del televisor mientras se juega**, de 44 px de alto o más. Ya
  existe una versión así para pantalla completa (`body.inmersivo .btns`): usar
  la misma idea en vertical sin pantalla completa.
- Orden mientras se juega, en vertical: televisor → teclas de consola → mando y
  FUEGO → tira del soporte → teclado.
- **La foto de la disquetera o la casetera se achica a una tira de una línea**
  mientras se juega (el piloto, el nombre del juego y, en cinta, sus botones y
  el contador). La foto grande sigue en el paso Drive.
- **El teclado del Atari queda plegado** tras un botón "Teclado" mientras se
  juega en el teléfono; casi ningún juego lo usa.

**Queda bien si:** con un juego andando en 390×844, START está visible sin
bajar y mide 44 px o más; televisor, consola, mando y FUEGO caben en la primera
pantalla.

### Fase 3 — Programar en BASIC

- **En el teléfono, la línea de escribir va pegada debajo del televisor**, y el
  conjunto televisor + línea queda fijo arriba (`position:sticky`) al bajar por
  los paneles.
- **El teclado del Atari se pliega en el teléfono** y en su lugar va una fila de
  teclas que el teclado del teléfono no tiene: BREAK, ESC, las cuatro flechas,
  CTRL, y las tres de consola.
- **Al tocar "Escribirlo en el Atari" o "Cargar":** subir hasta el televisor
  (scroll suave) y mostrar un aviso corto ("Escrito en el Atari. Escribe RUN
  para correrlo"). Con el televisor fijo arriba, el aviso alcanza.
- **En el computador:** dos columnas desde 1100 px de ancho: televisor fijo a la
  izquierda, paneles a la derecha. El teclado dibujado, plegado por defecto en
  BASIC (se escribe con el de verdad), con un botón para mostrarlo.

**Queda bien si:** en el teléfono, televisor y línea de escribir se ven juntos
con el teclado del teléfono abierto; tras "Escribirlo" se ve el listado
aparecer; en 1280×800 se ve "Tu disquete" sin bajar.

### Fase 4 — Acortar las páginas largas

- **BASIC en tres pestañas** bajo el televisor: **Programar** (tu disquete,
  ejemplos, chuleta), **Revistas**, **Manuales**. Se recuerda la última abierta.
- **Los programas como filas compactas:** portada chica, título y origen en una
  línea, y los botones. La descripción y el "cómo se juega" se abren al tocar la
  fila. Hoy cada tarjeta mide unos 300 px en el teléfono; el objetivo son 64 a
  72 px plegada.
- **Filtros por tipo** además de por país: Juegos, Gráficos, Sonido, Otros.
  Habrá que etiquetar los 74 programas (los datos están en los arreglos
  `REVISTAS` y `MAN_PROGS` de la página; se agrega un campo `tipo`).
- **Estantería de juegos en el teléfono:** vista de lista compacta (una fila por
  título: nombre, año y casa) como opción por defecto en pantallas angostas, con
  un botón para volver a las fichas de disquete. Buscador y abecedario fijos
  arriba al bajar.
- Subir el texto mínimo a 12 px (punto 16); en las fichas de disquete, 11 px
  como mínimo para año y casa.

**Queda bien si:** BASIC en el teléfono mide 6 pantallas o menos en cualquiera
de las tres pestañas con todo plegado; la estantería en lista, 8 o menos; no
queda texto menor de 11 px fuera del teclado dibujado.

### Fase 5 — El recorrido

Esta fase cambia cómo se entra a jugar. **Confirmar con el usuario antes de
hacerla** (ver preguntas abajo).

- **No bloquear los pasos.** Se puede tocar cualquiera desde el principio; los
  valores por defecto (65XE, CX40) ya sirven.
- **Recordar joystick y drive** igual que el modelo (`localStorage`, con
  `try/catch`).
- **Quien ya eligió antes entra directo a la estantería** (paso 4) con lo que
  tenía. Quien entra por primera vez ve el paso 1.
- **En los pasos 2, 3 y 4 el televisor se achica** a una franja (o se oculta en
  el teléfono) para que la elección quede en la primera pantalla. Vuelve a su
  tamaño al jugar, en el paso 1 y en BASIC.
- En el paso Drive, en el teléfono, la casetera y la disquetera lado a lado.

**Queda bien si:** un usuario que vuelve llega a tocar un juego con un toque
(más el del juego); en los pasos 2 a 4 lo que se elige se ve sin bajar en
390×844 y en 1280×800.

## 5. Preguntas para el usuario

Hacerlas antes de la fase que corresponde; no frenan las fases 1 a 3.

1. **Fase 4:** ¿de acuerdo con partir BASIC en tres pestañas (Programar,
   Revistas, Manuales)?
2. **Fase 4:** en el teléfono, ¿la estantería en lista compacta por defecto, o
   prefiere seguir viendo las fichas de disquete?
3. **Fase 5:** ¿de acuerdo con que quien ya eligió modelo, joystick y drive
   entre directo a la estantería?

## 6. Pruebas antes de cada publicación

Los scripts de prueba anteriores se perdieron; hay que rehacerlos (son cortos).
Como mínimo, con Playwright:

- **Disco:** elegir River Raid, esperar 8 s, la pantalla tiene más de 15 colores
  distintos.
- **Cinta:** elegir Zorro, comprobar que arranca la carga.
- **BASIC:** entrar, escribir `10 FOR I=1 TO 3:PRINT "HOLA LOLA ";I:NEXT I`,
  `RUN`, `SAVE "D:HOLA.BAS"`; aparece en "Tu disquete"; recargar la página y
  sigue ahí.
- **Un programa de revista:** "Escribirlo en el Atari", `RUN`, corre.
- **Cambio de modelo:** 600XL muestra "Atari 600XL / 16K"; BASIC en el 600XL
  dice "/ 16K / BASIC".
- **Visor:** abre un manual y muestra el número de páginas (en local los PDF de
  `/_blob/` no existen: basta con que abra y no dé error de JavaScript).
- **Sin errores de JavaScript** en ninguna de las anteriores, en 1280×800 y en
  390×844 táctil.
- `audita.py` y comparar con la tabla de la sección 2.

Lo que no se puede probar desde el entorno, y hay que pedirle al usuario: la
página dentro de claude.ai (sobre todo el visor de PDF) y el APK en un teléfono
real.

## 7. El APK

- No hace falta para las fases 1 a 5 mientras el usuario pruebe en el artifact;
  conviene armarlo **una vez al final**, o cuando el usuario lo pida.
- El entorno perdió el SDK de Android y **la clave de firma**. Para armarlo:
  reinstalar el SDK (cmdline-tools, plataforma 34, **build-tools 35.0.0**) y
  **pedirle al usuario el archivo `atarixl-firma.keystore`** que se le envió.
  Ponerlo en `~/.atarixl/firma.keystore`. Con otra clave el APK no se instala
  encima del que ya tiene (habría que desinstalar).
- Armar con `ANDROID_HOME=... web/android/construir.sh atari.html salida.apk`.
  Subir `versionCode` y `versionName` en `web/android/AndroidManifest.xml` (hoy
  10 y 10.0).
- El APK no lleva PDF; ahí el visor trae las páginas de archive.org.

## 8. Resultado (ejecutado)

Las cinco fases se aplicaron con `aplica_ux.py` sobre la pagina publicada
(version 35 del artifact). `prueba.py` corre 47 pruebas de regresion y de
las metas de este plan: 47 OK, 0 fallas, sin errores de JS.

| Estado | Computador antes / despues | Telefono antes / despues |
|---|---|---|
| 1 Modelo | 2,0 / 2,0 | 1,7 / 1,8 |
| 2 Joystick | 1,3 / 1,0 | 1,2 / 1,1 |
| 3 Drive | 1,5 / 1,3 | 1,4 / 1,0 |
| 4 Juego (discos) | 10,6 / 10,4 | 17,3 / 7,9 |
| 4 Juego (cintas) | 11,6 / 11,4 | 23,6 / 7,7 |
| Jugando | 2,4 / 2,4 | 1,4 / 1,0 |
| BASIC | 14,7 / 1,2 (columna con scroll propio) | 31,4 / 2,8 (Revistas 5,9) |

En el telefono ya no quedan blancos de toque de menos de 40px fuera del
teclado del Atari (que ahora va plegado).
