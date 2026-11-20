/* SOMA_PARCHE_V2_GUI */
/* SOMA FIX CMD_VSOMA DUP v3 */
/* SOMA GLOBAL AGENT SHELL PATCH v2 */
#include "gui.h"
#include "fb.h"
#include "net.h"
#include "netif.h" /* SOMA_201_GADGET_ESTADO */
#include "virtio_blk.h"
#include "smp.h"
#ifdef __x86_64__
#include "acpi_sys.h"
#include "hda.h" /* SOMA_201_GADGET_ESTADO */
#endif
#include "gui_font.h"
#include "icons/icons.h"
#include "mouse.h"
#include "browser.h"
#include "imgview.h"
#include "fm.h"
#include "notepad.h"
#include "memview.h"
#include "layout.h"
#include "paint.h"
#include "codeedit.h"                               /* SOMA_219_AVISO */
#include "media.h"
#include "term_ui.h"
#include "clipboard.h"   /* SOMA_PARCHE_PORTAPAPELES */
#include "web_font.h"    /* SOMA_PARCHE_028_UI: texto proporcional suavizado en las barras de titulo */
#include "coder_app.h"
#include "console.h"
#include "idt.h"
#include "thread.h"
#include "mutex.h"                                  /* SOMA_221_RESCATE */
#include "arch.h"
#include "agent.h"
#include "shell.h"
#include "rtc.h"
#include "fs.h"
#include "mem.h"
#include "pmm.h"          /* SOMA_157: RAM real en el banner del terminal */
#ifdef __x86_64__
#include "apic.h"         /* SOMA_158: reloj real en el banner */
#endif
#include "settings.h"
#include "pdfview.h"   /* PARCHE 088 */
#include "acct.h"      /* PARCHE 089 */
#include "sysmon.h"                                /* PARCHE 085 */
#include "recorder.h"                               /* SOMA_177_T480S */
#include "camapp.h"                                /* SOMA_178_T480S */
#include "capapp.h"                                /* SOMA_179_T480S */
#include "scrcap.h"
#include "camera.h"
#include "micrec.h"
#include "sysicon.h"                                /* SOMA_218_ENERGIA */
/* SOMA_221_RESCATE: el escritorio sabe en todo momento a quien da paso (el mismo dueno que usa el Monitor para contar la CPU) y
 * cuanto tiempo de procesador lleva esa llamada; lo usan el rescate de aplicaciones colgadas o averiadas (mas abajo). */
static volatile uint8_t her_guard_owner = ACCT_DESKTOP;
static volatile uint32_t her_guard_ticks;
static inline uint8_t her_acct(uint8_t o) { her_guard_owner = o; her_guard_ticks = 0; return acct_set_owner(o); }
#define acct_set_owner(o) her_acct(o)
#if defined(__x86_64__)
extern volatile int gui_rescue_inject;
#endif

enum app_mode {
    APP_DESKTOP = 0,
    APP_AGENT_CHAT,
    APP_TERMINAL_SHELL,
    APP_WEB_BROWSER,
    APP_IMAGE_VIEWER,
    APP_FILE_MANAGER,
    APP_NOTEPAD,
    APP_AGENT_MEMORY,
    APP_LAYOUT,
        APP_AGENTS,
    APP_PAINT,
    APP_CODER_APP,
    APP_MEDIA,
    APP_SOMA_APP, /*PL12_APP_MODE*/
    APP_SETTINGS,  /* PARCHE 085: Ajustes */
    APP_PDF,       /* PARCHE 088: lector de PDF */
    APP_SYSMON,    /* PARCHE 089: monitor del sistema */
    APP_RECORDER,  /* SOMA_177_T480S: grabadora */
    APP_CAMERA,    /* SOMA_178_T480S: camara web */
    APP_CAPTURE    /* SOMA_179_T480S: captura de pantalla y audio interno */
};

static enum app_mode current_app = APP_DESKTOP;
static enum app_mode g_pending_app = APP_DESKTOP;   /* SOMA_PARCHE_023_MEDIA: app que debe abrirse al arrancar la GUI (orden de shell previa a ella) */

/* SOMA MULTI-WINDOW STACKING ARCHITECTURE */
#define HER_MAX_WINDOWS 10

struct her_win_node {
    enum app_mode app;
    uint32_t x, y, w, h;
    int dragging;
    int resizing;
    int resize_edge;
    int32_t drag_ox, drag_oy;
    int term_slot; /* SOMA_203_INSTANCIAS: 0 si no es terminal */
};

static struct her_win_node her_windows[HER_MAX_WINDOWS];
static int her_num_windows = 0;       /* Ventanas abiertas activas */
static int her_active_win_idx = -1;   /* Índice de la ventana enfocada en el tope */

/* SOMA_203_INSTANCIAS: visibles y minimizadas son entidades distintas.
 * Cada nodo tiene un identificador estable de sesion de terminal. */
#define HER_MINIMIZED_MAX 16
static struct her_win_node her_minimized[HER_MINIMIZED_MAX];
static int her_min_count;
/* SOMA_206: fase de orbita estable por icono minimizado, aviso breve y puntero global */
static uint16_t her_min_phase[HER_MINIMIZED_MAX];
/* SOMA_223_MINIMIZAR: vuelo de la ventana minimizada hasta su sitio en la orbita (ver her_mfly_*) */
static int her_mfly_on, her_mfly_app = -1, her_mfly_slot;
static uint32_t her_mfly_icon_k = 256u;          /* brillo con el que aparece el icono que llega (0..256) */
static void her_mfly_draw(uint32_t w, uint32_t h);
static const char *her_hint_msg;
static uint64_t her_hint_until;
static int32_t g_mouse_x_s, g_mouse_y_s;
static int her_min_drag = -1;
static int32_t her_min_drag_x, her_min_drag_y;
static int32_t her_min_start_x, her_min_start_y;
static void her_term_focus(int slot);
/* SOMA_211_NOTAS: elige la instancia de la aplicacion de una ventana (terminal o Bloc de notas) */
static void her_win_focus(enum app_mode app, int slot);
static int her_multi_app(enum app_mode app) { return app == APP_TERMINAL_SHELL || app == APP_NOTEPAD || app == APP_IMAGE_VIEWER || app == APP_PDF || app == APP_FILE_MANAGER || app == APP_PAINT || app == APP_CODER_APP; }
/* SOMA_212_IMAGENES: instancias por aplicacion (la terminal tiene las suyas aparte) */
static int her_inst_new(enum app_mode app)
{
    switch (app) { case APP_NOTEPAD: return notepad_inst_new(); case APP_IMAGE_VIEWER: return imgview_inst_new(); case APP_PDF: return pdfview_inst_new(); case APP_FILE_MANAGER: return fm_inst_new(); case APP_PAINT: return paint_inst_new(); case APP_CODER_APP: return coder_app_inst_new(); default: return 0; }
}
static void her_inst_drop(enum app_mode app, int slot)
{
    switch (app) { case APP_NOTEPAD: notepad_inst_drop(slot); break; case APP_IMAGE_VIEWER: imgview_inst_drop(slot); break; case APP_PDF: pdfview_inst_drop(slot); break; case APP_FILE_MANAGER: fm_inst_drop(slot); break; case APP_PAINT: paint_inst_drop(slot); break; case APP_CODER_APP: coder_app_inst_drop(slot); break; default: break; }
}
static int her_inst_find(enum app_mode app, const char *path)
{
    switch (app) { case APP_NOTEPAD: return notepad_inst_find(path); case APP_IMAGE_VIEWER: return imgview_inst_find(path); case APP_PDF: return pdfview_inst_find(path); case APP_FILE_MANAGER: return fm_inst_find(path); case APP_PAINT: return paint_inst_find(path); case APP_CODER_APP: return coder_app_inst_find(path); default: return 0; }
}
static const char *her_inst_name(enum app_mode app, int slot)
{
    switch (app) { case APP_NOTEPAD: return notepad_inst_name(slot); case APP_IMAGE_VIEWER: return imgview_inst_name(slot); case APP_PDF: return pdfview_inst_name(slot); case APP_FILE_MANAGER: return fm_inst_name(slot); case APP_PAINT: return paint_inst_name(slot); case APP_CODER_APP: return coder_app_inst_name(slot); default: return ""; }
}
static const char *her_app_base_title(enum app_mode app)
{
    switch (app) { case APP_NOTEPAD: return "Notepad"; case APP_IMAGE_VIEWER: return "Imagenes"; case APP_PDF: return "Lector PDF"; case APP_FILE_MANAGER: return "Archivos"; case APP_PAINT: return "SOMA Paint"; case APP_CODER_APP: return "SOMA Studio"; default: return "SOMA"; }
}
static int her_open_noinit;
static void her_open_app_path(enum app_mode app, const char *path, uint32_t cw, uint32_t ch);                                  /* 1 = la ventana nueva va a cargar un archivo: no cargar el de muestra */
static int her_term_alloc(void);
static void her_term_kill(int slot);
static void her_restore_minimized(int idx);
static void her_minimize_window(int idx);
static int her_find_minimized(enum app_mode app);


static struct her_win_node *her_get_active_win(void)
{
    if (her_active_win_idx >= 0 && her_active_win_idx < her_num_windows)
        return &her_windows[her_active_win_idx];
    return 0;
}

static int her_find_window(enum app_mode app)
{
    for (int i = 0; i < her_num_windows; ++i) {
        if (her_windows[i].app == app) return i;
    }
    return -1;
}

static void her_bring_to_front(int idx)
{
    if (idx < 0 || idx >= her_num_windows || idx == her_num_windows - 1) return;
    struct her_win_node tmp = her_windows[idx];
    for (int i = idx; i < her_num_windows - 1; ++i) {
        her_windows[i] = her_windows[i + 1];
    }
    her_windows[her_num_windows - 1] = tmp;
    her_active_win_idx = her_num_windows - 1;
    current_app = tmp.app;
    her_win_focus(tmp.app, tmp.term_slot);
}

#define GUI_MAX_LOG_LINES 256
#define GUI_LINE_MAX_LEN  256

struct gui_chat_line { char text[GUI_LINE_MAX_LEN]; uint32_t color; };
static struct gui_chat_line *agent_log = 0;
static int agent_log_count = 0;

static void ensure_agent_log(void)
{
    if (!agent_log) {
        agent_log = (struct gui_chat_line *)kmalloc(sizeof(struct gui_chat_line) * GUI_MAX_LOG_LINES);
        if (agent_log) {
            memset((void *)agent_log, 0, sizeof(struct gui_chat_line) * GUI_MAX_LOG_LINES);
        }
    }
}

#define SOMA_AGENT_BAR_H        248u
#define SOMA_SYSTEM_BAR_H       44u
#define SOMA_WINDOW_GAP         10u
#define SOMA_AGENT_CMD_MAX      256u
#define SOMA_AGENT_LINE_H       16u
#define SOMA_AGENT_RESP_TOP     42u
#define SOMA_AGENT_RESP_H       148u
#define SOMA_DOCK_ROW1          (SOMA_AGENT_BAR_H + 34u)
#define SOMA_DOCK_ROW2          (SOMA_AGENT_BAR_H + 112u)
#define SOMA_DOCK_ROW3          (SOMA_AGENT_BAR_H + 190u)
#define SOMA_DOCK_ROW4          (SOMA_AGENT_BAR_H + 268u)
#define SOMA_DOCK_ROW5          (SOMA_AGENT_BAR_H + 346u)
#define SOMA_DOCK_COL1          40u
#define SOMA_DOCK_COL2          230u
#define SOMA_DOCK_ICON_W        150u
#define SOMA_DOCK_ICON_H        68u

static char agent_global_cmd[SOMA_AGENT_CMD_MAX];
static uint32_t agent_global_len = 0;
static uint32_t agent_global_cursor = 0;
static int agent_global_focus = 1;
static uint32_t agent_global_scroll = 0;

/* --- visibilidad / texto fantasma --- */
static int      her_icons_visible   = 0;
static int      her_vortex_closing  = 0;
static int      her_dock_pending_open = 0;
static int      soma_text_vortex    = 0;
/* Evita que la respuesta anterior reaparezca mientras el dock orbital esta activo. */
static int      her_text_suppressed_by_dock = 0;
/* SOMA ORBITAL ICONS PATCH v2: transicion y destello, sin buffers nuevos. */
static uint32_t her_icons_anim_phase = 0; /* 0 = ocultos; 256 = orbita estable */
static uint64_t her_icons_burst_at = 0;
static int her_icons_burst_opening = 1;
/* Estado de oscilacion axial lenta; no requiere memoria dinamica. */
static int32_t her_axis_phase = 0;
static int32_t her_axis_target = 0;
static uint64_t her_axis_next_change = 0;
static int      her_has_user_text   = 0;
static int      her_has_agent_text  = 0;
static uint32_t her_angle           = 0;
static uint64_t her_last_anim       = 0;

static int her_cursor_should_show(void)
{
    return her_icons_visible || her_min_count > 0 || her_num_windows > 0 || current_app != APP_DESKTOP;
}

/* --- geometría de ventana libre (drag + resize) --- */
static uint32_t her_win_x = 80,  her_win_y = 60;
static uint32_t her_win_w = 720, her_win_h = 480;
static int      her_dragging  = 0;
static int      her_resizing  = 0;
static int      her_resize_edge = 0;
static int32_t  her_drag_ox = 0, her_drag_oy = 0;
static int32_t  her_resize_start_mx = 0, her_resize_start_my = 0;
static int32_t  her_resize_start_x  = 0, her_resize_start_y  = 0;
static int32_t  her_resize_start_w  = 0, her_resize_start_h  = 0;

#define HER_TITLE_H   30
/* SOMA_PARCHE_028_UI: ventanas de esquinas redondeadas. El marco (1 px) pertenece al gestor de ventanas: las aplicaciones
 * reciben el rectangulo interior y ya no pueden pisarlo al repintarse. */
#define HER_WIN_R     10
/* SOMA_153B: el reloj de las animaciones del escritorio avanza como mucho esto por fotograma. Cubre 15 fps; tras una
 * pausa (fondo congelado mientras se usa el navegador) el movimiento continua donde estaba en vez de dar un salto. */
#ifndef HER_ANIM_DT_MAX
#define HER_ANIM_DT_MAX 70u
#endif
#define HER_CLI_X(wx)      ((wx) + 1u)
#define HER_CLI_Y(wy)      ((wy) + HER_TITLE_H)
#define HER_CLI_W(ww)      ((ww) > 2u ? (ww) - 2u : 1u)
#define HER_CLI_H(wh)      ((wh) > HER_TITLE_H + 1u ? (wh) - HER_TITLE_H - 1u : 1u)
#define HER_EDGE      10
#define HER_MIN_W   280
#define HER_MIN_H   180

/* SOMA_PARCHE_V2_GUI: anchos minimos utiles por aplicacion */
static uint32_t her_app_min_w(enum app_mode app)
{
    switch (app) {
    case APP_WEB_BROWSER:    return 520u;
    case APP_AGENT_MEMORY:   return 600u;
    case APP_FILE_MANAGER:   return 480u;
    case APP_IMAGE_VIEWER:   return 460u;
    case APP_NOTEPAD:        return 400u;
    case APP_LAYOUT:         return 820u;   /* PARCHE 084: hoja + barra */
    case APP_SETTINGS:       return 760u;   /* PARCHE 085 */
    case APP_PDF:            return 760u;   /* PARCHE 088 */
    case APP_SYSMON:         return 900u;   /* PARCHE 089 */
    case APP_RECORDER:       return 800u;   /* SOMA_177_T480S; SOMA_210: con la lista de grabaciones a la vista */
    case APP_CAMERA:         return 760u;   /* SOMA_178_T480S; SOMA_210 */
    case APP_CAPTURE:        return 720u;   /* SOMA_179_T480S; SOMA_210 */
    case APP_PAINT:          return 860u;   /* PARCHE 083: lienzo + herramientas + panel de color */
    case APP_CODER_APP:      return 720u;   /* PARCHE 093 */
    case APP_MEDIA:          return 560u;
    case APP_SOMA_APP:       return 320u; /*PL12_APP_MIN_W*/
    case APP_TERMINAL_SHELL: return 360u;
    case APP_AGENTS:         return 400u;
    default:                 return 340u;
    }
}

/* ====================================================================
 * MATEMÁTICA Y ESTRUCTURAS DEL VÓRTICE CÓSMICO (RING 0 / FIXED-POINT)
 * ==================================================================== */
static uint32_t her_isqrt(uint32_t val)
{
    uint32_t res = 0;
    uint32_t bit = 1u << 30;
    while (bit > val) bit >>= 2;
    while (bit != 0) {
        if (val >= res + bit) {
            val -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

#define HER_TORUS_PART  160  /* Rosquilla de partículas en acreción */
#define HER_STARS_PART  48   /* Estrellas cósmicas en toda la interfaz */
#define HER_MAX_PART    (HER_TORUS_PART + HER_STARS_PART)

struct her_particle {
    uint16_t angle;          /* Ángulo polar 0..1023 (1024 = 360°) */
    int32_t  r16;            /* Radio polar fixed-point (r << 8) */
    int32_t  vr16;           /* Velocidad radial hacia el centro */
    int16_t  omega;          /* Velocidad angular base */
    int32_t  prev_x, prev_y; /* Estela previa */
    uint8_t  size;
    uint32_t color;
};

struct her_cosmic_star {
    int32_t  x, y;           /* Posición fija 16.8 en pantalla */
    int32_t  vx, vy;         /* Velocidad fija 16.8 */
    int32_t  prev_x, prev_y;
    uint32_t color;
};

static struct her_particle     her_torus[HER_TORUS_PART];
static struct her_cosmic_star   her_stars[HER_STARS_PART];
static int her_cosmic_ready = 0;
static int soma_vortex_boost = 0;

/* Tabla seno 8-bit */
static const int8_t HER_SIN_TAB[64] = {
      0,  12,  25,  37,  49,  60,  71,  81,
     90,  98, 105, 111, 116, 120, 123, 125,
    127, 125, 123, 120, 116, 111, 105,  98,
     90,  81,  71,  60,  49,  37,  25,  12,
      0, -12, -25, -37, -49, -60, -71, -81,
    -90, -98,-105,-111,-116,-120,-123,-125,
   -127,-125,-123,-120,-116,-111,-105, -98,
    -90, -81, -71, -60, -49, -37, -25, -12
};
static int __attribute__((unused)) her_isin(uint32_t a) { return (int)HER_SIN_TAB[(a >> 2) & 63]; }
static int __attribute__((unused)) her_icos(uint32_t a) { return (int)HER_SIN_TAB[((a + 64) >> 2) & 63]; }

static uint32_t __attribute__((unused)) her_rainbow(uint32_t t)
{
    static const uint32_t pal[8] = {
        0x00FF6B6B, 0x00FFA94D, 0x00FFE66D, 0x006BCB77,
        0x004D96FF, 0x009B5DE5, 0x00F15BB5, 0x00FF6B6B
    };
    return pal[(t >> 3) & 7];
}

static uint32_t her_rng = 0xA53C9E7Bu;
static uint32_t her_rand(void)
{
    her_rng = her_rng * 1664525u + 1013904223u;
    return her_rng;
}

/* ====================================================================
 * SOMA HER-AURA v4 (ESTADO, ARMONICOS Y TEXTO)
 * ==================================================================== */
static volatile int her_agent_busy;

enum { HER_ST_IDLE = 0, HER_ST_LISTEN, HER_ST_THINK, HER_ST_SPEAK };

#define HER_RING_N   48
#define HER_ANS_MAX  1536
#define HER_THK_MAX  640

static const int16_t HER_S[256] = {
    0, 25, 50, 75, 100, 125, 150, 175, 200, 224, 249, 273, 297, 321, 345, 369,
    392, 415, 438, 460, 483, 505, 526, 548, 569, 590, 610, 630, 650, 669, 688, 706,
    724, 742, 759, 775, 792, 807, 822, 837, 851, 865, 878, 891, 903, 915, 926, 936,
    946, 955, 964, 972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024,
    1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009, 1004, 999, 993, 987, 980, 972, 964, 955,
    946, 936, 926, 915, 903, 891, 878, 865, 851, 837, 822, 807, 792, 775, 759, 742,
    724, 706, 688, 669, 650, 630, 610, 590, 569, 548, 526, 505, 483, 460, 438, 415,
    392, 369, 345, 321, 297, 273, 249, 224, 200, 175, 150, 125, 100, 75, 50, 25,
    0, -25, -50, -75, -100, -125, -150, -175, -200, -224, -249, -273, -297, -321, -345, -369,
    -392, -415, -438, -460, -483, -505, -526, -548, -569, -590, -610, -630, -650, -669, -688, -706,
    -724, -742, -759, -775, -792, -807, -822, -837, -851, -865, -878, -891, -903, -915, -926, -936,
    -946, -955, -964, -972, -980, -987, -993, -999, -1004, -1009, -1013, -1016, -1019, -1021, -1023, -1024,
    -1024, -1024, -1023, -1021, -1019, -1016, -1013, -1009, -1004, -999, -993, -987, -980, -972, -964, -955,
    -946, -936, -926, -915, -903, -891, -878, -865, -851, -837, -822, -807, -792, -775, -759, -742,
    -724, -706, -688, -669, -650, -630, -610, -590, -569, -548, -526, -505, -483, -460, -438, -415,
    -392, -369, -345, -321, -297, -273, -249, -224, -200, -175, -150, -125, -100, -75, -50, -25,
};

static int her_sn(uint32_t a)
{
    uint32_t i = (a >> 2) & 255u, f = a & 3u;
    int s0 = HER_S[i], s1 = HER_S[(i + 1u) & 255u];
    return s0 + ((s1 - s0) * (int)f) / 4;
}
static int her_cs(uint32_t a) { return her_sn(a + 256u); }

static uint32_t her_mix256(uint32_t c1, uint32_t c2, uint32_t k)
{
    uint32_t r = ((((c1 >> 16) & 255u) * (256u - k)) + (((c2 >> 16) & 255u) * k)) >> 8;
    uint32_t g = ((((c1 >> 8) & 255u) * (256u - k)) + (((c2 >> 8) & 255u) * k)) >> 8;
    uint32_t b = (((c1 & 255u) * (256u - k)) + ((c2 & 255u) * k)) >> 8;
    return (r << 16) | (g << 8) | b;
}
static uint32_t her_mix(uint32_t c1, uint32_t c2, uint32_t k16) { return her_mix256(c1, c2, k16 * 16u); }
static uint32_t her_scale(uint32_t c, uint32_t k)
{
    if (k > 256u) k = 256u;
    return ((((c >> 16) & 255u) * k >> 8) << 16) |
           ((((c >> 8) & 255u) * k >> 8) << 8) |
           (((c & 255u) * k) >> 8);
}
/* SOMA BLACK HOLE REALISM PATCH v1
 * Paleta acromatica fria: acero azulado -> blanco frio, sin arcoiris.
 * La variacion de luminancia es suave y usa la tabla seno ya existente.
 */
static uint32_t her_grad(uint32_t a)
{
    int wave = her_sn(a & 1023u);
    uint32_t k = 112u + (uint32_t)(wave + 1024) * 64u / 2048u;
    return her_mix256(0x00B9CBE0u, 0x00F7FAFFu, k);
}
static int her_ease(int cur, int tgt, uint32_t k)
{
    int d = tgt - cur;
    int s = (d * (int)k) / 256;
    if (s == 0 && d != 0) s = (d > 0) ? 1 : -1;
    return cur + s;
}

static uint16_t *her_ans = 0;
static uint32_t her_ans_len = 0, her_ans_reveal = 0;
static uint16_t *her_thk = 0;
static uint32_t her_thk_len = 0, her_thk_reveal = 0;
static char     her_last_q[SOMA_AGENT_CMD_MAX];
static uint32_t her_energy = 0, her_pulse = 0, her_prev_len = 0;
static uint32_t her_col_cur = 0x007DD3FCu;
static int      her_cur_r16 = 56 * 16, her_breath = 0;
static int      her_cur_amp[4] = { 32, 19, 0, 0 };
static int      her_cur_speed = 14, her_cur_spec = 70, her_cur_glow = 70;
static uint32_t her_phacc[4] = { 0, 0, 0, 0 };
static uint32_t her_grad_acc = 0, her_last_draw = 0;

static uint16_t her_ans_ls[40], her_ans_ll[40];
static uint16_t her_thk_ls[16], her_thk_ll[16];
static int her_bufs_ready = 0;

static void her_bufs_ensure(void)
{
    if (her_bufs_ready) return;
    her_ans = (uint16_t *)kmalloc(HER_ANS_MAX * sizeof(uint16_t));
    her_thk = (uint16_t *)kmalloc(HER_THK_MAX * sizeof(uint16_t));
    if (!her_ans || !her_thk) { her_ans_len = 0; her_thk_len = 0; return; }
    for (uint32_t i = 0; i < HER_ANS_MAX; ++i) her_ans[i] = 0;
    for (uint32_t i = 0; i < HER_THK_MAX; ++i) her_thk[i] = 0;
    her_bufs_ready = 1;
}

static int her_state(void)
{
    if (her_ans_reveal < her_ans_len || her_energy > 24u) return HER_ST_SPEAK;
    if (her_agent_busy) return HER_ST_THINK;
    if (agent_global_len > 0 && agent_global_focus) return HER_ST_LISTEN;
    return HER_ST_IDLE;
}

struct her_u8 { uint32_t cp; int need; };

static uint16_t her_cp_cell(uint32_t cp)
{
    if (cp == '\n') return (uint16_t)'\n';
    if (cp == '\t') return (uint16_t)' ';
    if (cp < 32u || cp == 127u) return 0;
    if (cp < 127u) return (uint16_t)cp;
    switch (cp) {
    case 0xE1: return (uint16_t)('a' | (1u << 8));
    case 0xE9: return (uint16_t)('e' | (1u << 8));
    case 0xED: return (uint16_t)('i' | (1u << 8));
    case 0xF3: return (uint16_t)('o' | (1u << 8));
    case 0xFA: return (uint16_t)('u' | (1u << 8));
    case 0xC1: return (uint16_t)('A' | (1u << 8));
    case 0xC9: return (uint16_t)('E' | (1u << 8));
    case 0xCD: return (uint16_t)('I' | (1u << 8));
    case 0xD3: return (uint16_t)('O' | (1u << 8));
    case 0xDA: return (uint16_t)('U' | (1u << 8));
    case 0xF1: return (uint16_t)('n' | (2u << 8));
    case 0xD1: return (uint16_t)('N' | (2u << 8));
    case 0xFC: return (uint16_t)('u' | (3u << 8));
    case 0xDC: return (uint16_t)('U' | (3u << 8));
    case 0xBF: return (uint16_t)('?' | (4u << 8));
    case 0xA1: return (uint16_t)('!' | (4u << 8));
    default:   return (uint16_t)'?';
    }
}

static int her_u8_feed(struct her_u8 *d, uint8_t b, uint16_t *out)
{
    uint16_t cell;
    if (b < 0x80u) {
        d->need = 0; cell = her_cp_cell(b);
    } else if (b >= 0xF0u) { d->need = 3; d->cp = b & 7u;  return 0; }
    else if (b >= 0xE0u)   { d->need = 2; d->cp = b & 15u; return 0; }
    else if (b >= 0xC0u)   { d->need = 1; d->cp = b & 31u; return 0; }
    else {
        if (d->need <= 0) return 0;
        d->cp = (d->cp << 6) | (b & 63u);
        if (--d->need > 0) return 0;
        cell = her_cp_cell(d->cp);
    }
    if (!cell) return 0;
    *out = cell;
    return 1;
}

static uint32_t her_text_cells_n(const char *s, uint32_t nb, uint16_t *dst, uint32_t max)
{
    struct her_u8 d = { 0, 0 };
    uint32_t n = 0;
    for (uint32_t i = 0; i < nb && s[i] && n < max; ++i) {
        uint16_t cell;
        if (her_u8_feed(&d, (uint8_t)s[i], &cell)) dst[n++] = cell;
    }
    return n;
}

static uint32_t her_text_cells(const char *s, uint16_t *dst, uint32_t max)
{
    uint32_t nb = 0; while (s[nb]) nb++;
    return her_text_cells_n(s, nb, dst, max);
}

static const char *her_find(const char *hay, const char *needle)
{
    for (; *hay; ++hay) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return hay;
    }
    return 0;
}

static void her_draw_cell(int32_t x, int32_t y, uint16_t cell, uint32_t col, int sc)
{
    uint8_t base = (uint8_t)(cell & 0xFFu);
    uint32_t mk = (uint32_t)(cell >> 8);
    /* SOMA_VECTOR_CELL_PATCH: texto del escritorio en vectorial */
    if (font_draw_cell_vector(x, y, base, mk, col, sc)) return;
    const uint8_t *g = font_get_glyph((char)base);
    uint8_t tmp[8];
    for (int r = 0; r < 8; ++r) tmp[r] = (mk == 4u) ? g[7 - r] : g[r];
    if (mk && mk != 4u && mk != 7u && base == 'i') tmp[0] = 0;
    fb_draw_glyph8_nodmg(x, y, tmp, col, sc);
    if (!mk || mk == 4u) return;

    int32_t s = sc;
    int32_t oy = (base >= 'A' && base <= 'Z') ? y - 2 * s : y;
#define HP(px, py) fb_fill_rect((uint32_t)(x + (px) * s), (uint32_t)(py), (uint32_t)s, (uint32_t)s, col)
    switch (mk) {
    case 1: HP(4, oy); HP(3, oy + s); break;
    case 2: HP(2, oy + s); HP(3, oy); HP(4, oy); HP(5, oy + s); break;
    case 3: HP(2, oy + s); HP(5, oy + s); break;
    default: break;
    }
#undef HP
}

static void her_draw_cells(int32_t x, int32_t y, const uint16_t *c, uint32_t len,
                           uint32_t g0, uint32_t reveal,
                           uint32_t base, uint32_t tip, int sc)
{
    int32_t adv = 9 * sc;
    for (uint32_t k = 0; k < len; ++k) {
        uint32_t gi = g0 + k;
        if (gi >= reveal) break;
        uint16_t cell = c[k];
        if ((cell & 0xFFu) == ' ') continue;
        uint32_t d = reveal - gi;
        uint32_t col = base;
        if (d < 14u) col = her_mix256(tip, base, d * 256u / 14u);
        her_draw_cell(x + (int32_t)k * adv, y, cell, col, sc);
    }
}

static void her_draw_label(uint32_t cur_w, uint32_t y, const char *s, uint32_t col)
{
    uint32_t n = 0; while (s[n]) n++;
    uint32_t w = n * 9u;
    uint32_t x = (cur_w > w) ? (cur_w - w) / 2u : 0u;
    for (uint32_t i = 0; i < n; ++i)
        her_draw_cell((int32_t)(x + i * 9u), (int32_t)y, (uint16_t)(uint8_t)s[i], col, 1);
}

static int her_wrap(const uint16_t *c, uint32_t n, uint32_t cols,
                    uint16_t *ls, uint16_t *ll, int maxl)
{
    int cnt = 0; uint32_t i = 0;
    if (cols < 1u) cols = 1u;
    while (i < n && cnt < maxl) {
        uint32_t start = i, j = i, end, next;
        int32_t last_sp = -1;
        while (j < n && (j - start) < cols) {
            uint8_t b = (uint8_t)(c[j] & 0xFFu);
            if (b == '\n') break;
            if (b == ' ') last_sp = (int32_t)j;
            j++;
        }
        if (j >= n)                         { end = n; next = n; }
        else if ((c[j] & 0xFFu) == '\n')    { end = j; next = j + 1u; }
        else if ((c[j] & 0xFFu) == ' ')     { end = j; next = j + 1u; }
        else if (last_sp > (int32_t)start)  { end = (uint32_t)last_sp; next = end + 1u; }
        else                                { end = j; next = j; }
        ls[cnt] = (uint16_t)start;
        ll[cnt] = (uint16_t)(end - start);
        cnt++; i = next;
    }
    return cnt;
}

static int her_ring_r16(uint32_t a)
{
    int r = her_cur_r16 + her_breath;
    r += her_sn(2u * a + (her_phacc[0] >> 10)) * her_cur_amp[0] / 1024;
    r += her_sn(3u * a + (her_phacc[1] >> 10)) * her_cur_amp[1] / 1024;
    r += her_sn(5u * a + (her_phacc[2] >> 10)) * her_cur_amp[2] / 1024;
    r += her_sn(7u * a + (her_phacc[3] >> 10)) * her_cur_amp[3] / 1024;
    return r;
}

static void her_reveal_step(void)
{
    her_bufs_ensure();
    if (!her_ans || !her_thk) return;
    if (her_ans_reveal < her_ans_len) {
        uint32_t step = 1u + her_ans_len / 160u;
        if (step > 6u) step = 6u;
        while (step-- && her_ans_reveal < her_ans_len) {
            uint8_t b = (uint8_t)(her_ans[her_ans_reveal] & 0xFFu);
            if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9'))
                her_energy += 40u;
            her_ans_reveal++;
        }
        if (her_energy > 255u) her_energy = 255u;
    }
    her_energy = (her_energy * 192u) >> 8;
    if (her_thk_reveal < her_thk_len) {
        her_thk_reveal += 3u + her_thk_len / 120u;
        if (her_thk_reveal > her_thk_len) her_thk_reveal = her_thk_len;
    }
}

static void her_begin_turn(const char *q)
{
    her_bufs_ensure();
    uint32_t i = 0; while (q && q[i] && i < sizeof(her_last_q) - 1u) { her_last_q[i] = q[i]; i++; }
    her_last_q[i] = '\0';
    her_ans_len = 0; her_ans_reveal = 0;
    her_thk_len = 0; her_thk_reveal = 0;
    her_energy = 0;
    her_text_suppressed_by_dock = 0;
}

static void her_finish_turn(const char *resp)
{
    her_bufs_ensure();
    static const char tag_r[] = "[Razonamiento]:\n";
    static const char tag_d[] = "\n\n[Dictamen]:\n";
    if (!resp) resp = "";
    const char *ans = resp;
    const char *d = her_find(resp, tag_d);
    uint32_t i = 0; while (tag_r[i] && resp[i] == tag_r[i]) i++;
    if (tag_r[i] == '\0' && d) {
        const char *ts = resp + (sizeof(tag_r) - 1u);
        if (her_thk_len == 0 && d > ts) {
            her_thk_reveal = 0;
            her_thk_len = her_text_cells_n(ts, (uint32_t)(d - ts), her_thk, HER_THK_MAX);
        }
        ans = d + (sizeof(tag_d) - 1u);
    }
    if (her_ans_len == 0) {
        her_ans_reveal = 0;
        her_ans_len = her_text_cells(*ans ? ans : "Mision ejecutada en hardware.",
                                     her_ans, HER_ANS_MAX);
    }
}

void gui_thought_update(const char *txt)
{
    coder_app_notify_thought(txt);
    her_bufs_ensure();
    if (!txt || !her_agent_busy) return;
    her_thk_reveal = 0; her_thk_len = 0;
    her_thk_len = her_text_cells(txt, her_thk, HER_THK_MAX);
}

static struct her_u8 her_stream_dec = { 0, 0 };

void gui_stream_begin(void)
{
    her_bufs_ensure();
    her_ans_len = 0; her_ans_reveal = 0;
    her_stream_dec.cp = 0; her_stream_dec.need = 0;
}

void gui_stream_push(const char *chunk)
{
    if (!chunk) return;
    for (uint32_t i = 0; chunk[i] && her_ans_len < HER_ANS_MAX; ++i) {
        uint16_t cell;
        if (her_u8_feed(&her_stream_dec, (uint8_t)chunk[i], &cell))
            her_ans[her_ans_len++] = cell;
    }
}

/* ====================================================================
 * ROSQUILLA DE ACRECIÓN Y ESTRELLAS CÓSMICAS (HER-COSMIC TORUS)
 * ==================================================================== */
static void her_cosmic_init_all(uint32_t cx, uint32_t cy)
{
    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    uint32_t max_reach = (cur_w > cur_h ? cur_w : cur_h);

    for (int i = 0; i < HER_TORUS_PART; ++i) {
        her_torus[i].angle = (uint16_t)(((uint32_t)i * 1024u) / HER_TORUS_PART);
        uint32_t r_base = 36u + ((uint32_t)i * 44u) / HER_TORUS_PART + (her_rand() & 15u);
        her_torus[i].r16 = (int32_t)r_base << 8;
        her_torus[i].vr16 = 20 + (int32_t)(her_rand() & 23u);
        her_torus[i].omega = 10 + (int32_t)(her_rand() & 7u);
        her_torus[i].color = her_grad(her_torus[i].angle);
        her_torus[i].size = ((i & 5) == 0) ? 2 : 1;
        her_torus[i].prev_x = -1;
        her_torus[i].prev_y = -1;
    }

    for (int s = 0; s < HER_STARS_PART; ++s) {
        uint32_t ang = (her_rand() >> 8) & 1023u;
        uint32_t dist = 110u + ((uint32_t)s * (max_reach / 2u)) / HER_STARS_PART + (her_rand() & 63u);
        int32_t sx = (int32_t)cx + ((dist * (uint32_t)her_cs(ang)) >> 10);
        int32_t sy = (int32_t)cy + ((dist * (uint32_t)her_sn(ang)) >> 10);
        her_stars[s].x = sx << 8;
        her_stars[s].y = sy << 8;
        her_stars[s].vx = 0;
        her_stars[s].vy = 0;
        her_stars[s].prev_x = sx;
        her_stars[s].prev_y = sy;
        her_stars[s].color = ((s & 3) == 0) ? 0x00FFFFFF : 0x00DCE8F5;
    }

    her_cosmic_ready = 1;
}

static void her_parts_spawn(uint32_t orb_cx, uint32_t orb_cy, uint32_t angle)
{
    (void)angle;
    if (!her_cosmic_ready) her_cosmic_init_all(orb_cx, orb_cy);
}

static void her_parts_update_draw(void)
{
    uint32_t cx = fb_get_width() / 2;
    uint32_t cy = fb_get_height() / 2;
    if (!her_cosmic_ready) her_cosmic_init_all(cx, cy);

    int spd = her_cur_speed + soma_vortex_boost;
    if (spd < 10) spd = 10;
    int step_mul = spd / 12;
    if (step_mul < 1) step_mul = 1;
    /* SOMA_153B: la fisica de las estrellas avanzaba UN paso por fotograma: su velocidad dependia de los fotogramas por
     * segundo (a 4 fps se arrastraban) y cada fotograma lento era un tiron. Ahora avanza en pasos fijos de 33 ms segun el
     * tiempo real transcurrido: misma velocidad a cualquier ritmo y movimiento uniforme. */
    static uint32_t st_last, st_acc;
    uint32_t st_now = (uint32_t)timer_get_uptime_ms();
    uint32_t st_dt = st_last ? st_now - st_last : 33u; st_last = st_now;
    if (st_dt > HER_ANIM_DT_MAX) st_dt = HER_ANIM_DT_MAX;
    st_acc += st_dt;
    int st_steps = (int)(st_acc / 33u); st_acc -= (uint32_t)st_steps * 33u;

    /* A. ESTRELLAS CÓSMICAS EN TODA LA PANTALLA */
    uint32_t max_d = fb_get_width() > fb_get_height() ? fb_get_width() : fb_get_height();
    for (int s = 0; s < HER_STARS_PART; ++s) {
        struct her_cosmic_star *st = &her_stars[s];
        int32_t px0 = st->prev_x, py0 = st->prev_y; int respawned = 0;
        for (int stp = 0; stp < st_steps && !respawned; ++stp) {
        int32_t cur_x = st->x >> 8;
        int32_t cur_y = st->y >> 8;
        int32_t dx = (int32_t)cx - cur_x;
        int32_t dy = (int32_t)cy - cur_y;
        /* PARCHE 030: dx*dx + dy*dy desbordaba int32 con estrellas lejanas (comportamiento indefinido; el valor envuelto
         * podia dar una "distancia" pequena falsa y activar el efecto del cursor en estrellas que no estaban cerca) */
        int64_t d2 = (int64_t)dx * dx + (int64_t)dy * dy;
        uint32_t dist = (uint32_t)her_isqrt(d2 > 0xFFFFFFFFLL ? 0xFFFFFFFFu : (uint32_t)d2);

        if (dist <= 30u) {
            uint32_t r_ang = (her_rand() >> 8) & 1023u;
            uint32_t r_dist = max_d / 2u + 20u + (her_rand() & 127u);
            int32_t nx = (int32_t)cx + ((r_dist * (uint32_t)her_cs(r_ang)) >> 10);
            int32_t ny = (int32_t)cy + ((r_dist * (uint32_t)her_sn(r_ang)) >> 10);
            st->x = nx << 8;
            st->y = ny << 8;
            st->vx = 0; st->vy = 0;
            st->prev_x = nx; st->prev_y = ny;
            respawned = 1;
            continue;
        }

        int32_t pull = 6 + (int32_t)(1200u / (dist + 30u));
        int32_t swirl = 5 + (int32_t)(1500u / (dist + 20u));
        int32_t ax = (dx * pull + dy * swirl) / (int32_t)dist;
        int32_t ay = (dy * pull - dx * swirl) / (int32_t)dist;

        st->vx = ((st->vx * 248) >> 8) + ax;
        st->vy = ((st->vy * 248) >> 8) + ay;
        st->x += st->vx;
        st->y += st->vy;
        }
        if (respawned) continue;
        if (st_steps == 0) { px0 = st->prev_x; py0 = st->prev_y; }

        int32_t px = st->x >> 8;
        int32_t py = st->y >> 8;
        int32_t ddx = (int32_t)cx - px, ddy = (int32_t)cy - py;
        int64_t dd2 = (int64_t)ddx * ddx + (int64_t)ddy * ddy;
        uint32_t dist = (uint32_t)her_isqrt(dd2 > 0xFFFFFFFFLL ? 0xFFFFFFFFu : (uint32_t)dd2);

        if (dist < 90u && (px0 != px || py0 != py)) {
            fb_draw_line(px0, py0, px, py, 0x008CA8C7);
        }
        st->prev_x = px;
        st->prev_y = py;

        if (dist < 80u) {
            fb_draw_pixel((uint32_t)px, (uint32_t)py, 0x00FFFFFF);
        } else {
            fb_draw_pixel((uint32_t)px, (uint32_t)py, st->color);
        }
    }

    /* B. ROSQUILLA DE ACRECIÓN (Toro de partículas denso) */
    for (int i = 0; i < HER_TORUS_PART; ++i) {
        struct her_particle *p = &her_torus[i];
        int32_t curr_r = p->r16 >> 8;

        int wave_offset = (her_ring_r16(p->angle) >> 4) - 56;
        int32_t effective_r = curr_r + wave_offset;

        int32_t ang_v = (int32_t)p->omega + (3600 / (curr_r > 5 ? curr_r : 5));
        p->angle = (uint16_t)((p->angle + (ang_v * step_mul * (int32_t)st_dt) / (4 * 33)) & 1023u);   /* SOMA_153B: por tiempo */

        int32_t rad_v = p->vr16 + (2400 / (curr_r > 6 ? curr_r : 6));
        p->r16 -= (rad_v * step_mul * (int32_t)st_dt) / (4 * 33);
        curr_r = p->r16 >> 8;

        if (curr_r <= 28) {
            p->r16 = (int32_t)(76u + (her_rand() & 23u)) << 8;
            p->angle = (uint16_t)((p->angle + 512u + (her_rand() & 255u)) & 1023u);
            p->color = her_grad((uint32_t)p->angle + (her_grad_acc >> 10));
            p->prev_x = -1;
            p->prev_y = -1;
            continue;
        }

        uint32_t axis = (uint32_t)her_axis_phase & 1023u;
        int32_t local_x = (effective_r * her_cs(p->angle)) >> 10;
        int32_t local_y = (effective_r * her_sn(p->angle)) >> 10;
        int32_t tilted_x = (local_x * her_cs(axis) - local_y * her_sn(axis)) >> 10;
        int32_t tilted_y = (local_x * her_sn(axis) + local_y * her_cs(axis)) >> 10;
        int32_t px = (int32_t)cx + tilted_x;
        int32_t py = (int32_t)cy + tilted_y;

        if (p->prev_x >= 0 && (p->prev_x != px || p->prev_y != py)) {
            /* Estelas blanco-frio; el nucleo sigue siendo blanco puro. */
            uint32_t trail_col = (curr_r < 42) ? 0x00F7FBFF : 0x00D5E2F0;
            uint32_t trail_light = (curr_r < 42) ? 184u : 120u;
            fb_draw_line(p->prev_x, p->prev_y, px, py,
                         her_scale(trail_col, trail_light));
        }
        p->prev_x = px;
        p->prev_y = py;

        if (curr_r <= 38) {
            fb_draw_pixel((uint32_t)px, (uint32_t)py, 0x00FFFFFF);
        } else if (p->size > 1) {
            fb_fill_circle(px, py, (int32_t)p->size, p->color);
            fb_draw_pixel((uint32_t)px, (uint32_t)py, 0x00FFFFFF);
        } else {
            fb_draw_pixel((uint32_t)px, (uint32_t)py, p->color);
        }
    }
}

/* El eje cambia de objetivo cada pocos segundos y se acerca suavemente.
 * Angulos en unidades de la tabla seno (1024 = vuelta completa). */
static void her_update_axis_wobble(uint64_t now_ms)
{
    if (her_axis_next_change == 0 || now_ms >= her_axis_next_change) {
        her_axis_target = (int32_t)(her_rand() % 97u) - 48;
        her_axis_next_change = now_ms + 2300u + (her_rand() % 2600u);
    }
    int32_t delta = her_axis_target - her_axis_phase;
    if (delta > 2) her_axis_phase += 2;
    else if (delta < -2) her_axis_phase -= 2;
    else her_axis_phase = her_axis_target;
}

/* ---------- orbe central: horizonte de sucesos ampliado y onda fluida ---------- */
static void her_draw_orb(uint32_t cx, uint32_t cy, uint32_t angle)
{
    static const int32_t W[4] = { 90, -140, 230, -330 };
    (void)angle;

    uint32_t t = (uint32_t)timer_get_uptime_ms();
    uint32_t dt = her_last_draw ? (t - her_last_draw) : 33u;
    her_last_draw = t;
    if (dt < 1u) dt = 1u;
    if (dt > HER_ANIM_DT_MAX) dt = HER_ANIM_DT_MAX;          /* SOMA_153B */

    int st = her_state();

    if (agent_global_len != her_prev_len) {
        her_pulse = 190u;
        her_prev_len = agent_global_len;
        soma_vortex_boost += 5;
        if (soma_vortex_boost > 14) soma_vortex_boost = 14;
    } else {
        her_pulse = (her_pulse > dt) ? her_pulse - dt : 0u;
    }

    int fluid_wave = her_sn((t % 2800u) * 1024u / 2800u) * 20 / 1024;

    int tr = 58 * 16, tsp = 15, tspec = 70, tglow = 70, bamp = 40;
    int ta[4] = { 42 + fluid_wave, 26 - fluid_wave / 2, 16 + fluid_wave / 3, 10 };
    uint32_t ttint = 0x007DD3FCu, bp = 4200u;

    int non_uniform_glide = (her_sn((t % 3600u) * 1024u / 3600u) * 8) / 1024;
    tsp += non_uniform_glide;

    switch (st) {
    case HER_ST_LISTEN:
        tr = 60 * 16;
        ta[0] = 43 + (int)(her_pulse * 4u / 25u);
        ta[1] = 30 + (int)(her_pulse / 18u);
        ta[2] = 16;
        tsp = 18 + non_uniform_glide; tspec = 52; tglow = 86; bamp = 42;
        ttint = 0x00BAE6FDu; bp = 2600u;
        break;
    case HER_ST_THINK:
        tr = 56 * 16;
        ta[0] = 62; ta[1] = 36; ta[2] = 20; ta[3] = 8;
        tsp = 20 + non_uniform_glide; tspec = 100; tglow = 88; bamp = 42;
        ttint = 0x00C4B5FDu; bp = 1700u;
        break;
    case HER_ST_SPEAK: {
        int e = (int)her_energy;
        tr = 58 * 16 + e * 28 / 255;
        ta[0] = 45 + e * 15 / 255;
        ta[1] = 30 + e * 18 / 255;
        ta[2] = 17 + e * 20 / 255;
        ta[3] = e * 10 / 255;
        tsp = 19 + e / 32; tspec = 100; tglow = 86 + e / 10; bamp = 42;
        ttint = 0x00FDA4AFu; bp = 1400u;
        break; }
    default:
        break;
    }

    /* Las acciones aportan un impulso pequeno y amortiguado, no un latigazo. */
    if (soma_vortex_boost > 0) {
        int boost_amp = (soma_vortex_boost * 8) / 100;
        ta[0] += boost_amp;
        ta[1] += boost_amp / 2;
        ta[2] += boost_amp / 3;
        tsp += soma_vortex_boost / 8;
        tglow += soma_vortex_boost / 12;
        soma_vortex_boost = (soma_vortex_boost * 210) >> 8;
    }

    uint32_t kk = (st == HER_ST_SPEAK) ? dt * 256u / 140u : dt * 256u / 360u;
    if (kk > 230u) kk = 230u;
    if (kk < 8u) kk = 8u;
    her_cur_r16 = her_ease(her_cur_r16, tr, kk);
    for (int k = 0; k < 4; ++k) her_cur_amp[k] = her_ease(her_cur_amp[k], ta[k], kk);
    her_cur_speed = her_ease(her_cur_speed, tsp, kk);
    her_cur_spec  = her_ease(her_cur_spec, tspec, kk);
    her_cur_glow  = her_ease(her_cur_glow, tglow, kk);
    her_col_cur = her_mix256(her_col_cur, ttint, kk);

    her_breath = her_sn((t % bp) * 1024u / bp) * bamp / 1024;
    for (int k = 0; k < 4; ++k)
        her_phacc[k] += (uint32_t)((int32_t)dt * W[k] * her_cur_speed / 16);
    her_grad_acc += dt * 60u * (uint32_t)her_cur_speed / 16u;

    /* ---- HORIZONTE DE SUCESOS ----
     * Sin circunferencia ni anillo fotonico dibujado: solo el nucleo negro.
     * La estructura se percibe a traves del flujo de particulas y sus estelas.
     * El eje del flujo cambia de inclinacion de forma lenta e irregular.
     */
    int32_t icx = (int32_t)cx, icy = (int32_t)cy;
    her_update_axis_wobble(t);
    fb_fill_circle(icx, icy, 27, 0x00000000);

}

static void agent_global_insert_str(const char *s)
{
    uint32_t slen = 0; while (s[slen]) slen++;
    if (slen == 0 || agent_global_len + slen >= sizeof(agent_global_cmd) - 2) return;
    for (uint32_t i = agent_global_len + slen; i >= agent_global_cursor + slen; --i)
        agent_global_cmd[i] = agent_global_cmd[i - slen];
    for (uint32_t i = 0; i < slen; ++i)
        agent_global_cmd[agent_global_cursor + i] = s[i];
    agent_global_cursor += slen;
    agent_global_len += slen;
    agent_global_cmd[agent_global_len] = '\0';
    agent_global_scroll = 0;
}

static void her_draw_user_zone(uint32_t cur_w, uint32_t cur_h, uint32_t orb_cy)
{
    (void)cur_h;
    if (her_icons_visible) return;
    if (cur_w < 160u) return;
    uint32_t t = (uint32_t)timer_get_uptime_ms();

    if (agent_global_len > 0) {
        static uint16_t ucells[SOMA_AGENT_CMD_MAX];
        uint32_t n = her_text_cells_n(agent_global_cmd, agent_global_len, ucells, SOMA_AGENT_CMD_MAX);
        int sc = (n * 18u <= cur_w - 120u) ? 2 : 1;
        uint32_t adv = 9u * (uint32_t)sc;
        uint32_t maxc = (cur_w - 80u) / adv;
        static uint16_t cur_dummy[SOMA_AGENT_CMD_MAX];
        uint32_t cur = her_text_cells_n(agent_global_cmd, agent_global_cursor, cur_dummy, SOMA_AGENT_CMD_MAX);
        if (cur > n) cur = n;
        uint32_t start = 0;
        if (n > maxc) {
            start = (cur > maxc) ? cur - maxc : 0u;
            if (start + maxc > n) start = n - maxc;
        }
        uint32_t shown = n - start;
        if (shown > maxc) shown = maxc;
        uint32_t w = shown * adv;
        uint32_t tx = (cur_w > w) ? (cur_w - w) / 2u : 0u;
        uint32_t ty = (orb_cy > 170u) ? orb_cy - 150u : 20u;
        for (uint32_t k = 0; k < shown; ++k) {
            uint16_t cell = ucells[start + k];
            if ((cell & 0xFFu) == ' ') continue;
            her_draw_cell((int32_t)(tx + k * adv), (int32_t)ty,
                          cell, 0x00F1F5F9u, sc);
        }
        if (agent_global_focus) {
            uint32_t lvl = (uint32_t)(her_sn((t % 1400u) * 1024u / 1400u) + 1024) * 256u / 2048u;
            uint32_t col = her_mix256(0x00334155u, her_col_cur, lvl);
            uint32_t cx = tx + (cur - start) * adv;
            fb_fill_rect((cx > 1u) ? cx - 1u : 0u, ty - 2u, (uint32_t)sc, 8u * (uint32_t)sc + 4u, col);
        }
    } else if (her_last_q[0] && (her_ans_len > 0 || her_agent_busy)) {
        static uint16_t qcells[SOMA_AGENT_CMD_MAX];
        uint32_t n = her_text_cells(her_last_q, qcells, SOMA_AGENT_CMD_MAX);
        uint32_t maxc = (cur_w - 80u) / 9u;
        if (n > maxc) n = maxc;
        uint32_t tx = (cur_w > n * 9u) ? (cur_w - n * 9u) / 2u : 0u;
        uint32_t ty = (orb_cy > 170u) ? orb_cy - 146u : 20u;
        for (uint32_t k = 0; k < n; ++k) {
            uint16_t cell = qcells[k];
            if ((cell & 0xFFu) == ' ') continue;
            her_draw_cell((int32_t)(tx + k * 9u), (int32_t)ty,
                          cell, 0x00475569u, 1);
        }
    } else if (current_app == APP_DESKTOP && !her_vortex_closing) {
        /* Prompt interactivo listo para escribir al agente */
        uint32_t ty = (orb_cy > 170u) ? orb_cy - 146u : 20u;
        const char *hint = "soma> escribe una mision...";
        uint32_t n = 0; while (hint[n]) n++;
        uint32_t w = n * 9u;
        uint32_t tx = (cur_w > w) ? (cur_w - w) / 2u : 0u;
        for (uint32_t k = 0; k < n; ++k) {
            her_draw_cell((int32_t)(tx + k * 9u), (int32_t)ty,
                          (uint16_t)(uint8_t)hint[k], 0x00334155u, 1);
        }
        uint32_t lvl = (uint32_t)(her_sn((t % 1400u) * 1024u / 1400u) + 1024) * 256u / 2048u;
        uint32_t col = her_mix256(0x001E293Bu, her_col_cur, lvl);
        fb_fill_rect(tx + w + 3u, ty - 1u, 2u, 10u, col);
    }
}

static void her_draw_agent_zone(uint32_t cur_w, uint32_t cur_h, uint32_t orb_cy)
{
    if (her_icons_visible || her_text_suppressed_by_dock) return;
    her_bufs_ensure();
    if (her_ans_len == 0 && her_thk_len == 0) return;
    if (cur_w < 200u) return;

    uint32_t y = orb_cy + 112u;
    uint32_t bottom = (cur_h > 40u) ? cur_h - 40u : cur_h;
    if (her_icons_visible && cur_h > 130u) bottom = cur_h - 130u;
    if (bottom <= y + 32u) return;
    uint32_t avail = bottom - y;
    uint32_t width_cap = cur_w - 120u;
    uint32_t block_end = y;

    if (her_ans_len > 0) {
        int sc = (her_ans_len <= 150u) ? 2 : 1;
        uint32_t adv = 9u * (uint32_t)sc;
        uint32_t lh = (sc == 2) ? 28u : 16u;
        uint32_t wmax = width_cap;
        uint32_t cap = (sc == 2) ? 900u : 820u;
        if (wmax > cap) wmax = cap;
        uint32_t cols = wmax / adv;
        if (cols < 8u) cols = 8u;
        int n = her_wrap(her_ans, her_ans_len, cols, her_ans_ls, her_ans_ll, 40);
        uint32_t reserve = (her_thk_len > 0) ? 96u : 0u;
        uint32_t maxl = (avail > reserve + lh) ? (avail - reserve) / lh : 1u;
        if (maxl < 2u) maxl = 2u;
        uint32_t total = ((uint32_t)n < maxl) ? (uint32_t)n : maxl;
        uint32_t started = 0;
        while ((int)started < n && her_ans_ls[started] < her_ans_reveal) started++;
        uint32_t first = (started > maxl) ? started - maxl : 0u;
        for (uint32_t l = first; l < started; ++l) {
            uint32_t w = (uint32_t)her_ans_ll[l] * adv;
            uint32_t x = (cur_w > w) ? (cur_w - w) / 2u : 0u;
            her_draw_cells((int32_t)x, (int32_t)(y + (l - first) * lh),
                           her_ans + her_ans_ls[l], her_ans_ll[l], her_ans_ls[l],
                           her_ans_reveal, 0x00F1F5F9u, her_col_cur, sc);
        }
        block_end = y + total * lh;
    }

    if (her_thk_len > 0) {
        int has_ans = (her_ans_len > 0);
        uint32_t ty = has_ans ? block_end + 22u : y;
        uint32_t wmax = (width_cap > 760u) ? 760u : width_cap;
        uint32_t cols = wmax / 9u;
        if (cols < 8u) cols = 8u;
        int n = her_wrap(her_thk, her_thk_len, cols, her_thk_ls, her_thk_ll, 16);
        uint32_t started = 0;
        while ((int)started < n && her_thk_ls[started] < her_thk_reveal) started++;
        uint32_t first = (started > 3u) ? started - 3u : 0u;
        if (has_ans) {
            her_draw_label(cur_w, ty, "pensamiento", 0x00334155u);
            ty += 20u;
        }
        uint32_t base = has_ans ? 0x00475569u : 0x0064748Bu;
        uint32_t tip = her_mix(her_col_cur, base, 6u);
        if (ty + 48u < cur_h) {
            for (uint32_t l = first; l < started; ++l) {
                uint32_t w = (uint32_t)her_thk_ll[l] * 9u;
                uint32_t x = (cur_w > w) ? (cur_w - w) / 2u : 0u;
                her_draw_cells((int32_t)x, (int32_t)(ty + (l - first) * 16u),
                               her_thk + her_thk_ls[l], her_thk_ll[l], her_thk_ls[l],
                               her_thk_reveal, base, tip, 1);
            }
        }
    }
}

/* SOMA TEXT ABSORPTION PATCH v5 */
/* SOMA ASYNC MATERIALIZE PATCH v4 */
/* SOMA ORBITAL CALM PATCH v3 */
/* SOMA ORBITAL ICONS PATCH v2: iconos como cuerpos en orbita estable. */
#define HER_DOCK_ITEM_COUNT 18
#define HER_ICON_ANIM_MAX   256u
#define HER_ICON_BOX_SIZE   44

struct her_dock_item {
    int icon;
    uint32_t color;
    const char *label;
    enum app_mode app;
};

static const struct her_dock_item her_dock_items[HER_DOCK_ITEM_COUNT] = {
    { ICON_AGENT,    0x00E0F2FE,  "SOMA",    APP_AGENT_CHAT },      /* Blanco plasma estelar */
    { ICON_TERMINAL, 0x0038BDF8,  "TERM",    APP_TERMINAL_SHELL },  /* Cian radiación */
    { ICON_BROWSER,  0x0060A5FA,  "WEB",     APP_WEB_BROWSER },     /* Azul fotónico */
    { ICON_FILES,    0x0094A3B8,  "FILES",   APP_FILE_MANAGER },    /* Titanio frío */
    { ICON_IMAGE,    0x00A78BFA,  "IMG",     APP_IMAGE_VIEWER },    /* Nebulosa cuántica */
    { ICON_NOTES,    0x00CBD5E1,  "NOTES",   APP_NOTEPAD },         /* Acero claro / Cristal */
    { ICON_MEMORY,   0x00818CF8,  "MEM",     APP_AGENT_MEMORY },    /* Zafiro sináptico */
    { ICON_LAYOUT,   0x0038BDF8,  "LAYOUT",  APP_LAYOUT },          /* Cian polar */
    { ICON_CODER,    0x0000FF88,  "CODER",   APP_CODER_APP },
    { ICON_PAINT,    0x00F59E0B,  "PAINT",   APP_PAINT },           /* Ámbar / Pintura */
    { ICON_AGENTS,   0x00BAE6FD,  "AGENTES", APP_AGENTS },          /* Éter orbital */
    { ICON_MEDIA,    0x00FB7185,  "MEDIA",   APP_MEDIA },           /* Rosa estelar: pulsar */
    { ICON_PDF,      0x00F87171,  "PDF",     APP_PDF },             /* PARCHE 088: lector de PDF */
    { ICON_MONITOR,  0x0034D399,  "MONITOR", APP_SYSMON },          /* PARCHE 089: monitor del sistema */
    { ICON_MIC,      0x00F87171,  "GRABAR",  APP_RECORDER },        /* SOMA_177_T480S: grabadora de sonido */
    { ICON_CAM,      0x0038BDF8,  "CAMARA",  APP_CAMERA },          /* SOMA_178_T480S: camara web */
    { ICON_SCR,      0x00A78BFA,  "CAPTURA", APP_CAPTURE },         /* SOMA_179_T480S: captura de pantalla */
    { ICON_SETTINGS, 0x0094A3B8,  "AJUSTES", APP_SETTINGS },        /* PARCHE 085: engranaje */
};

/* La aparicion se escalona ligeramente para que los iconos escapen uno tras
 * otro. Esta funcion es compartida por el render y la deteccion de clics.
 */
static uint32_t her_icon_item_progress(int i)
{
    if (i < 0 || i >= HER_DOCK_ITEM_COUNT) return 0;
    uint32_t delay = (uint32_t)i * 6u; /* Velocidad 2x */
    if (her_icons_anim_phase <= delay) return 0;
    return ((her_icons_anim_phase - delay) * HER_ICON_ANIM_MAX) /
           (HER_ICON_ANIM_MAX - delay);
}

/* Orbitas amplias, adaptadas a resoluciones pequeñas para no expulsar iconos
 * fuera de pantalla. El radio supera ampliamente el horizonte de sucesos. */
static int32_t her_orbit_radius_x(uint32_t cur_w)
{
    int32_t r = (int32_t)(cur_w * 23u / 100u);
    int32_t cap = (int32_t)(cur_w / 2u) - 36;
    if (cap < 40) cap = 40;
    if (r > 280) r = 280;
    if (r > cap) r = cap;
    if (r < 110 && cap >= 110) r = 110;
    if (r > cap) r = cap;
    return r;
}

static int32_t __attribute__((unused)) her_orbit_radius_y(uint32_t cur_h)
{
    uint32_t percent = (cur_h < 650u) ? 13u : 21u;
    int32_t r = (int32_t)(cur_h * percent / 100u);
    int32_t cap = (int32_t)(cur_h / 2u) - 42;
    if (cap < 35) cap = 35;
    if (r > 175) r = 175;
    if (r > cap) r = cap;
    if (r < 64 && cap >= 64) r = 64;
    if (r > cap) r = cap;
    return r;
}

/* La ventana se limita al cuadrante exterior de la orbita. El nucleo y la
 * mayor parte de la trayectoria orbital quedan visibles incluso en SVGA. */
static void her_fit_window_to_orbit(uint32_t cur_w, uint32_t cur_h)
{
    int32_t min_w = (cur_w < 900u) ? 180 : ((cur_w < 1200u) ? 220 : HER_MIN_W);
    int32_t max_w = (int32_t)(cur_w / 2u) - her_orbit_radius_x(cur_w) - 60;
    int32_t avail_w = (int32_t)cur_w - 32;
    int32_t max_h = (int32_t)cur_h - 48;
    if (max_w < min_w) max_w = min_w;
    if (max_w > avail_w) max_w = avail_w;
    if (max_h < HER_MIN_H) max_h = HER_MIN_H;
    if (her_win_w < (uint32_t)min_w || her_win_w > (uint32_t)max_w)
        her_win_w = (uint32_t)max_w;
    if (her_win_h < HER_MIN_H || her_win_h > (uint32_t)max_h)
        her_win_h = (uint32_t)max_h;
}

/* Las aplicaciones se distribuyen por las cuatro esquinas segun su tipo. */
static void her_place_window_corner(uint32_t cur_w, uint32_t cur_h, int corner)
{
    her_fit_window_to_orbit(cur_w, cur_h);
    her_win_x = ((corner & 1) == 0 || cur_w <= her_win_w + 32u)
              ? 16u : cur_w - her_win_w - 16u;
    her_win_y = ((corner & 2) == 0 || cur_h <= her_win_h + 48u)
              ? 32u : cur_h - her_win_h - 16u;
}

/* Orbita eliptica amplia sin linea guia: una revolucion cada 60 segundos. */
/* SOMA_203_INSTANCIAS: cinco columnas tematicas, separadas del orbe.
 * 0: asistentes, 1: sistema/desarrollo, 2: trabajo,
 * 3: multimedia, 4: captura/red. Posiciones estaticas en el escritorio. */
static const uint8_t her_group_col[HER_DOCK_ITEM_COUNT] = {
    0,1,4,2,3,2,0,2,1,3,0,3,2,1,3,4,4,1
};
static const uint8_t her_group_row[HER_DOCK_ITEM_COUNT] = {
    0,0,2,0,0,1,2,2,1,1,1,2,3,2,3,1,0,3
};
/* SOMA_208_ESCRITORIO: disposicion del usuario. Cada icono guarda su posicion en milesimas de pantalla (+1; 0 = la del grupo) para
 * que sirva a cualquier resolucion. Se guarda en /etc/escritorio.conf. */
static int16_t  her_icon_ux[HER_DOCK_ITEM_COUNT], her_icon_uy[HER_DOCK_ITEM_COUNT];
static int      her_icon_custom_n;                 /* iconos movidos por el usuario (oculta los rotulos de grupo) */
static uint32_t her_desk_ver;                      /* sube con cada cambio; entra en la firma de la capa de fondo */
static int      her_dp_kind, her_dp_moved;         /* arrastre en curso: 0 nada, 1 icono, 2 gadget */
static int      her_idrag = -1;                    /* icono que se esta arrastrando (ya sobrepasado el umbral) */
static int32_t  her_idrag_x, her_idrag_y;
/* SOMA_218_CUADRICULA: rejilla virtual del escritorio. Es la misma de la disposicion por grupos: columnas cada `gap` px
 * contadas desde el margen izquierdo en la mitad izquierda y desde el derecho en la derecha, y filas cada `dy` px a partir
 * de y0 (hacia arriba y hacia abajo). Asi un icono soltado por el usuario queda alineado con los que siguen en su sitio. */
static void her_grid_params(uint32_t cur_w, uint32_t cur_h, int32_t *margin, int32_t *gap, int32_t *y0, int32_t *dy)
{
    *margin = cur_w < 960u ? 54 : 68;
    *gap = cur_w < 960u ? 77 : 96;
    *y0 = cur_h < 650u ? 165 : 172;
    int32_t usable = (int32_t)cur_h - *y0 - 164;
    if (usable < 114) usable = 114;
    *dy = usable / 3;
    if (*dy > 104) *dy = 104;
}
static void her_grid_snap(uint32_t cur_w, uint32_t cur_h, int32_t *x, int32_t *y)
{
    int32_t margin, gap, y0, dy, w = (int32_t)cur_w, h = (int32_t)cur_h;
    her_grid_params(cur_w, cur_h, &margin, &gap, &y0, &dy);
    int32_t bx = margin, bd = 0x7FFFFFFF, last_l = margin;
    for (int32_t c = 0; c < 64; ++c) {                                   /* columnas de la izquierda */
        int32_t cx = margin + c * gap;
        if (c && cx > w / 2) break;
        int32_t d = cx > *x ? cx - *x : *x - cx;
        if (d < bd) { bd = d; bx = cx; }
        last_l = cx;
    }
    for (int32_t k = 0; k < 64; ++k) {                                   /* columnas de la derecha */
        int32_t cx = w - margin - k * gap;
        if (cx <= w / 2 || cx - last_l < gap * 3 / 4) break;             /* en el centro no se solapan con las de la izquierda */
        int32_t d = cx > *x ? cx - *x : *x - cx;
        if (d < bd) { bd = d; bx = cx; }
    }
    if (dy < 8) dy = 8;
    int32_t r = (*y - y0 + dy / 2 + dy * 64) / dy - 64;
    int32_t ry = y0 + r * dy;
    while (ry < 36 && ry + dy <= h - 36) ry += dy;
    while (ry > h - 36 && ry - dy >= 36) ry -= dy;
    *x = bx; *y = ry;
}
static void her_icon_orbit_target(int i, uint32_t cur_w, uint32_t cur_h,
                                  uint64_t now_ms, int32_t *out_x, int32_t *out_y)
{
    (void)now_ms;
    if (i == her_idrag) { *out_x = her_idrag_x; *out_y = her_idrag_y; return; }
    if (her_icon_ux[i]) {
        *out_x = ((int32_t)her_icon_ux[i] - 1) * (int32_t)cur_w / 1000;
        *out_y = ((int32_t)her_icon_uy[i] - 1) * (int32_t)cur_h / 1000;
        her_grid_snap(cur_w, cur_h, out_x, out_y);                      /* tambien las disposiciones guardadas antes */
        return;
    }
    int32_t w = (int32_t)cur_w;
    int32_t col = her_group_col[i], row = her_group_row[i];
    int32_t margin, gap, y0, dy;
    her_grid_params(cur_w, cur_h, &margin, &gap, &y0, &dy);
    int32_t x = col < 2 ? margin + col * gap : w - margin - (4 - col) * gap;
    *out_x = x;
    *out_y = y0 + row * dy;
}

/* Destello corto de materia expulsada/absorbida. No usa arrays ni memoria
 * adicional; se dibuja solo durante los primeros 280 ms de cada transicion.
 */
static void her_draw_icon_burst(uint32_t cur_w, uint32_t cur_h, uint64_t now_ms)
{
    if (her_icons_burst_at == 0) return;
    uint64_t elapsed64 = (now_ms >= her_icons_burst_at) ? now_ms - her_icons_burst_at : 0;
    if (elapsed64 >= 140u) {
        her_icons_burst_at = 0;
        return;
    }

    uint32_t elapsed = (uint32_t)elapsed64;
    uint32_t travel = elapsed * 48u / 140u;
    uint32_t fade = (140u - elapsed) * 256u / 140u;
    int32_t cx = (int32_t)(cur_w / 2u), cy = (int32_t)(cur_h / 2u);
    uint32_t tint = her_scale(0x00DCEBFFu, fade);

    for (int i = 0; i < 12; ++i) {
        uint32_t a = (((uint32_t)i * 1024u) / 12u + elapsed / 8u) & 1023u;
        int32_t r0, r1;
        if (her_icons_burst_opening) {
            r0 = 23 + (int32_t)travel;
            r1 = r0 + 8;
        } else {
            r1 = 23 + (int32_t)(48u - travel);
            r0 = r1 + 8;
        }
        int32_t x0 = cx + (r0 * her_cs(a)) / 1024;
        int32_t y0 = cy + (r0 * her_sn(a)) / 1024;
        int32_t x1 = cx + (r1 * her_cs(a)) / 1024;
        int32_t y1 = cy + (r1 * her_sn(a)) / 1024;
        fb_draw_line(x0, y0, x1, y1, tint);
    }
}

/* Particulas discretas que viajan desde el centro y se condensan en el icono. */
static void her_draw_icon_materialization(int i, int32_t px, int32_t py,
                                          uint32_t progress, uint64_t now_ms,
                                          uint32_t cur_w, uint32_t cur_h)
{
    uint32_t tick = (uint32_t)(now_ms / 24u);
    int32_t spread = 3 + (int32_t)((HER_ICON_ANIM_MAX - progress) * 25u /
                                   HER_ICON_ANIM_MAX);
    uint32_t brightness = 76u + (progress * 170u) / HER_ICON_ANIM_MAX;
    uint32_t color = her_scale(0x00E7F3FFu, brightness);

    for (int k = 0; k < 8; ++k) {
        uint32_t a = ((uint32_t)i * 113u + (uint32_t)k * 149u +
                      tick * (9u + (uint32_t)(k & 3))) & 1023u;
        int32_t ring = 2 + (int32_t)(((uint32_t)k * 7u + tick +
                                     (uint32_t)i * 3u) % (uint32_t)spread);
        int32_t sx = px + (ring * her_cs(a)) / 1024;
        int32_t sy = py + (ring * her_sn(a)) / 1024;
        int32_t tail = 2 + (k % 3);
        int32_t ex = sx - (tail * her_cs(a)) / 1024;
        int32_t ey = sy - (tail * her_sn(a)) / 1024;

        if (sx >= 0 && sy >= 0 && sx < (int32_t)cur_w &&
            sy < (int32_t)cur_h) {
            fb_draw_line(sx, sy, ex, ey, color);
            fb_draw_pixel((uint32_t)sx, (uint32_t)sy, 0x00FFFFFFu);
        }
    }
}

/* SOMA VECTOR ICONS PATCH v1: iconos vectoriales suavizados, sin recuadro. */
#define HER_ICON_MAX_S 96

static uint32_t *her_icon_sprites = 0;
#define HER_ICON_SLOTS 3
static int       her_icon_slot_s[HER_ICON_SLOTS];
static int       her_icon_slot_next;
static uint8_t   her_icon_ready2[HER_ICON_SLOTS][HER_DOCK_ITEM_COUNT];

static int her_icon_canvas(uint32_t cur_h)
{
    return (cur_h < 650u) ? 64 : 88;   /* lado del sprite en pixeles */
}

/* Geometria en una rejilla de diseno de 64x64 unidades.
 * X, Y llegan en 1/16 de unidad; los parametros de forma, en unidades. */
static int32_t ic_abs(int32_t v) { return v < 0 ? -v : v; }

static int32_t ic_circ(int32_t X, int32_t Y, int cx, int cy, int r)
{
    int32_t dx = X - cx * 16, dy = Y - cy * 16;
    return (int32_t)her_isqrt((uint32_t)(dx * dx + dy * dy)) - r * 16;
}

static int32_t ic_rbox(int32_t X, int32_t Y, int cx, int cy, int hw, int hh, int r)
{
    int32_t dx = ic_abs(X - cx * 16), dy = ic_abs(Y - cy * 16);
    int32_t qx = dx - (hw - r) * 16, qy = dy - (hh - r) * 16;
    int32_t ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    int32_t inner = qx > qy ? qx : qy;
    if (inner > 0) inner = 0;
    return (int32_t)her_isqrt((uint32_t)(ox * ox + oy * oy)) + inner - r * 16;
}

/* Capsula entre (ax,ay) y (bx,by); hw16 = medio grosor en 1/16 de unidad. */
static int32_t ic_seg(int32_t X, int32_t Y, int ax, int ay, int bx, int by, int hw16)
{
    int32_t px = X - ax * 16, py = Y - ay * 16;
    int32_t vx = (bx - ax) * 16, vy = (by - ay) * 16;
    int32_t l2 = vx * vx + vy * vy;
    int32_t t = l2 ? (px * vx + py * vy) * 256 / l2 : 0;
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    int32_t hx = px - vx * t / 256, hy = py - vy * t / 256;
    return (int32_t)her_isqrt((uint32_t)(hx * hx + hy * hy)) - hw16;
}

static int32_t ic_ell(int32_t X, int32_t Y, int cx, int cy, int rx, int ry)
{
    int32_t dx = (X - cx * 16) * ry / rx, dy = Y - cy * 16;
    int32_t d = (int32_t)her_isqrt((uint32_t)(dx * dx + dy * dy)) - ry * 16;
    return d * rx / ry;
}

#define IC_RING(d, h) (ic_abs(d) - (h))
#define IC_M(v) do { int32_t _v = (v); if (_v < m) m = _v; } while (0)
#define IC_A(v) do { int32_t _v = (v); if (_v < a) a = _v; } while (0)

/* Distancia con signo al trazo principal (dm) y al acento (da). */
static void her_icon_dist(int icon, int32_t X, int32_t Y, int32_t *dm, int32_t *da)
{
    int32_t m = 1 << 20, a = 1 << 20;
    switch (icon) {
    case ICON_CODER:
        IC_M(ic_seg(X, Y, 22, 20, 14, 32, 22));
        IC_M(ic_seg(X, Y, 14, 32, 22, 44, 22));
        IC_M(ic_seg(X, Y, 42, 20, 50, 32, 22));
        IC_M(ic_seg(X, Y, 50, 32, 42, 44, 22));
        IC_A(ic_seg(X, Y, 35, 17, 29, 47, 20));
        break;
    case ICON_AGENT:
        IC_M(IC_RING(ic_circ(X, Y, 32, 32, 20), 24));
        IC_A(ic_circ(X, Y, 32, 32, 9));
        IC_M(ic_circ(X, Y, 46, 18, 3));
        break;
    case ICON_TERMINAL:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 32, 22, 17, 6), 24));
        IC_A(ic_seg(X, Y, 20, 26, 28, 32, 28));
        IC_A(ic_seg(X, Y, 28, 32, 20, 38, 28));
        IC_A(ic_seg(X, Y, 33, 39, 44, 39, 28));
        break;
    case ICON_BROWSER:
        IC_M(IC_RING(ic_circ(X, Y, 32, 32, 20), 24));
        IC_M(IC_RING(ic_ell(X, Y, 32, 32, 9, 20), 20));
        IC_A(ic_seg(X, Y, 12, 32, 52, 32, 20));
        break;
    case ICON_FILES:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 35, 22, 15, 5), 24));
        IC_A(ic_rbox(X, Y, 19, 19, 10, 4, 2));
        IC_A(ic_seg(X, Y, 22, 36, 42, 36, 20));
        break;
    case ICON_IMAGE:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 32, 22, 18, 6), 24));
        IC_A(ic_circ(X, Y, 43, 24, 4));
        IC_M(ic_seg(X, Y, 15, 44, 25, 33, 24));
        IC_M(ic_seg(X, Y, 25, 33, 33, 41, 24));
        IC_M(ic_seg(X, Y, 33, 41, 40, 35, 24));
        IC_M(ic_seg(X, Y, 40, 35, 49, 44, 24));
        break;
    case ICON_NOTES:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 32, 16, 22, 5), 24));
        IC_A(ic_seg(X, Y, 24, 23, 40, 23, 20));
        IC_A(ic_seg(X, Y, 24, 31, 40, 31, 20));
        IC_A(ic_seg(X, Y, 24, 39, 34, 39, 20));
        break;
    case ICON_MEMORY:
        IC_A(ic_circ(X, Y, 32, 17, 6));
        IC_M(ic_seg(X, Y, 32, 23, 16, 42, 20));
        IC_M(ic_seg(X, Y, 32, 23, 32, 42, 20));
        IC_M(ic_seg(X, Y, 32, 23, 48, 42, 20));
        IC_M(IC_RING(ic_circ(X, Y, 16, 46, 5), 20));
        IC_M(IC_RING(ic_circ(X, Y, 32, 46, 5), 20));
        IC_M(IC_RING(ic_circ(X, Y, 48, 46, 5), 20));
        break;
    case ICON_LAYOUT:
        IC_M(IC_RING(ic_rbox(X, Y, 21, 32, 10, 20, 3), 20));
        IC_M(IC_RING(ic_rbox(X, Y, 43, 23, 10, 10, 3), 20));
        IC_A(ic_rbox(X, Y, 43, 45, 10, 6, 3));
        break;
    case ICON_MEDIA:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 32, 24, 18, 6), 24));
        IC_A(ic_seg(X, Y, 27, 24, 27, 40, 28));
        IC_A(ic_seg(X, Y, 27, 24, 41, 32, 28));
        IC_A(ic_seg(X, Y, 27, 40, 41, 32, 28));
        IC_A(ic_circ(X, Y, 31, 32, 5));
        break;
    case ICON_PAINT:
        IC_M(IC_RING(ic_circ(X, Y, 30, 32, 18), 24));
        IC_A(ic_circ(X, Y, 24, 25, 4));
        IC_A(ic_circ(X, Y, 36, 23, 4));
        IC_A(ic_circ(X, Y, 42, 33, 4));
        IC_M(ic_circ(X, Y, 25, 40, 4));
        IC_A(ic_seg(X, Y, 44, 16, 52, 8, 20));
        break;
    case ICON_MIC:                                       /* SOMA_177_T480S: microfono de pie */
        IC_M(IC_RING(ic_rbox(X, Y, 32, 24, 8, 14, 8), 22));
        IC_A(ic_rbox(X, Y, 32, 22, 3, 7, 3));
        IC_M(ic_seg(X, Y, 19, 30, 19, 34, 20)); IC_M(ic_seg(X, Y, 45, 30, 45, 34, 20));
        IC_M(ic_seg(X, Y, 19, 34, 24, 42, 20)); IC_M(ic_seg(X, Y, 45, 34, 40, 42, 20));
        IC_M(ic_seg(X, Y, 24, 42, 40, 42, 20));
        IC_M(ic_seg(X, Y, 32, 42, 32, 52, 22)); IC_M(ic_seg(X, Y, 23, 52, 41, 52, 22));
        break;
    case ICON_CAM:                                       /* SOMA_178_T480S: camara con objetivo */
        IC_M(IC_RING(ic_rbox(X, Y, 32, 36, 22, 14, 6), 22));
        IC_M(ic_seg(X, Y, 22, 21, 42, 21, 20)); IC_M(ic_seg(X, Y, 22, 21, 19, 25, 18)); IC_M(ic_seg(X, Y, 42, 21, 45, 25, 18));
        IC_M(IC_RING(ic_circ(X, Y, 32, 36, 9), 22));
        IC_A(ic_circ(X, Y, 32, 36, 4));
        IC_A(ic_circ(X, Y, 47, 29, 2));
        break;
    case ICON_SCR:                                       /* SOMA_179_T480S: pantalla con marco de captura y punto de grabacion */
        IC_M(ic_seg(X, Y, 14, 22, 14, 14, 20)); IC_M(ic_seg(X, Y, 14, 14, 22, 14, 20)); IC_M(ic_seg(X, Y, 42, 14, 50, 14, 20)); IC_M(ic_seg(X, Y, 50, 14, 50, 22, 20));
        IC_M(ic_seg(X, Y, 14, 42, 14, 50, 20)); IC_M(ic_seg(X, Y, 14, 50, 22, 50, 20)); IC_M(ic_seg(X, Y, 42, 50, 50, 50, 20)); IC_M(ic_seg(X, Y, 50, 42, 50, 50, 20));
        IC_A(ic_circ(X, Y, 32, 32, 8));
        break;
    case ICON_MONITOR:                                   /* PARCHE 089: pantalla con una grafica */
        IC_M(ic_seg(X, Y, 12, 16, 52, 16, 20)); IC_M(ic_seg(X, Y, 52, 16, 52, 44, 20)); IC_M(ic_seg(X, Y, 52, 44, 12, 44, 20)); IC_M(ic_seg(X, Y, 12, 44, 12, 16, 20));
        IC_M(ic_seg(X, Y, 26, 52, 38, 52, 20)); IC_M(ic_seg(X, Y, 32, 44, 32, 52, 18));
        IC_A(ic_seg(X, Y, 17, 36, 25, 30, 18)); IC_A(ic_seg(X, Y, 25, 30, 31, 34, 18)); IC_A(ic_seg(X, Y, 31, 34, 39, 23, 18)); IC_A(ic_seg(X, Y, 39, 23, 47, 27, 18));
        break;
    case ICON_PDF:                                       /* PARCHE 088: hoja con esquina doblada y renglones */
        IC_M(ic_seg(X, Y, 20, 12, 38, 12, 22)); IC_M(ic_seg(X, Y, 20, 12, 20, 52, 22)); IC_M(ic_seg(X, Y, 20, 52, 44, 52, 22));
        IC_M(ic_seg(X, Y, 44, 52, 44, 20, 22)); IC_M(ic_seg(X, Y, 38, 12, 44, 20, 22)); IC_M(ic_seg(X, Y, 38, 12, 38, 20, 18)); IC_M(ic_seg(X, Y, 38, 20, 44, 20, 18));
        IC_A(ic_seg(X, Y, 26, 28, 38, 28, 16)); IC_A(ic_seg(X, Y, 26, 35, 38, 35, 16)); IC_A(ic_seg(X, Y, 26, 42, 34, 42, 16));
        break;
    case ICON_SETTINGS:                                  /* PARCHE 085: engranaje de 8 dientes */
        IC_M(IC_RING(ic_circ(X, Y, 32, 32, 15), 22));
        IC_M(ic_seg(X, Y, 32, 12, 32, 16, 34)); IC_M(ic_seg(X, Y, 32, 48, 32, 52, 34));
        IC_M(ic_seg(X, Y, 12, 32, 16, 32, 34)); IC_M(ic_seg(X, Y, 48, 32, 52, 32, 34));
        IC_M(ic_seg(X, Y, 18, 18, 21, 21, 34)); IC_M(ic_seg(X, Y, 43, 43, 46, 46, 34));
        IC_M(ic_seg(X, Y, 46, 18, 43, 21, 34)); IC_M(ic_seg(X, Y, 18, 46, 21, 43, 34));
        IC_A(ic_circ(X, Y, 32, 32, 6));
        break;
    case ICON_AGENTS:
    default:
        IC_M(ic_seg(X, Y, 32, 17, 15, 46, 18));
        IC_M(ic_seg(X, Y, 32, 17, 49, 46, 18));
        IC_M(ic_seg(X, Y, 15, 46, 49, 46, 18));
        IC_A(ic_circ(X, Y, 32, 17, 7));
        IC_A(ic_circ(X, Y, 15, 46, 7));
        IC_A(ic_circ(X, Y, 49, 46, 7));
        IC_M(ic_circ(X, Y, 32, 37, 3));
        break;
    }
    *dm = m;
    *da = a;
}

/* Composicion "over" con alfa 0..256 (a tambien en 0..256). */
static void ic_over(uint32_t *r, uint32_t *g, uint32_t *b, uint32_t *a,
                    uint32_t col, uint32_t c)
{
    if (!c) return;
    uint32_t ia = (*a * (256u - c)) >> 8;
    uint32_t na = ia + c;
    *r = (((col >> 16) & 255u) * c + *r * ia) / na;
    *g = (((col >> 8) & 255u) * c + *g * ia) / na;
    *b = ((col & 255u) * c + *b * ia) / na;
    *a = na;
}

static uint32_t ic_cov(int32_t d, int32_t pw)
{
    int32_t v = 128 - (d * 256) / pw;
    if (v < 0) v = 0;
    if (v > 256) v = 256;
    return (uint32_t)v;
}

static void her_icon_render(int icon, uint32_t col, int S, uint32_t *out)
{
    uint32_t acc = her_mix256(col, 0x00FFFFFFu, 170u);
    int32_t pw = 1024 / S;
    if (pw < 1) pw = 1;

    for (int j = 0; j < S; ++j) {
        int32_t Y = ((2 * j + 1) * 512) / S;
        for (int i = 0; i < S; ++i) {
            int32_t X = ((2 * i + 1) * 512) / S;
            int32_t dm, da;
            uint32_t r = 0, g = 0, b = 0, a = 0;
            her_icon_dist(icon, X, Y, &dm, &da);
            if (dm > 0 && dm < 112)
                ic_over(&r, &g, &b, &a, col, (uint32_t)((112 - dm) * 44 / 112));
            ic_over(&r, &g, &b, &a, col, ic_cov(dm, pw));
            ic_over(&r, &g, &b, &a, acc, ic_cov(da, pw));
            out[j * S + i] = ((a > 255u ? 255u : a) << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

static const uint32_t *her_icon_get(int i, int S)
{
    /* SOMA_208: 3 tamanos a la vez (dock, orbita, auxiliar). Antes habia uno solo y con un icono minimizado en orbita los 18
     * sprites se rasterizaban de nuevo en cada fotograma (109 ms medidos en QEMU). */
    if (S > HER_ICON_MAX_S) S = HER_ICON_MAX_S;
    if (!her_icon_sprites) {
        her_icon_sprites = (uint32_t *)kmalloc((size_t)HER_ICON_SLOTS * HER_DOCK_ITEM_COUNT *
                           HER_ICON_MAX_S * HER_ICON_MAX_S * sizeof(uint32_t));
        if (!her_icon_sprites) return 0;
        for (int z = 0; z < HER_ICON_SLOTS; ++z) { her_icon_slot_s[z] = 0; for (int k = 0; k < HER_DOCK_ITEM_COUNT; ++k) her_icon_ready2[z][k] = 0; }
    }
    int z = -1;
    for (int q = 0; q < HER_ICON_SLOTS; ++q) if (her_icon_slot_s[q] == S) { z = q; break; }
    if (z < 0) {
        z = her_icon_slot_next; her_icon_slot_next = (her_icon_slot_next + 1) % HER_ICON_SLOTS;
        her_icon_slot_s[z] = S;
        for (int k = 0; k < HER_DOCK_ITEM_COUNT; ++k) her_icon_ready2[z][k] = 0;
    }
    uint32_t *spr = her_icon_sprites + ((size_t)z * HER_DOCK_ITEM_COUNT + (size_t)i) * HER_ICON_MAX_S * HER_ICON_MAX_S;
    if (!her_icon_ready2[z][i]) {
        her_icon_render(her_dock_items[i].icon, her_dock_items[i].color, S, spr);
        her_icon_ready2[z][i] = 1;
    }
    return spr;
}

/* Copia el sprite (lado S) centrado, escalado a D px, con alfa global 0..256. */
static inline uint32_t icon_blend(uint32_t bg, uint32_t fg)      /* igual que fb_blend_color (mismo resultado), en linea */
{
    uint32_t a = (fg >> 24) & 0xFF;
    if (a == 0) return bg;
    if (a == 255) return fg | 0xFF000000;
    uint32_t inv = 255 - a, t;
    t = ((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv + 1; uint32_t r = (t + (t >> 8)) >> 8;
    t = ((fg >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * inv + 1;   uint32_t g = (t + (t >> 8)) >> 8;
    t = (fg & 0xFF) * a + (bg & 0xFF) * inv + 1;                 uint32_t b = (t + (t >> 8)) >> 8;
    return (r << 16) | (g << 8) | b | 0xFF000000;
}
/* SOMA_164C_T480S: los iconos del dock giran y se pegan escalados en cada fotograma (medido en el T480s: 5 ms, la parte
 * mas cara del escritorio tras repartir el orbe). Ahora: tablas de escalado (sin dos divisiones por pixel), la mezcla en
 * linea, y todos los iconos de un fotograma se pegan por franjas de filas entre los nucleos. Mismo resultado. */
#define ICON_Q_MAX 20
struct icon_q { int32_t x0, y0; const uint32_t *spr; int S, D; uint32_t bright; };
static struct icon_q icon_q[ICON_Q_MAX];
static int icon_qn;
static void icon_blit_rows(const struct icon_q *q, volatile uint32_t *buf, int32_t W, int32_t H, int32_t ya, int32_t yb)
{
    int D = q->D, S = q->S;
    uint16_t xs[HER_ICON_MAX_S];
    for (int x = 0; x < D; ++x) xs[x] = (uint16_t)(x * S / D);
    int y0 = ya - q->y0, y1 = yb - q->y0;
    if (y0 < 0) y0 = 0;
    if (y1 > D) y1 = D;
    for (int y = y0; y < y1; ++y) {
        int32_t py = q->y0 + y;
        if (py < 0 || py >= H) continue;
        const uint32_t *row = q->spr + (y * S / D) * S;
        volatile uint32_t *dst = buf + (uint32_t)py * (uint32_t)W;
        for (int x = 0; x < D; ++x) {
            int32_t px = q->x0 + x;
            if (px < 0 || px >= W) continue;
            uint32_t p = row[xs[x]];
            uint32_t al = ((p >> 24) * q->bright) >> 8;
            if (!al) continue;
            dst[px] = icon_blend(dst[px], (al << 24) | (p & 0x00FFFFFFu)) & 0x00FFFFFFu;
        }
    }
}
struct icon_job { volatile uint32_t *buf; int32_t W, H, ya, yb; };
static void icon_blit_part(int idx, int n, void *arg)
{
    struct icon_job *j = (struct icon_job *)arg;
    int32_t span = j->yb - j->ya;
    int32_t a = j->ya + (int32_t)((int64_t)span * idx / n), b = j->ya + (int32_t)((int64_t)span * (idx + 1) / n);
    for (int i = 0; i < icon_qn; i++) icon_blit_rows(&icon_q[i], j->buf, j->W, j->H, a, b);
}
static void icon_flush(void)
{
    if (!icon_qn) return;
    volatile uint32_t *buf = fb_get_buffer();
    int32_t W = (int32_t)fb_get_width(), H = (int32_t)fb_get_height();
    if (buf) {
        struct icon_job j = { buf, W, H, 0x7FFFFFFF, -0x7FFFFFFF };
        for (int i = 0; i < icon_qn; i++) { if (icon_q[i].y0 < j.ya) j.ya = icon_q[i].y0; if (icon_q[i].y0 + icon_q[i].D > j.yb) j.yb = icon_q[i].y0 + icon_q[i].D; }
        if (j.ya < 0) j.ya = 0;
        if (j.yb > H) j.yb = H;
        if (j.yb > j.ya) smp_parallel(icon_blit_part, &j);
    }
    icon_qn = 0;
}
/* Copia el sprite (lado S) centrado, escalado a D px, con alfa global 0..256 (en cola: se pega en icon_flush). */
static void her_icon_blit(int32_t cx, int32_t cy, const uint32_t *spr, int S, int D,
                          uint32_t bright)
{
    if (!fb_get_buffer() || !spr || D < 2) return;
    if (D > HER_ICON_MAX_S) D = HER_ICON_MAX_S;
    int32_t x0 = cx - D / 2, y0 = cy - D / 2;
    fb_damage(x0, y0, D, D);
    if (icon_qn >= ICON_Q_MAX) icon_flush();
    struct icon_q *q = &icon_q[icon_qn++];
    q->x0 = x0; q->y0 = y0; q->spr = spr; q->S = S; q->D = D; q->bright = bright;
}

/* SOMA_202_QEMU_LIGERO_V2: energia vectorial elegante y barata de dibujar.
 * Sustituye el panel anterior por dos medallones de trazos limpios, sin bitmaps
 * ni AA costoso. Se conserva la confirmacion con doble pulsacion. */
#define HER_POWER_W      86
#define HER_POWER_H      74
#define HER_POWER_GAP    10
#define HER_POWER_RIGHT  14
#define HER_POWER_BOTTOM 38
static int her_power_pending = 0;          /* 1 = apagar, 2 = reiniciar */
static uint64_t her_power_deadline = 0;

static int32_t her_power_x(uint32_t w, int action)
{
    return (int32_t)w - HER_POWER_RIGHT - 2 * HER_POWER_W - HER_POWER_GAP
           + (action - 1) * (HER_POWER_W + HER_POWER_GAP);
}

static int32_t her_power_y(uint32_t h)
{
    return (int32_t)h - HER_POWER_BOTTOM - HER_POWER_H;
}

static int her_power_hit(int32_t mx, int32_t my, uint32_t w, uint32_t h)
{
    if (!her_icons_visible || her_icons_anim_phase < HER_ICON_ANIM_MAX ||
        w < 240u || h < 200u) return 0;
    for (int action = 1; action <= 2; ++action) {
        int32_t x = her_power_x(w, action), y = her_power_y(h);
        if (mx >= x && mx < x + HER_POWER_W &&
            my >= y && my < y + HER_POWER_H) return action;
    }
    return 0;
}

static void her_power_press(int action)
{
    uint64_t now = timer_get_uptime_ms();
    if (action == her_power_pending && now < her_power_deadline) {
        her_power_pending = 0;
        if (action == 1) shell_system_poweroff();
        else             shell_system_reboot();
        return;
    }
    her_power_pending = action;
    her_power_deadline = now + 5000u;
}

/* SOMA_218_ENERGIA: tarjetas de apagar y reiniciar con los iconos vectoriales del sistema (sysicon), suavizadas y con estados
 * de reposo, raton encima y pendiente de confirmar (con la cuenta atras como barra). Mismas medidas y zonas de pulsacion. */
static void her_power_card(int32_t x, int32_t y, int action, int hover, int selected, uint32_t left_ms)
{
    const int32_t w = HER_POWER_W, h = HER_POWER_H;
    uint32_t accent = (action == 1) ? 0x00FB7185u : 0x0067E8F9u;
    uint32_t base = 0x000E1626u;
    uint32_t fill = selected ? si_mix(base, accent, 46) : hover ? 0x00152033u : base;
    uint32_t edge = selected ? accent : hover ? si_mix(0x002A3648u, accent, 140) : 0x00263246u;
    fb_draw_box_shadow(x, y, w, h, 16, 0, 5, 0, 14, 0x70000000u);
    fb_fill_rrect_aa(x, y, w, h, 16, 0xE6000000u | fill);
    fb_stroke_rrect_aa(x, y, w, h, 16, 0xFF000000u | edge);
    fb_fill_rect_alpha(x + 14, y + 1, w - 28, 1, 0x1CFFFFFFu);                        /* brillo del borde superior */

    int32_t cx = x + w / 2, cy = y + 29;
    uint32_t disc = si_mix(0x000A111Du, accent, selected ? 96u : hover ? 62u : 34u);
    fb_fill_rrect_aa(cx - 20, cy - 20, 40, 40, 20, 0xFF000000u | disc);
    fb_stroke_rrect_aa(cx - 20, cy - 20, 40, 40, 20, 0xFF000000u | si_mix(disc, accent, selected ? 220u : hover ? 170u : 110u));
    uint32_t ic = selected ? 0x00FFFFFFu : si_mix(accent, 0x00FFFFFFu, hover ? 110u : 50u);
    si_icon(action == 1 ? SI_POWER : SI_RESTART, cx, cy, 26, ic);

    uint32_t txt = selected ? 0x00FFFFFFu : hover ? 0x00F1F5F9u : 0x00B8C4D6u;
    si_text_c(x, y + 52, w, 16, action == 1 ? "Apagar" : "Reiniciar", 12, txt, 1);

    if (selected) {                                                                   /* cuenta atras de la confirmacion */
        int32_t bw = (w - 28) * (int32_t)(left_ms > 5000u ? 5000u : left_ms) / 5000;
        fb_fill_rrect_aa(x + 14, y + h - 6, w - 28, 3, 1, 0x40000000u | si_mix(base, accent, 80));
        if (bw > 0) fb_fill_rrect_aa(x + 14, y + h - 6, bw, 3, 1, 0xFF000000u | accent);
    }
}

static void her_draw_power_controls(uint32_t w, uint32_t h)
{
    if (!her_icons_visible || her_icons_anim_phase < 144u ||
        w < 240u || h < 200u) return;
    uint64_t now = timer_get_uptime_ms();
    if (her_power_pending && now >= her_power_deadline)
        her_power_pending = 0;
    int hov = (her_icons_anim_phase >= HER_ICON_ANIM_MAX) ? her_power_hit(g_mouse_x_s, g_mouse_y_s, w, h) : 0;

    for (int action = 1; action <= 2; ++action) {
        int32_t x = her_power_x(w, action), y = her_power_y(h);
        if (her_icons_anim_phase < HER_ICON_ANIM_MAX) {
            int32_t k = ((int32_t)her_icons_anim_phase - 144) * 256 / 112;
            if (k < 0) k = 0;
            if (k > 256) k = 256;
            int32_t ox = (int32_t)w / 2 - HER_POWER_W / 2;
            int32_t oy = (int32_t)h / 2 - HER_POWER_H / 2;
            x = ox + (x - ox) * k / 256;
            y = oy + (y - oy) * k / 256;
        }
        int selected = (her_power_pending == action);
        her_power_card(x, y, action, hov == action, selected, selected ? (uint32_t)(her_power_deadline - now) : 0u);
    }

    if (her_power_pending) {
        const char *msg = (her_power_pending == 1) ? "Pulsa otra vez para apagar" : "Pulsa otra vez para reiniciar";
        uint32_t accent = (her_power_pending == 1) ? 0x00FB7185u : 0x0067E8F9u;
        int32_t tw = si_text_w(msg, 12, 1) + 24, th = 26;
        int32_t right = her_power_x(w, 2) + HER_POWER_W;
        int32_t tx = right - tw, ty = her_power_y(h) - th - 10;
        if (tx < 4) tx = 4;
        fb_fill_rrect_aa(tx, ty, tw, th, 13, 0xE6000000u | si_mix(0x000E1626u, accent, 30));
        fb_stroke_rrect_aa(tx, ty, tw, th, 13, 0xFF000000u | si_mix(0x000E1626u, accent, 150));
        si_text(tx + 12, ty, th, msg, 12, 0x00F1F5F9u, 1);
    }
}


/* SOMA_202_QEMU_LIGERO_V2: gadget de estado simplificado para QEMU.
 * Diseno mas barato: sin rectangulos redondeados AA ni rellenos complejos.
 * El sondeo se hace como mucho una vez por segundo y el panel solo muestra
 * informacion real del sistema. */
#define HST_W      286
#define HST_H      116
#define HST_RIGHT   16
#define HST_TOP     16
struct hst_snapshot {
    int valid;
    uint64_t ms;
    char hora[9];
    char fecha[11];
    int bateria;  /* -1 desconocida; 0..100 conocida */
    int carga;    /* 0 bateria, 1 cargando, 2 con AC */
    int red;      /* 0 sin interfaz, 1 sin enlace, 2 con enlace sin IP,
                   * 3 enlace e IP, 4 estado no consultable */
    int volumen;  /* -1 sin controlador de audio */
};
static struct hst_snapshot hst;

static void hst_dos(char *p, unsigned n)
{
    p[0] = (char)('0' + (n / 10u) % 10u);
    p[1] = (char)('0' + (n % 10u));
}

static void hst_tres(char *p, unsigned n)
{
    p[0] = (char)('0' + (n / 100u) % 10u);
    p[1] = (char)('0' + (n / 10u) % 10u);
    p[2] = (char)('0' + (n % 10u));
}

static void hst_leer(void)
{
    uint64_t ms = timer_get_uptime_ms();
    if (hst.valid && ms >= hst.ms && ms - hst.ms < 1000u) return;
    hst.ms = ms;
    hst.valid = 1;

    {
        struct rtc_time t;
        rtc_get_datetime(&t);
        if (t.hour < 24u && t.min < 60u && t.sec < 60u) {
            hst_dos(hst.hora, t.hour); hst.hora[2] = ':';
            hst_dos(hst.hora + 3, t.min); hst.hora[5] = ':';
            hst_dos(hst.hora + 6, t.sec); hst.hora[8] = 0;
        } else {
            const char *s = "--:--:--";
            for (int i = 0; i < 9; ++i) hst.hora[i] = s[i];
        }
        if (t.day >= 1u && t.day <= 31u && t.month >= 1u && t.month <= 12u &&
            t.year >= 2000u && t.year <= 9999u) {
            hst_dos(hst.fecha, t.day); hst.fecha[2] = '/';
            hst_dos(hst.fecha + 3, t.month); hst.fecha[5] = '/';
            hst_dos(hst.fecha + 6, t.year / 100u);
            hst_dos(hst.fecha + 8, t.year % 100u);
            hst.fecha[10] = 0;
        } else {
            const char *s = "--/--/----";
            for (int i = 0; i < 11; ++i) hst.fecha[i] = s[i];
        }
    }

    hst.bateria = -1;
    hst.carga = 0;
#ifdef __x86_64__
    {
        struct acpi_power_state ps;
        acpi_power_state(&ps);
        if (ps.valid && ps.percent != 0xFFFFFFFFu && ps.percent <= 100u) {
            hst.bateria = (int)ps.percent;
            hst.carga = ps.charging ? 1 : (ps.ac == 1 ? 2 : 0);
        }
    }
#endif

    {
        const struct netif_ops *ni = netif_active();
        if (!ni) hst.red = 0;
        else if (!ni->link) hst.red = 4;
        else if (!ni->link()) hst.red = 1;
        else if ((net_ip[0] | net_ip[1] | net_ip[2] | net_ip[3]) == 0u) hst.red = 2;
        else hst.red = 3;
    }

    hst.volumen = -1;
#ifdef __x86_64__
    if (hda_ready()) {
        int v = hda_get_volume();
        if (v >= 0 && v <= 100) hst.volumen = v;
    }
#endif
}

static void hst_draw_battery_icon(int32_t x, int32_t y, uint32_t col, int pct)
{
    fb_draw_rect((uint32_t)x, (uint32_t)y, 20u, 10u, col);
    fb_fill_rect((uint32_t)(x + 20), (uint32_t)(y + 3), 2u, 4u, col);
    if (pct > 0) {
        uint32_t fill = (uint32_t)pct * 16u / 100u;
        if (!fill) fill = 1u;
        fb_fill_rect((uint32_t)(x + 2), (uint32_t)(y + 2), fill, 6u, col);
    }
}

static void hst_draw_net_icon(int32_t x, int32_t y, uint32_t col)
{
    fb_fill_rect((uint32_t)x,     (uint32_t)(y + 8), 3u, 3u, col);
    fb_fill_rect((uint32_t)(x+5), (uint32_t)(y + 6), 3u, 5u, col);
    fb_fill_rect((uint32_t)(x+10),(uint32_t)(y + 3), 3u, 8u, col);
    fb_fill_rect((uint32_t)(x+15),(uint32_t)y,       3u,11u, col);
}

static void hst_draw_audio_icon(int32_t x, int32_t y, uint32_t col)
{
    fb_fill_rect((uint32_t)x, (uint32_t)(y + 4), 4u, 6u, col);
    fb_draw_line(x + 4, y + 5, x + 10, y + 1, col);
    fb_draw_line(x + 10, y + 1, x + 10, y + 13, col);
    fb_draw_line(x + 10, y + 13, x + 4, y + 9, col);
    fb_draw_line(x + 14, y + 4, x + 17, y + 7, col);
    fb_draw_line(x + 17, y + 7, x + 14, y + 10, col);
}

static int16_t hst_ux, hst_uy;                    /* SOMA_208: posicion del usuario (milesimas +1; 0 = esquina superior derecha) */
static int     hst_gdrag; static int32_t hst_gx, hst_gy;
static void hst_pos(uint32_t ancho, uint32_t alto, int32_t *x, int32_t *y)
{
    if (hst_gdrag) { *x = hst_gx; *y = hst_gy; return; }
    if (hst_ux && ancho > (uint32_t)HST_W && alto > (uint32_t)HST_H) {
        *x = ((int32_t)hst_ux - 1) * ((int32_t)ancho - HST_W) / 1000;
        *y = ((int32_t)hst_uy - 1) * ((int32_t)alto - HST_H) / 1000;
        return;
    }
    *x = (int32_t)ancho - HST_W - HST_RIGHT; *y = HST_TOP;
}
static int hst_hit(int32_t mx, int32_t my, uint32_t ancho, uint32_t alto)
{
    if (!her_icons_visible || her_icons_anim_phase < HER_ICON_ANIM_MAX) return 0;
    int32_t x, y; hst_pos(ancho, alto, &x, &y);
    return mx >= x && mx < x + HST_W && my >= y && my < y + HST_H;
}
static void hst_dibujar(uint32_t ancho, uint32_t alto)
{
    if (!her_icons_visible || her_icons_anim_phase < 144u ||
        ancho < (uint32_t)(HST_W + 24) || alto < (uint32_t)(HST_H + 24)) return;
    hst_leer();

    {
        int32_t x, y; hst_pos(ancho, alto, &x, &y);
        if (her_icons_anim_phase < HER_ICON_ANIM_MAX) {
            int32_t k = ((int32_t)her_icons_anim_phase - 144) * 256 / 112;
            if (k < 0) k = 0;
            if (k > 256) k = 256;
            int32_t ox = (int32_t)ancho / 2 - HST_W / 2;
            int32_t oy = (int32_t)alto / 2 - HST_H / 2;
            x = ox + (x - ox) * k / 256;
            y = oy + (y - oy) * k / 256;
        }
        uint32_t bg = 0x00111B28u;
        uint32_t border = 0x0035495Eu;
        uint32_t title = 0x0099ACC0u;
        uint32_t text = 0x00EEF4FBu;
        uint32_t soft = 0x00D8E3EFu;
        uint32_t bc = (hst.bateria < 0) ? title :
                      (hst.bateria <= 15 && !hst.carga) ? 0x00FB7185u :
                      hst.carga ? 0x0086EFACu : 0x0067E8F9u;
        uint32_t nc = hst.red == 3 ? 0x0086EFACu :
                      (hst.red == 2 || hst.red == 4) ? 0x00FBBF24u : title;
        uint32_t vc = hst.volumen >= 0 ? 0x00C4B5FDu : title;

        fb_fill_rect((uint32_t)x, (uint32_t)y, HST_W, HST_H, bg);
        fb_draw_rect((uint32_t)x, (uint32_t)y, HST_W, HST_H, border);
        fb_fill_rect((uint32_t)x, (uint32_t)y, HST_W, 2u, 0x0067E8F9u);

        font_draw_string((uint32_t)(x + 12), (uint32_t)(y + 10), "SOMA / ESTADO", title, 1);
        font_draw_string((uint32_t)(x + 12), (uint32_t)(y + 30), hst.hora, text, 2);
        font_draw_string((uint32_t)(x + 172), (uint32_t)(y + 34), hst.fecha, soft, 1);
        fb_fill_rect((uint32_t)(x + 12), (uint32_t)(y + 58), HST_W - 24u, 1u, border);

        hst_draw_battery_icon(x + 12, y + 70, bc, hst.bateria >= 0 ? hst.bateria : 0);
        if (hst.bateria >= 0) {
            char s[] = "BATERIA 000%";
            hst_tres(s + 8, (unsigned)hst.bateria);
            font_draw_string((uint32_t)(x + 40), (uint32_t)(y + 71), s, bc, 1);
            if (hst.carga == 1) font_draw_string((uint32_t)(x + 140), (uint32_t)(y + 71), "CARGANDO", 0x0086EFACu, 1);
            else if (hst.carga == 2) font_draw_string((uint32_t)(x + 140), (uint32_t)(y + 71), "CON AC", 0x0086EFACu, 1);
        } else {
            font_draw_string((uint32_t)(x + 40), (uint32_t)(y + 71), "BATERIA NO DISPONIBLE", title, 1);
        }

        hst_draw_net_icon(x + 12, y + 89, nc);
        font_draw_string((uint32_t)(x + 40), (uint32_t)(y + 90),
                         hst.red == 3 ? "RED CON IP" :
                         hst.red == 2 ? "RED SIN IP" :
                         hst.red == 1 ? "SIN ENLACE" :
                         hst.red == 4 ? "RED N/D" :
                                        "SIN INTERFAZ", nc, 1);

        hst_draw_audio_icon(x + 150, y + 87, vc);
        if (hst.volumen >= 0) {
            char s[] = "AUDIO 000%";
            hst_tres(s + 6, (unsigned)hst.volumen);
            font_draw_string((uint32_t)(x + 176), (uint32_t)(y + 90), s, vc, 1);
        } else {
            font_draw_string((uint32_t)(x + 176), (uint32_t)(y + 90), "AUDIO N/D", vc, 1);
        }
    }
}


static int32_t ofx_radius(int32_t h);
/* SOMA_203_INSTANCIAS: solo las ventanas minimizadas orbitan el agujero negro.
 * Un icono se restaura con clic. Arrastrarlo al horizonte lo destruye. */
static void her_min_orbit_position(int i, uint32_t w, uint32_t h,
                                   uint64_t now, int32_t *x, int32_t *y)
{
    int32_t cx = (int32_t)w / 2, cy = (int32_t)h / 2;
    int32_t r = ofx_radius((int32_t)h) + 55;
    if (r < 90) r = 90;
    if (r > (int32_t)h / 2 - 40) r = (int32_t)h / 2 - 40;
    if (r < 45) r = 45;
    int32_t ry = r * 3 / 4;
    uint32_t a = ((uint32_t)(now % 28000u) * 1024u / 28000u + her_min_phase[i]) & 1023u;
    *x = cx + her_cs(a) * r / 1024;
    *y = cy + her_sn(a) * ry / 1024;
}
static int her_min_dock_index(enum app_mode app)
{
    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i)
        if (her_dock_items[i].app == app) return i;
    return -1;
}
static void her_draw_minimized_icons(uint32_t w, uint32_t h)
{
    if (!her_min_count || w < 240u || h < 240u) return;
    uint64_t now = timer_get_uptime_ms();
    int s = her_icon_canvas(h);
    if (s > 48) s = 48;
    for (int i = 0; i < her_min_count; ++i) {
        int32_t x, y;
        her_min_orbit_position(i, w, h, now, &x, &y);
        if (her_min_drag == i && mouse_left_held()) { x = her_min_drag_x; y = her_min_drag_y; }
        int id = her_min_dock_index(her_minimized[i].app);
        if (id < 0) id = 0; /* Aplicacion SOMA sin icono propio en el dock. */
        const uint32_t *spr = her_icon_get(id, s);
        uint32_t ik = 256u;
        if (her_mfly_on && (int)her_minimized[i].app == her_mfly_app && her_minimized[i].term_slot == her_mfly_slot) {
            ik = her_mfly_icon_k;                                          /* SOMA_223: aun en vuelo */
            if (ik < 8u) continue;
        }
        if (spr) her_icon_blit(x, y, spr, s, ik >= 256u ? s : s * (int)(192u + ik / 4u) / 256, ik);
        /* Insignia de instancia individual, p.ej. dos terminales. */
        uint32_t number = (uint32_t)(her_minimized[i].term_slot);
        if (number > 0u) {
            char b[3] = { (char)('0' + number / 10u), (char)('0' + number % 10u), 0 };
            font_draw_string((uint32_t)(x + 14), (uint32_t)(y - 18),
                             number < 10u ? b + 1 : b, 0x00E2E8F0u, 1);
        }
    }
    icon_flush();
}
static int her_min_hit(int32_t mx, int32_t my, uint32_t w, uint32_t h)
{
    if (!her_min_count || w < 240u || h < 240u) return -1;
    uint64_t now = timer_get_uptime_ms();
    for (int i = her_min_count - 1; i >= 0; --i) {
        int32_t x, y;
        her_min_orbit_position(i, w, h, now, &x, &y);
        if (mx >= x - 30 && mx <= x + 30 && my >= y - 30 && my <= y + 30) return i;
    }
    return -1;
}
static int her_min_drop_center(int32_t mx, int32_t my, uint32_t w, uint32_t h)
{
    int32_t dx = mx - (int32_t)w / 2, dy = my - (int32_t)h / 2;
    int32_t r = ofx_radius((int32_t)h);
    if (r < 30) r = 30;
    return dx * dx + dy * dy <= (r + 8) * (r + 8);
}

static int her_dock_hit(int32_t mx, int32_t my, uint32_t cur_w, uint32_t cur_h);
static int her_dock_hit_index(int32_t mx, int32_t my, uint32_t cur_w, uint32_t cur_h);
/* SOMA_206: el nombre solo aparece al pasar el raton por encima de un icono. */
static void her_tip_box(int32_t x, int32_t y, const char *t)
{
    int32_t n = 0; while (t[n]) ++n;
    int32_t bw = n * 8 + 14, bh = 20;
    int32_t bx = x - bw / 2; if (bx < 4) bx = 4;
    if (bx + bw > (int32_t)fb_get_width() - 4) bx = (int32_t)fb_get_width() - 4 - bw;
    if (y < 4) y = 4;
    fb_fill_rect((uint32_t)bx, (uint32_t)y, (uint32_t)bw, (uint32_t)bh, 0x000B1220u);
    fb_fill_rect((uint32_t)bx, (uint32_t)y, (uint32_t)bw, 1u, 0x0067E8F9u);
    fb_fill_rect((uint32_t)bx, (uint32_t)(y + bh - 1), (uint32_t)bw, 1u, 0x00334155u);
    fb_fill_rect((uint32_t)bx, (uint32_t)y, 1u, (uint32_t)bh, 0x00334155u);
    fb_fill_rect((uint32_t)(bx + bw - 1), (uint32_t)y, 1u, (uint32_t)bh, 0x00334155u);
    font_draw_string((uint32_t)(bx + 7), (uint32_t)(y + 6), t, 0x00E2F1FFu, 1);
}
static void her_draw_hover_tooltip(uint32_t cur_w, uint32_t cur_h, uint64_t now_ms)
{
    if (her_icons_anim_phase < HER_ICON_ANIM_MAX || (her_dp_kind && her_dp_moved)) return;
    int32_t mx = g_mouse_x_s, my = g_mouse_y_s;
    int app = her_dock_hit(mx, my, cur_w, cur_h);
    if (app >= 0) {
        for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) if ((int)her_dock_items[i].app == app) {
            int32_t tx, ty; her_icon_orbit_target(i, cur_w, cur_h, now_ms, &tx, &ty);
            her_tip_box(tx, ty + her_icon_canvas(cur_h) * 11 / 32 + 6, her_dock_items[i].label);
            break;
        }
    } else {
        int mi = her_min_hit(mx, my, cur_w, cur_h);
        if (mi >= 0) {
            int id = her_min_dock_index(her_minimized[mi].app);
            int32_t x, y; her_min_orbit_position(mi, cur_w, cur_h, now_ms, &x, &y);
            her_tip_box(x, y + 28, id >= 0 ? her_dock_items[id].label : "APP");
        }
    }
    if (her_hint_msg && now_ms < her_hint_until)
        her_tip_box((int32_t)cur_w / 2, (int32_t)cur_h / 2 + her_icon_canvas(cur_h) + 70, her_hint_msg);
}

static void her_draw_dock(uint32_t cur_w, uint32_t cur_h)
{
    uint64_t now_ms = timer_get_uptime_ms();
    her_draw_icon_burst(cur_w, cur_h, now_ms);
    if (her_icons_anim_phase < HER_ICON_ANIM_MAX && her_hint_msg && now_ms < her_hint_until)   /* SOMA_219_AVISO: los avisos tambien */
        her_tip_box((int32_t)cur_w / 2, (int32_t)cur_h / 2 + her_icon_canvas(cur_h) + 70, her_hint_msg);   /* sin los iconos desplegados */
    if (her_icons_anim_phase == 0u) return;

    int32_t cx = (int32_t)(cur_w / 2u);
    int32_t cy = (int32_t)(cur_h / 2u);
    int S = her_icon_canvas(cur_h);
    int32_t half_px = S * 11 / 32;

    if (her_idrag >= 0) {                                /* SOMA_218_CUADRICULA: puntos de la rejilla y la celda de destino */
        int32_t margin, gap, y0, dy, w = (int32_t)cur_w, h = (int32_t)cur_h;
        her_grid_params(cur_w, cur_h, &margin, &gap, &y0, &dy);
        int32_t sx = her_idrag_x, sy = her_idrag_y;
        her_grid_snap(cur_w, cur_h, &sx, &sy);
        int32_t ry = y0; while (ry - dy >= 36) ry -= dy;
        for (; ry <= h - 36; ry += dy) {
            int32_t last_l = margin;
            for (int32_t c = 0; c < 64; ++c) {
                int32_t gx = margin + c * gap; if (c && gx > w / 2) break;
                fb_fill_rrect_aa(gx - 2, ry - 2, 4, 4, 2, 0x50A5C8EBu); last_l = gx;
            }
            for (int32_t k = 0; k < 64; ++k) {
                int32_t gx = w - margin - k * gap; if (gx <= w / 2 || gx - last_l < gap * 3 / 4) break;
                fb_fill_rrect_aa(gx - 2, ry - 2, 4, 4, 2, 0x50A5C8EBu);
            }
        }
        int32_t bs = half_px * 2 + 16;
        fb_fill_rrect_aa(sx - bs / 2, sy - bs / 2, bs, bs, 16, 0x2867E8F9u);
        fb_stroke_rrect_aa(sx - bs / 2, sy - bs / 2, bs, bs, 16, 0xA067E8F9u);
    }

    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
        uint32_t p = her_icon_item_progress(i);
        if (p == 0u) continue;

        int32_t target_x, target_y;
        her_icon_orbit_target(i, cur_w, cur_h, now_ms, &target_x, &target_y);

        int32_t px = cx + ((target_x - cx) * (int32_t)p) / (int32_t)HER_ICON_ANIM_MAX;
        int32_t py = cy + ((target_y - cy) * (int32_t)p) / (int32_t)HER_ICON_ANIM_MAX;
        if (p < HER_ICON_ANIM_MAX) her_draw_icon_materialization(i, px, py, p, now_ms, cur_w, cur_h);

        /* Fase final: el icono crece y se enciende donde se condensan las particulas. */
        if (p < 200u) continue;
        uint32_t solid = p - 200u;                       /* 0..56 */
        uint32_t bright = 40u + (solid * 216u) / 56u;
        if (bright > 256u) bright = 256u;
        int D = S * (80 + (int)((solid * 176u) / 56u)) / 256;
        if (D > S) D = S;

        const uint32_t *spr = her_icon_get(i, S);
        if (spr) her_icon_blit(target_x, target_y, spr, S, D, bright);
    }
    icon_flush();                                        /* SOMA_164C: todos los iconos a la vez, repartidos; despues los textos */
    if (her_icons_anim_phase >= 240u && !her_icon_custom_n) {
        static const char *const labels[5] = { "ASISTENTES", "SISTEMA", "TRABAJO", "MEDIA", "CAPTURA" };
        static const int anchors[5] = { 0, 1, 3, 4, 16 };
        for (int g = 0; g < 5; ++g) {
            int32_t gx, gy;
            her_icon_orbit_target(anchors[g], cur_w, cur_h, now_ms, &gx, &gy);
            int32_t n = 0;
            while (labels[g][n]) n++;
            int32_t labelx = gx - n * 4;
            font_draw_string((uint32_t)(labelx > 0 ? labelx : 0), (uint32_t)(gy - 38), labels[g], 0x005B7691u, 1);
        }
    }
    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
        uint32_t p = her_icon_item_progress(i);
        if (p < 200u) continue;
        int32_t target_x, target_y;
        her_icon_orbit_target(i, cur_w, cur_h, now_ms, &target_x, &target_y);
        if (p >= HER_ICON_ANIM_MAX) {
            int32_t label_y = target_y + half_px + 4;

            /* Indicador de app abierta: punto brillante bajo la etiqueta. */
            int is_open = (her_find_window(her_dock_items[i].app) >= 0);
            if (is_open) {
                uint32_t dot_col = (current_app == her_dock_items[i].app) ? 0x00E2F1FFu : 0x0064748Bu;
                fb_fill_circle(target_x, label_y + 16, (current_app == her_dock_items[i].app) ? 3 : 2, dot_col);
            }
        }
    }
    her_draw_hover_tooltip(cur_w, cur_h, now_ms);
    her_draw_power_controls(cur_w, cur_h);
}

static int her_dock_hit_index(int32_t mx, int32_t my, uint32_t cur_w, uint32_t cur_h)
{
    if (!her_icons_visible) return -1;
    uint64_t now_ms = timer_get_uptime_ms();
    const int32_t half = her_icon_canvas(cur_h) * 11 / 32;

    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
        uint32_t p = her_icon_item_progress(i);
        if (p < 232u) continue;

        int32_t target_x, target_y;
        her_icon_orbit_target(i, cur_w, cur_h, now_ms, &target_x, &target_y);
        if (mx >= target_x - half - 6 && mx <= target_x + half + 6 &&
            my >= target_y - half - 6 && my <= target_y + half + 14)
            return i;
    }
    return -1;
}
static int her_dock_hit(int32_t mx, int32_t my, uint32_t cur_w, uint32_t cur_h)
{
    int i = her_dock_hit_index(mx, my, cur_w, cur_h);
    return i < 0 ? -1 : (int)her_dock_items[i].app;
}

/* SOMA_208_ESCRITORIO: arrastrar iconos y gadget. Un toque sin mover (umbral 8 px) sigue abriendo la aplicacion. */
static void her_desk_save(void)
{
    char b[640]; int n = 0;
    for (int i = 0; i < HER_DOCK_ITEM_COUNT && n < 560; ++i) if (her_icon_ux[i]) {
        for (const char *t = her_dock_items[i].label; *t && n < 560; ++t) b[n++] = *t;
        int v[2] = { her_icon_ux[i] - 1, her_icon_uy[i] - 1 };
        for (int q = 0; q < 2; ++q) {
            b[n++] = ' '; char d[6]; int m = 0; int x = v[q]; if (x < 0) x = 0;
            do { d[m++] = (char)('0' + x % 10); x /= 10; } while (x && m < 5);
            while (m) b[n++] = d[--m];
        }
        b[n++] = '\n';
    }
    if (hst_ux && n < 600) {
        const char *t = "GADGET"; while (*t) b[n++] = *t++;
        int v[2] = { hst_ux - 1, hst_uy - 1 };
        for (int q = 0; q < 2; ++q) {
            b[n++] = ' '; char d[6]; int m = 0; int x = v[q]; if (x < 0) x = 0;
            do { d[m++] = (char)('0' + x % 10); x /= 10; } while (x && m < 5);
            while (m) b[n++] = d[--m];
        }
        b[n++] = '\n';
    }
    (void)vfs_write("/etc/escritorio.conf", b, (uint32_t)n);
}
static void her_desk_count(void)
{
    her_icon_custom_n = 0;
    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) if (her_icon_ux[i]) her_icon_custom_n++;
    her_desk_ver++;
}
static void her_desk_load(void)
{
    char b[640]; int n = vfs_read("/etc/escritorio.conf", b, sizeof b - 1);
    if (n <= 0) return;
    b[n] = 0;
    for (char *l = b; *l; ) {
        char *e = l; while (*e && *e != '\n') ++e;
        char sv = *e; *e = 0;
        char *sp = l; while (*sp && *sp != ' ') ++sp;
        if (*sp) {
            *sp = 0; int v[2] = { 0, 0 }; char *q = sp + 1;
            for (int k = 0; k < 2; ++k) { while (*q == ' ') ++q; while (*q >= '0' && *q <= '9') { v[k] = v[k] * 10 + (*q - '0'); ++q; } }
            if (v[0] > 1000) v[0] = 1000;
            if (v[1] > 1000) v[1] = 1000;
            if (l[0] == 'G' && l[1] == 'A' && l[2] == 'D') { hst_ux = (int16_t)(v[0] + 1); hst_uy = (int16_t)(v[1] + 1); }
            else for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
                const char *a = her_dock_items[i].label; const char *c = l;
                while (*a && *a == *c) { ++a; ++c; }
                if (!*a && !*c) { her_icon_ux[i] = (int16_t)(v[0] + 1); her_icon_uy[i] = (int16_t)(v[1] + 1); break; }
            }
        }
        if (!sv) break;
        l = e + 1;
    }
    her_desk_count();
}
void gui_desktop_layout_reset(void)
{
    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) her_icon_ux[i] = her_icon_uy[i] = 0;
    hst_ux = hst_uy = 0;
    her_desk_count(); her_desk_save();
}
static int     her_dp_idx = -1;
static int32_t her_dp_sx, her_dp_sy, her_dp_ox, her_dp_oy;
static int her_dp_begin(int32_t mx, int32_t my, uint32_t w, uint32_t h)
{
    uint64_t now = timer_get_uptime_ms();
    int i = her_dock_hit_index(mx, my, w, h);
    if (i >= 0) {
        int32_t tx, ty; her_icon_orbit_target(i, w, h, now, &tx, &ty);
        her_dp_kind = 1; her_dp_idx = i; her_dp_moved = 0; her_dp_sx = mx; her_dp_sy = my; her_dp_ox = tx - mx; her_dp_oy = ty - my;
        return 1;
    }
    if (hst_hit(mx, my, w, h)) {
        int32_t gx, gy; hst_pos(w, h, &gx, &gy);
        her_dp_kind = 2; her_dp_idx = -1; her_dp_moved = 0; her_dp_sx = mx; her_dp_sy = my; her_dp_ox = gx - mx; her_dp_oy = gy - my;
        return 1;
    }
    return 0;
}
/* Devuelve 1 si hay que repintar. */
static int her_dp_motion(int32_t mx, int32_t my, uint32_t w, uint32_t h)
{
    if (!her_dp_kind) return 0;
    if (!her_dp_moved) {
        int32_t dx = mx - her_dp_sx, dy = my - her_dp_sy;
        if (dx * dx + dy * dy < 64) return 0;
        her_dp_moved = 1;
    }
    if (her_dp_kind == 1) {
        int32_t x = mx + her_dp_ox, y = my + her_dp_oy;
        if (x < 36) x = 36;
        if (x > (int32_t)w - 36) x = (int32_t)w - 36;
        if (y < 36) y = 36;
        if (y > (int32_t)h - 36) y = (int32_t)h - 36;
        her_idrag = her_dp_idx; her_idrag_x = x; her_idrag_y = y;
    } else {
        int32_t x = mx + her_dp_ox, y = my + her_dp_oy;
        if (x < 0) x = 0;
        if (x > (int32_t)w - HST_W) x = (int32_t)w - HST_W;
        if (y < 0) y = 0;
        if (y > (int32_t)h - HST_H) y = (int32_t)h - HST_H;
        hst_gdrag = 1; hst_gx = x; hst_gy = y;
    }
    return 1;
}
/* Al soltar. Devuelve la aplicacion a abrir si fue un toque en un icono, o -1. */
static int her_dp_end(uint32_t w, uint32_t h)
{
    int kind = her_dp_kind, idx = her_dp_idx, moved = her_dp_moved;
    her_dp_kind = 0; her_dp_idx = -1; her_dp_moved = 0;
    if (kind == 1) {
        int32_t fx = her_idrag_x, fy = her_idrag_y;
        her_idrag = -1;
        if (!moved) return (int)her_dock_items[idx].app;
        her_grid_snap(w, h, &fx, &fy);                                          /* SOMA_218_CUADRICULA: a la celda mas cercana */
        uint64_t now = timer_get_uptime_ms();
        int32_t ox, oy; her_icon_orbit_target(idx, w, h, now, &ox, &oy);       /* donde estaba */
        for (int j = 0; j < HER_DOCK_ITEM_COUNT; ++j) {                         /* celda ocupada = intercambiar */
            if (j == idx) continue;
            int32_t tx, ty; her_icon_orbit_target(j, w, h, now, &tx, &ty);
            if (tx - fx < 24 && fx - tx < 24 && ty - fy < 24 && fy - ty < 24) {
                her_icon_ux[j] = (int16_t)((ox * 1000 + (int32_t)w / 2) / (int32_t)w + 1); her_icon_uy[j] = (int16_t)((oy * 1000 + (int32_t)h / 2) / (int32_t)h + 1);
                break;
            }
        }
        her_icon_ux[idx] = (int16_t)((fx * 1000 + (int32_t)w / 2) / (int32_t)w + 1); her_icon_uy[idx] = (int16_t)((fy * 1000 + (int32_t)h / 2) / (int32_t)h + 1);
        her_desk_count(); her_desk_save();
    } else if (kind == 2) {
        if (moved && w > (uint32_t)HST_W && h > (uint32_t)HST_H) {
            hst_gdrag = 0;
            hst_ux = (int16_t)(hst_gx * 1000 / ((int32_t)w - HST_W) + 1); hst_uy = (int16_t)(hst_gy * 1000 / ((int32_t)h - HST_H) + 1);
            her_desk_ver++; her_desk_save();
        }
        hst_gdrag = 0;
    }
    return -1;
}

/* SOMA_PARCHE_028_UI: icono vectorial de la aplicacion para la barra de titulo (-1 = ninguno) */
static int her_app_vicon(enum app_mode app)
{
    switch (app) {
    case APP_AGENT_CHAT:     return VICON_AGENT;
    case APP_TERMINAL_SHELL: return VICON_TERMINAL;
    case APP_WEB_BROWSER:    return VICON_BROWSER;
    case APP_FILE_MANAGER:   return VICON_FILES;
    case APP_IMAGE_VIEWER:   return VICON_IMAGE;
    case APP_NOTEPAD:        return VICON_NOTES;
    case APP_AGENT_MEMORY:   return VICON_MEMORY;
    case APP_LAYOUT:         return VICON_LAYOUT;
    case APP_AGENTS:         return VICON_AGENTS;
    case APP_PAINT:          return VICON_PAINT;
    default:                 return -1;
    }
}

static uint32_t her_win_border_color(int is_active) { return is_active ? 0xFF3B82B4u : 0xFF2A3446u; }
static int her_in_refresh = 0;                       /* her_refresh_all dibuja el marco de cada ventana por su cuenta */
static struct fb_cguard her_cguards[HER_MAX_WINDOWS];

/* Sombra o halo exterior: se dibuja antes de guardar el fondo de las esquinas */
static void her_draw_window_shadow(struct her_win_node *win, int is_active)
{
    if (!win) return;
    if (is_active) {
        fb_draw_shadow((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h, HER_WIN_R, 22, 8, 0xA0000000u);
        fb_draw_shadow((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h, HER_WIN_R, 14, 0, 0x2638BDF8u);
    } else {
        fb_draw_shadow((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h, HER_WIN_R, 14, 5, 0x80000000u);
    }
}

static void her_draw_one_window_chrome(struct her_win_node *win, int is_active)
{
    if (!win) return;
    int32_t x = (int32_t)win->x, y = (int32_t)win->y, w = (int32_t)win->w, h = (int32_t)win->h;
    uint32_t title_top  = is_active ? 0x00222E44 : 0x00141B2A;
    uint32_t title_bot  = is_active ? 0x001A2334 : 0x00111724;
    uint32_t title_txt  = is_active ? 0x00F1F5F9 : 0x008391A7;
    uint32_t body_bg    = 0x000B1220;

    /* cuerpo y barra de titulo (solo las esquinas superiores quedan a cargo de este dibujo; las inferiores, de la guarda) */
    fb_fill_rounded_rect(x, y, w, h, HER_WIN_R, body_bg);
    fb_fill_rounded_rect(x, y, w, HER_TITLE_H + HER_WIN_R, HER_WIN_R, title_top);
    fb_fill_rect((uint32_t)x, (uint32_t)(y + HER_TITLE_H), (uint32_t)w, HER_WIN_R, body_bg);
    fb_gradient_v(x + 1, y + HER_WIN_R, w - 2, HER_TITLE_H - HER_WIN_R, title_top, title_bot);
    fb_fill_rect((uint32_t)(x + 1), (uint32_t)(y + HER_TITLE_H - 1), (uint32_t)(w - 2), 1, is_active ? 0x000F1624 : 0x000C111C);
    fb_stroke_rrect_aa(x, y, w, h, HER_WIN_R, her_win_border_color(is_active));

    const char *title = "SOMA";
    switch (win->app) {
        case APP_AGENT_CHAT:     title = "Agente SOMA"; break;
        case APP_TERMINAL_SHELL: title = "Terminal"; break;
        case APP_WEB_BROWSER:    title = "Navegador"; break;
        case APP_NOTEPAD: case APP_IMAGE_VIEWER: case APP_CODER_APP: case APP_PAINT: case APP_FILE_MANAGER: case APP_PDF: {                /* SOMA_211_NOTAS / SOMA_212: el archivo de esa ventana */
            static char nt[80]; const char *fn = her_inst_name(win->app, win->term_slot); uint32_t q = 0;
            const char *a = her_app_base_title(win->app); while (*a && q < sizeof nt - 1) nt[q++] = *a++;
            if (fn[0]) { a = "  -  "; while (*a && q < sizeof nt - 1) nt[q++] = *a++; a = fn; while (*a && q < sizeof nt - 1) nt[q++] = *a++; }
            nt[q] = 0; title = nt; break; }
        case APP_AGENT_MEMORY:   title = "Memoria"; break;
        case APP_LAYOUT:         title = "Maquetador"; break;
        case APP_AGENTS:         title = "Agentes"; break;
        case APP_MEDIA:          title = "SOMA Media"; break;
        case APP_SETTINGS:       title = "Ajustes"; break;
        case APP_SYSMON:         title = "Monitor del sistema"; break;
        case APP_RECORDER:       title = "Grabadora"; break;
        case APP_CAMERA:         title = "Camara"; break;                     /* SOMA_178_T480S */
        case APP_CAPTURE:        title = "Captura"; break;                    /* SOMA_179_T480S */
        case APP_SOMA_APP:       title = gui_soma_win_get_title(); break; /*PL12_CHROME_TITLE*/
        default: break;
    }
    int32_t tx0 = x + 12;
    int vi = her_app_vicon(win->app);
    if (vi >= 0) { vicon_draw(vi, x + 10, y + (HER_TITLE_H - 16) / 2, 16, is_active ? 0x0038BDF8 : 0x0055657C); tx0 = x + 33; }
    fb_set_clip(x + 4, y, w > 44 ? w - 44 : 1, HER_TITLE_H);
    if (wf_init()) {
        uint32_t n = 0; while (title && title[n]) n++;
        wf_draw(tx0, y + (HER_TITLE_H - 13) / 2 + wf_ascent(13) - 1, title, n, 13, title_txt, 0, 255);
    } else {
        font_draw_string((uint32_t)tx0, (uint32_t)(y + 9), title, title_txt, 1);
    }
    fb_reset_clip();

    /* boton de cierre (misma zona activa de siempre: 24 px a la derecha) */
    {
        int32_t cx16 = (x + w - 16) * 16, cy16 = (y + HER_TITLE_H / 2) * 16;
        fb_fill_rrect_aa(x + w - 26, y + HER_TITLE_H / 2 - 10, 20, 20, 10, is_active ? 0xFFE5484Du : 0xFF2A3446u);
        uint32_t xc = is_active ? 0xFFFFFFFFu : 0xFF7C8AA0u;
        fb_line_aa(cx16 - 56, cy16 - 56, cx16 + 56, cy16 + 56, 24, xc);
        fb_line_aa(cx16 - 56, cy16 + 56, cx16 + 56, cy16 - 56, 24, xc);
    }
}

static void her_draw_window_chrome(uint32_t cur_w, uint32_t cur_h)
{
    (void)cur_w; (void)cur_h;
    if (her_in_refresh) return;
    for (int i = 0; i < her_num_windows; ++i) {
        her_draw_window_shadow(&her_windows[i], (i == her_num_windows - 1));
        her_draw_one_window_chrome(&her_windows[i], (i == her_num_windows - 1));
    }
}

/* PARCHE 085: puntero de redimension segun el borde (1 izq, 2 der, 4 arriba, 8 abajo) */
static int her_resize_cursor(int edge)
{
    if (!edge) return -1;
    int h = edge & 3, v = edge & 12;
    if (h && v) return ((edge & 1) && (edge & 4)) || ((edge & 2) && (edge & 8)) ? CURSOR_RESIZE_NWSE : CURSOR_RESIZE_NESW;
    return h ? CURSOR_RESIZE_H : CURSOR_RESIZE_V;
}
static int her_is_resize_cursor(int c) { return c == CURSOR_CROSSHAIR || c == CURSOR_RESIZE_H || c == CURSOR_RESIZE_V || c == CURSOR_RESIZE_NWSE || c == CURSOR_RESIZE_NESW; }

static int her_win_hit_target(struct her_win_node *win, int32_t mx, int32_t my, int *out_edge)
{
    *out_edge = 0;
    if (!win) return 0;
    const int32_t OUT = 8, IN = 5, CORNER = 14;
    int32_t x = (int32_t)win->x, y = (int32_t)win->y;
    int32_t w = (int32_t)win->w, h = (int32_t)win->h;

    if (mx < x - OUT || mx > x + w + OUT || my < y - OUT || my > y + h + OUT)
        return 0;

    /* Boton cerrar */
    if (mx >= x + w - 28 && mx <= x + w - 4 && my >= y + 2 && my <= y + HER_TITLE_H - 2)
        return 3;

    /* Tirador de esquina inferior derecha */
    if (mx >= x + w - 18 && my >= y + h - 18) { *out_edge = 2 | 8; return 2; }

    int edge = 0;
    if (mx <= x + IN)     edge |= 1;
    if (mx >= x + w - IN) edge |= 2;
    if (my <= y + IN)     edge |= 4;
    if (my >= y + h - IN) edge |= 8;
    /* Esquinas: zona ampliada para poder redimensionar en diagonal */
    if (edge & 3)  { if (my <= y + CORNER) edge |= 4; else if (my >= y + h - CORNER) edge |= 8; }
    if (edge & 12) { if (mx <= x + CORNER) edge |= 1; else if (mx >= x + w - CORNER) edge |= 2; }
    if (edge) { *out_edge = edge; return 2; }

    if (my >= y && my < y + HER_TITLE_H) return 1;
    return 4;
}

static int __attribute__((unused)) her_win_hit(int32_t mx, int32_t my, int *out_edge)
{
    *out_edge = 0;
    struct her_win_node *win = her_get_active_win();
    if (!win) return 0;
    return her_win_hit_target(win, mx, my, out_edge);
}

static void her_win_apply_motion(int32_t mx, int32_t my, uint32_t cur_w, uint32_t cur_h)
{
    struct her_win_node *win = (her_num_windows > 0) ? &her_windows[her_num_windows - 1] : 0;
    if (!win) return;
    int32_t max_w = (int32_t)cur_w - 20;
    int32_t max_h = (int32_t)cur_h - 20;
    int32_t min_w = (int32_t)her_app_min_w(win->app);
    int32_t min_h = (cur_h < 650u) ? HER_MIN_H : 260;
    if (min_w > max_w) min_w = max_w;
    if (min_h > max_h) min_h = max_h;

    if (her_dragging) {
        int32_t nx = mx - her_drag_ox;
        int32_t ny = my - her_drag_oy;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx + (int32_t)win->w > (int32_t)cur_w) nx = (int32_t)cur_w - (int32_t)win->w;
        if (ny + (int32_t)win->h > (int32_t)cur_h) ny = (int32_t)cur_h - (int32_t)win->h;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        win->x = (uint32_t)nx;
        win->y = (uint32_t)ny;
        her_win_x = win->x; her_win_y = win->y;
    } else if (her_resizing) {
        int32_t dx = mx - her_resize_start_mx;
        int32_t dy = my - her_resize_start_my;
        int32_t new_x = her_resize_start_x;
        int32_t new_y = her_resize_start_y;
        int32_t new_w = her_resize_start_w;
        int32_t new_h = her_resize_start_h;

        /* Borde derecho */
        if (her_resize_edge & 2) {
            new_w = her_resize_start_w + dx;
            if (new_w < min_w) new_w = min_w;
            if (new_x + new_w > (int32_t)cur_w) new_w = (int32_t)cur_w - new_x;
        }
        /* Borde izquierdo */
        if (her_resize_edge & 1) {
            new_w = her_resize_start_w - dx;
            new_x = her_resize_start_x + dx;
            if (new_w < min_w) {
                new_x = her_resize_start_x + (her_resize_start_w - min_w);
                new_w = min_w;
            }
            if (new_x < 0) {
                new_w += new_x;
                new_x = 0;
            }
        }
        /* Borde inferior */
        if (her_resize_edge & 8) {
            new_h = her_resize_start_h + dy;
            if (new_h < min_h) new_h = min_h;
            if (new_y + new_h > (int32_t)cur_h) new_h = (int32_t)cur_h - new_y;
        }
        /* Borde superior */
        if (her_resize_edge & 4) {
            new_h = her_resize_start_h - dy;
            new_y = her_resize_start_y + dy;
            if (new_h < min_h) {
                new_y = her_resize_start_y + (her_resize_start_h - min_h);
                new_h = min_h;
            }
            if (new_y < 0) {
                new_h += new_y;
                new_y = 0;
            }
        }

        if (new_w < min_w) new_w = min_w;
        if (new_h < min_h) new_h = min_h;
        if (new_w > max_w) new_w = max_w;
        if (new_h > max_h) new_h = max_h;

        win->x = (uint32_t)new_x;
        win->y = (uint32_t)new_y;
        win->w = (uint32_t)new_w;
        win->h = (uint32_t)new_h;

        her_win_x = win->x; her_win_y = win->y;
        her_win_w = win->w; her_win_h = win->h;
    }
}

static char     cur_cmd[256];
static uint32_t cur_cmd_len = 0;
static uint32_t cur_cursor_pos = 0;
static volatile int her_anim_alive = 0;

static void render_desktop(void);
static void her_refresh_all(void);
static void her_vortex_close_step(void);
static void her_draw_vortex_particles(void);
static int her_cursor_should_show(void);
static void her_anim_start(void);
__attribute__((unused)) static void her_anim_halt(void);
static void her_anim_stop(void);

#define HER_SYNC_WIN() do {                                              \
        win_x = HER_CLI_X(her_win_x);                                    \
        win_y = HER_CLI_Y(her_win_y);                                    \
        win_w = HER_CLI_W(her_win_w);                                    \
        win_h = HER_CLI_H(her_win_h);                                    \
        g_live_win_x = win_x; g_live_win_y = win_y;                      \
        g_live_win_w = win_w; g_live_win_h = win_h;                      \
    } while (0)

static void log_add_raw(struct gui_chat_line *lines, int *count, const char *msg, uint32_t color) {
    ensure_agent_log();
    if (!lines) lines = agent_log;
    if (!lines) return;
    if (*count >= GUI_MAX_LOG_LINES) { for (int i=0; i<GUI_MAX_LOG_LINES-1; ++i) lines[i] = lines[i+1]; *count = GUI_MAX_LOG_LINES-1; }
    uint32_t p = 0; while (*msg && p < GUI_LINE_MAX_LEN-1) lines[*count].text[p++] = *msg++;
    lines[*count].text[p] = '\0'; lines[*count].color = color; (*count)++;
}

static void log_add(struct gui_chat_line *lines, int *count, const char *prefix, const char *msg, uint32_t color, uint32_t max_chars) {
    if (max_chars == 0 || max_chars > GUI_LINE_MAX_LEN - 4) max_chars = GUI_LINE_MAX_LEN - 4;
    char line_buf[GUI_LINE_MAX_LEN]; uint32_t lp = 0;
    if (prefix) while (*prefix && lp < max_chars-1) line_buf[lp++] = *prefix++;
    while (*msg) {
        if (*msg == '\n' || *msg == '\r') { line_buf[lp] = '\0'; log_add_raw(lines, count, line_buf, color); lp = 0; line_buf[lp++]=' '; line_buf[lp++]=' '; msg++; continue; }
        line_buf[lp++] = *msg++;
        if (lp >= max_chars - 2) { line_buf[lp] = '\0'; log_add_raw(lines, count, line_buf, color); lp = 0; line_buf[lp++]=' '; line_buf[lp++]=' '; }
    }
    if (lp > 2 || (prefix && lp > 0)) { line_buf[lp] = '\0'; log_add_raw(lines, count, line_buf, color); }
}

/* SOMA_PARCHE_024_TERM: el emulador de terminal vive en src/apps/term_ui.c (historial de 1000 lineas, barra de
 * desplazamiento, seleccion, portapapeles, menu y zoom). Aqui solo queda el historial de ordenes y el pegamento. */
static char term_hist[16][256];
static int  term_hist_count = 0, term_hist_idx = -1;

/* SOMA_203_INSTANCIAS: edicion e historial por terminal, nunca compartidos. */
struct her_terminal_session {
    int used;
    char cmd[256];
    uint32_t len, cursor;
    char history[16][256];
    int hist_count, hist_idx;
};
static struct her_terminal_session her_term_sessions[16];
static int her_term_live = -1;
static int her_tbg_slot;

static int her_term_alloc(void)
{
    for (int slot = 1; slot < 16; ++slot) {
        if (!her_term_sessions[slot].used) {
            memset(&her_term_sessions[slot], 0, sizeof her_term_sessions[slot]);
            her_term_sessions[slot].used = 1;
            her_term_sessions[slot].hist_idx = -1;
            return slot;
        }
    }
    return 0;
}
static void her_term_focus(int slot)
{
    if (slot == her_term_live) return;
    if (her_term_live > 0 && her_term_live < 16 && her_term_sessions[her_term_live].used) {
        struct her_terminal_session *old = &her_term_sessions[her_term_live];
        memcpy(old->cmd, cur_cmd, sizeof cur_cmd);
        old->len = cur_cmd_len; old->cursor = cur_cursor_pos;
        memcpy(old->history, term_hist, sizeof term_hist);
        old->hist_count = term_hist_count; old->hist_idx = term_hist_idx;
    }
    if (slot > 0 && slot < 16 && her_term_sessions[slot].used) {
        if (!term_ui_select(slot)) return;  /* OOM: nunca mostrar el estado de otra instancia. */
        struct her_terminal_session *next = &her_term_sessions[slot];
        memcpy(cur_cmd, next->cmd, sizeof cur_cmd);
        cur_cmd_len = next->len; cur_cursor_pos = next->cursor;
        memcpy(term_hist, next->history, sizeof term_hist);
        term_hist_count = next->hist_count; term_hist_idx = next->hist_idx;
        her_term_live = slot;
    } else {
        (void)term_ui_select(0);
        her_term_live = -1;
    }
}
static void her_win_focus(enum app_mode app, int slot)
{
    her_term_focus(app == APP_TERMINAL_SHELL ? slot : -1);
    if (app == APP_NOTEPAD && slot > 0) notepad_select(slot);
    if (app == APP_IMAGE_VIEWER && slot > 0) imgview_select(slot);
    if (app == APP_CODER_APP && slot > 0) coder_app_select(slot);
    if (app == APP_PAINT && slot > 0) paint_select(slot);
    if (app == APP_FILE_MANAGER && slot > 0) fm_select(slot);
    if (app == APP_PDF && slot > 0) pdfview_select(slot);
}
static void her_term_kill(int slot)
{
    if (slot <= 0 || slot >= 16 || !her_term_sessions[slot].used) return;
    if (her_tbg_slot == slot) her_tbg_slot = 0; /* No entregar el resultado a una sesion reutilizada. */
    if (her_term_live == slot) her_term_focus(-1);
    term_ui_drop(slot);
    memset(&her_term_sessions[slot], 0, sizeof her_term_sessions[slot]);
}


#define term_add_line(line, mv) term_print(line)

static void term_init_banner(uint32_t max_visible) {
    (void)max_visible;
    term_ui_init();
    term_add_line("\033[1;36m  ____  ____  __  __    _      \033[1;33mOS:     \033[1;37mSOMA v0.2 \033[0;36m(Multi-Agente)\033[0m", 0);
    term_add_line("\033[1;36m / ___|/ __ \\|  \\/  |  / \\     \033[1;33mKernel: \033[1;37mx86_64 Bare-Metal \033[1;32m(HAL v1)\033[0m", 0);
#ifdef __x86_64__
    if (apic_system_active() && apic_system_info()->timer_lapic)   /* SOMA_158_T480S: el reloj que se usa de verdad */
        term_add_line("\033[1;36m \\___ \\ |  | | |\\/| | / _ \\    \033[1;33mUptime: \033[1;37mLAPIC 1 kHz \033[0;32m(1 ms tick, APIC)\033[0m", 0);
    else
#endif
    term_add_line("\033[1;36m \\___ \\ |  | | |\\/| | / _ \\    \033[1;33mUptime: \033[1;37mPIT 1 kHz \033[0;32m(1 ms tick)\033[0m", 0);
    term_add_line("\033[1;36m  ___)| |__| | |  | |/ ___ \\   \033[1;33mCPU:    \033[1;37mRing 0 \033[1;35m(CR0:WP, NXE)\033[0m", 0);
    {   /* SOMA_157_T480S: la RAM real que gestiona el PMM (antes un texto fijo de 512 MiB) */
        size_t fr = 0, us = 0, tot = 0; pmm_get_stats(&fr, &us, &tot);
        uint64_t mib = (uint64_t)tot * 4096u >> 20;
        char ln[160], num[24]; uint32_t k = 0, nn = 0;
        const char *a = "\033[1;36m |____/\\____/|_|  |_/_/   \\_\\  \033[1;33mRAM:    \033[1;37m";
        while (*a && k < sizeof ln - 1) ln[k++] = *a++;
        if (mib >= 1024) { uint64_t g10 = mib * 10 / 1024; do { num[nn++] = (char)('0' + g10 % 10); g10 /= 10; } while (g10); }
        else { uint64_t m = mib; do { num[nn++] = (char)('0' + m % 10); m /= 10; } while (m); }
        while (nn) { if (mib >= 1024 && nn == 1 && k < sizeof ln - 1) ln[k++] = ','; if (k < sizeof ln - 1) ln[k++] = num[--nn]; }
        const char *b = mib >= 1024 ? " GiB \033[1;32m(PMM Allocator)\033[0m" : " MiB \033[1;32m(PMM Allocator)\033[0m";
        while (*b && k < sizeof ln - 1) ln[k++] = *b++;
        ln[k] = 0;
        term_add_line(ln, 0);
    }
    {   /* SOMA_162_T480S: disco y red reales (antes, textos fijos de QEMU) */
        char ln[200]; uint32_t k = 0; const char *parts[2][2] = { { "                               \033[1;33mDisco:  \033[1;37m", sysdisk_describe() },
                                                                   { "                               \033[1;33mRed:    \033[1;37m", net_describe() } };
        for (int r = 0; r < 2; r++) {
            k = 0;
            for (int q = 0; q < 2; q++) for (const char *a = parts[r][q]; *a && k < sizeof ln - 8; a++) ln[k++] = *a;
            const char *z = "\033[0m"; while (*z) ln[k++] = *z++;
            ln[k] = 0; term_add_line(ln, 0);
        }
    }
    term_add_line("                               \033[1;33mAgente: \033[1;37mnail-35b (ReAct LAN)\033[0m", 0);
    term_add_line("", 0);
    term_add_line("\033[1;36m----------------------------------------------------------------------\033[0m", 0);
    term_add_line(" \033[1;37mSOMA 0.3 pre-beta :: Escribe '\033[1;32msoma <mision>\033[1;37m' para el Agente o '\033[1;33mhelp\033[1;37m'.\033[0m", 0);
    term_add_line("\033[1;36m----------------------------------------------------------------------\033[0m", 0);
}

static void render_real_terminal(uint32_t lx, uint32_t ly, uint32_t lw, uint32_t lh, const char *cmd, uint32_t cmd_len, uint32_t cursor_pos, int show_cursor) {
    term_render(lx, ly, lw, lh, cmd, cmd_len, cursor_pos, show_cursor);
}

static void render_log_window_chat(struct gui_chat_line *lines, int count, uint32_t lx, uint32_t ly, uint32_t lw, uint32_t lh) {
    fb_fill_rect(lx, ly, lw, lh, 0x0004070E); fb_draw_rect(lx, ly, lw, lh, COLOR_CYAN);
    fb_fill_rect(lx, ly, lw, 36, 0x001E293B); fb_draw_rect(lx, ly, lw, 36, 0x00334155);
    font_draw_string(lx + 16, ly + 14, "SOMA v0.2 :: Agente IA (ReAct & LLM)", 0x00F8FAFC, 1);
    uint32_t max_visible = (lh > 50) ? (lh - 50) / 16 : 10;
    int start_idx = 0; if (count > (int)max_visible) start_idx = count - (int)max_visible;
    uint32_t line_y = ly + 48;
    for (int i = start_idx; i < count; ++i) { font_draw_string(lx + 16, line_y, lines[i].text, lines[i].color, 1); line_y += 16; }
}

static void render_input_bar_chat(const char *prompt, uint32_t prompt_color, uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh, const char *text, uint32_t len, uint32_t cursor_pos, int show_cursor) {
    fb_fill_rect(bx, by, bw, bh, 0x00020617); fb_draw_rect(bx, by, bw, bh, prompt_color);
    font_draw_string(bx + 14, by + 14, prompt, prompt_color, 2);
    uint32_t p_len = 0; while (prompt[p_len]) p_len++;
    uint32_t text_x = bx + 14 + (p_len * 8 * 2);
    fb_set_clip((int32_t)text_x, (int32_t)(by + 2), (int32_t)(bw - 28 - (p_len * 16)), (int32_t)(bh - 4));
    int offset = 0;
    if (cursor_pos * 16 > bw - 60 - (p_len * 16)) offset = cursor_pos * 16 - (bw - 60 - (p_len * 16));
    font_draw_string(text_x - offset, by + 14, text, 0x00F8FAFC, 2);
    if (show_cursor) {
        uint32_t cx = text_x - offset + (cursor_pos * 16);
        fb_fill_rect(cx, by + 12, 16, 20, prompt_color);
        if (cursor_pos < len && text[cursor_pos] >= 32 && text[cursor_pos] <= 126) {
            char ch[2] = { text[cursor_pos], '\0' };
            font_draw_string(cx, by + 14, ch, 0x00000000, 2);
        }
    }
    fb_reset_clip();
}

static void render_agent_global_rail(uint32_t cur_w, uint32_t cur_h)
{
    (void)cur_w; (void)cur_h;
    her_refresh_all();
}

/* ====================================================================
 * PARCHE 091: la mision del agente corre en su propio hilo ("soma_agente") y la GUI sigue viva: se pueden mover y
 * redimensionar ventanas, usar otras aplicaciones y escribir mientras SOMA piensa.
 * Cuando el agente necesita la pantalla (abre una ventana, escribe un archivo "en vivo", ejecuta un programa grafico)
 * pide turno con gui_enter(): el bucle principal se detiene en un punto seguro (al principio de la vuelta), le cede la
 * pantalla y, cuando el agente la devuelve con gui_leave(), vuelve a leer la geometria y repinta todo.
 * ==================================================================== */
static volatile int g_agent_thr_on;        /* hay una mision en marcha en el hilo del agente */
static volatile int g_agent_thr_done;      /* la mision termino: el bucle principal cierra el turno */
static volatile uint32_t g_agent_tid;
static volatile int g_gui_loop_on;         /* el bucle principal de la GUI esta corriendo */
static volatile int g_gui_req, g_gui_grant; static int g_gui_depth;
static char g_agent_mission[SOMA_AGENT_CMD_MAX];
static int gui_in_agent_thread(void) { struct tcb *t = thread_current(); return g_agent_thr_on && t && t->tid == g_agent_tid; }
int gui_enter(void)
{
    if (!gui_in_agent_thread() || !g_gui_loop_on) return 0;
    if (g_gui_depth++ > 0) return 1;
    g_gui_req = 1;
    for (uint32_t i = 0; !g_gui_grant && g_gui_loop_on && i < 60000u; i++) thread_sleep(1);
    return 1;
}
void gui_leave(int tok)
{
    if (!tok) return;
    if (--g_gui_depth > 0) return;
    g_gui_req = 0;
}
static void agent_thread_main(void *arg)
{
    (void)arg;
    agent_run("soma", g_agent_mission);
    g_agent_thr_done = 1;
}
/* cierre del turno (lo hace el bucle principal) */
static void agent_turn_finish(void)
{
    const uint32_t wrap = 76;
    her_agent_busy = 0;
    her_finish_turn(agent_last_response);
    if (agent_last_response[0] != '\0')
        log_add(agent_log, &agent_log_count, "[soma]: ", agent_last_response, 0x0034D399, wrap);
    else
        log_add(agent_log, &agent_log_count, "[soma]: ", "Mision ejecutada en hardware.", 0x0034D399, wrap);
    agent_global_scroll = 0;
    g_agent_thr_on = 0; g_agent_thr_done = 0; g_gui_depth = 0; g_gui_req = 0; g_gui_grant = 0;
}
int gui_is_running(void) { return g_gui_loop_on; }   /* PARCHE 097 */
int gui_agent_busy(void) { return g_agent_thr_on; }

/* SOMA_175B3: ordenes lentas del terminal (wifi buscar, wifi arrancar; con el 175C wifi conectar y desconectar) en un hilo aparte. Antes se ejecutaban en el hilo de
 * la GUI y el escritorio se quedaba congelado los segundos que dura la busqueda. Su salida se captura en ese hilo y se
 * muestra en el terminal al terminar; mientras, el sistema sigue respondiendo. Una a la vez. */
static volatile int tbg_state;                 /* 0 libre, 1 en marcha, 2 terminada (falta mostrarla) */
static char tbg_cmd[256], tbg_out[4096];
static char *tbg_cap; static uint32_t tbg_n;
enum { TBG_CAP = 64 * 1024 };
static int tbg_is_slow(const char *c)
{
    while (*c == ' ') c++;
    if (c[0] == 't' && c[1] == 'l' && c[2] == 's' && c[3] == ' ') return 1;                     /* SOMA_176A: tls <url> tarda segundos */
    if (c[0] == 'c' && c[1] == 'a' && c[2] == 'm' && c[3] == ' ') {                            /* SOMA_178_T480S: iniciar, foto y video esperan segundos */
        const char *s = c + 4; while (*s == ' ') s++;
        if (s[0] == 'i' && s[1] == 'n' && s[2] == 'i') return 1;
        if (s[0] == 'f' || s[0] == 'v') return 1;
    }
    if ((c[0] == 'c' && c[1] == 'a' && c[2] == 'p' && c[3] == ' ') || (c[0] == 'c' && c[1] == 'a' && c[2] == 'p' && c[3] == 't' && c[4] == 'u' && c[5] == 'r' && c[6] == 'a' && c[7] == ' ')) {   /* SOMA_179_T480S: foto, video, audio y av esperan segundos */
        const char *s = c + (c[3] == ' ' ? 4 : 8); while (*s == ' ') s++;
        if (s[0] == 'f' || s[0] == 'v' || (s[0] == 'a' && s[1] != 'b') || s[0] == 'i' || s[0] == 'p') return 1;
    }
    if (c[0] == 'm' && c[1] == 'i' && c[2] == 'c' && c[3] == ' ') {                            /* SOMA_177B: nivel, grabar y probar esperan segundos */
        const char *s = c + 4; while (*s == ' ') s++;
        if (s[0] == 'n' || s[0] == 'g' || s[0] == 'p') return 1;
    }
    if (!(c[0] == 'w' && c[1] == 'i' && c[2] == 'f' && c[3] == 'i' && c[4] == ' ')) return 0;
    c += 5; while (*c == ' ') c++;
    /* buscar, arrancar, conectar y desconectar (SOMA_175C) */
    static const char *const slow[6] = { "buscar", "arrancar", "conectar", "desconectar", "prueba", "ipfija" };   /* SOMA_175C5: prueba */
    for (int i = 0; i < 6; i++) { const char *w = slow[i]; int k = 0; while (w[k] && c[k] == w[k]) k++; if (!w[k] && (c[k] == 0 || c[k] == ' ')) return 1; }
    return 0;
}
static void tbg_main(void *a)
{
    (void)a;
    tbg_out[0] = 0; tbg_n = 0;
    int cap = !console_capture_active();
    if (cap) console_capture_begin(tbg_cap, TBG_CAP, "");
    dispatch_command(tbg_cmd, tbg_out, sizeof tbg_out);
    if (cap) tbg_n = console_capture_end();
    tbg_state = 2;
}
static int tbg_start(const char *cmd)
{
    if (tbg_state) return -1;
    if (!tbg_cap) { tbg_cap = (char *)kmalloc(TBG_CAP); if (!tbg_cap) return -2; }
    uint32_t i = 0; while (cmd[i] && i < sizeof tbg_cmd - 1) { tbg_cmd[i] = cmd[i]; i++; } tbg_cmd[i] = 0;
    tbg_state = 1;
    if (!thread_create("term_bg", tbg_main, 0)) { tbg_state = 0; return -3; }
    return 0;
}
/* muestra la salida (sin colores ANSI) linea a linea; 1 si habia algo */
static int tbg_show(void)
{
    const char *show = tbg_out;
    if (!show[0]) {
        uint32_t w = 0, cn = tbg_n;
        for (uint32_t r = 0; r < cn; r++) {
            if (tbg_cap[r] == 27 && r + 1 < cn && tbg_cap[r + 1] == '[') {
                r += 2; while (r < cn && !((tbg_cap[r] >= 'A' && tbg_cap[r] <= 'Z') || (tbg_cap[r] >= 'a' && tbg_cap[r] <= 'z'))) r++;
                continue;
            }
            tbg_cap[w++] = tbg_cap[r];
        }
        tbg_cap[w < TBG_CAP ? w : TBG_CAP - 1] = 0;
        show = tbg_cap;
    }
    int any = 0;
    while (*show) {
        char line[220]; uint32_t lp = 0;
        while (*show && *show != '\n' && lp < sizeof line - 1) { if (*show != '\r') line[lp++] = *show; show++; }
        line[lp] = 0; if (*show == '\n') show++;
        if (lp) { term_print(line); any = 1; }
    }
    return any;
}

static void submit_global_agent_prompt(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    (void)win_x; (void)win_y; (void)win_w; (void)win_h;
    if (agent_global_len == 0) return;

    char mission_copy[SOMA_AGENT_CMD_MAX];
    uint32_t n = agent_global_len;
    if (n >= sizeof(mission_copy)) n = sizeof(mission_copy) - 1;
    for (uint32_t i = 0; i < n; ++i) mission_copy[i] = agent_global_cmd[i];
    mission_copy[n] = '\0';

    const uint32_t wrap = 76;
    if (g_agent_thr_on) {   /* PARCHE 091: una mision a la vez */
        log_add(agent_log, &agent_log_count, "[soma]: ", "Sigo trabajando en la mision anterior; espera a que termine.", 0x00FBBF24, wrap);
        her_refresh_all(); fb_present();
        return;
    }
    log_add(agent_log, &agent_log_count, "user> ", mission_copy, COLOR_CYAN, wrap);
    her_begin_turn(mission_copy);

    agent_global_len = 0;
    agent_global_cursor = 0;
    agent_global_cmd[0] = '\0';
    agent_global_scroll = 0;
    her_prev_len = 0;
    her_pulse = 0;
    agent_last_response[0] = '\0';

    her_refresh_all();
    fb_present();

    /* PARCHE 091: la mision va a su hilo; si no se puede crear, como antes (bloqueando) */
    if (g_gui_loop_on) {
        for (uint32_t i = 0; i < sizeof(g_agent_mission); i++) { g_agent_mission[i] = mission_copy[i]; if (!mission_copy[i]) break; }
        g_agent_thr_done = 0; g_gui_req = 0; g_gui_grant = 0; g_gui_depth = 0;
        her_agent_busy = 1; g_agent_thr_on = 1;
        struct tcb *at = thread_create("soma_agente", agent_thread_main, 0);
        if (at) { g_agent_tid = at->tid; thread_set_owner(at, ACCT_AGENT); return; }
        g_agent_thr_on = 0; her_agent_busy = 0;
    }
    her_anim_start();
    agent_run("soma", mission_copy);
    her_anim_stop();

    her_finish_turn(agent_last_response);

    if (agent_last_response[0] != '\0')
        log_add(agent_log, &agent_log_count, "[soma]: ", agent_last_response, 0x0034D399, wrap);
    else
        log_add(agent_log, &agent_log_count, "[soma]: ", "Mision ejecutada en hardware.", 0x0034D399, wrap);

    agent_global_scroll = 0;
    her_refresh_all();
    fb_present();
}

/* ====================================================================
 * SOMA_ORB_FX_PATCH (A): orbe estilo XMB/PS3, absorcion y expulsion
 * Todo en enteros. Mezcla ADITIVA directamente sobre el backbuffer.
 * ==================================================================== */
#define OFX_LEVEL 3
#if OFX_LEVEL >= 3
#define OFX_HALO_STEP 1
#define OFX_RIB_N     5
#define OFX_RIB_STEP  1
#define OFX_BURST_APP 170
#define OFX_BURST_ICO 150
#elif OFX_LEVEL == 2
#define OFX_HALO_STEP 1
#define OFX_RIB_N     5
#define OFX_RIB_STEP  2
#define OFX_BURST_APP 110
#define OFX_BURST_ICO 100
#else
#define OFX_HALO_STEP 2
#define OFX_RIB_N     3
#define OFX_RIB_STEP  3
#define OFX_BURST_APP 60
#define OFX_BURST_ICO 60
#endif

#define OFX_EJ_MAX   384
#define OFX_RING_MAX 8

struct ofx_ejecta {
    int32_t  x, y, vx, vy;          /* 24.8 */
    int16_t  life, maxlife;
    int8_t   turn;
    uint8_t  alive;
    uint32_t col;
};

struct ofx_ring_t {
    int32_t  r, speed;
    int16_t  life, maxlife;
    uint32_t col;
    uint8_t  alive;
};

struct ofx_rib_cfg {
    int16_t  rad, amp;              /* radio y amplitud (px a 1080p) */
    uint8_t  harm, sq;              /* armonicos de la onda, aplastamiento /256 */
    int16_t  tilt;                  /* inclinacion base (1024 = 360 grados) */
    uint8_t  wid, alpha;            /* semi-ancho maximo, opacidad */
    int32_t  spd;                   /* velocidad (con signo) */
    uint32_t col;
};

static const struct ofx_rib_cfg ofx_rib[5] = {
    { 150, 14, 3, 110,  -40,  9, 200,  300, 0x009FD8FFu },
    { 190, 20, 2,  90,   30, 12, 170, -420, 0x004F8CFFu },
    { 235, 26, 4,  80,  -15, 14, 150,  520, 0x008B5CF6u },
    { 175, 12, 5, 130,   70,  6, 150, -240, 0x0038BDF8u },
    { 280, 30, 3,  70,   10, 16, 120,  380, 0x00E0F2FEu },
};

static const uint32_t ofx_pal[10] = {
    0x0038BDF8u, 0x0010B981u, 0x00F59E0Bu, 0x0038BDF8u, 0x0010B981u,
    0x00F472B6u, 0x00A855F7u, 0x000EA5E9u, 0x0034D399u, 0x00F59E0Bu
};

static struct ofx_ejecta  ofx_ej[OFX_EJ_MAX];
static struct ofx_ring_t  ofx_rings[OFX_RING_MAX];
static int       ofx_ej_next = 0;
static int       ofx_ring_next = 0;
static uint32_t  ofx_flash = 0;
static uint32_t  ofx_absorb_n = 0;
static uint32_t  ofx_ph[5];
static uint64_t  ofx_phf[5];      /* fase con 16 bits fraccionarios (PARCHE V2) */
static int32_t   ofx_sp_f = 0;    /* velocidad suavizada x256 */
static uint32_t  ofx_last_ms = 0;
static uint32_t  ofx_acc = 0;
static int       ofx_prev_icons = 0;
static int       ofx_prev_app = -1;
static int       ofx_jet_left = 0;
static uint32_t  ofx_jet_col = 0;
static int32_t   ofx_jet_dx = 0, ofx_jet_dy = 0;
static uint32_t  ofx_halo[256];

static inline uint32_t ofx_mul(uint32_t c, uint32_t k)      /* k 0..256 */
{
    if (k >= 256u) return c & 0x00FFFFFFu;
    uint32_t r = (((c >> 16) & 255u) * k) >> 8;
    uint32_t g = (((c >> 8) & 255u) * k) >> 8;
    uint32_t b = ((c & 255u) * k) >> 8;
    return (r << 16) | (g << 8) | b;
}

/* SOMA_153_ORBE_OCULTO: rectangulo de pantalla que la ventana de arriba va a tapar por completo en este fotograma
 * (solo si es OPACA; con transparencia no hay rectangulo y el orbe se dibuja entero como antes). Lo que el orbe pinte
 * ahi se sobrescribe despues sin llegar a verse, asi que no se calcula. Se recorta en el radio de las esquinas
 * redondeadas para que por ellas se siga viendo el fondo. Solo vale dentro de her_refresh_all. */
#ifndef SOMA_ORB_OCCLUSION
#define SOMA_ORB_OCCLUSION 1
#endif
static int ofx_occ_on; static int32_t ofx_occ_x0, ofx_occ_y0, ofx_occ_x1, ofx_occ_y1;
static int ofx_rib_mode = 1;                              /* SOMA_153B: 1 = cintas a media resolucion (en la comparacion se alterna) */
#if SOMA_WEB_PERF_LOG
static uint64_t her153_rib_cycles;
static inline uint64_t her153_cycles(void);
#endif
static inline int ofx_occluded(int32_t x, int32_t y)
{
    return ofx_occ_on && x >= ofx_occ_x0 && x < ofx_occ_x1 && y >= ofx_occ_y0 && y < ofx_occ_y1;
}
static inline void ofx_px(volatile uint32_t *bb, int32_t w, int32_t h,
                          int32_t x, int32_t y, uint32_t a)
{
    if (x < 0 || y < 0 || x >= w || y >= h || !a) return;
    if (ofx_occluded(x, y)) return;
    volatile uint32_t *p = bb + (uint32_t)y * (uint32_t)w + (uint32_t)x;
    uint32_t d = *p;
    uint32_t r = ((d >> 16) & 255u) + ((a >> 16) & 255u);
    uint32_t g = ((d >> 8) & 255u) + ((a >> 8) & 255u);
    uint32_t b = (d & 255u) + (a & 255u);
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    *p = (r << 16) | (g << 8) | b;
}

/* Linea aditiva con degradado de color c0 (inicio) -> c1 (fin) y borde suave */
static void ofx_line(volatile uint32_t *bb, int32_t w, int32_t h,
                     int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                     uint32_t c0, uint32_t c1)
{
    int32_t dx = x1 - x0, dy = y1 - y0;
    int32_t adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int32_t n = adx > ady ? adx : ady;
    if (n > 420) return;
    if (n == 0) { ofx_px(bb, w, h, x0, y0, c1); return; }
    int horiz = adx > ady;
    for (int32_t i = 0; i <= n; ++i) {
        int32_t x = x0 + dx * i / n;
        int32_t y = y0 + dy * i / n;
        uint32_t c = her_mix256(c0, c1, (uint32_t)(256 * i / n));
        ofx_px(bb, w, h, x, y, c);
        uint32_t s = ofx_mul(c, 90u);
        if (horiz) ofx_px(bb, w, h, x, y + 1, s);
        else       ofx_px(bb, w, h, x + 1, y, s);
    }
}

static void ofx_draw_ring(volatile uint32_t *bb, int32_t w, int32_t h,
                          int32_t cx, int32_t cy, int32_t r, uint32_t col)
{
    if (r < 2) return;
    uint32_t stp = (r < 60) ? 4u : ((r < 120) ? 2u : 1u);
    uint32_t soft = ofx_mul(col, 120u);
    for (uint32_t a = 0; a < 1024u; a += stp) {
        int32_t c = her_cs(a), s = her_sn(a);
        ofx_px(bb, w, h, cx + ((r * c) >> 10), cy + ((r * s) >> 10), col);
        ofx_px(bb, w, h, cx + (((r + 1) * c) >> 10), cy + (((r + 1) * s) >> 10), soft);
        ofx_px(bb, w, h, cx + (((r - 1) * c) >> 10), cy + (((r - 1) * s) >> 10), soft);
    }
}

static int32_t ofx_radius(int32_t h)
{
    int32_t R = h / 5;
    if (R < 100) R = 100;
    if (R > 300) R = 300;
    return R;
}

static uint32_t ofx_app_color(int app)
{
    switch (app) {
    case APP_TERMINAL_SHELL: return 0x0010B981u;
    case APP_AGENT_CHAT:     return 0x0038BDF8u;
    case APP_WEB_BROWSER:    return 0x00F59E0Bu;
    case APP_FILE_MANAGER:   return 0x0038BDF8u;
    case APP_IMAGE_VIEWER:   return 0x0010B981u;
    case APP_NOTEPAD:        return 0x00F472B6u;
    case APP_AGENT_MEMORY:   return 0x00A855F7u;
    case APP_LAYOUT:         return 0x000EA5E9u;
        case APP_AGENTS:         return 0x0034D399u;
    case APP_PAINT:          return 0x00F59E0Bu;
    case APP_CODER_APP:      return 0x0000FF88u;
    case APP_MEDIA:          return 0x00FB7185u;
    case APP_RECORDER:       return 0x00F87171u;
    case APP_CAMERA:         return 0x0038BDF8u;   /* SOMA_178_T480S */
    case APP_CAPTURE:        return 0x00A78BFAu;   /* SOMA_179_T480S */
    case APP_SOMA_APP:       return 0x0038BDF8u; /*PL12_OFX_COL*/
    default:                 return 0x007DD3FCu;
    }
}

/* ---------- generadores ---------- */
static void ofx_spawn_ring(int32_t r, int32_t speed, int life, uint32_t col)
{
    struct ofx_ring_t *g = &ofx_rings[ofx_ring_next];
    ofx_ring_next = (ofx_ring_next + 1) % OFX_RING_MAX;
    g->r = r; g->speed = speed;
    g->life = (int16_t)life; g->maxlife = (int16_t)life;
    g->col = col; g->alive = 1;
}

/* SOMA_223_MINIMIZAR: la ventana que se minimiza se deshace en un mosaico de teselas con sus propios colores; cada tesela sale
 * con un pequeno retraso (primero las mas cercanas al agujero negro), traza una estela que gira alrededor del orbe en el plano
 * inclinado de la orbita, roza el horizonte de sucesos y se posa en el hueco que ocupara el icono, que aparece al final con un
 * destello suave. Unos 0,75 s. Coste: 192 particulas con una linea aditiva cada una durante el vuelo (nada fuera de el). */
#define MFLY_N    192
#define MFLY_DUR  560u                                   /* vuelo de cada tesela (ms) */
#define MFLY_LAG  190u                                   /* retraso maximo entre la primera y la ultima */
struct her_mfly_p { int16_t sx, sy, tw, th; uint16_t delay; int16_t ja, jr; uint32_t col; };
static struct her_mfly_p her_mfly_p[MFLY_N];
static uint64_t her_mfly_t0;
static uint32_t her_mfly_a0;                             /* angulo de la orbita del icono al empezar */
static int32_t  her_mfly_delta;                          /* giro extra hasta el destino (unidades de 1/1024 de vuelta) */
static uint32_t her_mfly_acol;
static uint32_t her_atan1024(int32_t y, int32_t x)       /* angulo de (x, y) en 1/1024 de vuelta, sentido de her_sn */
{
    int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (!ax && !ay) return 0;
    int32_t mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    int32_t z = (int32_t)(((int64_t)mn << 15) / mx);                       /* 0..32768 */
    int32_t a = (128 * z + (int32_t)(((int64_t)45 * z * (32768 - z)) >> 15)) >> 15;   /* atan(z) en 1/1024 (0..128) */
    if (ay > ax) a = 256 - a;
    if (x < 0) a = 512 - a;
    if (y < 0) a = 1024 - a;
    return (uint32_t)a & 1023u;
}
static void her_mfly_geom(uint32_t w, uint32_t h, int32_t *r_orb)
{
    int32_t r = ofx_radius((int32_t)h) + 55;              /* lo mismo que her_min_orbit_position */
    if (r < 90) r = 90;
    if (r > (int32_t)h / 2 - 40) r = (int32_t)h / 2 - 40;
    if (r < 45) r = 45;
    (void)w; *r_orb = r;
}
static void her_mfly_start(const struct her_win_node *v, int min_idx)
{
    uint32_t w = fb_get_width(), h = fb_get_height();
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb || w < 240u || h < 240u || min_idx < 0) return;
    int32_t cx = (int32_t)w / 2, cy = (int32_t)h / 2;
    int32_t wx = v->x, wy = v->y, ww = (int32_t)v->w, wh = (int32_t)v->h;
    if (ww < 64 || wh < 48) return;
    int id = her_min_dock_index(v->app);
    her_mfly_acol = id >= 0 ? her_dock_items[id].color : 0x0038BDF8u;
    uint64_t now = timer_get_uptime_ms();
    her_mfly_a0 = ((uint32_t)(now % 28000u) * 1024u / 28000u + her_min_phase[min_idx]) & 1023u;
    /* angulo de salida: el del centro de la ventana, en el plano de la orbita (y estirada 4/3) */
    uint32_t aw = her_atan1024(((wy + wh / 2) - cy) * 4 / 3, (wx + ww / 2) - cx);
    int32_t d = (int32_t)((her_mfly_a0 - aw) & 1023u);    /* giro en el sentido de la orbita */
    if (d < 320) d += 1024;                               /* al menos un tercio de vuelta alrededor del orbe */
    her_mfly_delta = d;
    const int C = 16, R = 12;
    int32_t tw = ww / C, th = wh / R; if (tw < 2) tw = 2; if (th < 2) th = 2;
    int32_t maxd = 1;
    for (int k = 0; k < MFLY_N; ++k) {
        int c = k % C, r = k / C;
        int32_t px = wx + c * tw, py = wy + r * th;
        int32_t mx = px + tw / 2, my = py + th / 2;
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (mx >= (int32_t)w) mx = (int32_t)w - 1;
        if (my >= (int32_t)h) my = (int32_t)h - 1;
        uint32_t sc = bb[(uint32_t)my * w + (uint32_t)mx] & 0x00FFFFFFu;
        int border = (r == 0 || r == R - 1 || c == 0 || c == C - 1);
        uint32_t lum = ((sc >> 16) & 255u) + ((sc >> 8) & 255u) + (sc & 255u);
        struct her_mfly_p *q = &her_mfly_p[k];
        q->sx = (int16_t)px; q->sy = (int16_t)py; q->tw = (int16_t)tw; q->th = (int16_t)th;
        q->col = border ? her_mix256(her_mfly_acol, 0x00FFFFFFu, 70u)
                        : (lum < 60u ? her_mix256(sc, her_mfly_acol, 150u) : her_mix256(sc, her_mfly_acol, 90u));
        int32_t dx = mx - cx, dy = my - cy;
        int32_t dd = (int32_t)her_isqrt((uint32_t)(dx * dx + dy * dy));
        q->delay = (uint16_t)dd;                                         /* se normaliza abajo */
        if (dd > maxd) maxd = dd;
        uint32_t rnd = her_rand();
        q->ja = (int16_t)((int32_t)(rnd & 63u) - 32);                    /* dispersion del chorro a mitad de camino */
        q->jr = (int16_t)((int32_t)((rnd >> 6) & 63u) - 32);
    }
    int32_t mind = maxd;
    for (int k = 0; k < MFLY_N; ++k) if (her_mfly_p[k].delay < mind) mind = her_mfly_p[k].delay;
    for (int k = 0; k < MFLY_N; ++k) {
        int32_t dd = her_mfly_p[k].delay - mind, span = maxd - mind; if (span < 1) span = 1;
        her_mfly_p[k].delay = (uint16_t)((uint32_t)dd * MFLY_LAG / (uint32_t)span + (her_rand() & 15u));
    }
    her_mfly_app = (int)v->app; her_mfly_slot = v->term_slot;
    her_mfly_t0 = now; her_mfly_icon_k = 0u; her_mfly_on = 1;
}
/* posicion de una particula en el instante e (0..256) de su vuelo */
static void her_mfly_pos(const struct her_mfly_p *q, int32_t e, int32_t cx, int32_t cy, int32_t r_orb,
                         uint32_t a_now_shift, int32_t *ox, int32_t *oy)
{
    int32_t sx = q->sx + q->tw / 2 - cx, sy = (q->sy + q->th / 2 - cy) * 4 / 3;
    int32_t r0 = (int32_t)her_isqrt((uint32_t)(sx * sx + sy * sy));
    uint32_t a0 = her_atan1024(sy, sx);
    int32_t bump = her_sn((uint32_t)e * 2u);              /* sin(pi*e) en 1/1024: 0 en los extremos, 1024 a mitad */
    int32_t a = (int32_t)a0 + her_mfly_delta * e / 256 + (int32_t)a_now_shift * e / 256 + q->ja * bump / 1024;
    int32_t rr = r0 + (r_orb - r0) * e / 256;             /* del sitio de la ventana al radio de la orbita */
    int32_t dip = (rr - 40) * 45 / 100;                    /* y a mitad de camino roza el horizonte */
    if (dip < 0) dip = 0;
    rr = rr - dip * bump / 1024 + q->jr * bump / 1024;
    if (rr < 34) rr = 34;
    *ox = cx + her_cs((uint32_t)a & 1023u) * rr / 1024;
    *oy = cy + her_sn((uint32_t)a & 1023u) * rr * 3 / 4 / 1024;
}
static void her_mfly_draw(uint32_t w, uint32_t h)
{
    if (!her_mfly_on) return;
    volatile uint32_t *bb = fb_get_buffer();
    uint64_t now = timer_get_uptime_ms();
    uint32_t el = (uint32_t)(now - her_mfly_t0);
    int found = 0;
    for (int i = 0; i < her_min_count; ++i)
        if ((int)her_minimized[i].app == her_mfly_app && her_minimized[i].term_slot == her_mfly_slot) { found = 1; break; }
    if (!bb || !found || el >= MFLY_DUR + MFLY_LAG + 40u) { her_mfly_on = 0; her_mfly_icon_k = 256u; return; }
    int32_t cx = (int32_t)w / 2, cy = (int32_t)h / 2, r_orb;
    her_mfly_geom(w, h, &r_orb);
    uint32_t a_now = ((uint32_t)(now % 28000u) * 1024u / 28000u);
    uint32_t a_t0 = ((uint32_t)(her_mfly_t0 % 28000u) * 1024u / 28000u);
    uint32_t shift = (a_now - a_t0) & 1023u;               /* lo que ha girado la orbita desde que empezo */
    int32_t bx0 = (int32_t)w, by0 = (int32_t)h, bx1 = 0, by1 = 0;
    for (int k = 0; k < MFLY_N; ++k) {
        const struct her_mfly_p *q = &her_mfly_p[k];
        int32_t u = el > q->delay ? (int32_t)((el - q->delay) * 256u / MFLY_DUR) : 0;
        if (u > 256) continue;                             /* ya llego */
        if (u <= 0) {                                      /* aun no ha salido: la tesela sigue en su sitio */
            fb_fill_rect((uint32_t)(q->sx < 0 ? 0 : q->sx), (uint32_t)(q->sy < 0 ? 0 : q->sy), (uint32_t)q->tw, (uint32_t)q->th, ofx_mul(q->col, 150u));
            continue;
        }
        int32_t e = 256 - (256 - u) * (256 - u) / 256 * (256 - u) / 256;   /* salida rapida y llegada suave */
        int32_t x, y, tx, ty;
        her_mfly_pos(q, e, cx, cy, r_orb, shift, &x, &y);
        int32_t ue = u > 22 ? u - 22 : 0;                  /* la cola: el mismo camino un poco antes */
        int32_t et = 256 - (256 - ue) * (256 - ue) / 256 * (256 - ue) / 256;
        her_mfly_pos(q, et, cx, cy, r_orb, shift, &tx, &ty);
        {   /* cola de 56 px como mucho: al arrancar el paso es largo y saldria un trazo recto */
            int32_t ddx = x - tx, ddy = y - ty; int32_t L = (int32_t)her_isqrt((uint32_t)(ddx * ddx + ddy * ddy));
            if (L > 56) { tx = x - ddx * 56 / L; ty = y - ddy * 56 / L; }
        }
        uint32_t fade = u < 200 ? 256u : (uint32_t)(256 - u) * 256u / 56u;   /* se apaga al posarse */
        if (u < 36) {                                      /* desintegracion: la tesela encoge mientras arranca */
            int32_t sw = q->tw * (36 - u) / 36, sh = q->th * (36 - u) / 36;
            if (sw > 1 && sh > 1 && x - sw / 2 >= 0 && y - sh / 2 >= 0 && x + sw / 2 < (int32_t)w && y + sh / 2 < (int32_t)h)
                fb_fill_rect((uint32_t)(x - sw / 2), (uint32_t)(y - sh / 2), (uint32_t)sw, (uint32_t)sh, q->col);
        }
        uint32_t hot = (uint32_t)(her_sn((uint32_t)e * 2u) > 0 ? her_sn((uint32_t)e * 2u) : 0) / 5u;   /* mas blanca junto al horizonte */
        uint32_t tint = her_mix256(q->col, her_mfly_acol, 110u);           /* el color de la aplicacion tine la estela */
        uint32_t head = ofx_mul(her_mix256(tint, 0x00FFFFFFu, 16u + hot / 2u), 200u * fade / 256u);
        uint32_t tail = ofx_mul(tint, 22u * fade / 256u);
        ofx_line(bb, (int32_t)w, (int32_t)h, tx, ty, x, y, tail, head);
        uint32_t g = ofx_mul(head, 70u);
        ofx_px(bb, (int32_t)w, (int32_t)h, x + 1, y, g); ofx_px(bb, (int32_t)w, (int32_t)h, x - 1, y, g);
        ofx_px(bb, (int32_t)w, (int32_t)h, x, y + 1, g); ofx_px(bb, (int32_t)w, (int32_t)h, x, y - 1, g);
        int32_t lx = tx < x ? tx : x, hx = tx < x ? x : tx, ly = ty < y ? ty : y, hy = ty < y ? y : ty;
        if (lx < bx0) bx0 = lx;
        if (ly < by0) by0 = ly;
        if (hx > bx1) bx1 = hx;
        if (hy > by1) by1 = hy;
    }
    /* el icono aparece en el ultimo cuarto, con un halo que se apaga */
    int32_t tot = (int32_t)(MFLY_DUR + MFLY_LAG);
    int32_t k = ((int32_t)el - tot * 3 / 4) * 256 / (tot / 4);
    if (k < 0) k = 0;
    if (k > 256) k = 256;
    her_mfly_icon_k = (uint32_t)k;
    if (k > 0 && k < 256) {
        for (int i = 0; i < her_min_count; ++i)
            if ((int)her_minimized[i].app == her_mfly_app && her_minimized[i].term_slot == her_mfly_slot) {
                int32_t ix, iy; her_min_orbit_position(i, w, h, now, &ix, &iy);
                int32_t rr = 18 + (256 - k) / 12;
                uint32_t al = (uint32_t)(256 - k) * 110u / 256u;
                fb_fill_rrect_aa(ix - rr, iy - rr, 2 * rr, 2 * rr, rr, (al << 24) | (her_mfly_acol & 0x00FFFFFFu));
                break;
            }
    }
    if (bx1 >= bx0) fb_damage(bx0 - 3, by0 - 3, bx1 - bx0 + 7, by1 - by0 + 7);
}
/* Minimizar con la animacion (la X de la ventana y el cierre por teclado); her_minimize_window sigue sin animacion. */
static void her_minimize_window_fx(int idx)
{
    if (idx < 0 || idx >= her_num_windows) return;
    struct her_win_node v = her_windows[idx];
    her_minimize_window(idx);
    for (int i = her_min_count - 1; i >= 0; --i)
        if (her_minimized[i].app == v.app && her_minimized[i].term_slot == v.term_slot) { her_mfly_start(&v, i); break; }
}

static void ofx_spawn_ej(int32_t cx, int32_t cy, int32_t dirx, int32_t diry,
                         uint32_t col, int radial, int32_t spmin, int32_t spvar)
{
    struct ofx_ejecta *e = &ofx_ej[ofx_ej_next];
    ofx_ej_next = (ofx_ej_next + 1) % OFX_EJ_MAX;

    int32_t r0 = 30 + (int32_t)(her_rand() & 7u);
    int32_t sp = spmin + (int32_t)(her_rand() % (uint32_t)spvar);
    int32_t sx, sy, ux, uy;

    if (radial) {
        uint32_t ang = her_rand() & 1023u;
        ux = her_cs(ang); uy = her_sn(ang);
        sx = cx + ((r0 * ux) >> 10);
        sy = cy + ((r0 * uy) >> 10);
    } else {
        int32_t s  = (int32_t)(her_rand() % 921u) - 460;     /* abanico */
        int32_t o  = (int32_t)(her_rand() % 61u) - 30;       /* origen en el borde */
        ux = dirx + ((-diry * s) >> 10);
        uy = diry + ((dirx * s) >> 10);
        sx = cx + ((r0 * dirx) >> 10) + ((-diry * o) >> 10);
        sy = cy + ((r0 * diry) >> 10) + ((dirx * o) >> 10);
    }
    e->x = sx << 8;  e->y = sy << 8;
    e->vx = (ux * sp) >> 2;
    e->vy = (uy * sp) >> 2;
    e->maxlife = (int16_t)(24 + (int32_t)(her_rand() % 20u));
    e->life = e->maxlife;
    e->turn = (her_rand() & 1u) ? 1 : -1;
    e->col = ((her_rand() & 3u) == 0u) ? her_mix256(col, 0x00FFFFFFu, 140u) : col;
    e->alive = 1;
}

static void ofx_trigger_app(int32_t cx, int32_t cy, int app)
{
    uint32_t col = ofx_app_color(app);
    int32_t tx = (int32_t)her_win_x + (int32_t)her_win_w / 2;
    int32_t ty = (int32_t)her_win_y + (int32_t)her_win_h / 2;
    int32_t dx = tx - cx, dy = ty - cy;
    uint32_t len = her_isqrt((uint32_t)(dx * dx + dy * dy));
    int radial_only = (len < 4u);
    if (!radial_only) {
        ofx_jet_dx = (dx * 1024) / (int32_t)len;
        ofx_jet_dy = (dy * 1024) / (int32_t)len;
    }
    ofx_jet_col = col;
    ofx_jet_left = radial_only ? 0 : 10;
    for (int i = 0; i < OFX_BURST_APP; ++i) {
        int rad = radial_only || (i % 5) == 0;
        ofx_spawn_ej(cx, cy, ofx_jet_dx, ofx_jet_dy, col, rad, 9, 14);
    }
    ofx_spawn_ring(30, 7, 22, col);
    ofx_spawn_ring(30, 4, 32, her_mix256(col, 0x00FFFFFFu, 120u));
    ofx_flash += 170u;
    if (ofx_flash > 255u) ofx_flash = 255u;
}

static void ofx_trigger_icons(int32_t cx, int32_t cy)
{
    for (int i = 0; i < OFX_BURST_ICO; ++i)
        ofx_spawn_ej(cx, cy, 0, 0, ofx_pal[i % 10], 1, 5, 10);
    ofx_spawn_ring(30, 6, 24, 0x0038BDF8u);
    ofx_spawn_ring(30, 4, 30, 0x00A855F7u);
    ofx_spawn_ring(30, 9, 18, 0x00F8FAFCu);
    ofx_flash += 130u;
    if (ofx_flash > 255u) ofx_flash = 255u;
}

/* Gancho llamado desde her_vortex_close_step cada vez que el horizonte absorbe */
static void ofx_on_absorb(uint32_t col)
{
    ofx_flash += 10u;
    if (ofx_flash > 255u) ofx_flash = 255u;
    if ((ofx_absorb_n++ & 15u) == 0u)
        ofx_spawn_ring(64, -3, 12, col & 0x00FFFFFFu);   /* onda que se contrae */
}

/* ---------- simulacion (30 Hz, independiente de cuantas veces se pinte) ---------- */
static void ofx_tick(int32_t cx, int32_t cy)
{
    if (ofx_jet_left > 0) {
        ofx_jet_left--;
        for (int i = 0; i < 9; ++i)
            ofx_spawn_ej(cx, cy, ofx_jet_dx, ofx_jet_dy, ofx_jet_col, 0, 10, 12);
    }
    for (int i = 0; i < OFX_EJ_MAX; ++i) {
        struct ofx_ejecta *e = &ofx_ej[i];
        if (!e->alive) continue;
        e->x += e->vx;
        e->y += e->vy;
        int32_t nvx = e->vx - (e->turn * (e->vy >> 5));
        int32_t nvy = e->vy + (e->turn * (e->vx >> 5));
        e->vx = (nvx * 246) >> 8;
        e->vy = (nvy * 246) >> 8;
        if (--e->life <= 0) e->alive = 0;
    }
    for (int i = 0; i < OFX_RING_MAX; ++i) {
        struct ofx_ring_t *g = &ofx_rings[i];
        if (!g->alive) continue;
        g->r += g->speed;
        if (--g->life <= 0 || g->r <= 26) g->alive = 0;
    }
}

static void ofx_frame_update(int32_t cx, int32_t cy)
{
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    uint32_t dt = ofx_last_ms ? (now - ofx_last_ms) : 33u;
    ofx_last_ms = now;
    if (dt > HER_ANIM_DT_MAX) dt = HER_ANIM_DT_MAX;          /* SOMA_153B: tras una pausa se reanuda sin salto */

    int sp = her_cur_speed;
    if (sp < 10) sp = 10;
    sp += soma_vortex_boost / 2;
    {
        int32_t tgt = (int32_t)sp * 256;
        if (ofx_sp_f == 0) ofx_sp_f = tgt;
        ofx_sp_f += ((tgt - ofx_sp_f) * (int32_t)dt) / 250;
        for (int k = 0; k < 5; ++k) {
            int64_t inc = (int64_t)dt * ofx_rib[k].spd * ofx_sp_f * 16;
            ofx_phf[k] += (uint64_t)inc;
            ofx_ph[k] = (uint32_t)(ofx_phf[k] >> 16);
        }
    }

    uint32_t dec = ofx_flash * dt * 3u / 1024u;
    if (ofx_flash && !dec) dec = 1u;
    ofx_flash = (dec >= ofx_flash) ? 0u : ofx_flash - dec;

    /* disparadores */
    int app = (int)current_app;
    if (ofx_prev_app >= 0 && app != ofx_prev_app && app != (int)APP_DESKTOP)
        ofx_trigger_app(cx, cy, app);
    ofx_prev_app = app;

    if (her_icons_visible && !ofx_prev_icons) ofx_trigger_icons(cx, cy);
    ofx_prev_icons = her_icons_visible;

    ofx_acc += dt;
    int ticks = 0;
    while (ofx_acc >= 33u && ticks < 3) { ofx_acc -= 33u; ticks++; ofx_tick(cx, cy); }
    if (ofx_acc > 99u) ofx_acc = 0u;
}

/* ---------- halo ---------- */
static void ofx_build_halo(uint32_t gain)               /* 256 = 1.0 */
{
    uint32_t c_in  = 0x00E8F6FFu;
    uint32_t c_mid = her_mix256(0x003B82F6u, her_col_cur & 0x00FFFFFFu, 70u);
    uint32_t c_out = 0x006D28D9u;
    for (uint32_t v = 0; v < 256u; ++v) {
        uint32_t col = (v < 36u) ? her_mix256(c_in, c_mid, v * 256u / 36u)
                                 : her_mix256(c_mid, c_out, (v - 36u) * 256u / 219u);
        uint32_t glow = 255u * 40u / (40u + v);
        glow = glow * (255u - v) / 255u;
        uint32_t k = glow * gain / 560u;
        if (k > 256u) k = 256u;
        ofx_halo[v] = ofx_mul(col, k);
    }
}

/* SOMA_164B_T480S: el halo por franjas de filas entre los nucleos. Cada franja son pasos enteros de OFX_HALO_STEP filas, asi
 * que ningun bloque de pixeles cae en dos franjas; la suma aditiva es la misma en cualquier orden: resultado identico. */
static void ofx_halo_band(volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R, int32_t s0, int32_t s1)
{
    int32_t R2 = R * R;
    uint32_t kmul = (uint32_t)(255 * 65536 / R2);
    for (int32_t st = s0; st < s1; ++st) {
        int32_t y = cy - R + st * OFX_HALO_STEP;
        if (y < 0 || y >= h) continue;
        int32_t dy = y - cy, dy2 = dy * dy;
        for (int32_t x = cx - R; x < cx + R; x += OFX_HALO_STEP) {
            if (x < 0 || x >= w) continue;
            if (ofx_occ_on && y >= ofx_occ_y0 && y + OFX_HALO_STEP <= ofx_occ_y1 && x >= ofx_occ_x0 && x < ofx_occ_x1) {
                x = ofx_occ_x1 - 1; while ((x - (cx - R)) % OFX_HALO_STEP) x--; continue;   /* SOMA_153: saltar el tramo tapado */
            }
            int32_t dx = x - cx;
            int32_t d2 = dx * dx + dy2;
            if (d2 >= R2 || d2 < 484) continue;
            uint32_t col = ofx_halo[(((uint32_t)d2 * kmul) >> 16) & 255u];
            if (!col) continue;
            for (int32_t sy = 0; sy < OFX_HALO_STEP; ++sy)
                for (int32_t sx = 0; sx < OFX_HALO_STEP; ++sx)
                    ofx_px(bb, w, h, x + sx, y + sy, col);
        }
    }
}
struct ofx_halo_job { volatile uint32_t *bb; int32_t w, h, cx, cy, R; };
static void ofx_halo_part(int idx, int n, void *arg)
{
    struct ofx_halo_job *j = (struct ofx_halo_job *)arg;
    int32_t steps = (2 * j->R + OFX_HALO_STEP - 1) / OFX_HALO_STEP;
    ofx_halo_band(j->bb, j->w, j->h, j->cx, j->cy, j->R, (int32_t)((int64_t)steps * idx / n), (int32_t)((int64_t)steps * (idx + 1) / n));
}
static int ofx_par_n = 0;                                   /* 0 = todos los nucleos; 1 = forzar uno (autoprueba) */
static void ofx_draw_halo(volatile uint32_t *bb, int32_t w, int32_t h,
                          int32_t cx, int32_t cy, int32_t R)
{
    struct ofx_halo_job j = { bb, w, h, cx, cy, R };
    smp_parallel_n(ofx_halo_part, &j, ofx_par_n ? ofx_par_n : 16);
}

/* ---------- cintas de seda estilo XMB (PARCHE V2: continuidad subpixel) ---------- */
static int32_t ofx_sin16(uint32_t a)          /* a: 1/16384 de vuelta -> +-1024 */
{
    uint32_t i = (a >> 6) & 255u, f = a & 63u;
    int32_t s0 = HER_S[i], s1 = HER_S[(i + 1u) & 255u];
    return s0 + ((s1 - s0) * (int32_t)f) / 64;
}
static int32_t ofx_cos16(uint32_t a) { return ofx_sin16(a + 4096u); }

static uint32_t ofx_dbl(uint32_t c)
{
    uint32_t r = ((c >> 16) & 255u) * 2u, g = ((c >> 8) & 255u) * 2u, b = (c & 255u) * 2u;
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    return (r << 16) | (g << 8) | b;
}

/* Reparto bilineal de un punto en coordenadas x1024 */
static void ofx_splat(volatile uint32_t *bb, int32_t w, int32_t h,
                      int32_t x1024, int32_t y1024, uint32_t col)
{
    int32_t ix = x1024 >> 10, iy = y1024 >> 10;
    uint32_t fx = (uint32_t)(x1024 & 1023) >> 2;
    uint32_t fy = (uint32_t)(y1024 & 1023) >> 2;
    uint32_t w00 = ((256u - fx) * (256u - fy)) >> 8;
    uint32_t w10 = (fx * (256u - fy)) >> 8;
    uint32_t w01 = ((256u - fx) * fy) >> 8;
    uint32_t w11 = (fx * fy) >> 8;
    if (w00) ofx_px(bb, w, h, ix,     iy,     ofx_mul(col, w00));
    if (w10) ofx_px(bb, w, h, ix + 1, iy,     ofx_mul(col, w10));
    if (w01) ofx_px(bb, w, h, ix,     iy + 1, ofx_mul(col, w01));
    if (w11) ofx_px(bb, w, h, ix + 1, iy + 1, ofx_mul(col, w11));
}

static void ofx_draw_ribbons(volatile uint32_t *bb, int32_t w, int32_t h,
                             int32_t cx, int32_t cy, int32_t R, uint32_t gain)
{
    int32_t t0 = 90 * R / 216;
    for (int k = 0; k < OFX_RIB_N; ++k) {
        const struct ofx_rib_cfg *rc = &ofx_rib[k];
        int32_t base16 = (int32_t)rc->rad * R * 16 / 216;
        int32_t amp16  = (int32_t)rc->amp * R * 16 / 216;
        int32_t wid16  = (int32_t)rc->wid * R * 16 / 216;
        if (wid16 < 32) wid16 = 32;

        uint64_t ph = ofx_phf[k];
        uint32_t head16 = (uint32_t)(ph >> 24) & 16383u;
        uint32_t wph16  = (uint32_t)(ph >> 23) & 16383u;
        uint32_t tl16 = (uint32_t)((int32_t)rc->tilt * 16 +
                        ((ofx_sin16((uint32_t)(ph >> 26) & 16383u) * 480) >> 10)) & 16383u;
        int32_t ct = ofx_cos16(tl16), st = ofx_sin16(tl16);
        uint32_t rcol = her_mix256(rc->col, her_col_cur & 0x00FFFFFFu, 70u);
        const uint32_t lead16 = 384u;

        for (uint32_t a = 0; a < 1024u; a += OFX_RIB_STEP) {
            int32_t ca = her_cs(a), sa = her_sn(a);
            int32_t wave = ofx_sin16(((a * (uint32_t)rc->harm) << 4) + wph16);
            int32_t r0_16 = base16 + ((amp16 * wave) >> 10);
            int32_t pin = ofx_sin16((a << 5) + (wph16 >> 1));
            int32_t wv16 = 16 + ((wid16 * (1024 + pin)) >> 11);

            uint32_t tail16 = (head16 - (a << 4)) & 16383u;
            uint32_t inten;
            if (tail16 > 16384u - lead16) {            /* borde de entrada suave */
                uint32_t d = 16384u - tail16;
                inten = 28u + (228u * (lead16 - d)) / lead16;
            } else {
                inten = 28u + (228u * (16383u - tail16)) / 16383u;
            }
            uint32_t depth = 192u + (uint32_t)(((sa + 1024) * 64) >> 11);
            uint32_t kcol = (inten * depth) >> 8;
            kcol = (kcol * rc->alpha) >> 8;
            kcol = (kcol * gain) >> 8;
            if (kcol > 256u) kcol = 256u;
            uint32_t bc = ofx_mul(rcol, kcol);
            if (!bc) continue;

            int32_t smax = (wv16 >> 4) + 1;
            uint32_t den = (uint32_t)(wv16 + 16);
            for (int32_t s = -smax; s <= smax; ++s) {
                int32_t as = s < 0 ? -s : s;
                int32_t num = wv16 + 16 - as * 16;
                if (num <= 0) continue;
                uint32_t fq = ((uint32_t)num * 256u) / den;
                fq = (fq * fq) >> 8;
                int32_t rr16 = r0_16 + s * 16;
                int32_t lx = (rr16 * ca) >> 4;                       /* px*1024 */
                int32_t ly = (((rr16 * sa) >> 4) * (int32_t)rc->sq) >> 8;
                int32_t X = (lx * ct - ly * st) >> 10;
                int32_t Y = (lx * st + ly * ct) >> 10;
                if (ofx_occ_on) {                        /* SOMA_153: los 4 pixeles del reparto caen bajo la ventana opaca */
                    int32_t px0 = cx + (X >> 10), py0 = cy + (Y >> 10);
                    if (ofx_occluded(px0, py0) && ofx_occluded(px0 + 1, py0 + 1)) continue;
                }
                int32_t ady = (Y < 0 ? -Y : Y) >> 10;
                uint32_t att = 256u;
                if (ady > t0) {
                    int32_t t = 256 - (ady - t0) * 3;
                    att = (t < 40) ? 40u : (uint32_t)t;
                }
                uint32_t add = ofx_mul(bc, (fq * att) >> 8);
                if (!add) continue;
                ofx_splat(bb, w, h, cx * 1024 + X, cy * 1024 + Y, ofx_dbl(add));
            }
        }
    }
}

/* SOMA_153B_CINTAS: las cintas del orbe se acumulan a MEDIA resolucion y se amplian x2 con interpolacion bilineal,
 * recorriendo solo los pixeles con contenido (la bilineal es la suma de una "tienda" 3x3 por pixel de media resolucion).
 * Antes, a resolucion completa, las cintas finas salian punteadas (muestras a ~1,9 px) y los huecos parpadeaban al
 * moverse; ahora son continuas. Mismo brillo medio. En SOMA: ~35 % menos ciclos. -DSOMA_ORB_RIB_HALF=0 = original. */
#ifndef SOMA_ORB_RIB_HALF
#define SOMA_ORB_RIB_HALF 1
#endif
#define RB_TS 8              /* teselas de 8x8 (media res) marcadas como tocadas */
/* SOMA_154: el buffer de media resolucion vive en el monton y se dimensiona segun el radio real del orbe.
 * SOMA_164B_T480S: un buffer por cinta, para pintarlas a la vez en varios nucleos (fase 1); la ampliacion x2 se reparte por
 * franjas de filas de pantalla (fase 2) y cada nucleo escribe solo las suyas. Las sumas son saturadas y todas positivas, asi
 * que el orden no cambia nada: el resultado es identico al de un solo nucleo (lo comprueba 'orbe prueba'). */
struct rb_ctx { uint32_t *acc[OFX_RIB_N]; uint8_t *tile[OFX_RIB_N]; int32_t w, h, tw; };
static struct rb_ctx g_rb;
static inline void rb_add(uint32_t *acc, uint8_t *tile, int32_t rw, int32_t rtw, int x, int y, uint32_t a)
{
    if ((unsigned)x >= (unsigned)rw || (unsigned)y >= (unsigned)rw || !a) return;
    uint32_t *p = &acc[y * rw + x];
    uint32_t d = *p;
    uint32_t r = ((d >> 16) & 255u) + ((a >> 16) & 255u), g = ((d >> 8) & 255u) + ((a >> 8) & 255u), b = (d & 255u) + (a & 255u);
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    *p = (r << 16) | (g << 8) | b;
    tile[(y / RB_TS) * rtw + x / RB_TS] = 1;
}
static void rb_splat(uint32_t *acc, uint8_t *tile, int32_t rw, int32_t rtw, int32_t x1024, int32_t y1024, uint32_t col)
{
    int32_t ix = x1024 >> 10, iy = y1024 >> 10;
    uint32_t fx = (uint32_t)(x1024 & 1023) >> 2, fy = (uint32_t)(y1024 & 1023) >> 2;
    uint32_t w00 = ((256u - fx) * (256u - fy)) >> 8, w10 = (fx * (256u - fy)) >> 8, w01 = ((256u - fx) * fy) >> 8, w11 = (fx * fy) >> 8;
    if (w00) rb_add(acc, tile, rw, rtw, ix, iy, ofx_mul(col, w00));
    if (w10) rb_add(acc, tile, rw, rtw, ix + 1, iy, ofx_mul(col, w10));
    if (w01) rb_add(acc, tile, rw, rtw, ix, iy + 1, ofx_mul(col, w01));
    if (w11) rb_add(acc, tile, rw, rtw, ix + 1, iy + 1, ofx_mul(col, w11));
}
static inline uint32_t rb_lerp4(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t fx, uint32_t fy)   /* fx,fy 0..256 */
{
    uint32_t w00 = (256u - fx) * (256u - fy), w10 = fx * (256u - fy), w01 = (256u - fx) * fy, w11 = fx * fy;
    uint32_t r = (((a >> 16) & 255u) * w00 + ((b >> 16) & 255u) * w10 + ((c >> 16) & 255u) * w01 + ((d >> 16) & 255u) * w11) >> 16;
    uint32_t g = (((a >> 8) & 255u) * w00 + ((b >> 8) & 255u) * w10 + ((c >> 8) & 255u) * w01 + ((d >> 8) & 255u) * w11) >> 16;
    uint32_t bl = ((a & 255u) * w00 + (b & 255u) * w10 + (c & 255u) * w01 + (d & 255u) * w11) >> 16;
    return (r << 16) | (g << 8) | bl;
}
static int rb_ensure(struct rb_ctx *c, int32_t need)       /* solo en el hilo que llama (los trabajos no reservan memoria) */
{
    if (c->acc[0] && need <= c->w) return 0;
    uint32_t *na[OFX_RIB_N]; uint8_t *nt[OFX_RIB_N]; int ok = 1;
    for (int k = 0; k < OFX_RIB_N; k++) {
        na[k] = (uint32_t *)kmalloc(sizeof(uint32_t) * (uint32_t)need * (uint32_t)need);
        nt[k] = (uint8_t *)kmalloc((uint32_t)(need / RB_TS) * (uint32_t)(need / RB_TS));
        if (!na[k] || !nt[k]) ok = 0;
    }
    if (!ok) { for (int k = 0; k < OFX_RIB_N; k++) { if (na[k]) kfree(na[k]); if (nt[k]) kfree(nt[k]); } return -1; }
    for (int k = 0; k < OFX_RIB_N; k++) {
        if (c->acc[k]) kfree(c->acc[k]);
        if (c->tile[k]) kfree(c->tile[k]);
        memset(na[k], 0, sizeof(uint32_t) * (uint32_t)need * (uint32_t)need);
        memset(nt[k], 0, (uint32_t)(need / RB_TS) * (uint32_t)(need / RB_TS));
        c->acc[k] = na[k]; c->tile[k] = nt[k];
    }
    c->w = c->h = need; c->tw = need / RB_TS;
    return 0;
}
struct rb_job { struct rb_ctx *c; volatile uint32_t *bb; int32_t w, h, cx, cy, R, ox, oy, y0, y1; uint32_t gain; };
/* fase 1: cada cinta en su buffer (antes se borra lo que toco en el fotograma anterior) */
static void rb_paint_part(int idx, int n, void *arg)
{
    struct rb_job *j = (struct rb_job *)arg; struct rb_ctx *c = j->c;
    int32_t cx = j->cx, cy = j->cy, R = j->R, ox = j->ox, oy = j->oy;
    int32_t t0 = 90 * R / 216;
    for (int k = idx; k < OFX_RIB_N; k += n) {
        uint32_t *acc = c->acc[k]; uint8_t *tile = c->tile[k];
        for (int ty = 0; ty < c->h / RB_TS; ty++) for (int tx = 0; tx < c->tw; tx++) if (tile[ty * c->tw + tx]) {
            tile[ty * c->tw + tx] = 0;
            for (int y = ty * RB_TS; y < (ty + 1) * RB_TS; y++) memset(&acc[y * c->w + tx * RB_TS], 0, RB_TS * 4);
        }
        const struct ofx_rib_cfg *rc = &ofx_rib[k];
        int32_t base16 = (int32_t)rc->rad * R * 16 / 216, amp16 = (int32_t)rc->amp * R * 16 / 216, wid16 = (int32_t)rc->wid * R * 16 / 216;
        if (wid16 < 32) wid16 = 32;
        uint64_t ph = ofx_phf[k];
        uint32_t head16 = (uint32_t)(ph >> 24) & 16383u, wph16 = (uint32_t)(ph >> 23) & 16383u;
        uint32_t tl16 = (uint32_t)((int32_t)rc->tilt * 16 + ((ofx_sin16((uint32_t)(ph >> 26) & 16383u) * 480) >> 10)) & 16383u;
        int32_t ct = ofx_cos16(tl16), st = ofx_sin16(tl16);
        uint32_t rcol = her_mix256(rc->col, her_col_cur & 0x00FFFFFFu, 70u);
        const uint32_t lead16 = 384u;
        for (uint32_t a = 0; a < 1024u; a++) {
            int32_t ca = her_cs(a), sa = her_sn(a);
            int32_t wave = ofx_sin16(((a * (uint32_t)rc->harm) << 4) + wph16);
            int32_t r0_16 = base16 + ((amp16 * wave) >> 10);
            int32_t pin = ofx_sin16((a << 5) + (wph16 >> 1));
            int32_t wv16 = 16 + ((wid16 * (1024 + pin)) >> 11);
            uint32_t tail16 = (head16 - (a << 4)) & 16383u, inten;
            if (tail16 > 16384u - lead16) { uint32_t d = 16384u - tail16; inten = 28u + (228u * (lead16 - d)) / lead16; }
            else inten = 28u + (228u * (16383u - tail16)) / 16383u;
            uint32_t depth = 192u + (uint32_t)(((sa + 1024) * 64) >> 11);
            uint32_t kcol = (inten * depth) >> 8; kcol = (kcol * rc->alpha) >> 8; kcol = (kcol * j->gain) >> 8; if (kcol > 256u) kcol = 256u;
            uint32_t bc = ofx_mul(rcol, kcol);
            if (!bc) continue;
            int32_t smax = (wv16 >> 4) + 1; uint32_t den = (uint32_t)(wv16 + 16);
            uint32_t inv = (256u << 16) / den;                 /* una division por angulo en vez de una por muestra */
            int32_t s0 = -smax;
            int32_t rr0 = r0_16 + s0 * 16;
            int32_t lx = (rr0 * ca) >> 4, ly = (((rr0 * sa) >> 4) * (int32_t)rc->sq) >> 8;
            int32_t dlx = (32 * ca) >> 4, dly = (((32 * sa) >> 4) * (int32_t)rc->sq) >> 8;
            int32_t X = (lx * ct - ly * st) >> 10, Y = (lx * st + ly * ct) >> 10;
            int32_t dX = (dlx * ct - dly * st) >> 10, dY = (dlx * st + dly * ct) >> 10;
            int32_t bx = (cx - ox) * 1024, by = (cy - oy) * 1024;
            for (int32_t s = s0; s <= smax; s += 2, X += dX, Y += dY) {
                int32_t as = s < 0 ? -s : s, num = wv16 + 16 - as * 16;
                if (num <= 0) continue;
                uint32_t fq = ((uint32_t)num * inv) >> 16; fq = (fq * fq) >> 8;
                int32_t ady = (Y < 0 ? -Y : Y) >> 10; uint32_t att = 256u;
                if (ady > t0) { int32_t t = 256 - (ady - t0) * 3; att = (t < 40) ? 40u : (uint32_t)t; }
                uint32_t add = ofx_mul(bc, (fq * att) >> 8);
                if (!add) continue;
                if (ofx_occ_on) {                        /* SOMA_153: muestra bajo la ventana opaca (con 3 px de margen por la ampliacion) */
                    int32_t qx = cx + (X >> 10), qy = cy + (Y >> 10);
                    if (ofx_occluded(qx - 3, qy - 3) && ofx_occluded(qx + 3, qy + 3)) continue;
                }
                rb_splat(acc, tile, c->w, c->tw, (bx + X) >> 1, (by + Y) >> 1, add);
            }
        }
    }
}
static inline uint32_t sat_add(uint32_t d, uint32_t a)
{
    uint32_t r = ((d >> 16) & 255u) + ((a >> 16) & 255u), g = ((d >> 8) & 255u) + ((a >> 8) & 255u), b = (d & 255u) + (a & 255u);
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    return (r << 16) | (g << 8) | b;
}
static inline void px_band(volatile uint32_t *bb, int32_t w, int32_t h, int32_t y0, int32_t y1, int32_t x, int32_t y, uint32_t c)
{
    if (y >= y0 && y < y1) ofx_px(bb, w, h, x, y, c);
}
/* fase 2: suma de las cintas + ampliacion x2 (tienda 3x3) en las filas de pantalla [Y0, Y1) de este trozo */
static void rb_up_part(int idx, int n, void *arg)
{
    struct rb_job *j = (struct rb_job *)arg; struct rb_ctx *c = j->c;
    int32_t span = j->y1 - j->y0;
    int32_t Y0 = j->y0 + (int32_t)((int64_t)span * idx / n), Y1 = j->y0 + (int32_t)((int64_t)span * (idx + 1) / n);
    int32_t ya = (Y0 - j->oy - 1) / 2 - 1, yb = (Y1 - j->oy + 1) / 2 + 1;     /* filas de media resolucion que tocan la franja */
    if (ya < 0) ya = 0;
    if (yb > c->h) yb = c->h;
    for (int ty = ya / RB_TS; ty <= (yb - 1) / RB_TS && ty < c->h / RB_TS; ty++) for (int tx = 0; tx < c->tw; tx++) {
        int any = 0;
        for (int k = 0; k < OFX_RIB_N; k++) if (c->tile[k][ty * c->tw + tx]) { any = 1; break; }
        if (!any) continue;
        for (int y = ty * RB_TS; y < (ty + 1) * RB_TS; y++) {
            if (y < ya || y >= yb) continue;
            int py = j->oy + 2 * y;
            if (py + 1 < Y0 || py - 1 >= Y1) continue;
            for (int x = tx * RB_TS; x < (tx + 1) * RB_TS; x++) {
                uint32_t cc = 0;
                for (int k = 0; k < OFX_RIB_N; k++) { uint32_t v = c->acc[k][y * c->w + x]; if (v) cc = sat_add(cc, v); }
                if (!cc) continue;
                uint32_t c2 = ofx_mul(cc, 128u), c4 = ofx_mul(cc, 64u);
                int px = j->ox + 2 * x;
                volatile uint32_t *bb = j->bb; int32_t w = j->w, h = j->h;
                px_band(bb, w, h, Y0, Y1, px, py, cc);
                px_band(bb, w, h, Y0, Y1, px - 1, py, c2); px_band(bb, w, h, Y0, Y1, px + 1, py, c2);
                px_band(bb, w, h, Y0, Y1, px, py - 1, c2); px_band(bb, w, h, Y0, Y1, px, py + 1, c2);
                px_band(bb, w, h, Y0, Y1, px - 1, py - 1, c4); px_band(bb, w, h, Y0, Y1, px + 1, py - 1, c4);
                px_band(bb, w, h, Y0, Y1, px - 1, py + 1, c4); px_band(bb, w, h, Y0, Y1, px + 1, py + 1, c4);
            }
        }
    }
}
static void ofx_draw_ribbons(volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R, uint32_t gain);
static void rb_draw(struct rb_ctx *c, volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R, uint32_t gain)
{
    int32_t ext = 0;                                   /* alcance maximo de las cintas en px completos, con margen */
    for (int k = 0; k < OFX_RIB_N; ++k) {
        int32_t amp = (int32_t)ofx_rib[k].amp; if (amp < 0) amp = -amp;
        int32_t e = ((int32_t)ofx_rib[k].rad + amp + (int32_t)ofx_rib[k].wid) * R / 216;
        if (e > ext) ext = e;
    }
    ext += 8;
    int32_t need = (ext + RB_TS - 1) / RB_TS * RB_TS;
    if (need < 64) need = 64;
    if (rb_ensure(c, need) != 0) { ofx_draw_ribbons(bb, w, h, cx, cy, R, gain); return; }
    struct rb_job j = { c, bb, w, h, cx, cy, R, cx - c->w, cy - c->h, 0, 0, gain };
    j.y0 = j.oy - 1; if (j.y0 < 0) j.y0 = 0;
    j.y1 = j.oy + 2 * c->h + 1; if (j.y1 > h) j.y1 = h;
    int np = ofx_par_n ? ofx_par_n : 16;
    smp_parallel_n(rb_paint_part, &j, np < OFX_RIB_N ? np : OFX_RIB_N);
    if (j.y1 > j.y0) smp_parallel_n(rb_up_part, &j, np);
}
static void ofx_draw_ribbons_fast(volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R, uint32_t gain)
{
    rb_draw(&g_rb, bb, w, h, cx, cy, R, gain);
}

/* SOMA_164B_T480S: 'orbe prueba': el halo y las cintas con 1 nucleo y con todos deben dar exactamente los mismos pixeles */
static void ot_puts(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; if (max) o[*p < max ? *p : max - 1] = 0; }
static void ot_dec(char *o, uint32_t max, uint32_t *p, uint32_t v) { char d[12]; int n = 0; do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v); char t[12]; int k = 0; while (n) t[k++] = d[--n]; t[k] = 0; ot_puts(o, max, p, t); }
int orb_parallel_selftest(char *out, uint32_t max)
{
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    uint32_t p = 0;
    if (w <= 0 || h <= 0) { ot_puts(out, max, &p, "orbe: sin pantalla\n"); return 1; }
    uint32_t n = (uint32_t)w * (uint32_t)h;
    uint32_t *b1 = (uint32_t *)kmalloc(n * 4u), *b2 = (uint32_t *)kmalloc(n * 4u);
    static struct rb_ctx c1, c2;
    if (!b1 || !b2) { if (b1) kfree(b1); if (b2) kfree(b2); ot_puts(out, max, &p, "orbe: sin memoria\n"); return 1; }
    int32_t R = ofx_radius(h), cx = w / 2, cy = h / 2;
    int bad = 0; uint64_t tt[2] = { 0, 0 };
    for (int round = 0; round < 4 && !bad; round++) {
        uint32_t gain = 150u + 120u * (uint32_t)round;
        for (int pass = 0; pass < 2; pass++) {
            uint32_t *b = pass ? b2 : b1;
            for (uint32_t i = 0; i < n; i++) b[i] = (i * 2654435761u >> 26) * 0x010101u;   /* fondo con algo de textura */
            ofx_par_n = pass ? 0 : 1;
            uint64_t t0 = timer_get_uptime_ms();
            ofx_build_halo(gain);
            ofx_draw_halo(b, w, h, cx, cy, R);
            rb_draw(pass ? &c2 : &c1, b, w, h, cx, cy, R, gain);
            tt[pass] += timer_get_uptime_ms() - t0;
        }
        ofx_par_n = 0;
        for (uint32_t i = 0; i < n; i++) if (b1[i] != b2[i]) { bad = 1; break; }
        for (int k = 0; k < OFX_RIB_N; ++k) ofx_phf[k] += 0x0123456789ULL * (uint64_t)(k + 1);   /* otra postura de las cintas */
    }
    kfree(b1); kfree(b2);
    ot_puts(out, max, &p, bad ? "orbe: FALLO: con varios nucleos el orbe no sale igual que con uno\n" : "orbe: halo y cintas identicos pixel a pixel con 1 y con todos los nucleos (4 posturas)\n");
    ot_puts(out, max, &p, "  1 nucleo: "); ot_dec(out, max, &p, (uint32_t)tt[0]); ot_puts(out, max, &p, " ms | todos: "); ot_dec(out, max, &p, (uint32_t)tt[1]); ot_puts(out, max, &p, " ms\n");
    return 1;
}

/* ---------- pasada trasera: halo + cintas (antes de las particulas) ---------- */
/* SOMA_164B_T480S: lo que cuesta cada parte de un fotograma del escritorio (ciclos del TSC), media de 60 fotogramas; 'orbe' */
static uint64_t tmp_pf[8], tmp_rd[8]; static uint32_t tmp_pfn;
uint64_t orb_perf_last[16]; uint32_t orb_perf_period, orb_perf_windows;
#ifdef __x86_64__
static inline uint64_t tmp_tsc(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return (uint64_t)hi << 32 | lo; }
#else
static inline uint64_t tmp_tsc(void) { uint64_t v; __asm__ volatile ("isb; mrs %0, cntvct_el0" : "=r"(v)); return v; }
#endif
/* ====================================================================================================================
 * SOMA_209_ECO: menos trabajo por fotograma sin cambiar la imagen.
 *   1. Halo: la geometria (que entrada de la tabla de color toca a cada pixel) se calcula una vez; cada fotograma solo consulta.
 *   2. Borrado y volcado por zonas tambien con ventanas abiertas (antes, pantalla entera en cada fotograma).
 *   3. El contenido de una ventana que no ha cambiado se copia de su ultima pintura en vez de volver a dibujarse.
 *   4. Las cintas se pintan en una capa propia cada OFX_RIBL_MS y se suman al fondo en cada fotograma.
 * 1-2 dan los mismos pixeles ('orbe eco off' vuelve al camino anterior). 3 y 4 dan los mismos pixeles en cada instante en que se
 * renuevan; entre medias (70-80 ms) el cursor de texto y las cintas no avanzan ('orbe ventanas off', 'orbe cintas off').
 * 'orbe verifica' lo comprueba.
 * ==================================================================================================================== */
#ifndef OFX_RIBL_MS
#define OFX_RIBL_MS 80u
#endif
int her_eco_on = 1, her_ribl_on = 1, her_eco_verify = 0;
uint32_t her_eco_stat[10];   /* 0 capas de cintas pintadas, 1 reutilizadas, 2 cuerpos copiados, 3 cuerpos pintados, 4 cuerpos distintos (verifica),
                              * 5 pixeles sueltos (verifica), 6 pixeles de pantalla != backbuffer (verifica), 7 fotogramas verificados */
static uint32_t her_eco_period = 33u, her_eco_vn;

static uint16_t *hm_map; static int32_t *hm_span; static int32_t hm_R = -1, hm_side;
static int ofx_halo_map_build(int32_t R)
{
    if (hm_map && hm_R == R) return 1;
    if (hm_map) { kfree(hm_map); hm_map = 0; }
    if (hm_span) { kfree(hm_span); hm_span = 0; }
    hm_R = -1;
    int32_t side = 2 * R + OFX_HALO_STEP;
    hm_map = (uint16_t *)kmalloc((size_t)side * (size_t)side * sizeof(uint16_t));
    hm_span = (int32_t *)kmalloc((size_t)side * 2u * sizeof(int32_t));
    if (!hm_map || !hm_span) { if (hm_map) kfree(hm_map); if (hm_span) kfree(hm_span); hm_map = 0; hm_span = 0; return 0; }
    for (int32_t i = 0; i < side * side; ++i) hm_map[i] = 256u;
    int32_t R2 = R * R;
    uint32_t kmul = (uint32_t)(255 * 65536 / R2);
    int32_t steps = (2 * R + OFX_HALO_STEP - 1) / OFX_HALO_STEP;
    for (int32_t st = 0; st < steps; ++st) {                     /* el mismo recorrido que ofx_halo_band */
        int32_t yy = st * OFX_HALO_STEP, dy = yy - R, dy2 = dy * dy;
        for (int32_t xx = 0; xx < 2 * R; xx += OFX_HALO_STEP) {
            int32_t dx = xx - R, d2 = dx * dx + dy2;
            if (d2 >= R2 || d2 < 484) continue;
            uint16_t idx = (uint16_t)((((uint32_t)d2 * kmul) >> 16) & 255u);
            for (int32_t sy = 0; sy < OFX_HALO_STEP; ++sy)
                for (int32_t sx = 0; sx < OFX_HALO_STEP; ++sx) hm_map[(yy + sy) * side + xx + sx] = idx;
        }
    }
    for (int32_t y = 0; y < side; ++y) {
        int32_t a = 0, b = side;
        while (a < side && hm_map[y * side + a] == 256u) ++a;
        while (b > a && hm_map[y * side + b - 1] == 256u) --b;
        hm_span[2 * y] = a; hm_span[2 * y + 1] = b;
    }
    hm_R = R; hm_side = side;
    return 1;
}
static void ofx_halo_fast(volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R)
{
    if (cx - R < 0 || cy - R < 0 || cx + R + OFX_HALO_STEP > w || cy + R + OFX_HALO_STEP > h || !ofx_halo_map_build(R)) {
        ofx_draw_halo(bb, w, h, cx, cy, R); return;
    }
    uint32_t lut[257];
    for (int v = 0; v < 256; ++v) lut[v] = ofx_halo[v] & 0x00FFFFFFu;
    lut[256] = 0;
    int32_t side = hm_side, bx = cx - R, by = cy - R;
    for (int32_t row = 0; row < side; ++row) {
        int32_t x0 = hm_span[2 * row], x1 = hm_span[2 * row + 1];
        if (x0 >= x1) continue;
        int32_t y = by + row;
        volatile uint32_t *p = bb + (uint32_t)y * (uint32_t)w + (uint32_t)bx;
        const uint16_t *m = hm_map + row * side;
        int32_t o0 = x1, o1 = x1;                                /* tramo tapado por la ventana superior (como ofx_occluded) */
        if (ofx_occ_on && y >= ofx_occ_y0 && y < ofx_occ_y1) { o0 = ofx_occ_x0 - bx; o1 = ofx_occ_x1 - bx; }
        for (int32_t x = x0; x < x1; ++x) {
            if (x >= o0 && x < o1) { x = o1 - 1; continue; }
            uint32_t a = lut[m[x]];
            if (!a) continue;
            uint32_t d = p[x];
            if (!(d & 0x00FFFFFFu)) { p[x] = a; continue; }
            uint32_t r = ((d >> 16) & 255u) + ((a >> 16) & 255u), g = ((d >> 8) & 255u) + ((a >> 8) & 255u), b = (d & 255u) + (a & 255u);
            if (r > 255u) r = 255u;
            if (g > 255u) g = 255u;
            if (b > 255u) b = 255u;
            p[x] = (r << 16) | (g << 8) | b;
        }
    }
}

static void ofx_ribbons_direct(volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R, uint32_t gain)
{
    if (SOMA_ORB_RIB_HALF && ofx_rib_mode) ofx_draw_ribbons_fast(bb, w, h, cx, cy, R, gain);
    else ofx_draw_ribbons(bb, w, h, cx, cy, R, gain);
}
/* Capa trasera del orbe (halo + cintas). Es lo primero que se dibuja sobre el fondo negro recien borrado, asi que guardar su
 * caja y volver a copiarla da exactamente los mismos pixeles mientras nada de lo que la determina cambie.
 *   - Halo solo: depende de la tabla de color, del radio y de la ventana que tapa. Si no cambian, se copia (identico siempre).
 *   - Halo + cintas: ademas las cintas avanzan con el tiempo. Con 'orbe cintas on' se reutilizan hasta OFX_RIBL_MS. */
static int ofx_bg_black;                                         /* 1 = el llamador garantiza fondo negro en la caja del orbe */
struct ob_key { int32_t R, cx, cy, w, h, occ[5]; uint32_t col; uint32_t lut[256]; };
static struct ob_key hk_key, ok_key; static int hk_valid, ok_valid;
static uint32_t *hk_px, *ok_px; static int32_t hk_side, ok_side; static uint64_t ok_ms;
static void ob_key_now(struct ob_key *k, int32_t R, int32_t cx, int32_t cy, int32_t w, int32_t h)
{
    k->R = R; k->cx = cx; k->cy = cy; k->w = w; k->h = h;
    k->occ[0] = ofx_occ_on; k->occ[1] = ofx_occ_on ? ofx_occ_x0 : 0; k->occ[2] = ofx_occ_on ? ofx_occ_y0 : 0;
    k->occ[3] = ofx_occ_on ? ofx_occ_x1 : 0; k->occ[4] = ofx_occ_on ? ofx_occ_y1 : 0;
    k->col = her_col_cur & 0x00FFFFFFu;
    memcpy(k->lut, ofx_halo, sizeof k->lut);
}
static int ob_key_eq(const struct ob_key *a, const struct ob_key *b)
{
    if (a->R != b->R || a->cx != b->cx || a->cy != b->cy || a->w != b->w || a->h != b->h || a->col != b->col) return 0;
    for (int i = 0; i < 5; ++i) if (a->occ[i] != b->occ[i]) return 0;
    return memcmp(a->lut, b->lut, sizeof a->lut) == 0;
}
static int32_t ob_extent(int32_t R)                              /* alcance de las cintas (como rb_draw) con margen */
{
    int32_t ext = 0;
    for (int k = 0; k < OFX_RIB_N; ++k) {
        int32_t amp = (int32_t)ofx_rib[k].amp; if (amp < 0) amp = -amp;
        int32_t e = ((int32_t)ofx_rib[k].rad + amp + (int32_t)ofx_rib[k].wid) * R / 216;
        if (e > ext) ext = e;
    }
    ext = (ext + 8 + RB_TS - 1) / RB_TS * RB_TS + 8;
    if (ext < R + 8) ext = R + 8;
    return ext;
}
static int ob_box_ok(int32_t cx, int32_t cy, int32_t e, int32_t w, int32_t h) { return cx - e >= 0 && cy - e >= 0 && cx + e <= w && cy + e <= h; }
static void ob_copy(uint32_t *buf, volatile uint32_t *bb, int32_t w, int32_t cx, int32_t cy, int32_t e, int to_screen)
{
    int32_t side = 2 * e;
    for (int32_t r = 0; r < side; ++r) {
        uint32_t *scr = (uint32_t *)(bb + (size_t)(cy - e + r) * (size_t)w + (size_t)(cx - e));
        if (to_screen) memcpy(scr, buf + (size_t)r * (size_t)side, (size_t)side * 4u);
        else memcpy(buf + (size_t)r * (size_t)side, scr, (size_t)side * 4u);
    }
}
static uint32_t *ob_buf(uint32_t **px, int32_t *side_now, int32_t e)
{
    if (*px && *side_now == 2 * e) return *px;
    if (*px) kfree(*px);
    *px = (uint32_t *)kmalloc((size_t)(2 * e) * (size_t)(2 * e) * 4u);
    *side_now = *px ? 2 * e : 0;
    return *px;
}
/* Halo y cintas del fotograma. Devuelve por t[0], t[1] el tiempo de cada parte. */
static void ofx_back_eco(volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R, uint32_t gain, uint64_t *t)
{
    uint64_t q0 = tmp_tsc(), now = timer_get_uptime_ms();
    int black = ofx_bg_black;
    int32_t E = ob_extent(R);
    struct ob_key k;
    ob_key_now(&k, R, cx, cy, w, h);
    if (black && her_ribl_on && ok_valid && ok_px && ok_side == 2 * E && now - ok_ms < OFX_RIBL_MS && ofx_flash <= 8u &&
        (SOMA_ORB_RIB_HALF && ofx_rib_mode) && ob_key_eq(&k, &ok_key) && ob_box_ok(cx, cy, E, w, h)) {
        ob_copy(ok_px, bb, w, cx, cy, E, 1);
        her_eco_stat[1]++;
        t[0] += tmp_tsc() - q0;
        return;
    }
    if (black && hk_valid && hk_px && hk_side == 2 * R + 2 * OFX_HALO_STEP && ob_key_eq(&k, &hk_key) && ob_box_ok(cx, cy, R + OFX_HALO_STEP, w, h)) {
        ob_copy(hk_px, bb, w, cx, cy, R + OFX_HALO_STEP, 1);
        her_eco_stat[8]++;
    } else {
        ofx_halo_fast(bb, w, h, cx, cy, R);
        hk_valid = 0;
        if (black && ob_box_ok(cx, cy, R + OFX_HALO_STEP, w, h) && ob_buf(&hk_px, &hk_side, R + OFX_HALO_STEP)) {
            ob_copy(hk_px, bb, w, cx, cy, R + OFX_HALO_STEP, 0); hk_key = k; hk_valid = 1;
        }
        her_eco_stat[9]++;
    }
    uint64_t q1 = tmp_tsc(); t[0] += q1 - q0;
    ofx_ribbons_direct(bb, w, h, cx, cy, R, gain);
    her_eco_stat[0]++;
    ok_valid = 0;
    static uint64_t prev_ms;                                     /* si los fotogramas llegan mas espaciados que la vida de la capa, no se guarda */
    int soon = now - prev_ms < OFX_RIBL_MS; prev_ms = now;
    if (black && her_ribl_on && soon && ofx_flash <= 8u && ob_box_ok(cx, cy, E, w, h) && ob_buf(&ok_px, &ok_side, E)) {
        ob_copy(ok_px, bb, w, cx, cy, E, 0); ok_key = k; ok_ms = now; ok_valid = 1;
    }
    t[1] += tmp_tsc() - q1;
}
/* 'orbe verifica': el halo por tabla y las cintas por capa deben dar exactamente los mismos pixeles que el camino directo,
 * sobre fondo negro y con textura, con y sin ventana tapando. */
int orb_eco_selftest(char *out, uint32_t max)
{
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    uint32_t p = 0;
    if (w <= 0 || h <= 0) { ot_puts(out, max, &p, "orbe: sin pantalla\n"); return 1; }
    uint32_t n = (uint32_t)w * (uint32_t)h;
    uint32_t *b1 = (uint32_t *)kmalloc(n * 4u), *b2 = (uint32_t *)kmalloc(n * 4u);
    if (!b1 || !b2) { if (b1) kfree(b1); if (b2) kfree(b2); ot_puts(out, max, &p, "orbe: sin memoria\n"); return 1; }
    int32_t R = ofx_radius(h), cx = w / 2, cy = h / 2;
    uint32_t bad_h = 0, bad_r = 0; int cases = 0;
    uint64_t ph[5]; for (int k = 0; k < 5; ++k) ph[k] = ofx_phf[k];
    int occ = ofx_occ_on; int32_t o[4] = { ofx_occ_x0, ofx_occ_y0, ofx_occ_x1, ofx_occ_y1 };
    for (int round = 0; round < 8; round++) {
        uint32_t gain = 150u + 60u * (uint32_t)round;
        int tex = round & 1, oc = (round >> 1) & 1;
        ofx_occ_on = oc; ofx_occ_x0 = cx - R / 2; ofx_occ_y0 = cy - R / 3; ofx_occ_x1 = cx + R; ofx_occ_y1 = cy + R * 2;
        ofx_build_halo(gain);
        for (int pass = 0; pass < 2; pass++) {
            uint32_t *b = pass ? b2 : b1;
            for (uint32_t i = 0; i < n; i++) b[i] = tex ? (i * 2654435761u >> 25) * 0x010101u : 0u;
            if (pass) ofx_halo_fast(b, w, h, cx, cy, R); else ofx_draw_halo(b, w, h, cx, cy, R);
        }
        for (uint32_t i = 0; i < n; i++) if (b1[i] != b2[i]) bad_h++;
        for (int k = 0; k < OFX_RIB_N; ++k) ofx_phf[k] += 0x0123456789ULL * (uint64_t)(k + 1);
        cases++;
    }
    for (int k = 0; k < 5; ++k) ofx_phf[k] = ph[k];
    (void)bad_r;
    ofx_occ_on = occ; ofx_occ_x0 = o[0]; ofx_occ_y0 = o[1]; ofx_occ_x1 = o[2]; ofx_occ_y1 = o[3];
    kfree(b1); kfree(b2);
    ot_puts(out, max, &p, "halo por tabla: "); ot_dec(out, max, &p, bad_h); ot_puts(out, max, &p, " pixeles distintos del halo original ("); ot_dec(out, max, &p, (uint32_t)cases); ot_puts(out, max, &p, " casos: fondo negro/textura, con/sin ventana)\n");
    return 1;
}

static void ofx_orb_back(uint32_t ucx, uint32_t ucy)
{
    int32_t cx = (int32_t)ucx, cy = (int32_t)ucy;
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb || w <= 0 || h <= 0) return;

    ofx_frame_update(cx, cy);

    int32_t R = ofx_radius(h);
    uint32_t gain = 150u + (uint32_t)her_cur_glow + ofx_flash +
                    (uint32_t)her_pulse / 2u + (uint32_t)her_energy / 2u;
    if (gain > 600u) gain = 600u;
    {   /* PARCHE V2: ganancia suavizada, sin parpadeos bruscos */
        static int32_t gs = 0;
        if (gs == 0) gs = (int32_t)gain;
        gs += ((int32_t)gain - gs) / 3;
        gain = (uint32_t)gs;
    }

    fb_damage(cx - R * 3 / 2 - 2, cy - R * 3 / 2 - 2, R * 3 + 4, R * 3 + 4);
    ofx_build_halo(gain);
#if SOMA_WEB_PERF_LOG
    uint64_t rc0 = her153_cycles();
#endif
    if (her_eco_on) ofx_back_eco(bb, w, h, cx, cy, R, gain, &tmp_rd[6]);
    else {
        uint64_t q0 = tmp_tsc();
        ofx_draw_halo(bb, w, h, cx, cy, R);
        uint64_t q1 = tmp_tsc(); tmp_rd[6] += q1 - q0;
        ofx_ribbons_direct(bb, w, h, cx, cy, R, gain);
        tmp_rd[7] += tmp_tsc() - q1;
    }
#if SOMA_WEB_PERF_LOG
    her153_rib_cycles += her153_cycles() - rc0;
#endif
}

/* ---------- pasada delantera: anillo fotonico, destello, ondas, expulsion ---------- */
static void ofx_orb_front(uint32_t ucx, uint32_t ucy)
{
    int32_t cx = (int32_t)ucx, cy = (int32_t)ucy;
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb || w <= 0 || h <= 0) return;

    int32_t R = ofx_radius(h);
    uint32_t fl = ofx_flash;

    /* anillo fotonico con efecto Doppler (un lado mas brillante, rotando) */
    {
        static const uint8_t wt[5] = { 90, 255, 190, 110, 50 };
        uint32_t head = (ofx_ph[0] >> 11) & 1023u;
        uint32_t pc = her_mix256(0x00FFFFFFu, her_col_cur & 0x00FFFFFFu, 90u);
        uint32_t ibase = 120u + fl / 2u + (uint32_t)her_pulse / 4u;
        for (uint32_t a = 0; a < 1024u; a += 2u) {
            int32_t c = her_cs(a), s = her_sn(a);
            uint32_t dop = 128u + (uint32_t)(her_cs((a - head) & 1023u) + 1024) / 16u;
            uint32_t k = (ibase * dop) >> 8;
            if (k > 256u) k = 256u;
            uint32_t c0 = ofx_mul(pc, k);
            for (int32_t rr = 29; rr <= 33; ++rr)
                ofx_px(bb, w, h, cx + ((rr * c) >> 10), cy + ((rr * s) >> 10),
                       ofx_mul(c0, wt[rr - 29]));
        }
    }

    /* destello anamorfico horizontal */
    {
        int32_t L = R * 13 / 10;
        uint32_t str = 70u + fl / 2u + (uint32_t)her_energy / 4u;
        uint32_t sc = her_mix256(0x007DD3FCu, her_col_cur & 0x00FFFFFFu, 100u);
        for (int32_t dx = 26; dx < L; ++dx) {
            uint32_t k = (uint32_t)(256 - (dx - 26) * 256 / (L - 26));
            k = (k * k) >> 8;
            k = (k * str) >> 8;
            if (k > 256u) k = 256u;
            uint32_t c1 = ofx_mul(sc, k);
            uint32_t c2 = ofx_mul(c1, 100u);
            ofx_px(bb, w, h, cx + dx, cy, c1);
            ofx_px(bb, w, h, cx - dx, cy, c1);
            ofx_px(bb, w, h, cx + dx, cy - 1, c2);
            ofx_px(bb, w, h, cx - dx, cy - 1, c2);
            ofx_px(bb, w, h, cx + dx, cy + 1, c2);
            ofx_px(bb, w, h, cx - dx, cy + 1, c2);
        }
    }

    int any = 0;

    /* ondas de choque */
    for (int i = 0; i < OFX_RING_MAX; ++i) {
        struct ofx_ring_t *g = &ofx_rings[i];
        if (!g->alive || g->maxlife <= 0) continue;
        any = 1;
        uint32_t f = (uint32_t)g->life * 256u / (uint32_t)g->maxlife;
        uint32_t c = ofx_mul(her_mix256(0x00FFFFFFu, g->col, 110u), (f * 3u) >> 2);
        ofx_draw_ring(bb, w, h, cx, cy, g->r, c);
    }

    /* particulas expulsadas con estela */
    for (int i = 0; i < OFX_EJ_MAX; ++i) {
        struct ofx_ejecta *e = &ofx_ej[i];
        if (!e->alive || e->maxlife <= 0) continue;
        any = 1;
        uint32_t f = (uint32_t)e->life * 256u / (uint32_t)e->maxlife;   /* 256 recien nacida */
        int32_t hx = e->x >> 8, hy = e->y >> 8;
        int32_t tx = (e->x - e->vx * 2) >> 8, ty = (e->y - e->vy * 2) >> 8;
        uint32_t hot = her_mix256(0x00FFFFFFu, e->col, 256u - f);
        uint32_t c_head = ofx_mul(hot, 128u + f / 2u);
        uint32_t c_tail = ofx_mul(e->col, f / 3u);
        ofx_line(bb, w, h, tx, ty, hx, hy, c_tail, c_head);
        if (f > 100u) {
            uint32_t g1 = ofx_mul(c_head, 90u);
            ofx_px(bb, w, h, hx + 1, hy, g1); ofx_px(bb, w, h, hx - 1, hy, g1);
            ofx_px(bb, w, h, hx, hy + 1, g1); ofx_px(bb, w, h, hx, hy - 1, g1);
        }
    }

    if (any) fb_damage(0, 0, w, h);
}
/* ================= fin SOMA_ORB_FX_PATCH (A) ================= */

static int her_summon_on;                                   /* SOMA_168B_ORBE: el orbe esta al frente */
/* SOMA_207_CAPA: con ventanas abiertas, el fondo (orbe, cintas, particulas, iconos orbitando) se guarda una vez por latido y se
 * reutiliza en los repintados por teclado o raton. Escribir en una terminal ya no recalcula el halo ni las cintas. La imagen es
 * la misma: solo se deja de recalcular lo que no ha cambiado desde el ultimo latido. */
#ifndef SOMA_LAYER_CACHE
#define SOMA_LAYER_CACHE 1
#endif
static uint32_t *her_lay_px; static size_t her_lay_cap;
static int32_t her_lay_x, her_lay_y, her_lay_w, her_lay_h;
static uint64_t her_lay_sig, her_lay_ms;
static int her_lay_fresh_req;                 /* 1 = el latido pide un fondo nuevo */
uint32_t her_lay_hits, her_lay_miss;          /* para 'orbe' */
int her_lay_on = SOMA_LAYER_CACHE;            /* 'orbe capa off' lo desactiva para comparar */
static int her_lay_bypass(void)
{
    return her_lay_fresh_req || her_mfly_on || her_icons_visible || her_min_drag >= 0 || (her_dp_kind && her_dp_moved) || (her_icons_anim_phase && her_icons_anim_phase < HER_ICON_ANIM_MAX) || her_summon_on ||
           agent_global_len > 0 || her_ans_len || her_thk_len || her_vortex_closing || ofx_flash > 8u ||
           (her_hint_msg && timer_get_uptime_ms() < her_hint_until) || her_num_windows <= 0;
}
static uint64_t her_lay_signature(uint32_t w, uint32_t h)
{
    uint64_t v = 1469598103934665603ull;
#define LAYH(x) do { v ^= (uint64_t)(uint32_t)(x); v *= 1099511628211ull; } while (0)
    LAYH(w); LAYH(h); LAYH(ofx_occ_on); LAYH(ofx_occ_x0); LAYH(ofx_occ_y0); LAYH(ofx_occ_x1); LAYH(ofx_occ_y1);
    LAYH(her_desk_ver); LAYH(her_min_count); LAYH(current_app); LAYH(her_icons_visible); LAYH(her_power_pending);
    for (int i = 0; i < her_num_windows; ++i) LAYH(her_windows[i].app);
    if (her_icons_visible) {   /* el nombre al pasar el raton, el punto de 'abierta' y las tarjetas de energia dependen de esto */
        LAYH(her_dock_hit(g_mouse_x_s, g_mouse_y_s, w, h)); LAYH(her_min_hit(g_mouse_x_s, g_mouse_y_s, w, h));
        LAYH(her_power_hit(g_mouse_x_s, g_mouse_y_s, w, h));
    }
    for (int i = 0; i < her_min_count; ++i) { LAYH(her_min_phase[i]); LAYH(her_minimized[i].app); LAYH(her_minimized[i].term_slot); }
#undef LAYH
    return v;
}
static void her_lay_box(uint32_t w, uint32_t h, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    int32_t R = ofx_radius((int32_t)h);
    int32_t e = R * 3 / 2 + 8;
    int32_t r = R + 55;
    if (r < 90) r = 90;
    if (r > (int32_t)h / 2 - 40) r = (int32_t)h / 2 - 40;
    if (r < 45) r = 45;
    if (her_min_count && r + 64 > e) e = r + 64;
    *x0 = (int32_t)w / 2 - e; *x1 = (int32_t)w / 2 + e; *y0 = (int32_t)h / 2 - e; *y1 = (int32_t)h / 2 + e;
    if (*x0 < 0) { *x0 = 0; }
    if (*y0 < 0) { *y0 = 0; }
    if (*x1 > (int32_t)w) { *x1 = (int32_t)w; }
    if (*y1 > (int32_t)h) { *y1 = (int32_t)h; }
}
uint32_t her_lay_why[16];
static int her_lay_use(uint32_t w, uint32_t h)
{
    if (!her_lay_on) { her_lay_miss++; return 0; }
    if (!her_lay_px) { her_lay_miss++; her_lay_why[0]++; return 0; }
    if (her_lay_bypass()) {
        her_lay_miss++; her_lay_why[1]++;
        her_lay_why[4] += her_lay_fresh_req; her_lay_why[5] += (her_min_drag >= 0); her_lay_why[6] += her_icons_visible; her_lay_why[7] += (her_icons_anim_phase != 0);
        her_lay_why[8] += her_summon_on; her_lay_why[9] += (agent_global_len > 0); her_lay_why[10] += (her_ans_len || her_thk_len);
        her_lay_why[11] += her_vortex_closing; her_lay_why[12] += (ofx_flash > 8u);
        return 0;
    }
    uint64_t now = timer_get_uptime_ms();
    if (now - her_lay_ms > 300u) { her_lay_miss++; her_lay_why[2]++; return 0; }
    if (her_lay_sig != her_lay_signature(w, h)) { her_lay_miss++; her_lay_why[3]++; return 0; }
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb) return 0;
    if (her_eco_on) fb_clear_changed(0x00000000); else fb_clear(0x00000000);
    for (int32_t r = 0; r < her_lay_h; ++r)
        memcpy((void *)(bb + (size_t)(her_lay_y + r) * w + (size_t)her_lay_x), her_lay_px + (size_t)r * (size_t)her_lay_w, (size_t)her_lay_w * 4u);
    fb_damage(her_lay_x, her_lay_y, her_lay_w, her_lay_h);
    her_lay_hits++;
    return 1;
}
static void her_lay_store(uint32_t w, uint32_t h)
{
    if (!her_lay_on || her_num_windows <= 0 || her_summon_on || (her_lay_bypass() && !her_lay_fresh_req)) { her_lay_sig = 0; return; }
    volatile uint32_t *bb = fb_get_buffer();
    int32_t x0, y0, x1, y1; her_lay_box(w, h, &x0, &y0, &x1, &y1);
    if (!bb || x1 <= x0 || y1 <= y0) return;
    size_t need = (size_t)(x1 - x0) * (size_t)(y1 - y0) * 4u;
    if (need > her_lay_cap) {
        if (her_lay_px) kfree(her_lay_px);
        her_lay_px = (uint32_t *)kmalloc(need); her_lay_cap = her_lay_px ? need : 0;
        if (!her_lay_px) { her_lay_sig = 0; return; }
    }
    her_lay_x = x0; her_lay_y = y0; her_lay_w = x1 - x0; her_lay_h = y1 - y0;
    for (int32_t r = 0; r < her_lay_h; ++r)
        memcpy(her_lay_px + (size_t)r * (size_t)her_lay_w, (const void *)(bb + (size_t)(her_lay_y + r) * w + (size_t)her_lay_x), (size_t)her_lay_w * 4u);
    her_lay_sig = her_lay_signature(w, h); her_lay_ms = timer_get_uptime_ms();
}

/* SOMA_208: con fotogramas lentos los paquetes del raton se acumulaban y se procesaban de uno en uno por vuelta del bucle (el
 * puntero iba a trompicones y los clics llegaban tarde). Los paquetes de solo movimiento se suman en un unico evento; el primero
 * que cambia el estado del boton se entrega aparte, asi que ninguna pulsacion ni suelta se pierde. */
static int her_mouse_poll_merged(int32_t *dx, int32_t *dy, int *clk)
{
    if (!mouse_poll(dx, dy, clk)) return 0;
    int held0 = mouse_left_held();
    for (int g = 0; g < 48 && !*clk; ++g) {
        int32_t ax, ay; int ac;
        if (!mouse_poll(&ax, &ay, &ac)) break;
        *dx += ax; *dy += ay;
        if (ac || mouse_left_held() != held0) { *clk = ac; break; }
    }
    return 1;
}

static void render_desktop(void)
{
    uint64_t r0 = tmp_tsc();
    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    if (her_in_refresh && her_lay_use(cur_w, cur_h)) { tmp_rd[0] += tmp_tsc() - r0; return; }

    /* SOMA_PARCHE_012_RENDER: en escritorio puro solo se borra lo dibujado el fotograma anterior
     * (en vez de 8,3 MB). Con ventanas abiertas, o cada 60 fotogramas, borrado completo. */
    static int prev_desktop_only = 0, prev_eco = 0;
    static uint32_t partial_frames = 0;
    int desktop_only = (her_num_windows == 0 && current_app == APP_DESKTOP);
    /* SOMA_209: con ventanas, cada una declara su rectangulo (her_refresh_all). Mientras se arrastra o redimensiona una ventana
     * (y dos fotogramas despues) se borra la pantalla entera como antes: ese camino repone fondo guardado sin declararlo. */
    static int eco_hold = 0;
    if (her_dragging || her_resizing) eco_hold = 3; else if (eco_hold) eco_hold--;
    int eco = her_eco_on && her_in_refresh && !her_summon_on && !eco_hold;
    int eco_ok = eco && prev_eco;
    prev_eco = eco;
    static uint64_t full_ms = 0;                                /* SOMA_209: borrado completo de seguridad al menos una vez por segundo */
    uint64_t rd_now = timer_get_uptime_ms();
    if (rd_now - full_ms >= 1000u) partial_frames = 60;
    if (her_mfly_on) partial_frames = 60;                   /* SOMA_223: las estelas cruzan toda la pantalla */
    if (partial_frames >= 60) full_ms = rd_now;
    if (((desktop_only && prev_desktop_only) || eco_ok) && partial_frames < 60) {
        fb_clear_changed(0x00000000);
        partial_frames++;
    } else {
        fb_clear(0x00000000);
        partial_frames = 0;
    }
    prev_desktop_only = desktop_only;
    uint64_t r1 = tmp_tsc(); tmp_rd[0] += r1 - r0;

    uint32_t orb_cx = cur_w / 2;
    uint32_t orb_cy = cur_h / 2;

    if (her_summon_on) {                                    /* SOMA_168B_ORBE: el orbe se dibuja delante de las ventanas */
        if (current_app != APP_DESKTOP) her_draw_window_chrome(cur_w, cur_h);
        return;
    }
    ofx_bg_black = 1;                                       /* recien borrado: la caja del orbe es negra */
    ofx_orb_back(orb_cx, orb_cy);
    ofx_bg_black = 0;
    uint64_t r2 = tmp_tsc(); tmp_rd[1] += r2 - r1;
    her_parts_update_draw();
    uint64_t r3 = tmp_tsc(); tmp_rd[2] += r3 - r2;
    her_draw_orb(orb_cx, orb_cy, her_angle);
    uint64_t r4 = tmp_tsc(); tmp_rd[3] += r4 - r3;
    ofx_orb_front(orb_cx, orb_cy);
    uint64_t r5 = tmp_tsc(); tmp_rd[4] += r5 - r4;
    her_draw_user_zone(cur_w, cur_h, orb_cy);
    her_draw_agent_zone(cur_w, cur_h, orb_cy);
    her_draw_dock(cur_w, cur_h);
    her_mfly_draw(cur_w, cur_h);                            /* SOMA_223_MINIMIZAR: antes que los iconos (fija el brillo del que llega) */
    her_draw_minimized_icons(cur_w, cur_h);
    if (her_num_windows == 0) hst_dibujar(cur_w, cur_h);

    if (current_app != APP_DESKTOP)
        her_draw_window_chrome(cur_w, cur_h);
    her_draw_vortex_particles();
    tmp_rd[5] += tmp_tsc() - r5;
    if (her_in_refresh) her_lay_store(cur_w, cur_h);

    if (fb_verify_enabled && desktop_only && partial_frames > 0)
        (void)fb_debug_verify(0x00000000);
}

static void agents_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int cursor_visible)
{
    (void)cursor_visible;
    if (ww < 180u || wh < 160u) return;
    fb_fill_rect(wx, wy, ww, wh, 0x000B1220);
    fb_fill_rounded_rect((int32_t)(wx + 12u), (int32_t)(wy + 10u), (int32_t)(ww - 24u), 30, 5, 0x00131E2A);
    font_draw_string(wx + 24u, wy + 20u, "AGENTES DISPONIBLES", 0x00F8FAFC, 1);
    font_draw_string(wx + 24u, wy + 48u, "Jerarquia de coordinacion de SOMA", 0x0064778A, 1);

    uint32_t root_x = wx + 24u, root_y = wy + 68u;
    uint32_t card_x = wx + 54u;
    uint32_t card_w = ww > 86u ? ww - 78u : ww - 62u;
    fb_fill_rounded_rect((int32_t)root_x, (int32_t)root_y, (int32_t)(ww - 48u), 48, 6, 0x00113343);
    fb_draw_rounded_rect((int32_t)root_x, (int32_t)root_y, (int32_t)(ww - 48u), 48, 6, COLOR_CYAN);
    font_draw_string(root_x + 12u, root_y + 10u, "SOMA  -  ORQUESTADOR PRINCIPAL", COLOR_CYAN, 1);
    font_draw_string(root_x + 12u, root_y + 28u, "Coordina especialistas y deriva operaciones de sistema.", 0x00CBD5E1, 1);

    uint32_t branch_x = wx + 38u;
    fb_fill_rect(branch_x, root_y + 48u, 2u, 230u, 0x00334155);

    uint32_t y1 = wy + 132u, y2 = wy + 220u, y3 = wy + 308u;
    uint32_t ys[3] = { y1, y2, y3 };
    const char *names[3] = { "Mayordomo  -  Operaciones del sistema", "Buscador  -  Web e investigacion", "Maquetador  -  Documentos y composicion" };
    const char *descs[3] = {
        "Ejecuta rapido RAM, disco, VFS, busquedas y gestion de archivos.",
        "Investiga informacion web, actualidad y consultas de conocimiento.",
        "Organiza contenido en documentos estructurados con layout SOMA."
    };
    const uint32_t colors[3] = { 0x0034D399, 0x00F59E0B, 0x000EA5E9 };
    for (int i = 0; i < 3; ++i) {
        fb_fill_rect(branch_x, ys[i] + 26u, 16u, 2u, 0x00334155);
        fb_fill_rounded_rect((int32_t)card_x, (int32_t)ys[i], (int32_t)card_w, 66, 6, 0x00101827);
        fb_draw_rounded_rect((int32_t)card_x, (int32_t)ys[i], (int32_t)card_w, 66, 6, colors[i]);
        font_draw_string(card_x + 12u, ys[i] + 12u, names[i], colors[i], 1);
        font_draw_string(card_x + 12u, ys[i] + 34u, descs[i], 0x00CBD5E1, 1);
    }
    font_draw_string(wx + 22u, wy + wh - 20u, "Tareas comunes: RAM | Disco | Buscar archivo | Crear carpeta", 0x0064778A, 1);
}

static void her_render_app_body(enum app_mode app, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int is_focused)
{
    uint8_t own_prev = acct_set_owner((uint8_t)(ACCT_APP0 + app));   /* PARCHE 089 */
    fb_set_clip((int32_t)wx, (int32_t)wy, (int32_t)ww, (int32_t)wh);
#if defined(__x86_64__)
    if (gui_rescue_inject) {                                      /* SOMA_221_RESCATE: 'escritorio rescate memoria|cuelgue' */
        int k = gui_rescue_inject; gui_rescue_inject = 0;
        if (k == 1) { volatile uintptr_t nul = 8; *(volatile int *)nul = 1; }
        else for (volatile uint32_t spin = 0; ; spin++) __asm__ volatile ("pause");
    }
#endif
    switch (app) {
    case APP_TERMINAL_SHELL:
        render_real_terminal(wx, wy, ww, wh, cur_cmd, cur_cmd_len, cur_cursor_pos, is_focused);
        break;
    case APP_AGENT_CHAT:
        render_log_window_chat(agent_log, agent_log_count, wx, wy, ww,
                               wh > 56 ? wh - 56 : wh);
        render_input_bar_chat("soma> ", COLOR_CYAN, wx, wy + wh - 46, ww, 46,
                              agent_global_cmd, agent_global_len,
                              agent_global_cursor, is_focused && agent_global_focus);
        break;
    case APP_WEB_BROWSER:   browser_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_IMAGE_VIEWER:  imgview_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_FILE_MANAGER:  fm_render_window(wx, wy, ww, wh, is_focused);      break;
    case APP_NOTEPAD:       notepad_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_AGENT_MEMORY:  memview_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_LAYOUT:        layout_render_window(wx, wy, ww, wh, is_focused);  break;
        case APP_PAINT:         paint_render_window(wx, wy, ww, wh, is_focused);   break;
    case APP_CODER_APP:      coder_app_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_MEDIA:          media_render_window(wx, wy, ww, wh, is_focused);   break;
    case APP_AGENTS:        agents_render_window(wx, wy, ww, wh, is_focused);  break;
    case APP_SOMA_APP:      gui_soma_win_render_body(wx, wy, ww, wh);          break; /*PL12_RENDER_BODY*/
    case APP_SETTINGS:      settings_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_PDF:           pdfview_render_window(wx, wy, ww, wh, is_focused); break;   /* PARCHE 088 */
    case APP_SYSMON:        sysmon_render_window(wx, wy, ww, wh, is_focused);  break;   /* PARCHE 089 */
    case APP_RECORDER:      recorder_render_window(wx, wy, ww, wh, is_focused); break;  /* SOMA_177_T480S */
    case APP_CAMERA:        camapp_render_window(wx, wy, ww, wh, is_focused); break;     /* SOMA_178_T480S */
    case APP_CAPTURE:       capapp_render_window(wx, wy, ww, wh, is_focused); break;     /* SOMA_179_T480S */
    default: break;
    }
    fb_reset_clip();
    acct_set_owner(own_prev);
}

/* SOMA_152_WEB_FLUIDO: compositor por demanda para el navegador.
 * Antes, con cualquier ventana abierta, el latido de 30 Hz borraba y recomponia la pantalla entera (fondo animado, sombras,
 * marcos y el cuerpo de cada ventana) y la volcaba completa: ~22 ms de cada 33 ms en QEMU (~70 % de la CPU), robados al
 * hilo del navegador (red, HTML, CSS, JS, maquetacion) y sumados como latencia a cada vuelta de rueda.
 * Ahora, si la ventana de arriba es el navegador:
 *   - la ventana se repinta SOLA y solo cuando su huella cambia (browser_frame_dirty), hasta 100 fotogramas/s;
 *   - el escritorio de fondo (estrellas, orbe) baja a SOMA_GUI_BG_MS (por defecto 15 fotogramas/s desde el 153B) y se congela del todo
 *     mientras el usuario usa el navegador o la pagina carga (browser_hot), como hace un compositor con las capas ocultas;
 *   - cualquier cambio de escena (mover/redimensionar, abrir/cerrar ventanas, foco, ajustes) recompone todo al instante.
 * Con -DSOMA_WEB_FLUIDO=0 vuelve exactamente el comportamiento anterior. */
#ifndef SOMA_WEB_FLUIDO
#define SOMA_WEB_FLUIDO 1
#endif
#ifndef SOMA_GUI_BG_MS
#define SOMA_GUI_BG_MS 66
#endif
#ifndef SOMA_WEB_FRAME_MS
#define SOMA_WEB_FRAME_MS 10
#endif
static uint64_t her152_scene_key(void)
{
    uint64_t k = 1469598103934665603ull;
    #define MIX(v) do { k ^= (uint64_t)(v); k *= 1099511628211ull; } while (0)
    MIX(her_num_windows); MIX(current_app); MIX(fb_get_width()); MIX(fb_get_height()); MIX(settings_opacity()); MIX(settings_active_opaque()); MIX(font_get_current()); MIX(wf_ui_gen());   /* SOMA_184: cambiar de fuente invalida la escena */
    MIX(her_icons_visible); MIX(agent_global_focus); MIX(agent_log_count); MIX(her_ans_len); MIX(her_thk_len); MIX(her_agent_busy);
    for (int i = 0; i < her_num_windows; ++i) { MIX(her_windows[i].app); MIX(her_windows[i].x); MIX(her_windows[i].y); MIX(her_windows[i].w); MIX(her_windows[i].h); }
    #undef MIX
    return k;
}
static int her152_eligible(void)
{
#if SOMA_WEB_FLUIDO
    if (her_num_windows <= 0 || current_app != APP_WEB_BROWSER) return 0;
    if (her_windows[her_num_windows - 1].app != APP_WEB_BROWSER) return 0;
    if (her_dragging || her_resizing || her_vortex_closing) return 0;
    if (her_icons_anim_phase != 0u && her_icons_anim_phase != HER_ICON_ANIM_MAX) return 0;   /* transicion del dock en marcha */
    return 1;
#else
    return 0;
#endif
}

#ifndef SOMA_WEB_PERF_LOG
#define SOMA_WEB_PERF_LOG 0
#endif
#if SOMA_WEB_PERF_LOG
/* SOMA_152: con -DSOMA_WEB_PERF_LOG=1, cada 3 s por el puerto serie: fotogramas completos del escritorio y fotogramas
 * solo-navegador, con su coste. Sirve para comparar antes/despues en la maquina real. */
static uint32_t pf152_full_n, pf152_full_ms, pf152_web_n, pf152_web_ms, pf152_web_max; static uint64_t pf152_t0;
static void pf152_note(int web, uint32_t ms)
{
    if (web) { pf152_web_n++; pf152_web_ms += ms; if (ms > pf152_web_max) pf152_web_max = ms; }
    else { pf152_full_n++; pf152_full_ms += ms; }
    uint64_t now = timer_get_uptime_ms();
    if (!pf152_t0) pf152_t0 = now;
    if (now - pf152_t0 >= 3000) {
        kprint("[web152] 3s: escritorio="); kprint_dec(pf152_full_n); kprint(" fot/"); kprint_dec(pf152_full_ms);
        kprint("ms navegador="); kprint_dec(pf152_web_n); kprint(" fot/"); kprint_dec(pf152_web_ms); kprint("ms (max ");
        kprint_dec(pf152_web_max); kprint("ms)\n");
        pf152_full_n = pf152_full_ms = pf152_web_n = pf152_web_ms = pf152_web_max = 0; pf152_t0 = now;
    }
}
#define PF152(web, ms) pf152_note((web), (ms))
#else
#define PF152(web, ms) ((void)0)
#endif
#ifndef SOMA_WEB_PERF_LOG
#define SOMA_WEB_PERF_LOG 0
#endif
static int s153_mode = 1;                                 /* con SOMA_WEB_PERF_LOG se alterna para comparar */
static void her153_set_occlusion(void)
{
    ofx_occ_on = 0;
    if (!SOMA_ORB_OCCLUSION || !s153_mode || her_num_windows <= 0) return;
    struct her_win_node *t = &her_windows[her_num_windows - 1];
    uint32_t alpha = (uint32_t)settings_opacity() * 255u / 100u;
    if (alpha < 255u && !settings_active_opaque()) return;       /* ventana translucida: el fondo se ve, dibujar todo */
    int32_t m = HER_WIN_R + 2;
    if ((int32_t)t->w <= 2 * m || (int32_t)t->h <= 2 * m) return;
    ofx_occ_x0 = (int32_t)t->x + m; ofx_occ_y0 = (int32_t)t->y + m;
    ofx_occ_x1 = (int32_t)(t->x + t->w) - m; ofx_occ_y1 = (int32_t)(t->y + t->h) - m;
    ofx_occ_on = 1;
}
#if SOMA_WEB_PERF_LOG
static uint64_t s153_t0, s153_cyc[2]; static uint32_t s153_n[2];
static void her153_note(uint64_t cyc)
{
    uint64_t now = timer_get_uptime_ms();
    if (!s153_t0) s153_t0 = now;
    s153_cyc[s153_mode] += cyc; s153_n[s153_mode]++;
    if (now - s153_t0 >= 6000) {
        kprint("[desk153] ocultar_orbe="); kprint_dec((uint32_t)s153_mode); kprint(" fotogramas="); kprint_dec(s153_n[s153_mode]);
        kprint(" coste_medio_kciclos="); kprint_dec(s153_n[s153_mode] ? (uint32_t)((s153_cyc[s153_mode] / s153_n[s153_mode]) >> 10) : 0);
        kprint(" cintas_kciclos="); kprint_dec(s153_n[s153_mode] ? (uint32_t)((her153_rib_cycles / s153_n[s153_mode]) >> 10) : 0); kprint("\n");
        her153_rib_cycles = 0;
        s153_cyc[s153_mode] = 0; s153_n[s153_mode] = 0; s153_mode = !s153_mode; ofx_rib_mode = s153_mode; s153_t0 = now;
    }
}
static inline uint64_t her153_cycles(void)
{
#if defined(__x86_64__)
    return __builtin_ia32_rdtsc();
#else
    uint64_t v; __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(v)); return v;
#endif
}
#endif
/* SOMA_209_ECO (3): contenido de ventana sin cambios = copia de su ultima pintura. Se reutiliza solo si: la aplicacion esta en la
 * lista, la ventana no ha cambiado de sitio, tamano ni foco, no ha habido teclado ni raton en 150 ms, la pintura tiene menos de
 * HER_BC_MS y nadie ha dibujado en esa zona de la pantalla desde el ultimo fotograma (suma de comprobacion del backbuffer).
 * Lo que una aplicacion anima por reloj (el parpadeo del cursor de texto) puede llegar hasta HER_BC_MS tarde: medido, 1 de cada
 * 8 copias. 'orbe ventanas off' lo desactiva. */
#ifndef HER_BC_MS
#define HER_BC_MS 70u
#endif
int her_bc_on = 1;
struct her_bc { int used; enum app_mode app; int slot; uint32_t x, y, w, h; int top; uint32_t *px; size_t cap; uint64_t t, sum; int sum_ok; uint32_t ver, gen; };
/* SOMA_216_RENDIMIENTO: aplicaciones que dicen cuando cambia su contenido. Con ellas la copia vale hasta que cambie la version,
 * sin limite de tiempo, haya o no actividad del raton, y sin sumas de comprobacion. */
static int her_app_version(enum app_mode a, uint32_t *v)
{
    switch (a) { case APP_SYSMON: *v = sysmon_version(); return 1; default: return 0; }
}
static uint32_t her_bc_gen(void) { return wf_ui_gen() ^ ((uint32_t)settings_opacity() << 20) ^ ((uint32_t)settings_active_opaque() << 28); }
static struct her_bc her_bcs[HER_MAX_WINDOWS];
static int her_bc_base_ok; static uint64_t her_bc_input_ms;
static int her_bc_app_ok(enum app_mode a)
{
    return a == APP_TERMINAL_SHELL || a == APP_NOTEPAD || a == APP_FILE_MANAGER || a == APP_IMAGE_VIEWER || a == APP_AGENT_CHAT ||
           a == APP_AGENT_MEMORY || a == APP_AGENTS || a == APP_SETTINGS || a == APP_PDF || a == APP_LAYOUT || a == APP_SYSMON;
}
static struct her_bc *her_bc_find(enum app_mode app, int slot, int create)
{
    for (int i = 0; i < HER_MAX_WINDOWS; ++i) if (her_bcs[i].used && her_bcs[i].app == app && her_bcs[i].slot == slot) return &her_bcs[i];
    if (create) for (int i = 0; i < HER_MAX_WINDOWS; ++i) if (!her_bcs[i].used) {
        her_bcs[i].used = 1; her_bcs[i].app = app; her_bcs[i].slot = slot; her_bcs[i].px = 0; her_bcs[i].cap = 0; her_bcs[i].sum_ok = 0; her_bcs[i].t = 0;
        return &her_bcs[i];
    }
    return 0;
}
static int her_bc_rect(const struct her_win_node *win, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = (int32_t)HER_CLI_X(win->x); *y = (int32_t)HER_CLI_Y(win->y); *w = (int32_t)HER_CLI_W(win->w); *h = (int32_t)HER_CLI_H(win->h);
    return *x >= 0 && *y >= 0 && *w > 8 && *h > 8 && *x + *w <= (int32_t)fb_get_width() && *y + *h <= (int32_t)fb_get_height();
}
static uint64_t her_bc_sum(int32_t x, int32_t y, int32_t w, int32_t h)
{
    volatile uint32_t *bb = fb_get_buffer(); uint32_t W = fb_get_width();
    uint64_t s = 0x9E3779B97F4A7C15ull;
    if (!bb) return 0;
    for (int32_t r = 0; r < h; ++r) {
        const uint32_t *p = (const uint32_t *)(bb + (size_t)(y + r) * W + (size_t)x);
        uint64_t a = (uint64_t)r;
        for (int32_t i = 0; i < w; ++i) a = ((a << 7) | (a >> 57)) + p[i];
        s = (s ^ a) * 0x100000001B3ull;
    }
    return s;
}
static void her_bc_baseline(void)                              /* tras volcar un fotograma completo */
{
    her_bc_base_ok = 0;
    if (!her_eco_on) return;
    for (int i = 0; i < HER_MAX_WINDOWS; ++i) {                  /* soltar la memoria de las ventanas que ya no estan abiertas */
        struct her_bc *e = &her_bcs[i]; int open = 0;
        if (!e->used) continue;
        for (int k = 0; k < her_num_windows; ++k) if (her_windows[k].app == e->app && her_windows[k].term_slot == e->slot) open = 1;
        if (!open) { if (e->px) kfree(e->px); e->px = 0; e->cap = 0; e->used = 0; }
    }
    for (int k = 0; k < her_num_windows; ++k) {
        struct her_bc *e = her_bc_find(her_windows[k].app, her_windows[k].term_slot, 0);
        int32_t x, y, w, h;
        if (!e) continue;
        e->sum_ok = 0;
        { uint32_t v; if (her_app_version(her_windows[k].app, &v)) continue; }
        if (!e->px || !her_bc_rect(&her_windows[k], &x, &y, &w, &h)) continue;
        e->sum = her_bc_sum(x, y, w, h); e->sum_ok = 1;
    }
    her_bc_base_ok = 1;
}
static int her_bc_live(const struct her_win_node *win, int top, uint64_t now)   /* antes de borrar el fotograma anterior */
{
    int32_t x, y, w, h;
    if (!her_bc_app_ok(win->app) || !her_bc_rect(win, &x, &y, &w, &h)) return 0;
    struct her_bc *e = her_bc_find(win->app, win->term_slot, 0);
    if (!e || !e->px || e->top != top || e->x != win->x || e->y != win->y || e->w != win->w || e->h != win->h) return 0;
    { uint32_t v; if (her_app_version(win->app, &v)) return v == e->ver && e->gen == her_bc_gen(); }
    if (!e->sum_ok) return 0;
    if (now - e->t >= HER_BC_MS) return 0;
    return her_bc_sum(x, y, w, h) == e->sum;
}
static void her_bc_store(const struct her_win_node *win, int top, uint64_t now)
{
    int32_t x, y, w, h;
    uint32_t ver = 0; int versioned = her_app_version(win->app, &ver);
    if (!her_eco_on || !her_bc_on) return;
    if (!versioned && (her_eco_period >= HER_BC_MS || now - her_bc_input_ms < 150u)) return;   /* no se podria reutilizar: no copiar */
    if (!her_bc_app_ok(win->app) || !her_bc_rect(win, &x, &y, &w, &h)) return;
    struct her_bc *e = her_bc_find(win->app, win->term_slot, 1);
    volatile uint32_t *bb = fb_get_buffer(); uint32_t W = fb_get_width();
    if (!e || !bb) return;
    size_t need = (size_t)w * (size_t)h * 4u;
    if (need > e->cap) {
        if (e->px) kfree(e->px);
        e->px = (uint32_t *)kmalloc(need); e->cap = e->px ? need : 0;
        if (!e->px) return;
    }
    for (int32_t r = 0; r < h; ++r) memcpy(e->px + (size_t)r * (size_t)w, (const void *)(bb + (size_t)(y + r) * W + (size_t)x), (size_t)w * 4u);
    e->x = win->x; e->y = win->y; e->w = win->w; e->h = win->h; e->top = top; e->t = now; e->sum_ok = 0; e->ver = ver; e->gen = her_bc_gen();
}
static uint32_t her_bc_put(const struct her_win_node *win, int compare_only)
{
    int32_t x, y, w, h; uint32_t bad = 0;
    struct her_bc *e = her_bc_find(win->app, win->term_slot, 0);
    volatile uint32_t *bb = fb_get_buffer(); uint32_t W = fb_get_width();
    if (!e || !e->px || !bb || !her_bc_rect(win, &x, &y, &w, &h)) return 0;
    for (int32_t r = 0; r < h; ++r) {
        uint32_t *d = (uint32_t *)(bb + (size_t)(y + r) * W + (size_t)x); const uint32_t *sp = e->px + (size_t)r * (size_t)w;
        if (compare_only) { for (int32_t i = 0; i < w; ++i) if (d[i] != sp[i]) bad++; }
        else memcpy(d, sp, (size_t)w * 4u);
    }
    if (!compare_only) fb_damage(x, y, w, h);
    return bad;
}

static void her_refresh_all(void)
{
    uint64_t t0 = tmp_tsc();
    int bc_live[HER_MAX_WINDOWS];
    uint64_t bc_now = timer_get_uptime_ms();
    {
        int base = her_bc_base_ok; her_bc_base_ok = 0;
        for (int i = 0; i < HER_MAX_WINDOWS; ++i) bc_live[i] = 0;
        if (her_eco_on && her_bc_on && !her_summon_on)
            for (int i = 0; i < her_num_windows; ++i) {
                uint32_t v;
                if (her_app_version(her_windows[i].app, &v) || (base && bc_now - her_bc_input_ms >= 150u))
                    bc_live[i] = her_bc_live(&her_windows[i], i == her_num_windows - 1, bc_now);
            }
    }
    her_in_refresh = 1;
    her153_set_occlusion();                                 /* SOMA_153 */
    render_desktop();
    tmp_pf[0] += tmp_tsc() - t0; tmp_pfn++;
    ofx_occ_on = 0;
    her_in_refresh = 0;
    fb_cguard_keep(0);
    if (settings_opacity() >= 100) fb_tr_release();             /* PARCHE 086: sin transparencia no se guardan las dos capas de pantalla */
    if (her_num_windows == 0) { fb_tr_off(); return; }

    for (int i = 0; i < her_num_windows; ++i) {
        struct her_win_node *win = &her_windows[i];
        int top = (i == her_num_windows - 1);
        struct fb_cguard *cg = &her_cguards[i];

        /* SOMA_PARCHE_028_UI: sombra -> se guarda el fondo de las esquinas inferiores -> marco -> cuerpo -> se recompone el arco */
        uint64_t t1 = tmp_tsc();
        her_draw_window_shadow(win, top);
        /* PARCHE 085: transparencia. La activa se mezcla en cada volcado (fb_tr_*); las de detras, una vez aqui. */
        uint32_t alpha = (uint32_t)settings_opacity() * 255u / 100u;
        uint32_t *under = 0;
        if (alpha < 255 && !(top && settings_active_opaque())) {
            if (top) fb_tr_begin((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h, alpha);
            else under = fb_rect_save((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h);
        } else if (top) fb_tr_off();
        fb_cguard_capture(cg, (int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h, HER_WIN_R);
        her_draw_one_window_chrome(win, top);
        uint64_t t2 = tmp_tsc(); tmp_pf[1] += t2 - t1;
        if (bc_live[i] && !her_eco_verify) { her_bc_put(win, 0); her_eco_stat[2]++; }
        else {
            her_win_focus(win->app, win->term_slot);
            her_render_app_body(win->app, HER_CLI_X(win->x), HER_CLI_Y(win->y), HER_CLI_W(win->w), HER_CLI_H(win->h), top);
            if (bc_live[i]) { if (her_bc_put(win, 1)) her_eco_stat[4]++; her_eco_stat[2]++; } else her_eco_stat[3]++;
            her_bc_store(win, top, bc_now);
        }
        uint64_t t3 = tmp_tsc(); tmp_pf[2] += t3 - t2;
        if (under) fb_rect_blend_saved(under, (int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h, alpha);
        tmp_pf[3] += tmp_tsc() - t3;
        fb_cguard_apply(cg, her_win_border_color(top));
        if (top) fb_cguard_keep(cg);          /* la ventana activa se repinta sola entre fotogramas: fb_present repone sus esquinas */
        if (her_eco_on) fb_damage((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h);   /* SOMA_209: para el borrado por zonas */
    }
    { struct her_win_node *last = &her_windows[her_num_windows - 1];
      her_win_focus(last->app, last->term_slot); }
    hst_dibujar(fb_get_width(), fb_get_height());
}

static void her_open_app(enum app_mode app, uint32_t cur_w, uint32_t cur_h)
{
    int existing = her_multi_app(app) ? -1 : her_find_window(app);
    if (existing >= 0) {
        her_bring_to_front(existing);
        agent_global_focus = (app == APP_AGENT_CHAT) ? 1 : 0;
        return;
    }
    if (!her_multi_app(app)) {
        int mi = her_find_minimized(app);
        if (mi >= 0) { her_restore_minimized(mi); return; }
    }

    if (her_num_windows >= HER_MAX_WINDOWS) return; /* No destruir silenciosamente otra ventana. */
    int terminal_slot = 0;
    if (app == APP_TERMINAL_SHELL) {
        terminal_slot = her_term_alloc();
        if (!terminal_slot) return;
        if (!term_ui_select(terminal_slot)) { her_term_kill(terminal_slot); return; }
    }
    if (her_multi_app(app) && app != APP_TERMINAL_SHELL) {    /* SOMA_211_NOTAS / SOMA_212: cada ventana, su documento */
        terminal_slot = her_inst_new(app);
        if (!terminal_slot) {
            her_hint_msg = "No caben mas ventanas de esta aplicacion: cierra alguna arrastrandola al agujero negro";
            her_hint_until = timer_get_uptime_ms() + 3000u;
            return;
        }
    }

    int idx = her_num_windows++;
    her_windows[idx].app = app;
    her_windows[idx].term_slot = terminal_slot;
    her_windows[idx].dragging = 0;
    her_windows[idx].resizing = 0;

    her_fit_window_to_orbit(cur_w, cur_h);
    her_windows[idx].w = her_win_w;
    her_windows[idx].h = her_win_h;
    {   /* SOMA_PARCHE_V2_GUI: tamano minimo por aplicacion al abrir */
        uint32_t mw = her_app_min_w(app), mh = (app == APP_PAINT || app == APP_LAYOUT || app == APP_SETTINGS || app == APP_PDF || app == APP_SYSMON || app == APP_CODER_APP || app == APP_RECORDER || app == APP_CAMERA || app == APP_CAPTURE) ? 600u : HER_MIN_H + 60u;
        if (cur_w > 64u && mw > cur_w - 32u) mw = cur_w - 32u;
        if ((app == APP_PAINT || app == APP_LAYOUT || app == APP_SETTINGS || app == APP_PDF || app == APP_SYSMON || app == APP_CODER_APP || app == APP_RECORDER || app == APP_CAMERA || app == APP_CAPTURE) && cur_h > 96u && mh > cur_h - 60u) mh = cur_h - 60u;
        if (her_windows[idx].w < mw) her_windows[idx].w = mw;
        if (cur_h > 96u && mh <= cur_h - 48u && her_windows[idx].h < mh) her_windows[idx].h = mh;
        if ((app == APP_WEB_BROWSER || app == APP_PDF || app == APP_IMAGE_VIEWER || app == APP_SYSMON || app == APP_CODER_APP) && cur_w > 400u && cur_h > 300u) {   /* PARCHE 088: tambien PDF e imagenes */   /* PARCHE 086: el navegador abre grande (2/3 de ancho, 85 % de alto) */
            uint32_t bw = cur_w * 2u / 3u, bh = cur_h * 85u / 100u;
            if (bw > 1400u) bw = 1400u;
            if (bw < mw) bw = mw;
            if (bw > cur_w - 32u) bw = cur_w - 32u;
            if (bh > cur_h - 56u) bh = cur_h - 56u;
            if (her_windows[idx].w < bw) her_windows[idx].w = bw;
            if (her_windows[idx].h < bh) her_windows[idx].h = bh;
        }
    }

    uint32_t cascade_off = (uint32_t)(idx * 28);
    int corner = (int)app;
    uint32_t base_x = ((corner & 1) == 0 || cur_w <= her_windows[idx].w + 32u) ? 20u : (cur_w - her_windows[idx].w - 20u);
    uint32_t base_y = ((corner & 2) == 0 || cur_h <= her_windows[idx].h + 48u) ? 36u : (cur_h - her_windows[idx].h - 20u);

    if ((corner & 1) == 0) base_x += cascade_off; else base_x = (base_x > cascade_off) ? base_x - cascade_off : 10u;
    if ((corner & 2) == 0) base_y += cascade_off; else base_y = (base_y > cascade_off) ? base_y - cascade_off : 30u;

    if (base_x + her_windows[idx].w > cur_w) base_x = cur_w - her_windows[idx].w - 10u;
    if (base_y + her_windows[idx].h > cur_h) base_y = cur_h - her_windows[idx].h - 10u;

    her_windows[idx].x = base_x;
    her_windows[idx].y = base_y;

    her_win_x = her_windows[idx].x;
    her_win_y = her_windows[idx].y;
    her_win_w = her_windows[idx].w;
    her_win_h = her_windows[idx].h;

    her_active_win_idx = idx;
    current_app = app;
    agent_global_focus = (app == APP_AGENT_CHAT) ? 1 : 0;
    her_win_focus(app, terminal_slot);
    if (terminal_slot && app == APP_TERMINAL_SHELL) term_init_banner(0); /* Historia nueva solo en una instancia nueva. */

    switch (app) {
    case APP_WEB_BROWSER:  browser_init();  break;
    case APP_FILE_MANAGER: if (!her_open_noinit) fm_init(); break;   /* SOMA_214: no escanear /workspace si viene una carpeta */
    case APP_IMAGE_VIEWER: if (!her_open_noinit) imgview_init(); break;   /* SOMA_212: no cargar la de muestra si viene un archivo */
    case APP_NOTEPAD:      notepad_init();  break;
    case APP_AGENT_MEMORY: memview_init();  break;
    case APP_LAYOUT:       layout_init();   break;          /* PARCHE 084: conserva el documento abierto */
    case APP_PDF:          pdfview_init();  break;          /* PARCHE 088 */
    case APP_SYSMON:       sysmon_init();   break;          /* PARCHE 089 */
    case APP_RECORDER:     recorder_init(); break;          /* SOMA_177_T480S */
    case APP_CAMERA:       camapp_init(); break;            /* SOMA_178_T480S */
    case APP_CAPTURE:      capapp_init(); break;            /* SOMA_179_T480S */
    case APP_PAINT:        paint_ensure();  break;          /* PARCHE 083: conserva el dibujo al reabrir */
    case APP_CODER_APP:   coder_app_init(); break;
    case APP_MEDIA:       media_init();     break;
    default: break;
    }
}

/* SOMA_203_INSTANCIAS: X = minimizar (sin perder el estado de usuario). */
static void her_minimize_window(int idx)
{
    if (idx < 0 || idx >= her_num_windows) return;
    if (her_min_count >= HER_MINIMIZED_MAX) {
        her_hint_msg = "Orbita llena: arrastra un icono al centro para cerrarlo";
        her_hint_until = timer_get_uptime_ms() + 3000u;
        return;
    }
    struct her_win_node v = her_windows[idx];
    if (v.app == APP_TERMINAL_SHELL) her_term_focus(-1);
    {   /* fase: el hueco mas amplio entre los iconos ya en orbita */
        uint32_t best = 0, bestd = 0;
        for (uint32_t c = 0; c < 1024u; c += 32u) {
            uint32_t md = 1024u;
            for (int q = 0; q < her_min_count; ++q) {
                uint32_t d = (c - her_min_phase[q]) & 1023u; if (d > 512u) d = 1024u - d;
                if (d < md) md = d;
            }
            if (md > bestd) { bestd = md; best = c; }
        }
        her_min_phase[her_min_count] = (uint16_t)best;
    }
    her_minimized[her_min_count++] = v;
    for (int j = idx; j < her_num_windows - 1; ++j) her_windows[j] = her_windows[j + 1];
    --her_num_windows;
    if (her_num_windows > 0) {
        struct her_win_node *top = &her_windows[her_num_windows - 1];
        her_active_win_idx = her_num_windows - 1;
        current_app = top->app;
        her_win_x = top->x; her_win_y = top->y; her_win_w = top->w; her_win_h = top->h;
        agent_global_focus = (current_app == APP_AGENT_CHAT) ? 1 : 0;
        her_win_focus(current_app, top->term_slot);
    } else {
        her_active_win_idx = -1; current_app = APP_DESKTOP; agent_global_focus = 1;
        her_term_focus(-1);
    }
}
static void her_restore_minimized(int idx)
{
    if (idx < 0 || idx >= her_min_count || her_num_windows >= HER_MAX_WINDOWS) return;
    struct her_win_node v = her_minimized[idx];
    for (int j = idx; j < her_min_count - 1; ++j) { her_minimized[j] = her_minimized[j + 1]; her_min_phase[j] = her_min_phase[j + 1]; }
    --her_min_count;
    her_term_focus(-1);
    her_windows[her_num_windows++] = v;
    her_active_win_idx = her_num_windows - 1;
    current_app = v.app;
    her_win_x = v.x; her_win_y = v.y; her_win_w = v.w; her_win_h = v.h;
    agent_global_focus = (v.app == APP_AGENT_CHAT) ? 1 : 0;
    her_win_focus(v.app, v.term_slot);
}
static void her_kill_minimized(int idx)
{
    if (idx < 0 || idx >= her_min_count) return;
    struct her_win_node dead = her_minimized[idx];
    if (dead.app == APP_TERMINAL_SHELL) her_term_kill(dead.term_slot);
    her_inst_drop(dead.app, dead.term_slot);                  /* SOMA_211_NOTAS / SOMA_212 */
    if (dead.app == APP_CAMERA) camapp_close();
    if (dead.app == APP_SOMA_APP) gui_soma_win_on_close();
    for (int j = idx; j < her_min_count - 1; ++j) { her_minimized[j] = her_minimized[j + 1]; her_min_phase[j] = her_min_phase[j + 1]; }
    --her_min_count;
}
static int her_find_minimized(enum app_mode app)
{
    for (int i = 0; i < her_min_count; ++i) if (her_minimized[i].app == app) return i;
    return -1;
}

/* ====================================================================
 * SOMA ENHANCED VORTEX & ACCRETION DISK ENGINE (Fixed-Point 16.8)
 * ==================================================================== */
#define HER_VORTEX_MAX 512

struct her_vortex_p {
    int32_t  x, y;           /* Posición fija 16.8 */
    int32_t  vx, vy;         /* Velocidad fija 16.8 */
    int32_t  prev_x, prev_y; /* Estela luminosa */
    uint32_t color;
    uint32_t edge_color;
    uint8_t  absorbed;
    uint8_t  size;
    int16_t  w_size, h_size;
};

static struct her_vortex_p her_vortex_parts[HER_VORTEX_MAX];

/* static int her_vortex_closing = 0; (declarada arriba) */
static int her_vortex_frame = 0;
static int her_vortex_total_absorbed = 0;
/* static int her_dock_pending_open = 0; (declarada arriba) */
/* static int soma_text_vortex = 0; (declarada arriba) */

static void her_close_target_window_vortex(int win_idx)
{
    if (win_idx < 0 || win_idx >= her_num_windows) return;
        struct her_win_node closing_win = her_windows[win_idx];
    if (closing_win.app == APP_TERMINAL_SHELL) her_term_kill(closing_win.term_slot);
    her_inst_drop(closing_win.app, closing_win.term_slot);   /* SOMA_211_NOTAS / SOMA_212 */
    enum app_mode closing_app = closing_win.app;
    if (closing_app == APP_SOMA_APP) {
        gui_soma_win_on_close(); /*PL12_NOTIFY_CLOSE*/
    }
    if (closing_app == APP_CAMERA) camapp_close();     /* SOMA_178_T480S: apaga la camara */

    for (int i = win_idx; i < her_num_windows - 1; ++i) {
        her_windows[i] = her_windows[i + 1];
    }
    her_num_windows--;

    if (her_num_windows > 0) {
        her_active_win_idx = her_num_windows - 1;
        current_app = her_windows[her_active_win_idx].app;
        her_win_x = her_windows[her_active_win_idx].x;
        her_win_y = her_windows[her_active_win_idx].y;
        her_win_w = her_windows[her_active_win_idx].w;
        her_win_h = her_windows[her_active_win_idx].h;
        agent_global_focus = (current_app == APP_AGENT_CHAT) ? 1 : 0;
        her_win_focus(current_app, her_windows[her_active_win_idx].term_slot);
    } else {
        her_term_focus(-1);
        her_active_win_idx = -1;
        current_app = APP_DESKTOP;
        agent_global_focus = 1;
    }

    soma_text_vortex = 0;
    her_dock_pending_open = 0;

    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    if (cur_w == 0 || cur_h == 0) {
        if (!her_cursor_should_show()) mouse_erase_cursor();
        return;
    }

    uint32_t app_col = 0x0038BDF8u;
    switch (closing_app) {
    case APP_TERMINAL_SHELL: app_col = 0x0010B981u; break;
    case APP_AGENT_CHAT:     app_col = 0x0038BDF8u; break;
    case APP_WEB_BROWSER:    app_col = 0x00F59E0Bu; break;
    case APP_FILE_MANAGER:   app_col = 0x0038BDF8u; break;
    case APP_IMAGE_VIEWER:   app_col = 0x0010B981u; break;
    case APP_NOTEPAD:        app_col = 0x00F472B6u; break;
    case APP_AGENT_MEMORY:   app_col = 0x00A855F7u; break;
    case APP_LAYOUT:         app_col = 0x000EA5E9u; break;
        case APP_AGENTS:         app_col = 0x0034D399u; break;
    case APP_PAINT:          app_col = 0x00F59E0Bu; break;
    case APP_CODER_APP:      app_col = 0x0000FF88u; break;
    case APP_MEDIA:          app_col = 0x00FB7185u; break;
    case APP_RECORDER:       app_col = 0x00F87171u; break;
    case APP_CAMERA:         app_col = 0x0038BDF8u; break;                /* SOMA_178_T480S */
    case APP_CAPTURE:        app_col = 0x00A78BFAu; break;                /* SOMA_179_T480S */
    case APP_SOMA_APP:       app_col = 0x0038BDF8u; break; /*PL12_VORTEX_COL*/
    default:                 app_col = 0x007DD3FCu; break;
    }

    int32_t wx = (int32_t)closing_win.x;
    int32_t wy = (int32_t)closing_win.y;
    int32_t ww = (int32_t)closing_win.w;
    int32_t wh = (int32_t)closing_win.h;
    if (ww < 100) ww = 400;
    if (wh < 100) wh = 300;
    int32_t wcx = wx + ww / 2;
    int32_t wcy = wy + wh / 2;

    int32_t tile_w = ww / 16;
    int32_t tile_h = wh / 16;
    if (tile_w < 4) tile_w = 4;
    if (tile_h < 4) tile_h = 4;

    volatile uint32_t *target_bb = fb_get_buffer();
    int p_idx = 0;
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            if (p_idx >= HER_VORTEX_MAX) break;
            struct her_vortex_p *p = &her_vortex_parts[p_idx++];
            int32_t orig_x = wx + c * tile_w;
            int32_t orig_y = wy + r * tile_h;
            if (orig_x < 0) orig_x = 0;
            if (orig_x >= (int32_t)cur_w) orig_x = (int32_t)cur_w - 1;
            if (orig_y < 0) orig_y = 0;
            if (orig_y >= (int32_t)cur_h) orig_y = (int32_t)cur_h - 1;

            p->x = orig_x << 8;
            p->y = orig_y << 8;
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->absorbed = 0;
            p->w_size = (int16_t)tile_w;
            p->h_size = (int16_t)tile_h;
            p->size = ((p_idx & 7) == 0) ? 2 : 1;

            uint32_t samp_col = 0;
            if (target_bb) {
                int32_t mid_x = orig_x + tile_w / 2;
                int32_t mid_y = orig_y + tile_h / 2;
                if (mid_x >= (int32_t)cur_w) mid_x = (int32_t)cur_w - 1;
                if (mid_y >= (int32_t)cur_h) mid_y = (int32_t)cur_h - 1;
                samp_col = target_bb[mid_y * cur_w + mid_x];
            }
            int is_border = (r == 0 || r == 15 || c == 0 || c == 15);
            if ((samp_col & 0x00FFFFFFu) < 0x00151515u || is_border)
                p->color = her_mix256(app_col, 0x00FFFFFFu, is_border ? 60u : 0u);
            else
                p->color = her_mix256(samp_col, app_col, 80u);
            p->edge_color = her_mix256(app_col, 0x00FFFFFFu, 180u);

            int32_t ox = (orig_x + tile_w / 2) - wcx;
            int32_t oy = (orig_y + tile_h / 2) - wcy;
            int32_t rnd_x = (int32_t)(her_rand() & 15u) - 7;
            int32_t rnd_y = (int32_t)((her_rand() >> 4) & 15u) - 7;
            p->vx = (ox * 70) / (ww > 0 ? ww : 1) + rnd_x * 16;
            p->vy = (oy * 70) / (wh > 0 ? wh : 1) + rnd_y * 16;
        }
    }

    her_dragging = 0;
    her_resizing = 0;
    her_vortex_total_absorbed = 0;
    her_vortex_frame = 0;
    her_vortex_closing = 1;
    soma_vortex_boost = 10;
    if (!her_cursor_should_show()) mouse_erase_cursor();
}

/* SOMA_208: icono minimizado soltado en el agujero negro: se deshace en particulas que caen al horizonte y despues se cierra. */
/* SOMA_219_AVISO: una ventana con cambios sin guardar (Notas, Paint, Studio) no se cierra al primer intento: se avisa y vuelve a
 * orbitar. Si se suelta otra vez en el agujero negro en los 8 s siguientes, se cierra sin guardar. */
static int her_inst_dirty(enum app_mode app, int slot)
{
    switch (app) { case APP_NOTEPAD: return notepad_inst_dirty(slot); case APP_PAINT: return paint_inst_dirty(slot); case APP_CODER_APP: return ce_inst_dirty(slot); default: return 0; }
}
static int her_kill_armed_app = -1, her_kill_armed_slot;
static uint64_t her_kill_armed_until;
static char her_kill_hint[112];
static int her_kill_confirmed(int idx)
{
    enum app_mode app = her_minimized[idx].app; int slot = her_minimized[idx].term_slot;
    if (!her_inst_dirty(app, slot)) return 1;
    uint64_t now = timer_get_uptime_ms();
    if (her_kill_armed_app == (int)app && her_kill_armed_slot == slot && now < her_kill_armed_until) { her_kill_armed_app = -1; return 1; }
    her_kill_armed_app = (int)app; her_kill_armed_slot = slot; her_kill_armed_until = now + 8000u;
    const char *nm = her_inst_name(app, slot);
    int n = 0;
    for (const char *t = (nm && *nm) ? nm : her_app_base_title(app); *t && n < 40; ++t) her_kill_hint[n++] = *t;
    for (const char *t = ": cambios sin guardar. Sueltalo otra vez en el agujero negro para cerrar sin guardar"; *t && n < (int)sizeof her_kill_hint - 1; ++t) her_kill_hint[n++] = *t;
    her_kill_hint[n] = 0;
    her_hint_msg = her_kill_hint; her_hint_until = now + 8000u;
    return 0;
}
static void her_kill_minimized_vortex(int idx, int32_t px, int32_t py)
{
    if (idx < 0 || idx >= her_min_count) return;
    if (!her_kill_confirmed(idx)) return;                     /* SOMA_219_AVISO */
    uint32_t col = 0x0038BDF8u;
    int id = her_min_dock_index(her_minimized[idx].app);
    if (id >= 0) col = her_dock_items[id].color;
    uint32_t cw = fb_get_width(), ch = fb_get_height();
    int n = 0;
    if (cw && ch) for (int k = 0; k < 96 && n < HER_VORTEX_MAX; ++k) {
        struct her_vortex_p *p = &her_vortex_parts[n++];
        int32_t x = px + (int32_t)(her_rand() % 40u) - 20, y = py + (int32_t)(her_rand() % 40u) - 20;
        if (x < 0) x = 0;
        if (x >= (int32_t)cw) x = (int32_t)cw - 1;
        if (y < 0) y = 0;
        if (y >= (int32_t)ch) y = (int32_t)ch - 1;
        p->x = x << 8; p->y = y << 8; p->prev_x = p->x; p->prev_y = p->y;
        p->absorbed = 0; p->w_size = 4; p->h_size = 4; p->size = ((k & 3) == 0) ? 2 : 1;
        p->color = (k & 1) ? col : 0x00FFFFFFu;
        p->edge_color = her_mix256(col, 0x00FFFFFFu, 180u);
        p->vx = ((int32_t)(her_rand() & 15u) - 7) * 12; p->vy = ((int32_t)((her_rand() >> 4) & 15u) - 7) * 12;
    }
    for (int q = n; q < HER_VORTEX_MAX; ++q) her_vortex_parts[q].absorbed = 1;
    her_kill_minimized(idx);
    if (n > 0) {
        her_vortex_frame = 0; her_vortex_total_absorbed = 0; her_vortex_closing = 1; soma_vortex_boost = 10;
        ofx_flash += 60u; if (ofx_flash > 255u) ofx_flash = 255u;
    }
}

/* Enrutador canónico para cerrar la ventana activa desde cualquier app */
static void her_close_current_app_vortex(void)
{
    if (her_num_windows <= 0) return;
    her_minimize_window_fx(her_num_windows - 1);
}

/* Integración física en el disco de acreción hasta el horizonte de sucesos */
static void her_vortex_close_step(void)
{
    if (!her_vortex_closing) return;
    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    uint32_t cx = cur_w / 2u, cy = cur_h / 2u;
    if (cur_w == 0u || cur_h == 0u) {
        her_vortex_closing = 0;
        return;
    }

    her_vortex_frame++;
    int active_in_flight = 0;
    int frame = her_vortex_frame;
    uint32_t horizon_r = 27u; /* Radio del horizonte de sucesos */

    for (int i = 0; i < HER_VORTEX_MAX; ++i) {
        struct her_vortex_p *p = &her_vortex_parts[i];
        if (p->absorbed) continue;
        int32_t cur_px = p->x >> 8;
        int32_t cur_py = p->y >> 8;
        int32_t dx = (int32_t)cx - cur_px;
        int32_t dy = (int32_t)cy - cur_py;
        uint32_t dist = her_isqrt((uint32_t)(dx * dx + dy * dy));

        /* Absorción al tocar o penetrar el horizonte de sucesos */
        if (dist <= horizon_r || dist == 0u) {
            p->x = (int32_t)(cx << 8);
            p->y = (int32_t)(cy << 8);
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->absorbed = 1;
            her_vortex_total_absorbed++;
            ofx_on_absorb(p->color); /* SOMA_ORB_FX_PATCH */
            /* La particula desaparece al tocar el horizonte: sin energia,
             * sin pulso, sin cambio de color y sin reinyeccion en la rosquilla. */
            continue;
        }
        active_in_flight++;
        p->prev_x = p->x;
        p->prev_y = p->y;

        int max_burst = soma_text_vortex ? 4 : 2;
        if (frame < max_burst) {
            /* Desprendimiento de aplicaciones reducido a solo 2 frames */
            p->x += p->vx;
            p->y += p->vy;
            p->vx = (p->vx * 192) >> 8;
            p->vy = (p->vy * 192) >> 8;
            int32_t shrink_w = (int32_t)p->w_size * (max_burst - frame) / max_burst;
            int32_t shrink_h = (int32_t)p->h_size * (max_burst - frame) / max_burst;
            if (shrink_w < 1) shrink_w = 1;
            if (shrink_h < 1) shrink_h = 1;
            p->w_size = (int16_t)shrink_w;
            p->h_size = (int16_t)shrink_h;
        } else {
            /* Fase 2: Integración al doble de velocidad en disco y horizonte */
            /* SOMA FAST ABSORB v1 */
            int32_t pull_spd, swirl_spd;
            if (soma_text_vortex) {
                /* Arrastre de letras e iconos */
                if (dist > 180u)      { pull_spd = 36; swirl_spd = 12; }
                else if (dist > 90u)  { pull_spd = 28; swirl_spd = 18; }
                else if (dist > 45u)  { pull_spd = 18; swirl_spd = 24; }
                else                  { pull_spd = 12; swirl_spd = 28; }
            } else {
                /* Aplicaciones: el doble de rapido (absorcion a la mitad de tiempo) */
                if (dist > 300u)      { pull_spd = 96; swirl_spd = 20; }
                else if (dist > 160u) { pull_spd = 72; swirl_spd = 24; }
                else if (dist > 85u)  { pull_spd = 56; swirl_spd = 26; }
                else if (dist > 45u)  { pull_spd = 32; swirl_spd = 18; }
                else                  { pull_spd = 22; swirl_spd = 10; }
            }

            /* El giro tangencial aleja la particula del centro (~s^2 / 2d). Se
             * compensa en el empuje radial para que SIEMPRE se acerque al horizonte
             * y no se quede orbitando alrededor del agujero negro. */
            int32_t d32 = (int32_t)dist;
            if (swirl_spd > d32 / 3) swirl_spd = d32 / 3;
            pull_spd += (swirl_spd * swirl_spd) / (2 * d32);
            if (pull_spd > d32 - 20) pull_spd = d32 - 20;
            if (pull_spd < 4) pull_spd = 4;

            int32_t step_x = (int32_t)(((int64_t)dx * pull_spd +
                                         (int64_t)dy * swirl_spd) * 256 / (int32_t)dist);
            int32_t step_y = (int32_t)(((int64_t)dy * pull_spd -
                                         (int64_t)dx * swirl_spd) * 256 / (int32_t)dist);
            p->vx = step_x;
            p->vy = step_y;
            p->x += p->vx;
            p->y += p->vy;
        }
    }

    soma_vortex_boost = (active_in_flight > 0) ? (soma_text_vortex ? 12 : 8) : 0;
    if (active_in_flight == 0) {
        her_vortex_closing = 0;
        soma_vortex_boost = 0;
        her_energy = 0u;
        her_pulse = 0u;
        /* Al ser absorbida la última partícula de texto, surgen los iconos del dock */
        if (her_dock_pending_open) {
            her_dock_pending_open = 0;
            soma_text_vortex = 0;
            her_icons_visible = 1;
            her_icons_anim_phase = 0;
            her_icons_burst_opening = 1;
            her_icons_burst_at = timer_get_uptime_ms();
        }
    }
}

/* ================= SOMA_ORB_FX_PATCH (B): estelas cometarias ================= */
static void ofx_vortex_trails(void)
{
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb || w <= 0 || h <= 0) return;
    int32_t cx = w / 2, cy = h / 2;

    for (int i = 0; i < HER_VORTEX_MAX; ++i) {
        struct her_vortex_p *p = &her_vortex_parts[i];
        if (p->absorbed) continue;
        int32_t x = p->x >> 8, y = p->y >> 8;
        if (x < -64 || y < -64 || x > w + 64 || y > h + 64) continue;

        int32_t dx = cx - x, dy = cy - y;
        uint32_t dist = her_isqrt((uint32_t)(dx * dx + dy * dy));
        /* 0 lejos ... 256 pegada al horizonte: se vuelve blanca y se estira */
        uint32_t hot = (dist >= 220u) ? 0u : (220u - dist) * 256u / 220u;
        uint32_t base = p->color & 0x00FFFFFFu;
        uint32_t c_head = ofx_mul(her_mix256(base, 0x00FFFFFFu, hot), 215u);
        uint32_t c_tail = ofx_mul(base, 34u);

        int32_t stretch = 200 + (int32_t)hot;                  /* x0.8 .. x1.8 */
        int32_t tx = (p->x - ((p->vx * stretch) >> 8)) >> 8;
        int32_t ty = (p->y - ((p->vy * stretch) >> 8)) >> 8;
        ofx_line(bb, w, h, tx, ty, x, y, c_tail, c_head);

        uint32_t g = ofx_mul(c_head, 80u);
        ofx_px(bb, w, h, x + 1, y, g); ofx_px(bb, w, h, x - 1, y, g);
        ofx_px(bb, w, h, x, y + 1, g); ofx_px(bb, w, h, x, y - 1, g);
    }
}
/* ================= fin SOMA_ORB_FX_PATCH (B) ================= */

static void her_draw_vortex_particles(void)
{
    if (!her_vortex_closing) return;
    ofx_vortex_trails();
    uint32_t w = fb_get_width(), h = fb_get_height();
    for (int i = 0; i < HER_VORTEX_MAX; ++i) {
        struct her_vortex_p *p = &her_vortex_parts[i];
        if (p->absorbed) continue;
        int32_t x = p->x >> 8, y = p->y >> 8;
        int32_t px = p->prev_x >> 8, py = p->prev_y >> 8;
        if (x < 0 || y < 0 || x >= (int32_t)w || y >= (int32_t)h) continue;
        if (px >= 0 && py >= 0 && px < (int32_t)w && py < (int32_t)h &&
            (px != x || py != y))
            fb_draw_line(px, py, x, y, her_scale(p->color, 150u));
        if (her_vortex_frame < (soma_text_vortex ? 4 : 2) && p->w_size > 2 && p->h_size > 2) {
            int32_t dw = p->w_size, dh = p->h_size;
            if (x + dw > (int32_t)w) dw = (int32_t)w - x;
            if (y + dh > (int32_t)h) dh = (int32_t)h - y;
            if (dw > 2 && dh > 2) {
                fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)dw,
                             (uint32_t)dh, p->color);
                fb_draw_rect((uint32_t)x, (uint32_t)y, (uint32_t)dw,
                             (uint32_t)dh, p->edge_color);
            } else {
                fb_fill_circle(x, y, 2, p->color);
            }
            continue;
        }
        if (p->size > 1) {
            fb_fill_circle(x, y, (int32_t)p->size, p->color);
            fb_draw_pixel((uint32_t)x, (uint32_t)y, 0x00FFFFFFu);
        } else {
            fb_draw_pixel((uint32_t)x, (uint32_t)y, p->color);
        }
    }
}

/* Destrucción integral en partículas de los diálogos de usuario y LLM */
static void her_start_text_destruction(uint32_t cur_w, uint32_t cur_h)
{
    her_text_suppressed_by_dock = 1;
    if (cur_w == 0u || cur_h == 0u) {
        her_last_q[0] = '\0';
        her_ans_len = her_ans_reveal = 0;
        her_thk_len = her_thk_reveal = 0;
        agent_global_len = agent_global_cursor = 0;
        agent_global_cmd[0] = '\0';
        her_energy = 0;
        her_dock_pending_open = 1;
        return;
    }

    volatile uint32_t *fb = fb_get_buffer();
    if (!fb) {
        her_last_q[0] = '\0';
        her_ans_len = her_ans_reveal = 0;
        her_thk_len = her_thk_reveal = 0;
        agent_global_len = agent_global_cursor = 0;
        agent_global_cmd[0] = '\0';
        her_energy = 0;
        her_dock_pending_open = 1;
        return;
    }

    uint32_t cx = cur_w / 2u, cy = cur_h / 2u;
    uint32_t top0 = (cy > 170u) ? cy - 170u : 12u;
    uint32_t top1 = (cy > 170u) ? cy - 105u : 64u;
    uint32_t bottom0 = cy + 105u;
    uint32_t bottom1 = (cur_h > 35u) ? cur_h - 35u : cur_h;
    if (top0 > cur_h) top0 = cur_h;
    if (top1 > cur_h) top1 = cur_h;
    if (bottom0 > cur_h) bottom0 = cur_h;
    if (bottom1 > cur_h) bottom1 = cur_h;

    int p_idx = 0;

    /* Captura completa de todos los píxeles iluminados de las letras en pantalla */
    for (uint32_t y = 0; y < cur_h && p_idx < HER_VORTEX_MAX; ++y) {
        if (!((y >= top0 && y < top1) || (y >= bottom0 && y < bottom1))) continue;
        uint32_t row = y * cur_w;
        for (uint32_t x = 30; x < cur_w - 30 && p_idx < HER_VORTEX_MAX; x += 2) {
            uint32_t sample = fb[row + x] & 0x00FFFFFFu;
            uint32_t r = (sample >> 16) & 0xFFu;
            uint32_t g = (sample >> 8)  & 0xFFu;
            uint32_t b = sample & 0xFFu;
            if (r + g + b < 40u) continue; /* Fondo cósmico oscuro */

            struct her_vortex_p *p = &her_vortex_parts[p_idx++];
            p->x = (int32_t)(x << 8);
            p->y = (int32_t)(y << 8);
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->absorbed = 0;
            p->w_size = 3;
            p->h_size = 3;
            p->size = ((p_idx & 3) == 0) ? 2 : 1;
            p->color = sample;
            p->edge_color = her_mix256(sample, 0x00FFFFFFu, 180u);

            /* Impulso de fragmentación */
            int32_t ox = (int32_t)x - (int32_t)cx;
            int32_t oy = (int32_t)y - (int32_t)cy;
            int32_t rnd_x = (int32_t)(her_rand() & 31u) - 15;
            int32_t rnd_y = (int32_t)((her_rand() >> 5) & 31u) - 15;
            p->vx = (ox * 60) / (int32_t)cur_w + rnd_x * 12;
            p->vy = (oy * 60) / (int32_t)cur_h + rnd_y * 12;
        }
    }

    /* Limpieza total de los textos para dar paso exclusivamente a las partículas */
    her_last_q[0] = '\0';
    her_ans_len = 0;
    her_ans_reveal = 0;
    her_thk_len = 0;
    her_thk_reveal = 0;
    agent_global_len = 0;
    agent_global_cursor = 0;
    agent_global_cmd[0] = '\0';
    her_energy = 0;

    her_vortex_frame = 0;
    her_vortex_total_absorbed = 0;

    if (p_idx > 0) {
        her_vortex_closing = 1;
        soma_vortex_boost = 14;
        soma_text_vortex = 1;
        her_dock_pending_open = 1;
    } else {
        her_vortex_closing = 0;
        soma_text_vortex = 0;
        her_dock_pending_open = 0;
        her_icons_visible = 1;
        her_icons_burst_opening = 1;
        her_icons_burst_at = timer_get_uptime_ms();
    }
}


/* Absorción de los iconos orbitales por el disco de acreción del agujero negro */
static void her_close_icons_vortex(uint32_t cur_w, uint32_t cur_h)
{
    her_power_pending = 0;
    if (cur_w == 0u || cur_h == 0u) {
        her_icons_visible = 0;
        her_icons_anim_phase = 0;
        her_dock_pending_open = 0;
        her_text_suppressed_by_dock = 0;
        agent_global_focus = 1;
        return;
    }

    uint64_t now_ms = timer_get_uptime_ms();
    int p_idx = 0;

    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
        int32_t target_x, target_y;
        her_icon_orbit_target(i, cur_w, cur_h, now_ms, &target_x, &target_y);
        uint32_t col = her_dock_items[i].color;

        /* Generar 20 partículas por cada icono orbital */
        for (int k = 0; k < 20 && p_idx < HER_VORTEX_MAX; ++k) {
            struct her_vortex_p *p = &her_vortex_parts[p_idx++];
            int32_t ox = (int32_t)(her_rand() % 56u) - 28;
            int32_t oy = (int32_t)(her_rand() % 56u) - 28;
            int32_t px = target_x + ox;
            int32_t py = target_y + oy;
            if (px < 0) px = 0;
            if (px >= (int32_t)cur_w) px = (int32_t)cur_w - 1;
            if (py < 0) py = 0;
            if (py >= (int32_t)cur_h) py = (int32_t)cur_h - 1;

            p->x = px << 8;
            p->y = py << 8;
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->absorbed = 0;
            p->w_size = 4;
            p->h_size = 4;
            p->size = ((k & 3) == 0) ? 2 : 1;
            p->color = (k & 1) ? col : 0x00FFFFFFu;
            p->edge_color = her_mix256(col, 0x00FFFFFFu, 180u);

            int32_t rnd_x = (int32_t)(her_rand() & 15u) - 7;
            int32_t rnd_y = (int32_t)((her_rand() >> 4) & 15u) - 7;
            p->vx = rnd_x * 16;
            p->vy = rnd_y * 16;
        }
    }

    /* Ocultar iconos de inmediato y habilitar entrada al agente */
    her_icons_visible = 0;
    her_icons_anim_phase = 0;
    her_icons_burst_at = 0;
    her_dock_pending_open = 0;
    her_text_suppressed_by_dock = 0;
    agent_global_focus = 1;
    agent_global_len = 0;
    agent_global_cursor = 0;
    agent_global_cmd[0] = '\0';

    her_vortex_frame = 0;
    her_vortex_total_absorbed = 0;
    her_vortex_closing = (p_idx > 0) ? 1 : 0;
    soma_text_vortex = 1; /* Arrastre dinámico hacia el disco de acreción */
    soma_vortex_boost = 14;
    mouse_erase_cursor();
}

static uint32_t g_live_win_x = 0, g_live_win_y = 0, g_live_win_w = 0, g_live_win_h = 0;
static int g_live_ready = 0;
static int g_live_busy = 0;

static int live_has(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) return 0;
    for (; *hay; ++hay) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return 1;
    }
    return 0;
}

static int live_path_allowed(const char *name)
{
    static const char *const deny[] = {
        "/sys/", "/agent/", "/proc/", "/dev/", "_stm.txt", "/etc/history.txt", 0
    };
    for (int i = 0; deny[i]; ++i) {
        if (live_has(name, deny[i])) return 0;
    }
    return 1;
}

static int live_data_is_text(const char *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)d[i];
        if (c < 9 || (c > 13 && c < 32)) return 0;
    }
    return 1;
}

/* ====================================================================
 * SOMA_PARCHE_V2_GUI: hilo de fondo (animacion + raton) y hook en vivo
 * Las herramientas del agente ya NO detienen la animacion.
 * ==================================================================== */
static int32_t g_mouse_x_s, g_mouse_y_s;

static void gui_live_paint(void)
{
    if (!fb_is_active()) return;
    if (her_anim_alive) return;      /* el hilo de fondo repinta a 30 FPS */
    her_refresh_all();
    fb_present();
}

/*PL12_SOMA_WIN_IMPL*/
/* ====================================================================
 * SUBSISTEMA DE VENTANA ESTANDAR SOMA-LANG (Her-Aura Canvas)
 * ==================================================================== */
#define SOMA_APP_SURF_W 1024
#define SOMA_APP_SURF_H 768

static uint32_t *soma_app_surf = 0;
static uint32_t  soma_app_bg_col = 0x000F172A;
static char      soma_app_title_buf[64] = "SOMA App";
static volatile int soma_app_active_flag = 0;
static volatile int soma_app_clicked_flag = 0;
static int       soma_app_key_queue[32];
static int       soma_app_key_head = 0;
static int       soma_app_key_tail = 0;

static void soma_app_ensure_surf(void)
{
    if (!soma_app_surf) {
        soma_app_surf = (uint32_t *)kmalloc(SOMA_APP_SURF_W * SOMA_APP_SURF_H * sizeof(uint32_t));
        if (soma_app_surf) {
            for (uint32_t i = 0; i < SOMA_APP_SURF_W * SOMA_APP_SURF_H; ++i)
                soma_app_surf[i] = soma_app_bg_col;
        }
    }
}

const char *gui_soma_win_get_title(void)
{
    return soma_app_title_buf[0] ? soma_app_title_buf : "SOMA App";
}

int gui_soma_win_is_active(void)
{
    if (!fb_is_active()) return 0;
    return soma_app_active_flag && (her_find_window(APP_SOMA_APP) >= 0);
}

void gui_soma_win_on_close(void)
{
    soma_app_active_flag = 0;
}

/* PARCHE 098: un programa SOMA-LANG corre en su propio hilo y dibuja en su superficie; el escritorio sigue vivo y es SU bucle quien
 * abre, compone y cierra la ventana. Antes el programa corria dentro del hilo del escritorio (todo congelado) o se adueñaba de la
 * pantalla. */
static volatile uint32_t g_gui_tid;
static volatile int g_soma_req_open, g_soma_req_close, g_soma_dirty;
static char g_soma_req_title[64]; static int g_soma_req_w, g_soma_req_h;
static int gui_on_gui_thread(void) { if (!g_gui_loop_on) return 1; struct tcb *t = thread_current(); return t && t->tid == g_gui_tid; }

/* SOMA_221_RESCATE: contencion de aplicaciones del escritorio (x86_64).
 * Las aplicaciones corren en el hilo del escritorio, en ring 0: un puntero nulo o un bucle sin fin dentro de cualquiera de ellas
 * tumbaba o congelaba el sistema entero. Ahora, si ocurre mientras el escritorio da paso a una aplicacion (fuera del asignador de
 * memoria y sin un reparto entre nucleos en curso):
 *   - una excepcion de CPU (#DE, #UD, #GP, #PF) o un desbordamiento de pila detectado por el protector, o
 *   - mas de HER_HANG_MS de procesador seguidos en una sola llamada (la interfaz llevaria ese tiempo congelada),
 * el hilo vuelve al principio del bucle del escritorio, la ventana de esa aplicacion se cierra (su estado se da por perdido: no se
 * libera, por si esta danado) y un aviso dice que paso. Es contencion, no aislamiento: tras HER_RESCUE_MAX rescates en un arranque
 * el siguiente fallo vuelve a ser un panico. */
#define HER_HANG_MS    10000u
#define HER_RESCUE_MAX 16
#if defined(__x86_64__)
static void *her_jb[5];
static volatile int her_jb_armed, her_rescuing;
static int her_rescue_app = -1, her_rescue_kind, her_rescue_vec;
static uint64_t her_rescue_rip, her_rescue_addr;
static uint32_t her_rescues;
static uint8_t her_rescue_stack[16384] __attribute__((aligned(16)));
static char her_rescue_msg[160];
volatile int gui_rescue_inject;                                   /* pruebas: 1 = fallo de memoria, 2 = bucle sin fin */
static int her_rescue_ok(void)
{
    if (!her_jb_armed || her_rescuing || !g_gui_loop_on || her_rescues >= HER_RESCUE_MAX) return 0;
    if (her_guard_owner < ACCT_APP0 || her_guard_owner == ACCT_APP0 + APP_DESKTOP) return 0;
    struct tcb *t = thread_current();
    if (!t || t->tid != g_gui_tid) return 0;
    return !kheap_busy() && !smp_busy();
}
static __attribute__((noreturn, noinline)) void her_rescue_entry(void)
{
    kmutex_release_all_for_thread(thread_current());             /* como al matar un hilo: cerrojos que tenia a medias */
    __builtin_longjmp(her_jb, 1);
}
static void her_rescue_arm(int kind, uint64_t *rip, uint64_t *rsp)
{
    her_rescuing = 1; her_jb_armed = 0;
    her_rescue_kind = kind; her_rescue_app = her_guard_owner - ACCT_APP0;
    *rip = (uint64_t)(uintptr_t)her_rescue_entry;
    *rsp = (((uint64_t)(uintptr_t)her_rescue_stack + sizeof her_rescue_stack) & ~15ull) - 8;
}
int gui_fault_recover(int vector, uint64_t fault_rip, uint64_t fault_addr, uint64_t *rip, uint64_t *rsp)
{
    if (vector != 0 && vector != 6 && vector != 13 && vector != 14) return 0;
    if (!her_rescue_ok()) return 0;
    her_rescue_vec = vector; her_rescue_rip = fault_rip; her_rescue_addr = fault_addr;
    her_rescue_arm(1, rip, rsp);
    return 1;
}
int gui_hang_tick(uint64_t *rip, uint64_t *rsp)                  /* cada milisegundo, desde la interrupcion del temporizador */
{
    if (her_guard_owner < ACCT_APP0 || !her_jb_armed) return 0;
    struct tcb *t = thread_current();
    if (!t || t->tid != g_gui_tid) return 0;                      /* solo cuenta el tiempo que el escritorio esta en la CPU */
    if (++her_guard_ticks < HER_HANG_MS) return 0;
    if (!her_rescue_ok()) { her_guard_ticks = HER_HANG_MS / 2; return 0; }   /* en un momento delicado: se vuelve a mirar luego */
    her_rescue_vec = -2; her_rescue_rip = *rip; her_rescue_addr = 0;
    her_rescue_arm(2, rip, rsp);
    return 1;
}
void gui_fault_from_ssp(void)
{
    if (!her_rescue_ok()) return;
    her_rescue_vec = -1; her_rescue_rip = (uint64_t)(uintptr_t)__builtin_return_address(0); her_rescue_addr = 0;
    her_rescuing = 1; her_jb_armed = 0; her_rescue_kind = 1; her_rescue_app = her_guard_owner - ACCT_APP0;
    her_rescue_entry();
}
static void her_rc_cat(const char *t) { uint32_t n = 0; while (her_rescue_msg[n]) ++n; while (*t && n + 1 < sizeof her_rescue_msg) her_rescue_msg[n++] = *t++; her_rescue_msg[n] = 0; }
#endif
static int gui_soma_win_open_impl(const char *title, int w, int h);
static void gui_soma_win_close_impl(void);
int gui_soma_win_open(const char *title, int w, int h)
{
    if (gui_on_gui_thread()) return gui_soma_win_open_impl(title, w, h);
    uint32_t i = 0; if (!title) title = "";
    while (title[i] && i < sizeof g_soma_req_title - 1) { g_soma_req_title[i] = title[i]; i++; }
    g_soma_req_title[i] = 0; g_soma_req_w = w; g_soma_req_h = h;
    soma_app_ensure_surf();
    g_soma_req_open = 1;
    for (uint32_t k = 0; g_soma_req_open && g_gui_loop_on && k < 3000u; k++) thread_sleep(1);
    return gui_soma_win_is_active();
}
static int g_soma_win_tok;   /* PARCHE 091: un programa grafico lanzado por el agente tiene la pantalla hasta que cierra */
void gui_soma_win_close(void)
{
    if (!gui_on_gui_thread()) {                       /* PARCHE 098 */
        soma_app_active_flag = 0; g_soma_req_close = 1;
        for (uint32_t k = 0; g_soma_req_close && g_gui_loop_on && k < 2000u; k++) thread_sleep(1);
        return;
    }
    gui_soma_win_close_impl();
}
static void gui_soma_win_close_impl(void)
{
    soma_app_active_flag = 0;
    int idx = her_find_window(APP_SOMA_APP);
    if (idx >= 0) {
        her_close_target_window_vortex(idx);
    } else {
        int mi = her_find_minimized(APP_SOMA_APP);
        if (mi >= 0) her_kill_minimized(mi);
    }
    if (g_soma_win_tok) { int t = g_soma_win_tok; g_soma_win_tok = 0; gui_leave(t); }
}

static void gui_soma_service(int32_t cmx, int32_t cmy)   /* llamado en cada vuelta del bucle del escritorio */
{
    if (g_soma_req_open) { gui_soma_win_open_impl(g_soma_req_title, g_soma_req_w, g_soma_req_h); g_soma_req_open = 0; }
    if (g_soma_req_close) { gui_soma_win_close_impl(); g_soma_req_close = 0; her_refresh_all(); fb_present(); }
    if (g_soma_dirty) {
        g_soma_dirty = 0;
        int si = her_find_window(APP_SOMA_APP);
        if (si >= 0 && !her_dragging && !her_resizing) {
            struct her_win_node *sw = &her_windows[si];
            int covered = 0;
            for (int j = si + 1; j < her_num_windows; j++) {
                struct her_win_node *o = &her_windows[j];
                if (o->x < sw->x + sw->w && sw->x < o->x + o->w && o->y < sw->y + sw->h && sw->y < o->y + o->h) { covered = 1; break; }
            }
            if (!covered && settings_opacity() >= 100) her_render_app_body(APP_SOMA_APP, HER_CLI_X(sw->x), HER_CLI_Y(sw->y), HER_CLI_W(sw->w), HER_CLI_H(sw->h), si == her_num_windows - 1);
            else her_refresh_all();
            if (her_cursor_should_show()) mouse_draw_cursor(cmx, cmy);
            fb_present();
        }
    }
}
static int gui_soma_win_open_impl(const char *title, int w, int h)
{
    if (!fb_is_active()) return 0;
    soma_app_ensure_surf();
    if (title && title[0]) {
        uint32_t i = 0;
        while (title[i] && i < sizeof(soma_app_title_buf) - 1) {
            soma_app_title_buf[i] = title[i];
            i++;
        }
        soma_app_title_buf[i] = '\0';
    } else {
        soma_app_title_buf[0] = 'S'; soma_app_title_buf[1] = 'O'; soma_app_title_buf[2] = 'M';
        soma_app_title_buf[3] = 'A'; soma_app_title_buf[4] = ' '; soma_app_title_buf[5] = 'A';
        soma_app_title_buf[6] = 'p'; soma_app_title_buf[7] = 'p'; soma_app_title_buf[8] = '\0';
    }

    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    if (cur_w == 0 || cur_h == 0) return 0;

    int existing = her_find_window(APP_SOMA_APP);
    if (existing >= 0) {
        her_bring_to_front(existing);
        if (w >= 120 && h >= 80) {
            uint32_t nw = (uint32_t)w;
            uint32_t nh = (uint32_t)(h + HER_TITLE_H);
            if (nw > cur_w - 20) nw = cur_w - 20;
            if (nh > cur_h - 20) nh = cur_h - 20;
            her_windows[existing].w = nw;
            her_windows[existing].h = nh;
        }
        soma_app_active_flag = 1;
        gui_live_paint();
        return 1;
    }

    her_open_app(APP_SOMA_APP, cur_w, cur_h);
    int idx = her_find_window(APP_SOMA_APP);
    if (idx >= 0 && w >= 120 && h >= 80) {
        uint32_t nw = (uint32_t)w;
        uint32_t nh = (uint32_t)(h + HER_TITLE_H);
        if (nw > cur_w - 20) nw = cur_w - 20;
        if (nh > cur_h - 20) nh = cur_h - 20;
        her_windows[idx].w = nw;
        her_windows[idx].h = nh;
    }
    soma_app_active_flag = 1;
    gui_live_paint();
    return 1;
}

void gui_soma_win_clear(uint32_t color)
{
    soma_app_ensure_surf();
    if (!soma_app_surf) return;
    soma_app_bg_col = color;
    for (uint32_t i = 0; i < SOMA_APP_SURF_W * SOMA_APP_SURF_H; ++i) {
        soma_app_surf[i] = color;
    }
}

void gui_soma_win_pixel(int x, int y, uint32_t color)
{
    soma_app_ensure_surf();
    if (!soma_app_surf) return;
    if (x >= 0 && x < (int)SOMA_APP_SURF_W && y >= 0 && y < (int)SOMA_APP_SURF_H) {
        soma_app_surf[y * SOMA_APP_SURF_W + x] = color;
    }
}

void gui_soma_win_rect(int x, int y, int w, int h, uint32_t color)
{
    soma_app_ensure_surf();
    if (!soma_app_surf || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)SOMA_APP_SURF_W) w = (int)SOMA_APP_SURF_W - x;
    if (y + h > (int)SOMA_APP_SURF_H) h = (int)SOMA_APP_SURF_H - y;
    if (w <= 0 || h <= 0) return;

    for (int r = 0; r < h; ++r) {
        uint32_t *row = soma_app_surf + (y + r) * SOMA_APP_SURF_W + x;
        for (int c = 0; c < w; ++c) {
            row[c] = color;
        }
    }
}

void gui_soma_win_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    soma_app_ensure_surf();
    if (!soma_app_surf) return;
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        gui_soma_win_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void gui_soma_win_circle(int cx, int cy, int r, uint32_t color)
{
    soma_app_ensure_surf();
    if (!soma_app_surf || r <= 0) return;
    int x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        gui_soma_win_rect(cx - x, cy + y, 2 * x + 1, 1, color);
        gui_soma_win_rect(cx - x, cy - y, 2 * x + 1, 1, color);
        gui_soma_win_rect(cx - y, cy + x, 2 * y + 1, 1, color);
        gui_soma_win_rect(cx - y, cy - x, 2 * y + 1, 1, color);
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

void gui_soma_win_text(int x, int y, const char *str, uint32_t color)
{
    soma_app_ensure_surf();
    if (!soma_app_surf || !str) return;
    int cur_x = x;
    int cur_y = y;
    while (*str) {
        char ch = *str++;
        if (ch == '\n') {
            cur_x = x;
            cur_y += 10;
            continue;
        }
        if (ch == ' ') {
            cur_x += 8;
            continue;
        }
        const uint8_t *g = font_get_glyph(ch);
        if (g) {
            for (int r = 0; r < 8; ++r) {
                uint8_t bits = g[r];
                for (int c = 0; c < 8; ++c) {
                    if (bits & (1 << (7 - c))) {
                        gui_soma_win_pixel(cur_x + c, cur_y + r, color);
                    }
                }
            }
        }
        cur_x += 8;
    }
}

void gui_soma_win_present(void)
{
    if (!fb_is_active()) return;
    if (!gui_on_gui_thread()) { g_soma_dirty = 1; return; }   /* PARCHE 098: el escritorio compone el fotograma */
    int idx = her_find_window(APP_SOMA_APP);
    if (idx >= 0) {
        struct her_win_node *win = &her_windows[idx];
        fb_damage((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h);
    }
    fb_present();
}

void gui_soma_win_get_size(int *out_w, int *out_h)
{
    int idx = her_find_window(APP_SOMA_APP);
    if (idx >= 0) {
        *out_w = (int)her_windows[idx].w - 4;
        *out_h = (int)her_windows[idx].h - HER_TITLE_H - 3;
        if (*out_w < 10) *out_w = 10;
        if (*out_h < 10) *out_h = 10;
    } else {
        *out_w = 640;
        *out_h = 420;
    }
}

void gui_soma_win_get_mouse(int *out_rx, int *out_ry, int *out_btn, int *out_inside, int *out_clicked)
{
    int idx = her_find_window(APP_SOMA_APP);
    if (idx >= 0) {
        struct her_win_node *win = &her_windows[idx];
        int cx = (int)win->x + 2;
        int cy = (int)win->y + HER_TITLE_H + 1;
        int cw = (int)win->w - 4;
        int ch = (int)win->h - HER_TITLE_H - 3;
        int rx = g_mouse_x_s - cx;
        int ry = g_mouse_y_s - cy;
        *out_rx = rx;
        *out_ry = ry;
        *out_btn = mouse_left_held();
        *out_inside = (rx >= 0 && ry >= 0 && rx < cw && ry < ch);
        *out_clicked = soma_app_clicked_flag;
        soma_app_clicked_flag = 0;
    } else {
        *out_rx = 0;
        *out_ry = 0;
        *out_btn = 0;
        *out_inside = 0;
        *out_clicked = 0;
    }
}

void gui_soma_win_record_click(void)
{
    soma_app_clicked_flag = 1;
}

void gui_soma_win_push_key(int key)
{
    int next = (soma_app_key_head + 1) % 32;
    if (next != soma_app_key_tail) {
        soma_app_key_queue[soma_app_key_head] = key;
        soma_app_key_head = next;
    }
}

int gui_soma_win_get_key(void)
{
    if (soma_app_key_head != soma_app_key_tail) {
        int k = soma_app_key_queue[soma_app_key_tail];
        soma_app_key_tail = (soma_app_key_tail + 1) % 32;
        return k;
    }
    if (!gui_on_gui_thread()) return 0;                /* PARCHE 098: con el escritorio vivo, las teclas llegan por la cola */
    if (kgetc_ready()) {
        return (unsigned char)kgetc();
    }
    return 0;
}

void gui_soma_win_render_body(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    if (!soma_app_surf) {
        fb_fill_rect(wx, wy, ww, wh, soma_app_bg_col);
        return;
    }
    uint32_t copy_w = (ww < SOMA_APP_SURF_W) ? ww : SOMA_APP_SURF_W;
    uint32_t copy_h = (wh < SOMA_APP_SURF_H) ? wh : SOMA_APP_SURF_H;

    fb_blit((int32_t)wx, (int32_t)wy, soma_app_surf, (int32_t)copy_w, (int32_t)copy_h, SOMA_APP_SURF_W);
    if (copy_w < ww) {
        fb_fill_rect(wx + copy_w, wy, ww - copy_w, copy_h, soma_app_bg_col);
    }
    if (copy_h < wh) {
        fb_fill_rect(wx, wy + copy_h, ww, wh - copy_h, soma_app_bg_col);
    }
}

static void gui_live_file_hook_body(const char *name, const char *data, uint32_t len);
static void her_open_notepad_path(const char *path, uint32_t cw, uint32_t ch);
static void gui_live_file_hook(const char *name, const char *data, uint32_t len)
{
    int tok = gui_enter();              /* PARCHE 091 */
    gui_live_file_hook_body(name, data, len);
    gui_leave(tok);
}
static void gui_live_file_hook_body(const char *name, const char *data, uint32_t len)
{
    if (!g_live_ready || g_live_busy || !name || !name[0]) return;
    if (!fb_is_active()) return;

    struct tcb *self = thread_current();
    if (!self || !self->agent_mode) return;
    if (self->tid != 0 && !gui_in_agent_thread()) return;   /* PARCHE 091: tambien desde el hilo del agente */

    if (!live_path_allowed(name)) return;
    if (!data) { data = ""; len = 0; }
    if (!live_data_is_text(data, len)) return;

    g_live_busy = 1;
    uint32_t cw = fb_get_width(), ch = fb_get_height();

    if (live_has(data, "#SOMA-LAYOUT") || live_has(name, "_layout.txt") ||
        live_has(name, "layout.txt") || live_has(name, ".tex") || live_has(data, "\\documentclass")) {   /* PARCHE 084: LaTeX */
        her_open_app(APP_LAYOUT, cw, ch);
        layout_set_source(data, len);
        gui_live_paint();
        g_live_busy = 0;
        return;
    }

    her_open_notepad_path(name, cw, ch);                 /* SOMA_211_NOTAS: la ventana de ese archivo, o una nueva */
    notepad_live_begin(name, data, len);
    gui_live_paint();

    uint32_t chunk = 4u + len / 80u;
    if (chunk > 200u) chunk = 200u;

    while (notepad_live_step(chunk) > 0) {
        gui_live_paint();
        thread_sleep(20);
    }

    notepad_live_end();
    gui_live_paint();
    g_live_busy = 0;
}

static int gui_bg_mouse_poll(void)
{
    uint32_t w = fb_get_width(), h = fb_get_height();
    int moved = 0;
    if (w == 0u || h == 0u) return 0;
    for (int i = 0; i < 8; ++i) {
        int32_t dx = 0, dy = 0; int clk = 0;
        if (!mouse_poll(&dx, &dy, &clk)) break;
        if (dx || dy) { g_mouse_x_s += dx; g_mouse_y_s -= dy; moved = 1; }
    }
    if (g_mouse_x_s < 0) g_mouse_x_s = 0;
    if (g_mouse_x_s >= (int32_t)w) g_mouse_x_s = (int32_t)w - 1;
    if (g_mouse_y_s < 0) g_mouse_y_s = 0;
    if (g_mouse_y_s >= (int32_t)h) g_mouse_y_s = (int32_t)h - 1;
    return moved;
}

static void gui_bg_cursor(void)
{
    if (her_cursor_should_show()) mouse_draw_cursor(g_mouse_x_s, g_mouse_y_s);
    else mouse_erase_cursor();
}

static void her_anim_worker(void *arg)
{
    (void)arg;
    uint64_t last = 0;
    /* SOMA_155D: con ventanas abiertas cada fotograma de fondo recompone la pantalla entera (escritorio + orbe + todas las
     * ventanas + volcado completo). A 30 fps fijos eso se comia la CPU en hardware real (T480s: un nucleo, 1080p/1440p y
     * volcado a la VRAM real) y el raton iba a saltos. Ahora: (1) se mide lo que cuesta ese fotograma y, con ventanas, se
     * espacian para que el fondo use como mucho ~1/4 de la CPU (entre 30 y 4 fps); (2) mientras se mueve el raton o se teclea
     * el fondo espera 150 ms, asi el puntero y la ventana activa van primero. Escritorio sin ventanas: igual que antes. */
    uint64_t bg_cost = 0, last_input = 0;
    uint32_t kev_seen = console_kbd_events();
    while (her_agent_busy) {
        if (fb_is_active() && !fb_user_exclusive()) {          /* SOMA_PARCHE_021_FPU */
            int moved = gui_bg_mouse_poll();
            uint64_t now = timer_get_uptime_ms();
            uint32_t kev = console_kbd_events();
            if (moved || kev != kev_seen || mouse_left_held()) { last_input = now; kev_seen = kev; }
            uint64_t interval = 33u;
            int multi = smp_cpus() > 1;                         /* SOMA_164B_T480S: ver el bucle principal */
            if (her_num_windows > 0) {
                interval = bg_cost * (multi ? 2u : 4u);              /* SOMA_164C_T480S */
                if (interval < 33u) interval = 33u;
                if (interval > 250u) interval = 250u;
            }
            int interacting = her_num_windows > 0 && now - last_input < 150u && (!multi || now - last < 66u);
            if (now - last >= interval && !interacting) {
                last = now;
                uint32_t w = fb_get_width(), h = fb_get_height();
                her_angle = (her_angle + 3) & 255;
                if (her_icons_visible) {
                    if (her_icons_anim_phase < HER_ICON_ANIM_MAX) {
                        her_icons_anim_phase += 36u;
                        if (her_icons_anim_phase > HER_ICON_ANIM_MAX)
                            her_icons_anim_phase = HER_ICON_ANIM_MAX;
                    }
                } else if (her_icons_anim_phase > 0u) {
                    her_icons_anim_phase = (her_icons_anim_phase > 36u)
                                         ? her_icons_anim_phase - 36u : 0u;
                }
                her_reveal_step();
                her_parts_spawn(w / 2, h / 2, her_angle);
                her_vortex_close_step();
                her_refresh_all();
                gui_bg_cursor();
                fb_present();
                {   /* SOMA_155D: coste medio del fotograma de fondo (media movil) */
                    uint64_t c = timer_get_uptime_ms() - now;
                    bg_cost = bg_cost ? (bg_cost * 3u + c) / 4u : c;
                }
            } else if (moved) {
                gui_bg_cursor();
                fb_present();
            }
        }
        thread_sleep(4);
    }
    her_anim_alive = 0;
}

static void her_anim_start(void)
{
    her_agent_busy = 1;
    if (her_anim_alive) return;
    her_anim_alive = 1;
    if (!thread_create("her_anim", her_anim_worker, 0)) {
        her_anim_alive = 0;
        her_agent_busy = 0;
    }
}

/* Parada real: solo la llama quien termina la mision del agente */
static void her_anim_stop(void)
{
    her_agent_busy = 0;
    for (int i = 0; i < 400 && her_anim_alive; ++i)
        thread_sleep(5);
}

/* Obsoleta: antes las herramientas paraban la animacion y congelaban la GUI */
static void her_anim_halt(void)
{
}

static void ensure_window_geom(void)
{
    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    if (cur_w == 0 || cur_h == 0) return;
    her_place_window_corner(cur_w, cur_h, 0);
    g_live_win_x = HER_CLI_X(her_win_x);
    g_live_win_y = HER_CLI_Y(her_win_y);
    g_live_win_w = HER_CLI_W(her_win_w);
    g_live_win_h = HER_CLI_H(her_win_h);

}

/* SOMA_PARCHE_V2_GUI: las herramientas abren ventanas reales del sistema multiventana */
static int gui_win_open(enum app_mode app)
{
    if (!fb_is_active()) return 0;
    her_open_app(app, fb_get_width(), fb_get_height());
    gui_live_paint();
    return 1;
}

static void gui_open_browser__impl(const char *url)
{
    if (!url || !*url) return;
    char local_url[160];
    if (url[0] != '/' && memcmp(url, "file://", 7) != 0 && memcmp(url, "http://", 7) != 0 && memcmp(url, "https://", 8) != 0) {
        local_url[0] = '/';
        uint32_t p = 0;
        while (url[p] && p < sizeof(local_url) - 2) {
            local_url[p + 1] = url[p]; p++;
        }
        local_url[p + 1] = '\0'; url = local_url;
    }
    if (gui_win_open(APP_WEB_BROWSER)) {
        /* browser_init() lanza la demo en segundo plano: esperar antes de cargar la URL */
        for (int i = 0; i < 300 && browser_is_loading(); ++i) thread_sleep(10);
        browser_load_url(url);
        for (int i = 0; i < 30 && browser_is_loading(); ++i) thread_sleep(10);
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    current_app = APP_WEB_BROWSER;
    agent_global_focus = 0;
    browser_init();
    browser_load_url(url);
    for (int i = 0; i < 30 && browser_is_loading(); ++i) thread_sleep(10);
    g_pending_app = APP_WEB_BROWSER;                    /* PARCHE 086: antes se abria el escritorio sin la ventana */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_browser(const char *url)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_browser__impl(url);
    gui_leave(tok);
}

static void gui_open_file_manager__impl(const char *folder)
{
    if (fb_is_active()) {                                    /* SOMA_214_ARCHIVOS: la ventana de esa carpeta, o una nueva */
        her_open_app_path(APP_FILE_MANAGER, folder, fb_get_width(), fb_get_height());
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    current_app = APP_FILE_MANAGER;
    agent_global_focus = 0;
    if (folder && *folder) fm_open_folder(folder); else fm_init();
    g_pending_app = APP_FILE_MANAGER;                   /* PARCHE 086 */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_file_manager(const char *folder)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_file_manager__impl(folder);
    gui_leave(tok);
}

static void gui_open_memview__impl(void)
{
    if (gui_win_open(APP_AGENT_MEMORY)) return;
    ensure_window_geom();
    current_app = APP_AGENT_MEMORY;
    agent_global_focus = 0;
    memview_init();
    g_pending_app = APP_AGENT_MEMORY;                   /* PARCHE 120: sin esto la orden abria el escritorio sin la ventana */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_memview(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_memview__impl();
    gui_leave(tok);
}

/* PARCHE 085: Ajustes */
static void gui_open_settings__impl(void)
{
    if (gui_win_open(APP_SETTINGS)) return;
    ensure_window_geom();
    settings_load();
    g_pending_app = APP_SETTINGS;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_settings(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_settings__impl();
    gui_leave(tok);
}

/* PARCHE 085: "navegador" desde la consola (con la GUI apagada abre el escritorio con el navegador) */
static void gui_open_web__impl(void)
{
    if (gui_win_open(APP_WEB_BROWSER)) return;
    ensure_window_geom();
    g_pending_app = APP_WEB_BROWSER;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_web(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_web__impl();
    gui_leave(tok);
}

/* PARCHE 088: lector de PDF (con la GUI apagada abre el escritorio con la ventana) */
static void gui_open_pdf__impl(const char *path)
{
    if (fb_is_active()) { her_open_app_path(APP_PDF, path, fb_get_width(), fb_get_height()); gui_live_paint(); return; }   /* SOMA_213_PDF */
    ensure_window_geom();
    pdfview_init();
    if (path && *path) pdfview_open_file(path);
    g_pending_app = APP_PDF;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_pdf(const char *path)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_pdf__impl(path);
    gui_leave(tok);
}

/* PARCHE 089: para el Monitor del sistema */
const int gui_app_sysmon = APP_SYSMON;
const char *gui_app_title(int app)
{
    switch ((enum app_mode)app) {
    case APP_AGENT_CHAT: return "SOMA (agente)"; case APP_TERMINAL_SHELL: return "Terminal"; case APP_WEB_BROWSER: return "Navegador";
    case APP_FILE_MANAGER: return "Archivos"; case APP_IMAGE_VIEWER: return "Imagenes"; case APP_NOTEPAD: return "Bloc de notas";
    case APP_AGENT_MEMORY: return "Memoria de agentes"; case APP_LAYOUT: return "Maquetador"; case APP_AGENTS: return "Agentes";
    case APP_PAINT: return "Paint"; case APP_CODER_APP: return "Coder"; case APP_MEDIA: return "Media"; case APP_SETTINGS: return "Ajustes";
    case APP_PDF: return "Lector PDF"; case APP_SYSMON: return "Monitor del sistema"; case APP_RECORDER: return "Grabadora"; case APP_CAMERA: return "Camara"; case APP_CAPTURE: return "Captura"; case APP_SOMA_APP: return "Programa SOMA"; case APP_DESKTOP: return "Escritorio";
    default: return "Aplicacion";
    }
}
int gui_app_list(int *apps, int max) { int n = 0; for (int i = 0; i < her_num_windows && n < max; i++) apps[n++] = (int)her_windows[i].app; return n; }
int gui_close_app(int app)
{
    for (int i = 0; i < her_num_windows; i++) if ((int)her_windows[i].app == app) { her_close_target_window_vortex(i); return 1; }   /* el bucle principal resincroniza y repinta */
    int mi = her_find_minimized((enum app_mode)app);
    if (mi >= 0) { her_kill_minimized(mi); return 1; }
    return 0;
}
static void gui_open_sysmon__impl(void)
{
    if (gui_win_open(APP_SYSMON)) return;
    ensure_window_geom();
    g_pending_app = APP_SYSMON;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_sysmon(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_sysmon__impl();
    gui_leave(tok);
}

static void gui_open_recorder__impl(void)
{
    if (gui_win_open(APP_RECORDER)) return;
    ensure_window_geom();
    g_pending_app = APP_RECORDER;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_recorder(void)   /* SOMA_177_T480S: orden "grabadora" */
{
    int tok = gui_enter();
    gui_open_recorder__impl();
    gui_leave(tok);
}
static void gui_open_camera__impl(void)
{
    if (gui_win_open(APP_CAMERA)) return;
    ensure_window_geom();
    g_pending_app = APP_CAMERA;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
static void gui_open_capture__impl(void)
{
    if (gui_win_open(APP_CAPTURE)) return;
    ensure_window_geom();
    g_pending_app = APP_CAPTURE;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_capture(void)   /* SOMA_179_T480S: orden "captura abrir" */
{
    int tok = gui_enter();
    gui_open_capture__impl();
    gui_leave(tok);
}
void gui_open_camera(void)   /* SOMA_178_T480S: orden "camara" */
{
    int tok = gui_enter();
    gui_open_camera__impl();
    gui_leave(tok);
}

static void gui_open_layout__impl(void)
{
    if (gui_win_open(APP_LAYOUT)) return;
    ensure_window_geom();
    layout_init();
    g_pending_app = APP_LAYOUT;                         /* PARCHE 084: abre de verdad la ventana al arrancar la GUI */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_layout(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_layout__impl();
    gui_leave(tok);
}

static void gui_open_layout_file__impl(const char *path)
{
    if (gui_win_open(APP_LAYOUT)) { layout_open_file(path); gui_live_paint(); return; }
    ensure_window_geom();
    layout_init();
    layout_open_file(path);
    g_pending_app = APP_LAYOUT;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_layout_file(const char *path)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_layout_file__impl(path);
    gui_leave(tok);
}

void gui_open_coder_app(void);
void coder_app_load_project(const char *path);
/* SOMA_217_STUDIO: la orden 'coder' trae la ventana de Studio mas reciente (o la restaura); si no hay ninguna, abre una */
static int her_raise_app(enum app_mode app)
{
    for (int i = her_num_windows - 1; i >= 0; --i)
        if (her_windows[i].app == app) {
            her_bring_to_front(i); agent_global_focus = 0;
            her_win_x = her_windows[her_num_windows - 1].x; her_win_y = her_windows[her_num_windows - 1].y;
            her_win_w = her_windows[her_num_windows - 1].w; her_win_h = her_windows[her_num_windows - 1].h;
            return 1;
        }
    for (int i = her_min_count - 1; i >= 0; --i) if (her_minimized[i].app == app) { her_restore_minimized(i); return 1; }
    return 0;
}
void gui_open_coder_file(const char *path)   /* SOMA_217_STUDIO: el agente abre un archivo: su ventana o una nueva */
{
    int tok = gui_enter();
    if (fb_is_active()) { her_open_app_path(APP_CODER_APP, path, fb_get_width(), fb_get_height()); gui_live_paint(); }
    else { coder_app_load_project(path); gui_open_coder_app(); }
    gui_leave(tok);
}
static void gui_open_coder_app__impl(void)
{
    if (fb_is_active()) { if (!her_raise_app(APP_CODER_APP)) her_open_app(APP_CODER_APP, fb_get_width(), fb_get_height()); gui_live_paint(); return; }
    ensure_window_geom();
    current_app = APP_CODER_APP;
    agent_global_focus = 0;
    coder_app_init();
    g_pending_app = APP_CODER_APP;                      /* PARCHE 093: abre de verdad la ventana al arrancar la GUI */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_coder_app(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_coder_app__impl();
    gui_leave(tok);
}

/* SOMA_211_NOTAS: abre un archivo en el Bloc de notas. Si ya esta abierto en una ventana (aunque este minimizada) se trae esa;
 * si no, ventana nueva. Sin archivo: ventana nueva con un documento vacio. */
static void her_open_app_path(enum app_mode app, const char *path, uint32_t cw, uint32_t ch)
{
    int slot = (path && *path) ? her_inst_find(app, path) : 0;
    if (slot) {
        for (int i = 0; i < her_num_windows; ++i)
            if (her_windows[i].app == app && her_windows[i].term_slot == slot) {
                her_bring_to_front(i); agent_global_focus = 0;
                her_win_x = her_windows[her_num_windows - 1].x; her_win_y = her_windows[her_num_windows - 1].y;
                her_win_w = her_windows[her_num_windows - 1].w; her_win_h = her_windows[her_num_windows - 1].h;
                return;
            }
        for (int i = 0; i < her_min_count; ++i)
            if (her_minimized[i].app == app && her_minimized[i].term_slot == slot) { her_restore_minimized(i); return; }
    }
    int before = her_num_windows;
    her_open_noinit = (path && *path) ? 1 : 0;
    her_open_app(app, cw, ch);
    her_open_noinit = 0;
    if (her_num_windows > before && path && *path) {
        if (app == APP_NOTEPAD) notepad_load_file(path);
        else if (app == APP_IMAGE_VIEWER) imgview_load_file(path);
        else if (app == APP_CODER_APP) coder_app_open_path(path);
        else if (app == APP_PAINT) paint_open_file(path);
        else if (app == APP_FILE_MANAGER) fm_open_folder(path);
        else if (app == APP_PDF) pdfview_open_file(path);
    }
}
static void her_open_notepad_path(const char *path, uint32_t cw, uint32_t ch) { her_open_app_path(APP_NOTEPAD, path, cw, ch); }
static void gui_open_notepad__impl(const char *path)
{
    if (fb_is_active()) {
        her_open_notepad_path(path, fb_get_width(), fb_get_height());
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    current_app = APP_NOTEPAD;
    agent_global_focus = 0;
    notepad_init();
    if (path && *path) notepad_load_file(path);
    g_pending_app = APP_NOTEPAD;                        /* PARCHE 120 */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_notepad(const char *path)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_notepad__impl(path);
    gui_leave(tok);
}

/* SOMA_PARCHE_ARCHIVOS: abre el visor de imagenes con un archivo (lo usa el explorador de archivos) */
static void gui_open_imgview__impl(const char *path)
{
    if (fb_is_active()) {                                    /* SOMA_212_IMAGENES: la ventana de esa imagen, o una nueva */
        her_open_app_path(APP_IMAGE_VIEWER, path, fb_get_width(), fb_get_height());
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    current_app = APP_IMAGE_VIEWER;
    agent_global_focus = 0;
    imgview_init();
    if (path && *path) imgview_load_file(path);
    g_pending_app = APP_IMAGE_VIEWER;                   /* PARCHE 088: abria el escritorio sin la ventana */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_imgview(const char *path)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_imgview__impl(path);
    gui_leave(tok);
}

static void gui_open_paint__impl(const char *path)
{
    if (fb_is_active()) {                                    /* SOMA_215_PAINT: la ventana de ese dibujo, o una nueva */
        her_open_app_path(APP_PAINT, path, fb_get_width(), fb_get_height());
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    paint_init();
    if (path && *path) paint_open_file(path);
    g_pending_app = APP_PAINT;                          /* PARCHE 083: la orden 'paint' abre de verdad la ventana al arrancar la GUI */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_paint(const char *path)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_paint__impl(path);
    gui_leave(tok);
}

static void gui_open_terminal__impl(int debug)
{
    term_set_debug(debug);
    if (gui_win_open(APP_TERMINAL_SHELL)) return;
    ensure_window_geom();
    g_pending_app = APP_TERMINAL_SHELL;
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_terminal(int debug)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_terminal__impl(debug);
    gui_leave(tok);
}

static void gui_open_media__impl(const char *path)
{
    if (gui_win_open(APP_MEDIA)) {
        if (path && *path) media_play(path);
        return;
    }
    ensure_window_geom();
    g_pending_app = APP_MEDIA;
    if (path && *path) media_play(path);
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_media(const char *path)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_media__impl(path);
    gui_leave(tok);
}

static void gui_open_agents__impl(void)
{
    if (gui_win_open(APP_AGENTS)) return;
    ensure_window_geom();
    current_app = APP_AGENTS;
    agent_global_focus = 0;
    g_pending_app = APP_AGENTS;                         /* PARCHE 120 */
    cmd_vsoma("", 0, 0);
    g_pending_app = APP_DESKTOP;
}
void gui_open_agents(void)   /* PARCHE 091: el hilo del agente pide turno de pantalla */
{
    int tok = gui_enter();
    gui_open_agents__impl();
    gui_leave(tok);
}

/* El estado del raton es compartido con el hilo de fondo */
#define mouse_x g_mouse_x_s
#define mouse_y g_mouse_y_s

/* ===================== SOMA_168B_ORBE: el orbe al frente (tecla Windows, u Option sola en el Mac) =====================
 * En el escritorio vacio la tecla muestra u oculta las aplicaciones alrededor del orbe (lo que antes hacia la x).
 * Con aplicaciones abiertas el orbe sale del fondo y se pone DELANTE de todas, en el centro de la pantalla:
 *   apertura (~0,45 s): el fondo se oscurece con un velo azul noche, un campo de estrellas sale disparado desde el centro
 *   como un salto a la velocidad de la luz y se frena hasta quedar como estrellas que titilan, el orbe emerge de la
 *   oscuridad con ondas de choque y particulas, y las aplicaciones salen en orbita;
 *   cierre (~0,3 s, con Esc, otra vez la tecla, un clic fuera o al elegir una aplicacion): todo se recoge hacia el
 *   centro y el orbe vuelve detras de las ventanas.
 * Mientras esta delante se dibuja sobre una copia congelada de las ventanas (sin volver a pintar cada aplicacion). */
#define SUM_OPEN_MS  450u
#define SUM_CLOSE_MS 300u
#define SUM_STARS    140
static struct { uint16_t a, r; uint8_t b, tw; } sum_st[SUM_STARS];
static int sum_st_ready;
static uint32_t sum_ease(uint32_t t)                       /* 0..256 -> 0..256, 1 - (1 - t)^3 */
{
    if (t >= 256u) return 256u;
    uint32_t u = 256u - t;
    return 256u - (u * u / 256u) * u / 256u;
}
static void sum_stars_init(void)
{
    if (sum_st_ready) return;
    sum_st_ready = 1;
    for (int i = 0; i < SUM_STARS; i++) {
        sum_st[i].a = (uint16_t)(her_rand() & 1023u);
        uint32_t r = 90u + her_rand() % 935u; sum_st[i].r = (uint16_t)r;            /* fraccion del radio de la pantalla (/1024) */
        sum_st[i].b = (uint8_t)(90u + her_rand() % 166u);
        sum_st[i].tw = (uint8_t)(her_rand() & 255u);
    }
}
static void sum_draw(uint32_t cur_w, uint32_t cur_h, const uint32_t *snap, uint32_t k, int closing, uint64_t now)
{
    int32_t cx = (int32_t)(cur_w / 2u), cy = (int32_t)(cur_h / 2u);
    fb_reset_clip();
    if (snap) fb_blit(0, 0, snap, (int32_t)cur_w, (int32_t)cur_h, (int32_t)cur_w);
    fb_damage(0, 0, (int32_t)cur_w, (int32_t)cur_h);
    fb_fill_rect_alpha(0, 0, (int32_t)cur_w, (int32_t)cur_h, ((205u * k / 256u) << 24) | 0x00030714u);   /* velo azul noche */

    /* estrellas: salto (estelas largas que se frenan) al abrir; al cerrar, caen hacia el centro */
    volatile uint32_t *bb = fb_get_buffer();
    if (bb) {
        int32_t D = (int32_t)her_isqrt(cur_w * cur_w / 4u + cur_h * cur_h / 4u);
        for (int i = 0; i < SUM_STARS; i++) {
            int32_t base = (int32_t)sum_st[i].r * D / 1024;
            int32_t rr = base * (int32_t)(60u + 196u * k / 256u) / 256;
            int32_t L = (int32_t)((256u - k) * 90u / 256u) + 1;
            int32_t r0 = closing ? rr + L : rr - L;
            if (r0 < 0) r0 = 0;
            int32_t ux = her_cs(sum_st[i].a), uy = her_sn(sum_st[i].a);
            int32_t x1 = cx + rr * ux / 1024, y1 = cy + rr * uy / 1024, x0 = cx + r0 * ux / 1024, y0 = cy + r0 * uy / 1024;
            uint32_t tw = 190u + (uint32_t)((her_sn((uint32_t)((now / 3u + sum_st[i].tw * 4u) & 1023u)) + 1024) * 66 / 2048);
            uint32_t lum = (uint32_t)sum_st[i].b * tw / 256u * k / 256u;
            uint32_t col = (i % 7 == 0) ? 0x00C4B5FDu : (i % 5 == 0) ? 0x0067E8F9u : 0x00E2E8F0u;
            uint32_t head = ofx_mul(col, lum), tail = ofx_mul(col, lum / 6u);
            if (x1 < 1 || y1 < 1 || x1 >= (int32_t)cur_w - 1 || y1 >= (int32_t)cur_h - 1) continue;
            ofx_line(bb, (int32_t)cur_w, (int32_t)cur_h, x0, y0, x1, y1, tail, head);
            if (k > 200u && sum_st[i].b > 200u) { ofx_px(bb, (int32_t)cur_w, (int32_t)cur_h, x1 + 1, y1, ofx_mul(head, 90u)); ofx_px(bb, (int32_t)cur_w, (int32_t)cur_h, x1, y1 + 1, ofx_mul(head, 90u)); }
        }
    }

    /* el orbe completo, delante */
    ofx_orb_back((uint32_t)cx, (uint32_t)cy);
    her_parts_update_draw();
    her_draw_orb((uint32_t)cx, (uint32_t)cy, her_angle);
    ofx_orb_front((uint32_t)cx, (uint32_t)cy);
    if (k < 250u) {                                          /* emerge de la oscuridad (o se hunde en ella al cerrar): bruma de borde suave */
        int32_t R0 = ofx_radius((int32_t)cur_h);
        uint32_t a = (255u - k) * 7u / 20u;
        for (int j = 4; j >= 0; j--) {                       /* 5 discos concentricos: el centro queda mas oscuro que el borde */
            int32_t R = R0 * (8 + 2 * j) / 10;
            fb_fill_rrect_aa(cx - R, cy - R, 2 * R, 2 * R, R, (a << 24) | 0x00030714u);
        }
    }
    her_draw_dock(cur_w, cur_h);
    her_draw_minimized_icons(cur_w, cur_h);
    her_draw_vortex_particles();
    if (!closing && k > 128u) {
        const char *hint = "Elige una aplicacion  -  Esc o la tecla Windows para volver";
        uint32_t n = (uint32_t)font_text_width(hint, 1);           /* SOMA_226_SISTEMA: centrado con la fuente del escritorio */
        font_draw_string(cur_w / 2u - n / 2u, cur_h - 34u, hint, her_mix256(0x00030714u, 0x0094A3B8u, (k - 128u) * 2u), 1);
    }
    hst_dibujar(cur_w, cur_h);
}
/* Devuelve la aplicacion elegida (ya abierta o traida al frente) o -1 */
static int her_summon_run(uint32_t cur_w, uint32_t cur_h)
{
    int32_t cx = (int32_t)(cur_w / 2u), cy = (int32_t)(cur_h / 2u);
    sum_stars_init();
    her_summon_on = 1;
    her_refresh_all();                                       /* ventanas sin el orbe de fondo */
    fb_tr_off(); fb_cguard_keep(0);
    uint32_t *snap = fb_rect_save(0, 0, (int32_t)cur_w, (int32_t)cur_h);
    uint64_t now = timer_get_uptime_ms(), t0 = now, beat = now, tc = 0;
    her_text_suppressed_by_dock = 0;
    her_icons_visible = 1; her_icons_anim_phase = 0; her_icons_burst_opening = 1; her_icons_burst_at = now;
    ofx_trigger_icons(cx, cy);
    ofx_spawn_ring(20, 14, 26, 0x0067E8F9u);                 /* onda grande del salto */
    mouse_set_cursor(CURSOR_ARROW);
    int closing = 0, chosen = -1, chosen_min = -1;
    uint32_t k = 0, k_close = 256u;
    for (;;) {
        uint64_t f0 = timer_get_uptime_ms(); now = f0;
        int32_t dx = 0, dy = 0; int clk = 0, guard = 0;
        while (guard++ < 64 && mouse_poll(&dx, &dy, &clk)) {
            mouse_x += dx; mouse_y -= dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_x >= (int32_t)cur_w) mouse_x = (int32_t)cur_w - 1;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_y >= (int32_t)cur_h) mouse_y = (int32_t)cur_h - 1;
            if (her_min_drag >= 0) { her_min_drag_x = mouse_x; her_min_drag_y = mouse_y; }
            if (her_dp_kind && mouse_left_held()) her_dp_motion(mouse_x, mouse_y, cur_w, cur_h);
            if (clk && !closing) {
                int energy = her_power_hit(mouse_x, mouse_y, cur_w, cur_h);
                if (energy) her_power_press(energy);
                else {
                    int mi = her_min_hit(mouse_x, mouse_y, cur_w, cur_h);
                    if (mi >= 0) { her_min_drag = mi; her_min_start_x = mouse_x; her_min_start_y = mouse_y; }
                    else if (!her_dp_kind && her_dp_begin(mouse_x, mouse_y, cur_w, cur_h)) { /* se decide al soltar: toque = abrir, arrastre = recolocar */ }
                }
            }
            dx = dy = 0; clk = 0;
        }
        if (her_dp_kind && !mouse_left_held()) {
            int a = her_dp_end(cur_w, cur_h);
            if (a >= 0 && !closing) { chosen = a; closing = 1; }
        }
        if (her_min_drag >= 0 && !mouse_left_held()) {
            int mi = her_min_drag; her_min_drag = -1;
            if (her_min_drop_center(mouse_x, mouse_y, cur_w, cur_h)) her_kill_minimized_vortex(mi, mouse_x, mouse_y);
            else {
                int32_t dx = mouse_x - her_min_start_x, dy = mouse_y - her_min_start_y;
                if (dx * dx + dy * dy <= 1600) { chosen_min = mi; closing = 1; }
            }
        }
        while (kgetc_ready()) { char c = kgetc(); if (c == 27) closing = closing ? closing : 1; }
        if (console_take_super() && !closing) closing = 1;
        if ((g_gui_req || fb_user_exclusive()) && !closing) closing = 1;
        if (closing == 1) {                                  /* empieza el cierre */
            closing = 2; tc = now; k_close = k;
            her_icons_visible = 0;
            ofx_spawn_ring(160, -6, 20, 0x00A855F7u);        /* ondas que se contraen hacia el orbe */
            ofx_spawn_ring(110, -4, 22, 0x0038BDF8u);
            if (chosen >= 0) { ofx_flash += 120u; if (ofx_flash > 255u) ofx_flash = 255u; }
        }
        if (now - beat >= 33u) {                             /* mismo latido que el escritorio */
            beat = now;
            her_angle = (her_angle + 3) & 255;
            if (her_icons_visible) { her_icons_anim_phase += 36u; if (her_icons_anim_phase > HER_ICON_ANIM_MAX) her_icons_anim_phase = HER_ICON_ANIM_MAX; }
            else her_icons_anim_phase = her_icons_anim_phase > 36u ? her_icons_anim_phase - 36u : 0u;
            her_parts_spawn((uint32_t)cx, (uint32_t)cy, her_angle);
            her_vortex_close_step();
        }
        if (!closing) { uint64_t e = now - t0; k = e >= SUM_OPEN_MS ? 256u : sum_ease((uint32_t)(e * 256u / SUM_OPEN_MS)); }
        else {
            uint64_t e = now - tc;
            if (e >= SUM_CLOSE_MS || fb_user_exclusive()) break;
            k = k_close * (256u - sum_ease((uint32_t)(e * 256u / SUM_CLOSE_MS))) / 256u;
        }
        if (!snap) { her_refresh_all(); fb_tr_off(); fb_cguard_keep(0); }   /* sin memoria para la copia: se repinta todo */
        sum_draw(cur_w, cur_h, snap, k, closing != 0, now);
        mouse_draw_cursor(mouse_x, mouse_y);
        fb_present();
        uint64_t spent = timer_get_uptime_ms() - f0;
        if (spent < 16u) thread_sleep((uint32_t)(16u - spent));
    }
    if (snap) kfree(snap);
    her_summon_on = 0;
    her_icons_visible = 0; her_icons_anim_phase = 0;
    her_power_pending = 0;
    her_min_drag = -1; her_dp_kind = 0; her_idrag = -1; hst_gdrag = 0;
    if (chosen_min >= 0) her_restore_minimized(chosen_min);
    if (chosen >= 0) {
        her_open_app((enum app_mode)chosen, cur_w, cur_h);
        ofx_trigger_app(cx, cy, chosen);
    }
    return chosen;
}
/* SOMA_220_PRUEBA: 'escritorio prueba [n]' abre, pinta, minimiza y cierra n veces cada aplicacion y compara la memoria del
 * nucleo antes y despues. Se ejecuta en el hilo del escritorio (la orden solo deja la peticion y espera el resultado). */
static volatile int gui_stress_n, gui_stress_done;
static char *gui_stress_out; static uint32_t gui_stress_max;
static void her_stress_put(uint32_t *p, const char *t) { while (*t && *p + 1 < gui_stress_max) gui_stress_out[(*p)++] = *t++; gui_stress_out[*p] = 0; }
static void her_stress_num(uint32_t *p, int64_t v)
{
    char b[24]; int n = 0; int neg = v < 0; uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
    do { b[n++] = (char)('0' + u % 10u); u /= 10u; } while (u && n < 22);
    if (neg) b[n++] = '-';
    char o[24]; int m = 0; while (n) o[m++] = b[--n]; o[m] = 0; her_stress_put(p, o);
}
static int64_t her_stress_open_d;                               /* memoria con la ventana abierta respecto a antes de abrirla */
static int her_stress_cycle(enum app_mode app, uint32_t cw, uint32_t ch)
{
    int before = her_num_windows;
    size_t u0 = 0, f0 = 0, u1 = 0, f1 = 0; kheap_stats(&u0, &f0);
    her_open_app(app, cw, ch);
    if (her_num_windows != before + 1) return 0;
    int idx = her_num_windows - 1;
    her_refresh_all();                                          /* que la aplicacion se dibuje (algunas reservan al pintar) */
    kheap_stats(&u1, &f1); her_stress_open_d = (int64_t)u1 - (int64_t)u0;
    her_minimize_window(idx);
    for (int i = her_min_count - 1; i >= 0; --i)
        if (her_minimized[i].app == app) { her_kill_minimized(i); return 1; }
    return 0;
}
static void her_stress_run(int n, uint32_t cw, uint32_t ch)
{
    static const enum app_mode apps[] = { APP_NOTEPAD, APP_IMAGE_VIEWER, APP_PDF, APP_FILE_MANAGER, APP_PAINT, APP_CODER_APP,
                                          APP_TERMINAL_SHELL, APP_SYSMON, APP_SETTINGS, APP_WEB_BROWSER };
    uint32_t p = 0; int bad = 0;
    her_stress_put(&p, "Prueba de ventanas: "); her_stress_num(&p, n); her_stress_put(&p, " aperturas y cierres por aplicacion\n");
    for (unsigned a = 0; a < sizeof apps / sizeof apps[0]; ++a) {
        if (her_num_windows >= HER_MAX_WINDOWS - 1) { her_stress_put(&p, "  (no quedan ventanas libres: cierra alguna)\n"); break; }
        int pin = 0;                                            /* con una ventana ya abierta, las demas usan las instancias 2+ */
        if (her_multi_app(apps[a])) { int b = her_num_windows; her_open_app(apps[a], cw, ch); pin = her_num_windows == b + 1; }
        her_stress_cycle(apps[a], cw, ch);                      /* calentamiento: cajones que se reservan una sola vez */
        size_t u0 = 0, f0 = 0, u1 = 0, f1 = 0; kheap_stats(&u0, &f0);
        int ok = 0;
        for (int k = 0; k < n; ++k) ok += her_stress_cycle(apps[a], cw, ch);
        kheap_stats(&u1, &f1);
        if (pin) {
            int w = her_find_window(apps[a]);
            if (w >= 0) { her_minimize_window(w); for (int i = her_min_count - 1; i >= 0; --i) if (her_minimized[i].app == apps[a]) { her_kill_minimized(i); break; } }
        }
        int64_t d = (int64_t)u1 - (int64_t)u0;
        int leak = d > 4096;
        bad |= leak || ok != n;
        her_stress_put(&p, "  "); her_stress_put(&p, gui_app_title((int)apps[a]));
        her_stress_put(&p, ": "); her_stress_num(&p, ok); her_stress_put(&p, "/"); her_stress_num(&p, n);
        her_stress_put(&p, " ciclos | abierta +"); her_stress_num(&p, her_stress_open_d / 1024); her_stress_put(&p, " KiB | al final ");
        her_stress_put(&p, d >= 0 ? "+" : ""); her_stress_num(&p, d); her_stress_put(&p, " B");
        her_stress_put(&p, leak ? "  <- NO VUELVE\n" : ok != n ? "  <- NO SE ABRIO\n" : "  ok\n");
    }
    int hc = kheap_check();
    her_stress_put(&p, "  integridad del heap: "); her_stress_put(&p, hc == 0 ? "correcta\n" : "DANADA\n");
    her_stress_put(&p, (bad || hc) ? "RESULTADO: revisar las lineas marcadas\n" : "RESULTADO: correcto (la memoria vuelve a su valor)\n");
    her_lay_fresh_req = 1;
}
static int gui_desktop_live;
/* SOMA_225_AJUSTES: cambio de resolucion de trabajo pedido por Ajustes; se aplica al principio de una vuelta del bucle del escritorio */
static volatile int gui_res_req; static volatile uint32_t gui_res_w, gui_res_h;
void gui_request_resolution(uint32_t w, uint32_t h) { gui_res_w = w; gui_res_h = h; gui_res_req = 1; }
static void her_after_resize(uint32_t cw, uint32_t ch)
{
    for (int i = 0; i < her_num_windows; i++) {                  /* las ventanas siguen dentro de la pantalla */
        struct her_win_node *v = &her_windows[i];
        if (v->w > cw - 40u) v->w = cw * 3u / 4u;
        if (v->h > ch - 60u) v->h = ch * 3u / 4u;
        if (v->x + v->w > cw) v->x = cw - v->w;
        if (v->y + v->h > ch) v->y = ch - v->h;
    }
    if (g_mouse_x_s >= (int32_t)cw) g_mouse_x_s = (int32_t)cw - 1;
    if (g_mouse_y_s >= (int32_t)ch) g_mouse_y_s = (int32_t)ch - 1;
    her_lay_fresh_req = 1;
}
int gui_stress_request(int n, char *out, uint32_t max)
{
    if (!gui_desktop_live || !out || max < 64) return -1;
    if (n < 1) n = 1;
    if (n > 200) n = 200;
    gui_stress_out = out; gui_stress_max = max; out[0] = 0;
    gui_stress_done = 0; gui_stress_n = n;
    for (int t = 0; t < 1200 && !gui_stress_done; ++t) thread_sleep(100);
    if (!gui_stress_done) { gui_stress_n = 0; return -2; }
    return 0;
}

/* Escritorio vacio: mostrar u ocultar las aplicaciones alrededor del orbe (antes, la tecla x) */
static void her_toggle_icons(uint32_t cur_w, uint32_t cur_h)
{
    if (!her_icons_visible) her_power_pending = 0;
    if (her_icons_visible) { her_close_icons_vortex(cur_w, cur_h); return; }
    int has_text = (her_last_q[0] != '\0' || her_ans_len > 0 || her_thk_len > 0);
    if (has_text) { render_desktop(); her_start_text_destruction(cur_w, cur_h); return; }
    her_text_suppressed_by_dock = 0;
    her_icons_visible = 1;
    her_icons_anim_phase = 0;
    her_icons_burst_opening = 1;
    her_icons_burst_at = timer_get_uptime_ms();
}

/* SOMA_PARCHE_PORTAPAPELES: atajos globales del portapapeles. Devuelve 1 si la tecla se ha consumido. */
static void soma_clip_agent(int op)
{
    if (op == CLIP_OP_PASTE) {
        uint32_t n = 0; const char *t = clip_text(&n);
        if (!t || n == 0) { clip_toast("El portapapeles esta vacio"); return; }
        char one[2] = { 0, 0 }; uint32_t put = 0;
        for (uint32_t i = 0; i < n && put < 1500; i++) {
            char ch = t[i];
            if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
            if ((uint8_t)ch < 32) continue;
            one[0] = ch; agent_global_insert_str(one); put++;      /* los bytes UTF-8 pasan tal cual */
        }
        clip_toast("Pegado en la barra del agente");
        return;
    }
    if (agent_global_len == 0) { clip_toast("La barra del agente esta vacia"); return; }
    clip_set_text(agent_global_cmd, agent_global_len);
    if (op == CLIP_OP_CUT) { agent_global_len = 0; agent_global_cursor = 0; agent_global_cmd[0] = '\0'; clip_toast("Texto cortado"); }
    else clip_toast("Texto copiado");
}

static int soma_clip_apply(int op)
{
    if (agent_global_focus) { soma_clip_agent(op); return 1; }
    switch (current_app) {
    case APP_NOTEPAD:      return notepad_clip_op(op);
    case APP_IMAGE_VIEWER: return imgview_clip_op(op);
    case APP_PDF:          return pdfview_clip_op(op);   /* PARCHE 088 */
    case APP_CODER_APP:    if (coder_app_clip_op(op)) { her_refresh_all(); fb_present(); return 1; } return 0;   /* PARCHE 093 */
    case APP_PAINT:        return paint_clip_op(op);
    case APP_FILE_MANAGER: return fm_clip_op(op);
    case APP_WEB_BROWSER:
        if (op == CLIP_OP_PASTE) {                       /* equivale a escribir el texto en el campo activo */
            uint32_t n = 0; const char *t = clip_text(&n);
            if (!t || n == 0) { clip_toast("El portapapeles esta vacio"); return 1; }
            for (uint32_t i = 0; i < n && i < 2000; i++) {
                char ch = t[i];
                if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
                if ((uint8_t)ch < 32) continue;
                browser_handle_key(ch);
            }
            clip_toast("Pegado en el navegador");
        } else clip_toast("Copiar desde el navegador aun no esta disponible");
        return 1;
    case APP_TERMINAL_SHELL:
        if (op == CLIP_OP_PASTE && clip_overlay_active() == 0 && clip_kind() != CLIP_NONE) {
            struct term_edit ted = { cur_cmd, &cur_cmd_len, &cur_cursor_pos, sizeof(cur_cmd) };
            term_key((char)22, &ted);
            return 1;
        }
        return op == CLIP_OP_CUT ? 1 : 0;                /* Ctrl+C / Ctrl+V los resuelve el propio terminal */
    case APP_DESKTOP:
        clip_toast("Abre una aplicacion para usar el portapapeles");
        return 1;
    default:
        clip_toast("Esta aplicacion no usa el portapapeles");
        return 1;
    }
}

static int soma_clip_gui_key(char c)
{
    if (clip_overlay_active()) {
        if (clip_overlay_key(c)) {
            if (current_app == APP_TERMINAL_SHELL && !agent_global_focus) {
                struct term_edit ted = { cur_cmd, &cur_cmd_len, &cur_cursor_pos, sizeof(cur_cmd) };
                term_key((char)22, &ted);
            } else soma_clip_apply(CLIP_OP_PASTE);
        }
        return 1;
    }
    if (c == 25) { clip_overlay_open(); return 1; }       /* Ctrl+Y: historial */
    if (c == 3) return soma_clip_apply(CLIP_OP_COPY);
    if (c == 24) return soma_clip_apply(CLIP_OP_CUT);
    if (c == 22) return soma_clip_apply(CLIP_OP_PASTE);
    return 0;
}

static int g_gui_dead_accent = 0;
#if defined(__x86_64__)
/* SOMA_221_RESCATE: ya en el bucle del escritorio, con su pila normal: se cierra la ventana de la aplicacion rescatada. */
static void her_rc_khex(uint64_t v) { char b[19]; b[0] = '0'; b[1] = 'x'; for (int i = 0; i < 16; ++i) b[2 + i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 15]; b[18] = 0; kprint(b); }
static void her_after_rescue(void)
{
    fb_reset_clip();
    her_in_refresh = 0; her_dragging = 0; her_resizing = 0; her_min_drag = -1; her_dp_kind = 0; her_idrag = -1;
    acct_set_owner(ACCT_DESKTOP);
    her_rescues++;
    enum app_mode app = (enum app_mode)her_rescue_app;
    const char *name = gui_app_title((int)app);
    her_rescue_msg[0] = 0; her_rc_cat(name);
    if (her_rescue_kind == 2) her_rc_cat(" no respondia (10 s sin soltar el procesador) y se ha cerrado");
    else {
        her_rc_cat(" ha fallado (");
        her_rc_cat(her_rescue_vec == 14 ? "acceso a memoria no valido" : her_rescue_vec == 13 ? "fallo de proteccion" : her_rescue_vec == 6 ? "instruccion no valida" : her_rescue_vec == 0 ? "division por cero" : "pila desbordada");
        her_rc_cat(") y se ha cerrado");
    }
    kprint("\n[ESCRITORIO] "); kprint(her_rescue_msg); kprint(" | rip "); her_rc_khex(her_rescue_rip);
    if (her_rescue_vec == 14) { kprint(" direccion "); her_rc_khex(her_rescue_addr); }
    kprint(" | rescate "); kprint_dec(her_rescues); kprint(" de "); kprint_dec(HER_RESCUE_MAX); kprint("\n");
    int slot = 0, w = -1;
    for (int i = her_num_windows - 1; i >= 0; --i) if (her_windows[i].app == app) { w = i; break; }   /* la de arriba de esa aplicacion */
    if (w >= 0) {
        slot = her_windows[w].term_slot;
        her_minimize_window(w);                                    /* fuera de la pantalla sin llamar a la aplicacion */
        for (int i = her_min_count - 1; i >= 0; --i)
            if (her_minimized[i].app == app && her_minimized[i].term_slot == slot) {
                for (int j = i; j < her_min_count - 1; ++j) { her_minimized[j] = her_minimized[j + 1]; her_min_phase[j] = her_min_phase[j + 1]; }
                --her_min_count; break;
            }
    }
    (void)slot;
    her_hint_msg = her_rescue_msg; her_hint_until = timer_get_uptime_ms() + 7000u;
    her_rescuing = 0;
    her_lay_fresh_req = 1;
}
#endif

int cmd_vsoma(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    while (*args == ' ') args++;

    uint32_t target_w = FB_RES_FHD_W;
    uint32_t target_h = FB_RES_FHD_H;

    if (args[0] == '8' || (args[0] == 's' && args[1] == 'v')) {
        target_w = FB_RES_SVGA_W; target_h = FB_RES_SVGA_H;
    } else if (args[0] == '7' || (args[0] == '1' && args[1] == '2')) {
        target_w = FB_RES_HD_W; target_h = FB_RES_HD_H;
    }

    if (fb_set_mode(target_w, target_h) != 0) {
        kprint("[VSOMA ERROR] No se pudo activar modo grafico.\n");
        return 1;
    }

    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();

    /* uint32_t side_w = 0; */
    uint32_t win_x = HER_CLI_X(her_win_x);
    uint32_t win_y = HER_CLI_Y(her_win_y);
    uint32_t win_w = HER_CLI_W(her_win_w);
    uint32_t win_h = HER_CLI_H(her_win_h);

    uint32_t max_visible_terminal = (win_h - 60) / 15;

    g_live_win_x = win_x; g_live_win_y = win_y;
    g_live_win_w = win_w; g_live_win_h = win_h;
    g_live_ready = 1;
    vfs_live_hook = gui_live_file_hook;

    agent_log_count = 0;
    agent_global_scroll = 0;
    agent_global_focus = 1;
    log_add(agent_log, &agent_log_count, 0, "[SOMA AI AGENT]: Interfaz conversacional activa.", COLOR_CYAN, (win_w - 32)/8);

    term_init_banner(max_visible_terminal);
    browser_init();
    fm_init();
    imgview_init();
    notepad_init();
    memview_init();
    her_desk_load();

    current_app = APP_DESKTOP;
    her_icons_visible = 0;
    her_icons_anim_phase = 0;
    her_icons_burst_at = 0;
    her_icons_burst_opening = 1;
    her_has_user_text = 0;
    her_has_agent_text = 0;
    her_angle = 0;
    her_dragging = 0; her_resizing = 0;
    her_last_anim = timer_get_uptime_ms();
    {
        uint32_t _w = fb_get_width(), _h = fb_get_height();
        her_win_w = _w > 800 ? 720 : (_w > 400 ? _w - 80 : _w);
        her_win_h = _h > 600 ? 480 : (_h > 300 ? _h - 120 : _h);
        her_place_window_corner(_w, _h, 0);
    }
    HER_SYNC_WIN();
    agent_global_focus = 1;
    render_desktop();
    fb_present_full();
    settings_load();                                        /* PARCHE 085: transparencia y fuente guardadas */
    if (g_pending_app != APP_DESKTOP) {
        enum app_mode pa = g_pending_app; g_pending_app = APP_DESKTOP;
        her_open_app(pa, cur_w, cur_h);
        HER_SYNC_WIN();
        agent_global_focus = 0;
        her_refresh_all();
        fb_present_full();
    }

    mouse_x = (int32_t)cur_w / 2;
    mouse_y = (int32_t)cur_h / 2;
    mouse_init();
    (void)console_take_super();                             /* SOMA_168B_ORBE: pulsaciones de antes de la GUI no cuentan */
    mouse_erase_cursor();
    fb_present();

    cur_cmd_len = 0;
    cur_cursor_pos = 0;
    for (uint32_t i = 0; i < sizeof(cur_cmd); ++i) cur_cmd[i] = '\0';

    int cursor_visible = 1;
    uint64_t last_cursor_time = timer_get_uptime_ms();
    uint64_t last_clock_time = timer_get_uptime_ms();

    while (1) {
        acct_set_owner(ACCT_DESKTOP);   /* PARCHE 089: el bucle y la animacion son del escritorio; cada aplicacion se cuenta al dibujarse y al recibir eventos */
        g_gui_loop_on = 1;
        { struct tcb *gt = thread_current(); if (gt) g_gui_tid = gt->tid; }   /* PARCHE 098 */
#if defined(__x86_64__)
        if (__builtin_setjmp(her_jb)) {                         /* SOMA_221_RESCATE: vuelta desde una aplicacion colgada o averiada */
            her_after_rescue();
            HER_SYNC_WIN(); her_refresh_all(); fb_present();
        }
        her_jb_armed = 1;
#endif
        { extern void shell_lock_drop(void); shell_lock_drop(); }             /* PARCHE 100 */
        gui_soma_service(mouse_x, mouse_y);
        if (g_gui_req) {                /* PARCHE 091: el agente pide la pantalla; se la cedemos en este punto seguro */
            g_gui_grant = 1;
            while (g_gui_req && g_agent_thr_on) thread_sleep(1);
            g_gui_grant = 0;
            HER_SYNC_WIN(); her_refresh_all(); fb_present();
        }
        if (current_app == APP_SETTINGS && !her_dragging && !her_resizing && settings_tick()) {   /* SOMA_175C: Ajustes > Redes al dia */
            HER_SYNC_WIN(); settings_render_window(win_x, win_y, win_w, win_h, 1); fb_present();   /* SOMA_175C2: solo su ventana (antes */
        }                                                                                         /* se recomponia todo el escritorio) */
        if (tbg_state == 2) {           /* SOMA_175B3: termino una orden lenta del terminal */
            int old_slot = her_term_live;
            if (her_tbg_slot > 0 && her_tbg_slot < 16 && her_term_sessions[her_tbg_slot].used)
                (void)term_ui_select(her_tbg_slot);
            else (void)term_ui_select(0);
            if (!tbg_show()) term_print("(la orden no mostro nada)");
            (void)term_ui_select(old_slot > 0 ? old_slot : 0);
            her_tbg_slot = 0;
            tbg_state = 0;
            HER_SYNC_WIN(); her_refresh_all(); fb_present();
        }
        if (g_agent_thr_done) {         /* PARCHE 091: la mision termino */
            agent_turn_finish();
            HER_SYNC_WIN(); her_refresh_all(); fb_present();
        }
        /* PARCHE 089: el bucle de la GUI ya no gira sin parar. Si no hay tecla pendiente ni fotograma de la animacion a punto, cede
         * la CPU 1 ms (el hilo inactivo detiene el procesador): el uso de CPU que muestra el Monitor es real y QEMU deja de consumir
         * un nucleo entero del anfitrion. El raton se sigue leyendo cada milisegundo. */
        /* SOMA_152_WEB_FLUIDO: con el navegador delante el latido del fondo es mas lento (o se congela); la espera de 1 ms
         * debe seguir haciendose hasta ese plazo, si no el bucle giraria en vacio entre fotogramas. */
        int her152_fast = her152_eligible();
        uint32_t her152_period = 33u;
        if (her152_fast) her152_period = browser_hot() ? 0x7FFFFFFFu : (uint32_t)SOMA_GUI_BG_MS;
        /* SOMA_155D: con una aplicacion abierta (que no sea el navegador, que ya tiene su ruta propia) cada latido recompone la
         * pantalla entera. En el T480s eso a 30 fps fijos se llevaba toda la CPU y el raton iba a tirones. El latido se espacia
         * segun lo que cuesta (como mucho ~1/4 de la CPU, entre 30 y 4 fps) y se aplaza mientras se mueve el raton o se teclea. */
        static uint64_t h155_cost, h155_input, h207_ccost = 12u;
        static uint32_t h155_kev;
        static int32_t h155_mx = -1, h155_my = -1;
        int h155_apps = (!her152_fast && her_num_windows > 0);
        {
            uint64_t t = timer_get_uptime_ms();
            uint32_t kev = console_kbd_events();
            if (kev != h155_kev || mouse_x != h155_mx || mouse_y != h155_my || mouse_left_held()) {
                h155_input = t; h155_kev = kev; h155_mx = mouse_x; h155_my = mouse_y; her_bc_input_ms = t;
            }
            if (h155_apps) {
                /* SOMA_164B_T480S: con varios nucleos el fondo se dibuja repartido y cuesta mucho menos: presupuesto de 1/3 de la
                 * CPU (antes 1/4) y, mientras se interactua, ~15 fps en vez de pararse (antes esperaba 150 ms). */
                int multi = smp_cpus() > 1;
                uint64_t p = h155_cost * (multi ? 2u : 4u);              /* SOMA_164C_T480S: 1/2 del nucleo 0 con varios nucleos (antes 1/3) */
                if (p < 33u) p = 33u;
                if (p > 250u) p = 250u;
                /* SOMA_216_RENDIMIENTO: antes, al mover el raton o escribir, el fondo bajaba a ~15 fps y el orbe se renovaba a 10 fps
                 * (parche 207): el orbe iba a tirones con ventanas abiertas. Ahora el ritmo solo depende de lo que cuesta un fotograma
                 * y el orbe se renueva en cada latido; la capa del 207 queda para los repintados entre latidos (teclas, clics). */
                (void)h207_ccost; (void)h155_input;
                /* SOMA_164D_T480S: arrastrando o cambiando de tamano una ventana, el fotograma ES la respuesta: tan rapido como
                 * cueste (16..33 ms, hasta 60 fps), no al ritmo del fondo. Antes iba a ~15 fps y el arrastre daba saltos. */
                if (her_dragging || her_resizing) { p = h155_cost + 2u; if (p < 16u) p = 16u; if (p > 33u) p = 33u; }
                her152_period = (uint32_t)p;
            }
        }
        if (!kgetc_ready() && timer_get_uptime_ms() - her_last_anim < her152_period - 3u && !her_dragging && !her_resizing && !mouse_left_held()) thread_sleep(1);
        uint64_t now = timer_get_uptime_ms();

        /* SOMA_PARCHE_021_FPU: un proceso de usuario tiene la pantalla; la GUI espera sin dibujar */
        if (fb_user_exclusive()) {
            thread_sleep(8);
            her_last_anim = now; last_clock_time = now;
            continue;
        }

        gui_desktop_live = 1;
        if (gui_res_req) {                                      /* SOMA_225_AJUSTES */
            gui_res_req = 0;
            if (fb_set_logical(gui_res_w, gui_res_h) == 0) {
                cur_w = fb_get_width(); cur_h = fb_get_height();
                her_after_resize(cur_w, cur_h);
            }
            HER_SYNC_WIN(); her_refresh_all(); fb_present();
            continue;
        }
        if (gui_stress_n) {                                     /* SOMA_220_PRUEBA */
            her_stress_run(gui_stress_n, cur_w, cur_h);
            gui_stress_n = 0; gui_stress_done = 1;
            HER_SYNC_WIN(); her_refresh_all(); fb_present();
            continue;
        }
        if (console_take_super()) {                             /* SOMA_168B_ORBE: tecla Windows / Option */
            if (her_num_windows > 0) {
                int app = her_summon_run(cur_w, cur_h);
                if (app >= 0) agent_global_focus = (app == APP_AGENT_CHAT) ? 1 : 0;
                HER_SYNC_WIN();
                her_refresh_all();
            } else {
                her_toggle_icons(cur_w, cur_h);
                render_desktop();
            }
            if (her_cursor_should_show()) mouse_draw_cursor(mouse_x, mouse_y); else mouse_erase_cursor();
            fb_present();
            her_last_anim = timer_get_uptime_ms();
            continue;
        }

        /* SOMA_152_WEB_FLUIDO: fotograma solo de la ventana del navegador (sin recomponer el escritorio) */
        int her152_full = 0;
        if (her152_fast) {
            static uint64_t her152_scene, her152_last_web;
            uint64_t sk = her152_scene_key();
            if (sk != her152_scene) { her152_scene = sk; her152_full = 1; }
            else if (now - her152_last_web >= (browser_loading() ? 100u : (uint32_t)SOMA_WEB_FRAME_MS) && now - her_last_anim >= SOMA_WEB_FRAME_MS &&
                     browser_frame_dirty(win_w, win_h)) {   /* SOMA_190: cargando sin que el usuario toque, 10 fps como mucho */
                her152_last_web = now;
                uint64_t wf0 = timer_get_uptime_ms(); (void)wf0;
                acct_set_owner((uint8_t)(ACCT_APP0 + APP_WEB_BROWSER));
                fb_set_clip((int32_t)win_x, (int32_t)win_y, (int32_t)win_w, (int32_t)win_h);
                browser_render_window(win_x, win_y, win_w, win_h, 1);
                fb_reset_clip();
                acct_set_owner(ACCT_DESKTOP);
                if (her_cursor_should_show()) mouse_draw_cursor(mouse_x, mouse_y); else mouse_erase_cursor();
                fb_present();
                PF152(1, (uint32_t)(timer_get_uptime_ms() - wf0));
                {   /* SOMA_190: reparto de CPU. Un fotograma caro (maquina lenta, pagina grande) no puede dejar sin CPU al hilo que carga la
                     * pagina: tras dibujar, el escritorio duerme tanto como tardo (el triple mientras la pagina carga sola). */
                    uint64_t fc = timer_get_uptime_ms() - wf0;
                    uint64_t d = fc * (browser_loading() ? 3u : 1u);
                    if (d > 150u) d = 150u;
                    if (d >= 2u) thread_sleep((uint32_t)d); else thread_yield();
                }
            }
        }
        /* --- LATIDO PERPETUO: El agujero negro palpita a 30 FPS en todo momento --- */
        if (her152_full || now - her_last_anim >= her152_period) {
            her_last_anim = now;
            her_angle = (her_angle + 3) & 255;
            /* Transicion suave en pasos pequenos a 30 FPS (~0,5 s). */
            if (her_icons_visible) {
                if (her_icons_anim_phase < HER_ICON_ANIM_MAX) {
                    her_icons_anim_phase += 36u; /* Velocidad 2x */
                    if (her_icons_anim_phase > HER_ICON_ANIM_MAX)
                        her_icons_anim_phase = HER_ICON_ANIM_MAX;
                }
            } else if (her_icons_anim_phase > 0u) {
                her_icons_anim_phase = (her_icons_anim_phase > 36u)
                                     ? her_icons_anim_phase - 36u : 0u;
            }
            {
                uint32_t ocx = cur_w / 2, ocy = cur_h / 2;
                her_reveal_step();
                her_parts_spawn(ocx, ocy, her_angle);
            }
            her_vortex_close_step();
            uint64_t pf0 = timer_get_uptime_ms(); (void)pf0;
#if SOMA_WEB_PERF_LOG
            uint64_t c153 = her153_cycles();
#endif
            /* SOMA_207: mientras se interactua el orbe se renueva a ~15 fps; el resto de repintados reutiliza la capa guardada */
            her_lay_fresh_req = 1;                               /* SOMA_216: el orbe se renueva en cada latido */
            uint32_t h207_hits0 = her_lay_hits;
            her_eco_period = her152_period;
            her_refresh_all();
            her_lay_fresh_req = 0;
            if (her_cursor_should_show())
                mouse_draw_cursor(mouse_x, mouse_y);
            else
                mouse_erase_cursor();
            int ver_now = her_eco_verify && ((++her_eco_vn & 7u) == 0);
            if (ver_now) her_eco_stat[5] += fb_debug_verify(0x00000000);
            { uint64_t tp = tmp_tsc(); fb_present(); tmp_pf[4] += tmp_tsc() - tp; }
            if (ver_now) { her_eco_stat[6] += fb_debug_vram_diff(); her_eco_stat[7]++; }
            her_bc_baseline();
            if (tmp_pfn >= 60) {                                         /* SOMA_164B: medias de 60 fotogramas para 'orbe' */
                for (int q = 0; q < 8; q++) { orb_perf_last[q] = tmp_pf[q] / tmp_pfn; orb_perf_last[8 + q] = tmp_rd[q] / tmp_pfn; tmp_pf[q] = 0; tmp_rd[q] = 0; }
                orb_perf_period = her152_period; orb_perf_windows = (uint32_t)her_num_windows; tmp_pfn = 0;
            }
            PF152(0, (uint32_t)(timer_get_uptime_ms() - pf0));
            if (h155_apps) {                                            /* SOMA_155D: coste del latido (media movil) */
                uint64_t c = timer_get_uptime_ms() - pf0;
                if (her_lay_hits != h207_hits0) h207_ccost = h207_ccost ? (h207_ccost * 3u + c) / 4u : c;
                else h155_cost = h155_cost ? (h155_cost * 3u + c) / 4u : c;
            }
            {   /* SOMA_190: el bucle siempre cede la CPU tras un fotograma completo (antes, con fotogramas mas caros que su plazo,
                 * encadenaba uno tras otro y el vigilante saltaba: 'Ningun hilo cedio la CPU') */
                uint64_t fc = timer_get_uptime_ms() - pf0;
                if (fc >= 8u && !her_dragging && !her_resizing) thread_sleep((uint32_t)(fc > 100u ? 100u : fc)); else thread_yield();
            }
#if SOMA_WEB_PERF_LOG
            if (her_num_windows > 0 && !her152_full) her153_note(her153_cycles() - c153);   /* fotogramas del fondo con ventana */
#endif
        }

        if (now - last_clock_time >= 1000) {
            last_clock_time = now;
            struct rtc_time t; rtc_get_datetime(&t);
            char cstr[16]; uint32_t cp = 0;
            cstr[cp++] = (char)('0' + (t.hour / 10)); cstr[cp++] = (char)('0' + (t.hour % 10)); cstr[cp++] = ':';
            cstr[cp++] = (char)('0' + (t.min / 10)); cstr[cp++] = (char)('0' + (t.min % 10)); cstr[cp++] = ':';
            cstr[cp++] = (char)('0' + (t.sec / 10)); cstr[cp++] = (char)('0' + (t.sec % 10)); cstr[cp] = '\0';
            uint32_t system_y = cur_h > SOMA_SYSTEM_BAR_H ? cur_h - SOMA_SYSTEM_BAR_H : 0;
            fb_fill_rect(140, system_y + 10, 120, 20, 0x000B0F19);
            font_draw_string(140, system_y + 14, cstr, 0x00F8FAFC, 1);
#ifdef __x86_64__
            {   /* SOMA_165_T480S: bateria junto al reloj (copia que renueva el hilo del ACPI; aqui no se espera al EC) */
                struct acpi_power_state ps; acpi_power_state(&ps);
                if (ps.valid && ps.percent != 0xFFFFFFFF) {
                    char bs[40]; uint32_t bp = 0; const char *pre = "Bateria ";
                    while (*pre) bs[bp++] = *pre++;
                    if (ps.percent >= 100) bs[bp++] = '1';
                    if (ps.percent >= 10) bs[bp++] = (char)('0' + (ps.percent / 10) % 10);
                    bs[bp++] = (char)('0' + ps.percent % 10); bs[bp++] = '%';
                    const char *suf = ps.charging ? " cargando" : (ps.ac == 1 ? " enchufado" : "");
                    while (*suf) bs[bp++] = *suf++;
                    bs[bp] = 0;
                    uint32_t col = (ps.discharging && ps.percent <= 10) ? 0x00F87171 : (ps.charging || ps.ac == 1) ? 0x0086EFAC : 0x00CBD5E1;
                    fb_fill_rect(270, system_y + 10, 240, 20, 0x000B0F19);
                    font_draw_string(270, system_y + 14, bs, col, 1);
                }
            }
#endif
        }
        if (!her_dragging && !her_resizing) {   /* SOMA_179_T480S: Captura, Camara y Grabadora se repintan solas (hora, nivel, imagen) aunque no se mueva el raton:
                                                 * sus tick() estaban dentro de mouse_poll y no corrian sin actividad del puntero */
            static const int live_apps[3] = { APP_CAPTURE, APP_CAMERA, APP_RECORDER };
            for (int k = 0; k < 3; k++) {
                int app = live_apps[k], si = -1;
                for (int i = 0; i < her_num_windows; i++) if ((int)her_windows[i].app == app) si = i;
                if (si < 0) continue;
                uint8_t own = acct_set_owner((uint8_t)(ACCT_APP0 + app));
                int ch = app == APP_CAPTURE ? capapp_tick() : app == APP_CAMERA ? camapp_tick() : recorder_tick();
                if (ch) {
                    struct her_win_node *sw = &her_windows[si];
                    int covered = 0;
                    for (int j = si + 1; j < her_num_windows; j++) {
                        struct her_win_node *o = &her_windows[j];
                        if (o->x < sw->x + sw->w && sw->x < o->x + o->w && o->y < sw->y + sw->h && sw->y < o->y + o->h) { covered = 1; break; }
                    }
                    int opaque = settings_opacity() >= 100;
                    if (!covered && opaque) her_render_app_body(app, HER_CLI_X(sw->x), HER_CLI_Y(sw->y), HER_CLI_W(sw->w), HER_CLI_H(sw->h), si == her_num_windows - 1);
                    else her_refresh_all();
                    if (her_cursor_should_show()) mouse_draw_cursor(mouse_x, mouse_y);
                    fb_present();
                }
                acct_set_owner(own);
            }
        }
        if (!her_dragging && !her_resizing) {   /* PARCHE 089/090: el Monitor se actualiza cada segundo aunque no sea la ventana activa */
            int si = -1;
            for (int i = 0; i < her_num_windows; i++) if (her_windows[i].app == APP_SYSMON) si = i;
            if (si >= 0) {
                uint8_t own = acct_set_owner((uint8_t)(ACCT_APP0 + APP_SYSMON));
                if (sysmon_tick()) {
                    struct her_win_node *sw = &her_windows[si];
                    int covered = 0;   /* alguna ventana de encima lo tapa: hay que recomponer todo para no pintar sobre ella */
                    for (int j = si + 1; j < her_num_windows; j++) {
                        struct her_win_node *o = &her_windows[j];
                        if (o->x < sw->x + sw->w && sw->x < o->x + o->w && o->y < sw->y + sw->h && sw->y < o->y + o->h) { covered = 1; break; }
                    }
                    int opaque = settings_opacity() >= 100;
                    if (!covered && opaque) her_render_app_body(APP_SYSMON, HER_CLI_X(sw->x), HER_CLI_Y(sw->y), HER_CLI_W(sw->w), HER_CLI_H(sw->h), si == her_num_windows - 1);
                    else her_refresh_all();
                    if (her_cursor_should_show()) mouse_draw_cursor(mouse_x, mouse_y);
                    fb_present();
                }
                acct_set_owner(own);
            }
        }

        if (now - last_cursor_time >= 400) {
            cursor_visible = !cursor_visible;
            last_cursor_time = now;
        }

        int32_t m_dx = 0, m_dy = 0;
        int m_left_click = 0;

        if (her_mouse_poll_merged(&m_dx, &m_dy, &m_left_click)) {
            if (!mouse_left_held()) {
                if (her_dp_kind) {                                  /* SOMA_208: soltar un icono o el gadget */
                    int dhit = her_dp_end(cur_w, cur_h);
                    if (dhit >= 0) { her_open_app((enum app_mode)dhit, cur_w, cur_h); HER_SYNC_WIN(); }
                    her_refresh_all();
                    if (her_cursor_should_show()) mouse_draw_cursor(mouse_x, mouse_y); else mouse_erase_cursor();
                    fb_present();
                    continue;
                }
                if (her_min_drag >= 0) {
                    int mi = her_min_drag;
                    her_min_drag = -1;
                    if (her_min_drop_center(mouse_x, mouse_y, cur_w, cur_h))
                        her_kill_minimized_vortex(mi, mouse_x, mouse_y);
                    else {
                        int32_t dx = mouse_x - her_min_start_x;
                        int32_t dy = mouse_y - her_min_start_y;
                        if (dx * dx + dy * dy <= 1600) her_restore_minimized(mi);
                    }
                    HER_SYNC_WIN(); her_refresh_all(); fb_present();
                    continue;
                }
                if (her_dragging || her_resizing) {
                    her_dragging = 0;
                    her_resizing = 0;
                    mouse_set_cursor(CURSOR_ARROW);
                }
                if (current_app == APP_PAINT) {
                    paint_handle_mouse(mouse_x, mouse_y, 0, win_x, win_y, win_w, win_h);
                }
                if (current_app == APP_TERMINAL_SHELL) term_mouse_up(mouse_x, mouse_y);
            }

            if (m_dx != 0 || m_dy != 0) {
                mouse_x += m_dx; mouse_y -= m_dy;
                if (her_min_drag >= 0) { her_min_drag_x = mouse_x; her_min_drag_y = mouse_y; }
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_x >= (int32_t)cur_w) mouse_x = (int32_t)cur_w - 1;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_y >= (int32_t)cur_h) mouse_y = (int32_t)cur_h - 1;
                if (her_dp_kind && mouse_left_held() && her_dp_motion(mouse_x, mouse_y, cur_w, cur_h)) {
                    her_refresh_all();
                    if (her_cursor_should_show()) mouse_draw_cursor(mouse_x, mouse_y); else mouse_erase_cursor();
                    fb_present();
                    continue;
                }

                if (her_dragging || her_resizing) {
                    her_win_apply_motion(mouse_x, mouse_y, cur_w, cur_h);
                    HER_SYNC_WIN();
                    if (her_cursor_should_show())
                        mouse_draw_cursor(mouse_x, mouse_y);
                    fb_present();
                } else if (!mouse_left_held() && her_num_windows > 0) {
                    /* Hover de bordes: cambiar cursor a CROSSHAIR al posarse en un marco */
                    int over_edge = 0;
                    for (int wi = her_num_windows - 1; wi >= 0; --wi) {
                        int edge = 0;
                        int h = her_win_hit_target(&her_windows[wi], mouse_x, mouse_y, &edge);
                        if (h == 2) { over_edge = edge; break; }
                        else if (h > 0) { break; }
                    }
                    int rc = her_resize_cursor(over_edge);           /* PARCHE 085: flecha doble segun el borde */
                    if (rc >= 0) {
                        if (mouse_get_current_cursor() != rc) mouse_set_cursor(rc);
                    } else if (her_is_resize_cursor(mouse_get_current_cursor())) {
                        mouse_set_cursor(CURSOR_ARROW);
                    }
                }

                if (current_app == APP_TERMINAL_SHELL && !her_dragging && !her_resizing)
                    term_mouse_move(mouse_x, mouse_y, mouse_left_held());

                if (current_app == APP_PAINT && !her_dragging && !her_resizing && mouse_left_held()) {
                    paint_handle_mouse(mouse_x, mouse_y, 1, win_x, win_y, win_w, win_h);
                    paint_render_window(win_x, win_y, win_w, win_h, 1);
                }

                if (her_cursor_should_show())
                    mouse_draw_cursor(mouse_x, mouse_y);
                else
                    mouse_erase_cursor();
            }

            acct_set_owner((uint8_t)(ACCT_APP0 + current_app));   /* PARCHE 089 */
            {   /* SOMA_PARCHE_024_TERM: rueda y boton derecho */
                int wheel = mouse_wheel_take();
                int rclick = mouse_right_take_click();
                if (current_app == APP_WEB_BROWSER) browser_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held()); /* SOMA_PARCHE_NAVEGADOR */
                if (current_app == APP_FILE_MANAGER && !her_dragging && !her_resizing) {   /* SOMA_PARCHE_028_UI */
                    if (fm_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held(), rclick))
                        fm_render_window(win_x, win_y, win_w, win_h, 1);
                }
                if (current_app == APP_CODER_APP && !her_dragging && !her_resizing) {   /* PARCHE 093: arrastre de seleccion y rueda */
                    if (coder_app_mouse(mouse_x, mouse_y, wheel, mouse_left_held())) { coder_app_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_SYSMON && !her_dragging && !her_resizing) {   /* PARCHE 089: muestra por segundo y rueda */
                    int rd = sysmon_tick();
                    if (sysmon_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel)) rd = 1;
                    if (rd) { sysmon_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_CAPTURE && !her_dragging && !her_resizing) {    /* SOMA_179_T480S: tiempo y nivel en vivo */
                    if (capapp_tick()) { capapp_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_CAMERA && !her_dragging && !her_resizing) {     /* SOMA_178_T480S: imagen en vivo */
                    if (camapp_tick()) { camapp_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_RECORDER && !her_dragging && !her_resizing) {   /* SOMA_177_T480S: nivel y tiempo en vivo */
                    if (recorder_tick()) { recorder_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_PDF && !her_dragging && !her_resizing) {      /* PARCHE 088: rueda y arrastre del lector PDF */
                    if (pdfview_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held())) { pdfview_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_IMAGE_VIEWER && !her_dragging && !her_resizing) {   /* PARCHE 088: rueda y arrastre del visor */
                    if (imgview_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held())) { imgview_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_SETTINGS && !her_dragging && !her_resizing) {  /* PARCHE 085: deslizador de opacidad */
                    if (settings_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, mouse_left_held())) { her_refresh_all(); fb_present(); }
                    if (wheel && settings_wheel(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel)) { her_refresh_all(); fb_present(); }   /* SOMA_227_IMAGEN */
                }
                if (current_app == APP_LAYOUT && !her_dragging && !her_resizing) {   /* PARCHE 084: rueda y barra del maquetador */
                    if (layout_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held()))
                        layout_render_window(win_x, win_y, win_w, win_h, 1);
                }
                if (current_app == APP_NOTEPAD && !her_dragging && !her_resizing) {   /* SOMA_PARCHE_028_UI: arrastre, rueda y menu contextual del Notepad con la posicion real del puntero */
                    if (notepad_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held(), rclick))
                        notepad_render_window(win_x, win_y, win_w, win_h, 1);
                }
                if (current_app == APP_TERMINAL_SHELL && point_in_rect(mouse_x, mouse_y, win_x, win_y, win_w, win_h)) {
                    int redraw = 0;
                    if (wheel && term_wheel(wheel)) redraw = 1;
                    if (rclick && term_mouse_right(mouse_x, mouse_y, win_x, win_y, win_w, win_h)) redraw = 1;
                    if (redraw) render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                }
                if (current_app == APP_WEB_BROWSER && rclick && point_in_rect(mouse_x, mouse_y, win_x, win_y, win_w, win_h)) {   /* PARCHE 085 */
                    if (browser_right_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h)) her_refresh_all();
                }
            }

            if (m_left_click) {
                int win_clicked = -1;
                int win_hit_code = 0;
                int win_edge_code = 0;

                /* Buscar de arriba a abajo en orden Z */
                for (int wi = her_num_windows - 1; wi >= 0; --wi) {
                    int edge = 0;
                    int h = her_win_hit_target(&her_windows[wi], mouse_x, mouse_y, &edge);
                    if (h > 0) {
                        win_clicked = wi;
                        win_hit_code = h;
                        win_edge_code = edge;
                        break;
                    }
                }

                if (win_clicked < 0 && her_min_drag < 0) {
                    int mi = her_min_hit(mouse_x, mouse_y, cur_w, cur_h);
                    if (mi >= 0) {
                        her_min_drag = mi;
                        her_min_drag_x = her_min_start_x = mouse_x;
                        her_min_drag_y = her_min_start_y = mouse_y;
                        if (!mouse_left_held()) {                       /* toque tan rapido que ya esta soltado: restaurar */
                            her_min_drag = -1; her_restore_minimized(mi);
                            HER_SYNC_WIN(); her_refresh_all(); fb_present();
                        }
                        continue;
                    }
                }
                if (win_clicked >= 0) {
                    if (win_clicked != her_num_windows - 1) {
                        her_bring_to_front(win_clicked);
                        win_clicked = her_num_windows - 1;
                    }

                    struct her_win_node *top_win = &her_windows[win_clicked];
                    her_win_x = top_win->x; her_win_y = top_win->y;
                    her_win_w = top_win->w; her_win_h = top_win->h;
                    HER_SYNC_WIN();

                    if (win_hit_code == 3) {
                        her_minimize_window_fx(win_clicked);              /* SOMA_223_MINIMIZAR */
                        her_refresh_all();
                        fb_present();
                        continue;
                    }
                    if (win_hit_code == 1) {
                        her_dragging = 1; her_resizing = 0;
                        mouse_set_cursor(CURSOR_MOVE);                 /* PARCHE 085 */
                        her_drag_ox = mouse_x - (int32_t)top_win->x;
                        her_drag_oy = mouse_y - (int32_t)top_win->y;
                        her_refresh_all();
                        fb_present();
                        continue;
                    }
                    if (win_hit_code == 2) {
                        her_resizing = 1; her_dragging = 0;
                        her_resize_edge = win_edge_code;
                        { int rc = her_resize_cursor(win_edge_code); if (rc >= 0) mouse_set_cursor(rc); }
                        her_resize_start_mx = mouse_x; her_resize_start_my = mouse_y;
                        her_resize_start_x = (int32_t)top_win->x; her_resize_start_y = (int32_t)top_win->y;
                        her_resize_start_w = (int32_t)top_win->w; her_resize_start_h = (int32_t)top_win->h;
                        her_refresh_all();
                        fb_present();
                        continue;
                    }
                }

                int her_inside = (win_clicked >= 0);

                if (!her_inside) {
                    int energy = her_power_hit(mouse_x, mouse_y, cur_w, cur_h);
                    if (energy) {
                        her_power_press(energy);
                        her_refresh_all();
                        fb_present();
                        continue;
                    }
                    if (her_dp_kind) { her_refresh_all(); fb_present(); continue; }          /* ya hay un arrastre: el nivel del boton no lo reinicia */
                    if (her_dp_begin(mouse_x, mouse_y, cur_w, cur_h)) {
                        int dhit = -1;
                        if (!mouse_left_held()) dhit = her_dp_end(cur_w, cur_h);     /* toque tan rapido que ya esta soltado */
                        if (dhit >= 0) { her_open_app((enum app_mode)dhit, cur_w, cur_h); HER_SYNC_WIN(); }
                        her_refresh_all();
                        if (her_cursor_should_show())
                            mouse_draw_cursor(mouse_x, mouse_y);
                        else
                            mouse_erase_cursor();
                        fb_present();
                        continue;
                    }
                    if (her_icons_visible && current_app == APP_DESKTOP) {
                        /* Clic en el agujero negro o fondo mientras orbitan: absorber iconos */
                        her_close_icons_vortex(cur_w, cur_h);
                        her_refresh_all();
                        fb_present();
                        continue;
                    }
                    agent_global_focus = 1;
                    her_refresh_all();
                    fb_present();
                    continue;
                }

                if (current_app != APP_DESKTOP) {
                    agent_global_focus = (current_app == APP_AGENT_CHAT) ? 1 : 0;
                    if (current_app == APP_RECORDER) {               /* SOMA_177_T480S */
                        recorder_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        recorder_render_window(win_x, win_y, win_w, win_h, 1);
                    } else
                    if (current_app == APP_CAPTURE) {                /* SOMA_179_T480S */
                        capapp_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        capapp_render_window(win_x, win_y, win_w, win_h, 1);
                    } else
                    if (current_app == APP_CAMERA) {                 /* SOMA_178_T480S */
                        camapp_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        camapp_render_window(win_x, win_y, win_w, win_h, 1);
                    } else
                    if (current_app == APP_SYSMON) {                 /* PARCHE 089 */
                        if (sysmon_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h) == 2) { HER_SYNC_WIN(); her_refresh_all(); }
                        else sysmon_render_window(win_x, win_y, win_w, win_h, 1);
                    } else
                    if (current_app == APP_PDF) {                    /* PARCHE 088 */
                        pdfview_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        pdfview_render_window(win_x, win_y, win_w, win_h, 1);
                    } else
                    if (current_app == APP_SETTINGS) {               /* PARCHE 085 */
                        if (settings_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h)) her_refresh_all();
                        else settings_render_window(win_x, win_y, win_w, win_h, 1);
                    } else if (current_app == APP_WEB_BROWSER) {
                        int act = browser_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (act == 1) { her_close_current_app_vortex(); continue; }
                        browser_render_window(win_x, win_y, win_w, win_h, 1);
                    } else if (current_app == APP_IMAGE_VIEWER) {
                        int act = imgview_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (act == 1) { her_close_current_app_vortex(); continue; }
                        imgview_render_window(win_x, win_y, win_w, win_h, 1);
                    } else if (current_app == APP_CODER_APP) {
                        int act = coder_app_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (act == 1) { her_close_current_app_vortex(); continue; }
                        coder_app_render_window(win_x, win_y, win_w, win_h, 1);
                    } else if (current_app == APP_PAINT) {
                        int act = paint_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (act == 1) { her_close_current_app_vortex(); continue; }
                                            paint_render_window(win_x, win_y, win_w, win_h, 1);
                } else if (current_app == APP_MEDIA) {
                    media_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                    media_render_window(win_x, win_y, win_w, win_h, 1);
                } else if (current_app == APP_SOMA_APP) {
                    gui_soma_win_record_click(); /*PL12_SOMA_CLICK*/
                } else if (current_app == APP_LAYOUT) {
                        int act = layout_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (act == 1) { her_close_current_app_vortex(); continue; }
                        else { layout_render_window(win_x, win_y, win_w, win_h, 1); }
                    } else if (current_app == APP_AGENT_MEMORY) {
                        int act = memview_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (act == 1) { her_close_current_app_vortex(); continue; }
                        else if (act == 4) {
                            const char *sel_file = memview_get_selected_path();
                            if (sel_file && *sel_file) {
                                her_open_notepad_path(sel_file, cur_w, cur_h);   /* SOMA_211_NOTAS */
                                HER_SYNC_WIN(); her_refresh_all();
                            }
                        } else {
                            memview_render_window(win_x, win_y, win_w, win_h, 1);
                        }
                    } else if (current_app == APP_NOTEPAD) {
                        notepad_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        notepad_render_window(win_x, win_y, win_w, win_h, 1);
                    } else if (current_app == APP_FILE_MANAGER) {
                        char target_file[160]; target_file[0] = '\0';
                        int action = fm_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h, target_file, sizeof(target_file));
                        if (action == 5) { /* SOMA_PARCHE_ARCHIVOS: el explorador ha abierto otra ventana */
                            HER_SYNC_WIN(); her_refresh_all();
                        } else if (action == 2) {
                            her_open_app_path(APP_IMAGE_VIEWER, target_file, cur_w, cur_h); HER_SYNC_WIN(); her_refresh_all();   /* SOMA_212_IMAGENES */
                        } else if (action == 3) {
                            current_app = APP_WEB_BROWSER; render_desktop(); browser_load_url(target_file); browser_render_window(win_x, win_y, win_w, win_h, 1);
                        } else if (action == 4) {
                            her_open_notepad_path(target_file, cur_w, cur_h); HER_SYNC_WIN(); her_refresh_all();   /* SOMA_211_NOTAS */
                        } else {
                            fm_render_window(win_x, win_y, win_w, win_h, 1);
                        }
                    } else if (current_app == APP_TERMINAL_SHELL) {
                        struct term_edit ted = { cur_cmd, &cur_cmd_len, &cur_cursor_pos, sizeof(cur_cmd) };
                        term_mouse_down(mouse_x, mouse_y, win_x, win_y, win_w, win_h, &ted);
                        render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    } else if (current_app == APP_AGENT_CHAT) {
                        if (cur_cmd_len > 0 && point_in_rect(mouse_x, mouse_y, win_x + win_w - 110, win_y + win_h - 46, 96, 46)) goto exec_agent;
                    }
                }
            }
            fb_present();
        }

        if (kgetc_ready()) {
            char c = kgetc();
            if (soma_clip_gui_key(c)) continue;   /* SOMA_PARCHE_PORTAPAPELES */
            acct_set_owner((uint8_t)(ACCT_APP0 + current_app));   /* PARCHE 089 */
            if (c == 27) {
                if (current_app == APP_TERMINAL_SHELL && term_popup_open()) {      /* SOMA_PARCHE_024_TERM: Esc cierra el menu */
                    term_popup_close();
                    render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present(); continue;
                }
                if (current_app == APP_FILE_MANAGER && fm_escape()) {              /* SOMA_PARCHE_028_UI */
                    fm_render_window(win_x, win_y, win_w, win_h, 1);
                    fb_present(); continue;
                }
                if (current_app == APP_CODER_APP) {                                 /* PARCHE 093: Esc cierra barras/seleccion del editor, no sale del escritorio */
                    if (coder_app_escape()) { coder_app_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
                    continue;
                }
                if (current_app == APP_NOTEPAD && notepad_escape()) {              /* SOMA_PARCHE_028_UI: Esc cierra el menu o el dialogo del Notepad */
                    notepad_render_window(win_x, win_y, win_w, win_h, 1);
                    fb_present(); continue;
                }
                break;
            }

            if (current_app == APP_DESKTOP) agent_global_focus = 1;
            /* SOMA_168B_ORBE: la x ya no llama al orbe (ahora es la tecla Windows, u Option sola en el Mac): se escribe */

            if (agent_global_focus) {
                if (c == '\t') {
                    agent_global_focus = 0;
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                if (c == 16) {
                    uint32_t visible = SOMA_AGENT_RESP_H / SOMA_AGENT_LINE_H;
                    if (visible == 0) visible = 1;
                    if ((uint32_t)agent_log_count > visible) {
                        uint32_t max_scroll = (uint32_t)agent_log_count - visible;
                        if (agent_global_scroll < max_scroll) agent_global_scroll++;
                    }
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                if (c == 14) {
                    if (agent_global_scroll > 0) agent_global_scroll--;
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                if (c == 2) { /* Flecha izquierda con soporte UTF-8 */
                    if (agent_global_cursor > 0) {
                        agent_global_cursor--;
                        while (agent_global_cursor > 0 && ((uint8_t)agent_global_cmd[agent_global_cursor] & 0xC0) == 0x80)
                            agent_global_cursor--;
                    }
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                if (c == 6) { /* Flecha derecha con soporte UTF-8 */
                    if (agent_global_cursor < agent_global_len) {
                        agent_global_cursor++;
                        while (agent_global_cursor < agent_global_len && ((uint8_t)agent_global_cmd[agent_global_cursor] & 0xC0) == 0x80)
                            agent_global_cursor++;
                    }
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                if (c == '\b' || c == 127) {
                if (current_app == APP_TERMINAL_SHELL) term_note_input();
                    g_gui_dead_accent = 0;
                    if (agent_global_cursor > 0) {
                        uint32_t del = 1;
                        if (((uint8_t)agent_global_cmd[agent_global_cursor - 1] & 0xC0) == 0x80) {
                            while (agent_global_cursor > del && ((uint8_t)agent_global_cmd[agent_global_cursor - del] & 0xC0) == 0x80)
                                del++;
                        }
                        for (uint32_t i = agent_global_cursor - del; i + del <= agent_global_len; ++i)
                            agent_global_cmd[i] = agent_global_cmd[i + del];
                        agent_global_len -= del;
                        agent_global_cursor -= del;
                        agent_global_cmd[agent_global_len] = '\0';
                    }
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                if (c == '\r' || c == '\n') {
                    submit_global_agent_prompt(win_x, win_y, win_w, win_h - 56);
                    continue;
                }
                /* Tecla de acento como dead-key en GUI */
                if (c == 0x27 || (uint8_t)c == 0xB4 || c == 0x60) {
                    g_gui_dead_accent = 1;
                    continue;
                }
                if (c == 0x22) {
                    g_gui_dead_accent = 2;
                    continue;
                }

                if ((uint8_t)c >= 32 && (uint8_t)c != 127) {
                    if (g_gui_dead_accent != 0) {
                        int dk = g_gui_dead_accent;
                        g_gui_dead_accent = 0;
                        if (dk == 1) {
                            switch (c) {
                            case 'a': agent_global_insert_str("\xC3\xA1"); break;
                            case 'e': agent_global_insert_str("\xC3\xA9"); break;
                            case 'i': agent_global_insert_str("\xC3\xAD"); break;
                            case 'o': agent_global_insert_str("\xC3\xB3"); break;
                            case 'u': agent_global_insert_str("\xC3\xBA"); break;
                            case 'A': agent_global_insert_str("\xC3\x81"); break;
                            case 'E': agent_global_insert_str("\xC3\x89"); break;
                            case 'I': agent_global_insert_str("\xC3\x8D"); break;
                            case 'O': agent_global_insert_str("\xC3\x93"); break;
                            case 'U': agent_global_insert_str("\xC3\x9A"); break;
                            case ' ': agent_global_insert_str("'"); break;
                            default: {
                                char s_tmp[3] = { (char)0x27, c, 0 };
                                agent_global_insert_str(s_tmp);
                                break;
                            }
                            }
                        } else if (dk == 2) {
                            switch (c) {
                            case 'u': agent_global_insert_str("\xC3\xBC"); break;
                            case 'U': agent_global_insert_str("\xC3\x9C"); break;
                            case ' ': agent_global_insert_str("\""); break;
                            default: {
                                char s_tmp[3] = { (char)0x22, c, 0 };
                                agent_global_insert_str(s_tmp);
                                break;
                            }
                            }
                        }
                    } else {
                        char one[2] = { c, 0 };
                        agent_global_insert_str(one);
                    }
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                continue;
            }

                        if (current_app == APP_DESKTOP) continue;
            if (current_app == APP_SOMA_APP) { /*PL12_SOMA_KEY*/
                if (c == 27) {
                    her_close_current_app_vortex();
                } else {
                    gui_soma_win_push_key((int)(unsigned char)c);
                }
                continue;
            }
            if (current_app == APP_CODER_APP) {
                coder_app_handle_key(c);
                coder_app_render_window(win_x, win_y, win_w, win_h, 1);
                fb_present();
                continue;
            }
            if (current_app == APP_AGENTS) {
                agents_render_window(win_x, win_y, win_w, win_h, 1);
                fb_present();
                continue;
            }

            if (current_app == APP_CAPTURE) {                    /* SOMA_179_T480S */
                if (c == 27) { her_close_current_app_vortex(); continue; }
                capapp_handle_key(c); capapp_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_CAMERA) {                     /* SOMA_178_T480S */
                if (c == 27) { her_close_current_app_vortex(); continue; }
                camapp_handle_key(c); camapp_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_RECORDER) {                   /* SOMA_177_T480S */
                if (c == 27) { micrec_stop(); her_close_current_app_vortex(); continue; }
                recorder_handle_key(c); recorder_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_SYSMON) {                     /* PARCHE 089 */
                if (c == 27) { her_close_current_app_vortex(); continue; }
                sysmon_handle_key(c); sysmon_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_PDF) {                        /* PARCHE 088 */
                if (c == 27) { her_close_current_app_vortex(); continue; }
                pdfview_handle_key(c); pdfview_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_SETTINGS) {                   /* PARCHE 085 */
                if (c == 27) { her_close_current_app_vortex(); continue; }
                if (settings_handle_key(c)) her_refresh_all(); else settings_render_window(win_x, win_y, win_w, win_h, 1);
                fb_present(); continue;
            }
            if (current_app == APP_WEB_BROWSER) {
                browser_handle_key(c); browser_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_IMAGE_VIEWER) {
                imgview_handle_key(c); imgview_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_FILE_MANAGER) {
                fm_handle_key(c); fm_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_NOTEPAD) {
                notepad_handle_key(c); notepad_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_AGENT_MEMORY) {
                memview_handle_key(c); memview_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_MEDIA) {
                media_handle_key(c); media_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_PAINT) {
                if (c == 27) { her_close_current_app_vortex(); continue; }
                paint_handle_key(c); paint_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_LAYOUT) {
                if (c == 27) {
                    her_close_current_app_vortex();
                    continue;
                }
                layout_handle_key(c); layout_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }

            if (current_app == APP_TERMINAL_SHELL) {
                struct term_edit ted = { cur_cmd, &cur_cmd_len, &cur_cursor_pos, sizeof(cur_cmd) };
                if (term_key(c, &ted)) {
                    render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present(); continue;
                }
            }

            if (c == 2) {
                if (cur_cursor_pos > 0) {
                    cur_cursor_pos--;
                    if (current_app == APP_TERMINAL_SHELL) render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    else if (current_app == APP_AGENT_CHAT) render_input_bar_chat("soma> ", COLOR_CYAN, win_x, win_y + win_h - 46, win_w, 46, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
                continue;
            }
            if (c == 6) {
                if (cur_cursor_pos < cur_cmd_len) {
                    cur_cursor_pos++;
                    if (current_app == APP_TERMINAL_SHELL) render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    else if (current_app == APP_AGENT_CHAT) render_input_bar_chat("soma> ", COLOR_CYAN, win_x, win_y + win_h - 46, win_w, 46, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
                continue;
            }
            if (c == 16 && current_app == APP_TERMINAL_SHELL) {
                term_note_input();
                if (term_hist_count > 0) {
                    if (term_hist_idx == -1) term_hist_idx = term_hist_count - 1;
                    else if (term_hist_idx > 0) term_hist_idx--;
                    cur_cmd_len = 0;
                    while (term_hist[term_hist_idx][cur_cmd_len] && cur_cmd_len < sizeof(cur_cmd)-1) {
                        cur_cmd[cur_cmd_len] = term_hist[term_hist_idx][cur_cmd_len];
                        cur_cmd_len++;
                    }
                    cur_cmd[cur_cmd_len] = '\0';
                    cur_cursor_pos = cur_cmd_len;
                    render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
                continue;
            }
            if (c == 14 && current_app == APP_TERMINAL_SHELL) {
                term_note_input();
                if (term_hist_idx != -1) {
                    if (term_hist_idx < term_hist_count - 1) {
                        term_hist_idx++;
                        cur_cmd_len = 0;
                        while (term_hist[term_hist_idx][cur_cmd_len] && cur_cmd_len < sizeof(cur_cmd)-1) {
                            cur_cmd[cur_cmd_len] = term_hist[term_hist_idx][cur_cmd_len];
                            cur_cmd_len++;
                        }
                        cur_cmd[cur_cmd_len] = '\0';
                        cur_cursor_pos = cur_cmd_len;
                    } else {
                        term_hist_idx = -1;
                        cur_cmd_len = 0; cur_cmd[0] = '\0'; cur_cursor_pos = 0;
                    }
                    render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
                continue;
            }
            if (c == '\t' && current_app == APP_TERMINAL_SHELL) {
                term_note_input();
                const char *matches[16];
                int n_matches = shell_autocomplete(cur_cmd, cur_cursor_pos, matches, 16);
                if (n_matches == 1) {
                    const char *m = matches[0];
                    cur_cmd_len = 0;
                    while (m[cur_cmd_len] && cur_cmd_len < sizeof(cur_cmd)-2) { cur_cmd[cur_cmd_len] = m[cur_cmd_len]; cur_cmd_len++; }
                    cur_cmd[cur_cmd_len++] = ' '; cur_cmd[cur_cmd_len] = '\0';
                    cur_cursor_pos = cur_cmd_len;
                    render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
                continue;
            }

            if (c == '\b' || c == 127) {
                if (cur_cursor_pos > 0) {
                    for (uint32_t i = cur_cursor_pos - 1; i < cur_cmd_len; ++i) cur_cmd[i] = cur_cmd[i + 1];
                    cur_cmd_len--; cur_cursor_pos--; cur_cmd[cur_cmd_len] = '\0';
                    if (current_app == APP_TERMINAL_SHELL) render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    else render_input_bar_chat("soma> ", COLOR_CYAN, win_x, win_y + win_h - 46, win_w, 46, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
                continue;
            }

            if (c == '\r' || c == '\n') {
                if (current_app == APP_TERMINAL_SHELL) {
                    term_note_input();
                    if (cur_cmd_len == 0) {
                        term_add_line("\033[1;36msoma\033[1;32m>\033[0m", max_visible_terminal);
                        render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                        fb_present(); continue;
                    }
                    char echo_line[220]; uint32_t ep = 0;
                    const char *prm = "\033[1;36msoma\033[1;32m>\033[0m ";
                    while (*prm) echo_line[ep++] = *prm++;
                    for (uint32_t i = 0; i < cur_cmd_len && ep < sizeof(echo_line) - 1; ++i) echo_line[ep++] = cur_cmd[i];
                    echo_line[ep] = '\0'; term_add_line(echo_line, max_visible_terminal);

                    if (term_hist_count < 16) {
                        uint32_t hp = 0; while (cur_cmd[hp]) { term_hist[term_hist_count][hp] = cur_cmd[hp]; hp++; }
                        term_hist[term_hist_count][hp] = '\0';
                        term_hist_count++;
                    } else {
                        for (int h = 0; h < 15; ++h) {
                            for (int k = 0; k < 256; ++k) term_hist[h][k] = term_hist[h + 1][k];
                        }
                        uint32_t hp = 0; while (cur_cmd[hp]) { term_hist[15][hp] = cur_cmd[hp]; hp++; }
                        term_hist[15][hp] = '\0';
                    }
                    term_hist_idx = -1;

                    char cmd_run[256]; for (uint32_t i = 0; i <= cur_cmd_len; ++i) cmd_run[i] = cur_cmd[i];
                    cur_cmd_len = 0; cur_cursor_pos = 0; cur_cmd[0] = '\0';

                    if (cmd_run[0] == 'c' && cmd_run[1] == 'l' && cmd_run[2] == 'e') term_clear();
                    else if (cmd_run[0] == 's' && cmd_run[1] == 'o' && cmd_run[2] == 'm' && cmd_run[3] == 'a' && cmd_run[4] == ' ' && cmd_run[5]) {
                        /* PARCHE 091: "soma <mision>" tambien va al hilo del agente; la terminal no se bloquea */
                        const char *ms = cmd_run + 5; uint32_t k = 0;
                        while (ms[k] && k < sizeof(agent_global_cmd) - 1) { agent_global_cmd[k] = ms[k]; k++; }
                        agent_global_cmd[k] = '\0'; agent_global_len = k; agent_global_cursor = k;
                        int was_busy = g_agent_thr_on;
                        submit_global_agent_prompt(win_x, win_y, win_w, win_h);
                        term_add_line(was_busy ? "SOMA sigue con la mision anterior: espera a que termine." : "Mision enviada a SOMA: trabaja en segundo plano y la respuesta sale en su ventana. Puedes seguir usando el sistema.", max_visible_terminal);
                    }
                    else if (tbg_state) {
                        term_add_line("Espera: la orden anterior (wifi) sigue en marcha; su resultado saldra aqui.", max_visible_terminal);
                    }
                    else if (tbg_is_slow(cmd_run) && tbg_start(cmd_run) == 0) {
                        her_tbg_slot = her_term_live;
                        term_add_line("En marcha en segundo plano: puedes seguir usando el sistema; el resultado saldra aqui.", max_visible_terminal);
                    }
                    else {
                        static char cmd_out[4096]; cmd_out[0] = '\0';
                        /* SOMA_156B_T480S: muchas ordenes (hw por secciones, dmesg, teclado, pantalla, apic...) imprimen con
                         * kprint y dejan out_buf vacio: en el terminal no se veia nada (en el portatil no hay puerto serie
                         * donde mirarlo). Se captura lo que imprime la orden; si deja texto en out_buf, manda ese (como antes). */
                        static char *cap_txt; enum { CAP_MAX = 64 * 1024 };
                        if (!cap_txt) cap_txt = (char *)kmalloc(CAP_MAX);
                        int capturing = cap_txt && !console_capture_active();
                        if (capturing) console_capture_begin(cap_txt, CAP_MAX, "");
                        int ret = dispatch_command(cmd_run, cmd_out, sizeof(cmd_out));
                        if (capturing) {
                            uint32_t cn = console_capture_end();
                            if (cmd_out[0] == '\0' && cn) {                 /* quita los colores ANSI (ESC [ ... letra) */
                                uint32_t w = 0;
                                for (uint32_t r = 0; r < cn; r++) {
                                    if (cap_txt[r] == 27 && r + 1 < cn && cap_txt[r + 1] == '[') {
                                        r += 2; while (r < cn && !((cap_txt[r] >= 'A' && cap_txt[r] <= 'Z') || (cap_txt[r] >= 'a' && cap_txt[r] <= 'z'))) r++;
                                        continue;
                                    }
                                    cap_txt[w++] = cap_txt[r];
                                }
                                cap_txt[w] = 0;
                            } else cap_txt[0] = 0;
                        }
                        const char *show = cmd_out[0] ? cmd_out : (capturing ? cap_txt : cmd_out);
                        if (show[0] != '\0') {
                            const char *scan = show;
                            while (*scan) {
                                char line_single[220]; uint32_t lp = 0;
                                while (*scan && *scan != '\n' && lp < sizeof(line_single) - 1) { if (*scan != '\r') line_single[lp++] = *scan; scan++; }
                                line_single[lp] = '\0'; if (*scan == '\n') scan++;
                                if (lp > 0) term_add_line(line_single, max_visible_terminal);
                            }
                        } else if (!ret) term_add_line("Comando no reconocido.", max_visible_terminal);
                    }
                    render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1); fb_present(); continue;

                } else if (current_app == APP_AGENT_CHAT) {
                    if (cur_cmd_len == 0) continue;
                exec_agent:
                    agent_global_len = cur_cmd_len < sizeof(agent_global_cmd) - 1
                                     ? cur_cmd_len : sizeof(agent_global_cmd) - 1;
                    for (uint32_t i = 0; i < agent_global_len; ++i) agent_global_cmd[i] = cur_cmd[i];
                    agent_global_cmd[agent_global_len] = '\0';
                    agent_global_cursor = agent_global_len;
                    agent_global_focus = 1;
                    submit_global_agent_prompt(win_x, win_y, win_w, win_h - 56);
                    cur_cmd_len = 0; cur_cursor_pos = 0; cur_cmd[0] = '\0';
                    continue;
                }
            }

            if ((uint8_t)c >= 32 && (uint8_t)c != 127) {
                if (current_app == APP_TERMINAL_SHELL) term_note_input();
                if (cur_cmd_len < sizeof(cur_cmd) - 2) {
                    for (uint32_t i = cur_cmd_len; i > cur_cursor_pos; --i) cur_cmd[i] = cur_cmd[i - 1];
                    cur_cmd[cur_cursor_pos++] = c; cur_cmd_len++; cur_cmd[cur_cmd_len] = '\0';
                    if (current_app == APP_TERMINAL_SHELL) render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    else render_input_bar_chat("soma> ", COLOR_CYAN, win_x, win_y + win_h - 46, win_w, 46, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
            }
        }
        /* SOMA_PARCHE_007_IDLE: la animacion ya esta limitada por tiempo (33 ms) y el reloj
         * a 1 s; entre medias solo se sondean teclado y raton. Dormir 1 ms deja correr a idle. */
        thread_sleep(1);
    }
    g_live_ready = 0; vfs_live_hook = 0;
    fb_cguard_keep(0);              /* SOMA_PARCHE_028_UI: al salir de la GUI ya no hay esquinas que reponer */
    mouse_erase_cursor(); mouse_shutdown(); fb_restore_text_mode(); keyboard_reset_state(); return 1;
}
