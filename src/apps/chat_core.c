/* SOMA_285_CHAT: historial de conversaciones de la aplicacion de chat (ver chat_core.h). Sin dependencias del nucleo. */
#include "chat_core.h"

static uint32_t slen(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static void mcpy(char *d, const char *s, uint32_t n) { for (uint32_t i = 0; i < n; i++) d[i] = s[i]; }
static char *dup_n(const char *s, uint32_t n)
{
    char *d = (char *)cc_alloc(n + 1); if (!d) return 0;
    if (n) mcpy(d, s, n);
    d[n] = 0; return d;
}

void cc_init(struct cc_hist *h) { h->c = 0; h->n = h->cap = 0; h->sig_id = 1; }
static void msg_libera(struct cc_msg *m) { if (m->txt) cc_free(m->txt); if (m->adj) cc_free(m->adj); m->txt = m->adj = 0; m->n = 0; }
static void conv_libera(struct cc_conv *c)
{
    for (uint32_t i = 0; i < c->nm; i++) msg_libera(&c->m[i]);
    if (c->m) cc_free(c->m);
    c->m = 0; c->nm = c->cap = 0;
}
void cc_libera(struct cc_hist *h)
{
    for (uint32_t i = 0; i < h->n; i++) conv_libera(&h->c[i]);
    if (h->c) cc_free(h->c);
    cc_init(h);
}
static int crece(void **p, uint32_t *cap, uint32_t need, uint32_t elem)
{
    if (need <= *cap) return 1;
    uint32_t nc = *cap ? *cap * 2 : 8; while (nc < need) nc *= 2;
    uint8_t *n = (uint8_t *)cc_alloc(nc * elem); if (!n) return 0;
    uint8_t *o = (uint8_t *)*p;
    for (uint32_t i = 0; i < *cap * elem; i++) n[i] = o[i];
    if (o) cc_free(o);
    *p = n; *cap = nc; return 1;
}
struct cc_conv *cc_nueva(struct cc_hist *h, uint32_t t, int modo, int mem)
{
    if (h->n >= CC_CONV_MAX) return 0;
    if (!crece((void **)&h->c, &h->cap, h->n + 1, sizeof(struct cc_conv))) return 0;
    struct cc_conv *c = &h->c[h->n++];
    uint8_t *z = (uint8_t *)c; for (uint32_t i = 0; i < sizeof *c; i++) z[i] = 0;
    c->id = h->sig_id++; c->t0 = c->t1 = t; c->modo = (uint8_t)modo; c->mem = (uint8_t)mem;
    return c;
}
int cc_indice(const struct cc_hist *h, uint32_t id)
{
    for (uint32_t i = 0; i < h->n; i++) if (h->c[i].id == id) return (int)i;
    return -1;
}
struct cc_conv *cc_por_id(struct cc_hist *h, uint32_t id) { int i = cc_indice(h, id); return i < 0 ? 0 : &h->c[i]; }
int cc_borra(struct cc_hist *h, uint32_t id)
{
    int i = cc_indice(h, id); if (i < 0) return 0;
    conv_libera(&h->c[i]);
    for (uint32_t k = (uint32_t)i; k + 1 < h->n; k++) h->c[k] = h->c[k + 1];
    h->n--; return 1;
}
struct cc_msg *cc_anade(struct cc_conv *c, int rol, const char *txt, uint32_t n, const char *adj, uint32_t t)
{
    if (c->nm >= CC_MSG_MAX) return 0;
    if (n > CC_TXT_MAX) n = CC_TXT_MAX;
    if (!crece((void **)&c->m, &c->cap, c->nm + 1, sizeof(struct cc_msg))) return 0;
    char *d = dup_n(txt ? txt : "", txt ? n : 0); if (!d) return 0;
    char *a = 0;
    if (adj && adj[0]) { a = dup_n(adj, slen(adj)); if (!a) { cc_free(d); return 0; } }
    struct cc_msg *m = &c->m[c->nm++];
    m->rol = (uint8_t)rol; m->modo = c->modo; m->mem = c->mem; m->estado = CC_OK; m->t = t; m->dur = 0;
    m->txt = d; m->n = txt ? n : 0; m->adj = a;
    if (t > c->t1) c->t1 = t;
    return m;
}
int cc_texto(struct cc_msg *m, const char *txt, uint32_t n)
{
    if (n > CC_TXT_MAX) n = CC_TXT_MAX;
    char *d = dup_n(txt, n); if (!d) return 0;
    if (m->txt) cc_free(m->txt);
    m->txt = d; m->n = n; return 1;
}
void cc_trunca(struct cc_conv *c, uint32_t desde)
{
    while (c->nm > desde) msg_libera(&c->m[--c->nm]);
}

/* ---------------------------------------------------------------- titulo */
static uint32_t utf8_corte(const char *s, uint32_t n) { while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--; return n; }
void cc_titulo_pon(struct cc_conv *c, const char *t)
{
    uint32_t n = slen(t); while (n && (t[n - 1] == ' ' || t[n - 1] == '\n')) n--;
    while (n && (*t == ' ' || *t == '\n')) { t++; n--; }
    if (n >= CC_TIT_MAX) n = utf8_corte(t, CC_TIT_MAX - 1);
    mcpy(c->titulo, t, n); c->titulo[n] = 0; c->titulo_manual = 1;
}
void cc_titulo_auto(struct cc_conv *c)
{
    if (c->titulo_manual) return;
    const struct cc_msg *m = 0;
    for (uint32_t i = 0; i < c->nm; i++) if (c->m[i].rol == CC_USER && c->m[i].n) { m = &c->m[i]; break; }
    if (!m) { c->titulo[0] = 0; return; }
    char t[CC_TIT_MAX]; uint32_t k = 0, lim = 44; int sp = 0;
    for (uint32_t i = 0; i < m->n && k + 1 < sizeof t; i++) {
        char ch = m->txt[i];
        if (ch == '\n' || ch == '\r') { if (k) break; else continue; }
        if (ch == '#' || ch == '*' || ch == '`' || ch == '_' || ch == '>' || ch == '$' || ch == '\\') continue;
        if (ch == ' ' || ch == '\t') { if (!k || sp) continue; sp = 1; t[k++] = ' '; continue; }
        sp = 0; t[k++] = ch;
    }
    while (k && t[k - 1] == ' ') k--;
    if (k > lim) {
        uint32_t cut = utf8_corte(t, lim), w = cut;
        while (w > lim / 2 && t[w] != ' ') w--;
        if (t[w] == ' ') cut = w;
        k = cut; while (k && t[k - 1] == ' ') k--;
        if (k + 4 < sizeof t) { t[k++] = (char)0xE2; t[k++] = (char)0x80; t[k++] = (char)0xA6; }   /* ... */
    }
    mcpy(c->titulo, t, k); c->titulo[k] = 0;
}

/* ---------------------------------------------------------------- archivo */
struct sk { char *o; uint32_t n, max; };
static void put(struct sk *s, const char *p, uint32_t n) { for (uint32_t i = 0; i < n; i++) { if (s->o && s->n < s->max) s->o[s->n] = p[i]; s->n++; } }
static void puts_(struct sk *s, const char *p) { put(s, p, slen(p)); }
static void putu(struct sk *s, uint32_t v)
{
    char t[12]; int k = 0; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    char r[12]; for (int i = 0; i < k; i++) r[i] = t[k - 1 - i];
    put(s, " ", 1); put(s, r, (uint32_t)k);
}
uint32_t cc_serializa(const struct cc_hist *h, char *out, uint32_t max)
{
    struct sk s = { out, 0, out ? max : 0 };
    puts_(&s, "SOMACHAT 1"); putu(&s, h->sig_id); put(&s, "\n", 1);
    for (uint32_t i = 0; i < h->n; i++) {
        const struct cc_conv *c = &h->c[i];
        put(&s, "C", 1); putu(&s, c->id); putu(&s, c->t0); putu(&s, c->t1); putu(&s, c->modo); putu(&s, c->mem); putu(&s, c->fija);
        putu(&s, c->titulo_manual); putu(&s, slen(c->titulo)); put(&s, "\n", 1); puts_(&s, c->titulo); put(&s, "\n", 1);
        for (uint32_t k = 0; k < c->nm; k++) {
            const struct cc_msg *m = &c->m[k];
            put(&s, "M", 1); putu(&s, m->rol); putu(&s, m->modo); putu(&s, m->mem); putu(&s, m->estado); putu(&s, m->t); putu(&s, m->dur);
            putu(&s, m->n); putu(&s, slen(m->adj)); put(&s, "\n", 1);
            put(&s, m->txt, m->n); puts_(&s, m->adj ? m->adj : ""); put(&s, "\n", 1);
        }
    }
    return s.n;
}
struct rd { const char *p; uint32_t n, i; int mal; };
static uint32_t rnum(struct rd *r)
{
    while (r->i < r->n && r->p[r->i] == ' ') r->i++;
    uint32_t v = 0; int any = 0;
    while (r->i < r->n && r->p[r->i] >= '0' && r->p[r->i] <= '9') { v = v * 10 + (uint32_t)(r->p[r->i] - '0'); r->i++; any = 1; }
    if (!any) r->mal = 1;
    return v;
}
static void rfin(struct rd *r) { if (r->i < r->n && r->p[r->i] == '\n') r->i++; else r->mal = 1; }
int cc_carga(struct cc_hist *h, const char *buf, uint32_t n)
{
    static const char cab[] = "SOMACHAT 1";
    if (n < sizeof cab - 1) return -1;
    for (uint32_t i = 0; i < sizeof cab - 1; i++) if (buf[i] != cab[i]) return -1;
    struct rd r = { buf, n, sizeof cab - 1, 0 };
    uint32_t sig = rnum(&r); rfin(&r);
    if (r.mal) return -1;
    struct cc_conv *c = 0; int nc = 0;
    while (r.i < r.n && !r.mal) {
        char k = r.p[r.i++];
        if (k == 'C') {
            uint32_t id = rnum(&r), t0 = rnum(&r), t1 = rnum(&r), modo = rnum(&r), mem = rnum(&r), fija = rnum(&r), man = rnum(&r), tl = rnum(&r); rfin(&r);
            if (r.mal || tl >= CC_TIT_MAX || r.i + tl > r.n) { r.mal = 1; break; }
            c = cc_nueva(h, t0, (int)modo, (int)mem); if (!c) break;
            c->id = id; c->t1 = t1; c->fija = (uint8_t)(fija != 0); c->titulo_manual = (uint8_t)(man != 0);
            mcpy(c->titulo, r.p + r.i, tl); c->titulo[tl] = 0; r.i += tl; rfin(&r);
            if (id >= h->sig_id) h->sig_id = id + 1;
            nc++;
        } else if (k == 'M' && c) {
            uint32_t rol = rnum(&r), modo = rnum(&r), mem = rnum(&r), est = rnum(&r), t = rnum(&r), dur = rnum(&r), tn = rnum(&r), an = rnum(&r); rfin(&r);
            if (r.mal || tn > CC_TXT_MAX || an > 4096 || r.i + tn + an > r.n) { r.mal = 1; break; }
            char adj[4097]; mcpy(adj, r.p + r.i + tn, an); adj[an] = 0;
            uint32_t t1 = c->t1;
            struct cc_msg *m = cc_anade(c, rol ? CC_ASIS : CC_USER, r.p + r.i, tn, an ? adj : 0, t);
            if (!m) break;
            c->t1 = t1;
            m->modo = (uint8_t)modo; m->mem = (uint8_t)mem; m->estado = (uint8_t)est; m->dur = dur;
            r.i += tn + an; rfin(&r);
        } else { r.mal = 1; break; }
    }
    if (sig > h->sig_id) h->sig_id = sig;
    return nc;
}

/* ---------------------------------------------------------------- lista y busqueda */
static uint32_t pliega(const char *s, uint32_t n, uint32_t *i)          /* siguiente letra sin mayusculas ni tildes */
{
    uint8_t c = (uint8_t)s[*i]; (*i)++;
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c == 0xC3 && *i < n) {
        uint8_t d = (uint8_t)s[*i]; (*i)++;
        switch (d | 0x20) {
        case 0xA1: case 0xA0: case 0xA4: case 0xA2: return 'a';
        case 0xA9: case 0xA8: case 0xAB: case 0xAA: return 'e';
        case 0xAD: case 0xAC: case 0xAF: case 0xAE: return 'i';
        case 0xB3: case 0xB2: case 0xB6: case 0xB4: return 'o';
        case 0xBA: case 0xB9: case 0xBC: case 0xBB: return 'u';
        case 0xB1: return 'n';
        case 0xA7: return 'c';
        default: return 0xC300u | d;
        }
    }
    return c;
}
int cc_coincide(const char *texto, uint32_t n, const char *filtro)
{
    uint32_t fn = slen(filtro);
    if (!fn) return 1;
    for (uint32_t a = 0; a < n; ) {
        uint32_t i = a, j = 0; int ok = 1;
        while (j < fn) {
            if (i >= n) { ok = 0; break; }
            if (pliega(texto, n, &i) != pliega(filtro, fn, &j)) { ok = 0; break; }
        }
        if (ok) return 1;
        a++; while (a < n && ((uint8_t)texto[a] & 0xC0) == 0x80) a++;
    }
    return 0;
}
static int conv_coincide(const struct cc_conv *c, const char *f)
{
    if (!f || !f[0]) return 1;
    if (cc_coincide(c->titulo, slen(c->titulo), f)) return 1;
    for (uint32_t i = 0; i < c->nm; i++) if (cc_coincide(c->m[i].txt, c->m[i].n, f)) return 1;
    return 0;
}
static int antes(const struct cc_conv *a, const struct cc_conv *b)       /* a va antes que b */
{
    if (a->fija != b->fija) return a->fija > b->fija;
    if (a->t1 != b->t1) return a->t1 > b->t1;
    return a->id > b->id;
}
int cc_orden(const struct cc_hist *h, const char *filtro, uint32_t *ids, int max)
{
    int k = 0;
    for (uint32_t i = 0; i < h->n && k < max; i++) {
        const struct cc_conv *c = &h->c[i];
        if (!conv_coincide(c, filtro)) continue;
        int p = k++;
        while (p > 0) {
            const struct cc_conv *o = &h->c[cc_indice(h, ids[p - 1])];
            if (!antes(c, o)) break;
            ids[p] = ids[p - 1]; p--;
        }
        ids[p] = c->id;
    }
    return k;
}
int cc_grupo(const struct cc_conv *c, uint32_t ahora)
{
    if (c->fija) return 0;
    uint32_t d0 = ahora / 1440, d = c->t1 / 1440;
    if (d >= d0) return 1;
    if (d + 1 == d0) return 2;
    if (d + 7 >= d0) return 3;
    if (d + 30 >= d0) return 4;
    return 5;
}
const char *cc_grupo_nombre(int g)
{
    static const char *const N[6] = { "Fijadas", "Hoy", "Ayer", "\303\232ltimos 7 d\303\255as", "\303\232ltimos 30 d\303\255as", "Anteriores" };
    return g >= 0 && g < 6 ? N[g] : "";
}

/* ---------------------------------------------------------------- contexto */
uint32_t cc_ventana(const struct cc_conv *c, uint32_t hasta, uint32_t presupuesto)
{
    if (hasta > c->nm) hasta = c->nm;
    uint32_t usado = 0, x = hasta;
    while (x > 0) {
        uint32_t t = c->m[x - 1].n + 16;
        if (x < hasta && usado + t > presupuesto) break;
        usado += t; x--;
    }
    while (x < hasta && c->m[x].rol != CC_USER) x++;          /* empezar por una pregunta */
    return x;
}
uint32_t cc_contexto(const struct cc_conv *c, uint32_t hasta, char *dst, uint32_t max)
{
    if (!max) return 0;
    uint32_t x = cc_ventana(c, hasta, max > 400 ? max - 400 : max / 2), q = 0;
    for (uint32_t i = x; i < hasta && i < c->nm; i++) {
        const struct cc_msg *m = &c->m[i];
        if (!m->n) continue;
        const char *who = m->rol == CC_USER ? "Usuario: " : "SOMA: ";
        uint32_t wl = slen(who);
        if (q + wl + 2 >= max) break;
        mcpy(dst + q, who, wl); q += wl;
        uint32_t n = m->n; if (q + n + 2 >= max) n = utf8_corte(m->txt, max - q - 2);
        mcpy(dst + q, m->txt, n); q += n; dst[q++] = '\n';
    }
    dst[q] = 0; return q;
}

/* ---------------------------------------------------------------- fechas */
static void civil(uint32_t dias, int *y, int *mo, int *d)               /* dias desde 1970-01-01 -> fecha (Howard Hinnant) */
{
    int32_t z = (int32_t)dias + 719468, era = z / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t yy = (int32_t)yoe + era * 400; uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1); *mo = (int)(mp < 10 ? mp + 3 : mp - 9); *y = (int)(yy + (*mo <= 2));
}
static int pnum(char *o, int k, int cap, uint32_t v, int dos)
{
    char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    if (dos && n < 2) t[n++] = '0';
    while (n && k + 1 < cap) o[k++] = t[--n];
    o[k] = 0; return k;
}
static int pstr(char *o, int k, int cap, const char *s) { while (*s && k + 1 < cap) o[k++] = *s++; o[k] = 0; return k; }
void cc_hora(uint32_t t, char *out)
{
    uint32_t m = t % 1440; int k = pnum(out, 0, 6, m / 60, 0); out[k++] = ':'; pnum(out, k, 6, m % 60, 1);
}
void cc_fecha(uint32_t t, uint32_t ahora, char *out, int cap)
{
    static const char *const MES[12] = { "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic" };
    uint32_t d = t / 1440, d0 = ahora / 1440; char h[6]; cc_hora(t, h); int k = 0; out[0] = 0;
    if (d == d0) { k = pstr(out, k, cap, "hoy, "); pstr(out, k, cap, h); return; }
    if (d + 1 == d0) { k = pstr(out, k, cap, "ayer, "); pstr(out, k, cap, h); return; }
    int y, mo, dd, y0, m0, e0; civil(d, &y, &mo, &dd); civil(d0, &y0, &m0, &e0);
    k = pnum(out, k, cap, (uint32_t)dd, 0); k = pstr(out, k, cap, " "); k = pstr(out, k, cap, MES[mo - 1]);
    if (y != y0) { k = pstr(out, k, cap, " "); pnum(out, k, cap, (uint32_t)y, 0); }
    else { k = pstr(out, k, cap, ", "); pstr(out, k, cap, h); }
}
