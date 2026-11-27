/* SOMA_358_HOJA: leer ODS. En x86_64 SOLO en el servicio aislado mimesvc (ring 3). */
#include "hoja_util.h"

/* ================================================================ leer */
static uint32_t a_px(const char *s)                              /* "2.5cm", "1in", "72pt", "120px", "25mm" */
{
    int64_t e = 0, d = 0, den = 1; while (*s == ' ') ++s;
    while (*s >= '0' && *s <= '9' && e < 1000000) e = e * 10 + (*s++ - '0');
    if (*s == '.') { ++s; while (*s >= '0' && *s <= '9') { if (den < 100000) { d = d * 10 + (*s - '0'); den *= 10; } ++s; } }
    int64_t v = e * 100000 + d * 100000 / den;                   /* valor * 100000 */
    int64_t px;
    if (empieza(s, "cm")) px = (v * 96 + 127000) / 254000; else if (empieza(s, "mm")) px = (v * 96 + 1270000) / 2540000; else if (empieza(s, "in")) px = (v * 96 + 50000) / 100000;
    else if (empieza(s, "pt")) px = (v * 96 + 3600000) / 7200000; else if (empieza(s, "pc")) px = (v * 96 + 300000) / 600000; else px = (v + 50000) / 100000;
    return px < 0 ? 0 : px > 4000 ? 4000 : (uint32_t)px;
}
static uint32_t color6(const char *s) { if (*s != '#' || sl(s) < 7) return 0; uint32_t v = 0; for (int k = 1; k <= 6; ++k) { char c = s[k]; int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; v = v * 16 + (uint32_t)d; } return v | 0x01000000u; }
#define OMAX 2048
struct onum { char nombre[48]; uint8_t nf, dec, miles; char cod[HJ_NFCOD]; };
struct oest { char nombre[48]; char datos[48]; struct hj_estilo e; uint16_t id; uint32_t px; uint8_t fam; };   /* fam 1 celda, 2 columna, 3 fila */
struct oest_t { struct onum n[OMAX]; uint32_t nn; struct oest s[OMAX]; uint32_t ns; };
static void lee_estilos_ods(struct hj_libro *l, const char *xml, uint32_t n, struct oest_t *E)
{
    struct xr x = { xml, n, 0, { 0 }, 0, 0, 0, 0 }; char a[96];
    struct onum *nu = 0; struct oest *st = 0;
    while (xr_sig(&x)) {
        const char *t = x.tag;
        if (x.cierra) { if (igual(t, "number-style") || igual(t, "percentage-style") || igual(t, "currency-style") || igual(t, "date-style") || igual(t, "time-style") || igual(t, "text-style")) nu = 0; if (igual(t, "style")) st = 0; continue; }
        if (igual(t, "number-style") || igual(t, "percentage-style") || igual(t, "currency-style") || igual(t, "date-style") || igual(t, "time-style") || igual(t, "text-style") || igual(t, "boolean-style")) {
            if (E->nn >= OMAX) continue;
            nu = &E->n[E->nn++]; zero(nu, sizeof *nu); xr_attr(&x, "name", nu->nombre, sizeof nu->nombre);
            nu->nf = igual(t, "number-style") ? NF_NUMERO : igual(t, "percentage-style") ? NF_PORCENTAJE : igual(t, "currency-style") ? NF_MONEDA : igual(t, "date-style") ? NF_FECHA : igual(t, "time-style") ? NF_HORA : igual(t, "text-style") ? NF_TEXTO : NF_GENERAL;
            if (x.vacia) nu = 0;
            continue;
        }
        if (nu && (igual(t, "number") || igual(t, "scientific-number"))) {
            if (xr_attr(&x, "decimal-places", a, sizeof a)) { int64_t d = num(a); nu->dec = (uint8_t)(d >= 0 && d <= 15 ? d : 0); }
            if (xr_attr(&x, "grouping", a, sizeof a) && igual(a, "true")) nu->miles = 1;
            if (igual(t, "scientific-number")) nu->nf = NF_CIENTIFICA;
            /* el codigo de Excel equivalente (asi se conservan los textos de delante y detras) */
            char c[40]; uint32_t k = 0; const char *ent = nu->miles ? "#,##0" : "0"; while (*ent) c[k++] = *ent++;
            if (nu->dec) { c[k++] = '.'; for (int i = 0; i < nu->dec && k + 6 < sizeof c; ++i) c[k++] = '0'; }
            if (nu->nf == NF_CIENTIFICA) { c[k++] = 'E'; c[k++] = '+'; c[k++] = '0'; c[k++] = '0'; }
            c[k] = 0; uint32_t L = sl(nu->cod); cpy(nu->cod + L, HJ_NFCOD - L, c);
            continue;
        }
        if (nu && (igual(t, "text") || igual(t, "currency-symbol")) && !x.vacia) {
            char tx[32]; xr_texto(&x, t, tx, sizeof tx); uint32_t L = sl(nu->cod);
            if (igual(tx, "%")) { if (L + 2 < HJ_NFCOD) { nu->cod[L++] = '%'; nu->cod[L] = 0; } }
            else if (tx[0] && L + sl(tx) + 3 < HJ_NFCOD) { nu->cod[L++] = '"'; cpy(nu->cod + L, HJ_NFCOD - L, tx); L = sl(nu->cod); nu->cod[L++] = '"'; nu->cod[L] = 0; }
            continue;
        }
        if (nu && (nu->nf == NF_FECHA || nu->nf == NF_FECHAHORA || nu->nf == NF_HORA) && !x.cierra) {
            char st[16] = ""; xr_attr(&x, "style", st, sizeof st); int largo = igual(st, "long");
            const char *c = 0;
            if (igual(t, "day")) c = largo ? "dd" : "d";
            else if (igual(t, "month")) { char tt[8] = ""; xr_attr(&x, "textual", tt, sizeof tt); c = igual(tt, "true") ? (largo ? "mmmm" : "mmm") : (largo ? "mm" : "m"); }
            else if (igual(t, "year")) c = largo ? "yyyy" : "yy";
            else if (igual(t, "hours")) { c = largo ? "hh" : "h"; if (nu->nf == NF_FECHA) nu->nf = NF_FECHAHORA; }
            else if (igual(t, "minutes")) { c = largo ? "mm" : "m"; if (nu->nf == NF_FECHA) nu->nf = NF_FECHAHORA; }
            else if (igual(t, "seconds")) c = largo ? "ss" : "s";
            else if (igual(t, "day-of-week")) c = largo ? "dddd" : "ddd";
            else if (igual(t, "am-pm")) c = "AM/PM";
            if (c) { uint32_t L = sl(nu->cod); cpy(nu->cod + L, HJ_NFCOD - L, c); }
            continue;
        }
        if (igual(t, "style")) {
            if (E->ns >= OMAX) continue;
            st = &E->s[E->ns++]; zero(st, sizeof *st); xr_attr(&x, "name", st->nombre, sizeof st->nombre);
            if (xr_attr(&x, "family", a, sizeof a)) st->fam = igual(a, "table-cell") ? 1 : igual(a, "table-column") ? 2 : igual(a, "table-row") ? 3 : 0;
            xr_attr(&x, "data-style-name", st->datos, sizeof st->datos);
            if (x.vacia) st = 0;
            continue;
        }
        if (!st) continue;
        if (igual(t, "text-properties")) {
            if (xr_attr(&x, "font-weight", a, sizeof a) && (igual(a, "bold") || num(a) >= 600)) st->e.negrita = 1;
            if (xr_attr(&x, "font-style", a, sizeof a) && igual(a, "italic")) st->e.cursiva = 1;
            if (xr_attr(&x, "text-underline-style", a, sizeof a) && !igual(a, "none")) st->e.subrayado = 1;
            if (xr_attr(&x, "text-line-through-style", a, sizeof a) && !igual(a, "none")) st->e.tachado = 1;
            if (xr_attr(&x, "color", a, sizeof a)) { uint32_t c = color6(a); if ((c & 0xFFFFFF) != 0) st->e.color = c; }
            if (xr_attr(&x, "font-size", a, sizeof a)) { int64_t v = num(a); if (v > 0 && v < 96 && v != 11 && v != 10) st->e.tam = (uint8_t)v; }
        } else if (igual(t, "table-cell-properties")) {
            if (xr_attr(&x, "background-color", a, sizeof a)) st->e.fondo = color6(a);
            static const char *const B[4] = { "border-top", "border-bottom", "border-left", "border-right" };
            for (int k = 0; k < 4; ++k) if (xr_attr(&x, B[k], a, sizeof a) && !igual(a, "none")) st->e.bordes |= (uint8_t)(1 << k);
            if (xr_attr(&x, "border", a, sizeof a) && !igual(a, "none")) st->e.bordes = 15;
            if (xr_attr(&x, "wrap-option", a, sizeof a) && igual(a, "wrap")) st->e.ajustar = 1;
            if (xr_attr(&x, "vertical-align", a, sizeof a)) st->e.valinear = igual(a, "top") ? 1 : igual(a, "middle") ? 2 : 0;
        } else if (igual(t, "paragraph-properties")) {
            if (xr_attr(&x, "text-align", a, sizeof a)) st->e.alinear = igual(a, "start") || igual(a, "left") ? AL_IZQ : igual(a, "center") ? AL_CENTRO : igual(a, "end") || igual(a, "right") ? AL_DER : 0;
        } else if (igual(t, "table-column-properties")) { if (xr_attr(&x, "column-width", a, sizeof a)) st->px = a_px(a); }
        else if (igual(t, "table-row-properties")) { if (xr_attr(&x, "row-height", a, sizeof a)) st->px = a_px(a); if (xr_attr(&x, "use-optimal-row-height", a, sizeof a) && igual(a, "true")) st->px = 0; }
    }
    for (uint32_t i = 0; i < E->ns; ++i) {
        struct oest *s = &E->s[i]; if (s->fam != 1) continue;
        if (s->datos[0]) for (uint32_t k = 0; k < E->nn; ++k) if (igual(E->n[k].nombre, s->datos)) {
            s->e.nf = E->n[k].nf; s->e.decimales = E->n[k].dec; s->e.miles = E->n[k].miles; if (s->e.nf == NF_MONEDA) s->e.miles = 1;
            if (E->n[k].cod[0] && s->e.nf != NF_TEXTO && s->e.nf != NF_GENERAL) { s->e.nf = NF_CODIGO; cpy(s->e.codigo, HJ_NFCOD, E->n[k].cod); }
            break; }
        s->id = (uint16_t)hj_estilo_id(l, &s->e);
    }
}
static const struct oest *busca_est(const struct oest_t *E, const char *n, int fam) { for (uint32_t i = 0; i < E->ns; ++i) if (E->s[i].fam == fam && igual(E->s[i].nombre, n)) return &E->s[i]; return 0; }
static int lee_fecha_iso(const char *s, f64 *v)
{
    int64_t y = num(s); const char *p = s; if (*p == '-') ++p; while (*p >= '0' && *p <= '9') ++p;
    if (*p != '-') return 0;
    int64_t m = num(++p); while (*p >= '0' && *p <= '9') ++p;
    if (*p != '-') return 0;
    int64_t d = num(++p); while (*p >= '0' && *p <= '9') ++p;
    if (y < 1900 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    f64 r = hj_fecha_serie((int)y, (int)m, (int)d);
    if (*p == 'T') { int64_t hh = num(++p); while (*p >= '0' && *p <= '9') ++p; int64_t mi = 0, ss = 0; if (*p == ':') { mi = num(++p); while (*p >= '0' && *p <= '9') ++p; if (*p == ':') ss = num(++p); } r = f64_add(r, f64_div(Fx(hh * 3600 + mi * 60 + ss), Fx(86400))); }
    *v = r; return 1;
}
static int lee_duracion(const char *s, f64 *v)                    /* PT14H30M00S */
{
    if (s[0] != 'P') return 0;
    int64_t seg = 0, k = 0; const char *p = s + 1; int en_t = 0;
    while (*p) { if (*p == 'T') { en_t = 1; ++p; continue; } k = num(p); while ((*p >= '0' && *p <= '9') || *p == '.') ++p; if (*p == 'D') seg += k * 86400; else if (*p == 'H' && en_t) seg += k * 3600; else if (*p == 'M' && en_t) seg += k * 60; else if (*p == 'S') seg += k; if (*p) ++p; }
    *v = f64_div(Fx(seg), Fx(86400)); return 1;
}
struct ocelda { uint32_t col; uint16_t est; uint8_t tipo; f64 v; char *txt; char *form; };
int hj_lee_ods(struct hj_libro *l, const uint8_t *d, uint32_t n, char *err, uint32_t cap)
{
    uint32_t cn; uint8_t *c = zr_lee(d, n, "content.xml", &cn);
    if (!c) { cpy(err, cap, "No es una hoja de OpenDocument (falta content.xml)."); return -1; }
    struct oest_t *E = (struct oest_t *)hj_alloc((uint32_t)sizeof *E); if (!E) { hj_free(c); cpy(err, cap, "Sin memoria."); return -2; }
    zero(E, (uint32_t)sizeof *E);
    { uint32_t sn; uint8_t *st = zr_lee(d, n, "styles.xml", &sn); if (st) { lee_estilos_ods(l, (const char *)st, sn, E); hj_free(st); } }
    /* los estilos automaticos de content.xml (estan antes del cuerpo) */
    { const char *s = (const char *)c; uint32_t fin = cn; for (uint32_t i = 0; i + 13 < cn; ++i) if (s[i] == '<' && empieza(s + i + 1, "office:body")) { fin = i; break; } lee_estilos_ods(l, s, fin, E); }
    /* primero los nombres de las hojas */
    int nh = 0;
    { struct xr x = { (const char *)c, cn, 0, { 0 }, 0, 0, 0, 0 }; char a[HJ_NOMBRE * 2];
      while (xr_sig(&x)) if (!x.cierra && igual(x.tag, "table") && xr_attr(&x, "name", a, sizeof a)) {
          if (nh == 0) { if (hj_hoja_renombra(l, 0, a)) (void)hj_hoja_renombra(l, 0, "Hoja1"); }
          else if (hj_hoja_nueva(l, a) < 0) { if (hj_hoja_nueva(l, 0) < 0) break; }
          if (++nh >= HJ_HOJAS) break;
      } }
    uint32_t tcap = HJ_TXT_MAX + 64, fcap = HJ_FORM_MAX * 2 + 64;
    char *tx = (char *)hj_alloc(tcap), *fo = (char *)hj_alloc(fcap), *canon = (char *)hj_alloc(fcap), *a = (char *)hj_alloc(fcap);
    struct ocelda *fila = (struct ocelda *)hj_alloc(1024 * (uint32_t)sizeof *fila);
    if (!tx || !fo || !canon || !a || !fila) { if (tx) hj_free(tx); if (fo) hj_free(fo); if (canon) hj_free(canon); if (a) hj_free(a); if (fila) hj_free(fila); hj_free(E); hj_free(c); cpy(err, cap, "Sin memoria."); return -2; }
    struct xr x = { (const char *)c, cn, 0, { 0 }, 0, 0, 0, 0 };
    int h = -1; uint32_t f = 0, col = 0, ncol = 0;
    while (xr_sig(&x) && !l->sin_memoria) {
        const char *t = x.tag;
        if (x.cierra) continue;
        if (igual(t, "table")) { ++h; f = 0; ncol = 0; if (h >= l->nh) break; continue; }
        if (h < 0 || h >= l->nh) {
            if (igual(t, "database-range") && xr_attr(&x, "target-range-address", a, fcap)) { /* fuera de las tablas: se trata abajo */ }
            else continue;
        }
        if (igual(t, "table-column") && h >= 0) {
            int64_t k = xr_attr(&x, "number-columns-repeated", a, 32) ? num(a) : 1; if (k < 1) k = 1;
            const struct oest *e = xr_attr(&x, "style-name", a, 64) ? busca_est(E, a, 2) : 0;
            for (int64_t i = 0; i < k && ncol < HJ_COLS; ++i, ++ncol) if (e && e->px >= 8 && k <= 64) hj_pon_ancho(&l->h[h], ncol, (uint16_t)e->px);   /* las 16 000 de relleno: por defecto */
            continue;
        }
        if (igual(t, "table-row") && h >= 0) {
            int64_t rep = xr_attr(&x, "number-rows-repeated", a, 32) ? num(a) : 1; if (rep < 1) rep = 1;
            const struct oest *re = xr_attr(&x, "style-name", a, 64) ? busca_est(E, a, 3) : 0;
            int oculta = xr_attr(&x, "visibility", a, 32) && (igual(a, "collapse") || igual(a, "filter"));
            uint32_t nfc = 0; col = 0;
            if (!x.vacia) while (xr_sig(&x)) {
                if (x.cierra && igual(x.tag, "table-row")) break;
                if (x.cierra) continue;
                if (!igual(x.tag, "table-cell") && !igual(x.tag, "covered-table-cell")) continue;
                int64_t k = xr_attr(&x, "number-columns-repeated", a, 32) ? num(a) : 1; if (k < 1) k = 1;
                struct ocelda o; zero(&o, sizeof o); o.col = col;
                if (xr_attr(&x, "style-name", a, 64)) { const struct oest *e = busca_est(E, a, 1); if (e) o.est = e->id; }
                char vt[24] = ""; xr_attr(&x, "value-type", vt, sizeof vt);
                int tiene_f = xr_attr(&x, "formula", fo, fcap);
                if (igual(vt, "float") || igual(vt, "percentage") || igual(vt, "currency")) { if (xr_attr(&x, "value", a, 64)) { const char *e2 = 0; f64 v = f64_parse(a, &e2); if (e2 && e2 != a) { o.tipo = HJ_NUM; o.v = v; } } }
                else if (igual(vt, "date")) { if (xr_attr(&x, "date-value", a, 64) && lee_fecha_iso(a, &o.v)) o.tipo = HJ_NUM; }
                else if (igual(vt, "time")) { if (xr_attr(&x, "time-value", a, 64) && lee_duracion(a, &o.v)) o.tipo = HJ_NUM; }
                else if (igual(vt, "boolean")) { if (xr_attr(&x, "boolean-value", a, 16)) { o.tipo = HJ_BOOL; o.v = igual(a, "true") ? Fx(1) : 0; } }
                /* el texto: los <text:p> */
                tx[0] = 0; uint32_t tl = 0; int hay_p = 0;
                if (!x.vacia) while (xr_sig(&x)) {
                    if (x.cierra && (igual(x.tag, "table-cell") || igual(x.tag, "covered-table-cell"))) break;
                    if (x.cierra) continue;
                    if (igual(x.tag, "p")) { if (hay_p && tl + 1 < tcap) { tx[tl++] = '\n'; tx[tl] = 0; } hay_p = 1; tl += xr_texto(&x, "p", tx + tl, tcap - tl); }
                    else if (igual(x.tag, "annotation") && !x.vacia) { while (xr_sig(&x)) if (x.cierra && igual(x.tag, "annotation")) break; }
                }
                if (igual(vt, "string") || (!vt[0] && tx[0])) { o.tipo = HJ_TXT; if (xr_attr(&x, "string-value", a, fcap) && a[0] && !tx[0]) cpy(tx, tcap, a); }
                if (o.tipo == HJ_TXT) o.txt = hj_strdup(tx);
                if (tiene_f && fo[0]) o.form = hj_strdup(fo);
                if (!o.tipo && !o.form && !o.est) { col += (uint32_t)(k > HJ_COLS ? HJ_COLS : k); continue; }   /* vacias */
                if (k > 256) k = 256;
                for (int64_t i = 0; i < k && nfc < 1024 && col < HJ_COLS; ++i, ++col) { fila[nfc] = o; fila[nfc].col = col; if (i) { fila[nfc].txt = o.txt ? hj_strdup(o.txt) : 0; fila[nfc].form = o.form ? hj_strdup(o.form) : 0; } ++nfc; }
            }
            if (rep > 1000 && nfc) rep = 1000;
            for (int64_t r = 0; r < rep && f < HJ_FILAS; ++r, ++f) {
                if (nfc || (re && re->px >= 8 && re->px != HJ_ALTO_DEF) || oculta) {
                    if (re && re->px >= 8 && re->px != HJ_ALTO_DEF && r < 1000) hj_pon_alto(&l->h[h], f, (uint16_t)re->px);
                    if (oculta && r < 1000) hj_oculta_fila(&l->h[h], f, 1);
                }
                for (uint32_t q = 0; q < nfc; ++q) {
                    struct ocelda *o = &fila[q];
                    if (o->tipo == HJ_NUM) (void)hj_pon_num(l, h, f, o->col, o->v);
                    else if (o->tipo == HJ_BOOL) (void)hj_pon_bool(l, h, f, o->col, !f64_eq(o->v, 0));
                    else if (o->tipo == HJ_TXT) { int e = o->form ? hj_error_de_texto(o->txt ? o->txt : "") : 0; if (e) (void)hj_pon_err(l, h, f, o->col, e); else (void)hj_pon_txt(l, h, f, o->col, o->txt ? o->txt : ""); }
                    if (o->form && !hj_form_de_ods(l, h, o->form, canon, fcap) && sl(canon) <= HJ_FORM_MAX) {
                        if (r) { if (!hj_form_desplaza(l, h, canon, (int32_t)r, 0, a, fcap)) cpy(canon, fcap, a); }
                        if (!hj_celda(l, h, f, o->col)) (void)hj_pon_num(l, h, f, o->col, 0);
                        (void)hj_pon_form_cache(l, h, f, o->col, canon);
                    }
                    if (o->est) (void)hj_pon_estilo(l, h, f, o->col, o->est);
                }
                if (!nfc && rep > 1) { f += (uint32_t)(rep - r - 1); break; }
            }
            for (uint32_t q = 0; q < nfc; ++q) { if (fila[q].txt) hj_free(fila[q].txt); if (fila[q].form) hj_free(fila[q].form); }
            continue;
        }
        if (igual(t, "database-range") && xr_attr(&x, "target-range-address", a, fcap) && xr_attr(&x, "display-filter-buttons", tx, 16) && igual(tx, "true")) {
            /* 'Hoja 1'.A1:'Hoja 1'.C9  o  Hoja1.$A$1:.$C$9 */
            char nm[HJ_NOMBRE * 2]; uint32_t k = 0; const char *p = a; if (*p == '$') ++p;
            if (*p == '\'') { ++p; while (*p && !(*p == '\'' && p[1] != '\'')) { if (*p == '\'') ++p; if (k + 1 < sizeof nm) nm[k++] = *p; ++p; } if (*p) ++p; }
            else while (*p && *p != '.') { if (k + 1 < sizeof nm) nm[k++] = *p; ++p; }
            nm[k] = 0; if (*p == '.') ++p;
            char r1[24]; k = 0; while (*p && *p != ':' && k + 1 < sizeof r1) { if (*p != '$') r1[k++] = *p; ++p; } r1[k] = 0;
            if (*p == ':') ++p;
            const char *q = p; for (const char *z = p; *z; ++z) if (*z == '.') q = z + 1;
            char r2[24]; k = 0; while (*q && k + 1 < sizeof r2) { if (*q != '$') r2[k++] = *q; ++q; } r2[k] = 0;
            int hh = hj_hoja_busca(l, nm); uint32_t f1, c1, f2, c2;
            if (hh >= 0 && hj_lee_dir(r1, &f1, &c1) > 0 && hj_lee_dir(r2, &f2, &c2) > 0 && f2 >= f1 && c2 >= c1) { struct hj_hoja *s = &l->h[hh]; s->hay_filtro = 1; s->filtro_r1 = f1; s->filtro_c1 = c1; s->filtro_r2 = f2; s->filtro_c2 = c2; }
            continue;
        }
    }
    hj_free(tx); hj_free(fo); hj_free(canon); hj_free(a); hj_free(fila); hj_free(E); hj_free(c);
    /* settings.xml: hoja activa e inmovilizar */
    uint32_t sn; uint8_t *st = zr_lee(d, n, "settings.xml", &sn);
    if (st) {
        struct xr y = { (const char *)st, sn, 0, { 0 }, 0, 0, 0, 0 }; char nm[96], v[96]; int hoja = -1, prof = 0, hsm = 0, vsm = 0;
        while (xr_sig(&y)) {
            if (igual(y.tag, "config-item-map-entry")) { if (y.cierra) { if (hoja >= 0 && --prof == 0) hoja = -1; continue; } if (hoja >= 0) { ++prof; continue; } if (xr_attr(&y, "name", nm, sizeof nm)) { hoja = hj_hoja_busca(l, nm); prof = hoja >= 0 ? 1 : 0; hsm = vsm = 0; } continue; }
            if (y.cierra || !igual(y.tag, "config-item") || !xr_attr(&y, "name", nm, sizeof nm)) continue;
            xr_texto(&y, "config-item", v, sizeof v);
            if (igual(nm, "ActiveTable") && hoja < 0) { int k = hj_hoja_busca(l, v); if (k >= 0) l->activa = k; }
            if (hoja < 0) continue;
            if (igual(nm, "HorizontalSplitMode")) hsm = (int)num(v); else if (igual(nm, "VerticalSplitMode")) vsm = (int)num(v);
            else if (igual(nm, "HorizontalSplitPosition") && hsm == 2) { int64_t k = num(v); if (k > 0 && k < 1000) l->h[hoja].inmov_c = (uint32_t)k; }
            else if (igual(nm, "VerticalSplitPosition") && vsm == 2) { int64_t k = num(v); if (k > 0 && k < 100000) l->h[hoja].inmov_f = (uint32_t)k; }
        }
        hj_free(st);
    }
    if (l->sin_memoria) { cpy(err, cap, "Sin memoria para todo el libro (demasiadas celdas)."); return -2; }
    return 0;
}
