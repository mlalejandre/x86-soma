"""SOMA_274_JS: el manifiesto del LCM de la orden js (lcm/manifiestos/js.json): formato, que sus ordenes existan en la tabla de comandos
del nucleo y que cada prueba retenida use una orden que `js` entiende (js archivo.js | js -c texto).
    python3 tests/test_js_manifiesto_274.py"""
import json, os, re, sys
RAIZ = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
fallos = 0; n = 0
def ck(c, m):
    global fallos, n
    n += 1
    if not c:
        fallos += 1; print("FALLO:", m)
man = json.load(open(os.path.join(RAIZ, "lcm", "manifiestos", "js.json"), encoding="utf-8"))
sh = open(os.path.join(RAIZ, "src", "agent", "shell.c"), encoding="utf-8").read()
ck(re.search(r'\{\s*"js"\s*,\s*cmd_js\s*,\s*CMD_F_NORMAL', sh) is not None, "la orden js esta en la tabla de comandos")
ck('#include "shell_js.c"' in sh, "shell_js.c incluido en shell.c")
ordenes = man["ordenes"]
ck(len(ordenes) >= 1, "hay ordenes")
for o in ordenes:
    ck(o["orden"].split()[0] == "js", "empieza por js: " + o["orden"])
    ck(len(o["frases"]) >= 6, "al menos 6 frases")
    ck(len(o.get("pruebas", [])) >= 6, "al menos 6 pruebas retenidas")
    ck(o["peligrosa"] is True, "ejecutar codigo pide confirmacion")
    ck(all("?)" not in f and ")?" not in f for f in o["frases"]), "sin '?' de regex en los patrones")
    ck(all(len(p) == 2 and p[1].startswith("js ") and p[1].split()[1].endswith(".js") for p in o["pruebas"]), "pruebas: 'js archivo.js'")
    frases = set(o["frases"])
    ck(len(frases) == len(o["frases"]), "frases sin repetir")
    ck(all(v.endswith(".js") for v in o["valores_propios"]["A"]), "valores de {A} son .js")
print(("ok " if not fallos else "FALLO") + f" manifiesto de js (274): {n} comprobaciones, {fallos} fallos")
sys.exit(1 if fallos else 0)
