/* SOMA_PARCHE_ARCHIVOS: explorador de archivos profesional con Biblioteca por categorias.
 *
 *  - Biblioteca virtual: Imagenes, Video, Audio, Documentos, Textos, Programas, Web, Comprimidos y Otros. Reune los archivos
 *    de RamFS y del almacen grande BIGFS por extension, sin moverlos de sitio.
 *  - "Organizar": mueve de verdad los archivos sueltos de la carpeta actual a subcarpetas por categoria (con deshacer).
 *  - Navegacion por RamFS y por BIGFS, ruta navegable (migas de pan), busqueda, columnas ordenables, iconos por tipo,
 *    vista previa (texto, BMP, WAV, AVI), apertura segun el tipo y portapapeles (SOMA_PARCHE_PORTAPAPELES, incluido aqui).
 */
#include "fm.h"
#include "fb.h"
#include "gui_font.h"
#include "mouse.h"
#include "console.h"
#include "fs.h"
#include "unidades.h"      /* SOMA_268_ACCESO: /usuario */
#include "agent_policy.h"   /* SOMA_268_ACCESO: usuario_ruta_escribible */
#include "mem.h"
#include "arch.h"
#include "rtc.h"
#include "sysinfo.h"
#include "idt.h"
#include "shell.h"
#include "bigfs_glue.h"
#include "media.h"
#include "gui.h"
#include "web_font.h"     /* SOMA_PARCHE_028_UI: texto suavizado */
#include "web_img.h"      /* SOMA_PARCHE_028_UI: miniaturas PNG/JPEG/GIF/BMP */
#include "fm_edit.h"      /* SOMA_273_ARCHIVOS: edicion de texto de los dialogos */

uint64_t timer_get_uptime_ms(void);
void fm_refresh(void);
int  fm_clip_op(int op);

#define MAX_FM_FILES 256
#define FM_ROW_H     24
#define LIB_MAX      512
#define UNDO_MAX     128
#define FM_PATH      160

/* ---- categorias ---- */
enum { FC_IMG = 0, FC_VID, FC_AUD, FC_DOC, FC_TXT, FC_PRG, FC_WEB, FC_ZIP, FC_OTR, FM_NCAT };

static const char *const cat_name[FM_NCAT] = {
    "Imagenes", "Video", "Audio", "Documentos", "Textos", "Programas", "Web", "Comprimidos", "Otros"
};
static const char *const cat_kind[FM_NCAT] = {
    "Imagen", "Video", "Audio", "Documento", "Texto", "Programa", "Web", "Comprimido", "Archivo"
};
static const uint32_t cat_col[FM_NCAT] = {
    0x00F59E0B, 0x00EF4444, 0x00A78BFA, 0x0038BDF8, 0x00818CF8, 0x00EC4899, 0x0010B981, 0x0094A3B8, 0x0064748B
};

static const struct { const char *e; uint8_t c; } ext_tab[] = {
    {"bmp", FC_IMG}, {"png", FC_IMG}, {"jpg", FC_IMG}, {"jpeg", FC_IMG}, {"gif", FC_IMG}, {"ico", FC_IMG},
    {"webp", FC_IMG}, {"tga", FC_IMG}, {"svg", FC_IMG},
    {"avi", FC_VID}, {"mp4", FC_VID}, {"mkv", FC_VID}, {"mov", FC_VID}, {"webm", FC_VID}, {"mpg", FC_VID}, {"mpeg", FC_VID},
    {"wav", FC_AUD}, {"mp3", FC_AUD}, {"ogg", FC_AUD}, {"flac", FC_AUD}, {"mid", FC_AUD}, {"aac", FC_AUD}, {"m4a", FC_AUD},
    {"pdf", FC_DOC}, {"doc", FC_DOC}, {"docx", FC_DOC}, {"odt", FC_DOC}, {"rtf", FC_DOC}, {"xls", FC_DOC},
    {"xlsx", FC_DOC}, {"ods", FC_DOC}, {"ppt", FC_DOC}, {"pptx", FC_DOC}, {"csv", FC_DOC},
    {"txt", FC_TXT}, {"md", FC_TXT}, {"log", FC_TXT}, {"ini", FC_TXT}, {"cfg", FC_TXT}, {"conf", FC_TXT},
    {"json", FC_TXT}, {"xml", FC_TXT}, {"c", FC_TXT}, {"h", FC_TXT}, {"sh", FC_TXT}, {"py", FC_TXT},
    {"cmd", FC_TXT}, {"tex", FC_DOC}, {"latex", FC_DOC}, {"yaml", FC_TXT}, {"yml", FC_TXT}, {"asm", FC_TXT},
    {"soma", FC_PRG}, {"elf", FC_PRG}, {"exe", FC_PRG}, {"app", FC_PRG},
    {"html", FC_WEB}, {"htm", FC_WEB}, {"css", FC_WEB}, {"js", FC_WEB},
    {"zip", FC_ZIP}, {"tar", FC_ZIP}, {"gz", FC_ZIP}, {"tgz", FC_ZIP}, {"7z", FC_ZIP}, {"rar", FC_ZIP},
    {"xz", FC_ZIP}, {"bz2", FC_ZIP}
};

/* ---- tipos ---- */
enum fm_view_kind { FV_RAM = 0, FV_BIG = 1, FV_LIB = 2, FV_CAT = 3 };
enum { ST_RAM = 0, ST_BIG = 1, ST_VIRT = 2 };

enum fm_modal_mode {
    FM_MODAL_NONE = 0,
    FM_MODAL_NEW_FILE,
    FM_MODAL_NEW_FOLDER,
    FM_MODAL_RENAME,
    FM_MODAL_DELETE_CONFIRM,
    FM_MODAL_ORGANIZE
};

enum fm_sort_mode {
    FM_SORT_NAME_ASC = 0, FM_SORT_NAME_DESC,
    FM_SORT_SIZE_ASC,     FM_SORT_SIZE_DESC,
    FM_SORT_TYPE_ASC,     FM_SORT_TYPE_DESC
};

struct fm_item {
    char     name[64];
    char     full_path[FM_PATH];
    uint64_t size;
    uint8_t  is_dir;
    uint8_t  store;      /* ST_RAM, ST_BIG o ST_VIRT (carpeta de categoria de la Biblioteca) */
    uint8_t  cat;
    uint8_t  pad;
    uint16_t sub_count;
};

struct lib_ent {
    char     path[144];
    uint64_t size;
    uint8_t  store, cat;
};

struct fm_undo { char from[FM_PATH]; char to[FM_PATH]; uint8_t store; };

/* SOMA_214_ARCHIVOS: varias ventanas del explorador. Carpeta, lista, seleccion, orden, vista, filtro, historial, menus y
 * dialogos viven en una instancia por ventana; la Biblioteca, el deshacer y la cache de miniaturas se comparten. */
struct fm_hist { uint8_t view, cat; char path[FM_PATH]; };
struct fm_crumb { int32_t x, w; uint8_t view, cat; const char *label; uint8_t llen; uint16_t plen; };
struct fm_inst {
    struct fm_item file_list[MAX_FM_FILES];
    int file_count, selected_idx, scroll_offset;
    enum fm_sort_mode sort_mode;
    char current_folder[FM_PATH];
    int fm_view, fm_cat;
    char preview_text[256], preview_extra[64];
    uint64_t last_click_time;
    enum fm_modal_mode modal_mode;
    char modal_input[64]; uint32_t modal_input_len; char modal_target[FM_PATH];
    uint32_t modal_caret, modal_s0, modal_s1; char modal_err[72];     /* SOMA_273_ARCHIVOS */
    char fm_filter[24]; uint32_t fm_filter_len; int fm_search_active;
    char fm_msg[112]; uint64_t fm_msg_until; char st_info[160];
    int g_vis_rows;
    int fm_mode, fm_reveal, fm_hover, fm_hover_ui, fm_hover_side, fm_sb_drag, fm_sb_grab;
    int fm_ctx_open, fm_ctx_kind, fm_ctx_hover; int32_t fm_ctx_x, fm_ctx_y;
    int g_vis_cols;
    struct fm_hist fm_histv[16]; int fm_hn, fm_hpos;
    struct fm_crumb crumbs[24]; int ncr, cr_first;
};
#define FM_INST_MAX 8
static void fm_inst_defaults(struct fm_inst *p)
{
    char *b = (char *)p; for (size_t i = 0; i < sizeof *p; ++i) b[i] = 0;
    p->selected_idx = -1; p->sort_mode = FM_SORT_NAME_ASC;
    const char *w = "/usuario"; int k = 0; while (w[k]) { p->current_folder[k] = w[k]; k++; } p->current_folder[k] = 0;
    p->fm_view = FV_RAM; p->modal_mode = FM_MODAL_NONE; p->g_vis_rows = 10;
    p->fm_hover = -1; p->fm_hover_ui = -1; p->fm_hover_side = -1; p->fm_ctx_hover = -1; p->g_vis_cols = 1; p->fm_hpos = -1;
}
static struct fm_inst fm_inst1;
static struct fm_inst *fm_insts[FM_INST_MAX + 1] = { 0, &fm_inst1 };
static int fm_iused[FM_INST_MAX + 1], fm_iready;
static struct fm_inst *FMI = &fm_inst1;
static void fm_iboot(void) { if (!fm_iready) { fm_iready = 1; fm_inst_defaults(&fm_inst1); } }
#define file_list (FMI->file_list)
#define file_count (FMI->file_count)
#define selected_idx (FMI->selected_idx)
#define scroll_offset (FMI->scroll_offset)
#define sort_mode (FMI->sort_mode)
#define current_folder (FMI->current_folder)
#define fm_view (FMI->fm_view)
#define fm_cat (FMI->fm_cat)
#define preview_text (FMI->preview_text)
#define preview_extra (FMI->preview_extra)
#define last_click_time (FMI->last_click_time)
#define modal_mode (FMI->modal_mode)
#define modal_input (FMI->modal_input)
#define modal_input_len (FMI->modal_input_len)
#define modal_target (FMI->modal_target)
#define modal_caret (FMI->modal_caret)
#define modal_s0 (FMI->modal_s0)
#define modal_s1 (FMI->modal_s1)
#define modal_err (FMI->modal_err)
#define fm_filter (FMI->fm_filter)
#define fm_filter_len (FMI->fm_filter_len)
#define fm_search_active (FMI->fm_search_active)
#define fm_msg (FMI->fm_msg)
#define fm_msg_until (FMI->fm_msg_until)
#define st_info (FMI->st_info)
#define g_vis_rows (FMI->g_vis_rows)
#define fm_mode (FMI->fm_mode)
#define fm_reveal (FMI->fm_reveal)
#define fm_hover (FMI->fm_hover)
#define fm_hover_ui (FMI->fm_hover_ui)
#define fm_hover_side (FMI->fm_hover_side)
#define fm_sb_drag (FMI->fm_sb_drag)
#define fm_sb_grab (FMI->fm_sb_grab)
#define fm_ctx_open (FMI->fm_ctx_open)
#define fm_ctx_kind (FMI->fm_ctx_kind)
#define fm_ctx_hover (FMI->fm_ctx_hover)
#define fm_ctx_x (FMI->fm_ctx_x)
#define fm_ctx_y (FMI->fm_ctx_y)
#define g_vis_cols (FMI->g_vis_cols)
#define fm_histv (FMI->fm_histv)
#define fm_hn (FMI->fm_hn)
#define fm_hpos (FMI->fm_hpos)
#define crumbs (FMI->crumbs)
#define ncr (FMI->ncr)
#define cr_first (FMI->cr_first)
static int fm_big_state = 0;       /* 0 sin probar, 1 montado, 2 sin formatear, 3 error */
static int fm_big_err = 0;

static struct lib_ent lib[LIB_MAX];
static int lib_n = 0;
static int lib_cnt[FM_NCAT];
static uint64_t lib_bytes[FM_NCAT];
static int lib_dirty = 1;
static int lib_built = 0;
static int lib_truncated = 0;

static struct fm_undo undo_log[UNDO_MAX];
static int undo_n = 0;

/* ---- utilidades de cadena (Ring 0, sin libc) ---- */
static void str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (src && src[i] && i + 1 < max) { dst[i] = src[i]; i++; }
    if (max > 0) dst[i] = '\0';
}
static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return (*a == *b);
}
static uint32_t str_len(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static int str_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return 1;
}
static char fm_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int str_cmp_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = fm_lower(*a), cb = fm_lower(*b);
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static int str_contains_ci(const char *hay, const char *needle) {
    if (!needle || !*needle) return 1;
    for (; *hay; ++hay) {
        const char *h = hay, *n = needle;
        while (*h && *n && fm_lower(*h) == fm_lower(*n)) { h++; n++; }
        if (!*n) return 1;
    }
    return 0;
}
static void ap(char *buf, uint32_t max, const char *s) {
    uint32_t l = str_len(buf);
    while (s && *s && l + 1 < max) buf[l++] = *s++;
    buf[l] = '\0';
}
static void ap_u(char *buf, uint32_t max, uint64_t v) {
    char t[24]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    char o[24]; int k = 0;
    while (n) o[k++] = t[--n];
    o[k] = '\0';
    ap(buf, max, o);
}
static void ap_size(char *buf, uint32_t max, uint64_t v) {
    const uint64_t KI = 1024ull, MI = 1024ull * 1024ull, GI = 1024ull * 1024ull * 1024ull;
    if (v >= GI)      { ap_u(buf, max, v / GI); ap(buf, max, "."); ap_u(buf, max, ((v % GI) * 10) / GI); ap(buf, max, " GiB"); }
    else if (v >= MI) { ap_u(buf, max, v / MI); ap(buf, max, "."); ap_u(buf, max, ((v % MI) * 10) / MI); ap(buf, max, " MiB"); }
    else if (v >= KI) { ap_u(buf, max, v / KI); ap(buf, max, "."); ap_u(buf, max, ((v % KI) * 10) / KI); ap(buf, max, " KiB"); }
    else              { ap_u(buf, max, v); ap(buf, max, " B"); }
}
static uint32_t rd16(const char *p) { return (uint32_t)(uint8_t)p[0] | ((uint32_t)(uint8_t)p[1] << 8); }
static uint32_t rd32(const char *p) { return rd16(p) | (rd16(p + 2) << 16); }

static void fm_build_path(const char *dir, const char *item, char *out, uint32_t max) {
    uint32_t p = 0;
    while (dir && dir[p] && p + 1 < max) { out[p] = dir[p]; p++; }
    if (p > 0 && out[p - 1] != '/' && p + 1 < max) out[p++] = '/';
    uint32_t ip = 0;
    while (item && item[ip] && p + 1 < max) out[p++] = item[ip++];
    out[p] = '\0';
}
static void fm_get_parent_folder(const char *dir, char *parent, uint32_t max) {
    if (!dir || str_eq(dir, "/") || dir[0] == '\0') { str_copy(parent, "/", max); return; }
    uint32_t len = str_len(dir);
    while (len > 1 && dir[len - 1] == '/') len--;
    int last_slash = -1;
    for (int i = (int)len - 1; i >= 0; --i) if (dir[i] == '/') { last_slash = i; break; }
    if (last_slash <= 0) str_copy(parent, "/", max);
    else {
        uint32_t cp = (uint32_t)last_slash < max - 1 ? (uint32_t)last_slash : max - 1;
        for (uint32_t i = 0; i < cp; ++i) parent[i] = dir[i];
        parent[cp] = '\0';
    }
}
static const char *fm_base(const char *p) { const char *b = p; for (; *p; p++) if (*p == '/') b = p + 1; return b; }

static int fm_cat_of(const char *name) {
    const char *dot = 0;
    for (const char *s = name; *s; ++s) if (*s == '.') dot = s;
    if (!dot || dot == name || !dot[1]) return FC_OTR;
    char ext[8]; uint32_t n = 0;
    for (const char *s = dot + 1; *s; ++s) { if (n >= 7) return FC_OTR; ext[n++] = fm_lower(*s); }
    ext[n] = '\0';
    for (uint32_t i = 0; i < sizeof(ext_tab) / sizeof(ext_tab[0]); ++i)
        if (str_eq(ext_tab[i].e, ext)) return ext_tab[i].c;
    return FC_OTR;
}
static int fm_has_ext(const char *name, const char *ext) {
    const char *dot = 0;
    for (const char *s = name; *s; ++s) if (*s == '.') dot = s;
    if (!dot) return 0;
    const char *e = dot + 1;
    while (*e && *ext && fm_lower(*e) == *ext) { e++; ext++; }
    return !*e && !*ext;
}

static void fm_say(const char *m) {
    str_copy(fm_msg, m, sizeof(fm_msg));
    fm_msg_until = timer_get_uptime_ms() + 5000;
}

static int fm_is_sysdir(const char *p) {
    static const char *const sys[] = { "/etc", "/proc", "/dev", "/sys", "/tmp", "/bin", 0 };
    for (int i = 0; sys[i]; ++i) {
        uint32_t l = str_len(sys[i]);
        if (str_starts_with(p, sys[i]) && (p[l] == '\0' || p[l] == '/')) return 1;
    }
    return 0;
}
static int fm_is_protected(const char *name) {
    static const char *const prot[] = { "logo_soma.bmp", "chip_soma.bmp", "notas.txt", "dibujo.bmp", "autoexec.cmd", 0 };
    for (int i = 0; prot[i]; ++i) if (str_eq(name, prot[i])) return 1;
    return name[0] == '.';
}

/* ====================================================================
 * ACCESO A ALMACENES (RamFS / BIGFS)
 * ==================================================================== */
static struct bigfs *fm_big(void) {
    int err = 0;
    struct bigfs *fs = bigfs_sys(&err);
    if (fs) { fm_big_state = 1; fm_big_err = 0; }
    else { fm_big_err = err; fm_big_state = (err == BIGFS_E_NOTFMT) ? 2 : 3; }
    return fs;
}

static char ram_buf[4096];

/* SOMA_268_ACCESO: "Mi unidad" (/usuario) es donde trabaja el gestor; el resto del sistema se puede ver pero no cambiar (solo /tmp, temporal) */
static int fm_es_unid(const char *p) { return p && str_starts_with(p, "/usuario") && (p[8] == '/' || p[8] == '\0'); }
static int fm_puede(const char *p) {
    if (usuario_ruta_escribible(p)) return 1;
    fm_say("Solo puedes cambiar archivos en Mi unidad (/usuario)");
    return 0;
}

static const char *fm_ram_list_begin(void) {
    int len = vfs_format_list_ext(1, 0, ram_buf, sizeof(ram_buf) - 1);
    if (len <= 0) return 0;
    ram_buf[len] = '\0';
    const char *p = ram_buf;
    if (p[0] == 'A' && p[1] == 'r') { while (*p && *p != ':') p++; if (*p == ':') p++; }
    return p;
}
static int fm_ram_next(const char **pp, char *out, uint32_t max) {
    const char *p = *pp;
    while (*p == ' ' || *p == ',') p++;
    if (!*p) { *pp = p; return 0; }
    uint32_t k = 0;
    while (*p && *p != ',') { if (k + 1 < max) out[k++] = *p; p++; }
    out[k] = '\0'; *pp = p;
    return 1;
}

static int fm_exists(int store, const char *path) {
    if (store == ST_BIG) {
        struct bigfs *fs = fm_big(); uint32_t ino;
        return fs && bigfs_lookup(fs, path, &ino) >= 0;
    }
    return vfs_size(path) >= 0;                          /* PARCHE 087 */
}

static int64_t fm_read_head(int store, const char *path, char *buf, uint32_t n) {
    if (store == ST_BIG) {
        struct bigfs *fs = fm_big(); uint32_t ino;
        if (!fs || bigfs_lookup(fs, path, &ino) != (int)BIGFS_T_FILE) return -1;
        return bigfs_read(fs, ino, 0, buf, n);
    }
    uint32_t l = 0; char *a = vfs_read_alloc(path, &l);   /* PARCHE 087: vfs_read deja un byte para el '\0'; aqui se piden n exactos */
    if (!a) return -1;
    if (l > n) l = n;
    memcpy(buf, a, l); kfree(a);
    return (int64_t)l;
}

static int fm_move_ram(const char *from, const char *to) {
    if (!usuario_ruta_escribible(from) || !usuario_ruta_escribible(to)) return -5;      /* SOMA_268_ACCESO */
    if (fm_es_unid(to) && fm_es_unid(from)) return fm_exists(ST_RAM, to) ? -2 : vfs_unid_rename(from, to);
    if (str_len(to) >= (fm_es_unid(to) ? FM_PATH : FS_NAME_MAX)) return -3;
    if (fm_exists(ST_RAM, to)) return -2;
    uint32_t r = 0; char *tmp = vfs_read_alloc(from, &r);   /* PARCHE 087: archivos de cualquier tamano */
    if (!tmp) return -4;
    int rc = -1;
    if (vfs_write(to, tmp, r) >= 0) { vfs_delete(from); rc = 0; }
    kfree(tmp);
    return rc;
}
static int fm_move(int store, const char *from, const char *to) {
    if (store == ST_BIG) {
        struct bigfs *fs = fm_big();
        if (!fs) return -1;
        return bigfs_rename(fs, from, to);
    }
    return fm_move_ram(from, to);
}

static void fm_delete_big_rec(struct bigfs *fs, const char *path, int depth) {
    uint32_t ino; int t = bigfs_lookup(fs, path, &ino);
    if (t < 0) return;
    if (t == (int)BIGFS_T_FILE) { bigfs_unlink(fs, path); return; }
    if (depth < 12) {
        uint32_t cur = 0; struct bigfs_stat st;
        while (bigfs_readdir(fs, ino, &cur, &st) == 1) {
            char child[FM_PATH];
            fm_build_path(path, st.name, child, sizeof(child));
            fm_delete_big_rec(fs, child, depth + 1);
        }
    }
    bigfs_rmdir(fs, path);
}
static void fm_delete_item(int store, const char *path, int is_dir) {
    if (store == ST_BIG) {
        struct bigfs *fs = fm_big();
        if (!fs) return;
        if (is_dir) fm_delete_big_rec(fs, path, 0); else bigfs_unlink(fs, path);
        return;
    }
    if (!usuario_ruta_escribible(path)) return;                                       /* SOMA_268_ACCESO */
    if (fm_es_unid(path)) { vfs_unid_borrar_rec(path); return; }
    if (!is_dir) { vfs_delete(path); return; }
    uint32_t tl = str_len(path);
    const char *p = fm_ram_list_begin();
    char fn[80];
    while (p && fm_ram_next(&p, fn, sizeof(fn)))
        if (str_starts_with(fn, path) && (fn[tl] == '/' || fn[tl] == '\0')) vfs_delete(fn);
    char marker[96];
    fm_build_path(path, ".dir", marker, sizeof(marker));
    vfs_delete(marker);
}

/* renombrar una carpeta de RamFS = mover todo lo que cuelga de ella */
static int fm_rename_ram_dir(const char *from, const char *to) {
    uint32_t fl = str_len(from);
    const char *p = fm_ram_list_begin();
    char fn[80], nn[96]; int n = 0, bad = 0;
    static char names[4096];
    uint32_t np = 0;
    while (p && fm_ram_next(&p, fn, sizeof(fn)))
        if (str_starts_with(fn, from) && fn[fl] == '/') {
            uint32_t l = str_len(fn);
            if (np + l + 2 < sizeof(names)) { for (uint32_t i = 0; i <= l; ++i) names[np++] = fn[i]; }
        }
    for (uint32_t o = 0; o < np; ) {
        const char *src = names + o;
        o += str_len(src) + 1;
        str_copy(nn, to, sizeof(nn));
        ap(nn, sizeof(nn), src + fl);
        if (fm_move_ram(src, nn) == 0) n++; else bad++;
    }
    return bad ? -1 : (n >= 0 ? 0 : -1);
}

/* ====================================================================
 * BIBLIOTECA (indice virtual por categorias)
 * ==================================================================== */
static void lib_add(const char *path, uint64_t size, int store) {
    if (lib_n >= LIB_MAX) { lib_truncated = 1; return; }
    if (str_len(path) >= sizeof(lib[0].path)) return;
    str_copy(lib[lib_n].path, path, sizeof(lib[0].path));
    lib[lib_n].size = size;
    lib[lib_n].store = (uint8_t)store;
    lib[lib_n].cat = (uint8_t)fm_cat_of(fm_base(path));
    lib_cnt[lib[lib_n].cat]++;
    lib_bytes[lib[lib_n].cat] += size;
    lib_n++;
}

static void lib_walk_big(struct bigfs *fs, uint32_t dir, char *path, uint32_t plen, int depth) {
    uint32_t cur = 0; struct bigfs_stat st;
    while (lib_n < LIB_MAX && bigfs_readdir(fs, dir, &cur, &st) == 1) {
        if (st.name[0] == '.') continue;
        uint32_t nl = str_len(st.name);
        if (plen + 1 + nl >= 143) continue;
        path[plen] = '/';
        for (uint32_t i = 0; i < nl; ++i) path[plen + 1 + i] = st.name[i];
        path[plen + 1 + nl] = '\0';
        if (st.type == BIGFS_T_DIR) { if (depth < 8) lib_walk_big(fs, st.ino, path, plen + 1 + nl, depth + 1); }
        else lib_add(path, st.size, ST_BIG);
        path[plen] = '\0';
    }
}

static void lib_build(void) {
    lib_n = 0; lib_truncated = 0;
    for (int i = 0; i < FM_NCAT; ++i) { lib_cnt[i] = 0; lib_bytes[i] = 0; }
    const char *p = fm_ram_list_begin();
    char fn[80];
    while (p && lib_n < LIB_MAX && fm_ram_next(&p, fn, sizeof(fn))) {
        if (fn[0] != '/' || vfs_is_hidden(fn) || fm_is_sysdir(fn)) continue;
        int sz = vfs_size(fn);
        lib_add(fn, sz >= 0 ? (uint64_t)sz : 0, ST_RAM);
    }
    struct bigfs *fs = fm_big();
    if (fs) {
        uint32_t root;
        if (bigfs_lookup(fs, "/", &root) >= 0) {
            char path[144]; path[0] = '\0';
            lib_walk_big(fs, root, path, 0, 0);
        }
    }
    lib_dirty = 0; lib_built = 1;
}

/* ====================================================================
 * LISTADO / ORDENACION
 * ==================================================================== */
static int fm_cmp(const struct fm_item *a, const struct fm_item *b) {
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    int r = 0;
    switch (sort_mode) {
    case FM_SORT_NAME_DESC: return str_cmp_ci(b->name, a->name);
    case FM_SORT_SIZE_ASC:  if (a->size != b->size) r = a->size > b->size ? 1 : -1; break;
    case FM_SORT_SIZE_DESC: if (a->size != b->size) r = a->size < b->size ? 1 : -1; break;
    case FM_SORT_TYPE_ASC:  if (a->cat != b->cat) r = (int)a->cat - (int)b->cat; break;
    case FM_SORT_TYPE_DESC: if (a->cat != b->cat) r = (int)b->cat - (int)a->cat; break;
    default: break;
    }
    return r ? r : str_cmp_ci(a->name, b->name);
}
static void fm_sort_list(void) {
    if (file_count <= 1) return;
    if (fm_view == FV_LIB && !fm_filter_len) return;
    for (int i = 1; i < file_count; ++i) {
        struct fm_item tmp = file_list[i];
        int j = i - 1;
        while (j >= 0 && fm_cmp(&file_list[j], &tmp) > 0) { file_list[j + 1] = file_list[j]; j--; }
        file_list[j + 1] = tmp;
    }
}
static int fm_find_item_index(const char *name) {
    for (int i = 0; i < file_count; ++i) if (str_eq(file_list[i].name, name)) return i;
    return -1;
}
static struct fm_item *fm_sel_item(void) {
    return (selected_idx >= 0 && selected_idx < file_count) ? &file_list[selected_idx] : 0;
}

static void fm_item_clear(struct fm_item *it) {
    it->name[0] = '\0'; it->full_path[0] = '\0'; it->size = 0;
    it->is_dir = 0; it->store = ST_RAM; it->cat = FC_OTR; it->pad = 0; it->sub_count = 0;
}

static void fm_update_status(void) {
    st_info[0] = '\0';
    uint32_t f = 0, b = 0, d = 0;
    vfs_get_stats(&f, &b, &d);
    (void)b; (void)d;
    ap(st_info, sizeof(st_info), "RamFS ");
    ap_u(st_info, sizeof(st_info), f); ap(st_info, sizeof(st_info), "/"); ap_u(st_info, sizeof(st_info), FS_MAX_FILES);
    ap(st_info, sizeof(st_info), "  |  BIGFS ");
    if (fm_big_state == 1) {
        struct bigfs *fs = fm_big();
        if (fs) {
            struct bigfs_info bi; bigfs_info(fs, &bi);
            uint64_t cb = bi.cluster_bytes ? bi.cluster_bytes : BIGFS_CLUSTER;
            ap_size(st_info, sizeof(st_info), bi.free_clusters * cb);
            ap(st_info, sizeof(st_info), " libres");
        } else ap(st_info, sizeof(st_info), "no disponible");
    } else if (fm_big_state == 2) ap(st_info, sizeof(st_info), "sin formatear");
    else if (fm_big_state == 3) ap(st_info, sizeof(st_info), "no disponible");
    else ap(st_info, sizeof(st_info), "(sin consultar)");
}

static int fm_match(const char *name) { return !fm_filter_len || str_contains_ci(name, fm_filter); }

/* SOMA_268_ACCESO: el contenido de una carpeta de /usuario (exFAT): se lee directamente de la unidad, con tamanos y carpetas reales */
static void fm_reload_unid(void) {
    int cap = MAX_FM_FILES, total = 0, n;                       /* un nombre exFAT ocupa hasta 768 bytes: ~200 KiB para 256 entradas */
    struct exfat_dirent *d = (struct exfat_dirent *)kmalloc((size_t)cap * sizeof *d);
    if (!d) { cap = 64; d = (struct exfat_dirent *)kmalloc((size_t)cap * sizeof *d); }
    if (!d) { fm_say("Memoria insuficiente"); return; }
    n = unid_listar(current_folder, d, cap, &total);
    if (n < 0) { fm_say("La unidad del usuario no esta disponible en este pendrive"); kfree(d); return; }
    for (int i = 0; i < n && file_count < MAX_FM_FILES; ++i) {
        if (!fm_match(d[i].name)) continue;
        struct fm_item *it = &file_list[file_count++];
        fm_item_clear(it);
        str_copy(it->name, d[i].name, sizeof(it->name));
        fm_build_path(current_folder, d[i].name, it->full_path, sizeof(it->full_path));
        it->is_dir = d[i].is_dir ? 1 : 0;
        if (!it->is_dir) { it->size = d[i].size; it->cat = (uint8_t)fm_cat_of(d[i].name); }
    }
    if (total > n) fm_say("Hay mas elementos de los que caben en la vista");
    kfree(d);
}

static void fm_reload_ram(void) {
    if (fm_es_unid(current_folder)) { fm_reload_unid(); return; }                       /* SOMA_268_ACCESO */
    uint32_t cur_len = str_len(current_folder);
    int is_root = str_eq(current_folder, "/");
    const char *p = fm_ram_list_begin();
    char fname[80];
    while (p && file_count < MAX_FM_FILES && fm_ram_next(&p, fname, sizeof(fname))) {
        const char *rel = 0;
        if (is_root) { if (fname[0] == '/') rel = fname + 1; }
        else if (str_starts_with(fname, current_folder) && fname[cur_len] == '/') rel = fname + cur_len + 1;
        if (!rel || !*rel) continue;
        const char *slash = 0;
        for (const char *s = rel; *s; ++s) if (*s == '/') { slash = s; break; }
        if (slash) {
            char dirname[64];
            uint32_t dlen = (uint32_t)(slash - rel);
            if (dlen >= sizeof(dirname)) dlen = sizeof(dirname) - 1;
            for (uint32_t k = 0; k < dlen; ++k) dirname[k] = rel[k];
            dirname[dlen] = '\0';
            int ex = fm_find_item_index(dirname);
            if (ex >= 0) {
                if (!str_eq(slash + 1, ".dir")) file_list[ex].sub_count++;
            } else if (fm_match(dirname)) {
                struct fm_item *it = &file_list[file_count++];
                fm_item_clear(it);
                str_copy(it->name, dirname, sizeof(it->name));
                fm_build_path(current_folder, dirname, it->full_path, sizeof(it->full_path));
                it->is_dir = 1; it->sub_count = str_eq(slash + 1, ".dir") ? 0 : 1;
            }
        } else if (!str_eq(rel, ".dir") && fm_match(rel)) {
            struct fm_item *it = &file_list[file_count++];
            fm_item_clear(it);
            str_copy(it->name, rel, sizeof(it->name));
            str_copy(it->full_path, fname, sizeof(it->full_path));
            int sz = vfs_size(fname);
            it->size = sz >= 0 ? (uint64_t)sz : 0;
            it->cat = (uint8_t)fm_cat_of(rel);
        }
    }
}

static void fm_reload_big(void) {
    struct bigfs *fs = fm_big();
    if (!fs) return;
    uint32_t dir;
    int t = bigfs_lookup(fs, current_folder, &dir);
    if (t != (int)BIGFS_T_DIR) { fm_say("Carpeta no encontrada en BIGFS"); return; }
    uint32_t cur = 0; struct bigfs_stat st;
    while (file_count < MAX_FM_FILES && bigfs_readdir(fs, dir, &cur, &st) == 1) {
        if (st.name[0] == '.' && st.name[1] == '\0') continue;
        if (!fm_match(st.name)) continue;
        struct fm_item *it = &file_list[file_count++];
        fm_item_clear(it);
        str_copy(it->name, st.name, sizeof(it->name));
        fm_build_path(current_folder, st.name, it->full_path, sizeof(it->full_path));
        it->store = ST_BIG;
        it->is_dir = (st.type == BIGFS_T_DIR);
        it->size = it->is_dir ? 0 : st.size;
        it->cat = it->is_dir ? FC_OTR : (uint8_t)fm_cat_of(st.name);
    }
}

static void fm_reload_lib(void) {
    if (lib_dirty || !lib_built) lib_build();
    if (fm_view == FV_LIB && !fm_filter_len) {
        for (int c = 0; c < FM_NCAT; ++c) {
            struct fm_item *it = &file_list[file_count++];
            fm_item_clear(it);
            str_copy(it->name, cat_name[c], sizeof(it->name));
            str_copy(it->full_path, "@Biblioteca/", sizeof(it->full_path));
            ap(it->full_path, sizeof(it->full_path), cat_name[c]);
            it->is_dir = 1; it->store = ST_VIRT; it->cat = (uint8_t)c;
            it->size = lib_bytes[c];
            it->sub_count = (uint16_t)(lib_cnt[c] > 65535 ? 65535 : lib_cnt[c]);
        }
        return;
    }
    for (int i = 0; i < lib_n && file_count < MAX_FM_FILES; ++i) {
        if (fm_view == FV_CAT && lib[i].cat != fm_cat) continue;
        const char *nm = fm_base(lib[i].path);
        if (!fm_match(nm)) continue;
        struct fm_item *it = &file_list[file_count++];
        fm_item_clear(it);
        str_copy(it->name, nm, sizeof(it->name));
        str_copy(it->full_path, lib[i].path, sizeof(it->full_path));
        it->store = lib[i].store; it->cat = lib[i].cat; it->size = lib[i].size;
    }
}

static void fm_update_preview(int idx);
static void fm_side_rebuild(void);

static void fm_reload(void) {
    file_count = 0; selected_idx = -1;
    preview_text[0] = '\0'; preview_extra[0] = '\0';
    if (fm_view == FV_RAM) fm_reload_ram();
    else if (fm_view == FV_BIG) fm_reload_big();
    else fm_reload_lib();
    fm_sort_list();
    if (fm_view == FV_RAM && str_eq(current_folder, "/usuario")) fm_side_rebuild();      /* SOMA_273_ARCHIVOS */
    if (scroll_offset > file_count - 1) scroll_offset = 0;
    if (scroll_offset < 0) scroll_offset = 0;
    fm_update_status();
}

void fm_refresh(void) { lib_dirty = 1; fm_reload(); }

/* SOMA_273_ARCHIVOS: el explorador solo trabaja dentro de Mi unidad (/usuario). Cualquier otro destino (Biblioteca, BIGFS, "/", "..") vuelve a la raiz de la unidad. */
static int fm_ruta_ok(const char *p) { return fm_ed_ruta_ok(p); }
static void fm_goto(int view, const char *path, int cat) {
    if (view != FV_RAM || !fm_ruta_ok(path)) { view = FV_RAM; path = "/usuario"; cat = 0; }
    fm_view = view; fm_cat = cat;
    if (view == FV_LIB) str_copy(current_folder, "@Biblioteca", sizeof(current_folder));
    else if (view == FV_CAT) { str_copy(current_folder, "@Biblioteca/", sizeof(current_folder)); ap(current_folder, sizeof(current_folder), cat_name[cat]); }
    else {
        str_copy(current_folder, (path && *path) ? path : "/", sizeof(current_folder));
        uint32_t l = str_len(current_folder);
        while (l > 1 && current_folder[l - 1] == '/') current_folder[--l] = '\0';
        if (current_folder[0] != '/') { char t[FM_PATH]; str_copy(t, current_folder, sizeof(t)); current_folder[0] = '/'; str_copy(current_folder + 1, t, sizeof(current_folder) - 1); }
    }
    scroll_offset = 0; selected_idx = -1;
    fm_filter[0] = '\0'; fm_filter_len = 0; fm_search_active = 0;
    fm_reload();
}

void fm_init(void) {
    fm_iboot();
    modal_mode = FM_MODAL_NONE;
    fm_msg[0] = '\0';
    fm_goto(FV_RAM, "/usuario", 0);
}

void fm_open_folder(const char *folder) {
    fm_iboot();
    modal_mode = FM_MODAL_NONE;
    if (!folder || !*folder) { fm_goto(FV_RAM, current_folder, 0); return; }
    if (folder[0] == '@') {
        for (int c = 0; c < FM_NCAT; ++c) {
            char t[FM_PATH]; str_copy(t, "@Biblioteca/", sizeof(t)); ap(t, sizeof(t), cat_name[c]);
            if (str_eq(folder, t)) { fm_goto(FV_CAT, 0, c); return; }
        }
        fm_goto(FV_LIB, 0, 0);
        return;
    }
    if (str_eq(folder, "/big") || str_starts_with(folder, "/big/")) {
        fm_goto(FV_BIG, folder[4] ? folder + 4 : "/", 0);
        return;
    }
    fm_goto(FV_RAM, folder, 0);
}

static void fm_go_up(void) {
    if (fm_view == FV_CAT) { fm_goto(FV_LIB, 0, 0); return; }
    if (fm_view == FV_LIB) return;
    if (str_eq(current_folder, "/") || str_eq(current_folder, "/usuario")) return;      /* SOMA_273_ARCHIVOS: no se sube de Mi unidad */
    char parent[FM_PATH];
    fm_get_parent_folder(current_folder, parent, sizeof(parent));
    fm_goto(fm_view, parent, 0);
}

/* ====================================================================
 * VISTA PREVIA
 * ==================================================================== */
static void fm_update_preview(int idx) {
    preview_text[0] = '\0'; preview_extra[0] = '\0';
    if (idx < 0 || idx >= file_count) return;
    struct fm_item *it = &file_list[idx];
    if (it->is_dir) {
        if (it->store == ST_VIRT) {
            ap_u(preview_text, sizeof(preview_text), it->sub_count);
            ap(preview_text, sizeof(preview_text), " archivos en esta categoria (");
            ap_size(preview_text, sizeof(preview_text), it->size); ap(preview_text, sizeof(preview_text), ")");
            str_copy(preview_extra, "Categoria", sizeof(preview_extra));
        } else {
            str_copy(preview_text, it->store == ST_BIG ? "Carpeta del almacen BIGFS" : "Carpeta de archivos del sistema", sizeof(preview_text));
            str_copy(preview_extra, "Carpeta", sizeof(preview_extra));
        }
        return;
    }
    static char raw[260];
    int64_t r = fm_read_head(it->store, it->full_path, raw, 256);
    if (r < 0) r = 0;
    raw[r] = '\0';
    str_copy(preview_extra, cat_kind[it->cat], sizeof(preview_extra));
    if (r == 0) { str_copy(preview_text, "(Archivo vacio)", sizeof(preview_text)); return; }

    if (it->cat == FC_IMG && r >= 30 && raw[0] == 'B' && raw[1] == 'M') {
        ap(preview_text, sizeof(preview_text), "Imagen BMP ");
        ap_u(preview_text, sizeof(preview_text), rd32(raw + 18)); ap(preview_text, sizeof(preview_text), "x");
        ap_u(preview_text, sizeof(preview_text), rd32(raw + 22)); ap(preview_text, sizeof(preview_text), " (");
        ap_u(preview_text, sizeof(preview_text), rd16(raw + 28)); ap(preview_text, sizeof(preview_text), " bit)");
        return;
    }
    if (it->cat == FC_AUD && r >= 36 && str_starts_with(raw, "RIFF") && raw[8] == 'W' && raw[9] == 'A') {
        uint32_t rate = rd32(raw + 24), ch = rd16(raw + 22), bits = rd16(raw + 34), brate = rd32(raw + 28);
        ap(preview_text, sizeof(preview_text), "Audio WAV ");
        ap_u(preview_text, sizeof(preview_text), rate); ap(preview_text, sizeof(preview_text), " Hz, ");
        ap_u(preview_text, sizeof(preview_text), ch); ap(preview_text, sizeof(preview_text), ch == 1 ? " canal, " : " canales, ");
        ap_u(preview_text, sizeof(preview_text), bits); ap(preview_text, sizeof(preview_text), " bit");
        if (brate) { uint64_t s = it->size / brate; ap(preview_text, sizeof(preview_text), ", "); ap_u(preview_text, sizeof(preview_text), s / 60); ap(preview_text, sizeof(preview_text), ":"); if (s % 60 < 10) ap(preview_text, sizeof(preview_text), "0"); ap_u(preview_text, sizeof(preview_text), s % 60); ap(preview_text, sizeof(preview_text), " min"); }
        return;
    }
    if (it->cat == FC_VID && r >= 72 && str_starts_with(raw, "RIFF") && raw[8] == 'A' && raw[9] == 'V' && raw[24] == 'a') {
        uint32_t us = rd32(raw + 32), fr = rd32(raw + 48), w = rd32(raw + 64), h = rd32(raw + 68);
        ap(preview_text, sizeof(preview_text), "Video AVI ");
        ap_u(preview_text, sizeof(preview_text), w); ap(preview_text, sizeof(preview_text), "x"); ap_u(preview_text, sizeof(preview_text), h);
        uint64_t s = ((uint64_t)fr * us) / 1000000ull;
        ap(preview_text, sizeof(preview_text), ", "); ap_u(preview_text, sizeof(preview_text), s / 60); ap(preview_text, sizeof(preview_text), ":");
        if (s % 60 < 10) ap(preview_text, sizeof(preview_text), "0");
        ap_u(preview_text, sizeof(preview_text), s % 60); ap(preview_text, sizeof(preview_text), " min");
        return;
    }
    int binary = 0;
    for (int64_t k = 0; k < r; ++k) if ((uint8_t)raw[k] < 9) { binary = 1; break; }
    if (binary) {
        str_copy(preview_text, "(contenido binario: ", sizeof(preview_text));
        ap_size(preview_text, sizeof(preview_text), it->size); ap(preview_text, sizeof(preview_text), ")");
        return;
    }
    for (int64_t k = 0; k < r; ++k) if (raw[k] == '\r' || raw[k] == '\n' || raw[k] == '\t') raw[k] = ' ';
    str_copy(preview_text, raw, sizeof(preview_text));
}

/* ====================================================================
 * SOMA_PARCHE_028_UI: INTERFAZ RENOVADA DEL EXPLORADOR
 *  - barra de herramientas con historial (atras/adelante/subir), ruta navegable, busqueda y vista lista/iconos
 *  - barra lateral con lugares, biblioteca y uso del almacenamiento
 *  - panel de detalles con miniatura de las imagenes y vista previa del texto
 *  - menu contextual bajo el puntero, resalte al pasar el raton, rueda y barra de desplazamiento arrastrable
 * Toda la logica de archivos (RamFS, BIGFS, Biblioteca, organizar, deshacer, portapapeles) es la de antes.
 * ==================================================================== */
static int32_t fm_mx = -1, fm_my = -1;
static int  fm_prev_left = 0, fm_swallow = 0;

static void fm_set_sel(int idx) {
    selected_idx = idx;
    if (idx >= 0) fm_reveal = 1;
    fm_update_preview(idx);
}

/* ---- historial de navegacion ---- */
static int fm_hist_same(const struct fm_hist *h) {
    if (h->view != fm_view) return 0;
    if (fm_view == FV_CAT) return h->cat == fm_cat;
    if (fm_view == FV_LIB) return 1;
    return str_eq(h->path, current_folder);
}
static void fm_hist_sync(void) {
    if (fm_hpos >= 0 && fm_hist_same(&fm_histv[fm_hpos])) return;
    if (fm_hpos + 1 >= 16) { for (int i = 1; i < 16; ++i) fm_histv[i - 1] = fm_histv[i]; fm_hpos--; }
    fm_hpos++; fm_hn = fm_hpos + 1;
    struct fm_hist *h = &fm_histv[fm_hpos];
    h->view = (uint8_t)fm_view; h->cat = (uint8_t)fm_cat; str_copy(h->path, current_folder, sizeof(h->path));
}
static void fm_hist_go(int d) {
    int np = fm_hpos + d;
    if (np < 0 || np >= fm_hn) return;
    fm_hpos = np;
    struct fm_hist *h = &fm_histv[np];
    if (h->view == FV_RAM || h->view == FV_BIG) fm_goto(h->view, h->path, 0); else fm_goto(h->view, 0, h->cat);
}

/* ---- texto de la interfaz (proporcional y suavizado) ---- */
static int fm_wf = -1;
static int fm_has_wf(void) { if (fm_wf < 0) fm_wf = wf_init() ? 1 : 0; return fm_wf; }
static int ftw(const char *s, int px) {
    uint32_t n = str_len(s);
    if (fm_has_wf()) return wf_width(s, n, px, 0);
    int c = 0; for (uint32_t i = 0; i < n; ++i) if (((uint8_t)s[i] & 0xC0) != 0x80) c++;
    return c * 8;
}
static void ftxn(int32_t x, int32_t top, int32_t h, const char *s, uint32_t n, int px, uint32_t col, int bold) {
    if (!n) return;
    if (fm_has_wf()) { wf_draw(x, top + (h + (px * 72) / 100) / 2, s, n, px, col, bold ? WF_BOLD : 0, 255); return; }
    char b[160]; uint32_t k = n < 159 ? n : 159; for (uint32_t i = 0; i < k; ++i) b[i] = s[i]; b[k] = '\0';
    font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), b, col, 1);
}
static void ftx(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold) { ftxn(x, top, h, s, str_len(s), px, col, bold); }
/* texto recortado con "..." si no cabe en maxw; devuelve el ancho dibujado */
static int ftx_fit(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t col, int bold, int maxw) {
    uint32_t n = str_len(s);
    int w = fm_has_wf() ? wf_width(s, n, px, bold ? WF_BOLD : 0) : ftw(s, px);
    if (w <= maxw) { ftxn(x, top, h, s, n, px, col, bold); return w; }
    int dots = ftw("...", px);
    while (n > 0) {
        n--; while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
        int ww = fm_has_wf() ? wf_width(s, n, px, bold ? WF_BOLD : 0) : (int)n * 8;
        if (ww + dots <= maxw) {
            char b[200]; if (n > 190) n = 190; for (uint32_t i = 0; i < n; ++i) b[i] = s[i];
            b[n] = '.'; b[n + 1] = '.'; b[n + 2] = '.'; b[n + 3] = '\0';
            ftxn(x, top, h, b, n + 3, px, col, bold); return ww + dots;
        }
    }
    return 0;
}

/* ---- paleta ---- */
#define KC_WIN     0x000C111B
#define KC_BAR     0x00131A27
#define KC_SIDE    0x000F1520
#define KC_LINE    0x00212B3B
#define KC_FIELD   0x00090D15
#define KC_TXT     0x00E5EAF2
#define KC_DIM     0x008592A8
#define KC_FAINT   0x00566379
#define KC_ACC     0x0038BDF8
#define KC_HOVER   0x00182133
#define KC_SELBG   0x001A3558
#define KC_PRIMARY 0x000B6BCB

/* ---- iconos vectoriales de archivo y carpeta (tamano s px) ---- */
static void fm_ext_upper(const char *name, char *out, uint32_t max) {
    const char *dot = 0; for (const char *p = name; *p; ++p) if (*p == '.') dot = p;
    out[0] = '\0'; if (!dot || dot == name || !dot[1]) return;
    uint32_t k = 0; for (const char *p = dot + 1; *p && k + 1 < max && k < 4; ++p) out[k++] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
    out[k] = '\0';
}
static uint32_t fm_mix(uint32_t a, uint32_t b, uint32_t t) {   /* t 0..256 hacia b */
    uint32_t rb = ((((a & 0xFF00FFu) * (256 - t)) + ((b & 0xFF00FFu) * t)) >> 8) & 0xFF00FFu;
    uint32_t g = ((((a & 0x00FF00u) * (256 - t)) + ((b & 0x00FF00u) * t)) >> 8) & 0x00FF00u;
    return rb | g;
}
static void fm_glyph(int cat, int32_t cx, int32_t cy, int32_t s, uint32_t col) {
    uint32_t c = 0xFF000000u | col; int32_t X = cx * 16, Y = cy * 16, u = s * 16 / 10, w = s >= 30 ? 28 : 20;
    switch (cat) {
    case FC_IMG:
        fb_line_aa(X - 2 * u, Y + u, X - u / 2, Y - u / 2, w, c); fb_line_aa(X - u / 2, Y - u / 2, X + u / 2, Y + u / 2, w, c);
        fb_line_aa(X + u / 2, Y + u / 2, X + u, Y, w, c); fb_line_aa(X + u, Y, X + 2 * u, Y + u, w, c);
        fb_line_aa(X + u, Y - u, X + u, Y - u, w * 2, c); break;
    case FC_VID:
        fb_line_aa(X - u, Y - u, X - u, Y + u, w, c); fb_line_aa(X - u, Y - u, X + u + u / 3, Y, w, c); fb_line_aa(X - u, Y + u, X + u + u / 3, Y, w, c); break;
    case FC_AUD:
        fb_line_aa(X + u / 2, Y - u - u / 2, X + u / 2, Y + u / 2, w, c); fb_line_aa(X + u / 2, Y - u - u / 2, X + u + u / 2, Y - u, w, c);
        fb_line_aa(X - u / 3, Y + u / 2, X - u / 3, Y + u / 2, w * 3, c); break;
    case FC_PRG:
        fb_line_aa(X - u - u / 2, Y - u / 2, X - u / 3, Y + u / 3, w, c); fb_line_aa(X - u / 3, Y + u / 3, X - u - u / 2, Y + u + u / 6, w, c);
        fb_line_aa(X + u / 6, Y + u + u / 6, X + u + u / 2, Y + u + u / 6, w, c); break;
    case FC_WEB:
        fb_stroke_rrect_aa(cx - s / 8, cy - s / 8, s / 4 + 1, s / 4 + 1, s / 8, c);
        fb_line_aa(X - u * 3 / 2, Y, X + u * 3 / 2, Y, w - 4, c); fb_line_aa(X, Y - u * 3 / 2, X, Y + u * 3 / 2, w - 4, c); break;
    case FC_ZIP:
        for (int i = -2; i <= 2; ++i) { fb_line_aa(X + (i & 1 ? u / 2 : -u / 2), Y + i * u * 2 / 3, X + (i & 1 ? u / 2 : -u / 2), Y + i * u * 2 / 3, w * 2, c); }
        break;
    default:
        for (int i = -1; i <= 1; ++i) { fb_line_aa(X - u * 3 / 2, Y + i * u, X + (i == 1 ? u / 2 : u * 3 / 2), Y + i * u, w - 4, c); }
        break;
    }
}
static void fm_icon2(int32_t x, int32_t y, int32_t s, int cat, int is_dir, int virt, const char *name) {
    if (is_dir) {
        uint32_t base = virt ? cat_col[cat] : 0x00E9B949;
        uint32_t back = fm_mix(base, 0x00000000, 70), front = fm_mix(base, 0x00FFFFFF, 30);
        int32_t r = s >= 32 ? s / 9 : 2;
        fb_fill_rrect_aa(x, y + s / 8, s * 9 / 20, s / 4, r, 0xFF000000u | back);
        fb_fill_rrect_aa(x, y + s / 5, s, s * 7 / 10, r, 0xFF000000u | back);
        fb_fill_rrect_aa(x, y + s * 3 / 10, s, s * 6 / 10, r, 0xFF000000u | front);
        if (virt && s >= 28) fm_glyph(cat, x + s / 2, y + s * 3 / 5, s / 3, fm_mix(base, 0x00000000, 120));
        return;
    }
    uint32_t col = cat_col[cat];
    int32_t pw = s * 3 / 4, px0 = x + (s - pw) / 2, r = s >= 32 ? s / 12 : 2, fold = s / 4;
    fb_fill_rrect_aa(px0, y, pw, s, r, 0xFFDDE3ECu);
    fb_fill_rrect_aa(px0 + pw - fold, y, fold, fold, r, 0xFFAFB9C8u);          /* esquina doblada */
    if (s >= 32) {
        int32_t bh = s / 4;
        fb_fill_rrect_aa(px0, y + s - bh - s / 10, pw, bh, 0, 0xFF000000u | col);
        char ext[8]; fm_ext_upper(name ? name : "", ext, sizeof(ext));
        if (ext[0] && fm_has_wf()) {
            int fpx = s >= 60 ? 13 : 10, tw = wf_width(ext, str_len(ext), fpx, WF_BOLD);
            if (tw < pw - 4) wf_draw(px0 + (pw - tw) / 2, y + s - s / 10 - (bh - fpx * 7 / 10) / 2, ext, str_len(ext), fpx, 0x00FFFFFF, WF_BOLD, 255);
        }
        fm_glyph(cat, px0 + pw / 2, y + s * 2 / 5, s / 2, col);
    } else {
        fb_fill_rrect_aa(px0, y + s - s / 3, pw, s / 4, 0, 0xFF000000u | col);
    }
}

/* ---- miniatura de la imagen seleccionada (PNG, JPEG, GIF, BMP) ---- */
#define FM_TH_W 232
#define FM_TH_H 150
static uint32_t *fm_th_px;          /* SOMA_PARCHE_028_UI: dinamica (la .bss del kernel esta casi llena) */
static int fm_th_w = 0, fm_th_h = 0, fm_th_state = 0;     /* 0 nada, 1 lista, 2 no se pudo */
static char fm_th_key[FM_PATH + 4];
static void fm_thumb_for(struct fm_item *it) {
    char key[FM_PATH + 4]; key[0] = (char)('0' + it->store); key[1] = '\0'; ap(key, sizeof(key), it->full_path);
    if (str_eq(key, fm_th_key)) return;
    str_copy(fm_th_key, key, sizeof(fm_th_key));
    fm_th_state = 2; fm_th_w = fm_th_h = 0;
    if (it->is_dir || it->cat != FC_IMG || it->size < 8 || it->size > (6u << 20)) return;
    if (!fm_th_px) { fm_th_px = (uint32_t *)kmalloc(FM_TH_W * FM_TH_H * 4); if (!fm_th_px) return; }
    char *buf = (char *)kmalloc((uint32_t)it->size + 16);
    if (!buf) return;
    int64_t r = fm_read_head(it->store, it->full_path, buf, (uint32_t)it->size);
    struct wimg im; im.px = 0; im.w = im.h = 0;
    if (r > 8 && wi_decode((const uint8_t *)buf, (uint32_t)r, &im) == 0 && im.px && im.w > 0 && im.h > 0) {
        int tw = FM_TH_W, th = (int)((int64_t)im.h * FM_TH_W / im.w);
        if (th > FM_TH_H) { th = FM_TH_H; tw = (int)((int64_t)im.w * FM_TH_H / im.h); }
        if (tw > im.w && th > im.h) { tw = im.w; th = im.h; }
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
        for (int y = 0; y < th; ++y) for (int x = 0; x < tw; ++x) {
            int sx0 = (int)((int64_t)x * im.w / tw), sx1 = (int)((int64_t)(x + 1) * im.w / tw), sy0 = (int)((int64_t)y * im.h / th), sy1 = (int)((int64_t)(y + 1) * im.h / th);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            if (sx1 - sx0 > 4) sx1 = sx0 + 4;           /* promedio de hasta 4x4 muestras */
            if (sy1 - sy0 > 4) sy1 = sy0 + 4;
            uint32_t a = 0, rr = 0, g = 0, b = 0, n = 0;
            for (int yy = sy0; yy < sy1; ++yy) for (int xx = sx0; xx < sx1; ++xx) { uint32_t c = im.px[(size_t)yy * im.w + xx]; uint32_t al = c >> 24; a += al; rr += ((c >> 16) & 255) * al; g += ((c >> 8) & 255) * al; b += (c & 255) * al; n++; }
            uint32_t al = a / n, bgc = ((x / 8 + y / 8) & 1) ? 0x2A : 0x22;             /* fondo a cuadros para la transparencia */
            uint32_t R = a ? rr / a : 0, G = a ? g / a : 0, B = a ? b / a : 0;
            R = (R * al + bgc * (255 - al)) / 255; G = (G * al + bgc * (255 - al)) / 255; B = (B * al + (bgc + 8) * (255 - al)) / 255;
            fm_th_px[y * FM_TH_W + x] = (R << 16) | (G << 8) | B;
        }
        fm_th_w = tw; fm_th_h = th; fm_th_state = 1;
    }
    wi_free(&im);
    kfree(buf);
}

/* ---- miniaturas de la vista de iconos: cache pequena (48x48 px por imagen); se decodifica como mucho una por
 *      fotograma para que abrir una carpeta con muchas fotos no congele la interfaz ---- */
#define FM_GT 48
#define FM_GT_N 24
struct fm_gthumb { char key[FM_PATH + 4]; uint32_t px[FM_GT * FM_GT]; int w, h, state; uint32_t use; };   /* state 1 lista, 2 fallo */
static struct fm_gthumb *fm_gt; static uint32_t fm_gt_clock; static int fm_gt_budget;
static const struct fm_gthumb *fm_grid_thumb(struct fm_item *it) {
    if (it->is_dir || it->cat != FC_IMG || it->size < 8 || it->size > (4u << 20)) return 0;
    if (!fm_gt) { fm_gt = (struct fm_gthumb *)kmalloc(sizeof(struct fm_gthumb) * FM_GT_N); if (!fm_gt) return 0; memset(fm_gt, 0, sizeof(struct fm_gthumb) * FM_GT_N); }
    char key[FM_PATH + 4]; key[0] = (char)('0' + it->store); key[1] = '\0'; ap(key, sizeof(key), it->full_path);
    struct fm_gthumb *slot = &fm_gt[0];
    for (int i = 0; i < FM_GT_N; ++i) {
        if (fm_gt[i].state && str_eq(fm_gt[i].key, key)) { fm_gt[i].use = ++fm_gt_clock; return fm_gt[i].state == 1 ? &fm_gt[i] : 0; }
        if (!fm_gt[i].state || fm_gt[i].use < slot->use) slot = &fm_gt[i];
    }
    if (fm_gt_budget <= 0) return 0;                      /* ya se decodifico una en este fotograma */
    fm_gt_budget--;
    str_copy(slot->key, key, sizeof(slot->key)); slot->state = 2; slot->use = ++fm_gt_clock; slot->w = slot->h = 0;
    char *buf = (char *)kmalloc((uint32_t)it->size + 16);
    if (!buf) return 0;
    int64_t r = fm_read_head(it->store, it->full_path, buf, (uint32_t)it->size);
    struct wimg im; im.px = 0; im.w = im.h = 0;
    if (r > 8 && wi_decode((const uint8_t *)buf, (uint32_t)r, &im) == 0 && im.px && im.w > 0 && im.h > 0) {
        /* recorte central cuadrado, como las miniaturas de los exploradores actuales */
        int side = im.w < im.h ? im.w : im.h, ox = (im.w - side) / 2, oy = (im.h - side) / 2, n = side < FM_GT ? side : FM_GT;
        for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
            int sx0 = ox + x * side / n, sx1 = ox + (x + 1) * side / n, sy0 = oy + y * side / n, sy1 = oy + (y + 1) * side / n;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            if (sx1 - sx0 > 4) sx1 = sx0 + 4;
            if (sy1 - sy0 > 4) sy1 = sy0 + 4;
            uint32_t rr = 0, g = 0, b = 0, a = 0, k = 0;
            for (int yy = sy0; yy < sy1; ++yy) for (int xx = sx0; xx < sx1; ++xx) { uint32_t c = im.px[(size_t)yy * im.w + xx], al = c >> 24; a += al; rr += ((c >> 16) & 255) * al; g += ((c >> 8) & 255) * al; b += (c & 255) * al; k++; }
            uint32_t al = a / k, R = a ? rr / a : 0, G = a ? g / a : 0, B = a ? b / a : 0, bg = 0x26;
            R = (R * al + bg * (255 - al)) / 255; G = (G * al + bg * (255 - al)) / 255; B = (B * al + (bg + 10) * (255 - al)) / 255;
            slot->px[y * FM_GT + x] = (R << 16) | (G << 8) | B;
        }
        slot->w = slot->h = n; slot->state = 1;
    }
    wi_free(&im);
    kfree(buf);
    return slot->state == 1 ? slot : 0;
}

/* ---- geometria (la misma tabla para dibujar y para el raton) ---- */
enum { FB_BACK = 0, FB_FWD, FB_UP, FB_LIST, FB_GRID, FB_NFOL, FB_NFIL, FB_ORG, FB_REF, FB_N,
       FB_OPEN = 20, FB_REN, FB_DEL, FB_UNDO, FB_SRCH, FB_CRUMB };
static const char *const fb_tip[FB_N] = {
    "Atr\xC3\xA1s", "Adelante", "Subir a la carpeta superior", "Vista de lista", "Vista de iconos",
    "Nueva carpeta", "Nuevo archivo", "Organizar por categor\xC3\xAD" "as", "Actualizar" };
#define FM_TB_H   48
#define FM_BTN    32
#define FM_ROW    30
#define FM_HDR    30
#define FM_ST_H   26
#define FM_TILE_W 108
#define FM_TILE_H 104
struct fl {
    int32_t x, y, w, h;
    int32_t bx[FB_N], by;                       /* botones de la barra */
    int32_t crumb_x, crumb_w, srch_x, srch_w;
    int32_t side_w, body_y, body_h;
    int32_t main_x, main_w, det_x, det_w;
    int32_t hdr_y, items_y, items_h, sb_x;
    int32_t col_type, col_size, has_type;
    int32_t cols, vis, rows_vis;                /* en iconos: columnas, elementos visibles y filas visibles */
    int32_t st_y;
};
static void fl_make(struct fl *L, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh) {
    L->x = (int32_t)wx; L->y = (int32_t)wy; L->w = (int32_t)ww; L->h = (int32_t)wh;
    L->by = L->y + (FM_TB_H - FM_BTN) / 2;
    int32_t x = L->x + 10;
    L->bx[FB_BACK] = x; x += FM_BTN + 2; L->bx[FB_FWD] = x; x += FM_BTN + 2; L->bx[FB_UP] = x; x += FM_BTN + 8;
    int32_t r = L->x + L->w - 10;
    r -= FM_BTN; L->bx[FB_REF] = r; r -= FM_BTN + 2; L->bx[FB_ORG] = r; r -= FM_BTN + 2; L->bx[FB_NFIL] = r; r -= FM_BTN + 2; L->bx[FB_NFOL] = r;
    r -= 10 + FM_BTN; L->bx[FB_GRID] = r; r -= FM_BTN; L->bx[FB_LIST] = r; r -= 10;
    int32_t room = r - x;
    L->srch_w = room > 520 ? 220 : (room > 300 ? room * 2 / 5 : (room > 160 ? 110 : 0));
    L->srch_x = r - L->srch_w;
    L->crumb_x = x; L->crumb_w = L->srch_x - (L->srch_w ? 8 : 0) - x; if (L->crumb_w < 40) L->crumb_w = 40;
    L->side_w = L->w < 620 ? 150 : 196;
    L->body_y = L->y + FM_TB_H; L->st_y = L->y + L->h - FM_ST_H; L->body_h = L->st_y - L->body_y; if (L->body_h < 40) L->body_h = 40;
    L->main_x = L->x + L->side_w; L->main_w = L->w - L->side_w;
    L->det_w = L->main_w >= 660 ? 280 : 0;
    L->det_x = L->x + L->w - L->det_w;
    L->main_w -= L->det_w;
    L->hdr_y = L->body_y;
    int grid = (fm_mode == 1);
    L->items_y = grid ? L->body_y + 8 : L->body_y + FM_HDR;
    L->items_h = L->body_y + L->body_h - L->items_y; if (L->items_h < FM_ROW) L->items_h = FM_ROW;
    L->sb_x = L->main_x + L->main_w - 12;
    L->col_size = L->main_x + L->main_w - 22 - 90;
    L->has_type = L->main_w >= 400;
    L->col_type = L->has_type ? L->col_size - (L->main_w >= 560 ? 170 : 120) : L->col_size;
    if (grid) {
        L->cols = (L->main_w - 24) / FM_TILE_W; if (L->cols < 1) L->cols = 1;
        L->rows_vis = L->items_h / FM_TILE_H; if (L->rows_vis < 1) L->rows_vis = 1;
        L->vis = L->cols * L->rows_vis;
    } else {
        L->cols = 1; L->rows_vis = L->items_h / FM_ROW; if (L->rows_vis < 1) L->rows_vis = 1; L->vis = L->rows_vis;
    }
}
/* filas de la barra lateral (SOMA_273_ARCHIVOS: solo Mi unidad y sus carpetas, que se rellenan al listar la raiz) */
struct side_row { uint8_t kind; /* 0 titulo, 1 carpeta */ const char *label; const char *path; int cat; };
#define SIDE_MAX 24
static char side_lab[SIDE_MAX][64], side_pat[SIDE_MAX][FM_PATH];
static struct side_row side_rows[SIDE_MAX] = { {0, "MI UNIDAD", 0, 0}, {1, "Mi unidad", "/usuario", 0} };
static int side_n = 2;
#define SIDE_N side_n
#define SIDE_HEAD_H 28
#define SIDE_ITEM_H 26
static void fm_side_rebuild(void) {
    if (fm_filter_len) return;
    side_n = 2; int dirs = 0;
    for (int i = 0; i < file_count; ++i) if (file_list[i].is_dir) dirs++;
    if (!dirs) return;
    side_rows[side_n].kind = 0; side_rows[side_n].label = "CARPETAS"; side_rows[side_n].path = 0; side_rows[side_n].cat = 0; side_n++;
    for (int i = 0; i < file_count && side_n < SIDE_MAX; ++i) {
        if (!file_list[i].is_dir) continue;
        int k = side_n;
        str_copy(side_lab[k], file_list[i].name, sizeof(side_lab[k])); str_copy(side_pat[k], file_list[i].full_path, sizeof(side_pat[k]));
        side_rows[k].kind = 1; side_rows[k].label = side_lab[k]; side_rows[k].path = side_pat[k]; side_rows[k].cat = 0; side_n++;
    }
}
static int side_y(const struct fl *L, int i) {
    int y = L->body_y + 4;
    for (int k = 0; k < i; ++k) y += side_rows[k].kind == 0 ? SIDE_HEAD_H : SIDE_ITEM_H;
    return y;
}
static int side_is_cur(const struct side_row *s) {
    switch (s->kind) {
    case 1: {
        if (fm_view != FV_RAM) return 0;
        if (str_eq(s->path, "/usuario")) return str_eq(current_folder, s->path);
        return str_starts_with(current_folder, s->path) && (current_folder[str_len(s->path)] == '/' || current_folder[str_len(s->path)] == '\0');
    }
    default: return 0;
    }
}
static int side_hit(const struct fl *L, int32_t mx, int32_t my) {
    if (mx < L->x || mx >= L->x + L->side_w || my < L->body_y || my >= L->body_y + L->body_h) return -1;
    for (int i = 0; i < SIDE_N; ++i) {
        if (side_rows[i].kind == 0) continue;
        int y = side_y(L, i);
        if (y + SIDE_ITEM_H > L->body_y + L->body_h) break;
        if (my >= y && my < y + SIDE_ITEM_H) return i;
    }
    return -1;
}

/* migas de pan */
static void fm_build_crumbs(const struct fl *L) {
    ncr = 0;
    struct fm_crumb *c;
    #define CR_ADD(v, ct, lab, ll, pl) do { if (ncr < 24) { c = &crumbs[ncr++]; c->view = (v); c->cat = (uint8_t)(ct); c->label = (lab); c->llen = (uint8_t)(ll); c->plen = (uint16_t)(pl); c->x = 0; c->w = 0; } } while (0)
    if (fm_view == FV_LIB || fm_view == FV_CAT) {
        CR_ADD(FV_LIB, 0, "Biblioteca", 10, 0);
        if (fm_view == FV_CAT) CR_ADD(FV_CAT, fm_cat, cat_name[fm_cat], str_len(cat_name[fm_cat]), 0);
    } else {
        if (fm_view == FV_RAM) CR_ADD(FV_RAM, 0, "Mi unidad", 9, 8);
        else CR_ADD(FV_BIG, 0, "BIGFS", 5, 1);
        const char *p = current_folder;
        uint32_t pos = (fm_view == FV_RAM && fm_es_unid(current_folder)) ? 8 : 0;
        while (p[pos]) {
            while (p[pos] == '/') pos++;
            if (!p[pos]) break;
            uint32_t st = pos;
            while (p[pos] && p[pos] != '/') pos++;
            CR_ADD(fm_view, 0, p + st, pos - st, pos);
        }
    }
    #undef CR_ADD
    int32_t total = 0;
    for (int i = 0; i < ncr; ++i) {
        char t[72]; uint32_t n = crumbs[i].llen < 70 ? crumbs[i].llen : 70; for (uint32_t k = 0; k < n; ++k) t[k] = crumbs[i].label[k];
        t[n] = '\0';
        crumbs[i].w = ftw(t, 13) + 24; total += crumbs[i].w;
    }
    cr_first = 0;
    while (cr_first < ncr - 1 && total > L->crumb_w - 44) { total -= crumbs[cr_first].w; cr_first++; }
    int32_t x = L->crumb_x + 30 + (cr_first > 0 ? 22 : 0);
    for (int i = cr_first; i < ncr; ++i) { crumbs[i].x = x; x += crumbs[i].w; }
}
static void fm_crumb_go(int i) {
    struct fm_crumb *c = &crumbs[i];
    if (c->view == FV_LIB) { fm_goto(FV_LIB, 0, 0); return; }
    if (c->view == FV_CAT) { fm_goto(FV_CAT, 0, c->cat); return; }
    char path[FM_PATH]; uint32_t n = c->plen;
    if (n == 0 || n >= sizeof(path)) { path[0] = '/'; path[1] = '\0'; }
    else { for (uint32_t k = 0; k < n; ++k) path[k] = current_folder[k]; path[n] = '\0'; }
    fm_goto(c->view, path, 0);
}

/* ---- desplazamiento ---- */
static int fm_total_rows(const struct fl *L) { return (file_count + L->cols - 1) / L->cols; }
static int fm_first_row(const struct fl *L) { return scroll_offset / L->cols; }
static void fm_clamp_scroll(const struct fl *L) {
    int rows = fm_total_rows(L), first = fm_first_row(L), mx = rows - L->rows_vis;
    if (mx < 0) mx = 0;
    if (fm_reveal && selected_idx >= 0 && selected_idx < file_count) {
        int r = selected_idx / L->cols;
        if (r < first) first = r;
        if (r >= first + L->rows_vis) first = r - L->rows_vis + 1;
    }
    fm_reveal = 0;
    if (first > mx) first = mx;
    if (first < 0) first = 0;
    scroll_offset = first * L->cols;
    g_vis_rows = L->vis; g_vis_cols = L->cols;
}
static int fm_sb_thumb(const struct fl *L, int32_t *ty, int32_t *th) {
    int rows = fm_total_rows(L), mx = rows - L->rows_vis, track = L->items_h - 8;
    if (mx <= 0 || track < 20) return 0;
    int h = track * L->rows_vis / rows; if (h < 28) h = 28;
    if (h > track) h = track;
    *th = h; *ty = L->items_y + 4 + (track - h) * fm_first_row(L) / mx;
    return track - h;
}
static void fm_sb_to(const struct fl *L, int32_t my) {
    int32_t ty, th; int span = fm_sb_thumb(L, &ty, &th);
    int mx = fm_total_rows(L) - L->rows_vis;
    if (span <= 0 || mx <= 0) return;
    int pos = my - fm_sb_grab - (L->items_y + 4);
    if (pos < 0) pos = 0;
    if (pos > span) pos = span;
    scroll_offset = ((pos * mx + span / 2) / span) * L->cols;
}
/* elemento bajo el puntero (-1 = ninguno) */
static int fm_item_at(const struct fl *L, int32_t mx, int32_t my) {
    if (mx < L->main_x || mx >= L->sb_x || my < L->items_y || my >= L->items_y + L->items_h) return -1;
    int idx;
    if (fm_mode == 1) {
        int c = (mx - (L->main_x + 12)) / FM_TILE_W, r = (my - L->items_y) / FM_TILE_H;
        if (c < 0 || c >= L->cols) return -1;
        idx = scroll_offset + r * L->cols + c;
    } else idx = scroll_offset + (my - L->items_y) / FM_ROW;
    return (idx >= 0 && idx < file_count) ? idx : -1;
}

/* ---- menu contextual ---- */
enum { FMI_OPEN = 1, FMI_CUT, FMI_COPY, FMI_PASTE, FMI_REN, FMI_DEL, FMI_NFOL, FMI_NFIL, FMI_ORG, FMI_REF, FMI_VIEW, FMI_PATH };
struct fm_mi { const char *label; const char *key; int id; };
static const struct fm_mi fm_menu_item[] = {
    { "Abrir", "Intro", FMI_OPEN }, { 0, 0, 0 },
    { "Cortar", "Ctrl+X", FMI_CUT }, { "Copiar", "Ctrl+C", FMI_COPY }, { "Pegar", "Ctrl+V", FMI_PASTE }, { 0, 0, 0 },
    { "Renombrar", "E", FMI_REN }, { "Eliminar", "Supr", FMI_DEL }, { 0, 0, 0 },
    { "Copiar ruta", 0, FMI_PATH }, { 0, 0, 0 },
    { "Nueva carpeta", "F", FMI_NFOL }, { "Nuevo archivo", "N", FMI_NFIL },
};
static const struct fm_mi fm_menu_bg[] = {
    { "Nueva carpeta", "F", FMI_NFOL }, { "Nuevo archivo", "N", FMI_NFIL }, { 0, 0, 0 },
    { "Pegar", "Ctrl+V", FMI_PASTE }, { 0, 0, 0 },
    { "Organizar por categor\xC3\xAD" "as", "O", FMI_ORG }, { "Actualizar", "R", FMI_REF }, { 0, 0, 0 },
    { "Ver como iconos", "V", FMI_VIEW },
};
#define FM_CTX_W 240
#define FM_CTX_IH 26
#define FM_CTX_SH 9
static const struct fm_mi *fm_menu(int *n) {
    if (fm_ctx_kind == 0) { *n = (int)(sizeof(fm_menu_item) / sizeof(fm_menu_item[0])); return fm_menu_item; }
    *n = (int)(sizeof(fm_menu_bg) / sizeof(fm_menu_bg[0])); return fm_menu_bg;
}
static int fm_ctx_h(void) { int n; const struct fm_mi *m = fm_menu(&n); int h = 12; for (int i = 0; i < n; ++i) h += m[i].id ? FM_CTX_IH : FM_CTX_SH; return h; }
static int fm_real(void) { return fm_view == FV_RAM || fm_view == FV_BIG; }
extern int clip_kind(void) __attribute__((weak));
extern int clip_set_text(const char *s, uint32_t n) __attribute__((weak));
static int fm_menu_enabled(int id) {
    struct fm_item *it = fm_sel_item();
    switch (id) {
    case FMI_OPEN: case FMI_PATH: return it != 0;
    case FMI_CUT: case FMI_COPY: case FMI_REN: case FMI_DEL: return it && it->store != ST_VIRT;
    case FMI_PASTE: return fm_real() && clip_kind && clip_kind() != 0;
    case FMI_NFOL: case FMI_NFIL: case FMI_ORG: return fm_real();
    default: return 1;
    }
}
static int fm_menu_hit(int32_t mx, int32_t my) {
    if (!fm_ctx_open || mx < fm_ctx_x || mx >= fm_ctx_x + FM_CTX_W) return -1;
    int n; const struct fm_mi *m = fm_menu(&n);
    int32_t y = fm_ctx_y + 6;
    for (int i = 0; i < n; ++i) {
        int h = m[i].id ? FM_CTX_IH : FM_CTX_SH;
        if (my >= y && my < y + h) return m[i].id ? i : -1;
        y += h;
    }
    return -1;
}
static int fm_menu_inside(int32_t mx, int32_t my) { return fm_ctx_open && mx >= fm_ctx_x && mx < fm_ctx_x + FM_CTX_W && my >= fm_ctx_y && my < fm_ctx_y + fm_ctx_h(); }
static void fm_draw_menu(void) {
    if (!fm_ctx_open) return;
    int n; const struct fm_mi *m = fm_menu(&n);
    int32_t x = fm_ctx_x, y = fm_ctx_y, h = fm_ctx_h();
    fb_draw_shadow(x, y, FM_CTX_W, h, 9, 18, 6, 0xB0000000u);
    fb_fill_rounded_rect(x, y, FM_CTX_W, h, 9, 0x001A2030);
    fb_stroke_rrect_aa(x, y, FM_CTX_W, h, 9, 0xFF3A465Cu);
    y += 6;
    for (int i = 0; i < n; ++i) {
        if (!m[i].id) { fb_fill_rect((uint32_t)(x + 10), (uint32_t)(y + FM_CTX_SH / 2), FM_CTX_W - 20, 1, 0x002E3A4E); y += FM_CTX_SH; continue; }
        int en = fm_menu_enabled(m[i].id), hot = en && i == fm_ctx_hover;
        const char *lab = m[i].label;
        if (m[i].id == FMI_VIEW) lab = fm_mode == 1 ? "Ver como lista" : "Ver como iconos";
        uint32_t col = !en ? 0x0056627A : (m[i].id == FMI_DEL ? (hot ? 0x00FFFFFF : 0x00F87171) : (hot ? 0x00FFFFFF : 0x00E2E8F0));
        if (hot) fb_fill_rounded_rect(x + 5, y, FM_CTX_W - 10, FM_CTX_IH, 6, m[i].id == FMI_DEL ? 0x00B42332 : KC_PRIMARY);
        ftx(x + 16, y, FM_CTX_IH, lab, 13, col, 0);
        if (m[i].key) ftx(x + FM_CTX_W - 16 - ftw(m[i].key, 12), y, FM_CTX_IH, m[i].key, 12, !en ? 0x00434E63 : (hot ? 0x00D6E9FF : 0x007B8AA3), 0);
        y += FM_CTX_IH;
    }
}

/* ---- iconos de la barra ---- */
#define LN(a, b, c, d) fb_line_aa((a), (b), (c), (d), 26, col)
static void fm_tb_icon(int id, int32_t cx, int32_t cy, uint32_t col) {
    int32_t X = cx * 16, Y = cy * 16;
    switch (id) {
    case FB_BACK: LN(X + 80, Y, X - 72, Y); LN(X - 72, Y, X - 8, Y - 72); LN(X - 72, Y, X - 8, Y + 72); break;
    case FB_FWD:  LN(X - 80, Y, X + 72, Y); LN(X + 72, Y, X + 8, Y - 72); LN(X + 72, Y, X + 8, Y + 72); break;
    case FB_UP:   LN(X, Y + 80, X, Y - 72); LN(X, Y - 72, X - 72, Y - 8); LN(X, Y - 72, X + 72, Y - 8); break;
    case FB_LIST: for (int i = -1; i <= 1; ++i) { LN(X - 88, Y + i * 72, X - 72, Y + i * 72); LN(X - 32, Y + i * 72, X + 96, Y + i * 72); } break;
    case FB_GRID: for (int i = 0; i < 4; ++i) fb_fill_rrect_aa(cx - 7 + (i & 1) * 8, cy - 7 + (i >> 1) * 8, 6, 6, 1, col); break;
    case FB_NFOL:
        fb_stroke_rrect_aa(cx - 9, cy - 6, 18, 13, 2, col); LN(X - 136, Y - 96, X - 40, Y - 96);
        LN(X + 64, Y + 8, X + 64, Y + 8); LN(X, Y + 8, X + 0, Y + 8); LN(X - 40, Y + 16, X + 40, Y + 16); LN(X, Y - 24, X, Y + 56); break;
    case FB_NFIL:
        fb_stroke_rrect_aa(cx - 7, cy - 9, 14, 18, 2, col); LN(X - 48, Y + 16, X + 48, Y + 16); LN(X, Y - 32, X, Y + 64); break;
    case FB_ORG:
        for (int i = 0; i < 4; ++i) { fb_stroke_rrect_aa(cx - 8 + (i & 1) * 9, cy - 8 + (i >> 1) * 9, 7, 7, 2, col); }
        break;
    case FB_REF: {
        static const signed char cs[12][2] = { {50,-87}, {87,-50}, {100,0}, {87,50}, {50,87}, {0,100}, {-50,87}, {-87,50}, {-100,0}, {-87,-50}, {-50,-87}, {0,-100} };
        int r = 104; for (int i = 1; i + 1 < 12; ++i) LN(X + cs[i][0] * r / 100, Y + cs[i][1] * r / 100, X + cs[i + 1][0] * r / 100, Y + cs[i + 1][1] * r / 100);
        LN(X + 56, Y - r, X - 8, Y - r - 58); LN(X + 56, Y - r, X - 8, Y - r + 58); break; }
    default: break;
    }
}
static void fm_search_icon(int32_t cx, int32_t cy, uint32_t col) {
    fb_stroke_rrect_aa(cx - 6, cy - 6, 10, 10, 5, col);
    fb_line_aa((cx + 2) * 16, (cy + 2) * 16, (cx + 6) * 16, (cy + 6) * 16, 26, col);
}
#undef LN

/* ---- barra de uso (lateral) ---- */
static void fm_usage(int32_t x, int32_t y, int32_t w, const char *label, const char *val, uint32_t used, uint32_t total, uint32_t col) {
    ftx(x, y, 16, label, 12, KC_DIM, 0);
    ftx(x + w - ftw(val, 11), y, 16, val, 11, KC_FAINT, 0);
    fb_fill_rrect_aa(x, y + 20, w, 5, 2, 0xFF1E2838u);
    if (total) { int32_t f = (int32_t)((uint64_t)w * (used > total ? total : used) / total); if (f > 0) fb_fill_rrect_aa(x, y + 20, f < 5 ? 5 : f, 5, 2, 0xFF000000u | col); }
}

/* ---- dialogos ---- */
static void fm_modal_rect(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, uint32_t *mx, uint32_t *my, uint32_t *mw, uint32_t *mh) {
    *mw = 440; *mh = (modal_mode == FM_MODAL_ORGANIZE) ? 196 : (modal_mode == FM_MODAL_DELETE_CONFIRM ? 158 : 184);
    *mx = wx + (ww > *mw ? (ww - *mw) / 2 : 10);
    *my = wy + (wh > *mh ? (wh - *mh) / 2 : 10);
}
static uint32_t fm_dg_n = 0, fm_dg_c = 0;       /* SOMA_273_ARCHIVOS: teclas que llegan al dialogo (diagnostico) */
static int fm_pw(const char *s, uint32_t n) { char t[72]; uint32_t i = 0; for (; i < n && i < sizeof(t) - 1 && s[i]; ++i) t[i] = s[i]; t[i] = '\0'; return ftw(t, 14); }
static void fm_draw_modal(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, uint64_t now) {
    uint32_t mx, my, mw, mh; fm_modal_rect(win_x, win_y, win_w, win_h, &mx, &my, &mw, &mh);
    fb_fill_rect_alpha((int32_t)win_x, (int32_t)win_y, (int32_t)win_w, (int32_t)win_h, 0x90060910u);
    fb_draw_shadow((int32_t)mx, (int32_t)my, (int32_t)mw, (int32_t)mh, 12, 24, 10, 0xB0000000u);
    fb_fill_rounded_rect((int32_t)mx, (int32_t)my, (int32_t)mw, (int32_t)mh, 12, 0x00171E2C);
    fb_stroke_rrect_aa((int32_t)mx, (int32_t)my, (int32_t)mw, (int32_t)mh, 12, 0xFF334055u);
    const char *t = "Accion";
    if (modal_mode == FM_MODAL_NEW_FILE)            t = "Nuevo archivo";
    else if (modal_mode == FM_MODAL_NEW_FOLDER)     t = "Nueva carpeta";
    else if (modal_mode == FM_MODAL_RENAME)         t = "Renombrar";
    else if (modal_mode == FM_MODAL_DELETE_CONFIRM) t = "Eliminar";
    else if (modal_mode == FM_MODAL_ORGANIZE)       t = "Organizar por categorias";
    ftx((int32_t)mx + 22, (int32_t)my + 14, 24, t, 16, 0x00F8FAFC, 1);
    int32_t y = (int32_t)my + 50;
    if (modal_mode == FM_MODAL_DELETE_CONFIRM) {
        ftx((int32_t)mx + 22, y, 20, "Se eliminara de forma permanente:", 13, KC_DIM, 0);
        ftx_fit((int32_t)mx + 22, y + 24, 20, modal_target, 13, 0x00FCA5A5, 1, (int)mw - 44);
    } else if (modal_mode == FM_MODAL_ORGANIZE) {
        ftx((int32_t)mx + 22, y, 18, "Los archivos sueltos se moveran a subcarpetas:", 13, KC_DIM, 0);
        ftx((int32_t)mx + 22, y + 20, 18, "Imagenes, Video, Audio, Documentos, Textos,", 13, KC_TXT, 0);
        ftx((int32_t)mx + 22, y + 38, 18, "Programas, Web, Comprimidos y Otros.", 13, KC_TXT, 0);
        ftx_fit((int32_t)mx + 22, y + 60, 18, modal_target, 12, KC_ACC, 0, (int)mw - 44);
        ftx((int32_t)mx + 22, y + 80, 18, "Los archivos del sistema no se tocan. Se puede deshacer.", 12, 0x00FBBF24, 0);
    } else {
        const char *pr = modal_mode == FM_MODAL_RENAME ? "Nuevo nombre" : "Nombre";
        ftx((int32_t)mx + 22, y, 18, pr, 12, KC_DIM, 0);
        int32_t fx = (int32_t)mx + 22, fy = y + 22, fw = (int32_t)mw - 44;
        fb_fill_rounded_rect(fx, fy, fw, 34, 8, KC_FIELD);
        fb_stroke_rrect_aa(fx, fy, fw, 34, 8, modal_err[0] ? 0xFFEF4444u : (0xFF000000u | KC_ACC));
        int32_t tx0 = fx + 12;
        fb_set_clip(fx + 6, fy, fw - 12, 34);
        if (modal_s1 > modal_s0) {                                          /* SOMA_273_ARCHIVOS: tramo seleccionado */
            int32_t x0 = tx0 + fm_pw(modal_input, modal_s0), x1 = tx0 + fm_pw(modal_input, modal_s1);
            fb_fill_rounded_rect(x0 - 1, fy + 6, x1 - x0 + 2, 22, 4, 0x00265E9A);
        }
        ftx(tx0, fy, 34, modal_input, 14, 0x00FFFFFF, 0);
        if (((now / 530) & 1) == 0) fb_fill_rect((uint32_t)(tx0 + fm_pw(modal_input, modal_caret) + 1), (uint32_t)(fy + 8), 2, 18, KC_ACC);
        fb_reset_clip();
        if (modal_err[0]) ftx(fx, fy + 38, 16, modal_err, 12, 0x00FCA5A5, 0);
        else ftx(fx, fy + 38, 16, "Intro: aceptar    Esc: cancelar    Ctrl+A: seleccionar todo", 11, KC_FAINT, 0);
        { char dg[64]; dg[0] = '\0'; ap(dg, sizeof(dg), "teclas recibidas: "); ap_u(dg, sizeof(dg), fm_dg_n); ap(dg, sizeof(dg), "   ultima: ");
          ap_u(dg, sizeof(dg), fm_dg_c); ftx((int32_t)mx + 22, (int32_t)(my + mh - 20), 14, dg, 10, 0x00394456, 0); }   /* diagnostico temporal del parche 273 */
    }
    int del = (modal_mode == FM_MODAL_DELETE_CONFIRM);
    uint32_t ok_x = mx + mw - 210, by = my + mh - 50;
    fb_fill_rounded_rect((int32_t)ok_x, (int32_t)by, 96, 32, 8, del ? 0x00C2283A : KC_PRIMARY);
    const char *okl = del ? "Eliminar" : (modal_mode == FM_MODAL_ORGANIZE ? "Organizar" : "Aceptar");
    ftx((int32_t)ok_x + (96 - ftw(okl, 13)) / 2, (int32_t)by, 32, okl, 13, 0x00FFFFFF, 1);
    uint32_t can_x = mx + mw - 106;
    fb_fill_rounded_rect((int32_t)can_x, (int32_t)by, 86, 32, 8, 0x00252E40);
    ftx((int32_t)can_x + (86 - ftw("Cancelar", 13)) / 2, (int32_t)by, 32, "Cancelar", 13, KC_TXT, 0);
}

/* ---- panel de detalles ---- */
static void fm_det_buttons(const struct fl *L, int32_t *y, int32_t *bw) { *bw = (L->det_w - 28 - 12) / 3; *y = L->st_y - 48; }
static void fm_draw_details(const struct fl *L) {
    int32_t x = L->det_x, y = L->body_y, w = L->det_w, h = L->body_h;
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, KC_SIDE);
    fb_fill_rect((uint32_t)x, (uint32_t)y, 1, (uint32_t)h, KC_LINE);
    struct fm_item *it = fm_sel_item();
    int32_t px = x + 14, pw = w - 28;
    fb_set_clip(x + 1, y, w - 1, h);
    if (!it) {
        int32_t cy = y + h / 2 - 60;
        fm_icon2(x + w / 2 - 28, cy, 56, FC_OTR, 1, 0, 0);
        const char *a = "Selecciona un elemento";
        ftx(x + (w - ftw(a, 14)) / 2, cy + 70, 20, a, 14, KC_TXT, 1);
        const char *b = "para ver sus detalles";
        ftx(x + (w - ftw(b, 12)) / 2, cy + 92, 18, b, 12, KC_DIM, 0);
        const char *c2 = "Doble clic abre - Clic derecho: menu";
        ftx(x + (w - ftw(c2, 11)) / 2, cy + 120, 16, c2, 11, KC_FAINT, 0);
        fb_reset_clip();
        return;
    }
    /* vista previa */
    int32_t box_h = FM_TH_H + 16;
    fb_fill_rounded_rect(px, y + 14, pw, box_h, 10, 0x00090D15);
    if (!it->is_dir && it->cat == FC_IMG) fm_thumb_for(it);
    if (!it->is_dir && it->cat == FC_IMG && fm_th_state == 1) {
        int32_t tx0 = px + (pw - fm_th_w) / 2, ty0 = y + 14 + (box_h - fm_th_h) / 2;
        fb_blit(tx0, ty0, fm_th_px, fm_th_w, fm_th_h, FM_TH_W);
    } else {
        fm_icon2(px + (pw - 80) / 2, y + 14 + (box_h - 80) / 2, 80, it->cat, it->is_dir, it->store == ST_VIRT, it->name);
    }
    int32_t ty = y + 14 + box_h + 14;
    ftx_fit(px, ty, 22, it->name, 15, KC_TXT, 1, pw);
    ty += 26;
    /* etiqueta del tipo */
    const char *kind = it->is_dir ? (it->store == ST_VIRT ? "Categoria" : "Carpeta") : cat_kind[it->cat];
    uint32_t kc = it->is_dir && it->store != ST_VIRT ? 0x00E9B949 : cat_col[it->cat];
    int kw = ftw(kind, 11) + 16;
    fb_fill_rounded_rect(px, ty + 2, kw, 20, 10, fm_mix(kc, 0x00000000, 175));
    ftx(px + 8, ty + 2, 20, kind, 11, kc, 1);
    ty += 32;
    /* propiedades */
    char v[FM_PATH + 16];
    v[0] = '\0';
    if (it->is_dir) { if (it->store == ST_BIG) ap(v, sizeof(v), "--"); else { ap_u(v, sizeof(v), it->sub_count); ap(v, sizeof(v), it->sub_count == 1 ? " elemento" : " elementos"); } }
    else ap_size(v, sizeof(v), it->size);
    ftx(px, ty, 18, "Tama\xC3\xB1o", 12, KC_FAINT, 0); ftx_fit(px + 80, ty, 18, v, 12, KC_TXT, 0, pw - 80); ty += 22;
    v[0] = '\0';
    if (it->store == ST_VIRT) ap(v, sizeof(v), "Biblioteca");
    else { fm_get_parent_folder(it->full_path, v, sizeof(v)); }
    ftx(px, ty, 18, "Ubicaci\xC3\xB3n", 12, KC_FAINT, 0); ftx_fit(px + 80, ty, 18, v, 12, KC_TXT, 0, pw - 80); ty += 22;
    ftx(px, ty, 18, "Almac\xC3\xA9n", 12, KC_FAINT, 0); ftx(px + 80, ty, 18, it->store == ST_BIG ? "BIGFS (disco)" : (it->store == ST_VIRT ? "Indice" : "RamFS"), 12, KC_TXT, 0); ty += 28;
    /* contenido */
    int32_t by, bw; fm_det_buttons(L, &by, &bw);
    if (preview_text[0] && ty + 40 < by && !(it->cat == FC_IMG && fm_th_state == 1)) {
        fb_fill_rect((uint32_t)px, (uint32_t)(ty - 8), (uint32_t)pw, 1, KC_LINE);
        const char *s = preview_text; int lines = 0, maxl = (by - 12 - ty) / 17;
        while (*s && lines < maxl && lines < 8) {
            uint32_t n = 0, last_sp = 0;
            while (s[n]) { if (s[n] == ' ') last_sp = n; char t[200]; uint32_t k = n + 1 < 199 ? n + 1 : 199; for (uint32_t i = 0; i < k; ++i) t[i] = s[i]; t[k] = '\0'; if (ftw(t, 12) > pw) break; n++; }
            if (s[n] && last_sp > 0) n = last_sp + 1;
            if (!n) n = 1;
            ftxn(px, ty, 17, s, n, 12, 0x00A5B1C4, 0);
            s += n; lines++; ty += 17;
        }
    }
    /* acciones */
    int virt = it->store == ST_VIRT;
    const char *ol = it->is_dir ? "Abrir" : (it->cat == FC_PRG ? "Ejecutar" : ((it->cat == FC_AUD || it->cat == FC_VID) ? "Reproducir" : "Abrir"));
    int32_t bx = px;
    fb_fill_rounded_rect(bx, by, bw, 32, 8, fm_hover_ui == FB_OPEN ? 0x000E7FD6 : KC_PRIMARY);
    ftx(bx + (bw - ftw(ol, 12)) / 2, by, 32, ol, 12, 0x00FFFFFF, 1); bx += bw + 6;
    fb_fill_rounded_rect(bx, by, bw, 32, 8, virt ? 0x001A2130 : (fm_hover_ui == FB_REN ? 0x002E3950 : 0x00232C3F));
    ftx(bx + (bw - ftw("Renombrar", 12)) / 2, by, 32, "Renombrar", 12, virt ? KC_FAINT : KC_TXT, 0); bx += bw + 6;
    fb_fill_rounded_rect(bx, by, bw, 32, 8, virt ? 0x001A2130 : (fm_hover_ui == FB_DEL ? 0x00B42332 : 0x003A1E26));
    ftx(bx + (bw - ftw("Eliminar", 12)) / 2, by, 32, "Eliminar", 12, virt ? KC_FAINT : (fm_hover_ui == FB_DEL ? 0x00FFFFFF : 0x00FCA5A5), 0);
    fb_reset_clip();
}
static int fm_det_hit(const struct fl *L, int32_t mx, int32_t my) {
    if (!L->det_w || !fm_sel_item()) return -1;
    int32_t by, bw; fm_det_buttons(L, &by, &bw);
    if (my < by || my >= by + 32) return -1;
    int32_t bx = L->det_x + 14;
    if (mx >= bx && mx < bx + bw) return FB_OPEN;
    bx += bw + 6; if (mx >= bx && mx < bx + bw) return FB_REN;
    bx += bw + 6; if (mx >= bx && mx < bx + bw) return FB_DEL;
    return -1;
}
static int fm_tb_hit(const struct fl *L, int32_t mx, int32_t my) {
    if (my < L->by || my >= L->by + FM_BTN) return -1;
    for (int i = 0; i < FB_N; ++i) if (mx >= L->bx[i] && mx < L->bx[i] + FM_BTN) return i;
    if (L->srch_w && mx >= L->srch_x && mx < L->srch_x + L->srch_w) return FB_SRCH;
    if (mx >= L->crumb_x && mx < L->crumb_x + L->crumb_w) return FB_CRUMB;
    return -1;
}
static int fm_btn_enabled(int i) {
    switch (i) {
    case FB_BACK: return fm_hpos > 0;
    case FB_FWD: return fm_hpos + 1 < fm_hn;
    case FB_UP: return !(fm_view == FV_LIB || ((fm_view == FV_RAM || fm_view == FV_BIG) && str_eq(current_folder, "/")));
    case FB_NFOL: case FB_NFIL: case FB_ORG: return fm_real();
    default: return 1;
    }
}
static int fm_undo_x(const struct fl *L) { return L->x + L->w - 10 - 92; }

/* ====================================================================
 * RENDERIZADO
 * ==================================================================== */
void fm_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor) {
    fm_gt_budget = 1;
    (void)show_cursor;
    struct fl L; fl_make(&L, win_x, win_y, win_w, win_h);
    fm_hist_sync();
    fm_clamp_scroll(&L);
    uint64_t now = timer_get_uptime_ms();
    fb_fill_rect(win_x, win_y, win_w, win_h, KC_WIN);

    /* 1. barra de herramientas */
    fb_fill_rect(win_x, win_y, win_w, FM_TB_H, KC_BAR);
    fb_fill_rect(win_x, win_y + FM_TB_H - 1, win_w, 1, KC_LINE);
    for (int i = 0; i < FB_N; ++i) {
        int en = fm_btn_enabled(i), on = (i == FB_LIST && fm_mode == 0) || (i == FB_GRID && fm_mode == 1);
        int32_t bx = L.bx[i];
        if (i == FB_LIST) fb_fill_rounded_rect(bx - 2, L.by - 2, FM_BTN * 2 + 4, FM_BTN + 4, 9, 0x000C1119);
        if (on) fb_fill_rounded_rect(bx, L.by, FM_BTN, FM_BTN, 8, 0x00283449);
        else if (en && fm_hover_ui == i) fb_fill_rounded_rect(bx, L.by, FM_BTN, FM_BTN, 8, 0x00222B3D);
        uint32_t col = !en ? 0xFF404B5Eu : (on || fm_hover_ui == i ? 0xFFFFFFFFu : 0xFFC4CDDAu);
        if (i == FB_ORG && en) col = fm_hover_ui == i ? 0xFFFFFFFFu : 0xFFA5B4FCu;
        fm_tb_icon(i, bx + FM_BTN / 2, L.by + FM_BTN / 2, col);
    }
    /* ruta */
    fb_fill_rounded_rect(L.crumb_x, L.by, L.crumb_w, FM_BTN, FM_BTN / 2, KC_FIELD);
    fb_stroke_rrect_aa(L.crumb_x, L.by, L.crumb_w, FM_BTN, FM_BTN / 2, fm_hover_ui == FB_CRUMB ? 0xFF3A4760u : 0xFF263142u);
    fm_build_crumbs(&L);
    fm_icon2(L.crumb_x + 10, L.by + 8, 16, fm_view == FV_CAT ? fm_cat : FC_OTR, 1, fm_view == FV_CAT || fm_view == FV_LIB, 0);
    fb_set_clip(L.crumb_x + 4, L.by, L.crumb_w - 12, FM_BTN);
    if (cr_first > 0) ftx(L.crumb_x + 30, L.by, FM_BTN, "...", 13, KC_FAINT, 0);
    for (int i = cr_first; i < ncr; ++i) {
        int last = (i == ncr - 1);
        ftxn(crumbs[i].x, L.by, FM_BTN, crumbs[i].label, crumbs[i].llen, 13, last ? KC_TXT : KC_DIM, last);
        if (!last) { int32_t cx = crumbs[i].x + crumbs[i].w - 13, cy = L.by + FM_BTN / 2; fb_line_aa(cx * 16, (cy - 4) * 16, (cx + 4) * 16, cy * 16, 20, 0xFF4B5873u); fb_line_aa((cx + 4) * 16, cy * 16, cx * 16, (cy + 4) * 16, 20, 0xFF4B5873u); }
    }
    fb_reset_clip();
    /* busqueda */
    if (L.srch_w) {
        fb_fill_rounded_rect(L.srch_x, L.by, L.srch_w, FM_BTN, FM_BTN / 2, KC_FIELD);
        fb_stroke_rrect_aa(L.srch_x, L.by, L.srch_w, FM_BTN, FM_BTN / 2, fm_search_active ? (0xFF000000u | KC_ACC) : (fm_hover_ui == FB_SRCH ? 0xFF3A4760u : 0xFF263142u));
        fm_search_icon(L.srch_x + 16, L.by + FM_BTN / 2 - 1, fm_search_active ? (0xFF000000u | KC_ACC) : 0xFF6B7890u);
        fb_set_clip(L.srch_x + 30, L.by, L.srch_w - 52, FM_BTN);
        if (fm_filter_len || fm_search_active) {
            ftx(L.srch_x + 32, L.by, FM_BTN, fm_filter, 13, 0x00FFFFFF, 0);
            if (fm_search_active && ((now / 530) & 1) == 0) fb_fill_rect((uint32_t)(L.srch_x + 33 + ftw(fm_filter, 13)), (uint32_t)(L.by + 8), 2, 16, KC_ACC);
        } else ftx(L.srch_x + 32, L.by, FM_BTN, "Buscar en esta carpeta", 13, KC_FAINT, 0);
        fb_reset_clip();
        if (fm_filter_len) { int32_t cx = L.srch_x + L.srch_w - 16, cy = L.by + FM_BTN / 2; fb_line_aa((cx - 4) * 16, (cy - 4) * 16, (cx + 4) * 16, (cy + 4) * 16, 22, 0xFF8A97ADu); fb_line_aa((cx - 4) * 16, (cy + 4) * 16, (cx + 4) * 16, (cy - 4) * 16, 22, 0xFF8A97ADu); }
    }

    /* 2. barra lateral */
    fb_fill_rect((uint32_t)L.x, (uint32_t)L.body_y, (uint32_t)L.side_w, (uint32_t)L.body_h, KC_SIDE);
    fb_fill_rect((uint32_t)(L.x + L.side_w - 1), (uint32_t)L.body_y, 1, (uint32_t)L.body_h, KC_LINE);
    int32_t usage_h = 0;      /* SOMA_273_ARCHIVOS: sin medidores de RamFS/BIGFS; el explorador solo muestra Mi unidad */
    fb_set_clip(L.x, L.body_y, L.side_w - 1, L.body_h - usage_h);
    for (int i = 0; i < SIDE_N; ++i) {
        const struct side_row *s = &side_rows[i];
        int y = side_y(&L, i);
        if (s->kind == 0) { ftx(L.x + 16, y + 6, SIDE_HEAD_H - 6, s->label, 11, KC_FAINT, 1); continue; }
        int cur = side_is_cur(s), hov = (fm_hover_side == i);
        if (cur) fb_fill_rounded_rect(L.x + 8, y + 1, L.side_w - 16, SIDE_ITEM_H - 2, 7, KC_SELBG);
        else if (hov) fb_fill_rounded_rect(L.x + 8, y + 1, L.side_w - 16, SIDE_ITEM_H - 2, 7, KC_HOVER);
        uint32_t tc = cur ? 0x00FFFFFF : 0x00C9D2DF;
        if (s->kind == 4) {
            fb_fill_rrect_aa(L.x + 20, y + SIDE_ITEM_H / 2 - 4, 8, 8, 4, 0xFF000000u | cat_col[s->cat]);
            ftx(L.x + 36, y, SIDE_ITEM_H, cat_name[s->cat], 13, tc, 0);
            if (lib_built) { char cb[12]; cb[0] = '\0'; ap_u(cb, sizeof(cb), (uint64_t)lib_cnt[s->cat]); ftx(L.x + L.side_w - 18 - ftw(cb, 11), y, SIDE_ITEM_H, cb, 11, KC_FAINT, 0); }
        } else {
            int ic = s->kind == 3 ? FC_DOC : FC_OTR;
            if (s->kind == 2) { fb_fill_rrect_aa(L.x + 17, y + 7, 15, 12, 3, 0xFF6366F1u); fb_fill_rect((uint32_t)(L.x + 20), (uint32_t)(y + 14), 9, 2, 0x00C7D2FE); }
            else fm_icon2(L.x + 17, y + 5, 15, ic, 1, s->kind == 3, 0);
            ftx(L.x + 40, y, SIDE_ITEM_H, s->label, 13, tc, cur);
        }
    }
    fb_reset_clip();
    if (L.body_h > 300) {                                           /* SOMA_273_ARCHIVOS: ayuda discreta */
        ftx(L.x + 16, L.body_y + L.body_h - 52, 16, "Clic derecho para", 11, KC_FAINT, 0);
        ftx(L.x + 16, L.body_y + L.body_h - 36, 16, "crear, renombrar,", 11, KC_FAINT, 0);
        ftx(L.x + 16, L.body_y + L.body_h - 20, 16, "copiar o eliminar", 11, KC_FAINT, 0);
    }
    if (usage_h) {
        int32_t ux = L.x + 16, uw = L.side_w - 32, uy = L.body_y + L.body_h - usage_h + 6;
        uint32_t f = 0, b = 0, d = 0; vfs_get_stats(&f, &b, &d); (void)b; (void)d;
        char v[48]; v[0] = '\0'; ap_u(v, sizeof(v), f); ap(v, sizeof(v), " / "); ap_u(v, sizeof(v), FS_MAX_FILES);
        fm_usage(ux, uy, uw, "RamFS", v, f, FS_MAX_FILES, f * 10 > FS_MAX_FILES * 8 ? 0x00F59E0B : KC_ACC);
        v[0] = '\0';
        uint64_t tot = 0, fre = 0;
        if (fm_big_state == 1) { struct bigfs *fs = fm_big(); if (fs) { struct bigfs_info bi; bigfs_info(fs, &bi); uint64_t cb = bi.cluster_bytes ? bi.cluster_bytes : BIGFS_CLUSTER; tot = bi.total_clusters * cb; fre = bi.free_clusters * cb; ap_size(v, sizeof(v), fre); ap(v, sizeof(v), " libres"); } }
        else ap(v, sizeof(v), fm_big_state == 2 ? "sin formatear" : (fm_big_state == 3 ? "no disponible" : "--"));
        fm_usage(ux, uy + 34, uw, "BIGFS", v, tot ? (uint32_t)((tot - fre) >> 10) : 0, (uint32_t)(tot >> 10), 0x008B5CF6);
    }

    /* 3. lista / iconos */
    int32_t mx0 = L.main_x, mw = L.main_w;
    fb_fill_rect((uint32_t)mx0, (uint32_t)L.body_y, (uint32_t)mw, (uint32_t)L.body_h, KC_WIN);
    int bib = (fm_view == FV_CAT || (fm_view == FV_LIB && fm_filter_len));
    if (fm_mode == 0) {
        fb_fill_rect((uint32_t)mx0, (uint32_t)L.hdr_y, (uint32_t)mw, FM_HDR, KC_WIN);
        fb_fill_rect((uint32_t)(mx0 + 10), (uint32_t)(L.hdr_y + FM_HDR - 1), (uint32_t)(mw - 20), 1, KC_LINE);
        int sn = (sort_mode <= FM_SORT_NAME_DESC), sz = (sort_mode == FM_SORT_SIZE_ASC || sort_mode == FM_SORT_SIZE_DESC), stp = (sort_mode >= FM_SORT_TYPE_ASC);
        int asc = (sort_mode == FM_SORT_NAME_ASC || sort_mode == FM_SORT_SIZE_ASC || sort_mode == FM_SORT_TYPE_ASC);
        const char *hn[3] = { "Nombre", bib ? "Ubicaci\xC3\xB3n" : "Tipo", "Tama\xC3\xB1o" };
        int32_t hx[3] = { mx0 + 44, L.col_type, L.col_size };
        int act[3] = { sn, stp && !bib, sz };
        for (int k = 0; k < 3; ++k) {
            if (k == 1 && !L.has_type) continue;
            ftx(hx[k], L.hdr_y, FM_HDR, hn[k], 12, act[k] ? KC_ACC : KC_DIM, act[k]);
            if (act[k]) { int32_t ax = hx[k] + ftw(hn[k], 12) + 12, ay = L.hdr_y + FM_HDR / 2; uint32_t c = 0xFF000000u | KC_ACC;
                if (asc) { fb_line_aa((ax - 4) * 16, (ay + 2) * 16, ax * 16, (ay - 2) * 16, 20, c); fb_line_aa(ax * 16, (ay - 2) * 16, (ax + 4) * 16, (ay + 2) * 16, 20, c); }
                else { fb_line_aa((ax - 4) * 16, (ay - 2) * 16, ax * 16, (ay + 2) * 16, 20, c); fb_line_aa(ax * 16, (ay + 2) * 16, (ax + 4) * 16, (ay - 2) * 16, 20, c); } }
        }
    }
    fb_set_clip(mx0, L.items_y, mw - 12, L.items_h);
    if (file_count == 0) {
        int32_t cy = L.items_y + L.items_h / 2 - 70;
        fm_icon2(mx0 + mw / 2 - 32, cy, 64, FC_OTR, 1, 0, 0);
        const char *a = "Esta carpeta est\xC3\xA1 vac\xC3\xAD" "a", *b = "Clic derecho para crear una carpeta o un archivo";
        if (fm_view == FV_BIG && fm_big_state == 2) { a = "El almacen BIGFS no esta formateado"; b = "En el terminal ejecuta:  big format yes"; }
        else if (fm_view == FV_BIG && fm_big_state == 3) { a = "BIGFS no disponible"; b = bigfs_strerror(fm_big_err); }
        else if (fm_filter_len) { a = "Sin resultados"; b = "Prueba con otro texto de busqueda"; }
        else if (!fm_real()) b = "No hay archivos de este tipo";
        ftx(mx0 + (mw - ftw(a, 15)) / 2, cy + 78, 22, a, 15, KC_TXT, 1);
        ftx(mx0 + (mw - ftw(b, 12)) / 2, cy + 102, 18, b, 12, KC_DIM, 0);
    } else if (fm_mode == 0) {
        for (int r = 0; r < L.rows_vis + 1; ++r) {
            int idx = scroll_offset + r;
            if (idx >= file_count) break;
            int32_t ry = L.items_y + r * FM_ROW;
            struct fm_item *it = &file_list[idx];
            int sel = (idx == selected_idx), hov = (idx == fm_hover);
            if (sel) fb_fill_rounded_rect(mx0 + 8, ry + 1, mw - 28, FM_ROW - 2, 7, KC_SELBG);
            else if (hov) fb_fill_rounded_rect(mx0 + 8, ry + 1, mw - 28, FM_ROW - 2, 7, KC_HOVER);
            fm_icon2(mx0 + 16, ry + 5, 20, it->cat, it->is_dir, it->store == ST_VIRT, it->name);
            int32_t name_w = (L.has_type ? L.col_type : L.col_size) - (mx0 + 44) - 12;
            ftx_fit(mx0 + 44, ry, FM_ROW, it->name, 13, sel ? 0x00FFFFFF : KC_TXT, 0, name_w);
            if (L.has_type) {
                char tb[FM_PATH + 8]; tb[0] = '\0';
                if (bib) { if (it->store == ST_BIG) ap(tb, sizeof(tb), "BIGFS:"); char dir[FM_PATH]; fm_get_parent_folder(it->full_path, dir, sizeof(dir)); ap(tb, sizeof(tb), dir); }
                else if (it->is_dir) ap(tb, sizeof(tb), it->store == ST_VIRT ? "Categoria" : "Carpeta");
                else ap(tb, sizeof(tb), cat_kind[it->cat]);
                ftx_fit(L.col_type, ry, FM_ROW, tb, 12, sel ? 0x00C8D7EA : KC_DIM, 0, L.col_size - L.col_type - 12);
            }
            char sb[32]; sb[0] = '\0';
            if (it->is_dir) { if (it->store == ST_BIG) ap(sb, sizeof(sb), "--"); else { ap_u(sb, sizeof(sb), it->sub_count); ap(sb, sizeof(sb), it->sub_count == 1 ? " elemento" : " elementos"); } }
            else ap_size(sb, sizeof(sb), it->size);
            ftx(L.col_size, ry, FM_ROW, sb, 12, sel ? 0x00C8D7EA : KC_DIM, 0);
        }
    } else {
        for (int r = 0; r < L.rows_vis + 1; ++r) for (int c = 0; c < L.cols; ++c) {
            int idx = scroll_offset + r * L.cols + c;
            if (idx >= file_count) break;
            struct fm_item *it = &file_list[idx];
            int32_t tx0 = mx0 + 12 + c * FM_TILE_W, ty0 = L.items_y + r * FM_TILE_H;
            int sel = (idx == selected_idx), hov = (idx == fm_hover);
            if (sel) { fb_fill_rounded_rect(tx0 + 3, ty0 + 2, FM_TILE_W - 6, FM_TILE_H - 4, 10, KC_SELBG); fb_stroke_rrect_aa(tx0 + 3, ty0 + 2, FM_TILE_W - 6, FM_TILE_H - 4, 10, 0xFF2F6FB0u); }
            else if (hov) fb_fill_rounded_rect(tx0 + 3, ty0 + 2, FM_TILE_W - 6, FM_TILE_H - 4, 10, KC_HOVER);
            {
                const struct fm_gthumb *gt = fm_grid_thumb(it);
                if (gt) {                                  /* miniatura real con esquinas redondeadas */
                    int32_t ix = tx0 + (FM_TILE_W - gt->w) / 2, iy = ty0 + 10 + (48 - gt->h) / 2;
                    uint32_t cs[4 * 6 * 6]; int ok = fb_corners_save(ix, iy, gt->w, gt->h, 6, cs);
                    fb_blit(ix, iy, gt->px, gt->w, gt->h, FM_GT);
                    if (ok) fb_corners_restore(ix, iy, gt->w, gt->h, 6, cs);
                    fb_stroke_rrect_aa(ix, iy, gt->w, gt->h, 6, 0x40FFFFFFu);
                } else fm_icon2(tx0 + (FM_TILE_W - 48) / 2, ty0 + 10, 48, it->cat, it->is_dir, it->store == ST_VIRT, it->name);
            }
            int nw = ftw(it->name, 12), maxw = FM_TILE_W - 14;
            if (nw > maxw) ftx_fit(tx0 + 7, ty0 + 64, 20, it->name, 12, sel ? 0x00FFFFFF : KC_TXT, 0, maxw);
            else ftx(tx0 + (FM_TILE_W - nw) / 2, ty0 + 64, 20, it->name, 12, sel ? 0x00FFFFFF : KC_TXT, 0);
            char sb[32]; sb[0] = '\0';
            if (it->is_dir) { if (it->store != ST_BIG) { ap_u(sb, sizeof(sb), it->sub_count); ap(sb, sizeof(sb), " elem."); } }
            else ap_size(sb, sizeof(sb), it->size);
            if (sb[0]) ftx(tx0 + (FM_TILE_W - ftw(sb, 10)) / 2, ty0 + 82, 16, sb, 10, KC_FAINT, 0);
        }
    }
    fb_reset_clip();
    /* barra de desplazamiento */
    { int32_t ty, th; if (fm_sb_thumb(&L, &ty, &th)) fb_fill_rrect_aa(L.sb_x + 3, ty, 6, th, 3, fm_sb_drag ? 0xFF38BDF8u : 0xFF3A465Cu); }

    /* 4. detalles */
    if (L.det_w) fm_draw_details(&L);

    /* 5. barra de estado */
    fb_fill_rect(win_x, (uint32_t)L.st_y, win_w, FM_ST_H, KC_BAR);
    fb_fill_rect(win_x, (uint32_t)L.st_y, win_w, 1, KC_LINE);
    int32_t right = L.x + L.w - 10;
    if (undo_n > 0) {
        int32_t ux = fm_undo_x(&L);
        fb_fill_rounded_rect(ux, L.st_y + 3, 92, FM_ST_H - 6, 10, fm_hover_ui == FB_UNDO ? 0x00B45309 : 0x00402A10);
        ftx(ux + (92 - ftw("Deshacer", 12)) / 2, L.st_y + 3, FM_ST_H - 6, "Deshacer", 12, 0x00FDE68A, 1);
        right = ux - 10;
    }
    fb_set_clip(L.x, L.st_y, right - L.x, FM_ST_H);
    {
        char sb[200]; sb[0] = '\0'; uint32_t col = KC_DIM;
        if (fm_msg[0] && now < fm_msg_until) { str_copy(sb, fm_msg, sizeof(sb)); col = 0x00FDE68A; }
        else if (fm_hover_ui >= 0 && fm_hover_ui < FB_N) { str_copy(sb, fb_tip[fm_hover_ui], sizeof(sb)); col = KC_TXT; }
        else {
            ap_u(sb, sizeof(sb), (uint64_t)file_count); ap(sb, sizeof(sb), file_count == 1 ? " elemento" : " elementos");
            struct fm_item *it = fm_sel_item();
            if (it) { ap(sb, sizeof(sb), "   -   "); ap(sb, sizeof(sb), it->name); if (!it->is_dir) { ap(sb, sizeof(sb), " ("); ap_size(sb, sizeof(sb), it->size); ap(sb, sizeof(sb), ")"); } }
            if (lib_truncated && (fm_view == FV_LIB || fm_view == FV_CAT)) ap(sb, sizeof(sb), "   (indice limitado)");
        }
        ftx_fit(L.x + 14, L.st_y, FM_ST_H, sb, 12, col, 0, right - L.x - 20);
    }
    fb_reset_clip();

    /* 6. menu y dialogo */
    fm_draw_menu();
    if (modal_mode != FM_MODAL_NONE) fm_draw_modal(win_x, win_y, win_w, win_h, now);
}

/* ====================================================================
 * ACCIONES: ABRIR, CREAR, BORRAR, RENOMBRAR, ORGANIZAR
 * ==================================================================== */
static int fm_stage(const struct fm_item *it, char *out, uint32_t max) {
    if (it->store == ST_RAM) { str_copy(out, it->full_path, max); return 0; }
    if (it->size > 16u * 1024u * 1024u) { fm_say("Archivo demasiado grande para abrirlo (max 16 MiB)"); return -1; }   /* PARCHE 087 */
    char *buf = (char *)kmalloc((size_t)it->size + 1);
    if (!buf) { fm_say("Memoria insuficiente"); return -1; }
    int64_t r = fm_read_head(it->store, it->full_path, buf, (uint32_t)it->size);
    if (r < 0) { kfree(buf); fm_say("No se pudo leer el archivo"); return -1; }
    char dst[48]; str_copy(dst, "/tmp/fm_vista", sizeof(dst));
    const char *dot = 0;
    for (const char *s = it->name; *s; ++s) if (*s == '.') dot = s;
    if (dot && str_len(dot) <= 8) ap(dst, sizeof(dst), dot);
    int w = vfs_write(dst, buf, (uint32_t)r);
    kfree(buf);
    if (w < 0) { fm_say("No se pudo preparar la copia temporal"); return -1; }
    str_copy(out, dst, max);
    fm_say("Abierta una copia temporal (los cambios no vuelven a BIGFS)");
    return 0;
}

/* gui_open_imgview lo anade este parche a gui.c; si falta (otra version de gui.c) se usa el camino antiguo (codigo 2) */
extern void gui_open_imgview(const char *path) __attribute__((weak));
#define FM_ACT_EXT 5     /* la GUI ya ha abierto otra ventana: que repinte todo en vez de dibujar el explorador encima */

static int fm_open_item(struct fm_item *it, char *out_path, uint32_t max_out) {
    if (it->is_dir) {
        if (it->store == ST_VIRT) fm_goto(FV_CAT, 0, it->cat);
        else fm_goto(it->store == ST_BIG ? FV_BIG : FV_RAM, it->full_path, 0);
        return 0;
    }
    switch (it->cat) {
    case FC_AUD:
    case FC_VID:
        if (it->store != ST_BIG) { fm_say("El reproductor solo abre audio/video del almacen BIGFS"); return 0; }
        if (!fm_has_ext(it->name, "avi") && !fm_has_ext(it->name, "wav")) { fm_say("El reproductor solo admite AVI (MJPG+PCM) y WAV"); return 0; }
        gui_open_media(it->full_path);
        return FM_ACT_EXT;
    case FC_PRG: {
        char cmd[FM_PATH + 16], dummy[96]; dummy[0] = '\0';
        if (fm_has_ext(it->name, "soma")) {
            char p[48];
            if (fm_stage(it, p, sizeof(p)) < 0) return 0;
            str_copy(cmd, "app ", sizeof(cmd)); ap(cmd, sizeof(cmd), p);
            dispatch_command(cmd, dummy, sizeof(dummy));
            return 0;
        }
        if (it->store == ST_BIG) {
            str_copy(cmd, "exec /big", sizeof(cmd)); ap(cmd, sizeof(cmd), it->full_path);
            dispatch_command(cmd, dummy, sizeof(dummy));
            fm_say(dummy[0] ? dummy : "Programa ejecutado");
        } else fm_say("Solo se pueden ejecutar desde aqui los .soma y los ELF de BIGFS");
        return 0;
    }
    case FC_ZIP:
        fm_say("Archivo comprimido: no hay descompresor en SOMA todavia");
        return 0;
    default:
        break;
    }
    char p[FM_PATH];
    if (fm_stage(it, p, sizeof(p)) < 0) return 0;
    if (it->cat == FC_IMG) {
        if (gui_open_imgview) { gui_open_imgview(p); return FM_ACT_EXT; }
        str_copy(out_path, p, max_out);
        return 2;
    }
    if (it->cat == FC_WEB && (fm_has_ext(it->name, "html") || fm_has_ext(it->name, "htm"))) { gui_open_browser(p); return FM_ACT_EXT; }
    if (fm_has_ext(it->name, "tex") || fm_has_ext(it->name, "latex")) { gui_open_layout_file(p); return FM_ACT_EXT; }   /* PARCHE 084 */
    if (fm_has_ext(it->name, "pdf")) { gui_open_pdf(p); return FM_ACT_EXT; }                                          /* PARCHE 088 */
    gui_open_notepad(p);
    return FM_ACT_EXT;
}

static void fm_select_name(const char *name) {
    int i = fm_find_item_index(name);
    if (i >= 0) fm_set_sel(i); else fm_update_preview(-1);
}

/* SOMA_273_ARCHIVOS: nombre sugerido y unico ("Nueva carpeta", "Nueva carpeta 2"...), seleccionado para sustituirlo al teclear */
static void fm_ed_load(struct fm_ed *e) { e->t = modal_input; e->cap = sizeof(modal_input); e->len = modal_input_len; e->caret = modal_caret; e->s0 = modal_s0; e->s1 = modal_s1; }
static void fm_ed_store(const struct fm_ed *e) { modal_input_len = e->len; modal_caret = e->caret; modal_s0 = e->s0; modal_s1 = e->s1; }
static void fm_default_name(int folder) {
    char nm[64];
    for (int n = 1; n < 100; ++n) {
        nm[0] = '\0';
        ap(nm, sizeof(nm), folder ? "Nueva carpeta" : "Nuevo archivo");
        if (n > 1) { ap(nm, sizeof(nm), " "); ap_u(nm, sizeof(nm), (uint64_t)n); }
        if (!folder) ap(nm, sizeof(nm), ".txt");
        if (fm_find_item_index(nm) < 0) break;
    }
    struct fm_ed e; fm_ed_set(&e, modal_input, sizeof(modal_input), nm); fm_ed_select_base(&e, folder); fm_ed_store(&e);
    modal_err[0] = '\0';
}
static void fm_new_item(int folder) {
    if (!(fm_view == FV_RAM || fm_view == FV_BIG)) { fm_say("Abre una carpeta real (p. ej. Workspace) para crear elementos"); return; }
    modal_mode = folder ? FM_MODAL_NEW_FOLDER : FM_MODAL_NEW_FILE;
    fm_default_name(folder);
}

static void fm_ask_delete(void) {
    struct fm_item *it = fm_sel_item();
    if (!it) return;
    if (it->store == ST_VIRT) { fm_say("Las categorias de la Biblioteca no se pueden borrar"); return; }
    modal_mode = FM_MODAL_DELETE_CONFIRM;
    if (it->store == ST_BIG) { str_copy(modal_target, "BIGFS:", sizeof(modal_target)); ap(modal_target, sizeof(modal_target), it->full_path); }
    else str_copy(modal_target, it->full_path, sizeof(modal_target));
}
static void fm_ask_rename(void) {
    struct fm_item *it = fm_sel_item();
    if (!it) return;
    if (it->store == ST_VIRT) { fm_say("Las categorias de la Biblioteca no se pueden renombrar"); return; }
    modal_mode = FM_MODAL_RENAME;
    { struct fm_ed e; fm_ed_set(&e, modal_input, sizeof(modal_input), it->name); fm_ed_select_base(&e, it->is_dir); fm_ed_store(&e); modal_err[0] = '\0'; }
    str_copy(modal_target, it->full_path, sizeof(modal_target));
}
static void fm_ask_organize(void) {
    if (!(fm_view == FV_RAM || fm_view == FV_BIG)) { fm_say("Abre una carpeta real (p. ej. Workspace) y pulsa Organizar"); return; }
    if (fm_view == FV_RAM && fm_is_sysdir(current_folder)) { fm_say("Es una carpeta del sistema: no se organiza"); return; }
    modal_mode = FM_MODAL_ORGANIZE;
    modal_target[0] = '\0';
    if (fm_view == FV_BIG) ap(modal_target, sizeof(modal_target), "BIGFS:");
    ap(modal_target, sizeof(modal_target), current_folder);
}

static void fm_undo_push(int store, const char *from, const char *to) {
    if (undo_n >= UNDO_MAX) return;
    str_copy(undo_log[undo_n].from, from, FM_PATH);
    str_copy(undo_log[undo_n].to, to, FM_PATH);
    undo_log[undo_n].store = (uint8_t)store;
    undo_n++;
}

static void fm_organize(void) {
    int store = (fm_view == FV_BIG) ? ST_BIG : ST_RAM;
    int moved = 0, skipped = 0, failed = 0, limit = 0;
    undo_n = 0;
    if (store == ST_RAM) {
        uint32_t cl = str_len(current_folder);
        int root = str_eq(current_folder, "/");
        const char *p = fm_ram_list_begin();
        char fn[80];
        static char names[4096]; uint32_t np = 0;
        while (p && fm_ram_next(&p, fn, sizeof(fn))) {
            const char *rel = 0;
            if (root) { if (fn[0] == '/') rel = fn + 1; }
            else if (str_starts_with(fn, current_folder) && fn[cl] == '/') rel = fn + cl + 1;
            if (!rel || !*rel) continue;
            int deep = 0; for (const char *s = rel; *s; ++s) if (*s == '/') { deep = 1; break; }
            if (deep) continue;
            uint32_t l = str_len(fn);
            if (np + l + 2 < sizeof(names)) { for (uint32_t i = 0; i <= l; ++i) names[np++] = fn[i]; }
        }
        for (uint32_t o = 0; o < np; ) {
            const char *src = names + o;
            o += str_len(src) + 1;
            const char *nm = fm_base(src);
            if (fm_is_protected(nm)) { skipped++; continue; }
            int cat = fm_cat_of(nm);
            if (str_eq(fm_base(current_folder), cat_name[cat])) continue;
            char dir[FM_PATH], dst[FM_PATH];
            fm_build_path(current_folder, cat_name[cat], dir, sizeof(dir));
            fm_build_path(dir, nm, dst, sizeof(dst));
            if (undo_n >= UNDO_MAX) { limit = 1; break; }
            if (fm_move_ram(src, dst) == 0) { fm_undo_push(ST_RAM, src, dst); moved++; } else failed++;
        }
    } else {
        struct bigfs *fs = fm_big();
        uint32_t dir;
        if (fs && bigfs_lookup(fs, current_folder, &dir) == (int)BIGFS_T_DIR) {
            static char names[4096]; uint32_t np = 0;
            uint32_t cur = 0; struct bigfs_stat st;
            while (bigfs_readdir(fs, dir, &cur, &st) == 1) {
                if (st.type != BIGFS_T_FILE) continue;
                uint32_t l = str_len(st.name);
                if (np + l + 2 < sizeof(names)) { for (uint32_t i = 0; i <= l; ++i) names[np++] = st.name[i]; }
            }
            for (uint32_t o = 0; o < np; ) {
                const char *nm = names + o;
                o += str_len(nm) + 1;
                if (fm_is_protected(nm)) { skipped++; continue; }
                int cat = fm_cat_of(nm);
                if (str_eq(fm_base(current_folder), cat_name[cat])) continue;
                char cdir[FM_PATH], src[FM_PATH], dst[FM_PATH];
                fm_build_path(current_folder, cat_name[cat], cdir, sizeof(cdir));
                fm_build_path(current_folder, nm, src, sizeof(src));
                fm_build_path(cdir, nm, dst, sizeof(dst));
                if (undo_n >= UNDO_MAX) { limit = 1; break; }
                int mr = bigfs_mkdir(fs, cdir);
                if (mr != 0 && mr != BIGFS_E_EXIST) { failed++; continue; }
                if (bigfs_rename(fs, src, dst) == 0) { fm_undo_push(ST_BIG, src, dst); moved++; } else failed++;
            }
        } else failed++;
    }
    lib_dirty = 1;
    fm_reload();
    char m[112]; m[0] = '\0';
    ap(m, sizeof(m), "Organizado: "); ap_u(m, sizeof(m), (uint64_t)moved); ap(m, sizeof(m), " movidos");
    if (skipped) { ap(m, sizeof(m), ", "); ap_u(m, sizeof(m), (uint64_t)skipped); ap(m, sizeof(m), " protegidos"); }
    if (failed)  { ap(m, sizeof(m), ", "); ap_u(m, sizeof(m), (uint64_t)failed); ap(m, sizeof(m), " con error"); }
    if (limit)   ap(m, sizeof(m), " (pulsa Organizar otra vez para seguir)");
    fm_say(m);
}

static void fm_undo(void) {
    int ok = 0, bad = 0;
    for (int i = undo_n - 1; i >= 0; --i) {
        if (fm_move(undo_log[i].store, undo_log[i].to, undo_log[i].from) == 0) ok++; else bad++;
        if (undo_log[i].store == ST_BIG) {      /* quitar la carpeta de categoria si ha quedado vacia */
            char dir[FM_PATH]; fm_get_parent_folder(undo_log[i].to, dir, sizeof(dir));
            for (int c = 0; c < FM_NCAT; ++c)
                if (str_eq(fm_base(dir), cat_name[c])) { struct bigfs *fs = fm_big(); if (fs) bigfs_rmdir(fs, dir); break; }
        }
    }
    undo_n = 0;
    lib_dirty = 1;
    fm_reload();
    char m[96]; m[0] = '\0';
    ap(m, sizeof(m), "Deshecho: "); ap_u(m, sizeof(m), (uint64_t)ok); ap(m, sizeof(m), " archivos devueltos");
    if (bad) { ap(m, sizeof(m), ", "); ap_u(m, sizeof(m), (uint64_t)bad); ap(m, sizeof(m), " con error"); }
    fm_say(m);
}

static int fm_valid_name(const char *n) { return fm_ed_name_ok(n); }      /* SOMA_273_ARCHIVOS: tambien sin : * ? " < > | \\ ni punto o espacio al final */

static void fm_modal_confirm(void) {
    enum fm_modal_mode mode = modal_mode;
    modal_mode = FM_MODAL_NONE;
    if (mode == FM_MODAL_ORGANIZE) { fm_organize(); return; }

    if (mode == FM_MODAL_NEW_FILE || mode == FM_MODAL_NEW_FOLDER) {
        if (!fm_valid_name(modal_input)) { modal_mode = mode; str_copy(modal_err, "Nombre no v\xC3\xA1lido: no puede estar vac\xC3\xADo ni llevar / \\ : * ? \" < > |", sizeof(modal_err)); return; }
        if (fm_find_item_index(modal_input) >= 0) { modal_mode = mode; str_copy(modal_err, "Ya existe algo con ese nombre en esta carpeta", sizeof(modal_err)); return; }
        char full[FM_PATH];
        fm_build_path(current_folder, modal_input, full, sizeof(full));
        int ok = 0;
        if (fm_view == FV_BIG) {
            struct bigfs *fs = fm_big();
            if (fs) {
                if (mode == FM_MODAL_NEW_FILE) { uint32_t ino; ok = (bigfs_create(fs, full, &ino) == 0); }
                else ok = (bigfs_mkdir(fs, full) == 0);
            }
        } else if (!fm_puede(full)) {                                                     /* SOMA_268_ACCESO */
            return;
        } else if (fm_es_unid(full)) {
            ok = (mode == FM_MODAL_NEW_FILE) ? (vfs_write(full, "", 0) >= 0) : (vfs_unid_mkdir(full) == 0);
        } else {
            if (str_len(full) >= FS_NAME_MAX - 5) { fm_say("Ruta demasiado larga para RamFS"); return; }
            if (mode == FM_MODAL_NEW_FILE) ok = (vfs_write(full, "", 0) >= 0);
            else { char marker[FM_PATH]; fm_build_path(full, ".dir", marker, sizeof(marker)); ok = (vfs_write(marker, "", 0) >= 0); }
        }
        if (!ok) fm_say("No se pudo crear (sin espacio o sin permisos)");
        else fm_say(mode == FM_MODAL_NEW_FOLDER ? "Carpeta creada" : "Archivo creado");
        lib_dirty = 1; fm_reload();
        fm_select_name(modal_input);
        return;
    }

    if (mode == FM_MODAL_RENAME) {
        struct fm_item *it = fm_sel_item();
        if (!it) return;
        if (str_eq(modal_input, it->name)) return;                                         /* SOMA_273_ARCHIVOS: sin cambios */
        if (!fm_valid_name(modal_input)) { modal_mode = mode; str_copy(modal_err, "Nombre no v\xC3\xA1lido: no puede estar vac\xC3\xADo ni llevar / \\ : * ? \" < > |", sizeof(modal_err)); return; }
        if (fm_find_item_index(modal_input) >= 0) { modal_mode = mode; str_copy(modal_err, "Ya existe algo con ese nombre en esta carpeta", sizeof(modal_err)); return; }
        char parent[FM_PATH], nf[FM_PATH];
        fm_get_parent_folder(it->full_path, parent, sizeof(parent));
        fm_build_path(parent, modal_input, nf, sizeof(nf));
        int rc;
        if (it->store == ST_RAM && (!fm_puede(it->full_path) || !fm_puede(nf))) return;   /* SOMA_268_ACCESO */
        if (it->is_dir && it->store == ST_RAM && fm_es_unid(nf)) rc = vfs_unid_rename(it->full_path, nf);
        else if (it->is_dir && it->store == ST_RAM) rc = fm_rename_ram_dir(it->full_path, nf);
        else rc = fm_move(it->store, it->full_path, nf);
        if (rc != 0) fm_say("No se pudo renombrar (el nombre ya existe o es demasiado largo)");
        else fm_say("Renombrado");
        lib_dirty = 1; fm_reload();
        fm_select_name(modal_input);
        return;
    }

    if (mode == FM_MODAL_DELETE_CONFIRM) {
        struct fm_item *it = fm_sel_item();
        if (it) {
            fm_delete_item(it->store, it->full_path, it->is_dir);
            lib_dirty = 1; fm_reload();
            fm_say("Eliminado");
        }
    }
}

/* ====================================================================
 * RATON Y TECLADO
 * ==================================================================== */
static void fm_sort_click(int col) {
    if (col == 0) sort_mode = (sort_mode == FM_SORT_NAME_ASC) ? FM_SORT_NAME_DESC : FM_SORT_NAME_ASC;
    else if (col == 1) sort_mode = (sort_mode == FM_SORT_TYPE_ASC) ? FM_SORT_TYPE_DESC : FM_SORT_TYPE_ASC;
    else sort_mode = (sort_mode == FM_SORT_SIZE_ASC) ? FM_SORT_SIZE_DESC : FM_SORT_SIZE_ASC;
    fm_sort_list();
    selected_idx = -1; preview_text[0] = '\0'; preview_extra[0] = '\0';
}
static void fm_toggle_view(void) { fm_mode = !fm_mode; fm_reveal = 1; }

static int fm_menu_exec(int id, char *out_path, uint32_t max_out) {
    struct fm_item *it = fm_sel_item();
    switch (id) {
    case FMI_OPEN: if (it) return fm_open_item(it, out_path, max_out); break;
    case FMI_CUT: fm_clip_op(1); break;
    case FMI_COPY: fm_clip_op(0); break;
    case FMI_PASTE: fm_clip_op(2); break;
    case FMI_REN: fm_ask_rename(); break;
    case FMI_DEL: fm_ask_delete(); break;
    case FMI_NFOL: fm_new_item(1); break;
    case FMI_NFIL: fm_new_item(0); break;
    case FMI_ORG: fm_ask_organize(); break;
    case FMI_REF: fm_refresh(); break;
    case FMI_VIEW: fm_toggle_view(); break;
    case FMI_PATH:
        if (it && clip_set_text) {
            char p[FM_PATH + 8]; p[0] = '\0';
            if (it->store == ST_BIG) ap(p, sizeof(p), "/big");
            ap(p, sizeof(p), it->full_path);
            clip_set_text(p, str_len(p)); fm_say("Ruta copiada al portapapeles");
        }
        break;
    default: break;
    }
    return 0;
}

int fm_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, char *out_path, uint32_t max_out) {
    struct fl L; fl_make(&L, win_x, win_y, win_w, win_h);
    fm_hist_sync();
    fm_clamp_scroll(&L);
    fm_mx = mx; fm_my = my;
    if (fm_swallow) { fm_swallow = 0; return 0; }

    if (modal_mode != FM_MODAL_NONE) {
        uint32_t bx, by, mw, mh; fm_modal_rect(win_x, win_y, win_w, win_h, &bx, &by, &mw, &mh);
        if (point_in_rect(mx, my, bx + mw - 210, by + mh - 50, 96, 32)) { fm_modal_confirm(); return 0; }
        if (point_in_rect(mx, my, bx + mw - 106, by + mh - 50, 86, 32)) { modal_mode = FM_MODAL_NONE; return 0; }
        return 0;
    }
    if (fm_ctx_open) {
        int i = fm_menu_hit(mx, my), inside = fm_menu_inside(mx, my);
        int n; const struct fm_mi *m = fm_menu(&n);
        if (inside && (i < 0 || !fm_menu_enabled(m[i].id))) return 0;
        fm_ctx_open = 0; fm_ctx_hover = -1;
        if (i >= 0) return fm_menu_exec(m[i].id, out_path, max_out);
        return 0;
    }

    int tb = fm_tb_hit(&L, mx, my);
    if (tb != FB_SRCH && my >= L.y && my < L.y + FM_TB_H) fm_search_active = 0;
    if (tb >= 0 && tb < FB_N) {
        if (!fm_btn_enabled(tb)) return 0;
        switch (tb) {
        case FB_BACK: fm_hist_go(-1); break;
        case FB_FWD: fm_hist_go(1); break;
        case FB_UP: fm_go_up(); break;
        case FB_LIST: fm_mode = 0; fm_reveal = 1; break;
        case FB_GRID: fm_mode = 1; fm_reveal = 1; break;
        case FB_NFOL: fm_new_item(1); break;
        case FB_NFIL: fm_new_item(0); break;
        case FB_ORG: fm_ask_organize(); break;
        case FB_REF: fm_refresh(); break;
        default: break;
        }
        return 0;
    }
    if (tb == FB_SRCH) {
        if (fm_filter_len && mx >= L.srch_x + L.srch_w - 26) { fm_filter[0] = '\0'; fm_filter_len = 0; fm_search_active = 0; fm_reload(); }
        else fm_search_active = 1;
        return 0;
    }
    if (tb == FB_CRUMB) {
        fm_build_crumbs(&L);
        for (int i = cr_first; i < ncr; ++i) if (mx >= crumbs[i].x && mx < crumbs[i].x + crumbs[i].w) { fm_crumb_go(i); return 0; }
        return 0;
    }
    if (my < L.body_y) return 0;
    fm_search_active = 0;

    int sh = side_hit(&L, mx, my);
    if (sh >= 0) {
        const struct side_row *s = &side_rows[sh];
        if (s->kind == 1) fm_goto(FV_RAM, s->path, 0);
        return 0;
    }
    if (mx < L.x + L.side_w) return 0;

    int dh = fm_det_hit(&L, mx, my);
    if (dh >= 0) {
        struct fm_item *sel = fm_sel_item();
        if (dh == FB_OPEN && sel) return fm_open_item(sel, out_path, max_out);
        if (dh == FB_REN) fm_ask_rename();
        if (dh == FB_DEL) fm_ask_delete();
        return 0;
    }
    if (undo_n > 0 && my >= L.st_y && mx >= fm_undo_x(&L) && mx < fm_undo_x(&L) + 92) { fm_undo(); return 0; }
    if (L.det_w && mx >= L.det_x) return 0;

    /* cabecera de columnas */
    if (fm_mode == 0 && my >= L.hdr_y && my < L.hdr_y + FM_HDR && mx >= L.main_x && mx < L.sb_x) {
        if (mx >= L.col_size) fm_sort_click(2);
        else if (L.has_type && mx >= L.col_type) fm_sort_click(1);
        else fm_sort_click(0);
        return 0;
    }
    /* barra de desplazamiento */
    if (mx >= L.sb_x - 2 && mx < L.main_x + L.main_w && my >= L.items_y && my < L.items_y + L.items_h) {
        int32_t ty, th;
        if (fm_sb_thumb(&L, &ty, &th)) {
            if (my >= ty && my < ty + th) { fm_sb_drag = 1; fm_sb_grab = my - ty; }
            else { int r = fm_first_row(&L) + (my < ty ? -L.rows_vis : L.rows_vis); scroll_offset = (r < 0 ? 0 : r) * L.cols; }
        }
        return 0;
    }
    /* elementos */
    if (my >= L.items_y && my < L.items_y + L.items_h) {
        int idx = fm_item_at(&L, mx, my);
        if (idx < 0) { selected_idx = -1; fm_update_preview(-1); return 0; }
        uint64_t now = timer_get_uptime_ms();
        if (selected_idx == idx && (now - last_click_time) < 450) { last_click_time = 0; return fm_open_item(&file_list[idx], out_path, max_out); }
        last_click_time = now;
        fm_set_sel(idx);
    }
    return 0;
}

/* SOMA_PARCHE_028_UI: movimiento, rueda, arrastre de la barra y boton derecho con la posicion real del puntero.
 * Devuelve 1 si hay que repintar. */
int fm_mouse(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int wheel, int left_held, int right_click) {
    struct fl L; fl_make(&L, win_x, win_y, win_w, win_h);
    fm_clamp_scroll(&L);
    int redraw = 0, press = left_held && !fm_prev_left;
    int inside = mx >= L.x && mx < L.x + L.w && my >= L.y && my < L.y + L.h;
    fm_mx = mx; fm_my = my; fm_prev_left = left_held;
    if (!left_held) { if (fm_sb_drag) redraw = 1; fm_sb_drag = 0; fm_swallow = 0; }
    if (modal_mode != FM_MODAL_NONE) return 0;
    if (fm_ctx_open) {
        int h = fm_menu_hit(mx, my);
        if (h != fm_ctx_hover) { fm_ctx_hover = h; redraw = 1; }
        if (press && !fm_menu_inside(mx, my)) { fm_ctx_open = 0; fm_ctx_hover = -1; if (inside) fm_swallow = 1; return 1; }
        if (!right_click) return redraw;
    }
    if (right_click && inside) {
        int idx = fm_item_at(&L, mx, my);
        if (idx >= 0) { fm_set_sel(idx); fm_ctx_kind = 0; }
        else if (mx >= L.main_x && mx < L.main_x + L.main_w && my >= L.body_y && my < L.st_y) { fm_ctx_kind = 1; selected_idx = -1; fm_update_preview(-1); }
        else return redraw;
        fm_reveal = 0;
        int32_t h = fm_ctx_h(), x = mx + 1, y = my + 1;
        if (x + FM_CTX_W > L.x + L.w - 4) x = mx - FM_CTX_W;
        if (y + h > L.y + L.h - 4) y = my - h;
        if (x < L.x + 4) x = L.x + 4;
        if (y < L.y + 4) y = L.y + 4;
        fm_ctx_x = x; fm_ctx_y = y; fm_ctx_open = 1; fm_ctx_hover = fm_menu_hit(mx, my);
        return 1;
    }
    if (left_held && fm_sb_drag) { fm_sb_to(&L, my); return 1; }
    if (wheel && inside && mx >= L.main_x && mx < L.main_x + L.main_w) {
        int r = fm_first_row(&L) - wheel * (fm_mode == 1 ? 1 : 3);
        scroll_offset = (r < 0 ? 0 : r) * L.cols;
        fm_clamp_scroll(&L);
        redraw = 1;
    }
    int hi = inside && !left_held ? fm_item_at(&L, mx, my) : -1;
    if (hi != fm_hover) { fm_hover = hi; redraw = 1; }
    int hu = -1;
    if (inside && !left_held) {
        int t = fm_tb_hit(&L, mx, my);
        if (t >= 0 && (t >= FB_N || fm_btn_enabled(t))) hu = t;
        else if (fm_det_hit(&L, mx, my) >= 0) hu = fm_det_hit(&L, mx, my);
        else if (undo_n > 0 && my >= L.st_y && mx >= fm_undo_x(&L) && mx < fm_undo_x(&L) + 92) hu = FB_UNDO;
    }
    if (hu != fm_hover_ui) { fm_hover_ui = hu; redraw = 1; }
    int hs = inside && !left_held ? side_hit(&L, mx, my) : -1;
    if (hs != fm_hover_side) { fm_hover_side = hs; redraw = 1; }
    return redraw;
}

/* Esc: cierra el menu, el dialogo o la busqueda. 1 = tecla usada (la GUI no debe salir). */
int fm_escape(void) {
    if (fm_ctx_open) { fm_ctx_open = 0; fm_ctx_hover = -1; return 1; }
    if (modal_mode != FM_MODAL_NONE) { modal_mode = FM_MODAL_NONE; return 1; }
    if (fm_search_active || fm_filter_len) { fm_filter[0] = '\0'; fm_filter_len = 0; fm_search_active = 0; scroll_offset = 0; fm_reload(); return 1; }
    return 0;
}

static void fm_filter_changed(void) { scroll_offset = 0; fm_reload(); }

void fm_handle_key(char c) {
    if (fm_ctx_open) { fm_ctx_open = 0; fm_ctx_hover = -1; if (c == 27) return; }
    if (modal_mode != FM_MODAL_NONE) {
        fm_dg_n++; fm_dg_c = (uint8_t)c;
        if (c == 27) { modal_mode = FM_MODAL_NONE; return; }
        if (c == '\r' || c == '\n') { fm_modal_confirm(); return; }
        if (modal_mode == FM_MODAL_ORGANIZE || modal_mode == FM_MODAL_DELETE_CONFIRM) return;       /* solo Intro/Esc */
        struct fm_ed e; fm_ed_load(&e);
        modal_err[0] = '\0';
        if (c == '\b' || (uint8_t)c == 127) fm_ed_back(&e);
        else if (c == 4) fm_ed_supr(&e);                      /* Ctrl+D / Supr */
        else if (c == 2) fm_ed_left(&e);                      /* flecha izquierda */
        else if (c == 6) fm_ed_right(&e);                     /* flecha derecha */
        else if (c == 16) fm_ed_home(&e);                     /* flecha arriba = inicio */
        else if (c == 14 || c == 5) fm_ed_end(&e);            /* flecha abajo / Ctrl+E = fin */
        else if (c == 1) fm_ed_select_all(&e);                /* Ctrl+A */
        else if (fm_ed_char_ok(c) || (uint8_t)c >= 128) fm_ed_insert(&e, c);       /* los bytes UTF-8 (tildes, enes) pasan tal cual */
        fm_ed_store(&e);
        return;
    }
    int cols = g_vis_cols > 0 ? g_vis_cols : 1;
    int vr = g_vis_rows > 0 ? g_vis_rows : 10;
    if (c == 16) { if (file_count > 0) fm_set_sel(selected_idx < 0 ? 0 : (selected_idx - cols >= 0 ? selected_idx - cols : selected_idx)); return; }
    if (c == 14) { if (file_count > 0) fm_set_sel(selected_idx < 0 ? 0 : (selected_idx + cols < file_count ? selected_idx + cols : file_count - 1)); return; }
    if (c == 28) { if (file_count > 0) fm_set_sel(selected_idx - vr > 0 ? selected_idx - vr : 0); return; }
    if (c == 29) { if (file_count > 0) fm_set_sel(selected_idx + vr < file_count ? selected_idx + vr : file_count - 1); return; }
    if (fm_mode == 1 && !fm_search_active && (c == 2 || c == 6)) {
        if (file_count > 0) { int n = selected_idx < 0 ? 0 : selected_idx + (c == 2 ? -1 : 1); if (n < 0) n = 0; if (n >= file_count) n = file_count - 1; fm_set_sel(n); }
        return;
    }
    if (fm_search_active) {
        if (c == 27) { fm_filter[0] = '\0'; fm_filter_len = 0; fm_search_active = 0; fm_filter_changed(); }
        else if (c == '\r' || c == '\n') fm_search_active = 0;
        else if (c == '\b' || (uint8_t)c == 127) {
            if (fm_filter_len > 0) { fm_filter[--fm_filter_len] = '\0'; fm_filter_changed(); }
            else fm_search_active = 0;
        } else if ((uint8_t)c >= 32 && (uint8_t)c < 127 && fm_filter_len < sizeof(fm_filter) - 1) {
            fm_filter[fm_filter_len++] = c; fm_filter[fm_filter_len] = '\0'; fm_filter_changed();
        }
        return;
    }
    struct fm_item *it = fm_sel_item();
    if (c == 1) { if (file_count > 0) fm_set_sel(0); }
    else if (c == 5) { if (file_count > 0) fm_set_sel(file_count - 1); }
    else if (c == '/') { fm_search_active = 1; }
    else if (c == '\r' || c == '\n' || c == 6) {
        if (it) { char path[FM_PATH]; path[0] = '\0'; fm_open_item(it, path, sizeof(path)); }
    } else if (c == '\b' || c == 27 || c == 2 || c == 'u' || c == 'U') {
        if (fm_filter_len && c == 27) { fm_filter[0] = '\0'; fm_filter_len = 0; fm_filter_changed(); }
        else fm_go_up();
    } else if (c == 'n' || c == 'N') fm_new_item(0);
    else if (c == 'f' || c == 'F') fm_new_item(1);
    else if (c == 'r' || c == 'R') fm_refresh();
    else if (c == 'o' || c == 'O') fm_ask_organize();
    else if (c == 'e' || c == 'E') fm_ask_rename();
    else if (c == 'v' || c == 'V') fm_toggle_view();
    else if (c == 26 || c == 'z' || c == 'Z') { if (undo_n > 0) fm_undo(); }
    else if ((c == 'd' || c == 'D' || c == 4 || (uint8_t)c == 127) && it) fm_ask_delete();
}

/* ====================================================================
 * SOMA_PARCHE_PORTAPAPELES: adaptador del portapapeles del gestor de archivos
 * (funciona con o sin el parche del portapapeles aplicado: las funciones clip_* son opcionales)
 * ==================================================================== */
extern int clip_kind(void) __attribute__((weak));
extern const char *clip_text(uint32_t *n) __attribute__((weak));
extern int clip_set_file(const char *path, int is_cut) __attribute__((weak));
extern int clip_file_is_cut(void) __attribute__((weak));
extern void clip_file_done(void) __attribute__((weak));
extern int clip_image_to_bmp(uint8_t *out, uint32_t max) __attribute__((weak));
extern void clip_toast(const char *msg) __attribute__((weak));

#define FMC_OP_COPY  0
#define FMC_OP_CUT   1
#define FMC_KIND_TEXT  1
#define FMC_KIND_IMAGE 2
#define FMC_KIND_FILE  3
#define FMC_MAX (160u * 1024u)
#define FMC_CHUNK (64u * 1024u)

static void fmc_toast(const char *m) { if (clip_toast) clip_toast(m); fm_say(m); }

/* el portapapeles guarda rutas; las de BIGFS llevan el prefijo /big como en las ordenes del shell */
static int fmc_split(const char *clip, const char **path) {
    if (str_starts_with(clip, "/big/")) { *path = clip + 4; return ST_BIG; }
    *path = clip; return ST_RAM;
}

static void fmc_dest(char *out, uint32_t max, int store, const char *name) {
    char stem[48], ext[16]; uint32_t sn = 0, en = 0, nl = str_len(name);
    int dot = -1; for (uint32_t i = 0; i < nl; i++) if (name[i] == '.') dot = (int)i;
    uint32_t stem_end = dot > 0 ? (uint32_t)dot : nl;
    for (uint32_t i = 0; i < stem_end && sn < sizeof(stem) - 1; i++) stem[sn++] = name[i];
    stem[sn] = 0;
    if (dot > 0) for (uint32_t i = (uint32_t)dot; i < nl && en < sizeof(ext) - 1; i++) ext[en++] = name[i];
    ext[en] = 0;
    for (int tries = 0; tries < 50; tries++) {
        char nm[64]; nm[0] = '\0';
        ap(nm, sizeof(nm), stem);
        if (tries > 0) { ap(nm, sizeof(nm), "_copia"); if (tries > 1) ap_u(nm, sizeof(nm), (uint64_t)tries); }
        ap(nm, sizeof(nm), ext);
        fm_build_path(current_folder, nm, out, max);
        if (!fm_exists(store, out)) return;
    }
}

/* copia un archivo entre almacenes; devuelve 0 si fue bien */
static int fmc_copy(int s_store, const char *src, int d_store, const char *dst, const char **why) {
    *why = "No se pudo copiar";
    if (s_store == ST_RAM) {
        if (vfs_size(src) < 0) { *why = "El archivo original ya no existe"; return -1; }
        uint32_t rl = 0; char *buf = vfs_read_alloc(src, &rl);   /* PARCHE 087 */
        if (!buf) { *why = "Memoria insuficiente"; return -1; }
        int r = (int)rl;
        int rc = -1;
        if (d_store == ST_RAM) {
            if (!usuario_ruta_escribible(dst)) *why = "Solo puedes guardar en Mi unidad (/usuario)";   /* SOMA_268_ACCESO */
            else if (str_len(dst) >= (fm_es_unid(dst) ? FM_PATH : FS_NAME_MAX)) *why = "Ruta demasiado larga";
            else if (vfs_write(dst, buf, (uint32_t)r) >= 0) rc = 0;
            else *why = "RamFS lleno o sin permisos";
        } else {
            struct bigfs *fs = fm_big(); uint32_t ino;
            if (fs && bigfs_create(fs, dst, &ino) == 0) {
                if (r == 0 || bigfs_write(fs, ino, 0, buf, (uint64_t)r) == (int64_t)r) rc = 0;
                else { bigfs_unlink(fs, dst); *why = "BIGFS sin espacio"; }
            }
        }
        kfree(buf);
        return rc;
    }
    struct bigfs *fs = fm_big(); uint32_t sino;
    if (!fs || bigfs_lookup(fs, src, &sino) != (int)BIGFS_T_FILE) { *why = "El archivo original ya no existe"; return -1; }
    struct bigfs_stat st;
    if (bigfs_stat(fs, sino, &st) < 0) return -1;
    if (d_store == ST_RAM && st.size > FS_FILE_MAX) { *why = "Demasiado grande para RamFS (max 32 MiB)"; return -1; }
    char *buf = (char *)kmalloc(FMC_CHUNK);
    if (!buf) { *why = "Memoria insuficiente"; return -1; }
    int rc = -1;
    if (d_store == ST_RAM) {
        if (!usuario_ruta_escribible(dst)) *why = "Solo puedes guardar en Mi unidad (/usuario)";   /* SOMA_268_ACCESO */
        else if (str_len(dst) >= (fm_es_unid(dst) ? FM_PATH : FS_NAME_MAX)) *why = "Ruta demasiado larga";
        else {
            char *all = (char *)kmalloc((size_t)st.size + 1);  /* PARCHE 087: el archivo entero, no solo el primer trozo */
            if (!all) *why = "Memoria insuficiente";
            else {
                int64_t r = bigfs_read(fs, sino, 0, all, st.size);
                if (r == (int64_t)st.size && vfs_write(dst, all, (uint32_t)r) >= 0) rc = 0;
                else *why = "No se pudo escribir en RamFS (sin memoria?)";
                kfree(all);
            }
        }
    } else {
        uint32_t dino;
        if (bigfs_create(fs, dst, &dino) == 0) {
            uint64_t off = 0; rc = 0;
            while (off < st.size) {
                uint64_t n = st.size - off; if (n > FMC_CHUNK) n = FMC_CHUNK;
                int64_t r = bigfs_read(fs, sino, off, buf, n);
                if (r <= 0 || bigfs_write(fs, dino, off, buf, (uint64_t)r) != r) { rc = -1; break; }
                off += (uint64_t)r;
            }
            if (rc) { bigfs_unlink(fs, dst); *why = "BIGFS sin espacio o error de escritura"; }
        }
    }
    kfree(buf);
    return rc;
}

int fm_clip_op(int op) {
    if (op == FMC_OP_COPY || op == FMC_OP_CUT) {
        struct fm_item *it = fm_sel_item();
        if (!it) { fmc_toast("Selecciona un archivo primero"); return 1; }
        if (it->is_dir) { fmc_toast("Las carpetas no se pueden copiar aun"); return 1; }
        if (!clip_set_file) return 1;
        char cp[FM_PATH + 8]; cp[0] = '\0';
        if (it->store == ST_BIG) ap(cp, sizeof(cp), "/big");
        ap(cp, sizeof(cp), it->full_path);
        clip_set_file(cp, op == FMC_OP_CUT);
        char m[96]; m[0] = '\0';
        ap(m, sizeof(m), op == FMC_OP_CUT ? "Cortado: " : "Copiado: "); ap(m, sizeof(m), it->name);
        fmc_toast(m);
        return 1;
    }
    if (!clip_kind) return 1;
    if (!(fm_view == FV_RAM || fm_view == FV_BIG)) { fmc_toast("Abre una carpeta real para pegar"); return 1; }
    int dstore = (fm_view == FV_BIG) ? ST_BIG : ST_RAM;
    int kind = clip_kind();
    char dest[FM_PATH]; dest[0] = 0;
    if (kind == FMC_KIND_FILE) {
        uint32_t n = 0; const char *clip = clip_text ? clip_text(&n) : 0;
        if (!clip) { fmc_toast("El portapapeles esta vacio"); return 1; }
        const char *src; int sstore = fmc_split(clip, &src);
        fmc_dest(dest, sizeof(dest), dstore, fm_base(src));
        int cut = clip_file_is_cut ? clip_file_is_cut() : 0;
        const char *why = "";
        if (cut && sstore == dstore) {
            char same[FM_PATH]; fm_build_path(current_folder, fm_base(src), same, sizeof(same));
            if (str_eq(same, src)) { fmc_toast("El archivo ya esta en esta carpeta"); return 1; }
            if (fm_move(dstore, src, dest) != 0) { fmc_toast("No se pudo mover el archivo"); return 1; }
            if (clip_file_done) clip_file_done();
            fmc_toast("Archivo movido aqui");
        } else {
            if (fmc_copy(sstore, src, dstore, dest, &why) != 0) { fmc_toast(why); return 1; }
            if (cut) { fm_delete_item(sstore, src, 0); if (clip_file_done) clip_file_done(); }
            fmc_toast(cut ? "Archivo movido aqui" : "Archivo pegado");
        }
    } else if (kind == FMC_KIND_IMAGE) {
        uint8_t *bmp = (uint8_t *)kmalloc(FMC_MAX);
        if (!bmp) { fmc_toast("Memoria insuficiente"); return 1; }
        int bn = clip_image_to_bmp ? clip_image_to_bmp(bmp, FMC_MAX) : -1;
        if (bn < 0) { kfree(bmp); fmc_toast("Imagen demasiado grande para guardarla aqui"); return 1; }
        fmc_dest(dest, sizeof(dest), dstore, "portapapeles.bmp");
        int ok = 0;
        if (dstore == ST_RAM) ok = (usuario_ruta_escribible(dest) && str_len(dest) < (fm_es_unid(dest) ? FM_PATH : FS_NAME_MAX) && vfs_write(dest, (const char *)bmp, (uint32_t)bn) >= 0);   /* SOMA_268_ACCESO */
        else { struct bigfs *fs = fm_big(); uint32_t ino; ok = (fs && bigfs_create(fs, dest, &ino) == 0 && bigfs_write(fs, ino, 0, bmp, (uint64_t)bn) == (int64_t)bn); }
        kfree(bmp);
        if (!ok) { fmc_toast("No se pudo guardar la imagen"); return 1; }
        fmc_toast("Imagen guardada como BMP");
    } else if (kind == FMC_KIND_TEXT) {
        uint32_t n = 0; const char *t = clip_text ? clip_text(&n) : 0;
        fmc_dest(dest, sizeof(dest), dstore, "portapapeles.txt");
        int ok = 0;
        if (t) {
            if (dstore == ST_RAM) ok = (usuario_ruta_escribible(dest) && str_len(dest) < (fm_es_unid(dest) ? FM_PATH : FS_NAME_MAX) && vfs_write(dest, t, n) >= 0);   /* SOMA_268_ACCESO */
            else { struct bigfs *fs = fm_big(); uint32_t ino; ok = (fs && bigfs_create(fs, dest, &ino) == 0 && (n == 0 || bigfs_write(fs, ino, 0, t, n) == (int64_t)n)); }
        }
        if (!ok) { fmc_toast("No se pudo guardar el texto"); return 1; }
        fmc_toast("Texto guardado en un archivo");
    } else { fmc_toast("El portapapeles esta vacio"); return 1; }
    lib_dirty = 1;
    fm_reload();
    return 1;
}


#undef file_list
#undef file_count
#undef selected_idx
#undef scroll_offset
#undef sort_mode
#undef current_folder
#undef fm_view
#undef fm_cat
#undef preview_text
#undef preview_extra
#undef last_click_time
#undef modal_mode
#undef modal_input
#undef modal_input_len
#undef modal_target
#undef modal_caret
#undef modal_s0
#undef modal_s1
#undef modal_err
#undef fm_filter
#undef fm_filter_len
#undef fm_search_active
#undef fm_msg
#undef fm_msg_until
#undef st_info
#undef g_vis_rows
#undef fm_mode
#undef fm_reveal
#undef fm_hover
#undef fm_hover_ui
#undef fm_hover_side
#undef fm_sb_drag
#undef fm_sb_grab
#undef fm_ctx_open
#undef fm_ctx_kind
#undef fm_ctx_hover
#undef fm_ctx_x
#undef fm_ctx_y
#undef g_vis_cols
#undef fm_histv
#undef fm_hn
#undef fm_hpos
#undef crumbs
#undef ncr
#undef cr_first

/* SOMA_214_ARCHIVOS: instancias */
int fm_inst_new(void)
{
    fm_iboot();
    for (int k = 1; k <= FM_INST_MAX; ++k) {
        if (fm_iused[k]) continue;
        if (!fm_insts[k]) { fm_insts[k] = (struct fm_inst *)kmalloc(sizeof(struct fm_inst)); if (!fm_insts[k]) return 0; }
        fm_inst_defaults(fm_insts[k]);
        fm_iused[k] = 1;
        FMI = fm_insts[k];
        fm_goto(FV_RAM, "/usuario", 0);                  /* SOMA_273_ARCHIVOS: la ventana nueva empieza en Mi unidad, ya con su lista */
        return k;
    }
    return 0;
}
void fm_select(int slot) { fm_iboot(); if (slot >= 1 && slot <= FM_INST_MAX && fm_insts[slot]) FMI = fm_insts[slot]; }
void fm_inst_drop(int slot)
{
    if (slot < 1 || slot > FM_INST_MAX || !fm_iused[slot]) return;
    fm_iused[slot] = 0;
    if (FMI == fm_insts[slot]) FMI = &fm_inst1;
    if (slot != 1 && fm_insts[slot]) { kfree(fm_insts[slot]); fm_insts[slot] = 0; }
}
int fm_inst_find(const char *path)
{
    for (int k = 1; k <= FM_INST_MAX; ++k) {
        if (!fm_iused[k] || !fm_insts[k] || !path) continue;
        const char *a = fm_insts[k]->current_folder, *b = path;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b && fm_insts[k]->fm_view == FV_RAM) return k;
    }
    return 0;
}
const char *fm_inst_name(int slot)
{
    if (slot < 1 || slot > FM_INST_MAX || !fm_insts[slot]) return "";
    struct fm_inst *p = fm_insts[slot];
    if (p->fm_view == FV_RAM && p->current_folder[0] == '/' && p->current_folder[1] == 'u' && p->current_folder[8] == '\0') return "Mi unidad";   /* SOMA_273_ARCHIVOS */
    if (p->fm_view == FV_LIB || p->fm_view == FV_CAT) return "Biblioteca";
    const char *s = p->current_folder, *n = s;
    for (; *s; ++s) if (*s == '/' && s[1]) n = s + 1;
    if (p->fm_view == FV_BIG && (!n[0] || (n[0] == '/' && !n[1]))) return "Almacen";
    return n[0] == '/' && !n[1] ? "/" : n;
}
