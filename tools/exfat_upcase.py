#!/usr/bin/env python3
"""SOMA_267_DISCOS: tabla de mayusculas de exFAT (Up-case Table) a partir de los datos Unicode de Python.

exFAT compara los nombres sin distinguir mayusculas con una tabla que va dentro del propio volumen. La de un volumen nuevo de SOMA
se genera aqui: para cada caracter del plano basico (U+0000..U+FFFF) su mayuscula si es UN solo caracter del plano basico
(str.upper), y si no, el mismo. Se guarda comprimida como dice la especificacion: un tramo de caracteres que no cambian se escribe
0xFFFF y su longitud.

    python3 tools/exfat_upcase.py src/core/exfat_upcase.inc     regenera la tabla para el nucleo (src/core/exfat.c)
La usan tambien tools/soma_disco.py (al formatear MODELOS y USUARIO) y las pruebas."""
import sys


def tabla():
    t = []
    for c in range(0x10000):
        if 0xD800 <= c <= 0xDFFF:
            t.append(c)
            continue
        u = chr(c).upper()
        t.append(ord(u) if len(u) == 1 and ord(u) < 0x10000 else c)
    return t


def comprimida():
    """lista de u16 (formato comprimido de exFAT)"""
    t = tabla()
    out, i = [], 0
    while i < 0x10000:
        if t[i] == i:
            j = i
            while j < 0x10000 and t[j] == j:
                j += 1
            if j - i > 2:
                out += [0xFFFF, j - i]
                i = j
                continue
        out.append(t[i])
        i += 1
    return out


def bytes_tabla():
    return b"".join(v.to_bytes(2, "little") for v in comprimida())


def checksum(b):
    s = 0
    for x in b:
        s = (((s << 31) | (s >> 1)) + x) & 0xFFFFFFFF
    return s


def main():
    sal = sys.argv[1] if len(sys.argv) > 1 else "src/core/exfat_upcase.inc"
    v = comprimida()
    b = bytes_tabla()
    lin = ["/* SOMA_267_DISCOS: tabla de mayusculas de exFAT, comprimida (generada por tools/exfat_upcase.py: no editar).",
           f" * {len(v)} valores, {len(b)} bytes, suma de comprobacion 0x{checksum(b):08X}. */",
           f"#define EXFAT_UPCASE_N {len(v)}u", f"#define EXFAT_UPCASE_SUM 0x{checksum(b):08X}u",
           "static const uint16_t exfat_upcase_tbl[EXFAT_UPCASE_N] = {"]
    for k in range(0, len(v), 16):
        lin.append("    " + ", ".join(f"0x{x:04X}" for x in v[k:k + 16]) + ",")
    lin.append("};")
    open(sal, "w").write("\n".join(lin) + "\n")
    print(f"{sal}: {len(v)} valores ({len(b)} bytes), checksum 0x{checksum(b):08X}")


if __name__ == "__main__":
    main()
