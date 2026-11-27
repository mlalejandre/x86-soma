/* SOMA_360_HOJA_GRAFICOS: la hoja de calculo con el resto de SOMA.
 *   - Contactos y Calendario a una hoja (libro nuevo con su tabla, formato y filtro puesto).
 *   - Enviar el libro por Correo (se guarda una copia XLSX en /tmp y se abre un correo nuevo con ella adjunta: se envia al pulsar Enviar).
 *   - Contexto para el agente: si la mision habla de hojas, tablas, celdas o de un libro abierto, el agente recibe un resumen del libro.
 * Todo lo que toca un libro desde otro hilo (el agente) se hace con el turno de la GUI. */
#include <stdint.h>
#include <stddef.h>
#include "hoja.h"
#include "hoja_fmt.h"
#include "hoja_integra.h"
#include "contactos.h"
#include "contactos_core.h"
#include "agenda.h"
#include "agenda_core.h"
#include "correo.h"
#include "gui.h"
#include "fs.h"
#include "mem.h"

static uint32_t sl(const char *s) { uint32_t n = 0; while (s && s[n]) ++n; return n; }
static void put(char *o, uint32_t cap, uint32_t *p, const char *s) { while (s && *s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void putn(char *o, uint32_t cap, uint32_t *p, int64_t v) { char b[24]; int n = 0; uint64_t u = v < 0 ? (uint64_t)(-v) : (uint64_t)v; if (v < 0) put(o, cap, p, "-"); do { b[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 22); while (n) { char c[2] = { b[--n], 0 }; put(o, cap, p, c); } }
static void cpy(char *d, uint32_t cap, const char *s) { uint32_t p = 0; if (cap) d[0] = 0; put(d, cap, &p, s); }
static char baja(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* ---------------------------------------------------------------- una tabla con titulos en negrita, anchos y filtro */
static void titulos(struct hj_libro *l, const char *const *T, int n)
{
    struct hj_estilo e = *hj_estilo_de(l, 0); e.negrita = 1; e.fondo = 0x01DCFCE7u; e.bordes = 2;
    uint32_t id = hj_estilo_id(l, &e);
    for (int c = 0; c < n; ++c) { (void)hj_pon_txt(l, 0, 0, (uint32_t)c, T[c]); (void)hj_pon_estilo(l, 0, 0, (uint32_t)c, (uint16_t)id); }
    l->h[0].inmov_f = 1;
}
static void remata(struct hj_libro *l, int ncols, uint32_t nfilas, const uint16_t *anchos)
{
    struct hj_hoja *s = &l->h[0];
    for (int c = 0; c < ncols; ++c) hj_pon_ancho(s, (uint32_t)c, anchos[c]);
    if (nfilas) { s->hay_filtro = 1; s->filtro_r1 = 0; s->filtro_c1 = 0; s->filtro_r2 = nfilas; s->filtro_c2 = (uint32_t)ncols - 1; }
    l->version++;
}
int hoja_desde_contactos(char *msg, uint32_t cap)
{
    struct ct_libreta *cl = contactos_libreta();
    if (!cl || !ct_numero(cl)) { cpy(msg, cap, "La libreta de contactos est\303\241 vac\303\255a."); return -1; }
    int k = hoja_nuevo("Contactos"); if (k < 0) { cpy(msg, cap, "Hay 8 libros abiertos: cierra alguno."); return -1; }
    struct hj_libro *l = hoja_libro(k); (void)hj_hoja_renombra(l, 0, "Contactos");
    static const char *const T[] = { "Nombre", "Apellidos", "Tel\303\251fono", "Email", "Empresa", "Cargo", "Cumplea\303\261os", "Grupo", "Ciudad", "Direcci\303\263n" };
    titulos(l, T, 10);
    struct hj_estilo ef = *hj_estilo_de(l, 0); ef.nf = NF_FECHA; uint32_t idf = hj_estilo_id(l, &ef);
    uint32_t f = 1;
    for (uint32_t i = 0; i < CT_MAX; ++i) {
        const struct ct_contacto *c = &cl->c[i];
        if (!c->id || !c->activo) continue;
        (void)hj_pon_txt(l, 0, f, 0, c->nombre[0] ? c->nombre : c->apodo);
        if (c->apellidos[0]) (void)hj_pon_txt(l, 0, f, 1, c->apellidos);
        if (c->tel[0][0]) (void)hj_pon_txt(l, 0, f, 2, c->tel[0]);              /* como texto: los ceros y los + se respetan */
        if (c->email[0][0]) (void)hj_pon_txt(l, 0, f, 3, c->email[0]);
        if (c->empresa[0]) (void)hj_pon_txt(l, 0, f, 4, c->empresa);
        if (c->cargo[0]) (void)hj_pon_txt(l, 0, f, 5, c->cargo);
        if (c->nac_m && c->nac_d) {
            if (c->nac_y > 0) { (void)hj_pon_num(l, 0, f, 6, hj_fecha_serie(c->nac_y, c->nac_m, c->nac_d)); (void)hj_pon_estilo(l, 0, f, 6, (uint16_t)idf); }
            else { char t[16]; uint32_t p = 0; t[0] = 0; putn(t, sizeof t, &p, c->nac_d); put(t, sizeof t, &p, "/"); putn(t, sizeof t, &p, c->nac_m); (void)hj_pon_txt(l, 0, f, 6, t); }
        }
        if (c->grupo[0]) (void)hj_pon_txt(l, 0, f, 7, c->grupo);
        if (c->ciudad[0]) (void)hj_pon_txt(l, 0, f, 8, c->ciudad);
        if (c->calle[0]) (void)hj_pon_txt(l, 0, f, 9, c->calle);
        ++f;
        if (l->sin_memoria) break;
    }
    static const uint16_t A[] = { 120, 150, 120, 200, 140, 120, 100, 100, 110, 200 };
    remata(l, 10, f - 1, A);
    (void)hj_ordena(l, 0, 1, 0, f - 1, 9, 0, 0, 1, 0);
    hoja_toca(k); hoja_pon_actual(k);
    uint32_t p = 0; if (cap) msg[0] = 0; put(msg, cap, &p, "Libro con "); putn(msg, cap, &p, f - 1); put(msg, cap, &p, f - 1 == 1 ? " contacto: " : " contactos: "); put(msg, cap, &p, hoja_ruta(k));
    return k;
}
int hoja_desde_agenda(const char *tramo, char *msg, uint32_t cap)
{
    struct ag_cal *c = agenda_cal(); if (!c) { cpy(msg, cap, "Sin memoria."); return -1; }
    int64_t ahora = agenda_ahora(), d0, d1;
    if (!tramo || !tramo[0] || ag_parsea_tramo(tramo, ahora, &d0, &d1)) { int y, m, d; ag_civil(ag_dia_de(ahora), &y, &m, &d); d0 = ag_dias(y, m, 1); d1 = d0 + 92; }
    static struct ag_oc oc[1000];
    int n = ag_agenda_rango(c, d0 * 1440, d1 * 1440, oc, 1000);
    if (n <= 0) { cpy(msg, cap, "No hay citas en esas fechas."); return -1; }
    int k = hoja_nuevo("Calendario"); if (k < 0) { cpy(msg, cap, "Hay 8 libros abiertos: cierra alguno."); return -1; }
    struct hj_libro *l = hoja_libro(k); (void)hj_hoja_renombra(l, 0, "Calendario");
    static const char *const T[] = { "Fecha", "Empieza", "Termina", "Cita", "Lugar", "Nota" };
    titulos(l, T, 6);
    struct hj_estilo ef = *hj_estilo_de(l, 0); ef.nf = NF_FECHA; uint32_t idf = hj_estilo_id(l, &ef);
    struct hj_estilo eh = *hj_estilo_de(l, 0); eh.nf = NF_HORA; uint32_t idh = hj_estilo_id(l, &eh);
    uint32_t f = 1;
    for (int i = 0; i < n; ++i) {
        const struct ag_ev *e = &c->ev[oc[i].idx];
        int y, mo, d, h, mi; ag_desde_min(oc[i].ini, &y, &mo, &d, &h, &mi);
        (void)hj_pon_num(l, 0, f, 0, hj_fecha_serie(y, mo, d)); (void)hj_pon_estilo(l, 0, f, 0, (uint16_t)idf);
        if (!e->todo_dia) {
            (void)hj_pon_num(l, 0, f, 1, f64_div(f64_from_i64((int64_t)h * 60 + mi), f64_from_i64(1440))); (void)hj_pon_estilo(l, 0, f, 1, (uint16_t)idh);
            if (e->dur) { int64_t fin = (int64_t)h * 60 + mi + e->dur; (void)hj_pon_num(l, 0, f, 2, f64_div(f64_from_i64(fin % 1440), f64_from_i64(1440))); (void)hj_pon_estilo(l, 0, f, 2, (uint16_t)idh); }
        } else (void)hj_pon_txt(l, 0, f, 1, "todo el d\303\255a");
        (void)hj_pon_txt(l, 0, f, 3, e->titulo);
        if (e->lugar[0]) (void)hj_pon_txt(l, 0, f, 4, e->lugar);
        if (e->nota[0] && !(e->nota[0] == 'c' && e->nota[1] == 'o' && e->nota[2] == 'n' && e->nota[3] == 't')) (void)hj_pon_txt(l, 0, f, 5, e->nota);   /* no las marcas "contacto:ID" */
        ++f;
        if (l->sin_memoria) break;
    }
    static const uint16_t A[] = { 100, 80, 80, 260, 160, 220 };
    remata(l, 6, f - 1, A);
    hoja_toca(k); hoja_pon_actual(k);
    uint32_t p = 0; if (cap) msg[0] = 0; put(msg, cap, &p, "Libro con "); putn(msg, cap, &p, f - 1); put(msg, cap, &p, f - 1 == 1 ? " cita: " : " citas: "); put(msg, cap, &p, hoja_ruta(k));
    return k;
}

/* ---------------------------------------------------------------- enviar por correo */
int hoja_envia_correo(int i, const char *para, char *msg, uint32_t cap)
{
    struct hj_libro *l = hoja_libro(i); if (cap) msg[0] = 0;
    if (!l) { cpy(msg, cap, "No hay ning\303\272n libro abierto."); return -1; }
    const char *r = hoja_ruta(i), *b = r; for (const char *z = r; *z; ++z) if (*z == '/') b = z + 1;
    char nom[128]; cpy(nom, sizeof nom, b);
    { uint32_t n = sl(nom); while (n && nom[n - 1] != '.') --n; if (n) nom[n - 1] = 0; }
    for (char *z = nom; *z; ++z) if (*z == '/' || *z == '\\' || (uint8_t)*z < 32) *z = '_';
    if (!nom[0]) cpy(nom, sizeof nom, "Libro");
    char ruta[200]; uint32_t p = 0; ruta[0] = 0; put(ruta, sizeof ruta, &p, "/tmp/"); put(ruta, sizeof ruta, &p, nom); put(ruta, sizeof ruta, &p, ".xlsx");
    (void)hj_recalcula(l);
    uint32_t n = 0; uint8_t *d = hj_escribe_xlsx(l, &n);                 /* la copia que se envia: el libro de SOMA no cambia de ruta */
    if (!d || !n) { if (d) kfree(d); cpy(msg, cap, "Sin memoria para preparar el archivo."); return -1; }
    if (n > CORREO_ADJ_MAX) { kfree(d); cpy(msg, cap, "El libro ocupa m\303\241s de 20 MB: demasiado para un correo."); return -1; }
    int w = vfs_write(ruta, (const char *)d, n); kfree(d);
    if (w < 0) { cpy(msg, cap, "No se pudo preparar el archivo en /tmp."); return -1; }
    char asunto[160]; p = 0; asunto[0] = 0; put(asunto, sizeof asunto, &p, nom);
    if (correo_redactar_adjunto(ruta, para, asunto, msg, cap)) return -1;
    p = 0; put(msg, cap, &p, "Correo preparado con "); put(msg, cap, &p, nom); put(msg, cap, &p, ".xlsx adjunto: rev\303\255salo y pulsa Enviar.");
    return 0;
}

/* ---------------------------------------------------------------- contexto para el agente */
static int contiene(const char *s, const char *w)
{
    uint32_t n = sl(w);
    for (; *s; ++s) { uint32_t i = 0; while (i < n && s[i] && baja(s[i]) == w[i]) ++i; if (i == n) return 1; }
    return 0;
}
static int habla_de_hojas(const char *m)
{
    static const char *const P[] = { "hoja", "excel", "xlsx", ".ods", ".csv", "celda", "columna", "fila ", "filas", "tabla", "libro", "formula", "f\303\263rmula", "grafico", "gr\303\241fico", "suma de", "promedio", "calc", 0 };
    for (int i = 0; P[i]; ++i) if (contiene(m, P[i])) return 1;
    for (int i = 0; i < HOJA_LIBROS; ++i) if (hoja_libro(i)) {            /* el nombre de un libro abierto */
        const char *r = hoja_ruta(i), *b = r; for (const char *z = r; *z; ++z) if (*z == '/') b = z + 1;
        char nb[64]; cpy(nb, sizeof nb, b); for (char *z = nb; *z; ++z) { if (*z == '.') { *z = 0; break; } *z = baja(*z); }
        if (sl(nb) >= 4 && contiene(m, nb)) return 1;
    }
    return 0;
}
void hoja_contexto(const char *mision, char *dst, uint32_t max, uint32_t *pos)
{
    if (!mision || !dst || *pos + 600 >= max || !habla_de_hojas(mision)) return;
    int tok = gui_turn_begin();
    int i = hoja_actual(); struct hj_libro *l = hoja_libro(i);
    if (!l) { put(dst, max, pos, "\n[HOJA DE C\303\201LCULO: no hay ning\303\272n libro abierto. hoja nueva NOMBRE | hoja abrir /usuario/archivo.xlsx | hoja contactos | hoja agenda]"); gui_turn_end(tok); return; }
    int h = l->activa; const struct hj_hoja *s = &l->h[h];
    put(dst, max, pos, "\n[HOJA DE C\303\201LCULO abierta: "); put(dst, max, pos, hoja_ruta(i)); if (hoja_cambiado(i)) put(dst, max, pos, " (sin guardar)");
    put(dst, max, pos, ". Hojas:"); for (int k = 0; k < l->nh; ++k) { put(dst, max, pos, k ? ", " : " "); put(dst, max, pos, l->h[k].nombre); }
    put(dst, max, pos, ". Activa: "); put(dst, max, pos, s->nombre);
    if (s->max_fila && s->max_col) { char a[16]; hj_dir(s->max_fila - 1, s->max_col - 1, a); put(dst, max, pos, ", usado A1:"); put(dst, max, pos, a); }
    put(dst, max, pos, ". \303\223rdenes: hoja ver A1:F20 | hoja pon B5 =SUMA(B1:B4) | hoja calcula =PROMEDIO(B2:B9) | hoja grafico columnas A1:D5 | hoja ordenar | hoja filtrar | hoja guardar ARCHIVO.xlsx | hoja enviar EMAIL"
                         " (f\303\263rmulas en castellano con ';' y coma decimal). Primeras filas (celda=valor):\n");
    static char b[256];
    uint32_t mf = s->max_fila < 15 ? s->max_fila : 15, mc = s->max_col < 10 ? s->max_col : 10;
    for (uint32_t f = 0; f < mf && *pos + 300 < max; ++f) {
        int algo = 0;
        for (uint32_t c = 0; c < mc && *pos + 300 < max; ++c) {
            const struct hj_celda *x = hj_celda_c(l, h, f, c); if (!x || x->t == HJ_VACIA) continue;
            char d[16]; hj_dir(f, c, d); hj_texto_ver(l, h, f, c, b, 60);
            for (char *z = b; *z; ++z) if (*z == '\n' || *z == '|' || *z == ']') *z = ' ';
            put(dst, max, pos, algo ? " | " : ""); put(dst, max, pos, d); put(dst, max, pos, "="); put(dst, max, pos, b);
            if (x->es_form) { hj_texto_edicion(l, h, f, c, b, 80); for (char *z = b; *z; ++z) if (*z == '\n' || *z == ']') *z = ' '; put(dst, max, pos, " ("); put(dst, max, pos, b); put(dst, max, pos, ")"); }
            algo = 1;
        }
        if (algo) put(dst, max, pos, "\n");
    }
    if (s->max_fila > mf) { put(dst, max, pos, "(\342\200\246 "); putn(dst, max, pos, s->max_fila - mf); put(dst, max, pos, " filas m\303\241s: hoja ver para leerlas)\n"); }
    put(dst, max, pos, "]");
    gui_turn_end(tok);
}
