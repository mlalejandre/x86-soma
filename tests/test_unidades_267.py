#!/usr/bin/env python3
"""SOMA_267_DISCOS: src/core/unidades.c (lo que hace el nucleo con MODELOS y USUARIO) sobre pendrives de tools/soma_disco.py.

  python3 tests/test_unidades_267.py [--rapido]

1. unid_plan (C) == plan_particiones (Python) en cientos de tamanos.
2. Primer arranque con la imagen del repositorio grabada en un pendrive de 20 GB: MODELOS y USUARIO se agrandan al plan del
   pendrive, quedan vacias con sus carpetas y pasan fsck.exfat; un segundo arranque no hace nada. Nunca se escribe en SISTEMA ni DATOS.
3. Si USUARIO o MODELOS ya tienen algo, o el pendrive se grabo a su tamano, no se toca nada (huella identica).
4. Cortes de luz: la escritura numero K falla (y todas las siguientes), para cada K posible (en --rapido, una muestra): el
   siguiente arranque termina el trabajo y el resultado es el mismo que sin corte.
5. Rutas, permisos (MODELOS de solo lectura desde el VFS), archivos abiertos (tests/test_unidades_267.c ops).
"""
import hashlib, os, random, shutil, subprocess, sys, tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ / "tools"))
import soma_disco as sd

RAPIDO = "--rapido" in sys.argv
ok = fallos = saltadas = 0


def ck(c, msg):
    global ok, fallos
    if c:
        ok += 1
    else:
        fallos += 1
        print("FALLO:", msg)


def compila(tmp):
    cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc") or shutil.which("clang")
    out = str(Path(tmp) / "test_unidades_267")
    base = [cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-I", str(RAIZ / "src/core"), str(RAIZ / "tests/test_unidades_267.c"),
            str(RAIZ / "src/core/unidades.c"), str(RAIZ / "src/core/exfat.c"), "-o", out]
    for san in (["-fsanitize=address,undefined"], []):
        r = subprocess.run(base[:1] + san + base[1:], capture_output=True, text=True)
        if r.returncode == 0:
            return out
    print(r.stderr[-2000:])
    return None


def c(prog, *a):
    return subprocess.run([prog] + [str(x) for x in a], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))


def huella(p, n=None, desde=0):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        f.seek(desde)
        quedan = n
        while quedan is None or quedan > 0:
            b = f.read(16 << 20 if quedan is None else min(16 << 20, quedan))
            if not b:
                break
            h.update(b)
            if quedan is not None:
                quedan -= len(b)
    return h.hexdigest()


def copia(a, b):
    """copia dispersa (la imagen de prueba mide 20 GB y casi todo son huecos): solo se copian los tramos con datos.
    Con SEEK_DATA/SEEK_HOLE (Linux, macOS APFS) sin leer los huecos; si no, saltando los bloques a cero."""
    tam = os.path.getsize(a)
    with open(a, "rb") as fa, open(b, "wb") as fb:
        fb.truncate(tam)
        tramos = []
        try:
            pos = 0
            while pos < tam:
                ini = os.lseek(fa.fileno(), pos, os.SEEK_DATA)
                fin = os.lseek(fa.fileno(), ini, os.SEEK_HOLE)
                tramos.append((ini, fin))
                pos = fin
        except (OSError, AttributeError):
            if not tramos:
                tramos = [(0, tam)]
        for ini, fin in tramos:
            fa.seek(ini)
            p = ini
            while p < fin:
                blk = fa.read(min(8 << 20, fin - p))
                if not blk:
                    break
                if blk.count(0) != len(blk):
                    fb.seek(p)
                    fb.write(blk)
                p += len(blk)


def final_correcto(p, fsck):
    with open(p, "rb") as f:
        d = sd.Disco(f, os.path.getsize(p) // 512)
        i = sd.analizar(d)
        if i["tipo"] != "nuevo":
            return f"tipo {i['tipo']}"
        pl = sd.plan_particiones(d.sectores)
        if [(x.lba, x.n) for x in i["parts"]] != [(x.lba, x.n) for x in pl]:
            return "las particiones no siguen el plan"
        for k, (serie, carpetas) in ((2, (0x50AA0103, sd.CARPETAS_MODELOS)), (3, (0x50AA0104, sd.CARPETAS_USUARIO))):
            e = sd.ExFat(d, i["parts"][k].lba, i["parts"][k].n)
            if e.serie != serie or sorted(x[0] for x in e.listar()) != sorted(carpetas) or e.sect != i["parts"][k].n:
                return f"particion {k + 1}: serie {e.serie:#x} o carpetas {e.listar()}"
        if fsck:
            for k in (2, 3):
                q = i["parts"][k]
                x = Path(p).with_suffix(".p")
                with open(x, "wb") as o:
                    o.truncate(q.n * 512)
                    f.seek(q.lba * 512)
                    hecho = 0
                    while hecho < min(q.n * 512, 64 << 20):
                        b = f.read(8 << 20)
                        o.seek(hecho)
                        o.write(b)
                        hecho += len(b)
                r = subprocess.run([fsck, "-n", str(x)], capture_output=True, text=True)
                os.unlink(x)
                if r.returncode or "clean" not in r.stdout:
                    return "fsck.exfat: " + r.stdout[-300:]
    return ""


def main():
    tmp = Path(tempfile.mkdtemp(prefix="unid267_"))
    fsck = next((p for p in [os.path.join(d, "fsck.exfat") for d in [os.environ.get("EXFATPROGS", "")] + os.environ.get("PATH", "").split(os.pathsep) + ["/sbin", "/usr/sbin"]] if os.path.isfile(p)), None)
    try:
        prog = compila(tmp)
        ck(prog is not None, "compilar tests/test_unidades_267.c")
        if not prog:
            return
        # ---------------------------------------------------------- 1. plan
        rnd = random.Random(267)
        tot = [int(g * 1e9) // 512 for g in (4, 4.3, 4.5, 5.37, 7.45, 8, 14.9, 16, 24, 25, 29.8, 32, 64, 128, 1000, 2199, 2200, 4000)]
        tot += [rnd.randint(8_000_000, 9_000_000_000) for _ in range(60 if RAPIDO else 400)]
        distintos = 0
        for t in tot:
            for dn in (sd.DATOS_SECT, 1 << 23, (1 << 22) | 12345):
                try:
                    py = [f"{p.tipo} {p.lba} {p.n}" for p in sd.plan_particiones(t, dn)]
                except sd.SinSitio:
                    py = ["NO"]
                distintos += c(prog, "plan", t, dn).stdout.splitlines() != py
        ck(distintos == 0, f"unid_plan (C) igual que plan_particiones (Python) en {len(tot) * 3} casos ({distintos} distintos)")

        # ---------------------------------------------------------- 2. primer arranque
        base = tmp / "imagen20.img"
        with open(base, "wb") as f:
            f.truncate(sd.IMAGEN_SECT * 512)
        with open(base, "r+b") as f:
            sd.grabar(sd.Disco(f, sd.IMAGEN_SECT), b"E" * 100000, b"", b"\n" * 1024, log=lambda *a: None, imagen=True)
        with open(base, "r+b") as f:
            f.truncate(20 * 10 ** 9 // 512 * 512)                       # la imagen grabada en un pendrive de 20 GB
        st = tmp / "st.img"
        copia(base, st)
        n12 = (2048 + sd.SISTEMA_SECT + sd.DATOS_SECT - 1) * 512       # del sector 1 al final de DATOS (el MBR si cambia)
        h12 = huella(st, n12, 512)
        r = c(prog, "agrandar", st)
        ck(r.returncode == 0 and r.stdout.startswith("1 "), f"primer arranque: se agrandan ({r.stdout.strip()} {r.stderr[-300:]})")
        msg = final_correcto(st, fsck)
        ck(msg == "", f"quedan al tamano del plan, vacias, con sus carpetas y limpias ({msg})")
        ck(huella(st, n12, 512) == h12, "SISTEMA y DATOS identicos")
        r = c(prog, "agrandar", st)
        ck(r.stdout.startswith("0 ya ocupan"), f"segundo arranque: nada ({r.stdout.strip()})")
        r = c(prog, "ops", st)
        ck(r.returncode == 0, "operaciones de las unidades:\n" + r.stdout[-1500:] + r.stderr[-1500:])
        print("  " + (r.stdout.strip().splitlines() or ["?"])[-1])

        # ---------------------------------------------------------- 3. no tocar
        for que in ("usuario", "modelos"):
            x = tmp / f"lleno_{que}.img"
            copia(base, x)
            with open(x, "r+b") as f:
                d = sd.Disco(f, os.path.getsize(x) // 512)
                i = sd.analizar(d)
                q = i["parts"][3 if que == "usuario" else 2]
                e = sd.ExFat(d, q.lba, q.n)
                e.escribir("/Documentos/mia.txt" if que == "usuario" else "/voz/ggml-base-q5_1.bin", b"importante")
                e.cierra()
            h = huella(x) if not RAPIDO else None
            r = c(prog, "agrandar", x)
            ck(r.stdout.startswith("0 "), f"{que} con archivos: no se agranda ({r.stdout.strip()})")
            if h:
                ck(huella(x) == h, f"{que} con archivos: el pendrive no cambia")
            os.unlink(x)
        x = tmp / "justo.img"                                           # grabado por construyeimagen a su tamano
        with open(x, "wb") as f:
            f.truncate(20 * 10 ** 9 // 512 * 512)
        with open(x, "r+b") as f:
            sd.grabar(sd.Disco(f, os.path.getsize(x) // 512), b"E" * 1000, b"", b"\n" * 1024, log=lambda *a: None)
        r = c(prog, "agrandar", x)
        ck(r.stdout.startswith("0 "), f"pendrive grabado a su tamano: nada que agrandar ({r.stdout.strip()})")
        os.unlink(x)

        # ---------------------------------------------------------- 4. cortes de luz
        copia(base, tmp / "cuenta.img")
        r = c(prog, "agrandar", tmp / "cuenta.img", 10 ** 9)
        total = int(r.stdout.split("escrituras=")[1])
        os.unlink(tmp / "cuenta.img")
        ks = list(range(total + 1)) if not RAPIDO else sorted(set([0, 1, 2, total // 3, total // 2, total - 2, total - 1, total] + random.Random(1).sample(range(total), 6)))
        malos = []
        for k in ks:
            x = tmp / "corte.img"
            copia(base, x)
            r1 = c(prog, "agrandar", x, k)
            if r1.returncode == 3:
                malos.append((k, "escritura prohibida"))
                continue
            r2 = c(prog, "agrandar", x)
            m = final_correcto(x, None)
            if r1.stdout.startswith("1 ") and k < total:
                malos.append((k, "dijo que habia terminado con escrituras fallidas"))
            elif m:
                malos.append((k, f"{r1.stdout.strip()} -> {r2.stdout.strip()}: {m}"))
            os.unlink(x)
        ck(not malos, f"corte en cada una de las {len(ks)} escrituras ({total} en total): el siguiente arranque lo deja bien ({malos[:3]})")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc()
        fallos += 1
        print("FALLO: excepcion en las pruebas (arriba)")
    print("%s unidades (MODELOS y USUARIO en el nucleo): %d bien, %d fallos" % ("FALLO" if fallos else "ok ", ok, fallos))
    sys.exit(1 if fallos else 0)
