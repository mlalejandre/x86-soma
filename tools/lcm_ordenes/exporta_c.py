"""Convierte el traductor entrenado (traductor.pt) en el bloque de bytes que lee el C de SOMA (lcm_ordenes.inc).

  python3 -m tools.lcm_ordenes.exporta_c [salida.bin] [--inc lcm_ordenes_pesos.inc]

Pesos int8 por fila (real = q * mult / 2^24), sesgos int32 en Q12, tablas de tanh (Q12) y exp (Q20) generadas aquí, con
coma flotante, para que el núcleo no la necesite."""
import math
import os
import struct
import sys

from .traduce import RUTA, ORDEN, PELIGROSA, UMBRAL

Q = 12


def _t8(W):
    """W: lista de filas (floats) -> bytes"""
    filas, cols = len(W), len(W[0])
    mult, qs = [], bytearray()
    for f in W:
        mx = max(abs(x) for x in f) or 1e-8
        s = mx / 127.0
        m = int(round(s * (1 << 24)))
        if m < 1:
            m = 1
        s = m / float(1 << 24)
        mult.append(m)
        qs += bytes((max(-127, min(127, int(round(x / s)))) & 0xFF) for x in f)
    return struct.pack("<II", filas, cols) + b"".join(struct.pack("<I", m) for m in mult) + bytes(qs)


def _v32(v):
    return struct.pack("<I", len(v)) + b"".join(struct.pack("<i", int(round(x * (1 << Q)))) for x in v)


def bloque(ruta=RUTA):
    """Un modelo, o varios separados por ':' (conjunto: contenedor LCMS; el C promedia sus probabilidades)."""
    rutas = ruta.split(":")
    if len(rutas) > 1:
        partes = [bloque(r) for r in rutas]
        return b"LCMS" + struct.pack("<I", len(partes)) + b"".join(struct.pack("<I", len(x)) + x for x in partes)
    import torch
    ck = torch.load(ruta, map_location="cpu")
    sd = {k: v.float().tolist() for k, v in ck["modelo"].items()}
    voc = ck["voc"]; intents = ck["intents"]; tags = ck["tags"]
    dw = len(sd["ew.weight"][0]); db = len(sd["eb.0.weight"][0]); nb = len(sd["eb.0.weight"])
    h = len(sd["lstm.weight_hh_l0"][0]); inn = len(sd["lstm.weight_ih_l0"][0])
    out = bytearray(b"LCMO" + struct.pack("<I", 1))
    ntanh, nexp = 16 * 128 + 1, 16 * 64 + 1
    out += struct.pack("<11I", len(voc), dw, nb, db, h, inn, len(intents), len(tags), int(round(UMBRAL * 1000)), ntanh, nexp)
    ent = sorted(((w.encode("utf-8"), i) for w, i in voc.items()), key=lambda x: x[0])
    cuerpo = bytearray(); offs = []
    for w, i in ent:
        assert len(w) < 256
        offs.append(len(cuerpo)); cuerpo += struct.pack("<IB", i, len(w)) + w
    out += b"".join(struct.pack("<I", o) for o in offs) + cuerpo
    for it in intents:
        n = it.encode(); o = ORDEN.get(it, "").encode()
        out += bytes([len(n)]) + n + bytes([len(o)]) + o + bytes([1 if PELIGROSA.get(it) else 0])
    for t in tags:
        b = t.encode(); out += bytes([len(b)]) + b
    out += _t8(sd["ew.weight"])
    for i in range(4):
        out += _t8(sd[f"eb.{i}.weight"])
    for suf in ("", "_reverse"):
        out += _t8(sd["lstm.weight_ih_l0" + suf]) + _t8(sd["lstm.weight_hh_l0" + suf])
        out += _v32([a + b for a, b in zip(sd["lstm.bias_ih_l0" + suf], sd["lstm.bias_hh_l0" + suf])])
    out += _t8(sd["intent.weight"]) + _v32(sd["intent.bias"])
    out += _t8(sd["tag.weight"]) + _v32(sd["tag.bias"])
    out += b"".join(struct.pack("<i", int(round(math.tanh(-8 + k / 128.0) * (1 << Q)))) for k in range(ntanh))
    out += b"".join(struct.pack("<i", int(round(math.exp(-16 + k / 64.0) * (1 << 20)))) for k in range(nexp))
    return bytes(out)


def como_inc(b, nombre="lcm_ordenes_blob"):
    lin = [f"/* SOMA_253_LCM: modelo del traductor (generado por tools/lcm_ordenes/exporta_c.py; no editar). {len(b)} bytes. */",
           f"static const unsigned char {nombre}[{len(b)}] __attribute__((aligned(8))) = {{"]
    for i in range(0, len(b), 32):
        lin.append(",".join(str(x) for x in b[i:i + 32]) + ",")
    lin.append("};")
    return "\n".join(lin) + "\n"


if __name__ == "__main__":
    b = bloque()
    sal = next((a for a in sys.argv[1:] if not a.startswith("-")), os.path.join(os.path.dirname(RUTA), "traductor.bin"))
    open(sal, "wb").write(b)
    if "--inc" in sys.argv:
        open(sys.argv[sys.argv.index("--inc") + 1], "w").write(como_inc(b))
    print(f"{sal}: {len(b)} bytes")
