#!/usr/bin/env python3
"""SOMA_265_CC: prueba de la orden `cc` (TinyCC en ring 3) en QEMU, con el escritorio en marcha.

Arranca build/x86_64/soma.iso, escribe unos programas en C en /workspace (a traves de Python: en las ordenes de fw_cfg ';' separa
ordenes, asi que el C se escribe con '@' en vez de ';', '$' en vez de '%' y '`' en vez de la barra invertida, y un script de Python
los traduce), los compila y los ejecuta con `cc`, y comprueba lo que sale por la consola serie. Necesita qemu-system-x86_64 y la
ISO ya construida (funciona tambien en macOS):

    make ARCH=x86_64 PLATFORM=qemu_virt
    python3 tests/prueba_cc_265.py            (devuelve 0 si todo sale bien; VER=1 muestra la salida si falla)
"""
import os, re, subprocess, sys, tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(RAIZ, 'build', 'x86_64', 'soma.iso')

PROGRAMAS = {
    'hola.c': r'''#include <stdio.h>
int main(int argc, char **argv)
{
    printf("hola desde C: %d", argc);
    for (int i = 1; i < argc; i++) printf(" %s", argv[i]);
    printf("\n");
    return 7;
}
''',
    'num.c': r'''#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
int main(void)
{
    int v[] = { 5, 3, 9, 1 };
    qsort(v, 4, sizeof v[0], cmp);
    char *s = malloc(64);
    snprintf(s, 64, "%d%d%d%d", v[0], v[1], v[2], v[3]);
    printf("numeros %.3f %g %s %ld %.2e\n", sqrt(2.0), 1e21, s, strlen(s) * 1000000000L, strtod("2.5e-3", 0));
    free(s);
    return 0;
}
''',
    'arch.c': r'''#include <stdio.h>
#include <string.h>
int main(void)
{
    FILE *f = fopen("salida.txt", "w");
    fprintf(f, "escrito desde C\n");
    fclose(f);
    char b[64] = "";
    f = fopen("/workspace/salida.txt", "r");
    fgets(b, sizeof b, f);
    fclose(f);
    printf("leido: %s", b);
    printf("fuera de workspace: %s\n", fopen("/etc/x.txt", "w") ? "PERMITIDO" : "denegado");
    printf("privado: %s\n", fopen("/etc/llm.conf", "r") ? "LEIDO" : "denegado");
    return 0;
}
''',
    'error.c': r'''#include <stdio.h>
int main(void)
{
    int x = 3
    return x;
}
''',
    'fallo.c': r'''#include <stdio.h>
int main(void)
{
    volatile int *p = 0;
    printf("antes del fallo\n");
    fflush(stdout);
    *p = 1;
    return 0;
}
''',
    'win.c': r'''#include <stdio.h>
#include <soma.h>
int main(void)
{
    if (!soma_win_create("Prueba C", 300, 200)) { printf("sin escritorio\n"); return 1; }
    int w = 0, h = 0;
    soma_win_size(&w, &h);
    printf("ventana C activa %d tamano %d\n", soma_win_active(), w > 100 && h > 50);
    for (int f = 0; f < 10; f++) {
        soma_win_clear(0x101828);
        soma_win_rect(10, 10, 50, 50, 0xFF0000);
        soma_win_line(0, 0, 100000, 100000, 0x00FF00);
        soma_win_circle(100, 100, 20, 0x0000FF);
        soma_win_text(10, 80, "hola", 0xFFFFFF);
        soma_win_present();
        soma_sleep(30);
    }
    struct soma_mouse m = soma_win_mouse();
    printf("raton %d tecla %d\n", m.clicked == 0 || m.clicked == 1, soma_win_key());
    soma_win_close();
    soma_sleep(300);
    printf("cerrada %d\n", !soma_win_active());
    return 0;
}
''',
}


def a_python(nombre, c):
    """script de Python que escribe el programa en C (sin ';', '%' ni barras invertidas en la orden)"""
    t = c.replace(';', '@').replace('%', '$').replace('\\', '`')
    assert '"""' not in t
    py = 'T = """' + t + '"""\n' + f"open('/workspace/{nombre}', 'w').write(T.replace('@', chr(59)).replace('$', chr(37)).replace('`', chr(92)))\n"
    return py.replace('\n', '\\n').replace('"', '\\"').replace(',', ',,')


ORDENES = ['@4000'] + [x for n, c in PROGRAMAS.items() for x in (f'write /workspace/g.py "{a_python(n, c)}"', 'python g.py')] + [
    'cc',
    'cc -run hola.c uno dos',
    'cc hola.c',
    'cc -run hola.elf tres',
    'cc -run num.c',
    'cc -run arch.c',
    'cat /workspace/salida.txt',
    'cc error.c',
    'cc -run fallo.c',
    'cc -run win.c',
    'cc -run noexiste.c',
    'procs',
    '@500',
]

ESPERADO = [   # (descripcion, expresion regular sobre la salida)
    ('cc sin nada explica el uso', r'Compilador de C \(TinyCC\)'),
    ('cc -run compila y ejecuta con argumentos', r'hola desde C: 3 uno dos'),
    ('codigo de salida de main', r'cc: el programa termino con codigo 7'),
    ('cc archivo.c deja el ejecutable', r'cc: /workspace/hola\.elf'),
    ('cc -run de un .elf ya compilado', r'hola desde C: 2 tres'),
    ('printf, strtod, math, qsort, malloc', r'numeros 1\.414 1e\+21 1359 4000000000 2\.50e-03'),
    ('archivos en /workspace', r'leido: escrito desde C'),
    ('no escribe fuera de /workspace', r'fuera de workspace: denegado'),
    ('no lee rutas privadas', r'privado: denegado'),
    ('el archivo queda en el VFS', r'(?m)^escrito desde C'),
    ('error de compilacion con archivo y linea', r'error\.c:5: error: '),
    ('un fallo mata solo al programa', r'antes del fallo[\s\S]*cc: el programa termino \(fallo de la CPU'),
    ('ventana con soma.h', r'ventana C activa 1 tamano 1'),
    ('raton y teclado de la ventana', r'raton 1 tecla 0'),
    ('soma_win_close la cierra', r'cerrada 1'),
    ('archivo que no existe', r'no se puede leer /workspace/noexiste\.c'),
]


def main():
    if not os.path.exists(ISO):
        print('falta', ISO, '(make ARCH=x86_64 PLATFORM=qemu_virt)')
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
            subprocess.run(a, timeout=int(os.environ.get('PLAZO', '150')), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            pass
        sal = open(raw, 'rb').read().decode('utf-8', 'replace').replace('\r', '')
        sal = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', sal)
    fallos = 0
    for desc, rx in ESPERADO:
        ok = re.search(rx, sal) is not None
        fallos += not ok
        print(('  ok   ' if ok else '  FALLO'), desc)
    print(f'prueba_cc_265: {len(ESPERADO) - fallos}/{len(ESPERADO)} bien')
    if fallos and os.environ.get('VER'):
        print(sal[sal.find('[CMD] cc'):] if '[CMD] cc' in sal else sal[-6000:])
    return 1 if fallos else 0


if __name__ == '__main__':
    sys.exit(main())
