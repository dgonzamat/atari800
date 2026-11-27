---
title: Copihue BBS
emoji: 🇨🇱
colorFrom: red
colorTo: blue
sdk: docker
app_port: 7860
pinned: false
---

# Copihue BBS

BBS chileno: foros, chat en vivo, quién está conectado y usuarios. Un solo servidor
en Python atiende **telnet** (puerto 2323) y **WebSocket** con subprotocolo `binary`,
el de fTelnet (puerto 7860). Ese puerto sirve también una página web con un terminal
fTelnet ya conectado al BBS.

## En tu PC o en un servidor con Docker

```bash
docker compose up -d --build
```

- Telnet: `telnet localhost 2323` (o desde DOS 486, BBS → **D**).
- Navegador: <http://localhost:8080>.
- Datos: volumen `copihue-data` (SQLite).
- `COPIHUE_SYSOP` en `docker-compose.yml`: alias con permiso para borrar cualquier mensaje.

## En un Space de Hugging Face (gratis, público)

Un Space con `sdk: docker` construye este `Dockerfile` y publica el puerto 7860 por
HTTPS. Solo hay WebSocket (no telnet directo). El disco del Space se borra al
reiniciarse, así que conviene el respaldo en un dataset privado: definir la variable
`COPIHUE_HF_REPO` (p. ej. `usuario/copihue-bbs-data`) y el secreto `HF_TOKEN` con
permiso de escritura. Al iniciar descarga `copihue.db` y sube cada cambio
(cada 60 s como máximo).
