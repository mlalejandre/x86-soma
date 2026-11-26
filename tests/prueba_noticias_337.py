#!/usr/bin/env python3
"""SOMA_337_NOTICIAS: la orden noticias y su configuracion en QEMU (sin red de verdad).

    make ARCH=x86_64 && python3 tests/prueba_noticias_337.py
"""
import os, re, subprocess, sys, tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')
ORDENES = ['@2500', 'noticias categorias portada,deportes,salud', 'noticias lugar Alcala de Henares',
           'noticias fuente anadir https://feeds.example.com/rss.xml', 'noticias fuente anadir ftp://mal', 'noticias fuente',
           'cat /etc/noticias.conf', 'noticias deportes', 'noticias local', 'noticias xyz', 'noticias',
           'write /workspace/canal.xml "<rss><channel><title>Canal SOMA</title><item><title>Primera noticia - Medio Uno</title>'
           '<link>https://ejemplo.es/1</link><pubDate>Tue, 25 Nov 2025 18:00:00 GMT</pubDate></item><item><title><![CDATA[Segunda <b>especial</b>]]>'
           '</title><link>javascript:alert(1)</link><description><![CDATA[<p>Hola</p> mundo]]></description></item></channel></rss>"',
           'noticias leer /workspace/canal.xml', 'uptime']


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
            subprocess.run(a, timeout=int(os.environ.get('PLAZO', '120')), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            pass
        sal = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', open(raw, 'rb').read().decode('utf-8', 'replace').replace('\r', ''))

    def bloque(orden, n=0):
        partes = sal.split('[CMD] ' + orden + '\n')
        return partes[n + 1].split('[CMD-FIN]')[0] if len(partes) > n + 1 else ''

    esperado = [
        ('categorias', 'noticias categorias portada,deportes,salud', r'Categorias guardadas'),
        ('lugar (y activa Local)', 'noticias lugar Alcala de Henares', r'Local buscara noticias de Alcala de Henares'),
        ('canal propio', 'noticias fuente anadir https://feeds.example.com/rss.xml', r'Canal anadido'),
        ('canal que no es web', 'noticias fuente anadir ftp://mal', r'empieza por http'),
        ('lista de canales', 'noticias fuente', r'1\. https://feeds\.example\.com/rss\.xml'),
        ('se guarda', 'cat /etc/noticias.conf', r'categorias=portada,local,salud,deportes[\s\S]*lugar=Alcala de Henares[\s\S]*fuente=https://feeds\.example\.com/rss\.xml'),
        ('sin red: lo dice', 'noticias deportes', r'Deportes: (Sin conexion a Internet|No se pudo descargar)'),
        ('local sin red: lo dice', 'noticias local', r'Local: (Sin conexion a Internet|No se pudo descargar)'),
        ('categoria desconocida', 'noticias xyz', r'categoria desconocida'),
        ('estado', 'noticias', r'Categorias activas: portada local salud deportes[\s\S]*Lugar \(para Local\): Alcala de Henares'),
        ('un canal se lee en ring 3', 'noticias leer /workspace/canal.xml', r'Leidos \(en ring 3\): 2\n  1\. Primera noticia \| Medio Uno \| \d\d/11 \| https://ejemplo\.es/1\n  2\. Segunda especial \| Canal SOMA \| sin fecha \| sin enlace'),
        ('el sistema sigue', 'uptime', r'Uptime'),
    ]
    fallos = 0
    for nombre, orden, n_patron in [(e[0], e[1], e[2]) for e in esperado]:
        texto = bloque(orden)
        ok = re.search(n_patron, texto) is not None
        print(('OK   ' if ok else 'MAL  ') + nombre)
        if not ok:
            fallos += 1
            print('     ' + (texto.strip().replace('\n', '\n     ') or '(sin salida)'))
    print('fallos:', fallos)
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
