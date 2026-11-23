#!/usr/bin/env python3
"""SOMA_271_LCM: pruebas del circuito de manifiestos, empaquetado y puerta de regresión (sin entrenar nada).
    python3 tests/test_lcm_271.py"""
import json, os, subprocess, sys, tempfile, shutil
RAIZ = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, RAIZ)
fallos = n = 0


def CK(c, msg):
    global fallos, n
    n += 1
    if not c:
        fallos += 1
        print("FALLO:", msg)


def valida(d):
    """ejecuta la validación de manifiestos en un proceso limpio con una carpeta de manifiestos dada; -> (código, salida)"""
    r = subprocess.run([sys.executable, "-m", "tools.lcm_ordenes.manifiestos", "validar"], cwd=RAIZ, capture_output=True, text=True,
                       env=dict(os.environ, LCM_MANIFIESTOS=d, PYTHONPATH=RAIZ))
    return r.returncode, r.stdout + r.stderr


def manifiesto(**cambios):
    o = {"id": "cc_x", "orden": "cc {A}", "peligrosa": False, "frases": [f"compila {{A}} {i}" for i in range(7)]}
    o.update(cambios)
    return {"manifiesto": 1, "ordenes": [o]}


def escribe(d, obj, nombre="m.json"):
    json.dump(obj, open(os.path.join(d, nombre), "w"))


# ---- manifiestos
t = tempfile.mkdtemp()
try:
    escribe(t, manifiesto()); c, s = valida(t); CK(c == 0, "manifiesto correcto rechazado: " + s)
    for cambio, esperado in [({"orden": "comandoquenoexiste {A}"}, "no existe en el núcleo"), ({"id": "ls"}, "ya existe"), ({"id": "Mal Id"}, "id no válido"),
                             ({"peligrosa": "no"}, "peligrosa"), ({"frases": ["a {A}"]}, "al menos"), ({"orden": "cc {Z}"}, "piezas desconocidas"),
                             ({"frases": ["compila x"] * 7}, "sin todas las piezas"), ({"frases": ["(compila {A}"] * 7}, "sin cerrar"),
                             ({"pruebas": [["una"]]}, "pruebas"), ({"pruebas": [["frase", "nocomando x"]]}, "no existe")]:
        escribe(t, manifiesto(**cambio)); c, s = valida(t)
        CK(c != 0 and esperado in s, f"debía rechazar {cambio}: {s[-200:]}")
    escribe(t, manifiesto(valores_propios={"A": ["a.c", "b.c"]})); c, s = valida(t); CK(c == 0, "valores_propios correctos rechazados: " + s)
    for vp in ({"Z": ["x"]}, {"A": []}, {"A": "a.c"}, [1]):
        escribe(t, manifiesto(valores_propios=vp)); c, s = valida(t); CK(c != 0 and "valores_propios" in s, f"debía rechazar valores_propios {vp}")
    escribe(t, manifiesto())
    open(os.path.join(t, "m.json"), "w").write("{no json"); c, s = valida(t); CK(c != 0 and "JSON" in s, "JSON roto")
    escribe(t, manifiesto()); escribe(t, manifiesto(), "otro.json"); c, s = valida(t); CK(c != 0 and "ya existe" in s, "id repetido entre manifiestos")
finally:
    shutil.rmtree(t)
c, s = valida(os.path.join(RAIZ, "lcm", "manifiestos")); CK(c == 0, "los manifiestos reales no son válidos: " + s)

# ---- cobertura: lo que declara el catálogo existe en el núcleo
r = subprocess.run([sys.executable, "-m", "tools.lcm_ordenes.manifiestos", "cobertura"], cwd=RAIZ, capture_output=True, text=True, env=dict(os.environ, PYTHONPATH=RAIZ))
CK(r.returncode == 0 and "NO en el núcleo" not in r.stdout, "el catálogo pide comandos que no existen: " + r.stdout[-300:])
CK("cc" not in r.stdout.split("SIN CUBRIR")[1].split("\n")[1].split(), "cc debería estar cubierto")

# ---- empaquetado
from tools.lcm_ordenes import reentrena as R
blob = bytes(range(256)) * 10
p = R.empaqueta(blob, {"modelo": "lcm-x", "puerta": "apto"})
m, b = R.desempaqueta(p); CK(b == blob and m["modelo"] == "lcm-x" and m["puerta"] == "apto", "ida y vuelta del paquete")
for mal, txt in [(p[:-1], "truncado"), (p[:-5] + bytes([p[-5] ^ 1]) + p[-4:], "byte del modelo cambiado"), (b"XXXX" + p[4:], "sin magia")]:
    try:
        R.desempaqueta(mal); CK(False, "debía rechazar el paquete " + txt)
    except ValueError:
        CK(True, "")
CK(R.fnv1a(b"") == 0x811C9DC5 and R.fnv1a(b"a") == 0xE40C292C, "FNV-1a de referencia")

# ---- puerta de regresión (con medidas inventadas)
from tools.lcm_ordenes import gate


def c(n, ord_bien, ord_, mal=0, mal_sin=0, pel=0):
    return {"n": n, "ord": ord_, "ord_bien": ord_bien, "MAL": mal, "mal_sin": mal_sin, "pelig_sin": pel, "bien": ord_bien, "cede": 0}


def med(b1=90, tot=90, ud=3, pel_ud=0, nuevo=None, mal=1):
    return {"bancos": {"1": c(100, b1, 100, mal)}, "total": c(100, tot, 100, mal), "variantes": {"sin tildes": c(100, tot, 100)},
            "ud": {"n": 1000, "ejecutan": ud, "pelig_sin": pel_ud}, "nuevo": nuevo or {}}


base = med()
CK(gate.compara(base, med())[0], "igual debe pasar")
CK(gate.compara(base, med(b1=95, tot=95))[0], "mejor debe pasar")
CK(not gate.compara(base, med(tot=89))[0], "el total pierde una orden: rechazar")
CK(not gate.compara(base, med(b1=87, tot=90))[0], "un banco pierde 3 puntos: rechazar")
CK(gate.compara(base, med(b1=89, tot=90))[0], "un banco pierde 1 punto (dentro de la tolerancia): aceptar")
CK(not gate.compara(base, med(mal=3))[0], "más errores: rechazar")
CK(not gate.compara(base, med(ud=4))[0], "más frases normales que ejecutan algo: rechazar")
CK(not gate.compara(base, med(pel_ud=1))[0], "peligrosa sin confirmar en UD: rechazar")
mal_pel = med(); mal_pel["bancos"]["1"]["pelig_sin"] = 1; CK(not gate.compara(base, mal_pel)[0], "peligrosa sin confirmar: rechazar")
CK(gate.compara(base, med(nuevo={"cc.json": c(14, 12, 14)}))[0], "lo nuevo aprendido (12/14): aceptar")
CK(not gate.compara(base, med(nuevo={"cc.json": c(14, 9, 14)}))[0], "lo nuevo no aprendido (9/14): rechazar")
CK(not gate.compara(base, med(nuevo={"cc.json": c(14, 14, 14, 1, 1)}))[0], "lo nuevo con un error sin preguntar: rechazar")

# ---- el paquete lo acepta el C (con el modelo de fábrica incorporado)
bin_ = os.path.join(RAIZ, "tools", "lcm_ordenes", "lcm_modelo_prueba.bin")
t2 = tempfile.mkdtemp()
try:
    import re
    inc = open(os.path.join(RAIZ, "src", "agent", "lcm_ordenes_pesos.inc")).read()
    cuerpo = inc[inc.index("= {") + 3:inc.rindex("}")]
    blob = bytes(int(x) & 255 for x in re.findall(r"\d+", cuerpo))
    pq = os.path.join(t2, "m.lcm"); open(pq, "wb").write(R.empaqueta(blob, {"modelo": "lcm-test-0001", "fecha": "2026-11-22", "puerta": "apto"}))
    exe = os.path.join(t2, "t257")
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-function", "-Isrc/agent", "tests/test_lcm_257.c", "-o", exe], cwd=RAIZ, capture_output=True, text=True)
    CK(r.returncode == 0, "no compila test_lcm_257: " + r.stderr[-300:])
    if r.returncode == 0:
        r = subprocess.run([exe], cwd=RAIZ, capture_output=True, text=True, env=dict(os.environ, LCM_PAQUETE_PRUEBA=pq))
        CK(r.returncode == 0 and "0 fallos" in r.stdout, "el C no acepta el paquete de Python: " + r.stdout[-300:])
finally:
    shutil.rmtree(t2)

print(f"{'FALLO' if fallos else 'ok '} circuito del LCM (271): {n} comprobaciones, {fallos} fallos")
sys.exit(1 if fallos else 0)
