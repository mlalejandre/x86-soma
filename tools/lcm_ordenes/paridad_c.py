"""¿El C de SOMA da lo mismo que el modelo de Python?  python3 -m tools.lcm_ordenes.paridad_c modelo.bin lcm_host
Compara, frase a frase (exámenes, sus variantes y las frases de UD): estado, orden y probabilidad."""
import random
import subprocess
import sys

from . import exhaustivo as X
from .traduce import traduce, UMBRAL


def estado_py(r):
    if r["orden"]:
        return 0
    if r["intent"] == "LLM":
        return 1
    m = r["motivo"]
    return 3 if m.startswith("falta") else 4 if ("caracteres" in m or "espacios" in m) else 2


def frases():
    rnd = random.Random(7)
    todos = [q for E in X.examenes().values() for q, _ in E]
    var = [X.sin_tildes(q) for q in todos] + [q[:1].upper() + q[1:] for q in todos]
    var += [x for q in todos for x in [X.errata(q, "x", rnd)] if x]
    raras = ["", "   ", "¿¿??", "rm -rf / ; reboot", "escribe hola; reboot en a.txt", "borra `x`.txt", "ping 1.1.1.1 && reboot",
             "a" * 3000, "ñandú ÑANDÚ über Ärger", "😀 borra el archivo x.txt", "\x01\x02 lista archivos", "abre http://x.y/a?b=c&d=e"]
    return todos + var + X.frases_ud() + raras


if __name__ == "__main__":
    modelo, host = sys.argv[1], sys.argv[2]
    fs = [f.replace("\n", " ") for f in frases()]
    out = subprocess.run([host, modelo], input="\n".join(fs) + "\n", capture_output=True, text=True, errors="replace").stdout.split("\n")
    dif_est = dif_ord = dif_p = 0; ejemplos = []
    peor = 0
    for f, ln in zip(fs, out):
        est, it, pm, conf, orden = (ln.split("\t") + [""] * 5)[:5]
        r = traduce(f)
        e = estado_py(r)
        o = r["orden"] or (r["motivo"].split()[1].rstrip(":") if e == 3 else "")
        d = abs(int(pm) / 1000 - r["p"])
        peor = max(peor, d)
        if int(est) != e:
            dif_est += 1; ejemplos.append(("estado", f[:70], e, est, r["p"], pm))
        elif e == 0 and orden != r["orden"]:
            dif_ord += 1; ejemplos.append(("orden", f[:70], r["orden"], orden))
        if d > 0.02:
            dif_p += 1
    n = len(fs)
    print(f"{n} frases | estado distinto: {dif_est} ({100 * dif_est / n:.2f} %) | orden distinta: {dif_ord} | "
          f"probabilidad desviada >0,02: {dif_p} | desviación máxima {peor:.3f}")
    for e in ejemplos[:15]:
        print("   ", e)
