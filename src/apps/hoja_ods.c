/* SOMA_358_HOJA: escribir ODS (OpenDocument Spreadsheet). Leer: hoja_ods_lee.c. Ver hoja_fmt.h. */
#include "hoja_util.h"

static void hex6(struct gb *b, uint32_t c) { static const char H[] = "0123456789abcdef"; gb_c(b, '#'); for (int k = 20; k >= 0; k -= 4) gb_c(b, H[(c >> k) & 15]); }

/* ================================================================ escribir */
static void estilo_numero(struct gb *b, const struct hj_estilo *e, uint32_t i)
{
    uint8_t nf, dec, mi; hj_tipo_numero(e, &nf, &dec, &mi);
    if (nf == NF_GENERAL) return;
    char nm[16] = "N"; uint32_t p = 1; { char t[12]; int k = 0; uint32_t v = i; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) nm[p++] = t[--k]; nm[p] = 0; }
    switch (nf) {
    case NF_NUMERO: case NF_CIENTIFICA:
        gb_s(b, "<number:number-style style:name=\""); gb_s(b, nm); gb_s(b, "\">");
        if (nf == NF_CIENTIFICA) { gb_s(b, "<number:scientific-number number:decimal-places=\""); gb_n(b, dec); gb_s(b, "\" number:min-integer-digits=\"1\" number:min-exponent-digits=\"2\"/>"); }
        else { gb_s(b, "<number:number number:decimal-places=\""); gb_n(b, dec); gb_s(b, "\" number:min-integer-digits=\"1\""); if (mi) gb_s(b, " number:grouping=\"true\""); gb_s(b, "/>"); }
        gb_s(b, "</number:number-style>"); return;
    case NF_PORCENTAJE:
        gb_s(b, "<number:percentage-style style:name=\""); gb_s(b, nm); gb_s(b, "\"><number:number number:decimal-places=\""); gb_n(b, dec); gb_s(b, "\" number:min-integer-digits=\"1\"/><number:text>%</number:text></number:percentage-style>"); return;
    case NF_MONEDA:
        gb_s(b, "<number:currency-style style:name=\""); gb_s(b, nm); gb_s(b, "\"><number:number number:decimal-places=\""); gb_n(b, dec); gb_s(b, "\" number:min-integer-digits=\"1\" number:grouping=\"true\"/><number:text> </number:text><number:currency-symbol>\342\202\254</number:currency-symbol></number:currency-style>"); return;
    case NF_FECHA: case NF_FECHAHORA:
        gb_s(b, "<number:date-style style:name=\""); gb_s(b, nm); gb_s(b, "\"><number:day number:style=\"long\"/><number:text>/</number:text><number:month number:style=\"long\"/><number:text>/</number:text><number:year number:style=\"long\"/>");
        if (nf == NF_FECHAHORA) gb_s(b, "<number:text> </number:text><number:hours number:style=\"long\"/><number:text>:</number:text><number:minutes number:style=\"long\"/>");
        gb_s(b, "</number:date-style>"); return;
    case NF_HORA:
        gb_s(b, "<number:time-style style:name=\""); gb_s(b, nm); gb_s(b, "\"><number:hours number:style=\"long\"/><number:text>:</number:text><number:minutes number:style=\"long\"/></number:time-style>"); return;
    case NF_TEXTO: gb_s(b, "<number:text-style style:name=\""); gb_s(b, nm); gb_s(b, "\"><number:text-content/></number:text-style>"); return;
    default: return;
    }
}
static void estilo_celda(struct gb *b, const struct hj_estilo *e, uint32_t i)
{
    uint8_t nf, dec, mi; hj_tipo_numero(e, &nf, &dec, &mi);
    gb_s(b, "<style:style style:name=\"ce"); gb_n(b, i); gb_s(b, "\" style:family=\"table-cell\" style:parent-style-name=\"Default\"");
    if (nf != NF_GENERAL) { gb_s(b, " style:data-style-name=\"N"); gb_n(b, i); gb_s(b, "\""); }
    gb_s(b, "><style:table-cell-properties");
    if (e->fondo & 0x01000000u) { gb_s(b, " fo:background-color=\""); hex6(b, e->fondo & 0xFFFFFF); gb_s(b, "\""); }
    if (e->bordes & 1) gb_s(b, " fo:border-top=\"0.06pt solid #000000\"");
    if (e->bordes & 2) gb_s(b, " fo:border-bottom=\"0.06pt solid #000000\"");
    if (e->bordes & 4) gb_s(b, " fo:border-left=\"0.06pt solid #000000\"");
    if (e->bordes & 8) gb_s(b, " fo:border-right=\"0.06pt solid #000000\"");
    if (e->ajustar) gb_s(b, " fo:wrap-option=\"wrap\"");
    if (e->valinear) { gb_s(b, " style:vertical-align=\""); gb_s(b, e->valinear == 1 ? "top" : e->valinear == 2 ? "middle" : "bottom"); gb_s(b, "\""); }
    gb_s(b, "/>");
    if (e->alinear) { gb_s(b, "<style:paragraph-properties fo:text-align=\""); gb_s(b, e->alinear == AL_IZQ ? "start" : e->alinear == AL_CENTRO ? "center" : "end"); gb_s(b, "\"/>"); }
    gb_s(b, "<style:text-properties");
    if (e->negrita) gb_s(b, " fo:font-weight=\"bold\"");
    if (e->cursiva) gb_s(b, " fo:font-style=\"italic\"");
    if (e->subrayado) gb_s(b, " style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\"");
    if (e->tachado) gb_s(b, " style:text-line-through-style=\"solid\"");
    if (e->color & 0x01000000u) { gb_s(b, " fo:color=\""); hex6(b, e->color & 0xFFFFFF); gb_s(b, "\""); }
    if (e->tam) { gb_s(b, " fo:font-size=\""); gb_n(b, e->tam); gb_s(b, "pt\""); }
    gb_s(b, "/></style:style>");
}
static void pulgadas(struct gb *b, uint32_t px)                    /* px (96 por pulgada) -> "1.2500in" */
{
    uint64_t d = (uint64_t)px * 10000 / 96; gb_n(b, (int64_t)(d / 10000)); gb_c(b, '.');
    char t[5]; for (int k = 3; k >= 0; --k) { t[k] = (char)('0' + d % 10); d /= 10; } t[4] = 0; gb_s(b, t); gb_s(b, "in");
}
static void nombre_tabla(struct gb *b, const char *n) { gb_c(b, '\''); for (const char *z = n; *z; ++z) { if (*z == '\'') gb_s(b, "''"); else { char t[2] = { *z, 0 }; gb_esc(b, t); } } gb_c(b, '\''); }
static void celda_ods(struct hj_libro *l, int h, const struct hj_celda *x, struct gb *b, char *tmp, uint32_t tcap)
{
    gb_s(b, "<table:table-cell");
    if (x->estilo) { gb_s(b, " table:style-name=\"ce"); gb_n(b, x->estilo); gb_s(b, "\""); }
    if (x->es_form && x->form && !hj_form_ods(l, h, x->form, tmp, tcap)) { gb_s(b, " table:formula=\"of:="); gb_esc(b, tmp); gb_s(b, "\""); }
    uint8_t nf, dec, mi; hj_tipo_numero(hj_estilo_de(l, x->estilo), &nf, &dec, &mi);
    char vis[512]; hj_texto_ver(l, h, x->fila, x->col, vis, sizeof vis);
    switch (x->t) {
    case HJ_NUM:
        if ((nf == NF_FECHA || nf == NF_FECHAHORA) && !f64_lt(x->n, 0)) {
            int y, m, d, hh, mi2, ss; hj_serie_fecha(x->n, &y, &m, &d, &hh, &mi2, &ss);
            gb_s(b, " office:value-type=\"date\" office:date-value=\""); gb_n(b, y); gb_c(b, '-'); if (m < 10) gb_c(b, '0'); gb_n(b, m); gb_c(b, '-'); if (d < 10) gb_c(b, '0'); gb_n(b, d);
            if (nf == NF_FECHAHORA || hh || mi2 || ss) { gb_c(b, 'T'); if (hh < 10) gb_c(b, '0'); gb_n(b, hh); gb_c(b, ':'); if (mi2 < 10) gb_c(b, '0'); gb_n(b, mi2); gb_c(b, ':'); if (ss < 10) gb_c(b, '0'); gb_n(b, ss); }
            gb_s(b, "\"");
        } else if (nf == NF_HORA && !f64_lt(x->n, 0)) {
            int y, m, d, hh, mi2, ss; hj_serie_fecha(x->n, &y, &m, &d, &hh, &mi2, &ss); int64_t dias = f64_to_i64(f64_floor(x->n));
            gb_s(b, " office:value-type=\"time\" office:time-value=\"PT"); gb_n(b, dias * 24 + hh); gb_s(b, "H"); gb_n(b, mi2); gb_s(b, "M"); gb_n(b, ss); gb_s(b, "S\"");
        } else {
            gb_s(b, nf == NF_PORCENTAJE ? " office:value-type=\"percentage\"" : nf == NF_MONEDA ? " office:value-type=\"currency\" office:currency=\"EUR\"" : " office:value-type=\"float\"");
            gb_s(b, " office:value=\""); gb_f(b, x->n); gb_s(b, "\"");
        }
        break;
    case HJ_BOOL: gb_s(b, " office:value-type=\"boolean\" office:boolean-value=\""); gb_s(b, f64_eq(x->n, 0) ? "false\"" : "true\""); cpy(vis, sizeof vis, f64_eq(x->n, 0) ? "FALSE" : "TRUE"); break;
    case HJ_TXT: gb_s(b, " office:value-type=\"string\""); break;
    case HJ_ERR: if (!x->es_form) gb_s(b, " office:value-type=\"string\""); cpy(vis, sizeof vis, hj_nombre_error(x->err, 1)); break;
    default: break;
    }
    if (x->t == HJ_VACIA && !x->es_form) { gb_s(b, "/>"); return; }
    gb_s(b, ">");
    const char *t = x->t == HJ_TXT && x->txt ? x->txt : vis;
    gb_s(b, "<text:p>");
    for (const char *z = t; *z; ++z) { if (*z == '\n') gb_s(b, "</text:p><text:p>"); else { char q[2] = { *z, 0 }; gb_esc(b, q); } }
    gb_s(b, "</text:p></table:table-cell>");
}
static void hoja_ods(struct hj_libro *l, int h, struct gb *b, char *tmp, uint32_t tcap)
{
    const struct hj_hoja *s = &l->h[h];
    gb_s(b, "<table:table table:name=\""); gb_esc(b, s->nombre); gb_s(b, "\">");
    uint32_t nc = s->max_col ? s->max_col : 1;
    for (uint32_t c = 0; c < nc; ) {
        uint16_t w = hj_ancho(s, c); uint32_t k = 1; while (c + k < nc && hj_ancho(s, c + k) == w) ++k;
        gb_s(b, "<table:table-column table:style-name=\"co"); gb_n(b, w); gb_s(b, "\""); if (k > 1) { gb_s(b, " table:number-columns-repeated=\""); gb_n(b, k); gb_s(b, "\""); }
        gb_s(b, " table:default-cell-style-name=\"Default\"/>"); c += k;
    }
    /* celdas por filas (orden): un indice ordenado */
    uint32_t n = 0; for (int32_t i = 0; i < s->nc; ++i) if (!(s->c[i].t == HJ_VACIA && !s->c[i].es_form && !s->c[i].estilo)) ++n;
    int32_t *v = n ? (int32_t *)hj_alloc(n * 4u) : 0; uint32_t k = 0;
    if (v) for (int32_t i = 0; i < s->nc; ++i) if (!(s->c[i].t == HJ_VACIA && !s->c[i].es_form && !s->c[i].estilo)) v[k++] = i;
    { static const uint32_t G[] = { 88573, 29524, 9841, 3280, 1093, 364, 121, 40, 13, 4, 1 };
      for (int g = 0; g < 11 && v; ++g) for (uint32_t i = G[g]; i < k; ++i) { int32_t t = v[i]; uint64_t kt = ((uint64_t)s->c[t].fila << 14) | s->c[t].col; uint32_t j = i;
          while (j >= G[g]) { int32_t u = v[j - G[g]]; if ((((uint64_t)s->c[u].fila << 14) | s->c[u].col) <= kt) break; v[j] = u; j -= G[g]; } v[j] = t; } }
    uint32_t i = 0, f = 0, ultima = s->max_fila;
    while (f < ultima) {
        int con = i < k && s->c[v[i]].fila == f; uint16_t alto = hj_alto(s, f); int oculta = hj_fila_oculta(s, f);
        if (!con && alto == HJ_ALTO_DEF && !oculta) {                /* filas vacias seguidas */
            uint32_t sig = i < k ? s->c[v[i]].fila : ultima, r = 0;
            while (f + r < sig && hj_alto(s, f + r) == HJ_ALTO_DEF && !hj_fila_oculta(s, f + r)) ++r;
            gb_s(b, "<table:table-row"); if (r > 1) { gb_s(b, " table:number-rows-repeated=\""); gb_n(b, r); gb_s(b, "\""); } gb_s(b, "><table:table-cell/></table:table-row>");
            f += r; continue;
        }
        gb_s(b, "<table:table-row"); if (alto != HJ_ALTO_DEF) { gb_s(b, " table:style-name=\"ro"); gb_n(b, alto); gb_s(b, "\""); }
        if (oculta) gb_s(b, s->hay_filtro ? " table:visibility=\"filter\"" : " table:visibility=\"collapse\"");
        gb_s(b, ">");
        uint32_t c = 0; int alguna = 0;
        while (i < k && s->c[v[i]].fila == f) {
            const struct hj_celda *x = &s->c[v[i++]];
            if (x->col > c) { gb_s(b, "<table:table-cell"); if (x->col - c > 1) { gb_s(b, " table:number-columns-repeated=\""); gb_n(b, x->col - c); gb_s(b, "\""); } gb_s(b, "/>"); }
            celda_ods(l, h, x, b, tmp, tcap); c = (uint32_t)x->col + 1; alguna = 1;
        }
        if (!alguna) gb_s(b, "<table:table-cell/>");
        gb_s(b, "</table:table-row>"); ++f;
    }
    if (!ultima) gb_s(b, "<table:table-row><table:table-cell/></table:table-row>");
    if (v) hj_free(v);
    gb_s(b, "</table:table>");
}
uint8_t *hj_escribe_ods(struct hj_libro *l, uint32_t *nout)
{
    *nout = 0;
    struct zw *z = zw_nuevo(); if (!z) return 0;
    static const char MIME[] = "application/vnd.oasis.opendocument.spreadsheet";
    int mal = zw_pon(z, "mimetype", (const uint8_t *)MIME, sizeof MIME - 1, 0) != 0;     /* el primero y sin comprimir */
    uint32_t tcap = HJ_FORM_MAX * 3 + 256; char *tmp = (char *)hj_alloc(tcap); if (!tmp) mal = 1;
    struct gb b; zero(&b, sizeof b);
    if (!mal) {
        gb_s(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
                 "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
                 "xmlns:number=\"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0\" xmlns:of=\"urn:oasis:names:tc:opendocument:xmlns:of:1.2\" office:version=\"1.2\"><office:automatic-styles>");
        for (uint32_t i = 1; i < l->ne; ++i) estilo_numero(&b, &l->e[i], i);
        for (uint32_t i = 1; i < l->ne; ++i) estilo_celda(&b, &l->e[i], i);
        /* anchos y altos usados */
        uint16_t vistos[512]; uint32_t nv = 0;
        for (int h = 0; h < l->nh; ++h) { uint32_t nc = l->h[h].max_col ? l->h[h].max_col : 1; for (uint32_t c = 0; c < nc; ++c) { uint16_t w = hj_ancho(&l->h[h], c); int ya = 0; for (uint32_t q = 0; q < nv; ++q) if (vistos[q] == w) ya = 1; if (!ya && nv < 512) { vistos[nv++] = w; gb_s(&b, "<style:style style:name=\"co"); gb_n(&b, w); gb_s(&b, "\" style:family=\"table-column\"><style:table-column-properties fo:break-before=\"auto\" style:column-width=\""); pulgadas(&b, w); gb_s(&b, "\"/></style:style>"); } } }
        nv = 0;
        for (int h = 0; h < l->nh; ++h) for (uint32_t q = 0; q < l->h[h].naltos; ++q) { uint16_t a = l->h[h].altos[q].px; int ya = 0; for (uint32_t r = 0; r < nv; ++r) if (vistos[r] == a) ya = 1; if (!ya && nv < 512) { vistos[nv++] = a; gb_s(&b, "<style:style style:name=\"ro"); gb_n(&b, a); gb_s(&b, "\" style:family=\"table-row\"><style:table-row-properties style:row-height=\""); pulgadas(&b, a); gb_s(&b, "\" style:use-optimal-row-height=\"false\" fo:break-before=\"auto\"/></style:style>"); } }
        gb_s(&b, "</office:automatic-styles><office:body><office:spreadsheet>");
        for (int h = 0; h < l->nh; ++h) hoja_ods(l, h, &b, tmp, tcap);
        int hay_f = 0; for (int h = 0; h < l->nh; ++h) hay_f |= l->h[h].hay_filtro;
        if (hay_f) {
            gb_s(&b, "<table:database-ranges>");
            for (int h = 0; h < l->nh; ++h) if (l->h[h].hay_filtro) {
                const struct hj_hoja *s = &l->h[h]; char a[16], c[16]; hj_dir(s->filtro_r1, s->filtro_c1, a); hj_dir(s->filtro_r2, s->filtro_c2, c);
                gb_s(&b, "<table:database-range table:name=\"__Anonymous_Sheet_DB__"); gb_n(&b, h); gb_s(&b, "\" table:target-range-address=\""); nombre_tabla(&b, s->nombre); gb_c(&b, '.'); gb_s(&b, a); gb_c(&b, ':'); nombre_tabla(&b, s->nombre); gb_c(&b, '.'); gb_s(&b, c);
                gb_s(&b, "\" table:display-filter-buttons=\"true\"/>");
            }
            gb_s(&b, "</table:database-ranges>");
        }
        gb_s(&b, "</office:spreadsheet></office:body></office:document-content>");
        if (b.err || zw_pon(z, "content.xml", (const uint8_t *)b.p, b.n, 1)) mal = 1;
        gb_libera(&b);
    }
    if (!mal) {
        static const char STY[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-styles xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" office:version=\"1.2\">"
            "<office:styles><style:style style:name=\"Default\" style:family=\"table-cell\"><style:text-properties fo:font-size=\"11pt\"/></style:style></office:styles></office:document-styles>";
        static const char MAN[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">"
            "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.2\" manifest:media-type=\"application/vnd.oasis.opendocument.spreadsheet\"/>"
            "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/><manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
            "<manifest:file-entry manifest:full-path=\"settings.xml\" manifest:media-type=\"text/xml\"/></manifest:manifest>";
        if (zw_pon(z, "styles.xml", (const uint8_t *)STY, sizeof STY - 1, 1)) mal = 1;
        /* settings.xml: hoja activa e inmovilizar */
        zero(&b, sizeof b);
        gb_s(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-settings xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:config=\"urn:oasis:names:tc:opendocument:xmlns:config:1.0\" office:version=\"1.2\"><office:settings>"
                 "<config:config-item-set config:name=\"ooo:view-settings\"><config:config-item-map-indexed config:name=\"Views\"><config:config-item-map-entry>"
                 "<config:config-item config:name=\"ActiveTable\" config:type=\"string\">");
        gb_esc(&b, l->h[l->activa].nombre); gb_s(&b, "</config:config-item><config:config-item-map-named config:name=\"Tables\">");
        for (int h = 0; h < l->nh; ++h) {
            const struct hj_hoja *s = &l->h[h]; if (!s->inmov_f && !s->inmov_c) continue;
            gb_s(&b, "<config:config-item-map-entry config:name=\""); gb_esc(&b, s->nombre); gb_s(&b, "\">");
            gb_s(&b, "<config:config-item config:name=\"HorizontalSplitMode\" config:type=\"short\">"); gb_n(&b, s->inmov_c ? 2 : 0); gb_s(&b, "</config:config-item>");
            gb_s(&b, "<config:config-item config:name=\"VerticalSplitMode\" config:type=\"short\">"); gb_n(&b, s->inmov_f ? 2 : 0); gb_s(&b, "</config:config-item>");
            gb_s(&b, "<config:config-item config:name=\"HorizontalSplitPosition\" config:type=\"int\">"); gb_n(&b, s->inmov_c); gb_s(&b, "</config:config-item>");
            gb_s(&b, "<config:config-item config:name=\"VerticalSplitPosition\" config:type=\"int\">"); gb_n(&b, s->inmov_f); gb_s(&b, "</config:config-item>");
            gb_s(&b, "<config:config-item config:name=\"ActiveSplitRange\" config:type=\"short\">2</config:config-item>");
            gb_s(&b, "<config:config-item config:name=\"PositionRight\" config:type=\"int\">"); gb_n(&b, s->inmov_c); gb_s(&b, "</config:config-item>");
            gb_s(&b, "<config:config-item config:name=\"PositionBottom\" config:type=\"int\">"); gb_n(&b, s->inmov_f); gb_s(&b, "</config:config-item>");
            gb_s(&b, "</config:config-item-map-entry>");
        }
        gb_s(&b, "</config:config-item-map-named></config:config-item-map-entry></config:config-item-map-indexed></config:config-item-set></office:settings></office:document-settings>");
        if (b.err || zw_pon(z, "settings.xml", (const uint8_t *)b.p, b.n, 1)) mal = 1;
        gb_libera(&b);
        if (zw_pon(z, "META-INF/manifest.xml", (const uint8_t *)MAN, sizeof MAN - 1, 1)) mal = 1;
    }
    if (tmp) hj_free(tmp);
    uint8_t *r = zw_cierra(z, nout);
    if (mal && r) { hj_free(r); r = 0; *nout = 0; }
    return r;
}

