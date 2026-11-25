#!/usr/bin/env python3
"""SOMA_310_PRESUPUESTO: cuanto ocupa el nucleo frente a su limite (x86_64).

La imagen del nucleo (codigo, datos y .bss) tiene que acabar por debajo de la direccion que fija linker_x86_64.ld
(ASSERT(__kernel_image_end <= ...), hoy 46 MiB: lo que hay despues es de la virtqueue y del PMM). Si se pasa, el enlazador
falla de golpe. Este script lo dice ANTES: cuanto ocupa cada parte, los simbolos mas grandes y que porcentaje del limite se usa.

    python3 tools/size_check.py [build/x86_64/soma.elf] [--avisar-en 85] [--fallar-en 90] [--top 12]

Sale con 1 si se pasa de --fallar-en (la CI usa 90) y avisa a partir de --avisar-en. Python puro (lee el ELF el mismo, sin nm):
funciona igual en Linux y en macOS.
"""
import argparse
import re
import struct
import sys
from pathlib import Path


def leer_elf(datos: bytes):
    """Secciones (nombre, direccion, tamano, tipo, flags) y simbolos (nombre, valor, tamano, tipo) de un ELF64 little-endian."""
    if datos[:4] != b"\x7fELF" or datos[4] != 2 or datos[5] != 1:
        raise ValueError("no es un ELF de 64 bits little-endian")
    shoff, = struct.unpack_from("<Q", datos, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", datos, 0x3A)
    if shoff == 0 or shentsize < 64 or shoff + shnum * shentsize > len(datos):
        raise ValueError("tabla de secciones invalida")
    raw = []
    for i in range(shnum):
        raw.append(struct.unpack_from("<IIQQQQIIQQ", datos, shoff + i * shentsize))
    def nombre(tabla_off, tabla_tam, idx):
        if idx >= tabla_tam:
            return ""
        fin = datos.find(b"\x00", tabla_off + idx, tabla_off + tabla_tam)
        return datos[tabla_off + idx:fin if fin >= 0 else tabla_off + tabla_tam].decode("utf-8", "replace")
    sh_str = raw[shstrndx] if shstrndx < len(raw) else None
    secciones = []
    for s in raw:
        n = nombre(sh_str[4], sh_str[5], s[0]) if sh_str else ""
        secciones.append({"nombre": n, "tipo": s[1], "flags": s[2], "dir": s[3], "off": s[4], "tam": s[5], "link": s[6], "entsize": s[9]})
    simbolos = []
    for s in secciones:
        if s["tipo"] != 2:                                   # SHT_SYMTAB
            continue
        st = secciones[s["link"]] if s["link"] < len(secciones) else None
        ent = s["entsize"] or 24
        for k in range(s["tam"] // ent):
            st_name, st_info, _oth, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", datos, s["off"] + k * ent)
            n = nombre(st["off"], st["tam"], st_name) if st else ""
            simbolos.append({"nombre": n, "valor": st_value, "tam": st_size, "tipo": st_info & 15, "shndx": st_shndx})
    return secciones, simbolos


def limite_del_enlazador(ld: Path):
    m = re.search(r"ASSERT\(\s*__kernel_image_end\s*<=\s*(0x[0-9A-Fa-f]+|\d+)", ld.read_text(encoding="utf-8")) if ld.is_file() else None
    return int(m.group(1), 0) if m else None


def mib(n):
    return "%.1f MiB" % (n / (1024.0 * 1024.0))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", nargs="?", default="build/x86_64/soma.elf")
    ap.add_argument("--ld", default="linker_x86_64.ld")
    ap.add_argument("--avisar-en", type=float, default=85.0)
    ap.add_argument("--fallar-en", type=float, default=100.0)
    ap.add_argument("--top", type=int, default=12)
    a = ap.parse_args(argv)
    elf = Path(a.elf)
    if not elf.is_file():
        print("size-check: no existe %s (compila antes con make)" % elf)
        return 2
    try:
        secciones, simbolos = leer_elf(elf.read_bytes())
    except (ValueError, struct.error) as e:
        print("size-check: %s: %s" % (elf, e))
        return 2
    limite = limite_del_enlazador(Path(a.ld))
    fin = next((s["valor"] for s in simbolos if s["nombre"] == "__kernel_image_end"), None)
    if limite is None or fin is None:
        print("size-check: falta %s" % ("el ASSERT de __kernel_image_end en " + a.ld if limite is None else "el simbolo __kernel_image_end"))
        return 2
    print("== Presupuesto de la imagen del nucleo (%s)" % elf)
    for n in (".text", ".rodata", ".data", ".bss"):
        s = next((x for x in secciones if x["nombre"] == n), None)
        if s:
            print("   %-8s %10d bytes  (%s)" % (n, s["tam"], mib(s["tam"])))
    pct = 100.0 * fin / limite
    print("   fin de la imagen: 0x%08X de 0x%08X permitidos -> %.1f %% (%s de %s; quedan %s)"
          % (fin, limite, pct, mib(fin), mib(limite), mib(max(0, limite - fin))))
    objetos = [(s["tam"], s["nombre"]) for s in simbolos if s["tipo"] in (1, 2) and s["tam"] > 0]
    # Los programas de ring 3 incrustados (ld -b binary) no tienen tamano: se calcula con su _start y su _end
    valores = {s["nombre"]: s["valor"] for s in simbolos}
    for n, v in valores.items():
        if n.startswith("_binary_") and n.endswith("_start") and (n[:-6] + "_end") in valores:
            objetos.append((valores[n[:-6] + "_end"] - v, n[8:-6].replace("_elf", ".elf") + " (programa incrustado)"))
    grandes = [{"tam": t, "nombre": n} for t, n in sorted(objetos, key=lambda x: -x[0])[:a.top]]
    print("   lo que mas ocupa:")
    for s in grandes:
        print("     %9d KiB  %s" % (s["tam"] // 1024, s["nombre"]))
    if pct >= a.fallar_en:
        print("size-check: FALLO - la imagen usa el %.1f %% del limite (maximo permitido en esta comprobacion: %.0f %%)" % (pct, a.fallar_en))
        return 1
    if pct >= a.avisar_en:
        print("size-check: AVISO - la imagen usa el %.1f %% del limite: conviene sacar datos a disco (BIGFS) o a ring 3" % pct)
    else:
        print("size-check: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
