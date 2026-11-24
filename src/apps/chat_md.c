/* SOMA_285_CHAT: maquetador de Markdown + LaTeX + HTML basico de la aplicacion de chat (ver chat_md.h). */
#include "chat_md.h"
#include "tex.h"

/* ---------------------------------------------------------------- memoria de trabajo */
#define AR_MAX   (160u * 1024u)        /* textos desmontados (citas, listas, celdas) */
#define AT_MAX   5000                  /* piezas de un bloque */
#define TX_OPS   2600                  /* operaciones de las formulas de un bloque */
#define TX_POOL  (24u * 1024u)
#define CEL_MAX  256                   /* celdas de una tabla */
enum { A_WORD = 0, A_SPACE, A_MATH, A_BR, A_IMG };
enum { D_UNDER = 1, D_STRIKE = 2, D_CODE = 4, D_SUP = 8, D_SUB = 16, D_MARK = 32 };
struct at {
    uint8_t k, fl, deco; int8_t dpx;
    uint32_t col;
    uint32_t s, n;                     /* WORD: texto en el pool de salida; IMG: origen en el pool */
    int32_t w, asc, desc;
    int16_t link;                      /* indice de zona (MDZ_LINK) o -1 */
    int16_t ox;                        /* MATH: desplazamiento x de sus operaciones; IMG: alto */
    int32_t m0, mn, oy;                /* MATH: operaciones en tx[] y desplazamiento y (la linea base queda en 0) */
};
struct md_scr {
    char ar[AR_MAX]; uint32_t nar;
    struct at a[AT_MAX]; int na;
    struct tex_op tx[TX_OPS]; int ntx;
    char tp[TX_POOL]; uint32_t ntp;
    int cel_a[CEL_MAX], cel_n[CEL_MAX], cel_h[CEL_MAX];
    uint8_t cls[4096];
};
typedef char md_scr_cabe[(sizeof(struct md_scr) <= MD_SCRATCH) ? 1 : -1];

struct L {
    const struct md_metrics *m; const struct md_theme *t; struct md_out *o; struct md_scr *S;
    int px; int depth;
    int lk_src, lk_cur, lk_y;          /* enlace que se esta pintando: una zona por linea */
};

/* ---------------------------------------------------------------- utilidades */
static int is_sp(char c) { return c == ' ' || c == '\t'; }
static int is_dig(char c) { return c >= '0' && c <= '9'; }
static int is_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_dig(c) || (uint8_t)c >= 0x80; }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int pre(const char *s, int n, const char *p) { int i = 0; while (p[i]) { if (i >= n || lower(s[i]) != lower(p[i])) return 0; i++; } return 1; }
static int W(struct L *L, const char *s, int n, int px, int fl) { return n > 0 ? L->m->width(s, n, px, fl, L->m->u) : 0; }
static int ASC(struct L *L, int px) { return L->m->ascent(px, L->m->u); }
static int DSC(struct L *L, int px) { return L->m->descent(px, L->m->u); }
static int u8back(const char *s, int n) { while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--; return n; }

static uint32_t pool_put(struct L *L, const char *s, uint32_t n)
{
    struct md_out *o = L->o;
    if (o->npool + n > o->poolcap) { o->full = 1; return o->npool; }
    for (uint32_t i = 0; i < n; i++) o->pool[o->npool + i] = s[i];
    uint32_t r = o->npool; o->npool += n; return r;
}
static struct md_op *op_new(struct L *L, int type)
{
    struct md_out *o = L->o;
    if (o->nops >= o->cap) { o->full = 1; return 0; }
    struct md_op *p = &o->ops[o->nops++];
    p->type = (uint8_t)type; p->fl = 0; p->px = 0; p->x = p->y = p->w = p->h = 0; p->rgb = 0; p->s = p->n = 0; p->zone = -1;
    return p;
}
static void op_text(struct L *L, int x, int y, uint32_t s, uint32_t n, int px, int fl, uint32_t col, int zone)
{
    struct md_op *p = op_new(L, MD_TEXT); if (!p) return;
    p->x = x; p->y = y; p->s = s; p->n = n; p->px = (int16_t)px; p->fl = (uint8_t)fl; p->rgb = col; p->zone = (int16_t)zone;
    if (x > L->o->width - 400) { int r = x + W(L, L->o->pool + s, (int)n, px, fl); if (r > L->o->width) L->o->width = r; }
}
static void op_text_str(struct L *L, int x, int y, const char *s, int px, int fl, uint32_t col)
{
    uint32_t n = 0; while (s[n]) n++;
    uint32_t at = pool_put(L, s, n); if (L->o->full) return;
    op_text(L, x, y, at, n, px, fl, col, -1);
}
static void op_rect(struct L *L, int x, int y, int w, int h, int r, uint32_t argb, int type)
{
    struct md_op *p = op_new(L, type); if (!p) return;
    p->x = x; p->y = y; p->w = w; p->h = h; p->px = (int16_t)r; p->rgb = argb;
}
static void op_line(struct L *L, int x0, int y0, int x1, int y1, int w16, uint32_t col)
{
    struct md_op *p = op_new(L, MD_LINE); if (!p) return;
    p->x = x0 * 16; p->y = y0 * 16; p->w = (x1 - x0) * 16; p->h = (y1 - y0) * 16; p->px = (int16_t)w16; p->rgb = col;
}
static int zone_new(struct L *L, int kind, uint32_t s, uint32_t n)
{
    struct md_out *o = L->o;
    if (o->nz >= o->zcap) { o->full = 1; return -1; }
    struct md_zone *z = &o->z[o->nz]; z->kind = (uint8_t)kind; z->x = z->y = z->w = z->h = 0; z->s = s; z->n = n;
    return o->nz++;
}
static int link_zone(struct L *L, int zi, int y)
{
    if (zi < 0) return -1;
    if (zi == L->lk_src && L->lk_cur >= 0) {
        if (y == L->lk_y) return L->lk_cur;
        int nz = zone_new(L, MDZ_LINK, L->o->z[zi].s, L->o->z[zi].n);
        if (nz < 0) return L->lk_cur;
        L->lk_cur = nz; L->lk_y = y; return nz;
    }
    L->lk_src = zi; L->lk_cur = zi; L->lk_y = y; return zi;
}
static void zone_grow(struct L *L, int zi, int x, int y, int w, int h)
{
    if (zi < 0) return;
    struct md_zone *z = &L->o->z[zi];
    if (z->w == 0 && z->h == 0) { z->x = x; z->y = y; z->w = w; z->h = h; return; }
    int x1 = z->x + z->w, y1 = z->y + z->h;
    if (x < z->x) z->x = x;
    if (y < z->y) z->y = y;
    if (x + w > x1) x1 = x + w;
    if (y + h > y1) y1 = y + h;
    z->w = x1 - z->x; z->h = y1 - z->y;
}

/* ---------------------------------------------------------------- entidades */
static int put_cp(char *d, uint32_t cp)
{
    if (cp < 0x80) { d[0] = (char)cp; return 1; }
    if (cp < 0x800) { d[0] = (char)(0xC0 | (cp >> 6)); d[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) { d[0] = (char)(0xE0 | (cp >> 12)); d[1] = (char)(0x80 | ((cp >> 6) & 63)); d[2] = (char)(0x80 | (cp & 63)); return 3; }
    d[0] = (char)(0xF0 | (cp >> 18)); d[1] = (char)(0x80 | ((cp >> 12) & 63)); d[2] = (char)(0x80 | ((cp >> 6) & 63)); d[3] = (char)(0x80 | (cp & 63)); return 4;
}
static int entidad(const char *s, int n, char *out, int *olen)          /* bytes consumidos (0 = no es entidad) */
{
    if (n < 3 || s[0] != '&') return 0;
    int e = 1; while (e < n && e < 12 && s[e] != ';' && !is_sp(s[e])) e++;
    if (e >= n || s[e] != ';') return 0;
    uint32_t cp = 0;
    if (s[1] == '#') {
        int i = 2, hex = 0; if (i < e && (s[i] == 'x' || s[i] == 'X')) { hex = 1; i++; }
        if (i >= e) return 0;
        for (; i < e; i++) {
            int c = lower(s[i]), v = is_dig((char)c) ? c - '0' : (hex && c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
            if (v < 0) return 0;
            cp = cp * (hex ? 16u : 10u) + (uint32_t)v; if (cp > 0x10FFFF) return 0;
        }
        if (!cp) return 0;
    } else {
        static const struct { const char *n; uint32_t cp; } E[] = {
            { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 }, { "copy", 0xA9 }, { "reg", 0xAE },
            { "deg", 0xB0 }, { "middot", 0xB7 }, { "times", 0xD7 }, { "divide", 0xF7 }, { "euro", 0x20AC }, { "hellip", 0x2026 }, { "mdash", 0x2014 },
            { "ndash", 0x2013 }, { "laquo", 0xAB }, { "raquo", 0xBB }, { "rarr", 0x2192 }, { "larr", 0x2190 }, { "harr", 0x2194 }, { "rArr", 0x21D2 },
            { "le", 0x2264 }, { "ge", 0x2265 }, { "ne", 0x2260 }, { "plusmn", 0xB1 }, { "infin", 0x221E }, { "check", 0x2713 }, { "bull", 0x2022 },
            { "trade", 0x2122 }, { "sect", 0xA7 }, { "para", 0xB6 }, { "iexcl", 0xA1 }, { "iquest", 0xBF }, { 0, 0 } };
        for (int k = 0; E[k].n; k++) {
            int j = 0; while (E[k].n[j] && 1 + j < e && E[k].n[j] == s[1 + j]) j++;
            if (!E[k].n[j] && 1 + j == e) { cp = E[k].cp; break; }
        }
        if (!cp) return 0;
    }
    *olen = put_cp(out, cp == 0xA0 ? ' ' : cp);
    return e + 1;
}

/* ---------------------------------------------------------------- piezas en linea */
struct sty { uint8_t fl, deco; int8_t dpx; uint32_t col; int16_t link; };
static struct at *at_new(struct L *L, int k, const struct sty *st)
{
    struct md_scr *S = L->S;
    if (S->na >= AT_MAX) { L->o->full = 1; return 0; }
    struct at *a = &S->a[S->na++];
    a->k = (uint8_t)k; a->fl = st->fl; a->deco = st->deco; a->dpx = st->dpx; a->col = st->col; a->link = st->link;
    a->s = a->n = 0; a->w = a->asc = a->desc = 0; a->ox = 0; a->m0 = a->mn = 0; a->oy = 0;
    return a;
}
static int sty_px(struct L *L, const struct sty *st) { int p = L->px + st->dpx; if (st->deco & (D_SUP | D_SUB)) p = p * 3 / 4; if (st->deco & D_CODE) p -= 1; return p < 8 ? 8 : p; }
static int sty_fl(const struct sty *st) { return st->fl | ((st->deco & D_CODE) ? MD_F_MONO : 0); }
static void at_measure(struct L *L, struct at *a)
{
    struct sty st = { a->fl, a->deco, a->dpx, a->col, a->link };
    int px = sty_px(L, &st);
    a->w = W(L, L->o->pool + a->s, (int)a->n, px, sty_fl(&st)) + ((a->deco & D_CODE) ? 8 : 0);
    a->asc = ASC(L, L->px + a->dpx); a->desc = DSC(L, L->px + a->dpx);
    if (a->deco & D_SUP) a->asc += px / 2;
    if (a->deco & D_SUB) a->desc += px / 3;
}
/* texto normal: se parte en palabras y espacios */
static void add_text(struct L *L, const char *s, int n, const struct sty *st)
{
    int i = 0;
    while (i < n) {
        if (is_sp(s[i]) || s[i] == '\n') {
            while (i < n && (is_sp(s[i]) || s[i] == '\n')) i++;
            struct at *a = at_new(L, A_SPACE, st); if (!a) return;
            int px = sty_px(L, st); a->w = W(L, " ", 1, px, sty_fl(st)); a->asc = ASC(L, px); a->desc = DSC(L, px);
            if (st->deco & D_CODE) { a->k = A_WORD; a->s = pool_put(L, " ", 1); a->n = 1; at_measure(L, a); }
            continue;
        }
        int j = i;
        /* palabra: hasta un espacio; se corta tambien despues de / - para que las URL largas partan */
        while (j < n && !is_sp(s[j]) && s[j] != '\n') { j++; if (j - i > 1 && (s[j - 1] == '/' || s[j - 1] == '-') && j < n && !is_sp(s[j])) break; }
        struct at *a = at_new(L, A_WORD, st); if (!a) return;
        a->s = pool_put(L, s + i, (uint32_t)(j - i)); a->n = (uint32_t)(j - i);
        at_measure(L, a);
        i = j;
    }
}
static void add_br(struct L *L, const struct sty *st) { struct at *a = at_new(L, A_BR, st); if (a) { a->asc = ASC(L, L->px); a->desc = DSC(L, L->px); } }

/* formulas */
static int tm_w(const char *s, int n, int px, int style, void *u)
{
    struct L *L = (struct L *)u; int fl = 0;
    if (style & TS_BOLD) fl |= MD_F_BOLD;
    if (style & TS_ITALIC) fl |= MD_F_ITALIC;
    if (style & TS_MONO) fl |= MD_F_MONO;
    return W(L, s, n, px, fl);
}
static int tm_a(int px, void *u) { return ASC((struct L *)u, px); }
static int tm_d(int px, void *u) { return DSC((struct L *)u, px); }
/* maqueta una formula en tx[]: devuelve 0 si salio; caja en coordenadas de tex */
static int math_box(struct L *L, const char *src, int n, int width, int *m0, int *mn, int *x0, int *x1, int *y0, int *y1, int *base)
{
    struct md_scr *S = L->S;
    struct tex_metrics tm = { tm_w, tm_a, tm_d, 0, L };
    struct tex_out to; to.ops = S->tx + S->ntx; to.nops = 0; to.cap = TX_OPS - S->ntx; to.pool = S->tp + S->ntp; to.npool = 0; to.poolcap = TX_POOL - S->ntp;
    to.height = 0; to.title[0] = 0; to.errors = 0; to.sections = to.equations = to.footnotes = 0;
    if (to.cap < 40 || to.poolcap < 256) { L->o->full = 1; return -1; }
    (void)tex_typeset(src, (uint32_t)n, width, L->px, &tm, &to);
    if (to.nops <= 0) return -1;
    *m0 = S->ntx; *mn = to.nops;
    for (int i = 0; i < to.nops; i++) if (to.ops[i].type == TOP_TEXT || to.ops[i].type == TOP_IMAGE) to.ops[i].s += S->ntp;
    S->ntx += to.nops; S->ntp += to.npool;
    int ax = 1 << 30, bx = -(1 << 30), ay = 1 << 30, by = -(1 << 30), bl = -1;
    for (int i = 0; i < to.nops; i++) {
        const struct tex_op *p = &to.ops[i]; int px0, px1, py0, py1;
        if (p->type == TOP_TEXT) {
            px0 = p->x; px1 = p->x + tm_w(S->tp + p->s, (int)p->n, p->px, p->style, L); py0 = p->y - tm_a(p->px, L); py1 = p->y + tm_d(p->px, L);
        } else if (p->type == TOP_LINE) {
            int a = p->x / 16, b = (p->x + p->w) / 16, c = p->y / 16, d = (p->y + p->h) / 16;
            px0 = a < b ? a : b; px1 = (a > b ? a : b) + 1; py0 = (c < d ? c : d) - 1; py1 = (c > d ? c : d) + 1;
        } else { px0 = p->x; px1 = p->x + p->w; py0 = p->y; py1 = p->y + p->h; }
        if (px0 < ax) ax = px0;
        if (px1 > bx) bx = px1;
        if (py0 < ay) ay = py0;
        if (py1 > by) by = py1;
    }
    /* linea base: la del primer texto al tamano base (la del "x" de x^2), o la mitad */
    for (int i = 0; i < to.nops; i++) if (to.ops[i].type == TOP_TEXT && to.ops[i].px == L->px) { bl = to.ops[i].y; break; }
    if (bl < 0) bl = (ay + by) / 2 + tm_a(L->px, L) / 3;
    *x0 = ax; *x1 = bx; *y0 = ay; *y1 = by; *base = bl;
    return 0;
}
__attribute__((noinline)) static void add_math(struct L *L, const char *s, int n, const struct sty *st)
{
    char src[1200]; int k = 0;
    src[k++] = '$';
    for (int i = 0; i < n && k < (int)sizeof src - 2; i++) src[k++] = s[i] == '\n' ? ' ' : s[i];
    src[k++] = '$';
    int m0, mn, x0, x1, y0, y1, bl;
    if (math_box(L, src, k, 4000, &m0, &mn, &x0, &x1, &y0, &y1, &bl) < 0) { add_text(L, s, n, st); return; }
    if (x1 - x0 > 3000) { add_text(L, s, n, st); return; }
    struct at *a = at_new(L, A_MATH, st); if (!a) return;
    a->m0 = m0; a->mn = mn; a->ox = (int16_t)(-x0 + 1); a->oy = -bl;
    a->w = x1 - x0 + 2; a->asc = bl - y0; a->desc = y1 - bl;
    L->o->maths++;
}
static void add_img(struct L *L, const char *src, int n, const char *alt, int an, const struct sty *st)
{
    struct at *a = at_new(L, A_IMG, st); if (!a) return;
    a->s = pool_put(L, src, (uint32_t)n); a->n = (uint32_t)n;
    int w = 320, h = 200, r = L->m->image ? L->m->image(L->o->pool + a->s, n, &w, &h, L->m->u) : -1;
    if (r != 0 || w <= 0 || h <= 0) { w = 320; h = r < 0 ? 64 : 180; }
    a->w = w; a->ox = 0; a->asc = h; a->desc = 0;
    (void)alt; (void)an; a->oy = r;
    L->o->images++;
}

/* busca el cierre de un delimitador de enfasis de longitud dl a partir de i */
static int busca_cierre(const char *s, int n, int i, char d, int dl)
{
    for (int j = i; j < n; j++) {
        if (s[j] == '\\') { j++; continue; }
        if (s[j] == '`') { int k = j + 1; while (k < n && s[k] != '`') k++; if (k < n) j = k; continue; }
        if (s[j] != d) continue;
        int r = j; while (r < n && s[r] == d) r++;
        int run = r - j;
        if (run >= dl && j > i && !is_sp(s[j - 1])) {
            if (d == '_' && r < n && is_alnum(s[r])) { j = r - 1; continue; }
            return j + (run - dl);           /* el cierre son los ultimos dl */
        }
        j = r - 1;
    }
    return -1;
}
static int html_attr(const char *s, int n, const char *name, int *a0, int *an)
{
    for (int i = 0; i + 1 < n; i++) {
        if (!pre(s + i, n - i, name)) continue;
        int j = i; while (s[j] && j < n && s[j] != '=') j++;
        j++; while (j < n && is_sp(s[j])) j++;
        if (j >= n) return 0;
        char q = s[j]; if (q == '"' || q == '\'') { j++; int k = j; while (k < n && s[k] != q) k++; *a0 = j; *an = k - j; return 1; }
        int k = j; while (k < n && !is_sp(s[k]) && s[k] != '>') k++;
        *a0 = j; *an = k - j; return 1;
    }
    return 0;
}
static int es_url_ini(const char *s, int n) { return pre(s, n, "https://") || pre(s, n, "http://") || pre(s, n, "www."); }

static void inl(struct L *L, const char *s, int n, struct sty st, int lvl);
/* etiqueta HTML en linea en s[i]: aplica y devuelve bytes consumidos (0 = no es etiqueta) */
static int html_tag(struct L *L, const char *s, int n, int i, struct sty *st, int lvl)
{
    int e = i + 1; while (e < n && s[e] != '>' && s[e] != '<' && s[e] != '\n') e++;
    if (e >= n || s[e] != '>') return 0;
    const char *t = s + i + 1; int tn = e - i - 1, close = 0;
    if (tn > 0 && t[0] == '/') { close = 1; t++; tn--; }
    if (tn <= 0 || !((t[0] >= 'a' && t[0] <= 'z') || (t[0] >= 'A' && t[0] <= 'Z'))) return 0;
    int nl = 0; while (nl < tn && is_alnum(t[nl])) nl++;
    static const char *const SIMPLE[] = { "b", "strong", "i", "em", "u", "ins", "s", "del", "strike", "code", "kbd", "tt", "sup", "sub", "mark", "a", 0 };
    int which = -1;
    for (int k = 0; SIMPLE[k]; k++) { int l = 0; while (SIMPLE[k][l]) l++; if (l == nl && pre(t, nl, SIMPLE[k])) { which = k; break; } }
    if (pre(t, nl, "br") && nl == 2) { add_br(L, st); return e - i + 1; }
    if (which < 0 || close) return e - i + 1;          /* etiqueta desconocida o cierre suelto: se quita */
    /* busca el cierre </tag> */
    int j = e + 1, depth = 1, cs = -1, ce = -1;
    while (j < n) {
        if (s[j] == '<') {
            int c = (j + 1 < n && s[j + 1] == '/');
            const char *u = s + j + 1 + c; int un = n - (j + 1 + c);
            if (pre(u, un, SIMPLE[which]) && (un <= nl || !is_alnum(u[nl]))) {
                if (c) { if (--depth == 0) { cs = j; ce = j; while (ce < n && s[ce] != '>') ce++; break; } }
                else depth++;
            }
        }
        j++;
    }
    if (cs < 0) return e - i + 1;
    struct sty in = *st;
    switch (which) {
    case 0: case 1: in.fl |= MD_F_BOLD; break;
    case 2: case 3: in.fl |= MD_F_ITALIC; break;
    case 4: case 5: in.deco |= D_UNDER; break;
    case 6: case 7: case 8: in.deco |= D_STRIKE; break;
    case 9: case 10: case 11: in.deco |= D_CODE; in.col = L->t->inl_fg; break;
    case 12: in.deco |= D_SUP; break;
    case 13: in.deco |= D_SUB; break;
    case 14: in.deco |= D_MARK; break;
    case 15: { int a0, an; if (html_attr(t, tn, "href", &a0, &an)) { uint32_t u = pool_put(L, t + a0, (uint32_t)an); in.link = (int16_t)zone_new(L, MDZ_LINK, u, (uint32_t)an); in.col = L->t->link; in.deco |= D_UNDER; L->o->links++; } break; }
    }
    if (in.deco & D_CODE) { add_text(L, s + e + 1, cs - e - 1, &in); }
    else inl(L, s + e + 1, cs - e - 1, in, lvl + 1);
    return ce - i + 1;
}
static void inl(struct L *L, const char *s, int n, struct sty st, int lvl)
{
    char buf[256]; int bn = 0;
#define FLUSH() do { if (bn) { add_text(L, buf, bn, &st); bn = 0; } } while (0)
#define PUTC(c) do { if (bn >= (int)sizeof buf - 4) FLUSH(); buf[bn++] = (char)(c); } while (0)
    if (lvl > 12) { add_text(L, s, n, &st); return; }
    int i = 0;
    while (i < n && !L->o->full) {
        char c = s[i];
        /* salto duro: \ al final de linea */
        if (c == '\\' && i + 1 < n && s[i + 1] == '\n') { FLUSH(); add_br(L, &st); i += 2; continue; }
        if (c == '\\' && i + 1 < n && (s[i + 1] == '(' || s[i + 1] == '[')) {       /* \( formula \) */
            char cl = s[i + 1] == '(' ? ')' : ']'; int j = i + 2;
            while (j + 1 < n && !(s[j] == '\\' && s[j + 1] == cl)) j++;
            if (j + 1 < n) { FLUSH(); add_math(L, s + i + 2, j - i - 2, &st); i = j + 2; continue; }
        }
        if (c == '\\' && i + 1 < n && !is_alnum(s[i + 1]) && s[i + 1] != ' ') { PUTC(s[i + 1]); i += 2; continue; }
        if (c == '\n') {
            /* dos espacios antes del salto = salto duro */
            int hard = bn >= 2 && buf[bn - 1] == ' ' && buf[bn - 2] == ' ';
            while (bn && buf[bn - 1] == ' ') bn--;
            FLUSH(); if (hard) add_br(L, &st); else PUTC(' ');
            i++; continue;
        }
        if (c == '`') {
            int r = i; while (r < n && s[r] == '`') r++;
            int tl = r - i, j = r;
            while (j < n) { if (s[j] == '`') { int q = j; while (q < n && s[q] == '`') q++; if (q - j == tl) break; j = q; } else j++; }
            if (j < n) {
                FLUSH(); int a0 = r, a1 = j; if (a1 - a0 >= 2 && s[a0] == ' ' && s[a1 - 1] == ' ') { a0++; a1--; }
                struct sty cs = st; cs.deco |= D_CODE; cs.col = L->t->inl_fg;
                /* el codigo en linea va en una sola pieza si es corto (no se parte por espacios) */
                if (a1 - a0 <= 24) { struct at *a = at_new(L, A_WORD, &cs); if (a) { a->s = pool_put(L, s + a0, (uint32_t)(a1 - a0)); a->n = (uint32_t)(a1 - a0); at_measure(L, a); } }
                else add_text(L, s + a0, a1 - a0, &cs);
                i = j + tl; continue;
            }
            while (i < r) { PUTC('`'); i++; }
            continue;
        }
        if (c == '$') {
            int dd = (i + 1 < n && s[i + 1] == '$');
            int a0 = i + 1 + dd, j = a0;
            if (a0 < n && !is_sp(s[a0])) {
                while (j < n && !(s[j] == '$' && s[j - 1] != '\\')) { if (s[j] == '\n' && !dd && j + 1 < n && s[j + 1] == '\n') break; j++; }
                int ok = j < n && s[j] == '$' && j > a0 && !is_sp(s[j - 1]);
                if (ok && dd && !(j + 1 < n && s[j + 1] == '$')) ok = 0;
                if (ok && !dd && j + 1 < n && is_dig(s[j + 1])) ok = 0;            /* "cuesta $5 y $10": no es formula */
                if (ok) { FLUSH(); add_math(L, s + a0, j - a0, &st); i = j + 1 + dd; continue; }
            }
            PUTC('$'); i++; continue;
        }
        if (c == '!' && i + 1 < n && s[i + 1] == '[') {
            int j = i + 2; while (j < n && s[j] != ']') j++;
            if (j + 1 < n && s[j + 1] == '(') {
                int k = j + 2; while (k < n && s[k] != ')') k++;
                if (k < n) {
                    int u0 = j + 2, u1 = k; while (u0 < u1 && is_sp(s[u0])) u0++;
                    int sp = u0; while (sp < u1 && !is_sp(s[sp])) sp++;
                    if (sp > u0 && s[u0] == '<' && s[sp - 1] == '>') { u0++; sp--; }
                    FLUSH(); add_img(L, s + u0, sp - u0, s + i + 2, j - i - 2, &st); i = k + 1; continue;
                }
            }
        }
        if (c == '[') {
            int j = i + 1, d = 1; while (j < n && d) { if (s[j] == '[') d++; else if (s[j] == ']') d--; if (d) j++; }
            if (j + 1 < n && s[j + 1] == '(') {
                int k = j + 2, pd = 1; while (k < n && pd) { if (s[k] == '(') pd++; else if (s[k] == ')') pd--; if (pd) k++; }
                if (k < n) {
                    int u0 = j + 2, u1 = k; while (u0 < u1 && is_sp(s[u0])) u0++;
                    int sp = u0; while (sp < u1 && !is_sp(s[sp])) sp++;
                    if (sp > u0 && s[u0] == '<' && s[sp - 1] == '>') { u0++; sp--; }
                    FLUSH();
                    struct sty ls = st; uint32_t u = pool_put(L, s + u0, (uint32_t)(sp - u0));
                    ls.link = (int16_t)zone_new(L, MDZ_LINK, u, (uint32_t)(sp - u0)); ls.col = L->t->link; ls.deco |= D_UNDER; L->o->links++;
                    inl(L, s + i + 1, j - i - 1, ls, lvl + 1);
                    i = k + 1; continue;
                }
            }
        }
        if (c == '<') {
            int j = i + 1; while (j < n && s[j] != '>' && !is_sp(s[j])) j++;
            if (j < n && s[j] == '>' && (es_url_ini(s + i + 1, j - i - 1) || pre(s + i + 1, j - i - 1, "mailto:"))) {
                FLUSH(); struct sty ls = st; uint32_t u = pool_put(L, s + i + 1, (uint32_t)(j - i - 1));
                ls.link = (int16_t)zone_new(L, MDZ_LINK, u, (uint32_t)(j - i - 1)); ls.col = L->t->link; ls.deco |= D_UNDER; L->o->links++;
                add_text(L, s + i + 1, j - i - 1, &ls); i = j + 1; continue;
            }
            FLUSH();
            int used = html_tag(L, s, n, i, &st, lvl);
            if (used > 0) { i += used; continue; }
            PUTC('<'); i++; continue;
        }
        if (c == '&') { char e[4]; int el = 0, u = entidad(s + i, n - i, e, &el); if (u) { for (int k = 0; k < el; k++) PUTC(e[k]); i += u; continue; } }
        if ((c == 'h' || c == 'w') && (i == 0 || !is_alnum(s[i - 1])) && es_url_ini(s + i, n - i) && st.link < 0) {
            int j = i; while (j < n && !is_sp(s[j]) && s[j] != '\n' && s[j] != '<' && s[j] != '>' && s[j] != '"') j++;
            while (j > i && (s[j - 1] == '.' || s[j - 1] == ',' || s[j - 1] == ';' || s[j - 1] == ':' || s[j - 1] == '!' || s[j - 1] == '?' || s[j - 1] == ')' || s[j - 1] == '*')) {
                if (s[j - 1] == ')') { int o = 0, cl = 0; for (int k = i; k < j; k++) { if (s[k] == '(') o++; if (s[k] == ')') cl++; } if (o >= cl) break; }
                j--;
            }
            if (j - i > 8) {
                FLUSH(); struct sty ls = st; uint32_t u = pool_put(L, s + i, (uint32_t)(j - i));
                ls.link = (int16_t)zone_new(L, MDZ_LINK, u, (uint32_t)(j - i)); ls.col = L->t->link; ls.deco |= D_UNDER; L->o->links++;
                add_text(L, s + i, j - i, &ls); i = j; continue;
            }
        }
        if (c == '*' || c == '_' || c == '~' || c == '=') {
            int r = i; while (r < n && s[r] == c) r++;
            int run = r - i;
            int left_ok = r < n && !is_sp(s[r]) && s[r] != '\n';
            if (c == '_' && i > 0 && is_alnum(s[i - 1])) left_ok = 0;
            if ((c == '~' || c == '=') && run != 2) left_ok = 0;
            if (c == '*' && i > 0 && is_dig(s[i - 1]) && r < n && is_dig(s[r])) left_ok = 0;     /* 2*3*4: multiplicacion */
            if (left_ok) {
                int dl = run > 3 ? 3 : run;
                int cl = busca_cierre(s, n, r, c, dl);
                if (cl < 0 && dl == 3) { dl = 2; cl = busca_cierre(s, n, i + 2, c, 2); r = i + 2; }
                if (cl < 0 && dl >= 2 && c != '~' && c != '=') { dl = 1; cl = busca_cierre(s, n, i + 1, c, 1); r = i + 1; }
                if (cl > r) {
                    FLUSH(); struct sty in = st;
                    if (c == '~') in.deco |= D_STRIKE;
                    else if (c == '=') in.deco |= D_MARK;
                    else { if (dl == 1 || dl == 3) in.fl |= MD_F_ITALIC; if (dl >= 2) in.fl |= MD_F_BOLD; }
                    for (int k = 0; k < run - dl; k++) PUTC(c);
                    FLUSH();
                    inl(L, s + r, cl - r, in, lvl + 1);
                    i = cl + dl; continue;
                }
            }
            for (int k = 0; k < run; k++) PUTC(c);
            i = r; continue;
        }
        PUTC(c); i++;
    }
    FLUSH();
#undef FLUSH
#undef PUTC
}

/* ---------------------------------------------------------------- reparto en lineas */
/* parte una palabra demasiado ancha para avail: devuelve los bytes que caben (al menos un caracter) */
static int cabe(struct L *L, const char *s, int n, int px, int fl, int avail)
{
    int lo = 1, hi = n, best = 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, m = u8back(s, mid); if (m <= 0) m = mid;
        if (W(L, s, m, px, fl) <= avail) { best = m; lo = mid + 1; } else hi = mid - 1;
    }
    best = u8back(s, best); if (best <= 0) { best = 1; while (best < n && ((uint8_t)s[best] & 0xC0) == 0x80) best++; }
    return best;
}
static int flow(struct L *L, int a0, int a1, int x, int y, int w, int align, int emit, int *maxw);
static int flow_copia(struct L *L, int a0, int n, int x, int y, int w, int align, int emit, int *maxw)
{
    struct md_scr *S = L->S; int base = S->na;
    if (base + n + 64 > AT_MAX) { if (maxw) *maxw = 0; L->o->full = 1; return 0; }
    for (int i = 0; i < n; i++) S->a[base + i] = S->a[a0 + i];
    S->na = base + n;
    int h = flow(L, base, base + n, x, y, w, align, emit, maxw);
    S->na = base;
    return h;
}
static void emit_at(struct L *L, const struct at *a0, int x, int base, int lh_top, int lh)
{
    const struct md_theme *t = L->t;
    struct at ac = *a0, *a = &ac;
    a->link = (int16_t)link_zone(L, a0->link, lh_top);
    if (a->k == A_MATH) {
        const struct md_scr *S = L->S;
        int dx = x + a->ox, dy = base + a->oy;
        for (int i = a->m0; i < a->m0 + a->mn; i++) {
            const struct tex_op *p = &S->tx[i];
            uint32_t col = p->rgb == TEX_ERR ? t->err : p->rgb == TEX_MUTED ? t->muted : p->rgb == TEX_LINK ? t->link : a->col;
            if (p->type == TOP_TEXT) {
                int fl = ((p->style & TS_BOLD) ? MD_F_BOLD : 0) | ((p->style & TS_ITALIC) ? MD_F_ITALIC : 0) | ((p->style & TS_MONO) ? MD_F_MONO : 0);
                uint32_t at = pool_put(L, S->tp + p->s, p->n); if (L->o->full) return;
                op_text(L, dx + p->x, dy + p->y, at, p->n, p->px, fl, col, a->link);
            } else if (p->type == TOP_RECT) {
                if (p->style == 1) continue;
                op_rect(L, dx + p->x, dy + p->y, p->w, p->h, 0, col, MD_RECT);
            } else if (p->type == TOP_LINE) {
                struct md_op *q = op_new(L, MD_LINE); if (!q) return;
                q->x = p->x + dx * 16; q->y = p->y + dy * 16; q->w = p->w; q->h = p->h; q->px = p->px > 0 ? p->px : 16; q->rgb = col;
            }
        }
        zone_grow(L, a->link, x, base - a->asc, a->w, a->asc + a->desc);
        return;
    }
    if (a->k == A_IMG) {
        struct md_op *p = op_new(L, MD_IMG); if (!p) return;
        p->x = x; p->y = lh_top; p->w = a->w; p->h = a->asc; p->s = a->s; p->n = a->n; p->rgb = (uint32_t)a->oy; p->zone = a->link;
        int z = zone_new(L, MDZ_IMG, a->s, a->n); zone_grow(L, z, x, lh_top, a->w, a->asc);
        if (a->link >= 0) zone_grow(L, a->link, x, lh_top, a->w, a->asc);
        if (x + a->w > L->o->width) L->o->width = x + a->w;
        return;
    }
    if (a->k != A_WORD && a->k != A_SPACE) return;
    struct sty st = { a->fl, a->deco, a->dpx, a->col, a->link };
    int px = sty_px(L, &st), fl = sty_fl(&st), y = base;
    if (a->deco & D_SUP) y -= (L->px + a->dpx) * 2 / 5;
    if (a->deco & D_SUB) y += (L->px + a->dpx) / 4;
    (void)lh;
    if (a->deco & D_MARK) op_rect(L, x - 1, base - ASC(L, px) - 1, a->w + 2, ASC(L, px) + DSC(L, px) + 2, 3, t->mark_bg, MD_RECT);
    if (a->deco & D_CODE) {
        if (a->k == A_WORD) op_rect(L, x, base - ASC(L, px) - 3, a->w, ASC(L, px) + DSC(L, px) + 5, 5, t->inl_bg, MD_RECT);
        if (a->k == A_WORD) op_text(L, x + 4, y, a->s, a->n, px, fl, a->col, a->link);
    } else if (a->k == A_WORD) op_text(L, x, y, a->s, a->n, px, fl, a->col, a->link);
    if (a->deco & D_STRIKE) op_line(L, x, base - ASC(L, px) * 3 / 8, x + a->w, base - ASC(L, px) * 3 / 8, 18, a->col);
    if ((a->deco & D_UNDER) && !(a->deco & D_CODE)) op_line(L, x, base + 2, x + a->w, base + 2, a->link >= 0 ? 14 : 16, a->link >= 0 ? (0x80000000u | a->col) : a->col);
    zone_grow(L, a->link, x, base - ASC(L, px) - 2, a->w, ASC(L, px) + DSC(L, px) + 4);
}
/* reparte las piezas [a0, a1) en lineas de ancho w a partir de (x, y). align 0 izq, 1 centro, 2 der. emit = 0 solo mide.
 * Devuelve el alto; *maxw = la linea mas ancha. */
static int flow(struct L *L, int a0, int a1, int x, int y, int w, int align, int emit, int *maxw)
{
    struct md_scr *S = L->S;
    int lh0 = L->px * 155 / 100, top = y, i = a0, mw = 0;
    if (w < 20) w = 20;
    while (i < a1) {
        /* salta espacios al principio de linea */
        while (i < a1 && S->a[i].k == A_SPACE) i++;
        if (i >= a1) break;
        if (S->a[i].k == A_BR) { y += lh0; i++; continue; }
        if (S->a[i].k == A_IMG) {
            struct at *a = &S->a[i];
            int iw = a->w, ih = a->asc;
            if (iw > w) { ih = ih * w / iw; iw = w; }
            if (ih > 460) { iw = iw * 460 / ih; ih = 460; }
            if (iw < 1) iw = 1;
            a->w = iw; a->asc = ih;
            if (emit) emit_at(L, a, x, 0, y + 4, ih);
            if (iw > mw) mw = iw;
            y += ih + 8; i++;
            continue;
        }
        int j = i, lw = 0, asc = ASC(L, L->px), dsc = DSC(L, L->px);
        while (j < a1) {
            struct at *a = &S->a[j];
            if (a->k == A_BR || a->k == A_IMG) break;
            if (lw + a->w > w && a->k == A_SPACE) break;
            if (lw + a->w > w) {
                if (lw > 0) break;
                /* palabra sola mas ancha que la linea: se parte */
                if (a->k == A_WORD) {
                    struct sty st = { a->fl, a->deco, a->dpx, a->col, a->link };
                    int px = sty_px(L, &st), fl = sty_fl(&st), pad = (a->deco & D_CODE) ? 8 : 0;
                    int k = cabe(L, L->o->pool + a->s, (int)a->n, px, fl, w - pad);
                    if (k < (int)a->n && S->na < AT_MAX) {
                        for (int q = S->na; q > j + 1; q--) S->a[q] = S->a[q - 1];
                        S->na++; a1++;
                        S->a[j + 1] = *a; S->a[j + 1].s = a->s + (uint32_t)k; S->a[j + 1].n = a->n - (uint32_t)k; at_measure(L, &S->a[j + 1]);
                        a->n = (uint32_t)k; at_measure(L, a);
                    }
                }
                lw += a->w; if (a->asc > asc) asc = a->asc; if (a->desc > dsc) dsc = a->desc; j++;
                break;
            }
            lw += a->w; if (a->asc > asc) asc = a->asc; if (a->desc > dsc) dsc = a->desc; j++;
        }
        int e = j; while (e > i && S->a[e - 1].k == A_SPACE) { lw -= S->a[e - 1].w; e--; }
        int lh = asc + dsc + (L->px * 55 / 100); if (lh < lh0) lh = lh0;
        int base = y + (lh - asc - dsc) / 2 + asc;
        int ox = align == 1 ? (w - lw) / 2 : align == 2 ? w - lw : 0; if (ox < 0) ox = 0;
        if (emit) { int cx = x + ox; for (int k = i; k < e; k++) { emit_at(L, &S->a[k], cx, base, y, lh); cx += S->a[k].w; } }
        if (lw > mw) mw = lw;
        y += lh; i = j;
        if (i < a1 && S->a[i].k == A_BR) i++;
    }
    if (maxw) *maxw = mw;
    return y - top;
}

/* ---------------------------------------------------------------- resaltado de codigo */
static int kw_in(const char *w, int n, const char *const *list)
{
    for (int i = 0; list[i]; i++) { int k = 0; while (k < n && list[i][k] == w[k]) k++; if (k == n && !list[i][k]) return 1; }
    return 0;
}
enum { LG_C, LG_PY, LG_JS, LG_SH, LG_RS, LG_GO, LG_JAVA, LG_SQL, LG_JSON, LG_HTML, LG_CSS, LG_TXT };
static int lang_id(const char *l, int n)
{
    static const struct { const char *n; int id; } T[] = {
        { "c", LG_C }, { "h", LG_C }, { "cpp", LG_C }, { "c++", LG_C }, { "cc", LG_C }, { "cs", LG_JAVA }, { "csharp", LG_JAVA }, { "objc", LG_C },
        { "python", LG_PY }, { "py", LG_PY }, { "python3", LG_PY }, { "js", LG_JS }, { "javascript", LG_JS }, { "ts", LG_JS }, { "typescript", LG_JS },
        { "jsx", LG_JS }, { "tsx", LG_JS }, { "sh", LG_SH }, { "bash", LG_SH }, { "shell", LG_SH }, { "zsh", LG_SH }, { "console", LG_SH }, { "rust", LG_RS }, { "rs", LG_RS },
        { "go", LG_GO }, { "golang", LG_GO }, { "java", LG_JAVA }, { "kotlin", LG_JAVA }, { "swift", LG_JAVA }, { "sql", LG_SQL }, { "json", LG_JSON },
        { "yaml", LG_SH }, { "yml", LG_SH }, { "toml", LG_SH }, { "ini", LG_SH }, { "html", LG_HTML }, { "xml", LG_HTML }, { "svg", LG_HTML }, { "css", LG_CSS },
        { "scss", LG_CSS }, { "asm", LG_SH }, { "nasm", LG_SH }, { "makefile", LG_SH }, { "make", LG_SH }, { "lua", LG_PY }, { "ruby", LG_PY }, { "rb", LG_PY },
        { "php", LG_JS }, { "dockerfile", LG_SH }, { "latex", LG_TXT }, { "tex", LG_TXT }, { 0, 0 } };
    for (int i = 0; T[i].n; i++) { int k = 0; while (k < n && T[i].n[k] == lower(l[k])) k++; if (k == n && !T[i].n[k]) return T[i].id; }
    return n ? LG_TXT : LG_C;
}
static const char *const KW_C[] = { "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "return", "goto", "struct", "union", "enum",
    "typedef", "static", "const", "volatile", "extern", "sizeof", "inline", "register", "class", "public", "private", "protected", "template", "typename", "namespace",
    "using", "new", "delete", "virtual", "override", "true", "false", "nullptr", "NULL", "this", "try", "catch", "throw", "auto", "constexpr", "#include", "#define",
    "#if", "#ifdef", "#ifndef", "#endif", "#else", "#pragma", "#elif", 0 };
static const char *const TY_C[] = { "int", "char", "void", "float", "double", "long", "short", "unsigned", "signed", "bool", "uint8_t", "uint16_t", "uint32_t",
    "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t", "size_t", "string", "std", "vector", "String", "var", "let", "i32", "u8", "u32", "u64", "i64", "usize",
    "f32", "f64", "str", "Self", "Vec", "Option", "Result", "Box", "byte", "rune", "error", "boolean", "Integer", "Object", 0 };
static const char *const KW_PY[] = { "def", "class", "if", "elif", "else", "for", "while", "in", "not", "and", "or", "is", "return", "import", "from", "as", "with",
    "try", "except", "finally", "raise", "pass", "break", "continue", "lambda", "yield", "global", "nonlocal", "assert", "del", "async", "await", "None", "True",
    "False", "self", "print", "end", "then", "local", "function", "elsif", "unless", "do", "require", "nil", "puts", 0 };
static const char *const KW_JS[] = { "function", "const", "let", "var", "if", "else", "for", "while", "do", "return", "class", "extends", "new", "this", "import",
    "export", "from", "default", "async", "await", "try", "catch", "finally", "throw", "typeof", "instanceof", "in", "of", "switch", "case", "break", "continue",
    "true", "false", "null", "undefined", "interface", "type", "implements", "public", "private", "readonly", "enum", "static", "yield", "echo", 0 };
static const char *const KW_SH[] = { "if", "then", "else", "elif", "fi", "for", "in", "do", "done", "while", "until", "case", "esac", "function", "return", "export",
    "local", "echo", "cd", "sudo", "exit", "source", "alias", "set", "unset", "read", "true", "false", "make", "git", "python3", "pip", "apt", "brew", "ls", "cat",
    "grep", "sed", "awk", "rm", "cp", "mv", "mkdir", "chmod", "curl", "FROM", "RUN", "COPY", "CMD", "WORKDIR", "ENV", "mov", "push", "pop", "call", "ret", "jmp", 0 };
static const char *const KW_RS[] = { "fn", "let", "mut", "if", "else", "match", "for", "while", "loop", "in", "return", "struct", "enum", "impl", "trait", "pub",
    "use", "mod", "crate", "self", "super", "where", "as", "ref", "move", "async", "await", "dyn", "const", "static", "unsafe", "true", "false", "Some", "None",
    "Ok", "Err", "func", "package", "import", "type", "interface", "map", "chan", "go", "defer", "range", "var", "nil", "select", "default", "case", "switch", 0 };
static const char *const KW_JAVA[] = { "class", "public", "private", "protected", "static", "final", "void", "new", "return", "if", "else", "for", "while", "do",
    "switch", "case", "break", "continue", "try", "catch", "finally", "throw", "throws", "import", "package", "extends", "implements", "interface", "this", "super",
    "true", "false", "null", "abstract", "synchronized", "fun", "val", "var", "override", "using", "namespace", "func", "let", "struct", "guard", 0 };
static const char *const KW_SQL[] = { "select", "from", "where", "and", "or", "not", "insert", "into", "values", "update", "set", "delete", "create", "table",
    "drop", "alter", "index", "join", "left", "right", "inner", "outer", "on", "group", "by", "order", "having", "limit", "as", "distinct", "null", "primary",
    "key", "foreign", "references", "union", "all", "case", "when", "then", "else", "end", "like", "in", "is", "count", "sum", "avg", "min", "max", 0 };
int md_resalta(const char *lang, int nlang, const char *s, int n, uint8_t *cls, int estado)
{
    int lg = lang_id(lang, nlang);
    const char *const *kw = lg == LG_PY ? KW_PY : lg == LG_JS ? KW_JS : lg == LG_SH ? KW_SH : (lg == LG_RS || lg == LG_GO) ? KW_RS : lg == LG_JAVA ? KW_JAVA : lg == LG_SQL ? KW_SQL : KW_C;
    int hash_com = lg == LG_PY || lg == LG_SH, slash_com = lg == LG_C || lg == LG_JS || lg == LG_RS || lg == LG_GO || lg == LG_JAVA || lg == LG_CSS;
    int dash_com = lg == LG_SQL;
    for (int i = 0; i < n; i++) cls[i] = MDK_TXT;
    if (lg == LG_TXT) return 0;
    int i = 0;
    if (estado == 1) {                       /* dentro de un comentario de bloque */
        while (i < n && !(s[i] == '*' && i + 1 < n && s[i + 1] == '/')) cls[i++] = MDK_COM;
        if (i >= n) return 1;
        cls[i] = cls[i + 1] = MDK_COM; i += 2;
    }
    if (estado == 2) {                       /* comentario HTML */
        while (i < n && !(s[i] == '-' && i + 2 < n && s[i + 1] == '-' && s[i + 2] == '>')) cls[i++] = MDK_COM;
        if (i >= n) return 2;
        cls[i] = cls[i + 1] = cls[i + 2] = MDK_COM; i += 3;
    }
    if (lg == LG_HTML) {
        while (i < n) {
            if (s[i] == '<' && i + 3 < n && s[i + 1] == '!' && s[i + 2] == '-' && s[i + 3] == '-') {
                int j = i; while (j < n && !(s[j] == '-' && j + 2 < n && s[j + 1] == '-' && s[j + 2] == '>')) cls[j++] = MDK_COM;
                if (j >= n) return 2;
                cls[j] = cls[j + 1] = cls[j + 2] = MDK_COM; i = j + 3; continue;
            }
            if (s[i] == '<') {
                cls[i++] = MDK_KW; if (i < n && s[i] == '/') cls[i++] = MDK_KW;
                while (i < n && is_alnum(s[i])) cls[i++] = MDK_KW;
                while (i < n && s[i] != '>') {
                    if (s[i] == '"' || s[i] == '\'') { char q = s[i]; cls[i++] = MDK_STR; while (i < n && s[i] != q) cls[i++] = MDK_STR; if (i < n) cls[i++] = MDK_STR; continue; }
                    if (is_alnum(s[i])) { while (i < n && (is_alnum(s[i]) || s[i] == '-')) cls[i++] = MDK_FN; continue; }
                    i++;
                }
                if (i < n) cls[i++] = MDK_KW;
                continue;
            }
            i++;
        }
        return 0;
    }
    while (i < n) {
        char c = s[i];
        if ((hash_com && c == '#' && !(lg == LG_SH && i > 0 && s[i - 1] == '$')) || (slash_com && c == '/' && i + 1 < n && s[i + 1] == '/') || (dash_com && c == '-' && i + 1 < n && s[i + 1] == '-')) {
            if (!(lg == LG_C && c == '#')) { while (i < n) cls[i++] = MDK_COM; break; }
        }
        if (slash_com && c == '/' && i + 1 < n && s[i + 1] == '*') {
            int j = i; while (j < n && !(s[j] == '*' && j + 1 < n && s[j + 1] == '/' && j > i + 1)) cls[j++] = MDK_COM;
            if (j >= n) return 1;
            cls[j] = cls[j + 1] = MDK_COM; i = j + 2; continue;
        }
        if (c == '"' || c == '\'' || (c == '`' && lg == LG_JS)) {
            if (c == '\'' && (lg == LG_RS) && i + 2 < n && s[i + 2] != '\'') { i++; continue; }   /* 'a de Rust */
            int j = i + 1; cls[i] = MDK_STR;
            while (j < n && s[j] != c) { if (s[j] == '\\' && j + 1 < n) { cls[j++] = MDK_STR; } cls[j++] = MDK_STR; }
            if (j < n) cls[j++] = MDK_STR;
            i = j; continue;
        }
        if (is_dig(c) && (i == 0 || !is_alnum(s[i - 1]))) {
            int j = i; while (j < n && (is_alnum(s[j]) || s[j] == '.' || s[j] == '_')) cls[j++] = MDK_NUM;
            i = j; continue;
        }
        if (is_alnum(c) || c == '_' || (c == '#' && lg == LG_C) || (c == '@')) {
            int j = i + 1; while (j < n && (is_alnum(s[j]) || s[j] == '_')) j++;
            int k = MDK_TXT;
            if (lg == LG_JSON) k = MDK_TXT;
            else if (kw_in(s + i, j - i, kw)) k = MDK_KW;
            else if (lg == LG_SQL) { char t[24]; int m = j - i < 23 ? j - i : 23; for (int q = 0; q < m; q++) t[q] = (char)lower(s[i + q]); if (kw_in(t, m, KW_SQL)) k = MDK_KW; }
            else if (kw_in(s + i, j - i, TY_C)) k = MDK_TYP;
            else if (c == '@') k = MDK_FN;
            else { int q = j; while (q < n && s[q] == ' ') q++; if (q < n && s[q] == '(') k = MDK_FN; else if (c >= 'A' && c <= 'Z' && lg != LG_SH) k = MDK_TYP; }
            if (lg == LG_JSON && (kw_in(s + i, j - i, (const char *const[]){ "true", "false", "null", 0 }))) k = MDK_KW;
            for (int q = i; q < j; q++) cls[q] = (uint8_t)k;
            i = j; continue;
        }
        i++;
    }
    return 0;
}

/* ---------------------------------------------------------------- bloques */
struct ln { const char *s; int n; };
static int indent_of(const char *s, int n) { int k = 0, i = 0; while (i < n && is_sp(s[i])) { k += s[i] == '\t' ? 4 : 1; i++; } return k; }
static int blank(const char *s, int n) { for (int i = 0; i < n; i++) if (!is_sp(s[i]) && s[i] != '\r') return 0; return 1; }
static const char *skip_ind(const char *s, int n, int k, int *rn)       /* quita hasta k columnas de sangria */
{
    int i = 0, c = 0; while (i < n && c < k && is_sp(s[i])) { c += s[i] == '\t' ? 4 : 1; i++; }
    *rn = n - i; return s + i;
}
static int es_hr(const char *s, int n)
{
    int i = 0; while (i < n && is_sp(s[i])) i++;
    if (i >= n || i > 3) return 0;
    char c = s[i]; if (c != '-' && c != '*' && c != '_') return 0;
    int k = 0; for (; i < n; i++) { if (s[i] == c) k++; else if (!is_sp(s[i]) && s[i] != '\r') return 0; }
    return k >= 3;
}
static int es_fence(const char *s, int n, char *fc, int *fl)
{
    int i = 0; while (i < n && is_sp(s[i]) && i < 4) i++;
    if (i + 3 > n) return 0;
    char c = s[i]; if (c != '`' && c != '~') return 0;
    int k = i; while (k < n && s[k] == c) k++;
    if (k - i < 3) return 0;
    *fc = c; *fl = k - i; return 1;
}
static int es_head(const char *s, int n, int *lvl)
{
    int i = 0; while (i < n && is_sp(s[i]) && i < 4) i++;
    int k = 0; while (i + k < n && s[i + k] == '#') k++;
    if (k < 1 || k > 6 || (i + k < n && !is_sp(s[i + k]))) return 0;
    *lvl = k; return 1;
}
static int es_item(const char *s, int n, int *ind, int *ord, int *num, int *cont)  /* lista: sangria, ordenada, numero, columna del contenido */
{
    int i = 0; while (i < n && is_sp(s[i])) i++;
    if (i >= n) return 0;
    *ind = indent_of(s, i);
    if ((s[i] == '-' || s[i] == '*' || s[i] == '+') && (i + 1 >= n || is_sp(s[i + 1]))) {
        if (es_hr(s, n)) return 0;
        *ord = 0; *num = 0; int j = i + 1; while (j < n && is_sp(s[j]) && j < i + 5) j++;
        *cont = indent_of(s, i) + (j - i); return 1;
    }
    int j = i, v = 0; while (j < n && is_dig(s[j]) && j - i < 9) { v = v * 10 + (s[j] - '0'); j++; }
    if (j > i && j < n && (s[j] == '.' || s[j] == ')') && (j + 1 >= n || is_sp(s[j + 1]))) {
        *ord = 1; *num = v; int k = j + 1; while (k < n && is_sp(s[k]) && k < j + 5) k++;
        *cont = indent_of(s, i) + (k - i); return 1;
    }
    return 0;
}
static int es_tabla_sep(const char *s, int n)
{
    int i = 0, cols = 0, dash = 0; while (i < n && is_sp(s[i])) i++;
    if (i < n && s[i] == '|') i++;
    for (; i < n; i++) {
        char c = s[i];
        if (c == '-') dash++;
        else if (c == '|') { if (!dash) return 0; cols++; dash = 0; }
        else if (c != ':' && !is_sp(c) && c != '\r') return 0;
    }
    if (dash) cols++;
    return cols;
}
static int tiene_barra(const char *s, int n) { for (int i = 0; i < n; i++) if (s[i] == '|' && (i == 0 || s[i - 1] != '\\')) return 1; return 0; }
static int empieza_bloque(const char *s, int n)
{
    int lv, a, b, c, d, fl; char fc;
    int q = 0; while (q < n && is_sp(s[q])) q++;
    return es_head(s, n, &lv) || es_fence(s, n, &fc, &fl) || es_hr(s, n) || es_item(s, n, &a, &b, &c, &d) || (q < n && s[q] == '>') ||
           (n >= 2 && s[0] == '$' && s[1] == '$') || (n >= 2 && s[0] == '\\' && s[1] == '[');
}
/* arena: copia de lineas desmontadas */
static char *ar_take(struct L *L, uint32_t n) { struct md_scr *S = L->S; S->nar = (S->nar + 7u) & ~7u; if (S->nar + n > AR_MAX) { L->o->full = 1; return 0; } char *p = S->ar + S->nar; S->nar += n; return p; }

static int blocks(struct L *L, const char *src, int len, int x, int y, int w);
static uint32_t col_mix(uint32_t a, uint32_t b, int t)                 /* t/256 de b sobre a */
{
    uint32_t r = (((a >> 16) & 255) * (uint32_t)(256 - t) + ((b >> 16) & 255) * (uint32_t)t) >> 8;
    uint32_t g = (((a >> 8) & 255) * (uint32_t)(256 - t) + ((b >> 8) & 255) * (uint32_t)t) >> 8;
    uint32_t c = ((a & 255) * (uint32_t)(256 - t) + (b & 255) * (uint32_t)t) >> 8;
    return r << 16 | g << 8 | c;
}
static int para(struct L *L, const char *s, int n, int x, int y, int w, struct sty st, int align)
{
    struct md_scr *S = L->S; int a0 = S->na;
    if (a0 == 0) { S->ntx = 0; S->ntp = 0; }
    inl(L, s, n, st, 0);
    int mw, h = flow(L, a0, S->na, x, y, w, align, 1, &mw);
    S->na = a0;
    return h;
}
__attribute__((noinline)) static int code_block(struct L *L, const char *lang, int nl, const char *code, int n, int x, int y, int w)
{
    const struct md_theme *t = L->t; struct md_out *o = L->o;
    int px = L->px - 1, fl = MD_F_MONO, lh = px * 160 / 100, hd = 30, pad = 14;
    if (n > 0 && code[n - 1] == '\n') n--;
    /* alto: lineas partidas al ancho */
    int cw = W(L, "M", 1, px, fl); if (cw < 1) cw = 1;
    int cols = (w - 2 * pad) / cw; if (cols < 8) cols = 8;
    int lines = 0;
    for (int i = 0; i <= n; ) {
        int j = i; while (j < n && code[j] != '\n') j++;
        int chars = 0; for (int k = i; k < j; k++) if (((uint8_t)code[k] & 0xC0) != 0x80) chars += code[k] == '\t' ? 4 : 1;
        lines += chars ? (chars + cols - 1) / cols : 1;
        i = j + 1; if (j >= n) break;
    }
    int h = hd + pad + lines * lh + pad - 4;
    op_rect(L, x, y, w, h, 10, t->code_bg, MD_RECT);
    op_rect(L, x, y, w, hd, 10, t->code_hd, MD_RECT);
    op_rect(L, x, y + hd - 10, w, 10, 0, t->code_hd, MD_RECT);
    op_line(L, x, y + hd, x + w, y + hd, 16, t->code_bd);
    op_rect(L, x, y, w, h, 10, t->code_bd, MD_STROKE);
    {
        char lab[32]; int k = 0;
        for (int i = 0; i < nl && k < 24; i++) lab[k++] = (char)lower(lang[i]);
        if (!k) { const char *d = "c\303\263digo"; while (*d) lab[k++] = *d++; }
        lab[k] = 0;
        op_text_str(L, x + 14, y + hd / 2 + ASC(L, 12) / 2 - 1, lab, 12, MD_F_BOLD, t->muted);
        uint32_t cs = pool_put(L, code, (uint32_t)n);
        int z = zone_new(L, MDZ_COPY, cs, (uint32_t)n);
        int bw = W(L, "Copiar", 6, 12, 0) + 30;
        zone_grow(L, z, x + w - bw - 6, y + 4, bw, hd - 8);
        /* icono de copiar: dos rectangulos */
        int ix = x + w - bw + 2, iy = y + hd / 2 - 6;
        op_rect(L, ix + 3, iy - 1, 9, 11, 2, t->muted, MD_STROKE);
        op_rect(L, ix, iy + 2, 9, 11, 2, t->code_hd, MD_RECT);
        op_rect(L, ix, iy + 2, 9, 11, 2, t->muted, MD_STROKE);
        struct md_op *p = 0;
        uint32_t at = pool_put(L, "Copiar", 6);
        if (!o->full) { op_text(L, ix + 16, y + hd / 2 + ASC(L, 12) / 2 - 1, at, 6, 12, 0, t->muted, z); p = &o->ops[o->nops - 1]; }
        (void)p;
    }
    uint32_t P[7] = { t->code_fg, t->kw, t->str, t->num, t->com, t->fn, t->typ };
    int cy = y + hd + pad + ASC(L, px), estado = 0, cx0 = x + pad;
    for (int i = 0; i <= n; ) {
        int j = i; while (j < n && code[j] != '\n') j++;
        int ln = j - i; if (ln > (int)sizeof L->S->cls) ln = (int)sizeof L->S->cls;
        estado = md_resalta(lang, nl, code + i, ln, L->S->cls, estado);
        /* trozos del mismo color, partidos por columnas */
        int col = 0, k = i;
        while (k < i + ln) {
            int c0 = L->S->cls[k - i], e = k;
            while (e < i + ln && L->S->cls[e - i] == c0 && code[e] != '\t') e++;
            if (e == k) { col += 4; k++; if (col >= cols) { col -= cols; cy += lh; } continue; }    /* tabulador */
            /* caracteres del trozo */
            int q = k;
            while (q < e) {
                int room = cols - col, b = q, ch = 0;
                while (b < e && ch < room) { b++; while (b < e && ((uint8_t)code[b] & 0xC0) == 0x80) b++; ch++; }
                uint32_t at = pool_put(L, code + q, (uint32_t)(b - q)); if (o->full) return h;
                op_text(L, cx0 + col * cw, cy, at, (uint32_t)(b - q), px, fl, P[c0 < 7 ? c0 : 0], -1);
                col += ch; q = b;
                if (col >= cols && q < e) { col = 0; cy += lh; }
            }
            k = e;
        }
        cy += lh;
        i = j + 1; if (j >= n) break;
    }
    o->codes++;
    if (x + w > o->width) o->width = x + w;
    return h;
}
__attribute__((noinline)) static int math_block(struct L *L, const char *src, int n, int x, int y, int w)
{
    struct md_scr *S = L->S; int m0, mn, x0, x1, y0, y1, bl;
    if (S->na == 0) { S->ntx = 0; S->ntp = 0; }
    if (math_box(L, src, n, w, &m0, &mn, &x0, &x1, &y0, &y1, &bl) < 0) return 0;
    int dy = y + 6 - y0, dx = x;
    if (x1 - x0 > w) dx = x - x0;                      /* no cabe: a la izquierda */
    for (int i = m0; i < m0 + mn; i++) {
        const struct tex_op *p = &S->tx[i];
        uint32_t col = p->rgb == TEX_ERR ? L->t->err : p->rgb == TEX_MUTED ? L->t->muted : L->t->fg;
        if (p->type == TOP_TEXT) {
            int fl = ((p->style & TS_BOLD) ? MD_F_BOLD : 0) | ((p->style & TS_ITALIC) ? MD_F_ITALIC : 0) | ((p->style & TS_MONO) ? MD_F_MONO : 0);
            uint32_t at = pool_put(L, S->tp + p->s, p->n); if (L->o->full) break;
            op_text(L, dx + p->x, dy + p->y, at, p->n, p->px, fl, col, -1);
        } else if (p->type == TOP_RECT) { if (p->style != 1) op_rect(L, dx + p->x, dy + p->y, p->w, p->h, 0, col, MD_RECT); }
        else if (p->type == TOP_LINE) {
            struct md_op *q = op_new(L, MD_LINE); if (!q) break;
            q->x = p->x + dx * 16; q->y = p->y + dy * 16; q->w = p->w; q->h = p->h; q->px = p->px > 0 ? p->px : 16; q->rgb = col;
        }
    }
    L->o->maths++;
    return y1 - y0 + 12;
}
/* celdas de una fila: [a,b) de cada celda */
static int celdas(const char *s, int n, int *a, int *b, int max)
{
    int i = 0; while (i < n && is_sp(s[i])) i++;
    if (i < n && s[i] == '|') i++;
    int e = n; while (e > i && (is_sp(s[e - 1]) || s[e - 1] == '\r')) e--;
    if (e > i && s[e - 1] == '|' && (e < 2 || s[e - 2] != '\\')) e--;
    int k = 0, c0 = i, code = 0;
    for (int j = i; j <= e && k < max; j++) {
        if (j < e && s[j] == '`') code ^= 1;
        if (j == e || (s[j] == '|' && !code && (j == 0 || s[j - 1] != '\\'))) {
            int x0 = c0, x1 = j; while (x0 < x1 && is_sp(s[x0])) x0++; while (x1 > x0 && is_sp(s[x1 - 1])) x1--;
            a[k] = x0; b[k] = x1; k++; c0 = j + 1;
        }
    }
    return k;
}
__attribute__((noinline)) static int table_block(struct L *L, const struct ln *ls, int nl, int x, int y, int w)
{
    const struct md_theme *t = L->t; struct md_scr *S = L->S;
    int ca[32], cb[32], al[32], nc = 0;
    nc = celdas(ls[0].s, ls[0].n, ca, cb, 32);
    { int sa[32], sb[32]; int ns = celdas(ls[1].s, ls[1].n, sa, sb, 32);
      for (int c = 0; c < 32; c++) al[c] = 0;
      for (int c = 0; c < ns && c < 32; c++) { int l = sb[c] > sa[c] && ls[1].s[sa[c]] == ':', r = sb[c] > sa[c] && ls[1].s[sb[c] - 1] == ':'; al[c] = l && r ? 1 : r ? 2 : 0; }
      if (ns > nc) nc = ns; }
    if (nc < 1) return 0;
    int rows = nl - 1; if (rows > CEL_MAX / nc) rows = CEL_MAX / nc;     /* fila 0 = cabecera; las de datos empiezan en ls[2] */
    int pad = 12, natw[32], minw[32];
    for (int c = 0; c < nc; c++) { natw[c] = 30; minw[c] = 30; }
    /* piezas de cada celda (se miden una vez) */
    int base0 = S->na;
    if (base0 == 0) { S->ntx = 0; S->ntp = 0; }
    for (int r = 0; r < rows; r++) {
        const struct ln *row = r == 0 ? &ls[0] : &ls[r + 1];
        int a[32], b[32], k = celdas(row->s, row->n, a, b, 32);
        for (int c = 0; c < nc; c++) {
            int idx = r * nc + c;
            S->cel_a[idx] = S->na;
            struct sty st = { (uint8_t)(r == 0 ? MD_F_BOLD : 0), 0, 0, r == 0 ? t->head : t->fg, -1 };
            if (c < k) inl(L, row->s + a[c], b[c] - a[c], st, 0);
            S->cel_n[idx] = S->na - S->cel_a[idx];
            int tw = 0, lw = 0;
            for (int q = S->cel_a[idx]; q < S->na; q++) { tw += S->a[q].w; if (S->a[q].k != A_SPACE && S->a[q].w > lw) lw = S->a[q].w; }
            if (tw + 2 * pad > natw[c]) natw[c] = tw + 2 * pad;
            if (lw + 2 * pad > minw[c]) minw[c] = lw + 2 * pad;
        }
    }
    int tot = 0; for (int c = 0; c < nc; c++) tot += natw[c];
    int cw[32];
    if (tot <= w) { for (int c = 0; c < nc; c++) cw[c] = natw[c]; }
    else {
        int mint = 0; for (int c = 0; c < nc; c++) { if (minw[c] > w / nc && minw[c] > 120) minw[c] = w / nc > 60 ? w / nc : 60; mint += minw[c]; }
        int extra = w - mint; if (extra < 0) extra = 0;
        int want = 0; for (int c = 0; c < nc; c++) want += natw[c] - minw[c] > 0 ? natw[c] - minw[c] : 0;
        for (int c = 0; c < nc; c++) { int d = natw[c] - minw[c] > 0 ? natw[c] - minw[c] : 0; cw[c] = minw[c] + (want ? (int)((long)extra * d / want) : 0); }
    }
    int tw = 0; for (int c = 0; c < nc; c++) tw += cw[c];
    /* alto de cada fila */
    int rh[CEL_MAX], th = 0;
    for (int r = 0; r < rows; r++) {
        int mh = 0;
        for (int c = 0; c < nc; c++) { int idx = r * nc + c, mw; int h = flow_copia(L, S->cel_a[idx], S->cel_n[idx], 0, 0, cw[c] - 2 * pad, al[c], 0, &mw); if (h > mh) mh = h; }
        rh[r] = mh + 16; th += rh[r];
    }
    /* fondo, rayas, cabecera, bordes */
    op_rect(L, x, y, tw, th, 8, t->tr_alt & 0x00FFFFFFu, MD_RECT);
    op_rect(L, x, y, tw, rh[0], 8, t->th_bg, MD_RECT);
    op_rect(L, x, y + rh[0] - 8, tw, 8, 0, t->th_bg, MD_RECT);
    int yy = y + rh[0];
    for (int r = 1; r < rows; r++) {
        if (!(r & 1)) op_rect(L, x + 1, yy, tw - 2, rh[r] - (r == rows - 1 ? 1 : 0), r == rows - 1 ? 8 : 0, col_mix(t->tr_alt, t->th_bg, 70), MD_RECT);
        op_line(L, x, yy, x + tw, yy, 16, t->rule);
        yy += rh[r];
    }
    { int xx = x; for (int c = 0; c + 1 < nc; c++) { xx += cw[c]; op_line(L, xx, y, xx, y + th, 16, t->rule); } }
    op_rect(L, x, y, tw, th, 8, t->rule, MD_STROKE);
    yy = y;
    for (int r = 0; r < rows; r++) {
        int xx = x;
        for (int c = 0; c < nc; c++) {
            int idx = r * nc + c, mw;
            int h = flow_copia(L, S->cel_a[idx], S->cel_n[idx], 0, 0, cw[c] - 2 * pad, al[c], 0, &mw);
            (void)flow_copia(L, S->cel_a[idx], S->cel_n[idx], xx + pad, yy + (rh[r] - h) / 2, cw[c] - 2 * pad, al[c], 1, &mw);
            xx += cw[c];
        }
        yy += rh[r];
    }
    S->na = base0;
    L->o->tables++;
    if (x + tw > L->o->width) L->o->width = x + tw;
    return th;
}
/* separa src en lineas (en la arena) */
static int split_lines(struct L *L, const char *src, int len, struct ln **out)
{
    int n = 1; for (int i = 0; i < len; i++) if (src[i] == '\n') n++;
    struct ln *ls = (struct ln *)ar_take(L, (uint32_t)(n * (int)sizeof(struct ln) + 8));
    if (!ls) return 0;
    /* alinear */
    int k = 0, a = 0;
    for (int i = 0; i <= len; i++) if (i == len || src[i] == '\n') { int e = i; if (e > a && src[e - 1] == '\r') e--; ls[k].s = src + a; ls[k].n = e - a; k++; a = i + 1; }
    *out = ls; return k;
}
static char *join(struct L *L, const struct ln *ls, int a, int b, int strip, int quote, int *outn)   /* une lineas quitando sangria o "> " */
{
    uint32_t tot = 0; for (int i = a; i < b; i++) tot += (uint32_t)ls[i].n + 1;
    char *d = ar_take(L, tot + 1); if (!d) { *outn = 0; return 0; }
    int k = 0;
    for (int i = a; i < b; i++) {
        const char *s = ls[i].s; int n = ls[i].n;
        if (quote) { int j = 0; while (j < n && is_sp(s[j]) && j < 3) j++; if (j < n && s[j] == '>') { j++; if (j < n && s[j] == ' ') j++; } s += j; n -= j; }
        if (strip) s = skip_ind(s, n, strip, &n);
        for (int q = 0; q < n; q++) d[k++] = s[q];
        if (i + 1 < b) d[k++] = '\n';
    }
    d[k] = 0; *outn = k; return d;
}
static int blocks(struct L *L, const char *src, int len, int x, int y, int w)
{
    const struct md_theme *t = L->t; struct md_scr *S = L->S;
    if (++L->depth > 10) { L->depth--; struct sty st = { 0, 0, 0, t->fg, -1 }; return para(L, src, len, x, y, w, st, 0); }
    uint32_t ar_mark = S->nar;
    struct ln *ls; int nl = split_lines(L, src, len, &ls);
    int y0 = y, i = 0, gap = L->px * 70 / 100, prev = 0;
    while (i < nl && !L->o->full) {
        const char *s = ls[i].s; int n = ls[i].n;
        if (blank(s, n)) { i++; continue; }
        if (prev) y += gap;
        prev = 1; L->o->blocks++;
        int lv, fl; char fc;
        /* bloque de codigo */
        if (es_fence(s, n, &fc, &fl)) {
            int ind = indent_of(s, n), p = 0; while (p < n && is_sp(s[p])) p++;
            p += fl; while (p < n && is_sp(s[p])) p++;
            int l0 = p; while (p < n && !is_sp(s[p]) && s[p] != '{' && s[p] != fc) p++;
            int j = i + 1;
            while (j < nl) { char f2; int fl2; if (es_fence(ls[j].s, ls[j].n, &f2, &fl2) && f2 == fc && fl2 >= fl) { int q = 0; while (q < ls[j].n && (is_sp(ls[j].s[q]) || ls[j].s[q] == fc)) q++; if (q >= ls[j].n) break; } j++; }
            int cn; char *code = join(L, ls, i + 1, j, ind, 0, &cn);
            if (code) y += code_block(L, s + l0, p - l0, code, cn, x, y, w);
            i = j + 1; continue;
        }
        /* formula en bloque */
        if ((n >= 2 && s[0] == '$' && s[1] == '$') || (n >= 2 && s[0] == '\\' && s[1] == '[') || pre(s, n, "\\begin{")) {
            int j = i, env = pre(s, n, "\\begin{");
            const char *endtok = s[0] == '$' ? "$$" : env ? "\\end{" : "\\]";
            int first_close = 0;
            { int q = 2; for (; q + 1 < n; q++) if (!env && s[q] == endtok[0] && s[q + 1] == endtok[1]) { first_close = 1; break; } if (env) { for (q = 7; q < n; q++) if (pre(s + q, n - q, "\\end{")) { first_close = 1; break; } } }
            if (!first_close) {
                j = i + 1;
                while (j < nl) { int f = 0; for (int q = 0; q + 1 < ls[j].n; q++) if (pre(ls[j].s + q, ls[j].n - q, endtok)) { f = 1; break; } if (f) break; j++; }
            }
            if (j >= nl) j = nl - 1;
            int mn; char *m = join(L, ls, i, j + 1, 0, 0, &mn);
            int h = m ? math_block(L, m, mn, x, y, w) : 0;
            if (h > 0) { y += h; i = j + 1; continue; }
            struct sty st = { 0, 0, 0, t->fg, -1 }; y += para(L, m ? m : s, m ? mn : n, x, y, w, st, 0); i = j + 1; continue;
        }
        if (es_head(s, n, &lv)) {
            int p = 0; while (p < n && (is_sp(s[p]) || s[p] == '#')) p++;
            int e = n; while (e > p && (is_sp(s[e - 1]) || s[e - 1] == '#')) e--;
            static const int SC[7] = { 100, 160, 135, 117, 106, 100, 92 };
            int opx = L->px; L->px = opx * SC[lv] / 100;
            if (prev && y > y0) y += lv <= 2 ? gap : gap / 2;
            struct sty st = { MD_F_BOLD, 0, 0, lv >= 6 ? t->muted : t->head, -1 };
            y += para(L, s + p, e - p, x, y, w, st, 0);
            L->px = opx;
            if (lv <= 2) { y += 4; op_line(L, x, y, x + w, y, 16, t->rule); y += 2; }
            i++; continue;
        }
        if (es_hr(s, n)) { y += 4; op_line(L, x, y, x + w, y, 18, t->rule); y += 6; i++; continue; }
        /* cita */
        { int q = 0; while (q < n && is_sp(s[q]) && q < 3) q++;
          if (q < n && s[q] == '>') {
              int j = i; while (j < nl && !blank(ls[j].s, ls[j].n)) j++;
              int qn; char *qt = join(L, ls, i, j, 0, 1, &qn);
              /* alertas de GitHub: > [!NOTE] ... */
              uint32_t bar = t->quote; int skip = 0; const char *cap = 0;
              if (qt && qn > 4 && qt[0] == '[' && qt[1] == '!') {
                  if (pre(qt, qn, "[!warning]")) { bar = 0x00F59E0Bu; cap = "Atenci\303\263n"; }
                  else if (pre(qt, qn, "[!caution]")) { bar = 0x00EF4444u; cap = "Precauci\303\263n"; }
                  else if (pre(qt, qn, "[!tip]")) { bar = 0x0010B981u; cap = "Consejo"; }
                  else if (pre(qt, qn, "[!important]")) { bar = 0x00A855F7u; cap = "Importante"; }
                  else if (pre(qt, qn, "[!note]")) { bar = t->link; cap = "Nota"; }
                  if (cap) while (skip < qn && qt[skip] != '\n') skip++;
              }
              int h = 0;
              if (qt) {
                  struct md_theme t2 = *t; t2.fg = col_mix(t->fg, t->muted, 110);
                  const struct md_theme *ot = L->t; L->t = &t2;
                  if (cap) { op_text_str(L, x + 18, y + ASC(L, L->px) + 2, cap, L->px, MD_F_BOLD, bar); h += L->px * 160 / 100; }
                  h += blocks(L, qt + skip, qn - skip, x + 18, y + h, w - 18);
                  L->t = ot;
              }
              op_rect(L, x, y, 3, h, 1, bar, MD_RECT);
              y += h; i = j; continue;
          } }
        /* tabla */
        if (i + 1 < nl && tiene_barra(s, n) && es_tabla_sep(ls[i + 1].s, ls[i + 1].n)) {
            int j = i + 2; while (j < nl && !blank(ls[j].s, ls[j].n) && tiene_barra(ls[j].s, ls[j].n)) j++;
            y += table_block(L, ls + i, j - i, x, y, w);
            i = j; continue;
        }
        /* lista */
        { int ind, ord, num, cont;
          if (es_item(s, n, &ind, &ord, &num, &cont)) {
              int nitem = 0, base_ind = ind, ord0 = ord, cnt = num;
              int mark_w = ord ? W(L, "00.", 3, L->px, 0) + 10 : 22;
              { int q = i; int mx = num; while (q < nl) { int a, b, c, d; if (es_item(ls[q].s, ls[q].n, &a, &b, &c, &d) && a == base_ind && b == ord0 && c >= 0 && b) if (c > mx) mx = c; q++; } if (ord && mx >= 100) mark_w = W(L, "000.", 4, L->px, 0) + 10; }
              while (i < nl) {
                  int a, b, c, d;
                  if (!es_item(ls[i].s, ls[i].n, &a, &b, &c, &d) || b != ord0 || a > base_ind + 1 || a < base_ind) break;
                  if (nitem) y += L->px * 30 / 100;
                  /* contenido del elemento */
                  int j = i + 1, last_blank = 0;
                  while (j < nl) {
                      const char *s2 = ls[j].s; int n2 = ls[j].n;
                      if (blank(s2, n2)) { last_blank = 1; j++; continue; }
                      int i2 = indent_of(s2, n2), a2, b2, c2, d2, lv2; char f2; int fl2;
                      if (i2 >= (d < base_ind + 2 ? d : base_ind + 2) && i2 > base_ind) { last_blank = 0; j++; continue; }
                      if (last_blank) break;
                      if (es_item(s2, n2, &a2, &b2, &c2, &d2) || es_head(s2, n2, &lv2) || es_fence(s2, n2, &f2, &fl2) || es_hr(s2, n2) || s2[0] == '>' || (tiene_barra(s2, n2) && j + 1 < nl && es_tabla_sep(ls[j + 1].s, ls[j + 1].n))) break;
                      j++;      /* continuacion perezosa */
                  }
                  int e = j; while (e > i + 1 && blank(ls[e - 1].s, ls[e - 1].n)) e--;
                  /* primera linea sin el marcador */
                  struct ln save = ls[i]; int q = 0, col = 0;
                  while (q < ls[i].n && col < d) { col += ls[i].s[q] == '\t' ? 4 : 1; q++; }
                  if (q > ls[i].n) q = ls[i].n;
                  ls[i].s += q; ls[i].n -= q;
                  /* casilla de tarea */
                  int task = -1;
                  if (ls[i].n >= 3 && ls[i].s[0] == '[' && ls[i].s[2] == ']' && (ls[i].s[1] == ' ' || ls[i].s[1] == 'x' || ls[i].s[1] == 'X') && (ls[i].n == 3 || ls[i].s[3] == ' ')) {
                      task = ls[i].s[1] != ' '; ls[i].s += ls[i].n > 3 ? 4 : 3; ls[i].n -= ls[i].n > 3 ? 4 : 3;
                  }
                  int cn; char *ct = join(L, ls, i, e, d, 0, &cn);
                  ls[i] = save;
                  /* la primera linea no tiene sangria: join quito d columnas de las demas */
                  int lh_ = ASC(L, L->px) + DSC(L, L->px) + L->px * 55 / 100; if (lh_ < L->px * 155 / 100) lh_ = L->px * 155 / 100;
                  int my = y + (lh_ - ASC(L, L->px) - DSC(L, L->px)) / 2 + ASC(L, L->px);
                  uint32_t mc = t->muted;
                  if (task >= 0) {
                      int bx = x + mark_w - 18, by = my - ASC(L, L->px) + 1, bs = 14;
                      if (task) { op_rect(L, bx, by, bs, bs, 4, t->check, MD_RECT); op_line(L, bx + 3, by + 7, bx + 6, by + 10, 24, 0x00FFFFFFu); op_line(L, bx + 6, by + 10, bx + 11, by + 4, 24, 0x00FFFFFFu); }
                      else op_rect(L, bx, by, bs, bs, 4, t->muted, MD_STROKE);
                  } else if (ord) {
                      char nb[16]; int k = 0, v = cnt, tq[12], m = 0; do { tq[m++] = v % 10; v /= 10; } while (v && m < 10);
                      while (m) nb[k++] = (char)('0' + tq[--m]);
                      nb[k++] = '.'; nb[k] = 0;
                      int nw = W(L, nb, k, L->px, 0);
                      op_text_str(L, x + mark_w - 8 - nw, my, nb, L->px, 0, mc);
                  } else {
                      int lvl = L->depth - 1, cx = x + 8, cy = my - ASC(L, L->px) * 35 / 100, r = 3;
                      if (lvl % 3 == 0) op_rect(L, cx - r, cy - r, 2 * r, 2 * r, r, t->fg, MD_RECT);
                      else if (lvl % 3 == 1) op_rect(L, cx - r, cy - r, 2 * r, 2 * r, r, t->muted, MD_STROKE);
                      else op_rect(L, cx - r + 1, cy - r + 1, 2 * r - 2, 2 * r - 2, 1, t->muted, MD_RECT);
                  }
                  int h = ct ? blocks(L, ct, cn, x + mark_w, y, w - mark_w) : 0;
                  if (h < L->px * 155 / 100) h = L->px * 155 / 100;
                  if (task == 1) { /* tachar no: solo mas apagado */ }
                  y += h; nitem++; cnt++;
                  i = j;
                  while (i < nl && blank(ls[i].s, ls[i].n)) { int k2 = i + 1; while (k2 < nl && blank(ls[k2].s, ls[k2].n)) k2++; int a2, b2, c2, d2; if (k2 < nl && es_item(ls[k2].s, ls[k2].n, &a2, &b2, &c2, &d2) && b2 == ord0 && a2 == base_ind) i = k2; else break; }
              }
              continue;
          } }
        /* parrafo */
        { int j = i + 1;
          while (j < nl && !blank(ls[j].s, ls[j].n)) {
              if (empieza_bloque(ls[j].s, ls[j].n)) break;
              if (tiene_barra(ls[j].s, ls[j].n) && j + 1 < nl && es_tabla_sep(ls[j + 1].s, ls[j + 1].n)) break;
              j++;
          }
          int pn; char *pt = join(L, ls, i, j, 0, 0, &pn);
          struct sty st = { 0, 0, 0, t->fg, -1 };
          if (pt) y += para(L, pt, pn, x, y, w, st, 0);
          i = j; }
    }
    S->nar = ar_mark;
    L->depth--;
    return y - y0;
}
int md_layout(const char *src, uint32_t len, int width, int base_px, const struct md_metrics *m, const struct md_theme *t,
              struct md_out *o, void *scratch)
{
    struct L L; L.m = m; L.t = t; L.o = o; L.S = (struct md_scr *)scratch; L.px = base_px; L.depth = 0; L.lk_src = -1; L.lk_cur = -1; L.lk_y = 0;
    o->nops = 0; o->npool = 0; o->nz = 0; o->height = 0; o->width = 0; o->full = 0; o->blocks = o->maths = o->codes = o->tables = o->images = o->links = 0;
    L.S->nar = 0; L.S->na = 0; L.S->ntx = 0; L.S->ntp = 0;
    if (!src || !len || width < 40) return 0;
    o->height = blocks(&L, src, (int)len, 0, 0, width);
    return o->height;
}
