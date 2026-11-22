#!/usr/bin/env python3
"""SOMA_267_DISCOS: tools/somafs.py y tools/datos.py frente al codigo REAL del nucleo (tests/test_datos_267.c con src/fs/bigfs.c,
slotio.h y spillio.h).

  python3 tests/test_datos_267.py [--rapido]

1. Lo que escribe el nucleo (ranuras con diario, BIGFS /.ramfs) lo lee igual Python.
2. Lo que escribe Python (cientos de operaciones: nuevos, sustituir, pequeno<->grande, borrar, mas de 64 archivos) lo lee igual el
   nucleo, bigfs_fsck no encuentra nada y Python lo relee igual.
3. Cortes: una escritura comprometida en el diario y sin aplicar, y un temporal de BIGFS sin renombrar: Python ve lo mismo que vera
   el nucleo, y al escribir los termina igual que el.
4. tools/datos.py sobre un pendrive entero (imagen de soma_disco.py): listar, escribir, leer, borrar, exportar (sin secretos),
   comprobar; y sobre una DATOS sin usar dice que aun no hay nada.
"""
import os, random, shutil, subprocess, sys, tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ / "tools"))
import soma_disco as sd
import somafs as sf

RAPIDO = "--rapido" in sys.argv
ok = fallos = 0


def ck(c, msg):
    global ok, fallos
    if c:
        ok += 1
    else:
        fallos += 1
        print("FALLO:", msg)


def fnv(b):
    h = 1469598103934665603
    for x in b:
        h ^= x
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def compila(tmp):
    cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc") or shutil.which("clang")
    out = str(Path(tmp) / "test_datos_267")
    base = [cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-I", str(RAIZ / "src/fs"),
            str(RAIZ / "tests/test_datos_267.c"), str(RAIZ / "src/fs/bigfs.c"), "-o", out]
    for san in (["-fsanitize=address,undefined"], []):
        r = subprocess.run(base[:1] + san + base[1:], capture_output=True, text=True)
        if r.returncode == 0:
            return out
    print(r.stderr[-2000:])
    return None


def c(prog, *a):
    r = subprocess.run([prog] + [str(x) for x in a], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
    return r


def lista_c(prog, img):
    r = c(prog, "lee", img)
    lin = r.stdout.splitlines()
    fsck = [l for l in lin if l.startswith("FSCK")]
    malos = [l for l in lin if l.startswith("MAL")]
    return sorted(l for l in lin if not l.startswith(("FSCK", "MAL", "bigfs"))), (fsck[0] if fsck else "FSCK ?"), malos


def abre_py(img, escribir=False):
    f = open(img, "r+b" if escribir else "rb")
    n = os.path.getsize(img) // 512
    d = sd.Disco(f, n)
    return f, sf.Soma(sf.Region(d, 0, n), escribir=escribir)


def lista_py(s):
    return sorted(f"{n} {t} {fnv(s.leer(n))}" for n, t, _, _ in s.archivos())


def main():
    tmp = tempfile.mkdtemp(prefix="datos267_")
    try:
        prog = compila(tmp)
        ck(prog is not None, "compilar tests/test_datos_267.c con src/fs/bigfs.c")
        if not prog:
            return
        img = Path(tmp) / "soma.img"
        # ---------------------------------------------------------- 1. nucleo -> Python
        for semilla in ([1, 2] if RAPIDO else [1, 2, 3, 4, 5]):
            c(prog, "nuevo", img, 96)
            r = c(prog, "escribe", img, semilla, 120)
            esperado = sorted(r.stdout.splitlines())
            f, s = abre_py(img)
            ck(lista_py(s) == esperado and not s.avisos, f"semilla {semilla}: Python lee los {len(esperado)} archivos que escribio el nucleo ({s.avisos[:2]})")
            f.close()
        # ---------------------------------------------------------- 2. Python -> nucleo
        rnd = random.Random(267)
        c(prog, "nuevo", img, 160)
        modelo = {}
        f, s = abre_py(img, True)
        nops = 400 if RAPIDO else 900
        pico = grandes = 0
        for k in range(nops):
            pico = max(pico, len(modelo))
            t = rnd.random()
            if t < 0.55 or not modelo:
                nombre = rnd.choice(["/etc/", "/mem/", "/workspace/", "/agent/", "/mis_archivos/"]) + rnd.choice(["notas", "conf", "x", "ñandú", "datos"]) + f"_{rnd.randint(0, 120)}.txt"
                tam = rnd.choice([0, 1, 511, 512, 16383, 16384, rnd.randint(0, 16000), rnd.randint(16384, 400000)])
                b = rnd.randbytes(tam)
                if nombre not in modelo and len(modelo) >= 250:
                    continue
                s.escribir(nombre, b)
                modelo[nombre] = b
                grandes += len(b) >= 16384
            elif t < 0.8:
                nombre = rnd.choice(sorted(modelo))
                s.borrar(nombre)
                del modelo[nombre]
            else:
                nombre = rnd.choice(sorted(modelo))
                b = rnd.randbytes(rnd.choice([10, 20000, 70000, 15000]))
                s.escribir(nombre, b)
                modelo[nombre] = b
            if k % 97 == 96:                              # volver a abrir de vez en cuando
                f.close()
                f, s = abre_py(img, True)
        f.close()
        esperado = sorted(f"{n} {len(b)} {fnv(b)}" for n, b in modelo.items())
        f, s = abre_py(img)
        ck(lista_py(s) == esperado, f"Python relee lo que escribio ({len(esperado)} archivos)")
        f.close()
        lst, fsck, malos = lista_c(prog, img)
        ck(lst == esperado and not malos, f"el nucleo lee lo que escribio Python ({len(lst)}/{len(esperado)}; {malos[:3]})")
        ck(fsck == "FSCK 0 0", f"bigfs_fsck sin problemas ({fsck})")
        ck(grandes > 20 and pico > 64, f"la prueba cubre archivos grandes ({grandes}) y mas de 64 archivos (pico {pico})")

        # ---------------------------------------------------------- 3. cortes
        c(prog, "nuevo", img, 96)
        base = sorted(c(prog, "escribe", img, 7, 30).stdout.splitlines())
        nombre_slot3 = base and [l for l in c(prog, "lee", img).stdout.splitlines()][3].split()[0]
        corte = c(prog, "corta", img, 3, nombre_slot3).stdout.strip()
        esperado = sorted([l for l in base if not l.startswith(nombre_slot3 + " ")] + [corte])
        f, s = abre_py(img)
        ck(s.diario_pendiente is not None and lista_py(s) == esperado, "diario comprometido sin aplicar: Python (solo lectura) ve la version nueva, como el nucleo")
        f.close()
        f, s = abre_py(img, True)                          # al escribir, lo termina como slotio_recover
        s.escribir("/etc/despues_del_corte.txt", b"hola")
        f.close()
        esperado.append("/etc/despues_del_corte.txt 4 " + fnv(b"hola"))
        esperado.sort()
        lst, fsck, malos = lista_c(prog, img)
        ck(lst == esperado and fsck == "FSCK 0 0" and not malos, "y tras escribir, el nucleo ve lo mismo (diario terminado)")
        # temporal de BIGFS sin renombrar (corte en spill_put_atomic): se promociona si falta el definitivo
        f, s = abre_py(img, True)
        h = bytearray(64)
        h[0:4] = b"RFS1"
        h[4:4 + len(b"/mem/temporal.bin")] = b"/mem/temporal.bin"
        datos = random.Random(3).randbytes(50000)
        h[52:56] = len(datos).to_bytes(4, "little")
        s.big.crear(sf.spill_tmp(77), bytes(h) + datos)
        f.close()
        f, s = abre_py(img)
        ck(any(n == "/mem/temporal.bin" for n, *_ in s.archivos()), "temporal de BIGFS completo: Python lo ve (lo que haria spill_recover)")
        f.close()
        lst, fsck, malos = lista_c(prog, img)
        ck(f"/mem/temporal.bin 50000 {fnv(datos)}" in lst, "y el nucleo lo promociona igual")

        # ---------------------------------------------------------- 4. tools/datos.py sobre un pendrive
        pend = Path(tmp) / "pendrive.img"
        with open(pend, "wb") as fp:
            fp.truncate(sd.IMAGEN_SECT * 512)
        with open(pend, "r+b") as fp:
            d = sd.Disco(fp, sd.IMAGEN_SECT)
            sd.grabar(d, b"EFI" * 1000, b"", b"\n" * 4096, log=lambda *a: None, imagen=True)
            info = sd.analizar(d)
        datos_py = [sys.executable, str(RAIZ / "tools/datos.py"), str(pend)]
        r = subprocess.run(datos_py + ["listar"], capture_output=True, text=True)
        ck(r.returncode != 0 and "primer arranque" in (r.stdout + r.stderr) + "" or "arranca SOMA" in (r.stdout + r.stderr), f"datos.py: DATOS sin usar -> lo dice ({(r.stdout + r.stderr).strip()[-120:]})")
        # SOMA arranca: formatea su disco dentro de DATOS (aqui, con el nucleo real sobre la region de datos)
        p = info["datos"]
        region = Path(tmp) / "region.img"
        c(prog, "nuevo", region, 96)
        c(prog, "escribe", region, 11, 20)
        with open(region, "rb") as fr, open(pend, "r+b") as fp:
            fp.seek((p.lba + sd.UD_DATA_OFF) * 512)
            fp.write(fr.read())
        (Path(tmp) / "hosts.txt").write_bytes(b"127.0.0.1 soma\n")
        (Path(tmp) / "grande.bin").write_bytes(random.Random(8).randbytes(100000))
        r1 = subprocess.run(datos_py + ["escribir", "/etc/hosts", str(Path(tmp) / "hosts.txt")], capture_output=True, text=True)
        r2 = subprocess.run(datos_py + ["escribir", "/mem/grande.bin", str(Path(tmp) / "grande.bin")], capture_output=True, text=True)
        r3 = subprocess.run(datos_py + ["leer", "/etc/hosts"], capture_output=True)
        r4 = subprocess.run(datos_py + ["leer", "/mem/grande.bin", str(Path(tmp) / "vuelta.bin")], capture_output=True, text=True)
        ck(r1.returncode == 0 and r2.returncode == 0 and r3.stdout == b"127.0.0.1 soma\n" and (Path(tmp) / "vuelta.bin").read_bytes() == (Path(tmp) / "grande.bin").read_bytes(),
           f"datos.py: escribir y leer (pequeno y grande) {r1.stderr[-200:]} {r2.stderr[-200:]}")
        r = subprocess.run(datos_py + ["listar"], capture_output=True, text=True)
        ck(r.returncode == 0 and "/etc/hosts" in r.stdout and "22 archivos" in r.stdout, f"datos.py: listar ({r.stdout[-200:]}{r.stderr[-200:]})")
        (Path(tmp) / "secreto.txt").write_bytes(b"clave-wifi")
        subprocess.run(datos_py + ["escribir", "/etc/wifi.conf", str(Path(tmp) / "secreto.txt")], capture_output=True)
        exp = Path(tmp) / "copia"
        r = subprocess.run(datos_py + ["exportar", str(exp)], capture_output=True, text=True)
        ck(r.returncode == 0 and (exp / "soma/etc/hosts").read_bytes() == b"127.0.0.1 soma\n" and not (exp / "soma/etc/wifi.conf").exists() and "omitidos" in r.stdout,
           f"datos.py: exportar sin secretos ({r.stdout.strip()[-150:]})")
        r = subprocess.run(datos_py + ["borrar", "/etc/hosts"], capture_output=True, text=True)
        r2 = subprocess.run(datos_py + ["comprobar"], capture_output=True, text=True)
        ck(r.returncode == 0 and r2.returncode == 0 and "todo bien" in r2.stdout, f"datos.py: borrar y comprobar ({r2.stdout.strip()[-200:]})")
        with open(pend, "rb") as fp, open(region, "wb") as fr:          # y el nucleo lo lee igual
            fp.seek((p.lba + sd.UD_DATA_OFF) * 512)
            fr.write(fp.read(96 << 20))
        lst, fsck, malos = lista_c(prog, region)
        nombres = [l.split()[0] for l in lst]
        ck("/mem/grande.bin" in nombres and "/etc/hosts" not in nombres and "/etc/wifi.conf" in nombres and fsck == "FSCK 0 0" and not malos,
           "el nucleo lee lo que dejo datos.py en el pendrive")
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
    print("%s DATOS (somafs/datos.py frente al nucleo): %d bien, %d fallos" % ("FALLO" if fallos else "ok ", ok, fallos))
    sys.exit(1 if fallos else 0)
