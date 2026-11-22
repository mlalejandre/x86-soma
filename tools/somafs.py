#!/usr/bin/env python3
"""SOMA_267_DISCOS: el formato de la particion DATOS de SOMA, en Python (para tools/datos.py y la migracion del pendrive).

Dentro de DATOS, tras la marca (8 sectores), esta el disco de SOMA (src/fs/disk_layout.h):
  LBA 1024          superbloque SOMAFS02
  LBA 1025..3136    64 ranuras de 33 sectores: cabecera (nombre, tamano, CRC) + 32 sectores de datos (archivos < 16 KiB)
  LBA 3200..3233    diario de un hueco (SOMAJRN1): una escritura interrumpida se rehace o se descarta (src/fs/slotio.h)
  LBA 31            marca de restauracion de checkpoint en curso (si esta, se termina en SOMA antes de tocar nada)
  LBA 16384..       BIGFS (SOMABIG1, src/fs/bigfs.c): archivos grandes; los de RamFS que no caben en ranura van en /.ramfs/NNN
                    con una cabecera RFS1 (src/fs/spillio.h)
Las escrituras siguen exactamente los mismos protocolos que el nucleo (diario para las ranuras; temporal + renombrar para BIGFS;
inodo de BIGFS en un solo sector con su CRC, los datos antes que el inodo), asi que un corte a mitad deja el archivo viejo o el nuevo.
Lo comprueban tests/test_datos_267.py (contra el C real de bigfs.c, slotio.h y spillio.h)."""
import struct, time, zlib

SEC = 512
FS_SUPER, FS_DATA, FS_SPF, FS_SLOTS, FS_JOURNAL = 1024, 1025, 33, 64, 3200
FS_NAME_MAX, FS_DATA_MAX, FS_MAX_FILES, FS_FILE_MAX = 48, 16384, 256, 32 * 1024 * 1024
LBA_CKPT_INTENT = 31
BIG_START = 16384
BIG_CLUSTER_SECT, BIG_CLUSTER, BIG_INODE, BIG_MAX_EXT, BIG_NAME_MAX = 8, 4096, 256, 20, 63
T_FREE, T_FILE, T_DIR = 0, 1, 2
SECRETOS = ("/etc/wifi.conf", "/etc/history.txt", "/etc/llm.conf")


class ErrorSoma(Exception):
    pass


def crc32(b, crc=0):
    return zlib.crc32(b, crc) & 0xFFFFFFFF


def efimero(nombre):
    return "/sys/" in nombre or "/tmp/" in nombre


def es_secreto(nombre):
    n = "/" + nombre.lstrip("/")
    return n in SECRETOS or n.startswith("/etc/secreto/") or n.endswith(".conf") or "clave" in n.lower() or "token" in n.lower()


def ahora_num():
    t = time.localtime()
    return ((((t.tm_year * 100 + t.tm_mon) * 100 + t.tm_mday) * 100 + t.tm_hour) * 100 + t.tm_min) * 100 + t.tm_sec


class Region:
    """sectores [base, base+n) de un dispositivo con leer(lba, n) / escribir(lba, bytes)"""

    def __init__(self, dev, base, n):
        self.d, self.base, self.sectores = dev, base, n

    def leer(self, lba, n=1):
        if lba < 0 or lba + n > self.sectores:
            raise ErrorSoma("lectura fuera de la region")
        return self.d.leer(self.base + lba, n)

    def escribir(self, lba, datos):
        n = (len(datos) + SEC - 1) // SEC
        if lba < 0 or lba + n > self.sectores:
            raise ErrorSoma("escritura fuera de la region")
        self.d.escribir(self.base + lba, datos)


# ====================================================================== BIGFS
class BigFS:
    def __init__(self, reg):
        self.r = reg
        s = reg.leer(0)
        if s[0:8] != b"SOMABIG1":
            raise ErrorSoma("no hay BIGFS (SOMABIG1)")
        (self.version, self.csect, self.total, self.ninodos, self.istart, self.dstart, self.dclus, crc) = struct.unpack_from("<IIQIIIII", s, 8)
        if crc != crc32(s[0:40]) or self.version != 1 or self.csect != 8 or self.total > reg.sectores or self.istart != 1:
            raise ErrorSoma("superbloque de BIGFS danado")
        ds = (1 + (self.ninodos + 1) // 2 + 7) // 8 * 8
        if ds != self.dstart or self.ninodos < 2 or self.ninodos & 1 or self.dclus != min((self.total - ds) // 8, 0xFFFFFFF0):
            raise ErrorSoma("geometria de BIGFS incoherente")
        raw = reg.leer(1, (self.ninodos + 1) // 2)
        self.tab = [self._ino(raw[i * 256:(i + 1) * 256]) for i in range(self.ninodos)]
        self.descartados = self._analiza()
        self.mapa = bytearray((self.dclus + 7) // 8)
        for i, n in enumerate(self.tab):
            for s0, c in n["ext"]:
                for k in range(s0, s0 + c):
                    self.mapa[k >> 3] |= 1 << (k & 7)

    @staticmethod
    def _ino(b):
        st, crc, par, next_, size, mt = struct.unpack_from("<IIIIQQ", b, 0)
        nombre = b[32:96].split(b"\0")[0]
        ext = [struct.unpack_from("<II", b, 96 + 8 * k) for k in range(BIG_MAX_EXT)]
        return {"state": st, "crc": crc, "parent": par, "next": next_, "size": size, "mtime": mt, "rawname": b[32:96], "name": nombre,
                "extall": ext, "ext": ext[:next_] if next_ <= BIG_MAX_EXT else [], "raw": b}

    def _crc_ok(self, n):
        return n["crc"] == crc32(n["raw"][8:256])

    def _analiza(self):
        """las mismas reglas que analyze() de bigfs.c: lo descartado se trata como libre"""
        n = self.ninodos
        v = [0] * n
        for i, x in enumerate(self.tab):
            if x["state"] == T_FREE:
                continue
            v[i] = 1
            if x["state"] not in (T_FILE, T_DIR) or not self._crc_ok(x):
                v[i] = 2
                continue
            nm = x["rawname"]
            ln = nm.find(b"\0")
            ln = 64 if ln < 0 else ln
            if any(c == 0x2F or c < 32 for c in nm[:ln]):
                ln = -1
            if i == 0:
                if x["state"] != T_DIR or nm[0] != 0 or x["parent"] != 0:
                    v[i] = 2
                continue
            if ln < 1 or ln >= 64 or x["next"] > BIG_MAX_EXT:
                v[i] = 2
                continue
            if x["state"] == T_DIR and (x["next"] or x["size"]):
                v[i] = 2
                continue
            cl = 0
            for s0, c in x["ext"]:
                if c == 0 or s0 + c > self.dclus:
                    v[i] = 2
                    break
                cl += c
            if v[i] == 1 and x["state"] == T_FILE and x["size"] > cl * BIG_CLUSTER:
                v[i] = 2
        if self.tab[0]["state"] != T_DIR:
            v[0] = 2
        for i in range(1, n):
            if v[i] != 1:
                continue
            cur, pasos = i, 0
            while cur != 0:
                p = self.tab[cur]["parent"]
                if p >= n or v[p] != 1 or self.tab[p]["state"] != T_DIR:
                    v[i] = 2
                    break
                cur = p
                pasos += 1
                if pasos > n:
                    v[i] = 2
                    break
        for i in range(1, n):
            if v[i] != 1:
                continue
            for j in range(i + 1, n):
                if v[j] == 1 and self.tab[j]["parent"] == self.tab[i]["parent"] and self.tab[j]["name"] == self.tab[i]["name"]:
                    v[j] = 2
        visto = bytearray((self.dclus + 7) // 8)
        for i in range(n):
            if v[i] != 1:
                continue
            choque = any(visto[k >> 3] >> (k & 7) & 1 for s0, c in self.tab[i]["ext"] for k in range(s0, s0 + c))
            if choque:
                v[i] = 2
                continue
            for s0, c in self.tab[i]["ext"]:
                for k in range(s0, s0 + c):
                    visto[k >> 3] |= 1 << (k & 7)
        if v[0] != 1:
            raise ErrorSoma("BIGFS danado (raiz)")
        malos = 0
        for i in range(n):
            if v[i] == 2:
                malos += 1
                self.tab[i] = self._ino(bytes(256))
        return malos

    # -------------------------------------------------- consulta
    def _hijo(self, d, nombre):
        nb = nombre.encode()
        for i in range(1, self.ninodos):
            x = self.tab[i]
            if x["state"] != T_FREE and x["parent"] == d and x["name"] == nb:
                return i
        return None

    def busca(self, ruta):
        cur = 0
        for p in [p for p in ruta.split("/") if p and p != "."]:
            if p == "..":
                cur = self.tab[cur]["parent"]
                continue
            if self.tab[cur]["state"] != T_DIR:
                return None
            cur = self._hijo(cur, p)
            if cur is None:
                return None
        return cur

    def hijos(self, d):
        return [i for i in range(1, self.ninodos) if self.tab[i]["state"] != T_FREE and self.tab[i]["parent"] == d]

    def recorrer(self, d=0, prefijo=""):
        out = []
        for i in self.hijos(d):
            x = self.tab[i]
            r = prefijo + "/" + x["name"].decode("utf-8", "replace")
            out.append((r, x["state"] == T_DIR, x["size"], i))
            if x["state"] == T_DIR:
                out += self.recorrer(i, r)
        return out

    def leer(self, ino, off=0, n=None):
        x = self.tab[ino]
        n = x["size"] - off if n is None else min(n, max(0, x["size"] - off))
        out = bytearray()
        cidx, pos = 0, 0
        for s0, c in x["ext"]:
            ini, fin = pos, pos + c * BIG_CLUSTER
            a, b = max(off, ini), min(off + n, fin)
            if a < b:
                lba = self.dstart + s0 * 8 + (a - ini) // SEC
                k0 = (a - ini) % SEC
                ns = (k0 + (b - a) + SEC - 1) // SEC
                out += self.r.leer(lba, ns)[k0:k0 + (b - a)]
            pos = fin
        return bytes(out)

    def libres(self):
        return sum(1 for k in range(self.dclus) if not self.mapa[k >> 3] >> (k & 7) & 1)

    # -------------------------------------------------- escritura
    def _commit(self, i):
        x = self.tab[i]
        b = bytearray(256)
        nm = x["name"][:63]
        struct.pack_into("<IIIIQQ", b, 0, x["state"], 0, x["parent"], len(x["ext"]), x["size"], ahora_num())
        b[32:32 + len(nm)] = nm
        for k, (s0, c) in enumerate(x["ext"]):
            struct.pack_into("<II", b, 96 + 8 * k, s0, c)
        struct.pack_into("<I", b, 4, crc32(bytes(b[8:256])))
        self.tab[i] = self._ino(bytes(b))
        self._sector(i)

    def _sector(self, i):
        s = i // 2
        self.r.escribir(self.istart + s, self.tab[2 * s]["raw"] + self.tab[2 * s + 1]["raw"])

    def _libre_ino(self):
        for i in range(1, self.ninodos):
            if self.tab[i]["state"] == T_FREE:
                return i
        raise ErrorSoma("BIGFS sin inodos libres")

    def _padre(self, ruta):
        partes = [p for p in ruta.split("/") if p]
        if not partes:
            raise ErrorSoma("ruta vacia")
        d = self.busca("/" + "/".join(partes[:-1]))
        if d is None or self.tab[d]["state"] != T_DIR:
            raise ErrorSoma(f"no existe la carpeta de {ruta}")
        nombre = partes[-1]
        nb = nombre.encode()
        if not 1 <= len(nb) <= BIG_NAME_MAX or nombre in (".", "..") or any(c < 32 or c == 127 for c in nb):
            raise ErrorSoma(f"nombre no valido en BIGFS: {nombre!r}")
        return d, nb

    def _reserva(self, ncl):
        """extents para ncl cluster: un tramo si se puede; si no, varios (como mucho 20)"""
        if ncl == 0:
            return []
        libre = lambda k: not self.mapa[k >> 3] >> (k & 7) & 1
        run, ini = 0, 0
        tramos = []
        for k in range(self.dclus):
            if libre(k):
                if run == 0:
                    ini = k
                run += 1
                if run == ncl:
                    return [(ini, ncl)]
            else:
                if run:
                    tramos.append((ini, run))
                run = 0
        if run:
            tramos.append((ini, run))
        tramos.sort(key=lambda t: -t[1])
        out, falta = [], ncl
        for s0, c in tramos:
            if falta == 0:
                break
            t = min(c, falta)
            out.append((s0, t))
            falta -= t
        if falta:
            raise ErrorSoma("BIGFS sin sitio")
        if len(out) > BIG_MAX_EXT:
            raise ErrorSoma("BIGFS demasiado fragmentado para este archivo")
        return sorted(out)

    def _marca(self, ext, uso):
        for s0, c in ext:
            for k in range(s0, s0 + c):
                if uso:
                    self.mapa[k >> 3] |= 1 << (k & 7)
                else:
                    self.mapa[k >> 3] &= ~(1 << (k & 7)) & 0xFF

    def crear(self, ruta, datos):
        """archivo nuevo (falla si existe): datos primero, inodo despues (un sector)"""
        d, nb = self._padre(ruta)
        if self._hijo(d, nb.decode()) is not None:
            raise ErrorSoma(f"ya existe {ruta}")
        i = self._libre_ino()
        ext = self._reserva((len(datos) + BIG_CLUSTER - 1) // BIG_CLUSTER)
        pos = 0
        for s0, c in ext:
            trozo = datos[pos:pos + c * BIG_CLUSTER]
            self.r.escribir(self.dstart + s0 * 8, trozo)
            pos += len(trozo)
        self._marca(ext, True)
        x = self._ino(bytes(256))
        x.update(state=T_FILE, parent=d, name=nb, ext=ext, size=len(datos))
        self.tab[i] = x
        self._commit(i)
        return i

    def mkdir(self, ruta):
        d, nb = self._padre(ruta)
        if self._hijo(d, nb.decode()) is not None:
            return
        i = self._libre_ino()
        x = self._ino(bytes(256))
        x.update(state=T_DIR, parent=d, name=nb, ext=[], size=0)
        self.tab[i] = x
        self._commit(i)

    def borrar(self, ruta):
        i = self.busca(ruta)
        if i is None or i == 0:
            raise ErrorSoma(f"no existe {ruta}")
        if self.tab[i]["state"] == T_DIR and self.hijos(i):
            raise ErrorSoma(f"la carpeta {ruta} no esta vacia")
        ext = self.tab[i]["ext"]
        self.tab[i] = self._ino(bytes(256))
        self._sector(i)                                # libre: todo a cero, sin CRC (como remove_node)
        self._marca(ext, False)

    def renombrar(self, de, a):
        i = self.busca(de)
        if i is None or i == 0:
            raise ErrorSoma(f"no existe {de}")
        d, nb = self._padre(a)
        if self._hijo(d, nb.decode()) is not None:
            raise ErrorSoma(f"ya existe {a}")
        x = dict(self.tab[i])
        x.update(parent=d, name=nb)
        self.tab[i] = x
        self._commit(i)


# ====================================================================== VFS (SOMAFS02)
def spill_ruta(i):
    return "/.ramfs/%03d" % i


def spill_tmp(i):
    return "/.ramfs/t%03d" % i


class Soma:
    """El disco de SOMA (la parte de datos de DATOS: lba 0 = 8 sectores despues de la marca)."""

    def __init__(self, reg, escribir=False):
        self.r, self.escribe = reg, escribir
        sb = reg.leer(FS_SUPER)
        if sb[0:8] != b"SOMAFS02" or struct.unpack_from("<III", sb, 8)[0] != 2 or struct.unpack_from("<III", sb, 8)[2] != FS_SLOTS:
            raise ErrorSoma("DATOS aun no tiene el sistema de archivos de SOMA (arranca SOMA una vez desde el pendrive)")
        if reg.leer(LBA_CKPT_INTENT)[0:20] == b"SOMA_RESTORE_PENDING":
            raise ErrorSoma("hay una restauracion de checkpoint a medias: arranca SOMA una vez para que la termine")
        try:
            self.big = BigFS(Region(reg, BIG_START, reg.sectores - BIG_START)) if reg.sectores > BIG_START + 64 else None
        except ErrorSoma:
            self.big = None
        self.diario_pendiente = self._diario()
        if self.diario_pendiente and escribir:
            self._recupera_diario()
            self.diario_pendiente = None
        if escribir and self.big:
            self._recupera_spill()
        self._carga()

    # -------------------------------------------------- diario y recuperaciones (como slotio_recover y spill_recover)
    def _diario(self):
        j = self.r.leer(FS_JOURNAL)
        if j[0:8] != b"SOMAJRN1":
            return None
        seq, slot, nd, pcrc, hcrc = struct.unpack_from("<IIIII", j, 8)
        if crc32(j[0:24]) != hcrc or slot >= FS_SLOTS or nd > 32:
            return ("descartar",)
        hdr = self.r.leer(FS_JOURNAL + 1)
        img = self.r.leer(FS_JOURNAL + 2, nd) if nd else b""
        if crc32(img, crc32(hdr)) != pcrc:
            return ("descartar",)
        return ("rehacer", slot, hdr, img)

    def _recupera_diario(self):
        d = self.diario_pendiente
        if d[0] == "rehacer":
            _, slot, hdr, img = d
            size = min(struct.unpack_from("<I", hdr, 48)[0], len(img))
            self._aplica(slot, hdr, img[:size])
        self.r.escribir(FS_JOURNAL, bytes(SEC))

    def _aplica(self, slot, hdr, datos):
        base = FS_DATA + slot * FS_SPF
        self.r.escribir(base + 1, datos.ljust(32 * SEC, b"\0"))
        self.r.escribir(base, hdr)

    def _spill_valido(self, ruta):
        i = self.big.busca(ruta)
        if i is None or self.big.tab[i]["state"] != T_FILE or self.big.tab[i]["size"] < 64:
            return None
        h = self.big.leer(i, 0, 64)
        if h[0:4] != b"RFS1":
            return None
        ln = struct.unpack_from("<I", h, 52)[0]
        return (i, h, ln) if self.big.tab[i]["size"] == 64 + ln else None

    def _recupera_spill(self):
        if self.big.busca("/.ramfs") is None:
            return
        for s in range(FS_MAX_FILES):
            t = self.big.busca(spill_tmp(s))
            if t is None:
                continue
            if self.big.busca(spill_ruta(s)) is None and self._spill_valido(spill_tmp(s)):
                self.big.renombrar(spill_tmp(s), spill_ruta(s))
            else:
                self.big.borrar(spill_tmp(s))

    # -------------------------------------------------- carga (como vfs_init / load_file_from_disk)
    def _spill(self, s):
        if not self.big:
            return None
        v = self._spill_valido(spill_ruta(s))
        if v is None and not self.escribe and self.big.busca(spill_ruta(s)) is None:
            v = self._spill_valido(spill_tmp(s))            # solo lectura: lo que haria spill_recover
        if v is None:
            i = self.big.busca(spill_ruta(s))
            if i is None:
                return None
            h = self.big.leer(i, 0, 64)
            if h[0:4] != b"RFS1":
                return None
            v = (i, h, struct.unpack_from("<I", h, 52)[0])
        i, h, ln = v
        return h[4:52].split(b"\0")[0].decode("utf-8", "replace"), ln, i

    def _carga(self):
        self.ranura = {}            # indice -> dict(nombre, tam, donde, crc_ok)
        self.avisos = []
        for s in range(FS_SLOTS):
            base = FS_DATA + s * FS_SPF
            h = self.r.leer(base)
            if self.diario_pendiente and self.diario_pendiente[0] == "rehacer" and self.diario_pendiente[1] == s:
                h = self.diario_pendiente[2]
            used, spilled = h[52], h[53]
            if used == 0:
                continue
            if used > 1:
                self.avisos.append(f"ranura {s}: cabecera no valida")
                continue
            nombre = h[0:48].split(b"\0")[0]
            if not nombre or b"\0" not in h[0:48]:
                self.avisos.append(f"ranura {s}: nombre no valido")
                continue
            nombre = nombre.decode("utf-8", "replace")
            size = struct.unpack_from("<I", h, 48)[0]
            if spilled == ord("B"):
                sp = self._spill(s)
                if size > FS_FILE_MAX or sp is None:
                    self.avisos.append(f"{nombre}: su contenido en BIGFS no esta")
                    continue
                if sp[1] != size:
                    nombre, size = sp[0], sp[1]           # el de BIGFS es el mas reciente (PARCHE 113)
                self.ranura[s] = {"nombre": nombre, "tam": size, "donde": "BIGFS", "big": sp[2]}
                continue
            if size >= FS_DATA_MAX:
                self.avisos.append(f"ranura {s}: tamano no valido")
                continue
            crc_ok = None
            if h[58] == ord("C"):                          # PARCHE 112: CRC en pad[0..3] (byte 54), marca 'C' en pad[4]
                datos = self._datos_ranura(s, size)
                crc_ok = crc32(datos) == struct.unpack_from("<I", h, 54)[0]
                if not crc_ok:
                    self.avisos.append(f"{nombre}: no coincide con su CRC (dato danado)")
            self.ranura[s] = {"nombre": nombre, "tam": size, "donde": "ranura", "crc_ok": crc_ok}
        if self.big:
            for s in range(FS_SLOTS, FS_MAX_FILES):
                sp = self._spill(s)
                if sp and sp[0] and sp[1] <= FS_FILE_MAX:
                    self.ranura[s] = {"nombre": sp[0], "tam": sp[1], "donde": "BIGFS", "big": sp[2]}

    def _datos_ranura(self, s, size):
        if self.diario_pendiente and self.diario_pendiente[0] == "rehacer" and self.diario_pendiente[1] == s:
            return self.diario_pendiente[3][:size].ljust(size, b"\0")
        return self.r.leer(FS_DATA + s * FS_SPF + 1, 32)[:size]

    # -------------------------------------------------- consulta
    def archivos(self):
        return sorted(((v["nombre"], v["tam"], v["donde"], s) for s, v in self.ranura.items()), key=lambda t: t[0])

    def _indice(self, nombre):
        q = nombre
        for s, v in self.ranura.items():
            n = v["nombre"]
            if n == q or (n.startswith("/") and n[1:] == q) or (q.startswith("/") and q[1:] == n):
                return s
        return None

    def leer(self, nombre):
        s = self._indice(nombre)
        if s is None:
            raise ErrorSoma(f"no existe {nombre}")
        v = self.ranura[s]
        if v["donde"] == "BIGFS":
            return self.big.leer(v["big"], 64, v["tam"])
        return self._datos_ranura(s, v["tam"])

    # -------------------------------------------------- escritura (como sync_file_to_disk)
    def _super(self):
        sb = bytearray(SEC)
        sb[0:8] = b"SOMAFS02"
        n = sum(1 for s, v in self.ranura.items() if s < FS_SLOTS)
        struct.pack_into("<IIII", sb, 8, 2, n, FS_SLOTS, FS_DATA_MAX)
        self.r.escribir(FS_SUPER, bytes(sb))

    def _cabecera(self, nombre, size, spilled, datos=None):
        h = bytearray(SEC)
        nb = nombre.encode()
        h[0:len(nb)] = nb
        struct.pack_into("<I", h, 48, size)
        h[52] = 1
        h[53] = ord("B") if spilled else 0
        if not spilled:
            struct.pack_into("<I", h, 54, crc32(datos))
            h[58] = ord("C")
        return bytes(h)

    def _commit_ranura(self, s, hdr, datos):
        """slotio_commit: diario (datos, cabecera, compromiso), en su sitio, y diario a cero"""
        nd = (len(datos) + SEC - 1) // SEC
        cuerpo = datos.ljust(nd * SEC, b"\0")
        if nd:
            self.r.escribir(FS_JOURNAL + 2, cuerpo)
        self.r.escribir(FS_JOURNAL + 1, hdr)
        self._barrera()                                # la carga del diario, durable antes del compromiso
        j = bytearray(SEC)
        j[0:8] = b"SOMAJRN1"
        self.seq = getattr(self, "seq", int(time.time()) & 0x7FFFFFFF) + 1
        struct.pack_into("<IIII", j, 8, self.seq, s, nd, crc32(cuerpo, crc32(hdr)))
        struct.pack_into("<I", j, 24, crc32(bytes(j[0:24])))
        self.r.escribir(FS_JOURNAL, bytes(j))
        self._barrera()
        self._aplica(s, hdr, datos)
        self._barrera()
        self.r.escribir(FS_JOURNAL, bytes(SEC))

    def _barrera(self):
        d = self.r
        while d is not None and not hasattr(d, "vacia"):
            d = getattr(d, "d", None)
        if d is not None:
            d.vacia()

    def _spill_put(self, s, nombre, datos):
        if not self.big:
            raise ErrorSoma("DATOS no tiene BIGFS: los archivos de 16 KiB o mas no se pueden guardar desde fuera (arranca SOMA una vez)")
        self.big.mkdir("/.ramfs")
        h = bytearray(64)
        h[0:4] = b"RFS1"
        nb = nombre.encode()[:FS_NAME_MAX - 1]
        h[4:4 + len(nb)] = nb
        struct.pack_into("<I", h, 52, len(datos))
        if self.big.busca(spill_tmp(s)) is not None:
            self.big.borrar(spill_tmp(s))
        i = self.big.crear(spill_tmp(s), bytes(h) + datos)
        if self.big.busca(spill_ruta(s)) is not None:
            self.big.borrar(spill_ruta(s))
        self.big.renombrar(spill_tmp(s), spill_ruta(s))
        return i

    def escribir(self, nombre, datos):
        if not self.escribe:
            raise ErrorSoma("abierto solo para leer")
        if not nombre.startswith("/"):
            nombre = "/" + nombre
        if len(nombre.encode()) >= FS_NAME_MAX:
            raise ErrorSoma(f"nombre demasiado largo para SOMA (como mucho {FS_NAME_MAX - 1} bytes): {nombre}")
        if efimero(nombre):
            raise ErrorSoma("/sys/ y /tmp/ no se guardan en disco")
        if len(datos) > FS_FILE_MAX:
            raise ErrorSoma("archivo demasiado grande para SOMA (32 MiB como mucho)")
        s = self._indice(nombre)
        if s is not None:
            nombre = self.ranura[s]["nombre"]
        else:
            libres = [k for k in range(FS_MAX_FILES) if k not in self.ranura]
            if not libres:
                raise ErrorSoma("SOMA ya tiene 256 archivos")
            s = libres[0]
        antes = self.ranura.get(s)
        if s < FS_SLOTS and len(datos) < FS_DATA_MAX:
            self._commit_ranura(s, self._cabecera(nombre, len(datos), False, datos), datos)
            self.ranura[s] = {"nombre": nombre, "tam": len(datos), "donde": "ranura", "crc_ok": True}
            if antes and antes["donde"] == "BIGFS" and self.big:      # PARCHE 113: lo de BIGFS se borra despues
                for r in (spill_ruta(s), spill_tmp(s)):
                    if self.big.busca(r) is not None:
                        self.big.borrar(r)
        else:
            i = self._spill_put(s, nombre, datos)
            if s < FS_SLOTS:
                self.r.escribir(FS_DATA + s * FS_SPF, self._cabecera(nombre, len(datos), True))
            self.ranura[s] = {"nombre": nombre, "tam": len(datos), "donde": "BIGFS", "big": i}
        self._super()

    def borrar(self, nombre):
        if not self.escribe:
            raise ErrorSoma("abierto solo para leer")
        s = self._indice(nombre)
        if s is None:
            raise ErrorSoma(f"no existe {nombre}")
        v = self.ranura.pop(s)
        if v["donde"] == "BIGFS" and self.big:
            for r in (spill_ruta(s), spill_tmp(s)):
                if self.big.busca(r) is not None:
                    self.big.borrar(r)
        if s < FS_SLOTS:
            self.r.escribir(FS_DATA + s * FS_SPF, bytes(SEC))
        self._super()
