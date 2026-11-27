# Copihue BBS: BBS chileno por telnet (2323) y WebSocket + pagina web (7860)
# BASE permite usar un espejo si Docker Hub limita las descargas, p. ej.:
#   docker build --build-arg BASE=mirror.gcr.io/library/python:3.12-slim .
ARG BASE=python:3.12-slim
FROM ${BASE}
ENV PYTHONUNBUFFERED=1 TZ=America/Santiago COPIHUE_DATA=/home/bbs/data COPIHUE_PORT=2323 COPIHUE_WS_PORT=7860
# Hugging Face Spaces ejecuta el contenedor con el usuario 1000
RUN useradd --create-home --uid 1000 bbs && mkdir -p /home/bbs/data && chown -R bbs:bbs /home/bbs
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY server.py .
USER bbs
VOLUME ["/home/bbs/data"]
EXPOSE 2323 7860
CMD ["python", "server.py"]
