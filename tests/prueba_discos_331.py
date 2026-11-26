#!/usr/bin/env python3
"""SOMA_331_DISCOS: la herramienta de discos de punta a punta en QEMU (q35 con un disco SATA y uno NVMe de prueba).

    make ARCH=x86_64 && python3 tests/prueba_discos_331.py

Comprueba en SOMA: discos protegidos, desbloqueo con el nombre, el disco del sistema que no se deja, tabla GPT nueva con FAT32,
borrar y crear particiones (FAT32 y exFAT) y grabar una imagen con verificacion. Despues mira los discos desde el anfitrion:
sfdisk lee la tabla, fsck.fat y fsck.exfat (si estan) dan los sistemas de archivos por buenos y el NVMe empieza por la imagen.
"""
import os, re, shutil, subprocess, sys, tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')
IMAGEN = 'SOMAIMG-331-imagen-de-prueba'

ORDENES = [
    'big format yes',
    'discos lista',
    'discos formatear sda',
    'discos desbloquear sda nvme0',
    'discos desbloquear vda vda',
    'discos desbloquear sda sda',
    'discos formatear sda gpt fat32 PRUEBA',
    'discos lista',
    'discos nueva sda 8',
    'discos borrar sda 1',
    'discos nueva sda 16 fat32 DOS',
    'discos nueva sda 34 fat32 DOS',
    'discos nueva sda 0 exfat TRES',
    'discos lista',
    'big write /prueba.img ' + IMAGEN,
    'discos grabar nvme0 /prueba.img',
    'discos desbloquear nvme0 nvme0',
    'discos grabar nvme0 /prueba.img',
    'discos grabar nvme0 /no-existe.img',
    '@500',
]


def herramienta(nombre):
    return shutil.which(nombre) or next((p for p in (f'/sbin/{nombre}', f'/usr/sbin/{nombre}') if os.path.exists(p)), None)


def main():
    if not os.path.exists(ISO):
        print('falta', ISO, '(make ARCH=x86_64)')
        return 2
    fallos = 0
    with tempfile.TemporaryDirectory() as d:
        hdd, sata, nvme, raw = (os.path.join(d, n) for n in ('hdd.img', 'sata.img', 'nvme.img', 'serie.txt'))
        for ruta, tam in ((hdd, 256), (sata, 64), (nvme, 32)):
            with open(ruta, 'wb') as f:
                f.truncate(tam << 20)
        a = ['qemu-system-x86_64', '-machine', 'q35', '-m', '1G', '-cdrom', ISO,
             '-drive', f'file={hdd},format=raw,if=virtio',
             '-drive', f'file={sata},format=raw,if=none,id=s0', '-device', 'ide-hd,drive=s0,bus=ide.0',
             '-drive', f'file={nvme},format=raw,if=none,id=n0', '-device', 'nvme,drive=n0,serial=somanvme',
             '-fw_cfg', 'name=opt/soma/cmds,string=' + ';'.join(ORDENES),
             '-serial', f'file:{raw}', '-display', 'none', '-no-reboot']
        try:
            subprocess.run(a, timeout=int(os.environ.get('PLAZO', '90')), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            pass
        sal = open(raw, 'rb').read().decode('utf-8', 'replace').replace('\r', '')
        sal = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', sal)

        def bloque(orden, n=0):
            partes = sal.split('[CMD] ' + orden + '\n')
            return partes[n + 1].split('[CMD-FIN]')[0] if len(partes) > n + 1 else ''

        esperado = [
            ('los discos se ven y estan protegidos', 'discos lista', 0, r'sda .*ahci, sin tabla, protegido'),
            ('el NVMe tambien', 'discos lista', 0, r'nvme0 .*nvme, sin tabla, protegido'),
            ('un disco protegido no se escribe', 'discos formatear sda', 0, r'protegido|desbloque'),
            ('el desbloqueo pide el mismo nombre', 'discos desbloquear sda nvme0', 0, r'nombre'),
            ('el disco del sistema no se desbloquea', 'discos desbloquear vda vda', 0, r'sistema'),
            ('desbloqueo correcto', 'discos desbloquear sda sda', 0, r'desbloqueado'),
            ('GPT nueva con FAT32', 'discos formatear sda gpt fat32 PRUEBA', 0, r'hecho|listo|formatead'),
            ('la tabla nueva se lee', 'discos lista', 1, r'sda .*GPT, desbloqueado[\s\S]*1\. .*FAT32 "PRUEBA"'),
            ('sin hueco no se crea nada', 'discos nueva sda 8', 0, r'hueco|espacio|cabe'),
            ('borrar la particion', 'discos borrar sda 1', 0, r'hecho|borrad'),
            ('16 MiB no dan para FAT32', 'discos nueva sda 16 fat32 DOS', 0, r'demasiado pequena'),
            ('crear FAT32 de 34 MiB', 'discos nueva sda 34 fat32 DOS', 0, r'hecho|cread'),
            ('crear exFAT con el resto', 'discos nueva sda 0 exfat TRES', 0, r'hecho|cread'),
            ('las dos particiones', 'discos lista', 2, r'1\. .*FAT32 "DOS"[\s\S]*2\. .*exFAT "TRES"'),
            ('grabar exige desbloquear', 'discos grabar nvme0 /prueba.img', 0, r'protegido|desbloque'),
            ('grabar y comprobar', 'discos grabar nvme0 /prueba.img', 1, r'imagen grabada y comprobada'),
            ('imagen que no existe', 'discos grabar nvme0 /no-existe.img', 0, r'no se puede leer la imagen'),
        ]
        for nombre, orden, n, patron in esperado:
            texto = bloque(orden, n)
            ok = re.search(patron, texto) is not None
            print(('OK   ' if ok else 'MAL  ') + nombre)
            if not ok:
                fallos += 1
                print('     ' + (texto.strip().replace('\n', '\n     ') or '(sin salida)'))

        # desde el anfitrion
        print('--- desde el anfitrion')
        if open(nvme, 'rb').read(len(IMAGEN)) == IMAGEN.encode():
            print('OK   el NVMe empieza por la imagen')
        else:
            print('MAL  el NVMe no tiene la imagen'); fallos += 1
        sf = herramienta('sfdisk')
        parts = []
        if sf:
            r = subprocess.run([sf, '-d', sata], capture_output=True, text=True)
            parts = [(int(m.group(1)), int(m.group(2))) for m in re.finditer(r'start=\s*(\d+), size=\s*(\d+)', r.stdout)]
            ok = r.returncode == 0 and 'label: gpt' in r.stdout and len(parts) == 2 and parts[0][1] == 34 * 2048
            print(('OK   ' if ok else 'MAL  ') + 'sfdisk lee la GPT con las dos particiones')
            if not ok:
                fallos += 1; print(r.stdout, r.stderr)
        else:
            print('--   sin sfdisk: no se mira la tabla desde el anfitrion')
        datos = open(sata, 'rb').read()
        for k, (herr, nombre) in enumerate((('fsck.fat', 'FAT32'), ('fsck.exfat', 'exFAT'))):
            h = herramienta(herr)
            if not h or len(parts) < 2:
                print(f'--   sin {herr} o sin tabla: no se comprueba el {nombre}')
                continue
            ini, tam = parts[k]
            img = os.path.join(d, f'p{k}.img')
            open(img, 'wb').write(datos[ini * 512:(ini + tam) * 512])
            r = subprocess.run([h, '-n', img], capture_output=True, text=True)
            ok = r.returncode == 0
            print(('OK   ' if ok else 'MAL  ') + f'{herr} da el {nombre} por bueno')
            if not ok:
                fallos += 1; print(r.stdout[-800:], r.stderr[-800:])
    print('fallos:', fallos)
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
