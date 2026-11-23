"""SOMA_272_AGENDA: el manifiesto del LCM (lcm/manifiestos/agenda.json) y la orden `agenda` del nucleo tienen que decir lo mismo:
cada orden que el LCM puede producir se ejecuta con la orden real y no da "No lo he entendido". Ademas se comprueba el formato.
    python3 tests/test_agenda_manifiesto_272.py"""
import json, os, subprocess, sys
RAIZ = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
fallos = 0; n = 0
def ck(c, m):
    global fallos, n
    n += 1
    if not c:
        fallos += 1; print("FALLO:", m)
man = json.load(open(os.path.join(RAIZ, "lcm", "manifiestos", "agenda.json"), encoding="utf-8"))
ordenes = man["ordenes"]
ck(len(ordenes) >= 10, "ordenes en el manifiesto")
ck(all(o["orden"].split()[0] == "agenda" for o in ordenes), "todas empiezan por agenda")
ck(all(len(o["frases"]) >= 6 for o in ordenes), "al menos 6 frases por orden")
ck(all(len(o.get("pruebas", [])) >= 6 for o in ordenes), "al menos 6 pruebas retenidas por orden")
ck([o["id"] for o in ordenes if o["peligrosa"]] == ["agenda_borrar"], "solo borrar pide confirmacion")
ck(all("?)" not in f and ")?" not in f for o in ordenes for f in o["frases"]), "sin signos '?' de regex en los patrones")
# ejecutar con la orden real
exe = "/tmp/agenda_cli_272"
r = subprocess.run(["gcc", "-std=gnu11", "-g", "-w", "-Itests/stubs_agenda", "-Isrc/apps",
                    "tests/agenda_cli_272.c", "src/apps/agenda.c", "src/apps/agenda_core.c", "-o", exe], cwd=RAIZ, capture_output=True, text=True)
ck(r.returncode == 0, "compila el probador: " + r.stderr[:300])
if r.returncode == 0:
    lineas = []
    for o in ordenes:
        for f, orden in o.get("pruebas", []):
            lineas.append((o["id"], orden))
        for v in (o.get("valores_propios", {}).get("T", [])[:6] if "{T}" in o["orden"] else []):
            lineas.append((o["id"], o["orden"].replace("{T}", v)))
    entrada = "".join(orden + "\n" for _, orden in lineas if orden != "agenda abrir")
    s = subprocess.run([exe], input=entrada, cwd=RAIZ, capture_output=True, text=True, env=os.environ)
    ck(s.returncode == 0, "el probador no falla: " + s.stderr[:300])
    salida = dict(l.split("\t", 1) for l in s.stdout.splitlines() if "\t" in l)
    for ident, orden in lineas:
        if orden == "agenda abrir": continue
        o = salida.get(orden)
        ck(o is not None, "sin salida para " + orden)
        if o is None: continue
        ck("No lo he entendido" not in o and "No entiendo" not in o and "Uso:" not in o or ident == "agenda_borrar", f"{orden} -> {o[:120]}")
        if ident == "agenda_anadir": ck(o.startswith("Apuntado"), f"{orden} -> {o[:120]}")
print(("ok " if not fallos else "FALLO") + f" manifiesto de la agenda (272): {n} comprobaciones, {fallos} fallos")
sys.exit(1 if fallos else 0)
