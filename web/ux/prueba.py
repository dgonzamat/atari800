"""Pruebas de la pagina despues del plan de UI/UX (PLAN_UX.md, seccion 6).

    python3.13 prueba.py [URL] [carpeta_fotos]

Cada prueba imprime OK o FALLA con lo que vio. Sale con codigo 1 si alguna
falla. Necesita la pagina servida por HTTP (python3.13 -m http.server).
"""
import io, sys, json
from playwright.sync_api import sync_playwright
from PIL import Image

URL = sys.argv[1] if len(sys.argv) > 1 else "http://localhost:8920/atari.html"
OUT = (sys.argv[2] if len(sys.argv) > 2 else ".").rstrip("/") + "/"
CH = dict(executable_path="/opt/pw-browsers/chromium",
          args=["--use-gl=swiftshader", "--enable-unsafe-swiftshader", "--autoplay-policy=no-user-gesture-required"])
fallas = []


def prueba(nombre, ok, detalle=""):
    print(("OK    " if ok else "FALLA ") + nombre + ("  [" + str(detalle) + "]" if detalle != "" else ""))
    if not ok:
        fallas.append(nombre)


def colores(pg):
    png = pg.locator("#scr").screenshot()
    im = Image.open(io.BytesIO(png)).convert("RGB").resize((168, 120))
    return len(set(im.getdata()))


def abre(ctx):
    pg = ctx.new_page(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto(URL, wait_until="load")
    pg.wait_for_function("document.body.dataset.paso", timeout=60000)
    pg.wait_for_timeout(1500)
    return pg, errs


def basic_teclea(pg, texto):
    for linea in texto.split("\n"):
        pg.keyboard.type(linea, delay=35)
        pg.keyboard.press("Enter")
        pg.wait_for_timeout(250)


with sync_playwright() as p:
    nav = p.chromium.launch(**CH)

    # ------------------------------------------------------------ computador
    ctx = nav.new_context(viewport={"width": 1280, "height": 800})
    pg, errs = abre(ctx)
    prueba("PC: arranca en el paso 1", pg.evaluate("document.body.dataset.paso") == "1")
    prueba("PC: los pasos no estan trabados",
           pg.evaluate("document.querySelectorAll('.pasos .paso.trabado').length") == 0)

    # disco: River Raid
    pg.click('.paso[data-ir="3"]'); pg.wait_for_timeout(300)
    pg.click('.sigue[data-soporte="disk"]'); pg.wait_for_timeout(300)
    pg.fill("#qDisk", "river raid"); pg.wait_for_timeout(400)
    pg.click("#gridDisk li:not([hidden]) button"); pg.wait_for_timeout(9000)
    n = colores(pg)
    prueba("PC: River Raid dibuja (>15 colores)", n > 15, n)
    prueba("PC: rotulo del disco sin D1 repetido",
           pg.inner_text(".d1050-rot").split() [:3] == ["D1", "River", "Raid"], pg.inner_text(".d1050-rot"))
    prueba("PC: jugando, la consola de la maquina sigue a la vista",
           pg.is_visible("#bStart") and not pg.is_visible("#consolaTv"))

    # interruptores del televisor
    pg.click("#bSound")
    prueba("PC: Sonido apagado dice aria-pressed=false",
           pg.get_attribute("#bSound", "aria-pressed") == "false" and pg.inner_text("#bSound").strip() == "Sonido")
    pg.click("#bSound")
    pg.click("#bPad")
    prueba("PC: Mando esconde el mando", not pg.is_visible("#gamepad")
           and pg.get_attribute("#bPad", "aria-pressed") == "false")
    pg.click("#bPad")
    prueba("PC: Mando lo vuelve a mostrar", pg.is_visible("#gamepad"))

    # cinta: el Zorro
    pg.click("#bVolver"); pg.wait_for_timeout(300)
    pg.click('.paso[data-ir="3"]'); pg.wait_for_timeout(300)
    pg.click('.sigue[data-soporte="cas"]'); pg.wait_for_timeout(300)
    pg.fill("#qCas", "zorro"); pg.wait_for_timeout(400)
    pg.click("#gridCas li:not([hidden]) button"); pg.wait_for_timeout(12000)
    pct = pg.inner_text("#cintapct")
    prueba("PC: la cinta del Zorro empieza a cargar", pg.is_visible("#cinta") and pct != "0%", pct)

    # 600XL
    pg.click("#bVolver"); pg.wait_for_timeout(300)
    pg.click('.paso[data-ir="1"]'); pg.wait_for_timeout(300)
    pg.click("#maquinas > :nth-child(4)"); pg.wait_for_timeout(2500)
    mach = pg.inner_text("#mach")
    prueba("PC: 600XL dice Atari 600XL / 16K", "600XL" in mach and "16K" in mach, mach)

    # BASIC en el 600XL
    pg.click(".paso-basic"); pg.wait_for_timeout(3000)
    mach = pg.inner_text("#mach")
    prueba("PC: BASIC en el 600XL dice / 16K / BASIC", mach.endswith("/ 16K / BASIC"), mach)
    prueba("PC: en BASIC el teclado del Atari esta plegado", not pg.is_visible(".xl"))
    prueba("PC: en BASIC la consola esta bajo la pantalla", pg.is_visible("#consolaTv") and pg.is_visible("#cStart"))
    caja = pg.evaluate("(() => { const a = document.querySelector('.tv').getBoundingClientRect(), b = document.querySelector('#etBasic').getBoundingClientRect(); return [a.right, b.left, b.top, innerHeight]; })()")
    prueba("PC 1280: BASIC en dos columnas", caja[1] >= caja[0] and caja[2] < caja[3], caja)
    pg.click("#bTeclado"); pg.wait_for_timeout(300)
    prueba("PC: el boton Teclado despliega el teclado", pg.is_visible(".xl"))
    pg.click("#bTeclado")

    # volver al 65XE para el resto del BASIC
    pg.click('.paso[data-ir="1"]'); pg.wait_for_timeout(300)
    pg.click("#maquinas > :nth-child(1)"); pg.wait_for_timeout(1500)
    pg.click(".paso-basic"); pg.wait_for_timeout(3500)
    pg.click("#scr")
    basic_teclea(pg, '10 FOR I=1 TO 3:PRINT "HOLA LOLA ";I:NEXT I\nRUN')
    pg.wait_for_timeout(1500)
    basic_teclea(pg, 'SAVE "D:HOLA.BAS"')
    pg.wait_for_timeout(3500)
    lista = pg.inner_text("#basLista")
    prueba("PC: SAVE deja HOLA.BAS en Tu disquete", "HOLA.BAS" in lista, lista.replace("\n", " ")[:60])

    # pestanas, filtro y filas compactas
    pg.click('#basTabs [data-tab="rev"]'); pg.wait_for_timeout(300)
    prueba("PC: pestana Revistas muestra solo revistas",
           pg.is_visible(".bas-revistas") and not pg.is_visible(".bas-manuales")
           and not pg.is_visible("#basLista"))
    tipos = pg.eval_on_selector_all("#basTipos button", "b => b.map(x => x.textContent)")
    prueba("PC: filtro por tipo con cuentas", len(tipos) >= 4, tipos)
    pg.click('#basTipos button[data-tipo="son"]'); pg.wait_for_timeout(200)
    vis = pg.evaluate("[...document.querySelectorAll('#basRev .bas-rev > li')].filter(l => !l.hidden).map(l => l.dataset.tipo)")
    prueba("PC: el filtro Sonido deja solo sonido", len(vis) > 0 and set(vis) == {"son"}, len(vis))
    pg.click('#basTipos button[data-tipo=""]')
    alto = pg.evaluate("document.querySelector('#basRev .bas-rev > li').getBoundingClientRect().height")
    prueba("PC: fila de revista compacta (<=80px)", alto <= 80, round(alto))
    li = "#basRev .bas-pais:last-child ul.bas-rev > li:last-child"
    pg.click(li + " .txt b"); pg.wait_for_timeout(200)
    prueba("PC: un toque abre la fila", pg.is_visible(li + " .acc button"))
    pg.click(li + " .acc button"); pg.wait_for_timeout(500)
    prueba("PC: aparece el aviso de escrito", pg.is_visible("#avisoTv.ver"))
    pg.wait_for_timeout(9000)
    tv = pg.evaluate("(() => { const r = document.querySelector('.tv').getBoundingClientRect(); return [r.top, r.bottom, innerHeight]; })()")
    prueba("PC: la pantalla queda a la vista al escribir", tv[0] > -10 and tv[1] <= tv[2] + 10, tv)
    pg.click("#scr")
    basic_teclea(pg, "RUN")
    pg.wait_for_timeout(5000)
    n = colores(pg)
    prueba("PC: el programa de revista corre (pantalla cambia)", n >= 2, n)

    # el visor
    pg.click('#basTabs [data-tab="man"]'); pg.wait_for_timeout(300)
    pg.click("#basMan .ligas button"); pg.wait_for_timeout(2500)
    prueba("PC: el visor de manuales se abre", pg.evaluate("document.getElementById('lector').open"))
    pg.click("#lectorCerrar"); pg.wait_for_timeout(300)

    # recordar: pestana, maquina, drive, programas
    pg.reload(wait_until="load")
    pg.wait_for_function("document.body.dataset.paso", timeout=60000); pg.wait_for_timeout(2500)
    prueba("PC: quien ya eligio drive vuelve a la estanteria",
           pg.evaluate("document.body.dataset.paso") == "4", pg.evaluate("document.body.dataset.paso"))
    pg.click(".paso-basic"); pg.wait_for_timeout(3000)
    prueba("PC: el programa guardado sigue tras recargar", "HOLA.BAS" in pg.inner_text("#basLista"))
    prueba("PC: se recuerda la ultima pestana del BASIC",
           pg.evaluate("document.getElementById('etBasic').dataset.tab") == "man")
    prueba("PC: sin errores de JS", not errs, errs[:3])
    ctx.close()

    # -------------------------------------------------------------- telefono
    ctx = nav.new_context(viewport={"width": 390, "height": 844}, has_touch=True, is_mobile=True,
                          device_scale_factor=2)
    pg, errs = abre(ctx)
    pg.tap('.paso[data-ir="2"]'); pg.wait_for_timeout(400)
    ayuda = pg.evaluate("getComputedStyle(document.querySelector('.paso[data-ir=\"2\"]'), '::after').visibility")
    prueba("Tel: el texto de ayuda no queda pegado tras tocar", ayuda == "hidden", ayuda)
    pg.tap("#mandos > :nth-child(2)"); pg.wait_for_timeout(200)
    pg.tap('.sigue[data-ir="3"]'); pg.wait_for_timeout(400)
    lado = pg.evaluate("(() => { const a = document.querySelector('#sopCinta').getBoundingClientRect(), b = document.querySelector('#sopDisco').getBoundingClientRect(); return Math.abs(a.top - b.top) < 5; })()")
    prueba("Tel: cinta y disco lado a lado", lado)
    pg.tap('.sigue[data-soporte="disk"]'); pg.wait_for_timeout(400)
    prueba("Tel: la estanteria sale en lista", pg.evaluate("document.getElementById('secDisk').classList.contains('en-lista')"))
    pg.fill("#qDisk", "river raid"); pg.wait_for_timeout(400)
    pg.tap("#gridDisk li:not([hidden]) button"); pg.wait_for_timeout(9000)
    pg.evaluate("scrollTo(0,0)"); pg.wait_for_timeout(300)
    pg.screenshot(path=OUT + "prueba_tel_jugando.png")
    m = pg.evaluate("""(() => { const q = s => document.querySelector(s).getBoundingClientRect();
        const f = q('#fire'), st = q('#cStart'), tv = q('.tv');
        return { fuegoDer: f.right + 10, ancho: innerWidth, start: [Math.round(st.width), Math.round(st.height), Math.round(st.top - tv.bottom)],
                 alto: document.documentElement.scrollHeight, foto: document.querySelector('.d1050').offsetHeight }; })()""")
    prueba("Tel: el aro de FUEGO cabe en la pantalla", m["fuegoDer"] <= m["ancho"], m["fuegoDer"])
    prueba("Tel: START de 44px y pegado al televisor", m["start"][1] >= 44 and m["start"][2] < 80, m["start"])
    prueba("Tel: jugando cabe en una pantalla, sin scroll", m["alto"] <= 844, m["alto"])
    prueba("Tel: jugando no se ve la foto del drive", m["foto"] == 0, m["foto"])
    barra = pg.evaluate("(() => { const b = [...document.querySelectorAll('.tv-barra button')].filter(x => x.offsetWidth).map(x => Math.round(x.getBoundingClientRect().top)); return new Set(b).size; })()")
    prueba("Tel: los interruptores del televisor en una fila", barra == 1, barra)
    pg.tap("#bTeclado"); pg.wait_for_timeout(500)
    prueba("Tel: Teclado despliega el teclado del Atari", pg.is_visible(".xl"))
    pg.tap("#bTeclado"); pg.wait_for_timeout(300)

    # BASIC en el telefono
    pg.tap("#bVolver"); pg.wait_for_timeout(300)
    pg.tap(".paso-basic"); pg.wait_for_timeout(3500)
    pg.screenshot(path=OUT + "prueba_tel_basic.png")
    pg.tap("#basLinea")
    pg.fill("#basLinea", '10 PRINT "TEL"'); pg.tap("#basReturn"); pg.wait_for_timeout(1500)
    pg.fill("#basLinea", 'SAVE "D:TEL.BAS"'); pg.tap("#basReturn"); pg.wait_for_timeout(4000)
    prueba("Tel: la linea escribe y SAVE guarda", "TEL.BAS" in pg.inner_text("#basLista"))
    pg.tap('#basTabs [data-tab="rev"]'); pg.wait_for_timeout(300)
    pg.evaluate("scrollTo(0, 3000)"); pg.wait_for_timeout(500)
    fijo = pg.evaluate("(() => { const t = document.querySelector('.tv').getBoundingClientRect(), l = document.querySelector('.bas-linea').getBoundingClientRect(); return [Math.round(t.top), Math.round(t.bottom), Math.round(l.top), Math.round(l.bottom)]; })()")
    prueba("Tel: con scroll, pantalla y linea siguen arriba", fijo[0] <= 2 and abs(fijo[2] - fijo[1]) <= 8 and fijo[3] < 480, fijo)
    alto = pg.evaluate("document.documentElement.scrollHeight") / 844
    prueba("Tel: BASIC Revistas <= 6 pantallas", alto <= 6, round(alto, 1))
    pg.tap('#basTabs [data-tab="prog"]'); pg.wait_for_timeout(300)
    alto = pg.evaluate("document.documentElement.scrollHeight") / 844
    prueba("Tel: BASIC Programar <= 6 pantallas", alto <= 6, round(alto, 1))
    pg.tap('.ctv-basic [data-k="brk"]'); pg.wait_for_timeout(300)
    prueba("Tel: las teclas especiales no se salen",
           pg.evaluate("[...document.querySelectorAll('.ctv-basic button')].every(b => b.scrollWidth <= b.clientWidth + 1)"))
    chicos = pg.evaluate("""[...document.querySelectorAll('button, input, select, summary')].filter(e => {
        const r = e.getBoundingClientRect(), s = getComputedStyle(e);
        return r.width > 0 && r.height > 0 && s.visibility !== 'hidden' && !e.closest('dialog:not([open]),.teclas,.pad')
          && (r.height < 40); }).map(e => (e.id || e.className || e.tagName) + ' ' + Math.round(e.getBoundingClientRect().height))""")
    prueba("Tel: BASIC sin blancos de toque de menos de 40px", not chicos, chicos[:6])

    # recordar en el telefono
    pg.reload(wait_until="load")
    pg.wait_for_function("document.body.dataset.paso", timeout=60000); pg.wait_for_timeout(2500)
    prueba("Tel: vuelve a la estanteria y recuerda el joystick",
           pg.evaluate("document.body.dataset.paso") == "4"
           and pg.evaluate("document.body.classList.contains('mando-qs')"))
    m = pg.evaluate("document.documentElement.scrollHeight") / 844
    prueba("Tel: estanteria de discos <= 8,5 pantallas", m <= 8.5, round(m, 1))
    # pantalla completa: nada pegado encima del juego y el televisor entero
    pg.tap('.paso[data-ir="1"]'); pg.wait_for_timeout(300)
    ancho = pg.evaluate("document.querySelector('.tv').getBoundingClientRect().width")
    pg.tap('.paso[data-ir="4"]'); pg.wait_for_timeout(300)
    prueba("Tel: el televisor no se achica en la estanteria",
           abs(pg.evaluate("document.querySelector('.tv').getBoundingClientRect().width") - ancho) < 2)
    pg.fill("#qDisk", "river raid"); pg.wait_for_timeout(300)
    pg.tap("#gridDisk li:not([hidden]) button"); pg.wait_for_timeout(5000)
    pg.evaluate("scrollTo(0,0)"); pg.tap("#bFull"); pg.wait_for_timeout(1200)
    tapa = pg.evaluate("getComputedStyle(document.querySelector('.xl-tapa'), '::after').display")
    pg.screenshot(path=OUT + "prueba_tel_completa.png")
    prueba("Tel: pantalla completa sin rectangulo pegado", tapa == "none", tapa)
    pg.tap("#bSalirInm"); pg.wait_for_timeout(600)
    prueba("Tel: Salir vuelve de pantalla completa",
           not pg.evaluate("document.body.classList.contains('inmersivo')"))
    # girar jugando: entra solo a pantalla completa y sale al volver
    pg.set_viewport_size({"width": 844, "height": 390}); pg.wait_for_timeout(800)
    girado = pg.evaluate("document.body.classList.contains('inmersivo')")
    tapado = pg.evaluate("""(() => { const c = document.querySelector('#scr').getBoundingClientRect(); let n = 0;
        for (let i = 1; i < 8; i++) for (let j = 1; j < 6; j++) { const e = document.elementFromPoint(c.left + c.width*i/8, c.top + c.height*j/6);
          if (e && !e.closest('.screen-bezel,#gamepad,.btns')) n++; } return n; })()""")
    pg.set_viewport_size({"width": 390, "height": 844}); pg.wait_for_timeout(800)
    prueba("Tel: girar jugando pone pantalla completa y nada la tapa", girado and tapado == 0, [girado, tapado])
    prueba("Tel: volver a vertical sale de pantalla completa",
           not pg.evaluate("document.body.classList.contains('inmersivo')"))
    prueba("Tel: sin errores de JS", not errs, errs[:3])
    ctx.close()

    # elegir en un telefono chico: fichas y boton de seguir a la vista
    ctx = nav.new_context(viewport={"width": 360, "height": 640}, has_touch=True, is_mobile=True)
    pg, errs = abre(ctx)
    VE = """(sel) => { const v = e => { const r = e.getBoundingClientRect(); return r.top >= -1 && r.bottom <= innerHeight + 1 && r.height > 0; };
        const s = [...document.querySelectorAll('.etapa .sigue')].filter(e => e.offsetParent);
        return [...document.querySelectorAll(sel)].every(v) && s.length > 0 && s.every(v); }"""
    pg.wait_for_timeout(800)
    prueba("Tel 360x640: paso 1 con las 4 maquinas y el boton a la vista", pg.evaluate(VE, "#maquinas > *"))
    pg.tap('.sigue[data-ir="2"]'); pg.wait_for_timeout(900)
    prueba("Tel 360x640: paso 2 con los 3 mandos y el boton a la vista", pg.evaluate(VE, "#mandos > *"))
    pg.tap('.sigue[data-ir="3"]'); pg.wait_for_timeout(900)
    prueba("Tel 360x640: paso 3 con cinta, disco y botones a la vista", pg.evaluate(VE, "#etSoporte .soporte"))
    prueba("Tel 360x640: sin errores de JS", not errs, errs[:3])
    ctx.close()
    nav.close()

print("\n%d fallas" % len(fallas) + (": " + ", ".join(fallas) if fallas else ""))
sys.exit(1 if fallas else 0)
