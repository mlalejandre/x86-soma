/* SOMA_188: modulos ES (<script type="module">, import/export, import(), import.meta, mapas de importacion).
 * El interprete no tiene un modo modulo en el analizador: cada modulo se reescribe aqui como una funcion
 *   (function(__exp, __imp, __meta, __dimp){ <getters de exportacion> <imports> <cuerpo> })
 * Los export pasan a ser getters sobre el objeto del modulo (enlaces vivos: devuelven el valor actual de la variable),
 * los import se resuelven al empezar (las dependencias se evaluan antes, en profundidad, como en los navegadores) y
 * import()/import.meta se cambian por __dimp()/__meta. El registro de modulos esta en el preludio (__modImport).
 * Solo se reescribe lo que esta en el nivel superior; cadenas, plantillas, comentarios y regex se respetan y los
 * saltos de linea se conservan (los numeros de linea de los errores siguen valiendo). */
#include "web_js_i.h"
#define ARG(i) ((i) < argc ? args[i] : jundef())

struct mtx { const char *s, *e, *p; int prev_t; char prev_c; const char *prev_a, *prev_b; int err; };
struct mtok { int t; const char *a, *b; int nl; };   /* t: 'i' identificador, 's' cadena, 'n' numero, 'p' signo, 't' plantilla, 'r' regex, 0 fin */

static int m_idc(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || c >= 0x80 || c == '\\'; }
static int m_iskw_re(const char *a, const char *b)   /* palabras tras las que '/' empieza una regex */
{
    static const char *const K[] = { "return", "typeof", "case", "do", "else", "in", "of", "new", "delete", "void", "throw", "instanceof", "yield", "await", 0 };
    for (int i = 0; K[i]; i++) { uint32_t n = wu_strlen(K[i]); if ((uint32_t)(b - a) == n && wu_strneq(a, K[i], n)) return 1; }
    return 0;
}
static void m_skip(struct mtx *m, int *nl)
{
    for (;;) {
        while (m->p < m->e && (*m->p == ' ' || *m->p == '\t' || *m->p == '\n' || *m->p == '\r' || (unsigned char)*m->p == 0xC2 || (unsigned char)*m->p == 0xA0)) { if (*m->p == '\n') *nl = 1; m->p++; }
        if (m->p + 1 < m->e && m->p[0] == '/' && m->p[1] == '/') { while (m->p < m->e && *m->p != '\n') m->p++; continue; }
        if (m->p + 1 < m->e && m->p[0] == '/' && m->p[1] == '*') { m->p += 2; while (m->p + 1 < m->e && !(m->p[0] == '*' && m->p[1] == '/')) { if (*m->p == '\n') *nl = 1; m->p++; } m->p = m->p + 2 <= m->e ? m->p + 2 : m->e; continue; }
        if (m->p + 3 < m->e && m->p[0] == '<' && m->p[1] == '!' && m->p[2] == '-' && m->p[3] == '-') { while (m->p < m->e && *m->p != '\n') m->p++; continue; }
        break;
    }
}
static void m_next(struct mtx *m, struct mtok *k);
static void m_template(struct mtx *m)   /* m->p tras la ` inicial */
{
    while (m->p < m->e) {
        char c = *m->p;
        if (c == '\\') { m->p += 2; continue; }
        if (c == '`') { m->p++; return; }
        if (c == '$' && m->p + 1 < m->e && m->p[1] == '{') {
            m->p += 2; int d = 1; struct mtok k;
            while (m->p < m->e && d > 0) { m_next(m, &k); if (!k.t) return; if (k.t == 'p' && k.b - k.a == 1) { if (*k.a == '{') d++; else if (*k.a == '}') d--; } }
            continue;
        }
        m->p++;
    }
}
static void m_next(struct mtx *m, struct mtok *k)
{
    k->nl = 0; m_skip(m, &k->nl);
    k->a = m->p; k->t = 0;
    if (m->p >= m->e) { k->b = m->p; return; }
    unsigned char c = (unsigned char)*m->p;
    if (c == '"' || c == '\'') {
        m->p++; while (m->p < m->e && *m->p != (char)c && *m->p != '\n') { if (*m->p == '\\') m->p++; m->p++; } if (m->p < m->e) m->p++;
        k->t = 's';
    } else if (c == '`') { m->p++; m_template(m); k->t = 't'; }
    else if ((c >= '0' && c <= '9') || (c == '.' && m->p + 1 < m->e && m->p[1] >= '0' && m->p[1] <= '9')) { while (m->p < m->e && (m_idc((unsigned char)*m->p) || *m->p == '.')) m->p++; k->t = 'n'; }
    else if (m_idc(c) || c == '#') { m->p++; while (m->p < m->e && m_idc((unsigned char)*m->p)) m->p++; k->t = 'i'; }
    else if (c == '/') {
        int re = m->prev_t == 0 || (m->prev_t == 'p' && m->prev_c != ')' && m->prev_c != ']') || (m->prev_t == 'i' && m_iskw_re(m->prev_a, m->prev_b));
        if (re) {
            m->p++; int cls = 0;
            while (m->p < m->e && *m->p != '\n') { char d = *m->p; if (d == '\\') { m->p += 2; continue; } if (d == '[') cls = 1; else if (d == ']') cls = 0; else if (d == '/' && !cls) break; m->p++; }
            if (m->p < m->e && *m->p == '/') m->p++;
            while (m->p < m->e && m_idc((unsigned char)*m->p)) m->p++;
            k->t = 'r';
        } else { m->p++; if (m->p < m->e && *m->p == '=') m->p++; k->t = 'p'; }
    } else {
        /* signos: '=>' '...' '?.' y el resto de uno en uno */
        if (c == '=' && m->p + 1 < m->e && m->p[1] == '>') m->p += 2;
        else if (c == '.' && m->p + 2 < m->e && m->p[1] == '.' && m->p[2] == '.') m->p += 3;
        else m->p++;
        k->t = 'p';
    }
    k->b = m->p;
    m->prev_t = k->t; m->prev_c = k->t == 'p' ? *k->a : 0; m->prev_a = k->a; m->prev_b = k->b;
}
static int tis(const struct mtok *k, const char *w) { uint32_t n = wu_strlen(w); return k->t == 'i' && (uint32_t)(k->b - k->a) == n && wu_strneq(k->a, w, n); }
static int tpc(const struct mtok *k, char c) { return k->t == 'p' && k->b - k->a == 1 && *k->a == c; }
static void addn(struct wb *b, const char *a, const char *z) { wb_addn(b, a, (uint32_t)(z - a)); }
static void newlines(struct wb *b, const char *a, const char *z) { for (; a < z; a++) if (*a == '\n') wb_addc(b, '\n'); }
static void qname(struct wb *b, const struct mtok *k)   /* nombre de exportacion como cadena JS */
{
    if (k->t == 's') { addn(b, k->a, k->b); return; }
    wb_addc(b, '"'); addn(b, k->a, k->b); wb_addc(b, '"');
}
static void getter(struct wb *g, const struct mtok *ename, const char *expr_a, const char *expr_b, const char *pre)
{
    wb_add(g, "Object.defineProperty(__exp,"); qname(g, ename); wb_add(g, ",{enumerable:!0,configurable:!0,get:function(){return ");
    if (pre) wb_add(g, pre);
    addn(g, expr_a, expr_b); wb_add(g, "}});");
}
struct mstate { struct wb body, get, imp, bind, tail; int nimp; };
static int import_var(struct mstate *S, const struct mtok *spec)
{
    int id = ++S->nimp; char t[16]; int l = wu_itoa(id, t); t[l] = 0;
    wb_add(&S->imp, "var __i"); wb_add(&S->imp, t); wb_add(&S->imp, "=__imp("); addn(&S->imp, spec->a, spec->b); wb_add(&S->imp, ");");
    return id;
}
static void ivar(struct wb *b, int id) { char t[16]; int l = wu_itoa(id, t); t[l] = 0; wb_add(b, "__i"); wb_add(b, t); }
/* fin de declaracion import/export: with/assert {...} y ';' opcionales */
static void stmt_tail(struct mtx *m)
{
    const char *save = m->p; struct mtx sm = *m; struct mtok k; m_next(m, &k);
    if (tis(&k, "with") || tis(&k, "assert")) { m_next(m, &k); if (tpc(&k, '{')) { int d = 1; while (d > 0) { m_next(m, &k); if (!k.t) break; if (tpc(&k, '{')) d++; else if (tpc(&k, '}')) d--; } } save = m->p; sm = *m; m_next(m, &k); }
    if (tpc(&k, ';')) return;
    *m = sm; m->p = save;
}

/* names del patron de una declaracion exportada: identificadores que no son claves ni valores por defecto */
static void decl_names(struct mtx *m, struct mstate *S)
{
    struct mtok k;
    for (;;) {
        m_next(m, &k); if (!k.t) return;
        if (k.t == 'i') { getter(&S->get, &k, k.a, k.b, 0); }
        else if (tpc(&k, '{') || tpc(&k, '[')) {
            int d = 1; struct mtok prev = k, cur; int skipping = 0, sd = 0;
            while (d > 0) {
                m_next(m, &cur); if (!cur.t) return;
                if (skipping) { if (tpc(&cur, '{') || tpc(&cur, '[') || tpc(&cur, '(')) sd++; else if (tpc(&cur, '}') || tpc(&cur, ']') || tpc(&cur, ')')) { if (sd == 0) { skipping = 0; d--; continue; } sd--; } else if (tpc(&cur, ',') && sd == 0) skipping = 0; prev = cur; continue; }
                if (tpc(&cur, '{') || tpc(&cur, '[')) d++;
                else if (tpc(&cur, '}') || tpc(&cur, ']')) d--;
                else if (tpc(&cur, '=')) { skipping = 1; sd = 0; }
                else if (cur.t == 'i') {
                    struct mtx sm = *m; struct mtok nx; m_next(m, &nx); *m = sm;
                    if (!tpc(&nx, ':') && !(prev.t == 'p' && prev.b - prev.a == 3)) getter(&S->get, &cur, cur.a, cur.b, 0);
                    else if (prev.t == 'p' && prev.b - prev.a == 3) getter(&S->get, &cur, cur.a, cur.b, 0);   /* ...resto */
                }
                prev = cur;
            }
        }
        /* saltar el inicializador hasta ',' de nivel 0 o el final de la declaracion */
        int d = 0;
        for (;;) {
            struct mtx sm = *m; m_next(m, &k);
            if (!k.t) return;
            if (tpc(&k, '(') || tpc(&k, '[') || tpc(&k, '{')) d++;
            else if (tpc(&k, ')') || tpc(&k, ']') || tpc(&k, '}')) { if (d == 0) { *m = sm; return; } d--; }
            else if (d == 0 && tpc(&k, ',')) break;
            else if (d == 0 && tpc(&k, ';')) { *m = sm; return; }
            else if (d == 0 && k.nl && k.t == 'i' && (tis(&k, "export") || tis(&k, "import") || tis(&k, "const") || tis(&k, "let") || tis(&k, "var") || tis(&k, "function") || tis(&k, "class"))) { *m = sm; return; }
        }
    }
}

int js_module_transform(const char *src, uint32_t len, struct wb *out, int async_wrap)
{
    struct mstate S; memset(&S, 0, sizeof S);
    struct mtx m; memset(&m, 0, sizeof m); m.s = src; m.e = src + len; m.p = src;
    const char *copy = src; int depth = 0; struct mtok k, prevk; memset(&prevk, 0, sizeof prevk);
    for (;;) {
        m_next(&m, &k); if (!k.t) break;
        if (k.t == 'p') { char c = *k.a; if (c == '(' || c == '[' || c == '{') depth++; else if ((c == ')' || c == ']' || c == '}') && depth > 0) depth--; }
        int after_dot = prevk.t == 'p' && prevk.b - prevk.a == 1 && *prevk.a == '.';
        if (k.t == 'i' && !after_dot && tis(&k, "import")) {
            struct mtx sm = m; struct mtok n1; m_next(&m, &n1);
            if (tpc(&n1, '(')) { addn(&S.body, copy, k.a); wb_add(&S.body, "__dimp"); copy = k.b; m = sm; prevk = k; continue; }
            if (tpc(&n1, '.')) { struct mtok n2; m_next(&m, &n2); if (tis(&n2, "meta")) { addn(&S.body, copy, k.a); wb_add(&S.body, "__meta"); copy = n2.b; prevk = n2; continue; } m = sm; prevk = k; continue; }
            if (depth != 0) { m = sm; prevk = k; continue; }
            /* import estatico */
            addn(&S.body, copy, k.a);
            struct mtok names[64], alias[64]; int nn = 0, kinds[64]; struct mtok spec; memset(&spec, 0, sizeof spec);
            struct mtok cur = n1;
            for (int guard = 0; guard < 400 && cur.t; guard++) {
                if (cur.t == 's') { spec = cur; break; }
                if (tis(&cur, "from") || tpc(&cur, ',')) { m_next(&m, &cur); continue; }
                if (tpc(&cur, '*')) { struct mtok as, nm; m_next(&m, &as); m_next(&m, &nm); if (nn < 64) { kinds[nn] = 2; names[nn] = nm; alias[nn] = nm; nn++; } m_next(&m, &cur); continue; }
                if (tpc(&cur, '{')) {
                    struct mtok a; m_next(&m, &a);
                    while (a.t && !tpc(&a, '}')) {
                        if (tpc(&a, ',')) { m_next(&m, &a); continue; }
                        struct mtok orig = a, loc = a; struct mtok n3; m_next(&m, &n3);
                        if (tis(&n3, "as")) { m_next(&m, &loc); m_next(&m, &n3); }
                        if (nn < 64) { kinds[nn] = 1; names[nn] = orig; alias[nn] = loc; nn++; }
                        a = n3;
                    }
                    m_next(&m, &cur); continue;
                }
                if (cur.t == 'i') { if (nn < 64) { kinds[nn] = 0; names[nn] = cur; alias[nn] = cur; nn++; } m_next(&m, &cur); continue; }
                m_next(&m, &cur);
            }
            if (!spec.t) { S.body.err = 1; break; }
            stmt_tail(&m);
            newlines(&S.body, k.a, m.p);
            int id = import_var(&S, &spec);
            for (int i = 0; i < nn; i++) {
                wb_add(&S.bind, "var "); addn(&S.bind, alias[i].a, alias[i].b); wb_addc(&S.bind, '='); ivar(&S.bind, id);
                if (kinds[i] == 0) wb_add(&S.bind, ".default");
                else if (kinds[i] == 1) { wb_addc(&S.bind, '['); qname(&S.bind, &names[i]); wb_addc(&S.bind, ']'); }
                wb_addc(&S.bind, ';');
            }
            copy = m.p; prevk.t = 'p'; prevk.a = ";"; prevk.b = prevk.a + 1; m.prev_t = 'p'; m.prev_c = ';';
            continue;
        }
        if (k.t == 'i' && !after_dot && depth == 0 && tis(&k, "export")) {
            struct mtx sm = m; struct mtok n1; m_next(&m, &n1);
            if (tpc(&n1, ':') || tpc(&n1, '.') || tpc(&n1, '(') || tpc(&n1, '=')) { m = sm; prevk = k; continue; }
            addn(&S.body, copy, k.a);
            if (tis(&n1, "default")) {
                struct mtx s2 = m; struct mtok d1, d2, d3; m_next(&m, &d1); m_next(&m, &d2); m_next(&m, &d3); m = s2;
                const struct mtok *nm = 0;
                if (tis(&d1, "function")) nm = tpc(&d2, '*') ? &d3 : &d2;
                else if (tis(&d1, "class")) nm = &d2;
                else if (tis(&d1, "async") && tis(&d2, "function")) { struct mtx s3 = m; struct mtok e1, e2, e3, e4; m_next(&m, &e1); m_next(&m, &e2); m_next(&m, &e3); m_next(&m, &e4); m = s3; nm = tpc(&e3, '*') ? &e4 : &e3; static struct mtok keep; keep = *nm; nm = &keep; }
                if (nm && nm->t == 'i' && !tis(nm, "extends")) {   /* declaracion con nombre: se queda; default -> nombre */
                    struct mtok dn; dn.t = 'i'; dn.a = "default"; dn.b = dn.a + 7; getter(&S.get, &dn, nm->a, nm->b, 0);
                    newlines(&S.body, k.a, d1.a); copy = d1.a; prevk = n1; continue;
                }
                wb_add(&S.body, "__exp.default="); newlines(&S.body, k.a, n1.b); copy = n1.b; prevk = n1; m.prev_t = 'p'; m.prev_c = '='; continue;
            }
            if (tpc(&n1, '{')) {
                struct mtok en[64], ln[64]; int ne = 0; struct mtok a; m_next(&m, &a);
                while (a.t && !tpc(&a, '}')) {
                    if (tpc(&a, ',')) { m_next(&m, &a); continue; }
                    struct mtok loc = a, ex = a, n3; m_next(&m, &n3);
                    if (tis(&n3, "as")) { m_next(&m, &ex); m_next(&m, &n3); }
                    if (ne < 64) { ln[ne] = loc; en[ne] = ex; ne++; }
                    a = n3;
                }
                struct mtx s2 = m; struct mtok f; m_next(&m, &f);
                if (tis(&f, "from")) {
                    struct mtok spec; m_next(&m, &spec); stmt_tail(&m); int id = import_var(&S, &spec);
                    for (int i = 0; i < ne; i++) {
                        char t[24]; int l = wu_itoa(id, t + 3); t[0] = '_'; t[1] = '_'; t[2] = 'i'; t[3 + l] = 0;
                        struct wb e = {0}; wb_add(&e, t); wb_addc(&e, '['); qname(&e, &ln[i]); wb_addc(&e, ']');
                        if (e.p) getter(&S.get, &en[i], e.p, e.p + e.len, 0);
                        wb_free(&e);
                    }
                } else { m = s2; stmt_tail(&m); for (int i = 0; i < ne; i++) getter(&S.get, &en[i], ln[i].a, ln[i].b, 0); }
                newlines(&S.body, k.a, m.p); copy = m.p; prevk.t = 'p'; m.prev_t = 'p'; m.prev_c = ';'; continue;
            }
            if (tpc(&n1, '*')) {
                struct mtok a; m_next(&m, &a); struct mtok ns; memset(&ns, 0, sizeof ns);
                if (tis(&a, "as")) { m_next(&m, &ns); m_next(&m, &a); }
                struct mtok spec; m_next(&m, &spec); stmt_tail(&m); int id = import_var(&S, &spec);
                if (ns.t) { char t[24]; int l = wu_itoa(id, t + 3); t[0] = '_'; t[1] = '_'; t[2] = 'i'; t[3 + l] = 0; getter(&S.get, &ns, t, t + 3 + l, 0); }
                else { wb_add(&S.tail, "__expAll(__exp,"); ivar(&S.tail, id); wb_add(&S.tail, ");"); }
                newlines(&S.body, k.a, m.p); copy = m.p; m.prev_t = 'p'; m.prev_c = ';'; continue;
            }
            if (tis(&n1, "var") || tis(&n1, "let") || tis(&n1, "const")) {
                struct mtx s2 = m; decl_names(&m, &S); m = s2;
                newlines(&S.body, k.a, n1.a); copy = n1.a; prevk = n1; continue;
            }
            if (tis(&n1, "function") || tis(&n1, "class") || tis(&n1, "async")) {
                struct mtx s2 = m; struct mtok x; m_next(&m, &x);
                if (tis(&n1, "async")) m_next(&m, &x);   /* function */
                if (tpc(&x, '*')) m_next(&m, &x);
                if (x.t == 'i') getter(&S.get, &x, x.a, x.b, 0);
                m = s2; newlines(&S.body, k.a, n1.a); copy = n1.a; prevk = n1; continue;
            }
            m = sm; prevk = k; continue;
        }
        prevk = k;
    }
    addn(&S.body, copy, m.e);
    int err = S.body.err || S.get.err || S.imp.err || S.bind.err || S.tail.err;
    if (!err) {
        wb_add(out, async_wrap ? "(async function(__exp,__imp,__meta,__dimp){" : "(function(__exp,__imp,__meta,__dimp){");
        if (S.get.p) wb_addn(out, S.get.p, S.get.len);
        if (S.imp.p) wb_addn(out, S.imp.p, S.imp.len);
        if (S.bind.p) wb_addn(out, S.bind.p, S.bind.len);
        if (S.tail.p) wb_addn(out, S.tail.p, S.tail.len);
        if (S.body.p) wb_addn(out, S.body.p, S.body.len);
        wb_add(out, "\n})");
    }
    wb_free(&S.body); wb_free(&S.get); wb_free(&S.imp); wb_free(&S.bind); wb_free(&S.tail);
    return err || out->err ? -1 : 0;
}

/* __modTransform(fuente, asincrono) -> fuente de la funcion envoltorio */
static jv n_modtransform(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    jstr *s = js_tostr(rt, ARG(0)); struct wb o = {0};
    if (js_module_transform(s->d, s->len, &o, js_truthy(ARG(1))) != 0) { wb_free(&o); js_throw_err(rt, E_SYNTAX, "modulo ES no valido"); }
    jv r = jstrv(js_str_new(rt, o.p ? o.p : "", o.len)); wb_free(&o); return r;
}
#if defined(WEB_HOST_TEST)
int g_jmod_off;
#endif
int js_module_on(void)
{
#if defined(WEB_HOST_TEST)
    return !g_jmod_off;
#elif defined(__x86_64__)
    extern int bootinfo_disabled(const char *name);
    static int v = -1; if (v < 0) v = bootinfo_disabled("jsmodule") ? 0 : 1;
    return v;
#else
    return 1;
#endif
}
void js_module_init(struct jrt *rt)
{
    if (!js_module_on()) return;
    js_def(rt, rt->global, js_intern_c(rt, "__modTransform"), jobjv(js_native(rt, "__modTransform", n_modtransform, 2, 0)), 0);
}
