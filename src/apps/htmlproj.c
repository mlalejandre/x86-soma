/* SOMA_275_HTML: ver htmlproj.h */
#include <stdint.h>
#include <stddef.h>
#include "htmlproj.h"

/* ------------------------------------------------------------ utilidades */
static uint32_t slen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int ieq(const char *a, uint32_t an, const char *b) { uint32_t i = 0; for (; i < an && b[i]; i++) if (lc((unsigned char)a[i]) != lc((unsigned char)b[i])) return 0; return i == an && !b[i]; }
static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_sp(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

struct ob { char *p; uint32_t n, cap; int over; };
static void oc(struct ob *o, char c) { if (o->n + 1 < o->cap) o->p[o->n++] = c; else o->over = 1; }
static void on(struct ob *o, const char *s, uint32_t n) { for (uint32_t i = 0; i < n; i++) oc(o, s[i]); }
static void os(struct ob *o, const char *s) { on(o, s, slen(s)); }
static void odec(struct ob *o, int v) { char t[12]; int k = 0; if (v < 0) { oc(o, '-'); v = -v; } do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) oc(o, t[--k]); }
static void oend(struct ob *o) { if (o->cap) o->p[o->n < o->cap ? o->n : o->cap - 1] = 0; }

int html_path_join(const char *dir, const char *rel, char *out, uint32_t cap)
{
    char tmp[512]; uint32_t n = 0;
    if (rel[0] != '/') { for (uint32_t i = 0; dir && dir[i] && n + 1 < sizeof tmp; i++) tmp[n++] = dir[i]; if (n == 0 || tmp[n - 1] != '/') tmp[n++] = '/'; }
    for (uint32_t i = 0; rel[i] && rel[i] != '?' && rel[i] != '#' && n + 1 < sizeof tmp; i++) tmp[n++] = rel[i];
    tmp[n] = 0;
    uint32_t o = 0; uint32_t i = 0;
    if (o + 1 >= cap) return -1;
    out[o++] = '/';
    while (i < n) {
        while (tmp[i] == '/') i++;
        uint32_t s = i; while (i < n && tmp[i] != '/') i++;
        uint32_t L = i - s;
        if (L == 0 || (L == 1 && tmp[s] == '.')) continue;
        if (L == 2 && tmp[s] == '.' && tmp[s + 1] == '.') { if (o > 1) { o--; while (o > 1 && out[o - 1] != '/') o--; } continue; }
        if (o + L + 2 >= cap) return -1;
        for (uint32_t k = 0; k < L; k++) out[o++] = tmp[s + k];
        out[o++] = '/';
    }
    if (o > 1) o--;
    out[o] = 0;
    return 0;
}

static int is_remote(const char *u, uint32_t n)
{
    if (n == 0) return 1;
    if (u[0] == '#') return 1;
    static const char *const P[] = { "http:", "https:", "data:", "mailto:", "tel:", "javascript:", "blob:", "about:", "//", 0 };
    for (int i = 0; P[i]; i++) { uint32_t pl = slen(P[i]); if (n >= pl && ieq(u, pl, P[i])) return 1; }
    return 0;
}

/* ------------------------------------------------------------ analizador de etiquetas */
struct tag { uint32_t a, b; };                 /* nombre: src[a..b) */
struct attr { uint32_t na, nb, va, vb; int has; };
/* lee los atributos de una etiqueta de apertura desde p (tras el nombre) hasta '>'; devuelve la posicion tras '>' o 0 si no se cierra */
static uint32_t read_attrs(const char *s, uint32_t p, uint32_t len, struct attr *at, int max, int *nat, int *selfclose)
{
    *nat = 0; *selfclose = 0;
    while (p < len) {
        while (p < len && is_sp(s[p])) p++;
        if (p >= len) return 0;
        if (s[p] == '>') return p + 1;
        if (s[p] == '/' ) { if (p + 1 < len && s[p + 1] == '>') { *selfclose = 1; return p + 2; } p++; continue; }
        struct attr a; a.na = p; while (p < len && !is_sp(s[p]) && s[p] != '=' && s[p] != '>' && !(s[p] == '/' && p + 1 < len && s[p + 1] == '>')) p++;
        a.nb = p; a.has = 0; a.va = a.vb = p;
        while (p < len && is_sp(s[p])) p++;
        if (p < len && s[p] == '=') {
            p++; while (p < len && is_sp(s[p])) p++;
            a.has = 1;
            if (p < len && (s[p] == '"' || s[p] == '\'')) { char q = s[p++]; a.va = p; while (p < len && s[p] != q) p++; if (p >= len) return 0; a.vb = p; p++; }
            else { a.va = p; while (p < len && !is_sp(s[p]) && s[p] != '>') p++; a.vb = p; }
        }
        if (a.nb > a.na && *nat < max) at[(*nat)++] = a;
    }
    return 0;
}
static int get_attr(const char *s, const struct attr *at, int n, const char *name, uint32_t *va, uint32_t *vb)
{
    for (int i = 0; i < n; i++) if (ieq(s + at[i].na, at[i].nb - at[i].na, name)) { if (va) *va = at[i].va; if (vb) *vb = at[i].vb; return at[i].has ? 2 : 1; }
    return 0;
}
static int ieq2(const char *x, const char *y, uint32_t n) { for (uint32_t i = 0; i < n; i++) if (lc((unsigned char)x[i]) != lc((unsigned char)y[i])) return 0; return 1; }
static int in_list(const char *s, uint32_t n, const char *const *L) { for (int i = 0; L[i]; i++) if (ieq(s, n, L[i])) return 1; return 0; }
static const char *const VOID_TAGS[] = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr", 0 };
static const char *const OPT_END[] = { "li", "p", "dt", "dd", "tr", "td", "th", "thead", "tbody", "tfoot", "option", "optgroup", "colgroup", "html", "head", "body", "rt", "rp", 0 };

#define DEPTH 96

struct ck { const char *s; uint32_t len; const char *name; struct ob *o; struct html_report *rep; int line_cache_pos, line_cache; };
static int ckline(struct ck *c, uint32_t pos)
{
    if ((uint32_t)c->line_cache_pos > pos) { c->line_cache_pos = 0; c->line_cache = 1; }
    for (uint32_t i = (uint32_t)c->line_cache_pos; i < pos; i++) if (c->s[i] == '\n') c->line_cache++;
    c->line_cache_pos = (int)pos; return c->line_cache;
}
static void msg(struct ck *c, uint32_t pos, int err, const char *t1, const char *nm, uint32_t nl, const char *t2)
{
    os(c->o, c->name); oc(c->o, ':'); odec(c->o, ckline(c, pos)); os(c->o, err ? ": error: " : ": aviso: ");
    os(c->o, t1); if (nm) on(c->o, nm, nl); if (t2) os(c->o, t2); oc(c->o, '\n');
    if (err) c->rep->errores++; else c->rep->avisos++;
}

int html_check(const char *src, uint32_t len, const char *name, const char *dir, const struct html_env *env, char *out, uint32_t cap,
               struct html_report *rep)
{
    struct ob o = { out, 0, cap, 0 };
    struct html_report r0 = { 0, 0, 0, 0 }; if (!rep) rep = &r0; rep->errores = rep->avisos = rep->scripts = rep->enlaces = 0;
    struct ck c = { src, len, name, &o, rep, 0, 1 };
    struct tag stack[DEPTH]; int sp = 0;
    uint32_t pos_stack[DEPTH];
    uint32_t ids[64][2]; int nids = 0;
    int seen_doctype = 0, seen_title = 0, seen_lang = 0, seen_html = 0, seen_charset = 0, nscripts = 0, uses_canvas = 0;
    uint32_t p = 0;
    while (p < len) {
        if (src[p] != '<') { p++; continue; }
        uint32_t at = p;
        if (p + 3 < len && src[p + 1] == '!' && src[p + 2] == '-' && src[p + 3] == '-') {                 /* comentario */
            uint32_t q = p + 4; int ok = 0;
            while (q + 2 < len) { if (src[q] == '-' && src[q + 1] == '-' && src[q + 2] == '>') { ok = 1; q += 3; break; } q++; }
            if (!ok) { msg(&c, at, 1, "comentario <!-- sin cerrar", 0, 0, 0); break; }
            p = q; continue;
        }
        if (p + 1 < len && src[p + 1] == '!') {                                                              /* doctype */
            uint32_t q = p; while (q < len && src[q] != '>') q++;
            if (p + 9 <= len && ieq(src + p + 2, 7, "doctype")) seen_doctype = 1;
            p = q < len ? q + 1 : len; continue;
        }
        if (p + 1 < len && src[p + 1] == '/') {                                                              /* cierre */
            uint32_t a = p + 2, b = a; while (b < len && !is_sp(src[b]) && src[b] != '>') b++;
            uint32_t q = b; while (q < len && src[q] != '>') q++;
            if (q >= len) { msg(&c, at, 1, "etiqueta </", src + a, b - a, " sin cerrar con >"); break; }
            if (b == a) { p = q + 1; continue; }
            int found = -1; for (int i = sp - 1; i >= 0; i--) if (stack[i].b - stack[i].a == b - a && ieq2(src + stack[i].a, src + a, b - a)) { found = i; break; }
            if (found < 0) { if (!in_list(src + a, b - a, VOID_TAGS)) msg(&c, at, 1, "</", src + a, b - a, "> sin etiqueta de apertura"); }
            else {
                for (int i = sp - 1; i > found; i--) {
                    if (!in_list(src + stack[i].a, stack[i].b - stack[i].a, OPT_END)) msg(&c, pos_stack[i], 1, "<", src + stack[i].a, stack[i].b - stack[i].a, "> sin cerrar");
                }
                sp = found;
            }
            p = q + 1; continue;
        }
        if (p + 1 < len && is_alpha((unsigned char)src[p + 1])) {                                            /* apertura */
            uint32_t a = p + 1, b = a; while (b < len && !is_sp(src[b]) && src[b] != '>' && src[b] != '/') b++;
            struct attr at_[24]; int nat = 0, sc = 0;
            uint32_t q = read_attrs(src, b, len, at_, 24, &nat, &sc);
            if (!q) { msg(&c, at, 1, "etiqueta <", src + a, b - a, " sin cerrar (falta > o una comilla)"); break; }
            uint32_t nl = b - a; const char *nm = src + a;
            uint32_t v0, v1;
            if (ieq(nm, nl, "html")) { seen_html = 1; if (get_attr(src, at_, nat, "lang", 0, 0)) seen_lang = 1; }
            if (ieq(nm, nl, "title")) seen_title = 1;
            if (ieq(nm, nl, "meta") && get_attr(src, at_, nat, "charset", 0, 0)) seen_charset = 1;
            if (ieq(nm, nl, "canvas")) uses_canvas = 1;
            if (get_attr(src, at_, nat, "id", &v0, &v1) == 2 && v1 > v0) {                                    /* ids repetidos */
                int dup = 0; for (int i = 0; i < nids; i++) { uint32_t L = ids[i][1] - ids[i][0]; if (L == v1 - v0) { uint32_t k = 0; while (k < L && src[ids[i][0] + k] == src[v0 + k]) k++; if (k == L) dup = 1; } }
                if (dup) msg(&c, at, 1, "id repetido: ", src + v0, v1 - v0, 0);
                else if (nids < 64) { ids[nids][0] = v0; ids[nids][1] = v1; nids++; }
            }
            if (ieq(nm, nl, "img") && !get_attr(src, at_, nat, "alt", 0, 0)) msg(&c, at, 0, "<img> sin alt (descripcion para accesibilidad)", 0, 0, 0);
            const char *ref = 0; const char *refn = 0;
            if (ieq(nm, nl, "img") || (ieq(nm, nl, "script")) || ieq(nm, nl, "source") || ieq(nm, nl, "audio") || ieq(nm, nl, "video")) refn = "src";
            else if (ieq(nm, nl, "link") || ieq(nm, nl, "a")) refn = "href";
            (void)ref;
            if (refn && get_attr(src, at_, nat, refn, &v0, &v1) == 2 && !is_remote(src + v0, v1 - v0)) {
                int linkcss = ieq(nm, nl, "link") ? (get_attr(src, at_, nat, "rel", 0, 0) != 0) : 1;
                if (linkcss && !ieq(nm, nl, "a")) {
                    rep->enlaces++;
                    char rel[300], full[400]; uint32_t L = v1 - v0 < sizeof rel - 1 ? v1 - v0 : sizeof rel - 1; for (uint32_t k = 0; k < L; k++) rel[k] = src[v0 + k]; rel[L] = 0;
                    if (env && env->exists && html_path_join(dir ? dir : "/", rel, full, sizeof full) == 0 && !env->exists(env->ud, full)) msg(&c, at, 1, "no existe el archivo ", src + v0, v1 - v0, 0);
                }
            }
            if (ieq(nm, nl, "script") || ieq(nm, nl, "style")) {                                              /* texto crudo hasta el cierre */
                int is_script = ieq(nm, nl, "script"); uint32_t cs = q, ce = q; int closed = 0;
                while (ce < len) { if (src[ce] == '<' && ce + 1 < len && src[ce + 1] == '/' && ce + 2 + nl <= len && ieq(src + ce + 2, nl, is_script ? "script" : "style")) { closed = 1; break; } ce++; }
                if (!closed) { msg(&c, at, 1, is_script ? "<script> sin cerrar" : "<style> sin cerrar", 0, 0, 0); break; }
                if (is_script && !get_attr(src, at_, nat, "src", 0, 0)) {
                    uint32_t t0, t1; int has_type = get_attr(src, at_, nat, "type", &t0, &t1) == 2;
                    int js = !has_type || ieq(src + t0, t1 - t0, "text/javascript") || ieq(src + t0, t1 - t0, "application/javascript") || t1 == t0;
                    if (ce > cs) {
                        for (uint32_t k = cs; k + 10 < ce; k++) if (src[k] == 'g' && ieq(src + k, 10, "getContext")) { msg(&c, cs + (k - cs), 0, "getContext(): el canvas 2D aun no esta en el navegador de SOMA; usa elementos HTML y CSS (div, svg)", 0, 0, 0); break; }
                    }
                    if (js && ce > cs && env && env->js_check && nscripts < 8) {
                        nscripts++; rep->scripts++;
                        int ln = 1; char m[160]; m[0] = 0;
                        if (env->js_check(env->ud, src + cs, ce - cs, &ln, m, sizeof m) != 0) {
                            uint32_t pos = cs; int want = ln > 1 ? ln - 1 : 0; while (want > 0 && pos < ce) { if (src[pos] == '\n') want--; pos++; }
                            msg(&c, pos, 1, "error en el script: ", m, slen(m), 0);
                        }
                    }
                }
                p = ce; uint32_t e2 = ce; while (e2 < len && src[e2] != '>') e2++; p = e2 < len ? e2 + 1 : len;
                continue;
            }
            if (!sc && !in_list(nm, nl, VOID_TAGS)) {
                if (sp >= DEPTH) { msg(&c, at, 1, "demasiado anidado (mas de 96 niveles)", 0, 0, 0); break; }
                stack[sp].a = a; stack[sp].b = b; pos_stack[sp] = at; sp++;
            }
            p = q; continue;
        }
        p++;
    }
    for (int i = sp - 1; i >= 0; i--)
        if (!in_list(src + stack[i].a, stack[i].b - stack[i].a, OPT_END)) msg(&c, pos_stack[i], 1, "<", src + stack[i].a, stack[i].b - stack[i].a, "> sin cerrar");
    if (!seen_doctype) msg(&c, 0, 0, "falta <!DOCTYPE html> al principio (sin el, el navegador usa el modo antiguo)", 0, 0, 0);
    if (seen_html && !seen_lang) msg(&c, 0, 0, "<html> sin lang (por ejemplo lang=\"es\")", 0, 0, 0);
    if (!seen_title) msg(&c, 0, 0, "falta <title>", 0, 0, 0);
    if (!seen_charset && seen_doctype) msg(&c, 0, 0, "falta <meta charset=\"utf-8\"> (las tildes pueden verse mal)", 0, 0, 0);
    if (uses_canvas) msg(&c, 0, 0, "<canvas>: el dibujo 2D aun no esta en el navegador de SOMA; usa elementos HTML y CSS (div, svg)", 0, 0, 0);
    oend(&o);
    return rep->errores;
}

/* ------------------------------------------------------------ empaquetar */
static void b64(struct ob *o, const uint8_t *d, uint32_t n)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (uint32_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        oc(o, T[v >> 18 & 63]); oc(o, T[v >> 12 & 63]); oc(o, i + 1 < n ? T[v >> 6 & 63] : '='); oc(o, i + 2 < n ? T[v & 63] : '=');
    }
}
static const char *mime_of(const char *path)
{
    uint32_t n = slen(path), e = n; while (e > 0 && path[e - 1] != '.') e--;
    const char *x = path + e; uint32_t xl = n - e;
    static const struct { const char *e, *m; } M[] = { { "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" }, { "gif", "image/gif" },
        { "svg", "image/svg+xml" }, { "bmp", "image/bmp" }, { "webp", "image/webp" }, { "ico", "image/x-icon" }, { 0, 0 } };
    for (int i = 0; M[i].e; i++) if (ieq(x, xl, M[i].e)) return M[i].m;
    return 0;
}
/* copia texto de un script/estilo en linea: "</script" -> "<\/script" para no cerrar la etiqueta antes de tiempo */
static void oraw(struct ob *o, const char *d, uint32_t n, const char *tag)
{
    uint32_t tl = slen(tag);
    for (uint32_t i = 0; i < n; i++) {
        if (d[i] == '<' && i + 1 < n && d[i + 1] == '/' && i + 2 + tl <= n && ieq(d + i + 2, tl, tag)) { oc(o, '<'); oc(o, '\\'); continue; }
        oc(o, d[i]);
    }
}
int html_bundle(const char *src, uint32_t len, const char *dir, const struct html_env *env, char *out, uint32_t cap, int *missing)
{
    struct ob o = { out, 0, cap, 0 };
    int miss = 0; uint32_t p = 0;
    while (p < len) {
        if (src[p] != '<' || p + 1 >= len || !is_alpha((unsigned char)src[p + 1])) {
            if (src[p] == '<' && p + 3 < len && src[p + 1] == '!' && src[p + 2] == '-' && src[p + 3] == '-') {     /* comentario: tal cual */
                uint32_t q = p + 4; while (q + 2 < len && !(src[q] == '-' && src[q + 1] == '-' && src[q + 2] == '>')) q++;
                q = q + 3 < len ? q + 3 : len; on(&o, src + p, q - p); p = q; continue;
            }
            oc(&o, src[p++]); continue;
        }
        uint32_t a = p + 1, b = a; while (b < len && !is_sp(src[b]) && src[b] != '>' && src[b] != '/') b++;
        struct attr at[24]; int nat = 0, sc = 0;
        uint32_t q = read_attrs(src, b, len, at, 24, &nat, &sc);
        if (!q) { on(&o, src + p, len - p); break; }
        const char *nm = src + a; uint32_t nl = b - a; uint32_t v0, v1;
        char full[400], rel[300]; const char *data; uint32_t dl;
        int is_link = ieq(nm, nl, "link"), is_scr = ieq(nm, nl, "script"), is_img = ieq(nm, nl, "img"), is_style = ieq(nm, nl, "style");
        if (is_style) {                                                                                           /* los <style> en linea no se tocan */
            uint32_t ce = q; while (ce < len && !(src[ce] == '<' && ce + 7 < len && src[ce + 1] == '/' && ieq(src + ce + 2, 5, "style"))) ce++;
            uint32_t e2 = ce; while (e2 < len && src[e2] != '>') e2++; e2 = e2 < len ? e2 + 1 : len;
            on(&o, src + p, e2 - p); p = e2; continue;
        }
        if (is_link && get_attr(src, at, nat, "href", &v0, &v1) == 2 && !is_remote(src + v0, v1 - v0)) {
            uint32_t r0, r1; int isss = get_attr(src, at, nat, "rel", &r0, &r1) == 2 && ieq(src + r0, r1 - r0, "stylesheet");
            if (isss) {
                uint32_t L = v1 - v0 < sizeof rel - 1 ? v1 - v0 : sizeof rel - 1; for (uint32_t k = 0; k < L; k++) rel[k] = src[v0 + k]; rel[L] = 0;
                if (env && env->read && html_path_join(dir ? dir : "/", rel, full, sizeof full) == 0 && env->read(env->ud, full, &data, &dl) == 0) {
                    os(&o, "<style>/* "); os(&o, rel); os(&o, " */\n"); oraw(&o, data, dl, "style"); os(&o, "\n</style>");
                    if (env->release) env->release(env->ud, data);
                    p = q; continue;
                }
                miss++;
            }
        } else if (is_scr && get_attr(src, at, nat, "src", &v0, &v1) == 2 && !is_remote(src + v0, v1 - v0)) {
            uint32_t L = v1 - v0 < sizeof rel - 1 ? v1 - v0 : sizeof rel - 1; for (uint32_t k = 0; k < L; k++) rel[k] = src[v0 + k]; rel[L] = 0;
            uint32_t ce = q; while (ce < len && !(src[ce] == '<' && ce + 8 < len && src[ce + 1] == '/' && ieq(src + ce + 2, 6, "script"))) ce++;
            uint32_t e2 = ce; while (e2 < len && src[e2] != '>') e2++; e2 = e2 < len ? e2 + 1 : len;
            if (env && env->read && html_path_join(dir ? dir : "/", rel, full, sizeof full) == 0 && env->read(env->ud, full, &data, &dl) == 0) {
                os(&o, "<script");
                for (int i = 0; i < nat; i++) {                                    /* los demas atributos (type="module"...) se conservan; sin src */
                    if (ieq(src + at[i].na, at[i].nb - at[i].na, "src")) continue;
                    oc(&o, ' '); on(&o, src + at[i].na, at[i].nb - at[i].na);
                    if (at[i].has) { os(&o, "=\""); on(&o, src + at[i].va, at[i].vb - at[i].va); oc(&o, '"'); }
                }
                os(&o, ">/* "); os(&o, rel); os(&o, " */\n"); oraw(&o, data, dl, "script"); os(&o, "\n</script>");
                if (env->release) env->release(env->ud, data);
                p = e2; continue;
            }
            miss++; on(&o, src + p, e2 - p); p = e2; continue;
        } else if (is_img && get_attr(src, at, nat, "src", &v0, &v1) == 2 && !is_remote(src + v0, v1 - v0)) {
            uint32_t L = v1 - v0 < sizeof rel - 1 ? v1 - v0 : sizeof rel - 1; for (uint32_t k = 0; k < L; k++) rel[k] = src[v0 + k]; rel[L] = 0;
            const char *mt = mime_of(rel);
            if (mt && env && env->read && html_path_join(dir ? dir : "/", rel, full, sizeof full) == 0 && env->read(env->ud, full, &data, &dl) == 0) {
                if (dl <= 300u * 1024u) {
                    on(&o, src + p, v0 - p); os(&o, "data:"); os(&o, mt); os(&o, ";base64,"); b64(&o, (const uint8_t *)data, dl); on(&o, src + v1, q - v1);
                    if (env->release) env->release(env->ud, data);
                    p = q; continue;
                }
                if (env->release) env->release(env->ud, data);
            } else miss++;
        }
        on(&o, src + p, q - p); p = q;
    }
    oend(&o);
    if (missing) *missing = miss;
    return o.over ? -1 : (int)o.n;
}

/* ------------------------------------------------------------ plantillas (solo DOM y CSS: el canvas 2D aun no esta en el navegador) */
static const char T_INDEX[] =
"<!DOCTYPE html>\n<html lang=\"es\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"<title>%TITULO%</title>\n<link rel=\"stylesheet\" href=\"estilo.css\">\n</head>\n<body>\n";
static const char T_BASICA_BODY[] =
"<main>\n<h1>%TITULO%</h1>\n<p id=\"saludo\">Hola desde SOMA.</p>\n<button id=\"boton\">Pulsame</button>\n<p id=\"cuenta\"></p>\n</main>\n<script src=\"app.js\"></script>\n</body>\n</html>\n";
static const char T_APP_BODY[] =
"<main>\n<h1>%TITULO%</h1>\n<form id=\"formulario\">\n<input id=\"nueva\" type=\"text\" placeholder=\"Nueva tarea\" autocomplete=\"off\">\n<button type=\"submit\">Anadir</button>\n</form>\n"
"<ul id=\"lista\"></ul>\n<p id=\"resumen\"></p>\n</main>\n<script src=\"app.js\"></script>\n</body>\n</html>\n";
static const char T_JUEGO_BODY[] =
"<main>\n<h1>%TITULO%</h1>\n<p>Atrapa el cuadrado: <b id=\"puntos\">0</b> puntos. Tiempo: <b id=\"tiempo\">30</b> s</p>\n<div id=\"campo\"><div id=\"objetivo\"></div></div>\n"
"<button id=\"empezar\">Empezar</button>\n</main>\n<script src=\"app.js\"></script>\n</body>\n</html>\n";
static const char T_CSS[] =
"* { box-sizing: border-box; }\nbody { margin: 0; font-family: sans-serif; background: #0f172a; color: #e2e8f0; }\n"
"main { max-width: 640px; margin: 0 auto; padding: 24px; }\nh1 { color: #38bdf8; }\n"
"button { background: #0ea5e9; color: #fff; border: 0; border-radius: 6px; padding: 8px 16px; font-size: 16px; cursor: pointer; }\nbutton:hover { background: #0284c7; }\n"
"input { padding: 8px; font-size: 16px; border-radius: 6px; border: 1px solid #475569; background: #1e293b; color: #e2e8f0; width: 70%; }\n"
"ul { list-style: none; padding: 0; }\nli { background: #1e293b; margin: 6px 0; padding: 8px 12px; border-radius: 6px; cursor: pointer; }\nli.hecha { text-decoration: line-through; opacity: 0.6; }\n"
"#campo { position: relative; width: 100%; height: 280px; background: #1e293b; border-radius: 8px; margin: 12px 0; overflow: hidden; }\n"
"#objetivo { position: absolute; width: 40px; height: 40px; background: #f59e0b; border-radius: 6px; left: 20px; top: 20px; cursor: pointer; }\n";
static const char T_BASICA_JS[] =
"var cuenta = 0;\nvar boton = document.getElementById('boton');\nvar texto = document.getElementById('cuenta');\n"
"boton.addEventListener('click', function () {\n  cuenta++;\n  texto.textContent = 'Has pulsado ' + cuenta + (cuenta === 1 ? ' vez' : ' veces');\n});\n";
static const char T_APP_JS[] =
"var tareas = [];\ntry { tareas = JSON.parse(localStorage.getItem('tareas') || '[]'); } catch (e) { tareas = []; }\n"
"var lista = document.getElementById('lista');\nvar resumen = document.getElementById('resumen');\n"
"function guardar() { try { localStorage.setItem('tareas', JSON.stringify(tareas)); } catch (e) {} }\n"
"function pinta() {\n  lista.innerHTML = '';\n  for (var i = 0; i < tareas.length; i++) {\n    var li = document.createElement('li');\n    li.textContent = tareas[i].texto;\n"
"    if (tareas[i].hecha) li.className = 'hecha';\n    li.setAttribute('data-i', i);\n    li.addEventListener('click', function () {\n      var t = tareas[this.getAttribute('data-i')];\n      t.hecha = !t.hecha; guardar(); pinta();\n    });\n    lista.appendChild(li);\n  }\n"
"  var pendientes = tareas.filter(function (t) { return !t.hecha; }).length;\n  resumen.textContent = pendientes + ' pendientes de ' + tareas.length;\n}\n"
"document.getElementById('formulario').addEventListener('submit', function (e) {\n  e.preventDefault();\n  var campo = document.getElementById('nueva');\n  var v = campo.value.trim();\n  if (v) { tareas.push({ texto: v, hecha: false }); campo.value = ''; guardar(); pinta(); }\n});\npinta();\n";
static const char T_JUEGO_JS[] =
"var puntos = 0, tiempo = 30, reloj = null;\nvar campo = document.getElementById('campo');\nvar objetivo = document.getElementById('objetivo');\n"
"function mueve() {\n  objetivo.style.left = Math.floor(Math.random() * (campo.clientWidth - 40 > 0 ? campo.clientWidth - 40 : 200)) + 'px';\n  objetivo.style.top = Math.floor(Math.random() * 240) + 'px';\n}\n"
"objetivo.addEventListener('click', function () {\n  if (reloj === null) return;\n  puntos++;\n  document.getElementById('puntos').textContent = puntos;\n  mueve();\n});\n"
"document.getElementById('empezar').addEventListener('click', function () {\n  if (reloj !== null) clearInterval(reloj);\n  puntos = 0; tiempo = 30;\n  document.getElementById('puntos').textContent = puntos;\n  document.getElementById('tiempo').textContent = tiempo;\n  mueve();\n"
"  reloj = setInterval(function () {\n    tiempo--;\n    document.getElementById('tiempo').textContent = tiempo;\n    if (tiempo <= 0) { clearInterval(reloj); reloj = null; }\n  }, 1000);\n});\n";

const char *html_template_name(int file) { return file == 0 ? "index.html" : file == 1 ? "estilo.css" : file == 2 ? "app.js" : 0; }
int html_template(const char *kind, int file, const char *titulo, char *out, uint32_t cap)
{
    int k = !kind || !*kind || ieq(kind, slen(kind), "basica") ? 0 : ieq(kind, slen(kind), "app") ? 1 : ieq(kind, slen(kind), "juego") ? 2 : -1;
    if (k < 0 || file < 0 || file > 2) return -1;
    const char *parts[2]; int np = 0;
    if (file == 0) { parts[np++] = T_INDEX; parts[np++] = k == 0 ? T_BASICA_BODY : k == 1 ? T_APP_BODY : T_JUEGO_BODY; }
    else if (file == 1) parts[np++] = T_CSS;
    else parts[np++] = k == 0 ? T_BASICA_JS : k == 1 ? T_APP_JS : T_JUEGO_JS;
    struct ob o = { out, 0, cap, 0 };
    for (int i = 0; i < np; i++) {
        for (const char *t = parts[i]; *t; t++) {
            if (t[0] == '%' && t[1] == 'T' && t[2] == 'I' && t[3] == 'T' && t[4] == 'U' && t[5] == 'L' && t[6] == 'O' && t[7] == '%') {
                for (const char *x = titulo && *titulo ? titulo : "Mi aplicacion"; *x; x++) {
                    if (*x == '<') os(&o, "&lt;"); else if (*x == '>') os(&o, "&gt;"); else if (*x == '&') os(&o, "&amp;"); else if (*x == '"') os(&o, "&quot;"); else oc(&o, *x);
                }
                t += 7; continue;
            }
            oc(&o, *t);
        }
    }
    oend(&o);
    return o.over ? -1 : (int)o.n;
}
