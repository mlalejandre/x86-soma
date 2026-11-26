#!/usr/bin/env python3
"""SOMA_332_HORA: zona horaria de punta a punta en QEMU, con el reloj del equipo fijado.

    make ARCH=x86_64 && python3 tests/prueba_hora_332.py

El RTC de QEMU arranca el 2026-11-25 a las 18:30 UTC (invierno: en Madrid son las 19:30) y el 2026-07-01 a las 10:00 UTC
(verano: las 12:00). Comprueba la zona por defecto, el cambio de zona por ciudad, por nombre IANA y por desfase, que se
guarda en /etc/hora.conf, el reloj del equipo en hora local (como Windows) y que el programador ve la hora local.
"""
import os, re, subprocess, sys, tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')


def arranca(rtc, ordenes, hdd):
    raw = hdd + '.serie'
    a = ['qemu-system-x86_64', '-machine', 'pc', '-m', '1G', '-cdrom', ISO, '-boot', 'd',
         '-drive', f'file={hdd},format=raw,if=virtio', '-rtc', f'base={rtc},clock=vm',
         '-fw_cfg', 'name=opt/soma/cmds,string=' + ';'.join(ordenes),
         '-serial', f'file:{raw}', '-display', 'none', '-no-reboot']
    try:
        subprocess.run(a, timeout=int(os.environ.get('PLAZO', '60')), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        pass
    sal = open(raw, 'rb').read().decode('utf-8', 'replace').replace('\r', '')
    return re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', sal)


def bloque(sal, orden, n=0):
    partes = sal.split('[CMD] ' + orden + '\n')
    return partes[n + 1].split('[CMD-FIN]')[0] if len(partes) > n + 1 else ''


def main():
    if not os.path.exists(ISO):
        print('falta', ISO, '(make ARCH=x86_64)')
        return 2
    fallos = 0
    with tempfile.TemporaryDirectory() as d:
        hdd = os.path.join(d, 'hdd.img')
        with open(hdd, 'wb') as f:
            f.truncate(256 << 20)
        s1 = arranca('2026-11-25T18:30:00', [
            'big format yes', 'date', 'hora',
            'hora zona nueva york', 'date',
            'hora zona Asia/Kolkata', 'date',
            'hora zona UTC-3', 'date',
            'hora zona atlantida',
            'hora zona Bogot\u00e1', 'date',
            'hora reloj local', 'hora',
            'hora reloj utc', 'hora zona madrid', 'cat /etc/hora.conf',
            'agenda hoy',
        ], hdd)
        s2 = arranca('2026-07-01T10:00:00', ['date', 'hora zona sidney', 'date', 'hora zona madrid'], hdd)
        s3 = arranca('2026-11-25T18:30:00', ['hora zona canarias', '@200'], hdd)
        s4 = arranca('2026-11-25T18:30:00', ['date'], hdd)       # se guardo canarias: sigue al arrancar otra vez

    esperado = [
        (s1, 'por defecto, Espana peninsular en invierno', 'date', 0, r'2026-11-25 19:3\d:\d\d UTC\+01:00 \(Espana \(Madrid\)'),
        (s1, 'la orden hora', 'hora', 0, r'hora local: 2026-11-25 19:3\d[\s\S]*UTC:\s+2026-11-25 18:3\d[\s\S]*guarda UTC'),
        (s1, 'Nueva York por la ciudad', 'date', 1, r'2026-11-25 13:3\d:\d\d UTC-05:00 \(EE\. UU\. este'),
        (s1, 'India por el nombre IANA (media hora)', 'date', 2, r'2026-11-26 00:0\d:\d\d UTC\+05:30'),
        (s1, 'desfase fijo', 'date', 3, r'2026-11-25 15:3\d:\d\d UTC-03:00 \(UTC-03:00'),
        (s1, 'zona desconocida', 'hora zona atlantida', 0, r'no conozco esa zona'),
        (s1, 'con tilde', 'date', 4, r'2026-11-25 13:3\d:\d\d UTC-05:00 \(Colombia'),
        (s1, 'reloj del equipo en hora local (Windows)', 'hora', 1, r'hora local: 2026-11-25 18:3\d[\s\S]*UTC:\s+2026-11-25 23:3\d[\s\S]*hora local \(como Windows\)'),
        (s1, 'se guarda', 'cat /etc/hora.conf', 0, r'zona=Europe/Madrid\s*\nreloj=utc'),
        (s2, 'verano', 'date', 0, r'2026-07-01 12:0\d:\d\d UTC\+02:00 \(Espana \(Madrid\), horario de verano\)'),
        (s2, 'hemisferio sur en invierno', 'date', 1, r'2026-07-01 20:0\d:\d\d UTC\+10:00 \(Australia este'),
        (s4, 'la zona se conserva al reiniciar', 'date', 0, r'2026-11-25 18:3\d:\d\d UTC \(Canarias'),
    ]
    for sal, nombre, orden, n, patron in esperado:
        texto = bloque(sal, orden, n)
        ok = re.search(patron, texto) is not None
        print(('OK   ' if ok else 'MAL  ') + nombre)
        if not ok:
            fallos += 1
            print('     ' + (texto.strip().replace('\n', '\n     ') or '(sin salida)'))
    print('fallos:', fallos)
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
