/* SOMA_202: lado del NUCLEO del navegador aislado (ver src/core/web_r3_abi.h y src/user/websvc.c).
 *
 * Lanza el proceso del motor, le presta cuatro regiones de memoria, le pasa ordenes por una cola y recoge de vuelta el estado de
 * la pagina y los fotogramas ya pintados. Todo lo que viene del proceso se lee por los marcos fisicos de las regiones (nunca por
 * punteros suyos), con tamanos acotados y cadenas terminadas a la fuerza. Si el proceso muere, se nota y se vuelve a lanzar. */
#include <stdint.h>
#include <stddef.h>
#include "web_r3.h"
#include "usermode.h"
#include "sandbox.h"
#include "vmm.h"
#include "pmm.h"
#include "mem.h"
#include "thread.h"
#include "acct.h"
#include "fs.h"
#include "fb.h"
#include "console.h"
#include "rng.h"
#include "net.h"
#include "tls13.h"
#include "tls_trust.h"

uint64_t timer_get_uptime_ms(void);
#include "progz.h"
PROGZ_DECL(websvc);                                         /* SOMA_314_PROGZ: websvc.elf va comprimido en la imagen */

#define Q_N 64
struct qcmd { uint32_t seq, op, len; int32_t a[8]; char *data; };
static struct qcmd q[Q_N]; static volatile uint32_t q_h, q_t;
static uint32_t g_seq;
static struct uregion r_in, r_out, r_heap, r_surf;
static uint32_t g_heap_bytes;                       /* SOMA_341_NAVEGADOR_MEMORIA: monton concedido al proceso en este arranque */
static uint64_t pmm_libre_bytes(void) { size_t fr = 0, us = 0, tot = 0; pmm_get_stats(&fr, &us, &tot); return (uint64_t)fr * 4096u; }
static int g_idx = -1; static volatile int g_up;
static volatile int g_stop_req;
static struct w3_state *g_st;                       /* copia validada del estado (sin la zona de respuestas) */
static uint8_t *g_reply; static uint32_t g_reply_len; static int g_reply_status; static uint32_t g_reply_seq;
static uint32_t *g_fb; static uint32_t g_fb_cap; static volatile int g_fw, g_fh, g_fsx, g_fsy; static volatile uint32_t g_fseq;
static uint32_t g_starts, g_deaths; static int64_t g_last_exit;
static void (*g_log_cb)(int level, const char *msg);
static volatile uint64_t g_last_sys; static volatile int g_in_wait;   /* perro guardian: el proceso sano llama al nucleo varias veces por segundo */
#define W3_HANG_MS 20000
/* SOMA_203 */
static uint32_t g_fflags, g_paint_ms, g_paint_sum, g_paint_n;
static int32_t g_acc_seen, g_scroll_pend;           /* desplazamientos pedidos por el motor: suma vista y parte aun sin recoger */
static uint8_t *g_save; static uint32_t g_save_len, g_save_cap, g_saves; static uint64_t g_save_ms;   /* cookies + almacenamiento del ultimo proceso */
static uint32_t g_blob_seq; static uint8_t *g_blob; static uint32_t g_blob_len;                        /* pixeles pedidos con W3C_GET_IMG */
static int g_vw_ok[2], g_vh_ok[2];                  /* tamanos de vista pedidos por el nucleo (el actual y el anterior): un fotograma no puede ser mayor */
static volatile int g_local_ok;                     /* la pagina pedida por el USUARIO es local: solo entonces el proceso puede pedir archivos */
static int url_is_local(const char *u) { return u && ((u[0] == 'f' && u[1] == 'i' && u[2] == 'l' && u[3] == 'e' && u[4] == ':') || (u[0] == 'a' && u[1] == 'b' && u[2] == 'o' && u[3] == 'u' && u[4] == 't' && u[5] == ':') || u[0] == '/'); }
#define ST_HDR ((uint32_t)offsetof(struct w3_state, reply))

/* ---------------------------------------------------------------- regiones (los marcos estan mapeados 1:1 en el nucleo) */
static void reg_free(struct uregion *r)
{
    if (r->frames) { for (uint32_t k = 0; k < r->npages; ++k) if (r->frames[k]) pmm_free_frame((uintptr_t)r->frames[k]); kfree(r->frames); }
    r->frames = 0; r->npages = 0; r->used = 0;
}
static int reg_init(struct uregion *r, uint64_t va, uint32_t npages, uint64_t flags, int demand)
{
    r->va = va; r->npages = npages; r->used = 0; r->flags = flags; r->demand = demand;
    r->frames = (uint64_t *)kmalloc((size_t)npages * sizeof(uint64_t));
    if (!r->frames) { r->npages = 0; return -1; }
    memset(r->frames, 0, (size_t)npages * sizeof(uint64_t));
    if (demand) return 0;
    for (uint32_t k = 0; k < npages; ++k) { uintptr_t f = pmm_alloc_frame(); if (!f) return -1; memset((void *)f, 0, 4096); r->frames[k] = f; r->used++; }
    return 0;
}
/* lee de una region; las paginas que el proceso nunca toco valen cero */
static void reg_read(const struct uregion *r, uint64_t off, void *dst, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n) {
        uint64_t k = off >> 12, o = off & 0xFFF, c = 4096 - o; if (c > n) c = n;
        if (k < r->npages && r->frames[k]) memcpy(d, (const uint8_t *)(uintptr_t)r->frames[k] + o, c); else memset(d, 0, c);
        d += c; off += c; n -= c;
    }
}
/* escribe en la region de entrada reservando y mapeando (solo lectura para el proceso) las paginas que falten */
static int in_write(uint64_t off, const void *src, uint64_t n)
{
    uint64_t as = usermode_proc_as(g_idx);
    if (!as || off + n > (uint64_t)r_in.npages * 4096u) return -1;
    const uint8_t *s = (const uint8_t *)src;
    while (n) {
        uint64_t k = off >> 12, o = off & 0xFFF, c = 4096 - o; if (c > n) c = n;
        if (!r_in.frames[k]) {
            uintptr_t f = pmm_alloc_frame(); if (!f) return -1;
            memset((void *)f, 0, 4096);
            if (vmm_as_map(as, r_in.va + k * 4096, f, r_in.flags) != 0) { pmm_free_frame(f); return -1; }
            r_in.frames[k] = f; r_in.used++;
        }
        memcpy((uint8_t *)(uintptr_t)r_in.frames[k] + o, s, c);
        s += c; off += c; n -= c;
    }
    return 0;
}

/* ---------------------------------------------------------------- vida del proceso */
static void q_clear(void) { while (q_h != q_t) { struct qcmd *c = &q[q_h % Q_N]; if (c->data) kfree(c->data); c->data = 0; q_h++; } }
static void teardown(void)
{
    reg_free(&r_in); reg_free(&r_out); reg_free(&r_heap); reg_free(&r_surf);
    g_idx = -1; g_up = 0; q_clear();
}
int web_r3_alive(void)
{
    if (!g_up) return 0;
    if (usermode_proc_alive(g_idx) && !g_in_wait && g_last_sys && timer_get_uptime_ms() - g_last_sys > W3_HANG_MS) {
        kprint("[WEB-R3] el proceso del navegador no responde (bucle sin fin?): se termina\n");
        uint32_t tid = usermode_proc_tid(g_idx); if (tid) thread_kill(tid);
    }
    if (usermode_proc_alive(g_idx)) return 1;
    g_last_exit = usermode_wait(g_idx);             /* murio: fallo, memoria agotada o llamada no permitida */
    g_deaths++;
    kprint("[WEB-R3] el proceso del navegador ha terminado (codigo "); if (g_last_exit < 0) { kprint("-"); kprint_dec((uint32_t)(-g_last_exit)); } else kprint_dec((uint32_t)g_last_exit); kprint(")\n");
    teardown();
    return 0;
}
void web_r3_stop(void)
{
    if (!g_up) return;
    if (usermode_proc_alive(g_idx)) { uint32_t tid = usermode_proc_tid(g_idx); if (tid) thread_kill(tid); }
    (void)usermode_wait(g_idx);
    teardown();
}
int web_r3_start(int vw, int vh)
{
    if (web_r3_alive()) return 0;
    const uint64_t NXR = VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_NX, NXW = NXR | VMM_FLAG_WRITABLE;
    if (!g_st) g_st = (struct w3_state *)kmalloc(ST_HDR);
    if (!g_reply) g_reply = (uint8_t *)kmalloc(W3_REPLY_MAX + 1);
    if (!g_st || !g_reply) return -1;
    memset(g_st, 0, ST_HDR); g_reply_len = 0; g_fseq = 0; g_fw = g_fh = 0; g_stop_req = 0; g_acc_seen = 0; g_scroll_pend = 0; g_blob_seq = 0;
    if (reg_init(&r_in, W3_IN_VA, W3_IN_MAX / 4096u, NXR, 1) || reg_init(&r_out, W3_OUT_VA, W3_OUT_PAGES, NXW, 0) ||
        reg_init(&r_heap, W3_HEAP_VA, (g_heap_bytes = w3_heap_bytes(pmm_libre_bytes())) / 4096u, NXW, 1) || reg_init(&r_surf, W3_SURF_VA, (uint32_t)(W3_SURF_MAX / 4096u), NXW, 1)) { teardown(); return -1; }
    r_in.demand = 0;                                /* la entrada solo crece cuando el NUCLEO escribe en ella */
    { uintptr_t f = pmm_alloc_frame(); if (!f) { teardown(); return -1; } memset((void *)f, 0, 4096); r_in.frames[0] = f; r_in.used = 1; }
    struct uexec_spec sp; memset(&sp, 0, sizeof sp);
    sp.sysmask = (1u << SYS_EXIT) | (1u << SYS_YIELD) | (1u << SYS_SLEEP) | (1u << SYS_UPTIME_MS) | USER_SYS_NET_MASK | USER_SYS_WEB_MASK;
    sp.regions[0] = &r_in; sp.regions[1] = &r_out; sp.regions[2] = &r_heap; sp.regions[3] = &r_surf; sp.nregions = 4;
    sp.reserve_frames = SBX_RESERVE_FRAMES;
    int idx = -2, err = 0;
    struct progz pz;                                         /* SOMA_314_PROGZ: se descomprime, se lanza y se libera */
    if (progz_abre(PROGZ(websvc), "navegador", &pz) != 0) idx = -1;
    else {
        for (int tries = 0; tries < 200 && idx == -2; ++tries) { idx = usermode_exec_ex("navegador", pz.elf, pz.len, 0x7FFFFF00u, &sp, &err); if (idx == -2) thread_sleep(5); }
        progz_cierra(&pz);
    }
    if (idx < 0) { kprint("[WEB-R3] no se pudo lanzar el proceso del navegador (error "); kprint_dec((uint32_t)(-err)); kprint(")\n"); teardown(); return -1; }
    g_idx = idx; g_up = 1; g_starts++; g_last_sys = timer_get_uptime_ms(); g_in_wait = 0;
    int32_t a[5] = { vw, vh, tls13_verify, (int32_t)(((uint32_t)net_ip[0] << 24) | ((uint32_t)net_ip[1] << 16) | ((uint32_t)net_ip[2] << 8) | net_ip[3]),
                     (int32_t)(g_heap_bytes >> 20) };
    tls_trust_setup();
    uint32_t s = web_r3_send(W3C_INIT, a, 5, 0, 0);
    if (!s || web_r3_wait(s, 5000) != 1) { kprint("[WEB-R3] el proceso del navegador no arranca\n"); web_r3_stop(); return -1; }
    return 0;
}

/* ---------------------------------------------------------------- ordenes */
uint32_t web_r3_send(int op, const int32_t *a, int na, const void *data, uint32_t len)
{
    if (!g_up || len > W3_CMD_DATA_MAX) return 0;
    if ((op == W3C_INIT || op == W3C_VIEWPORT) && na >= 2) { g_vw_ok[1] = g_vw_ok[0]; g_vh_ok[1] = g_vh_ok[0]; g_vw_ok[0] = a[0]; g_vh_ok[0] = a[1]; }
    if (op == W3C_NAVIGATE) { char u[8]; uint32_t i = 0; for (; i < 7 && i < len; i++) u[i] = ((const char *)data)[i]; u[i] = 0; g_local_ok = url_is_local(u); }
    if (op == W3C_HISTORY) g_local_ok = (na >= 2 && a[1] == 1);          /* a[1] = 1: la interfaz sabe que esa entrada del historial es local */
    struct qcmd *c;
    if (q_h != q_t && (op == W3C_MOVE || op == W3C_SCROLL || op == W3C_VIEWPORT) && q[(q_t - 1) % Q_N].op == (uint32_t)op) c = &q[(q_t - 1) % Q_N];   /* solo cuenta la ultima */
    else {
        if (q_t - q_h >= Q_N) return 0;
        c = &q[q_t % Q_N]; c->data = 0; c->len = 0; c->seq = ++g_seq; if (!c->seq) c->seq = ++g_seq;
        c->op = (uint32_t)op;
        if (len) { c->data = (char *)kmalloc(len); if (!c->data) return 0; memcpy(c->data, data, len); c->len = len; }
        memset(c->a, 0, sizeof c->a);
        for (int i = 0; i < na && i < 8; i++) c->a[i] = a[i];
        q_t++;
        uint32_t tid = usermode_proc_tid(g_idx); if (tid) thread_wake(tid);
        return c->seq;
    }
    for (int i = 0; i < na && i < 8; i++) c->a[i] = a[i];
    return c->seq;
}
int web_r3_wait(uint32_t seq, uint32_t timeout_ms)
{
    uint64_t t0 = timer_get_uptime_ms();
    for (;;) {
        if (g_st && (int32_t)(g_st->seq_done - seq) >= 0) return 1;
        if (!web_r3_alive()) return -1;
        if (timer_get_uptime_ms() - t0 >= timeout_ms) return 0;
        thread_sleep(2);
    }
}
int web_r3_call(int op, const int32_t *a, int na, const void *data, uint32_t len, void *out, uint32_t max, uint32_t timeout_ms)
{
    uint32_t s = web_r3_send(op, a, na, data, len);
    if (!s || web_r3_wait(s, timeout_ms) != 1 || g_reply_seq != s) return -1;
    uint32_t n = g_reply_len < max ? g_reply_len : max;
    if (out && n) memcpy(out, g_reply, n);
    return g_reply_status ? (int)n : -1;
}
void web_r3_request_stop(void) { g_stop_req = 1; uint32_t tid = g_up ? usermode_proc_tid(g_idx) : 0; if (tid) thread_wake(tid); }
int  web_r3_pending(void) { return q_h != q_t; }
const struct w3_state *web_r3_state(void) { return g_st; }
void web_r3_set_log(void (*cb)(int, const char *)) { g_log_cb = cb; }
void web_r3_stats(uint32_t *starts, uint32_t *deaths, int64_t *last_exit, uint32_t *heap_pages, uint32_t *surf_pages)
{
    if (starts) *starts = g_starts;
    if (deaths) *deaths = g_deaths;
    if (last_exit) *last_exit = g_last_exit;
    if (heap_pages) *heap_pages = r_heap.used;
    if (surf_pages) *surf_pages = r_surf.used;
}

/* SOMA_203: pixeles de una imagen de la pagina (Guardar imagen / Copiar imagen). want_px = 0: solo comprobar que esta y su
 * direccion completa. Los pixeles quedan en un buffer del nucleo hasta la siguiente orden con respuesta o web_r3_image_free(). */
void web_r3_image_free(void) { if (g_blob) { kfree(g_blob); g_blob = 0; g_blob_len = 0; } }
int web_r3_get_image(const char *src, int want_px, const uint32_t **px, int *w, int *h, char *abs, uint32_t absmax)
{
    static struct w3_img_reply ir; int32_t a[1] = { want_px ? 1 : 0 };
    uint32_t n = 0; while (src && src[n]) n++;
    if (!n || n >= 2048 || !web_r3_alive()) return 0;
    uint32_t s = web_r3_send(W3C_GET_IMG, a, 1, src, n);
    if (!s) return 0;
    if (want_px) { web_r3_image_free(); g_blob_seq = s; }
    if (web_r3_wait(s, 2500) != 1 || g_reply_seq != s || g_reply_len != sizeof ir) { if (g_blob_seq == s) g_blob_seq = 0; return 0; }
    memcpy(&ir, g_reply, sizeof ir); ir.abs[sizeof ir.abs - 1] = 0;
    if (abs && absmax) { uint32_t i = 0; for (; ir.abs[i] && i + 1 < absmax; i++) abs[i] = ir.abs[i]; abs[i] = 0; }
    if (!ir.ok || ir.w <= 0 || ir.h <= 0 || (uint64_t)ir.w * (uint64_t)ir.h * 4u > W3_BLOB_MAX) return 0;
    if (want_px && (!g_blob || g_blob_len != (uint32_t)ir.w * (uint32_t)ir.h * 4u)) return 0;      /* las medidas tienen que cuadrar con lo copiado */
    if (px) *px = (const uint32_t *)(const void *)g_blob;
    if (w) *w = ir.w;
    if (h) *h = ir.h;
    return 1;
}
void web_r3_stats2(uint32_t *frames, uint32_t *paint_last, uint32_t *paint_avg, uint32_t *frame_h, uint32_t *saves, uint32_t *save_len)
{
    if (frames) *frames = g_paint_n;
    if (paint_last) *paint_last = g_paint_ms;
    if (paint_avg) *paint_avg = g_paint_n ? g_paint_sum / g_paint_n : 0;
    if (frame_h) *frame_h = (uint32_t)g_fh;
    if (saves) *saves = g_saves;
    if (save_len) *save_len = g_save_len;
}

/* ---------------------------------------------------------------- fotogramas */
uint32_t web_r3_frame_seq(void) { return g_fseq; }
/* Dibuja el ultimo fotograma en (x, y), recortado a w x h. Si la pagina se ha desplazado desde que se pinto, se dibuja corrido
 * (lo que falta se ve en blanco hasta que llega el siguiente). */
void web_r3_blit(int x, int y, int w, int h, int sx, int sy)
{
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, 0x00FFFFFF);
    if (!g_fb || !g_fw || !g_fh) return;
    int dx = g_fsx - sx, dy = g_fsy - sy;
    if (g_fflags & W3F_FIXED) dx = dy = 0;          /* SOMA_203: con elementos fijos el fotograma no se corre: se espera al siguiente */
    int row0 = dy < 0 ? -dy : 0, rows = g_fh - row0;                 /* SOMA_203: el fotograma puede ser mas alto que la vista: solo las filas que se ven */
    if (dy < 0) dy = 0;
    if (rows > h - dy) rows = h - dy;
    int32_t c0, c1, c2, c3; fb_get_clip(&c0, &c1, &c2, &c3);
    int nx0 = x > c0 ? x : c0, ny0 = y > c1 ? y : c1, nx1 = x + w < c2 ? x + w : c2, ny1 = y + h < c3 ? y + h : c3;
    if (nx1 > nx0 && ny1 > ny0 && rows > 0) {
        fb_set_clip(nx0, ny0, nx1 - nx0, ny1 - ny0);
        fb_blit(x + dx, y + dy, g_fb + (size_t)row0 * (size_t)g_fw, g_fw, rows, g_fw);
        fb_set_clip(c0, c1, c2 - c0, c3 - c1);
    }
}

/* ---------------------------------------------------------------- llamadas al sistema del proceso */
static int mine(int proc_idx)
{
    if (!g_up || proc_idx != g_idx) return 0;
    struct tcb *t = thread_current(); if (t) t->owner = ACCT_WEB;      /* para el sistema de archivos este hilo ES el navegador (parche 198) */
    g_last_sys = timer_get_uptime_ms();
    return 1;
}
int64_t web_r3_sys_wait(int proc_idx, int64_t ms)
{
    if (!mine(proc_idx)) return -1;
    if (ms < 0) { if (g_stop_req) { g_stop_req = 0; return 2; } return 0; }
    if (ms > 1000) ms = 1000;
    uint64_t t0 = timer_get_uptime_ms();
    for (;;) {
        if (g_stop_req) { g_stop_req = 0; return 2; }
        if (q_h != q_t) {
            struct qcmd *c = &q[q_h % Q_N];
            static struct w3_cmd hd; static const char zero = 0;
            memset(&hd, 0, sizeof hd);
            hd.magic = W3_MAGIC; hd.seq = c->seq; hd.op = c->op; hd.len = c->len; memcpy(hd.a, c->a, sizeof hd.a);
            if (c->op == W3C_INIT) { struct tls_trust_info ti; tls_trust_get_info(&ti); hd.u64 = ti.now; }
            rng_bytes(hd.seed, sizeof hd.seed);
            int bad = in_write(0, &hd, sizeof hd) || (c->len && in_write(sizeof hd, c->data, c->len)) || in_write(sizeof hd + c->len, &zero, 1);
            if (c->data) kfree(c->data);
            c->data = 0; q_h++;
            if (bad) return -1;                    /* sin memoria para la orden: el proceso la vera como "sin orden" y seguira */
            return 1;
        }
        uint64_t el = timer_get_uptime_ms() - t0;
        if (el >= (uint64_t)ms) { g_last_sys = timer_get_uptime_ms(); return 0; }
        g_in_wait = 1;
        thread_sleep((uint64_t)ms - el < 20 ? (uint32_t)((uint64_t)ms - el) : 20);
        g_in_wait = 0; g_last_sys = timer_get_uptime_ms();
    }
}
static void clampi(int32_t *v, int lo, int hi) { if (*v < lo) *v = lo; if (*v > hi) *v = hi; }
int64_t web_r3_sys_post(int proc_idx, uint64_t what, uint64_t a, uint64_t b)
{
    if (!mine(proc_idx)) return -1;
    static struct w3_state tmp_guard;               /* solo por su tamano de cabecera: se lee a una copia y se valida antes de publicarla */
    (void)tmp_guard;
    if (what == W3P_FRAME) {
        if (a == 0 || b == 0 || a > W3_MAX_W) return -1;
        if (a == 0 || b > W3_FRAME_MAX_H || a * b * 4u > W3_SURF_MAX) return -1;
        if ((int)a > (g_vw_ok[0] > g_vw_ok[1] ? g_vw_ok[0] : g_vw_ok[1]) || (int)b > 2 * (g_vh_ok[0] > g_vh_ok[1] ? g_vh_ok[0] : g_vh_ok[1])) return -1;   /* mayor que la vista que pidio el nucleo (SOMA_203: hasta el doble de alto, por el margen de desplazamiento) */
        uint32_t need = (uint32_t)a * (uint32_t)b;
        if (need > g_fb_cap) {
            uint32_t *nb = (uint32_t *)kmalloc((size_t)need * 4u);
            if (!nb) return -1;
            if (g_fb) kfree(g_fb);
            g_fb = nb; g_fb_cap = need;
        }
        reg_read(&r_surf, 0, g_fb, (uint64_t)need * 4u);
        int32_t meta[6]; reg_read(&r_out, offsetof(struct w3_state, frame_w), meta, sizeof meta);
        clampi(&meta[2], 0, 0x3FFFFFFF); clampi(&meta[3], 0, 0x3FFFFFFF);
        g_fw = (int)a; g_fh = (int)b; g_fsx = meta[2]; g_fsy = meta[3]; g_fflags = (uint32_t)meta[4] & W3F_FIXED; g_fseq++;
        g_paint_ms = (uint32_t)meta[5] > 60000u ? 60000u : (uint32_t)meta[5]; g_paint_sum += g_paint_ms; g_paint_n++;
        return 0;
    }
    if (what == W3P_SAVE) {                         /* SOMA_203: copia de cookies y almacenamiento; el nucleo la guarda sin mirarla */
        uint64_t now = timer_get_uptime_ms();
        if (b < 8 || b > W3_SAVE_MAX || a < W3_HEAP_VA || a - W3_HEAP_VA > (uint64_t)r_heap.npages * 4096u - b) return -1;
        if (g_save_ms && now - g_save_ms < 250) return -1;
        if (b > g_save_cap) {
            uint32_t cap = ((uint32_t)b + 0xFFFFu) & ~0xFFFFu; uint8_t *nb = (uint8_t *)kmalloc(cap);
            if (!nb) return -1;
            if (g_save) kfree(g_save);
            g_save = nb; g_save_cap = cap;
        }
        reg_read(&r_heap, a - W3_HEAP_VA, g_save, b);
        g_save_len = (uint32_t)b; g_save_ms = now ? now : 1; g_saves++;
        return 0;
    }
    if (what != W3P_STATE || !g_st) return -1;
    static struct w3_state *in;                     /* copia de trabajo (solo la cabecera) */
    if (!in) in = (struct w3_state *)kmalloc(ST_HDR);
    if (!in) return -1;
    reg_read(&r_out, 0, in, ST_HDR);
    if (in->magic != W3_MAGIC) return -1;
    in->url[sizeof in->url - 1] = 0; in->title[sizeof in->title - 1] = 0; in->status[sizeof in->status - 1] = 0; in->hover_href[sizeof in->hover_href - 1] = 0;
    in->alert_msg[sizeof in->alert_msg - 1] = 0; in->log_msg[sizeof in->log_msg - 1] = 0; in->fs_path[sizeof in->fs_path - 1] = 0;
    clampi(&in->dd_count, 0, W3_DD_MAX); clampi(&in->vw, 0, W3_MAX_W); clampi(&in->vh, 0, W3_MAX_H); clampi(&in->hn, 0, 64); clampi(&in->hpos, -1, 63);
    clampi(&in->doc_h, 0, 0x3FFFFFFF); clampi(&in->sx, 0, 0x3FFFFFFF); clampi(&in->sy, 0, 0x3FFFFFFF); clampi(&in->phase, 0, 16); clampi(&in->progress, 0, 100);
    for (int i = 0; i < W3_DD_MAX; i++) in->dd_opt[i][sizeof in->dd_opt[i] - 1] = 0;
    if (in->reply_len > W3_REPLY_MAX) in->reply_len = W3_REPLY_MAX;
    if (in->seq_done != g_st->seq_done) {           /* termino una orden: su respuesta (si la hay) va con ella */
        g_reply_len = in->reply_len; g_reply_status = in->reply_status; g_reply_seq = in->seq_done;
        if (g_reply_len) reg_read(&r_out, ST_HDR, g_reply, g_reply_len);
        g_reply[g_reply_len] = 0;
        if (g_blob_seq && g_blob_seq == in->seq_done) {                 /* SOMA_203: pixeles, solo si el nucleo los pidio con esta orden */
            if (g_blob) { kfree(g_blob); g_blob = 0; g_blob_len = 0; }
            if (in->blob_len && in->blob_len <= W3_BLOB_MAX && in->blob_va >= W3_HEAP_VA && in->blob_va - W3_HEAP_VA <= (uint64_t)r_heap.npages * 4096u - in->blob_len) {
                g_blob = (uint8_t *)kmalloc(in->blob_len);
                if (g_blob) { reg_read(&r_heap, in->blob_va - W3_HEAP_VA, g_blob, in->blob_len); g_blob_len = in->blob_len; }
            }
            g_blob_seq = 0;
        }
    }
    { int32_t d = (int32_t)((uint32_t)in->scroll_req - (uint32_t)g_acc_seen); g_acc_seen = in->scroll_req;      /* SOMA_203 */
      clampi(&d, -(1 << 20), 1 << 20); g_scroll_pend += d; clampi(&g_scroll_pend, -(1 << 22), 1 << 22); }
    memcpy(g_st, in, ST_HDR);
    g_st->scroll_req = g_scroll_pend;               /* se conserva hasta que la interfaz lo recoge */
    g_st->inner_scroll = !!g_st->inner_scroll;
    if (g_st->url[0] && !url_is_local(g_st->url)) g_local_ok = 0;      /* la pagina ya es de la red: se acabaron los archivos locales */
    if (in->log_level && g_log_cb) g_log_cb((int)in->log_level, in->log_msg);
    return 0;
}
void web_r3_scroll_req_done(void) { g_scroll_pend = 0; if (g_st) g_st->scroll_req = 0; }
int64_t web_r3_sys_fs(int proc_idx, uint64_t what)
{
    if (!mine(proc_idx)) return -1;
    if (what == 1) return (g_save && g_save_len && in_write(W3_FILE_OFF, g_save, g_save_len) == 0) ? (int64_t)g_save_len : -1;   /* SOMA_203 */
    if (what != 0) return -1;
    char path[512]; reg_read(&r_out, offsetof(struct w3_state, fs_path), path, sizeof path); path[sizeof path - 1] = 0;
    if (path[0] != '/') return -1;
    /* Archivos locales solo mientras la pagina que pidio el USUARIO es local (file:, about:). Un proceso comprometido por una pagina
     * de la red no puede leer el disco. La unica excepcion son las autoridades de certificacion anadidas por el usuario (publicas). */
    static const char pem[] = "/etc/tls_confianza.pem"; int is_pem = 1;
    for (uint32_t i = 0; i < sizeof pem; i++) if (path[i] != pem[i]) { is_pem = 0; break; }
    if (!is_pem && !g_local_ok) return -1;
    uint32_t len = 0; char *d = vfs_read_alloc(path, &len);          /* y aun asi con la politica del hilo del navegador: sin secretos */
    if (!d) return -1;
    int64_t r = -1;
    if (len && len <= W3_FILE_MAX && in_write(W3_FILE_OFF, d, len) == 0) r = (int64_t)len;
    kfree(d);
    return r;
}

/* ---------------------------------------------------------------- prueba sin interfaz: `webr3 <url>` */
int web_r3_selftest(const char *url)
{
    kprint("WEBR3: motor del navegador en ring 3 -> "); kprint(url); kprint("\n");
    uint64_t t0 = timer_get_uptime_ms();
    if (web_r3_start(1024, 700) != 0) { kprint("WEBR3: FALLO (no arranca)\n"); return 1; }
    kprint("  proceso en marcha en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
    int32_t a[1] = { 0 }; uint32_t n = 0; while (url[n]) n++;
    uint32_t s = web_r3_send(W3C_NAVIGATE, a, 1, url, n);
    int r = s ? web_r3_wait(s, 45000) : -1;
    if (r != 1) { kprint(r < 0 ? "WEBR3: FALLO (el proceso murio durante la carga)\n" : "WEBR3: FALLO (la carga no termino en 45 s)\n"); return 1; }
    uint64_t t1 = timer_get_uptime_ms(); uint32_t fs = 0;
    while (timer_get_uptime_ms() - t1 < 90000) {                      /* espera a que se asiente (imagenes, scripts en turnos); SOMA_341: hasta 90 s, sale en cuanto queda quieta */
        if (!web_r3_alive()) { kprint("WEBR3: FALLO (el proceso murio)\n"); return 1; }
        if (g_st->phase == 0 && g_fseq == fs && timer_get_uptime_ms() - t1 > 1200) break;
        fs = g_fseq; thread_sleep(300);
    }
    uint32_t h = 2166136261u, nz = 0;
    for (uint32_t i = 0; g_fb && i < (uint32_t)(g_fw * g_fh); i++) { h ^= g_fb[i]; h *= 16777619u; if ((g_fb[i] & 0xFFFFFF) != 0xFFFFFF) nz++; }
    kprint("  titulo: "); kprint(g_st->title); kprint("\n  url:    "); kprint(g_st->url); kprint("\n  estado: "); kprint(g_st->status);
    kprint("\n  fase "); kprint_dec((uint32_t)g_st->phase); kprint(", alto del documento "); kprint_dec((uint32_t)g_st->doc_h); kprint(" px, imagenes "); kprint_dec((uint32_t)g_st->img_ok); kprint("/"); kprint_dec((uint32_t)g_st->nimg);
    kprint("\n  fotogramas "); kprint_dec(g_fseq); kprint(" ("); kprint_dec((uint32_t)g_fw); kprint("x"); kprint_dec((uint32_t)g_fh); kprint("), pixeles no blancos "); kprint_dec(nz); kprint(", huella 0x"); kprint_hex32(h);
    kprint("\n  memoria del proceso: monton "); kprint_dec(r_heap.used * 4u); kprint(" KiB de "); kprint_dec(g_heap_bytes >> 20); kprint(" MiB, superficie "); kprint_dec(r_surf.used * 4u); kprint(" KiB; en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
    kprint(g_fseq && g_st->url[0] ? "WEBR3: OK\n" : "WEBR3: FALLO (sin fotograma o sin pagina)\n");
    return !(g_fseq && g_st->url[0]);
}
