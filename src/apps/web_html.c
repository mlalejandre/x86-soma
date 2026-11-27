/* SOMA_PARCHE_NAVEGADOR: tokenizador HTML5 simplificado, constructor de arbol y API DOM. */
#include "web_dom.h"

static const char *const tag_names[WT_COUNT] = {
    "", "html", "head", "body", "title", "meta", "link", "style", "script", "noscript", "base",
    "div", "span", "p", "a", "img", "br", "hr", "h1", "h2", "h3", "h4", "h5", "h6",
    "ul", "ol", "li", "dl", "dt", "dd", "table", "thead", "tbody", "tfoot", "tr", "td", "th",
    "caption", "colgroup", "col", "form", "input", "button", "select", "option", "optgroup",
    "textarea", "label", "fieldset", "legend", "pre", "code", "blockquote", "b", "strong", "i",
    "em", "u", "s", "small", "sub", "sup", "iframe", "svg", "canvas", "video", "audio",
    "nav", "header", "footer", "main", "section", "article", "aside", "figure", "figcaption",
    "details", "summary", "center", "font", "wbr", "source", "embed", "param", "track",
    "address", "menu", "dialog", "template", "object", "picture", "area", "map", "kbd", "samp",
    "mark", "q", "cite", "abbr", "del", "ins", "big", "strike", "tt", "dfn", "var", "time",
    "output", "progress", "meter", "datalist", "frame", "frameset", "ruby", "rt", "rp", "bdi", "bdo",
};

int wd_tag_of(const char *name, uint32_t n)
{
    for (int i = 1; i < WT_COUNT; i++) {
        const char *t = tag_names[i]; uint32_t k = 0;
        while (k < n && t[k] && t[k] == name[k]) k++;
        if (k == n && !t[k]) return i;
    }
    return WT_NONE;
}

static int is_void_tag(int t)
{
    switch (t) {
    case WT_META: case WT_LINK: case WT_BASE: case WT_IMG: case WT_BR: case WT_HR: case WT_INPUT: case WT_COL:
    case WT_WBR: case WT_SOURCE: case WT_EMBED: case WT_PARAM: case WT_TRACK: case WT_AREA: case WT_FRAME: return 1;
    }
    return 0;
}

/* ---------------- entidades ---------------- */
static const char latin1_names[] =
    "nbsp iexcl cent pound curren yen brvbar sect uml copy ordf laquo not shy reg macr deg plusmn sup2 sup3 acute micro para middot cedil sup1 ordm raquo frac14 frac12 frac34 iquest "
    "Agrave Aacute Acirc Atilde Auml Aring AElig Ccedil Egrave Eacute Ecirc Euml Igrave Iacute Icirc Iuml ETH Ntilde Ograve Oacute Ocirc Otilde Ouml times Oslash Ugrave Uacute Ucirc Uuml Yacute THORN szlig "
    "agrave aacute acirc atilde auml aring aelig ccedil egrave eacute ecirc euml igrave iacute icirc iuml eth ntilde ograve oacute ocirc otilde ouml divide oslash ugrave uacute ucirc uuml yacute thorn yuml";
static const char greek_names[] = "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigmaf sigma tau upsilon phi chi psi omega";
static const char greekc_names[] = "Alpha Beta Gamma Delta Epsilon Zeta Eta Theta Iota Kappa Lambda Mu Nu Xi Omicron Pi Rho - Sigma Tau Upsilon Phi Chi Psi Omega";
struct ent { const char *n; uint16_t cp; };
static const struct ent ents[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009},
    {"zwnj", 0x200C}, {"zwj", 0x200D}, {"lrm", 0x200E}, {"rlm", 0x200F}, {"ndash", 0x2013}, {"mdash", 0x2014}, {"lsquo", 0x2018},
    {"rsquo", 0x2019}, {"sbquo", 0x201A}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bdquo", 0x201E}, {"dagger", 0x2020},
    {"Dagger", 0x2021}, {"bull", 0x2022}, {"hellip", 0x2026}, {"permil", 0x2030}, {"prime", 0x2032}, {"Prime", 0x2033},
    {"lsaquo", 0x2039}, {"rsaquo", 0x203A}, {"euro", 0x20AC}, {"trade", 0x2122}, {"larr", 0x2190}, {"uarr", 0x2191},
    {"rarr", 0x2192}, {"darr", 0x2193}, {"harr", 0x2194}, {"lArr", 0x21D0}, {"rArr", 0x21D2}, {"hArr", 0x21D4},
    {"forall", 0x2200}, {"part", 0x2202}, {"exist", 0x2203}, {"empty", 0x2205}, {"nabla", 0x2207}, {"isin", 0x2208},
    {"sum", 0x2211}, {"minus", 0x2212}, {"radic", 0x221A}, {"infin", 0x221E}, {"and", 0x2227}, {"or", 0x2228},
    {"cap", 0x2229}, {"cup", 0x222A}, {"int", 0x222B}, {"asymp", 0x2248}, {"ne", 0x2260}, {"equiv", 0x2261},
    {"le", 0x2264}, {"ge", 0x2265}, {"loz", 0x25CA}, {"spades", 0x2660}, {"clubs", 0x2663}, {"hearts", 0x2665},
    {"diams", 0x2666}, {"OElig", 0x152}, {"oelig", 0x153}, {"Scaron", 0x160}, {"scaron", 0x161}, {"Yuml", 0x178},
    {"fnof", 0x192}, {"circ", 0x2C6}, {"tilde", 0x2DC}, {"check", 0x2713}, {"star", 0x2606}, {"starf", 0x2605},
};

static int ent_lookup(const char *s, uint32_t n, uint32_t *cp)
{
    if (n == 0 || n > 10) return 0;
    for (uint32_t i = 0; i < sizeof(ents) / sizeof(ents[0]); i++)
        if (wu_strneq(ents[i].n, s, n) && ents[i].n[n] == 0) { *cp = ents[i].cp; return 1; }
    const char *tabs[3] = { latin1_names, greek_names, greekc_names };
    uint32_t bases[3] = { 160, 0x3B1, 0x391 };
    for (int t = 0; t < 3; t++) {
        const char *p = tabs[t]; uint32_t idx = 0;
        while (*p) {
            const char *q = p; while (*q && *q != ' ') q++;
            uint32_t len = (uint32_t)(q - p);
            if (len == n && wu_strneq(p, s, n)) { *cp = bases[t] + idx; return 1; }
            idx++; p = *q ? q + 1 : q;
        }
    }
    return 0;
}

void wd_decode_entities(const char *s, uint32_t n, struct wb *out)
{
    uint32_t i = 0;
    while (i < n) {
        if (s[i] != '&') { uint32_t j = i; while (j < n && s[j] != '&') j++; wb_addn(out, s + i, j - i); i = j; continue; }
        uint32_t j = i + 1, cp = 0; int ok = 0;
        if (j < n && s[j] == '#') {
            j++; uint32_t v = 0; int any = 0;
            if (j < n && (s[j] == 'x' || s[j] == 'X')) { j++; while (j < n && wu_ishex(s[j]) && v < 0x110000) { v = v * 16 + (uint32_t)wu_hexval(s[j]); j++; any = 1; } }
            else { while (j < n && wu_isdigit(s[j]) && v < 0x110000) { v = v * 10 + (uint32_t)(s[j] - '0'); j++; any = 1; } }
            if (any) {
                if (j < n && s[j] == ';') j++;
                if (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v < 0xE000)) v = 0xFFFD;
                cp = v; ok = 1;
            }
        } else {
            uint32_t k = j; while (k < n && k - j < 10 && wu_isalnum(s[k])) k++;
            if (k > j && ent_lookup(s + j, k - j, &cp)) { j = k; if (j < n && s[j] == ';') j++; ok = 1; }
        }
        if (ok) { char u[4]; int l = wu_utf8_put(cp, u); wb_addn(out, u, (uint32_t)l); i = j; }
        else { wb_addc(out, '&'); i++; }
    }
}

uint32_t wd_latin1_to_utf8(const uint8_t *in, uint32_t n, char *out, uint32_t max)
{
    static const uint16_t w1252[32] = {
        0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0x8D, 0x17D, 0x8F,
        0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178 };
    uint32_t o = 0;
    for (uint32_t i = 0; i < n && o + 4 < max; i++) {
        uint32_t c = in[i];
        if (c >= 0x80 && c < 0xA0) c = w1252[c - 0x80];
        o += (uint32_t)wu_utf8_put(c, out + o);
    }
    out[o] = 0; return o;
}

/* ---------------- DOM basico ---------------- */
struct wdoc *wd_new(void)
{
    struct wdoc *d = (struct wdoc *)kmalloc(sizeof(struct wdoc));
    if (!d) return 0;
    memset(d, 0, sizeof(*d));
    d->ar.limit = 20u * 1024u * 1024u;
    struct wnode *r = (struct wnode *)wa_zalloc(&d->ar, sizeof(struct wnode));
    if (!r) { kfree(d); return 0; }
    r->type = WN_DOC; r->flags = WNF_INDOC; r->name = "#document"; r->uid = ++d->next_uid;
    d->root = r; d->nodes = 1;
    return d;
}

void wd_free(struct wdoc *d) { if (!d) return; wa_free_all(&d->ar); kfree(d); }

static struct wnode *new_node(struct wdoc *d, int type)
{
    if (d->nodes > 60000) return 0;
    struct wnode *n = (struct wnode *)wa_zalloc(&d->ar, sizeof(struct wnode));
    if (!n) return 0;
    n->type = (uint8_t)type; n->uid = ++d->next_uid; d->nodes++;
    return n;
}

struct wnode *wd_new_elem(struct wdoc *d, const char *name, uint32_t nlen)
{
    struct wnode *n = new_node(d, WN_ELEM); if (!n) return 0;
    char *nm = (char *)wa_alloc(&d->ar, nlen + 1); if (!nm) return 0;
    for (uint32_t i = 0; i < nlen; i++) nm[i] = (char)wu_lower(name[i]);
    nm[nlen] = 0; n->name = nm; n->tlen = nlen;
    n->tag = (uint8_t)wd_tag_of(nm, nlen);
    if (is_void_tag(n->tag)) n->flags |= WNF_VOID;
    n->flags |= WNF_DIRTY;
    return n;
}

struct wnode *wd_new_text(struct wdoc *d, const char *s, uint32_t n)
{
    struct wnode *t = new_node(d, WN_TEXT); if (!t) return 0;
    t->name = wa_strndup(&d->ar, s, n); t->tlen = n; if (!t->name) return 0;
    return t;
}

static void mark_indoc(struct wnode *n, int on)
{
    struct wnode *c = n;
    while (c) {
        if (on) c->flags |= WNF_INDOC; else c->flags &= (uint16_t)~WNF_INDOC;
        if (c->type == WN_ELEM && c->shadow) mark_indoc(c->shadow, on);   /* SOMA_188 */
        if (c->first) { c = c->first; continue; }
        while (c && c != n && !c->next) c = c->parent;
        if (!c || c == n) break;
        c = c->next;
    }
}

void wd_remove(struct wnode *c)
{
    if (!c || !c->parent) return;
    struct wnode *p = c->parent;
    if (c->prev) c->prev->next = c->next; else p->first = c->next;
    if (c->next) c->next->prev = c->prev; else p->last = c->prev;
    c->parent = c->prev = c->next = 0;
    mark_indoc(c, 0);
    p->flags |= WNF_DIRTY; /* SOMA_146: invalidar layout al eliminar un hijo */
}

void wd_insert_before(struct wnode *p, struct wnode *c, struct wnode *ref)
{
    if (!p || !c || c == ref) return;
    if (c->parent) wd_remove(c);
    c->parent = p;
    if (!ref) { c->prev = p->last; c->next = 0; if (p->last) p->last->next = c; else p->first = c; p->last = c; }
    else { c->next = ref; c->prev = ref->prev; if (ref->prev) ref->prev->next = c; else p->first = c; ref->prev = c; }
    if (p->flags & WNF_INDOC) mark_indoc(c, 1);
    p->flags |= WNF_DIRTY;
}

void wd_append(struct wnode *p, struct wnode *c) { wd_insert_before(p, c, 0); }

const char *wd_attr(const struct wnode *n, const char *name)
{
    if (!n) return 0;
    for (const struct wattr *a = n->attrs; a; a = a->next) if (wu_streq(a->name, name)) return a->val;
    return 0;
}

void wd_set_attr(struct wdoc *d, struct wnode *n, const char *name, const char *val)
{
    if (!n || !name) return;
    const char *v = wa_strdup(&d->ar, val ? val : "");
    if (n->tag == WT_IMG && (wu_streq(name, "src") || wu_streq(name, "srcset"))) n->flags &= (uint16_t)~WNF_IMGEV;   /* SOMA_346: otro load/error */
    for (struct wattr *a = n->attrs; a; a = a->next) if (wu_streq(a->name, name)) { a->val = v; n->flags |= WNF_DIRTY; return; }
    struct wattr *a = (struct wattr *)wa_alloc(&d->ar, sizeof(*a)); if (!a) return;
    a->name = wa_strdup(&d->ar, name); a->val = v; a->next = 0;
    if (!n->attrs) n->attrs = a; else { struct wattr *t = n->attrs; while (t->next) t = t->next; t->next = a; }
    n->flags |= WNF_DIRTY;
}

void wd_del_attr(struct wnode *n, const char *name)
{
    struct wattr **pp = &n->attrs;
    while (*pp) { if (wu_streq((*pp)->name, name)) { *pp = (*pp)->next; n->flags |= WNF_DIRTY; return; } pp = &(*pp)->next; }
}

/* recorrido en preorden sin recursion */
static struct wnode *tree_next(struct wnode *n, struct wnode *root)
{
    if (n->first) return n->first;
    while (n && n != root) { if (n->next) return n->next; n = n->parent; }
    return 0;
}

struct wnode *wd_by_id(struct wnode *root, const char *id)
{
    if (!root || !id || !*id) return 0;
    for (struct wnode *n = root; n; n = tree_next(n, root)) {
        if (n->type != WN_ELEM) continue;
        const char *v = wd_attr(n, "id");
        if (v && wu_streq(v, id)) return n;
    }
    return 0;
}

int wd_has_class(const struct wnode *n, const char *cls)
{
    const char *c = wd_attr(n, "class"); if (!c) return 0;
    uint32_t cl = wu_strlen(cls);
    while (*c) {
        while (wu_isspace(*c)) c++;
        const char *s = c; while (*c && !wu_isspace(*c)) c++;
        if ((uint32_t)(c - s) == cl && wu_strneq(s, cls, cl)) return 1;
    }
    return 0;
}

int wd_is_ancestor(const struct wnode *a, const struct wnode *n)
{
    for (const struct wnode *p = n; p; p = p->parent) if (p == a) return 1;
    return 0;
}

void wd_text_content(const struct wnode *n, struct wb *out)
{
    if (!n) return;
    if (n->type == WN_TEXT || n->type == WN_COMMENT) { wb_addn(out, n->name, n->tlen); return; }
    struct wnode *root = (struct wnode *)n;
    for (struct wnode *c = tree_next(root, root); c; c = tree_next(c, root))
        if (c->type == WN_TEXT) wb_addn(out, c->name, c->tlen);
}

void wd_set_text(struct wdoc *d, struct wnode *n, const char *s)
{
    if (!n) return;
    if (n->type == WN_TEXT || n->type == WN_COMMENT) { n->name = wa_strdup(&d->ar, s); n->tlen = wu_strlen(s); n->flags |= WNF_DIRTY; if (n->parent) n->parent->flags |= WNF_DIRTY; return; }
    while (n->first) wd_remove(n->first);
    if (s && *s) { struct wnode *t = wd_new_text(d, s, wu_strlen(s)); if (t) wd_append(n, t); }
}

static void esc_text(struct wb *o, const char *s, uint32_t n, int attr)
{
    for (uint32_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '&') wb_add(o, "&amp;");
        else if (c == '<' && !attr) wb_add(o, "&lt;");
        else if (c == '>' && !attr) wb_add(o, "&gt;");
        else if (c == '"' && attr) wb_add(o, "&quot;");
        else wb_addc(o, c);
    }
}

void wd_outer_html(const struct wnode *n, struct wb *o, int inner_only)
{
    if (!n) return;
    struct wnode *root = (struct wnode *)n;
    struct wnode *c = inner_only ? root->first : root;
    while (c) {
        int raw = c->parent && (c->parent->tag == WT_SCRIPT || c->parent->tag == WT_STYLE);
        if (c->type == WN_TEXT) { if (raw) wb_addn(o, c->name, c->tlen); else esc_text(o, c->name, c->tlen, 0); }
        else if (c->type == WN_COMMENT) { wb_add(o, "<!--"); wb_addn(o, c->name, c->tlen); wb_add(o, "-->"); }
        else if (c->type == WN_ELEM) {
            wb_addc(o, '<'); wb_add(o, c->name);
            for (struct wattr *a = c->attrs; a; a = a->next) { wb_addc(o, ' '); wb_add(o, a->name); wb_add(o, "=\""); esc_text(o, a->val, wu_strlen(a->val), 1); wb_addc(o, '"'); }
            wb_addc(o, '>');
        }
        /* siguiente en preorden emitiendo cierres */
        if (c->type == WN_ELEM && c->first) { c = c->first; continue; }
        for (;;) {
            if (c->type == WN_ELEM && !(c->flags & WNF_VOID)) { wb_add(o, "</"); wb_add(o, c->name); wb_addc(o, '>'); }
            if (c == root) { c = 0; break; }
            if (inner_only && c->parent == root && !c->next) { c = 0; break; }
            if (c->next) { c = c->next; break; }
            c = c->parent;
            if (!c) break;
            if (c == root) { if (!inner_only && c->type == WN_ELEM && !(c->flags & WNF_VOID)) { wb_add(o, "</"); wb_add(o, c->name); wb_addc(o, '>'); } c = 0; break; }
        }
    }
}

struct wnode *wd_clone(struct wdoc *d, const struct wnode *n, int deep)
{
    struct wnode *c = 0;
    if (n->type == WN_TEXT || n->type == WN_COMMENT) { c = wd_new_text(d, n->name, n->tlen); if (c) c->type = n->type; return c; }
    c = wd_new_elem(d, n->name, n->tlen); if (!c) return 0;
    for (const struct wattr *a = n->attrs; a; a = a->next) wd_set_attr(d, c, a->name, a->val);
    c->value = n->value; c->flags |= (n->flags & WNF_CHECKED);
    if (deep) for (const struct wnode *k = n->first; k; k = k->next) { struct wnode *kc = wd_clone(d, k, 1); if (kc) wd_append(c, kc); }
    return c;
}

/* ---------------- constructor de arbol ---------------- */
#define MAX_DEPTH 180
struct ps {
    struct wdoc *d;
    struct wnode *stk[MAX_DEPTH + 4];
    int sp;
    int body_started, frag, skip_nl;
    int keep_comments;   /* SOMA_188 */
    struct wb txt;
};

static struct wnode *cur(struct ps *s) { return s->stk[s->sp - 1]; }

static void push(struct ps *s, struct wnode *n)
{
    wd_append(cur(s), n);
    if (!(n->flags & WNF_VOID) && s->sp < MAX_DEPTH) s->stk[s->sp++] = n;
}

static int is_ws_only(const char *p, uint32_t n) { for (uint32_t i = 0; i < n; i++) if (!wu_isspace(p[i])) return 0; return 1; }

static void ensure_html(struct ps *s)
{
    if (s->frag || s->d->html) return;
    struct wnode *h = wd_new_elem(s->d, "html", 4); if (!h) return;
    s->d->html = h; push(s, h);
}
static void ensure_head(struct ps *s)
{
    if (s->frag || s->d->head) return;
    ensure_html(s);
    struct wnode *h = wd_new_elem(s->d, "head", 4); if (!h) return;
    s->d->head = h;
    while (s->sp > 2) s->sp--;
    push(s, h);
}
static void ensure_body(struct ps *s)
{
    if (s->frag || s->body_started) return;
    ensure_html(s);
    if (!s->d->head && !web_feat_off("htmlparse")) ensure_head(s);   /* SOMA_188: <head> implicito (vacio) antes del cuerpo, como el algoritmo HTML */
    s->body_started = 1;
    while (s->sp > 2) s->sp--;
    if (s->sp == 3) s->sp--;
    struct wnode *b = wd_new_elem(s->d, "body", 4); if (!b) return;
    s->d->body = b; push(s, b);
}

/* SOMA_188: interruptores de arranque soma.<nombre>=off de las funciones nuevas del navegador */
#if defined(WEB_HOST_TEST) || defined(SOMA_HOST_TEST)
int (*g_web_feat_hook)(const char *name);
int web_feat_off(const char *n) { return g_web_feat_hook ? g_web_feat_hook(n) : 0; }
#elif defined(__x86_64__)
extern int bootinfo_disabled(const char *name);
int web_feat_off(const char *n) { return bootinfo_disabled(n); }
#else
int web_feat_off(const char *n) { (void)n; return 0; }
#endif
static void flush_text(struct ps *s)
{
    if (!s->txt.len) return;
    struct wnode *c = cur(s);
    if (!s->frag && !s->body_started && (c->type == WN_DOC || c->tag == WT_HTML || c->tag == WT_HEAD)) {
        if (is_ws_only(s->txt.p, s->txt.len)) { wb_clear(&s->txt); return; }
        ensure_body(s);
        c = cur(s);
    }
    if (c->last && c->last->type == WN_TEXT) {
        /* fusionar con el texto previo */
        struct wb m = {0, 0, 0, 0};
        wb_addn(&m, c->last->name, c->last->tlen); wb_addn(&m, s->txt.p, s->txt.len);
        if (!m.err) c->last->name = wa_strndup(&s->d->ar, m.p, m.len), c->last->tlen = m.len;
        wb_free(&m);
    } else {
        struct wnode *t = wd_new_text(s->d, s->txt.p, s->txt.len);
        if (t) wd_append(c, t);
    }
    wb_clear(&s->txt);
}

static int in_stack(struct ps *s, int tag, const int *stops, int nstops)
{
    for (int i = s->sp - 1; i > 0; i--) {
        int t = s->stk[i]->tag;
        if (t == tag) return i;
        for (int k = 0; k < nstops; k++) if (stops[k] == t) return -1;
    }
    return -1;
}
static const int scope_stops[] = { WT_HTML, WT_TABLE, WT_TD, WT_TH, WT_CAPTION, WT_BUTTON, WT_OBJECT, WT_TEMPLATE };
static const int list_stops[] = { WT_HTML, WT_UL, WT_OL, WT_TABLE, WT_DL, WT_TD, WT_TH };
static const int cell_stops[] = { WT_HTML, WT_TABLE, WT_TEMPLATE };

static void pop_to(struct ps *s, int idx) { if (idx > 0) s->sp = idx; }

static int closes_p(int t)
{
    switch (t) {
    case WT_DIV: case WT_P: case WT_H1: case WT_H2: case WT_H3: case WT_H4: case WT_H5: case WT_H6: case WT_UL: case WT_OL:
    case WT_LI: case WT_DL: case WT_DT: case WT_DD: case WT_TABLE: case WT_FORM: case WT_FIELDSET: case WT_PRE: case WT_BLOCKQUOTE:
    case WT_HR: case WT_NAV: case WT_HEADER: case WT_FOOTER: case WT_MAIN: case WT_SECTION: case WT_ARTICLE: case WT_ASIDE:
    case WT_FIGURE: case WT_FIGCAPTION: case WT_DETAILS: case WT_SUMMARY: case WT_CENTER: case WT_ADDRESS: case WT_MENU: case WT_DIALOG:
        return 1;
    }
    return 0;
}

static void implied_end_for_start(struct ps *s, int t)
{
    int i;
    if (closes_p(t)) { i = in_stack(s, WT_P, scope_stops, 8); if (i > 0) pop_to(s, i); }
    switch (t) {
    case WT_LI: i = in_stack(s, WT_LI, list_stops, 7); if (i > 0) pop_to(s, i); break;
    case WT_DT: case WT_DD:
        i = in_stack(s, WT_DD, list_stops, 7); if (i > 0) pop_to(s, i);
        i = in_stack(s, WT_DT, list_stops, 7); if (i > 0) pop_to(s, i); break;
    case WT_OPTION: if (cur(s)->tag == WT_OPTION) s->sp--; break;
    case WT_OPTGROUP: if (cur(s)->tag == WT_OPTION) s->sp--; if (cur(s)->tag == WT_OPTGROUP) s->sp--; break;
    case WT_H1: case WT_H2: case WT_H3: case WT_H4: case WT_H5: case WT_H6:
        if (cur(s)->tag >= WT_H1 && cur(s)->tag <= WT_H6) s->sp--;
        break;
    case WT_TR:
        i = in_stack(s, WT_TD, cell_stops, 3); if (i < 0) i = in_stack(s, WT_TH, cell_stops, 3);
        if (i > 0) pop_to(s, i);
        i = in_stack(s, WT_TR, cell_stops, 3); if (i > 0) pop_to(s, i); break;
    case WT_TD: case WT_TH:
        i = in_stack(s, WT_TD, cell_stops, 3); if (i < 0) i = in_stack(s, WT_TH, cell_stops, 3);
        if (i > 0) pop_to(s, i);
        break;
    case WT_THEAD: case WT_TBODY: case WT_TFOOT:
        i = in_stack(s, WT_TD, cell_stops, 3); if (i < 0) i = in_stack(s, WT_TH, cell_stops, 3); if (i > 0) pop_to(s, i);
        i = in_stack(s, WT_TR, cell_stops, 3); if (i > 0) pop_to(s, i);
        i = in_stack(s, WT_TBODY, cell_stops, 3); if (i < 0) i = in_stack(s, WT_THEAD, cell_stops, 3); if (i < 0) i = in_stack(s, WT_TFOOT, cell_stops, 3);
        if (i > 0) pop_to(s, i);
        break;
    case WT_A: i = in_stack(s, WT_A, scope_stops, 8); if (i > 0) pop_to(s, i); break;
    case WT_BUTTON: i = in_stack(s, WT_BUTTON, scope_stops, 7); if (i > 0) pop_to(s, i); break;
    }
}

static int is_head_tag(int t)
{
    return t == WT_TITLE || t == WT_META || t == WT_LINK || t == WT_STYLE || t == WT_SCRIPT || t == WT_BASE || t == WT_TEMPLATE;
}

/* texto sin procesar hasta </nombre */
static const char *find_close(const char *p, const char *end, const char *name, uint32_t nl)
{
    while (p < end) {
        if (p[0] == '<' && p[1] == '/' && (uint32_t)(end - p) > nl + 2 && wu_strnieq(p + 2, name, nl)) {
            char c = p[2 + nl];
            if (wu_isspace(c) || c == '>' || c == '/') return p;
        }
        p++;
    }
    return end;
}

static void add_attr_raw(struct ps *s, struct wnode *n, const char *nm, uint32_t nl, const char *v, uint32_t vl, int has_v)
{
    char name[48]; uint32_t k = 0;
    for (; k < nl && k < 47; k++) name[k] = (char)wu_lower(nm[k]);
    name[k] = 0;
    if (wd_attr(n, name)) return;
    struct wb dec = {0, 0, 0, 0};
    if (has_v) wd_decode_entities(v, vl, &dec);
    wd_set_attr(s->d, n, name, dec.p ? dec.p : "");
    wb_free(&dec);
}

static void parse_core(struct ps *s, const char *html, uint32_t len)
{
    const char *p = html, *end = html + len;
    while (p < end) {
        wu_tick();
        if (s->d->ar.fail || s->d->nodes > 60000) break;
        if (*p != '<') {
            const char *q = p; while (q < end && *q != '<') q++;
            if (s->skip_nl && *p == '\n') { p++; }
            s->skip_nl = 0;
            wd_decode_entities(p, (uint32_t)(q - p), &s->txt);
            p = q; continue;
        }
        /* comentarios / doctype / basura */
        if (end - p >= 4 && p[1] == '!' && p[2] == '-' && p[3] == '-') {
            flush_text(s);
            const char *cs = p + 4, *q = cs, *cz;
            if (q < end && *q == '>') { cz = q; p = q + 1; }                                   /* <!--> */
            else if (q + 1 < end && q[0] == '-' && q[1] == '>') { cz = q; p = q + 2; }       /* <!---> */
            else {
                while (q + 2 < end && !(q[0] == '-' && q[1] == '-' && q[2] == '>')) q++;
                if (q + 2 < end) { cz = q; p = q + 3; } else { cz = end; p = end; }
            }
            if (s->keep_comments) {   /* SOMA_188: el comentario queda en el DOM (lit-html, Vue, etc. lo usan como ancla) */
                struct wnode *cm = wd_new_text(s->d, cs, (uint32_t)(cz - cs));
                if (cm) { cm->type = WN_COMMENT; wd_append(cur(s), cm); }
            }
            continue;
        }
        if (p + 1 < end && (p[1] == '!' || p[1] == '?')) {
            flush_text(s);
            const char *q = p + 2; while (q < end && *q != '>') q++;
            /* SOMA_188: "comentario falso" del algoritmo HTML: <?...> (dato con el '?') y <!...> que no es DOCTYPE.
               lit-html marca asi las partes de texto (<?lit$123$>): sin el comentario no rellenaba listas ni contadores */
            if (s->keep_comments && !(p[1] == '!' && end - p >= 9 && wu_strnieq(p + 2, "doctype", 7))) {
                const char *cs = p[1] == '?' ? p + 1 : p + 2;
                struct wnode *cm = wd_new_text(s->d, cs, (uint32_t)(q - cs));
                if (cm) { cm->type = WN_COMMENT; wd_append(cur(s), cm); }
            }
            p = q < end ? q + 1 : end; continue;
        }
        int closing = (p + 1 < end && p[1] == '/');
        const char *q = p + 1 + closing;
        if (!(q < end && wu_isalpha(*q))) {
            if (closing && q < end && *q == '>') { p = q + 1; continue; }
            if (closing) { const char *r = q; while (r < end && *r != '>') r++; p = r < end ? r + 1 : end; continue; }
            wb_addc(&s->txt, '<'); p++; continue;
        }
        const char *ns = q; while (q < end && !wu_isspace(*q) && *q != '>' && *q != '/') q++;
        uint32_t nl = (uint32_t)(q - ns);
        flush_text(s);
        int tag = wd_tag_of(ns, nl);
        char lname[40]; uint32_t ll = nl < 39 ? nl : 39;
        for (uint32_t i = 0; i < ll; i++) lname[i] = (char)wu_lower(ns[i]);
        lname[ll] = 0;
        if (tag == WT_NONE) tag = wd_tag_of(lname, ll);

        if (closing) {
            const char *r = q; while (r < end && *r != '>') r++;
            p = r < end ? r + 1 : end;
            if (tag == WT_HTML || tag == WT_BODY || tag == WT_HEAD) continue;
            if (tag == WT_BR) { ensure_body(s); struct wnode *b = wd_new_elem(s->d, "br", 2); if (b) push(s, b); continue; }
            if (tag == WT_P) {
                int i = in_stack(s, WT_P, scope_stops, 8);
                if (i > 0) pop_to(s, i); else { ensure_body(s); struct wnode *e = wd_new_elem(s->d, "p", 1); if (e) wd_append(cur(s), e); }
                continue;
            }
            const int *stops = scope_stops; int ns2 = 8;
            if (tag == WT_LI) { stops = list_stops; ns2 = 7; }
            else if (tag == WT_TD || tag == WT_TH || tag == WT_TR || tag == WT_TBODY || tag == WT_THEAD || tag == WT_TFOOT) { stops = cell_stops; ns2 = 3; }
            else if (tag == WT_TABLE) { stops = cell_stops; ns2 = 1; }
            int idx = -1;
            for (int i = s->sp - 1; i > 0; i--) {
                struct wnode *n = s->stk[i];
                if ((tag != WT_NONE && n->tag == tag) || (tag == WT_NONE && wu_streq(n->name, lname))) { idx = i; break; }
                int stop = 0; for (int k = 0; k < ns2; k++) if (stops[k] == n->tag) stop = 1;
                if (stop) break;
            }
            if (idx > 0) pop_to(s, idx);
            continue;
        }

        /* etiqueta de apertura: atributos */
        struct wnode *el = wd_new_elem(s->d, lname, ll);
        if (!el) { p = end; break; }
        int self_close = 0;
        for (;;) {
            while (q < end && (wu_isspace(*q) || *q == '/')) { if (*q == '/') self_close = 1; q++; if (q < end && wu_isspace(*q)) self_close = 0; }
            if (q >= end || *q == '>') break;
            self_close = 0;
            const char *as = q;
            while (q < end && !wu_isspace(*q) && *q != '=' && *q != '>' && !(*q == '/' && q + 1 < end && q[1] == '>')) q++;
            uint32_t al = (uint32_t)(q - as);
            while (q < end && wu_isspace(*q)) q++;
            if (q < end && *q == '=') {
                q++; while (q < end && wu_isspace(*q)) q++;
                const char *vs; uint32_t vl;
                if (q < end && (*q == '"' || *q == '\'')) {
                    char qc = *q++; vs = q; while (q < end && *q != qc) q++; vl = (uint32_t)(q - vs); if (q < end) q++;
                } else { vs = q; while (q < end && !wu_isspace(*q) && *q != '>') q++; vl = (uint32_t)(q - vs); }
                if (al) add_attr_raw(s, el, as, al, vs, vl, 1);
            } else if (al) add_attr_raw(s, el, as, al, 0, 0, 0);
        }
        p = q < end ? q + 1 : end;
        /* SOMA_PARCHE_028_UI: dentro de <svg> (contenido foraneo) el cierre "/>" si cuenta: sin esto <path/><path/> quedaban anidados */
        int foreign_leaf = 0;
        if (self_close && tag != WT_SVG) { for (int i = s->sp - 1; i > 0; i--) if (s->stk[i]->tag == WT_SVG) { foreign_leaf = 1; break; } }

        /* estructura implicita */
        if (tag == WT_HTML) {
            ensure_html(s);
            if (s->d->html) for (struct wattr *a = el->attrs; a; a = a->next) if (!wd_attr(s->d->html, a->name)) wd_set_attr(s->d, s->d->html, a->name, a->val);
            continue;
        }
        if (tag == WT_HEAD) { if (!s->body_started) ensure_head(s); continue; }   /* SOMA_188: un <head> tras empezar el cuerpo se ignora */
        if (tag == WT_BODY) {
            if (s->body_started && s->d->body) { for (struct wattr *a = el->attrs; a; a = a->next) if (!wd_attr(s->d->body, a->name)) wd_set_attr(s->d, s->d->body, a->name, a->val); continue; }
            ensure_body(s);
            if (s->d->body) for (struct wattr *a = el->attrs; a; a = a->next) wd_set_attr(s->d, s->d->body, a->name, a->val);
            continue;
        }
        if (!s->frag && !s->body_started) {
            if (is_head_tag(tag) || tag == WT_NOSCRIPT) ensure_head(s); else ensure_body(s);
        }
        if (tag == WT_FORM && in_stack(s, WT_FORM, cell_stops, 3) > 0) continue;
        implied_end_for_start(s, tag);
        if (tag == WT_TD || tag == WT_TH) { if (cur(s)->tag == WT_TABLE || cur(s)->tag == WT_TBODY) { struct wnode *tr = wd_new_elem(s->d, "tr", 2); if (tr) push(s, tr); } }
        if (tag == WT_TR && cur(s)->tag == WT_TABLE) { struct wnode *tb = wd_new_elem(s->d, "tbody", 5); if (tb) push(s, tb); }
        if (s->sp >= MAX_DEPTH && !(el->flags & WNF_VOID)) s->sp--;
        push(s, el);
        if (foreign_leaf) { if (s->sp > 1 && cur(s) == el) s->sp--; continue; }

        /* elementos de texto crudo */
        int in_svg = 0; if (tag == WT_TITLE || tag == WT_STYLE) for (int i = s->sp - 2; i > 0; i--) if (s->stk[i]->tag == WT_SVG) { in_svg = 1; break; }   /* SOMA_188: <title> de SVG no es el del documento */
        if (in_svg && tag == WT_TITLE) { /* elemento normal dentro de <svg> */ }
        else if (tag == WT_SCRIPT || tag == WT_STYLE || tag == WT_TEXTAREA || tag == WT_TITLE || tag == WT_NOSCRIPT || tag == WT_IFRAME ||
            (tag == WT_NONE && (wu_streq(lname, "xmp") || wu_streq(lname, "noembed") || wu_streq(lname, "noframes") || wu_streq(lname, "plaintext")))) {
            const char *close = find_close(p, end, lname, ll);
            if (close > p) {
                if (tag == WT_TEXTAREA || tag == WT_TITLE) {
                    const char *b = p; if (tag == WT_TEXTAREA && *b == '\n') b++;
                    wd_decode_entities(b, (uint32_t)(close - b), &s->txt);
                } else wb_addn(&s->txt, p, (uint32_t)(close - p));
                flush_text(s);
            }
            if (tag == WT_TITLE && el->first && el->first->type == WN_TEXT && !s->d->title) s->d->title = el->first->name;
            p = close;
            if (p < end) { while (p < end && *p != '>') p++; if (p < end) p++; }
            s->sp--;
            continue;
        }
        if (tag == WT_PRE || tag == WT_TEXTAREA) s->skip_nl = 1;
    }
    flush_text(s);
}

void wd_parse(struct wdoc *d, const char *html, uint32_t len)
{
    struct ps *s = (struct ps *)kmalloc(sizeof(struct ps)); if (!s) return;
    memset(s, 0, sizeof(*s));
    s->d = d; s->stk[0] = d->root; s->sp = 1; s->keep_comments = !web_feat_off("htmlparse");
    parse_core(s, html, len);
    if (!d->html) ensure_html(s);
    if (!d->head) ensure_head(s);
    if (!d->body) ensure_body(s);
    wb_free(&s->txt); kfree(s);
}

void wd_parse_fragment(struct wdoc *d, struct wnode *ctx, const char *html, uint32_t len)
{
    struct ps *s = (struct ps *)kmalloc(sizeof(struct ps)); if (!s) return;
    memset(s, 0, sizeof(*s));
    s->d = d; s->stk[0] = ctx; s->sp = 1; s->frag = 1; s->body_started = 1; s->keep_comments = !web_feat_off("htmlparse");
    parse_core(s, html, len);
    wb_free(&s->txt); kfree(s);
}
