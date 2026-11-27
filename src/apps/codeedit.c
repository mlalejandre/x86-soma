/* PARCHE 093: editor de codigo profesional para SOMA Studio (Coder). Ver codeedit.h. */
#include "acceso.h"      /* SOMA_363_ACCESO */
#include "codeedit.h"
#include "fb.h"
#include "gui_font.h"
#include "web_font.h"
#include "fs.h"
#include "mem.h"
#include "clipboard.h"
#include "textfield.h"

extern uint64_t timer_get_uptime_ms(void);

#define CE_MAX   (256 * 1024)
#define CE_MAXL  65536
#define CE_UN    400
#define CE_PX    14
#define CE_FL    (WF_MONO | WF_RAW)

/* colores (0x00RRGGBB) */
#define K_BG     0x0B1120u
#define K_GUT    0x0D1424u
#define K_GUTTX  0x4B5A72u
#define K_GUTCUR 0xCBD5E1u
#define K_CURL   0x121C30u
#define K_SEL    0x1E4A80u
#define K_FIND   0x5B4A10u
#define K_FINDC  0xB7791Fu
#define K_CARET  0x7DD3FCu
#define K_GUIDE  0x18233Au
#define K_TXT    0xE2E8F0u
#define K_PUNC   0xA5B4CBu
#define K_COM    0x64748Bu
#define K_STR    0xF2C46Bu
#define K_NUM    0xC4A7FFu
#define K_KW     0xF472B6u
#define K_DECL   0x38BDF8u
#define K_NS     0x34D399u
#define K_BUILT  0x60A5FAu
#define K_CONST  0xFB923Cu
#define K_CALL   0x93C5FDu
#define K_ERR    0xF43F5Eu
#define K_ERRBG  0x3A1220u
#define K_BAR    0x0F172Au
#define K_BORD   0x1E293Bu
#define K_FIELD  0x060A14u
#define K_ACC    0x38BDF8u
#define K_DIM    0x94A3B8u


struct ur { uint32_t pos; char *d; uint32_t dn; char *i; uint32_t in, icap; uint32_t cb, ab; uint64_t t; uint8_t kind; };
/* SOMA_216_STUDIO: un editor por ventana de SOMA Studio. Texto, lineas, cursor, seleccion, desplazamiento, archivo, deshacer,
 * error marcado y barra de busqueda viven en una instancia; ce_select() elige la de cada ventana (cambio de puntero). */
struct ce_inst {
    char B[CE_MAX]; uint32_t N; uint32_t LS[CE_MAXL]; int NL;
    uint32_t cur, anc; int want_col, top, left, dirty, ro;
    char path_[128], banner[96]; int err_line; char err_msg[128];
    struct ur U[CE_UN]; int un, ut, saved_ut;
    int pr_kind, pr_focus; char pr_a[160], pr_b[160]; struct tfield tfa, tfb; int tf_ok;
    char find_s[160];
};
#define CE_INST_MAX 6
static struct ce_inst ce_inst1 = { .NL = 1, .want_col = -1, .pr_focus = 1, .path_ = "/workspace/nuevo.soma" };
static struct ce_inst *ce_insts[CE_INST_MAX + 1] = { 0, &ce_inst1 };
static int ce_iused[CE_INST_MAX + 1];
static struct ce_inst *CE = &ce_inst1;
static void ce_idefaults(struct ce_inst *q)
{
    char *b = (char *)q; for (size_t i = 0; i < sizeof *q; ++i) b[i] = 0;
    q->NL = 1; q->want_col = -1; q->pr_focus = 1;
    const char *t = "/workspace/nuevo.soma"; int k = 0; while (t[k]) { q->path_[k] = t[k]; k++; } q->path_[k] = 0;
}
#define B (CE->B)
#define N (CE->N)
#define LS (CE->LS)
#define NL (CE->NL)
#define ce_cur (CE->cur)
#define ce_anc (CE->anc)
#define want_col (CE->want_col)
#define top (CE->top)
#define left (CE->left)
#define dirty (CE->dirty)
#define ro (CE->ro)
#define path_ (CE->path_)
#define banner (CE->banner)
#define err_line (CE->err_line)
#define err_msg (CE->err_msg)
#define U (CE->U)
#define un (CE->un)
#define ut (CE->ut)
#define saved_ut (CE->saved_ut)
#define pr_kind (CE->pr_kind)
#define pr_focus (CE->pr_focus)
#define pr_a (CE->pr_a)
#define pr_b (CE->pr_b)
#define tfa (CE->tfa)
#define tfb (CE->tfb)
#define tf_ok (CE->tf_ok)
#define find_s (CE->find_s)

/* geometria de la ultima pintura */
static int g_x, g_y, g_w, g_h, g_tx, g_ty, g_tw, g_th, g_rows = 30, g_cols = 80, g_gw, g_sbx;
static int g_wf = -1, g_cw64 = 8 * 64, g_lh = 18, g_base = 13;
static int g_drag, g_sbdrag;
static uint64_t g_lclick; static uint32_t g_lpos; static int g_clicks;

/* barra de busqueda / prompts */
static void tf_ready(void) { if (!tf_ok) { tf_ok = 1; tf_init(&tfa, pr_a, sizeof pr_a, 0); tf_init(&tfb, pr_b, sizeof pr_b, 0); } }
struct rr { int x, y, w, h; };
static struct rr r_prev, r_next, r_rep, r_all, r_close, r_fa, r_fb, r_ok;

/* ---------------------------------------------------------------- utilidades */
static int slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void scpy(char *d, const char *s, int cap) { int i = 0; if (!s) s = ""; while (s[i] && i < cap - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int is_word(uint8_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80; }
static int in_r(struct rr *r, int x, int y) { return r->w > 0 && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h; }
static uint8_t lc(uint8_t c) { return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32) : c; }

static void font_setup(void)
{
    if (g_wf >= 0) return;
    g_wf = wf_init() ? 1 : 0;
    if (g_wf) {
        g_cw64 = wf_width64("M", 1, CE_PX, CE_FL); if (g_cw64 < 4 * 64) g_cw64 = 8 * 64;
        g_lh = CE_PX + 6; g_base = (g_lh + wf_ascent(CE_PX) - wf_descent(CE_PX)) / 2;
    } else { g_cw64 = 8 * 64; g_lh = 18; g_base = 13; }
}
static int colx(int col) { return (int)(((int64_t)col * g_cw64) >> 6); }
static void run(int x, int y, const char *s, int n, uint32_t col)
{
    if (n <= 0) return;
    if (g_wf) { wf_draw(x, y + g_base, s, (uint32_t)n, CE_PX, col, CE_FL, 255); return; }
    char t[256]; if (n > 255) n = 255; for (int i = 0; i < n; i++) t[i] = s[i]; t[n] = 0;
    font_draw_string((uint32_t)x, (uint32_t)(y + 1), t, col, 1);
}
static void ui_text(int x, int y, const char *s, uint32_t col)   /* texto de interfaz (barras) */
{
    if (g_wf) wf_draw(x, y + 13, s, (uint32_t)slen(s), 13, col, 0, 255);
    else font_draw_string((uint32_t)x, (uint32_t)(y + 2), s, col, 1);
}
static int ui_w(const char *s) { return g_wf ? wf_width(s, (uint32_t)slen(s), 13, 0) : slen(s) * 8; }

/* ---------------------------------------------------------------- lineas */
static void rebuild(void)
{
    NL = 0; LS[NL++] = 0;
    for (uint32_t i = 0; i < N && NL < CE_MAXL; i++) if (B[i] == '\n') LS[NL++] = i + 1;
}
static int line_of(uint32_t p)
{
    int lo = 0, hi = NL - 1;
    while (lo < hi) { int m = (lo + hi + 1) / 2; if (LS[m] <= p) lo = m; else hi = m - 1; }
    return lo;
}
static uint32_t lstart(int l) { if (l < 0) l = 0; if (l >= NL) l = NL - 1; return LS[l]; }
static uint32_t lend(int l) { if (l < 0) l = 0; if (l >= NL) l = NL - 1; return (l + 1 < NL) ? LS[l + 1] - 1 : N; }
static uint32_t prev_cp(uint32_t p) { if (p > 0) { p--; while (p > 0 && ((uint8_t)B[p] & 0xC0) == 0x80) p--; } return p; }
static uint32_t next_cp(uint32_t p) { if (p < N) { p++; while (p < N && ((uint8_t)B[p] & 0xC0) == 0x80) p++; } return p; }
static int vcol(uint32_t p)
{
    uint32_t s = lstart(line_of(p)); int c = 0;
    for (uint32_t i = s; i < p; i++) if (((uint8_t)B[i] & 0xC0) != 0x80) c++;
    return c;
}
static uint32_t pos_at(int l, int col)
{
    uint32_t p = lstart(l), e = lend(l); int c = 0;
    while (p < e && c < col) { p = next_cp(p); c++; }
    return p;
}
static int has_sel(void) { return ce_cur != ce_anc; }
static void sel_range(uint32_t *a, uint32_t *b) { if (ce_cur < ce_anc) { *a = ce_cur; *b = ce_anc; } else { *a = ce_anc; *b = ce_cur; } }
static uint32_t indent_of(int l) { uint32_t s = lstart(l), e = lend(l), p = s; while (p < e && B[p] == ' ') p++; return p - s; }

static void ensure_visible(void)
{
    int l = line_of(ce_cur), c = vcol(ce_cur);
    if (l < top) top = l;
    if (l >= top + g_rows) top = l - g_rows + 1;
    if (top < 0) top = 0;
    if (c < left) left = c > 4 ? c - 4 : 0;
    if (c >= left + g_cols - 2) left = c - g_cols + 6;
    if (left < 0) left = 0;
}

/* ---------------------------------------------------------------- edicion con historial */
static void ur_free(struct ur *r) { if (r->d) kfree(r->d); if (r->i) kfree(r->i); r->d = r->i = 0; }
static char *dup_n(const char *s, uint32_t n, uint32_t cap) { char *p = (char *)kmalloc(cap ? cap : 1); if (p && n) memcpy(p, s, n); return p; }

static int raw_replace(uint32_t a, uint32_t b, const char *ins, uint32_t n)
{
    if (b < a) b = a;
    if (b > N) b = N;
    if (N - (b - a) + n >= CE_MAX) return 0;
    memmove(B + a + n, B + b, N - b);
    if (n) memcpy(B + a, ins, n);
    N = N - (b - a) + n;
    rebuild();
    return 1;
}
/* kind: 0 normal, 1 escritura (se agrupa), 2 grupo forzado nuevo */
static int edit(uint32_t a, uint32_t b, const char *ins, uint32_t n, int kind)
{
    if (ro) { scpy(banner, "Solo lectura mientras SOMA modifica este archivo", sizeof banner); return 0; }
    uint64_t now = timer_get_uptime_ms();
    if (kind == 1 && ut > 0 && ut == un && a == b && n == 1 && ins[0] != '\n' && ins[0] != ' ') {
        struct ur *p = &U[ut - 1];
        if (p->kind == 1 && p->dn == 0 && p->pos + p->in == a && now - p->t < 2000) {
            if (p->in + 1 > p->icap) { uint32_t nc = p->icap * 2 + 16; char *q = dup_n(p->i, p->in, nc); if (!q) return 0; kfree(p->i); p->i = q; p->icap = nc; }
            if (!raw_replace(a, b, ins, n)) return 0;
            p->i[p->in++] = ins[0]; p->t = now; dirty = 1;
            return 1;
        }
    }
    for (int k = ut; k < un; k++) ur_free(&U[k]);
    un = ut;
    if (saved_ut > ut) saved_ut = -1;
    if (un == CE_UN) { ur_free(&U[0]); memmove(&U[0], &U[1], sizeof(U[0]) * (CE_UN - 1)); un--; ut--; if (saved_ut >= 0) saved_ut--; }
    struct ur *r = &U[un];
    r->pos = a; r->dn = b - a; r->d = dup_n(B + a, b - a, b - a); r->in = n; r->icap = n + 16; r->i = dup_n(ins, n, n + 16);
    r->cb = ce_cur; r->ab = ce_anc; r->t = now; r->kind = (uint8_t)kind;
    if (!r->d || !r->i || !raw_replace(a, b, ins, n)) { ur_free(r); return 0; }
    un++; ut = un; dirty = 1;
    return 1;
}
static void after_edit(uint32_t p) { ce_cur = ce_anc = p; want_col = -1; ensure_visible(); }
static void replace_sel(const char *s, uint32_t n, int kind)
{
    uint32_t a, b; sel_range(&a, &b);
    if (edit(a, b, s, n, kind)) after_edit(a + n);
}
int ce_undo(void)
{
    if (ut <= 0 || ro) return 0;
    struct ur *r = &U[--ut];
    raw_replace(r->pos, r->pos + r->in, r->d, r->dn);
    ce_cur = r->cb; ce_anc = r->ab; if (ce_cur > N) ce_cur = N; if (ce_anc > N) ce_anc = N;
    dirty = (ut != saved_ut); want_col = -1; ensure_visible(); return 1;
}
int ce_redo(void)
{
    if (ut >= un || ro) return 0;
    struct ur *r = &U[ut++];
    raw_replace(r->pos, r->pos + r->dn, r->i, r->in);
    after_edit(r->pos + r->in); dirty = (ut != saved_ut); return 1;
}
static void hist_clear(void) { for (int k = 0; k < un; k++) ur_free(&U[k]); un = ut = 0; saved_ut = 0; }

/* ---------------------------------------------------------------- archivo */
static uint32_t normalize(const char *s, uint32_t n, char *out, uint32_t cap)
{
    uint32_t o = 0;
    for (uint32_t i = 0; i < n && o + 4 < cap; i++) {
        char c = s[i];
        if (c == '\r') continue;
        if (c == '\t') { for (int k = 0; k < 4; k++) out[o++] = ' '; continue; }
        out[o++] = c;
    }
    return o;
}
int ce_load(const char *p)
{
    uint32_t len = 0; char *d;
    if (!p || !*p) return 0;
    int sz = vfs_size(p);
    if (sz > CE_MAX - 1024) return -1;
    d = vfs_read_alloc(p, &len);
    hist_clear();
    scpy(path_, p, sizeof path_);
    N = d ? normalize(d, len, B, CE_MAX) : 0;
    if (d) kfree(d);
    rebuild(); ce_cur = ce_anc = 0; top = left = 0; want_col = -1; dirty = 0; err_line = 0; err_msg[0] = 0;
    return d ? 1 : 0;
}
static const char *ce_nuevo_def(void)                                   /* SOMA_363_ACCESO: /usuario/Proyectos/nuevo.soma */
{
    static char b[96];
    if (!acceso_usuario() || acceso_carpeta(ACC_PROYECTOS, b, sizeof b - 12) != 0) return "/workspace/nuevo.soma";
    uint32_t n = 0; while (b[n]) n++;
    const char *t = "/nuevo.soma"; while (*t) b[n++] = *t++; b[n] = 0;
    return b;
}
void ce_new(const char *p) { hist_clear(); scpy(path_, p && *p ? p : ce_nuevo_def(), sizeof path_); N = 0; rebuild(); ce_cur = ce_anc = 0; top = left = 0; dirty = 0; err_line = 0; err_msg[0] = 0; }
int ce_save(void)
{
    if (vfs_write(path_, B, N) < 0) return 0;
    dirty = 0; saved_ut = ut; return 1;
}
int ce_save_as(const char *p) { char old[128]; scpy(old, path_, sizeof old); scpy(path_, p, sizeof path_); if (ce_save()) return 1; scpy(path_, old, sizeof path_); return 0; }
/* SOMA_180: un solo bufer temporal para recargar, reemplazar todo, reescribir lineas y pegar (antes cuatro de CE_MAX, 768 KiB de BSS de mas:
 * la imagen del kernel rozaba el limite de 14 MiB). Cada funcion lo llena y lo entrega a edit() antes de volver; ninguna llama a otra. */
/* SOMA_323_BSS: y ya no ocupa memoria hasta que se usa el editor (se reserva la primera vez y se queda) */
static char *ce_tmp_p;
static char *ce_tmp(void) { if (!ce_tmp_p) ce_tmp_p = (char *)kmalloc(CE_MAX); return ce_tmp_p; }
int ce_reload_keep_undo(void)
{
    uint32_t len = 0; char *d = vfs_read_alloc(path_, &len);
    if (!d) return 0;
    char *const tmp = ce_tmp();
    if (!tmp) { kfree(d); return 0; }
    uint32_t n = normalize(d, len, tmp, CE_MAX); kfree(d);
    if (n == N && memcmp(tmp, B, n) == 0) return 0;
    int l = line_of(ce_cur), sro = ro; ro = 0;
    ce_anc = 0; ce_cur = N;
    if (edit(0, N, tmp, n, 2)) { int ll = l < NL ? l : NL - 1; ce_cur = ce_anc = lstart(ll); }
    ro = sro; saved_ut = ut; dirty = 0; ensure_visible();
    return 1;
}
const char *ce_path(void) { return path_; }
int ce_dirty(void) { return dirty; }
const char *ce_text(uint32_t *len) { if (len) *len = N; B[N < CE_MAX ? N : CE_MAX - 1] = 0; return B; }
void ce_set_readonly(int r, const char *b) { ro = r; scpy(banner, r && b ? b : "", sizeof banner); }
void ce_set_error(int line, const char *msg) { err_line = line; scpy(err_msg, msg ? msg : "", sizeof err_msg); }
void ce_goto_line(int line) { if (line < 1) line = 1; if (line > NL) line = NL; ce_cur = ce_anc = lstart(line - 1) + indent_of(line - 1); top = line - 1 - g_rows / 3; if (top < 0) top = 0; ensure_visible(); }
void ce_status(int *l, int *c, int *lines, int *sc)
{
    uint32_t a, b; sel_range(&a, &b);
    if (l) *l = line_of(ce_cur) + 1;
    if (c) *c = vcol(ce_cur) + 1;
    if (lines) *lines = NL;
    if (sc) *sc = (int)(b - a);
}

/* ---------------------------------------------------------------- busqueda */
static int match_at(uint32_t p, const char *s, int n)
{
    if (p + (uint32_t)n > N) return 0;
    for (int i = 0; i < n; i++) if (lc((uint8_t)B[p + i]) != lc((uint8_t)s[i])) return 0;
    return 1;
}
static int find_from(uint32_t from, int dir, uint32_t *out)
{
    int n = slen(find_s); if (!n || (uint32_t)n > N) return 0;
    if (dir > 0) {
        for (uint32_t p = from; p + n <= N; p++) if (match_at(p, find_s, n)) { *out = p; return 1; }
        for (uint32_t p = 0; p < from && p + n <= N; p++) if (match_at(p, find_s, n)) { *out = p; return 1; }
    } else {
        for (int64_t p = (int64_t)from - 1; p >= 0; p--) if (match_at((uint32_t)p, find_s, n)) { *out = (uint32_t)p; return 1; }
        for (int64_t p = (int64_t)N - n; p >= (int64_t)from; p--) if (match_at((uint32_t)p, find_s, n)) { *out = (uint32_t)p; return 1; }
    }
    return 0;
}
static void count_matches(int *total, int *idx)
{
    int n = slen(find_s); *total = 0; *idx = 0; if (!n) return;
    uint32_t a, b; sel_range(&a, &b);
    for (uint32_t p = 0; p + n <= N; p++) if (match_at(p, find_s, n)) { (*total)++; if (p == a) *idx = *total; p += n - 1; }
}
void ce_find_next(int dir)
{
    uint32_t a, b, p; sel_range(&a, &b);
    if (find_from(dir > 0 ? (has_sel() ? b : ce_cur) : a, dir, &p)) { ce_anc = p; ce_cur = p + (uint32_t)slen(find_s); top = line_of(p) - g_rows / 3; if (top < 0) top = 0; ensure_visible(); }
}
static void replace_one(void)
{
    uint32_t a, b; sel_range(&a, &b); int n = slen(find_s);
    if (n && b - a == (uint32_t)n && match_at(a, find_s, n)) replace_sel(pr_b, (uint32_t)slen(pr_b), 0);
    ce_find_next(1);
}
static void replace_all(void)
{
    int n = slen(find_s), rn = slen(pr_b); if (!n) return;
    char *const tmp = ce_tmp(); uint32_t o = 0; int cnt = 0;
    if (!tmp) return;
    for (uint32_t p = 0; p < N;) {
        if (p + n <= N && match_at(p, find_s, n)) { if (o + rn >= CE_MAX - 1) return; memcpy(tmp + o, pr_b, (uint32_t)rn); o += rn; p += n; cnt++; }
        else tmp[o++] = B[p++];
        if (o >= CE_MAX - 1) return;
    }
    if (!cnt) return;
    uint32_t l = (uint32_t)line_of(ce_cur);
    if (edit(0, N, tmp, o, 2)) { ce_cur = ce_anc = lstart((int)l); ensure_visible(); }
}

/* ---------------------------------------------------------------- operaciones de bloque */
static void block_lines(int *l0, int *l1)
{
    uint32_t a, b; sel_range(&a, &b);
    *l0 = line_of(a); *l1 = line_of(b);
    if (*l1 > *l0 && b == lstart(*l1)) (*l1)--;
}
/* reescribe las lineas l0..l1 aplicando fn a cada una; un solo paso de deshacer */
static void rewrite_lines(int l0, int l1, int mode, int arg)
{
    char *const tmp = ce_tmp(); uint32_t o = 0;
    if (!tmp) return;
    uint32_t s = lstart(l0), e = lend(l1);
    int all_com = 1, minind = 1 << 30;
    for (int l = l0; l <= l1; l++) {
        uint32_t ls = lstart(l), le = lend(l), ind = indent_of(l);
        if (ls + ind == le) continue;
        if ((int)ind < minind) minind = (int)ind;
        if (!(ls + ind + 1 < le && B[ls + ind] == '/' && B[ls + ind + 1] == '/')) all_com = 0;
    }
    if (minind == 1 << 30) { minind = 0; all_com = 0; }
    for (int l = l0; l <= l1; l++) {
        uint32_t ls = lstart(l), le = lend(l), ind = indent_of(l), p = ls;
        if (mode == 0) {                        /* indentar */
            if (le > ls) for (int k = 0; k < arg; k++) tmp[o++] = ' ';
        } else if (mode == 1) {                 /* desindentar */
            uint32_t k = ind < (uint32_t)arg ? ind : (uint32_t)arg; p += k;
        } else if (mode == 2) {                 /* comentar / descomentar */
            if (ls + ind == le) { /* vacia */ }
            else if (all_com) { memcpy(tmp + o, B + ls, ind); o += ind; p = ls + ind + 2; if (p < le && B[p] == ' ') p++; }
            else { memcpy(tmp + o, B + ls, (uint32_t)minind); o += (uint32_t)minind; tmp[o++] = '/'; tmp[o++] = '/'; tmp[o++] = ' '; p = ls + (uint32_t)minind; }
        }
        while (p < le) tmp[o++] = B[p++];
        if (l < l1) tmp[o++] = '\n';
        if (o > CE_MAX - 64) return;
    }
    int had = has_sel();
    if (edit(s, e, tmp, o, 2)) {
        if (had || l1 > l0) { ce_anc = lstart(l0); ce_cur = lend(l1); }
        else { ce_cur = ce_anc = lend(l0); }
        ensure_visible();
    }
}
void ce_toggle_comment(void) { int a, b; block_lines(&a, &b); rewrite_lines(a, b, 2, 0); }
static void dup_line(void)
{
    int l = line_of(ce_cur); uint32_t s = lstart(l), e = lend(l), c = ce_cur - s;
    static char tmp[4096]; uint32_t n = e - s; if (n + 1 > sizeof tmp) return;
    tmp[0] = '\n'; memcpy(tmp + 1, B + s, n);
    if (edit(e, e, tmp, n + 1, 2)) after_edit(lstart(l + 1) + c);
}

/* ---------------------------------------------------------------- portapapeles */
int ce_clip_op(int op)
{
    if (pr_kind && pr_focus) {                          /* PARCHE 099: portapapeles en la barra de busqueda / rutas */
        tf_ready(); tf_clip(pr_focus == 2 ? &tfb : &tfa, op);
        if (pr_focus == 1 && (pr_kind == CE_PR_FIND || pr_kind == CE_PR_REPLACE)) scpy(find_s, pr_a, sizeof find_s);
        return 1;
    }
    uint32_t a, b; sel_range(&a, &b);
    if (op == CLIP_OP_PASTE) {
        uint32_t n = 0; const char *t = clip_text(&n);
        if (!t || !n) return 1;
        char *const tmp = ce_tmp(); if (!tmp) return 1;
        uint32_t m = normalize(t, n, tmp, CE_MAX);
        replace_sel(tmp, m, 2);
        return 1;
    }
    if (!has_sel()) { int l = line_of(ce_cur); a = lstart(l); b = (l + 1 < NL) ? LS[l + 1] : N; }   /* sin seleccion: la linea entera */
    if (b > a) clip_set_text(B + a, b - a);
    if (op == CLIP_OP_CUT && b > a && !ro) { if (edit(a, b, "", 0, 2)) after_edit(a); }
    return 1;
}

/* ---------------------------------------------------------------- prompts */
void ce_prompt(int kind)
{
    pr_kind = kind; pr_focus = 1; pr_b[0] = pr_b[0] ? pr_b[0] : 0;
    if (kind == CE_PR_FIND || kind == CE_PR_REPLACE) {
        uint32_t a, b; sel_range(&a, &b);
        if (b > a && b - a < sizeof pr_a - 1 && line_of(a) == line_of(b)) { memcpy(pr_a, B + a, b - a); pr_a[b - a] = 0; }
        else scpy(pr_a, find_s, sizeof pr_a);
        scpy(find_s, pr_a, sizeof find_s);
    } else if (kind == CE_PR_GOTO) pr_a[0] = 0;
    else if (kind == CE_PR_OPEN) { if (acceso_usuario() && acceso_carpeta(ACC_PROYECTOS, pr_a, sizeof pr_a - 1) == 0) { uint32_t n = 0; while (pr_a[n]) n++; pr_a[n] = '/'; pr_a[n + 1] = 0; } else scpy(pr_a, "/workspace/", sizeof pr_a); }   /* SOMA_363_ACCESO */
    else if (kind == CE_PR_SAVEAS) scpy(pr_a, path_, sizeof pr_a);
    tf_ready(); tf_set(&tfa, pr_a); tf_set(&tfb, pr_b);
    if (kind == CE_PR_SAVEAS) {                     /* selecciona el nombre sin la carpeta ni la extension */
        int n = slen(pr_a), sl = 0, dot = n; for (int i = 0; i < n; i++) { if (pr_a[i] == '/') sl = i + 1; if (pr_a[i] == '.') dot = i; }
        if (dot < sl) dot = n;
        tfa.anc = (uint32_t)sl; tfa.cur = (uint32_t)dot;
    } else if (kind == CE_PR_FIND || kind == CE_PR_REPLACE) tf_select_all(&tfa);
}
const char *ce_prompt_value(void) { return pr_a; }
int ce_escape(void)
{
    if (pr_kind) { pr_kind = 0; return 1; }
    if (has_sel()) { ce_anc = ce_cur; return 1; }
    return 0;
}
static int prompt_key(int k, int ctrl, int shift)
{
    tf_ready();
    struct tfield *f = (pr_focus == 2) ? &tfb : &tfa;
    if (k == 16 && (pr_kind == CE_PR_FIND || pr_kind == CE_PR_REPLACE)) { ce_find_next(-1); return CE_ACT_REDRAW; }
    if (k == 14 && (pr_kind == CE_PR_FIND || pr_kind == CE_PR_REPLACE)) { ce_find_next(1); return CE_ACT_REDRAW; }
    int r = tf_key(f, (char)k, ctrl, shift);
    if (r == TF_ESC) { pr_kind = 0; return CE_ACT_REDRAW; }
    if (r == TF_TAB) { if (pr_kind == CE_PR_REPLACE) pr_focus = pr_focus == 1 ? 2 : 1; return CE_ACT_REDRAW; }
    if (r == TF_ENTER) {
        switch (pr_kind) {
        case CE_PR_FIND: ce_find_next(1); break;
        case CE_PR_REPLACE: if (pr_focus == 2) replace_one(); else ce_find_next(1); break;
        case CE_PR_GOTO: { int v = 0; for (int i = 0; pr_a[i] >= '0' && pr_a[i] <= '9'; i++) v = v * 10 + (pr_a[i] - '0'); if (v > 0) ce_goto_line(v); pr_kind = 0; break; }
        case CE_PR_OPEN: pr_kind = 0; return CE_ACT_OPEN;
        case CE_PR_SAVEAS: pr_kind = 0; return CE_ACT_SAVEAS;
        }
        return CE_ACT_REDRAW;
    }
    if (f == &tfa && (pr_kind == CE_PR_FIND || pr_kind == CE_PR_REPLACE) && r == TF_EDIT) {   /* busqueda incremental */
        scpy(find_s, pr_a, sizeof find_s);
        uint32_t a, b, p; sel_range(&a, &b);
        if (find_s[0] && find_from(a, 1, &p)) { ce_anc = p; ce_cur = p + (uint32_t)slen(find_s); top = line_of(p) - g_rows / 3; if (top < 0) top = 0; ensure_visible(); }
    }
    return CE_ACT_REDRAW;
}

/* ---------------------------------------------------------------- teclado */
static void move_to(uint32_t p, int extend, int keep_col)
{
    ce_cur = p; if (!extend) ce_anc = p;
    if (!keep_col) want_col = -1;
    ensure_visible();
}
static void vmove(int dl, int extend)
{
    if (!extend && has_sel()) { uint32_t a, b; sel_range(&a, &b); ce_cur = dl < 0 ? a : b; ce_anc = ce_cur; }
    int l = line_of(ce_cur); if (want_col < 0) want_col = vcol(ce_cur);
    int nl = l + dl; if (nl < 0) { move_to(0, extend, 0); return; } if (nl >= NL) { move_to(N, extend, 0); return; }
    move_to(pos_at(nl, want_col), extend, 1);
}
static void do_enter(void)
{
    uint32_t a, b; sel_range(&a, &b);
    int l = line_of(a); uint32_t ls = lstart(l), ind = indent_of(l);
    if (ind > a - ls) ind = a - ls;
    uint32_t q = a; while (q > ls && B[q - 1] == ' ') q--;
    int open = (q > ls && (B[q - 1] == '{' || B[q - 1] == '(' || B[q - 1] == '['));
    int close = (b < N && (B[b] == '}' || B[b] == ')' || B[b] == ']'));
    char t[512]; uint32_t n = 0; t[n++] = '\n';
    for (uint32_t k = 0; k < ind && n < 200; k++) t[n++] = ' ';
    if (open) for (int k = 0; k < 4; k++) t[n++] = ' ';
    uint32_t caret = n;
    if (open && close) { t[n++] = '\n'; for (uint32_t k = 0; k < ind && n < 500; k++) t[n++] = ' '; }
    if (edit(a, b, t, n, 2)) after_edit(a + caret);
}
static void do_backspace(void)
{
    if (has_sel()) { replace_sel("", 0, 2); return; }
    if (ce_cur == 0) return;
    int l = line_of(ce_cur); uint32_t ls = lstart(l), c = ce_cur - ls;
    if (c > 0 && indent_of(l) >= c) {             /* en la sangria: borra hasta el tope de tabulacion anterior */
        uint32_t k = c % 4 ? c % 4 : 4;
        if (edit(ce_cur - k, ce_cur, "", 0, 2)) after_edit(ce_cur - k);
        return;
    }
    uint32_t p = prev_cp(ce_cur);
    if (edit(p, ce_cur, "", 0, 2)) after_edit(p);
}
static void do_tab(int shift)
{
    uint32_t a, b; sel_range(&a, &b);
    if (shift || (has_sel() && line_of(a) != line_of(b))) { int l0, l1; block_lines(&l0, &l1); rewrite_lines(l0, l1, shift ? 1 : 0, 4); return; }
    int c = vcol(a); int k = 4 - (c % 4);
    replace_sel("    ", (uint32_t)k, 2);
}
static void type_char(const char *s, int n)
{
    if (n == 1 && s[0] == '}' && !has_sel()) {                /* cierra bloque: quita una sangria */
        int l = line_of(ce_cur); uint32_t ls = lstart(l), ind = indent_of(l);
        if (ls + ind == ce_cur && ind >= 4) { char t[2] = "}"; if (edit(ce_cur - 4, ce_cur, t, 1, 2)) after_edit(ce_cur - 3); return; }
    }
    replace_sel(s, (uint32_t)n, 1);
}
static uint8_t u8b[4]; static int u8n, u8need;

int ce_key(char ch, int ctrl, int shift)
{
    int k = (uint8_t)ch;
    font_setup();
    if (pr_kind && pr_focus) {
        if (ctrl && (k == 6 || k == 7)) { ce_prompt(k == 6 ? CE_PR_FIND : CE_PR_GOTO); return CE_ACT_REDRAW; }
        return prompt_key(k, ctrl, shift);
    }
    /* atajos con Ctrl (los que chocan con teclas de cursor exigen Ctrl pulsado) */
    if (ctrl) {
        switch (k) {
        case 1: ce_anc = 0; ce_cur = N; return CE_ACT_REDRAW;
        case 6: ce_prompt(CE_PR_FIND); return CE_ACT_REDRAW;
        case 8: ce_prompt(CE_PR_REPLACE); return CE_ACT_REDRAW;
        case 4: dup_line(); return CE_ACT_REDRAW;
        case 5: return CE_ACT_RUN;
        case 2: return CE_ACT_VERIFY;
        case 14: return CE_ACT_NEW;
        case 16: case 28: top = top > 0 ? top - 1 : 0; return CE_ACT_REDRAW;
        case 29: if (top + 1 < NL) top++; return CE_ACT_REDRAW;
        default: break;
        }
    }
    switch (k) {
    case 19: return CE_ACT_SAVE;
    case 26: ce_undo(); return CE_ACT_REDRAW;
    case 18: ce_redo(); return CE_ACT_REDRAW;
    case 7:  ce_prompt(CE_PR_GOTO); return CE_ACT_REDRAW;
    case 15: ce_prompt(CE_PR_OPEN); return CE_ACT_REDRAW;
    case 11: ce_toggle_comment(); return CE_ACT_REDRAW;
    case 12: { int l = line_of(ce_cur); ce_anc = lstart(l); ce_cur = (l + 1 < NL) ? LS[l + 1] : N; return CE_ACT_REDRAW; }
    case 16: vmove(-1, shift); return CE_ACT_REDRAW;
    case 14: vmove(1, shift); return CE_ACT_REDRAW;
    case 28: vmove(-(g_rows - 1), shift); top -= g_rows - 1; if (top < 0) top = 0; ensure_visible(); return CE_ACT_REDRAW;
    case 29: vmove(g_rows - 1, shift); return CE_ACT_REDRAW;
    case 2: if (!shift && has_sel()) { uint32_t a, b; sel_range(&a, &b); move_to(a, 0, 0); } else move_to(prev_cp(ce_cur), shift, 0); return CE_ACT_REDRAW;
    case 6: if (!shift && has_sel()) { uint32_t a, b; sel_range(&a, &b); move_to(b, 0, 0); } else move_to(next_cp(ce_cur), shift, 0); return CE_ACT_REDRAW;
    case 1: { int l = line_of(ce_cur); uint32_t s = lstart(l), fi = s + indent_of(l); move_to(ce_cur == fi ? s : fi, shift, 0); return CE_ACT_REDRAW; }
    case 5: move_to(lend(line_of(ce_cur)), shift, 0); return CE_ACT_REDRAW;
    case 4: if (has_sel()) replace_sel("", 0, 2); else if (ce_cur < N && edit(ce_cur, next_cp(ce_cur), "", 0, 2)) after_edit(ce_cur); return CE_ACT_REDRAW;
    case 8: case 127: do_backspace(); return CE_ACT_REDRAW;
    case '\r': case '\n': do_enter(); return CE_ACT_REDRAW;
    case '\t': do_tab(shift); return CE_ACT_REDRAW;
    case 27: ce_escape(); return CE_ACT_REDRAW;
    default: break;
    }
    if (k >= 0x80) {
        if (u8n == 0) { u8need = k >= 0xF0 ? 4 : k >= 0xE0 ? 3 : k >= 0xC0 ? 2 : 0; if (!u8need) return 0; }
        else if ((k & 0xC0) != 0x80) { u8n = 0; return 0; }
        u8b[u8n++] = (uint8_t)k; if (u8n < u8need) return 0;
        char s[4]; for (int i = 0; i < u8n; i++) s[i] = (char)u8b[i];
        int n = u8n; u8n = 0; type_char(s, n); return CE_ACT_REDRAW;
    }
    if (k >= 32) { char s[1] = { ch }; type_char(s, 1); return CE_ACT_REDRAW; }
    return CE_ACT_NONE;
}

/* ---------------------------------------------------------------- raton */
static uint32_t pos_from_xy(int mx, int my)
{
    int l = top + (my - g_ty) / g_lh; if (my < g_ty) l = top - 1;
    if (l < 0) return 0;
    if (l >= NL) return N;
    int rel = mx - g_tx; int c = rel <= 0 ? 0 : (int)(((int64_t)rel * 64 + g_cw64 / 2) / g_cw64);
    return pos_at(l, c + left);
}
int ce_press(int mx, int my)
{
    font_setup();
    if (my < g_y || my >= g_y + g_h || mx < g_x || mx >= g_x + g_w) return 0;
    if (pr_kind) {
        extern int keyboard_shift_held(void);
        tf_ready();
        if (in_r(&r_close, mx, my)) { pr_kind = 0; return 1; }
        if (tf_hit(&tfa, mx, my)) { pr_focus = 1; tf_press(&tfa, mx, my, keyboard_shift_held()); return 1; }
        if (pr_kind == CE_PR_REPLACE && tf_hit(&tfb, mx, my)) { pr_focus = 2; tf_press(&tfb, mx, my, keyboard_shift_held()); return 1; }
        if (in_r(&r_prev, mx, my)) { ce_find_next(-1); return 1; }
        if (in_r(&r_next, mx, my)) { ce_find_next(1); return 1; }
        if (in_r(&r_rep, mx, my)) { replace_one(); return 1; }
        if (in_r(&r_all, mx, my)) { replace_all(); return 1; }
        if (in_r(&r_fa, mx, my)) { pr_focus = 1; return 1; }
        if (in_r(&r_fb, mx, my)) { pr_focus = 2; return 1; }
        if (in_r(&r_ok, mx, my)) { pr_focus = 1; return 1; }
        if (my < g_ty - (banner[0] ? 24 : 0)) return 1;
    }
    if (mx >= g_sbx) { g_sbdrag = 1; ce_mouse(mx, my, 1, 0); return 1; }
    if (my < g_ty) return 1;
    pr_focus = 0;
    uint32_t p = pos_from_xy(mx, my);
    uint64_t now = timer_get_uptime_ms();
    if (now - g_lclick < 450 && (p == g_lpos || (g_clicks >= 2 && line_of(p) == line_of(g_lpos)))) g_clicks++; else g_clicks = 1;
    g_lclick = now; g_lpos = p;
    if (g_clicks == 2) {                                            /* palabra */
        uint32_t a = p, b = p;
        while (a > 0 && is_word((uint8_t)B[a - 1])) a--;
        while (b < N && is_word((uint8_t)B[b])) b++;
        ce_anc = a; ce_cur = b; g_drag = 0; return 1;
    }
    if (g_clicks >= 3) { int l = line_of(p); ce_anc = lstart(l); ce_cur = (l + 1 < NL) ? LS[l + 1] : N; g_drag = 0; return 1; }
    extern int keyboard_shift_held(void);
    ce_cur = p; if (!keyboard_shift_held()) ce_anc = p;
    want_col = -1; g_drag = 1;
    return 1;
}
int ce_mouse(int mx, int my, int held, int wheel)
{
    int rd = 0;
    if (tf_ok && pr_kind) { if (tf_drag(&tfa, mx, my, held)) rd = 1; if (tf_drag(&tfb, mx, my, held)) rd = 1; }
    if (wheel && mx >= g_x && mx < g_x + g_w && my >= g_y && my < g_y + g_h) {
        top += wheel * 3; if (top > NL - 1) top = NL - 1; if (top < 0) top = 0; rd = 1;
    }
    if (!held) { if (g_drag || g_sbdrag) rd = 1; g_drag = g_sbdrag = 0; return rd; }
    if (g_sbdrag) {
        int span = g_th > 1 ? g_th : 1, maxt = NL - g_rows > 0 ? NL - g_rows : 0;
        int t = (int)(((int64_t)(my - g_ty) * (NL > 0 ? NL : 1)) / span) - g_rows / 2;
        if (t < 0) t = 0;
        if (t > maxt) t = maxt;
        if (t != top) { top = t; rd = 1; }
        return rd;
    }
    if (g_drag) {
        if (my < g_ty && top > 0) top--;
        else if (my > g_ty + g_th && top + g_rows < NL) top++;
        uint32_t p = pos_from_xy(mx, my);
        if (p != ce_cur) { ce_cur = p; rd = 1; }
        if (mx > g_tx + g_tw - 8 && vcol(ce_cur) >= left + g_cols - 2) { left++; rd = 1; }
        if (mx < g_tx && left > 0) { left--; rd = 1; }
    }
    return rd;
}

/* ---------------------------------------------------------------- pintado */
static const char *const KW[] = { "if", "else", "while", "for", "loop", "break", "continue", "return", 0 };
static const char *const DECL[] = { "fn", "let", "proc", "agent", "spawn", "import", 0 };
static const char *const NS[] = { "win", "fb", "sys", "vfs", "mem", 0 };
static const char *const BUILT[] = { "print", "input", "ask", 0 };
static const char *const CONST[] = { "true", "false", "nil", 0 };
static int in_set(const char *s, int n, const char *const *set)
{
    for (int i = 0; set[i]; i++) { int k = 0; while (k < n && set[i][k] == s[k]) k++; if (k == n && !set[i][k]) return 1; }
    return 0;
}
static uint32_t word_color(uint32_t p, int n)
{
    const char *s = B + p;
    if (in_set(s, n, KW)) return K_KW;
    if (in_set(s, n, DECL)) return K_DECL;
    if (in_set(s, n, NS)) return K_NS;
    if (in_set(s, n, BUILT)) return K_BUILT;
    if (in_set(s, n, CONST)) return K_CONST;
    uint32_t q = p + (uint32_t)n; while (q < N && B[q] == ' ') q++;
    if (q < N && B[q] == '(') return K_CALL;
    if (p > 0 && B[p - 1] == '.') return K_CALL;
    return K_TXT;
}
/* dibuja un trozo [a,b) de la linea empezando en la columna visual c0 */
static void draw_span(int y, uint32_t a, uint32_t b, int c0, uint32_t col)
{
    /* recorta por la izquierda (desplazamiento horizontal) */
    while (a < b && c0 < left) { a = next_cp(a); c0++; }
    if (a >= b) return;
    int cols = 0; for (uint32_t i = a; i < b; i++) if (((uint8_t)B[i] & 0xC0) != 0x80) cols++;
    if (c0 - left > g_cols + 2) return;
    run(g_tx + colx(c0 - left), y, B + a, (int)(b - a), col);
    (void)cols;
}
static void draw_line(int l, int y)
{
    uint32_t s = lstart(l), e = lend(l), p = s; int c = 0;
    while (p < e) {
        uint8_t ch = (uint8_t)B[p];
        uint32_t q = p; uint32_t col;
        if ((ch == '/' && p + 1 < e && B[p + 1] == '/') || ch == '#') { q = e; col = K_COM; }
        else if (ch == '"' || ch == '\'') { q = p + 1; while (q < e && (uint8_t)B[q] != ch) { if (B[q] == '\\' && q + 1 < e) q++; q++; } if (q < e) q++; col = K_STR; }
        else if (ch >= '0' && ch <= '9') { while (q < e && (is_word((uint8_t)B[q]) || B[q] == '.')) q++; col = K_NUM; }
        else if (is_word(ch)) { while (q < e && is_word((uint8_t)B[q])) q++; col = word_color(p, (int)(q - p)); }
        else if (ch == ' ') { while (q < e && B[q] == ' ') q++; c += (int)(q - p); p = q; continue; }
        else { q = next_cp(p); col = K_PUNC; }
        int cc = 0; for (uint32_t i = p; i < q; i++) if (((uint8_t)B[i] & 0xC0) != 0x80) cc++;
        draw_span(y, p, q, c, col);
        c += cc; p = q;
    }
}
static int match_bracket(uint32_t p, uint32_t *out)
{
    char o = B[p], m; int dir;
    switch (o) { case '(': m = ')'; dir = 1; break; case '[': m = ']'; dir = 1; break; case '{': m = '}'; dir = 1; break;
                 case ')': m = '('; dir = -1; break; case ']': m = '['; dir = -1; break; case '}': m = '{'; dir = -1; break; default: return 0; }
    int depth = 0, steps = 0;
    for (int64_t i = p; i >= 0 && i < (int64_t)N && steps < 60000; i += dir, steps++) {
        if (B[i] == o) depth++; else if (B[i] == m) { if (--depth == 0) { *out = (uint32_t)i; return 1; } }
    }
    return 0;
}
static void hl_range(uint32_t a, uint32_t b, uint32_t color, int vis_from, int vis_to)
{
    if (b <= a) return;
    int la = line_of(a), lb = line_of(b);
    for (int l = la; l <= lb; l++) {
        if (l < vis_from || l > vis_to) continue;
        uint32_t s = l == la ? a : lstart(l), e = l == lb ? b : lend(l);
        int c0 = vcol(s), c1 = vcol(e); if (l != lb) c1++;
        int x0 = colx(c0 - left), x1 = colx(c1 - left);
        if (x1 < 0) continue;
        if (x0 < 0) x0 = 0;
        if (x1 > g_tw) x1 = g_tw;
        if (x1 > x0) fb_fill_rect((uint32_t)(g_tx + x0), (uint32_t)(g_ty + (l - top) * g_lh), (uint32_t)(x1 - x0), (uint32_t)g_lh, color);
    }
}
static void btn(struct rr *r, int x, int y, int w, const char *t, int hot)
{
    r->x = x; r->y = y; r->w = w; r->h = 24;
    fb_fill_rounded_rect(x, y, w, 24, 6, hot ? 0x1E3A5Fu : 0x162033u);
    int tw = ui_w(t); ui_text(x + (w - tw) / 2, y + 4, t, hot ? 0xFFFFFFu : K_DIM);
}
static void field(struct rr *r, struct tfield *t, int x, int y, int w, const char *label, int focus)
{
    int lw = ui_w(label);
    ui_text(x, y + 4, label, K_DIM);
    x += lw + 8; w -= lw + 8;
    r->x = x; r->y = y; r->w = w; r->h = 24;
    tf_render(t, x, y - 1, w, 26, focus, 0, K_TXT);
}

void ce_render(int x, int y, int w, int h, int focused)
{
    font_setup();
    g_x = x; g_y = y; g_w = w; g_h = h;
    r_prev.w = r_next.w = r_rep.w = r_all.w = r_close.w = r_fa.w = r_fb.w = r_ok.w = 0;
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, K_BG);
    int yy = y;
    if (pr_kind) {                                                   /* barra de busqueda / prompt */
        int bh = 34;
        fb_fill_rect((uint32_t)x, (uint32_t)yy, (uint32_t)w, (uint32_t)bh, K_BAR);
        fb_fill_rect((uint32_t)x, (uint32_t)(yy + bh - 1), (uint32_t)w, 1, K_BORD);
        int bx = x + 10, by = yy + 5, rx = x + w - 34;
        btn(&r_close, rx, by, 26, "x", 0);
        if (pr_kind == CE_PR_FIND || pr_kind == CE_PR_REPLACE) {
            int tot, idx; count_matches(&tot, &idx);
            char cnt[40]; int o = 0;
            if (!find_s[0]) scpy(cnt, "", sizeof cnt);
            else if (!tot) scpy(cnt, "Sin resultados", sizeof cnt);
            else { char t[12]; int v, k;
                #define PUTN(V) do { v = (V); k = 0; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) cnt[o++] = t[--k]; } while (0)
                if (idx) { PUTN(idx); cnt[o++] = ' '; cnt[o++] = 'd'; cnt[o++] = 'e'; cnt[o++] = ' '; }
                PUTN(tot); scpy(cnt + o, tot == 1 ? " resultado" : " resultados", (int)sizeof cnt - o);
                #undef PUTN
            }
            int rep = pr_kind == CE_PR_REPLACE;
            int right = rx - 8;
            if (rep) { right -= 70; btn(&r_all, right, by, 64, "Todo", 0); right -= 98; btn(&r_rep, right, by, 92, "Reemplazar", 0); }
            right -= 30; btn(&r_next, right, by, 26, "v", 0); right -= 30; btn(&r_prev, right, by, 26, "^", 0);
            int cw = ui_w(cnt); right -= cw + 12; ui_text(right + 4, by + 4, cnt, tot || !find_s[0] ? K_DIM : K_ERR);
            int avail = right - bx - 8;
            if (rep) { field(&r_fa, &tfa, bx, by, avail / 2 - 6, "Buscar", pr_focus == 1); field(&r_fb, &tfb, bx + avail / 2 + 6, by, avail / 2 - 6, "Por", pr_focus == 2); }
            else field(&r_fa, &tfa, bx, by, avail, "Buscar", pr_focus == 1);
        } else {
            const char *lab = pr_kind == CE_PR_GOTO ? "Ir a la linea" : pr_kind == CE_PR_OPEN ? "Abrir archivo" : "Guardar como";
            const char *hint = pr_kind == CE_PR_GOTO ? "Intro: ir" : "Intro: aceptar  Esc: cancelar";
            int hw = ui_w(hint); ui_text(rx - 12 - hw, by + 4, hint, K_DIM);
            field(&r_ok, &tfa, bx, by, rx - 24 - hw - bx, lab, pr_focus != 0);
        }
        yy += bh;
    }
    if (banner[0]) {
        fb_fill_rect((uint32_t)x, (uint32_t)yy, (uint32_t)w, 24, ro ? 0x3B2A08u : 0x162033u);
        ui_text(x + 12, yy + 4, banner, ro ? 0xFBBF24u : K_DIM);
        yy += 24;
        if (!ro) banner[0] = 0;   /* avisos puntuales: se ven una vez */
    }
    /* geometria del texto */
    int digits = 3; for (int v = NL; v >= 1000; v /= 10) digits++;
    g_gw = colx(digits) + 28;
    g_sbx = x + w - 12;
    g_tx = x + g_gw + 8; g_ty = yy + 4; g_tw = g_sbx - g_tx - 4; g_th = y + h - g_ty;
    g_rows = g_th / g_lh; if (g_rows < 1) g_rows = 1;
    g_cols = (int)(((int64_t)g_tw * 64) / g_cw64); if (g_cols < 4) g_cols = 4;
    if (top > NL - 1) top = NL - 1;
    if (top < 0) top = 0;
    int last = top + g_rows; if (last > NL - 1) last = NL - 1;
    int cl = line_of(ce_cur);
    fb_fill_rect((uint32_t)x, (uint32_t)yy, (uint32_t)g_gw, (uint32_t)(y + h - yy), K_GUT);
    fb_fill_rect((uint32_t)(x + g_gw), (uint32_t)yy, 1, (uint32_t)(y + h - yy), K_BORD);

    fb_set_clip(g_tx - 2, yy, g_sbx - g_tx + 2, y + h - yy);
    /* fondos: linea actual, error, coincidencias, seleccion */
    if (!has_sel() && cl >= top && cl <= last) fb_fill_rect((uint32_t)(x + g_gw + 1), (uint32_t)(g_ty + (cl - top) * g_lh), (uint32_t)(g_sbx - x - g_gw - 1), (uint32_t)g_lh, K_CURL);
    if (err_line > 0 && err_line - 1 >= top && err_line - 1 <= last) fb_fill_rect((uint32_t)(x + g_gw + 1), (uint32_t)(g_ty + (err_line - 1 - top) * g_lh), (uint32_t)(g_sbx - x - g_gw - 1), (uint32_t)g_lh, K_ERRBG);
    int fl = slen(find_s);
    if (fl && (pr_kind == CE_PR_FIND || pr_kind == CE_PR_REPLACE)) {
        uint32_t vs = lstart(top), ve = lend(last);
        for (uint32_t p = vs; p + fl <= ve; p++) if (match_at(p, find_s, fl)) { hl_range(p, p + fl, K_FIND, top, last); p += fl - 1; }
    }
    if (has_sel()) { uint32_t a, b; sel_range(&a, &b); hl_range(a, b, K_SEL, top, last); }
    { uint32_t m, at = (uint32_t)-1;                                    /* pareja de llaves */
      if (ce_cur < N && match_bracket(ce_cur, &m)) at = ce_cur; else if (ce_cur > 0 && match_bracket(ce_cur - 1, &m)) at = ce_cur - 1;
      if (at != (uint32_t)-1) { hl_range(at, at + 1, 0x2B3B55u, top, last); hl_range(m, m + 1, 0x2B3B55u, top, last); } }
    /* guias de sangria y texto */
    for (int l = top; l <= last; l++) {
        int ly = g_ty + (l - top) * g_lh;
        uint32_t ind = indent_of(l); if (lstart(l) + ind == lend(l)) { /* linea vacia: hereda de la anterior */ ind = l > 0 ? indent_of(l - 1) : 0; }
        for (uint32_t c = 4; c <= ind && c < 200; c += 4) { int gx = colx((int)c - 4 - left); if (gx >= 0) fb_fill_rect((uint32_t)(g_tx + gx + 1), (uint32_t)ly, 1, (uint32_t)g_lh, K_GUIDE); }
        draw_line(l, ly);
        if (err_line == l + 1 && err_msg[0]) {
            int ex = colx(vcol(lend(l)) - left) + 24; if (ex < 0) ex = 24;
            run(g_tx + ex, ly, err_msg, slen(err_msg), K_ERR);
        }
    }
    if (focused && !(pr_kind && pr_focus) && cl >= top && cl <= last) {
        int cx = colx(vcol(ce_cur) - left);
        if (cx >= 0) fb_fill_rect((uint32_t)(g_tx + cx), (uint32_t)(g_ty + (cl - top) * g_lh + 1), 2, (uint32_t)(g_lh - 2), K_CARET);
    }
    fb_reset_clip();
    /* numeros de linea */
    for (int l = top; l <= last; l++) {
        char t[12]; int v = l + 1, k = 0; char r[12]; do { r[k++] = (char)('0' + v % 10); v /= 10; } while (v);
        int n = 0; while (k) t[n++] = r[--k];
        int ly = g_ty + (l - top) * g_lh;
        if (err_line == l + 1) { fb_fill_rounded_rect(x + 5, ly + g_lh / 2 - 4, 8, 8, 4, K_ERR); }
        run(x + g_gw - 10 - colx(n), ly, t, n, l == cl ? K_GUTCUR : K_GUTTX);
    }
    /* barra de desplazamiento */
    fb_fill_rect((uint32_t)g_sbx, (uint32_t)yy, 12, (uint32_t)(y + h - yy), 0x080C16u);
    if (NL > g_rows) {
        int span = y + h - yy - 4, th = span * g_rows / NL; if (th < 24) th = 24;
        int ty = yy + 2 + (int)((int64_t)(span - th) * top / (NL - g_rows > 0 ? NL - g_rows : 1));
        fb_fill_rounded_rect(g_sbx + 3, ty, 6, th, 3, g_sbdrag ? 0x64748Bu : 0x334155u);
    }
    if (err_line > 0 && NL > 0) fb_fill_rect((uint32_t)(g_sbx + 2), (uint32_t)(yy + (int64_t)(y + h - yy) * (err_line - 1) / NL), 8, 3, K_ERR);
    if (has_sel() || (cl >= 0 && NL > g_rows)) fb_fill_rect((uint32_t)(g_sbx + 2), (uint32_t)(yy + (int64_t)(y + h - yy) * cl / (NL ? NL : 1)), 8, 2, K_CARET);
}


#undef B
#undef N
#undef LS
#undef NL
#undef ce_cur
#undef ce_anc
#undef want_col
#undef top
#undef left
#undef dirty
#undef ro
#undef path_
#undef banner
#undef err_line
#undef err_msg
#undef U
#undef un
#undef ut
#undef saved_ut
#undef pr_kind
#undef pr_focus
#undef pr_a
#undef pr_b
#undef tfa
#undef tfb
#undef tf_ok
#undef find_s

/* SOMA_216_STUDIO: instancias del editor */
int ce_inst_new(void)
{
    for (int k = 1; k <= CE_INST_MAX; ++k) {
        if (ce_iused[k]) continue;
        if (!ce_insts[k]) { ce_insts[k] = (struct ce_inst *)kmalloc(sizeof(struct ce_inst)); if (!ce_insts[k]) return 0; ce_idefaults(ce_insts[k]); }
        else if (k == 1) { CE = ce_insts[1]; hist_clear(); ce_idefaults(ce_insts[1]); }
        ce_iused[k] = 1;
        CE = ce_insts[k];
        return k;
    }
    return 0;
}
void ce_select(int slot) { if (slot >= 1 && slot <= CE_INST_MAX && ce_insts[slot]) CE = ce_insts[slot]; }
void ce_inst_drop(int slot)
{
    if (slot < 1 || slot > CE_INST_MAX || !ce_iused[slot]) return;
    struct ce_inst *keep = CE, *dead = ce_insts[slot];
    CE = dead; hist_clear();
    ce_iused[slot] = 0;
    if (slot != 1) { kfree(dead); ce_insts[slot] = 0; } else ce_idefaults(&ce_inst1);
    CE = keep == dead ? &ce_inst1 : keep;
}
const char *ce_inst_path(int slot)
{
    if (slot < 1 || slot > CE_INST_MAX || !ce_iused[slot] || !ce_insts[slot]) return "";
    return ce_insts[slot]->path_;
}
int ce_inst_dirty(int slot)                                     /* SOMA_219_AVISO */
{
    if (slot < 1 || slot > CE_INST_MAX || !ce_iused[slot] || !ce_insts[slot]) return 0;
    return ce_insts[slot]->dirty != 0;
}
