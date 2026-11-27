// DOS 486 de escritorio: sirve la version web desde un servidor local (127.0.0.1)
// y la abre en su propia ventana. Fuera del visor de claude.ai no hay bloqueo de red,
// asi que el BBS se conecta dentro de la misma pantalla.
const { app, BrowserWindow, Menu, shell } = require('electron');
const http = require('http');
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, 'web');
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.wasm': 'application/wasm',
  '.json': 'application/json', '.txt': 'text/plain; charset=utf-8' };

function serve() {
  return new Promise((resolve) => {
    const srv = http.createServer((req, res) => {
      let p = decodeURIComponent(new URL(req.url, 'http://local').pathname);
      if (p === '/') p = '/index.html';
      const file = path.normalize(path.join(ROOT, p));
      if (!file.startsWith(ROOT + path.sep)) { res.writeHead(403); res.end(); return; }
      fs.stat(file, (err, st) => {
        if (err || !st.isFile()) { res.writeHead(404); res.end(); return; }
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream', 'Content-Length': st.size });
        fs.createReadStream(file).pipe(res);
      });
    });
    srv.listen(0, '127.0.0.1', () => resolve(srv.address().port));
  });
}

app.whenReady().then(async () => {
  Menu.setApplicationMenu(null);
  const port = await serve();
  const win = new BrowserWindow({
    width: 980, height: 1000, minWidth: 420, minHeight: 500,
    title: 'DOS 486', backgroundColor: '#0d0e11', autoHideMenuBar: true,
    webPreferences: { contextIsolation: true, sandbox: true },
  });
  // Enlaces externos (p. ej. fTelnet) se abren en el navegador del sistema
  win.webContents.setWindowOpenHandler(({ url }) => { if (/^https:\/\//.test(url)) shell.openExternal(url); return { action: 'deny' }; });
  // F11: pantalla completa
  win.webContents.on('before-input-event', (e, input) => {
    if (input.type === 'keyDown' && input.key === 'F11') { win.setFullScreen(!win.isFullScreen()); e.preventDefault(); }
  });
  // DOS486_PROXY permite usar otro puente WebSocket->telnet (pruebas o puente propio)
  const q = process.env.DOS486_PROXY ? '?proxy=' + encodeURIComponent(process.env.DOS486_PROXY) : '';
  win.loadURL(`http://127.0.0.1:${port}/index.html${q}`);
});
app.on('window-all-closed', () => app.quit());
