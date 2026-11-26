#!/usr/bin/env python3
"""SOMA_338_AGENTES: el registro de los agentes y la orden agentes en QEMU (sin modelo de lenguaje).

    make ARCH=x86_64 && python3 tests/prueba_agentes_338.py
"""
import os, re, subprocess, sys, tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')
ORDENES = ['@2500', 'agentes', 'investiga sintomas de la gripe', 'soma cuanta memoria libre tengo', '@6000', 'soma explicame la fotosintesis', '@40000', 'agentes', 'uptime']


def main():
    if not os.path.exists(ISO):
        print('falta', ISO, '(make ARCH=x86_64)')
        return 2
    with tempfile.TemporaryDirectory() as d:
        hdd, raw = os.path.join(d, 'hdd.img'), os.path.join(d, 'serie.txt')
        with open(hdd, 'wb') as f:
            f.truncate(256 << 20)
        a = ['qemu-system-x86_64', '-machine', 'pc', '-m', '1G', '-cdrom', ISO, '-boot', 'd', '-drive', f'file={hdd},format=raw,if=virtio',
             '-netdev', 'user,id=n0', '-device', 'virtio-net-pci,netdev=n0',
             '-fw_cfg', 'name=opt/soma/cmds,string=' + ';'.join(ORDENES).replace(',', ',,'),
             '-serial', f'file:{raw}', '-display', 'none', '-no-reboot']
        try:
            subprocess.run(a, timeout=int(os.environ.get('PLAZO', '240')), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            pass
        sal = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', open(raw, 'rb').read().decode('utf-8', 'replace').replace('\r', ''))

    def bloque(orden, n=0):
        partes = sal.split('[CMD] ' + orden + '\n')
        return partes[n + 1].split('[CMD-FIN]')[0] if len(partes) > n + 1 else ''

    esperado = [
        ('al arrancar, en reposo y sin misiones', 'agentes', 0, r'Ahora: en reposo[\s\S]*Misiones recientes:\n  \(ninguna todavia\)'),
        ('las misiones del shell cuentan, con su resultado', 'agentes', 1, r'Misiones recientes:\n  \d\d:\d\d  \[(bien|error)\] soma explicame la fotosintesis|\[(bien|error)\] explicame la fotosintesis'),
        ('dos misiones (las delegaciones no cuentan aparte)', 'agentes', 1, r'Misiones recientes:\n(  \d\d:\d\d  \[[a-z]+\] [^\n]*\n){2}Actividad'),
        ('la investigacion sale en la actividad', 'agentes', 1, r'Actividad reciente:[\s\S]*(search|tema) '),
        ('el Investigador ha trabajado', 'agentes', 1, r'Investigador: [1-9]'),
        ('el sistema sigue', 'uptime', 0, r'Uptime'),
    ]
    fallos = 0
    for nombre, orden, n, patron in esperado:
        texto = bloque(orden, n)
        ok = re.search(patron, texto) is not None
        print(('OK   ' if ok else 'MAL  ') + nombre)
        if not ok:
            fallos += 1
            print('     ' + (texto.strip().replace('\n', '\n     ') or '(sin salida)'))
    print('fallos:', fallos)
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
