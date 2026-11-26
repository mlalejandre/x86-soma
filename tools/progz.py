#!/usr/bin/env python3
"""SOMA_314_PROGZ: comprime un programa de ring 3 para incrustarlo en la imagen del nucleo (src/core/progz_fmt.h).

Formato: cabecera de 24 bytes  "SOMZ" | version (1) | tamano original (u64) | huella FNV-1a 64 del original (u64)
         + el programa en zlib (nivel 9).
El nucleo lo descomprime con el servicio aislado zdec al lanzar el programa y comprueba tamano y huella antes de ejecutarlo.

    python3 tools/progz.py ENTRADA.elf SALIDA.elf.z
    python3 tools/progz.py --prueba          (ida y vuelta con datos de prueba; lo usa make test)
"""
import struct
import sys
import zlib

MAGIA = b"SOMZ"
VERSION = 1
TAM_MAX = 64 << 20


def fnv1a64(datos: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in datos:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def comprime(datos: bytes) -> bytes:
    if not datos or len(datos) > TAM_MAX:
        raise ValueError("tamano no valido: %d" % len(datos))
    return MAGIA + struct.pack("<IQQ", VERSION, len(datos), fnv1a64(datos)) + zlib.compress(datos, 9)


def descomprime(z: bytes) -> bytes:
    if len(z) < 24 or z[:4] != MAGIA:
        raise ValueError("no es un programa comprimido de SOMA")
    ver, tam, huella = struct.unpack_from("<IQQ", z, 4)
    if ver != VERSION or not 0 < tam <= TAM_MAX:
        raise ValueError("cabecera no valida")
    d = zlib.decompress(z[24:])
    if len(d) != tam or fnv1a64(d) != huella:
        raise ValueError("tamano o huella no coinciden")
    return d


def prueba() -> int:
    import os
    fallos = 0
    for n in (1, 7, 4096, 100000):
        d = os.urandom(n // 2) + b"\x00" * (n - n // 2)
        z = comprime(d)
        if descomprime(z) != d:
            fallos += 1
        roto = bytearray(z); roto[-3] ^= 0x40
        try:
            descomprime(bytes(roto)); fallos += 1
        except (ValueError, zlib.error):
            pass
        otra = bytearray(z); otra[16] ^= 1                       # huella cambiada
        try:
            descomprime(bytes(otra)); fallos += 1
        except ValueError:
            pass
    if fnv1a64(b"SOMA") != 0x744DE1245416A79D:                   # el mismo valor que comprueba tests/test_progz_314.c
        fallos += 1
    print("[progz] ida y vuelta: %s" % ("OK" if not fallos else "%d fallos" % fallos))
    return 1 if fallos else 0


def main(argv) -> int:
    if argv[1:2] == ["--prueba"]:
        return prueba()
    if len(argv) != 3:
        print(__doc__)
        return 2
    datos = open(argv[1], "rb").read()
    z = comprime(datos)
    if descomprime(z) != datos:
        print("progz: la ida y vuelta no coincide")
        return 1
    with open(argv[2], "wb") as f:
        f.write(z)
    print("progz: %s %d -> %d bytes (%.0f %%)" % (argv[1], len(datos), len(z), 100.0 * len(z) / len(datos)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
