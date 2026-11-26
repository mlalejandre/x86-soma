#!/usr/bin/env python3
"""SOMA_314_PROGZ: los programas de ring 3 que van comprimidos en la imagen siguen arrancando (QEMU).

Lanza python, js y cc desde la consola con una lista de ordenes (fw_cfg opt/soma/cmds) y comprueba que cada uno responde y que
el nucleo no avisa de ningun programa incrustado danado. (El navegador lo prueba `make sbx-check`, que ademas descomprime los
seis programas y comprueba que una copia danada no se ejecuta.) Necesita qemu-system-x86_64 y la ISO ya construida:

    make ARCH=x86_64 PLATFORM=qemu_virt
    python3 tests/prueba_progz_314.py            (devuelve 0 si todo sale bien; VER=1 muestra la salida si falla)
"""
import os, re, subprocess, sys, tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')

ORDENES = [
    'python -c "print(6*7+1000)"',
    'js -c "console.log(6*7+2000)"',
    'write /workspace/z.c "int main(void) { return 9 }"',
    'cc z.c',
    'python -c "print(6*7+3000)"',
    'js -c "console.log(6*7+4000)"',
    'procs',
    '@300',
]

ESPERADO = [   # (descripcion, expresion regular sobre la salida)
    ('python arranca', r'1042'),
    ('js arranca', r'2042'),
    ('cc arranca (y da el error de compilacion)', r'z\.c:1: error'),
    ('python arranca otra vez', r'3042'),
    ('js arranca otra vez', r'4042'),
]


def main():
    if not os.path.exists(ISO):
        print('falta', ISO, '(make ARCH=x86_64 PLATFORM=qemu_virt)')
        return 2
    with tempfile.TemporaryDirectory() as d:
        hdd, raw = os.path.join(d, 'hdd.img'), os.path.join(d, 'serie.txt')
        with open(hdd, 'wb') as f:
            f.truncate(64 << 20)
        cmds = ';'.join(ORDENES)
        a = ['qemu-system-x86_64', '-machine', 'pc', '-m', '1G', '-smp', '2', '-cdrom', ISO,
             '-drive', f'file={hdd},format=raw,if=virtio', '-fw_cfg', f'name=opt/soma/cmds,string={cmds}',
             '-serial', f'file:{raw}', '-display', 'none', '-no-reboot']
        try:
            subprocess.run(a, timeout=int(os.environ.get('PLAZO', '90')), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            pass
        sal = open(raw, 'rb').read().decode('utf-8', 'replace').replace('\r', '')
        sal = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', sal)
    fallos = 0
    for desc, rx in ESPERADO + [('ningun programa incrustado danado', None)]:
        ok = (re.search(rx, sal) is not None) if rx else ('[progz]' not in sal)
        fallos += not ok
        print(('  ok   ' if ok else '  FALLO'), desc)
    total = len(ESPERADO) + 1
    print(f'prueba_progz_314: {total - fallos}/{total} bien')
    if fallos and os.environ.get('VER'):
        print(sal[sal.find('[CMD] python'):])
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
