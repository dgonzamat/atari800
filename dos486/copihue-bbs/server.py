#!/usr/bin/env python3
"""Copihue BBS: BBS chileno por telnet (foros, chat en vivo, quien esta, usuarios).

Sin dependencias externas. Datos en SQLite (COPIHUE_DATA, por defecto /data).
Variables: COPIHUE_PORT (2323, telnet; 0 = apagado), COPIHUE_WS_PORT (0 = apagado;
WebSocket con subprotocolo "binary", el de fTelnet, y una pagina web con terminal),
COPIHUE_SYSOP (alias con permisos de SysOp), COPIHUE_MAX_SESSIONS (30),
COPIHUE_IDLE_MINUTES (15), TZ (America/Santiago).
Respaldo opcional en un dataset de Hugging Face: COPIHUE_HF_REPO (p. ej. usuario/copihue-bbs-data)
y HF_TOKEN; al iniciar se descarga copihue.db y tras cada cambio se sube (cada 60 s como maximo).
"""
import asyncio, codecs, datetime, hashlib, hmac, os, re, secrets, sqlite3, time

PORT = int(os.environ.get('COPIHUE_PORT', '2323'))
WS_PORT = int(os.environ.get('COPIHUE_WS_PORT', '0'))
HF_REPO = os.environ.get('COPIHUE_HF_REPO', '').strip()
DATA = os.environ.get('COPIHUE_DATA', '/data')
SYSOP = os.environ.get('COPIHUE_SYSOP', '').strip().lower()
MAX_SESSIONS = int(os.environ.get('COPIHUE_MAX_SESSIONS', '30'))
IDLE = int(os.environ.get('COPIHUE_IDLE_MINUTES', '15')) * 60
try:
    from zoneinfo import ZoneInfo
    TZ = ZoneInfo(os.environ.get('TZ', 'America/Santiago'))
except Exception:  # sin base de zonas horarias: UTC
    TZ = datetime.timezone.utc

FOROS = [('general', 'General'), ('juegos', 'Juegos'), ('retro', 'Computacion retro'), ('chile', 'Cosas de Chile')]
# Bandera de Chile (18x6): canton azul con estrella blanca, franja blanca, franja roja
FLAG = [
    '{1}██████{15}████████████',
    '{b1}{15}  *   {b0}{15}████████████',
    '{1}██████{15}████████████',
    '{4}██████████████████',
    '{4}██████████████████',
    '{4}██████████████████']
LOGO = [
    ' ██████╗ ██████╗ ██████╗ ██╗██╗  ██╗██╗   ██╗███████╗',
    '██╔════╝██╔═══██╗██╔══██╗██║██║  ██║██║   ██║██╔════╝',
    '██║     ██║   ██║██████╔╝██║███████║██║   ██║█████╗  ',
    '██║     ██║   ██║██╔═══╝ ██║██╔══██║██║   ██║██╔══╝  ',
    '╚██████╗╚██████╔╝██║     ██║██║  ██║╚██████╔╝███████╗',
    ' ╚═════╝ ╚═════╝ ╚═╝     ╚═╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝']
CGA2ANSI = [0, 4, 2, 6, 1, 5, 3, 7]
IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
ECHO, SGA = 1, 3
ALIAS_RE = re.compile(r'^[A-Za-z0-9ÁÉÍÓÚÑáéíóúñ _.-]{3,16}$')

# ---------------------------------------------------------------- datos
os.makedirs(DATA, exist_ok=True)
DB_PATH = os.path.join(DATA, 'copihue.db')
if HF_REPO and os.environ.get('HF_TOKEN'):
    try:
        from huggingface_hub import hf_hub_download
        src = hf_hub_download(HF_REPO, 'copihue.db', repo_type='dataset', token=os.environ['HF_TOKEN'])
        import shutil; shutil.copyfile(src, DB_PATH)
        print(f'Base restaurada desde {HF_REPO}', flush=True)
    except Exception as e:
        print(f'Sin respaldo previo en {HF_REPO} ({type(e).__name__}): se parte con una base nueva', flush=True)
db = sqlite3.connect(DB_PATH, isolation_level=None)
db.execute('PRAGMA journal_mode=WAL')
db.execute('''CREATE TABLE IF NOT EXISTS users (alias TEXT PRIMARY KEY COLLATE NOCASE, salt TEXT, pw TEXT,
              created TEXT, last TEXT, calls INTEGER DEFAULT 0)''')
db.execute('''CREATE TABLE IF NOT EXISTS msgs (id INTEGER PRIMARY KEY AUTOINCREMENT, foro TEXT, alias TEXT,
              subject TEXT, body TEXT, at TEXT)''')

dirty = False
def changed():
    global dirty
    dirty = True

async def backup_loop():
    """Sube copihue.db al dataset de Hugging Face cuando hubo cambios (maximo cada 60 s)."""
    global dirty
    if not (HF_REPO and os.environ.get('HF_TOKEN')):
        return
    from huggingface_hub import HfApi
    api = HfApi(token=os.environ['HF_TOKEN'])
    try:
        api.create_repo(HF_REPO, repo_type='dataset', private=True, exist_ok=True)
    except Exception as e:
        print(f'No se pudo crear el dataset {HF_REPO}: {e!r}', flush=True)
    while True:
        await asyncio.sleep(60)
        if not dirty:
            continue
        dirty = False
        try:
            snap = DB_PATH + '.snap'
            bk = sqlite3.connect(snap); db.backup(bk); bk.close()
            await asyncio.to_thread(api.upload_file, path_or_fileobj=snap, path_in_repo='copihue.db', repo_id=HF_REPO,
                                    repo_type='dataset', commit_message='respaldo de Copihue BBS')
            print(f'[{now_iso()}] respaldo subido a {HF_REPO}', flush=True)
        except Exception as e:
            dirty = True
            print(f'[{now_iso()}] fallo el respaldo: {e!r}', flush=True)

def now_iso():
    return datetime.datetime.now(TZ).isoformat(timespec='seconds')

def fmt(iso):
    try:
        return datetime.datetime.fromisoformat(iso).astimezone(TZ).strftime('%d-%m-%y %H:%M')
    except Exception:
        return ''

def pw_hash(pw, salt):
    return hashlib.pbkdf2_hmac('sha256', pw.encode(), bytes.fromhex(salt), 200_000).hex()

def clean(t, n):
    """Solo texto imprimible: nunca secuencias ANSI ni controles de otros usuarios."""
    return ''.join(c for c in str(t) if c.isprintable())[:n]

# ---------------------------------------------------------------- sesiones
sessions = set()

class Hangup(Exception):
    pass

class Session:
    def __init__(self, reader, writer):
        self.r, self.w = reader, writer
        self.enc, self.ansi = 'cp437', True
        self.alias, self.where = '', 'Entrando'
        self.inbox = []           # lineas de chat pendientes
        self.typing = ''          # lo que esta escribiendo (para reimprimirlo)
        self.prompt = ''
        self.tn = 0               # estado del parser telnet
        self.pending_cr = False
        self.dec = codecs.getincrementaldecoder('cp437')()

    # ---- salida
    def colorize(self, text):
        def rep(m):
            n = int(m.group(2))
            if not self.ansi:
                return ''
            if m.group(1):                      # {bN}: color de fondo
                return f'\x1b[4{CGA2ANSI[n & 7]}m'
            if n < 0:
                return '\x1b[0m'
            return f'\x1b[{1 if n >= 8 else 0};3{CGA2ANSI[n & 7]}m'
        return re.sub(r'\{(b?)(-?\d{1,2})\}', rep, text)

    async def send(self, text):
        data = self.colorize(text).replace('\r\n', '\n').replace('\n', '\r\n').encode(self.enc, 'replace')
        self.w.write(data.replace(b'\xff', b'\xff\xff'))
        await self.w.drain()

    # ---- entrada
    async def _byte(self):
        while True:
            try:
                b = await asyncio.wait_for(self.r.read(1), timeout=IDLE)
            except asyncio.TimeoutError:
                await self.send('\n{12}Tiempo de inactividad agotado. Hasta pronto.{7}\n')
                raise Hangup()
            if not b:
                raise Hangup()
            c = b[0]
            if self.tn == 0:
                if c == IAC: self.tn = 1; continue
                return c
            if self.tn == 1:
                if c == IAC: self.tn = 0; return c
                if c in (WILL, WONT, DO, DONT): self.tn = 2; continue
                if c == SB: self.tn = 3; continue
                self.tn = 0; continue
            if self.tn == 2: self.tn = 0; continue
            if self.tn == 3:
                if c == IAC: self.tn = 4
                continue
            if self.tn == 4:
                self.tn = 0 if c == SE else 3
                continue

    async def key(self):
        """Una tecla (los menus no esperan Enter)."""
        while True:
            c = await self._byte()
            if self.pending_cr and c in (10, 0):
                self.pending_cr = False; continue
            self.pending_cr = (c == 13)
            if c == 13 or c == 10: return '\r'
            if c in (8, 127): return '\b'
            if c == 27: return '\x1b'
            if c >= 32:
                ch = self.dec.decode(bytes([c]))
                if ch: return ch

    async def line(self, maxlen=70, mask=False, chat=False):
        buf = ''
        while True:
            if chat and self.inbox:
                await self.flush_chat(buf)
            try:
                k = await asyncio.wait_for(self.key(), timeout=0.3 if chat else None)
            except asyncio.TimeoutError:
                continue
            if k == '\r':
                await self.send('\n'); self.typing = ''; return buf
            if k == '\b':
                if buf: buf = buf[:-1]; await self.send('\b \b')
                continue
            if k == '\x1b' or not k.isprintable() or len(buf) >= maxlen:
                continue
            buf += k; self.typing = buf
            await self.send('*' if mask else k)

    async def flush_chat(self, buf):
        lines = self.inbox; self.inbox = []
        await self.send('\r\x1b[K' if self.ansi else '\r\n')
        for l in lines:
            await self.send(l + '\n')
        await self.send(self.prompt + buf)

# ---------------------------------------------------------------- pantallas
async def box(s, title, lines, width=66, c=12):
    t = f' {title} '
    side = width - 2 - len(t)
    out = f'{{{c}}}╔{"═" * (side // 2)}{{15}}{t}{{{c}}}{"═" * (side - side // 2)}╗\n'
    for l in lines:
        vis = re.sub(r'\{b?-?\d{1,2}\}', '', l)
        out += f'{{{c}}}║{{7}}{l}{" " * max(0, width - 2 - len(vis))}{{{c}}}║\n'
    out += f'{{{c}}}╚{"═" * (width - 2)}╝{{7}}\n'
    await s.send(out)

async def anykey(s, msg='[Pulsa una tecla]'):
    await s.send(f'{{8}}   {msg}{{7}}')
    await s.key()
    await s.send('\n')

async def menu_key(s, allowed):
    while True:
        k = (await s.key()).upper()
        if k in allowed:
            await s.send(k + '\n'); return k

async def login(s):
    await s.send('\n{7}Juego de caracteres:  {14}[1]{7} CP437 (DOS, SyncTERM, fTelnet)   {14}[2]{7} UTF-8\n'
                 '{7}Elige (Enter = 1): ')
    k = await s.key()
    if k == '2':
        s.enc = 'utf-8'; s.dec = codecs.getincrementaldecoder('utf-8')()
    await s.send('\x1b[2J\x1b[H\n' if s.ansi else '\n')
    for i, l in enumerate(LOGO):
        await s.send(f'{{{[12, 12, 12, 4, 4, 8][i]}}} {l}  {FLAG[i]}{{7}}\n')
    await s.send('{12}   B   B   S {8}  ·  Santiago de Chile  ·  telnet desde Docker{7}\n\n')
    for _ in range(3):
        await s.send('{11}   Alias (o NUEVO para registrarte){7}: {15}')
        alias = clean((await s.line(16)).strip(), 16)
        if not alias:
            continue
        if alias.upper() == 'NUEVO':
            return await register(s)
        row = db.execute('SELECT alias, salt, pw FROM users WHERE alias = ?', (alias,)).fetchone()
        await s.send('{11}   Clave{7}: {15}')
        pw = await s.line(32, mask=True)
        if row and hmac.compare_digest(pw_hash(pw, row[1]), row[2]):
            return row[0]
        await s.send('{12}   Alias o clave incorrectos.{7}\n')
    raise Hangup()

async def register(s):
    while True:
        await s.send('{11}   Elige tu alias (3 a 16 caracteres){7}: {15}')
        alias = clean((await s.line(16)).strip(), 16)
        if not ALIAS_RE.match(alias) or alias.upper() == 'NUEVO':
            await s.send('{12}   Usa entre 3 y 16 letras, numeros, espacios, punto, guion o guion bajo.{7}\n'); continue
        if db.execute('SELECT 1 FROM users WHERE alias = ?', (alias,)).fetchone():
            await s.send('{12}   Ese alias ya existe.{7}\n'); continue
        break
    while True:
        await s.send('{11}   Clave (minimo 4 caracteres){7}: {15}')
        p1 = await s.line(32, mask=True)
        await s.send('{11}   Repite la clave{7}: {15}')
        p2 = await s.line(32, mask=True)
        if len(p1) >= 4 and p1 == p2:
            break
        await s.send('{12}   Las claves no coinciden o son muy cortas.{7}\n')
    salt = secrets.token_hex(16)
    db.execute('INSERT INTO users (alias, salt, pw, created, last, calls) VALUES (?,?,?,?,?,0)',
               (alias, salt, pw_hash(p1, salt), now_iso(), ''))
    changed()
    await s.send(f'{{10}}   Bienvenido/a a Copihue BBS, {alias}.{{7}}\n')
    return alias

async def main_menu(s):
    is_sysop = s.alias.lower() == SYSOP
    while True:
        s.where = 'Menu principal'
        await s.send('\x1b[2J\x1b[H\n' if s.ansi else '\n')
        await box(s, 'COPIHUE BBS  ·  MENU PRINCIPAL', ['',
            '   {14}[F]{7} Foros de mensajes        {14}[C]{7} Chat en vivo',
            '   {14}[Q]{7} Quien esta conectado     {14}[U]{7} Usuarios',
            '   {14}[A]{7} Acerca de Copihue BBS    {14}[S]{7} Salir (colgar)', ''])
        await s.send(f'\n{{8}}   {s.alias}{" · SysOp" if is_sysop else ""}   ·   {len(sessions)} conectados{{7}}\n\n{{11}}   Comando{{7}}: {{15}}')
        k = await menu_key(s, 'FCQUAS')
        if k == 'F': await foros(s, is_sysop)
        elif k == 'C': await chat(s)
        elif k == 'Q': await quien(s)
        elif k == 'U': await usuarios(s)
        elif k == 'A': await acerca(s)
        else: return

async def foros(s, is_sysop):
    while True:
        s.where = 'Foros'
        counts = dict(db.execute('SELECT foro, COUNT(*) FROM msgs GROUP BY foro').fetchall())
        await s.send('\x1b[2J\x1b[H\n' if s.ansi else '\n')
        await box(s, 'FOROS DE COPIHUE BBS', [''] + [f'   {{14}}{i + 1}{{7}}. {n:<24}{{8}}{counts.get(f, 0):>5} mensajes{{7}}'
                                                     for i, (f, n) in enumerate(FOROS)] + [''], 58)
        await s.send('\n{11}   Elige un foro (1-4) o [V]olver{7}: {15}')
        k = await menu_key(s, '1234V')
        if k == 'V': return
        await foro(s, FOROS[int(k) - 1], is_sysop)

async def foro(s, f, is_sysop):
    fid, fname = f
    while True:
        s.where = 'Foro ' + fname
        rows = db.execute('SELECT id, alias, subject, at FROM msgs WHERE foro = ? ORDER BY id DESC LIMIT 9', (fid,)).fetchall()
        await s.send('\x1b[2J\x1b[H\n' if s.ansi else '\n')
        await s.send(f'{{12}}   FORO {{15}}{fname}{{8}}   ·   ultimos {len(rows)} mensajes\n{{8}}   {"─" * 74}{{7}}\n')
        if not rows:
            await s.send('{8}   Todavia no hay mensajes. Escribe el primero con [E].{7}\n')
        for i, (mid, al, subj, at) in enumerate(rows):
            await s.send(f'   {{14}}{i + 1}{{7}}. {{15}}{subj[:33]:<34}{{11}}{al:<17}{{8}}{fmt(at)}{{7}}\n')
        await s.send(f'\n{{11}}   {"[1-" + str(len(rows)) + "] leer · " if rows else ""}[E]scribir · [V]olver{{7}}: {{15}}')
        k = await menu_key(s, 'VE' + ''.join(str(i + 1) for i in range(len(rows))))
        if k == 'V': return
        if k == 'E': await escribir(s, fid, fname); continue
        await leer(s, rows[int(k) - 1][0], is_sysop)

async def leer(s, mid, is_sysop):
    row = db.execute('SELECT alias, subject, body, at FROM msgs WHERE id = ?', (mid,)).fetchone()
    if not row: return
    al, subj, body, at = row
    await s.send('\x1b[2J\x1b[H\n' if s.ansi else '\n')
    await s.send(f'{{11}}   De: {{15}}{al}{{11}}     Fecha: {{15}}{fmt(at)}\n{{11}}   Asunto: {{15}}{subj}\n{{8}}   {"─" * 74}{{7}}\n\n')
    for l in body.split('\n')[:15]:
        await s.send('   ' + l + '\n')
    can_del = is_sysop or al.lower() == s.alias.lower()
    await s.send(f'\n{{8}}   [Pulsa una tecla para volver]{"   [B] borrar" if can_del else ""}{{7}}')
    k = (await s.key()).upper()
    await s.send('\n')
    if k == 'B' and can_del:
        await s.send('{12}   ¿Borrar el mensaje? (S/N){7}: ')
        if await menu_key(s, 'SN') == 'S':
            db.execute('DELETE FROM msgs WHERE id = ?', (mid,)); changed()
            await s.send('{10}   Mensaje borrado.{7}\n'); await asyncio.sleep(0.8)

async def escribir(s, fid, fname):
    s.where = 'Escribiendo en ' + fname
    await s.send(f'\n{{12}}   NUEVO MENSAJE EN {{15}}{fname}{{7}}\n{{11}}   Asunto{{7}}: {{15}}')
    subj = clean((await s.line(40)).strip(), 40)
    if not subj:
        await s.send('{8}   Sin asunto: cancelado.{7}\n'); await asyncio.sleep(0.8); return
    await s.send('{7}   Escribe el mensaje (hasta 15 lineas). Una linea vacia lo termina.\n\n')
    lines = []
    while len(lines) < 15:
        await s.send(f'{{8}}{len(lines) + 1:>3}>{{15}} ')
        l = clean(await s.line(74), 74)
        if not l.strip(): break
        lines.append(l)
    if not lines:
        await s.send('{8}   Mensaje vacio: cancelado.{7}\n'); await asyncio.sleep(0.8); return
    await s.send('{11}   ¿Publicar? (S/N){7}: ')
    if await menu_key(s, 'SN') == 'S':
        db.execute('INSERT INTO msgs (foro, alias, subject, body, at) VALUES (?,?,?,?,?)', (fid, s.alias, subj, '\n'.join(lines), now_iso()))
        changed()
        await s.send('{10}   Mensaje publicado.{7}\n')
        broadcast(f'{{8}}* {s.alias} publico "{subj}" en {fname}{{7}}', exclude=s, only_chat=True)
    await asyncio.sleep(0.8)

def broadcast(text, exclude=None, only_chat=False):
    for o in list(sessions):
        if o is exclude or not o.alias: continue
        if only_chat and o.where != 'Chat en vivo': continue
        o.inbox.append(text)

async def chat(s):
    s.where = 'Chat en vivo'
    await s.send('\x1b[2J\x1b[H\n' if s.ansi else '\n')
    await s.send('{12}   CHAT EN VIVO{8}  ·  no se guarda  ·  /salir para volver{7}\n\n')
    broadcast(f'{{8}}* {s.alias} entro al chat{{7}}', exclude=s, only_chat=True)
    s.prompt = '{15}> '
    try:
        while True:
            await s.send(s.prompt)
            t = clean((await s.line(200, chat=True)).strip(), 200)
            if t.lower() == '/salir': break
            if t:
                line = f'{{11}}{s.alias}{{7}}: {t}'
                broadcast(line, exclude=s, only_chat=True)
    finally:
        s.prompt = ''
        broadcast(f'{{8}}* {s.alias} salio del chat{{7}}', exclude=s, only_chat=True)

async def quien(s):
    rows = [f'   {{15}}{(o.alias or "(entrando)"):<18}{{8}}{o.where[:34]}{{7}}{"  {14}(tu){7}" if o is s else ""}' for o in list(sessions)]
    await s.send('\x1b[2J\x1b[H\n' if s.ansi else '\n')
    await box(s, 'QUIEN ESTA CONECTADO', [''] + rows + [''])
    await anykey(s)

async def usuarios(s):
    rows = db.execute('SELECT alias, created, calls FROM users ORDER BY created LIMIT 18').fetchall()
    lines = [f'   {{15}}{a:<18}{{8}}desde {fmt(c)}   {n} llamadas{{7}}' for a, c, n in rows] or ['   {8}Todavia no hay usuarios.{7}']
    await s.send('\x1b[2J\x1b[H\n' if s.ansi else '\n')
    await box(s, 'USUARIOS DE COPIHUE BBS', [''] + lines + [''], 62)
    await anykey(s)

async def acerca(s):
    await s.send('\x1b[2J\x1b[H\n' if s.ansi else '\n')
    await box(s, 'ACERCA DE COPIHUE BBS', ['',
        '   Copihue BBS es un BBS chileno creado en 2026, servido por telnet',
        '   desde un contenedor Docker. Los foros y el chat son reales: los',
        '   escriben las personas que llaman. Nada viene inventado.',
        '',
        '   Homenaje a la escena BBS chilena de los anos 90. Una de esas',
        '   BBS fue Araucaria BBS, de Nunoa (Santiago), cuyo sysop era',
        '   Fernando Jorquera; aparece en un listado de BBS hispanos de',
        '   2002 con conexion solo por telefono.', ''], 72)
    await anykey(s)

# ---------------------------------------------------------------- servidor
async def handle(reader, writer):
    s = Session(reader, writer)
    peer = writer.get_extra_info('peername')
    try:
        if len(sessions) >= MAX_SESSIONS:
            await s.send('Copihue BBS esta lleno. Intenta mas tarde.\n'); return
        sessions.add(s)
        writer.write(bytes([IAC, WILL, ECHO, IAC, WILL, SGA, IAC, DO, SGA])); await writer.drain()
        s.alias = await login(s)
        row = db.execute('SELECT last, calls FROM users WHERE alias = ?', (s.alias,)).fetchone()
        last, calls = row if row else ('', 0)
        nuevos = db.execute('SELECT COUNT(*) FROM msgs WHERE at > ? AND alias <> ? COLLATE NOCASE', (last or '', s.alias)).fetchone()[0]
        db.execute('UPDATE users SET last = ?, calls = calls + 1 WHERE alias = ?', (now_iso(), s.alias)); changed()
        await s.send(f'\n{{7}}   Llamada numero {{15}}{calls + 1}{{7}} de {s.alias}.'
                     f'{" Ultima: " + fmt(last) + "." if last else ""} Mensajes nuevos: {{14}}{nuevos}{{7}}\n\n')
        print(f'[{now_iso()}] entra {s.alias} desde {peer}', flush=True)
        await anykey(s)
        await main_menu(s)
        await s.send('\n{14}   Gracias por llamar a Copihue BBS. ¡Vuelve pronto!{7}\n')
    except (Hangup, ConnectionError, asyncio.IncompleteReadError):
        pass
    except Exception as e:  # nunca botar el servidor por una sesion
        print(f'[{now_iso()}] error en sesion {peer}: {e!r}', flush=True)
    finally:
        sessions.discard(s)
        try:
            writer.close(); await writer.wait_closed()
        except Exception:
            pass

class WSReader:
    """Adapta un WebSocket a la interfaz de lectura de asyncio (read)."""
    def __init__(self, ws):
        self.ws, self.buf = ws, bytearray()
    async def read(self, n):
        while not self.buf:
            try:
                m = await self.ws.recv()
            except Exception:
                return b''
            self.buf += m if isinstance(m, (bytes, bytearray)) else m.encode('latin-1', 'replace')
        out = bytes(self.buf[:n]); del self.buf[:n]; return out

class WSWriter:
    def __init__(self, ws):
        self.ws, self.out = ws, bytearray()
    def write(self, b):
        self.out += b
    async def drain(self):
        if self.out:
            data, self.out = bytes(self.out), bytearray()
            if self.ws.subprotocol == 'plain':
                await self.ws.send(data.decode('latin-1'))
            else:
                await self.ws.send(data)
    def get_extra_info(self, k):
        return self.ws.remote_address if k == 'peername' else None
    def close(self):
        pass
    async def wait_closed(self):
        await self.ws.close()

PAGE = '''<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Copihue BBS</title><style>body{margin:0;background:#000;color:#ccc;font:15px system-ui;display:flex;flex-direction:column;align-items:center;gap:10px;padding:14px}
a{color:#ffd75a}#fTelnetContainer{max-width:100%}</style></head><body>
<h1 style="color:#ff5555;font:700 28px monospace;margin:4px;display:flex;align-items:center;gap:12px">
<svg width="42" height="28" viewBox="0 0 3 2" aria-label="Bandera de Chile"><rect width="3" height="2" fill="#d52b1e"/><rect width="3" height="1" fill="#fff"/><rect width="1" height="1" fill="#0039a6"/>
<polygon fill="#fff" points="0.5,0.2 0.5706,0.4029 0.7853,0.4073 0.6142,0.5371 0.6763,0.7427 0.5,0.62 0.3237,0.7427 0.3858,0.5371 0.2147,0.4073 0.4294,0.4029"/></svg>COPIHUE BBS</h1>
<div>BBS chileno &middot; Santiago de Chile &middot; tambi&eacute;n desde <a href="https://github.com/dgonzamat/atari800">DOS 486</a></div>
<div id="fTelnetContainer" class="fTelnetContainer"></div>
<script src="https://embed-v2.ftelnet.ca/ftelnet/ftelnet.norip.noxfer.min.js" id="fTelnetScript"></script>
<script>var o=new fTelnetOptions();o.ConnectionType='telnet';o.Emulation='ansi-bbs';o.Font='CP437';o.Hostname=location.hostname;
o.Port=location.protocol==='https:'?443:(+location.port||80);o.ProxyHostname='';o.SplashScreen='';o.AutoConnect=true;o.ForceWss=location.protocol==='https:';
var ft=new fTelnetClient('fTelnetContainer',o);setTimeout(function(){try{ft.Connect()}catch(e){}},400);</script></body></html>'''

async def ws_main():
    import websockets
    from websockets.http11 import Response
    from websockets.datastructures import Headers
    def process_request(connection, request):
        if request.headers.get('Upgrade', '').lower() != 'websocket':
            body = PAGE.encode()
            return Response(200, 'OK', Headers([('Content-Type', 'text/html; charset=utf-8'), ('Content-Length', str(len(body)))]), body)
        return None
    async def ws_handler(ws):
        await handle(WSReader(ws), WSWriter(ws))
    async with websockets.serve(ws_handler, '0.0.0.0', WS_PORT, subprotocols=['binary', 'plain'],
                                process_request=process_request, max_size=2 ** 16, ping_interval=30):
        print(f'Copihue BBS: WebSocket y pagina web en el puerto {WS_PORT}', flush=True)
        await asyncio.Future()

async def main():
    tasks = [asyncio.create_task(backup_loop())]
    if WS_PORT:
        tasks.append(asyncio.create_task(ws_main()))
    if PORT:
        srv = await asyncio.start_server(handle, '0.0.0.0', PORT)
        print(f'Copihue BBS: telnet en el puerto {PORT} (datos en {DATA})', flush=True)
        tasks.append(asyncio.create_task(srv.serve_forever()))
    await asyncio.gather(*tasks)

if __name__ == '__main__':
    asyncio.run(main())
