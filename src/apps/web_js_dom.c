/* SOMA_PARCHE_NAVEGADOR: puente DOM <-> JavaScript (document, elementos, estilo, almacenamiento, ventana). */
#include "web_js_i.h"
#include "web_net.h"
#include "rng.h"
#include "web_css.h"   /* SOMA_188: innerText usa el estilo calculado */

struct wcss;
int wc_query(struct wcss *c, struct wnode *root, const char *sel, struct wnode **out, int max, int first_only);
int wc_matches(struct wcss *c, struct wnode *n, const char *sel);
void wc_set_inline(struct wdoc *d, struct wnode *n, const char *prop, const char *val);
const char *wc_get_inline(struct wnode *n, const char *prop, char *out, uint32_t max);
#include "web_js_prelude.h"

#define ARG(i) ((i) < argc ? args[i] : jundef())
enum { HK_NODE = 1, HK_STYLE, HK_CSTYLE, HK_DATASET, HK_STORAGE, HK_CLASSLIST };

struct dom {
    jobj *p_node, *p_elem, *p_html, *p_doc, *p_text, *p_comment, *p_frag, *p_style, *p_cstyle, *p_dataset, *p_storage, *p_classlist, *p_nodelist;
    jobj *p_tag[WT_COUNT];
    jobj *local, *session;
    char url[WN_URL_MAX + 64]; struct wnode *cur_script; jobj *mo148_cb;
    jobj *wraps;                   /* SOMA 189: todos los envoltorios de nodos (wnode->js esta fuera del monton JS: esta lista los mantiene vivos) */
};
#define DOM(rt) ((struct dom *)(rt)->dom)
#define HOST(rt) ((rt)->host)
#define DOC(rt) ((rt)->doc)

static jv S(struct jrt *rt, const char *c) { return jstrv(js_str_cstr(rt, c ? c : "")); }
static jv Sn(struct jrt *rt, const char *c, uint32_t n) { return jstrv(js_str_new(rt, c, n)); }
static void touch(struct jrt *rt) { if (HOST(rt) && HOST(rt)->dirty) HOST(rt)->dirty(HOST(rt)->ud, 1); }
static jv hostobj(struct jrt *rt, jobj *proto, int kind, void *p, int dynamic)
{
    jobj *o = js_obj_new(rt, proto); o->cls = JC_HOST; o->u.host.kind = kind; o->u.host.p = p; if (dynamic) o->flag |= 1; return jobjv(o);
}
static struct wnode *node_or_null(jv v) { return (v.t == JT_OBJ && v.u.o->cls == JC_HOST && v.u.o->u.host.kind == HK_NODE) ? (struct wnode *)v.u.o->u.host.p : 0; }
static struct wnode *tonode(struct jrt *rt, jv v)
{
    struct wnode *n = node_or_null(v);
    if (!n) js_throw_err(rt, E_TYPE, "El valor no es un nodo del DOM");
    return n;
}
static jobj *wrap(struct jrt *rt, struct wnode *n)
{
    if (n->js) return (jobj *)n->js;
    struct dom *d = DOM(rt); jobj *proto;
    if (n->type == WN_DOC) proto = d->p_doc;
    else if (n->type == WN_TEXT) proto = d->p_text;
    else if (n->type == WN_COMMENT) proto = d->p_comment;
    else if (n->name && n->name[0] == '#') proto = d->p_frag;
    else proto = (n->tag < WT_COUNT && d->p_tag[n->tag]) ? d->p_tag[n->tag] : d->p_html;
    jobj *o = hostobj(rt, proto, HK_NODE, n, 0).u.o; if (n->type == WN_DOC || (n->type == WN_ELEM && n->tag == WT_FORM)) o->flag |= 2; n->js = o;   /* SOMA_187A: flag 2 = acceso por nombre */
    if (!d->wraps) d->wraps = js_arr_new(rt, 64);
    js_arr_push(rt, d->wraps, jobjv(o));
    return o;
}
static jv W(struct jrt *rt, struct wnode *n) { return n ? jobjv(wrap(rt, n)) : jnull(); }
/* SOMA_148_NATIVE_MO_BRIDGE: notificacion tras setters C, sin
 * ejecutar los callbacks del usuario hasta la cola de microtareas. */
static void mo148_emit(struct jrt *rt, struct wnode *n, const char *type, const char *name, const char *old)
{
    struct dom *state = DOM(rt);
    if (!state || !state->mo148_cb) return;
    jv cb = jobjv(state->mo148_cb);
    jv args[4] = { S(rt, type), W(rt, n), name ? S(rt, name) : jnull(), old ? S(rt, old) : jnull() };
    (void)js_call(rt, cb, jundef(), args, 4);
}
static int mo148_diff(const char *a, const char *b)
{
    if (!a || !b) return a != b;
    return !wu_streq(a, b);
}
static jstr *Kc(struct jrt *rt, const char *c);
static int is_elem(const struct wnode *n);
/* SOMA_188: listas sin tope de 1024 (childNodes, querySelectorAll, getElementsBy*...): bufer del tamano del documento */
static struct wnode **nbuf(struct jrt *rt, int *cap)
{
    int c = (int)DOC(rt)->nodes + 64; if (c < 64) c = 64; if (c > 400000) c = 400000;
    struct wnode **b = (struct wnode **)kmalloc((uint32_t)c * (uint32_t)sizeof *b);
    if (!b) js_throw_err(rt, E_RANGE, "memoria agotada");
    *cap = c; return b;
}
static jv nodelist_free(struct jrt *rt, struct wnode **ns, int n);
static jv nodelist(struct jrt *rt, struct wnode **ns, int n)
{
    jobj *a = js_arr_new(rt, (uint32_t)n); a->proto = DOM(rt)->p_nodelist;
    for (int i = 0; i < n; i++) js_arr_push(rt, a, W(rt, ns[i]));
    for (int i = 0; i < n; i++) {   /* acceso por nombre: coleccion.id / coleccion.name */
        if (!is_elem(ns[i])) continue;
        for (int k = 0; k < 2; k++) {
            const char *nm = wd_attr(ns[i], k ? "name" : "id");
            if (!nm || !nm[0] || (nm[0] >= '0' && nm[0] <= '9')) continue;
            jstr *key = Kc(rt, nm);
            if (!js_has(rt, a, key)) js_def(rt, a, key, W(rt, ns[i]), 0);
        }
    }
    return jobjv(a);
}
static struct wcss *css(struct jrt *rt) { return (struct wcss *)DOC(rt)->sheets; }
static int is_elem(const struct wnode *n) { return n && n->type == WN_ELEM && n->name && n->name[0] != '#'; }
static int is_frag(const struct wnode *n) { return n && n->type == WN_ELEM && n->name && n->name[0] == '#'; }
static char *dupn(struct jrt *rt, const char *s, uint32_t n)
{
    char *p = wa_strndup(&DOC(rt)->ar, s, n);
    if (!p) js_throw_err(rt, E_RANGE, "memoria agotada");
    return p;
}
static jstr *Kc(struct jrt *rt, const char *c) { return js_intern_c(rt, c); }
static jv hidden_get(struct jrt *rt, jobj *o, const char *key) { return js_get(rt, jobjv(o), Kc(rt, key)); }

/* ------------------------------------------------------------ camelCase <-> guiones */
static void hyphenate(const char *in, char *out, uint32_t max)
{
    uint32_t o = 0;
    if (wu_streq(in, "cssFloat")) in = "float";
    if ((wu_starts(in, "webkit") && in[6] >= 'A' && in[6] <= 'Z') || (wu_starts(in, "moz") && in[3] >= 'A' && in[3] <= 'Z') || (wu_starts(in, "ms") && in[2] >= 'A' && in[2] <= 'Z')) out[o++] = '-';
    for (; *in && o + 3 < max; in++) { if (*in >= 'A' && *in <= 'Z') { if (o) out[o++] = '-'; out[o++] = (char)(*in + 32); } else out[o++] = *in; }
    out[o] = 0;
}
static void camelize(const char *in, char *out, uint32_t max)
{
    uint32_t o = 0;
    for (; *in && o + 2 < max; in++) { if (*in == '-' && in[1] >= 'a' && in[1] <= 'z') { in++; out[o++] = (char)(*in - 32); } else out[o++] = *in; }
    out[o] = 0;
}
static int css_key_ok(const char *k) { if (!*k) return 0; for (; *k; k++) if (!(wu_isalnum(*k) || *k == '-' || *k == '_')) return 0; return 1; }

/* ------------------------------------------------------------ propiedades dinamicas (style, dataset, storage) */
static const char *storage_op(struct jrt *rt, int local, const char *k, const char *v, int op)
{
    if (!HOST(rt) || !HOST(rt)->storage) return 0;
    return HOST(rt)->storage(HOST(rt)->ud, local, k, v, op);
}
static int is_local(jobj *o) { return o->u.host.p == (void *)1; }
static jv dyn_get(struct jrt *rt, jobj *o, jstr *key, int *found)
{
    char buf[600], prop[120]; struct wnode *n = (struct wnode *)o->u.host.p;
    switch (o->u.host.kind) {
    case HK_STYLE:
        if (!css_key_ok(key->d)) return jundef();
        hyphenate(key->d, prop, sizeof prop); wc_get_inline(n, prop, buf, sizeof buf); *found = 1; return S(rt, buf);
    case HK_CSTYLE:
        if (!css_key_ok(key->d)) return jundef();
        hyphenate(key->d, prop, sizeof prop); buf[0] = 0;
        if (HOST(rt) && HOST(rt)->computed) HOST(rt)->computed(HOST(rt)->ud, n, prop, buf, sizeof buf);
        *found = 1; return S(rt, buf);
    case HK_DATASET: {
        wu_copy(buf, "data-", sizeof buf); hyphenate(key->d, buf + 5, sizeof buf - 5);
        const char *v = wd_attr(n, buf); if (!v) return jundef(); *found = 1; return S(rt, v);
    }
    case HK_STORAGE: { const char *v = storage_op(rt, is_local(o), key->d, 0, 0); if (!v) return jundef(); *found = 1; return S(rt, v); }
    default: return jundef();
    }
}
static void dyn_put(struct jrt *rt, jobj *o, jstr *key, jv val)
{
    char buf[600], prop[120]; struct wnode *n = (struct wnode *)o->u.host.p; jstr *vs = js_tostr(rt, val);
    switch (o->u.host.kind) {
    case HK_STYLE:
        if (!css_key_ok(key->d)) return;
        hyphenate(key->d, prop, sizeof prop); const char *old_style = wd_attr(n, "style");
        wc_set_inline(DOC(rt), n, prop, vs->len ? vs->d : "");
        if (mo148_diff(old_style, wd_attr(n, "style"))) mo148_emit(rt, n, "attributes", "style", old_style);
        touch(rt); return;
    case HK_DATASET:
        wu_copy(buf, "data-", sizeof buf); hyphenate(key->d, buf + 5, sizeof buf - 5);
        const char *old_data = wd_attr(n, buf);
        wd_set_attr(DOC(rt), n, buf, dupn(rt, vs->d, vs->len));
        mo148_emit(rt, n, "attributes", buf, old_data); touch(rt); return;
    case HK_STORAGE: storage_op(rt, is_local(o), key->d, vs->d, 1); return;
    default: return;
    }
}
static int dyn_del(struct jrt *rt, jobj *o, jstr *key)
{
    char buf[600], prop[120]; struct wnode *n = (struct wnode *)o->u.host.p;
    switch (o->u.host.kind) {
    case HK_STYLE: { hyphenate(key->d, prop, sizeof prop); const char *old = wd_attr(n, "style");
        wc_set_inline(DOC(rt), n, prop, "");
        if (mo148_diff(old, wd_attr(n, "style"))) mo148_emit(rt, n, "attributes", "style", old);
        touch(rt); return 1; }
    case HK_DATASET: { wu_copy(buf, "data-", sizeof buf); hyphenate(key->d, buf + 5, sizeof buf - 5);
        const char *old = wd_attr(n, buf); wd_del_attr(n, buf);
        if (old) mo148_emit(rt, n, "attributes", buf, old);
        touch(rt); return 1; }
    case HK_STORAGE: storage_op(rt, is_local(o), key->d, 0, 2); return 1;
    default: return 1;
    }
}
static int dyn_has(struct jrt *rt, jobj *o, jstr *key)
{
    char buf[600];
    switch (o->u.host.kind) {
    case HK_DATASET: wu_copy(buf, "data-", sizeof buf); hyphenate(key->d, buf + 5, sizeof buf - 5); return wd_attr((struct wnode *)o->u.host.p, buf) != 0;
    case HK_STORAGE: return storage_op(rt, is_local(o), key->d, 0, 0) != 0;
    default: return 0;
    }
}
static jobj *dyn_keys(struct jrt *rt, jobj *o)
{
    jobj *out = js_arr_new(rt, 4); char buf[200];
    if (o->u.host.kind == HK_DATASET) {
        for (struct wattr *a = ((struct wnode *)o->u.host.p)->attrs; a; a = a->next)
            if (wu_starts(a->name, "data-")) { camelize(a->name + 5, buf, sizeof buf); js_arr_push(rt, out, S(rt, buf)); }
    } else if (o->u.host.kind == HK_STORAGE) {
        for (int i = 0; i < 4096; i++) { char ib[16]; wu_itoa(i, ib); const char *k = storage_op(rt, is_local(o), ib, 0, 4); if (!k) break; js_arr_push(rt, out, S(rt, k)); }
    }
    return out;
}
/* SOMA_187A: acceso por nombre. document.f devuelve <form name="f"> (tambien <img>, <embed>, <iframe> con name) y form.q devuelve el control con name o id "q" */
static jv dyn_named(struct jrt *rt, jobj *o, jstr *key, int *found)
{
    struct wnode *d = (struct wnode *)o->u.host.p;
    if (!d || !key->len || key->d[0] == '@') return jundef();
    int doc = d->type == WN_DOC;
    if (!doc && !(d->type == WN_ELEM && d->tag == WT_FORM)) return jundef();
    for (struct wnode *n = d->first; n; ) {
        if (n->type == WN_ELEM) {
            const char *nm = wd_attr(n, "name");
            if (doc) { if ((n->tag == WT_FORM || n->tag == WT_IMG || n->tag == WT_EMBED || n->tag == WT_IFRAME) && nm && wu_streq(nm, key->d)) { *found = 1; return W(rt, n); } }
            else if (n->tag == WT_INPUT || n->tag == WT_SELECT || n->tag == WT_TEXTAREA || n->tag == WT_BUTTON || n->tag == WT_IMG) {
                const char *id = wd_attr(n, "id");
                if ((nm && wu_streq(nm, key->d)) || (id && wu_streq(id, key->d))) { *found = 1; return W(rt, n); }
            }
        }
        if (n->first) { n = n->first; continue; }
        while (n && n != d && !n->next) n = n->parent;
        if (!n || n == d) break;
        n = n->next;
    }
    return jundef();
}
static const struct jdyn DYN = { dyn_get, dyn_put, dyn_del, dyn_has, dyn_keys, dyn_named };

/* ------------------------------------------------------------ utilidades de nodos */
static int script_is_js(const struct wnode *n)
{
    const char *t = wd_attr(n, "type");
    if (!t || !*t) return 1;
    return wu_strieq(t, "text/javascript") || wu_strieq(t, "application/javascript") || wu_strieq(t, "text/ecmascript") || wu_strieq(t, "application/ecmascript") || wu_strieq(t, "text/jscript") || wu_strieq(t, "text/babel");
}
static void run_script_node(struct jrt *rt, struct wnode *n)
{
    if (n->sel) return;
    n->sel = 1;
    if (!script_is_js(n)) return;
    const char *src = wd_attr(n, "src");
    if (src && *src) { if (HOST(rt)->load_script) HOST(rt)->load_script(HOST(rt)->ud, n); return; }
    struct wb b = {0}; wd_text_content(n, &b);
    if (b.len) { struct wnode *prev = DOM(rt)->cur_script; DOM(rt)->cur_script = n; wjs_eval(rt->js, b.p, b.len, "script"); DOM(rt)->cur_script = prev; }
    wb_free(&b);
}
static void run_scripts(struct jrt *rt, struct wnode *n)
{
    if (!(n->flags & WNF_INDOC)) return;
    if (n->tag == WT_SCRIPT && n->type == WN_ELEM) { run_script_node(rt, n); return; }
    for (struct wnode *c = n->first; c; c = c->next) run_scripts(rt, c);
}
/* SOMA_146: scripts parseados desde cadenas HTML son inertes; los scripts
 * creados por document.createElement('script') siguen ejecutandose al insertarse. */
static void mark_fragment_scripts_inert(struct wnode *root)
{
    struct wnode *n = root;
    while (n) {
        if (n->type == WN_ELEM && n->tag == WT_SCRIPT) n->sel = 1;
        if (n->first) { n = n->first; continue; }
        while (n && n != root && !n->next) n = n->parent;
        if (!n || n == root) break;
        n = n->next;
    }
}
static void insert_node(struct jrt *rt, struct wnode *parent, struct wnode *c, struct wnode *ref)
{
    if (parent->type == WN_TEXT || parent->type == WN_COMMENT) js_throw_err(rt, E_ERROR, "HierarchyRequestError: el nodo padre no admite hijos");
    if (c == parent || wd_is_ancestor(c, parent)) js_throw_err(rt, E_ERROR, "HierarchyRequestError: no se puede insertar un nodo dentro de si mismo");
    if (ref && ref->parent != parent) js_throw_err(rt, E_ERROR, "NotFoundError: el nodo de referencia no es hijo de este nodo");
    if (is_frag(c)) {
        while (c->first) { struct wnode *k = c->first; wd_remove(k); if (ref) wd_insert_before(parent, k, ref); else wd_append(parent, k); run_scripts(rt, k); }
        touch(rt); return;
    }
    if (c == ref) return;
    if (c->parent) wd_remove(c);
    if (ref) wd_insert_before(parent, c, ref); else wd_append(parent, c);
    run_scripts(rt, c); touch(rt);
}
static struct wnode *to_node_or_text(struct jrt *rt, jv v)
{
    struct wnode *n = node_or_null(v);
    if (n) return n;
    jstr *s = js_tostr(rt, v); return wd_new_text(DOC(rt), dupn(rt, s->d, s->len), s->len);
}
static struct wnode *first_elem(struct wnode *n) { for (n = n->first; n; n = n->next) if (n->type == WN_ELEM) return n; return 0; }
static struct wnode *last_elem(struct wnode *n) { struct wnode *r = 0; for (n = n->first; n; n = n->next) if (n->type == WN_ELEM) r = n; return r; }
static struct wnode *next_elem(struct wnode *n) { for (n = n->next; n; n = n->next) if (n->type == WN_ELEM) return n; return 0; }
static struct wnode *prev_elem(struct wnode *n) { for (n = n->prev; n; n = n->prev) if (n->type == WN_ELEM) return n; return 0; }
static void collect_tag(struct wnode *root, const char *tag, int all, struct wnode **out, int *n, int max)
{
    for (struct wnode *c = root->first; c && *n < max; c = c->next) {
        if (c->type != WN_ELEM) continue;
        if (all || wu_streq(c->name, tag)) out[(*n)++] = c;
        collect_tag(c, tag, all, out, n, max);
    }
}
static int has_all_classes(const struct wnode *n, const char *list)
{
    char cls[96]; int any = 0; const char *p = list;
    while (*p) {
        while (wu_isspace(*p)) p++;
        uint32_t k = 0; while (*p && !wu_isspace(*p) && k + 1 < sizeof cls) cls[k++] = *p++;
        while (*p && !wu_isspace(*p)) p++;
        cls[k] = 0; if (!k) continue; any = 1;
        if (!wd_has_class(n, cls)) return 0;
    }
    return any;
}
static void collect_class(struct wnode *root, const char *list, struct wnode **out, int *n, int max)
{
    for (struct wnode *c = root->first; c && *n < max; c = c->next) {
        if (c->type != WN_ELEM) continue;
        if (has_all_classes(c, list)) out[(*n)++] = c;
        collect_class(c, list, out, n, max);
    }
}
static void collect_name(struct wnode *root, const char *nm, struct wnode **out, int *n, int max)
{
    for (struct wnode *c = root->first; c && *n < max; c = c->next) {
        if (c->type != WN_ELEM) continue;
        const char *v = wd_attr(c, "name"); if (v && wu_streq(v, nm)) out[(*n)++] = c;
        collect_name(c, nm, out, n, max);
    }
}
static void lower_copy(const char *s, uint32_t n, char *out, uint32_t max) { uint32_t i = 0; for (; i < n && i + 1 < max; i++) out[i] = (char)wu_lower(s[i]); out[i] = 0; }
static void opt_collect(struct wnode *n, struct wnode **o, int *c, int max)
{
    for (struct wnode *k = n->first; k && *c < max; k = k->next) { if (k->tag == WT_OPTION) o[(*c)++] = k; else if (k->tag == WT_OPTGROUP) opt_collect(k, o, c, max); }
}
static int sel_index(struct wnode *n)
{
    struct wnode *o[256]; int c = 0; opt_collect(n, o, &c, 256);
    if (n->flags & WNF_EDITED) return n->sel < c ? n->sel : (c ? 0 : -1);
    for (int i = 0; i < c; i++) if (wd_attr(o[i], "selected")) return i;
    return c ? 0 : -1;
}
static jv opt_text(struct jrt *rt, struct wnode *o) { struct wb b = {0}; wd_text_content(o, &b); jv r = Sn(rt, b.p ? b.p : "", b.len); wb_free(&b); return r; }
static jv opt_value(struct jrt *rt, struct wnode *o) { const char *v = wd_attr(o, "value"); return v ? S(rt, v) : opt_text(rt, o); }
static struct wnode *form_of(struct wnode *n) { for (struct wnode *p = n->parent; p; p = p->parent) if (p->tag == WT_FORM) return p; return 0; }
static void collect_controls(struct wnode *root, struct wnode **out, int *n, int max)
{
    for (struct wnode *c = root->first; c && *n < max; c = c->next) {
        if (c->type != WN_ELEM) continue;
        if (c->tag == WT_INPUT || c->tag == WT_SELECT || c->tag == WT_TEXTAREA || c->tag == WT_BUTTON || c->tag == WT_FIELDSET || c->tag == WT_OBJECT) out[(*n)++] = c;
        collect_controls(c, out, n, max);
    }
}
static int is_checkable(struct wnode *n)
{
    if (n->tag != WT_INPUT) return 0;
    const char *t = wd_attr(n, "type"); return t && (wu_strieq(t, "checkbox") || wu_strieq(t, "radio"));
}
static const char *ctl_value(struct jrt *rt, struct wnode *n, jv *tmp)
{
    if (n->tag == WT_SELECT) { struct wnode *o[256]; int c = 0, i = sel_index(n); opt_collect(n, o, &c, 256); if (i < 0 || i >= c) return ""; *tmp = opt_value(rt, o[i]); return tmp->u.s->d; }
    if (n->tag == WT_OPTION) { *tmp = opt_value(rt, n); return tmp->u.s->d; }
    if (n->value) return n->value;
    if (n->tag == WT_TEXTAREA) { struct wb b = {0}; wd_text_content(n, &b); *tmp = Sn(rt, b.p ? b.p : "", b.len); wb_free(&b); return tmp->u.s->d; }
    const char *v = wd_attr(n, "value");
    if (!v && is_checkable(n)) return "on";
    return v ? v : "";
}
static void set_ctl_value(struct jrt *rt, struct wnode *n, jstr *s)
{
    if (n->tag == WT_SELECT) {
        struct wnode *o[256]; int c = 0; opt_collect(n, o, &c, 256); n->sel = -1;
        for (int i = 0; i < c; i++) { jv v = opt_value(rt, o[i]); if (v.u.s->len == s->len && !memcmp(v.u.s->d, s->d, s->len)) { n->sel = i; break; } }
        n->flags |= WNF_EDITED; touch(rt); return;
    }
    if (n->tag == WT_OPTION) { wd_set_attr(DOC(rt), n, "value", dupn(rt, s->d, s->len)); return; }
    n->value = dupn(rt, s->d, s->len); n->flags |= WNF_EDITED; touch(rt);
}
static void set_checked(struct jrt *rt, struct wnode *n, int on)
{
    if (on) n->flags |= WNF_CHECKED; else n->flags &= (uint16_t)~WNF_CHECKED;
    n->flags |= WNF_EDITED;
    const char *t = wd_attr(n, "type");
    if (on && t && wu_strieq(t, "radio")) {
        const char *nm = wd_attr(n, "name");
        if (nm && *nm) {
            struct wnode *root = form_of(n); if (!root) root = DOC(rt)->root;
            struct wnode *ctl[512]; int c = 0; collect_controls(root, ctl, &c, 512);
            for (int i = 0; i < c; i++) if (ctl[i] != n && ctl[i]->tag == WT_INPUT) { const char *t2 = wd_attr(ctl[i], "type"), *n2 = wd_attr(ctl[i], "name"); if (t2 && wu_strieq(t2, "radio") && n2 && wu_streq(n2, nm)) ctl[i]->flags &= (uint16_t)~WNF_CHECKED; }
        }
    }
    touch(rt);
}
static void reset_form(struct wnode *f)
{
    for (struct wnode *c = f->first; c; c = c->next) {
        if (c->type != WN_ELEM) continue;
        c->value = 0; c->flags &= (uint16_t)~WNF_EDITED;
        if (is_checkable(c)) { if (wd_attr(c, "checked")) c->flags |= WNF_CHECKED; else c->flags &= (uint16_t)~WNF_CHECKED; }
        reset_form(c);
    }
}
/* class tokens */
static jv class_get(struct jrt *rt, struct wnode *n) { const char *c = wd_attr(n, "class"); return S(rt, c ? c : ""); }
static int cl_has(const char *list, const char *tok, uint32_t tl)
{
    const char *p = list ? list : "";
    while (*p) { while (wu_isspace(*p)) p++; const char *s = p; while (*p && !wu_isspace(*p)) p++; if ((uint32_t)(p - s) == tl && !memcmp(s, tok, tl)) return 1; }
    return 0;
}
static void cl_edit(struct jrt *rt, struct wnode *n, const char *tok, uint32_t tl, int add)
{
    const char *cur = wd_attr(n, "class"); struct wb b = {0}; const char *p = cur ? cur : ""; int has = 0;
    while (*p) {
        while (wu_isspace(*p)) p++;
        const char *s = p; while (*p && !wu_isspace(*p)) p++;
        if (p == s) break;
        if ((uint32_t)(p - s) == tl && !memcmp(s, tok, tl)) { has = 1; if (!add) continue; }
        if (b.len) wb_addc(&b, ' ');
        wb_addn(&b, s, (uint32_t)(p - s));
    }
    if (add && !has) { if (b.len) wb_addc(&b, ' '); wb_addn(&b, tok, tl); }
    if (cur ? mo148_diff(cur, b.p ? b.p : "") : (b.len != 0)) {
        wd_set_attr(DOC(rt), n, "class", dupn(rt, b.p ? b.p : "", b.len));
        mo148_emit(rt, n, "attributes", "class", cur); touch(rt);
    }
    wb_free(&b);
}
static void check_token(struct jrt *rt, jstr *t)
{
    if (!t->len) js_throw_err(rt, E_SYNTAX, "SyntaxError: el token no puede estar vacio");
    for (uint32_t i = 0; i < t->len; i++) if (wu_isspace(t->d[i])) js_throw_err(rt, E_ERROR, "InvalidCharacterError: el token contiene espacios");
}

/* ------------------------------------------------------------ URLs y geometria */
static jv abs_url(struct jrt *rt, const char *v)
{
    char out[WN_URL_MAX + 64]; const char *base = DOC(rt)->base[0] ? DOC(rt)->base : DOM(rt)->url;
    if (!v || !*v) return S(rt, base);
    if (wn_url_resolve(base, v, out, sizeof out) != 0) return S(rt, v);
    return S(rt, out);
}
static void node_rect(struct jrt *rt, struct wnode *n, int *x, int *y, int *w, int *h)
{
    *x = *y = *w = *h = 0;
    if (HOST(rt) && HOST(rt)->layout_rect) HOST(rt)->layout_rect(HOST(rt)->ud, n, x, y, w, h);
}
static void view_info(struct jrt *rt, int *w, int *h, int *sx, int *sy)
{
    *w = 800; *h = 600; *sx = *sy = 0;
    if (HOST(rt) && HOST(rt)->viewport) HOST(rt)->viewport(HOST(rt)->ud, w, h, sx, sy);
}
static jv link_part(struct jrt *rt, struct wnode *n, int which)
{
    jv h = abs_url(rt, wd_attr(n, "href")); struct wn_url u; char tmp[WN_URL_MAX + 64];
    if (wn_url_parse(h.u.s->d, &u) != 0) return S(rt, "");
    switch (which) {
    case 0: wu_copy(tmp, u.scheme, sizeof tmp); wu_copy(tmp + wu_strlen(tmp), ":", 2); return S(rt, tmp);
    case 1: case 2: case 7: {
        int dp = (wu_streq(u.scheme, "http") && u.port == 80) || (wu_streq(u.scheme, "https") && u.port == 443) || u.port <= 0;
        if (which == 2) return S(rt, u.host);
        uint32_t k = 0; if (which == 7) { k = wu_copy(tmp, u.scheme, sizeof tmp); k += wu_copy(tmp + k, "://", sizeof tmp - k); }
        k += wu_copy(tmp + k, u.host, sizeof tmp - k);
        if (!dp) { tmp[k++] = ':'; k += (uint32_t)wu_itoa(u.port, tmp + k); }
        tmp[k] = 0; return S(rt, tmp);
    }
    case 3: { tmp[0] = 0; int dp = (wu_streq(u.scheme, "http") && u.port == 80) || (wu_streq(u.scheme, "https") && u.port == 443) || u.port <= 0; if (!dp) wu_itoa(u.port, tmp); return S(rt, tmp); }
    case 4: case 5: {
        const char *q = wu_strchr(u.path, '?');
        if (which == 4) return Sn(rt, u.path, q ? (uint32_t)(q - u.path) : wu_strlen(u.path));
        return S(rt, q && q[1] ? q : "");
    }
    case 6: if (!u.frag[0]) return S(rt, ""); tmp[0] = '#'; wu_copy(tmp + 1, u.frag, sizeof tmp - 1); return S(rt, tmp);
    }
    return S(rt, "");
}

/* ------------------------------------------------------------ atributos reflejados */
enum { AK_STR, AK_URL, AK_BOOL, AK_INT };
struct attrdef { const char *prop, *attr; int kind; };
static const struct attrdef ATTRS[] = {
    { "src", "src", AK_URL }, { "href", "href", AK_URL }, { "action", "action", AK_URL }, { "poster", "poster", AK_URL }, { "alt", "alt", AK_STR }, { "name", "name", AK_STR },
    { "placeholder", "placeholder", AK_STR }, { "rel", "rel", AK_STR }, { "target", "target", AK_STR }, { "method", "method", AK_STR }, { "htmlFor", "for", AK_STR }, { "content", "content", AK_STR },
    { "charset", "charset", AK_STR }, { "media", "media", AK_STR }, { "width", "width", AK_INT }, { "height", "height", AK_INT }, { "colSpan", "colspan", AK_INT }, { "rowSpan", "rowspan", AK_INT },
    { "maxLength", "maxlength", AK_INT }, { "min", "min", AK_STR }, { "max", "max", AK_STR }, { "step", "step", AK_STR }, { "pattern", "pattern", AK_STR }, { "accept", "accept", AK_STR },
    { "autocomplete", "autocomplete", AK_STR }, { "role", "role", AK_STR }, { "download", "download", AK_STR }, { "async", "async", AK_BOOL }, { "defer", "defer", AK_BOOL },
    { "disabled", "disabled", AK_BOOL }, { "readOnly", "readonly", AK_BOOL }, { "required", "required", AK_BOOL }, { "multiple", "multiple", AK_BOOL }, { "autofocus", "autofocus", AK_BOOL },
    { "open", "open", AK_BOOL }, { "noValidate", "novalidate", AK_BOOL }, { "crossOrigin", "crossorigin", AK_STR }, { "integrity", "integrity", AK_STR }, { "srcset", "srcset", AK_STR },
    { "sizes", "sizes", AK_STR }, { "loading", "loading", AK_STR }, { "decoding", "decoding", AK_STR }, { "referrerPolicy", "referrerpolicy", AK_STR }, { "enctype", "enctype", AK_STR },
    { "label", "label", AK_STR }, { "wrap", "wrap", AK_STR }, { "cols", "cols", AK_INT }, { "rows", "rows", AK_INT }, { "size", "size", AK_INT }, { "start", "start", AK_INT },
    { "scope", "scope", AK_STR }, { "title", "title", AK_STR }, { "lang", "lang", AK_STR }, { "dir", "dir", AK_STR }, { "accessKey", "accesskey", AK_STR }, { "slot", "slot", AK_STR },
    { "useMap", "usemap", AK_STR }, { "httpEquiv", "http-equiv", AK_STR }, { "nonce", "nonce", AK_STR }, { "inputMode", "inputmode", AK_STR }, { "autocapitalize", "autocapitalize", AK_STR },
    { "controls", "controls", AK_BOOL }, { "loop", "loop", AK_BOOL }, { "muted", "muted", AK_BOOL }, { "autoplay", "autoplay", AK_BOOL }, { "preload", "preload", AK_STR },
    { "frameBorder", "frameborder", AK_STR }, { "allowFullscreen", "allowfullscreen", AK_BOOL }, { "sandbox", "sandbox", AK_STR }, { "align", "align", AK_STR }, { "bgColor", "bgcolor", AK_STR },
    { "valign", "valign", AK_STR }, { "border", "border", AK_STR }, { "cellPadding", "cellpadding", AK_STR }, { "cellSpacing", "cellspacing", AK_STR }, { "noModule", "nomodule", AK_BOOL },
    { "as", "as", AK_STR }, { "hreflang", "hreflang", AK_STR }, { "ping", "ping", AK_STR }, { "span", "span", AK_INT }, { "dateTime", "datetime", AK_STR }, { "cite", "cite", AK_STR },
    { "acceptCharset", "accept-charset", AK_STR }, { "formAction", "formaction", AK_URL }, { "formMethod", "formmethod", AK_STR }, { "formTarget", "formtarget", AK_STR }, { "enterKeyHint", "enterkeyhint", AK_STR },
    { 0, 0, 0 }
};
#define ATTR_BASE 1000
static jv attr_prop(struct jrt *rt, struct wnode *n, const struct attrdef *a, int argc, jv *args)
{
    if (argc > 0) {
        const char *old = wd_attr(n, a->attr);
        if (a->kind == AK_BOOL) { if (js_truthy(args[0])) wd_set_attr(DOC(rt), n, a->attr, ""); else wd_del_attr(n, a->attr); }
        else { jstr *s = js_tostr(rt, args[0]); wd_set_attr(DOC(rt), n, a->attr, dupn(rt, s->d, s->len)); }
        if (old || wd_attr(n, a->attr)) mo148_emit(rt, n, "attributes", a->attr, old);
        touch(rt); return jundef();
    }
    const char *v = wd_attr(n, a->attr);
    switch (a->kind) {
    case AK_BOOL: return jbool(v != 0);
    case AK_URL: return v ? abs_url(rt, v) : S(rt, "");
    case AK_INT: if (!v) return jint(a->attr[0] == 'c' && a->attr[1] == 'o' && a->attr[2] == 'l' && a->attr[3] == 's' ? 20 : (a->attr[0] == 'r' ? 2 : (a->attr[0] == 's' ? (n->tag == WT_SELECT ? 0 : 20) : (a->attr[0] == 'm' ? -1 : 0)))); return jint(wu_atoi(v));
    default: return S(rt, v ? v : "");
    }
}

/* ------------------------------------------------------------ propiedades de nodo */
enum {
    P_NODETYPE = 1, P_NODENAME, P_NODEVALUE, P_PARENTNODE, P_PARENTELEMENT, P_CHILDNODES, P_FIRSTCHILD, P_LASTCHILD, P_NEXTSIB, P_PREVSIB, P_OWNERDOC, P_ISCONNECTED, P_TEXTCONTENT, P_BASEURI,
    P_TAGNAME, P_LOCALNAME, P_NS, P_ID, P_CLASSNAME, P_CHILDREN, P_FIRSTELEM, P_LASTELEM, P_NEXTELEM, P_PREVELEM, P_CHILDCOUNT, P_INNERHTML, P_OUTERHTML, P_INNERTEXT, P_CLASSLIST, P_STYLE, P_DATASET, P_ATTRIBUTES,
    P_OFFSETW, P_OFFSETH, P_OFFSETT, P_OFFSETL, P_OFFSETPARENT, P_CLIENTW, P_CLIENTH, P_CLIENTT, P_CLIENTL, P_SCROLLW, P_SCROLLH, P_SCROLLT, P_SCROLLL, P_TABINDEX, P_HIDDEN, P_CONTENTEDITABLE, P_ISCE, P_SHADOWROOT,
    P_VALUE, P_CHECKED, P_DEFCHECKED, P_DEFVALUE, P_SELECTED, P_SELINDEX, P_OPTIONS, P_FORM, P_ELEMENTS, P_LENGTH, P_FILES, P_INDETERMINATE, P_SELSTART, P_SELEND, P_VALIDITY, P_WILLVALIDATE, P_TEXT, P_TYPE,
    P_LABELS, P_NATW, P_NATH, P_COMPLETE, P_CURRENTSRC, P_CONTENTWINDOW, P_CONTENTDOC, P_ROWS_UNUSED, P_VALUEASNUMBER, P_SELECTEDOPTIONS, P_INDEX, P_CELLS_UNUSED,
    P_LINK0 = 200,
    P_DOCELEM = 300, P_HEAD, P_BODY, P_TITLE, P_URL, P_DOMAIN, P_REFERRER, P_COOKIE, P_READYSTATE, P_CHARSET, P_CONTENTTYPE, P_COMPAT, P_DEFVIEW, P_ACTIVEEL, P_FORMS, P_IMAGES, P_LINKS, P_SCRIPTS, P_ALL,
    P_STYLESHEETS, P_FONTS, P_DOCHIDDEN, P_VISIBILITY, P_LASTMOD, P_DOCTYPE, P_SCROLLINGEL, P_EMBEDS, P_CURRENTSCRIPT
};
static jv text_of(struct jrt *rt, struct wnode *n)
{
    if (n->type == WN_TEXT || n->type == WN_COMMENT) return Sn(rt, n->name ? n->name : "", n->tlen);
    struct wb b = {0}; wd_text_content(n, &b); jv r = Sn(rt, b.p ? b.p : "", b.len); wb_free(&b); return r;
}
/* SOMA_188: innerText segun la maquetacion (algoritmo "rendered text collection" simplificado, como Chrome):
   omite display:none, visibility:hidden, <script>/<style>/<template>; colapsa espacios segun white-space; saltos por
   bloques (2 en <p>), <br>, tabuladores entre celdas; aplica text-transform. Sin estilo calculado: textContent. */
struct itx { struct wb *b; int req; int ws_pending; };
static void itx_flush(struct itx *x)
{
    if (x->req && x->b->len) { while (x->b->len && x->b->p[x->b->len - 1] == ' ') x->b->len--; for (int i = 0; i < x->req; i++) wb_addc(x->b, '\n'); }
    x->req = 0;
}
static void itx_req(struct itx *x, int n) { if (n > x->req) x->req = n; }
static void itx_text(struct itx *x, const struct wstyle *ps, const char *t, uint32_t n)
{
    int ws = ps ? ps->white_space : 0, tt = ps ? ps->text_transform : 0;
    int pre = ws == WS_PRE || ws == WS_PRE_WRAP, keepnl = pre || ws == WS_PRE_LINE;
    for (uint32_t i = 0; i < n; i++) {
        char c = t[i];
        if (!pre && (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f')) {
            if (c == '\n' && keepnl) { x->ws_pending = 0; itx_flush(x); wb_addc(x->b, '\n'); continue; }
            x->ws_pending = 1; continue;
        }
        if (c == '\r') continue;
        if (x->req) { x->ws_pending = 0; itx_flush(x); }
        if (x->ws_pending) { x->ws_pending = 0; if (x->b->len && x->b->p[x->b->len - 1] != '\n' && x->b->p[x->b->len - 1] != ' ' && x->b->p[x->b->len - 1] != '\t') wb_addc(x->b, ' '); }
        if (tt == 1 && c >= 'a' && c <= 'z') c = (char)(c - 32); else if (tt == 2 && c >= 'A' && c <= 'Z') c = (char)(c + 32);
        else if (tt == 3 && c >= 'a' && c <= 'z' && (!x->b->len || x->b->p[x->b->len - 1] == ' ' || x->b->p[x->b->len - 1] == '\n')) c = (char)(c - 32);
        wb_addc(x->b, c);
    }
}
static void itx_walk(struct itx *x, struct wnode *n, int depth)
{
    if (depth > 400) return;
    for (struct wnode *c = n->first; c; c = c->next) {
        if (c->type == WN_TEXT) { const struct wstyle *ps = n->st; if (ps && ps->visibility) continue; if (c->tlen) itx_text(x, ps, c->name, c->tlen); continue; }
        if (c->type != WN_ELEM) continue;
        const struct wstyle *s = c->st;
        if (!s || s->display == D_NONE) continue;
        int t = c->tag;
        if (t == WT_SCRIPT || t == WT_STYLE || t == WT_TEMPLATE || t == WT_HEAD || t == WT_NOSCRIPT) continue;
        if (t == WT_BR) { x->ws_pending = 0; if (x->req) itx_flush(x); wb_addc(x->b, '\n'); continue; }
        int d = s->display, blockish = d == D_BLOCK || d == D_LIST_ITEM || d == D_FLEX || d == D_GRID || d == D_TABLE || d == D_TABLE_CAPTION || d == D_TABLE_GROUP;
        if (n->st && (n->st->display == D_FLEX || n->st->display == D_INLINE_FLEX || n->st->display == D_GRID)) blockish = 1;   /* hijos de flex/grid: bloques */
        int rq = t == WT_P ? 2 : (blockish || d == D_TABLE_ROW) ? 1 : 0;
        if (rq) { x->ws_pending = 0; itx_req(x, rq); }
        if (c->tag == WT_INPUT || c->tag == WT_TEXTAREA || c->tag == WT_SELECT || c->tag == WT_IMG) { if (c->tag != WT_IMG && c->tag != WT_INPUT) {} }
        else if (c->tag != WT_TEXTAREA) itx_walk(x, c, depth + 1);
        if (rq) { x->ws_pending = 0; itx_req(x, rq); }
        if (d == D_TABLE_CELL) { struct wnode *nx = c->next; while (nx && nx->type != WN_ELEM) nx = nx->next; if (nx) { x->ws_pending = 0; if (x->req) itx_flush(x); wb_addc(x->b, '\t'); } }
    }
}
static jv inner_text(struct jrt *rt, struct wnode *n)
{
    if (n->type != WN_ELEM) return text_of(rt, n);
    char tmp[16]; if (HOST(rt) && HOST(rt)->computed) HOST(rt)->computed(HOST(rt)->ud, n, "display", tmp, sizeof tmp);   /* estilo al dia */
    if (!n->st || n->st->display == D_NONE || !(n->flags & WNF_INDOC) || n->tag == WT_SCRIPT || n->tag == WT_STYLE) return text_of(rt, n);
    for (struct wnode *a = n->parent; a; a = a->parent) if (a->type == WN_ELEM && a->st && a->st->display == D_NONE) return text_of(rt, n);
    struct wb b = {0}; struct itx x = { &b, 0, 0 };
    itx_walk(&x, n, 0);
    while (b.len && (b.p[b.len - 1] == ' ')) b.len--;   /* espacios finales colapsables */
    jv r = Sn(rt, b.p ? b.p : "", b.len); wb_free(&b); return r;
}
static void set_text(struct jrt *rt, struct wnode *n, jstr *s)
{
    if (n->type == WN_TEXT || n->type == WN_COMMENT) { n->name = dupn(rt, s->d, s->len); n->tlen = s->len; }
    else { while (n->first) wd_remove(n->first); if (s->len) wd_append(n, wd_new_text(DOC(rt), dupn(rt, s->d, s->len), s->len)); }
    touch(rt);
}
static void set_inner_html(struct jrt *rt, struct wnode *n, jstr *s)
{
    while (n->first) wd_remove(n->first);
    if (s->len) {
        wd_parse_fragment(DOC(rt), n, s->d, s->len);
        for (struct wnode *k = n->first; k; k = k->next) mark_fragment_scripts_inert(k);
    }
    touch(rt);
}
static jv doc_collect(struct jrt *rt, int kind)
{
    int cap; struct wnode **buf = nbuf(rt, &cap); int c = 0; struct wnode *r = DOC(rt)->root;
    switch (kind) {
    case 0: collect_tag(r, "form", 0, buf, &c, cap); break;
    case 1: collect_tag(r, "img", 0, buf, &c, cap); break;
    case 2: { int n = 0; collect_tag(r, "a", 0, buf, &n, cap); for (int i = 0; i < n; i++) if (wd_attr(buf[i], "href")) buf[c++] = buf[i]; break; }
    case 3: collect_tag(r, "script", 0, buf, &c, cap); break;
    case 4: collect_tag(r, "", 1, buf, &c, cap); break;
    case 5: collect_tag(r, "embed", 0, buf, &c, cap); break;
    }
    return nodelist_free(rt, buf, c);
}
/* SOMA_188: elemento del espacio de nombres SVG (<svg> o dentro, salvo <foreignObject>) */
static int svg_ns(const struct wnode *n) { for (const struct wnode *a = n; a && a->type == WN_ELEM; a = a->parent) { if (a->tag == WT_SVG) return 1; if (a->name && wu_streq(a->name, "foreignobject")) return 0; } return 0; }
static jv nodelist_free(struct jrt *rt, struct wnode **ns, int n) { jv r = nodelist(rt, ns, n); kfree(ns); return r; }
static jv node_prop(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    if (magic >= ATTR_BASE && !node_or_null(thisv) && thisv.t == JT_OBJ) {
        /* SOMA 188B: los atributos reflejados viven todos en HTMLElement.prototype (en un navegador, cada uno en su interfaz: async solo en
         * HTMLScriptElement). Un prototipo que hereda de HTMLElement y define un metodo con ese nombre (Polymer: proto.async = function) no
         * debe tropezar con el setter: se crea la propiedad propia, como si el accesor no existiera para ese objeto. */
        if (argc > 0) { js_def(rt, thisv.u.o, js_intern_c(rt, ATTRS[magic - ATTR_BASE].prop), ARG(0), 0); return jundef(); }
        return jundef();
    }
    struct wnode *n = tonode(rt, thisv); int set = argc > 0; jv a0 = ARG(0); struct dom *dm = DOM(rt);
    if (magic >= ATTR_BASE) return attr_prop(rt, n, &ATTRS[magic - ATTR_BASE], argc, args);
    if (magic >= P_LINK0 && magic < P_LINK0 + 8) { if (set) { wd_set_attr(DOC(rt), n, "href", dupn(rt, js_tostr(rt, a0)->d, js_tostr(rt, a0)->len)); touch(rt); return jundef(); } return link_part(rt, n, magic - P_LINK0); }
    switch (magic) {
    case P_NODETYPE: return jint(n == DOC(rt)->root ? 9 : is_frag(n) ? 11 : n->type);
    case P_NODENAME: if (n->type == WN_ELEM && !is_frag(n) && svg_ns(n)) return S(rt, n->name);
        if (n->type == WN_ELEM && !is_frag(n)) { char u[64]; uint32_t i = 0; for (; n->name[i] && i < 62; i++) u[i] = (char)((n->name[i] >= 'a' && n->name[i] <= 'z') ? n->name[i] - 32 : n->name[i]); u[i] = 0; return S(rt, u); }
        return S(rt, n->type == WN_TEXT ? "#text" : n->type == WN_COMMENT ? "#comment" : n->type == WN_DOC ? "#document" : "#document-fragment");
    case P_NS: if (n->type != WN_ELEM || is_frag(n)) return jnull(); return S(rt, svg_ns(n) ? "http://www.w3.org/2000/svg" : "http://www.w3.org/1999/xhtml");   /* SOMA_188 */
    case P_TAGNAME: if (svg_ns(n)) return S(rt, n->name); { char u[64]; uint32_t i = 0; for (; n->name[i] && i < 62; i++) u[i] = (char)((n->name[i] >= 'a' && n->name[i] <= 'z') ? n->name[i] - 32 : n->name[i]); u[i] = 0; return S(rt, u); }
    case P_LOCALNAME: return S(rt, n->name);
    case P_NODEVALUE: if (set) { if (n->type == WN_TEXT || n->type == WN_COMMENT) {
            const char *old = n->name; set_text(rt, n, js_tostr(rt, a0));
            mo148_emit(rt, n, "characterData", 0, old);
        } return jundef(); } return (n->type == WN_TEXT || n->type == WN_COMMENT) ? text_of(rt, n) : jnull();
    case P_PARENTNODE: return wd_is_shadow_root(n) ? jnull() : W(rt, n->parent);   /* SOMA_188: la raiz sombra no tiene padre */
    case P_PARENTELEMENT: return (n->parent && n->parent->type == WN_ELEM && !wd_is_shadow_root(n) && !wd_is_shadow_root(n->parent)) ? W(rt, n->parent) : jnull();
    case P_CHILDNODES: { int cap; struct wnode **buf = nbuf(rt, &cap); int c = 0; for (struct wnode *k = n->first; k && c < cap; k = k->next) buf[c++] = k; return nodelist_free(rt, buf, c); }
    case P_CHILDREN: { int cap; struct wnode **buf = nbuf(rt, &cap); int c = 0; for (struct wnode *k = n->first; k && c < cap; k = k->next) if (k->type == WN_ELEM) buf[c++] = k; return nodelist_free(rt, buf, c); }
    case P_FIRSTCHILD: return W(rt, n->first);
    case P_LASTCHILD: return W(rt, n->last);
    case P_NEXTSIB: return W(rt, n->next);
    case P_PREVSIB: return W(rt, n->prev);
    case P_FIRSTELEM: return W(rt, first_elem(n));
    case P_LASTELEM: return W(rt, last_elem(n));
    case P_NEXTELEM: return W(rt, next_elem(n));
    case P_PREVELEM: return W(rt, prev_elem(n));
    case P_CHILDCOUNT: { int c = 0; for (struct wnode *k = n->first; k; k = k->next) if (k->type == WN_ELEM) c++; return jint(c); }
    case P_OWNERDOC: return n->type == WN_DOC ? jnull() : W(rt, DOC(rt)->root);
    case P_ISCONNECTED: return jbool((n->flags & WNF_INDOC) != 0);
    case P_BASEURI: return S(rt, DOC(rt)->base[0] ? DOC(rt)->base : dm->url);
    case P_TEXTCONTENT: case P_INNERTEXT: if (set) { set_text(rt, n, js_tostr(rt, a0)); return jundef(); } if (n->type == WN_DOC) return jnull();
        if (magic == P_INNERTEXT && !web_feat_off("innertext")) return inner_text(rt, n);   /* SOMA_188 (soma.innertext=off: como antes) */
        return text_of(rt, n);
    case P_ID: if (set) { jstr *s = js_tostr(rt, a0); const char *old = wd_attr(n, "id");
        wd_set_attr(DOC(rt), n, "id", dupn(rt, s->d, s->len));
        mo148_emit(rt, n, "attributes", "id", old); touch(rt); return jundef(); } { const char *v = wd_attr(n, "id"); return S(rt, v ? v : ""); }
    case P_CLASSNAME: if (set) { jstr *s = js_tostr(rt, a0); const char *old = wd_attr(n, "class");
        wd_set_attr(DOC(rt), n, "class", dupn(rt, s->d, s->len));
        mo148_emit(rt, n, "attributes", "class", old); touch(rt); return jundef(); } return class_get(rt, n);
    case P_INNERHTML:
        if (set) { set_inner_html(rt, n, js_tostr(rt, a0)); return jundef(); }
        { struct wb b = {0}; wd_outer_html(n, &b, 1); jv r = Sn(rt, b.p ? b.p : "", b.len); wb_free(&b); return r; }
    case P_OUTERHTML:
        if (set) {
            struct wnode *p = n->parent; if (!p) return jundef(); jstr *s = js_tostr(rt, a0);
            struct wnode *tmp = wd_new_elem(DOC(rt), "div", 3);
            if (!tmp) js_throw_err(rt, E_RANGE, "memoria agotada");
            wd_parse_fragment(DOC(rt), tmp, s->d, s->len); mark_fragment_scripts_inert(tmp);
            while (tmp->first) { struct wnode *k = tmp->first; wd_remove(k); wd_insert_before(p, k, n); }
            wd_remove(n); touch(rt); return jundef();
        }
        { struct wb b = {0}; wd_outer_html(n, &b, 0); jv r = Sn(rt, b.p ? b.p : "", b.len); wb_free(&b); return r; }
    case P_CLASSLIST: { jobj *w = n->js; jv c = js_get(rt, jobjv(w), Kc(rt, "@@cl")); if (c.t == JT_OBJ) return c; c = hostobj(rt, dm->p_classlist, HK_CLASSLIST, n, 0); js_def(rt, w, Kc(rt, "@@cl"), c, 0); return c; }
    case P_STYLE: {
        jobj *w = n->js;
        if (set) { jstr *s = js_tostr(rt, a0); const char *old = wd_attr(n, "style");
            wd_set_attr(DOC(rt), n, "style", dupn(rt, s->d, s->len));
            mo148_emit(rt, n, "attributes", "style", old); touch(rt); return jundef(); }
        jv c = js_get(rt, jobjv(w), Kc(rt, "@@st")); if (c.t == JT_OBJ) return c; c = hostobj(rt, dm->p_style, HK_STYLE, n, 1); js_def(rt, w, Kc(rt, "@@st"), c, 0); return c;
    }
    case P_DATASET: { jobj *w = n->js; jv c = js_get(rt, jobjv(w), Kc(rt, "@@ds")); if (c.t == JT_OBJ) return c; c = hostobj(rt, dm->p_dataset, HK_DATASET, n, 1); js_def(rt, w, Kc(rt, "@@ds"), c, 0); return c; }
    case P_ATTRIBUTES: {
        jobj *a = js_arr_new(rt, 8); a->proto = dm->p_nodelist;
        for (struct wattr *t = n->attrs; t; t = t->next) { jobj *o = js_obj_new(rt, dm->p_node ? rt->p_object : rt->p_object); js_set_c(rt, o, "name", S(rt, t->name)); js_set_c(rt, o, "nodeName", S(rt, t->name)); js_set_c(rt, o, "value", S(rt, t->val)); js_set_c(rt, o, "nodeValue", S(rt, t->val)); js_set_c(rt, o, "specified", jbool(1)); js_arr_push(rt, a, jobjv(o)); }
        return jobjv(a);
    }
    case P_OFFSETW: case P_OFFSETH: case P_OFFSETT: case P_OFFSETL: case P_CLIENTW: case P_CLIENTH: case P_SCROLLW: case P_SCROLLH: {
        int x, y, w, h; node_rect(rt, n, &x, &y, &w, &h);
        if (n == DOC(rt)->body || n == DOC(rt)->html) { int vw, vh, sx, sy; view_info(rt, &vw, &vh, &sx, &sy); if (magic == P_CLIENTW) return jint(vw); if (magic == P_CLIENTH) return jint(vh); }
        switch (magic) { case P_OFFSETW: case P_CLIENTW: case P_SCROLLW: return jint(w); case P_OFFSETH: case P_CLIENTH: case P_SCROLLH: return jint(h); case P_OFFSETT: return jint(y); default: return jint(x); }
    }
    case P_CLIENTT: case P_CLIENTL: return jint(0);
    case P_OFFSETPARENT: return (n->parent && n->parent->type == WN_ELEM) ? W(rt, DOC(rt)->body) : jnull();
    case P_SCROLLT: case P_SCROLLL: if (set && magic == P_SCROLLT && n != DOC(rt)->body && n != DOC(rt)->html) { int64_t v = f64_to_i64(js_tonum(rt, a0)); n->scy = v < 0 ? 0 : v > 100000000 ? 100000000 : (int32_t)v; return jundef(); }
        if (!set && magic == P_SCROLLT && n != DOC(rt)->body && n != DOC(rt)->html) return jint(n->scy);   /* SOMA_181: scrollTop de cajas con overflow */
        if (set) { if (n == DOC(rt)->body || n == DOC(rt)->html) { int vw, vh, sx, sy; view_info(rt, &vw, &vh, &sx, &sy); if (HOST(rt)->scroll_to) HOST(rt)->scroll_to(HOST(rt)->ud, magic == P_SCROLLL ? (int)f64_to_i64(js_tonum(rt, a0)) : sx, magic == P_SCROLLT ? (int)f64_to_i64(js_tonum(rt, a0)) : sy); } return jundef(); }
        { int vw, vh, sx, sy; view_info(rt, &vw, &vh, &sx, &sy); if (n == DOC(rt)->body || n == DOC(rt)->html) return jint(magic == P_SCROLLT ? sy : sx); return jint(0); }
    case P_TABINDEX: if (set) { const char *old = wd_attr(n, "tabindex");
        jstr *s = js_tostr(rt, a0); wd_set_attr(DOC(rt), n, "tabindex", dupn(rt, s->d, s->len));
        mo148_emit(rt, n, "attributes", "tabindex", old); return jundef(); } { const char *v = wd_attr(n, "tabindex"); return jint(v ? wu_atoi(v) : ((n->tag == WT_A || n->tag == WT_INPUT || n->tag == WT_BUTTON || n->tag == WT_SELECT || n->tag == WT_TEXTAREA) ? 0 : -1)); }
    case P_HIDDEN: if (set) { const char *old = wd_attr(n, "hidden");
        if (js_truthy(a0)) wd_set_attr(DOC(rt), n, "hidden", ""); else wd_del_attr(n, "hidden");
        if (old || wd_attr(n, "hidden")) mo148_emit(rt, n, "attributes", "hidden", old);
        touch(rt); return jundef(); } return jbool(wd_attr(n, "hidden") != 0);
    case P_CONTENTEDITABLE: if (set) { const char *old = wd_attr(n, "contenteditable");
        jstr *s = js_tostr(rt, a0); wd_set_attr(DOC(rt), n, "contenteditable", dupn(rt, s->d, s->len));
        mo148_emit(rt, n, "attributes", "contenteditable", old); return jundef(); } { const char *v = wd_attr(n, "contenteditable"); return S(rt, v ? (*v ? v : "true") : "inherit"); }
    case P_ISCE: { const char *v = wd_attr(n, "contenteditable"); return jbool(v && (!*v || wu_strieq(v, "true"))); }
    case P_SHADOWROOT: { struct wnode *sr = n->shadow; const char *md = sr ? wd_attr(sr, "mode") : 0; return sr && !(md && wu_streq(md, "closed")) ? W(rt, sr) : jnull(); }   /* SOMA_188 */
    case P_VALUE: {
        if (n->tag == WT_LI || n->tag == WT_PROGRESS || n->tag == WT_METER) { const char *v = wd_attr(n, "value"); return jint(v ? wu_atoi(v) : 0); }
        if (set) { if (n->tag == WT_INPUT) { const char *t = wd_attr(n, "type"); if (t && wu_strieq(t, "file")) return jundef(); } set_ctl_value(rt, n, js_tostr(rt, a0)); return jundef(); }
        jv tmp = jundef(); const char *v = ctl_value(rt, n, &tmp); return S(rt, v);
    }
    case P_DEFVALUE: { const char *v = wd_attr(n, "value"); if (n->tag == WT_TEXTAREA) return text_of(rt, n); return S(rt, v ? v : ""); }
    case P_CHECKED: if (set) { set_checked(rt, n, js_truthy(a0)); return jundef(); } if (n->flags & WNF_CHECKED) return jbool(1); return jbool(!(n->flags & WNF_EDITED) && wd_attr(n, "checked") != 0 && 0);
    case P_DEFCHECKED: return jbool(wd_attr(n, "checked") != 0);
    case P_INDETERMINATE: if (set) { n->sel = js_truthy(a0); return jundef(); } return jbool(n->tag == WT_INPUT && n->sel == 1);
    case P_SELECTED: {
        if (n->tag != WT_OPTION) return jbool(0);
        struct wnode *sel = n->parent; while (sel && sel->tag != WT_SELECT) sel = sel->parent;
        struct wnode *o[256]; int c = 0, idx = -1; if (sel) { opt_collect(sel, o, &c, 256); for (int i = 0; i < c; i++) if (o[i] == n) idx = i; }
        if (set) { if (sel && idx >= 0 && js_truthy(a0)) { sel->sel = idx; sel->flags |= WNF_EDITED; } else if (!sel) { if (js_truthy(a0)) wd_set_attr(DOC(rt), n, "selected", ""); else wd_del_attr(n, "selected"); } touch(rt); return jundef(); }
        return jbool(sel ? sel_index(sel) == idx : wd_attr(n, "selected") != 0);
    }
    case P_SELINDEX: if (set) { n->sel = (int32_t)f64_to_i64(js_tonum(rt, a0)); n->flags |= WNF_EDITED; touch(rt); return jundef(); } return jint(n->tag == WT_SELECT ? sel_index(n) : -1);
    case P_INDEX: { struct wnode *sel = n->parent; while (sel && sel->tag != WT_SELECT) sel = sel->parent; if (!sel) return jint(0); struct wnode *o[256]; int c = 0; opt_collect(sel, o, &c, 256); for (int i = 0; i < c; i++) if (o[i] == n) return jint(i); return jint(0); }
    case P_OPTIONS: case P_SELECTEDOPTIONS: { struct wnode *o[256]; int c = 0; opt_collect(n, o, &c, 256); if (magic == P_SELECTEDOPTIONS) { int si = sel_index(n); if (si >= 0 && si < c) { o[0] = o[si]; c = 1; } else c = 0; } return nodelist(rt, o, c); }
    case P_FORM: return W(rt, form_of(n));
    case P_ELEMENTS: { struct wnode *buf[512]; int c = 0; collect_controls(n, buf, &c, 512); return nodelist(rt, buf, c); }
    case P_LENGTH:
        if (n->type == WN_TEXT || n->type == WN_COMMENT) return jint(js_u16len_n(n->name, n->tlen));      /* SOMA_185: unidades UTF-16 */
        if (n->tag == WT_SELECT) { struct wnode *o[256]; int c = 0; opt_collect(n, o, &c, 256); return jint(c); }
        { struct wnode *buf[512]; int c = 0; collect_controls(n, buf, &c, 512); return jint(c); }
    case P_FILES: return jobjv(js_arr_new(rt, 0));
    case P_SELSTART: case P_SELEND: { jv tmp = jundef(); const char *v = ctl_value(rt, n, &tmp); int len = (int)wu_strlen(v); if (set) { n->sel = (int32_t)f64_to_i64(js_tonum(rt, a0)); return jundef(); } return jint(n->sel > 0 && n->sel <= len ? n->sel : (magic == P_SELEND ? len : len)); }
    case P_VALIDITY: { jobj *o = js_obj_new(rt, rt->p_object); js_set_c(rt, o, "valid", jbool(1)); js_set_c(rt, o, "valueMissing", jbool(0)); return jobjv(o); }
    case P_WILLVALIDATE: return jbool(1);
    case P_TEXT:
        if (n->type == WN_ELEM && n->tag != WT_A && n->tag != WT_SCRIPT && n->tag != WT_OPTION && n->tag != WT_TITLE && !web_feat_off("webcomp")) {
            /* SOMA_188: 'text' solo existe en <a>, <script>, <option> y <title>; en otros es una propiedad normal
               (lit-html asigna .text a componentes antes de mejorarlos: escribia el texto en el DOM visible) */
            if (set) { js_def(rt, thisv.u.o, js_intern_c(rt, "text"), a0, JP_ENUM); return jundef(); }
            return jundef();
        }
        if (set) { set_text(rt, n, js_tostr(rt, a0)); return jundef(); } return text_of(rt, n);
    case P_TYPE: {
        if (set) { jstr *s = js_tostr(rt, a0); const char *old = wd_attr(n, "type");
            wd_set_attr(DOC(rt), n, "type", dupn(rt, s->d, s->len));
            mo148_emit(rt, n, "attributes", "type", old); touch(rt); return jundef(); }
        const char *v = wd_attr(n, "type"); char low[40]; if (v) lower_copy(v, wu_strlen(v), low, sizeof low);
        if (n->tag == WT_INPUT) return S(rt, v && *v ? low : "text");
        if (n->tag == WT_BUTTON) return S(rt, v && *v ? low : "submit");
        if (n->tag == WT_SELECT) return S(rt, wd_attr(n, "multiple") ? "select-multiple" : "select-one");
        if (n->tag == WT_TEXTAREA) return S(rt, "textarea");
        return S(rt, v ? v : "");
    }
    case P_LABELS: return jobjv(js_arr_new(rt, 0));
    case P_NATW: case P_NATH: case P_COMPLETE: case P_CURRENTSRC: {
        if (magic == P_CURRENTSRC) return abs_url(rt, wd_attr(n, "src"));
        if (magic == P_COMPLETE) return jbool(1);
        int x, y, w, h; node_rect(rt, n, &x, &y, &w, &h); return jint(magic == P_NATW ? w : h);
    }
    case P_CONTENTWINDOW: case P_CONTENTDOC: return jnull();
    default: break;
    }
    /* documento */
    struct wdoc *d = DOC(rt);
    switch (magic) {
    case P_DOCELEM: return W(rt, d->html);
    case P_HEAD: return W(rt, d->head);
    case P_BODY: if (set) { struct wnode *b = node_or_null(a0); if (b && d->html) { if (d->body && d->body != b) { d->body->parent ? (void)wd_remove(d->body) : (void)0; } wd_append(d->html, b); d->body = b; touch(rt); } return jundef(); } return W(rt, d->body);
    case P_SCROLLINGEL: return W(rt, d->html);
    case P_TITLE: {
        struct wnode *t = 0; struct wnode *buf[32]; int c = 0; collect_tag(d->root, "title", 0, buf, &c, 32);
        for (int i = 0; i < c && !t; i++) { int sv = 0; for (struct wnode *a = buf[i]->parent; a; a = a->parent) if (a->tag == WT_SVG) { sv = 1; break; } if (!sv) t = buf[i]; }   /* SOMA_188 */
        if (set) {
            jstr *s = js_tostr(rt, a0);
            if (!t && d->head) { t = wd_new_elem(d, "title", 5); wd_append(d->head, t); }
            if (t) set_text(rt, t, s);
            d->title = dupn(rt, s->d, s->len); touch(rt); return jundef();
        }
        if (!t) return S(rt, d->title ? d->title : "");
        { struct wb b = {0}; wd_text_content(t, &b); uint32_t i = 0, o = 0; char *p = b.p ? b.p : ""; jstr *tmp = js_str_new(rt, p, b.len); char *w = tmp->d; int sp = 1;
          for (; i < tmp->len; i++) { if (wu_isspace(w[i])) { if (!sp) w[o++] = ' '; sp = 1; } else { w[o++] = w[i]; sp = 0; } } if (o && w[o - 1] == ' ') o--; wb_free(&b); return Sn(rt, w, o); }
    }
    case P_URL: return S(rt, dm->url);
    case P_DOMAIN: { struct wn_url u; if (wn_url_parse(dm->url, &u) == 0) return S(rt, u.host); return S(rt, ""); }
    case P_REFERRER: return S(rt, "");
    case P_COOKIE:
        if (set) { if (HOST(rt)->set_cookie) HOST(rt)->set_cookie(HOST(rt)->ud, js_tostr(rt, a0)->d); return jundef(); }
        { char buf[4096]; buf[0] = 0; const char *c = HOST(rt)->get_cookie ? HOST(rt)->get_cookie(HOST(rt)->ud, buf, sizeof buf) : 0; return S(rt, c ? c : buf); }
    case P_READYSTATE: { jv v = js_get(rt, jobjv(n->js), Kc(rt, "@@rs")); return IS_UNDEF(v) ? S(rt, "loading") : v; }
    case P_CHARSET: return S(rt, "UTF-8");
    case P_CONTENTTYPE: return S(rt, "text/html");
    case P_COMPAT: return S(rt, "CSS1Compat");
    case P_DEFVIEW: return jobjv(rt->global);
    case P_ACTIVEEL: return d->focus ? W(rt, d->focus) : W(rt, d->body);
    case P_FORMS: return doc_collect(rt, 0);
    case P_IMAGES: return doc_collect(rt, 1);
    case P_LINKS: return doc_collect(rt, 2);
    case P_SCRIPTS: return doc_collect(rt, 3);
    case P_ALL: return doc_collect(rt, 4);
    case P_EMBEDS: return doc_collect(rt, 5);
    case P_STYLESHEETS: return jobjv(js_arr_new(rt, 0));
    case P_FONTS: { jv f = js_get(rt, jobjv(n->js), Kc(rt, "@@fonts")); if (f.t == JT_OBJ) return f; return jundef(); }
    case P_DOCHIDDEN: return jbool(0);
    case P_VISIBILITY: return S(rt, "visible");
    case P_LASTMOD: return S(rt, "01/01/2026 00:00:00");
    case P_DOCTYPE: return jnull();
    case P_CURRENTSCRIPT: return DOM(rt)->cur_script ? W(rt, DOM(rt)->cur_script) : jnull();
    default: return jundef();
    }
}

/* ------------------------------------------------------------ metodos de nodo */
enum {
    M_APPEND = 1, M_INSBEFORE, M_REMOVECHILD, M_REPLACECHILD, M_CLONE, M_CONTAINS, M_HASCHILD, M_GETROOT, M_ISSAME,
    M_GETATTR, M_SETATTR, M_REMOVEATTR, M_HASATTR, M_HASATTRS, M_ATTRNAMES, M_QS, M_QSA, M_MATCHES, M_CLOSEST,
    M_BYTAG, M_BYCLASS, M_BYNAME, M_RESET, M_SUBMIT, M_SELECT, M_RECT, M_BYID, M_CREATEELEM, M_CREATETEXT, M_CREATECOMMENT,
    M_CREATEFRAG, M_IMPORT, M_HASFOCUS, M_WRITE, M_NOP, M_FALSE, M_ELEMFROMPOINT, M_TOSTRING_ELEM, M_NAMEDITEM, M_ITEM,
    M_STYLE_GET, M_STYLE_SET, M_STYLE_REMOVE, M_CL_ADD, M_CL_REMOVE, M_CL_TOGGLE, M_CL_CONTAINS, M_CL_REPLACE, M_CL_ITEM, M_CL_STR, M_CL_FOREACH, M_CL_ITER,
    M_ST_GETITEM, M_ST_SETITEM, M_ST_REMOVEITEM, M_ST_CLEAR, M_ST_KEY, M_CSSTEXT, M_CL_LENGTH, M_ST_LENGTH, M_STYLE_LENGTH, M_STYLE_ITEM, M_CL_VALUE
};
static jv make_rect(struct jrt *rt, int x, int y, int w, int h)
{
    jobj *o = js_obj_new(rt, rt->p_object);
    js_set_c(rt, o, "x", jint(x)); js_set_c(rt, o, "y", jint(y)); js_set_c(rt, o, "left", jint(x)); js_set_c(rt, o, "top", jint(y));
    js_set_c(rt, o, "right", jint(x + w)); js_set_c(rt, o, "bottom", jint(y + h)); js_set_c(rt, o, "width", jint(w)); js_set_c(rt, o, "height", jint(h));
    return jobjv(o);
}
static void node_changed_attr(struct jrt *rt, struct wnode *n, const char *name, int on)
{
    if (wu_streq(name, "checked") && !(n->flags & WNF_EDITED)) { if (on) n->flags |= WNF_CHECKED; else n->flags &= (uint16_t)~WNF_CHECKED; }
    else if (wu_streq(name, "disabled")) { if (on) n->flags |= WNF_DISABLED; else n->flags &= (uint16_t)~WNF_DISABLED; }
    else if (wu_streq(name, "value") && !(n->flags & WNF_EDITED)) n->value = 0;
    touch(rt);
}
static void clear_js(struct wnode *n) { n->js = 0; for (struct wnode *c = n->first; c; c = c->next) clear_js(c); }
static jv node_method(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    struct wnode *n = (magic >= M_STYLE_GET || magic == M_NAMEDITEM || magic == M_ITEM) ? 0 : tonode(rt, thisv);
    char low[128];
    switch (magic) {
    case M_APPEND: insert_node(rt, n, tonode(rt, ARG(0)), 0); return ARG(0);
    case M_INSBEFORE: insert_node(rt, n, tonode(rt, ARG(0)), IS_NULLISH(ARG(1)) ? 0 : tonode(rt, ARG(1))); return ARG(0);
    case M_REMOVECHILD: { struct wnode *c = tonode(rt, ARG(0)); if (c->parent != n) js_throw_err(rt, E_ERROR, "NotFoundError: el nodo no es hijo de este nodo"); wd_remove(c); touch(rt); return ARG(0); }
    case M_REPLACECHILD: { struct wnode *nc = tonode(rt, ARG(0)), *old = tonode(rt, ARG(1)); if (old->parent != n) js_throw_err(rt, E_ERROR, "NotFoundError: el nodo no es hijo de este nodo"); if (nc == old) return ARG(1); insert_node(rt, n, nc, old); if (old->parent == n) wd_remove(old); touch(rt); return ARG(1); }
    case M_CLONE: case M_IMPORT: { struct wnode *src = magic == M_CLONE ? n : tonode(rt, ARG(0)); struct wnode *c = wd_clone(DOC(rt), src, magic == M_CLONE ? js_truthy(ARG(0)) : js_truthy(ARG(1))); if (!c) return jnull(); clear_js(c); return W(rt, c); }
    case M_CONTAINS: { struct wnode *o = node_or_null(ARG(0)); return jbool(o && (o == n || wd_is_ancestor(n, o))); }
    case M_HASCHILD: return jbool(n->first != 0);
    case M_GETROOT: { struct wnode *r = n; while (r->parent) r = r->parent; return W(rt, r); }
    case M_ISSAME: return jbool(node_or_null(ARG(0)) == n);
    case M_GETATTR: { jstr *k = js_tostr(rt, ARG(0)); lower_copy(k->d, k->len, low, sizeof low); const char *v = wd_attr(n, low); return v ? S(rt, v) : jnull(); }
    case M_SETATTR: { jstr *k = js_tostr(rt, ARG(0)), *v = js_tostr(rt, ARG(1)); lower_copy(k->d, k->len, low, sizeof low); wd_set_attr(DOC(rt), n, low, dupn(rt, v->d, v->len)); node_changed_attr(rt, n, low, 1); return jundef(); }
    case M_REMOVEATTR: { jstr *k = js_tostr(rt, ARG(0)); lower_copy(k->d, k->len, low, sizeof low); wd_del_attr(n, low); node_changed_attr(rt, n, low, 0); return jundef(); }
    case M_HASATTR: { jstr *k = js_tostr(rt, ARG(0)); lower_copy(k->d, k->len, low, sizeof low); return jbool(wd_attr(n, low) != 0); }
    case M_HASATTRS: return jbool(n->attrs != 0);
    case M_ATTRNAMES: { jobj *a = js_arr_new(rt, 8); for (struct wattr *t = n->attrs; t; t = t->next) js_arr_push(rt, a, S(rt, t->name)); return jobjv(a); }
    case M_QS: case M_QSA: {
        jstr *s = js_tostr(rt, ARG(0));
        if (magic == M_QS) { struct wnode *one[1]; int c1 = wc_query(css(rt), n, s->d, one, 1, 1); return c1 ? W(rt, one[0]) : jnull(); }
        int cap; struct wnode **buf = nbuf(rt, &cap); int c = wc_query(css(rt), n, s->d, buf, cap, 0);
        return nodelist_free(rt, buf, c);
    }
    case M_MATCHES: return jbool(is_elem(n) && wc_matches(css(rt), n, js_tostr(rt, ARG(0))->d));
    case M_CLOSEST: { jstr *s = js_tostr(rt, ARG(0)); for (struct wnode *p = n; p && p->type == WN_ELEM; p = p->parent) if (is_elem(p) && wc_matches(css(rt), p, s->d)) return W(rt, p); return jnull(); }
    case M_BYTAG: { jstr *s = js_tostr(rt, ARG(0)); int cap; struct wnode **buf = nbuf(rt, &cap); int c = 0; lower_copy(s->d, s->len, low, sizeof low); collect_tag(n, low, wu_streq(low, "*"), buf, &c, cap); return nodelist_free(rt, buf, c); }
    case M_BYCLASS: { jstr *cs = js_tostr(rt, ARG(0)); int cap; struct wnode **buf = nbuf(rt, &cap); int c = 0; collect_class(n, cs->d, buf, &c, cap); return nodelist_free(rt, buf, c); }
    case M_BYNAME: { jstr *ns = js_tostr(rt, ARG(0)); int cap; struct wnode **buf = nbuf(rt, &cap); int c = 0; collect_name(n, ns->d, buf, &c, cap); return nodelist_free(rt, buf, c); }
    case M_RESET: reset_form(n); touch(rt); return jundef();
    case M_SUBMIT: if (HOST(rt)->submit) HOST(rt)->submit(HOST(rt)->ud, n); return jundef();
    case M_SELECT: n->sel = 0; return jundef();
    case M_RECT: { int x, y, w, h, vw, vh, sx, sy; node_rect(rt, n, &x, &y, &w, &h); view_info(rt, &vw, &vh, &sx, &sy); return make_rect(rt, x - sx, y - sy, w, h); }
    case M_BYID: { jstr *s = js_tostr(rt, ARG(0)); struct wnode *r = wd_by_id(DOC(rt)->root, s->d); return W(rt, r); }
    case M_CREATEELEM: { jstr *s = js_tostr(rt, ARG(0)); lower_copy(s->d, s->len, low, sizeof low); struct wnode *e = wd_new_elem(DOC(rt), low, (uint32_t)wu_strlen(low)); return W(rt, e); }
    case M_CREATETEXT: case M_CREATECOMMENT: { jstr *s = js_tostr(rt, ARG(0)); struct wnode *t = wd_new_text(DOC(rt), dupn(rt, s->d, s->len), s->len); if (magic == M_CREATECOMMENT) t->type = WN_COMMENT; return W(rt, t); }
    case M_CREATEFRAG: return W(rt, wd_new_elem(DOC(rt), "#document-fragment", 18));
    case M_HASFOCUS: return jbool(1);
    case M_WRITE: { jstr *s = js_tostr(rt, ARG(0)); if (DOC(rt)->body) { wd_parse_fragment(DOC(rt), DOC(rt)->body, s->d, s->len); touch(rt); } return jundef(); }
    case M_NOP: return jundef();
    case M_FALSE: return jbool(0);
    case M_ELEMFROMPOINT: return W(rt, DOC(rt)->body);
    case M_NAMEDITEM: { jobj *a = thisv.u.o; jstr *s = js_tostr(rt, ARG(0)); for (uint32_t i = 0; i < a->alen; i++) { struct wnode *e = node_or_null(a->el[i]); if (e) { const char *id = wd_attr(e, "id"), *nm = wd_attr(e, "name"); if ((id && wu_streq(id, s->d)) || (nm && wu_streq(nm, s->d))) return a->el[i]; } } return jnull(); }
    case M_ITEM: { if (thisv.t != JT_OBJ) return jnull(); jobj *a = thisv.u.o; int64_t i = f64_to_i64(js_tonum(rt, ARG(0))); return (i >= 0 && (uint32_t)i < a->alen) ? a->el[i] : jnull(); }
    default: break;
    }
    /* style */
    if (magic >= M_STYLE_GET && magic <= M_STYLE_REMOVE) {
        if (thisv.t != JT_OBJ || thisv.u.o->u.host.kind == 0) js_throw_err(rt, E_TYPE, "Objeto de estilo no valido");
        struct wnode *sn = (struct wnode *)thisv.u.o->u.host.p; jstr *k = js_tostr(rt, ARG(0)); char prop[120], buf[600];
        if (k->len && k->d[0] == '-' && k->d[1] == '-') wu_copy(prop, k->d, sizeof prop); else hyphenate(k->d, prop, sizeof prop);
        if (magic == M_STYLE_GET) {
            if (thisv.u.o->u.host.kind == HK_CSTYLE) { buf[0] = 0; if (HOST(rt)->computed) HOST(rt)->computed(HOST(rt)->ud, sn, prop, buf, sizeof buf); return S(rt, buf); }
            wc_get_inline(sn, prop, buf, sizeof buf); return S(rt, buf);
        }
        if (thisv.u.o->u.host.kind == HK_CSTYLE) return jundef();
        if (magic == M_STYLE_SET) { jstr *v = js_tostr(rt, ARG(1)); const char *old = wd_attr(sn, "style");
            wc_set_inline(DOC(rt), sn, prop, v->len ? v->d : "");
            if (mo148_diff(old, wd_attr(sn, "style"))) mo148_emit(rt, sn, "attributes", "style", old);
            touch(rt); return jundef(); }
        wc_get_inline(sn, prop, buf, sizeof buf); const char *old = wd_attr(sn, "style");
        wc_set_inline(DOC(rt), sn, prop, "");
        if (mo148_diff(old, wd_attr(sn, "style"))) mo148_emit(rt, sn, "attributes", "style", old);
        touch(rt); return S(rt, buf);
    }
    if (magic == M_CSSTEXT || magic == M_STYLE_LENGTH || magic == M_STYLE_ITEM) {
        struct wnode *sn = (struct wnode *)thisv.u.o->u.host.p; const char *cur = wd_attr(sn, "style");
        if (magic == M_CSSTEXT) { if (argc > 0) { jstr *s = js_tostr(rt, args[0]); const char *old = wd_attr(sn, "style");
            wd_set_attr(DOC(rt), sn, "style", dupn(rt, s->d, s->len));
            mo148_emit(rt, sn, "attributes", "style", old); touch(rt); return jundef(); } return S(rt, cur ? cur : ""); }
        int c = 0; char names[64][64]; const char *p = cur ? cur : "";
        while (*p && c < 64) { while (*p == ';' || wu_isspace(*p)) p++; const char *s = p; while (*p && *p != ':' && *p != ';') p++; uint32_t l = (uint32_t)(p - s); while (l && wu_isspace(s[l - 1])) l--; if (l && *p == ':') { if (l > 63) l = 63; memcpy(names[c], s, l); names[c][l] = 0; c++; } while (*p && *p != ';') p++; }
        if (magic == M_STYLE_LENGTH) return jint(c);
        int64_t i = f64_to_i64(js_tonum(rt, ARG(0))); return (i >= 0 && i < c) ? S(rt, names[i]) : S(rt, "");
    }
    /* classList */
    if (magic >= M_CL_ADD && magic <= M_CL_ITER) {
        struct wnode *cn = (struct wnode *)thisv.u.o->u.host.p; const char *cur = wd_attr(cn, "class");
        switch (magic) {
        case M_CL_ADD: case M_CL_REMOVE: for (int i = 0; i < argc; i++) { jstr *t = js_tostr(rt, args[i]); check_token(rt, t); cl_edit(rt, cn, t->d, t->len, magic == M_CL_ADD); } return jundef();
        case M_CL_TOGGLE: { jstr *t = js_tostr(rt, ARG(0)); check_token(rt, t); int has = cl_has(cur, t->d, t->len), want = argc > 1 && !IS_UNDEF(args[1]) ? js_truthy(args[1]) : !has; if (want != has) cl_edit(rt, cn, t->d, t->len, want); return jbool(want); }
        case M_CL_CONTAINS: { jstr *t = js_tostr(rt, ARG(0)); return jbool(cl_has(cur, t->d, t->len)); }
        case M_CL_REPLACE: { jstr *o = js_tostr(rt, ARG(0)), *nw = js_tostr(rt, ARG(1)); if (!cl_has(cur, o->d, o->len)) return jbool(0); cl_edit(rt, cn, o->d, o->len, 0); cl_edit(rt, cn, nw->d, nw->len, 1); return jbool(1); }
        case M_CL_STR: return S(rt, cur ? cur : "");
        default: break;
        }
        jobj *a = js_arr_new(rt, 4); const char *p = cur ? cur : "";
        while (*p) { while (wu_isspace(*p)) p++; const char *s = p; while (*p && !wu_isspace(*p)) p++; if (p > s) js_arr_push(rt, a, Sn(rt, s, (uint32_t)(p - s))); }
        if (magic == M_CL_ITEM) { int64_t i = f64_to_i64(js_tonum(rt, ARG(0))); return (i >= 0 && (uint32_t)i < a->alen) ? a->el[i] : jnull(); }
        if (magic == M_CL_ITER) return js_call(rt, js_get_c(rt, jobjv(a), "values"), jobjv(a), 0, 0);
        for (uint32_t i = 0; i < a->alen; i++) { jv cb[3] = { a->el[i], jint(i), thisv }; js_call(rt, ARG(0), jundef(), cb, 3); }
        return jundef();
    }
    if (magic == M_CL_LENGTH || magic == M_CL_VALUE) {
        struct wnode *cn = (struct wnode *)thisv.u.o->u.host.p; const char *cur = wd_attr(cn, "class");
        if (magic == M_CL_VALUE) { if (argc > 0) { jstr *s = js_tostr(rt, args[0]); const char *old = wd_attr(cn, "class");
            wd_set_attr(DOC(rt), cn, "class", dupn(rt, s->d, s->len));
            mo148_emit(rt, cn, "attributes", "class", old); touch(rt); return jundef(); } return S(rt, cur ? cur : ""); }
        int c = 0; const char *p = cur ? cur : ""; while (*p) { while (wu_isspace(*p)) p++; const char *s = p; while (*p && !wu_isspace(*p)) p++; if (p > s) c++; } return jint(c);
    }
    /* almacenamiento */
    jobj *so = thisv.t == JT_OBJ ? thisv.u.o : 0; if (!so || so->u.host.kind != HK_STORAGE) js_throw_err(rt, E_TYPE, "Objeto Storage no valido");
    int loc = is_local(so);
    switch (magic) {
    case M_ST_GETITEM: { const char *v = storage_op(rt, loc, js_tostr(rt, ARG(0))->d, 0, 0); return v ? S(rt, v) : jnull(); }
    case M_ST_SETITEM: { jstr *k = js_tostr(rt, ARG(0)), *v = js_tostr(rt, ARG(1)); storage_op(rt, loc, k->d, v->d, 1); return jundef(); }
    case M_ST_REMOVEITEM: storage_op(rt, loc, js_tostr(rt, ARG(0))->d, 0, 2); return jundef();
    case M_ST_CLEAR: storage_op(rt, loc, 0, 0, 3); return jundef();
    case M_ST_KEY: { char ib[16]; wu_itoa(f64_to_i64(js_tonum(rt, ARG(0))), ib); const char *k = storage_op(rt, loc, ib, 0, 4); return k ? S(rt, k) : jnull(); }
    case M_ST_LENGTH: { int c = 0; for (; c < 4096; c++) { char ib[16]; wu_itoa(c, ib); if (!storage_op(rt, loc, ib, 0, 4)) break; } return jint(c); }
    default: return jundef();
    }
}

/* ------------------------------------------------------------ funciones globales internas (__xxx) y ventana */
static jv g_resolve(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    char out[WN_URL_MAX + 64]; jstr *b = js_tostr(rt, ARG(0)), *r = js_tostr(rt, ARG(1));
    if (wn_url_resolve(b->d, r->d, out, sizeof out) != 0) return jstrv(r);
    return S(rt, out);
}
static jv g_url(struct jrt *rt, jv t, jv *args, int argc, int magic) { return S(rt, DOM(rt)->url); }
static jv g_nav(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    jstr *u = js_tostr(rt, ARG(0)); int mode = (int)f64_to_i64(js_tonum(rt, ARG(1)));
    if (HOST(rt)->navigate) HOST(rt)->navigate(HOST(rt)->ud, u->d, mode);
    /* SOMA_192: la URL que ve el script solo cambia si el navegador acepto el pushState/replaceState (el parche 191 rechaza los que
     * salen del origen, pero document.URL y la base de las URL relativas se quedaban con la URL rechazada) */
    if (mode == 2 || mode == 3) wu_copy(DOM(rt)->url, HOST(rt)->url && HOST(rt)->navigate ? HOST(rt)->url : u->d, sizeof DOM(rt)->url);
    return jundef();
}
static jv g_hist(struct jrt *rt, jv t, jv *args, int argc, int magic) { if (HOST(rt)->history) HOST(rt)->history(HOST(rt)->ud, (int)f64_to_i64(js_tonum(rt, ARG(0)))); return jundef(); }
static jv g_ua(struct jrt *rt, jv t, jv *args, int argc, int magic) { return S(rt, HOST(rt)->ua ? HOST(rt)->ua : "SOMA"); }
static jv g_alert(struct jrt *rt, jv t, jv *args, int argc, int magic) { if (HOST(rt)->alert) HOST(rt)->alert(HOST(rt)->ud, js_tostr(rt, ARG(0))->d); return jundef(); }
static jv g_now(struct jrt *rt, jv t, jv *args, int argc, int magic) { return jnum(f64_from_i64((int64_t)web_now_ms())); }
/* SOMA_192: crypto.getRandomValues/randomUUID piden aqui 32 bits al generador del nucleo (antes usaban Math.random). */
static jv g_rnd32(struct jrt *rt, jv t, jv *args, int argc, int magic) { (void)rt; return jnum(f64_from_i64((int64_t)rng_u32())); }
static jv g_scroll(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    if (HOST(rt)->scroll_to) HOST(rt)->scroll_to(HOST(rt)->ud, (int)f64_to_i64(js_tonum(rt, ARG(0))), (int)f64_to_i64(js_tonum(rt, ARG(1))));
    return jundef();
}
static jv g_fetch(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    jstr *m = js_tostr(rt, ARG(0)), *u = js_tostr(rt, ARG(1)), *b = js_tostr(rt, ARG(2)), *ct = js_tostr(rt, ARG(3));
    if (!HOST(rt)->fetch) return jnull();
    int status = 0; char *out = 0; uint32_t olen = 0; char ctype[96]; ctype[0] = 0;
    int rc = HOST(rt)->fetch(HOST(rt)->ud, m->d, u->d, b->len ? b->d : 0, b->len, ct->len ? ct->d : 0, &status, &out, &olen, ctype, sizeof ctype);
    if (rc != 0) { if (out) kfree(out); return jnull(); }
    jobj *o = js_obj_new(rt, rt->p_object);
    js_set_c(rt, o, "status", jint(status)); js_set_c(rt, o, "ctype", S(rt, ctype)); js_set_c(rt, o, "url", jstrv(u));
    if (olen > (16u << 20)) olen = 16u << 20;
    js_set_c(rt, o, "body", Sn(rt, out ? out : "", olen)); if (out) kfree(out);
    return jobjv(o);
}
static jv g_cstyle(struct jrt *rt, jv t, jv *args, int argc, int magic) { struct wnode *n = tonode(rt, ARG(0)); return hostobj(rt, DOM(rt)->p_cstyle, HK_CSTYLE, n, 1); }
static jv g_attrfn(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    struct wnode *n = tonode(rt, ARG(0)); jstr *ty = js_tostr(rt, ARG(1)); char an[80], key[96];
    wu_copy(an, "on", sizeof an); wu_copy(an + 2, ty->d, sizeof an - 2);
    const char *code = wd_attr(n, an); if (!code) return jnull();
    wu_copy(key, "@@", sizeof key); wu_copy(key + 2, an, sizeof key - 2);
    jv c = js_get(rt, jobjv(n->js), Kc(rt, key)); if (c.t == JT_OBJ) return c;
    struct wb b = {0}; wb_add(&b, "(function(event){"); wb_add(&b, code); wb_add(&b, "\n})");
    int ok = 0; jv f = js_eval_string(rt, b.p, b.len, &ok); wb_free(&b);
    if (!ok || !js_is_callable(f)) return jnull();
    js_def(rt, n->js, Kc(rt, key), f, 0); return f;
}
static jv g_parsefrag(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    jstr *h = js_tostr(rt, ARG(0)); struct wnode *f = wd_new_elem(DOC(rt), "#document-fragment", 18);
    wd_parse_fragment(DOC(rt), f, h->d, h->len); mark_fragment_scripts_inert(f); return W(rt, f);
}
static jv g_activate(struct jrt *rt, jv t, jv *args, int argc, int magic) { struct wnode *n = tonode(rt, ARG(0)); if (HOST(rt)->activate) HOST(rt)->activate(HOST(rt)->ud, n); return jundef(); }
/* SOMA_188: shadow DOM. La raiz es un nodo "#shadow-root" (nodeType 11) con parent = anfitrion, fuera de su lista de hijos */
static jv g_attachshadow(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    struct wnode *h = tonode(rt, ARG(0)); jstr *mode = js_tostr(rt, ARG(1));
    if (h->type != WN_ELEM) js_throw_err(rt, E_TYPE, "attachShadow sobre un nodo que no es elemento");
    if (h->shadow) js_throw_err(rt, E_TYPE, "NotSupportedError: el elemento ya tiene una raiz sombra");
    struct wnode *sr = wd_new_elem(DOC(rt), "#shadow-root", 12); if (!sr) js_throw_err(rt, E_RANGE, "memoria agotada");
    sr->parent = h; h->shadow = sr; if (h->flags & WNF_INDOC) sr->flags |= WNF_INDOC;
    wd_set_attr(DOC(rt), sr, "mode", dupn(rt, mode->d, mode->len));
    touch(rt); return W(rt, sr);
}
/* SOMA_188: CSS de adoptedStyleSheets: se guarda en la raiz (sombra o documento) y scan_styles lo aplica (sin <style> visible en el DOM) */
static jv g_adoptcss(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    struct wnode *n = node_or_null(ARG(0)); if (!n) return jundef();
    jstr *css = js_tostr(rt, ARG(1));
    wd_set_attr(DOC(rt), n, "#adopted", dupn(rt, css->d, css->len));
    touch(rt); return jundef();
}
static jv g_feat(struct jrt *rt, jv t, jv *args, int argc, int magic) { jstr *n = js_tostr(rt, ARG(0)); char b[32]; uint32_t k = n->len < 31 ? n->len : 31; memcpy(b, n->d, k); b[k] = 0; return jbool(!web_feat_off(b)); }   /* SOMA_188 */
static jv g_shadowhost(struct jrt *rt, jv t, jv *args, int argc, int magic) { struct wnode *n = node_or_null(ARG(0)); return n && wd_is_shadow_root(n) ? W(rt, n->parent) : jnull(); }
static jv g_focus(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    struct wnode *n = node_or_null(ARG(0)), *old = DOC(rt)->focus;
    if (old) old->flags &= (uint16_t)~WNF_FOCUS;
    DOC(rt)->focus = n; if (n) n->flags |= WNF_FOCUS;
    if (HOST(rt)->focus) HOST(rt)->focus(HOST(rt)->ud, n);
    touch(rt); return jundef();
}
static jv g_setready(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    js_def(rt, DOC(rt)->root->js, Kc(rt, "@@rs"), ARG(0), 0); return jundef();
}
static jv win_prop(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    int w, h, sx, sy; view_info(rt, &w, &h, &sx, &sy);
    switch (magic) { case 0: case 2: return jint(w); case 1: case 3: return jint(h); case 4: case 6: return jint(sx); case 5: case 7: return jint(sy); default: return jint(1); }
}
static jv illegal_ctor(struct jrt *rt, jv t, jv *args, int argc, int magic) { js_throw_err(rt, E_TYPE, "Illegal constructor"); }
static jv make_ctor_fn(struct jrt *rt, jv t, jv *args, int argc, int magic)
{
    rt->new_target = 0;
    switch (magic) {
    case 0: { jv a[1] = { S(rt, "img") }; jv el = node_method(rt, W(rt, DOC(rt)->root), a, 1, M_CREATEELEM); struct wnode *n = node_or_null(el);
        if (argc > 0) wd_set_attr(DOC(rt), n, "width", dupn(rt, js_tostr(rt, args[0])->d, js_tostr(rt, args[0])->len));
        if (argc > 1) wd_set_attr(DOC(rt), n, "height", dupn(rt, js_tostr(rt, args[1])->d, js_tostr(rt, args[1])->len));
        return el; }
    case 1: { jv a[1] = { S(rt, "option") }; jv el = node_method(rt, W(rt, DOC(rt)->root), a, 1, M_CREATEELEM); struct wnode *n = node_or_null(el);
        if (argc > 0) { jstr *s = js_tostr(rt, args[0]); wd_append(n, wd_new_text(DOC(rt), dupn(rt, s->d, s->len), s->len)); }
        if (argc > 1) wd_set_attr(DOC(rt), n, "value", dupn(rt, js_tostr(rt, args[1])->d, js_tostr(rt, args[1])->len));
        if (argc > 3 && js_truthy(args[3])) wd_set_attr(DOC(rt), n, "selected", "");
        return el; }
    case 2: { jv a[1] = { S(rt, "audio") }; jv el = node_method(rt, W(rt, DOC(rt)->root), a, 1, M_CREATEELEM);
        if (argc > 0) wd_set_attr(DOC(rt), node_or_null(el), "src", dupn(rt, js_tostr(rt, args[0])->d, js_tostr(rt, args[0])->len));
        return el; }
    case 3: return node_method(rt, W(rt, DOC(rt)->root), args, argc, M_CREATETEXT);
    case 4: return node_method(rt, W(rt, DOC(rt)->root), args, argc, M_CREATECOMMENT);
    default: return node_method(rt, W(rt, DOC(rt)->root), args, argc, M_CREATEFRAG);
    }
}

/* ------------------------------------------------------------ registro */
struct pdef { const char *name; int id; int rw; };
static const struct pdef NODE_P[] = {
    { "nodeType", P_NODETYPE, 0 }, { "nodeName", P_NODENAME, 0 }, { "nodeValue", P_NODEVALUE, 1 }, { "parentNode", P_PARENTNODE, 0 }, { "parentElement", P_PARENTELEMENT, 0 }, { "childNodes", P_CHILDNODES, 0 },
    { "firstChild", P_FIRSTCHILD, 0 }, { "lastChild", P_LASTCHILD, 0 }, { "nextSibling", P_NEXTSIB, 0 }, { "previousSibling", P_PREVSIB, 0 }, { "ownerDocument", P_OWNERDOC, 0 }, { "isConnected", P_ISCONNECTED, 0 },
    { "textContent", P_TEXTCONTENT, 1 }, { "baseURI", P_BASEURI, 0 }, { 0, 0, 0 }
};
static const struct pdef ELEM_P[] = {
    { "tagName", P_TAGNAME, 0 }, { "localName", P_LOCALNAME, 0 }, { "namespaceURI", P_NS, 0 }, { "id", P_ID, 1 }, { "className", P_CLASSNAME, 1 }, { "children", P_CHILDREN, 0 }, { "firstElementChild", P_FIRSTELEM, 0 }, { "lastElementChild", P_LASTELEM, 0 },
    { "nextElementSibling", P_NEXTELEM, 0 }, { "previousElementSibling", P_PREVELEM, 0 }, { "childElementCount", P_CHILDCOUNT, 0 }, { "innerHTML", P_INNERHTML, 1 }, { "outerHTML", P_OUTERHTML, 1 }, { "innerText", P_INNERTEXT, 1 },
    { "classList", P_CLASSLIST, 0 }, { "style", P_STYLE, 1 }, { "dataset", P_DATASET, 0 }, { "attributes", P_ATTRIBUTES, 0 }, { "offsetWidth", P_OFFSETW, 0 }, { "offsetHeight", P_OFFSETH, 0 }, { "offsetTop", P_OFFSETT, 0 },
    { "offsetLeft", P_OFFSETL, 0 }, { "offsetParent", P_OFFSETPARENT, 0 }, { "clientWidth", P_CLIENTW, 0 }, { "clientHeight", P_CLIENTH, 0 }, { "clientTop", P_CLIENTT, 0 }, { "clientLeft", P_CLIENTL, 0 },
    { "scrollWidth", P_SCROLLW, 0 }, { "scrollHeight", P_SCROLLH, 0 }, { "scrollTop", P_SCROLLT, 1 }, { "scrollLeft", P_SCROLLL, 1 }, { "tabIndex", P_TABINDEX, 1 }, { "hidden", P_HIDDEN, 1 },
    { "contentEditable", P_CONTENTEDITABLE, 1 }, { "isContentEditable", P_ISCE, 0 }, { "shadowRoot", P_SHADOWROOT, 0 },
    { "value", P_VALUE, 1 }, { "checked", P_CHECKED, 1 }, { "defaultChecked", P_DEFCHECKED, 0 }, { "defaultValue", P_DEFVALUE, 0 }, { "selected", P_SELECTED, 1 }, { "selectedIndex", P_SELINDEX, 1 }, { "options", P_OPTIONS, 0 },
    { "selectedOptions", P_SELECTEDOPTIONS, 0 }, { "form", P_FORM, 0 }, { "elements", P_ELEMENTS, 0 }, { "length", P_LENGTH, 0 }, { "files", P_FILES, 0 }, { "indeterminate", P_INDETERMINATE, 1 }, { "selectionStart", P_SELSTART, 1 },
    { "selectionEnd", P_SELEND, 1 }, { "validity", P_VALIDITY, 0 }, { "willValidate", P_WILLVALIDATE, 0 }, { "text", P_TEXT, 1 }, { "type", P_TYPE, 1 }, { "labels", P_LABELS, 0 }, { "naturalWidth", P_NATW, 0 },
    { "naturalHeight", P_NATH, 0 }, { "complete", P_COMPLETE, 0 }, { "currentSrc", P_CURRENTSRC, 0 }, { "contentWindow", P_CONTENTWINDOW, 0 }, { "contentDocument", P_CONTENTDOC, 0 }, { "index", P_INDEX, 0 },
    { "protocol", P_LINK0 + 0, 1 }, { "host", P_LINK0 + 1, 1 }, { "hostname", P_LINK0 + 2, 1 }, { "port", P_LINK0 + 3, 1 }, { "pathname", P_LINK0 + 4, 1 }, { "search", P_LINK0 + 5, 1 }, { "hash", P_LINK0 + 6, 1 }, { "origin", P_LINK0 + 7, 0 },
    { 0, 0, 0 }
};
static const struct pdef DOC_P[] = {
    { "documentElement", P_DOCELEM, 0 }, { "head", P_HEAD, 0 }, { "body", P_BODY, 1 }, { "title", P_TITLE, 1 }, { "URL", P_URL, 0 }, { "documentURI", P_URL, 0 }, { "domain", P_DOMAIN, 0 }, { "referrer", P_REFERRER, 0 },
    { "cookie", P_COOKIE, 1 }, { "readyState", P_READYSTATE, 0 }, { "characterSet", P_CHARSET, 0 }, { "charset", P_CHARSET, 0 }, { "inputEncoding", P_CHARSET, 0 }, { "contentType", P_CONTENTTYPE, 0 }, { "compatMode", P_COMPAT, 0 },
    { "defaultView", P_DEFVIEW, 0 }, { "activeElement", P_ACTIVEEL, 0 }, { "forms", P_FORMS, 0 }, { "images", P_IMAGES, 0 }, { "links", P_LINKS, 0 }, { "scripts", P_SCRIPTS, 0 }, { "all", P_ALL, 0 }, { "embeds", P_EMBEDS, 0 },
    { "styleSheets", P_STYLESHEETS, 0 }, { "hidden", P_DOCHIDDEN, 0 }, { "visibilityState", P_VISIBILITY, 0 }, { "lastModified", P_LASTMOD, 0 }, { "doctype", P_DOCTYPE, 0 }, { "scrollingElement", P_SCROLLINGEL, 0 },
    { "currentScript", P_CURRENTSCRIPT, 0 }, { 0, 0, 0 }
};
struct mdef { const char *name; int id; int nargs; };
static const struct mdef NODE_M[] = {
    { "appendChild", M_APPEND, 1 }, { "insertBefore", M_INSBEFORE, 2 }, { "removeChild", M_REMOVECHILD, 1 }, { "replaceChild", M_REPLACECHILD, 2 }, { "cloneNode", M_CLONE, 1 }, { "contains", M_CONTAINS, 1 },
    { "hasChildNodes", M_HASCHILD, 0 }, { "getRootNode", M_GETROOT, 0 }, { "isSameNode", M_ISSAME, 1 }, { "normalize", M_NOP, 0 }, { 0, 0, 0 }
};
static const struct mdef ELEM_M[] = {
    { "getAttribute", M_GETATTR, 1 }, { "setAttribute", M_SETATTR, 2 }, { "removeAttribute", M_REMOVEATTR, 1 }, { "hasAttribute", M_HASATTR, 1 }, { "hasAttributes", M_HASATTRS, 0 }, { "getAttributeNames", M_ATTRNAMES, 0 },
    { "getAttributeNS", M_GETATTR, 2 }, { "setAttributeNS", M_SETATTR, 3 }, { "removeAttributeNS", M_REMOVEATTR, 2 },
    { "querySelector", M_QS, 1 }, { "querySelectorAll", M_QSA, 1 }, { "matches", M_MATCHES, 1 }, { "webkitMatchesSelector", M_MATCHES, 1 }, { "msMatchesSelector", M_MATCHES, 1 }, { "closest", M_CLOSEST, 1 },
    { "getElementsByTagName", M_BYTAG, 1 }, { "getElementsByClassName", M_BYCLASS, 1 }, { "getElementsByTagNameNS", M_BYTAG, 2 }, { "reset", M_RESET, 0 }, { "submit", M_SUBMIT, 0 }, { "select", M_SELECT, 0 },
    { "setSelectionRange", M_NOP, 2 }, { "getBoundingClientRect", M_RECT, 0 }, { "setCustomValidity", M_NOP, 1 }, { "checkValidity", M_FALSE + 0, 0 }, { 0, 0, 0 }
};
static const struct mdef DOC_M[] = {
    { "getElementById", M_BYID, 1 }, { "getElementsByName", M_BYNAME, 1 }, { "createElement", M_CREATEELEM, 1 }, { "createTextNode", M_CREATETEXT, 1 }, { "createComment", M_CREATECOMMENT, 1 },
    { "createDocumentFragment", M_CREATEFRAG, 0 }, { "importNode", M_IMPORT, 2 }, { "adoptNode", M_NOP, 1 }, { "hasFocus", M_HASFOCUS, 0 }, { "write", M_WRITE, 1 }, { "writeln", M_WRITE, 1 },
    { "open", M_NOP, 0 }, { "close", M_NOP, 0 }, { "execCommand", M_FALSE, 1 }, { "elementFromPoint", M_ELEMFROMPOINT, 2 }, { 0, 0, 0 }
};
static jobj *mkclass(struct jrt *rt, const char *name, jobj *parent_proto)
{
    jobj *proto = js_obj_new(rt, parent_proto ? parent_proto : rt->p_object);
    jobj *c = js_native(rt, name, illegal_ctor, 0, 0);
    js_def(rt, c, rt->s_prototype, jobjv(proto), JP_NOCONF | JP_RO); js_def(rt, proto, rt->s_constructor, jobjv(c), 0);
    js_def(rt, rt->global, js_intern_c(rt, name), jobjv(c), 0);
    js_def(rt, proto, js_intern_c(rt, "@@toStringTag"), S(rt, name), 0);
    return proto;
}
static void reg_props(struct jrt *rt, jobj *proto, const struct pdef *t)
{
    for (; t->name; t++) js_getter(rt, proto, t->name, node_prop, t->rw ? node_prop : 0, t->id);
}
static void reg_methods(struct jrt *rt, jobj *proto, const struct mdef *t)
{
    for (; t->name; t++) js_method(rt, proto, t->name, node_method, t->nargs, t->id);
}
static void reg_g(struct jrt *rt, const char *name, jnative f, int nargs) { js_def(rt, rt->global, js_intern_c(rt, name), jobjv(js_native(rt, name, f, nargs, 0)), 0); }

void js_dom_init(struct jrt *rt)
{
    struct dom *d = (struct dom *)js_alloc(rt, (uint32_t)sizeof *d); rt->dom = d; rt->dyn = &DYN;
    wu_copy(d->url, HOST(rt)->url ? HOST(rt)->url : "about:blank", sizeof d->url);
    /* jerarquia de prototipos */
    mkclass(rt, "EventTarget", 0);
    d->p_node = mkclass(rt, "Node", 0);
    d->p_elem = mkclass(rt, "Element", d->p_node);
    d->p_html = mkclass(rt, "HTMLElement", d->p_elem);
    d->p_doc = mkclass(rt, "Document", d->p_node);
    { jobj *hd = mkclass(rt, "HTMLDocument", d->p_doc); (void)hd; }
    jobj *cd = mkclass(rt, "CharacterData", d->p_node);
    d->p_text = mkclass(rt, "Text", cd); d->p_comment = mkclass(rt, "Comment", cd);
    (void)mkclass(rt, "CDATASection", d->p_text); (void)mkclass(rt, "ProcessingInstruction", cd);   /* SOMA 188B: los parchea el polyfill ShadyDOM de YouTube */
    d->p_frag = mkclass(rt, "DocumentFragment", d->p_node);
    jobj *svg = mkclass(rt, "SVGElement", d->p_elem);
    static const struct { int tag; const char *name; } TAGS[] = {
        { WT_A, "HTMLAnchorElement" }, { WT_IMG, "HTMLImageElement" }, { WT_INPUT, "HTMLInputElement" }, { WT_FORM, "HTMLFormElement" }, { WT_BUTTON, "HTMLButtonElement" },
        { WT_SELECT, "HTMLSelectElement" }, { WT_TEXTAREA, "HTMLTextAreaElement" }, { WT_OPTION, "HTMLOptionElement" }, { WT_CANVAS, "HTMLCanvasElement" }, { WT_SCRIPT, "HTMLScriptElement" },
        { WT_IFRAME, "HTMLIFrameElement" }, { WT_BODY, "HTMLBodyElement" }, { WT_HTML, "HTMLHtmlElement" }, { WT_HEAD, "HTMLHeadElement" }, { WT_LINK, "HTMLLinkElement" }, { WT_STYLE, "HTMLStyleElement" },
        { WT_TABLE, "HTMLTableElement" }, { WT_TR, "HTMLTableRowElement" }, { WT_TD, "HTMLTableCellElement" }, { WT_VIDEO, "HTMLVideoElement" }, { WT_AUDIO, "HTMLAudioElement" }, { WT_DIV, "HTMLDivElement" },
        { WT_SPAN, "HTMLSpanElement" }, { WT_P, "HTMLParagraphElement" }, { WT_UL, "HTMLUListElement" }, { WT_OL, "HTMLOListElement" }, { WT_LI, "HTMLLIElement" }, { WT_LABEL, "HTMLLabelElement" },
        { WT_META, "HTMLMetaElement" }, { WT_TITLE, "HTMLTitleElement" }, { WT_BR, "HTMLBRElement" }, { WT_HR, "HTMLHRElement" }, { WT_H1, "HTMLHeadingElement" }, { WT_PRE, "HTMLPreElement" }, { WT_DIALOG, "HTMLDialogElement" },
        { WT_TEMPLATE, "HTMLTemplateElement" }, { WT_DETAILS, "HTMLDetailsElement" }, { WT_FIELDSET, "HTMLFieldSetElement" }, { WT_PROGRESS, "HTMLProgressElement" }, { 0, 0 }
    };
    for (int i = 0; TAGS[i].name; i++) d->p_tag[TAGS[i].tag] = mkclass(rt, TAGS[i].name, d->p_html);
    for (int t = WT_H2; t <= WT_H6; t++) d->p_tag[t] = d->p_tag[WT_H1];
    d->p_tag[WT_TH] = d->p_tag[WT_TD]; d->p_tag[WT_SVG] = svg;
    d->p_style = js_obj_new(rt, rt->p_object); d->p_cstyle = js_obj_new(rt, d->p_style); d->p_dataset = js_obj_new(rt, rt->p_object);
    d->p_storage = js_obj_new(rt, rt->p_object); d->p_classlist = js_obj_new(rt, rt->p_object);
    d->p_nodelist = js_obj_new(rt, rt->p_array); d->p_nodelist->cls = JC_OBJECT;
    js_def(rt, rt->global, js_intern_c(rt, "NodeList"), jobjv(js_native(rt, "NodeList", illegal_ctor, 0, 0)), 0);
    js_def(rt, js_get_c(rt, jobjv(rt->global), "NodeList").u.o, rt->s_prototype, jobjv(d->p_nodelist), 0);
    js_def(rt, rt->global, js_intern_c(rt, "HTMLCollection"), js_get_c(rt, jobjv(rt->global), "NodeList"), 0);
    reg_props(rt, d->p_node, NODE_P); reg_props(rt, d->p_elem, ELEM_P); reg_props(rt, d->p_doc, DOC_P);
    for (const struct attrdef *a = ATTRS; a->prop; a++) js_getter(rt, d->p_html, a->prop, node_prop, node_prop, ATTR_BASE + (int)(a - ATTRS));
    js_getter(rt, d->p_text, "data", node_prop, node_prop, P_NODEVALUE); js_getter(rt, d->p_text, "length", node_prop, 0, P_LENGTH); js_getter(rt, d->p_comment, "data", node_prop, node_prop, P_NODEVALUE);
    js_getter(rt, d->p_text, "wholeText", node_prop, 0, P_TEXTCONTENT);
    reg_methods(rt, d->p_node, NODE_M); reg_methods(rt, d->p_elem, ELEM_M); reg_methods(rt, d->p_doc, DOC_M);
    reg_methods(rt, d->p_doc, (const struct mdef[]){ { "querySelector", M_QS, 1 }, { "querySelectorAll", M_QSA, 1 }, { "getElementsByTagName", M_BYTAG, 1 }, { "getElementsByClassName", M_BYCLASS, 1 }, { 0, 0, 0 } });
    reg_methods(rt, d->p_frag, (const struct mdef[]){ { "querySelector", M_QS, 1 }, { "querySelectorAll", M_QSA, 1 }, { "getElementById", M_BYID, 1 }, { 0, 0, 0 } });
    {   /* SOMA 188B: mezcla ParentNode tambien en DocumentFragment y Document (YouTube: Array.from(fragment.children)) */
        static const struct pdef PN[] = { { "children", P_CHILDREN, 0 }, { "firstElementChild", P_FIRSTELEM, 0 }, { "lastElementChild", P_LASTELEM, 0 }, { "childElementCount", P_CHILDCOUNT, 0 }, { 0, 0, 0 } };
        reg_props(rt, d->p_frag, PN); reg_props(rt, d->p_doc, PN);
    }
    js_method(rt, d->p_nodelist, "item", node_method, 1, M_ITEM); js_method(rt, d->p_nodelist, "namedItem", node_method, 1, M_NAMEDITEM);
    js_method(rt, d->p_style, "getPropertyValue", node_method, 1, M_STYLE_GET); js_method(rt, d->p_style, "setProperty", node_method, 3, M_STYLE_SET); js_method(rt, d->p_style, "removeProperty", node_method, 1, M_STYLE_REMOVE);
    js_method(rt, d->p_style, "item", node_method, 1, M_STYLE_ITEM); js_getter(rt, d->p_style, "cssText", node_method, node_method, M_CSSTEXT); js_getter(rt, d->p_style, "length", node_method, 0, M_STYLE_LENGTH);
    js_method(rt, d->p_cstyle, "getPropertyValue", node_method, 1, M_STYLE_GET);
    js_method(rt, d->p_classlist, "add", node_method, 1, M_CL_ADD); js_method(rt, d->p_classlist, "remove", node_method, 1, M_CL_REMOVE); js_method(rt, d->p_classlist, "toggle", node_method, 2, M_CL_TOGGLE);
    js_method(rt, d->p_classlist, "contains", node_method, 1, M_CL_CONTAINS); js_method(rt, d->p_classlist, "replace", node_method, 2, M_CL_REPLACE); js_method(rt, d->p_classlist, "item", node_method, 1, M_CL_ITEM);
    js_method(rt, d->p_classlist, "toString", node_method, 0, M_CL_STR); js_method(rt, d->p_classlist, "forEach", node_method, 1, M_CL_FOREACH);
    js_def(rt, d->p_classlist, js_intern_c(rt, "@@iterator"), jobjv(js_native(rt, "values", node_method, 0, M_CL_ITER)), 0);
    js_getter(rt, d->p_classlist, "length", node_method, 0, M_CL_LENGTH); js_getter(rt, d->p_classlist, "value", node_method, node_method, M_CL_VALUE);
    js_method(rt, d->p_storage, "getItem", node_method, 1, M_ST_GETITEM); js_method(rt, d->p_storage, "setItem", node_method, 2, M_ST_SETITEM); js_method(rt, d->p_storage, "removeItem", node_method, 1, M_ST_REMOVEITEM);
    js_method(rt, d->p_storage, "clear", node_method, 0, M_ST_CLEAR); js_method(rt, d->p_storage, "key", node_method, 1, M_ST_KEY); js_getter(rt, d->p_storage, "length", node_method, 0, M_ST_LENGTH);
    /* constructores de fabrica */
    static const char *FN[] = { "Image", "Option", "Audio", "Text", "Comment", "DocumentFragment" };
    for (int i = 0; i < 6; i++) {
        jobj *c = js_native(rt, FN[i], make_ctor_fn, 0, i); jv old = js_get_c(rt, jobjv(rt->global), FN[i]);
        if (old.t == JT_OBJ) { jv pr = js_get(rt, old, rt->s_prototype); js_def(rt, c, rt->s_prototype, pr, JP_NOCONF | JP_RO); }
        js_def(rt, rt->global, js_intern_c(rt, FN[i]), jobjv(c), 0);
    }
    /* objetos globales */
    jobj *docw = wrap(rt, DOC(rt)->root);
    js_def(rt, rt->global, js_intern_c(rt, "document"), jobjv(docw), 0);
    js_def(rt, rt->global, js_intern_c(rt, "localStorage"), hostobj(rt, d->p_storage, HK_STORAGE, (void *)1, 1), 0);
    js_def(rt, rt->global, js_intern_c(rt, "sessionStorage"), hostobj(rt, d->p_storage, HK_STORAGE, (void *)2, 1), 0);
    static const char *WP[] = { "innerWidth", "innerHeight", "outerWidth", "outerHeight", "scrollX", "scrollY", "pageXOffset", "pageYOffset", "devicePixelRatio" };
    for (int i = 0; i < 9; i++) js_getter(rt, rt->global, WP[i], win_prop, 0, i);
    reg_g(rt, "__resolve", g_resolve, 2); reg_g(rt, "__url", g_url, 0); reg_g(rt, "__nav", g_nav, 2); reg_g(rt, "__rnd32", g_rnd32, 0); reg_g(rt, "__hist", g_hist, 1); reg_g(rt, "__ua", g_ua, 0);
    reg_g(rt, "__alert", g_alert, 1); reg_g(rt, "__now", g_now, 0); reg_g(rt, "__scrollTo", g_scroll, 2); reg_g(rt, "__fetch", g_fetch, 4); reg_g(rt, "__cstyle", g_cstyle, 1);
    reg_g(rt, "__attrfn", g_attrfn, 2); reg_g(rt, "__parseFrag", g_parsefrag, 1); reg_g(rt, "__activate", g_activate, 1); reg_g(rt, "__focus", g_focus, 1); reg_g(rt, "__attachShadow", g_attachshadow, 2); reg_g(rt, "__shadowHost", g_shadowhost, 1); reg_g(rt, "__adoptCss", g_adoptcss, 2); reg_g(rt, "__feat", g_feat, 1); reg_g(rt, "__setready", g_setready, 1);
    /* prelude en JS */
    int ok = 0; jv r = js_eval_string(rt, WEB_PRELUDE, (uint32_t)wu_strlen(WEB_PRELUDE), &ok);
    /* SOMA 148: capturar callback privado y borrar el gancho global. */
    if (ok) { jv mo = js_get_c(rt, jobjv(rt->global), "__soma148MutationNotify");
        if (js_is_callable(mo)) d->mo148_cb = mo.u.o;
        (void)js_delete(rt, rt->global, Kc(rt, "__soma148MutationNotify"));
    }
    if (!ok) { jstr *m = js_tostr(rt, js_get(rt, r, rt->s_message)); char b[300]; wu_copy(b, "prelude: ", sizeof b); wu_copy(b + 9, m->d, sizeof b - 9); js_log(rt, 2, b); }
}

/* ------------------------------------------------------------ eventos desde el navegador */
static const char *event_class(const char *t)
{
    if (wu_streq(t, "click") || wu_streq(t, "dblclick") || wu_streq(t, "mousedown") || wu_streq(t, "mouseup") || wu_streq(t, "mousemove") || wu_streq(t, "mouseover") || wu_streq(t, "mouseout") ||
        wu_streq(t, "mouseenter") || wu_streq(t, "mouseleave") || wu_streq(t, "contextmenu") || wu_streq(t, "auxclick")) return "MouseEvent";
    if (wu_starts(t, "pointer")) return "PointerEvent";
    if (wu_streq(t, "keydown") || wu_streq(t, "keyup") || wu_streq(t, "keypress")) return "KeyboardEvent";
    if (wu_streq(t, "focus") || wu_streq(t, "blur") || wu_streq(t, "focusin") || wu_streq(t, "focusout")) return "FocusEvent";
    if (wu_streq(t, "wheel")) return "WheelEvent";
    if (wu_streq(t, "input") || wu_streq(t, "beforeinput")) return "InputEvent";
    if (wu_streq(t, "submit")) return "SubmitEvent";
    if (wu_starts(t, "transition")) return "TransitionEvent";   /* SOMA_188 */
    if (wu_starts(t, "animation")) return "AnimationEvent";
    return "Event";
}
/* SOMA_187A: KeyboardEvent.code a partir de key (disposicion US): "h" -> "KeyH", "7" -> "Digit7", " " -> "Space", signos -> "Minus"... */
static void key_code_name(const char *k, char *out, uint32_t max)
{
    out[0] = 0; if (!k || !k[0]) return;
    if (!k[1]) {
        char c = k[0];
        if (c >= 'a' && c <= 'z') { wu_copy(out, "KeyX", max); out[3] = (char)(c - 32); return; }
        if (c >= 'A' && c <= 'Z') { wu_copy(out, "KeyX", max); out[3] = c; return; }
        if (c >= '0' && c <= '9') { wu_copy(out, "DigitX", max); out[5] = c; return; }
        static const struct { char c; const char *n; } P[] = { { ' ', "Space" }, { '-', "Minus" }, { '=', "Equal" }, { ',', "Comma" }, { '.', "Period" }, { '/', "Slash" }, { ';', "Semicolon" },
            { '\'', "Quote" }, { '[', "BracketLeft" }, { ']', "BracketRight" }, { '\\', "Backslash" }, { '`', "Backquote" }, { 0, 0 } };
        for (int i = 0; P[i].n; i++) if (P[i].c == c) { wu_copy(out, P[i].n, max); return; }
        return;
    }
    wu_copy(out, k, max);   /* Enter, Tab, Escape, ArrowUp... coinciden con key */
}
int js_dom_dispatch(struct jrt *rt, struct wnode *target, const char *type, int x, int y, int key, const char *keystr, int mods)
{
    const char *cls = event_class(type);
    int nobub = wu_streq(type, "focus") || wu_streq(type, "blur") || wu_streq(type, "mouseenter") || wu_streq(type, "mouseleave") || wu_streq(type, "load") || wu_streq(type, "scroll") || wu_streq(type, "error");
    int nocancel = wu_streq(type, "focus") || wu_streq(type, "blur") || wu_streq(type, "focusin") || wu_streq(type, "focusout") || wu_streq(type, "load") || wu_streq(type, "scroll") || wu_streq(type, "input") || wu_streq(type, "change");
    jobj *init = js_obj_new(rt, rt->p_object); int vw, vh, sx, sy; view_info(rt, &vw, &vh, &sx, &sy);
    js_set_c(rt, init, "bubbles", jbool(!nobub)); js_set_c(rt, init, "cancelable", jbool(!nocancel));
    { int nocomp = wu_streq(type, "load") || wu_streq(type, "scroll") || wu_streq(type, "error") || wu_streq(type, "change") || wu_streq(type, "mouseenter") || wu_streq(type, "mouseleave");
      js_set_c(rt, init, "composed", jbool(!nocomp)); }   /* SOMA_188: eventos de usuario cruzan el shadow DOM, como en los navegadores */
    js_set_c(rt, init, "clientX", jint(x)); js_set_c(rt, init, "clientY", jint(y)); js_set_c(rt, init, "x", jint(x)); js_set_c(rt, init, "y", jint(y));
    js_set_c(rt, init, "pageX", jint(x + sx)); js_set_c(rt, init, "pageY", jint(y + sy)); js_set_c(rt, init, "screenX", jint(x)); js_set_c(rt, init, "screenY", jint(y));
    js_set_c(rt, init, "shiftKey", jbool(mods & 1)); js_set_c(rt, init, "ctrlKey", jbool(mods & 2)); js_set_c(rt, init, "altKey", jbool(mods & 4)); js_set_c(rt, init, "metaKey", jbool(mods & 8));
    if (cls[0] == 'K') {
        js_set_c(rt, init, "keyCode", jint(key)); js_set_c(rt, init, "which", jint(key)); js_set_c(rt, init, "key", S(rt, keystr ? keystr : ""));
        { char cb[24]; key_code_name(keystr, cb, sizeof cb); js_set_c(rt, init, "code", S(rt, cb)); }   /* SOMA_187A */
    } else if (cls[0] == 'T' || cls[0] == 'A') {   /* SOMA_188: TransitionEvent/AnimationEvent: nombre en keystr, ms en key */
        js_set_c(rt, init, cls[0] == 'T' ? "propertyName" : "animationName", S(rt, keystr ? keystr : ""));
        js_set_c(rt, init, "elapsedTime", jnum(f64_div(f64_from_i64(key), f64_from_i64(1000))));
    } else if (cls[0] == 'I') {   /* SOMA_187A: InputEvent con data e inputType */
        if (keystr) js_set_c(rt, init, "data", S(rt, keystr));
        js_set_c(rt, init, "inputType", S(rt, !keystr ? "deleteContentBackward" : (keystr[0] == '\n' || keystr[0] == '\r') ? "insertLineBreak" : "insertText"));
    } else if (cls[0] == 'M' || cls[0] == 'P') { js_set_c(rt, init, "button", jint(wu_streq(type, "contextmenu") ? 2 : 0)); js_set_c(rt, init, "buttons", jint(wu_streq(type, "mousedown") ? 1 : 0)); js_set_c(rt, init, "detail", jint(wu_streq(type, "dblclick") ? 2 : 1)); }
    jv ctor = js_get_c(rt, jobjv(rt->global), cls);
    jv targs[2] = { S(rt, type), jobjv(init) };
    jv ev = js_construct(rt, ctor, targs, 2, 0);
    js_set_c(rt, ev.u.o, "isTrusted", jbool(1));
    jv tv = target ? W(rt, target) : jobjv(rt->global);
    jv disp = js_get_c(rt, tv, "dispatchEvent");
    js_call(rt, disp, tv, &ev, 1);
    return js_truthy(js_get_c(rt, ev, "defaultPrevented"));
}
static void fire_simple(struct jrt *rt, jv target, const char *type, int bubbles)
{
    jobj *init = js_obj_new(rt, rt->p_object); js_set_c(rt, init, "bubbles", jbool(bubbles));
    jv a[2] = { S(rt, type), jobjv(init) }; jv ev = js_construct(rt, js_get_c(rt, jobjv(rt->global), "Event"), a, 2, 0);
    js_call(rt, js_get_c(rt, target, "dispatchEvent"), target, &ev, 1);
}
void js_dom_fire_load(struct jrt *rt)
{
    jv doc = jobjv(DOC(rt)->root->js), win = jobjv(rt->global); jv a = S(rt, "interactive");
    g_setready(rt, jundef(), &a, 1, 0); fire_simple(rt, doc, "readystatechange", 0); fire_simple(rt, doc, "DOMContentLoaded", 1);
    a = S(rt, "complete"); g_setready(rt, jundef(), &a, 1, 0); fire_simple(rt, doc, "readystatechange", 0);
    fire_simple(rt, win, "load", 0);
}

void js_dom_set_script(struct jrt *rt, struct wnode *n) { if (rt->dom) DOM(rt)->cur_script = n; }
void js_dom_set_url(struct jrt *rt, const char *url) { if (rt->dom) wu_copy(DOM(rt)->url, url, sizeof DOM(rt)->url); }
