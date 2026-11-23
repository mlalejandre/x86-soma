/* SOMA_274_JS: "js", el JavaScript de SOMA para aplicaciones. El mismo motor del navegador (web_js_*), sin pagina, como proceso de ring 3.
 *   js archivo.js         ejecuta /workspace/archivo.js          js -c "codigo"       ejecuta una linea
 *   js -s / js -x         comprobar / ejecutar el codigo que le pasa el nucleo (programador; SYS_SOMA_CON op 8), como python -s / -x
 * Llamadas al sistema: las de python (SYS_SOMA_CON: consola, reloj y ventana; SYS_SOMA_VFS: archivos de /workspace).
 * Memoria: una region bajo demanda en USER_HEAP_VA que reserva el nucleo (orden js). El proceso no ve nada mas de SOMA. */
#include <stdint.h>
#include <stddef.h>
#include "ulib.h"
#include "memfn.h"
#include "../arch/x86_64/usermode.h"
#include "../apps/jsrun_core.h"

#define JS_HEAP_MIB 160u
static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int scmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
struct soma_vfs_req { const char *path; u64 plen; void *buf; u64 len; };

/* ------------------------------------------------------------ monton (igual que el del navegador aislado) */
#define H_MAGIC  0x4A533734ULL
#define H_NCLASS 22
static const uint32_t H_SZ[H_NCLASS] = { 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192, 12288, 16384, 24576, 32768, 65536 };
struct hblk { uint64_t size; uint64_t tag; };
static struct hblk *h_free[H_NCLASS], *h_big;
static uint64_t h_brk = USER_HEAP_VA, h_end = USER_HEAP_VA + ((uint64_t)JS_HEAP_MIB << 20);
static struct hblk *h_grow(uint64_t need) { if (h_end - h_brk < need) return 0; struct hblk *b = (struct hblk *)h_brk; h_brk += need; b->size = need; return b; }
void *kmalloc(size_t n)
{
    if (n == 0 || n > ((uint64_t)JS_HEAP_MIB << 20)) return 0;
    uint64_t need = ((uint64_t)n + 16u + 15u) & ~15ULL;
    struct hblk *b = 0;
    if (need <= 65536) {
        int c = 0; while (H_SZ[c] < need) c++;
        need = H_SZ[c];
        if (h_free[c]) { b = h_free[c]; h_free[c] = (struct hblk *)b->tag; }
        else if (!(b = h_grow(need))) return 0;
    } else {
        need = (need + 4095u) & ~4095ULL;
        struct hblk **pp = &h_big;
        for (struct hblk *f = h_big; f; pp = (struct hblk **)&f->tag, f = (struct hblk *)f->tag) {
            if (f->size < need) continue;
            if (f->size - need >= 8192) { struct hblk *r = (struct hblk *)((uint8_t *)f + need); r->size = f->size - need; r->tag = f->tag; *pp = r; f->size = need; }
            else *pp = (struct hblk *)f->tag;
            b = f; break;
        }
        if (!b && !(b = h_grow(need))) return 0;
    }
    b->tag = H_MAGIC;
    memset((uint8_t *)b + 16, 0, (size_t)(b->size - 16));
    return (uint8_t *)b + 16;
}
void kfree(void *p)
{
    if (!p) return;
    struct hblk *b = (struct hblk *)((uint8_t *)p - 16);
    if (b->tag != H_MAGIC || (uint64_t)b < USER_HEAP_VA || (uint64_t)b >= h_brk) u_exit(-77);      /* doble liberacion o puntero ajeno */
    if (b->size <= 65536) { int c = 0; while (H_SZ[c] < b->size) c++; b->tag = (uint64_t)h_free[c]; h_free[c] = b; return; }
    struct hblk *prev = 0, *cur = h_big;
    while (cur && cur < b) { prev = cur; cur = (struct hblk *)cur->tag; }
    b->tag = (uint64_t)cur;
    if (prev) prev->tag = (uint64_t)b; else h_big = b;
    if (cur && (uint8_t *)b + b->size == (uint8_t *)cur) { b->size += cur->size; b->tag = cur->tag; }
    if (prev && (uint8_t *)prev + prev->size == (uint8_t *)b) { prev->size += b->size; prev->tag = b->tag; }
}
size_t kheap_max_bytes(void) { return (size_t)JS_HEAP_MIB << 20; }

/* ------------------------------------------------------------ lo que pide el motor */
uint64_t web_now_ms(void) { return u_uptime_ms(); }
void web_yield(void) { u_yield(); }
extern char *g_co_lo, *g_co_hi;
uint32_t web_stack_left(void)
{
    char c; char *sp = &c;
    if (g_co_lo && sp >= g_co_lo && sp < g_co_hi) return (uint32_t)(sp - g_co_lo);
    return 60000;
}
char *web_stack_hi(const char *sp) { (void)sp; return 0; }
/* Sin red ni paginas: las URL y el azar de bajo nivel son lo unico que el motor pide a web_net.c y al nucleo.
 * rng_bytes alimenta Math.random (xorshift64* sembrado con el reloj): NO es criptografico, y crypto.getRandomValues no debe usarse para secretos. */
int bootinfo_disabled(const char *f) { (void)f; return 0; }
struct wn_url;
int wn_url_parse(const char *url, struct wn_url *u) { (void)url; (void)u; return -1; }
int wn_url_resolve(const char *base, const char *ref, char *out, uint32_t max) { (void)base; (void)ref; (void)out; (void)max; return -1; }
void rng_bytes(void *out, size_t n)
{
    static uint64_t s;
    if (!s) { i64 t[6] = {0}; u_sys3(SYS_SOMA_CON, 1, (i64)t, 0); s = 0x9E3779B97F4A7C15ULL ^ (u_uptime_ms() << 20) ^ (uint64_t)(t[3] * 3600 + t[4] * 60 + t[5]) ^ (uint64_t)u_getpid(); if (!s) s = 1; }
    uint8_t *o = (uint8_t *)out;
    while (n) { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; uint64_t v = s * 0x2545F4914F6CDD1DULL; for (int i = 0; i < 8 && n; i++, n--) { *o++ = (uint8_t)v; v >>= 8; } }
}

/* Date.now(): el reloj del equipo (SYS_SOMA_CON op 1 = anio, mes, dia, hora, minuto, segundo) -> ms desde 1970 */
static int64_t days_from_civil(int64_t y, int64_t m, int64_t d)
{
    y -= m <= 2; int64_t era = (y >= 0 ? y : y - 399) / 400; int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
uint64_t web_epoch_ms(void)
{
    static uint64_t base_ms, base_up;
    if (!base_ms) {
        i64 t[6] = {0};
        u_sys3(SYS_SOMA_CON, 1, (i64)t, 0);
        if (t[0] < 2000) t[0] = 2026;
        if (t[1] < 1 || t[1] > 12) t[1] = 1;
        if (t[2] < 1 || t[2] > 31) t[2] = 1;
        base_ms = (uint64_t)((days_from_civil(t[0], t[1], t[2]) * 86400 + t[3] * 3600 + t[4] * 60 + t[5]) * 1000);
        base_up = u_uptime_ms();
    }
    return base_ms + (u_uptime_ms() - base_up);
}

/* ------------------------------------------------------------ E/S del nucleo de js */
static void io_out(void *u, const char *s, uint32_t n) { (void)u; while (n) { i64 w = u_write(s, n); if (w <= 0) break; s += w; n -= (uint32_t)w; } }
static int io_readline(void *u, char *buf, uint32_t max)
{
    (void)u;
    i64 n = u_sys3(SYS_SOMA_CON, 0, (i64)buf, (i64)(max - 1));
    if (n < 0) return -1;
    buf[n] = 0; return (int)n;
}
static char g_path[256];
static const char *full_path(const char *p)
{
    if (p[0] == '/') return p;
    uint32_t k = 0; const char *w = "/workspace/";
    while (*w) g_path[k++] = *w++;
    while (*p && k < sizeof g_path - 1) g_path[k++] = *p++;
    g_path[k] = 0; return g_path;
}
static i64 vfs_op(int op, const char *path, void *buf, u64 len)
{
    struct soma_vfs_req r = { path, slen(path), buf, len };
    return u_sys3(SYS_SOMA_VFS, op, (i64)&r, 0);
}
static int io_readfile(void *u, const char *path, char **out, uint32_t *len)
{
    (void)u;
    const char *p = full_path(path);
    i64 sz = vfs_op(2, p, 0, 0);
    if (sz < 0 || sz > (16 << 20)) return -1;
    char *b = (char *)kmalloc((size_t)sz + 1); if (!b) return -1;
    i64 n = vfs_op(0, p, b, (u64)sz);
    if (n < 0) { kfree(b); return -1; }
    b[n] = 0; *out = b; *len = (uint32_t)n; return 0;
}
static int io_writefile(void *u, const char *path, const char *d, uint32_t n) { (void)u; return vfs_op(1, full_path(path), (void *)d, n) < 0 ? -1 : 0; }
static uint64_t io_now(void *u) { (void)u; return u_uptime_ms(); }
static void io_sleep(void *u, uint32_t ms) { (void)u; u_sleep(ms ? ms : 1); }
static int io_win_create(void *u, const char *t, uint32_t tl, int w, int h)
{
    (void)u; u64 q[4] = { (u64)t, (u64)tl, (u64)w, (u64)h };
    return u_sys3(SYS_SOMA_CON, 2, (i64)q, 0) == 1;
}
static void io_win_close(void *u) { (void)u; u_sys3(SYS_SOMA_CON, 3, 0, 0); }
static void io_win_present(void *u) { (void)u; u_sys3(SYS_SOMA_CON, 5, 0, 0); }
static void io_win_state(void *u, int64_t st[8], int take) { (void)u; for (int i = 0; i < 8; i++) st[i] = 0; u_sys3(SYS_SOMA_CON, 6, (i64)st, take); }
static int io_win_key(void *u) { (void)u; return (int)u_sys3(SYS_SOMA_CON, 7, 0, 0); }
static void io_win_draw(void *u, const int32_t *w, uint32_t n) { (void)u; if (n) u_sys3(SYS_SOMA_CON, 4, (i64)w, (i64)(n * 4)); }

static const struct jsr_io IO = { 0, io_out, io_readline, io_readfile, io_writefile, io_now, 0, io_sleep, 0,
                                  io_win_create, io_win_close, io_win_present, io_win_state, io_win_key, io_win_draw };

static char g_args[USER_ARGS_MAX + 8];
static void say(const char *s) { io_out(0, s, (uint32_t)slen(s)); }
static int code_of(int r) { return r >= JSR_ERR_EXIT ? r - JSR_ERR_EXIT : r; }

int js_main(void);
__asm__(".text\n.global _start\n_start:\n  andq $-16, %rsp\n  call js_main\n  movq %rax, %rdi\n  xorq %rax, %rax\n  syscall\n  jmp .\n");

int js_main(void)
{
    i64 al = u_getargs(g_args, sizeof g_args);
    if (al < 0) al = 0;
    char *p = g_args; while (*p == ' ') p++;
    if (!*p) { say("uso: js archivo.js | js -c \"codigo\"\n"); return 2; }
    char *tok = p; while (*p && *p != ' ') p++;
    char *rest = p; if (*rest) *rest++ = 0; while (*rest == ' ') rest++;
    if (!scmp(tok, "-s") || !scmp(tok, "-x")) {
        i64 sz = u_sys3(SYS_SOMA_CON, 8, 0, 0);
        if (sz < 0) { say("js: no hay codigo que comprobar\n"); return 2; }
        char *src = (char *)kmalloc((size_t)sz + 1); if (!src) { say("js: sin memoria\n"); return 2; }
        i64 n = u_sys3(SYS_SOMA_CON, 8, (i64)src, sz); if (n < 0) n = 0;
        src[n] = 0;
        int r = jsrun_run(&IO, src, (uint32_t)n, "programa.js", tok[1] == 's');
        if (tok[1] == 's' && r == JSR_OK) say("OK\n");
        return code_of(r);
    }
    if (!scmp(tok, "-c")) {
        if (!*rest) { say("uso: js -c \"codigo\"\n"); return 2; }
        return code_of(jsrun_run(&IO, rest, (uint32_t)slen(rest), "<cadena>", 0));
    }
    const char *path = full_path(tok);
    i64 sz = vfs_op(2, path, 0, 0);
    if (sz < 0) { say(sz == -13 ? "js: sin permiso para leer " : "js: no existe "); say(path); say("\n"); return 2; }
    char *src = (char *)kmalloc((size_t)sz + 1); if (!src) { say("js: sin memoria\n"); return 2; }
    i64 n = vfs_op(0, path, src, (u64)sz);
    if (n < 0) { say("js: no se pudo leer el script\n"); return 2; }
    src[n] = 0;
    return code_of(jsrun_run(&IO, src, (uint32_t)n, tok, 0));
}
