#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""sonda_youtube_nativo.py - Que le daria YouTube HOY a un SOMA que hablase con el directamente (sin yt-dlp, sin JavaScript).

Hace desde tu Mac exactamente las peticiones HTTPS que haria SOMA: una busqueda y, para varios "clientes" de YouTube, la
peticion del reproductor de un video. Despues intenta bajar los primeros 64 KiB de cada formato util. Solo usa la biblioteca
estandar de Python. No instala ni cambia nada. El resultado queda en soma_sonda_yt.txt: enviamelo.

Uso:   python3 sonda_youtube_nativo.py            (video de prueba: Big Buck Bunny)
       python3 sonda_youtube_nativo.py <id de 11 caracteres> [otro id ...]
       python3 sonda_youtube_nativo.py --muestras  guarda ademas las respuestas tal cual en soma_yt_muestras.zip (para escribir y
                                                   probar los lectores de SOMA). OJO: las direcciones de video que contiene llevan
                                                   tu IP publica y caducan en unas horas.
"""
import zipfile
import json
import sys
import time
import urllib.error
import urllib.request

SALIDA = "soma_sonda_yt.txt"
lineas = []
MUESTRAS = {}                 # nombre -> bytes (solo con --muestras)
GUARDAR = "--muestras" in sys.argv


def p(s=""):
    print(s)
    lineas.append(s)


# Clientes de YouTube que no necesitan ejecutar el JavaScript del reproductor (tomados de yt-dlp, 2026-11-21).
CLIENTES = [
    ("visionos", 101, {"clientName": "VISIONOS", "clientVersion": "1.02", "deviceMake": "Apple", "deviceModel": "RealityDevice17,1",
                       "osName": "visionOS", "osVersion": "26.5.23O471",
                       "userAgent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15"}),
    ("android_vr", 28, {"clientName": "ANDROID_VR", "clientVersion": "1.65.10", "deviceMake": "Oculus", "deviceModel": "Quest 3",
                        "androidSdkVersion": 32, "osName": "Android", "osVersion": "12L",
                        "userAgent": "com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip"}),
    ("ios", 5, {"clientName": "IOS", "clientVersion": "21.26.4", "deviceMake": "Apple", "deviceModel": "iPhone16,2",
                "osName": "iPhone", "osVersion": "18.3.2.22D82",
                "userAgent": "com.google.ios.youtube/21.26.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)"}),
    ("android", 3, {"clientName": "ANDROID", "clientVersion": "21.26.364", "androidSdkVersion": 30, "osName": "Android", "osVersion": "11",
                    "userAgent": "com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip"}),
    ("tv", 7, {"clientName": "TVHTML5", "clientVersion": "7.20260707.07.00",
               "userAgent": "Mozilla/5.0 (ChromiumStylePlatform) Cobalt/25.lts.30.1034943-gold (unlike Gecko), Unknown_TV_Unknown_0/Unknown (Unknown, Unknown)"}),
]
WEB = {"clientName": "WEB", "clientVersion": "2.20260708.00.00",
       "userAgent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36"}


def post(ruta, cliente, num, cuerpo):
    c = dict(cliente)
    c.update({"hl": "es", "gl": "ES", "timeZone": "UTC", "utcOffsetMinutes": 0})
    cuerpo = dict(cuerpo)
    cuerpo["context"] = {"client": c}
    datos = json.dumps(cuerpo).encode("utf-8")
    req = urllib.request.Request("https://www.youtube.com/youtubei/v1/%s?prettyPrint=false" % ruta, data=datos, headers={
        "Content-Type": "application/json", "User-Agent": cliente["userAgent"], "X-YouTube-Client-Name": str(num),
        "X-YouTube-Client-Version": cliente["clientVersion"], "Origin": "https://www.youtube.com", "Accept-Language": "es"})
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=25) as r:
            b = r.read(8 * 1024 * 1024)
            return r.status, b, time.time() - t0, len(datos)
    except urllib.error.HTTPError as e:
        return e.code, e.read(4000), time.time() - t0, len(datos)
    except Exception as e:
        return -1, repr(e).encode(), time.time() - t0, len(datos)


def trozo(url, ua):
    """Baja los primeros 64 KiB como lo haria SOMA. -> (estado, bytes, tipo de contenedor, segundos)"""
    req = urllib.request.Request(url, headers={"User-Agent": ua, "Range": "bytes=0-65535"})
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=25) as r:
            b = r.read(70000)
            cont = "mp4" if b[4:8] == b"ftyp" else "webm" if b[:4] == b"\x1a\x45\xdf\xa3" else "m3u8" if b[:7] == b"#EXTM3U" else b[:8].hex()
            return r.status, len(b), cont, time.time() - t0
    except urllib.error.HTTPError as e:
        return e.code, 0, "", time.time() - t0
    except Exception as e:
        return -1, 0, repr(e)[:60], time.time() - t0


def videos_de(o, out):
    """Recorre la respuesta de busqueda y saca (id, titulo) de cada videoRenderer."""
    if isinstance(o, dict):
        v = o.get("videoRenderer")
        if isinstance(v, dict) and v.get("videoId"):
            t = v.get("title") or {}
            titulo = "".join(r.get("text", "") for r in (t.get("runs") or [])) or t.get("simpleText", "")
            out.append((v["videoId"], titulo[:60], ((v.get("lengthText") or {}).get("simpleText") or "")))
        for x in o.values():
            videos_de(x, out)
    elif isinstance(o, list):
        for x in o:
            videos_de(x, out)


def sonda_busqueda():
    p("== BUSQUEDA (cliente WEB, youtubei/v1/search) ==")
    st, b, seg, env = post("search", WEB, 1, {"query": "big buck bunny"})
    p("estado %d, %d bytes de respuesta en %.1f s (peticion de %d bytes)" % (st, len(b), seg, env))
    if st != 200:
        p("  respuesta: %r" % b[:300])
        return
    if GUARDAR:
        MUESTRAS["busqueda_web.json"] = b
        for nombre, num, cli in (("busqueda_android.json", 3, CLIENTES[3][2]), ("busqueda_ios.json", 5, CLIENTES[2][2])):
            st3, b3, s3, _e = post("search", cli, num, {"query": "big buck bunny"})
            p("  (muestra %s: estado %d, %d bytes, %.1f s)" % (nombre, st3, len(b3), s3))
            MUESTRAS[nombre] = b3
        st3, b3, s3, _e = post("search", WEB, 1, {"query": "como hacer pan de masa madre", "params": "EgIQAQ%3D%3D"})
        p("  (muestra busqueda_web_solo_videos.json: estado %d, %d bytes)" % (st3, len(b3)))
        MUESTRAS["busqueda_web_solo_videos.json"] = b3
    try:
        res = []
        videos_de(json.loads(b.decode("utf-8", "replace")), res)
        p("  videos encontrados: %d" % len(res))
        for r in res[:4]:
            p("    %s  %s  %s" % r)
    except ValueError as e:
        p("  JSON no valido: %s" % e)
    for nombre in ("mqdefault.jpg",):
        try:
            with urllib.request.urlopen("https://i.ytimg.com/vi/aqz-KE-bpKQ/" + nombre, timeout=15) as r:
                j = r.read(300000)
            prog = b"\xff\xc2" in j
            p("  miniatura %s: %d bytes, JPEG %s" % (nombre, len(j), "PROGRESIVO" if prog else "baseline"))
        except Exception as e:
            p("  miniatura %s: %r" % (nombre, e))


def describe(f):
    mime = (f.get("mimeType") or "").replace(" ", "")
    como = "URL" if f.get("url") else "CIFRADA(js)" if (f.get("signatureCipher") or f.get("cipher")) else "sin-url"
    return "itag %-4s %-44s %-9s %7s kbit/s  %s" % (f.get("itag"), mime[:44], f.get("qualityLabel") or f.get("audioQuality", "")[-6:],
                                                 int((f.get("bitrate") or 0) / 1000), como)


def sonda_video(vid):
    p("")
    p("=" * 100)
    p("VIDEO %s" % vid)
    for nombre, num, cliente in CLIENTES:
        p("")
        p("-- cliente %s --" % nombre)
        st, b, seg, env = post("player", cliente, num, {
            "videoId": vid, "contentCheckOk": True, "racyCheckOk": True,
            "playbackContext": {"contentPlaybackContext": {"html5Preference": "HTML5_PREF_WANTS"}}})
        p("estado %d, %d bytes en %.1f s" % (st, len(b), seg))
        if st != 200:
            p("  respuesta: %r" % b[:300])
            continue
        try:
            j = json.loads(b.decode("utf-8", "replace"))
        except ValueError as e:
            p("  JSON no valido: %s" % e)
            continue
        if GUARDAR and nombre in ("android", "ios"):
            MUESTRAS["player_%s_%s.json" % (nombre, vid)] = b
        ps = j.get("playabilityStatus") or {}
        p("  reproducible: %s  %s" % (ps.get("status"), (ps.get("reason") or "")[:120]))
        sd = j.get("streamingData") or {}
        vd = j.get("videoDetails") or {}
        p("  titulo: %s | duracion %s s | descripcion %d caracteres" % ((vd.get("title") or "")[:50], vd.get("lengthSeconds"), len(vd.get("shortDescription") or "")))
        subs = (((j.get("captions") or {}).get("playerCaptionsTracklistRenderer") or {}).get("captionTracks")) or []
        p("  subtitulos: %s" % ", ".join("%s%s" % (c.get("languageCode"), "(auto)" if c.get("kind") == "asr" else "") for c in subs[:8]))
        p("  SABR (serverAbrStreamingUrl): %s | HLS: %s | DASH: %s" % (
            "SI" if sd.get("serverAbrStreamingUrl") else "no", "SI" if sd.get("hlsManifestUrl") else "no", "SI" if sd.get("dashManifestUrl") else "no"))
        juntos, sep = sd.get("formats") or [], sd.get("adaptiveFormats") or []
        p("  formatos con video+audio: %d | separados: %d (con URL directa: %d)" % (len(juntos), len(sep), sum(1 for f in sep if f.get("url"))))
        for f in juntos:
            p("    " + describe(f))
        interes = [f for f in sep if "avc1" in (f.get("mimeType") or "") and (f.get("height") or 0) <= 720] + \
                  [f for f in sep if "mp4a" in (f.get("mimeType") or "")] + [f for f in sep if "opus" in (f.get("mimeType") or "")][:1]
        for f in interes:
            p("    " + describe(f))
        otros = sorted(set((f.get("mimeType") or "").split(";")[0] + "/" + ((f.get("mimeType") or "").split('codecs="')[-1][:4]) for f in sep))
        p("    codecs separados: %s" % ", ".join(otros))
        ua = cliente["userAgent"]
        probar = [f for f in juntos if f.get("url")][:1] + [f for f in interes if f.get("url") and f.get("itag") in (134, 135, 136, 140)]
        for f in probar:
            st2, n, cont, s2 = trozo(f["url"], ua)
            p("    DESCARGA itag %s: estado %d, %d bytes, contenedor %s, %.1f s%s" % (
                f.get("itag"), st2, n, cont, s2, "   <-- SOMA podria reproducirlo" if st2 in (200, 206) and n > 1000 else ""))
        if GUARDAR and nombre == "android":
            for f in [x for x in juntos if x.get("url")][:1]:
                url = f["url"]
                total = int(f.get("contentLength") or 0)
                p("    itag %s: contentLength %s, bitrate %s, %sx%s, fps %s, audio %s Hz" % (f.get("itag"), f.get("contentLength"), f.get("bitrate"),
                                                                                    f.get("width"), f.get("height"), f.get("fps"), f.get("audioSampleRate")))
                for etiqueta, rango in (("inicio 1 MiB", "bytes=0-1048575"), ("mitad 256 KiB", "bytes=%d-%d" % (total // 2, total // 2 + 262143) if total else None),
                                        ("final 256 KiB", "bytes=%d-%d" % (max(0, total - 262144), total - 1) if total else None), ("sin Range (8 s)", "")):
                    if rango is None:
                        continue
                    cab = {"User-Agent": ua}
                    if rango:
                        cab["Range"] = rango
                    t0 = time.time()
                    try:
                        with urllib.request.urlopen(urllib.request.Request(url, headers=cab), timeout=25) as r:
                            if rango:
                                d = r.read(1200000)
                            else:
                                d = b""
                                while time.time() - t0 < 8:
                                    x = r.read(65536)
                                    if not x:
                                        break
                                    d += x
                            dt = time.time() - t0
                            p("    %-14s estado %d, %d bytes en %.1f s (%.0f KiB/s) Content-Range: %s" % (etiqueta, r.status, len(d), dt, len(d) / 1024 / max(dt, 0.01), r.headers.get("Content-Range")))
                            if etiqueta.startswith("inicio"):
                                MUESTRAS["itag18_inicio_%s.mp4" % vid] = d
                                cajas, o = [], 0
                                while o + 8 <= len(d) and len(cajas) < 8:
                                    n = int.from_bytes(d[o:o + 4], "big")
                                    cajas.append("%s(%d)" % (d[o + 4:o + 8].decode("latin1"), n))
                                    if n < 8:
                                        break
                                    o += n
                                p("    cajas MP4 al principio: %s" % " ".join(cajas))
                            if etiqueta.startswith("final"):
                                p("    'moov' en el ultimo trozo: %s" % ("SI" if b"moov" in d else "no"))
                    except urllib.error.HTTPError as e:
                        p("    %-14s estado %d" % (etiqueta, e.code))
                    except Exception as e:
                        p("    %-14s %r" % (etiqueta, e))
        if sd.get("hlsManifestUrl"):
            st2, n, cont, s2 = trozo(sd["hlsManifestUrl"], ua)
            p("    DESCARGA lista HLS: estado %d, %d bytes, %s" % (st2, n, cont))


def main():
    ids = [a for a in sys.argv[1:] if len(a) == 11] or ["aqz-KE-bpKQ"]
    p("sonda_youtube_nativo  %s  Python %s" % (time.strftime("%Y-%m-%d %H:%M"), sys.version.split()[0]))
    sonda_busqueda()
    for vid in ids:
        sonda_video(vid)
    with open(SALIDA, "w", encoding="utf-8") as f:
        f.write("\n".join(lineas) + "\n")
    if GUARDAR:
        MUESTRAS[SALIDA] = ("\n".join(lineas) + "\n").encode("utf-8")
        with zipfile.ZipFile("soma_yt_muestras.zip", "w", zipfile.ZIP_DEFLATED) as z:
            for nombre, datos in MUESTRAS.items():
                z.writestr(nombre, datos)
        print("\nGuardado en soma_yt_muestras.zip (%d archivos): enviamelo." % len(MUESTRAS))
        return 0
    print("\nGuardado en %s: enviamelo." % SALIDA)
    return 0


if __name__ == "__main__":
    sys.exit(main())
