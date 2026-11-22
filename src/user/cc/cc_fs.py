#!/usr/bin/env python3
"""SOMA_265_CC: genera cc_fs.inc, los archivos que el compilador de C lleva dentro (/tcc/include/..., /tcc/lib/...).
    python3 src/user/cc/cc_fs.py SOMA_RAIZ salida.inc
Cabeceras: las de TinyCC (third_party/tinycc/include) y las de la biblioteca de SOMA (src/user/cc/include, mandan estas).
Biblioteca: src/user/cc/libc.c y soma_sys.h, y de TinyCC lib/libtcc1.c, va_list.c, builtin.c, alloca.S, stdatomic.c y atomic.S."""
import os
import sys


def main():
    raiz, sal = sys.argv[1], sys.argv[2]
    tcc = os.path.join(raiz, "third_party", "tinycc")
    cc = os.path.join(raiz, "src", "user", "cc")
    ent = {}
    for base, dest in ((os.path.join(tcc, "include"), "/tcc/include"), (os.path.join(cc, "include"), "/tcc/include")):
        for d, _, fs in os.walk(base):
            for f in sorted(fs):
                if f.endswith(".h"):
                    p = os.path.join(d, f)
                    ent[dest + "/" + os.path.relpath(p, base).replace(os.sep, "/")] = p
    for f in ("libc.c", "soma_sys.h"):
        ent["/tcc/lib/" + f] = os.path.join(cc, f)
    for f in ("libtcc1.c", "va_list.c", "builtin.c", "alloca.S", "stdatomic.c", "atomic.S"):
        ent["/tcc/lib/" + f] = os.path.join(tcc, "lib", f)
    out = ["/* SOMA_265_CC: generado por src/user/cc/cc_fs.py (no editar): archivos que el compilador lleva dentro */"]
    nombres = []
    for i, (ruta, p) in enumerate(sorted(ent.items())):
        b = open(p, "rb").read()
        lin = []
        for k in range(0, len(b), 24):
            lin.append(",".join(str(x) for x in b[k:k + 24]))
        out.append(f"static const unsigned char ccf_{i}[{len(b) + 1}] = {{\n" + ",\n".join(lin) + (",0};" if b else "0};"))
        nombres.append(f'    {{ "{ruta}", ccf_{i}, {len(b)} }},')
    out.append("const struct __fs_ent __soma_fs[] = {\n" + "\n".join(nombres) + "\n    { 0, 0, 0 }\n};")
    open(sal, "w").write("\n".join(out) + "\n")
    print(f"{sal}: {len(ent)} archivos, {sum(os.path.getsize(p) for p in ent.values()) // 1024} KB")


if __name__ == "__main__":
    main()
