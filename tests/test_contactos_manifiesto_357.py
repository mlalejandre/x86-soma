"""SOMA_357_CONTACTOS: el manifiesto del LCM (lcm/manifiestos/contactos.json) y la orden `contactos` del nucleo tienen que decir lo mismo:
cada orden que el LCM puede producir llega a su rama de la orden real (no cae en la busqueda ni en la ayuda). Tambien la prueba de
integracion de contactos.c con agenda.c (cumpleanos en el Calendario, importar/exportar, el contexto del agente).
    python3 tests/test_contactos_manifiesto_357.py"""
import json, os, platform, subprocess, sys
RAIZ = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
fallos = 0; n = 0
def ck(c, m):
    global fallos, n
    n += 1
    if not c:
        fallos += 1; print("FALLO:", m)
man = json.load(open(os.path.join(RAIZ, "lcm", "manifiestos", "contactos.json"), encoding="utf-8"))
ordenes = man["ordenes"]
ck(len(ordenes) >= 10, "ordenes en el manifiesto")
ck(all(o["orden"].split()[0] == "contactos" for o in ordenes), "todas empiezan por contactos")
ck(all(len(o["frases"]) >= 6 for o in ordenes), "al menos 6 frases por orden")
ck(all(len(o.get("pruebas", [])) >= 6 for o in ordenes), "al menos 6 pruebas por orden")
ck([o["id"] for o in ordenes if o["peligrosa"]] == ["contactos_borrar"], "solo borrar pide confirmacion")
ck(all("?)" not in f and ")?" not in f for o in ordenes for f in o["frases"]), "sin '?' de regex")
for o in ordenes:
    for frase, orden in o["pruebas"]:
        base = o["orden"].split("{")[0].strip()
        ck(orden.startswith(base), f"{o['id']}: la prueba '{orden}' no es de la forma '{o['orden']}'")
if platform.system() != "Linux":
    print(f"test_contactos_manifiesto_357: {n} comprobaciones, {fallos} fallos (sin ejecutar la orden real: solo en Linux)")
    sys.exit(1 if fallos else 0)
exe = "/tmp/contactos_cli_357"
r = subprocess.run(["gcc", "-std=gnu11", "-g", "-w", "-fsanitize=address,undefined", "-DWEB_HOST_TEST", "-Itests/stubs_contactos", "-Isrc/apps", "-Isrc/core",
                    "-Isrc/fs", "-Isrc/net", "-Isrc/drivers", "-Isrc", "-Isrc/arch/x86_64", "tests/contactos_cli_357.c", "src/apps/contactos.c",
                    "src/apps/contactos_core.c", "src/apps/agenda.c", "src/apps/agenda_core.c", "-Wl,--unresolved-symbols=ignore-all", "-o", exe],
                   cwd=RAIZ, capture_output=True, text=True)
ck(r.returncode == 0, "compila el probador: " + r.stderr[:400])
if r.returncode == 0:
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    s = subprocess.run([exe, "--integra"], cwd=RAIZ, capture_output=True, text=True, env=env)
    ck(s.returncode == 0, "integracion: " + s.stdout[-1500:] + s.stderr[-800:])
    # la libreta con la gente de los ejemplos y, despues, cada orden del manifiesto
    altas = ["contactos añadir " + v for v in ["Ana López, móvil 600 11 22 33, email ana@x.es, nació el 2 de enero de 1990, vive en Calle Mayor 3, Madrid, grupo familia",
             "Luis Pérez, email luis@ejemplo.es, tel 611 22 33 44, cumpleaños 5 de marzo", "Marta Gil, teléfono 622 000 111, email marta@x.es, grupo amigos",
             "Carlos García, email carlos@empresa.com, móvil 633 333 333", "Lucía Ramos, móvil 655 44 33 22, nació el 3 de mayo de 1990, email lucia@x.es",
             "Elena Ruiz, email elena@correo.es, vive en Calle Luna 2, Bilbao", "Pablo Sanz 699 887 766, email pablo@x.es, cumple 7/7", "Javi Moreno, email javi@x.com, grupo trabajo, tel 644 000 000",
             "mamá, móvil 600 000 001", "el Pepe, móvil 600 000 002", "mi hermano Juan, móvil 600 000 003, cumple 1 de abril", "la doctora Sanz, teléfono 91 555 44 33"]]
    lineas = []
    for o in ordenes:
        if o["id"] in ("contactos_abrir", "contactos_borrar", "contactos_anadir"): continue
        for _, orden in o["pruebas"]: lineas.append((o["id"], orden))
    for o in ordenes:
        if o["id"] == "contactos_anadir":
            for _, orden in o["pruebas"]: lineas.append((o["id"], orden))
    for o in ordenes:
        if o["id"] == "contactos_borrar":
            for _, orden in o["pruebas"]: lineas.append((o["id"], orden))
    entrada = "".join(a + "\n" for a in altas) + "".join(orden + "\n" for _, orden in lineas)
    s = subprocess.run([exe], input=entrada, cwd=RAIZ, capture_output=True, text=True, env=env)
    ck(s.returncode == 0, "el probador no falla: " + s.stderr[:400])
    sal = [l.split("\t", 1) for l in s.stdout.splitlines() if "\t" in l]
    res = {}
    for k, v in sal: res.setdefault(k, v)
    for a in altas: ck("Contacto guardado" in res.get(a, ""), "alta: " + a + " -> " + res.get(a, "")[:120])
    ESPERA = {"contactos_telefono": ["6", "9"], "contactos_email": ["@"], "contactos_direccion": [":"], "contactos_cumple_de": [":"],
              "contactos_cumples": ["Cumplea"], "contactos_ver": ["["], "contactos_grupo": ["Grupo", "No hay nadie en ese grupo"],
              "contactos_exportar": ["Exportados"], "contactos_importar": ["No encuentro ese archivo"], "contactos_anadir": ["Contacto guardado", "Ya lo ten"],
              "contactos_borrar": ["Borrado"]}
    for ident, orden in lineas:
        v = res.get(orden)
        ck(v is not None, "sin salida para " + orden)
        if v is None: continue
        ck("contactos ayuda" not in v and "Hay varios" not in v and "No encuentro a nadie" not in v, f"{orden} -> {v[:160]}")
        ck(any(e in v for e in ESPERA[ident]), f"{ident}: {orden} -> {v[:160]}")
print(f"test_contactos_manifiesto_357: {n} comprobaciones, {fallos} fallos")
sys.exit(1 if fallos else 0)
