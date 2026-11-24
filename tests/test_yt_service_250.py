#!/usr/bin/env python3
"""SOMA_250_YOUTUBE: prueba del servicio del anfitrion (tools/yt_service.py) en modo demo, sin YouTube ni yt-dlp.

Arranca el servicio en un puerto libre y comprueba lo que SOMA va a recibir:
  - la lista de resultados tiene el formato que lee yt_parse_results (src/apps/yt_proto.h);
  - la miniatura es un JPEG de 168x94;
  - el video emitido es un AVI que el LECTOR Y EL DECODIFICADOR DE SOMA aceptan: se pasa por tests/avicheck_250.c
    (src/core/avi.c + src/core/jpeg.c, el mismo codigo del reproductor), que decodifica todos los fotogramas;
  - el audio y el video van intercalados de cerca (el reproductor los lee con dos cursores sobre una ventana de memoria);
  - pedir el video desde un segundo t entrega solo el resto; identificadores no validos y clave incorrecta se rechazan;
  - el camino REAL (sin modo demo) con un yt-dlp de pega y un servidor web local que hace de YouTube: la busqueda (se descartan
    canales y listas, se limpia el texto), las direcciones con sus cabeceras HTTP (ffmpeg debe enviarlas), el salto y el plan B
    (si "YouTube" rechaza a ffmpeg con un 403, el video llega por una tuberia desde yt-dlp).
Uso: test_yt_service_250.py <ruta de avicheck_250 compilado>. Sin ffmpeg la prueba se salta (no falla).
"""
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
        with urllib.request.urlopen(base + ruta, timeout=120) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def recorrer(avi):
    """-> (ancho, alto, fotogramas, bytes de audio, mayor distancia en bytes entre un trozo de audio y el video siguiente)."""
    check(avi[:4] == b"RIFF" and avi[8:12] == b"AVI ", "no es RIFF AVI")
    i = avi.find(b"movi")
    check(i > 0, "sin lista movi")
    ancho, alto = struct.unpack("<II", avi[64:72])          # avih: dwWidth, dwHeight
    i += 4
    nv = ab = 0
    ultimo_v = i
    max_dist = 0
    while i + 8 <= len(avi):
        fcc, tam = avi[i:i + 4], struct.unpack("<I", avi[i + 4:i + 8])[0]
        if fcc == b"00dc":
            nv += 1
            check(tam == 0 or avi[i + 8:i + 10] == b"\xff\xd8", "un fotograma no es JPEG")    # vacio = repetir el anterior
            ultimo_v = i
        elif fcc == b"01wb":
            ab += tam
            max_dist = max(max_dist, i - ultimo_v)
        else:
            check(False, "trozo inesperado %r" % fcc)
            break
        i += 8 + tam + (tam & 1)
    check(i == len(avi), "el AVI no acaba en un limite de trozo")
    return ancho, alto, nv, ab, max_dist


# ------------------------------------------------------------------ "YouTube" y "yt-dlp" de pega para el camino real
FALSO_YTDLP = r'''#!/usr/bin/env python3
import json, os, sys
a = sys.argv[1:]
base = os.environ["PRUEBA_BASE"]
d = os.environ["PRUEBA_DIR"]
open(os.path.join(d, "llamadas.txt"), "a").write(" ".join(a) + "\n")
if any(x.startswith("ytsearch") for x in a):
    for e in ({"id": "AAAAAAAAAAA", "title": "T\u00edtulo\tcon tab \U0001F600 y \u201ccomillas\u201d", "duration": 12.0, "channel": "Canal \u00f1", "view_count": 1234567},
              {"id": "UCxxxxxxxxxxxxxxxxxxxxxx", "title": "Un canal, no un video"},
              {"id": "BBBBBBBBBBB", "title": "Directo", "duration": None, "uploader": "Otro", "view_count": None}):
        print(json.dumps(e))
elif "-J" in a:
    pre = base + ("/403" if os.environ.get("PRUEBA_403") else "")
    cab = {"User-Agent": "UA-de-prueba/1.0", "X-Prueba": "soma"}
    print(json.dumps({"id": "AAAAAAAAAAA", "title": "T\u00edtulo de prueba", "duration": 12.0,
                      "requested_formats": [{"url": pre + "/v.mkv", "http_headers": cab}, {"url": pre + "/a.mkv", "http_headers": cab}]}))
elif "-o" in a and a[a.index("-o") + 1] == "-":
    sys.stdout.buffer.write(open(os.path.join(d, "av.mkv"), "rb").read())
else:
    sys.exit(3)
'''

vistas = []                                  # (ruta, User-Agent, X-Prueba, Range) de cada peticion al "YouTube" local


class FalsoYouTube(BaseHTTPRequestHandler):
    carpeta = "."

    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        vistas.append((self.path, self.headers.get("User-Agent"), self.headers.get("X-Prueba"), self.headers.get("Range")))
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
        ini = 0
        r = self.headers.get("Range")
        if r and r.startswith("bytes="):
            ini = int(r[6:].split("-")[0] or 0)
            self.send_response(206)
            self.send_header("Content-Range", "bytes %d-%d/%d" % (ini, len(datos) - 1, len(datos)))
        else:
            self.send_response(200)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(len(datos) - ini))
        self.end_headers()
        try:
            self.wfile.write(datos[ini:])
        except (BrokenPipeError, ConnectionResetError):
            pass


def camino_real(base):
    with tempfile.TemporaryDirectory() as d:
        ff = ["ffmpeg", "-nostdin", "-loglevel", "error", "-y"]
        v = ["-f", "lavfi", "-i", "testsrc2=size=320x180:rate=25:duration=12"]
        a = ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=12"]
        ok = (subprocess.run(ff + v + ["-c:v", "mpeg4", "-q:v", "5", os.path.join(d, "v.mkv")]).returncode == 0 and
              subprocess.run(ff + a + ["-c:a", "aac", os.path.join(d, "a.mkv")]).returncode == 0 and
              subprocess.run(ff + v + a + ["-c:v", "mpeg4", "-q:v", "5", "-c:a", "aac", os.path.join(d, "av.mkv")]).returncode == 0)
        if not ok:
            print("  (este ffmpeg no tiene mpeg4/aac: camino real omitido)")
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
        try:
            st, b = get(base, "/yt/search?q=gatos&n=5")
            lineas = b.decode("utf-8").splitlines()
            check(st == 200 and lineas[0] == "SOMAYT 1 2", "busqueda real: %r" % lineas[:1])
            if len(lineas) == 3:
                c = lineas[1].split("\t")
                check(c == ["AAAAAAAAAAA", "12", 'T\u00edtulo con tab y "comillas"', "Canal \u00f1", "1234567"], "resultado real limpio: %r" % c)
                check(lineas[2].split("\t") == ["BBBBBBBBBBB", "0", "Directo", "Otro", "0"], "directo sin duracion: %r" % lineas[2])
            check("ytsearch5:gatos" in open(os.path.join(d, "llamadas.txt")).read(), "la busqueda no llego a yt-dlp tal cual")

            del vistas[:]
            st, avi = get(base, "/yt/video?id=AAAAAAAAAAA&h=240&fps=10")       # plan A: ffmpeg abre las dos direcciones
            check(st == 200, "video real: estado %d %r" % (st, avi[:120]))
            if st == 200:
                ancho, alto, nv, ab, dist = recorrer(avi)
                check(alto == 240 and abs(nv - 120) <= 3, "video real: %dx%d, %d fotogramas (esperados 120)" % (ancho, alto, nv))
                check(abs(ab - 12 * 48000 * 4) <= 48000 * 4, "video real: audio %d bytes" % ab)
            check(any(p == "/v.mkv" for p, _u, _x, _r in vistas) and any(p == "/a.mkv" for p, _u, _x, _r in vistas), "ffmpeg no pidio video y audio")
            check(all(u == "UA-de-prueba/1.0" and x == "soma" for _p, u, x, _r in vistas), "ffmpeg no envio las cabeceras de yt-dlp: %r" % vistas[:2])

            st, avi = get(base, "/yt/video?id=AAAAAAAAAAA&t=8&h=240&fps=10")   # salto: quedan 4 s
            if st == 200:
                ancho, alto, nv, ab, dist = recorrer(avi)
                check(abs(nv - 40) <= 3 and struct.unpack("<I", avi[48:52])[0] == 40, "salto real a t=8: %d fotogramas (esperados 40)" % nv)
            else:
                check(False, "salto real: estado %d" % st)

            os.environ["PRUEBA_403"] = "1"                                      # "YouTube" rechaza a ffmpeg: plan B por tuberia
            yt_service._cache_formatos.clear()
            st, avi = get(base, "/yt/video?id=AAAAAAAAAAA&t=6&h=240&fps=10")
            check(st == 200, "plan B: estado %d %r" % (st, avi[:120]))
            if st == 200:
                ancho, alto, nv, ab, dist = recorrer(avi)
                check(alto == 240 and abs(nv - 60) <= 3, "plan B: %d fotogramas (esperados 60)" % nv)
            check(" -o - " in open(os.path.join(d, "llamadas.txt")).read(), "plan B: no se llamo a yt-dlp para descargar")
            del os.environ["PRUEBA_403"]
        finally:
            yt_service._opts.update(demo=True, ytdlp="yt-dlp")
            yt_service._cache_formatos.clear()
            yt_service._cache_busqueda.clear()
            web.shutdown()


def main():
    if not shutil.which("ffmpeg"):
        print("[yt_service_250] sin ffmpeg: prueba omitida")
        return 0
    avidump = sys.argv[1] if len(sys.argv) > 1 else None
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    puerto = s.getsockname()[1]
    s.close()
    yt_service._opts.update(demo=True, clave="", verbose=False)
    srv = yt_service._Servidor(("127.0.0.1", puerto), yt_service.Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    base = "http://127.0.0.1:%d" % puerto

    st, b = get(base, "/yt/health")
    check(st == 200 and b.startswith(b"SOMAYT 1 ok"), "health")

    st, b = get(base, "/yt/search?q=prueba&n=12")
    lineas = b.decode("utf-8").splitlines()
    check(st == 200 and lineas and lineas[0].startswith("SOMAYT 1 "), "cabecera de la busqueda")
    check(int(lineas[0].split()[2]) == len(lineas) - 1 == 3, "numero de resultados")
    for l in lineas[1:]:
        c = l.split("\t")
        check(len(c) in (5, 6) and yt_service.ID_RE.match(c[0]) and c[1].isdigit() and c[4].isdigit() and c[2], "linea de resultado: %r" % l)
    st, b = get(base, "/yt/search?q=")
    check(st == 400, "busqueda vacia -> 400")

    st, b = get(base, "/yt/thumb?id=DEMO0000001")
    check(st == 200 and b[:2] == b"\xff\xd8", "miniatura JPEG")
    try:
        from PIL import Image
        import io
        im = Image.open(io.BytesIO(b))
        check(im.size == (168, 94), "tamano de la miniatura %r" % (im.size,))
    except ImportError:
        pass

    for mala in ("/yt/video?id=corto", "/yt/video?id=..%2F..%2Fetc%2Fx", "/yt/thumb?id=DEMO000000%3B", "/yt/video"):
        st, b = get(base, mala)
        check(st == 400, "identificador no valido aceptado: %s" % mala)
    st, b = get(base, "/yt/otra")
    check(st == 404, "ruta desconocida -> 404")

    # video completo de 30 s a 360p y 15 fps
    st, avi = get(base, "/yt/video?id=DEMO0000001&h=360&fps=15")
    check(st == 200, "video: estado %d" % st)
    ancho, alto, nv, ab, dist = recorrer(avi)
    check((ancho, alto) == (640, 360), "tamano del video %dx%d" % (ancho, alto))
    check(abs(nv - 450) <= 2, "fotogramas: %d (esperados 450)" % nv)
    check(abs(ab - 30 * 48000 * 4) <= 48000 * 4 // 5, "audio: %d bytes (esperados %d)" % (ab, 30 * 48000 * 4))
    check(dist < 400 * 1024, "audio y video demasiado separados: %d bytes" % dist)
    check(struct.unpack("<I", avi[48:52])[0] == 450, "la cabecera no lleva la duracion (fotogramas totales)")
    if avidump and os.path.exists(avidump):
        with tempfile.TemporaryDirectory() as d:
            ruta = os.path.join(d, "v.avi")
            with open(ruta, "wb") as f:
                f.write(avi)
            r = subprocess.run([avidump, ruta], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
            campos = r.stdout.decode().split()
            check(r.returncode == 0 and len(campos) == 9, "el lector de SOMA rechaza el AVI: %s" % r.stderr.decode()[:200])
            if len(campos) == 9:
                check(campos[:2] == ["640", "360"] and int(campos[2]) == nv and int(campos[4]) == ab and campos[5:] == ["48000", "2", "66666", "450"],
                      "lo que ve el lector de SOMA: %r" % campos)
                check(int(campos[3]) <= nv // 20, "demasiados fotogramas repetidos: %s" % campos[3])
    else:
        print("  (sin avicheck_250: no se comprueba con el decodificador de SOMA)")

    # desde el segundo 20: solo quedan 10 s; y a 240p con 10 fps
    st, avi = get(base, "/yt/video?id=DEMO0000001&t=20&h=240&fps=10")
    ancho, alto, nv, ab, dist = recorrer(avi)
    check(st == 200 and alto == 240 and ancho in (426, 428), "240p: %dx%d" % (ancho, alto))
    check(abs(nv - 100) <= 2 and struct.unpack("<I", avi[48:52])[0] == 100, "salto a t=20: %d fotogramas (esperados 100)" % nv)

    # con clave: sin ella o con otra, 403
    yt_service._opts["clave"] = "s3creto"
    st, b = get(base, "/yt/search?q=x")
    check(st == 403, "sin clave -> 403")
    st, b = get(base, "/yt/search?q=x&k=otra")
    check(st == 403, "clave incorrecta -> 403")
    st, b = get(base, "/yt/search?q=x&k=s3creto")
    check(st == 200, "clave correcta -> 200")
    st, b = get(base, "/yt/health")
    check(st == 200, "health no pide clave")
    yt_service._opts["clave"] = ""

    camino_real(base)

    # piezas puras
    check(yt_service.limpiar("Hola\tmundo\n áñ \U0001F600 中文 “comillas”", 80) == 'Hola mundo áñ "comillas"', "limpiar")
    cab = yt_service.cabecera_avi(640, 360, 15, 450, 48000, 2)
    check(cab[:4] == b"RIFF" and cab[-4:] == b"movi" and len(cab) % 2 == 0, "cabecera AVI")

    srv.shutdown()
    print("[yt_service_250] %s" % ("FALLOS: %d" % len(fallos) if fallos else "OK"))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
