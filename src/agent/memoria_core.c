/* SOMA_277_MEMORIA: nucleo de la memoria (ver memoria_core.h). Sin libc ni coma flotante. */
#include "memoria_core.h"

/* ------------------------------------------------------------------ utilidades */
static uint32_t slen(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static void mcopy(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; while (n--) *a++ = *b++; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int starts(const char *s, const char *p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }
static const char *sfind(const char *h, const char *n)
{
    uint32_t nl = slen(n);
    if (!nl) return h;
    for (; *h; h++) if (*h == *n && starts(h, n)) return h;
    return 0;
}
static void put(char *o, uint32_t cap, uint32_t *p, const char *s) { while (*s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void put_n(char *o, uint32_t cap, uint32_t *p, const char *s, uint32_t n) { while (n-- && *s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
/* copia como mucho n bytes de s sin partir un caracter UTF-8; si recorta, termina en "..." */
static void put_clip(char *o, uint32_t cap, uint32_t *p, const char *s, uint32_t n)
{
    uint32_t l = slen(s);
    if (l <= n) { put(o, cap, p, s); return; }
    while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
    put_n(o, cap, p, s, n); put(o, cap, p, "...");
}
static void put_dec(char *o, uint32_t cap, uint32_t *p, uint32_t v)
{
    char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) { if (*p + 1 < cap) o[(*p)++] = t[--n]; else n--; }
    if (cap) o[*p < cap ? *p : cap - 1] = 0;
}

uint32_t mc_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1)); }
    return ~c;
}

/* ------------------------------------------------------------------ fechas */
uint32_t mc_epoch(int y, int mo, int d, int h, int mi, int s)
{
    y -= mo <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + doe - 719468;
    int64_t t = days * 86400 + h * 3600 + mi * 60 + s;
    return t < 0 ? 0 : (uint32_t)t;
}
void mc_fecha(uint32_t t, char *out, uint32_t cap)
{
    int64_t z = (int64_t)(t / 86400u) + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int d = (int)(doy - (153 * mp + 2) / 5 + 1);
    int mo = (int)(mp < 10 ? mp + 3 : mp - 9);
    y += mo <= 2;
    uint32_t sod = t % 86400u, p = 0;
    if (!cap) return;
    out[0] = 0;
    if (d < 10) put(out, cap, &p, "0");
    put_dec(out, cap, &p, (uint32_t)d); put(out, cap, &p, "/");
    if (mo < 10) put(out, cap, &p, "0");
    put_dec(out, cap, &p, (uint32_t)mo); put(out, cap, &p, "/");
    put_dec(out, cap, &p, (uint32_t)y); put(out, cap, &p, " ");
    if (sod / 3600 < 10) put(out, cap, &p, "0");
    put_dec(out, cap, &p, sod / 3600); put(out, cap, &p, ":");
    if ((sod / 60) % 60 < 10) put(out, cap, &p, "0");
    put_dec(out, cap, &p, (sod / 60) % 60);
}

/* ------------------------------------------------------------------ texto */
/* minusculas, sin acentos, solo [a-z0-9] y un espacio entre palabras. Devuelve la longitud. */
uint32_t mc_fold(const char *s, char *out, uint32_t cap)
{
    uint32_t n = 0; int sep = 1;
    if (!cap) return 0;
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s; char r = 0;
        if (c >= 'A' && c <= 'Z') r = (char)(c + 32);
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) r = (char)c;
        else if (c == 0xC3 && s[1]) {
            unsigned char d = (unsigned char)s[1]; s++;
            switch (d) {
            case 0xA1: case 0xA0: case 0xA2: case 0xA4: case 0x81: case 0x80: case 0x82: case 0x84: r = 'a'; break;
            case 0xA9: case 0xA8: case 0xAA: case 0xAB: case 0x89: case 0x88: case 0x8A: case 0x8B: r = 'e'; break;
            case 0xAD: case 0xAC: case 0xAE: case 0xAF: case 0x8D: case 0x8C: case 0x8E: case 0x8F: r = 'i'; break;
            case 0xB3: case 0xB2: case 0xB4: case 0xB6: case 0x93: case 0x92: case 0x94: case 0x96: r = 'o'; break;
            case 0xBA: case 0xB9: case 0xBB: case 0xBC: case 0x9A: case 0x99: case 0x9B: case 0x9C: r = 'u'; break;
            case 0xB1: case 0x91: r = 'n'; break;
            case 0xA7: case 0x87: r = 'c'; break;
            default: r = 0;
            }
        } else if (c >= 0x80) {                                  /* otro caracter multibyte: separador (se saltan sus bytes de continuacion) */
            while (((unsigned char)s[1] & 0xC0) == 0x80) s++;
            r = 0;
        }
        if (r) { if (n + 1 >= cap) break; out[n++] = r; sep = 0; }
        else if (!sep) { if (n + 1 >= cap) break; out[n++] = ' '; sep = 1; }
    }
    if (n && out[n - 1] == ' ') n--;
    out[n] = 0;
    return n;
}

static uint32_t hash32(const char *s, uint32_t n)
{
    uint32_t h = 0x811C9DC5u;
    while (n--) { h ^= (uint8_t)*s++; h *= 0x01000193u; }
    return h;
}
static int vacia(const char *t, uint32_t n)       /* palabras demasiado comunes para distinguir un recuerdo */
{
    static const char *const V[] = { "que", "del", "los", "las", "una", "por", "con", "para", "como", "mas", "pero", "sus", "este", "esta", "esto", "eso",
        "ese", "fue", "son", "hay", "muy", "ya", "mis", "tus", "nos", "les", "han", "has", "ser", "era", "todo", "toda", "cual", "quien", "donde",
        "cuando", "cuanto", "the", "and", "was", "are", "you", "his", "her", "has", "had", "dime", "dame", "puedes", "tengo", "tiene", "tienes", 0 };
    if (n < 3) return 1;
    for (int i = 0; V[i]; i++) if (slen(V[i]) == n && starts(t, V[i])) return 1;
    return 0;
}

int mc_noFactual(const char *texto)
{
    if (!texto) return 1;
    uint32_t n = slen(texto);
    while (n && (texto[n - 1] == ' ' || texto[n - 1] == '\n' || texto[n - 1] == '\r')) n--;
    if (n < 8) return 1;
    if (texto[n - 1] == '?') return 1;
    if (starts(texto, "\302\277")) return 1;                         /* "¿" */
    char f[96]; mc_fold(texto, f, sizeof f);
    static const char *const S[] = { "hola", "buenas", "buenos dias", "buenas tardes", "buenas noches", "gracias", "muchas gracias", "vale", "adios", "hasta luego",
                                     "perfecto", "genial", "de acuerdo", "entendido", "ok", "muy bien", "estupendo", 0 };
    for (int i = 0; S[i]; i++) if (seq(f, S[i])) return 1;
    if (slen(f) < 28 && (starts(f, "hola ") || starts(f, "gracias ") || starts(f, "buenas ") || starts(f, "vale ") || starts(f, "adios "))) return 1;
    return 0;
}

/* heuristica de memorIA: ¿el hecho nuevo parece un cambio de estado de otro anterior? (mudanza, empleo, preferencia...) */
static int resto_tras(const char *f, const char *clave, char *out, uint32_t cap)     /* texto que sigue a `clave` en f (ya plegado) */
{
    const char *p = sfind(f, clave);
    if (!p) return 0;
    p += slen(clave);
    uint32_t n = 0; while (p[n] && n + 1 < cap) { out[n] = p[n]; n++; }
    out[n] = 0;
    return 1;
}
int mc_parece_actualizacion(const char *viejo, const char *nuevo)
{
    char o[MC_TEXTO_MAX + 8], n[MC_TEXTO_MAX + 8];
    mc_fold(viejo ? viejo : "", o, sizeof o); mc_fold(nuevo ? nuevo : "", n, sizeof n);
    static const char *const cues[] = {
        "mudado", "se ha mudado", "se mudo", "me he mudado", "me mude", "mudanza", "traslado", "trasladado", "me traslade", "he cambiado", "cambio de domicilio",
        "ahora vivo", "ahora trabajo", "ahora estudio", "ya no vivo", "ya no trabajo", "ya no estudio", "a partir de ahora", "desde ahora", "actualmente",
        "mi nuevo empleo", "nuevo domicilio", "cambie de", "deje de", "he dejado de", "empece a", "renuncie", "me despidieron",
        "moved to", "i moved", "changed my", "no longer", "now i live", "now i work", "started working", "left my job", "my new address", "currently live",
        "vive ahora", "vivo ahora", "trabaja ahora", "trabajo ahora", "ahora vive", "ahora trabaja", "fue despedido", "fue despedida", "despedido de", "ya no ",
        "dejo de", "termino la carrera", "se ha graduado", "se graduo", "se caso", "se divorcio", "se separo", "se jubilo", "se ha jubilado",
        "cambio de trabajo", "cambio de ciudad", "nuevo trabajo", 0 };
    for (int i = 0; cues[i]; i++) if (sfind(n, cues[i])) return 1;
    static const char *const claves[] = { "vive en ", "vivo en ", "reside en ", "resido en ", "habita en ", "trabaja en ", "trabajo en ", "trabaja para ", "trabajo para ",
        "estudia en ", "estudio en ", "prefiere ", "prefiero ", "mi domicilio es ", "mi direccion es ", "mi ciudad es ", "mi empresa es ", "mi puesto es ",
        "mi cargo es ", "mi pareja es ", "mi perro se llama ", "mi gato se llama ", "mi mascota se llama ", "mi edad es ", 0 };
    for (int i = 0; claves[i]; i++) {
        char a[200], b[200];
        if (resto_tras(o, claves[i], a, sizeof a) && resto_tras(n, claves[i], b, sizeof b) && a[0] && b[0] && !seq(a, b) && !starts(a, b) && !starts(b, a)) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ arena y elementos */
int mc_init(struct mc *m, uint32_t cap)
{
    if (!m || cap == 0 || cap > MC_MAX_ITEMS) return -1;
    for (uint32_t i = 0; i < sizeof *m; i++) ((uint8_t *)m)[i] = 0;
    m->it = (struct mc_item *)mc_alloc(cap * (uint32_t)sizeof(struct mc_item));
    if (!m->it) return -1;
    m->cap = cap; m->nbloques = 0; m->uso = MC_BLOQUE;
    return 0;
}
void mc_destroy(struct mc *m)
{
    if (!m) return;
    for (uint32_t i = 0; i < m->nbloques; i++) mc_free(m->bloque[i]);
    if (m->it) mc_free(m->it);
    for (uint32_t i = 0; i < sizeof *m; i++) ((uint8_t *)m)[i] = 0;
}
static uint8_t *arena(struct mc *m, uint32_t n)
{
    if (n > MC_BLOQUE) return 0;
    if (m->nbloques == 0 || m->uso + n > MC_BLOQUE) {
        if (m->nbloques >= MC_BLOQUES_MAX) return 0;
        uint8_t *b = (uint8_t *)mc_alloc(MC_BLOQUE);
        if (!b) return 0;
        m->bloque[m->nbloques++] = b; m->uso = 0;
    }
    uint8_t *p = m->bloque[m->nbloques - 1] + m->uso;
    m->uso += n;
    return p;
}
const struct mc_item *mc_get(const struct mc *m, uint32_t id) { return (m && id >= 1 && id <= m->n) ? &m->it[id - 1] : 0; }
static void ensucia(struct mc *m, uint32_t id) { if (id >= 1) m->sucio[(id - 1) / MC_SHARD] = 1; }

static uint32_t add_raw(struct mc *m, int kind, int rol, const char *texto, uint32_t tl, const char *fuente, uint32_t fl, int imp, uint32_t t, uint32_t tick,
                        uint32_t t_hasta, uint32_t sust, int estado, int refuerzos, const int8_t *vec, uint32_t dim)
{
    if (!m || !m->it || m->n >= m->cap || (kind != MC_CELDA && kind != MC_EPISODIO)) return 0;
    if (tl > MC_TEXTO_MAX) { tl = MC_TEXTO_MAX; while (tl > 0 && ((uint8_t)texto[tl] & 0xC0) == 0x80) tl--; }     /* no cortar a mitad de un caracter UTF-8 */
    if (fl > MC_FUENTE_MAX) { fl = MC_FUENTE_MAX; while (fl > 0 && ((uint8_t)fuente[fl] & 0xC0) == 0x80) fl--; }
    if (dim > MC_DIM_MAX) return 0;
    char *tp = (char *)arena(m, tl + 1), *fp = (char *)arena(m, fl + 1);
    int8_t *vp = 0;
    if (!tp || !fp) return 0;
    if (vec && dim) { vp = (int8_t *)arena(m, dim); if (!vp) return 0; mcopy(vp, vec, dim); } else dim = 0;
    if (tl) mcopy(tp, texto, tl);
    tp[tl] = 0;
    if (fl) mcopy(fp, fuente, fl);
    fp[fl] = 0;
    struct mc_item *e = &m->it[m->n];
    e->id = m->n + 1; e->t = t; e->t_hasta = t_hasta; e->sust_por = sust; e->tick = tick;
    e->texto = tp; e->fuente = fp; e->vec = vp; e->tlen = (uint16_t)tl; e->flen = (uint16_t)fl; e->dim = (uint16_t)dim;
    e->kind = (uint8_t)kind; e->estado = (uint8_t)estado; e->imp = (uint8_t)(imp < 1 ? 1 : imp > 10 ? 10 : imp); e->rol = (uint8_t)rol;
    e->refuerzos = (uint8_t)refuerzos; e->nodo = 0; e->_pad[0] = e->_pad[1] = 0;
    m->n++;
    return e->id;
}
uint32_t mc_add(struct mc *m, int kind, int rol, const char *texto, const char *fuente, int imp, uint32_t t, uint32_t tick, const int8_t *vec, uint32_t dim)
{
    uint32_t id = add_raw(m, kind, rol, texto ? texto : "", slen(texto), fuente ? fuente : "", slen(fuente), imp, t, tick, 0, 0, MC_ACTIVO, 0, vec, dim);
    if (id) ensucia(m, id);
    return id;
}
int mc_set_vec(struct mc *m, uint32_t id, const int8_t *vec, uint32_t dim)
{
    if (!mc_get(m, id) || !vec || !dim || dim > MC_DIM_MAX) return -1;
    int8_t *vp = (int8_t *)arena(m, dim);
    if (!vp) return -1;
    mcopy(vp, vec, dim);
    m->it[id - 1].vec = vp; m->it[id - 1].dim = (uint16_t)dim;
    ensucia(m, id);
    return 0;
}
int mc_set_estado(struct mc *m, uint32_t id, int estado, uint32_t t_hasta, uint32_t sust_por)
{
    if (!mc_get(m, id)) return -1;
    m->it[id - 1].estado = (uint8_t)estado; m->it[id - 1].t_hasta = t_hasta; m->it[id - 1].sust_por = sust_por;
    ensucia(m, id);
    return 0;
}
int mc_refuerza(struct mc *m, uint32_t id, uint32_t tick)
{
    if (!mc_get(m, id)) return -1;
    struct mc_item *e = &m->it[id - 1];
    if (e->imp < 10) e->imp++;
    if (e->refuerzos < 255) e->refuerzos++;
    e->tick = tick;
    ensucia(m, id);
    return 0;
}
uint32_t mc_find_exacto(const struct mc *m, int kind, const char *texto)
{
    char a[MC_TEXTO_MAX + 8], b[MC_TEXTO_MAX + 8];
    uint32_t la = mc_fold(texto, a, sizeof a);
    if (!la) return 0;
    for (uint32_t i = 0; i < m->n; i++) {
        const struct mc_item *e = &m->it[i];
        if (e->kind != kind || e->estado != MC_ACTIVO) continue;
        if (e->tlen < la / 2 || e->tlen > la * 2 + 16) continue;             /* descarte barato: la longitud plegada no puede diferir tanto */
        if (mc_fold(e->texto, b, sizeof b) == la && seq(a, b)) return e->id;
    }
    return 0;
}

void mc_stats(const struct mc *m, uint32_t *celdas, uint32_t *episodios, uint32_t *archivadas, uint32_t *sustituidas, uint32_t *sin_vec)
{
    uint32_t c = 0, e = 0, a = 0, s = 0, v = 0;
    for (uint32_t i = 0; i < m->n; i++) {
        const struct mc_item *x = &m->it[i];
        if (x->estado == MC_ARCHIVADO) a++; else if (x->estado == MC_SUSTITUIDO) s++;
        else if (x->kind == MC_CELDA) c++; else e++;
        if (!x->vec) v++;
    }
    if (celdas) *celdas = c;
    if (episodios) *episodios = e;
    if (archivadas) *archivadas = a;
    if (sustituidas) *sustituidas = s;
    if (sin_vec) *sin_vec = v;
}

/* ------------------------------------------------------------------ vectores */
int32_t mc_dot(const int8_t *a, const int8_t *b, uint32_t dim)
{
    int32_t s = 0;
    for (uint32_t i = 0; i < dim; i++) s += (int32_t)a[i] * (int32_t)b[i];
    return s;
}
static uint32_t isqrt64(uint64_t v)
{
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return (uint32_t)r;
}
int mc_parse_embedding(const char *json, int8_t *out, uint32_t max_dim)
{
    static const int64_t P10[] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000LL, 10000000000LL, 100000000000LL,
                                   1000000000000LL, 10000000000000LL, 100000000000000LL, 1000000000000000LL };
    if (!json || !out || !max_dim) return -1;
    const char *p = sfind(json, "\"embedding\"");
    if (!p) return -1;
    p += 11;
    while (*p && *p != '[') p++;
    if (!*p) return -1;
    while (*p == '[' || *p == ' ' || *p == '\n') p++;
    static int32_t q[MC_DIM_MAX];            /* Q20 */
    uint32_t n = 0;
    while (*p && *p != ']') {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p == ']') break;
        int neg = 0; if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
        int64_t mant = 0; int digs = 0, frac = 0, any = 0, e10 = 0, dot = 0;
        while ((*p >= '0' && *p <= '9') || *p == '.') {
            if (*p == '.') { if (dot) break; dot = 1; p++; continue; }
            any = 1;
            if (digs < 9) { mant = mant * 10 + (*p - '0'); if (mant) digs++; if (dot) frac++; }
            else if (!dot) e10++;                                                   /* digito entero que no cabe: pesa 10 */
            p++;
        }
        if (!any) return -1;
        if (*p == 'e' || *p == 'E') {
            p++; int en = 0, ev = 0; if (*p == '-') { en = 1; p++; } else if (*p == '+') p++;
            while (*p >= '0' && *p <= '9') { if (ev < 1000) ev = ev * 10 + (*p - '0'); p++; }
            e10 += en ? -ev : ev;
        }
        e10 -= frac;
        int64_t v = mant << 20;
        if (e10 < 0) { int k = -e10; if (k > 15) v = 0; else v /= P10[k]; }
        else if (e10 > 0) { v = e10 > 6 ? ((int64_t)1 << 40) : v * P10[e10]; }
        if (v > ((int64_t)1 << 30)) v = (int64_t)1 << 30;
        if (n >= max_dim || n >= MC_DIM_MAX) return -1;
        q[n++] = (int32_t)(neg ? -v : v);
        while (*p && *p != ',' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r') { if (*p == '[') return -1; p++; }
    }
    if (n < 8) return -1;
    uint64_t ss = 0; for (uint32_t i = 0; i < n; i++) ss += (uint64_t)((int64_t)q[i] * q[i]);
    uint32_t nr = isqrt64(ss);
    if (!nr) return -1;
    for (uint32_t i = 0; i < n; i++) {
        int64_t v = ((int64_t)q[i] * 127 * 2 + (q[i] >= 0 ? (int64_t)nr : -(int64_t)nr)) / (2 * (int64_t)nr);
        out[i] = (int8_t)(v > 127 ? 127 : v < -127 ? -127 : v);
    }
    return (int)n;
}

/* ------------------------------------------------------------------ busqueda */
struct top { uint32_t id; int32_t sc; };
static void top_mete(struct top *t, uint32_t *n, uint32_t k, uint32_t id, int32_t sc)
{
    uint32_t i = *n;
    if (i == k && sc <= t[k - 1].sc) return;
    if (i < k) (*n)++; else i = k - 1;
    while (i > 0 && t[i - 1].sc < sc) { t[i] = t[i - 1]; i--; }
    t[i].id = id; t[i].sc = sc;
}
static int visible(const struct mc_item *e, uint32_t kindmask, int historial)
{
    if (!((kindmask >> e->kind) & 1u)) return 0;
    return historial || e->estado == MC_ACTIVO;
}
static uint32_t ilog2(uint32_t v) { uint32_t r = 0; while (v >>= 1) r++; return r; }

int mc_vecinos(const struct mc *m, const int8_t *qvec, uint32_t qdim, uint32_t k, uint32_t *ids, int32_t *dots)
{
    struct top t[MC_K_MAX]; uint32_t n = 0;
    if (!m || !qvec || !qdim || !k) return 0;
    if (k > MC_K_MAX) k = MC_K_MAX;
    for (uint32_t i = 0; i < m->n; i++) {
        const struct mc_item *e = &m->it[i];
        if (e->kind != MC_CELDA || e->estado != MC_ACTIVO || !e->vec || e->dim != qdim) continue;
        top_mete(t, &n, k, e->id, mc_dot(qvec, e->vec, qdim));
    }
    for (uint32_t i = 0; i < n; i++) { ids[i] = t[i].id; if (dots) dots[i] = t[i].sc; }
    return (int)n;
}

int mc_busca(const struct mc *m, const int8_t *qvec, uint32_t qdim, const char *qtext, uint32_t kindmask, int historial, int minsim,
             uint32_t k, uint32_t *ids, int32_t *score)
{
    struct top t[MC_K_MAX]; uint32_t n = 0;
    if (!m || !k || !kindmask) return 0;
    if (k > MC_K_MAX) k = MC_K_MAX;
    int usa_vec = qvec && qdim;
    /* canal vectorial */
    if (usa_vec) {
        for (uint32_t i = 0; i < m->n; i++) {
            const struct mc_item *e = &m->it[i];
            if (!visible(e, kindmask, historial) || !e->vec || e->dim != qdim) continue;
            int32_t d = mc_dot(qvec, e->vec, qdim);
            if (d >= minsim) top_mete(t, &n, k, e->id, d);
        }
    }
    uint32_t nv = n;
    for (uint32_t i = 0; i < nv; i++) { ids[i] = t[i].id; score[i] = t[i].sc; }
    /* canal lexical: todo si no hay vector de consulta; si no, solo lo que aun no tiene vector (pendiente de embedding) */
    if (qtext && *qtext && nv < k) {
        char f[MC_TEXTO_MAX + 8]; uint32_t qh[16]; uint32_t nq = 0;
        mc_fold(qtext, f, 512);
        for (const char *p = f; *p && nq < 16;) {
            const char *s = p; while (*p && *p != ' ') p++;
            uint32_t l = (uint32_t)(p - s);
            if (!vacia(s, l)) { uint32_t h = hash32(s, l); int dup = 0; for (uint32_t j = 0; j < nq; j++) if (qh[j] == h) dup = 1; if (!dup) qh[nq++] = h; }
            while (*p == ' ') p++;
        }
        if (nq && m->n) {
            uint16_t *mask = (uint16_t *)mc_alloc(m->n * (uint32_t)sizeof(uint16_t));
            if (mask) {
                uint32_t df[16] = { 0 }, N = 0;
                for (uint32_t i = 0; i < m->n; i++) {
                    const struct mc_item *e = &m->it[i]; mask[i] = 0;
                    if (!visible(e, kindmask, historial)) continue;
                    if (usa_vec && e->vec && e->dim == qdim) continue;
                    N++;
                    mc_fold(e->texto, f, sizeof f);
                    for (const char *p = f; *p;) {
                        const char *s = p; while (*p && *p != ' ') p++;
                        uint32_t h = hash32(s, (uint32_t)(p - s));
                        for (uint32_t j = 0; j < nq; j++) if (qh[j] == h) mask[i] |= (uint16_t)(1u << j);
                        while (*p == ' ') p++;
                    }
                    for (uint32_t j = 0; j < nq; j++) if (mask[i] & (1u << j)) df[j]++;
                }
                uint32_t idf[16];
                for (uint32_t j = 0; j < nq; j++) idf[j] = 1 + ilog2(((N + 1) << 4) / (df[j] + 1) + 1);
                struct top l[MC_K_MAX]; uint32_t nl = 0;
                for (uint32_t i = 0; i < m->n; i++) {
                    if (!mask[i]) continue;
                    uint32_t pts = 0, hit = 0;
                    for (uint32_t j = 0; j < nq; j++) if (mask[i] & (1u << j)) { pts += idf[j] * 4; hit++; }
                    if (nq >= 3 && hit * 2 < nq) continue;                                   /* con varias palabras, al menos la mitad deben coincidir */
                    top_mete(l, &nl, k, m->it[i].id, (int32_t)pts);
                }
                mc_free(mask);
                for (uint32_t i = 0; i < nl && n < k; i++) {
                    int dup = 0; for (uint32_t j = 0; j < n; j++) if (ids[j] == l[i].id) dup = 1;
                    if (!dup) { ids[n] = l[i].id; score[n] = -(1 + l[i].sc); n++; }
                }
                return (int)n;
            }
        }
    }
    return (int)nv;
}

/* ------------------------------------------------------------------ JSON minimo */
static const char *ws(const char *p) { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++; return p; }
static int hex4(const char *p, uint32_t *v)
{
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i]; uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0'); else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10); else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10); else return 0;
        r = r * 16 + d;
    }
    *v = r; return 1;
}
static uint32_t utf8(uint32_t cp, char *o)
{
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}
/* p apunta a la comilla de apertura; deja en out la cadena sin escapes (saltos de linea -> espacio) y devuelve lo que sigue a la de cierre, o 0 */
static const char *jstr(const char *p, char *out, uint32_t cap)
{
    uint32_t n = 0;
    if (*p != '"') return 0;
    p++;
    while (*p && *p != '"') {
        char c = *p; char tmp[4]; uint32_t tl = 1;
        if (c == '\\') {
            p++;
            switch (*p) {
            case 'n': case 'r': case 't': tmp[0] = ' '; break;
            case 'u': {
                uint32_t cp;
                if (!hex4(p + 1, &cp)) return 0;
                p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && p[1] == '\\' && p[2] == 'u') { uint32_t lo; if (hex4(p + 3, &lo) && lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; } }
                tl = utf8(cp, tmp); break;
            }
            case 0: return 0;
            default: tmp[0] = *p;
            }
        } else tmp[0] = c;
        if (n + tl + 1 <= cap) { for (uint32_t i = 0; i < tl; i++) out[n++] = tmp[i]; }
        p++;
    }
    if (*p != '"') return 0;
    if (cap) out[n < cap ? n : cap - 1] = 0;
    return p + 1;
}
static const char *jskip(const char *p)       /* salta un valor JSON cualquiera */
{
    p = ws(p);
    if (*p == '"') { char t[2]; const char *e = jstr(p, t, 1); return e; }
    if (*p == '{' || *p == '[') {
        char open = *p, close = open == '{' ? '}' : ']'; int d = 0;
        for (; *p; p++) {
            if (*p == '"') { p++; while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; } if (!*p) return 0; continue; }
            if (*p == open) d++; else if (*p == close) { d--; if (!d) return p + 1; }
        }
        return 0;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n') p++;
    return p;
}
static int jnum100(const char *p)           /* "8", "8.5", "0.85" -> centesimas */
{
    int ip = 0, fr = 0, fd = 0;
    p = ws(p);
    if (*p == '"') p++;
    while (*p >= '0' && *p <= '9') { if (ip < 1000) ip = ip * 10 + (*p - '0'); p++; }
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9' && fd < 2) { fr = fr * 10 + (*p - '0'); fd++; p++; } if (fd == 1) fr *= 10; }
    return ip * 100 + fr;
}

int mc_parse_hechos(const char *reply, struct mc_hecho *out, int max)
{
    if (!reply || !out || max <= 0) return -1;
    const char *p = sfind(reply, "\"hechos\"");
    if (!p) return -1;
    p = ws(p + 8);
    if (*p != ':') return -1;
    p = ws(p + 1);
    if (*p != '[') return -1;
    p = ws(p + 1);
    int n = 0;
    while (*p && *p != ']') {
        struct mc_hecho h; h.texto[0] = 0; h.imp = 6; h.instruccion = 0;
        if (*p == '"') {
            char t[600]; const char *e = jstr(p, t, sizeof t); if (!e) return n ? n : -1;
            uint32_t i = 0; while (t[i] && i + 1 < sizeof h.texto) { h.texto[i] = t[i]; i++; } h.texto[i] = 0;
            p = e;
        } else if (*p == '{') {
            p = ws(p + 1);
            while (*p && *p != '}') {
                char key[32]; const char *e = jstr(p, key, sizeof key); if (!e) return n ? n : -1;
                p = ws(e); if (*p != ':') return n ? n : -1;
                p = ws(p + 1);
                if (seq(key, "texto")) {
                    char t[600]; e = jstr(p, t, sizeof t); if (!e) return n ? n : -1;
                    uint32_t i = 0; while (t[i] && i + 1 < sizeof h.texto) { h.texto[i] = t[i]; i++; } h.texto[i] = 0;
                    p = e;
                } else if (seq(key, "importancia")) {
                    int v = jnum100(p) / 100; h.imp = v < 1 ? 1 : v > 10 ? 10 : v; p = jskip(p); if (!p) return n ? n : -1;
                } else if (seq(key, "tipo")) {
                    char t[24]; e = jstr(p, t, sizeof t); if (e) { h.instruccion = starts(t, "instruccion") || starts(t, "instrucci"); p = e; } else { p = jskip(p); if (!p) return n ? n : -1; }
                } else { p = jskip(p); if (!p) return n ? n : -1; }
                p = ws(p); if (*p == ',') p = ws(p + 1);
            }
            if (*p == '}') p++;
        } else { p = jskip(p); if (!p) break; }
        /* texto limpio: espacios colapsados, minimo 3 caracteres */
        char c[600]; uint32_t cn = 0; int sp = 1;
        for (uint32_t i = 0; h.texto[i] && cn + 1 < sizeof c; i++) { char ch = h.texto[i]; if (ch == ' ' || ch == '\n' || ch == '\t') { if (!sp) c[cn++] = ' '; sp = 1; } else { c[cn++] = ch; sp = 0; } }
        while (cn && c[cn - 1] == ' ') cn--;
        c[cn] = 0;
        if (cn >= 3 && n < max) { mcopy(h.texto, c, cn + 1); out[n++] = h; }
        p = ws(p); if (*p == ',') p = ws(p + 1);
    }
    return n;
}

int mc_parse_relacion(const char *reply, int *conf100)
{
    int rel = MC_REL_NINGUNA;
    if (conf100) *conf100 = 0;
    if (!reply) return rel;
    const char *p = sfind(reply, "\"relation\"");
    if (p) {
        p = ws(p + 10);
        if (*p == ':') {
            p = ws(p + 1);
            char v[32]; if (jstr(p, v, sizeof v)) {
                if (starts(v, "supersedes")) rel = MC_REL_SUSTITUYE; else if (starts(v, "same")) rel = MC_REL_IGUAL; else if (starts(v, "complementary")) rel = MC_REL_COMPLEMENTA;
                else if (starts(v, "conflict")) rel = MC_REL_CONFLICTO; else if (starts(v, "unrelated")) rel = MC_REL_NO_RELACIONADO;
            }
        }
    }
    p = sfind(reply, "\"confidence\"");
    if (p && conf100) { p = ws(p + 12); if (*p == ':') { int c = jnum100(p + 1); *conf100 = c > 100 ? 100 : c; } }
    return rel;
}

/* ------------------------------------------------------------------ prompts */
int mc_prompt_hechos(const char *texto, char *out, uint32_t cap)
{
    uint32_t p = 0;
    if (!cap) return 0;
    out[0] = 0;
    put(out, cap, &p, "Analiza el siguiente texto para incorporarlo al \303\241rbol de conocimiento de memoria:\n<<<");
    put_n(out, cap, &p, texto ? texto : "", 6000);
    put(out, cap, &p, ">>>\n\nExtrae TODOS los hechos, principios, conceptos clave o datos sustanciales y duraderos.\nReglas fundamentales:\n"
        "1. Hechos personales: si habla del usuario, red\303\241\143talo en tercera persona: 'El usuario...'.\n"
        "2. Conocimiento t\303\251\143nico/acad\303\251mico: cada concepto como afirmaci\303\263n at\303\263mica, autosuficiente y universal.\n"
        "3. Ignora saludos, preguntas, \303\263rdenes, relleno conversacional, botones web, enlaces y n\303\272meros de p\303\241gina. "
        "Si no hay nada que memorizar, o el texto es una pregunta, una orden o una petici\303\263n, devuelve \"hechos\": []. "
        "Cada hecho debe contener el dato CONCRETO (nombre, lugar, cifra); nunca hechos vagos como \"vive en una ubicaci\303\263n espec\303\255fica\".\n"
        "4. Importancia (1 a 10): 10 = identidad o directivas fundamentales; 7-9 = principios clave; 4-6 = secundarios; 1-3 = menores.\n"
        "5. Tipo: 'dato' o 'instruccion'.\n"
        "Devuelve EXCLUSIVAMENTE este JSON: {\"hechos\":[{\"texto\":\"...\",\"importancia\":8,\"tipo\":\"dato\"}]}");
    return (int)p;
}
int mc_prompt_relacion(const char *viejo, uint32_t id_viejo, const char *nuevo, const char *fecha, char *out, uint32_t cap)
{
    uint32_t p = 0;
    if (!cap) return 0;
    out[0] = 0;
    put(out, cap, &p, "CLASIFICA LA RELACI\303\223N TEMPORAL ENTRE DOS HECHOS DE UNA MEMORIA PERSONAL.\nHecho previo (id=");
    put_dec(out, cap, &p, id_viejo); put(out, cap, &p, "): "); put_n(out, cap, &p, viejo, MC_TEXTO_MAX);
    put(out, cap, &p, "\nHecho nuevo (fecha de vigencia propuesta="); put(out, cap, &p, fecha); put(out, cap, &p, "): "); put_n(out, cap, &p, nuevo, MC_TEXTO_MAX);
    put(out, cap, &p, "\n\nDevuelve SOLO JSON con relation, confidence y reason. relation debe ser una de: 'supersedes', 'same', 'complementary', 'conflict' o 'unrelated'.\n"
        "Usa 'supersedes' \303\232NICAMENTE si ambos hechos se refieren al mismo sujeto y atributo/estado, y el nuevo expresa un cambio temporal de estado "
        "(por ejemplo, mudanza de domicilio, nuevo empleo o cambio de preferencia expl\303\255cito).\n"
        "{\"relation\":\"supersedes|same|complementary|conflict|unrelated\",\"confidence\":0.0,\"reason\":\"...\"}");
    return (int)p;
}

/* ------------------------------------------------------------------ contexto para el prompt */
uint32_t mc_contexto(const struct mc *m, const uint32_t *celdas, uint32_t nc, const uint32_t *episodios, uint32_t ne, char *out, uint32_t cap)
{
    uint32_t p = 0; char f[24];
    if (!cap) return 0;
    out[0] = 0;
    if (nc) {
        put(out, cap, &p, "\n[MEMORIA A LARGO PLAZO: hechos consolidados; cada uno indica de qui\303\251n es y su fuente]:\n");
        for (uint32_t i = 0; i < nc; i++) {
            const struct mc_item *e = mc_get(m, celdas[i]); if (!e) continue;
            put(out, cap, &p, "- "); put(out, cap, &p, e->texto);
            mc_fecha(e->t, f, sizeof f); put(out, cap, &p, " (desde "); put_n(out, cap, &p, f, 10);
            if (e->flen) { put(out, cap, &p, "; fuente: "); put(out, cap, &p, e->fuente); }
            put(out, cap, &p, ")\n");
        }
    }
    if (ne) {
        put(out, cap, &p, "\n[MEMORIA EPIS\303\223\104ICA: mensajes literales anteriores relacionados con la pregunta]:\n");
        for (uint32_t i = 0; i < ne; i++) {
            const struct mc_item *e = mc_get(m, episodios[i]); if (!e) continue;
            mc_fecha(e->t, f, sizeof f);
            put(out, cap, &p, "- ["); put(out, cap, &p, f); put(out, cap, &p, "] ");
            put(out, cap, &p, e->rol == MC_ROL_ASISTENTE ? "Asistente: " : e->rol == MC_ROL_DOC ? "Documento: " : "Usuario: ");
            put_clip(out, cap, &p, e->texto, MC_EPI_CLIP); put(out, cap, &p, "\n");
        }
    }
    return p;
}

uint32_t mc_contexto_charla(const struct mc *m, const uint32_t *ids, uint32_t n, char *out, uint32_t cap)
{
    uint32_t p = 0; char f[24];
    if (!cap) return 0;
    out[0] = 0;
    if (!n) return 0;
    put(out, cap, &p, "\n[MENSAJES RECIENTES DE ESTA CHARLA]:\n");
    for (uint32_t i = 0; i < n; i++) {
        const struct mc_item *e = mc_get(m, ids[i]); if (!e) continue;
        mc_fecha(e->t, f, sizeof f);
        put(out, cap, &p, "["); put(out, cap, &p, f); put(out, cap, &p, "] ");
        put(out, cap, &p, e->rol == MC_ROL_ASISTENTE ? "Asistente: " : "Usuario: ");
        put_clip(out, cap, &p, e->texto, MC_EPI_CLIP); put(out, cap, &p, "\n");
    }
    return p;
}

/* ------------------------------------------------------------------ guardado por fragmentos */
uint32_t mc_nfrag(const struct mc *m) { return (m->n + MC_SHARD - 1) / MC_SHARD; }
uint32_t mc_frag_sucio(const struct mc *m, uint32_t idx) { return idx < MC_MAX_SHARDS ? m->sucio[idx] : 0; }
void mc_frag_limpio(struct mc *m, uint32_t idx) { if (idx < MC_MAX_SHARDS) m->sucio[idx] = 0; }
uint32_t mc_frag_max_bytes(void) { return 16 + MC_SHARD * (32 + MC_TEXTO_MAX + 1 + MC_FUENTE_MAX + 1 + MC_DIM_MAX) + 4; }

static void w32(uint8_t *b, uint32_t *p, uint32_t v) { b[(*p)++] = (uint8_t)v; b[(*p)++] = (uint8_t)(v >> 8); b[(*p)++] = (uint8_t)(v >> 16); b[(*p)++] = (uint8_t)(v >> 24); }
static void w16(uint8_t *b, uint32_t *p, uint32_t v) { b[(*p)++] = (uint8_t)v; b[(*p)++] = (uint8_t)(v >> 8); }
static uint32_t r32(const uint8_t *b) { return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
static uint32_t r16(const uint8_t *b) { return (uint32_t)b[0] | ((uint32_t)b[1] << 8); }

int mc_frag_guarda(const struct mc *m, uint32_t idx, uint8_t *buf, uint32_t cap)
{
    uint32_t a = idx * MC_SHARD, z = a + MC_SHARD; if (z > m->n) z = m->n;
    if (idx >= MC_MAX_SHARDS || a >= m->n || cap < 16) return -1;
    uint32_t p = 0;
    buf[p++] = 'S'; buf[p++] = 'M'; buf[p++] = 'S'; buf[p++] = '1';
    w32(buf, &p, idx); w32(buf, &p, z - a);
    for (uint32_t i = a; i < z; i++) {
        const struct mc_item *e = &m->it[i];
        if (p + 32 + e->tlen + e->flen + e->dim + 4 > cap) return -1;
        w32(buf, &p, e->id); w32(buf, &p, e->t); w32(buf, &p, e->t_hasta); w32(buf, &p, e->sust_por); w32(buf, &p, e->tick);
        w16(buf, &p, e->tlen); w16(buf, &p, e->flen); w16(buf, &p, e->dim);
        buf[p++] = e->kind; buf[p++] = e->estado; buf[p++] = e->imp; buf[p++] = e->rol; buf[p++] = e->refuerzos; buf[p++] = e->nodo;      /* SOMA_279_SUENO: antes iba a 0 */
        mcopy(buf + p, e->texto, e->tlen); p += e->tlen;
        mcopy(buf + p, e->fuente, e->flen); p += e->flen;
        if (e->dim) { mcopy(buf + p, e->vec, e->dim); p += e->dim; }
    }
    uint32_t c = mc_crc32(buf, p);
    w32(buf, &p, c);
    return (int)p;
}

int mc_frag_carga(struct mc *m, const uint8_t *buf, uint32_t len)
{
    if (!m || len < 16 + 4 || buf[0] != 'S' || buf[1] != 'M' || buf[2] != 'S' || buf[3] != '1') return -1;
    if (mc_crc32(buf, len - 4) != r32(buf + len - 4)) return -2;
    uint32_t idx = r32(buf + 4), cnt = r32(buf + 8), p = 12;
    if (idx != m->n / MC_SHARD || m->n % MC_SHARD || cnt > MC_SHARD) return -3;
    for (uint32_t i = 0; i < cnt; i++) {
        if (p + 32 + 4 > len) return -4;
        uint32_t id = r32(buf + p), t = r32(buf + p + 4), th = r32(buf + p + 8), su = r32(buf + p + 12), tk = r32(buf + p + 16);
        uint32_t tl = r16(buf + p + 20), fl = r16(buf + p + 22), dm = r16(buf + p + 24);
        uint8_t kind = buf[p + 26], est = buf[p + 27], imp = buf[p + 28], rol = buf[p + 29], rf = buf[p + 30], nodo = buf[p + 31];
        p += 32;
        if (id != m->n + 1 || tl > MC_TEXTO_MAX || fl > MC_FUENTE_MAX || dm > MC_DIM_MAX || p + tl + fl + dm + 4 > len) return -5;
        if (add_raw(m, kind, rol, (const char *)buf + p, tl, (const char *)buf + p + tl, fl, imp, t, tk, th, su, est, rf,
                    dm ? (const int8_t *)(buf + p + tl + fl) : 0, dm) != id) return -6;
        m->it[id - 1].nodo = nodo;
        p += tl + fl + dm;
    }
    return p + 4 == len ? 0 : -7;
}

/* ================================================================== SOMA_279_SUENO: arbol, olvido y ciclo de sueno */
static int nombre_igual(const char *a, const char *b)
{
    char fa[96], fb[96];
    mc_fold(a, fa, sizeof fa); mc_fold(b, fb, sizeof fb);
    return fa[0] && seq(fa, fb);
}
static void nombre_limpio(const char *a, const char *z, char *out)           /* [a,z) sin controles, recortado sin partir UTF-8 */
{
    uint32_t n = 0;
    while (a < z && n < MC_NODO_NOMBRE - 1) { char c = *a++; out[n++] = ((uint8_t)c < 0x20) ? ' ' : c; }
    if (a < z) while (n > 0 && ((uint8_t)out[n] & 0xC0) == 0x80) n--;      /* se recorto: no dejar medio caracter */
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
}
static uint32_t nodo_libre(const struct mc *m) { for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) if (!m->nodo[i].usado) return i; return 0; }
static uint32_t nodo_hijo(struct mc *m, uint32_t padre, uint32_t nivel, const char *nombre, int crear)
{
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++)
        if (m->nodo[i].usado && m->nodo[i].padre == padre && m->nodo[i].nivel == nivel && nombre_igual(m->nodo[i].nombre, nombre)) return i;
    if (!crear) return 0;
    uint32_t id = nodo_libre(m);
    if (!id) return 0;
    struct mc_nodo *n = &m->nodo[id];
    for (uint32_t i = 0; i < sizeof *n; i++) ((uint8_t *)n)[i] = 0;
    n->usado = 1; n->padre = (uint8_t)padre; n->nivel = (uint8_t)nivel; n->sucio = 1;
    mcopy(n->nombre, nombre, slen(nombre) + 1);
    m->arbol_sucio = 1;
    return id;
}
uint32_t mc_nodo_ruta(struct mc *m, const char *ruta, int crear)
{
    uint32_t padre = 0, nivel = 0; const char *p = ruta;
    if (!m || !ruta) return 0;
    while (nivel < MC_NIVELES) {
        while (*p == ' ' || *p == '/') p++;
        const char *a = p; while (*p && *p != '/') p++;
        const char *z = p; while (z > a && z[-1] == ' ') z--;
        if (z == a) break;
        char nom[MC_NODO_NOMBRE]; nombre_limpio(a, z, nom);
        if (!nom[0]) break;
        uint32_t id = nodo_hijo(m, padre, nivel, nom, 0), nuevo = 0;
        if (!id) { if (!crear) return 0; id = nodo_hijo(m, padre, nivel, nom, 1); nuevo = 1; }
        if (!id) return padre;                                                  /* ya no caben mas nodos: se queda en el ultimo */
        if (nuevo && padre) m->nodo[padre].sucio = 1;
        padre = id; nivel++;
    }
    return padre;
}
void mc_nodo_ruta_texto(const struct mc *m, uint32_t nodo, char *out, uint32_t cap)
{
    uint32_t cadena[MC_NIVELES + 1], nc = 0, p = 0;
    if (cap) out[0] = 0;
    while (nodo && nodo <= MC_NODOS_MAX && m->nodo[nodo].usado && nc < MC_NIVELES) { cadena[nc++] = nodo; nodo = m->nodo[nodo].padre; }
    for (uint32_t i = nc; i > 0; i--) { put(out, cap, &p, m->nodo[cadena[i - 1]].nombre); if (i > 1) put(out, cap, &p, "/"); }
}
int mc_asigna(struct mc *m, uint32_t id, uint32_t nodo)
{
    if (!mc_get(m, id) || nodo > MC_NODOS_MAX || (nodo && !m->nodo[nodo].usado)) return -1;
    struct mc_item *e = &m->it[id - 1];
    if (e->kind != MC_CELDA) return -1;
    if (e->nodo && m->nodo[e->nodo].usado) m->nodo[e->nodo].sucio = 1;
    if (nodo) m->nodo[nodo].sucio = 1;
    e->nodo = (uint8_t)nodo;
    ensucia(m, id); m->arbol_sucio = 1;
    return 0;
}
uint32_t mc_recuenta(struct mc *m)
{
    uint32_t usados = 0;
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) { m->nodo[i].cuenta = 0; if (m->nodo[i].usado) usados++; }
    for (uint32_t i = 0; i < m->n; i++) {
        const struct mc_item *e = &m->it[i];
        if (e->kind == MC_CELDA && e->estado == MC_ACTIVO && e->nodo && m->nodo[e->nodo].usado && m->nodo[e->nodo].cuenta < 65535) m->nodo[e->nodo].cuenta++;
    }
    return usados;
}
uint32_t mc_nodo_hijos(const struct mc *m, uint32_t nodo)
{
    uint32_t n = 0;
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) if (m->nodo[i].usado && m->nodo[i].padre == nodo) n++;
    return n;
}
uint32_t mc_poda_nodos(struct mc *m)
{
    uint32_t total = 0, cambio = 1;
    mc_recuenta(m);
    while (cambio) {
        cambio = 0;
        for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) {
            struct mc_nodo *n = &m->nodo[i];
            if (!n->usado || n->fijo || n->cuenta || mc_nodo_hijos(m, i)) continue;
            if (n->padre && m->nodo[n->padre].usado) m->nodo[n->padre].sucio = 1;
            for (uint32_t k = 0; k < sizeof *n; k++) ((uint8_t *)n)[k] = 0;
            for (uint32_t k = 0; k < m->n; k++) if (m->it[k].nodo == i) { m->it[k].nodo = 0; ensucia(m, m->it[k].id); }   /* SOMA_283_MEMORIA_APP: lo archivado
                                       que colgaba de aqui no puede quedarse apuntando a un hueco que otro tema reutilizara */
            total++; cambio = 1; m->arbol_sucio = 1;
        }
    }
    return total;
}
int mc_nodo_set_desc(struct mc *m, uint32_t nodo, const char *desc)
{
    if (nodo < 1 || nodo > MC_NODOS_MAX || !m->nodo[nodo].usado || !desc) return -1;
    uint32_t n = slen(desc);
    if (n > MC_NODO_DESC - 1) { n = MC_NODO_DESC - 1; while (n > 0 && ((uint8_t)desc[n] & 0xC0) == 0x80) n--; }
    mcopy(m->nodo[nodo].desc, desc, n); m->nodo[nodo].desc[n] = 0; m->nodo[nodo].sucio = 0; m->arbol_sucio = 1;
    return 0;
}
int mc_nodo_fusiona(struct mc *m, uint32_t a, uint32_t b, const char *nombre)
{
    if (a < 1 || b < 1 || a > MC_NODOS_MAX || b > MC_NODOS_MAX || a == b || !m->nodo[a].usado || !m->nodo[b].usado) return -1;
    for (uint32_t i = 0; i < m->n; i++) if (m->it[i].nodo == b) { m->it[i].nodo = (uint8_t)a; ensucia(m, i + 1); }
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) if (m->nodo[i].usado && m->nodo[i].padre == b) m->nodo[i].padre = (uint8_t)a;
    struct mc_nodo *n = &m->nodo[b];
    for (uint32_t k = 0; k < sizeof *n; k++) ((uint8_t *)n)[k] = 0;
    if (nombre && nombre[0]) { char nom[MC_NODO_NOMBRE]; nombre_limpio(nombre, nombre + slen(nombre), nom); if (nom[0]) mcopy(m->nodo[a].nombre, nom, slen(nom) + 1); }
    m->nodo[a].sucio = 1; m->arbol_sucio = 1;
    mc_recuenta(m);
    return 0;
}
uint32_t mc_nodo_celdas(const struct mc *m, uint32_t nodo, uint32_t *ids, uint32_t max)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < m->n && n < max; i++) {
        const struct mc_item *e = &m->it[i];
        if (e->kind == MC_CELDA && e->estado == MC_ACTIVO && e->nodo == nodo) ids[n++] = e->id;
    }
    return n;
}
uint32_t mc_sin_nodo(const struct mc *m, uint32_t *ids, uint32_t max) { return mc_nodo_celdas(m, 0, ids, max); }
uint32_t mc_nodo_sucio(const struct mc *m)
{
    uint32_t best = 0; int nv = -1;
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) if (m->nodo[i].usado && m->nodo[i].sucio && (int)m->nodo[i].nivel > nv) { best = i; nv = m->nodo[i].nivel; }
    return best;
}
uint32_t mc_nodo_sobrecargado(struct mc *m)
{
    uint32_t best = 0, mx = MC_MAX_HIJOS;
    mc_recuenta(m);
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) if (m->nodo[i].usado && m->nodo[i].cuenta > mx) { best = i; mx = m->nodo[i].cuenta; }
    return best;
}
int mc_set_texto(struct mc *m, uint32_t id, const char *texto)
{
    if (!mc_get(m, id) || !texto) return -1;
    uint32_t tl = slen(texto);
    if (tl > MC_TEXTO_MAX) { tl = MC_TEXTO_MAX; while (tl > 0 && ((uint8_t)texto[tl] & 0xC0) == 0x80) tl--; }
    char *tp = (char *)arena(m, tl + 1);
    if (!tp) return -1;
    mcopy(tp, texto, tl); tp[tl] = 0;
    struct mc_item *e = &m->it[id - 1];
    e->texto = tp; e->tlen = (uint16_t)tl; e->vec = 0; e->dim = 0;          /* el vector era del texto anterior */
    ensucia(m, id);
    return 0;
}
int mc_set_imp(struct mc *m, uint32_t id, int imp, uint32_t tick)
{
    if (!mc_get(m, id)) return -1;
    m->it[id - 1].imp = (uint8_t)(imp < 1 ? 1 : imp > 10 ? 10 : imp); m->it[id - 1].tick = tick;
    ensucia(m, id);
    return 0;
}

/* ---- la curva del olvido ---- */
static const uint32_t EXP_Q16[194] = {   /* exp(-i/16) x 65536, i = 0..193 */
    65536, 61565, 57835, 54331, 51039, 47947, 45042, 42313, 39750, 37341, 35079, 32954,
    30957, 29081, 27319, 25664, 24109, 22649, 21276, 19987, 18776, 17639, 16570, 15566,
    14623, 13737, 12905, 12123, 11388, 10698, 10050, 9441, 8869, 8332, 7827, 7353,
    6907, 6489, 6096, 5726, 5380, 5054, 4747, 4460, 4190, 3936, 3697, 3473,
    3263, 3065, 2879, 2705, 2541, 2387, 2243, 2107, 1979, 1859, 1746, 1641,
    1541, 1448, 1360, 1278, 1200, 1128, 1059, 995, 935, 878, 825, 775,
    728, 684, 642, 604, 567, 533, 500, 470, 442, 415, 390, 366,
    344, 323, 303, 285, 268, 252, 236, 222, 209, 196, 184, 173,
    162, 153, 143, 135, 127, 119, 112, 105, 99, 93, 87, 82,
    77, 72, 68, 64, 60, 56, 53, 50, 47, 44, 41, 39,
    36, 34, 32, 30, 28, 27, 25, 23, 22, 21, 19, 18,
    17, 16, 15, 14, 13, 13, 12, 11, 10, 10, 9, 9,
    8, 8, 7, 7, 6, 6, 6, 5, 5, 5, 4, 4,
    4, 4, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2,
    2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    0, 0,
};
uint32_t mc_eff_q8(uint32_t imp, uint32_t edad, uint32_t tau)
{
    if (imp < 1) imp = 1;
    if (imp > 10) imp = 10;
    uint64_t den = (uint64_t)tau * imp * imp * imp;
    if (!den) return 0;
    uint64_t x = (((uint64_t)edad) << 16) / den;                /* edad / (tau * imp^3), en 1/65536 */
    uint32_t e;
    if (x >= 12ull * 65536ull) e = 0;
    else {
        uint32_t i = (uint32_t)(x >> 12), fr = (uint32_t)(x & 4095u);      /* tabla cada 1/16; interpolacion lineal entre dos entradas */
        e = EXP_Q16[i] - (uint32_t)(((uint64_t)(EXP_Q16[i] - EXP_Q16[i + 1]) * fr) >> 12);
    }
    return (uint32_t)(((uint64_t)imp * 256u * e) >> 16);
}
static uint32_t edad_de(const struct mc_item *e, uint32_t tick) { return tick > e->tick ? tick - e->tick : 0; }
uint32_t mc_olvida(struct mc *m, uint32_t tick, uint32_t min_edad, uint32_t tau, uint32_t bajo_q8)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < m->n; i++) {
        struct mc_item *e = &m->it[i];
        if (e->kind != MC_CELDA || e->estado != MC_ACTIVO) continue;
        uint32_t ed = edad_de(e, tick);
        if (ed < min_edad || mc_eff_q8(e->imp, ed, tau) >= bajo_q8) continue;
        e->estado = MC_ARCHIVADO; e->t_hasta = 0; ensucia(m, e->id); n++;       /* t_hasta = 0: lo archivo el olvido (lo archivado a mano lleva fecha) */
        if (e->nodo && m->nodo[e->nodo].usado) m->nodo[e->nodo].sucio = 1;
    }
    if (n) m->arbol_sucio = 1;
    return n;
}
uint32_t mc_restaura(struct mc *m, const uint32_t *ids, uint32_t n, uint32_t tick)
{
    uint32_t r = 0;
    for (uint32_t k = 0; k < n; k++) {
        if (!mc_get(m, ids[k])) continue;
        struct mc_item *e = &m->it[ids[k] - 1];
        if (e->kind != MC_CELDA || e->estado != MC_ARCHIVADO || e->t_hasta != 0 || e->sust_por) continue;
        e->estado = MC_ACTIVO; if (e->imp < 6) e->imp = 6; e->tick = tick; if (e->refuerzos < 255) e->refuerzos++;
        if (e->nodo && !m->nodo[e->nodo].usado) e->nodo = 0;                       /* su rama se podo mientras dormia: se reordena */
        else if (e->nodo) m->nodo[e->nodo].sucio = 1;
        ensucia(m, e->id); r++;
    }
    if (r) m->arbol_sucio = 1;
    return r;
}
uint32_t mc_candidato_rescate(const struct mc *m, uint32_t tick, uint32_t min_edad, uint32_t tau, uint32_t bajo_q8, const uint32_t *saltar, uint32_t ns)
{
    uint32_t best = 0, bt = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < m->n; i++) {
        const struct mc_item *e = &m->it[i];
        if (e->kind != MC_CELDA || e->estado != MC_ACTIVO) continue;
        uint32_t ed = edad_de(e, tick), eff = mc_eff_q8(e->imp, ed, tau);
        if (ed < min_edad || eff < bajo_q8 || eff > bajo_q8 + 90u) continue;       /* + 0,35: la franja de memorIA */
        { int ya = 0; for (uint32_t k = 0; k < ns; k++) if (saltar[k] == e->id) ya = 1; if (ya) continue; }
        if (e->tick < bt) { bt = e->tick; best = e->id; }
    }
    return best;
}

/* ---- parejas candidatas ---- */
#define MC_PAL 40
static uint32_t palabras_hash(const char *texto, uint32_t *h)       /* hashes de las palabras distintas */
{
    char f[MC_TEXTO_MAX + 1]; uint32_t n = 0, i = 0;
    mc_fold(texto, f, sizeof f);
    while (f[i] && n < MC_PAL) {
        uint32_t a = i; while (f[i] && f[i] != ' ') i++;
        uint32_t hs = hash32(f + a, i - a), dup = 0;
        for (uint32_t k = 0; k < n; k++) if (h[k] == hs) { dup = 1; break; }
        if (!dup) h[n++] = hs;
        while (f[i] == ' ') i++;
    }
    return n;
}
static int es_visto(const uint64_t *v, uint32_t nv, uint32_t a, uint32_t b)
{
    uint64_t k = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
    for (uint32_t i = 0; i < nv; i++) if (v[i] == k) return 1;
    return 0;
}
int mc_par_redundante(const struct mc *m, uint32_t *cursor, const uint64_t *vistos, uint32_t nv, uint32_t *a, uint32_t *b)
{
    for (uint32_t k = 0; k <= MC_NODOS_MAX; k++) {
        uint32_t nodo = (*cursor + k) % (MC_NODOS_MAX + 1), ids[48], n = mc_nodo_celdas(m, nodo, ids, 48);
        if (n < 2) continue;
        uint32_t hs[48][MC_PAL], nh[48];
        for (uint32_t i = 0; i < n; i++) nh[i] = palabras_hash(m->it[ids[i] - 1].texto, hs[i]);
        for (uint32_t i = 0; i < n; i++) for (uint32_t j = i + 1; j < n; j++) {
            if (es_visto(vistos, nv, ids[i], ids[j])) continue;
            const struct mc_item *x = &m->it[ids[i] - 1], *y = &m->it[ids[j] - 1];
            uint32_t inter = 0;
            for (uint32_t p = 0; p < nh[i]; p++) for (uint32_t q = 0; q < nh[j]; q++) if (hs[i][p] == hs[j][q]) { inter++; break; }
            uint32_t uni = nh[i] + nh[j] - inter;
            int red = uni && inter * 100 >= 55 * uni;
            if (!red && x->vec && y->vec && x->dim == y->dim && mc_dot(x->vec, y->vec, x->dim) >= 14194) red = 1;     /* 0,88 */
            if (red) { *cursor = nodo + 1; *a = ids[i]; *b = ids[j]; return 1; }
        }
    }
    *cursor = (*cursor + 1) % (MC_NODOS_MAX + 1);
    return 0;
}
int mc_par_ramas(const struct mc *m, const uint64_t *vistos, uint32_t nv, uint32_t *a, uint32_t *b, int *sim100)
{
    int mejor = -2; uint32_t ba = 0, bb = 0;
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) for (uint32_t j = i + 1; j <= MC_NODOS_MAX; j++) {
        const struct mc_nodo *x = &m->nodo[i], *y = &m->nodo[j];
        if (!x->usado || !y->usado || x->padre != y->padre || x->fijo || y->fijo || x->cuenta > 1 || y->cuenta > 1) continue;
        if (mc_nodo_hijos(m, i) || mc_nodo_hijos(m, j) || es_visto(vistos, nv, i, j)) continue;
        uint32_t ia[1], ib[1]; int sim = -1;
        if (mc_nodo_celdas(m, i, ia, 1) && mc_nodo_celdas(m, j, ib, 1)) {
            const struct mc_item *p = &m->it[ia[0] - 1], *q = &m->it[ib[0] - 1];
            if (p->vec && q->vec && p->dim == q->dim) { int32_t d = mc_dot(p->vec, q->vec, p->dim); sim = d <= 0 ? 0 : (int)((int64_t)d * 100 / MC_COS_UNO); }
        }
        if (sim >= 0 && sim < 50) continue;                          /* sin afinidad: ni se pregunta al LLM */
        if (sim > mejor) { mejor = sim; ba = i; bb = j; }
    }
    if (!ba) return 0;
    *a = ba; *b = bb; if (sim100) *sim100 = mejor;
    return 1;
}

/* ---- lectura del JSON que devuelve el LLM ---- */
static uint32_t jint(const char *p)             /* entero sin signo, con o sin comillas; 0 si no hay */
{
    uint32_t v = 0; int nd = 0;
    p = ws(p); if (*p == '"') p++;
    while (*p >= '0' && *p <= '9' && nd < 9) { v = v * 10 + (uint32_t)(*p - '0'); p++; nd++; }
    return v;
}
static const char *clave_valor(const char *reply, const char *clave)       /* apunta al valor de "clave": ..., o 0 */
{
    char k[48]; uint32_t n = 0;
    k[n++] = '"'; for (uint32_t i = 0; clave[i] && n < sizeof k - 2; i++) k[n++] = clave[i]; k[n++] = '"'; k[n] = 0;
    const char *p = sfind(reply, k);
    if (!p) return 0;
    p = ws(p + n);
    if (*p != ':') return 0;
    return ws(p + 1);
}
static int json_bool(const char *p)             /* 1 true, 0 false, -1 otra cosa */
{
    if (starts(p, "true")) return 1;
    if (starts(p, "false")) return 0;
    if (*p == '"') { p++; if (starts(p, "true") || starts(p, "si") || starts(p, "yes")) return 1; if (starts(p, "false") || starts(p, "no")) return 0; }
    return -1;
}
static void limpia_espacios(char *t)
{
    uint32_t n = 0; int sp = 1;
    for (uint32_t i = 0; t[i]; i++) { char c = t[i]; if (c == ' ' || c == '\n' || c == '\t') { if (!sp) t[n++] = ' '; sp = 1; } else { t[n++] = c; sp = 0; } }
    while (n && t[n - 1] == ' ') n--;
    t[n] = 0;
}
static const char *lee_ids(const char *p, uint32_t *ids, uint32_t *n, uint32_t max)     /* [1, "2", 3] */
{
    *n = 0;
    p = ws(p); if (*p != '[') return 0;
    p = ws(p + 1);
    while (*p && *p != ']') {
        uint32_t v = jint(p);
        if (v && *n < max) ids[(*n)++] = v;
        p = jskip(p); if (!p) return 0;
        p = ws(p); if (*p == ',') p = ws(p + 1);
    }
    return *p == ']' ? p + 1 : 0;
}

int mc_parse_ordena(const char *reply, struct mc_asig *out, int max)
{
    const char *p = reply ? clave_valor(reply, "asignaciones") : 0;
    int n = 0;
    if (!p || *p != '[' || max <= 0) return -1;
    p = ws(p + 1);
    while (*p && *p != ']') {
        if (*p != '{') { p = jskip(p); if (!p) break; p = ws(p); if (*p == ',') p = ws(p + 1); continue; }
        struct mc_asig a; a.id = 0; a.ruta[0] = 0;
        p = ws(p + 1);
        while (*p && *p != '}') {
            char key[24]; const char *e = jstr(p, key, sizeof key); if (!e) return n ? n : -1;
            p = ws(e); if (*p != ':') return n ? n : -1;
            p = ws(p + 1);
            if (seq(key, "id")) { a.id = jint(p); p = jskip(p); }
            else if (seq(key, "ruta")) { char t[96]; e = jstr(p, t, sizeof t); if (!e) return n ? n : -1; mcopy(a.ruta, t, slen(t) + 1); p = e; }
            else p = jskip(p);
            if (!p) return n ? n : -1;
            p = ws(p); if (*p == ',') p = ws(p + 1);
        }
        if (*p == '}') p++;
        limpia_espacios(a.ruta);
        if (a.id && a.ruta[0] && n < max) out[n++] = a;
        p = ws(p); if (*p == ',') p = ws(p + 1);
    }
    return n;
}
int mc_parse_descripcion(const char *reply, char *out, uint32_t cap)
{
    const char *p = reply ? clave_valor(reply, "description") : 0;
    if (cap) out[0] = 0;
    if (!p) p = reply ? clave_valor(reply, "descripcion") : 0;
    if (!p || !cap) return 0;
    char t[MC_NODO_DESC * 3]; if (!jstr(p, t, sizeof t)) return 0;
    limpia_espacios(t);
    if (!t[0]) return 0;
    uint32_t n = slen(t); if (n > cap - 1) { n = cap - 1; while (n > 0 && ((uint8_t)t[n] & 0xC0) == 0x80) n--; }
    mcopy(out, t, n); out[n] = 0;
    return 1;
}
int mc_parse_fusion(const char *reply, struct mc_fusion *f)
{
    f->redundante = 0; f->texto[0] = 0; f->imp = 0;
    const char *p = reply ? clave_valor(reply, "redundant") : 0;
    if (!p) return 0;
    int b = json_bool(p); if (b < 0) return 0;
    f->redundante = b;
    p = clave_valor(reply, "merged");
    if (p) { char t[600]; if (jstr(p, t, sizeof t)) { limpia_espacios(t); mcopy(f->texto, t, slen(t) + 1); } }
    p = clave_valor(reply, "importance");
    if (p) { int v = jnum100(p) / 100; f->imp = v < 1 ? 0 : v > 10 ? 10 : v; }
    return 1;
}
int mc_parse_rescate(const char *reply, int *imp)
{
    const char *p = reply ? clave_valor(reply, "preserve") : 0;
    if (imp) *imp = 0;
    if (!p) return -1;
    int b = json_bool(p); if (b < 0) return -1;
    if (b && imp) { const char *q = clave_valor(reply, "importance"); int v = q ? jnum100(q) / 100 : 7; *imp = v < 5 ? 5 : v > 10 ? 10 : v; }
    return b;
}
int mc_parse_unir(const char *reply, char *nombre, uint32_t cap)
{
    const char *p = reply ? clave_valor(reply, "mergeable") : 0;
    if (cap) nombre[0] = 0;
    if (!p) return -1;
    int b = json_bool(p); if (b < 0) return -1;
    if (b && cap) { const char *q = clave_valor(reply, "merged_name"); char t[96]; if (q && jstr(q, t, sizeof t)) { limpia_espacios(t); uint32_t n = slen(t); if (n > cap - 1) n = cap - 1; mcopy(nombre, t, n); nombre[n] = 0; } }
    return b;
}
static const char *lee_grupo(const char *reply, const char *clave, char *nombre, uint32_t ncap, uint32_t *ids, uint32_t *n, uint32_t max)
{
    const char *p = clave_valor(reply, clave);
    if (!p || *p != '{') return 0;
    const char *fin = jskip(p); if (!fin) return 0;
    char sub_[1600]; uint32_t l = (uint32_t)(fin - p); if (l >= sizeof sub_) l = sizeof sub_ - 1;
    mcopy(sub_, p, l); sub_[l] = 0;                       /* el grupo, para que "ids" y "nombre" no se tomen de otro */
    const char *q = clave_valor(sub_, "nombre");
    nombre[0] = 0;
    if (q) { char t[96]; if (jstr(q, t, sizeof t)) { limpia_espacios(t); uint32_t k = slen(t); if (k > ncap - 1) k = ncap - 1; mcopy(nombre, t, k); nombre[k] = 0; } }
    q = clave_valor(sub_, "ids"); *n = 0;
    if (q) lee_ids(q, ids, n, max);
    return fin;
}
int mc_parse_split(const char *reply, struct mc_split *sp)
{
    sp->nombre_a[0] = sp->nombre_b[0] = 0; sp->na = sp->nb = 0;
    if (!reply) return 0;
    int a = lee_grupo(reply, "grupo_A", sp->nombre_a, sizeof sp->nombre_a, sp->ids_a, &sp->na, 16) != 0;
    int b = lee_grupo(reply, "grupo_B", sp->nombre_b, sizeof sp->nombre_b, sp->ids_b, &sp->nb, 16) != 0;
    return a && b;
}

/* ---- prompts ---- */
static void put_celda(const struct mc *m, char *out, uint32_t cap, uint32_t *p, uint32_t id, uint32_t clip)
{
    const struct mc_item *e = mc_get(m, id); if (!e) return;
    put(out, cap, p, "- ["); put_dec(out, cap, p, id); put(out, cap, p, "] "); put_clip(out, cap, p, e->texto, clip); put(out, cap, p, "\n");
}
int mc_prompt_ordena(const struct mc *m, const uint32_t *ids, uint32_t n, char *out, uint32_t cap)
{
    uint32_t p = 0, hay = 0; char r[160];
    if (!cap) return 0;
    out[0] = 0;
    put(out, cap, &p, "Eres el archivista de una memoria. Clasifica cada recuerdo en una ruta de temas de hasta 3 niveles (Tema/Subtema/Detalle).\n");
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) if (m->nodo[i].usado) {
        if (!hay) put(out, cap, &p, "Rutas que ya existen (reutilizalas siempre que encajen):\n");
        hay++; mc_nodo_ruta_texto(m, i, r, sizeof r); put(out, cap, &p, "- "); put(out, cap, &p, r); put(out, cap, &p, "\n");
        if (hay >= 60) break;
    }
    put(out, cap, &p, "Recuerdos:\n");
    for (uint32_t i = 0; i < n; i++) put_celda(m, out, cap, &p, ids[i], 220);
    put(out, cap, &p, "Reglas: nombres cortos en espanol (1 a 3 palabras); los recuerdos sobre la misma persona o ambito van juntos; "
                      "PROHIBIDO crear categorias paraguas como 'Varios', 'General' o 'Otros'.\n"
                      "Devuelve SOLO este JSON: {\"asignaciones\":[{\"id\":12,\"ruta\":\"Tema/Subtema\"}]}");
    return (int)p;
}
int mc_prompt_describe(const struct mc *m, uint32_t nodo, char *out, uint32_t cap)
{
    uint32_t p = 0, k = 0, ids[10];
    if (!cap) return 0;
    out[0] = 0;
    if (nodo < 1 || nodo > MC_NODOS_MAX || !m->nodo[nodo].usado) return 0;
    put(out, cap, &p, "Nodo '"); put(out, cap, &p, m->nodo[nodo].nombre); put(out, cap, &p, "' (profundidad "); put_dec(out, cap, &p, (uint32_t)m->nodo[nodo].nivel + 1u); put(out, cap, &p, ").\nContenido:\n");
    for (uint32_t i = 1; i <= MC_NODOS_MAX && k < 8; i++) if (m->nodo[i].usado && m->nodo[i].padre == nodo) {
        put(out, cap, &p, "- "); put(out, cap, &p, m->nodo[i].nombre); put(out, cap, &p, ": "); put(out, cap, &p, m->nodo[i].desc); put(out, cap, &p, "\n"); k++;
    }
    uint32_t nc = mc_nodo_celdas(m, nodo, ids, 10);
    for (uint32_t i = 0; i < nc; i++) { const struct mc_item *e = mc_get(m, ids[i]); put(out, cap, &p, "- "); put_clip(out, cap, &p, e->texto, 200); put(out, cap, &p, "\n"); }
    put(out, cap, &p, "Escribe una descripcion del AMBITO de este nodo en maximo 20 palabras.\nDevuelve: {\"description\": \"...\"}");
    return (int)p;
}
int mc_prompt_fusion(const struct mc *m, uint32_t a, uint32_t b, char *out, uint32_t cap)
{
    uint32_t p = 0;
    const struct mc_item *x = mc_get(m, a), *y = mc_get(m, b);
    if (!cap) return 0;
    out[0] = 0;
    if (!x || !y) return 0;
    put(out, cap, &p, "Compara estos dos recuerdos del mismo tema:\nA: "); put_clip(out, cap, &p, x->texto, 500);
    put(out, cap, &p, "\nB: "); put_clip(out, cap, &p, y->texto, 500);
    put(out, cap, &p, "\n\nSi son redundantes o uno complementa al otro, unificalos en una sola afirmacion concisa. "
                      "Devuelve JSON: {\"redundant\": true, \"merged\": \"...\", \"importance\": 8}.\n"
                      "Si representan hechos distintos y deben conservarse ambos (por ejemplo, datos diferentes sobre personas o cosas diferentes), devuelve: {\"redundant\": false}.");
    return (int)p;
}
int mc_prompt_rescate(const struct mc *m, uint32_t id, char *out, uint32_t cap)
{
    uint32_t p = 0; const struct mc_item *e = mc_get(m, id);
    if (!cap) return 0;
    out[0] = 0;
    if (!e) return 0;
    put(out, cap, &p, "Evalua si este conocimiento merece ser preservado de forma duradera:\n\""); put_clip(out, cap, &p, e->texto, 500);
    put(out, cap, &p, "\"\nDevuelve JSON: {\"preserve\": true/false, \"importance\": <1 a 10>}");
    return (int)p;
}
int mc_prompt_split(const struct mc *m, uint32_t nodo, char *out, uint32_t cap)
{
    uint32_t p = 0, ids[16];
    if (!cap) return 0;
    out[0] = 0;
    if (nodo < 1 || nodo > MC_NODOS_MAX || !m->nodo[nodo].usado) return 0;
    uint32_t n = mc_nodo_celdas(m, nodo, ids, 16);
    put(out, cap, &p, "El grupo de memoria '"); put(out, cap, &p, m->nodo[nodo].nombre); put(out, cap, &p, "' tiene demasiados recuerdos ("); put_dec(out, cap, &p, m->nodo[nodo].cuenta);
    put(out, cap, &p, ").\nDividelos en 2 subgrupos tematicos distintos y coherentes:\n");
    for (uint32_t i = 0; i < n; i++) put_celda(m, out, cap, &p, ids[i], 100);
    put(out, cap, &p, "\nDevuelve EXCLUSIVAMENTE este JSON: {\"grupo_A\": {\"nombre\": \"...\", \"ids\": [1]}, \"grupo_B\": {\"nombre\": \"...\", \"ids\": [2]}}");
    return (int)p;
}
int mc_prompt_unir(const struct mc *m, uint32_t a, uint32_t b, int sim100, char *out, uint32_t cap)
{
    uint32_t p = 0;
    if (!cap) return 0;
    out[0] = 0;
    if (a < 1 || b < 1 || a > MC_NODOS_MAX || b > MC_NODOS_MAX || !m->nodo[a].usado || !m->nodo[b].usado) return 0;
    put(out, cap, &p, "Pertenecen estos dos subtemas a un mismo concepto integrable?");
    if (sim100 >= 0) {
        put(out, cap, &p, " (similitud semantica detectada: ");
        if (sim100 >= 100) put(out, cap, &p, "1.00"); else { put(out, cap, &p, "0."); if (sim100 < 10) put(out, cap, &p, "0"); put_dec(out, cap, &p, (uint32_t)sim100); }
        put(out, cap, &p, ")");
    }
    put(out, cap, &p, "\nTema 1: "); put(out, cap, &p, m->nodo[a].nombre); put(out, cap, &p, "\nTema 2: "); put(out, cap, &p, m->nodo[b].nombre);
    put(out, cap, &p, "\n\nReglas estrictas:\n1. Solo responde mergeable=true si representan el MISMO ambito especifico o variantes muy cercanas (ej: 'postres' y 'reposteria').\n"
                      "2. Si son temas distintos (ej: 'mascotas' y 'finanzas'), devuelve mergeable=false.\n"
                      "3. PROHIBIDO crear categorias paraguas artificiales (como 'estilo de vida', 'varios').\n"
                      "Devuelve JSON: {\"mergeable\": true/false, \"merged_name\": \"...\"}");
    return (int)p;
}

/* ---- aplicar lo que decide el LLM ---- */
int mc_aplica_fusion(struct mc *m, uint32_t a, uint32_t b, const struct mc_fusion *f, uint32_t tick, uint32_t ahora)
{
    const struct mc_item *x = mc_get(m, a), *y = mc_get(m, b);
    if (!x || !y || a == b || x->kind != MC_CELDA || y->kind != MC_CELDA || x->estado != MC_ACTIVO || y->estado != MC_ACTIVO || !f->redundante) return -1;
    int imp = x->imp > y->imp ? x->imp : y->imp;
    if (f->imp > imp) imp = f->imp;                                  /* nunca baja: lo que fusiona vale al menos lo que valia lo mejor */
    if (f->texto[0] && !seq(f->texto, x->texto) && mc_set_texto(m, a, f->texto) != 0) return -1;
    mc_set_imp(m, a, imp, tick);
    mc_set_estado(m, b, MC_SUSTITUIDO, ahora, a);
    if (m->it[b - 1].nodo && m->nodo[m->it[b - 1].nodo].usado) m->nodo[m->it[b - 1].nodo].sucio = 1;
    if (m->it[a - 1].nodo && m->nodo[m->it[a - 1].nodo].usado) m->nodo[m->it[a - 1].nodo].sucio = 1;
    m->arbol_sucio = 1;
    return 0;
}
int mc_aplica_split(struct mc *m, uint32_t nodo, const struct mc_split *sp)
{
    if (nodo < 1 || nodo > MC_NODOS_MAX || !m->nodo[nodo].usado || !sp->nombre_b[0] || !sp->nb) return 0;
    mc_recuenta(m);
    uint32_t validos[16], nv = 0, total = m->nodo[nodo].cuenta;
    for (uint32_t i = 0; i < sp->nb && nv < 16; i++) {
        const struct mc_item *e = mc_get(m, sp->ids_b[i]);
        if (e && e->kind == MC_CELDA && e->estado == MC_ACTIVO && e->nodo == nodo) validos[nv++] = sp->ids_b[i];
    }
    if (!nv) return 0;
    if (nv >= total) nv = total / 2;
    if (!nv) return 0;
    uint32_t lvl = m->nodo[nodo].nivel, pid = m->nodo[nodo].padre;
    int hijo = lvl + 1 < MC_NIVELES;
    uint32_t b = nodo_hijo(m, hijo ? nodo : pid, hijo ? lvl + 1 : lvl, sp->nombre_b, 1);
    if (!b || b == nodo) return 0;
    uint32_t movidas = 0;
    for (uint32_t i = 0; i < nv; i++) if (mc_asigna(m, validos[i], b) == 0) movidas++;
    if (hijo && sp->nombre_a[0] && !nombre_igual(sp->nombre_a, sp->nombre_b)) {
        uint32_t a = nodo_hijo(m, nodo, lvl + 1, sp->nombre_a, 1);
        if (a && a != nodo && a != b) for (uint32_t i = 0; i < sp->na; i++) {
            const struct mc_item *e = mc_get(m, sp->ids_a[i]);
            if (e && e->kind == MC_CELDA && e->estado == MC_ACTIVO && e->nodo == nodo && mc_asigna(m, sp->ids_a[i], a) == 0) movidas++;
        }
    }
    m->nodo[nodo].sucio = 1; m->arbol_sucio = 1;
    mc_recuenta(m);
    return (int)movidas;
}

/* ---- guardado del arbol y el reloj: "SMN1" tick pasos n { id padre nivel fijo nombre[48] desc[120] } crc ---- */
uint32_t mc_arbol_max_bytes(void) { return 16 + MC_NODOS_MAX * (4 + MC_NODO_NOMBRE + MC_NODO_DESC) + 4; }
int mc_arbol_guarda(const struct mc *m, uint8_t *buf, uint32_t cap)
{
    uint32_t p = 0, n = 0;
    if (cap < mc_arbol_max_bytes()) return -1;
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) if (m->nodo[i].usado) n++;
    buf[p++] = 'S'; buf[p++] = 'M'; buf[p++] = 'N'; buf[p++] = '1';
    w32(buf, &p, m->tick); w32(buf, &p, m->pasos); w32(buf, &p, n);
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) {
        const struct mc_nodo *x = &m->nodo[i]; if (!x->usado) continue;
        buf[p++] = (uint8_t)i; buf[p++] = x->padre; buf[p++] = x->nivel; buf[p++] = (uint8_t)(x->fijo | (x->sucio << 1));
        mcopy(buf + p, x->nombre, MC_NODO_NOMBRE); p += MC_NODO_NOMBRE;
        mcopy(buf + p, x->desc, MC_NODO_DESC); p += MC_NODO_DESC;
    }
    uint32_t c = mc_crc32(buf, p); w32(buf, &p, c);
    return (int)p;
}
int mc_arbol_carga(struct mc *m, const uint8_t *buf, uint32_t len)
{
    if (len < 20 || buf[0] != 'S' || buf[1] != 'M' || buf[2] != 'N' || buf[3] != '1') return -1;
    if (mc_crc32(buf, len - 4) != r32(buf + len - 4)) return -2;
    uint32_t tick = r32(buf + 4), pasos = r32(buf + 8), n = r32(buf + 12), p = 16;
    if (n > MC_NODOS_MAX || p + n * (4 + MC_NODO_NOMBRE + MC_NODO_DESC) + 4 != len) return -3;
    uint8_t visto[MC_NODOS_MAX + 1], padres[MC_NODOS_MAX + 1];
    for (uint32_t i = 0; i <= MC_NODOS_MAX; i++) visto[i] = padres[i] = 0;
    for (uint32_t k = 0, q = p; k < n; k++, q += 4 + MC_NODO_NOMBRE + MC_NODO_DESC) {      /* primero se valida todo: si falla, no se toca nada */
        uint32_t id = buf[q], padre = buf[q + 1], nivel = buf[q + 2];
        if (id < 1 || id > MC_NODOS_MAX || visto[id] || nivel >= MC_NIVELES || padre > MC_NODOS_MAX) return -4;
        visto[id] = 1; padres[id] = (uint8_t)padre;
    }
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) if (visto[i] && padres[i] && !visto[padres[i]]) return -5;      /* hijo sin padre */
    for (uint32_t i = 0; i <= MC_NODOS_MAX; i++) { struct mc_nodo *x = &m->nodo[i]; for (uint32_t k = 0; k < sizeof *x; k++) ((uint8_t *)x)[k] = 0; }
    for (uint32_t k = 0; k < n; k++) {
        uint32_t id = buf[p], fl = buf[p + 3];
        struct mc_nodo *x = &m->nodo[id];
        x->usado = 1; x->padre = buf[p + 1]; x->nivel = buf[p + 2]; x->fijo = (uint8_t)(fl & 1); x->sucio = (uint8_t)((fl >> 1) & 1);
        mcopy(x->nombre, buf + p + 4, MC_NODO_NOMBRE); x->nombre[MC_NODO_NOMBRE - 1] = 0;
        mcopy(x->desc, buf + p + 4 + MC_NODO_NOMBRE, MC_NODO_DESC); x->desc[MC_NODO_DESC - 1] = 0;
        p += 4 + MC_NODO_NOMBRE + MC_NODO_DESC;
    }
    m->tick = tick; m->pasos = pasos; m->arbol_sucio = 0;
    mc_recuenta(m);
    return 0;
}

/* ======================================================================================================================
 * SOMA_280_IMPORTAR: importar el cartucho de memorIA (formato .smem)
 * ==================================================================================================================== */
static int b64v(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static int b64_a_bytes(const char *s, uint8_t *out, uint32_t max)           /* bytes o -1 */
{
    uint32_t n = 0, acc = 0; int bits = 0;
    for (; *s && *s != '='; s++) {
        int v = b64v((unsigned char)*s); if (v < 0) return -1;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; if (n >= max) return -1; out[n++] = (uint8_t)((acc >> bits) & 0xFF); }
    }
    return (int)n;
}
static void desescapa(char *s)                                              /* \\ \t \n \r -> el caracter */
{
    char *d = s;
    while (*s) {
        if (*s == '\\' && s[1]) {
            s++;
            *d++ = *s == 't' ? '\t' : *s == 'n' ? '\n' : *s == 'r' ? '\r' : *s;
            s++;
        } else *d++ = *s++;
    }
    *d = 0;
}
static uint64_t hash64(const char *s, uint32_t n, int kind)
{
    uint64_t h = 0xCBF29CE484222325ull ^ (kind == MC_EPISODIO ? 0xE9150DE0ull : 0);
    while (n--) { h ^= (uint8_t)*s++; h *= 0x100000001B3ull; }
    return h ? h : 1;
}
static int hs_busca_pon(struct mc_imp *s, uint64_t h, int poner)             /* 1 si ya estaba */
{
    uint32_t i = (uint32_t)(h ^ (h >> 29)) & (s->hs_cap - 1);
    for (uint32_t k = 0; k < s->hs_cap; k++, i = (i + 1) & (s->hs_cap - 1)) {
        if (s->hs[i] == h) return 1;
        if (!s->hs[i]) { if (poner) s->hs[i] = h; return 0; }
    }
    return 0;
}
static uint64_t hash_texto(const char *t, int kind)
{
    char f[MC_TEXTO_MAX + 8]; uint32_t n = mc_fold(t, f, sizeof f);
    return n ? hash64(f, n, kind) : 0;
}
int mc_imp_ini(struct mc *m, struct mc_imp *s, uint32_t ord_cap)
{
    uint32_t keep_vec = s->vec_ok, keep_dim = s->dim_vec, keep_ahora = s->ahora;
    char mod[40]; for (uint32_t i = 0; i < sizeof mod; i++) mod[i] = s->modelo[i];
    for (uint32_t i = 0; i < sizeof *s; i++) ((char *)s)[i] = 0;
    s->vec_ok = keep_vec; s->dim_vec = keep_dim; s->ahora = keep_ahora; for (uint32_t i = 0; i < sizeof mod; i++) s->modelo[i] = mod[i];
    if (!m || !m->it || !ord_cap) return -1;
    s->ord_cap = ord_cap;
    s->ord_id = (uint32_t *)mc_alloc(ord_cap * 4u + 4u); s->ord_sust = (uint32_t *)mc_alloc(ord_cap * 4u + 4u);
    s->hs_cap = 1; while (s->hs_cap < (m->cap + 4096u) * 2u) s->hs_cap <<= 1;
    s->hs = (uint64_t *)mc_alloc(s->hs_cap * 8u);
    if (!s->ord_id || !s->ord_sust || !s->hs) { mc_imp_fin(m, s); return -1; }
    for (uint32_t i = 0; i <= ord_cap; i++) s->ord_id[i] = s->ord_sust[i] = 0;
    for (uint32_t i = 0; i < s->hs_cap; i++) s->hs[i] = 0;
    for (uint32_t i = 0; i < m->n; i++) { uint64_t h = hash_texto(m->it[i].texto, m->it[i].kind); if (h) hs_busca_pon(s, h, 1); }
    s->tick = m->tick;
    return 0;
}
static uint32_t num(const char *p) { uint32_t v = 0; while (*p >= '0' && *p <= '9') v = v * 10 + (uint32_t)(*p++ - '0'); return v; }
static void cabecera(struct mc_imp *s, const char *l)
{
    const char *p = l;
    s->cab_ok = 0;
    if (!starts(l, "#SOMAMEM ")) return;
    p += 9; if (num(p) != 1) return;
    s->cab_ok = 1; s->cab_dim = 0; s->cab_parte = s->cab_partes = 0; s->modelo[0] = 0;
    for (; *p; p++) {
        if (starts(p, "modelo=")) { uint32_t n = 0; p += 7; while (*p && *p != ' ' && n + 1 < sizeof s->modelo) s->modelo[n++] = *p++; s->modelo[n] = 0; }
        else if (starts(p, "dim=")) { p += 4; s->cab_dim = num(p); }
        else if (starts(p, "parte=")) { p += 6; s->cab_parte = num(p); while (*p >= '0' && *p <= '9') p++; if (*p == '/') s->cab_partes = num(p + 1); }
    }
}
static int vec_de(struct mc_imp *s, const char *b64, int8_t *vb, uint32_t *dim)
{
    *dim = 0;
    if (!b64 || !*b64) return 0;
    if (!s->vec_ok) { s->sin_vec++; return 0; }
    int n = b64_a_bytes(b64, (uint8_t *)vb, MC_DIM_MAX);
    if (n < 8 || (s->dim_vec && (uint32_t)n != s->dim_vec) || (!s->dim_vec && s->cab_dim && (uint32_t)n != s->cab_dim)) { s->sin_vec++; return 0; }
    *dim = (uint32_t)n;
    return 1;
}
int mc_imp_linea(struct mc *m, struct mc_imp *s, char *linea)
{
    char *f[12]; uint32_t nf = 0;
    if (!linea || !s || !s->hs) return -1;
    { char *e = linea; while (*e) e++; while (e > linea && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0; }
    if (!*linea) return 1;
    if (*linea == '#') { cabecera(s, linea); return 1; }
    f[nf++] = linea;
    for (char *q = linea; *q && nf < 12; q++) if (*q == '\t') { *q = 0; f[nf++] = q + 1; }
    for (uint32_t i = 1; i < nf; i++) desescapa(f[i]);
    if (f[0][0] == 'N' && !f[0][1]) {
        if (nf < 2) { s->malas++; return -1; }
        uint32_t nodo = mc_nodo_ruta(m, f[1], 1);
        if (!nodo) { s->nodos_llenos++; return 1; }
        if (nf >= 3 && f[2][0]) { mc_nodo_set_desc(m, nodo, f[2]); s->nodos_desc++; }
        return 1;
    }
    if (f[0][0] == 'C' && !f[0][1]) {
        if (nf < 10) { s->malas++; return -1; }
        uint32_t ord = num(f[1]), imp = num(f[2]), epoch = num(f[3]), est = num(f[4]), sust = num(f[5]), hasta = num(f[6]);
        if (!ord || ord > s->ord_cap || !f[9][0] || est > 2) { s->malas++; return -1; }
        if (m->n >= m->cap) { s->lleno = 1; return -2; }
        uint64_t h = hash_texto(f[9], MC_CELDA);
        if (!h) { s->malas++; return -1; }
        if (hs_busca_pon(s, h, 0)) { s->repetidas++; return 1; }
        int8_t vb[MC_DIM_MAX]; uint32_t dim = 0;
        int tiene = nf >= 11 ? vec_de(s, f[10], vb, &dim) : 0;
        uint32_t th = est == 2 ? (hasta ? hasta : (epoch ? epoch : 1)) : 0;
        if (!epoch) epoch = s->ahora;                                            /* memorIA no sabia cuando: la fecha de hoy */
        uint32_t id = add_raw(m, MC_CELDA, MC_ROL_USUARIO, f[9], slen(f[9]), f[8], slen(f[8]), (int)(imp < 1 ? 1 : imp > 10 ? 10 : imp), epoch, s->tick,
                              th, 0, (int)est, 0, tiene ? vb : 0, tiene ? dim : 0);
        if (!id) { s->lleno = 1; return -2; }
        hs_busca_pon(s, h, 1);
        ensucia(m, id);
        s->celdas++; s->ord_id[ord] = id; if (est == 2 && sust && sust <= s->ord_cap) s->ord_sust[ord] = sust;
        if (ord > s->ord_max) s->ord_max = ord;
        if (f[7][0]) {
            uint32_t nodo = mc_nodo_ruta(m, f[7], 1);
            if (nodo) mc_asigna(m, id, nodo); else s->nodos_llenos++;
        }
        return 0;
    }
    if (f[0][0] == 'E' && !f[0][1]) {
        if (nf < 4) { s->malas++; return -1; }
        uint32_t epoch = num(f[1]), rol = num(f[2]);
        if (!f[3][0]) { s->malas++; return -1; }
        if (m->n >= m->cap) { s->lleno = 1; return -2; }
        uint64_t h = hash_texto(f[3], MC_EPISODIO);
        if (!h) { s->malas++; return -1; }
        if (hs_busca_pon(s, h, 0)) { s->repetidas++; return 1; }
        int8_t vb[MC_DIM_MAX]; uint32_t dim = 0;
        int tiene = nf >= 5 ? vec_de(s, f[4], vb, &dim) : 0;
        if (!epoch) epoch = s->ahora;
        uint32_t id = add_raw(m, MC_EPISODIO, rol > MC_ROL_DOC ? MC_ROL_USUARIO : (int)rol, f[3], slen(f[3]), "", 0, 5, epoch, s->tick, 0, 0, MC_ACTIVO, 0, tiene ? vb : 0, tiene ? dim : 0);
        if (!id) { s->lleno = 1; return -2; }
        hs_busca_pon(s, h, 1);
        ensucia(m, id);
        s->epis++;
        return 0;
    }
    s->malas++;
    return -1;
}
void mc_imp_fin(struct mc *m, struct mc_imp *s)
{
    if (m && s->ord_id && s->ord_sust) {
        for (uint32_t o = 1; o <= s->ord_max && o <= s->ord_cap; o++) {
            uint32_t id = s->ord_id[o], su = s->ord_sust[o];
            if (id && su && s->ord_id[su] && mc_get(m, id)) { m->it[id - 1].sust_por = s->ord_id[su]; ensucia(m, id); }
        }
        mc_recuenta(m);
        for (uint32_t n = 1; n <= MC_NODOS_MAX; n++) if (m->nodo[n].usado && m->nodo[n].desc[0]) m->nodo[n].sucio = 0;     /* la descripcion de memorIA se respeta */
        m->arbol_sucio = 1;
    }
    if (s->ord_id) mc_free(s->ord_id);
    if (s->ord_sust) mc_free(s->ord_sust);
    if (s->hs) mc_free(s->hs);
    s->ord_id = s->ord_sust = 0; s->hs = 0;
}

/* ======================================================================================================================
 * SOMA_283_MEMORIA_APP: borrar de verdad, ramas a mano y escritura .smem
 * ==================================================================================================================== */
int mc_compacta(struct mc *m, const uint8_t *quita)
{
    if (!m || !m->it || !quita) return -1;
    uint32_t borra = 0;
    for (uint32_t i = 0; i < m->n; i++) if (quita[i]) borra++;
    if (!borra) return 0;
    struct mc *t = (struct mc *)mc_alloc((uint32_t)sizeof *t);
    uint32_t *map = (uint32_t *)mc_alloc(m->n * 4u + 4u);
    if (!t || !map || mc_init(t, m->cap) != 0) { if (t) { if (t->it) mc_destroy(t); mc_free(t); } if (map) mc_free(map); return -1; }
    uint32_t k = 0;
    for (uint32_t i = 0; i < m->n; i++) map[i] = quita[i] ? 0 : ++k;
    for (uint32_t i = 0; i < m->n; i++) {
        if (quita[i]) continue;
        const struct mc_item *e = &m->it[i];
        uint32_t sust = (e->sust_por && e->sust_por <= m->n) ? map[e->sust_por - 1] : 0, th = e->t_hasta;
        int est = e->estado;
        if (est == MC_SUSTITUIDO && !sust) { est = MC_ACTIVO; th = 0; }              /* su sustituta se ha borrado: vuelve a valer */
        uint32_t id = add_raw(t, e->kind, e->rol, e->texto, e->tlen, e->fuente, e->flen, e->imp, e->t, e->tick, th, sust, est, e->refuerzos, e->vec, e->dim);
        if (!id) { mc_destroy(t); mc_free(t); mc_free(map); return -1; }
        t->it[id - 1].nodo = e->nodo;
    }
    for (uint32_t i = 0; i <= MC_NODOS_MAX; i++) t->nodo[i] = m->nodo[i];
    t->tick = m->tick; t->pasos = m->pasos; t->arbol_sucio = 1;
    uint32_t nf_viejo = mc_nfrag(m);
    for (uint32_t i = 0; i < MC_MAX_SHARDS; i++) t->sucio[i] = i < nf_viejo ? 1 : 0;
    for (uint32_t i = 0; i < m->nbloques; i++) mc_free(m->bloque[i]);
    mc_free(m->it);
    *m = *t;                                                                         /* la estructura nueva (con sus bloques) pasa a ser la memoria */
    mc_free(t); mc_free(map);
    mc_recuenta(m);
    return (int)borra;
}
int mc_nodo_dentro(const struct mc *m, uint32_t nodo, uint32_t rama)
{
    for (uint32_t g = 0; nodo && nodo <= MC_NODOS_MAX && g <= MC_NIVELES; g++) { if (nodo == rama) return 1; nodo = m->nodo[nodo].padre; }
    return 0;
}
int mc_nodo_renombra(struct mc *m, uint32_t nodo, const char *nombre)
{
    if (!m || !nodo || nodo > MC_NODOS_MAX || !m->nodo[nodo].usado || !nombre) return -1;
    while (*nombre == ' ') nombre++;
    const char *z = nombre; while (*z) z++;
    char nom[MC_NODO_NOMBRE]; nombre_limpio(nombre, z, nom);
    for (uint32_t i = 0; nom[i]; i++) if (nom[i] == '/') nom[i] = '-';               /* la barra separa niveles en las rutas */
    if (!nom[0]) return -1;
    struct mc_nodo *n = &m->nodo[nodo];
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) {
        const struct mc_nodo *o = &m->nodo[i];
        if (i != nodo && o->usado && o->padre == n->padre && o->nivel == n->nivel) {
            char a[MC_NODO_NOMBRE + 8], b[MC_NODO_NOMBRE + 8]; mc_fold(o->nombre, a, sizeof a); mc_fold(nom, b, sizeof b);
            if (a[0] && starts(a, b) && starts(b, a)) return -1;
        }
    }
    uint32_t i = 0; while (nom[i] && i < MC_NODO_NOMBRE - 1) { n->nombre[i] = nom[i]; i++; } n->nombre[i] = 0;
    n->fijo = 1; m->arbol_sucio = 1;                                                 /* lo nombrado a mano no lo poda el sueno */
    return 0;
}
uint32_t mc_nodo_quita(struct mc *m, uint32_t nodo, uint8_t *marca)
{
    if (!m || !nodo || nodo > MC_NODOS_MAX || !m->nodo[nodo].usado) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < m->n; i++) {
        struct mc_item *e = &m->it[i];
        if (e->kind == MC_CELDA && e->nodo && mc_nodo_dentro(m, e->nodo, nodo)) { e->nodo = 0; ensucia(m, e->id); if (marca) marca[i] = 1; n++; }
    }
    uint32_t padre = m->nodo[nodo].padre;
    uint8_t va[MC_NODOS_MAX + 1];
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) va[i] = m->nodo[i].usado && mc_nodo_dentro(m, i, nodo);
    for (uint32_t i = 1; i <= MC_NODOS_MAX; i++) if (va[i]) { struct mc_nodo *x = &m->nodo[i]; for (uint32_t b = 0; b < sizeof *x; b++) ((uint8_t *)x)[b] = 0; }
    if (padre && m->nodo[padre].usado) m->nodo[padre].sucio = 1;
    m->arbol_sucio = 1;
    mc_recuenta(m);
    return n;
}
static void esc_campo(char *o, uint32_t cap, uint32_t *p, const char *s)
{
    for (; s && *s && *p + 3 < cap; s++) {
        char c = *s;
        if (c == '\\') { o[(*p)++] = '\\'; o[(*p)++] = '\\'; }
        else if (c == '\t') { o[(*p)++] = '\\'; o[(*p)++] = 't'; }
        else if (c == '\n') { o[(*p)++] = '\\'; o[(*p)++] = 'n'; }
        else if (c == '\r') { o[(*p)++] = '\\'; o[(*p)++] = 'r'; }
        else o[(*p)++] = c;
    }
    o[*p] = 0;
}
static void put_u(char *o, uint32_t cap, uint32_t *p, uint32_t v)
{
    char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) { if (*p + 1 < cap) o[(*p)++] = t[--n]; else n--; }
    if (cap) o[*p < cap ? *p : cap - 1] = 0;
}
uint32_t mc_smem_cabecera(const char *modelo, uint32_t dim, char *out, uint32_t cap)
{
    uint32_t p = 0;
    put(out, cap, &p, "#SOMAMEM 1 modelo="); put(out, cap, &p, modelo && *modelo ? modelo : "ninguno");
    put(out, cap, &p, " dim="); put_u(out, cap, &p, dim); put(out, cap, &p, " parte=1/1\n");
    return p + 1 < cap ? p : 0;
}
uint32_t mc_smem_nodo(const struct mc *m, uint32_t nodo, char *out, uint32_t cap)
{
    if (!m || !nodo || nodo > MC_NODOS_MAX || !m->nodo[nodo].usado) return 0;
    char ruta[3 * MC_NODO_NOMBRE + 8]; mc_nodo_ruta_texto(m, nodo, ruta, sizeof ruta);
    uint32_t p = 0;
    put(out, cap, &p, "N\t"); esc_campo(out, cap, &p, ruta); put(out, cap, &p, "\t"); esc_campo(out, cap, &p, m->nodo[nodo].desc); put(out, cap, &p, "\n");
    return p + 4 < cap ? p : 0;
}
static void b64(char *o, uint32_t cap, uint32_t *p, const uint8_t *d, uint32_t n)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (uint32_t i = 0; i < n && *p + 5 < cap; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        o[(*p)++] = T[v >> 18 & 63]; o[(*p)++] = T[v >> 12 & 63];
        o[(*p)++] = i + 1 < n ? T[v >> 6 & 63] : '='; o[(*p)++] = i + 2 < n ? T[v & 63] : '=';
    }
    o[*p] = 0;
}
uint32_t mc_smem_item(const struct mc *m, uint32_t id, uint32_t ord, uint32_t sust_ord, int con_vec, char *out, uint32_t cap)
{
    const struct mc_item *e = mc_get(m, id);
    if (!e || cap < 16) return 0;
    uint32_t p = 0;
    if (e->kind == MC_CELDA) {
        char ruta[3 * MC_NODO_NOMBRE + 8]; ruta[0] = 0;
        if (e->nodo && m->nodo[e->nodo].usado) mc_nodo_ruta_texto(m, e->nodo, ruta, sizeof ruta);
        put(out, cap, &p, "C\t"); put_u(out, cap, &p, ord ? ord : 1); put(out, cap, &p, "\t"); put_u(out, cap, &p, e->imp); put(out, cap, &p, "\t");
        put_u(out, cap, &p, e->t); put(out, cap, &p, "\t"); put_u(out, cap, &p, e->estado > 2 ? 0 : e->estado); put(out, cap, &p, "\t");
        put_u(out, cap, &p, e->estado == MC_SUSTITUIDO ? sust_ord : 0); put(out, cap, &p, "\t"); put_u(out, cap, &p, e->estado == MC_SUSTITUIDO ? e->t_hasta : 0); put(out, cap, &p, "\t");
        esc_campo(out, cap, &p, ruta); put(out, cap, &p, "\t"); esc_campo(out, cap, &p, e->fuente); put(out, cap, &p, "\t"); esc_campo(out, cap, &p, e->texto);
    } else {
        put(out, cap, &p, "E\t"); put_u(out, cap, &p, e->t); put(out, cap, &p, "\t"); put_u(out, cap, &p, e->rol); put(out, cap, &p, "\t"); esc_campo(out, cap, &p, e->texto);
    }
    if (con_vec && e->vec && e->dim) { put(out, cap, &p, "\t"); b64(out, cap, &p, (const uint8_t *)e->vec, e->dim); }
    put(out, cap, &p, "\n");
    return p + 4 < cap ? p : 0;
}
uint32_t mc_bytes(const struct mc *m)
{
    if (!m || !m->nbloques) return m && m->it ? m->cap * (uint32_t)sizeof(struct mc_item) : 0;
    return (m->nbloques - 1) * MC_BLOQUE + m->uso + m->cap * (uint32_t)sizeof(struct mc_item);
}
