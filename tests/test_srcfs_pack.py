#!/usr/bin/env python3
"""Pruebas del empaquetador SRCFS REAL (ejecutar.py::pack_sources) y generador de la imagen de la
prueba cruzada con el lector del kernel (tests/test_srcfs.c).

  python3 tests/test_srcfs_pack.py                    # pruebas unitarias del empaquetador
  python3 tests/test_srcfs_pack.py --emit IMG MANIFEST  # empaqueta src/ real en IMG (16 MiB) y escribe
                                                      # MANIFEST con 'nombre bytes' por archivo
La funcion se extrae de ejecutar.py y se ejecuta tal cual (no es una copia).
"""
import contextlib
import io
import re
import struct
import sys
import random
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SECTOR = 512
DISK = 16 * 1024 * 1024
fails = 0
checks = 0


def check(cond, msg):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print(f"  FALLO: {msg}")


def load_packer(src_dir: Path, hdd: Path):
    text = (ROOT / "ejecutar.py").read_text(encoding="utf-8")
    a = text.index("SRCFS_LBA = ")
    b = text.index("def pack_sources")
    end = b + len("def pack_sources")
    m = re.search(r"\n(?=\S)", text[end:])
    ns = dict(Path=Path, SECTOR=SECTOR, SRC_DIR=src_dir, ALLOWED_EXT={".c", ".h", ".s"}, HDD=hdd)
    exec(text[a:end + m.start()], ns)
    return ns


def pack(src_dir: Path):
    hdd = src_dir.parent / "hdd.img"
    hdd.write_bytes(b"\0" * DISK)
    ns = load_packer(src_dir, hdd)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ns["pack_sources"]()
    return ns, hdd, buf.getvalue()


def read_image(hdd: Path, ns):
    """Lee la imagen como lo hace el kernel (srcfs.c)."""
    d = hdd.read_bytes()
    base = ns["SRCFS_LBA"] * SECTOR
    idx = d[base:base + ns["SRCFS_INDEX_SECTORS"] * SECTOR]
    if idx[:8] not in (b"SRCFSv01", b"SRCFSv02"):
        return None
    v2 = idx[:8] == b"SRCFSv02"
    count = struct.unpack_from("<I", idx, 8)[0]
    cap = ns["SRCFS_INDEX_SECTORS"] * SECTOR // 64 - 1
    out = {}
    for i in range(1, min(count, cap) + 1):
        e = idx[i * 64:(i + 1) * 64]
        name = e[:48].split(b"\0")[0].decode()
        lba, size = struct.unpack_from("<II", e, 48)
        csize, flags = struct.unpack_from("<II", e, 56) if v2 else (size, 0)
        raw = d[lba * SECTOR:lba * SECTOR + csize]
        data = zlib.decompress(raw) if flags & 1 else raw
        out[name] = (lba, csize, data)          # PARCHE 140: (lba, bytes guardados en disco, contenido original)
    return out


def tree(base: Path, files: dict):
    for rel, data in files.items():
        p = base / "src" / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    return base / "src"


def test_muchos_archivos():
    with tempfile.TemporaryDirectory() as td:
        files = {f"d{i % 7}/f{i:03d}.c": (b"int x%d;\n" % i) * (1 + i * 5) for i in range(100)}
        ns, hdd, log = pack(tree(Path(td), files))
        img = read_image(hdd, ns)
        check(img is not None and len(img) == 100, f"deberian empaquetarse 100 archivos (hay {len(img or {})})")
        check("AVISO" not in log, "sin avisos con 100 archivos")
        bad = [n for n, (_, _, data) in (img or {}).items() if data != files[n]]
        check(not bad, f"contenido distinto en {bad[:3]}")
        first = min(l for l, _, _ in img.values())
        check(first == ns["SRCFS_LBA"] + ns["SRCFS_INDEX_SECTORS"], "los datos empiezan justo tras el indice")


def test_tope_de_entradas():
    with tempfile.TemporaryDirectory() as td:
        files = {f"m/f{i:04d}.h": b"x\n" for i in range(1100)}   # SOMA_267_SRCFS: mas que las 1023 entradas
        ns, hdd, log = pack(tree(Path(td), files))
        img = read_image(hdd, ns)
        cap = ns["SRCFS_INDEX_SECTORS"] * SECTOR // 64 - 1
        check(cap == 1023, f"capacidad del indice = 1023 (es {cap})")   # SOMA_267_SRCFS
        check(len(img) == cap, f"se empaquetan exactamente {cap} (hay {len(img)})")
        check("solo caben" in log, "avisa de que no caben todos")


def test_tablas_de_datos_se_omiten_primero():
    with tempfile.TemporaryDirectory() as td:
        r = random.Random(5)                                       # PARCHE 140: datos incompresibles (si no, con zlib todo cabria)
        files = {"fonts/glifos.h": r.randbytes(3 * 1024 * 1024)}
        for i in range(10):
            files[f"core/c{i}.c"] = r.randbytes(300 * 1024)          # 3 MiB de codigo
        ns, hdd, log = pack(tree(Path(td), files))
        img = read_image(hdd, ns)
        check("fonts/glifos.h" not in img, "la tabla de datos se omite si no cabe todo")
        check(all(f"core/c{i}.c" in img for i in range(10)), "todo el codigo se conserva")
        check("se omite fonts/glifos.h" in log, "lo avisa")


def test_nunca_fuera_de_la_region():
    with tempfile.TemporaryDirectory() as td:
        r = random.Random(6)
        files = {f"core/g{i}.c": r.randbytes(1024 * 1024) for i in range(6)}  # 6 MiB de codigo incompresible
        ns, hdd, log = pack(tree(Path(td), files))
        img = read_image(hdd, ns)
        limit = ns["SRCFS_LIMIT_LBA"]
        check(all(l + (s + SECTOR - 1) // SECTOR <= limit for l, s, _ in img.values()),
              "ningun archivo termina mas alla de SRCFS_LIMIT_LBA")
        raw = hdd.read_bytes()
        check(raw[limit * SECTOR:] == b"\0" * (DISK - limit * SECTOR), "nada escrito tras la region SRCFS")
        check(0 < len(img) < 6, f"se empaqueta lo que cabe (hay {len(img)} de 6)")
        check("sin espacio" in log, "lo avisa")


def test_compresion():
    """PARCHE 140: lo compresible se guarda comprimido (y se recupera igual); lo incompresible, tal cual."""
    with tempfile.TemporaryDirectory() as td:
        r = random.Random(7)
        files = {"a/texto.c": b"int x = 1; /* comentario */\n" * 4000, "a/ruido.h": r.randbytes(5000), "a/vacio.c": b""}
        ns, hdd, log = pack(tree(Path(td), files))
        img = read_image(hdd, ns)
        check(img is not None and all(img[n][2] == files[n] for n in files), "contenido identico tras comprimir")
        check(img["a/texto.c"][1] < len(files["a/texto.c"]) // 10, f"el texto se comprime ({img['a/texto.c'][1]} bytes)")
        check(img["a/ruido.h"][1] == 5000, "lo incompresible se guarda sin comprimir")
        check(hdd.read_bytes()[ns["SRCFS_LBA"] * SECTOR:][:8] == b"SRCFSv02", "formato v02")


def test_fuentes_reales():
    src = ROOT / "src"
    if not src.is_dir():
        return
    ns, hdd, log = pack(src)
    img = read_image(hdd, ns)
    reales = [p for p in src.rglob("*") if p.is_file() and p.suffix.lower() in {".c", ".h", ".s"}]
    # PARCHE 138: la region SRCFS (~4 MiB, pegada a BIGFS) ya estaba llena: lo que garantiza el empaquetador es que TODO EL CODIGO cabe y que solo
    # ceden las tablas de datos de SRCFS_LOW_PRIORITY (fuentes tipograficas, iconos, imagenes de prueba). Antes se toleraban "5 omitidos" sin mirar cuales.
    bajos = ns["SRCFS_LOW_PRIORITY"]
    codigo = [p.relative_to(src).as_posix() for p in reales if not p.relative_to(src).as_posix().startswith(bajos)]
    fuera = [c for c in codigo if c not in (img or {})]
    check(img is not None and not fuera, f"todo el codigo cabe ({len(codigo) - len(fuera)} de {len(codigo)}; fuera: {fuera[:5]})")
    print(f"  srcfs: {len(img or {})} de {len(reales)} archivos; omitidas {len(reales) - len(img or {})} tablas de datos")
    check("src/core/mem.c".replace("src/", "") in img, "core/mem.c esta empaquetado")
    # PARCHE 107: ninguna carpeta de src/ puede quedar fuera (antes el corte alfabetico dejaba sin net/, lang/, ui/ y user/)
    # PARCHE 139: se exigen todas las carpetas de CODIGO; las que son solo tablas de datos (fonts/, icons/, cursors/) pueden ceder enteras
    en_src = {p.relative_to(src).parts[0] for p in reales if len(p.relative_to(src).parts) > 1 and not p.relative_to(src).as_posix().startswith(bajos)}
    en_img = {n.split("/")[0] for n in img if "/" in n}
    check(en_src <= en_img, f"todas las carpetas de codigo de src/ estan en la imagen (faltan {sorted(en_src - en_img)})")
    cap = ns["SRCFS_DATA_SECTORS"] * SECTOR
    usado = sum(-(-c // SECTOR) * SECTOR for (_, c, _) in img.values())
    print(f"  srcfs: {usado // 1024} KiB usados de {cap // 1024} KiB (comprimido)")
    check(len(img) == len(reales), f"PARCHE 140: con compresion caben TODAS las fuentes, tablas incluidas ({len(img)} de {len(reales)})")
    bad = [n for n, (_, _, data) in img.items() if data != (src / n).read_bytes()]
    check(not bad, f"contenido distinto en {bad[:3]}")


def emit(img_path: Path, manifest: Path):
    src = ROOT / "src"
    ns, hdd, log = pack(src)
    img_path.parent.mkdir(parents=True, exist_ok=True)
    img_path.write_bytes(hdd.read_bytes())
    img = read_image(hdd, ns)
    manifest.write_text("".join(f"{n} {len(d)}\n" for n, (_, _, d) in sorted(img.items())))
    print(f"[srcfs] imagen de prueba: {len(img)} fuentes -> {img_path}")


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "--emit":
        emit(Path(sys.argv[2]), Path(sys.argv[3]))
        return 0
    for t in (test_muchos_archivos, test_tope_de_entradas, test_tablas_de_datos_se_omiten_primero,
              test_nunca_fuera_de_la_region, test_compresion, test_fuentes_reales):
        t()
    print(f"[srcfs-pack] {checks} comprobaciones, {fails} fallos")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
