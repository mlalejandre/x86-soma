#!/usr/bin/env python3
"""SOMA_334_ORBE_MD: una respuesta con tabla, LaTeX y codigo en el orbe (QEMU, con captura de pantalla).

    make ARCH=x86_64 && python3 tests/prueba_orbe_md_334.py

Deja la captura en build/tests/orbe_md_334.png. Comprueba que la orden funciona, que el sistema sigue respondiendo despues y
que en la zona de la respuesta hay texto pintado (y fondos: la cabecera de la tabla y el bloque de codigo).
"""
import os, re, socket, subprocess, sys, tempfile, time

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')
MD = ('## Comparativa\\n\\n| Planeta | Radio (km) | Lunas |\\n|:--|--:|:-:|\\n| Tierra | 6371 | 1 |\\n| Marte | 3390 | 2 |\\n\\n'
      'La energia es \\\\(E = mc^2\\\\) y la media (el shell se come los dolares):\\n\\n\\\\[\\\\bar{x} = \\\\frac{1}{n} \\\\sum_{i=1}^{n} x_i\\\\]\\n\\n'
      '```python\\ndef media(xs):\\n    return sum(xs) / len(xs)\\n```\\n\\n- uno\\n- **dos**\\n')


def main():
    if not os.path.exists(ISO):
        print('falta', ISO, '(make ARCH=x86_64)')
        return 2
    os.makedirs(os.path.join(RAIZ, 'build', 'tests'), exist_ok=True)
    png = os.path.join(RAIZ, 'build', 'tests', 'orbe_md_334.png')
    ordenes = ['@9000', 'write /workspace/r.md "' + MD + '"', 'orbe /workspace/r.md', '@6000', 'uptime']
    with tempfile.TemporaryDirectory() as d:
        hdd, raw, mon, ppm = (os.path.join(d, n) for n in ('hdd.img', 'serie.txt', 'mon.sock', 'p.ppm'))
        with open(hdd, 'wb') as f:
            f.truncate(256 << 20)
        q = subprocess.Popen(['qemu-system-x86_64', '-machine', 'pc', '-m', '1G', '-cdrom', ISO, '-boot', 'd',
                              '-drive', f'file={hdd},format=raw,if=virtio', '-fw_cfg', 'name=opt/soma/cmds,string=' + ';'.join(ordenes).replace(',', ',,'),
                              '-serial', f'file:{raw}', '-display', 'none', '-monitor', f'unix:{mon},server,nowait', '-no-reboot'],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sal = ''

        def leer():
            try:
                return open(raw, 'rb').read().decode('utf-8', 'replace')
            except OSError:
                return ''

        def captura(ruta):
            try:
                s = socket.socket(socket.AF_UNIX); s.connect(mon); time.sleep(0.3)
                s.send(('screendump ' + ruta + '\n').encode()); time.sleep(2); s.close()
            except OSError:
                pass
        for _ in range(60):                                       # antes: el escritorio recien arrancado
            time.sleep(1)
            if '[ARRANQUE] completo' in leer():
                break
        time.sleep(3)
        captura(ppm + '.antes')
        for _ in range(int(os.environ.get('PLAZO', '90'))):
            time.sleep(1)
            sal = leer()
            if '[CMD] uptime' in sal and '[CMD-FIN]' in sal.split('[CMD] uptime')[-1]:
                break
        time.sleep(1)
        captura(ppm)
        try:
            s = socket.socket(socket.AF_UNIX); s.connect(mon); time.sleep(0.3); s.send(b'quit\n'); time.sleep(0.5)
        except OSError:
            pass
        try:
            q.wait(timeout=10)
        except subprocess.TimeoutExpired:
            q.kill()
        datos = open(ppm, 'rb').read() if os.path.exists(ppm) else b''
        antes = open(ppm + '.antes', 'rb').read() if os.path.exists(ppm + '.antes') else b''
    sal = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', sal.replace('\r', ''))
    fallos = 0

    def ok(nombre, c):
        nonlocal fallos
        print(('OK   ' if c else 'MAL  ') + nombre)
        fallos += 0 if c else 1

    ok('la orden pone el texto en el orbe', 'En el orbe.' in sal)
    ok('el sistema sigue respondiendo', re.search(r'\[CMD\] uptime\n[\s\S]*?\[CMD-FIN\]', sal) is not None)
    ok('hay captura', datos.startswith(b'P6'))
    if datos.startswith(b'P6'):
        cab = datos.split(b'\n', 3)
        w, h = map(int, cab[1].split()); px = cab[3]
        pa = antes.split(b'\n', 3)[3] if antes.startswith(b'P6') else b''
        claros = fondos = cambios = 0
        for y in range(h // 2, h - 60, 2):                       # la respuesta va bajo el orbe
            for x in range(w // 4, 3 * w // 4, 2):
                i = (y * w + x) * 3; r, g, b = px[i], px[i + 1], px[i + 2]
                if pa and abs(r - pa[i]) + abs(g - pa[i + 1]) + abs(b - pa[i + 2]) > 60: cambios += 1
                if r > 200 and g > 200 and b > 200: claros += 1
                elif 8 <= r <= 40 and 12 <= g <= 50 and 20 <= b <= 70 and b > r: fondos += 1
        ok('la respuesta ha cambiado la pantalla', cambios > 8000)
        ok('hay texto pintado en la respuesta', claros > 400)
        ok('hay fondos (tabla, codigo)', fondos > 2000)
        try:
            from PIL import Image
            Image.frombytes('RGB', (w, h), px[:w * h * 3]).save(png)
            print('captura:', png)
        except Exception:
            open(png[:-4] + '.ppm', 'wb').write(datos)
    print('fallos:', fallos)
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
