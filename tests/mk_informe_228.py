#!/usr/bin/env python3
"""SOMA_228_ARRANQUE: imagen FAT32 de prueba para tests/test_arranque_228.c, hecha con el mismo codigo que construyeimagen.py.
Uso: mk_informe_228.py <imagen>"""
import sys, importlib.util
from pathlib import Path
spec = importlib.util.spec_from_file_location("ci", Path(__file__).resolve().parent.parent / "construyeimagen.py")
ci = importlib.util.module_from_spec(spec); spec.loader.exec_module(ci)
NSEC = 600000                                    # ~293 MiB: lo justo para que sea FAT32 (>= 65525 clusters)
regs = ci.fat32(2048, NSEC, "SOMA", {"EFI/BOOT/BOOTX64.EFI": b"E" * 70000, "LEEME.TXT": b"hola\r\n",
                                    "INFORME.TXT": ci.informe_vacio(), "ZZ.BIN": bytes(range(256)) * 300}, 0x1234)
with open(sys.argv[1], "wb") as f:
    f.truncate((2048 + NSEC) * 512)
    for off, d in regs:
        f.seek(off); f.write(d)
    m = bytearray(512)                            # MBR con una sola particion FAT32
    m[446:462] = bytes([0x80, 0xFE, 0xFF, 0xFF, 0x0C, 0xFE, 0xFF, 0xFF]) + (2048).to_bytes(4, "little") + NSEC.to_bytes(4, "little")
    m[510:512] = b"\x55\xAA"
    f.seek(0); f.write(m)
print("imagen", sys.argv[1])
