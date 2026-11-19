#!/usr/bin/env python3
"""SOMA_175A_T480S: descarga, comprueba e instala el firmware de la WiFi del ThinkPad T480s (Intel 8265).

    python3 tools/firmware_wifi.py              descarga de kernel.org (o de sus replicas) e instala en firmware/
    python3 tools/firmware_wifi.py --archivo F  instala un archivo que ya tengas (p. ej. copiado de un Linux)
    python3 tools/firmware_wifi.py --comprobar  solo comprueba el instalado

El archivo es iwlwifi-8265-36.ucode, del paquete oficial linux-firmware (licencia de Intel que permite redistribuirlo;
se guarda tambien firmware/LICENCE.iwlwifi_firmware). Al compilar "make t480s" se mete en la imagen del pendrive y GRUB lo
carga en memoria junto a SOMA (module2).
"""
import hashlib, os, struct, subprocess, sys, tempfile, urllib.request
from pathlib import Path

NAME = 'iwlwifi-8265-36.ucode'
LIC = 'LICENCE.iwlwifi_firmware'
BASES = [
    'https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/',
    'https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/',
    'https://raw.githubusercontent.com/wkennington/linux-firmware/master/',
]

def check(data: bytes) -> str:
    """'' si es un firmware valido de la 8265 con las imagenes INIT y RUNTIME; si no, el motivo"""
    if len(data) < 1_000_000 or len(data) > 6_000_000: return f'tamano raro ({len(data)} bytes)'
    if data[:4] != b'\0\0\0\0' or data[4:8] != b'IWL\n': return 'no es un firmware Intel en formato TLV'
    off, secs = 88, {19: 0, 20: 0}
    while off + 8 <= len(data):
        t, l = struct.unpack('<II', data[off:off + 8])
        if l > len(data) - off - 8: return 'archivo cortado'
        if t in secs: secs[t] += 1
        off += 8 + ((l + 3) & ~3)
    if not secs[19] or not secs[20]: return 'faltan las imagenes RUNTIME o INIT'
    return ''

def fetch(url: str) -> bytes:
    try:                                                    # curl usa los certificados del sistema (macOS incluido)
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / 'f'
            r = subprocess.run(['curl', '-fsSL', '--max-time', '120', '-o', str(out), url], capture_output=True, timeout=150)
            if r.returncode == 0: return out.read_bytes()
    except (OSError, subprocess.SubprocessError):
        pass
    with urllib.request.urlopen(url, timeout=120) as r:      # sin curl: Python
        return r.read()

def install(root: Path, data: bytes, src: str) -> None:
    d = root / 'firmware'; d.mkdir(exist_ok=True)
    (d / NAME).write_bytes(data)
    h = hashlib.sha256(data).hexdigest()
    (d / (NAME + '.sha256')).write_text(f'{h}  {NAME}\n# origen: {src}\n')
    print(f'Firmware instalado: firmware/{NAME} ({len(data)} bytes, sha256 {h[:16]}...)')

def fetch_any(base: str, name: str) -> bytes:
    """linux-firmware moderno guarda los de Intel en intel/iwlwifi/ (SOMA_175A2); los antiguos, en la raiz"""
    err = None
    for sub in ('intel/iwlwifi/', ''):
        try: return fetch(base + sub + name)
        except Exception as e: err = e
    raise err

def download(root: Path) -> bool:
    for base in BASES:
        try:
            print(f'Descargando {NAME} de {base.split("/")[2]} ...', flush=True)
            data = fetch_any(base, NAME)
            why = check(data)
            if why: print(f'  no vale: {why}'); continue
            install(root, data, base + NAME)
            try:
                lic = fetch_any(base, LIC)
                if b'Intel' in lic: (root / 'firmware' / LIC).write_bytes(lic)
            except Exception:
                pass
            return True
        except Exception as e:
            print(f'  fallo: {e}')
    return False

def main() -> int:
    root = Path(__file__).resolve().parent.parent
    a = sys.argv[1:]
    if a and a[0] == '--comprobar':
        f = root / 'firmware' / NAME
        if not f.exists(): print('No hay firmware instalado'); return 1
        why = check(f.read_bytes()); print('Firmware correcto' if not why else 'Firmware NO valido: ' + why); return 1 if why else 0
    if a and a[0] == '--archivo' and len(a) > 1:
        data = Path(a[1]).read_bytes(); why = check(data)
        if why: print('Ese archivo no vale: ' + why); return 1
        install(root, data, a[1]); return 0
    if download(root): return 0
    print('\nNo se pudo descargar. Hazlo a mano: baja', NAME, 'de')
    print('  ' + BASES[0] + NAME)
    print('y ejecuta: python3 tools/firmware_wifi.py --archivo RUTA/' + NAME)
    return 1

if __name__ == '__main__':
    sys.exit(main())
