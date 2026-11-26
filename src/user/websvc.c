/* SOMA_202: NAVEGADOR AISLADO. El motor web completo en un proceso de ring 3 (ver src/core/web_r3_abi.h).
 *
 * Este programa enlaza el mismo codigo que antes vivia en el nucleo: web_page.c, web_html.c, web_css.c, web_layout.c, web_js_*.c,
 * web_font.c, web_net.c, los decodificadores de imagen, el TLS 1.3 con su validacion de certificados y las primitivas de dibujo
 * (fb.c, gui_font.c) apuntando a una superficie propia. El nucleo solo le manda ordenes (navega, clic, tecla, desplaza...) y lee
 * de vuelta un estado y los pixeles ya pintados.
 *
 * No hay nada aqui que el nucleo necesite creer: si una pagina consigue ejecutar codigo en este proceso, tiene un monton, una
 * superficie y la red; no tiene archivos, ni dispositivos, ni la memoria de nadie mas. */
#include <stdint.h>
#include <stddef.h>
#include "web_r3_abi.h"
#include "../arch/x86_64/usermode.h"
#include "memfn.h"
#include "../apps/web_page.h"
#include "../apps/web_img.h"
#include "../drivers/fb.h"
#include "../core/tls13.h"
#include "../core/x509.h"
#include "../core/tlscrypto.h"   /* SOMA_300_CI: sha256 (gcc 14 no admite llamadas sin declarar) */
#include "../core/tls_roots.h"
#include "../core/crypto.h"
#include "../core/smp.h"

static inline long ws3(long nr, long a, long b, long c)
{
    long r;
    __asm__ volatile ("syscall" : "=a"(r), "+D"(a), "+S"(b), "+d"(c) : "0"(nr) : "rcx", "r11", "r8", "r9", "r10", "memory");
    return r;
}
__attribute__((noreturn)) static void ws_exit(long code) { ws3(SYS_EXIT, code, 0, 0); for (;;) { } }
uint64_t timer_get_uptime_ms(void);                    /* unet.c */

/* ------------------------------------------------------------ monton
 * Clases de tamano con listas libres (lo habitual en un navegador: millones de bloques pequenos) y, para lo grande, una lista
 * ordenada por direccion con fusion de vecinos. Cabecera de 16 bytes: tamano del bloque y una marca que delata dobles liberaciones. */
#define H_MAGIC  0x57334845ULL
#define H_NCLASS 22
static const uint32_t H_SZ[H_NCLASS] = { 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192, 12288, 16384, 24576, 32768, 65536 };
struct hblk { uint64_t size; uint64_t tag; };          /* en libres, tag = siguiente de la lista */
static struct hblk *h_free[H_NCLASS], *h_big;
/* SOMA_341_NAVEGADOR_MEMORIA: hasta W3C_INIT, lo minimo que el nucleo siempre concede; despues, lo que diga (a[4], en MiB) */
static uint64_t h_brk = W3_HEAP_VA, h_end = W3_HEAP_VA + W3_HEAP_MIN, h_used;
static struct hblk *h_grow(uint64_t need) { if (h_end - h_brk < need) return 0; struct hblk *b = (struct hblk *)h_brk; h_brk += need; b->size = need; return b; }
void *kmalloc(size_t n)
{
    if (n == 0 || n > W3_HEAP_MAX) return 0;
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
    b->tag = H_MAGIC; h_used += b->size;
    return (uint8_t *)b + 16;
}
void kfree(void *p)
{
    if (!p) return;
    struct hblk *b = (struct hblk *)((uint8_t *)p - 16);
    if (b->tag != H_MAGIC || (uint64_t)b < W3_HEAP_VA || (uint64_t)b >= h_brk) ws_exit(-77);      /* doble liberacion o puntero ajeno */
    h_used -= b->size;
    if (b->size <= 65536) { int c = 0; while (H_SZ[c] < b->size) c++; b->tag = (uint64_t)h_free[c]; h_free[c] = b; return; }
    struct hblk *prev = 0, *cur = h_big;
    while (cur && cur < b) { prev = cur; cur = (struct hblk *)cur->tag; }
    b->tag = (uint64_t)cur;
    if (prev) prev->tag = (uint64_t)b; else h_big = b;
    if (cur && (uint8_t *)b + b->size == (uint8_t *)cur) { b->size += cur->size; b->tag = cur->tag; }
    if (prev && (uint8_t *)prev + prev->size == (uint8_t *)b) { prev->size += b->size; prev->tag = b->tag; }
}
size_t kheap_max_bytes(void) { return (size_t)(h_end - W3_HEAP_VA); }
volatile uint8_t kheap_owner;

/* ------------------------------------------------------------ lo que el motor espera de "el nucleo" */
static struct w3_state *const S = (struct w3_state *)W3_OUT_VA;
static const struct w3_cmd *const C = (const struct w3_cmd *)W3_IN_VA;
static struct wpage *P;
static char *s_stk_lo, *s_stk_hi;
extern char *g_co_lo, *g_co_hi;
static uint64_t s_poll_at;

uint64_t web_now_ms(void) { return timer_get_uptime_ms(); }
#include "../apps/web_dl.h"                       /* SOMA_343_DESCARGAS */
void web_yield(void)
{
    if (wdl_inside()) { wdl_yield(); return; }          /* SOMA_343_DESCARGAS: una descarga que espera cede al bucle principal */
    uint64_t now = timer_get_uptime_ms();
    if (now - s_poll_at >= 40) { s_poll_at = now; if (ws3(SYS_WEB_WAIT, -1, 0, 0) == 2 && P) P->stop = 1; }   /* el usuario pulso parar */
    ws3(SYS_YIELD, 0, 0, 0);
}
/* SOMA_343_DESCARGAS: dentro de una descarga, esperar a la red es ceder el turno; fuera (una hoja de estilo, un script, la pagina),
 * mientras se espera tambien avanzan las descargas de imagenes */
void unet_idle(void) { if (wdl_inside()) { wdl_yield(); return; } wdl_pump(); web_yield(); ws3(SYS_SLEEP, 1, 0, 0); }
uint32_t web_stack_left(void)
{
    char c; char *sp = &c;
    if (g_co_lo && sp >= g_co_lo && sp < g_co_hi) return (uint32_t)(sp - g_co_lo);
    if (sp >= s_stk_lo && sp < s_stk_hi) return (uint32_t)(sp - s_stk_lo);
    return 8000;
}
char *web_stack_hi(const char *sp) { return (sp >= s_stk_lo && sp < s_stk_hi) ? s_stk_hi : 0; }
static void scopy(char *d, const char *s, uint32_t max) { uint32_t i = 0; if (!max) return; for (; s && s[i] && i + 1 < max; i++) d[i] = s[i]; d[i] = 0; }
static void post_state(void) { S->magic = W3_MAGIC; ws3(SYS_WEB_POST, W3P_STATE, 0, 0); }
void wp_log(int level, const char *msg) { S->log_level = (uint32_t)(level > 0 ? level : 1); scopy(S->log_msg, msg, sizeof S->log_msg); post_state(); S->log_level = 0; }
int wp_fs_read(const char *path, char **out, uint32_t *len)
{
    scopy(S->fs_path, path, sizeof S->fs_path);
    long n = ws3(SYS_WEB_FS, 0, 0, 0);
    S->fs_path[0] = 0;
    if (n <= 0 || (uint64_t)n > W3_FILE_MAX) return -1;
    char *o = (char *)kmalloc((size_t)n + 1); if (!o) return -1;
    memcpy(o, (const void *)(W3_IN_VA + W3_FILE_OFF), (size_t)n); o[n] = 0;
    *out = o; *len = (uint32_t)n; return 0;
}
/* generador: SHA-256 en modo contador sobre la semilla que entrega el nucleo con cada orden */
static uint8_t s_seed[32]; static uint64_t s_ctr;
static void reseed(const uint8_t *more) { uint8_t b[64]; memcpy(b, s_seed, 32); memcpy(b + 32, more, 32); sha256(b, 64, s_seed); }
void rng_bytes(void *out, size_t n)
{
    uint8_t *o = (uint8_t *)out, b[40], h[32];
    while (n) { memcpy(b, s_seed, 32); memcpy(b + 32, &s_ctr, 8); s_ctr++; sha256(b, 40, h); size_t c = n < 32 ? n : 32; memcpy(o, h, c); o += c; n -= c; }
}
uint8_t net_ip[4];
int bootinfo_disabled(const char *f) { (void)f; return 0; }
static uint64_t s_unix0, s_up0;
static uint64_t unix_now(void) { return s_unix0 ? s_unix0 + (timer_get_uptime_ms() - s_up0) / 1000 : 0; }
/* SOMA_343_DESCARGAS: "generacion" de la confianza (autoridades del usuario y verificar si/no). Una conexion TLS guardada para reutilizarla
 * solo sirve mientras no cambie: si el usuario quita una autoridad, la siguiente peticion abre otra conexion y vuelve a comprobar. */
uint32_t tls_trust_gen;
void tls_trust_setup(void)
{
    static int inited; static uint64_t last;
    if (!inited) { inited = 1; x509_set_roots(tls_roots_blob, tls_roots_len); tls13_unix_time = unix_now; }
    uint64_t now = timer_get_uptime_ms();
    if (last && now - last < 30000) return;
    last = now ? now : 1;
    char *pem = 0; uint32_t len = 0, h = 2166136261u;
    x509_clear_anchors();
    if (wp_fs_read("/etc/tls_confianza.pem", &pem, &len) == 0) { (void)x509_add_anchors_pem(pem, len); for (uint32_t i = 0; i < len; i++) { h ^= (uint8_t)pem[i]; h *= 16777619u; } kfree(pem); }
    tls_trust_gen = h ^ (uint32_t)len;
}
/* decodificadores de imagen: aqui mismo (este proceso ya esta aislado) */
int wi_decode(const uint8_t *d, uint32_t n, struct wimg *o) { return wi_decode_local(d, n, o); }
int wi_decode_fit(const uint8_t *d, uint32_t n, struct wimg *o, int maxw, int maxh) { return wi_decode_fit_local(d, n, o, maxw, maxh); }
int wi_decode_anim(const uint8_t *d, uint32_t n, struct wimg *o, uint32_t max_pixels) { return wi_decode_anim_local(d, n, o, max_pixels); }
int wi_decode_svg(const uint8_t *d, uint32_t n, struct wimg *o, int dw, int dh, uint32_t color) { return wi_decode_svg_local(d, n, o, dw, dh, color); }
void fb_offscreen_enable(void);
/* fb.c se enlaza por sus primitivas de dibujo sobre fb_target_begin(); su parte de hardware no se usa nunca */
struct bootfb_info;
const struct bootfb_info *bootfb_get(void) { return 0; }
void *bootfb_map(void) { return 0; }
void bootfb_console_stop(void) { } void bootfb_console_resume(void) { } void bootfb_swap_rb(void) { }
int bootfb_use_firmware(void) { return 0; } int bootinfo_efi(void) { return 0; }
void clip_overlay_draw(void) { } void console_clear(void) { }
uint32_t pci_bar0(uint8_t b, uint8_t d, uint8_t f) { (void)b; (void)d; (void)f; return 0; }
uint16_t pci_device_id(uint8_t b, uint8_t d, uint8_t f) { (void)b; (void)d; (void)f; return 0xFFFF; }
uint16_t pci_vendor_id(uint8_t b, uint8_t d, uint8_t f) { (void)b; (void)d; (void)f; return 0xFFFF; }
int vmm_map_page(uint64_t v, uint64_t p, uint64_t fl) { (void)v; (void)p; (void)fl; return -1; }
int smp_cpus(void) { return 1; }
void smp_parallel(smp_fn fn, void *arg) { fn(0, 1, arg); }

/* ------------------------------------------------------------ estado y fotogramas */
static int s_mx = -1, s_my = -1; static uint64_t s_key; static uint32_t s_shash;
static uint64_t page_key(void)
{
    uint64_t k = 1469598103934665603ull;
    #define MIX(v) do { k ^= (uint64_t)(v); k *= 1099511628211ull; } while (0)
    MIX(P->gen); MIX((uintptr_t)P->lay); MIX(P->vw); MIX(P->vh);   /* SOMA_203: el desplazamiento ya no esta aqui (ver publish) */ MIX(P->caret_on); MIX((uintptr_t)P->focus);
    MIX((uintptr_t)P->dd_sel); MIX(P->img_ok); MIX(P->img_fail); MIX(P->nimg); MIX(P->anim_tick); MIX(P->lay ? wl_inner_gen(P->lay) : 0);
    if (P->focus) { MIX((uintptr_t)P->focus->value); MIX(P->focus->sel); MIX(P->focus->flags); }
    #undef MIX
    return k;
}
static int scan_flags(void);
static void fill_state(void)
{
    S->phase = P->phase; S->status_kind = P->status_kind; S->progress = P->progress;
    S->vw = P->vw; S->vh = P->vh; S->sx = P->sx; S->sy = P->sy; S->doc_h = wp_doc_height(P);
    S->hn = P->hn; S->hpos = P->hpos; S->can_back = wp_can_back(P); S->can_fwd = wp_can_fwd(P);
    S->focus_text = P->focus != 0; S->alert_pending = P->alert_pending;
    if (P->scroll_req) { S->scroll_req += P->scroll_req; P->scroll_req = 0; }   /* SOMA_203: suma acumulada; el nucleo aplica la diferencia (antes se repetia la ultima peticion con cada estado) */
    S->inner_scroll = !P->lay_busy && (scan_flags() & 2) != 0;
    S->img_ok = P->img_ok; S->img_fail = P->img_fail; S->nimg = P->nimg; S->heap_used = (uint32_t)(h_used >> 10);
    scopy(S->url, P->url, sizeof S->url); scopy(S->title, P->title, sizeof S->title); scopy(S->status, wp_status(P), sizeof S->status);
    scopy(S->alert_msg, P->alert_msg, sizeof S->alert_msg);
    S->cursor = WPK_NONE; S->hover_href[0] = 0;
    if (P->lay && !P->lay_busy && s_mx >= 0) {
        S->cursor = wp_hit_cursor(P, s_mx, s_my);
        struct wnode *lk = 0; (void)wl_hit(P->lay, s_mx, s_my, &lk, 0);
        const char *h = lk ? wd_attr(lk, "href") : 0; if (h) scopy(S->hover_href, h, sizeof S->hover_href);
    }
    S->dd_open = 0;
    if (P->dd_sel && P->lay && !P->lay_busy) {
        int x, y, w, h;
        if (wl_node_rect(P->lay, P->dd_sel, &x, &y, &w, &h)) {
            int cnt = wl_select_count(P->dd_sel); if (cnt > W3_DD_MAX) cnt = W3_DD_MAX;
            S->dd_open = 1; S->dd_x = x; S->dd_y = y; S->dd_w = w; S->dd_h = h; S->dd_count = cnt; S->dd_cur = wl_select_index(P->dd_sel);
            for (int i = 0; i < cnt; i++) {
                struct wnode *o = wl_select_option(P->dd_sel, i); struct wb b = { 0, 0, 0, 0 };
                if (o) wd_text_content(o, &b);
                scopy(S->dd_opt[i], b.p ? b.p : "", sizeof S->dd_opt[i]); wb_free(&b);
            }
        }
    }
}
static uint32_t state_hash(void)
{
    uint32_t h = 2166136261u; const uint8_t *p = (const uint8_t *)S;
    for (uint32_t i = 0; i < offsetof(struct w3_state, fs_path); i++) { h ^= p[i]; h *= 16777619u; }
    if (S->dd_open) for (uint32_t i = 0; i < sizeof S->dd_opt[0] * (uint32_t)S->dd_count; i++) { h ^= ((const uint8_t *)S->dd_opt)[i]; h *= 16777619u; }
    return h;
}
/* SOMA_203: DESPLAZAMIENTO SIN REPINTAR. El fotograma se pinta mas alto que la vista (el doble: margen por arriba y por abajo,
 * mas hacia donde se mueve la pagina) y el nucleo lo muestra corrido mientras la vista quepa dentro. Solo se repinta cuando
 * cambia el contenido o cuando la vista se acerca al borde de lo pintado. Con elementos fijos o pegajosos no vale (correr el
 * fotograma los moveria): ahi se pinta la vista exacta, como antes. Con un campo de texto activo (el cursor parpadea y obliga
 * a repintar dos veces por segundo) el margen solo se usa mientras se esta desplazando. */
static int s_fw, s_fh, s_fsx, s_fsy, s_last_sy, s_dir, s_scan, s_scan_h; static struct wlay *s_scan_lay; static uint32_t s_scan_gen;
static uint64_t s_scroll_ms;
static int scan_flags(void)
{
    if (!P->lay) return 0;
    if (s_scan_lay != P->lay || s_scan_gen != P->gen || s_scan_h != P->lay->doc_h) { s_scan_lay = P->lay; s_scan_gen = P->gen; s_scan_h = P->lay->doc_h; s_scan = wl_scan_flags(P->lay); }
    return s_scan;
}
/* SOMA_342_NAVEGADOR_AGIL: cada imagen que llegaba repintaba el fotograma entero (dos pantallas de alto): en una portada con 120 fotos,
 * 121 repintados de ~45 ms en QEMU. Mientras llegan imagenes, lo que solo cambia el contenido se agrupa: como mucho un repintado cada
 * PINTA_MIN_MS. Lo que cambia la vista (desplazarse, otro tamano) se repinta al momento, como siempre. */
#define PINTA_MIN_MS 250
static uint64_t s_paint_t;
static int s_paint_due;
static void publish(int force)
{
    if (!P) return;
    if (P->vw > 0 && P->vh > 0 && !P->lay_busy) {
        uint64_t k = page_key(), now = timer_get_uptime_ms();
        int vw = P->vw, vh = P->vh, doc_h = wp_doc_height(P), fl = scan_flags();
        if (P->sy != s_last_sy) { s_dir = P->sy > s_last_sy ? 1 : -1; s_last_sy = P->sy; s_scroll_ms = now; }
        int over = !(fl & 1) && doc_h > vh && !P->dd_sel && (!P->focus || now - s_scroll_ms < 1500);
        /* SOMA_203: `force` (acaba de terminar una orden) ya solo obliga a publicar el estado; antes repintaba con cada movimiento del raton */
        int vista = vw != s_fw || P->sx != s_fsx || P->sy < s_fsy || P->sy + vh > s_fsy + s_fh;
        if (!vista && !over && P->sy != s_fsy) vista = 1;
        if (!vista && over) {                          /* cerca del borde de lo pintado y aun queda documento por ese lado */
            int thr = vh / 8;
            if ((P->sy - s_fsy < thr && s_fsy > 0) || (s_fsy + s_fh - (P->sy + vh) < thr && s_fsy + s_fh < doc_h)) vista = 1;
        }
        int repaint = vista || k != s_key;
        s_paint_due = 0;
        if (repaint && !vista && P->phase == WP_IMAGES && now - s_paint_t < PINTA_MIN_MS) { repaint = 0; s_paint_due = 1; }
        if (repaint) {
            int fh = vh, fsy = P->sy;
            if (over) {
                fh = vh * 2; if (fh > doc_h) fh = doc_h; if (fh > W3_FRAME_MAX_H) fh = W3_FRAME_MAX_H;
                if ((uint64_t)fh * (uint64_t)vw * 4u > W3_SURF_MAX) fh = (int)(W3_SURF_MAX / 4u / (uint64_t)vw);
                if (fh < vh) fh = vh;
                int spare = fh - vh;
                fsy = P->sy - (s_dir > 0 ? spare / 4 : s_dir < 0 ? spare - spare / 4 : spare / 2);
                if (fsy + fh > doc_h) fsy = doc_h - fh;
                if (fsy < 0) fsy = 0;
                if (fsy > P->sy) fsy = P->sy;
            }
            s_key = k; s_paint_t = now;
            fb_target_begin((uint32_t *)W3_SURF_VA, (uint32_t)vw, (uint32_t)fh);
            fb_fill_rect(0, 0, (uint32_t)vw, (uint32_t)fh, 0x00FFFFFF);
            if (P->lay) { wl_paint(P->lay, 0, 0, vw, fh, P->sx, fsy); P->lay->scroll_x = P->sx; P->lay->scroll_y = P->sy; }
            fb_target_end();
            s_fw = vw; s_fh = fh; s_fsx = P->sx; s_fsy = fsy;
            S->frame_w = vw; S->frame_h = fh; S->frame_sx = P->sx; S->frame_sy = fsy;
            S->frame_flags = (fl & 1) ? W3F_FIXED : 0; S->paint_ms = (uint32_t)(timer_get_uptime_ms() - now);
            S->frame_seq++;
            ws3(SYS_WEB_POST, W3P_FRAME, vw, fh);
        }
    }
    fill_state();
    uint32_t h = state_hash();
    if (h != s_shash || force) { s_shash = h; post_state(); }
}

/* SOMA_203: COOKIES Y ALMACENAMIENTO A SALVO DE UNA CAIDA. Cuando cambian, se entrega al nucleo una copia (como mucho una por
 * segundo); el nucleo la guarda sin interpretarla y se la devuelve al proceso siguiente si este se cae. */
static uint32_t s_sv_ck, s_sv_st; static uint64_t s_sv_ms;
static void save_state(void)
{
    uint64_t now = timer_get_uptime_ms();
    if (!P || (wn_cookie_gen() == s_sv_ck && wp_store_gen() == s_sv_st) || now - s_sv_ms < 1000) return;
    s_sv_ms = now;
    struct wb b = { 0, 0, 0, 0 }; uint32_t hd[2] = { W3_SAVE_MAGIC, 0 };
    uint8_t *ck = (uint8_t *)kmalloc(64u << 10); if (!ck) return;
    hd[1] = wn_cookie_export(ck, 64u << 10);
    wb_addn(&b, (const char *)hd, 8); wb_addn(&b, (const char *)ck, hd[1]); kfree(ck);
    wp_store_export(P, &b);
    if (!b.err && b.p && b.len <= W3_SAVE_MAX && ws3(SYS_WEB_POST, W3P_SAVE, (long)(uintptr_t)b.p, (long)b.len) == 0) { s_sv_ck = wn_cookie_gen(); s_sv_st = wp_store_gen(); }
    wb_free(&b);
}
static void restore_state(void)
{
    long n = ws3(SYS_WEB_FS, 1, 0, 0);
    if (n < 8 || (uint64_t)n > W3_SAVE_MAX) return;
    uint8_t *d = (uint8_t *)kmalloc((size_t)n); if (!d) return;
    memcpy(d, (const void *)(W3_IN_VA + W3_FILE_OFF), (size_t)n);
    uint32_t hd[2]; memcpy(hd, d, 8);
    if (hd[0] == W3_SAVE_MAGIC && hd[1] <= (uint32_t)n - 8) {
        if (hd[1]) (void)wn_cookie_import(d + 8, hd[1]);
        (void)wp_store_import(P, d + 8 + hd[1], (uint32_t)n - 8 - hd[1]);
    }
    kfree(d);
    s_sv_ck = wn_cookie_gen(); s_sv_st = wp_store_gen();
}

/* ------------------------------------------------------------ ordenes */
static void reply(int status, const void *d, uint32_t n)
{
    if (n > W3_REPLY_MAX) n = W3_REPLY_MAX;
    if (n) memcpy(S->reply, d, n);
    S->reply_len = n; S->reply_status = status;
}
static void run_cmd(void)
{
    if (C->magic != W3_MAGIC || C->len > W3_CMD_DATA_MAX) ws_exit(-78);
    const char *d = (const char *)(W3_IN_VA + sizeof(struct w3_cmd));     /* el nucleo deja un 0 detras de los datos */
    const int32_t *a = C->a;
    reseed(C->seed);
    S->reply_len = 0; S->reply_status = 0; S->blob_len = 0; S->blob_va = 0;
    if (C->op == W3C_INIT) {
        s_unix0 = C->u64; s_up0 = timer_get_uptime_ms(); tls13_verify = a[2];
        if (a[4] > 0 && (uint64_t)a[4] << 20 <= W3_HEAP_MAX && W3_HEAP_VA + ((uint64_t)a[4] << 20) > h_end)
            h_end = W3_HEAP_VA + ((uint64_t)a[4] << 20);             /* SOMA_341_NAVEGADOR_MEMORIA: solo crece (lo ya repartido sigue dentro) */
        net_ip[0] = (uint8_t)(a[3] >> 24); net_ip[1] = (uint8_t)(a[3] >> 16); net_ip[2] = (uint8_t)(a[3] >> 8); net_ip[3] = (uint8_t)a[3];
        if (!P) { P = wp_new(); if (P) restore_state(); }
        if (!P) ws_exit(-79);
        wp_set_viewport(P, a[0], a[1]);
    } else if (P) switch (C->op) {
    case W3C_NAVIGATE: P->stop = 0; wp_navigate(P, d, a[0]); break;
    case W3C_HISTORY: P->stop = 0; wp_history_go(P, a[0]); break;
    case W3C_VIEWPORT: if (a[0] > 0 && a[1] > 0 && a[0] <= W3_MAX_W && a[1] <= W3_MAX_H) wp_set_viewport(P, a[0], a[1]); break;
    case W3C_SCROLL: { int m = wp_doc_height(P) - P->vh; if (m < 0) m = 0; P->sx = a[0] < 0 ? 0 : a[0]; P->sy = a[1] < 0 ? 0 : a[1] > m ? m : a[1]; break; }
    case W3C_MOVE: s_mx = a[0]; s_my = a[1]; if (P->lay) wp_mouse_move(P, a[0], a[1]); break;
    case W3C_CLICK: s_mx = a[0]; s_my = a[1]; wp_mouse_click(P, a[0], a[1], a[2], a[3], a[4]); break;
    case W3C_KEY: wp_key(P, a[0], d, a[1]); break;
    case W3C_SELECT: if (P->dd_sel) { if (a[0] >= 0) wp_select_choose(P, P->dd_sel, a[0]); else P->dd_sel = 0; } break;
    case W3C_EVAL: { char o[64]; wp_eval_js(P, d, o, sizeof o); break; }
    case W3C_FIND: wp_find_scroll(P, d); break;
    case W3C_ALERT_ACK: P->alert_pending = 0; break;
    case W3C_INNER_SCROLL:                             /* SOMA_203: si ninguna caja bajo el cursor puede moverse mas, la rueda es de la pagina */
        if (P->lay && !P->lay_busy && wl_inner_scroll(P->lay, a[0], a[1], a[2])) reply(1, 0, 0);
        else __atomic_add_fetch(&P->scroll_req, a[2], __ATOMIC_RELEASE);
        break;
    case W3C_GET_TEXT: {
        struct wnode *n = P->doc ? wd_by_id(P->doc->root, d) : 0; struct wb b = { 0, 0, 0, 0 };
        if (n) wd_text_content(n, &b);
        reply(n ? 1 : 0, b.p, b.p ? b.len : 0); wb_free(&b); break;
    }
    case W3C_SET_TEXT: {
        uint32_t il = 0; while (il < C->len && d[il]) il++;
        struct wnode *n = (P->doc && il < C->len) ? wd_by_id(P->doc->root, d) : 0;
        if (n) { wd_set_text(P->doc, n, d + il + 1); P->need_style = 1; }
        reply(n ? 1 : 0, 0, 0); break;
    }
    case W3C_GET_HIST: reply(1, P->hist, sizeof P->hist); break;
    case W3C_SET_HIST: if (C->len == sizeof P->hist && a[0] >= 0 && a[0] <= WP_HIST && a[1] >= -1 && a[1] < a[0]) {
            memcpy(P->hist, d, sizeof P->hist); P->hn = a[0]; P->hpos = a[1];
            for (int i = 0; i < WP_HIST; i++) { P->hist[i].url[sizeof P->hist[i].url - 1] = 0; P->hist[i].title[sizeof P->hist[i].title - 1] = 0; }
        } break;
    case W3C_HIT: {
        char b[2048 + 2100]; uint32_t n = 0; b[0] = 0;
        if (P->lay && !P->lay_busy) {
            struct wnode *lk = 0, *nd = wl_hit(P->lay, a[0], a[1], &lk, 0);
            const char *h = lk ? wd_attr(lk, "href") : 0, *src = nd ? wl_img_src(nd) : 0;
            scopy(b, h ? h : "", 2048); while (b[n]) n++; n++;
            scopy(b + n, src ? src : "", 2048); while (b[n]) n++; n++;
        } else { b[1] = 0; n = 2; }
        reply(1, b, n); break;
    }
    case W3C_GET_IMG: {                                /* SOMA_203: Guardar/Copiar imagen. Los pixeles se quedan en el monton; el nucleo los copia de ahi */
        static struct w3_img_reply ir; const uint32_t *px = 0; int w = 0, h = 0;
        memset(&ir, 0, sizeof ir);
        ir.ok = wp_img_get(P, d, &px, &w, &h, ir.abs, sizeof ir.abs);
        if (ir.ok && w > 0 && h > 0 && (uint64_t)w * (uint64_t)h * 4u <= W3_BLOB_MAX) {
            ir.w = w; ir.h = h;
            if (a[0]) { S->blob_va = (uint64_t)(uintptr_t)px; S->blob_len = (uint32_t)w * (uint32_t)h * 4u; }
        } else ir.ok = 0;
        reply(1, &ir, sizeof ir); break;
    }
    case W3C_CRASH:                                    /* solo pruebas: un fallo de verdad DENTRO de este proceso */
        if (a[0] == 1) __asm__ volatile ("ud2");
        else if (a[0] == 2) { for (volatile int z = 0;; z++) { } }
        else if (a[0] == 3) { for (;;) { char *m = (char *)kmalloc(1u << 20); if (!m) { m = (char *)(h_brk); } for (uint32_t i = 0; i < (1u << 20); i += 4096) m[i] = 1; } }
        else { volatile uintptr_t z = 8; *(volatile int *)z = 1; }
        break;
    default: break;
    }
    S->seq_done = C->seq;
}

static void ws_main(void)
{
    for (;;) {
        int next = P ? wp_tick(P, timer_get_uptime_ms()) : 200;
        wn_keepalive_prune();                                    /* SOMA_343_DESCARGAS: cierra las conexiones guardadas que caducaron */
        publish(0); save_state();
        if (s_paint_due) { uint64_t t = timer_get_uptime_ms() - s_paint_t; int falta = t < PINTA_MIN_MS ? (int)(PINTA_MIN_MS - t) : 1; if (next > falta) next = falta; }   /* SOMA_342 */
        if (next < 1) next = 1;
        if (next > 250) next = 250;
        long r = ws3(SYS_WEB_WAIT, next, 0, 0);
        if (r == 1) { run_cmd(); publish(1); }
        else if (r == 2 && P) P->stop = 1;
    }
}

__attribute__((section(".text._start"), used)) void _start(void)
{
    const uint64_t STK = 2u << 20;
    s_stk_lo = (char *)kmalloc(STK);
    if (!s_stk_lo) ws_exit(-80);
    s_stk_hi = s_stk_lo + STK;
    fb_offscreen_enable();
    /* el resto corre sobre una pila propia de 2 MiB (la del proceso son 64 KiB: el motor es recursivo) */
    __asm__ volatile ("movq %0, %%rsp\n\txorl %%ebp, %%ebp\n\tcall *%1" : : "r"((uint64_t)(uintptr_t)s_stk_hi - 64), "r"((void (*)(void))ws_main) : "memory");
    ws_exit(0);
}
