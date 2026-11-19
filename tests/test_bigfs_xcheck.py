#!/usr/bin/env python3
"""Prueba CRUZADA del BIGFS: el formato lo implementan dos programas (C en el kernel, Python en el host) y deben
coincidir byte a byte:
  1) Python crea una imagen con archivos (incluidos multi-extent y vacios) -> el codigo C real la verifica entera.
  2) El codigo C real crea otra imagen -> Python la verifica entera (fsck limpio + contenido + tamanos).
  3) Python detecta corrupcion (CRC, solapes).
Uso: python3 tests/test_bigfs_xcheck.py build/tests/test_bigfs
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
try:
    import bigfs as B  # noqa: E402
except ModuleNotFoundError:
    print("[bigfs cruzada] OMITIDA: falta tools/bigfs.py (la implementacion Python del formato); el lado C ya se prueba en test_bigfs")
    raise SystemExit(0)

fails = 0
checks = 0


def check(cond, msg):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print(f"  FALLO: {msg}")


def main():
    cbin = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build" / "tests" / "test_bigfs")
    with tempfile.TemporaryDirectory(prefix="soma_bigfs_") as td:
        td = Path(td)
        # 1) Python -> C
        img, man = td / "py.img", td / "py.manifest"
        fs = B.BigFS(img, 0, None, create_mb=8)
        fs.format()
        spec = [("/leeme.txt", 11, 1), ("/musica/a.wav", 700000, 2), ("/musica/rock/b.wav", 1500001, 3),
                ("/prog/hola.elf", 8920, 4), ("/vacio", 0, 5), ("/prog/grande.bin", 3000000, 6)]
        B._emit(fs, man, spec)
        rep = fs.fsck()
        check(rep["problems"] == 0, f"fsck de Python sobre su propia imagen: {rep}")
        fs.close()
        r = subprocess.run([cbin, "--verify", str(img), str(man)], capture_output=True, text=True)
        check(r.returncode == 0, "el codigo C no verifica la imagen de Python:\n" + r.stdout + r.stderr)
        print("  " + r.stdout.strip().splitlines()[0] if r.stdout.strip() else "")
        # 2) C -> Python
        img2, man2 = td / "c.img", td / "c.manifest"
        r = subprocess.run([cbin, "--emit", str(img2), str(man2)], capture_output=True, text=True)
        check(r.returncode == 0, "el codigo C no pudo crear la imagen:\n" + r.stdout + r.stderr)
        fs2 = B.BigFS(img2)
        fs2.mount()
        rep = fs2.fsck()
        check(rep["problems"] == 0, f"fsck de Python sobre la imagen de C: {rep}")
        nfiles = 0
        for line in man2.read_text().splitlines():
            kind, path, size, h = line.split()
            if kind == "D":
                check(fs2.ino[fs2.lookup(path)].state == B.T_DIR, f"directorio {path}")
                continue
            data = fs2.read_file(path)
            check(len(data) == int(size), f"{path}: tamano {len(data)} != {size}")
            check(B.fnv64(data) == int(h, 16), f"{path}: contenido distinto")
            nfiles += 1
        check(nfiles == 6, f"archivos verificados: {nfiles}")
        print(f"  imagen del kernel leida por Python: {nfiles} archivos verificados byte a byte")
        # 3) Python escribe y modifica sobre la imagen de C; C la vuelve a verificar
        fs2.write_file("/prog/hola.elf", bytes(range(256)) * 40)
        fs2.remove("/vacio")
        fs2.mkdir("/nuevo/dir")
        fs2.write_file("/nuevo/dir/x.bin", b"abc" * 5000)
        check(fs2.fsck()["problems"] == 0, "fsck tras editar con Python")
        fs2.close()
        r = subprocess.run([cbin, "--fsck", str(img2)], capture_output=True, text=True)
        check(r.returncode == 0, "el fsck del codigo C no acepta la imagen editada por Python:\n" + r.stdout + r.stderr)
        # 4) deteccion de corrupcion
        fs3 = B.BigFS(img)
        fs3.mount()
        i = fs3.lookup("/musica/a.wav")
        fs3.f.seek(fs3.base + (1 + i // 2) * 512 + (i % 2) * 256 + 20)
        fs3.f.write(b"\xAA")
        fs3.f.flush()
        rep = fs3.fsck()
        check(rep["bad_crc"] == 1 and rep["problems"] == 1, f"corrupcion no detectada: {rep}")
        fs3.close()
        r = subprocess.run([cbin, "--verify", str(img), str(man)], capture_output=True, text=True)
        check(r.returncode != 0, "el codigo C debe rechazar la imagen corrupta")
    print(f"[bigfs cruzada] {checks} comprobaciones, {fails} fallos")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
