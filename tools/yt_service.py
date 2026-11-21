#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Servicio YouTube de SOMA (SOMA_250_YOUTUBE).

La aplicacion YouTube de SOMA no habla con YouTube directamente: no tiene decodificadores H.264/AV1/AAC ni puede resolver
las direcciones de video (YouTube las protege con desafios en JavaScript que cambian a menudo). Este servicio corre en un
equipo con yt-dlp y ffmpeg (el Mac o el PC de desarrollo) y hace ese trabajo:

    GET /yt/health                         -> "SOMAYT 1 ok ..."
    GET /yt/search?q=<texto>&n=12          -> lista de videos (texto, un video por linea, campos separados por tabulador)
    GET /yt/thumb?id=<id>                  -> miniatura JPEG de 168x94
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
import json
import re
import shutil
import socket
import struct
import subprocess
import sys
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


def linea_resultado(vid, dur, titulo, canal, vistas):
    return "%s\t%d\t%s\t%s\t%d" % (vid, max(0, int(dur or 0)), limpiar(titulo, 120) or "(sin titulo)", limpiar(canal, 48), max(0, int(vistas or 0)))


# ------------------------------------------------------------------------------------------------- busqueda
DEMO = [("DEMO0000001", 30, "Carta de ajuste de SOMA (30 s)", "SOMA demo", 1250),
        ("DEMO0000002", 95, "Barras y tono de 1 kHz (1 min 35 s)", "SOMA demo", 87),
        ("DEMO0000003", 600, "Prueba larga para saltos (10 min)", "SOMA demo", 3)]


def buscar(q, n):
    if _opts["demo"]:
        return [linea_resultado(*d) for d in DEMO]
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
        lineas.append(linea_resultado(vid, j.get("duration"), j.get("title"), j.get("channel") or j.get("uploader"), j.get("view_count")))
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
    with _lock:
        if len(_cache_formatos) > 64:
            _cache_formatos.clear()
        _cache_formatos[clave] = (time.time(), info)
    return info


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
            self._texto(200, "SOMAYT %s ok demo=%d pillow=%d yt-dlp=%d ffmpeg=%d\n" % (
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
            else:
                self._texto(404, "Ruta desconocida")
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
    if _arrancado["srv"] is not None or puerto_ocupado(port):
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
