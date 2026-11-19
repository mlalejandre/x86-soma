/* PARCHE 085: Ajustes de la interfaz con la estetica del explorador. Apariencia: transparencia de las ventanas (40-100 %, con opcion
 * de dejar opaca la activa). SOMA_184: pagina Fuentes: fuente del escritorio, de la interfaz y los documentos (90-120 %) y del editor de
 * texto, elegidas del catalogo general (las mismas fuentes que usa el navegador). Los cambios se aplican al momento y se guardan. */
#include "settings.h"
#include "fb.h"
#include "fs.h"
#include "web_font.h"
#include "gui_font.h"
#include "netif.h"          /* SOMA_175C: pagina Redes */
#include "net.h"
#include "idt.h"           /* timer_get_uptime_ms */
#include "llm.h"           /* SOMA_195: pagina IA */
#include "llm_proto.h"
#include "thread.h"
#if defined(__x86_64__)
#include "netmgr.h"
#include "iwl.h"
#include "wpa.h"
#endif

#define KC_WIN     0x000C111B
#define KC_BAR     0x00131A27
#define KC_SIDE    0x000F1520
#define KC_LINE    0x00212B3B
#define KC_CARD    0x00111827
#define KC_TXT     0x00E5EAF2
#define KC_DIM     0x008592A8
#define KC_FAINT   0x00566379
#define KC_ACC     0x0038BDF8
#define KC_SELBG   0x001A3558
#define KC_PRIMARY 0x000B6BCB

static int g_opacity = 100, g_active_opaque = 0, g_loaded;
static int g_editor_face;                         /* SOMA_184: cara del editor de texto (FFACE_*) */
static char g_desk[40];                           /* SOMA_184: fuente del escritorio elegida en Ajustes (nombre del catalogo; vacio = la de siempre) */
static int g_drag;
static char g_msg[96] = "Los cambios se aplican al momento y se guardan en /etc/ui.conf";
static const int SCALES[4] = { 90, 100, 110, 120 };
static const char *const SCALE_NAME[4] = { "Pequena", "Normal", "Grande", "Muy grande" };

static int s_len(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void s_cat(char *d, const char *s, int max) { int i = s_len(d), j = 0; while (s && s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }
static void s_num(char *d, int max, int v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); char r[12]; for (int i = 0; i < n; i++) r[i] = t[n - 1 - i]; r[n] = 0; s_cat(d, r, max); }
static int g_wf = -1;
static int has_wf(void) { if (g_wf < 0) g_wf = wf_init() ? 1 : 0; return g_wf; }
static int tw(const char *s, int px, int bold) { int n = s_len(s); return has_wf() ? wf_width(s, (uint32_t)n, px, bold ? WF_BOLD : 0) : n * 8; }
static void tx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold)
{
    int n = s_len(s); if (!n) return;
    if (has_wf()) { int p = wf_ui_px(px); wf_draw(x, top + (h + (p * 72) / 100) / 2, s, (uint32_t)n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}

int settings_opacity(void) { return g_opacity; }
int settings_active_opaque(void) { return g_active_opaque; }

static void save(void)
{
    char b[224] = "opacidad="; s_num(b, sizeof b, g_opacity);
    s_cat(b, "\nactiva_opaca=", sizeof b); s_num(b, sizeof b, g_active_opaque);
    s_cat(b, "\nfuente=", sizeof b); s_cat(b, wf_ui_mono() ? "mono" : "sans", sizeof b);      /* compatible con versiones anteriores */
    s_cat(b, "\nfamilia=", sizeof b); s_num(b, sizeof b, wf_ui_family());                       /* SOMA_184 */
    s_cat(b, "\nescala=", sizeof b); s_num(b, sizeof b, wf_ui_scale());
    s_cat(b, "\neditor=", sizeof b); s_num(b, sizeof b, g_editor_face);
    if (g_desk[0]) { s_cat(b, "\nescritorio=", sizeof b); s_cat(b, g_desk, sizeof b); }
    s_cat(b, "\n", sizeof b);
    (void)vfs_write("/etc/ui.conf", b, (uint32_t)s_len(b));
}
static int val_of(const char *t, const char *key, int def)
{
    int kl = s_len(key);
    for (int i = 0; t[i]; i++) {
        if ((i == 0 || t[i - 1] == '\n')) {
            int j = 0; while (j < kl && t[i + j] == key[j]) j++;
            if (j == kl && t[i + j] == '=') { const char *v = t + i + j + 1; if (v[0] == 'm') return 1; if (v[0] == 's') return 0; int n = 0; while (*v >= '0' && *v <= '9') n = n * 10 + (*v++ - '0'); return n; }
        }
    }
    return def;
}
static int str_of(const char *t, const char *key, char *out, int max)       /* SOMA_184: valor de texto de una clave */
{
    int kl = s_len(key);
    out[0] = 0;
    for (int i = 0; t[i]; i++) {
        if (i == 0 || t[i - 1] == '\n') {
            int j = 0; while (j < kl && t[i + j] == key[j]) j++;
            if (j == kl && t[i + j] == '=') { const char *v = t + i + j + 1; int n = 0; while (v[n] && v[n] != '\n' && v[n] != '\r' && n < max - 1) { out[n] = v[n]; n++; } out[n] = 0; return n; }
        }
    }
    return 0;
}
int settings_editor_face(void) { return font_sysfont_off() ? FFACE_MONO : g_editor_face; }
void settings_load(void)
{
    if (g_loaded) return;
    g_loaded = 1;
    char b[224]; int n = vfs_read("/etc/ui.conf", b, sizeof b - 1);
    if (n <= 0) return;
    b[n] = 0;
    g_opacity = val_of(b, "opacidad", 100); if (g_opacity < 40) g_opacity = 40;
    if (g_opacity > 100) g_opacity = 100;
    g_active_opaque = val_of(b, "activa_opaca", 0) ? 1 : 0;
    int fam = val_of(b, "familia", -1);                                    /* SOMA_184: 0 DejaVu Sans, 1 Mono, 2 Liberation; antes solo "fuente=" */
    if (fam < 0) fam = val_of(b, "fuente", 0);
    int off = font_sysfont_off();                                           /* soma.sysfont=off: solo las fuentes de siempre */
    if (off && fam == WF_FAM_LIBERATION) fam = WF_FAM_SANS;
    wf_ui_set_family(fam, val_of(b, "escala", 100));
    g_editor_face = val_of(b, "editor", 0); if (g_editor_face < 0 || g_editor_face > FFACE_LIBERATION) g_editor_face = FFACE_MONO;
    if (!off && str_of(b, "escritorio", g_desk, sizeof g_desk) && !font_set_by_name(g_desk)) g_desk[0] = 0;
}

/* ---------------------------------------------------------------- disposicion */
struct sl { int x, y, w, h, side_w, cx, cw, c1_y, c1_h, sl_x, sl_w, sl_y, tg_x, tg_y, prev_y, seg_w, reset_x, reset_y;
            int f1_y, f1_h, f1_seg, f1_demo, f2_y, f2_h, fam_y, sz_y, demo_y, f3_y, f3_h, ed_y, reset2_y; };   /* SOMA_184: pagina Fuentes */
static void lay(struct sl *L, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    L->x = (int)x; L->y = (int)y; L->w = (int)w; L->h = (int)h;
    L->side_w = w > 640 ? 200 : 150;
    L->cx = L->x + L->side_w + 24; L->cw = L->x + L->w - 24 - L->cx;
    int cy = L->y + 84;
    L->c1_y = cy; L->c1_h = 190;
    L->sl_x = L->cx + 20; L->sl_w = L->cw - 40 - 70; L->sl_y = cy + 62;
    L->tg_x = L->cx + L->cw - 20 - 44; L->tg_y = cy + 100;
    L->prev_y = cy + 136;
    L->seg_w = L->cw - 40;
    L->reset_x = L->cx + L->cw - 130; L->reset_y = L->c1_y + L->c1_h + 16;
    L->f1_y = cy; L->f1_h = 128; L->f1_seg = cy + 56; L->f1_demo = cy + 94;                  /* escritorio */
    L->f2_y = L->f1_y + L->f1_h + 12; L->f2_h = 190;                                         /* interfaz y documentos */
    L->fam_y = L->f2_y + 56; L->sz_y = L->f2_y + 96; L->demo_y = L->f2_y + 140;
    L->f3_y = L->f2_y + L->f2_h + 12; L->f3_h = 100; L->ed_y = L->f3_y + 56;                  /* editor de texto */
    L->reset2_y = L->f3_y + L->f3_h + 14;
}
static void card(int x, int y, int w, int h) { fb_fill_rrect_aa(x, y, w, h, 12, 0xFF000000u | KC_CARD); fb_stroke_rrect_aa(x, y, w, h, 12, 0xFF1E293Bu); }
static void seg(int x, int y, int w, int n, int sel, const char *const *lab)
{
    fb_fill_rounded_rect(x, y, w, 32, 10, 0x000C1119);
    int bw = (w - 4) / n;
    for (int i = 0; i < n; i++) {
        if (i == sel) fb_fill_rounded_rect(x + 2 + i * bw, y + 2, bw, 28, 8, 0x00283449);
        tx(x + 2 + i * bw + (bw - tw(lab[i], 12, i == sel)) / 2, y + 2, 28, lab[i], 12, i == sel ? 0x00FFFFFF : KC_DIM, i == sel);
    }
}

/* ================================================================ SOMA_175C: pagina Redes (cable, movil por USB y WiFi) */
static int g_page;                                    /* 0 Apariencia, 1 Redes */
__attribute__((unused)) static char g_sel[33], g_pw[65];
__attribute__((unused)) static int g_pw_len, g_pw_focus, g_pw_show;
__attribute__((unused)) static char g_nmsg[120];
struct nrow { char ssid[33]; int rssi, sec, saved; };
#define NROWS_MAX 12
__attribute__((unused)) static struct nrow g_rows[NROWS_MAX]; static int g_nrows;
__attribute__((unused)) static int g_list_y, g_row_h = 30, g_btn_y, g_scan_x, g_disc_x, g_tg_x, g_tg_y, g_pw_y, g_conn_x, g_forget_x, g_eye_x, g_pw_x, g_pw_w, g_panel;
__attribute__((unused)) static void s_cpy(char *d, const char *s, int max) { int i = 0; while (s && s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int s_eq(const char *a, const char *b) { int i = 0; while (a[i] && a[i] == b[i]) i++; return a[i] == b[i]; }
__attribute__((unused)) static void ip_str(char *b, int max, const uint8_t *ip) { b[0] = 0; for (int i = 0; i < 4; i++) { s_num(b, max, ip[i]); if (i < 3) s_cat(b, ".", max); } }
__attribute__((unused)) static void dot(int x, int y, uint32_t c) { fb_fill_rrect_aa(x, y, 10, 10, 5, 0xFF000000u | c); }
__attribute__((unused)) static void bars(int x, int y, int rssi, uint32_t on)
{
    int lv = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -128 ? 1 : 0;
    for (int i = 0; i < 4; i++) { int bh = 4 + i * 3; fb_fill_rect((uint32_t)(x + i * 5), (uint32_t)(y + 14 - bh), 3, (uint32_t)bh, i < lv ? on : 0x00334155); }
}
__attribute__((unused)) static void button(int x, int y, int w, const char *t, int primary)
{
    fb_fill_rounded_rect(x, y, w, 30, 15, primary ? KC_PRIMARY : 0x00182133);
    if (!primary) fb_stroke_rrect_aa(x, y, w, 30, 15, 0xFF263142u);
    tx(x + (w - tw(t, 12, 1)) / 2, y, 30, t, 12, 0x00FFFFFF, 1);
}
/* ================================================================ SOMA_195: pagina IA (con que modelo de lenguaje habla SOMA) */
#define PAGE_IA 3
static struct llm_conf g_ia;                          /* copia de trabajo; se guarda con "Guardar y probar" */
static char g_ia_f[4][LLM_KEY_MAX];                   /* lo que se esta escribiendo: servidor, modelo, clave y (SOMA_196) clave del lanzador */
static const int IA_FMAX[4] = { LLM_BASE_MAX - 1, LLM_MODEL_MAX - 1, LLM_KEY_MAX - 1, LLM_KEY_MAX - 1 };
static const char *const IA_FIELD[4] = { "servidor", "modelo", "clave", "clave-lanzador" };
static int g_ia_listmode;                             /* SOMA_196: 0 = modelos del servidor en marcha; 1 = modelos que el lanzador puede arrancar */
static char g_ia_pick[LLM_MODEL_MAX];
static int ia_nf(void) { return g_ia.prov == LLM_LOCAL ? 4 : 3; }      /* el lanzador solo existe para el servidor local */
static int g_ia_loaded, g_ia_focus = -1, g_ia_show, g_ia_first;
static char g_ia_msg[220]; static int g_ia_msg_ok;
static volatile int g_ia_busy, g_ia_job, g_ia_ver; static int g_ia_seen;
static char g_ia_models[4096]; static int g_ia_nmodels;
static void ia_fields_from(void) { for (int k = 0; k < 4; k++) { const char *s = k == 0 ? g_ia.base[g_ia.prov] : k == 1 ? g_ia.model[g_ia.prov] : k == 2 ? g_ia.key[g_ia.prov] : g_ia.launcher_key; g_ia_f[k][0] = 0; s_cat(g_ia_f[k], s, LLM_KEY_MAX); } }
static void ia_load(void)
{
    if (g_ia_loaded) return;
    g_ia = *llm_conf_get(); ia_fields_from(); g_ia_loaded = 1;
    g_ia_msg[0] = 0; s_cat(g_ia_msg, "Elige proveedor, servidor y modelo y pulsa Guardar y probar.", sizeof g_ia_msg); g_ia_msg_ok = -1;
}
static int ia_fields_to(int report)                    /* 1 = todo valido */
{
    static const char *const bad[4] = { "El servidor no es valido (ejemplos: 192.168.1.200:8087, https://api.openai.com)", "El nombre del modelo no es valido", "La clave no es valida", "La clave del lanzador no es valida" };
    for (int k = 0; k < 4; k++) if (llm_conf_set(&g_ia, g_ia.prov, IA_FIELD[k], g_ia_f[k]) != 0) { if (report) { g_ia_msg[0] = 0; s_cat(g_ia_msg, bad[k], sizeof g_ia_msg); g_ia_msg_ok = 0; } return 0; }
    return 1;
}
static void ia_progress(uint32_t secs)                 /* SOMA_196: el modelo esta cargando en el servidor */
{
    g_ia_msg[0] = 0; s_cat(g_ia_msg, "Arrancando ", sizeof g_ia_msg); s_cat(g_ia_msg, g_ia_pick, sizeof g_ia_msg); s_cat(g_ia_msg, ": cargando el modelo en el servidor... ", sizeof g_ia_msg);
    s_num(g_ia_msg, sizeof g_ia_msg, (int)secs); s_cat(g_ia_msg, " s", sizeof g_ia_msg); g_ia_msg_ok = -1; g_ia_ver++;
}
static void ia_worker(void *arg)                       /* la red va en su hilo: el escritorio no se queda parado esperando al modelo */
{
    (void)arg;
    static char reply[400];
    if (g_ia_job == 3) {                               /* SOMA_196: que modelos puede arrancar el lanzador */
        int n = llm_launcher_models(g_ia_models, sizeof g_ia_models);
        g_ia_nmodels = n > 0 ? n : 0; g_ia_first = 0; g_ia_listmode = 1; g_ia_msg[0] = 0;
        if (n < 0) { s_cat(g_ia_msg, llm_last_error, sizeof g_ia_msg); g_ia_msg_ok = 0; }
        else if (!n) { s_cat(g_ia_msg, "El lanzador no encuentra modelos .gguf en el servidor.", sizeof g_ia_msg); g_ia_msg_ok = 0; }
        else { s_cat(g_ia_msg, "Pulsa el modelo que quieres arrancar en el servidor.", sizeof g_ia_msg); g_ia_msg_ok = -1; }
        g_ia_busy = 0; g_ia_ver++;
        thread_exit();
    }
    if (g_ia_job == 4) {                               /* SOMA_196: arrancar, esperar a que cargue y probar */
        if (!llm_launcher_start(g_ia_pick, 240000, ia_progress)) {
            g_ia_msg[0] = 0; s_cat(g_ia_msg, "No arranco. ", sizeof g_ia_msg); s_cat(g_ia_msg, llm_last_error, sizeof g_ia_msg); g_ia_msg_ok = 0;
            g_ia_busy = 0; g_ia_ver++;
            thread_exit();
        }
        g_ia = *llm_conf_get(); ia_fields_from(); g_ia_nmodels = 0; g_ia_listmode = 0;
        g_ia_job = 1;                                  /* y ahora la prueba de siempre */
    }
    if (g_ia_job == 1) {
        uint64_t t0 = timer_get_uptime_ms();
        int ok = llm_chat("Responde unicamente con estas tres palabras: SOMA TEST OK", reply, sizeof reply, 60000);
        g_ia_msg[0] = 0;
        if (ok) { s_cat(g_ia_msg, "Funciona (", sizeof g_ia_msg); s_num(g_ia_msg, sizeof g_ia_msg, (int)(timer_get_uptime_ms() - t0)); s_cat(g_ia_msg, " ms). El modelo responde: ", sizeof g_ia_msg); s_cat(g_ia_msg, reply, sizeof g_ia_msg); }
        else { s_cat(g_ia_msg, "No funciona. ", sizeof g_ia_msg); s_cat(g_ia_msg, llm_last_error, sizeof g_ia_msg); if (g_ia.prov == LLM_LOCAL) s_cat(g_ia_msg, ". Si esta apagado: Arrancar servidor.", sizeof g_ia_msg); }
        for (int i = 0; g_ia_msg[i]; i++) if (g_ia_msg[i] == '\n' || g_ia_msg[i] == '\r') g_ia_msg[i] = ' ';
        g_ia_msg_ok = ok;
    } else {
        int n = llm_list_models(g_ia_models, sizeof g_ia_models);
        g_ia_nmodels = n > 0 ? n : 0; g_ia_first = 0; g_ia_msg[0] = 0;
        if (n < 0 && g_ia.prov == LLM_LOCAL) { s_cat(g_ia_msg, "El servidor no responde. Si esta apagado, pulsa Arrancar servidor.", sizeof g_ia_msg); g_ia_msg_ok = 0; }
        else if (n < 0) { s_cat(g_ia_msg, "No se pudo pedir la lista. ", sizeof g_ia_msg); s_cat(g_ia_msg, llm_last_error, sizeof g_ia_msg); g_ia_msg_ok = 0; }
        else if (!n) { s_cat(g_ia_msg, "El servidor no devolvio ningun modelo: escribe el nombre a mano.", sizeof g_ia_msg); g_ia_msg_ok = 0; }
        else { s_num(g_ia_msg, sizeof g_ia_msg, n); s_cat(g_ia_msg, " modelos. Pulsa uno para elegirlo y despues Guardar y probar.", sizeof g_ia_msg); g_ia_msg_ok = -1; }
    }
    g_ia_busy = 0; g_ia_ver++;
    thread_exit();
}
static void ia_start(int job)
{
    if (g_ia_busy) return;
    if (!ia_fields_to(1)) return;
    g_ia_msg[0] = 0; g_ia_msg_ok = -1;
    if (llm_conf_save(&g_ia) != 0) s_cat(g_ia_msg, "(no se pudo guardar /etc/llm.conf: vale hasta reiniciar) ", sizeof g_ia_msg);
    ia_fields_from();
    s_cat(g_ia_msg, job == 1 ? "Guardado. Probando el modelo..." : job == 2 ? "Guardado. Pidiendo la lista de modelos..." : job == 3 ? "Preguntando al lanzador del servidor..." : "Pidiendo al servidor que arranque el modelo...", sizeof g_ia_msg);
    g_ia_job = job; g_ia_busy = 1;
    if (!thread_create("ajustes_ia", ia_worker, 0)) { g_ia_busy = 0; g_ia_msg[0] = 0; s_cat(g_ia_msg, "No se pudo crear el hilo de la prueba", sizeof g_ia_msg); g_ia_msg_ok = 0; }
}
static int ia_tick(void) { if (g_ia_seen != g_ia_ver) { g_ia_seen = g_ia_ver; return 1; } return 0; }

static uint32_t g_sig; static uint64_t g_last_tick;
int settings_tick(void)
{
    if (g_page == PAGE_IA) return ia_tick();
    if (g_page != 1) return 0;
    static uint64_t last_check;                                         /* SOMA_175C2: como mucho 2 veces por segundo (antes en cada vuelta */
    uint64_t t = timer_get_uptime_ms();                                  /* del bucle de la GUI, ~1000 veces/s, y consultaba el enlace de cada */
    if (t - last_check < 500) return 0;                                  /* interfaz: el escritorio se quedaba sin tiempo) */
    last_check = t;
    uint32_t sig = 0;
#if defined(__x86_64__)
    struct iwl_wifi_status w; iwl_wifi_status(&w);
    sig = (uint32_t)w.state * 7u + (uint32_t)w.scanning * 131u + (uint32_t)w.connecting * 1031u + (uint32_t)w.nbss * 17u + (uint32_t)w.ready * 3u + (uint32_t)w.enabled * 5u;
#endif
    for (int i = 0; i < netif_count(); i++) sig = sig * 31u + (uint32_t)netif_is_up(i) + (netif_get(i) == netif_active() ? 7u : 0u);
    sig = sig * 31u + net_ip[3] + net_ip[2] * 7u;
    uint64_t now = timer_get_uptime_ms();
    if (sig != g_sig || now - g_last_tick > 5000) { g_sig = sig; g_last_tick = now; return 1; }
    return 0;
}

static void render_redes(const struct sl *L)
{
    char b[140];
    tx(L->cx, L->y + 18, 30, "Redes", 20, KC_TXT, 1);
    tx(L->cx, L->y + 46, 22, "Cable, movil por USB y WiFi. Se usa la primera con enlace (cable > movil > WiFi).", 12, KC_DIM, 0);
    /* tarjeta 1: conexiones */
    int y1 = L->y + 84, h1 = 128;
    card(L->cx, y1, L->cw, h1);
    tx(L->cx + 20, y1 + 10, 22, "Conexiones", 14, KC_TXT, 1);
    const struct netif_ops *act = netif_active();
    static const char *const names[3] = { "eth0", "usb0", "wlan0" };
    static const char *const label[3] = { "Cable", "Movil por USB", "WiFi" };
    for (int k = 0; k < 3; k++) {
        int ry = y1 + 36 + k * 22, link = 0, found = 0, up = 0;
        const struct netif_ops *o = 0;
        for (int i = 0; i < netif_count(); i++) { const struct netif_ops *q = netif_get(i); if (q && s_eq(q->name, names[k])) { o = q; found = 1; up = netif_is_up(i); link = up && q->link ? q->link() : 0; } }
        int active = o && o == act && link;
        dot(L->cx + 22, ry + 6, active ? 0x0022C55E : link ? 0x0038BDF8 : 0x00475569);
        tx(L->cx + 40, ry, 22, label[k], 12, KC_TXT, active);
        b[0] = 0;
        if (!found) s_cat(b, "no disponible", sizeof b);
        else if (k == 0 && !up) s_cat(b, "no detectado", sizeof b);
        else if (active) s_cat(b, "en uso", sizeof b);
        else if (link) s_cat(b, "con enlace (en reserva)", sizeof b);
        else s_cat(b, k == 0 ? "sin cable" : k == 1 ? "sin movil (activa compartir conexion por USB)" : "sin conectar", sizeof b);
        tx(L->cx + 170, ry, 22, b, 12, active ? 0x0022C55E : KC_DIM, 0);
    }
    b[0] = 0; s_cat(b, "IP ", sizeof b); { char ip[20]; ip_str(ip, sizeof ip, net_ip); s_cat(b, ip, sizeof b); }
#if defined(__x86_64__)
    s_cat(b, "   -   ", sizeof b); s_cat(b, netmgr_state(), sizeof b);
#endif
    fb_set_clip(L->cx + 20, y1 + h1 - 30, L->cw - 40, 24);
    tx(L->cx + 20, y1 + h1 - 30, 22, b, 11, KC_FAINT, 0);
    fb_reset_clip();
    /* tarjeta 2: WiFi */
    int y2 = y1 + h1 + 14, h2 = L->y + L->h - 16 - y2;
    if (h2 < 140) h2 = 140;
    card(L->cx, y2, L->cw, h2);
    tx(L->cx + 20, y2 + 10, 22, "WiFi", 14, KC_TXT, 1);
#if !defined(__x86_64__)
    tx(L->cx + 20, y2 + 40, 22, "En esta plataforma la WiFi se maneja con la orden wifi del terminal.", 12, KC_DIM, 0);
    g_nrows = 0; g_panel = 0;
#else
    struct iwl_wifi_status w; iwl_wifi_status(&w);
    g_tg_x = L->cx + L->cw - 20 - 44; g_tg_y = y2 + 12;
    fb_fill_rrect_aa(g_tg_x, g_tg_y, 44, 24, 12, w.enabled ? (0xFF000000u | KC_PRIMARY) : 0xFF334155u);
    fb_fill_rrect_aa(g_tg_x + (w.enabled ? 22 : 2), g_tg_y + 2, 20, 20, 10, 0xFFFFFFFFu);
    b[0] = 0;
    if (!w.present) { s_cat(b, "Sin tarjeta WiFi: ", sizeof b); s_cat(b, w.why, sizeof b); }
    else if (!w.enabled) s_cat(b, "Desactivada", sizeof b);
    else if (w.state == IWL_W_UP) { s_cat(b, "Conectada a ", sizeof b); s_cat(b, w.ssid, sizeof b); s_cat(b, "  (canal ", sizeof b); s_num(b, sizeof b, w.chan); s_cat(b, ", ", sizeof b); s_num(b, sizeof b, w.rate_mbps); s_cat(b, " Mbit/s)", sizeof b); }
    else if (w.connecting) { s_cat(b, "Conectando a ", sizeof b); s_cat(b, w.ssid, sizeof b); s_cat(b, ": ", sizeof b); s_cat(b, w.state_name, sizeof b); s_cat(b, "...", sizeof b); }
    else if (w.scanning) s_cat(b, "Buscando redes (unos segundos)...", sizeof b);
    else if (w.state == IWL_W_FAIL && w.why[0]) { s_cat(b, "No se pudo conectar: ", sizeof b); s_cat(b, w.why, sizeof b); }
    else if (!w.ready) { s_cat(b, "Preparando la tarjeta... ", sizeof b); s_cat(b, w.why, sizeof b); }
    else s_cat(b, "Lista. Elige una red de la lista.", sizeof b);
    fb_set_clip(L->cx + 20, y2 + 34, L->cw - 40, 22);
    tx(L->cx + 20, y2 + 34, 22, b, 12, w.state == IWL_W_UP ? 0x0022C55E : w.state == IWL_W_FAIL ? 0x00F87171 : KC_DIM, w.state == IWL_W_UP);
    fb_reset_clip();
    g_btn_y = y2 + 62; g_scan_x = L->cx + 20; g_disc_x = -1;
    button(g_scan_x, g_btn_y, 130, w.scanning ? "Buscando..." : "Buscar redes", 0);
    if (w.state == IWL_W_UP) { g_disc_x = g_scan_x + 142; button(g_disc_x, g_btn_y, 120, "Desconectar", 0); }
    if (g_nmsg[0]) { fb_set_clip(L->cx + 300, g_btn_y, L->cw - 320, 30); tx(L->cx + 300, g_btn_y, 30, g_nmsg, 11, KC_FAINT, 0); fb_reset_clip(); }
    /* lista: una fila por nombre de red (la de mejor senal), ordenada por senal */
    int n = 0; const struct iwl_bss *bl = iwl_bss_list(&n);
    g_nrows = 0;
    for (int i = 0; i < n; i++) {
        if (!bl[i].ssid[0]) continue;
        int k = 0; while (k < g_nrows && !s_eq(g_rows[k].ssid, bl[i].ssid)) k++;
        if (k < g_nrows) { if (bl[i].rssi > g_rows[k].rssi) g_rows[k].rssi = bl[i].rssi; continue; }
        if (g_nrows >= NROWS_MAX) {                      /* sustituye a la peor si esta es mejor */
            int worst = 0; for (int j = 1; j < g_nrows; j++) if (g_rows[j].rssi < g_rows[worst].rssi) worst = j;
            if (bl[i].rssi <= g_rows[worst].rssi) continue;
            k = worst;
        } else k = g_nrows++;
        s_cpy(g_rows[k].ssid, bl[i].ssid, 33); g_rows[k].rssi = bl[i].rssi; g_rows[k].sec = bl[i].sec; g_rows[k].saved = iwl_wifi_is_saved(bl[i].ssid);
    }
    for (int i = 1; i < g_nrows; i++) { struct nrow v = g_rows[i]; int j = i; while (j > 0 && g_rows[j - 1].rssi < v.rssi) { g_rows[j] = g_rows[j - 1]; j--; } g_rows[j] = v; }
    g_list_y = g_btn_y + 42;
    int panel_h = 46, avail = y2 + h2 - 12 - panel_h - g_list_y, maxr = avail / g_row_h;
    if (maxr < 1) maxr = 1;
    if (g_nrows > maxr) g_nrows = maxr;
    if (!g_nrows) tx(L->cx + 20, g_list_y, 24, w.ready ? "Sin redes en la lista: pulsa Buscar redes." : "", 12, KC_FAINT, 0);
    for (int i = 0; i < g_nrows; i++) {
        int ry = g_list_y + i * g_row_h, sel = s_eq(g_rows[i].ssid, g_sel), conn = w.state == IWL_W_UP && s_eq(g_rows[i].ssid, w.ssid);
        if (sel) fb_fill_rounded_rect(L->cx + 12, ry, L->cw - 24, g_row_h - 2, 8, KC_SELBG);
        bars(L->cx + 22, ry + 6, g_rows[i].rssi, conn ? 0x0022C55E : KC_ACC);
        fb_set_clip(L->cx + 50, ry, L->cw - 260, g_row_h);
        tx(L->cx + 50, ry, g_row_h - 2, g_rows[i].ssid, 12, KC_TXT, conn || sel);
        fb_reset_clip();
        b[0] = 0; s_cat(b, wpa_sec_name(g_rows[i].sec), sizeof b);
        if (conn) s_cat(b, "  - conectada", sizeof b); else if (g_rows[i].saved) s_cat(b, "  - guardada", sizeof b);
        tx(L->cx + L->cw - 200, ry, g_row_h - 2, b, 11, conn ? 0x0022C55E : KC_DIM, 0);
    }
    /* panel de la red elegida */
    g_panel = 0; g_pw_y = y2 + h2 - 12 - panel_h + 8;
    int si = -1; for (int i = 0; i < g_nrows; i++) if (s_eq(g_rows[i].ssid, g_sel)) si = i;
    if (si >= 0 && !(w.state == IWL_W_UP && s_eq(g_sel, w.ssid))) {
        g_panel = 1;
        int needpw = g_rows[si].sec != SEC_OPEN && !g_rows[si].saved;
        int x0 = L->cx + 20;
        g_pw_x = x0; g_pw_w = needpw ? L->cw - 40 - (g_rows[si].saved ? 250 : 140) : 0;
        if (needpw) {
            fb_fill_rounded_rect(g_pw_x, g_pw_y, g_pw_w, 30, 8, 0x00090D15);
            fb_stroke_rrect_aa(g_pw_x, g_pw_y, g_pw_w, 30, 8, g_pw_focus ? (0xFF000000u | KC_ACC) : 0xFF263142u);
            char shown[70]; int k = 0;
            if (!g_pw_len) { s_cpy(shown, "Contrasena de la red", sizeof shown); }
            else { for (int i = 0; i < g_pw_len && k < 64; i++) shown[k++] = g_pw_show ? g_pw[i] : '*'; shown[k] = 0; }
            if (g_pw_focus && g_pw_len) { shown[k] = '_'; shown[k + 1] = 0; }
            fb_set_clip(g_pw_x + 8, g_pw_y, g_pw_w - 50, 30);
            tx(g_pw_x + 12, g_pw_y, 30, shown, 12, g_pw_len ? KC_TXT : KC_FAINT, 0);
            fb_reset_clip();
            g_eye_x = g_pw_x + g_pw_w - 44;
            tx(g_eye_x, g_pw_y, 30, g_pw_show ? "ocultar" : "ver", 10, KC_ACC, 0);
        } else {
            g_eye_x = -1;
            tx(x0, g_pw_y, 30, g_rows[si].sec == SEC_OPEN ? "Red abierta (sin contrasena)" : "Red guardada: se usara la contrasena guardada", 12, KC_DIM, 0);
        }
        g_conn_x = L->cx + L->cw - 20 - 120;
        g_forget_x = g_rows[si].saved ? g_conn_x - 112 : -1;
        button(g_conn_x, g_pw_y, 120, w.connecting ? "Conectando..." : "Conectar", 1);
        if (g_forget_x >= 0) button(g_forget_x, g_pw_y, 100, "Olvidar", 0);
    }
#endif
}

static int click_redes(const struct sl *L, int32_t mx, int32_t my)
{
    (void)L;
#if defined(__x86_64__)
    struct iwl_wifi_status w; iwl_wifi_status(&w);
    if (mx >= g_tg_x && mx < g_tg_x + 44 && my >= g_tg_y && my < g_tg_y + 24) {
        iwl_wifi_enable(!w.enabled); s_cpy(g_nmsg, w.enabled ? "WiFi desactivada" : "WiFi activada", sizeof g_nmsg); return 0;
    }
    if (my >= g_btn_y && my < g_btn_y + 30) {
        if (mx >= g_scan_x && mx < g_scan_x + 130) {
            int r = iwl_wifi_request_scan();
            s_cpy(g_nmsg, r == 0 ? "" : r == -2 ? "Desconecta primero para buscar otras redes" : "La WiFi no esta lista", sizeof g_nmsg);
            return 0;
        }
        if (g_disc_x >= 0 && mx >= g_disc_x && mx < g_disc_x + 120) { iwl_wifi_request_disconnect(); s_cpy(g_nmsg, "Desconectando...", sizeof g_nmsg); return 0; }
    }
    for (int i = 0; i < g_nrows; i++) {
        int ry = g_list_y + i * g_row_h;
        if (my >= ry && my < ry + g_row_h && mx >= L->cx + 12 && mx < L->cx + L->cw - 12) {
            if (!s_eq(g_sel, g_rows[i].ssid)) { s_cpy(g_sel, g_rows[i].ssid, sizeof g_sel); g_pw_len = 0; g_pw[0] = 0; g_pw_show = 0; }
            g_pw_focus = g_rows[i].sec != SEC_OPEN && !g_rows[i].saved;
            g_nmsg[0] = 0;
            return 0;
        }
    }
    if (g_panel && my >= g_pw_y && my < g_pw_y + 30) {
        if (g_eye_x >= 0 && mx >= g_eye_x && mx < g_eye_x + 44) { g_pw_show = !g_pw_show; return 0; }
        if (g_pw_w && mx >= g_pw_x && mx < g_pw_x + g_pw_w) { g_pw_focus = 1; return 0; }
        if (mx >= g_conn_x && mx < g_conn_x + 120) {
            g_pw[g_pw_len] = 0;
            int r = iwl_wifi_request_connect(g_sel, g_pw_len ? g_pw : 0, 1);
            s_cpy(g_nmsg, r ? "Espera: ya hay una conexion en curso" : "", sizeof g_nmsg);
            if (!r) { for (int i = 0; i < 65; i++) g_pw[i] = 0; g_pw_len = 0; g_pw_focus = 0; }
            return 0;
        }
        if (g_forget_x >= 0 && mx >= g_forget_x && mx < g_forget_x + 100) { iwl_wifi_forget(g_sel); s_cpy(g_nmsg, "Red olvidada", sizeof g_nmsg); return 0; }
    }
    g_pw_focus = 0;
#else
    (void)mx; (void)my;
#endif
    return 0;
}

/* SOMA_195: pagina IA */
static int seg_index(const struct sl *L, int32_t mx, int n);
struct ial { int c1_y, c1_h, seg_y, f_y[4], nf, b3_x, b3_w, f_x, f_w, eye_x, btn_y, b1_x, b1_w, b2_x, b2_w, msg_y, c2_y, c2_h, list_y, rows, pg_x; };
static void ia_lay(const struct sl *L, struct ial *A)
{
    A->c1_y = L->y + 84; A->seg_y = A->c1_y + 40;
    A->f_x = L->cx + 110; A->f_w = L->cw - 130;
    A->nf = ia_nf();
    for (int k = 0; k < 4; k++) A->f_y[k] = A->seg_y + 46 + k * 36;
    A->eye_x = A->f_x + A->f_w - 44;
    A->btn_y = A->f_y[A->nf - 1] + 42; A->b1_x = L->cx + 20; A->b1_w = 150; A->b2_x = A->b1_x + A->b1_w + 10; A->b2_w = 120;
    A->b3_x = A->b2_x + A->b2_w + 10; A->b3_w = 150;
    A->msg_y = A->btn_y + 36; A->c1_h = A->msg_y + 44 - A->c1_y;
    A->c2_y = A->c1_y + A->c1_h + 12; A->c2_h = L->y + L->h - 16 - A->c2_y;
    A->list_y = A->c2_y + 36; A->rows = (A->c2_h - 44) / 24; if (A->rows < 0) A->rows = 0;
    A->pg_x = L->cx + L->cw - 20 - 150;
}
static const char *ia_model_at(int idx, char *out, int max)
{
    const char *m = g_ia_models; int k = 0;
    for (int i = 0; i < idx; i++) { while (*m && *m != '\n') m++; if (*m) m++; }
    while (m[k] && m[k] != '\n' && k < max - 1) { out[k] = m[k]; k++; }
    out[k] = 0; return out;
}
static void ia_field(int x, int y, int w, const char *s, int focus, const char *hint)
{
    fb_fill_rounded_rect(x, y, w, 30, 8, 0x000C1119);
    fb_stroke_rrect_aa(x, y, w, 30, 8, focus ? (0xFF000000u | KC_ACC) : 0xFF263142u);
    int avail = w - 24, n = s_len(s), st = 0;
    while (st < n && tw(s + st, 12, 0) > avail) st++;                       /* si no cabe se ve el final (donde se escribe) */
    fb_set_clip(x + 10, y, w - 20, 30);
    if (n) tx(x + 10, y, 30, s + st, 12, KC_TXT, 0); else if (!focus) tx(x + 10, y, 30, hint, 12, KC_FAINT, 0);
    if (focus) fb_fill_rect((uint32_t)(x + 11 + (n ? tw(s + st, 12, 0) : 0)), (uint32_t)(y + 7), 1, 16, KC_ACC);
    fb_reset_clip();
}
static void render_ia(const struct sl *L)
{
    struct ial A; ia_load(); ia_lay(L, &A);
    static const char *const prov[4] = { "Local / red", "ChatGPT", "Claude", "Gemini" };
    static const char *const lab[4] = { "Servidor", "Modelo", "Clave", "Lanzador" };
    static const char *const hint[4] = { "ip:puerto o https://servidor", "pulsa Ver modelos o escribe el nombre", "clave de la API", "clave que muestra soma_lanzador.py en el servidor" };
    tx(L->cx, L->y + 18, 30, "Inteligencia artificial", 20, KC_TXT, 1);
    tx(L->cx, L->y + 46, 22, "El modelo de lenguaje del orbe y los agentes: local o por API.", 12, KC_DIM, 0);
    card(L->cx, A.c1_y, L->cw, A.c1_h);
    tx(L->cx + 20, A.c1_y + 10, 22, "Proveedor", 14, KC_TXT, 1);
    seg(L->cx + 20, A.seg_y, L->seg_w, 4, g_ia.prov, prov);
    for (int k = 0; k < A.nf; k++) {
        tx(L->cx + 20, A.f_y[k], 30, lab[k], 13, KC_DIM, 0);
        if (k >= 2) {
            static char m[LLM_KEY_MAX]; int n = s_len(g_ia_f[k]);
            for (int i = 0; i < n; i++) m[i] = (g_ia_show || (g_ia_focus != k && i >= n - 4 && n > 8)) ? g_ia_f[k][i] : '*';
            m[n] = 0;
            ia_field(A.f_x, A.f_y[k], A.f_w - 52, m, g_ia_focus == k, k == 2 && g_ia.prov == LLM_LOCAL ? "no hace falta (solo si tu servidor la pide)" : hint[k]);
            if (k == 2) tx(A.eye_x + 4, A.f_y[k], 30, g_ia_show ? "ocultar" : "ver", 11, KC_ACC, 0);
        } else ia_field(A.f_x, A.f_y[k], A.f_w, g_ia_f[k], g_ia_focus == k, hint[k]);
    }
    button(A.b1_x, A.btn_y, A.b1_w, g_ia_busy && g_ia_job == 1 ? "Probando..." : "Guardar y probar", 1);
    button(A.b2_x, A.btn_y, A.b2_w, g_ia_busy && g_ia_job == 2 ? "Pidiendo..." : "Ver modelos", 0);
    if (A.nf == 4) button(A.b3_x, A.btn_y, A.b3_w, g_ia_busy && g_ia_job >= 3 ? "Arrancando..." : "Arrancar servidor", 0);   /* SOMA_196 */
    fb_set_clip(L->cx + 20, A.msg_y, L->cw - 40, 40);
    {   /* el mensaje en dos renglones como mucho */
        uint32_t col = g_ia_msg_ok == 1 ? 0x004ADE80 : g_ia_msg_ok == 0 ? 0x00F87171 : KC_DIM;
        static char l1[220]; uint32_t n = 0, cut, best = 0;
        while (n < sizeof l1 - 1 && g_ia_msg[n]) { l1[n] = g_ia_msg[n]; n++; }
        l1[n] = 0; cut = n;
        if (tw(l1, 12, 0) > L->cw - 40) {
            for (uint32_t i = 1; i < n; i++) { if (g_ia_msg[i] != ' ') continue; l1[i] = 0; int fits = tw(l1, 12, 0) <= L->cw - 40; l1[i] = ' '; if (!fits) break; best = i; }
            if (best) { cut = best; l1[cut] = 0; }
        }
        tx(L->cx + 20, A.msg_y, 20, l1, 12, col, 0);
        if (cut < n) tx(L->cx + 20, A.msg_y + 18, 20, g_ia_msg + cut + 1, 12, col, 0);
    }
    fb_reset_clip();
    if (A.c2_h < 70) return;
    card(L->cx, A.c2_y, L->cw, A.c2_h);
    tx(L->cx + 20, A.c2_y + 8, 22, g_ia_listmode && g_ia_nmodels ? "Modelos que el servidor puede arrancar" : "Modelos del servidor", 14, KC_TXT, 1);
    if (!g_ia_nmodels) { tx(L->cx + 20, A.list_y, 24, g_ia.prov == LLM_LOCAL ? "Pulsa Ver modelos para preguntar al servidor cuales tiene." : "Pon la clave y pulsa Ver modelos para ver los de tu cuenta.", 12, KC_FAINT, 0); return; }
    if (g_ia_nmodels > A.rows) {
        char b[40]; b[0] = 0; s_num(b, sizeof b, g_ia_first + 1); s_cat(b, "-", sizeof b); s_num(b, sizeof b, g_ia_first + A.rows < g_ia_nmodels ? g_ia_first + A.rows : g_ia_nmodels); s_cat(b, " de ", sizeof b); s_num(b, sizeof b, g_ia_nmodels);
        tx(A.pg_x - tw(b, 11, 0) - 10, A.c2_y + 8, 22, b, 11, KC_FAINT, 0);
        button(A.pg_x, A.c2_y + 4, 70, "Anterior", 0); button(A.pg_x + 80, A.c2_y + 4, 70, "Siguiente", 0);
    }
    for (int r = 0; r < A.rows && g_ia_first + r < g_ia_nmodels; r++) {
        char one[LLM_MODEL_MAX]; ia_model_at(g_ia_first + r, one, sizeof one);
        int sel = s_eq(one, g_ia_listmode ? g_ia_pick : g_ia_f[1]), ry = A.list_y + r * 24;
        if (sel) fb_fill_rounded_rect(L->cx + 12, ry, L->cw - 24, 22, 6, KC_SELBG);
        fb_set_clip(L->cx + 20, ry, L->cw - 40, 22);
        tx(L->cx + 20, ry, 22, one, 12, sel ? 0x00FFFFFF : KC_TXT, sel);
        fb_reset_clip();
    }
}
static int click_ia(const struct sl *L, int32_t mx, int32_t my)
{
    struct ial A; ia_load(); ia_lay(L, &A);
    int was = g_ia_focus; g_ia_focus = -1;
    if (my >= A.seg_y && my < A.seg_y + 32 && mx >= L->cx + 20 && mx < L->cx + 20 + L->seg_w) {
        if (g_ia_busy) return 1;
        int p = seg_index(L, mx, 4);
        if (p != g_ia.prov) { (void)ia_fields_to(0); g_ia.prov = p; ia_fields_from(); g_ia_nmodels = 0; g_ia_listmode = 0; g_ia_show = 0;
            g_ia_msg[0] = 0; s_cat(g_ia_msg, p == LLM_LOCAL ? "Servidor de tu equipo o de tu red local (llama.cpp, Ollama, LM Studio...). Pulsa Guardar y probar." : "Hace falta la clave de la API de ese proveedor. Lo que preguntes viajara a sus servidores.", sizeof g_ia_msg); g_ia_msg_ok = -1; }
        return 1;
    }
    for (int k = 0; k < A.nf; k++) if (my >= A.f_y[k] && my < A.f_y[k] + 30 && mx >= A.f_x && mx < A.f_x + A.f_w) {
        if (k == 2 && mx >= A.eye_x) { g_ia_show = !g_ia_show; g_ia_focus = was; return 1; }
        g_ia_focus = k; return 1;
    }
    if (my >= A.btn_y && my < A.btn_y + 30) {
        if (mx >= A.b1_x && mx < A.b1_x + A.b1_w) { ia_start(1); return 1; }
        if (mx >= A.b2_x && mx < A.b2_x + A.b2_w) { g_ia_listmode = 0; ia_start(2); return 1; }
        if (A.nf == 4 && mx >= A.b3_x && mx < A.b3_x + A.b3_w) { ia_start(3); return 1; }     /* SOMA_196 */
    }
    if (g_ia_nmodels > A.rows && my >= A.c2_y + 4 && my < A.c2_y + 34) {
        if (mx >= A.pg_x && mx < A.pg_x + 70) { g_ia_first -= A.rows; if (g_ia_first < 0) g_ia_first = 0; return 1; }
        if (mx >= A.pg_x + 80 && mx < A.pg_x + 150) { if (g_ia_first + A.rows < g_ia_nmodels) g_ia_first += A.rows; return 1; }
    }
    if (g_ia_nmodels && A.rows > 0 && my >= A.list_y && mx >= L->cx + 12 && mx < L->cx + L->cw - 12) {
        int r = (my - A.list_y) / 24;
        if (r < A.rows && g_ia_first + r < g_ia_nmodels && g_ia_listmode) {       /* SOMA_196: arrancar ese modelo en el servidor */
            if (g_ia_busy) return 1;
            ia_model_at(g_ia_first + r, g_ia_pick, LLM_MODEL_MAX);
            ia_start(4);
            return 1;
        }
        if (r < A.rows && g_ia_first + r < g_ia_nmodels) {
            ia_model_at(g_ia_first + r, g_ia_f[1], LLM_MODEL_MAX);
            g_ia_msg[0] = 0; s_cat(g_ia_msg, "Modelo elegido: ", sizeof g_ia_msg); s_cat(g_ia_msg, g_ia_f[1], sizeof g_ia_msg); s_cat(g_ia_msg, ". Pulsa Guardar y probar.", sizeof g_ia_msg); g_ia_msg_ok = -1;
            return 1;
        }
    }
    return was >= 0;
}
static int key_ia(char c)
{
    if (c == '\t') { g_ia_focus = (g_ia_focus + 1) % ia_nf(); return 1; }
    if (g_ia_focus < 0) return 0;
    char *f = g_ia_f[g_ia_focus]; int n = s_len(f);
    if (c == 8 || c == 127) { if (n) f[n - 1] = 0; return 1; }
    if (c == '\n' || c == '\r') { ia_start(1); return 1; }
    if ((unsigned char)c > 32 && (unsigned char)c < 127 && n < IA_FMAX[g_ia_focus]) { f[n] = c; f[n + 1] = 0; return 1; }
    return 0;
}

static void render_sidebar(const struct sl *L)
{
    tx(L->x + 18, L->y + 14, 30, "Ajustes", 18, KC_TXT, 1);
    static const char *const items[6] = { "Apariencia", "Redes", "Fuentes", "IA", "Pantalla", "Sistema" };   /* SOMA_195: IA */
    for (int i = 0; i < 6; i++) {
        int iy = L->y + 60 + i * 40, on = i == g_page;
        if (on) { fb_fill_rounded_rect(L->x + 10, iy, L->side_w - 20, 34, 9, KC_SELBG); fb_fill_rounded_rect(L->x + 10, iy + 8, 3, 18, 1, KC_ACC); }
        int ix = L->x + 30, icy = iy + 17; uint32_t ic = on ? 0xFFFFFFFFu : (i < 4 ? 0xFF8592A8u : 0xFF566379u);
        if (i == 0) { fb_stroke_rrect_aa(ix - 7, icy - 7, 14, 14, 7, ic); fb_fill_rrect_aa(ix - 3, icy - 3, 4, 4, 2, ic); }
        else if (i == 1) { for (int k = 0; k < 3; k++) fb_fill_rect((uint32_t)(ix - 6 + k * 5), (uint32_t)(icy + 4 - k * 4), 3, (uint32_t)(4 + k * 4), ic & 0xFFFFFF); }
        else if (i == 2) { tx(ix - 10, iy, 34, "Aa", 13, ic & 0xFFFFFF, 1); }                                              /* SOMA_184: Fuentes */
        else if (i == 3) { fb_stroke_rrect_aa(ix - 8, icy - 7, 16, 12, 4, ic); fb_fill_rrect_aa(ix - 4, icy - 2, 2, 2, 1, ic); fb_fill_rrect_aa(ix + 2, icy - 2, 2, 2, 1, ic); fb_fill_rect((uint32_t)(ix - 5), (uint32_t)(icy + 5), 3, 3, ic & 0xFFFFFF); }   /* IA */
        else if (i == 4) { fb_stroke_rrect_aa(ix - 8, icy - 6, 16, 11, 2, ic); fb_fill_rect((uint32_t)(ix - 3), (uint32_t)(icy + 6), 6, 2, ic & 0xFFFFFF); }
        else { fb_stroke_rrect_aa(ix - 6, icy - 6, 12, 12, 6, ic); fb_fill_rrect_aa(ix - 2, icy - 2, 4, 4, 2, ic); }
        tx(L->x + 46, iy, 34, items[i], 13, on ? 0x00FFFFFF : (i < 4 ? KC_DIM : KC_FAINT), on);
        if (i >= 4) tx(L->x + 46 + tw(items[i], 13, 0) + 8, iy, 34, "pronto", 10, 0x00475569, 0);
    }
}

/* ================================================================ SOMA_184: pagina Fuentes */
static const int UIFAM[3] = { WF_FAM_SANS, WF_FAM_LIBERATION, WF_FAM_MONO };        /* orden en pantalla -> familia de la interfaz */
static int fam_sel(int fam) { for (int i = 0; i < 3; i++) if (UIFAM[i] == fam) return i; return 0; }
static void render_reset(const struct sl *L, int ry)
{
    fb_fill_rounded_rect(L->reset_x, ry, 130, 32, 16, 0x00182133);
    fb_stroke_rrect_aa(L->reset_x, ry, 130, 32, 16, 0xFF263142u);
    tx(L->reset_x + (130 - tw("Restablecer", 13, 1)) / 2, ry, 32, "Restablecer", 13, KC_TXT, 1);
    fb_set_clip(L->cx, ry, L->reset_x - L->cx - 10, 32);
    tx(L->cx, ry, 32, g_msg, 11, KC_FAINT, 0);
    fb_reset_clip();
}
static void render_fuentes(const struct sl *L)
{
    char b[120];
    tx(L->cx, L->y + 18, 30, "Fuentes", 20, KC_TXT, 1);
    tx(L->cx, L->y + 46, 22, "Escritorio, aplicaciones, documentos y editor (las mismas del navegador).", 12, KC_DIM, 0);
    if (font_sysfont_off()) {
        card(L->cx, L->f1_y, L->cw, 70);
        tx(L->cx + 20, L->f1_y + 14, 22, "Catalogo ampliado desactivado", 14, KC_TXT, 1);
        tx(L->cx + 20, L->f1_y + 36, 18, "soma.sysfont=off (o modo seguro): se usan las fuentes de siempre.", 11, KC_DIM, 0);
        return;
    }
    /* escritorio */
    static const char *const dsk[4] = { "DejaVu Mono", "DejaVu Sans", "Liberation", "Lib. negrita" };
    card(L->cx, L->f1_y, L->cw, L->f1_h);
    tx(L->cx + 20, L->f1_y + 14, 22, "Fuente del escritorio", 14, KC_TXT, 1);
    b[0] = 0; s_cat(b, "Rotulos, iconos y ventanas. El catalogo completo (", sizeof b); s_num(b, sizeof b, font_get_count()); s_cat(b, " fuentes) esta en el comando font.", sizeof b);
    tx(L->cx + 20, L->f1_y + 34, 18, b, 11, KC_DIM, 0);
    seg(L->cx + 20, L->f1_seg, L->seg_w, 4, font_face_of(font_get_current()), dsk);
    fb_fill_rounded_rect(L->cx + 20, L->f1_demo, L->seg_w, 26, 8, 0x00090D15);
    fb_set_clip(L->cx + 20, L->f1_demo, L->seg_w, 26);
    font_draw_string((uint32_t)(L->cx + 32), (uint32_t)(L->f1_demo + 8), "El veloz murcielago hindu comia kiwi. 0123", KC_TXT, 1);
    fb_reset_clip();
    /* interfaz y documentos */
    static const char *const fam[3] = { "DejaVu Sans", "Liberation Sans", "DejaVu Sans Mono" };
    card(L->cx, L->f2_y, L->cw, L->f2_h);
    tx(L->cx + 20, L->f2_y + 14, 22, "Interfaz y documentos", 14, KC_TXT, 1);
    tx(L->cx + 20, L->f2_y + 34, 18, "Texto de las aplicaciones y del visor de documentos (no afecta a las paginas web).", 11, KC_DIM, 0);
    seg(L->cx + 20, L->fam_y, L->seg_w, 3, fam_sel(wf_ui_family()), fam);
    int si = 1; for (int i = 0; i < 4; i++) if (SCALES[i] == wf_ui_scale()) si = i;
    seg(L->cx + 20, L->sz_y, L->seg_w, 4, si, SCALE_NAME);
    fb_fill_rounded_rect(L->cx + 20, L->demo_y, L->seg_w, 40, 10, 0x00090D15);
    fb_set_clip(L->cx + 20, L->demo_y, L->seg_w, 40);
    tx(L->cx + 34, L->demo_y, 40, "El veloz murcielago hindu comia kiwi. 0123", 14, KC_TXT, 0);
    fb_reset_clip();
    /* editor de texto */
    static const char *const ed[3] = { "DejaVu Sans Mono", "DejaVu Sans", "Liberation Sans" };
    card(L->cx, L->f3_y, L->cw, L->f3_h);
    tx(L->cx + 20, L->f3_y + 14, 22, "Editor de texto (Notas)", 14, KC_TXT, 1);
    tx(L->cx + 20, L->f3_y + 34, 18, "Letra del texto que escribes; el terminal sigue usando DejaVu Sans Mono.", 11, KC_DIM, 0);
    seg(L->cx + 20, L->ed_y, L->seg_w, 3, g_editor_face, ed);
    render_reset(L, L->reset2_y);
}

void settings_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focus)
{
    (void)focus;
    settings_load();
    struct sl L; lay(&L, x, y, w, h);
    if (g_page == 1) {
        fb_fill_rect(x, y, w, h, KC_WIN);
        fb_fill_rect(x, y, (uint32_t)L.side_w, h, KC_SIDE);
        fb_fill_rect(x + (uint32_t)L.side_w - 1, y, 1, h, KC_LINE);
        render_sidebar(&L);
        render_redes(&L);
        return;
    }
    if (g_page == PAGE_IA) {                                             /* SOMA_195: IA */
        fb_fill_rect(x, y, w, h, KC_WIN);
        fb_fill_rect(x, y, (uint32_t)L.side_w, h, KC_SIDE);
        fb_fill_rect(x + (uint32_t)L.side_w - 1, y, 1, h, KC_LINE);
        render_sidebar(&L);
        render_ia(&L);
        return;
    }
    if (g_page == 2) {                                                   /* SOMA_184: Fuentes */
        fb_fill_rect(x, y, w, h, KC_WIN);
        fb_fill_rect(x, y, (uint32_t)L.side_w, h, KC_SIDE);
        fb_fill_rect(x + (uint32_t)L.side_w - 1, y, 1, h, KC_LINE);
        render_sidebar(&L);
        render_fuentes(&L);
        return;
    }
    char b[96];
    fb_fill_rect(x, y, w, h, KC_WIN);
    /* barra lateral */
    fb_fill_rect(x, y, (uint32_t)L.side_w, h, KC_SIDE);
    fb_fill_rect(x + (uint32_t)L.side_w - 1, y, 1, h, KC_LINE);
    render_sidebar(&L);
    /* cabecera */
    tx(L.cx, L.y + 18, 30, "Apariencia", 20, KC_TXT, 1);
    tx(L.cx, L.y + 46, 22, "Transparencia de las ventanas (las fuentes estan en la pagina Fuentes)", 12, KC_DIM, 0);

    /* tarjeta 1: transparencia */
    card(L.cx, L.c1_y, L.cw, L.c1_h);
    tx(L.cx + 20, L.c1_y + 14, 22, "Transparencia de las ventanas", 14, KC_TXT, 1);
    tx(L.cx + 20, L.c1_y + 34, 18, "100 % = opacas. Se ve el escritorio a traves de las ventanas.", 11, KC_DIM, 0);
    {
        int ty = L.sl_y + 10, kx = L.sl_x + (g_opacity - 40) * (L.sl_w - 1) / 60;
        fb_fill_rounded_rect(L.sl_x, ty - 3, L.sl_w, 6, 3, 0x00263142);
        fb_fill_rounded_rect(L.sl_x, ty - 3, kx - L.sl_x + 1, 6, 3, KC_PRIMARY);
        fb_fill_rrect_aa(kx - 10, ty - 10, 20, 20, 10, 0xFFFFFFFFu);
        fb_stroke_rrect_aa(kx - 10, ty - 10, 20, 20, 10, 0x40000000u);
        b[0] = 0; s_num(b, sizeof b, g_opacity); s_cat(b, " %", sizeof b);
        tx(L.sl_x + L.sl_w + 18, L.sl_y, 20, b, 15, KC_TXT, 1);
    }
    tx(L.cx + 20, L.tg_y, 24, "Mantener opaca la ventana activa", 13, KC_TXT, 0);
    {
        int on = g_active_opaque;
        fb_fill_rrect_aa(L.tg_x, L.tg_y, 44, 24, 12, on ? (0xFF000000u | KC_PRIMARY) : 0xFF334155u);
        fb_fill_rrect_aa(L.tg_x + (on ? 22 : 2), L.tg_y + 2, 20, 20, 10, 0xFFFFFFFFu);
    }
    /* vista previa: "escritorio" con una ventana translucida encima */
    {
        int px = L.cx + 20, pw = L.cw - 40, ph = 38, py = L.prev_y;
        for (int i = 0; i < pw; i += 2) { uint32_t c = 0x001E3A8Au + (uint32_t)(i * 60 / pw) * 0x00010100u; fb_fill_rect((uint32_t)(px + i), (uint32_t)py, 2, (uint32_t)ph, c); }
        for (int k = 0; k < 6; k++) fb_fill_rrect_aa(px + 12 + k * (pw / 6), py + 8, 22, 22, 11, 0xFF000000u | (0x00F59E0Bu + (uint32_t)k * 0x00101030u));
        uint32_t a = (uint32_t)(g_opacity * 255 / 100);
        fb_fill_rect_alpha(px + pw / 4, py + 4, pw / 2, ph - 8, (a << 24) | KC_WIN);
        tx(px + pw / 4 + 12, py + 4, ph - 8, "Ventana de ejemplo", 12, KC_TXT, 0);
    }

    /* pie */
    render_reset(&L, L.reset_y);
}

static int set_opacity_from(const struct sl *L, int mx)
{
    int x = mx - L->sl_x; if (x < 0) x = 0;
    if (x > L->sl_w - 1) x = L->sl_w - 1;
    int v = 40 + x * 60 / (L->sl_w - 1);
    v = (v + 2) / 5 * 5;                                   /* de 5 en 5 */
    if (v == g_opacity) return 0;
    g_opacity = v; return 1;
}
/* SOMA_184: Restablecer (Apariencia y Fuentes) */
static int click_reset(const struct sl *L, int32_t mx, int32_t my, int ry)
{
    if (mx < L->reset_x || mx >= L->reset_x + 130 || my < ry || my >= ry + 32) return 0;
    g_opacity = 100; g_active_opaque = 0; wf_ui_set_family(WF_FAM_SANS, 100);
    g_editor_face = FFACE_MONO; g_desk[0] = 0; (void)font_set(FONT_VECTOR);
    save();
    g_msg[0] = 0; s_cat(g_msg, "Valores por defecto restablecidos", sizeof g_msg);
    return 1;
}
static int seg_index(const struct sl *L, int32_t mx, int n) { int i = (mx - L->cx - 22) * n / (L->seg_w - 4); return i < 0 ? 0 : (i >= n ? n - 1 : i); }
static int click_fuentes(const struct sl *L, int32_t mx, int32_t my)
{
    if (click_reset(L, mx, my, L->reset2_y)) return 1;
    if (font_sysfont_off() || mx < L->cx + 20 || mx >= L->cx + 20 + L->seg_w) return 0;
    if (my >= L->f1_seg && my < L->f1_seg + 32) {                                   /* escritorio */
        int id = font_face_catalog_id(seg_index(L, mx, 4));
        if (!font_set(id)) return 0;
        g_desk[0] = 0; s_cat(g_desk, font_get_name(id), sizeof g_desk); save(); return 1;
    }
    if (my >= L->fam_y && my < L->fam_y + 32) { wf_ui_set_family(UIFAM[seg_index(L, mx, 3)], wf_ui_scale()); save(); return 1; }
    if (my >= L->sz_y && my < L->sz_y + 32) { wf_ui_set_family(wf_ui_family(), SCALES[seg_index(L, mx, 4)]); save(); return 1; }
    if (my >= L->ed_y && my < L->ed_y + 32) { g_editor_face = seg_index(L, mx, 3); save(); return 1; }
    return 0;
}
int settings_handle_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    struct sl L; lay(&L, x, y, w, h);
    g_drag = 0;
    if (mx >= L.x + 10 && mx < L.x + L.side_w - 10) {                   /* SOMA_175C: barra lateral */
        for (int i = 0; i < 4; i++) { int iy = L.y + 60 + i * 40; if (my >= iy && my < iy + 34) { g_page = i; g_pw_focus = 0; g_ia_focus = -1; if (i == PAGE_IA && !g_ia_busy) g_ia_loaded = 0; return i == PAGE_IA; } }
    }
    if (g_page == PAGE_IA) return click_ia(&L, mx, my);                 /* SOMA_195 */
    if (g_page == 1) return click_redes(&L, mx, my);
    if (g_page == 2) return click_fuentes(&L, mx, my);
    if (my >= L.sl_y - 6 && my < L.sl_y + 26 && mx >= L.sl_x - 12 && mx < L.sl_x + L.sl_w + 12) { g_drag = 1; int c = set_opacity_from(&L, mx); if (c) save(); return c; }
    if (mx >= L.tg_x && mx < L.tg_x + 44 && my >= L.tg_y && my < L.tg_y + 24) { g_active_opaque = !g_active_opaque; save(); return 1; }
    if (click_reset(&L, mx, my, L.reset_y)) return 1;
    return 0;
}
int settings_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int held)
{
    (void)my;
    if (!g_drag || g_page != 0) return 0;
    if (!held) { g_drag = 0; save(); return 1; }
    struct sl L; lay(&L, x, y, w, h);
    return set_opacity_from(&L, mx);
}
int settings_handle_key(char c)
{
    if (g_page == PAGE_IA) return key_ia(c);                            /* SOMA_195 */
    if (g_page == 1) {                                                  /* SOMA_175C: contrasena de la WiFi */
        if (!g_pw_focus) return 0;
        if (c == 8 || c == 127) { if (g_pw_len) g_pw[--g_pw_len] = 0; return 0; }
        if (c == '\n' || c == '\r') {
#if defined(__x86_64__)
            g_pw[g_pw_len] = 0;
            if (g_sel[0] && !iwl_wifi_request_connect(g_sel, g_pw_len ? g_pw : 0, 1)) { for (int i = 0; i < 65; i++) g_pw[i] = 0; g_pw_len = 0; g_pw_focus = 0; }
#endif
            return 0;
        }
        if ((unsigned char)c >= 32 && (unsigned char)c < 127 && g_pw_len < 63) { g_pw[g_pw_len++] = c; g_pw[g_pw_len] = 0; }
        return 0;
    }
    if (c == '+' || c == '=') { if (g_opacity < 100) { g_opacity += 5; save(); return 1; } }
    if (c == '-') { if (g_opacity > 40) { g_opacity -= 5; save(); return 1; } }
    return 0;
}
