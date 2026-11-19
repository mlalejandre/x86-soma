#!/usr/bin/env python3
"""Pruebas de los verificadores del parche 021: tools/fpcheck.py (resultado esperado de ssetest) y tools/fbcheck.py."""
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
if not (ROOT / "tools" / "fbcheck.py").exists() or not (ROOT / "tools" / "fpcheck.py").exists():   # PARCHE 138
    print("[fpcheck+fbcheck] OMITIDO (faltan tools/fbcheck.py o tools/fpcheck.py)"); sys.exit(0)
import fbcheck as C  # noqa: E402
import fpcheck as F  # noqa: E402

fails = 0
checks = 0


def check(cond, msg):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print(f"  FALLO: {msg}")


def dbl(bits): return struct.unpack("<d", struct.pack("<Q", bits))[0]


def main():
    # emulador de redondeo == aritmetica nativa en el modo al mas cercano
    for n in (1, 2, 10, 1000, 20000):
        check(F.series_bits(0, n) == F.series_exact_nearest(n), f"emulador nearest == nativo (N={n})")
    # orden: abajo <= cercano <= arriba, y todos proximos a pi^2/6
    for n in (100, 5000):
        d, c, u = dbl(F.series_bits(2, n)), dbl(F.series_bits(0, n)), dbl(F.series_bits(1, n))
        check(d <= c <= u, f"orden de redondeos (N={n})")
        check(abs(c - 1.6449340668482264) < 1.0 / n, f"cerca de pi^2/6 (N={n})")
    check(F.series_bits(1, 5000) != F.series_bits(2, 5000), "arriba y abajo difieren")
    check(F.series_bits(0, 1) == struct.unpack("<Q", struct.pack("<d", 1.0))[0], "N=1 -> 1.0")

    # fbcheck
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        w, h = 320, 200
        exp = bytes(C.expected_frame(w, h))
        ppm = lambda data, ww=w, hh=h: f"P6\n{ww} {hh}\n255\n".encode() + data
        (td / "ok.ppm").write_bytes(ppm(exp))
        check(C.check(td / "ok.ppm", w, h) == [], "captura correcta aceptada")
        bad = bytearray(exp); bad[(10 * w + 20) * 3 + 1] ^= 4
        (td / "bad.ppm").write_bytes(ppm(bytes(bad)))
        r = C.check(td / "bad.ppm", w, h)
        check(len(r) == 1 and "1 pixeles distintos" in r[0] and "(20, 10)" in r[0], "un pixel distinto se detecta y se localiza")
        (td / "size.ppm").write_bytes(ppm(exp, w, h))
        check(C.check(td / "size.ppm", 640, 480) != [], "tamano equivocado")
        norect = bytearray(exp)
        for y in range(50, 150): norect[(y * w + 100) * 3:(y * w + 300) * 3] = bytes(600)
        (td / "norect.ppm").write_bytes(ppm(bytes(norect)))
        check(C.check(td / "norect.ppm", w, h) != [], "rectangulo ausente")
        (td / "trunc.ppm").write_bytes(ppm(exp)[:-10])
        try:
            C.read_ppm(td / "trunc.ppm"); check(False, "PPM truncado debe fallar")
        except ValueError:
            check(True, "")
        # el patron: esquinas y rectangulo
        check(exp[0:3] == b"\x00\x00\x00", "pixel (0,0) = (0,0,0)")
        last = ((h - 1) * w + (w - 1)) * 3
        check(tuple(exp[last:last + 3]) == (255, 255, (w - 1) ^ (h - 1) & 255 if False else ((w - 1) ^ (h - 1)) & 255), "pixel de la esquina opuesta")
        check(tuple(exp[(60 * w + 150) * 3:(60 * w + 150) * 3 + 3]) == (255, 128, 0), "dentro del rectangulo")
    print(f"[fpcheck+fbcheck] {checks} comprobaciones, {fails} fallos")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
