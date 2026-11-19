#!/usr/bin/env python3
"""SOMA 153/153B: compara el coste de los fotogramas del escritorio ANTES y DESPUES de las mejoras del orbe
(153: no dibujarlo bajo una ventana opaca; 153B: cintas a media resolucion).

La build de medicion alterna sola los dos modos cada 6 s en el mismo arranque (misma maquina, mismo momento) e imprime
[desk153]. Este script arranca QEMU con el navegador abierto, recoge esas lineas y resume la diferencia.

  make clean && make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS='-DSOMA_WEB_ASYNC_IMAGES=1 -DSOMA_WEB_PERF_LOG=1 -DSOMA_GUI_BG_MS=33'
  python3 tools/desk_bench.py                       # 60 s con about:blank
  python3 tools/desk_bench.py --seconds 90 https://www.20minutos.es

(-DSOMA_GUI_BG_MS=33 pone el fondo a 30 fps para que haya muchos fotogramas que medir.)
"""
from __future__ import annotations
import argparse, os, re, subprocess, sys, tempfile, threading, time
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('url', nargs='?', default='about:blank')
    ap.add_argument('--seconds', type=int, default=60)
    ap.add_argument('--iso', default='build/soma.iso')
    a = ap.parse_args()
    if not Path(a.iso).is_file():
        print('Falta', a.iso, '(compila antes con make)', file=sys.stderr); return 1
    cmd = ['qemu-system-x86_64', '-machine', 'pc', '-m', '512M', '-no-reboot', '-cdrom', a.iso,
           '-netdev', 'user,id=net0', '-device', 'virtio-net-pci,netdev=net0',
           '-fw_cfg', f'name=opt/soma/openurl,string={a.url}', '-serial', 'stdio', '-display', 'none']
    if Path('hdd.img').is_file():
        cmd += ['-drive', 'file=hdd.img,format=raw,if=virtio,snapshot=on']
    q = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    lines: list[str] = []
    threading.Thread(target=lambda: [lines.append(l.decode(errors='replace').rstrip()) for l in q.stdout], daemon=True).start()
    try:
        time.sleep(a.seconds)
    finally:
        q.terminate()
        try: q.wait(5)
        except subprocess.TimeoutExpired: q.kill()
    res = {0: [], 1: []}; rib = {0: [], 1: []}
    for l in lines:
        m = re.search(r'\[desk153\] ocultar_orbe=(\d) fotogramas=(\d+) coste_medio_kciclos=(\d+)(?: cintas_kciclos=(\d+))?', l)
        if m and int(m.group(2)) > 20:
            res[int(m.group(1))].append(int(m.group(3)))
            if m.group(4): rib[int(m.group(1))].append(int(m.group(4)))
    if not res[0] or not res[1]:
        print('No hay suficientes lineas [desk153]. Compila con -DSOMA_WEB_PERF_LOG=1 -DSOMA_GUI_BG_MS=33 y deja correr mas tiempo.')
        return 1
    off = sum(res[0]) / len(res[0]); on = sum(res[1]) / len(res[1])
    print(f'fotograma del escritorio ANTES  : {off / 1024:8.1f} Mciclos  ({len(res[0])} tandas de 6 s)')
    print(f'fotograma del escritorio DESPUES: {on / 1024:8.1f} Mciclos  ({len(res[1])} tandas de 6 s)')
    print(f'ahorro por fotograma: {100 * (off - on) / off:.0f} %')
    if rib[0] and rib[1]:
        ro = sum(rib[0]) / len(rib[0]); rn = sum(rib[1]) / len(rib[1])
        print(f'  solo las cintas: {ro / 1024:.1f} -> {rn / 1024:.1f} Mciclos ({100 * (ro - rn) / ro:.0f} % menos)')
    print('(con la ventana translucida el orbe no se oculta: el ahorro es solo el de las cintas, y es lo correcto)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
