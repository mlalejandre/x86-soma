#!/usr/bin/env python3
"""SOMA_239_PYTHON_GRAFICO: prueba de soma.win (la ventana de Python) en QEMU, con el escritorio en marcha.

Arranca build/x86_64/soma.iso, escribe un script en /workspace con `write` y lo ejecuta con `python`; comprueba lo que imprime
por la consola serie. Necesita qemu-system-x86_64 y la ISO ya construida:

    make ARCH=x86_64 PLATFORM=qemu_virt
    python3 tests/prueba_python_239.py            (devuelve 0 si todo sale bien)

En las ordenes de fw_cfg ';' separa ordenes y ',' se escribe ',,'.
"""
import os, re, subprocess, sys, tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')

VENTANA = r"""from soma import win
import time
print('teclas',, win.UP,, win.DOWN,, win.LEFT,, win.RIGHT,, hex(win.rgb(1,, 2,, 3)))
win.create('Prueba',, 300,, 200)
print('activa',, win.active())
w,, h = win.size()
print('tamano',, w > 100 and h > 50)
win.clear(0x101828)
win.rect(10,, 10,, 50,, 50,, 0xFF0000)
win.text(10,, 80,, 'hola',, 0xFFFFFF)
win.text(10,, 90,, 42,, 0xFFFFFF)
win.line(0,, 0,, 100000,, 100000,, 0x00FF00)
win.circle(100,, 100,, 20,, 0x0000FF)
for i in range(3000): win.pixel(i % 300,, i // 300,, 0xFFFF00)
win.present()
m = win.mouse()
print('raton',, type(m.x).__name__,, m.clicked in (True,, False),, m.inside in (True,, False))
print('tecla',, win.key())
try:
    win.rect(1,, 2)
except TypeError:
    print('error de argumentos')
time.sleep_ms(300)
win.close()
time.sleep_ms(500)
print('cerrada',, not win.active())
"""

ARCHIVO = r"""open('w.txt',, 'w').write('sin close')
print('guardado',, open('w.txt').read())
"""


def fw(s):
    return s.replace('\n', '\\n').replace('"', '\\"')


ORDENES = [
    '@4000',
    f'write /workspace/v.py "{fw(VENTANA)}"',
    'python v.py',
    f'write /workspace/a.py "{fw(ARCHIVO)}"',
    'python a.py',
    'cat /workspace/w.txt',
    '@500',
]

ESPERADO = [
    ('constantes de teclas y rgb', r'teclas 16 14 2 6 0x10203'),
    ('la ventana se abre', r'activa True'),
    ('tamano del area de dibujo', r'tamano True'),
    ('raton como objeto con atributos', r'raton int True True'),
    ('sin tecla pendiente = 0', r'tecla 0'),
    ('argumentos que faltan = TypeError', r'error de argumentos'),
    ('win.close() la cierra', r'cerrada True'),
    ('open().write() sin close guarda', r'guardado sin close'),
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
            subprocess.run(a, timeout=int(os.environ.get('PLAZO', '75')), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            pass
        sal = open(raw, 'rb').read().decode('utf-8', 'replace').replace('\r', '')
        sal = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', sal)
    fallos = 0
    for desc, rx in ESPERADO:
        ok = re.search(rx, sal) is not None
        fallos += not ok
        print(('  ok   ' if ok else '  FALLO'), desc)
    print(f'prueba_python_239: {len(ESPERADO) - fallos}/{len(ESPERADO)} bien')
    if fallos and os.environ.get('VER'):
        print(sal[sal.find('[CMD] python'):])
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
