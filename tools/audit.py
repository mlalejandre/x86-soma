#!/usr/bin/env python3
"""Auditoria estatica ligera de SOMA (solo lectura).

Uso:  python3 tools/audit.py [--root DIR] [--strict] [--top N]

Informa de: funciones C inseguras, marcadores TODO/FIXME, tamano de los
fuentes mas grandes y coherencia de la lista de archivos protegidos entre
ejecutar.py, protected.txt y src/agent/agent.c.
Con --strict devuelve codigo 1 si hay hallazgos de funciones inseguras.
"""
import argparse
import re
import sys
from pathlib import Path

UNSAFE = ("strcpy", "strcat", "sprintf", "vsprintf", "gets")
SRC_EXT = {".c", ".h", ".s"}


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def scan_sources(root: Path):
    unsafe, todos, sizes = {}, 0, []
    unsafe_re = re.compile(r"\b(" + "|".join(UNSAFE) + r")\s*\(")
    for p in sorted((root / "src").rglob("*")):
        if p.suffix.lower() not in SRC_EXT or not p.is_file():
            continue
        raw = p.read_text(encoding="utf-8", errors="replace")
        sizes.append((len(raw.splitlines()), p.relative_to(root).as_posix()))
        todos += len(re.findall(r"\b(TODO|FIXME|XXX)\b", raw))
        rel = p.relative_to(root).as_posix()
        # SOMA_299_CI: las libc de los programas de usuario (TinyCC, voz, vplay) DEFINEN estas funciones para los programas que
        # se compilan dentro de SOMA: no son usos inseguros del sistema. Y un nombre dentro de una cadena ("strcpy(") no es una llamada.
        if rel.startswith("src/user/cc/") or rel.endswith("_libc.c"):
            continue
        for i, line in enumerate(strip_comments(raw).splitlines(), 1):
            line = re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', line)
            for m in unsafe_re.finditer(line):
                unsafe.setdefault(p.relative_to(root).as_posix(), []).append((i, m.group(1)))
    return unsafe, todos, sorted(sizes, reverse=True)


def protected_sets(root: Path):
    py = set()
    ej = root / "ejecutar.py"
    if ej.is_file():
        m = re.search(r"PROTECTED_FILES\s*=\s*\{(.*?)\}", ej.read_text(encoding="utf-8"), re.S)
        if m:
            py = set(re.findall(r"'([^']+)'", m.group(1)))
    txt = set()
    pt = root / "protected.txt"
    if pt.is_file():
        for line in pt.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                txt.add(Path(line).name)
    c = set()
    sh = root / "src/agent/agent.c"
    if sh.is_file():
        m = re.search(r"protected_list\[\]\s*=\s*\{(.*?)\};", sh.read_text(encoding="utf-8"), re.S)
        if m:
            c = set(re.findall(r'"([^"]+)"', m.group(1)))
    return py, txt, c


def srcfs_check(root: Path) -> int:
    """El formato SRCFS lo comparten el kernel, disk_layout.h y ejecutar.py: deben coincidir."""
    def num(path, pattern):
        p = root / path
        m = re.search(pattern, p.read_text(encoding="utf-8"), re.M) if p.is_file() else None
        return int(m.group(1)) if m else None

    vals = {
        "srcfs.c    SRCFS_INDEX_SECTORS": num("src/fs/srcfs.c", r"#define\s+SRCFS_INDEX_SECTORS\s+(\d+)"),
        "ejecutar   SRCFS_INDEX_SECTORS": num("ejecutar.py", r"^SRCFS_INDEX_SECTORS\s*=\s*(\d+)"),
        "srcfs.c    SRCFS_LBA": num("src/fs/srcfs.c", r"#define\s+SRCFS_LBA\s+(\d+)"),
        "ejecutar   SRCFS_LBA": num("ejecutar.py", r"^SRCFS_LBA\s*=\s*(\d+)"),
        "layout     SRCFS_INDEX": num("src/fs/disk_layout.h", r"#define\s+SOMA_LBA_SRCFS_INDEX\s+(\d+)"),
        "layout     SRCFS_DATA": num("src/fs/disk_layout.h", r"#define\s+SOMA_LBA_SRCFS_DATA\s+(\d+)"),
    }
    print("== Formato SRCFS (kernel / disk_layout.h / ejecutar.py)")
    bad = 0
    if any(v is None for v in vals.values()):
        for k, v in vals.items():
            if v is None:
                print(f"   no se pudo leer: {k}")
        return 1
    sec_k, sec_e = vals["srcfs.c    SRCFS_INDEX_SECTORS"], vals["ejecutar   SRCFS_INDEX_SECTORS"]
    if sec_k != sec_e:
        print(f"   INCOHERENTE: sectores de indice: kernel={sec_k} ejecutar.py={sec_e}"); bad += 1
    if vals["srcfs.c    SRCFS_LBA"] != vals["ejecutar   SRCFS_LBA"] or vals["srcfs.c    SRCFS_LBA"] != vals["layout     SRCFS_INDEX"]:
        print("   INCOHERENTE: el LBA del indice no coincide entre kernel, ejecutar.py y disk_layout.h"); bad += 1
    if vals["layout     SRCFS_DATA"] - vals["layout     SRCFS_INDEX"] != sec_k:
        print(f"   INCOHERENTE: disk_layout.h deja {vals['layout     SRCFS_DATA'] - vals['layout     SRCFS_INDEX']} sectores de indice, el kernel usa {sec_k}"); bad += 1
    if not bad:
        print(f"   coherente: indice de {sec_k} sectores ({sec_k * 512 // 64 - 1} entradas) en LBA {vals['srcfs.c    SRCFS_LBA']}")
    return bad


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path.cwd())
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--top", type=int, default=8)
    a = ap.parse_args()
    root = a.root.resolve()
    if not (root / "src").is_dir():
        print("ERROR: no hay src/ en", root, file=sys.stderr)
        return 2

    unsafe, todos, sizes = scan_sources(root)
    total = sum(len(v) for v in unsafe.values())
    print(f"== Funciones inseguras ({', '.join(UNSAFE)}): {total} en {len(unsafe)} archivos")
    for f, hits in sorted(unsafe.items(), key=lambda kv: -len(kv[1])):
        print(f"   {len(hits):3d}  {f}  (lineas {', '.join(str(h[0]) for h in hits[:6])}{'...' if len(hits) > 6 else ''})")
    print(f"== Marcadores TODO/FIXME/XXX: {todos}")
    print(f"== Fuentes mas grandes (lineas):")
    for n, f in sizes[:a.top]:
        print(f"   {n:6d}  {f}")

    py, txt, c = protected_sets(root)
    host = py | txt  # ejecutar.py aplica ambas listas (por nombre de archivo)
    existing = {p.name for p in (root / "src").rglob("*") if p.is_file()}
    print("== Archivos protegidos (por nombre de archivo)")
    print(f"   host (ejecutar.py + protected.txt): {len(host)} | dentro del SO (agent.c): {len(c)}")
    problemas = 0
    for name in sorted(host | c):
        notas = []
        if name not in existing:
            notas.append("no existe en src/")
        if name not in host:
            notas.append("falta en el host")
        if name not in c:
            notas.append("falta en el SO (agent.c)")
        if notas:
            problemas += 1
            print(f"   {name}: {', '.join(notas)}")
    if not problemas:
        print("   coherente")
    srcfs_bad = srcfs_check(root)
    return 1 if (a.strict and (total or srcfs_bad)) else 0


if __name__ == "__main__":
    raise SystemExit(main())
