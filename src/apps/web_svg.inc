/* SOMA_180: rasterizador SVG del navegador. Solo enteros (coma fija 16.16 en int64), sin dependencias: se incluye desde web_img.c y por
 * tanto corre en el proceso aislado imgdec (x86_64). Pruebas: tests/test_web_img_180.c.
 *
 * Admite: svg, g, a, switch, defs, symbol, use, path (todas las ordenes, arcos incluidos), rect (esquinas redondas), circle, ellipse, line,
 * polyline, polygon; fill, stroke (grosor, extremos; uniones redondeadas), opacidades, fill-rule, transform (matrix, translate, scale,
 * rotate, skewX, skewY), style="" y hojas <style> sencillas (etiqueta, .clase, #id), currentColor, linearGradient y radialGradient
 * (stops, gradientUnits, gradientTransform, href), viewBox y preserveAspectRatio.
 * No admite (se ignoran sin fallar): text, image, clipPath, mask, filter, pattern, marker, guiones de trazo, animaciones SMIL. */

typedef int64_t fx;
#define FX1 65536
#define FXI(v) ((fx)(v) * FX1)
/* todo se satura a +-2^56: un SVG con numeros absurdos da un dibujo absurdo, pero nunca desborda un entero con signo */
#define FXSAT ((fx)1 << 56)
static fx fxsat(fx v) { return v > FXSAT ? FXSAT : v < -FXSAT ? -FXSAT : v; }
static fx fxmul(fx a, fx b) { __int128 r = ((__int128)a * b) >> 16; return r > FXSAT ? FXSAT : r < -FXSAT ? -FXSAT : (fx)r; }
static fx fxdiv(fx a, fx b) { if (!b) return 0; fx lim = (fx)1 << 46; if (a > lim) a = lim; else if (a < -lim) a = -lim; return fxsat((a * FX1) / b); }
static uint64_t sv_isqrt(uint64_t v) { uint64_t r = 0, b = 1ull << 62; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }
static fx fxsqrt(fx v) { return v <= 0 ? 0 : (fx)sv_isqrt((uint64_t)v * FX1); }
static const int32_t sv_atan[16] = { 2949120, 1740967, 919879, 466945, 234379, 117304, 58666, 29335, 14668, 7334, 3667, 1833, 917, 458, 229, 115 };
/* seno y coseno de un angulo en grados (16.16) por CORDIC */
static void sv_sincos(fx deg, fx *s, fx *c)
{
    deg %= FXI(360); if (deg >= FXI(180)) deg -= FXI(360); if (deg < -FXI(180)) deg += FXI(360);
    int neg = 0; if (deg > FXI(90)) { deg -= FXI(180); neg = 1; } else if (deg < -FXI(90)) { deg += FXI(180); neg = 1; }
    fx x = 39797, y = 0, z = deg;
    for (int i = 0; i < 16; i++) { fx nx, ny; if (z >= 0) { nx = x - (y >> i); ny = y + (x >> i); z -= sv_atan[i]; } else { nx = x + (y >> i); ny = y - (x >> i); z += sv_atan[i]; } x = nx; y = ny; }
    if (neg) { x = -x; y = -y; }
    *s = y; *c = x;
}
static fx sv_atan2(fx y, fx x)
{
    if (!x && !y) return 0;
    fx add = 0; if (x < 0) { add = y >= 0 ? FXI(180) : -FXI(180); x = -x; y = -y; }
    while (x > (1ll << 40) || y > (1ll << 40) || y < -(1ll << 40)) { x /= 2; y /= 2; }
    while (x < (1 << 20) && y < (1 << 20) && y > -(1 << 20)) { x *= 16; y *= 16; }
    fx z = 0;
    for (int i = 0; i < 16; i++) { fx nx, ny; if (y > 0) { nx = x + (y >> i); ny = y - (x >> i); z += sv_atan[i]; } else { nx = x - (y >> i); ny = y + (x >> i); z -= sv_atan[i]; } x = nx; y = ny; }
    return z + add;
}
struct smat { fx a, b, c, d, e, f; };
static struct smat sm_mul(struct smat m, struct smat n)      /* primero n, luego m */
{
    struct smat r = { fxmul(m.a, n.a) + fxmul(m.c, n.b), fxmul(m.b, n.a) + fxmul(m.d, n.b), fxmul(m.a, n.c) + fxmul(m.c, n.d), fxmul(m.b, n.c) + fxmul(m.d, n.d),
                      fxmul(m.a, n.e) + fxmul(m.c, n.f) + m.e, fxmul(m.b, n.e) + fxmul(m.d, n.f) + m.f };
    return r;
}
static int sm_inv(struct smat m, struct smat *o)
{
    fx det = fxmul(m.a, m.d) - fxmul(m.b, m.c); if (det > -4 && det < 4) return 0;
    o->a = fxdiv(m.d, det); o->b = -fxdiv(m.b, det); o->c = -fxdiv(m.c, det); o->d = fxdiv(m.a, det);
    o->e = -(fxmul(o->a, m.e) + fxmul(o->c, m.f)); o->f = -(fxmul(o->b, m.e) + fxmul(o->d, m.f));
    return 1;
}

struct sattr { char *k, *v; };
struct snode { char *name; int at, nat, parent, first, last, next; };
struct srule { const char *sel; int sl; const char *decl; int dl; };
struct sedge { int32_t x0, y0, x1, y1; int32_t dir; };
struct sstop { fx off; uint32_t argb; };
struct spaint { int kind; uint32_t rgb; int alpha; int node; };       /* kind: 0 nada, 1 color, 2 degradado (node) */
struct sstyle { struct spaint fill, stroke; fx fill_op, stroke_op, sw, opacity; int cap, evenodd, hidden; uint32_t color; };
struct ssub { int start, n, closed; };
struct svg {
    char *buf; struct snode *nd; int nn, ncap; struct sattr *at; int na, acap;
    struct srule *rules; int nrules;
    uint32_t *px; int W, H; int fail;
    struct sedge *ed; int ne, ecap;
    fx *pts; int np, pcap; struct ssub *sub; int nsub, scap;        /* camino aplanado en coordenadas de usuario */
    fx bx0, by0, bx1, by1; int nodes_drawn;
    int32_t *part, *diff;
};
#define SV_MAX_EDGES 600000
#define SV_MAX_PTS   400000

static int sv_sp(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f'; }
static int sv_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static char *sv_strstr(const char *h, const char *nd) { for (; *h; h++) { int i = 0; while (nd[i] && h[i] == nd[i]) i++; if (!nd[i]) return (char *)(uintptr_t)h; } return 0; }
static const char *sv_strchr(const char *s, int c) { for (; *s; s++) if (*s == (char)c) return s; return 0; }
static int sv_lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
/* sin distinguir mayusculas: el <svg> en linea llega del DOM de HTML, que pasa etiquetas y atributos a minusculas (viewbox, lineargradient) */
static int sv_eq(const char *a, const char *b) { while (*a && sv_lc(*a) == sv_lc(*b)) { a++; b++; } return sv_lc(*a) == sv_lc(*b); }
static int sv_eqn(const char *a, int al, const char *b) { int i = 0; for (; i < al && b[i]; i++) if (sv_lc(a[i]) != sv_lc(b[i])) return 0; return i == al && !b[i]; }

/* ---------------- XML ---------------- */
static int sv_parse(struct svg *S, uint32_t n)
{
    char *p = S->buf, *end = S->buf + n; int cur = -1;
    while (p < end) {
        char *lt = p; while (lt < end && *lt != '<') lt++;
        if (lt >= end) break;
        p = lt + 1;
        if (p + 2 < end && p[0] == '!' && p[1] == '-' && p[2] == '-') { p += 3; while (p + 2 < end && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) p++; p += 3; continue; }
        if (p + 8 < end && !memcmp(p, "![CDATA[", 8)) { p += 8; while (p + 2 < end && !(p[0] == ']' && p[1] == ']' && p[2] == '>')) p++; p += 3; continue; }
        if (p < end && (*p == '?' || *p == '!')) { int dep = 0; while (p < end) { if (*p == '[') dep++; else if (*p == ']') dep--; else if (*p == '>' && dep <= 0) break; p++; } p++; continue; }
        if (p < end && *p == '/') { while (p < end && *p != '>') p++; p++; if (cur >= 0) cur = S->nd[cur].parent; continue; }
        char *nm = p; while (p < end && !sv_sp(*p) && *p != '>' && *p != '/') p++;
        char *nme = p; if (nme >= end || nme == nm) break;
        if (S->nn >= S->ncap) return -1;
        int id = S->nn++; struct snode *N = &S->nd[id];
        N->at = S->na; N->nat = 0; N->parent = cur; N->first = N->last = N->next = -1;
        if (cur >= 0) { if (S->nd[cur].last >= 0) S->nd[S->nd[cur].last].next = id; else S->nd[cur].first = id; S->nd[cur].last = id; }
        int selfc = 0, done = 0; char nmc = *nme;
        if (nmc == '>') done = 1; else if (nmc == '/') selfc = 1;
        p = nme + 1;
        *nme = 0;
        { char *kk = nm; for (char *c = nm; c < nme; c++) if (*c == ':') kk = c + 1; N->name = kk; }
        while (!done && p < end) {
            while (p < end && sv_sp(*p)) p++;
            if (p >= end) break;
            if (*p == '>') { p++; break; }
            if (*p == '/') { selfc = 1; p++; continue; }
            char *k = p; while (p < end && *p != '=' && !sv_sp(*p) && *p != '>' && *p != '/') p++;
            char *ke = p; if (ke >= end) break;
            char kc = *ke; while (p < end && sv_sp(*p)) p++;
            char *v = (char *)"";
            if (p < end && *p == '=') {
                p++; while (p < end && sv_sp(*p)) p++;
                if (p < end && (*p == '"' || *p == '\'')) { char q = *p++; v = p; while (p < end && *p != q) p++; if (p < end) *p++ = 0; }
                else { v = p; while (p < end && !sv_sp(*p) && *p != '>') p++; if (p < end) { if (*p == '>') done = 1; *p++ = 0; } }
                *ke = 0;
            } else { *ke = 0; if (kc == '>') { done = 1; p = ke + 1; } else if (kc == '/') { selfc = 1; p = ke + 1; } else if (p == ke) p++; }
            if (ke > k && S->na < S->acap) { char *kk = k; for (char *c = k; c < ke; c++) if (*c == ':') kk = c + 1; S->at[S->na].k = kk; S->at[S->na].v = v; S->na++; N->nat++; }
        }
        if (selfc) continue;
        if (sv_eq(N->name, "style")) {                 /* el texto de la hoja se guarda como atributo "#text" */
            char *s = p, *e = p; while (e + 1 < end && !(e[0] == '<' && e[1] == '/')) e++;
            if (e + 1 >= end) e = end;
            char *close = e; while (close < end && *close != '>') close++;
            if (e < end) *e = 0;
            while (*s && sv_sp(*s)) s++;
            if (sv_strstr(s, "<![CDATA[") == s) { s += 9; char *z = sv_strstr(s, "]]>"); if (z) *z = 0; }
            if (S->na < S->acap) { S->at[S->na].k = (char *)"#text"; S->at[S->na].v = s; S->na++; N->nat++; }
            p = close < end ? close + 1 : end;
            continue;
        }
        cur = id;
    }
    return S->nn > 0 ? 0 : -1;
}
static const char *sv_attr(const struct svg *S, int n, const char *k)
{
    const struct snode *N = &S->nd[n];
    for (int i = 0; i < N->nat; i++) if (sv_eq(S->at[N->at + i].k, k)) return S->at[N->at + i].v;
    return 0;
}
/* busca "nombre:valor" en una lista de declaraciones CSS */
static int sv_decl(const char *d, int dl, const char *name, const char **v, int *vl)
{
    int i = 0, found = 0;
    while (i < dl) {
        while (i < dl && (sv_sp(d[i]) || d[i] == ';')) i++;
        int k = i; while (i < dl && d[i] != ':' && d[i] != ';') i++;
        int ke = i; while (ke > k && sv_sp(d[ke - 1])) ke--;
        if (i >= dl || d[i] != ':') continue;
        i++; while (i < dl && sv_sp(d[i])) i++;
        int vs = i; while (i < dl && d[i] != ';') i++;
        int ve = i; while (ve > vs && sv_sp(d[ve - 1])) ve--;
        if (ve - vs > 10 && !memcmp(d + ve - 10, "!important", 10)) { ve -= 10; while (ve > vs && sv_sp(d[ve - 1])) ve--; }
        if (sv_eqn(d + k, ke - k, name)) { *v = d + vs; *vl = ve - vs; found = 1; }
    }
    return found;
}
static void sv_css(struct svg *S)
{
    int cap = 0; for (int i = 0; i < S->nn; i++) if (sv_eq(S->nd[i].name, "style")) { const char *t = sv_attr(S, i, "#text"); if (t) for (const char *c = t; *c; c++) if (*c == '{' || *c == ',') cap++; }
    if (!cap) return;
    S->rules = (struct srule *)kmalloc((size_t)(cap + 1) * sizeof(struct srule)); if (!S->rules) return;
    for (int i = 0; i < S->nn; i++) {
        if (!sv_eq(S->nd[i].name, "style")) continue;
        const char *t = sv_attr(S, i, "#text"); if (!t) continue;
        const char *p = t;
        while (*p) {
            if (p[0] == '/' && p[1] == '*') { p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++; if (*p) p += 2; continue; }
            const char *ss = p; while (*p && *p != '{' && *p != '}') p++;
            if (*p != '{') { if (*p) p++; continue; }
            const char *se = p; p++;
            const char *ds = p; int dep = 1; while (*p && dep) { if (*p == '{') dep++; else if (*p == '}') dep--; if (dep) p++; }
            const char *de = p; if (*p) p++;
            if (ss < se && *ss == '@') continue;
            const char *c = ss;
            while (c < se) {
                const char *e = c; while (e < se && *e != ',') e++;
                const char *a = c, *b = e; while (a < b && sv_sp(*a)) a++; while (b > a && sv_sp(b[-1])) b--;
                if (b > a && S->nrules < cap) { struct srule *R = &S->rules[S->nrules++]; R->sel = a; R->sl = (int)(b - a); R->decl = ds; R->dl = (int)(de - ds); }
                c = e + 1;
            }
        }
    }
}
static int sv_has_class(const char *cls, const char *c, int cl)
{
    if (!cls) return 0;
    while (*cls) { while (sv_sp(*cls)) cls++; const char *s = cls; while (*cls && !sv_sp(*cls)) cls++; if (cls - s == cl && !memcmp(s, c, (size_t)cl)) return 1; }
    return 0;
}
static int sv_sel_match(const struct svg *S, int n, const char *sel, int sl)
{
    int st = sl; while (st > 0 && !sv_sp(sel[st - 1]) && sel[st - 1] != '>') st--;        /* solo el ultimo compuesto */
    const char *p = sel + st, *e = sel + sl; if (p >= e) return 0;
    while (p < e) {
        if (*p == '*') { p++; continue; }
        char kind = (*p == '.' || *p == '#') ? *p++ : 't';
        const char *s = p; while (p < e && *p != '.' && *p != '#' && *p != ':' && *p != '[') p++;
        int l = (int)(p - s);
        if (kind == 't') { if (!sv_eqn(s, l, S->nd[n].name)) return 0; }
        else if (kind == '.') { if (!sv_has_class(sv_attr(S, n, "class"), s, l)) return 0; }
        else { const char *id = sv_attr(S, n, "id"); if (!id || (int)sv_strlen(id) != l || memcmp(id, s, (size_t)l)) return 0; }
        if (p < e && (*p == ':' || *p == '[')) return 0;
    }
    return 1;
}
/* propiedad de presentacion: style="" > hoja de estilos > atributo */
static int sv_prop(const struct svg *S, int n, const char *name, const char **v, int *vl)
{
    const char *st = sv_attr(S, n, "style");
    if (st && sv_decl(st, (int)sv_strlen(st), name, v, vl)) return 1;
    int found = 0;
    for (int i = 0; i < S->nrules; i++) if (sv_sel_match(S, n, S->rules[i].sel, S->rules[i].sl) && sv_decl(S->rules[i].decl, S->rules[i].dl, name, v, vl)) found = 1;
    if (found) return 1;
    const char *a = sv_attr(S, n, name); if (a) { while (sv_sp(*a)) a++; *v = a; *vl = (int)sv_strlen(a); while (*vl > 0 && sv_sp(a[*vl - 1])) (*vl)--; return 1; }
    return 0;
}

/* ---------------- numeros, colores ---------------- */
static fx sv_num(const char **pp, const char *e, int *ok)
{
    const char *p = *pp; while (p < e && (sv_sp(*p) || *p == ',')) p++;
    int neg = 0, any = 0; if (p < e && (*p == '-' || *p == '+')) { neg = *p == '-'; p++; }
    uint64_t ip = 0; while (p < e && *p >= '0' && *p <= '9') { if (ip < (1ull << 22)) ip = ip * 10 + (uint64_t)(*p - '0'); p++; any = 1; }
    uint64_t fr = 0, sc = 1; if (p < e && *p == '.') { p++; while (p < e && *p >= '0' && *p <= '9') { if (sc < 100000000ull) { fr = fr * 10 + (uint64_t)(*p - '0'); sc *= 10; } p++; any = 1; } }
    if (!any) { if (ok) *ok = 0; *pp = p; return 0; }
    fx v = (fx)(ip * FX1 + (fr * FX1 + sc / 2) / sc);
    if (p < e && (*p == 'e' || *p == 'E') && p + 1 < e && ((p[1] >= '0' && p[1] <= '9') || ((p[1] == '-' || p[1] == '+') && p + 2 < e && p[2] >= '0' && p[2] <= '9'))) {
        p++; int en = 0; if (*p == '-' || *p == '+') { en = *p == '-'; p++; }
        int ex = 0; while (p < e && *p >= '0' && *p <= '9') { if (ex < 100) ex = ex * 10 + (*p - '0'); p++; }
        for (int i = 0; i < ex && i < 12; i++) v = en ? v / 10 : (v < (1ll << 40) ? v * 10 : v);
    }
    if (ok) *ok = 1;
    *pp = p; return neg ? -v : v;
}
static fx sv_len(const char *s, int l, fx rel, int *ok)
{
    const char *p = s, *e = s + l; int o = 0; fx v = sv_num(&p, e, &o);
    if (ok) *ok = o;
    if (!o) return 0;
    if (p < e && *p == '%') return fxmul(v, rel) / 100;
    if (p + 1 < e) {
        if (p[0] == 'p' && p[1] == 't') return v * 4 / 3;
        if (p[0] == 'p' && p[1] == 'c') return v * 16;
        if (p[0] == 'm' && p[1] == 'm') return v * 37795 / 10000;
        if (p[0] == 'c' && p[1] == 'm') return v * 37795 / 1000;
        if (p[0] == 'i' && p[1] == 'n') return v * 96;
        if (p[0] == 'e' && p[1] == 'm') return v * 16;
        if (p[0] == 'e' && p[1] == 'x') return v * 8;
        if (p[0] == 'r' && p[1] == 'e') return v * 16;
    }
    return v;
}
static fx sv_alen(const struct svg *S, int n, const char *k, fx rel, fx def)
{
    const char *v; int vl, ok = 0; if (!sv_prop(S, n, k, &v, &vl)) return def;
    fx r = sv_len(v, vl, rel, &ok); return ok ? r : def;
}
static const struct { const char *n; uint32_t c; } sv_named[] = {
    { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 }, { "blue", 0x0000FF }, { "yellow", 0xFFFF00 }, { "gray", 0x808080 }, { "grey", 0x808080 },
    { "silver", 0xC0C0C0 }, { "maroon", 0x800000 }, { "purple", 0x800080 }, { "fuchsia", 0xFF00FF }, { "magenta", 0xFF00FF }, { "lime", 0x00FF00 }, { "olive", 0x808000 }, { "navy", 0x000080 },
    { "teal", 0x008080 }, { "aqua", 0x00FFFF }, { "cyan", 0x00FFFF }, { "orange", 0xFFA500 }, { "pink", 0xFFC0CB }, { "brown", 0xA52A2A }, { "gold", 0xFFD700 }, { "indigo", 0x4B0082 },
    { "violet", 0xEE82EE }, { "darkgray", 0xA9A9A9 }, { "darkgrey", 0xA9A9A9 }, { "lightgray", 0xD3D3D3 }, { "lightgrey", 0xD3D3D3 }, { "dimgray", 0x696969 }, { "darkblue", 0x00008B },
    { "darkgreen", 0x006400 }, { "darkred", 0x8B0000 }, { "crimson", 0xDC143C }, { "tomato", 0xFF6347 }, { "coral", 0xFF7F50 }, { "salmon", 0xFA8072 }, { "khaki", 0xF0E68C }, { "beige", 0xF5F5DC },
    { "ivory", 0xFFFFF0 }, { "skyblue", 0x87CEEB }, { "steelblue", 0x4682B4 }, { "royalblue", 0x4169E1 }, { "dodgerblue", 0x1E90FF }, { "turquoise", 0x40E0D0 }, { "limegreen", 0x32CD32 },
    { "seagreen", 0x2E8B57 }, { "forestgreen", 0x228B22 }, { "orangered", 0xFF4500 }, { "firebrick", 0xB22222 }, { "slategray", 0x708090 }, { "whitesmoke", 0xF5F5F5 }, { "gainsboro", 0xDCDCDC },
    { "midnightblue", 0x191970 }, { "hotpink", 0xFF69B4 }, { "deeppink", 0xFF1493 }, { "chocolate", 0xD2691E }, { "tan", 0xD2B48C }, { "plum", 0xDDA0DD }, { "orchid", 0xDA70D6 }, { "lavender", 0xE6E6FA } };
static int sv_hex(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
/* 1 = color (rgb, alpha 0..255), 0 = none/transparent, -1 = no se entiende, 2 = currentColor, 3 = url(#id) (id en *ref, *rl) */
static int sv_color(const char *s, int l, uint32_t *rgb, int *alpha, const char **ref, int *rl)
{
    while (l > 0 && sv_sp(*s)) { s++; l--; }
    *alpha = 255;
    if (l >= 4 && !memcmp(s, "url(", 4)) { const char *p = s + 4, *e = s + l; while (p < e && (sv_sp(*p) || *p == '"' || *p == '\'' || *p == '#')) p++; const char *q = p; while (q < e && *q != ')' && *q != '"' && *q != '\'' && !sv_sp(*q)) q++; *ref = p; *rl = (int)(q - p); return 3; }
    if (l && s[0] == '#') {
        int h[8], n = 0; for (int i = 1; i < l && n < 8; i++) { int v = sv_hex(s[i]); if (v < 0) break; h[n++] = v; }
        if (n == 3 || n == 4) { *rgb = (uint32_t)((h[0] * 17) << 16 | (h[1] * 17) << 8 | (h[2] * 17)); if (n == 4) *alpha = h[3] * 17; return 1; }
        if (n == 6 || n == 8) { *rgb = (uint32_t)((h[0] * 16 + h[1]) << 16 | (h[2] * 16 + h[3]) << 8 | (h[4] * 16 + h[5])); if (n == 8) *alpha = h[6] * 16 + h[7]; return 1; }
        return -1;
    }
    if (l >= 4 && sv_lc(s[0]) == 'r' && sv_lc(s[1]) == 'g' && sv_lc(s[2]) == 'b') {
        const char *p = s + 3, *e = s + l; if (p < e && sv_lc(*p) == 'a') p++; if (p < e && *p == '(') p++;
        int c[3] = { 0, 0, 0 };
        for (int i = 0; i < 3; i++) { int ok; fx v = sv_num(&p, e, &ok); if (!ok) return -1; if (p < e && *p == '%') { v = v * 255 / 100; p++; } int iv = (int)((v + FX1 / 2) / FX1); c[i] = iv < 0 ? 0 : iv > 255 ? 255 : iv; }
        while (p < e && (sv_sp(*p) || *p == ',' || *p == '/')) p++;
        if (p < e && *p != ')') { int ok; fx a = sv_num(&p, e, &ok); if (ok) { if (p < e && *p == '%') a /= 100; int ia = (int)(a * 255 / FX1); *alpha = ia < 0 ? 0 : ia > 255 ? 255 : ia; } }
        *rgb = (uint32_t)(c[0] << 16 | c[1] << 8 | c[2]); return 1;
    }
    if (sv_eqn(s, l, "none") || sv_eqn(s, l, "transparent")) return 0;
    if (sv_eqn(s, l, "currentColor")) return 2;
    for (unsigned i = 0; i < sizeof sv_named / sizeof sv_named[0]; i++) if (sv_eqn(s, l, sv_named[i].n)) { *rgb = sv_named[i].c; return 1; }
    return -1;
}
static int sv_find_id(const struct svg *S, const char *id, int l)
{
    if (l <= 0) return -1;
    for (int i = 0; i < S->nn; i++) { const char *v = sv_attr(S, i, "id"); if (v && (int)sv_strlen(v) == l && !memcmp(v, id, (size_t)l)) return i; }
    return -1;
}
static void sv_paint(const struct svg *S, int n, const char *name, struct spaint *P, uint32_t cur)
{
    const char *v; int vl; if (!sv_prop(S, n, name, &v, &vl)) return;
    uint32_t rgb = 0; int a = 255, rl = 0; const char *ref = 0;
    int k = sv_color(v, vl, &rgb, &a, &ref, &rl);
    if (k == 0) { P->kind = 0; }
    else if (k == 1) { P->kind = 1; P->rgb = rgb; P->alpha = a; }
    else if (k == 2) { P->kind = 1; P->rgb = cur; P->alpha = 255; }
    else if (k == 3) {
        int g = sv_find_id(S, ref, rl);
        if (g >= 0 && (sv_eq(S->nd[g].name, "linearGradient") || sv_eq(S->nd[g].name, "radialGradient"))) { P->kind = 2; P->node = g; P->alpha = 255; }
        else { const char *p = ref + rl; const char *e = v + vl; while (p < e && *p != ')') p++; if (p < e) p++; while (p < e && sv_sp(*p)) p++;      /* color de reserva tras url() */
               if (p < e) { k = sv_color(p, (int)(e - p), &rgb, &a, &ref, &rl); if (k == 1) { P->kind = 1; P->rgb = rgb; P->alpha = a; } else if (k == 2) { P->kind = 1; P->rgb = cur; P->alpha = 255; } else P->kind = 0; } else P->kind = 0; }
    }
}
static fx sv_opac(const struct svg *S, int n, const char *name, fx def)
{
    const char *v; int vl; if (!sv_prop(S, n, name, &v, &vl)) return def;
    const char *p = v; int ok; fx r = sv_num(&p, v + vl, &ok); if (!ok) return def;
    if (p < v + vl && *p == '%') r /= 100;
    return r < 0 ? 0 : r > FX1 ? FX1 : r;
}
static struct smat sv_transform(const char *s, int l)
{
    struct smat m = { FX1, 0, 0, FX1, 0, 0 }; const char *p = s, *e = s + l;
    while (p < e) {
        while (p < e && (sv_sp(*p) || *p == ',')) p++;
        const char *k = p; while (p < e && *p != '(' ) p++;
        if (p >= e) break;
        int kl = (int)(p - k); while (kl > 0 && sv_sp(k[kl - 1])) kl--;
        p++; fx a[6]; int na = 0;
        while (p < e && *p != ')') { int ok; fx v = sv_num(&p, e, &ok); if (!ok) { while (p < e && *p != ')') p++; break; } if (na < 6) a[na++] = v; while (p < e && (sv_sp(*p) || *p == ',')) p++;
            if (p + 2 < e && p[0] == 'd' && p[1] == 'e' && p[2] == 'g') p += 3; }
        if (p < e) p++;
        struct smat t = { FX1, 0, 0, FX1, 0, 0 };
        if (sv_eqn(k, kl, "matrix") && na == 6) { t.a = a[0]; t.b = a[1]; t.c = a[2]; t.d = a[3]; t.e = a[4]; t.f = a[5]; }
        else if (sv_eqn(k, kl, "translate") && na >= 1) { t.e = a[0]; t.f = na > 1 ? a[1] : 0; }
        else if (sv_eqn(k, kl, "scale") && na >= 1) { t.a = a[0]; t.d = na > 1 ? a[1] : a[0]; }
        else if (sv_eqn(k, kl, "rotate") && na >= 1) { fx sn, cs; sv_sincos(a[0], &sn, &cs); t.a = cs; t.b = sn; t.c = -sn; t.d = cs;
            if (na >= 3) { struct smat t1 = { FX1, 0, 0, FX1, a[1], a[2] }, t2 = { FX1, 0, 0, FX1, -a[1], -a[2] }; t = sm_mul(sm_mul(t1, t), t2); } }
        else if (sv_eqn(k, kl, "skewX") && na >= 1) { fx sn, cs; sv_sincos(a[0], &sn, &cs); t.c = cs ? fxdiv(sn, cs) : 0; }
        else if (sv_eqn(k, kl, "skewY") && na >= 1) { fx sn, cs; sv_sincos(a[0], &sn, &cs); t.b = cs ? fxdiv(sn, cs) : 0; }
        m = sm_mul(m, t);
    }
    return m;
}

/* ---------------- caminos ---------------- */
static void sv_begin(struct svg *S) { S->np = 0; S->nsub = 0; S->bx0 = S->by0 = (fx)1 << 60; S->bx1 = S->by1 = -((fx)1 << 60); }
static void sv_pt(struct svg *S, fx x, fx y)
{
    if (S->np >= S->pcap) {
        int nc = S->pcap ? S->pcap * 2 : 1024; if (nc > SV_MAX_PTS) { S->fail = 1; return; }
        fx *np = (fx *)kmalloc((size_t)nc * 2 * sizeof(fx)); if (!np) { S->fail = 1; return; }
        if (S->pts) { memcpy(np, S->pts, (size_t)S->np * 2 * sizeof(fx)); kfree(S->pts); }
        S->pts = np; S->pcap = nc;
    }
    S->pts[S->np * 2] = x; S->pts[S->np * 2 + 1] = y; S->np++;
    if (S->nsub) S->sub[S->nsub - 1].n++;
    if (x < S->bx0) S->bx0 = x;
    if (x > S->bx1) S->bx1 = x;
    if (y < S->by0) S->by0 = y;
    if (y > S->by1) S->by1 = y;
}
static void sv_move(struct svg *S, fx x, fx y)
{
    if (S->nsub >= S->scap) {
        int nc = S->scap ? S->scap * 2 : 64; struct ssub *ns = (struct ssub *)kmalloc((size_t)nc * sizeof(struct ssub)); if (!ns) { S->fail = 1; return; }
        if (S->sub) { memcpy(ns, S->sub, (size_t)S->nsub * sizeof(struct ssub)); kfree(S->sub); }
        S->sub = ns; S->scap = nc;
    }
    S->sub[S->nsub].start = S->np; S->sub[S->nsub].n = 0; S->sub[S->nsub].closed = 0; S->nsub++;
    sv_pt(S, x, y);
}
static void sv_cubic(struct svg *S, fx res, fx x0, fx y0, fx x1, fx y1, fx x2, fx y2, fx x3, fx y3)
{
    fx dx = (x1 > x0 ? x1 - x0 : x0 - x1) + (x2 > x1 ? x2 - x1 : x1 - x2) + (x3 > x2 ? x3 - x2 : x2 - x3), dy = (y1 > y0 ? y1 - y0 : y0 - y1) + (y2 > y1 ? y2 - y1 : y1 - y2) + (y3 > y2 ? y3 - y2 : y2 - y3);
    fx lenpx = fxmul(dx + dy, res) / FX1; int n = (int)sv_isqrt((uint64_t)(lenpx < 0 ? 0 : lenpx)) * 2 + 3; if (n > 120) n = 120;
    for (int i = 1; i <= n; i++) {
        fx t = (fx)i * FX1 / n, u = FX1 - t, uu = fxmul(u, u), tt = fxmul(t, t);
        fx b0 = fxmul(uu, u), b1 = 3 * fxmul(uu, t), b2 = 3 * fxmul(u, tt), b3 = fxmul(tt, t);
        if (i == n) sv_pt(S, x3, y3); else sv_pt(S, fxmul(b0, x0) + fxmul(b1, x1) + fxmul(b2, x2) + fxmul(b3, x3), fxmul(b0, y0) + fxmul(b1, y1) + fxmul(b2, y2) + fxmul(b3, y3));
    }
}
static int sv_circ_n(fx r_user, fx res) { fx rpx = fxmul(r_user, res) / FX1; int n = 7 * (int)sv_isqrt((uint64_t)(rpx < 0 ? 0 : rpx)) + 10; return n > 400 ? 400 : n; }
/* arco eliptico de (x1,y1) a (x2,y2) */
static void sv_arc(struct svg *S, fx res, fx x1, fx y1, fx rx, fx ry, fx phi, int fa, int fs, fx x2, fx y2)
{
    if (rx < 0) rx = -rx;
    if (ry < 0) ry = -ry;
    if (rx < 4 || ry < 4 || (x1 == x2 && y1 == y2)) { sv_pt(S, x2, y2); return; }
    fx sp, cp; sv_sincos(phi, &sp, &cp);
    fx dx2 = (x1 - x2) / 2, dy2 = (y1 - y2) / 2;
    fx x1p = fxmul(cp, dx2) + fxmul(sp, dy2), y1p = -fxmul(sp, dx2) + fxmul(cp, dy2);
    fx ux = fxdiv(x1p, rx), uy = fxdiv(y1p, ry);
    fx lam = fxmul(ux, ux) + fxmul(uy, uy), cux = 0, cuy = 0;
    if (lam > FX1) { fx s = fxsqrt(lam); rx = fxmul(rx, s); ry = fxmul(ry, s); ux = fxdiv(ux, s); uy = fxdiv(uy, s); }
    else if (lam > 0) { fx k = fxsqrt(fxdiv(FX1 - lam, lam)); if (fa == fs) k = -k; cux = fxmul(k, uy); cuy = -fxmul(k, ux); }
    fx th1 = sv_atan2(uy - cuy, ux - cux), th2 = sv_atan2(-uy - cuy, -ux - cux), dth = th2 - th1;
    if (!fs && dth > 0) dth -= FXI(360); else if (fs && dth < 0) dth += FXI(360);
    fx ad = dth < 0 ? -dth : dth; int full = sv_circ_n(rx > ry ? rx : ry, res), n = (int)(ad * full / FXI(360)) + 2;
    fx mx = (x1 + x2) / 2, my = (y1 + y2) / 2;
    for (int i = 1; i < n; i++) {
        fx sn, cs; sv_sincos(th1 + dth * i / n, &sn, &cs);
        fx ex = fxmul(rx, cux + cs), ey = fxmul(ry, cuy + sn);
        sv_pt(S, fxmul(cp, ex) - fxmul(sp, ey) + mx, fxmul(sp, ex) + fxmul(cp, ey) + my);
    }
    sv_pt(S, x2, y2);
}
static void sv_ellipse(struct svg *S, fx res, fx cx, fx cy, fx rx, fx ry)
{
    if (rx <= 0 || ry <= 0) return;
    int n = sv_circ_n(rx > ry ? rx : ry, res);
    sv_move(S, cx + rx, cy);
    for (int i = 1; i < n; i++) { fx sn, cs; sv_sincos(FXI(360) * i / n, &sn, &cs); sv_pt(S, cx + fxmul(rx, cs), cy + fxmul(ry, sn)); }
    if (S->nsub) S->sub[S->nsub - 1].closed = 1;
}
static void sv_path(struct svg *S, fx res, const char *d)
{
    const char *p = d, *e = d + sv_strlen(d); char cmd = 0, prev = 0; fx cx = 0, cy = 0, sx = 0, sy = 0, lx = 0, ly = 0; int open = 0, guard = 0;
    while (p < e && !S->fail && ++guard < 2000000) {
        while (p < e && (sv_sp(*p) || *p == ',')) p++;
        if (p >= e) break;
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) { cmd = *p++; if (cmd == 'e' || cmd == 'E') break; }
        else if (!cmd) break;
        else if (cmd == 'M') cmd = 'L'; else if (cmd == 'm') cmd = 'l';
        int rel = cmd >= 'a', C = rel ? cmd - 32 : cmd, ok = 1; fx a[7];
        if (C == 'Z') { if (open && S->nsub) { S->sub[S->nsub - 1].closed = 1; } cx = sx; cy = sy; open = 0; prev = 'Z'; cmd = 0;
            while (p < e && (sv_sp(*p) || *p == ',')) p++;
            if (p < e && !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) break;
            continue; }
        int need = C == 'M' || C == 'L' || C == 'T' ? 2 : C == 'H' || C == 'V' ? 1 : C == 'C' ? 6 : C == 'S' || C == 'Q' ? 4 : C == 'A' ? 7 : 0;
        if (!need) break;
        for (int i = 0; i < need && ok; i++) {
            if (C == 'A' && (i == 3 || i == 4)) { while (p < e && (sv_sp(*p) || *p == ',')) p++; if (p < e && (*p == '0' || *p == '1')) a[i] = *p++ - '0'; else ok = 0; }
            else a[i] = sv_num(&p, e, &ok);
        }
        if (!ok) break;
        fx ox = rel ? cx : 0, oy = rel ? cy : 0;
        if (C != 'M' && !open) { sv_move(S, cx, cy); open = 1; }
        switch (C) {
        case 'M': cx = a[0] + ox; cy = a[1] + oy; sx = cx; sy = cy; sv_move(S, cx, cy); open = 1; break;
        case 'L': cx = a[0] + ox; cy = a[1] + oy; sv_pt(S, cx, cy); break;
        case 'H': cx = a[0] + ox; sv_pt(S, cx, cy); break;
        case 'V': cy = a[0] + oy; sv_pt(S, cx, cy); break;
        case 'C': sv_cubic(S, res, cx, cy, a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy, a[4] + ox, a[5] + oy); lx = a[2] + ox; ly = a[3] + oy; cx = a[4] + ox; cy = a[5] + oy; break;
        case 'S': { fx c1x = (prev == 'C' || prev == 'S') ? 2 * cx - lx : cx, c1y = (prev == 'C' || prev == 'S') ? 2 * cy - ly : cy;
            sv_cubic(S, res, cx, cy, c1x, c1y, a[0] + ox, a[1] + oy, a[2] + ox, a[3] + oy); lx = a[0] + ox; ly = a[1] + oy; cx = a[2] + ox; cy = a[3] + oy; break; }
        case 'Q': case 'T': { fx qx, qy, ex, ey;
            if (C == 'Q') { qx = a[0] + ox; qy = a[1] + oy; ex = a[2] + ox; ey = a[3] + oy; } else { qx = (prev == 'Q' || prev == 'T') ? 2 * cx - lx : cx; qy = (prev == 'Q' || prev == 'T') ? 2 * cy - ly : cy; ex = a[0] + ox; ey = a[1] + oy; }
            sv_cubic(S, res, cx, cy, cx + (qx - cx) * 2 / 3, cy + (qy - cy) * 2 / 3, ex + (qx - ex) * 2 / 3, ey + (qy - ey) * 2 / 3, ex, ey); lx = qx; ly = qy; cx = ex; cy = ey; break; }
        case 'A': sv_arc(S, res, cx, cy, a[0], a[1], a[2], a[3] != 0, a[4] != 0, a[5] + ox, a[6] + oy); cx = a[5] + ox; cy = a[6] + oy; break;
        default: break;
        }
        prev = (char)C;
    }
}

/* ---------------- trama ---------------- */
static void sv_edge(struct svg *S, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (y0 == y1) return;
    if (S->ne >= S->ecap) {
        int nc = S->ecap ? S->ecap * 2 : 2048; if (nc > SV_MAX_EDGES) { S->fail = 1; return; }
        struct sedge *ne = (struct sedge *)kmalloc((size_t)nc * sizeof(struct sedge)); if (!ne) { S->fail = 1; return; }
        if (S->ed) { memcpy(ne, S->ed, (size_t)S->ne * sizeof(struct sedge)); kfree(S->ed); }
        S->ed = ne; S->ecap = nc;
    }
    struct sedge *E = &S->ed[S->ne++];
    if (y0 < y1) { E->x0 = x0; E->y0 = y0; E->x1 = x1; E->y1 = y1; E->dir = 1; } else { E->x0 = x1; E->y0 = y1; E->x1 = x0; E->y1 = y0; E->dir = -1; }
}
static int32_t sv_dev(fx v) { fx r = v / 256; return r > 0x3FFFFFFF ? 0x3FFFFFFF : r < -0x3FFFFFFF ? -0x3FFFFFFF : (int32_t)r; }     /* 16.16 -> 24.8 */
static void sv_xf(const struct smat *m, fx x, fx y, int32_t *dx, int32_t *dy) { *dx = sv_dev(fxmul(m->a, x) + fxmul(m->c, y) + m->e); *dy = sv_dev(fxmul(m->b, x) + fxmul(m->d, y) + m->f); }
static void sv_fill_edges(struct svg *S, const struct smat *m)
{
    for (int s = 0; s < S->nsub; s++) {
        const struct ssub *U = &S->sub[s]; if (U->n < 2) continue;
        int32_t fx0, fy0, px, py; sv_xf(m, S->pts[U->start * 2], S->pts[U->start * 2 + 1], &fx0, &fy0); px = fx0; py = fy0;
        for (int i = 1; i < U->n; i++) { int32_t x, y; sv_xf(m, S->pts[(U->start + i) * 2], S->pts[(U->start + i) * 2 + 1], &x, &y); sv_edge(S, px, py, x, y); px = x; py = y; }
        sv_edge(S, px, py, fx0, fy0);
    }
}
static void sv_disc(struct svg *S, int32_t cx, int32_t cy, int32_t r)
{
    if (r < 96) return;                                           /* menos de 3/8 de pixel de radio: no se nota */
    int n = 4 * (int)sv_isqrt((uint64_t)(r >> 8)) + 8; if (n > 64) n = 64;
    int32_t px = cx + r, py = cy;
    for (int i = 1; i <= n; i++) { fx sn, cs; sv_sincos(FXI(360) * i / n, &sn, &cs); int32_t x = i == n ? cx + r : cx + (int32_t)(((fx)r * cs) / FX1), y = i == n ? cy : cy + (int32_t)(((fx)r * sn) / FX1); sv_edge(S, px, py, x, y); px = x; py = y; }
}
static void sv_stroke_edges(struct svg *S, const struct smat *m, fx sw, int cap)
{
    fx det = fxmul(m->a, m->d) - fxmul(m->b, m->c); if (det < 0) det = -det;
    fx sc = fxsqrt(det); int32_t hw = sv_dev(fxmul(sw, sc)) / 2; if (hw < 64) hw = 64;      /* minimo medio pixel de ancho */
    for (int s = 0; s < S->nsub; s++) {
        const struct ssub *U = &S->sub[s]; int cnt = U->n + (U->closed ? 1 : 0); if (U->n < 1) continue;
        int32_t px = 0, py = 0, drew = 0;
        for (int i = 0; i < cnt; i++) {
            int idx = U->start + (i % U->n); int32_t x, y; sv_xf(m, S->pts[idx * 2], S->pts[idx * 2 + 1], &x, &y);
            if (i > 0 && (x != px || y != py)) {
                int64_t dx = x - px, dy = y - py; int64_t len = (int64_t)sv_isqrt((uint64_t)(dx * dx + dy * dy)); if (len < 1) len = 1;
                int32_t nx = (int32_t)(dy * hw / len), ny = (int32_t)(-dx * hw / len), ax = px, ay = py, bx = x, by = y;
                if (cap == 2 && !U->closed) { int32_t ex = (int32_t)(dx * hw / len), ey = (int32_t)(dy * hw / len); if (i == 1) { ax -= ex; ay -= ey; } if (i == cnt - 1) { bx += ex; by += ey; } }
                sv_edge(S, ax + nx, ay + ny, bx + nx, by + ny); sv_edge(S, bx + nx, by + ny, bx - nx, by - ny); sv_edge(S, bx - nx, by - ny, ax - nx, ay - ny); sv_edge(S, ax - nx, ay - ny, ax + nx, ay + ny);
                drew = 1;
            }
            int interior = U->closed ? 1 : (i > 0 && i < cnt - 1);
            if (i < U->n && (interior || cap == 1)) sv_disc(S, x, y, hw);
            px = x; py = y;
        }
        if (!drew && cap == 1) sv_disc(S, px, py, hw);
    }
}
struct sgrad { int radial; int ns; struct sstop st[24]; struct smat inv; fx x1, y1, x2, y2, r, len2; };
static uint32_t sv_grad_at(const struct sgrad *G, int px, int py)
{
    fx X = FXI(px) + FX1 / 2, Y = FXI(py) + FX1 / 2, gx = fxmul(G->inv.a, X) + fxmul(G->inv.c, Y) + G->inv.e, gy = fxmul(G->inv.b, X) + fxmul(G->inv.d, Y) + G->inv.f, t;
    if (G->radial) { fx dx = gx - G->x1, dy = gy - G->y1; t = G->r > 0 ? fxdiv(fxsqrt(fxmul(dx, dx) + fxmul(dy, dy)), G->r) : FX1; }
    else t = G->len2 > 0 ? fxdiv(fxmul(gx - G->x1, G->x2 - G->x1) + fxmul(gy - G->y1, G->y2 - G->y1), G->len2) : FX1;
    if (t > 2 * FX1) t = 2 * FX1;
    if (t <= G->st[0].off) return G->st[0].argb;
    for (int i = 1; i < G->ns; i++) {
        if (t <= G->st[i].off) {
            fx span = G->st[i].off - G->st[i - 1].off; uint32_t a = G->st[i - 1].argb, b = G->st[i].argb; if (span <= 0) return b;
            uint32_t k = (uint32_t)((t - G->st[i - 1].off) * 256 / span), r = 0;
            for (int sh = 0; sh < 32; sh += 8) { uint32_t ca = (a >> sh) & 255, cb = (b >> sh) & 255; r |= ((ca * (256 - k) + cb * k) >> 8) << sh; }
            return r;
        }
    }
    return G->st[G->ns - 1].argb;
}
static int sv_grad_prep(struct svg *S, int g, const struct smat *ctm, struct sgrad *G, uint32_t cur)
{
    memset(G, 0, sizeof *G); G->radial = sv_eq(S->nd[g].name, "radialGradient");
    int src = g;
    for (int hop = 0; hop < 4; hop++) {                           /* stops propios o heredados por href */
        for (int c = S->nd[src].first; c >= 0 && G->ns < 24; c = S->nd[c].next) {
            if (!sv_eq(S->nd[c].name, "stop")) continue;
            const char *v; int vl, ok = 0; fx off = 0;
            if (sv_prop(S, c, "offset", &v, &vl)) { const char *p = v; off = sv_num(&p, v + vl, &ok); if (ok && p < v + vl && *p == '%') off /= 100; }
            if (off < 0) off = 0;
            if (off > FX1) off = FX1;
            if (G->ns && off < G->st[G->ns - 1].off) off = G->st[G->ns - 1].off;
            uint32_t rgb = 0; int a = 255, rl; const char *ref;
            if (sv_prop(S, c, "stop-color", &v, &vl)) { int k = sv_color(v, vl, &rgb, &a, &ref, &rl); if (k == 2) rgb = cur; else if (k == 0) a = 0; }
            a = (int)(a * sv_opac(S, c, "stop-opacity", FX1) / FX1);
            G->st[G->ns].off = off; G->st[G->ns].argb = ((uint32_t)a << 24) | rgb; G->ns++;
        }
        if (G->ns) break;
        const char *h = sv_attr(S, src, "href"); if (!h || *h != '#') break;
        int nx = sv_find_id(S, h + 1, (int)sv_strlen(h + 1)); if (nx < 0 || nx == src) break;
        src = nx;
    }
    if (!G->ns) return 0;
    int geo = g;                                                  /* la geometria tambien puede venir del referenciado */
    if (!sv_attr(S, g, "x1") && !sv_attr(S, g, "x2") && !sv_attr(S, g, "cx") && !sv_attr(S, g, "r") && !sv_attr(S, g, "y1") && !sv_attr(S, g, "y2") && src != g) geo = src;
    const char *un = sv_attr(S, geo, "gradientUnits"); if (!un) un = sv_attr(S, g, "gradientUnits");
    int user = un && sv_eq(un, "userSpaceOnUse");
    fx bw = S->bx1 - S->bx0, bh = S->by1 - S->by0; if (!user && (bw <= 0 || bh <= 0)) return -1;
    fx relw = user ? FXI(100) : FX1, relh = relw;
    if (G->radial) { G->x1 = sv_alen(S, geo, "cx", relw, relw / 2); G->y1 = sv_alen(S, geo, "cy", relh, relh / 2); G->r = sv_alen(S, geo, "r", relw, relw / 2); }
    else { G->x1 = sv_alen(S, geo, "x1", relw, 0); G->y1 = sv_alen(S, geo, "y1", relh, 0); G->x2 = sv_alen(S, geo, "x2", relw, relw); G->y2 = sv_alen(S, geo, "y2", relh, 0);
           G->len2 = fxmul(G->x2 - G->x1, G->x2 - G->x1) + fxmul(G->y2 - G->y1, G->y2 - G->y1); }
    struct smat m = *ctm;
    if (!user) { struct smat b = { bw, 0, 0, bh, S->bx0, S->by0 }; m = sm_mul(m, b); }
    const char *gt = sv_attr(S, geo, "gradientTransform"); if (!gt) gt = sv_attr(S, g, "gradientTransform");
    if (gt) m = sm_mul(m, sv_transform(gt, (int)sv_strlen(gt)));
    if (!sm_inv(m, &G->inv)) return -1;
    return 1;
}
static void sv_sort_edges(struct sedge *e, int n)
{
    for (int gap = n / 2; gap > 0; gap = gap == 2 ? 1 : gap * 5 / 11)
        for (int i = gap; i < n; i++) { struct sedge t = e[i]; int j = i; while (j >= gap && e[j - gap].y0 > t.y0) { e[j] = e[j - gap]; j -= gap; } e[j] = t; }
}
struct sxing { int32_t x; int32_t dir; };
/* rellena los bordes acumulados: regla de relleno, pintura y opacidad (0..65536) */
static void sv_raster(struct svg *S, int evenodd, const struct spaint *P, const struct sgrad *G, fx opacity)
{
    int n = S->ne; if (!n || S->fail) { S->ne = 0; return; }
    sv_sort_edges(S->ed, n);
    int32_t ymin = S->ed[0].y0, ymax = ymin; for (int i = 0; i < n; i++) if (S->ed[i].y1 > ymax) ymax = S->ed[i].y1;
    int r0 = ymin >> 8, r1 = (ymax + 255) >> 8; if (r0 < 0) r0 = 0;
    if (r1 > S->H) r1 = S->H;
    int SSN = S->H <= 600 ? 8 : 4, W = S->W;
    int *act = (int *)kmalloc((size_t)n * sizeof(int)); struct sxing *xs = (struct sxing *)kmalloc((size_t)(n + 1) * sizeof(struct sxing));
    if (!act || !xs) { if (act) kfree(act); if (xs) kfree(xs); S->fail = 1; S->ne = 0; return; }
    int nact = 0, nexte = 0; uint32_t base_a = (uint32_t)(P->alpha * opacity / FX1); if (base_a > 255) base_a = 255;
    uint32_t cr = (P->rgb >> 16) & 255, cg = (P->rgb >> 8) & 255, cb = P->rgb & 255;
    for (int row = r0; row < r1; row++) {
        int lo = W, hi = -1;
        for (int s = 0; s < SSN; s++) {
            int32_t y = row * 256 + (2 * s + 1) * 256 / (2 * SSN);
            while (nexte < n && S->ed[nexte].y0 <= y) act[nact++] = nexte++;
            int nx = 0;
            for (int i = 0; i < nact; ) {
                const struct sedge *E = &S->ed[act[i]];
                if (E->y1 <= y) { act[i] = act[--nact]; continue; }
                int64_t x = E->x0 + (int64_t)(E->x1 - E->x0) * (y - E->y0) / (E->y1 - E->y0);
                int32_t xi = x < -256 ? -256 : x > (int64_t)W * 256 + 256 ? W * 256 + 256 : (int32_t)x;
                int j = nx++; while (j > 0 && xs[j - 1].x > xi) { xs[j] = xs[j - 1]; j--; }
                xs[j].x = xi; xs[j].dir = E->dir; i++;
            }
            int wind = 0;
            for (int i = 0; i + 1 < nx; i++) {
                wind += evenodd ? 1 : xs[i].dir;
                int in = evenodd ? (wind & 1) : wind != 0; if (!in) continue;
                int32_t xa = xs[i].x, xb = xs[i + 1].x; if (xa < 0) xa = 0;
                if (xb > W * 256) xb = W * 256;
                if (xb <= xa) continue;
                int ia = xa >> 8, ib = xb >> 8;
                if (ia < lo) lo = ia;
                if (ib > hi) hi = ib;
                if (ia == ib) S->part[ia] += xb - xa;
                else { S->part[ia] += 256 - (xa & 255); if (ib < W) S->part[ib] += xb & 255; S->diff[ia + 1] += 256; S->diff[ib] -= 256; }
            }
        }
        if (hi < lo) continue;
        if (hi >= W) hi = W - 1;
        int run = 0; uint32_t *dst = S->px + (size_t)row * W;
        for (int x = lo; x <= hi + 1 && x <= W; x++) {
            run += S->diff[x]; S->diff[x] = 0;
            if (x >= W) break;
            int cov = (S->part[x] + run) / SSN; S->part[x] = 0;
            if (cov <= 0) continue;
            if (cov > 256) cov = 256;
            uint32_t a = base_a, r = cr, g = cg, b = cb;
            if (G) { uint32_t c = sv_grad_at(G, x, row); a = (uint32_t)((c >> 24) * opacity / FX1); r = (c >> 16) & 255; g = (c >> 8) & 255; b = c & 255; }
            a = a * (uint32_t)cov / 256; if (!a) continue;
            uint32_t d = dst[x], da = d >> 24, ia = 255 - a;                                  /* lienzo premultiplicado */
            dst[x] = ((a + da * ia / 255) << 24) | ((r * a / 255 + ((d >> 16) & 255) * ia / 255) << 16) | ((g * a / 255 + ((d >> 8) & 255) * ia / 255) << 8) | (b * a / 255 + (d & 255) * ia / 255);
        }
        for (int x = lo > 0 ? lo : 0; x <= W; x++) { if (x > hi + 2) break; S->diff[x] = 0; }
    }
    for (int x = 0; x <= W; x++) { S->diff[x] = 0; S->part[x] = 0; }
    kfree(act); kfree(xs); S->ne = 0;
}
static void sv_shape_paint(struct svg *S, const struct smat *m, const struct sstyle *st)
{
    if (st->hidden || S->np < 1) return;
    for (int pass = 0; pass < 2 && !S->fail; pass++) {
        const struct spaint *P = pass ? &st->stroke : &st->fill; if (!P->kind) continue;
        if (pass && st->sw <= 0) continue;
        fx op = fxmul(st->opacity, pass ? st->stroke_op : st->fill_op); if (op <= 0) continue;
        struct sgrad G; const struct sgrad *gp = 0; struct spaint solid = *P;
        if (P->kind == 2) { int r = sv_grad_prep(S, P->node, m, &G, st->color); if (r <= 0) continue; if (G.ns == 1) { solid.kind = 1; solid.rgb = G.st[0].argb & 0xFFFFFF; solid.alpha = (int)(G.st[0].argb >> 24); } else gp = &G; }
        S->ne = 0;
        if (pass) sv_stroke_edges(S, m, st->sw, st->cap); else sv_fill_edges(S, m);
        sv_raster(S, pass ? 0 : st->evenodd, &solid, gp, op);
    }
}
static fx sv_res(const struct smat *m) { fx det = fxmul(m->a, m->d) - fxmul(m->b, m->c); if (det < 0) det = -det; return fxsqrt(det); }
static void sv_draw(struct svg *S, int n, struct smat m, struct sstyle st, int depth, fx vw, fx vh);
static void sv_children(struct svg *S, int n, struct smat m, const struct sstyle *st, int depth, fx vw, fx vh)
{
    for (int c = S->nd[n].first; c >= 0 && !S->fail; c = S->nd[c].next) sv_draw(S, c, m, *st, depth + 1, vw, vh);
}
static void sv_draw(struct svg *S, int n, struct smat m, struct sstyle st, int depth, fx vw, fx vh)
{
    const char *nm = S->nd[n].name, *v; int vl;
    if (depth > 24 || ++S->nodes_drawn > 200000) return;
    if (sv_eq(nm, "defs") || sv_eq(nm, "symbol") || sv_eq(nm, "clipPath") || sv_eq(nm, "mask") || sv_eq(nm, "linearGradient") || sv_eq(nm, "radialGradient") || sv_eq(nm, "style") ||
        sv_eq(nm, "title") || sv_eq(nm, "desc") || sv_eq(nm, "metadata") || sv_eq(nm, "pattern") || sv_eq(nm, "filter") || sv_eq(nm, "marker") || sv_eq(nm, "script") || sv_eq(nm, "text") ||
        sv_eq(nm, "foreignObject") || sv_eq(nm, "image")) return;
    if (sv_prop(S, n, "display", &v, &vl) && sv_eqn(v, vl, "none")) return;
    if (sv_prop(S, n, "visibility", &v, &vl)) st.hidden = sv_eqn(v, vl, "hidden") || sv_eqn(v, vl, "collapse");
    if (sv_prop(S, n, "color", &v, &vl)) { uint32_t rgb; int a, rl; const char *ref; if (sv_color(v, vl, &rgb, &a, &ref, &rl) == 1) st.color = rgb; }
    sv_paint(S, n, "fill", &st.fill, st.color); sv_paint(S, n, "stroke", &st.stroke, st.color);
    st.fill_op = sv_opac(S, n, "fill-opacity", st.fill_op); st.stroke_op = sv_opac(S, n, "stroke-opacity", st.stroke_op);
    st.opacity = fxmul(st.opacity, sv_opac(S, n, "opacity", FX1));
    st.sw = sv_alen(S, n, "stroke-width", (vw + vh) / 2, st.sw);
    if (sv_prop(S, n, "stroke-linecap", &v, &vl)) st.cap = sv_eqn(v, vl, "round") ? 1 : sv_eqn(v, vl, "square") ? 2 : 0;
    if (sv_prop(S, n, "fill-rule", &v, &vl)) st.evenodd = sv_eqn(v, vl, "evenodd");
    if (sv_prop(S, n, "transform", &v, &vl)) m = sm_mul(m, sv_transform(v, vl));
    if (st.opacity <= 0) return;
    fx res = sv_res(&m);
    if (sv_eq(nm, "g") || sv_eq(nm, "a") || sv_eq(nm, "switch")) { sv_children(S, n, m, &st, depth, vw, vh); return; }
    if (sv_eq(nm, "svg")) {                                       /* svg anidado: posicion y viewBox propios */
        struct smat t = { FX1, 0, 0, FX1, sv_alen(S, n, "x", vw, 0), sv_alen(S, n, "y", vh, 0) }; m = sm_mul(m, t);
        const char *vb = sv_attr(S, n, "viewBox"); fx w = sv_alen(S, n, "width", vw, vw), h = sv_alen(S, n, "height", vh, vh);
        if (vb) { const char *p = vb, *e = vb + sv_strlen(vb); int o1, o2, o3, o4; fx bx = sv_num(&p, e, &o1), by = sv_num(&p, e, &o2), bw = sv_num(&p, e, &o3), bh = sv_num(&p, e, &o4);
            if (o1 && o2 && o3 && o4 && bw > 0 && bh > 0) { fx sx = fxdiv(w, bw), sy = fxdiv(h, bh), s = sx < sy ? sx : sy; struct smat t2 = { s, 0, 0, s, (w - fxmul(bw, s)) / 2 - fxmul(bx, s), (h - fxmul(bh, s)) / 2 - fxmul(by, s) }; m = sm_mul(m, t2); w = bw; h = bh; } }
        sv_children(S, n, m, &st, depth, w, h); return;
    }
    if (sv_eq(nm, "use")) {
        const char *h = sv_attr(S, n, "href"); if (!h || *h != '#') return;
        int t = sv_find_id(S, h + 1, (int)sv_strlen(h + 1)); if (t < 0 || t == n) return;
        struct smat tr = { FX1, 0, 0, FX1, sv_alen(S, n, "x", vw, 0), sv_alen(S, n, "y", vh, 0) }; m = sm_mul(m, tr);
        if (sv_eq(S->nd[t].name, "symbol") || sv_eq(S->nd[t].name, "svg")) {
            const char *vb = sv_attr(S, t, "viewBox"); fx w = sv_alen(S, n, "width", vw, -1), hh = sv_alen(S, n, "height", vh, -1);
            if (vb) { const char *p = vb, *e = vb + sv_strlen(vb); int o1, o2, o3, o4; fx bx = sv_num(&p, e, &o1), by = sv_num(&p, e, &o2), bw = sv_num(&p, e, &o3), bh = sv_num(&p, e, &o4);
                if (o1 && o2 && o3 && o4 && bw > 0 && bh > 0) { if (w < 0) w = bw; if (hh < 0) hh = bh; fx sx = fxdiv(w, bw), sy = fxdiv(hh, bh), s = sx < sy ? sx : sy; struct smat t2 = { s, 0, 0, s, (w - fxmul(bw, s)) / 2 - fxmul(bx, s), (hh - fxmul(bh, s)) / 2 - fxmul(by, s) }; m = sm_mul(m, t2); } }
            sv_children(S, t, m, &st, depth, vw, vh);
        } else sv_draw(S, t, m, st, depth + 1, vw, vh);
        return;
    }
    sv_begin(S);
    if (sv_eq(nm, "path")) { const char *d = sv_attr(S, n, "d"); if (!d) { if (sv_prop(S, n, "d", &v, &vl)) d = 0; } if (d) sv_path(S, res, d); }
    else if (sv_eq(nm, "rect")) {
        fx x = sv_alen(S, n, "x", vw, 0), y = sv_alen(S, n, "y", vh, 0), w = sv_alen(S, n, "width", vw, 0), h = sv_alen(S, n, "height", vh, 0), rx = sv_alen(S, n, "rx", vw, -1), ry = sv_alen(S, n, "ry", vh, -1);
        if (w <= 0 || h <= 0) return;
        if (rx < 0 && ry < 0) rx = ry = 0; else if (rx < 0) rx = ry; else if (ry < 0) ry = rx;
        if (rx > w / 2) rx = w / 2;
        if (ry > h / 2) ry = h / 2;
        if (rx <= 0 || ry <= 0) { sv_move(S, x, y); sv_pt(S, x + w, y); sv_pt(S, x + w, y + h); sv_pt(S, x, y + h); }
        else { sv_move(S, x + rx, y); sv_pt(S, x + w - rx, y); sv_arc(S, res, x + w - rx, y, rx, ry, 0, 0, 1, x + w, y + ry); sv_pt(S, x + w, y + h - ry); sv_arc(S, res, x + w, y + h - ry, rx, ry, 0, 0, 1, x + w - rx, y + h);
               sv_pt(S, x + rx, y + h); sv_arc(S, res, x + rx, y + h, rx, ry, 0, 0, 1, x, y + h - ry); sv_pt(S, x, y + ry); sv_arc(S, res, x, y + ry, rx, ry, 0, 0, 1, x + rx, y); }
        if (S->nsub) S->sub[S->nsub - 1].closed = 1;
    }
    else if (sv_eq(nm, "circle")) { fx r = sv_alen(S, n, "r", (vw + vh) / 2, 0); sv_ellipse(S, res, sv_alen(S, n, "cx", vw, 0), sv_alen(S, n, "cy", vh, 0), r, r); }
    else if (sv_eq(nm, "ellipse")) sv_ellipse(S, res, sv_alen(S, n, "cx", vw, 0), sv_alen(S, n, "cy", vh, 0), sv_alen(S, n, "rx", vw, 0), sv_alen(S, n, "ry", vh, 0));
    else if (sv_eq(nm, "line")) { sv_move(S, sv_alen(S, n, "x1", vw, 0), sv_alen(S, n, "y1", vh, 0)); sv_pt(S, sv_alen(S, n, "x2", vw, 0), sv_alen(S, n, "y2", vh, 0)); st.fill.kind = 0; }
    else if (sv_eq(nm, "polyline") || sv_eq(nm, "polygon")) {
        const char *p = sv_attr(S, n, "points"); if (!p) return;
        const char *e = p + sv_strlen(p); int first = 1;
        for (;;) { int o1, o2; fx x = sv_num(&p, e, &o1), y = sv_num(&p, e, &o2); if (!o1 || !o2) break; if (first) { sv_move(S, x, y); first = 0; } else sv_pt(S, x, y); if (S->fail) break; }
        if (nm[4] == 'g' && S->nsub) S->sub[S->nsub - 1].closed = 1;
    }
    else { sv_children(S, n, m, &st, depth, vw, vh); return; }      /* elemento desconocido: se tratan sus hijos */
    sv_shape_paint(S, &m, &st);
}

static int svg_render(const uint8_t *d, uint32_t n, struct wimg *o, int dw, int dh, uint32_t color)
{
    if (n < 10 || n > (8u << 20)) return -1;
    struct svg *S = (struct svg *)kmalloc(sizeof *S); if (!S) return -1;
    memset(S, 0, sizeof *S);
    int rc = -1; uint32_t lt = 0, eq = 0;
    for (uint32_t i = 0; i < n; i++) { if (d[i] == '<') lt++; else if (d[i] == '=') eq++; }
    S->ncap = (int)lt + 2; S->acap = (int)eq + (int)lt + 8;
    if (S->ncap > 300000 || S->acap > 2000000) goto out;
    S->buf = (char *)kmalloc(n + 2); S->nd = (struct snode *)kmalloc((size_t)S->ncap * sizeof(struct snode)); S->at = (struct sattr *)kmalloc((size_t)S->acap * sizeof(struct sattr));
    if (!S->buf || !S->nd || !S->at) goto out;
    memcpy(S->buf, d, n); S->buf[n] = 0; S->buf[n + 1] = 0;
    for (uint32_t i = 0; i < n; i++) if (!S->buf[i]) S->buf[i] = ' ';
    if (sv_parse(S, n) != 0) goto out;
    int root = -1; for (int i = 0; i < S->nn; i++) if (sv_eq(S->nd[i].name, "svg")) { root = i; break; }
    if (root < 0) goto out;
    sv_css(S);
    /* tamano natural y viewBox */
    fx bx = 0, by = 0, bw = 0, bh = 0; int hasvb = 0;
    const char *vb = sv_attr(S, root, "viewBox");
    if (vb) { const char *p = vb, *e = vb + sv_strlen(vb); int o1, o2, o3, o4; bx = sv_num(&p, e, &o1); by = sv_num(&p, e, &o2); bw = sv_num(&p, e, &o3); bh = sv_num(&p, e, &o4); hasvb = o1 && o2 && o3 && o4 && bw > 0 && bh > 0; }
    fx nw = -1, nh = -1; const char *a;
    if ((a = sv_attr(S, root, "width")) != 0 && !sv_strchr(a, '%')) { int ok; nw = sv_len(a, (int)sv_strlen(a), 0, &ok); if (!ok || nw <= 0) nw = -1; }
    if ((a = sv_attr(S, root, "height")) != 0 && !sv_strchr(a, '%')) { int ok; nh = sv_len(a, (int)sv_strlen(a), 0, &ok); if (!ok || nh <= 0) nh = -1; }
    if (nw < 0 && nh < 0) { if (hasvb) { nw = bw; nh = bh; } else { nw = FXI(300); nh = FXI(150); } }
    else if (nw < 0) nw = hasvb ? fxmul(nh, fxdiv(bw, bh)) : nh;
    else if (nh < 0) nh = hasvb ? fxmul(nw, fxdiv(bh, bw)) : nw;
    if (!hasvb) { bx = by = 0; bw = nw; bh = nh; }
    int natw = (int)((nw + FX1 / 2) / FX1), nath = (int)((nh + FX1 / 2) / FX1);
    if (natw < 1) natw = 1;
    if (nath < 1) nath = 1;
    if (natw > 16384) natw = 16384;
    if (nath > 16384) nath = 16384;
    int W = dw > 0 && dh > 0 ? dw : natw, H = dw > 0 && dh > 0 ? dh : nath;
    while (W > 2048 || H > 2048 || (uint64_t)W * H > 3000000u) { W = (W + 1) / 2; H = (H + 1) / 2; }
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    S->W = W; S->H = H;
    S->px = (uint32_t *)kmalloc((size_t)W * H * 4); S->part = (int32_t *)kmalloc((size_t)(W + 2) * 4); S->diff = (int32_t *)kmalloc((size_t)(W + 2) * 4);
    if (!S->px || !S->part || !S->diff) goto out;
    memset(S->px, 0, (size_t)W * H * 4); memset(S->part, 0, (size_t)(W + 2) * 4); memset(S->diff, 0, (size_t)(W + 2) * 4);
    if (!hasvb && dw > 0 && dh > 0) { bw = FXI(W); bh = FXI(H); }      /* sin viewBox las unidades son pixeles: no se escala al cambiar la caja */
    fx sx = fxdiv(FXI(W), bw), sy = fxdiv(FXI(H), bh); struct smat m;
    const char *par = sv_attr(S, root, "preserveAspectRatio");
    if (par && sv_strstr(par, "none")) { m.a = sx; m.b = 0; m.c = 0; m.d = sy; m.e = -fxmul(bx, sx); m.f = -fxmul(by, sy); }
    else { fx s = (par && sv_strstr(par, "slice")) ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy); fx ax = 2, ay = 2;        /* cuartos: 0 min, 2 mid, 4 max */
        if (par) { if (sv_strstr(par, "xMin") || sv_strstr(par, "xmin")) ax = 0; else if (sv_strstr(par, "xMax") || sv_strstr(par, "xmax")) ax = 4; if (sv_strstr(par, "YMin") || sv_strstr(par, "ymin")) ay = 0; else if (sv_strstr(par, "YMax") || sv_strstr(par, "ymax")) ay = 4; }
        m.a = s; m.b = 0; m.c = 0; m.d = s; m.e = (FXI(W) - fxmul(bw, s)) * ax / 4 - fxmul(bx, s); m.f = (FXI(H) - fxmul(bh, s)) * ay / 4 - fxmul(by, s); }
    struct sstyle st; memset(&st, 0, sizeof st);
    st.fill.kind = 1; st.fill.rgb = 0; st.fill.alpha = 255; st.fill_op = st.stroke_op = st.opacity = FX1; st.sw = FX1; st.color = color & 0xFFFFFF;
    /* el propio <svg>: sus propiedades heredables y luego los hijos */
    { const char *v; int vl;
      if (sv_prop(S, root, "color", &v, &vl)) { uint32_t rgb; int al, rl; const char *ref; if (sv_color(v, vl, &rgb, &al, &ref, &rl) == 1) st.color = rgb; }
      sv_paint(S, root, "fill", &st.fill, st.color); sv_paint(S, root, "stroke", &st.stroke, st.color);
      st.fill_op = sv_opac(S, root, "fill-opacity", FX1); st.stroke_op = sv_opac(S, root, "stroke-opacity", FX1); st.opacity = sv_opac(S, root, "opacity", FX1);
      st.sw = sv_alen(S, root, "stroke-width", (bw + bh) / 2, FX1);
      if (sv_prop(S, root, "stroke-linecap", &v, &vl)) st.cap = sv_eqn(v, vl, "round") ? 1 : sv_eqn(v, vl, "square") ? 2 : 0;
      if (sv_prop(S, root, "fill-rule", &v, &vl)) st.evenodd = sv_eqn(v, vl, "evenodd"); }
    sv_children(S, root, m, &st, 0, bw, bh);
    if (S->fail && wi_oom) goto out;
    for (size_t i = 0; i < (size_t)W * H; i++) {                  /* quitar la premultiplicacion */
        uint32_t c = S->px[i], al = c >> 24; if (!al) { S->px[i] = 0; continue; }
        if (al < 255) { uint32_t r = ((c >> 16) & 255) * 255 / al, g = ((c >> 8) & 255) * 255 / al, b = (c & 255) * 255 / al; S->px[i] = (al << 24) | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b); }
    }
    o->px = S->px; S->px = 0; o->w = W; o->h = H; o->nw = natw; o->nh = nath; rc = 0;
out:
    if (S->px) kfree(S->px);
    if (S->part) kfree(S->part);
    if (S->diff) kfree(S->diff);
    if (S->ed) kfree(S->ed);
    if (S->pts) kfree(S->pts);
    if (S->sub) kfree(S->sub);
    if (S->rules) kfree(S->rules);
    if (S->nd) kfree(S->nd);
    if (S->at) kfree(S->at);
    if (S->buf) kfree(S->buf);
    kfree(S);
    return rc;
}
