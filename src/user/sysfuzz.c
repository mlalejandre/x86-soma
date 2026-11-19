/* PARCHE 117: fuzzer de llamadas al sistema HOSTIL (ring 3). Lanza miles de syscalls con argumentos maliciosos (punteros del kernel, fuera de la ventana,
 * en el borde, desbordando u64, longitudes enormes, numeros de syscall inexistentes...) y comprueba con un ORACULO que el kernel nunca acepta lo que no debe.
 * Uso: exec /bin/sysfuzz.elf "<semilla> <iteraciones>". Codigo de salida: 0 = ningun fallo; n > 0 = n violaciones del oraculo (tope 100). */
#include "ulib.h"

#define UBASE   0x8000000000UL
#define UWIN    0x2000000UL
#define USTACK  (UBASE + 0x1000000UL)

static u64 rs;
static u64 rnd(void) { rs ^= rs >> 12; rs ^= rs << 25; rs ^= rs >> 27; return rs * 2685821657736338717UL; }
/* Memoria que el kernel PUEDE escribir porque se la pedimos: un arena con relleno a ambos lados (el kernel escribe como mucho 128 bytes por llamada). El estado
 * del oraculo (rs, breaches...) vive fuera: las direcciones del fuzzer que caerian sobre la memoria propia se desvian al arena, para que un kernel correcto
 * que obedece una peticion legitima no pueda ser confundido con un kernel que corrompe. */
static struct { unsigned char pre[4096]; unsigned char buf[8192]; unsigned char post[4096]; } arena __attribute__((aligned(4096)));
#define buf (arena.buf)
static char abuf[128];
static unsigned char sentinel[64];
static u64 breaches, calls;

static const u64 KPTR[] = { 0x1, 0x1000, 0x100000, 0x200000, 0x7FFFFFFFUL, 0x80000000UL, 0xFFFFFFFFUL, 0x7FFFFFFFFFUL, 0xFFFFFFFF80000000UL,
                            0xFFFF800000000000UL, 0xFFFF800000100000UL, 0x0000800000000000UL, 0x7FFFFFFFFFFFFFFFUL, 0xFFFFFFFFFFFFFFFFUL,
                            0xFFFFFFFFFFFFF000UL, 0x8000000000UL - 1, 0x8000000000UL + 0x2000000UL, 0x8000000000UL + 0x2000000UL + 1, 0x4000000000UL };
static u64 rv(void)
{
    switch (rnd() % 16) {
    case 0:  return 0;
    case 1:  return 1;
    case 2:  return (u64)-1;
    case 3:  return rnd() % 300;
    case 4:  return KPTR[rnd() % (sizeof KPTR / sizeof KPTR[0])];
    case 5:  { static const u64 e[] = { UBASE - 1, UBASE, UBASE + UWIN - 1, UBASE + UWIN, UBASE + UWIN + 1, UBASE + 4096, UBASE + 0x3FFFFF, UBASE + 0x400000 }; return e[rnd() % 8]; }
    case 6:  return USTACK + (rnd() % 0x11000) - 0x800;
    case 7:  return (u64)buf + (rnd() % 8192) - 64;
    case 8:  return 1UL << 31;
    case 9:  return 1UL << 32;
    case 10: return 1UL << 63;
    case 11: return (1UL << 63) - 1;
    case 12: return rnd();
    case 13: return rnd() & 0xFFF;
    case 14: return UBASE + rnd() % UWIN;
    default: return (UBASE + rnd() % UWIN) & ~0xFFFUL;
    }
}
/* el rango [p, p+len) queda DEFINITIVAMENTE fuera de la ventana de usuario (el kernel debe rechazarlo siempre) */
static int outside(u64 p, u64 len)
{
    if (len == 0) return 0;
    if (p < UBASE || p >= UBASE + UWIN) return 1;
    if (len > UWIN || p + len < p || p + len > UBASE + UWIN) return 1;
    return 0;
}
/* destino de ESCRITURA del kernel: si cae sobre memoria propia escribible (datos/bss o pila) se desvia al arena; el codigo (solo lectura), las direcciones sin mapear
 * y las de fuera de la ventana se dejan tal cual: el kernel debe rechazarlas */
static u64 safe_out(u64 p)
{
    if ((p >= UBASE + 0x1000 && p < UBASE + 0x20000) || (p >= USTACK - 0x2000 && p < USTACK + 0x12000)) {
        if (p >= (u64)&arena && p < (u64)&arena + sizeof arena) return p;
        return (u64)buf + (p & 0x7FF);
    }
    return p;
}
static void fail(void) { if (breaches < 100) breaches++; }
static void expect_neg(i64 r) { if (r >= 0) fail(); }

extern void _start(void);

void _start(void)
{
    i64 n = u_getargs(abuf, sizeof abuf);
    u64 seed = 1, iters = 1000; int k = 0;
    if (n > 0) {
        u64 v = 0; int have = 0;
        for (; abuf[k] >= '0' && abuf[k] <= '9'; k++) { v = v * 10 + (u64)(abuf[k] - '0'); have = 1; }
        if (have) seed = v;
        while (abuf[k] == ' ') k++;
        v = 0; have = 0;
        for (; abuf[k] >= '0' && abuf[k] <= '9'; k++) { v = v * 10 + (u64)(abuf[k] - '0'); have = 1; }
        if (have && v > 0 && v < 10000000) iters = v;
    }
    rs = seed * 0x9E3779B97F4A7C15UL + 1; if (!rs) rs = 1;
    for (u64 i = 0; i < 64; i++) { buf[i] = '.'; sentinel[i] = 0x5A; }
    u64 pid0 = (u64)u_getpid();

    for (u64 it = 0; it < iters; it++) {
        i64 nr; int kind = (int)(rnd() % 8);
        u64 a = rv(), b = rv(), c = rv();
        if (kind == 0) {                                        /* syscalls que NO existen: deben dar error, nunca crashear */
            nr = (i64)(20 + rnd() % 400);
            if (rnd() & 1) nr = (i64)rv();
            if (nr >= 0 && nr < 20) continue;
            calls++; expect_neg(u_sys3(nr, (i64)a, (i64)b, (i64)c));
            continue;
        }
        nr = (i64)(1 + rnd() % 19);                             /* 1..19: nunca SYS_EXIT (0) */
        if (nr == SYS_SLEEP) { a = rnd() % 3; }                 /* sin dormir de verdad: acota el tiempo de la prueba */
        if (nr == SYS_WRITE && !outside(a, b)) { a = (u64)buf; b = rnd() % 4; }         /* escritura valida: unos pocos '.' */
        if (nr == SYS_AUDIO_WRITE && !outside(b == 0 ? a : a, b)) { b = 0; }            /* sin colgarse esperando al dispositivo de audio */
        if (nr == SYS_GETARGS || nr == SYS_AUDIO_STATUS || nr == SYS_FB_INFO) a = safe_out(a);
        if (nr == SYS_READ) b = safe_out(b);
        calls++;
        i64 r = u_sys3(nr, (i64)a, (i64)b, (i64)c);
        if (r < 0 && !soma_errno_valid(r)) fail();                                       /* PARCHE 124: todo error es un codigo de la tabla unica (nunca un -1 generico ni un codigo interno) */
        /* ORACULO: lo que el kernel jamas debe aceptar */
        switch (nr) {
        case SYS_WRITE:       if (outside(a, b)) expect_neg(r); break;
        case SYS_OPEN:        if (b >= 1 && b <= 255 && outside(a, b)) expect_neg(r); break;
        case SYS_READ:        if (c >= 1 && outside(b, c > 65536 ? 65536 : c)) expect_neg(r); break;
        case SYS_GETARGS:     if (b >= 1 && outside(a, b)) expect_neg(r); break;
        case SYS_AUDIO_WRITE: if (b >= 1 && outside(a, b)) expect_neg(r); break;
        case SYS_AUDIO_STATUS:if (outside(a, 24)) expect_neg(r); break;
        case SYS_FB_INFO:     if (outside(a, 12)) expect_neg(r); break;
        default: break;
        }
        if (nr == SYS_GETARGS && b >= 1 && a == (u64)&_start) expect_neg(r);             /* destino en una pagina de SOLO LECTURA (codigo) */
        if ((nr == SYS_AUDIO_STATUS || nr == SYS_FB_INFO) && a == (u64)&_start) expect_neg(r);
        if ((it & 63) == 0) {                                                            /* PARCHE 121: rutas privadas e internas de BIGFS jamas se abren desde ring 3 */
            static const char *const prohibidas[] = { "/.ramfs/0", "/.ramfs/t000", "/bin/../.ramfs/0", "//.ramfs/0", "/mem/x", "/agent/state.txt", "/etc/init.sh", "/./.ckpt/B", "/music/../../.ramfs/3", 0 };
            const char *pp = prohibidas[(it >> 6) % 9];
            i64 fd = u_open(pp);
            if (fd >= 0) fail();
        }
        if ((it & 255) == 0 && (u64)u_getpid() != pid0) fail();                          /* la identidad del proceso no cambia */
    }
    /* despues de todo, el proceso sigue sano: puede escribir con una llamada valida y su memoria no se ha tocado */
    if (u_write("", 0) != 0) fail();
    for (u64 i = 0; i < 64; i++) if (sentinel[i] != 0x5A) fail();          /* el kernel no ha escrito donde nadie se lo pidio */
    u_exit((i64)breaches);
}
