/* SOMA_265_CC: el compilador de C de SOMA (orden "cc"). Es TinyCC (third_party/tinycc, LGPL 2.1) con este programa delante,
 * compilado como un programa de ring 3: si algo falla al compilar, falla este proceso, nunca el nucleo.
 *
 *   cc hola.c                  compila y enlaza /workspace/hola.c -> /workspace/hola.elf
 *   cc -o juego.elf a.c b.c    varios archivos, salida elegida
 *   cc -c hola.c               solo compila -> hola.o
 *   cc -run hola.c [args]      compila a /workspace/.cc_run.elf (el nucleo lo ejecuta despues; ver shell_procesos.c)
 *   cc -s                      programador: comprueba el codigo que le pasa el nucleo (@programa.c), sin escribir nada
 *   cc -x                      programador: lo compila a /workspace/.programa.elf para probarlo
 *   opciones: -w (sin avisos), -DNOMBRE[=v], -Iruta, -heap N (MiB de memoria dinamica del programa: 8 si no se dice; los
 *             programas tienen 12 MiB en total para codigo, datos, memoria dinamica y 1 MiB de pila), -O -Wall -g -std=... (se ignoran)
 *
 * Las cabeceras y la biblioteca de C (src/user/cc/libc.c, la misma con la que esta hecho este programa) van dentro (cc_fs.inc,
 * en /tcc/...). Cada programa se enlaza con ella compilada en el momento: sin archivos de biblioteca que instalar. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "libtcc.h"

struct __fs_ent { const char *path; const unsigned char *data; unsigned long len; };
#include "cc_fs.inc"

#define TEXT_BASE "0x8000000000"                     /* USER_BASE de SOMA (src/arch/x86_64/usermode.h) */

static int g_errores;
static void error_cb(void *op, const char *msg)
{
    (void)op;
    for (const char *p = msg; *p; ++p) {                                  /* el codigo inyectado se llama programa.c al contarlo */
        if (*p == '@' && !strncmp(p, "@programa.c", 11)) continue;
        fputc(*p, stderr);
    }
    fputc('\n', stderr);
    if (strstr(msg, "error")) g_errores++;
}

static const char *const RUNTIME[] = { "/tcc/lib/libc.c", "/tcc/lib/libtcc1.c", "/tcc/lib/va_list.c", "/tcc/lib/builtin.c", "/tcc/lib/alloca.S",
                                       "/tcc/lib/stdatomic.c", "/tcc/lib/atomic.S", 0 };

/* paginas de 4 KiB que ocupan los segmentos del ELF (lo mismo que mira src/core/elf.c al cargarlo) */
#define MAX_PAGINAS 3072
static long paginas(const char *ruta)
{
    FILE *f = fopen(ruta, "rb");
    if (!f) return -1;
    unsigned char h[64]; long pag = 0;
    if (fread(h, 1, 64, f) == 64) {
        unsigned long phoff = *(unsigned long *)(h + 32); unsigned phnum = *(unsigned short *)(h + 56);
        for (unsigned i = 0; i < phnum && i < 16; ++i) {
            unsigned char p[56];
            if (fseek(f, (long)(phoff + i * 56u), SEEK_SET) || fread(p, 1, 56, f) != 56) break;
            if (*(unsigned *)p != 1) continue;
            unsigned long va = *(unsigned long *)(p + 16), msz = *(unsigned long *)(p + 40);
            if (msz) pag += (long)(((va + msz + 4095) >> 12) - (va >> 12));
        }
    }
    fclose(f);
    return pag;
}

static void uso(void)
{
    fputs("uso: cc archivo.c [mas.c] [-o salida.elf] [-c] [-w] [-Dnombre[=valor]] [-Iruta] [-heap MiB]\n"
          "     cc -run archivo.c [argumentos]   compila y ejecuta\n"
          "     el programa se ejecuta con: run salida.elf  (o directamente: cc -run archivo.c)\n", stderr);
}

static char *sin_ext(const char *p, const char *ext)
{
    size_t n = strlen(p);
    char *r = malloc(n + 8);
    strcpy(r, p);
    char *d = strrchr(r, '.'), *s = strrchr(r, '/');
    if (d && (!s || d > s)) *d = 0;
    strcat(r, ext);
    return r;
}

int main(int argc, char **argv)
{
    const char *fuentes[64]; int nf = 0;
    const char *salida = 0; int solo_c = 0, run = 0, check = 0, prueba = 0, sin_avisos = 0;
    const char *defs[32]; int nd = 0; const char *incs[16]; int ni = 0; long heap_mib = 8;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "-o") && i + 1 < argc) salida = argv[++i];
        else if (!strcmp(a, "-c")) solo_c = 1;
        else if (!strcmp(a, "-run")) { run = 1; if (i + 1 < argc && nf < 64) fuentes[nf++] = argv[++i]; break; }   /* lo demas son argumentos del programa */
        else if (!strcmp(a, "-s")) check = 1;
        else if (!strcmp(a, "-x")) prueba = 1;
        else if (!strcmp(a, "-w")) sin_avisos = 1;
        else if (!strcmp(a, "-heap") && i + 1 < argc) { heap_mib = strtol(argv[++i], 0, 10); if (heap_mib < 1 || heap_mib > 10) { fputs("cc: -heap va de 1 a 10 (MiB)\n", stderr); return 2; } }
        else if (!strncmp(a, "-D", 2) && nd < 32) defs[nd++] = a + 2;
        else if (!strncmp(a, "-I", 2) && ni < 16) incs[ni++] = a + 2;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { uso(); return 0; }
        else if (a[0] == '-') { /* -O2, -Wall, -g, -std=c99...: se aceptan y se ignoran */ }
        else if (nf < 64) fuentes[nf++] = a;
    }
    if (check || prueba) { nf = 0; fuentes[nf++] = "@programa.c"; }
    if (!nf) { uso(); return 2; }

    TCCState *s = tcc_new();
    if (!s) { fputs("cc: sin memoria\n", stderr); return 3; }
    tcc_set_error_func(s, 0, error_cb);
    tcc_set_lib_path(s, "/tcc");
    char opc[160];
    snprintf(opc, sizeof opc, "-nostdlib -nostdinc -static%s -Wl,-Ttext=" TEXT_BASE, sin_avisos ? " -w" : "");
    tcc_set_options(s, opc);
    tcc_add_sysinclude_path(s, "/tcc/include");
    tcc_add_include_path(s, "/workspace");
    for (int i = 0; i < ni; ++i) tcc_add_include_path(s, incs[i]);
    tcc_define_symbol(s, "__SOMA__", "1");
    for (int i = 0; i < nd; ++i) {
        char n[128]; const char *eq = strchr(defs[i], '=');
        size_t l = eq ? (size_t)(eq - defs[i]) : strlen(defs[i]); if (l >= sizeof n) l = sizeof n - 1;
        memcpy(n, defs[i], l); n[l] = 0;
        tcc_define_symbol(s, n, eq ? eq + 1 : "1");
    }
    int obj = solo_c || check;
    tcc_set_output_type(s, obj ? TCC_OUTPUT_OBJ : TCC_OUTPUT_EXE);
    for (int i = 0; i < nf && !g_errores; ++i) {
        if (fuentes[i][0] != '@' && access(fuentes[i], 0) != 0) {             /* antes que TinyCC, y en castellano */
            char r[256]; realpath(fuentes[i], r);
            fprintf(stderr, "cc: no se puede leer %s (%s)\n", r, errno == EACCES ? "ruta privada" : "no existe");
            g_errores++; break;
        }
        if (tcc_add_file(s, fuentes[i]) < 0 && !g_errores) { fprintf(stderr, "cc: no se puede leer %s\n", fuentes[i]); g_errores++; }
    }
    if (!obj && !g_errores) {
        tcc_add_include_path(s, "/tcc/lib");
        char hs[32]; snprintf(hs, sizeof hs, "(%ldu << 20)", heap_mib);
        tcc_define_symbol(s, "SOMA_HEAP_SIZE", hs);
        for (int i = 0; RUNTIME[i] && !g_errores; ++i)
            if (tcc_add_file(s, RUNTIME[i]) < 0) { fprintf(stderr, "cc: falta la biblioteca %s\n", RUNTIME[i]); g_errores++; }
    }
    if (g_errores) { tcc_delete(s); return 1; }
    if (check) { tcc_delete(s); return 0; }                                   /* compila: basta */
    if (!salida) {
        if (run) salida = "/workspace/.cc_run.elf";
        else if (prueba) salida = "/workspace/.programa.elf";
        else salida = sin_ext(fuentes[0][0] == '@' ? "programa.c" : fuentes[0], solo_c ? ".o" : ".elf");
    }
    if (tcc_output_file(s, salida) < 0 || g_errores) { fprintf(stderr, "cc: no se pudo escribir %s\n", salida); tcc_delete(s); return 1; }
    tcc_delete(s);
    if (!obj) {                                                              /* cabe en los 12 MiB de un proceso de SOMA? */
        long pag = paginas(salida);
        if (pag > MAX_PAGINAS) {
            FILE *z = fopen(salida, "wb"); if (z) fclose(z);                 /* vacio: el nucleo no lo cargara */
            fprintf(stderr, "cc: el programa no cabe en memoria: necesita %ld KiB y un proceso de SOMA tiene %d KiB (codigo + datos +"
                            " memoria dinamica de %ld MiB + 1 MiB de pila). Usa menos memoria estatica o un -heap menor\n",
                    pag * 4, MAX_PAGINAS * 4, heap_mib);
            return 1;
        }
    }
    if (!check && !prueba && !run) printf("cc: %s%s\n", salida[0] == '/' ? "" : "/workspace/", salida);
    return 0;
}
