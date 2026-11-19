#!/usr/bin/env python3
"""SOMA 152: medicion automatica de la fluidez del navegador en QEMU.

Arranca build/soma.iso con el navegador abierto en una URL (fw_cfg opt/soma/openurl), espera a que cargue, mide la CPU
del proceso QEMU en reposo, simula la rueda del raton y las flechas por QMP y resume las lineas [web152] del puerto serie.

  make clean && make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS='-DSOMA_WEB_PERF_LOG=1'
  python3 tools/web_bench.py --serve ./mis_paginas http://10.0.2.2:8000/pagina.html

--serve DIR sirve DIR por HTTP en el puerto 8000 del anfitrion (el invitado lo ve como 10.0.2.2:8000).
Para comparar antes/despues, ejecutalo con cada build: lo importante son 'cpu_reposo', 'cpu_rueda' y los fotogramas.
"""
from __future__ import annotations
import argparse, functools, http.server, json, os, re, socket, subprocess, sys, tempfile, threading, time
from pathlib import Path


class QMP:
    def __init__(self, path: str):
        self.s = socket.socket(socket.AF_UNIX)
        for _ in range(100):
            try:
                self.s.connect(path); break
            except OSError:
                time.sleep(0.1)
        self.f = self.s.makefile('rwb')
        self.f.readline(); self.cmd('qmp_capabilities')

    def cmd(self, c: str, **a):
        self.f.write((json.dumps({'execute': c, 'arguments': a}) + '\n').encode()); self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if 'return' in r or 'error' in r:
                return r

    def ev(self, *events):
        return self.cmd('input-send-event', events=list(events))

    def wheel(self, down: bool = True):
        b = 'wheel-down' if down else 'wheel-up'
        self.ev({'type': 'btn', 'data': {'down': True, 'button': b}})
        self.ev({'type': 'btn', 'data': {'down': False, 'button': b}})

    def key(self, k: str):
        self.ev({'type': 'key', 'data': {'down': True, 'key': {'type': 'qcode', 'data': k}}})
        time.sleep(0.03)
        self.ev({'type': 'key', 'data': {'down': False, 'key': {'type': 'qcode', 'data': k}}})


def cpu_seconds(pid: int) -> float:
    try:
        f = open(f'/proc/{pid}/stat').read().split(')')[1].split()
        return (int(f[11]) + int(f[12])) / os.sysconf('SC_CLK_TCK')
    except OSError:                                   # macOS: sin /proc
        out = subprocess.run(['ps', '-o', 'time=', '-p', str(pid)], capture_output=True, text=True).stdout.strip()
        parts = [float(x) for x in re.split('[:]', out.replace('-', ':'))] if out else [0]
        sec = 0.0
        for x in parts:
            sec = sec * 60 + x
        return sec


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('url')
    ap.add_argument('--serve', metavar='DIR', help='servir DIR en http://10.0.2.2:8000/')
    ap.add_argument('--load-wait', type=float, default=30.0, help='segundos de espera tras arrancar (carga)')
    ap.add_argument('--iso', default='build/soma.iso')
    ap.add_argument('--mem', default='512M')
    a = ap.parse_args()
    if not Path(a.iso).is_file():
        print('Falta', a.iso, '(compila antes con make)', file=sys.stderr); return 1
    if a.serve:
        handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=a.serve)
        httpd = http.server.ThreadingHTTPServer(('0.0.0.0', 8000), handler)
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
    td = tempfile.mkdtemp(prefix='soma152_')
    qmp_path = os.path.join(td, 'qmp.sock')
    disk = 'hdd.img' if Path('hdd.img').is_file() else None
    cmd = ['qemu-system-x86_64', '-machine', 'pc', '-m', a.mem, '-no-reboot', '-cdrom', a.iso,
           '-netdev', 'user,id=net0', '-device', 'virtio-net-pci,netdev=net0',
           '-fw_cfg', f'name=opt/soma/openurl,string={a.url}',
           '-serial', 'stdio', '-display', 'none', '-qmp', f'unix:{qmp_path},server,nowait']
    if disk:
        cmd += ['-drive', f'file={disk},format=raw,if=virtio,snapshot=on']
    q = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    lines: list[str] = []
    threading.Thread(target=lambda: [lines.append(l.decode(errors='replace').rstrip()) for l in q.stdout], daemon=True).start()
    try:
        qmp = QMP(qmp_path)
        time.sleep(a.load_wait)
        c0 = cpu_seconds(q.pid); time.sleep(8); idle = (cpu_seconds(q.pid) - c0) / 8
        n0 = len(lines)
        c0 = cpu_seconds(q.pid); t0 = time.time()
        for _ in range(40):
            qmp.wheel(True); time.sleep(0.1)
        for _ in range(10):
            qmp.wheel(False); time.sleep(0.1)
        time.sleep(1.0)
        scroll = (cpu_seconds(q.pid) - c0) / (time.time() - t0)
        for _ in range(10):
            qmp.key('down'); time.sleep(0.12)
        time.sleep(2)
        qmp.cmd('screendump', filename=os.path.abspath('build/web_bench.ppm'))
    finally:
        q.terminate()
        try:
            q.wait(5)
        except subprocess.TimeoutExpired:
            q.kill()
    loads = [l for l in lines if '[web152] carga' in l]
    frames = [l for l in lines[n0:] if '[web152] 3s' in l]
    print(f'cpu_reposo={idle * 100:.0f}%  cpu_rueda={scroll * 100:.0f}%  (proceso QEMU en el anfitrion)')
    for l in loads[-3:]:
        print(l)
    for l in frames:
        print(l)
    scripts = [l for l in lines if '[web152] script' in l]          # 152B: scripts lentos (>= 20 ms) y fin de scripts
    for l in sorted((l for l in scripts if 'terminados' not in l), key=lambda x: -int(re.findall(r'script (\d+)ms', x)[0]) if re.findall(r'script (\d+)ms', x) else 0)[:15]:
        print(l)
    for l in scripts:
        if 'terminados' in l: print(l)
    if not loads and not frames:
        print('No hay lineas [web152]: compila con EXTRA_CFLAGS=-DSOMA_WEB_PERF_LOG=1')
    print('captura: build/web_bench.ppm')
    return 0


if __name__ == '__main__':
    sys.exit(main())
