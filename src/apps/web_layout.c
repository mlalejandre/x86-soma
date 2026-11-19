/* SOMA_PARCHE_NAVEGADOR: motor de maquetacion (cajas, texto en linea, flotantes, flex, grid, tablas) y pintado. */
#include "web_layout.h"
#include "web_raster_150.h" /* SOMA_150_WEB_RASTER */
#include "web_font.h"
#include "fb.h"
/* PARCHE 085: el contenido de las paginas no usa los ajustes de fuente de la interfaz */
#define wf_draw(x, y, s, n, px, c, fl, a) wf_draw((x), (y), (s), (n), (px), (c), (fl) | WF_RAW | WF_WEB, (a))
#define wf_width64(s, n, px, fl) wf_width64((s), (n), (px), (fl) | WF_RAW | WF_WEB)
#define wf_cp_adv64(cp, px, fl) wf_cp_adv64((cp), (px), (fl) | WF_WEB)   /* SOMA_182 */
#define wf_ascent(px) wf_web_ascent(px)
#define wf_descent(px) wf_web_descent(px)

#define MAXD 100
static inline int wl181_off(void) { return g_wl181_off; }
#define BIG 0x3fffffff
enum { BK_BLOCK = 1, BK_FLEX, BK_GRID, BK_TABLE, BK_TROW, BK_REPL };
enum { TS_CAPTION = 1, TS_CELL = 2 };

static inline int wmax(int a, int b) { return a > b ? a : b; }
static inline int wmin(int a, int b) { return a < b ? a : b; }
static inline int wclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int ceil64(int v) { return g_wl181_off ? ((v + 63) >> 6) : ((v + 32) >> 6); }   /* SOMA_181: redondeo al entero mas cercano (antes por exceso: cada palabra sumaba ~0,5 px y las lineas ajustadas se partian de mas) */

static int lenv(const struct wlen *l, int base, int def)
{
    if (l->kind == LK_VAL) return l->px + (int)(((int64_t)base * l->pct) / 10000);
    return def;
}
static int fontfl(const struct wstyle *s) { return (s->bold ? WF_BOLD : 0) | (s->italic ? WF_ITALIC : 0) | (s->mono ? WF_MONO : 0); }
static int textw(const struct wstyle *s, const char *t, uint32_t n)
{
    int w = ceil64(wf_width64(t, n, s->font_px, fontfl(s)));
    if (s->letter_sp && n) { uint32_t cnt = 0, i = 0, adv; while (i < n) { wu_utf8_next(t + i, &adv); i += adv; cnt++; } w += cnt * s->letter_sp; }
    return w;
}
static int spacew(const struct wstyle *s) { int w = (wf_cp_adv64(' ', s->font_px, fontfl(s)) + 32) >> 6; return w + s->letter_sp; }
/* SOMA_182: con Liberation Sans el espacio mide fracciones de pixel (16 px: 4,44); redondear cada espacio acumula un error del 1 % por linea.
 * Se arrastra el resto en 1/64 px por linea para que la suma sea la de Chrome. La anchura intrinseca usa la misma cuenta. */
static int spacew64(const struct wstyle *s) { return wf_cp_adv64(' ', s->font_px, fontfl(s)) + s->letter_sp * 64; }
static int sp_carry(int *fr, const struct wstyle *s)
{
    if (!g_wf182_on) return spacew(s);
    int w64 = spacew64(s), w = w64 >> 6; *fr += w64 & 63;
    if (*fr >= 64) { w++; *fr -= 64; }
    return w;
}

/* ----------------------------- contexto de flotantes ----------------------------- */
struct wflr { int x0, y0, x1, y1; };
struct wfl { struct wflr *r; int n, cap, maxb, lasty; };
struct wctx { struct wfl *fl; int ox, oy; };

static void fl_push(struct wlay *L, struct wfl *f, int x0, int y0, int x1, int y1)
{
    if (f->n == f->cap) {
        int nc = f->cap ? f->cap * 2 : 16; struct wflr *np = (struct wflr *)wa_alloc(&L->ar, nc * sizeof(*np));
        if (!np) return;
        if (f->r) memcpy(np, f->r, f->n * sizeof(*np));
        f->r = np; f->cap = nc;
    }
    f->r[f->n].x0 = x0; f->r[f->n].y0 = y0; f->r[f->n].x1 = x1; f->r[f->n].y1 = y1; f->n++;
    if (y1 > f->maxb) f->maxb = y1;
}
/* borde izquierdo ocupado / derecho ocupado en la franja [y, y+h) */
static void fl_band(const struct wfl *f, int y, int h, int *l, int *r)
{
    *l = -BIG; *r = BIG;
    for (int i = 0; i < f->n; i++) {
        const struct wflr *q = &f->r[i];
        if (q->y1 <= y || q->y0 >= y + (h > 0 ? h : 1)) continue;
        if (q->x0 < 0 && q->x1 < 0) continue;
        if (q->x0 == -BIG) { if (q->x1 > *l) *l = q->x1; } else if (q->x1 == BIG) { if (q->x0 < *r) *r = q->x0; }
    }
}
static int fl_clear_y(const struct wfl *f, int side /* 1 izq, 2 der, 3 ambos */)
{
    int y = -BIG;
    for (int i = 0; i < f->n; i++) { int left = f->r[i].x0 == -BIG; if (((side & 1) && left) || ((side & 2) && !left)) if (f->r[i].y1 > y) y = f->r[i].y1; }
    return y;
}
static int fl_next_bottom(const struct wfl *f, int y)
{
    int best = BIG; for (int i = 0; i < f->n; i++) if (f->r[i].y1 > y && f->r[i].y1 < best) best = f->r[i].y1; return best;
}

/* ----------------------------- construccion de cajas ----------------------------- */
struct iarr { struct wii *p; int n, cap; };
static int ia_push(struct iarr *a, const struct wii *it)
{
    if (a->n == a->cap) {
        int nc = a->cap ? a->cap * 2 : 16; struct wii *np = (struct wii *)kmalloc(nc * sizeof(*np)); if (!np) return 0;
        if (a->p) { memcpy(np, a->p, a->n * sizeof(*np)); kfree(a->p); } a->p = np; a->cap = nc;
    }
    a->p[a->n++] = *it; return 1;
}
struct wbld { struct wlay *L; struct wbox *cb; int depth; int nboxes; };

static struct wbox *mkbox(struct wbld *B, struct wnode *n, const struct wstyle *st, uint8_t kind)
{
    wu_tick();
    struct wbox *b = (struct wbox *)wa_zalloc(&B->L->ar, sizeof(*b)); if (!b) return 0;
    b->node = n; b->st = st; b->kind = kind; b->zi = st ? st->z_index : 0; b->def_h = -1; B->nboxes++;
    if (n && n->type == WN_ELEM && !n->box) n->box = b;
    return b;
}
static void addchild(struct wbox *p, struct wbox *c)
{
    c->parent = p; c->next = 0;
    if (p->last) p->last->next = c; else p->first = c;
    p->last = c;
}
static int is_float(const struct wstyle *s) { return s->floating && s->position != P_ABSOLUTE && s->position != P_FIXED; }
static int is_abs(const struct wstyle *s) { return s->position == P_ABSOLUTE || s->position == P_FIXED; }
static int edisp(const struct wnode *n)
{
    const struct wstyle *s = n->st; if (!s) return D_NONE;
    int d = s->display;
    if (is_float(s) || is_abs(s)) { if (d == D_INLINE || d == D_INLINE_BLOCK) d = D_BLOCK; else if (d == D_INLINE_FLEX) d = D_FLEX; else if (d == D_INLINE_TABLE) d = D_TABLE; }
    return d;
}
static int is_replaced(const struct wnode *n)
{
    switch (n->tag) {
    case WT_IMG: case WT_SELECT: case WT_TEXTAREA: case WT_IFRAME: case WT_EMBED: case WT_VIDEO: case WT_AUDIO: case WT_CANVAS: case WT_SVG: case WT_OBJECT: return 1;
    case WT_INPUT: { const char *t = wd_attr(n, "type"); return !(t && wu_strieq(t, "hidden")); }
    default: return 0;
    }
}
static int blocklevel(int d) { return d == D_BLOCK || d == D_LIST_ITEM || d == D_FLEX || d == D_GRID || d == D_TABLE || d == D_TABLE_ROW || d == D_TABLE_CELL || d == D_TABLE_GROUP || d == D_TABLE_CAPTION; }
static int has_block_desc(const struct wnode *n, int depth)
{
    if (depth > 6) return 0;
    for (const struct wnode *c = n->first; c; c = c->next) {
        if (c->type != WN_ELEM || !c->st) continue;
        int d = edisp(c); if (d == D_NONE) continue;
        if (is_float(c->st) || is_abs(c->st)) continue;
        if (d == D_CONTENTS) { if (has_block_desc(c, depth + 1)) return 1; continue; }
        if (is_replaced(c)) continue;
        if (blocklevel(d)) return 1;
        if (d == D_INLINE && has_block_desc(c, depth + 1)) return 1;
    }
    return 0;
}
/* tratamiento de espacios y mayusculas */
static uint32_t case_cp(uint32_t cp, int mode)   /* mode 1 mayus, 2 minus */
{
    if (mode == 1) { if (cp >= 'a' && cp <= 'z') return cp - 32; if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 32; if (cp >= 0x430 && cp <= 0x44F) return cp - 32; if (cp >= 0x450 && cp <= 0x45F) return cp - 80; if (cp >= 0x3B1 && cp <= 0x3C9 && cp != 0x3C2) return cp - 32; }
    else { if (cp >= 'A' && cp <= 'Z') return cp + 32; if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32; if (cp >= 0x410 && cp <= 0x42F) return cp + 32; if (cp >= 0x400 && cp <= 0x40F) return cp + 80; if (cp >= 0x391 && cp <= 0x3A9) return cp + 32; }
    return cp;
}
static const char *norm_text(struct wlay *L, const char *s, uint32_t n, const struct wstyle *st, uint32_t *outlen)
{
    int ws = st->white_space; int preserve = (ws == WS_PRE || ws == WS_PRE_WRAP);
    uint32_t tabs = 0; if (preserve) for (uint32_t i = 0; i < n; i++) if (s[i] == '\t') tabs++;
    char *o = (char *)wa_alloc(&L->ar, n + tabs * 3 + 4); if (!o) { *outlen = 0; return ""; }
    uint32_t k = 0; int prev_sp = 0, cap_next = 1;
    for (uint32_t i = 0; i < n; ) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\r') { i++; continue; }
        int isws = (c == ' ' || c == '\t' || c == '\n' || c == '\f');
        if (preserve) {
            if (c == '\t') { for (int t = 0; t < 4; t++) o[k++] = ' '; i++; continue; }
        } else if (ws == WS_PRE_LINE) {
            if (c == '\n') { while (k && o[k - 1] == ' ') k--; o[k++] = '\n'; prev_sp = 1; i++; continue; }
            if (isws) { if (!prev_sp) o[k++] = ' '; prev_sp = 1; i++; continue; }
        } else if (isws) { if (!prev_sp) o[k++] = ' '; prev_sp = 1; i++; continue; }
        prev_sp = 0;
        if (st->text_transform && c >= 0x20) {
            uint32_t adv; uint32_t cp = wu_utf8_next(s + i, &adv); uint32_t nc = cp;
            if (st->text_transform == 1) nc = case_cp(cp, 1); else if (st->text_transform == 2) nc = case_cp(cp, 2);
            else if (st->text_transform == 3) { if (cap_next) nc = case_cp(cp, 1); }
            cap_next = (c == ' ' || c == '\n' || c == '(' || c == '"');
            char tmp[4]; int tl = wu_utf8_put(nc, tmp);
            if ((uint32_t)tl == adv) { for (int t = 0; t < tl; t++) o[k++] = tmp[t]; } else { for (uint32_t t = 0; t < adv; t++) o[k++] = s[i + t]; }
            i += adv; continue;
        }
        o[k++] = (char)c; i++;
    }
    o[k] = 0; *outlen = k; return o;
}

/* ---- enlace con el cargador: lectura de imagen ---- */
static int img_info_need(struct wlay *L, const char *url, struct wimginfo *o, int need)
{
    memset(o, 0, sizeof(*o)); o->need_size = need; if (!L->img_cb || !url || !*url) return 0;
    return L->img_cb(L->img_ud, url, o);
}
static int img_info(struct wlay *L, const char *url, struct wimginfo *o)
{
    memset(o, 0, sizeof(*o)); if (!L->img_cb || !url || !*url) return 0;
    o->painting = L->painting;
    return L->img_cb(L->img_ud, url, o);
}

struct wcont {
    struct wbld *B; struct wbox *b; struct wnode *host;
    struct iarr run; int need_blocks, flexlike, lcount, lstart, is_list;
    int tmp_depth;
};
static void build_content(struct wbld *B, struct wbox *b, struct wnode *n, int flexlike);
static struct wbox *build_elem(struct wbld *B, struct wnode *n, struct wbox *parent, int forced_block);

static void flush_run(struct wcont *C)
{
    struct wbld *B = C->B; struct wlay *L = B->L;
    if (!C->run.n) return;
    struct wii *src = C->run.p; int cnt = C->run.n;
    if (C->need_blocks) {
        int only_ws = 1;
        for (int i = 0; i < cnt; i++) {
            if (src[i].type == II_TEXT) { for (uint32_t k = 0; k < src[i].len; k++) if (src[i].txt[k] != ' ' && src[i].txt[k] != '\n') { only_ws = 0; break; } }
            else if (src[i].type == II_FLOAT || src[i].type == II_ABS) only_ws = 0;
            else if (src[i].type != II_OPEN && src[i].type != II_CLOSE) only_ws = 0;
        }
        if (only_ws) {
            C->run.n = 0; return;
        }
        struct wbox *a = mkbox(B, C->host, C->b->st, BK_BLOCK); if (!a) { C->run.n = 0; return; }
        a->flags |= BF_ANON;
        a->items = (struct wii *)wa_alloc(&L->ar, cnt * sizeof(*a->items)); if (!a->items) { C->run.n = 0; return; }
        memcpy(a->items, src, cnt * sizeof(*a->items)); a->nitems = cnt;
        addchild(C->b, a);
        /* los flotantes/absolutos de la tanda pasan a ser hijos de la caja anonima */
        for (int i = 0; i < cnt; i++) if (a->items[i].box && (a->items[i].type == II_FLOAT || a->items[i].type == II_ABS || a->items[i].type == II_ATOM)) a->items[i].box->parent = a;
    } else {
        C->b->items = (struct wii *)wa_alloc(&L->ar, cnt * sizeof(*C->b->items)); if (!C->b->items) { C->run.n = 0; return; }
        memcpy(C->b->items, src, cnt * sizeof(*C->b->items)); C->b->nitems = cnt;
    }
    C->run.n = 0;
}

static void emit_node(struct wcont *C, struct wnode *c, struct wnode *link);

/* SOMA_188: <slot> dentro de un arbol sombra: su anfitrion (o NULL) y que hijos del anfitrion le tocan */
static struct wnode *slot_host(const struct wnode *n)
{
    if (!n || n->type != WN_ELEM || !n->name || !wu_streq(n->name, "slot")) return 0;
    for (const struct wnode *a = n->parent; a; a = a->parent) if (wd_is_shadow_root(a)) return a->parent;
    return 0;
}
static int slot_takes(const struct wnode *c, const char *slotname)
{
    if (c->type == WN_TEXT) return !slotname || !*slotname;
    if (c->type != WN_ELEM) return 0;
    const char *s = wd_attr(c, "slot");
    if (!slotname || !*slotname) return !s || !*s;
    return s && wu_streq(s, slotname);
}
/* SOMA_188: ::before / ::after activos de un elemento */
static struct wnode *pse_of(const struct wnode *n, int k)
{
    if (!n || n->type != WN_ELEM || !n->st) return 0;
    struct wnode *p = n->st->pse[k];
    return p && p->st && p->st->display != D_NONE ? p : 0;
}
static void emit_children(struct wcont *C, struct wnode *n, struct wnode *link)
{
    struct wbld *B = C->B;
    if (B->depth > MAXD || web_stack_left() < 6000) return;
    B->depth++;
    struct wnode *pb = pse_of(n, 0), *pa = pse_of(n, 1);
    if (pb) emit_node(C, pb, link);
    if (n->type == WN_ELEM && n->shadow && !wl181_off()) {   /* SOMA_188: shadow DOM: se dibuja el arbol sombra */
        for (struct wnode *c = n->shadow->first; c; c = c->next) emit_node(C, c, link);
    } else if (slot_host(n)) {   /* <slot>: nodos asignados del anfitrion o, si no hay, su contenido por defecto */
        struct wnode *h = slot_host(n); const char *nm = wd_attr(n, "name"); int any = 0;
        for (struct wnode *c = h->first; c; c = c->next) if (slot_takes(c, nm)) { emit_node(C, c, link); any = 1; }
        if (!any) for (struct wnode *c = n->first; c; c = c->next) emit_node(C, c, link);
    } else
    for (struct wnode *c = n->first; c; c = c->next) emit_node(C, c, link);
    if (pa) emit_node(C, pa, link);
    B->depth--;
}

/* hijo "lista de elementos" contable para <ol start> */
static void emit_node(struct wcont *C, struct wnode *c, struct wnode *link)
{
    struct wbld *B = C->B; struct wlay *L = B->L; struct wii it;
    memset(&it, 0, sizeof it);
    if (c->type == WN_TEXT) {
        const struct wnode *p = c->parent; const struct wstyle *ps = p && p->st ? p->st : C->b->st;
        if (!ps || !c->tlen) return;
        uint32_t nl; const char *t = norm_text(L, c->name, c->tlen, ps, &nl);
        if (!nl) return;
        it.type = II_TEXT; it.st = ps; it.node = c; it.link = link; it.txt = t; it.len = nl; ia_push(&C->run, &it); return;
    }
    if (c->type != WN_ELEM || !c->st) return;
    int d = edisp(c);
    if (d == D_NONE) return;
    if (c->tag == WT_INPUT) { const char *t = wd_attr(c, "type"); if (t && wu_strieq(t, "hidden")) return; }
    if (d == D_CONTENTS) { emit_children(C, c, c->tag == WT_A && wd_attr(c, "href") ? c : link); return; }
    const struct wstyle *s = c->st;
    if (c->tag == WT_BR) { it.type = II_BR; it.st = s; it.node = c; ia_push(&C->run, &it); return; }
    if (c->tag == WT_WBR) return;
    struct wnode *nlink = (c->tag == WT_A && wd_attr(c, "href")) ? c : link;
    int fl = is_float(s), ab = is_abs(s);
    int blockish = blocklevel(d) || C->flexlike;
    if (!fl && !ab && d == D_INLINE && !is_replaced(c) && has_block_desc(c, 0)) blockish = 1;
    if (ab || fl) {
        struct wbox *x = build_elem(B, c, C->b, 1); if (!x) return;
        x->flags |= ab ? (s->position == P_FIXED ? (BF_ABS | BF_FIXED) : BF_ABS) : BF_FLOAT;
        if (C->flexlike && fl) x->flags &= ~BF_FLOAT;
        if (C->need_blocks && !C->run.n) { addchild(C->b, x); }
        else { it.type = ab ? II_ABS : II_FLOAT; it.box = x; it.st = s; it.node = c; ia_push(&C->run, &it); }
        return;
    }
    if (blockish) {
        flush_run(C);
        struct wbox *x = build_elem(B, c, C->b, 1); if (!x) return;
        if (d == D_LIST_ITEM) { x->list_n = C->lcount; }
        if (c->tag == WT_LI) { const char *v = wd_attr(c, "value"); if (v) { C->lcount = wu_atoi(v); x->list_n = C->lcount; } C->lcount++; }
        else if (d == D_LIST_ITEM) C->lcount++;
        x->tab_sub = 0; addchild(C->b, x); return;
    }
    if (d == D_INLINE && !is_replaced(c)) {
        it.type = II_OPEN; it.st = s; it.node = c; it.link = nlink; ia_push(&C->run, &it);
        emit_children(C, c, nlink);
        memset(&it, 0, sizeof it); it.type = II_CLOSE; it.st = s; it.node = c; it.link = nlink; ia_push(&C->run, &it);
        return;
    }
    /* atomo en linea: inline-block, inline-flex, inline-table, reemplazado */
    struct wbox *x = build_elem(B, c, C->b, 0); if (!x) return;
    it.type = II_ATOM; it.box = x; it.st = s; it.node = c; it.link = nlink; ia_push(&C->run, &it);
}

static void build_table(struct wbld *B, struct wbox *tb, struct wnode *n);

static void build_content(struct wbld *B, struct wbox *b, struct wnode *n, int flexlike)
{
    if (B->depth > MAXD || web_stack_left() < 6000) return;
    struct wcont C; memset(&C, 0, sizeof C); C.B = B; C.b = b; C.host = n; C.flexlike = flexlike; C.lcount = 1;
    if (n->tag == WT_OL) { const char *s = wd_attr(n, "start"); if (s) C.lcount = wu_atoi(s); }
    /* decide si hay hijos de nivel bloque */
    C.need_blocks = flexlike;
    if (!C.need_blocks) {
        struct wnode *stack[MAXD + 2]; int sp = 0; (void)stack; (void)sp;
        for (struct wnode *c = (n->shadow && !wl181_off()) ? n->shadow->first : n->first; c && !C.need_blocks; c = c->next) {
            if (c->type != WN_ELEM || !c->st) continue;
            if (slot_host(c)) { struct wnode *h = slot_host(c); const char *nm = wd_attr(c, "name"); for (struct wnode *k = h->first; k; k = k->next) if (k->type == WN_ELEM && k->st && slot_takes(k, nm) && blocklevel(edisp(k)) && !is_float(k->st) && !is_abs(k->st)) C.need_blocks = 1; continue; }
            int d = edisp(c); if (d == D_NONE) continue;
            if (is_float(c->st) || is_abs(c->st)) continue;
            if (d == D_CONTENTS) { if (has_block_desc(c, 0)) C.need_blocks = 1; continue; }
            if (is_replaced(c)) continue;
            if (blocklevel(d)) C.need_blocks = 1; else if (d == D_INLINE && has_block_desc(c, 0)) C.need_blocks = 1;
        }
        for (int k = 0; k < 2 && !C.need_blocks; k++) { struct wnode *ps = pse_of(n, k); if (ps && !is_float(ps->st) && !is_abs(ps->st) && blocklevel(edisp(ps))) C.need_blocks = 1; }   /* SOMA_188 */
    }
    emit_children(&C, n, n->tag == WT_A && wd_attr(n, "href") ? n : 0);
    flush_run(&C);
    if (C.run.p) kfree(C.run.p);
    (void)B;
}

static void build_table(struct wbld *B, struct wbox *tb, struct wnode *n)
{
    struct wbox *row = 0;
    if (B->depth > MAXD) return;
    B->depth++;
    for (int pass = 0; pass < 1; pass++) {
        struct wnode *gstack[2] = { n, 0 };
        (void)gstack;
        /* recorre hijos; los grupos se aplanan un nivel */
        struct wnode *c = n->first; struct wnode *g = 0;
        while (c) {
            struct wnode *cur = c;
            if (cur->type == WN_ELEM && cur->st && edisp(cur) == D_TABLE_GROUP && !g) { g = cur; c = cur->first; if (!c) { c = g->next; g = 0; } continue; }
            struct wnode *nxt = c->next; if (!nxt && g) { nxt = g->next; g = 0; }
            if (cur->type == WN_ELEM && cur->st) {
                int d = edisp(cur);
                if (d == D_NONE) { c = nxt; continue; }
                if (d == D_TABLE_CAPTION) {
                    struct wbox *cap = mkbox(B, cur, cur->st, BK_BLOCK);
                    if (cap) { cap->tab_sub = TS_CAPTION; cap->flags |= BF_BFC; build_content(B, cap, cur, 0); addchild(tb, cap); }
                    c = nxt; continue;
                }
                if (d == D_TABLE_ROW) {
                    row = mkbox(B, cur, cur->st, BK_TROW); if (!row) { c = nxt; continue; }
                    addchild(tb, row);
                    for (struct wnode *k = cur->first; k; k = k->next) {
                        if (k->type != WN_ELEM || !k->st) continue;
                        int kd = edisp(k); if (kd == D_NONE) continue;
                        struct wbox *cell = mkbox(B, k, k->st, BK_BLOCK); if (!cell) continue;
                        cell->tab_sub = TS_CELL; cell->flags |= BF_BFC;
                        const char *cs = wd_attr(k, "colspan"), *rs = wd_attr(k, "rowspan");
                        cell->colspan = cs ? wclamp(wu_atoi(cs), 1, 64) : 1; cell->rowspan = rs ? wclamp(wu_atoi(rs), 1, 64) : 1;
                        if (kd == D_TABLE_CELL) build_content(B, cell, k, 0);
                        else { /* hijo suelto dentro de una fila: celda anonima */
                            struct wcont C; memset(&C, 0, sizeof C); C.B = B; C.b = cell; C.host = k; C.need_blocks = 1; C.lcount = 1;
                            emit_node(&C, k, 0); flush_run(&C); if (C.run.p) kfree(C.run.p);
                        }
                        addchild(row, cell);
                    }
                    c = nxt; continue;
                }
            }
            /* celda suelta u otro contenido: fila anonima */
            if (cur->type == WN_ELEM && cur->st && (edisp(cur) == D_TABLE_CELL || 1)) {
                if (!row || !(row->flags & BF_ANON)) { row = mkbox(B, 0, tb->st, BK_TROW); if (row) { row->flags |= BF_ANON; addchild(tb, row); } }
                if (row) {
                    struct wbox *cell = mkbox(B, cur, cur->st, BK_BLOCK);
                    if (cell) {
                        cell->tab_sub = TS_CELL; cell->flags |= BF_BFC; cell->colspan = 1; cell->rowspan = 1;
                        if (edisp(cur) == D_TABLE_CELL) {
                            const char *cs = wd_attr(cur, "colspan"); if (cs) cell->colspan = wclamp(wu_atoi(cs), 1, 64);
                            build_content(B, cell, cur, 0);
                        } else { struct wcont C; memset(&C, 0, sizeof C); C.B = B; C.b = cell; C.host = cur; C.need_blocks = 1; C.lcount = 1; emit_node(&C, cur, 0); flush_run(&C); if (C.run.p) kfree(C.run.p); }
                        addchild(row, cell);
                    }
                }
            }
            c = nxt;
        }
    }
    B->depth--;
}

static struct wbox *build_elem(struct wbld *B, struct wnode *n, struct wbox *parent, int forced_block)
{
    const struct wstyle *s = n->st; int d = edisp(n); (void)forced_block;
    struct wbox *b = 0;
    if (is_replaced(n)) {
        b = mkbox(B, n, s, BK_REPL); if (!b) return 0;
        b->flags |= BF_BFC;
        switch (n->tag) {
        case WT_IMG: b->ctl = CTL_IMG; break;
        case WT_SVG: b->ctl = CTL_IMG; break;       /* SOMA_PARCHE_028_UI: <svg> en linea, rasterizado por la pasarela */
        case WT_SELECT: b->ctl = CTL_SELECT; break;
        case WT_TEXTAREA: b->ctl = CTL_TEXTAREA; break;
        case WT_INPUT: {
            const char *t = wd_attr(n, "type"); b->ctl = CTL_TEXT;
            if (t) {
                if (wu_strieq(t, "password")) b->ctl = CTL_PASSWORD; else if (wu_strieq(t, "checkbox")) b->ctl = CTL_CHECK; else if (wu_strieq(t, "radio")) b->ctl = CTL_RADIO;
                else if (wu_strieq(t, "submit") || wu_strieq(t, "button") || wu_strieq(t, "reset")) b->ctl = CTL_BTN_INPUT;
                else if (wu_strieq(t, "image")) b->ctl = CTL_IMG; else if (wu_strieq(t, "range")) b->ctl = CTL_RANGE;
                else if (wu_strieq(t, "color")) b->ctl = CTL_COLOR; else if (wu_strieq(t, "file")) b->ctl = CTL_FILE;
            }
            break; }
        default: b->ctl = CTL_PLACEHOLDER; break;
        }
    } else if (d == D_TABLE || d == D_INLINE_TABLE) {
        b = mkbox(B, n, s, BK_TABLE); if (!b) return 0; b->flags |= BF_BFC;
    } else if (d == D_FLEX || d == D_INLINE_FLEX) {
        b = mkbox(B, n, s, BK_FLEX); if (!b) return 0; b->flags |= BF_BFC;
    } else if (d == D_GRID) {
        b = mkbox(B, n, s, BK_GRID); if (!b) return 0; b->flags |= BF_BFC;
    } else {
        b = mkbox(B, n, s, BK_BLOCK); if (!b) return 0;
        if (d == D_INLINE_BLOCK || d == D_TABLE_CELL || s->overflow_x || s->overflow_y || is_float(s) || is_abs(s)) b->flags |= BF_BFC;
    }
    b->parent = parent;
    if (is_float(s)) b->flags |= BF_FLOAT;
    if (d == D_INLINE || d == D_INLINE_BLOCK || d == D_INLINE_FLEX || d == D_INLINE_TABLE) b->flags |= BF_INLINE;
    struct wbox *savecb = B->cb;
    if (s->position == P_ABSOLUTE) { b->abs_cb = B->cb; }
    if (s->position == P_RELATIVE || s->position == P_ABSOLUTE || s->position == P_FIXED || s->position == P_STICKY) { if (s->position != P_FIXED) B->cb = b; else B->cb = b; }
    if (s->position == P_RELATIVE || s->position == P_STICKY) b->flags |= BF_REL;
    if (s->position == P_ABSOLUTE) {   /* se registra en el contenedor posicionado */
        struct wbox *cb = savecb; if (cb) { b->abs_next = cb->abs_chain; cb->abs_chain = b; } else { b->abs_next = B->L->root ? B->L->root->abs_chain : 0; }
    } else if (s->position == P_FIXED) {
        b->abs_cb = 0; if (B->L->root) { b->abs_next = B->L->root->abs_chain; B->L->root->abs_chain = b; }
    }
    if (b->kind == BK_TABLE) build_table(B, b, n);
    else if (b->kind == BK_FLEX || b->kind == BK_GRID) build_content(B, b, n, 1);
    else if (b->kind == BK_BLOCK) build_content(B, b, n, 0);
    B->cb = savecb;
    return b;
}

/* ----------------------------- controles de formulario ----------------------------- */
const char *wl_img_src(const struct wnode *n)
{
    if (n->tag == WT_SVG) return wd_attr(n, "data-soma-svg");
    const char *s = wd_attr(n, "src");
    if (n->tag == WT_INPUT) return s;
    const char *ds = wd_attr(n, "data-src"); if (!ds) ds = wd_attr(n, "data-original"); if (!ds) ds = wd_attr(n, "data-lazy-src");
    if (ds && *ds && (!s || !*s || wu_starts(s, "data:image/gif") || wu_starts(s, "data:image/svg"))) return ds;
    return s;
}
static void opt_collect(struct wnode *n, struct wnode **out, int *cnt, int max)
{
    for (struct wnode *c = n->first; c; c = c->next) {
        if (c->type != WN_ELEM) continue;
        if (c->tag == WT_OPTION) { if (*cnt < max) out[(*cnt)++] = c; }
        else if (c->tag == WT_OPTGROUP) opt_collect(c, out, cnt, max);
    }
}
int wl_select_count(struct wnode *n) { struct wnode *o[256]; int c = 0; opt_collect(n, o, &c, 256); return c; }
struct wnode *wl_select_option(struct wnode *n, int i) { struct wnode *o[256]; int c = 0; opt_collect(n, o, &c, 256); return (i >= 0 && i < c) ? o[i] : 0; }
int wl_select_index(struct wnode *n)
{
    struct wnode *o[256]; int c = 0; opt_collect(n, o, &c, 256);
    if (n->flags & WNF_EDITED) return n->sel < c ? n->sel : 0;
    for (int i = 0; i < c; i++) if (wd_attr(o[i], "selected")) return i;
    return 0;
}
static const char *opt_label(struct wnode *o)
{
    const char *l = wd_attr(o, "label"); if (l && *l) return l;
    for (struct wnode *t = o->first; t; t = t->next) if (t->type == WN_TEXT && t->tlen) return t->name;
    l = wd_attr(o, "value"); return l ? l : "";
}
const char *wl_ctl_text(struct wnode *n)
{
    if (n->tag == WT_TEXTAREA) {
        if (n->value) return n->value;
        for (struct wnode *t = n->first; t; t = t->next) if (t->type == WN_TEXT) return t->name;
        return "";
    }
    if (n->tag == WT_SELECT) { struct wnode *o = wl_select_option(n, wl_select_index(n)); return o ? opt_label(o) : ""; }
    if (n->value) return n->value;
    const char *v = wd_attr(n, "value");
    if (n->tag == WT_INPUT) {
        const char *t = wd_attr(n, "type");
        if ((!v || !*v) && t && wu_strieq(t, "submit")) return "Submit";
        if ((!v || !*v) && t && wu_strieq(t, "reset")) return "Reset";
    }
    return v ? v : "";
}
static int attr_px(const struct wnode *n, const char *name, int base, int *is_pct)
{
    const char *v = wd_attr(n, name); *is_pct = 0; if (!v) return -1;
    while (wu_isspace(*v)) v++;
    if (!wu_isdigit(*v)) return -1;
    int x = wu_atoi(v); const char *q = v; while (wu_isdigit(*q) || *q == '.') q++;
    if (*q == '%') { *is_pct = 1; return base >= 0 ? x * base / 100 : -1; }
    return x;
}

/* ----------------------------- bordes, margenes y tamanos ----------------------------- */
static int collapse(int a, int b) { if (a >= 0 && b >= 0) return wmax(a, b); if (a < 0 && b < 0) return wmin(a, b); return a + b; }

static void edges(struct wbox *b, int cbw)
{
    const struct wstyle *s = b->st;
    b->pl = b->pr = b->pt = b->pb = b->bl = b->br = b->bt = b->bb = b->ml = b->mr = b->mt = b->mb = 0; b->mauto = 0;
    if (!s || (b->flags & BF_ANON)) { b->mt_eff = 0; b->mb_eff = 0; return; }
    b->pt = wmax(0, lenv(&s->padding[0], cbw, 0)); b->pr = wmax(0, lenv(&s->padding[1], cbw, 0));
    b->pb = wmax(0, lenv(&s->padding[2], cbw, 0)); b->pl = wmax(0, lenv(&s->padding[3], cbw, 0));
    b->bt = s->border_style[0] ? s->border_w[0] : 0; b->br = s->border_style[1] ? s->border_w[1] : 0;
    b->bb = s->border_style[2] ? s->border_w[2] : 0; b->bl = s->border_style[3] ? s->border_w[3] : 0;
    if (b->tab_sub == TS_CELL && b->parent && b->parent->parent && b->parent->parent->st && b->parent->parent->st->border_collapse) {
        if (b->col > 0) b->bl = 0;
        if (b->row > 0) b->bt = 0;
    }
    if (b->tab_sub == TS_CELL) { b->mt = b->mb = b->ml = b->mr = 0; }
    else {
        b->mt = lenv(&s->margin[0], cbw, 0); b->mr = lenv(&s->margin[1], cbw, 0); b->mb = lenv(&s->margin[2], cbw, 0); b->ml = lenv(&s->margin[3], cbw, 0);
        if (s->margin[3].kind == LK_AUTO) b->mauto |= 1;
        if (s->margin[1].kind == LK_AUTO) b->mauto |= 2;
        if (s->margin[0].kind == LK_AUTO) b->mauto |= 4;
        if (s->margin[2].kind == LK_AUTO) b->mauto |= 8;
    }
    b->mt_eff = b->mt; b->mb_eff = b->mb;
}
static int hextra(const struct wbox *b)   /* relleno + borde horizontal (sin margenes) */
{
    const struct wstyle *s = b->st; if (!s || (b->flags & BF_ANON)) return 0;
    int pl = wmax(0, lenv(&s->padding[3], 0, 0)), pr = wmax(0, lenv(&s->padding[1], 0, 0));
    return pl + pr + (s->border_style[3] ? s->border_w[3] : 0) + (s->border_style[1] ? s->border_w[1] : 0);
}
static int hmargin(const struct wbox *b)
{
    const struct wstyle *s = b->st; if (!s || (b->flags & BF_ANON) || b->tab_sub == TS_CELL) return 0;
    return lenv(&s->margin[3], 0, 0) + lenv(&s->margin[1], 0, 0);
}

static void repl_size(struct wlay *L, struct wbox *b, int cbw, int cbh, int *pw, int *ph)
{
    const struct wstyle *s = b->st; struct wnode *n = b->node; int intr = cbw < 0;
    int w = -1, h = -1, nw = 0, nh = 0, aspect = 0, ip;
    if (s->width.kind == LK_VAL && !(intr && s->width.pct)) w = lenv(&s->width, intr ? 0 : cbw, 0);
    else if (s->width.kind != LK_VAL) { int x = attr_px(n, "width", intr ? -1 : cbw, &ip); if (x >= 0) w = x; }
    if (s->height.kind == LK_VAL && !(s->height.pct && cbh < 0)) h = lenv(&s->height, cbh < 0 ? 0 : cbh, 0);
    else if (s->height.kind != LK_VAL) { int x = attr_px(n, "height", cbh, &ip); if (x >= 0) h = x; }
    /* SOMA_PARCHE_028_UI: con el ancho puesto por CSS y el alto automatico, los atributos width/height solo dan la proporcion
     * (como en los navegadores actuales): evita fotos estiradas con img{width:100%;height:auto} */
    int ar_w = 0, ar_h = 0;
    if (s->width.kind == LK_VAL && s->height.kind != LK_VAL && (b->ctl == CTL_IMG)) {
        int aw = attr_px(n, "width", -1, &ip), ah = attr_px(n, "height", -1, &ip);
        if (aw > 0 && ah > 0) { ar_w = aw; ar_h = ah; }
        h = -1;
    }
    if (s->box_sizing && w >= 0) w = wmax(0, w - hextra(b));
    if (s->box_sizing && h >= 0 && !wl181_off()) h = wmax(0, h - (b->pt + b->pb + b->bt + b->bb));   /* SOMA_188: border-box tambien en el alto */
    int px = s->font_px, lh = wc_line_px(s);
    switch (b->ctl) {
    case CTL_IMG: {
        struct wimginfo ii; const char *src = wl_img_src(n);
        /* SOMA_152B: si el CSS/atributos ya fijan ancho y alto, el tamano natural no cambia la maquetacion */
        if (src && img_info_need(L, src, &ii, w < 0 || h < 0) && ii.state == 2 && ii.w > 0 && ii.h > 0) { nw = ii.w; nh = ii.h; aspect = 1;
            if (ii.ratio_only && w < 0 && h < 0 && !intr && cbw > 0 && !wl181_off()) { w = cbw - hextra(b); if (w < 0) w = 0; }   /* SOMA_188: SVG solo con proporcion: ancho disponible */
        }
        else if (src && ii.state == 3 && n->tag != WT_SVG) { const char *alt = wd_attr(n, "alt"); nw = alt ? textw(s, alt, wu_strlen(alt)) + 4 : 0; nh = alt && *alt ? lh + 2 : 0; if (w < 0 && h < 0 && !alt) { nw = 16; nh = 16; } }
        else if (w < 0 && h < 0) { nw = 0; nh = 0; }
        break; }
    case CTL_TEXT: case CTL_PASSWORD: {
        const char *sz = wd_attr(n, "size"); int c = sz ? wclamp(wu_atoi(sz), 1, 200) : 20;
        if (wl181_off()) { nw = c * ((wf_cp_adv64('0', px, fontfl(s)) * 87 / 100 + 32) >> 6); nh = lh; break; }
        /* SOMA_188: como Chrome (LayoutTextControlSingleLine): size x ancho medio + (ancho maximo - ancho medio), medido con Liberation Sans */
        if (s->mono) nw = ((c - 1) * px * 602 + px * 1700 + 500) / 1000; else nw = ((c - 1) * px * 574 + px * 2190 + 500) / 1000;
        nh = lh; break; }
    case CTL_CHECK: case CTL_RADIO: nw = 13; nh = 13; break;
    case CTL_BTN_INPUT: { const char *t = wl_ctl_text(n); nw = textw(s, t, wu_strlen(t)); nh = lh; break; }
    case CTL_RANGE: nw = 129; nh = 16; break;
    case CTL_COLOR: nw = 38; nh = 15; break;
    case CTL_FILE: nw = 200; nh = lh; break;
    case CTL_SELECT: {
        struct wnode *o[256]; int c = 0, mx = 0; opt_collect(n, o, &c, 256);
        for (int i = 0; i < c; i++) { const char *l = opt_label(o[i]); int t = textw(s, l, wu_strlen(l)); if (t > mx) mx = t; }
        if (wl181_off()) { nw = (mx ? mx : 40) + 22; nh = lh; break; }
        nw = (mx ? mx : 40) + 18; nh = lh + 2; break; }   /* SOMA_188: sin relleno de UA (como Chrome): hueco de la flecha y margen interno */
    case CTL_TEXTAREA: {
        const char *cs = wd_attr(n, "cols"), *rs = wd_attr(n, "rows"); int cols = cs ? wclamp(wu_atoi(cs), 1, 300) : 20, rows = rs ? wclamp(wu_atoi(rs), 1, 100) : 2;
        if (wl181_off()) { nw = cols * ((wf_cp_adv64('0', px, fontfl(s)) * 87 / 100 + 32) >> 6); nh = rows * lh; break; }
        nw = cols * (s->mono ? (px * 602 + 500) / 1000 : (px * 574 + 500) / 1000) + 15; nh = rows * lh; break; }   /* SOMA_188: cols x ancho medio (entero) + barra de 15px (como Chrome) */
    default:
        nw = (n->tag == WT_AUDIO) ? 300 : 300; nh = (n->tag == WT_AUDIO) ? 32 : 150;
        if (n->tag == WT_EMBED || n->tag == WT_OBJECT) { nw = 0; nh = 0; }
        break;
    }
    if (s->ar_w && s->ar_h && !wl181_off() && b->ctl == CTL_IMG) {   /* SOMA_181: aspect-ratio explicito sobre la proporcion natural */
        if (w >= 0 && h < 0) { h = (int)((int64_t)w * s->ar_h / s->ar_w); }
        else if (h >= 0 && w < 0) { w = (int)((int64_t)h * s->ar_w / s->ar_h); }
        else if (w < 0 && h < 0 && nw > 0) { w = nw; h = (int)((int64_t)nw * s->ar_h / s->ar_w); }
    }
    if (w < 0 && h < 0) { w = nw; h = nh; }
    else if (w < 0) { w = (aspect && nh > 0) ? (int)((int64_t)h * nw / nh) : nw; }
    else if (h < 0) { h = (aspect && nw > 0) ? (int)((int64_t)w * nh / nw) : (ar_w > 0 ? (int)((int64_t)w * ar_h / ar_w) : nh); }
    int minw = s->min_w.kind == LK_VAL ? lenv(&s->min_w, intr ? 0 : cbw, 0) : 0;
    if (s->max_w.kind == LK_VAL && !(intr && s->max_w.pct)) {
        int mw = lenv(&s->max_w, intr ? 0 : cbw, 0);
        if (w > mw) { if (aspect && s->height.kind != LK_VAL && w > 0) h = (int)((int64_t)h * mw / w); w = mw; }
    }
    if (w < minw) { if (aspect && w > 0 && s->height.kind != LK_VAL) h = (int)((int64_t)h * minw / w); w = minw; }
    if (s->max_h.kind == LK_VAL && !s->max_h.pct) { int mh = s->max_h.px; if (h > mh) { if (aspect && s->width.kind != LK_VAL && h > 0) w = (int)((int64_t)w * mh / h); h = mh; } }
    *pw = wmax(0, w); *ph = wmax(0, h);
}

static void intrinsic(struct wlay *L, struct wbox *b);

static int hextra_half(const struct wstyle *s, int right)   /* margen+relleno+borde de un elemento en linea, lado izq(0)/der(1) */
{
    int i = right ? 1 : 3;
    return lenv(&s->margin[i], 0, 0) + wmax(0, lenv(&s->padding[i], 0, 0)) + (s->border_style[i] ? s->border_w[i] : 0);
}

static void intr_items(struct wlay *L, struct wbox *b)
{
    int cur = 0, mx = 0, mn = 0; int sp_pend = 0, spfr = 0;
    for (int i = 0; i < b->nitems; i++) {
        struct wii *it = &b->items[i];
        switch (it->type) {
        case II_TEXT: {
            const struct wstyle *st = it->st; int pre = st->white_space == WS_PRE || st->white_space == WS_PRE_WRAP, nw = st->white_space == WS_NOWRAP || st->white_space == WS_PRE;
            uint32_t pos = 0; struct { uint32_t s, n; int kind; } k; int wsw = spacew(st); int runw = 0;
            while (pos < it->len) {
                uint32_t s0 = pos; k.kind = 0;
                if (it->txt[pos] == '\n') { k.kind = 2; pos++; }
                else if (it->txt[pos] == ' ') { while (pos < it->len && it->txt[pos] == ' ' && (pre || pos == s0)) pos++; k.kind = 1; }
                else {
                    uint32_t adv; uint32_t cp = wu_utf8_next(it->txt + pos, &adv);
                    if (cp >= 0x2E80 && cp < 0xA000) pos += adv; else { while (pos < it->len && it->txt[pos] != ' ' && it->txt[pos] != '\n') { cp = wu_utf8_next(it->txt + pos, &adv); if (cp >= 0x2E80 && cp < 0xA000) break; pos += adv; } }
                }
                k.s = s0; k.n = pos - s0;
                if (k.kind == 2) { mx = wmax(mx, cur); cur = 0; sp_pend = 0; spfr = 0; mn = wmax(mn, runw); runw = 0; continue; }
                if (k.kind == 1) { if (pre) { cur += (int)k.n * wsw; runw += (int)k.n * wsw; } else if (cur > 0) sp_pend = sp_carry(&spfr, st); if (!pre && nw) runw += cur ? wsw : 0; continue; }
                int w = textw(st, it->txt + k.s, k.n);
                if (sp_pend && cur > 0) { cur += sp_pend; } sp_pend = 0;
                cur += w;
                if (nw) runw = cur; else mn = wmax(mn, w);
            }
            mn = wmax(mn, runw);
            break; }
        case II_OPEN: { cur += hextra_half(it->st, 0); break; }
        case II_CLOSE: { cur += hextra_half(it->st, 1); break; }
        case II_BR: mx = wmax(mx, cur); cur = 0; sp_pend = 0; spfr = 0; break;
        case II_ATOM: case II_FLOAT: {
            struct wbox *x = it->box; if (!x) break; intrinsic(L, x);
            int ex = hextra(x) + hmargin(x); int xo = x->imax + ex, xi = x->imin + ex;
            if (it->type == II_FLOAT) { mx = wmax(mx, xo); mn = wmax(mn, xi); }
            else { if (sp_pend && cur > 0) cur += sp_pend; sp_pend = 0; cur += xo; mn = wmax(mn, xi); }
            break; }
        default: break;
        }
    }
    mx = wmax(mx, cur); b->imax = mx; b->imin = mn; if (b->imin > b->imax) b->imax = b->imin;
}

/* ----------------------------- tablas: columnas ----------------------------- */
struct wtcol { int mn, mx, fixed, pct; };
#define MAXCOLS 64
static int tbl_spacing(const struct wbox *t) { return t->st->border_collapse ? 0 : t->st->spacing_h; }

static int table_cols(struct wlay *L, struct wbox *t, int cw, struct wtcol *cols)
{
    int occ[MAXCOLS]; memset(occ, 0, sizeof occ); int ncols = 0, rowi = 0;
    memset(cols, 0, sizeof(struct wtcol) * MAXCOLS);
    for (struct wbox *r = t->first; r; r = r->next) {
        if (r->kind != BK_TROW) continue;
        int col = 0;
        for (struct wbox *c = r->first; c; c = c->next) {
            while (col < MAXCOLS && occ[col] > 0) col++;
            if (col >= MAXCOLS) break;
            c->col = col; c->row = rowi;
            for (int k = 0; k < c->colspan && col + k < MAXCOLS; k++) occ[col + k] = c->rowspan;
            col += c->colspan; if (col > ncols) ncols = col;
        }
        for (int i = 0; i < MAXCOLS; i++) if (occ[i] > 0) occ[i]--;
        rowi++;
    }
    if (ncols > MAXCOLS) ncols = MAXCOLS;
    int sp = tbl_spacing(t);
    for (int pass = 0; pass < 2; pass++)
        for (struct wbox *r = t->first; r; r = r->next) {
            if (r->kind != BK_TROW) continue;
            for (struct wbox *c = r->first; c; c = c->next) {
                if ((pass == 0) != (c->colspan == 1)) continue;
                if (c->col >= MAXCOLS) continue;
                edges(c, cw); intrinsic(L, c);
                int ex = c->pl + c->pr + c->bl + c->br; int mn = c->imin + ex, mx = c->imax + ex;
                const struct wstyle *s = c->st; int fixedw = -1, pct = 0;
                if (s->width.kind == LK_VAL) { if (s->width.pct && !s->width.px) pct = s->width.pct; else fixedw = s->width.px + (s->box_sizing ? 0 : ex); }
                if (fixedw >= 0) { if (fixedw < mn) fixedw = mn; mx = fixedw; }
                if (c->colspan == 1) {
                    struct wtcol *k = &cols[c->col];
                    if (mn > k->mn) k->mn = mn;
                    if (mx > k->mx) k->mx = mx;
                    if (fixedw >= 0 && fixedw > k->fixed) k->fixed = fixedw;
                    if (pct > k->pct) k->pct = pct;
                } else {
                    int span = wmin(c->colspan, ncols - c->col); if (span < 1) continue;
                    int smn = 0, smx = 0; for (int k = 0; k < span; k++) { smn += cols[c->col + k].mn; smx += cols[c->col + k].mx; }
                    smn += sp * (span - 1); smx += sp * (span - 1);
                    if (mn > smn) { int d = mn - smn; for (int k = 0; k < span; k++) cols[c->col + k].mn += d / span + (k < d % span); }
                    if (mx > smx) { int d = mx - smx; for (int k = 0; k < span; k++) cols[c->col + k].mx += d / span + (k < d % span); }
                    if (pct && span) for (int k = 0; k < span; k++) if (!cols[c->col + k].pct) cols[c->col + k].pct = pct / span;
                }
            }
        }
    for (int i = 0; i < ncols; i++) { if (cols[i].mx < cols[i].mn) cols[i].mx = cols[i].mn; if (cols[i].fixed && cols[i].fixed < cols[i].mn) cols[i].fixed = cols[i].mn; }
    return ncols;
}

/* ----------------------------- tamanos intrinsecos ----------------------------- */
static void outer_mm(const struct wbox *c, int *omin, int *omax)
{
    const struct wstyle *s = c->st; int ex = hextra(c) + hmargin(c);
    *omin = c->imin + ex; *omax = c->imax + ex;
    if (s && !(c->flags & BF_ANON) && s->width.kind == LK_VAL && !s->width.pct) {
        int fw = s->width.px - (s->box_sizing ? hextra(c) : 0); if (fw < 0) fw = 0;
        *omin = *omax = fw + ex;
    }
    if (s && !(c->flags & BF_ANON) && s->max_w.kind == LK_VAL && !s->max_w.pct) { int mw = s->max_w.px - (s->box_sizing ? hextra(c) : 0) + ex; if (*omax > mw) *omax = mw; if (*omin > mw) *omin = mw; }
}

static void intrinsic(struct wlay *L, struct wbox *b)
{
    if (b->iok) return;
    b->iok = 1;
    if (web_stack_left() < 5000) return;
    const struct wstyle *s = b->st; int mx = 0, mn = 0;
    switch (b->kind) {
    case BK_REPL: { int w, h; repl_size(L, b, -1, -1, &w, &h); b->imin = b->imax = w;
        /* SOMA_PARCHE_028_UI: una imagen con ancho o tope en porcentaje (width:100%, max-width:100%) se encoge con su contenedor */
        if (b->ctl == CTL_IMG && s && ((s->width.kind == LK_VAL && s->width.pct) || (s->max_w.kind == LK_VAL && s->max_w.pct))) b->imin = 0;
        return; }
    case BK_TABLE: {
        struct wtcol cols[MAXCOLS]; int n = table_cols(L, b, 0, cols); int sp = tbl_spacing(b);
        for (int i = 0; i < n; i++) { mx += cols[i].mx; mn += cols[i].mn; }
        mx += sp * (n + 1); mn += sp * (n + 1);
        for (struct wbox *c = b->first; c; c = c->next) if (c->tab_sub == TS_CAPTION) { intrinsic(L, c); if (c->imin > mn) mn = c->imin; if (c->imax > mx) mx = c->imax; }
        b->imin = mn; b->imax = wmax(mx, mn); return; }
    case BK_BLOCK:
        if (b->items) { intr_items(L, b); return; }
        for (struct wbox *c = b->first; c; c = c->next) {
            if (c->flags & BF_ABS) continue;
            intrinsic(L, c); int a, z; outer_mm(c, &a, &z); mn = wmax(mn, a); mx = wmax(mx, z);
        }
        break;
    case BK_FLEX: {
        int row = s->flex_dir < 2, gap = lenv(&s->gap_c, 0, 0), cnt = 0, sm = 0, sx = 0;
        for (struct wbox *c = b->first; c; c = c->next) {
            if (c->flags & BF_ABS) continue;
            intrinsic(L, c); int a, z; outer_mm(c, &a, &z); cnt++;
            sm += a; sx += z; mn = wmax(mn, a); mx = wmax(mx, z);
        }
        if (row) { mx = sx + gap * (cnt > 0 ? cnt - 1 : 0); if (!s->flex_wrap) mn = sm + gap * (cnt > 0 ? cnt - 1 : 0); }
        break; }
    case BK_GRID: {
        int nc = s->grid_cols > 0 ? s->grid_cols : 1, gap = lenv(&s->gap_c, 0, 0); int cmn[GRID_MAX_TRACKS], cmx[GRID_MAX_TRACKS]; memset(cmn, 0, sizeof cmn); memset(cmx, 0, sizeof cmx);
        int idx = 0;
        for (struct wbox *c = b->first; c; c = c->next) {
            if (c->flags & BF_ABS) continue;
            intrinsic(L, c); int a, z; outer_mm(c, &a, &z); int col = idx % nc; idx++;
            if (col < GRID_MAX_TRACKS) { cmn[col] = wmax(cmn[col], a); cmx[col] = wmax(cmx[col], z); }
        }
        for (int i = 0; i < nc && i < GRID_MAX_TRACKS; i++) {
            const struct wlen *t = &s->grid_tracks[i];
            if (t->kind == LK_VAL && !t->pct) { mn += t->px; mx += t->px; } else { mn += cmn[i]; mx += cmx[i]; }
        }
        mx += gap * (nc - 1); mn += gap * (nc - 1);
        break; }
    default: break;
    }
    b->imin = mn; b->imax = wmax(mx, mn);
}

static void first_margin(struct wbox *b, int cw, int depth)
{
    b->top_abs = 0; b->mt_eff = b->mt;
    if (b->kind != BK_BLOCK || b->items || (b->flags & BF_BFC) || b->pt || b->bt || depth > 20 || b->tab_sub) return;
    for (struct wbox *c = b->first; c; c = c->next) {
        if (c->flags & (BF_ABS | BF_FLOAT)) continue;
        if (c->st && c->st->clear) return;
        edges(c, cw); first_margin(c, cw, depth + 1);
        b->mt_eff = collapse(b->mt, c->mt_eff); b->top_abs = 1; return;
    }
}

static void size_block_w(struct wlay *L, struct wbox *b, int cbw, int cbh, int fit)
{
    const struct wstyle *s = b->st;
    edges(b, cbw);
    int ex = b->pl + b->pr + b->bl + b->br, content;
    int unconstrained = 0;
    if (b->kind == BK_REPL) {
        int w, h; repl_size(L, b, cbw, cbh, &w, &h); content = w; b->cont_h = h; unconstrained = 0;
    } else {
        int minw = s->min_w.kind == LK_VAL ? lenv(&s->min_w, cbw, 0) : 0, maxw = s->max_w.kind == LK_VAL ? lenv(&s->max_w, cbw, BIG) : BIG;
        if (s->box_sizing) { minw = wmax(0, minw - ex); if (maxw != BIG) maxw = wmax(0, maxw - ex); }
        if (s->width.kind == LK_VAL && !(b->flags & BF_ANON)) { content = lenv(&s->width, cbw, 0); if (s->box_sizing) content = wmax(0, content - ex); }
        else if (fit || b->kind == BK_TABLE || ((s->width.kind == LK_FIT || s->width.kind == LK_MINC) && !(b->flags & BF_ANON))) {
            intrinsic(L, b); int avail = wmax(0, cbw - b->ml - b->mr - ex);
            content = s->width.kind == LK_MINC ? b->imin : wmin(wmax(b->imin, avail), b->imax); unconstrained = 1;
        } else { content = wmax(0, cbw - b->ml - b->mr - ex); unconstrained = 2; }
        if (content > maxw) content = maxw;
        if (content < minw) content = minw;
    }
    b->w = content + ex;
    if (unconstrained != 2 && b->tab_sub != TS_CELL) {
        int free_ = cbw - b->w - b->ml - b->mr;
        if (free_ > 0) {
            if ((b->mauto & 3) == 3) { b->ml = free_ / 2; b->mr = free_ - b->ml; }
            else if (b->mauto & 1) b->ml = free_; else if (b->mauto & 2) b->mr = free_;
        }
    } else if (unconstrained == 2 && cbw - b->w - b->ml - b->mr > 0 && (b->mauto & 3) && s->max_w.kind == LK_VAL) {
        int free_ = cbw - b->w - b->ml - b->mr;
        if ((b->mauto & 3) == 3) { b->ml = free_ / 2; b->mr = free_ - b->ml; } else if (b->mauto & 1) b->ml = free_; else b->mr = free_;
    }
    b->def_h = -1; b->cbh = cbh;
    if (b->kind != BK_REPL && s->height.kind == LK_VAL && !(s->height.pct && cbh < 0) && !(b->flags & BF_ANON)) {
        int hv = lenv(&s->height, cbh < 0 ? 0 : cbh, 0); if (s->box_sizing) hv -= b->pt + b->pb + b->bt + b->bb;
        b->def_h = wmax(0, hv);
    }
    if (b->kind != BK_REPL && b->def_h < 0 && s->ar_w && s->ar_h && s->height.kind != LK_VAL && !(b->flags & BF_ANON) && !wl181_off()) {   /* SOMA_181: aspect-ratio */
        int64_t hh = s->box_sizing ? (int64_t)b->w * s->ar_h / s->ar_w - (b->pt + b->pb + b->bt + b->bb) : (int64_t)(b->w - ex) * s->ar_h / s->ar_w;
        b->def_h = (int)wmax(0, (int)(hh > 100000 ? 100000 : hh));
    }
    b->rdx = b->rdy = 0;
    if (b->flags & BF_REL) {
        if (s->position == P_RELATIVE) {
            if (s->left.kind == LK_VAL) b->rdx = lenv(&s->left, cbw, 0); else if (s->right.kind == LK_VAL) b->rdx = -lenv(&s->right, cbw, 0);
            if (s->top.kind == LK_VAL && !s->top.pct) b->rdy = s->top.px; else if (s->bottom.kind == LK_VAL && !s->bottom.pct) b->rdy = -s->bottom.px;
        }
    }
    first_margin(b, wmax(0, b->w - ex), 0);
}

/* ----------------------------- maquetacion en linea ----------------------------- */
static void lay_content(struct wlay *L, struct wbox *b, struct wctx *cx);
static void lay_bfc(struct wlay *L, struct wbox *b)
{
    b->flags |= BF_BFC; if (b->top_abs) { b->top_abs = 0; b->mt_eff = b->mt; }
    struct wfl f; memset(&f, 0, sizeof f); struct wctx cx = { &f, 0, 0 };
    lay_content(L, b, &cx);
}
static void lay_child(struct wlay *L, struct wbox *c, struct wctx *cx)
{
    wu_tick();
    if (c->flags & BF_BFC) lay_bfc(L, c);
    else { struct wctx n = { cx->fl, cx->ox + c->x, cx->oy + c->y }; lay_content(L, c, &n); }
}

static void place_float(struct wctx *cx, struct wlay *L, struct wbox *x, int y_local, int left0, int right0)
{
    struct wfl *f = cx->fl; int wo = x->ml + x->w + x->mr, ho = x->mt + x->h + x->mb, isleft = x->st->floating == FL_LEFT;
    int y = cx->oy + y_local; if (y < f->lasty) y = f->lasty;
    if (x->st->clear) { int side = x->st->clear; int cl = fl_clear_y(f, side == 3 ? 3 : (side == 1 ? 1 : (side == 2 ? 2 : 3))); if (cl != -BIG && cl > y) y = cl; }
    int cl = cx->ox + left0, cr = cx->ox + right0, l = cl, r = cr;
    for (int guard = 0; guard < 400; guard++) {
        fl_band(f, y, ho, &l, &r);
        if (l < cl) l = cl;
        if (r > cr) r = cr;
        if (r - l >= wo || (l == cl && r == cr)) break;
        int nb = fl_next_bottom(f, y); if (nb == BIG) break; y = nb;
    }
    int fx = isleft ? l : r - wo;
    x->x = fx + x->ml - cx->ox; x->y = y + x->mt - cx->oy;
    if (ho > 0) fl_push(L, f, isleft ? -BIG : fx, y, isleft ? fx + wo : BIG, y + ho);
    f->lasty = y;
}

struct wlat { struct wbox *bx; int asc, dsc, va, sh; };
struct went { const struct wstyle *st; struct wnode *node; int x0, first, visual; };
struct wldeco { int x0, x1, depth; const struct wstyle *st; struct wnode *node; uint8_t first, last; };
struct wil {
    struct wlay *L; struct wbox *b; struct wctx *cx;
    int cw, left0, right0, ly, lleft, lright, lx, lstart_x, lhas, pend_sp, first_line, spfr;
    int asc, dsc, tbh, last_base, indent;
    struct wfrag *fr; int nfr, capfr, fs;
    struct wlat *la; int nla, capla;
    struct wldeco *ld; int nld, capld;
    struct wdeco *dc; int ndc, capdc;
    struct went stk[32]; int sp;
    struct wbox *pf[24]; int npf;
    int seg_open; uint32_t seg_s, seg_e; int seg_w, seg_x; const struct wii *seg_it;
};
static int grow(void **p, int *cap, int n, size_t sz)
{
    if (n < *cap) return 1;
    int nc = *cap ? *cap * 2 : 32; void *np = kmalloc((size_t)nc * sz); if (!np) return 0;
    if (*p) { memcpy(np, *p, (size_t)n * sz); kfree(*p); }
    *p = np; *cap = nc; return 1;
}
static void txt_metrics(const struct wstyle *st, int *a, int *d)
{
    int px = st->font_px, fa = wf_ascent(px), fd = wf_descent(px), lh = wc_line_px(st);
    int lead = lh - (fa + fd); *a = fa + (lead >> 1); *d = lh - *a;
}
static int va_shift(int va, const struct wstyle *vs, int asc, int dsc)
{
    switch (va) {
    case VA_SUB: return -(vs->font_px / 5);
    case VA_SUPER: return vs->font_px * 2 / 5 + 1;
    case VA_MIDDLE: { int xh = vs->font_px * 5 / 9; return xh / 2 - (asc - dsc) / 2; }
    default: return 0;
    }
}
static void eff_va(struct wil *S, int *va, const struct wstyle **vs)
{
    for (int i = S->sp - 1; i >= 0; i--) if (S->stk[i].st->vertical_align) { *va = S->stk[i].st->vertical_align; *vs = S->stk[i].st; return; }
    *va = 0; *vs = S->b->st;
}
static int va_is_tb(int va) { return va == VA_TOP || va == VA_BOTTOM || va == VA_TEXT_TOP || va == VA_TEXT_BOTTOM; }
static void il_account(struct wil *S, int asc, int dsc, int va, int sh)
{
    if (va_is_tb(va)) { if (asc + dsc > S->tbh) S->tbh = asc + dsc; }
    else { if (asc + sh > S->asc) S->asc = asc + sh; if (dsc - sh > S->dsc) S->dsc = dsc - sh; }
}
static void il_start_line(struct wil *S)
{
    int l, r; fl_band(S->cx->fl, S->cx->oy + S->ly, 1, &l, &r);
    S->lleft = S->left0; S->lright = S->right0;
    if (l != -BIG) S->lleft = wmax(S->lleft, l - S->cx->ox);
    if (r != BIG) S->lright = wmin(S->lright, r - S->cx->ox);
    if (S->lright < S->lleft) S->lright = S->lleft;
    if (S->first_line) S->lleft += S->indent;
    S->lx = S->lleft; S->lstart_x = S->lx; S->lhas = 0; S->pend_sp = 0; S->spfr = 0; S->asc = S->dsc = S->tbh = 0;
    S->fs = S->nfr; S->nla = 0; S->nld = 0;
    for (int i = 0; i < S->sp; i++) { S->stk[i].x0 = S->lx; }
}
static void il_place_pending(struct wil *S)
{
    for (int i = 0; i < S->npf; i++) place_float(S->cx, S->L, S->pf[i], S->ly, S->left0, S->right0);
    S->npf = 0;
}
static int deco_depth_cmp(const struct wldeco *a, const struct wldeco *b) { return a->depth - b->depth; }

static void il_finish_line(struct wil *S, int forced, const struct wstyle *st_forced)
{
    if (!S->lhas && !forced) return;
    struct wbox *b = S->b;
    /* tramos de elementos en linea aun abiertos */
    for (int i = 0; i < S->sp; i++) {
        struct went *e = &S->stk[i];
        if (e->visual && S->lx > e->x0 && grow((void **)&S->ld, &S->capld, S->nld, sizeof(*S->ld))) {
            struct wldeco *d = &S->ld[S->nld++]; d->x0 = e->x0; d->x1 = S->lx; d->depth = i; d->st = e->st; d->node = e->node; d->first = (uint8_t)e->first; d->last = 0;
        }
        e->first = 0;
    }
    const struct wstyle *ss = st_forced ? st_forced : b->st; int sa, sd; txt_metrics(b->st, &sa, &sd);
    if (st_forced && st_forced->font_px) { int fa, fd; txt_metrics(st_forced, &fa, &fd); if (fa > sa) sa = fa; if (fd > sd) sd = fd; }
    (void)ss;
    if (sa > S->asc) S->asc = sa;
    if (sd > S->dsc) S->dsc = sd;
    int lh = S->asc + S->dsc;
    if (S->tbh > lh) { S->dsc += S->tbh - lh; lh = S->tbh; }
    int base = S->ly + S->asc;
    for (int i = S->fs; i < S->nfr; i++) {
        struct wfrag *f = &S->fr[i];
        if (f->va == VA_TOP || f->va == VA_TEXT_TOP) f->y = S->ly + f->asc;
        else if (f->va == VA_BOTTOM || f->va == VA_TEXT_BOTTOM) f->y = S->ly + lh - f->dsc;
        else f->y = base - f->sh;
    }
    for (int i = 0; i < S->nla; i++) {
        struct wlat *a = &S->la[i]; struct wbox *x = a->bx; int yb;
        if (a->va == VA_TOP || a->va == VA_TEXT_TOP) yb = S->ly + a->asc;
        else if (a->va == VA_BOTTOM || a->va == VA_TEXT_BOTTOM) yb = S->ly + lh - a->dsc;
        else yb = base - a->sh;
        x->y = yb - a->asc + x->mt;
    }
    int off = 0, ta = b->st->text_align, used = S->lx - S->lstart_x, avail = S->lright - S->lstart_x;
    if (ta == TA_CENTER) off = (avail - used) / 2; else if (ta == TA_RIGHT) off = avail - used;
    if (off < 0) off = 0;
    if (off) {
        for (int i = S->fs; i < S->nfr; i++) S->fr[i].x += off;
        for (int i = 0; i < S->nla; i++) S->la[i].bx->x += off;
        for (int i = 0; i < S->nld; i++) { S->ld[i].x0 += off; S->ld[i].x1 += off; }
    }
    /* decoraciones: del exterior al interior */
    for (int i = 1; i < S->nld; i++) { struct wldeco t = S->ld[i]; int j = i - 1; while (j >= 0 && deco_depth_cmp(&S->ld[j], &t) > 0) { S->ld[j + 1] = S->ld[j]; j--; } S->ld[j + 1] = t; }
    for (int i = 0; i < S->nld; i++) {
        struct wldeco *d = &S->ld[i]; if (!grow((void **)&S->dc, &S->capdc, S->ndc, sizeof(*S->dc))) break;
        struct wdeco *o = &S->dc[S->ndc++]; int px = d->st->font_px;
        o->x0 = d->x0; o->x1 = d->x1; o->y0 = base - wf_ascent(px) - wmax(0, lenv(&d->st->padding[0], 0, 0)) - (d->st->border_style[0] ? d->st->border_w[0] : 0);
        o->y1 = base + wf_descent(px) + wmax(0, lenv(&d->st->padding[2], 0, 0)) + (d->st->border_style[2] ? d->st->border_w[2] : 0);
        o->st = d->st; o->node = d->node; o->link = 0; o->first = d->first; o->last = d->last;
    }
    S->last_base = base;
    S->ly += lh; S->first_line = 0;
    il_place_pending(S);
    il_start_line(S);
    S->first_line = 0;
}

static void il_emit_frag(struct wil *S, const struct wii *it, uint32_t s, uint32_t e, int w, int x)
{
    if (e <= s || !grow((void **)&S->fr, &S->capfr, S->nfr, sizeof(*S->fr))) return;
    struct wfrag *f = &S->fr[S->nfr++]; memset(f, 0, sizeof *f);
    f->x = x; f->w = w; f->txt = it->txt + s; f->len = (uint16_t)wmin((int)(e - s), 65535); f->st = it->st; f->node = it->node; f->link = it->link;
    int a, d; txt_metrics(it->st, &a, &d); f->asc = (int16_t)a; f->dsc = (int16_t)d;
    int va; const struct wstyle *vs; eff_va(S, &va, &vs);
    f->va = (uint8_t)va; f->sh = (int16_t)(va_is_tb(va) ? 0 : va_shift(va, vs, a, d));
    il_account(S, a, d, va, f->sh);
}
static void il_flush_seg(struct wil *S)
{
    if (!S->seg_open) return;
    S->seg_open = 0;
    il_emit_frag(S, S->seg_it, S->seg_s, S->seg_e, S->seg_w, S->seg_x);
}
static int il_nowrap(struct wil *S) { int ws = (S->sp ? S->stk[S->sp - 1].st : S->b->st)->white_space; return ws == WS_NOWRAP || ws == WS_PRE; }
static int il_avoid_floats(struct wil *S)
{
    if (S->lleft == S->left0 + (S->first_line ? S->indent : 0) && S->lright == S->right0) return 0;
    int nb = fl_next_bottom(S->cx->fl, S->cx->oy + S->ly); if (nb == BIG) return 0;
    S->ly = nb - S->cx->oy; il_start_line(S); return 1;
}

static void il_break_word(struct wil *S, const struct wii *it, uint32_t s, uint32_t n)
{
    const struct wstyle *st = it->st; uint32_t p = s, e = s + n, seg0 = s; int acc = 0, fl = fontfl(st);
    while (p < e) {
        uint32_t adv; uint32_t cp = wu_utf8_next(it->txt + p, &adv); int cwid = ((wf_cp_adv64(cp, st->font_px, fl) + 32) >> 6) + st->letter_sp;
        if (acc > 0 && S->lx + acc + cwid > S->lright) {
            il_emit_frag(S, it, seg0, p, acc, S->lx); S->lx += acc; S->lhas = 1; il_finish_line(S, 0, st); seg0 = p; acc = 0;
        }
        acc += cwid; p += adv;
    }
    if (acc > 0) { il_emit_frag(S, it, seg0, e, acc, S->lx); S->lx += acc; S->lhas = 1; }
    S->pend_sp = 0;
}

static void il_word(struct wil *S, const struct wii *it, uint32_t s, uint32_t n, int w)
{
    int nowrap = il_nowrap(S);
    for (int guard = 0; guard < 2000; guard++) {
        int sp = S->lhas ? S->pend_sp : 0;
        if (nowrap || S->lx + sp + w <= S->lright) {
            if (!S->seg_open) { S->lx += sp; S->seg_open = 1; S->seg_s = s; S->seg_x = S->lx; S->seg_w = 0; S->seg_it = it; }
            else { S->seg_w += sp; S->lx += sp; }
            S->seg_e = s + n; S->seg_w += w; S->lx += w; S->lhas = 1; S->pend_sp = 0; return;
        }
        if (S->lhas) { il_flush_seg(S); il_finish_line(S, 0, it->st); continue; }
        if (il_avoid_floats(S)) continue;
        if (w > S->right0 - S->left0) { il_break_word(S, it, s, n); return; }
        S->seg_open = 1; S->seg_s = s; S->seg_x = S->lx; S->seg_w = 0; S->seg_it = it; S->seg_e = s + n; S->seg_w = w; S->lx += w; S->lhas = 1; S->pend_sp = 0; return;
    }
}

static void il_text(struct wil *S, const struct wii *it)
{
    const struct wstyle *st = it->st; int ws = st->white_space, pre = (ws == WS_PRE || ws == WS_PRE_WRAP), keepnl = (pre || ws == WS_PRE_LINE), breakall = st->word_break == 1;
    int sw = spacew(st); uint32_t pos = 0;
    S->seg_open = 0;
    while (pos < it->len) {
        uint32_t s0 = pos; int kind = 0; char ch = it->txt[pos];
        if (ch == '\n') { kind = 2; pos++; }
        else if (ch == ' ') { while (pos < it->len && it->txt[pos] == ' ' && (pre || pos == s0)) pos++; kind = 1; }
        else {
            uint32_t adv; uint32_t cp = wu_utf8_next(it->txt + pos, &adv);
            if (breakall || (cp >= 0x2E80 && cp < 0xA000) || (cp >= 0xAC00 && cp < 0xD7B0) || (cp >= 0xFF00 && cp < 0xFFF0)) pos += adv;
            else {
                while (pos < it->len && it->txt[pos] != ' ' && it->txt[pos] != '\n') {
                    cp = wu_utf8_next(it->txt + pos, &adv);
                    if (cp >= 0x2E80 && cp < 0xA000) break;
                    pos += adv;
                    if (cp == '-' && pos > s0 + 1 && pos < it->len && wu_isalnum((unsigned char)it->txt[pos]) && wu_isalnum((unsigned char)it->txt[pos - 2])) break;
                }
            }
        }
        uint32_t n = pos - s0;
        if (kind == 2) { if (keepnl) { il_flush_seg(S); il_finish_line(S, 1, st); } else if (S->lhas) S->pend_sp = sp_carry(&S->spfr, st); continue; }
        if (kind == 1) {
            if (pre) { il_word(S, it, s0, n, (int)n * sw); }
            else if (S->lhas) S->pend_sp = sp_carry(&S->spfr, st);
            continue;
        }
        il_word(S, it, s0, n, textw(st, it->txt + s0, n));
    }
    il_flush_seg(S);
}

static void il_atom(struct wil *S, const struct wii *it)
{
    struct wbox *x = it->box; struct wlay *L = S->L; if (!x) return;
    size_block_w(L, x, S->cw, -1, 1);
    if (x->kind == BK_REPL) {
        x->h = x->cont_h + x->pt + x->pb + x->bt + x->bb;
        if (x->ctl == CTL_TEXT || x->ctl == CTL_PASSWORD || x->ctl == CTL_BTN_INPUT || x->ctl == CTL_SELECT || x->ctl == CTL_TEXTAREA) {
            int a, d; txt_metrics(x->st, &a, &d); x->baseline = x->bt + x->pt + a + (x->cont_h - (a + d)) / 2;
        } else x->baseline = x->h;
    } else lay_bfc(L, x);
    int outer = x->ml + x->w + x->mr, nowrap = il_nowrap(S);
    for (int guard = 0; guard < 2000; guard++) {
        int sp = S->lhas ? S->pend_sp : 0;
        if (nowrap || S->lx + sp + outer <= S->lright) break;
        if (S->lhas) { il_finish_line(S, 0, it->st); continue; }
        if (il_avoid_floats(S)) continue;
        break;
    }
    int sp = S->lhas ? S->pend_sp : 0;
    x->x = S->lx + sp + x->ml; S->lx += sp + outer; S->lhas = 1; S->pend_sp = 0;
    int bl = x->baseline >= 0 ? x->baseline : x->h;
    int va; const struct wstyle *vs; eff_va(S, &va, &vs);
    if (x->st->vertical_align) { va = x->st->vertical_align; vs = x->st; }
    int asc = bl + x->mt, dsc = x->h - bl + x->mb, sh = va_is_tb(va) ? 0 : va_shift(va, vs, asc, dsc);
    if (grow((void **)&S->la, &S->capla, S->nla, sizeof(*S->la))) { struct wlat *a = &S->la[S->nla++]; a->bx = x; a->asc = asc; a->dsc = dsc; a->va = va; a->sh = sh; }
    il_account(S, asc, dsc, va, sh);
}

static void il_float(struct wil *S, const struct wii *it)
{
    struct wbox *x = it->box; if (!x) return;
    size_block_w(S->L, x, S->cw, -1, 1);
    if (x->kind == BK_REPL) x->h = x->cont_h + x->pt + x->pb + x->bt + x->bb; else lay_bfc(S->L, x);
    if (S->lhas && S->npf < 24) { S->pf[S->npf++] = x; return; }
    place_float(S->cx, S->L, x, S->ly, S->left0, S->right0);
    if (!S->lhas) il_start_line(S);
}

static int has_visual(const struct wstyle *s)
{
    if (s->bg_color || s->has_grad || s->bg_image) return 1;
    for (int i = 0; i < 4; i++) if (s->border_style[i] && s->border_w[i]) return 1;
    return 0;
}

static int lay_inline(struct wlay *L, struct wbox *b, struct wctx *cx, int cw)
{
    struct wil *S = (struct wil *)kmalloc(sizeof(*S)); if (!S) return 0;
    memset(S, 0, sizeof *S); S->L = L; S->b = b; S->cx = cx; S->cw = cw;
    S->left0 = b->pl + b->bl; S->right0 = S->left0 + cw; S->ly = b->pt + b->bt; S->first_line = 1;
    S->indent = lenv(&b->st->text_indent, cw, 0);
    il_start_line(S);
    for (int i = 0; i < b->nitems; i++) {
        const struct wii *it = &b->items[i];
        if (web_stack_left() < 3000) break;
        switch (it->type) {
        case II_TEXT: il_text(S, it); break;
        case II_OPEN: {
            const struct wstyle *s = it->st; int ml = lenv(&s->margin[3], cw, 0), pl = wmax(0, lenv(&s->padding[3], cw, 0)), bl = s->border_style[3] ? s->border_w[3] : 0;
            if (S->sp < 32) {
                if ((ml + pl + bl) > 0 && S->lhas) { S->lx += S->pend_sp; S->pend_sp = 0; }
                struct went *e = &S->stk[S->sp++]; e->st = s; e->node = it->node; e->x0 = S->lx + ml; e->first = 1; e->visual = has_visual(s);
                S->lx += ml + pl + bl;
            }
            break; }
        case II_CLOSE: {
            if (S->sp <= 0) break;
            struct went *e = &S->stk[--S->sp]; const struct wstyle *s = e->st;
            int pr = wmax(0, lenv(&s->padding[1], cw, 0)), br = s->border_style[1] ? s->border_w[1] : 0, mr = lenv(&s->margin[1], cw, 0);
            S->lx += pr + br;
            if (e->visual && S->lx > e->x0 && grow((void **)&S->ld, &S->capld, S->nld, sizeof(*S->ld))) {
                struct wldeco *d = &S->ld[S->nld++]; d->x0 = e->x0; d->x1 = S->lx; d->depth = S->sp; d->st = s; d->node = e->node; d->first = (uint8_t)e->first; d->last = 1;
            }
            S->lx += mr; break; }
        case II_BR: il_flush_seg(S); il_finish_line(S, 1, it->st); break;
        case II_ATOM: il_atom(S, it); break;
        case II_FLOAT: il_float(S, it); break;
        case II_ABS: if (it->box) { it->box->sx = S->lx + (S->lhas ? S->pend_sp : 0); it->box->sy = S->ly; } break;
        default: break;
        }
    }
    il_flush_seg(S);
    if (S->lhas) il_finish_line(S, 0, b->st);
    il_place_pending(S);
    int ch = S->ly - (b->pt + b->bt);
    b->baseline = S->last_base ? S->last_base : -1;
    if (S->nfr) { b->frags = (struct wfrag *)wa_alloc(&L->ar, S->nfr * sizeof(*b->frags)); if (b->frags) { memcpy(b->frags, S->fr, S->nfr * sizeof(*b->frags)); b->nfrags = S->nfr; } }
    if (S->ndc) { b->decos = (struct wdeco *)wa_alloc(&L->ar, S->ndc * sizeof(*b->decos)); if (b->decos) { memcpy(b->decos, S->dc, S->ndc * sizeof(*b->decos)); b->ndecos = S->ndc; } }
    if (S->fr) kfree(S->fr);
    if (S->la) kfree(S->la);
    if (S->ld) kfree(S->ld);
    if (S->dc) kfree(S->dc);
    kfree(S);
    return ch;
}

/* ----------------------------- bloques ----------------------------- */
static int lay_blocks(struct wlay *L, struct wbox *b, struct wctx *cx, int cw)
{
    int left = b->pl + b->bl, top = b->pt + b->bt, y = top, pend = 0, first = 1;
    int cbh = b->def_h;
    for (struct wbox *c = b->first; c; c = c->next) {
        if (web_stack_left() < 3000) break;
        if (c->flags & BF_ABS) { c->sx = left; c->sy = y + (pend > 0 ? pend : 0); continue; }
        if (c->flags & BF_FLOAT) {
            size_block_w(L, c, cw, cbh, 1); lay_bfc(L, c);
            place_float(cx, L, c, y + (pend > 0 ? pend : 0), left, left + cw); continue;
        }
        if (c->kind == BK_TROW) continue;
        size_block_w(L, c, cw, cbh, 0);
        int mt = (first && b->top_abs) ? 0 : c->mt_eff;
        int cy = y + collapse(pend, mt);
        if (c->st && c->st->clear) { int cl = fl_clear_y(cx->fl, c->st->clear); if (cl != -BIG && cl - cx->oy > cy) cy = cl - cx->oy; }
        c->x = left + c->ml;
        if ((c->flags & BF_BFC) && cx->fl->n) {
            int l, r; fl_band(cx->fl, cx->oy + cy, 1, &l, &r);
            int lcl = l == -BIG ? left : wmax(left, l - cx->ox), rcl = r == BIG ? left + cw : wmin(left + cw, r - cx->ox);
            if (lcl > left || rcl < left + cw) {
                if (c->st->width.kind != LK_VAL || c->w + c->ml + c->mr > rcl - lcl) size_block_w(L, c, rcl - lcl, cbh, c->st->width.kind == LK_VAL ? 1 : 0);
                c->x = lcl + c->ml;
            }
        }
        c->y = cy;
        lay_child(L, c, cx);
        y = cy + c->h; pend = c->mb_eff; first = 0;
        b->baseline = c->baseline >= 0 ? c->y + c->baseline : -1;
    }
    int can_pass = !(b->flags & BF_BFC) && !b->pb && !b->bb && b->def_h < 0 && b->parent && !(b->tab_sub);
    if (can_pass && b->st && b->st->height.kind != LK_VAL) { b->mb_eff = collapse(b->mb, pend); }
    else { y += pend; }
    return y - top;
}

/* desplaza el contenido de una caja (para alineacion vertical en celdas, etc.) */
static void shift_content(struct wbox *b, int dy)
{
    if (!dy) return;
    for (struct wbox *c = b->first; c; c = c->next) c->y += dy;
    for (int i = 0; i < b->nfrags; i++) b->frags[i].y += dy;
    for (int i = 0; i < b->ndecos; i++) { b->decos[i].y0 += dy; b->decos[i].y1 += dy; }
    for (int i = 0; i < b->nitems; i++) { struct wbox *x = b->items[i].box; if (x && (b->items[i].type == II_ATOM || b->items[i].type == II_FLOAT)) x->y += dy; }
    if (b->baseline >= 0) b->baseline += dy;
}

/* ----------------------------- flex ----------------------------- */
struct wfi { struct wbox *b; int base, minv, maxv, grow, shrink, main, frozen, cross, auto_m; };

/* SOMA_181: una columna flex con hijos que crecen/encogen o reparten espacio necesita recolocarse cuando su alto cambia por estiramiento */
static int col_needs_height(const struct wbox *c)
{
    const struct wstyle *s = c->st; if (s->justify != JC_START) return 1;
    for (const struct wbox *k = c->first; k; k = k->next) {
        if (k->flags & BF_ABS) continue;
        if (k->st && (k->st->flex_grow > 0 || ((k->st->overflow_x || k->st->overflow_y) && k->st->flex_shrink > 0) || (k->mauto & 12))) return 1;
    }
    return 0;
}
static int lay_flex(struct wlay *L, struct wbox *b, int cw)
{
    const struct wstyle *s = b->st; int row = s->flex_dir < 2, rev = s->flex_dir & 1;
    int left = b->pl + b->bl, top = b->pt + b->bt;
    int gapm = row ? lenv(&s->gap_c, cw, 0) : lenv(&s->gap_r, cw, 0), gapx = row ? lenv(&s->gap_r, cw, 0) : lenv(&s->gap_c, cw, 0);
    int n = 0; for (struct wbox *c = b->first; c; c = c->next) if (!(c->flags & BF_ABS) && c->kind != BK_TROW) n++;
    for (struct wbox *c = b->first; c; c = c->next) if (c->flags & BF_ABS) { c->sx = left; c->sy = top; }
    if (!n) return 0;
    struct wfi *it = (struct wfi *)kmalloc(sizeof(*it) * (size_t)n); if (!it) return 0;
    int k = 0; for (struct wbox *c = b->first; c; c = c->next) if (!(c->flags & BF_ABS) && c->kind != BK_TROW) { memset(&it[k], 0, sizeof it[k]); it[k++].b = c; c->flags &= ~BF_FLOAT; }
    for (int i = 1; i < n; i++) { struct wfi t = it[i]; int j = i - 1; while (j >= 0 && it[j].b->st->order > t.b->st->order) { it[j + 1] = it[j]; j--; } it[j + 1] = t; }
    int cbh = b->def_h, total_h = 0;
    if (row) {
        for (int i = 0; i < n; i++) {
            struct wbox *c = it[i].b; const struct wstyle *cs = c->st; edges(c, cw); intrinsic(L, c);
            int ex = c->pl + c->pr + c->bl + c->br, basis;
            if (cs->flex_basis.kind == LK_VAL) { basis = lenv(&cs->flex_basis, cw, 0); if (cs->box_sizing) basis = wmax(0, basis - ex); }
            else if (cs->width.kind == LK_VAL && !(c->flags & BF_ANON)) { basis = lenv(&cs->width, cw, 0); if (cs->box_sizing) basis = wmax(0, basis - ex); }
            else basis = c->imax;
            if (c->kind == BK_REPL) { int w, h; repl_size(L, c, cw, cbh, &w, &h); if (cs->width.kind != LK_VAL && cs->flex_basis.kind != LK_VAL) basis = w; }
            it[i].base = basis + ex + c->ml + c->mr;
            int minc = cs->min_w.kind == LK_VAL ? lenv(&cs->min_w, cw, 0) : 0; if (cs->min_w.kind == LK_VAL && cs->box_sizing) minc = wmax(0, minc - ex);
            it[i].minv = (minc > 0 ? minc : (cs->overflow_x ? 0 : wmin(c->imin, basis))) + ex + c->ml + c->mr;
            it[i].maxv = cs->max_w.kind == LK_VAL ? lenv(&cs->max_w, cw, BIG) + (cs->box_sizing ? 0 : ex) + c->ml + c->mr : BIG;
            it[i].grow = cs->flex_grow; it[i].shrink = cs->flex_shrink;
            it[i].auto_m = ((c->mauto & 1) ? 1 : 0) + ((c->mauto & 2) ? 1 : 0);
        }
        int i0 = 0, ycur = top, nlines = 0;
        while (i0 < n) {
            int i1 = i0, sum = 0;
            while (i1 < n) {
                int add = it[i1].base + (i1 > i0 ? gapm : 0);
                if (s->flex_wrap && i1 > i0 && sum + add > cw) break;
                sum += add; i1++;
            }
            int cnt = i1 - i0;
            int free_ = cw - sum;
            for (int i = i0; i < i1; i++) { it[i].main = it[i].base; it[i].frozen = 0; }
            if (free_ > 0) {
                int tg = 0; for (int i = i0; i < i1; i++) tg += it[i].grow;
                if (tg > 0) {
                    int dist = tg < 100 ? (int)((int64_t)free_ * tg / 100) : free_;
                    for (int pass = 0; pass < 3; pass++) {
                        int tgg = 0; for (int i = i0; i < i1; i++) if (!it[i].frozen) tgg += it[i].grow;
                        if (tgg <= 0) break;
                        int used = 0;
                        for (int i = i0; i < i1; i++) if (!it[i].frozen) {
                            int add = (int)((int64_t)dist * it[i].grow / tgg), nv = it[i].main + add;
                            if (nv > it[i].maxv) { add = it[i].maxv - it[i].main; nv = it[i].maxv; it[i].frozen = 1; }
                            it[i].main = nv; used += add;
                        }
                        dist -= used; if (dist <= 0) break;
                    }
                }
            } else if (free_ < 0) {
                int need = -free_;
                for (int pass = 0; pass < 4 && need > 0; pass++) {
                    int64_t tw = 0; for (int i = i0; i < i1; i++) if (!it[i].frozen) tw += (int64_t)it[i].shrink * (it[i].base > 0 ? it[i].base : 1);
                    if (tw <= 0) break;
                    int taken = 0;
                    for (int i = i0; i < i1; i++) if (!it[i].frozen) {
                        int sub = (int)((int64_t)need * it[i].shrink * (it[i].base > 0 ? it[i].base : 1) / tw), nv = it[i].main - sub;
                        if (nv < it[i].minv) { sub = it[i].main - it[i].minv; nv = it[i].minv; it[i].frozen = 1; }
                        it[i].main = nv; taken += sub;
                    }
                    need -= taken; if (taken <= 0) break;
                }
            }
            int lineh = 0, used = 0;
            for (int i = i0; i < i1; i++) {
                struct wbox *c = it[i].b; int ex = c->pl + c->pr + c->bl + c->br;
                c->w = wmax(ex, it[i].main - c->ml - c->mr); used += it[i].main;
                if (c->kind == BK_REPL) { int w, h; repl_size(L, c, cw, cbh, &w, &h); if (c->w - ex != w && w > 0 && c->st->height.kind != LK_VAL) { c->cont_h = (int)((int64_t)h * (c->w - ex) / w); } else c->cont_h = h; c->h = c->cont_h + c->pt + c->pb + c->bt + c->bb; c->baseline = c->h; }
                else { c->def_h = (c->st->height.kind == LK_VAL && !c->st->height.pct) ? wmax(0, lenv(&c->st->height, 0, 0) - (c->st->box_sizing ? c->pt + c->pb + c->bt + c->bb : 0)) : -1;
                    if (c->def_h < 0 && c->st->ar_w && c->st->ar_h && c->st->height.kind != LK_VAL && !(c->flags & BF_ANON) && !wl181_off()) {   /* SOMA_181 */
                        int64_t hh = c->st->box_sizing ? (int64_t)c->w * c->st->ar_h / c->st->ar_w - (c->pt + c->pb + c->bt + c->bb) : (int64_t)(c->w - ex) * c->st->ar_h / c->st->ar_w; c->def_h = (int)wmax(0, (int)(hh > 100000 ? 100000 : hh)); }
                    lay_bfc(L, c); }
                it[i].cross = c->h + c->mt + c->mb; if (it[i].cross > lineh) lineh = it[i].cross;
            }
            if (!s->flex_wrap && b->def_h >= 0) lineh = b->def_h;
            int rem = cw - used - gapm * (cnt - 1), off = 0, extra = 0;
            int autos = 0; for (int i = i0; i < i1; i++) autos += it[i].auto_m;
            int jc = s->justify; if (rev) { if (jc == JC_START) jc = JC_END; else if (jc == JC_END) jc = JC_START; }
            if (rem > 0 && autos) { /* margenes auto absorben el espacio */ }
            else if (rem > 0) {
                switch (jc) {
                case JC_END: off = rem; break; case JC_CENTER: off = rem / 2; break;
                case JC_BETWEEN: if (cnt > 1) extra = rem / (cnt - 1); break;
                case JC_AROUND: extra = rem / cnt; off = extra / 2; break;
                case JC_EVENLY: extra = rem / (cnt + 1); off = extra; break;
                default: break;
                }
            }
            int maxasc = 0;
            for (int i = i0; i < i1; i++) { struct wbox *c = it[i].b; int al = c->st->align_self == AI_AUTO ? s->align_items : c->st->align_self; if (al == AI_BASELINE) { int bl = c->baseline >= 0 ? c->baseline : c->h; if (c->mt + bl > maxasc) maxasc = c->mt + bl; } }
            int x = left + off, ar = rem > 0 && autos ? rem / autos : 0, spare = rem > 0 && autos ? rem - ar * autos : 0;
            for (int ii = 0; ii < cnt; ii++) {
                int i = rev ? (i1 - 1 - ii) : (i0 + ii); struct wbox *c = it[i].b;
                if (rem > 0 && autos) { if (c->mauto & 1) { c->ml += ar + (spare > 0 ? 1 : 0); if (spare > 0) spare--; } if (c->mauto & 2) { c->mr += ar + (spare > 0 ? 1 : 0); if (spare > 0) spare--; } }
                c->x = x + c->ml;
                x += c->ml + c->w + c->mr + gapm + extra;
                int al = c->st->align_self == AI_AUTO ? s->align_items : c->st->align_self, outer = c->h + c->mt + c->mb, dy = 0;
                switch (al) {
                case AI_END: dy = lineh - outer; break; case AI_CENTER: dy = (lineh - outer) / 2; break;
                case AI_BASELINE: { int bl = c->baseline >= 0 ? c->baseline : c->h; dy = maxasc - (c->mt + bl); break; }
                case AI_STRETCH: if (c->st->height.kind != LK_VAL && c->kind != BK_REPL && lineh > outer) {
                        int add = lineh - outer;
                        if (!wl181_off() && c->kind == BK_FLEX && c->st->flex_dir >= 2 && col_needs_height(c)) {   /* SOMA_181: la columna estirada reparte el alto nuevo entre sus hijos */
                            c->def_h = wmax(0, c->h + add - (c->pt + c->pb + c->bt + c->bb)); lay_bfc(L, c);
                        } else c->h += add;
                    } break;
                default: break;
                }
                c->y = ycur + c->mt + (dy > 0 ? dy : 0);
            }
            ycur += lineh + gapx; total_h += lineh; nlines++; i0 = i1;
        }
        if (nlines > 1) total_h += gapx * (nlines - 1);
        kfree(it);
        return total_h;
    }
    /* columna */
    int y = top, nv = 0;
    for (int i = 0; i < n; i++) {
        struct wbox *c = it[i].b; const struct wstyle *cs = c->st;
        int al = cs->align_self == AI_AUTO ? s->align_items : cs->align_self;
        size_block_w(L, c, cw, -1, al != AI_STRETCH || cs->width.kind == LK_VAL ? 1 : 0);
        if (c->kind == BK_REPL) c->h = c->cont_h + c->pt + c->pb + c->bt + c->bb; else lay_bfc(L, c);
        if (cs->flex_basis.kind == LK_VAL && cs->flex_basis.px > 0 && !cs->flex_basis.pct) c->h = cs->flex_basis.px;
        it[i].grow = cs->flex_grow; it[i].shrink = cs->flex_shrink;   /* SOMA_181: antes la columna no leia flex-grow (solo la fila) */
        it[i].main = c->h + c->mt + c->mb; total_h += it[i].main; nv++;
    }
    total_h += gapm * (n - 1);
    int free_ = b->def_h >= 0 ? b->def_h - total_h : 0, off = 0, extra = 0;
    if (!wl181_off() && b->def_h >= 0) {
        /* SOMA_181: reparto de flex-grow/flex-shrink en columna con base y minimos (min-height: auto = contenido, salvo con overflow) */
        int vx = 0;
        for (int i = 0; i < n; i++) {
            struct wbox *c = it[i].b; const struct wstyle *cs = c->st; vx = c->pt + c->pb + c->bt + c->bb;
            int hyp = c->h, base = hyp;
            if (cs->flex_basis.kind == LK_VAL) { base = lenv(&cs->flex_basis, b->def_h, 0) + (cs->box_sizing ? 0 : vx); if (cs->box_sizing) base = wmax(base, vx); }
            int mn = cs->min_h.kind == LK_VAL ? lenv(&cs->min_h, b->def_h, 0) + (cs->box_sizing ? 0 : vx) : 0;
            if (!(cs->overflow_x || cs->overflow_y) && cs->flex_basis.kind != LK_VAL) { if (mn < hyp) mn = hyp; }
            else if (!(cs->overflow_x || cs->overflow_y) && mn < hyp && cs->flex_grow > 0) mn = hyp;
            int mx = cs->max_h.kind == LK_VAL && !cs->max_h.pct ? cs->max_h.px + (cs->box_sizing ? 0 : vx) : BIG;
            it[i].base = base; it[i].minv = mn; it[i].maxv = mx; it[i].main = base; it[i].frozen = 0;
        }
        int tot = gapm * (n - 1); for (int i = 0; i < n; i++) tot += it[i].main + it[i].b->mt + it[i].b->mb;
        int fr = b->def_h - tot;
        for (int pass = 0; pass < 4 && fr != 0; pass++) {
            int64_t tg = 0;
            for (int i = 0; i < n; i++) if (!it[i].frozen) tg += fr > 0 ? it[i].grow : (int64_t)it[i].shrink * (it[i].base > 0 ? it[i].base : 1);
            if (tg <= 0) break;
            int moved = 0, viol = 0;
            for (int i = 0; i < n; i++) if (!it[i].frozen) {
                int64_t w = fr > 0 ? it[i].grow : (int64_t)it[i].shrink * (it[i].base > 0 ? it[i].base : 1);
                int delta = (int)((int64_t)fr * w / (fr > 0 ? (tg < 100 ? 100 : tg) : tg)), nv2 = it[i].main + delta;
                if (nv2 < it[i].minv) { nv2 = it[i].minv; it[i].frozen = 1; viol = 1; } else if (nv2 > it[i].maxv) { nv2 = it[i].maxv; it[i].frozen = 1; viol = 1; }
                moved += nv2 - it[i].main; it[i].main = nv2;
            }
            fr -= moved; if (!viol && (fr >= 0 ? fr <= 1 : fr >= -1)) break; if (!moved) break;
        }
        total_h = gapm * (n - 1);
        for (int i = 0; i < n; i++) {
            struct wbox *c = it[i].b;
            if (it[i].main != c->h) {
                if (c->kind == BK_FLEX && c->st->flex_dir >= 2 && col_needs_height(c)) { c->def_h = wmax(0, it[i].main - (c->pt + c->pb + c->bt + c->bb)); lay_bfc(L, c); }
                else { c->h = it[i].main; c->cont_h = wmax(0, c->h - (c->pt + c->pb + c->bt + c->bb)); }
            }
            it[i].main = c->h + c->mt + c->mb; total_h += it[i].main;
        }
        free_ = b->def_h - total_h; if (free_ < 0) free_ = 0;
    }
    if (free_ > 0) {
        int tg = 0; for (int i = 0; i < n; i++) tg += it[i].grow;
        if (wl181_off() && tg > 0) { for (int i = 0; i < n; i++) { int add = (int)((int64_t)free_ * it[i].grow / (tg < 100 ? 100 : tg)); it[i].b->h += add; it[i].main += add; } total_h = b->def_h; free_ = 0; }
        else if (!wl181_off() || tg <= 0) {
            int jc = s->justify; if (rev) { if (jc == JC_START) jc = JC_END; else if (jc == JC_END) jc = JC_START; }
            switch (jc) { case JC_END: off = free_; break; case JC_CENTER: off = free_ / 2; break; case JC_BETWEEN: if (n > 1) extra = free_ / (n - 1); break; case JC_AROUND: extra = free_ / n; off = extra / 2; break; case JC_EVENLY: extra = free_ / (n + 1); off = extra; break; default: break; }
        }
    }
    y = top + off;
    for (int ii = 0; ii < n; ii++) {
        int i = rev ? n - 1 - ii : ii; struct wbox *c = it[i].b; int al = c->st->align_self == AI_AUTO ? s->align_items : c->st->align_self;
        c->y = y + c->mt;
        int outer_w = c->w + c->ml + c->mr, dx = 0;
        if (al == AI_END) dx = cw - outer_w; else if (al == AI_CENTER) dx = (cw - outer_w) / 2;
        c->x = left + c->ml + (dx > 0 ? dx : 0);
        y += it[i].main + gapm + extra;
    }
    kfree(it);
    return wmax(total_h, 0);
}

/* ----------------------------- grid ----------------------------- */
/* SOMA_181: nombres de area de grid-template-areas -> rectangulo (fila0, col0, fila1, col1; fin exclusivo) */
struct garea { const char *name; int nl, r0, c0, r1, c1; };
static int grid_areas_parse(const char *t, struct garea *out, int max, int *nrows_out, int *ncols_out)
{
    int na = 0, row = 0, maxc = 0;
    while (t && *t) {
        while (*t && *t != '"' && *t != '\'') t++;
        if (!*t) break;
        char q = *t++; int col = 0;
        while (*t && *t != q) {
            while (*t == ' ' || *t == '\t' || *t == '\n') t++;
            if (!*t || *t == q) break;
            const char *w = t; while (*t && *t != q && *t != ' ' && *t != '\t' && *t != '\n') t++;
            int wl = (int)(t - w);
            if (!(wl == 1 && *w == '.')) {
                int k; for (k = 0; k < na; k++) if (out[k].nl == wl && !memcmp(out[k].name, w, (size_t)wl)) break;
                if (k == na && na < max) { out[na].name = w; out[na].nl = wl; out[na].r0 = row; out[na].c0 = col; out[na].r1 = row + 1; out[na].c1 = col + 1; na++; }
                else if (k < na) { if (col + 1 > out[k].c1) out[k].c1 = col + 1; if (row + 1 > out[k].r1) out[k].r1 = row + 1; if (col < out[k].c0) out[k].c0 = col; }
            }
            col++;
        }
        if (col > maxc) maxc = col;
        if (*t) t++;
        row++;
    }
    *nrows_out = row; *ncols_out = maxc; return na;
}
static int gr_free(const uint8_t *occ, int nc, int r, int c, int rs, int cs) { for (int y = r; y < r + rs; y++) for (int x = c; x < c + cs; x++) if (occ[y * nc + x]) return 0; return 1; }
static void gr_mark(uint8_t *occ, int nc, int r, int c, int rs, int cs) { for (int y = r; y < r + rs; y++) for (int x = c; x < c + cs; x++) occ[y * nc + x] = 1; }

static int lay_grid(struct wlay *L, struct wbox *b, int cw)
{
    const struct wstyle *s = b->st; int left = b->pl + b->bl, top = b->pt + b->bt;
    int legacy = wl181_off();
    int nc = s->grid_cols > 0 ? s->grid_cols : 1; if (nc > GRID_MAX_TRACKS) nc = GRID_MAX_TRACKS;
    int gapc = lenv(&s->gap_c, cw, 0), gapr = lenv(&s->gap_r, cw, 0);
    struct wlen gtr[GRID_MAX_TRACKS]; memcpy(gtr, s->grid_tracks, sizeof gtr);
    int explicit_cols = s->grid_cols;
    if (s->grid_auto_min > 0) {      /* SOMA_PARCHE_028_UI: repeat(auto-fit, minmax(N, 1fr)) */
        nc = (cw + gapc) / (s->grid_auto_min + gapc); if (nc < 1) nc = 1; if (nc > GRID_MAX_TRACKS) nc = GRID_MAX_TRACKS;
        for (int c = 0; c < nc; c++) { gtr[c].kind = LK_FR; gtr[c].px = 100; gtr[c].pct = 0; }
    }
    /* areas con nombre: dan columnas y filas aunque no haya plantilla */
    struct garea areas[16]; int na = 0, arows = 0, acols = 0;
    if (!legacy && s->gx && s->gx->areas) na = grid_areas_parse(s->gx->areas, areas, 16, &arows, &acols);
    if (!legacy && acols > nc && explicit_cols == 0) { nc = wmin(acols, GRID_MAX_TRACKS); for (int c = 0; c < nc; c++) { gtr[c].kind = LK_FR; gtr[c].px = 100; gtr[c].pct = 0; } explicit_cols = nc; }
    int n = 0; for (struct wbox *c = b->first; c; c = c->next) if (!(c->flags & BF_ABS) && c->kind != BK_TROW) n++; else if (c->flags & BF_ABS) { c->sx = left; c->sy = top; }
    if (!n) return 0;
    /* SOMA_188: grid-auto-flow: column sin plantilla de columnas: columnas implicitas "auto", colocacion por columnas */
    int colflow = 0, cf_rows = 1;
    if (!legacy && (s->grid_flow & 1) && explicit_cols == 0 && na == 0 && s->grid_auto_min <= 0) {
        cf_rows = s->gx && s->gx->nrows > 0 ? s->gx->nrows : 1;
        nc = (n + cf_rows - 1) / cf_rows; if (nc < 1) nc = 1; if (nc > GRID_MAX_TRACKS) nc = GRID_MAX_TRACKS;
        for (int c = 0; c < nc; c++) { memset(&gtr[c], 0, sizeof gtr[c]); gtr[c].kind = LK_AUTO; }
        explicit_cols = nc; colflow = 1;
    }
    struct wbox **its = (struct wbox **)kmalloc(sizeof(*its) * (size_t)n);
    int maxrows = n * 2 + GRID_MAX_ROWS + 4; if (!legacy) maxrows += 64;
    int *pc = (int *)kmalloc(sizeof(int) * 4 * (size_t)n);                       /* fila, col, span col, span fila */
    uint8_t *occ = (uint8_t *)kmalloc((size_t)maxrows * (size_t)nc);
    if (!its || !pc || !occ) { if (its) kfree(its); if (pc) kfree(pc); if (occ) kfree(occ); return 0; }
    memset(occ, 0, (size_t)maxrows * (size_t)nc);
    int k = 0; for (struct wbox *c = b->first; c; c = c->next) if (!(c->flags & BF_ABS) && c->kind != BK_TROW) { its[k] = c; c->flags &= ~BF_FLOAT; k++; }
    for (int i = 1; i < n; i++) { struct wbox *t = its[i]; int j = i - 1; while (j >= 0 && its[j]->st->order > t->st->order) { its[j + 1] = its[j]; j--; } its[j + 1] = t; }
    /* colocacion */
    int nrows = 0;
    if (legacy) {
        int col = 0, row = 0;
        for (int i = 0; i < n; i++) {
            int span = its[i]->st->col_span > 0 ? wmin(its[i]->st->col_span, nc) : 1;
            if (col + span > nc) { col = 0; row++; }
            pc[i * 4] = row; pc[i * 4 + 1] = col; pc[i * 4 + 2] = span; pc[i * 4 + 3] = 1; col += span; if (col >= nc) { col = 0; row++; }
            if (pc[i * 4] + 1 > nrows) nrows = pc[i * 4] + 1;
        }
    } else {
        int fixed_r[2] = { 0, 0 }; (void)fixed_r;
        int *def = (int *)kmalloc(sizeof(int) * (size_t)n);   /* 0 auto, 1 fila y columna fijas, 2 solo columna, 3 solo fila */
        if (!def) { kfree(its); kfree(pc); kfree(occ); return 0; }
        for (int i = 0; i < n; i++) {
            const struct wstyle *cs = its[i]->st; int r0 = -1, c0 = -1, rs = 1, csn = 1;
            if (cs->garea) {
                for (int a = 0; a < na; a++) if (areas[a].nl == (int)wu_strlen(cs->garea) && !memcmp(areas[a].name, cs->garea, (size_t)areas[a].nl)) { r0 = areas[a].r0; c0 = areas[a].c0; rs = areas[a].r1 - areas[a].r0; csn = areas[a].c1 - areas[a].c0; break; }
            }
            if (c0 < 0) {
                int st0 = cs->gs[0], en = cs->ge[0], sp = cs->gsp[0] ? cs->gsp[0] : (cs->col_span > 1 ? cs->col_span : 0);
                int tl = nc + 1;   /* numero de lineas */
                if (st0 < 0) st0 = tl + st0 + 1;
                if (en < 0) en = tl + en + 1;
                if (st0 > 0 && en > st0) { c0 = st0 - 1; csn = en - st0; }
                else if (st0 > 0) { c0 = st0 - 1; csn = sp ? sp : 1; }
                else if (en > 0 && sp) { c0 = wmax(0, en - 1 - sp); csn = sp; }
                else if (sp) csn = sp;
                else if (en > 1) { c0 = 0; csn = en - 1; }
            }
            if (r0 < 0) {
                int st0 = cs->gs[1], en = cs->ge[1], sp = cs->gsp[1];
                int tl = (s->gx ? s->gx->nrows : 0) + 1; if (arows + 1 > tl) tl = arows + 1;
                if (st0 < 0) st0 = tl + st0 + 1;
                if (en < 0) en = tl + en + 1;
                if (st0 > 0 && en > st0) { r0 = st0 - 1; rs = en - st0; }
                else if (st0 > 0) { r0 = st0 - 1; rs = sp ? sp : 1; }
                else if (en > 0 && sp) { r0 = wmax(0, en - 1 - sp); rs = sp; }
                else if (sp) rs = sp;
            }
            if (csn < 1) csn = 1;
            if (csn > nc) csn = nc;
            if (rs < 1) rs = 1;
            if (rs > 32) rs = 32;
            if (c0 >= 0 && c0 + csn > nc) c0 = nc - csn;
            pc[i * 4] = r0; pc[i * 4 + 1] = c0; pc[i * 4 + 2] = csn; pc[i * 4 + 3] = rs;
            def[i] = (r0 >= 0 && c0 >= 0) ? 1 : (c0 >= 0 ? 2 : (r0 >= 0 ? 3 : 0));
        }
        for (int i = 0; i < n; i++) if (def[i] == 1) { int r = pc[i * 4], rs = pc[i * 4 + 3]; if (r + rs <= maxrows) { gr_mark(occ, nc, r, pc[i * 4 + 1], rs, pc[i * 4 + 2]); if (r + rs > nrows) nrows = r + rs; } else def[i] = -1; }
        int cr = 0, cc = 0;
        for (int i = 0; i < n; i++) {
            if (def[i] == 1) continue;
            int csn = pc[i * 4 + 2], rs = pc[i * 4 + 3];
            if (def[i] == 2) { int c0 = pc[i * 4 + 1], r = 0; while (r + rs < maxrows && !gr_free(occ, nc, r, c0, rs, csn)) r++; pc[i * 4] = r; }
            else if (def[i] == 3) { int r0 = pc[i * 4], c = 0; if (r0 + rs > maxrows) r0 = maxrows - rs; pc[i * 4] = r0; while (c + csn <= nc && !gr_free(occ, nc, r0, c, rs, csn)) c++; if (c + csn > nc) c = 0; pc[i * 4 + 1] = c; }
            else if (colflow) {   /* SOMA_188: por columnas (filas de la plantilla), lo que no cabe sigue debajo */
                int r = 0, c = 0, ok = 0;
                for (c = 0; c + csn <= nc && !ok; c++) for (r = 0; r + rs <= cf_rows; r++) if (gr_free(occ, nc, r, c, rs, csn)) { ok = 1; break; }
                if (ok) { c--; pc[i * 4] = r; pc[i * 4 + 1] = c; }
                else { r = cf_rows; while (r + rs < maxrows && !gr_free(occ, nc, r, 0, rs, csn)) r++; pc[i * 4] = r; pc[i * 4 + 1] = 0; }
            }
            else {
                if (cc + csn > nc) { cc = 0; cr++; }
                while (cr + rs < maxrows && !gr_free(occ, nc, cr, cc, rs, csn)) { cc++; if (cc + csn > nc) { cc = 0; cr++; } }
                pc[i * 4] = cr; pc[i * 4 + 1] = cc; cc += csn; if (cc >= nc) { cc = 0; cr++; }
            }
            if (pc[i * 4] + rs > maxrows) pc[i * 4] = wmax(0, maxrows - rs);
            gr_mark(occ, nc, pc[i * 4], pc[i * 4 + 1], rs, csn);
            if (pc[i * 4] + rs > nrows) nrows = pc[i * 4] + rs;
        }
        for (int i = 0; i < n; i++) if (def[i] == -1) { pc[i * 4] = wmax(0, nrows - 1); pc[i * 4 + 1] = 0; pc[i * 4 + 3] = 1; }
        kfree(def);
        { int er = s->gx ? s->gx->nrows : 0; if (arows > er) er = arows; if (er > nrows) nrows = er; }
    }
    if (nrows < 1) nrows = 1;
    /* anchos de columna */
    int tw[GRID_MAX_TRACKS]; int autoc[GRID_MAX_TRACKS]; memset(autoc, 0, sizeof autoc);
    int fr_sum = 0, fixed = 0;
    for (int c = 0; c < nc; c++) {
        const struct wlen *t = &gtr[c];
        if (explicit_cols > 0 && t->kind == LK_VAL) { tw[c] = lenv(t, cw, 0); fixed += tw[c]; }
        else if (explicit_cols > 0 && t->kind == LK_FR) { tw[c] = 0; fr_sum += t->px; }
        else { tw[c] = 0; autoc[c] = 1; }
    }
    if (explicit_cols == 0) { autoc[0] = 0; tw[0] = cw; }
    int auto_total = 0;
    for (int c = 0; c < nc; c++) if (autoc[c]) {
        int mx = 0;
        for (int i = 0; i < n; i++) if (pc[i * 4 + 1] == c && pc[i * 4 + 2] == 1) { intrinsic(L, its[i]); int a, z; outer_mm(its[i], &a, &z); if (z > mx) mx = z; }
        tw[c] = mx; auto_total += mx;
    }
    int avail = cw - gapc * (nc - 1);
    if (fr_sum > 0) {
        int rem = avail - fixed - auto_total; if (rem < 0) rem = 0;
        for (int c = 0; c < nc; c++) if (explicit_cols > 0 && gtr[c].kind == LK_FR) {
            tw[c] = (int)((int64_t)rem * gtr[c].px / fr_sum);
            /* los fr no bajan del contenido minimo */
            int mn = 0; for (int i = 0; i < n; i++) if (pc[i * 4 + 1] == c && pc[i * 4 + 2] == 1) { intrinsic(L, its[i]); int a, z; outer_mm(its[i], &a, &z); if (a > mn) mn = a; }
            if (tw[c] < mn && gtr[c].px > 0) tw[c] = mn;
        }
    } else if (explicit_cols > 0) {
        int sum = fixed + auto_total, na2 = 0; for (int c = 0; c < nc; c++) na2 += autoc[c];
        if (na2 && avail > sum) { int add = (avail - sum) / na2; for (int c = 0; c < nc; c++) if (autoc[c]) tw[c] += add; }
    }
    int colx[GRID_MAX_TRACKS + 1]; colx[0] = left; for (int c = 0; c < nc; c++) colx[c + 1] = colx[c] + tw[c] + gapc;
    /* maquetar cada elemento en su celda (ancho) y medir su alto */
    int *rh = (int *)kmalloc(sizeof(int) * (size_t)(nrows + 1) * 2); if (!rh) { kfree(its); kfree(pc); kfree(occ); return 0; }
    int *ry = rh + nrows + 1;
    for (int r = 0; r < nrows; r++) rh[r] = 0;
    int ex_v = b->pt + b->pb + b->bt + b->bb;
    for (int i = 0; i < n; i++) {
        struct wbox *c = its[i]; int c0 = pc[i * 4 + 1], span = pc[i * 4 + 2];
        int cellw = colx[c0 + span] - gapc - colx[c0];
        int js = c->st->justify_self ? c->st->justify_self : s->justify_items;
        size_block_w(L, c, cellw, -1, (c->st->width.kind == LK_VAL || (!legacy && js)) ? 1 : 0);
        if (c->kind == BK_REPL) c->h = c->cont_h + c->pt + c->pb + c->bt + c->bb; else lay_bfc(L, c);
        if (legacy) { int oh = c->h + c->mt + c->mb; int r = pc[i * 4]; if (oh > rh[r]) rh[r] = oh; }
    }
    int total_h = 0;
    if (legacy) {
        for (int r = 0; r < nrows; r++) { ry[r] = top + total_h; total_h += rh[r] + gapr; }
        total_h -= gapr;
        int y = top;
        for (int r = 0; r < nrows; r++) { ry[r] = y; y += rh[r] + gapr; }
    } else {
        /* alto de filas: pistas explicitas (px, %, fr, auto) y filas implicitas (grid-auto-rows) */
        const struct wgx *gx = s->gx; int64_t frsum = 0; int fixed_h = 0;
        int target = b->def_h;
        if (target < 0 && s->min_h.kind == LK_VAL) { int mh = lenv(&s->min_h, b->cbh < 0 ? 0 : b->cbh, 0); if (s->box_sizing) mh -= ex_v; if (mh > 0) target = mh; }
        int *kind = (int *)kmalloc(sizeof(int) * (size_t)(nrows + 1)); int *fr = (int *)kmalloc(sizeof(int) * (size_t)(nrows + 1));
        if (!kind || !fr) { if (kind) kfree(kind); if (fr) kfree(fr); kfree(rh); kfree(its); kfree(pc); kfree(occ); return 0; }
        for (int r = 0; r < nrows; r++) {
            struct wlen t; if (gx && r < gx->nrows) t = gx->rows[r]; else if (gx) t = gx->auto_rows; else { memset(&t, 0, sizeof t); t.kind = LK_AUTO; }
            kind[r] = 0; fr[r] = 0;                              /* 0 auto, 1 fija, 2 fr */
            if (t.kind == LK_VAL && !(t.pct && target < 0)) { kind[r] = 1; rh[r] = lenv(&t, target < 0 ? 0 : target - ex_v * 0, 0); }
            else if (t.kind == LK_FR) { kind[r] = 2; fr[r] = t.px; frsum += t.px; rh[r] = 0; }
            else rh[r] = 0;
        }
        for (int i = 0; i < n; i++) if (pc[i * 4 + 3] == 1) { struct wbox *c = its[i]; int r = pc[i * 4]; int oh = c->h + c->mt + c->mb; if (kind[r] != 1 && oh > rh[r]) rh[r] = oh; }
        for (int i = 0; i < n; i++) if (pc[i * 4 + 3] > 1) {            /* elementos de varias filas: el deficit va a la ultima fila no fija */
            struct wbox *c = its[i]; int r0 = pc[i * 4], rs = pc[i * 4 + 3], oh = c->h + c->mt + c->mb, sum = gapr * (rs - 1);
            for (int r = r0; r < r0 + rs && r < nrows; r++) sum += rh[r];
            if (sum < oh) { int last = r0; for (int r = r0; r < r0 + rs && r < nrows; r++) if (kind[r] != 1) last = r; if (kind[last] != 1) rh[last] += oh - sum; }
        }
        for (int r = 0; r < nrows; r++) if (kind[r] == 1) fixed_h += rh[r]; else if (kind[r] == 0) fixed_h += rh[r]; else fixed_h += 0;
        if (frsum > 0) {
            int rem = target >= 0 ? target - fixed_h - gapr * (nrows - 1) : 0;
            for (int r = 0; r < nrows; r++) if (kind[r] == 2) { int sh = rem > 0 ? (int)((int64_t)rem * fr[r] / frsum) : 0; rh[r] = wmax(rh[r], sh); }
        }
        int y = top; for (int r = 0; r < nrows; r++) { ry[r] = y; y += rh[r] + gapr; }
        total_h = y - gapr - top;
        kfree(kind); kfree(fr);
    }
    for (int i = 0; i < n; i++) {
        struct wbox *c = its[i]; int c0 = pc[i * 4 + 1], span = pc[i * 4 + 2], r0 = pc[i * 4], rs = pc[i * 4 + 3];
        int cellw = colx[c0 + span] - gapc - colx[c0];
        int celh = ry[wmin(r0 + rs - 1, nrows - 1)] + rh[wmin(r0 + rs - 1, nrows - 1)] - ry[r0];
        int js = legacy ? 0 : (c->st->justify_self ? c->st->justify_self : s->justify_items), dx = 0;
        if (js) { int outer_w = c->w + c->ml + c->mr; dx = js == 2 ? cellw - outer_w : js == 3 ? (cellw - outer_w) / 2 : 0; if (dx < 0) dx = 0; }
        c->x = colx[c0] + c->ml + dx;
        int al = c->st->align_self == AI_AUTO ? s->align_items : c->st->align_self, outer = c->h + c->mt + c->mb, dy = 0;
        if (al == AI_STRETCH) {
            if (c->st->height.kind != LK_VAL && c->kind != BK_REPL && celh > outer) {
                int add = celh - outer;
                if (!legacy && c->kind == BK_FLEX && c->st->flex_dir >= 2 && col_needs_height(c)) { c->def_h = wmax(0, c->h + add - (c->pt + c->pb + c->bt + c->bb)); lay_bfc(L, c); }
                else c->h += add;
            }
        }
        else if (al == AI_END) dy = celh - outer; else if (al == AI_CENTER) dy = (celh - outer) / 2;
        c->y = ry[r0] + c->mt + (dy > 0 ? dy : 0);
    }
    kfree(its); kfree(pc); kfree(occ); kfree(rh);
    return total_h;
}

/* ----------------------------- tablas ----------------------------- */
static int lay_table(struct wlay *L, struct wbox *b, int cw)
{
    struct wtcol cols[MAXCOLS]; int ncols = table_cols(L, b, cw, cols), sp = tbl_spacing(b);
    int left = b->pl + b->bl, top = b->pt + b->bb * 0 + b->bt, y = top;
    int mn[MAXCOLS], mx[MAXCOLS], fx[MAXCOLS], widths[MAXCOLS], sum_mn = 0, sum_mx = 0;
    for (int i = 0; i < ncols; i++) {
        int f = cols[i].fixed;
        if (cols[i].pct) { int pw = (int)((int64_t)cw * cols[i].pct / 10000); if (pw > f) f = pw; if (f < cols[i].mn) f = cols[i].mn; }
        fx[i] = f > 0; mn[i] = f > 0 ? f : cols[i].mn; mx[i] = f > 0 ? f : cols[i].mx;
        if (mx[i] < mn[i]) mx[i] = mn[i];
        sum_mn += mn[i]; sum_mx += mx[i];
    }
    int avail = cw - sp * (ncols + 1);
    if (avail <= sum_mn) { for (int i = 0; i < ncols; i++) widths[i] = mn[i]; }
    else if (avail >= sum_mx) {
        int extra = avail - sum_mx, flex = 0; for (int i = 0; i < ncols; i++) if (!fx[i]) flex += mx[i] > 0 ? mx[i] : 1;
        int allf = flex == 0; if (allf) for (int i = 0; i < ncols; i++) flex += mx[i] > 0 ? mx[i] : 1;
        int given = 0, lastf = -1;
        for (int i = 0; i < ncols; i++) {
            widths[i] = mx[i];
            if (allf || !fx[i]) { int add = (int)((int64_t)extra * (mx[i] > 0 ? mx[i] : 1) / (flex ? flex : 1)); widths[i] += add; given += add; lastf = i; }
        }
        if (lastf >= 0) widths[lastf] += extra - given;
    } else {
        int64_t den = sum_mx - sum_mn, num = avail - sum_mn;
        for (int i = 0; i < ncols; i++) widths[i] = mn[i] + (int)((int64_t)(mx[i] - mn[i]) * num / (den ? den : 1));
    }
    for (struct wbox *c = b->first; c; c = c->next) if (c->tab_sub == TS_CAPTION) {
        size_block_w(L, c, cw, -1, 0); lay_bfc(L, c); c->x = left + c->ml; c->y = y + c->mt; y += c->h + c->mt + c->mb;
    }
    int nr = 0; for (struct wbox *r = b->first; r; r = r->next) if (r->kind == BK_TROW) nr++;
    int *rh = (int *)kmalloc(sizeof(int) * (size_t)(nr + 1) * 2); if (!rh) return 0;
    int *rowy = rh + nr + 1; struct wbox **rows = (struct wbox **)kmalloc(sizeof(*rows) * (size_t)(nr + 1)); if (!rows) { kfree(rh); return 0; }
    int ri = 0; for (struct wbox *r = b->first; r; r = r->next) if (r->kind == BK_TROW) rows[ri++] = r;
    int colx[MAXCOLS + 1]; colx[0] = sp; for (int i = 0; i < ncols; i++) colx[i + 1] = colx[i] + widths[i] + sp;
    int totw = colx[ncols];
    for (int r = 0; r < nr; r++) {
        int h = 0;
        for (struct wbox *c = rows[r]->first; c; c = c->next) {
            if (c->col >= ncols) continue;
            int span = wmin(c->colspan, ncols - c->col); c->w = colx[c->col + span] - sp - colx[c->col];
            c->def_h = -1; c->cbh = -1; lay_bfc(L, c);
            int mh = (c->st->height.kind == LK_VAL && !c->st->height.pct) ? c->st->height.px + (c->st->box_sizing ? 0 : c->pt + c->pb + c->bt + c->bb) : 0;
            if (c->h < mh) { c->cont_h = c->h; c->h = mh; }
            if (c->rowspan == 1 && c->h > h) h = c->h;
        }
        rh[r] = h;
    }
    for (int r = 0; r < nr; r++) for (struct wbox *c = rows[r]->first; c; c = c->next) {
        if (c->rowspan <= 1 || c->col >= ncols) continue;
        int span = wmin(c->rowspan, nr - r), tot = sp * (span - 1); for (int k = 0; k < span; k++) tot += rh[r + k];
        if (c->h > tot) rh[r + span - 1] += c->h - tot;
    }
    int ycur = y + sp;
    for (int r = 0; r < nr; r++) { rowy[r] = ycur; ycur += rh[r] + sp; }
    for (int r = 0; r < nr; r++) {
        struct wbox *row = rows[r]; row->x = left; row->y = rowy[r]; row->w = totw; row->h = rh[r]; row->kind = BK_TROW;
        for (struct wbox *c = row->first; c; c = c->next) {
            if (c->col >= ncols) continue;
            int span = wmin(c->rowspan, nr - r), full = sp * (span - 1); for (int k = 0; k < span; k++) full += rh[r + k];
            int nat = c->h, extra = full - nat; int va = c->st->vertical_align;
            c->x = colx[c->col]; c->y = 0;
            if (extra > 0 && va != VA_TOP) shift_content(c, va == VA_BOTTOM ? extra : extra / 2);
            c->h = full > nat ? full : nat;
        }
    }
    kfree(rows); kfree(rh);
    return ycur - top;
}

/* ----------------------------- posicionamiento absoluto ----------------------------- */
static void lay_abs(struct wlay *L, struct wbox *cb, struct wbox *a)
{
    const struct wstyle *s = a->st; int fixed = (a->flags & BF_FIXED) != 0, c0x, c0y, cwid, chei;
    if (fixed || cb == L->root || !cb) { c0x = 0; c0y = 0; cwid = L->vw; chei = L->vh; }
    else { c0x = cb->bl; c0y = cb->bt; cwid = cb->w - cb->bl - cb->br; chei = cb->h - cb->bt - cb->bb; }
    int hl = s->left.kind == LK_VAL, hr = s->right.kind == LK_VAL, ht = s->top.kind == LK_VAL, hb = s->bottom.kind == LK_VAL;
    int lv = lenv(&s->left, cwid, 0), rv = lenv(&s->right, cwid, 0), tv = lenv(&s->top, chei, 0), bv = lenv(&s->bottom, chei, 0);
    edges(a, cwid);
    int stretchw = hl && hr && s->width.kind != LK_VAL && a->kind != BK_REPL;
    if (stretchw) size_block_w(L, a, cwid - lv - rv, chei, 0);
    else size_block_w(L, a, hl && !hr ? cwid - lv : (hr && !hl ? cwid - rv : cwid), chei, 1);
    a->mauto = 0;
    if (a->kind == BK_REPL) a->h = a->cont_h + a->pt + a->pb + a->bt + a->bb;
    else {
        if (hl && hr && s->width.kind == LK_VAL && (s->margin[3].kind == LK_AUTO || s->margin[1].kind == LK_AUTO)) {
            int free_ = cwid - lv - rv - a->w; if (free_ > 0) { if (s->margin[3].kind == LK_AUTO && s->margin[1].kind == LK_AUTO) { a->ml = free_ / 2; a->mr = free_ - a->ml; } else if (s->margin[3].kind == LK_AUTO) a->ml = free_; else a->mr = free_; }
        }
        if (ht && hb && s->height.kind != LK_VAL) { a->def_h = wmax(0, chei - tv - bv - a->mt - a->mb - a->pt - a->pb - a->bt - a->bb); }
        lay_bfc(L, a);
    }
    int ax = a->sx, ay = a->sy;
    for (struct wbox *p = a->parent; p && p != cb; p = p->parent) { ax += p->x; ay += p->y; }
    if (hl) a->x = c0x + lv + a->ml; else if (hr) a->x = c0x + cwid - rv - a->w - a->mr; else a->x = ax + a->ml;
    if (ht) a->y = c0y + tv + a->mt; else if (hb) a->y = c0y + chei - bv - a->h - a->mb; else a->y = ay + a->mt;
    if (fixed) { /* respecto al visor */ }
}

/* ----------------------------- contenido ----------------------------- */
static void lay_content(struct wlay *L, struct wbox *b, struct wctx *cx)
{
    wu_tick();
    if (b->node && b->node->tag == WT_BUTTON && b->kind == BK_BLOCK && b->def_h >= 0 && !b->pad0 && !(b->flags & BF_ANON) && !wl181_off()) {
        /* SOMA_181: el contenido de un <button> con alto fijo se centra en vertical (como en los navegadores) */
        b->pad0 = 1; int sdh = b->def_h, spt = b->pt, spb = b->pb;
        b->def_h = -1; lay_bfc(L, b);
        int ch0 = b->cont_h; b->def_h = sdh;
        if (ch0 < sdh) { int off = (sdh - ch0) / 2; b->pt = spt + off; b->pb = spb + (sdh - ch0 - off); b->def_h = ch0; }
        lay_bfc(L, b);
        b->def_h = sdh; b->pt = spt; b->pb = spb; b->pad0 = 0;
        return;
    }
    int ex_v = b->pt + b->pb + b->bt + b->bb, top = b->pt + b->bt;
    int cw = wmax(0, b->w - b->pl - b->pr - b->bl - b->br);
    if (web_stack_left() < 5000 || L->depth > 180) { b->h = ex_v; return; }
    L->depth++;
    int ch = 0; b->baseline = -1;
    switch (b->kind) {
    case BK_REPL: ch = b->cont_h; break;
    case BK_BLOCK: ch = b->items ? lay_inline(L, b, cx, cw) : lay_blocks(L, b, cx, cw); break;
    case BK_FLEX: ch = lay_flex(L, b, cw); break;
    case BK_GRID: ch = lay_grid(L, b, cw); break;
    case BK_TABLE: ch = lay_table(L, b, cw); break;
    default: break;
    }
    if (b->kind != BK_REPL) {
        if (b->flags & BF_BFC) { int bt2 = cx->fl->maxb - top; if (bt2 > ch) ch = bt2; }
        else if (b->st && b->st->clearfix && !(b->flags & BF_ANON)) {
            int bm = -BIG; for (int i = 0; i < cx->fl->n; i++) if (cx->fl->r[i].y0 >= cx->oy + top && cx->fl->r[i].y1 > bm) bm = cx->fl->r[i].y1;
            if (bm != -BIG && bm - cx->oy - top > ch) ch = bm - cx->oy - top;
        }
        b->sch = ch;   /* SOMA_181 */
        if (b->def_h >= 0) ch = b->def_h;
        else if (b->st && !(b->flags & BF_ANON)) {
            const struct wstyle *s = b->st;
            if (s->min_h.kind == LK_VAL) { int mh = lenv(&s->min_h, b->cbh < 0 ? 0 : b->cbh, 0); if (s->box_sizing) mh -= ex_v; if (ch < mh) ch = mh; }
        }
        if (b->st && !(b->flags & BF_ANON) && b->st->max_h.kind == LK_VAL && !b->st->max_h.pct) { int mh = b->st->max_h.px; if (b->st->box_sizing) mh -= ex_v; if (ch > mh) ch = mh; }
        b->cont_h = ch;
    }
    if (b->kind != BK_REPL && b->st && !(b->flags & BF_ANON) && b->st->line_clamp && b->st->overflow_y && !wl181_off()) {   /* SOMA_181: -webkit-line-clamp */
        int mh = wc_line_px(b->st) * b->st->line_clamp; if (ch > mh) { ch = mh; b->cont_h = ch; }
    }
    b->h = ch + top + b->pb + b->bb;
    if (b->st && !(b->flags & BF_ANON) && (b->st->tr_x.kind == LK_VAL || b->st->tr_y.kind == LK_VAL) && !wl181_off()) {   /* SOMA_181: transform: translate() */
        if (b->st->tr_x.kind == LK_VAL) b->rdx += lenv(&b->st->tr_x, b->w, 0);
        if (b->st->tr_y.kind == LK_VAL) b->rdy += lenv(&b->st->tr_y, b->h, 0);
    }
    for (struct wbox *a = b->abs_chain; a; a = a->abs_next) lay_abs(L, b, a);
    L->depth--;
}

/* ----------------------------- API ----------------------------- */
struct wlay *wl_new(struct wdoc *d, struct wcss *css, wl_img_cb cb, void *ud)
{
    struct wlay *L = (struct wlay *)kmalloc(sizeof(*L)); if (!L) return 0;
    memset(L, 0, sizeof *L); L->d = d; L->css = css; L->img_cb = cb; L->img_ud = ud; L->ar.limit = 48u << 20; L->zoom = 100;
    return L;
}
void wl_free(struct wlay *L)
{
    if (!L) return;
    wa_free_all(&L->ar); if (L->deferred) kfree(L->deferred); kfree(L);
}
static void clear_boxes(struct wnode *n)
{
    /* SOMA_188: arbol compuesto (los arboles sombra tambien) y nodos de ::before/::after: si no, quedaban punteros a cajas
       de la maquetacion anterior (ya liberada) en los nodos sombra -> uso tras liberar al hacer clic en un <input> sombra */
    for (struct wnode *c = n; c; c = wd_walk_next(c, n)) {
        c->box = 0;
        if (c->type == WN_ELEM && c->st) for (int k = 0; k < 2; k++) { struct wnode *ps = c->st->pse[k]; if (ps) { ps->box = 0; for (struct wnode *t = ps->first; t; t = t->next) t->box = 0; } }
        if (c->type == WN_ELEM && c->shadow) c->shadow->box = 0;
    }
}
static void extent(struct wlay *L, struct wbox *b, int ox, int oy, int depth)
{
    if (depth > 80 || web_stack_left() < 3000) return;
    int x = ox + b->x + b->rdx, y = oy + b->y + b->rdy;
    if (x + b->w > L->doc_w) L->doc_w = x + b->w;
    if (y + b->h > L->doc_h) L->doc_h = y + b->h;
    if (b->st && !(b->flags & BF_ANON) && (b->st->overflow_x || b->st->overflow_y) && depth > 0) { }
    else {
        for (struct wbox *c = b->first; c; c = c->next) if (!(c->flags & BF_ABS)) extent(L, c, x, y, depth + 1);
        for (int i = 0; i < b->nitems; i++) { struct wbox *c = b->items[i].box; if (c && (b->items[i].type == II_ATOM || b->items[i].type == II_FLOAT)) extent(L, c, x, y, depth + 1); }
    }
    for (struct wbox *a = b->abs_chain; a; a = a->abs_next) if (!(a->flags & BF_FIXED)) extent(L, a, x, y, depth + 1);
}
void wl_layout(struct wlay *L, int vw, int vh)
{
    if (!L || !L->d) return;
    if (vw < 16) vw = 16;
    if (vh < 16) vh = 16;
    L->vw = vw; L->vh = vh; L->doc_w = vw; L->doc_h = 0; L->depth = 0;
    wa_free_all(&L->ar); L->root = 0; L->canvas_bg = 0; L->canvas_nodes[0] = L->canvas_nodes[1] = 0;
    if (L->deferred) { kfree(L->deferred); L->deferred = 0; L->ndef = L->capdef = 0; }
    clear_boxes(L->d->root);
    struct wnode *h = L->d->html; if (!h || !h->st) return;
    struct wbld B; memset(&B, 0, sizeof B); B.L = L;
    struct wbox *root = mkbox(&B, h, h->st, BK_BLOCK); if (!root) return;
    root->flags |= BF_BFC; L->root = root; B.cb = root; h->box = root;
    build_content(&B, root, h, 0);
    size_block_w(L, root, vw, vh, 0);
    root->w = vw - root->ml - root->mr; root->x = root->ml; root->y = root->mt;
    root->mt_eff = root->mt; root->top_abs = 0;
    lay_bfc(L, root);
    extent(L, root, 0, 0, 0);
    if (root->y + root->h + root->mb > L->doc_h) L->doc_h = root->y + root->h + root->mb;
    /* fondo del lienzo: el del html o, si no tiene, el del body */
    struct wnode *body = L->d->body;
    if (h->st->bg_color) { L->canvas_bg = h->st->bg_color; L->canvas_nodes[0] = h; }
    else if (body && body->st && body->st->bg_color) { L->canvas_bg = body->st->bg_color; L->canvas_nodes[0] = body; }
}

/* ============================== pintado ============================== */
struct wpt { struct wlay *L; int cx0, cy0, cx1, cy1; int scx, scy; uint32_t bg; int vx, vy; /* SOMA_181: origen del visor en pantalla */ };

static uint32_t mul_alpha(uint32_t c, int alpha)
{
    uint32_t a = c >> 24; a = a * (uint32_t)alpha / 255u; return (a << 24) | (c & 0xFFFFFF);
}
static void clip_apply(struct wpt *P) { fb_set_clip(P->cx0, P->cy0, P->cx1 - P->cx0, P->cy1 - P->cy0); }
static void clip_isect(struct wpt *P, int x, int y, int w, int h)
{
    if (x > P->cx0) P->cx0 = x;
    if (y > P->cy0) P->cy0 = y;
    if (x + w < P->cx1) P->cx1 = x + w;
    if (y + h < P->cy1) P->cy1 = y + h;
    if (P->cx1 < P->cx0) P->cx1 = P->cx0;
    if (P->cy1 < P->cy0) P->cy1 = P->cy0;
    clip_apply(P);
}
static void fillr(struct wpt *P, int x, int y, int w, int h, uint32_t argb)
{
    uint32_t a = argb >> 24; if (!a || w <= 0 || h <= 0) return;
    int x1 = x + w, y1 = y + h; if (x < P->cx0) x = P->cx0; if (y < P->cy0) y = P->cy0; if (x1 > P->cx1) x1 = P->cx1; if (y1 > P->cy1) y1 = P->cy1;
    if (x1 <= x || y1 <= y) return;
    if (a >= 255) fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)(x1 - x), (uint32_t)(y1 - y), argb & 0xFFFFFF);
    else fb_fill_rect_alpha(x, y, x1 - x, y1 - y, argb);
}
static int isqrt_i(int v) { if (v <= 0) return 0; int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }
static int corner_inset(int r, int row)   /* row 0..r-1 desde el borde superior (centros de pixel) */
{
    int t = 2 * r - 2 * row - 1; int v = 4 * r * r - t * t; int xs = isqrt_i(v);
    int in = r - (xs + 1) / 2; return in < 0 ? 0 : in;
}
static void rrect(struct wpt *P, int x, int y, int w, int h, int r, uint32_t argb)
{
    if (r <= 0 || w <= 0 || h <= 0) { fillr(P, x, y, w, h, argb); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) { fillr(P, x, y, w, h, argb); return; }
    if (argb >> 24) { fb_fill_rrect_aa(x, y, w, h, r, argb); return; }      /* SOMA_PARCHE_028_UI: esquinas con antialias (respeta el recorte activo) */
    for (int i = 0; i < r; i++) {
        int in = corner_inset(r, i);
        fillr(P, x + in, y + i, w - 2 * in, 1, argb);
        fillr(P, x + in, y + h - 1 - i, w - 2 * in, 1, argb);
    }
    fillr(P, x, y + r, w, h - 2 * r, argb);
}
static uint32_t lerp_col(uint32_t a, uint32_t b, int t, int den)
{
    if (den <= 0) return a;
    uint32_t o = 0;
    for (int s = 0; s < 32; s += 8) { int ca = (a >> s) & 255, cb = (b >> s) & 255; o |= (uint32_t)(ca + (cb - ca) * t / den) << s; }
    return o;
}
static void gradient(struct wpt *P, int x, int y, int w, int h, uint32_t a, uint32_t b, int horiz, int alpha)
{
    if (a >> 24 == 0) a |= 0xFF000000u;
    if (b >> 24 == 0) b |= 0xFF000000u;
    if (horiz) { int x0 = x < P->cx0 ? P->cx0 : x, x1 = x + w > P->cx1 ? P->cx1 : x + w; for (int i = x0; i < x1; i++) fillr(P, i, y, 1, h, mul_alpha(lerp_col(a, b, i - x, w > 1 ? w - 1 : 1), alpha)); }
    else { int y0 = y < P->cy0 ? P->cy0 : y, y1 = y + h > P->cy1 ? P->cy1 : y + h; for (int j = y0; j < y1; j++) fillr(P, x, j, w, 1, mul_alpha(lerp_col(a, b, j - y, h > 1 ? h - 1 : 1), alpha)); }
}
/* SOMA_PARCHE_028_UI: degradado recortado a las esquinas redondeadas de la caja (botones tipo pastilla, tarjetas) */
static void gradient_r(struct wpt *P, int x, int y, int w, int h, uint32_t a, uint32_t b, int horiz, int alpha, int r)
{
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) { gradient(P, x, y, w, h, a, b, horiz, alpha); return; }
    if (a >> 24 == 0) a |= 0xFF000000u;
    if (b >> 24 == 0) b |= 0xFF000000u;
    if (horiz) {
        int x0 = x < P->cx0 ? P->cx0 : x, x1 = x + w > P->cx1 ? P->cx1 : x + w;
        for (int i = x0; i < x1; i++) { int k = i - x, e = k < r ? k : (w - 1 - k < r ? w - 1 - k : -1), in = e >= 0 ? corner_inset(r, e) : 0; fillr(P, i, y + in, 1, h - 2 * in, mul_alpha(lerp_col(a, b, k, w > 1 ? w - 1 : 1), alpha)); }
    } else {
        int y0 = y < P->cy0 ? P->cy0 : y, y1 = y + h > P->cy1 ? P->cy1 : y + h;
        for (int j = y0; j < y1; j++) { int k = j - y, e = k < r ? k : (h - 1 - k < r ? h - 1 - k : -1), in = e >= 0 ? corner_inset(r, e) : 0; fillr(P, x + in, j, w - 2 * in, 1, mul_alpha(lerp_col(a, b, k, h > 1 ? h - 1 : 1), alpha)); }
    }
}
/* SOMA_152_WEB_FLUIDO: cache de imagenes ya escaladas (y recortadas al radio). Antes cada repintado completo de la vista
 * (al terminar una maquetacion, un reloj en JS, una imagen nueva...) volvia a escalar pixel a pixel todas las fotos
 * visibles: en una pagina de tarjetas era la mayor parte de los ~25 ms de pintado en QEMU. Ahora se escala una vez y se
 * copia. Clave: serial del mapa de bits (unico por decodificacion: nunca se confunde aunque se reutilice la direccion),
 * tamano mostrado, radio y opacidad. Mismo algoritmo y mismos pixeles que el camino sin cache. Solo hilo de la GUI. */
#ifndef SOMA_WEB_IMG_CACHE_KB
#define SOMA_WEB_IMG_CACHE_KB 12288
#endif
struct ic152 { uint32_t serial; int w, h, radius, alpha, opaque_mid; uint32_t *px; uint32_t bytes; uint64_t used; };
static struct ic152 s_ic152[32];
static uint32_t s_ic152_bytes; static uint64_t s_ic152_tick;
/* devuelve 1 si todas las filas fuera de las esquinas quedaron opacas (alfa 255): se pueden copiar sin mezclar */
static int scale_rows152(const struct wimginfo *ii, int w, int h, int radius, int alpha, uint32_t *out)
{
    int opaque_mid = 1;
    int iw = ii->pw > 0 ? ii->pw : ii->w, ih = ii->ph > 0 ? ii->ph : ii->h; int stepx = iw / w, stepy = ih / h;
    int nsx = stepx >= 2 ? wmin(stepx, 3) : 1, nsy = stepy >= 2 ? wmin(stepy, 3) : 1;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    for (int py = 0; py < h; py++) {
        int sys[3];
        for (int sj = 0; sj < nsy; sj++) { int sy = (int)(((int64_t)py * ih * nsy + (int64_t)sj * ih + ih / 2) / ((int64_t)h * nsy)); sys[sj] = sy >= ih ? ih - 1 : sy; }
        uint32_t *o = out + (size_t)py * (size_t)w;
        for (int px = 0; px < w; px++) {
            uint32_t r = 0, g = 0, bl = 0, a = 0, cnt = 0;
            for (int sj = 0; sj < nsy; sj++) {
                const uint32_t *row = ii->px + (size_t)sys[sj] * iw;
                for (int si = 0; si < nsx; si++) {
                    int sx = (int)(((int64_t)px * iw * nsx + (int64_t)si * iw + iw / 2) / ((int64_t)w * nsx)); if (sx >= iw) sx = iw - 1;
                    uint32_t c = row[sx], ca = c >> 24;
                    r += ((c >> 16) & 255) * ca; g += ((c >> 8) & 255) * ca; bl += (c & 255) * ca; a += ca; cnt++;
                }
            }
            uint32_t oa = cnt ? a / cnt : 0, orr = a ? r / a : 0, og = a ? g / a : 0, ob = a ? bl / a : 0;
            if (radius > 0) {
                int cxp = px < radius ? radius - px : (px >= w - radius ? px - (w - radius - 1) : 0), cyp = py < radius ? radius - py : (py >= h - radius ? py - (h - radius - 1) : 0);
                if (cxp && cyp) { int d2 = cxp * cxp + cyp * cyp, r2 = radius * radius; if (d2 > r2 + radius) oa = 0; else if (d2 > r2 - radius) oa = oa * (uint32_t)(r2 + radius - d2) / (uint32_t)(2 * radius + 1); }
            }
            oa = oa * (uint32_t)alpha / 255u;
            if (oa != 255u && py >= radius && py < h - radius) opaque_mid = 0;
            o[px] = (oa << 24) | (orr << 16) | (og << 8) | ob;
        }
    }
    return opaque_mid;
}
static const struct ic152 *icache_get152(const struct wimginfo *ii, int w, int h, int radius, int alpha)
{
    if (!ii->serial || w <= 0 || h <= 0) return 0;
    uint64_t bytes64 = (uint64_t)w * (uint64_t)h * 4u;
    if (bytes64 > (uint64_t)SOMA_WEB_IMG_CACHE_KB * 1024u / 3u) return 0;   /* imagenes enormes: camino normal (recortado) */
    uint32_t bytes = (uint32_t)bytes64;
    s_ic152_tick++;
    for (int i = 0; i < 32; i++) {
        struct ic152 *e = &s_ic152[i];
        if (e->px && e->serial == ii->serial && e->w == w && e->h == h && e->radius == radius && e->alpha == alpha) { e->used = s_ic152_tick; return e; }
    }
    /* hueco: la entrada menos usada, liberando hasta caber en el presupuesto */
    for (;;) {
        int victim = -1, empty = -1; uint64_t old = ~0ull;
        for (int i = 0; i < 32; i++) { if (!s_ic152[i].px) { if (empty < 0) empty = i; continue; } if (s_ic152[i].used < old) { old = s_ic152[i].used; victim = i; } }
        if (empty >= 0 && s_ic152_bytes + bytes <= (uint32_t)SOMA_WEB_IMG_CACHE_KB * 1024u) {
            struct ic152 *e = &s_ic152[empty];
            uint32_t *px = (uint32_t *)kmalloc(bytes); if (!px) return 0;
            e->opaque_mid = scale_rows152(ii, w, h, radius, alpha, px);
            e->serial = ii->serial; e->w = w; e->h = h; e->radius = radius; e->alpha = alpha; e->px = px; e->bytes = bytes; e->used = s_ic152_tick;
            s_ic152_bytes += bytes;
            return e;
        }
        if (victim < 0) return 0;
        kfree(s_ic152[victim].px); s_ic152_bytes -= s_ic152[victim].bytes; memset(&s_ic152[victim], 0, sizeof s_ic152[victim]);
    }
}

/* imagen escalada con promedio de muestras; radio opcional */
static void draw_image(struct wpt *P, int x, int y, int w, int h, const struct wimginfo *ii, int radius, int alpha)
{
    if (w <= 0 || h <= 0 || !ii->px || ii->w <= 0 || ii->h <= 0) return;
    int x0 = wmax(x, P->cx0), y0 = wmax(y, P->cy0), x1 = wmin(x + w, P->cx1), y1 = wmin(y + h, P->cy1);
    if (x1 <= x0 || y1 <= y0) return;
    int vw = x1 - x0, vh = y1 - y0;
    int iw = ii->pw > 0 ? ii->pw : ii->w, ih = ii->ph > 0 ? ii->ph : ii->h; int stepx = iw / w, stepy = ih / h;      /* >=1 al reducir */
    if (!soma150_direct_image(w, h, iw, ih, radius, alpha)) {                    /* SOMA_152: cache de la imagen escalada */
        const struct ic152 *c = icache_get152(ii, w, h, radius, alpha);
        if (c) {
            const uint32_t *src = c->px + (size_t)(y0 - y) * (size_t)w + (size_t)(x0 - x);
            int r = radius > w / 2 ? w / 2 : radius; if (r > h / 2) r = h / 2;
            if (c->opaque_mid) {
                /* foto opaca: solo las filas de las esquinas redondeadas necesitan mezcla */
                int top = r - (y0 - y), bot = (y + h - r) - y0;            /* filas [0,top) y [bot,vh) llevan esquina */
                if (top < 0) top = 0;
                if (top > vh) top = vh;
                if (bot < top) bot = top;
                if (bot > vh) bot = vh;
                if (top > 0) fb_blit_alpha(x0, y0, src, vw, top, w);
                if (bot > top) fb_blit(x0, y0 + top, src + (size_t)top * (size_t)w, vw, bot - top, w);
                if (vh > bot) fb_blit_alpha(x0, y0 + bot, src + (size_t)bot * (size_t)w, vw, vh - bot, w);
            } else fb_blit_alpha(x0, y0, src, vw, vh, w);
            return;
        }
    }
#if SOMA_WEB_IMAGE_FASTPATH
    /* Sin escalado ni mascara: usar el bitmap original, sin copiar ni reservar
     * un temporal de la altura completa. fb_blit_alpha conserva la transparencia. */
    if (soma150_direct_image(w, h, iw, ih, radius, alpha)) {
        fb_blit_alpha(x0, y0, ii->px + (size_t)(y0 - y) * (size_t)iw + (size_t)(x0 - x), vw, vh, iw);
        return;
    }
#endif
    int tile_rows = vh;
#if SOMA_WEB_IMAGE_FASTPATH
    tile_rows = soma150_tile_rows(vw, vh);
#endif
    if (tile_rows <= 0) return;
    uint32_t *tmp = (uint32_t *)kmalloc((size_t)vw * (size_t)tile_rows * 4u);
#if SOMA_WEB_IMAGE_FASTPATH
    while (!tmp && tile_rows > 1) { tile_rows = (tile_rows + 1) / 2; if (tile_rows == 1) break;
        tmp = (uint32_t *)kmalloc((size_t)vw * (size_t)tile_rows * 4u); }
    if (!tmp && tile_rows == 1) tmp = (uint32_t *)kmalloc((size_t)vw * 4u);
#endif
    if (!tmp) return;
    int nsx = stepx >= 2 ? wmin(stepx, 3) : 1, nsy = stepy >= 2 ? wmin(stepy, 3) : 1;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    /* tabla de columnas de origen (evita divisiones de 64 bits por pixel) */
    int *xt = (int *)kmalloc((size_t)vw * nsx * sizeof(int)); if (!xt) { kfree(tmp); return; }
    for (int i = 0; i < vw; i++) for (int si = 0; si < nsx; si++) {
        int px = x0 + i - x; int sx = (int)(((int64_t)px * iw * nsx + (int64_t)si * iw + iw / 2) / ((int64_t)w * nsx)); if (sx >= iw) sx = iw - 1; xt[i * nsx + si] = sx;
    }
    int opaque_fast = (nsx == 1 && nsy == 1 && radius == 0 && alpha >= 255);
    for (int row0 = 0; row0 < vh; row0 += tile_rows) {
        int nr = vh - row0 < tile_rows ? vh - row0 : tile_rows;
        for (int j = row0; j < row0 + nr; j++) {
        int py = y0 + j - y;
        if (opaque_fast) {
            int sy = (int)(((int64_t)py * ih + ih / 2) / h); if (sy >= ih) sy = ih - 1;
            const uint32_t *row = ii->px + (size_t)sy * iw; uint32_t *o = tmp + (size_t)(j - row0) * vw;
            for (int i = 0; i < vw; i++) o[i] = row[xt[i]];
            continue;
        }
        int sys[3];
        for (int sj = 0; sj < nsy; sj++) { int sy = (int)(((int64_t)py * ih * nsy + (int64_t)sj * ih + ih / 2) / ((int64_t)h * nsy)); sys[sj] = sy >= ih ? ih - 1 : sy; }
        for (int i = 0; i < vw; i++) {
            int px = x0 + i - x; uint32_t r = 0, g = 0, bl = 0, a = 0, cnt = 0;
            for (int sj = 0; sj < nsy; sj++) {
                const uint32_t *row = ii->px + (size_t)sys[sj] * iw;
                for (int si = 0; si < nsx; si++) {
                    uint32_t c = row[xt[i * nsx + si]], ca = c >> 24;
                    r += ((c >> 16) & 255) * ca; g += ((c >> 8) & 255) * ca; bl += (c & 255) * ca; a += ca; cnt++;
                }
            }
            uint32_t oa = cnt ? a / cnt : 0, orr = a ? r / a : 0, og = a ? g / a : 0, ob = a ? bl / a : 0;
            if (radius > 0) {
                int cxp = px < radius ? radius - px : (px >= w - radius ? px - (w - radius - 1) : 0), cyp = py < radius ? radius - py : (py >= h - radius ? py - (h - radius - 1) : 0);
                if (cxp && cyp) { int d2 = cxp * cxp + cyp * cyp, r2 = radius * radius; if (d2 > r2 + radius) oa = 0; else if (d2 > r2 - radius) oa = oa * (uint32_t)(r2 + radius - d2) / (uint32_t)(2 * radius + 1); }
            }
            oa = oa * (uint32_t)alpha / 255u;
            tmp[(size_t)(j - row0) * (size_t)vw + (size_t)i] = (oa << 24) | (orr << 16) | (og << 8) | ob;
        }
        }
        fb_blit_alpha(x0, y0 + row0, tmp, vw, nr, vw);
    }
    kfree(xt);
    kfree(tmp);
}
static void draw_border(struct wpt *P, int x, int y, int w, int h, const struct wbox *b, int alpha)
{
    const struct wstyle *s = b->st; int r = s->radius;
    int bt = b->bt, br = b->br, bb = b->bb, bl = b->bl;
    if (!(bt || br || bb || bl)) return;
    if (s->border_style[0] >= 4 && s->border_style[1] >= 4 && s->border_style[2] >= 4 && s->border_style[3] >= 4) {
        /* SOMA_188: borde de control como el tema de Chrome: linea de 1px gris redondeada; el resto del grosor es relleno */
        uint32_t c = s->border_color[0] == 1 ? s->color : s->border_color[0]; if ((c & 0xFFFFFF) == 0) c = 0xFF767676u;
        int rr = r > 0 ? r : 2;
        rrect(P, x, y, w, h, rr, mul_alpha(c, alpha));
        uint32_t in = s->bg_color && (s->bg_color >> 24) == 255 ? s->bg_color : (P->bg | 0xFF000000u);
        rrect(P, x + 1, y + 1, w - 2, h - 2, wmax(0, rr - 1), mul_alpha(in, 255));
        return;
    }
    if (r > 0 && bt == br && br == bb && bb == bl && s->border_style[0] == 1) {
        uint32_t c = s->border_color[0] == 1 ? s->color : s->border_color[0];
        rrect(P, x, y, w, h, r, mul_alpha(c, alpha));
        rrect(P, x + bl, y + bt, w - 2 * bl, h - 2 * bt, wmax(0, r - bl), mul_alpha(P->bg | 0xFF000000u, 255));
        return;
    }
    for (int side = 0; side < 4; side++) {
        int wd = side == 0 ? bt : side == 1 ? br : side == 2 ? bb : bl; if (!wd) continue;
        uint32_t c = s->border_color[side] == 1 ? s->color : s->border_color[side]; c = mul_alpha(c, alpha);
        int rx, ry, rw, rh;
        switch (side) { case 0: rx = x; ry = y; rw = w; rh = wd; break; case 1: rx = x + w - wd; ry = y + bt; rw = wd; rh = h - bt - bb; break; case 2: rx = x; ry = y + h - wd; rw = w; rh = wd; break; default: rx = x; ry = y + bt; rw = wd; rh = h - bt - bb; break; }
        int st = s->border_style[side];
        if (st == 1 || st >= 4) fillr(P, rx, ry, rw, rh, c);
        else {
            int seg = st == 3 ? wd : wd * 3, gap = st == 3 ? wd : wd * 2; if (seg < 2) seg = 2; if (gap < 2) gap = 2;
            if (side & 1) { for (int p = 0; p < rh; p += seg + gap) fillr(P, rx, ry + p, rw, wmin(seg, rh - p), c); }
            else { for (int p = 0; p < rw; p += seg + gap) fillr(P, rx + p, ry, wmin(seg, rw - p), rh, c); }
        }
    }
}
static void draw_bgimage(struct wpt *P, const struct wbox *b, int x, int y, int w, int h, int alpha)
{
    const struct wstyle *s = b->st; struct wimginfo ii;
    if (!s->bg_image || !img_info(P->L, s->bg_image, &ii) || ii.state != 2 || ii.w <= 0 || ii.h <= 0) return;
    int px = x + b->bl, py = y + b->bt, pw = w - b->bl - b->br, ph = h - b->bt - b->bb;
    int dw = ii.w, dh = ii.h;
    switch (s->bg_size_mode) {
    case 1: { int64_t sx = (int64_t)pw * 1000 / ii.w, sy = (int64_t)ph * 1000 / ii.h, sc = sx > sy ? sx : sy; dw = (int)(ii.w * sc / 1000); dh = (int)(ii.h * sc / 1000); break; }
    case 2: { int64_t sx = (int64_t)pw * 1000 / ii.w, sy = (int64_t)ph * 1000 / ii.h, sc = sx < sy ? sx : sy; dw = (int)(ii.w * sc / 1000); dh = (int)(ii.h * sc / 1000); break; }
    case 3: { if (s->bg_w.kind == LK_VAL) dw = lenv(&s->bg_w, pw, ii.w); if (s->bg_h.kind == LK_VAL) dh = lenv(&s->bg_h, ph, ii.h); else if (s->bg_w.kind == LK_VAL) dh = (int)((int64_t)ii.h * dw / ii.w); break; }
    default: break;
    }
    if (dw <= 0 || dh <= 0) return;
    int ox = px + (int)((int64_t)(pw - dw) * s->bg_pos_x / 100) + s->bg_off_x, oy = py + (int)((int64_t)(ph - dh) * s->bg_pos_y / 100) + s->bg_off_y;
    struct wpt Q = *P; clip_isect(&Q, px, py, pw, ph);
    int rep = s->bg_repeat;     /* SOMA_181: 0 repetir, 1 repeat-x, 2 repeat-y, 3 no-repeat (el pintado tenia 1=no-repeat, 2=x, 3=y: no-repeat se repetia en vertical) */
    int repx = wl181_off() ? (rep == 0 || rep == 2) : (rep == 0 || rep == 1), repy = wl181_off() ? (rep == 0 || rep == 3) : (rep == 0 || rep == 2);
    int tx0 = ox, ty0 = oy, tx1 = ox + dw, ty1 = oy + dh;
    if (repx) { while (tx0 > px) tx0 -= dw; tx1 = px + pw; }
    if (repy) { while (ty0 > py) ty0 -= dh; ty1 = py + ph; }
    int count = 0;
    for (int ty = ty0; ty < ty1 && count < 400; ty += dh) for (int tx = tx0; tx < tx1 && count < 400; tx += dw) { draw_image(&Q, tx, ty, dw, dh, &ii, 0, alpha); count++; }
    P->cx0 = Q.cx0 * 0 + P->cx0; clip_apply(P);
}

static void paint_box(struct wpt *P, struct wbox *b, int ox, int oy, int alpha, int depth);

static int first_baseline(const struct wbox *b, int depth)
{
    if (depth > 20) return -1;
    if (b->nfrags) return b->frags[0].y;
    for (const struct wbox *c = b->first; c; c = c->next) { if (c->flags & BF_ABS) continue; int f = first_baseline(c, depth + 1); if (f >= 0) return c->y + f; }
    return -1;
}
static void roman_str(int n, char *o, int upper)
{
    static const int v[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const char *r[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
    int k = 0; if (n <= 0 || n > 3999) { o[0] = 0; return; }
    for (int i = 0; i < 13; i++) while (n >= v[i]) { for (const char *p = r[i]; *p; p++) o[k++] = upper ? (char)(*p - 32) : *p; n -= v[i]; }
    o[k] = 0;
}
static void paint_marker(struct wpt *P, struct wbox *b, int x, int y, int alpha)
{
    const struct wstyle *s = b->st; if (s->list_style == LS_NONE) return;
    int px = s->font_px, fb = first_baseline(b, 0); if (fb < 0) fb = b->pt + b->bt + wf_ascent(px);
    int base = y + fb; uint32_t col = mul_alpha(s->color, alpha);
    int right = x - px / 3 - 2; if (s->list_inside) right = x + b->bl + b->pl - 2;
    if (s->list_style == LS_DISC || s->list_style == LS_CIRCLE || s->list_style == LS_SQUARE) {
        int d = wmax(5, px * 3 / 8), cx_ = right - d - px / 8, cy_ = base - px * 3 / 10 - d / 2 - 1;
        if (s->list_style == LS_SQUARE) fillr(P, cx_, cy_, d, d, col);
        else if (s->list_style == LS_DISC) rrect(P, cx_, cy_, d, d, d / 2, col);
        else { rrect(P, cx_, cy_, d, d, d / 2, col); rrect(P, cx_ + 1, cy_ + 1, d - 2, d - 2, wmax(0, d / 2 - 1), 0xFF000000u | P->bg); }
        return;
    }
    char t[40]; int n = b->list_n;
    if (s->list_style == LS_DECIMAL) wu_itoa(n, t);
    else if (s->list_style == LS_ALPHA || s->list_style == LS_UALPHA) { int a = n > 0 ? (n - 1) % 26 : 0; t[0] = (char)((s->list_style == LS_ALPHA ? 'a' : 'A') + a); t[1] = 0; }
    else roman_str(n, t, 0);
    int len = wu_strlen(t); t[len++] = '.'; t[len] = 0;
    int w = textw(s, t, len); wf_draw(right - w, base, t, len, px, s->color & 0xFFFFFF, fontfl(s), alpha);
}

/* SOMA_188: box-shadow inset: franjas concentricas desde el borde interior; grosor por lado = spread +/- desplazamiento,
   difuminado lineal sobre el blur (aprox. de la gaussiana). Cada pixel se pinta una sola vez. */
static int ish_alpha(int d, int t, int blur)
{
    int h = blur / 2; if (t <= 0 && !blur) return 0;
    if (d < t - h) return 256;
    if (d >= t + h + (blur & 1)) return 0;
    if (!blur) return d < t ? 256 : 0;
    return (int)(256 * (t + h + (blur & 1) - d) / (blur + 1));
}
static void paint_inset_shadow(struct wpt *P, const struct wstyle *s, int x, int y, int w, int h, int alpha)
{
    if (w <= 0 || h <= 0) return;
    uint32_t c = mul_alpha(s->ish_color, alpha); if (!(c >> 24)) return;
    int sp = s->ish_spread, bl = s->ish_blur;
    int tt = sp + s->ish_y, tb = sp - s->ish_y, tlf = sp + s->ish_x, tr = sp - s->ish_x;
    int maxd = wmax(wmax(tt, tb), wmax(tlf, tr)) + bl; if (maxd <= 0) return;
    if (maxd > wmin(w, h) / 2 + 1) maxd = wmin(w, h) / 2 + 1;
    uint32_t ca = c >> 24, rgb = c & 0xFFFFFF;
    for (int d = 0; d < maxd; d++) {
        int iw = w - 2 * d, ih = h - 2 * d; if (iw <= 0 || ih <= 0) break;
        int at = ish_alpha(d, tt, bl), ab = ish_alpha(d, tb, bl), al = ish_alpha(d, tlf, bl), ar = ish_alpha(d, tr, bl);
        if (at) fillr(P, x + d, y + d, iw, 1, ((ca * (uint32_t)at >> 8) << 24) | rgb);
        if (ab && ih > 1) fillr(P, x + d, y + h - 1 - d, iw, 1, ((ca * (uint32_t)ab >> 8) << 24) | rgb);
        if (al && ih > 2) fillr(P, x + d, y + d + 1, 1, ih - 2, ((ca * (uint32_t)al >> 8) << 24) | rgb);
        if (ar && ih > 2 && iw > 1) fillr(P, x + w - 1 - d, y + d + 1, 1, ih - 2, ((ca * (uint32_t)ar >> 8) << 24) | rgb);
    }
}
static uint32_t parse_hex_attr(const char *v)
{
    if (!v) return 0xFF000000u;
    int ok; uint32_t c = wc_parse_color(v, &ok); return ok ? (c | 0xFF000000u) : 0xFF000000u;
}
static void line_h(struct wpt *P, int x0, int x1, int y, uint32_t c) { fillr(P, x0, y, x1 - x0, 1, c); }

static void paint_ctl(struct wpt *P, struct wbox *b, int x, int y, int alpha)
{
    const struct wstyle *s = b->st; struct wnode *n = b->node; struct wlay *L = P->L;
    int cx_ = x + b->bl + b->pl, cy_ = y + b->bt + b->pt, cw = b->w - b->bl - b->br - b->pl - b->pr, ch = b->cont_h;
    int px = s->font_px, fl = fontfl(s); uint32_t col = mul_alpha(s->color, alpha);
    int asc, dsc; txt_metrics(s, &asc, &dsc); (void)dsc;
    int disabled = (n->flags & WNF_DISABLED) != 0 || wd_attr(n, "disabled") != 0;
    if (disabled) col = (col & 0xFF000000u) | 0x00808080u;
    switch (b->ctl) {
    case CTL_IMG: {
        struct wimginfo ii; const char *src = wl_img_src(n);
        if (src && img_info(L, src, &ii) && ii.state == 2) {
            if (s->obj_fit && !wl181_off() && ii.w > 0 && ii.h > 0 && cw > 0 && ch > 0) {   /* SOMA_181: object-fit / object-position */
                int64_t sx = (int64_t)cw * 1000 / ii.w, sy = (int64_t)ch * 1000 / ii.h, sc = 1000;
                switch (s->obj_fit) { case 1: sc = sx < sy ? sx : sy; break; case 2: sc = sx > sy ? sx : sy; break; case 4: sc = (sx < sy ? sx : sy); if (sc > 1000) sc = 1000; break; default: sc = 1000; break; }
                int dw = (int)(ii.w * sc / 1000), dh = (int)(ii.h * sc / 1000); if (dw < 1) dw = 1; if (dh < 1) dh = 1;
                int ox = cx_ + (int)((int64_t)(cw - dw) * s->obj_pos_x / 100), oy = cy_ + (int)((int64_t)(ch - dh) * s->obj_pos_y / 100);
                struct wpt Q = *P; clip_isect(&Q, cx_, cy_, cw, ch); draw_image(&Q, ox, oy, dw, dh, &ii, 0, alpha); clip_apply(P);
            } else draw_image(P, cx_, cy_, cw, ch, &ii, s->radius, alpha);
        }
        else {
            if (n->tag == WT_SVG) { /* sin pasarela no hay dibujo: se deja el hueco */ }
            else if (ii.state == 3 || !src) {
                fillr(P, cx_, cy_, cw, ch, mul_alpha(0xFFF2F2F2u, alpha));
                const char *alt = wd_attr(n, "alt");
                if (alt && *alt) { struct wpt Q = *P; clip_isect(&Q, cx_, cy_, cw, ch); wf_draw(cx_ + 2, cy_ + asc, alt, wu_strlen(alt), px, 0x666666, 0, alpha); clip_apply(P); }
                fillr(P, cx_, cy_, cw, 1, mul_alpha(0xFFBBBBBBu, alpha)); fillr(P, cx_, cy_ + ch - 1, cw, 1, mul_alpha(0xFFBBBBBBu, alpha));
                fillr(P, cx_, cy_, 1, ch, mul_alpha(0xFFBBBBBBu, alpha)); fillr(P, cx_ + cw - 1, cy_, 1, ch, mul_alpha(0xFFBBBBBBu, alpha));
            } else if (cw > 24 && ch > 24) fillr(P, cx_, cy_, cw, ch, mul_alpha(0x18808080u, alpha));
        }
        break; }
    case CTL_TEXT: case CTL_PASSWORD: case CTL_TEXTAREA: {
        const char *t = wl_ctl_text(n); uint32_t tl = wu_strlen(t); int showph = 0;
        char *pw = 0;
        if (!tl) { const char *ph = wd_attr(n, "placeholder"); if (ph && *ph) { t = ph; tl = wu_strlen(ph); showph = 1; } }
        if (b->ctl == CTL_PASSWORD && !showph && tl) {
            uint32_t cnt = 0, i = 0, adv; while (i < tl) { wu_utf8_next(t + i, &adv); i += adv; cnt++; }
            pw = (char *)kmalloc(cnt * 3 + 1); if (pw) { for (uint32_t k = 0; k < cnt; k++) { pw[k * 3] = (char)0xE2; pw[k * 3 + 1] = (char)0x80; pw[k * 3 + 2] = (char)0xA2; } pw[cnt * 3] = 0; t = pw; tl = cnt * 3; }
        }
        struct wpt Q = *P; clip_isect(&Q, cx_, cy_, cw, wmax(ch, 1));
        uint32_t tc = col; int tal = alpha;
        if (showph) {   /* SOMA_188: ::placeholder (color con alfa, cursiva, negrita); por defecto el gris de Chrome */
            uint32_t pc = s->ph_color ? s->ph_color : 0xFF757575u; if (pc == 0x00000001u) pc = 0;
            tc = pc; tal = (int)((uint32_t)alpha * (pc >> 24) / 255u);
            if (s->ph_italic >= 0) fl = (fl & ~WF_ITALIC) | (s->ph_italic ? WF_ITALIC : 0);
            if (s->ph_bold >= 0) fl = (fl & ~WF_BOLD) | (s->ph_bold ? WF_BOLD : 0);
        }
        int is_focus = (L->focus == n);
        if (b->ctl == CTL_TEXTAREA) {
            int lh = wc_line_px(s), line = 0; uint32_t ls = 0;
            for (uint32_t i = 0; i <= tl; i++) {
                if (i == tl || t[i] == '\n') { if (i > ls) wf_draw(cx_, cy_ + line * lh + asc, t + ls, i - ls, px, tc & 0xFFFFFF, fl, tal); line++; ls = i + 1; if (line * lh > ch + lh) break; }
            }
            if (is_focus && L->caret_on) { int cur = n->sel < 0 ? 0 : (n->sel > (int)tl ? (int)tl : n->sel); int ln = 0; uint32_t st0 = 0; for (int i = 0; i < cur; i++) if (t[i] == '\n') { ln++; st0 = i + 1; } int cxp = textw(s, t + st0, cur - st0); fillr(&Q, cx_ + cxp, cy_ + ln * lh + 1, 1, lh - 2, col); }
        } else {
            int cur = n->sel < 0 ? 0 : (n->sel > (int)tl ? (int)tl : n->sel); if (showph) cur = 0;
            int carx = textw(s, t, cur), off = carx > cw - 3 ? carx - cw + 3 : 0;
            int ty = cy_ + (ch - (asc + dsc)) / 2 + asc;
            wf_draw(cx_ - off, ty, t, tl, px, tc & 0xFFFFFF, fl, tal);
            if (is_focus && L->caret_on) fillr(&Q, cx_ + carx - off, cy_ + 1, 1, ch - 2, col);
        }
        if (pw) kfree(pw);
        clip_apply(P);
        break; }
    case CTL_CHECK: case CTL_RADIO: {
        int checked = (n->flags & WNF_CHECKED) != 0, rad = b->ctl == CTL_RADIO;
        uint32_t bc = disabled ? 0xFFBBBBBBu : 0xFF767676u;
        rrect(P, cx_, cy_, 13, 13, rad ? 6 : 2, mul_alpha(checked ? 0xFF2563EBu : bc, alpha));
        rrect(P, cx_ + 1, cy_ + 1, 11, 11, rad ? 5 : 1, mul_alpha(checked ? 0xFF2563EBu : 0xFFFFFFFFu, alpha));
        if (checked) {
            if (rad) rrect(P, cx_ + 4, cy_ + 4, 5, 5, 2, mul_alpha(0xFFFFFFFFu, alpha));
            else { for (int i = 0; i < 3; i++) { fillr(P, cx_ + 3 + i, cy_ + 6 + i, 2, 2, mul_alpha(0xFFFFFFFFu, alpha)); } for (int i = 0; i < 5; i++) fillr(P, cx_ + 5 + i, cy_ + 8 - i, 2, 2, mul_alpha(0xFFFFFFFFu, alpha)); }
        }
        break; }
    case CTL_BTN_INPUT: {
        const char *t = wl_ctl_text(n); int tl = wu_strlen(t), tw_ = textw(s, t, tl);
        wf_draw(cx_ + (cw - tw_) / 2, cy_ + (ch - (asc + dsc)) / 2 + asc, t, tl, px, col & 0xFFFFFF, fl, alpha); break; }
    case CTL_SELECT: {
        const char *t = wl_ctl_text(n); int tl = wu_strlen(t);
        int sx0 = wl181_off() ? 0 : wmax(0, 4 - b->pl);   /* SOMA_188: margen interno del menu (Chrome) con padding 0 */
        struct wpt Q = *P; clip_isect(&Q, cx_, cy_, cw - 14 + sx0 / 2, ch);
        wf_draw(cx_ + sx0, cy_ + (ch - (asc + dsc)) / 2 + asc, t, tl, px, col & 0xFFFFFF, fl, alpha);
        clip_apply(P);
        int ax = cx_ + cw - 12, ay = cy_ + ch / 2 - 2;
        for (int i = 0; i < 4; i++) fillr(P, ax + i, ay + i, 9 - 2 * i, 1, col);
        break; }
    case CTL_RANGE: {
        const char *mn = wd_attr(n, "min"), *mx = wd_attr(n, "max"); int lo = mn ? wu_atoi(mn) : 0, hi = mx ? wu_atoi(mx) : 100; const char *v = n->value ? n->value : wd_attr(n, "value");
        int val = v ? wu_atoi(v) : (lo + hi) / 2; if (hi <= lo) hi = lo + 1;
        int kx = cx_ + 6 + (cw - 12) * wclamp(val - lo, 0, hi - lo) / (hi - lo);
        rrect(P, cx_, cy_ + ch / 2 - 2, cw, 4, 2, mul_alpha(0xFFC8CCD2u, alpha)); rrect(P, cx_, cy_ + ch / 2 - 2, kx - cx_, 4, 2, mul_alpha(0xFF2563EBu, alpha));
        rrect(P, kx - 6, cy_ + ch / 2 - 6, 12, 12, 6, mul_alpha(0xFF2563EBu, alpha)); break; }
    case CTL_COLOR: { const char *v = n->value ? n->value : wd_attr(n, "value"); rrect(P, cx_, cy_, cw, ch, 2, mul_alpha(parse_hex_attr(v), alpha)); break; }
    case CTL_FILE: {
        fillr(P, cx_, cy_, 90, ch, mul_alpha(0xFFE9EAEDu, alpha)); wf_draw(cx_ + 6, cy_ + (ch - (asc + dsc)) / 2 + asc, "Choose file", 11, px, 0x222222, fl, alpha);
        wf_draw(cx_ + 98, cy_ + (ch - (asc + dsc)) / 2 + asc, "No file chosen", 14, px, 0x666666, fl, alpha); break; }
    default:
        if (n->tag == WT_VIDEO || n->tag == WT_AUDIO || n->tag == WT_IFRAME) {
            fillr(P, cx_, cy_, cw, ch, mul_alpha(n->tag == WT_IFRAME ? 0xFFF4F4F4u : 0xFF111111u, alpha));
            if (n->tag != WT_AUDIO && cw > 40 && ch > 30) { int cxm = cx_ + cw / 2, cym = cy_ + ch / 2; for (int i = 0; i < 10; i++) fillr(P, cxm - 5 + i / 2, cym - 10 + i, 10 - i, 1, mul_alpha(n->tag == WT_IFRAME ? 0xFF999999u : 0xFFFFFFFFu, alpha)); for (int i = 0; i < 10; i++) fillr(P, cxm - 5, cym + i, 1, 1, 0); }
        }
        break;
    }
    if (L->focus == n && b->ctl != CTL_TEXT && b->ctl != CTL_PASSWORD && b->ctl != CTL_TEXTAREA) {
        uint32_t fc = mul_alpha(0xFF3B82F6u, alpha); fillr(P, x - 1, y - 1, b->w + 2, 2, fc); fillr(P, x - 1, y + b->h - 1, b->w + 2, 2, fc); fillr(P, x - 1, y - 1, 2, b->h + 2, fc); fillr(P, x + b->w - 1, y - 1, 2, b->h + 2, fc);
    }
}

static void paint_text_lines(struct wpt *P, struct wbox *b, int x, int y, int alpha)
{
    for (int i = 0; i < b->ndecos; i++) {
        const struct wdeco *d = &b->decos[i]; const struct wstyle *s = d->st;
        int dx = x + d->x0, dy = y + d->y0, dw = d->x1 - d->x0, dh = d->y1 - d->y0;
        if (dy > P->cy1 || dy + dh < P->cy0) continue;
        if (s->bg_color) rrect(P, dx, dy, dw, dh, s->radius, mul_alpha(s->bg_color, alpha));
        if (s->has_grad) gradient(P, dx, dy, dw, dh, s->grad_a, s->grad_b, s->grad_dir, alpha);
        for (int sd = 0; sd < 4; sd++) {
            int wd = s->border_style[sd] ? s->border_w[sd] : 0; if (!wd) continue;
            uint32_t c = mul_alpha(s->border_color[sd] == 1 ? s->color : s->border_color[sd], alpha);
            if (sd == 0) fillr(P, dx, dy, dw, wd, c); else if (sd == 2) fillr(P, dx, dy + dh - wd, dw, wd, c);
            else if (sd == 3 && d->first) fillr(P, dx, dy, wd, dh, c); else if (sd == 1 && d->last) fillr(P, dx + dw - wd, dy, wd, dh, c);
        }
    }
    /* SOMA_188: text-overflow: ellipsis (una linea) y -webkit-line-clamp (ultima linea visible) terminan en ... */
    const struct wstyle *bs = (b->st && !(b->flags & BF_ANON)) ? b->st : 0;
    for (struct wbox *q = b; !bs && q; q = q->parent) if (q->st && !(q->flags & BF_ANON)) bs = q->st;
    int ell = bs && bs->text_overflow && bs->overflow_x && !wl181_off(), limit = b->w - b->br - b->pr;
    int clamp_y = INT32_MIN, done_y = INT32_MIN;
    if (bs && bs->line_clamp && bs->overflow_y && !wl181_off()) {
        int nl = 0, ly = INT32_MIN; for (int i = 0; i < b->nfrags; i++) if (b->frags[i].y != ly) { ly = b->frags[i].y; if (++nl == bs->line_clamp) clamp_y = ly; }
        if (nl <= bs->line_clamp) clamp_y = INT32_MIN;
    }
    for (int i = 0; i < b->nfrags; i++) {
        const struct wfrag *f = &b->frags[i]; const struct wstyle *s = f->st; int by = y + f->y;
        if (f->y == done_y) continue;
        if (by - f->asc > P->cy1 || by + f->dsc < P->cy0 || x + f->x > P->cx1 || x + f->x + f->w < P->cx0) continue;
        if (s->visibility || s->fs_zero) continue;   /* SOMA_188: font-size: 0 no se dibuja */
        int last_on_line = i + 1 >= b->nfrags || b->frags[i + 1].y != f->y;
        if ((ell && f->x + f->w > limit) || (f->y == clamp_y && last_on_line)) {
            static const char E3[] = "\xE2\x80\xA6"; int fl2 = fontfl(s), ew = textw(s, E3, 3);
            int room = (ell ? limit : limit) - f->x - ew; uint32_t n2 = 0;
            if (f->y == clamp_y && !(ell && f->x + f->w > limit) && f->x + f->w + ew <= limit) n2 = f->len;
            else while (n2 < f->len) { uint32_t nx = n2 + 1; while (nx < f->len && ((unsigned char)f->txt[nx] & 0xC0) == 0x80) nx++; if (textw(s, f->txt, nx) > room) break; n2 = nx; }
            while (n2 > 0 && f->txt[n2 - 1] == ' ') n2--;
            int a8 = (int)(((s->color >> 24) * (uint32_t)alpha) / 255);
            if (n2) wf_draw(x + f->x, by, f->txt, n2, s->font_px, s->color & 0xFFFFFF, fl2, a8);
            int tw2 = n2 ? textw(s, f->txt, n2) : 0;
            wf_draw(x + f->x + tw2, by, E3, 3, s->font_px, s->color & 0xFFFFFF, fl2, a8);
            if (s->text_decor & 1) fillr(P, x + f->x, by + s->font_px / 10 + 1, tw2 + ew, wmax(1, s->font_px / 14), mul_alpha(s->color, alpha));
            done_y = f->y; continue;
        }
        wf_draw(x + f->x, by, f->txt, f->len, s->font_px, s->color & 0xFFFFFF, fontfl(s), (int)(((s->color >> 24) * (uint32_t)alpha) / 255));
        if (s->text_decor) {
            int th = wmax(1, s->font_px / 14); uint32_t c = mul_alpha(s->color, alpha);
            if (s->text_decor & 1) fillr(P, x + f->x, by + s->font_px / 10 + 1, f->w, th, c);
            if (s->text_decor & 2) fillr(P, x + f->x, by - s->font_px * 3 / 10, f->w, th, c);
            if (s->text_decor & 4) fillr(P, x + f->x, by - wf_ascent(s->font_px), f->w, th, c);
        }
    }
}

/* SOMA_181: desplazamiento interno de cajas con overflow */
static int box_scroll_max(const struct wbox *b)
{
    const struct wstyle *s = b->st;
    if (!s || (b->flags & BF_ANON) || b->kind == BK_REPL || s->overflow_y == 0 || b->node == 0 || wl181_off()) return 0;
    int m = b->sch - b->cont_h; return m > 0 ? m : 0;
}
static int box_scroll(const struct wbox *b)
{
    int m = box_scroll_max(b); if (m <= 0 || !b->node) return 0;
    int v = b->node->scy; return v < 0 ? 0 : v > m ? m : v;
}
/* SOMA_181: position: sticky (solo con top en px/%): el cuadro se queda pegado al borde superior del visor sin salir de su padre.
 * y_cur: y actual del cuadro en el sistema de coordenadas de y_view (borde superior del visor en ese mismo sistema). */
static int sticky_dy(const struct wlay *L, const struct wbox *b, int y_cur, int y_view)
{
    const struct wstyle *s = b->st;
    if (!s || s->position != P_STICKY || s->top.kind != LK_VAL || (b->flags & BF_ANON) || wl181_off()) return 0;
    int top = lenv(&s->top, L->vh, 0);
    int dy = y_view + top - y_cur; if (dy <= 0) return 0;
    const struct wbox *par = b->parent;
    if (par) {
        int oy = y_cur - b->y - b->rdy;                                  /* origen (borde superior) del padre */
        int room = oy + par->h - par->pb - par->bb - b->mb - (y_cur + b->h);
        if (room < dy) dy = room;
    }
    return dy > 0 ? dy : 0;
}

/* SOMA_181: posicionados relativos con z-index > 0 y sticky se pintan (y reciben el clic) por encima del resto del flujo de su padre */
static int is_lifted(const struct wbox *c) { const struct wstyle *s = c->st; return s && !(c->flags & BF_ANON) && !wl181_off() && (s->position == P_STICKY || (s->position == P_RELATIVE && s->z_index > 0)); }
static void paint_abs_list(struct wpt *P, struct wbox *cb, int x, int y, int alpha, int depth, int fixed_pass)
{
    struct wbox *arr[64]; int n = 0;
    for (struct wbox *a = cb->abs_chain; a; a = a->abs_next) { if (!!(a->flags & BF_FIXED) != fixed_pass) continue; if (n < 64) arr[n++] = a; }
    /* la cadena esta en orden inverso de construccion */
    for (int i = 0; i < n / 2; i++) { struct wbox *t = arr[i]; arr[i] = arr[n - 1 - i]; arr[n - 1 - i] = t; }
    for (int i = 1; i < n; i++) { struct wbox *t = arr[i]; int j = i - 1; while (j >= 0 && arr[j]->zi > t->zi) { arr[j + 1] = arr[j]; j--; } arr[j + 1] = t; }
    for (int i = 0; i < n; i++) paint_box(P, arr[i], fixed_pass ? P->vx : x, fixed_pass ? P->vy : y, alpha, depth + 1);   /* SOMA_181: fixed es relativo al visor, no a la pantalla */
}

/* SOMA_188: transform rotate()/scale() en cajas decorativas (iconos CSS: X, chevrones, hamburguesas). Aritmetica entera:
 * seno en 1/16384, coordenadas en 1/256 px, relleno convexo con 4 subfilas y cobertura horizontal exacta. */
static const uint16_t SIN90[91] = { 0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845, 3126, 3406, 3686, 3964, 4240, 4516, 4790, 5063, 5334, 5604, 5872, 6138, 6402, 6664,
    6924, 7182, 7438, 7692, 7943, 8192, 8438, 8682, 8923, 9162, 9397, 9630, 9860, 10087, 10311, 10531, 10749, 10963, 11174, 11381, 11585, 11786, 11982, 12176, 12365, 12551, 12733, 12911,
    13085, 13255, 13421, 13583, 13741, 13894, 14044, 14189, 14330, 14466, 14598, 14726, 14849, 14968, 15082, 15191, 15296, 15396, 15491, 15582, 15668, 15749, 15826, 15897, 15964, 16026,
    16083, 16135, 16182, 16225, 16262, 16294, 16322, 16344, 16362, 16374, 16382, 16384 };
static int isin10(int d10)
{
    d10 = ((d10 % 3600) + 3600) % 3600; int q = d10 / 900, r = d10 % 900, sgn = q >= 2 ? -1 : 1; if (q & 1) r = 900 - r;
    int a = r / 10, f = r % 10; int v = SIN90[a] + (a < 90 ? (SIN90[a + 1] - SIN90[a]) * f / 10 : 0);
    return sgn * v;
}
static int icos10(int d10) { return isin10(d10 + 900); }
static void fill_quad(struct wpt *P, const int64_t *qx, const int64_t *qy, int n, uint32_t argb)
{
    int64_t ymin = qy[0], ymax = qy[0], xmin = qx[0], xmax = qx[0];
    for (int i = 1; i < n; i++) { if (qy[i] < ymin) ymin = qy[i]; if (qy[i] > ymax) ymax = qy[i]; if (qx[i] < xmin) xmin = qx[i]; if (qx[i] > xmax) xmax = qx[i]; }
    int px0 = (int)(xmin >> 8) - 1, px1 = (int)(xmax >> 8) + 2, py0 = (int)(ymin >> 8), py1 = (int)(ymax >> 8) + 1;
    if (px0 < P->cx0) px0 = P->cx0;
    if (px1 > P->cx1) px1 = P->cx1;
    if (py0 < P->cy0) py0 = P->cy0;
    if (py1 > P->cy1) py1 = P->cy1;
    int w = px1 - px0; if (w <= 0 || py1 <= py0 || w > 4096) return;
    uint16_t *cov = (uint16_t *)kmalloc((uint32_t)w * 2u); if (!cov) return;
    int a0 = (int)(argb >> 24);
    for (int py = py0; py < py1; py++) {
        memset(cov, 0, (size_t)w * 2u); int any = 0;
        for (int k = 0; k < 4; k++) {
            int64_t sy = ((int64_t)py << 8) + 32 + 64 * k, xl = INT64_MAX, xr = INT64_MIN;
            for (int i = 0; i < n; i++) {
                int64_t ax = qx[i], ay = qy[i], bx = qx[(i + 1) % n], by = qy[(i + 1) % n];
                if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) { int64_t xx = ax + (bx - ax) * (sy - ay) / (by - ay); if (xx < xl) xl = xx; if (xx > xr) xr = xx; }
            }
            if (xl >= xr) continue;
            any = 1;
            for (int64_t cx = xl >> 8; cx <= (xr >> 8); cx++) {
                int ix = (int)cx - px0; if (ix < 0 || ix >= w) continue;
                int64_t l = cx << 8, r = l + 256; if (xl > l) l = xl; if (xr < r) r = xr;
                if (r > l) cov[ix] = (uint16_t)(cov[ix] + (r - l));
            }
        }
        if (!any) continue;
        for (int i = 0; i < w; i++) if (cov[i]) { int a = a0 * (cov[i] > 1024 ? 1024 : cov[i]) / 1024; if (a > 0) fb_fill_rect_alpha(px0 + i, py, 1, 1, ((uint32_t)a << 24) | (argb & 0xFFFFFF)); }
    }
    kfree(cov);
}
static void xf_rect(struct wpt *P, int64_t cx, int64_t cy, int rx0, int ry0, int rx1, int ry1, int s10, int c10, int scx, int scy, uint32_t argb)
{   /* rectangulo relativo al centro (px) -> escalado, girado y trasladado */
    int64_t qx[4], qy[4]; int xs[4] = { rx0, rx1, rx1, rx0 }, ys[4] = { ry0, ry0, ry1, ry1 };
    for (int i = 0; i < 4; i++) {
        int64_t px = (int64_t)xs[i] * 256 * scx / 100, py = (int64_t)ys[i] * 256 * scy / 100;
        qx[i] = cx + (px * c10 - py * s10) / 16384; qy[i] = cy + (px * s10 + py * c10) / 16384;
    }
    fill_quad(P, qx, qy, 4, argb);
}
static void paint_xform_box(struct wpt *P, struct wbox *b, int x, int y, int a2)
{
    const struct wstyle *s = b->st; int sn = isin10(s->rot10), cs = icos10(s->rot10);
    int scx = s->sc_x ? s->sc_x : 100, scy = s->sc_y ? s->sc_y : 100;
    int64_t cx = ((int64_t)x << 8) + ((int64_t)b->w << 7), cy = ((int64_t)y << 8) + ((int64_t)b->h << 7);
    int hw = b->w / 2, hh = b->h / 2, rw = b->w - hw, rh = b->h - hh;
    if (s->bg_color) xf_rect(P, cx, cy, -hw, -hh, rw, rh, sn, cs, scx, scy, mul_alpha(s->bg_color, a2));
    for (int side = 0; side < 4; side++) {
        int bw = side == 0 ? b->bt : side == 1 ? b->br : side == 2 ? b->bb : b->bl; uint32_t bc = s->border_color[side];
        if (bw <= 0 || !s->border_style[side] || !(bc >> 24)) continue;
        bc = mul_alpha(bc, a2);
        if (side == 0) xf_rect(P, cx, cy, -hw, -hh, rw, -hh + bw, sn, cs, scx, scy, bc);
        else if (side == 1) xf_rect(P, cx, cy, rw - bw, -hh, rw, rh, sn, cs, scx, scy, bc);
        else if (side == 2) xf_rect(P, cx, cy, -hw, rh - bw, rw, rh, sn, cs, scx, scy, bc);
        else xf_rect(P, cx, cy, -hw, -hh, -hw + bw, rh, sn, cs, scx, scy, bc);
    }
}
static void paint_box(struct wpt *P, struct wbox *b, int ox, int oy, int alpha, int depth)
{
    if (depth > 90 || web_stack_left() < 4000) return;
    const struct wstyle *s = b->st; struct wlay *L = P->L;
    int x = ox + b->x + b->rdx, y = oy + b->y + b->rdy;
    int anon = (b->flags & BF_ANON) != 0;
    if (s && !anon && s->display == D_NONE) return;
    if (s && s->position == P_STICKY) y += sticky_dy(L, b, y, P->vy);
    int a2 = (s && !anon && s->opacity < 255) ? alpha * s->opacity / 255 : alpha;
    if (a2 <= 0) return;
    if (b->h > 0 && b->abs_chain == 0 && (y > P->cy1 + 600 || y + b->h < P->cy0 - 600)) return;
    if (s && !anon && !wl181_off()) {   /* SOMA_188: transform scale(0) oculta; rotate/scale en cajas decorativas se dibujan transformadas */
        if (s->sc_x == -32768 || s->sc_y == -32768) return;
        if ((s->rot10 || (s->sc_x && s->sc_x != 100) || (s->sc_y && s->sc_y != 100)) && !b->first && !b->nfrags && !b->abs_chain && b->kind != BK_REPL && !s->visibility && s->radius == 0 && b->w > 0 && b->h > 0) {
            clip_apply(P); paint_xform_box(P, b, x, y, a2); return;
        }
    }
    struct wpt save = *P; uint32_t save_bg = P->bg;
    int vis = !(s && !anon && s->visibility);
    /* SOMA_PARCHE_028_UI: overflow:hidden con border-radius -> el contenido (p. ej. la foto de una tarjeta) se recorta al arco */
    uint32_t *rc_buf = 0; int rc_r = 0;
    if (s && !anon && vis && s->radius > 0 && (s->overflow_x || s->overflow_y) && b != L->root && b->kind != BK_REPL && (b->first || b->items) &&
        !(y > P->cy1 || y + b->h < P->cy0 || x > P->cx1 || x + b->w < P->cx0)) {
        rc_r = wmin(wmin(s->radius, 48), wmin(b->w, b->h) / 2);
        if (rc_r >= 2) { rc_buf = (uint32_t *)kmalloc((size_t)rc_r * rc_r * 16u); if (rc_buf && !fb_corners_save(x, y, b->w, b->h, rc_r, rc_buf)) { kfree(rc_buf); rc_buf = 0; } }
    }
    if (s && !anon && vis && b->kind != BK_TROW) {
        int skipbg = (b->node && (b->node == L->canvas_nodes[0]));
        int onscreen = !(y > P->cy1 || y + b->h < P->cy0 || x > P->cx1 || x + b->w < P->cx0);
        if (onscreen) {
            if (!skipbg) {
                for (int k = 3; k >= 0; k--) if ((s->xsh_c[k] >> 24) && !wl181_off()) {   /* SOMA_188: sombras adicionales (la primera queda encima) */
                    uint32_t sc = mul_alpha(s->xsh_c[k], a2);
                    if (sc >> 24) fb_draw_box_shadow(x, y, b->w, b->h, s->radius, s->xsh_x[k], s->xsh_y[k], s->xsh_s[k], s->xsh_b[k], sc);
                }
                if (s->shadow_color >> 24) {       /* SOMA_PARCHE_028_UI: box-shadow */
                    uint32_t sc = mul_alpha(s->shadow_color, a2);
                    if (sc >> 24) fb_draw_box_shadow(x, y, b->w, b->h, s->radius, s->shadow_x, s->shadow_y, s->shadow_spread, s->shadow_blur, sc);
                }
                if (s->bg_color) rrect(P, x, y, b->w, b->h, s->radius, mul_alpha(s->bg_color, a2));
                if (s->has_grad) gradient_r(P, x, y, b->w, b->h, s->grad_a, s->grad_b, s->grad_dir, a2, s->radius);
                if (s->bg_image) draw_bgimage(P, b, x, y, b->w, b->h, a2);
            }
            if (s->bg_color && (s->bg_color >> 24) == 255) P->bg = s->bg_color & 0xFFFFFF;
            if (!skipbg && (s->ish_color >> 24)) paint_inset_shadow(P, s, x + b->bl, y + b->bt, b->w - b->bl - b->br, b->h - b->bt - b->bb, a2);
            draw_border(P, x, y, b->w, b->h, b, a2);
            if (s->display == D_LIST_ITEM) paint_marker(P, b, x, y, a2);
        }
    } else if (b->kind == BK_TROW && s && !anon && vis) {
        const struct wstyle *gs = (b->node && b->node->parent && b->node->parent->st && b->node->parent->st->display == D_TABLE_GROUP) ? b->node->parent->st : 0;
        if (gs && gs->bg_color) fillr(P, x, y, b->w, b->h, mul_alpha(gs->bg_color, a2));
        if (s->bg_color) fillr(P, x, y, b->w, b->h, mul_alpha(s->bg_color, a2));
    }
    if (s && !anon && (s->overflow_x || s->overflow_y) && b != L->root) {
        clip_isect(P, x + b->bl, y + b->bt, b->w - b->bl - b->br, b->h - b->bt - b->bb);
        if (s->radius > 0 && 0) {}
    }
    int bsc = box_scroll(b); int y0 = y; y -= bsc;   /* SOMA_181: el contenido sube al desplazar */
    if (b->kind == BK_REPL) { if (vis) paint_ctl(P, b, x, y, a2); }
    else {
        for (struct wbox *c = b->first; c; c = c->next) if (!(c->flags & (BF_ABS | BF_FLOAT)) && !is_lifted(c)) paint_box(P, c, x, y, a2, depth + 1);
        for (struct wbox *c = b->first; c; c = c->next) if ((c->flags & BF_FLOAT) && !(c->flags & BF_ABS)) paint_box(P, c, x, y, a2, depth + 1);
        for (struct wbox *c = b->first; c; c = c->next) if (!(c->flags & (BF_ABS | BF_FLOAT)) && is_lifted(c)) paint_box(P, c, x, y, a2, depth + 1);
        if (b->items) {
            for (int i = 0; i < b->nitems; i++) if (b->items[i].type == II_FLOAT && b->items[i].box) paint_box(P, b->items[i].box, x, y, a2, depth + 1);
            if (vis || b->nfrags) paint_text_lines(P, b, x, y, a2);
            for (int i = 0; i < b->nitems; i++) if (b->items[i].type == II_ATOM && b->items[i].box) paint_box(P, b->items[i].box, x, y, a2, depth + 1);
        }
    }
    if (b->abs_chain) paint_abs_list(P, b, x, y, a2, depth, 0);
    if (b == L->root && b->abs_chain) paint_abs_list(P, b, P->vx, P->vy, a2, depth, 1);
    y = y0;
    if (bsc || (box_scroll_max(b) > 0 && s->overflow_y == 2)) {   /* barra de desplazamiento fina */
        int m = box_scroll_max(b), th = b->h - b->bt - b->bb, bh = wmax(16, (int)((int64_t)th * b->cont_h / wmax(1, b->sch))), by = y + b->bt + (m > 0 ? (int)((int64_t)(th - bh) * bsc / m) : 0);
        fillr(P, x + b->w - b->br - 7, by, 5, bh, 0x66808080u);
    }
    *P = save; P->bg = save_bg; clip_apply(P);
    if (rc_buf) { fb_corners_restore(x, y, b->w, b->h, rc_r, rc_buf); kfree(rc_buf); }
}

void wl_paint(struct wlay *L, int sx, int sy, int sw, int sh, int scroll_x, int scroll_y)
{
    if (!L || !L->root || sw <= 0 || sh <= 0) return;
    L->scroll_x = scroll_x; L->scroll_y = scroll_y;
    struct wpt P; memset(&P, 0, sizeof P); P.L = L; P.cx0 = sx; P.cy0 = sy; P.cx1 = sx + sw; P.cy1 = sy + sh; P.vx = sx; P.vy = sy; P.scx = scroll_x; P.scy = scroll_y; P.bg = 0xFFFFFF;
    int32_t ocx0, ocy0, ocx1, ocy1; fb_get_clip(&ocx0, &ocy0, &ocx1, &ocy1);
    int cx0 = wmax(P.cx0, ocx0), cy0 = wmax(P.cy0, ocy0), cx1 = wmin(P.cx1, ocx1), cy1 = wmin(P.cy1, ocy1);
    P.cx0 = cx0; P.cy0 = cy0; P.cx1 = cx1; P.cy1 = cy1; if (P.cx1 <= P.cx0 || P.cy1 <= P.cy0) return;
    clip_apply(&P);
    uint32_t cbg = L->canvas_bg ? L->canvas_bg : 0xFFFFFFFFu;
    fillr(&P, sx, sy, sw, sh, 0xFF000000u | cbg); P.bg = cbg & 0xFFFFFF;
    L->painting = 1; paint_box(&P, L->root, sx - scroll_x, sy - scroll_y, 255, 0); L->painting = 0;   /* SOMA_188 */
    fb_set_clip(ocx0, ocy0, ocx1 - ocx0, ocy1 - ocy0);
}

/* ============================== busqueda ============================== */
/* SOMA_188: un nodo con pointer-events:none (o texto dentro de uno) no es destino de clics */
static int ptr_ok(const struct wnode *n)
{
    const struct wnode *e = n && n->type == WN_TEXT ? n->parent : n;
    return !(e && e->type == WN_ELEM && e->st && e->st->no_ptr);
}
static struct wnode *hit_box(struct wlay *L, struct wbox *b, int px, int py, int ox, int oy, struct wbox **bo, int depth)
{
    if (depth > 90 || web_stack_left() < 4000) return 0;
    const struct wstyle *s = b->st; int anon = (b->flags & BF_ANON) != 0;
    if (s && !anon && s->display == D_NONE) return 0;
    int x = ox + b->x + b->rdx, y = oy + b->y + b->rdy;
    if (s && !anon && s->position == P_STICKY) y += sticky_dy(L, b, y, L->scroll_y);
    int clip = s && !anon && (s->overflow_x || s->overflow_y) && b != L->root;
    int inside = px >= x && px < x + b->w && py >= y && py < y + b->h;
    if (clip && !inside) return 0;
    y -= box_scroll(b);   /* SOMA_181: los hijos de una caja desplazada */
    if (s && !anon && s->visibility && !b->first && !b->items) return 0;
    struct wnode *r; struct wbox *neg[64]; int negn = 0;
    { struct wbox *arr[64]; int n = 0; for (struct wbox *a = b->abs_chain; a; a = a->abs_next) if (!(a->flags & BF_FIXED) && n < 64) arr[n++] = a;
      for (int i = 1; i < n; i++) { struct wbox *t = arr[i]; int j = i - 1; while (j >= 0 && arr[j]->zi < t->zi) { arr[j + 1] = arr[j]; j--; } arr[j + 1] = t; }
      for (int i = 0; i < n; i++) if (arr[i]->zi >= 0 && (r = hit_box(L, arr[i], px, py, x, y, bo, depth + 1))) return r;
      negn = n; for (int i = 0; i < n; i++) neg[i] = arr[i]; }   /* SOMA_188: los de z-index negativo van detras del contenido */
    if (b == L->root) { for (struct wbox *a = b->abs_chain; a; a = a->abs_next) if ((a->flags & BF_FIXED) && (r = hit_box(L, a, px - L->scroll_x, py - L->scroll_y, 0, 0, bo, depth + 1))) return r; }
    if (b->kind == BK_REPL) { if (inside && ptr_ok(b->node) && !(s && s->visibility)) { if (bo) *bo = b; return b->node; } return 0; }
    if (b->items) {
        for (int i = b->nitems - 1; i >= 0; i--) { struct wii *it = &b->items[i]; if ((it->type == II_ATOM || it->type == II_FLOAT) && it->box && (r = hit_box(L, it->box, px, py, x, y, bo, depth + 1))) return r; }
        for (int i = b->nfrags - 1; i >= 0; i--) {
            const struct wfrag *f = &b->frags[i];
            if (px >= x + f->x && px < x + f->x + f->w && py >= y + f->y - f->asc && py < y + f->y + f->dsc && ptr_ok(f->node)) { if (bo) *bo = b; return f->node && f->node->type == WN_TEXT ? f->node->parent : f->node; }
        }
    }
    struct wbox *kids[2048]; int nk = 0;
    for (struct wbox *c = b->first; c && nk < 2048; c = c->next) if (!(c->flags & BF_ABS)) kids[nk++] = c;
    for (int i = nk - 1; i >= 0; i--) if (!(kids[i]->flags & BF_FLOAT) && is_lifted(kids[i])) if ((r = hit_box(L, kids[i], px, py, x, y, bo, depth + 1))) return r;
    for (int i = nk - 1; i >= 0; i--) if (kids[i]->flags & BF_FLOAT) if ((r = hit_box(L, kids[i], px, py, x, y, bo, depth + 1))) return r;
    for (int i = nk - 1; i >= 0; i--) if (!(kids[i]->flags & BF_FLOAT) && !is_lifted(kids[i])) if ((r = hit_box(L, kids[i], px, py, x, y, bo, depth + 1))) return r;
    for (int i = 0; i < negn; i++) if (neg[i]->zi < 0 && (r = hit_box(L, neg[i], px, py, x, y, bo, depth + 1))) return r;
    if (inside && s && !anon && b->node && (b != L->root) && !s->no_ptr && !s->visibility) { if (s->bg_color || s->bg_image || s->has_grad || b->kind == BK_BLOCK) { if (bo) *bo = b; return b->node; } }
    if (inside && b == L->root) { if (bo) *bo = b; return b->node; }
    return 0;
}
int wl_inner_scroll(struct wlay *L, int px, int py, int dy)
{
    if (!L || !L->root || wl181_off() || !dy) return 0;
    struct wbox *bx = 0; struct wnode *n = wl_hit(L, px, py, 0, &bx); (void)n;
    struct wbox *b = bx; if (!b && n && (n->flags & WNF_INDOC)) { struct wnode *e = n; while (e && !e->box) e = e->parent; b = e ? e->box : 0; }   /* SOMA_188: solo nodos del documento */
    for (int guard = 0; b && guard < 200; guard++) {
        int m = box_scroll_max(b);
        if (m > 0 && b->st->overflow_y == 2 && b->node) {
            int cur = box_scroll(b), nv = cur + dy; if (nv < 0) nv = 0; if (nv > m) nv = m;
            if (nv != cur) { b->node->scy = nv; L->inner_gen++; return 1; }
        }
        b = (b->flags & BF_ABS) && b->abs_cb ? b->abs_cb : b->parent;
    }
    return 0;
}
uint32_t wl_inner_gen(const struct wlay *L) { return L ? L->inner_gen : 0; }
/* SOMA_203: lo que el navegador aislado necesita saber de la pagina para desplazarla sin repintar: bit 0 = hay elementos fijos o
 * pegajosos (un fotograma corrido los moveria de sitio); bit 1 = hay cajas con desplazamiento propio (la rueda puede ser suya). */
int wl_scan_flags(struct wlay *L)
{
    int f = 0;
    if (!L || !L->d || !L->d->root) return 0;
    for (struct wnode *n = L->d->root; n && f != 3;) {
        if (n->st && (n->st->position == P_FIXED || n->st->position == P_STICKY)) f |= 1;
        if ((n->flags & WNF_INDOC) && n->box && n->st && n->st->overflow_y == 2 && box_scroll_max(n->box) > 0) f |= 2;
        if (n->first) { n = n->first; continue; }
        while (n && !n->next) n = n->parent;
        if (n) n = n->next;
    }
    return f;
}
int wl_node_scroll_max(struct wlay *L, struct wnode *n) { (void)L; return n && (n->flags & WNF_INDOC) && n->box ? box_scroll_max(n->box) : 0; }

struct wnode *wl_hit(struct wlay *L, int px, int py, struct wnode **link, struct wbox **boxout)
{
    if (link) *link = 0;
    if (boxout) *boxout = 0;
    if (!L || !L->root) return 0;
    struct wnode *n = hit_box(L, L->root, px, py, 0, 0, boxout, 0);
    while (n && (n->flags & WNF_PSEUDO)) n = n->parent;   /* SOMA_188: clic en ::before/::after = clic en el elemento */
    if (n && link) { for (struct wnode *p = n; p; p = p->parent) if (p->type == WN_ELEM && p->tag == WT_A && wd_attr(p, "href")) { *link = p; break; } }
    return n;
}
static int node_within(const struct wnode *a, const struct wnode *n, const struct wnode *stop)
{
    for (; a && a != stop; a = a->parent) if (a == n) return 1;
    return a == n;
}
int wl_node_rect(struct wlay *L, struct wnode *n, int *x, int *y, int *w, int *h)
{
    if (!L || !L->root || !n) return 0;
    if (!(n->flags & WNF_INDOC) && !(n->flags & WNF_PSEUDO)) return 0;   /* SOMA_188: nodo fuera del documento: su caja puede ser de una maquetacion liberada */
    struct wnode *e = n; while (e && !e->box) e = e->parent;
    if (!e) return 0;
    struct wbox *b = e->box; int ax = 0, ay = 0;
    for (struct wbox *p = b; p; ) {
        ax += p->x + p->rdx; ay += p->y + p->rdy;
        if (p->flags & BF_FIXED) break;
        if (p->flags & BF_ABS) p = p->abs_cb ? p->abs_cb : L->root; else p = p->parent;
        if (p == L->root) { ax += p->x; ay += p->y; break; }
    }
    int rx = ax, ry = ay, rw = b->w, rh = b->h;
    if (e != n && !wl181_off()) {
        /* SOMA_188: elemento en linea (o texto) sin caja propia: union de sus fragmentos, adornos y cajas atomicas dentro
         * del bloque que lo contiene (antes se devolvia el bloque entero: getBoundingClientRect de un <span> = ancho de linea) */
        struct { struct wbox *b; int ox, oy; } st[256]; int sp = 0; int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
        st[sp].b = b; st[sp].ox = ax - b->x - b->rdx; st[sp].oy = ay - b->y - b->rdy; sp++;
        while (sp > 0) {
            sp--; struct wbox *q = st[sp].b; int qx = st[sp].ox + q->x + q->rdx, qy = st[sp].oy + q->y + q->rdy;
            if (q != b && q->node && !(q->flags & BF_ANON) && q->node != e && node_within(q->node, n, e)) {
                x0 = wmin(x0, qx); y0 = wmin(y0, qy); x1 = wmax(x1, qx + q->w); y1 = wmax(y1, qy + q->h); continue;
            }
            for (int i = 0; i < q->ndecos; i++) { const struct wdeco *d = &q->decos[i]; if (d->node != n) continue;
                x0 = wmin(x0, qx + d->x0); y0 = wmin(y0, qy + d->y0); x1 = wmax(x1, qx + d->x1); y1 = wmax(y1, qy + d->y1); }
            for (int i = 0; i < q->nfrags; i++) { const struct wfrag *f = &q->frags[i]; if (!f->node || !node_within(f->node, n, e)) continue;
                int fx = qx + f->x, fy = qy + f->y - f->asc; x0 = wmin(x0, fx); y0 = wmin(y0, fy); x1 = wmax(x1, fx + f->w); y1 = wmax(y1, qy + f->y + f->dsc); }
            for (int i = 0; i < q->nitems && sp < 254; i++) if (q->items[i].type == II_ATOM && q->items[i].box) { st[sp].b = q->items[i].box; st[sp].ox = qx; st[sp].oy = qy; sp++; }
            for (struct wbox *c = q->first; c && sp < 254; c = c->next) if (!(c->flags & BF_ABS)) { st[sp].b = c; st[sp].ox = qx; st[sp].oy = qy; sp++; }
        }
        if (x1 > x0) { rx = x0; ry = y0; rw = x1 - x0; rh = y1 - y0; }
        else { rw = 0; rh = 0; }
    }
    if (x) *x = rx;
    if (y) *y = ry;
    if (w) *w = rw;
    if (h) *h = rh;
    return 1;
}
