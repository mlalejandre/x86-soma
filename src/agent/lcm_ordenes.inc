/* SOMA_253_LCM: traductor de peticiones a ordenes, entrenado en el LCM (github.com/mlalejandre/LCM, src/ordenes).
 *
 * NO se compila por separado: lo incluye src/agent/agent.c (como shell_*.c en shell.c). Sin coma flotante (el nucleo compila
 * con -mgeneral-regs-only): la red va cuantizada (pesos int8 por fila, activaciones en Q12) y tanh/exp son tablas.
 * Sin dependencias: solo stdint. Los pesos, el vocabulario, las ordenes y las tablas van en un unico bloque de bytes
 * (lcm_ordenes_pesos.inc, generado por tools/lcm_ordenes/exporta_c.py a partir del modelo entrenado).
 *
 * lcm_traduce(texto, &r): r.estado = LCM_ORDEN (r.orden lista; r.confirmar si es peligrosa), LCM_LLM (no es una orden),
 * LCM_DUDA (no esta seguro: que decida el camino de siempre), LCM_FALTA (es una orden, pero falta una pieza: r.falta),
 * LCM_RARO (una pieza trae caracteres de shell: no se arma la orden). */
#include <stdint.h>

#define LCM_ORDEN 0
#define LCM_LLM   1
#define LCM_DUDA  2
#define LCM_FALTA 3
#define LCM_RARO  4

#define LCM_MAXTOK   64
#define LCM_MAXTOKB  96
#define LCM_MAXIN    1024
#define LCM_MAXH     160
#define LCM_MAXIT    160
#define LCM_MAXTAG   32
#define LCM_MAXSLOT  12

struct lcm_res {
    int  estado, intent, confirmar;
    uint32_t p_mil;                  /* probabilidad de la orden elegida, en tantos por mil */
    char intent_nombre[24];
    char falta[8];
    char orden[512];
};

/* ------------------------------------------------------------------------------------------- lectura del bloque */
struct lcm_t8 { uint32_t filas, cols; const uint8_t *mult; const int8_t *q; };     /* real = q * mult / 2^24 */
struct lcm_v32 { uint32_t n; const uint8_t *v; };                                    /* int32 en Q12 */

static struct lcm_modelo {
    int ok;
    uint32_t nvoc, dw, nb, db, h, in, ni, nt, umbral_mil, ntanh, nexp;
    const uint8_t *voc;               /* nvoc entradas: u32 id, u8 len, bytes (ordenadas por bytes) */
    const uint8_t *it_nom[LCM_MAXIT], *it_ord[LCM_MAXIT]; uint8_t it_nl[LCM_MAXIT], it_ol[LCM_MAXIT], it_pel[LCM_MAXIT];
    char tag_slot[LCM_MAXTAG]; char tag_bi[LCM_MAXTAG];                            /* 'B'/'I'/'O' y letra de la pieza */
    struct lcm_t8 ew, eb[4], wih[2], whh[2], wint, wtag;
    struct lcm_v32 b[2], bint, btag;
    const uint8_t *tanh_t, *exp_t;    /* int32: tanh en Q12 para x en [-8,8] paso 1/128; exp en Q20 para x en [-16,0] paso 1/64 */
    uint32_t llm_id, bn;
    const uint8_t *voc_off;           /* tabla de desplazamientos (u32) de cada entrada del vocabulario */
} lcm_ms[2];
#define lcm_m (lcm_ms[0])                 /* el primero: datos comunes (ordenes, etiquetas, umbral) */
static struct lcm_modelo *lcm_mc = &lcm_ms[0];   /* el que se esta ejecutando */
static int lcm_nm = 1;                    /* SOMA_258_LCM: conjunto de modelos; se promedian sus probabilidades */

static uint32_t lcm_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int32_t  lcm_i32(const uint8_t *p) { return (int32_t)lcm_u32(p); }

static const uint8_t *lcm_lee_t8(const uint8_t *p, const uint8_t *fin, struct lcm_t8 *t)
{
    if (!p || p + 8 > fin) return 0;
    t->filas = lcm_u32(p); t->cols = lcm_u32(p + 4); p += 8;
    t->mult = p; p += 4u * t->filas;
    t->q = (const int8_t *)p; p += (uint64_t)t->filas * t->cols;
    return p <= fin ? p : 0;
}

static const uint8_t *lcm_lee_v32(const uint8_t *p, const uint8_t *fin, struct lcm_v32 *v)
{
    if (!p || p + 4 > fin) return 0;
    v->n = lcm_u32(p); v->v = p + 4; p += 4 + 4u * v->n;
    return p <= fin ? p : 0;
}

static int lcm_carga1(struct lcm_modelo *m, const uint8_t *blob, uint32_t len)
{
    const uint8_t *p = blob, *fin = blob + len;
    m->ok = 0;
    if (len < 64 || p[0] != 'L' || p[1] != 'C' || p[2] != 'M' || p[3] != 'O' || lcm_u32(p + 4) != 1) return 0;
    p += 8;
    m->nvoc = lcm_u32(p); m->dw = lcm_u32(p + 4); m->nb = lcm_u32(p + 8); m->db = lcm_u32(p + 12);
    m->h = lcm_u32(p + 16); m->in = lcm_u32(p + 20); m->ni = lcm_u32(p + 24); m->nt = lcm_u32(p + 28);
    m->umbral_mil = lcm_u32(p + 32); m->ntanh = lcm_u32(p + 36); m->nexp = lcm_u32(p + 40);
    p += 44;
    if (m->h > LCM_MAXH || m->ni > LCM_MAXIT || m->nt > LCM_MAXTAG || m->in != m->dw + 4 * m->db + 4 || m->in > 4 * LCM_MAXH) return 0;
    m->voc_off = p; p += 4u * m->nvoc;
    m->voc = p;
    if (p > fin) return 0;
    { const uint8_t *q = p; for (uint32_t i = 0; i < m->nvoc; ++i) { if (q + 5 > fin) return 0; q += 5 + q[4]; } p = q; }
    m->llm_id = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < m->ni; ++i) {
        if (p + 3 > fin) return 0;
        m->it_nl[i] = p[0]; m->it_nom[i] = p + 1; p += 1 + p[0];
        m->it_ol[i] = p[0]; m->it_ord[i] = p + 1; p += 1 + p[0];
        m->it_pel[i] = p[0]; p += 1;
        if (m->it_nl[i] == 3 && m->it_nom[i][0] == 'L' && m->it_nom[i][1] == 'L' && m->it_nom[i][2] == 'M') m->llm_id = i;
    }
    for (uint32_t i = 0; i < m->nt; ++i) {
        if (p + 1 > fin) return 0;
        uint8_t l = p[0];
        m->tag_bi[i] = l ? (char)p[1] : 'O';
        m->tag_slot[i] = (l >= 3) ? (char)p[3] : 0;
        p += 1 + l;
    }
    p = lcm_lee_t8(p, fin, &m->ew);
    for (int i = 0; i < 4; ++i) p = lcm_lee_t8(p, fin, &m->eb[i]);
    for (int d = 0; d < 2; ++d) { p = lcm_lee_t8(p, fin, &m->wih[d]); p = lcm_lee_t8(p, fin, &m->whh[d]); p = lcm_lee_v32(p, fin, &m->b[d]); }
    p = lcm_lee_t8(p, fin, &m->wint); p = lcm_lee_v32(p, fin, &m->bint);
    p = lcm_lee_t8(p, fin, &m->wtag); p = lcm_lee_v32(p, fin, &m->btag);
    if (!p || p + 4u * (m->ntanh + m->nexp) > fin) return 0;
    m->tanh_t = p; p += 4u * m->ntanh; m->exp_t = p; p += 4u * m->nexp;
    if (m->wih[0].cols != m->in || m->whh[0].cols != m->h || m->wih[0].filas != 4 * m->h || m->wint.cols != 4 * m->h ||
        m->wtag.cols != 2 * m->h || m->wint.filas != m->ni || m->wtag.filas != m->nt || m->ew.cols != m->dw || m->llm_id == 0xFFFFFFFFu) return 0;
    m->bn = 0;
    for (uint32_t i = 0; i < m->nt; ++i) if (m->tag_bi[i] == 'B' && m->tag_slot[i] == 'N') m->bn = i;
    m->ok = 1;
    return 1;
}

/* Un modelo ("LCMO...") o un conjunto ("LCMS", n, y n veces: longitud + modelo). */
static int lcm_carga(const uint8_t *blob, uint32_t len)
{
    lcm_nm = 0; lcm_ms[0].ok = 0; lcm_ms[1].ok = 0; lcm_mc = &lcm_ms[0];
    if (len >= 8 && blob[0] == 'L' && blob[1] == 'C' && blob[2] == 'M' && blob[3] == 'S') {
        uint32_t n = lcm_u32(blob + 4), off = 8;
        if (n < 1 || n > 2) return 0;
        for (uint32_t i = 0; i < n; ++i) {
            if (off + 4 > len) return 0;
            uint32_t l = lcm_u32(blob + off); off += 4;
            if (l > len - off || !lcm_carga1(&lcm_ms[i], blob + off, l)) { lcm_ms[0].ok = 0; return 0; }
            off += l;
        }
        if (n == 2 && (lcm_ms[1].ni != lcm_ms[0].ni || lcm_ms[1].nt != lcm_ms[0].nt || lcm_ms[1].llm_id != lcm_ms[0].llm_id)) { lcm_ms[0].ok = 0; return 0; }
        lcm_nm = (int)n;
        return 1;
    }
    if (!lcm_carga1(&lcm_ms[0], blob, len)) return 0;
    lcm_nm = 1;
    return 1;
}

/* ------------------------------------------------------------------------------------------- texto (UTF-8) */
static uint32_t lcm_dec(const uint8_t *s, uint32_t *i)       /* siguiente punto de codigo (UTF-8 mal formado: byte a byte) */
{
    uint32_t c = s[*i];
    if (c < 0x80) { (*i)++; return c; }
    if ((c & 0xE0) == 0xC0 && (s[*i + 1] & 0xC0) == 0x80) { c = ((c & 0x1F) << 6) | (s[*i + 1] & 0x3F); *i += 2; return c; }
    if ((c & 0xF0) == 0xE0 && (s[*i + 1] & 0xC0) == 0x80 && (s[*i + 2] & 0xC0) == 0x80) {
        c = ((c & 0x0F) << 12) | ((uint32_t)(s[*i + 1] & 0x3F) << 6) | (s[*i + 2] & 0x3F); *i += 3; return c; }
    if ((c & 0xF8) == 0xF0 && (s[*i + 1] & 0xC0) == 0x80 && (s[*i + 2] & 0xC0) == 0x80 && (s[*i + 3] & 0xC0) == 0x80) {
        c = ((c & 0x07) << 18) | ((uint32_t)(s[*i + 1] & 0x3F) << 12) | ((uint32_t)(s[*i + 2] & 0x3F) << 6) | (s[*i + 3] & 0x3F); *i += 4; return c; }
    (*i)++; return 0xFFFD;
}

static uint32_t lcm_enc(uint32_t c, uint8_t *o)
{
    if (c < 0x80) { o[0] = (uint8_t)c; return 1; }
    if (c < 0x800) { o[0] = (uint8_t)(0xC0 | (c >> 6)); o[1] = (uint8_t)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) { o[0] = (uint8_t)(0xE0 | (c >> 12)); o[1] = (uint8_t)(0x80 | ((c >> 6) & 0x3F)); o[2] = (uint8_t)(0x80 | (c & 0x3F)); return 3; }
    o[0] = (uint8_t)(0xF0 | (c >> 18)); o[1] = (uint8_t)(0x80 | ((c >> 12) & 0x3F)); o[2] = (uint8_t)(0x80 | ((c >> 6) & 0x3F)); o[3] = (uint8_t)(0x80 | (c & 0x3F));
    return 4;
}

static int lcm_es_dig(uint32_t c) { return c >= '0' && c <= '9'; }
static int lcm_es_esp(uint32_t c) { return c == ' ' || (c >= 9 && c <= 13) || c == 0x1C || c == 0x1D || c == 0x1E || c == 0x1F || c == 0x85 || c == 0xA0 || c == 0x2028 || c == 0x2029 || c == 0x3000 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F || c == 0x1680; }
static int lcm_es_w(uint32_t c)        /* \w de Python para el texto que se espera (latino) */
{
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || lcm_es_dig(c) || c == '_';
    if (c == 0xAA || c == 0xB5 || c == 0xBA || c == 0xB2 || c == 0xB3 || c == 0xB9 || c == 0xBC || c == 0xBD || c == 0xBE) return 1;
    if (c >= 0xC0 && c <= 0x24F) return c != 0xD7 && c != 0xF7;
    if (c >= 0x370 && c <= 0x3FF) return c != 0x37E && c != 0x387 && c != 0x375;
    if (c >= 0x400 && c <= 0x481) return 1;
    return 0;
}
static int lcm_es_mayus(uint32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7); }
static uint32_t lcm_minus(uint32_t c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    return c;
}

/* palabras: igual que trocea() de Python:
 * https?://\S+ | www\.\S+ | [\w/~\-]+(?:\.[\w\-]+)+/?\S* | /[\w/\.\-]+ | \w+(?:-\w+)+ | \d+(?:[.,]\d+)* | \w+ | [^\s\w] */
struct lcm_tok { uint32_t ini, fin; };

static uint32_t lcm_cp_en(const uint8_t *s, uint32_t i, uint32_t *sig) { uint32_t j = i; uint32_t c = s[i] ? lcm_dec(s, &j) : 0; if (sig) *sig = j; return c; }

static int lcm_tiene_punto_barra(const uint8_t *s, uint32_t a, uint32_t b)
{
    for (uint32_t k = a; k < b; ++k) if (s[k] == '.' || s[k] == '/') return 1;
    return 0;
}

static int lcm_signo_suelto(const uint8_t *s, struct lcm_tok t)      /* una sola letra que no es \w ni espacio */
{
    uint32_t i = t.ini; uint32_t c = lcm_dec(s, &i);
    return i == t.fin && !lcm_es_w(c) && !lcm_es_esp(c);
}

static int lcm_trocea(const uint8_t *s, struct lcm_tok *t, int max)
{
    int n = 0; uint32_t i = 0, j, c;
    while (s[i] && n < max) {
        c = lcm_cp_en(s, i, &j);
        if (lcm_es_esp(c)) { i = j; continue; }
        uint32_t fin = 0;
        /* 1-2: direcciones web */
        if ((s[i] == 'h' && s[i+1] == 't' && s[i+2] == 't' && s[i+3] == 'p') || (s[i] == 'w' && s[i+1] == 'w' && s[i+2] == 'w' && s[i+3] == '.')) {
            uint32_t k = i;
            if (s[i] == 'h') { k = i + 4; if (s[k] == 's') ++k; if (s[k] == ':' && s[k+1] == '/' && s[k+2] == '/') k += 3; else k = 0; }
            else k = i + 4;
            if (k) { uint32_t q = k, nq; uint32_t cc = s[q] ? lcm_cp_en(s, q, &nq) : 0;
                if (cc && !lcm_es_esp(cc)) { while (s[q]) { cc = lcm_cp_en(s, q, &nq); if (lcm_es_esp(cc)) break; q = nq; } fin = q; } }
        }
        /* 3: [\w/~-]+ seguido de .[\w-] : hasta el siguiente espacio */
        if (!fin) {
            uint32_t q = i, nq;
            while (s[q]) { uint32_t cc = lcm_cp_en(s, q, &nq); if (!(lcm_es_w(cc) || cc == '/' || cc == '~' || cc == '-')) break; q = nq; }
            if (q > i && s[q] == '.') { uint32_t nq2; uint32_t cc = s[q+1] ? lcm_cp_en(s, q + 1, &nq2) : 0;
                if (cc && (lcm_es_w(cc) || cc == '-')) { while (s[q]) { cc = lcm_cp_en(s, q, &nq); if (lcm_es_esp(cc)) break; q = nq; } fin = q; } }
        }
        /* 4: /[\w/.-]+ */
        if (!fin && s[i] == '/') {
            uint32_t q = i + 1, nq;
            while (s[q]) { uint32_t cc = lcm_cp_en(s, q, &nq); if (!(lcm_es_w(cc) || cc == '/' || cc == '.' || cc == '-')) break; q = nq; }
            if (q > i + 1) fin = q;
        }
        /* 4b: palabras con guiones (Viaje-Roma-2026) */
        if (!fin && lcm_es_w(c)) {
            uint32_t q = i, nq, guiones = 0;
            for (;;) {
                while (s[q]) { uint32_t cc = lcm_cp_en(s, q, &nq); if (!lcm_es_w(cc)) break; q = nq; }
                if (s[q] == '-' && s[q + 1]) { uint32_t n2; uint32_t cc = lcm_cp_en(s, q + 1, &n2); if (lcm_es_w(cc)) { q = q + 1; ++guiones; continue; } }
                break;
            }
            if (guiones) fin = q;
        }
        /* 5: numeros */
        if (!fin && lcm_es_dig(c)) {
            uint32_t q = i; while (lcm_es_dig(s[q])) ++q;
            while ((s[q] == '.' || s[q] == ',') && lcm_es_dig(s[q+1])) { q += 1; while (lcm_es_dig(s[q])) ++q; }
            fin = q;
        }
        /* 6: \w+ */
        if (!fin && lcm_es_w(c)) {
            uint32_t q = i, nq; while (s[q]) { uint32_t cc = lcm_cp_en(s, q, &nq); if (!lcm_es_w(cc)) break; q = nq; }
            fin = q;
        }
        if (fin && lcm_tiene_punto_barra(s, i, fin)) {
            /* archivo o direccion: los signos pegados al final van aparte, como en Python */
            while (fin > i + 1) {
                if (fin >= i + 3 && s[fin - 2] == 0xC2 && s[fin - 1] == 0xBB) { fin -= 2; continue; }      /* » */
                uint8_t u = s[fin - 1];
                if (u == '?' || u == '!' || u == ',' || u == ';' || u == ':' || u == ')' || u == '"' || u == '\'' || u == '.') { --fin; continue; }
                break;
            }
        }
        if (!fin) fin = j;     /* 7: un signo */
        if (fin - i < LCM_MAXTOKB) { t[n].ini = i; t[n].fin = fin; ++n; }
        i = fin;
    }
    return n;
}

static uint32_t lcm_fnv(const uint8_t *a, uint32_t la, const uint8_t *b, uint32_t lb, uint32_t nb)
{
    uint32_t v = 2166136261u;
    for (uint32_t i = 0; i < la; ++i) v = (v ^ a[i]) * 16777619u;
    for (uint32_t i = 0; i < lb; ++i) v = (v ^ b[i]) * 16777619u;
    return (v % (nb - 1)) + 1;
}

static int lcm_voc_busca(const uint8_t *w, uint32_t l)
{
    struct lcm_modelo *m = lcm_mc;
    int lo = 0, hi = (int)m->nvoc - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const uint8_t *e = m->voc + lcm_u32(m->voc_off + 4u * (uint32_t)mid);
        uint32_t el = e[4]; const uint8_t *eb = e + 5;
        int cmp = 0; uint32_t k = 0;
        while (k < el && k < l && eb[k] == w[k]) ++k;
        if (k < el && k < l) cmp = (int)eb[k] - (int)w[k];
        else cmp = (int)el - (int)l;
        if (!cmp) return (int)lcm_u32(e);
        if (cmp < 0) lo = mid + 1; else hi = mid - 1;
    }
    return 1;
}

/* ------------------------------------------------------------------------------------------- red en enteros */
#define LCM_Q 12
static int32_t lcm_tanh(int32_t x)
{
    struct lcm_modelo *m = lcm_mc;
    int64_t u = (int64_t)x + (8 << LCM_Q);
    if (u <= 0) return -(1 << LCM_Q);
    if (u >= (int64_t)(16 << LCM_Q)) return 1 << LCM_Q;
    uint32_t k = (uint32_t)(u >> 5), fr = (uint32_t)(u & 31);
    int32_t a = lcm_i32(m->tanh_t + 4 * k), b = lcm_i32(m->tanh_t + 4 * (k + 1 < m->ntanh ? k + 1 : k));
    return a + (int32_t)(((int64_t)(b - a) * fr) >> 5);
}
static int32_t lcm_sigm(int32_t x) { return ((1 << LCM_Q) + lcm_tanh(x / 2)) >> 1; }
static int64_t lcm_exp20(int32_t d)        /* exp(d) en Q20, d <= 0 en Q12 */
{
    struct lcm_modelo *m = lcm_mc;
    if (d >= 0) return 1 << 20;
    int64_t u = (int64_t)d + (16 << LCM_Q);
    if (u <= 0) return 0;
    uint32_t k = (uint32_t)(u >> 6), fr = (uint32_t)(u & 63);
    int64_t a = lcm_i32(m->exp_t + 4 * k), b = lcm_i32(m->exp_t + 4 * (k + 1 < m->nexp ? k + 1 : k));
    return a + (((b - a) * fr) >> 6);
}

static int32_t lcm_fila_q12(const struct lcm_t8 *t, uint32_t f, uint32_t j) { return (int32_t)(((int64_t)t->q[(uint64_t)f * t->cols + j] * lcm_u32(t->mult + 4 * f)) >> 12); }

static void lcm_matvec(const struct lcm_t8 *t, const int32_t *x, int32_t *y, int acumula)
{
    for (uint32_t f = 0; f < t->filas; ++f) {
        const int8_t *q = t->q + (uint64_t)f * t->cols;
        int64_t acc = 0;
        for (uint32_t j = 0; j < t->cols; ++j) acc += (int64_t)q[j] * x[j];
        int32_t v = (int32_t)((acc * (int64_t)lcm_u32(t->mult + 4 * f)) >> 24);
        y[f] = acumula ? y[f] + v : v;
    }
}

static int32_t lcm_x[LCM_MAXTOK][4 * LCM_MAXH];
static int32_t lcm_hs[LCM_MAXTOK][2 * LCM_MAXH];
static int32_t lcm_g[4 * LCM_MAXH], lcm_hh[LCM_MAXH], lcm_cc[LCM_MAXH], lcm_lg[LCM_MAXTOK][LCM_MAXTAG];
static int64_t lcm_pt[LCM_MAXTOK][LCM_MAXTAG], lcm_pi[LCM_MAXIT];   /* probabilidades (Q20) promediadas del conjunto */

static void lcm_lstm(int d, int n)
{
    struct lcm_modelo *m = lcm_mc;
    uint32_t H = m->h;
    for (uint32_t k = 0; k < H; ++k) { lcm_hh[k] = 0; lcm_cc[k] = 0; }
    for (int s = 0; s < n; ++s) {
        int t = d ? n - 1 - s : s;
        lcm_matvec(&m->wih[d], lcm_x[t], lcm_g, 0);
        lcm_matvec(&m->whh[d], lcm_hh, lcm_g, 1);
        for (uint32_t k = 0; k < 4 * H; ++k) lcm_g[k] += lcm_i32(m->b[d].v + 4 * k);
        for (uint32_t k = 0; k < H; ++k) {
            int32_t ig = lcm_sigm(lcm_g[k]), fg = lcm_sigm(lcm_g[H + k]), gg = lcm_tanh(lcm_g[2 * H + k]), og = lcm_sigm(lcm_g[3 * H + k]);
            lcm_cc[k] = (int32_t)(((int64_t)fg * lcm_cc[k] + (int64_t)ig * gg) >> LCM_Q);
            lcm_hh[k] = (int32_t)(((int64_t)og * lcm_tanh(lcm_cc[k])) >> LCM_Q);
            lcm_hs[t][d * H + k] = lcm_hh[k];
        }
    }
}

/* Rasgos de una palabra (igual que rasgos() de Python). */
static void lcm_rasgos(const uint8_t *s, struct lcm_tok tk, int32_t *x)
{
    struct lcm_modelo *m = lcm_mc;
    uint32_t cps[LCM_MAXTOKB]; uint32_t nc = 0, i = tk.ini;
    while (i < tk.fin && nc < LCM_MAXTOKB) cps[nc++] = lcm_dec(s, &i);
    /* original: rasgos de forma */
    int punto = 0, barra = 0, dig = 0;
    for (uint32_t k = 0; k < nc; ++k) { if (cps[k] == '.') punto = 1; if (cps[k] == '/') barra = 1; if (lcm_es_dig(cps[k])) dig = 1; }
    if (nc && cps[nc - 1] == '.') punto = 0;
    int may = nc && lcm_es_mayus(cps[0]);
    /* f = minusculas y sin tilde en aeiou */
    uint8_t f[4 * LCM_MAXTOKB]; uint32_t fl = 0; uint32_t off[LCM_MAXTOKB + 1];
    for (uint32_t k = 0; k < nc; ++k) {
        uint32_t c = lcm_minus(cps[k]);
        if (c == 0xE1) c = 'a'; else if (c == 0xE9) c = 'e'; else if (c == 0xED) c = 'i'; else if (c == 0xF3) c = 'o'; else if (c == 0xFA) c = 'u';
        off[k] = fl; fl += lcm_enc(c, f + fl);
    }
    off[nc] = fl;
    uint32_t wid = (nc && lcm_es_dig(lcm_minus(cps[0]))) ? 1u : (uint32_t)lcm_voc_busca(f, fl);
    if (wid >= m->ew.filas) wid = 1;
    uint32_t p = 0;
    for (uint32_t j = 0; j < m->dw; ++j) x[p++] = lcm_fila_q12(&m->ew, wid, j);
    uint32_t a3 = nc >= 3 ? off[nc - 3] : 0, a4 = nc >= 4 ? off[nc - 4] : 0;
    uint32_t p3 = nc >= 3 ? off[3] : fl, p4 = nc >= 4 ? off[4] : fl;
    uint32_t hsh[4];
    hsh[0] = lcm_fnv((const uint8_t *)"s3", 2, f + a3, fl - a3, m->nb);
    hsh[1] = lcm_fnv((const uint8_t *)"s4", 2, f + a4, fl - a4, m->nb);
    hsh[2] = lcm_fnv((const uint8_t *)"p3", 2, f, p3, m->nb);
    hsh[3] = lcm_fnv((const uint8_t *)"p4", 2, f, p4, m->nb);
    for (int e = 0; e < 4; ++e) for (uint32_t j = 0; j < m->db; ++j) x[p++] = lcm_fila_q12(&m->eb[e], hsh[e], j);
    x[p++] = punto << LCM_Q; x[p++] = barra << LCM_Q; x[p++] = dig << LCM_Q; x[p++] = may << LCM_Q;
}

static void lcm_pone(char *o, uint32_t cap, uint32_t *p, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n && *p + 1 < cap; ++i) o[(*p)++] = s[i];
    o[*p < cap ? *p : cap - 1] = 0;
}

/* ---- piezas: forma, palabras vacias y cortesia final (igual que en traduce.py) */
static uint32_t lcm_pliega_tok(const uint8_t *s, struct lcm_tok t, char *o, uint32_t cap)   /* minusculas, sin tilde en aeiou */
{
    uint32_t i = t.ini, p = 0;
    while (i < t.fin && p + 5 < cap) {
        uint32_t c = lcm_minus(lcm_dec(s, &i));
        if (c == 0xE1) c = 'a'; else if (c == 0xE9) c = 'e'; else if (c == 0xED) c = 'i'; else if (c == 0xF3) c = 'o'; else if (c == 0xFA) c = 'u';
        p += lcm_enc(c, (uint8_t *)o + p);
    }
    o[p] = 0;
    return p;
}
static int lcm_igual(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static int lcm_tiene_w(const uint8_t *s, struct lcm_tok t) { uint32_t i = t.ini; while (i < t.fin) if (lcm_es_w(lcm_dec(s, &i))) return 1; return 0; }
static int lcm_tiene_forma(char sl, const uint8_t *s, struct lcm_tok t)
{
    if (sl == 'A') {
        if (s[t.ini] == '/') return 1;
        for (uint32_t i = t.ini + 1; i + 1 < t.fin; ++i)
            if (s[i] == '.' && s[i - 1] != '.' && s[i - 1] != ' ' && s[i + 1] != '.' && s[i + 1] != ' ') return 1;
        return 0;
    }
    if (sl == 'H') { for (uint32_t i = t.ini; i < t.fin; ++i) if (s[i] == '.' || lcm_es_dig(s[i])) return 1; return 0; }
    if (sl == 'U') { for (uint32_t i = t.ini; i < t.fin; ++i) if (s[i] == '.' || (s[i] == ':' && s[i + 1] == '/' && s[i + 2] == '/')) return 1; return 0; }
    return 1;
}
static const char *const LCM_VACIAS[] = { "de", "del", "la", "el", "los", "las", "un", "una", "unos", "unas", "a", "al", "en", "con", "por", "para", "que", "q", "se", "y", "o",
    "lo", "le", "me", "mi", "mis", "tu", "su", "es", "esta", "este", "eso", "esto", "ahi", "aqui", "alli", "carpeta", "directorio", "archivo", "fichero", "documento", "red", "wifi",
    "clave", "contrasena", "password", "llamada", "llamado", "llame", "nombre", "nueva", "nuevo", "abierta", "abierto", "tiene", "borra", "llamalo", "llamala", "como", "sitio",
    "algo", "todo", "nada", "favor", "porfa", "ya", "ahora", 0 };
static const char *const LCM_APPS[] = { "navegador", "internet", "browser", "web", "chrome", "firefox", "terminal", "consola", "shell", "archivos", "explorador", "carpetas",
    "ficheros", "imagen", "visor", "foto", "notas", "bloc", "editor", "notepad", "codigo", "studio", "coder", "programador", "memoria", "agentes", "maquetador", "latex", "paint",
    "dibujo", "pintar", "media", "reproductor", "video", "musica", "pelicula", "ajustes", "configuracion", "preferencias", "pdf", "lector", "monitor", "rendimiento", "grabadora",
    "microfono", "camara", "webcam", "captura", "youtube", "chat", "agente", "programa", "app", 0 };
/* cortesias del final, de la mas larga a la mas corta */
static const char *const LCM_CORTESIA[] = { "si no es molestia", "de una vez", "por favor", "muchas gracias", "ahora mismo", "si puedes", "cuando puedas", "porfa", "xfa", "plis",
    "gracias", "ya", "ahora", "anda", "venga", "rapido", "porfavor", 0 };
static int lcm_corte_cortesia(const uint8_t *s, const struct lcm_tok *tk, int n)
{
    char w[LCM_MAXTOK][32]; for (int i = 0; i < n; ++i) lcm_pliega_tok(s, tk[i], w[i], sizeof w[i]);
    int k = n, quitadas = 0;
    while (k && !lcm_tiene_w(s, tk[k - 1])) --k;
    for (;;) {
        int hallado = 0;
        for (int c = 0; LCM_CORTESIA[c] && !hallado; ++c) {
            char pal[4][16]; int np = 0; const char *q = LCM_CORTESIA[c];
            while (*q && np < 4) { uint32_t l = 0; while (*q && *q != ' ') { if (l + 1 < 16) pal[np][l++] = *q; ++q; } pal[np][l] = 0; ++np; while (*q == ' ') ++q; }
            if (k - np < 0) continue;
            int ok = 1; for (int j = 0; j < np && ok; ++j) if (!lcm_igual(w[k - np + j], pal[j])) ok = 0;
            if (ok) { k -= np; quitadas = n - k; hallado = 1; while (k && !lcm_tiene_w(s, tk[k - 1])) --k; }
        }
        if (!hallado) break;
    }
    return quitadas ? n - quitadas : n;
}
static int lcm_vale(char sl, const uint8_t *s, const struct lcm_tok *tk, int ini, int fin)
{
    int todas = 1; char w[64];
    for (int t = ini; t <= fin; ++t) {
        lcm_pliega_tok(s, tk[t], w, sizeof w);
        int vacia = !lcm_tiene_w(s, tk[t]);
        for (int k = 0; !vacia && LCM_VACIAS[k]; ++k) if (lcm_igual(w, LCM_VACIAS[k])) vacia = 1;
        if (!vacia) todas = 0;
    }
    if (todas) return 0;
    if (sl == 'P') {
        char j[256]; uint32_t p = 0; j[0] = 0;
        for (int t = ini; t <= fin && p + 40 < sizeof j; ++t) { if (t > ini) j[p++] = ' '; p += lcm_pliega_tok(s, tk[t], j + p, sizeof j - p); }
        for (int k = 0; LCM_APPS[k]; ++k) {
            const char *a = LCM_APPS[k];
            for (const char *z = j; *z; ++z) { const char *x = z, *y = a; while (*y && *x == *y) { ++x; ++y; } if (!*y) return 1; }
        }
        return 0;
    }
    return 1;
}

/* 'cuarenta y cinco' -> '45' (como numeros_en_cifras de Python). 'un', 'una', 'uno' sueltos no se tocan. */
static int lcm_palabra_num(const char *w, uint32_t n, int *v)
{
    static const char *const nom[] = { "cero", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve", "diez", "once", "doce", "trece", "catorce", "quince",
        "dieciseis", "diecisiete", "dieciocho", "diecinueve", "veinte", "veintiuno", "veintiun", "veintidos", "veintitres", "veinticuatro", "veinticinco", "veintiseis",
        "veintisiete", "veintiocho", "veintinueve", "cien", "ciento", "treinta", "cuarenta", "cincuenta", "sesenta", "setenta", "ochenta", "noventa", "uno", "un", "una", 0 };
    static const int val[] = { 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 21, 22, 23, 24, 25, 26, 27, 28, 29, 100, 100, 30, 40, 50, 60, 70, 80, 90, 1, 1, 1 };
    char f[24]; uint32_t k = 0, i = 0;
    while (i < n && k + 1 < sizeof f) {
        unsigned char c = (unsigned char)w[i];
        if (c == 0xC3 && i + 1 < n) { unsigned char d = (unsigned char)w[i + 1]; i += 2;
            f[k++] = (char)((d == 0xA1 || d == 0x81) ? 'a' : (d == 0xA9 || d == 0x89) ? 'e' : (d == 0xAD || d == 0x8D) ? 'i' : (d == 0xB3 || d == 0x93) ? 'o' : (d == 0xBA || d == 0x9A) ? 'u' : '?'); continue; }
        if (c == ',' || c == '.' || c == '?' || c == '!') { ++i; continue; }
        f[k++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c); ++i;
    }
    f[k] = 0;
    for (int j = 0; nom[j]; ++j) { const char *a = nom[j], *b = f; while (*a && *a == *b) { ++a; ++b; } if (!*a && !*b) { *v = val[j]; return j; } }
    return -1;
}
static int lcm_cifras(const char *in, char *out, uint32_t cap)
{
    uint32_t p = 0; int cambios = 0; const char *s = in;
    while (*s && p + 12 < cap) {
        if (*s == ' ') { out[p++] = *s++; continue; }
        const char *w = s; while (*s && *s != ' ') ++s;
        int v = 0, j = lcm_palabra_num(w, (uint32_t)(s - w), &v);
        if (j >= 0 && j <= 38) {
            if (v >= 30 && v <= 90) {           /* decena [y unidad] */
                const char *q = s; while (*q == ' ') ++q;
                if ((q[0] == 'y' || q[0] == 'Y') && q[1] == ' ') {
                    const char *u = q + 2; while (*u == ' ') ++u; const char *ue = u; while (*ue && *ue != ' ') ++ue;
                    int vu = 0, ju = lcm_palabra_num(u, (uint32_t)(ue - u), &vu);
                    if (ju >= 0 && vu >= 1 && vu <= 9) { v += vu; s = ue; }
                }
            }
            char d[8]; int nd = 0; do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v && nd < 7);
            while (nd) out[p++] = d[--nd];
            ++cambios;
        } else { while (w < s && p + 1 < cap) out[p++] = *w++; }
    }
    out[p] = 0;
    return cambios;
}

static int lcm_traduce1(const char *texto, struct lcm_res *r);
int lcm_traduce(const char *texto, struct lcm_res *r);
int lcm_traduce(const char *texto, struct lcm_res *r)
{
    int e = lcm_traduce1(texto, r);
    if (e == LCM_FALTA && r->falta[0] == 'N' && !r->falta[1] && lcm_m.ok) {
        static char otra[LCM_MAXIN];
        if (lcm_cifras(texto, otra, sizeof otra)) {
            static struct lcm_res r2;
            lcm_traduce1(otra, &r2);
            *r = r2;
            return r->estado;
        }
    }
    return e;
}

static int64_t lcm_prob_slot(int t, char sl)      /* P(B-sl) + P(I-sl) en la palabra t, en Q20 (promedio del conjunto) */
{
    struct lcm_modelo *m = &lcm_m; int64_t pn = 0;
    for (uint32_t k = 0; k < m->nt; ++k) if (m->tag_slot[k] == sl) pn += lcm_pt[t][k];
    return pn;
}

static int lcm_traduce1(const char *texto, struct lcm_res *r)
{
    static uint8_t s[LCM_MAXIN];
    static struct lcm_tok tk[LCM_MAXTOK];
    struct lcm_modelo *m = &lcm_m;
    uint32_t n0 = 0;
    r->estado = LCM_DUDA; r->intent = -1; r->confirmar = 0; r->p_mil = 0; r->intent_nombre[0] = 0; r->falta[0] = 0; r->orden[0] = 0;
    if (!m->ok || !texto) return r->estado;
    while (texto[n0] && n0 + 4 < sizeof s) { s[n0] = (uint8_t)texto[n0]; ++n0; }
    s[n0] = s[n0 + 1] = s[n0 + 2] = s[n0 + 3] = 0;
    int n = lcm_trocea(s, tk, LCM_MAXTOK);
    if (!n) { r->estado = LCM_LLM; return r->estado; }
    /* cada modelo del conjunto: probabilidades de la orden y de cada etiqueta; se promedian (como en Python) */
    static int32_t pool[4 * LCM_MAXH], li[LCM_MAXIT];
    for (uint32_t i = 0; i < m->ni; ++i) lcm_pi[i] = 0;
    for (int t = 0; t < n; ++t) for (uint32_t k = 0; k < m->nt; ++k) lcm_pt[t][k] = 0;
    for (int mi = 0; mi < lcm_nm; ++mi) {
        lcm_mc = &lcm_ms[mi];
        struct lcm_modelo *mm = lcm_mc;
        uint32_t H = mm->h;
        for (int t = 0; t < n; ++t) lcm_rasgos(s, tk[t], lcm_x[t]);
        lcm_lstm(0, n); lcm_lstm(1, n);
        for (uint32_t k = 0; k < 2 * H; ++k) {          /* orden: max y media de la secuencia */
            int32_t mx = lcm_hs[0][k]; int64_t sum = 0;
            for (int t = 0; t < n; ++t) { if (lcm_hs[t][k] > mx) mx = lcm_hs[t][k]; sum += lcm_hs[t][k]; }
            pool[k] = mx; pool[2 * H + k] = (int32_t)(sum / n);
        }
        lcm_matvec(&mm->wint, pool, li, 0);
        uint32_t bm = 0;
        for (uint32_t i = 0; i < mm->ni; ++i) { li[i] += lcm_i32(mm->bint.v + 4 * i); if (li[i] > li[bm]) bm = i; }
        int64_t z = 0;
        for (uint32_t i = 0; i < mm->ni; ++i) z += lcm_exp20(li[i] - li[bm]);
        for (uint32_t i = 0; i < mm->ni; ++i) lcm_pi[i] += ((lcm_exp20(li[i] - li[bm]) << 20) / (z ? z : 1)) / lcm_nm;
        for (int t = 0; t < n; ++t) {
            lcm_matvec(&mm->wtag, lcm_hs[t], lcm_lg[t], 0);
            int b = 0;
            for (uint32_t k = 0; k < mm->nt; ++k) { lcm_lg[t][k] += lcm_i32(mm->btag.v + 4 * k); if (lcm_lg[t][k] > lcm_lg[t][b]) b = (int)k; }
            int64_t zz = 0;
            for (uint32_t k = 0; k < mm->nt; ++k) zz += lcm_exp20(lcm_lg[t][k] - lcm_lg[t][b]);
            for (uint32_t k = 0; k < mm->nt; ++k) lcm_pt[t][k] += ((lcm_exp20(lcm_lg[t][k] - lcm_lg[t][b]) << 20) / (zz ? zz : 1)) / lcm_nm;
        }
    }
    lcm_mc = &lcm_ms[0];
    uint32_t best = 0;
    for (uint32_t i = 0; i < m->ni; ++i) if (lcm_pi[i] > lcm_pi[best]) best = i;
    r->p_mil = (uint32_t)((lcm_pi[best] * 1000) >> 20);
    r->intent = (int)best;
    { uint32_t p = 0; lcm_pone(r->intent_nombre, sizeof r->intent_nombre, &p, (const char *)m->it_nom[best], m->it_nl[best]); }
    /* piezas: etiqueta por palabra */
    int tag[LCM_MAXTOK];
    int hay_n = 0;
    for (int t = 0; t < n; ++t) {
        int b = 0;
        for (uint32_t k = 0; k < m->nt; ++k) if (lcm_pt[t][k] > lcm_pt[t][b]) b = (int)k;
        tag[t] = b;
        if (m->tag_slot[b] == 'N') hay_n = 1;
    }
    if (hay_n) {       /* una pieza de numero es un numero */
        int mejor = -1; int64_t pm = -1;
        for (int t = 0; t < n; ++t) {
            if (m->tag_slot[tag[t]] == 'N') tag[t] = 0;
            int solo_dig = tk[t].fin > tk[t].ini;
            for (uint32_t q = tk[t].ini; q < tk[t].fin; ++q) if (!lcm_es_dig(s[q])) solo_dig = 0;
            if (!solo_dig) continue;
            int64_t pr = lcm_prob_slot(t, 'N');
            if (pr > pm) { pm = pr; mejor = t; }
        }
        if (mejor >= 0) tag[mejor] = (int)m->bn;
    }
    {   /* archivo con extension o ruta, equipo con punto o cifras, web con punto: si no, la palabra con esa forma mas probable */
        static const char forma[3] = { 'A', 'H', 'U' };
        for (int f = 0; f < 3; ++f) {
            char sl = forma[f]; int prim = -1;
            for (int t = 0; t < n && prim < 0; ++t) if (m->tag_slot[tag[t]] == sl) prim = t;
            if (prim < 0 || lcm_tiene_forma(sl, s, tk[prim])) continue;
            int bsl = -1; for (uint32_t k = 0; k < m->nt; ++k) if (m->tag_slot[k] == sl && m->tag_bi[k] == 'B') bsl = (int)k;
            for (int t = 0; t < n; ++t) if (m->tag_slot[tag[t]] == sl) tag[t] = 0;
            int mejor = -1; int64_t pm = -1;
            for (int t = 0; t < n; ++t) {
                if (m->tag_bi[tag[t]] != 'O' || !lcm_tiene_forma(sl, s, tk[t])) continue;
                int64_t pr = lcm_prob_slot(t, sl);
                if (pr > pm) { pm = pr; mejor = t; }
            }
            if (mejor >= 0 && bsl >= 0) tag[mejor] = bsl;
        }
    }
    if (best == m->llm_id) { r->estado = LCM_LLM; return r->estado; }
    if (r->p_mil < m->umbral_mil) { r->estado = LCM_DUDA; return r->estado; }
    /* cada pieza: el primer tramo que sea un dato de verdad (como en Python); si ninguno, la palabra tras el primero */
    int ini[128], fin_[128];
    for (int k = 0; k < 128; ++k) { ini[k] = -1; fin_[k] = -1; }
    int corte = lcm_corte_cortesia(s, tk, n);
    for (int k = 'A'; k <= 'Z'; ++k) {
        int primero_ult = -2, t = 0;
        while (t < n && ini[k] < 0) {
            char sl = m->tag_slot[tag[t]];
            int empieza = sl == (char)k && (m->tag_bi[tag[t]] == 'B' || t == 0 || m->tag_slot[tag[t - 1]] != (char)k);
            if (!empieza) { ++t; continue; }
            int a0 = t, b0 = t;
            while (b0 + 1 < n && m->tag_slot[tag[b0 + 1]] == (char)k && m->tag_bi[tag[b0 + 1]] == 'I') ++b0;
            t = b0 + 1;
            int a = a0, b = b0 < corte ? b0 : corte - 1;
            if (primero_ult == -2) primero_ult = b >= a ? b : -1;
            while (a <= b && lcm_signo_suelto(s, tk[a])) ++a;
            while (b >= a && lcm_signo_suelto(s, tk[b])) --b;
            if (a <= b && lcm_vale((char)k, s, tk, a, b)) { ini[k] = a; fin_[k] = b; }
        }
        if (ini[k] < 0 && primero_ult >= 0 && k != 'T') {
            int j2 = primero_ult + 1;
            if (j2 < corte && m->tag_bi[tag[j2]] == 'O' && lcm_tiene_w(s, tk[j2]) && lcm_tiene_forma((char)k, s, tk[j2])) {
                char w[64]; lcm_pliega_tok(s, tk[j2], w, sizeof w); int vac = 0;
                for (int q = 0; LCM_VACIAS[q]; ++q) if (lcm_igual(w, LCM_VACIAS[q])) vac = 1;
                if (!vac) { ini[k] = j2; fin_[k] = j2; }
            }
        }
    }
    const uint8_t *tpl = m->it_ord[best]; uint32_t tl = m->it_ol[best];
    for (uint32_t k = 0; k + 2 < tl; ++k)
        if (tpl[k] == '{' && tpl[k + 2] == '}' && ini[tpl[k + 1]] < 0) { r->falta[0] = (char)tpl[k + 1]; r->falta[1] = 0; r->estado = LCM_FALTA; return r->estado; }
    uint32_t p = 0;
    for (uint32_t k = 0; k < tl; ++k) {
        if (k + 2 < tl && tpl[k] == '{' && tpl[k + 2] == '}') {
            int sl = tpl[k + 1];
            int libre = (sl == 'T' && (k + 3 == tl || (k > 0 && tpl[k - 1] == '"'))) || (sl == 'P' && k + 3 == tl);   /* texto final o entre comillas, o la app al final */
            for (int t = ini[sl]; t <= fin_[sl]; ++t) {
                if (t > ini[sl] && s[tk[t].ini] != '.' && s[tk[t].ini] != '/' && !libre) { r->orden[0] = 0; r->estado = LCM_RARO; return r->estado; }
                /* " ".join(...).replace(" .", ".").replace(" /", "/") */
                if (t > ini[sl] && s[tk[t].ini] != '.' && s[tk[t].ini] != '/') lcm_pone(r->orden, sizeof r->orden, &p, " ", 1);
                for (uint32_t q = tk[t].ini; q < tk[t].fin; ++q) {
                    char c = (char)s[q];
                    if (c == ';' || c == '|' || c == '&' || c == '<' || c == '>' || c == '`' || c == '$' || c == '"' || c == '\\') { r->orden[0] = 0; r->estado = LCM_RARO; return r->estado; }
                }
                lcm_pone(r->orden, sizeof r->orden, &p, (const char *)s + tk[t].ini, tk[t].fin - tk[t].ini);
            }
            k += 2;
        } else lcm_pone(r->orden, sizeof r->orden, &p, (const char *)tpl + k, 1);
    }
    {   /* el tipo de archivo decide con que se abre: .pdf -> pdf, imagen -> imagen, el resto -> cat (como en Python) */
        const char *nm = (const char *)m->it_nom[best]; uint32_t nl = m->it_nl[best];
        int es_c = nl == 3 && nm[0] == 'c' && nm[1] == 'a' && nm[2] == 't', es_p = nl == 3 && nm[0] == 'p' && nm[1] == 'd' && nm[2] == 'f';
        int es_i = nl == 6 && nm[0] == 'i' && nm[1] == 'm' && nm[2] == 'a' && nm[3] == 'g' && nm[4] == 'e' && nm[5] == 'n';
        if (es_c || es_p || es_i) {
            const char *arg = r->orden; while (*arg && *arg != ' ') ++arg; while (*arg == ' ') ++arg;
            const char *pt = 0; for (const char *z = arg; *z; ++z) if (*z == '.') pt = z;
            char ext[8]; uint32_t el = 0;
            if (pt) for (const char *z = pt + 1; *z && el + 1 < sizeof ext; ++z) ext[el++] = (char)((*z >= 'A' && *z <= 'Z') ? *z + 32 : *z);
            ext[el] = 0;
            static const char *const imgs[] = { "png", "jpg", "jpeg", "gif", "bmp", "webp", "ppm", "svg", 0 };
            int img = 0; for (int k = 0; imgs[k]; ++k) { const char *a2 = imgs[k], *b2 = ext; while (*a2 && *a2 == *b2) { ++a2; ++b2; } if (!*a2 && !*b2) img = 1; }
            const char *dest = (el == 3 && ext[0] == 'p' && ext[1] == 'd' && ext[2] == 'f') ? "pdf" : img ? "imagen" : "cat";
            int di = -1;
            for (uint32_t k = 0; k < m->ni; ++k) { const char *a2 = (const char *)m->it_nom[k]; uint32_t l2 = m->it_nl[k], q = 0; while (q < l2 && dest[q] && a2[q] == dest[q]) ++q; if (q == l2 && !dest[q]) di = (int)k; }
            if (di >= 0 && (uint32_t)di != best) {
                char a3[480]; uint32_t q = 0; while (arg[q] && q + 1 < sizeof a3) { a3[q] = arg[q]; ++q; } a3[q] = 0;
                uint32_t p2 = 0; r->orden[0] = 0;
                lcm_pone(r->orden, sizeof r->orden, &p2, dest, (uint32_t)(dest[0] == 'i' ? 6 : 3));
                lcm_pone(r->orden, sizeof r->orden, &p2, " ", 1);
                lcm_pone(r->orden, sizeof r->orden, &p2, a3, q);
                best = (uint32_t)di; r->intent = di;
                uint32_t p3 = 0; lcm_pone(r->intent_nombre, sizeof r->intent_nombre, &p3, (const char *)m->it_nom[best], m->it_nl[best]);
            }
        }
    }
    r->confirmar = m->it_pel[best];
    r->estado = LCM_ORDEN;
    return r->estado;
}
