/* PARCHE 089: Monitor del sistema con la estetica del explorador.
 * Secciones: Resumen, Procesos, Memoria, CPU, Graficos, Almacenamiento, Red y Hardware. Toma una muestra por segundo y guarda
 * dos minutos de historial. La CPU y la memoria se reparten por "dueno" (acct.h): cada aplicacion de la GUI, RamFS, la pantalla,
 * el nucleo... aunque todas las ventanas compartan el hilo de la GUI. Los hilos llevan su propio tiempo de CPU. */
#include "sysmon.h"
#include "fb.h"
#include "fs.h"
#include "mem.h"
#include "thread.h"
#include "acct.h"
#include "hwmon.h"
#include "smp.h"
#include "ramstat.h"
#include "sysinfo.h"
#include "netif.h"
#include "bigfs.h"
#include "bigfs_glue.h"
#include "virtio_blk.h"
#include "idt.h"
#include "web_font.h"
#include "gui_font.h"
#include "gui.h"
#include "mouse.h"

#define KC_WIN     0x000C111B
#define KC_BAR     0x00131A27
#define KC_SIDE    0x000F1520
#define KC_LINE    0x00212B3B
#define KC_CARD    0x00111827
#define KC_CARD2   0x00151E2E
#define KC_TXT     0x00E5EAF2
#define KC_DIM     0x008592A8
#define KC_FAINT   0x00566379
#define KC_ACC     0x0038BDF8
#define KC_SELBG   0x001A3558
#define C_CPU  0x0038BDF8
#define C_RAM  0x00A78BFA
#define C_NETR 0x0034D399
#define C_NETT 0x00F59E0B
#define C_GPU  0x00F472B6
#define C_TEMP 0x00FB923C
#define HN 120

enum { S_SUM, S_PROC, S_MEM, S_CPU, S_GPU, S_DISK, S_NET, S_HW, S_N };
static const char *const SEC_NAME[S_N] = { "Resumen", "Procesos", "Memoria", "CPU", "Graficos", "Almacenamiento", "Red", "Hardware" };
static int g_sec, g_ptab, g_sort = 1, g_sel = -1, g_scroll, g_inited;
static char g_msg[100];

/* ---------------------------------------------------------------- muestras */
static uint16_t h_cpu[HN], h_ram[HN], h_fps[HN], h_core[8][HN]; static uint32_t h_rx[HN], h_tx[HN]; static int16_t h_temp[HN];
static int h_n, h_pos;
static uint64_t l_t, l_idle, l_own[256], l_rx, l_tx, l_pres, l_ms, l_core[8];
static int cpu_pct, core_pct[8], own_pct[256], fps, ncores = 1;   /* decimas de % */
static uint32_t rx_rate, tx_rate; static int temp_mc = -1; static uint32_t cur_mhz;
static uint64_t own_mem[256]; static uint32_t own_blk[256];
static struct ramstat_info g_ram; static struct kheap_counters g_heap;
#define MAXT 48
static struct thread_info g_thr[MAXT]; static int g_nthr; static int thr_pct[MAXT];
static struct thread_info p_thr[MAXT]; static int p_nthr;
static uint64_t g_last_ms;
static struct hw_cpu g_cpu; static struct hw_gpu g_gpu; static struct hw_board g_board; static struct hw_dev g_dev[48]; static int g_ndev; static int g_hw;

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static void s_cat(char *d, const char *s, int max) { int i = s_len(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void s_num(char *d, int max, uint64_t v) { char t[24]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 23); char r[24]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
static void s_pct(char *d, int max, int p10) { s_num(d, max, (uint64_t)(p10 / 10)); s_cat(d, ",", max); s_num(d, max, (uint64_t)(p10 % 10)); s_cat(d, " %", max); }
static void s_bytes(char *d, int max, uint64_t b)
{
    if (b >= (1ULL << 30)) { s_num(d, max, b >> 30); s_cat(d, ",", max); s_num(d, max, ((b >> 20) & 1023) * 10 / 1024); s_cat(d, " GiB", max); }
    else if (b >= (1ULL << 20)) { s_num(d, max, b >> 20); s_cat(d, ",", max); s_num(d, max, ((b >> 10) & 1023) * 10 / 1024); s_cat(d, " MiB", max); }
    else if (b >= 1024) { s_num(d, max, (b + 512) >> 10); s_cat(d, " KiB", max); }
    else { s_num(d, max, b); s_cat(d, " B", max); }
}
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int tw(const char *s, int px, int bold) { int n = s_len(s); return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8; }
static void tx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold)
{
    int n = s_len(s); if (!n) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}
static void txr(int32_t right, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold) { tx(right - tw(s, px, bold), top, h, s, px, col, bold); }

static const char *owner_name(int o)
{
    switch (o) {
    case ACCT_SYSTEM: return "Nucleo del sistema"; case ACCT_RAMFS: return "RamFS (archivos)"; case ACCT_SCREEN: return "Pantalla (bufferes)";
    case ACCT_NET: return "Red"; case ACCT_AGENT: return "Agente"; case ACCT_DESKTOP: return "Escritorio"; case ACCT_WEB: return "Navegador (descargas)";
    case ACCT_IDLE: return "Inactivo";
    default: return o >= ACCT_APP0 && o < ACCT_IDLE ? gui_app_title(o - ACCT_APP0) : "Otro";
    }
}

/* SOMA_216_RENDIMIENTO: cambia cada vez que lo dibujado puede cambiar (muestra nueva, clic, tecla, rueda). La GUI copia el
 * contenido de la ventana de la ultima pintura mientras no cambie, en vez de redibujarlo en cada fotograma. */
static uint32_t g_ver = 1;
uint32_t sysmon_version(void) { return g_ver; }
static void sample(void)
{
    g_ver++;
    uint64_t now = acct_now(), ms = timer_get_uptime_ms();
    uint64_t idle = acct_owner_cycles(ACCT_IDLE);
    struct sysinfo si; sysinfo_get(&si);
    uint32_t a, b; uint64_t rxb = 0, txb = 0; netif_stats(&a, &b, &txb, &rxb); if (!rxb && !txb) { rxb = si.rx_bytes; txb = si.tx_bytes; }
    struct fb_info fi; fb_get_info(&fi);
    ramstat_get(&g_ram); kheap_get_counters(&g_heap); kheap_owner_usage(own_mem, own_blk);
    ncores = smp_cpus(); if (ncores > 8) ncores = 8;
    if (ncores < 1) ncores = 1;
    /* hilos */
    p_nthr = g_nthr; for (int i = 0; i < g_nthr; i++) p_thr[i] = g_thr[i];
    g_nthr = thread_snapshot(g_thr, MAXT);
    if (l_t) {
        uint64_t dt = now - l_t; if (!dt) dt = 1;
        uint64_t di = idle - l_idle; if (di > dt) di = dt;
        cpu_pct = (int)(1000 - di * 1000 / dt);
        for (int o = 0; o < 256; o++) { uint64_t c = acct_owner_cycles((uint8_t)o); own_pct[o] = (int)((c - l_own[o]) * 1000 / dt); if (own_pct[o] > 1000) own_pct[o] = 1000; l_own[o] = c; }
        for (int i = 0; i < g_nthr; i++) {
            thr_pct[i] = 0;
            for (int k = 0; k < p_nthr; k++) if (p_thr[k].tid == g_thr[i].tid) { uint64_t d = g_thr[i].cycles - p_thr[k].cycles; thr_pct[i] = (int)(d * 1000 / dt); if (thr_pct[i] > 1000) thr_pct[i] = 1000; break; }
        }
        uint64_t dms = ms - l_ms; if (!dms) dms = 1;
        core_pct[0] = cpu_pct;
        for (int c = 1; c < ncores; c++) { struct smp_core_info ci; if (smp_core_info(c, &ci) == 0) { uint64_t d = ci.busy_ticks - l_core[c]; core_pct[c] = (int)(d * 1000 / dms); if (core_pct[c] > 1000) core_pct[c] = 1000; l_core[c] = ci.busy_ticks; } }
        rx_rate = (uint32_t)((rxb - l_rx) * 1000 / dms); tx_rate = (uint32_t)((txb - l_tx) * 1000 / dms);
        fps = (int)((fi.presents - l_pres) * 1000 / dms);
        h_cpu[h_pos] = (uint16_t)cpu_pct; h_ram[h_pos] = (uint16_t)(g_ram.total ? g_ram.footprint * 1000 / g_ram.total : 0);
        h_fps[h_pos] = (uint16_t)fps; h_rx[h_pos] = rx_rate; h_tx[h_pos] = tx_rate;
        for (int c = 0; c < ncores; c++) h_core[c][h_pos] = (uint16_t)core_pct[c];
        hwmon_live(&temp_mc, &cur_mhz); h_temp[h_pos] = (int16_t)(temp_mc < 0 ? -1 : temp_mc / 100);
        h_pos = (h_pos + 1) % HN; if (h_n < HN) h_n++;
    } else {
        for (int o = 0; o < 256; o++) l_own[o] = acct_owner_cycles((uint8_t)o);
        for (int c = 1; c < ncores; c++) { struct smp_core_info ci; if (smp_core_info(c, &ci) == 0) l_core[c] = ci.busy_ticks; }
    }
    l_t = now; l_idle = idle; l_rx = rxb; l_tx = txb; l_pres = fi.presents; l_ms = ms;
}
void sysmon_init(void)
{
    if (!g_hw) { hwmon_cpu(&g_cpu); hwmon_gpu(&g_gpu); hwmon_board(&g_board); g_ndev = hwmon_devices(g_dev, 48); g_hw = 1; }
    if (!g_inited) { g_inited = 1; sample(); g_last_ms = timer_get_uptime_ms(); }
}
int sysmon_tick(void)
{
    uint64_t ms = timer_get_uptime_ms();
    if (ms - g_last_ms < 1000) return 0;
    g_last_ms = ms; sample();
    return 1;
}

/* ---------------------------------------------------------------- dibujo basico */
static void card(int x, int y, int w, int h) { fb_fill_rrect_aa(x, y, w, h, 12, 0xFF000000u | KC_CARD); fb_stroke_rrect_aa(x, y, w, h, 12, 0xFF1F2A3Bu); }
static void bar(int x, int y, int w, int h, int p10, uint32_t col)
{
    fb_fill_rrect_aa(x, y, w, h, h / 2, 0xFF1E293Bu);
    int f = (int)((int64_t)w * (p10 < 0 ? 0 : p10 > 1000 ? 1000 : p10) / 1000);
    if (f > h / 2) fb_fill_rrect_aa(x, y, f, h, h / 2, 0xFF000000u | col);
}
/* grafica de historial: n valores del anillo, escala max (0 = automatica). Area translucida + linea suavizada */
static void graph(int x, int y, int w, int h, const void *data, int is32, uint32_t vmax, uint32_t col, int grid)
{
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, 0x000D1420);
    if (grid) for (int i = 1; i < 4; i++) fb_fill_rect((uint32_t)x, (uint32_t)(y + h * i / 4), (uint32_t)w, 1, 0x00172133);
    int n = h_n; if (n < 2) return;
    uint32_t vals[HN];
    for (int i = 0; i < n; i++) { int k = (h_pos - n + i + HN) % HN; vals[i] = is32 ? ((const uint32_t *)data)[k] : ((const uint16_t *)data)[k]; }
    if (!vmax) { for (int i = 0; i < n; i++) if (vals[i] > vmax) vmax = vals[i]; if (!vmax) vmax = 1; vmax += vmax / 5; }
    int px0 = 0, py0 = 0;
    for (int i = 0; i < n; i++) {
        int px = x + w - 1 - (int)((int64_t)(n - 1 - i) * (w - 1) / (HN - 1));
        uint32_t v = vals[i] > vmax ? vmax : vals[i];
        int py = y + h - 1 - (int)((int64_t)v * (h - 2) / vmax);
        if (i > 0) {
            for (int xx = px0; xx <= px; xx++) { int yy = py0 + (int)((int64_t)(py - py0) * (xx - px0) / (px > px0 ? px - px0 : 1)); fb_fill_rect_alpha(xx, yy, 1, y + h - yy, 0x38000000u | col); }
            fb_line_aa(px0 * 16, py0 * 16, px * 16, py * 16, 26, 0xFF000000u | col);
        }
        px0 = px; py0 = py;
    }
}
/* anillo: segmentos con su fraccion (sumas <= 1000) */
static uint32_t ang16(int dx, int dy)    /* 0 arriba, sentido horario, 0..65535 */
{
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax; if (!mx) return 0;
    int64_t t = (int64_t)mn * 65536 / mx;                   /* 0..1 */
    uint32_t a = (uint32_t)((8192 * t + 2847 * t / 65536 * (65536 - t)) >> 16);   /* atan(t) en 1/65536 de vuelta */
    if (ay > ax) a = 16384 - a;                             /* angulo desde el eje x */
    /* convertir a "desde arriba en sentido horario" */
    uint32_t ang;   /* desde el eje x positivo, antihorario con y hacia arriba */
    if (dx >= 0 && dy <= 0) ang = a; else if (dx < 0 && dy <= 0) ang = 32768 - a; else if (dx < 0) ang = 32768 + a; else ang = 65536 - a;
    return (16384 - ang + 65536) & 65535;
}
static void donut(int cx, int cy, int ro, int ri, const int *frac, const uint32_t *cols, int n, uint32_t bg)
{
    uint32_t edges[12]; uint32_t acc = 0;
    for (int i = 0; i < n; i++) { acc += (uint32_t)frac[i]; edges[i] = acc * 65536u / 1000u; }
    int ro2 = ro * ro, ri2 = ri * ri;
    for (int y = -ro; y <= ro; y++) for (int x = -ro; x <= ro; x++) {
        int d2 = x * x + y * y; if (d2 > ro2 || d2 < ri2) continue;
        uint32_t a = ang16(x, y), c = bg;
        for (int i = 0; i < n; i++) if (a < edges[i]) { c = cols[i]; break; }
        uint32_t al = 255; int e = ro2 - d2; if (e < 2 * ro) al = (uint32_t)(e * 255 / (2 * ro)); int e2 = d2 - ri2; if (e2 < 2 * ri && (uint32_t)(e2 * 255 / (2 * ri)) < al) al = (uint32_t)(e2 * 255 / (2 * ri));
        fb_fill_rect_alpha(cx + x, cy + y, 1, 1, (al << 24) | (c & 0xFFFFFF));
    }
}
static void kv(int x, int y, int w, const char *k, const char *v)
{
    tx(x, y, 26, k, 12, KC_DIM, 0);
    int kw_ = tw(k, 12, 0) + 12, avail = w - kw_, px = 12;
    if (tw(v, px, 0) > avail) px = 11;
    if (tw(v, px, 0) > avail) px = 10;
    fb_set_clip(x + kw_, y, avail, 26);
    if (tw(v, px, 0) <= avail) txr(x + w, y, 26, v, px, KC_TXT, 0); else tx(x + kw_, y, 26, v, px, KC_TXT, 0);   /* no cabe: se corta por la derecha */
    fb_reset_clip();
    fb_fill_rect((uint32_t)x, (uint32_t)(y + 26), (uint32_t)w, 1, 0x00182233);
}
static void sec_icon(int s, int cx, int cy, uint32_t col)
{
    uint32_t a = 0xFF000000u | col; int S = 16;
    switch (s) {
    case S_SUM: for (int i = 0; i < 4; i++) fb_fill_rrect_aa(cx - 7 + (i & 1) * 8, cy - 7 + (i >> 1) * 8, 6, 6, 1, a); break;
    case S_PROC: for (int i = 0; i < 3; i++) { fb_fill_rect((uint32_t)(cx - 7), (uint32_t)(cy - 5 + i * 5), 3, 2, col); fb_fill_rect((uint32_t)(cx - 2), (uint32_t)(cy - 5 + i * 5), 9, 2, col); } break;
    case S_MEM: fb_stroke_rrect_aa(cx - 8, cy - 5, 16, 10, 2, a); for (int i = 0; i < 4; i++) fb_fill_rect((uint32_t)(cx - 6 + i * 4), (uint32_t)(cy - 3), 2, 6, col); break;
    case S_CPU: fb_stroke_rrect_aa(cx - 6, cy - 6, 12, 12, 2, a); fb_fill_rect((uint32_t)(cx - 2), (uint32_t)(cy - 2), 4, 4, col); for (int i = -1; i <= 1; i++) { fb_fill_rect((uint32_t)(cx + i * 3), (uint32_t)(cy - 9), 1, 3, col); fb_fill_rect((uint32_t)(cx + i * 3), (uint32_t)(cy + 7), 1, 3, col); } break;
    case S_GPU: fb_stroke_rrect_aa(cx - 8, cy - 6, 16, 11, 2, a); fb_fill_rect((uint32_t)(cx - 3), (uint32_t)(cy + 6), 6, 2, col); break;
    case S_DISK: fb_stroke_rrect_aa(cx - 7, cy - 7, 14, 14, 3, a); fb_fill_rrect_aa(cx - 2, cy - 2, 4, 4, 2, a); break;
    case S_NET: fb_line_aa((cx - 4) * S, (cy + 6) * S, (cx - 4) * S, (cy - 6) * S, 22, a); fb_line_aa((cx - 4) * S, (cy - 6) * S, (cx - 7) * S, (cy - 3) * S, 22, a); fb_line_aa((cx + 4) * S, (cy - 6) * S, (cx + 4) * S, (cy + 6) * S, 22, a); fb_line_aa((cx + 4) * S, (cy + 6) * S, (cx + 7) * S, (cy + 3) * S, 22, a); break;
    default: fb_stroke_rrect_aa(cx - 7, cy - 7, 14, 14, 7, a); fb_fill_rect((uint32_t)cx, (uint32_t)(cy - 3), 1, 7, col); break;
    }
}

/* ---------------------------------------------------------------- filas de procesos */
struct prow { int kind; int id; const char *name; const char *what; int cpu; uint64_t mem; uint32_t tid; int state; uint64_t sw; int user; };
static struct prow g_rows[96]; static int g_nrows;
static void build_rows(void)
{
    g_nrows = 0;
    if (g_ptab == 0) {   /* aplicaciones y subsistemas */
        int apps[24]; int na = gui_app_list(apps, 24);
        for (int i = 0; i < na && g_nrows < 96; i++) {
            int dup = 0; for (int k = 0; k < g_nrows; k++) if (g_rows[k].id == ACCT_APP0 + apps[i]) dup = 1;
            if (dup) continue;
            struct prow *r = &g_rows[g_nrows++]; memset(r, 0, sizeof *r);
            r->kind = 0; r->id = ACCT_APP0 + apps[i]; r->name = gui_app_title(apps[i]); r->what = "Aplicacion";
        }
        for (int o = 0; o < 255 && g_nrows < 96; o++) {
            if (o >= ACCT_APP0) { int open = 0; for (int k = 0; k < g_nrows; k++) if (g_rows[k].id == o) open = 1; if (open || !own_mem[o]) continue; }
            else if (!own_mem[o] && !own_pct[o]) continue;
            struct prow *r = &g_rows[g_nrows++]; memset(r, 0, sizeof *r);
            r->kind = 1; r->id = o; r->name = owner_name(o); r->what = o >= ACCT_APP0 ? "Aplicacion (cerrada)" : "Servicio del sistema";
        }
        for (int i = 0; i < g_nrows; i++) { g_rows[i].cpu = own_pct[g_rows[i].id]; g_rows[i].mem = own_mem[g_rows[i].id]; }
    } else {
        for (int i = 0; i < g_nthr && g_nrows < 96; i++) {
            struct prow *r = &g_rows[g_nrows++]; memset(r, 0, sizeof *r);
            r->kind = 2; r->id = i; r->name = g_thr[i].name; r->cpu = thr_pct[i]; r->mem = (uint64_t)g_thr[i].stack_kb * 1024; r->tid = g_thr[i].tid; r->state = (int)g_thr[i].state; r->sw = g_thr[i].switches; r->user = g_thr[i].user;
            r->what = g_thr[i].user ? "Proceso de usuario" : g_thr[i].owner == ACCT_IDLE ? "Inactivo" : "Hilo del nucleo";
        }
    }
    for (int i = 1; i < g_nrows; i++) {   /* orden */
        struct prow t = g_rows[i]; int j = i - 1;
        while (j >= 0) {
            int sw;
            if (g_sort == 1) sw = g_rows[j].cpu < t.cpu; else if (g_sort == 2) sw = g_rows[j].mem < t.mem;
            else { const char *a = g_rows[j].name, *b = t.name; while (*a && *a == *b) { a++; b++; } sw = (uint8_t)*a > (uint8_t)*b; }
            if (!sw) break;
            g_rows[j + 1] = g_rows[j]; j--;
        }
        g_rows[j + 1] = t;
    }
}

/* ---------------------------------------------------------------- secciones */
static void hdr(int x, int y, const char *t, const char *sub) { tx(x, y, 34, t, 20, KC_TXT, 1); if (sub) tx(x, y + 30, 22, sub, 12, KC_DIM, 0); }
static void stat_card(int x, int y, int w, int h, const char *title, const char *big, const char *sub, const void *data, int is32, uint32_t vmax, uint32_t col)
{
    card(x, y, w, h);
    fb_fill_rrect_aa(x + 16, y + 18, 8, 8, 4, 0xFF000000u | col);
    tx(x + 30, y + 10, 24, title, 12, KC_DIM, 1);
    tx(x + 16, y + 34, 36, big, 24, KC_TXT, 1);
    if (sub) { fb_set_clip(x + 16, y + 70, w - 32, 20); tx(x + 16, y + 70, 20, sub, 11, KC_DIM, 0); fb_reset_clip(); }
    graph(x + 12, y + 96, w - 24, h - 108, data, is32, vmax, col, 0);
}
static void s_uptime(char *d, int max)
{
    uint64_t s = timer_get_uptime_ms() / 1000; uint64_t hh = s / 3600, mm = (s / 60) % 60;
    if (hh) { s_num(d, max, hh); s_cat(d, " h ", max); }
    s_num(d, max, mm); s_cat(d, " min ", max); s_num(d, max, s % 60); s_cat(d, " s", max);
}
static void sec_summary(int x, int y, int w, int h)
{
    char sub[120] = "SOMA 0.3 pre-beta  -  "; s_cat(sub, g_cpu.arch, sizeof sub); s_cat(sub, "  -  encendido hace ", sizeof sub); s_uptime(sub, sizeof sub);
    hdr(x, y, "Resumen del sistema", sub);
    int cy = y + 66, cw = (w - 36) / 4, ch = 200;
    if (cw < 150) cw = (w - 12) / 2;
    char b1[32] = "", s1[64] = "", b2[32] = "", s2[64] = "", b3[32] = "", s3[64] = "", b4[32] = "", s4[64] = "";
    s_pct(b1, sizeof b1, cpu_pct); s_num(s1, sizeof s1, (uint64_t)ncores); s_cat(s1, ncores == 1 ? " nucleo" : " nucleos", sizeof s1);
    if (cur_mhz || g_cpu.mhz) { s_cat(s1, "  -  ", sizeof s1); s_num(s1, sizeof s1, cur_mhz ? cur_mhz : g_cpu.mhz); s_cat(s1, " MHz", sizeof s1); }
    s_bytes(b2, sizeof b2, g_ram.footprint); s_cat(s2, "de ", sizeof s2); s_bytes(s2, sizeof s2, g_ram.total); s_cat(s2, "  -  ", sizeof s2); s_pct(s2, sizeof s2, g_ram.total ? (int)(g_ram.footprint * 1000 / g_ram.total) : 0);
    s_bytes(b3, sizeof b3, rx_rate + tx_rate); s_cat(b3, "/s", sizeof b3); s_cat(s3, "bajada ", sizeof s3); s_bytes(s3, sizeof s3, rx_rate); s_cat(s3, "/s  subida ", sizeof s3); s_bytes(s3, sizeof s3, tx_rate); s_cat(s3, "/s", sizeof s3);
    s_num(b4, sizeof b4, (uint64_t)fps); s_cat(b4, " fps", sizeof b4); struct fb_info fi; fb_get_info(&fi); s_num(s4, sizeof s4, fi.w); s_cat(s4, " x ", sizeof s4); s_num(s4, sizeof s4, fi.h); s_cat(s4, "  -  ", sizeof s4); s_cat(s4, g_gpu.name, sizeof s4);
    int pos[4][2]; for (int i = 0; i < 4; i++) { if (cw * 4 + 36 <= w) { pos[i][0] = x + i * (cw + 12); pos[i][1] = cy; } else { pos[i][0] = x + (i & 1) * (cw + 12); pos[i][1] = cy + (i >> 1) * (ch + 12); } }
    stat_card(pos[0][0], pos[0][1], cw, ch, "PROCESADOR", b1, s1, h_cpu, 0, 1000, C_CPU);
    stat_card(pos[1][0], pos[1][1], cw, ch, "MEMORIA", b2, s2, h_ram, 0, 1000, C_RAM);
    stat_card(pos[2][0], pos[2][1], cw, ch, "RED", b3, s3, h_rx, 1, 0, C_NETR);
    stat_card(pos[3][0], pos[3][1], cw, ch, "PANTALLA", b4, s4, h_fps, 0, 0, C_GPU);
    int ty = pos[3][1] + ch + 16, th = y + h - ty; if (th < 120) return;
    int lw = (w - 12) * 6 / 10, rw = w - 12 - lw;
    card(x, ty, lw, th); tx(x + 16, ty + 8, 28, "Lo que mas consume ahora", 13, KC_TXT, 1);
    int save = g_ptab; g_ptab = 0; int ss = g_sort; g_sort = 1; build_rows(); g_ptab = save; g_sort = ss;
    int ry = ty + 42;
    for (int i = 0; i < g_nrows && ry + 30 < ty + th; i++) {
        if (g_rows[i].id == ACCT_IDLE) continue;
        tx(x + 16, ry, 26, g_rows[i].name, 12, KC_TXT, 0);
        bar(x + lw - 250, ry + 9, 110, 8, g_rows[i].cpu, C_CPU);
        char t[24] = ""; s_pct(t, sizeof t, g_rows[i].cpu); txr(x + lw - 90, ry, 26, t, 11, KC_DIM, 0);
        char m[24] = ""; s_bytes(m, sizeof m, g_rows[i].mem); txr(x + lw - 16, ry, 26, m, 11, KC_DIM, 0);
        ry += 30;
    }
    card(x + lw + 12, ty, rw, th); tx(x + lw + 28, ty + 8, 28, "Equipo", 13, KC_TXT, 1);
    int kx = x + lw + 28, kw = rw - 32, ky = ty + 40;
    char t[96];
    kv(kx, ky, kw, "Procesador", g_cpu.brand); ky += 28;
    t[0] = 0; s_num(t, sizeof t, (uint64_t)g_cpu.cores); s_cat(t, " nucleos / ", sizeof t); s_num(t, sizeof t, (uint64_t)g_cpu.threads); s_cat(t, " hilos", sizeof t); kv(kx, ky, kw, "Nucleos", t); ky += 28;
    t[0] = 0; s_bytes(t, sizeof t, g_ram.total); kv(kx, ky, kw, "Memoria RAM", t); ky += 28;
    kv(kx, ky, kw, "Graficos", g_gpu.name); ky += 28;
    kv(kx, ky, kw, "Placa", g_board.model); ky += 28;
    if (temp_mc >= 0 && ky + 28 < ty + th) { t[0] = 0; s_num(t, sizeof t, (uint64_t)(temp_mc / 1000)); s_cat(t, ",", sizeof t); s_num(t, sizeof t, (uint64_t)(temp_mc % 1000 / 100)); s_cat(t, " C", sizeof t); kv(kx, ky, kw, "Temperatura", t); }
}
static void sec_proc(int x, int y, int w, int h)
{
    hdr(x, y, "Procesos", g_ptab == 0 ? "Aplicaciones y servicios: CPU y memoria repartidas por dueno (aunque compartan hilo)" : "Hilos del nucleo y procesos de usuario: tiempo de CPU propio de cada hilo");
    /* pestanas */
    const char *tabs[2] = { "Aplicaciones", "Hilos" };
    for (int i = 0; i < 2; i++) { int bx = x + i * 130, by = y + 62; fb_fill_rounded_rect(bx, by, 122, 30, 9, g_ptab == i ? KC_SELBG : 0x00182133); fb_stroke_rrect_aa(bx, by, 122, 30, 9, g_ptab == i ? 0xFF2B5C8Au : 0xFF263142u); tx(bx + (122 - tw(tabs[i], 12, 0)) / 2, by, 30, tabs[i], 12, KC_TXT, 0); }
    build_rows();
    int ty = y + 104, cx_cpu = x + w - 330, cx_mem = x + w - 130, cx_st = x + w * 42 / 100;
    card(x, ty, w, h - (ty - y));
    /* cabecera */
    tx(x + 16, ty + 6, 28, g_sort == 3 ? "Nombre  v" : "Nombre", 11, g_sort == 3 ? KC_ACC : KC_DIM, 1);
    tx(cx_st, ty + 6, 28, g_ptab ? "Tipo / estado" : "Tipo", 11, KC_DIM, 1);
    tx(cx_cpu, ty + 6, 28, g_sort == 1 ? "CPU  v" : "CPU", 11, g_sort == 1 ? KC_ACC : KC_DIM, 1);
    txr(x + w - 16, ty + 6, 28, g_sort == 2 ? "v  Memoria" : g_ptab ? "Pila" : "Memoria", 11, g_sort == 2 ? KC_ACC : KC_DIM, 1);
    fb_fill_rect((uint32_t)(x + 8), (uint32_t)(ty + 34), (uint32_t)(w - 16), 1, KC_LINE);
    int rh = 34, ry = ty + 40, vis = (h - (ty - y) - 40 - 50) / rh;
    if (g_scroll > g_nrows - vis) g_scroll = g_nrows - vis;
    if (g_scroll < 0) g_scroll = 0;
    for (int k = 0; k < vis && k + g_scroll < g_nrows; k++) {
        int i = k + g_scroll; struct prow *r = &g_rows[i]; int yy = ry + k * rh;
        if (i == g_sel) fb_fill_rrect_aa(x + 8, yy, w - 16, rh - 2, 8, 0xFF1A3558u);
        fb_fill_rrect_aa(x + 18, yy + 11, 10, 10, 3, 0xFF000000u | (r->kind == 0 ? C_CPU : r->kind == 1 ? C_RAM : r->user ? C_NETT : 0x0064748B));
        char nm[64] = ""; s_cat(nm, r->name, sizeof nm); if (r->kind == 2) { s_cat(nm, "  (", sizeof nm); s_num(nm, sizeof nm, r->tid); s_cat(nm, ")", sizeof nm); }
        fb_set_clip(x + 36, yy, cx_st - x - 44, rh); tx(x + 36, yy, rh, nm, 12, KC_TXT, r->kind == 0); fb_reset_clip();
        char st[48] = ""; s_cat(st, r->what, sizeof st);
        if (r->kind == 2) { s_cat(st, r->state == THREAD_STATE_RUNNING ? "  -  en marcha" : r->state == THREAD_STATE_SLEEPING ? "  -  dormido" : r->state == THREAD_STATE_READY ? "  -  listo" : "  -  terminado", sizeof st); }
        fb_set_clip(cx_st, yy, cx_cpu - cx_st - 10, rh); tx(cx_st, yy, rh, st, 11, KC_DIM, 0); fb_reset_clip();
        bar(cx_cpu, yy + 13, 120, 8, r->cpu, r->cpu > 500 ? 0x00F87171 : C_CPU);
        char c[16] = ""; s_pct(c, sizeof c, r->cpu); tx(cx_cpu + 130, yy, rh, c, 11, KC_TXT, 0);
        char m[24] = ""; s_bytes(m, sizeof m, r->mem); txr(x + w - 16, yy, rh, m, 11, KC_TXT, 0); (void)cx_mem;
    }
    /* pie con accion */
    int fy = y + h - 44;
    fb_fill_rect((uint32_t)(x + 8), (uint32_t)(fy - 6), (uint32_t)(w - 16), 1, KC_LINE);
    char t[96] = ""; s_num(t, sizeof t, (uint64_t)g_nrows); s_cat(t, g_ptab ? " hilos" : " entradas", sizeof t); s_cat(t, "   |   CPU total ", sizeof t); s_pct(t, sizeof t, cpu_pct);
    tx(x + 16, fy, 34, g_msg[0] ? g_msg : t, 12, g_msg[0] ? 0x00FBBF24 : KC_DIM, 0);
    if (g_sel >= 0 && g_sel < g_nrows) {
        struct prow *r = &g_rows[g_sel]; const char *lbl = 0;
        if (r->kind == 0 && r->id != ACCT_APP0 + gui_app_sysmon) lbl = "Cerrar aplicacion";
        if (r->kind == 2 && r->user) lbl = "Terminar proceso";
        if (lbl) { int bw = tw(lbl, 12, 1) + 32; fb_fill_rounded_rect(x + w - 16 - bw, fy + 2, bw, 30, 15, 0x00B91C1C); tx(x + w - 16 - bw + 16, fy + 2, 30, lbl, 12, 0x00FFFFFF, 1); }
    }
}
static void sec_mem(int x, int y, int w, int h)
{
    char sub[120] = ""; s_bytes(sub, sizeof sub, g_ram.footprint); s_cat(sub, " en uso de ", sizeof sub); s_bytes(sub, sizeof sub, g_ram.total); s_cat(sub, "  -  heap del nucleo: ", sizeof sub); s_bytes(sub, sizeof sub, g_heap.used_bytes); s_cat(sub, " de ", sizeof sub); s_bytes(sub, sizeof sub, kheap_max_bytes()); s_cat(sub, " maximo", sizeof sub);
    hdr(x, y, "Memoria", sub);
    int ty = y + 66, lw = 360 < w / 2 ? 360 : w / 2, ch = 300;
    card(x, ty, lw, ch);
    /* reparto */
    uint64_t tot = g_ram.total ? g_ram.total : 1;
    uint64_t apps = 0; for (int o = ACCT_APP0; o < ACCT_IDLE; o++) apps += own_mem[o];
    apps += own_mem[ACCT_WEB] + own_mem[ACCT_DESKTOP] + own_mem[ACCT_AGENT];
    uint64_t fs = own_mem[ACCT_RAMFS], scr = own_mem[ACCT_SCREEN], sys = own_mem[ACCT_SYSTEM] + own_mem[ACCT_NET];
    uint64_t img = g_ram.image, other = g_ram.footprint > img + apps + fs + scr + sys ? g_ram.footprint - img - apps - fs - scr - sys : 0;
    uint64_t parts[6] = { img, sys, scr, fs, apps, other };
    static const uint32_t cols[6] = { 0x0064748B, 0x0038BDF8, 0x00F472B6, 0x0034D399, 0x00A78BFA, 0x00F59E0B };
    static const char *const names[6] = { "Imagen del nucleo", "Nucleo (heap)", "Pantalla", "RamFS (archivos)", "Aplicaciones", "Pilas, tablas y reservas" };
    int fr[6], sum = 0; for (int i = 0; i < 6; i++) { fr[i] = (int)(parts[i] * 1000 / tot); sum += fr[i]; }
    int dcx = x + 100, dcy = ty + 120;
    donut(dcx, dcy, 80, 54, fr, cols, 6, 0x00243042);
    char big[24] = ""; s_pct(big, sizeof big, g_ram.total ? (int)(g_ram.footprint * 1000 / g_ram.total) : 0);
    tx(dcx - tw(big, 17, 1) / 2, dcy - 20, 26, big, 17, KC_TXT, 1); tx(dcx - tw("en uso", 11, 0) / 2, dcy + 4, 18, "en uso", 11, KC_DIM, 0);
    int ly = ty + 22;
    for (int i = 0; i < 6; i++) {
        fb_fill_rrect_aa(x + 200, ly + 8, 10, 10, 3, 0xFF000000u | cols[i]);
        fb_set_clip(x + 216, ly, lw - 220, 40); tx(x + 216, ly, 22, names[i], 11, KC_TXT, 0);
        char t[24] = ""; s_bytes(t, sizeof t, parts[i]); tx(x + 216, ly + 18, 18, t, 11, KC_DIM, 0); fb_reset_clip();
        ly += 42;
    }
    { char t[64] = "Libre: "; s_bytes(t, sizeof t, g_ram.free_b); tx(x + 20, ty + ch - 50, 22, t, 12, KC_TXT, 1); }
    { char t[80] = "Pico del heap: "; s_bytes(t, sizeof t, g_heap.peak_used_bytes); s_cat(t, "  -  bloques: ", sizeof t); s_num(t, sizeof t, g_heap.live_blocks); tx(x + 20, ty + ch - 28, 20, t, 11, KC_DIM, 0); }
    /* historial */
    int gx = x + lw + 12, gw = w - lw - 12;
    card(gx, ty, gw, ch); tx(gx + 16, ty + 8, 26, "Uso de memoria (2 minutos)", 13, KC_TXT, 1);
    graph(gx + 14, ty + 42, gw - 28, ch - 100, h_ram, 0, 1000, C_RAM, 1);
    { char t[120] = "Fallos de reserva: "; s_num(t, sizeof t, g_heap.failed_allocs); s_cat(t, "   Reservas: ", sizeof t); s_num(t, sizeof t, g_heap.alloc_calls); s_cat(t, "   Liberaciones: ", sizeof t); s_num(t, sizeof t, g_heap.free_calls); tx(gx + 16, ty + ch - 48, 22, t, 11, KC_DIM, 0); }
    { char t[120] = "Heap mapeado: "; s_bytes(t, sizeof t, g_heap.mapped_bytes); s_cat(t, "   Corrupciones detectadas: ", sizeof t); s_num(t, sizeof t, g_heap.corruptions); tx(gx + 16, ty + ch - 28, 20, t, 11, g_heap.corruptions ? 0x00F87171 : KC_DIM, 0); }
    /* tabla por dueno */
    int by = ty + ch + 12, bh = y + h - by; if (bh < 80) return;
    card(x, by, w, bh); tx(x + 16, by + 8, 26, "Memoria del heap por aplicacion y servicio", 13, KC_TXT, 1);
    int idx[64], n = 0; for (int o = 0; o < 256 && n < 64; o++) if (own_mem[o]) idx[n++] = o;
    for (int i = 1; i < n; i++) { int t = idx[i], j = i - 1; while (j >= 0 && own_mem[idx[j]] < own_mem[t]) { idx[j + 1] = idx[j]; j--; } idx[j + 1] = t; }
    uint64_t mx = n ? own_mem[idx[0]] : 1;
    int cols2 = w > 900 ? 2 : 1, colw = (w - 32) / cols2, ry = by + 40, rows = (bh - 50) / 28;
    for (int i = 0; i < n && i < rows * cols2; i++) {
        int cx = x + 16 + (i / rows) * colw, yy = ry + (i % rows) * 28;
        fb_set_clip(cx, yy, colw / 2 - 10, 26); tx(cx, yy, 26, owner_name(idx[i]), 12, KC_TXT, 0); fb_reset_clip();
        bar(cx + colw / 2 - 4, yy + 10, colw / 2 - 110, 7, (int)(own_mem[idx[i]] * 1000 / mx), C_RAM);
        char t[24] = ""; s_bytes(t, sizeof t, own_mem[idx[i]]); txr(cx + colw - 16, yy, 26, t, 11, KC_DIM, 0);
    }
}
static void sec_cpu(int x, int y, int w, int h)
{
    char sub[120] = ""; s_cat(sub, g_cpu.brand, sizeof sub); s_cat(sub, "  -  uso ", sizeof sub); s_pct(sub, sizeof sub, cpu_pct);
    hdr(x, y, "Procesador", sub);
    int ty = y + 66, gh = 210;
    card(x, ty, w, gh); tx(x + 16, ty + 8, 26, "Uso total (2 minutos)", 13, KC_TXT, 1);
    char t[32] = ""; s_pct(t, sizeof t, cpu_pct); txr(x + w - 16, ty + 6, 30, t, 18, C_CPU, 1);
    graph(x + 14, ty + 42, w - 28, gh - 56, h_cpu, 0, 1000, C_CPU, 1);
    int cy = ty + gh + 12, cw = ncores > 1 ? (w - 12 * (ncores - 1)) / ncores : 0, ch = 120;
    if (ncores > 1 && cw > 90) {
        for (int c = 0; c < ncores; c++) {
            int cx = x + c * (cw + 12); card(cx, cy, cw, ch);
            char n[32] = "Nucleo "; s_num(n, sizeof n, (uint64_t)c); tx(cx + 12, cy + 6, 24, n, 11, KC_DIM, 1);
            char p[16] = ""; s_pct(p, sizeof p, core_pct[c]); txr(cx + cw - 12, cy + 6, 24, p, 11, KC_TXT, 1);
            graph(cx + 10, cy + 34, cw - 20, ch - 46, h_core[c], 0, 1000, C_CPU, 0);
        }
        cy += ch + 12;
    }
    int dh = y + h - cy; if (dh < 80) return;
    card(x, cy, w, dh);
    int kx = x + 20, kw = (w - 60) / 2, ky = cy + 14, k2 = x + 40 + kw;
    char v[96];
    kv(kx, ky, kw, "Modelo", g_cpu.brand); kv(k2, ky, kw, "Fabricante", g_cpu.vendor); ky += 30;
    kv(kx, ky, kw, "Arquitectura", g_cpu.arch);
    v[0] = 0; s_num(v, sizeof v, (uint64_t)g_cpu.cores); s_cat(v, " nucleos, ", sizeof v); s_num(v, sizeof v, (uint64_t)g_cpu.threads); s_cat(v, " hilos (SOMA usa ", sizeof v); s_num(v, sizeof v, (uint64_t)ncores); s_cat(v, ")", sizeof v); kv(k2, ky, kw, "Nucleos", v); ky += 30;
    v[0] = 0; if (cur_mhz || g_cpu.mhz) { s_num(v, sizeof v, cur_mhz ? cur_mhz : g_cpu.mhz); s_cat(v, " MHz", sizeof v); if (g_cpu.max_mhz > (cur_mhz ? cur_mhz : g_cpu.mhz)) { s_cat(v, " (max ", sizeof v); s_num(v, sizeof v, g_cpu.max_mhz); s_cat(v, ")", sizeof v); } } else s_cpy(v, "-", sizeof v);
    kv(kx, ky, kw, "Frecuencia", v);
    v[0] = 0; s_cat(v, "familia ", sizeof v); s_num(v, sizeof v, (uint64_t)g_cpu.family); s_cat(v, ", modelo ", sizeof v); s_num(v, sizeof v, (uint64_t)g_cpu.model); s_cat(v, ", rev. ", sizeof v); s_num(v, sizeof v, (uint64_t)g_cpu.stepping); kv(k2, ky, kw, "Identificador", v); ky += 30;
    v[0] = 0; if (g_cpu.l1d_kb) { s_cat(v, "L1 ", sizeof v); s_num(v, sizeof v, g_cpu.l1d_kb); s_cat(v, "+", sizeof v); s_num(v, sizeof v, g_cpu.l1i_kb); s_cat(v, " KiB", sizeof v); }
    if (g_cpu.l2_kb) { s_cat(v, "  L2 ", sizeof v); s_num(v, sizeof v, g_cpu.l2_kb); s_cat(v, " KiB", sizeof v); }
    if (g_cpu.l3_kb) { s_cat(v, "  L3 ", sizeof v); s_num(v, sizeof v, g_cpu.l3_kb >= 1024 ? g_cpu.l3_kb / 1024 : g_cpu.l3_kb); s_cat(v, g_cpu.l3_kb >= 1024 ? " MiB" : " KiB", sizeof v); }
    if (!v[0]) s_cpy(v, "-", sizeof v);
    kv(kx, ky, kw, "Caches", v);
    v[0] = 0; if (temp_mc >= 0) { s_num(v, sizeof v, (uint64_t)(temp_mc / 1000)); s_cat(v, ",", sizeof v); s_num(v, sizeof v, (uint64_t)(temp_mc % 1000 / 100)); s_cat(v, " C", sizeof v); if (g_cpu.volt_mv > 0) { s_cat(v, "  -  ", sizeof v); s_num(v, sizeof v, (uint64_t)g_cpu.volt_mv); s_cat(v, " mV", sizeof v); } } else s_cpy(v, "no disponible", sizeof v);
    kv(k2, ky, kw, "Temperatura", v); ky += 30;
    kv(kx, ky, kw, "Virtualizacion", g_cpu.hyper[0] ? g_cpu.hyper : "no (hardware real)");
    v[0] = 0; s_num(v, sizeof v, (uint64_t)sched_preempt_count); kv(k2, ky, kw, "Cambios por rodaja", v); ky += 30;
    if (ky + 40 < cy + dh) { tx(kx, ky, 24, "Extensiones", 12, KC_DIM, 0); fb_set_clip(kx, ky + 22, w - 40, 40); tx(kx, ky + 22, 22, g_cpu.feats[0] ? g_cpu.feats : "-", 12, KC_TXT, 0); fb_reset_clip(); }
}
static void sec_gpu(int x, int y, int w, int h)
{
    struct fb_info fi; fb_get_info(&fi);
    char sub[120] = ""; s_cat(sub, g_gpu.name, sizeof sub); s_cat(sub, "  -  ", sizeof sub); s_num(sub, sizeof sub, (uint64_t)fps); s_cat(sub, " fotogramas por segundo", sizeof sub);
    hdr(x, y, "Graficos y pantalla", sub);
    int ty = y + 66, gh = 220;
    card(x, ty, w, gh); tx(x + 16, ty + 8, 26, "Fotogramas por segundo (volcados a pantalla)", 13, KC_TXT, 1);
    char t[24] = ""; s_num(t, sizeof t, (uint64_t)fps); s_cat(t, " fps", sizeof t); txr(x + w - 16, ty + 6, 30, t, 18, C_GPU, 1);
    graph(x + 14, ty + 42, w - 28, gh - 56, h_fps, 0, 0, C_GPU, 1);
    int cy = ty + gh + 12, dh = y + h - cy; if (dh < 80) return;
    card(x, cy, w, dh);
    int kx = x + 20, kw = (w - 60) / 2, ky = cy + 14, k2 = x + 40 + kw; char v[96];
    kv(kx, ky, kw, "Procesador grafico", g_gpu.name); kv(k2, ky, kw, "Fabricante", g_gpu.vendor[0] ? g_gpu.vendor : "-"); ky += 30;
    v[0] = 0; s_num(v, sizeof v, fi.w); s_cat(v, " x ", sizeof v); s_num(v, sizeof v, fi.h); s_cat(v, " a ", sizeof v); s_num(v, sizeof v, fi.bpp); s_cat(v, " bits", sizeof v); kv(kx, ky, kw, "Resolucion", v);
    kv(k2, ky, kw, "Controlador", fi.driver ? fi.driver : "-"); ky += 30;
    v[0] = 0; s_bytes(v, sizeof v, fi.backbuffer_bytes); s_cat(v, " (doble bufer)", sizeof v); kv(kx, ky, kw, "Memoria de pantalla", v);
    v[0] = 0; if (fi.tr_bytes) s_bytes(v, sizeof v, fi.tr_bytes); else s_cpy(v, "0 (opacas)", sizeof v); kv(k2, ky, kw, "Capas de transparencia", v); ky += 30;
    v[0] = 0; s_num(v, sizeof v, fi.presents); kv(kx, ky, kw, "Fotogramas desde el arranque", v);
    v[0] = 0; s_num(v, sizeof v, fi.pitch); s_cat(v, " bytes por linea", sizeof v); kv(k2, ky, kw, "Paso", v); ky += 30;
    v[0] = 0; if (g_gpu.core_mhz) { s_num(v, sizeof v, g_gpu.core_mhz); s_cat(v, " MHz nucleo  -  ", sizeof v); s_num(v, sizeof v, g_gpu.v3d_mhz); s_cat(v, " MHz V3D", sizeof v); } else s_cpy(v, "-", sizeof v);
    kv(kx, ky, kw, "Frecuencias", v);
    v[0] = 0; if (g_gpu.mem_mb) { s_num(v, sizeof v, g_gpu.mem_mb); s_cat(v, " MiB", sizeof v); } else s_cpy(v, "-", sizeof v); kv(k2, ky, kw, "Memoria de la GPU", v); ky += 30;
    tx(kx, ky + 4, 22, "SOMA dibuja por software (sin aceleracion 3D): la GPU solo muestra el bufer final.", 11, KC_FAINT, 0);
}
static void sec_disk(int x, int y, int w, int h)
{
    hdr(x, y, "Almacenamiento", "Sistema de archivos en memoria, almacen grande y discos");
    uint32_t files = 0, bytes = 0, dirty = 0; vfs_get_stats(&files, &bytes, &dirty);
    int ty = y + 66, cw = (w - 12) / 2, ch = 170;
    card(x, ty, cw, ch); sec_icon(S_DISK, x + 28, ty + 24, C_NETR); tx(x + 46, ty + 10, 28, "RamFS", 14, KC_TXT, 1);
    char t[96] = ""; s_num(t, sizeof t, files); s_cat(t, " de ", sizeof t); s_num(t, sizeof t, FS_MAX_FILES); s_cat(t, " archivos", sizeof t);
    tx(x + 20, ty + 48, 24, t, 12, KC_TXT, 0); bar(x + 20, ty + 76, cw - 40, 10, (int)(files * 1000 / FS_MAX_FILES), C_NETR);
    t[0] = 0; s_bytes(t, sizeof t, bytes); s_cat(t, " de datos  -  ", sizeof t); s_bytes(t, sizeof t, own_mem[ACCT_RAMFS]); s_cat(t, " en memoria", sizeof t); tx(x + 20, ty + 96, 22, t, 11, KC_DIM, 0);
    int p = vfs_big_persist(); tx(x + 20, ty + 122, 22, p > 0 ? "Se guarda en disco (los grandes en BIGFS)" : p == 0 ? "Disco sin BIGFS: los archivos grandes solo en memoria" : "Sin disco: todo vive en memoria", 11, p > 0 ? 0x0034D399 : 0x00FBBF24, 0);
    card(x + cw + 12, ty, cw, ch); sec_icon(S_DISK, x + cw + 40, ty + 24, C_RAM); tx(x + cw + 58, ty + 10, 28, "BIGFS (archivos grandes)", 14, KC_TXT, 1);
    int e = 0; struct bigfs *bf = virtio_blk_capacity() ? bigfs_sys(&e) : 0;
    if (bf) {
        struct bigfs_info bi; bigfs_info(bf, &bi);
        uint64_t tb = bi.total_clusters * bi.cluster_bytes, fb2 = bi.free_clusters * bi.cluster_bytes;
        t[0] = 0; s_bytes(t, sizeof t, tb - fb2); s_cat(t, " usados de ", sizeof t); s_bytes(t, sizeof t, tb); tx(x + cw + 32, ty + 48, 24, t, 12, KC_TXT, 0);
        bar(x + cw + 32, ty + 76, cw - 40, 10, tb ? (int)((tb - fb2) * 1000 / tb) : 0, C_RAM);
        t[0] = 0; s_num(t, sizeof t, bi.used_inodes); s_cat(t, " de ", sizeof t); s_num(t, sizeof t, bi.total_inodes); s_cat(t, " entradas", sizeof t); tx(x + cw + 32, ty + 96, 22, t, 11, KC_DIM, 0);
    } else tx(x + cw + 32, ty + 48, 24, virtio_blk_capacity() ? "Sin formatear (se formatea solo al guardar algo grande)" : "No hay disco virtio", 12, KC_DIM, 0);
    int dy = ty + ch + 12, dh = y + h - dy; if (dh < 80) return;
    card(x, dy, w, dh); tx(x + 16, dy + 8, 26, "Discos", 13, KC_TXT, 1);
    int ky = dy + 42;
    uint64_t cap = virtio_blk_capacity();
    if (cap) { t[0] = 0; s_bytes(t, sizeof t, cap * 512); s_cat(t, "  -  SOMAFS02 + fuentes + BIGFS", sizeof t); kv(x + 20, ky, w - 40, "virtio-blk (disco del sistema)", t); ky += 30; }
    for (int i = 0; i < g_ndev && ky + 28 < dy + dh; i++) { uint32_t c = g_dev[i].cls >> 16; if (cap && g_dev[i].vendor == 0x1af4) continue; if (c == 0x01 || (g_dev[i].bus[0] == 'S' && g_dev[i].bus[1] == 'D')) { kv(x + 20, ky, w - 40, g_dev[i].name, g_dev[i].extra); ky += 30; } }
    if (!cap) tx(x + 20, ky, 24, "Sin disco del sistema: los archivos viven en memoria y en la tarjeta SD (orden 'fat').", 12, KC_DIM, 0);
}
static void sec_net(int x, int y, int w, int h)
{
    struct sysinfo si; sysinfo_get(&si);
    char sub[120] = "Bajada "; s_bytes(sub, sizeof sub, rx_rate); s_cat(sub, "/s  -  subida ", sizeof sub); s_bytes(sub, sizeof sub, tx_rate); s_cat(sub, "/s", sizeof sub);
    hdr(x, y, "Red", sub);
    int ty = y + 66, gh = 220, gw = (w - 12) / 2;
    card(x, ty, gw, gh); tx(x + 16, ty + 8, 26, "Bajada", 13, C_NETR, 1); graph(x + 14, ty + 42, gw - 28, gh - 56, h_rx, 1, 0, C_NETR, 1);
    card(x + gw + 12, ty, gw, gh); tx(x + gw + 28, ty + 8, 26, "Subida", 13, C_NETT, 1); graph(x + gw + 26, ty + 42, gw - 28, gh - 56, h_tx, 1, 0, C_NETT, 1);
    int cy = ty + gh + 12, dh = y + h - cy; if (dh < 80) return;
    card(x, cy, w, dh); tx(x + 16, cy + 8, 26, "Interfaces", 13, KC_TXT, 1);
    int ky = cy + 42;
    const struct netif_ops *act = netif_active();
    for (int i = 0; i < netif_count() && ky + 60 < cy + dh; i++) {
        const struct netif_ops *o = netif_get(i); if (!o) continue;
        int up = netif_is_up(i), ln = up && o->link ? o->link() : 0;
        fb_fill_rrect_aa(x + 20, ky + 8, 10, 10, 5, 0xFF000000u | (ln ? 0x0034D399 : up ? 0x00FBBF24 : 0x00475569));
        char t[120] = ""; s_cat(t, o->name, sizeof t); if (o == act) s_cat(t, "  (activa)", sizeof t); tx(x + 38, ky, 26, t, 13, KC_TXT, 1);
        t[0] = 0; s_cat(t, o->desc ? o->desc : "", sizeof t); tx(x + 200, ky, 26, t, 11, KC_DIM, 0);
        const uint8_t *m = up && o->mac ? o->mac() : 0; char mac[24] = "-";
        if (m) { const char *hx = "0123456789abcdef"; int k = 0; for (int j = 0; j < 6; j++) { mac[k++] = hx[m[j] >> 4]; mac[k++] = hx[m[j] & 15]; if (j < 5) mac[k++] = ':'; } mac[k] = 0; }
        uint32_t tp = 0, rp = 0; uint64_t tb = 0, rb = 0; if (up && o->stats) o->stats(&tp, &rp, &tb, &rb);
        t[0] = 0; s_cat(t, "MAC ", sizeof t); s_cat(t, mac, sizeof t); s_cat(t, "   recibido ", sizeof t); s_bytes(t, sizeof t, rb); s_cat(t, "   enviado ", sizeof t); s_bytes(t, sizeof t, tb);
        s_cat(t, ln ? "   enlace arriba" : up ? "   sin enlace" : "   apagada", sizeof t);
        tx(x + 38, ky + 24, 22, t, 11, KC_DIM, 0);
        ky += 58;
    }
    if (si.ip && ky + 30 < cy + dh) {
        char t[96] = "IP "; for (int j = 0; j < 4; j++) { s_num(t, sizeof t, si.ip[j]); if (j < 3) s_cat(t, ".", sizeof t); }
        if (si.gw) { s_cat(t, "   puerta de enlace ", sizeof t); for (int j = 0; j < 4; j++) { s_num(t, sizeof t, si.gw[j]); if (j < 3) s_cat(t, ".", sizeof t); } }
        tx(x + 20, ky + 4, 24, t, 12, KC_TXT, 0);
    }
}
static void sec_hw(int x, int y, int w, int h)
{
    hdr(x, y, "Hardware", g_board.model);
    int ty = y + 66, lw = (w - 12) * 4 / 10, ch = h - 66;
    card(x, ty, lw, ch); tx(x + 16, ty + 8, 26, "Equipo", 13, KC_TXT, 1);
    int kx = x + 16, kw = lw - 32, ky = ty + 42; char v[96];
    kv(kx, ky, kw, "Modelo", g_board.model); ky += 30;
    v[0] = 0; s_bytes(v, sizeof v, (uint64_t)g_board.ram_mb << 20); kv(kx, ky, kw, "Memoria instalada", v); ky += 30;
    if (g_board.gpu_mem_mb) { v[0] = 0; s_num(v, sizeof v, g_board.gpu_mem_mb); s_cat(v, " MiB", sizeof v); kv(kx, ky, kw, "Reservada a la GPU", v); ky += 30; }
    kv(kx, ky, kw, "Firmware", g_board.firmware[0] ? g_board.firmware : "-"); ky += 30;
    if (g_board.revision) { v[0] = 0; const char *hx = "0123456789abcdef"; char r[11] = "0x"; for (int i = 0; i < 8; i++) r[2 + i] = hx[(g_board.revision >> (28 - 4 * i)) & 15]; r[10] = 0; s_cat(v, r, sizeof v); kv(kx, ky, kw, "Revision", v); ky += 30; }
    if (g_board.serial[0]) { kv(kx, ky, kw, "Numero de serie", g_board.serial); ky += 30; }
    kv(kx, ky, kw, "Procesador", g_cpu.brand); ky += 30;
    kv(kx, ky, kw, "Graficos", g_gpu.name); ky += 30;
    v[0] = 0; s_cat(v, "SOMA 0.3 pre-beta ", sizeof v); s_cat(v, g_cpu.arch, sizeof v); kv(kx, ky, kw, "Sistema", v); ky += 30;
    v[0] = 0; s_uptime(v, sizeof v); kv(kx, ky, kw, "Tiempo encendido", v);
    int dx = x + lw + 12, dw = w - lw - 12;
    card(dx, ty, dw, ch); { char t[48] = "Dispositivos ("; s_num(t, sizeof t, (uint64_t)g_ndev); s_cat(t, ")", sizeof t); tx(dx + 16, ty + 8, 26, t, 13, KC_TXT, 1); }
    int ry = ty + 42, rows = (ch - 50) / 32;
    if (g_scroll > g_ndev - rows) g_scroll = g_ndev - rows;
    if (g_scroll < 0) g_scroll = 0;
    for (int k = 0; k < rows && k + g_scroll < g_ndev; k++) {
        struct hw_dev *d = &g_dev[k + g_scroll]; int yy = ry + k * 32;
        fb_fill_rrect_aa(dx + 16, yy + 6, 46, 20, 6, 0xFF1E293Bu); tx(dx + 16 + (46 - tw(d->bus, 10, 1)) / 2, yy + 6, 20, d->bus, 10, KC_ACC, 1);
        char t[64] = ""; s_cat(t, d->extra, sizeof t);
        if (d->vendor) { const char *hx = "0123456789abcdef"; char id[12]; id[0] = ' '; for (int i = 0; i < 4; i++) id[1 + i] = hx[(d->vendor >> (12 - 4 * i)) & 15]; id[5] = ':'; for (int i = 0; i < 4; i++) id[6 + i] = hx[(d->device >> (12 - 4 * i)) & 15]; id[10] = 0; s_cat(t, id, sizeof t); }
        int ew = tw(t, 10, 0); txr(dx + dw - 16, yy, 32, t, 10, KC_DIM, 0);
        fb_set_clip(dx + 72, yy, dw - 100 - ew, 32); tx(dx + 72, yy, 32, d->name, 12, KC_TXT, 0); fb_reset_clip();
    }
}

/* ---------------------------------------------------------------- ventana */
#define SIDE 196
void sysmon_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused)
{
    (void)focused;
    sysmon_init();
    int x = (int)wx, y = (int)wy, w = (int)ww, h = (int)wh;
    fb_fill_rect(wx, wy, ww, wh, KC_WIN);
    fb_fill_rect(wx, wy, SIDE, wh, KC_SIDE); fb_fill_rect((uint32_t)(x + SIDE - 1), wy, 1, wh, KC_LINE);
    tx(x + 20, y + 16, 30, "Monitor", 18, KC_TXT, 1);
    tx(x + 20, y + 44, 20, "del sistema", 12, KC_DIM, 0);
    for (int s = 0; s < S_N; s++) {
        int sy = y + 84 + s * 42, on = s == g_sec;
        if (on) { fb_fill_rrect_aa(x + 10, sy, SIDE - 20, 36, 9, 0xFF1A3558u); fb_fill_rect((uint32_t)(x + 10), (uint32_t)(sy + 8), 3, 20, KC_ACC); }
        sec_icon(s, x + 34, sy + 18, on ? KC_ACC : KC_DIM);
        tx(x + 54, sy, 36, SEC_NAME[s], 13, on ? KC_TXT : KC_DIM, on);
    }
    /* mini estado en la barra lateral */
    int my = y + h - 120;
    if (my > y + 84 + S_N * 42 + 10) {
        char t[32] = "CPU "; s_pct(t, sizeof t, cpu_pct); tx(x + 20, my, 20, t, 11, KC_DIM, 0); bar(x + 20, my + 22, SIDE - 40, 6, cpu_pct, C_CPU);
        char m[48] = "RAM "; s_pct(m, sizeof m, g_ram.total ? (int)(g_ram.footprint * 1000 / g_ram.total) : 0); tx(x + 20, my + 36, 20, m, 11, KC_DIM, 0); bar(x + 20, my + 58, SIDE - 40, 6, g_ram.total ? (int)(g_ram.footprint * 1000 / g_ram.total) : 0, C_RAM);
        if (temp_mc >= 0) { char tt[32] = ""; s_num(tt, sizeof tt, (uint64_t)(temp_mc / 1000)); s_cat(tt, " C", sizeof tt); tx(x + 20, my + 72, 20, tt, 11, C_TEMP, 1); }
    }
    int cx = x + SIDE + 20, cy = y + 16, cw = w - SIDE - 40, ch = h - 32;
    fb_set_clip(x + SIDE, y, w - SIDE, h);
    switch (g_sec) {
    case S_SUM: sec_summary(cx, cy, cw, ch); break;
    case S_PROC: sec_proc(cx, cy, cw, ch); break;
    case S_MEM: sec_mem(cx, cy, cw, ch); break;
    case S_CPU: sec_cpu(cx, cy, cw, ch); break;
    case S_GPU: sec_gpu(cx, cy, cw, ch); break;
    case S_DISK: sec_disk(cx, cy, cw, ch); break;
    case S_NET: sec_net(cx, cy, cw, ch); break;
    default: sec_hw(cx, cy, cw, ch); break;
    }
    fb_reset_clip();
}
int sysmon_handle_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    g_ver++;
    int x = (int)wx, y = (int)wy, w = (int)ww, h = (int)wh;
    if (mx < x + SIDE) {
        int s = (my - (y + 84)) / 42;
        if (my >= y + 84 && s >= 0 && s < S_N) { g_sec = s; g_scroll = 0; g_sel = -1; g_msg[0] = 0; }
        return 0;
    }
    if (g_sec != S_PROC) return 0;
    int cx = x + SIDE + 20, cy = y + 16, cw = w - SIDE - 40, ch = h - 32;
    if (my >= cy + 62 && my < cy + 92) { int t = (mx - cx) / 130; if (t >= 0 && t < 2) { g_ptab = t; g_sel = -1; g_scroll = 0; g_msg[0] = 0; } return 0; }
    int ty = cy + 104;
    if (my >= ty && my < ty + 34) {
        int cx_cpu = cx + cw - 330;
        g_sort = mx >= cx + cw - 120 ? 2 : mx >= cx_cpu ? 1 : mx < cx + cw * 42 / 100 ? 3 : g_sort;
        return 0;
    }
    int fy = cy + ch - 44;
    if (my >= fy && g_sel >= 0 && g_sel < g_nrows) {
        struct prow *r = &g_rows[g_sel];
        if (r->kind == 0 && r->id != ACCT_APP0 + gui_app_sysmon && mx > cx + cw / 2) {
            const char *nm = r->name;
            if (gui_close_app(r->id - ACCT_APP0)) { s_cpy(g_msg, "Cerrada: ", sizeof g_msg); s_cat(g_msg, nm, sizeof g_msg); g_sel = -1; return 2; }
        }
        if (r->kind == 2 && r->user && mx > cx + cw / 2) {
            if (thread_kill(r->tid) == 0) { s_cpy(g_msg, "Proceso terminado: ", sizeof g_msg); s_cat(g_msg, r->name, sizeof g_msg); } else s_cpy(g_msg, "No se pudo terminar el proceso", sizeof g_msg);
            g_sel = -1; return 0;
        }
        return 0;
    }
    int ry = ty + 40;
    if (my >= ry) { int i = (my - ry) / 34 + g_scroll; g_sel = i < g_nrows ? i : -1; g_msg[0] = 0; }
    return 0;
}
void sysmon_handle_key(char ch)
{
    g_ver++;
    int c = (uint8_t)ch;
    if (c >= '1' && c <= '8') { g_sec = c - '1'; g_scroll = 0; g_sel = -1; }
    else if (c == 9 || c == 14) { g_sec = (g_sec + 1) % S_N; g_scroll = 0; g_sel = -1; }
    else if (c == 16) { g_sec = (g_sec + S_N - 1) % S_N; g_scroll = 0; g_sel = -1; }
    else if (c == 'h' || c == 'H') { if (g_sec == S_PROC) { g_ptab = !g_ptab; g_sel = -1; } }
    else if (c == 'c') g_sort = 1;
    else if (c == 'm') g_sort = 2;
    else if (c == 'n') g_sort = 3;
}
int sysmon_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel)
{
    (void)mx; (void)my; (void)x; (void)y; (void)w; (void)h;
    if (!wheel) return 0;
    if (g_sec == S_PROC || g_sec == S_HW) { g_scroll += wheel > 0 ? 3 : -3; if (g_scroll < 0) g_scroll = 0; g_ver++; return 1; }
    return 0;
}
