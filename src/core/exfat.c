/* SOMA_267_DISCOS: exFAT de lectura y escritura. Ver exfat.h. Especificacion: "exFAT file system specification" (Microsoft).
 * Todo lo que llega del disco se comprueba (numeros de cluster, longitudes, cadenas sin fin): un volumen danado da un error, nunca
 * una lectura o escritura fuera del volumen. */
#include "exfat.h"
#include "exfat_upcase.inc"

#define SET_MAX   19                                   /* archivo + flujo + 17 nombres (255 caracteres) */
#define EOC       0xFFFFFFFFu
#define CL_OK(v, c) ((c) >= 2u && (c) < (v)->clusters + 2u)

/* ------------------------------------------------------------------ utilidades */
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }
static void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *p, uint32_t v) { p16(p, (uint16_t)v); p16(p + 2, (uint16_t)(v >> 16)); }
static void p64(uint8_t *p, uint64_t v) { p32(p, (uint32_t)v); p32(p + 4, (uint32_t)(v >> 32)); }
static void mset(void *d, int c, uint32_t n) { uint8_t *a = d; while (n--) *a++ = (uint8_t)c; }
static void mcpy(void *d, const void *s, uint32_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; }

const char *exfat_err_str(int e)
{
    switch (e) {
    case 0: return "bien";
    case EXFAT_E_IO: return "error de lectura o escritura";
    case EXFAT_E_NOFS: return "no es exFAT";
    case EXFAT_E_UNSUP: return "exFAT no soportado";
    case EXFAT_E_NOENT: return "no existe";
    case EXFAT_E_NOTDIR: return "no es una carpeta";
    case EXFAT_E_ISDIR: return "es una carpeta";
    case EXFAT_E_CORRUPT: return "volumen danado";
    case EXFAT_E_ARG: return "argumento no valido";
    case EXFAT_E_EXIST: return "ya existe";
    case EXFAT_E_NOSPC: return "no queda sitio";
    case EXFAT_E_NOTEMPTY: return "la carpeta no esta vacia";
    case EXFAT_E_RO: return "solo lectura";
    case EXFAT_E_NOMEM: return "sin memoria";
    case EXFAT_E_NAME: return "nombre no valido";
    default: return "error";
    }
}

/* sectores RELATIVOS al volumen, siempre dentro de el */
static int rd(struct exfat_vol *v, uint64_t rel, uint32_t n, void *buf)
{
    if (rel >= v->sectors || n > v->sectors - rel) return EXFAT_E_CORRUPT;
    uint8_t *b = buf;
    while (n) { uint32_t k = n > 128 ? 128 : n; if (v->io.read(v->io.ctx, v->part_lba + rel, k, b) != 0) return EXFAT_E_IO; rel += k; b += (uint64_t)k * 512; n -= k; }
    return 0;
}
static int wr(struct exfat_vol *v, uint64_t rel, uint32_t n, const void *buf)
{
    if (!v->writable) return EXFAT_E_RO;
    if (rel >= v->sectors || n > v->sectors - rel) return EXFAT_E_CORRUPT;
    if (v->cache_lba >= rel && v->cache_lba < rel + n) v->cache_lba = ~0ull;     /* lo que habia en cache ya no vale */
    if (v->fcache_lba >= rel && v->fcache_lba < rel + n) v->fcache_lba = ~0ull;
    const uint8_t *b = buf;
    while (n) { uint32_t k = n > 128 ? 128 : n; if (v->io.write(v->io.ctx, v->part_lba + rel, k, b) != 0) return EXFAT_E_IO; rel += k; b += (uint64_t)k * 512; n -= k; }
    return 0;
}
static int sec_get(struct exfat_vol *v, uint64_t rel)
{
    if (v->cache_lba == rel) return 0;
    int e = rd(v, rel, 1, v->cache);
    v->cache_lba = e ? ~0ull : rel;
    return e;
}
static uint64_t clus_sec(const struct exfat_vol *v, uint32_t c) { return (uint64_t)v->heap_off + ((uint64_t)(c - 2) << v->spc_shift); }
static uint32_t spc(const struct exfat_vol *v) { return 1u << v->spc_shift; }

/* ------------------------------------------------------------------ FAT */
static int fat_get(struct exfat_vol *v, uint32_t c, uint32_t *out)
{
    uint64_t s = v->fat_off + (uint64_t)c / 128u;
    if (v->fcache_lba != s) { int e = rd(v, s, 1, v->fcache); if (e) { v->fcache_lba = ~0ull; return e; } v->fcache_lba = s; }
    *out = le32(v->fcache + (c % 128u) * 4u);
    return 0;
}
static uint32_t g_walk_first, g_walk_idx, g_walk_cl;                /* pista de cluster_at (abajo) */
static int fat_set(struct exfat_vol *v, uint32_t c, uint32_t val)
{
    g_walk_first = 0;                                                /* la FAT cambia: la pista ya no vale */
    uint64_t s = v->fat_off + (uint64_t)c / 128u;
    if (v->fcache_lba != s) { int e = rd(v, s, 1, v->fcache); if (e) { v->fcache_lba = ~0ull; return e; } v->fcache_lba = s; }
    p32(v->fcache + (c % 128u) * 4u, val);
    uint8_t tmp[512]; mcpy(tmp, v->fcache, 512);
    int e = wr(v, s, 1, tmp);
    if (!e) { mcpy(v->fcache, tmp, 512); v->fcache_lba = s; }
    return e;
}
/* el cluster que sigue a c en una cadena (EOC al final); nofat: contiguo */
static int next_cl(struct exfat_vol *v, uint32_t c, int nofat, uint32_t *out)
{
    if (nofat) { *out = c + 1; return 0; }
    uint32_t n; int e = fat_get(v, c, &n); if (e) return e;
    if (n >= 0xFFFFFFF8u) { *out = EOC; return 0; }
    if (!CL_OK(v, n)) return EXFAT_E_CORRUPT;
    *out = n;
    return 0;
}

/* ------------------------------------------------------------------ mapa de cluster libres */
static int bm_get(const struct exfat_vol *v, uint32_t c) { uint32_t i = c - 2; return (v->bitmap[i >> 3] >> (i & 7)) & 1; }
/* el sector del archivo del mapa que guarda el byte b */
static int bm_sector(struct exfat_vol *v, uint64_t byte, uint64_t *sec)
{
    uint32_t k = (uint32_t)(byte / v->cluster_bytes), c = v->bitmap_first;
    for (uint32_t i = 0; i < k; i++) { int e = next_cl(v, c, 0, &c); if (e) return e; if (c == EOC) return EXFAT_E_CORRUPT; }
    *sec = clus_sec(v, c) + (byte % v->cluster_bytes) / 512u;
    return 0;
}
/* marca [c, c+n) como usado (val=1) o libre (val=0) en memoria y en el disco */
static int bm_set(struct exfat_vol *v, uint32_t c, uint32_t n, int val)
{
    if (!n) return 0;
    if (!CL_OK(v, c) || n > v->clusters + 2u - c) return EXFAT_E_CORRUPT;
    if (!val) g_walk_first = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t b = c - 2 + i; uint8_t m = (uint8_t)(1u << (b & 7));
        int was = (v->bitmap[b >> 3] & m) != 0;
        if (val && !was) { v->bitmap[b >> 3] |= m; v->free_count--; }
        if (!val && was) { v->bitmap[b >> 3] &= (uint8_t)~m; v->free_count++; }
    }
    uint64_t first = ((uint64_t)(c - 2) >> 3) & ~511ull, last = (uint64_t)(c - 2 + n - 1) >> 3;
    for (uint64_t off = first; off <= last; off += 512) {
        uint64_t s; int e = bm_sector(v, off, &s); if (e) return e;
        uint8_t tmp[512]; mset(tmp, 0, 512);
        uint64_t k = v->bitmap_bytes - off; if (k > 512) k = 512;
        mcpy(tmp, v->bitmap + off, (uint32_t)k);
        e = wr(v, s, 1, tmp); if (e) return e;
    }
    return 0;
}
/* n cluster seguidos libres (desde la pista); 0 si no hay */
static uint32_t find_run(struct exfat_vol *v, uint32_t n, uint32_t from)
{
    if (!n || n > v->free_count) return 0;
    uint32_t total = v->clusters, run = 0, start = 0;
    if (from < 2 || from >= total + 2) from = 2;
    for (uint32_t pass = 0; pass < 2; pass++) {
        uint32_t a = pass ? 2 : from, b = pass ? from : total + 2;
        run = 0;
        for (uint32_t c = a; c < b; c++) {
            if (bm_get(v, c)) { run = 0; continue; }
            if (!run) start = c;
            if (++run == n) return start;
        }
    }
    return 0;
}
/* reserva n cluster: seguidos si se puede (*nofat = 1) o en cadena de la FAT. *first = 0 si n = 0 */
static int alloc_clusters(struct exfat_vol *v, uint32_t n, uint32_t *first, int *nofat)
{
    *first = 0; *nofat = 1;
    if (!n) return 0;
    if (n > v->free_count) return EXFAT_E_NOSPC;
    uint32_t s = find_run(v, n, v->hint);
    if (s) {
        int e = bm_set(v, s, n, 1); if (e) return e;
        v->hint = s + n; *first = s; return 0;
    }
    *nofat = 0;                                                      /* en cadena: cluster a cluster */
    uint32_t prev = 0, got = 0, c = v->hint >= 2 && v->hint < v->clusters + 2 ? v->hint : 2;
    for (uint32_t step = 0; got < n && step < v->clusters; step++, c++) {
        if (c >= v->clusters + 2) c = 2;
        if (bm_get(v, c)) continue;
        int e = bm_set(v, c, 1, 1); if (!e) e = fat_set(v, c, EOC);
        if (!e && prev) e = fat_set(v, prev, c);
        if (e) return e;
        if (!prev) *first = c;
        prev = c; got++;
    }
    if (got < n) return EXFAT_E_NOSPC;
    v->hint = prev + 1;
    return 0;
}
/* libera una cadena (nofat: n seguidos; si no, siguiendo la FAT hasta EOC, como mucho maxn) */
static int free_clusters(struct exfat_vol *v, uint32_t first, int nofat, uint32_t n)
{
    if (!first) return 0;
    if (!CL_OK(v, first)) return EXFAT_E_CORRUPT;
    if (nofat) return bm_set(v, first, n, 0);
    uint32_t c = first;
    for (uint32_t i = 0; c != EOC && i <= v->clusters; i++) {
        uint32_t nx; int e = next_cl(v, c, 0, &nx); if (e) return e;
        e = bm_set(v, c, 1, 0); if (!e) e = fat_set(v, c, 0); if (e) return e;
        c = nx;
    }
    return 0;
}

/* ------------------------------------------------------------------ nombres */
static int utf8_to16(const char *s, uint32_t len, uint16_t *out, uint32_t *n)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < len;) {
        uint32_t c = (uint8_t)s[i], need = 0;
        if (c < 0x80) need = 0; else if ((c & 0xE0) == 0xC0) { c &= 0x1F; need = 1; } else if ((c & 0xF0) == 0xE0) { c &= 0x0F; need = 2; }
        else if ((c & 0xF8) == 0xF0) { c &= 0x07; need = 3; } else return EXFAT_E_NAME;
        if (i + need >= len && need) return EXFAT_E_NAME;
        i++;
        for (uint32_t j = 0; j < need; j++, i++) { if (i >= len || ((uint8_t)s[i] & 0xC0) != 0x80) return EXFAT_E_NAME; c = (c << 6) | ((uint8_t)s[i] & 0x3F); }
        if (c < 0x20 || c == '"' || c == '*' || c == '/' || c == ':' || c == '<' || c == '>' || c == '?' || c == '\\' || c == '|' || c == 0x7F) return EXFAT_E_NAME;
        if ((c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) return EXFAT_E_NAME;
        if (c >= 0x10000) { if (k + 2 > 255) return EXFAT_E_NAME; c -= 0x10000; out[k++] = (uint16_t)(0xD800 | (c >> 10)); out[k++] = (uint16_t)(0xDC00 | (c & 0x3FF)); }
        else { if (k + 1 > 255) return EXFAT_E_NAME; out[k++] = (uint16_t)c; }
    }
    if (!k) return EXFAT_E_NAME;
    if (k == 1 && out[0] == '.') return EXFAT_E_NAME;
    if (k == 2 && out[0] == '.' && out[1] == '.') return EXFAT_E_NAME;
    *n = k;
    return 0;
}
static void utf16_to8(const uint16_t *u, uint32_t n, char *o, uint32_t cap)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = u[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && u[i + 1] >= 0xDC00 && u[i + 1] <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (u[i + 1] - 0xDC00); i++; }
        else if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
        uint8_t b[4]; uint32_t m;
        if (c < 0x80) { b[0] = (uint8_t)c; m = 1; }
        else if (c < 0x800) { b[0] = (uint8_t)(0xC0 | c >> 6); b[1] = (uint8_t)(0x80 | (c & 63)); m = 2; }
        else if (c < 0x10000) { b[0] = (uint8_t)(0xE0 | c >> 12); b[1] = (uint8_t)(0x80 | (c >> 6 & 63)); b[2] = (uint8_t)(0x80 | (c & 63)); m = 3; }
        else { b[0] = (uint8_t)(0xF0 | c >> 18); b[1] = (uint8_t)(0x80 | (c >> 12 & 63)); b[2] = (uint8_t)(0x80 | (c >> 6 & 63)); b[3] = (uint8_t)(0x80 | (c & 63)); m = 4; }
        if (k + m + 1 > cap) break;
        for (uint32_t j = 0; j < m; j++) o[k++] = (char)b[j];
    }
    if (cap) o[k] = 0;
}
static uint16_t up(const struct exfat_vol *v, uint16_t c) { return v->upcase ? v->upcase[c] : (c >= 'a' && c <= 'z') ? (uint16_t)(c - 32) : c; }
static uint16_t name_hash(const struct exfat_vol *v, const uint16_t *u, uint32_t n)
{
    uint16_t h = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t c = up(v, u[i]);
        h = (uint16_t)(((h & 1) ? 0x8000 : 0) + (h >> 1) + (c & 0xFF));
        h = (uint16_t)(((h & 1) ? 0x8000 : 0) + (h >> 1) + (c >> 8));
    }
    return h;
}
static uint16_t set_sum(const uint8_t *set, uint32_t cnt)
{
    uint16_t s = 0;
    for (uint32_t i = 0; i < cnt * 32u; i++) { if (i == 2 || i == 3) continue; s = (uint16_t)(((s & 1) ? 0x8000 : 0) + (s >> 1) + set[i]); }
    return s;
}

/* ------------------------------------------------------------------ directorios */
struct dpos { uint64_t sec; uint16_t off; };
/* un archivo o carpeta: donde empiezan sus datos y su juego de entradas (para reescribirlo) */
struct node {
    uint32_t first; uint64_t size, valid; uint16_t attr; int nofat, root, cnt;
    uint8_t set[SET_MAX * 32]; struct dpos pos[SET_MAX];
};
struct dit { struct exfat_vol *v; uint32_t cl, idx, nclus; int nofat, done; uint32_t sec, ent; };
static void dit_start(struct dit *it, struct exfat_vol *v, const struct node *d)
{
    it->v = v; it->cl = d->first; it->idx = 0; it->nofat = d->nofat; it->sec = 0; it->ent = 0;
    it->nclus = d->root ? 0 : (uint32_t)((d->size + v->cluster_bytes - 1) / v->cluster_bytes);
    it->done = !CL_OK(v, d->first) || (!d->root && !it->nclus);
}
/* la siguiente entrada de 32 bytes (copiada en e) y su posicion; 1 = hay, 0 = fin, <0 error */
static int dit_next(struct dit *it, uint8_t e[32], struct dpos *p)
{
    struct exfat_vol *v = it->v;
    if (it->done) return 0;
    uint64_t s = clus_sec(v, it->cl) + it->sec;
    int r = sec_get(v, s); if (r) return r;
    mcpy(e, v->cache + it->ent * 32u, 32);
    p->sec = s; p->off = (uint16_t)(it->ent * 32u);
    if (++it->ent == 16) {
        it->ent = 0;
        if (++it->sec == spc(v)) {
            it->sec = 0; it->idx++;
            if (it->nclus && it->idx >= it->nclus) it->done = 1;
            else if (it->idx > v->clusters) return EXFAT_E_CORRUPT;      /* cadena en bucle */
            else {
                uint32_t nx; r = next_cl(v, it->cl, it->nofat, &nx); if (r) return r;
                if (nx == EOC) it->done = 1; else if (!CL_OK(v, nx)) return EXFAT_E_CORRUPT; else it->cl = nx;
            }
        }
    }
    return 1;
}
/* lee el resto del juego de entradas que empieza en e0 (ya leida); deja el juego en n. 1 = juego de archivo valido, 0 = no lo es */
static int read_set(struct dit *it, const uint8_t e0[32], const struct dpos *p0, struct node *n, uint16_t *name, uint32_t *nlen)
{
    uint32_t sc = e0[1];
    mcpy(n->set, e0, 32); n->pos[0] = *p0;
    uint8_t e[32]; struct dpos p;
    uint32_t got = 1;
    for (uint32_t i = 0; i < sc; i++) {
        int r = dit_next(it, e, &p); if (r <= 0) return r < 0 ? r : 0;
        if (got < SET_MAX) { mcpy(n->set + got * 32u, e, 32); n->pos[got] = p; got++; }
        else got++;                                                   /* entradas de fabricante: se leen y no se guardan */
    }
    n->cnt = (int)(got > SET_MAX ? 0 : got);                          /* 0: no se puede reescribir (demasiadas entradas) */
    const uint8_t *st = n->set + 32;
    if (sc < 2 || st[0] != 0xC0) return 0;
    uint32_t nl = st[3], ne = (nl + 14) / 15;
    if (!nl || ne > sc - 1 || ne + 2 > SET_MAX) return 0;
    for (uint32_t i = 0; i < ne; i++) {
        const uint8_t *fe = n->set + (2 + i) * 32u;
        if (fe[0] != 0xC1) return 0;
        for (uint32_t j = 0; j < 15 && i * 15 + j < nl; j++) name[i * 15 + j] = le16(fe + 2 + j * 2);
    }
    *nlen = nl;
    n->attr = le16(n->set + 4);
    n->nofat = (st[1] & 2) != 0;
    n->first = le32(st + 20);
    n->size = le64(st + 24);
    n->valid = le64(st + 8);
    n->root = 0;
    if (n->valid > n->size) n->valid = n->size;
    if (n->size && !CL_OK(it->v, n->first)) return 0;
    if (n->cnt && set_sum(n->set, (uint32_t)n->cnt) != le16(n->set + 2)) return 0;   /* suma de comprobacion mala: se ignora */
    return 1;
}

static int name_eq(const struct exfat_vol *v, const uint16_t *a, uint32_t na, const uint16_t *b, uint32_t nb)
{
    if (na != nb) return 0;
    for (uint32_t i = 0; i < na; i++) if (up(v, a[i]) != up(v, b[i])) return 0;
    return 1;
}
static uint16_t g_nbuf[256];
/* busca name en la carpeta d. 0 = encontrado (en out), EXFAT_E_NOENT, o error */
static int lookup(struct exfat_vol *v, const struct node *d, const uint16_t *name, uint32_t nl, struct node *out)
{
    if (!(d->attr & 0x10) && !d->root) return EXFAT_E_NOTDIR;
    uint16_t h = name_hash(v, name, nl);
    struct dit it; dit_start(&it, v, d);
    uint8_t e[32]; struct dpos p;
    for (;;) {
        int r = dit_next(&it, e, &p); if (r < 0) return r; if (!r) return EXFAT_E_NOENT;
        if (e[0] == 0x00) return EXFAT_E_NOENT;
        if (e[0] != 0x85) continue;
        uint32_t ln = 0;
        r = read_set(&it, e, &p, out, g_nbuf, &ln); if (r < 0) return r;
        if (!r) continue;
        if (le16(out->set + 32 + 4) != h) continue;
        if (name_eq(v, g_nbuf, ln, name, nl)) return 0;
    }
}
static void root_node(const struct exfat_vol *v, struct node *n)
{
    n->first = v->root; n->size = 0; n->valid = 0; n->attr = 0x10; n->nofat = 0; n->root = 1; n->cnt = 0;
}
static struct node g_n[4];                                            /* resolver, padre, destino (un volumen a la vez: lo protege quien llama) */
static uint16_t g_last[256]; static uint32_t g_last_n;
/* path -> nodo. Con parent_only, *out es la carpeta que contiene el ultimo nombre (que queda en g_last) */
static int resolve(struct exfat_vol *v, const char *path, struct node *out, int parent_only)
{
    root_node(v, out);
    if (!path) return EXFAT_E_ARG;
    while (*path == '/') path++;
    g_last_n = 0;
    while (*path) {
        const char *e = path; while (*e && *e != '/') e++;
        const char *nx = e; while (*nx == '/') nx++;
        uint32_t nl; int r = utf8_to16(path, (uint32_t)(e - path), g_last, &nl); if (r) return r;
        g_last_n = nl;
        if (parent_only && !*nx) return 0;
        if (!(out->attr & 0x10)) return EXFAT_E_NOTDIR;
        r = lookup(v, out, g_last, nl, &g_n[3]); if (r) return r;
        mcpy(out, &g_n[3], sizeof *out);
        path = nx;
    }
    return parent_only ? EXFAT_E_ARG : 0;                              /* "/" no tiene padre */
}
static void fill_dirent(const struct node *n, const uint16_t *name, uint32_t nl, struct exfat_dirent *d)
{
    if (name) utf16_to8(name, nl, d->name, sizeof d->name); else d->name[0] = 0;
    d->size = n->root ? 0 : n->size; d->first_cluster = n->first; d->attr = n->root ? 0x10 : n->attr;
    d->is_dir = (d->attr & 0x10) != 0; d->nofat = (uint8_t)n->nofat;
    d->mtime = n->root ? 0 : le32(n->set + 12);
}

int exfat_stat(struct exfat_vol *v, const char *path, struct exfat_dirent *out)
{
    int r = resolve(v, path, &g_n[0], 0); if (r) return r;
    fill_dirent(&g_n[0], g_last_n ? g_last : 0, g_last_n, out);
    return 0;
}
int exfat_readdir(struct exfat_vol *v, const char *path, struct exfat_dirent *out, int max, int *total)
{
    int r = resolve(v, path, &g_n[0], 0); if (r) return r;
    if (!(g_n[0].attr & 0x10)) return EXFAT_E_NOTDIR;
    struct dit it; dit_start(&it, v, &g_n[0]);
    uint8_t e[32]; struct dpos p; int n = 0, t = 0;
    for (;;) {
        r = dit_next(&it, e, &p); if (r < 0) return r; if (!r || e[0] == 0) break;
        if (e[0] != 0x85) continue;
        uint32_t ln = 0;
        r = read_set(&it, e, &p, &g_n[1], g_nbuf, &ln); if (r < 0) return r;
        if (!r) continue;
        if (n < max && out) fill_dirent(&g_n[1], g_nbuf, ln, &out[n++]);
        t++;
    }
    if (total) *total = t;
    return n;
}

/* el cluster numero idx de un archivo (con una pista para lecturas seguidas) */
static int cluster_at(struct exfat_vol *v, const struct node *n, uint32_t idx, uint32_t *out)
{
    if (n->nofat) { *out = n->first + idx; return CL_OK(v, *out) ? 0 : EXFAT_E_CORRUPT; }
    uint32_t c = n->first, i = 0;
    if (g_walk_first == n->first && g_walk_idx <= idx && CL_OK(v, g_walk_cl)) { c = g_walk_cl; i = g_walk_idx; }
    for (; i < idx; i++) { int e = next_cl(v, c, 0, &c); if (e) return e; if (c == EOC) return EXFAT_E_CORRUPT; }
    g_walk_first = n->first; g_walk_idx = idx; g_walk_cl = c;
    *out = c;
    return 0;
}
static int read_node(struct exfat_vol *v, struct node *n, uint64_t off, void *buf, uint32_t len);
int exfat_read(struct exfat_vol *v, const char *path, uint64_t off, void *buf, uint32_t len)
{
    int r = resolve(v, path, &g_n[0], 0); if (r) return r;
    return read_node(v, &g_n[0], off, buf, len);
}
int exfat_open(struct exfat_vol *v, const char *path, struct exfat_file *f)
{
    int r = resolve(v, path, &g_n[0], 0); if (r) return r;
    if (g_n[0].attr & 0x10) return EXFAT_E_ISDIR;
    f->first = g_n[0].first; f->size = g_n[0].size; f->valid = g_n[0].valid; f->nofat = (uint8_t)g_n[0].nofat; f->serial = v->serial;
    return 0;
}
int exfat_pread(struct exfat_vol *v, const struct exfat_file *f, uint64_t off, void *buf, uint32_t len)
{
    if (f->serial != v->serial) return EXFAT_E_NOENT;                 /* el volumen ya no es el mismo */
    if (f->valid > f->size || (f->size && !CL_OK(v, f->first))) return EXFAT_E_CORRUPT;
    struct node *n = &g_n[0];
    mset(n, 0, sizeof *n);
    n->first = f->first; n->size = f->size; n->valid = f->valid; n->nofat = f->nofat; n->attr = 0x20;
    return read_node(v, n, off, buf, len);
}
static int read_node(struct exfat_vol *v, struct node *n, uint64_t off, void *buf, uint32_t len)
{
    int r;
    if (n->attr & 0x10) return EXFAT_E_ISDIR;
    if (off >= n->size) return 0;
    if (len > n->size - off) len = (uint32_t)(n->size - off);
    uint8_t *b = buf; uint32_t done = 0;
    while (done < len) {
        uint64_t o = off + done;
        if (o >= n->valid) { mset(b + done, 0, len - done); done = len; break; }          /* mas alla de lo escrito: ceros */
        uint32_t c; r = cluster_at(v, n, (uint32_t)(o / v->cluster_bytes), &c); if (r) return r;
        uint64_t s = clus_sec(v, c) + (o % v->cluster_bytes) / 512u;
        uint32_t in = (uint32_t)(o % 512u), k = 512u - in;
        if (k > len - done) k = len - done;
        if (o + k > n->valid) k = (uint32_t)(n->valid - o);
        if (in == 0 && k == 512u) {                                   /* sectores enteros seguidos dentro del cluster */
            uint32_t left = (uint32_t)(v->cluster_bytes - o % v->cluster_bytes) / 512u, want = (len - done) / 512u;
            uint64_t vleft = (n->valid - o) / 512u;
            if (want > left) want = left;
            if (want > vleft) want = (uint32_t)vleft;
            if (want > 128) want = 128;
            if (want >= 1) { r = rd(v, s, want, b + done); if (r) return r; done += want * 512u; continue; }
        }
        r = sec_get(v, s); if (r) return r;
        mcpy(b + done, v->cache + in, k);
        done += k;
    }
    return (int)done;
}

/* ------------------------------------------------------------------ escribir entradas */
static int write_set(struct exfat_vol *v, const struct node *n)
{
    /* sector a sector: todas las entradas del juego que caen en el */
    for (int i = 0; i < n->cnt;) {
        uint64_t s = n->pos[i].sec;
        int r = sec_get(v, s); if (r) return r;
        uint8_t tmp[512]; mcpy(tmp, v->cache, 512);
        int j = i;
        for (; j < n->cnt && n->pos[j].sec == s; j++) mcpy(tmp + n->pos[j].off, n->set + j * 32u, 32);
        r = wr(v, s, 1, tmp); if (r) return r;
        i = j;
    }
    return 0;
}
static void set_finish(struct node *n)
{
    p16(n->set + 2, set_sum(n->set, (uint32_t)n->cnt));
}
/* actualiza el flujo (cluster, tamanos, cadena) y la fecha de un juego ya leido, y lo escribe */
static int update_stream(struct exfat_vol *v, struct node *n, uint32_t first, uint64_t size, int nofat, int touch)
{
    if (!n->cnt) return EXFAT_E_UNSUP;
    uint8_t *st = n->set + 32;
    st[1] = (uint8_t)(0x01 | (nofat && first ? 0x02 : 0));
    p32(st + 20, first); p64(st + 24, size); p64(st + 8, size);
    if (touch) { uint32_t t = v->io.now ? v->io.now() : 0; p32(n->set + 12, t); p32(n->set + 16, t); n->set[21] = 0; n->set[23] = 0; n->set[24] = 0; }
    n->first = first; n->size = n->valid = size; n->nofat = nofat && first;
    set_finish(n);
    return write_set(v, n);
}
static int zero_cluster(struct exfat_vol *v, uint32_t c)
{
    static uint8_t z[512 * 16];
    mset(z, 0, sizeof z);
    uint32_t s = spc(v);
    for (uint32_t k = 0; k < s; k += 16) { int r = wr(v, clus_sec(v, c) + k, s - k < 16 ? s - k : 16, z); if (r) return r; }
    return 0;
}
/* una carpeta mas larga (un cluster a cero al final). d es el nodo de la carpeta; se actualiza su entrada en el padre */
static int dir_grow(struct exfat_vol *v, struct node *d, uint32_t *newc)
{
    uint32_t nclus = d->root ? 0 : (uint32_t)(d->size / v->cluster_bytes), last = d->first;
    if (d->root) {                                                   /* la raiz: siguiendo la FAT */
        for (uint32_t i = 0; i <= v->clusters; i++) { uint32_t nx; int r = next_cl(v, last, 0, &nx); if (r) return r; if (nx == EOC) break; last = nx; }
    } else if (d->nofat) last = d->first + nclus - 1;
    else { int r = cluster_at(v, d, nclus - 1, &last); if (r) return r; }
    uint32_t c = 0;
    if (d->nofat && CL_OK(v, last + 1) && !bm_get(v, last + 1)) c = last + 1;     /* sigue seguida */
    else { c = find_run(v, 1, last + 1); if (!c) return EXFAT_E_NOSPC; }
    int r = bm_set(v, c, 1, 1); if (r) return r;
    r = zero_cluster(v, c); if (r) return r;
    int nofat = d->nofat;
    if (nofat && c != last + 1) {                                    /* deja de ser seguida: se escribe su cadena en la FAT */
        for (uint32_t i = 0; i + 1 < nclus; i++) { r = fat_set(v, d->first + i, d->first + i + 1); if (r) return r; }
        nofat = 0;
    }
    if (!nofat) { r = fat_set(v, c, EOC); if (!r) r = fat_set(v, last, c); if (r) return r; }
    *newc = c;
    if (d->root) return 0;
    return update_stream(v, d, d->first, d->size + v->cluster_bytes, nofat, 0);
}
/* sitio para cnt entradas seguidas en la carpeta d (alargandola si hace falta): posiciones en pos */
static int find_slots(struct exfat_vol *v, struct node *d, int cnt, struct dpos *pos)
{
    struct dit it; dit_start(&it, v, d);
    uint8_t e[32]; struct dpos p; int run = 0;
    for (;;) {
        int r = dit_next(&it, e, &p); if (r < 0) return r; if (!r) break;
        if (e[0] & 0x80) { run = 0; continue; }                       /* en uso */
        pos[run++] = p;
        if (run == cnt) return 0;
    }
    while (run < cnt) {                                              /* no cabe: un cluster mas (las posiciones siguen en el) */
        uint32_t c; int r = dir_grow(v, d, &c); if (r) return r;
        for (uint32_t s = 0; s < spc(v) && run < cnt; s++)
            for (uint32_t k = 0; k < 16 && run < cnt; k++) { pos[run].sec = clus_sec(v, c) + s; pos[run].off = (uint16_t)(k * 32u); run++; }
    }
    return 0;
}
/* juego nuevo (archivo o carpeta) con nombre en la carpeta d; deja el nodo en n */
static int create_set(struct exfat_vol *v, struct node *d, const uint16_t *name, uint32_t nl, uint16_t attr, uint32_t first, uint64_t size, int nofat, struct node *n,
                      const uint8_t *times)
{
    uint32_t ne = (nl + 14) / 15; int cnt = (int)(2 + ne);
    mset(n->set, 0, sizeof n->set);
    uint8_t *f = n->set, *st = n->set + 32;
    f[0] = 0x85; f[1] = (uint8_t)(cnt - 1); p16(f + 4, attr);
    if (times) mcpy(f + 8, times, 18);
    else { uint32_t t = v->io.now ? v->io.now() : 0; p32(f + 8, t); p32(f + 12, t); p32(f + 16, t); }
    st[0] = 0xC0; st[1] = (uint8_t)(0x01 | (nofat && first ? 0x02 : 0)); st[3] = (uint8_t)nl; p16(st + 4, name_hash(v, name, nl));
    p64(st + 8, size); p32(st + 20, first); p64(st + 24, size);
    for (uint32_t i = 0; i < ne; i++) {
        uint8_t *fe = n->set + (2 + i) * 32u; fe[0] = 0xC1;
        for (uint32_t j = 0; j < 15 && i * 15 + j < nl; j++) p16(fe + 2 + j * 2, name[i * 15 + j]);
    }
    n->cnt = cnt; n->attr = attr; n->first = first; n->size = n->valid = size; n->nofat = nofat && first; n->root = 0;
    set_finish(n);
    int r = find_slots(v, d, cnt, n->pos); if (r) return r;
    return write_set(v, n);
}
static int delete_set(struct exfat_vol *v, struct node *n)
{
    if (!n->cnt) return EXFAT_E_UNSUP;
    for (int i = 0; i < n->cnt; i++) n->set[i * 32] &= 0x7F;
    return write_set(v, n);
}
/* escribe len bytes en cluster ya reservados desde el cluster first (seguidos o en cadena), desde el byte off del archivo */
static int put_data(struct exfat_vol *v, struct node *n, uint64_t off, const uint8_t *data, uint64_t len)
{
    uint64_t done = 0;
    while (done < len) {
        uint64_t o = off + done;
        uint32_t c; int r = cluster_at(v, n, (uint32_t)(o / v->cluster_bytes), &c); if (r) return r;
        uint64_t s = clus_sec(v, c) + (o % v->cluster_bytes) / 512u;
        uint32_t in = (uint32_t)(o % 512u);
        if (in == 0 && len - done >= 512u) {
            uint32_t left = (uint32_t)(v->cluster_bytes - o % v->cluster_bytes) / 512u, want = (uint32_t)((len - done) / 512u > 128 ? 128 : (len - done) / 512u);
            if (want > left) want = left;
            r = wr(v, s, want, data + done); if (r) return r;
            done += (uint64_t)want * 512u; continue;
        }
        uint8_t tmp[512];
        if (in) { r = sec_get(v, s); if (r) return r; mcpy(tmp, v->cache, 512); } else mset(tmp, 0, 512);
        uint32_t k = 512u - in; if (k > len - done) k = (uint32_t)(len - done);
        mcpy(tmp + in, data + done, k);
        if (in + k < 512u && !in) mset(tmp + k, 0, 512u - k);
        r = wr(v, s, 1, tmp); if (r) return r;
        done += k;
    }
    return 0;
}
static uint32_t nclus_of(const struct exfat_vol *v, uint64_t bytes) { return (uint32_t)((bytes + v->cluster_bytes - 1) / v->cluster_bytes); }

int exfat_write(struct exfat_vol *v, const char *path, const void *data, uint64_t len)
{
    if (!v->writable) return EXFAT_E_RO;
    if (!data && len) return EXFAT_E_ARG;
    struct node *d = &g_n[1], *n = &g_n[2];
    int r = resolve(v, path, d, 1); if (r) return r;
    if (!(d->attr & 0x10)) return EXFAT_E_NOTDIR;
    uint16_t name[256]; uint32_t nl = g_last_n; mcpy(name, g_last, nl * 2u);
    r = lookup(v, d, name, nl, n);
    int existe = r == 0;
    if (r && r != EXFAT_E_NOENT) return r;
    if (existe && (n->attr & 0x10)) return EXFAT_E_ISDIR;
    if (existe && !n->cnt) return EXFAT_E_UNSUP;
    uint32_t need = nclus_of(v, len), old_first = existe ? n->first : 0, old_n = existe ? nclus_of(v, n->size) : 0;
    int old_nofat = existe ? n->nofat : 0;
    if (need > v->free_count) return EXFAT_E_NOSPC;
    uint32_t first; int nofat;
    r = alloc_clusters(v, need, &first, &nofat); if (r) return r;
    struct node tmp; tmp.first = first; tmp.nofat = nofat; tmp.size = tmp.valid = len; tmp.attr = 0x20; tmp.root = 0; tmp.cnt = 0;
    g_walk_first = 0;
    r = put_data(v, &tmp, 0, data, len);
    if (r) { free_clusters(v, first, nofat, need); return r; }
    if (existe) {
        n->set[4] |= 0x20;                                            /* archivo */
        r = update_stream(v, n, first, len, nofat, 1);
        if (r) { free_clusters(v, first, nofat, need); return r; }
        g_walk_first = 0;
        return free_clusters(v, old_first, old_nofat, old_n);          /* lo viejo, al final */
    }
    r = create_set(v, d, name, nl, 0x20, first, len, nofat, n, 0);
    if (r) free_clusters(v, first, nofat, need);
    return r;
}
int exfat_append(struct exfat_vol *v, const char *path, const void *data, uint32_t len)
{
    if (!v->writable) return EXFAT_E_RO;
    struct node *d = &g_n[1], *n = &g_n[2];
    int r = resolve(v, path, d, 1); if (r) return r;
    uint16_t name[256]; uint32_t nl = g_last_n; mcpy(name, g_last, nl * 2u);
    r = lookup(v, d, name, nl, n);
    if (r == EXFAT_E_NOENT) return exfat_write(v, path, data, len);
    if (r) return r;
    if (n->attr & 0x10) return EXFAT_E_ISDIR;
    if (!n->cnt) return EXFAT_E_UNSUP;
    if (!len) return 0;
    uint64_t size = n->valid, nsize = size + len;                    /* (lo reservado y no escrito se sobrescribe) */
    uint32_t have = nclus_of(v, n->size), need = nclus_of(v, nsize);
    int nofat = n->nofat; uint32_t first = n->first;
    if (need > have) {
        uint32_t add = need - have;
        if (add > v->free_count) return EXFAT_E_NOSPC;
        if (!first) {                                                 /* estaba vacio */
            r = alloc_clusters(v, add, &first, &nofat); if (r) return r;
        } else {
            uint32_t last; struct node t = *n; r = cluster_at(v, &t, have - 1, &last); if (r) return r;
            uint32_t s = nofat ? last + 1 : 0; uint32_t k = 0;
            if (nofat) { for (; k < add && CL_OK(v, s + k) && !bm_get(v, s + k); k++) { } }
            if (nofat && k == add) { r = bm_set(v, s, add, 1); if (r) return r; }   /* sigue seguido */
            else {
                if (nofat) { for (uint32_t i = 0; i + 1 < have; i++) { r = fat_set(v, first + i, first + i + 1); if (r) return r; } r = fat_set(v, last, EOC); if (r) return r; nofat = 0; }
                uint32_t nf; int nn; r = alloc_clusters(v, add, &nf, &nn); if (r) return r;
                if (nn) { for (uint32_t i = 0; i < add; i++) { r = fat_set(v, nf + i, i + 1 < add ? nf + i + 1 : EOC); if (r) return r; } }
                r = fat_set(v, last, nf); if (r) return r;
            }
        }
    }
    struct node t = *n; t.first = first; t.nofat = nofat; t.size = t.valid = nsize;
    g_walk_first = 0;
    r = put_data(v, &t, size, data, len); if (r) return r;
    g_walk_first = 0;
    return update_stream(v, n, first, nsize, nofat, 1);
}
int exfat_mkdir(struct exfat_vol *v, const char *path)
{
    if (!v->writable) return EXFAT_E_RO;
    struct node *d = &g_n[1], *n = &g_n[2];
    int r = resolve(v, path, d, 1); if (r) return r;
    if (!(d->attr & 0x10)) return EXFAT_E_NOTDIR;
    uint16_t name[256]; uint32_t nl = g_last_n; mcpy(name, g_last, nl * 2u);
    r = lookup(v, d, name, nl, n);
    if (r == 0) return EXFAT_E_EXIST;
    if (r != EXFAT_E_NOENT) return r;
    uint32_t c = find_run(v, 1, v->hint); if (!c) return EXFAT_E_NOSPC;
    r = bm_set(v, c, 1, 1); if (!r) r = zero_cluster(v, c); if (!r) r = fat_set(v, c, EOC);
    if (r) return r;
    v->hint = c + 1;
    r = create_set(v, d, name, nl, 0x10, c, v->cluster_bytes, 0, n, 0);
    if (r) free_clusters(v, c, 0, 1);
    return r;
}
static int dir_empty(struct exfat_vol *v, const struct node *d)
{
    struct dit it; dit_start(&it, v, d);
    uint8_t e[32]; struct dpos p;
    for (;;) { int r = dit_next(&it, e, &p); if (r < 0) return r; if (!r || e[0] == 0) return 1; if (e[0] == 0x85) return 0; }
}
int exfat_remove(struct exfat_vol *v, const char *path)
{
    if (!v->writable) return EXFAT_E_RO;
    struct node *n = &g_n[2];
    int r = resolve(v, path, n, 0); if (r) return r;
    if (n->root) return EXFAT_E_ARG;
    if (n->attr & 0x10) { r = dir_empty(v, n); if (r < 0) return r; if (!r) return EXFAT_E_NOTEMPTY; }
    uint32_t first = n->first, cnt = nclus_of(v, n->size); int nofat = n->nofat;
    r = delete_set(v, n); if (r) return r;
    g_walk_first = 0;
    return free_clusters(v, first, nofat, cnt);
}
int exfat_rename(struct exfat_vol *v, const char *from, const char *to)
{
    if (!v->writable) return EXFAT_E_RO;
    struct node *n = &g_n[2], *d = &g_n[1];
    int r = resolve(v, from, n, 0); if (r) return r;
    if (n->root) return EXFAT_E_ARG;
    static struct node old; mcpy(&old, n, sizeof old);
    {   /* una carpeta no puede ir dentro de si misma */
        const char *a = from, *b = to; while (*a == '/') a++; while (*b == '/') b++;
        uint32_t k = 0; while (a[k] && a[k] == b[k]) k++;
        if ((old.attr & 0x10) && !a[k] && b[k] == '/') return EXFAT_E_ARG;
    }
    r = resolve(v, to, d, 1); if (r) return r;
    if (!(d->attr & 0x10)) return EXFAT_E_NOTDIR;
    uint16_t name[256]; uint32_t nl = g_last_n; mcpy(name, g_last, nl * 2u);
    r = lookup(v, d, name, nl, &g_n[0]);
    if (r == 0) {
        if (g_n[0].pos[0].sec == old.pos[0].sec && g_n[0].pos[0].off == old.pos[0].off) {   /* mismo archivo: solo cambian mayusculas */
        } else return EXFAT_E_EXIST;
    } else if (r != EXFAT_E_NOENT) return r;
    if (!old.cnt) return EXFAT_E_UNSUP;
    uint8_t times[18]; mcpy(times, old.set + 8, 18);
    r = create_set(v, d, name, nl, old.attr, old.first, old.size, old.nofat, &g_n[0], times); if (r) return r;
    return delete_set(v, &old);
}

uint64_t exfat_free_bytes(const struct exfat_vol *v) { return (uint64_t)v->free_count * v->cluster_bytes; }
uint64_t exfat_total_bytes(const struct exfat_vol *v) { return (uint64_t)v->clusters * v->cluster_bytes; }

/* ------------------------------------------------------------------ montar */
static uint32_t boot_sum(const uint8_t *secs, uint32_t nsec)          /* sectores 0..10 de la region de arranque */
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < nsec * 512u; i++) { if (i == 106 || i == 107 || i == 112) continue; s = ((s & 1) ? 0x80000000u : 0) + (s >> 1) + secs[i]; }
    return s;
}
int exfat_is_boot(const uint8_t *s)
{
    if (s[510] != 0x55 || s[511] != 0xAA) return 0;
    static const char m[8] = { 'E', 'X', 'F', 'A', 'T', ' ', ' ', ' ' };
    for (int i = 0; i < 8; i++) if (s[3 + i] != (uint8_t)m[i]) return 0;
    return 1;
}
static int check_region(struct exfat_vol *v, uint64_t base, uint8_t *buf /* 12 sectores */)
{
    int r = rd(v, base, 12, buf); if (r) return r;
    if (!exfat_is_boot(buf)) return EXFAT_E_NOFS;
    for (int i = 11; i < 64; i++) if (buf[i]) return EXFAT_E_NOFS;          /* MustBeZero */
    uint32_t sum = boot_sum(buf, 11);
    for (int i = 0; i < 128; i++) if (le32(buf + 11 * 512 + i * 4) != sum) return EXFAT_E_CORRUPT;
    return 0;
}
static void umount_mem(struct exfat_vol *v)
{
    if (v->bitmap && v->io.free) v->io.free(v->bitmap);
    if (v->upcase && v->io.free) v->io.free(v->upcase);
    v->bitmap = 0; v->upcase = 0;
}
void exfat_umount(struct exfat_vol *v) { umount_mem(v); }
int exfat_mount(struct exfat_vol *v, const struct exfat_io *io, uint64_t part_lba, uint64_t part_sectors)
{
    mset(v, 0, sizeof *v);
    if (!io || !io->read || !io->alloc) return EXFAT_E_ARG;
    v->io = *io; v->part_lba = part_lba; v->sectors = part_sectors; v->cache_lba = ~0ull; v->fcache_lba = ~0ull;
    if (part_sectors < 64) return EXFAT_E_NOFS;
    uint8_t *b = (uint8_t *)io->alloc(12 * 512);
    if (!b) return EXFAT_E_NOMEM;
    int r = check_region(v, 0, b);
    if (r) { int r2 = check_region(v, 12, b); if (r2) { io->free(b); return r == EXFAT_E_NOFS && r2 == EXFAT_E_NOFS ? EXFAT_E_NOFS : EXFAT_E_CORRUPT; } }
    uint64_t vol_len = le64(b + 72);
    v->fat_off = le32(b + 80); v->fat_len = le32(b + 84); v->heap_off = le32(b + 88); v->clusters = le32(b + 92);
    v->root = le32(b + 96); v->serial = le32(b + 100);
    uint16_t flags = le16(b + 106); uint8_t bps = b[108], spcs = b[109]; v->nfats = b[110];
    io->free(b);
    if (bps != 9) return EXFAT_E_UNSUP;
    if (spcs > 16 || v->nfats < 1 || v->nfats > 2) return EXFAT_E_CORRUPT;
    if (vol_len > part_sectors || vol_len < 64) return EXFAT_E_CORRUPT;
    v->sectors = vol_len; v->spc_shift = spcs; v->cluster_bytes = 512u << spcs;
    if (v->fat_off < 24 || (uint64_t)v->fat_off + (uint64_t)v->fat_len * v->nfats > v->heap_off) return EXFAT_E_CORRUPT;
    if ((uint64_t)v->fat_len * 128u < (uint64_t)v->clusters + 2u) return EXFAT_E_CORRUPT;
    if ((uint64_t)v->heap_off + ((uint64_t)v->clusters << spcs) > vol_len || !v->clusters || v->clusters > 0xFFFFFFF5u) return EXFAT_E_CORRUPT;
    if (!CL_OK(v, v->root)) return EXFAT_E_CORRUPT;
    /* la raiz: mapa de cluster, tabla de mayusculas y etiqueta */
    struct node root; root_node(v, &root);
    struct dit it; dit_start(&it, v, &root);
    uint8_t e[32]; struct dpos p;
    uint32_t up_first = 0; uint64_t up_len = 0; uint32_t up_sum = 0;
    for (int k = 0; k < 4096; k++) {
        r = dit_next(&it, e, &p); if (r < 0) return r; if (!r || e[0] == 0) break;
        if (e[0] == 0x81 && !(e[1] & 1) && !v->bitmap_first) { v->bitmap_first = le32(e + 20); v->bitmap_bytes = le64(e + 24); }
        else if (e[0] == 0x82 && !up_first) { up_sum = le32(e + 4); up_first = le32(e + 20); up_len = le64(e + 24); }
        else if (e[0] == 0x83) { uint16_t lb[11]; uint32_t ln = e[1] > 11 ? 11 : e[1]; for (uint32_t i = 0; i < ln; i++) lb[i] = le16(e + 2 + i * 2); utf16_to8(lb, ln, v->label, sizeof v->label); }
    }
    if (!CL_OK(v, v->bitmap_first) || v->bitmap_bytes < (v->clusters + 7u) / 8u || v->bitmap_bytes > 0x20000000u) return EXFAT_E_CORRUPT;
    v->bitmap = (uint8_t *)io->alloc((uint32_t)v->bitmap_bytes + 512u);
    if (!v->bitmap) return EXFAT_E_NOMEM;
    {   /* el mapa, cluster a cluster */
        struct node bm; bm.first = v->bitmap_first; bm.nofat = 0; bm.size = bm.valid = v->bitmap_bytes; bm.attr = 0; bm.root = 0; bm.cnt = 0;
        g_walk_first = 0;
        for (uint64_t off = 0; off < v->bitmap_bytes; off += 512) {
            uint32_t c; r = cluster_at(v, &bm, (uint32_t)(off / v->cluster_bytes), &c); if (r) { umount_mem(v); return r; }
            r = rd(v, clus_sec(v, c) + (off % v->cluster_bytes) / 512u, 1, v->bitmap + off); if (r) { umount_mem(v); return r; }
        }
        g_walk_first = 0;
    }
    v->free_count = 0;
    for (uint32_t c = 2; c < v->clusters + 2; c++) if (!bm_get(v, c)) v->free_count++;
    v->hint = 2;
    /* la tabla de mayusculas (si falla, se usa la propia: los nombres se comparan igual en ASCII) */
    v->upcase = (uint16_t *)io->alloc(65536u * 2u);
    if (v->upcase) {
        for (uint32_t i = 0; i < 65536u; i++) v->upcase[i] = (uint16_t)i;
        int ok = 0;
        if (CL_OK(v, up_first) && up_len >= 2 && up_len <= 2u * 65536u + 4096u) {
            uint8_t *t = (uint8_t *)io->alloc((uint32_t)up_len + 512u);
            if (t) {
                struct node un; un.first = up_first; un.nofat = 0; un.size = un.valid = up_len; un.attr = 0; un.root = 0; un.cnt = 0;
                ok = 1; g_walk_first = 0;
                for (uint64_t off = 0; off < up_len && ok; off += 512) {
                    uint32_t c; if (cluster_at(v, &un, (uint32_t)(off / v->cluster_bytes), &c) || rd(v, clus_sec(v, c) + (off % v->cluster_bytes) / 512u, 1, t + off)) ok = 0;
                }
                g_walk_first = 0;
                if (ok) {
                    uint32_t s = 0; for (uint64_t i = 0; i < up_len; i++) s = ((s & 1) ? 0x80000000u : 0) + (s >> 1) + t[i];
                    if (s != up_sum) ok = 0;
                }
                if (ok) {                                             /* descomprimir: 0xFFFF n = n caracteres que no cambian */
                    uint32_t c = 0;
                    for (uint64_t i = 0; i + 1 < up_len && c < 65536u; i += 2) {
                        uint16_t x = le16(t + i);
                        if (x == 0xFFFF && i + 3 < up_len) { c += le16(t + i + 2); i += 2; continue; }
                        v->upcase[c++] = x;
                    }
                }
                io->free(t);
            }
        }
        if (!ok) for (uint32_t i = 'a'; i <= 'z'; i++) v->upcase[i] = (uint16_t)(i - 32);
    }
    v->writable = io->write != 0 && v->nfats == 1 && !(flags & 4);
    return 0;
}

/* ------------------------------------------------------------------ formatear */
int exfat_format(const struct exfat_io *io, uint64_t part_lba, uint64_t sectors, const char *label, uint32_t serial)
{
    if (!io || !io->write || !io->alloc) return EXFAT_E_ARG;
    if (sectors < 8192 || sectors > 0xFFFFFFFFull * 64) return EXFAT_E_ARG;          /* como poco 4 MiB */
    uint64_t bytes = sectors * 512u;
    uint32_t shift = bytes <= (256ull << 20) ? 3 : bytes <= (32ull << 30) ? 6 : 8;  /* 4 KiB, 32 KiB o 128 KiB (como Windows) */
    uint32_t spcn = 1u << shift, cb = 512u << shift;
    uint32_t fat_off = 2048;                                         /* 1 MiB: alineado para la memoria flash */
    if (sectors < 65536) fat_off = 128;
    uint64_t cc = (sectors - fat_off) / spcn;
    uint32_t fat_len = (uint32_t)((cc + 2) * 4 + 511) / 512u;
    uint32_t heap = (fat_off + fat_len + spcn - 1) & ~(spcn - 1);
    if (heap < fat_off + fat_len) heap = fat_off + fat_len;
    uint32_t clusters = (uint32_t)((sectors - heap) / spcn);
    if (clusters < 16) return EXFAT_E_ARG;
    uint64_t bm_bytes = (clusters + 7u) / 8u;
    uint32_t bm_cl = (uint32_t)((bm_bytes + cb - 1) / cb), up_bytes = EXFAT_UPCASE_N * 2u, up_cl = (up_bytes + cb - 1) / cb;
    uint32_t bm_first = 2, up_first = bm_first + bm_cl, root = up_first + up_cl, used = bm_cl + up_cl + 1;
    struct exfat_vol v; mset(&v, 0, sizeof v);
    v.io = *io; v.part_lba = part_lba; v.sectors = sectors; v.writable = 1; v.cache_lba = v.fcache_lba = ~0ull;
    uint8_t *z = (uint8_t *)io->alloc(128 * 512);
    if (!z) return EXFAT_E_NOMEM;
    int r = 0;
    mset(z, 0, 128 * 512);
    for (uint64_t s = 0; s < fat_off && !r; s += 128) r = wr(&v, s, (uint32_t)(fat_off - s < 128 ? fat_off - s : 128), z);   /* regiones de arranque y hueco */
    for (uint64_t s = 0; s < fat_len && !r; s += 128) r = wr(&v, fat_off + s, (uint32_t)(fat_len - s < 128 ? fat_len - s : 128), z);
    /* FAT: 0 y 1 reservados; las cadenas del mapa, de la tabla y de la raiz */
    for (uint32_t c = 0; c < used + 2 && !r; c++) {
        uint32_t val = c == 0 ? 0xFFFFFFF8u : c == 1 ? EOC : (c + 1 == up_first || c + 1 == root || c == root) ? EOC : c + 1;
        uint64_t s = fat_off + c / 128u;
        if (c % 128u == 0) mset(z, 0, 512);
        p32(z + (c % 128u) * 4u, val);
        if (c % 128u == 127 || c + 1 == used + 2) r = wr(&v, s, 1, z);
    }
    v.heap_off = heap; v.spc_shift = shift; v.cluster_bytes = cb; v.clusters = clusters;
    mset(z, 0, 128 * 512);
    for (uint32_t c = bm_first; c < root + 1 && !r; c++)
        for (uint32_t k = 0; k < spcn && !r; k += 128) r = wr(&v, clus_sec(&v, c) + k, spcn - k < 128 ? spcn - k : 128, z);
    if (!r) {                                                         /* mapa: los primeros 'used' cluster ocupados */
        for (uint32_t i = 0; i < used; i++) z[i >> 3] |= (uint8_t)(1u << (i & 7));
        r = wr(&v, clus_sec(&v, bm_first), (used / 8u + 512u) / 512u, z);
        mset(z, 0, 128 * 512);
    }
    if (!r) {                                                         /* tabla de mayusculas */
        uint8_t *t = (uint8_t *)io->alloc(up_bytes + 512u);
        if (!t) r = EXFAT_E_NOMEM;
        else {
            mset(t, 0, up_bytes + 512u);
            for (uint32_t i = 0; i < EXFAT_UPCASE_N; i++) p16(t + i * 2u, exfat_upcase_tbl[i]);
            r = wr(&v, clus_sec(&v, up_first), (up_bytes + 511u) / 512u, t);
            io->free(t);
        }
    }
    if (!r) {                                                         /* raiz: etiqueta, mapa y tabla */
        uint8_t *e = z;
        e[0] = 0x83;
        { uint16_t lb[11]; uint32_t ln = 0; if (label && *label) { uint32_t L = 0; while (label[L]) L++; if (utf8_to16(label, L, lb, &ln) || ln > 11) ln = 0; }
          e[1] = (uint8_t)ln; for (uint32_t i = 0; i < ln; i++) p16(e + 2 + i * 2, lb[i]); if (!ln) e[0] = 0x83; }
        e += 32; e[0] = 0x81; e[1] = 0; p32(e + 20, bm_first); p64(e + 24, bm_bytes);
        e += 32; e[0] = 0x82; p32(e + 4, EXFAT_UPCASE_SUM); p32(e + 20, up_first); p64(e + 24, up_bytes);
        r = wr(&v, clus_sec(&v, root), 1, z);
        mset(z, 0, 128 * 512);
    }
    if (!r) {                                                         /* region de arranque (12 sectores) y su copia */
        uint8_t *b = z;
        b[0] = 0xEB; b[1] = 0x76; b[2] = 0x90; mcpy(b + 3, "EXFAT   ", 8);
        p64(b + 64, part_lba); p64(b + 72, sectors); p32(b + 80, fat_off); p32(b + 84, fat_len); p32(b + 88, heap);
        p32(b + 92, clusters); p32(b + 96, root); p32(b + 100, serial); p16(b + 104, 0x0100); p16(b + 106, 0);
        b[108] = 9; b[109] = (uint8_t)shift; b[110] = 1; b[111] = 0x80; b[112] = (uint8_t)((uint64_t)used * 100u / clusters);
        for (int i = 120; i < 510; i++) b[i] = 0xF4;
        b[510] = 0x55; b[511] = 0xAA;
        for (int s = 1; s <= 8; s++) { b[s * 512 + 510] = 0x55; b[s * 512 + 511] = 0xAA; }
        uint32_t sum = boot_sum(b, 11);
        for (int i = 0; i < 128; i++) p32(b + 11 * 512 + i * 4, sum);
        r = wr(&v, 0, 12, b);
        if (!r) r = wr(&v, 12, 12, b);
    }
    io->free(z);
    return r;
}
