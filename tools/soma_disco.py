#!/usr/bin/env python3
"""SOMA_267_DISCOS: el pendrive de SOMA en cuatro particiones, en Python puro (Linux, macOS y Windows; sin mtools ni drivers).

    1  SISTEMA   FAT32   1 GiB   arranque (EFI/BOOT/BOOTX64.EFI) + INFORME.TXT. Se rehace entera en cada grabacion.
    2  DATOS     SOMA    2 GiB   configuracion, memoria, programas instalados (formato propio de SOMA: SOMAFS02 + BIGFS, con su
                                 marca "SOMADATA-DISCO-1"). No se toca al actualizar. Desde fuera: python3 tools/datos.py
    3  MODELOS   exFAT  16 GiB   modelos de IA (voz/, llm/). En pendrives pequenos, menos (plan_particiones).
    4  USUARIO   exFAT   resto   los archivos del usuario: Documentos, Imagenes, Audio, Videos, Descargas, Proyectos.

Aqui esta todo lo que hace falta para eso y lo usan construyeimagen.py, ejecutar.py, tools/datos.py y las pruebas:
  - Disco: E/S por sectores sobre un archivo de imagen o un dispositivo (/dev/sdX, /dev/rdiskN), siempre alineada.
  - MBR, el plan de particiones (el mismo calculo que src/core/unidades.h: lo comprueban las pruebas).
  - FAT32: escritor (la particion SISTEMA) y lector con nombres largos (para migrar pendrives antiguos).
  - exFAT: formateador y lector/escritor (carpetas, archivos de cualquier tamano, borrar), con la misma tabla de mayusculas que el
    nucleo (src/core/exfat_upcase.inc). Lo comprueban fsck.exfat y el controlador C de SOMA (tests/test_exfat_267.py).
  - DATOS: la marca y la preparacion.
  - analizar(): que hay ya en el pendrive (nada, SOMA nuevo, SOMA antiguo de 2 o 3 particiones, o el fichero SOMADATA.IMG).
  - grabar(): deja el pendrive con las cuatro particiones conservando lo que haya que conservar (ver la funcion).
"""
import os, shutil, struct, sys, tempfile, time
from pathlib import Path

SECTOR = 512
MIB = 2048                                   # sectores
GIB = 1 << 21
P1_LBA = 2048
SISTEMA_SECT = GIB                           # 1 GiB
DATOS_SECT = 2 * GIB                         # 2 GiB
MODELOS_MAX = 16 * GIB
USUARIO_MIN_GRANDE = 8 * GIB                 # con mas de 24 GiB libres, MODELOS se lleva 16 y USUARIO el resto (al menos 8)
PARTE_MIN = 512 * MIB                        # MODELOS y USUARIO, como poco 512 MiB cada una
TIPO_FAT32 = 0x0C
TIPO_DATOS = 0xDA                            # "datos sin sistema de archivos": Windows y macOS no la montan ni ofrecen formatearla
TIPO_EXFAT = 0x07
IMAGEN_SECT = P1_LBA + SISTEMA_SECT + DATOS_SECT + GIB + GIB      # la imagen del repositorio: 5 GiB + 1 MiB (cabe en 8 GB)
SERIE_IMAGEN = (0x50AA0003, 0x50AA0004)      # MODELOS y USUARIO de la imagen: SOMA las agranda en el primer arranque si estan vacias
CARPETAS_USUARIO = ["Documentos", "Imágenes", "Audio", "Vídeos", "Descargas", "Proyectos"]
CARPETAS_MODELOS = ["voz", "llm"]
UD_MARK = b"SOMADATA-DISCO-1"
UD_DATA_OFF = 8
UD_ZERO_SECT = 16392
ETIQUETAS = ("SISTEMA", "DATOS", "MODELOS", "USUARIO")
RAIZ = Path(__file__).resolve().parent.parent


class ErrorDisco(Exception):
    pass


class SinSitio(ErrorDisco):
    pass


# ====================================================================== E/S por sectores
class Disco:
    """Sectores de 512 B sobre un archivo abierto ('r+b' o 'rb'). Todo acceso es de sectores enteros (macOS /dev/rdiskN lo exige)
    y se comprueba contra el tamano; con `zonas` (lista de (lba, n)) solo se puede escribir dentro de ellas."""

    def __init__(self, f, sectores, zonas=None, solo_lectura=False):
        self.f, self.sectores, self.zonas, self.ro = f, sectores, zonas, solo_lectura
        self.escritos = 0

    def leer(self, lba, n):
        if n <= 0:
            return b""
        if lba < 0 or lba + n > self.sectores:
            raise ErrorDisco(f"lectura fuera del disco ({lba}+{n} > {self.sectores})")
        self.f.seek(lba * SECTOR)
        b = self.f.read(n * SECTOR)
        while b is not None and 0 < len(b) < n * SECTOR:     # lecturas cortas de un dispositivo
            m = self.f.read(n * SECTOR - len(b))
            if not m:
                break
            b += m
        if len(b) != n * SECTOR:
            b += bytes(n * SECTOR - len(b))     # imagen dispersa mas corta: ceros
        return b

    def escribir(self, lba, datos):
        if not datos:
            return
        if len(datos) % SECTOR:
            datos = datos + bytes(SECTOR - len(datos) % SECTOR)
        n = len(datos) // SECTOR
        if self.ro:
            raise ErrorDisco("disco de solo lectura")
        if lba < 0 or lba + n > self.sectores:
            raise ErrorDisco(f"escritura fuera del disco ({lba}+{n} > {self.sectores})")
        if self.zonas is not None and not any(z0 <= lba and lba + n <= z0 + zn for z0, zn in self.zonas):
            raise ErrorDisco(f"escritura fuera de las zonas permitidas ({lba}+{n})")
        self.f.seek(lba * SECTOR)
        mv = memoryview(datos)
        for i in range(0, len(datos), 1 << 20):
            trozo = mv[i:i + (1 << 20)]
            while len(trozo):                        # un dispositivo sin bufer puede escribir menos de lo pedido
                k = self.f.write(trozo)
                if not k:
                    raise ErrorDisco(f"no se pudo escribir en el sector {lba + i // SECTOR}")
                trozo = trozo[k:]
        self.escritos += n

    def ceros(self, lba, n):
        z = bytes(min(n, 2048) * SECTOR)
        while n > 0:
            k = min(n, 2048)
            self.escribir(lba, z[:k * SECTOR])
            lba += k
            n -= k

    def vacia(self):
        self.f.flush()
        try:
            os.fsync(self.f.fileno())
        except OSError:
            pass


class Vista:
    """Una particion vista como disco propio (lba relativo), sin poder salirse de ella."""

    def __init__(self, disco, lba, n):
        self.d, self.base, self.sectores = disco, lba, n

    def leer(self, lba, n):
        if lba < 0 or lba + n > self.sectores:
            raise ErrorDisco("lectura fuera de la particion")
        return self.d.leer(self.base + lba, n)

    def escribir(self, lba, datos):
        n = (len(datos) + SECTOR - 1) // SECTOR
        if lba < 0 or lba + n > self.sectores:
            raise ErrorDisco("escritura fuera de la particion")
        self.d.escribir(self.base + lba, datos)

    def ceros(self, lba, n):
        if lba < 0 or lba + n > self.sectores:
            raise ErrorDisco("escritura fuera de la particion")
        self.d.ceros(self.base + lba, n)


# ====================================================================== MBR y plan
class Part:
    def __init__(self, idx, tipo, lba, n, activa=False):
        self.idx, self.tipo, self.lba, self.n, self.activa = idx, tipo, lba, n, activa

    @property
    def fin(self):
        return self.lba + self.n

    def __repr__(self):
        return f"p{self.idx + 1}(0x{self.tipo:02X} {self.lba}+{self.n})"


def leer_mbr(dev):
    """(firma del disco, [Part]) o (None, []) si no hay MBR. GPT -> ErrorDisco."""
    s = dev.leer(0, 1)
    if s[510:512] != b"\x55\xAA":
        return None, []
    sig = struct.unpack_from("<I", s, 0x1B8)[0]
    ps = []
    for i in range(4):
        e = s[0x1BE + 16 * i:0x1BE + 16 * i + 16]
        act, tipo = e[0], e[4]
        lba, n = struct.unpack_from("<II", e, 8)
        if tipo == 0xEE:
            raise ErrorDisco("el pendrive usa GPT")
        if tipo and lba and n:
            ps.append(Part(i, tipo, lba, n, act == 0x80))
    return sig, ps


def mbr_bytes(parts, sig, anterior=None):
    m = bytearray(anterior[:440] if anterior and len(anterior) >= 440 else bytes(440)) + bytearray(72)
    struct.pack_into("<I", m, 440, sig)
    for i in range(4):
        struct.pack_into("<16s", m, 446 + 16 * i, bytes(16))
    for p in parts:
        struct.pack_into("<B3sB3sII", m, 446 + 16 * p.idx, 0x80 if p.activa else 0, b"\xFE\xFF\xFF", p.tipo, b"\xFE\xFF\xFF", p.lba, p.n)
    m[510:512] = b"\x55\xAA"
    return bytes(m)


def plan_particiones(total, datos_n=DATOS_SECT):
    """Las cuatro particiones para un disco de `total` sectores. El mismo calculo esta en src/core/unidades.h (unid_plan)."""
    fin = min(total, 0xFFFFFFFF) & ~(MIB - 1)
    p2 = P1_LBA + SISTEMA_SECT
    r0 = (p2 + datos_n + MIB - 1) & ~(MIB - 1)
    if fin <= r0 or fin - r0 < 2 * PARTE_MIN:
        need = (r0 + 2 * PARTE_MIN) * SECTOR
        raise SinSitio(f"el pendrive es demasiado pequeno: hacen falta {need / 1e9:.1f} GB")
    libre = fin - r0
    mod = max(libre // 2, libre - USUARIO_MIN_GRANDE) & ~(MIB - 1)
    mod = min(mod, MODELOS_MAX)
    usu = libre - mod
    return [Part(0, TIPO_FAT32, P1_LBA, SISTEMA_SECT, True), Part(1, TIPO_DATOS, p2, datos_n),
            Part(2, TIPO_EXFAT, r0, mod), Part(3, TIPO_EXFAT, r0 + mod, usu)]


# ====================================================================== FAT32: escritor (SISTEMA) y lector (pendrives antiguos)
def _fecha():
    from datetime import datetime
    n = datetime.now()
    return ((n.hour << 11) | (n.minute << 5) | (n.second // 2)), (((n.year - 1980) << 9) | (n.month << 5) | n.day)


def _ent(name83, attr, clus, size):
    t, d = _fecha()
    return struct.pack("<11sBBBHHHHHHHI", name83, attr, 0, 0, t, d, d, clus >> 16, t, d, clus & 0xFFFF, size)


def _n83(nombre):
    base, _, ext = nombre.upper().partition(".")
    return (base[:8].ljust(8) + ext[:3].ljust(3)).encode("ascii")


def _es83(nombre):
    if nombre in (".", ".."):
        return True
    base, punto, ext = nombre.partition(".")
    ok = lambda t, n: 0 < len(t) <= n and all(c.isascii() and (c.isupper() or c.isdigit() or c in "!#$%&'()-@^_`{}~") for c in t)
    return ok(base, 8) and (not punto or ok(ext, 3)) and "." not in ext


def _alias(nombre, usados):
    limpia = lambda t: "".join(c for c in t.upper() if c.isascii() and (c.isalnum() or c in "!#$%&'()-@^_`{}~")) or "_"
    base, _, ext = nombre.rpartition(".") if "." in nombre.lstrip(".") else (nombre, "", "")
    base, ext = limpia(base), limpia(ext)[:3] if ext else ""
    for k in range(1, 100000):
        suf = f"~{k}"
        c = (base[:8 - len(suf)] + suf).ljust(8) + ext.ljust(3)
        if c not in usados:
            usados.add(c)
            return c.encode("ascii")
    raise ErrorDisco("demasiados nombres parecidos")


def _lfn_entradas(nombre, corto):
    s = 0
    for x in corto:
        s = ((s >> 1) | ((s & 1) << 7)) + x & 0xFF
    u = nombre.encode("utf-16-le")
    u += b"\0\0" if len(u) % 26 else b""
    u = u.ljust((len(u) + 25) // 26 * 26, b"\xff")
    trozos = [u[i:i + 26] for i in range(0, len(u), 26)]
    out = []
    for k in range(len(trozos), 0, -1):
        t = trozos[k - 1]
        e = bytearray(32)
        e[0] = k | (0x40 if k == len(trozos) else 0)
        e[1:11], e[11], e[12], e[13] = t[0:10], 0x0F, 0, s
        e[14:26], e[28:32] = t[10:22], t[22:26]
        out.append(bytes(e))
    return b"".join(out)


def fat32_regiones(part_lba, nsec, etiqueta, archivos, volid):
    """Regiones (desplazamiento en bytes, datos) de un FAT32 completo. archivos: {'EFI/BOOT/BOOTX64.EFI': bytes, 'Música/canción.mp3': ...}
    (nombres largos VFAT cuando no son 8.3; carpetas de cualquier tamano)."""
    spc, rsvd, nf = 8, 32, 2
    t1, t2 = nsec - rsvd, (256 * spc + nf) // 2
    fatsz = (t1 + t2 - 1) // t2
    fds = rsvd + nf * fatsz
    nclus = (nsec - fds) // spc
    cbytes = spc * SECTOR
    base = part_lba * SECTOR
    fat = {0: 0x0FFFFFF8, 1: 0x0FFFFFFF}
    sig = [2]
    regiones = []

    def alloc(n):
        c0 = sig[0]
        sig[0] += n
        if sig[0] - 2 > nclus:
            raise SinSitio("los archivos no caben en la particion FAT32")
        for k in range(n):
            fat[c0 + k] = (c0 + k + 1) if k < n - 1 else 0x0FFFFFFF
        return c0

    def coff(c):
        return base + (fds + (c - 2) * spc) * SECTOR

    dirs = {"": []}
    for ruta in sorted(archivos):
        partes = ruta.split("/")
        for i in range(1, len(partes)):
            d = "/".join(partes[:i])
            if d not in dirs:
                dirs[d] = []
                dirs["/".join(partes[:i - 1])].append(("d", partes[i - 1], d))
        dirs["/".join(partes[:-1])].append(("f", partes[-1], ruta))
    # nombres cortos y entradas de cada carpeta (para saber cuantos cluster ocupa)
    nombres = {}
    for d, items in dirs.items():
        usados = set()
        for _, nombre, ruta in items:
            if _es83(nombre):
                b8, _, e3 = nombre.partition(".")
                corto = (b8.ljust(8) + e3.ljust(3)).encode("ascii")
                usados.add(corto.decode())
                nombres[ruta] = (corto, b"")
        for _, nombre, ruta in items:
            if ruta not in nombres:
                corto = _alias(nombre, usados)
                nombres[ruta] = (corto, _lfn_entradas(nombre, corto))
    tam_dir = {d: 32 * (1 if d == "" else 2) + sum(32 + len(nombres[r][1]) for _, _, r in items) + 32 for d, items in dirs.items()}
    clus = {}
    for d in sorted(dirs):
        clus[d] = alloc(max(1, (tam_dir[d] + cbytes - 1) // cbytes))
    for d, items in dirs.items():
        raw = bytearray()
        if d == "":
            raw += _ent(etiqueta.upper()[:11].ljust(11).encode("ascii"), 0x08, 0, 0)
        else:
            padre = "/".join(d.split("/")[:-1])
            raw += _ent(b".          ", 0x10, clus[d], 0) + _ent(b"..         ", 0x10, clus[padre] if padre else 0, 0)
        for tipo, nombre, ruta in items:
            corto, lfn = nombres[ruta]
            raw += lfn
            if tipo == "d":
                raw += _ent(corto, 0x10, clus[ruta], 0)
            else:
                datos = archivos[ruta]
                c = alloc((len(datos) + cbytes - 1) // cbytes) if datos else 0
                if datos:
                    regiones.append((coff(c), datos))
                raw += _ent(corto, 0x20, c, len(datos))
        n = max(1, (tam_dir[d] + cbytes - 1) // cbytes)
        regiones.append((coff(clus[d]), bytes(raw).ljust(n * cbytes, b"\0")))
    usados = sig[0] - 2
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x58\x90"
    bs[3:11] = b"SOMA    "
    struct.pack_into("<HBHBHHBHHHII", bs, 11, SECTOR, spc, rsvd, nf, 0, 0, 0xF8, 0, 63, 255, part_lba, nsec)
    struct.pack_into("<IHHIHH", bs, 36, fatsz, 0, 0, clus[""], 1, 6)
    struct.pack_into("<BBBI11s8s", bs, 64, 0x80, 0, 0x29, volid, etiqueta.upper()[:11].ljust(11).encode("ascii"), b"FAT32   ")
    bs[510:512] = b"\x55\xAA"
    fsi = bytearray(SECTOR)
    struct.pack_into("<I", fsi, 0, 0x41615252)
    struct.pack_into("<II", fsi, 484, 0x61417272, nclus - usados)
    struct.pack_into("<I", fsi, 492, sig[0])
    struct.pack_into("<I", fsi, 508, 0xAA550000)
    fsi[510:512] = b"\x55\xAA"
    reservados = bytearray(rsvd * SECTOR)
    reservados[0:SECTOR] = bs
    reservados[SECTOR:2 * SECTOR] = fsi
    reservados[6 * SECTOR:7 * SECTOR] = bs
    reservados[7 * SECTOR:8 * SECTOR] = fsi
    regiones.append((base, bytes(reservados)))
    tabla = bytearray(fatsz * SECTOR)
    for c, v in fat.items():
        struct.pack_into("<I", tabla, c * 4, v)
    for k in range(nf):
        regiones.append((base + (rsvd + k * fatsz) * SECTOR, bytes(tabla)))
    return sorted(regiones)


def escribir_regiones(dev, regiones):
    for off, datos in regiones:
        assert off % SECTOR == 0
        dev.escribir(off // SECTOR, datos)


class Fat32:
    """Lector de FAT12/16/32 con nombres largos (VFAT). Solo lectura."""

    def __init__(self, dev, lba, n):
        self.d, self.lba, self.n = dev, lba, n
        b = dev.leer(lba, 1)
        if b[510:512] != b"\x55\xAA":
            raise ErrorDisco("no es FAT")
        bps, spc, rsvd, nf, nroot, tot16, _, fat16, _, _, _, tot32 = struct.unpack_from("<HBHBHHBHHHII", b, 11)
        if bps != 512 or spc == 0 or spc & (spc - 1) or nf == 0 or rsvd == 0:
            raise ErrorDisco("no es FAT (BPB)")
        fatsz = fat16 or struct.unpack_from("<I", b, 36)[0]
        tot = tot16 or tot32
        self.spc, self.rsvd, self.nf, self.fatsz = spc, rsvd, nf, fatsz
        rootsec = (nroot * 32 + 511) // 512
        self.fds = rsvd + nf * fatsz + rootsec
        if tot > n or self.fds >= tot:
            raise ErrorDisco("FAT con tamano incoherente")
        self.nclus = (tot - self.fds) // spc
        self.tipo = 12 if self.nclus < 4085 else 16 if self.nclus < 65525 else 32
        self.root_lba = rsvd + nf * fatsz
        self.rootsec = rootsec
        self.root_clus = struct.unpack_from("<I", b, 44)[0] if self.tipo == 32 else 0
        off = 0x47 if self.tipo == 32 else 0x2B
        self.etiqueta = b[off:off + 11].decode("latin-1").strip()
        self._fat = dev.leer(lba + rsvd, fatsz)

    def _sig(self, c):
        if self.tipo == 32:
            return struct.unpack_from("<I", self._fat, c * 4)[0] & 0x0FFFFFFF
        if self.tipo == 16:
            return struct.unpack_from("<H", self._fat, c * 2)[0]
        v = struct.unpack_from("<H", self._fat, c * 3 // 2)[0]
        return v >> 4 if c & 1 else v & 0xFFF

    def _fin(self, v):
        return v >= (0x0FFFFFF8 if self.tipo == 32 else 0xFFF8 if self.tipo == 16 else 0xFF8)

    def cadena(self, c):
        out, vistos = [], set()
        while 2 <= c < self.nclus + 2 and not self._fin(c):
            if c in vistos:
                raise ErrorDisco("cadena FAT con un ciclo")
            vistos.add(c)
            out.append(c)
            c = self._sig(c)
        return out

    def _clus_lba(self, c):
        return self.lba + self.fds + (c - 2) * self.spc

    def _dir_bytes(self, clus):
        if clus == 0 and self.tipo != 32:
            return self.d.leer(self.lba + self.root_lba, self.rootsec)
        return b"".join(self.d.leer(self._clus_lba(c), self.spc) for c in self.cadena(clus or self.root_clus))

    def listar(self, clus=None):
        """[(nombre, es_dir, cluster, tamano)] sin . ni .. ni etiquetas"""
        raw = self._dir_bytes(self.root_clus if clus is None or (clus == 0 and self.tipo == 32) else clus)
        out, lfn = [], {}
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5:
                lfn = {}
                continue
            attr = e[11]
            if attr == 0x0F:
                seq = e[0] & 0x1F
                parte = (e[1:11] + e[14:26] + e[28:32]).decode("utf-16-le", "replace")
                lfn[seq] = parte.split("\0")[0].replace("￿", "")
                continue
            if attr & 0x08:
                lfn = {}
                continue
            base, ext = e[0:8].decode("latin-1").rstrip(), e[8:11].decode("latin-1").rstrip()
            if e[0] == 0x05:
                base = "\xe5" + base[1:]
            corto = base + ("." + ext if ext else "")
            if e[12] & 0x08:
                corto = corto.split(".")[0].lower() + ("." + ext if ext else "")
            if e[12] & 0x10 and ext:
                corto = base + "." + ext.lower()
            nombre = "".join(lfn[k] for k in sorted(lfn)) if lfn else corto
            lfn = {}
            if nombre in (".", ".."):
                continue
            c = (struct.unpack_from("<H", e, 20)[0] << 16 | struct.unpack_from("<H", e, 26)[0]) if self.tipo == 32 else struct.unpack_from("<H", e, 26)[0]
            out.append((nombre, bool(attr & 0x10), c, struct.unpack_from("<I", e, 28)[0]))
        return out

    def busca(self, ruta):
        """(nombre, es_dir, cluster, tamano) o None; la raiz es ("", True, None, 0)"""
        ent = ("", True, None, 0)
        for parte in [p for p in ruta.strip("/").split("/") if p]:
            if not ent[1]:
                return None
            ent = next((h for h in self.listar(ent[2]) if h[0].lower() == parte.lower()), None)
            if ent is None:
                return None
        return ent

    def leer(self, ruta=None, ent=None, destino=None):
        ent = ent or self.busca(ruta)
        if ent is None or ent[1]:
            raise ErrorDisco(f"no existe {ruta}")
        tam, cb = ent[3], self.spc * SECTOR
        cs = self.cadena(ent[2]) if tam else []
        if len(cs) * cb < tam:
            raise ErrorDisco(f"{ruta}: la cadena FAT es mas corta que el archivo")
        partes, quedan = [], tam
        i = 0
        while quedan > 0:                      # tramos contiguos de una vez
            j = i
            while j + 1 < len(cs) and cs[j + 1] == cs[j] + 1 and (j + 1 - i) < 256:
                j += 1
            b = self.d.leer(self._clus_lba(cs[i]), (j - i + 1) * self.spc)[:quedan]
            quedan -= len(b)
            if destino:
                destino.write(b)
            else:
                partes.append(b)
            i = j + 1
        return None if destino else b"".join(partes)

    def contiguo(self, ruta):
        """(lba absoluto, sectores) de un archivo contiguo, o None"""
        ent = self.busca(ruta)
        if not ent or ent[1] or not ent[3]:
            return None
        cs = self.cadena(ent[2])
        if any(cs[k + 1] != cs[k] + 1 for k in range(len(cs) - 1)):
            return None
        return self._clus_lba(cs[0]), ent[3] // SECTOR

    def recorrer(self, clus=None, prefijo=""):
        """[(ruta, es_dir, ent)] de todo el arbol"""
        out = []
        for h in self.listar(clus):
            r = prefijo + "/" + h[0]
            out.append((r, h[1], h))
            if h[1] and h[2]:
                out += self.recorrer(h[2], r)
        return out

    def usado(self):
        return sum(h[2][3] for h in self.recorrer() if not h[1])


def es_fat(dev, lba):
    b = dev.leer(lba, 1)
    return b[510:512] == b"\x55\xAA" and (b[0x52:0x57] == b"FAT32" or b[0x36:0x39] == b"FAT")


# ====================================================================== exFAT
def _upcase_del_nucleo():
    """La tabla comprimida que lleva el nucleo (src/core/exfat_upcase.inc). Asi Python y C formatean igual en cualquier version de
    Python (la tabla Unicode de Python cambia de una version a otra)."""
    inc = RAIZ / "src" / "core" / "exfat_upcase.inc"
    t = inc.read_text()
    cuerpo = t[t.index("{", t.index("exfat_upcase_tbl")) + 1:t.rindex("}")]
    vals = [int(x, 0) for x in cuerpo.replace("\n", " ").split(",") if x.strip()]
    return b"".join(v.to_bytes(2, "little") for v in vals)


_UPC = None


def _upcase():
    global _UPC
    if _UPC is None:
        b = _upcase_del_nucleo()
        tabla = list(range(0x10000))
        v = [b[i] | b[i + 1] << 8 for i in range(0, len(b), 2)]
        i = k = 0
        while k < len(v) and i < 0x10000:
            if v[k] == 0xFFFF and k + 1 < len(v):
                i += v[k + 1]
                k += 2
                continue
            tabla[i] = v[k]
            i += 1
            k += 1
        _UPC = (b, tabla)
    return _UPC


def _sum32(b):
    s = 0
    for x in b:
        s = (((s << 31) | (s >> 1)) + x) & 0xFFFFFFFF
    return s


def _boot_sum(b):
    s = 0
    for i in range(11 * 512):
        if i in (106, 107, 112):
            continue
        s = (((s << 31) | (s >> 1)) + b[i]) & 0xFFFFFFFF
    return s


def _set_sum(ents):
    s = 0
    for i, x in enumerate(ents):
        if i in (2, 3):
            continue
        s = (((s << 15) | (s >> 1)) + x) & 0xFFFF
    return s


def _name_hash(u16, tabla):
    h = 0
    for c in u16:
        c = tabla[c]
        for x in (c & 0xFF, c >> 8):
            h = (((h << 15) | (h >> 1)) + x) & 0xFFFF
    return h


def _dos_ahora():
    t = time.localtime()
    return ((t.tm_year - 1980) << 25) | (t.tm_mon << 21) | (t.tm_mday << 16) | (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)


def exfat_formatear(dev, lba, n, etiqueta, serie=None):
    """Formatea la region (lba, n) de dev como exFAT. Mismos parametros que exfat_format (src/core/exfat.c)."""
    if n < 8192:
        raise ErrorDisco("particion demasiado pequena para exFAT")
    serie = int(time.time()) & 0xFFFFFFFF if serie is None else serie
    v = Vista(dev, lba, n)
    nbytes = n * SECTOR
    shift = 3 if nbytes <= (256 << 20) else 6 if nbytes <= (32 << 30) else 8
    spc, cb = 1 << shift, 512 << shift
    fat_off = 2048 if n >= 65536 else 128
    cc = (n - fat_off) // spc
    fat_len = ((cc + 2) * 4 + 511) // 512
    heap = (fat_off + fat_len + spc - 1) & ~(spc - 1)
    clusters = (n - heap) // spc
    if clusters > 0xFFFFFFF5:
        clusters = 0xFFFFFFF5
    upb, _ = _upcase()
    bm_bytes = (clusters + 7) // 8
    bm_cl = (bm_bytes + cb - 1) // cb
    up_cl = (len(upb) + cb - 1) // cb
    bm_first, up_first = 2, 2 + bm_cl
    root = up_first + up_cl
    usados = bm_cl + up_cl + 1
    v.ceros(0, fat_off)
    v.ceros(fat_off, fat_len)
    fat = bytearray((usados + 2) * 4)
    for c in range(usados + 2):
        val = 0xFFFFFFF8 if c == 0 else 0xFFFFFFFF if c == 1 else 0xFFFFFFFF if c + 1 in (up_first, root) or c == root else c + 1
        struct.pack_into("<I", fat, c * 4, val)
    v.escribir(fat_off, bytes(fat))

    def cs(c):
        return heap + (c - 2) * spc

    v.ceros(cs(bm_first), (root + 1 - bm_first) * spc)
    bm = bytearray(bm_cl * cb)
    for i in range(usados):
        bm[i >> 3] |= 1 << (i & 7)
    v.escribir(cs(bm_first), bytes(bm[:((usados + 7) // 8 + 511) // 512 * 512]))
    v.escribir(cs(up_first), upb)
    raiz = bytearray(512)
    lb = etiqueta.encode("utf-16-le")[:22]
    raiz[0] = 0x83
    raiz[1] = len(lb) // 2
    raiz[2:2 + len(lb)] = lb
    raiz[32] = 0x81
    struct.pack_into("<IQ", raiz, 32 + 20, bm_first, bm_bytes)
    raiz[64] = 0x82
    struct.pack_into("<I", raiz, 64 + 4, _sum32(upb))
    struct.pack_into("<IQ", raiz, 64 + 20, up_first, len(upb))
    v.escribir(cs(root), bytes(raiz))
    b = bytearray(12 * 512)
    b[0:3] = b"\xEB\x76\x90"
    b[3:11] = b"EXFAT   "
    struct.pack_into("<QQIIIIIIHH", b, 64, lba, n, fat_off, fat_len, heap, clusters, root, serie, 0x0100, 0)
    b[108], b[109], b[110], b[111], b[112] = 9, shift, 1, 0x80, usados * 100 // clusters
    for i in range(120, 510):
        b[i] = 0xF4
    for s in range(9):
        b[s * 512 + 510:s * 512 + 512] = b"\x55\xAA"
    s = _boot_sum(b)
    for i in range(128):
        struct.pack_into("<I", b, 11 * 512 + i * 4, s)
    v.escribir(0, bytes(b))
    v.escribir(12, bytes(b))


def es_exfat(dev, lba):
    b = dev.leer(lba, 1)
    return b[3:11] == b"EXFAT   " and b[510:512] == b"\x55\xAA"


class _Nodo:
    __slots__ = ("first", "size", "valid", "attr", "nofat", "raiz", "pos", "ents", "nombre")

    def __init__(self, **k):
        for s in self.__slots__:
            setattr(self, s, k.get(s))


class ExFat:
    """exFAT de lectura y escritura (lo que necesitan la grabacion y la migracion: crear carpetas, escribir y borrar archivos).
    Los archivos nuevos van en cluster seguidos (NoFatChain) si hay hueco; si no, en cadena FAT. Las carpetas crecen como hace falta."""

    def __init__(self, dev, lba, n):
        self.v = Vista(dev, lba, n)
        b = self.v.leer(0, 12)
        if b[3:11] != b"EXFAT   ":
            raise ErrorDisco("no es exFAT")
        if _boot_sum(b) != struct.unpack_from("<I", b, 11 * 512)[0]:
            b = self.v.leer(12, 12)
            if b[3:11] != b"EXFAT   " or _boot_sum(b) != struct.unpack_from("<I", b, 11 * 512)[0]:
                raise ErrorDisco("exFAT danado (region de arranque)")
        (_, self.sect, self.fat_off, self.fat_len, self.heap, self.clusters, self.root, self.serie, _, self.flags) = struct.unpack_from("<QQIIIIIIHH", b, 64)
        if b[108] != 9 or b[110] != 1:
            raise ErrorDisco("exFAT no soportado (sector o numero de FAT)")
        self.shift = b[109]
        self.spc, self.cb = 1 << self.shift, 512 << self.shift
        if self.sect > n or self.heap + self.clusters * self.spc > n:
            raise ErrorDisco("exFAT mayor que su particion")
        self.fat = bytearray(self.v.leer(self.fat_off, min(self.fat_len, ((self.clusters + 2) * 4 + 511) // 512)))
        self.fat_sucio = set()
        _, self.tabla = _upcase()
        raiz = _Nodo(first=self.root, size=0, valid=0, attr=0x10, nofat=False, raiz=True)
        self.bm_first = None
        self.etiqueta = ""
        for e, _pos in self._entradas(raiz):
            if e[0] == 0x81:
                self.bm_first, self.bm_bytes = struct.unpack_from("<IQ", e, 20)
            elif e[0] == 0x82:
                up_first, up_bytes = struct.unpack_from("<IQ", e, 20)
                upb = self._leer_cadena(up_first, up_bytes, False)
                if _sum32(upb) != struct.unpack_from("<I", e, 4)[0]:
                    raise ErrorDisco("tabla de mayusculas danada")
                self.tabla = self._descomprime(upb)
            elif e[0] == 0x83:
                self.etiqueta = e[2:2 + 2 * e[1]].decode("utf-16-le", "replace")
        if self.bm_first is None:
            raise ErrorDisco("exFAT sin mapa de bits")
        self.bm = bytearray(self._leer_cadena(self.bm_first, self.bm_bytes, False))
        self.bm_sucio = set()
        self.pista = 2

    # -------------------------------------------------- bajo nivel
    @staticmethod
    def _descomprime(b):
        t = list(range(0x10000))
        v = [b[i] | b[i + 1] << 8 for i in range(0, len(b) - 1, 2)]
        i = k = 0
        comp = len(v) < 0x10000
        while k < len(v) and i < 0x10000:
            if comp and v[k] == 0xFFFF and k + 1 < len(v):
                i += v[k + 1]
                k += 2
                continue
            t[i] = v[k]
            i += 1
            k += 1
        return t

    def _cs(self, c):
        return self.heap + (c - 2) * self.spc

    def _fat_get(self, c):
        return struct.unpack_from("<I", self.fat, c * 4)[0]

    def _fat_set(self, c, val):
        struct.pack_into("<I", self.fat, c * 4, val)
        self.fat_sucio.add(c * 4 // 512)

    def _cadena(self, first, nbytes, nofat):
        if first == 0:
            return []
        n = max(1, (nbytes + self.cb - 1) // self.cb) if nofat else None
        if nofat:
            if first < 2 or first + n > self.clusters + 2:
                raise ErrorDisco("cluster fuera del volumen")
            return list(range(first, first + n))
        out, c = [], first
        while 2 <= c < self.clusters + 2:
            out.append(c)
            if len(out) > self.clusters:
                raise ErrorDisco("cadena FAT con un ciclo")
            c = self._fat_get(c)
        if c < 0xFFFFFFF7 and c != 0xFFFFFFFF:
            raise ErrorDisco("cadena FAT rota")
        return out

    def _leer_cadena(self, first, nbytes, nofat):
        cs = self._cadena(first, nbytes, nofat)
        out = bytearray()
        i = 0
        while i < len(cs) and len(out) < nbytes:
            j = i
            while j + 1 < len(cs) and cs[j + 1] == cs[j] + 1 and j - i < 255:
                j += 1
            out += self.v.leer(self._cs(cs[i]), (j - i + 1) * self.spc)
            i = j + 1
        return bytes(out[:nbytes])

    def _libre(self, c):
        i = c - 2
        return not (self.bm[i >> 3] >> (i & 7)) & 1

    def _marca(self, c, n, uso):
        for k in range(c, c + n):
            i = k - 2
            if uso:
                self.bm[i >> 3] |= 1 << (i & 7)
            else:
                self.bm[i >> 3] &= ~(1 << (i & 7)) & 0xFF
            self.bm_sucio.add((i >> 3) // 512)

    def _tramo(self, n):
        """primer tramo libre de n cluster seguidos (desde la pista) o None"""
        tot = self.clusters
        for vuelta in (self.pista, 2):
            c = vuelta
            run = 0
            ini = c
            while c < tot + 2:
                if self._libre(c):
                    if run == 0:
                        ini = c
                    run += 1
                    if run == n:
                        return ini
                else:
                    run = 0
                c += 1
            if vuelta == 2:
                break
        return None

    def _reserva(self, n):
        """(primer cluster, sin FAT?) para n cluster: seguidos si se puede; si no, en cadena"""
        if n == 0:
            return 0, False                            # vacio: sin cluster y sin NoFatChain (como pide fsck.exfat)
        c = self._tramo(n)
        if c is not None:
            self._marca(c, n, True)
            self.pista = c + n
            return c, True
        libres = [k for k in range(2, self.clusters + 2) if self._libre(k)][:n]
        if len(libres) < n:
            raise SinSitio("no queda sitio en el volumen")
        for a, b in zip(libres, libres[1:]):
            self._fat_set(a, b)
        self._fat_set(libres[-1], 0xFFFFFFFF)
        for k in libres:
            self._marca(k, 1, True)
        return libres[0], False

    def _libera(self, first, nbytes, nofat):
        for c in self._cadena(first, nbytes, nofat):
            self._marca(c, 1, False)
            if not nofat:
                self._fat_set(c, 0)

    def _graba_meta(self):
        for s in sorted(self.fat_sucio):
            self.v.escribir(self.fat_off + s, bytes(self.fat[s * 512:(s + 1) * 512]).ljust(512, b"\0"))
        self.fat_sucio.clear()
        bmc = self._cadena(self.bm_first, self.bm_bytes, False)
        for s in sorted(self.bm_sucio):
            off = s * 512
            ci, dentro = off // self.cb, off % self.cb
            self.v.escribir(self._cs(bmc[ci]) + dentro // 512, bytes(self.bm[off:off + 512]).ljust(512, b"\0"))
        self.bm_sucio.clear()

    def cierra(self):
        self._graba_meta()
        b = bytearray(self.v.leer(0, 12))         # porcentaje de uso (no entra en la suma de comprobacion)
        libres = sum(1 for k in range(2, self.clusters + 2) if self._libre(k)) if self.clusters < (1 << 22) else None
        if libres is not None:
            b[112] = (self.clusters - libres) * 100 // max(1, self.clusters)
            self.v.escribir(0, bytes(b[:512]))
            self.v.escribir(12, bytes(b[:512]))

    # -------------------------------------------------- carpetas
    def _clusters_dir(self, d):
        if d.raiz:
            return self._cadena(d.first, 0, False)
        return self._cadena(d.first, d.size, d.nofat)

    def _entradas(self, d):
        """(entrada de 32 B, (cluster, desplazamiento)) de una carpeta hasta la marca de fin"""
        for c in self._clusters_dir(d):
            raw = self.v.leer(self._cs(c), self.spc)
            for off in range(0, self.cb, 32):
                e = raw[off:off + 32]
                if e[0] == 0:
                    return
                yield e, (c, off)

    def _conjuntos(self, d):
        """[(nombre, attr, first, valid, size, nofat, [posiciones], [entradas])]"""
        it = list(self._entradas(d))
        out, i = [], 0
        while i < len(it):
            e, p = it[i]
            if e[0] != 0x85:
                i += 1
                continue
            n = e[1]
            if n < 2 or n > 18 or i + n >= len(it):
                i += 1
                continue
            grupo = it[i:i + n + 1]
            ents = b"".join(g[0] for g in grupo)
            if struct.unpack_from("<H", e, 2)[0] != _set_sum(ents) or grupo[1][0][0] != 0xC0:
                i += 1
                continue
            st = grupo[1][0]
            nl = st[3]
            nombre = b"".join(g[0][2:32] for g in grupo[2:] if g[0][0] == 0xC1)[:nl * 2].decode("utf-16-le", "replace")
            valid, first, size = struct.unpack_from("<Q", st, 8)[0], struct.unpack_from("<I", st, 20)[0], struct.unpack_from("<Q", st, 24)[0]
            out.append((nombre, struct.unpack_from("<H", e, 4)[0], first, valid, size, bool(st[1] & 2), [g[1] for g in grupo], ents))
            i += n + 1
        return out

    def _igual(self, a, b):
        t = self.tabla
        ua, ub = a.encode("utf-16-le"), b.encode("utf-16-le")
        if len(ua) != len(ub):
            return False
        for i in range(0, len(ua), 2):
            if t[ua[i] | ua[i + 1] << 8] != t[ub[i] | ub[i + 1] << 8]:
                return False
        return True

    def _raiz(self):
        return _Nodo(first=self.root, size=0, valid=0, attr=0x10, nofat=False, raiz=True, nombre="")

    def _busca(self, ruta):
        """(_Nodo) o None. Los nodos llevan .pos y .ents para poder reescribirlos."""
        d = self._raiz()
        for parte in [p for p in ruta.split("/") if p]:
            if not d.attr & 0x10:
                return None
            sig = None
            for c in self._conjuntos(d):
                if self._igual(c[0], parte):
                    sig = _Nodo(first=c[2], size=c[4], valid=c[3], attr=c[1], nofat=c[5], raiz=False, pos=c[6], ents=c[7], nombre=c[0])
                    break
            if sig is None:
                return None
            d = sig
        return d

    def existe(self, ruta):
        return self._busca(ruta) is not None

    def es_carpeta(self, ruta):
        n = self._busca(ruta)
        return n is not None and bool(n.attr & 0x10)

    def listar(self, ruta="/"):
        d = self._busca(ruta)
        if d is None or not d.attr & 0x10:
            raise ErrorDisco(f"no es una carpeta: {ruta}")
        return [(c[0], bool(c[1] & 0x10), c[4]) for c in self._conjuntos(d)]

    def recorrer(self, ruta="/"):
        out = []
        for nombre, es_dir, tam in self.listar(ruta):
            r = ruta.rstrip("/") + "/" + nombre
            out.append((r, es_dir, tam))
            if es_dir:
                out += self.recorrer(r)
        return out

    def leer(self, ruta, destino=None):
        n = self._busca(ruta)
        if n is None or n.attr & 0x10:
            raise ErrorDisco(f"no existe el archivo {ruta}")
        cs = self._cadena(n.first, n.size, n.nofat) if n.first else []
        out, quedan, i = [], n.valid, 0
        while quedan > 0 and i < len(cs):
            j = i
            while j + 1 < len(cs) and cs[j + 1] == cs[j] + 1 and j - i < 255:
                j += 1
            b = self.v.leer(self._cs(cs[i]), (j - i + 1) * self.spc)[:quedan]
            quedan -= len(b)
            (destino.write(b) if destino else out.append(b))
            i = j + 1
        ceros = n.size - n.valid
        if ceros > 0:
            (destino.write(bytes(ceros)) if destino else out.append(bytes(ceros)))
        return None if destino else b"".join(out)

    def _escribe_conjunto(self, pos, ents):
        porclus = {}
        for k, (c, off) in enumerate(pos):
            porclus.setdefault(self._cs(c) + off // 512, []).append((off % 512, ents[k * 32:k * 32 + 32]))
        for s, trozos in porclus.items():
            sec = bytearray(self.v.leer(s, 1))
            for o, e in trozos:
                sec[o:o + 32] = e
            self.v.escribir(s, bytes(sec))

    def _hueco(self, d, k):
        """k posiciones libres seguidas en la carpeta d (la agranda si hace falta). Devuelve [(cluster, off)]"""
        while True:
            cs = self._clusters_dir(d)
            libres = []
            fin = False
            for c in cs:
                raw = self.v.leer(self._cs(c), self.spc)
                for off in range(0, self.cb, 32):
                    t = raw[off]
                    if fin or t == 0 or not t & 0x80:
                        if t == 0:
                            fin = True
                        libres.append((c, off))
                        if len(libres) == k:
                            return libres
                    else:
                        libres = []
            self._agranda(d)

    def _agranda(self, d):
        cs = self._clusters_dir(d)
        ult = cs[-1]
        if d.raiz:
            nuevo, _ = self._reserva(1)
            self._fat_set(ult, nuevo)
            self._fat_set(nuevo, 0xFFFFFFFF)
            self.v.ceros(self._cs(nuevo), self.spc)
            return
        if d.nofat and ult + 1 < self.clusters + 2 and self._libre(ult + 1):
            self._marca(ult + 1, 1, True)
            nuevo = ult + 1
        else:
            nuevo, _ = self._reserva(1)
            if d.nofat:                                # pasa a cadena FAT
                for a, b in zip(cs, cs[1:]):
                    self._fat_set(a, b)
                d.nofat = False
            self._fat_set(ult, nuevo)
            self._fat_set(nuevo, 0xFFFFFFFF)
        self.v.ceros(self._cs(nuevo), self.spc)
        d.size += self.cb
        d.valid = d.size
        self._actualiza_stream(d)

    def _actualiza_stream(self, n):
        ents = bytearray(n.ents)
        ents[32 + 1] = (ents[32 + 1] & ~2 & 0xFF) | (2 if n.nofat else 0) | 1
        struct.pack_into("<Q", ents, 32 + 8, n.valid)
        struct.pack_into("<I", ents, 32 + 20, n.first)
        struct.pack_into("<Q", ents, 32 + 24, n.size)
        t = _dos_ahora()
        struct.pack_into("<I", ents, 12, t)
        struct.pack_into("<I", ents, 16, t)
        struct.pack_into("<H", ents, 2, _set_sum(bytes(ents)))
        n.ents = bytes(ents)
        self._escribe_conjunto(n.pos, n.ents)

    def _nuevo_conjunto(self, d, nombre, attr, first, size, nofat):
        u16 = nombre.encode("utf-16-le")
        nl = len(u16) // 2
        if not 1 <= nl <= 255 or any(ch in nombre for ch in '"*/:<>?\\|') or any(ord(ch) < 32 for ch in nombre) or nombre in (".", ".."):
            raise ErrorDisco(f"nombre no valido para exFAT: {nombre!r}")
        nn = (nl + 14) // 15
        ents = bytearray(32 * (2 + nn))
        t = _dos_ahora()
        ents[0], ents[1] = 0x85, 1 + nn
        struct.pack_into("<HHHIII", ents, 2, 0, attr, 0, t, t, t)
        ents[32] = 0xC0
        ents[33] = 1 | (2 if nofat else 0)
        ents[35] = nl
        struct.pack_into("<H", ents, 36, _name_hash([u16[i] | u16[i + 1] << 8 for i in range(0, len(u16), 2)], self.tabla))
        struct.pack_into("<QIIQ", ents, 40, size, 0, first, size)
        for k in range(nn):
            e = 64 + 32 * k
            ents[e] = 0xC1
            trozo = u16[k * 30:(k + 1) * 30]
            ents[e + 2:e + 2 + len(trozo)] = trozo
        struct.pack_into("<H", ents, 2, _set_sum(bytes(ents)))
        pos = self._hueco(d, 2 + nn)
        self._escribe_conjunto(pos, bytes(ents))
        return pos, bytes(ents)

    def _padre(self, ruta):
        partes = [p for p in ruta.split("/") if p]
        if not partes:
            raise ErrorDisco("ruta vacia")
        d = self._busca("/" + "/".join(partes[:-1]))
        if d is None or not d.attr & 0x10:
            raise ErrorDisco(f"no existe la carpeta de {ruta}")
        return d, partes[-1]

    def mkdir(self, ruta, existe_ok=True):
        n = self._busca(ruta)
        if n is not None:
            if n.attr & 0x10 and existe_ok:
                return
            raise ErrorDisco(f"ya existe {ruta}")
        d, nombre = self._padre(ruta)
        c, _ = self._reserva(1)
        self.v.ceros(self._cs(c), self.spc)
        self._nuevo_conjunto(d, nombre, 0x10, c, self.cb, True)

    def mkdirs(self, ruta):
        acc = ""
        for p in [p for p in ruta.split("/") if p]:
            acc += "/" + p
            self.mkdir(acc)

    def escribir(self, ruta, datos=None, origen=None, tam=None):
        """Crea o sustituye un archivo. datos: bytes; u origen: archivo abierto (se lee a trozos) con tam bytes."""
        tam = len(datos) if datos is not None else tam
        viejo = self._busca(ruta)
        if viejo is not None and viejo.attr & 0x10:
            raise ErrorDisco(f"{ruta} es una carpeta")
        d, nombre = self._padre(ruta)
        ncl = (tam + self.cb - 1) // self.cb
        first, nofat = self._reserva(ncl)
        try:
            cs = self._cadena(first, tam, nofat) if ncl else []
            hecho, i = 0, 0
            while i < len(cs):
                j = i
                while j + 1 < len(cs) and cs[j + 1] == cs[j] + 1 and j - i < 255:
                    j += 1
                nb = min((j - i + 1) * self.cb, tam - hecho)
                trozo = datos[hecho:hecho + nb] if datos is not None else origen.read(nb)
                if len(trozo) != nb:
                    raise ErrorDisco("el archivo de origen se ha acortado")
                self.v.escribir(self._cs(cs[i]), trozo)
                hecho += nb
                i = j + 1
        except Exception:
            if ncl:
                self._libera(first, tam, nofat)
            raise
        if viejo is not None:                          # el viejo se borra despues de tener el nuevo completo
            self._borra_nodo(viejo)
            d, nombre = self._padre(ruta)
        self._nuevo_conjunto(d, viejo.nombre if viejo is not None else nombre, 0x20, first, tam, nofat)
        self._graba_meta()

    def _borra_nodo(self, n):
        ents = bytearray(n.ents)
        for k in range(0, len(ents), 32):
            ents[k] &= 0x7F
        self._escribe_conjunto(n.pos, bytes(ents))
        if n.first:
            self._libera(n.first, n.size, n.nofat)

    def borrar(self, ruta):
        n = self._busca(ruta)
        if n is None:
            raise ErrorDisco(f"no existe {ruta}")
        if n.raiz:
            raise ErrorDisco("la raiz no se borra")
        if n.attr & 0x10 and self._conjuntos(n):
            raise ErrorDisco(f"la carpeta {ruta} no esta vacia")
        self._borra_nodo(n)
        self._graba_meta()

    def libre(self):
        return sum(1 for k in range(2, self.clusters + 2) if self._libre(k)) * self.cb

    def copia_arbol(self, origen, destino="/", al_copiar=None):
        """copia una carpeta del equipo dentro del volumen (fusiona carpetas; un archivo con el nombre de una carpeta va con sufijo)"""
        origen = Path(origen)
        self.mkdirs(destino)
        for entrada in sorted(origen.iterdir(), key=lambda p: p.name):
            if entrada.name in (".DS_Store",) or entrada.name.startswith("._"):
                continue
            dst = destino.rstrip("/") + "/" + entrada.name
            if entrada.is_dir():
                if self.existe(dst) and not self.es_carpeta(dst):
                    dst += " (carpeta)"
                self.copia_arbol(entrada, dst, al_copiar)
            elif entrada.is_file():
                if self.existe(dst) and self.es_carpeta(dst):
                    dst += " (archivo)"
                with open(entrada, "rb") as f:
                    self.escribir(dst, origen=f, tam=entrada.stat().st_size)
                if al_copiar:
                    al_copiar(dst, entrada.stat().st_size)


# ====================================================================== DATOS
def marca_datos(part_lba, part_n, sig):
    s = bytearray(512)
    s[0:16] = UD_MARK
    struct.pack_into("<III", s, 16, part_lba & 0xFFFFFFFF, part_n & 0xFFFFFFFF, sig)
    t = b"SOMA: disco del sistema (no formatear si quieres conservar tus archivos)"
    s[32:32 + len(t)] = t
    s[510:512] = b"\x55\xAA"
    return bytes(s)


def lee_marca(dev, lba):
    s = dev.leer(lba, 1)
    if s[0:16] != UD_MARK:
        return None
    return struct.unpack_from("<III", s, 16)


def preparar_datos(dev, part, sig):
    """DATOS nueva: 8 MiB a cero (buzon, VFS, diario, checkpoints, fuentes y la cabecera de BIGFS) y la marca. SOMA la reconoce al
    arrancar y formatea su sistema de archivos dentro (lo mismo que hacia al reclamar SOMADATA)."""
    dev.ceros(part.lba + UD_DATA_OFF, min(UD_ZERO_SECT, part.n - UD_DATA_OFF))
    dev.escribir(part.lba, marca_datos(part.lba, part.n, sig))


# ====================================================================== que hay en el pendrive
def analizar(dev):
    """Describe el pendrive: {'tipo': 'vacio'|'nuevo'|'antiguo'|'ajeno', 'sig', 'parts', y lo que haya que conservar}."""
    info = {"tipo": "vacio", "sig": None, "parts": [], "sistema": None, "datos": None, "modelos": None, "usuario": None,
            "somadata_img": None, "datos_fat": None, "modelos_fat": [], "informe": None, "datos_virgen": False, "avisos": []}
    try:
        sig, ps = leer_mbr(dev)
    except ErrorDisco as e:
        info["tipo"] = "ajeno"
        info["avisos"].append(str(e))
        return info
    info["sig"], info["parts"] = sig, ps
    if sig is None or not ps:
        return info
    info["tipo"] = "ajeno"
    for p in ps:
        if p.fin > dev.sectores:
            info["avisos"].append(f"la particion {p.idx + 1} se sale del disco")
            continue
        m = lee_marca(dev, p.lba)
        if m is not None and info["datos"] is None:
            info["datos"] = p
            continue
        if es_exfat(dev, p.lba):
            try:
                ex = ExFat(dev, p.lba, p.n)
                if ex.etiqueta.upper() == "MODELOS" and info["modelos"] is None:
                    info["modelos"] = p
                elif ex.etiqueta.upper() == "USUARIO" and info["usuario"] is None:
                    info["usuario"] = p
            except ErrorDisco as e:
                info["avisos"].append(f"particion {p.idx + 1}: {e}")
            continue
        if es_fat(dev, p.lba):
            try:
                fs = Fat32(dev, p.lba, p.n)
            except ErrorDisco as e:
                info["avisos"].append(f"particion {p.idx + 1}: {e}")
                continue
            et = fs.etiqueta.upper()
            if et in ("SOMA", "SISTEMA") and info["sistema"] is None:
                info["sistema"] = p
                for nombre, es_dir, c, tam in fs.listar():
                    if nombre.upper() == "SOMADATA.IMG" and not es_dir:
                        info["somadata_img"] = (p, nombre, tam)
                    if nombre.upper() == "INFORME.TXT" and not es_dir:
                        info["informe"] = nombre
                mod = fs.busca("/MODELOS")
                if mod and mod[1]:
                    for nombre, es_dir, c, tam in fs.listar(mod[2]):
                        if not es_dir and not nombre.startswith("._") and tam > 0:
                            info["modelos_fat"].append(("/MODELOS/" + nombre, tam))
            elif et == "SOMADATA" and info["datos"] is None:
                info["datos"] = p                       # SOMADATA aun sin reclamar: nada que conservar
                info["datos_virgen"] = True
            elif et == "DATOS" and info["datos_fat"] is None:
                info["datos_fat"] = p
    if info["sistema"] and info["datos"] and info["modelos"] and info["usuario"] and info["datos"].tipo == TIPO_DATOS:
        info["tipo"] = "nuevo"
    elif info["sistema"] and (info["datos"] or info["somadata_img"]):
        info["tipo"] = "antiguo"
    elif info["sistema"]:
        info["tipo"] = "antiguo"                        # SOMA sin disco de datos: se conserva lo que haya en la particion
    return info


def describe(info):
    t = {"vacio": "sin particiones", "ajeno": "no es un pendrive de SOMA", "nuevo": "SOMA con 4 particiones",
         "antiguo": "SOMA antiguo"}[info["tipo"]]
    partes = []
    for k in ("sistema", "datos", "modelos", "usuario", "datos_fat"):
        p = info[k]
        if p:
            partes.append(f"{k.upper().replace('_FAT', ' (FAT32)')} {p.n * SECTOR / 1e9:.1f} GB")
    if info["somadata_img"]:
        partes.append(f"SOMADATA.IMG {info['somadata_img'][2] / 1e9:.1f} GB")
    return t + (": " + ", ".join(partes) if partes else "")


# ====================================================================== grabar
def _copia_region(dev, lba, n, f):
    hecho = 0
    while hecho < n:
        k = min(n - hecho, 8192)
        f.write(dev.leer(lba + hecho, k))
        hecho += k


def _escribe_region(dev, lba, f, n):
    hecho = 0
    while hecho < n:
        k = min(n - hecho, 8192)
        b = f.read(k * SECTOR)
        if len(b) != k * SECTOR:
            raise ErrorDisco("la copia temporal de DATOS esta incompleta")
        dev.escribir(lba + hecho, b)
        hecho += k


def grabar(dev, efi, leeme=b"", informe_vacio=b"", modelos_equipo=(), formatear=False, temporal=None, log=print,
           confirmar=lambda msg: True, imagen=False):
    """Deja `dev` con las cuatro particiones.

    - Pendrive ya nuevo (4 particiones): SOLO se rehace SISTEMA (con el INFORME.TXT que tuviera). DATOS, MODELOS y USUARIO no se
      tocan, salvo anadir a MODELOS/voz los modelos de `modelos_equipo` que falten.
    - Pendrive antiguo (SOMA + SOMADATA [+ DATOS FAT32] o SOMADATA.IMG): se CONSERVA todo y se migra. Primero se copia a una carpeta
      temporal del equipo lo que va a cambiar de sitio (los archivos de la particion DATOS FAT32, los modelos de SOMA/MODELOS y
      SOMADATA.IMG o una SOMADATA fuera de su sitio). Si no cabe en el equipo, se avisa y NO se toca nada. Despues se reparticiona:
      SOMADATA se queda en su sitio y con su tamano como DATOS (mismo inicio, se cambia el tipo), MODELOS y USUARIO se crean y se
      rellenan con lo copiado.
    - Cualquier otro pendrive (o formatear=True): se borra entero (confirmar() debe devolver True).
    Devuelve un resumen (dict)."""
    info = analizar(dev)
    log(f"Pendrive: {describe(info)}")
    for a in info["avisos"]:
        log(f"  (aviso: {a})")
    sig_vieja = info["sig"]
    res = {"modo": None, "migrado": [], "conservado": []}
    if info["tipo"] == "nuevo" and not formatear:
        res["modo"] = "actualizar"
        return _actualizar(dev, info, efi, leeme, informe_vacio, modelos_equipo, log, res)
    if info["tipo"] == "antiguo" and not formatear:
        res["modo"] = "migrar"
        return _migrar(dev, info, efi, leeme, informe_vacio, modelos_equipo, temporal, log, res)
    if info["tipo"] in ("ajeno", "antiguo", "nuevo") and not imagen:
        if not confirmar(f"Se BORRARA TODO el pendrive ({dev.sectores * SECTOR / 1e9:.1f} GB)."):
            raise ErrorDisco("cancelado: no se ha tocado el pendrive")
    res["modo"] = "nuevo"
    sig = int.from_bytes(os.urandom(4), "little") or 1
    plan = plan_particiones(dev.sectores)
    _crea(dev, plan, sig, efi, leeme, informe_vacio, None, log, imagen=imagen)
    if modelos_equipo:
        _modelos(dev, plan[2], modelos_equipo, log)
    return res


def _sistema(dev, p, efi, leeme, informe):
    archivos = {"EFI/BOOT/BOOTX64.EFI": efi, "LEEME.TXT": leeme, "INFORME.TXT": informe}
    escribir_regiones(dev, fat32_regiones(p.lba, p.n, "SOMA", archivos, 0x50AA0001))


def _crea(dev, plan, sig, efi, leeme, informe, datos_previo, log, imagen=False):
    """MBR + SISTEMA + (DATOS si no se conserva) + MODELOS y USUARIO formateadas con sus carpetas"""
    mbr0 = dev.leer(0, 1)
    dev.ceros(1, P1_LBA - 1)                                   # restos de GPT u otros cargadores
    if dev.sectores > plan[-1].fin + MIB:
        dev.ceros(dev.sectores - MIB, MIB)                     # la copia de GPT del final
    log("  SISTEMA (FAT32, arranque)...")
    _sistema(dev, plan[0], efi, leeme, informe)
    if datos_previo is None:
        log("  DATOS (formato de SOMA)...")
        preparar_datos(dev, plan[1], sig)
    s3, s4 = SERIE_IMAGEN if imagen else (int.from_bytes(os.urandom(4), "little"), int.from_bytes(os.urandom(4), "little"))
    log(f"  MODELOS (exFAT, {plan[2].n * SECTOR / 1e9:.1f} GB)...")
    exfat_formatear(dev, plan[2].lba, plan[2].n, "MODELOS", s3)
    ex = ExFat(dev, plan[2].lba, plan[2].n)
    for c in CARPETAS_MODELOS:
        ex.mkdir("/" + c)
    ex.cierra()
    log(f"  USUARIO (exFAT, {plan[3].n * SECTOR / 1e9:.1f} GB)...")
    exfat_formatear(dev, plan[3].lba, plan[3].n, "USUARIO", s4)
    ex = ExFat(dev, plan[3].lba, plan[3].n)
    for c in CARPETAS_USUARIO:
        ex.mkdir("/" + c)
    ex.cierra()
    dev.escribir(0, mbr_bytes(plan, sig, mbr0))               # la tabla, lo ultimo
    dev.vacia()


def _informe(dev, info, informe_vacio):
    if info["sistema"] and info["informe"]:
        try:
            b = Fat32(dev, info["sistema"].lba, info["sistema"].n).leer("/" + info["informe"])
            if len(b) == len(informe_vacio):
                return b
        except ErrorDisco:
            pass
    return informe_vacio


def _modelos(dev, p, modelos_equipo, log, ya=None):
    ex = ExFat(dev, p.lba, p.n)
    ex.mkdirs("/voz")
    for m in modelos_equipo:
        m = Path(m)
        dst = "/voz/" + m.name
        if ex.existe(dst) and ex._busca(dst).size == m.stat().st_size:
            continue
        if m.stat().st_size + (64 << 20) > ex.libre():
            log(f"  (no cabe {m.name} en MODELOS: se salta)")
            continue
        log(f"  modelo {m.name} ({m.stat().st_size >> 20} MiB) -> MODELOS/voz")
        with open(m, "rb") as f:
            ex.escribir(dst, origen=f, tam=m.stat().st_size)
    ex.cierra()


def _actualizar(dev, info, efi, leeme, informe_vacio, modelos_equipo, log, res):
    log("Solo se actualiza SISTEMA: DATOS, MODELOS y USUARIO se conservan tal cual.")
    informe = _informe(dev, info, informe_vacio)
    p = info["sistema"]
    if p.n < SISTEMA_SECT // 2:
        raise ErrorDisco("la particion SISTEMA es demasiado pequena")
    _sistema(dev, p, efi, leeme, informe)
    res["conservado"] = ["DATOS", "MODELOS", "USUARIO"]
    if modelos_equipo:
        _modelos(dev, info["modelos"], modelos_equipo, log)
    dev.vacia()
    return res


def _migrar(dev, info, efi, leeme, informe_vacio, modelos_equipo, temporal, log, res):
    """pendrive antiguo -> cuatro particiones conservando todo (ver grabar)"""
    sig = info["sig"] or (int.from_bytes(os.urandom(4), "little") or 1)
    p2_std = P1_LBA + SISTEMA_SECT
    datos = info["datos"]
    en_sitio = datos is not None and not info["datos_virgen"] and datos.lba == p2_std
    datos_n = datos.n if en_sitio else DATOS_SECT
    copiar_datos = None                                        # (lba, n): DATOS a mover (fuera de su sitio o SOMADATA.IMG)
    if datos is not None and not info["datos_virgen"] and not en_sitio:
        copiar_datos = (datos.lba, datos.n)
    elif datos is None and info["somadata_img"]:
        fs = Fat32(dev, info["sistema"].lba, info["sistema"].n)
        r = fs.contiguo("/" + info["somadata_img"][1])
        if r is None:
            raise ErrorDisco("SOMADATA.IMG esta fragmentado: no se puede migrar (copialo a mano antes)")
        m = lee_marca(dev, r[0])
        if m is not None:
            copiar_datos = r
        else:
            log("  SOMADATA.IMG aun no lo uso SOMA: no hay nada que conservar en el")
    if copiar_datos:
        datos_n = max(DATOS_SECT, (copiar_datos[1] + MIB - 1) & ~(MIB - 1))
    try:
        plan = plan_particiones(dev.sectores, datos_n)
    except SinSitio:
        if en_sitio:
            raise SinSitio("SOMADATA ocupa casi todo el pendrive y no queda sitio para MODELOS y USUARIO. Copia antes lo que quieras "
                           "conservar (tools/datos.py exportar) y graba con --formatear, o usa un pendrive mayor.")
        raise
    if copiar_datos and copiar_datos[0] < plan[1].fin and copiar_datos[0] + copiar_datos[1] > plan[1].lba and copiar_datos[0] != plan[1].lba:
        pass                                                   # se solapa con su destino: por eso se copia antes al equipo
    # ---- lo que hay que llevarse al equipo
    fs1 = Fat32(dev, info["sistema"].lba, info["sistema"].n)
    datos_fat = Fat32(dev, info["datos_fat"].lba, info["datos_fat"].n) if info["datos_fat"] else None
    necesita = sum(t for _, t in info["modelos_fat"]) + (datos_fat.usado() if datos_fat else 0) + (copiar_datos[1] * SECTOR if copiar_datos else 0)
    base = Path(temporal or tempfile.gettempdir())
    libre = shutil.disk_usage(base).free
    if necesita + (256 << 20) > libre:
        raise SinSitio(f"para conservar tus archivos hay que copiarlos antes al equipo: hacen falta {necesita / 1e9:.1f} GB libres en "
                       f"{base} y hay {libre / 1e9:.1f} GB. No se ha tocado el pendrive (usa --temporal CARPETA en otro disco).")
    tmp = Path(tempfile.mkdtemp(prefix="soma_migracion_", dir=str(base)))
    log(f"Copiando al equipo lo que cambia de sitio ({necesita / 1e9:.2f} GB) en {tmp} ...")
    try:
        (tmp / "modelos").mkdir()
        for ruta, tam in info["modelos_fat"]:
            with open(tmp / "modelos" / Path(ruta).name, "wb") as f:
                fs1.leer(ruta, destino=f)
            res["migrado"].append("MODELOS" + ruta[len("/MODELOS"):] + " -> MODELOS/voz")
        if datos_fat:
            for ruta, es_dir, ent in datos_fat.recorrer():
                if Path(ruta).name.startswith("._") or Path(ruta).name in (".DS_Store", ".Spotlight-V100", ".fseventsd", ".Trashes", "System Volume Information"):
                    continue
                dst = tmp / "usuario" / ruta.lstrip("/")
                if any(part in (".Spotlight-V100", ".fseventsd", ".Trashes", "System Volume Information") for part in Path(ruta).parts):
                    continue
                if es_dir:
                    dst.mkdir(parents=True, exist_ok=True)
                else:
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    with open(dst, "wb") as f:
                        datos_fat.leer(ent=ent, destino=f)
            res["migrado"].append("particion DATOS (FAT32) -> USUARIO")
        if copiar_datos:
            with open(tmp / "datos.raw", "wb") as f:
                _copia_region(dev, copiar_datos[0], copiar_datos[1], f)
            res["migrado"].append("disco de SOMA -> DATOS")
        informe = _informe(dev, info, informe_vacio)
        # ---- verificar la copia antes de tocar nada
        for ruta, tam in info["modelos_fat"]:
            if (tmp / "modelos" / Path(ruta).name).stat().st_size != tam:
                raise ErrorDisco(f"la copia de {ruta} no coincide")
        if copiar_datos and (tmp / "datos.raw").stat().st_size != copiar_datos[1] * SECTOR:
            raise ErrorDisco("la copia de DATOS esta incompleta")
        # ---- reparticionar
        log("Reparticionando: SISTEMA, DATOS, MODELOS y USUARIO" + (" (DATOS se queda donde estaba)" if en_sitio else ""))
        if en_sitio:
            res["conservado"].append("DATOS (en su sitio)")
            if lee_marca(dev, datos.lba) and lee_marca(dev, datos.lba)[1] != datos.n:
                dev.escribir(datos.lba, marca_datos(datos.lba, datos.n, sig))
        _crea(dev, plan, sig, efi, leeme, informe, datos if en_sitio else ("copia" if copiar_datos else None), log)
        if copiar_datos:
            with open(tmp / "datos.raw", "rb") as f:
                f.seek(UD_DATA_OFF * SECTOR)
                _escribe_region(dev, plan[1].lba + UD_DATA_OFF, f, copiar_datos[1] - UD_DATA_OFF)
            dev.escribir(plan[1].lba, marca_datos(plan[1].lba, plan[1].n, sig))
        modelos = sorted((tmp / "modelos").iterdir()) + [Path(m) for m in modelos_equipo]
        if modelos:
            _modelos(dev, plan[2], modelos, log)
        if (tmp / "usuario").exists():
            log("  copiando tus archivos de la particion DATOS antigua a USUARIO...")
            ex = ExFat(dev, plan[3].lba, plan[3].n)
            ex.copia_arbol(tmp / "usuario", "/")
            ex.cierra()
        dev.vacia()
    except BaseException:
        log(f"ERROR: la copia de seguridad de lo migrado sigue en {tmp} (no la borres hasta comprobar el pendrive)")
        raise
    shutil.rmtree(tmp, ignore_errors=True)
    return res
