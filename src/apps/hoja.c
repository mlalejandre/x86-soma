/* SOMA_358_HOJA: ver hoja.h */
#include <stdint.h>
#include <stddef.h>
#include "hoja.h"
#include "hoja_fmt.h"
#include "agenda.h"
#include "fs.h"
#include "mem.h"
#include "bigfs.h"
#include "bigfs_glue.h"
#if defined(__x86_64__) && !defined(WEB_HOST_TEST)
#include "sandbox.h"
#include "acceso.h"      /* SOMA_363_ACCESO */
#endif

void *hj_alloc(uint32_t n) { return kmalloc(n ? n : 1); }
void  hj_free(void *p) { if (p) kfree(p); }

struct hoja_doc { struct hj_libro l; char ruta[192]; int formato; uint32_t ver_guardada; };
static struct hoja_doc *g_doc[HOJA_LIBROS];
static int g_actual = -1;
static uint32_t g_ver = 1;

/* ---------------------------------------------------------------- cadenas */
static uint32_t sl(const char *s) { uint32_t n = 0; while (s && s[n]) ++n; return n; }
static void put(char *o, uint32_t cap, uint32_t *p, const char *s) { while (s && *s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void putn(char *o, uint32_t cap, uint32_t *p, int64_t v) { char b[24]; int n = 0; uint64_t u = v < 0 ? (uint64_t)(-v) : (uint64_t)v; if (v < 0) put(o, cap, p, "-"); do { b[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 22); while (n) { char c[2] = { b[--n], 0 }; put(o, cap, p, c); } }
static void cpy(char *d, uint32_t cap, const char *s) { uint32_t p = 0; if (cap) d[0] = 0; put(d, cap, &p, s); }
static char baja(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int emp(const char *s, const char *pre) { while (*pre) { if (baja(*s) != *pre) return 0; ++s; ++pre; } return 1; }
static const char *sin_blancos(const char *s) { while (*s == ' ') ++s; return s; }
static const char *palabra(const char *s, const char *w) { s = sin_blancos(s); uint32_t n = sl(w); if (!emp(s, w)) return 0; if (s[n] && s[n] != ' ') return 0; return sin_blancos(s + n); }
static int termina(const char *s, const char *suf) { uint32_t a = sl(s), b = sl(suf); if (b > a) return 0; for (uint32_t i = 0; i < b; ++i) if (baja(s[a - b + i]) != suf[i]) return 0; return 1; }
static int64_t ahora(void) { return agenda_ahora(); }
static void ini(void) { if (!hj_ahora_fn) hj_ahora_fn = ahora; }

/* ---------------------------------------------------------------- archivos */
int hoja_formato_ruta(const char *r)
{
    if (termina(r, ".xlsx") || termina(r, ".xlsm")) return HJF_XLSX;
    if (termina(r, ".ods")) return HJF_ODS;
    if (termina(r, ".csv") || termina(r, ".tsv")) return HJF_CSV;
    if (termina(r, ".hoja")) return HJF_SOMA;
    return HJF_DESCONOCIDO;
}
/* Solo hojas de calculo (por extension) y, para escribir, solo en las carpetas del usuario: asi la orden no sirve
 * para leer claves o .conf ni para pisar archivos del sistema. */
static int ruta_valida(const char *r, int escribir)
{
    if (!r || r[0] != '/' || sl(r) >= 190 || !hoja_formato_ruta(r)) return 0;
    for (const char *z = r; *z; ++z) { if ((uint8_t)*z < 32 || *z == '\\') return 0; if (z[0] == '/' && z[1] == '.' ) return 0; }
    if (!escribir) return emp(r, "/usuario/") || emp(r, "/usb/") || emp(r, "/tmp/") || emp(r, "/workspace/");   /* SOMA_363_ACCESO */
    return emp(r, "/usuario/") || emp(r, "/usb/") || emp(r, "/tmp/");
}
static uint8_t *lee_archivo(const char *ruta, uint32_t *n, uint32_t max)
{
    *n = 0; int t = vfs_size(ruta);
    if (t >= 0) { if ((uint32_t)t > max) { *n = (uint32_t)t; return 0; } uint32_t k = 0; char *b = vfs_read_alloc(ruta, &k); if (b) { *n = k; return (uint8_t *)b; } }
    int e = 0; struct bigfs *fs = bigfs_sys(&e); uint32_t ino; struct bigfs_stat st;
    if (!fs || bigfs_lookup(fs, ruta, &ino) != (int)BIGFS_T_FILE || bigfs_stat(fs, ino, &st) < 0 || !st.size) return 0;
    if (st.size > max) { *n = (uint32_t)(st.size > 0xFFFFFFFFu ? 0xFFFFFFFFu : st.size); return 0; }
    uint8_t *b = (uint8_t *)kmalloc((size_t)st.size + 1u); if (!b) return 0;
    if (bigfs_read(fs, ino, 0, b, st.size) != (int64_t)st.size) { kfree(b); return 0; }
    b[st.size] = 0; *n = (uint32_t)st.size; return b;
}
/* el archivo de fuera -> texto "SOMA-HOJA 1" (leido aislado en x86_64). 0 = bien */
static int a_texto_propio(const uint8_t *d, uint32_t n, int tipo, char **txt, uint32_t *tn, char *err, uint32_t cap)
{
    *txt = 0; *tn = 0;
#if defined(__x86_64__) && !defined(WEB_HOST_TEST)
    if (n > SBX_IN_MAX) { cpy(err, cap, "El archivo es demasiado grande."); return -1; }
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    if (!c) { cpy(err, cap, "Sin memoria."); return -2; }
    sbx_prepare(c, SBX_MIME_HOJA, d, n, 0);
    c->in.arg[0] = tipo;
    int r = sbx_svc_call(SBX_SVC_MIME, c, 60000), res = -2;
    if (r == 0 && c->out.status == 0) {
        if (c->out.val[0] != 0) {
            uint32_t L = c->out.inline_len < 200 ? c->out.inline_len : 199; char m[200]; for (uint32_t i = 0; i < L; ++i) m[i] = (char)c->out.inline_data[i];
            m[L] = 0; for (uint32_t i = 0; i < L; ++i) if ((uint8_t)m[i] < 32) m[i] = ' ';
            cpy(err, cap, m[0] ? m : "No se pudo leer el archivo."); res = -1;
        } else if (c->out.data_len > 11 && c->out.data_len < (256u << 20)) {
            char *b = (char *)kmalloc((size_t)c->out.data_len + 1u);
            if (b && sbx_copy_data(c, b, c->out.data_len) == 0) { b[c->out.data_len] = 0; *txt = b; *tn = (uint32_t)c->out.data_len; res = 0; }
            else { if (b) kfree(b); cpy(err, cap, "Sin memoria."); }
        } else cpy(err, cap, "El lector aislado no devolvio nada.");
    } else cpy(err, cap, "El lector aislado no responde.");
    sbx_release(c); kfree(c);
    return res;
#else
    struct hj_libro *l = (struct hj_libro *)kmalloc(sizeof *l);
    if (!l) { cpy(err, cap, "Sin memoria."); return -2; }
    int r = hj_lee_archivo(l, d, n, tipo, err, cap);
    if (!r) {
        uint32_t c = hj_serializa_tam(l); char *b = (char *)kmalloc(c);
        uint32_t k = b ? hj_serializa(l, b, c) : 0;
        if (k) { *txt = b; *tn = k; } else { if (b) kfree(b); r = -2; cpy(err, cap, "Sin memoria."); }
    }
    hj_libro_libera(l); kfree(l);
    return r;
#endif
}
uint32_t hoja_version(void) { return g_ver; }
struct hj_libro *hoja_libro(int i) { return i >= 0 && i < HOJA_LIBROS && g_doc[i] ? &g_doc[i]->l : 0; }
const char *hoja_ruta(int i) { return i >= 0 && i < HOJA_LIBROS && g_doc[i] ? g_doc[i]->ruta : ""; }
int hoja_cambiado(int i) { return i >= 0 && i < HOJA_LIBROS && g_doc[i] && g_doc[i]->l.version != g_doc[i]->ver_guardada; }
void hoja_toca(int i) { struct hj_libro *l = hoja_libro(i); if (!l) return; l->version++; (void)hj_recalcula(l); ++g_ver; }
int hoja_actual(void) { return g_actual >= 0 && g_doc[g_actual] ? g_actual : -1; }
void hoja_pon_actual(int i) { if (i >= 0 && i < HOJA_LIBROS && g_doc[i]) { g_actual = i; ++g_ver; } }
static int hueco(void) { for (int i = 0; i < HOJA_LIBROS; ++i) if (!g_doc[i]) return i; return -1; }
/* SOMA_363_ACCESO: los libros nuevos van a /usuario/Documentos/Hojas de calculo (sin unidad del usuario, a /tmp) */
const char *hoja_carpeta(void)
{
    static char d[96];
    acceso_carpeta(ACC_HOJAS, d, sizeof d - 1);
    uint32_t n = 0; while (d[n]) n++;
    d[n] = '/'; d[n + 1] = 0;
    return d;
}
int hoja_nuevo(const char *nombre)
{
    ini();
    int i = hueco(); if (i < 0) return -1;
    struct hoja_doc *d = (struct hoja_doc *)kmalloc(sizeof *d); if (!d) return -1;
    memset(d, 0, sizeof *d);
    if (hj_libro_nuevo(&d->l)) { kfree(d); return -1; }
    char nb[96]; cpy(nb, sizeof nb, nombre && nombre[0] ? nombre : "Libro");
    for (char *z = nb; *z; ++z) if (*z == '/' || *z == '\\' || *z == ':' || (uint8_t)*z < 32) *z = '_';
    uint32_t p = 0; put(d->ruta, sizeof d->ruta, &p, hoja_carpeta()); put(d->ruta, sizeof d->ruta, &p, nb);   /* SOMA_363 */ if (!hoja_formato_ruta(d->ruta)) put(d->ruta, sizeof d->ruta, &p, ".xlsx");
    d->formato = hoja_formato_ruta(d->ruta); d->ver_guardada = d->l.version;
    g_doc[i] = d; g_actual = i; ++g_ver;
    return i;
}
int hoja_abre(const char *ruta, char *msg, uint32_t cap)
{
    ini(); if (cap) msg[0] = 0;
    for (int i = 0; i < HOJA_LIBROS; ++i) if (g_doc[i]) { const char *a = g_doc[i]->ruta, *b = ruta; while (*a && *a == *b) { ++a; ++b; } if (!*a && !*b) { g_actual = i; return i; } }
    if (!ruta_valida(ruta, 0)) { cpy(msg, cap, "Solo abro hojas de c\303\241lculo: .xlsx, .xlsm, .ods, .csv, .tsv o .hoja (con la ruta entera, /usuario/...)."); return -1; }
    int i = hueco(); if (i < 0) { cpy(msg, cap, "Hay 8 libros abiertos: cierra alguno."); return -3; }
    uint32_t n = 0; uint8_t *d = lee_archivo(ruta, &n, HJF_MAX_ENTRADA);
    if (!d) { cpy(msg, cap, n ? "El archivo es demasiado grande (48 MB como mucho)." : "No encuentro ese archivo."); return -1; }
    char *txt; uint32_t tn; int tipo = hoja_formato_ruta(ruta);
    int r = a_texto_propio(d, n, tipo, &txt, &tn, msg, cap);
    kfree(d);
    if (r) return r;
    struct hoja_doc *doc = (struct hoja_doc *)kmalloc(sizeof *doc);
    if (!doc) { kfree(txt); cpy(msg, cap, "Sin memoria."); return -2; }
    memset(doc, 0, sizeof *doc);
    r = hj_carga(&doc->l, txt, tn);                              /* lo que vuelve del servicio se vuelve a validar aqui */
    kfree(txt);
    if (r) { kfree(doc); cpy(msg, cap, r == -1 ? "El lector aislado devolvio algo que no es una hoja." : "Sin memoria para el libro."); return -2; }
    cpy(doc->ruta, sizeof doc->ruta, ruta); doc->formato = tipo ? tipo : HJF_XLSX; doc->ver_guardada = doc->l.version;
    g_doc[i] = doc; g_actual = i; ++g_ver;
    return i;
}
int hoja_guarda(int i, const char *ruta, char *msg, uint32_t cap)
{
    struct hj_libro *l = hoja_libro(i); if (cap) msg[0] = 0;
    if (!l) { cpy(msg, cap, "No hay ningun libro abierto."); return -1; }
    const char *dst = ruta && ruta[0] ? ruta : g_doc[i]->ruta;
    int f = hoja_formato_ruta(dst);
    if (!f) { cpy(msg, cap, "Pon la extensi\303\263n: .xlsx (Excel), .ods (LibreOffice), .csv o .hoja."); return -1; }
    if (termina(dst, ".xlsm")) { cpy(msg, cap, "Gu\303\241rdalo como .xlsx (SOMA no escribe macros)."); return -1; }
    if (!ruta_valida(dst, 1)) { cpy(msg, cap, "Solo se guarda en /usuario/ o /tmp/."); return -1; }
    (void)hj_recalcula(l);
    uint8_t *b = 0; uint32_t n = 0;
    if (f == HJF_XLSX) b = hj_escribe_xlsx(l, &n);
    else if (f == HJF_ODS) b = hj_escribe_ods(l, &n);
    else if (f == HJF_CSV) { uint32_t c = 1u << 20; for (int k = 0; k < 6 && !n; ++k, c *= 4) { if (b) kfree(b); b = (uint8_t *)kmalloc(c); if (!b) break; n = hj_csv_escribe(l, l->activa, termina(dst, ".tsv") ? '\t' : ';', (char *)b, c); } }
    else { uint32_t c = hj_serializa_tam(l); b = (uint8_t *)kmalloc(c); if (b) n = hj_serializa(l, (char *)b, c); }
    if (!b || !n) { if (b) kfree(b); cpy(msg, cap, "Sin memoria para escribir el archivo."); return -2; }
    int w = vfs_write(dst, (const char *)b, n); kfree(b);
    if (w < 0) { cpy(msg, cap, emp(dst, "/usuario/") ? "No se pudo escribir en /usuario (\302\277est\303\241 conectada tu unidad de usuario?). Prueba en /tmp/." : "No se pudo escribir el archivo."); return -1; }
    if (!ruta || !ruta[0] || !g_doc[i]->ruta[0]) { g_doc[i]->ver_guardada = l->version; }
    else if (f != HJF_CSV) { cpy(g_doc[i]->ruta, sizeof g_doc[i]->ruta, dst); g_doc[i]->formato = f; g_doc[i]->ver_guardada = l->version; }
    uint32_t p = 0; put(msg, cap, &p, "Guardado en "); put(msg, cap, &p, dst);
    if (f == HJF_CSV) put(msg, cap, &p, " (CSV: solo la hoja activa y sin formulas ni formato)");
    put(msg, cap, &p, ".");
    ++g_ver;
    return 0;
}
int hoja_cierra(int i)
{
    if (i < 0 || i >= HOJA_LIBROS || !g_doc[i]) return -1;
    hj_libro_libera(&g_doc[i]->l); kfree(g_doc[i]); g_doc[i] = 0;
    if (g_actual == i) { g_actual = -1; for (int k = 0; k < HOJA_LIBROS; ++k) if (g_doc[k]) { g_actual = k; break; } }
    ++g_ver; return 0;
}

/* ---------------------------------------------------------------- la orden `hoja` */
static const char *AYUDA =
    "Hoja de c\303\241lculo. Abre y guarda XLSX (Excel), ODS (LibreOffice) y CSV.\n"
    "  hoja                         libros abiertos\n"
    "  hoja nueva [nombre] | abrir <archivo> | guardar [archivo.xlsx|.ods|.csv|.hoja] | cerrar | libro <n>\n"
    "  hoja ver [rango]             (hoja ver A1:F20)\n"
    "  hoja pon <celda> <valor>     hoja pon B5 =SUMA(B1:B4)   |   hoja pon A1 Gastos de noviembre\n"
    "  hoja borra <rango> | calcula <f\303\263rmula> | funciones\n"
    "  hoja hojas | hoja <nombre de hoja> | nueva-hoja <nombre> | renombrar <nombre>\n"
    "  hoja ordenar <rango> por <columna> [desc] | filtrar <rango> <columna> <criterio> | filtrar quitar\n"
    "  hoja buscar <texto> | reemplazar <texto> por <otro> | inmovilizar <celda> | ancho <columna> <px>\n"
    "  hoja grafico columnas|barras|lineas|tarta|area|dispersion [A1:D9] [titulo Texto] | hoja grafico (lista) | grafico borrar <n>\n"
    "F\303\263rmulas en castellano (SUMA, PROMEDIO, SI, BUSCARV...) con ';' entre argumentos y coma decimal.\n";
static uint32_t u8c(const char *s) { uint32_t n = 0; for (; *s; ++s) if (((uint8_t)*s & 0xC0) != 0x80) ++n; return n; }
static void pad(char *o, uint32_t cap, uint32_t *p, const char *s, uint32_t ancho, int der)
{
    char t[64]; uint32_t k = 0, c = 0;
    for (const char *z = s; *z && k + 4 < sizeof t; ++z) { if (((uint8_t)*z & 0xC0) != 0x80) { if (c + 1 >= ancho && u8c(z) > 1) { put(t, sizeof t, &k, "\342\200\246"); c++; break; } ++c; } t[k++] = (*z == '\n' || *z == '\t') ? ' ' : *z; t[k] = 0; }
    t[k] = 0;
    if (der) for (uint32_t i = c; i < ancho; ++i) put(o, cap, p, " ");
    put(o, cap, p, t);
    if (!der) for (uint32_t i = c; i < ancho; ++i) put(o, cap, p, " ");
}
static void ver_rango(struct hj_libro *l, int h, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, char *o, uint32_t max, uint32_t *p)
{
    const struct hj_hoja *s = &l->h[h];
    if (f2 >= s->max_fila) f2 = s->max_fila ? s->max_fila - 1 : 0;
    if (c2 >= s->max_col) c2 = s->max_col ? s->max_col - 1 : 0;
    if (f2 - f1 > 59) f2 = f1 + 59;
    if (c2 - c1 > 11) c2 = c1 + 11;
    uint32_t an[12]; char b[256];
    for (uint32_t c = c1; c <= c2; ++c) { uint32_t w = 3; for (uint32_t f = f1; f <= f2; ++f) { if (hj_fila_oculta(s, f)) continue; hj_texto_ver(l, h, f, c, b, sizeof b); uint32_t k = u8c(b); if (k > w) w = k; } an[c - c1] = w > 24 ? 24 : w; }
    char rn[16]; uint32_t q = 0; rn[0] = 0; putn(rn, sizeof rn, &q, (int64_t)f2 + 1); uint32_t rw = sl(rn);
    pad(o, max, p, "", rw, 0); put(o, max, p, " \342\224\202");
    for (uint32_t c = c1; c <= c2; ++c) { char cn[8]; hj_nombre_col(c, cn); put(o, max, p, " "); pad(o, max, p, cn, an[c - c1], 0); }
    put(o, max, p, "\n");
    for (uint32_t f = f1; f <= f2; ++f) {
        if (hj_fila_oculta(s, f)) continue;
        q = 0; rn[0] = 0; putn(rn, sizeof rn, &q, (int64_t)f + 1); pad(o, max, p, rn, rw, 1); put(o, max, p, " \342\224\202");
        for (uint32_t c = c1; c <= c2; ++c) {
            hj_texto_ver(l, h, f, c, b, sizeof b); const struct hj_celda *x = hj_celda_c(l, h, f, c);
            int der = x && (x->t == HJ_NUM || x->t == HJ_BOOL || x->t == HJ_ERR);
            put(o, max, p, " "); pad(o, max, p, b, an[c - c1], der);
        }
        put(o, max, p, "\n");
        if (*p + 400 >= max) { put(o, max, p, "\342\200\246\n"); break; }
    }
}
/* "gastos.xlsx" -> "/usuario/Documentos/Hojas de calculo/gastos.xlsx" (SOMA_363_ACCESO; si no esta ahi pero si en /usuario, esa) */
static const char *ruta_entera(const char *r, char *b, uint32_t cap)
{
    if (!*r || *r == '/') return r;
    uint32_t p = 0; b[0] = 0; put(b, cap, &p, "/usuario/"); put(b, cap, &p, r);
    if (vfs_size(b) >= 0) return b;                                      /* los de antes del 363, en la raiz de Mi unidad */
    p = 0; b[0] = 0; put(b, cap, &p, hoja_carpeta()); put(b, cap, &p, r);
    return b;
}
static int col_de(const char *s, uint32_t *c) { uint32_t v = 0; int n = 0; s = sin_blancos(s); while (((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) && n < 4) { v = v * 26 + (uint32_t)(baja(*s) - 'a' + 1); ++s; ++n; } if (!n || n > 3 || v > HJ_COLS) return 0; *c = v - 1; return n; }
int hoja_cmd(const char *args, char *out, uint32_t max)
{
    ini();
    uint32_t p = 0; out[0] = 0; if (!args) args = "";
    args = sin_blancos(args); const char *r;
    int i = hoja_actual(); struct hj_libro *l = hoja_libro(i);
    if (!*args) {
        int hay = 0;
        for (int k = 0; k < HOJA_LIBROS; ++k) if (g_doc[k]) {
            hay = 1; put(out, max, &p, k == i ? "* " : "  "); putn(out, max, &p, k + 1); put(out, max, &p, ". "); put(out, max, &p, g_doc[k]->ruta);
            put(out, max, &p, " ("); putn(out, max, &p, g_doc[k]->l.nh); put(out, max, &p, g_doc[k]->l.nh == 1 ? " hoja" : " hojas"); if (hoja_cambiado(k)) put(out, max, &p, ", sin guardar"); put(out, max, &p, ")\n");
        }
        if (!hay) put(out, max, &p, "No hay ning\303\272n libro abierto. hoja nueva | hoja abrir <archivo>\n");
        put(out, max, &p, "(hoja ayuda: todas las \303\263rdenes)\n");
        return 1;
    }
    if (palabra(args, "ayuda") || palabra(args, "help") || palabra(args, "?")) { put(out, max, &p, AYUDA); return 1; }
    if (palabra(args, "funciones")) { put(out, max, &p, "Funciones: "); hj_lista_funciones(out + p, max - p, 0); p = sl(out); put(out, max, &p, "\n"); return 1; }
    if ((r = palabra(args, "nueva")) || (r = palabra(args, "nuevo"))) {
        int k = hoja_nuevo(r); if (k < 0) { put(out, max, &p, "No se pudo crear (8 libros abiertos como mucho).\n"); return 1; }
        put(out, max, &p, "Libro nuevo: "); put(out, max, &p, g_doc[k]->ruta); put(out, max, &p, " (se guarda con: hoja guardar)\n"); return 1;
    }
    if ((r = palabra(args, "abrir")) || (r = palabra(args, "abre"))) {
        if (!*r) { put(out, max, &p, "Uso: hoja abrir <archivo.xlsx|.ods|.csv>\n"); return 1; }
        char rb[200]; r = ruta_entera(r, rb, sizeof rb);
        char msg[200]; int k = hoja_abre(r, msg, sizeof msg);
        if (k < 0) { put(out, max, &p, msg); put(out, max, &p, "\n"); return 1; }
        struct hj_libro *x = hoja_libro(k); put(out, max, &p, "Abierto "); put(out, max, &p, r); put(out, max, &p, ": ");
        putn(out, max, &p, x->nh); put(out, max, &p, x->nh == 1 ? " hoja.\n" : " hojas.\n");
        ver_rango(x, x->activa, 0, 0, 14, 7, out, max, &p); return 1;
    }
    if ((r = palabra(args, "libro"))) { int k = (int)(*r - '1'); if (k >= 0 && k < HOJA_LIBROS && g_doc[k]) { hoja_pon_actual(k); put(out, max, &p, "Libro actual: "); put(out, max, &p, g_doc[k]->ruta); put(out, max, &p, "\n"); } else put(out, max, &p, "No hay ese libro (hoja: la lista).\n"); return 1; }
    if (!l) { put(out, max, &p, "No hay ning\303\272n libro abierto. hoja nueva | hoja abrir <archivo>\n"); return 1; }
    int h = l->activa;
    if ((r = palabra(args, "guardar")) || (r = palabra(args, "guarda")) || (r = palabra(args, "exportar")) || (r = palabra(args, "exporta"))) {
        char rb[200]; r = ruta_entera(r, rb, sizeof rb);
        char msg[300]; (void)hoja_guarda(i, r, msg, sizeof msg); put(out, max, &p, msg); put(out, max, &p, "\n"); return 1;
    }
    if (palabra(args, "cerrar")) { char rt[192]; cpy(rt, sizeof rt, g_doc[i]->ruta); int c = hoja_cambiado(i); hoja_cierra(i); put(out, max, &p, "Cerrado "); put(out, max, &p, rt); put(out, max, &p, c ? " (ten\303\255a cambios sin guardar)\n" : "\n"); return 1; }
    if ((r = palabra(args, "ver")) || (r = palabra(args, "muestra")) || (r = palabra(args, "ensena"))) {
        uint32_t f1 = 0, c1 = 0, f2 = 19, c2 = 7;
        if (*r && hj_lee_rango(r, &f1, &c1, &f2, &c2) <= 0) { put(out, max, &p, "Rango no v\303\241lido (ejemplo: A1:F20).\n"); return 1; }
        put(out, max, &p, l->h[h].nombre); put(out, max, &p, ":\n");
        ver_rango(l, h, f1, c1, f2, c2, out, max, &p); return 1;
    }
    if ((r = palabra(args, "pon")) || (r = palabra(args, "escribe")) || (r = palabra(args, "poner"))) {
        uint32_t f, c; int k = hj_lee_dir(r, &f, &c);
        if (k <= 0) { put(out, max, &p, "Uso: hoja pon <celda> <valor o =f\303\263rmula>\n"); return 1; }
        const char *v = sin_blancos(r + k); int e = 0;
        int x = hj_escribe(l, h, f, c, v, &e);
        if (x == -1) { put(out, max, &p, "No entiendo la f\303\263rmula (revisa par\303\251ntesis, comillas y que los argumentos vayan con ';').\n"); return 1; }
        if (x) { put(out, max, &p, "Sin memoria.\n"); return 1; }
        hoja_toca(i);
        char d[16], b[512]; hj_dir(f, c, d); hj_texto_ver(l, h, f, c, b, sizeof b);
        put(out, max, &p, d); put(out, max, &p, " = "); put(out, max, &p, b); put(out, max, &p, "\n"); return 1;
    }
    if ((r = palabra(args, "borra")) || (r = palabra(args, "borrar"))) {
        uint32_t f1, c1, f2, c2; if (hj_lee_rango(r, &f1, &c1, &f2, &c2) <= 0) { put(out, max, &p, "Uso: hoja borra <rango>\n"); return 1; }
        hj_borra_rango(l, h, f1, c1, f2, c2, 0); hoja_toca(i); put(out, max, &p, "Borrado.\n"); return 1;
    }
    if ((r = palabra(args, "calcula")) || (r = palabra(args, "calcular"))) {
        if (!*r) { put(out, max, &p, "Uso: hoja calcula =SUMA(A1:A10)\n"); return 1; }
        char f[600]; uint32_t q = 0; f[0] = 0; if (*r != '=') put(f, sizeof f, &q, "="); put(f, sizeof f, &q, r);
        uint32_t ff = HJ_FILAS - 1, cc = HJ_COLS - 1; int e = 0;
        struct hj_celda *ant = hj_celda(l, h, ff, cc); int habia = ant != 0;
        int x = hj_escribe(l, h, ff, cc, f, &e);
        if (x) { put(out, max, &p, x == -1 ? "No entiendo la f\303\263rmula.\n" : "Sin memoria.\n"); return 1; }
        (void)hj_recalcula(l); char b[512]; hj_texto_ver(l, h, ff, cc, b, sizeof b);
        hj_borra_celda(l, h, ff, cc); if (!habia) { struct hj_celda *z = hj_celda(l, h, ff, cc); if (z) z->estilo = 0; }
        { /* la celda de calculo no cuenta para el tamano de la hoja */ struct hj_hoja *s = &l->h[h]; uint32_t mf = 0, mc = 0; for (int32_t k = 0; k < s->nc; ++k) { const struct hj_celda *z = &s->c[k]; if (z->t == HJ_VACIA && !z->es_form && !z->estilo) continue; if (z->fila + 1 > mf) mf = z->fila + 1; if ((uint32_t)z->col + 1 > mc) mc = (uint32_t)z->col + 1; } s->max_fila = mf; s->max_col = mc; }
        put(out, max, &p, b); put(out, max, &p, "\n"); return 1;
    }
    if (palabra(args, "hojas")) {
        for (int k = 0; k < l->nh; ++k) { put(out, max, &p, k == h ? "* " : "  "); putn(out, max, &p, k + 1); put(out, max, &p, ". "); put(out, max, &p, l->h[k].nombre); put(out, max, &p, "\n"); }
        return 1;
    }
    if ((r = palabra(args, "nueva-hoja"))) { int k = hj_hoja_nueva(l, *r ? r : 0); if (k < 0) put(out, max, &p, "No se pudo (nombre repetido o no v\303\241lido, o 32 hojas).\n"); else { l->activa = k; hoja_toca(i); put(out, max, &p, "Hoja nueva: "); put(out, max, &p, l->h[k].nombre); put(out, max, &p, "\n"); } return 1; }
    if ((r = palabra(args, "renombrar"))) { int x = hj_hoja_renombra(l, h, r); put(out, max, &p, x == 0 ? "Hoja renombrada.\n" : x == -2 ? "Ya hay una hoja con ese nombre.\n" : "Nombre no v\303\241lido (sin : / \\ ? * [ ], 31 letras como mucho).\n"); if (!x) hoja_toca(i); return 1; }
    if ((r = palabra(args, "ordenar")) || (r = palabra(args, "ordena"))) {
        uint32_t f1, c1, f2, c2; int k = hj_lee_rango(r, &f1, &c1, &f2, &c2);
        const char *q = k > 0 ? palabra(r + k, "por") : 0; uint32_t col;
        if (k <= 0 || !q || !col_de(q, &col)) { put(out, max, &p, "Uso: hoja ordenar A2:D50 por B [desc]   (sin la fila de t\303\255tulos)\n"); return 1; }
        int desc = 0; { const char *z = q; while (*z && *z != ' ') ++z; z = sin_blancos(z); desc = emp(z, "desc") || emp(z, "mayor") || emp(z, "z"); }
        if (hj_ordena(l, h, f1, c1, f2, c2, col, desc, -1, 0)) put(out, max, &p, "No se pudo ordenar (la columna tiene que estar dentro del rango).\n"); else { hoja_toca(i); put(out, max, &p, "Ordenado.\n"); }
        return 1;
    }
    if ((r = palabra(args, "filtrar")) || (r = palabra(args, "filtra"))) {
        struct hj_hoja *s = &l->h[h];
        if (palabra(r, "quitar") || palabra(r, "quita") || palabra(r, "todo")) { if (s->hay_filtro) { s->nfil = 0; for (uint32_t f = s->filtro_r1; f <= s->filtro_r2 && f < HJ_FILAS; ++f) hj_oculta_fila(s, f, 0); s->hay_filtro = 0; } hoja_toca(i); put(out, max, &p, "Filtro quitado.\n"); return 1; }
        uint32_t f1, c1, f2, c2, col; int k = hj_lee_rango(r, &f1, &c1, &f2, &c2);
        const char *q = k > 0 ? sin_blancos(r + k) : 0; int kc = q ? col_de(q, &col) : 0;
        if (k <= 0 || !kc) { put(out, max, &p, "Uso: hoja filtrar A1:D50 C >100   (rango con la fila de t\303\255tulos; columna; criterio)\n"); return 1; }
        int vis = hj_filtra(l, h, f1, c1, f2, c2, col, sin_blancos(q + kc));
        hoja_toca(i); put(out, max, &p, "Filas que se ven: "); putn(out, max, &p, vis); put(out, max, &p, "\n"); return 1;
    }
    if ((r = palabra(args, "buscar")) || (r = palabra(args, "busca"))) {
        uint32_t f = 0, c = 0; int n = 0; char d[16], b[256];
        uint32_t f0 = 0, c0 = 0; int primero = 1;
        while (n < 30 && hj_busca(l, h, &f, &c, r, 0, 0)) { if (!primero && f == f0 && c == c0) break; if (primero) { f0 = f; c0 = c; primero = 0; } hj_dir(f, c, d); hj_texto_ver(l, h, f, c, b, sizeof b); put(out, max, &p, "  "); put(out, max, &p, d); put(out, max, &p, ": "); put(out, max, &p, b); put(out, max, &p, "\n"); ++n; }
        if (!n) put(out, max, &p, "No est\303\241.\n");
        return 1;
    }
    if ((r = palabra(args, "reemplazar")) || (r = palabra(args, "reemplaza"))) {
        char a[128]; uint32_t k = 0; const char *q = r; while (*q && !(q[0] == ' ' && emp(q + 1, "por "))) { if (k + 1 < sizeof a) a[k++] = *q; ++q; }
        a[k] = 0; if (!*q) { put(out, max, &p, "Uso: hoja reemplazar <texto> por <otro>\n"); return 1; }
        int n = hj_reemplaza(l, h, a, q + 5, 0); if (n) hoja_toca(i);
        put(out, max, &p, "Cambiadas "); putn(out, max, &p, n); put(out, max, &p, " celdas.\n"); return 1;
    }
    if ((r = palabra(args, "inmovilizar")) || (r = palabra(args, "inmoviliza"))) {
        uint32_t f, c; if (palabra(r, "quitar") || palabra(r, "no")) { l->h[h].inmov_f = l->h[h].inmov_c = 0; hoja_toca(i); put(out, max, &p, "Sin inmovilizar.\n"); return 1; }
        if (hj_lee_dir(r, &f, &c) <= 0) { put(out, max, &p, "Uso: hoja inmovilizar B2   (quedan fijas las filas de arriba y las columnas de la izquierda)\n"); return 1; }
        l->h[h].inmov_f = f; l->h[h].inmov_c = c; hoja_toca(i); put(out, max, &p, "Inmovilizado.\n"); return 1;
    }
    if ((r = palabra(args, "ancho"))) {
        uint32_t c; int k = col_de(r, &c); const char *q = sin_blancos(r + k); uint32_t v = 0; while (*q >= '0' && *q <= '9' && v < 10000) v = v * 10 + (uint32_t)(*q++ - '0');
        if (!k || v < 8 || v > 2000) { put(out, max, &p, "Uso: hoja ancho B 150\n"); return 1; }
        hj_pon_ancho(&l->h[h], c, (uint16_t)v); hoja_toca(i); put(out, max, &p, "Hecho.\n"); return 1;
    }
    if ((r = palabra(args, "grafico")) || (r = palabra(args, "gr\303\241fico")) || (r = palabra(args, "graficos")) || (r = palabra(args, "gr\303\241ficos"))) {   /* SOMA_360_HOJA_GRAFICOS */
        static const char *const T[][3] = { { "barras", 0, 0 }, { "columnas", 0, 0 }, { "lineas", "l\303\255neas", "linea" }, { "tarta", "quesitos", "circular" }, { "area", "\303\241rea", 0 }, { "dispersion", "dispersi\303\263n", "xy" } };
        struct hj_hoja *s = &l->h[h];
        if (!*r) {
            if (!s->ngraf) { put(out, max, &p, "No hay gr\303\241ficos en esta hoja. Uso: hoja grafico columnas|barras|lineas|tarta|area|dispersion [A1:D9] [titulo Texto]\n"); return 1; }
            for (uint32_t k = 0; k < s->ngraf; ++k) { const struct hj_grafico *g = &s->graf[k]; char a[16], b[16]; hj_dir(g->r1, g->c1, a); hj_dir(g->r2, g->c2, b);
                putn(out, max, &p, (int64_t)k + 1); put(out, max, &p, ". "); put(out, max, &p, hj_grafico_tipo_nombre(g->tipo)); put(out, max, &p, " de "); put(out, max, &p, a); put(out, max, &p, ":"); put(out, max, &p, b);
                if (g->titulo[0]) { put(out, max, &p, " \302\253"); put(out, max, &p, g->titulo); put(out, max, &p, "\302\273"); } put(out, max, &p, "\n"); }
            return 1;
        }
        { const char *q; if ((q = palabra(r, "borrar")) || (q = palabra(r, "borra"))) { int k = (int)(*q - '1'); if (k < 0 || k >= s->ngraf) { put(out, max, &p, "No hay ese gr\303\241fico (hoja grafico: la lista).\n"); return 1; } hj_grafico_borra(l, h, k); hoja_toca(i); put(out, max, &p, "Gr\303\241fico borrado.\n"); return 1; } }
        int tipo = 1; const char *q = r;
        for (int k = 0; k < 6; ++k) for (int j = 0; j < 3 && T[k][j]; ++j) { const char *z = palabra(r, T[k][j]); if (z) { tipo = k; q = z; } }
        uint32_t f1 = 0, c1 = 0, f2 = s->max_fila ? s->max_fila - 1 : 0, c2 = s->max_col ? s->max_col - 1 : 0;
        int n = hj_lee_rango(q, &f1, &c1, &f2, &c2); if (n > 0) q = sin_blancos(q + n);
        const char *tit = palabra(q, "titulo"); if (!tit) tit = palabra(q, "t\303\255tulo");
        int k = hj_grafico_nuevo(l, h, f1, c1, f2, c2, tipo);
        if (k < 0) { put(out, max, &p, "No se pudo (16 gr\303\241ficos por hoja como mucho).\n"); return 1; }
        struct hj_gforma fm; if (hj_grafico_forma(l, h, &s->graf[k], &fm)) { hj_grafico_borra(l, h, k); put(out, max, &p, "En ese rango no hay datos para un gr\303\241fico.\n"); return 1; }
        if (tit && *tit) cpy(s->graf[k].titulo, sizeof s->graf[k].titulo, tit);
        hoja_toca(i);
        put(out, max, &p, "Gr\303\241fico de "); put(out, max, &p, hj_grafico_tipo_nombre(tipo)); put(out, max, &p, " con "); putn(out, max, &p, fm.nser); put(out, max, &p, fm.nser == 1 ? " serie y " : " series y "); putn(out, max, &p, fm.ncat); put(out, max, &p, " puntos. Se guarda tambi\303\251n en el XLSX.\n");
        return 1;
    }
    { int k = hj_hoja_busca(l, args); if (k < 0 && (r = palabra(args, "hoja"))) k = hj_hoja_busca(l, r);
      if (k >= 0) { l->activa = k; ++g_ver; put(out, max, &p, "Hoja activa: "); put(out, max, &p, l->h[k].nombre); put(out, max, &p, "\n"); return 1; } }
    put(out, max, &p, "No lo he entendido.\n"); put(out, max, &p, AYUDA);
    return 1;
}
