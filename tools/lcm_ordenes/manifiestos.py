"""SOMA_271_LCM: manifiestos. Cada comando, aplicación o capacidad nueva de SOMA se declara en un archivo de lcm/manifiestos/*.json
y el LCM lo aprende al reentrenar: sin tocar el catálogo ni el código del traductor.

    python3 -m tools.lcm_ordenes.manifiestos validar        comprueba los manifiestos (formato, comandos que existen, piezas)
    python3 -m tools.lcm_ordenes.manifiestos cobertura      qué comandos del núcleo conoce el LCM y cuáles no

Formato (todo en castellano, igual que el catálogo):
  {"manifiesto": 1, "origen": "cc", "valores": {...},                  (valores: ejemplos extra de una pieza para TODAS las órdenes; opcional)
   "ordenes": [{"id": "cc_compila", "orden": "cc {A}", "peligrosa": false,
                "frases":      ["(compila|compílame) {A}", ...],       patrones como en catalogo.py: (a|b) [opcional] {A}
                "infinitivos": ["compilar {A}"],                        opcional: "¿podrías compilar {A}?"
                "preguntas":   ["qué hace {A}"],                        opcional
                "valores_propios": {"A": ["hola.c"]},                   opcional: ejemplos de una pieza SOLO para esta orden (no ensucian a las demás)
                "pruebas":     [["compila hola.c", "cc hola.c"]]}]}     opcional pero recomendado: frases NUEVAS (no vistas al entrenar) y la orden exacta
                                                                        que debe salir; el reentrenamiento exige acertarlas para aplicarse                      opcional: "dime qué hace {A}"
Piezas: {A} archivo, {B} 2.º archivo, {D} carpeta, {N} número, {R} red wifi, {K} clave, {H} equipo, {U} web, {T} texto, {P} programa, {X} cosa."""
import json
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.abspath(os.path.join(AQUI, "..", ".."))
DIR = os.environ.get("LCM_MANIFIESTOS") or os.path.join(RAIZ, "lcm", "manifiestos")
MIN_FRASES = 6
# comandos que no son "ordenes" que el LCM deba traducir (internos, de depuración o que ya resuelve otro camino)
SIN_LCM = os.path.join(DIR, "_sin_lcm.json")


class ErrorManifiesto(Exception):
    pass


def comandos_nucleo(raiz=RAIZ):
    """Nombres de la tabla de comandos de src/agent/shell.c."""
    ruta = os.path.join(raiz, "src", "agent", "shell.c")
    if not os.path.exists(ruta):
        return None
    txt = open(ruta, encoding="utf-8", errors="replace").read()
    return set(re.findall(r'^\s*\{\s*"([A-Za-z0-9_.-]+)"\s*,\s*cmd_\w+\s*,\s*CMD_F_', txt, re.M))


def archivos():
    if not os.path.isdir(DIR):
        return []
    return sorted(os.path.join(DIR, f) for f in os.listdir(DIR) if f.endswith(".json") and not f.startswith("_"))


def _piezas(t):
    return set(re.findall(r"\{([A-Z])\}", t))


def carga(valores, ids_existentes, nucleo=None):
    """-> lista de ordenes validadas. Lanza ErrorManifiesto con TODOS los problemas."""
    errores, salida, vistos = [], [], set(ids_existentes)
    for f in archivos():
        n = os.path.basename(f)
        try:
            d = json.load(open(f, encoding="utf-8"))
        except Exception as e:
            errores.append(f"{n}: no es JSON válido ({e})"); continue
        if not isinstance(d, dict) or d.get("manifiesto") != 1 or not isinstance(d.get("ordenes"), list):
            errores.append(f"{n}: falta \"manifiesto\": 1 o la lista \"ordenes\""); continue
        for o in d["ordenes"]:
            i = o.get("id", "?")
            p = f"{n}/{i}"
            if not isinstance(i, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", i):
                errores.append(f"{p}: id no válido (minúsculas, cifras y _)"); continue
            if i in vistos:
                errores.append(f"{p}: el id ya existe (catálogo u otro manifiesto)"); continue
            vistos.add(i)
            orden = o.get("orden")
            if not isinstance(orden, str) or not orden.strip():
                errores.append(f"{p}: falta la orden"); continue
            if not isinstance(o.get("peligrosa"), bool):
                errores.append(f"{p}: \"peligrosa\" debe ser true o false (lo peligroso pide confirmación)"); continue
            nom = orden.split()[0]
            if nucleo is not None and nom not in nucleo:
                errores.append(f"{p}: el comando \"{nom}\" no existe en el núcleo"); continue
            pz = _piezas(orden)
            if not pz <= set(valores):
                errores.append(f"{p}: piezas desconocidas {sorted(pz - set(valores))}"); continue
            fr = o.get("frases")
            if not isinstance(fr, list) or len(fr) < MIN_FRASES or not all(isinstance(x, str) and x.strip() for x in fr):
                errores.append(f"{p}: hacen falta al menos {MIN_FRASES} frases (cuantas más y más variadas, mejor aprende)"); continue
            mal = [x for x in fr if not pz <= _piezas(x)]
            if mal:
                errores.append(f"{p}: frase sin todas las piezas de la orden {sorted(pz)}: «{mal[0]}»"); continue
            if any(x.count("(") != x.count(")") or x.count("[") != x.count("]") for x in fr):
                errores.append(f"{p}: paréntesis o corchetes sin cerrar"); continue
            ex = ([], [])
            for k, (campo) in enumerate(("infinitivos", "preguntas")):
                v = o.get(campo, [])
                if not isinstance(v, list) or not all(isinstance(x, str) for x in v) or any(not pz <= _piezas(x) for x in v):
                    errores.append(f"{p}: \"{campo}\" no válido (lista de textos con las mismas piezas)"); break
                ex[k].extend(v)
            else:
                pr = o.get("pruebas", [])
                if not isinstance(pr, list) or not all(isinstance(x, list) and len(x) == 2 and all(isinstance(y, str) and y.strip() for y in x) for x in pr):
                    errores.append(f"{p}: \"pruebas\" debe ser una lista de [frase, orden esperada]"); continue
                if any(x[1] != "LLM" and x[1].split()[0] not in (nucleo if nucleo is not None else {x[1].split()[0]}) for x in pr):
                    errores.append(f"{p}: una prueba espera un comando que no existe"); continue
                vp = o.get("valores_propios", {})
                if not isinstance(vp, dict) or not all(k in valores and isinstance(v, list) and v and all(isinstance(y, str) and y.strip() for y in v) for k, v in vp.items()):
                    errores.append(f"{p}: \"valores_propios\" debe ser {{pieza: [ejemplos]}} con piezas conocidas"); continue
                salida.append((i, orden, 1 if o["peligrosa"] else 0, list(fr), ex[0], ex[1], n, [tuple(x) for x in pr], vp))
    if errores:
        raise ErrorManifiesto("\n".join(errores))
    return salida


def aplica(g):
    """Añade los manifiestos al catálogo (g = globals() de catalogo.py)."""
    for f in archivos():                   # "valores": {"A": ["hola.c", ...]} amplía los ejemplos de una pieza existente
        try:
            v = json.load(open(f, encoding="utf-8")).get("valores", {})
        except Exception:
            continue                       # el error de formato lo da carga()
        for k, lst in (v.items() if isinstance(v, dict) else []):
            if k in g["VALORES"] and isinstance(lst, list):
                g["VALORES"][k] += [x for x in lst if isinstance(x, str) and x not in g["VALORES"][k]]
    for (i, orden, pel, fr, inf, pre, _n, _pr, vp) in carga(g["VALORES"], [x[0] for x in g["I"]], comandos_nucleo()):
        g["I"].append((i, orden, pel, fr))
        if inf or pre:
            g["EXTRA"][i] = (inf, pre)
        if vp:
            g.setdefault("PROPIOS", {})[i] = vp


def pruebas():
    """Frases de prueba que cada manifiesto trae (NUNCA se usan para entrenar): [(origen, frase, orden esperada)]. Son el examen de lo nuevo."""
    from . import catalogo
    out = []
    for (i, _o, _p, _f, _a, _b, n, pr, _vp) in carga(catalogo.VALORES, [], comandos_nucleo()):
        out += [(n, q, e) for q, e in pr]
    return out


def cobertura():
    from . import catalogo
    nucleo = comandos_nucleo()
    if nucleo is None:
        return None
    ordenes = {x[1].split()[0] for x in catalogo.I if x[0] != "LLM"}
    excl = set()
    if os.path.exists(SIN_LCM):
        excl = set(json.load(open(SIN_LCM, encoding="utf-8")).get("comandos", []))
    return {"nucleo": sorted(nucleo), "cubiertos": sorted(nucleo & ordenes), "excluidos": sorted((nucleo - ordenes) & excl),
            "sin_cubrir": sorted(nucleo - ordenes - excl), "fantasma": sorted(ordenes - nucleo)}


def main():
    a = sys.argv[1] if len(sys.argv) > 1 else "validar"
    if a == "validar":
        try:
            from . import catalogo
            n = len(archivos())
        except ErrorManifiesto as e:
            print("MANIFIESTOS NO VÁLIDOS:\n" + str(e)); sys.exit(1)
        print(f"manifiestos válidos: {n} archivos, catálogo de {len(catalogo.I)} órdenes")
    elif a == "cobertura":
        c = cobertura()
        if c is None:
            sys.exit("no encuentro src/agent/shell.c")
        print(f"comandos del núcleo: {len(c['nucleo'])} | que el LCM sabe pedir: {len(c['cubiertos'])} | excluidos a propósito: {len(c['excluidos'])}")
        print("SIN CUBRIR (el LCM no los conoce; declaralos en un manifiesto o ponlos en lcm/manifiestos/_sin_lcm.json):")
        print("  " + (" ".join(c["sin_cubrir"]) or "(ninguno)"))
        if c["fantasma"]:
            print("En el catálogo pero NO en el núcleo (órdenes que fallarían):", " ".join(c["fantasma"]))
            sys.exit(1)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
