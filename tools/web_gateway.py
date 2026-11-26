#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Pasarela web de SOMA (SOMA_PARCHE_028_UI, version 3).

El kernel de SOMA no implementa TLS. Para las paginas https:// el navegador de SOMA pide cada recurso a esta
pasarela, que corre en el equipo anfitrion (el invitado QEMU la ve en 10.0.2.2:8096):

    GET|POST /raw?url=<URL codificada>
    cabeceras: X-Soma-Method, X-Soma-Cookie, X-Soma-Referer, X-Soma-Accept

La respuesta es siempre "HTTP/1.1 200" con el estado real en X-Soma-Status, las cookies en X-Soma-Set-Cookie y las
redirecciones sin seguir (Location), para que el navegador lleve su propio historial y su tarro de cookies.

Ademas de transportar, adapta lo que el motor de SOMA no sabe leer:
  * imagenes WebP, AVIF, ICO, TIFF y JPEG CMYK -> PNG o JPEG; las fotos grandes se reducen (necesita Pillow)
  * imagenes SVG -> PNG (rasterizador propio sobre Pillow; usa cairosvg si esta instalado)
  * cuerpos comprimidos con brotli/zstd (si el modulo esta instalado; si no, no se le piden al servidor)
  * el texto viaja comprimido con gzip hacia SOMA (la red emulada es lenta)

Uso:
    python3 tools/web_gateway.py                 # 127.0.0.1:8096
    python3 tools/web_gateway.py --allow-private # permite destinos de redes privadas
ejecutar.py la arranca solo (ensure_gateway); no hace falta lanzarla a mano.
"""
import argparse
import gzip
import http.client
import io
import ipaddress
import os
import re
import socket
import ssl
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

VERSION = "6"                        # SOMA_342: conexiones reutilizadas con cada servidor; SOMA_343: keep-alive tambien con SOMA
DEFAULT_PORT = 8096
MAX_BODY = 24 * 1024 * 1024          # lo maximo que se lee de un servidor
MAX_TO_GUEST = 7 * 1024 * 1024       # el navegador acepta 8 MiB por recurso
IMG_MAX_SIDE = 1280                  # lado mayor de una imagen entregada a SOMA (menos bytes por la red emulada y menos memoria)
IMG_MAX_PIXELS = 1280 * 960
IMG_KEEP_BYTES = 160 * 1024          # PNG/GIF y JPEG baseline pequenos se entregan tal cual
PREFETCH_WORKERS = 12                # descargas paralelas de los recursos de una pagina (SOMA_342: 8 -> 12, ya sin un TLS por recurso)
PREFETCH_MAX = 160                   # SOMA_342: 90 -> 160 (las portadas de los periodicos tienen mas de 100 fotos)
POOL_PER_HOST = 6                    # SOMA_342: conexiones abiertas que se guardan por servidor
POOL_IDLE_S = 25                     # ... y cuanto tiempo (los servidores suelen cerrar a los 30-60 s)
TIMEOUT = 20

try:
    from PIL import Image, ImageChops, ImageDraw
    Image.MAX_IMAGE_PIXELS = 80 * 1024 * 1024
    HAVE_PIL = True
except Exception:                     # pragma: no cover - depende del equipo
    Image = ImageChops = ImageDraw = None
    HAVE_PIL = False
try:
    import brotli                     # type: ignore
except Exception:
    brotli = None
try:
    import zstandard                  # type: ignore
except Exception:
    zstandard = None
try:
    import cairosvg                   # type: ignore
except Exception:
    cairosvg = None

_opts = {"allow_private": False, "insecure": False, "verbose": False}
_stats = {"req": 0, "img_conv": 0, "svg": 0, "err": 0}
_lock = threading.Lock()


def log(msg):
    if _opts["verbose"]:
        sys.stderr.write("[pasarela] %s\n" % msg)
        sys.stderr.flush()


# ----------------------------------------------------------------------------------------------------------- TLS
def _ssl_context():
    if _opts["insecure"]:
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        return ctx
    ctx = ssl.create_default_context()
    try:
        if not ctx.get_ca_certs() and not os.environ.get("SSL_CERT_FILE"):
            # Python de python.org en macOS llega sin autoridades: se prueban certifi y el almacen del sistema
            try:
                import certifi         # type: ignore
                ctx.load_verify_locations(certifi.where())
            except Exception:
                for cand in ("/etc/ssl/cert.pem", "/etc/ssl/certs/ca-certificates.crt"):
                    if os.path.exists(cand):
                        ctx.load_verify_locations(cand)
                        break
    except Exception:
        pass
    return ctx


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


_opener = None


def _get_opener():
    global _opener
    if _opener is None:
        _opener = urllib.request.build_opener(_NoRedirect(), urllib.request.HTTPSHandler(context=_ssl_context()))
    return _opener


# ------------------------------------------------------------------------------------------------- seguridad
def _is_private_host(host):
    """True si el nombre apunta a esta maquina o a una red privada (la pagina no debe poder alcanzar el anfitrion)."""
    h = host.strip("[]").lower()
    if h in ("localhost", "") or h.endswith(".localhost") or h.endswith(".local") or h.endswith(".internal"):
        return True
    try:
        infos = socket.getaddrinfo(h, None)
    except Exception:
        return False                    # no resuelve aqui (p. ej. tras un proxy): el intento fallara por si solo
    for info in infos:
        try:
            ip = ipaddress.ip_address(info[4][0].split("%")[0])
        except ValueError:
            continue
        if ip.is_private or ip.is_loopback or ip.is_link_local or ip.is_multicast or ip.is_reserved or ip.is_unspecified:
            return True
    return False


# --------------------------------------------------------------------------------------------------- cuerpos
def _decode_body(data, encoding):
    enc = (encoding or "").lower()
    try:
        if "gzip" in enc:
            return zlib.decompress(data, 16 + zlib.MAX_WBITS)
        if "deflate" in enc:
            try:
                return zlib.decompress(data)
            except zlib.error:
                return zlib.decompress(data, -zlib.MAX_WBITS)
        if "br" in enc and brotli is not None:
            return brotli.decompress(data)
        if "zstd" in enc and zstandard is not None:
            return zstandard.ZstdDecompressor().decompressobj().decompress(data)
    except Exception as e:
        log("descompresion fallida (%s): %s" % (enc, e))
    return data


def sniff_image(data):
    d = data[:64]
    if d.startswith(b"\x89PNG\r\n\x1a\n"):
        return "png"
    if d[:2] == b"\xff\xd8":
        return "jpeg"
    if d[:4] == b"GIF8":
        return "gif"
    if d[:2] == b"BM":
        return "bmp"
    if d[:4] == b"RIFF" and d[8:12] == b"WEBP":
        return "webp"
    if d[4:8] == b"ftyp":
        return "avif"
    if d[:4] == b"\x00\x00\x01\x00":
        return "ico"
    if d[:4] in (b"II*\x00", b"MM\x00*"):
        return "tiff"
    head = data[:1024].lstrip(b"\xef\xbb\xbf \t\r\n").lower()
    if head.startswith(b"<svg") or (head.startswith(b"<?xml") and b"<svg" in data[:4096].lower()) or \
            (head.startswith(b"<!--") and b"<svg" in data[:4096].lower()) or (head.startswith(b"<!doctype svg")):
        return "svg"
    return None


def _png_bytes(im):
    out = io.BytesIO()
    im.save(out, "PNG", optimize=False, compress_level=6)
    return out.getvalue()


def _fit(im):
    w, h = im.size
    scale = 1.0
    if max(w, h) > IMG_MAX_SIDE:
        scale = IMG_MAX_SIDE / float(max(w, h))
    if w * h * scale * scale > IMG_MAX_PIXELS:
        scale = (IMG_MAX_PIXELS / float(w * h)) ** 0.5
    if scale < 1.0:
        im = im.resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.LANCZOS)
    return im


def convert_image(data, kind):
    """Devuelve (bytes, content_type) en un formato que SOMA decodifica, o None si no hay que tocarla / no se puede."""
    if kind == "svg":
        png = svg_to_png(data)
        if png:
            with _lock:
                _stats["svg"] += 1
            return png, "image/png"
        return None
    if not HAVE_PIL:
        return None
    try:
        im = Image.open(io.BytesIO(data))
        if kind == "ico":               # el tamano mayor del icono
            try:
                sizes = im.ico.sizes()
                im.size = max(sizes)
            except Exception:
                pass
        w, h = im.size
        small = len(data) <= IMG_KEEP_BYTES and max(w, h) <= IMG_MAX_SIDE and w * h <= IMG_MAX_PIXELS
        if kind in ("png", "gif", "bmp") and small:
            return None
        if kind == "jpeg" and small and im.mode in ("RGB", "L") and not im.info.get("progressive") and not im.info.get("progression"):
            return None                 # baseline: SOMA lo decodifica rapido. Progresivos, CMYK y grandes se recodifican
        try:
            im.seek(0)                  # animaciones: primer fotograma
        except Exception:
            pass
        has_alpha = im.mode in ("RGBA", "LA", "PA") or (im.mode == "P" and "transparency" in im.info)
        im = im.convert("RGBA" if has_alpha else "RGB")
        im = _fit(im)
        if has_alpha:
            lo, _hi = im.getchannel("A").getextrema()
            if lo == 255:
                has_alpha = False
                im = im.convert("RGB")
        out = io.BytesIO()
        if has_alpha or (kind in ("png", "gif", "ico") and im.size[0] * im.size[1] <= 512 * 512):
            im.save(out, "PNG", optimize=False, compress_level=6)
            ctype = "image/png"
        else:
            im.save(out, "JPEG", quality=86, progressive=False, optimize=False)
            ctype = "image/jpeg"
        with _lock:
            _stats["img_conv"] += 1
        return out.getvalue(), ctype
    except Exception as e:
        log("imagen no convertida (%s): %s" % (kind, e))
        return None


# ===================================================================================================== SVG -> PNG
# Rasterizador reducido: formas basicas, trazados (M L H V C S Q T A Z), transformaciones, relleno, trazo y opacidad.
# Sin degradados (se usa su primer color), filtros, texto ni CSS complejo: suficiente para logotipos e iconos.
_NUM = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")
_NAMED = {
    "black": (0, 0, 0), "white": (255, 255, 255), "red": (255, 0, 0), "green": (0, 128, 0), "blue": (0, 0, 255),
    "gray": (128, 128, 128), "grey": (128, 128, 128), "silver": (192, 192, 192), "yellow": (255, 255, 0),
    "orange": (255, 165, 0), "purple": (128, 0, 128), "navy": (0, 0, 128), "teal": (0, 128, 128),
    "lime": (0, 255, 0), "aqua": (0, 255, 255), "cyan": (0, 255, 255), "fuchsia": (255, 0, 255),
    "magenta": (255, 0, 255), "maroon": (128, 0, 0), "olive": (128, 128, 0), "pink": (255, 192, 203),
    "brown": (165, 42, 42), "gold": (255, 215, 0), "indigo": (75, 0, 130), "violet": (238, 130, 238),
    "darkgray": (169, 169, 169), "darkgrey": (169, 169, 169), "lightgray": (211, 211, 211),
    "lightgrey": (211, 211, 211), "dimgray": (105, 105, 105), "whitesmoke": (245, 245, 245),
    "crimson": (220, 20, 60), "tomato": (255, 99, 71), "coral": (255, 127, 80), "salmon": (250, 128, 114),
    "skyblue": (135, 206, 235), "steelblue": (70, 130, 180), "royalblue": (65, 105, 225),
    "dodgerblue": (30, 144, 255), "darkblue": (0, 0, 139), "darkgreen": (0, 100, 0), "darkred": (139, 0, 0),
    "seagreen": (46, 139, 87), "limegreen": (50, 205, 50), "forestgreen": (34, 139, 34),
    "slategray": (112, 128, 144), "slategrey": (112, 128, 144), "beige": (245, 245, 220), "tan": (210, 180, 140),
}


def _svg_color(v, current=(0, 0, 0), defs=None):
    """-> (r, g, b, a) o None (sin pintura)."""
    if v is None:
        return None
    v = v.strip()
    lv = v.lower()
    if lv in ("none", "transparent", ""):
        return None
    if lv == "currentcolor":
        return (current[0], current[1], current[2], 255)
    if lv.startswith("#"):
        hx = lv[1:]
        try:
            if len(hx) in (3, 4):
                vals = [int(c * 2, 16) for c in hx]
            elif len(hx) in (6, 8):
                vals = [int(hx[i:i + 2], 16) for i in range(0, len(hx), 2)]
            else:
                return None
        except ValueError:
            return None
        return (vals[0], vals[1], vals[2], vals[3] if len(vals) > 3 else 255)
    m = re.match(r"rgba?\(([^)]*)\)", lv)
    if m:
        parts = [p for p in re.split(r"[\s,/]+", m.group(1).strip()) if p]
        try:
            ch = []
            for p in parts[:3]:
                ch.append(int(round(float(p[:-1]) * 2.55)) if p.endswith("%") else int(round(float(p))))
            a = 255
            if len(parts) > 3:
                a = int(round(float(parts[3][:-1]) * 2.55)) if parts[3].endswith("%") else int(round(float(parts[3]) * 255))
            return (max(0, min(255, ch[0])), max(0, min(255, ch[1])), max(0, min(255, ch[2])), max(0, min(255, a)))
        except (ValueError, IndexError):
            return None
    m = re.match(r"url\(\s*['\"]?#([^)'\"\s]+)", v)
    if m:
        if defs is not None and m.group(1) in defs:
            return defs[m.group(1)]
        return (128, 128, 128, 255)
    if lv in _NAMED:
        c = _NAMED[lv]
        return (c[0], c[1], c[2], 255)
    return None


def _mat_mul(a, b):
    return (a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1],
            a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
            a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5])


def _parse_transform(s):
    import math
    m = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
    for name, args in re.findall(r"([a-zA-Z]+)\s*\(([^)]*)\)", s or ""):
        v = [float(x) for x in _NUM.findall(args)]
        t = None
        if name == "matrix" and len(v) == 6:
            t = tuple(v)
        elif name == "translate" and v:
            t = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0.0)
        elif name == "scale" and v:
            t = (v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0)
        elif name == "rotate" and v:
            a = math.radians(v[0])
            c, sn = math.cos(a), math.sin(a)
            t = (c, sn, -sn, c, 0, 0)
            if len(v) == 3:
                t = _mat_mul(_mat_mul((1, 0, 0, 1, v[1], v[2]), t), (1, 0, 0, 1, -v[1], -v[2]))
        elif name.lower() == "skewx" and v:
            t = (1, 0, math.tan(math.radians(v[0])), 1, 0, 0)
        elif name.lower() == "skewy" and v:
            t = (1, math.tan(math.radians(v[0])), 0, 1, 0, 0)
        if t:
            m = _mat_mul(m, t)
    return m


def _len(v, ref=100.0, default=0.0):
    if v is None:
        return default
    m = _NUM.match(v.strip())
    if not m:
        return default
    n = float(m.group(0))
    unit = v.strip()[m.end():].strip().lower()
    if unit == "%":
        return n * ref / 100.0
    return n * {"pt": 1.3333, "pc": 16.0, "mm": 3.7795, "cm": 37.795, "in": 96.0, "em": 16.0, "rem": 16.0, "ex": 8.0}.get(unit, 1.0)


def _cubic(p0, p1, p2, p3, out, n=14):
    for i in range(1, n + 1):
        t = i / float(n)
        u = 1 - t
        out.append((u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
                    u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1]))


def _quad(p0, p1, p2, out, n=10):
    for i in range(1, n + 1):
        t = i / float(n)
        u = 1 - t
        out.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0], u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))


def _arc(p0, rx, ry, phi, large, sweep, p1, out):
    import math
    if rx == 0 or ry == 0 or p0 == p1:
        out.append(p1)
        return
    rx, ry = abs(rx), abs(ry)
    ph = math.radians(phi)
    cp, sp = math.cos(ph), math.sin(ph)
    dx, dy = (p0[0] - p1[0]) / 2.0, (p0[1] - p1[1]) / 2.0
    x1, y1 = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry)
    if lam > 1:
        s = math.sqrt(lam)
        rx, ry = rx * s, ry * s
    num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
    den = rx * rx * y1 * y1 + ry * ry * x1 * x1
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if bool(large) == bool(sweep):
        co = -co
    cxp, cyp = co * rx * y1 / ry, -co * ry * x1 / rx
    cx = cp * cxp - sp * cyp + (p0[0] + p1[0]) / 2.0
    cy = sp * cxp + cp * cyp + (p0[1] + p1[1]) / 2.0

    def ang(ux, uy, vx, vy):
        d = math.hypot(ux, uy) * math.hypot(vx, vy)
        if d == 0:
            return 0.0
        a = math.acos(max(-1.0, min(1.0, (ux * vx + uy * vy) / d)))
        return -a if ux * vy - uy * vx < 0 else a
    t1 = ang(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry)
    dt = ang((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(4, int(abs(dt) / (math.pi / 16)) + 1)
    for i in range(1, n + 1):
        t = t1 + dt * i / float(n)
        ex, ey = rx * math.cos(t), ry * math.sin(t)
        out.append((cp * ex - sp * ey + cx, sp * ex + cp * ey + cy))


class _Scan(object):
    """Lector de datos de trazado: numeros, banderas de arco (que pueden venir pegadas, "01") y ordenes."""
    def __init__(self, d):
        self.d, self.i, self.n = d or "", 0, len(d or "")

    def skip(self):
        while self.i < self.n and self.d[self.i] in " \t\r\n,":
            self.i += 1

    def cmd(self):
        self.skip()
        if self.i < self.n and self.d[self.i].isalpha():
            self.i += 1
            return self.d[self.i - 1]
        return None

    def more(self):
        self.skip()
        return self.i < self.n and self.d[self.i] in "+-.0123456789"

    def num(self):
        self.skip()
        m = _NUM.match(self.d, self.i)
        if not m:
            raise ValueError("numero")
        self.i = m.end()
        return float(m.group(0))

    def flag(self):
        self.skip()
        if self.i >= self.n or self.d[self.i] not in "01":
            raise ValueError("bandera")
        self.i += 1
        return int(self.d[self.i - 1])


def _path_subpaths(d):
    """-> lista de (puntos, cerrado)."""
    sc = _Scan(d)
    subs, cur, pos, start = [], [], (0.0, 0.0), (0.0, 0.0)
    cmd, last_c, last_q = None, None, None
    try:
        while True:
            c = sc.cmd()
            if c is not None:
                cmd = c
                if cmd in "Zz":
                    if cur:
                        subs.append((cur, True))
                    cur, pos, last_c, last_q = [], start, None, None
                    continue
                if cmd.upper() not in "MLHVCSQTA":
                    break
            elif cmd is None or cmd in "Zz" or not sc.more():
                break
            rel = cmd.islower()
            c = cmd.upper()
            if c == "M":
                x, y = sc.num(), sc.num()
                if rel:
                    x, y = x + pos[0], y + pos[1]
                if cur:
                    subs.append((cur, False))
                pos = start = (x, y)
                cur = [pos]
                cmd = "l" if rel else "L"
                last_c = last_q = None
            elif c == "L":
                x, y = sc.num(), sc.num()
                pos = (x + pos[0], y + pos[1]) if rel else (x, y)
                cur = cur or [start]
                cur.append(pos)
                last_c = last_q = None
            elif c == "H":
                x = sc.num()
                pos = (x + pos[0] if rel else x, pos[1])
                cur = cur or [start]
                cur.append(pos)
                last_c = last_q = None
            elif c == "V":
                y = sc.num()
                pos = (pos[0], y + pos[1] if rel else y)
                cur = cur or [start]
                cur.append(pos)
                last_c = last_q = None
            elif c in "CS":
                if c == "C":
                    x1, y1 = sc.num(), sc.num()
                    if rel:
                        x1, y1 = x1 + pos[0], y1 + pos[1]
                else:
                    x1, y1 = (2 * pos[0] - last_c[0], 2 * pos[1] - last_c[1]) if last_c else pos
                x2, y2, x, y = sc.num(), sc.num(), sc.num(), sc.num()
                if rel:
                    x2, y2, x, y = x2 + pos[0], y2 + pos[1], x + pos[0], y + pos[1]
                cur = cur or [pos]
                _cubic(pos, (x1, y1), (x2, y2), (x, y), cur)
                last_c, last_q, pos = (x2, y2), None, (x, y)
            elif c in "QT":
                if c == "Q":
                    x1, y1 = sc.num(), sc.num()
                    if rel:
                        x1, y1 = x1 + pos[0], y1 + pos[1]
                else:
                    x1, y1 = (2 * pos[0] - last_q[0], 2 * pos[1] - last_q[1]) if last_q else pos
                x, y = sc.num(), sc.num()
                if rel:
                    x, y = x + pos[0], y + pos[1]
                cur = cur or [pos]
                _quad(pos, (x1, y1), (x, y), cur)
                last_q, last_c, pos = (x1, y1), None, (x, y)
            elif c == "A":
                rx, ry, phi = sc.num(), sc.num(), sc.num()
                large, sweep = sc.flag(), sc.flag()
                x, y = sc.num(), sc.num()
                if rel:
                    x, y = x + pos[0], y + pos[1]
                cur = cur or [pos]
                _arc(pos, rx, ry, phi, large, sweep, (x, y), cur)
                pos = (x, y)
                last_c = last_q = None
    except (IndexError, ValueError, TypeError):
        pass
    if cur:
        subs.append((cur, False))
    return subs


def _stroke_polys(points, closed, width):
    """Trazo como union de segmentos gruesos y juntas redondas: devuelve poligonos convexos."""
    import math
    polys = []
    hw = width / 2.0
    pts = list(points)
    if closed and len(pts) > 1:
        pts.append(pts[0])
    for a, b in zip(pts, pts[1:]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dy)
        if ln < 1e-9:
            continue
        nx, ny = -dy / ln * hw, dx / ln * hw
        polys.append([(a[0] + nx, a[1] + ny), (b[0] + nx, b[1] + ny), (b[0] - nx, b[1] - ny), (a[0] - nx, a[1] - ny)])
    if hw >= 0.75:
        for p in pts:
            polys.append([(p[0] + hw * math.cos(k * math.pi / 6), p[1] + hw * math.sin(k * math.pi / 6)) for k in range(12)])
    return polys


class _SvgRenderer(object):
    def __init__(self, root, max_side=512):
        self.root = root
        self.defs = {}
        self.ids = {}
        self.css = []
        w = _len(root.get("width"), default=0.0) if root.get("width") and "%" not in root.get("width") else 0.0
        h = _len(root.get("height"), default=0.0) if root.get("height") and "%" not in root.get("height") else 0.0
        vb = [float(x) for x in _NUM.findall(root.get("viewbox") or "")]
        if len(vb) == 4 and vb[2] > 0 and vb[3] > 0:
            self.vb = vb
            if not w and not h:
                w, h = vb[2], vb[3]
            elif not w:
                w = h * vb[2] / vb[3]
            elif not h:
                h = w * vb[3] / vb[2]
        else:
            if not w or not h:
                w, h = w or h or 300.0, h or w or 150.0
            self.vb = [0.0, 0.0, w, h]
        # los iconos pequenos se amplian para que no salgan borrosos al escalarlos la pagina
        scale = 1.0
        if max(w, h) < 64:
            scale = 64.0 / max(w, h)
        if max(w, h) * scale > max_side:
            scale = max_side / float(max(w, h))
        self.W, self.H = max(1, int(round(w * scale))), max(1, int(round(h * scale)))
        self.SS = 3 if self.W * self.H <= 700 * 700 else 2
        sx, sy = self.W * self.SS / self.vb[2], self.H * self.SS / self.vb[3]
        self.base = (sx, 0.0, 0.0, sy, -self.vb[0] * sx, -self.vb[1] * sy)
        self.img = Image.new("RGBA", (self.W * self.SS, self.H * self.SS), (0, 0, 0, 0))
        self.count = 0

    @staticmethod
    def _tag(el):
        t = el.tag
        return t.rsplit("}", 1)[-1] if isinstance(t, str) else ""

    def _collect(self, el):
        for e in el.iter():
            tag = self._tag(e)
            i = e.get("id")
            if i:
                self.ids[i] = e
            if tag in ("lineargradient", "radialgradient") and i:
                col = None
                for st in e.iter():
                    if self._tag(st) == "stop":
                        s = self._style(st, {})
                        col = _svg_color(s.get("stop-color", "#000"))
                        if col:
                            op = float(_NUM.findall(s.get("stop-opacity", "1") or "1")[0]) if _NUM.findall(s.get("stop-opacity", "1") or "1") else 1.0
                            col = (col[0], col[1], col[2], int(col[3] * max(0.0, min(1.0, op))))
                            break
                if col is None:
                    href = e.get("href") or ""
                    col = ("ref", href.lstrip("#"))
                self.defs[i] = col
            if tag == "style" and e.text:
                for sel, body in re.findall(r"([^{}]+)\{([^}]*)\}", re.sub(r"/\*.*?\*/", "", e.text, flags=re.S)):
                    decl = dict((k.strip().lower(), v.strip()) for k, v in (p.split(":", 1) for p in body.split(";") if ":" in p))
                    for s in sel.split(","):
                        self.css.append((s.strip(), decl))
        for k, v in list(self.defs.items()):
            if isinstance(v, tuple) and v and v[0] == "ref":
                t = self.defs.get(v[1])
                self.defs[k] = t if (t and t[0] != "ref") else (128, 128, 128, 255)

    def _style(self, el, inherited):
        st = dict(inherited)
        for k in ("opacity", "transform", "display"):     # no heredables
            st.pop(k, None)
        tag, cls, eid = self._tag(el), (el.get("class") or "").split(), el.get("id")
        for sel, decl in self.css:
            if sel == tag or (sel.startswith(".") and sel[1:] in cls) or (sel.startswith("#") and sel[1:] == eid) or sel == "*" \
                    or ("." in sel and not sel.startswith(".") and sel.split(".", 1)[0] == tag and sel.split(".", 1)[1] in cls):
                st.update(decl)
        for k in ("fill", "stroke", "stroke-width", "fill-opacity", "stroke-opacity", "opacity", "fill-rule", "color",
                  "display", "visibility", "stop-color", "stop-opacity", "stroke-linecap", "clip-rule"):
            v = el.get(k)
            if v is not None:
                st[k] = v
        for part in (el.get("style") or "").split(";"):
            if ":" in part:
                k, v = part.split(":", 1)
                st[k.strip().lower()] = v.strip()
        return st

    def _paint(self, subpaths, m, st, alpha):
        if not subpaths:
            return
        SSW, SSH = self.img.size
        current = _svg_color(st.get("color", "#000")) or (0, 0, 0, 255)

        def tr(p):
            return (m[0] * p[0] + m[2] * p[1] + m[4], m[1] * p[0] + m[3] * p[1] + m[5])
        fill = _svg_color(st.get("fill", "#000"), current, self.defs)
        if fill:
            mask = Image.new("1", (SSW, SSH), 0)
            many = sum(1 for pts, _c in subpaths if len(pts) >= 3) > 1
            for pts, _closed in subpaths:
                if len(pts) < 3:
                    continue
                poly = [tr(p) for p in pts]
                if many:                # huecos: paridad (even-odd) por XOR de subtrazados
                    layer = Image.new("1", (SSW, SSH), 0)
                    ImageDraw.Draw(layer).polygon(poly, fill=1)
                    mask = ImageChops.logical_xor(mask, layer)
                else:
                    ImageDraw.Draw(mask).polygon(poly, fill=1)
            a = fill[3] / 255.0 * alpha * self._op(st.get("fill-opacity"))
            self._blend(mask, fill, a)
        stroke = _svg_color(st.get("stroke"), current, self.defs)
        if stroke:
            import math
            sw = _len(st.get("stroke-width"), default=1.0) * math.sqrt(abs(m[0] * m[3] - m[1] * m[2]))
            if sw > 0.05:
                mask = Image.new("1", (SSW, SSH), 0)
                dr = ImageDraw.Draw(mask)
                for pts, closed in subpaths:
                    for poly in _stroke_polys([tr(p) for p in pts], closed, max(sw, 1.0)):
                        dr.polygon(poly, fill=1)
                a = stroke[3] / 255.0 * alpha * self._op(st.get("stroke-opacity"))
                self._blend(mask, stroke, a)
        self.count += 1

    @staticmethod
    def _op(v):
        if v is None:
            return 1.0
        f = _NUM.findall(v)
        if not f:
            return 1.0
        x = float(f[0])
        if v.strip().endswith("%"):
            x /= 100.0
        return max(0.0, min(1.0, x))

    def _blend(self, mask, color, a):
        if a <= 0:
            return
        layer = Image.new("RGBA", self.img.size, (color[0], color[1], color[2], 0))
        layer.putalpha(mask.convert("L").point(lambda v: int(v * a)))
        self.img = Image.alpha_composite(self.img, layer)

    def _shape(self, el, tag, vw, vh):
        g = lambda k, ref=vw, d=0.0: _len(el.get(k), ref, d)
        import math
        if tag == "path":
            return _path_subpaths(el.get("d"))
        if tag == "rect":
            x, y, w, h = g("x"), g("y", vh), g("width"), g("height", vh)
            if w <= 0 or h <= 0:
                return []
            rx = _len(el.get("rx"), vw, -1.0)
            ry = _len(el.get("ry"), vh, -1.0)
            if rx < 0 and ry < 0:
                return [([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], True)]
            rx = ry if rx < 0 else rx
            ry = rx if ry < 0 else ry
            rx, ry = min(rx, w / 2.0), min(ry, h / 2.0)
            pts = []
            for cx, cy, a0 in ((x + w - rx, y + ry, -90), (x + w - rx, y + h - ry, 0), (x + rx, y + h - ry, 90), (x + rx, y + ry, 180)):
                for k in range(0, 9):
                    a = math.radians(a0 + k * 11.25)
                    pts.append((cx + rx * math.cos(a), cy + ry * math.sin(a)))
            return [(pts, True)]
        if tag in ("circle", "ellipse"):
            cx, cy = g("cx"), g("cy", vh)
            rx = g("r") if tag == "circle" else g("rx")
            ry = g("r") if tag == "circle" else g("ry", vh)
            if rx <= 0 or ry <= 0:
                return []
            return [([(cx + rx * math.cos(k * math.pi / 24), cy + ry * math.sin(k * math.pi / 24)) for k in range(48)], True)]
        if tag == "line":
            return [([(g("x1"), g("y1", vh)), (g("x2"), g("y2", vh))], False)]
        if tag in ("polygon", "polyline"):
            v = [float(x) for x in _NUM.findall(el.get("points") or "")]
            pts = list(zip(v[0::2], v[1::2]))
            return [(pts, tag == "polygon")] if len(pts) >= 2 else []
        return []

    def _walk(self, el, m, st_in, alpha, depth=0):
        tag = self._tag(el)
        if depth > 24 or tag in ("defs", "style", "symbol", "clippath", "mask", "lineargradient", "radialgradient", "pattern",
                                 "title", "desc", "metadata", "filter", "marker", "script", "foreignobject", "text"):
            if not (tag == "symbol" and depth == 0):
                return
        st = self._style(el, st_in)
        if st.get("display") == "none" or st.get("visibility") == "hidden":
            return
        if el.get("transform"):
            m = _mat_mul(m, _parse_transform(el.get("transform")))
        alpha = alpha * self._op(st.get("opacity"))
        if tag == "use":
            ref = (el.get("href") or "").lstrip("#")
            tgt = self.ids.get(ref)
            if tgt is not None:
                m2 = _mat_mul(m, (1, 0, 0, 1, _len(el.get("x")), _len(el.get("y"))))
                if self._tag(tgt) == "symbol":
                    for ch in tgt:
                        self._walk(ch, m2, st, alpha, depth + 1)
                else:
                    self._walk(tgt, m2, st, alpha, depth + 1)
            return
        if tag in ("path", "rect", "circle", "ellipse", "line", "polygon", "polyline"):
            st2 = dict(st)
            if tag in ("line", "polyline") and "fill" not in st2:
                st2["fill"] = "none"
            self._paint(self._shape(el, tag, self.vb[2], self.vb[3]), m, st2, alpha)
            return
        for ch in el:
            self._walk(ch, m, st, alpha, depth + 1)

    def render(self, style=None):
        self._collect(self.root)
        self._walk(self.root, self.base, dict(style or {}), 1.0)
        if self.count == 0:
            return None
        return self.img.resize((self.W, self.H), Image.LANCZOS)


def _norm_tree(root):
    """Etiquetas y atributos en minusculas y sin espacio de nombres (el HTML de SOMA los entrega asi: viewbox, lineargradient)."""
    for el in root.iter():
        if isinstance(el.tag, str):
            el.tag = el.tag.rsplit("}", 1)[-1].lower()
        if el.attrib:
            items = [(k.rsplit("}", 1)[-1].lower(), v) for k, v in el.attrib.items()]
            el.attrib.clear()
            for k, v in items:
                if k == "xlink:href":
                    k = "href"
                el.attrib.setdefault(k, v)
    return root


def _lenient_tree(txt):
    """Arbol a partir de marcado que no es XML estricto (SVG serializado desde un documento HTML)."""
    import xml.etree.ElementTree as ET
    from html.parser import HTMLParser

    class P(HTMLParser):
        def __init__(self):
            HTMLParser.__init__(self, convert_charrefs=True)
            self.root = None
            self.stack = []

        def handle_starttag(self, tag, attrs):
            attrib = dict((k, v if v is not None else "") for k, v in attrs)
            if self.root is None:
                el = self.root = ET.Element(tag, attrib)
            elif self.stack:
                el = ET.SubElement(self.stack[-1], tag, attrib)
            else:
                return
            self.stack.append(el)

        def handle_startendtag(self, tag, attrs):
            self.handle_starttag(tag, attrs)
            if self.stack:
                self.stack.pop()

        def handle_endtag(self, tag):
            for i in range(len(self.stack) - 1, -1, -1):
                if self.stack[i].tag == tag:
                    del self.stack[i:]
                    break

        def handle_data(self, data):
            if self.stack and self.stack[-1].tag == "style":
                self.stack[-1].text = (self.stack[-1].text or "") + data
    p = P()
    p.feed(txt)
    p.close()
    return p.root


def svg_to_png(data, max_side=512, color=None, inline=False):
    """SVG -> PNG. color = "rrggbb" para currentColor; inline = marcado sacado de una pagina (relleno por defecto = color del texto)."""
    if cairosvg is not None and not inline:
        try:
            return cairosvg.svg2png(bytestring=data, output_width=None)
        except Exception as e:
            log("cairosvg: %s" % e)
    if not HAVE_PIL:
        return None
    try:
        import xml.etree.ElementTree as ET
        txt = data.decode("utf-8", "replace")
        txt = re.sub(r"<!DOCTYPE[^>\[]*(\[[^\]]*\])?[^>]*>", "", txt, flags=re.I)     # sin entidades externas
        txt = re.sub(r"<!ENTITY[^>]*>", "", txt, flags=re.I)
        root = None
        if not inline:
            try:
                root = ET.fromstring(txt)
            except ET.ParseError:
                root = None
        if root is None:
            root = _lenient_tree(txt)
        if root is None:
            return None
        root = _norm_tree(root)
        if root.tag != "svg":
            return None
        style = {}
        if color and re.match(r"^[0-9a-fA-F]{6}$", color):
            style["color"] = "#" + color
        if inline:
            style["fill"] = "currentColor"      # en las paginas, los iconos sin relleno propio heredan casi siempre el color del texto
        im = _SvgRenderer(root, max_side).render(style)
        return _png_bytes(im) if im is not None else None
    except Exception as e:
        log("SVG no convertido: %s" % e)
        return None


# ============================================================================================== servidor HTTP
_TEXT_TYPES = ("text/", "application/javascript", "application/json", "application/xml", "application/xhtml",
               "application/x-javascript", "application/ecmascript", "application/rss", "application/atom", "image/svg")
_cache = {}
_cache_bytes = [0]
CACHE_MAX = 64 * 1024 * 1024
_inflight = {}                          # url -> threading.Event (descarga en curso, iniciada por la precarga)
_STATIC_TYPES = ("image/", "text/css", "application/javascript", "text/javascript", "application/x-javascript", "font/",
                 "application/font", "application/json")


def _akind(accept):
    a = (accept or "").lower()
    return "img" if a.startswith("image/") else "any"


def _cache_get(key):
    with _lock:
        ent = _cache.get(key)
        if ent and time.time() - ent[0] < 300:
            return ent[1]
    return None


def _cache_put(key, value):
    size = len(value[3])
    if size > 6 * 1024 * 1024:
        return
    with _lock:
        if _cache_bytes[0] + size > CACHE_MAX:
            _cache.clear()
            _cache_bytes[0] = 0
        _cache[key] = (time.time(), value)
        _cache_bytes[0] += size


def _is_static(ctype):
    c = (ctype or "").lower()
    return any(c.startswith(t) for t in _STATIC_TYPES)


# ------------------------------------------------------------------------------- conexiones reutilizadas (SOMA_342)
# urllib abre una conexion (TCP + TLS: media docena de idas y vueltas) por cada recurso. Una portada con 100 fotos del mismo servidor
# pagaba 100 apretones TLS. Ahora las conexiones con cada servidor se guardan abiertas y se reutilizan (HTTP/1.1 keep-alive).
# Si hay un proxy configurado en el entorno se sigue usando urllib, que lo respeta.
_pool = {}
_pool_lock = threading.Lock()
_ctx = [None]
_stats_pool = {"nuevas": 0, "reusadas": 0}


def _ssl_ctx():
    if _ctx[0] is None:
        _ctx[0] = _ssl_context()
    return _ctx[0]


def _conn_get(scheme, host, port):
    key = (scheme, host, port)
    now = time.time()
    with _pool_lock:
        lst = _pool.get(key) or []
        while lst:
            c, t = lst.pop()
            if now - t < POOL_IDLE_S:
                _stats_pool["reusadas"] += 1
                return c, True
            try:
                c.close()
            except Exception:
                pass
        _stats_pool["nuevas"] += 1
    if scheme == "https":
        return http.client.HTTPSConnection(host, port, timeout=TIMEOUT, context=_ssl_ctx()), False
    return http.client.HTTPConnection(host, port, timeout=TIMEOUT), False


def _conn_put(scheme, host, port, c):
    with _pool_lock:
        lst = _pool.setdefault((scheme, host, port), [])
        if len(lst) < POOL_PER_HOST:
            lst.append((c, time.time()))
            return
    c.close()


def _fetch_pool(url, method, body, headers):
    sp = urllib.parse.urlsplit(url)
    scheme, host = sp.scheme.lower(), sp.hostname
    if scheme not in ("http", "https") or not host:
        raise ValueError("URL no valida")
    port = sp.port or (443 if scheme == "https" else 80)
    path = (sp.path or "/") + ("?" + sp.query if sp.query else "")
    hdrs = {k: v for k, v in (headers or {}).items() if v}
    enc = "gzip, deflate"
    if brotli is not None:
        enc += ", br"
    hdrs["Accept-Encoding"] = enc
    post = method == "POST"
    for intento in (0, 1):
        c, reusada = _conn_get(scheme, host, port) if not post else (None, False)
        if c is None:                    # un POST nunca va por una conexion reutilizada: no se podria repetir sin riesgo
            c = http.client.HTTPSConnection(host, port, timeout=TIMEOUT, context=_ssl_ctx()) if scheme == "https" else \
                http.client.HTTPConnection(host, port, timeout=TIMEOUT)
        try:
            c.request(method, path, body=body if post else None, headers=hdrs)
            resp = c.getresponse()
            data = resp.read(MAX_BODY + 1)
        except (http.client.RemoteDisconnected, http.client.BadStatusLine, ConnectionError, ssl.SSLEOFError):
            c.close()
            if reusada and intento == 0:  # el servidor cerro la conexion guardada: otra vez con una nueva
                continue
            raise
        except Exception:
            c.close()
            raise
        if post or len(data) > MAX_BODY or resp.will_close or not resp.isclosed():
            c.close()
        else:
            _conn_put(scheme, host, port, c)
        return resp.status, resp.headers, data
    raise ConnectionError("no se pudo repetir la peticion")


def _usa_proxy(url):
    """True si urllib mandaria esta URL por un proxy del entorno (entonces se usa urllib, que lo sabe hacer)."""
    sp = urllib.parse.urlsplit(url)
    px = urllib.request.getproxies()
    if not (px.get(sp.scheme.lower()) or px.get("all")):
        return False
    try:
        return not urllib.request.proxy_bypass(sp.hostname or "")
    except Exception:
        return True


def fetch(url, method="GET", body=None, headers=None):
    """-> (status, headers_list, final_ctype, body_bytes, set_cookies, location)."""
    if not _usa_proxy(url):
        status, hdrs, data = _fetch_pool(url, method, body, headers)
        if len(data) > MAX_BODY:
            data = data[:MAX_BODY]
        data = _decode_body(data, hdrs.get("Content-Encoding"))
        cookies = hdrs.get_all("Set-Cookie") or []
        return status, hdrs, hdrs.get("Content-Type") or "", data, cookies, hdrs.get("Location")
    req = urllib.request.Request(url, data=body if method == "POST" else None, method=method)
    for k, v in (headers or {}).items():
        if v:
            req.add_header(k, v)
    enc = "gzip, deflate"
    if brotli is not None:
        enc += ", br"
    req.add_header("Accept-Encoding", enc)
    try:
        resp = _get_opener().open(req, timeout=TIMEOUT)
    except urllib.error.HTTPError as e:
        resp = e
    status = getattr(resp, "status", None) or resp.getcode()
    hdrs = resp.headers
    data = resp.read(MAX_BODY + 1)
    if len(data) > MAX_BODY:
        data = data[:MAX_BODY]
    data = _decode_body(data, hdrs.get("Content-Encoding"))
    cookies = hdrs.get_all("Set-Cookie") or []
    return status, hdrs, hdrs.get("Content-Type") or "", data, cookies, hdrs.get("Location")


def get_resource(url, method, body, h, want_image):
    """Descarga y adapta un recurso. -> (status, ctype, extra_headers(tuple), data). Lanza excepciones de red."""
    status, hdrs, ctype, data, cookies, location = fetch(url, method, body, h)
    extra = []
    if location:
        extra.append(("Location", urllib.parse.urljoin(url, location)))
    for c in cookies:
        extra.append(("X-Soma-Set-Cookie", c.replace("\r", " ").replace("\n", " ")))
    low = ctype.lower()
    kind = None
    if 200 <= status < 300 and data and (low.startswith("image/") or want_image or low.startswith("application/octet-stream") or not low):
        kind = sniff_image(data)
    if kind:
        conv = convert_image(data, kind)
        if conv:
            data, ctype = conv
        elif kind in ("png", "jpeg", "gif", "bmp"):
            ctype = "image/" + kind
    if len(data) > MAX_TO_GUEST:
        data = data[:MAX_TO_GUEST]
    return status, ctype, tuple(extra), data, bool(cookies)


# ------------------------------------------------------------------------------------------- precarga paralela
_RES_RE = re.compile(r"""<(img|source|link|script|input)\b([^>]*)>""", re.I)
_ATTR_RE = re.compile(r"""([a-zA-Z_:-]+)\s*=\s*("([^"]*)"|'([^']*)'|([^\s>]+))""")
_pool_sem = threading.BoundedSemaphore(PREFETCH_WORKERS)


def _attrs(s):
    out = {}
    for m in _ATTR_RE.finditer(s):
        out[m.group(1).lower()] = m.group(3) if m.group(3) is not None else (m.group(4) if m.group(4) is not None else m.group(5))
    return out


def _srcset_best(ss):
    best, score = None, -1
    for part in ss.split(","):
        bits = part.strip().split()
        if not bits:
            continue
        sc = 100
        if len(bits) > 1:
            d = bits[1].lower()
            try:
                sc = int(float(d[:-1])) if d.endswith("w") else int(float(d[:-1]) * 600) if d.endswith("x") else 100
            except ValueError:
                sc = 100
            if d.endswith("w") and sc > 1400:
                sc = 1400 - (sc - 1400) // 4
        if sc > score:
            best, score = bits[0], sc
    return best


def page_resources(html, base_url):
    """URLs de imagenes, hojas de estilo y scripts de una pagina, en el orden en que SOMA las pedira."""
    m = re.search(r"<base\s[^>]*href\s*=\s*[\"']([^\"']+)", html, re.I)
    base = urllib.parse.urljoin(base_url, m.group(1)) if m else base_url
    css, js, img = [], [], []
    for mm in _RES_RE.finditer(html):
        tag, at = mm.group(1).lower(), _attrs(mm.group(2))
        if tag == "link":
            rel = (at.get("rel") or "").lower()
            if "stylesheet" in rel and at.get("href"):
                css.append(at["href"])
            elif ("icon" in rel or "preload" in rel and (at.get("as") or "") == "image") and at.get("href"):
                img.append(at["href"])
        elif tag == "script":
            if at.get("src") and "module" not in (at.get("type") or ""):
                js.append(at["src"])
        elif tag in ("img", "source", "input"):
            cand = None
            for k in ("data-src", "data-lazy-src", "data-original", "data-lazy", "data-url"):
                if at.get(k) and not at[k].startswith("data:"):
                    cand = at[k]
                    break
            if not cand:
                for k in ("data-srcset", "srcset"):
                    if at.get(k):
                        cand = _srcset_best(at[k])
                        if cand:
                            break
            if not cand and at.get("src") and not at["src"].startswith("data:"):
                cand = at["src"]
            if cand:
                img.append(cand)
    out, seen = [], set()
    for kind, lst in (("any", css), ("any", js), ("img", img)):
        for u in lst:
            u = u.strip().replace("&amp;", "&")
            if not u or u.startswith(("data:", "javascript:", "#", "about:")):
                continue
            full = urllib.parse.urljoin(base, u).split("#", 1)[0]
            if not full.startswith(("http://", "https://")) or (full, kind) in seen:
                continue
            seen.add((full, kind))
            out.append((full, kind))
            if len(out) >= PREFETCH_MAX:
                return out
    return out


def _prefetch_one(url, kind, referer, ua, lang):
    key = (url, kind)
    try:
        if _cache_get(key):
            return
        host = urllib.parse.urlsplit(url).hostname or ""
        if not _opts["allow_private"] and _is_private_host(host):
            return
        h = {"User-Agent": ua, "Referer": referer, "Accept-Language": lang,
             "Accept": ("image/png,image/jpeg,image/gif,image/webp,image/svg+xml,image/*;q=0.8,*/*;q=0.5" if (kind == "img" and HAVE_PIL)
                        else "image/png,image/jpeg,image/gif" if kind == "img" else "*/*")}
        status, ctype, extra, data, has_cookie = get_resource(url, "GET", None, h, kind == "img")
        if 200 <= status < 300 and _is_static(ctype):
            _cache_put(key, (status, ctype, extra, data))
    except Exception as e:
        log("precarga fallida %s: %s" % (url[:80], e))
    finally:
        with _lock:
            ev = _inflight.pop(key, None)
        if ev:
            ev.set()
        _pool_sem.release()


def start_prefetch(html_bytes, page_url, ua, lang):
    try:
        html = html_bytes.decode("utf-8", "replace")
    except Exception:
        return
    res = page_resources(html, page_url)
    if not res:
        return

    def run():
        for url, kind in res:
            key = (url, kind)
            with _lock:
                if key in _inflight or (key in _cache and time.time() - _cache[key][0] < 300):
                    continue
                _inflight[key] = threading.Event()
            _pool_sem.acquire()
            threading.Thread(target=_prefetch_one, args=(url, kind, page_url, ua, lang), daemon=True).start()
    threading.Thread(target=run, name="soma-prefetch", daemon=True).start()
    log("precarga: %d recursos de %s" % (len(res), page_url[:80]))


class Handler(BaseHTTPRequestHandler):
    # SOMA_343_DESCARGAS: HTTP/1.1 con keep-alive: SOMA reutiliza la conexion con la pasarela (antes, una por recurso). Toda respuesta
    # lleva Content-Length (_reply); si algo falla sin responder, o queda un cuerpo sin leer, la conexion se cierra.
    protocol_version = "HTTP/1.1"
    disable_nagle_algorithm = True
    server_version = "SomaGateway/" + VERSION

    def log_message(self, fmt, *args):
        if _opts["verbose"]:
            sys.stderr.write("[pasarela] %s\n" % (fmt % args))

    def _reply(self, status, ctype, body, extra=None, compress=True):
        out_headers = [("X-Soma-Status", str(status)), ("X-Soma-Gateway", self._caps()), ("Content-Type", ctype or "application/octet-stream")]
        if compress and len(body) > 600 and "gzip" in (self.headers.get("Accept-Encoding") or "") and \
                any((ctype or "").lower().startswith(t) for t in _TEXT_TYPES):
            body = gzip.compress(body, 5)
            out_headers.append(("Content-Encoding", "gzip"))
        out_headers += extra or []
        out_headers += [("Content-Length", str(len(body))), ("Cache-Control", "no-store")]
        if self.close_connection:
            out_headers.append(("Connection", "close"))
        self._replied = True
        try:
            self.send_response(200)
            for k, v in out_headers:
                self.send_header(k, v)
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    @staticmethod
    def _caps():
        caps = ["v" + VERSION]
        if HAVE_PIL:
            caps += ["webp", "avif", "svg", "ico"]
        elif cairosvg is not None:
            caps += ["svg"]
        return " ".join(caps)

    def _error(self, status, title, detail, url=""):
        with _lock:
            _stats["err"] += 1
        esc = lambda s: (s or "").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        html = ("<!doctype html><html><head><meta charset='utf-8'><title>%s</title><style>body{font-family:sans-serif;"
                "margin:48px;color:#1f2937}h1{color:#b91c1c;font-size:24px}code{background:#f1f5f9;padding:2px 6px;"
                "border-radius:4px}p{line-height:1.5}</style></head><body><h1>%s</h1><p>%s</p><p><code>%s</code></p>"
                "<p style='color:#64748b'>Pasarela web de SOMA v%s</p></body></html>") % (esc(title), esc(title), detail, esc(url), VERSION)
        self._reply(status, "text/html; charset=utf-8", html.encode("utf-8"))

    def do_GET(self):
        self._seguro()

    def do_POST(self):
        self._seguro()

    def do_HEAD(self):
        self._seguro()

    def _seguro(self):
        self._replied = False
        try:
            self._handle()
        except Exception as e:                  # nunca dejar a SOMA esperando una respuesta que no llega
            if not self._replied:
                self.close_connection = True
                try:
                    self._error(500, "Error de la pasarela", "Fallo interno: %s" % str(e)[:160])
                except Exception:
                    pass
        finally:
            if not self._replied:
                self.close_connection = True

    def _handle(self):
        with _lock:
            _stats["req"] += 1
        parsed = urllib.parse.urlsplit(self.path)
        if parsed.path not in ("/raw", "/svg") and (self.headers.get("Content-Length") or "0").strip() not in ("", "0"):
            self.close_connection = True        # SOMA_343: esas rutas no leen el cuerpo
        if parsed.path in ("/", "/health", "/status"):
            info = "SOMA web gateway v%s\npillow=%s brotli=%s cairosvg=%s\n%s\nconexiones: %s\n" % (
                VERSION, HAVE_PIL, brotli is not None, cairosvg is not None, _stats, _stats_pool)
            self._reply(200, "text/plain; charset=utf-8", info.encode(), compress=False)
            return
        if parsed.path == "/svg":
            self._svg(parsed)
            return
        if parsed.path != "/raw":
            self._error(404, "Ruta desconocida", "La pasarela solo atiende <code>/raw?url=...</code>.")
            return
        q = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
        url = (q.get("url") or [""])[0].strip()
        if not re.match(r"^https?://", url, re.I):
            self._error(400, "Dirección no válida", "Solo se admiten direcciones http:// y https://.", url)
            return
        url = url.split("#", 1)[0]
        host = urllib.parse.urlsplit(url).hostname or ""
        if not _opts["allow_private"] and _is_private_host(host):
            self._error(403, "Destino bloqueado", "La pasarela no accede a esta máquina ni a redes privadas "
                        "(arráncala con <code>--allow-private</code> para permitirlo).", url)
            return
        method = (self.headers.get("X-Soma-Method") or self.command or "GET").upper()
        if method not in ("GET", "POST", "HEAD"):
            method = "GET"
        body = None
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            n = 0
        if method == "POST":
            body = self.rfile.read(n) if 0 < n <= 8 * 1024 * 1024 else b""
            if n > 8 * 1024 * 1024:
                self.close_connection = True    # el cuerpo no se ha leido: la conexion no sirve para otra peticion
        elif n > 0:
            if n <= 1024 * 1024:
                self.rfile.read(n)              # un GET con cuerpo: se descarta para que no se mezcle con la siguiente peticion
            else:
                self.close_connection = True
        accept = self.headers.get("X-Soma-Accept") or "*/*"
        want_image = accept.lower().startswith("image/")
        h = {
            "User-Agent": self.headers.get("User-Agent") or "Mozilla/5.0 (X11; SOMA) AppleWebKit/537.36 (KHTML, like Gecko) SOMA-Browser/2.0 Safari/537.36",
            "Accept": "image/png,image/jpeg,image/gif,image/webp,image/svg+xml,image/*;q=0.8,*/*;q=0.5" if (want_image and HAVE_PIL) else accept,
            "Accept-Language": self.headers.get("Accept-Language") or "es-ES,es;q=0.9,en;q=0.8",
            "Referer": self.headers.get("X-Soma-Referer"),
            "Cookie": self.headers.get("X-Soma-Cookie"),
        }
        if method == "POST":
            h["Content-Type"] = self.headers.get("Content-Type") or "application/x-www-form-urlencoded"

        key = (url, _akind(accept)) if method == "GET" else None
        if key:
            with _lock:
                ev = _inflight.get(key)
            if ev:
                ev.wait(TIMEOUT + 5)            # la precarga ya lo esta descargando: se espera en vez de pedirlo dos veces
            cached = _cache_get(key)
            if cached:
                status, ctype, extra, data = cached
                self._reply(status, ctype, data, list(extra))
                return
        try:
            status, ctype, extra, data, has_cookie = get_resource(url, method, body, h, want_image)
        except ssl.SSLCertVerificationError as e:
            self._error(502, "Certificado no verificado",
                        "No se pudo comprobar el certificado del sitio (%s).<br>Si usas Python de python.org en macOS, ejecuta "
                        "<code>Install Certificates.command</code> o <code>pip3 install certifi</code>." % e.__class__.__name__, url)
            return
        except urllib.error.URLError as e:
            reason = getattr(e, "reason", e)
            if isinstance(reason, ssl.SSLCertVerificationError):
                self._error(502, "Certificado no verificado",
                            "No se pudo comprobar el certificado del sitio.<br>Si usas Python de python.org en macOS, ejecuta "
                            "<code>Install Certificates.command</code> o <code>pip3 install certifi</code>.", url)
            else:
                self._error(502, "No se pudo conectar", "El servidor no responde o el nombre no existe (%s)." % str(reason)[:120], url)
            return
        except Exception as e:            # tiempo agotado, conexion cortada...
            self._error(504, "Sin respuesta", "La descarga fall\u00f3: %s" % str(e)[:160], url)
            return
        if key and 200 <= status < 300 and (not has_cookie or _is_static(ctype)) and _is_static(ctype):
            _cache_put(key, (status, ctype, extra, data))
        if 200 <= status < 300 and method == "GET" and (ctype or "").lower().startswith(("text/html", "application/xhtml")):
            start_prefetch(data, url, h["User-Agent"], h["Accept-Language"])
        self._reply(status, ctype, data, list(extra))


    def _svg(self, parsed):
        """POST /svg?c=rrggbb con el marcado de un <svg> en linea -> PNG."""
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            n = 0
        if n <= 0 or n > 256 * 1024:
            self.close_connection = n > 0        # SOMA_343: el cuerpo se queda sin leer
            self._reply(400, "text/plain", b"svg vacio o demasiado grande", compress=False)
            return
        data = self.rfile.read(n)
        q = urllib.parse.parse_qs(parsed.query)
        color = (q.get("c") or [""])[0]
        key = ("svg", color, hash(data))
        png = _cache_get(key)
        if png is None:
            out = svg_to_png(data, 256, color=color, inline=True)
            png = (200, "image/png", (), out) if out else (422, "text/plain", (), b"no se pudo rasterizar")
            if out:
                with _lock:
                    _stats["svg"] += 1
            _cache_put(key, png)
        self._reply(png[0], png[1], png[3], compress=False)


class _Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def port_in_use(port, host="127.0.0.1"):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(0.4)
    try:
        return s.connect_ex((host, port)) == 0
    finally:
        s.close()


def serve(port=DEFAULT_PORT, bind="127.0.0.1"):
    srv = _Server((bind, port), Handler)
    return srv


_started = {"srv": None}


def ensure_gateway(port=DEFAULT_PORT, allow_private=False, verbose=False, quiet=False):
    """Arranca la pasarela en un hilo de fondo si aun no hay nada escuchando en el puerto. La usa ejecutar.py."""
    if _started["srv"] is not None:
        return True
    if port_in_use(port):
        if not quiet:
            print("[pasarela web] ya hay un servicio en 127.0.0.1:%d; se usa ese." % port)
        return True
    _opts["allow_private"] = bool(allow_private)
    _opts["verbose"] = bool(verbose)
    try:
        srv = serve(port)
    except OSError as e:
        print("[pasarela web] no se pudo abrir el puerto %d: %s" % (port, e))
        return False
    _started["srv"] = srv
    t = threading.Thread(target=srv.serve_forever, name="soma-web-gateway", daemon=True)
    t.start()
    if not quiet:
        print("[pasarela web] v%s en 127.0.0.1:%d (HTTPS para el navegador de SOMA)%s" % (
            VERSION, port, "" if HAVE_PIL else "  -  sin Pillow: instala 'pip3 install pillow' para ver WebP/AVIF/SVG"))
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description="Pasarela HTTPS del navegador de SOMA")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--allow-private", action="store_true", help="permite destinos de redes privadas y de esta maquina")
    ap.add_argument("--insecure", action="store_true", help="no verifica certificados TLS (solo para pruebas)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)
    _opts["allow_private"] = args.allow_private
    _opts["insecure"] = args.insecure
    _opts["verbose"] = args.verbose
    srv = serve(args.port, args.bind)
    print("Pasarela web de SOMA v%s en %s:%d  (Pillow: %s, brotli: %s, cairosvg: %s)" % (
        VERSION, args.bind, args.port, "si" if HAVE_PIL else "NO", "si" if brotli else "no", "si" if cairosvg else "no"))
    if not HAVE_PIL:
        print("  Aviso: sin Pillow no se convierten WebP/AVIF/SVG. Instala con: pip3 install pillow")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
