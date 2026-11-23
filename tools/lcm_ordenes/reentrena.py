"""SOMA_271_LCM: reentrena el LCM con lo que declaran los manifiestos, lo pasa por la puerta de regresión y lo deja empaquetado y versionado.

    python3 -m tools.lcm_ordenes.reentrena [--n 150000] [--epocas 8] [--trabajo DIR] [--rapido] [--instalar-incorporado]

Pasos: 1 validar manifiestos · 2 entrenar 2 redes (semillas 11 y 29; --semillas 11,29,41,53 entrena más y elige el mejor par) · 3 medir con todos los bancos y comparar con lcm/modelos/base_metricas.json
· 4 solo si es APTO: exportar, empaquetar en lcm/modelos/lcm-FECHA-HASH.lcm (con sus metadatos) y escribir el informe.
Instalar (desde fuera de SOMA, el LCM no se toca desde dentro):  python3 tools/datos.py DISCO escribir /lcm/modelo.lcm lcm/modelos/lcm-....lcm
--instalar-incorporado además regenera src/agent/lcm_ordenes_pesos.inc (el modelo de fábrica del núcleo; hay que recompilar).
--rapido entrena poquísimo: solo para probar que todo el circuito funciona (la puerta lo rechazará)."""
import argparse
import datetime
import hashlib
import json
import os
import struct
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.abspath(os.path.join(AQUI, "..", ".."))
MODELOS = os.path.join(RAIZ, "lcm", "modelos")


def fnv1a(b):
    h = 0x811C9DC5
    for x in b:
        h = ((h ^ x) * 0x01000193) & 0xFFFFFFFF
    return h


def empaqueta(blob, meta):
    """Formato LCMP (lo lee el núcleo): 'LCMP', u32 formato=1, u32 longitud del modelo, u32 FNV-1a del modelo, u32 longitud de metadatos, metadatos, modelo."""
    m = "".join(f"{k}={v}\n" for k, v in meta.items()).encode("utf-8")
    return b"LCMP" + struct.pack("<IIII", 1, len(blob), fnv1a(blob), len(m)) + m + blob


def desempaqueta(b):
    if b[:4] != b"LCMP":
        raise ValueError("no es un paquete LCMP")
    fmt, lb, ck, lm = struct.unpack("<IIII", b[4:20])
    meta, blob = b[20:20 + lm], b[20 + lm:20 + lm + lb]
    if fmt != 1 or len(blob) != lb or fnv1a(blob) != ck:
        raise ValueError("paquete dañado")
    return dict(x.split("=", 1) for x in meta.decode("utf-8").splitlines() if "=" in x), blob


def run(cmd, env, log=None):
    r = subprocess.run(cmd, cwd=RAIZ, env=env, stdout=log, stderr=subprocess.STDOUT)
    return r.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=150000)
    ap.add_argument("--epocas", type=int, default=8)
    ap.add_argument("--trabajo", default=os.path.join(MODELOS, "trabajo"))
    ap.add_argument("--semillas", default="11,29", help="redes a entrenar (2 por defecto). Con mas, se prueban TODOS los pares y se queda el mejor que pase la puerta")
    ap.add_argument("--desde", default="", help="SOMA_276_LCM: ajuste fino: .pt de partida de cada semilla, en el mismo orden que --semillas (ej. a.pt,b.pt). Sin esto se entrena desde cero")
    ap.add_argument("--lr", type=float, default=0.0, help="tasa de aprendizaje (por defecto 2e-3 desde cero, 4e-4 con --desde)")
    ap.add_argument("--rapido", action="store_true")
    ap.add_argument("--instalar-incorporado", action="store_true")
    ap.add_argument("--solo-medir", action="store_true", help="no entrena: mide los .pt que ya hay en el directorio de trabajo")
    a = ap.parse_args()
    if a.rapido:
        a.n, a.epocas = 4000, 1
    os.makedirs(a.trabajo, exist_ok=True)
    env = dict(os.environ, PYTHONPATH=RAIZ, OMP_NUM_THREADS="1", MKL_NUM_THREADS="1")
    py = sys.executable

    print("1/4 manifiestos")
    if run([py, "-m", "tools.lcm_ordenes.manifiestos", "validar"], env):
        sys.exit("manifiestos no válidos: nada que entrenar")
    run([py, "-m", "tools.lcm_ordenes.manifiestos", "cobertura"], env)

    sem = [int(x) for x in a.semillas.split(",")]
    pts = {x: os.path.join(a.trabajo, f"red{x}.pt") for x in sem}
    desde = [d for d in a.desde.split(",") if d]
    if desde and len(desde) != len(sem):
        sys.exit("--desde necesita un .pt por semilla (%d)" % len(sem))
    lr = a.lr or (4e-4 if desde else 2e-3)
    if not a.solo_medir:
        print(f"2/4 entrenando {len(sem)} redes (n={a.n}, épocas={a.epocas}{', ajuste fino lr=%g' % lr if desde else ''}) en paralelo...")
        ps = []
        for x in sem:
            e = dict(env, LCM_TRAD_RUTA=pts[x])
            lg = open(os.path.join(a.trabajo, f"entrena{x}.log"), "w")
            ps.append(subprocess.Popen([py, "-c", f"from tools.lcm_ordenes.traduce import entrena; entrena(n={a.n}, epocas={a.epocas}, semilla={x}, desde={(desde[sem.index(x)] if desde else None)!r}, lr={lr})"],
                                       cwd=RAIZ, env=e, stdout=lg, stderr=subprocess.STDOUT))
        if any(p.wait() for p in ps):
            sys.exit("el entrenamiento falló: mira " + a.trabajo + "/entrena*.log")

    print("3/4 midiendo con todos los bancos (cada par de redes)")
    base = os.path.join(MODELOS, "base_metricas.json")
    import itertools
    mejor = None
    for par in itertools.combinations(sem, 2):
        env["LCM_TRAD_RUTA"] = ":".join(pts[x] for x in par)
        nuevo = os.path.join(a.trabajo, "metricas_%d_%d.json" % par)
        if run([py, "-m", "tools.lcm_ordenes.gate", "medir", nuevo], env):
            sys.exit("la medición falló")
        r = subprocess.run([py, "-m", "tools.lcm_ordenes.gate", "comparar", base, nuevo], cwd=RAIZ, env=env, capture_output=True, text=True)
        open(os.path.join(a.trabajo, "informe_%d_%d.txt" % par), "w").write(r.stdout)
        t = json.load(open(nuevo))["total"]
        print(f"  redes {par}: {'APTO' if r.returncode == 0 else 'rechazado'} | exactas {100 * t['ord_bien'] / t['ord']:.1f} % | errores {100 * t['MAL'] / t['n']:.2f} %")
        if r.returncode == 0 and (mejor is None or (t["ord_bien"], -t["MAL"]) > mejor[0]):
            mejor = ((t["ord_bien"], -t["MAL"]), par, nuevo, r.stdout)
    if not mejor:
        print("\nNO SE APLICA: ningún par de redes pasa la puerta (informes en " + a.trabajo + "/informe_*.txt). El modelo actual sigue siendo el bueno.")
        sys.exit(1)
    _, par, nuevo, informe = mejor
    env["LCM_TRAD_RUTA"] = ":".join(pts[x] for x in par)
    print(informe)
    if len(sem) > 2:
        informe = f"(elegido el par de semillas {par} entre {len(list(itertools.combinations(sem, 2)))}; la eleccion usa los bancos de la puerta)\n" + informe
    open(os.path.join(a.trabajo, "informe_puerta.txt"), "w").write(informe)

    print("4/4 exportando y empaquetando")
    binf = os.path.join(a.trabajo, "modelo.bin")
    cmd = [py, "-m", "tools.lcm_ordenes.exporta_c", binf]
    if a.instalar_incorporado:
        cmd += ["--inc", os.path.join(RAIZ, "src", "agent", "lcm_ordenes_pesos.inc")]
    if run(cmd, env):
        sys.exit("la exportación falló")
    blob = open(binf, "rb").read()
    h = hashlib.sha256(blob).hexdigest()[:8]
    fecha = datetime.date.today().strftime("%Y%m%d")
    from . import catalogo, manifiestos
    t = json.load(open(nuevo))["total"]
    meta = {"modelo": f"lcm-{fecha}-{h}", "fecha": datetime.date.today().isoformat(), "ordenes": len(catalogo.I) - 1,
            "manifiestos": ",".join(sorted(os.path.basename(f)[:-5] for f in manifiestos.archivos())) or "-",
            "exactas": f"{100 * t['ord_bien'] / max(1, t['ord']):.1f}", "errores": f"{100 * t['MAL'] / max(1, t['n']):.2f}", "puerta": "apto"}
    out = os.path.join(MODELOS, meta["modelo"] + ".lcm")
    open(out, "wb").write(empaqueta(blob, meta))
    open(os.path.join(MODELOS, meta["modelo"] + ".txt"), "w").write(informe)
    json.dump(json.load(open(nuevo)), open(os.path.join(MODELOS, "base_metricas.json"), "w"), ensure_ascii=False, indent=1)
    print(f"\nLISTO: {out}  ({len(blob) // 1024} KiB)\nInstalar en el pendrive (desde fuera de SOMA):\n  sudo python3 tools/datos.py DISCO escribir /lcm/modelo.lcm {os.path.relpath(out, RAIZ)}")


if __name__ == "__main__":
    main()
