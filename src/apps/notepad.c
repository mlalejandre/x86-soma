#include "notepad.h"
#include "fb.h"
#include "gui_font.h"
#include "fs.h"
#include "console.h"
#include "mem.h"
#include "clipboard.h"
#include "mouse.h"
#include "gui.h"
#include "web_font.h"
#include "settings.h"

/* SOMA_PARCHE_028_UI: el raton llega por eventos reales de la GUI (notepad_handle_click / notepad_mouse). Antes se
 * consultaban mouse_get_x()/mouse_get_y(), que en este kernel son los valores fijos de relleno de la maquina virtual
 * (640,360): por eso la seleccion saltaba al centro de la pantalla y el menu contextual no salia bajo el puntero. */
extern void gui_open_browser(const char *path) __attribute__((weak));
uint64_t timer_get_uptime_ms(void);

#define NP_MAX_SIZE 131072                 /* PARCHE 087: 128 KiB (antes 16 KiB, el tope de RamFS) */

/* Seleccion de texto y arrastre del raton */
static int32_t np_mx = -1, np_my = -1;  /* ultima posicion REAL del puntero (la da la GUI) */
static int np_prev_left = 0;
static int np_hover_btn = -1;

/* Estado para acentos y teclas muertas (Dead Keys) */

/* SOMA Live Typewriter Hook (IA) */

/* Geometria visual */
#define NP_TOOLBAR_H   40
#define NP_CRUMB_H     26
#define NP_STATUS_H    26
#define NP_GUTTER_W    52
#define NP_CELL_W      8
#define NP_LINE_H      18
#define NP_PAD_X       10
#define NP_PAD_Y       8
#define NP_SB_W        12
#define NP_EM          13
#define NP_BASE        13

/* Paleta */
#define NC_BG        0x000B0E14
#define NC_BAR       0x00141926
#define NC_BAR2      0x000F131D
#define NC_LINE      0x00212938
#define NC_GUTTER    0x000D1119
#define NC_GUT_TXT   0x00485670
#define NC_TXT       0x00E2E8F0
#define NC_DIM       0x008A97AD
#define NC_ACCENT    0x0038BDF8
#define NC_SEL       0x00214E86
#define NC_CURLINE   0x00121722
#define NC_HOVER     0x00232C3F

/* -------------------------------------------------------------------------
 * MENU CONTEXTUAL FLOTANTE (clic derecho)
 * ------------------------------------------------------------------------- */
#define CTX_W       252
#define CTX_ITEM_H  26
#define CTX_SEP_H   9
#define CTX_PAD     6
enum { MI_CUT = 0, MI_COPY, MI_PASTE, MI_DEL, MI_ALL, MI_LINE, MI_OPEN, MI_SAVE, MI_BROWSER, MI_SPEAK };
struct np_mi { const char *label; const char *key; int id; };
static const struct np_mi np_menu[] = {
    { "Cortar",               "Ctrl+X", MI_CUT },
    { "Copiar",               "Ctrl+C", MI_COPY },
    { "Pegar",                "Ctrl+V", MI_PASTE },
    { "Eliminar",             "Supr",   MI_DEL },
    { 0, 0, -1 },
    { "Seleccionar todo",     "Ctrl+A", MI_ALL },
    { "Seleccionar l\xC3\xADnea", 0,    MI_LINE },
    { 0, 0, -1 },
    { "Abrir archivo...",     "Ctrl+O", MI_OPEN },
    { "Guardar",              "Ctrl+S", MI_SAVE },
    { "Abrir en el navegador", "Ctrl+B", MI_BROWSER },
#if defined(__x86_64__)
    { 0, 0, -1 },
    { "Leer en voz alta",     0,        MI_SPEAK },                 /* SOMA_247_HABLA: la seleccion o todo el texto */
#endif
};
#define CTX_N ((int)(sizeof(np_menu) / sizeof(np_menu[0])))

/* -------------------------------------------------------------------------
 * MODAL DE ARCHIVOS (OPEN / SAVE AS)
 * ------------------------------------------------------------------------- */
enum np_dialog_mode {
    NP_DLG_NONE = 0,
    NP_DLG_OPEN,
    NP_DLG_SAVE_AS
};


#define DLG_MAX_FILES 64
struct dlg_item {
    char name[48];
    uint32_t size;
    int is_dir;
};
static char ram_buf[4096];

/* SOMA_211_NOTAS: varias ventanas del Bloc de notas. Todo lo que es de un documento (texto, ruta, cursor, seleccion, menu,
 * dialogo de abrir/guardar...) vive en una instancia; la GUI elige la de cada ventana con notepad_select() antes de dibujarla
 * o de pasarle eventos. Elegir es cambiar un puntero: no se copia el texto. La instancia 1 usa la memoria estatica de siempre;
 * las demas se reservan al abrir la ventana y se liberan al cerrarla. */
struct np_inst {
    char np_text[NP_MAX_SIZE];
    uint32_t np_len, np_cursor;
    char np_path[128];
    int np_scroll;
    char np_status[64];
    int np_dirty;
    int np_sel, np_dragging, np_sel_mode;
    uint32_t np_anchor_lo, np_anchor_hi;
    int np_click_count;
    uint64_t np_last_click_time;
    int32_t np_last_click_x, np_last_click_y;
    int np_swallow_click, np_sb_drag, np_sb_grab;
    uint64_t np_caret_t;
    int np_goal_col, np_dead_key;
    uint8_t np_utf8_lead;
    int np_follow, np_live;
    const char *np_live_src;
    uint32_t np_live_len, np_live_pos;
    int np_ctx_open; int32_t np_ctx_x, np_ctx_y; int np_ctx_hover;
    enum np_dialog_mode dlg_mode;
    char dlg_folder[128], dlg_filename[64];
    uint32_t dlg_filename_len;
    struct dlg_item dlg_items[DLG_MAX_FILES];
    int dlg_item_count, dlg_sel_idx, dlg_scroll;
    uint64_t dlg_last_click;
    uint32_t np_cols_last;
    int np_named;                      /* 1 = la ruta es la de un archivo abierto o guardado */
};
#define NP_INST_MAX 8
static struct np_inst np_inst1 = {
    .np_path = "/workspace/notas.txt", .np_status = "Listo", .np_sel = -1, .np_goal_col = -1, .np_ctx_hover = -1,
    .dlg_mode = NP_DLG_NONE, .dlg_folder = "/documentos", .dlg_filename = "notas.txt", .dlg_filename_len = 9, .dlg_sel_idx = -1,
};
static struct np_inst *np_insts[NP_INST_MAX + 1] = { 0, &np_inst1 };
static struct np_inst *NP = &np_inst1;
#define np_text (NP->np_text)
#define np_len (NP->np_len)
#define np_cursor (NP->np_cursor)
#define np_path (NP->np_path)
#define np_scroll (NP->np_scroll)
#define np_status (NP->np_status)
#define np_dirty (NP->np_dirty)
#define np_sel (NP->np_sel)
#define np_dragging (NP->np_dragging)
#define np_sel_mode (NP->np_sel_mode)
#define np_anchor_lo (NP->np_anchor_lo)
#define np_anchor_hi (NP->np_anchor_hi)
#define np_click_count (NP->np_click_count)
#define np_last_click_time (NP->np_last_click_time)
#define np_last_click_x (NP->np_last_click_x)
#define np_last_click_y (NP->np_last_click_y)
#define np_swallow_click (NP->np_swallow_click)
#define np_sb_drag (NP->np_sb_drag)
#define np_sb_grab (NP->np_sb_grab)
#define np_caret_t (NP->np_caret_t)
#define np_goal_col (NP->np_goal_col)
#define np_dead_key (NP->np_dead_key)
#define np_utf8_lead (NP->np_utf8_lead)
#define np_follow (NP->np_follow)
#define np_live (NP->np_live)
#define np_live_src (NP->np_live_src)
#define np_live_len (NP->np_live_len)
#define np_live_pos (NP->np_live_pos)
#define np_ctx_open (NP->np_ctx_open)
#define np_ctx_x (NP->np_ctx_x)
#define np_ctx_y (NP->np_ctx_y)
#define np_ctx_hover (NP->np_ctx_hover)
#define dlg_mode (NP->dlg_mode)
#define dlg_folder (NP->dlg_folder)
#define dlg_filename (NP->dlg_filename)
#define dlg_filename_len (NP->dlg_filename_len)
#define dlg_items (NP->dlg_items)
#define dlg_item_count (NP->dlg_item_count)
#define dlg_sel_idx (NP->dlg_sel_idx)
#define dlg_scroll (NP->dlg_scroll)
#define dlg_last_click (NP->dlg_last_click)
#define np_cols_last (NP->np_cols_last)
#define np_named (NP->np_named)

/* -------------------------------------------------------------------------
 * Utilidades de Cadena y Codificación UTF-8
 * ------------------------------------------------------------------------- */
static void str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (src && src[i] && i + 1 < max) { dst[i] = src[i]; i++; }
    if (max > 0) dst[i] = '\0';
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return (*a == *b);
}

static uint32_t str_len(const char *s) {
    uint32_t n = 0; while (s && s[n]) n++; return n;
}

static int str_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return 1;
}

static void ap(char *buf, uint32_t max, const char *s) {
    uint32_t l = str_len(buf);
    while (s && *s && l + 1 < max) buf[l++] = *s++;
    buf[l] = '\0';
}

static const char *get_filename_part(const char *p) {
    if (!p || !*p) return "nuevo.txt";
    const char *last = p;
    while (*p) { if (*p == '/' || *p == '\\') last = p + 1; p++; }
    return (*last != '\0') ? last : "notas.txt";
}

static void u32_to_str(uint32_t val, char *buf) {
    char tmp[12]; int n = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (val > 0) { tmp[n++] = '0' + (val % 10); val /= 10; }
    uint32_t len = 0;
    while (n > 0) buf[len++] = tmp[--n];
    buf[len] = '\0';
}

static void path_join(const char *dir, const char *file, char *out, uint32_t max) {
    uint32_t p = 0;
    while (dir && dir[p] && p + 1 < max) { out[p] = dir[p]; p++; }
    if (p > 0 && out[p - 1] != '/' && p + 1 < max) out[p++] = '/';
    uint32_t fp = 0;
    while (file && file[fp] && p + 1 < max) out[p++] = file[fp++];
    out[p] = '\0';
}

static int utf8_charlen(uint8_t c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static uint32_t utf8_next(const char *s, uint32_t pos, uint32_t max) {
    if (pos >= max) return max;
    int len = utf8_charlen((uint8_t)s[pos]);
    if (pos + len > max) return max;
    return pos + len;
}

static uint32_t utf8_prev(const char *s, uint32_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && ((uint8_t)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

/* -------------------------------------------------------------------------
 * Gestión del Explorador de Archivos Modal
 * ------------------------------------------------------------------------- */
static const char *np_ram_list_begin(void) {
    int len = vfs_format_list_ext(1, 0, ram_buf, sizeof(ram_buf) - 1);
    if (len <= 0) return 0;
    ram_buf[len] = '\0';
    const char *p = ram_buf;
    if (p[0] == 'A' && p[1] == 'r') { while (*p && *p != ':') p++; if (*p == ':') p++; }
    return p;
}

static int np_ram_next(const char **pp, char *out, uint32_t max) {
    const char *p = *pp;
    while (*p == ' ' || *p == ',') p++;
    if (!*p) { *pp = p; return 0; }
    uint32_t k = 0;
    while (*p && *p != ',') { if (k + 1 < max) out[k++] = *p; p++; }
    out[k] = '\0'; *pp = p;
    return 1;
}

static void dlg_refresh_files(void) {
    dlg_item_count = 0;
    dlg_sel_idx = -1;
    dlg_scroll = 0;

    if (!str_eq(dlg_folder, "/")) {
        str_copy(dlg_items[dlg_item_count].name, "..", sizeof(dlg_items[0].name));
        dlg_items[dlg_item_count].is_dir = 1;
        dlg_items[dlg_item_count].size = 0;
        dlg_item_count++;
    }

    uint32_t fol_len = str_len(dlg_folder);
    int is_root = str_eq(dlg_folder, "/");
    const char *p = np_ram_list_begin();
    char fname[80];

    while (p && dlg_item_count < DLG_MAX_FILES && np_ram_next(&p, fname, sizeof(fname))) {
        const char *rel = 0;
        if (is_root) {
            if (fname[0] == '/') rel = fname + 1;
        } else if (str_starts_with(fname, dlg_folder) && fname[fol_len] == '/') {
            rel = fname + fol_len + 1;
        }
        if (!rel || !*rel) continue;

        const char *slash = 0;
        for (const char *s = rel; *s; ++s) { if (*s == '/') { slash = s; break; } }
        if (slash) {
            char dname[48];
            uint32_t dl = (uint32_t)(slash - rel);
            if (dl >= sizeof(dname)) dl = sizeof(dname) - 1;
            for (uint32_t k = 0; k < dl; ++k) dname[k] = rel[k];
            dname[dl] = '\0';

            int dup = 0;
            for (int k = 0; k < dlg_item_count; ++k) {
                if (dlg_items[k].is_dir && str_eq(dlg_items[k].name, dname)) { dup = 1; break; }
            }
            if (!dup && dlg_item_count < DLG_MAX_FILES) {
                str_copy(dlg_items[dlg_item_count].name, dname, sizeof(dlg_items[0].name));
                dlg_items[dlg_item_count].is_dir = 1;
                dlg_items[dlg_item_count].size = 0;
                dlg_item_count++;
            }
        } else if (!str_eq(rel, ".dir")) {
            str_copy(dlg_items[dlg_item_count].name, rel, sizeof(dlg_items[0].name));
            dlg_items[dlg_item_count].is_dir = 0;
            char full[128];
            path_join(dlg_folder, rel, full, sizeof(full));
            char tmp[8];
            int sz = vfs_read(full, tmp, 1);
            dlg_items[dlg_item_count].size = (sz >= 0) ? 1 : 0;
            dlg_item_count++;
        }
    }
}

static void dlg_open_folder(const char *folder) {
    str_copy(dlg_folder, folder, sizeof(dlg_folder));
    if (!dlg_folder[0]) str_copy(dlg_folder, "/", sizeof(dlg_folder));
    dlg_refresh_files();
}

static void dlg_go_up(void) {
    if (str_eq(dlg_folder, "/")) return;
    uint32_t len = str_len(dlg_folder);
    while (len > 1 && dlg_folder[len - 1] == '/') len--;
    int slash = -1;
    for (int i = (int)len - 1; i >= 0; --i) {
        if (dlg_folder[i] == '/') { slash = i; break; }
    }
    if (slash <= 0) str_copy(dlg_folder, "/", sizeof(dlg_folder));
    else { dlg_folder[slash] = '\0'; }
    dlg_refresh_files();
}

/* Abrir por defecto la carpeta de documentos donde residen los .txt */
static void dlg_open_documents_folder(void) {
    str_copy(dlg_folder, "/documentos", sizeof(dlg_folder));
    dlg_refresh_files();
    if (dlg_item_count > 0) return;

    str_copy(dlg_folder, "/workspace", sizeof(dlg_folder));
    dlg_refresh_files();
    if (dlg_item_count > 0) return;

    str_copy(dlg_folder, "/documentos", sizeof(dlg_folder));
    dlg_refresh_files();
}

/* -------------------------------------------------------------------------
 * Ciclo de Vida y Persistencia
 * ------------------------------------------------------------------------- */
void notepad_init(void) {
    np_named = 0;
    np_follow = 0;
    np_live = 0;
    np_len = 0;
    np_cursor = 0;
    np_scroll = 0;
    np_dirty = 0;
    np_sel = -1;
    np_dragging = 0;
    np_sel_mode = 0;
    np_click_count = 0;
    np_swallow_click = 0;
    np_sb_drag = 0;
    np_hover_btn = -1;
    np_goal_col = -1;
    np_ctx_hover = -1;
    np_dead_key = 0;
    np_utf8_lead = 0;
    np_ctx_open = 0;
    dlg_mode = NP_DLG_NONE;
    np_text[0] = '\0';
    str_copy(np_status, "Nuevo documento", sizeof(np_status));
}

void notepad_load_file(const char *path) {
    notepad_init();
    np_named = 1, str_copy(np_path, path, sizeof(np_path));

    int r = vfs_read(np_path, np_text, NP_MAX_SIZE - 1);
    if (r >= 0) {
        np_len = (uint32_t)r;
        np_text[np_len] = '\0';
        np_cursor = 0;                 /* SOMA_PARCHE_028_UI: al abrir, el cursor al principio del documento */
        np_scroll = 0;
        np_dirty = 0;
        str_copy(np_status, "Archivo cargado", sizeof(np_status));
    } else {
        str_copy(np_status, "Nuevo documento (sin guardar)", sizeof(np_status));
    }
}

static int notepad_save(void) {
    if (vfs_write(np_path, np_text, np_len) >= 0) {
        np_dirty = 0;
        str_copy(np_status, "Guardado con exito", sizeof(np_status));
        return 1;
    } else {
        str_copy(np_status, "Error al guardar archivo", sizeof(np_status));
        return 0;
    }
}

void notepad_open_in_browser(const char *path) {
    if (np_dirty) notepad_save();
    const char *target = (path && *path) ? path : np_path;
    if (gui_open_browser) {
        gui_open_browser(target);
        str_copy(np_status, "Abierto en Navegador", sizeof(np_status));
    } else {
        str_copy(np_status, "Navegador no disponible", sizeof(np_status));
    }
}

/* -------------------------------------------------------------------------
 * Gestión de Rango de Selección y Búfer
 * ------------------------------------------------------------------------- */
static void np_sel_range(uint32_t *lo, uint32_t *hi) {
    uint32_t c = (np_cursor > np_len) ? np_len : np_cursor;
    if (np_sel < 0) { *lo = *hi = c; return; }
    uint32_t a = (uint32_t)np_sel;
    if (a > np_len) a = np_len;
    *lo = (a < c) ? a : c;
    *hi = (a < c) ? c : a;
}

static int np_has_sel(void) {
    uint32_t lo, hi;
    np_sel_range(&lo, &hi);
    return hi > lo;
}

static void np_delete_range(uint32_t lo, uint32_t hi) {
    if (hi > np_len) hi = np_len;
    if (lo >= hi) return;
    uint32_t d = hi - lo;
    for (uint32_t i = lo; i + d <= np_len; ++i) np_text[i] = np_text[i + d];
    np_len -= d;
    np_cursor = lo;
    np_sel = -1;
    np_dirty = 1;
    np_text[np_len] = '\0';
}

static void np_insert_bytes(const char *s, uint32_t n) {
    if (np_has_sel()) {
        uint32_t lo, hi;
        np_sel_range(&lo, &hi);
        np_delete_range(lo, hi);
    }
    np_sel = -1;
    if (np_cursor > np_len) np_cursor = np_len;
    if (np_len + n > NP_MAX_SIZE - 2) {
        n = (NP_MAX_SIZE - 2 > np_len) ? (NP_MAX_SIZE - 2 - np_len) : 0;
    }
    if (n == 0) return;

    for (uint32_t i = np_len + n; i >= np_cursor + n; --i) {
        np_text[i] = np_text[i - n];
        if (i == 0) break;
    }
    for (uint32_t i = 0; i < n; ++i) np_text[np_cursor + i] = s[i];

    np_cursor += n;
    np_len += n;
    np_text[np_len] = '\0';
    np_dirty = 1;
}

static uint32_t np_line_start(uint32_t p) {
    while (p > 0 && np_text[p - 1] != '\n') p--;
    return p;
}

static uint32_t np_line_end(uint32_t p) {
    while (p < np_len && np_text[p] != '\n') p++;
    return p;
}

static void np_move(uint32_t pos, int extend) {
    if (pos > np_len) pos = np_len;
    if (extend) {
        if (np_sel < 0) np_sel = (int)np_cursor;
    } else {
        np_sel = -1;
    }
    np_cursor = pos;
}

/* -------------------------------------------------------------------------
 * SOMA_PARCHE_028_UI: geometria, filas visuales y busqueda de la posicion del raton
 * Una sola tabla de filas (np_rows) sirve para dibujar, para situar el cursor y para seleccionar: asi lo que se ve
 * y lo que se pulsa coinciden siempre (antes cada parte calculaba el ajuste de linea por su cuenta).
 * ------------------------------------------------------------------------- */
struct np_geo {
    int32_t x, y, w, h;                 /* ventana (zona de la aplicacion) */
    int32_t crumb_y;                    /* tira con la ruta del archivo */
    int32_t ed_x, ed_y, ed_w, ed_h;     /* editor: margen de numeros + texto + barra de desplazamiento */
    int32_t tx_x, tx_w;                 /* columna 0 del texto y ancho util */
    int32_t sb_x;                       /* barra de desplazamiento */
    int32_t st_y;                       /* barra de estado */
    uint32_t cols;                      /* caracteres por fila */
};

static void np_geo_of(struct np_geo *g, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    g->x = (int32_t)win_x; g->y = (int32_t)win_y; g->w = (int32_t)win_w; g->h = (int32_t)win_h;
    g->crumb_y = g->y + NP_TOOLBAR_H;
    g->ed_x = g->x; g->ed_y = g->crumb_y + NP_CRUMB_H; g->ed_w = g->w;
    g->st_y = g->y + g->h - NP_STATUS_H;
    g->ed_h = g->st_y - g->ed_y; if (g->ed_h < NP_LINE_H) g->ed_h = NP_LINE_H;
    g->tx_x = g->ed_x + NP_GUTTER_W + NP_PAD_X;
    g->sb_x = g->ed_x + g->ed_w - NP_SB_W;
    g->tx_w = g->sb_x - 6 - g->tx_x; if (g->tx_w < NP_CELL_W) g->tx_w = NP_CELL_W;
    g->cols = (uint32_t)(g->tx_w / NP_CELL_W); if (g->cols < 4) g->cols = 4;
    np_cols_last = g->cols;
}

static uint32_t np_rows[NP_MAX_SIZE + 2];   /* inicio (indice en np_text) de cada fila visual */
static uint32_t np_nrows = 1;

static inline uint32_t np_step(uint32_t i)
{
    uint32_t s = (uint32_t)utf8_charlen((uint8_t)np_text[i]);
    if (i + s > np_len) return 1;
    for (uint32_t k = 1; k < s; ++k) if (((uint8_t)np_text[i + k] & 0xC0) != 0x80) return 1;   /* byte suelto (Latin-1) */
    return s;
}
static inline uint32_t np_adv(uint8_t c, uint32_t col) { return c == '\t' ? 4u - (col & 3u) : 1u; }

/* Ajuste de linea por palabras: se parte en el ultimo espacio; una palabra mas larga que la fila se corta. */
static void np_layout(uint32_t cols)
{
    uint32_t n = 0, i = 0, col = 0, brk = 0;
    if (cols < 1) cols = 1;
    np_rows[n++] = 0;
    while (i < np_len) {
        uint8_t c = (uint8_t)np_text[i];
        if (c == '\n') { i++; np_rows[n++] = (uint32_t)i; col = 0; brk = 0; continue; }
        uint32_t a = np_adv(c, col);
        if (col + a > cols && col > 0) {
            uint32_t start = (brk > np_rows[n - 1]) ? brk : i;
            np_rows[n++] = (uint32_t)start;
            col = 0;
            for (uint32_t k = start; k < i; k += np_step(k)) col += np_adv((uint8_t)np_text[k], col);
            brk = 0;
            continue;
        }
        col += a; i += np_step(i);
        if (c == ' ' || c == '\t') brk = i;
    }
    np_nrows = n;
}

static uint32_t np_row_of(uint32_t pos)
{
    uint32_t lo = 0, hi = np_nrows;
    while (hi - lo > 1) { uint32_t mid = (lo + hi) / 2; if (np_rows[mid] <= pos) lo = mid; else hi = mid; }
    return lo;
}
static int np_row_soft(uint32_t r) { return r + 1 < np_nrows && np_rows[r + 1] > 0 && np_text[np_rows[r + 1] - 1] != '\n'; }
/* ultima posicion de cursor de la fila (sin contar el salto de linea) */
static uint32_t np_row_end(uint32_t r)
{
    if (r + 1 >= np_nrows) return np_len;
    uint32_t e = np_rows[r + 1];
    if (e > np_rows[r] && np_text[e - 1] == '\n') e--;
    return e;
}
static uint32_t np_col_of(uint32_t r, uint32_t pos)
{
    uint32_t col = 0;
    for (uint32_t k = np_rows[r]; k < pos && k < np_len; k += np_step(k)) col += np_adv((uint8_t)np_text[k], col);
    return col;
}
static uint32_t np_pos_at_col(uint32_t r, uint32_t want)
{
    uint32_t col = 0, k = np_rows[r], e = np_row_end(r);
    while (k < e) { uint32_t a = np_adv((uint8_t)np_text[k], col); if (col + a > want) break; col += a; k += np_step(k); }
    if (k == e && np_row_soft(r) && e > np_rows[r]) k = utf8_prev(np_text, e);
    return k;
}

static int np_max_scroll(const struct np_geo *g)
{
    int total = (int)np_nrows * NP_LINE_H + 2 * NP_PAD_Y;
    return total > g->ed_h ? total - g->ed_h : 0;
}
static void np_clamp_scroll(const struct np_geo *g)
{
    int m = np_max_scroll(g);
    if (np_scroll > m) np_scroll = m;
    if (np_scroll < 0) np_scroll = 0;
}

/* Del puntero al texto. *caret = posicion de cursor mas cercana; *chr = caracter que hay debajo (o fin de fila). */
static void np_hit(const struct np_geo *g, int32_t mx, int32_t my, uint32_t *caret, uint32_t *chr)
{
    int32_t ry = my - (g->ed_y + NP_PAD_Y) + np_scroll;
    uint32_t r = ry <= 0 ? 0u : (uint32_t)(ry / NP_LINE_H);
    if (r >= np_nrows) { *caret = np_len; *chr = np_len; return; }
    int32_t rel = mx - g->tx_x, cx = 0;
    uint32_t i = np_rows[r], e = np_row_end(r), col = 0;
    while (i < e) {
        uint32_t st = np_step(i), a = np_adv((uint8_t)np_text[i], col);
        int32_t w = (int32_t)a * NP_CELL_W;
        if (rel < cx + w) { *chr = i; *caret = (rel < cx + w / 2) ? i : i + st; goto done; }
        cx += w; col += a; i += st;
    }
    *chr = e; *caret = e;
done:
    if (*caret == e && np_row_soft(r) && e > np_rows[r]) *caret = utf8_prev(np_text, e);   /* fila partida: el cursor se queda en ella */
}

/* clases de caracter para seleccionar por palabras: 0 espacio, 1 palabra, 2 puntuacion */
static int np_cls_at(uint32_t i)
{
    uint8_t c = (uint8_t)np_text[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return 0;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return 1;
    if (c < 0x80) return 2;
    /* signos de apertura y comillas latinas (U+00A1, U+00BF, U+00AB, U+00BB, U+00B7) cuentan como puntuacion */
    uint8_t lead = c, tail = 0;
    if ((c & 0xC0) == 0x80) { if (i == 0) return 1; lead = (uint8_t)np_text[i - 1]; tail = c; }
    else if (i + 1 < np_len) tail = (uint8_t)np_text[i + 1];
    if (lead == 0xC2 && (tail == 0xA1 || tail == 0xBF || tail == 0xAB || tail == 0xBB || tail == 0xB7)) return 2;
    return 1;
}
static void np_word_at(uint32_t p, uint32_t *ws, uint32_t *we)
{
    if (p > np_len) p = np_len;
    if (p >= np_len || np_text[p] == '\n') {
        if (p > 0 && np_text[p - 1] != '\n') p = utf8_prev(np_text, p);
        else { *ws = *we = p; return; }
    }
    int k = np_cls_at(p);
    uint32_t a = p, b = p;
    while (a > 0 && np_text[a - 1] != '\n' && np_cls_at(a - 1) == k) a--;
    while (b < np_len && np_text[b] != '\n' && np_cls_at(b) == k) b++;
    *ws = a; *we = b;
}
static void np_line_at(uint32_t p, uint32_t *ls, uint32_t *le)
{
    if (p > np_len) p = np_len;
    *ls = np_line_start(p);
    uint32_t e = np_line_end(p);
    if (e < np_len) e++;                 /* incluye el salto de linea */
    *le = e;
}

/* Extiende la seleccion hasta el puntero respetando el modo (letras, palabras o lineas) */
static void np_drag_to(const struct np_geo *g, int32_t mx, int32_t my)
{
    uint32_t caret, chr;
    np_hit(g, mx, my, &caret, &chr);
    if (np_sel_mode == 0) {
        np_sel = (int)np_anchor_lo;
        np_cursor = caret;
    } else {
        uint32_t a, b;
        if (np_sel_mode == 1) np_word_at(chr, &a, &b); else np_line_at(chr, &a, &b);
        if (a < np_anchor_lo) { np_sel = (int)np_anchor_hi; np_cursor = a; }
        else { np_sel = (int)np_anchor_lo; np_cursor = b > np_anchor_hi ? b : np_anchor_hi; }
    }
    np_goal_col = -1;
    np_caret_t = timer_get_uptime_ms();
}

/* barra de desplazamiento: devuelve el recorrido util y deja en ty y th el tirador */
static int np_sb_thumb(const struct np_geo *g, int32_t *ty, int32_t *th)
{
    int track = g->ed_h - 8, m = np_max_scroll(g);
    int total = (int)np_nrows * NP_LINE_H + 2 * NP_PAD_Y;
    if (track < 24) track = 24;
    int h = m > 0 ? (int)(((int64_t)track * g->ed_h) / total) : track;
    if (h < 28) h = 28;
    if (h > track) h = track;
    *th = h;
    *ty = g->ed_y + 4 + (m > 0 ? (int)(((int64_t)(track - h) * np_scroll) / m) : 0);
    return track - h;
}
static void np_sb_to(const struct np_geo *g, int32_t my)
{
    int32_t ty, th; int span = np_sb_thumb(g, &ty, &th), m = np_max_scroll(g);
    if (span <= 0 || m <= 0) return;
    int pos = my - np_sb_grab - (g->ed_y + 4);
    if (pos < 0) pos = 0;
    if (pos > span) pos = span;
    np_scroll = (int)(((int64_t)m * pos) / span);
    np_follow = 0;
}

/* -------------------------------------------------------------------------
 * Renderizado de Glifos Acentuados y Caracteres en Español
 * ------------------------------------------------------------------------- */
static void draw_editor_glyph(uint32_t x, uint32_t y, const uint8_t *s, uint32_t len,
                              uint32_t text_y, uint32_t text_h, uint32_t fg, uint32_t bg) {
    if (y + 16 < text_y || y >= text_y + text_h) return;

    char base = '?';
    enum { DIA_NONE = 0, DIA_ACUTE, DIA_TILDE, DIA_UMLAUT, DIA_INV_EXCL, DIA_INV_QUES } dia = DIA_NONE;

    if (len == 1) {
        uint8_t c = s[0];
        if (c >= 32 && c <= 126) {
            base = (char)c;
        } else {
            /* Mapeo Latin-1 y CP437 sin duplicados numéricos */
            switch (c) {
            case 0xE1: case 160: base = 'a'; dia = DIA_ACUTE; break; /* á */
            case 0xE9: case 130: base = 'e'; dia = DIA_ACUTE; break; /* é */
            case 0xED:           base = 'i'; dia = DIA_ACUTE; break; /* í */
            case 0xF3: case 162: base = 'o'; dia = DIA_ACUTE; break; /* ó */
            case 0xFA: case 163: base = 'u'; dia = DIA_ACUTE; break; /* ú */
            case 0xC1: case 181: base = 'A'; dia = DIA_ACUTE; break; /* Á */
            case 0xC9: case 144: base = 'E'; dia = DIA_ACUTE; break; /* É */
            case 0xCD: case 214: base = 'I'; dia = DIA_ACUTE; break; /* Í */
            case 0xD3: case 224: base = 'O'; dia = DIA_ACUTE; break; /* Ó */
            case 0xDA:           base = 'U'; dia = DIA_ACUTE; break; /* Ú */
            case 0xF1: case 164: base = 'n'; dia = DIA_TILDE; break; /* ñ */
            case 0xD1: case 165: base = 'N'; dia = DIA_TILDE; break; /* Ñ */
            case 0xFC: case 129: base = 'u'; dia = DIA_UMLAUT; break;/* ü */
            case 0xDC: case 154: base = 'U'; dia = DIA_UMLAUT; break;/* Ü */
            case 0xBF: case 168: dia = DIA_INV_QUES; break;          /* ¿ */
            case 0xA1: case 173: dia = DIA_INV_EXCL; break;          /* ¡ */
            default:   base = ' '; break;
            }
        }
    } else if (len == 2) {
        uint32_t cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        switch (cp) {
        case 0xE1: base = 'a'; dia = DIA_ACUTE; break; /* á */
        case 0xE9: base = 'e'; dia = DIA_ACUTE; break; /* é */
        case 0xED: base = 'i'; dia = DIA_ACUTE; break; /* í */
        case 0xF3: base = 'o'; dia = DIA_ACUTE; break; /* ó */
        case 0xFA: base = 'u'; dia = DIA_ACUTE; break; /* ú */
        case 0xC1: base = 'A'; dia = DIA_ACUTE; break; /* Á */
        case 0xC9: base = 'E'; dia = DIA_ACUTE; break; /* É */
        case 0xCD: base = 'I'; dia = DIA_ACUTE; break; /* Í */
        case 0xD3: base = 'O'; dia = DIA_ACUTE; break; /* Ó */
        case 0xDA: base = 'U'; dia = DIA_ACUTE; break; /* Ú */
        case 0xF1: base = 'n'; dia = DIA_TILDE; break; /* ñ */
        case 0xD1: base = 'N'; dia = DIA_TILDE; break; /* Ñ */
        case 0xFC: base = 'u'; dia = DIA_UMLAUT; break; /* ü */
        case 0xDC: base = 'U'; dia = DIA_UMLAUT; break; /* Ü */
        case 0xBF: dia = DIA_INV_QUES; break;          /* ¿ */
        case 0xA1: dia = DIA_INV_EXCL; break;          /* ¡ */
        default:   base = '?'; break;
        }
    }

    if (dia == DIA_INV_QUES) {
        font_draw_string(x, y, "?", fg, 1);
        return;
    }
    if (dia == DIA_INV_EXCL) {
        font_draw_string(x, y, "!", fg, 1);
        return;
    }

    char ch_s[2] = { base, '\0' };
    font_draw_string(x, y, ch_s, fg, 1);

    if (dia == DIA_ACUTE) {
        if (base == 'i') {
            fb_fill_rect(x + 2, y + 2, 4, 3, bg);
            fb_draw_line(x + 3, y + 2, x + 5, y, fg);
            fb_draw_line(x + 4, y + 2, x + 6, y, fg);
        } else if (base >= 'a' && base <= 'z') {
            fb_draw_line(x + 3, y + 2, x + 5, y, fg);
            fb_draw_line(x + 4, y + 2, x + 6, y, fg);
        } else {
            fb_draw_line(x + 3, y, x + 5, y - 2, fg);
            fb_draw_line(x + 4, y, x + 6, y - 2, fg);
        }
    } else if (dia == DIA_TILDE) {
        uint32_t ty = (base == 'n') ? (y + 1) : (y - 1);
        fb_draw_line(x + 1, ty + 1, x + 3, ty, fg);
        fb_draw_line(x + 3, ty, x + 5, ty + 1, fg);
        fb_draw_line(x + 5, ty + 1, x + 7, ty, fg);
    } else if (dia == DIA_UMLAUT) {
        uint32_t uy = (base == 'u') ? (y + 1) : (y - 1);
        fb_fill_rect(x + 2, uy, 2, 2, fg);
        fb_fill_rect(x + 5, uy, 2, 2, fg);
    }
}

/* -------------------------------------------------------------------------
 * Renderizado de Diálogos y Menús
 * ------------------------------------------------------------------------- */
static void render_file_dialog(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h) {
    fb_fill_rect_alpha((int32_t)win_x, (int32_t)win_y, (int32_t)win_w, (int32_t)win_h, 0xB4060910);   /* SOMA_PARCHE_028_UI: velo translucido */

    uint32_t dw = 560, dh = 360;
    uint32_t dx = win_x + (win_w > dw ? (win_w - dw) / 2 : 10);
    uint32_t dy = win_y + (win_h > dh ? (win_h - dh) / 2 : 10);

    fb_draw_shadow((int32_t)dx, (int32_t)dy, (int32_t)dw, (int32_t)dh, 8, 22, 8, 0xB0000000u);
    fb_fill_rounded_rect(dx, dy, dw, dh, 8, 0x00131722);
    fb_draw_rounded_rect(dx, dy, dw, dh, 8, 0x00334155);

    /* Barra de título del modal */
    fb_fill_rounded_rect(dx, dy, dw, 34, 8, 0x001A2030);
    fb_draw_line(dx, dy + 34, dx + dw, dy + 34, 0x002D3748);
    const char *title = (dlg_mode == NP_DLG_OPEN) ? "ABRIR ARCHIVO" : "GUARDAR ARCHIVO COMO";
    font_draw_string(dx + 16, dy + 9, title, 0x0038BDF8, 1);
    font_draw_string(dx + dw - 24, dy + 9, "X", 0x0094A3B8, 1);

    /* Panel Izquierdo: LUGARES */
    uint32_t pw = 128;
    fb_fill_rect(dx + 2, dy + 35, pw, dh - 37 - 52, 0x000F121C);
    fb_draw_line(dx + pw + 2, dy + 35, dx + pw + 2, dy + dh - 52, 0x00262E3D);

    font_draw_string(dx + 12, dy + 44, "LUGARES", 0x0064748B, 1);
    int is_doc = str_eq(dlg_folder, "/documentos");
    int is_ws  = str_eq(dlg_folder, "/workspace");
    int is_mf  = str_eq(dlg_folder, "/mis_archivos");
    int is_rt  = str_eq(dlg_folder, "/");

    /* 1. Documentos (por defecto) */
    if (is_doc) {
        fb_fill_rounded_rect(dx + 6, dy + 62, pw - 8, 24, 4, 0x001D2D44);
        fb_fill_rect(dx + 6, dy + 64, 3, 20, 0x0038BDF8);
    }
    font_draw_string(dx + 14, dy + 67, "Documentos", is_doc ? 0x0038BDF8 : 0x00CBD5E1, 1);

    /* 2. Workspace */
    if (is_ws) {
        fb_fill_rounded_rect(dx + 6, dy + 90, pw - 8, 24, 4, 0x001D2D44);
        fb_fill_rect(dx + 6, dy + 92, 3, 20, 0x0038BDF8);
    }
    font_draw_string(dx + 14, dy + 95, "Workspace", is_ws ? 0x0038BDF8 : 0x00CBD5E1, 1);

    /* 3. Mis Archivos */
    if (is_mf) {
        fb_fill_rounded_rect(dx + 6, dy + 118, pw - 8, 24, 4, 0x001D2D44);
        fb_fill_rect(dx + 6, dy + 120, 3, 20, 0x0038BDF8);
    }
    font_draw_string(dx + 14, dy + 123, "Mis Archivos", is_mf ? 0x0038BDF8 : 0x00CBD5E1, 1);

    /* 4. Raíz */
    if (is_rt) {
        fb_fill_rounded_rect(dx + 6, dy + 146, pw - 8, 24, 4, 0x001D2D44);
        fb_fill_rect(dx + 6, dy + 148, 3, 20, 0x0038BDF8);
    }
    font_draw_string(dx + 14, dy + 151, "Raiz ( / )", is_rt ? 0x0038BDF8 : 0x00CBD5E1, 1);

    /* Panel Derecho: Lista de Archivos */
    uint32_t fx = dx + pw + 10;
    uint32_t fw = dw - pw - 20;

    /* Barra de carpeta actual */
    fb_fill_rounded_rect(fx, dy + 42, fw, 26, 4, 0x000B0E16);
    fb_draw_rounded_rect(fx, dy + 42, fw, 26, 4, 0x00242E40);
    font_draw_string(fx + 8, dy + 47, "Ruta:", 0x0064748B, 1);
    font_draw_string(fx + 52, dy + 47, dlg_folder, 0x00F8FAFC, 1);

    uint32_t list_y = dy + 74;
    uint32_t list_h = 176;
    fb_fill_rect(fx, list_y, fw, list_h, 0x000A0D15);
    fb_draw_rect(fx, list_y, fw, list_h, 0x001E2738);

    /* Filas de archivos (vis_rows = 8 filas de 22px) */
    uint32_t vis_rows = list_h / 22;
    for (uint32_t r = 0; r < vis_rows; ++r) {
        int idx = dlg_scroll + (int)r;
        if (idx >= dlg_item_count) break;
        uint32_t ry = list_y + r * 22;
        int is_sel = (idx == dlg_sel_idx);

        if (is_sel) {
            fb_fill_rect(fx + 1, ry, fw - 16, 22, 0x001D4ED8);
        } else if (r & 1) {
            fb_fill_rect(fx + 1, ry, fw - 16, 22, 0x000E121E);
        }

        font_draw_string(fx + 8, ry + 4, dlg_items[idx].is_dir ? "[DIR]" : "[TXT]",
                         dlg_items[idx].is_dir ? 0x00F59E0B : 0x0038BDF8, 1);
        font_draw_string(fx + 58, ry + 4, dlg_items[idx].name, is_sel ? 0x00FFFFFF : 0x00E2E8F0, 1);
    }

    /* BARRA DE DESPLAZAMIENTO VISUAL (Scrollbar) EN EL MODAL */
    uint32_t sb_x = fx + fw - 11;
    uint32_t sb_y = list_y + 3;
    uint32_t sb_w = 6;
    uint32_t sb_h = list_h - 6;

    fb_fill_rounded_rect(sb_x, sb_y, sb_w, sb_h, 3, 0x000F131C);
    if (dlg_item_count > (int)vis_rows) {
        uint32_t thumb_h = (vis_rows * sb_h) / (uint32_t)dlg_item_count;
        if (thumb_h < 18) thumb_h = 18;
        if (thumb_h > sb_h) thumb_h = sb_h;
        uint32_t max_s = (uint32_t)(dlg_item_count - (int)vis_rows);
        uint32_t thumb_y = sb_y + ((uint32_t)dlg_scroll * (sb_h - thumb_h)) / max_s;
        fb_fill_rounded_rect(sb_x, thumb_y, sb_w, thumb_h, 3, 0x0038BDF8);
    } else {
        fb_fill_rounded_rect(sb_x, sb_y, sb_w, sb_h, 3, 0x00242E40);
    }

    /* Pie: Entrada de Nombre con botón [ ✕ ] y cursor parpadeante */
    uint32_t by = dy + dh - 52;
    fb_draw_line(dx, by - 4, dx + dw, by - 4, 0x00232B3A);

    font_draw_string(dx + 16, by + 12, "Archivo:", 0x0094A3B8, 1);

    uint32_t in_box_x = dx + 84;
    uint32_t in_box_y = by + 8;
    uint32_t in_box_w = 230;
    uint32_t in_box_h = 26;

    fb_fill_rounded_rect(in_box_x, in_box_y, in_box_w, in_box_h, 4, 0x00090C14);
    fb_draw_rounded_rect(in_box_x, in_box_y, in_box_w, in_box_h, 4, 0x0038BDF8);

    /* Texto y cursor de edición */
    font_draw_string(in_box_x + 8, in_box_y + 5, dlg_filename, 0x00FFFFFF, 1);
    uint32_t caret_x = in_box_x + 8 + dlg_filename_len * 8;
    if (caret_x < in_box_x + in_box_w - 22) {
        if (((timer_get_uptime_ms() / 400) & 1) == 0) {
            fb_fill_rect(caret_x, in_box_y + 4, 2, 16, 0x0038BDF8);
        }
    }

    /* Botón [ ✕ ] para vaciar el nombre con un clic */
    font_draw_string(in_box_x + in_box_w - 18, in_box_y + 5, "x", 0x0064748B, 1);

    /* Botones de acción */
    uint32_t btn_main_x = dx + dw - 225;
    fb_fill_rounded_rect(btn_main_x, by + 8, 105, 26, 4, 0x000284C7);
    fb_draw_rounded_rect(btn_main_x, by + 8, 105, 26, 4, 0x0038BDF8);
    font_draw_string(btn_main_x + (dlg_mode == NP_DLG_OPEN ? 28 : 20), by + 13,
                     dlg_mode == NP_DLG_OPEN ? "Abrir" : "Guardar", 0x00FFFFFF, 1);

    uint32_t btn_can_x = dx + dw - 110;
    fb_fill_rounded_rect(btn_can_x, by + 8, 95, 26, 4, 0x00222738);
    fb_draw_rounded_rect(btn_can_x, by + 8, 95, 26, 4, 0x003B445B);
    font_draw_string(btn_can_x + 16, by + 13, "Cancelar", 0x00E2E8F0, 1);
}

/* -------------------------------------------------------------------------
 * SOMA_PARCHE_028_UI: texto de la interfaz (proporcional y suavizado; si no hay fuente, rejilla de 8 px)
 * ------------------------------------------------------------------------- */
static int np_wf = -1;
static int np_has_wf(void) { if (np_wf < 0) np_wf = wf_init() ? 1 : 0; return np_wf; }
static int np_tw(const char *s, int px)
{
    uint32_t n = str_len(s);
    if (np_has_wf()) return wf_width(s, n, px, 0);
    int cells = 0;
    for (uint32_t i = 0; i < n; ++i) if (((uint8_t)s[i] & 0xC0) != 0x80) cells++;
    return cells * 8;
}
/* texto centrado en vertical dentro de la franja [top, top+h) */
static void np_tx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold)
{
    uint32_t n = str_len(s);
    if (!n) return;
    if (np_has_wf()) { wf_draw(x, top + (h + (px * 72) / 100) / 2, s, n, px, col, bold ? WF_BOLD : 0, 255); return; }
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), s, col, 1);
}
/* un caracter del editor (monoespaciado) */
static void np_draw_cp(int32_t x, int32_t y, const uint8_t *s, uint32_t len, uint32_t fg, uint32_t bg, const struct np_geo *g)
{
    if (font_vec_available()) {
        uint32_t cp = s[0];
        if (len == 2) cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3Fu);
        else if (len == 3) cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
        else if (len == 4) cp = '?';
        if (cp > 32 && cp != 127) { int ff = settings_editor_face(); font_vec_draw_face(ff, x, y, cp, fg, font_vec_em(ff, NP_EM), NP_CELL_W, NP_BASE); }   /* SOMA_184: fuente del editor (Ajustes > Fuentes) */
        return;
    }
    draw_editor_glyph((uint32_t)x, (uint32_t)(y + 3), s, len, (uint32_t)g->ed_y, (uint32_t)g->ed_h, fg, bg);
}

/* -------------------------------------------------------------------------
 * Barra de herramientas (una sola tabla para dibujar y para pulsar)
 * ------------------------------------------------------------------------- */
enum { NB_NEW = 0, NB_OPEN, NB_SAVE, NB_SAVEAS, NB_CUT, NB_COPY, NB_PASTE, NB_SPEAK, NB_BROWSER, NB_COUNT };
static const char *const np_btn_label[NB_COUNT] = { "Nuevo", "Abrir", "Guardar", "Guardar como", "Cortar", "Copiar", "Pegar", "Leer en voz alta", "Ver en navegador" };
/* SOMA_247_HABLA: lectura en voz alta (texto a voz con una voz de Piper; solo x86_64) */
#if defined(__x86_64__)
#include "voz_sys.h"
static int np_speaking(void) { return voz_speak_state(); }
#else
static int np_speaking(void) { return 0; }
#endif
static const char *np_label(int i) { return (i == NB_SPEAK && np_speaking()) ? "Parar lectura" : np_btn_label[i]; }
struct np_geo;
struct np_rect;
static void np_speak_rect(const struct np_geo *g, struct np_rect *r);
struct np_rect { int32_t x, y, w, h; };

static void np_btn_rects(const struct np_geo *g, struct np_rect *r)
{
    int32_t x = g->x + 10, y = g->y + 6, h = NP_TOOLBAR_H - 12, right = g->x + g->w - 8;
    for (int i = 0; i < NB_COUNT; ++i) {
        if (i == NB_SPEAK) { r[i].x = r[i].w = 0; r[i].y = y; r[i].h = h; continue; }   /* va en la tira de la ruta (siempre cabe) */
        if (i == NB_CUT || i == NB_BROWSER) x += 11;                 /* separador de grupo */
        int32_t w = np_tw(np_btn_label[i], 12) + 22;
        r[i].y = y; r[i].h = h;
        if (x + w > right) { r[i].x = 0; r[i].w = 0; x = right + 1; continue; }   /* no cabe: se oculta (sigue en el menu) */
        r[i].x = x; r[i].w = w;
        x += w + 4;
    }
}
__attribute__((unused)) static void np_speak_rect(const struct np_geo *g, struct np_rect *r)   /* SOMA_247_HABLA: a la derecha de la ruta */
{
    int32_t w = np_tw("Leer en voz alta", 11) + 46;
    r->h = NP_CRUMB_H - 6; r->y = g->crumb_y + 3; r->w = w; r->x = g->x + g->w - w - 8;
}
static int np_btn_hit(const struct np_geo *g, int32_t mx, int32_t my)
{
    struct np_rect r[NB_COUNT];
    if (my < g->y || my >= g->y + NP_TOOLBAR_H) return -1;
    np_btn_rects(g, r);
    for (int i = 0; i < NB_COUNT; ++i)
        if (r[i].w > 0 && mx >= r[i].x && mx < r[i].x + r[i].w && my >= r[i].y && my < r[i].y + r[i].h) return i;
    return -1;
}

/* -------------------------------------------------------------------------
 * Menu contextual
 * ------------------------------------------------------------------------- */
static int np_ctx_height(void)
{
    int h = 2 * CTX_PAD;
    for (int i = 0; i < CTX_N; ++i) h += np_menu[i].id < 0 ? CTX_SEP_H : CTX_ITEM_H;
    return h;
}
static int np_menu_enabled(int id)
{
    switch (id) {
    case MI_CUT: case MI_COPY: case MI_DEL: return np_has_sel();
    case MI_PASTE: { uint32_t n = 0; const char *t = clip_text(&n); return t && n > 0; }
    case MI_ALL: case MI_LINE: return np_len > 0;
    default: return 1;
    }
}
/* indice del elemento bajo el puntero (-1 = ninguno o separador) */
static int np_menu_hit(int32_t mx, int32_t my)
{
    if (!np_ctx_open || mx < np_ctx_x || mx >= np_ctx_x + CTX_W) return -1;
    int32_t y = np_ctx_y + CTX_PAD;
    for (int i = 0; i < CTX_N; ++i) {
        int h = np_menu[i].id < 0 ? CTX_SEP_H : CTX_ITEM_H;
        if (my >= y && my < y + h) return np_menu[i].id < 0 ? -1 : i;
        y += h;
    }
    return -1;
}
static int np_menu_inside(int32_t mx, int32_t my)
{
    return np_ctx_open && mx >= np_ctx_x && mx < np_ctx_x + CTX_W && my >= np_ctx_y && my < np_ctx_y + np_ctx_height();
}

static void render_context_menu(void)
{
    if (!np_ctx_open) return;
    int32_t mx = np_ctx_x, my = np_ctx_y, mw = CTX_W, mh = np_ctx_height();
    fb_draw_shadow(mx, my, mw, mh, 9, 18, 6, 0xB0000000u);
    fb_fill_rounded_rect(mx, my, mw, mh, 9, 0x001A2030);
    fb_stroke_rrect_aa(mx, my, mw, mh, 9, 0xFF3A465Cu);

    int32_t y = my + CTX_PAD;
    for (int i = 0; i < CTX_N; ++i) {
        const struct np_mi *it = &np_menu[i];
        if (it->id < 0) {
            fb_fill_rect((uint32_t)(mx + 10), (uint32_t)(y + CTX_SEP_H / 2), (uint32_t)(mw - 20), 1, 0x002E3A4E);
            y += CTX_SEP_H;
            continue;
        }
        int en = np_menu_enabled(it->id), hot = (i == np_ctx_hover) && en;
        if (hot) fb_fill_rounded_rect(mx + 5, y, mw - 10, CTX_ITEM_H, 6, 0x000B6BCB);
        uint32_t col = !en ? 0x0056627A : (hot ? 0x00FFFFFF : 0x00E2E8F0);
        uint32_t kcol = !en ? 0x00434E63 : (hot ? 0x00D6E9FF : 0x007B8AA3);
        np_tx(mx + 16, y, CTX_ITEM_H, it->label, 13, col, 0);
        if (it->key) np_tx(mx + mw - 16 - np_tw(it->key, 12), y, CTX_ITEM_H, it->key, 12, kcol, 0);
        y += CTX_ITEM_H;
    }
}

/* -------------------------------------------------------------------------
 * Ventana principal
 * ------------------------------------------------------------------------- */
static void np_chevron(int32_t x, int32_t cy, uint32_t argb)
{
    fb_line_aa(x * 16, (cy - 4) * 16, (x + 4) * 16, cy * 16, 20, argb);
    fb_line_aa((x + 4) * 16, cy * 16, x * 16, (cy + 4) * 16, 20, argb);
}

void notepad_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor) {
    struct np_geo g;
    np_geo_of(&g, win_x, win_y, win_w, win_h);
    np_layout(g.cols);
    uint64_t now = timer_get_uptime_ms();

    /* Arrastre: si el boton ya no esta pulsado se da por terminado; fuera del editor, desplazamiento automatico */
    if ((np_dragging || np_sb_drag) && !mouse_left_held()) {
        np_dragging = 0; np_sb_drag = 0;
        if (np_sel == (int)np_cursor) np_sel = -1;
    }
    if (np_dragging && np_my >= 0 && dlg_mode == NP_DLG_NONE) {
        int step = 0;
        if (np_my < g.ed_y) step = -(1 + (g.ed_y - np_my) / 24);
        else if (np_my >= g.ed_y + g.ed_h) step = 1 + (np_my - (g.ed_y + g.ed_h)) / 24;
        if (step) {
            if (step > 6) step = 6;
            if (step < -6) step = -6;
            np_scroll += step * NP_LINE_H;
            np_clamp_scroll(&g);
            np_drag_to(&g, np_mx, np_my);
            np_follow = 0;
        }
    }

    /* El cursor de texto siempre a la vista mientras se escribe */
    uint32_t cur = np_cursor > np_len ? np_len : np_cursor;
    uint32_t cur_row = np_row_of(cur);
    if (np_follow) {
        int cy = (int)cur_row * NP_LINE_H, view_h = g.ed_h - 2 * NP_PAD_Y;
        if (view_h < NP_LINE_H) view_h = NP_LINE_H;
        if (cy < np_scroll) np_scroll = cy;
        else if (cy + NP_LINE_H > np_scroll + view_h) np_scroll = cy + NP_LINE_H - view_h;
    }
    np_clamp_scroll(&g);

    /* 1. Barra de herramientas */
    fb_fill_rect(win_x, win_y, win_w, NP_TOOLBAR_H, NC_BAR);
    {
        struct np_rect r[NB_COUNT];
        np_btn_rects(&g, r);
        for (int i = 0; i < NB_COUNT; ++i) {
            if (r[i].w <= 0) continue;
            if (i == NB_CUT || i == NB_BROWSER) fb_fill_rect((uint32_t)(r[i].x - 8), (uint32_t)(r[i].y + 6), 1, (uint32_t)(r[i].h - 12), 0x002B3547);
            int hot = (i == np_hover_btn) && !np_ctx_open && dlg_mode == NP_DLG_NONE;
            int primary = (i == NB_SAVE && np_dirty);
            uint32_t fg = 0x00CBD5E1;
            if (primary) { fb_fill_rounded_rect(r[i].x, r[i].y, r[i].w, r[i].h, 7, hot ? 0x000E7FD6 : 0x000B6BCB); fg = 0x00FFFFFF; }
            else if (hot) { fb_fill_rounded_rect(r[i].x, r[i].y, r[i].w, r[i].h, 7, NC_HOVER); fg = 0x00FFFFFF; }
            if (i == NB_BROWSER && !primary) fg = hot ? 0x00FFFFFF : NC_ACCENT;
            np_tx(r[i].x + 11, r[i].y, r[i].h, np_label(i), 12, fg, 0);
        }
    }

    /* 2. Ruta del archivo */
    fb_fill_rect(win_x, (uint32_t)g.crumb_y, win_w, NP_CRUMB_H, NC_BAR2);
    fb_fill_rect(win_x, (uint32_t)g.crumb_y, win_w, 1, NC_LINE);
    fb_fill_rect(win_x, (uint32_t)(g.crumb_y + NP_CRUMB_H - 1), win_w, 1, NC_LINE);
    {
        fb_set_clip(g.x, g.crumb_y, g.w, NP_CRUMB_H);
        int32_t cx = g.x + 14, cy = g.crumb_y + NP_CRUMB_H / 2;
        const char *p = np_path;
        char seg[64];
        while (*p) {
            while (*p == '/') p++;
            if (!*p) break;
            uint32_t k = 0;
            while (*p && *p != '/') { if (k + 1 < sizeof(seg)) seg[k++] = *p; p++; }
            seg[k] = '\0';
            int last = (*p == '\0');
            np_tx(cx, g.crumb_y, NP_CRUMB_H, seg, 12, last ? 0x00E2E8F0 : 0x0073819A, 0);
            cx += np_tw(seg, 12) + 8;
            if (!last) { np_chevron(cx, cy, 0xFF4B5873u); cx += 12; }
            else if (np_dirty) { fb_fill_rrect_aa(cx, cy - 4, 8, 8, 4, 0xFFF59E0Bu); cx += 14; np_tx(cx, g.crumb_y, NP_CRUMB_H, "sin guardar", 11, 0x00A8823E, 0); }
        }
        fb_reset_clip();
#if defined(__x86_64__)
        {   /* SOMA_247_HABLA: "Leer en voz alta" / "Parar lectura", a la derecha de la ruta */
            struct np_rect b; np_speak_rect(&g, &b);
            int on = np_speaking(), hot = np_mx >= b.x && np_mx < b.x + b.w && np_my >= b.y && np_my < b.y + b.h;
            fb_fill_rounded_rect(b.x, b.y, b.w, b.h, 9, on ? (hot ? 0x00B91C1C : 0x007F1D1D) : (hot ? NC_HOVER : 0x00182133));
            int32_t ix = b.x + 10, iy = b.y + b.h / 2;                     /* altavoz */
            uint32_t ic = on ? 0xFFFFFFFFu : 0xFF7DD3FCu;
            fb_fill_rrect_aa(ix, iy - 3, 4, 6, 1, ic);
            for (int k = 0; k < 4; k++) fb_fill_rect((uint32_t)(ix + 4 + k), (uint32_t)(iy - 2 - k), 1, (uint32_t)(4 + 2 * k), ic & 0xFFFFFF);
            if (on) { fb_fill_rect((uint32_t)(ix + 11), (uint32_t)(iy - 3), 2, 6, 0x00FFFFFF); fb_fill_rect((uint32_t)(ix + 14), (uint32_t)(iy - 3), 2, 6, 0x00FFFFFF); }
            else for (int k = 0; k < 2; k++) fb_stroke_rrect_aa(ix + 6 + 3 * k, iy - 3 - 2 * k, 6 + 2 * k, 6 + 4 * k, 3 + k, (ic & 0x00FFFFFFu) | 0x90000000u);
            np_tx(b.x + 34, b.y, b.h, np_label(NB_SPEAK), 11, on ? 0x00FFFFFF : 0x00BAE6FD, 0);
        }
#endif
    }

    /* 3. Editor */
    fb_fill_rect((uint32_t)g.ed_x, (uint32_t)g.ed_y, (uint32_t)g.ed_w, (uint32_t)g.ed_h, NC_BG);
    fb_fill_rect((uint32_t)g.ed_x, (uint32_t)g.ed_y, NP_GUTTER_W, (uint32_t)g.ed_h, NC_GUTTER);
    fb_fill_rect((uint32_t)(g.ed_x + NP_GUTTER_W), (uint32_t)g.ed_y, 1, (uint32_t)g.ed_h, 0x00161C28);

    uint32_t sel_lo, sel_hi;
    np_sel_range(&sel_lo, &sel_hi);
    int has_sel = sel_hi > sel_lo;
    int32_t top = g.ed_y + NP_PAD_Y - np_scroll;
    uint32_t first = np_scroll > NP_PAD_Y ? (uint32_t)((np_scroll - NP_PAD_Y) / NP_LINE_H) : 0;
    uint32_t line_no = 1;
    for (uint32_t k = 0; k < np_rows[first] && k < np_len; ++k) if (np_text[k] == '\n') line_no++;
    uint32_t cur_line_start = np_line_start(cur);

    fb_set_clip(g.ed_x, g.ed_y, g.ed_w - NP_SB_W, g.ed_h);
    for (uint32_t r = first; r < np_nrows; ++r) {
        int32_t y = top + (int32_t)r * NP_LINE_H;
        if (y >= g.ed_y + g.ed_h) break;
        uint32_t rs = np_rows[r], re = np_row_end(r);
        int line_first = (r == 0) || np_text[rs - 1] == '\n';
        int in_cur_line = (rs >= cur_line_start && rs <= np_line_end(cur));

        /* linea activa (solo sin seleccion, como en los editores habituales) */
        if (in_cur_line && !has_sel && show_cursor)
            fb_fill_rect((uint32_t)(g.ed_x + NP_GUTTER_W + 1), (uint32_t)y, (uint32_t)(g.ed_w - NP_GUTTER_W - 1), NP_LINE_H, NC_CURLINE);

        /* numero de linea */
        if (line_first) {
            char nb[12]; u32_to_str(line_no, nb);
            uint32_t nl = str_len(nb);
            uint32_t ncol = in_cur_line ? 0x00C8D3E5 : NC_GUT_TXT;
            int32_t nx = g.ed_x + NP_GUTTER_W - 10 - (int32_t)nl * 7;
            for (uint32_t k = 0; k < nl; ++k) {
                if (font_vec_available()) font_vec_draw(nx + (int32_t)k * 7, y, (uint32_t)nb[k], ncol, 12, 7, NP_BASE);
                else { char one[2] = { nb[k], 0 }; font_draw_string((uint32_t)(nx + (int32_t)k * 7), (uint32_t)(y + 4), one, ncol, 1); }
            }
        }
        if (r + 1 < np_nrows && np_rows[r + 1] > 0 && np_text[np_rows[r + 1] - 1] == '\n') line_no++;

        /* seleccion: un tramo continuo por fila */
        {
            uint32_t rnext = (r + 1 < np_nrows) ? np_rows[r + 1] : np_len;
            if (has_sel && sel_lo < rnext && sel_hi > rs) {
                uint32_t a = sel_lo > rs ? sel_lo : rs, b = sel_hi < re ? sel_hi : re;
                int32_t x0 = g.tx_x + (int32_t)np_col_of(r, a) * NP_CELL_W;
                int32_t x1 = g.tx_x + (int32_t)np_col_of(r, b) * NP_CELL_W;
                if (sel_hi > re && rnext > re) x1 += NP_CELL_W - 2;      /* el salto de linea tambien esta seleccionado */
                if (x1 > x0) fb_fill_rounded_rect(x0, y, x1 - x0, NP_LINE_H, 2, NC_SEL);
            }
        }

        /* texto */
        uint32_t col = 0;
        for (uint32_t i = rs; i < re; ) {
            uint32_t st = np_step(i), a = np_adv((uint8_t)np_text[i], col);
            int is_sel = (i >= sel_lo && i < sel_hi);
            np_draw_cp(g.tx_x + (int32_t)col * NP_CELL_W, y, (const uint8_t *)&np_text[i], st,
                       is_sel ? 0x00FFFFFF : NC_TXT, is_sel ? NC_SEL : NC_BG, &g);
            col += a; i += st;
        }

        /* cursor de texto */
        if (r == cur_row && show_cursor && !np_ctx_open && dlg_mode == NP_DLG_NONE && (((now - np_caret_t) / 530u) & 1u) == 0u) {
            int32_t cx = g.tx_x + (int32_t)np_col_of(r, cur) * NP_CELL_W;
            fb_fill_rect((uint32_t)(cx - 1), (uint32_t)(y + 1), 2, NP_LINE_H - 2, NC_ACCENT);
        }
    }
    fb_reset_clip();

    /* barra de desplazamiento */
    {
        int32_t ty, th;
        np_sb_thumb(&g, &ty, &th);
        if (np_max_scroll(&g) > 0)
            fb_fill_rrect_aa(g.sb_x + 3, ty, NP_SB_W - 6, th, 3, np_sb_drag ? 0xFF38BDF8u : 0xFF3A465Cu);
    }

    /* 4. Barra de estado */
    fb_fill_rect(win_x, (uint32_t)g.st_y, win_w, NP_STATUS_H, NC_BAR);
    fb_fill_rect(win_x, (uint32_t)g.st_y, win_w, 1, NC_LINE);
    {
        uint32_t led = np_dirty ? 0xFFF59E0Bu : 0xFF10B981u;
        if (np_live) led = 0xFF38BDF8u;
        fb_fill_rrect_aa(g.x + 14, g.st_y + NP_STATUS_H / 2 - 4, 8, 8, 4, led);

        /* derecha: linea, columna y tamano */
        char info[96]; char nb[12];
        uint32_t ln = 1, colc = 1;
        for (uint32_t k = 0; k < cur; ++k) if (np_text[k] == '\n') ln++;
        for (uint32_t k = cur_line_start; k < cur; k += np_step(k)) colc++;
        info[0] = '\0';
        if (has_sel) {
            uint32_t chars = 0;
            for (uint32_t k = sel_lo; k < sel_hi; k += np_step(k)) chars++;
            u32_to_str(chars, nb); ap(info, sizeof(info), nb); ap(info, sizeof(info), chars == 1 ? " seleccionado   " : " seleccionados   ");
        }
        ap(info, sizeof(info), "L\xC3\xADn "); u32_to_str(ln, nb); ap(info, sizeof(info), nb);
        ap(info, sizeof(info), ", Col "); u32_to_str(colc, nb); ap(info, sizeof(info), nb);
        ap(info, sizeof(info), "   "); u32_to_str(np_len, nb); ap(info, sizeof(info), nb); ap(info, sizeof(info), " B");
        int iw = np_tw(info, 12);
        int32_t ix = g.x + g.w - 14 - iw;
        if (ix > g.x + 120) np_tx(ix, g.st_y, NP_STATUS_H, info, 12, NC_DIM, 0);
        else ix = g.x + g.w;

        char full_status[80];
        str_copy(full_status, np_status, sizeof(full_status));
        if (np_dead_key == 1) str_copy(full_status, "Acento activo (pulsa vocal)", sizeof(full_status));
        fb_set_clip(g.x, g.st_y, ix - 12 - g.x, NP_STATUS_H);
        np_tx(g.x + 30, g.st_y, NP_STATUS_H, full_status, 12, NC_DIM, 0);
        fb_reset_clip();
    }

    /* 5. Menu contextual y dialogos modales */
    render_context_menu();
    if (dlg_mode != NP_DLG_NONE) {
        render_file_dialog(win_x, win_y, win_w, win_h);
    }
}

/* -------------------------------------------------------------------------
 * Control de Eventos de Ratón
 * ------------------------------------------------------------------------- */
static void np_select_all(void) { np_sel = 0; np_cursor = np_len; np_goal_col = -1; }

/* SOMA_247_HABLA: Leer en voz alta / Parar lectura. Lee la seleccion si la hay; si no, todo el documento. */
static void np_speak_toggle(void)
{
#if defined(__x86_64__)
    if (voz_speak_state()) { voz_speak_stop(); clip_toast("Lectura detenida"); return; }
    uint32_t lo = 0, hi = np_len;
    if (np_has_sel()) np_sel_range(&lo, &hi);
    uint32_t k = lo; while (k < hi && (np_text[k] == ' ' || np_text[k] == '\n' || np_text[k] == '\t' || np_text[k] == '\r')) k++;
    if (k >= hi) { clip_toast("No hay texto que leer"); return; }
    int r = voz_speak_start(np_text + lo, hi - lo);
    clip_toast(r == 0 ? (np_has_sel() ? "Leyendo la seleccion en voz alta..." : "Leyendo en voz alta...") : "No se pudo empezar a leer");
#endif
}

static void np_btn_exec(int b)
{
    switch (b) {
    case NB_NEW:    notepad_init(); break;
    case NB_OPEN:   dlg_mode = NP_DLG_OPEN; dlg_open_documents_folder(); break;
    case NB_SAVE:   notepad_save(); break;
    case NB_SAVEAS:
        dlg_mode = NP_DLG_SAVE_AS;
        str_copy(dlg_filename, get_filename_part(np_path), sizeof(dlg_filename));
        dlg_filename_len = str_len(dlg_filename);
        dlg_open_documents_folder();
        break;
    case NB_CUT:    notepad_clip_op(CLIP_OP_CUT); break;
    case NB_COPY:   notepad_clip_op(CLIP_OP_COPY); break;
    case NB_PASTE:  notepad_clip_op(CLIP_OP_PASTE); break;
    case NB_BROWSER: notepad_open_in_browser(np_path); break;
    case NB_SPEAK:  np_speak_toggle(); break;
    default: break;
    }
}

static void np_menu_exec(int id)
{
    switch (id) {
    case MI_CUT:   notepad_clip_op(CLIP_OP_CUT); break;
    case MI_COPY:  notepad_clip_op(CLIP_OP_COPY); break;
    case MI_PASTE: notepad_clip_op(CLIP_OP_PASTE); break;
    case MI_DEL:   if (np_has_sel()) { uint32_t lo, hi; np_sel_range(&lo, &hi); np_delete_range(lo, hi); np_follow = 1; } break;
    case MI_ALL:   np_select_all(); break;
    case MI_LINE:  { uint32_t a, b; np_line_at(np_cursor, &a, &b); np_sel = (int)a; np_cursor = b; np_follow = 1; } break;
    case MI_OPEN:  dlg_mode = NP_DLG_OPEN; dlg_open_documents_folder(); break;
    case MI_SAVE:  notepad_save(); break;
    case MI_BROWSER: notepad_open_in_browser(np_path); break;
    case MI_SPEAK: np_speak_toggle(); break;
    default: break;
    }
}

int notepad_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h) {
    uint64_t now = timer_get_uptime_ms();
    np_mx = mx; np_my = my;
    np_caret_t = now;

    if (np_swallow_click) { np_swallow_click = 0; return 1; }

    if (np_ctx_open) {
        int item = np_menu_hit(mx, my);
        int inside = np_menu_inside(mx, my);
        if (inside && (item < 0 || !np_menu_enabled(np_menu[item].id))) return 1;      /* separador o elemento apagado */
        np_ctx_open = 0; np_ctx_hover = -1;
        if (item >= 0) np_menu_exec(np_menu[item].id);
        return 1;
    }

    if (dlg_mode != NP_DLG_NONE) {
        uint32_t dw = 560, dh = 360;
        uint32_t dx = win_x + (win_w > dw ? (win_w - dw) / 2 : 10);
        uint32_t dy = win_y + (win_h > dh ? (win_h - dh) / 2 : 10);

        /* Botón cerrar [X] o Cancelar */
        if ((mx >= (int32_t)(dx + dw - 30) && mx <= (int32_t)(dx + dw - 10) && my >= (int32_t)(dy + 4) && my <= (int32_t)(dy + 26)) ||
            (mx >= (int32_t)(dx + dw - 110) && mx <= (int32_t)(dx + dw - 15) && my >= (int32_t)(dy + dh - 44) && my <= (int32_t)(dy + dh - 18))) {
            dlg_mode = NP_DLG_NONE;
            return 1;
        }

        /* Panel Lugares: Documentos, Workspace, Mis Archivos, Raíz */
        uint32_t pw = 128;
        if (mx >= (int32_t)(dx + 6) && mx <= (int32_t)(dx + pw)) {
            if (my >= (int32_t)(dy + 62) && my <= (int32_t)(dy + 86)) { dlg_open_folder("/documentos"); return 1; }
            if (my >= (int32_t)(dy + 90) && my <= (int32_t)(dy + 114)) { dlg_open_folder("/workspace"); return 1; }
            if (my >= (int32_t)(dy + 118) && my <= (int32_t)(dy + 142)) { dlg_open_folder("/mis_archivos"); return 1; }
            if (my >= (int32_t)(dy + 146) && my <= (int32_t)(dy + 170)) { dlg_open_folder("/"); return 1; }
        }

        /* Subir nivel al hacer clic en la barra de ruta */
        uint32_t fx = dx + pw + 10;
        uint32_t fw = dw - pw - 20;
        if (mx >= (int32_t)fx && mx <= (int32_t)(fx + fw) && my >= (int32_t)(dy + 42) && my <= (int32_t)(dy + 68)) {
            dlg_go_up();
            return 1;
        }

        uint32_t list_y = dy + 74;
        uint32_t list_h = 176;

        /* Clic en Scrollbar del modal */
        uint32_t sb_x = fx + fw - 16;
        if (mx >= (int32_t)sb_x && mx <= (int32_t)(fx + fw) && my >= (int32_t)list_y && my <= (int32_t)(list_y + list_h)) {
            uint32_t vis_rows = list_h / 22;
            if (dlg_item_count > (int)vis_rows) {
                int max_s = dlg_item_count - (int)vis_rows;
                int32_t rel = my - (int32_t)list_y;
                if (rel < 0) rel = 0;
                dlg_scroll = (rel * max_s) / (int32_t)list_h;
                if (dlg_scroll > max_s) dlg_scroll = max_s;
            }
            return 1;
        }

        /* Clic en lista de archivos */
        if (mx >= (int32_t)fx && mx < (int32_t)sb_x && my >= (int32_t)list_y && my <= (int32_t)(list_y + list_h)) {
            int row = (my - (int32_t)list_y) / 22;
            int idx = dlg_scroll + row;
            if (idx >= 0 && idx < dlg_item_count) {
                uint64_t now = timer_get_uptime_ms();
                int is_dbl = (dlg_sel_idx == idx && (now - dlg_last_click < 400));
                dlg_last_click = now;
                dlg_sel_idx = idx;

                if (dlg_items[idx].is_dir) {
                    if (is_dbl) {
                        if (str_eq(dlg_items[idx].name, "..")) dlg_go_up();
                        else {
                            char sub[128];
                            path_join(dlg_folder, dlg_items[idx].name, sub, sizeof(sub));
                            dlg_open_folder(sub);
                        }
                    }
                } else {
                    str_copy(dlg_filename, dlg_items[idx].name, sizeof(dlg_filename));
                    dlg_filename_len = str_len(dlg_filename);
                    if (is_dbl) {
                        char target[128];
                        path_join(dlg_folder, dlg_filename, target, sizeof(target));
                        if (dlg_mode == NP_DLG_OPEN) notepad_load_file(target);
                        else { np_named = 1, str_copy(np_path, target, sizeof(np_path)); notepad_save(); }
                        dlg_mode = NP_DLG_NONE;
                    }
                }
            }
            return 1;
        }

        /* Clic dentro de la caja de texto del nombre de archivo (o botón [x] de borrado) */
        uint32_t in_box_x = dx + 84;
        uint32_t in_box_y = dy + dh - 52 + 8;
        uint32_t in_box_w = 230;
        uint32_t in_box_h = 26;

        if (mx >= (int32_t)in_box_x && mx <= (int32_t)(in_box_x + in_box_w) &&
            my >= (int32_t)in_box_y && my <= (int32_t)(in_box_y + in_box_h)) {
            /* Si pulsa sobre la [x] en el borde derecho, vacía el nombre */
            if (mx >= (int32_t)(in_box_x + in_box_w - 22)) {
                dlg_filename[0] = '\0';
                dlg_filename_len = 0;
            }
            return 1;
        }

        /* Botón de acción (Abrir / Guardar) */
        uint32_t btn_main_x = dx + dw - 225;
        if (mx >= (int32_t)btn_main_x && mx <= (int32_t)(btn_main_x + 105) &&
            my >= (int32_t)(dy + dh - 44) && my <= (int32_t)(dy + dh - 18)) {
            char target[128];
            char final_name[64];
            str_copy(final_name, dlg_filename, sizeof(final_name));

            /* Añadir extensión .txt automáticamente si no se ha introducido ninguna */
            if (dlg_mode == NP_DLG_SAVE_AS && dlg_filename_len > 0) {
                int has_dot = 0;
                for (uint32_t k = 0; k < dlg_filename_len; ++k) {
                    if (dlg_filename[k] == '.') { has_dot = 1; break; }
                }
                if (!has_dot) ap(final_name, sizeof(final_name), ".txt");
            }

            path_join(dlg_folder, final_name, target, sizeof(target));
            if (dlg_mode == NP_DLG_OPEN) notepad_load_file(target);
            else { np_named = 1, str_copy(np_path, target, sizeof(np_path)); notepad_save(); }
            dlg_mode = NP_DLG_NONE;
            return 1;
        }
        return 1;
    }

    struct np_geo g;
    np_geo_of(&g, win_x, win_y, win_w, win_h);
    np_layout(g.cols);
    np_clamp_scroll(&g);

#if defined(__x86_64__)
    {   /* SOMA_247_HABLA: boton de la tira de la ruta */
        struct np_rect b; np_speak_rect(&g, &b);
        if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) { np_speak_toggle(); return 1; }
    }
#endif
    /* Barra de herramientas */
    {
        int b = np_btn_hit(&g, mx, my);
        if (b >= 0) { np_btn_exec(b); return 1; }
    }
    if (my < g.ed_y || my >= g.ed_y + g.ed_h || mx < g.ed_x || mx >= g.ed_x + g.ed_w) return 0;

    /* Barra de desplazamiento: arrastrar el tirador o saltar una pagina */
    if (mx >= g.sb_x - 2) {
        int32_t ty, th;
        np_sb_thumb(&g, &ty, &th);
        if (np_max_scroll(&g) > 0) {
            if (my >= ty && my < ty + th) { np_sb_drag = 1; np_sb_grab = my - ty; }
            else {
                int page = g.ed_h - 2 * NP_LINE_H;
                np_scroll += (my < ty) ? -page : page;
                np_clamp_scroll(&g);
            }
            np_follow = 0;
        }
        return 1;
    }

    /* Texto: un clic situa el cursor, dos seleccionan la palabra, tres la linea; arrastrar extiende la seleccion */
    uint32_t caret, chr;
    np_hit(&g, mx, my, &caret, &chr);
    int32_t ddx = mx - np_last_click_x, ddy = my - np_last_click_y;
    if (now - np_last_click_time < 450 && ddx > -5 && ddx < 5 && ddy > -5 && ddy < 5) np_click_count = (np_click_count % 3) + 1;
    else np_click_count = 1;
    np_last_click_time = now; np_last_click_x = mx; np_last_click_y = my;
    np_goal_col = -1;
    np_follow = 1;

    if (mx < g.ed_x + NP_GUTTER_W) {                    /* margen de numeros: linea entera */
        uint32_t a, b; np_line_at(chr, &a, &b);
        np_sel = (int)a; np_cursor = b; np_anchor_lo = a; np_anchor_hi = b; np_sel_mode = 2; np_dragging = 1;
        return 1;
    }
    if (keyboard_shift_held() && np_click_count == 1) {  /* Mayus + clic: extiende desde el ancla actual */
        if (np_sel < 0) np_sel = (int)np_cursor;
        np_cursor = caret;
        np_anchor_lo = np_anchor_hi = (uint32_t)np_sel; np_sel_mode = 0; np_dragging = 1;
        return 1;
    }
    if (np_click_count == 2) {
        uint32_t a, b; np_word_at(chr, &a, &b);
        np_sel = (int)a; np_cursor = b; np_anchor_lo = a; np_anchor_hi = b; np_sel_mode = 1;
    } else if (np_click_count == 3) {
        uint32_t a, b; np_line_at(chr, &a, &b);
        np_sel = (int)a; np_cursor = b; np_anchor_lo = a; np_anchor_hi = b; np_sel_mode = 2;
    } else {
        np_cursor = caret; np_sel = -1; np_anchor_lo = np_anchor_hi = caret; np_sel_mode = 0;
    }
    np_dragging = 1;
    return 1;
}

/* Abre el menu de edicion justo bajo el puntero (se voltea si no cabe y nunca sale de la ventana) */
int notepad_handle_right_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h) {
    if (dlg_mode != NP_DLG_NONE) return 1;
    struct np_geo g;
    np_geo_of(&g, win_x, win_y, win_w, win_h);
    np_layout(g.cols);
    np_dragging = 0; np_sb_drag = 0;

    /* clic derecho sobre el texto y fuera de la seleccion: el cursor va al punto pulsado (como en cualquier editor) */
    if (mx >= g.ed_x + NP_GUTTER_W && mx < g.sb_x && my >= g.ed_y && my < g.ed_y + g.ed_h) {
        uint32_t caret, chr, lo, hi;
        np_hit(&g, mx, my, &caret, &chr);
        np_sel_range(&lo, &hi);
        if (!(hi > lo && chr >= lo && chr < hi)) { np_cursor = caret; np_sel = -1; np_goal_col = -1; }
    }

    int32_t mh = np_ctx_height();
    int32_t x = mx + 1, y = my + 1;
    int32_t x_min = (int32_t)win_x + 4, x_max = (int32_t)(win_x + win_w) - CTX_W - 4;
    int32_t y_min = (int32_t)win_y + 4, y_max = (int32_t)(win_y + win_h) - mh - 12;
    if (x > x_max) x = mx - CTX_W;                       /* no cabe a la derecha: se abre hacia la izquierda */
    if (y > y_max) y = my - mh;                          /* no cabe hacia abajo: se abre hacia arriba */
    if (x > x_max) x = x_max;
    if (x < x_min) x = x_min;
    if (y > y_max) y = y_max;
    if (y < y_min) y = y_min;
    np_ctx_x = x; np_ctx_y = y;
    np_ctx_open = 1;
    np_ctx_hover = np_menu_hit(mx, my);
    np_caret_t = timer_get_uptime_ms();
    return 1;
}

/* SOMA_PARCHE_028_UI: movimiento, arrastre, rueda y boton derecho. Lo llama la GUI con cada evento del raton y con
 * las coordenadas reales del puntero. Devuelve 1 si hay que repintar la ventana. */
int notepad_mouse(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h,
                  int wheel, int left_held, int right_click) {
    struct np_geo g;
    np_geo_of(&g, win_x, win_y, win_w, win_h);
    np_layout(g.cols);
    int redraw = 0;
    int moved = (mx != np_mx || my != np_my);
    int press = left_held && !np_prev_left, release = !left_held && np_prev_left;
    int inside = (mx >= g.x && mx < g.x + g.w && my >= g.y && my < g.y + g.h);
    np_mx = mx; np_my = my; np_prev_left = left_held;
    if (!left_held) np_swallow_click = 0;

    if (dlg_mode != NP_DLG_NONE) {
        np_dragging = 0; np_sb_drag = 0;
        if (wheel && inside) {
            int vis = 176 / 22, max_s = dlg_item_count - vis;
            dlg_scroll -= wheel;
            if (dlg_scroll > max_s) dlg_scroll = max_s;
            if (dlg_scroll < 0) dlg_scroll = 0;
            redraw = 1;
        }
        return redraw;
    }

    if (np_ctx_open) {
        int h = np_menu_hit(mx, my);
        if (h != np_ctx_hover) { np_ctx_hover = h; redraw = 1; }
        if (press && !np_menu_inside(mx, my)) {            /* clic fuera del menu: se cierra y ese clic no hace nada mas */
            np_ctx_open = 0; np_ctx_hover = -1; redraw = 1;
            if (inside) np_swallow_click = 1;
        }
        if (!right_click) return redraw;
    }
    if (right_click) {
        if (!inside) { if (np_ctx_open) { np_ctx_open = 0; return 1; } return redraw; }
        notepad_handle_right_click(mx, my, win_x, win_y, win_w, win_h);
        return 1;
    }

    if (release) {
        if (np_dragging || np_sb_drag) redraw = 1;
        np_dragging = 0; np_sb_drag = 0;
        if (np_sel == (int)np_cursor) np_sel = -1;
    }
    if (left_held && np_sb_drag) { np_sb_to(&g, my); np_clamp_scroll(&g); return 1; }
    if (left_held && np_dragging && moved) { np_clamp_scroll(&g); np_drag_to(&g, mx, my); redraw = 1; }

    if (wheel && inside) {
        np_scroll -= wheel * 3 * NP_LINE_H;
        np_clamp_scroll(&g);
        np_follow = 0;
        if (left_held && np_dragging) np_drag_to(&g, mx, my);
        redraw = 1;
    }

    int hb = (inside && !left_held) ? np_btn_hit(&g, mx, my) : -1;
    if (hb != np_hover_btn) { np_hover_btn = hb; redraw = 1; }
    return redraw;
}

/* Esc: cierra el menu o el dialogo abiertos. Devuelve 1 si lo ha usado (la GUI no debe salir entonces). */
int notepad_escape(void) {
    if (np_ctx_open) { np_ctx_open = 0; np_ctx_hover = -1; return 1; }
    if (dlg_mode != NP_DLG_NONE) { dlg_mode = NP_DLG_NONE; return 1; }
    return 0;
}

/* Compatibilidad con la interfaz anterior */
int notepad_handle_drag(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h) {
    if (dlg_mode != NP_DLG_NONE || !np_dragging) return 0;
    struct np_geo g;
    np_geo_of(&g, win_x, win_y, win_w, win_h);
    np_layout(g.cols);
    np_mx = mx; np_my = my;
    np_drag_to(&g, mx, my);
    return 1;
}

int notepad_handle_mouse_up(int32_t mx, int32_t my) {
    (void)mx; (void)my;
    np_dragging = 0; np_sb_drag = 0;
    if (np_sel == (int)np_cursor) np_sel = -1;
    return 1;
}

/* -------------------------------------------------------------------------
 * Control de Teclado con Acentos en Español (á, é, í, ó, ú, ñ)
 * ------------------------------------------------------------------------- */
void notepad_handle_key(char c) {
    /* Edición de texto activa dentro del Diálogo de Archivos */
    if (dlg_mode != NP_DLG_NONE) {
        if (c == 27) { dlg_mode = NP_DLG_NONE; return; }
        if (c == '\r' || c == '\n') {
            char target[128];
            char final_name[64];
            str_copy(final_name, dlg_filename, sizeof(final_name));

            if (dlg_mode == NP_DLG_SAVE_AS && dlg_filename_len > 0) {
                int has_dot = 0;
                for (uint32_t k = 0; k < dlg_filename_len; ++k) {
                    if (dlg_filename[k] == '.') { has_dot = 1; break; }
                }
                if (!has_dot) ap(final_name, sizeof(final_name), ".txt");
            }

            path_join(dlg_folder, final_name, target, sizeof(target));
            if (dlg_mode == NP_DLG_OPEN) notepad_load_file(target);
            else { np_named = 1, str_copy(np_path, target, sizeof(np_path)); notepad_save(); }
            dlg_mode = NP_DLG_NONE;
            return;
        }

        /* Borrar caracteres (Backspace, DEL y Supr) */
        if (c == '\b' || (uint8_t)c == 127 || c == 4) {
            if (dlg_filename_len > 0) {
                dlg_filename[--dlg_filename_len] = '\0';
            }
            return;
        }

        /* Ctrl+A dentro del modal: vaciar para escribir de nuevo */
        if (c == 1) {
            dlg_filename[0] = '\0';
            dlg_filename_len = 0;
            return;
        }

        /* Escritura de caracteres en el nombre de archivo */
        if ((uint8_t)c >= 32 && (uint8_t)c <= 126 && c != '/' && c != '\\' && dlg_filename_len < sizeof(dlg_filename) - 5) {
            dlg_filename[dlg_filename_len++] = c;
            dlg_filename[dlg_filename_len] = '\0';
            return;
        }
        return;
    }

    int shift = keyboard_shift_held();
    int ctrl = keyboard_ctrl_held();
    np_follow = 1;
    np_caret_t = timer_get_uptime_ms();
    if (np_ctx_open) { np_ctx_open = 0; np_ctx_hover = -1; if (c == 27) return; }   /* SOMA_PARCHE_028_UI */
    if (c != 16 && c != 14) np_goal_col = -1;

    if (np_cursor > np_len) np_cursor = np_len;

    /* Atajos universales con Ctrl */
    if (ctrl) {
        np_dead_key = 0;
        np_utf8_lead = 0;
        if (c == 's' || c == 'S' || c == 19) { notepad_save(); return; }
        if (c == 'o' || c == 'O' || c == 15) { dlg_mode = NP_DLG_OPEN; dlg_open_documents_folder(); return; }
        if (c == 'b' || c == 'B') { notepad_open_in_browser(np_path); return; }
        if (c == 'c' || c == 'C' || c == 3)  { notepad_clip_op(CLIP_OP_COPY); return; }
        if (c == 'x' || c == 'X' || c == 24) { notepad_clip_op(CLIP_OP_CUT); return; }
        if (c == 'v' || c == 'V' || c == 22) { notepad_clip_op(CLIP_OP_PASTE); return; }
        if (c == 'a' || c == 'A' || c == 1)  { np_sel = 0; np_cursor = np_len; np_follow = 0; return; }
    }

    /* Ensamblador de secuencias multibyte UTF-8 nativas */
    if (np_utf8_lead != 0) {
        if (((uint8_t)c & 0xC0) == 0x80) {
            char u2[3] = { (char)np_utf8_lead, c, '\0' };
            np_utf8_lead = 0;
            np_insert_bytes(u2, 2);
            return;
        }
        np_utf8_lead = 0;
    }
    if ((uint8_t)c >= 0xC0 && (uint8_t)c <= 0xF7) {
        np_utf8_lead = (uint8_t)c;
        return;
    }

    /* Conversión de bytes Latin-1 directos */
    if ((uint8_t)c == 0xE1) { np_insert_bytes("\xC3\xA1", 2); return; } /* á */
    if ((uint8_t)c == 0xE9) { np_insert_bytes("\xC3\xA9", 2); return; } /* é */
    if ((uint8_t)c == 0xED) { np_insert_bytes("\xC3\xAD", 2); return; } /* í */
    if ((uint8_t)c == 0xF3) { np_insert_bytes("\xC3\xB3", 2); return; } /* ó */
    if ((uint8_t)c == 0xFA) { np_insert_bytes("\xC3\xBA", 2); return; } /* ú */
    if ((uint8_t)c == 0xC1) { np_insert_bytes("\xC3\x81", 2); return; } /* Á */
    if ((uint8_t)c == 0xC9) { np_insert_bytes("\xC3\x89", 2); return; } /* É */
    if ((uint8_t)c == 0xCD) { np_insert_bytes("\xC3\x8D", 2); return; } /* Í */
    if ((uint8_t)c == 0xD3) { np_insert_bytes("\xC3\x93", 2); return; } /* Ó */
    if ((uint8_t)c == 0xDA) { np_insert_bytes("\xC3\x9A", 2); return; } /* Ú */
    if ((uint8_t)c == 0xFC) { np_insert_bytes("\xC3\xBC", 2); return; } /* ü */
    if ((uint8_t)c == 0xDC) { np_insert_bytes("\xC3\x9C", 2); return; } /* Ü */

    /* MÁQUINA DE ESTADOS: TECLAS MUERTAS PARA ACENTOS EN ESPAÑOL */
    if (np_dead_key == 1) {
        np_dead_key = 0;
        if (c == 'a') { np_insert_bytes("\xC3\xA1", 2); return; }
        if (c == 'e') { np_insert_bytes("\xC3\xA9", 2); return; }
        if (c == 'i') { np_insert_bytes("\xC3\xAD", 2); return; }
        if (c == 'o') { np_insert_bytes("\xC3\xB3", 2); return; }
        if (c == 'u') { np_insert_bytes("\xC3\xBA", 2); return; }
        if (c == 'A') { np_insert_bytes("\xC3\x81", 2); return; }
        if (c == 'E') { np_insert_bytes("\xC3\x89", 2); return; }
        if (c == 'I') { np_insert_bytes("\xC3\x8D", 2); return; }
        if (c == 'O') { np_insert_bytes("\xC3\x93", 2); return; }
        if (c == 'U') { np_insert_bytes("\xC3\x9A", 2); return; }
        if (c == ' ') { np_insert_bytes("'", 1); return; }
        if (c == 0x27 || (uint8_t)c == 0xB4) { np_insert_bytes("''", 2); return; }
        np_insert_bytes("'", 1);
    } else if (np_dead_key == 2) {
        np_dead_key = 0;
        if (c == 'n') { np_insert_bytes("\xC3\xB1", 2); return; }
        if (c == 'N') { np_insert_bytes("\xC3\x91", 2); return; }
        if (c == ' ') { np_insert_bytes("~", 1); return; }
        np_insert_bytes("~", 1);
    } else if (np_dead_key == 3) {
        np_dead_key = 0;
        if (c == 'u') { np_insert_bytes("\xC3\xBC", 2); return; }
        if (c == 'U') { np_insert_bytes("\xC3\x9C", 2); return; }
        if (c == ' ') { np_insert_bytes("\"", 1); return; }
        np_insert_bytes("\"", 1);
    }

    /* Tecla de acento agudo */
    if ((uint8_t)c == 0xB4 || c == 0x27 || c == 0x60) {
        np_dead_key = 1;
        str_copy(np_status, "Acento activo (pulsa vocal)", sizeof(np_status));
        return;
    }
    if ((uint8_t)c == 0xA8 || c == '"') {
        np_dead_key = 3;
        return;
    }
    if (c == '~') {
        np_dead_key = 2;
        return;
    }

    /* Caracteres en español nativos */
    if ((uint8_t)c == 0xF1 || (uint8_t)c == 164) { np_insert_bytes("\xC3\xB1", 2); return; } /* ñ */
    if ((uint8_t)c == 0xD1 || (uint8_t)c == 165) { np_insert_bytes("\xC3\x91", 2); return; } /* Ñ */
    if ((uint8_t)c == 0xBF || (uint8_t)c == 168) { np_insert_bytes("\xC2\xBF", 2); return; } /* ¿ */
    if ((uint8_t)c == 0xA1 || (uint8_t)c == 173) { np_insert_bytes("\xC2\xA1", 2); return; } /* ¡ */

    /* Teclas de Navegación */
    if (c == 2) {
        uint32_t lo, hi; np_sel_range(&lo, &hi);
        if (!shift && hi > lo) np_move(lo, 0);
        else np_move(utf8_prev(np_text, np_cursor), shift);
    } else if (c == 6) {
        uint32_t lo, hi; np_sel_range(&lo, &hi);
        if (!shift && hi > lo) np_move(hi, 0);
        else np_move(utf8_next(np_text, np_cursor, np_len), shift);
    } else if (c == 16 || c == 14) {
        /* SOMA_PARCHE_028_UI: arriba / abajo por filas visuales (las que se ven), conservando la columna */
        np_layout(np_cols_last ? np_cols_last : 80u);
        uint32_t r = np_row_of(np_cursor);
        int keep = np_goal_col >= 0 ? np_goal_col : (int)np_col_of(r, np_cursor);
        if (c == 16) np_move(r > 0 ? np_pos_at_col(r - 1, (uint32_t)keep) : 0, shift);
        else np_move(r + 1 < np_nrows ? np_pos_at_col(r + 1, (uint32_t)keep) : np_len, shift);
        np_goal_col = keep;
    } else if (c == 1) {
        np_move(np_line_start(np_cursor), shift);
    } else if (c == 5) {
        np_move(np_line_end(np_cursor), shift);
    } else if (c == 4) {
        if (np_has_sel()) {
            uint32_t lo, hi; np_sel_range(&lo, &hi);
            np_delete_range(lo, hi);
        } else if (np_cursor < np_len) {
            uint32_t next = utf8_next(np_text, np_cursor, np_len);
            np_delete_range(np_cursor, next);
        }
    } else if (c == '\b' || c == 127) {
        if (np_has_sel()) {
            uint32_t lo, hi; np_sel_range(&lo, &hi);
            np_delete_range(lo, hi);
        } else if (np_cursor > 0) {
            uint32_t prev = utf8_prev(np_text, np_cursor);
            np_delete_range(prev, np_cursor);
        }
    } else if (c == '\t') {
        np_insert_bytes("    ", 4);
    } else if (c == '\r' || c == '\n') {
        uint32_t ls = np_line_start(np_cursor);
        uint32_t sp = 0;
        while (ls + sp < np_cursor && np_text[ls + sp] == ' ' && sp < 32) sp++;
        char indent[34]; indent[0] = '\n';
        for (uint32_t k = 0; k < sp; ++k) indent[1 + k] = ' ';
        indent[1 + sp] = '\0';
        np_insert_bytes(indent, 1 + sp);
    } else if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
        np_insert_bytes(&c, 1);
    }
}

/* -------------------------------------------------------------------------
 * Portapapeles
 * ------------------------------------------------------------------------- */
int notepad_clip_op(int op) {
    char msg[64];
    if (op == CLIP_OP_PASTE) {
        uint32_t n = 0;
        const char *t = clip_text(&n);
        if (!t || n == 0) { clip_toast("Portapapeles vacio"); return 1; }
        uint32_t room = NP_MAX_SIZE - 2 - np_len;
        if (np_has_sel()) { uint32_t lo, hi; np_sel_range(&lo, &hi); room += hi - lo; }
        static char tmp[NP_MAX_SIZE]; uint32_t k = 0;

        for (uint32_t i = 0; i < n && k < room && k < sizeof(tmp) - 1; ++i) {
            char ch = t[i];
            if (ch == '\r') continue;
            if (ch == '\t') ch = ' ';
            if ((uint8_t)ch < 32 && ch != '\n') continue;
            tmp[k++] = ch;
        }
        np_insert_bytes(tmp, k);
        np_follow = 1;
        clip_status_text(msg, sizeof msg, "Pegado: ", k);
        str_copy(np_status, msg, sizeof(np_status));
        clip_toast(msg);
        return 1;
    }

    uint32_t lo, hi;
    if (np_has_sel()) np_sel_range(&lo, &hi);
    else if (op == CLIP_OP_COPY) { lo = 0; hi = np_len; }
    else { lo = np_line_start(np_cursor); hi = np_line_end(np_cursor); if (hi < np_len) hi++; }

    if (hi <= lo) { clip_toast("Nada que copiar"); return 1; }
    clip_set_text(np_text + lo, hi - lo);
    if (op == CLIP_OP_CUT) { np_delete_range(lo, hi); np_follow = 1; }
    clip_status_text(msg, sizeof msg, op == CLIP_OP_CUT ? "Cortado: " : "Copiado: ", hi - lo);
    str_copy(np_status, msg, sizeof(np_status));
    clip_toast(msg);
    return 1;
}

/* -------------------------------------------------------------------------
 * Live Hook (IA SOMA)
 * ------------------------------------------------------------------------- */
void notepad_live_begin(const char *path, const char *data, uint32_t len) {
    if (!path) path = "/workspace/notas.txt";
    if (!data) { data = ""; len = 0; }
    if (len > NP_MAX_SIZE - 2) len = NP_MAX_SIZE - 2;

    np_named = 1, str_copy(np_path, path, sizeof(np_path));
    np_len = 0;
    np_text[0] = '\0';
    np_cursor = 0;
    np_live_src = data;
    np_live_len = len;
    np_live_pos = 0;
    np_live = 1;
    np_follow = 1;
    np_dirty = 0;
    str_copy(np_status, "SOMA escribiendo en vivo...", sizeof(np_status));
}

uint32_t notepad_live_step(uint32_t chunk) {
    if (!np_live || !np_live_src) return 0;
    if (chunk == 0) chunk = 1;

    while (chunk > 0 && np_live_pos < np_live_len && np_len < NP_MAX_SIZE - 2) {
        char ch = np_live_src[np_live_pos++];
        chunk--;
        if (ch == '\r') continue;
        np_text[np_len++] = ch;
    }
    np_text[np_len] = '\0';
    np_cursor = np_len;
    np_follow = 1;

    if (np_len >= NP_MAX_SIZE - 2) np_live_pos = np_live_len;
    return np_live_len - np_live_pos;
}

void notepad_live_end(void) {
    np_live = 0;
    np_live_src = 0;
    np_cursor = np_len;
    np_dirty = 0;
    str_copy(np_status, "Archivo generado por SOMA", sizeof(np_status));
}



#undef np_text
#undef np_len
#undef np_cursor
#undef np_path
#undef np_scroll
#undef np_status
#undef np_dirty
#undef np_sel
#undef np_dragging
#undef np_sel_mode
#undef np_anchor_lo
#undef np_anchor_hi
#undef np_click_count
#undef np_last_click_time
#undef np_last_click_x
#undef np_last_click_y
#undef np_swallow_click
#undef np_sb_drag
#undef np_sb_grab
#undef np_caret_t
#undef np_goal_col
#undef np_dead_key
#undef np_utf8_lead
#undef np_follow
#undef np_live
#undef np_live_src
#undef np_live_len
#undef np_live_pos
#undef np_ctx_open
#undef np_ctx_x
#undef np_ctx_y
#undef np_ctx_hover
#undef dlg_mode
#undef dlg_folder
#undef dlg_filename
#undef dlg_filename_len
#undef dlg_items
#undef dlg_item_count
#undef dlg_sel_idx
#undef dlg_scroll
#undef dlg_last_click
#undef np_cols_last
#undef np_named
/* SOMA_211_NOTAS: instancias */
static void np_inst_defaults(struct np_inst *p)
{
    memset(p, 0, sizeof *p);
    str_copy(p->np_path, "/workspace/notas.txt", sizeof p->np_path);
    str_copy(p->np_status, "Nuevo documento", sizeof p->np_status);
    p->np_sel = -1; p->np_goal_col = -1; p->np_ctx_hover = -1;
    p->dlg_mode = NP_DLG_NONE;
    str_copy(p->dlg_folder, "/documentos", sizeof p->dlg_folder);
    str_copy(p->dlg_filename, "notas.txt", sizeof p->dlg_filename);
    p->dlg_filename_len = 9; p->dlg_sel_idx = -1;
}
static int np_inst_used[NP_INST_MAX + 1];
int notepad_inst_new(void)
{
    for (int k = 1; k <= NP_INST_MAX; ++k) {
        if (np_inst_used[k]) continue;
        if (!np_insts[k]) {
            np_insts[k] = (struct np_inst *)kmalloc(sizeof(struct np_inst));
            if (!np_insts[k]) return 0;
        }
        np_inst_defaults(np_insts[k]);
        np_inst_used[k] = 1;
        NP = np_insts[k];
        return k;
    }
    return 0;
}
void notepad_select(int slot)
{
    if (slot >= 1 && slot <= NP_INST_MAX && np_insts[slot]) NP = np_insts[slot];
}
void notepad_inst_drop(int slot)
{
    if (slot < 1 || slot > NP_INST_MAX || !np_inst_used[slot]) return;
    np_inst_used[slot] = 0;
    if (NP == np_insts[slot]) NP = &np_inst1;
    if (slot != 1 && np_insts[slot]) { kfree(np_insts[slot]); np_insts[slot] = 0; }
}
int notepad_inst_find(const char *path)
{
    for (int k = 1; k <= NP_INST_MAX; ++k) {
        if (!np_inst_used[k] || !np_insts[k] || !path || !np_insts[k]->np_named) continue;
        const char *a = np_insts[k]->np_path, *b = path;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return k;
    }
    return 0;
}
const char *notepad_inst_name(int slot)
{
    if (slot < 1 || slot > NP_INST_MAX || !np_insts[slot] || !np_insts[slot]->np_named) return "";
    const char *p = np_insts[slot]->np_path, *n = p;
    for (; *p; ++p) if (*p == '/') n = p + 1;
    return n;
}
int notepad_inst_dirty(int slot) { return slot >= 1 && slot <= NP_INST_MAX && np_insts[slot] ? np_insts[slot]->np_dirty : 0; }
