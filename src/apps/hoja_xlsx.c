/* SOMA_358_HOJA: escribir XLSX (Office Open XML SpreadsheetML). Leer: hoja_xlsx_lee.c. Ver hoja_fmt.h. */
#include "hoja_util.h"

static void dec2(struct gb *b, int64_t c100) { gb_n(b, c100 / 100); gb_c(b, '.'); gb_c(b, (char)('0' + c100 / 10 % 10)); gb_c(b, (char)('0' + c100 % 10)); }

/* ================================================================ escribir */
struct ss { char **s; uint32_t n, cap; int32_t *t; uint32_t nt; };
static uint32_t hs(const char *s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }
static int ss_id(struct ss *x, const char *s)
{
    if (x->n * 2 >= x->nt) {
        uint32_t nt = x->nt ? x->nt * 2 : 1024; int32_t *t = (int32_t *)hj_alloc(nt * 4u); if (!t) return -1;
        for (uint32_t i = 0; i < nt; ++i) t[i] = -1;
        for (uint32_t i = 0; i < x->n; ++i) { uint32_t k = hs(x->s[i]) & (nt - 1); while (t[k] >= 0) k = (k + 1) & (nt - 1); t[k] = (int32_t)i; }
        if (x->t) hj_free(x->t);
        x->t = t;
        x->nt = nt;
    }
    uint32_t k = hs(s) & (x->nt - 1);
    while (x->t[k] >= 0) { if (igual(x->s[x->t[k]], s)) return x->t[k]; k = (k + 1) & (x->nt - 1); }
    if (x->n >= x->cap) { uint32_t nc = x->cap ? x->cap * 2 : 256; char **v = (char **)hj_alloc(nc * (uint32_t)sizeof(char *)); if (!v) return -1; for (uint32_t i = 0; i < x->n; ++i) v[i] = x->s[i]; if (x->s) hj_free(x->s); x->s = v; x->cap = nc; }
    x->s[x->n] = (char *)s; x->t[k] = (int32_t)x->n; return (int)x->n++;
}
static void color_xml(struct gb *b, const char *tag, uint32_t c)
{
    static const char H[] = "0123456789ABCDEF";
    gb_s(b, "<"); gb_s(b, tag); gb_s(b, " rgb=\"FF");
    for (int k = 20; k >= 0; k -= 4) gb_c(b, H[(c >> k) & 15]);
    gb_s(b, "\"/>");
}
static void numfmt_de(const struct hj_estilo *e, int *id, char *cod, uint32_t cap)
{
    cod[0] = 0; *id = 0;
    char d[24]; uint32_t k = 0; if (e->decimales) { d[k++] = '.'; for (int i = 0; i < e->decimales && k + 1 < sizeof d; ++i) d[k++] = '0'; } d[k] = 0;
    switch (e->nf) {
    case NF_NUMERO:
        if (!e->decimales) *id = e->miles ? 3 : 1; else if (e->decimales == 2) *id = e->miles ? 4 : 2;
        else { cpy(cod, cap, e->miles ? "#,##0" : "0"); uint32_t p = sl(cod); cpy(cod + p, cap - p, d); }
        return;
    case NF_MONEDA: { cpy(cod, cap, "#,##0"); uint32_t p = sl(cod); cpy(cod + p, cap - p, d); p = sl(cod); cpy(cod + p, cap - p, "\\ \"\342\202\254\""); return; }
    case NF_PORCENTAJE: if (!e->decimales) *id = 9; else if (e->decimales == 2) *id = 10; else { cpy(cod, cap, "0"); uint32_t p = sl(cod); cpy(cod + p, cap - p, d); p = sl(cod); cpy(cod + p, cap - p, "%"); } return;
    case NF_FECHA: cpy(cod, cap, "dd/mm/yyyy"); return;
    case NF_HORA: cpy(cod, cap, "hh:mm"); return;
    case NF_FECHAHORA: cpy(cod, cap, "dd/mm/yyyy hh:mm"); return;
    case NF_TEXTO: *id = 49; return;
    case NF_CIENTIFICA: if (e->decimales == 2) *id = 11; else { cpy(cod, cap, "0"); uint32_t p = sl(cod); cpy(cod + p, cap - p, d); p = sl(cod); cpy(cod + p, cap - p, "E+00"); } return;
    case NF_CODIGO: cpy(cod, cap, e->codigo); if (!cod[0]) *id = 0; return;
    default: return;
    }
}
static uint32_t cuenta_x(const struct hj_hoja *s) { uint32_t n = 0; for (int32_t i = 0; i < s->nc; ++i) if (!(s->c[i].t == HJ_VACIA && !s->c[i].es_form && !s->c[i].estilo)) ++n; return n; }
static void ordena_idx(const struct hj_hoja *s, int32_t *v, uint32_t n)
{
    static const uint32_t G[] = { 88573, 29524, 9841, 3280, 1093, 364, 121, 40, 13, 4, 1 };
    for (int g = 0; g < 11; ++g) for (uint32_t i = G[g]; i < n; ++i) {
        int32_t t = v[i]; uint64_t kt = ((uint64_t)s->c[t].fila << 14) | s->c[t].col; uint32_t j = i;
        while (j >= G[g]) { int32_t u = v[j - G[g]]; uint64_t ku = ((uint64_t)s->c[u].fila << 14) | s->c[u].col; if (ku <= kt) break; v[j] = u; j -= G[g]; }
        v[j] = t;
    }
}
static void hoja_xml(struct hj_libro *l, int h, struct ss *strs, struct gb *b, char *tmp, uint32_t tcap)
{
    const struct hj_hoja *s = &l->h[h];
    gb_s(b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");
    gb_s(b, "<sheetViews><sheetView workbookViewId=\"0\""); if (h == l->activa) gb_s(b, " tabSelected=\"1\""); gb_s(b, ">");
    if (s->inmov_f || s->inmov_c) {
        char d[16]; hj_dir(s->inmov_f, s->inmov_c, d);
        gb_s(b, "<pane"); if (s->inmov_c) { gb_s(b, " xSplit=\""); gb_n(b, s->inmov_c); gb_s(b, "\""); } if (s->inmov_f) { gb_s(b, " ySplit=\""); gb_n(b, s->inmov_f); gb_s(b, "\""); }
        gb_s(b, " topLeftCell=\""); gb_s(b, d); gb_s(b, "\" activePane=\""); gb_s(b, s->inmov_f && s->inmov_c ? "bottomRight" : s->inmov_f ? "bottomLeft" : "topRight"); gb_s(b, "\" state=\"frozen\"/>");
    }
    gb_s(b, "</sheetView></sheetViews><sheetFormatPr defaultRowHeight=\"15\"/>");
    if (s->nanchos) {
        gb_s(b, "<cols>");
        for (uint32_t k = 0; k < s->nanchos; ++k) { int64_t px = s->anchos[k].px; int64_t w = (px > 5 ? px - 5 : 1) * 100 / 7; gb_s(b, "<col min=\""); gb_n(b, (int64_t)s->anchos[k].i + 1); gb_s(b, "\" max=\""); gb_n(b, (int64_t)s->anchos[k].i + 1); gb_s(b, "\" width=\""); dec2(b, w); gb_s(b, "\" customWidth=\"1\"/>"); }
        gb_s(b, "</cols>");
    }
    gb_s(b, "<sheetData>");
    uint32_t n = cuenta_x(s), k = 0; int32_t *v = n ? (int32_t *)hj_alloc(n * 4u) : 0;
    if (v) { for (int32_t i = 0; i < s->nc; ++i) if (!(s->c[i].t == HJ_VACIA && !s->c[i].es_form && !s->c[i].estilo)) v[k++] = i; ordena_idx(s, v, k); }
    /* filas: las de las celdas, las de alto propio y las ocultas */
    uint32_t ia = 0, io = 0, i = 0;
    for (;;) {
        uint32_t f = 0xFFFFFFFFu;
        if (i < k && s->c[v[i]].fila < f) f = s->c[v[i]].fila;
        if (ia < s->naltos && s->altos[ia].i < f) f = s->altos[ia].i;
        while (io < s->noculta && !hj_fila_oculta(s, io)) ++io;
        if (io < s->noculta && io < f) f = io;
        if (f == 0xFFFFFFFFu) break;
        gb_s(b, "<row r=\""); gb_n(b, (int64_t)f + 1); gb_s(b, "\"");
        if (ia < s->naltos && s->altos[ia].i == f) { gb_s(b, " ht=\""); dec2(b, (int64_t)s->altos[ia].px * 75); gb_s(b, "\" customHeight=\"1\""); ++ia; }
        if (hj_fila_oculta(s, f)) gb_s(b, " hidden=\"1\"");
        if (io == f) ++io;
        gb_s(b, ">");
        while (i < k && s->c[v[i]].fila == f) {
            const struct hj_celda *x = &s->c[v[i++]]; char d[16]; hj_dir(x->fila, x->col, d);
            gb_s(b, "<c r=\""); gb_s(b, d); gb_s(b, "\""); if (x->estilo) { gb_s(b, " s=\""); gb_n(b, x->estilo); gb_s(b, "\""); }
            if (x->t == HJ_VACIA && !x->es_form) { gb_s(b, "/>"); continue; }
            int sid = -1;
            if (!x->es_form && x->t == HJ_TXT) sid = ss_id(strs, x->txt ? x->txt : "");
            if (x->t == HJ_TXT) gb_s(b, x->es_form ? " t=\"str\"" : sid >= 0 ? " t=\"s\"" : " t=\"inlineStr\"");
            else if (x->t == HJ_BOOL) gb_s(b, " t=\"b\""); else if (x->t == HJ_ERR) gb_s(b, " t=\"e\"");
            gb_s(b, ">");
            if (x->es_form && x->form) { if (hj_form_xlsx(l, h, x->form, tmp, tcap)) cpy(tmp, tcap, x->form); gb_s(b, "<f>"); gb_esc(b, tmp); gb_s(b, "</f>"); }
            switch (x->t) {
            case HJ_NUM: gb_s(b, "<v>"); gb_f(b, x->n); gb_s(b, "</v>"); break;
            case HJ_BOOL: gb_s(b, f64_eq(x->n, 0) ? "<v>0</v>" : "<v>1</v>"); break;
            case HJ_ERR: gb_s(b, "<v>"); gb_esc(b, hj_nombre_error(x->err, 1)); gb_s(b, "</v>"); break;
            case HJ_TXT: if (sid >= 0) { gb_s(b, "<v>"); gb_n(b, sid); gb_s(b, "</v>"); } else if (x->es_form) { gb_s(b, "<v>"); gb_esc(b, x->txt); gb_s(b, "</v>"); } else { gb_s(b, "<is><t xml:space=\"preserve\">"); gb_esc(b, x->txt); gb_s(b, "</t></is>"); } break;
            default: break;
            }
            gb_s(b, "</c>");
        }
        gb_s(b, "</row>");
    }
    if (v) hj_free(v);
    gb_s(b, "</sheetData>");
    if (s->hay_filtro) { char a[16], c[16]; hj_dir(s->filtro_r1, s->filtro_c1, a); hj_dir(s->filtro_r2, s->filtro_c2, c); gb_s(b, "<autoFilter ref=\""); gb_s(b, a); gb_s(b, ":"); gb_s(b, c); gb_s(b, "\"/>"); }
    gb_s(b, "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>");
    if (s->ngraf) gb_s(b, "<drawing r:id=\"rId1\"/>");                  /* SOMA_360_HOJA_GRAFICOS */
    gb_s(b, "</worksheet>");
}

/* ================================================================ SOMA_360_HOJA_GRAFICOS: graficos (DrawingML) */
static void ref_xlsx(struct gb *b, const char *hoja, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2)
{
    gb_c(b, '\'');
    for (const char *q = hoja; *q; ++q) { if (*q == '\'') gb_s(b, "''"); else { char t[2] = { *q, 0 }; gb_esc(b, t); } }
    char a[8]; gb_s(b, "'!$"); hj_nombre_col(c1, a); gb_s(b, a); gb_s(b, "$"); gb_n(b, (int64_t)f1 + 1);
    if (f2 != f1 || c2 != c1) { gb_s(b, ":$"); hj_nombre_col(c2, a); gb_s(b, a); gb_s(b, "$"); gb_n(b, (int64_t)f2 + 1); }
}
static const char *const PAL_XLSX[10] = { "4472C4", "ED7D31", "A5A5A5", "FFC000", "5B9BD5", "70AD47", "264478", "9E480E", "636363", "997300" };
static void relleno_xml(struct gb *b, uint32_t k) { gb_s(b, "<a:solidFill><a:srgbClr val=\""); gb_s(b, PAL_XLSX[k % 10]); gb_s(b, "\"/></a:solidFill>"); }
static void serie_xml(struct gb *b, const struct hj_libro *l, const struct hj_gforma *fm, uint32_t k, int tipo)
{
    const char *hn = l->h[fm->h].nombre; uint32_t f1, c1, f2, c2;
    gb_s(b, "<c:ser><c:idx val=\""); gb_n(b, k); gb_s(b, "\"/><c:order val=\""); gb_n(b, k); gb_s(b, "\"/>");
    if (fm->titulos) { hj_gforma_nombre(fm, k, &f1, &c1); gb_s(b, "<c:tx><c:strRef><c:f>"); ref_xlsx(b, hn, f1, c1, f1, c1); gb_s(b, "</c:f></c:strRef></c:tx>"); }
    /* colores explicitos: sin ellos algunos programas pintan las series vacias */
    if (tipo == 5) { gb_s(b, "<c:spPr><a:ln w=\"25400\"><a:noFill/></a:ln></c:spPr><c:marker><c:symbol val=\"circle\"/><c:size val=\"6\"/><c:spPr>"); relleno_xml(b, k); gb_s(b, "</c:spPr></c:marker>"); }
    else if (tipo == 2) { gb_s(b, "<c:spPr><a:ln w=\"28575\" cap=\"rnd\">"); relleno_xml(b, k); gb_s(b, "<a:round/></a:ln></c:spPr><c:marker><c:symbol val=\"circle\"/><c:size val=\"5\"/><c:spPr>"); relleno_xml(b, k); gb_s(b, "</c:spPr></c:marker>"); }
    else if (tipo == 3) {
        gb_s(b, "<c:spPr><a:ln w=\"12700\"><a:solidFill><a:srgbClr val=\"FFFFFF\"/></a:solidFill></a:ln></c:spPr>");
        for (uint32_t i = 0; i < fm->ncat && i < 200; ++i) { gb_s(b, "<c:dPt><c:idx val=\""); gb_n(b, i); gb_s(b, "\"/><c:bubble3D val=\"0\"/><c:spPr>"); relleno_xml(b, i); gb_s(b, "</c:spPr></c:dPt>"); }
    } else { gb_s(b, "<c:spPr>"); relleno_xml(b, k); gb_s(b, "</c:spPr>"); if (tipo <= 1) gb_s(b, "<c:invertIfNegative val=\"0\"/>"); }
    if (fm->categorias) {
        hj_gforma_cat(fm, 0, &f1, &c1); hj_gforma_cat(fm, fm->ncat - 1, &f2, &c2);
        gb_s(b, tipo == 5 ? "<c:xVal><c:numRef><c:f>" : "<c:cat><c:strRef><c:f>"); ref_xlsx(b, hn, f1, c1, f2, c2); gb_s(b, tipo == 5 ? "</c:f></c:numRef></c:xVal>" : "</c:f></c:strRef></c:cat>");
    }
    hj_gforma_valor(fm, k, 0, &f1, &c1); hj_gforma_valor(fm, k, fm->ncat - 1, &f2, &c2);
    gb_s(b, tipo == 5 ? "<c:yVal><c:numRef><c:f>" : "<c:val><c:numRef><c:f>"); ref_xlsx(b, hn, f1, c1, f2, c2); gb_s(b, tipo == 5 ? "</c:f></c:numRef></c:yVal>" : "</c:f></c:numRef></c:val>");
    if (tipo == 2 || tipo == 5) gb_s(b, "<c:smooth val=\"0\"/>");
    gb_s(b, "</c:ser>");
}
static void grafico_xml(struct gb *b, const struct hj_libro *l, int h, const struct hj_grafico *g)
{
    struct hj_gforma fm; int ok = !hj_grafico_forma(l, h, g, &fm);
    int t = g->tipo <= 5 ? g->tipo : 1;
    gb_s(b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><c:roundedCorners val=\"0\"/><c:chart>");
    if (g->titulo[0]) { gb_s(b, "<c:title><c:tx><c:rich><a:bodyPr/><a:p><a:r><a:t>"); gb_esc(b, g->titulo); gb_s(b, "</a:t></a:r></a:p></c:rich></c:tx><c:overlay val=\"0\"/></c:title><c:autoTitleDeleted val=\"0\"/>"); }
    else gb_s(b, "<c:autoTitleDeleted val=\"1\"/>");
    gb_s(b, "<c:plotArea><c:layout/>");
    static const char *const T[6] = { "barChart", "barChart", "lineChart", "pieChart", "areaChart", "scatterChart" };
    gb_s(b, "<c:"); gb_s(b, T[t]); gb_s(b, ">");
    if (t <= 1) { gb_s(b, t == 0 ? "<c:barDir val=\"bar\"/>" : "<c:barDir val=\"col\"/>"); gb_s(b, "<c:grouping val=\"clustered\"/>"); }
    else if (t == 2 || t == 4) gb_s(b, "<c:grouping val=\"standard\"/>");
    else if (t == 5) gb_s(b, "<c:scatterStyle val=\"lineMarker\"/>");
    gb_s(b, t == 3 ? "<c:varyColors val=\"1\"/>" : "<c:varyColors val=\"0\"/>");
    if (ok) { uint32_t n = t == 3 ? 1 : fm.nser; if (n > 64) n = 64; for (uint32_t k = 0; k < n; ++k) serie_xml(b, l, &fm, k, t); }
    if (t <= 1) gb_s(b, "<c:gapWidth val=\"150\"/>");
    if (t == 2) gb_s(b, "<c:marker val=\"1\"/>");
    if (t == 3) gb_s(b, "<c:firstSliceAng val=\"0\"/>");
    else gb_s(b, "<c:axId val=\"50010\"/><c:axId val=\"50020\"/>");
    gb_s(b, "</c:"); gb_s(b, T[t]); gb_s(b, ">");
    if (t != 3) {
        const char *p1 = t == 0 ? "l" : "b", *p2 = t == 0 ? "b" : "l";
        gb_s(b, t == 5 ? "<c:valAx>" : "<c:catAx>");
        gb_s(b, "<c:axId val=\"50010\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\""); gb_s(b, p1);
        gb_s(b, "\"/><c:numFmt formatCode=\"General\" sourceLinked=\"1\"/><c:majorTickMark val=\"out\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"50020\"/><c:crosses val=\"autoZero\"/>");
        gb_s(b, t == 5 ? "<c:crossBetween val=\"midCat\"/></c:valAx>" : "<c:auto val=\"1\"/><c:lblAlgn val=\"ctr\"/><c:lblOffset val=\"100\"/><c:noMultiLvlLbl val=\"0\"/></c:catAx>");
        gb_s(b, "<c:valAx><c:axId val=\"50020\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete val=\"0\"/><c:axPos val=\""); gb_s(b, p2);
        gb_s(b, "\"/><c:majorGridlines/><c:numFmt formatCode=\"General\" sourceLinked=\"1\"/><c:majorTickMark val=\"out\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"50010\"/><c:crosses val=\"autoZero\"/><c:crossBetween val=\"");
        gb_s(b, t == 4 ? "midCat" : "between"); gb_s(b, "\"/></c:valAx>");
    }
    gb_s(b, "</c:plotArea>");
    if (g->leyenda) gb_s(b, "<c:legend><c:legendPos val=\"r\"/><c:overlay val=\"0\"/></c:legend>");
    gb_s(b, "<c:plotVisOnly val=\"1\"/><c:dispBlanksAs val=\"gap\"/></c:chart></c:chartSpace>");
}
/* posicion de una esquina: celda y desplazamiento (EMU, 9525 por px) a partir de la celda (f,c) y dx,dy px */
static void esquina(const struct hj_hoja *s, uint32_t f, uint32_t c, uint32_t dx, uint32_t dy, uint32_t *tf, uint32_t *tc, uint32_t *ox, uint32_t *oy)
{
    while (c + 1 < HJ_COLS && dx >= hj_ancho(s, c)) { dx -= hj_ancho(s, c); ++c; }
    while (f + 1 < HJ_FILAS && dy >= hj_alto(s, f)) { dy -= hj_alto(s, f); ++f; }
    *tc = c; *tf = f; *ox = dx * 9525u; *oy = dy * 9525u;
}
static void dibujo_xml(struct gb *b, const struct hj_libro *l, int h)
{
    const struct hj_hoja *s = &l->h[h];
    gb_s(b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">");
    for (uint32_t k = 0; k < s->ngraf; ++k) {
        const struct hj_grafico *g = &s->graf[k]; uint32_t tf, tc, ox, oy;
        gb_s(b, "<xdr:twoCellAnchor editAs=\"oneCell\"><xdr:from><xdr:col>"); gb_n(b, g->col); gb_s(b, "</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>"); gb_n(b, g->fila); gb_s(b, "</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from>");
        esquina(s, g->fila, g->col, g->ancho, g->alto, &tf, &tc, &ox, &oy);
        gb_s(b, "<xdr:to><xdr:col>"); gb_n(b, tc); gb_s(b, "</xdr:col><xdr:colOff>"); gb_n(b, ox); gb_s(b, "</xdr:colOff><xdr:row>"); gb_n(b, tf); gb_s(b, "</xdr:row><xdr:rowOff>"); gb_n(b, oy); gb_s(b, "</xdr:rowOff></xdr:to>");
        gb_s(b, "<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr><xdr:cNvPr id=\""); gb_n(b, k + 2); gb_s(b, "\" name=\"Gr\303\241fico "); gb_n(b, k + 1);
        gb_s(b, "\"/><xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr><xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm><a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">");
        gb_s(b, "<c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" r:id=\"rId"); gb_n(b, k + 1);
        gb_s(b, "\"/></a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/></xdr:twoCellAnchor>");
    }
    gb_s(b, "</xdr:wsDr>");
}
static int pon_gb(struct zw *z, const char *nom, struct gb *b) { int r = b->err || zw_pon(z, nom, (const uint8_t *)b->p, b->n, 1); gb_libera(b); return r; }
static void nombre_n(char *o, uint32_t cap, const char *a, int n, const char *z) { uint32_t p = 0; o[0] = 0; cpy(o, cap, a); p = sl(o); char t[12]; int k = 0; do { t[k++] = (char)('0' + n % 10); n /= 10; } while (n && k < 10); while (k && p + 1 < cap) o[p++] = t[--k]; o[p] = 0; cpy(o + p, cap - p, z); }
/* dibujos y graficos de todas las hojas; el texto de [Content_Types] que les toca va a tipos */
static int graficos_xlsx(struct zw *z, struct hj_libro *l, struct gb *tipos)
{
    int nd = 0, ng = 0; char nom[96];
    for (int h = 0; h < l->nh; ++h) {
        const struct hj_hoja *s = &l->h[h]; if (!s->ngraf) continue;
        ++nd; struct gb b;
        zero(&b, sizeof b);
        gb_s(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" Target=\"../drawings/drawing");
        gb_n(&b, nd); gb_s(&b, ".xml\"/></Relationships>");
        nombre_n(nom, sizeof nom, "xl/worksheets/_rels/sheet", h + 1, ".xml.rels"); if (pon_gb(z, nom, &b)) return -1;
        zero(&b, sizeof b); dibujo_xml(&b, l, h);
        nombre_n(nom, sizeof nom, "xl/drawings/drawing", nd, ".xml"); if (pon_gb(z, nom, &b)) return -1;
        gb_s(tipos, "<Override PartName=\"/xl/drawings/drawing"); gb_n(tipos, nd); gb_s(tipos, ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>");
        zero(&b, sizeof b);
        gb_s(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
        for (uint32_t k = 0; k < s->ngraf; ++k) { gb_s(&b, "<Relationship Id=\"rId"); gb_n(&b, k + 1); gb_s(&b, "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart\" Target=\"../charts/chart"); gb_n(&b, ng + (int)k + 1); gb_s(&b, ".xml\"/>"); }
        gb_s(&b, "</Relationships>");
        nombre_n(nom, sizeof nom, "xl/drawings/_rels/drawing", nd, ".xml.rels"); if (pon_gb(z, nom, &b)) return -1;
        for (uint32_t k = 0; k < s->ngraf; ++k) {
            ++ng; zero(&b, sizeof b); grafico_xml(&b, l, h, &s->graf[k]);
            nombre_n(nom, sizeof nom, "xl/charts/chart", ng, ".xml"); if (pon_gb(z, nom, &b)) return -1;
            gb_s(tipos, "<Override PartName=\"/xl/charts/chart"); gb_n(tipos, ng); gb_s(tipos, ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"/>");
        }
    }
    return 0;
}
static void estilos_xml(const struct hj_libro *l, struct gb *b)
{
    gb_s(b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">");
    /* formatos propios: 164 en adelante (sin repetir) */
    int *ids = (int *)hj_alloc(l->ne * (uint32_t)sizeof(int)); char cod[HJ_NFCOD * 2];
    struct gb nf; zero(&nf, sizeof nf); int propios = 0;
    char (*vistos)[HJ_NFCOD * 2] = (char (*)[HJ_NFCOD * 2])hj_alloc(l->ne * HJ_NFCOD * 2u);
    for (uint32_t i = 0; i < l->ne && ids && vistos; ++i) {
        int id; numfmt_de(&l->e[i], &id, cod, sizeof cod);
        if (cod[0]) { int k = -1; for (int j = 0; j < propios; ++j) if (igual(vistos[j], cod)) k = j;
            if (k < 0) { k = propios++; cpy(vistos[k], sizeof vistos[k], cod); gb_s(&nf, "<numFmt numFmtId=\""); gb_n(&nf, 164 + k); gb_s(&nf, "\" formatCode=\""); gb_esc(&nf, cod); gb_s(&nf, "\"/>"); }
            id = 164 + k; }
        ids[i] = id;
    }
    if (propios) { gb_s(b, "<numFmts count=\""); gb_n(b, propios); gb_s(b, "\">"); if (nf.p) gb_s(b, nf.p); gb_s(b, "</numFmts>"); }
    gb_libera(&nf); if (vistos) hj_free(vistos);
    gb_s(b, "<fonts count=\""); gb_n(b, l->ne); gb_s(b, "\">");
    for (uint32_t i = 0; i < l->ne; ++i) {
        const struct hj_estilo *e = &l->e[i]; gb_s(b, "<font>");
        if (e->negrita) gb_s(b, "<b/>");
        if (e->cursiva) gb_s(b, "<i/>");
        if (e->tachado) gb_s(b, "<strike/>");
        if (e->subrayado) gb_s(b, "<u/>");
        gb_s(b, "<sz val=\""); gb_n(b, e->tam ? e->tam : 11); gb_s(b, "\"/>");
        if (e->color & 0x01000000u) color_xml(b, "color", e->color & 0xFFFFFF); else gb_s(b, "<color theme=\"1\"/>");
        gb_s(b, "<name val=\"Calibri\"/><family val=\"2\"/></font>");
    }
    gb_s(b, "</fonts><fills count=\""); gb_n(b, (int64_t)l->ne + 2); gb_s(b, "\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>");
    for (uint32_t i = 0; i < l->ne; ++i) {
        const struct hj_estilo *e = &l->e[i];
        if (e->fondo & 0x01000000u) { gb_s(b, "<fill><patternFill patternType=\"solid\">"); color_xml(b, "fgColor", e->fondo & 0xFFFFFF); gb_s(b, "<bgColor indexed=\"64\"/></patternFill></fill>"); }
        else gb_s(b, "<fill><patternFill patternType=\"none\"/></fill>");
    }
    gb_s(b, "</fills><borders count=\""); gb_n(b, l->ne); gb_s(b, "\">");
    for (uint32_t i = 0; i < l->ne; ++i) {
        uint8_t bo = l->e[i].bordes; gb_s(b, "<border>");
        static const char *const L[4] = { "left", "right", "top", "bottom" }; static const uint8_t M[4] = { 4, 8, 1, 2 };
        for (int k = 0; k < 4; ++k) { gb_s(b, "<"); gb_s(b, L[k]); if (bo & M[k]) { gb_s(b, " style=\"thin\"><color auto=\"1\"/></"); gb_s(b, L[k]); gb_s(b, ">"); } else gb_s(b, "/>"); }
        gb_s(b, "<diagonal/></border>");
    }
    gb_s(b, "</borders><cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs><cellXfs count=\""); gb_n(b, l->ne); gb_s(b, "\">");
    for (uint32_t i = 0; i < l->ne; ++i) {
        const struct hj_estilo *e = &l->e[i];
        gb_s(b, "<xf numFmtId=\""); gb_n(b, ids ? ids[i] : 0); gb_s(b, "\" fontId=\""); gb_n(b, i); gb_s(b, "\" fillId=\""); gb_n(b, (int64_t)i + 2); gb_s(b, "\" borderId=\""); gb_n(b, i);
        gb_s(b, "\" xfId=\"0\""); if (i) gb_s(b, " applyNumberFormat=\"1\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"");
        if (e->alinear || e->valinear || e->ajustar) {
            gb_s(b, "><alignment");
            if (e->alinear) { gb_s(b, " horizontal=\""); gb_s(b, e->alinear == AL_IZQ ? "left" : e->alinear == AL_CENTRO ? "center" : "right"); gb_s(b, "\""); }
            if (e->valinear) { gb_s(b, " vertical=\""); gb_s(b, e->valinear == 1 ? "top" : e->valinear == 2 ? "center" : "bottom"); gb_s(b, "\""); }
            if (e->ajustar) gb_s(b, " wrapText=\"1\"");
            gb_s(b, "/></xf>");
        } else gb_s(b, "/>");
    }
    gb_s(b, "</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>");
    if (ids) hj_free(ids);
}
uint8_t *hj_escribe_xlsx(struct hj_libro *l, uint32_t *nout)
{
    *nout = 0;
    struct zw *z = zw_nuevo(); if (!z) return 0;
    struct ss strs; zero(&strs, sizeof strs);
    uint32_t tcap = HJ_FORM_MAX * 2 + 256; char *tmp = (char *)hj_alloc(tcap);
    struct gb b; zero(&b, sizeof b); int mal = !tmp;
    char nom[64];
    /* las hojas primero: asi se sabe que textos hay */
    for (int h = 0; h < l->nh && !mal; ++h) {
        zero(&b, sizeof b); hoja_xml(l, h, &strs, &b, tmp, tcap);
        uint32_t p = 0; nom[0] = 0; cpy(nom, sizeof nom, "xl/worksheets/sheet"); p = sl(nom); { char t[8]; int k = 0, v = h + 1; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k && p + 1 < sizeof nom) nom[p++] = t[--k]; nom[p] = 0; } cpy(nom + p, sizeof nom - p, ".xml");
        if (b.err || zw_pon(z, nom, (const uint8_t *)b.p, b.n, 1)) mal = 1;
        gb_libera(&b);
    }
    if (!mal) {
        zero(&b, sizeof b);
        gb_s(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\""); gb_n(&b, strs.n); gb_s(&b, "\" uniqueCount=\""); gb_n(&b, strs.n); gb_s(&b, "\">");
        for (uint32_t i = 0; i < strs.n; ++i) { gb_s(&b, "<si><t xml:space=\"preserve\">"); gb_esc(&b, strs.s[i]); gb_s(&b, "</t></si>"); }
        gb_s(&b, "</sst>");
        if (b.err || zw_pon(z, "xl/sharedStrings.xml", (const uint8_t *)b.p, b.n, 1)) mal = 1;
        gb_libera(&b);
    }
    if (!mal) { zero(&b, sizeof b); estilos_xml(l, &b); if (b.err || zw_pon(z, "xl/styles.xml", (const uint8_t *)b.p, b.n, 1)) mal = 1; gb_libera(&b); }
    if (!mal) {
        zero(&b, sizeof b);
        gb_s(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");
        gb_s(&b, "<workbookPr/><bookViews><workbookView activeTab=\""); gb_n(&b, l->activa); gb_s(&b, "\"/></bookViews><sheets>");
        for (int h = 0; h < l->nh; ++h) { gb_s(&b, "<sheet name=\""); gb_esc(&b, l->h[h].nombre); gb_s(&b, "\" sheetId=\""); gb_n(&b, h + 1); gb_s(&b, "\" r:id=\"rId"); gb_n(&b, h + 1); gb_s(&b, "\"/>"); }
        gb_s(&b, "</sheets>");
        int hay_f = 0; for (int h = 0; h < l->nh; ++h) hay_f |= l->h[h].hay_filtro;
        if (hay_f) {
            gb_s(&b, "<definedNames>");
            for (int h = 0; h < l->nh; ++h) if (l->h[h].hay_filtro) {
                char a[16], c[16]; const struct hj_hoja *s = &l->h[h];
                gb_s(&b, "<definedName name=\"_xlnm._FilterDatabase\" localSheetId=\""); gb_n(&b, h); gb_s(&b, "\" hidden=\"1\">'");
                for (const char *q = s->nombre; *q; ++q) { if (*q == '\'') gb_s(&b, "''"); else { char t[2] = { *q, 0 }; gb_esc(&b, t); } }
                gb_s(&b, "'!$"); hj_nombre_col(s->filtro_c1, a); gb_s(&b, a); gb_s(&b, "$"); gb_n(&b, (int64_t)s->filtro_r1 + 1); gb_s(&b, ":$"); hj_nombre_col(s->filtro_c2, c); gb_s(&b, c); gb_s(&b, "$"); gb_n(&b, (int64_t)s->filtro_r2 + 1);
                gb_s(&b, "</definedName>");
            }
            gb_s(&b, "</definedNames>");
        }
        gb_s(&b, "<calcPr calcId=\"191029\" fullCalcOnLoad=\"1\"/></workbook>");
        if (b.err || zw_pon(z, "xl/workbook.xml", (const uint8_t *)b.p, b.n, 1)) mal = 1;
        gb_libera(&b);
    }
    if (!mal) {
        zero(&b, sizeof b);
        gb_s(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
        for (int h = 0; h < l->nh; ++h) { gb_s(&b, "<Relationship Id=\"rId"); gb_n(&b, h + 1); gb_s(&b, "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet"); gb_n(&b, h + 1); gb_s(&b, ".xml\"/>"); }
        gb_s(&b, "<Relationship Id=\"rId"); gb_n(&b, l->nh + 1); gb_s(&b, "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>");
        gb_s(&b, "<Relationship Id=\"rId"); gb_n(&b, l->nh + 2); gb_s(&b, "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/></Relationships>");
        if (b.err || zw_pon(z, "xl/_rels/workbook.xml.rels", (const uint8_t *)b.p, b.n, 1)) mal = 1;
        gb_libera(&b);
        struct gb tipos; zero(&tipos, sizeof tipos);
        if (!mal && graficos_xlsx(z, l, &tipos)) mal = 1;                /* SOMA_360_HOJA_GRAFICOS */
        zero(&b, sizeof b);
        gb_s(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                 "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                 "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>");
        for (int h = 0; h < l->nh; ++h) { gb_s(&b, "<Override PartName=\"/xl/worksheets/sheet"); gb_n(&b, h + 1); gb_s(&b, ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"); }
        if (tipos.p && tipos.n) gb_s(&b, tipos.p);
        gb_libera(&tipos);
        gb_s(&b, "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
                 "<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>"
                 "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
                 "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/></Types>");
        if (b.err || zw_pon(z, "[Content_Types].xml", (const uint8_t *)b.p, b.n, 1)) mal = 1;
        gb_libera(&b);
        static const char RELS[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
            "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
            "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/></Relationships>";
        static const char CORE[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:creator>SOMA</dc:creator></cp:coreProperties>";
        static const char APP[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\"><Application>SOMA Hoja de calculo</Application></Properties>";
        if (zw_pon(z, "_rels/.rels", (const uint8_t *)RELS, sizeof RELS - 1, 1) || zw_pon(z, "docProps/core.xml", (const uint8_t *)CORE, sizeof CORE - 1, 1) || zw_pon(z, "docProps/app.xml", (const uint8_t *)APP, sizeof APP - 1, 1)) mal = 1;
    }
    if (strs.s) hj_free(strs.s);
    if (strs.t) hj_free(strs.t);
    if (tmp) hj_free(tmp);
    uint8_t *r = zw_cierra(z, nout);
    if (mal && r) { hj_free(r); r = 0; *nout = 0; }
    return r;
}

