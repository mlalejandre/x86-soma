#!/usr/bin/env python3
"""SOMA_341_NAVEGADOR_MEMORIA: una portada de periodico (120 fotos de 990x556 en una rejilla, con el ancho por CSS y el alto
automatico) en el navegador de ring 3, con 4 GiB y con 512 MB. Antes del parche: 15 bien y 105 "sin memoria para mas imagenes".

    make ARCH=x86_64 && python3 tests/prueba_navegador_341.py        (necesita Pillow)
"""
import http.server, os, random, re, socketserver, subprocess, sys, tempfile, threading

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')
N, PUERTO = 120, 18341


def sitio(d):
    from PIL import Image, ImageDraw
    rnd = random.Random(1)
    R = rnd.randrange
    for i in range(N):
        im = Image.new('RGB', (990, 556), (R(256), R(256), R(256)))
        dr = ImageDraw.Draw(im)
        for _ in range(40):
            x0, x1 = sorted((R(990), R(990))); y0, y1 = sorted((R(556), R(556)))
            dr.rectangle([x0, y0, x1, y1], fill=(R(256), R(256), R(256)))
        im.save(os.path.join(d, f'f{i}.jpg'), quality=80)
    html = ['<!doctype html><html><head><meta charset="utf-8"><title>Portada densa 341</title>',
            '<style>.g{display:flex;flex-wrap:wrap}.g a{width:240px;margin:4px}.g img{width:240px}</style></head><body><div class="g">']
    html += [f'<a href="#n{i}"><img src="f{i}.jpg"><span>Titular {i}</span></a>' for i in range(N)]
    html.append('</div></body></html>')
    with open(os.path.join(d, 'index.html'), 'w') as f:
        f.write('\n'.join(html))


def arranca(mem, d):
    serie = os.path.join(d, f'serie_{mem}.txt')
    a = ['qemu-system-x86_64', '-machine', 'pc', '-m', mem, '-smp', '2', '-cdrom', ISO,
         '-netdev', 'user,id=n0', '-device', 'virtio-net-pci,netdev=n0',
         '-fw_cfg', f'name=opt/soma/cmds,string=@4000;webr3 http://10.0.2.2:{PUERTO}/',
         '-serial', f'file:{serie}', '-display', 'none', '-no-reboot']
    p = subprocess.Popen(a, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t = ''
    try:
        for _ in range(240):
            try:
                p.wait(timeout=1)
            except subprocess.TimeoutExpired:
                pass
            t = open(serie, errors='replace').read() if os.path.exists(serie) else ''
            if re.search(r'WEBR3: (OK|FALLO)', t):
                break
    finally:
        p.kill()
    return t


def main():
    if not os.path.exists(ISO):
        print('falta', ISO, '(make ARCH=x86_64)')
        return 2
    fallos = 0
    with tempfile.TemporaryDirectory() as d:
        web = os.path.join(d, 'web'); os.makedirs(web)
        sitio(web)

        class H(http.server.SimpleHTTPRequestHandler):
            def __init__(s, *a, **k): super().__init__(*a, directory=web, **k)
            def log_message(s, *a): pass
        socketserver.TCPServer.allow_reuse_address = True
        srv = socketserver.ThreadingTCPServer(('127.0.0.1', PUERTO), H)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        try:
            for mem in ('4G', '512M'):
                t = arranca(mem, d)
                m = re.search(r'imagenes: (\d+) ok, (\d+) fallan', t)
                h = re.search(r'monton (\d+) KiB de (\d+) MiB', t)
                ok = bool(m) and int(m.group(1)) == N and int(m.group(2)) == 0 and 'WEBR3: OK' in t
                print(('  ok    ' if ok else '  FALLO ') + f'{mem}: ' + (f'{m.group(1)} bien, {m.group(2)} fallan' if m else 'sin resultado') +
                      (f'; monton {int(h.group(1)) >> 10} de {h.group(2)} MiB' if h else ''))
                fallos += not ok
        finally:
            srv.shutdown()
    print('prueba_navegador_341:', 'todo bien' if not fallos else f'{fallos} fallos')
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
