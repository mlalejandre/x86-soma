#!/usr/bin/env python3
"""SOMA_267_DISCOS: el exFAT de SOMA comparado con otras implementaciones.

  python3 tests/test_exfat_267.py [--rapido]

1. Compila tests/test_exfat_267.c con src/core/exfat.c (con ASan/UBSan si se puede) y pasa sus pruebas en memoria.
2. Si hay mkfs.exfat y fsck.exfat (exfatprogs):
   - imagenes de mkfs.exfat con varios tamanos de cluster: SOMA hace cientos de operaciones al azar y
     fsck.exfat -n tiene que decir que el volumen esta limpio;
   - lo que SOMA formatea con exfat_format tambien tiene que pasar fsck.exfat.
3. Si hay una segunda implementacion (EXREF=ruta a un programa "dump IMG" / "put IMG CARPETA", p. ej. hecho con libexfat):
   - lo que escribe SOMA lo lee igual la otra implementacion (mismos archivos, tamanos y huellas);
   - lo que escribe la otra lo lee igual SOMA.
4. tools/soma_disco.py: su exFAT (el de construyeimagen.py) pasa fsck.exfat y SOMA lo lee igual.
Las herramientas que no esten se dicen y esas comprobaciones se saltan (nunca cuentan como bien).
"""
import os, random, shutil, subprocess, sys, tempfile, hashlib

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAPIDO = '--rapido' in sys.argv
ok = fallos = saltadas = 0


def ck(cond, msg):
    global ok, fallos
    if cond:
        ok += 1
    else:
        fallos += 1
        print('FALLO:', msg)


def salta(msg):
    global saltadas
    saltadas += 1
    print('(se salta)', msg)


def herramienta(nombre, *extra):
    for d in list(extra) + os.environ.get('PATH', '').split(os.pathsep) + ['/sbin', '/usr/sbin', '/usr/local/sbin']:
        p = os.path.join(d, nombre)
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return None


def fnv(b):
    h = 1469598103934665603
    for x in b:
        h ^= x
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return '%016x' % h


def compila(tmp):
    cc = os.environ.get('CC') or shutil.which('gcc') or shutil.which('cc') or shutil.which('clang')
    if not cc:
        return None
    out = os.path.join(tmp, 'test_exfat_267')
    base = [cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            '-I', os.path.join(RAIZ, 'src/core'), os.path.join(RAIZ, 'tests/test_exfat_267.c'),
            os.path.join(RAIZ, 'src/core/exfat.c'), '-o', out]
    for san in (['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined'], []):
        r = subprocess.run(base[:1] + san + base[1:], capture_output=True, text=True)
        if r.returncode == 0:
            return out
        if not san:
            print(r.stderr[-3000:])
    return None


def ejecuta(prog, *args, entrada=None):
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    return subprocess.run([prog] + list(args), capture_output=True, text=True, env=env, input=entrada)


def listado(txt):
    return sorted(l.strip() for l in txt.splitlines() if l.startswith(('D ', 'F ')))


def fsck_limpio(fsck, img):
    r = subprocess.run([fsck, '-n', img], capture_output=True, text=True)
    return r.returncode == 0 and 'clean' in r.stdout, (r.stdout + r.stderr)[-600:]


def arbol_azar(raiz, rnd, n):
    """carpeta del anfitrion con nombres raros, para que la escriba la otra implementacion"""
    nombres = ['árbol', 'Ñandú', 'música.mp3', 'Документ.txt', '日本語.txt', '😀.png', 'README', 'a b c', 'x' * 200]
    hechos = []
    for i in range(n):
        d = raiz
        for _ in range(rnd.randint(0, 2)):
            d = os.path.join(d, rnd.choice(nombres) + str(rnd.randint(0, 3)))
            if os.path.isfile(d):
                break
            os.makedirs(d, exist_ok=True)
        if os.path.isfile(d):
            continue
        p = os.path.join(d, rnd.choice(nombres) + '_%d' % i)
        if os.path.isdir(p):
            continue
        sz = rnd.choice([0, 1, 511, 4096, 4097, rnd.randint(0, 300000)])
        with open(p, 'wb') as f:
            f.write(rnd.randbytes(sz))
        hechos.append(p)
    return hechos


def main():
    tmp = tempfile.mkdtemp(prefix='exfat267_')
    try:
        prog = compila(tmp)
        ck(prog is not None, 'compilar tests/test_exfat_267.c')
        if not prog:
            return
        r = ejecuta(prog)
        print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-2000:])
        ck(r.returncode == 0, 'pruebas en memoria del exFAT:\n' + (r.stdout + r.stderr)[-3000:])

        mkfs = herramienta('mkfs.exfat', os.environ.get('EXFATPROGS', ''))
        fsck = herramienta('fsck.exfat', os.environ.get('EXFATPROGS', ''))
        exref = os.environ.get('EXREF') or herramienta('exref')
        if exref and not os.access(exref, os.X_OK):
            exref = None

        # ---- 2. imagenes de mkfs.exfat
        if not (mkfs and fsck):
            salta('no hay mkfs.exfat / fsck.exfat (exfatprogs): sin comparacion con otra implementacion')
        else:
            casos = [(32, '4K'), (64, '512'), (200, '32K'), (130, '128K'), (600, '64K')]
            if RAPIDO:
                casos = casos[:3]
            for i, (mib, cl) in enumerate(casos):
                img = os.path.join(tmp, 'mk%d.img' % i)
                with open(img, 'wb') as f:
                    f.truncate(mib << 20)
                subprocess.run([mkfs, '-q', '-c', cl, '-L', 'MK', img], check=True, capture_output=True)
                n = '300' if RAPIDO else '900'
                r = ejecuta(prog, 'ops', img, str(1000 + i), n)
                ck(r.returncode == 0, 'ops en mkfs.exfat %d MiB cluster %s: %s' % (mib, cl, r.stderr[-800:]))
                limpio, txt = fsck_limpio(fsck, img)
                ck(limpio, 'fsck.exfat tras las operaciones de SOMA (%d MiB, cluster %s):\n%s' % (mib, cl, txt))
                esperado = listado(r.stdout)
                d = ejecuta(prog, 'dump', img)
                ck(listado(d.stdout) == esperado, 'SOMA relee lo que escribio (%d MiB)' % mib)
                if exref:
                    x = subprocess.run([exref, 'dump', img], capture_output=True, text=True)
                    ck(x.returncode == 0 and listado(x.stdout) == esperado,
                       'la otra implementacion lee lo mismo que escribio SOMA (%d MiB, cluster %s): %d/%d entradas' %
                       (mib, cl, len(listado(x.stdout)), len(esperado)))
            img = os.path.join(tmp, 'diminuto.img')
            r = ejecuta(prog, 'format', img, '3', 'SOMA')
            ck(r.returncode != 0, 'exfat_format rechaza un volumen de menos de 4 MiB')
            os.unlink(img)
            for mib in ([12, 700] if RAPIDO else [5, 12, 100, 700, 2100]):
                img = os.path.join(tmp, 'fmt%d.img' % mib)
                r = ejecuta(prog, 'format', img, str(mib), 'SOMA')
                ck(r.returncode == 0, 'exfat_format %d MiB: %s' % (mib, r.stdout))
                limpio, txt = fsck_limpio(fsck, img)
                ck(limpio, 'fsck.exfat de un volumen formateado por SOMA (%d MiB):\n%s' % (mib, txt))
                r = ejecuta(prog, 'ops', img, str(mib), '200')
                limpio, txt = fsck_limpio(fsck, img)
                ck(r.returncode == 0 and limpio, 'operaciones en lo formateado por SOMA (%d MiB):\n%s' % (mib, txt))
                os.unlink(img)

        # ---- 3. lo que escribe la otra implementacion lo lee SOMA
        if not (exref and mkfs):
            salta('no hay segunda implementacion (EXREF): sin lectura cruzada')
        else:
            rnd = random.Random(267)
            for k in range(2 if RAPIDO else 5):
                src = os.path.join(tmp, 'arbol%d' % k)
                os.makedirs(src)
                arbol_azar(src, rnd, 60 if RAPIDO else 150)
                img = os.path.join(tmp, 'x%d.img' % k)
                with open(img, 'wb') as f:
                    f.truncate(128 << 20)
                subprocess.run([mkfs, '-q', '-c', rnd.choice(['4K', '32K']), img], check=True, capture_output=True)
                p = subprocess.run([exref, 'put', img, src], capture_output=True, text=True)
                if p.returncode:
                    salta('exref put fallo: ' + p.stderr[-300:])
                    continue
                esperado = []
                for dp, dn, fn in os.walk(src):
                    rel = dp[len(src):]
                    for d in dn:
                        esperado.append('D %s/%s' % (rel, d))
                    for f in fn:
                        b = open(os.path.join(dp, f), 'rb').read()
                        esperado.append('F %s/%s %d %s' % (rel, f, len(b), fnv(b)))
                d = ejecuta(prog, 'dump', img)
                ck(sorted(esperado) == listado(d.stdout), 'SOMA lee lo que escribio la otra implementacion (%d entradas)' % len(esperado))
                shutil.rmtree(src)
                os.unlink(img)

        # ---- 4. el exFAT de tools/soma_disco.py
        sd = os.path.join(RAIZ, 'tools', 'soma_disco.py')
        if not os.path.exists(sd):
            salta('no hay tools/soma_disco.py')
        else:
            sys.path.insert(0, os.path.join(RAIZ, 'tools'))
            import soma_disco
            rnd = random.Random(2670)
            for k, mib in enumerate([4, 40, 300] if RAPIDO else [4, 40, 300, 1100]):
                src = os.path.join(tmp, 'py%d' % k)
                os.makedirs(src)
                arbol_azar(src, rnd, 40 if RAPIDO else 120)
                for sub in soma_disco.CARPETAS_USUARIO:
                    os.makedirs(os.path.join(src, sub), exist_ok=True)
                img = os.path.join(tmp, 'py%d.img' % k)
                with open(img, 'wb') as f:
                    f.truncate(mib << 20)
                with open(img, 'r+b') as f:
                    dev = soma_disco.Disco(f, mib << 11)
                    try:
                        soma_disco.exfat_formatear(dev, 0, mib << 11, 'USUARIO', serie=0x267)
                        vol = soma_disco.ExFat(dev, 0, mib << 11)
                        vol.copia_arbol(src, '/')
                        vol.cierra()
                        hecho = True
                    except soma_disco.SinSitio:
                        hecho = False
                if not hecho:
                    ck(mib == 4, 'soma_disco: sin sitio en %d MiB' % mib)
                    continue
                esperado = []
                for dp, dn, fn in os.walk(src):
                    rel = dp[len(src):]
                    esperado += ['D %s/%s' % (rel, d) for d in dn]
                    esperado += ['F %s/%s %d %s' % (rel, f, os.path.getsize(os.path.join(dp, f)), fnv(open(os.path.join(dp, f), 'rb').read())) for f in fn]
                d = ejecuta(prog, 'dump', img)
                ck(sorted(esperado) == listado(d.stdout), 'SOMA lee el exFAT de soma_disco.py (%d MiB, %d entradas)' % (mib, len(esperado)))
                if fsck:
                    limpio, txt = fsck_limpio(fsck, img)
                    ck(limpio, 'fsck.exfat del exFAT de soma_disco.py (%d MiB):\n%s' % (mib, txt))
                if exref:
                    x = subprocess.run([exref, 'dump', img], capture_output=True, text=True)
                    ck(listado(x.stdout) == sorted(esperado), 'la otra implementacion lee el exFAT de soma_disco.py (%d MiB)' % mib)
                os.unlink(img)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    main()
    print('%s exFAT cruzado: %d bien, %d fallos, %d saltadas' % ('FALLO' if fallos else 'ok ', ok, fallos, saltadas))
    sys.exit(1 if fallos else 0)
