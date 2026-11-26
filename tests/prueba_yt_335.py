#!/usr/bin/env python3
"""SOMA_335_YT_BUSQUEDAS: sin modelo de lenguaje, el buscador con IA busca lo esencial de la peticion (QEMU).

    make ARCH=x86_64 && python3 tests/prueba_yt_335.py
"""
import os, re, subprocess, sys, tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')
ORDENES = ['@2500', 'youtube ia on', 'youtube quiero ver videos de gatos graciosos en youtube, por favor', '@15000', 'youtube estado']


def main():
    if not os.path.exists(ISO):
        print('falta', ISO, '(make ARCH=x86_64)')
        return 2
    with tempfile.TemporaryDirectory() as d:
        hdd, raw = os.path.join(d, 'hdd.img'), os.path.join(d, 'serie.txt')
        with open(hdd, 'wb') as f:
            f.truncate(256 << 20)
        a = ['qemu-system-x86_64', '-machine', 'pc', '-m', '1G', '-cdrom', ISO, '-boot', 'd', '-drive', f'file={hdd},format=raw,if=virtio',
             '-fw_cfg', 'name=opt/soma/cmds,string=' + ';'.join(ORDENES).replace(',', ',,'),
             '-serial', f'file:{raw}', '-display', 'none', '-no-reboot']
        try:
            subprocess.run(a, timeout=int(os.environ.get('PLAZO', '70')), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            pass
        sal = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', open(raw, 'rb').read().decode('utf-8', 'replace').replace('\r', ''))
    ok = re.search(r'ultima busqueda: "gatos graciosos"', sal) is not None
    print(('OK   ' if ok else 'MAL  ') + 'sin modelo, se busca "gatos graciosos" (no la frase entera)')
    if not ok:
        print(sal.split('[CMD] youtube estado')[-1][:800])
    print('fallos:', 0 if ok else 1)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
