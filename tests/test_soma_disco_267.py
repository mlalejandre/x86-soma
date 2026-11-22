#!/usr/bin/env python3
"""SOMA_267_DISCOS: pruebas de tools/soma_disco.py (el pendrive en cuatro particiones) con imagenes de pendrive dispersas.

  python3 tests/test_soma_disco_267.py [--rapido]

Escenarios (cada uno en un pendrive simulado):
  plan        el plan de particiones para muchos tamanos: alineado, dentro del disco, sin solapes, MODELOS 16 GiB en los grandes
  vacio       pendrive sin nada -> 4 particiones; fsck.fat / fsck.exfat limpios; carpetas de USUARIO y MODELOS
  actualizar  pendrive ya nuevo con datos en DATOS, MODELOS y USUARIO -> solo cambia SISTEMA (huella de las otras tres igual)
              y se conserva INFORME.TXT
  linux       pendrive antiguo de construyeimagen.py (SOMA + SOMADATA reclamada) -> SOMADATA se queda en su sitio como DATOS
              (identica, byte a byte) y los modelos de SOMA/MODELOS pasan a MODELOS/voz
  macos       pendrive antiguo de ejecutar.py en macOS (SOMA + SOMADATA + DATOS FAT32 con archivos del usuario de nombres
              largos y acentos) -> los archivos acaban en USUARIO, iguales
  img         SOMA con el fichero SOMADATA.IMG (sin particion) -> su contenido acaba en la particion DATOS con la marca nueva
  sinsitio    sin sitio en el equipo para la copia temporal -> error y el pendrive NO se toca
  ajeno       pendrive de otro sistema: sin confirmar no se toca; confirmando se prepara de cero
  imagen      la imagen del repositorio (5 GiB + 1 MiB): cabe en un pendrive de 8 GB y MODELOS/USUARIO llevan la serie de "agrandable"
Las comprobaciones con herramientas externas (fsck.fat, fsck.exfat, pyfatfs) se hacen si estan; si no, se dicen y se saltan.
"""
import hashlib, os, random, shutil, struct, subprocess, sys, tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ / "tools"))
import soma_disco as sd

RAPIDO = "--rapido" in sys.argv
ok = fallos = saltadas = 0
GB = 1000 ** 3


def ck(c, msg):
    global ok, fallos
    if c:
        ok += 1
    else:
        fallos += 1
        print("FALLO:", msg)


def salta(msg):
    global saltadas
    saltadas += 1
    print("(se salta)", msg)


def lee(ex, ruta):
    try:
        return ex.leer(ruta)
    except Exception:
        return None


def herr(nombre):
    for d in [os.environ.get("EXFATPROGS", ""), os.environ.get("DOSFSTOOLS", ""), "/sbin", "/usr/sbin", "/usr/local/sbin"] + os.environ.get("PATH", "").split(os.pathsep):
        p = os.path.join(d, nombre) if d else ""
        if p and os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return None


FSCK_FAT, FSCK_EXFAT = herr("fsck.fat") or herr("fsck.vfat"), herr("fsck.exfat")
try:
    from pyfatfs.PyFatFS import PyFatFS
except Exception:
    PyFatFS = None


class Pendrive:
    def __init__(self, tmp, nombre, bytes_):
        self.p = Path(tmp) / nombre
        with open(self.p, "wb") as f:
            f.truncate(bytes_ // 512 * 512)
        self.sect = bytes_ // 512

    def abre(self):
        self.f = open(self.p, "r+b")
        return sd.Disco(self.f, self.sect)

    def cierra(self):
        self.f.close()

    def huella(self, lba, n):
        h = hashlib.sha256()
        with open(self.p, "rb") as f:
            f.seek(lba * 512)
            quedan = n * 512
            while quedan:
                b = f.read(min(quedan, 8 << 20))
                if not b:
                    h.update(bytes(quedan))
                    break
                h.update(b)
                quedan -= len(b)
        return h.hexdigest()

    def extrae(self, part, destino):
        """la particion a un archivo propio (disperso) para fsck"""
        with open(self.p, "rb") as f, open(destino, "wb") as o:
            o.truncate(part.n * 512)
            f.seek(part.lba * 512)
            hecho = 0
            while hecho < part.n * 512:
                b = f.read(min(8 << 20, part.n * 512 - hecho))
                if not b:
                    break
                if b.count(0) != len(b):
                    o.seek(hecho)
                    o.write(b)
                hecho += len(b)
        return destino


def fsck_fat(img, part, tmp, que):
    if not FSCK_FAT:
        salta("no hay fsck.fat (dosfstools)")
        return
    x = img.extrae(part, Path(tmp) / "p.fat")
    r = subprocess.run([FSCK_FAT, "-n", str(x)], capture_output=True, text=True)
    ck(r.returncode == 0, f"{que}: fsck.fat limpio\n{(r.stdout + r.stderr)[-800:]}")
    os.unlink(x)


def fsck_exfat(img, part, tmp, que):
    if not FSCK_EXFAT:
        salta("no hay fsck.exfat (exfatprogs)")
        return
    x = img.extrae(part, Path(tmp) / "p.exfat")
    r = subprocess.run([FSCK_EXFAT, "-n", str(x)], capture_output=True, text=True)
    ck(r.returncode == 0 and "clean" in r.stdout, f"{que}: fsck.exfat limpio\n{(r.stdout + r.stderr)[-800:]}")
    os.unlink(x)


EFI1, EFI2 = random.Random(1).randbytes(3_100_000), random.Random(2).randbytes(3_200_000)
INFORME = b"informe vacio".ljust(256 * 1024, b"\n")
callado = lambda *a: None


def graba(img, efi=EFI1, **k):
    d = img.abre()
    try:
        return sd.grabar(d, efi, b"LEEME", INFORME, log=callado, **k)
    finally:
        img.cierra()


def info(img):
    d = img.abre()
    try:
        return sd.analizar(d)
    finally:
        img.cierra()


def exfat_de(img, part):
    d = img.abre()
    return d, sd.ExFat(d, part.lba, part.n)


def comprueba_nuevo(img, tmp, que):
    i = info(img)
    ck(i["tipo"] == "nuevo", f"{que}: el pendrive queda con 4 particiones ({i['tipo']}, {i['avisos']})")
    if i["tipo"] != "nuevo":
        return i
    ps = i["parts"]
    ck([p.tipo for p in ps] == [sd.TIPO_FAT32, sd.TIPO_DATOS, sd.TIPO_EXFAT, sd.TIPO_EXFAT] and ps[0].activa, f"{que}: tipos de particion")
    ck(all(p.lba % sd.MIB == 0 and p.n % sd.MIB == 0 for p in ps) and all(a.fin <= b.lba for a, b in zip(ps, ps[1:])) and ps[-1].fin <= img.sect,
       f"{que}: particiones alineadas a 1 MiB, sin solaparse y dentro del pendrive")
    d = img.abre()
    try:
        fs = sd.Fat32(d, ps[0].lba, ps[0].n)
        ck(fs.etiqueta == "SOMA" and lee(fs, "/EFI/BOOT/BOOTX64.EFI") in (EFI1, EFI2), f"{que}: SISTEMA arranca (BOOTX64.EFI)")
        m = sd.lee_marca(d, ps[1].lba)
        ck(m is not None and m[0] == ps[1].lba and m[1] == ps[1].n and m[2] == i["sig"], f"{que}: DATOS con su marca (inicio, tamano y firma del disco)")
        mo, us = sd.ExFat(d, ps[2].lba, ps[2].n), sd.ExFat(d, ps[3].lba, ps[3].n)
        ck(mo.etiqueta == "MODELOS" and all(mo.es_carpeta("/" + c) for c in sd.CARPETAS_MODELOS), f"{que}: MODELOS con voz/ y llm/")
        ck(us.etiqueta == "USUARIO" and all(us.es_carpeta("/" + c) for c in sd.CARPETAS_USUARIO), f"{que}: USUARIO con sus carpetas")
    finally:
        img.cierra()
    fsck_fat(img, ps[0], tmp, que + " SISTEMA")
    fsck_exfat(img, ps[2], tmp, que + " MODELOS")
    fsck_exfat(img, ps[3], tmp, que + " USUARIO")
    return i


def disco_antiguo(img, partes, sig=0x11223344):
    """MBR con particiones FAT32 (tipo 0x0C) como las de construyeimagen.py o diskutil"""
    d = img.abre()
    m = bytearray(512)
    struct.pack_into("<I", m, 440, sig)
    for i, (lba, n) in enumerate(partes):
        struct.pack_into("<B3sB3sII", m, 446 + 16 * i, 0x80 if i == 0 else 0, b"\xFE\xFF\xFF", 0x0C, b"\xFE\xFF\xFF", lba, n)
    m[510:512] = b"\x55\xAA"
    d.escribir(0, bytes(m))
    img.cierra()


def fat_en(img, lba, n, etiqueta, archivos):
    d = img.abre()
    sd.escribir_regiones(d, sd.fat32_regiones(lba, n, etiqueta, archivos, 0x77))
    img.cierra()


def somadata_reclamada(img, lba, n, sig, rnd):
    """lo que deja SOMA en SOMADATA: la marca, el VFS y algo de BIGFS (aqui, bytes al azar en varias zonas)"""
    d = img.abre()
    d.escribir(lba, sd.marca_datos(lba, n, sig))
    for off in (8, 1032, 3208, 4104, 8200, 16392, 20000, 900000):
        if off + 64 < n:
            d.escribir(lba + off, rnd.randbytes(64 * 512))
    img.cierra()


def arbol_usuario(rnd):
    a = {}
    for i in range(60 if RAPIDO else 200):
        carpeta = rnd.choice(["Fotos del verano", "Música/Clásica", "Trabajo/Informes 2025", "", "Ñandú y compañía", "日本"])
        nombre = rnd.choice(["foto", "canción", "informe final", "README", "datos"]) + f" {i}." + rnd.choice(["jpg", "mp3", "pdf", "txt", "BIN"])
        a[(carpeta + "/" if carpeta else "") + nombre] = rnd.randbytes(rnd.choice([0, 1, 4096, rnd.randint(1, 300000)]))
    return a


def main():
    tmp = tempfile.mkdtemp(prefix="sd267_")
    try:
        # ------------------------------------------------------------- plan
        malos = 0
        for gb in [4.3, 4.5, 6, 7.2, 7.45, 8, 14.9, 16, 20, 24, 28, 29.8, 32, 58, 64, 120, 128, 256, 500, 1000, 2000, 3000]:
            tot = int(gb * GB) // 512
            try:
                pl = sd.plan_particiones(tot)
            except sd.SinSitio:
                malos += gb >= 4.5
                continue
            ps = pl
            bien = (all(p.lba % sd.MIB == 0 and p.n % sd.MIB == 0 for p in ps) and all(a.fin == b.lba for a, b in zip(ps, ps[1:]))
                    and ps[-1].fin <= min(tot, 0xFFFFFFFF) and ps[2].n >= sd.PARTE_MIN and ps[3].n >= sd.PARTE_MIN and ps[2].n <= sd.MODELOS_MAX
                    and ps[0].n == sd.SISTEMA_SECT and ps[1].n == sd.DATOS_SECT)
            if gb >= 29:
                bien = bien and ps[2].n == sd.MODELOS_MAX
            if not bien:
                malos += 1
                print("  plan malo para", gb, ps)
        ck(malos == 0, "plan de particiones correcto para 4,5 GB .. 3 TB (MODELOS 16 GiB desde 32 GB)")
        try:
            sd.plan_particiones(int(4 * GB) // 512)
            ck(False, "un pendrive de 4 GB es demasiado pequeno")
        except sd.SinSitio:
            ck(True, "")
        p32 = sd.plan_particiones(int(32 * GB) // 512)
        ck(abs(p32[3].n * 512 / 2 ** 30 - 10.8) < 0.2, "32 GB: USUARIO ~10,8 GiB")

        # ------------------------------------------------------------- vacio
        img = Pendrive(tmp, "vacio.img", int(8 * GB))
        r = graba(img)
        ck(r["modo"] == "nuevo", f"vacio: modo nuevo ({r['modo']})")
        i0 = comprueba_nuevo(img, tmp, "vacio")

        # ------------------------------------------------------------- actualizar
        if i0["tipo"] == "nuevo":
            ps = i0["parts"]
            d, us = exfat_de(img, ps[3])
            us.escribir("/Documentos/mi carta.txt", b"Querida abuela...")
            us.cierra()
            img.cierra()
            d = img.abre()
            d.escribir(ps[1].lba + 1032, random.Random(5).randbytes(4096))        # algo del VFS dentro de DATOS
            fs = sd.Fat32(d, ps[0].lba, ps[0].n)
            img.cierra()
            # SOMA reescribe INFORME.TXT en su sitio: simularlo
            d = img.abre()
            fs = sd.Fat32(d, ps[0].lba, ps[0].n)
            ent = fs.busca("/INFORME.TXT")
            nuevo_inf = b"INFORME DE VERDAD".ljust(256 * 1024, b"\n")
            d.escribir(fs._clus_lba(ent[2]), nuevo_inf)
            img.cierra()
            antes = [img.huella(p.lba, p.n) for p in ps[1:]]
            r = graba(img, efi=EFI2)
            ck(r["modo"] == "actualizar", f"actualizar: modo actualizar ({r['modo']})")
            despues = [img.huella(p.lba, p.n) for p in ps[1:]]
            ck(antes == despues, "actualizar: DATOS, MODELOS y USUARIO identicos byte a byte")
            d = img.abre()
            fs = sd.Fat32(d, ps[0].lba, ps[0].n)
            ck(lee(fs, "/EFI/BOOT/BOOTX64.EFI") == EFI2, "actualizar: SISTEMA con el BOOTX64.EFI nuevo")
            ck(lee(fs, "/INFORME.TXT") == nuevo_inf, "actualizar: se conserva INFORME.TXT")
            img.cierra()
            fsck_fat(img, ps[0], tmp, "actualizar SISTEMA")
            # con modelos del equipo: se anaden a MODELOS/voz y lo demas sigue igual
            mdir = Path(tmp) / "modelos"
            mdir.mkdir()
            (mdir / "ggml-base-q5_1.bin").write_bytes(random.Random(9).randbytes(1_500_000))
            antes = [img.huella(p.lba, p.n) for p in (ps[1], ps[3])]
            graba(img, efi=EFI2, modelos_equipo=[mdir / "ggml-base-q5_1.bin"])
            d, mo = exfat_de(img, ps[2])
            ck(lee(mo, "/voz/ggml-base-q5_1.bin") == (mdir / "ggml-base-q5_1.bin").read_bytes(), "actualizar: modelo del equipo -> MODELOS/voz")
            img.cierra()
            ck([img.huella(p.lba, p.n) for p in (ps[1], ps[3])] == antes, "actualizar con modelos: DATOS y USUARIO intactos")
            fsck_exfat(img, ps[2], tmp, "actualizar MODELOS")
        os.unlink(img.p)

        # ------------------------------------------------------------- linux (2 particiones, SOMADATA reclamada)
        rnd = random.Random(267)
        img = Pendrive(tmp, "linux.img", int(8 * GB))
        p1, p2 = (2048, 1 << 21), (2048 + (1 << 21), 1 << 23)
        disco_antiguo(img, [p1, p2], sig=0xCAFEBABE)
        modelo = rnd.randbytes(2_300_000)
        voz_json = b'{"voz": "es_ES"}'
        fat_en(img, *p1, "SOMA", {"EFI/BOOT/BOOTX64.EFI": EFI1, "LEEME.TXT": b"x", "INFORME.TXT": b"INF VIEJO".ljust(256 * 1024, b"\n"),
                                  "MODELOS/ggml-base-q5_1.bin": modelo, "MODELOS/es_ES-voz.onnx.json": voz_json})
        somadata_reclamada(img, *p2, 0xCAFEBABE, rnd)
        h_datos = img.huella(*p2)
        i = info(img)
        ck(i["tipo"] == "antiguo" and i["datos"] and i["datos"].lba == p2[0] and len(i["modelos_fat"]) == 2, f"linux: se reconoce el pendrive antiguo ({sd.describe(i)})")
        r = graba(img, temporal=tmp)
        ck(r["modo"] == "migrar", f"linux: modo migrar ({r['modo']})")
        i = comprueba_nuevo(img, tmp, "linux")
        if i["tipo"] == "nuevo":
            ck(i["parts"][1].lba == p2[0] and i["parts"][1].n == p2[1], "linux: DATOS en el sitio y con el tamano de SOMADATA")
            ck(img.huella(*p2) == h_datos, "linux: el disco de SOMA (SOMADATA -> DATOS) identico byte a byte")
            ck(i["sig"] == 0xCAFEBABE, "linux: se conserva la firma del disco (la marca sigue valiendo)")
            d, mo = exfat_de(img, i["parts"][2])
            ck(lee(mo, "/voz/ggml-base-q5_1.bin") == modelo and lee(mo, "/voz/es_ES-voz.onnx.json") == voz_json, "linux: modelos de SOMA/MODELOS -> MODELOS/voz")
            fs = sd.Fat32(d, i["parts"][0].lba, i["parts"][0].n)
            ck((lee(fs, "/INFORME.TXT") or b"").startswith(b"INF VIEJO"), "linux: INFORME.TXT conservado")
            img.cierra()
        ck(not list(Path(tmp).glob("soma_migracion_*")), "linux: la copia temporal se borra al terminar bien")
        os.unlink(img.p)

        # ------------------------------------------------------------- macos (3 particiones, DATOS FAT32 del usuario)
        img = Pendrive(tmp, "mac.img", int(12 * GB))
        p3 = (p2[0] + p2[1], int(12 * GB) // 512 - (p2[0] + p2[1]))
        disco_antiguo(img, [p1, p2, p3], sig=0x0BADF00D)
        fat_en(img, *p1, "SOMA", {"EFI/BOOT/BOOTX64.EFI": EFI1, "LEEME.TXT": b"x", "INFORME.TXT": INFORME})
        somadata_reclamada(img, *p2, 0x0BADF00D, rnd)
        h_datos = img.huella(*p2)
        usuario = arbol_usuario(rnd)
        usuario["._basura de macOS"] = b"x" * 4096
        usuario[".fseventsd/x"] = b"x"
        fat_en(img, *p3, "DATOS", usuario)
        if PyFatFS:                                   # el lector FAT32 de soma_disco frente a otra implementacion
            x = img.extrae(sd.Part(2, 0x0C, *p3), Path(tmp) / "p3.fat")
            pf = PyFatFS(str(x), read_only=True)
            ck(all(pf.readbytes("/" + k) == v for k, v in usuario.items()), "macos: pyfatfs lee la DATOS FAT32 de la prueba (nombres largos)")
            pf.close()
            os.unlink(x)
        r = graba(img, temporal=tmp)
        i = comprueba_nuevo(img, tmp, "macos")
        if i["tipo"] == "nuevo":
            ck(img.huella(*p2) == h_datos, "macos: SOMADATA -> DATOS identica")
            d, us = exfat_de(img, i["parts"][3])
            malos = [k for k, v in usuario.items() if not k.startswith(("._", ".fseventsd")) and lee(us, "/" + k) != v]
            ck(not malos, f"macos: los {len(usuario) - 2} archivos de la DATOS antigua estan en USUARIO, iguales ({len(malos)} distintos: {malos[:3]})")
            ck(not us.existe("/._basura de macOS") and not us.existe("/.fseventsd"), "macos: sin la basura de macOS")
            ck(all(us.es_carpeta("/" + c) for c in sd.CARPETAS_USUARIO), "macos: y con las carpetas de USUARIO")
            img.cierra()
        os.unlink(img.p)

        # ------------------------------------------------------------- img (SOMADATA.IMG en la particion SOMA)
        img = Pendrive(tmp, "img.img", int(8 * GB))
        pbig = (2048, int(8 * GB) // 512 - 4096)
        disco_antiguo(img, [pbig], sig=0x5151)
        datos_img = bytearray(rnd.randbytes(64 << 20))
        fat_en(img, *pbig, "SOMA", {"EFI/BOOT/BOOTX64.EFI": EFI1, "INFORME.TXT": INFORME, "SOMADATA.IMG": bytes(datos_img)})
        d = img.abre()
        lba_img, n_img = sd.Fat32(d, *pbig).contiguo("/SOMADATA.IMG")
        d.escribir(lba_img, sd.marca_datos(lba_img, n_img, 0x5151))     # SOMA ya la reclamo
        img.cierra()
        r = graba(img, temporal=tmp)
        i = comprueba_nuevo(img, tmp, "img")
        if i["tipo"] == "nuevo":
            p = i["parts"][1]
            d = img.abre()
            ck(d.leer(p.lba + 8, n_img - 8) == bytes(datos_img[8 * 512:n_img * 512]), "img: el contenido de SOMADATA.IMG esta en DATOS")
            m = sd.lee_marca(d, p.lba)
            ck(m == (p.lba, p.n, i["sig"]), "img: marca nueva con el sitio de DATOS")
            img.cierra()
        os.unlink(img.p)

        # ------------------------------------------------------------- sinsitio
        img = Pendrive(tmp, "sinsitio.img", int(12 * GB))
        disco_antiguo(img, [p1, p2, p3])
        fat_en(img, *p1, "SOMA", {"EFI/BOOT/BOOTX64.EFI": EFI1})
        somadata_reclamada(img, *p2, 0x11223344, rnd)
        fat_en(img, *p3, "DATOS", {"grande.bin": rnd.randbytes(3 << 20)})
        h = img.huella(0, img.sect) if not RAPIDO else img.huella(0, 4 << 21)
        real = shutil.disk_usage
        shutil.disk_usage = lambda p: real(p)._replace(free=1 << 20)
        try:
            graba(img, temporal=tmp)
            ck(False, "sinsitio: tenia que fallar")
        except sd.SinSitio as e:
            ck("No se ha tocado" in str(e), f"sinsitio: error claro ({e})")
        finally:
            shutil.disk_usage = real
        ck((img.huella(0, img.sect) if not RAPIDO else img.huella(0, 4 << 21)) == h, "sinsitio: el pendrive no se ha tocado")
        os.unlink(img.p)

        # ------------------------------------------------------------- ajeno
        img = Pendrive(tmp, "ajeno.img", int(6 * GB))
        d = img.abre()
        m = bytearray(512)
        struct.pack_into("<B3sB3sII", m, 446, 0, b"\0\0\0", 0x07, b"\0\0\0", 2048, 1 << 22)        # una NTFS cualquiera
        m[510:512] = b"\x55\xAA"
        d.escribir(0, bytes(m))
        d.escribir(2048, b"\xEB\x52\x90NTFS    ".ljust(512, b"\0"))
        img.cierra()
        h = img.huella(0, 1 << 22)
        try:
            graba(img, confirmar=lambda msg: False)
            ck(False, "ajeno: sin confirmar tenia que fallar")
        except sd.ErrorDisco:
            ck(img.huella(0, 1 << 22) == h, "ajeno: sin confirmar no se toca")
        r = graba(img, confirmar=lambda msg: True)
        comprueba_nuevo(img, tmp, "ajeno")
        os.unlink(img.p)

        # ------------------------------------------------------------- imagen del repositorio
        img = Pendrive(tmp, "imagen.img", sd.IMAGEN_SECT * 512)
        graba(img, imagen=True)
        i = comprueba_nuevo(img, tmp, "imagen")
        ck(sd.IMAGEN_SECT * 512 < 7.4 * GB, "imagen: cabe en un pendrive de 8 GB")
        if i["tipo"] == "nuevo":
            d = img.abre()
            ck((sd.ExFat(d, i["parts"][2].lba, i["parts"][2].n).serie, sd.ExFat(d, i["parts"][3].lba, i["parts"][3].n).serie) == sd.SERIE_IMAGEN,
               "imagen: MODELOS y USUARIO marcadas como agrandables (serie de la imagen)")
            img.cierra()
        ocupado = os.stat(img.p).st_blocks * 512
        ck(ocupado < 64 << 20, f"imagen: casi vacia en disco ({ocupado >> 20} MiB ocupados): se comprime en poco")
        os.unlink(img.p)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc()
        fallos += 1
        print("FALLO: excepcion en las pruebas (arriba)")
    print("%s pendrive en 4 particiones: %d bien, %d fallos, %d saltadas" % ("FALLO" if fallos else "ok ", ok, fallos, saltadas))
    sys.exit(1 if fallos else 0)
