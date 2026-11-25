"""SOMA_310_PRESUPUESTO: tools/size_check.py con ELF sinteticos (hechos aqui mismo: no hace falta el compilador del nucleo)."""
import io
import os
import struct
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ / "tools"))
import size_check  # noqa: E402

fallos = 0
comprobaciones = 0


def check(c, msg):
    global fallos, comprobaciones
    comprobaciones += 1
    if not c:
        fallos += 1
        print("  FALLO:", msg)


def elf(simbolos, secciones=((".text", 1, 4096), (".data", 1, 8192), (".bss", 8, 1 << 20))):
    """ELF64 minimo: secciones (nombre, tipo, tamano) + .symtab + .strtab + .shstrtab. simbolos: (nombre, valor, tamano, tipo)."""
    shstr = b"\x00"; nombres = []
    for n, _t, _s in list(secciones) + [(".symtab", 0, 0), (".strtab", 0, 0), (".shstrtab", 0, 0)]:
        nombres.append(len(shstr)); shstr += n.encode() + b"\x00"
    strtab = b"\x00"; sym = b"\x00" * 24
    for n, v, tam, tipo in simbolos:
        sym += struct.pack("<IBBHQQ", len(strtab), (1 << 4) | tipo, 0, 1, v, tam); strtab += n.encode() + b"\x00"
    cuerpo = b""; off0 = 64
    sym_off = off0; cuerpo += sym
    str_off = off0 + len(cuerpo); cuerpo += strtab
    shs_off = off0 + len(cuerpo); cuerpo += shstr
    sh_off = off0 + len(cuerpo)
    sh = b"\x00" * 64
    for i, (n, t, s) in enumerate(secciones):
        sh += struct.pack("<IIQQQQIIQQ", nombres[i], t, 3, 0x100000, 0, s, 0, 0, 16, 0)
    k = len(secciones)
    nsec = k + 4                                              # nula + secciones + symtab + strtab + shstrtab
    sh += struct.pack("<IIQQQQIIQQ", nombres[k], 2, 0, 0, sym_off, len(sym), k + 2, 1, 8, 24)
    sh += struct.pack("<IIQQQQIIQQ", nombres[k + 1], 3, 0, 0, str_off, len(strtab), 0, 0, 1, 0)
    sh += struct.pack("<IIQQQQIIQQ", nombres[k + 2], 3, 0, 0, shs_off, len(shstr), 0, 0, 1, 0)
    cab = b"\x7fELF" + bytes([2, 1, 1]) + b"\x00" * 9 + struct.pack("<HHIQQQIHHHHHH", 2, 62, 1, 0, 0, sh_off, 0, 64, 0, 0, 64, nsec, nsec - 1)
    return cab + cuerpo + sh


def corre(datos, *args, ld="    ASSERT(__kernel_image_end <= 0x02E00000,\n"):
    with tempfile.TemporaryDirectory() as td:
        e = os.path.join(td, "soma.elf"); open(e, "wb").write(datos)
        l = os.path.join(td, "k.ld"); open(l, "w").write(ld)
        out = io.StringIO()
        with redirect_stdout(out):
            rc = size_check.main([e, "--ld", l] + list(args))
        return rc, out.getvalue()


LIM = 0x02E00000
base = [("lcm_ordenes_blob", 0x200000, 4700000, 1), ("procs", 0x700000, 742000, 1),
        ("_binary_vplay_elf_start", 0x800000, 0, 0), ("_binary_vplay_elf_end", 0x800000 + 2487000, 0, 0)]

rc, out = corre(elf(base + [("__kernel_image_end", LIM * 69 // 100, 0, 0)]))
check(rc == 0 and "size-check: OK" in out, "al 69 %% debe dar OK: %r" % out[-200:])
check("69.0 %" in out, "porcentaje: %r" % out)
check("lcm_ordenes_blob" in out and "vplay.elf (programa incrustado)" in out, "lista de lo que mas ocupa: %r" % out)
check(out.index("lcm_ordenes_blob") < out.index("vplay.elf"), "ordenado por tamano")
check(".bss" in out and "1048576 bytes" in out, "tamano de .bss: %r" % out)

rc, out = corre(elf(base + [("__kernel_image_end", LIM * 87 // 100, 0, 0)]))
check(rc == 0 and "AVISO" in out, "al 87 %% debe avisar sin fallar: rc=%d %r" % (rc, out[-200:]))

rc, out = corre(elf(base + [("__kernel_image_end", LIM * 92 // 100, 0, 0)]), "--fallar-en", "90")
check(rc == 1 and "FALLO" in out, "al 92 %% con --fallar-en 90 debe fallar: rc=%d" % rc)
rc, _ = corre(elf(base + [("__kernel_image_end", LIM * 92 // 100, 0, 0)]))
check(rc == 0, "sin --fallar-en, por defecto solo falla al 100 %")

rc, out = corre(elf(base))
check(rc == 2 and "__kernel_image_end" in out, "sin el simbolo: rc=%d %r" % (rc, out))
rc, out = corre(elf(base + [("__kernel_image_end", 1, 0, 0)]), ld="/* sin assert */\n")
check(rc == 2 and "ASSERT" in out, "sin el ASSERT en el .ld: rc=%d %r" % (rc, out))
rc, out = corre(b"no soy un elf" * 10)
check(rc == 2 and "ELF" in out, "archivo que no es ELF: rc=%d %r" % (rc, out))
rc, out = corre(elf(base + [("__kernel_image_end", 1, 0, 0)])[:100])
check(rc == 2, "ELF cortado: rc=%d" % rc)

# El limite sale del linker_x86_64.ld de verdad
check(size_check.limite_del_enlazador(RAIZ / "linker_x86_64.ld") == LIM, "limite de linker_x86_64.ld")

print("[size_check_310] %d comprobaciones, %d fallos" % (comprobaciones, fallos))
sys.exit(1 if fallos else 0)
