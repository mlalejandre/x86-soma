#!/usr/bin/env python3
# SOMA DEVELOPMENT RUNNER - THE SINGULARITY LOOP (v2 Multi-Arch)
#
# Protocolo del buzon (LBA 1..16, 8 KiB):
#   [0..7]  "PATCHv02"
#   [8..11] longitud u32 little-endian
#   [12..]  texto: uno o mas bloques
#             FILE: src/archivo.c
#             <<<<<<< SEARCH
#             texto exacto (debe aparecer UNA vez)
#             =======
#             texto nuevo
#             >>>>>>> REPLACE
# Resultado para la IA: LBA 20..23 (2 KiB, texto ASCII). Leer con 'sector_read 20'.

from pathlib import Path
import argparse
import difflib
import os
import re
import shutil
import subprocess
import time
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / "tools"))
import llm_server
import search_proxy

ROOT = Path(__file__).resolve().parent
BUILD_DIR = ROOT / "build"
SRC_DIR = ROOT / "src"
BACKUP_DIR = ROOT / "build_backup_src"
HDD = ROOT / "hdd.img"
DISK_MB = int(os.environ.get("SOMA_DISK_MB", "64"))   # SOMA_PARCHE_018_BIGFS: tamano minimo de hdd.img (el almacen de archivos grandes ocupa desde LBA 16384)
DOCKER_IMAGE = "soma-toolchain"

SECTOR = 512
MAILBOX_LBA = 1
MAILBOX_SIZE = 8192
MAILBOX_HDR = 12
MAGIC_V2 = b"PATCHv02"
MAGIC_V1 = b"PATCHv01"
RESULT_LBA = 20
RESULT_SIZE = 2048
BOOT_GATE_LBA = 24
QEMU_EXIT_GATE_OK = (0x20 << 1) | 1    # 0x41 = 65
QEMU_EXIT_GATE_FAIL = (0x22 << 1) | 1  # 0x45 = 69
QEMU_EXIT_PANIC = (0x23 << 1) | 1      # 0x47 = 71
QEMU_EXIT_PATCH = (0x10 << 1) | 1      # qemu_exit(0x10) -> codigo 0x21

ALLOWED_EXT = {".c", ".h", ".s"}
FILE_RE = re.compile(
    r"^FILE:[ \t]*(\S+)[ \t]*\n(.*?)(?=^FILE:[ \t]*\S+[ \t]*\n|\Z)",
    re.S | re.M,
)
BLOCK_RE = re.compile(
    r"<<<<<<< SEARCH\n(.*?)\n=======\n(.*?)\n?>>>>>>> REPLACE",
    re.S,
)


# ---------------------------------------------------------------- utilidades

def run(command: list[str], title: str) -> int:
    print(f"\n{'=' * 72}\n{title}\n{'=' * 72}\n\n$ {' '.join(command)}\n")
    return subprocess.run(command, cwd=ROOT).returncode


def get_host_uid_gid() -> tuple[str, str]:
    uid = subprocess.check_output(["id", "-u"], text=True).strip()
    gid = subprocess.check_output(["id", "-g"], text=True).strip()
    return uid, gid


def docker_image_exists() -> bool:
    res = subprocess.run(["docker", "image", "inspect", DOCKER_IMAGE],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return res.returncode == 0


def git(*args: str) -> None:
    if not (ROOT / ".git").exists() or not shutil.which("git"):
        return
    subprocess.run(["git", *args], cwd=ROOT,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def git_snapshot(message: str) -> None:
    git("add", "src")
    git("commit", "-m", message)


def build(uid: str, gid: str, arch: str = "x86_64") -> tuple[int, str]:
    cmd = ["docker", "run", "--rm", "--platform", "linux/amd64",
           "-u", f"{uid}:{gid}", "-v", f"{ROOT}:/soma", DOCKER_IMAGE, "make", f"ARCH={arch}"]
    print(f"\n{'=' * 72}\nCOMPILANDO SOMA BARE-METAL ({arch})\n{'=' * 72}\n\n$ {' '.join(cmd)}\n")
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    out = (p.stdout or "") + (p.stderr or "")
    print(out)
    return p.returncode, out


def summarize_errors(out: str, limit: int = 6) -> list[str]:
    hits = [l.strip() for l in out.splitlines()
            if re.search(r"error|undefined reference|\*\*\*", l, re.I)]
    return hits[:limit] or out.strip().splitlines()[-limit:]


# ---------------------------------------------------------------- buzon

def read_mailbox() -> tuple[str | None, str]:
    if not HDD.exists():
        return None, ""
    with open(HDD, "rb") as f:
        f.seek(MAILBOX_LBA * SECTOR)
        raw = f.read(MAILBOX_SIZE)
    magic = raw[:8]
    if magic == MAGIC_V1:
        return "v1", ""
    if magic != MAGIC_V2:
        return None, ""
    n = int.from_bytes(raw[8:12], "little")
    if n == 0 or n > MAILBOX_SIZE - MAILBOX_HDR:
        return "bad", ""
    return "ok", raw[MAILBOX_HDR:MAILBOX_HDR + n].decode("utf-8", errors="replace")


def clear_mailbox() -> None:
    with open(HDD, "rb+") as f:
        f.seek(MAILBOX_LBA * SECTOR)
        f.write(b"\x00" * MAILBOX_SIZE)


def write_result(status: str, detail: str = "") -> None:
    text = f"[PARCHE-RESULTADO] {status}"
    if detail:
        text += " | " + " | ".join(str(detail).splitlines())
    data = text.encode("ascii", "replace")[:RESULT_SIZE - 1]
    data += b"\x00" * (RESULT_SIZE - len(data))
    with open(HDD, "rb+") as f:
        f.seek(RESULT_LBA * SECTOR)
        f.write(data)
    print(f"[RESULTADO -> LBA {RESULT_LBA}] {text[:300]}")


# ---------------------------------------------------------------- parches

def arm_boot_gate() -> None:
    with open(HDD, "rb+") as f:
        f.seek(BOOT_GATE_LBA * SECTOR)
        data = b"GATE_TEST_REQ\x00" + b"\x00" * (SECTOR - 14)
        f.write(data)


def disarm_boot_gate() -> None:
    with open(HDD, "rb+") as f:
        f.seek(BOOT_GATE_LBA * SECTOR)
        f.write(b"\x00" * SECTOR)

RESUME_FLAG_LBA = 25

def arm_agent_resume() -> None:
    with open(HDD, "rb+") as f:
        f.seek(RESUME_FLAG_LBA * SECTOR)
        f.write(b"RESUME_REQ\x00" + b"\x00" * (SECTOR - 11))


PROTECTED_FILES = {
    'boot.s', 'idt.c', 'idt.h', 'io.h', 'srcfs.c', 'srcfs.h',
    'virtio_blk.c', 'virtio_blk.h', 'boot_gate.c', 'boot_gate.h'
}


def is_target_protected(path: Path) -> bool:
    if path.name in PROTECTED_FILES:
        return True
    prot_txt = ROOT / 'protected.txt'
    if prot_txt.exists():
        for line in prot_txt.read_text(encoding='utf-8').splitlines():
            line = line.strip()
            if line and not line.startswith('#') and Path(line).name == path.name:
                return True
    return False


def safe_target(name: str) -> Path | None:
    if "\x00" in name:
        return None
    p = (ROOT / name).resolve()
    src = SRC_DIR.resolve()
    if not (p.is_relative_to(src) and p.is_file()):
        base = Path(name).name
        candidates = [f for f in src.rglob(base) if f.is_file() and f.suffix.lower() in ALLOWED_EXT]
        if len(candidates) == 1:
            p = candidates[0]
    if p.is_relative_to(src) and p.suffix.lower() in ALLOWED_EXT and p.is_file():
        if is_target_protected(p):
            print(f"[SEGURIDAD HOST] Modificacion denegada: '{p.name}' es un archivo protegido.")
            return None
        return p
    return None


def compute_patch(text: str):
    text = text.replace("\r\n", "\n")
    sections = FILE_RE.findall(text)
    if not sections:
        return None, "No hay secciones 'FILE: src/...'"

    contents: dict[Path, list[str]] = {}
    for name, body in sections:
        target = safe_target(name)
        if target is None:
            return None, f"FILE invalido '{name}' (solo src/*.c|h|s existentes)"
        if target not in contents:
            old = target.read_text(encoding="utf-8")
            contents[target] = [old, old]
        blocks = BLOCK_RE.findall(body)
        if not blocks:
            return None, f"Sin bloques SEARCH/REPLACE para {name}"
        for search, repl in blocks:
            cur = contents[target][1]
            n = cur.count(search) if search else 0
            if n != 1:
                return None, (f"SEARCH aparece {n} veces (debe ser 1) en {name}: "
                              f"{search[:60]!r}")
            contents[target][1] = cur.replace(search, repl, 1)

    if len(sections) > 2:
        return None, f"Radio de mutacion excedido: maximo 2 archivos por parche (propuestos: {len(sections)})"

    for p, (o, n) in contents.items():
        rel_name = p.relative_to(ROOT)
        body = next((b for name, b in sections if safe_target(name) == p), "")
        blocks = BLOCK_RE.findall(body)
        if len(blocks) > 3:
            return None, f"Radio de mutacion excedido en {rel_name}: maximo 3 bloques por archivo (propuestos: {len(blocks)})"
        for search, repl in blocks:
            if not search.strip():
                return None, f"Bloque SEARCH vacio no permitido en {rel_name}"
            s_lines = len(search.splitlines())
            r_lines = len(repl.splitlines())
            if s_lines > 120:
                return None, f"Bloque SEARCH demasiado grande en {rel_name} ({s_lines} lineas > max 120)"
            if r_lines > 150:
                return None, f"Bloque REPLACE demasiado grande en {rel_name} ({r_lines} lineas > max 150)"

    changes = {p: (o, n) for p, (o, n) in contents.items() if o != n}
    if not changes:
        return None, "El parche no cambia nada"
    return changes, None


def show_diff(changes) -> None:
    for p, (old, new) in changes.items():
        rel = p.relative_to(ROOT)
        diff = difflib.unified_diff(old.splitlines(), new.splitlines(),
                                    f"a/{rel}", f"b/{rel}", lineterm="")
        print("\n".join(diff))


def apply_changes(changes) -> None:
    shutil.rmtree(BACKUP_DIR, ignore_errors=True)
    shutil.copytree(SRC_DIR, BACKUP_DIR)
    for p, (_, new) in changes.items():
        p.unlink(missing_ok=True)
        p.write_text(new, encoding="utf-8")
    time.sleep(0.6)


def restore_src() -> None:
    shutil.copytree(BACKUP_DIR, SRC_DIR, dirs_exist_ok=True)
    time.sleep(0.4)


def handle_mailbox(approve: bool) -> bool | None:
    state, text = read_mailbox()
    if state is None:
        return None
    clear_mailbox()

    print("\n" + "=" * 72)
    print("!!! PUENTE HOST-BRIDGE: LA IA PROPONE MODIFICAR EL CODIGO !!!")
    print("=" * 72)

    if state == "v1":
        write_result("REJECTED", "formato PATCHv01 obsoleto; usa FILE/SEARCH/REPLACE")
        return False
    if state == "bad":
        write_result("REJECTED", "longitud de parche invalida")
        return False

    changes, err = compute_patch(text)
    if err:
        print(f"[RECHAZADO] {err}")
        write_result("REJECTED", err)
        return False

    show_diff(changes)

    if approve:
        # SOMA_191: sin terminal nadie puede confirmar -> se rechaza (para el modo desatendido existe --sin-aprobar)
        if not sys.stdin.isatty():
            print("[RECHAZADO] no hay terminal para confirmar el parche (usa --sin-aprobar para el modo desatendido)")
            write_result("REJECTED", "sin terminal para la confirmacion humana")
            return False
        try:
            ans = input("\nAplicar este parche? [y/N] ").strip().lower()
        except EOFError:
            ans = ""
        if ans != "y":
            write_result("REJECTED", "rechazado por el humano")
            return False

    git_snapshot("SOMA: snapshot antes de parche de la IA")
    auto_snap = ROOT / "snapshots" / f"auto_pre_patch_{int(time.time())}"
    auto_snap.mkdir(parents=True, exist_ok=True)
    shutil.copytree(SRC_DIR, auto_snap / "src", dirs_exist_ok=True)
    if HDD.exists(): shutil.copy2(HDD, auto_snap / "hdd.img")
    print(f"[REPRODUCIBILIDAD] Snapshot automatico guardado en snapshots/{auto_snap.name}/")
    apply_changes(changes)
    names = ", ".join(str(p.relative_to(ROOT)) for p in changes)
    print(f"\n[OK] Parche aplicado a: {names}")
    return True


# ---------------------------------------------------------------- SRCFS

SRCFS_LBA = 8192
SRCFS_INDEX_SECTORS = 64   # SOMA_PARCHE_011_SRCFS: debe coincidir con src/fs/srcfs.c y disk_layout.h
SRCFS_ENTRY = 64
SRCFS_LIMIT_LBA = 16384
SRCFS_DATA_SECTORS = SRCFS_LIMIT_LBA - (SRCFS_LBA + SRCFS_INDEX_SECTORS)
# Tablas de datos (no son codigo): si las fuentes no caben, se omiten primero, las mas grandes antes.
SRCFS_LOW_PRIORITY = ("fonts/", "icons/", "cursors/", "arch/x86_64/sbx_testimg.h")   # PARCHE 138: imagenes de prueba (datos)


# PARCHE 047: el indice SRCFS solo admite 63 entradas; no empaquetar la otra arquitectura.
PACK_ARCH = None


def _otra_arch(p: Path) -> bool:
    if PACK_ARCH is None:
        return False
    parts = p.relative_to(SRC_DIR).parts
    return len(parts) >= 2 and parts[0] == "arch" and parts[1] != PACK_ARCH


def pack_sources() -> None:
    """PARCHE 140: SRCFS v02. Cada archivo se guarda comprimido con zlib si asi ocupa menos (el codigo C ocupa ~4 veces menos).
    Entrada de indice (64 bytes): name[48], u32 lba, u32 tamano original, u32 tamano guardado, u32 flags (bit 0 = zlib).
    El kernel (src/fs/srcfs.c) descomprime en el servicio aislado zdec (x86_64) o directamente (aarch64). Lee tambien v01."""
    import zlib
    files = sorted(p for p in SRC_DIR.rglob("*")
                   if p.is_file() and p.suffix.lower() in ALLOWED_EXT and not _otra_arch(p))
    max_entries = SRCFS_INDEX_SECTORS * SECTOR // SRCFS_ENTRY - 1
    if len(files) > max_entries:
        print(f"[SRCFS] AVISO: {len(files)} archivos; solo caben {max_entries}")
        files = files[:max_entries]

    stored = {}
    for p in files:
        data = p.read_bytes()
        z = zlib.compress(data, 9)
        stored[p] = (data, z, 1) if len(z) < len(data) else (data, data, 0)

    def _sectors(f: Path) -> int:
        return (len(stored[f][1]) + SECTOR - 1) // SECTOR

    need = sum(_sectors(f) for f in files)
    if need > SRCFS_DATA_SECTORS:
        bajos = sorted((f for f in files if f.relative_to(SRC_DIR).as_posix().startswith(SRCFS_LOW_PRIORITY)),
                       key=lambda f: -len(stored[f][1]))
        for f in bajos:
            if need <= SRCFS_DATA_SECTORS:
                break
            files.remove(f)
            need -= _sectors(f)
            print(f"[SRCFS] sin espacio para todo: se omite {f.relative_to(SRC_DIR).as_posix()}")
    while need > SRCFS_DATA_SECTORS and files:      # ultimo recurso: nunca escribir fuera de la region
        f = files.pop()
        need -= _sectors(f)
        print(f"[SRCFS] sin espacio: se omite {f.relative_to(SRC_DIR).as_posix()}")

    index = bytearray(SRCFS_INDEX_SECTORS * SECTOR)
    index[0:8] = b"SRCFSv02"
    index[8:12] = len(files).to_bytes(4, "little")

    lba = SRCFS_LBA + SRCFS_INDEX_SECTORS
    blobs = []
    total_orig = total_stored = 0
    for i, p in enumerate(files, 1):
        data, blob, flags = stored[p]
        rel_path = str(p.relative_to(SRC_DIR)).replace("\\", "/")
        name = rel_path.encode("utf-8")[:47]
        off = i * SRCFS_ENTRY
        index[off:off + len(name)] = name
        index[off + 48:off + 52] = lba.to_bytes(4, "little")
        index[off + 52:off + 56] = len(data).to_bytes(4, "little")
        index[off + 56:off + 60] = len(blob).to_bytes(4, "little")
        index[off + 60:off + 64] = flags.to_bytes(4, "little")
        blobs.append((lba, blob))
        lba += (len(blob) + SECTOR - 1) // SECTOR
        total_orig += len(data); total_stored += len(blob)

    if lba > SRCFS_LIMIT_LBA:
        print(f"[SRCFS] AVISO: las fuentes ocupan hasta LBA {lba} (limite {SRCFS_LIMIT_LBA}); truncando empaquetado seguro.")
        # Ajustar para escribir solo hasta el límite seguro sin colisionar con RamFS
        lba = SRCFS_LIMIT_LBA

    with open(HDD, "rb+") as f:
        f.seek(SRCFS_LBA * SECTOR)
        f.write(index)
        for blba, data in blobs:
            f.seek(blba * SECTOR)
            f.write(data + b"\x00" * (-len(data) % SECTOR))
    print(f"[SRCFS] {len(files)} fuentes empaquetadas en LBA {SRCFS_LBA}..{lba - 1} "
          f"({total_orig // 1024} KiB -> {total_stored // 1024} KiB comprimidas, {(SRCFS_LIMIT_LBA - lba) * SECTOR // 1024} KiB libres)")


# ---------------------------------------------------------------- main

def choose_arch() -> str:
    print("\nPlataforma de destino:")
    print("  1) x86_64   [Intel / AMD - PC estándar con GRUB / ISO]")
    print("  2) aarch64  [ARM64 - Cortex-A72 / QEMU virt ELF]")
    while True:
        try:
            choice = input("\nElige plataforma [1-2, Enter=1]: ").strip()
        except (EOFError, KeyboardInterrupt):
            return "x86_64"
        if choice in ("", "1"):
            return "x86_64"
        if choice == "2":
            return "aarch64"
        print("Opción no válida. Introduce 1 o 2.")


TARGETS = {
    "x86_64":  ("x86_64",  "PC x86_64 (QEMU, GRUB/ISO)"),
    "virt":    ("aarch64", "ARM64 en QEMU virt"),
    "pi5":     (None,      "Raspberry Pi 5 emulada (raspi5b, PCIe1 con xHCI)"),
    "pi5-rp1": (None,      "Raspberry Pi 5 emulada con RP1 (PCIe2: USB + Ethernet + GPIO)"),
    "pi5-img": (None,      "Raspberry Pi 5: solo compilar kernel_2712.img (para una Pi real)"),
    "pi5-qemu": (None,     "Raspberry Pi 5: construir/actualizar el emulador (imagen Docker)"),
    "pi5-sd":  (None,      "Raspberry Pi 5 REAL: genera soma_pi5_sd.img para grabar en una microSD"),
    "t480s-usb": (None,    "Lenovo ThinkPad T480s REAL: compila y copia SOMA a un pendrive USB (UEFI)"),
}


def choose_target() -> str:
    """PARCHE 072: menu de destinos (x86_64, aarch64 virt, variantes de Raspberry Pi 5)."""
    keys = list(TARGETS)
    print("\nDestino de compilacion/ejecucion:")
    for i, k in enumerate(keys, 1):
        print(f"  {i}) {k:<10} [{TARGETS[k][1]}]")
    while True:
        try:
            c = input(f"\nElige destino [1-{len(keys)}, Enter=1]: ").strip()
        except (EOFError, KeyboardInterrupt):
            return "x86_64"
        if c == "":
            return "x86_64"
        if c.isdigit() and 1 <= int(c) <= len(keys):
            return keys[int(c) - 1]
        if c in TARGETS:
            return c
        print("Opcion no valida.")


def run_pi5(target: str) -> int:
    """PARCHE 072: los destinos Raspberry Pi 5 se delegan en tools/pi5/pi5.py (mismas variables SOMA_PI5_*)."""
    script = str(ROOT / "tools" / "pi5" / "pi5.py")
    env = dict(os.environ)
    if target == "pi5-rp1":
        env["SOMA_PI5_RP1"] = "1"
    sub = {"pi5": "run", "pi5-rp1": "run", "pi5-img": "build", "pi5-qemu": "qemu", "pi5-sd": "sd"}[target]
    if not shutil.which("docker"):
        print("ERROR: falta Docker.")
        return 1
    if sub != "qemu" and not docker_image_exists():
        if run(["docker", "build", "--platform", "linux/amd64", "-t", DOCKER_IMAGE, "."], "CONSTRUYENDO TOOLCHAIN") != 0:
            return 1
    return subprocess.call([sys.executable, script, sub], cwd=str(ROOT), env=env)


# ---------------------------------------------------------------- SOMA_155_T480S: pendrive para el ThinkPad T480s
# Compila build/usb/EFI/BOOT/BOOTX64.EFI (GRUB + menu + nucleo en un solo archivo; make t480s-usb) y lo copia a un pendrive
# FAT32 (lo que mejor arranca en el firmware de Lenovo). Solo ofrece discos EXTERNOS: el disco interno nunca aparece en la lista.
# Si el pendrive ya tiene un volumen FAT32 "SOMA" solo se copia el archivo; si no, se formatea (pidiendo confirmacion).
import hashlib
import plistlib

T480S_EFI = BUILD_DIR / "usb" / "EFI" / "BOOT" / "BOOTX64.EFI"
T480S_LABEL = "SOMA"
T480S_DATA = "SOMADATA"     # SOMA_162_T480S: particion donde SOMA guarda sus archivos (el nucleo la reclama al primer arranque)


def _ask(msg: str, default: str = "") -> str:
    try:
        return input(msg).strip()
    except (EOFError, KeyboardInterrupt):
        print()
        return default


def t480s_build(uid: str, gid: str) -> int:
    """Compila en Docker y genera el BOOTX64.EFI del pendrive."""
    if not docker_image_exists() or subprocess.run(
            ["docker", "run", "--rm", "--platform", "linux/amd64", DOCKER_IMAGE, "test", "-d", "/usr/lib/grub/x86_64-efi"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
        if run(["docker", "build", "--platform", "linux/amd64", "-t", DOCKER_IMAGE, "."],
               "CONSTRUYENDO TOOLCHAIN (con GRUB para UEFI)") != 0:
            return 1
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
    cflags = os.environ.get("SOMA_EXTRA_CFLAGS", "-DSOMA_WEB_ASYNC_IMAGES=1")
    cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-u", f"{uid}:{gid}", "-v", f"{ROOT}:/soma", "-w", "/soma",
           DOCKER_IMAGE, "make", "ARCH=x86_64", "PLATFORM=qemu_virt", f"EXTRA_CFLAGS={cflags}", "t480s-usb"]
    print(f"\n{'=' * 72}\nCOMPILANDO SOMA PARA EL THINKPAD T480s\n{'=' * 72}\n\n$ {' '.join(cmd)}\n")
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    out = (p.stdout or "") + (p.stderr or "")
    if p.returncode != 0 or not T480S_EFI.is_file():
        print(out[-4000:])
        print("\nERROR de compilacion:")
        for l in summarize_errors(out):
            print("  " + l)
        return 1
    print(f"[OK] {T480S_EFI.relative_to(ROOT)} ({T480S_EFI.stat().st_size / 1e6:.1f} MB)")
    return 0


def _plist(cmd: list[str]) -> dict:
    out = subprocess.run(cmd, capture_output=True)
    if out.returncode != 0:
        return {}
    try:
        return plistlib.loads(out.stdout)
    except Exception:
        return {}


def macos_usb_disks() -> list[dict]:
    """Discos externos y fisicos (diskutil). Cada uno: id, size, name, volumes [(id, nombre, tipo, punto de montaje)]."""
    lst = _plist(["diskutil", "list", "-plist", "external", "physical"])
    disks = []
    for d in lst.get("AllDisksAndPartitions", []):
        dev = d.get("DeviceIdentifier", "")
        if not re.fullmatch(r"disk\d+", dev):
            continue
        info = _plist(["diskutil", "info", "-plist", dev])
        if not info or info.get("Internal", True):                       # doble comprobacion: nunca un disco interno
            continue
        if dev in ("disk0", "disk1"):
            continue
        vols = []
        for part in d.get("Partitions", []) or ([d] if d.get("Content") else []):
            vols.append((part.get("DeviceIdentifier", ""), part.get("VolumeName", "") or "",
                         part.get("Content", "") or "", part.get("MountPoint", "") or ""))
        disks.append({"id": dev, "size": int(info.get("TotalSize") or info.get("Size") or d.get("Size") or 0),
                      "name": info.get("MediaName") or info.get("IORegistryEntryName") or "USB", "bus": info.get("BusProtocol", ""),
                      "volumes": vols})
    return disks


def _soma_volume(disk: dict):
    """Volumen FAT32 llamado SOMA ya preparado en este disco (o None)."""
    for vid, name, content, mnt in disk["volumes"]:
        if name.upper() == T480S_LABEL and ("DOS" in content.upper() or "FAT" in content.upper() or "Microsoft" in content):
            return vid, mnt
    return None


def _data_partition(disk: dict):
    """SOMA_162_T480S: particion SOMADATA. Recien creada se llama SOMADATA; despues de que SOMA la reclame ya no tiene volumen
    legible para macOS, asi que tambien vale la segunda particion FAT sin nombre de un pendrive que tiene el volumen SOMA."""
    vols = disk["volumes"]
    for vid, name, content, mnt in vols:
        if name.upper() == T480S_DATA:
            return vid
    if _soma_volume(disk):
        for vid, name, content, mnt in vols:
            if vid.endswith("s2") and not name and ("DOS" in content.upper() or "FAT" in content.upper()):
                return vid
    return None


def _t480s_partition(dev: str, size: int) -> int:
    """SOMA_162_T480S: MBR con SOMA (arranque, FAT32), SOMADATA (disco de SOMA) y DATOS (el resto, FAT32 para el Mac)."""
    gb = size / 1e9
    if gb >= 7:
        cmd = ["diskutil", "partitionDisk", f"/dev/{dev}", "3", "MBR", "FAT32", T480S_LABEL, "1G", "FAT32", T480S_DATA, "4G", "FAT32", "DATOS", "R"]
        msg = "SOMA 1 GB (arranque) + SOMADATA 4 GB (archivos de SOMA) + DATOS (el resto, para ti)"
    else:
        cmd = ["diskutil", "partitionDisk", f"/dev/{dev}", "2", "MBR", "FAT32", T480S_LABEL, "512M", "FAT32", T480S_DATA, "R"]
        msg = "SOMA 512 MB (arranque) + SOMADATA (el resto, archivos de SOMA)"
    return run(cmd, f"PREPARANDO /dev/{dev}: {msg}")


def _mount_point(vid: str) -> str:
    info = _plist(["diskutil", "info", "-plist", vid])
    mnt = info.get("MountPoint") or ""
    if not mnt:
        subprocess.run(["diskutil", "mount", vid], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        mnt = _plist(["diskutil", "info", "-plist", vid]).get("MountPoint") or ""
    return mnt


def _sha256(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()


def t480s_copy(mnt: str) -> int:
    dst_dir = Path(mnt) / "EFI" / "BOOT"
    dst_dir.mkdir(parents=True, exist_ok=True)
    dst = dst_dir / "BOOTX64.EFI"
    print(f"Copiando BOOTX64.EFI a {dst} ...")
    shutil.copyfile(T480S_EFI, dst)
    os.sync()
    if _sha256(dst) != _sha256(T480S_EFI):
        print("ERROR: la copia en el pendrive no coincide con el original (pendrive defectuoso o lleno).")
        return 1
    inf = Path(mnt) / "INFORME.TXT"                                   # SOMA_228_ARRANQUE: hueco para el informe de hardware
    if not inf.exists() or inf.stat().st_size < 256 * 1024:
        try:
            with open(inf, "wb") as f:
                t = (b"SOMA - informe de arranque y hardware\r\n\r\nEste archivo lo rellena SOMA cada vez que arranca desde este "
                     b"pendrive. Si todavia ves este texto, SOMA no ha llegado a arrancar en este equipo.\r\n")
                f.write(t + b"\n" * (256 * 1024 - len(t)))
            os.sync()
        except OSError as e:
            print(f"(aviso: no se pudo crear INFORME.TXT: {e})")
    if shutil.which("dot_clean"):                                     # quita los ._* que deja macOS
        subprocess.run(["dot_clean", "-m", mnt], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("[OK] Copia verificada (SHA-256 identico).")
    return 0


def t480s_usb_macos(args) -> int:
    disks = macos_usb_disks()
    if not disks:
        print("\nNo hay ningun pendrive conectado (discos externos). Conectalo y vuelve a ejecutar.")
        return 1
    if args.disco:
        sel = [d for d in disks if d["id"] == args.disco.replace("/dev/", "").replace("rdisk", "disk")]
        if not sel:
            print(f"\n{args.disco} no es un disco externo conectado. Externos: {', '.join(d['id'] for d in disks)}")
            return 1
        disk = sel[0]
    else:
        print("\nPendrives / discos externos conectados:")
        for i, d in enumerate(disks, 1):
            vols = ", ".join(v[1] or v[2] for v in d["volumes"] if v[1] or v[2]) or "sin volumenes"
            print(f"  {i}) /dev/{d['id']:<7} {d['size'] / 1e9:6.1f} GB  {d['name']}  ({vols})")
        c = _ask(f"\nElige el pendrive [1-{len(disks)}] (Enter = cancelar): ")
        if not (c.isdigit() and 1 <= int(c) <= len(disks)):
            print("Cancelado.")
            return 1
        disk = disks[int(c) - 1]
    dev = disk["id"]
    soma = _soma_volume(disk)
    data = _data_partition(disk)
    reformat = args.formatear or not soma
    if soma and not data and not args.formatear:
        print(f"\n/dev/{dev} tiene el volumen '{T480S_LABEL}' pero no la particion {T480S_DATA}: SOMA arrancara, pero lo que guardes")
        print("se perdera al apagar. Para anadirla hay que volver a preparar el pendrive (se borra todo lo que tenga).")
        if not args.si and _ask("Escribe BORRAR para prepararlo ahora, o pulsa Enter para solo actualizar SOMA: ") == "BORRAR":
            reformat = True
    if not reformat:
        vid, _ = soma
        print(f"\n/dev/{dev} ya tiene el volumen FAT32 '{T480S_LABEL}': solo se actualiza SOMA (no se borra nada mas"
              + (f"; tus archivos de SOMA siguen en {T480S_DATA})." if data else ")."))
    else:
        if not (soma and not data and not args.formatear):      # (en ese caso ya se pidio BORRAR arriba)
            print(f"\nATENCION: se BORRARA TODO el contenido de /dev/{dev} ({disk['size'] / 1e9:.1f} GB, {disk['name']}),")
            print(f"incluidos los archivos que SOMA guardo en {T480S_DATA}.")
            if not args.si and _ask("Escribe BORRAR para prepararlo: ") != "BORRAR":
                print("Cancelado: no se ha tocado el pendrive.")
                return 1
        if _t480s_partition(dev, disk["size"]) != 0:
            print("ERROR al preparar el pendrive (¿esta protegido o en uso?).")
            return 1
        disk = next((d for d in macos_usb_disks() if d["id"] == dev), None)
        soma = _soma_volume(disk) if disk else None
        if not soma:
            print("ERROR: no aparece el volumen SOMA tras formatear.")
            return 1
        vid, _ = soma
    mnt = _mount_point(vid)
    if not mnt:
        print(f"ERROR: no se pudo montar {vid}.")
        return 1
    if t480s_copy(mnt) != 0:
        return 1
    subprocess.run(["diskutil", "eject", f"/dev/{dev}"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"[OK] /dev/{dev} expulsado. Ya puedes desconectarlo.")
    return 0


def t480s_usb_linux(args) -> int:
    """Linux: copia a una particion FAT montada con etiqueta SOMA (formatear requiere root: se indican las ordenes)."""
    out = subprocess.run(["lsblk", "-J", "-o", "NAME,LABEL,FSTYPE,MOUNTPOINT,TRAN,RM,SIZE"], capture_output=True, text=True)
    import json
    try:
        devs = json.loads(out.stdout).get("blockdevices", [])
    except Exception:
        devs = []
    for d in devs:
        if d.get("tran") != "usb":
            continue
        for part in d.get("children", []) or []:
            if (part.get("label") or "").upper() == T480S_LABEL and part.get("fstype") == "vfat" and part.get("mountpoint"):
                return t480s_copy(part["mountpoint"])
    print("\nNo hay un pendrive USB montado con un volumen FAT32 'SOMA'. Preparalo una vez (cambia sdX por tu pendrive; SE BORRA):")
    print("  sudo parted -s /dev/sdX mklabel msdos mkpart primary fat32 1MiB 1025MiB mkpart primary fat32 1025MiB 5121MiB mkpart primary fat32 5121MiB 100%")
    print("  sudo mkfs.vfat -F 32 -n SOMA /dev/sdX1; sudo mkfs.vfat -F 32 -n SOMADATA /dev/sdX2; sudo mkfs.vfat -F 32 -n DATOS /dev/sdX3")
    print("  (SOMADATA es donde SOMA guarda sus archivos; montalo SOMA y vuelve a ejecutar esta opcion)")
    print(f"Mientras tanto, el archivo esta en {T480S_EFI.relative_to(ROOT)} (va en EFI/BOOT/ del pendrive).")
    return 1


def run_t480s_usb(args) -> int:
    print("\nSOMA para el Lenovo ThinkPad T480s: compilar + copiar al pendrive")
    if not shutil.which("docker"):
        print("ERROR: falta Docker.")
        return 1
    uid, gid = get_host_uid_gid()
    if not args.sin_compilar and t480s_build(uid, gid) != 0:
        return 1
    if not T480S_EFI.is_file():
        print(f"ERROR: no existe {T480S_EFI.relative_to(ROOT)} (ejecuta sin --sin-compilar).")
        return 1
    rc = t480s_usb_macos(args) if sys.platform == "darwin" else t480s_usb_linux(args)
    if rc == 0:
        print("\nEn el portatil: enciende pulsando F12, elige el USB y despues 'SOMA - Lenovo ThinkPad T480s'.")
        print(f"SOMA guarda tus archivos en la particion {T480S_DATA} del pendrive. Si macOS dice que no puede leerla: 'Ignorar'")
        print("(nunca 'Inicializar': borraria lo que SOMA guardo).")
        print("(Si el USB no aparece en F12: BIOS con F1 -> F9 Setup Defaults -> Secure Boot Disabled -> F10.)")
    return rc


def audio_args() -> list[str]:
    """SOMA_PARCHE_020_AUDIO: tarjeta de sonido Intel HDA emulada. SOMA_AUDIO=off la quita; SOMA_AUDIO=<backend>
    fuerza el sistema de sonido de QEMU (coreaudio, pa, alsa, dsound, sdl, none...). Por defecto: el del sistema."""
    mode = os.environ.get("SOMA_AUDIO", "auto")
    if mode == "off":
        return []
    if mode in ("auto", "on"):
        mode = "coreaudio" if sys.platform == "darwin" else "dsound" if sys.platform.startswith("win") else "pa"
    return ["-audiodev", f"{mode},id=snd0", "-device", "intel-hda", "-device", "hda-duplex,audiodev=snd0"]


def x86_machine() -> str:
    """PARCHE 033: SOMA_MACHINE=q35 arranca el x86_64 en una maquina moderna (PCIe, tabla MCFG, AHCI); por defecto 'pc'."""
    m = os.environ.get("SOMA_MACHINE", "pc")
    if os.environ.get("SOMA_SATA") == "1":
        m = "q35"                       # el disco SATA de prueba necesita el AHCI integrado de q35
    return m if m in ("pc", "q35") else "pc"


def sata_args() -> list[str]:
    """PARCHE 040: SOMA_SATA=1 anade un disco SATA de prueba (sata.img, 8 MiB, sector 0 = 'SOMASATA...') al x86_64 en q35.
    Se usa con el comando 'ahci' del shell."""
    if os.environ.get("SOMA_SATA") != "1":
        return []
    img = ROOT / "sata.img"
    if not img.exists():
        with open(img, "wb") as f:
            f.write(b"SOMASATA-TEST-01\n".ljust(512, b"\0"))
            f.write(bytes(8 * 1024 * 1024 - 512))
    return ["-drive", f"file={img},format=raw,if=none,id=sata0", "-device", "ide-hd,drive=sata0,bus=ide.0"]


def nvme_args() -> list[str]:
    """PARCHE 043: SOMA_NVME=1 anade un disco NVMe de prueba (nvme.img, 8 MiB, sector 0 = 'SOMANVME...') al x86_64.
    Se usa con el comando 'nvme' del shell. Se puede combinar con SOMA_SATA=1."""
    if os.environ.get("SOMA_NVME") != "1":
        return []
    img = ROOT / "nvme.img"
    if not img.exists():
        with open(img, "wb") as f:
            f.write(b"SOMANVME-TEST-01\n".ljust(512, b"\0"))
            f.write(bytes(8 * 1024 * 1024 - 512))
    return ["-drive", f"file={img},format=raw,if=none,id=nvm0", "-device", "nvme,drive=nvm0,serial=somanvme"]


def usb_args() -> list[str]:
    """PARCHE 046: SOMA_USB=1 anade un controlador xHCI con un hub y un pendrive (usb.img) al x86_64 (comando 'usb' del shell).
    No anade teclado ni raton USB: QEMU les enviaria TODA la entrada del anfitrion y SOMA aun no tiene controlador HID (el PS/2 se
    quedaria sin teclado). SOMA_USB=kbd los anade tambien (solo para probar la fase 2).
    PARCHE 051: SOMA_USB=hub cuelga teclado y raton del hub (puertos 1.1 y 1.2): prueba la enumeracion tras hubs."""
    mode = os.environ.get("SOMA_USB")
    if mode not in ("1", "kbd", "hub"):
        return []
    img = ROOT / "usb.img"
    # PARCHE 050: disco de PRUEBA de 8 MiB: MBR con firma SOMAUSB- en el sector 0 y una particion FAT16 con ficheros (para 'fat ls usb0').
    # Se regenera si no tiene tabla de particiones valida (las versiones 046-049 eran ceros con la firma).
    need = True
    if img.exists() and img.stat().st_size == 8 * 1024 * 1024:
        with open(img, "rb") as f:
            head = f.read(512)
        need = not (head[:8] == b"SOMAUSB-" and head[510:512] == b"\x55\xAA")
    if need:
        import importlib.util
        spec = importlib.util.spec_from_file_location("mkfat", str(ROOT / "tools" / "mkfat.py"))
        mk = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mk)
        mk.make_usb_test_disk(str(img), 8)
    a = ["-device", "qemu-xhci,id=xhci", "-device", "usb-hub,bus=xhci.0,port=1",
         "-drive", f"file={img},format=raw,if=none,id=usbd0", "-device", "usb-storage,bus=xhci.0,port=2,drive=usbd0"]
    if mode == "kbd":
        a += ["-device", "usb-kbd,bus=xhci.0,port=3", "-device", "usb-mouse,bus=xhci.0,port=4"]
    if mode == "hub":
        a += ["-device", "usb-kbd,bus=xhci.0,port=1.1", "-device", "usb-mouse,bus=xhci.0,port=1.2"]
    return a


def usb_monitor_args() -> list[str]:
    """PARCHE 056: con SOMA_USB activo, el monitor de QEMU queda en build/qemu.mon para enchufar/quitar dispositivos en caliente
    (python3 tools/usbplug.py add|del, desde OTRA terminal)."""
    if os.environ.get("SOMA_USB") not in ("1", "kbd", "hub"):
        return []
    return ["-monitor", "unix:build/qemu.mon,server,nowait"]


def aarch64_machine() -> str:
    """PARCHE 058: SOMA_EL2=1 arranca la maquina virt con virtualization=on: el kernel entra en EL2 como en una Raspberry Pi real."""
    return "virt,virtualization=on" if os.environ.get("SOMA_EL2") == "1" else "virt"


def get_qemu_commands(arch: str) -> tuple[str, list[str], list[str]]:
    if arch == "x86_64":
        qemu_bin = "qemu-system-x86_64"
        canary = [
            qemu_bin, "-machine", x86_machine(), "-m", os.environ.get("SOMA_MEM", "512M"), 
            "-cdrom", "build/soma.iso", "-netdev", "user,id=net0,hostfwd=tcp:127.0.0.1:8090-:80",
            "-device", "virtio-net-pci,netdev=net0",
            "-drive", "file=hdd.img,format=raw,if=virtio",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
            "-serial", "stdio", "-display", "none",
        ] + sata_args() + nvme_args() + usb_args()
        main_cmd = [
            # SOMA 188B: 4 GiB por defecto en la sesion interactiva (con 512 MB el navegador no puede con Google: su portada gasta 260 MiB de JavaScript)
            qemu_bin, "-machine", x86_machine(), "-m", os.environ.get("SOMA_MEM", "4G"), "-d", "guest_errors,cpu_reset", "-D", "build/qemu.log",
            "-cdrom", "build/soma.iso", "-netdev", "user,id=net0,hostfwd=tcp:127.0.0.1:8090-:80", "-device", "virtio-net-pci,netdev=net0",
            "-drive", "file=hdd.img,format=raw,if=virtio",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
            "-object", "filter-dump,id=f1,netdev=net0,file=build/net.pcap",
            "-serial", "stdio",
        ] + audio_args() + sata_args() + nvme_args() + usb_args() + usb_monitor_args()
    else:  # aarch64 – virtio-mmio (más simple y fiable en QEMU virt)
        qemu_bin = "qemu-system-aarch64"
        canary = [
            qemu_bin, "-M", aarch64_machine(), "-cpu", "cortex-a72", "-m", "256M", "-smp", os.environ.get("SOMA_SMP", "4"), "-semihosting",
            "-kernel", "build/aarch64/soma.elf",
            "-netdev", "user,id=net0,hostfwd=tcp:127.0.0.1:8090-:80",
            "-device", "virtio-net-device,netdev=net0",
            "-drive", "file=hdd.img,format=raw,if=none,id=hd0",
            "-device", "virtio-blk-device,drive=hd0",
            "-serial", "stdio", "-display", "none",
        ]
        main_cmd = [
            qemu_bin, "-M", aarch64_machine(), "-cpu", "cortex-a72", "-m", "256M", "-smp", os.environ.get("SOMA_SMP", "4"), "-semihosting",
            "-kernel", "build/aarch64/soma.elf",
            "-netdev", "user,id=net0,hostfwd=tcp:127.0.0.1:8090-:80",
            "-device", "virtio-net-device,netdev=net0",
            "-drive", "file=hdd.img,format=raw,if=none,id=hd0",
            "-device", "virtio-blk-device,drive=hd0",
            "-serial", "stdio",
        ]
    return qemu_bin, canary, main_cmd


def main() -> int:
    ap = argparse.ArgumentParser(description="SOMA runner (Singularity Loop v2 Multi-Arch)")
    ap.add_argument("-a", "--arch", choices=["x86_64", "aarch64"], default=None,
                    help="arquitectura objetivo (x86_64 o aarch64)")
    ap.add_argument("-t", "--target", choices=list(TARGETS), default=None,
                    help="destino: x86_64, virt (aarch64 QEMU virt), pi5, pi5-rp1, pi5-img, pi5-qemu, pi5-sd (Raspberry Pi 5), t480s-usb (ThinkPad T480s)")
    # SOMA_191: la confirmacion humana es el comportamiento POR DEFECTO (antes habia que pedirla con --aprobar).
    ap.add_argument("--aprobar", action="store_true",
                    help="(por defecto desde el parche 191) mostrar el diff y pedir confirmacion antes de aplicar")
    ap.add_argument("--sin-aprobar", dest="sin_aprobar", action="store_true",
                    help="PELIGROSO: aplicar los parches de la IA sin confirmacion humana (modo desatendido)")
    ap.add_argument("--max-ciclos", type=int, default=10,
                    help="maximo de ciclos compilar/arrancar (por defecto 10)")
    ap.add_argument("--snapshot", type=str, metavar="NAME",
                    help="guarda un snapshot completo reproducible (src + hdd.img)")
    ap.add_argument("--restore", type=str, metavar="NAME",
                    help="restaura un snapshot completo (src + hdd.img)")
    ap.add_argument("--snapshots", action="store_true",
                    help="lista todos los snapshots guardados")
    ap.add_argument("--disco", type=str, metavar="diskN",
                    help="t480s-usb: pendrive de destino (p. ej. disk4) sin preguntar cual")
    ap.add_argument("--formatear", action="store_true",
                    help="t480s-usb: formatear el pendrive aunque ya tenga el volumen SOMA")
    ap.add_argument("--si", action="store_true",
                    help="t480s-usb: no pedir la confirmacion BORRAR al formatear (cuidado)")
    ap.add_argument("--sin-compilar", action="store_true",
                    help="t480s-usb: copiar el BOOTX64.EFI ya compilado, sin volver a compilar")
    llm_server.add_arguments(ap)
    search_proxy.add_arguments(ap)
    args = ap.parse_args()

    SNAPSHOT_DIR = ROOT / "snapshots"

    if args.snapshots:
        if not SNAPSHOT_DIR.exists() or not list(SNAPSHOT_DIR.iterdir()):
            print("No hay snapshots guardados en snapshots/.")
        else:
            print("\nSnapshots disponibles en snapshots/:")
            for s in sorted(SNAPSHOT_DIR.iterdir()):
                if s.is_dir():
                    has_hdd = (s / "hdd.img").exists()
                    has_src = (s / "src").exists()
                    print(f"  - {s.name} (src: {'OK' if has_src else 'NO'}, hdd: {'OK' if has_hdd else 'NO'})")
            print()
        return 0

    if args.snapshot:
        s_target = SNAPSHOT_DIR / args.snapshot
        s_target.mkdir(parents=True, exist_ok=True)
        shutil.copytree(SRC_DIR, s_target / "src", dirs_exist_ok=True)
        if HDD.exists():
            shutil.copy2(HDD, s_target / "hdd.img")
        print(f"\n[OK] Snapshot '{args.snapshot}' guardado con exito en {s_target.relative_to(ROOT)}/\n")
        return 0

    if args.restore:
        s_target = SNAPSHOT_DIR / args.restore
        if not s_target.exists():
            print(f"\n[ERROR] El snapshot '{args.restore}' no existe en snapshots/.\n")
            return 1
        shutil.copytree(s_target / "src", SRC_DIR, dirs_exist_ok=True)
        if (s_target / "hdd.img").exists():
            shutil.copy2(s_target / "hdd.img", HDD)
        print(f"\n[OK] Snapshot '{args.restore}' restaurado con exito (src y hdd.img sincronizados).\n")
        return 0

    print("\n" + "=" * 72 + "\nSOMA DEVELOPMENT RUNNER - THE SINGULARITY LOOP v2\n" + "=" * 72 + "\n")

    # Seleccion de destino (CLI o interactiva): -t/--target, -a/--arch (compatibilidad) o menu
    target = args.target
    if not target and args.arch:
        target = "x86_64" if args.arch == "x86_64" else "virt"
    if not target:
        target = choose_target() if sys.stdin.isatty() else "x86_64"
    if target == "t480s-usb":
        return run_t480s_usb(args)
    if TARGETS[target][0] is None:
        return run_pi5(target)
    arch = TARGETS[target][0]

    qemu_bin, canary_cmd, qemu_command = get_qemu_commands(arch)
    globals()["PACK_ARCH"] = arch

    if not shutil.which("docker") or not shutil.which(qemu_bin):
        print(f"ERROR: Faltan dependencias (Docker o {qemu_bin}).")
        return 1

    if not docker_image_exists():
        if run(["docker", "build", "--platform", "linux/amd64", "-t", DOCKER_IMAGE, "."],
               "CONSTRUYENDO TOOLCHAIN") != 0:
            return 1
    uid, gid = get_host_uid_gid()

    if not HDD.exists():
        print("Preparando disco fisico persistente (hdd.img de 16 MiB) fuera de build/ ...")
        with open(HDD, "wb") as f:
            f.write(b"\x00" * (16 * 1024 * 1024))
    # SOMA_PARCHE_018_BIGFS: el disco se AGRANDA (nunca se encoge ni se toca lo que ya tiene) hasta DISK_MB
    if HDD.stat().st_size < DISK_MB * 1024 * 1024:
        os.truncate(HDD, DISK_MB * 1024 * 1024)
        print(f"hdd.img ampliado a {DISK_MB} MiB para el almacen de archivos grandes (SOMA_DISK_MB cambia el tamano)")

    llm_server.ensure_server(args)
    search_proxy.ensure_proxy(args)
    # SOMA_PARCHE_028_UI: pasarela HTTPS del navegador (127.0.0.1:8096). Se arranca sola, en un hilo de fondo,
    # para que las paginas https:// con sus imagenes carguen sin tener que lanzar tools/web_gateway.py a mano.
    try:
        import web_gateway
        web_gateway.ensure_gateway()
    except Exception as exc:
        print(f"[pasarela web] no disponible ({exc}); las paginas https:// no cargaran")

    patched = False
    cycles = 0

    while True:
        cycles += 1
        if cycles > args.max_ciclos:
            print(f"Limite de {args.max_ciclos} ciclos alcanzado. Fin.")
            break

        if BUILD_DIR.exists():
            shutil.rmtree(BUILD_DIR)
        BUILD_DIR.mkdir(exist_ok=True)

        code, out = build(uid, gid, arch)

        if code != 0:
            if patched:
                errs = " ; ".join(summarize_errors(out))
                print("\n[ROLLBACK] El parche de la IA no compila. Restaurando src/ ...")
                restore_src()
                write_result("COMPILE_FAILED (rollback aplicado)", errs)
                arm_agent_resume()
                patched = False
                continue
            print("ERROR de compilacion. Rompiendo bucle.")
            return code

        if patched:
            print("\n" + "=" * 72)
            print(f"PUERTA DE ARRANQUE: PROBANDO KERNEL ({arch}) EN MODO CANARY")
            print("=" * 72)
            precanary = HDD.with_name(HDD.name + ".checkpoint_precanary")
            shutil.copy2(HDD, precanary)
            print(f"[REPRODUCIBILIDAD] Disco guardado antes del canary: {precanary.name}")
            pack_sources()
            arm_boot_gate()

            print("$ " + " ".join(canary_cmd) + "\n")
            try:
                canary_rc = subprocess.run(canary_cmd, cwd=ROOT, timeout=45).returncode
            except subprocess.TimeoutExpired:
                print("\n[BOOT-GATE TIMEOUT] El kernel se colgo o tardo demasiado (>45s).")
                canary_rc = -1

            disarm_boot_gate()

            if canary_rc == QEMU_EXIT_GATE_OK:
                print("\n[BOOT-GATE OK] El nuevo kernel paso el health-check. Parche validado.")
                write_result("BOOT_OK", "kernel nuevo verificado y operativo")
                git_snapshot("SOMA: parche verificado por puerta de arranque")
                arm_agent_resume()
                patched = False
            else:
                print(f"\n[BOOT-GATE FAIL] El kernel fallo en el arranque (rc={canary_rc}).")
                print("[ROLLBACK] Restaurando src/ a la version sana anterior...")
                restore_src()
                shutil.copy2(precanary, HDD)
                print("[ROLLBACK] Disco hdd.img restaurado al estado previo al canary.")
                write_result("BOOT_FAILED (rollback aplicado)", f"canary_rc={canary_rc}")
                arm_agent_resume()
                patched = False
                continue

        pack_sources()

        print("\n" + "=" * 72 + f"\nARRANCANDO SOMA BARE-METAL ({arch})\n" + "=" * 72 + "\n")
        try:
            rc = subprocess.run(qemu_command, cwd=ROOT).returncode
        except KeyboardInterrupt:
            print("\nQEMU detenido por el usuario.")
            break

        outcome = handle_mailbox(not args.sin_aprobar)   # SOMA_191

        if outcome is None:
            if rc == QEMU_EXIT_PANIC:
                print("\n[HOST MONITOR] !!! KERNEL PANIC DETECTADO EN EL GUEST (exit 0x23) !!!\n")
            if rc == QEMU_EXIT_PATCH:
                print("AVISO: el kernel pidio un parche (exit 0x21) pero el buzon esta vacio o corrupto.")
            print(">>> QEMU cerrado sin peticion de auto-modificacion. Fin del script.")
            break

        print(">>> Reiniciando el ciclo de compilacion...")
        patched = outcome

    return 0


if __name__ == "__main__":
    sys.exit(main())
    
