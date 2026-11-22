#!/usr/bin/env python3
"""construyeimagen.py - SOMA_227_IMAGEN / SOMA_267_DISCOS: el pendrive de SOMA para PC x86_64 (UEFI).

Uso (desde la raiz del proyecto):
    python3 construyeimagen.py                 menu
    python3 construyeimagen.py --grabar        1) compila y graba un pendrive conectado (Linux y macOS)
    python3 construyeimagen.py --carpeta       2) genera la carpeta imagen/ (imagen comprimida + instrucciones) para el repositorio
Opciones:
    --sin-compilar    usa el build/usb/EFI/BOOT/BOOTX64.EFI que ya existe
    --local           compila con las herramientas del equipo (make + grub-mkstandalone) en vez de Docker
    --disco DISCO     (con --grabar) pendrive de destino sin preguntar: /dev/sdb en Linux, disk4 en macOS
    --formatear       (con --grabar) borra el pendrive entero aunque ya sea de SOMA (pide escribir BORRAR)
    --temporal DIR    (con --grabar) carpeta del equipo para la copia temporal al migrar un pendrive antiguo
    --si              no pregunta (para scripts): acepta migrar y, con --formatear, borrar

SOMA_267_DISCOS: el pendrive tiene CUATRO particiones (tools/soma_disco.py):
    1  SOMA (sistema)  FAT32  1 GiB   arranque: EFI/BOOT/BOOTX64.EFI e INFORME.TXT. Es lo unico que se rehace al grabar.
    2  DATOS           SOMA   2 GiB   configuracion, memoria y programas instalados (formato propio; desde fuera: tools/datos.py)
    3  MODELOS         exFAT 16 GiB   modelos de IA (voz/, llm/); menos en pendrives pequenos
    4  USUARIO         exFAT  resto   tus archivos: Documentos, Imagenes, Audio, Videos, Descargas, Proyectos
Al grabar se mira primero lo que tiene el pendrive:
    - ya es de SOMA (4 particiones): SOLO se actualiza el sistema; DATOS, MODELOS y USUARIO quedan igual;
    - es de SOMA de antes (SOMA + SOMADATA, con o sin DATOS FAT32, o el fichero SOMADATA.IMG): se conserva todo y se pasa a las
      cuatro particiones (SOMADATA queda como DATOS sin moverse; lo que cambia de sitio se copia antes al equipo y, si no cabe,
      no se toca nada);
    - cualquier otro: se prepara de cero (pide escribir BORRAR).
Los modelos de la carpeta modelos/ del proyecto (ggml-*.bin de whisper.cpp, voces .onnx de Piper) se copian a MODELOS/voz.
La imagen del repositorio (--carpeta) mide 5 GiB + 1 MiB (cabe en un pendrive de 8 GB): en un pendrive mayor, SOMA agranda
MODELOS y USUARIO en el primer arranque, mientras aun estan vacias.
Todo es Python puro (sin mtools ni permisos de administrador); solo grabar el pendrive necesita sudo.
"""
import argparse, hashlib, json, lzma, os, platform, plistlib, re, shutil, struct, subprocess, sys, time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools"))
import soma_disco as sd                     # SOMA_267_DISCOS
EFI = ROOT / "build" / "usb" / "EFI" / "BOOT" / "BOOTX64.EFI"
DOCKER_IMAGE = "soma-toolchain"
OUT_DIR = ROOT / "imagen"
SECTOR = 512
TOTAL_SECT = sd.IMAGEN_SECT                # SOMA_267_DISCOS: imagen del repositorio (5 GiB + 1 MiB)
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
        # SOMA_242_TOOLCHAIN: GRUB para UEFI y g++ (voz a texto, parche 241); si falta algo se reconstruye el contenedor
        tiene = subprocess.run(["docker", "run", "--rm", "--platform", "linux/amd64", DOCKER_IMAGE, "sh", "-c",
                                "test -d /usr/lib/grub/x86_64-efi && command -v g++ && command -v python3"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
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


# ------------------------------------------------------------------ contenido de la particion de arranque
def informe_vacio():
    """SOMA_228_ARRANQUE: INFORME.TXT de la particion de arranque, aun sin informe. SOMA lo sobrescribe en cada arranque."""
    t = ("SOMA - informe de arranque y hardware\r\n\r\n"
         "Este archivo lo rellena SOMA cada vez que arranca desde este pendrive (y con la orden 'informe' o\r\n"
         "Ajustes > Sistema > Guardar informe). Si todavia ves este texto, SOMA no ha llegado a arrancar en este equipo:\r\n"
         "haz una foto de la pantalla donde se detuvo y compartela en " + REPO + "/issues\r\n").encode()
    return t + b"\n" * (INFORME_BYTES - len(t))


# SOMA_267_DISCOS: el FAT32 vive ahora en tools/soma_disco.py; se conserva el nombre de antes (lo usa tests/mk_informe_228.py)
fat32 = sd.fat32_regiones
P1_LBA, P1_SECT = sd.P1_LBA, sd.SISTEMA_SECT          # la particion del sistema (nombres de antes)


def leeme_sistema():
    return ("SOMA " + version() + "\r\nParticion de arranque (sistema). Tus archivos estan en la unidad USUARIO y los modelos de IA en\r\n"
            "MODELOS. La configuracion y la memoria de SOMA estan en la particion DATOS (se lee con tools/datos.py).\r\n" + REPO + "\r\n").encode()


def modelos_proyecto():
    """modelos de voz de la carpeta modelos/ del proyecto (whisper.cpp y Piper) -> MODELOS/voz del pendrive"""
    d = ROOT / "modelos"
    if not d.is_dir():
        return []
    return sorted(p for p in d.iterdir() if p.is_file() and not p.name.startswith(".") and
                  (p.name.startswith("ggml-") and p.suffix == ".bin" or p.name.endswith(".onnx") or p.name.endswith(".onnx.json")))


def escribir_imagen(ruta):
    """SOMA_267_DISCOS: la imagen del repositorio, con las cuatro particiones (archivo disperso: los huecos no ocupan)"""
    with open(ruta, "wb") as f:
        f.truncate(TOTAL_SECT * SECTOR)
    with open(ruta, "r+b") as f:
        dev = sd.Disco(f, TOTAL_SECT)
        sd.grabar(dev, EFI.read_bytes(), leeme_sistema(), informe_vacio(), log=lambda m: print(" ", m.strip()), imagen=True)


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

El pendrive queda con cuatro particiones: **SOMA** (el sistema, 1 GiB), **DATOS** (configuración y memoria de SOMA, 2 GiB; Windows y
macOS no la abren, es normal), **MODELOS** (modelos de IA) y **USUARIO** (tus archivos: Documentos, Imágenes, Audio...). MODELOS y
USUARIO se ven desde cualquier ordenador (exFAT). En un pendrive de más de 8 GB, SOMA las agranda solo en el primer arranque.
| `{img}.sha256` | Su huella SHA-256, para comprobar que se descargó bien |

Generada el {fecha} a partir del código de este repositorio con `python3 construyeimagen.py --carpeta`.

**Qué necesitas:** un pendrive de **8 GB o más** (se borra entero; con 32 GB o más, MODELOS tiene sus 16 GiB) y un PC x86_64 con **UEFI**. Probado en un Lenovo ThinkPad T480s.

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

Si macOS dice que no puede leer un disco, es la partición DATOS de SOMA: pulsa **Ignorar**, nunca «Inicializar».

Para **actualizar** SOMA más adelante sin perder nada, usa `python3 construyeimagen.py --grabar` desde el repositorio: solo
reescribe la partición del sistema. (Grabar esta imagen otra vez con Etcher o `dd` borra el pendrive entero.)

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
4. En el primer arranque SOMA prepara la partición `DATOS` y, si el pendrive es de más de 8 GB, agranda `MODELOS` (hasta 16 GiB) y `USUARIO` (el resto) mientras están vacías: desde ahí lo que guardes en SOMA se conserva.

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
    escribir_imagen(img)
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
            res.append({"dev": "/dev/" + d["name"], "size": int(d["size"]), "model": (d.get("model") or "USB").strip()})
    return res


def _plist(cmd):
    out = subprocess.run(cmd, capture_output=True)
    try:
        return plistlib.loads(out.stdout) if out.returncode == 0 else {}
    except Exception:
        return {}


def discos_macos():
    """discos externos y fisicos; nunca uno interno (doble comprobacion con diskutil info)"""
    res = []
    for d in _plist(["diskutil", "list", "-plist", "external", "physical"]).get("AllDisksAndPartitions", []):
        dev = d.get("DeviceIdentifier", "")
        if not re.fullmatch(r"disk\d+", dev) or dev in ("disk0", "disk1"):
            continue
        info = _plist(["diskutil", "info", "-plist", dev])
        if not info or info.get("Internal", True):
            continue
        res.append({"dev": dev, "size": int(info.get("TotalSize") or info.get("Size") or d.get("Size") or 0),
                    "model": info.get("MediaName") or info.get("IORegistryEntryName") or "USB"})
    return res


def opcion_grabar(args):
    sistema = platform.system()
    if sistema == "Windows":
        print("\nEn Windows grabar un disco entero desde un script exige bloquear y desmontar sus volumenes, y es facil")
        print("equivocarse de disco. Te dejo la imagen lista para grabarla con balenaEtcher o Rufus (instrucciones en LEEME.md).")
        opcion_carpeta()
        try:
            os.startfile(str(OUT_DIR))
        except Exception:
            pass
        return
    discos = discos_macos() if sistema == "Darwin" else discos_linux()
    if not discos:
        sys.exit("No veo ningun pendrive USB conectado. Conectalo y vuelve a ejecutar.")
    if args.disco:
        quiero = args.disco.replace("/dev/", "").replace("rdisk", "disk") if sistema == "Darwin" else args.disco
        sel = [d for d in discos if d["dev"] == quiero]
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
    if disco["size"] < (sd.P1_LBA + sd.SISTEMA_SECT + sd.DATOS_SECT + 2 * sd.PARTE_MIN) * SECTOR:
        sys.exit(f"El pendrive es demasiado pequeno: hacen falta {(sd.P1_LBA + sd.SISTEMA_SECT + sd.DATOS_SECT + 2 * sd.PARTE_MIN) * SECTOR / 1e9:.1f} GB.")
    cmd = [sys.executable, str(Path(__file__).resolve()), "--_grabar-dispositivo", disco["dev"], "--_tamano", str(disco["size"])]
    for op, val in (("--formatear", args.formatear), ("--si", args.si)):
        if val:
            cmd.append(op)
    if args.temporal:
        cmd += ["--temporal", args.temporal]
    if os.geteuid() != 0:
        print("Hacen falta permisos de administrador para escribir en el pendrive: se pedira tu contrasena (sudo).")
        cmd = ["sudo"] + cmd
    sys.exit(subprocess.run(cmd, cwd=ROOT).returncode)


def _confirmar_borrar(si):
    def f(msg):
        print("\n" + msg)
        return si or preguntar("Escribe BORRAR para continuar: ") == "BORRAR"
    return f


def _grabar_dispositivo(a):
    """segunda llamada, ya con sudo: mira el pendrive y lo deja con las cuatro particiones"""
    dev, tam = a.dispositivo, int(a.tamano or 0)
    mac = platform.system() == "Darwin"
    if mac:
        subprocess.run(["diskutil", "unmountDisk", "/dev/" + dev], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        ruta = "/dev/r" + dev
    else:
        ruta = dev
        for p in sorted(Path("/dev").glob(Path(dev).name + "*")):
            if str(p) != dev:
                subprocess.run(["umount", str(p)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            tam = int(Path(f"/sys/block/{Path(dev).name}/size").read_text()) * SECTOR
        except OSError:
            pass
    titulo(f"2/3  MIRANDO EL PENDRIVE {dev}")
    try:
        f = open(ruta, "r+b", buffering=0)
    except OSError as e:
        sys.exit(f"No se pudo abrir {ruta}: {e}")
    try:
        if not tam:
            tam = f.seek(0, os.SEEK_END)
        disco = sd.Disco(f, tam // SECTOR)
        info = sd.analizar(disco)
        print("  " + sd.describe(info))
        if info["tipo"] == "nuevo" and not a.formatear:
            print("  Se actualiza SOLO el sistema: DATOS, MODELOS y USUARIO no se tocan.")
        elif info["tipo"] == "antiguo" and not a.formatear:
            print("  Pendrive de una version anterior de SOMA: se conserva todo y se pasa a las cuatro particiones.")
            if not a.si and preguntar("Pulsa Intro para continuar (o escribe NO para cancelar): ").upper() == "NO":
                sys.exit("Cancelado: no se ha tocado el pendrive.")
        titulo(f"3/3  GRABANDO EN {dev}")
        t0 = time.time()
        res = sd.grabar(disco, EFI.read_bytes(), leeme_sistema(), informe_vacio(), modelos_equipo=modelos_proyecto(), formatear=a.formatear,
                        temporal=a.temporal, log=lambda m: print(m), confirmar=_confirmar_borrar(a.si))
        disco.vacia()
    except sd.ErrorDisco as e:
        sys.exit(f"\nERROR: {e}")
    finally:
        f.close()
    if mac:
        subprocess.run(["diskutil", "eject", "/dev/" + dev], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        subprocess.run(["blockdev", "--rereadpt", dev], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    hecho = {"actualizar": "sistema actualizado (lo demas, intacto)", "migrar": "pasado a cuatro particiones conservando tus datos",
             "nuevo": "preparado de cero"}[res["modo"]]
    for m in res.get("migrado", []):
        print(f"  migrado: {m}")
    print(f"\n[OK] Pendrive {dev} {hecho} en {time.time() - t0:.0f} s." + (" Ya puedes desconectarlo." if mac else ""))
    print("Desactiva Secure Boot, enchufalo al PC y arranca desde el USB (F12 en Lenovo).")


# ------------------------------------------------------------------ principal
def main():
    ap = argparse.ArgumentParser(description="Imagen de pendrive de SOMA para PC x86_64")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--grabar", action="store_true", help="1) generar la imagen y grabarla en un pendrive")
    g.add_argument("--carpeta", action="store_true", help="2) generar la carpeta imagen/ para el repositorio")
    ap.add_argument("--sin-compilar", action="store_true", help="usar el BOOTX64.EFI ya compilado")
    ap.add_argument("--local", action="store_true", help="compilar sin Docker (make y grub-mkstandalone del equipo)")
    ap.add_argument("--disco", help="pendrive de destino sin preguntar (/dev/sdb en Linux, disk4 en macOS)")
    ap.add_argument("--formatear", action="store_true", help="borrar el pendrive entero aunque ya sea de SOMA")
    ap.add_argument("--temporal", help="carpeta para la copia temporal al migrar un pendrive antiguo")
    ap.add_argument("--si", action="store_true", help="no preguntar")
    ap.add_argument("--_grabar-dispositivo", dest="dispositivo", help=argparse.SUPPRESS)
    ap.add_argument("--_tamano", dest="tamano", help=argparse.SUPPRESS)
    a = ap.parse_args()
    if a.dispositivo:                                   # segunda llamada, ya con sudo
        if not EFI.is_file():
            sys.exit("Falta " + str(EFI))
        _grabar_dispositivo(a)
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
