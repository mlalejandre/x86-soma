"""Pruebas exhaustivas del traductor de órdenes.  python3 -m tools.lcm_ordenes.exhaustivo [lcm|mixto] [--sin-ud]

  1. Cada examen tal cual (1: el primero, 2: escrito tras ver fallos de v1, 3: ciego, escrito por otro agente si existe).
  2. Robustez: los mismos casos sin tildes, con la primera letra en mayúscula, con una errata en una palabra que no es pieza,
     y con cortesías delante y detrás.
  3. Frases que no son órdenes, a miles: las de prueba de UD (noticias, foros, textos), que nunca se vieron al entrenar.
     Se cuenta cuántas acabarían ejecutando algo, y cuántas algo peligroso.
  «mixto» es lo que se instalará en SOMA: el LCM primero; si no está seguro, el mayordomo de siempre; si dice LLM con
  seguridad, al LLM (sin pasar por las reglas del mayordomo)."""
import glob
import os
import random
import re
import runpy
import sys
import unicodedata

from .mide import juzga, PELIGROSAS

AQUI = os.path.dirname(os.path.abspath(__file__))


def examenes():
    ex = {"1": runpy.run_path(os.path.join(AQUI, "examen.py"))["E"], "2": runpy.run_path(os.path.join(AQUI, "examen2.py"))["E"]}
    p3 = os.path.join(AQUI, "examen3.py")
    if os.path.exists(p3):
        ex["3 (ciego, ya visto)"] = runpy.run_path(p3)["E"]
    p4 = os.path.join(AQUI, "examen4.py")
    if os.path.exists(p4):
        ex["4 (ciego, ya visto)"] = runpy.run_path(p4)["E"]
    if "--estres" in sys.argv:
        ex["estres (desarrollo)"] = runpy.run_path(os.path.join(AQUI, "estres.py"))["E"]
    if "--examen5" in sys.argv:
        ex["5 CIEGO"] = runpy.run_path(os.path.join(AQUI, "examen5.py"))["E"]
    return ex


def traductor(modo):
    from .traduce import traduce, UMBRAL
    if modo == "lcm":
        return traduce
    if modo == "soma":
        # lo que se instala: una orden escrita tal cual (orden de SOMA y como mucho un argumento) se respeta; el resto, el LCM
        import json
        nombres = {o["nombre"] for o in json.load(open(os.path.join(AQUI, "soma_ordenes.json")))["ordenes"]}

        charla = {"hola", "buenas", "buenos dias", "buenas tardes", "buenas noches", "gracias", "muchas gracias", "vale gracias", "vale", "ok", "adios", "hasta luego",
                  "que tal", "como estas", "quien eres", "que sabes hacer", "bien", "genial", "perfecto", "de acuerdo"}

        def soma(q):
            f = re.sub(r"[^\w ]", "", q.lower().translate(str.maketrans("áéíóúü", "aeiouu"))).strip()
            if f in charla:                       # en SOMA la charla la contesta el LCM antes de la red: no ejecuta nada
                return {"orden": None, "confirmar": False, "p": 1.0}
            w = q.split()[0].lower() if q.split() else ""
            if w in nombres and len(q.split()) <= 2:
                return {"orden": q.strip(), "confirmar": w in PELIGROSAS, "p": 1.0}
            return traduce(q)
        return soma
    from .mayordomo_base import mayordomo
    if modo == "base":
        def base(q):
            o = mayordomo(q)
            return {"orden": None if o.startswith("NADA") else o, "confirmar": False}
        return base

    def mixto(q):
        r = traduce(q)
        if r["orden"] or (r["intent"] == "LLM" and r["p"] >= UMBRAL) or r["motivo"].startswith("falta"):
            return r
        o = mayordomo(q)
        return {"orden": None if o.startswith("NADA") else o, "confirmar": False, "p": r["p"]}
    return mixto


def sin_tildes(s):
    return "".join(c for c in unicodedata.normalize("NFD", s) if unicodedata.category(c) != "Mn").replace("¿", "").replace("¡", "")


def errata(q, esp, rnd):
    piezas = set(w.lower() for e in esp.split("||") for w in re.split(r"[\s\"]+", e))
    ws = q.split()
    cand = [i for i, w in enumerate(ws) if len(w) >= 5 and w.isalpha() and w.lower() not in piezas]
    if not cand:
        return None
    i = rnd.choice(cand); w = ws[i]; j = rnd.randrange(1, len(w) - 2)
    op = rnd.choice(["cambia", "quita", "dobla"])
    w = (w[:j] + w[j + 1] + w[j] + w[j + 2:]) if op == "cambia" else (w[:j] + w[j + 1:]) if op == "quita" else (w[:j] + w[j] + w[j:])
    ws[i] = w
    return " ".join(ws)


def cuenta(f, casos):
    c = {"bien": 0, "cede": 0, "MAL": 0, "n": 0, "ord": 0, "ord_bien": 0, "pelig_sin": 0, "mal_sin": 0}
    malos = []
    for q, esp in casos:
        r = f(q); k = juzga(esp, r)
        c[k] += 1; c["n"] += 1
        if esp != "LLM":
            c["ord"] += 1; c["ord_bien"] += k == "bien"
        o = r.get("orden")
        if o and o.split()[0] in PELIGROSAS and not r.get("confirmar"):
            c["pelig_sin"] += 1
        if k == "MAL":
            malos.append((q, esp, o))
            if o and not r.get("confirmar"):
                c["mal_sin"] += 1
    return c, malos


def linea(nombre, c):
    return (f"{nombre:28s} {c['n']:5d} casos | órdenes exactas {c['ord_bien']:4d}/{c['ord']:<4d} ({100 * c['ord_bien'] / max(1, c['ord']):5.1f} %) | "
            f"total bien {100 * c['bien'] / c['n']:5.1f} % | cede {c['cede']:3d} | MAL {c['MAL']:3d} ({100 * c['MAL'] / c['n']:.2f} %), sin preguntar {c['mal_sin']:3d} ({100 * c['mal_sin'] / c['n']:.2f} %) | peligrosa sin confirmar {c['pelig_sin']}")


def frases_ud():
    raiz = os.environ.get("LCM_UD") or os.path.join(AQUI, "datos", "ud")
    out = []
    for f in sorted(glob.glob(os.path.join(raiz, "**", "*test*.conllu"), recursive=True)):
        for ln in open(f, encoding="utf-8"):
            if ln.startswith("# text = "):
                t = ln[9:].strip()
                if 3 <= len(t.split()) <= 25:
                    out.append(t)
    return sorted(set(out))


if __name__ == "__main__":
    modo = next((a for a in sys.argv[1:] if not a.startswith("-")), "lcm")
    f = traductor(modo)
    rnd = random.Random(2026)
    todos = []; todos_malos = []
    print(f"== {modo} ==")
    for nom, E in examenes().items():
        c, m = cuenta(f, E); todos += E; todos_malos += m
        print(linea("examen " + nom, c))
    variantes = {
        "sin tildes ni ¿¡": [(sin_tildes(q), e if e == "LLM" else "||".join(sin_tildes(x) for x in e.split("||"))) for q, e in todos],
        "Mayúscula inicial": [(q[:1].upper() + q[1:], e) for q, e in todos],
        "con una errata": [(x, e) for q, e in todos for x in [errata(q, e, rnd)] if x],
        "con cortesías": [(rnd.choice(["oye, ", "por favor ", "me puedes ", "Soma, "]) + q + rnd.choice([" por favor", " gracias", "", " ya"]), e) for q, e in todos],
    }
    tot = {"n": 0, "MAL": 0, "pelig_sin": 0}
    c, _ = cuenta(f, todos); print(linea("TODOS LOS EXÁMENES", c))
    for nom, casos in variantes.items():
        c, m = cuenta(f, casos); todos_malos += m
        print(linea(nom, c))
    if "--sin-ud" not in sys.argv:
        ud = frases_ud()
        ej = [(t, f(t)) for t in ud]
        n_ej = [(t, r["orden"]) for t, r in ej if r.get("orden")]
        n_pel = [(t, o) for t, o in n_ej if o.split()[0] in PELIGROSAS]
        n_pel_sin = [(t, r["orden"]) for t, r in ej if r.get("orden") and r["orden"].split()[0] in PELIGROSAS and not r.get("confirmar")]
        print(f"{'frases UD (no son órdenes)':28s} {len(ud):5d} frases | ejecutarían algo: {len(n_ej)} ({100 * len(n_ej) / len(ud):.2f} %) | "
              f"algo peligroso (con confirmación): {len(n_pel)} | peligroso SIN confirmar: {len(n_pel_sin)}")
        for t, o in n_ej[:12]:
            print(f"      UD → {o:28s} | {t[:80]}")
    if "-v" in sys.argv:
        for q, e, o in todos_malos:
            print(f"   MAL | {q[:60]:60s} | esperada {e[:30]:30s} | da {o}")
