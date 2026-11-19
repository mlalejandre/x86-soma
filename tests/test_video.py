#!/usr/bin/env python3
"""SOMA 022: pruebas de video en host. Genera JPEG y AVI de referencia (tools/jpegref.py con PIL/libjpeg y tools/mkavi.py)
y ejecuta build/tests/test_jpeg y build/tests/test_avi sobre ellos. Sin PIL (pip install pillow) se omite con aviso."""
import subprocess, sys, tempfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent
try:
    import PIL  # noqa: F401
except ImportError:
    print("video: OMITIDO (falta Pillow: pip install pillow)")
    sys.exit(0)

# PARCHE 138: sin las herramientas de referencia (tools/jpegref.py, tools/mkavi.py) se omite con aviso en vez de romper 'make test'
_root = Path(__file__).resolve().parent.parent
_falta = [t for t in ("jpegref.py", "mkavi.py") if not (_root / "tools" / t).exists()]
if _falta:
    print("video: OMITIDO (faltan " + ", ".join("tools/" + t for t in _falta) + ")")
    sys.exit(0)

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode:
        sys.stderr.write(r.stderr)
        print("video: FALLO en", " ".join(map(str, cmd[:3])))
        sys.exit(1)

with tempfile.TemporaryDirectory(prefix="soma_video_") as td:
    td = Path(td)
    (td / "jpg").mkdir()
    run([sys.executable, str(root / "tools" / "jpegref.py"), str(td / "jpg")])
    run([str(root / "build" / "tests" / "test_jpeg"), str(td / "jpg")])
    variants = [("a", ["--frames", "10"]), ("b", ["--no-audio", "--frames", "7"]), ("c", ["--rec", "--junk", "--frames", "12"]),
                ("d", ["--audio-first", "--channels", "1", "--rate", "22050", "--frames", "9", "--w", "64", "--h", "48", "--sub", "0"]),
                ("e", ["--frames", "5", "--w", "33", "--h", "17", "--sub", "1"])]
    avis = []
    for n, args in variants:
        out = td / f"{n}.avi"
        run([sys.executable, str(root / "tools" / "mkavi.py"), str(out), *args])
        avis.append(str(out))
    run([str(root / "build" / "tests" / "test_avi"), *avis])

# --- AVI reales hechos por ffmpeg (si esta instalado): imagen vs la decodificacion de ffmpeg, audio byte a byte ---
import shutil
if shutil.which("ffmpeg"):
    try:
        import numpy as np
    except ImportError:
        np = None
    if np is not None:
        dump = td_root = None
        with tempfile.TemporaryDirectory(prefix="soma_ffavi_") as td:
            td = Path(td)
            r = subprocess.run(["cc", "-O1", "-o", str(td / "avidump"), str(root / "tests" / "avidump.c"), str(root / "src" / "core" / "avi.c"),
                                str(root / "src" / "core" / "jpeg.c")], capture_output=True, text=True)
            if r.returncode:
                print(r.stderr); print("video: FALLO compilando avidump"); sys.exit(1)
            for pix, size in (("yuvj420p", "320x240"), ("yuvj422p", "200x130"), ("yuvj444p", "96x72"), ("yuvj440p", "120x90"), ("yuvj420p", "333x201")):
                avi = td / f"ff_{pix}_{size}.avi"
                r = subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", f"testsrc2=size={size}:rate=25:duration=0.8",
                                    "-f", "lavfi", "-i", "sine=frequency=500:sample_rate=44100:duration=0.8", "-c:v", "mjpeg", "-q:v", "4",
                                    "-pix_fmt", pix, "-c:a", "pcm_s16le", "-ac", "2", str(avi)], capture_output=True, text=True)
                if r.returncode:
                    print("video: ffmpeg no pudo crear", avi.name, "(se omite):", r.stderr.strip()[:120]); continue
                r = subprocess.run([str(td / "avidump"), str(avi), str(td / "o")], capture_output=True, text=True)
                if r.returncode:
                    print(r.stderr); print("video: FALLO decodificando", avi.name); sys.exit(1)
                subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(avi), "-f", "rawvideo", "-pix_fmt", "rgb24", str(td / "r.rgb"),
                                "-f", "s16le", "-ac", "2", str(td / "r.pcm")], check=True)
                a = np.fromfile(td / "o.rgb", np.uint8).astype(int); b = np.fromfile(td / "r.rgb", np.uint8).astype(int)
                if a.shape != b.shape:
                    print(f"video: FALLO {avi.name}: {a.size} bytes de imagen contra {b.size} de ffmpeg"); sys.exit(1)
                d = abs(a - b)
                if d.mean() > 1.0 or d.max() > 12:
                    print(f"video: FALLO {avi.name}: diferencia media {d.mean():.2f}, maxima {d.max()} frente a ffmpeg"); sys.exit(1)
                if (td / "o.pcm").read_bytes() != (td / "r.pcm").read_bytes():
                    print(f"video: FALLO {avi.name}: el audio no coincide con el de ffmpeg"); sys.exit(1)
                print(f"video: ffmpeg {pix} {size}: {a.size // (int(size.split('x')[0]) * int(size.split('x')[1]) * 3)} fotogramas, dif media {d.mean():.2f}, max {d.max()}, audio exacto")
    else:
        print("video: (ffmpeg presente pero sin numpy: se omite la comparacion con ffmpeg)")
