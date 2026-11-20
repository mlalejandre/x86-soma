#!/usr/bin/env python3
"""construyeimagen.py - SOMA_227_IMAGEN: imagen de pendrive de SOMA para PC x86_64 (UEFI), lista para grabar.

Uso (desde la raiz del proyecto):
    python3 construyeimagen.py                 menu
    python3 construyeimagen.py --grabar        1) genera la imagen y la graba en un pendrive
    python3 construyeimagen.py --carpeta       2) genera la carpeta imagen/ (imagen comprimida + instrucciones) para el repositorio
Opciones:
    --sin-compilar    usa el build/usb/EFI/BOOT/BOOTX64.EFI que ya existe
    --local           compila con las herramientas del equipo (make + grub-mkstandalone) en vez de Docker
    --disco DISCO     (con --grabar) pendrive de destino sin preguntar: /dev/sdb en Linux, disk4 en macOS

La imagen es un disco MBR con dos particiones FAT32, igual que la que prepara ejecutar.py -t t480s-usb:
    1  SOMA      1 GiB  arranque: EFI/BOOT/BOOTX64.EFI (GRUB + nucleo + firmware WiFi en un solo archivo)
    2  SOMADATA  4 GiB  el disco de SOMA (lo reclama y lo formatea en el primer arranque; SOMA_228: si el pendrive es mayor, la
                        estira hasta el final, como mucho 8 GiB)
La particion SOMA lleva ademas INFORME.TXT (256 KiB): SOMA lo rellena en cada arranque con el informe de hardware, para que
cualquiera pueda abrirlo desde Windows, macOS o Linux y compartirlo si algo no funciona.
El formateo FAT32 lo hace este mismo script (Python puro, sin mtools ni permisos de administrador); solo grabar en un
pendrive necesita permisos. Para compilar hace falta Docker (o --local en Linux con la cadena de herramientas).
"""
import argparse, hashlib, json, lzma, os, platform, re, shutil, struct, subprocess, sys, time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent
EFI = ROOT / "build" / "usb" / "EFI" / "BOOT" / "BOOTX64.EFI"
DOCKER_IMAGE = "soma-toolchain"
OUT_DIR = ROOT / "imagen"
SECTOR = 512
P1_LBA, P1_SECT = 2048, 1 << 21            # SOMA: 1 GiB
P2_LBA, P2_SECT = 2048 + (1 << 21), 1 << 23  # SOMADATA: 4 GiB
TOTAL_SECT = P2_LBA + P2_SECT
REPO = "https://github.com/mlalejandre/x86-soma"
INFORME_BYTES = 256 * 1024                 # SOMA_228_ARRANQUE: hueco para el informe (SOMA lo reescribe en su sitio, sin cambiar el tamano)


# ------------------------------------------------------------------ utilidades
def titulo(t):
    print(f"\n{'=' * 72}\n{t}\n{'=' * 72}")


def preguntar(msg, defecto=""):
    try:
        return input(msg).strip()
    except (EOFError, KeyboardInterrupt):
        print()
        return defecto


def version():
    """'0.3-prebeta' a partir del nucleo (la cadena 'SOMA x.y [pre-beta] ::')."""
    try:
        t = (ROOT / "src" / "core" / "kernel.c").read_text(errors="replace")
        m = re.search(r"SOMA (\d+\.\d+)( pre-beta)? ::", t)
        if m:
            return m.group(1) + ("-prebeta" if m.group(2) else "")
    except OSError:
        pass
    return "dev"


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


# ------------------------------------------------------------------ compilar
def compilar(local):
    titulo("1/3  COMPILANDO SOMA (build/usb/EFI/BOOT/BOOTX64.EFI)")
    cflags = os.environ.get("SOMA_EXTRA_CFLAGS", "-DSOMA_WEB_ASYNC_IMAGES=1")
    make = ["make", "ARCH=x86_64", "PLATFORM=qemu_virt", f"EXTRA_CFLAGS={cflags}", "t480s-usb"]
    if local:
        r = subprocess.run(make, cwd=ROOT)
    else:
        if not shutil.which("docker"):
            sys.exit("No encuentro Docker. Instala Docker Desktop (Windows/Mac) o docker.io (Linux), o usa --local.")
        tiene = subprocess.run(["docker", "run", "--rm", "--platform", "linux/amd64", DOCKER_IMAGE, "test", "-d",
                                "/usr/lib/grub/x86_64-efi"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if not tiene:
            print("Preparando el contenedor de compilacion (solo la primera vez, unos minutos)...")
            if subprocess.run(["docker", "build", "--platform", "linux/amd64", "-t", DOCKER_IMAGE, "."], cwd=ROOT).returncode:
                sys.exit("ERROR: no se pudo construir el contenedor de Docker.")
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64"]
        if hasattr(os, "getuid"):
            cmd += ["-u", f"{os.getuid()}:{os.getgid()}"]
        cmd += ["-v", f"{ROOT}:/soma", "-w", "/soma", DOCKER_IMAGE] + make
        r = subprocess.run(cmd, cwd=ROOT)
    if r.returncode or not EFI.is_file():
        sys.exit("ERROR: la compilacion fallo (mira los mensajes de arriba).")
    print(f"[OK] {EFI.relative_to(ROOT)} ({EFI.stat().st_size / 1e6:.1f} MB)")


# ------------------------------------------------------------------ FAT32 en Python
def _fecha():
    n = datetime.now()
    return ((n.hour << 11) | (n.minute << 5) | (n.second // 2)), (((n.year - 1980) << 9) | (n.month << 5) | n.day)


def _ent(name83, attr, clus, size):
    t, d = _fecha()
    return struct.pack("<11sBBBHHHHHHHI", name83, attr, 0, 0, t, d, d, clus >> 16, t, d, clus & 0xFFFF, size)


def _n83(nombre):
    base, _, ext = nombre.upper().partition(".")
    return (base[:8].ljust(8) + ext[:3].ljust(3)).encode("ascii")


def fat32(part_lba, nsec, etiqueta, archivos, volid):
    """Regiones (desplazamiento, bytes) de un FAT32 completo en la particion. archivos: {'EFI/BOOT/BOOTX64.EFI': bytes}."""
    spc, rsvd, nf = 8, 32, 2
    t1, t2 = nsec - rsvd, (256 * spc + nf) // 2
    fatsz = (t1 + t2 - 1) // t2
    fds = rsvd + nf * fatsz
    nclus = (nsec - fds) // spc
    cbytes = spc * SECTOR
    base = part_lba * SECTOR
    fat = {0: 0x0FFFFFF8, 1: 0x0FFFFFFF}
    sig = [3]
    regiones = []

    def alloc(n):
        c0 = sig[0]; sig[0] += n
        for k in range(n):
            fat[c0 + k] = (c0 + k + 1) if k < n - 1 else 0x0FFFFFFF
        return c0

    def coff(c):
        return base + (fds + (c - 2) * spc) * SECTOR

    # arbol de directorios
    dirs = {"": []}
    for ruta in sorted(archivos):
        partes = ruta.split("/")
        for i in range(1, len(partes)):
            d = "/".join(partes[:i])
            if d not in dirs:
                dirs[d] = []
                dirs["/".join(partes[:i - 1])].append(("d", partes[i - 1], d))
        dirs["/".join(partes[:-1])].append(("f", partes[-1], ruta))
    clus = {"": 2}
    fat[2] = 0x0FFFFFFF
    for d in sorted(dirs):
        if d:
            clus[d] = alloc(1)
    for d, items in dirs.items():
        raw = bytearray()
        if d == "":
            raw += _ent(etiqueta.upper()[:11].ljust(11).encode("ascii"), 0x08, 0, 0)
        else:
            padre = "/".join(d.split("/")[:-1])
            raw += _ent(b".          ", 0x10, clus[d], 0) + _ent(b"..         ", 0x10, clus[padre] if padre else 0, 0)
        for tipo, nombre, ruta in items:
            if tipo == "d":
                raw += _ent(_n83(nombre), 0x10, clus[ruta], 0)
            else:
                datos = archivos[ruta]
                n = max(1, (len(datos) + cbytes - 1) // cbytes)
                c = alloc(n) if datos else 0
                if datos:
                    regiones.append((coff(c), datos))
                raw += _ent(_n83(nombre), 0x20, c, len(datos))
        assert len(raw) <= cbytes
        regiones.append((coff(clus[d]), bytes(raw).ljust(cbytes, b"\0")))
    usados = sig[0] - 2
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x58\x90"; bs[3:11] = b"SOMA    "
    struct.pack_into("<HBHBHHBHHHII", bs, 11, SECTOR, spc, rsvd, nf, 0, 0, 0xF8, 0, 63, 255, part_lba, nsec)
    struct.pack_into("<IHHIHH", bs, 36, fatsz, 0, 0, 2, 1, 6)
    struct.pack_into("<BBBI11s8s", bs, 64, 0x80, 0, 0x29, volid, etiqueta.upper()[:11].ljust(11).encode("ascii"), b"FAT32   ")
    bs[510:512] = b"\x55\xAA"
    fsi = bytearray(SECTOR)
    struct.pack_into("<I", fsi, 0, 0x41615252); struct.pack_into("<II", fsi, 484, 0x61417272, nclus - usados)
    struct.pack_into("<I", fsi, 492, sig[0]); struct.pack_into("<I", fsi, 508, 0xAA550000)
    reservados = bytearray(rsvd * SECTOR)
    reservados[0:SECTOR] = bs; reservados[SECTOR:2 * SECTOR] = fsi
    reservados[6 * SECTOR:7 * SECTOR] = bs; reservados[7 * SECTOR:8 * SECTOR] = fsi
    regiones.append((base, bytes(reservados)))
    tabla = bytearray(fatsz * SECTOR)
    for c, v in fat.items():
        struct.pack_into("<I", tabla, c * 4, v)
    for k in range(nf):
        regiones.append((base + (rsvd + k * fatsz) * SECTOR, bytes(tabla)))
    return regiones


def mbr():
    m = bytearray(SECTOR)
    struct.pack_into("<I", m, 440, int(time.time()) & 0xFFFFFFFF)
    for i, (act, lba, n) in enumerate(((0x80, P1_LBA, P1_SECT), (0x00, P2_LBA, P2_SECT))):
        struct.pack_into("<B3sB3sII", m, 446 + 16 * i, act, b"\xFE\xFF\xFF", 0x0C, b"\xFE\xFF\xFF", lba, n)
    m[510:512] = b"\x55\xAA"
    return [(0, bytes(m))]


def informe_vacio():
    """SOMA_228_ARRANQUE: INFORME.TXT de la particion de arranque, aun sin informe. SOMA lo sobrescribe en cada arranque."""
    t = ("SOMA - informe de arranque y hardware\r\n\r\n"
         "Este archivo lo rellena SOMA cada vez que arranca desde este pendrive (y con la orden 'informe' o\r\n"
         "Ajustes > Sistema > Guardar informe). Si todavia ves este texto, SOMA no ha llegado a arrancar en este equipo:\r\n"
         "haz una foto de la pantalla donde se detuvo y compartela en " + REPO + "/issues\r\n").encode()
    return t + b"\n" * (INFORME_BYTES - len(t))


def regiones_imagen():
    leeme = ("SOMA " + version() + "\r\nParticion de arranque. El disco de SOMA es la particion SOMADATA.\r\n" + REPO + "\r\n").encode()
    r = mbr()
    r += fat32(P1_LBA, P1_SECT, "SOMA", {"EFI/BOOT/BOOTX64.EFI": EFI.read_bytes(), "LEEME.TXT": leeme,
                                          "INFORME.TXT": informe_vacio()}, 0x50AA0001)
    r += fat32(P2_LBA, P2_SECT, "SOMADATA", {}, 0x50AA0002)
    return sorted(r)


def escribir_imagen(ruta, regiones):
    with open(ruta, "wb") as f:
        f.truncate(TOTAL_SECT * SECTOR)              # archivo disperso: los huecos son ceros sin ocupar disco
        for off, datos in regiones:
            f.seek(off); f.write(datos)


def comprimir(img, xz):
    """xz por bloques: los tramos de ceros (casi todo) cuestan muy poco."""
    tam = img.stat().st_size; hecho = 0; t0 = time.time(); ult = -1
    with open(img, "rb") as f, lzma.open(xz, "wb", preset=6) as z:
        for b in iter(lambda: f.read(8 << 20), b""):
            z.write(b); hecho += len(b)
            pc = hecho * 100 // tam
            if pc != ult:
                ult = pc; print(f"\r  comprimiendo {pc:3d} %", end="", flush=True)
    print(f"\r  comprimido en {time.time() - t0:.0f} s: {xz.stat().st_size / 1e6:.1f} MB")


# ------------------------------------------------------------------ opcion 2: carpeta para el repositorio
LEEME_CARPETA = """# SOMA {ver} — imagen lista para el pendrive (opción rápida)

Esta carpeta trae SOMA **ya compilado** como imagen de disco. Solo tienes que grabarla en un pendrive y arrancar el PC desde él.
Si prefieres compilarlo tú mismo, sigue las instrucciones del [README principal](../README.md#-novedad-de-la-v03-arrancar-soma-desde-un-pendrive-en-un-pc-x86_64).

| Archivo | Qué es |
|---|---|
| `{img}` | La imagen comprimida ({mb:.1f} MB; ocupa 5 GiB al grabarla) |
| `{img}.sha256` | Su huella SHA-256, para comprobar que se descargó bien |

Generada el {fecha} a partir del código de este repositorio con `python3 construyeimagen.py --carpeta`.

**Qué necesitas:** un pendrive de **8 GB o más** (se borra entero) y un PC x86_64 con **UEFI**. Probado en un Lenovo ThinkPad T480s.

> ⚠️ Grabar la imagen **borra el pendrive entero**. Comprueba dos veces que eliges el pendrive y no el disco del ordenador.

## La forma más sencilla (Windows, macOS y Linux): balenaEtcher

1. Descarga **balenaEtcher** de https://etcher.balena.io e instálalo.
2. *Flash from file* → elige `{img}` (no hace falta descomprimirlo).
3. *Select target* → tu pendrive. Etcher solo muestra discos externos.
4. *Flash!* y espera a que termine la verificación.

## Windows: Rufus

1. Descarga **Rufus** de https://rufus.ie.
2. *Dispositivo*: tu pendrive. *Elección de arranque*: **SELECCIONAR** → `{img}`.
3. **EMPEZAR**. Si pregunta el modo, elige **Escribir en modo Imagen DD**.
4. Si después Windows ofrece «formatear» alguna unidad del pendrive, pulsa **Cancelar**.

## macOS: Terminal

```
diskutil list external              # localiza tu pendrive, por ejemplo disk4
diskutil unmountDisk /dev/disk4
xzcat {img} | sudo dd of=/dev/rdisk4 bs=4m
diskutil eject /dev/disk4
```

Si macOS dice que no puede leer un disco, es la partición de SOMA: pulsa **Ignorar**, nunca «Inicializar».

## Linux: Terminal

```
lsblk -o NAME,SIZE,TRAN,MODEL       # localiza tu pendrive (TRAN = usb), por ejemplo sdb
sudo umount /dev/sdb?*
xzcat {img} | sudo dd of=/dev/sdb bs=4M status=progress conv=fsync
```

## Comprobar la descarga (opcional)

- Windows (PowerShell): `Get-FileHash {img} -Algorithm SHA256`
- macOS: `shasum -a 256 {img}`
- Linux: `sha256sum -c {img}.sha256`

El resultado tiene que coincidir con el de `{img}.sha256`.

## Arrancar

1. En la BIOS/UEFI del PC **desactiva Secure Boot** (SOMA no está firmado). En el ThinkPad T480s: `F1` al encender → *Security → Secure Boot → Disabled*.
2. Enchufa el pendrive, abre el menú de arranque (`F12` en Lenovo; `F11`, `F8` o `Esc` en otras marcas) y elige el USB.
3. En el menú de GRUB elige **SOMA** (o *resolución segura 1024x768* si la pantalla se queda en negro).
4. El primer arranque reclama la partición `SOMADATA` y, si el pendrive es de más de 8 GB, la amplía hasta ocuparlo (como mucho 8 GiB): desde ahí lo que guardes en SOMA se conserva.

¿Tu teclado no es español? Elige **SOMA - otro PC con teclado inglés (US)** en el menú de GRUB.

¿No arranca? Prueba otro puerto USB y comprueba que el PC está en modo **UEFI** (no «Legacy/CSM»).

## ¿Algo no funciona en tu equipo? Comparte el informe

SOMA está probado a fondo en un ThinkPad T480s; en otros equipos puede faltar algún driver. Para que se pueda arreglar:

- **Si arranca:** SOMA guarda solo, en cada arranque, un informe en el archivo **`INFORME.TXT`** de la unidad *SOMA* del
  pendrive. Enchufa el pendrive en tu ordenador de siempre, abre ese archivo y compártelo (en los *issues* del repositorio o
  en los comentarios del vídeo). Dice qué equipo tienes, qué dispositivos tienen driver en SOMA y cuáles no, y en qué paso del
  arranque hubo problemas. No contiene contraseñas ni claves. También puedes generarlo en *Ajustes → Sistema → Guardar informe*.
- **Si se queda colgado:** la última línea `[ARRANQUE n/13] ...` de la pantalla dice en qué paso. Haz una foto. Si sale una
  pantalla de error, dice qué opción de GRUB salta ese paso (pulsa `e` en el menú, añádela al final de la línea `multiboot2` y
  pulsa `F10`). En el siguiente arranque que llegue al escritorio, el informe recordará dónde se quedó el anterior.

## Licencias

SOMA es software libre con licencia MIT (archivo `LICENSE` del repositorio), de Marcos Luis Álvarez Alejandre.
La imagen incluye el firmware WiFi de Intel (`iwlwifi-8265-36.ucode`, licencia de redistribución de Intel, ver
`firmware/LICENCE.iwlwifi_firmware`), GRUB (GPLv3, código en https://www.gnu.org/software/grub/) y las fuentes DejaVu
y Liberation Sans (licencias libres). Más detalles en SOMA → Ajustes → Acerca de.
"""


def opcion_carpeta():
    ver = version()
    titulo("2/3  CREANDO LA IMAGEN")
    OUT_DIR.mkdir(exist_ok=True)
    nombre = f"soma-{ver}-x86_64-usb.img"
    img, xz = OUT_DIR / nombre, OUT_DIR / (nombre + ".xz")
    for viejo in OUT_DIR.glob("soma-*-usb.img*"):
        viejo.unlink()
    escribir_imagen(img, regiones_imagen())
    titulo("3/3  COMPRIMIENDO Y ESCRIBIENDO LAS INSTRUCCIONES")
    comprimir(img, xz)
    img.unlink()
    (OUT_DIR / (xz.name + ".sha256")).write_text(f"{sha256(xz)}  {xz.name}\n")
    (OUT_DIR / "LEEME.md").write_text(LEEME_CARPETA.format(ver=ver, img=xz.name, mb=xz.stat().st_size / 1e6,
                                                          fecha=datetime.now().strftime("%d/%m/%Y %H:%M")), encoding="utf-8")
    print(f"\n[OK] Carpeta {OUT_DIR.relative_to(ROOT)}/ lista:")
    for p in sorted(OUT_DIR.iterdir()):
        print(f"   {p.name:44s} {p.stat().st_size / 1e6:8.2f} MB")
    print("\nPuedes subirla al repositorio tal cual (git add imagen). Las instrucciones estan en imagen/LEEME.md.")


# ------------------------------------------------------------------ opcion 1: grabar en un pendrive
def discos_linux():
    out = subprocess.run(["lsblk", "-J", "-b", "-o", "NAME,SIZE,TRAN,RM,TYPE,MODEL,MOUNTPOINT"], capture_output=True, text=True)
    if out.returncode:
        return []
    res = []
    for d in json.loads(out.stdout).get("blockdevices", []):
        if d.get("type") == "disk" and (d.get("tran") == "usb" or str(d.get("rm")) in ("1", "True", "true")) and int(d.get("size") or 0) > 0:
            res.append({"dev": "/dev/" + d["name"], "size": int(d["size"]), "model": (d.get("model") or "USB").strip(),
                        "parts": ["/dev/" + c["name"] for c in d.get("children", []) or []]})
    return res


def grabar_regiones(dev, regiones):
    """Graba solo lo que tiene datos (MBR, sectores de arranque, FAT, directorios y archivos): segundos en vez de minutos.
    Los huecos del pendrive quedan con lo que tuvieran, que es correcto: las FAT se escriben enteras (todo libre) y SOMA
    formatea SOMADATA en el primer arranque."""
    fd = os.open(dev, os.O_WRONLY | getattr(os, "O_SYNC", 0))
    try:
        total = sum(len(d) for _, d in regiones); hecho = 0; ult = -1
        for off, datos in regiones:
            os.lseek(fd, off, os.SEEK_SET)
            for i in range(0, len(datos), 1 << 20):
                hecho += os.write(fd, datos[i:i + (1 << 20)])
                pc = hecho * 100 // total
                if pc != ult:
                    ult = pc; print(f"\r  grabando {pc:3d} %", end="", flush=True)
        os.fsync(fd)
    finally:
        os.close(fd)
    print("\r  grabado y sincronizado.        ")


def opcion_grabar(args):
    sistema = platform.system()
    if sistema == "Darwin":
        print("\nEn macOS el pendrive se prepara con ejecutar.py (deja tambien una particion DATOS para ti y conserva los")
        print("archivos de SOMA si el pendrive ya estaba preparado).")
        cmd = [sys.executable, str(ROOT / "ejecutar.py"), "-t", "t480s-usb", "--sin-compilar"]
        if args.disco:
            cmd += ["--disco", args.disco]
        sys.exit(subprocess.run(cmd, cwd=ROOT).returncode)
    if sistema == "Windows":
        print("\nEn Windows grabar un disco entero desde un script exige bloquear y desmontar sus volumenes, y es facil")
        print("equivocarse de disco. Te dejo la imagen lista para grabarla con balenaEtcher o Rufus (instrucciones en LEEME.md).")
        opcion_carpeta()
        try:
            os.startfile(str(OUT_DIR))
        except Exception:
            pass
        return
    # Linux
    discos = discos_linux()
    if not discos:
        sys.exit("No veo ningun pendrive USB conectado. Conectalo y vuelve a ejecutar.")
    if args.disco:
        sel = [d for d in discos if d["dev"] == args.disco]
        if not sel:
            sys.exit(f"{args.disco} no es un pendrive USB de la lista: " + ", ".join(d["dev"] for d in discos))
        disco = sel[0]
    else:
        print("\nPendrives conectados:")
        for i, d in enumerate(discos, 1):
            print(f"  {i}) {d['dev']:10s} {d['size'] / 1e9:6.1f} GB  {d['model']}")
        k = preguntar("Numero del pendrive (Intro para cancelar): ")
        if not k.isdigit() or not 1 <= int(k) <= len(discos):
            sys.exit("Cancelado.")
        disco = discos[int(k) - 1]
    if disco["size"] < TOTAL_SECT * SECTOR:
        sys.exit(f"El pendrive es demasiado pequeno: hacen falta {TOTAL_SECT * SECTOR / 1e9:.1f} GB.")
    print(f"\nSe BORRARA ENTERO {disco['dev']} ({disco['size'] / 1e9:.1f} GB, {disco['model']}).")
    if preguntar("Escribe BORRAR para continuar: ") != "BORRAR":
        sys.exit("Cancelado.")
    if os.geteuid() != 0:
        print("Hacen falta permisos de administrador: se pedira tu contrasena (sudo).")
        cmd = ["sudo", sys.executable, str(Path(__file__).resolve()), "--_grabar-dispositivo", disco["dev"]]
        sys.exit(subprocess.run(cmd, cwd=ROOT).returncode)
    _grabar_dispositivo(disco["dev"])


def _grabar_dispositivo(dev):
    titulo(f"2/3  CREANDO LA IMAGEN EN MEMORIA")
    regiones = regiones_imagen()
    # sin restos de una tabla GPT anterior: el primer MiB (salvo el MBR) y el ultimo MiB del pendrive a cero
    try:
        with open(f"/sys/block/{Path(dev).name}/size") as f:
            fin = int(f.read()) * SECTOR
    except OSError:
        fin = 0
    regiones = [(SECTOR, bytes(P1_LBA * SECTOR - SECTOR))] + regiones
    if fin > TOTAL_SECT * SECTOR + (1 << 20):
        regiones.append((fin - (1 << 20), bytes(1 << 20)))
    titulo(f"3/3  GRABANDO EN {dev}")
    for p in sorted(Path("/dev").glob(Path(dev).name + "*")):
        if str(p) != dev:
            subprocess.run(["umount", str(p)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    grabar_regiones(dev, sorted(regiones))
    subprocess.run(["blockdev", "--rereadpt", dev], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"\n[OK] Pendrive {dev} listo. Desactiva Secure Boot, enchufalo al PC y arranca desde el USB (F12 en Lenovo).")


# ------------------------------------------------------------------ principal
def main():
    ap = argparse.ArgumentParser(description="Imagen de pendrive de SOMA para PC x86_64")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--grabar", action="store_true", help="1) generar la imagen y grabarla en un pendrive")
    g.add_argument("--carpeta", action="store_true", help="2) generar la carpeta imagen/ para el repositorio")
    ap.add_argument("--sin-compilar", action="store_true", help="usar el BOOTX64.EFI ya compilado")
    ap.add_argument("--local", action="store_true", help="compilar sin Docker (make y grub-mkstandalone del equipo)")
    ap.add_argument("--disco", help="pendrive de destino sin preguntar (/dev/sdb en Linux, disk4 en macOS)")
    ap.add_argument("--_grabar-dispositivo", dest="dispositivo", help=argparse.SUPPRESS)
    a = ap.parse_args()
    if a.dispositivo:                                   # segunda llamada, ya con sudo
        if not EFI.is_file():
            sys.exit("Falta " + str(EFI))
        _grabar_dispositivo(a.dispositivo)
        return
    print(f"SOMA {version()} - imagen de pendrive para PC x86_64 (UEFI)")
    if not (a.grabar or a.carpeta):
        print("\n  1) Generar la imagen y grabarla en un pendrive conectado")
        print("  2) Generar la carpeta imagen/ (imagen comprimida + instrucciones) para subirla al repositorio")
        op = preguntar("\nElige 1 o 2: ")
        if op not in ("1", "2"):
            sys.exit("Cancelado.")
        a.grabar, a.carpeta = op == "1", op == "2"
    if a.sin_compilar:
        if not EFI.is_file():
            sys.exit(f"No existe {EFI.relative_to(ROOT)}: quita --sin-compilar.")
        print(f"Usando {EFI.relative_to(ROOT)} ya compilado.")
    else:
        compilar(a.local)
    if a.grabar:
        opcion_grabar(a)
    else:
        opcion_carpeta()


if __name__ == "__main__":
    main()
