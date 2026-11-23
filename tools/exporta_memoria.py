#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SOMA_280_IMPORTAR · exportador del cartucho de memorIA para SOMA. SE EJECUTA EN EL MAC (o donde este memorIA), no en SOMA.

Lee SOLO el archivo memoria.db de un cartucho (en modo lectura: no cambia nada) y escribe uno o varios archivos .smem que SOMA importa con
`memoria importar RUTA`. No lee .env, claves ni ningun otro archivo; ademas se salta los textos que parecen secretos (claves de API, contrasenas, etc.).

Uso:
  python3 tools/exporta_memoria.py ORIGEN [-o CARPETA] [--max N] [--max-episodios N] [--sin-vectores] [--sin-episodios] [--parte-mb 8] [--servir [PUERTO]]
  ORIGEN = la carpeta de un cartucho (la que contiene memoria.db) o el propio memoria.db.
  Ejemplo: python3 tools/exporta_memoria.py ~/Desktop/cerebro/memorias/memoria-unificada/body/cartuchos/principal --servir

Formato (texto, campos separados por TAB; dentro de un campo \\\\ \\t \\n \\r): ver src/agent/memoria_core.h. Cada parte es autosuficiente."""
import argparse
import calendar
import datetime
import http.server
import math
import os
import re
import secrets
import socket
import sqlite3
import sys
import threading
from array import array
from base64 import b64encode

MAX_NOMBRE = 47            # MC_NODO_NOMBRE - 1
MAX_DESC = 119             # MC_NODO_DESC - 1
MAX_TEXTO = 1200           # MC_TEXTO_MAX
MAX_FUENTE = 200           # MC_FUENTE_MAX
NIVELES = 3                # MC_NIVELES
NODOS_MAX = 250            # SOMA admite 255; margen para lo que cree el ciclo de sueno
DIM_MAX = 1024             # MC_DIM_MAX

SECRETOS = [re.compile(p) for p in (
    r"sk-[A-Za-z0-9_-]{20,}", r"AKIA[0-9A-Z]{16}", r"gh[pousr]_[A-Za-z0-9]{30,}", r"xox[abprs]-[A-Za-z0-9-]{10,}",
    r"-----BEGIN [A-Z ]*PRIVATE KEY-----", r"eyJ[A-Za-z0-9_-]{15,}\.[A-Za-z0-9_-]{15,}\.[A-Za-z0-9_-]{10,}",
    r"(?i)\b(contrase(?:n|ñ)a|password|passwd|clave(?: de acceso)?|api[ _-]?key|secret(?:o)?|token)\b[^.\n]{0,25}?(?:es|is|:|=)\s*[\"']?[^\s\"']{6,}",
    r"\b[0-9a-fA-F]{40,}\b",
)]
_B64 = re.compile(r"[A-Za-z0-9+/]{48,}={0,2}")


def es_secreto(t):
    if any(r.search(t) for r in SECRETOS):
        return True
    for m in _B64.finditer(t):          # una cadena larga mezclada (mayusculas, minusculas y cifras): parece un token; una ruta o una palabra larga, no
        c = m.group(0)
        if any(x.isdigit() for x in c) and any(x.islower() for x in c) and any(x.isupper() for x in c):
            return True
    return False


def recorta(b, n):
    """recorta unos bytes UTF-8 a n sin partir un caracter"""
    b = b[:n] if len(b) > n else b
    return b.decode("utf-8", "ignore")


def limpia_nombre(s):
    s = re.sub(r"[\t\r\n/\\]+", " ", str(s or "")).strip()
    s = re.sub(r"\s+", " ", s)
    return recorta(s.encode("utf-8"), MAX_NOMBRE).strip()


def esc(s):
    return str(s).replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


def epoch(s, defecto=0):
    """'2024-05-03 10:20:30' | ISO | solo fecha | numero: segundos Unix (la hora se toma tal cual, como UTC). 0 = defecto."""
    if s is None or s == "":
        return defecto
    try:
        if isinstance(s, (int, float)) or re.fullmatch(r"\d+(\.\d+)?", str(s).strip()):
            v = float(s)
            return int(v) if 0 < v < 2 ** 32 else defecto
        t = str(s).strip().replace("T", " ").replace("Z", "")
        t = re.sub(r"([+-]\d\d:?\d\d)$", "", t).strip()
        for f in ("%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d"):
            try:
                v = calendar.timegm(datetime.datetime.strptime(t, f).timetuple())
                return v if 0 < v < 2 ** 32 else defecto
            except ValueError:
                pass
    except Exception:
        pass
    return defecto


try:
    import numpy as _np
except Exception:  # sin numpy tambien funciona, solo mas despacio
    _np = None


def cuantiza(blob):
    """float32 -> int8 de norma 127 (igual que mc_parse_embedding del nucleo de SOMA: redondeo al mas cercano, lejos del cero)"""
    if _np is not None:
        v = _np.frombuffer(bytes(blob), dtype="<f4").astype("f8")
        if len(v) < 8 or len(v) > DIM_MAX:
            return None
        n = float(_np.sqrt((v * v).sum()))
        if not n > 0:
            return None
        w = v * (127.0 / n)
        q = _np.sign(w) * _np.floor(_np.abs(w) + 0.5)
        return _np.clip(q, -127, 127).astype("i1").tobytes()
    a = array("f")
    a.frombytes(bytes(blob))
    if sys.byteorder != "little":
        a.byteswap()
    n = math.sqrt(sum(float(x) * float(x) for x in a))
    if n <= 0 or len(a) < 8 or len(a) > DIM_MAX:
        return None
    k = 127.0 / n
    out = bytearray()
    for x in a:
        v = x * k
        q = int(math.floor(abs(v) + 0.5))
        q = 127 if q > 127 else q
        out.append((q if v >= 0 else -q) & 0xFF)
    return bytes(out)


def abre(origen):
    p = os.path.abspath(os.path.expanduser(origen))
    if os.path.isdir(p):
        c = os.path.join(p, "memoria.db")
        if not os.path.isfile(c):
            hallados = [os.path.join(p, d) for d in sorted(os.listdir(p)) if os.path.isfile(os.path.join(p, d, "memoria.db"))]
            if not hallados:
                for sub in ("body/cartuchos", "cartuchos", "body/largo_plazo", "largo_plazo"):
                    q = os.path.join(p, sub)
                    if os.path.isdir(q):
                        hallados += [os.path.join(q, d) for d in sorted(os.listdir(q)) if os.path.isfile(os.path.join(q, d, "memoria.db"))]
                        if os.path.isfile(os.path.join(q, "memoria.db")):
                            hallados.append(q)
            raise SystemExit("No hay memoria.db en %s.%s" % (p, ("\nCartuchos que he encontrado:\n  " + "\n  ".join(hallados)) if hallados else ""))
        p = c
    if not os.path.isfile(p) or os.path.basename(p) != "memoria.db":
        raise SystemExit("El origen tiene que ser la carpeta de un cartucho o un archivo llamado memoria.db: " + p)
    con = sqlite3.connect("file:" + p + "?mode=ro", uri=True, timeout=10)
    con.row_factory = sqlite3.Row
    return con, p


def columnas(con, tabla):
    return {r[1] for r in con.execute("PRAGMA table_info(%s)" % tabla)}


def lee_nodos(con):
    """devuelve {id: ruta de hasta 3 niveles} y {ruta: descripcion}"""
    filas = {r["id"]: r for r in con.execute("SELECT id, parent_id, name, description FROM nodes")}
    rutas, descr = {}, {}
    for i, r in filas.items():
        cadena, seen, x = [], set(), r
        while x is not None and x["id"] not in seen:
            seen.add(x["id"])
            cadena.append(x)
            x = filas.get(x["parent_id"])
        cadena.reverse()
        nombres = [limpia_nombre(c["name"]) or "tema" for c in cadena][:NIVELES]
        rutas[i] = "/".join(nombres)
        if len(cadena) <= NIVELES:
            d = re.sub(r"\s+", " ", str(r["description"] or "")).strip()
            if d:
                descr["/".join(nombres)] = recorta(d.encode("utf-8"), MAX_DESC)
    return rutas, descr


def reduce_nodos(rutas_celdas):
    """si hay mas de NODOS_MAX rutas distintas, las hojas con menos celdas suben a su padre. Devuelve (mapa ruta->ruta, nodos)"""
    cuenta = {}
    for r in rutas_celdas:
        if r:
            cuenta[r] = cuenta.get(r, 0) + 1

    def todos(c):
        s = set()
        for r in c:
            p = r.split("/")
            for k in range(1, len(p) + 1):
                s.add("/".join(p[:k]))
        return s
    mapa = {r: r for r in cuenta}
    while len(todos(set(mapa.values()))) > NODOS_MAX:
        usadas = {}
        for o, d in mapa.items():
            usadas[d] = usadas.get(d, 0) + cuenta[o]
        hojas = [r for r in usadas if "/" in r and not any(x.startswith(r + "/") for x in usadas)]
        if not hojas:
            hojas = [r for r in usadas if "/" in r] or list(usadas)
        h = min(hojas, key=lambda r: (usadas[r], -r.count("/")))
        padre = h.rsplit("/", 1)[0] if "/" in h else None
        if padre is None:
            break
        for o, d in list(mapa.items()):
            if d == h:
                mapa[o] = padre
    return mapa, sorted(todos(set(mapa.values())))


def construye(con, a):
    inf = {"secretos": 0, "sin_texto": 0}
    cc = columnas(con, "cells")
    sel = ["id", "node_id", "text", "importance"] + [c for c in ("fuente", "ingest_timestamp", "valid_from", "valid_until", "superseded_by", "archived", "created_tick") if c in cc]
    rutas, descr = lee_nodos(con) if "nodes" in {r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table'")} else ({}, {})
    celdas = {}
    for r in con.execute("SELECT %s FROM cells ORDER BY created_tick, id" % ", ".join(sel) if "created_tick" in cc else "SELECT %s FROM cells ORDER BY id" % ", ".join(sel)):
        d = dict(r)
        t = re.sub(r"[ \t]+", " ", str(d["text"] or "")).strip()
        if not t:
            inf["sin_texto"] += 1
            continue
        if es_secreto(t) or es_secreto(str(d.get("fuente") or "")):
            inf["secretos"] += 1
            continue
        d["t"] = recorta(t.encode("utf-8"), MAX_TEXTO)
        d["epoch"] = epoch(d.get("valid_from"), epoch(d.get("ingest_timestamp"), 0))
        d["imp"] = max(1, min(10, int(round(float(d.get("importance") or 5)))))
        sup = d.get("superseded_by")
        d["estado"] = 2 if (sup not in (None, "") or d.get("valid_until") not in (None, "")) else (1 if d.get("archived") else 0)
        d["sust"] = str(sup) if sup not in (None, "") else None
        d["ruta"] = rutas.get(d.get("node_id"), "")
        d["fuente"] = recorta(str(d.get("fuente") or "").replace("\n", " ").encode("utf-8"), MAX_FUENTE)
        celdas[str(d["id"])] = d
    for d in celdas.values():
        s = celdas.get(d["sust"]) if d["sust"] else None
        d["hasta"] = epoch(d.get("valid_until"), (s["epoch"] if s else 0) or d["epoch"] or 1) if d["estado"] == 2 else 0
        if d["sust"] and d["sust"] not in celdas:
            d["sust"] = None
    # vectores
    vecs, modelo, dim = {}, "", 0
    if not a.sin_vectores and "embeddings" in {r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table'")}:
        mods = con.execute("SELECT model, dim, COUNT(*) c FROM embeddings WHERE kind='cell' GROUP BY model, dim ORDER BY c DESC").fetchall()
        if mods and int(mods[0]["dim"]) <= DIM_MAX:
            modelo, dim = str(mods[0]["model"]), int(mods[0]["dim"])
    selecc = {"cell": set(), "episode": set()}
    # ---- seleccion bajo el tope
    act = [d for d in celdas.values() if d["estado"] == 0]
    if len(act) > a.max:
        act.sort(key=lambda d: (-d["imp"], -d["epoch"]))
        inf["activas_cortadas"] = len(act) - a.max
        act = act[:a.max]
    elegidas = list(act)
    ids = {str(d["id"]) for d in elegidas}
    resto = a.max - len(elegidas)
    for est in (2, 1):
        cand = [d for d in celdas.values() if d["estado"] == est and str(d["id"]) not in ids]
        if est == 2:
            cand = [d for d in cand if d["sust"] and d["sust"] in ids]
        cand.sort(key=lambda d: -d["epoch"])
        tom = cand[:max(0, resto)]
        elegidas += tom
        ids |= {str(d["id"]) for d in tom}
        resto -= len(tom)
    # supersedidas cuyo sustituto no entro: la referencia se pierde, no el estado
    for d in elegidas:
        if d["sust"] and d["sust"] not in ids:
            d["sust"] = None
    eps = []
    if not a.sin_episodios and "episodes" in {r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table'")}:
        lim = max(0, min(a.max_episodios, resto))
        for r in con.execute("SELECT id, ts, role, text FROM episodes ORDER BY ts DESC, id DESC"):
            if len(eps) >= lim:
                inf["episodios_cortados"] = inf.get("episodios_cortados", 0) + 1
                continue
            t = re.sub(r"[ \t]+", " ", str(r["text"] or "")).strip()
            if not t:
                continue
            if es_secreto(t):
                inf["secretos"] += 1
                continue
            rol = {"user": 0, "assistant": 1}.get(str(r["role"]), 2)
            eps.append({"id": str(r["id"]), "epoch": int(r["ts"] or 0) if 0 < float(r["ts"] or 0) < 2 ** 32 else 0, "rol": rol, "t": recorta(t.encode("utf-8"), MAX_TEXTO)})
        eps.reverse()
    # vectores de lo elegido
    if modelo:
        for kind, lista in (("cell", elegidas), ("episode", eps)):
            quiero = [str(d["id"]) for d in lista]
            for i in range(0, len(quiero), 500):
                tr = quiero[i:i + 500]
                for r in con.execute("SELECT ref, vec, dim FROM embeddings WHERE kind=? AND model=? AND ref IN (%s)" % ",".join("?" * len(tr)), [kind, modelo] + tr):
                    if int(r["dim"]) == dim:
                        q = cuantiza(r["vec"])
                        if q:
                            vecs[(kind, str(r["ref"]))] = q
    # ---- nodos: reducir a un maximo
    mapa, nodos = reduce_nodos([d["ruta"] for d in elegidas])
    for d in elegidas:
        d["ruta"] = mapa.get(d["ruta"], "")
    inf["nodos"] = len(nodos)
    return celdas, elegidas, eps, vecs, modelo, dim, descr, nodos, inf


def grupos_de(elegidas):
    """las sustituciones van juntas (en la misma parte) para poder enlazarlas"""
    por_id = {str(d["id"]): d for d in elegidas}
    padre = {k: k for k in por_id}

    def raiz(x):
        while padre[x] != x:
            padre[x] = padre[padre[x]]
            x = padre[x]
        return x
    for k, d in por_id.items():
        if d["sust"] and d["sust"] in por_id:
            padre[raiz(k)] = raiz(d["sust"])
    g = {}
    for d in elegidas:
        g.setdefault(raiz(str(d["id"])), []).append(d)
    return list(g.values())


def linea_celda(d, ord_, sust_ord, vec):
    f = ["C", str(ord_), str(d["imp"]), str(d["epoch"]), str(d["estado"]), str(sust_ord), str(d["hasta"]), esc(d["ruta"]), esc(d["fuente"]), esc(d["t"])]
    if vec:
        f.append(b64encode(vec).decode())
    return "\t".join(f) + "\n"


def escribe_partes(salida, celdas, elegidas, eps, vecs, modelo, dim, descr, nodos, tope_bytes):
    unidades = [("c", g) for g in grupos_de(elegidas)] + [("e", [e]) for e in eps]
    partes, actual, bytes_ = [], [], 0
    for tipo, g in unidades:
        peso = sum(len(x["t"].encode("utf-8")) * 1.2 + 300 + (1400 if (("cell" if tipo == "c" else "episode"), str(x["id"])) in vecs else 0) for x in g)
        if actual and bytes_ + peso > tope_bytes:
            partes.append(actual)
            actual, bytes_ = [], 0
        actual.append((tipo, g))
        bytes_ += peso
    if actual:
        partes.append(actual)
    if not partes:
        partes = [[]]
    nombres = []
    for pi, parte in enumerate(partes, 1):
        nombre = "memoria-%03d.smem" % pi
        cel = [x for t, g in parte if t == "c" for x in g]
        ord_de = {str(d["id"]): k for k, d in enumerate(cel, 1)}
        usadas = set()
        for d in cel:
            r = d["ruta"]
            if r:
                p = r.split("/")
                for k in range(1, len(p) + 1):
                    usadas.add("/".join(p[:k]))
        with open(os.path.join(salida, nombre), "w", encoding="utf-8", newline="\n") as fh:
            fh.write("#SOMAMEM 1 modelo=%s dim=%d parte=%d/%d\n" % (re.sub(r"\s+", "_", modelo) if modelo else "ninguno", dim if modelo else 0, pi, len(partes)))
            for r in sorted(usadas):
                fh.write("N\t%s\t%s\n" % (esc(r), esc(descr.get(r, ""))))
            for d in cel:
                so = ord_de.get(d["sust"], 0) if d["sust"] else 0
                fh.write(linea_celda(d, ord_de[str(d["id"])], so, vecs.get(("cell", str(d["id"])))))
            for t, g in parte:
                if t == "e":
                    e = g[0]
                    f = ["E", str(e["epoch"]), str(e["rol"]), esc(e["t"])]
                    v = vecs.get(("episode", e["id"]))
                    if v:
                        f.append(b64encode(v).decode())
                    fh.write("\t".join(f) + "\n")
        nombres.append(nombre)
    return nombres


def ip_local():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "IP_DEL_MAC"


def servir(carpeta, nombres, puerto):
    token = secrets.token_hex(8)
    permitido = {"/%s/%s" % (token, n): os.path.join(carpeta, n) for n in nombres}

    class H(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.0"

        def do_GET(self):
            ruta = permitido.get(self.path)
            if not ruta:
                self.send_error(404)
                return
            with open(ruta, "rb") as fh:
                datos = fh.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(datos)))
            self.end_headers()
            self.wfile.write(datos)
            sys.stderr.write("  servido %s a %s\n" % (os.path.basename(ruta), self.client_address[0]))

        def log_message(self, *a):
            pass
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", puerto), H)
    ip = ip_local()
    print("\nSirviendo SOLO estos %d archivo(s) en la red local (direccion con clave de un solo uso; Ctrl-C para terminar):" % len(nombres))
    print("En SOMA, una orden por parte:")
    for n in nombres:
        print("  descargar http://%s:%d/%s/%s /usuario/%s" % (ip, puerto, token, n, n))
        print("  memoria importar /usuario/%s" % n)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nTerminado.")
    finally:
        srv.server_close()


def main(argv=None):
    ap = argparse.ArgumentParser(description="Exporta la memoria de memorIA al formato .smem de SOMA (solo lee memoria.db).")
    ap.add_argument("origen")
    ap.add_argument("-o", "--salida", default="memoria-export")
    ap.add_argument("--max", type=int, default=36000, help="tope de elementos exportados (SOMA admite 40960 en total)")
    ap.add_argument("--max-episodios", type=int, default=8000)
    ap.add_argument("--sin-vectores", action="store_true")
    ap.add_argument("--sin-episodios", action="store_true")
    ap.add_argument("--parte-mb", type=float, default=8.0, help="tamano maximo aproximado de cada archivo")
    ap.add_argument("--servir", nargs="?", const=8099, type=int, metavar="PUERTO")
    a = ap.parse_args(argv)
    con, ruta_db = abre(a.origen)
    os.makedirs(a.salida, exist_ok=True)
    celdas, elegidas, eps, vecs, modelo, dim, descr, nodos, inf = construye(con, a)
    con.close()
    nombres = escribe_partes(a.salida, celdas, elegidas, eps, vecs, modelo, dim, descr, nodos, int(a.parte_mb * 1024 * 1024))
    act = sum(1 for d in elegidas if d["estado"] == 0)
    sus = sum(1 for d in elegidas if d["estado"] == 2)
    arc = sum(1 for d in elegidas if d["estado"] == 1)
    conv = sum(1 for d in elegidas if ("cell", str(d["id"])) in vecs) + sum(1 for e in eps if ("episode", e["id"]) in vecs)
    total = os.path.getsize
    res = ["Origen: " + ruta_db,
           "Celdas exportadas: %d (activas %d, sustituidas %d, archivadas %d) de %d en memorIA" % (len(elegidas), act, sus, arc, len(celdas)),
           "Episodios (mensajes literales) exportados: %d" % len(eps),
           "Nodos del arbol: %d (SOMA usa 3 niveles; lo mas profundo se junta en el tercero)" % inf["nodos"],
           "Vectores: %d%s" % (conv, (" (%s, %d dimensiones, ya en el formato de SOMA)" % (modelo, dim)) if modelo else " (ninguno: SOMA los calculara con su servidor de embeddings)"),
           "Saltados por parecer secretos (claves, contrasenas...): %d" % inf["secretos"]]
    if inf.get("activas_cortadas"):
        res.append("AVISO: %d celdas activas no caben bajo --max %d (se quedaron las mas importantes)" % (inf["activas_cortadas"], a.max))
    if inf.get("episodios_cortados"):
        res.append("Episodios sin exportar por el tope: %d (los mas antiguos)" % inf["episodios_cortados"])
    res.append("Archivos en %s: %s (%.1f MB)" % (os.path.abspath(a.salida), ", ".join(nombres), sum(total(os.path.join(a.salida, n)) for n in nombres) / 1e6))
    print("\n".join(res))
    with open(os.path.join(a.salida, "LEEME.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(res) + "\n\nPara importar en SOMA, por cada parte:\n  descargar http://IP_DEL_MAC:PUERTO/CLAVE/memoria-001.smem /usuario/memoria-001.smem\n  memoria importar /usuario/memoria-001.smem\n(o usa --servir y copia las ordenes que imprime)\n")
    if a.servir is not None:
        servir(a.salida, nombres, a.servir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
