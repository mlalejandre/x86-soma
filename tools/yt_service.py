#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Servicio YouTube de SOMA (SOMA_250_YOUTUBE).

La aplicacion YouTube de SOMA no habla con YouTube directamente: no tiene decodificadores H.264/AV1/AAC ni puede resolver
las direcciones de video (YouTube las protege con desafios en JavaScript que cambian a menudo). Este servicio corre en un
equipo con yt-dlp y ffmpeg (el Mac o el PC de desarrollo) y hace ese trabajo:

    GET /yt/health                         -> "SOMAYT 1 ok ..."
    GET /yt/search?q=<texto>&n=12          -> lista de videos (texto, un video por linea, campos separados por tabulador)
    GET /yt/thumb?id=<id>                  -> miniatura JPEG de 168x94
    GET /yt/info?id=<id>                   -> fecha, vistas, idiomas con subtitulos y la descripcion completa (SOMA_251)
    GET /yt/audio?id=<id>&max=<s>          -> el sonido en WAV de 16 kHz mono, para transcribirlo con whisper en SOMA (SOMA_251)
    GET /yt/subs?id=<id>&l=<idioma|auto>   -> los subtitulos de YouTube como texto con marcas de tiempo (SOMA_251)
    GET /yt/video?id=<id>&t=<s>&h=360&fps=15
                                           -> el video desde el segundo t como AVI (MJPG + PCM de 16 bits a 48 kHz estereo),
                                              emitido MIENTRAS se convierte: SOMA lo reproduce segun llega

El AVI lo monta este servicio (ffmpeg solo escribe una cabecera valida al terminar): cabecera fija con la duracion ya
puesta y, detras, los fotogramas y el audio tal como salen de ffmpeg. Es justo el formato del reproductor de SOMA
(src/core/avi.c + src/core/jpeg.c), el mismo de tools/mkavi.py.

Uso:
    python3 tools/yt_service.py                         # 127.0.0.1:8097 (QEMU lo ve en 10.0.2.2:8097)
    python3 tools/yt_service.py --bind 0.0.0.0 --clave SECRETO     # para un SOMA real en la red local
    python3 tools/yt_service.py --demo                  # sin YouTube ni yt-dlp: videos de prueba generados con ffmpeg
ejecutar.py lo arranca solo (ensure_service) si hay ffmpeg.

Requisitos: ffmpeg y yt-dlp en el PATH (macOS: brew install ffmpeg yt-dlp). Pillow es opcional (miniaturas mas pequenas).
"""
import argparse
import io
import glob
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

VERSION = "1"
DEFAULT_PORT = 8097
ID_RE = re.compile(r"^[A-Za-z0-9_-]{11}$")
THUMB_W, THUMB_H = 168, 94
ALTURAS = (240, 360, 480, 720)          # alturas de video admitidas (el reproductor de SOMA llega a 1280x720)
FPS_MIN, FPS_MAX = 5, 30
AUDIO_RATE = 48000                      # la que admite seguro el HD Audio de SOMA
AVI_MAX = 0xFFF00000                    # los desplazamientos del lector AVI de SOMA son de 32 bits
USER_AGENT = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"

try:
    from PIL import Image
    HAVE_PIL = True
except Exception:                       # pragma: no cover - depende del equipo
    Image = None
    HAVE_PIL = False

_opts = {"clave": "", "demo": False, "verbose": False, "ytdlp": "yt-dlp", "ffmpeg": "ffmpeg", "calidad": 6}
_lock = threading.Lock()
_cache_busqueda = {}                    # (q, n) -> (t, lineas)
_cache_mini = {}                        # id -> bytes
_cache_formatos = {}                    # (id, h) -> (t, info)
_cache_meta = {}                        # id -> (t, metadatos)                          (SOMA_251)
_cache_texto = {}                       # (id, idioma) -> (t, cabecera, texto)          (SOMA_251)
_cache_audio = {"clave": None, "wav": b""}      # el ultimo audio pedido (reintentos)   (SOMA_251)
_emision = {"proc": None, "turno": 0}   # un solo video a la vez: el nuevo corta al anterior


def log(msg):
    if _opts["verbose"]:
        sys.stderr.write("[youtube] %s\n" % msg)
        sys.stderr.flush()


class ErrorServicio(Exception):
    def __init__(self, estado, mensaje):
        Exception.__init__(self, mensaje)
        self.estado = estado
        self.mensaje = mensaje


# ----------------------------------------------------------------------------------------------------- texto
_SUST = {"‘": "'", "’": "'", "“": '"', "”": '"', "–": "-", "—": "-", "…": "...",
         " ": " ", "•": "-", "«": '"', "»": '"'}


def limpiar(s, maximo):
    """Texto de una linea para SOMA: sin tabuladores ni saltos, solo letras latinas (sus fuentes no tienen emoji ni CJK)."""
    out = []
    for ch in (s or ""):
        ch = _SUST.get(ch, ch)
        for c in ch:
            o = ord(c)
            if o < 32 or o == 127:
                c = " "
            elif o >= 0x250:
                continue
            out.append(c)
    return " ".join("".join(out).split())[:maximo]


def limpiar_parrafos(s, maximo):
    """Como limpiar(), pero conserva los saltos de linea (descripciones, subtitulos)."""
    out, total = [], 0
    for linea in (s or "").replace("\r", "").split("\n"):
        l = limpiar(linea, 2000)
        if not l and (not out or not out[-1]):
            continue                    # una sola linea en blanco seguida
        if total + len(l) + 1 > maximo:
            l = l[:max(0, maximo - total - 1)]
            out.append(l)
            break
        out.append(l)
        total += len(l) + 1
    while out and not out[-1]:
        out.pop()
    return "\n".join(out)


def linea_resultado(vid, dur, titulo, canal, vistas, desc=""):
    linea = "%s\t%d\t%s\t%s\t%d" % (vid, max(0, int(dur or 0)), limpiar(titulo, 120) or "(sin titulo)", limpiar(canal, 48), max(0, int(vistas or 0)))
    desc = limpiar(desc, 190)           # SOMA_251: sexta columna, solo si hay descripcion (un SOMA anterior la ignora)
    return linea + "\t" + desc if desc else linea


# ------------------------------------------------------------------------------------------------- busqueda
DEMO = [("DEMO0000001", 30, "Carta de ajuste de SOMA (30 s)", "SOMA demo", 1250),
        ("DEMO0000002", 95, "Barras y tono de 1 kHz (1 min 35 s)", "SOMA demo", 87),
        ("DEMO0000003", 600, "Prueba larga para saltos (10 min)", "SOMA demo", 3)]


DEMO_DESC = {
    "DEMO0000001": "Carta de ajuste con un tono de 1 kHz para probar el reproductor de SOMA.\n\nCapitulos:\n0:00 Barras de color\n0:15 Contador\n\nGenerado con ffmpeg (testsrc2).",
    "DEMO0000002": "Dos minutos de carta de ajuste para probar los saltos y la pausa.\nNo hay voz: solo un tono.",
    "DEMO0000003": "Diez minutos de prueba de resistencia del flujo de video.",
}
DEMO_SUBS = ("[0:00] Hola, esto es una prueba del servicio YouTube de SOMA. En este video se ve una carta de ajuste con barras de color.\n"
             "[0:10] El tono que suena es de mil hercios y sirve para comprobar que el sonido y la imagen van a la vez.\n"
             "[0:20] Al final aparece un contador. La receta secreta de la prueba lleva tres ingredientes: paciencia, cafe y un buen cable de red.\n")


def buscar(q, n):
    if _opts["demo"]:
        return [linea_resultado(*d, desc=DEMO_DESC.get(d[0], "").split("\n")[0]) for d in DEMO]
    clave = (q, n)
    with _lock:
        ent = _cache_busqueda.get(clave)
        if ent and time.time() - ent[0] < 300:
            return ent[1]
    cmd = [_opts["ytdlp"], "ytsearch%d:%s" % (n, q), "--flat-playlist", "--dump-json", "--no-warnings", "--ignore-errors"]
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=45)
    except FileNotFoundError:
        raise ErrorServicio(503, "Falta yt-dlp en el equipo del servicio (brew install yt-dlp)")
    except subprocess.TimeoutExpired:
        raise ErrorServicio(504, "YouTube no responde (la busqueda ha tardado demasiado)")
    lineas = []
    for raw in r.stdout.decode("utf-8", "replace").splitlines():
        try:
            j = json.loads(raw)
        except ValueError:
            continue
        vid = j.get("id") or ""
        if not ID_RE.match(vid):
            continue                    # canales y listas: solo videos
        lineas.append(linea_resultado(vid, j.get("duration"), j.get("title"), j.get("channel") or j.get("uploader"), j.get("view_count"),
                                      j.get("description") or ""))
    if not lineas and r.returncode != 0:
        raise ErrorServicio(502, "La busqueda ha fallado: " + limpiar(r.stderr.decode("utf-8", "replace")[-200:], 160))
    with _lock:
        if len(_cache_busqueda) > 64:
            _cache_busqueda.clear()
        _cache_busqueda[clave] = (time.time(), lineas)
    return lineas


# ----------------------------------------------------------------------------------------------- miniaturas
def _jpeg_soma(datos):
    """JPEG baseline 4:2:0 de 168x94 (lo que el decodificador de SOMA lee mas rapido). Sin Pillow se entrega tal cual."""
    if not HAVE_PIL:
        return datos
    im = Image.open(io.BytesIO(datos)).convert("RGB").resize((THUMB_W, THUMB_H), Image.LANCZOS)
    out = io.BytesIO()
    im.save(out, "JPEG", quality=82, progressive=False, optimize=False, subsampling=2)
    return out.getvalue()


def miniatura(vid):
    with _lock:
        if vid in _cache_mini:
            return _cache_mini[vid]
    if _opts["demo"]:
        n = int(vid[-1])
        r = subprocess.run([_opts["ffmpeg"], "-nostdin", "-loglevel", "error", "-f", "lavfi", "-i",
                            "testsrc2=size=%dx%d:rate=1" % (THUMB_W, THUMB_H), "-ss", str(n), "-frames:v", "1",
                            "-c:v", "mjpeg", "-q:v", "5", "-pix_fmt", "yuvj420p", "-f", "image2pipe", "pipe:1"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
        datos = r.stdout
        if not datos:
            raise ErrorServicio(502, "ffmpeg no ha generado la miniatura")
    else:
        datos = None
        for nombre in ("mqdefault.jpg", "hqdefault.jpg", "default.jpg"):
            try:
                req = urllib.request.Request("https://i.ytimg.com/vi/%s/%s" % (vid, nombre), headers={"User-Agent": USER_AGENT})
                with urllib.request.urlopen(req, timeout=10) as resp:
                    datos = resp.read(2 * 1024 * 1024)
                if datos:
                    break
            except Exception as e:
                log("miniatura %s/%s: %s" % (vid, nombre, e))
        if not datos:
            raise ErrorServicio(404, "Miniatura no disponible")
        try:
            datos = _jpeg_soma(datos)
        except Exception as e:
            log("miniatura sin convertir: %s" % e)
    with _lock:
        if len(_cache_mini) > 300:
            _cache_mini.clear()
        _cache_mini[vid] = datos
    return datos


# ------------------------------------------------------------------------------------- direcciones de video
def resolver(vid, altura):
    """-> {"entradas": [(url, cabeceras)], "duracion": s (0 = directo), "titulo": str}. Se recuerda 20 minutos."""
    clave = (vid, altura)
    with _lock:
        ent = _cache_formatos.get(clave)
        if ent and time.time() - ent[0] < 1200:
            return ent[1]
    # video y audio por separado hasta esa altura; se evitan los formatos por fragmentos DASH (ffmpeg no los abre desde su URL)
    nd = "[protocol!*=dash]"
    fmt = "bv*[height<=%d]%s+ba%s/b[height<=%d]%s/bv*[height<=%d]+ba/b[height<=%d]/bv*+ba/b" % (altura, nd, nd, altura, nd, altura, altura)
    cmd = [_opts["ytdlp"], "-J", "--no-playlist", "--no-warnings", "-f", fmt, "https://www.youtube.com/watch?v=" + vid]
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
    except FileNotFoundError:
        raise ErrorServicio(503, "Falta yt-dlp en el equipo del servicio (brew install yt-dlp)")
    except subprocess.TimeoutExpired:
        raise ErrorServicio(504, "YouTube no responde (no se pudo abrir el video)")
    if r.returncode != 0:
        err = r.stderr.decode("utf-8", "replace").strip().splitlines()
        raise ErrorServicio(502, "No se puede abrir el video: " + limpiar(err[-1] if err else "error de yt-dlp", 160))
    try:
        j = json.loads(r.stdout.decode("utf-8", "replace"))
    except ValueError:
        raise ErrorServicio(502, "Respuesta de yt-dlp no valida")
    partes = j.get("requested_formats") or [j]
    entradas = []
    for p in partes:
        if p.get("url"):
            entradas.append((p["url"], p.get("http_headers") or {}))
    if not entradas:
        raise ErrorServicio(502, "El video no tiene un formato que se pueda convertir")
    info = {"entradas": entradas[:2], "duracion": float(j.get("duration") or 0), "titulo": limpiar(j.get("title"), 120)}
    _guardar_meta(vid, j)               # SOMA_251: la descripcion y los subtitulos vienen en la misma respuesta
    with _lock:
        if len(_cache_formatos) > 64:
            _cache_formatos.clear()
        _cache_formatos[clave] = (time.time(), info)
    return info


# ------------------------------------------------------------- SOMA_251: descripcion, audio y subtitulos (para la IA de SOMA)
AUDIO_STT = 16000                       # whisper trabaja a 16 kHz mono
AUDIO_MAX_S = 7200


def _idioma_base(l):
    return (l or "").split("-")[0].lower()


def _guardar_meta(vid, j):
    fecha = str(j.get("upload_date") or "")
    if re.match(r"^\d{8}$", fecha):
        fecha = "%s-%s-%s" % (fecha[:4], fecha[4:6], fecha[6:])
    else:
        fecha = ""
    manual = [l for l in (j.get("subtitles") or {}) if l != "live_chat"]
    auto = list((j.get("automatic_captions") or {}).keys())
    meta = {"titulo": limpiar(j.get("title"), 120), "canal": limpiar(j.get("channel") or j.get("uploader"), 48),
            "duracion": int(float(j.get("duration") or 0)), "vistas": max(0, int(j.get("view_count") or 0)), "fecha": fecha,
            "idioma": _idioma_base(j.get("language")), "manual": manual, "auto": auto,
            "descripcion": limpiar_parrafos(j.get("description"), 6000)}
    with _lock:
        if len(_cache_meta) > 64:
            _cache_meta.clear()
        _cache_meta[vid] = (time.time(), meta)
    return meta


def metadatos(vid):
    if _opts["demo"]:
        d = [x for x in DEMO if x[0] == vid]
        if not d:
            raise ErrorServicio(404, "Ese video no existe en el modo demo")
        return {"titulo": d[0][2], "canal": d[0][3], "duracion": d[0][1], "vistas": d[0][4], "fecha": "2026-01-15", "idioma": "es",
                "manual": ["es"], "auto": [], "descripcion": DEMO_DESC.get(vid, "")}
    with _lock:
        ent = _cache_meta.get(vid)
        if ent and time.time() - ent[0] < 3600:
            return ent[1]
    cmd = [_opts["ytdlp"], "-J", "--no-playlist", "--no-warnings", "--skip-download", "https://www.youtube.com/watch?v=" + vid]
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
    except FileNotFoundError:
        raise ErrorServicio(503, "Falta yt-dlp en el equipo del servicio (brew install yt-dlp)")
    except subprocess.TimeoutExpired:
        raise ErrorServicio(504, "YouTube no responde (no se pudo leer la ficha del video)")
    try:
        j = json.loads(r.stdout.decode("utf-8", "replace"))
    except ValueError:
        err = r.stderr.decode("utf-8", "replace").strip().splitlines()
        raise ErrorServicio(502, "No se puede leer la ficha del video: " + limpiar(err[-1] if err else "error de yt-dlp", 160))
    return _guardar_meta(vid, j)


def idiomas_subs(meta):
    """Idiomas utiles para SOMA: los subtitulos hechos a mano y, de los automaticos, solo el idioma original del video."""
    out = []
    for l in meta["manual"]:
        if _idioma_base(l) not in out:
            out.append(_idioma_base(l))
    if meta["idioma"] and meta["idioma"] not in out and any(_idioma_base(a) == meta["idioma"] for a in meta["auto"]):
        out.append(meta["idioma"])
    return out[:8]


def texto_info(vid):
    m = metadatos(vid)
    return "SOMAYT %s info\ntitulo\t%s\ncanal\t%s\nduracion\t%d\nvistas\t%d\nfecha\t%s\nsubs\t%s\n\n%s\n" % (
        VERSION, m["titulo"], m["canal"], m["duracion"], m["vistas"], m["fecha"], ",".join(idiomas_subs(m)), m["descripcion"])


def elegir_pista(meta, idioma):
    """-> (codigo de la pista, es_automatica) o None. Orden: el idioma pedido, el del video, ingles, lo que haya a mano."""
    quiero = []
    for l in (None if idioma in ("", "auto") else _idioma_base(idioma), meta["idioma"] or None, "en"):
        if l and l not in quiero:
            quiero.append(l)

    def buscar_en(lista, l):
        exacto = [x for x in lista if x.lower() == l]
        if exacto:
            return exacto[0]
        orig = [x for x in lista if x.lower() == l + "-orig"]
        if orig:
            return orig[0]
        aprox = [x for x in lista if _idioma_base(x) == l]
        return aprox[0] if aprox else None
    for l in quiero:
        p = buscar_en(meta["manual"], l)
        if p:
            return p, False
        if l == meta["idioma"] or not meta["idioma"]:      # los automaticos de otros idiomas son traducciones de maquina
            p = buscar_en(meta["auto"], l)
            if p:
                return p, True
    if meta["manual"]:
        return meta["manual"][0], False
    for l in quiero:
        p = buscar_en(meta["auto"], l)
        if p:
            return p, True
    return None


def _marca(seg):
    seg = int(seg)
    return "%d:%02d:%02d" % (seg // 3600, seg // 60 % 60, seg % 60) if seg >= 3600 else "%d:%02d" % (seg // 60, seg % 60)


def _agrupar(trozos, cada=20.0):
    """[(segundo, texto)] -> lineas "[m:ss] texto", una cada "cada" segundos mas o menos."""
    lineas, ini, acc = [], None, []
    for t, txt in trozos:
        txt = limpiar(txt, 2000)
        if not txt:
            continue
        if ini is None:
            ini = t
        elif t - ini >= cada and acc:
            lineas.append("[%s] %s" % (_marca(ini), " ".join(acc)))
            ini, acc = t, []
        acc.append(txt)
    if acc:
        lineas.append("[%s] %s" % (_marca(ini or 0), " ".join(acc)))
    return "\n".join(lineas)


def texto_json3(datos):
    j = json.loads(datos.decode("utf-8", "replace"))
    trozos = []
    for ev in j.get("events") or []:
        segs = ev.get("segs")
        if not segs:
            continue
        txt = "".join(sg.get("utf8") or "" for sg in segs).replace("\n", " ").strip()
        if txt:
            trozos.append((float(ev.get("tStartMs") or 0) / 1000.0, txt))
    return _agrupar(trozos)


_VTT_T = re.compile(r"^(\d+):(\d\d):(\d\d)[.,]\d+\s+-->")
_VTT_T2 = re.compile(r"^(\d\d):(\d\d)[.,]\d+\s+-->")


def texto_vtt(datos):
    trozos, t, ultimo = [], 0.0, ""
    for l in datos.decode("utf-8", "replace").splitlines():
        l = l.strip()
        m = _VTT_T.match(l)
        m2 = None if m else _VTT_T2.match(l)
        if m:
            t = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
            continue
        if m2:
            t = int(m2.group(1)) * 60 + int(m2.group(2))
            continue
        if not l or l.startswith(("WEBVTT", "Kind:", "Language:", "NOTE")) or l.isdigit():
            continue
        l = re.sub(r"<[^>]+>", "", l).replace("&amp;", "&").replace("&nbsp;", " ").replace("&lt;", "<").replace("&gt;", ">").strip()
        if l and l != ultimo:           # los automaticos repiten cada linea mientras avanza
            trozos.append((t, l))
            ultimo = l
    return _agrupar(trozos)


def subtitulos(vid, idioma):
    """-> (codigo, automatica, texto). Los baja yt-dlp a una carpeta temporal."""
    if _opts["demo"]:
        if not [x for x in DEMO if x[0] == vid]:
            raise ErrorServicio(404, "Ese video no existe en el modo demo")
        return "es", False, DEMO_SUBS
    clave = (vid, idioma)
    with _lock:
        ent = _cache_texto.get(clave)
        if ent and time.time() - ent[0] < 3600:
            return ent[1]
    meta = metadatos(vid)
    pista = elegir_pista(meta, idioma)
    if not pista:
        raise ErrorServicio(404, "Este video no tiene subtitulos en YouTube: transcribelo con whisper")
    cod, auto = pista
    with tempfile.TemporaryDirectory(prefix="soma_yt_") as tmp:
        cmd = [_opts["ytdlp"], "--skip-download", "--no-playlist", "--no-warnings", "--write-auto-subs" if auto else "--write-subs",
               "--sub-langs", cod, "--sub-format", "json3/vtt/best", "-o", os.path.join(tmp, "s"),
               "https://www.youtube.com/watch?v=" + vid]
        try:
            r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
        except FileNotFoundError:
            raise ErrorServicio(503, "Falta yt-dlp en el equipo del servicio (brew install yt-dlp)")
        except subprocess.TimeoutExpired:
            raise ErrorServicio(504, "YouTube no responde (no se pudieron bajar los subtitulos)")
        texto = ""
        for f in sorted(glob.glob(os.path.join(tmp, "*"))):
            try:
                with open(f, "rb") as fh:
                    datos = fh.read(8 * 1024 * 1024)
                texto = texto_json3(datos) if f.endswith(".json3") else texto_vtt(datos) if f.endswith(".vtt") else ""
            except Exception as e:
                log("subtitulos %s: %r" % (f, e))
            if texto:
                break
        if not texto:
            err = r.stderr.decode("utf-8", "replace").strip().splitlines()
            raise ErrorServicio(502, "No se pudieron bajar los subtitulos: " + limpiar(err[-1] if err else "YouTube no los ha dado", 140))
    res = (_idioma_base(cod), auto, texto[:150000])
    with _lock:
        if len(_cache_texto) > 32:
            _cache_texto.clear()
        _cache_texto[clave] = (time.time(), res)
    return res


def cabecera_wav(nbytes, rate=AUDIO_STT, canales=1):
    return (b"RIFF" + struct.pack("<I", 36 + nbytes) + b"WAVE" + b"fmt " + struct.pack("<IHHIIHH", 16, 1, canales, rate, rate * canales * 2, canales * 2, 16) +
            b"data" + struct.pack("<I", nbytes))


def audio_wav(vid, max_s):
    """El sonido del video (los primeros max_s segundos) en WAV PCM de 16 bits, 16 kHz, mono. Se convierte entero antes de
    enviarlo para dar su tamano: SOMA lo guarda en su almacen y se lo pasa a whisper."""
    max_s = max(5, min(AUDIO_MAX_S, int(max_s)))
    clave = (vid, max_s, _opts["demo"])
    with _lock:
        if _cache_audio["clave"] == clave:
            return _cache_audio["wav"]
    sal = ["-vn", "-ac", "1", "-ar", str(AUDIO_STT), "-t", str(max_s), "-f", "s16le", "pipe:1"]
    base = [_opts["ffmpeg"], "-nostdin", "-loglevel", "error"]
    pcm = b""
    if _opts["demo"]:
        d = [x for x in DEMO if x[0] == vid]
        if not d:
            raise ErrorServicio(404, "Ese video no existe en el modo demo")
        r = subprocess.run(base + ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=16000:duration=%d" % min(max_s, d[0][1])] + sal,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        pcm = r.stdout
    else:
        info = resolver(vid, 240)
        url, cab = info["entradas"][-1]             # con video y audio separados, el audio es la segunda entrada
        cmd = list(base)
        ua = cab.get("User-Agent")
        if ua:
            cmd += ["-user_agent", ua]
        otras = "".join("%s: %s\r\n" % (k, v) for k, v in cab.items() if k.lower() != "user-agent" and "\r" not in v and "\n" not in v)
        if otras:
            cmd += ["-headers", otras]
        cmd += ["-reconnect", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "5", "-i", url] + sal
        try:
            r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=900)
            pcm = r.stdout
        except subprocess.TimeoutExpired:
            pcm = b""
        if len(pcm) < AUDIO_STT:                    # plan B: lo descarga yt-dlp y ffmpeg lo lee de la tuberia
            log("audio %s: ffmpeg no pudo con la direccion; se prueba con yt-dlp" % vid)
            try:
                dl = subprocess.Popen([_opts["ytdlp"], "-q", "--no-warnings", "--no-playlist", "-f", "ba/b", "-o", "-",
                                       "https://www.youtube.com/watch?v=" + vid], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            except FileNotFoundError:
                raise ErrorServicio(503, "Falta yt-dlp en el equipo del servicio (brew install yt-dlp)")
            try:
                r = subprocess.run(base + ["-i", "pipe:0"] + sal, stdin=dl.stdout, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=900)
                pcm = r.stdout
            except subprocess.TimeoutExpired:
                pcm = b""
            finally:
                try:
                    dl.kill()
                except OSError:
                    pass
                dl.wait()
    pcm = pcm[:min(len(pcm) & ~1, max_s * AUDIO_STT * 2)]
    if len(pcm) < AUDIO_STT // 2:
        raise ErrorServicio(502, "No se pudo extraer el sonido del video")
    wav = cabecera_wav(len(pcm)) + pcm
    with _lock:
        _cache_audio["clave"], _cache_audio["wav"] = clave, wav
    return wav


# ------------------------------------------------------------------------------------------------------ AVI
def _trozo(fcc, datos):
    return fcc + struct.pack("<I", len(datos)) + datos + (b"\0" if len(datos) & 1 else b"")


def _lista(tipo, carga):
    return b"LIST" + struct.pack("<I", len(carga) + 4) + tipo + carga


def cabecera_avi(ancho, alto, fps, fotogramas, rate, canales):
    """Cabecera AVI completa hasta el inicio de los datos de `movi`, con tamanos "abiertos" (el archivo aun no ha terminado).
    El lector de SOMA recorta el final de movi a lo que haya llegado."""
    ba = canales * 2
    usec = 1000000 // fps
    avih = struct.pack("<IIIIIIIIII4I", usec, 0, 0, 0x100, fotogramas, 0, 2 if canales else 1, 1 << 20, ancho, alto, 0, 0, 0, 0)
    strh_v = struct.pack("<4s4sIHHIIIIIIII4h", b"vids", b"MJPG", 0, 0, 0, 0, 1, fps, 0, fotogramas, 1 << 20, 0xFFFFFFFF, 0, 0, 0, ancho, alto)
    strf_v = struct.pack("<IiiHHI5I", 40, ancho, alto, 1, 24, 0x47504A4D, ancho * alto * 3, 0, 0, 0, 0)
    hdrl = _trozo(b"avih", avih) + _lista(b"strl", _trozo(b"strh", strh_v) + _trozo(b"strf", strf_v))
    if canales:
        strh_a = struct.pack("<4s4sIHHIIIIIIII4h", b"auds", b"\0\0\0\0", 0, 0, 0, 0, ba, rate * ba, 0, 0, 1 << 16, 0xFFFFFFFF, ba, 0, 0, 0, 0)
        strf_a = struct.pack("<HHIIHH", 1, canales, rate, rate * ba, ba, 16)
        hdrl += _lista(b"strl", _trozo(b"strh", strh_a) + _trozo(b"strf", strf_a))
    cuerpo = b"AVI " + _lista(b"hdrl", hdrl)
    return b"RIFF" + struct.pack("<I", 0xFFFFFFE0) + cuerpo + b"LIST" + struct.pack("<I", 0xFFFFFF00) + b"movi"


class FinFlujo(Exception):
    pass


def _leer(f, n):
    """n bytes exactos de la tuberia de ffmpeg (o FinFlujo)."""
    partes, falta = [], n
    while falta > 0:
        b = f.read(falta)
        if not b:
            raise FinFlujo()
        partes.append(b)
        falta -= len(b)
    return b"".join(partes) if len(partes) != 1 else partes[0]


def leer_cabecera_ffmpeg(f):
    """Lee el AVI que ffmpeg escribe por una tuberia hasta el inicio de `movi`. -> (ancho, alto, canales, rate)."""
    h = _leer(f, 12)
    if h[:4] != b"RIFF" or h[8:12] != b"AVI ":
        raise ErrorServicio(502, "ffmpeg no ha devuelto un AVI")
    ancho = alto = canales = rate = 0
    for _ in range(64):
        c = _leer(f, 8)
        fcc, tam = c[:4], struct.unpack("<I", c[4:])[0]
        if fcc == b"LIST":
            tipo = _leer(f, 4)
            if tipo == b"movi":
                if not ancho or not alto:
                    raise ErrorServicio(502, "El AVI de ffmpeg no tiene video")
                return ancho, alto, canales, rate
            if tam < 4 or tam > (1 << 20):
                raise ErrorServicio(502, "Cabecera AVI de ffmpeg no valida")
            cuerpo = _leer(f, tam - 4 + (tam & 1))
            if tipo == b"hdrl":
                p = 0
                while p + 8 <= len(cuerpo):
                    cf, ct = cuerpo[p:p + 4], struct.unpack("<I", cuerpo[p + 4:p + 8])[0]
                    if cf == b"LIST" and cuerpo[p + 8:p + 12] == b"strl":
                        q, fin, tipo_flujo = p + 12, p + 8 + ct, b""
                        while q + 8 <= fin:
                            sf, st = cuerpo[q:q + 4], struct.unpack("<I", cuerpo[q + 4:q + 8])[0]
                            d = cuerpo[q + 8:q + 8 + st]
                            if sf == b"strh" and len(d) >= 4:
                                tipo_flujo = d[:4]
                            elif sf == b"strf" and tipo_flujo == b"vids" and len(d) >= 12 and not ancho:
                                ancho, alto = struct.unpack("<ii", d[4:12])
                                alto = abs(alto)
                            elif sf == b"strf" and tipo_flujo == b"auds" and len(d) >= 8 and not canales:
                                canales, rate = struct.unpack("<HI", d[2:8])
                            q += 8 + st + (st & 1)
                    p += 8 + ct + (ct & 1)
        else:
            if tam > (1 << 20):
                raise ErrorServicio(502, "Cabecera AVI de ffmpeg no valida")
            _leer(f, tam + (tam & 1))
    raise ErrorServicio(502, "El AVI de ffmpeg no tiene datos")


def trozos_ffmpeg(f):
    """Recorre los datos de `movi` que escribe ffmpeg y entrega (clase, datos): clase 'v' (fotograma JPEG) o 'a' (PCM).
    Todo lo demas (indices, relleno, bloques RIFF de continuacion de los archivos de mas de 1 GiB) se descarta."""
    while True:
        try:
            c = _leer(f, 8)
        except FinFlujo:
            return
        fcc, tam = c[:4], struct.unpack("<I", c[4:])[0]
        if fcc in (b"LIST", b"RIFF"):
            try:
                _leer(f, 4)
            except FinFlujo:
                return
            continue
        if tam > 64 * 1024 * 1024:
            return                      # tamano absurdo: se ha perdido el paso
        try:
            datos = _leer(f, tam + (tam & 1))[:tam]
        except FinFlujo:
            return
        if fcc[2:] in (b"dc", b"db") and fcc[:2].isdigit():
            yield "v", datos
        elif fcc[2:] == b"wb" and fcc[:2].isdigit():
            yield "a", datos
        elif not all(32 <= b < 127 for b in fcc):
            return


def _salida_ffmpeg(altura, fps, mapa):
    return mapa + ["-vf", "fps=%d,scale=-2:%d:flags=bilinear" % (fps, altura),
                   "-c:v", "mjpeg", "-q:v", str(_opts["calidad"]), "-pix_fmt", "yuvj420p",
                   "-c:a", "pcm_s16le", "-ar", str(AUDIO_RATE), "-ac", "2", "-f", "avi", "pipe:1"]


def orden_ffmpeg_tuberia(t, altura, fps):
    """Plan B: el video llega por la entrada estandar (lo descarga yt-dlp). El salto se hace descartando lo anterior a t."""
    cmd = [_opts["ffmpeg"], "-nostdin", "-loglevel", "error", "-i", "pipe:0"]
    if t > 0:
        cmd += ["-ss", "%.3f" % t]
    return cmd + _salida_ffmpeg(altura, fps, ["-map", "0:v:0", "-map", "0:a:0?"])


def orden_ffmpeg(info, t, altura, fps):
    cmd = [_opts["ffmpeg"], "-nostdin", "-loglevel", "error"]
    if info is None:                    # modo demo
        cmd += ["-ss", "%.3f" % t, "-f", "lavfi", "-i", "testsrc2=size=854x480:rate=30",
                "-ss", "%.3f" % t, "-f", "lavfi", "-i", "sine=frequency=1000:sample_rate=48000"]
    else:
        for url, cab in info["entradas"]:
            if t > 0:
                cmd += ["-ss", "%.3f" % t]
            ua = cab.get("User-Agent")
            if ua:
                cmd += ["-user_agent", ua]
            otras = "".join("%s: %s\r\n" % (k, v) for k, v in cab.items() if k.lower() != "user-agent" and "\r" not in v and "\n" not in v)
            if otras:
                cmd += ["-headers", otras]
            cmd += ["-reconnect", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "5", "-i", url]
    dos = info is None or len(info["entradas"]) > 1
    return cmd + _salida_ffmpeg(altura, fps, ["-map", "0:v:0", "-map", "1:a:0?" if dos else "0:a:0?"])


def emitir(escribir, vid, t, altura, fps):
    """Convierte y emite el video. escribir(bytes) lanza una excepcion si SOMA corta. Devuelve los bytes emitidos."""
    if _opts["demo"]:
        info, dur = None, float(dict((d[0], d[1]) for d in DEMO).get(vid, 60))
    else:
        info = resolver(vid, altura)
        dur = info["duracion"]
    if dur and t >= dur:
        t = max(0.0, dur - 1.0)
    with _lock:
        _emision["turno"] += 1
        turno = _emision["turno"]
        viejo = _emision["proc"]
    if viejo is not None and viejo.poll() is None:
        viejo.kill()                    # SOMA ha pedido otro video (o un salto): el anterior ya no interesa
    procs = []                          # ffmpeg y, en el plan B, el yt-dlp que lo alimenta
    errores = []

    def lanzar(plan_b):
        """-> ffmpeg en marcha. Plan A: ffmpeg abre las direcciones de YouTube. Plan B: yt-dlp descarga y se lo pasa por una tuberia."""
        entrada = None
        if plan_b:
            alim = subprocess.Popen([_opts["ytdlp"], "-q", "--no-warnings", "--no-playlist", "-f", "b[height<=%d]/b" % altura, "-o", "-",
                                     "https://www.youtube.com/watch?v=" + vid], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            procs.append(alim)
            entrada = alim.stdout
            cmd = orden_ffmpeg_tuberia(t, altura, fps)
        else:
            cmd = orden_ffmpeg(info, t, altura, fps)
            if info is None:
                cmd[cmd.index("-map"):cmd.index("-map")] = ["-t", "%.3f" % max(1.0, dur - t)]
        log("ffmpeg: %s" % " ".join(c if len(c) < 60 else c[:57] + "..." for c in cmd))
        try:
            p = subprocess.Popen(cmd, stdin=entrada, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        except FileNotFoundError:
            raise ErrorServicio(503, "Falta ffmpeg en el equipo del servicio (brew install ffmpeg)")
        if entrada is not None:
            entrada.close()             # la tuberia queda entre yt-dlp y ffmpeg
        procs.append(p)
        with _lock:
            _emision["proc"] = p
        del errores[:]
        threading.Thread(target=lambda: errores.append(p.stderr.read()), daemon=True).start()
        return p

    def matar():
        for p in procs:
            if p.poll() is None:
                p.kill()

    proc = lanzar(False)
    total = 0
    try:
        try:
            ancho, alto, canales, rate = leer_cabecera_ffmpeg(proc.stdout)
        except FinFlujo:
            if turno != _emision["turno"]:
                return 0                # lo ha cortado una peticion mas nueva (un salto seguido de otro): no es un error
            proc.wait(timeout=5)
            msg = (errores[0] if errores else b"").decode("utf-8", "replace").strip().splitlines()
            motivo = limpiar(msg[-1] if msg else "sin datos", 160)
            if info is None:
                raise ErrorServicio(502, "ffmpeg no pudo abrir el video: " + motivo)
            # plan B: YouTube a veces rechaza a ffmpeg (403) aunque yt-dlp si puede descargar
            log("ffmpeg no abre las direcciones (%s): se prueba con yt-dlp por tuberia" % motivo)
            matar()
            proc = lanzar(True)
            try:
                ancho, alto, canales, rate = leer_cabecera_ffmpeg(proc.stdout)
            except FinFlujo:
                if turno != _emision["turno"]:
                    return 0
                raise ErrorServicio(502, "No se pudo abrir el video: " + motivo)
        if ancho * alto > 1280 * 720 or ancho > 4096 or alto > 4096:
            raise ErrorServicio(502, "Video demasiado grande para SOMA (%dx%d)" % (ancho, alto))
        fotogramas = int((dur - t) * fps + 0.5) if dur else 0
        cab = cabecera_avi(ancho, alto, fps, fotogramas, rate or AUDIO_RATE, canales)
        escribir(None)                  # ya se sabe que hay video: ahora se envia el 200
        escribir(cab)
        total = len(cab)
        for clase, datos in trozos_ffmpeg(proc.stdout):
            if turno != _emision["turno"]:
                break
            b = _trozo(b"00dc" if clase == "v" else b"01wb", datos)
            if total + len(b) > AVI_MAX:
                break
            escribir(b)
            total += len(b)
    finally:
        matar()
        try:
            proc.stdout.close()
        except Exception:
            pass
        with _lock:
            if _emision["proc"] in procs:
                _emision["proc"] = None
    return total


# ============================================================================================== servidor HTTP
class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server_version = "SomaYouTube/" + VERSION

    def log_message(self, fmt, *args):
        if _opts["verbose"]:
            sys.stderr.write("[youtube] %s\n" % (fmt % args))

    def _responder(self, estado, tipo, cuerpo):
        try:
            self.send_response(estado)
            self.send_header("Content-Type", tipo)
            self.send_header("Content-Length", str(len(cuerpo)))
            self.send_header("Connection", "close")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(cuerpo)
        except (BrokenPipeError, ConnectionResetError, socket.timeout):
            pass

    def _texto(self, estado, texto):
        self._responder(estado, "text/plain; charset=utf-8", texto.encode("utf-8"))

    def do_GET(self):
        u = urllib.parse.urlsplit(self.path)
        q = urllib.parse.parse_qs(u.query, keep_blank_values=True)

        def arg(nombre, defecto=""):
            return (q.get(nombre) or [defecto])[0]

        def entero(nombre, defecto, lo, hi):
            try:
                return max(lo, min(hi, int(float(arg(nombre, str(defecto))))))
            except ValueError:
                return defecto
        ruta = u.path
        if ruta in ("/", "/yt/health"):
            self._texto(200, "SOMAYT %s ok demo=%d pillow=%d yt-dlp=%d ffmpeg=%d ia=1\n" % (
                VERSION, 1 if _opts["demo"] else 0, 1 if HAVE_PIL else 0,
                1 if shutil.which(_opts["ytdlp"]) else 0, 1 if shutil.which(_opts["ffmpeg"]) else 0))
            return
        if _opts["clave"] and arg("k") != _opts["clave"]:
            self._texto(403, "Clave incorrecta: pon la misma en SOMA con la orden 'youtube clave <texto>'")
            return
        try:
            if ruta == "/yt/search":
                texto = arg("q").strip()[:160]
                if not texto:
                    raise ErrorServicio(400, "Falta el texto de la busqueda")
                lineas = buscar(texto, entero("n", 12, 1, 20))
                self._texto(200, "SOMAYT %s %d\n%s" % (VERSION, len(lineas), "".join(l + "\n" for l in lineas)))
            elif ruta == "/yt/thumb":
                vid = arg("id")
                if not ID_RE.match(vid):
                    raise ErrorServicio(400, "Identificador de video no valido")
                self._responder(200, "image/jpeg", miniatura(vid))
            elif ruta == "/yt/video":
                vid = arg("id")
                if not ID_RE.match(vid):
                    raise ErrorServicio(400, "Identificador de video no valido")
                altura = entero("h", 360, 144, 720)
                altura = min(ALTURAS, key=lambda a: abs(a - altura))
                self._video(vid, float(entero("t", 0, 0, 86400)), altura, entero("fps", 15, FPS_MIN, FPS_MAX))
            elif ruta in ("/yt/info", "/yt/audio", "/yt/subs"):          # SOMA_251
                vid = arg("id")
                if not ID_RE.match(vid):
                    raise ErrorServicio(400, "Identificador de video no valido")
                if ruta == "/yt/info":
                    self._texto(200, texto_info(vid))
                elif ruta == "/yt/audio":
                    self._responder(200, "audio/wav", audio_wav(vid, entero("max", 1200, 5, AUDIO_MAX_S)))
                else:
                    idioma = arg("l", "auto").strip().lower()[:8]
                    cod, auto, texto = subtitulos(vid, idioma if re.match(r"^[a-z]{2,3}$", idioma) else "auto")
                    self._texto(200, "SOMAYT %s subs %s %d\n%s\n" % (VERSION, cod, 1 if auto else 0, texto))
            else:
                self._texto(404, "Ruta desconocida (si es /yt/info, /yt/audio o /yt/subs: reinicia tools/yt_service.py, es una version antigua)")
        except ErrorServicio as e:
            log("error %d: %s" % (e.estado, e.mensaje))
            self._texto(e.estado, e.mensaje)
        except (BrokenPipeError, ConnectionResetError, socket.timeout):
            pass
        except Exception as e:           # un fallo aqui no debe tumbar el servicio
            log("fallo interno: %r" % (e,))
            self._texto(500, "Fallo interno del servicio: " + limpiar(str(e), 120))

    def _video(self, vid, t, altura, fps):
        enviado = [False]

        def escribir(b):
            if b is None:
                self.send_response(200)
                self.send_header("Content-Type", "video/x-msvideo")
                self.send_header("Connection", "close")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                enviado[0] = True
            else:
                self.wfile.write(b)
        self.connection.settimeout(120)   # SOMA en pausa con la memoria llena deja de leer: pasado este tiempo se corta
        try:
            total = emitir(escribir, vid, t, altura, fps)
            log("video %s desde %ds a %dp: %d KiB emitidos" % (vid, int(t), altura, total // 1024))
        except ErrorServicio:
            if enviado[0]:
                return                    # ya iba el 200: el AVI simplemente acaba aqui
            raise


class _Servidor(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def puerto_ocupado(port, host="127.0.0.1"):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(0.4)
    try:
        return s.connect_ex((host, port)) == 0
    finally:
        s.close()


_arrancado = {"srv": None}


def ensure_service(port=DEFAULT_PORT, quiet=False):
    """Arranca el servicio en un hilo de fondo si no hay nada escuchando en el puerto. Lo usa ejecutar.py."""
    if _arrancado["srv"] is not None:
        return True
    if puerto_ocupado(port):
        try:                            # SOMA_251: un servicio anterior que siga abierto no sabe de fichas, sonido ni subtitulos
            with urllib.request.urlopen("http://127.0.0.1:%d/yt/health" % port, timeout=3) as r:
                if b" ia=1" not in r.read(200) and not quiet:
                    print("[youtube] AVISO: en el puerto %d hay un servicio YouTube antiguo (sin la parte de IA). Cierralo y arranca "
                          "el nuevo:  python3 tools/yt_service.py" % port)
        except Exception:
            pass
        return True
    if not shutil.which(_opts["ffmpeg"]):
        if not quiet:
            print("[youtube] sin ffmpeg no hay servicio YouTube (brew install ffmpeg yt-dlp)")
        return False
    if not shutil.which(_opts["ytdlp"]):
        _opts["demo"] = True
    try:
        srv = _Servidor(("127.0.0.1", port), Handler)
    except OSError as e:
        print("[youtube] no se pudo abrir el puerto %d: %s" % (port, e))
        return False
    _arrancado["srv"] = srv
    threading.Thread(target=srv.serve_forever, name="soma-youtube", daemon=True).start()
    if not quiet:
        print("[youtube] servicio v%s en 127.0.0.1:%d%s" % (
            VERSION, port, "  -  MODO DEMO: falta yt-dlp (brew install yt-dlp)" if _opts["demo"] else ""))
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description="Servicio YouTube de SOMA (busqueda, miniaturas y video AVI en directo)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--bind", default="127.0.0.1", help="0.0.0.0 para atender a un SOMA real de la red local (usa --clave)")
    ap.add_argument("--clave", default="", help="clave compartida con SOMA (orden 'youtube clave <texto>')")
    ap.add_argument("--demo", action="store_true", help="videos de prueba generados con ffmpeg (sin YouTube ni yt-dlp)")
    ap.add_argument("--calidad", type=int, default=6, help="calidad JPEG de ffmpeg (2 mejor ... 12 mas ligero; por defecto 6)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)
    _opts.update(clave=args.clave, demo=args.demo, verbose=args.verbose, calidad=max(2, min(15, args.calidad)))
    if not shutil.which(_opts["ffmpeg"]):
        print("Falta ffmpeg (macOS: brew install ffmpeg)")
        return 2
    if not args.demo and not shutil.which(_opts["ytdlp"]):
        print("Falta yt-dlp (macOS: brew install yt-dlp). Para probar sin el: --demo")
        return 2
    if args.bind not in ("127.0.0.1", "localhost") and not args.clave:
        print("AVISO: el servicio queda abierto a toda la red sin clave. Recomendado: --clave <texto>")
    srv = _Servidor((args.bind, args.port), Handler)
    print("Servicio YouTube de SOMA v%s en %s:%d  (demo: %s, Pillow: %s)" % (
        VERSION, args.bind, args.port, "si" if args.demo else "no", "si" if HAVE_PIL else "no"))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
