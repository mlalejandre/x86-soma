/* SOMA_267_DISCOS: ver unidades.h */
#include "unidades.h"

#define MIB 2048u
#define GIB (1u << 21)
#define P1_LBA 2048u
#define SISTEMA_SECT GIB
#define MODELOS_MAX (16ull * GIB)
#define USUARIO_MIN_GRANDE (8ull * GIB)
#define PARTE_MIN (512u * MIB)
#define AGRANDAR_MIN (64u * MIB)

const char *const UNID_CARPETAS_USUARIO[] = { "Documentos", "Im\xC3\xA1genes", "Audio", "V\xC3\xAD" "deos", "Descargas", "Proyectos", 0 };
const char *const UNID_CARPETAS_MODELOS[] = { "voz", "llm", 0 };

static struct exfat_vol g_vol[UNID_N];
static char g_ext[UNID_N][48];                         /* SOMA_363_ACCESO: "usb/NOMBRE" de los medios externos */
static int g_on[UNID_N];
static uint32_t g_gen[UNID_N];
static void (*g_lock)(void), (*g_unlock)(void);
static void lk(void) { if (g_lock) g_lock(); }
static void ulk(void) { if (g_unlock) g_unlock(); }

int unid_plan(uint64_t total, uint32_t datos_n, struct unid_part p[4])
{
    uint64_t fin = (total > 0xFFFFFFFFull ? 0xFFFFFFFFull : total) & ~(uint64_t)(MIB - 1);
    uint64_t p2 = P1_LBA + SISTEMA_SECT;
    uint64_t r0 = (p2 + datos_n + MIB - 1) & ~(uint64_t)(MIB - 1);
    if (fin <= r0 || fin - r0 < 2ull * PARTE_MIN) return -1;
    uint64_t libre = fin - r0;
    uint64_t mod = libre / 2 > libre - USUARIO_MIN_GRANDE || libre < USUARIO_MIN_GRANDE ? libre / 2 : libre - USUARIO_MIN_GRANDE;
    mod &= ~(uint64_t)(MIB - 1);
    if (mod > MODELOS_MAX) mod = MODELOS_MAX;
    p[0].lba = P1_LBA; p[0].n = SISTEMA_SECT; p[0].tipo = UNID_TIPO_FAT32;
    p[1].lba = (uint32_t)p2; p[1].n = datos_n; p[1].tipo = UNID_TIPO_DATOS;
    p[2].lba = (uint32_t)r0; p[2].n = (uint32_t)mod; p[2].tipo = UNID_TIPO_EXFAT;
    p[3].lba = (uint32_t)(r0 + mod); p[3].n = (uint32_t)(libre - mod); p[3].tipo = UNID_TIPO_EXFAT;
    return 0;
}

/* ------------------------------------------------------------------ montaje */
void unid_set_lock(void (*l)(void), void (*u)(void)) { g_lock = l; g_unlock = u; }
int unid_montar(int u, const struct exfat_io *io, uint64_t sectores)
{
    if (u < 0 || u >= UNID_N) return EXFAT_E_ARG;
    lk();
    if (g_on[u]) { exfat_umount(&g_vol[u]); g_on[u] = 0; }
    int r = exfat_mount(&g_vol[u], io, 0, sectores);
    if (r == 0) { g_on[u] = 1; g_gen[u]++; }
    ulk();
    return r;
}
int unid_montar_ext(int u, const struct exfat_io *io, uint64_t sectores, const char *nombre)
{
    if (u < UNID_EXT0 || u >= UNID_N || !nombre || !nombre[0]) return EXFAT_E_ARG;
    lk();
    uint32_t k = 0; const char *pre = "usb/"; while (pre[k]) { g_ext[u][k] = pre[k]; k++; }
    for (const char *s = nombre; *s && k + 1 < sizeof g_ext[u]; ++s) g_ext[u][k++] = *s == '/' ? '_' : *s;
    g_ext[u][k] = 0;
    ulk();
    return unid_montar(u, io, sectores);
}
void unid_desmontar(int u)
{
    if (u < 0 || u >= UNID_N) return;
    lk(); if (g_on[u]) { exfat_umount(&g_vol[u]); g_on[u] = 0; g_gen[u]++; } ulk();
}
int unid_activa(int u) { return u >= 0 && u < UNID_N && g_on[u]; }
const char *unid_nombre(int u) { return u == UNID_USUARIO ? "usuario" : u == UNID_MODELOS ? "modelos" : u >= UNID_EXT0 && u < UNID_N && g_ext[u][0] ? g_ext[u] : "?"; }
int unid_info(int u, uint64_t *libre, uint64_t *total, char *etiqueta, uint32_t cap)
{
    if (!unid_activa(u)) return EXFAT_E_NOENT;
    lk();
    if (libre) *libre = exfat_free_bytes(&g_vol[u]);
    if (total) *total = exfat_total_bytes(&g_vol[u]);
    if (etiqueta && cap) { uint32_t i = 0; for (; g_vol[u].label[i] && i + 1 < cap; i++) etiqueta[i] = g_vol[u].label[i]; etiqueta[i] = 0; }
    ulk();
    return 0;
}

/* ------------------------------------------------------------------ rutas */
static int pref(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
int unid_ruta(const char *ruta, const char **resto)
{
    static const char *const raiz = "/";
    if (!ruta) return -1;
    const char *r = ruta;
    if (*r != '/') return -1;
    int u = -1; const char *q = 0;
    if (pref(r, "/usuario")) { u = UNID_USUARIO; q = r + 8; }
    else if (pref(r, "/modelos")) { u = UNID_MODELOS; q = r + 8; }
    else if (pref(r, "/usb/"))                                         /* SOMA_363_ACCESO: los medios externos */
        for (int k = UNID_EXT0; k < UNID_N && u < 0; ++k) {
            if (!g_on[k] || !g_ext[k][0]) continue;
            uint32_t n = 0; while (g_ext[k][n]) n++;
            const char *a = r + 1, *b = g_ext[k]; uint32_t i = 0; while (i < n && a[i] == b[i]) i++;
            if (i == n && (a[n] == 0 || a[n] == '/')) { u = k; q = a + n; }
        }
    if (u < 0 || (*q && *q != '/')) return -1;
    if (resto) *resto = *q ? q : raiz;
    return u;
}
/* nada de "." ni ".." dentro (la unidad no puede salirse de si misma ni confundir rutas) */
static int ruta_ok(const char *r)
{
    const char *p = r;
    while (*p) {
        while (*p == '/') p++;
        const char *s = p; while (*p && *p != '/') p++;
        uint32_t n = (uint32_t)(p - s);
        if ((n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.')) return 0;
    }
    return (uint32_t)(p - r) < EXFAT_PATH_MAX;
}
static int prep(const char *ruta, const char **resto)
{
    int u = unid_ruta(ruta, resto);
    if (u < 0) return EXFAT_E_NOENT;
    if (!g_on[u]) return EXFAT_E_NOENT;
    if (!ruta_ok(*resto)) return EXFAT_E_NAME;
    return u;
}
static int escribible(int u, int priv) { return u == UNID_USUARIO || u >= UNID_EXT0 || priv; }   /* SOMA_363: y los medios externos */

/* ------------------------------------------------------------------ operaciones */
int unid_tam(const char *ruta, uint64_t *tam, int *es_carpeta)
{
    const char *r; lk(); int u = prep(ruta, &r);
    if (u < 0) { ulk(); return u; }
    struct exfat_dirent d; int e = exfat_stat(&g_vol[u], r, &d);
    if (!e) { if (tam) *tam = d.size; if (es_carpeta) *es_carpeta = d.is_dir; }
    ulk(); return e;
}
int unid_leer(const char *ruta, uint64_t off, void *buf, uint32_t n)
{
    const char *r; lk(); int u = prep(ruta, &r);
    if (u < 0) { ulk(); return u; }
    uint32_t hecho = 0; int e = 0;
    while (hecho < n) {                                   /* exfat_read lee hasta un cluster cada vez */
        e = exfat_read(&g_vol[u], r, off + hecho, (uint8_t *)buf + hecho, n - hecho);
        if (e <= 0) break;
        hecho += (uint32_t)e;
    }
    ulk();
    return e < 0 && !hecho ? e : (int)hecho;
}
int unid_escribir(const char *ruta, const void *datos, uint64_t n, int priv)
{
    const char *r; lk(); int u = prep(ruta, &r);
    int e = u < 0 ? u : !escribible(u, priv) ? EXFAT_E_RO : exfat_write(&g_vol[u], r, datos, n);
    ulk(); return e;
}
int unid_anadir(const char *ruta, const void *datos, uint32_t n, int priv)
{
    const char *r; lk(); int u = prep(ruta, &r);
    int e = u < 0 ? u : !escribible(u, priv) ? EXFAT_E_RO : exfat_append(&g_vol[u], r, datos, n);
    ulk(); return e;
}
int unid_borrar(const char *ruta, int priv)
{
    const char *r; lk(); int u = prep(ruta, &r);
    int e = u < 0 ? u : !escribible(u, priv) ? EXFAT_E_RO : exfat_remove(&g_vol[u], r);
    ulk(); return e;
}
int unid_mkdir(const char *ruta, int priv)
{
    const char *r; lk(); int u = prep(ruta, &r);
    int e = u < 0 ? u : !escribible(u, priv) ? EXFAT_E_RO : exfat_mkdir(&g_vol[u], r);
    ulk(); return e;
}
int unid_renombrar(const char *de, const char *a, int priv)
{
    const char *r1, *r2; lk();
    int u = prep(de, &r1), u2 = prep(a, &r2), e;
    if (u < 0) e = u; else if (u2 < 0) e = u2; else if (u != u2) e = EXFAT_E_ARG;
    else e = !escribible(u, priv) ? EXFAT_E_RO : exfat_rename(&g_vol[u], r1, r2);
    ulk(); return e;
}
/* SOMA_268_ACCESO: borrar una carpeta con todo lo que contiene (el gestor de archivos). De a una entrada por vuelta y con poca pila:
 * la profundidad maxima es 8 y cada nivel gasta ~1,6 KB de pila (hilos de 60 KiB). */
static int borra_rec(const char *ruta, int priv, int prof)
{
    uint64_t t = 0; int dir = 0, e = unid_tam(ruta, &t, &dir);
    if (e) return e;
    if (!dir) return unid_borrar(ruta, priv);
    if (prof >= 8) return EXFAT_E_ARG;
    for (uint32_t vuelta = 0; vuelta < 1000000u; ++vuelta) {
        struct exfat_dirent d; int total = 0, n = unid_listar(ruta, &d, 1, &total);
        if (n < 0) return n;
        if (n == 0) break;
        char hijo[800]; uint32_t p = 0, l = 0, nl = 0;
        while (ruta[l]) l++;
        while (d.name[nl]) nl++;
        if (l + 1 + nl + 1 > sizeof hijo) return EXFAT_E_NAME;
        for (uint32_t k = 0; k < l; ++k) hijo[p++] = ruta[k];
        if (p && hijo[p - 1] != '/') hijo[p++] = '/';
        for (const char *s = d.name; *s && p + 1 < sizeof hijo; ++s) hijo[p++] = *s;
        hijo[p] = 0;
        e = borra_rec(hijo, priv, prof + 1);
        if (e) return e;
    }
    return unid_borrar(ruta, priv);
}
int unid_borrar_rec(const char *ruta, int priv)
{
    const char *r;
    if (unid_ruta(ruta, &r) < 0) return EXFAT_E_ARG;
    if (!r[0] || (r[0] == '/' && !r[1])) return EXFAT_E_ARG;               /* la raiz de la unidad no se borra */
    return borra_rec(ruta, priv, 0);
}
int unid_listar(const char *ruta, struct exfat_dirent *out, int max, int *total)
{
    const char *r; lk(); int u = prep(ruta, &r);
    int e = u < 0 ? u : exfat_readdir(&g_vol[u], r, out, max, total);
    ulk(); return e;
}
int unid_abrir(const char *ruta, struct unid_fd *fd)
{
    const char *r; lk(); int u = prep(ruta, &r);
    int e = u < 0 ? u : exfat_open(&g_vol[u], r, &fd->f);
    if (!e) { fd->u = u; fd->gen = g_gen[u]; }
    ulk(); return e;
}
int unid_pread(const struct unid_fd *fd, uint64_t off, void *buf, uint32_t n)
{
    if (!fd || fd->u < 0 || fd->u >= UNID_N) return EXFAT_E_ARG;
    lk();
    int e;
    if (!g_on[fd->u] || g_gen[fd->u] != fd->gen) e = EXFAT_E_NOENT;            /* la unidad se desmonto o cambio */
    else {
        uint32_t hecho = 0; e = 0;
        while (hecho < n) { e = exfat_pread(&g_vol[fd->u], &fd->f, off + hecho, (uint8_t *)buf + hecho, n - hecho); if (e <= 0) break; hecho += (uint32_t)e; }
        if (hecho) e = (int)hecho;
    }
    ulk(); return e;
}
int64_t unid_fd_tam(const struct unid_fd *fd) { return fd ? (int64_t)fd->f.size : EXFAT_E_ARG; }

/* ------------------------------------------------------------------ primer arranque */
static int lista_tiene(const char *const *l, const char *n)
{
    for (int i = 0; l[i]; i++) {
        const char *a = l[i], *b = n;
        while (*a && *b) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) break; a++; b++; }
        if (!*a && !*b) return 1;
    }
    return 0;
}
int unid_virgen(struct exfat_vol *v, int u)
{
    static struct exfat_dirent d[16], h[2];
    int total = 0, n = exfat_readdir(v, "/", d, 16, &total);
    if (n < 0 || total > 16) return 0;
    const char *const *l = u == UNID_USUARIO ? UNID_CARPETAS_USUARIO : UNID_CARPETAS_MODELOS;
    for (int i = 0; i < n; i++) {
        if (!d[i].is_dir || !lista_tiene(l, d[i].name)) return 0;
        char p[EXFAT_NAME_MAX + 2]; p[0] = '/'; uint32_t k = 0; while (d[i].name[k] && k + 2 < sizeof p) { p[k + 1] = d[i].name[k]; k++; } p[k + 1] = 0;
        int t = 0; if (exfat_readdir(v, p, h, 2, &t) < 0 || t != 0) return 0;
    }
    return 1;
}

struct region { const struct unid_disco *d; uint64_t lba, n; };
static int reg_rd(void *c, uint64_t lba, uint32_t n, void *b) { struct region *r = c; if (lba >= r->n || n > r->n - lba) return -1; return r->d->read(r->d->ctx, r->lba + lba, n, b); }
static int reg_wr(void *c, uint64_t lba, uint32_t n, const void *b) { struct region *r = c; if (lba >= r->n || n > r->n - lba) return -1; return r->d->write(r->d->ctx, r->lba + lba, n, b); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* monta (solo lectura) el exFAT que haya en [lba, lba+n) y dice su serie y si esta vacio */
static int mira(const struct unid_disco *d, uint64_t lba, uint64_t n, int u, uint32_t *serie, uint64_t *vol_sect, int *virgen)
{
    static struct exfat_vol v; struct region r = { d, lba, n };
    struct exfat_io io = { reg_rd, 0, &r, d->alloc, d->free, d->now };
    if (exfat_mount(&v, &io, 0, n) != 0) return -1;
    *serie = v.serial; *vol_sect = v.sectors; *virgen = unid_virgen(&v, u);
    exfat_umount(&v);
    return 0;
}
static int formatea(const struct unid_disco *d, const struct unid_part *p, int u)
{
    static struct exfat_vol v; struct region r = { d, p->lba, p->n };
    struct exfat_io io = { reg_rd, reg_wr, &r, d->alloc, d->free, d->now };
    int e = exfat_format(&io, 0, p->n, u == UNID_USUARIO ? "USUARIO" : "MODELOS", u == UNID_USUARIO ? UNID_SERIE_AGR_USU : UNID_SERIE_AGR_MOD);
    if (e) return e;
    if ((e = exfat_mount(&v, &io, 0, p->n)) != 0) return e;
    const char *const *l = u == UNID_USUARIO ? UNID_CARPETAS_USUARIO : UNID_CARPETAS_MODELOS;
    for (int i = 0; l[i] && !e; i++) { char q[32]; q[0] = '/'; uint32_t k = 0; while (l[i][k] && k + 2 < sizeof q) { q[k + 1] = l[i][k]; k++; } q[k + 1] = 0; e = exfat_mkdir(&v, q); }
    exfat_umount(&v);
    return e;
}

int unid_agrandar(const struct unid_disco *d, const char **why, uint32_t *mod_mib, uint32_t *usu_mib)
{
    static const char *dummy; if (!why) why = &dummy;
    static uint8_t m[512];
    if (d->read(d->ctx, 0, 1, m) != 0) { *why = "no se pudo leer el MBR"; return -1; }
    if (m[510] != 0x55 || m[511] != 0xAA) { *why = "sin MBR"; return 0; }
    struct unid_part p[4];
    for (int i = 0; i < 4; i++) { const uint8_t *e = m + 0x1BE + 16 * i; p[i].tipo = e[4]; p[i].lba = le32(e + 8); p[i].n = le32(e + 12); }
    if (p[0].tipo != UNID_TIPO_FAT32 || p[1].tipo != UNID_TIPO_DATOS || p[2].tipo != UNID_TIPO_EXFAT || p[3].tipo != UNID_TIPO_EXFAT) { *why = "no es el pendrive de cuatro particiones"; return 0; }
    if (p[2].lba != p[1].lba + p[1].n || p[3].lba != p[2].lba + p[2].n || (uint64_t)p[3].lba + p[3].n > d->sectores) { *why = "particiones fuera de su sitio"; return 0; }
    struct unid_part q[4];
    if (unid_plan(d->sectores, p[1].n, q) != 0 || q[2].lba != p[2].lba) { *why = "el plan no cuadra con el pendrive"; return 0; }
    uint64_t fin_ahora = (uint64_t)p[3].lba + p[3].n, fin_plan = (uint64_t)q[3].lba + q[3].n;
    if (fin_plan <= fin_ahora || fin_plan - fin_ahora < AGRANDAR_MIN) { *why = "ya ocupan el pendrive"; return 0; }
    uint32_t s3, s4; uint64_t v3, v4; int g3, g4;
    int ok3 = mira(d, p[2].lba, (uint64_t)q[3].lba + q[3].n - p[2].lba, UNID_MODELOS, &s3, &v3, &g3) == 0;
    int ok4n = 0; uint32_t s4n = 0; uint64_t v4n = 0; int g4n = 0;
    int ok4 = mira(d, p[3].lba, p[3].n, UNID_USUARIO, &s4, &v4, &g4) == 0;
    if (q[3].lba != p[3].lba) ok4n = mira(d, q[3].lba, q[3].n, UNID_USUARIO, &s4n, &v4n, &g4n) == 0;
    else { ok4n = ok4; s4n = s4; v4n = v4; g4n = g4; }
    /* a) recien grabada la imagen: las dos de la imagen y vacias */
    int fresca = ok3 && s3 == UNID_SERIE_IMG_MOD && g3 && v3 == p[2].n && ok4 && s4 == UNID_SERIE_IMG_USU && g4 && v4 == p[3].n;
    /* b) un agrandado cortado antes de escribir el MBR: USUARIO nueva ya hecha en su sitio nuevo */
    int cortado = ok4n && s4n == UNID_SERIE_AGR_USU && v4n == q[3].n && g4n;
    if (!fresca && !cortado) { *why = ok3 && ok4 && (s3 != UNID_SERIE_IMG_MOD || s4 != UNID_SERIE_IMG_USU) ? "no son las de la imagen" : "MODELOS o USUARIO ya tienen archivos"; return 0; }
    if (cortado && ok3 && !g3) { *why = "MODELOS tiene archivos"; return 0; }
    /* las dos estan vacias (o a medio formatear por un corte): se formatean otra vez enteras. Primero USUARIO, en su sitio nuevo
     * (no pisa la MODELOS vieja); despues MODELOS; el MBR, lo ultimo. Un corte en cualquier punto deja el MBR viejo y el siguiente
     * arranque lo repite (caso b). */
    int e;
    if ((e = formatea(d, &q[3], UNID_USUARIO)) != 0) { *why = "error al preparar USUARIO"; return e; }
    if ((e = formatea(d, &q[2], UNID_MODELOS)) != 0) { *why = "error al preparar MODELOS"; return e; }
    if (d->sync && d->sync(d->ctx) != 0) { *why = "error al vaciar la cache"; return -1; }
    for (int i = 2; i < 4; i++) { uint8_t *en = m + 0x1BE + 16 * i; put32(en + 8, q[i].lba); put32(en + 12, q[i].n); en[5] = 0xFE; en[6] = 0xFF; en[7] = 0xFF; }
    if (d->write(d->ctx, 0, 1, m) != 0 || (d->sync && d->sync(d->ctx) != 0)) { *why = "error al escribir el MBR"; return -1; }
    static uint8_t c[512];
    if (d->read(d->ctx, 0, 1, c) != 0) { *why = "no se pudo releer el MBR"; return -1; }
    for (int i = 0; i < 512; i++) if (c[i] != m[i]) { *why = "el MBR no se leyo como se escribio"; return -1; }
    if (mod_mib) *mod_mib = q[2].n / MIB;
    if (usu_mib) *usu_mib = q[3].n / MIB;
    *why = cortado ? "terminado un agrandado interrumpido" : "";
    return 1;
}
