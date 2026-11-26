#!/usr/bin/env python3
"""SOMA_333_SALIDA: el programador ejecuta programas sin abrir el Terminal (QEMU).

    make ARCH=x86_64 && python3 tests/prueba_salida_333.py

Un programa Python que imprime, pregunta (input) y responde; uno que no imprime nada (el panel se cierra solo); uno que no
termina (se para con "ejecutar parar"); y que no se puede lanzar otro mientras uno esta en marcha.
"""
import os, re, subprocess, sys, tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')

ORDENES = [
    'write /workspace/hola.py "print(\'hola desde python\')\\nn = input(\'Nombre? \')\\nprint(\'Adios, \' + n)"',
    'write /workspace/mudo.py "x = 1"',
    'write /workspace/bucle.py "while True:\\n    pass"',
    'ejecutar /workspace/hola.py', '@8000', 'studio salida',
    'ejecutar /workspace/mudo.py',
    'ejecutar escribir Ana', '@4000', 'studio salida',
    'ejecutar /workspace/mudo.py', '@8000', 'studio salida',
    'ejecutar /workspace/bucle.py', '@6000', 'studio salida', 'ejecutar /workspace/hola.py',
    'ejecutar parar', '@3000', 'studio salida',
    'write /workspace/err.py "print(1)\\nprint(1/0)"', 'ejecutar /workspace/err.py', '@6000', 'studio salida',
]


def main():
    if not os.path.exists(ISO):
        print('falta', ISO, '(make ARCH=x86_64)')
        return 2
    with tempfile.TemporaryDirectory() as d:
        hdd, raw = os.path.join(d, 'hdd.img'), os.path.join(d, 'serie.txt')
        with open(hdd, 'wb') as f:
            f.truncate(256 << 20)
        a = ['qemu-system-x86_64', '-machine', 'pc', '-m', '1G', '-smp', '2', '-cpu', 'max', '-cdrom', ISO, '-boot', 'd',
             '-drive', f'file={hdd},format=raw,if=virtio', '-fw_cfg', 'name=opt/soma/cmds,string=' + ';'.join(ORDENES).replace(',', ',,'),
             '-serial', f'file:{raw}', '-display', 'none', '-no-reboot']
        try:
            subprocess.run(a, timeout=int(os.environ.get('PLAZO', '120')), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            pass
        sal = open(raw, 'rb').read().decode('utf-8', 'replace').replace('\r', '')
        sal = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', sal)

    def bloque(orden, n=0):
        partes = sal.split('[CMD] ' + orden + '\n')
        return partes[n + 1].split('[CMD-FIN]')[0] if len(partes) > n + 1 else ''

    esperado = [
        ('se lanza', 'ejecutar /workspace/hola.py', 0, r'En marcha'),
        ('su salida y su pregunta, en el panel', 'studio salida', 0, r'hola desde python\nNombre\? \n\[en marcha\] \[panel visible\]'),
        ('no se lanza otro mientras tanto', 'ejecutar /workspace/mudo.py', 0, r'Ya hay un programa en marcha'),
        ('se le escribe', 'ejecutar escribir Ana', 0, r'Enviado'),
        ('pregunta, respuesta y lo que sigue', 'studio salida', 1, r'hola desde python\nNombre\? Ana\nAdios, Ana\n[\s\S]*\[terminado\] \[panel visible\]'),
        ('sin salida, el panel se cierra solo', 'studio salida', 2, r'\[terminado\] \[panel oculto\]'),
        ('un bucle sin fin sigue en marcha', 'studio salida', 3, r'\[en marcha\]'),
        ('y se para', 'studio salida', 4, r'\[parado\] \[panel visible\]'),
        ('un error se ve, y como error', 'studio salida', 5, r'1\nTraceback[\s\S]*ZeroDivisionError[\s\S]*\[termino con error\]'),
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
