#!/usr/bin/env python3
"""SOMA_252_VIDEO_NATIVO: el reproductor de video nativo, entero, en el anfitrion.

Compila para el anfitrion el MISMO codigo que va en ring 3 (src/user/vplay + third_party/libavc + third_party/libxaac) y lo
enfrenta a un nucleo simulado (tests/vplay_host_252.c) con videos generados por ffmpeg:
  - yt18:   640x360, H.264 Baseline + AAC estereo, indice al principio  (como el formato 18 de YouTube)
  - alto:   854x480, H.264 High con fotogramas B + AAC mono, indice al FINAL del archivo
  - mudo:   solo video (sin pista de audio)
Guiones: normal (todos los fotogramas, a su hora, hasta el final), lento (red mas lenta que el video), saltos (adelante,
atras, pausa, casi al final) y corte (la descarga falla a la mitad). Ademas compara lo decodificado con ffmpeg.
La primera vez tarda (compila unas 270 fuentes de terceros); despues reutiliza build/tests/vplay252/.
Sin ffmpeg con libx264 y aac la prueba se salta (no falla)."""
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys

RAIZ = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(RAIZ, "build", "tests", "vplay252")
CC = os.environ.get("HOST_CC") or os.environ.get("CC") or "cc"
T = os.path.join(RAIZ, "third_party")
V = os.path.join(RAIZ, "src", "user", "vplay")
AVC_INC = ["-I" + os.path.join(T, "libavc", "common"), "-I" + os.path.join(T, "libavc", "decoder"), "-DX86"]
XA_INC = ["-I" + os.path.join(T, "libxaac", "common"), "-I" + os.path.join(T, "libxaac", "decoder"), "-I" + os.path.join(T, "libxaac", "decoder", "drc_src")]

HILOS = r'''
int ithread_get_handle_size(void){return 8;} int ithread_get_mutex_lock_size(void){return 8;} int ithread_get_cond_struct_size(void){return 8;}
int ithread_create(void*a,void*b,void*c,void*d){(void)a;(void)b;(void)c;(void)d;return -1;} int ithread_join(void*a,void**b){(void)a;(void)b;return 0;}
int ithread_mutex_init(void*a){(void)a;return 0;} int ithread_mutex_destroy(void*a){(void)a;return 0;} int ithread_mutex_lock(void*a){(void)a;return 0;} int ithread_mutex_unlock(void*a){(void)a;return 0;}
int ithread_cond_init(void*a){(void)a;return 0;} int ithread_cond_destroy(void*a){(void)a;return 0;} int ithread_cond_wait(void*a,void*b){(void)a;(void)b;return 0;} int ithread_cond_signal(void*a){(void)a;return 0;}
void ithread_yield(void){} void ithread_usleep(unsigned u){(void)u;} void ithread_set_name(char*n){(void)n;}
'''


def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)


def compilar():
    os.makedirs(OUT, exist_ok=True)
    trabajos = []
    for f in sorted(glob.glob(os.path.join(T, "libavc", "common", "*.c")) + glob.glob(os.path.join(T, "libavc", "decoder", "*.c"))):
        trabajos.append((f, os.path.join(OUT, "avc_" + os.path.basename(f)[:-2] + ".o"), ["-O2", "-w"] + AVC_INC))
    for d in ("common", "decoder", os.path.join("decoder", "drc_src"), os.path.join("decoder", "generic")):
        for f in sorted(glob.glob(os.path.join(T, "libxaac", d, "*.c"))):
            trabajos.append((f, os.path.join(OUT, "xa_" + os.path.basename(f)[:-2] + ".o"), ["-O2", "-w"] + XA_INC))
    estricto = ["-O2", "-g", "-Wall", "-Wextra", "-Werror"]
    trabajos.append((os.path.join(V, "vp_vdec.c"), os.path.join(OUT, "vp_vdec.o"), estricto + AVC_INC))
    trabajos.append((os.path.join(V, "vp_adec.c"), os.path.join(OUT, "vp_adec.o"), estricto + XA_INC))
    trabajos.append((os.path.join(V, "vp_core.c"), os.path.join(OUT, "vp_core.o"), estricto))
    hilos = os.path.join(OUT, "hilos.c")
    if not os.path.exists(hilos):
        with open(hilos, "w") as fh:
            fh.write(HILOS)
    trabajos.append((hilos, os.path.join(OUT, "hilos.o"), ["-O2"]))
    cab = max(os.path.getmtime(p) for p in glob.glob(os.path.join(V, "*.h")))

    def uno(t):
        src, obj, fl = t
        if os.path.exists(obj) and os.path.getmtime(obj) >= max(os.path.getmtime(src), cab if src.startswith(V) else 0):
            return None
        r = run([CC] + fl + ["-c", src, "-o", obj])
        return None if r.returncode == 0 else "%s:\n%s" % (src, r.stdout.decode("utf-8", "replace")[-1500:])
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as ex:
        errores = [e for e in ex.map(uno, trabajos) if e]
    if errores:
        print("[vplay_252] FALLO al compilar:\n" + "\n".join(errores[:3]))
        return None
    exe = os.path.join(OUT, "vplay_host")
    r = run([CC, "-O2", "-g", "-Wall", "-Wextra", "-Werror", os.path.join(RAIZ, "tests", "vplay_host_252.c")] + [t[1] for t in trabajos] + ["-lm", "-o", exe])
    if r.returncode != 0:
        print("[vplay_252] FALLO al enlazar:\n" + r.stdout.decode("utf-8", "replace")[-2500:])
        return None
    return exe


def videos():
    ff = shutil.which("ffmpeg")
    if not ff:
        return None
    enc = run([ff, "-hide_banner", "-encoders"]).stdout.decode("utf-8", "replace")
    if "libx264" not in enc or " aac " not in enc:
        return None
    base = [ff, "-nostdin", "-loglevel", "error", "-y"]
    out = {}
    recetas = {
        "yt18": ["-f", "lavfi", "-i", "testsrc2=size=640x360:rate=30:duration=14", "-f", "lavfi", "-i", "sine=frequency=330:duration=14:sample_rate=44100",
                 "-c:v", "libx264", "-profile:v", "baseline", "-g", "60", "-b:v", "500k", "-pix_fmt", "yuv420p", "-c:a", "aac", "-ac", "2", "-b:a", "96k", "-movflags", "+faststart"],
        "alto": ["-f", "lavfi", "-i", "testsrc2=size=854x480:rate=30:duration=12", "-f", "lavfi", "-i", "sine=frequency=440:duration=12:sample_rate=44100",
                 "-c:v", "libx264", "-profile:v", "high", "-bf", "2", "-g", "90", "-b:v", "900k", "-pix_fmt", "yuv420p", "-c:a", "aac", "-ac", "1", "-b:a", "64k"],
        "mudo": ["-f", "lavfi", "-i", "testsrc2=size=320x180:rate=25:duration=8", "-c:v", "libx264", "-profile:v", "main", "-g", "50", "-pix_fmt", "yuv420p", "-an", "-movflags", "+faststart"],
    }
    for nombre, r in recetas.items():
        p = os.path.join(OUT, nombre + ".mp4")
        if not os.path.exists(p):
            if run(base + r + [p]).returncode != 0:
                return None
        out[nombre] = p
    return out


def audio_253(exe, vs):
    """SOMA_253: modo solo audio: PCM 16 kHz mono; se compara con el que saca ffmpeg del mismo MP4 (mejor desfase, relacion senal/error)."""
    ff = shutil.which("ffmpeg")
    fallos = 0
    for nombre in ("yt18", "alto"):
        pcm = os.path.join(OUT, nombre + ".pcm")
        for guion, kib in (("audio", None), ("audio", "30"), ("audio5", None)):
            r = run([exe, vs[nombre], guion] + ([kib] if kib else ["0"]) + [pcm], timeout=600)
            txt = r.stdout.decode("utf-8", "replace").strip().splitlines()
            if r.returncode != 0:
                fallos += 1
                print("  FALLO %s/%s%s:\n    %s" % (nombre, guion, " " + kib if kib else "", "\n    ".join(txt[-6:])))
        r = run([exe, vs[nombre], "audio", "0", pcm], timeout=600)
        ref = os.path.join(OUT, nombre + "_ref.pcm")
        if ff and run([ff, "-nostdin", "-loglevel", "error", "-y", "-i", vs[nombre], "-vn", "-ac", "1", "-ar", "16000", "-f", "s16le", ref]).returncode == 0:
            import array
            a = array.array("h"); b = array.array("h")
            a.frombytes(open(pcm, "rb").read()); b.frombytes(open(ref, "rb").read())
            n = min(len(a), len(b)) - 4000
            mejor = None
            for lag in range(-1200, 1200, 4):                      # ffmpeg descarta el relleno inicial del codec: hay un desfase fijo
                num = 0; den = 0
                for i in range(2000, 2000 + 6000, 3):
                    x = a[i + lag] if 0 <= i + lag < len(a) else 0; y = b[i]
                    num += (x - y) * (x - y); den += y * y
                if mejor is None or num < mejor[0]: mejor = (num, den, lag)
            snr = 10 * __import__("math").log10(max(mejor[1], 1) / max(mejor[0], 1))
            print("[vplay_253] %s: %d muestras (ffmpeg %d), desfase %d, relacion senal/error %.1f dB" % (nombre, len(a), len(b), mejor[2], snr))
            if snr < 20 or abs(len(a) - len(b)) > 2400:
                fallos += 1
                print("  FALLO: el audio no coincide con el de ffmpeg")
    return fallos


def main():
    exe = compilar()
    if not exe:
        return 1
    vs = videos()
    if not vs:
        print("[vplay_252] compilado; sin ffmpeg (con libx264 y aac) no se generan videos: reproduccion NO probada")
        return 0
    fallos = 0
    casos = [("yt18", "normal", None), ("yt18", "saltos", None), ("yt18", "corte", None), ("yt18", "lento", "40"),
             ("alto", "normal", None), ("alto", "saltos", None), ("alto", "corte", None), ("alto", "lento", "70"),
             ("mudo", "normal", None), ("mudo", "saltos", None)]
    sumas = {}
    for nombre, guion, kib in casos:
        r = run([exe, vs[nombre], guion] + ([kib] if kib else []), timeout=600)
        txt = r.stdout.decode("utf-8", "replace").strip().splitlines()
        if r.returncode != 0:
            fallos += 1
            print("  FALLO %s/%s:\n    %s" % (nombre, guion, "\n    ".join(txt[-6:])))
        elif guion == "normal":
            sumas[nombre] = txt[-1]
    fallos += audio_253(exe, vs)
    # determinismo: dos pasadas iguales dan exactamente la misma imagen
    r2 = run([exe, vs["yt18"], "normal"], timeout=600).stdout.decode("utf-8", "replace").strip().splitlines()
    if not r2 or r2[-1] != sumas.get("yt18"):
        fallos += 1
        print("  FALLO: dos reproducciones del mismo video no dan la misma imagen")
    print("[vplay_252] %s (%d casos)" % ("OK" if not fallos else "%d fallos" % fallos, len(casos) + 1))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
