#!/usr/bin/env python3
"""SOMA_329_IA_LOCAL: el motor de IA local (llama.cpp en ring 3) de punta a punta en QEMU, con modelos diminutos de prueba.

    make ARCH=x86_64 && python3 tests/prueba_ia_329.py

Genera los modelos (tools/ia_modelos_prueba.py, pesos con semilla fija), arranca SOMA con 4 procesadores y una lista de ordenes,
y compara con lo que da el mismo codigo de llama.cpp en el anfitrion (los numeros esperados de abajo salen de `make ia-host`):
  - instalar modelos (fw_cfg) y leerlos desde ring 3 con su huella exacta;
  - tokenizador WordPiece y SentencePiece, embeddings (BERT) y generacion voraz (llama);
  - un modelo roto: llama.cpp lanza una excepcion de C++, el motor la recoge, lo dice y sigue vivo;
  - la memoria calcula sus vectores con el motor local (sin servidor);
  - el proveedor "En SOMA" del LLM lista los modelos del almacen;
  - ia parar libera la memoria.
"""
import os, re, subprocess, sys, tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')
sys.path.insert(0, os.path.join(RAIZ, 'tools'))
import ia_modelos_prueba  # noqa: E402


def fnv(ruta):
    h = 1469598103934665603
    for b in open(ruta, 'rb').read():
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return '%016x' % h


ORDENES = [
    'big format yes',
    'ia instalar emb-prueba', 'ia instalar llm-prueba',
    'ia modelos',
    'ia huella emb-prueba', 'ia huella llm-prueba',
    'ia tokens emb-prueba hola mundo que tal',
    'ia embed emb-prueba hola mundo que tal',
    'ia tokens llm-prueba hola mundo que tal',
    'ia completa llm-prueba hola',
    'big write /modelos/roto.gguf GGUFbasura',
    'ia completa roto x',
    'ia estado',
    'ia completa llm-prueba hola',
    'memoria config emb_modelo emb-prueba', 'memoria config emb_motor local', 'memoria probar',
    'llm-config proveedor soma', 'llm-config modelos',
    'ia parar', 'ia estado',
    '@300',
]


def main():
    if not os.path.exists(ISO):
        print('falta', ISO, '(make ARCH=x86_64)')
        return 2
    with tempfile.TemporaryDirectory() as d:
        ia_modelos_prueba.emb(os.path.join(d, 'emb-prueba.gguf'))
        ia_modelos_prueba.llm(os.path.join(d, 'llm-prueba.gguf'))
        huella_emb, huella_llm = fnv(os.path.join(d, 'emb-prueba.gguf')), fnv(os.path.join(d, 'llm-prueba.gguf'))
        hdd, raw = os.path.join(d, 'hdd.img'), os.path.join(d, 'serie.txt')
        with open(hdd, 'wb') as f:
            f.truncate(256 << 20)
        a = ['qemu-system-x86_64', '-machine', 'pc', '-m', '2G', '-smp', '4', '-cpu', 'max', '-cdrom', ISO,
             '-drive', f'file={hdd},format=raw,if=virtio', '-fw_cfg', 'name=opt/soma/cmds,string=' + ';'.join(ORDENES),
             '-fw_cfg', f'name=opt/soma/ia/emb-prueba,file={d}/emb-prueba.gguf', '-fw_cfg', f'name=opt/soma/ia/llm-prueba,file={d}/llm-prueba.gguf',
             '-serial', f'file:{raw}', '-display', 'none', '-no-reboot']
        try:
            subprocess.run(a, timeout=int(os.environ.get('PLAZO', '170')), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            pass
        sal = open(raw, 'rb').read().decode('utf-8', 'replace').replace('\r', '')
        sal = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', sal)

    def bloque(orden, n=0):
        partes = sal.split('[CMD] ' + orden + '\n')
        return partes[n + 1].split('[CMD-FIN]')[0] if len(partes) > n + 1 else ''

    esperado = [
        ('instalar los modelos desde fw_cfg', 'ia instalar emb-prueba', 0, r'modelo instalado en /modelos/emb-prueba\.gguf'),
        ('el almacen los lista', 'ia modelos', 0, r'/modelos/llm-prueba\.gguf'),
        ('ring 3 lee el modelo BERT exacto (huella)', 'ia huella emb-prueba', 0, huella_emb + ' 103040'),
        ('ring 3 lee el modelo llama exacto (huella)', 'ia huella llm-prueba', 0, huella_llm + ' 172576'),
        ('tokenizador WordPiece', 'ia tokens emb-prueba hola mundo que tal', 0, r'^45 46 47 48$'),
        ('embedding como llama.cpp en el anfitrion', 'ia embed emb-prueba hola mundo que tal', 0,
         r'"embedding":\[0\.196029,-0\.262221,-0\.133193,0\.140399,-0\.230182,0\.059664,'),
        ('tokenizador SentencePiece (con bytes)', 'ia tokens llm-prueba hola mundo que tal', 0,
         r'^315 296 303 300 289 315 301 309 302 292 303 315 305 309 293 315 308 289 300$'),
        ('generacion voraz como llama.cpp en el anfitrion', 'ia completa llm-prueba hola', 0,
         r'"content":" es\\u0016t en1.*"usage":\{"prompt_tokens":6,"completion_tokens":8'),
        ('modelo roto: la excepcion de llama.cpp llega como error', 'ia completa roto x', 0, r'ia: .*(error loading model|failed)'),
        ('y el motor sigue vivo (no se reinicia)', 'ia estado', 0, r'en marcha.*\n.*hilo.*\n  ordenes: 7, fallidas: 1'),
        ('genera igual despues del error', 'ia completa llm-prueba hola', 1, r'"content":" es\\u0016t en1'),
        ('la memoria usa el motor local', 'memoria probar', 0, r'Motor local de embeddings OK: 32 dimensiones'),
        ('proveedor "En SOMA": modelos del almacen', 'llm-config modelos', 0, r'emb-prueba[\s\S]*llm-prueba'),
        ('ia parar libera la memoria', 'ia estado', 1, r'parado'),
    ]
    fallos = 0
    for desc, orden, n, rx in esperado:
        b = bloque(orden, n)
        ok = re.search(rx, b, re.M) is not None
        fallos += not ok
        print(('  ok   ' if ok else '  FALLO'), desc)
        if not ok and os.environ.get('VER'):
            print('       ', repr(b[:400]))
    hilos = re.search(r'ms\), (\d+) hilos?', bloque('ia estado', 0))
    print('  (calculo con %s hilo(s))' % (hilos.group(1) if hilos else '?'))
    if '[USER] proceso terminado por excepcion' in sal or 'error interno' in sal:
        fallos += 1
        print('  FALLO el proceso del motor murio por un fallo')
    total = len(esperado)
    print(f'prueba_ia_329: {total - fallos}/{total} bien')
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
