"""Aplica el plan de UI/UX (web/ux/PLAN_UX.md, fases 1 a 5) a la pagina
publicada del emulador.

    python3 aplica_ux.py entrada.html salida.html

La entrada es la pagina tal como esta publicada (recuperada con Artifact
read, sin el envoltorio). Cada cambio es un reemplazo exacto que tiene que
aparecer UNA vez: si la pagina cambio por otro lado, el script se detiene
en vez de parchear a ciegas. No toca el bloque del emulador ni los datos.
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
s = open(src, encoding="ascii").read()


def rep(viejo, nuevo, veces=1):
    global s
    n = s.count(viejo)
    if n != veces:
        sys.exit("esperaba %d y hay %d de: %r" % (veces, n, viejo[:90]))
    s = s.replace(viejo, nuevo)


# ---------------------------------------------------------------- marcado
# A4: tu y no vos
rep("Toc&#225; para encender el sonido", "Toca para encender el sonido")
rep("Gir&#225; el tel&#233;fono", "Gira el tel&#233;fono")
rep("Arrastr&#225; el dedo", "Arrastra el dedo")
rep('<span id="ctTitulo">eleg&#237; una cinta</span>', '<span id="ctTitulo">elige una cinta</span>')

# B8: los cuatro interruptores del televisor dicen su estado igual
rep('<button type="button" id="bSound">Sonido: on</button>',
    '<button type="button" id="bSound" aria-pressed="true" class="on">Sonido</button>')
rep('<button type="button" id="bPad">Mando</button>',
    '<button type="button" id="bPad" aria-pressed="true" class="on">Mando</button>')
rep('<button type="button" id="bFull">Pantalla completa</button>',
    '<button type="button" id="bFull" aria-pressed="false">Pantalla completa</button>')

# Fase 2/3: la consola debajo del televisor (y en BASIC, las teclas que el
# teclado del telefono no tiene)
rep("""    <!-- El orden en que se usaba una de estas maquinas: se encendia, se""",
    """    <!-- La consola a mano: debajo del televisor, donde se esta mirando.
         En el telefono el teclado del Atari se pliega y estas teclas son lo
         que queda; en BASIC se suman las que el teclado del telefono no
         tiene (BREAK, ESC, las flechas del cursor). -->
    <div class="consola-tv" id="consolaTv" role="group" aria-label="Teclas de la consola">
      <div class="ctv-fila ctv-basic" role="group" aria-label="Teclas especiales del Atari">
        <button type="button" data-k="brk" aria-label="Break">Brk</button>
        <button type="button" data-k="esc">Esc</button>
        <button type="button" data-k="izq" aria-label="Cursor a la izquierda">&#8592;</button>
        <button type="button" data-k="up" aria-label="Cursor arriba">&#8593;</button>
        <button type="button" data-k="dn" aria-label="Cursor abajo">&#8595;</button>
        <button type="button" data-k="der" aria-label="Cursor a la derecha">&#8594;</button>
        <button type="button" data-k="bs" aria-label="Borrar hacia atr&#225;s">BS</button>
        <button type="button" data-k="clr" aria-label="Limpiar la pantalla">Clr</button>
      </div>
      <div class="ctv-fila">
        <button type="button" class="cons" id="cStart">Start</button>
        <button type="button" class="cons" id="cSelect">Select</button>
        <button type="button" class="cons" id="cOption">Option</button>
        <button type="button" class="cons" id="cReset">Reset</button>
        <button type="button" id="bTeclado" aria-pressed="false">Teclado</button>
      </div>
    </div>

    <!-- El orden en que se usaba una de estas maquinas: se encendia, se""")

# Fase 4: el BASIC en tres pestanas
rep("""      <div class="basic">
        <section class="bas-caja" aria-labelledby="basT1">""",
    """      <div class="bas-tabs" id="basTabs" role="tablist" aria-label="Secciones del BASIC">
        <button type="button" role="tab" data-tab="prog" aria-selected="true">Programar</button>
        <button type="button" role="tab" data-tab="rev" aria-selected="false">Revistas</button>
        <button type="button" role="tab" data-tab="man" aria-selected="false">Manuales</button>
      </div>
      <div class="basic">
        <section class="bas-caja" data-tab="prog" aria-labelledby="basT1">""")
rep('<section class="bas-caja" aria-labelledby="basT2">',
    '<section class="bas-caja" data-tab="prog" aria-labelledby="basT2">')
rep('<section class="bas-caja" aria-labelledby="basT3">',
    '<section class="bas-caja" data-tab="prog" aria-labelledby="basT3">')
rep('<section class="bas-caja bas-manuales" aria-labelledby="basT5">',
    '<section class="bas-caja bas-manuales" data-tab="man" aria-labelledby="basT5">')
rep('<section class="bas-caja bas-revistas" aria-labelledby="basT4">',
    '<section class="bas-caja bas-revistas" data-tab="rev" aria-labelledby="basT4">')
# filtro por tipo, en revistas y en los programas de los manuales
rep("""          <div id="basRev"></div>""",
    """          <div class="bas-paises bas-tipos" id="basTipos" role="group" aria-label="Tipo de programa"></div>
          <div id="basRev"></div>""")
rep("""          <ul class="bas-rev" id="basManProg"></ul>""",
    """          <div class="bas-paises bas-tipos" id="basTiposMan" role="group" aria-label="Tipo de programa"></div>
          <ul class="bas-rev" id="basManProg"></ul>""")
# el aviso de "escrito en el Atari"
rep("""<div class="wrap">""",
    """<div class="aviso-tv" id="avisoTv" role="status" aria-live="polite"></div>
<div class="wrap">""")

# ---------------------------------------------------------------- JS
# A3: "D1 D1 - River Raid" -> "D1 River Raid"
rep('el.d1050pie.textContent = esCinta ? "sin disco" : ("D1 \\u00b7 " + g.name);',
    'el.d1050pie.textContent = esCinta ? "sin disco" : g.name;')
rep('el.d1050modo.textContent = esCinta ? "SIN DISCO" : "D1";',
    'el.d1050modo.textContent = "D1";')

# B8: sonido, mando y pantalla completa con aria-pressed y la misma luz
rep('el.sound.textContent = "Sonido: " + (muted ? "off" : "on");',
    'el.sound.setAttribute("aria-pressed", muted ? "false" : "true");\n'
    '    el.sound.classList.toggle("on", !muted);')
rep("""  document.getElementById("bPad").addEventListener("click", function () {
    document.body.classList.toggle("forcepad");""",
    """  document.getElementById("bPad").addEventListener("click", function () {
    /* esconde el mando en cualquier pantalla; antes cambiaba .forcepad, que
       en un telefono no hacia nada porque ahi el mando lo saca .touch */
    var sin = document.body.classList.toggle("sinpad");
    this.setAttribute("aria-pressed", sin ? "false" : "true");
    this.classList.toggle("on", !sin);""")
rep("""    bFull.classList.toggle("on", inmersivo);""",
    """    bFull.classList.toggle("on", inmersivo);
    bFull.setAttribute("aria-pressed", inmersivo ? "true" : "false");""")
rep("""      bFull.classList.remove("on");""",
    """      bFull.classList.remove("on");
      bFull.setAttribute("aria-pressed", "false");""")

# Pantalla completa: el boton entra o sale segun lo que se ve (.inmersivo),
# no segun la API, que en el modo solo-CSS (iOS, o al girar) no esta activa.
rep("""    if (enPantallaCompleta()) {
      if (document.exitFullscreen) document.exitFullscreen();
      else if (document.webkitExitFullscreen) document.webkitExitFullscreen();
    } else {""",
    """    if (document.body.classList.contains("inmersivo")) {
      if (enPantallaCompleta()) {
        if (document.exitFullscreen) document.exitFullscreen();
        else if (document.webkitExitFullscreen) document.webkitExitFullscreen();
      }
    } else {""")
rep("""  function avisaBarras(si) {""",
    """  /* Girar el telefono mientras se juega es pedir la pantalla entera: en
     apaisado el televisor quedaba chico en medio de la pagina gris. Entra
     solo a pantalla completa (la de CSS, que no necesita un toque) y, si
     entro asi, sale al volver a vertical. */
  (function () {
    if (!window.matchMedia) return;
    var mq = window.matchMedia("(orientation: landscape)"), auto = false;
    function ponInm(si) {
      document.body.classList.toggle("inmersivo", si);
      bFull.textContent = si ? "Salir" : "Pantalla completa";
      bFull.classList.toggle("on", si);
      bFull.setAttribute("aria-pressed", si ? "true" : "false");
      if (si) pedirWakeLock(); else soltarWakeLock();
      avisaBarras(si); medirBtns();
    }
    function mira() {
      var b = document.body, inm = b.classList.contains("inmersivo");
      var jugando = b.dataset.paso === "carga" && b.classList.contains("touch");
      if (mq.matches && jugando && !inm) { auto = true; ponInm(true); }
      else if (!mq.matches && auto && inm && !enPantallaCompleta()) { auto = false; ponInm(false); }
      else if (!mq.matches) auto = false;
    }
    if (mq.addEventListener) mq.addEventListener("change", mira);
    else if (mq.addListener) mq.addListener(mira);
    /* si ya estaba en apaisado al empezar a jugar */
    window.alEmpezarJuego = mira;
  })();

  function avisaBarras(si) {""")
rep("""    function irACarga() {
      etapa = 0;
      document.body.dataset.paso = "carga";
      pintarPasos(); pintarVolver();""",
    """    function irACarga() {
      etapa = 0;
      document.body.dataset.paso = "carga";
      pintarPasos(); pintarVolver();
      if (window.alEmpezarJuego) window.alEmpezarJuego();""")

# C11: al escribir un programa, la pantalla a la vista y un aviso
rep("""    function orden(txt) { cola.push(-5); escribir(txt); }""",
    """    function orden(txt) { cola.push(-5); escribir(txt); avisaEscrito(); }
    /* Escribir un programa largo pasa en la pantalla, y el boton que lo pide
       puede estar treinta pantallas mas abajo: se trae la pantalla a la
       vista y se dice que hacer despues. */
    var avisoT = 0;
    function avisaEscrito() {
      if (!enBasic()) return;
      var a = document.getElementById("avisoTv"), tv = document.querySelector(".tv");
      if (!a || !tv) return;
      var r = tv.getBoundingClientRect();
      if (r.top < -4 || r.bottom > window.innerHeight + 4)
        tv.scrollIntoView({ behavior: "smooth", block: "start" });
      a.textContent = "Escrito en el Atari. Escribe RUN y RETURN para correrlo.";
      a.classList.add("ver");
      clearTimeout(avisoT);
      avisoT = setTimeout(function () { a.classList.remove("ver"); }, 4500);
    }""")

# Elegir en el telefono: al entrar a un paso, las fichas y el boton de
# seguir quedan a la vista sin tener que bajar a buscarlos.
rep("""      window.scrollTo({ top: 0, behavior: "smooth" });
    }

    function irACarga() {""",
    """      alPaso(n);
    }
    /* En el telefono el televisor ocupa la primera media pantalla, y lo que
       se elige quedaba debajo: habia que bajar a buscarlo. Se baja solo lo
       justo para que se vea entero, sin pasarse del comienzo del paso. */
    function alPaso(n) {
      var id = { 1: "etMaquina", 2: "etJoystick", 3: "etSoporte" }[n];
      var e = id && document.getElementById(id), y = 0;
      if (e && document.body.dataset.listo && document.body.classList.contains("touch")) {
        var r = e.getBoundingClientRect(), arriba = r.top + window.scrollY;
        var fondo = r.bottom + window.scrollY;
        if (fondo > window.innerHeight) y = Math.min(fondo - window.innerHeight + 6, arriba - 8);
      }
      window.scrollTo({ top: Math.max(0, y), behavior: "smooth" });
    }

    function irACarga() {""")

# E14: pasos sin candado; joystick y drive se recuerdan
rep("var etapa = 1, etapaMax = 1;", "var etapa = 1, etapaMax = 4;")
rep("""      document.body.classList.add("mando-" + MANDOS[i].id);
    }""",
    """      document.body.classList.add("mando-" + MANDOS[i].id);
      try { localStorage.setItem("atari.mando", MANDOS[i].id); } catch (e) {}
    }""")
rep("""      document.body.dataset.soporte = cual;
      el.secCas.hidden = cual !== "cas";""",
    """      document.body.dataset.soporte = cual;
      try { localStorage.setItem("atari.soporte", cual); } catch (e) {}
      el.secCas.hidden = cual !== "cas";""")
rep("""construirTeclado(); construirMandos(); elegirMando(0);""",
    """construirTeclado(); construirMandos();
    elegirMando((function () {
      var id = null, k;
      try { id = localStorage.getItem("atari.mando"); } catch (e) {}
      for (k = 0; k < MANDOS.length; k++) if (MANDOS[k].id === id) return k;
      return 0;
    })());""")

UX_JS = r"""
    /* ==== UX 2026 (web/ux/PLAN_UX.md) =================================== */
    (function () {
      function lee(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }
      function guarda(k, v) { try { localStorage.setItem(k, v); } catch (e) {} }
      var tv = document.querySelector(".tv");

      /* la linea del telefono va justo debajo de la pantalla, y las dos
         quedan fijas arriba mientras se recorren los programas */
      var linea = document.querySelector(".bas-linea");
      if (linea && tv) tv.parentNode.insertBefore(linea, tv.nextSibling);
      function midoTv() {
        document.documentElement.style.setProperty("--alto-tv", tv.offsetHeight + "px");
      }
      if (window.ResizeObserver) new ResizeObserver(midoTv).observe(tv);
      else window.addEventListener("resize", midoTv);
      midoTv();

      /* la consola bajo el televisor: las mismas teclas que la de la maquina */
      holdBtn("cStart", "start"); holdBtn("cSelect", "select"); holdBtn("cOption", "option");
      document.getElementById("cReset").addEventListener("click", function () {
        document.getElementById("bReset").click();
      });
      var bTec = document.getElementById("bTeclado");
      bTec.addEventListener("click", function () {
        var si = document.body.classList.toggle("tec-abierto");
        bTec.setAttribute("aria-pressed", si ? "true" : "false");
        bTec.textContent = si ? "Ocultar teclado" : "Teclado";
        if (si) setTimeout(function () {
          document.querySelector(".xl").scrollIntoView({ behavior: "smooth", block: "nearest" });
        }, 60);
      });
      /* BREAK, ESC y el cursor: las flechas del Atari son CTRL con - = + * */
      var ESPECIAL = { brk: -5, esc: 0x1c, bs: 0x34, clr: 0x76,
                       up: 0x8e, dn: 0x8f, izq: 0x86, der: 0x87 };
      document.querySelectorAll(".ctv-basic button").forEach(function (b) {
        /* sin robarle el foco a la linea: el teclado del telefono no se cierra */
        b.addEventListener("pointerdown", function (e) { e.preventDefault(); });
        b.addEventListener("click", function () {
          var c = ESPECIAL[b.dataset.k];
          resumeAudio();
          if (c === -5) cola.push(-5); else pulsaTecla(c);
          buzz(6);
        });
      });

      /* las pestanas del BASIC, y la ultima elegida se recuerda */
      var etb = document.getElementById("etBasic");
      var tabs = document.querySelectorAll("#basTabs button");
      function ponTab(t) {
        etb.dataset.tab = t;
        tabs.forEach(function (b) {
          b.setAttribute("aria-selected", b.dataset.tab === t ? "true" : "false");
          b.tabIndex = b.dataset.tab === t ? 0 : -1;
        });
      }
      tabs.forEach(function (b) {
        b.addEventListener("click", function () { ponTab(b.dataset.tab); guarda("atari.basTab", b.dataset.tab); });
        b.addEventListener("keydown", function (e) {
          var l = [].slice.call(tabs), i = l.indexOf(b);
          if (e.key === "ArrowRight" || e.key === "ArrowLeft") {
            var n = l[(i + (e.key === "ArrowRight" ? 1 : l.length - 1)) % l.length];
            n.focus(); n.click(); e.preventDefault();
          }
        });
      });
      var t0 = lee("atari.basTab");
      ponTab(t0 === "rev" || t0 === "man" ? t0 : "prog");

      /* filas compactas: titulo y revista; un toque abre el resto */
      document.querySelectorAll(".bas-rev li").forEach(function (li) {
        li.tabIndex = 0;
        li.setAttribute("aria-expanded", "false");
      });
      function abre(li) {
        var si = li.classList.toggle("abierto");
        li.setAttribute("aria-expanded", si ? "true" : "false");
      }
      etb.addEventListener("click", function (e) {
        var li = e.target.closest(".bas-rev li");
        if (!li || e.target.closest("button,a")) return;
        abre(li);
      });
      etb.addEventListener("keydown", function (e) {
        if ((e.key === "Enter" || e.key === " ") && e.target.matches(".bas-rev li")) {
          e.preventDefault(); abre(e.target);
        }
      });

      /* el tipo de cada programa, sacado de su titulo y su descripcion */
      function sinTildes(t) {
        t = String(t || "").toLowerCase();
        return t.normalize ? t.normalize("NFD").replace(/[\u0300-\u036f]/g, "") : t;
      }
      function tipoDe(x) {
        var t = sinTildes(x.titulo + " " + x.desc), d = sinTildes(x.desc);
        if (/\bjuego|adivin|ahorcado|breakout|cuestionario|test de|reflejos|\bdebes\b/.test(t)) return "juego";
        if (/^(dibuja|pinta|traza|llena|rebota)/.test(d)) return "graf";
        if (/sonid|ruido|sirena|musica|cancion|\bnotas?\b|timbre|blues|imita|trino|piano|zumbido|disparo|olas|grazna/.test(t)) return "son";
        if (/graphics|dibuj|grafic|colou?r|circul|figura|pinta|bandera|letrero|laberinto|fractal|player/.test(t)) return "graf";
        return "otro";
      }
      var TIPOS = [["", "Todos"], ["juego", "Juegos"], ["graf", "Gr\u00e1ficos"],
                   ["son", "Sonido"], ["otro", "Otros"]];
      function filtroTipos(caja, lis, datos) {
        var cuenta = { "": lis.length };
        lis.forEach(function (li, i) {
          var t = tipoDe(datos[i]); li.dataset.tipo = t; cuenta[t] = (cuenta[t] || 0) + 1;
        });
        TIPOS.forEach(function (o) {
          if (o[0] && !cuenta[o[0]]) return;
          var b = document.createElement("button");
          b.type = "button"; b.dataset.tipo = o[0];
          b.textContent = o[1] + " " + cuenta[o[0]];
          b.setAttribute("aria-pressed", o[0] ? "false" : "true");
          b.addEventListener("click", function () {
            lis.forEach(function (li) { li.hidden = !!o[0] && li.dataset.tipo !== o[0]; });
            caja.querySelectorAll("button").forEach(function (x) {
              x.setAttribute("aria-pressed", x === b ? "true" : "false");
            });
          });
          caja.appendChild(b);
        });
      }
      filtroTipos(document.getElementById("basTipos"),
                  [].slice.call(document.querySelectorAll("#basRev .bas-rev > li")), REVISTAS);
      filtroTipos(document.getElementById("basTiposMan"),
                  [].slice.call(document.querySelectorAll("#basManProg > li")), MAN_PROGS);

      /* La estanteria en lista: en un telefono las fichas de disco y las
         cintas dibujadas dan dos titulos por pantalla. Cada soporte lleva
         tambien su renglon, y la vista elige cual se ve. */
      GAMES.forEach(function (g) {
        if (!g.el) return;
        var f = document.createElement("span"), b = document.createElement("b");
        var sm = document.createElement("small"), em = document.createElement("em");
        f.className = "fila-lista"; f.setAttribute("aria-hidden", "true");
        b.textContent = nombreJuego(g.name);
        sm.textContent = fichaCorta(g) || "";
        em.textContent = esCinta(g) ? (g.sys === "prisma" ? "Prisma" : "Turbo") : "Disco";
        em.className = esCinta(g) ? (g.sys === "prisma" ? "prisma" : "turbo") : "disco";
        f.appendChild(b); f.appendChild(sm); f.appendChild(em);
        g.el.appendChild(f);
      });
      var vistas = [];
      ["secCas", "secDisk"].forEach(function (id) {
        var sec = document.getElementById(id), cr = sec.querySelector(".cribas");
        var abc = sec.querySelector(".abc"), cab = document.createElement("div");
        cab.className = "estante-cab";
        sec.insertBefore(cab, cr); cab.appendChild(cr); cab.appendChild(abc);
        var v = document.createElement("button");
        v.type = "button"; v.className = "vista";
        cr.insertBefore(v, cr.querySelector(".cuenta"));
        v.addEventListener("click", function () {
          ponVista(!sec.classList.contains("en-lista"));
          guarda("atari.vista", sec.classList.contains("en-lista") ? "lista" : "fichas");
        });
        vistas.push([sec, v]);
      });
      function ponVista(lista) {
        vistas.forEach(function (p) {
          p[0].classList.toggle("en-lista", lista);
          p[1].textContent = lista ? "Ver fichas" : "Ver lista";
        });
      }
      var vg = lee("atari.vista");
      ponVista(vg ? vg === "lista"
        : !!(window.matchMedia && window.matchMedia("(max-width:700px)").matches));

      /* en el paso 3 la foto del aparato tambien lo elige */
      [["#sopCinta .xc12", "cas"], ["#sopDisco .d1050", "disk"]].forEach(function (p) {
        var f = document.querySelector(p[0]);
        if (f) f.addEventListener("click", function (e) {
          if (document.body.dataset.paso !== "3" || e.target.closest("button")) return;
          resumeAudio(); elegirSoporte(p[1]);
        });
      });

      /* el mando, apagado o encendido segun lo que hay */
      var bPad = document.getElementById("bPad");
      if (bPad) {
        var hay = document.body.classList.contains("touch") || document.body.classList.contains("forcepad");
        bPad.setAttribute("aria-pressed", hay ? "true" : "false");
        bPad.classList.toggle("on", hay);
      }
    })();
"""

# E14: quien ya eligio drive vuelve directo a la estanteria
rep("""    irAPaso(1);
    requestAnimationFrame(run);""",
    UX_JS + """
    (function () {
      var sop = null;
      try { sop = localStorage.getItem("atari.soporte"); } catch (e) {}
      document.body.dataset.listo = "1";
      if (sop === "cas" || sop === "disk") elegirSoporte(sop); else irAPaso(1);
    })();
    requestAnimationFrame(run);""")

# ---------------------------------------------------------------- CSS
UX_CSS = r"""
/* =====================================================================
   UX 2026 - web/ux/PLAN_UX.md, fases 1 a 5. Va al final para ganarle a
   todo lo anterior sin tocarlo.
   ===================================================================== */

/* --- Fase 1 ----------------------------------------------------------- */
/* A1: en tactil no hay "pasar por encima": el toque dejaba el texto de
   ayuda pegado sobre el televisor */
@media (hover:none){
  .pasos .paso:hover::after{opacity:0;visibility:hidden}
  .pasos .paso:focus-visible::after{opacity:1;visibility:visible}
}
/* A2: el aro negro de FUEGO se salia 4px por la derecha */
body:not(.inmersivo) .gamepad .fire{margin:10px 10px 10px 0}

/* blancos de toque de 44px en el telefono */
body.touch .tv-barra button,body.touch .sigue,body.touch .pasos .paso,
body.touch .bas-bot button,body.touch .bas-rev .acc button,body.touch .bas-man .ligas button,
body.touch .bas-paises button,body.touch .lector-cab button,body.touch .bas-hoja-cab button,
body.touch .cribas .q,body.touch .cribas .criba,body.touch .cribas .limpia,body.touch .cribas .vista,
body.touch .abc button,body.touch .consola-tv button,body.touch .bas-tabs button,
body.touch .bas-hem summary,body.touch .cruce button{min-height:44px}
body.touch .abc button{min-width:44px}
body.touch .tv-barra button{padding:4px 12px}
body.touch .bas-hem summary{display:flex;align-items:center}
body.touch .lector-cab input{min-height:40px}

/* B8: los interruptores del televisor, todos con su piloto */
.tv-barra button[aria-pressed]::before{
  content:"";display:inline-block;width:8px;height:8px;border-radius:50%;
  margin:0 7px 1px 0;vertical-align:middle;background:#57544d;
  box-shadow:inset 0 1px 1px rgba(0,0,0,.6);
}
.tv-barra button[aria-pressed="true"]::before{background:#101a33;box-shadow:none}
body.sinpad .gamepad{display:none !important}

/* F17: los rotulos de la cinta y el disco, legibles sobre el papel */
.sop-rot,.d1050-rot{color:#5c564c}
.sop-rot b,.d1050-rot b{color:#2f2b25}

/* F16: nada por debajo de 12px fuera de los rotulos de la maquina */
.padhint{font-size:12px}
.firma small{font-size:12px}
.grupo h3,.cribas .criba,.cribas .limpia,.abc button,.count{font-size:13px}
.bas-man-grupo{font-size:11px}

/* --- la consola bajo el televisor (Fase 2) y las teclas del BASIC ------ */
.consola-tv{display:none;flex-direction:column;gap:6px;margin:10px 0 0}
.ctv-fila{display:flex;gap:6px}
.consola-tv button{
  flex:1 1 0;min-width:0;padding:6px 4px;border-radius:4px;
  font-family:"IBM Plex Sans",sans-serif;font-size:12px;font-weight:600;
  letter-spacing:.06em;text-transform:uppercase;line-height:1.1;
  background:linear-gradient(180deg,var(--key) 0%,var(--key-lo) 100%);color:var(--key-ink);
  border:1px solid rgba(0,0,0,.35);box-shadow:inset 0 1px 0 rgba(255,255,255,.45);
}
.consola-tv button:hover{background:linear-gradient(180deg,#ddd5c4 0%,var(--key-lo) 100%);border-color:rgba(0,0,0,.35)}
.consola-tv button.on,.consola-tv .cons:active{background:var(--acento);color:#101a33}
.consola-tv #bTeclado{flex:1.6 1 0;background:#2c2a26;color:#e8e3d8;border-color:#4a4844;box-shadow:none}
.consola-tv #bTeclado[aria-pressed="true"]{background:var(--acento);color:#101a33}
.ctv-basic{display:none}
.ctv-basic button{
  font-family:"Atari XL","VT323",monospace;font-weight:400;letter-spacing:0;font-size:12px;
  background:#2c5aa0;color:#dce9ff;border-color:#1d3f75;box-shadow:inset 0 1px 0 rgba(255,255,255,.18);
}
.ctv-basic button:hover{background:#3667b4;border-color:#1d3f75}
.ctv-basic button:active{background:#1d4a8c}
body[data-paso="basic"] .ctv-basic{display:flex}
body[data-paso="basic"] #cReset{display:none}

/* la consola aparece cuando el teclado del Atari se pliega */
body[data-paso="basic"]:not(.inmersivo) .consola-tv{display:flex}
body[data-paso="basic"]:not(.inmersivo):not(.tec-abierto) .xl{display:none !important}
@media (max-width:700px),(pointer:coarse){
  body[data-paso="carga"]:not(.inmersivo) .consola-tv{display:flex}
  body[data-paso="carga"]:not(.inmersivo):not(.tec-abierto) .xl{display:none !important}
  /* B6: jugando, el aparato que gira se reduce a su rotulo */
  body[data-paso="carga"] .xc12,body[data-paso="carga"] .d1050{display:none}
  body[data-paso="carga"] #etSoporte{margin-top:10px;order:0}
  body[data-paso="carga"] .soportes{gap:6px}
  body[data-paso="carga"] .sop-rot,body[data-paso="carga"] .d1050-rot{
    padding:9px 12px;border-radius:4px;background:#2c2a26;color:#d8d4c8}
  body[data-paso="carga"] .sop-rot b,body[data-paso="carga"] .d1050-rot b{color:#f0c04a}
  body[data-paso="carga"] .gamepad{margin-top:12px}
}

/* --- Fase 3: programar ------------------------------------------------ */
.bas-linea{margin:8px 0 0}
body:not([data-paso="basic"]) .bas-linea{display:none !important}
/* en el telefono la pantalla y la linea quedan fijas arriba */
@media (max-width:1099px){
  body[data-paso="basic"]:not(.inmersivo) .tv{
    position:sticky;top:0;z-index:25;
    max-width:min(100%,calc(46svh * 1.4));
    background:var(--papel);box-shadow:0 0 0 6px var(--papel);
  }
  body[data-paso="basic"]:not(.inmersivo) .bas-linea{
    position:sticky;top:var(--alto-tv,0px);z-index:24;margin:0;padding:8px 0;
    background:var(--papel);
  }
  body[data-paso="basic"] .tv-barra{padding:4px 8px;gap:6px}
  body[data-paso="basic"] #bPad{display:none}
}
/* en el computador ancho: el televisor a la izquierda, los programas a la
   derecha en su propia columna con su propio scroll */
@media (min-width:1100px){
  body[data-paso="basic"]:not(.inmersivo) .wrap{max-width:1500px}
  body[data-paso="basic"]:not(.inmersivo) .machine{
    display:grid;grid-template-columns:minmax(0,1fr) minmax(400px,.8fr);
    column-gap:20px;align-items:start;
    grid-template-areas:"firma firma" "pasos pasos" "tv bas" "lin bas" "boot bas"
      "cinta bas" "cons bas" "xl bas" "stat bas" "resto bas";
    grid-template-rows:repeat(9,auto) 1fr;
  }
  body[data-paso="basic"]:not(.inmersivo) .firma{grid-area:firma}
  body[data-paso="basic"]:not(.inmersivo) .pasos{grid-area:pasos;margin-bottom:12px}
  body[data-paso="basic"]:not(.inmersivo) .tv{grid-area:tv}
  body[data-paso="basic"]:not(.inmersivo) .bas-linea{grid-area:lin}
  body[data-paso="basic"]:not(.inmersivo) #boot{grid-area:boot}
  body[data-paso="basic"]:not(.inmersivo) .cinta{grid-area:cinta}
  body[data-paso="basic"]:not(.inmersivo) .consola-tv{grid-area:cons}
  body[data-paso="basic"]:not(.inmersivo) .xl{grid-area:xl;margin-top:12px}
  body[data-paso="basic"]:not(.inmersivo) .statusbar{grid-area:stat;margin-top:10px}
  body[data-paso="basic"]:not(.inmersivo) #etBasic{
    grid-area:bas;position:sticky;top:12px;margin:0;
    max-height:calc(100vh - 24px);overflow:auto;overscroll-behavior:contain;
    padding-right:4px;
  }
  body[data-paso="basic"] .basic{grid-template-columns:minmax(0,1fr)}
  body[data-paso="basic"] .bas-rev{grid-template-columns:minmax(0,1fr)}
}
/* el aviso: escrito en el Atari, y que hacer ahora */
.aviso-tv{
  position:fixed;left:50%;bottom:calc(18px + env(safe-area-inset-bottom,0px));z-index:70;
  transform:translate(-50%,8px);max-width:calc(100vw - 32px);width:max-content;
  padding:10px 16px;border-radius:4px;background:#2b5aa8;color:#dce9ff;
  box-shadow:inset 0 0 0 2px #6f9de0,0 8px 20px rgba(0,0,0,.4);
  font-family:"VT323",monospace;font-size:20px;letter-spacing:.5px;line-height:1.2;
  opacity:0;visibility:hidden;pointer-events:none;
  transition:opacity .18s,transform .18s,visibility .18s;
}
.aviso-tv.ver{opacity:1;visibility:visible;transform:translate(-50%,0)}

/* --- Fase 4: BASIC en pestanas ---------------------------------------- */
.bas-tabs{display:flex;gap:6px;margin:12px 0}
.bas-tabs button{
  flex:1 1 0;min-width:0;padding:10px 6px;border-radius:4px;border:0;cursor:pointer;
  font-family:"Press Start 2P",monospace;font-size:11px;letter-spacing:.5px;line-height:1.3;
  background:linear-gradient(180deg,#35342f 0%,#22211d 100%);color:#c9c3b8;
  box-shadow:inset 0 0 0 2px rgba(255,255,255,.1);
}
.bas-tabs button:hover{background:linear-gradient(180deg,#403f39 0%,#2b2a25 100%);border:0}
.bas-tabs button[aria-selected="true"]{background:#d7b36a;color:#22211d;box-shadow:none}
.bas-tabs button:focus-visible{outline:2px solid var(--acento);outline-offset:2px}
#etBasic[data-tab="prog"] .bas-caja:not([data-tab="prog"]),
#etBasic[data-tab="rev"] .bas-caja:not([data-tab="rev"]),
#etBasic[data-tab="man"] .bas-caja:not([data-tab="man"]){display:none}
.bas-tipos{margin-top:-4px}
.bas-tipos button[aria-pressed="true"]{background:#e8e3d8;border-color:#e8e3d8;color:#22211d}

/* filas compactas: portada chica, titulo y de donde sale; un toque abre */
.bas-rev li{position:relative;cursor:pointer}
.bas-rev li:focus-visible{outline:2px solid var(--acento,#f0c04a);outline-offset:2px}
#etBasic .bas-rev{gap:6px}
.bas-rev li:not(.abierto){align-items:center;gap:10px;padding:6px 34px 6px 8px;min-height:56px}
.bas-rev li:not(.abierto) > img,.bas-rev li:not(.abierto) > .sinportada,
.bas-rev li:not(.abierto) .bas-hoja-btn img{width:32px}
.bas-rev li:not(.abierto) .txt{gap:1px}
.bas-rev li:not(.abierto) .bas-hoja-btn{pointer-events:none}
.bas-rev li:not(.abierto) .dice,.bas-rev li:not(.abierto) .como,
.bas-rev li:not(.abierto) .acc{display:none}
.bas-rev li:not(.abierto) .de{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.bas-rev li::after{
  content:"+";position:absolute;right:10px;top:50%;transform:translateY(-50%);
  font-family:"VT323",monospace;font-size:26px;line-height:1;color:#d7b36a;
}
.bas-rev li.abierto::after{content:"\2212";top:18px}
.bas-rev li.abierto{padding-right:34px}

/* la estanteria en lista */
.fila-lista{display:none}
.libsec.en-lista .grid{grid-template-columns:minmax(0,1fr) !important;gap:3px}
.libsec.en-lista .grupo{margin-bottom:6px}
.libsec.en-lista .grupo h3{margin-bottom:4px;padding-bottom:2px;font-size:12px;line-height:1.4}
.libsec.en-lista .grid > li > button{
  display:block;width:100%;max-width:none;margin:0;padding:0;aspect-ratio:auto;
  transform:none;overflow:visible;text-align:left;background:none;border:0;box-shadow:none;
}
.libsec.en-lista .grid > li > button > :not(.fila-lista){display:none !important}
.libsec.en-lista .fila-lista{
  display:grid;grid-template-columns:minmax(0,1fr) auto;grid-template-rows:auto auto;
  column-gap:10px;align-items:center;min-height:44px;padding:3px 12px;border-radius:4px;
  background:#fbfbfa;border:1px solid var(--raya);box-shadow:0 1px 2px rgba(0,0,0,.06);
}
.libsec.en-lista button:hover .fila-lista{border-color:#a8a298;background:#fff}
.libsec.en-lista button[aria-pressed="true"] .fila-lista{box-shadow:inset 0 0 0 2px var(--acento)}
.fila-lista b{grid-column:1;font-family:"IBM Plex Sans",sans-serif;font-size:15px;font-weight:600;line-height:1.25;
  color:var(--tinta);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.fila-lista small{grid-column:1;font-family:"IBM Plex Mono",monospace;font-size:12px;line-height:1.3;
  color:var(--tinta-baja);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.fila-lista em{grid-column:2;grid-row:1/3;font-style:normal;font-family:"IBM Plex Mono",monospace;
  font-size:12px;font-weight:600;letter-spacing:.06em;text-transform:uppercase;
  padding:3px 7px;border-radius:3px;background:#23201d;color:#ece4d6}
.fila-lista em.prisma{background:#5a2f86;color:#f2e8ff}
.fila-lista em.turbo{background:#e7b43c;color:#2a2010}
.cribas .vista{padding:7px 10px;border-radius:4px;font-family:"IBM Plex Mono",monospace;font-size:13px}
/* el buscador y las letras se quedan arriba */
.estante-cab{position:sticky;top:0;z-index:15;margin:0 -8px 10px;padding:8px 8px 6px;
  background:rgba(242,242,240,.97);box-shadow:0 8px 10px -10px rgba(0,0,0,.35)}
.estante-cab .cribas{margin:0 0 8px}
.estante-cab .abc{margin:0}
@media (max-width:700px){
  .estante-cab .abc{flex-wrap:nowrap;overflow-x:auto;padding-bottom:2px;scrollbar-width:none}
  .estante-cab .abc::-webkit-scrollbar{display:none}
  .estante-cab .abc li{flex:none}
  .cribas{gap:6px}
  .cribas .criba{flex:1 1 30%}
  .cribas .vista{flex:none}
  .cribas .cuenta{font-size:13px}
}

/* --- Fase 5: el recorrido --------------------------------------------- */
/* El televisor NO se achica en los pasos 2 a 4: se probo (E15) y se ve
   como que la pantalla pierde tamano. Se queda entero en todos los pasos. */
@media (max-width:700px){
  /* la cinta y el disco, uno al lado del otro */
  body[data-paso="3"] .soportes{grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
  body[data-paso="3"] .soporte .sigue{align-self:stretch;font-size:13px;letter-spacing:0;padding-left:6px;padding-right:6px}
  body[data-paso="3"] .sop-rot,body[data-paso="3"] .d1050-rot{font-size:14px;letter-spacing:0}
}
/* los cuatro interruptores del televisor en una sola fila del telefono */
@media (max-width:480px){
  .tv-barra .aparato{flex-wrap:nowrap;gap:4px;justify-content:center;width:100%}
  .tv-barra button{font-size:14px;letter-spacing:0;padding:4px 7px;white-space:nowrap}
  body.touch .tv-barra button{padding:4px 7px}
  .tv-barra button[aria-pressed]::before{margin-right:5px}
}
/* A pantalla completa la tapa del XE queda sin su caja, y su canal gris
   (el ::after donde va el modelo) se media contra la pantalla entera: un
   rectangulo claro pegado encima del juego. Ahi no hay tapa que dibujar. */
body.inmersivo .xl-tapa::before,body.inmersivo .xl-tapa::after{display:none !important}
/* y lo mismo las demas piezas de la carcasa: la puerta del cartucho del
   800XL y del 600XL, la rejilla y la chapa. A pantalla completa solo
   quedan la imagen, las teclas de consola y el mando. */
body.inmersivo .xl-cartucho,body.inmersivo .xl-rejilla,body.inmersivo .xl-marca{display:none !important}
body.inmersivo .firma{display:none !important}
/* Jugando en el telefono vertical todo cabe en UNA pantalla, sin scroll:
   televisor, consola y mando. La firma, la regleta de estado y el rotulo
   del drive se apartan (siguen con el teclado abierto), y el televisor se
   ajusta al alto que queda solo si no alcanza. --resto es lo que ocupan
   Volver, la barra del televisor, la consola, el mando y los margenes. */
@media (max-width:700px) and (orientation:portrait){
  body[data-paso="carga"]:not(.inmersivo):not(.tec-abierto) .firma,
  body[data-paso="carga"]:not(.inmersivo):not(.tec-abierto) .statusbar,
  body[data-paso="carga"]:not(.inmersivo):not(.tec-abierto) #etSoporte{display:none !important}
  body[data-paso="carga"]:not(.inmersivo):not(.tec-abierto){--resto:352px}
  body[data-paso="carga"][data-soporte="cas"]:not(.inmersivo):not(.tec-abierto){--resto:400px}
  body[data-paso="carga"]:not(.inmersivo):not(.tec-abierto) .tv{
    max-width:min(100%,calc((100svh - var(--resto)) * 1.4))}
  body[data-paso="carga"]:not(.inmersivo) .volver{margin-top:0}
  body[data-paso="carga"]:not(.inmersivo) .wrap{padding-bottom:10px}
}
/* Elegir en el telefono: las fichas de a dos (los mandos de a tres), con
   la foto arriba y el nombre debajo, y el boton de seguir pegado abajo de
   la pantalla mientras se elige. En el paso 1 el teclado y la regleta se
   apartan: la maquina se ve en su ficha. */
@media (max-width:700px){
  .mandos{grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
  #mandos{grid-template-columns:repeat(3,minmax(0,1fr))}
  .mandocard .caja{flex-direction:column;align-items:center;gap:6px;padding:9px 6px}
  .mandocard .txt{align-items:center;text-align:center;width:100%}
  .mandocard .txt .j-nota{display:none}
  .mandocard .txt .j-marca{font-size:12px;letter-spacing:0;line-height:1.2}
  .mandocard .txt em{font-size:10px;letter-spacing:.5px;line-height:1.3}
  .mandocard .txt b{margin-top:1px;font-size:13px}
  .mandocard .dib{width:64px;height:74px}
  .maqcard .dib{width:100%;height:58px}
  .maqcard .maqdib>span{width:100px;height:56px}
  body[data-paso="1"] .xl,body[data-paso="1"] .statusbar{display:none !important}
  .etapa-pie{position:sticky;bottom:0;z-index:12;margin:10px -6px 0;
    padding:8px 6px calc(8px + env(safe-area-inset-bottom,0px));
    background:rgba(242,242,240,.96);box-shadow:0 -8px 10px -10px rgba(0,0,0,.35)}
  .etapa-pie .sigue{flex:1 1 auto;justify-content:center;text-align:center}
  body[data-paso="3"] .xc12,body[data-paso="3"] .d1050{width:84%;margin-inline:auto;cursor:pointer}
}
@media (prefers-reduced-motion:reduce){.aviso-tv{transition:none}}
"""
rep("""@media (prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}
</style>""",
    """@media (prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}
""" + UX_CSS + """</style>""")

bad = [i for i, ch in enumerate(s) if ord(ch) > 126 or (ord(ch) < 32 and ch not in "\n\t\r")]
if bad:
    sys.exit("caracter no ASCII en %d: %r" % (bad[0], s[bad[0] - 40:bad[0] + 40]))
open(dst, "w", encoding="ascii").write(s)
print("ok", len(s))
