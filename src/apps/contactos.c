/* SOMA_357_CONTACTOS: ver contactos.h */
#include <stdint.h>
#include <stddef.h>
#include "contactos.h"
#include "agenda.h"
#include "fs.h"
#include "mem.h"
#include "rtc.h"
#include "idt.h"
#include "thread.h"
#include "bigfs.h"
#include "bigfs_glue.h"
#include "micrec.h"
#include "web_img.h"
#include "jpeg_enc.h"
#include "web_net.h"
#include "net.h"
#include "netif.h"
#if defined(__x86_64__) && !defined(WEB_HOST_TEST)
#include "sandbox.h"
#endif

#define CT_FICHERO  "/contactos/contactos.vcf"
#define CT_DIR_B    "/contactos"
#define CT_DIR_F    "/contactos/fotos"
#define CT_DIR_V    "/contactos/voz"
#define CT_FOTO_LADO 640
#define CT_FOTO_MAX  (8u << 20)          /* una foto de entrada (antes de reducirla) */
#define CT_VOZ_MS    30000u
#define CT_CUMPLE_MARCA "contacto:"
#define CT_AG_LIBRES 100                  /* huecos del Calendario que los cumpleanos nunca ocupan */

static struct ct_libreta *g_l;
static int g_cargado;
static char g_err_carga[120];
static uint32_t g_ver = 1;

/* ---------------------------------------------------------------- cadenas */
static uint32_t sl(const char *s) { uint32_t n = 0; while (s[n]) ++n; return n; }
static void put(char *o, uint32_t cap, uint32_t *p, const char *s) { while (*s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void putn(char *o, uint32_t cap, uint32_t *p, int64_t v) { char b[24]; int n = 0; uint64_t u = v < 0 ? (uint64_t)(-v) : (uint64_t)v; if (v < 0) put(o, cap, p, "-"); do { b[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 22); while (n) { char c[2] = { b[--n], 0 }; put(o, cap, p, c); } }
static void cpy(char *d, uint32_t cap, const char *s) { uint32_t p = 0; if (cap) d[0] = 0; put(d, cap, &p, s); }
static char baja(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int emp(const char *s, const char *pre) { while (*pre) { if (baja(*s) != *pre) return 0; ++s; ++pre; } return 1; }
static const char *sin_blancos(const char *s) { while (*s == ' ') ++s; return s; }
static const char *palabra(const char *s, const char *w) { s = sin_blancos(s); uint32_t n = sl(w); if (!emp(s, w)) return 0; if (s[n] && s[n] != ' ') return 0; return sin_blancos(s + n); }
static const char *una_de(const char *s, const char *const *W) { for (int i = 0; W[i]; ++i) { const char *r = palabra(s, W[i]); if (r) return r; } return 0; }
static int es_num(const char *s, uint32_t *v) { uint32_t x = 0; int n = 0; s = sin_blancos(s); while (*s >= '0' && *s <= '9' && n < 9) { x = x * 10 + (uint32_t)(*s - '0'); ++s; ++n; } s = sin_blancos(s); if (!n || *s) return 0; *v = x; return 1; }
static int contiene_pl(const char *pl, const char *w) { uint32_t n = sl(w); for (const char *s = pl; *s; ++s) { uint32_t k = 0; while (k < n && s[k] == w[k]) ++k; if (k == n) return 1; } return 0; }
static int termina(const char *s, const char *suf) { uint32_t a = sl(s), b = sl(suf); if (b > a) return 0; for (uint32_t i = 0; i < b; ++i) if (baja(s[a - b + i]) != suf[i]) return 0; return 1; }

/* ---------------------------------------------------------------- fecha */
void contactos_rev(char out[CT_REV])
{
    struct rtc_time t; rtc_get_datetime(&t); uint32_t p = 0; out[0] = 0;
    char b[24]; uint32_t q;
    int v[6] = { t.year, t.month, t.day, t.hour, t.min, t.sec }, w[6] = { 4, 2, 2, 2, 2, 2 };
    for (int i = 0; i < 6; ++i) {
        q = 0; b[0] = 0; putn(b, sizeof b, &q, v[i]);
        for (int k = (int)q; k < w[i]; ++k) put(out, CT_REV, &p, "0");
        put(out, CT_REV, &p, b); if (i == 2) put(out, CT_REV, &p, "T");
    }
    put(out, CT_REV, &p, "Z");
}
void contactos_hoy(int *y, int *m, int *d) { int h, mi; ag_desde_min(agenda_ahora(), y, m, d, &h, &mi); }

/* ---------------------------------------------------------------- leer un .vcf (aislado en x86_64) */
/* fichas en *fichas (kmalloc, max CT_MAX) y fotos en *fotos (kmalloc o 0). Devuelve fichas, -1 no es vCard, -2 sin memoria o fallo del servicio */
static int lee_vcf(const char *txt, uint32_t len, int con_fotos, struct ct_contacto **fichas, uint8_t **fotos, uint32_t *fotos_n)
{
    *fichas = 0; *fotos = 0; *fotos_n = 0;
    uint32_t fcap = con_fotos ? (24u << 20) : 0, fmax = con_fotos ? CT_FOTO_MAX : 0;
#if defined(__x86_64__) && !defined(WEB_HOST_TEST)
    if (len > SBX_IN_MAX) return -2;
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    if (!c) return -2;
    sbx_prepare(c, SBX_MIME_VCARD, txt, len, 0);
    c->in.arg[0] = CT_MAX; c->in.arg[1] = (int32_t)fcap; c->in.arg[2] = (int32_t)fmax;
    int r = sbx_svc_call(SBX_SVC_MIME, c, 30000), n = -2;
    if (r == 0 && c->out.status == 0) {
        int k = c->out.val[0]; uint32_t fn = (uint32_t)c->out.val[1]; uint64_t dl = c->out.data_len;
        if (k < 0) n = -1;
        else if (k == 0) n = 0;
        else if (k <= CT_MAX && fn <= fcap && dl == (uint64_t)k * sizeof(struct ct_contacto) + fn) {
            uint8_t *tmp = (uint8_t *)kmalloc((size_t)dl);
            if (tmp && sbx_copy_data(c, tmp, dl) == 0) {
                *fichas = (struct ct_contacto *)tmp; n = k;
                if (fn) { *fotos = (uint8_t *)kmalloc(fn); if (*fotos) { memcpy(*fotos, tmp + (uint64_t)k * sizeof(struct ct_contacto), fn); *fotos_n = fn; } }
            } else if (tmp) kfree(tmp);
        }
    }
    sbx_release(c); kfree(c);
#else
    struct ct_contacto *f = (struct ct_contacto *)kmalloc(CT_MAX * sizeof *f);
    uint8_t *fo = fcap ? (uint8_t *)kmalloc(fcap) : 0;
    if (!f || (fcap && !fo)) { if (f) kfree(f); if (fo) kfree(fo); return -2; }
    memset(f, 0, CT_MAX * sizeof *f);
    uint32_t fn = 0; int n = ct_vcard_lee(txt, len, f, CT_MAX, fo, fcap, fmax, &fn);
    if (n <= 0) { kfree(f); if (fo) kfree(fo); return n < 0 ? -1 : 0; }
    *fichas = f; *fotos = fo; *fotos_n = fn;
#endif
    /* lo que vuelve del servicio no se cree: se sanea cada ficha y cada foto tiene que caer dentro */
    for (int i = 0; i < n; ++i) {
        struct ct_contacto *x = &(*fichas)[i];
        ct_sanea(x); x->activo = 1;
        if (x->foto_len && (!*fotos || x->foto_off > *fotos_n || x->foto_len > *fotos_n - x->foto_off)) { x->foto_off = 0; x->foto_len = 0; }
    }
    return n;
}

/* ---------------------------------------------------------------- la libreta */
uint32_t contactos_version(void) { return g_ver; }
const char *contactos_error_carga(void) { return g_err_carga; }
struct ct_libreta *contactos_libreta(void)
{
    if (g_cargado) return g_l;
    g_cargado = 1;
    g_l = (struct ct_libreta *)kmalloc(sizeof *g_l);
    if (!g_l) { cpy(g_err_carga, sizeof g_err_carga, "Sin memoria para la libreta de contactos."); g_cargado = 0; return 0; }
    ct_vacia(g_l);
    uint32_t n = 0; char *b = vfs_read_alloc(CT_FICHERO, &n);
    if (!b) return g_l;
    if (n) {
        struct ct_contacto *f; uint8_t *fo; uint32_t fon;
        int k = lee_vcf(b, n, 0, &f, &fo, &fon);
        if (k < 0) cpy(g_err_carga, sizeof g_err_carga, "No se pudieron leer los contactos guardados: no se cambia nada hasta que se puedan leer.");
        for (int i = 0; i < k; ++i) {
            struct ct_contacto *s = &f[i], *d = 0;
            if (s->id && !ct_busca_id(g_l, s->id)) for (int j = 0; j < CT_MAX; ++j) if (!g_l->c[j].activo) { d = &g_l->c[j]; break; }
            if (d) { *d = *s; d->activo = 1; d->foto_off = d->foto_len = 0; if (d->id >= g_l->sig_id) g_l->sig_id = d->id + 1; }
            else { d = ct_nuevo(g_l); if (!d) break; uint32_t id = d->id; *d = *s; d->id = id; d->activo = 1; d->nfotos = 0; d->voz = 0; }
        }
        if (f) kfree(f);
        if (fo) kfree(fo);
    }
    kfree(b);
    return g_l;
}

/* los cumpleanos en el Calendario: un evento anual de todo el dia por contacto con fecha (nota = "contacto:ID") */
static uint32_t marca_id(const char *nota) { if (!emp(nota, CT_CUMPLE_MARCA)) return 0; uint32_t v = 0; const char *s = nota + sizeof CT_CUMPLE_MARCA - 1; while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0'); return v; }
static void titulo_cumple(const struct ct_contacto *c, char *t, uint32_t cap)
{
    char nc[CT_NOM + CT_APE + 4]; ct_nombre_completo(c, nc, sizeof nc);
    uint32_t p = 0; t[0] = 0; put(t, cap, &p, "Cumplea\303\261os de "); put(t, cap, &p, nc);
    while (p && ((uint8_t)t[p - 1] & 0xC0) == 0x80 && p + 1 >= cap) --p;
}
static int g_cumples_fuera;
static void sync_cumples(void)
{
    struct ag_cal *ag = agenda_cal(); if (!g_l || !ag) return;
    int cambia = 0; g_cumples_fuera = 0;
    static uint8_t visto[CT_MAX];
    memset(visto, 0, sizeof visto);
    for (int i = 0; i < AG_MAX_EV; ++i) {
        struct ag_ev *e = &ag->ev[i]; if (!e->activo) continue;
        uint32_t id = marca_id(e->nota); if (!id) continue;
        struct ct_contacto *c = ct_busca_id(g_l, id); int ci = c ? (int)(c - g_l->c) : -1;
        if (!c || !c->nac_m || visto[ci]) { ag_borra(ag, e->id); cambia = 1; continue; }
        visto[ci] = 1;
        char t[AG_TIT]; titulo_cumple(c, t, sizeof t);
        int y0 = c->nac_y ? c->nac_y : 2000;
        int64_t ini = ag_min(y0, c->nac_m, c->nac_d, 0, 0);
        int igual = e->ini == ini && e->rep == AG_REP_ANO && e->todo_dia; for (uint32_t k = 0; igual && (t[k] || e->titulo[k]); ++k) if (t[k] != e->titulo[k]) igual = 0;
        if (!igual) { cpy(e->titulo, AG_TIT, t); e->ini = ini; e->rep = AG_REP_ANO; e->cada = 1; e->todo_dia = 1; e->dur = 1440; cambia = 1; }
    }
    for (int i = 0; i < CT_MAX; ++i) {
        struct ct_contacto *c = &g_l->c[i]; if (!c->activo || !c->nac_m || visto[i]) continue;
        if (ag_numero(ag) + CT_AG_LIBRES >= AG_MAX_EV) { g_cumples_fuera++; continue; }
        struct ag_ev *e = ag_nuevo(ag); if (!e) { g_cumples_fuera++; continue; }
        uint32_t id = e->id; memset(e, 0, sizeof *e); e->id = id; e->activo = 1;
        titulo_cumple(c, e->titulo, AG_TIT);
        e->ini = ag_min(c->nac_y ? c->nac_y : 2000, c->nac_m, c->nac_d, 0, 0); e->dur = 1440; e->todo_dia = 1;
        e->rep = AG_REP_ANO; e->cada = 1; e->cat = AG_CAT_SOCIAL; e->antes = -1;
        uint32_t p = 0; put(e->nota, AG_NOTA, &p, CT_CUMPLE_MARCA); putn(e->nota, AG_NOTA, &p, c->id);
        cambia = 1;
    }
    if (cambia) (void)agenda_guarda();
}
int contactos_guarda(void)
{
    if (!g_l) return -1;
    if (g_err_carga[0]) return -1;                   /* no pisar un archivo que no se ha podido leer */
    uint32_t cap = ct_numero(g_l) * 1200u + 16384u, p = 0;
    char *b = (char *)kmalloc(cap); if (!b) return -1;
    for (int i = 0; i < CT_MAX; ++i) {
        const struct ct_contacto *c = &g_l->c[i]; if (!c->activo) continue;
        for (;;) {
            uint32_t w = cap - p > 1 ? ct_vcard_uno(c, 1, 0, 0, b + p, cap - p) : 0;
            if (w) { p += w; break; }
            uint32_t nc = cap * 2; char *nb = (char *)kmalloc(nc);
            if (!nb || nc > (16u << 20)) { if (nb) kfree(nb); kfree(b); return -1; }
            memcpy(nb, b, p); kfree(b); b = nb; cap = nc;
        }
    }
    int r = vfs_write(CT_FICHERO, b, p);
    kfree(b); ++g_ver;
    sync_cumples();
    return r < 0 ? -1 : 0;
}
int contactos_toca(uint32_t id)
{
    struct ct_libreta *l = contactos_libreta(); struct ct_contacto *c = l ? ct_busca_id(l, id) : 0;
    if (!c) return -1;
    char rev[CT_REV]; contactos_rev(rev); ct_marca(c, rev);
    if (!c->uid[0]) { uint32_t p = 0; put(c->uid, CT_UID, &p, "soma-"); putn(c->uid, CT_UID, &p, c->id); put(c->uid, CT_UID, &p, "-"); put(c->uid, CT_UID, &p, rev); }
    return contactos_guarda();
}
int contactos_anade_texto(const char *txt, char *res, uint32_t cap)
{
    struct ct_libreta *l = contactos_libreta(); if (!l) return -3;
    if (g_err_carga[0]) { if (res) cpy(res, cap, g_err_carga); return -3; }
    struct ct_contacto n; if (ct_parsea_texto(txt, &n)) return -1;
    /* si ya estaba (mismo nombre completo o mismo email) se completa su ficha en vez de duplicarla; lo dicho ahora manda */
    struct ct_contacto *c = 0; struct ct_contacto copia; int r = ct_fusiona_ex(l, &n, &c, 1);
    if (r < 0 || !c) return -2;
    if (r != 1) copia = *c;
    uint32_t id = c->id;
    if (r != 0 && contactos_toca(id)) { if (r == 1) ct_borra(l, id); else *c = copia; return -3; }
    if (res) {
        char f[1200]; int y, m, d; contactos_hoy(&y, &m, &d);
        uint32_t p = 0; res[0] = 0; put(res, cap, &p, r == 1 ? "Contacto guardado:\n" : r == 2 ? "Ya lo ten\303\255as: he completado su ficha:\n" : "Ya lo ten\303\255as, con esos mismos datos:\n");
        ct_ficha_txt(c, y, m, d, f, sizeof f); put(res, cap, &p, f);
    }
    return (int)id;
}

/* ---------------------------------------------------------------- BIGFS: fotos y voz */
static struct bigfs *bfs(void) { int e = 0; return bigfs_sys(&e); }
static void dirs(struct bigfs *fs) { (void)bigfs_mkdir(fs, CT_DIR_B); (void)bigfs_mkdir(fs, CT_DIR_F); (void)bigfs_mkdir(fs, CT_DIR_V); }
static void ruta_foto(uint32_t id, int k, char *o, uint32_t cap) { uint32_t p = 0; o[0] = 0; put(o, cap, &p, CT_DIR_F "/"); putn(o, cap, &p, id); put(o, cap, &p, "_"); putn(o, cap, &p, k); put(o, cap, &p, ".jpg"); }
static void ruta_voz(uint32_t id, char *o, uint32_t cap) { uint32_t p = 0; o[0] = 0; put(o, cap, &p, CT_DIR_V "/"); putn(o, cap, &p, id); put(o, cap, &p, ".wav"); }
static uint8_t *big_lee(const char *ruta, uint32_t *n, uint32_t max)
{
    struct bigfs *fs = bfs(); uint32_t ino; struct bigfs_stat st; *n = 0;
    if (!fs || bigfs_lookup(fs, ruta, &ino) != (int)BIGFS_T_FILE || bigfs_stat(fs, ino, &st) < 0 || !st.size || st.size > max) return 0;
    uint8_t *b = (uint8_t *)kmalloc((size_t)st.size + 1u); if (!b) return 0;
    if (bigfs_read(fs, ino, 0, b, st.size) != (int64_t)st.size) { kfree(b); return 0; }
    b[st.size] = 0; *n = (uint32_t)st.size; return b;
}
static int big_escribe(const char *ruta, const uint8_t *d, uint32_t n)
{
    struct bigfs *fs = bfs(); if (!fs) return -19;
    dirs(fs); (void)bigfs_unlink(fs, ruta);
    uint32_t ino; int r = bigfs_create(fs, ruta, &ino); if (r < 0) return r;
    int64_t w = bigfs_write(fs, ino, 0, d, n);
    if (w != (int64_t)n) { bigfs_unlink(fs, ruta); return w < 0 ? (int)w : -5; }
    return 0;
}
static int big_renombra(const char *de, const char *a) { struct bigfs *fs = bfs(); if (!fs) return -19; (void)bigfs_unlink(fs, a); return bigfs_rename(fs, de, a); }
static void big_borra(const char *ruta) { struct bigfs *fs = bfs(); if (fs) (void)bigfs_unlink(fs, ruta); }
/* un archivo de /usuario, de DATOS o del almacen BIGFS (las fotos de la camara, las descargas) */
static uint8_t *lee_archivo(const char *ruta, uint32_t *n, uint32_t max)
{
    *n = 0; int t = vfs_size(ruta);
    if (t >= 0) { if ((uint32_t)t > max) return 0; uint32_t k = 0; char *b = vfs_read_alloc(ruta, &k); if (b) { *n = k; return (uint8_t *)b; } }
    return big_lee(ruta, n, max);
}

/* decodifica aislado (imgdec, ring 3), reduce a 640 px, compone sobre blanco y vuelve a codificar en JPEG: lo que se guarda lo ha escrito SOMA */
static int normaliza(const uint8_t *d, uint32_t n, uint8_t **out, uint32_t *on)
{
    *out = 0; *on = 0;
    if (!d || n < 8 || n > CT_FOTO_MAX) return -1;
    struct wimg im; memset(&im, 0, sizeof im);
    if (wi_decode_fit(d, n, &im, CT_FOTO_LADO, CT_FOTO_LADO) != 0 || !im.px || im.w <= 0 || im.h <= 0 || im.w > 16384 || im.h > 16384) { wi_free(&im); return -1; }
    int w = im.w, h = im.h, tw = w, th = h;
    if (tw > CT_FOTO_LADO || th > CT_FOTO_LADO) { if (w >= h) { tw = CT_FOTO_LADO; th = (int)((int64_t)h * CT_FOTO_LADO / w); } else { th = CT_FOTO_LADO; tw = (int)((int64_t)w * CT_FOTO_LADO / h); } }
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    uint32_t *px = (uint32_t *)kmalloc((size_t)tw * (size_t)th * 4u);
    uint32_t cap = (uint32_t)tw * (uint32_t)th * 2u + 8192u; uint8_t *j = (uint8_t *)kmalloc(cap);
    if (!px || !j) { if (px) kfree(px); if (j) kfree(j); wi_free(&im); return -2; }
    for (int y = 0; y < th; ++y) {                         /* promedio de la caja de origen de cada pixel */
        int y0 = (int)((int64_t)y * h / th), y1 = (int)((int64_t)(y + 1) * h / th); if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < tw; ++x) {
            int x0 = (int)((int64_t)x * w / tw), x1 = (int)((int64_t)(x + 1) * w / tw); if (x1 <= x0) x1 = x0 + 1;
            uint32_t r = 0, g = 0, b = 0, k = 0;
            for (int yy = y0; yy < y1; yy += (y1 - y0 > 4 ? (y1 - y0) / 4 : 1)) for (int xx = x0; xx < x1; xx += (x1 - x0 > 4 ? (x1 - x0) / 4 : 1)) {
                uint32_t c = im.px[(size_t)yy * (size_t)w + (size_t)xx], a = c >> 24;
                r += (((c >> 16) & 255) * a + 255 * (255 - a)) / 255; g += (((c >> 8) & 255) * a + 255 * (255 - a)) / 255; b += ((c & 255) * a + 255 * (255 - a)) / 255; ++k;
            }
            px[(size_t)y * (size_t)tw + (size_t)x] = ((r / k) << 16) | ((g / k) << 8) | (b / k);
        }
    }
    wi_free(&im);
    size_t jn = jpeg_encode_rgb(px, tw, th, tw, 85, j, cap);
    kfree(px);
    if (!jn) { kfree(j); return -1; }
    *out = j; *on = (uint32_t)jn; return 0;
}
int contactos_foto_lee(uint32_t id, int k, uint8_t **buf, uint32_t *len)
{
    char r[64]; ruta_foto(id, k, r, sizeof r); *buf = big_lee(r, len, CT_FOTO_MAX);
    return *buf ? 0 : -1;
}
int contactos_foto_anade_bytes(uint32_t id, const uint8_t *d, uint32_t n)
{
    struct ct_libreta *l = contactos_libreta(); struct ct_contacto *c = l ? ct_busca_id(l, id) : 0;
    if (!c) return -1;
    if (c->nfotos >= CT_FOTOS) return -4;
    if (!bfs()) return -19;
    uint8_t *j; uint32_t jn; int r = normaliza(d, n, &j, &jn); if (r) return r == -2 ? -2 : -3;
    char ru[64]; ruta_foto(id, c->nfotos + 1, ru, sizeof ru);
    r = big_escribe(ru, j, jn); kfree(j);
    if (r) return -19;
    c->nfotos++;
    return contactos_toca(id) ? -5 : 0;
}
int contactos_foto_anade(uint32_t id, const char *ruta, char *msg, uint32_t cap)
{
    uint32_t n = 0; uint8_t *d = lee_archivo(ruta, &n, CT_FOTO_MAX);
    if (!d) { cpy(msg, cap, n ? "La imagen es demasiado grande (8 MB como mucho)." : "No encuentro esa imagen."); return -1; }
    int r = contactos_foto_anade_bytes(id, d, n); kfree(d);
    cpy(msg, cap, r == 0 ? "Foto a\303\261adida." : r == -4 ? "Ya tiene 6 fotos: quita alguna antes." : r == -19 ? "No hay almac\303\251n BIGFS (format\303\251alo con: big format yes)." :
                  r == -3 ? "No es una imagen que SOMA sepa leer (JPEG, PNG, GIF, WebP, BMP)." : "No se pudo guardar la foto.");
    return r;
}
int contactos_foto_borra(uint32_t id, int k)
{
    struct ct_libreta *l = contactos_libreta(); struct ct_contacto *c = l ? ct_busca_id(l, id) : 0;
    if (!c || k < 1 || k > c->nfotos) return -1;
    char a[64], b[64]; ruta_foto(id, k, a, sizeof a); big_borra(a);
    for (int i = k + 1; i <= c->nfotos; ++i) { ruta_foto(id, i, a, sizeof a); ruta_foto(id, i - 1, b, sizeof b); (void)big_renombra(a, b); }
    c->nfotos--;
    return contactos_toca(id);
}
int contactos_foto_principal(uint32_t id, int k)
{
    struct ct_libreta *l = contactos_libreta(); struct ct_contacto *c = l ? ct_busca_id(l, id) : 0;
    if (!c || k <= 1 || k > c->nfotos) return -1;
    char a[64], b[64], t[64]; ruta_foto(id, k, a, sizeof a); ruta_foto(id, 1, b, sizeof b); ruta_foto(id, 99, t, sizeof t);
    if (big_renombra(b, t) || big_renombra(a, b) || big_renombra(t, a)) return -1;
    return contactos_toca(id);
}
static int es_imagen(const char *n) { return termina(n, ".jpg") || termina(n, ".jpeg") || termina(n, ".png") || termina(n, ".gif") || termina(n, ".webp") || termina(n, ".bmp"); }
int contactos_imagenes(char rutas[][96], int max)
{
    int n = 0; struct bigfs *fs = bfs();
    if (fs) {                                                         /* la camara guarda en /fotos: las mas nuevas primero */
        uint32_t dir; struct bigfs_stat st[64]; int k = 0;
        if (bigfs_lookup(fs, "/fotos", &dir) == (int)BIGFS_T_DIR) { uint32_t cur = 0; while (k < 64 && bigfs_readdir(fs, dir, &cur, &st[k]) == 1) if (st[k].type == BIGFS_T_FILE && es_imagen(st[k].name)) ++k; }
        for (int i = 1; i < k; ++i) { struct bigfs_stat t = st[i]; int j = i; while (j > 0 && st[j - 1].mtime < t.mtime) { st[j] = st[j - 1]; --j; } st[j] = t; }
        for (int i = 0; i < k && n < max; ++i) { uint32_t p = 0; rutas[n][0] = 0; put(rutas[n], 96, &p, "/fotos/"); put(rutas[n], 96, &p, st[i].name); ++n; }
    }
    char *b = (char *)kmalloc(8192);
    if (b) {
        static const char *const D[] = { "/usuario", "/usuario/fotos", "/usuario/Fotos", "/usuario/Im\303\241genes", 0 };
        for (int dd = 0; D[dd] && n < max; ++dd) {
            b[0] = 0; (void)vfs_format_list_ext(0, D[dd], b, 8192);
            const char *s = b; if (emp(s, "archivos: ")) s += 10;
            while (*s && n < max) {
                const char *e = s; while (*e && !(e[0] == ',' && e[1] == ' ')) ++e;
                char r[96]; uint32_t L = (uint32_t)(e - s); if (L > 95) L = 95; for (uint32_t i = 0; i < L; ++i) r[i] = s[i]; r[L] = 0;
                if (es_imagen(r) && r[0] == '/') cpy(rutas[n++], 96, r);
                s = *e ? e + 2 : e;
            }
        }
        kfree(b);
    }
    return n;
}

/* ---- voz */
static int g_voz = CTV_NADA; static uint32_t g_voz_id; static uint64_t g_voz_t0;
int contactos_voz_estado(uint32_t *id, uint32_t *ms, int *nivel)
{
    struct micrec_info in; micrec_info(&in);
    if (g_voz == CTV_ESCUCHANDO && in.mode != MR_PLAY) g_voz = CTV_NADA;
    if (id) *id = g_voz_id;
    if (ms) *ms = g_voz == CTV_ESCUCHANDO ? in.play_ms : in.ms;
    if (nivel) *nivel = in.level;
    return g_voz;
}
int contactos_voz_graba(uint32_t id, char *msg, uint32_t cap)
{
    struct ct_libreta *l = contactos_libreta(); if (!l || !ct_busca_id(l, id)) { cpy(msg, cap, "No existe ese contacto."); return -1; }
    if (!bfs()) { cpy(msg, cap, "No hay almac\303\251n BIGFS para guardar la voz (big format yes)."); return -19; }
    struct micrec_info in; micrec_info(&in);
    if (in.unsaved && g_voz == CTV_NADA) { cpy(msg, cap, "La Grabadora tiene una grabaci\303\263n sin guardar: gu\303\241rdala o desc\303\241rtala antes."); return -16; }
    if (in.mode == MR_REC || in.mode == MR_PROBE) { cpy(msg, cap, "El micr\303\263fono est\303\241 ocupado."); return -16; }
    micrec_stop();
    int r = micrec_record(16000);
    if (r) { cpy(msg, cap, r == -19 ? "No hay micr\303\263fono utilizable." : "No se pudo empezar a grabar."); return r; }
    g_voz = CTV_GRABANDO; g_voz_id = id; g_voz_t0 = timer_get_uptime_ms(); ++g_ver;
    cpy(msg, cap, "Grabando la voz (30 s como mucho)\342\200\246"); return 0;
}
void contactos_voz_para(void)
{
    if (g_voz == CTV_GRABANDO) { micrec_stop(); g_voz = CTV_GUARDANDO; ++g_ver; }
    else if (g_voz == CTV_ESCUCHANDO) { micrec_stop(); g_voz = CTV_NADA; ++g_ver; }
}
int contactos_voz_escucha(uint32_t id, char *msg, uint32_t cap)
{
    struct ct_libreta *l = contactos_libreta(); struct ct_contacto *c = l ? ct_busca_id(l, id) : 0;
    if (!c || !c->voz) { cpy(msg, cap, "No hay voz grabada."); return -1; }
    struct micrec_info in; micrec_info(&in);
    if (in.unsaved) { cpy(msg, cap, "La Grabadora tiene una grabaci\303\263n sin guardar: gu\303\241rdala antes."); return -16; }
    char r[64]; ruta_voz(id, r, sizeof r);
    if (micrec_load(r) || micrec_play()) { cpy(msg, cap, "No se pudo reproducir la voz."); return -1; }
    g_voz = CTV_ESCUCHANDO; g_voz_id = id; ++g_ver; msg[0] = 0; return 0;
}
int contactos_voz_borra(uint32_t id)
{
    struct ct_libreta *l = contactos_libreta(); struct ct_contacto *c = l ? ct_busca_id(l, id) : 0;
    if (!c) return -1;
    char r[64]; ruta_voz(id, r, sizeof r); big_borra(r);
    c->voz = 0; return contactos_toca(id);
}
static void voz_tick(void)
{
    if (g_voz == CTV_GRABANDO && timer_get_uptime_ms() - g_voz_t0 > CT_VOZ_MS) contactos_voz_para();
    if (g_voz != CTV_GUARDANDO) return;
    struct micrec_info in; micrec_info(&in);
    if (in.mode != MR_IDLE) return;
    struct bigfs *fs = bfs(); char r[64]; ruta_voz(g_voz_id, r, sizeof r);
    if (fs) { dirs(fs); (void)micrec_delete(r); }
    int ok = fs && in.ms >= 300 && micrec_save(r, 0, 0) == 0;
    micrec_clear();
    struct ct_contacto *c = g_l ? ct_busca_id(g_l, g_voz_id) : 0;
    if (ok && c) { c->voz = 1; (void)contactos_toca(g_voz_id); }
    g_voz = CTV_NADA; ++g_ver;
}

/* ---- fotos de la nube (Google exporta un enlace): un hilo las baja una a una; el bucle de la GUI las guarda */
static volatile int g_dl_estado;             /* 0 libre, 1 bajando, 2 hecho (g_dl_buf), 3 fallo */
static uint32_t g_dl_id; static char g_dl_url[CT_URL]; static uint8_t *g_dl_buf; static uint32_t g_dl_n;
static uint8_t g_dl_hecho[CT_MAX];           /* ya intentado (por hueco) */
static uint64_t g_dl_ultimo;
static void dl_hilo(void *arg)
{
    (void)arg;
    static struct wn_resp rs; struct wn_req rq;
    memset(&rq, 0, sizeof rq); memset(&rs, 0, sizeof rs);
    rq.url = g_dl_url; rq.accept = "image/*"; rq.timeout_ms = 15000; rq.max_body = CT_FOTO_MAX; rq.want_binary = 1; rq.direct = 1;
    if (wn_fetch_isolated(&rq, &rs) == 0 && rs.status == 200 && rs.body && rs.len) {
        g_dl_buf = (uint8_t *)kmalloc(rs.len);
        if (g_dl_buf) { memcpy(g_dl_buf, rs.body, rs.len); g_dl_n = rs.len; }
    }
    wn_free_isolated(&rs);
    g_dl_estado = g_dl_buf ? 2 : 3;
    thread_exit();
}
static int hay_red(void) { return netif_active() && (net_ip[0] || net_ip[1] || net_ip[2] || net_ip[3]); }
static int dl_tick(void)
{
    if (!g_l) return 0;
    if (g_dl_estado == 2 || g_dl_estado == 3) {
        int r = 0;
        if (g_dl_estado == 2) { struct ct_contacto *c = ct_busca_id(g_l, g_dl_id); if (c && !c->nfotos) r = contactos_foto_anade_bytes(g_dl_id, g_dl_buf, g_dl_n) == 0; kfree(g_dl_buf); }
        g_dl_buf = 0; g_dl_n = 0; g_dl_estado = 0; return r;
    }
    if (g_dl_estado) return 0;
    uint64_t t = timer_get_uptime_ms(); if (t - g_dl_ultimo < 2000) return 0;
    g_dl_ultimo = t;
    if (!hay_red() || !bfs()) return 0;
    for (int i = 0; i < CT_MAX; ++i) {
        struct ct_contacto *c = &g_l->c[i];
        if (!c->activo || c->nfotos || !c->foto_url[0] || g_dl_hecho[i]) continue;
        g_dl_hecho[i] = 1; g_dl_id = c->id; cpy(g_dl_url, sizeof g_dl_url, c->foto_url); g_dl_estado = 1;
        if (!thread_create("ct_foto", dl_hilo, 0)) g_dl_estado = 0;
        break;
    }
    return 0;
}
int contactos_tick(void)
{
    uint32_t v = g_ver; voz_tick(); int r = dl_tick();
    return r || v != g_ver || g_voz == CTV_GRABANDO || g_voz == CTV_ESCUCHANDO;
}
int contactos_borra(uint32_t id)
{
    struct ct_libreta *l = contactos_libreta(); struct ct_contacto *c = l ? ct_busca_id(l, id) : 0;
    if (!c) return -1;
    if (g_err_carga[0]) return -1;
    char r[64];
    for (int k = 1; k <= c->nfotos; ++k) { ruta_foto(id, k, r, sizeof r); big_borra(r); }
    if (c->voz) { ruta_voz(id, r, sizeof r); big_borra(r); }
    ct_borra(l, id);
    return contactos_guarda();
}

/* ---------------------------------------------------------------- importar / exportar */
int contactos_importa(const char *ruta, char *res, uint32_t cap)
{
    struct ct_libreta *l = contactos_libreta(); uint32_t p = 0; res[0] = 0;
    if (!l || g_err_carga[0]) { put(res, cap, &p, g_err_carga[0] ? g_err_carga : "Sin memoria."); return -3; }
    uint32_t n = 0; uint8_t *b = lee_archivo(ruta, &n, 48u << 20);
    if (!b) { put(res, cap, &p, n ? "El archivo es demasiado grande.\n" : "No encuentro ese archivo.\n"); return -1; }
    struct ct_contacto *f; uint8_t *fo; uint32_t fon;
    int k = lee_vcf((const char *)b, n, 1, &f, &fo, &fon); kfree(b);
    if (k < 0) { put(res, cap, &p, k == -1 ? "Eso no parece un archivo de contactos (.vcf).\n" : "No se pudo leer el archivo (sin memoria o el lector aislado no responde).\n"); return -2; }
    int nuevos = 0, act = 0, iguales = 0, llena = 0, fotos = 0;
    char rev[CT_REV]; contactos_rev(rev);
    for (int i = 0; i < k; ++i) {
        struct ct_contacto *d = 0; int r = ct_fusiona(l, &f[i], &d);
        if (r < 0) { ++llena; continue; }
        if (r == 1) ++nuevos; else if (r == 2) ++act; else ++iguales;
        if (d && !d->uid[0]) { uint32_t q = 0; put(d->uid, CT_UID, &q, "soma-"); putn(d->uid, CT_UID, &q, d->id); put(d->uid, CT_UID, &q, "-"); put(d->uid, CT_UID, &q, rev); }
        if (d && !d->nfotos && f[i].foto_len && fo) {
            uint8_t *j; uint32_t jn; char ru[64];
            if (normaliza(fo + f[i].foto_off, f[i].foto_len, &j, &jn) == 0) { ruta_foto(d->id, 1, ru, sizeof ru); if (big_escribe(ru, j, jn) == 0) { d->nfotos = 1; ++fotos; } kfree(j); }
        }
    }
    if (f) kfree(f);
    if (fo) kfree(fo);
    if (nuevos + act) (void)contactos_guarda();
    put(res, cap, &p, "Importados de "); put(res, cap, &p, ruta); put(res, cap, &p, ": "); putn(res, cap, &p, nuevos); put(res, cap, &p, " nuevos, ");
    putn(res, cap, &p, act); put(res, cap, &p, " actualizados, "); putn(res, cap, &p, iguales); put(res, cap, &p, " sin cambios");
    if (fotos) { put(res, cap, &p, ", "); putn(res, cap, &p, fotos); put(res, cap, &p, " fotos"); }
    if (llena) { put(res, cap, &p, ". No caben "); putn(res, cap, &p, llena); put(res, cap, &p, " (la libreta admite 1500)"); }
    put(res, cap, &p, ".\n");
    if (g_cumples_fuera) { put(res, cap, &p, "Aviso: "); putn(res, cap, &p, g_cumples_fuera); put(res, cap, &p, " cumplea\303\261os no caben en el Calendario.\n"); }
    return nuevos + act;
}
int contactos_exporta(const char *ruta, int con_fotos, char *res, uint32_t cap)
{
    struct ct_libreta *l = contactos_libreta(); uint32_t p = 0; res[0] = 0;
    if (!l) { put(res, cap, &p, "Sin memoria."); return -1; }
    uint32_t bc = ct_numero(l) * 1500u + 16384u, bp = 0; char *b = (char *)kmalloc(bc);
    if (!b) { put(res, cap, &p, "Sin memoria para exportar.\n"); return -1; }
    int n = 0, nf = 0;
    for (int i = 0; i < CT_MAX; ++i) {
        const struct ct_contacto *c = &l->c[i]; if (!c->activo) continue;
        uint8_t *fo = 0; uint32_t fl = 0;
        if (con_fotos && c->nfotos) (void)contactos_foto_lee(c->id, 1, &fo, &fl);
        for (;;) {
            uint32_t w = bc - bp > 1 ? ct_vcard_uno(c, 0, fo, fl, b + bp, bc - bp) : 0;
            if (w) { bp += w; break; }
            uint32_t nc = bc * 2 + fl * 2; char *nb = nc <= (64u << 20) ? (char *)kmalloc(nc) : 0;
            if (!nb) { if (fo) kfree(fo); kfree(b); put(res, cap, &p, "Sin memoria para exportar.\n"); return -1; }
            memcpy(nb, b, bp); kfree(b); b = nb; bc = nc;
        }
        if (fo) { kfree(fo); ++nf; }
        ++n;
    }
    int w = vfs_write(ruta, b, bp); kfree(b);
    if (w < 0) { put(res, cap, &p, "No se pudo escribir el archivo (solo puedes escribir en /usuario y /tmp).\n"); return -1; }
    for (int i = 0; i < CT_MAX; ++i) l->c[i].cambiado = 0;          /* lo exportado ya no esta "pendiente de subir" */
    (void)contactos_guarda();
    put(res, cap, &p, "Exportados "); putn(res, cap, &p, n); put(res, cap, &p, " contactos"); if (nf) { put(res, cap, &p, " ("); putn(res, cap, &p, nf); put(res, cap, &p, " con foto)"); }
    put(res, cap, &p, " a "); put(res, cap, &p, ruta); put(res, cap, &p, ". Se importan en Google Contactos (Importar), iCloud, Outlook o el m\303\263vil.\n");
    return n;
}

/* ---------------------------------------------------------------- la orden `contactos` */
static const char *AYUDA =
    "Contactos: tu libreta de direcciones. Entiende frases en castellano.\n"
    "  contactos                         resumen y pr\303\263ximos cumplea\303\261os\n"
    "  contactos <nombre> | buscar <texto>     (contactos ana, contactos buscar madrid)\n"
    "  contactos ver <n|nombre>          la ficha completa\n"
    "  contactos a\303\261adir <texto>         contactos a\303\261adir Luis P\303\251rez, email luis@x.es, m\303\263vil 600 11 22 33,\n"
    "                                    naci\303\263 el 3 de mayo de 1990, vive en Calle Mayor 3, Madrid, grupo amigos\n"
    "  contactos editar <n|nombre> <campo> <valor>    campos: nombre apellidos apodo email email2 tel movil fijo\n"
    "                                    direccion ciudad cp provincia pais grupo empresa cargo nacimiento web nota favorito\n"
    "  contactos telefono|email|direccion|cumplea\303\261os <nombre>\n"
    "  contactos borrar <n|nombre>\n"
    "  contactos grupos | grupo <nombre> | favoritos | cumplea\303\261os [dias]\n"
    "  contactos foto <n|nombre> <imagen>     (p. ej. /fotos/foto_003.jpg de la c\303\241mara)\n"
    "  contactos exportar [ruta.vcf] [sin fotos]   (por defecto " CT_VCF_DEF ")\n"
    "  contactos importar <ruta.vcf>     de Google Contactos (Exportar > vCard), iCloud, Outlook o el m\303\263vil\n"
    "  contactos abrir                   la ventana (fotos y voz de cada contacto)\n";

static int resuelve_uno(const char *txt, struct ct_contacto **c, char *out, uint32_t max, uint32_t *p)
{
    struct ct_libreta *l = contactos_libreta(); uint16_t idx[8];
    int n = l ? ct_resuelve(l, txt, idx, 8) : 0;
    if (n == 1) { *c = &l->c[idx[0]]; return 1; }
    if (!n) { put(out, max, p, "No encuentro a nadie as\303\255 en los contactos.\n"); return 0; }
    put(out, max, p, "Hay varios; di cu\303\241l (por su n\303\272mero):\n");
    for (int i = 0; i < n; ++i) { char t[300]; ct_linea_txt(&l->c[idx[i]], t, sizeof t); put(out, max, p, "  "); put(out, max, p, t); put(out, max, p, "\n"); }
    return 0;
}
static void lista_txt(const uint16_t *idx, int n, char *out, uint32_t max, uint32_t *p)
{
    struct ct_libreta *l = contactos_libreta();
    for (int i = 0; i < n; ++i) { char t[300]; ct_linea_txt(&l->c[idx[i]], t, sizeof t); put(out, max, p, "  "); put(out, max, p, t); put(out, max, p, "\n"); if (*p + 300 >= max) { put(out, max, p, "  \342\200\246\n"); break; } }
}
static void cumples_txt(int dias, char *out, uint32_t max, uint32_t *p)
{
    struct ct_libreta *l = contactos_libreta(); uint16_t idx[24]; int16_t fa[24]; int y, m, d; contactos_hoy(&y, &m, &d);
    int n = ct_cumples(l, y, m, d, dias, idx, fa, 24);
    if (!n) { put(out, max, p, "Ning\303\272n cumplea\303\261os en los pr\303\263ximos "); putn(out, max, p, dias); put(out, max, p, " d\303\255as.\n"); return; }
    for (int i = 0; i < n; ++i) {
        const struct ct_contacto *c = &l->c[idx[i]]; char nc[CT_NOM + CT_APE + 4]; ct_nombre_completo(c, nc, sizeof nc);
        put(out, max, p, "  "); put(out, max, p, fa[i] == 0 ? "HOY" : fa[i] == 1 ? "ma\303\261ana" : "");
        if (fa[i] > 1) { put(out, max, p, "en "); putn(out, max, p, fa[i]); put(out, max, p, " d\303\255as"); }
        put(out, max, p, " ("); putn(out, max, p, c->nac_d); put(out, max, p, " de "); put(out, max, p, ct_nombre_mes(c->nac_m)); put(out, max, p, "): "); put(out, max, p, nc);
        int e = ct_edad(c, y, m, d); if (e >= 0) { put(out, max, p, ", cumple "); putn(out, max, p, fa[i] == 0 ? e : e + 1); }
        put(out, max, p, "\n");
    }
}
int contactos_cmd(const char *args, char *out, uint32_t max)
{
    uint32_t p = 0; out[0] = 0; if (!args) args = "";
    args = sin_blancos(args);
    struct ct_libreta *l = contactos_libreta(); const char *r;
    if (!l) { put(out, max, &p, "Sin memoria para los contactos.\n"); return 1; }
    if (g_err_carga[0]) { put(out, max, &p, g_err_carga); put(out, max, &p, "\n"); }
    if (!*args || palabra(args, "estado") || palabra(args, "resumen")) {
        put(out, max, &p, "Contactos: "); putn(out, max, &p, ct_numero(l)); put(out, max, &p, ".");
        int pend = 0; for (int i = 0; i < CT_MAX; ++i) pend += l->c[i].activo && l->c[i].cambiado;
        if (pend) { put(out, max, &p, " Cambiados desde la \303\272ltima exportaci\303\263n: "); putn(out, max, &p, pend); put(out, max, &p, "."); }
        put(out, max, &p, "\nPr\303\263ximos cumplea\303\261os:\n"); cumples_txt(30, out, max, &p);
        if (!*args) put(out, max, &p, "(contactos ayuda: todas las \303\263rdenes)\n");
        return 1;
    }
    if (palabra(args, "ayuda") || palabra(args, "help") || palabra(args, "?")) { put(out, max, &p, AYUDA); return 1; }
    { static const char *const W[] = { "a\303\261adir", "anadir", "a\303\261ade", "anade", "nuevo", "nueva", "crea", "crear", "guarda", "guardar", "apunta", "add", 0 };
      if ((r = una_de(args, W))) {
          if (!*r) { put(out, max, &p, "Uso: contactos a\303\261adir Luis P\303\251rez, email luis@x.es, m\303\263vil 600112233\n"); return 1; }
          char res[1500]; int id = contactos_anade_texto(r, res, sizeof res);
          if (id > 0) put(out, max, &p, res);
          else put(out, max, &p, id == -2 ? "La libreta est\303\241 llena (1500 contactos).\n" : id == -3 ? "No se pudo guardar.\n" : "No veo ni un nombre, ni un email, ni un tel\303\251fono. Ejemplo: contactos a\303\261adir Ana L\303\263pez, m\303\263vil 600112233\n");
          return 1;
      } }
    { static const char *const W[] = { "borrar", "borra", "quitar", "quita", "elimina", "eliminar", 0 };
      if ((r = una_de(args, W))) {
          struct ct_contacto *c; if (!*r) { put(out, max, &p, "Uso: contactos borrar <n|nombre>\n"); return 1; }
          if (!resuelve_uno(r, &c, out, max, &p)) return 1;
          char nc[CT_NOM + CT_APE + 4]; ct_nombre_completo(c, nc, sizeof nc); uint32_t id = c->id;
          if (contactos_borra(id) == 0) { put(out, max, &p, "Borrado ["); putn(out, max, &p, id); put(out, max, &p, "] "); put(out, max, &p, nc); put(out, max, &p, "\n"); }
          else put(out, max, &p, "No se pudo borrar.\n");
          return 1;
      } }
    { static const char *const W[] = { "editar", "edita", "cambiar", "cambia", "modifica", "modificar", "pon", "poner", 0 };
      if ((r = una_de(args, W))) {
          /* objetivo = lo que va antes del primer nombre de campo */
          const char *s = r, *campo = 0;
          while (*s) { s = sin_blancos(s); if (!*s) break; if (s != r && ct_es_campo(s)) { campo = s; break; } while (*s && *s != ' ') ++s; }
          if (!campo) { put(out, max, &p, "Uso: contactos editar <n|nombre> <campo> <valor>   (p. ej. contactos editar Ana movil 600112233)\n"); return 1; }
          char quien[120]; uint32_t L = (uint32_t)(campo - r); if (L > sizeof quien - 1) L = sizeof quien - 1; for (uint32_t i = 0; i < L; ++i) quien[i] = r[i]; quien[L] = 0;
          { uint32_t e = sl(quien); while (e && quien[e - 1] == ' ') quien[--e] = 0; }
          struct ct_contacto *c; if (!resuelve_uno(quien, &c, out, max, &p)) return 1;
          int k = ct_es_campo(campo); char cn[32]; uint32_t kk = (uint32_t)k < sizeof cn - 1 ? (uint32_t)k : sizeof cn - 1; for (uint32_t i = 0; i < kk; ++i) cn[i] = campo[i]; cn[kk] = 0;
          struct ct_contacto copia = *c;
          int e = ct_pon_campo(c, cn, campo + k);
          if (e) { *c = copia; put(out, max, &p, e == -2 ? "Ese valor no vale para ese campo (revisa el email o la fecha).\n" : "No conozco ese campo.\n"); return 1; }
          if (contactos_toca(c->id)) { *c = copia; put(out, max, &p, "No se pudo guardar.\n"); return 1; }
          char f[1500]; int y, m, d; contactos_hoy(&y, &m, &d); ct_ficha_txt(c, y, m, d, f, sizeof f); put(out, max, &p, "Cambiado.\n"); put(out, max, &p, f);
          return 1;
      } }
    { static const char *const W[] = { "ver", "ficha", "detalle", "muestra", "ense\303\261a", 0 };
      if ((r = una_de(args, W)) && *r) {
          struct ct_contacto *c; if (!resuelve_uno(r, &c, out, max, &p)) return 1;
          char f[1500]; int y, m, d; contactos_hoy(&y, &m, &d); ct_ficha_txt(c, y, m, d, f, sizeof f); put(out, max, &p, f); return 1;
      } }
    { static const char *const T[] = { "telefono", "tel\303\251fono", "telefonos", "tel\303\251fonos", "movil", "m\303\263vil", "numero", "n\303\272mero", 0 };
      static const char *const E[] = { "email", "correo", "mail", "emails", 0 };
      static const char *const D[] = { "direccion", "direcci\303\263n", "domicilio", 0 };
      int que = (r = una_de(args, T)) ? 1 : (r = una_de(args, E)) ? 2 : (r = una_de(args, D)) ? 3 : 0;
      if (!que) { static const char *const C[] = { "cumplea\303\261os", "cumpleanos", "cumple", "cumples", 0 }; if ((r = una_de(args, C)) && *r && !es_num(r, &(uint32_t){0})) que = 4; else r = 0; }
      if (que && r && *r) {
          if (palabra(r, "de")) r = palabra(r, "de");
          struct ct_contacto *c; if (!resuelve_uno(r, &c, out, max, &p)) return 1;
          char nc[CT_NOM + CT_APE + 4]; ct_nombre_completo(c, nc, sizeof nc); put(out, max, &p, nc); put(out, max, &p, ": ");
          int hay = 0;
          if (que == 1) for (int i = 0; i < CT_NTEL; ++i) if (c->tel[i][0]) { if (hay++) put(out, max, &p, ", "); put(out, max, &p, c->tel[i]); if (c->t_tel[i]) { put(out, max, &p, " ("); put(out, max, &p, ct_nombre_tipo(c->t_tel[i])); put(out, max, &p, ")"); } }
          if (que == 2) for (int i = 0; i < CT_NEMAIL; ++i) if (c->email[i][0]) { if (hay++) put(out, max, &p, ", "); put(out, max, &p, c->email[i]); }
          if (que == 3) { const char *const P[5] = { c->calle, c->cp, c->ciudad, c->prov, c->pais }; for (int i = 0; i < 5; ++i) if (P[i][0]) { if (hay++) put(out, max, &p, ", "); put(out, max, &p, P[i]); } }
          if (que == 4 && c->nac_m) {
              int y, m, d; contactos_hoy(&y, &m, &d); hay = 1;
              putn(out, max, &p, c->nac_d); put(out, max, &p, " de "); put(out, max, &p, ct_nombre_mes(c->nac_m)); if (c->nac_y) { put(out, max, &p, " de "); putn(out, max, &p, c->nac_y); }
              int f = ct_dias_para_cumple(c, y, m, d), e = ct_edad(c, y, m, d);
              if (f == 0) put(out, max, &p, " (\302\241es hoy!)"); else { put(out, max, &p, " (faltan "); putn(out, max, &p, f); put(out, max, &p, f == 1 ? " d\303\255a)" : " d\303\255as)"); }
              if (e >= 0) { put(out, max, &p, "; tiene "); putn(out, max, &p, e); put(out, max, &p, " a\303\261os"); }
          }
          if (!hay) put(out, max, &p, que == 1 ? "no tiene tel\303\251fono apuntado" : que == 2 ? "no tiene email apuntado" : que == 3 ? "no tiene direcci\303\263n apuntada" : "no tiene fecha de nacimiento apuntada");
          put(out, max, &p, "\n"); return 1;
      } }
    if (palabra(args, "grupos")) {
        static char g[32][CT_GRUPO]; int n = ct_grupos(l, g, 32);
        if (!n) { put(out, max, &p, "No hay grupos. Pon uno con: contactos editar <n> grupo Familia\n"); return 1; }
        put(out, max, &p, "Grupos:\n");
        for (int i = 0; i < n; ++i) { static uint16_t idx[CT_MAX]; int k = ct_lista(l, 0, g[i], 0, idx, CT_MAX); put(out, max, &p, "  "); put(out, max, &p, g[i]); put(out, max, &p, " ("); putn(out, max, &p, k); put(out, max, &p, ")\n"); }
        return 1;
    }
    if ((r = palabra(args, "grupo")) && *r) {
        static uint16_t idx[CT_MAX]; int n = ct_lista(l, 0, r, 0, idx, CT_MAX);
        if (!n) { put(out, max, &p, "No hay nadie en ese grupo.\n"); return 1; }
        put(out, max, &p, "Grupo "); put(out, max, &p, r); put(out, max, &p, ":\n"); lista_txt(idx, n, out, max, &p); return 1;
    }
    if (palabra(args, "favoritos") || palabra(args, "favoritas")) {
        static uint16_t idx[CT_MAX]; int n = ct_lista(l, 0, 0, 1, idx, CT_MAX);
        if (!n) { put(out, max, &p, "No hay favoritos (contactos editar <n> favorito si).\n"); return 1; }
        lista_txt(idx, n, out, max, &p); return 1;
    }
    { static const char *const C[] = { "cumplea\303\261os", "cumpleanos", "cumple", "cumples", 0 };
      if ((r = una_de(args, C))) {
          uint32_t dias = 30; if (*r) { const char *q = r; if (palabra(q, "proximos")) q = palabra(q, "proximos"); else if (palabra(q, "pr\303\263ximos")) q = palabra(q, "pr\303\263ximos"); uint32_t v; char nb[16]; uint32_t k = 0; while (q[k] >= '0' && q[k] <= '9' && k < 15) { nb[k] = q[k]; ++k; } nb[k] = 0; if (es_num(nb, &v) && v >= 1 && v <= 366) dias = v; }
          put(out, max, &p, "Cumplea\303\261os en los pr\303\263ximos "); putn(out, max, &p, dias); put(out, max, &p, " d\303\255as:\n"); cumples_txt((int)dias, out, max, &p); return 1;
      } }
    if ((r = palabra(args, "foto"))) {
        const char *s = r; while (*s && *s != '/') ++s;
        if (!*s) { put(out, max, &p, "Uso: contactos foto <n|nombre> <ruta de la imagen>\n"); return 1; }
        char quien[120]; uint32_t L = (uint32_t)(s - r); if (L > sizeof quien - 1) L = sizeof quien - 1; for (uint32_t i = 0; i < L; ++i) quien[i] = r[i]; quien[L] = 0;
        { uint32_t e = sl(quien); while (e && quien[e - 1] == ' ') quien[--e] = 0; }
        struct ct_contacto *c; if (!resuelve_uno(quien, &c, out, max, &p)) return 1;
        char msg[160]; (void)contactos_foto_anade(c->id, s, msg, sizeof msg); put(out, max, &p, msg); put(out, max, &p, "\n"); return 1;
    }
    if ((r = palabra(args, "exportar")) || (r = palabra(args, "export"))) {
        char ruta[160]; cpy(ruta, sizeof ruta, CT_VCF_DEF); int fotos = 1;
        if (*r == '/') { uint32_t k = 0; while (r[k] && r[k] != ' ' && k + 1 < sizeof ruta) { ruta[k] = r[k]; ++k; } ruta[k] = 0; r = sin_blancos(r + k); }
        if (palabra(r, "sin")) fotos = 0;
        (void)contactos_exporta(ruta, fotos, out + p, max - p); return 1;
    }
    if ((r = palabra(args, "importar")) || (r = palabra(args, "import"))) {
        if (!*r) { put(out, max, &p, "Uso: contactos importar <archivo.vcf>   (en Google Contactos: Exportar > vCard; luego p\303\241salo a /usuario)\n"); return 1; }
        (void)contactos_importa(r, out + p, max - p); return 1;
    }
    { static const char *const W[] = { "buscar", "busca", "lista", "listar", "todos", 0 };
      if ((r = una_de(args, W)) || 1) {
          const char *q = r ? r : args;
          static uint16_t idx[CT_MAX]; int n = ct_lista(l, q, 0, 0, idx, CT_MAX);
          if (!n && !r) { uint16_t j[8]; n = ct_resuelve(l, q, j, 8); for (int i = 0; i < n; ++i) idx[i] = j[i]; }
          if (!n) { put(out, max, &p, *q ? "No encuentro a nadie con ese texto.\n" : "La libreta est\303\241 vac\303\255a.\n"); if (!r) put(out, max, &p, "(contactos ayuda: todas las \303\263rdenes)\n"); return 1; }
          if (n == 1 && *q) { char f[1500]; int y, m, d; contactos_hoy(&y, &m, &d); ct_ficha_txt(&l->c[idx[0]], y, m, d, f, sizeof f); put(out, max, &p, f); return 1; }
          putn(out, max, &p, n); put(out, max, &p, n == 1 ? " contacto:\n" : " contactos:\n"); lista_txt(idx, n, out, max, &p); return 1;
      } }
    return 1;
}

/* ---------------------------------------------------------------- el agente */
static int palabra_en(const char *pl, const char *w)              /* w (plegada, >= 3) es una palabra entera de pl */
{
    uint32_t n = sl(w); if (n < 3) return 0;
    for (const char *s = pl; *s; ++s) {
        if (s != pl && ((s[-1] >= 'a' && s[-1] <= 'z') || (s[-1] >= '0' && s[-1] <= '9'))) continue;
        uint32_t k = 0; while (k < n && s[k] == w[k]) ++k;
        if (k == n && !((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= '0' && s[n] <= '9'))) return 1;
    }
    return 0;
}
void contactos_contexto(const char *mision, char *dst, uint32_t max, uint32_t *pos)
{
    if (!g_cargado && vfs_size(CT_FICHERO) <= 0) return;           /* sin libreta guardada no se reserva memoria para nada */
    struct ct_libreta *l = contactos_libreta(); if (!l || !mision || *pos + 600 >= max) return;
    char m[600]; ct_pliega(mision, m, sizeof m);
    static const char *const P[] = { "contacto", "contactos", "telefono", "movil", "email", "correo de", "direccion", "cumple", "cumpleanos", "llamar", "llama", "escribe", "escribir", "felicitar", "agenda de", "vive", 0 };
    int pista = 0; for (int i = 0; P[i]; ++i) if (contiene_pl(m, P[i])) pista = 1;
    uint16_t sel[4]; int ns = 0;
    for (int i = 0; i < CT_MAX && ns < 4; ++i) {
        const struct ct_contacto *c = &l->c[i]; if (!c->activo) continue;
        char nom[CT_NOM + 4], ape[CT_APE + 4], apo[CT_APODO + 4]; ct_pliega(c->nombre, nom, sizeof nom); ct_pliega(c->apellidos, ape, sizeof ape); ct_pliega(c->apodo, apo, sizeof apo);
        { uint32_t k = 0; while (ape[k] && ape[k] != ' ') ++k; ape[k] = 0; }     /* el primer apellido */
        int n_ok = palabra_en(m, nom), a_ok = palabra_en(m, ape), p_ok = palabra_en(m, apo);
        if ((n_ok && a_ok) || ((n_ok || p_ok) && pista) || (a_ok && pista && sl(ape) >= 4)) sel[ns++] = (uint16_t)i;
    }
    int cumple = contiene_pl(m, "cumple");
    if (!ns && !cumple) return;
    const char *cab = "\n[CONTACTOS de SOMA: fichas de la libreta del usuario que vienen al caso. Son los datos que constan; no inventes otros.]\n";
    uint32_t p = *pos; put(dst, max, &p, cab);
    int y, mm, d; contactos_hoy(&y, &mm, &d);
    for (int i = 0; i < ns && p + 700 < max; ++i) { char f[1500]; ct_ficha_txt(&l->c[sel[i]], y, mm, d, f, sizeof f); put(dst, max, &p, f); }
    if (cumple && p + 400 < max) { char t[1200]; uint32_t q = 0; t[0] = 0; cumples_txt(30, t, sizeof t, &q); put(dst, max, &p, "Cumplea\303\261os en los pr\303\263ximos 30 d\303\255as:\n"); put(dst, max, &p, t); }
    *pos = p;
}
