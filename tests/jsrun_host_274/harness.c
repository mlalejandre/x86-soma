/* SOMA_274_JS: ejecuta en el anfitrion (Linux x86_64) el programa de ring 3 REAL (src/user/jsrun.c: monton, argumentos, E/S, bucle) con el
 * motor compilado como en SOMA (cambio de pila incluido, sin WEB_HOST_TEST). Las llamadas al sistema de SOMA se emulan aqui:
 * consola = stdin/stdout, VFS = el directorio $JSR_WS, reloj = el del equipo, ventana = una ventana de mentira que se cierra tras N sondeos. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <stdint.h>
#include "../../src/user/ulib.h"
#include "../../src/arch/x86_64/usermode.h"

static const char *g_argline; static const char *g_srcfile;
static int g_polls, g_winmax = 5, g_draws, g_pres;
static long emu_sys3(long nr, long a, long b, long c);
#define u_sys3 emu_sys3
static void emu_exit(long c) { fflush(stdout); exit((int)(c & 255)); }
#define u_exit(c) emu_exit(c)
static long emu_write(const char *p, unsigned long n) { fflush(stdout); return write(1, p, n); }
#define u_write emu_write
static void emu_sleep(unsigned long ms) { usleep((useconds_t)ms * 1000); }
#define u_sleep emu_sleep
static void emu_yield(void) { }
#define u_yield emu_yield
static long emu_getpid(void) { return 42; }
#define u_getpid emu_getpid
static unsigned long emu_up(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned long)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
#define u_uptime_ms emu_up
static long emu_getargs(char *b, unsigned long n) { strncpy(b, g_argline, n - 1); b[n - 1] = 0; return (long)strlen(g_argline); }
#define u_getargs emu_getargs

struct soma_vfs_req_h { const char *path; u64 plen; void *buf; u64 len; };
static void hostpath(const char *p, char *o) { snprintf(o, 600, "%s/%s", getenv("JSR_WS"), p + 11); }
static long emu_sys3(long nr, long a, long b, long c)
{
    (void)c;
    if (nr == 30) {
        switch (a) {
        case 0: { if (!fgets((char *)b, (int)c + 1, stdin)) return -1; return (long)strlen((char *)b); }
        case 1: { time_t t = time(0); struct tm m; gmtime_r(&t, &m); long *o = (long *)b; o[0] = m.tm_year + 1900; o[1] = m.tm_mon + 1; o[2] = m.tm_mday; o[3] = m.tm_hour; o[4] = m.tm_min; o[5] = m.tm_sec; return 0; }
        case 2: g_polls = 0; return 1;
        case 3: printf("[win cerrada draws=%d presents=%d]\n", g_draws, g_pres); return 0;
        case 4: g_draws += (int)(c / 4); return 0;
        case 5: g_pres++; return 0;
        case 6: { long *s = (long *)b; s[0] = ++g_polls <= g_winmax; s[1] = 300; s[2] = 200; s[3] = 7; s[4] = 9; return 0; }
        case 7: return 16;
        case 8: { if (!g_srcfile) return -1; FILE *f = fopen(g_srcfile, "rb"); if (!f) return -1; fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
                  if (!b) { fclose(f); return sz; } long n = (long)fread((void *)b, 1, (size_t)c, f); fclose(f); return n; }
        }
        return -1;
    }
    if (nr == 31) {
        struct soma_vfs_req_h *r = (struct soma_vfs_req_h *)b; char hp[640];
        if (strncmp(r->path, "/workspace/", 11)) return a == 1 ? -13 : -2;
        hostpath(r->path, hp);
        if (a == 2) { FILE *f = fopen(hp, "rb"); if (!f) return -2; fseek(f, 0, SEEK_END); long sz = ftell(f); fclose(f); return sz; }
        if (a == 0) { FILE *f = fopen(hp, "rb"); if (!f) return -2; long n = (long)fread(r->buf, 1, r->len, f); fclose(f); return n; }
        if (a == 1) { FILE *f = fopen(hp, "wb"); if (!f) return -5; fwrite(r->buf, 1, r->len, f); fclose(f); return (long)r->len; }
    }
    return -38;
}
#define MEMFN(n) host_##n
#include "../../src/user/jsrun.c"

int main(int argc, char **argv)
{
    /* el nucleo reserva esta region bajo demanda; aqui la da mmap en la misma direccion */
    void *h = mmap((void *)USER_HEAP_VA, (size_t)JS_HEAP_MIB << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (h != (void *)USER_HEAP_VA) { fprintf(stderr, "no se pudo reservar el monton en %lx\n", (unsigned long)USER_HEAP_VA); return 99; }
    g_argline = argc > 1 ? argv[1] : "";
    g_srcfile = argc > 2 ? argv[2] : 0;
    if (argc > 3) g_winmax = atoi(argv[3]);
    int rc = js_main();
    fflush(stdout);
    fprintf(stderr, "[codigo %d]\n", rc);
    return rc;
}
