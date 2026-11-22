#!/usr/bin/env python3
"""SOMA_267_DISCOS: la particion DATOS del pendrive de SOMA, desde fuera (Linux o macOS).

DATOS guarda lo que no debe cambiar al actualizar SOMA: la configuracion (/etc), la memoria (/mem), los agentes (/agent), los
programas instalados y el almacen (BIGFS: modelos copiados, grabaciones...). Dentro de SOMA solo la tocan los programas del sistema;
desde fuera, con esta herramienta (el formato es el propio de SOMA, por eso ni Windows ni macOS la abren).

Uso:
    python3 tools/datos.py DISCO listar                      archivos de SOMA (nombre, tamano, donde)
    python3 tools/datos.py DISCO leer /etc/hostname          lo escribe en la terminal (o: leer RUTA DESTINO)
    python3 tools/datos.py DISCO escribir /etc/hostname mi_archivo.txt
    python3 tools/datos.py DISCO borrar /workspace/viejo.txt
    python3 tools/datos.py DISCO exportar CARPETA [--con-secretos]   copia de seguridad (sin claves ni .conf salvo que se pida)
    python3 tools/datos.py DISCO almacen                     archivos del almacen (BIGFS)
    python3 tools/datos.py DISCO comprobar                   integridad (CRC de las ranuras, diario, BIGFS)
DISCO es el pendrive (/dev/sdb en Linux, /dev/rdisk4 en macOS: hace falta sudo), una imagen de pendrive (.img) o, con --crudo,
un disco de SOMA sin particiones (el build/hdd.img de QEMU).
Escribir sigue los mismos protocolos que el nucleo (diario y BIGFS atomicos): un corte a mitad deja el archivo viejo o el nuevo.
No escribas con SOMA arrancado desde ese mismo pendrive.
"""
import argparse, os, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import soma_disco as sd
import somafs as sf


def abre(ruta, crudo, escribir):
    try:
        f = open(ruta, "r+b" if escribir else "rb")
    except PermissionError:
        sys.exit(f"Sin permiso para abrir {ruta}: ejecutalo con sudo (y en macOS usa /dev/rdiskN tras 'diskutil unmountDisk').")
    except FileNotFoundError:
        sys.exit(f"No existe {ruta}.")
    f.seek(0, os.SEEK_END)
    tam = f.tell()
    if tam == 0:                                   # dispositivo de bloques: tamano por ioctl/lseek
        try:
            tam = os.lseek(f.fileno(), 0, os.SEEK_END)
        except OSError:
            pass
    if tam < 1 << 20:
        sys.exit(f"{ruta}: no parece un disco ({tam} bytes)")
    dev = sd.Disco(f, tam // 512, solo_lectura=not escribir)
    if crudo:
        reg = sf.Region(dev, 0, dev.sectores)
        return f, reg, "disco de SOMA en bruto"
    info = sd.analizar(dev)
    p = info["datos"]
    if p is None and info["somadata_img"]:
        fs = sd.Fat32(dev, info["sistema"].lba, info["sistema"].n)
        r = fs.contiguo("/" + info["somadata_img"][1])
        if r and sd.lee_marca(dev, r[0]):
            p = sd.Part(0, 0, r[0], r[1])
    if p is None:
        sys.exit(f"{ruta}: no tiene la particion DATOS de SOMA ({sd.describe(info)})")
    if info["datos_virgen"] or sd.lee_marca(dev, p.lba) is None:
        sys.exit("DATOS esta sin usar: SOMA la prepara en su primer arranque (no hay nada que leer todavia)")
    n = min(p.n - sd.UD_DATA_OFF, 1 << 24)        # como ud_data_sectors: el disco de SOMA llega como mucho a 8 GiB
    return f, sf.Region(dev, p.lba + sd.UD_DATA_OFF, n), f"particion {p.idx + 1} ({p.n * 512 / 1e9:.1f} GB)"


def tam(n):
    return f"{n} B" if n < 1024 else f"{n / 1024:.1f} KiB" if n < 1 << 20 else f"{n / 1048576:.1f} MiB"


def main():
    ap = argparse.ArgumentParser(description="La particion DATOS de SOMA desde fuera")
    ap.add_argument("disco")
    ap.add_argument("orden", choices=["listar", "leer", "escribir", "borrar", "exportar", "almacen", "comprobar"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--crudo", action="store_true", help="DISCO es un disco de SOMA sin particiones (build/hdd.img)")
    ap.add_argument("--con-secretos", action="store_true", help="exportar tambien claves y archivos .conf")
    a = ap.parse_args()
    escribe = a.orden in ("escribir", "borrar")
    f, reg, donde = abre(a.disco, a.crudo, escribe)
    try:
        try:
            s = sf.Soma(reg, escribir=escribe)
        except sf.ErrorSoma as e:
            sys.exit(f"{a.disco}: {e}")
        if a.orden == "listar":
            print(f"DATOS: {donde}")
            for nombre, t, d, _ in s.archivos():
                print(f"  {nombre:48s} {tam(t):>10s}  {'(BIGFS)' if d == 'BIGFS' else ''}")
            print(f"{len(s.ranura)} archivos")
            for av in s.avisos:
                print("  aviso:", av)
        elif a.orden == "leer":
            if not a.args:
                sys.exit("falta la ruta")
            b = s.leer(a.args[0])
            if len(a.args) > 1:
                Path(a.args[1]).write_bytes(b)
                print(f"{a.args[0]} -> {a.args[1]} ({tam(len(b))})")
            else:
                sys.stdout.buffer.write(b)
        elif a.orden == "escribir":
            if len(a.args) != 2:
                sys.exit("uso: escribir RUTA_EN_SOMA ARCHIVO")
            b = Path(a.args[1]).read_bytes()
            s.escribir(a.args[0], b)
            f.flush()
            os.fsync(f.fileno())
            print(f"{a.args[1]} -> {a.args[0]} ({tam(len(b))})")
        elif a.orden == "borrar":
            if not a.args:
                sys.exit("falta la ruta")
            s.borrar(a.args[0])
            f.flush()
            os.fsync(f.fileno())
            print(f"borrado {a.args[0]}")
        elif a.orden == "exportar":
            if not a.args:
                sys.exit("falta la carpeta de destino")
            dst = Path(a.args[0])
            n = omit = 0
            for nombre, t, d, _ in s.archivos():
                if sf.es_secreto(nombre) and not a.con_secretos:
                    omit += 1
                    continue
                p = dst / "soma" / nombre.lstrip("/")
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(s.leer(nombre))
                n += 1
            if s.big:
                for r, es_dir, t, ino in s.big.recorrer():
                    if r.startswith("/.ramfs") or r.startswith("/."):
                        continue
                    p = dst / "almacen" / r.lstrip("/")
                    if es_dir:
                        p.mkdir(parents=True, exist_ok=True)
                    else:
                        p.parent.mkdir(parents=True, exist_ok=True)
                        with open(p, "wb") as o:
                            for off in range(0, t, 8 << 20):
                                o.write(s.big.leer(ino, off, 8 << 20))
                        n += 1
            print(f"{n} archivos exportados a {dst}" + (f" ({omit} con claves o .conf omitidos: --con-secretos para incluirlos)" if omit else ""))
        elif a.orden == "almacen":
            if not s.big:
                sys.exit("DATOS no tiene almacen BIGFS")
            for r, es_dir, t, _ in s.big.recorrer():
                if not r.startswith("/.ramfs"):
                    print(f"  {r + ('/' if es_dir else ''):56s} {'' if es_dir else tam(t)}")
            print(f"libre: {tam(s.big.libres() * sf.BIG_CLUSTER)}")
        elif a.orden == "comprobar":
            prob = list(s.avisos)
            if s.diario_pendiente:
                prob.append("hay una escritura en el diario pendiente (SOMA la termina al arrancar)")
            if s.big and s.big.descartados:
                prob.append(f"BIGFS: {s.big.descartados} inodos danados (SOMA los descarta)")
            if not s.big:
                prob.append("no hay BIGFS (los archivos grandes no se pueden guardar)")
            print(f"DATOS: {donde}: {len(s.ranura)} archivos")
            for p in prob:
                print("  -", p)
            print("todo bien" if not prob else f"{len(prob)} avisos")
            sys.exit(0 if not prob else 1)
    except (sf.ErrorSoma, sd.ErrorDisco) as e:
        sys.exit(f"error: {e}")
    finally:
        f.close()


if __name__ == "__main__":
    main()
