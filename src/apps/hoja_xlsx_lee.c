/* SOMA_358_HOJA: leer XLSX y el reparto por formato (hj_lee_archivo). En x86_64 SOLO en el servicio aislado mimesvc (ring 3). */
#include "hoja_util.h"

/* "12.75" (anchos y altos de XLSX) -> centesimas */
static int64_t centesimas(const char *s)
{
    int64_t e = 0, d = 0; int nd = 0;
    while (*s == ' ') ++s;
    while (*s >= '0' && *s <= '9' && e < 100000000) e = e * 10 + (*s++ - '0');
    if (*s == '.') { ++s; while (*s >= '0' && *s <= '9') { if (nd < 2) { d = d * 10 + (*s - '0'); ++nd; } ++s; } }
    while (nd < 2) { d *= 10; ++nd; }
    return e * 100 + d;
}

/* ================================================================ leer */
static const char *fmt_interno(int id)                            /* formatos de numero de serie de Excel */
{
    switch (id) {
    case 1: return "0"; case 2: return "0.00"; case 3: return "#,##0"; case 4: return "#,##0.00"; case 9: return "0%"; case 10: return "0.00%";
    case 11: return "0.00E+00"; case 12: return "# ?/?"; case 13: return "# ?\?/??"; case 14: return "dd/mm/yyyy"; case 15: return "d-mmm-yy"; case 16: return "d-mmm";
    case 17: return "mmm-yy"; case 18: return "h:mm AM/PM"; case 19: return "h:mm:ss AM/PM"; case 20: return "h:mm"; case 21: return "h:mm:ss"; case 22: return "dd/mm/yyyy h:mm";
    case 37: return "#,##0 ;(#,##0)"; case 38: return "#,##0 ;[Red](#,##0)"; case 39: return "#,##0.00;(#,##0.00)"; case 40: return "#,##0.00;[Red](#,##0.00)";
    case 44: return "#,##0.00 \"\342\202\254\""; case 45: return "mm:ss"; case 46: return "[h]:mm:ss"; case 47: return "mm:ss.0"; case 48: return "##0.0E+0"; case 49: return "@";
    default: return 0;
    }
}
struct xf { uint16_t nf, font, fill, border; uint8_t al, val, wrap; };
struct fnt { uint8_t b, i, u, s, tam; uint32_t color; };
static uint32_t rgb(const char *s) { uint32_t v = 0; int k = 0; for (; *s && k < 8; ++s, ++k) { char c = *s; int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; v = v * 16 + (uint32_t)d; } return (k == 8 ? v & 0xFFFFFF : v) | 0x01000000u; }
#define XMAX 4096
struct est_x { struct xf xf[XMAX]; uint32_t nxf; struct fnt fn[XMAX]; uint32_t nfn; uint32_t fill[XMAX]; uint32_t nfill; uint8_t bord[XMAX]; uint32_t nbord; struct { int id; char cod[HJ_NFCOD]; } nf[512]; uint32_t nnf; uint16_t mapa[XMAX]; };
static void lee_estilos(struct hj_libro *l, const char *xml, uint32_t n, struct est_x *E)
{
    struct xr x = { xml, n, 0, { 0 }, 0, 0, 0, 0 }; char a[256]; int sec = 0, en_fill = 0, en_border = 0;  /* sec: 1 fonts, 2 fills, 3 borders, 4 cellXfs */
    struct fnt *f = 0;
    while (xr_sig(&x)) {
        const char *t = x.tag;
        if (igual(t, "fonts")) { sec = x.cierra ? 0 : 1; continue; }
        if (igual(t, "fills")) { sec = x.cierra ? 0 : 2; continue; }
        if (igual(t, "borders")) { sec = x.cierra ? 0 : 3; continue; }
        if (igual(t, "cellXfs")) { sec = x.cierra ? 0 : 4; continue; }
        if (igual(t, "cellStyleXfs") || igual(t, "dxfs")) { sec = x.cierra ? 0 : 9; continue; }
        if (x.cierra) { if (igual(t, "fill")) en_fill = 0; if (igual(t, "border")) en_border = 0; continue; }
        if (igual(t, "numFmt") && E->nnf < 512) { xr_attr(&x, "numFmtId", a, sizeof a); E->nf[E->nnf].id = (int)num(a); xr_attr(&x, "formatCode", E->nf[E->nnf].cod, HJ_NFCOD); E->nnf++; continue; }
        if (sec == 1) {
            if (igual(t, "font")) { if (E->nfn < XMAX) { f = &E->fn[E->nfn++]; zero(f, sizeof *f); } else f = 0; continue; }
            if (!f) continue;
            if (igual(t, "b")) { f->b = !xr_attr(&x, "val", a, sizeof a) || es_si(a); } else if (igual(t, "i")) { f->i = !xr_attr(&x, "val", a, sizeof a) || es_si(a); }
            else if (igual(t, "u")) { f->u = !xr_attr(&x, "val", a, sizeof a) || !igual(a, "none"); } else if (igual(t, "strike")) { f->s = !xr_attr(&x, "val", a, sizeof a) || es_si(a); }
            else if (igual(t, "sz") && xr_attr(&x, "val", a, sizeof a)) { int64_t v = num(a); f->tam = (uint8_t)(v > 0 && v < 96 ? v : 0); if (f->tam == 11) f->tam = 0; }
            else if (igual(t, "color") && xr_attr(&x, "rgb", a, sizeof a)) { uint32_t c = rgb(a); if ((c & 0xFFFFFF) != 0) f->color = c; }
            continue;
        }
        if (sec == 2) {
            if (igual(t, "fill")) { en_fill = 1; if (E->nfill < XMAX) E->fill[E->nfill++] = 0; continue; }
            if (en_fill && igual(t, "fgColor") && xr_attr(&x, "rgb", a, sizeof a) && E->nfill) E->fill[E->nfill - 1] = rgb(a);
            continue;
        }
        if (sec == 3) {
            if (igual(t, "border")) { en_border = 1; if (E->nbord < XMAX) E->bord[E->nbord++] = 0; continue; }
            if (en_border && E->nbord && xr_attr(&x, "style", a, sizeof a) && !igual(a, "none")) {
                if (igual(t, "top")) E->bord[E->nbord - 1] |= 1; else if (igual(t, "bottom")) E->bord[E->nbord - 1] |= 2; else if (igual(t, "left") || igual(t, "start")) E->bord[E->nbord - 1] |= 4; else if (igual(t, "right") || igual(t, "end")) E->bord[E->nbord - 1] |= 8;
            }
            continue;
        }
        if (sec == 4) {
            if (igual(t, "xf")) { if (E->nxf >= XMAX) continue; struct xf *q = &E->xf[E->nxf++]; zero(q, sizeof *q);
                if (xr_attr(&x, "numFmtId", a, sizeof a)) q->nf = (uint16_t)num(a);
                if (xr_attr(&x, "fontId", a, sizeof a)) q->font = (uint16_t)num(a);
                if (xr_attr(&x, "fillId", a, sizeof a)) q->fill = (uint16_t)num(a);
                if (xr_attr(&x, "borderId", a, sizeof a)) q->border = (uint16_t)num(a);
                continue;
                }
            if (igual(t, "alignment") && E->nxf) { struct xf *q = &E->xf[E->nxf - 1];
                if (xr_attr(&x, "horizontal", a, sizeof a)) q->al = igual(a, "left") ? AL_IZQ : igual(a, "center") || igual(a, "centerContinuous") ? AL_CENTRO : igual(a, "right") ? AL_DER : 0;
                if (xr_attr(&x, "vertical", a, sizeof a)) q->val = igual(a, "top") ? 1 : igual(a, "center") ? 2 : 0;
                if (xr_attr(&x, "wrapText", a, sizeof a)) q->wrap = es_si(a); }
        }
    }
    for (uint32_t i = 0; i < E->nxf; ++i) {
        struct xf *q = &E->xf[i]; struct hj_estilo e; zero(&e, sizeof e);
        if (q->font < E->nfn) { struct fnt *g = &E->fn[q->font]; e.negrita = g->b; e.cursiva = g->i; e.subrayado = g->u; e.tachado = g->s; e.tam = g->tam; e.color = g->color; }
        if (q->fill < E->nfill) e.fondo = E->fill[q->fill];
        if (q->border < E->nbord) e.bordes = E->bord[q->border];
        e.alinear = q->al; e.valinear = q->val; e.ajustar = q->wrap;
        if (q->nf) {
            const char *c = 0; for (uint32_t k = 0; k < E->nnf; ++k) if (E->nf[k].id == q->nf) c = E->nf[k].cod;
            if (!c) c = fmt_interno(q->nf);
            if (c && !igual(c, "General")) { e.nf = NF_CODIGO; cpy(e.codigo, HJ_NFCOD, c); }
        }
        E->mapa[i] = (uint16_t)hj_estilo_id(l, &e);
    }
}
struct compartida { int si; uint32_t f, c; char *form; };
static int lee_hoja(struct hj_libro *l, int h, const char *xml, uint32_t n, char **sst, uint32_t nsst, const struct est_x *E)
{
    struct xr x = { xml, n, 0, { 0 }, 0, 0, 0, 0 };
    char a[64], t[16], fa[64], si[16], refa[64];
    uint32_t cap = HJ_TXT_MAX + 64, fcap = HJ_FORM_MAX + 64;
    char *v = (char *)hj_alloc(cap), *fm = (char *)hj_alloc(fcap), *canon = (char *)hj_alloc(fcap * 2);
    struct compartida *cmp = (struct compartida *)hj_alloc(1024 * (uint32_t)sizeof *cmp); uint32_t ncmp = 0;
    if (!v || !fm || !canon || !cmp) { if (v) hj_free(v); if (fm) hj_free(fm); if (canon) hj_free(canon); if (cmp) hj_free(cmp); return -2; }
    uint32_t fila = 0, col = 0; struct hj_hoja *s = &l->h[h];
    while (xr_sig(&x)) {
        if (x.cierra) continue;
        if (igual(x.tag, "pane")) {
            if (xr_attr(&x, "state", a, sizeof a) && (igual(a, "frozen") || igual(a, "frozenSplit"))) {
                if (xr_attr(&x, "xSplit", a, sizeof a)) { int64_t k = num(a); if (k > 0 && k < 1000) s->inmov_c = (uint32_t)k; }
                if (xr_attr(&x, "ySplit", a, sizeof a)) { int64_t k = num(a); if (k > 0 && k < 100000) s->inmov_f = (uint32_t)k; }
            }
            continue;
        }
        if (igual(x.tag, "col")) {
            int64_t mn = xr_attr(&x, "min", a, sizeof a) ? num(a) : 0, mx = xr_attr(&x, "max", a, sizeof a) ? num(a) : 0;
            if (mn < 1 || mx < mn || mx > HJ_COLS || mx - mn > 64) continue;
            int64_t px = 0; if (xr_attr(&x, "width", a, sizeof a)) px = centesimas(a) * 7 / 100 + 5;
            if (xr_attr(&x, "hidden", a, sizeof a) && es_si(a)) px = 8;
            if (px >= 8 && px <= 2000) for (int64_t c = mn; c <= mx; ++c) hj_pon_ancho(s, (uint32_t)c - 1, (uint16_t)px);
            continue;
        }
        if (igual(x.tag, "row")) {
            int64_t r = xr_attr(&x, "r", a, sizeof a) ? num(a) : (int64_t)fila + 2;
            if (r < 1 || r > HJ_FILAS) continue;
            fila = (uint32_t)r - 1; col = 0;
            if (xr_attr(&x, "ht", a, sizeof a) && xr_attr(&x, "customHeight", t, sizeof t) && es_si(t)) { int64_t px = centesimas(a) * 4 / 300; if (px >= 8 && px < 1000) hj_pon_alto(s, fila, (uint16_t)px); }
            if (xr_attr(&x, "hidden", a, sizeof a) && es_si(a)) hj_oculta_fila(s, fila, 1);
            continue;
        }
        if (igual(x.tag, "autoFilter") && xr_attr(&x, "ref", a, sizeof a)) {
            uint32_t f1, c1, f2, c2; if (hj_lee_rango(a, &f1, &c1, &f2, &c2) > 0) { s->hay_filtro = 1; s->filtro_r1 = f1; s->filtro_c1 = c1; s->filtro_r2 = f2; s->filtro_c2 = c2; }
            continue;
        }
        if (!igual(x.tag, "c")) continue;
        uint32_t f = fila, c = col;
        if (xr_attr(&x, "r", a, sizeof a)) { uint32_t ff, cc; if (hj_lee_dir(a, &ff, &cc) > 0) { f = ff; c = cc; } }
        col = c + 1;
        int est = xr_attr(&x, "s", a, sizeof a) ? (int)num(a) : 0;
        t[0] = 0; xr_attr(&x, "t", t, sizeof t);
        v[0] = 0; fm[0] = 0; fa[0] = 0; si[0] = 0; refa[0] = 0; int hay_v = 0, hay_f = 0;
        if (!x.vacia) while (xr_sig(&x)) {
            if (x.cierra && igual(x.tag, "c")) break;
            if (x.cierra) continue;
            if (igual(x.tag, "v")) { xr_texto(&x, "v", v, cap); hay_v = 1; }
            else if (igual(x.tag, "f")) { xr_attr(&x, "t", fa, sizeof fa); xr_attr(&x, "si", si, sizeof si); xr_attr(&x, "ref", refa, sizeof refa); xr_texto(&x, "f", fm, fcap); hay_f = 1; }
            else if (igual(x.tag, "t")) { uint32_t k = sl(v); xr_texto(&x, "t", v + k, cap - k); hay_v = 1; }
            else if (igual(x.tag, "rPh") && !x.vacia) { while (xr_sig(&x)) if (x.cierra && igual(x.tag, "rPh")) break; }
        }
        if (f >= HJ_FILAS || c >= HJ_COLS) continue;
        if (hay_v || hay_f) {
            if (igual(t, "s")) { int64_t k = num(v); if (k >= 0 && (uint64_t)k < nsst) (void)hj_pon_txt(l, h, f, c, sst[k]); }
            else if (igual(t, "inlineStr") || igual(t, "str")) (void)hj_pon_txt(l, h, f, c, v);
            else if (igual(t, "b")) (void)hj_pon_bool(l, h, f, c, es_si(v));
            else if (igual(t, "e")) (void)hj_pon_err(l, h, f, c, hj_error_de_texto(v));
            else if (igual(t, "d")) { f64 d; int nf; if (hj_lee_numero(v, &d, &nf, 0)) (void)hj_pon_num(l, h, f, c, d); }
            else if (hay_v && v[0]) { const char *e = 0; f64 d = f64_parse(v, &e); if (e && e != v) (void)hj_pon_num(l, h, f, c, d); }
        }
        if (hay_f) {
            const char *base = fm;
            if (igual(fa, "shared") && si[0]) {
                int idx = (int)num(si);
                if (fm[0]) { if (ncmp < 1024) { cmp[ncmp].si = idx; cmp[ncmp].f = f; cmp[ncmp].c = c; cmp[ncmp].form = hj_strdup(fm); ncmp++; } }
                else { base = 0; for (uint32_t k = 0; k < ncmp; ++k) if (cmp[k].si == idx && cmp[k].form) { if (!hj_form_normaliza(l, h, cmp[k].form, canon, fcap * 2) && !hj_form_desplaza(l, h, canon, (int32_t)f - (int32_t)cmp[k].f, (int32_t)c - (int32_t)cmp[k].c, fm, fcap)) base = fm; break; } }
            }
            if (base && base[0] && !hj_form_normaliza(l, h, base, canon, fcap * 2) && sl(canon) <= HJ_FORM_MAX) {
                if (!hj_celda(l, h, f, c)) (void)hj_pon_num(l, h, f, c, 0);
                (void)hj_pon_form_cache(l, h, f, c, canon);
            }
        }
        if (est > 0 && (uint32_t)est < E->nxf && E->mapa[est]) (void)hj_pon_estilo(l, h, f, c, E->mapa[est]);
        if (l->sin_memoria) break;
    }
    for (uint32_t k = 0; k < ncmp; ++k) if (cmp[k].form) hj_free(cmp[k].form);
    hj_free(cmp); hj_free(v); hj_free(fm); hj_free(canon);
    return l->sin_memoria ? -2 : 0;
}
/* ruta relativa de una relacion ("worksheets/sheet1.xml" o "/xl/worksheets/sheet1.xml") a la del ZIP */
static void ruta_rel(const char *base_dir, const char *target, char *out, uint32_t cap)
{
    if (target[0] == '/') { cpy(out, cap, target + 1); return; }
    uint32_t p = 0; out[0] = 0;
    for (const char *s = base_dir; *s && p + 1 < cap; ++s) out[p++] = *s;
    out[p] = 0;
    const char *t = target;
    while (t[0] == '.' && t[1] == '.' && t[2] == '/') { t += 3; if (p) { --p; while (p && out[p - 1] != '/') --p; out[p] = 0; } }
    cpy(out + p, cap - p, t);
}
/* ================================================================ SOMA_360_HOJA_GRAFICOS: graficos de una hoja (su dibujo y cada chart) */
static void dir_de(const char *ruta, char *out, uint32_t cap) { uint32_t k = sl(ruta); while (k && ruta[k - 1] != '/') --k; cpy(out, cap, ruta); if (k < cap) out[k] = 0; }
static void rels_de(const char *ruta, char *out, uint32_t cap)   /* "xl/worksheets/sheet1.xml" -> "xl/worksheets/_rels/sheet1.xml.rels" */
{
    char dir[160]; dir_de(ruta, dir, sizeof dir); uint32_t p;
    cpy(out, cap, dir); p = sl(out); cpy(out + p, cap - p, "_rels/"); p = sl(out); cpy(out + p, cap - p, ruta + sl(dir)); p = sl(out); cpy(out + p, cap - p, ".rels");
}
/* destino de la relacion id (o, con id 0, la primera cuyo tipo acaba en tipo) */
static int rel_destino(const uint8_t *d, uint32_t n, const char *ruta, const char *id, const char *tipo, char *out, uint32_t cap)
{
    char rn[200]; rels_de(ruta, rn, sizeof rn); uint32_t k; uint8_t *x = zr_lee(d, n, rn, &k); if (!x) return 0;
    char dir[160]; dir_de(ruta, dir, sizeof dir); int ok = 0;
    struct xr y = { (const char *)x, k, 0, { 0 }, 0, 0, 0, 0 }; char a[200], tg[200];
    while (xr_sig(&y)) {
        if (!igual(y.tag, "Relationship") || !xr_attr(&y, "Target", tg, sizeof tg)) continue;
        if (id) { if (!xr_attr(&y, "Id", a, sizeof a) || !igual(a, id)) continue; }
        else { if (!xr_attr(&y, "Type", a, sizeof a) || sl(a) < sl(tipo) || !igual(a + sl(a) - sl(tipo), tipo)) continue; }
        ruta_rel(dir, tg, out, cap); ok = 1; break;
    }
    hj_free(x); return ok;
}
static void lee_chart(struct hj_libro *l, int h, const uint8_t *d, uint32_t n, const char *ruta, struct hj_grafico *g)
{
    uint32_t k; uint8_t *x = zr_lee(d, n, ruta, &k); if (!x) return;
    struct xr y = { (const char *)x, k, 0, { 0 }, 0, 0, 0, 0 }; char a[256]; int en_titulo = 0, titulo_hecho = 0, tipo = -1, hay = 0; uint32_t tl = 0;
    int hd = -1;
    while (xr_sig(&y)) {
        const char *t = y.tag;
        if (igual(t, "title")) { if (y.cierra && en_titulo) titulo_hecho = 1; en_titulo = !y.cierra && !y.vacia && !titulo_hecho; continue; }
        if (y.cierra) continue;
        if (tipo < 0) {
            if (igual(t, "barChart") || igual(t, "bar3DChart")) tipo = 1;
            else if (igual(t, "lineChart") || igual(t, "line3DChart") || igual(t, "stockChart") || igual(t, "radarChart")) tipo = 2;
            else if (igual(t, "pieChart") || igual(t, "pie3DChart") || igual(t, "doughnutChart") || igual(t, "ofPieChart")) tipo = 3;
            else if (igual(t, "areaChart") || igual(t, "area3DChart")) tipo = 4;
            else if (igual(t, "scatterChart") || igual(t, "bubbleChart")) tipo = 5;
        }
        if (igual(t, "barDir") && xr_attr(&y, "val", a, sizeof a) && igual(a, "bar") && tipo == 1) tipo = 0;
        if (en_titulo && igual(t, "t") && !y.vacia && tl + 2 < sizeof g->titulo) { tl += xr_texto(&y, "t", g->titulo + tl, (uint32_t)sizeof g->titulo - tl); continue; }
        if (igual(t, "legend")) g->leyenda = 1;
        if (igual(t, "f") && !y.vacia && !en_titulo) {
            char f[256]; xr_texto(&y, "f", f, sizeof f);
            /* 'Hoja 1'!$A$1:$B$5 (o (ref1,ref2)): la hoja y el rango */
            const char *q = f; while (*q == '(') ++q;
            char hn[HJ_NOMBRE * 2]; uint32_t p = 0; hn[0] = 0;
            const char *ex = 0; for (const char *z = q; *z; ++z) if (*z == '!') { ex = z; break; }
            if (ex) { const char *z = q; if (*z == '\'') ++z; while (z < ex && p + 1 < sizeof hn) { if (*z == '\'' && z[1] == '\'') { hn[p++] = '\''; z += 2; continue; } if (*z == '\'' && z + 1 == ex) break; hn[p++] = *z++; } hn[p] = 0; q = ex + 1; }
            char rg[64]; p = 0; for (const char *z = q; *z && *z != ',' && *z != ')' && p + 1 < sizeof rg; ++z) if (*z != '$') rg[p++] = *z;
            rg[p] = 0;
            int hh = hn[0] ? hj_hoja_busca(l, hn) : h; uint32_t f1, c1, f2, c2;
            if (hh < 0 || hj_lee_rango(rg, &f1, &c1, &f2, &c2) <= 0) continue;
            if (hd < 0) hd = hh;
            if (hh != hd) continue;
            if (!hay) { g->r1 = f1; g->c1 = c1; g->r2 = f2; g->c2 = c2; hay = 1; }
            else { if (f1 < g->r1) g->r1 = f1; if (c1 < g->c1) g->c1 = c1; if (f2 > g->r2) g->r2 = f2; if (c2 > g->c2) g->c2 = c2; }
        }
    }
    hj_free(x);
    if (hay && tipo >= 0) { g->tipo = (uint8_t)tipo; g->hoja_datos = (uint8_t)hd; }
    else g->ancho = 0;                                                  /* sin datos que SOMA entienda: no se anade */
}
static void lee_graficos(struct hj_libro *l, int h, const uint8_t *d, uint32_t n, const char *ruta_hoja)
{
    char dib[200], ch[200]; uint32_t k;
    if (!rel_destino(d, n, ruta_hoja, 0, "/drawing", dib, sizeof dib)) return;
    uint8_t *x = zr_lee(d, n, dib, &k); if (!x) return;
    struct hj_hoja *s = &l->h[h];
    struct xr y = { (const char *)x, k, 0, { 0 }, 0, 0, 0, 0 }; char a[64];
    int lado = 0; int64_t fc = 0, fr = 0, fco = 0, fro = 0, tc = 0, tr = 0, tco = 0, tro = 0, cx = 0, cy = 0; char rid[64]; rid[0] = 0; int en = 0;
    while (xr_sig(&y) && s->ngraf < HJ_GRAF_MAX) {
        const char *t = y.tag;
        if (igual(t, "twoCellAnchor") || igual(t, "oneCellAnchor") || igual(t, "absoluteAnchor")) {
            if (!y.cierra) { en = 1; rid[0] = 0; fc = fr = fco = fro = tc = tr = tco = tro = cx = cy = 0; lado = 0; continue; }
            en = 0;
            if (!rid[0] || !rel_destino(d, n, dib, rid, 0, ch, sizeof ch)) continue;
            struct hj_grafico *g = &s->graf[s->ngraf]; zero(g, (uint32_t)sizeof *g);
            g->fila = (uint32_t)(fr >= 0 && fr < HJ_FILAS ? fr : 0); g->col = (uint32_t)(fc >= 0 && fc < HJ_COLS ? fc : 0);
            int64_t w, hh;
            if (cx > 0) { w = cx / 9525; hh = cy / 9525; }
            else {
                w = 0; for (int64_t c = fc; c < tc && c < fc + 200 && c < HJ_COLS; ++c) w += hj_ancho(s, (uint32_t)c);
                w += (tco - fco) / 9525;
                hh = 0; for (int64_t f = fr; f < tr && f < fr + 400 && f < HJ_FILAS; ++f) hh += hj_alto(s, (uint32_t)f);
                hh += (tro - fro) / 9525;
            }
            g->ancho = (uint32_t)(w >= 80 && w <= 4000 ? w : 480); g->alto = (uint32_t)(hh >= 60 && hh <= 4000 ? hh : 300);
            lee_chart(l, h, d, n, ch, g);
            if (g->ancho) s->ngraf++;
            continue;
        }
        if (!en || y.cierra) continue;
        if (igual(t, "from")) lado = 1; else if (igual(t, "to")) lado = 2;
        else if (igual(t, "col") || igual(t, "row") || igual(t, "colOff") || igual(t, "rowOff")) {
            if (y.vacia) continue;
            char tg[16]; cpy(tg, sizeof tg, t);
            char v[32]; xr_texto(&y, tg, v, sizeof v); int64_t q = num(v);
            int64_t *dst = igual(tg, "col") ? (lado == 2 ? &tc : &fc) : igual(tg, "row") ? (lado == 2 ? &tr : &fr) : igual(tg, "colOff") ? (lado == 2 ? &tco : &fco) : (lado == 2 ? &tro : &fro);
            *dst = q;
        }
        else if (igual(t, "ext") && lado != 3 && xr_attr(&y, "cx", a, sizeof a)) { int64_t q = num(a); if (q > 0 && !cx) { cx = q; if (xr_attr(&y, "cy", a, sizeof a)) cy = num(a); } }
        else if (igual(t, "xfrm")) lado = 3;                            /* el ext de dentro del marco no cuenta */
        else if (igual(t, "chart")) (void)xr_attr(&y, "id", rid, sizeof rid);
    }
    hj_free(x);
}
static int lee_xlsx(struct hj_libro *l, const uint8_t *d, uint32_t n, char *err, uint32_t cap)
{
    uint32_t wn, rn, sn, en; char wbruta[96] = "xl/workbook.xml", base[96] = "xl/";
    /* el libro: el que diga _rels/.rels (normalmente xl/workbook.xml) */
    uint8_t *rr = zr_lee(d, n, "_rels/.rels", &rn);
    if (rr) {
        struct xr x = { (const char *)rr, rn, 0, { 0 }, 0, 0, 0, 0 }; char ty[160], tg[96];
        while (xr_sig(&x)) if (igual(x.tag, "Relationship") && xr_attr(&x, "Type", ty, sizeof ty) && sl(ty) > 14 && igual(ty + sl(ty) - 14, "officeDocument") && xr_attr(&x, "Target", tg, sizeof tg)) { ruta_rel("", tg, wbruta, sizeof wbruta); break; }
        hj_free(rr);
    }
    { uint32_t k = sl(wbruta); while (k && wbruta[k - 1] != '/') --k; cpy(base, sizeof base, wbruta); base[k] = 0; }
    uint8_t *wb = zr_lee(d, n, wbruta, &wn);
    if (!wb) { cpy(err, cap, "No es un libro de Excel (falta el libro dentro del archivo)."); return -1; }
    char rels_n[128]; { uint32_t p = 0; rels_n[0] = 0; cpy(rels_n, sizeof rels_n, base); p = sl(rels_n); cpy(rels_n + p, sizeof rels_n - p, "_rels/"); p = sl(rels_n); const char *nb = wbruta + sl(base); cpy(rels_n + p, sizeof rels_n - p, nb); p = sl(rels_n); cpy(rels_n + p, sizeof rels_n - p, ".rels"); }
    uint8_t *rels = zr_lee(d, n, rels_n, &rn);
    /* hojas: nombre y destino */
    struct { char nombre[HJ_NOMBRE * 2]; char ruta[128]; } hs[HJ_HOJAS]; int nh = 0, activa = 0;
    struct est_x *E = (struct est_x *)hj_alloc((uint32_t)sizeof *E);
    if (!E) { hj_free(wb); if (rels) hj_free(rels); cpy(err, cap, "Sin memoria."); return -2; }
    zero(E, (uint32_t)sizeof *E);
    { struct xr x = { (const char *)wb, wn, 0, { 0 }, 0, 0, 0, 0 }; char a[128], id[64];
      while (xr_sig(&x)) {
          if (x.cierra) continue;
          if (igual(x.tag, "workbookView") && xr_attr(&x, "activeTab", a, sizeof a)) activa = (int)num(a);
          if (igual(x.tag, "sheet") && nh < HJ_HOJAS) {
              xr_attr(&x, "name", hs[nh].nombre, sizeof hs[nh].nombre); hs[nh].ruta[0] = 0;
              if (xr_attr(&x, "id", id, sizeof id) && rels) {
                  struct xr y = { (const char *)rels, rn, 0, { 0 }, 0, 0, 0, 0 }; char rid[64], tg[128];
                  while (xr_sig(&y)) if (igual(y.tag, "Relationship") && xr_attr(&y, "Id", rid, sizeof rid) && igual(rid, id) && xr_attr(&y, "Target", tg, sizeof tg)) { ruta_rel(base, tg, hs[nh].ruta, sizeof hs[nh].ruta); break; }
              }
              if (!hs[nh].ruta[0]) { uint32_t p = 0; cpy(hs[nh].ruta, sizeof hs[nh].ruta, base); p = sl(hs[nh].ruta); cpy(hs[nh].ruta + p, sizeof hs[nh].ruta - p, "worksheets/sheet"); p = sl(hs[nh].ruta); hs[nh].ruta[p++] = (char)('1' + nh % 9); hs[nh].ruta[p] = 0; cpy(hs[nh].ruta + p, sizeof hs[nh].ruta - p, ".xml"); }
              ++nh;
          }
      } }
    hj_free(wb);
    if (!nh) { if (rels) hj_free(rels); hj_free(E); cpy(err, cap, "El libro no tiene hojas."); return -1; }
    /* textos compartidos */
    char **sst = 0; uint32_t nsst = 0;
    { char sstn[128] = "xl/sharedStrings.xml";
      if (rels) { struct xr y = { (const char *)rels, rn, 0, { 0 }, 0, 0, 0, 0 }; char ty[160], tg[128]; while (xr_sig(&y)) if (igual(y.tag, "Relationship") && xr_attr(&y, "Type", ty, sizeof ty) && sl(ty) > 13 && igual(ty + sl(ty) - 13, "sharedStrings") && xr_attr(&y, "Target", tg, sizeof tg)) { ruta_rel(base, tg, sstn, sizeof sstn); break; } }
      uint8_t *ss = zr_lee(d, n, sstn, &sn);
      if (ss) {
          uint32_t cuenta = 0; for (uint32_t i = 0; i + 3 < sn; ++i) if (ss[i] == '<' && ss[i + 1] == 's' && ss[i + 2] == 'i' && (ss[i + 3] == '>' || ss[i + 3] == ' ')) ++cuenta;
          if (cuenta > 2000000) cuenta = 2000000;
          sst = cuenta ? (char **)hj_alloc(cuenta * (uint32_t)sizeof(char *)) : 0;
          char *tx = (char *)hj_alloc(HJ_TXT_MAX + 64);
          if (sst && tx) {
              struct xr x = { (const char *)ss, sn, 0, { 0 }, 0, 0, 0, 0 };
              while (xr_sig(&x) && nsst < cuenta) {
                  if (x.cierra || !igual(x.tag, "si")) continue;
                  tx[0] = 0; uint32_t k = 0;
                  if (!x.vacia) while (xr_sig(&x)) { if (x.cierra && igual(x.tag, "si")) break; if (!x.cierra && igual(x.tag, "t")) k += xr_texto(&x, "t", tx + k, HJ_TXT_MAX + 64 - k); else if (!x.cierra && igual(x.tag, "rPh") && !x.vacia) { while (xr_sig(&x)) if (x.cierra && igual(x.tag, "rPh")) break; } }
                  sst[nsst++] = hj_strdup(tx);
              }
          }
          if (tx) hj_free(tx);
          hj_free(ss);
      } }
    { uint8_t *st = zr_lee(d, n, "xl/styles.xml", &en); if (st) { lee_estilos(l, (const char *)st, en, E); hj_free(st); } }
    /* las hojas: primero todas (para que las formulas encuentren los nombres), luego su contenido */
    for (int i = 0; i < nh; ++i) {
        if (i == 0) { if (hj_hoja_renombra(l, 0, hs[0].nombre)) (void)hj_hoja_renombra(l, 0, "Hoja1"); }
        else if (hj_hoja_nueva(l, hs[i].nombre) < 0) (void)hj_hoja_nueva(l, 0);
    }
    int r = 0;
    for (int i = 0; i < nh && i < l->nh; ++i) {
        uint32_t hn; uint8_t *hx = zr_lee(d, n, hs[i].ruta, &hn);
        if (!hx) continue;
        r = lee_hoja(l, i, (const char *)hx, hn, sst, nsst, E);
        hj_free(hx);
        if (r) break;
        lee_graficos(l, i, d, n, hs[i].ruta);                           /* SOMA_360_HOJA_GRAFICOS */
    }
    if (activa >= 0 && activa < l->nh) l->activa = activa;
    for (uint32_t i = 0; i < nsst; ++i) if (sst[i]) hj_free(sst[i]);
    if (sst) hj_free(sst);
    if (rels) hj_free(rels);
    hj_free(E);
    if (r) { cpy(err, cap, "Sin memoria para todo el libro (demasiadas celdas)."); return r; }
    return 0;
}

/* ================================================================ reparto */
int hj_lee_ods(struct hj_libro *l, const uint8_t *d, uint32_t n, char *err, uint32_t cap);   /* hoja_ods.c */
static int empieza_ci(const uint8_t *d, uint32_t n, const char *s) { uint32_t k = sl(s); if (n < k) return 0; for (uint32_t i = 0; i < k; ++i) { char a = (char)d[i], b = s[i]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32); if (a != b) return 0; } return 1; }
static int termina_ci(const char *s, const char *suf) { uint32_t a = sl(s), b = sl(suf); if (b > a) return 0; for (uint32_t i = 0; i < b; ++i) { char x = s[a - b + i]; if (x >= 'A' && x <= 'Z') x = (char)(x + 32); if (x != suf[i]) return 0; } return 1; }
int hj_formato_de(const uint8_t *d, uint32_t n, const char *nombre)
{
    if (n >= 11 && empieza_ci(d, n, "soma-hoja 1")) return HJF_SOMA;
    if (zr_es_zip(d, n)) {
        uint32_t k; uint8_t *m = zr_lee(d, n, "mimetype", &k);
        if (m) { int ods = k >= 46 && empieza_ci(m, k, "application/vnd.oasis.opendocument.spreadsheet"); hj_free(m); if (ods) return HJF_ODS; }
        m = zr_lee(d, n, "[Content_Types].xml", &k); if (m) { hj_free(m); return HJF_XLSX; }
        m = zr_lee(d, n, "content.xml", &k); if (m) { hj_free(m); return HJF_ODS; }
        return HJF_DESCONOCIDO;
    }
    if (nombre && (termina_ci(nombre, ".xlsx") || termina_ci(nombre, ".xlsm") || termina_ci(nombre, ".ods"))) return HJF_DESCONOCIDO;   /* dice que lo es y no es un ZIP */
    if (n >= 8 && d[0] == 0xD0 && d[1] == 0xCF && d[2] == 0x11 && d[3] == 0xE0) return HJF_DESCONOCIDO;   /* .xls antiguo (OLE): no */
    uint32_t raros = 0; for (uint32_t i = 0; i < n && i < 4096; ++i) if (d[i] == 0) ++raros;
    if (raros) return HJF_DESCONOCIDO;
    return HJF_CSV;
}
int hj_lee_archivo(struct hj_libro *l, const uint8_t *d, uint32_t n, int tipo, char *err, uint32_t cap)
{
    if (cap) err[0] = 0;
    if (!tipo) tipo = hj_formato_de(d, n, 0);
    if (tipo == HJF_SOMA) { int r = hj_carga(l, (const char *)d, n); if (r) { if (hj_libro_nuevo(l)) return -2; cpy(err, cap, "El archivo de hoja de SOMA esta danado."); } return r; }
    if (hj_libro_nuevo(l)) { cpy(err, cap, "Sin memoria."); return -2; }
    if (n > HJF_MAX_ENTRADA) { cpy(err, cap, "El archivo es demasiado grande (48 MB como mucho)."); return -1; }
    int r;
    switch (tipo) {
    case HJF_XLSX: r = lee_xlsx(l, d, n, err, cap); break;
    case HJF_ODS: r = hj_lee_ods(l, d, n, err, cap); break;
    case HJF_CSV: r = hj_csv_lee(l, 0, (const char *)d, n, 0) < 0 ? -2 : 0; if (r) cpy(err, cap, "Sin memoria."); break;
    default: cpy(err, cap, "No reconozco el formato (abre XLSX, ODS o CSV; los .xls antiguos no)."); return -1;
    }
    if (!r) (void)hj_recalcula(l);
    (void)Fx;
    return r;
}
