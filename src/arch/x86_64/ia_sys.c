/* SOMA_329_IA_LOCAL: el motor de IA local (llama.cpp + ggml con AVX2) en un proceso de ring 3 que vive mientras se usa.
 *
 *   - Un solo proceso (ia.elf, comprimido en la imagen como la voz): se lanza con la primera orden y se para solo tras
 *     IA_OCIOSO_MS sin uso (o con "ia parar"), y entonces devuelve toda su memoria.
 *   - Memoria: el monton es una region GRANDE bajo demanda (marcos de la reserva alta, como la voz): solo gasta lo que toca
 *     (el modelo y los bufers de calculo). La comunicacion, una region compartida pequena (src/core/ia_abi.h).
 *   - Hilos de calculo en los nucleos secundarios (como la voz) y base de FS propia (SYS_SOMA_FS) para libstdc++.
 *   - El nucleo no hace coma flotante: manda texto y recibe texto (JSON como el de un servidor compatible con OpenAI).
 *   - Los modelos son archivos GGUF de /modelos en el almacen. "ia instalar <nombre>" los copia de la carpeta MODELOS del pendrive
 *     o, en QEMU, de -fw_cfg name=opt/soma/ia/<nombre>,file=<archivo.gguf>. */
#include <stdint.h>
#include <stddef.h>
#include "ia_sys.h"
#include "ia_abi.h"
#include "usermode.h"
#include "vmm.h"
#include "pmm.h"
#include "mem.h"
#include "mutex.h"
#include "thread.h"
#include "console.h"
#include "bigfs.h"
#include "bigfs_glue.h"
#include "fwcfg.h"
#include "usbdisk.h"
#include "fat.h"
#include "idt.h"
#include "progz.h"
#include "smp.h"
PROGZ_DECL(ia);

#define IA_MONTON_MIB  6144u                  /* region virtual del monton (solo se gasta lo que se toca) */
#define IA_OCIOSO_MS   (10u * 60u * 1000u)    /* sin ordenes en este tiempo, el proceso se para y libera la memoria */

static struct kmutex g_lk; static int g_lk_ok;
static volatile int g_idx = -1;
static struct uregion g_heap, g_sh;
static volatile struct ia_ctl *g_ctl;
static volatile uint64_t g_ultimo;            /* ultima orden (ms) */
static volatile int g_vigia;
static char g_log[4096];                      /* lo que el proceso escribe (errores internos) */
static uint32_t g_ordenes, g_fallos;
static uint32_t g_hilos_conf;                 /* SOMA_361_HILOS_IA: hilos pedidos con "ia hilos N" (0 = automatico) */

/* SOMA_361_HILOS_IA: automatico = el hilo principal (nucleo 0) + uno por cada nucleo fisico libre distinto del 0. Con SMT (T480s:
 * 4 nucleos, 8 hilos) salen 4 y no 8: los hermanos HT no suman en AVX2 y el del 0 frena al hilo principal, que marca el paso. */
static uint32_t hilos_para_orden(void)
{
    uint32_t h = g_hilos_conf ? g_hilos_conf : 1u + (uint32_t)smp_user_free_fisicos();
    return h > 8 ? 8 : h;
}

static uint32_t slen(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static void sput(char *o, uint32_t cap, uint32_t *p, const char *s) { while (s && *s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void sdec(char *o, uint32_t cap, uint32_t *p, uint64_t v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } sput(o, cap, p, b + i); }
static int ends_with(const char *s, const char *e)
{
    uint32_t a = slen(s), b = slen(e);
    if (a < b) return 0;
    for (uint32_t i = 0; i < b; ++i) { char c = s[a - b + i]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != e[i]) return 0; }
    return 1;
}
static void lock(void) { if (!g_lk_ok) { kmutex_init(&g_lk, "ia"); g_lk_ok = 1; } kmutex_lock(&g_lk); }

/* ---- region compartida: los marcos estan mapeados 1:1 en el nucleo; se copia pagina a pagina */
static void sh_escribe(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    while (n) {
        uint32_t k = off >> 12, o = off & 0xFFF, c = 4096 - o; if (c > n) c = n;
        uint8_t *d = (uint8_t *)(uintptr_t)g_sh.frames[k] + o;
        for (uint32_t i = 0; i < c; ++i) d[i] = s[i];
        s += c; off += c; n -= c;
    }
}
static void sh_lee(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n) {
        uint32_t k = off >> 12, o = off & 0xFFF, c = 4096 - o; if (c > n) c = n;
        const volatile uint8_t *s = (const volatile uint8_t *)(uintptr_t)g_sh.frames[k] + o;
        for (uint32_t i = 0; i < c; ++i) d[i] = s[i];
        d += c; off += c; n -= c;
    }
}
static void regiones_libera(void)
{
    struct uregion *rs[2] = { &g_heap, &g_sh };
    for (int i = 0; i < 2; ++i) {
        struct uregion *r = rs[i];
        if (!r->frames) continue;
        for (uint32_t k = 0; k < r->npages; ++k) if (r->frames[k]) pmm_free_frame((uintptr_t)r->frames[k]);
        kfree(r->frames); r->frames = 0; r->npages = 0; r->used = 0;
    }
    g_ctl = 0;
}

int ia_disponible(const char **why)
{
    static const char *d; if (!why) why = &d;
    if (!usermode_avx2_ok()) { *why = "este procesador no tiene AVX2/FMA (o no esta activo): hace falta para el motor de IA local"; return 0; }
    *why = "";
    return 1;
}

static int vivo(void) { return g_idx >= 0 && usermode_proc_alive(g_idx); }

/* para el proceso (con el cerrojo cogido) y libera todo */
static void para_bloqueado(void)
{
    if (g_idx >= 0) {
        if (usermode_proc_alive(g_idx) && g_ctl) {         /* primero se le pide que salga */
            g_ctl->cmd = IA_CMD_SALIR; __atomic_add_fetch(&g_ctl->cmd_seq, 1, __ATOMIC_RELEASE);
            uint64_t t0 = timer_get_uptime_ms();
            while (usermode_proc_alive(g_idx) && timer_get_uptime_ms() - t0 < 1000) thread_sleep(5);
        }
        if (usermode_proc_alive(g_idx)) usermode_stop(g_idx);
        (void)usermode_wait(g_idx);
        g_idx = -1;
    }
    regiones_libera();
}

static void vigia(void *arg)
{
    (void)arg;
    for (;;) {
        thread_sleep(15000);
        if (g_idx < 0 || !g_lk_ok) continue;
        if (timer_get_uptime_ms() - g_ultimo < IA_OCIOSO_MS) continue;
        kmutex_lock(&g_lk);
        if (g_idx >= 0 && timer_get_uptime_ms() - g_ultimo >= IA_OCIOSO_MS) para_bloqueado();
        kmutex_unlock(&g_lk);
    }
}

/* lanza el proceso (con el cerrojo cogido). 0 = listo */
static int lanza(char *err, uint32_t ecap)
{
    uint32_t p = 0;
    if (vivo()) return 0;
    if (g_idx >= 0) para_bloqueado();                       /* murio: se recoge */
    const uint64_t RW = VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITABLE | VMM_FLAG_NX;
    g_heap.va = USER_HEAP_VA; g_heap.npages = IA_MONTON_MIB << 8; g_heap.used = 0; g_heap.flags = RW; g_heap.demand = 2;
    g_heap.frames = (uint64_t *)kmalloc((size_t)g_heap.npages * sizeof(uint64_t));
    g_sh.va = IA_SH_VA; g_sh.npages = IA_SH_PAGES; g_sh.used = 0; g_sh.flags = RW; g_sh.demand = 0;
    g_sh.frames = (uint64_t *)kmalloc((size_t)g_sh.npages * sizeof(uint64_t));
    if (!g_heap.frames || !g_sh.frames) { regiones_libera(); sput(err, ecap, &p, "sin memoria para preparar el motor de IA"); return -1; }
    for (uint32_t k = 0; k < g_heap.npages; ++k) g_heap.frames[k] = 0;
    for (uint32_t k = 0; k < g_sh.npages; ++k) {
        uintptr_t f = pmm_alloc_frame();
        if (!f) { regiones_libera(); sput(err, ecap, &p, "sin memoria para la region compartida del motor de IA"); return -1; }
        uint8_t *q = (uint8_t *)f; for (int b = 0; b < 4096; ++b) q[b] = 0;
        g_sh.frames[k] = f; g_sh.used++;
    }
    g_ctl = (volatile struct ia_ctl *)(uintptr_t)g_sh.frames[0];
    g_ctl->magic = IA_MAGIC; g_ctl->version = IA_VERSION;
    g_ctl->monton = (uint64_t)IA_MONTON_MIB << 20;
    g_ctl->estado = IA_E_ARRANCANDO;
    struct uexec_spec sp;
    uint8_t *z = (uint8_t *)&sp; for (size_t i = 0; i < sizeof sp; ++i) z[i] = 0;
    sp.sysmask = (1u << SYS_EXIT) | (1u << SYS_WRITE) | (1u << SYS_YIELD) | (1u << SYS_SLEEP) | (1u << SYS_GETPID) | (1u << SYS_OPEN) |
                 (1u << SYS_READ) | (1u << SYS_SEEK) | (1u << SYS_CLOSE) | (1u << SYS_FSIZE) | (1u << SYS_UPTIME_MS);
    sp.regions[0] = &g_heap; sp.regions[1] = &g_sh; sp.nregions = 2;
    sp.reserve_frames = 16384;                                  /* el monton no crece si al sistema le quedarian menos de 64 MiB */
    sp.may_thr = 1;                                             /* hilos de calculo y base de FS */
    int idx = -2, e = 0;
    struct progz pz;
    if (progz_abre(PROGZ(ia), "ia", &pz) == 0) {
        idx = usermode_exec_ex("ia", pz.elf, pz.len, 0xF0000000u, &sp, &e);   /* ~46 dias: lo para el vigia o "ia parar" */
        progz_cierra(&pz);
    }
    if (idx < 0) { regiones_libera(); sput(err, ecap, &p, "no se pudo lanzar el motor de IA (sin procesos libres o memoria)"); return -1; }
    g_log[0] = 0;
    usermode_redirect(idx, g_log, sizeof g_log, "");
    g_idx = idx;
    uint64_t t0 = timer_get_uptime_ms();
    while (g_ctl->estado == IA_E_ARRANCANDO && usermode_proc_alive(idx) && timer_get_uptime_ms() - t0 < 20000) thread_sleep(5);
    if (g_ctl->estado != IA_E_LISTO) {
        sput(err, ecap, &p, "el motor de IA no arranco");
        if (g_log[0]) { sput(err, ecap, &p, ": "); sput(err, ecap, &p, g_log); }
        para_bloqueado();
        return -1;
    }
    if (!g_vigia) { g_vigia = 1; thread_create("ia_vigia", vigia, 0); }
    return 0;
}

int ia_orden(int cmd, const char *modelo, const char *in, uint32_t in_len, char *out, uint32_t cap, uint32_t plazo_ms,
             const volatile int *cancel, char *err, uint32_t ecap)
{
    uint32_t p = 0;
    if (err && ecap) err[0] = 0;
    if (out && cap) out[0] = 0;
    const char *why;
    if (!ia_disponible(&why)) { sput(err, ecap, &p, why); return -1; }
    if (in_len > IA_IN_MAX - 1) { sput(err, ecap, &p, "la entrada es demasiado grande para el motor de IA"); return -1; }
    if (modelo && slen(modelo) >= sizeof g_ctl->modelo) { sput(err, ecap, &p, "la ruta del modelo es demasiado larga"); return -1; }
    lock();
    g_ultimo = timer_get_uptime_ms();
    if (lanza(err, ecap) != 0) { g_fallos++; kmutex_unlock(&g_lk); return -1; }
    volatile struct ia_ctl *c = g_ctl;
    if (in_len) sh_escribe(IA_IN_OFF, in, in_len);
    { char m[sizeof c->modelo]; uint32_t k = 0; for (; modelo && modelo[k] && k + 1 < sizeof m; ++k) m[k] = modelo[k]; m[k] = 0;
      for (uint32_t i = 0; i <= k; ++i) ((volatile char *)c->modelo)[i] = m[i]; }
    c->hilos = hilos_para_orden();                             /* SOMA_361_HILOS_IA: el proceso no pasa de los libres */
    c->in_len = in_len; c->cmd = (uint32_t)cmd; c->cancela = 0; c->err[0] = 0; c->parcial_len = 0;
    uint32_t s = __atomic_add_fetch(&c->cmd_seq, 1, __ATOMIC_RELEASE);
    uint64_t t0 = timer_get_uptime_ms();
    int r = 0, cancelado = 0, vencido = 0;
    while (__atomic_load_n(&c->cmd_hecho, __ATOMIC_ACQUIRE) != s) {
        if (!usermode_proc_alive(g_idx)) { r = -2; break; }
        if (cancel && *cancel && !cancelado) { c->cancela = 1; cancelado = 1; t0 = timer_get_uptime_ms(); plazo_ms = 5000; }
        if (timer_get_uptime_ms() - t0 > plazo_ms) {
            /* se acabo el plazo: primero se le pide que corte (como al cancelar) y solo si no atiende se para el proceso */
            if (!cancelado) { c->cancela = 1; cancelado = 1; vencido = 1; t0 = timer_get_uptime_ms(); plazo_ms = 5000; continue; }
            r = -3; break;
        }
        thread_sleep(cancelado ? 2 : 5);
    }
    if (r == 0 && vencido) { sput(err, ecap, &p, "el motor de IA no ha terminado a tiempo (se ha cortado la respuesta)"); r = -5; }
    g_ordenes++;
    if (r == -2) {
        sput(err, ecap, &p, "el motor de IA se ha parado");
        if (g_log[0]) { sput(err, ecap, &p, ": "); sput(err, ecap, &p, g_log); }
        para_bloqueado();
    } else if (r == -3) {
        sput(err, ecap, &p, cancelado ? "el motor de IA no atendio la cancelacion" : "el motor de IA no ha terminado a tiempo");
        para_bloqueado();                                       /* se para: no se puede saber en que estado esta */
    } else if (r == -5) {
        /* cortado por el plazo: el proceso sigue vivo y con el modelo cargado */
    } else if (c->cmd_res != 0) {
        char e[sizeof c->err]; for (uint32_t i = 0; i < sizeof e; ++i) e[i] = c->err[i];
        e[sizeof e - 1] = 0;
        sput(err, ecap, &p, e[0] ? e : "error en el motor de IA");
        r = c->cmd_res < 0 ? c->cmd_res : -1;
    } else {
        uint32_t n = c->out_len;
        if (n > IA_OUT_MAX) n = IA_OUT_MAX;
        if (!out || n + 1 > cap) { sput(err, ecap, &p, "la respuesta del motor de IA no cabe"); r = -4; }
        else { sh_lee(IA_OUT_OFF, out, n); out[n] = 0; r = (int)n; }
    }
    if (r < 0) g_fallos++;
    g_ultimo = timer_get_uptime_ms();
    kmutex_unlock(&g_lk);
    return r;
}

int ia_parcial(char *out, uint32_t cap)
{
    volatile struct ia_ctl *c = g_ctl;
    if (!c || !out || cap < 2 || g_idx < 0) return -1;
    uint32_t n = __atomic_load_n(&c->parcial_len, __ATOMIC_ACQUIRE);
    if (n > IA_PARCIAL_MAX) n = IA_PARCIAL_MAX;
    if (n + 1 > cap) n = cap - 1;
    sh_lee(IA_PARCIAL_OFF, out, n);
    out[n] = 0;
    return (int)n;
}

void ia_para(void) { lock(); para_bloqueado(); kmutex_unlock(&g_lk); }

int ia_estado(char *out, uint32_t cap)
{
    uint32_t p = 0;
    const char *why;
    if (!ia_disponible(&why)) { sput(out, cap, &p, "motor de IA local: no disponible ("); sput(out, cap, &p, why); sput(out, cap, &p, ")\n"); return 0; }
    sput(out, cap, &p, "motor de IA local (llama.cpp en ring 3): ");
    if (!vivo()) sput(out, cap, &p, "parado (se arranca con la primera orden)\n");
    else {
        sput(out, cap, &p, "en marcha, "); sdec(out, cap, &p, (uint64_t)g_heap.used * 4096 >> 20); sput(out, cap, &p, " MiB de memoria; ");
        sput(out, cap, &p, "se para solo tras "); sdec(out, cap, &p, IA_OCIOSO_MS / 60000); sput(out, cap, &p, " min sin uso\n");
        if (g_ctl) {
            sput(out, cap, &p, "  ultima orden: "); sdec(out, cap, &p, g_ctl->tokens_entrada); sput(out, cap, &p, " tokens de entrada, ");
            sdec(out, cap, &p, g_ctl->tokens_salida); sput(out, cap, &p, " de salida, "); sdec(out, cap, &p, g_ctl->ms_proceso);
            sput(out, cap, &p, " ms (carga del modelo "); sdec(out, cap, &p, g_ctl->ms_carga); sput(out, cap, &p, " ms), ");
            sdec(out, cap, &p, g_ctl->hilos_usados); sput(out, cap, &p, g_ctl->hilos_usados == 1 ? " hilo\n" : " hilos\n");
        }
    }
    sput(out, cap, &p, "  hilos de calculo: ");                             /* SOMA_361_HILOS_IA */
    if (g_hilos_conf) { sdec(out, cap, &p, g_hilos_conf); sput(out, cap, &p, " (fijados con 'ia hilos')"); }
    else { sput(out, cap, &p, "automatico, ahora "); sdec(out, cap, &p, hilos_para_orden()); }
    sput(out, cap, &p, smp_smt_shift() ? "; SMT detectado: un hilo por nucleo fisico\n" : "\n");
    sput(out, cap, &p, "  ordenes: "); sdec(out, cap, &p, g_ordenes); sput(out, cap, &p, ", fallidas: "); sdec(out, cap, &p, g_fallos); sput(out, cap, &p, "\n");
    return (int)p;
}

/* ---- modelos */
int ia_modelos(char *out, uint32_t cap)
{
    uint32_t p = 0; int n = 0;
    if (cap) out[0] = 0;
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return 0;
    uint32_t dir;
    if (bigfs_lookup(fs, IA_DIR, &dir) != (int)BIGFS_T_DIR) return 0;
    uint32_t cur = 0; struct bigfs_stat st;
    while (bigfs_readdir(fs, dir, &cur, &st) == 1) {
        if (st.type != BIGFS_T_FILE || st.name[0] == '.' || !ends_with(st.name, ".gguf")) continue;
        sput(out, cap, &p, IA_DIR "/"); sput(out, cap, &p, st.name); sput(out, cap, &p, " ("); sdec(out, cap, &p, st.size >> 20);
        sput(out, cap, &p, " MiB)\n");
        n++;
    }
    return n;
}
static int contiene(const char *s, const char *t)
{
    uint32_t a = slen(s), b = slen(t);
    for (uint32_t i = 0; i + b <= a; ++i) {
        uint32_t k = 0;
        for (; k < b; ++k) { char x = s[i + k], y = t[k]; if (x >= 'A' && x <= 'Z') x = (char)(x + 32); if (y >= 'A' && y <= 'Z') y = (char)(y + 32); if (x != y) break; }
        if (k == b) return 1;
    }
    return 0;
}
/* "bge-m3" -> el primer .gguf de /modelos cuyo nombre lo contenga (o la ruta tal cual si empieza por /) */
int ia_modelo_ruta(const char *nombre, char *ruta, uint32_t cap)
{
    uint32_t p = 0;
    if (cap) ruta[0] = 0;
    if (!nombre || !*nombre) return -1;
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return -1;
    uint32_t ino; struct bigfs_stat st;
    if (nombre[0] == '/') {
        if (bigfs_lookup(fs, nombre, &ino) != (int)BIGFS_T_FILE) return -1;
        sput(ruta, cap, &p, nombre); return 0;
    }
    uint32_t dir;
    if (bigfs_lookup(fs, IA_DIR, &dir) != (int)BIGFS_T_DIR) return -1;
    uint32_t cur = 0;
    while (bigfs_readdir(fs, dir, &cur, &st) == 1) {
        if (st.type != BIGFS_T_FILE || st.name[0] == '.' || !ends_with(st.name, ".gguf") || !contiene(st.name, nombre)) continue;
        sput(ruta, cap, &p, IA_DIR "/"); sput(ruta, cap, &p, st.name);
        return 0;
    }
    return -1;
}

/* copia un .gguf a /modelos desde fw_cfg (QEMU: opt/soma/ia/<nombre>) o desde la carpeta MODELOS del pendrive */
static int instala(const char *nombre, char *msg, uint32_t cap)
{
    uint32_t p = 0;
    if (cap) msg[0] = 0;
    if (!nombre || !*nombre) { sput(msg, cap, &p, "falta el nombre del modelo"); return -1; }
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) { sput(msg, cap, &p, "no hay almacen (BIGFS)"); return -1; }
    char fw[96]; uint32_t q = 0; sput(fw, sizeof fw, &q, "opt/soma/ia/"); sput(fw, sizeof fw, &q, nombre);
    fwcfg_reserva();                                   /* nadie mas selecciona otro archivo de fw_cfg mientras se copia */
    uint32_t size = fwcfg_open(fw);
    if (!size) fwcfg_libera();
    char fat[128]; char dst[160]; int from_fw = size != 0;
    if (!from_fw) {
        struct fat_dirent *d = (struct fat_dirent *)kmalloc(48 * sizeof *d);
        if (!d) { sput(msg, cap, &p, "sin memoria"); return -1; }
        const char *why; int n = usbdisk_bootdir("MODELOS", d, 48, &why);
        int pick = -1;
        for (int i = 0; i < n; ++i) if (!d[i].is_dir && d[i].name[0] != '.' && ends_with(d[i].name, ".gguf") && contiene(d[i].name, nombre)) { pick = i; break; }
        if (pick < 0) { kfree(d); sput(msg, cap, &p, "no hay ningun .gguf con \""); sput(msg, cap, &p, nombre); sput(msg, cap, &p, "\" en la carpeta MODELOS del pendrive"); return -1; }
        q = 0; sput(fat, sizeof fat, &q, "MODELOS/"); sput(fat, sizeof fat, &q, d[pick].name);
        q = 0; sput(dst, sizeof dst, &q, IA_DIR "/"); sput(dst, sizeof dst, &q, d[pick].name);
        size = d[pick].size;
        kfree(d);
    } else {
        q = 0; sput(dst, sizeof dst, &q, IA_DIR "/"); sput(dst, sizeof dst, &q, nombre);
        if (!ends_with(dst, ".gguf")) sput(dst, sizeof dst, &q, ".gguf");
    }
    uint32_t ino;
    if (bigfs_lookup(fs, IA_DIR, &ino) != (int)BIGFS_T_DIR && bigfs_mkdir(fs, IA_DIR) < 0) { sput(msg, cap, &p, "no se pudo crear " IA_DIR); return -1; }
    if (bigfs_lookup(fs, dst, &ino) >= 0) bigfs_unlink(fs, dst);
    int r = bigfs_create(fs, dst, &ino);
    if (r < 0) { sput(msg, cap, &p, "no se pudo crear "); sput(msg, cap, &p, dst); sput(msg, cap, &p, ": "); sput(msg, cap, &p, bigfs_strerror(r)); return -1; }
    const uint32_t CH = 1u << 20;
    uint8_t *buf = (uint8_t *)kmalloc(CH);
    if (!buf) { bigfs_unlink(fs, dst); sput(msg, cap, &p, "sin memoria"); return -1; }
    uint32_t hecho = 0;
    while (hecho < size) {
        uint32_t n = size - hecho < CH ? size - hecho : CH;
        if (from_fw) fwcfg_read_seq(buf, n);
        else {
            const char *why; int k = usbdisk_bootfile_read(fat, hecho, buf, n, 0, &why);
            if (k <= 0) { kfree(buf); bigfs_unlink(fs, dst); sput(msg, cap, &p, "error leyendo el pendrive: "); sput(msg, cap, &p, why); return -1; }
            n = (uint32_t)k;
        }
        if (hecho == 0 && (n < 4 || buf[0] != 'G' || buf[1] != 'G' || buf[2] != 'U' || buf[3] != 'F')) {
            kfree(buf); bigfs_unlink(fs, dst); sput(msg, cap, &p, "el archivo no es un modelo GGUF"); return -1;
        }
        int64_t w = bigfs_write(fs, ino, hecho, buf, n);
        if (w != (int64_t)n) { kfree(buf); bigfs_unlink(fs, dst); sput(msg, cap, &p, "error escribiendo en el almacen"); return -1; }
        hecho += n;
        thread_yield();
    }
    kfree(buf);
    sput(msg, cap, &p, "modelo instalado en "); sput(msg, cap, &p, dst); sput(msg, cap, &p, " ("); sdec(msg, cap, &p, size >> 20); sput(msg, cap, &p, " MiB)");
    return 0;
}

int ia_instala(const char *nombre, char *msg, uint32_t cap)
{
    int r = instala(nombre, msg, cap);
    fwcfg_libera();
    return r;
}

/* ---- orden "ia" del terminal */
static const char *palabra(const char *s, char *w, uint32_t cap)
{
    while (*s == ' ') s++;
    uint32_t k = 0; while (*s && *s != ' ' && k + 1 < cap) w[k++] = *s++;
    w[k] = 0;
    while (*s == ' ') s++;
    return s;
}
static void json_cadena(char *o, uint32_t cap, uint32_t *p, const char *s)
{
    sput(o, cap, p, "\"");
    for (; *s && *p + 8 < cap; ++s) {
        char c = *s;
        if (c == '"' || c == '\\') { o[(*p)++] = '\\'; o[(*p)++] = c; }
        else if (c == '\n') { o[(*p)++] = '\\'; o[(*p)++] = 'n'; }
        else if ((unsigned char)c < 32) o[(*p)++] = ' ';
        else o[(*p)++] = c;
    }
    o[*p] = 0;
    sput(o, cap, p, "\"");
}
static int ia_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
int ia_cmd(const char *args, char *out, uint32_t cap)
{
    uint32_t p = 0;
    char w[32], m[48];
    const char *r = palabra(args ? args : "", w, sizeof w);
    if (!w[0] || !ia_strcmp(w, "estado")) { ia_estado(out, cap); return 1; }
    if (!ia_strcmp(w, "modelos")) {
        int n = ia_modelos(out, cap);
        if (!n) sput(out, cap, &p, "no hay modelos .gguf en /modelos (ia instalar <nombre> los copia de la carpeta MODELOS del pendrive)\n");
        return 1;
    }
    if (!ia_strcmp(w, "hilos")) {                                       /* SOMA_361_HILOS_IA: ia hilos [N|auto] */
        r = palabra(r, m, sizeof m);
        if (m[0]) {
            uint32_t v = 0; int ok = 1;
            if (!ia_strcmp(m, "auto")) v = 0;
            else { for (int i = 0; m[i]; i++) { if (m[i] < '0' || m[i] > '9') { ok = 0; break; } v = v * 10 + (uint32_t)(m[i] - '0'); if (v > 99) break; } }
            if (!ok || v > 8) { sput(out, cap, &p, "uso: ia hilos [1..8|auto]\n"); return 1; }
            g_hilos_conf = v;
        }
        sput(out, cap, &p, "hilos de calculo del motor de IA: ");
        if (g_hilos_conf) sdec(out, cap, &p, g_hilos_conf); else { sput(out, cap, &p, "automatico (ahora "); sdec(out, cap, &p, hilos_para_orden()); sput(out, cap, &p, ")"); }
        sput(out, cap, &p, "; nunca mas que nucleos libres + 1. Vale desde la siguiente orden (no se guarda al reiniciar)\n");
        return 1;
    }
    if (!ia_strcmp(w, "parar")) { ia_para(); sput(out, cap, &p, "motor de IA parado (memoria liberada)\n"); return 1; }
    if (!ia_strcmp(w, "instalar")) {
        static char msg[256]; r = palabra(r, m, sizeof m);
        int k = ia_instala(m, msg, sizeof msg);
        sput(out, cap, &p, msg); sput(out, cap, &p, "\n");
        return k == 0 ? 1 : 1;
    }
    if (!ia_strcmp(w, "chat") || !ia_strcmp(w, "embed") || !ia_strcmp(w, "tokens") || !ia_strcmp(w, "completa") || !ia_strcmp(w, "huella")) {
        r = palabra(r, m, sizeof m);
        static char ruta[160], err[220];
        if (ia_modelo_ruta(m, ruta, sizeof ruta) != 0) { sput(out, cap, &p, "no encuentro el modelo \""); sput(out, cap, &p, m); sput(out, cap, &p, "\" en /modelos\n"); return 1; }
        static char in[4096];
        uint32_t q = 0; int cmd = IA_CMD_TOKENIZA;
        if (w[0] == 'c' && w[1] == 'o') {               /* texto tal cual (sin plantilla de chat), voraz: para pruebas */
            cmd = IA_CMD_CHAT;
            sput(in, sizeof in, &q, "{\"prompt\":"); json_cadena(in, sizeof in, &q, r); sput(in, sizeof in, &q, ",\"max_tokens\":8,\"temperature\":0}");
        } else if (w[0] == 'c') {
            cmd = IA_CMD_CHAT;
            sput(in, sizeof in, &q, "{\"messages\":[{\"role\":\"user\",\"content\":"); json_cadena(in, sizeof in, &q, r); sput(in, sizeof in, &q, "}],\"max_tokens\":256}");
        } else if (w[0] == 'h') cmd = IA_CMD_HUELLA;
        else { if (w[0] == 'e') cmd = IA_CMD_EMBED; sput(in, sizeof in, &q, r); }
        uint64_t t0 = timer_get_uptime_ms();
        int n = ia_orden(cmd, ruta, in, q, out, cap, 10u * 60u * 1000u, 0, err, sizeof err);
        if (n < 0) { p = 0; sput(out, cap, &p, "ia: "); sput(out, cap, &p, err); sput(out, cap, &p, "\n"); return 1; }
        p = (uint32_t)n;
        sput(out, cap, &p, "\n["); sdec(out, cap, &p, timer_get_uptime_ms() - t0); sput(out, cap, &p, " ms]\n");
        return 1;
    }
    sput(out, cap, &p, "uso: ia [estado] | ia modelos | ia instalar <nombre> | ia chat <modelo> <texto> | ia completa <modelo> <texto> | ia embed <modelo> <texto> | ia tokens <modelo> <texto> | ia hilos [N|auto] | ia parar\n");
    return 1;
}
