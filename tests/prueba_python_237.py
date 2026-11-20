#!/usr/bin/env python3
"""SOMA_237_PYTHON: prueba de la orden `python` (MicroPython en ring 3) en QEMU.

Arranca build/x86_64/soma.iso con una lista de ordenes (fw_cfg opt/soma/cmds), escribe unos scripts en /workspace con `write`
y comprueba lo que sale por la consola serie. Necesita qemu-system-x86_64 y la ISO ya construida:

    make ARCH=x86_64 PLATFORM=qemu_virt
    python3 tests/prueba_python_237.py            (devuelve 0 si todo sale bien)

Limites del arnes, no de Python: cada orden de fw_cfg va hasta 127 caracteres, ';' separa ordenes y ',' se escribe ',,'.
"""
import os, re, subprocess, sys, tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')

ARCHIVOS = {   # nombre -> codigo (\n literal: `write` lo convierte en salto de linea; sin ';' ni '%')
    'util.py':  r"def doble(x):\n    return 2*x",
    'p1.py':    r"import sys,, util,, math\nprint('args',, sys.argv[1:])\nprint(util.doble(21),, math.sqrt(16))",
    'p2.py':    r"import soma\nsoma.escribir('sal.txt',, 'hola desde python')\nprint(soma.leer('sal.txt'))",
    'p3.py':    r"try:\n open('/etc/llm.conf')\nexcept OSError as e:\n print('leer',, e)",
    'p4.py':    r"try:\n open('/x.txt',, 'w')\nexcept OSError as e:\n print('escribir',, e)",
    'p5.py':    r"with open('t.txt',, 'w') as f:\n f.write('uno' + chr(10) + 'dos')\nprint(list(open('t.txt')))",
    'p6.py':    r"import json,, re,, random\nprint(json.dumps({'a': [1,, 2]}),, re.match('a+',, 'aab').group(0))",
    'p7.py':    r"import time,, random\nprint(time.time() > 1.7e9,, len(time.localtime()),, 0 <= random.random() < 1)",
    'p8.py':    r"try:\n input('nombre? ')\nexcept EOFError:\n print('sin entrada')",
}

ORDENES = [f'write /workspace/{n} "{c}"' for n, c in ARCHIVOS.items()] + [
    'python -c "print(2**100,, 1/3)"',
    'python -c "raise SystemExit(3)"',
    'python -c "print(1"',
    'python p1.py uno dos',
    'python p2.py', 'cat /workspace/sal.txt',
    'python p3.py', 'python p4.py', 'python p5.py', 'python p6.py', 'python p7.py', 'python p8.py',
    'python noexiste.py',
    'python /etc/llm.conf',
    'python -c "x = [0] * 10**8"',
    'python -c "import os"',
    'procs',
    '@500',
]

ESPERADO = [   # (descripcion, expresion regular sobre la salida)
    ('enteros grandes y float', r'1267650600228229401496703205376 0\.3333333333333333'),
    ('SystemExit con codigo', r'python: codigo de salida 3'),
    ('error de sintaxis', r'SyntaxError'),
    ('argv e import de /workspace', r"args \['uno', 'dos'\]\n42 4\.0"),
    ('modulo soma: escribir y leer', r'hola desde python'),
    ('open() no lee rutas privadas', r'leer \[Errno 13\] EACCES'),
    ('open() solo escribe en /workspace', r'escribir \[Errno 13\] EACCES'),
    ('open() con with', r"\['uno\\n', 'dos'\]"),
    ('json y re', r'\{"a": \[1, 2\]\} aa'),
    ('time y random', r'True 8 True'),
    ('input sin entrada da EOFError', r'sin entrada'),
    ('script que no existe', r'python: no existe /workspace/noexiste\.py'),
    ('script privado', r'python: sin permiso para leer /etc/llm\.conf'),
    ('memoria acotada', r'MemoryError'),
    ('modulos no incluidos', r"ImportError: no module named 'os'"),
]


def main():
    if not os.path.exists(ISO):
        print('falta', ISO, '(make ARCH=x86_64 PLATFORM=qemu_virt)')
        return 2
    for o in ORDENES:
        if len(o.replace(',,', ',')) > 127:
            print('orden demasiado larga para fw_cfg:', o)
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
    for desc, rx in ESPERADO:
        ok = re.search(rx, sal) is not None
        fallos += not ok
        print(('  ok   ' if ok else '  FALLO'), desc)
    print(f'prueba_python_237: {len(ESPERADO) - fallos}/{len(ESPERADO)} bien')
    if fallos and os.environ.get('VER'):
        print(sal[sal.find('[CMD] python'):])
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
