"""SOMA_275_HTML: el manifiesto del LCM de la orden html (lcm/manifiestos/html.json): formato, que la orden exista en la tabla de comandos y
que cada prueba retenida use un subcomando que `html` entiende (comprobar | empaquetar | abrir).
    python3 tests/test_html_manifiesto_275.py"""
import json, os, re, sys
RAIZ = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
fallos = 0; n = 0
def ck(c, m):
    global fallos, n
    n += 1
    if not c:
        fallos += 1; print("FALLO:", m)
man = json.load(open(os.path.join(RAIZ, "lcm", "manifiestos", "html.json"), encoding="utf-8"))
sh = open(os.path.join(RAIZ, "src", "agent", "shell.c"), encoding="utf-8").read()
ck(re.search(r'\{\s*"html"\s*,\s*cmd_html\s*,\s*CMD_F_NORMAL', sh) is not None, "la orden html esta en la tabla de comandos")
ck('#include "shell_html.c"' in sh, "shell_html.c incluido en shell.c")
ordenes = man["ordenes"]
ck({o["id"] for o in ordenes} == {"html_comprobar", "html_empaquetar", "html_abrir"}, "las tres ordenes")
for o in ordenes:
    sub = o["orden"].split()[1]
    ck(o["orden"].split()[0] == "html" and sub in ("comprobar", "empaquetar", "abrir"), "subcomando valido: " + o["orden"])
    ck(len(o["frases"]) >= 6, "al menos 6 frases")
    ck(len(o.get("pruebas", [])) >= 6, "al menos 6 pruebas retenidas")
    ck(all("?)" not in f and ")?" not in f for f in o["frases"]), "sin '?' de regex en los patrones (salvo el opcional de la pregunta)")
    ck(all(len(p) == 2 and p[1].startswith("html " + sub + " ") and p[1].split()[2].endswith(".html") for p in o["pruebas"]), "pruebas: html " + sub + " archivo.html")
    ck(len(set(o["frases"])) == len(o["frases"]), "frases sin repetir")
    ck(all(v.endswith(".html") for v in o["valores_propios"]["A"]), "valores de {A} son .html")
print(("ok " if not fallos else "FALLO") + f" manifiesto de html (275): {n} comprobaciones, {fallos} fallos")
sys.exit(1 if fallos else 0)
