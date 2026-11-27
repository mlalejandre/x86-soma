// SOMA_234_GUI_PARTIDO: marcas de parches anteriores cuyo codigo esta ahora en los archivos por temas. Los scripts de parche
// buscan su marca en este archivo para saber si ya estan aplicados; se conservan aqui para que la sigan encontrando:
//   /*PL12_CHROME_TITLE*/ | /*PL12_NOTIFY_CLOSE*/ | /*PL12_OFX_COL*/ | /*PL12_RENDER_BODY*/ | /*PL12_SOMA_WIN_IMPL*/
//   /*PL12_VORTEX_COL*/ | PARCHE 030 | PARCHE 086 | PARCHE 097 | PARCHE 120 | SOMA ASYNC MATERIALIZE PATCH v4
//   SOMA BLACK HOLE REALISM PATCH v1 | SOMA ORBITAL CALM PATCH v3 | SOMA TEXT ABSORPTION PATCH v5 | SOMA_153B_CINTAS
//   SOMA_153_ORBE_OCULTO | SOMA_154 | SOMA_157_T480S | SOMA_158_T480S | SOMA_162_T480S | SOMA_175C5 | SOMA_176A
//   SOMA_177B | SOMA_184 | SOMA_202_QEMU_LIGERO_V2 | SOMA_207_CAPA | SOMA_208_ESCRITORIO | SOMA_209 | SOMA_209_ECO
//   SOMA_213_PDF | SOMA_214 | SOMA_214_ARCHIVOS | SOMA_215_PAINT | SOMA_217_STUDIO | SOMA_218_CUADRICULA
//   SOMA_226_SISTEMA | SOMA_APP_SURF_H | SOMA_APP_SURF_W | SOMA_LAYER_CACHE | SOMA_ORB_FX_PATCH | SOMA_ORB_OCCLUSION
//   SOMA_ORB_RIB_HALF | SOMA_PARCHE_012_RENDER | SOMA_VECTOR_CELL_PATCH | SOMA_WEB_FLUIDO
/* SOMA_PARCHE_V2_GUI */
/* SOMA FIX CMD_VSOMA DUP v3 */
/* SOMA GLOBAL AGENT SHELL PATCH v2 */
#include "gui.h"
#include "fb.h"
#include "net.h"
#include "netif.h" /* SOMA_201_GADGET_ESTADO */
#include "sysdisk.h"
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
#include "chat.h"                 /* SOMA_285_CHAT */
#include "layout.h"
#include "paint.h"
#include "codeedit.h"                               /* SOMA_219_AVISO */
#include "media.h"
#include "youtube.h"                                /* SOMA_250_YOUTUBE */
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
#include "discos_gui.h"                            /* SOMA_331_DISCOS */
#include "noticias_gui.h"                          /* SOMA_337_NOTICIAS */
#include "noticias_rss.h"
#include "agentes_gui.h"                           /* SOMA_338_AGENTES */
#include "agentes_reg.h"
#include "hora.h"                                  /* SOMA_332_HORA */
#include "agenda_gui.h"                            /* SOMA_272_AGENDA */
#include "contactos_gui.h"                         /* SOMA_357_CONTACTOS */
#include "hoja_gui.h"                              /* SOMA_359_HOJA_VENTANA */
#include "hoja.h"
#include "contactos.h"
#include "correo.h"                                /* SOMA_295_CORREO */
#include "recorder.h"                               /* SOMA_177_T480S */
#include "camapp.h"                                /* SOMA_178_T480S */
#include "capapp.h"                                /* SOMA_179_T480S */
#include "scrcap.h"
#include "camera.h"
#include "micrec.h"
#if defined(__x86_64__)
#include "voz_sys.h"      /* SOMA_244_VOZ */
#include "usermode.h"
#endif
#include "sysicon.h"                                /* SOMA_218_ENERGIA */
#include "soma_str.h"
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
    APP_CAPTURE,   /* SOMA_179_T480S: captura de pantalla y audio interno */
    APP_YOUTUBE,   /* SOMA_250_YOUTUBE: videos de YouTube (busqueda y reproduccion en directo) */
    APP_AGENDA,    /* SOMA_272_AGENDA: calendario */
    APP_CORREO,    /* SOMA_295_CORREO: correo electronico (IMAP/SMTP) con IA */
    APP_DISCOS,    /* SOMA_331_DISCOS: discos, particiones, formato e imagenes */
    APP_NOTICIAS,  /* SOMA_337_NOTICIAS: noticias por categorias, con resumen de la IA */
    APP_CONTACTOS, /* SOMA_357_CONTACTOS: libreta de contactos (fotos y voz) */
    APP_HOJA       /* SOMA_359_HOJA_VENTANA: hoja de calculo */
};

static enum app_mode current_app = APP_DESKTOP;
/* SOMA_359_HOJA_VENTANA: aplicaciones que atienden Esc con su propia tecla (cerrar un dialogo, cancelar una edicion y, sin nada abierto,
 * cerrar la ventana). Antes Esc pasaba por el bloque general y sacaba del escritorio al modo texto. */
static int her_esc_propio(enum app_mode a)
{
    return a == APP_AGENTS || a == APP_CAPTURE || a == APP_CAMERA || a == APP_RECORDER || a == APP_AGENDA || a == APP_SYSMON ||
           a == APP_DISCOS || a == APP_CONTACTOS || a == APP_NOTICIAS || a == APP_HOJA;
}
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
    case APP_AGENT_MEMORY:   return 860u;   /* SOMA_283_MEMORIA_APP: consola de la memoria (3 columnas) */
    case APP_AGENT_CHAT:     return 560u;   /* SOMA_285_CHAT */
    case APP_FILE_MANAGER:   return 480u;
    case APP_IMAGE_VIEWER:   return 460u;
    case APP_NOTEPAD:        return 400u;
    case APP_LAYOUT:         return 900u;   /* PARCHE 084: hoja + barra (SOMA_248_DOC: + PDF, HTML y Revisar IA) */
    case APP_SETTINGS:       return 760u;   /* PARCHE 085 */
    case APP_PDF:            return 760u;   /* PARCHE 088 */
    case APP_SYSMON:         return 900u;   /* PARCHE 089 */
    case APP_DISCOS:         return 920u;   /* SOMA_331_DISCOS */
    case APP_NOTICIAS:       return 760u;   /* SOMA_337_NOTICIAS */
    case APP_AGENDA:         return 860u;   /* SOMA_272_AGENDA */
    case APP_CONTACTOS:      return 900u;   /* SOMA_357_CONTACTOS: lista + ficha */
    case APP_HOJA:           return 1000u;  /* SOMA_359_HOJA_VENTANA: barra de herramientas entera */
    case APP_CORREO:         return 960u;   /* SOMA_295_CORREO: carpetas + lista + lectura */
    case APP_RECORDER:       return 800u;   /* SOMA_177_T480S; SOMA_210: con la lista de grabaciones a la vista */
    case APP_CAMERA:         return 760u;   /* SOMA_178_T480S; SOMA_210 */
    case APP_CAPTURE:        return 720u;   /* SOMA_179_T480S; SOMA_210 */
    case APP_PAINT:          return 860u;   /* PARCHE 083: lienzo + herramientas + panel de color */
    case APP_CODER_APP:      return 720u;   /* PARCHE 093 */
    case APP_MEDIA:          return 880u;   /* SOMA_240_ICONOS: biblioteca + escenario + controles con nombre */
    case APP_YOUTUBE:        return 760u;   /* SOMA_250_YOUTUBE: reproductor + lista lateral */
    case APP_SOMA_APP:       return 320u; /*PL12_APP_MIN_W*/
    case APP_TERMINAL_SHELL: return 360u;
    case APP_AGENTS:         return 640u;   /* SOMA_338_AGENTES: panel con especialistas y misiones */
    default:                 return 340u;
    }
}

/* SOMA_234_GUI_PARTIDO: el escritorio, por temas (se incluyen aqui, en este orden; ver la cabecera de cada archivo) */
#include "gui_vortice.c"         /* matematicas del vortice y estado del orbe */
#include "gui_prompter.c"        /* SOMA_259_ORBE: la respuesta como un teleprompter, con formato, y la barra de fases */
#include "gui_cosmos.c"          /* rosquilla de acrecion */
#include "gui_iconos.c"          /* iconos en orbita y vectoriales */
#include "gui_ventanas.c"        /* arrastre de iconos y gadget */
#include "gui_agente.c"          /* el agente en su propio hilo y los turnos de pantalla */
#include "gui_voz.c"             /* SOMA_245_PTT: dictado para todo el sistema (mantener Alt) */
#include "gui_orbe_fx.c"         /* efectos del orbe: particulas */
#include "gui_fotograma.c"       /* dibujo de cada fotograma */
#include "gui_minimizar.c"       /* minimizar */
#include "gui_lang.c"            /* pintado en vivo y ventana estandar de SOMA-LANG */
#include "gui_abrir.c"           /* abrir aplicaciones */
#include "gui_orbe_frente.c"     /* el orbe al frente */
#include "gui_ayuda.c"           /* SOMA_294_AYUDA: bienvenida del orbe, consejos al iniciar y ayuda con F1 */

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
    case APP_AGENT_MEMORY: if (memview_clip_op(op)) { her_refresh_all(); fb_present(); } return 1;   /* SOMA_283_MEMORIA_APP: los campos de la memoria */
    case APP_AGENT_CHAT:   if (chat_clip_op(op)) { her_refresh_all(); fb_present(); } return 1;   /* SOMA_285_CHAT */
    case APP_CORREO:       if (correo_clip_op(op)) { her_refresh_all(); fb_present(); return 1; } return 0;   /* SOMA_295_CORREO */
    case APP_HOJA:         if (hoja_gui_clip_op(op)) { her_refresh_all(); fb_present(); } return 1;   /* SOMA_359_HOJA_VENTANA */
    case APP_YOUTUBE:      if (youtube_clip_op(op)) { her_refresh_all(); fb_present(); } return 1;   /* SOMA_250_YOUTUBE: la caja de busqueda */
    case APP_LAYOUT:       if (layout_clip_op(op)) { her_refresh_all(); fb_present(); } return 1;     /* SOMA_254: ruta y descripcion del Maquetador */
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
    chat_init();                                                /* SOMA_285_CHAT */
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
    (void)console_take_help();                              /* SOMA_294_AYUDA: F1 de antes tampoco */
    her_ayuda_arranque();                                   /* SOMA_294_AYUDA: la bienvenida y los consejos (una vez por encendido) */
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
        { struct tcb *gt = thread_current(); if (gt) g_turn_gui_tid = gt->tid; }   /* SOMA_248_DOC */
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
        if (g_turn_req) {               /* SOMA_248_DOC: otro hilo dibuja fuera de pantalla (documentos) */
            gui_turn_service();
            HER_SYNC_WIN(); her_refresh_all(); fb_present();
        }
        if (current_app == APP_SETTINGS && !her_dragging && !her_resizing && settings_tick()) {   /* SOMA_175C: Ajustes > Redes al dia */
            HER_SYNC_WIN(); settings_render_window(win_x, win_y, win_w, win_h, 1); fb_present();   /* SOMA_175C2: solo su ventana (antes */
        }                                                                                         /* se recomponia todo el escritorio) */
        if (current_app == APP_DISCOS && !her_dragging && !her_resizing && discos_gui_tick()) {   /* SOMA_331_DISCOS: progreso de lo que se esta escribiendo */
            HER_SYNC_WIN(); discos_gui_render(win_x, win_y, win_w, win_h, 1); fb_present();
        }
        if (current_app == APP_AGENTS && !her_dragging && !her_resizing && agentes_gui_tick()) {   /* SOMA_338_AGENTES: la mision en curso, al momento */
            HER_SYNC_WIN(); agentes_gui_render(win_x, win_y, win_w, win_h, 1); fb_present();
        }
        if (current_app == APP_CONTACTOS && !her_dragging && !her_resizing && contactos_gui_tick()) {   /* SOMA_357_CONTACTOS: voz, fotos que llegan, miniaturas */
            HER_SYNC_WIN(); contactos_gui_render(win_x, win_y, win_w, win_h, 1); fb_present();
        } else if (current_app != APP_CONTACTOS) (void)contactos_tick();
        if (current_app == APP_HOJA && !her_dragging && !her_resizing && hoja_gui_tick()) {   /* SOMA_359_HOJA_VENTANA: avisos y cambios hechos con la orden `hoja` */
            HER_SYNC_WIN(); hoja_gui_render(win_x, win_y, win_w, win_h, 1); fb_present();
        }   /* la voz se guarda y las fotos de la nube llegan aunque la ventana no este delante */
        if (current_app == APP_NOTICIAS && !her_dragging && !her_resizing && noticias_gui_tick()) {   /* SOMA_337_NOTICIAS: titulares y resumen al llegar */
            HER_SYNC_WIN(); noticias_gui_render(win_x, win_y, win_w, win_h, 1); fb_present();
        }
        {   /* SOMA_333_SALIDA: lo que imprime el programa que lanzo el programador (se recoge aunque su ventana no este delante) */
            static uint64_t cr_t, cr_full; uint64_t t = timer_get_uptime_ms();
            if (t - cr_t >= 50 && !her_dragging && !her_resizing) {
                cr_t = t;
                int cr = coder_app_tick();
                if (cr && current_app == APP_CODER_APP) { HER_SYNC_WIN(); coder_app_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
                else if (cr == 1 && t - cr_full >= 250) { cr_full = t; HER_SYNC_WIN(); her_refresh_all(); fb_present(); }
            }
        }
        if (tbg_state == 2) {           /* SOMA_175B3: termino una orden lenta del terminal */
            int old_slot = her_term_live;
            if (her_tbg_slot > 0 && her_tbg_slot < 16 && her_term_sessions[her_tbg_slot].used)
                (void)term_ui_select(her_tbg_slot);
            else (void)term_ui_select(0);
            if (tbg_live) {                 /* SOMA_237: el ultimo prompt de python (">>> ", "... ") ya no pide nada */
                while (tbg_n >= 4 && (tbg_cap[tbg_n - 4] == '>' || tbg_cap[tbg_n - 4] == '.') && tbg_cap[tbg_n - 2] == tbg_cap[tbg_n - 4] &&
                       tbg_cap[tbg_n - 3] == tbg_cap[tbg_n - 4] && tbg_cap[tbg_n - 1] == ' ') tbg_n -= 4;
                tbg_cap[tbg_n] = 0;
            }
            if (!tbg_show() && !tbg_live) term_print("(la orden no mostro nada)");
            if (tbg_live) { tbg_live = 0; tbg_prompt[0] = 0; term_set_prompt(0, 0); }     /* SOMA_237_PYTHON */
            (void)term_ui_select(old_slot > 0 ? old_slot : 0);
            her_tbg_slot = 0;
            tbg_state = 0;
            HER_SYNC_WIN(); her_refresh_all(); fb_present();
        }
        if (g_term_run_req[0] && !tbg_state) {   /* SOMA_239: el programador lanza un programa Python en el Terminal */
            char rc[160]; uint32_t k = 0; while (g_term_run_req[k] && k < sizeof rc - 1) { rc[k] = g_term_run_req[k]; k++; } rc[k] = 0;
            g_term_run_req[0] = 0;
            her_open_app(APP_TERMINAL_SHELL, cur_w, cur_h);
            HER_SYNC_WIN();
            agent_global_focus = 0;
            {   char e[200]; uint32_t ep = 0; const char *pr = "\033[1;36msoma\033[1;32m>\033[0m ";
                while (*pr && ep < sizeof e - 1) e[ep++] = *pr++;
                for (uint32_t j = 0; rc[j] && ep < sizeof e - 1; j++) e[ep++] = rc[j];
                e[ep] = 0; term_print(e); }
            if (tbg_start(rc) == 0) {
                her_tbg_slot = her_term_live;
                if (tbg_live) term_set_prompt(tbg_ui_slot(), "");
                term_print(tbg_live ? "Python en marcha: lo que escribas aqui es para el; Esc lo para." : "En marcha en segundo plano.");
            } else term_print("No se pudo lanzar: el terminal ya tiene una orden en marcha.");
            her_refresh_all(); fb_present();
        }
        if (tbg_state == 1 && tbg_live) {   /* SOMA_237_PYTHON: la salida de python, al momento (como mucho 20 veces por segundo) */
            static uint64_t py_last;
            uint64_t t = timer_get_uptime_ms();
            if (t - py_last >= 50) { py_last = t; if (tbg_stream()) { HER_SYNC_WIN(); her_refresh_all(); fb_present(); } }
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
        /* SOMA_353_VENTANA_FANTASMA: la ventana se puede abrir o mover desde otro hilo (la orden 'navegador', el agente: her_open_app
         * con el turno de la GUI) y eso cambia her_win_*, pero no estas copias del bucle hasta el siguiente HER_SYNC_WIN. Mientras,
         * el fotograma rapido del navegador (SOMA_152) lo pintaba con la geometria inicial: una "ventana fantasma" arriba a la
         * izquierda que tapaba el borde izquierdo de la de verdad, y el raton del navegador con las coordenadas de esa. */
        HER_SYNC_WIN();

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
        {   /* SOMA_245_PTT: dictado para todo el sistema (mantener Alt); la pastilla la dibuja fb_present encima de todo */
            int vz = her_voz_tick();
            if (vz == 2) { HER_SYNC_WIN(); her_refresh_all(); fb_present(); }
            else if (vz == 1) fb_present();
        }
        {   /* SOMA_294_AYUDA: F1 y los consejos (la tarjeta la dibuja fb_present encima de todo; nunca se queda con la entrada) */
            if (console_take_help()) her_ayuda_f1();
            int ay = her_ayuda_tick();
            if (ay == 2) { HER_SYNC_WIN(); her_refresh_all(); fb_present(); }
            else if (ay == 1) fb_present();
        }
        if (console_take_super()) {                             /* SOMA_168B_ORBE: tecla Windows / Option */
            her_ay_ev(AYEV_SUPER);                              /* SOMA_294_AYUDA: el consejo de la tecla Windows, aprendido */
            if (her_num_windows > 0) {
                int app = her_summon_run(cur_w, cur_h);
                if (app >= 0) agent_global_focus = 0;   /* SOMA_285_CHAT: el chat tiene su propio cuadro de texto (el orbe no se queda las teclas) */
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
            struct rtc_time t; hora_local(&t);           /* SOMA_332_HORA */
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
            static const int live_apps[7] = { APP_CAPTURE, APP_CAMERA, APP_RECORDER, APP_YOUTUBE, APP_AGENT_MEMORY, APP_AGENT_CHAT, APP_CORREO };   /* SOMA_295_CORREO */   /* SOMA_285_CHAT: respuestas y animaciones */
            { int abierta = 0; for (int i = 0; i < her_num_windows; i++) if (her_windows[i].app == APP_AGENT_CHAT) abierta = 1; if (!abierta) (void)chat_tick(); }   /* SOMA_285_CHAT: cerrada o minimizada, la respuesta llega igual */
   /* SOMA_250_YOUTUBE: el video se pinta solo; SOMA_283_MEMORIA_APP: la memoria en vivo */
            for (int k = 0; k < 7; k++) {
                int app = live_apps[k], si = -1;
                for (int i = 0; i < her_num_windows; i++) if ((int)her_windows[i].app == app) si = i;
                if (si < 0) continue;
                uint8_t own = acct_set_owner((uint8_t)(ACCT_APP0 + app));
                int ch = app == APP_CAPTURE ? capapp_tick() : app == APP_CAMERA ? camapp_tick() : app == APP_YOUTUBE ? youtube_tick() : app == APP_AGENT_MEMORY ? memview_tick() : app == APP_AGENT_CHAT ? chat_tick() : app == APP_CORREO ? correo_tick() : recorder_tick();
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
        if (!her_dragging && !her_resizing && paint_ia_slot() >= 0) {   /* SOMA_292_PAINT_IA: el dibujo de la IA se pinta solo, paso a paso */
            int ps = paint_ia_slot(), si = -1;
            for (int i = 0; i < her_num_windows; i++) if (her_windows[i].app == APP_PAINT && her_windows[i].term_slot == ps) si = i;
            if (si >= 0) {
                struct her_win_node *sw = &her_windows[si];
                if (sw->term_slot > 0) paint_select(sw->term_slot);   /* solo la instancia de Paint (sin tocar el foco del terminal) */
                if (paint_animando()) {
                    uint8_t own = acct_set_owner((uint8_t)(ACCT_APP0 + APP_PAINT));
                    int covered = 0;
                    for (int j = si + 1; j < her_num_windows; j++) {
                        struct her_win_node *o = &her_windows[j];
                        if (o->x < sw->x + sw->w && sw->x < o->x + o->w && o->y < sw->y + sw->h && sw->y < o->y + o->h) { covered = 1; break; }
                    }
                    if (!covered && settings_opacity() >= 100) her_render_app_body(APP_PAINT, HER_CLI_X(sw->x), HER_CLI_Y(sw->y), HER_CLI_W(sw->w), HER_CLI_H(sw->h), si == her_num_windows - 1);
                    else her_refresh_all();
                    if (her_cursor_should_show()) mouse_draw_cursor(mouse_x, mouse_y);
                    fb_present();
                    acct_set_owner(own);
                }
                if (her_num_windows > 0) { struct her_win_node *tw = &her_windows[her_num_windows - 1]; if (tw->app == APP_PAINT && tw->term_slot > 0) paint_select(tw->term_slot); }   /* la de Paint activa, como estaba */
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
                if (!her_cursor_should_show()) her_pr_raton(m_dy);   /* SOMA_259_ORBE: sin puntero, el raton arriba/abajo recorre la respuesta */
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

            if (agenda_gui_aviso() && current_app == APP_AGENDA) { agenda_gui_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }   /* SOMA_272_AGENDA: avisos activos, con o sin ventana */
            acct_set_owner((uint8_t)(ACCT_APP0 + current_app));   /* PARCHE 089 */
            {   /* SOMA_PARCHE_024_TERM: rueda y boton derecho */
                int wheel = mouse_wheel_take();
                if (wheel && !her_cursor_should_show()) her_pr_rueda(wheel);   /* SOMA_259_ORBE: y la rueda */
                int rclick = mouse_right_take_click();
                if (rclick && her_min_right_close(mouse_x, mouse_y, cur_w, cur_h)) { rclick = 0; her_refresh_all(); fb_present(); }   /* SOMA_291_CERRAR_ORBITA */
                if (current_app == APP_WEB_BROWSER) browser_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held()); /* SOMA_PARCHE_NAVEGADOR */
                if (current_app == APP_FILE_MANAGER && !her_dragging && !her_resizing) {   /* SOMA_PARCHE_028_UI */
                    if (fm_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held(), rclick))
                        fm_render_window(win_x, win_y, win_w, win_h, 1);
                }
                if (current_app == APP_AGENTS && !her_dragging && !her_resizing) {     /* SOMA_338_AGENTES: rueda y resaltado */
                    if (agentes_gui_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel)) { agentes_gui_render(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_CONTACTOS && !her_dragging && !her_resizing) {   /* SOMA_357_CONTACTOS: rueda y arrastre en los campos */
                    if (contactos_gui_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held())) { contactos_gui_render(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_HOJA && !her_dragging && !her_resizing) {   /* SOMA_359_HOJA_VENTANA: rueda, arrastres y menu del boton derecho */
                    if (hoja_gui_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held(), rclick)) { hoja_gui_render(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_NOTICIAS && !her_dragging && !her_resizing) {   /* SOMA_337_NOTICIAS: rueda y resaltado */
                    if (noticias_gui_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel)) { noticias_gui_render(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_CODER_APP && !her_dragging && !her_resizing) {   /* PARCHE 093: arrastre de seleccion y rueda */
                    if (coder_app_mouse(mouse_x, mouse_y, wheel, mouse_left_held())) { coder_app_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_CORREO && !her_dragging && !her_resizing) {   /* SOMA_295_CORREO: rueda, resaltado y arrastre en los campos */
                    if (correo_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held())) { correo_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_AGENT_CHAT && !her_dragging && !her_resizing) {   /* SOMA_285_CHAT: rueda, resaltado de enlaces y botones, arrastre */
                    if (chat_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held())) { chat_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
                }
                if (current_app == APP_AGENT_MEMORY && !her_dragging && !her_resizing) {   /* SOMA_283_MEMORIA_APP: rueda, resaltado y arrastre en los campos */
                    if (memview_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held())) { memview_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
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
                if (current_app == APP_YOUTUBE && !her_dragging && !her_resizing) {    /* SOMA_250_YOUTUBE: rueda, resaltado y seleccion en la busqueda */
                    if (youtube_mouse(mouse_x, mouse_y, win_x, win_y, win_w, win_h, wheel, mouse_left_held())) { youtube_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); }
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

            if (m_left_click && her_ayuda_clic(mouse_x, mouse_y)) m_left_click = 0;   /* SOMA_294_AYUDA: clic en una tarjeta de la ayuda */
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
                    agent_global_focus = 0;   /* SOMA_285_CHAT: el chat tiene su propio cuadro de texto (el orbe no se queda las teclas) */
                    if (current_app == APP_YOUTUBE) {                /* SOMA_250_YOUTUBE */
                        youtube_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        youtube_render_window(win_x, win_y, win_w, win_h, 1);
                    } else
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
                    if (current_app == APP_CORREO) {                 /* SOMA_295_CORREO */
                        correo_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        correo_render_window(win_x, win_y, win_w, win_h, 1);
                    } else
                    if (current_app == APP_AGENDA) {                 /* SOMA_272_AGENDA */
                        agenda_gui_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        agenda_gui_render_window(win_x, win_y, win_w, win_h, 1);
                    } else
                    if (current_app == APP_SYSMON) {                 /* PARCHE 089 */
                        if (sysmon_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h) == 2) { HER_SYNC_WIN(); her_refresh_all(); }
                        else sysmon_render_window(win_x, win_y, win_w, win_h, 1);
                    } else
                    if (current_app == APP_DISCOS) {                 /* SOMA_331_DISCOS */
                        discos_gui_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        discos_gui_render(win_x, win_y, win_w, win_h, 1);
                    } else
                    if (current_app == APP_AGENTS) {                 /* SOMA_338_AGENTES: Detener, Probar */
                        agentes_gui_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        agentes_gui_render(win_x, win_y, win_w, win_h, 1);
                    } else
                    if (current_app == APP_HOJA) {                   /* SOMA_359_HOJA_VENTANA */
                        int rc = hoja_gui_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (rc == 5) { her_open_app(APP_CORREO, cur_w, cur_h); HER_SYNC_WIN(); her_refresh_all(); }   /* SOMA_360: la hoja adjunta en un correo */
                        else hoja_gui_render(win_x, win_y, win_w, win_h, 1);
                    } else
                    if (current_app == APP_CONTACTOS) {              /* SOMA_357_CONTACTOS: la Camara (foto del contacto) o el Correo */
                        int rc = contactos_gui_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (rc == 2) { her_open_app(APP_CAMERA, cur_w, cur_h); HER_SYNC_WIN(); her_refresh_all(); }
                        else if (rc == 3) { her_open_app(APP_CORREO, cur_w, cur_h); HER_SYNC_WIN(); correo_redactar_a(contactos_gui_email()); her_refresh_all(); }
                        else if (rc == 4) { her_open_app(APP_HOJA, cur_w, cur_h); HER_SYNC_WIN(); her_refresh_all(); }   /* SOMA_360: la libreta en una hoja */
                        else contactos_gui_render(win_x, win_y, win_w, win_h, 1);
                    } else
                    if (current_app == APP_NOTICIAS) {               /* SOMA_337_NOTICIAS: un titular se abre en el Navegador */
                        if (noticias_gui_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h) == 3) {
                            char url[NT_URL]; const char *u = noticias_gui_url(); uint32_t k = 0; while (u[k] && k + 1 < sizeof url) { url[k] = u[k]; k++; } url[k] = 0;
                            her_open_app(APP_WEB_BROWSER, cur_w, cur_h); HER_SYNC_WIN(); browser_load_url(url); her_refresh_all();
                        } else noticias_gui_render(win_x, win_y, win_w, win_h, 1);
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
                    } else if (current_app == APP_AGENT_CHAT) {                     /* SOMA_285_CHAT */
                        int act = chat_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (act == 3) {                                              /* un enlace: al Navegador */
                            char url[1024]; const char *u = chat_url(); uint32_t k = 0; while (u[k] && k + 1 < sizeof url) { url[k] = u[k]; k++; } url[k] = 0;
                            her_open_app(APP_WEB_BROWSER, cur_w, cur_h); HER_SYNC_WIN(); browser_load_url(url); her_refresh_all();
                        } else if (act == 4) { her_open_notepad_path(chat_path(), cur_w, cur_h); HER_SYNC_WIN(); her_refresh_all(); }
                        else if (act == 5) { her_open_app_path(APP_IMAGE_VIEWER, chat_path(), cur_w, cur_h); HER_SYNC_WIN(); her_refresh_all(); }
                        else chat_render_window(win_x, win_y, win_w, win_h, 1);
                    }
                }
            }
            fb_present();
        }

        if (kgetc_ready()) {
            char c = kgetc();
            if (soma_clip_gui_key(c)) continue;   /* SOMA_PARCHE_PORTAPAPELES */
            acct_set_owner((uint8_t)(ACCT_APP0 + current_app));   /* PARCHE 089 */
            if (c == 27 && her_voz_escape()) continue;                       /* SOMA_244_VOZ: Esc cancela el dictado */
            if (her_ayuda_tecla(c)) continue;                                /* SOMA_294_AYUDA: Esc con una tarjeta de ayuda a la vista */
            if (c == 27 && current_app != APP_NOTEPAD && her_stop_escape(agent_global_focus || current_app == APP_DESKTOP)) continue;   /* SOMA_256: Esc detiene la lectura o la mision */
            if (c == 27 && current_app == APP_CORREO) { if (correo_escape()) { correo_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); } else her_close_current_app_vortex(); continue; }   /* SOMA_295_CORREO */
            if (c == 27 && current_app == APP_AGENT_CHAT) { (void)chat_escape(); chat_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue; }   /* SOMA_285_CHAT: Esc no sale del escritorio */
            if (c == 27 && !her_esc_propio(current_app)) {   /* SOMA_359_HOJA_VENTANA: las apps con Esc propio lo atienden mas abajo (antes Esc salia del escritorio) */
                if (current_app == APP_TERMINAL_SHELL && term_popup_open()) {      /* SOMA_PARCHE_024_TERM: Esc cierra el menu */
                    term_popup_close();
                    render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present(); continue;
                }
                if (current_app == APP_SOMA_APP) {                                  /* SOMA_239: Esc cierra la ventana de la aplicacion (SOMA-LANG */
                    her_close_current_app_vortex();                                 /* o Python), como dice su referencia; antes salia del escritorio */
                    continue;
                }
                if (current_app == APP_TERMINAL_SHELL && tbg_live_stop()) {         /* SOMA_237_PYTHON: Esc para python */
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
                if (current_app == APP_LAYOUT && layout_escape()) {                /* SOMA_254: Esc suelta el campo de texto antes de cerrar */
                    layout_render_window(win_x, win_y, win_w, win_h, 1);
                    fb_present(); continue;
                }
                if (current_app == APP_YOUTUBE && youtube_escape()) {              /* SOMA_250_YOUTUBE: Esc sale de la caja de busqueda o del video */
                    youtube_render_window(win_x, win_y, win_w, win_h, 1);
                    fb_present(); continue;
                }
                if (current_app == APP_NOTEPAD && notepad_escape()) {              /* SOMA_PARCHE_028_UI: Esc cierra el menu o el dialogo del Notepad */
                    notepad_render_window(win_x, win_y, win_w, win_h, 1);
                    fb_present(); continue;
                }
                if (current_app == APP_NOTEPAD && her_stop_escape(0)) continue;    /* SOMA_256: en el Bloc de notas, Esc para la lectura despues de cerrar menus */
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
            if (current_app == APP_AGENTS) {                     /* SOMA_338_AGENTES: Esc cierra la ventana */
                if (!agentes_gui_key(c) && c == 27) { her_close_current_app_vortex(); continue; }
                agentes_gui_render(win_x, win_y, win_w, win_h, 1);
                fb_present();
                continue;
            }

            if (current_app == APP_YOUTUBE) {                    /* SOMA_250_YOUTUBE */
                youtube_handle_key(c); youtube_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
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
            if (current_app == APP_CORREO) {                     /* SOMA_295_CORREO: Esc ya se atendio arriba */
                correo_handle_key(c); correo_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_AGENDA) {                     /* SOMA_272_AGENDA */
                if (c == 27) { her_close_current_app_vortex(); continue; }
                agenda_gui_handle_key(c); agenda_gui_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_SYSMON) {                     /* PARCHE 089 */
                if (c == 27) { her_close_current_app_vortex(); continue; }
                sysmon_handle_key(c); sysmon_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_DISCOS) {                     /* SOMA_331_DISCOS: Esc cierra el dialogo; sin dialogo, la ventana */
                if (!discos_gui_key(c) && c == 27) { her_close_current_app_vortex(); continue; }
                discos_gui_render(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_HOJA) {                       /* SOMA_359_HOJA_VENTANA: Esc cancela la edicion, cierra menus y barras; sin nada, la ventana */
                if (!hoja_gui_key(c) && c == 27) { her_close_current_app_vortex(); continue; }
                hoja_gui_render(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_CONTACTOS) {                  /* SOMA_357_CONTACTOS: Esc cierra la edicion o la barra; sin nada abierto, la ventana */
                if (!contactos_gui_key(c) && c == 27) { her_close_current_app_vortex(); continue; }
                contactos_gui_render(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_NOTICIAS) {                   /* SOMA_337_NOTICIAS: Esc cierra los ajustes; sin ajustes, la ventana */
                if (!noticias_gui_key(c) && c == 27) { her_close_current_app_vortex(); continue; }
                noticias_gui_render(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
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
            if (current_app == APP_AGENT_CHAT) {                              /* SOMA_285_CHAT */
                chat_handle_key(c); chat_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_MEDIA) {
                media_handle_key(c); media_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_PAINT) {
                if (c == 27) { her_close_current_app_vortex(); continue; }
                paint_handle_key(c); paint_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_LAYOUT) {
                if (c == 27 && !layout_escape()) {
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
                    if (tbg_live_enter(cur_cmd, cur_cmd_len)) {         /* SOMA_237_PYTHON: la linea es para python */
                        cur_cmd_len = 0; cur_cursor_pos = 0; cur_cmd[0] = '\0';
                        render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                        fb_present(); continue;
                    }
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
                        term_add_line("Espera: la orden anterior sigue en marcha; su resultado saldra aqui.", max_visible_terminal);
                    }
                    else if (tbg_is_slow(cmd_run) && tbg_start(cmd_run) == 0) {
                        her_tbg_slot = her_term_live;
                        if (tbg_live) term_set_prompt(tbg_ui_slot(), "");      /* SOMA_239: sin "soma>": lo que se escriba es para python */
                        term_add_line(tbg_live ? "Python en marcha: lo que escribas aqui es para el; Esc lo para." :
                                      "En marcha en segundo plano: puedes seguir usando el sistema; el resultado saldra aqui.", max_visible_terminal);
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
                        int ret = dispatch_command_usuario(cmd_run, cmd_out, sizeof(cmd_out));   /* SOMA_268_ACCESO */
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

                }                                                               /* SOMA_285_CHAT: el chat atiende sus teclas en chat_handle_key */
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
