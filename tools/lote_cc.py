#!/usr/bin/env python3
"""SOMA_328_COMPILA_RAPIDO: compila un lote de fuentes en paralelo y solo las que han cambiado.

Sustituye a los bucles en serie del Makefile (for f in ...; do cc -c $f; done), que recompilaban TODOS los archivos de un
programa de ring 3 (navegador aislado, Python, reproductor, voz...) cada vez que cambiaba uno, de uno en uno.

    python3 tools/lote_cc.py DIR LOTE PREFIJO NOMBRE ORDEN... ::: FUENTE...

  DIR      carpeta de los objetos
  LOTE     nombre del lote (varios lotes pueden compartir carpeta)
  PREFIJO  delante del nombre de cada objeto ("-" = ninguno; p. ej. "avc_")
  NOMBRE   "base": DIR/PREFIJO<nombre sin .c>.o   "ruta": DIR/PREFIJO<ruta con / cambiadas por _>.o
  ORDEN    la orden de compilacion sin -c ni -o, tal cual

Un objeto se recompila si no existe, si cambio la orden con la que se hizo (archivo .cmd) o si es mas viejo que la fuente o que
alguna de las cabeceras que la fuente incluyo la ultima vez (archivo .d de gcc -MMD). Los objetos que el lote hizo en otra
ocasion y ya no corresponden a ninguna fuente se borran (si no, se enlazarian con *.o).
Trabajos a la vez: SOMA_JOBS, o el numero de procesadores.
"""
import os
import shlex
import subprocess
import sys
import select
import time


class Jobserver:
    """Cliente del servidor de trabajos de GNU make (make -jN): asi el lote no lanza mas compilaciones que las que make permite
    en total. Sin el (make sin -j, o la orden sin "+"), el lote usa SOMA_JOBS o el numero de procesadores."""
    def __init__(self):
        self.ok = False
        self.r = self.w = None
        for tok in os.environ.get("MAKEFLAGS", "").split():
            for pre in ("--jobserver-auth=", "--jobserver-fds="):
                if tok.startswith(pre):
                    v = tok[len(pre):]
                    try:
                        if v.startswith("fifo:"):
                            self.r = os.open(v[5:], os.O_RDONLY | os.O_NONBLOCK)
                            self.w = os.open(v[5:], os.O_WRONLY)
                        else:
                            a, b = v.split(",")
                            self.r, self.w = int(a), int(b)
                            if self.r < 0 or self.w < 0:
                                return
                            os.fstat(self.r)
                            os.fstat(self.w)
                        self.ok = True
                    except (OSError, ValueError):
                        self.ok = False

    def toma(self, bloquea=False):
        # sin tocar el modo del descriptor (lo comparte make): se mira si hay ficha y solo entonces se lee
        try:
            if not select.select([self.r], [], [], 0)[0]:
                return None
            b = os.read(self.r, 1)
            return b if b else None
        except (BlockingIOError, InterruptedError):
            return None
        except OSError:
            self.ok = False
            return None

    def devuelve(self, ficha):
        if ficha is not None:
            try:
                os.write(self.w, ficha)
            except OSError:
                pass


def deps(dfile):
    try:
        txt = open(dfile, encoding="utf-8", errors="replace").read()
    except OSError:
        return None
    txt = txt.replace("\\\n", " ")
    out = []
    for linea in txt.splitlines():
        if ":" not in linea:
            continue
        _, _, resto = linea.partition(":")
        out += [t for t in resto.split() if t]
    return out


def al_dia(src, obj, cmdfile, dfile, orden):
    try:
        to = os.path.getmtime(obj)
        if open(cmdfile, encoding="utf-8").read() != orden:
            return False
    except OSError:
        return False
    ds = deps(dfile)
    if ds is None:
        return False
    for d in [src] + ds:
        try:
            if os.path.getmtime(d) > to:
                return False
        except OSError:
            return False              # una cabecera que ya no existe: se recompila (y gcc dira si falta)
    return True


def main(argv):
    if len(argv) < 7 or ":::" not in argv:
        print(__doc__)
        return 2
    k = argv.index(":::")
    d, lote, pre, modo = argv[1], argv[2], argv[3], argv[4]
    if pre == "-":
        pre = ""
    orden = shlex.join(argv[5:k])
    fuentes = argv[k + 1:]
    if modo not in ("base", "ruta") or k <= 5:
        print(__doc__)
        return 2
    os.makedirs(d, exist_ok=True)
    trabajos = []
    nuevos = set()
    for src in fuentes:
        if modo == "ruta":
            nombre = src.replace("/", "_")
        else:
            nombre = os.path.basename(src)
            for ext in (".cpp", ".c", ".S", ".s"):
                if nombre.endswith(ext):
                    nombre = nombre[:-len(ext)]
                    break
        obj = os.path.join(d, pre + nombre + ".o")
        nuevos.add(os.path.basename(obj))
        trabajos.append((src, obj, orden))
    # objetos de una vez anterior que ya no tienen fuente
    manif = os.path.join(d, ".lote_" + lote)
    try:
        viejos = set(open(manif, encoding="utf-8").read().split())
    except OSError:
        viejos = set()
    for v in viejos - nuevos:
        for f in (v, v[:-2] + ".d", v[:-2] + ".cmd"):
            try:
                os.remove(os.path.join(d, f))
            except OSError:
                pass
    pendientes = [t for t in trabajos if not al_dia(t[0], t[1], t[1][:-2] + ".cmd", t[1][:-2] + ".d", t[2])]

    def lanza(t):
        src, obj, o = t
        dfile, cmdfile = obj[:-2] + ".d", obj[:-2] + ".cmd"
        try:
            os.remove(cmdfile)
        except OSError:
            pass
        args = shlex.split(o) + ["-MMD", "-MF", dfile, "-c", src, "-o", obj]
        return subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")

    js = Jobserver()
    n = int(os.environ.get("SOMA_JOBS") or 0) or os.cpu_count() or 2
    fallos = 0
    if pendientes:
        print(f"[lote] {d}: {len(pendientes)} de {len(trabajos)} archivos que compilar"
              + (" (con los trabajos de make)" if js.ok else f" ({n} a la vez)"), flush=True)
        cola = list(pendientes)
        vivos = []                     # (proceso, trabajo, ficha de make o None)
        while cola or vivos:
            while cola and not fallos:
                if not vivos:
                    ficha = None       # el primer trabajo usa la ficha propia que make da a esta orden
                elif js.ok:
                    ficha = js.toma(bloquea=False)
                    if ficha is None:
                        break
                elif len(vivos) >= n:
                    break
                else:
                    ficha = None
                t = cola.pop(0)
                vivos.append((lanza(t), t, ficha))
            if fallos:
                cola = []
            for i, (pr, t, ficha) in enumerate(vivos):
                if pr.poll() is not None:
                    salida = pr.stdout.read()
                    pr.stdout.close()
                    if salida.strip():
                        sys.stdout.write(salida)
                    if pr.returncode:
                        fallos += 1
                        print(f"[lote] ERROR al compilar {t[0]}", flush=True)
                    else:
                        with open(t[1][:-2] + ".cmd", "w", encoding="utf-8") as f:
                            f.write(t[2])
                    js.devuelve(ficha)
                    del vivos[i]
                    break
            else:
                time.sleep(0.01)
    with open(manif, "w", encoding="utf-8") as f:
        f.write("\n".join(sorted(nuevos)) + "\n")
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
