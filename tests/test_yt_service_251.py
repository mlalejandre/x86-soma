#!/usr/bin/env python3
"""SOMA_251_YOUTUBE_IA: prueba de lo que el servicio del anfitrion (tools/yt_service.py) da a la IA de la aplicacion YouTube.

Modo demo (sin YouTube ni yt-dlp):
  - /yt/search trae la descripcion como sexta columna; /yt/info tiene el formato que lee yt_parse_info (yt_proto.h);
  - /yt/subs da texto con marcas [m:ss]; /yt/audio es un WAV PCM de 16 bits, 16 kHz, mono, con su Content-Length y recortado a max.
Camino real, con un yt-dlp de pega y un servidor web local que hace de YouTube:
  - la ficha (fecha, vistas, idiomas con subtitulos, descripcion de varias lineas con emoji y tabuladores -> texto limpio);
  - los subtitulos: se elige la pista (la pedida, la del idioma del video, automatica solo en el idioma original), se bajan
    en json3 o en vtt (los automaticos repiten lineas) y se agrupan cada ~20 s; un video sin subtitulos da un 404 claro;
  - el sonido: ffmpeg lee la direccion de audio con las cabeceras de yt-dlp; si "YouTube" lo rechaza (403), plan B por tuberia.
Funciones sueltas: limpiar_parrafos, elegir_pista, _agrupar. Sin ffmpeg la prueba se salta (no falla).
"""
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import yt_service  # noqa: E402

fallos = []


def check(cond, msg):
    if not cond:
        fallos.append(msg)
        print("  FALLO: " + msg)


def get(base, ruta):
    try:
        with urllib.request.urlopen(base + ruta, timeout=180) as r:
            return r.status, r.read(), dict(r.headers)
    except urllib.error.HTTPError as e:
        return e.code, e.read(), {}


def wav_info(b):
    """-> (formato, canales, frecuencia, bits, bytes de datos) de un WAV canonico de 44 bytes de cabecera."""
    if len(b) < 44 or b[:4] != b"RIFF" or b[8:16] != b"WAVEfmt " or b[36:40] != b"data":
        return None
    fmt, ch, rate, _br, _al, bits = struct.unpack("<HHIIHH", b[20:36])
    return fmt, ch, rate, bits, struct.unpack("<I", b[40:44])[0]


def unidades():
    lp = yt_service.limpiar_parrafos
    check(lp("uno\r\n\r\n\r\n\r\ndos \U0001F600\ttab\n\n", 100) == "uno\n\ndos tab", "limpiar_parrafos: %r" % lp("uno\r\n\r\n\r\n\r\ndos \U0001F600\ttab\n\n", 100))
    check(lp("\n\n", 100) == "" and lp(None, 100) == "", "limpiar_parrafos vacio")
    largo = lp("\n".join("linea %d" % i for i in range(1000)), 300)
    check(len(largo) <= 300 and largo.startswith("linea 0\nlinea 1"), "limpiar_parrafos no respeta el maximo: %d" % len(largo))
    ep = yt_service.elegir_pista
    m = {"idioma": "en", "manual": ["es-ES", "en"], "auto": ["en", "es", "fr", "en-orig"]}
    check(ep(m, "es") == ("es-ES", False), "pista: la pedida a mano")
    check(ep(m, "auto") == ("en", False), "pista: la del idioma del video")
    check(ep(m, "fr") == ("en", False), "pista: sin la pedida, la original a mano (no una traduccion automatica)")
    m = {"idioma": "es", "manual": [], "auto": ["en", "es-orig", "fr"]}
    check(ep(m, "auto") == ("es-orig", True), "pista: automatica del idioma original: %r" % (ep(m, "auto"),))
    check(ep(m, "en") == ("es-orig", True), "pista: mejor la original automatica que una traduccion: %r" % (ep(m, "en"),))
    check(ep({"idioma": "", "manual": [], "auto": ["de", "en"]}, "auto") == ("en", True), "pista: idioma desconocido")
    check(ep({"idioma": "ja", "manual": ["ko"], "auto": []}, "es") == ("ko", False), "pista: lo que haya a mano")
    check(ep({"idioma": "es", "manual": [], "auto": []}, "es") is None, "pista: sin subtitulos")
    ag = yt_service._agrupar([(0, "a"), (5, "b"), (19.9, "c"), (20, "d"), (3700, "e"), (3701, "")])
    check(ag == "[0:00] a b c\n[0:20] d\n[1:01:40] e", "_agrupar: %r" % ag)
    check(yt_service.linea_resultado("AAAAAAAAAAA", 5, "t", "c", 1) == "AAAAAAAAAAA\t5\tt\tc\t1", "resultado sin descripcion: cinco columnas")
    check(yt_service.linea_resultado("AAAAAAAAAAA", 5, "t", "c", 1, "una\tdesc\nlarga").endswith("\t1\tuna desc larga"), "resultado con descripcion")


def demo(base):
    st, b, _h = get(base, "/yt/health")
    check(st == 200 and b"ia=1" in b, "health sin ia=1: %r" % b)
    st, b, _h = get(base, "/yt/search?q=x")
    lineas = b.decode("utf-8").splitlines()
    check(st == 200 and len(lineas) == 4 and all(len(l.split("\t")) == 6 and l.split("\t")[5] for l in lineas[1:]), "busqueda demo sin descripcion: %r" % lineas[1:2])
    st, b, _h = get(base, "/yt/info?id=DEMO0000001")
    t = b.decode("utf-8")
    cab, _sep, desc = t.partition("\n\n")
    campos = dict(l.split("\t", 1) for l in cab.splitlines()[1:])
    check(st == 200 and cab.startswith("SOMAYT 1 info\n"), "info: cabecera %r" % cab[:30])
    check(campos.get("duracion") == "30" and campos.get("subs") == "es" and len(campos.get("fecha", "")) == 10, "info: campos %r" % campos)
    check("Capitulos:" in desc and "\n\n" in desc, "info: la descripcion pierde sus parrafos")
    st, b, _h = get(base, "/yt/info?id=DEMO0000009")
    check(st == 404, "info de un video que no existe: %d" % st)
    st, b, _h = get(base, "/yt/info?id=mal")
    check(st == 400, "info con identificador no valido: %d" % st)
    st, b, _h = get(base, "/yt/subs?id=DEMO0000001&l=es")
    t = b.decode("utf-8")
    check(st == 200 and t.startswith("SOMAYT 1 subs es 0\n[0:00] ") and "[0:20] " in t, "subs demo: %r" % t[:60])
    st, b, h = get(base, "/yt/audio?id=DEMO0000002&max=7")
    w = wav_info(b)
    check(st == 200 and w == (1, 1, 16000, 16, 7 * 32000), "audio demo recortado a 7 s: %r" % (w,))
    check(int(h.get("Content-Length", 0)) == len(b) == 44 + 7 * 32000, "audio demo: Content-Length %r, %d bytes" % (h.get("Content-Length"), len(b)))
    st, b, _h = get(base, "/yt/audio?id=DEMO0000001&max=600")
    w = wav_info(b)
    check(st == 200 and w and w[4] == 30 * 32000, "audio demo entero (30 s): %r" % (w,))
    yt_service._opts["clave"] = "secreto"
    try:
        check(get(base, "/yt/info?id=DEMO0000001")[0] == 403 and get(base, "/yt/audio?id=DEMO0000001")[0] == 403 and get(base, "/yt/subs?id=DEMO0000001")[0] == 403, "sin clave se atiende")
        check(get(base, "/yt/subs?id=DEMO0000001&k=secreto")[0] == 200, "con clave no se atiende")
    finally:
        yt_service._opts["clave"] = ""


# ------------------------------------------------------------------ "YouTube" y "yt-dlp" de pega para el camino real
FALSO_YTDLP = r'''#!/usr/bin/env python3
import json, os, sys
a = sys.argv[1:]
base = os.environ["PRUEBA_BASE"]
d = os.environ["PRUEBA_DIR"]
open(os.path.join(d, "llamadas.txt"), "a").write(" ".join(a) + "\n")
vid = a[-1].split("v=")[-1]
if "-J" in a:
    if vid == "ZZZZZZZZZZZ":
        sys.stderr.write("ERROR: [youtube] ZZZZZZZZZZZ: Video unavailable\n"); sys.exit(1)
    pre = base + ("/403" if os.environ.get("PRUEBA_403") else "")
    cab = {"User-Agent": "UA-de-prueba/1.0", "X-Prueba": "soma"}
    j = {"id": vid, "title": "Título de prueba \U0001F3AC", "duration": 12.0, "channel": "Canal ñ", "view_count": 98765, "upload_date": "20240501",
         "language": "es", "description": "Primera línea \U0001F600 con\ttabulador.\r\n\r\n\r\n• Punto “uno”\n00:00 Intro\n",
         "subtitles": {"en": [{"ext": "json3"}], "live_chat": [{"ext": "json"}]}, "automatic_captions": {"es-orig": [{}], "es": [{}], "fr": [{}], "en": [{}]},
         "requested_formats": [{"url": pre + "/v.mkv", "http_headers": cab}, {"url": pre + "/a.mkv", "http_headers": cab}]}
    if vid == "SSSSSSSSSSS":
        j["subtitles"], j["automatic_captions"] = {}, {}
    if vid == "VVVVVVVVVVV":
        j["subtitles"] = {}
    print(json.dumps(j))
elif "--skip-download" in a and "--sub-langs" in a:
    lang = a[a.index("--sub-langs") + 1]
    out = a[a.index("-o") + 1]
    if "--write-auto-subs" in a:        # automaticos en vtt: cada linea se repite mientras avanza
        open(out + "." + lang + ".vtt", "w", encoding="utf-8").write(
            "WEBVTT\nKind: captions\nLanguage: es\n\n00:00:01.000 --> 00:00:03.000 align:start position:0%\nhola<00:00:01.500><c> a</c><c> todos</c>\n\n"
            "00:00:03.000 --> 00:00:05.000\nhola a todos\nbienvenidos &amp; gracias\n\n00:00:25.000 --> 00:00:27.000\nbienvenidos &amp; gracias\nsegunda parte\n\n"
            "01:02:03.000 --> 01:02:05.000\nfinal — adiós\n")
    else:
        json.dump({"events": [{"tStartMs": 0, "segs": [{"utf8": "Hello "}, {"utf8": "world"}]}, {"tStartMs": 4000, "aAppend": 1, "segs": [{"utf8": "\n"}]},
                              {"tStartMs": 9000, "segs": [{"utf8": "second\nline \U0001F600"}]}, {"tStartMs": 30000}, {"tStartMs": 31000, "segs": [{"utf8": "third"}]}]},
                  open(out + "." + lang + ".json3", "w"))
elif "-o" in a and a[a.index("-o") + 1] == "-":
    sys.stdout.buffer.write(open(os.path.join(d, "a.mkv"), "rb").read())
else:
    sys.exit(3)
'''

vistas = []


class FalsoYouTube(BaseHTTPRequestHandler):
    carpeta = "."

    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        vistas.append((self.path, self.headers.get("User-Agent"), self.headers.get("X-Prueba")))
        if self.path.startswith("/403"):
            self.send_response(403)
            self.end_headers()
            return
        try:
            datos = open(os.path.join(self.carpeta, os.path.basename(self.path)), "rb").read()
        except OSError:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(datos)))
        self.end_headers()
        try:
            self.wfile.write(datos)
        except (BrokenPipeError, ConnectionResetError):
            pass


def limpiar_caches():
    yt_service._cache_formatos.clear()
    yt_service._cache_meta.clear()
    yt_service._cache_texto.clear()
    yt_service._cache_audio["clave"] = None


def camino_real(base):
    with tempfile.TemporaryDirectory() as d:
        ff = ["ffmpeg", "-nostdin", "-loglevel", "error", "-y"]
        a = ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=12"]
        if subprocess.run(ff + a + ["-ac", "2", "-c:a", "aac", os.path.join(d, "a.mkv")]).returncode != 0:
            print("  (este ffmpeg no tiene aac: camino real omitido)")
            return
        FalsoYouTube.carpeta = d
        web = ThreadingHTTPServer(("127.0.0.1", 0), FalsoYouTube)
        threading.Thread(target=web.serve_forever, daemon=True).start()
        falso = os.path.join(d, "yt-dlp-de-pega")
        with open(falso, "w") as f:
            f.write(FALSO_YTDLP.replace("#!/usr/bin/env python3", "#!" + sys.executable, 1))   # SOMA_304_CI: el mismo Python (en macOS el de Xcode puede no arrancar)
        os.chmod(falso, 0o755)
        os.environ["PRUEBA_BASE"] = "http://127.0.0.1:%d" % web.server_address[1]
        os.environ["PRUEBA_DIR"] = d
        yt_service._opts.update(demo=False, ytdlp=falso)
        limpiar_caches()
        llamadas = os.path.join(d, "llamadas.txt")
        try:
            st, b, _h = get(base, "/yt/info?id=AAAAAAAAAAA")
            t = b.decode("utf-8")
            cab, _sep, desc = t.partition("\n\n")
            campos = dict(l.split("\t", 1) for l in cab.splitlines()[1:])
            check(st == 200 and campos == {"titulo": "Título de prueba", "canal": "Canal ñ", "duracion": "12", "vistas": "98765", "fecha": "2024-05-01", "subs": "en,es"},
                  "ficha real: %r" % campos)
            check(desc == 'Primera línea con tabulador.\n\n- Punto "uno"\n00:00 Intro\n', "descripcion real limpia: %r" % desc)
            get(base, "/yt/info?id=AAAAAAAAAAA")
            check(open(llamadas).read().count("-J") == 1, "la ficha no se recuerda (yt-dlp llamado dos veces)")
            st, b, _h = get(base, "/yt/info?id=ZZZZZZZZZZZ")
            check(st == 502 and b"Video unavailable" in b, "ficha de un video caido: %d %r" % (st, b[:80]))

            st, b, _h = get(base, "/yt/subs?id=AAAAAAAAAAA&l=en")                # a mano, json3
            check(st == 200 and b.decode("utf-8") == "SOMAYT 1 subs en 0\n[0:00] Hello world second line\n[0:31] third\n", "subs json3: %r" % b[:120])
            st, b, _h = get(base, "/yt/subs?id=VVVVVVVVVVV&l=auto")              # solo automaticos: los del idioma original, en vtt
            t = b.decode("utf-8")
            check(st == 200 and t == "SOMAYT 1 subs es 1\n[0:01] hola a todos bienvenidos & gracias\n[0:25] segunda parte\n[1:02:03] final - adiós\n", "subs vtt automaticos: %r" % t)
            log = open(llamadas).read()
            check("--write-subs --sub-langs en " in log and "--write-auto-subs --sub-langs es " in log, "pistas pedidas a yt-dlp: %r" % log[-300:])
            st, b, _h = get(base, "/yt/subs?id=SSSSSSSSSSS&l=es")
            check(st == 404 and b"whisper" in b, "video sin subtitulos: %d %r" % (st, b[:80]))
            st, b, _h = get(base, "/yt/subs?id=AAAAAAAAAAA&l=../../etc")         # idioma no valido: como "auto" (los del idioma del video)
            check(st == 200 and b.startswith(b"SOMAYT 1 subs es 1\n"), "subs con idioma no valido: %d %r" % (st, b[:40]))
            check("etc" not in open(llamadas).read(), "un idioma no valido ha llegado a yt-dlp")

            del vistas[:]
            st, b, _h = get(base, "/yt/audio?id=AAAAAAAAAAA&max=600")            # plan A: ffmpeg abre la direccion de audio
            w = wav_info(b)
            check(st == 200 and w and w[:4] == (1, 1, 16000, 16) and abs(w[4] - 12 * 32000) < 6400, "audio real: %r" % (w,))
            check([p for p, _u, _x in vistas] == ["/a.mkv"], "audio real: se pidio %r" % [p for p, _u, _x in vistas])
            check(all(u == "UA-de-prueba/1.0" and x == "soma" for _p, u, x in vistas), "ffmpeg no envio las cabeceras de yt-dlp")
            st, b, _h = get(base, "/yt/audio?id=AAAAAAAAAAA&max=5")
            w = wav_info(b)
            check(st == 200 and w and w[4] == 5 * 32000, "audio real recortado a 5 s: %r" % (w,))

            os.environ["PRUEBA_403"] = "1"                                        # "YouTube" rechaza a ffmpeg: plan B por tuberia
            limpiar_caches()
            st, b, _h = get(base, "/yt/audio?id=AAAAAAAAAAA&max=600")
            w = wav_info(b)
            check(st == 200 and w and abs(w[4] - 12 * 32000) < 6400, "audio plan B: %d %r" % (st, w))
            check("-f ba/b -o - " in open(llamadas).read(), "plan B: no se llamo a yt-dlp para descargar el audio")
            del os.environ["PRUEBA_403"]
        finally:
            yt_service._opts.update(demo=True, ytdlp="yt-dlp")
            limpiar_caches()
            web.shutdown()


def main():
    unidades()
    if not shutil.which("ffmpeg"):
        print("[yt_service_251] sin ffmpeg: solo las pruebas sueltas (%d fallos)" % len(fallos))
        return 1 if fallos else 0
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    puerto = s.getsockname()[1]
    s.close()
    yt_service._opts.update(demo=True, clave="", verbose=False)
    srv = yt_service._Servidor(("127.0.0.1", puerto), yt_service.Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    base = "http://127.0.0.1:%d" % puerto
    try:
        demo(base)
        camino_real(base)
    finally:
        srv.shutdown()
    print("[yt_service_251] %s" % ("OK" if not fallos else "%d fallos" % len(fallos)))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
