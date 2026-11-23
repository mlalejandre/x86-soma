# -*- coding: utf-8 -*-
"""SOMA_280_IMPORTAR: prueba del exportador (tools/exporta_memoria.py) con una memoria.db que tiene el ESQUEMA REAL de memorIA.
Genera tambien los archivos .smem y esperado.txt que lee tests/test_memoria_280.c (el importador del nucleo).
Uso: python3 tests/test_exporta_280.py CARPETA_DE_TRABAJO"""
import array
import json
import os
import random
import shutil
import sqlite3
import sys

# SOMA_284_GITHUB: las claves FALSAS de esta prueba se arman al ejecutarla. Escritas enteras, el detector de secretos de
# subir_github.py las tomaria por reales y no dejaria subir el repositorio.
K_SK = "sk" + "-" + "abcdefghijklmnopqrstuvwxyz0123456789"
K_GH = "gh" + "p_" + "abcdefghijklmnopqrstuvwxyz0123456789ABCD"
K_SL = "xo" + "xb-1234567890-abcdefg"
K_PK = "-----BEGIN RSA " + "PRIVATE KEY-----"

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(AQUI), "tools"))
import exporta_memoria as X  # noqa: E402

fallos = 0
total = 0


def CK(c, msg):
    global fallos, total
    total += 1
    if not c:
        fallos += 1
        print("FALLO:", msg)


DDL = [
    "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)",
    "CREATE TABLE nodes (id INTEGER PRIMARY KEY AUTOINCREMENT, parent_id INTEGER, level INTEGER NOT NULL, slug TEXT NOT NULL, name TEXT NOT NULL, "
    "description TEXT DEFAULT '', importance REAL DEFAULT 5.0, celulas INTEGER DEFAULT 0, dirty INTEGER DEFAULT 1, created_tick INTEGER DEFAULT 0, pinned INTEGER DEFAULT 0)",
    "CREATE TABLE cells (id TEXT PRIMARY KEY, node_id INTEGER NOT NULL, text TEXT NOT NULL, importance REAL NOT NULL, kind TEXT DEFAULT 'dato', fuente TEXT, "
    "ingest_timestamp TEXT, created_tick INTEGER NOT NULL, updated_tick INTEGER NOT NULL, last_access_tick INTEGER DEFAULT 0, access_count INTEGER DEFAULT 0, "
    "archived INTEGER DEFAULT 0, ruta_original TEXT, tested_rev INTEGER DEFAULT -1, tested_tick INTEGER DEFAULT 0, valid_from TEXT, valid_until TEXT, superseded_by TEXT)",
    "CREATE TABLE embeddings (kind TEXT NOT NULL, ref TEXT NOT NULL, model TEXT NOT NULL, dim INTEGER NOT NULL, vec BLOB NOT NULL, PRIMARY KEY (kind, ref))",
    "CREATE TABLE episodes (id INTEGER PRIMARY KEY AUTOINCREMENT, uid TEXT UNIQUE, ts REAL NOT NULL, role TEXT NOT NULL, text TEXT NOT NULL)",
]


def vec(rng, d=1024):
    v = [rng.gauss(0, 1) for _ in range(d)]
    return array.array("f", v)


def main(trabajo):
    shutil.rmtree(trabajo, ignore_errors=True)
    os.makedirs(trabajo)
    cart = os.path.join(trabajo, "cartucho")
    os.makedirs(cart)
    db = os.path.join(cart, "memoria.db")
    con = sqlite3.connect(db)
    for q in DDL:
        con.execute(q)
    rng = random.Random(280)
    # nodos: Salud (raiz) > Alergias; Vida/Casa/Mascotas/Gatos (4 niveles: se junta en 3); un nombre con barra
    N = {}

    def nodo(nombre, padre, nivel, desc=""):
        c = con.execute("INSERT INTO nodes (parent_id, level, slug, name, description) VALUES (?,?,?,?,?)", (padre, nivel, nombre.lower(), nombre, desc))
        N[nombre] = c.lastrowid
        return c.lastrowid
    salud = nodo("Salud", None, 0, "Datos de salud del usuario")
    nodo("Alergias", salud, 1, "Alergias conocidas")
    vida = nodo("Vida", None, 0)
    casa = nodo("Casa/Hogar", vida, 1, "Donde vive")
    masc = nodo("Mascotas", casa, 2)
    nodo("Gatos", masc, 3, "no debe salir: nivel 4")
    cel = []

    def celda(i, texto, imp, nodo_, fuente="Conversacion", vf="2024-05-03 10:20:30", arch=0, vu=None, sup=None, emb=False):
        con.execute("INSERT INTO cells (id,node_id,text,importance,fuente,ingest_timestamp,created_tick,updated_tick,archived,valid_from,valid_until,superseded_by) "
                    "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)", (i, nodo_, texto, imp, fuente, vf, len(cel), len(cel), arch, vf, vu, sup))
        cel.append(i)
        if emb:
            v = vec(rng)
            con.execute("INSERT INTO embeddings VALUES ('cell',?,?,?,?)", (i, "bge-m3", 1024, v.tobytes()))
            return v
    celda("c1", "El usuario es alergico a la penicilina", 9, N["Alergias"], emb=True)
    v_ref = celda("c2", "El usuario vive en Bilbao, calle Ercilla 4", 7, N["Casa/Hogar"], emb=True)
    celda("c3", "El usuario vivia en Madrid", 6, N["Casa/Hogar"], vu="2024-06-01 09:00:00", sup="c2", vf="2022-01-01 00:00:00")
    celda("c4", "El gato se llama Miso\ty tiene 3 a\u00f1os\nsegunda linea \\ barra", 5, N["Gatos"])
    celda("c5", "Dato viejo olvidado por el decaimiento", 2, N["Vida"], arch=1)
    celda("c6", "Mi contrase\u00f1a del banco es Hunter2xyz99", 8, N["Vida"])
    celda("c7", "La clave de API es " + K_SK, 8, N["Vida"], fuente="nota")
    celda("c8", "Cumple los a\u00f1os el 14 de marzo", 8, N["Vida"], vf="2023-03-14 08:00:00", emb=True)
    celda("c9", "El usuario es alergico a la penicilina.", 9, N["Alergias"])           # duplicado (se salta al importar dos veces con el existente)
    for k in range(10, 40):
        celda("c%d" % k, "Hecho de relleno numero %d sobre el tema %d" % (k, k % 4), 1 + k % 10, N["Vida"] if k % 2 else N["Salud"], emb=(k % 3 == 0))
    con.execute("UPDATE cells SET valid_from=NULL WHERE id='c8'")
    for k in range(6):
        con.execute("INSERT INTO episodes (uid, ts, role, text) VALUES (?,?,?,?)", ("u%d" % k, 1717400000 + k * 60, "user" if k % 2 == 0 else "assistant", "Mensaje %d de la charla con acentos \u00f1\u00e9" % k))
        if k < 3:
            con.execute("INSERT INTO embeddings VALUES ('episode',?,?,?,?)", (str(k + 1), "bge-m3", 1024, vec(rng).tobytes()))
    con.execute("INSERT INTO episodes (uid, ts, role, text) VALUES ('s1', 1717500000, 'user', ?)", ("mi token es " + K_GH,))
    con.commit()
    con.close()
    antes = open(db, "rb").read()

    salida = os.path.join(trabajo, "salida")
    rc = X.main([cart, "-o", salida, "--parte-mb", "0.03"])        # partes diminutas: se parte en varias
    CK(rc == 0, "el exportador termina bien")
    CK(open(db, "rb").read() == antes, "la base de datos de origen no se ha tocado")
    partes = sorted(f for f in os.listdir(salida) if f.endswith(".smem"))
    CK(len(partes) >= 2, "con partes pequenas sale mas de un archivo (%s)" % partes)
    txt = ""
    cab = []
    for f in partes:
        L = open(os.path.join(salida, f), encoding="utf-8").read()
        cab.append(L.split("\n", 1)[0])
        txt += L
    CK(all(c.startswith("#SOMAMEM 1 modelo=bge-m3 dim=1024 parte=") for c in cab), "cabecera de cada parte: " + str(cab[:1]))
    CK("penicilina" in txt and "Bilbao" in txt, "los hechos normales salen")
    CK("Hunter2xyz99" not in txt and "sk-abcdef" not in txt and "ghp_abcdef" not in txt, "los secretos no salen")
    CK("Ercilla" in txt and "Madrid" in txt, "tambien sale el hecho sustituido (historia)")
    CK("Hecho de relleno numero 10 " in txt, "el relleno sale")
    lineas = [l.split("\t") for l in txt.split("\n") if l and not l.startswith("#")]
    cs = [l for l in lineas if l[0] == "C"]
    es = [l for l in lineas if l[0] == "E"]
    ns = [l for l in lineas if l[0] == "N"]
    CK(len(cs) == 37, "celdas exportadas = 39 - 2 secretos = 37 (hay %d)" % len(cs))
    CK(len(es) == 6, "episodios: 6 normales (el secreto no) = %d" % len(es))
    CK(all(len(l) in (10, 11) for l in cs), "toda celda tiene 10 u 11 campos")
    CK(any("Salud/Alergias" == l[1] for l in ns) and any("Vida/Casa Hogar/Mascotas" == l[1] for l in ns), "rutas de nodos: 3 niveles, '/' del nombre convertido en espacio: %s" % sorted(l[1] for l in ns)[:8])
    CK(not any("Gatos" in l[1] for l in ns), "el nivel 4 se junta en el 3")
    CK(any(l[1] == "Salud/Alergias" and l[2] == "Alergias conocidas" for l in ns), "descripcion de nodo")
    sust = [l for l in cs if l[4] == "2"]
    CK(len(sust) == 1 and sust[0][5] != "0", "la sustituida apunta a su sustituta dentro de la parte")
    madrid = sust[0]
    CK(madrid[6] != "0", "t_hasta de la sustituida (valid_until)")
    CK(sum(1 for l in cs if l[4] == "1") == 1, "una archivada")
    miso = [l for l in cs if "Miso" in l[9]][0]
    CK("\\n" in miso[9] and "\\\\" in miso[9] and "\t" not in miso[9] and "Miso y" in miso[9], "escapes en el texto (tab -> espacio, salto y barra escapados): " + repr(miso[9]))
    conv = [l for l in cs if len(l) == 11]
    CK(len(conv) >= 10, "hay celdas con vector (%d)" % len(conv))
    import base64
    b = base64.b64decode([l for l in cs if "penicilina" in l[9] and len(l) == 11][0][10])
    CK(len(b) == 1024, "vector de 1024 bytes")
    q = [x - 256 if x > 127 else x for x in b]
    nn = sum(x * x for x in q) ** 0.5
    CK(abs(nn - 127) < 4, "norma del vector ~127 (%.2f)" % nn)
    # cuantizacion: la del exportador debe dar lo mismo que el nucleo de SOMA (se comprueba en C con el JSON de abajo)
    with open(os.path.join(trabajo, "vec_ref.json"), "w") as fh:
        json.dump({"embedding": [float(x) for x in v_ref]}, fh)
    qref = X.cuantiza(v_ref.tobytes())
    with open(os.path.join(trabajo, "vec_ref.q"), "w") as fh:
        fh.write(base64.b64encode(qref).decode())
    if X._np is not None:                                     # con y sin numpy sale exactamente lo mismo
        con_np = X.cuantiza(v_ref.tobytes())
        guardado, X._np = X._np, None
        CK(X.cuantiza(v_ref.tobytes()) == con_np, "cuantizacion con y sin numpy identica")
        X._np = guardado
    CK(X.cuantiza(array.array("f", [0.0] * 1024).tobytes()) is None and X.cuantiza(array.array("f", [1.0] * 4).tobytes()) is None, "vector nulo o corto: se descarta")
    # fecha por defecto (c8 sin valid_from): usa ingest_timestamp
    c8 = [l for l in cs if "Cumple" in l[9]][0]
    CK(c8[3] != "0", "sin valid_from: usa ingest_timestamp (%s)" % c8[3])
    CK(X.epoch("2024-05-03 10:20:30") == 1714731630, "epoch")
    CK(X.epoch("2024-05-03T10:20:30Z") == 1714731630 and X.epoch("2024-05-03") == 1714694400, "epoch ISO y solo fecha")
    CK(X.epoch("basura", 7) == 7 and X.epoch(None, 9) == 9, "epoch invalido: el defecto")
    # --sin-vectores y --max
    s2 = os.path.join(trabajo, "salida2")
    X.main([cart, "-o", s2, "--sin-vectores", "--sin-episodios", "--max", "10"])
    t2 = "".join(open(os.path.join(s2, f), encoding="utf-8").read() for f in os.listdir(s2) if f.endswith(".smem"))
    l2 = [l.split("\t") for l in t2.split("\n") if l.startswith("C\t")]
    CK(len(l2) == 10 and all(len(l) == 10 for l in l2) and "\nE\t" not in t2 and "modelo=ninguno" in t2, "sin vectores, sin episodios y con tope: %d celdas" % len(l2))
    CK(sum(1 for l in l2 if l[4] == "0") == 10, "bajo el tope se quedan primero las activas")
    # nodos: tope
    mapa, nodos = X.reduce_nodos(["A/B/%d" % i for i in range(400)] + ["A/B/0"] * 5)
    CK(len(nodos) <= X.NODOS_MAX, "el arbol se reduce a <= %d nodos (%d)" % (X.NODOS_MAX, len(nodos)))
    # secretos
    for t, s in (("Mi password es abc12345", 1), ("la contrase\u00f1a: Zx9!qwerty", 1), ("Xoxb token " + K_SL, 1), (K_PK, 1),
                 ("El usuario vive en Bilbao", 0), ("Le gusta la musica y el cafe", 0), ("La palabra clave del juego es larga", 0), ("ruta /Users/ana/Documents/proyectos/memoria/cartuchos/principal/archivos/notas", 0),
                 ("commit 0123456789abcdef0123456789abcdef01234567", 1)):
        CK(X.es_secreto(t) == bool(s), "secreto(%r) = %s" % (t, bool(s)))
    # errores del origen
    try:
        X.abre(os.path.join(trabajo, "no_existe"))
        CK(False, "origen inexistente debe fallar")
    except SystemExit:
        CK(True, "origen inexistente")
    # esperado.txt para el test en C
    with open(os.path.join(trabajo, "esperado.txt"), "w") as fh:
        fh.write("partes %d\nceldas %d\nepisodios %d\nactivas %d\nsustituidas %d\narchivadas %d\nconvec %d\n" % (len(partes), len(cs), len(es), sum(1 for l in cs if l[4] == "0"), 1, 1, len(conv)))
        fh.write("episodios_con_vec %d\n" % sum(1 for l in es if len(l) == 5))
    print("test_exporta_280: %d comprobaciones, %d fallos" % (total, fallos))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/f280"))
