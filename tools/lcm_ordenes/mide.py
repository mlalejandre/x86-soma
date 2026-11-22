"""Mide un traductor de peticiones a órdenes con el examen. python3 -m tools.lcm_ordenes.mide [base|lcm] [-v]

Cuenta, por caso:
  · bien:            la orden es la esperada (y, si es peligrosa, pide confirmación) / o pasa al LLM cuando toca
  · cede:            no ejecuta nada y pasa al LLM (o pregunta) cuando había una orden: no es un error, pero no ayuda
  · MAL ejecutada:   ejecutaría una orden distinta de la esperada (o una peligrosa sin confirmar). Es lo grave.
"""
import re, sys
import sys as _s
if "--examen5" in _s.argv:
    from .examen5 import E
elif "--examen4" in _s.argv:
    from .examen4 import E
elif "--examen2" in _s.argv:
    from .examen2 import E
elif "--examen3" in _s.argv:
    from .examen3 import E
else:
    from .examen import E

PELIGROSAS = ("rm", "rmdir", "kill", "reboot", "poweroff", "shutdown", "halt", "fs-format", "rollback", "sector_write")


def norm(o):
    o = o.strip().replace("“", '"').replace("”", '"')
    o = re.sub(r"\s+", " ", o)
    o = re.sub(r"(?<=\s)/workspace/", "", o)
    return o.lower()


def juzga(esperada, r):
    """r = {"orden": str|None, "confirmar": bool}. Orden None = no ejecuta (cede al LLM o pregunta)."""
    o = r.get("orden")
    if esperada == "LLM":
        return "bien" if not o else "MAL"
    if not o:
        return "cede"
    if norm(o) in {norm(x) for x in esperada.split("||")}:
        if o.split()[0] in PELIGROSAS and not r.get("confirmar"):
            return "MAL"
        return "bien"
    return "MAL"


def traductor(nombre):
    if nombre == "base":
        from .mayordomo_base import mayordomo
        def f(q):
            o = mayordomo(q)
            return {"orden": None if o.startswith("NADA") else o, "confirmar": False}
        return f
    from .traduce import traduce
    return traduce


if __name__ == "__main__":
    nom = next((a for a in sys.argv[1:] if not a.startswith("-")), "lcm")
    f = traductor(nom)
    c = {"bien": 0, "cede": 0, "MAL": 0}
    cl = {"bien": 0, "cede": 0, "MAL": 0}
    for q, esp in E:
        r = f(q)
        k = juzga(esp, r)
        c[k] += 1
        if esp == "LLM":
            cl[k] += 1
        if "-v" in sys.argv and k != "bien":
            print(f"   {k:5s} | {q[:52]:52s} | esperada: {esp[:34]:34s} | da: {r.get('orden')}" + (f"  (p={r['p']:.2f})" if "p" in r else ""))
    n = len(E); nl = sum(1 for _, e in E if e == "LLM")
    print(f"{nom}: órdenes {c['bien'] - cl['bien']}/{n - nl} bien, {c['cede']} cede, {c['MAL'] - cl['MAL']} mal ejecutadas | "
          f"no-órdenes {cl['bien']}/{nl} bien al LLM | TOTAL {c['bien']}/{n} ({100 * c['bien'] / n:.1f} %), MAL {c['MAL']}")
