#!/usr/bin/env python3
"""SOMA_265_CC: pruebas del compilador de C de SOMA en el ordenador de desarrollo (solo Linux x86_64).

    python3 tests/test_cc_265.py                 construye cc.elf como el Makefile (con el gcc del equipo) y lo prueba
    python3 tests/test_cc_265.py --elf build/x86_64/user/cc.elf     prueba el que ya construyo make
    python3 tests/test_cc_265.py --rapido        sin las comparaciones masivas con glibc

Ejecuta los MISMOS .elf que cargaria el nucleo con tests/somaemu_265.c (ptrace: cada llamada al sistema se atiende como en
src/arch/x86_64/usermode.c; ninguna llega a Linux) y valida cada ejecutable con src/core/elf.c, el validador del nucleo.
  1) printf, strtod/strtof/strtold y matematicas: salida identica a la de glibc (miles de valores al azar)
  2) archivos, entrada, memoria, setjmp, estructuras, varios archivos, ventana sin escritorio, errores y fallos
  3) los ejecutables cumplen las reglas de SOMA: ELF estatico, W^X, <= 4 segmentos, <= 12 MiB
En macOS o Windows no se puede (ptrace de Linux): prueba en QEMU con tests/prueba_cc_265.py."""
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CCD = os.path.join(RAIZ, "src", "user", "cc")
TCC = os.path.join(RAIZ, "third_party", "tinycc")
PR = os.path.join(RAIZ, "tests", "cc_265")
HOST_CC = os.environ.get("HOST_CC", "cc")
TCC_DEFS = ["-DONE_SOURCE=1", "-DTCC_TARGET_X86_64", "-DCONFIG_TCC_NO_NATIVE", "-DCONFIG_TCC_STATIC", "-DCONFIG_TCC_BACKTRACE=0",
            "-DCONFIG_TCC_BCHECK=0", "-DCONFIG_TCC_SEMLOCK=0", "-DCONFIG_TCC_PREDEFS=1"]
bien = mal = 0


def nota(ok, desc, extra=""):
    global bien, mal
    if ok:
        bien += 1
    else:
        mal += 1
    print(("  ok    " if ok else "  FALLO ") + desc + (("\n" + extra) if (extra and not ok) else ""))


def run(a, **k):
    return subprocess.run(a, capture_output=True, text=True, errors="replace", **k)


def construye_cc(td):
    """cc.elf con las mismas opciones que el Makefile (USER_CFLAGS de SOMA), sin las cabeceras del sistema"""
    gi = run([HOST_CC, "-print-file-name=include"]).stdout.strip()
    b = os.path.join(td, "ccb")
    os.makedirs(b)
    run([sys.executable, os.path.join(CCD, "cc_fs.py"), RAIZ, os.path.join(b, "cc_fs.inc")]).check_returncode()
    base = ["-m64", "-march=x86-64", "-ffreestanding", "-O2", "-Wall", "-Wextra", "-fno-strict-aliasing", "-fno-delete-null-pointer-checks",
            "-fwrapv", "-fno-stack-protector", "-fPIE", "-mno-red-zone", "-nostdlib", "-static", "-msse2", "-mfpmath=sse",
            "-fno-tree-vectorize", "-U_FORTIFY_SOURCE", "-D_FORTIFY_SOURCE=0", "-nostdinc", "-isystem", os.path.join(CCD, "include"),
            "-isystem", gi, "-I" + CCD, "-I" + TCC, "-I" + b]
    pasos = [base + TCC_DEFS + ["-w", "-c", os.path.join(TCC, "libtcc.c"), "-o", b + "/libtcc.o"],
             base + ["-DSOMA_HEAP_SIZE=(10u << 20)", "-Werror", "-c", os.path.join(CCD, "libc.c"), "-o", b + "/libc.o"],
             base + ["-Werror", "-c", os.path.join(CCD, "cc.c"), "-o", b + "/cc.o"],
             base + ["-Wl,-T," + os.path.join(RAIZ, "src", "user", "user.ld"), "-Wl,-z,max-page-size=4096", "-Wl,--build-id=none",
                     "-Wl,--no-relax", b + "/cc.o", b + "/libtcc.o", b + "/libc.o", "-lgcc", "-o", b + "/cc.elf"]]
    for p in pasos:
        r = run([HOST_CC] + p)
        if r.returncode:
            print(r.stderr[-3000:])
            sys.exit("no se pudo construir cc.elf")
    return b + "/cc.elf"


def main():
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        print("test_cc_265: necesita Linux x86_64 (ptrace); aqui se omite. En QEMU: python3 tests/prueba_cc_265.py")
        return 0
    rapido = "--rapido" in sys.argv
    td = tempfile.mkdtemp(prefix="cc265_")
    try:
        emu, chk = os.path.join(td, "somaemu"), os.path.join(td, "elfcheck")
        run([HOST_CC, "-O1", "-o", emu, os.path.join(RAIZ, "tests", "somaemu_265.c")]).check_returncode()
        run([HOST_CC, "-O1", "-I" + os.path.join(RAIZ, "src", "core"), "-o", chk, os.path.join(RAIZ, "tests", "elfcheck_265.c"),
             os.path.join(RAIZ, "src", "core", "elf.c")]).check_returncode()
        elf = sys.argv[sys.argv.index("--elf") + 1] if "--elf" in sys.argv else construye_cc(td)
        r = run([chk, elf])
        nota(r.returncode == 0, "cc.elf es un ejecutable valido para SOMA: " + r.stdout.strip())
        raiz = os.path.join(td, "r")
        ws = os.path.join(raiz, "workspace")
        os.makedirs(ws)
        for f in os.listdir(PR):
            shutil.copy(os.path.join(PR, f), ws)

        def cc(args, plazo=60000):
            return run([emu, "-r", raiz, "-t", str(plazo), "-a", args, elf])

        def ejecuta(nombre, args="", entrada=None, plazo=60000):
            p = os.path.join(ws, nombre)
            if not os.path.exists(p):
                return None
            os.chmod(p, 0o755)
            a = [emu, "-r", raiz, "-t", str(plazo), "-a", args]
            if entrada is not None:
                ent = os.path.join(td, "entrada.txt")
                open(ent, "w").write(entrada)
                a += ["-i", ent]
            return run(a + [p])

        print("1) coma flotante y numeros, comparados con glibc")
        casos = [("fmt", ""), ("num", "")] + ([] if rapido else [("azar", "20000"), ("azar2", "50000"), ("azar3", "4000")])
        for n, a in casos:
            ref = os.path.join(td, "ref_" + n)
            r = run([HOST_CC, "-O0", "-w", "-o", ref, os.path.join(PR, n + ".c"), "-lm"])
            if r.returncode:
                nota(False, f"{n}: no compila con {HOST_CC}", r.stderr); continue
            esperado = run([ref] + ([a] if a else [])).stdout
            c = cc(n + ".c")
            if c.returncode:
                nota(False, f"{n}.c compila", c.stdout + c.stderr); continue
            p = ejecuta(n + ".elf", a, plazo=180000)
            difs = sum(1 for x, y in zip(esperado.split("\n"), p.stdout.split("\n")) if x != y) + abs(esperado.count("\n") - p.stdout.count("\n"))
            nota(p.returncode == 0 and difs == 0, f"{n}: {esperado.count(chr(10))} lineas identicas a glibc" if difs == 0 else f"{n}: {difs} lineas distintas de glibc",
                 "\n".join(f"  glibc: {x}\n  soma:  {y}" for x, y in list(zip(esperado.split("\n"), p.stdout.split("\n"))) if x != y)[:3000])
            v = run([chk, os.path.join(ws, n + ".elf")])
            nota(v.returncode == 0, f"{n}.elf pasa elf_parse: " + v.stdout.strip().split(": ", 1)[-1])

        print("2) programas")
        c = cc("archivos.c")
        p = ejecuta("archivos.elf")
        sal = p.stdout if p else c.stdout + c.stderr
        for rx, d in [(r"leida: linea 2 3\.00", "escribir y leer un archivo"), (r"leida: extra", "abrir para anadir ('a')"),
                      (r"lineas=6\ntam=\d+", "fseek/ftell"), (r"fscanf: linea 1 1\.50", "fscanf"), (r"binario: 256 bytes, iguales=1", "fread/fwrite binario"),
                      (r"escribir fuera de /workspace: denegado", "no se escribe fuera de /workspace"),
                      (r"abrir inexistente: falla \(no existe\)", "archivo inexistente: ENOENT"), (r"realpath: /workspace/x/y\.c", "rutas relativas en /workspace")]:
            nota(re.search(rx, sal) is not None, d, sal)
        nota(os.path.isfile(os.path.join(ws, "datos.txt")) and "linea 5 7.50" in open(os.path.join(ws, "datos.txt")).read(), "el archivo queda en el VFS")
        c = cc("entrada.c")
        p = ejecuta("entrada.elf", entrada="Ana\n3 4\n10 20\n30\n")
        sal = p.stdout if p else c.stdout + c.stderr
        nota("hola, Ana" in sal and "a+b=7" in sal and "suma=60" in sal and "fin de la entrada: 1" in sal, "entrada por stdin (fgets, scanf, fin)", sal)
        c = cc("varios.c")
        p = ejecuta("varios.elf")
        sal = (p.stdout if p else c.stdout + c.stderr)
        for t in ["longjmp=42", "recursion=8000", "albaricoque kiwi manzana pera uva", "malloc: suma=", "realloc hasta 4096 KiB ok",
                  "calloc ceros=1", "malloc de 100 MiB: NULL", "[uno][dos][tres][cuatro]", "struct: 76 2", "long double: 0.5", "fecha valida: 1",
                  "1537228672809129301 2635249153387078802 88", "fin", "atexit ejecutado"]:
            nota(t in sal, "varios: " + t, sal)
        nota(p is not None and p.returncode == 3, "codigo de salida de main (3)")
        c = cc("-o dos.elf dos_a.c dos_b.c")
        p = ejecuta("dos.elf")
        nota(p is not None and "dos archivos: 42" in p.stdout, "programa en dos archivos (-o)", c.stdout + c.stderr)
        c = cc("-c dos_b.c")
        nota(c.returncode == 0 and os.path.isfile(os.path.join(ws, "dos_b.o")), "-c: objeto suelto")
        c = cc("ventana.c")
        p = ejecuta("ventana.elf")
        nota(p is not None and "sin escritorio" in p.stdout, "soma.h: sin escritorio la ventana no se abre y el programa sigue", c.stdout + c.stderr)
        c = cc("error.c")
        e = c.stdout + c.stderr
        nota(c.returncode == 1 and re.search(r"error\.c:4: error: ", e) is not None and not os.path.exists(os.path.join(ws, "error.elf")),
             "error de compilacion: archivo:linea: error: y sin ejecutable", e)
        c = cc("fallo.c")
        p = ejecuta("fallo.elf")
        nota(p is not None and p.returncode == 125 and "antes" in p.stdout and "despues" not in p.stdout, "un puntero nulo mata solo al programa", (p.stdout + p.stderr) if p else "")
        c = cc("noexiste.c")
        nota(c.returncode == 1 and "cc: no se puede leer /workspace/noexiste.c (no existe)" in c.stdout + c.stderr, "archivo que no existe: lo dice en castellano", c.stdout + c.stderr)
        c = cc("/etc/llm.conf")
        nota(c.returncode == 1 and "ruta privada" in c.stdout + c.stderr, "no compila rutas privadas", c.stdout + c.stderr)
        c = cc("grande.c")
        nota(c.returncode == 1 and "no cabe en memoria" in c.stdout + c.stderr, "programa de mas de 12 MiB: lo dice el compilador", c.stdout + c.stderr)
        c = cc("-heap 4 -o grande4.elf grande.c")
        p = ejecuta("grande4.elf")
        nota(c.returncode == 0 and p is not None and p.returncode == 1, "con -heap 4 si cabe", c.stdout + c.stderr)
        shutil.copy(os.path.join(PR, "varios.c"), os.path.join(td, "src.c"))
        r = run([emu, "-r", raiz, "-a", "-s", "-s", os.path.join(td, "src.c"), elf])
        nota(r.returncode == 0, "cc -s: comprueba el codigo inyectado (programador)", r.stdout + r.stderr)
        shutil.copy(os.path.join(PR, "error.c"), os.path.join(td, "src.c"))
        r = run([emu, "-r", raiz, "-a", "-s", "-s", os.path.join(td, "src.c"), elf])
        nota(r.returncode == 1 and re.search(r"(^|\n)programa\.c:4: error:", r.stdout + r.stderr) is not None, "cc -s con un error: lo cuenta con su linea", r.stdout + r.stderr)
        shutil.copy(os.path.join(PR, "entrada.c"), os.path.join(td, "src.c"))
        r = run([emu, "-r", raiz, "-a", "-x", "-s", os.path.join(td, "src.c"), elf])
        p = ejecuta(".programa.elf", entrada="Luis\n1 2\n")
        nota(r.returncode == 0 and p is not None and "hola, Luis" in p.stdout, "cc -x: compila el codigo inyectado a /workspace/.programa.elf", r.stdout + r.stderr)

        print("2b) los ejemplos que el programador da al modelo (src/apps/coder_core.c) compilan y funcionan")
        fuente = open(os.path.join(RAIZ, "src", "apps", "coder_core.c"), encoding="utf-8").read()
        for nombre, esperado in (("EX_CLI_C", "Suma: 25\n1 es impar\n2 es par\n3 es impar\n"), ("EX_GUI_C", None)):
            m = re.search(r"static const char " + nombre + r"\[\] =\s*(.*?);\n", fuente, re.S)
            if not m:
                nota(False, nombre + ": no esta en coder_core.c"); continue
            codigo = "".join(bytes(t, "latin-1").decode("unicode_escape") for t in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
            open(os.path.join(ws, nombre + ".c"), "w").write(codigo)
            c = cc(nombre + ".c")
            avisos = "warning" in c.stdout + c.stderr
            p = ejecuta(nombre + ".elf")
            if esperado is None:
                nota(c.returncode == 0 and not avisos and p is not None and p.returncode == 1, nombre + ": compila sin avisos (sin escritorio, sale con 1)", c.stdout + c.stderr)
            else:
                nota(c.returncode == 0 and not avisos and p is not None and p.stdout == esperado, nombre + ": compila sin avisos y da la salida esperada", c.stdout + c.stderr + (p.stdout if p else ""))

        print("3) reglas de SOMA en todos los ejecutables")
        nota(os.path.getsize(os.path.join(ws, "grande.elf")) == 0, "el ejecutable que no cabe queda vacio (no se puede cargar)")
        elfs = sorted(f for f in os.listdir(ws) if f.endswith(".elf") and f != "grande.elf")
        r = run([chk] + [os.path.join(ws, f) for f in elfs])
        nota(r.returncode == 0, f"{len(elfs)} ejecutables validos (W^X, estaticos, <= 4 segmentos, <= 12 MiB)", r.stdout)
    finally:
        shutil.rmtree(td, ignore_errors=True)
    print(f"test_cc_265: {bien}/{bien + mal} bien")
    return 1 if mal else 0


if __name__ == "__main__":
    sys.exit(main())
