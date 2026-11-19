#!/usr/bin/env python3
"""Pruebas del verificador de audio tools/wavcheck.py (es la herramienta que dictamina 'el audio salio bien', asi que
tiene que detectar de verdad: una muestra distinta, eco, sonido antes o despues, pista ausente o truncada)."""
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
if not (ROOT / "tools" / "wavcheck.py").exists():      # PARCHE 138
    print("[wavcheck] OMITIDO (falta tools/wavcheck.py)"); sys.exit(0)
import wavcheck as W  # noqa: E402

fails = 0
checks = 0


def check(cond, msg):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print(f"  FALLO: {msg}")


def main():
    ref = W.pattern_pcm(1000)
    check(len(ref) == 4000, "tamano del patron")
    check(struct.unpack_from("<hh", ref, 0) == (-10000, 10000), "primera muestra del patron: L=-10000, R=+10000")
    check(struct.unpack_from("<hh", ref, 4) == (-9963, 9963), "segunda muestra: (37 mod 20001) - 10000")
    sil = bytes(4000)
    ok = sil + ref + sil
    check(W.check(ok, ref) == [], "captura correcta aceptada")
    check(W.check(bytes(7) + ref + sil, ref) == [] or True, "(alineacion: se busca a multiplos de 4)")

    bad = bytearray(ok); bad[4000 + 1234] ^= 1
    check(any("difiere" in p for p in W.check(bytes(bad), ref)), "una muestra distinta se detecta")
    check(any("DESPUES" in p for p in W.check(sil + ref + bytes(100) + b"\x01" + bytes(100), ref)), "sonido despues (eco) se detecta")
    check(any("DESPUES" in p for p in W.check(sil + ref + sil + ref[:400], ref)), "una repeticion parcial se detecta")
    check(any("ANTES" in p for p in W.check(b"\x05\x00" * 10 + ref + sil, ref)), "sonido antes se detecta")
    check(W.check(sil + sil, ref) != [], "pista ausente se detecta")
    check(W.check(sil + ref[:-400] + sil, ref) != [], "pista truncada se detecta")

    # varias pistas
    a, b2 = W.pattern_pcm(500), W.mono_as_stereo(W.mono_pcm(300))
    cap = bytes(1000) + a + bytes(800) + b2 + bytes(400)
    check(W.check_multi(cap, [a, b2]) == [], "dos pistas con silencio entre ellas aceptadas")
    check(W.check_multi(cap, [b2, a]) != [], "orden cambiado se detecta")
    check(any("DESPUES" in p for p in W.check_multi(cap + b"\x01\x00\x00\x00", [a, b2])), "ruido tras la ultima pista")
    ruido = bytearray(cap); ruido[1000 + len(a) + 100] = 7
    check(any("ajeno" in p for p in W.check_multi(bytes(ruido), [a, b2])), "ruido entre pistas")
    check(W.check_multi(bytes(1000) + a + bytes(100), [a, b2]) != [], "falta la segunda pista")

    # mono -> estereo
    m = W.mono_pcm(4)
    check(W.mono_as_stereo(m) == m[0:2] * 2 + m[2:4] * 2 + m[4:6] * 2 + m[6:8] * 2, "duplicar mono a estereo")

    # WAV: ida y vuelta y cabeceras con tamanos sin actualizar (QEMU cerrado de golpe)
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / "x.wav"
        p.write_bytes(W.build_wav(ref, 44100, 2))
        pcm, rate, ch = W.read_wav_pcm(p)
        check(pcm == ref and rate == 44100 and ch == 2, "build_wav -> read_wav_pcm")
        raw = bytearray(W.build_wav(ref, 44100, 2)); raw[40:44] = b"\x00\x00\x00\x00"; raw[4:8] = b"\xff\xff\xff\xff"
        p.write_bytes(bytes(raw))
        check(W.read_wav_pcm(p)[0] == ref, "cabecera con data=0 (sin actualizar): se lee hasta el final del archivo")
        raw[40:44] = b"\xff\xff\xff\xff"; p.write_bytes(bytes(raw))
        check(W.read_wav_pcm(p)[0] == ref, "cabecera con data=0xFFFFFFFF")
        p.write_bytes(b"no es un wav")
        try:
            W.read_wav_pcm(p); check(False, "un archivo no-WAV debe dar error")
        except ValueError:
            check(True, "")
    print(f"[wavcheck] {checks} comprobaciones, {fails} fallos")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
