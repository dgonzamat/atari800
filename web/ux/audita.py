"""Auditoria de UI/UX: recorre cada estado de la pagina en computador y en
telefono, saca una foto y mide lo que se puede medir (alto de pagina,
blancos de toque chicos, texto chico, desborde lateral).

Uso:  python3.13 audita.py [URL] [carpeta_de_salida]
      (la pagina servida con: python3.13 -m http.server 8920)

Ojo con la columna de contraste: mira background-color, y las fichas
oscuras se pintan con un degradado, asi que ahi da falsos positivos. Sirve
para el texto que va sobre el papel claro.
"""
import json, os, sys
from playwright.sync_api import sync_playwright

URL = sys.argv[1] if len(sys.argv) > 1 else "http://localhost:8920/atari.html"
CH = dict(executable_path="/opt/pw-browsers/chromium",
          args=["--use-gl=swiftshader", "--enable-unsafe-swiftshader"])
OUT = (sys.argv[2] if len(sys.argv) > 2 else "ux_salida") + "/"

MIDE = r"""
() => {
  const vis = e => { const r = e.getBoundingClientRect(), s = getComputedStyle(e);
    return r.width > 0 && r.height > 0 && s.visibility !== 'hidden' && s.display !== 'none' && +s.opacity > 0.05; };
  const lum = c => { const m = c.match(/[\d.]+/g).map(Number); const f = v => { v/=255; return v<=0.03928? v/12.92 : Math.pow((v+0.055)/1.055,2.4); };
    return [0.2126*f(m[0])+0.7152*f(m[1])+0.0722*f(m[2]), m.length>3? m[3] : 1]; };
  const fondo = e => { for (let n = e; n && n.nodeType === 1; n = n.parentElement) { const b = getComputedStyle(n).backgroundColor;
      const m = b.match(/[\d.]+/g); if (m && (m.length < 4 || +m[3] > 0.5)) return b; } return 'rgb(242,242,240)'; };
  const chicos = [], bajos = [], letras = {};
  document.querySelectorAll('button, a, input, select, summary, [role=button]').forEach(e => {
    if (!vis(e) || e.closest('dialog:not([open])')) return;
    const r = e.getBoundingClientRect();
    if (r.width < 40 || r.height < 40) chicos.push((e.id || e.className || e.tagName).toString().slice(0,28) + ' ' + Math.round(r.width) + 'x' + Math.round(r.height) + ' "' + (e.textContent||e.getAttribute('aria-label')||'').trim().slice(0,18) + '"');
  });
  const w = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  let n; const vistos = new Set();
  while ((n = w.nextNode())) {
    const t = n.nodeValue.trim(); if (t.length < 3) continue;
    const e = n.parentElement; if (!e || !vis(e) || e.closest('script,style,dialog:not([open]),.sr')) continue;
    const s = getComputedStyle(e), px = parseFloat(s.fontSize);
    const k = Math.round(px); letras[k] = (letras[k]||0) + 1;
    const [l1] = lum(s.color), [l2] = lum(fondo(e));
    const c = (Math.max(l1,l2)+0.05)/(Math.min(l1,l2)+0.05);
    const clave = (e.className||e.tagName) + '|' + s.color;
    if (c < 4.5 && !vistos.has(clave)) { vistos.add(clave); bajos.push(c.toFixed(1) + ':1 ' + Math.round(px) + 'px .' + (e.className||e.tagName).toString().slice(0,22) + ' "' + t.slice(0,26) + '"'); }
  }
  return { paso: document.body.dataset.paso, alto: document.documentElement.scrollHeight, vp: innerHeight,
           desborde: document.documentElement.scrollWidth - innerWidth,
           chicos: chicos.length, chicos_ej: chicos.slice(0, 14), bajos: bajos.slice(0, 16), letras };
}
"""


def recorre(nav, ancho, alto, tactil):
    nombre = "pc" if not tactil else "tel"
    ctx = nav.new_context(viewport={"width": ancho, "height": alto}, has_touch=tactil, is_mobile=tactil,
                          device_scale_factor=2 if tactil else 1)
    pg = ctx.new_page(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto(URL, wait_until="load"); pg.wait_for_timeout(3500)
    res = {}

    def foto(clave, entera=True):
        pg.wait_for_timeout(600)
        pg.screenshot(path=OUT + "%s_%s.png" % (nombre, clave), full_page=entera)
        res[clave] = pg.evaluate(MIDE)

    toca = (lambda s: pg.tap(s)) if tactil else (lambda s: pg.click(s))
    foto("1_modelo")
    toca('.sigue[data-ir="2"]'); foto("2_joystick")
    toca('.sigue[data-ir="3"]'); foto("3_drive")
    toca('.sigue[data-soporte="disk"]'); foto("4_juego_disco")
    pg.fill("#qDisk", "river raid"); pg.wait_for_timeout(500)
    b = pg.query_selector("#gridDisk li:not([hidden]) button"); b.scroll_into_view_if_needed()
    (b.tap() if tactil else b.click()); pg.wait_for_timeout(8000)
    pg.evaluate("window.scrollTo(0,0)"); foto("5_jugando")
    pg.screenshot(path=OUT + "%s_5_jugando_vista.png" % nombre, full_page=False)
    toca("#bVolver"); pg.wait_for_timeout(400)
    toca('.paso[data-ir="3"]'); pg.wait_for_timeout(300)
    toca('.sigue[data-soporte="cas"]'); foto("4_juego_cinta")
    toca(".paso-basic"); pg.wait_for_timeout(2500); pg.evaluate("window.scrollTo(0,0)")
    foto("6_basic")
    pg.screenshot(path=OUT + "%s_6_basic_vista.png" % nombre, full_page=False)
    res["basic_secciones"] = pg.evaluate("""() => [...document.querySelectorAll('#etBasic .bas-caja, #etBasic .bas-linea')].map(e => {
        const r = e.getBoundingClientRect(); const h = e.querySelector('h3');
        return (h ? h.textContent.trim().slice(0,30) : e.className) + ': y=' + Math.round(r.top + scrollY) + ' alto=' + Math.round(r.height); })""")
    res["errores"] = errs
    ctx.close()
    return res


os.makedirs(OUT, exist_ok=True)
with sync_playwright() as p:
    nav = p.chromium.launch(**CH)
    out = {"pc": recorre(nav, 1280, 800, False), "tel": recorre(nav, 390, 844, True)}
    nav.close()
json.dump(out, open(OUT + "medidas.json", "w"), indent=1, ensure_ascii=False)
for d, r in out.items():
    print("==", d)
    for k, v in r.items():
        if isinstance(v, dict):
            print(" %-16s alto %5d (%.1f pantallas) desborde %d  blancos<40px %d  letras %s" % (
                k, v["alto"], v["alto"] / v["vp"], v["desborde"], v["chicos"],
                dict(sorted(((int(a), b) for a, b in v["letras"].items()))[:6])))
        else:
            print(" ", k, v)
