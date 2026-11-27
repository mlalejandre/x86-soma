/* SOMA_360_HOJA_GRAFICOS: los graficos en la ventana (pintar, elegir, mover, cambiar de tamano). Incluido desde hoja_gui.c.
 * Sin FPU: los valores se pasan a milesimas en enteros; la tarta usa una tabla de arcotangentes (65536 = una vuelta). */
static const uint16_t ATAN_T[257] = { 0, 41, 81, 122, 163, 204, 244, 285, 326, 367, 407, 448, 489, 529, 570, 610, 651, 692, 732, 773, 813, 854, 894, 935, 975, 1015, 1056, 1096, 1136, 1177, 1217, 1257, 1297, 1337, 1377, 1417, 1457, 1497, 1537, 1577, 1617, 1656, 1696, 1736, 1775, 1815, 1854, 1894, 1933, 1973, 2012, 2051, 2090, 2129, 2168, 2207, 2246, 2285, 2324, 2363, 2401, 2440, 2478, 2517, 2555, 2594, 2632, 2670, 2708, 2746, 2784, 2822, 2860, 2897, 2935, 2973, 3010, 3047, 3085, 3122, 3159, 3196, 3233, 3270, 3307, 3344, 3380, 3417, 3453, 3490, 3526, 3562, 3599, 3635, 3670, 3706, 3742, 3778, 3813, 3849, 3884, 3920, 3955, 3990, 4025, 4060, 4095, 4129, 4164, 4199, 4233, 4267, 4302, 4336, 4370, 4404, 4438, 4471, 4505, 4539, 4572, 4605, 4639, 4672, 4705, 4738, 4771, 4803, 4836, 4869, 4901, 4933, 4966, 4998, 5030, 5062, 5094, 5125, 5157, 5188, 5220, 5251, 5282, 5313, 5344, 5375, 5406, 5437, 5467, 5498, 5528, 5559, 5589, 5619, 5649, 5679, 5708, 5738, 5768, 5797, 5826, 5856, 5885, 5914, 5943, 5972, 6000, 6029, 6058, 6086, 6114, 6142, 6171, 6199, 6227, 6254, 6282, 6310, 6337, 6365, 6392, 6419, 6446, 6473, 6500, 6527, 6554, 6580, 6607, 6633, 6660, 6686, 6712, 6738, 6764, 6790, 6815, 6841, 6867, 6892, 6917, 6943, 6968, 6993, 7018, 7043, 7068, 7092, 7117, 7141, 7166, 7190, 7214, 7238, 7262, 7286, 7310, 7334, 7358, 7381, 7405, 7428, 7451, 7475, 7498, 7521, 7544, 7566, 7589, 7612, 7635, 7657, 7679, 7702, 7724, 7746, 7768, 7790, 7812, 7834, 7856, 7877, 7899, 7920, 7942, 7963, 7984, 8005, 8026, 8047, 8068, 8089, 8110, 8131, 8151, 8172, 8192 };
static uint32_t angulo(int dx, int dy)      /* desde las 12 en el sentido del reloj */
{
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (!ax && !ay) return 0;
    uint32_t b = ay >= ax ? ATAN_T[(uint32_t)ax * 256u / (uint32_t)ay] : 16384u - ATAN_T[(uint32_t)ay * 256u / (uint32_t)ax];
    if (dx >= 0 && dy < 0) return b;
    if (dx >= 0) return 32768u - b;
    if (dy >= 0) return 32768u + b;
    return (65536u - b) & 0xFFFFu;
}
static const uint32_t GPAL[10] = { 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47, 0x264478, 0x9E480E, 0x636363, 0x997300 };
#define GMAX_S 12
#define GMAX_C 400
struct gdatos { int n_s, n_c, cat_num; int64_t v[GMAX_S][GMAX_C]; uint8_t hay[GMAX_S][GMAX_C]; int64_t x[GMAX_C]; char ns[GMAX_S][40]; char nc[GMAX_C][32]; };
static struct gdatos *g_gd;

static int64_t a_mil(f64 v) { f64 m = f64_mul(v, f64_from_i64(1000)); if (!f64_is_finite(m)) return 0; int64_t r = f64_to_i64(m); if (r > 9000000000000000LL) r = 9000000000000000LL; if (r < -9000000000000000LL) r = -9000000000000000LL; return r; }
static void etiqueta(int64_t mil, char *b, uint32_t cap) { num_corto(f64_div(f64_from_i64(mil), f64_from_i64(1000)), b, cap); }
static int lee_datos(struct hj_libro *l, int h, const struct hj_grafico *g, struct gdatos *d)
{
    struct hj_gforma fm; d->n_s = d->n_c = 0; d->cat_num = 1;
    if (hj_grafico_forma(l, h, g, &fm)) return -1;
    d->n_s = (int)(fm.nser > GMAX_S ? GMAX_S : fm.nser); d->n_c = (int)(fm.ncat > GMAX_C ? GMAX_C : fm.ncat);
    if (g->tipo == 3) d->n_s = 1;
    static char b[256]; uint32_t f, c; struct hj_valor v;
    for (int k = 0; k < d->n_s; ++k) {
        if (fm.titulos) { hj_gforma_nombre(&fm, (uint32_t)k, &f, &c); hj_texto_ver(l, fm.h, f, c, b, sizeof b); cpy(d->ns[k], b, sizeof d->ns[k]); }
        else { d->ns[k][0] = 0; cat(d->ns[k], "Serie ", sizeof d->ns[k]); num(d->ns[k], sizeof d->ns[k], k + 1); }
        for (int i = 0; i < d->n_c; ++i) {
            hj_gforma_valor(&fm, (uint32_t)k, (uint32_t)i, &f, &c); hj_valor_de(l, fm.h, f, c, &v);
            d->hay[k][i] = v.t == HJ_NUM; d->v[k][i] = v.t == HJ_NUM ? a_mil(v.n) : 0;
        }
    }
    for (int i = 0; i < d->n_c; ++i) {
        if (fm.categorias) {
            hj_gforma_cat(&fm, (uint32_t)i, &f, &c); hj_valor_de(l, fm.h, f, c, &v); hj_texto_ver(l, fm.h, f, c, b, sizeof b);
            cpy(d->nc[i], b, sizeof d->nc[i]); d->x[i] = v.t == HJ_NUM ? a_mil(v.n) : (int64_t)(i + 1) * 1000; if (v.t != HJ_NUM) d->cat_num = 0;
        } else { d->nc[i][0] = 0; num(d->nc[i], sizeof d->nc[i], i + 1); d->x[i] = (int64_t)(i + 1) * 1000; }
    }
    return 0;
}
static int64_t paso_bonito(int64_t r)
{
    if (r <= 0) return 1000;
    int64_t crudo = r / 5; if (crudo < 1) crudo = 1;
    int64_t mag = 1; while (mag <= crudo / 10) mag *= 10;
    int64_t n = crudo / mag;
    return (n <= 1 ? 1 : n <= 2 ? 2 : n <= 5 ? 5 : 10) * mag;
}
static void escala(int64_t *mn, int64_t *mx, int64_t *paso, int con_cero)
{
    if (con_cero) { if (*mn > 0) *mn = 0; if (*mx < 0) *mx = 0; }
    if (*mx <= *mn) { *mx = *mn + 1000; }
    *paso = paso_bonito(*mx - *mn);
    int64_t a = *mn / *paso * *paso; if (a > *mn) a -= *paso;
    int64_t b = *mx / *paso * *paso; if (b < *mx) b += *paso;
    *mn = a; *mx = b;
}
static int ypos(int64_t v, int64_t mn, int64_t mx, int y0, int h) { if (mx == mn) return y0 + h; return y0 + h - (int)((v - mn) * (int64_t)h / (mx - mn)); }
static int xpos(int64_t v, int64_t mn, int64_t mx, int x0, int w) { if (mx == mn) return x0; return x0 + (int)((v - mn) * (int64_t)w / (mx - mn)); }
static void linea(int x0, int y0, int x1, int y1, uint32_t c, int grosor) { fb_line_aa(x0 * 16, y0 * 16, x1 * 16, y1 * 16, grosor, 0xFF000000u | c); }
static void pinta_grafico(struct hj_libro *l, int h, const struct hj_grafico *g, int x, int y, int w, int hh, int elegido)
{
    rect(x, y, w, hh, 0x00FFFFFFu);
    fb_stroke_rrect_aa(x, y, w, hh, 0, elegido ? 0xFF16A34Au : 0xFFC9CED6u);
    if (elegido) { fb_stroke_rrect_aa(x + 1, y + 1, w - 2, hh - 2, 0, 0xFF16A34Au); rect(x + w - 8, y + hh - 8, 8, 8, 0x0016A34Au); }
    if (!g_gd) g_gd = (struct gdatos *)kmalloc(sizeof *g_gd);
    struct gdatos *d = g_gd;
    int px = Z(11), top = y + 8;
    if (g->titulo[0]) { int tw2 = tw(g->titulo, Z(14), WF_BOLD); txc(x + (w - tw2) / 2, y + 4, w - 8, Z(24), g->titulo, Z(14), 0x00202020u, WF_BOLD); top = y + Z(28); }
    if (!d || lee_datos(l, h, g, d) || !d->n_c) { txc(x + 12, top, w - 24, 24, "Sin datos num\303\251ricos en el rango", Z(12), 0x00808080u, 0); return; }
    int t = g->tipo;
    /* leyenda a la derecha */
    int leg_w = 0, nleg = t == 3 ? d->n_c : d->n_s;
    if (g->leyenda && nleg > 0) { for (int k = 0; k < nleg && k < 24; ++k) { int a = tw(t == 3 ? d->nc[k] : d->ns[k], px, 0) + 22; if (a > leg_w) leg_w = a; } if (leg_w > w / 3) leg_w = w / 3; }
    int ax = x + 10, ay = top, aw = w - 20 - (leg_w ? leg_w + 8 : 0), ah = y + hh - 10 - ay;
    if (aw < 40 || ah < 40) return;
    if (leg_w) {
        int ly = ay + (ah - nleg * (px + 8)) / 2; if (ly < ay) ly = ay;
        for (int k = 0; k < nleg && ly + px < y + hh - 4; ++k, ly += px + 8) {
            rect(x + w - leg_w - 8, ly + 2, 10, 10, GPAL[k % 10]);
            txc(x + w - leg_w + 6, ly, leg_w - 12, px + 4, t == 3 ? d->nc[k] : d->ns[k], px, 0x00404040u, 0);
        }
    }
    fb_set_clip(x + 1, y + 1, w - 2, hh - 2);
    if (t == 3) {                                                              /* tarta */
        int64_t tot = 0; for (int i = 0; i < d->n_c; ++i) if (d->hay[0][i] && d->v[0][i] > 0) tot += d->v[0][i];
        int r = (aw < ah ? aw : ah) / 2 - 4; if (r < 10 || tot <= 0) { fb_reset_clip(); return; }
        int cx = ax + aw / 2, cy = ay + ah / 2;
        uint32_t lim[GMAX_C + 1]; int64_t acc = 0; lim[0] = 0;
        for (int i = 0; i < d->n_c; ++i) { if (d->hay[0][i] && d->v[0][i] > 0) acc += d->v[0][i]; lim[i + 1] = (uint32_t)(acc * 65536 / tot); }
        for (int yy = -r; yy <= r; ++yy) {
            int xr = 0; while ((xr + 1) * (xr + 1) + yy * yy <= r * r) ++xr;
            int run_x = -xr, run_k = -1;
            for (int xx = -xr; xx <= xr + 1; ++xx) {
                int k = -1;
                if (xx <= xr) { uint32_t a = angulo(xx, yy); k = 0; while (k < d->n_c - 1 && a >= lim[k + 1]) ++k; }
                if (k != run_k) {
                    if (run_k >= 0) rect(cx + run_x, cy + yy, xx - run_x, 1, GPAL[run_k % 10]);
                    if (run_k >= 0 && k >= 0) rect(cx + xx - 1, cy + yy, 2, 1, 0x00FFFFFFu);   /* separacion entre porciones */
                    run_k = k; run_x = xx;
                }
            }
        }
        fb_reset_clip(); return;
    }
    /* ejes */
    int64_t mn = 0, mx = 0; int primero = 1;
    for (int k = 0; k < d->n_s; ++k) for (int i = 0; i < d->n_c; ++i) if (d->hay[k][i]) { if (primero || d->v[k][i] < mn) mn = d->v[k][i]; if (primero || d->v[k][i] > mx) mx = d->v[k][i]; primero = 0; }
    int64_t paso; escala(&mn, &mx, &paso, t != 5 && t != 2 ? 1 : (mn > 0 && mx > 0 && mn < mx / 3) || (mn <= 0));
    int64_t xmn = 0, xmx = 0, xpaso = 1000;
    if (t == 5) { xmn = d->x[0]; xmx = d->x[0]; for (int i = 1; i < d->n_c; ++i) { if (d->x[i] < xmn) xmn = d->x[i]; if (d->x[i] > xmx) xmx = d->x[i]; } escala(&xmn, &xmx, &xpaso, xmn > 0 && xmn < xmx / 3); }
    char b[48];
    /* ancho de las etiquetas del eje de valores */
    int lw = 0; for (int64_t v = mn; v <= mx; v += paso) { etiqueta(v, b, sizeof b); int a = tw(b, px, 0); if (a > lw) lw = a; if ((mx - mn) / paso > 40) break; }
    int horiz = t == 0;
    int clw = 0; if (horiz) for (int i = 0; i < d->n_c; ++i) { int a = tw(d->nc[i], px, 0); if (a > clw) clw = a; }
    if (clw > aw / 3) clw = aw / 3;
    int pxl = ax + (horiz ? clw + 6 : lw + 6), pyt = ay + 4, pw = ax + aw - pxl - 4, ph = ay + ah - pyt - (px + 8);
    if (pw < 20 || ph < 20) { fb_reset_clip(); return; }
    /* rejilla y etiquetas de valores */
    for (int64_t v = mn; v <= mx; v += paso) {
        etiqueta(v, b, sizeof b);
        if (horiz) { int gx = xpos(v, mn, mx, pxl, pw); rect(gx, pyt, 1, ph, 0x00E6E8EBu); txc(gx - tw(b, px, 0) / 2, pyt + ph + 2, tw(b, px, 0) + 2, px + 6, b, px, 0x00606060u, 0); }
        else { int gy = ypos(v, mn, mx, pyt, ph); rect(pxl, gy, pw, 1, 0x00E6E8EBu); txc(pxl - 6 - tw(b, px, 0), gy - px / 2 - 2, lw + 4, px + 4, b, px, 0x00606060u, 0); }
        if ((mx - mn) / paso > 40) break;
    }
    if (t == 5) {
        for (int64_t v = xmn; v <= xmx; v += xpaso) { etiqueta(v, b, sizeof b); int gx = xpos(v, xmn, xmx, pxl, pw); rect(gx, pyt, 1, ph, 0x00EEF0F2u); txc(gx - tw(b, px, 0) / 2, pyt + ph + 2, tw(b, px, 0) + 2, px + 6, b, px, 0x00606060u, 0); if ((xmx - xmn) / xpaso > 40) break; }
    } else {
        /* etiquetas de categorias (saltando si no caben) */
        int nc = d->n_c, salto = 1; int esp = (horiz ? ph : pw) / (nc ? nc : 1);
        int mxl = 0; for (int i = 0; i < nc; ++i) { int a = horiz ? px + 4 : tw(d->nc[i], px, 0) + 6; if (a > mxl) mxl = a; }
        while (esp * salto < mxl && salto < nc) ++salto;
        for (int i = 0; i < nc; i += salto) {
            if (horiz) { int cy2 = pyt + (2 * i + 1) * ph / (2 * nc); txc(ax, cy2 - px / 2 - 2, clw + 2, px + 4, d->nc[i], px, 0x00606060u, 0); }
            else { int cx2 = (t == 2 || t == 4) && nc > 1 ? pxl + i * pw / (nc - 1) : pxl + (2 * i + 1) * pw / (2 * nc); int a = tw(d->nc[i], px, 0); txc(cx2 - a / 2, pyt + ph + 2, a + 2, px + 6, d->nc[i], px, 0x00606060u, 0); }
        }
    }
    int base = horiz ? xpos(0 < mn ? mn : 0 > mx ? mx : 0, mn, mx, pxl, pw) : ypos(0 < mn ? mn : 0 > mx ? mx : 0, mn, mx, pyt, ph);
    if (horiz) rect(base, pyt, 1, ph, 0x00909090u); else rect(pxl, base, pw, 1, 0x00909090u);
    int nc = d->n_c, ns = d->n_s;
    if (t <= 1) {                                                              /* barras y columnas */
        int grupo = (horiz ? ph : pw) / nc, ancho = grupo * 8 / 10 / ns; if (ancho < 1) ancho = 1;
        for (int i = 0; i < nc; ++i) for (int k = 0; k < ns; ++k) {
            if (!d->hay[k][i]) continue;
            int p0 = (horiz ? pyt : pxl) + i * grupo + grupo / 10 + k * ancho;
            if (horiz) { int vx = xpos(d->v[k][i], mn, mx, pxl, pw); int a = vx < base ? vx : base, bb = vx < base ? base : vx; rect(a, p0, bb - a + (bb == a), ancho - (ancho > 3), GPAL[k % 10]); }
            else { int vy = ypos(d->v[k][i], mn, mx, pyt, ph); int a = vy < base ? vy : base, bb = vy < base ? base : vy; rect(p0, a, ancho - (ancho > 3), bb - a + (bb == a), GPAL[k % 10]); }
        }
    } else if (t == 2 || t == 4) {                                             /* lineas y area */
        for (int k = ns - 1; k >= 0; --k) {
            int px0 = 0, py0 = 0, hay0 = 0;
            for (int i = 0; i < nc; ++i) {
                int cx2 = nc > 1 ? pxl + i * pw / (nc - 1) : pxl + pw / 2, cy2 = ypos(d->v[k][i], mn, mx, pyt, ph);
                if (!d->hay[k][i]) { hay0 = 0; continue; }
                if (t == 4 && hay0) for (int xx = px0; xx <= cx2; ++xx) { int yy = cx2 == px0 ? cy2 : py0 + (cy2 - py0) * (xx - px0) / (cx2 - px0); int a = yy < base ? yy : base, bb = yy < base ? base : yy; fb_fill_rect_alpha(xx, a, 1, bb - a, 0x99000000u | GPAL[k % 10]); }
                if (hay0) linea(px0, py0, cx2, cy2, GPAL[k % 10], 36);
                if (t == 2) fb_fill_circle(cx2, cy2, 3, GPAL[k % 10]);
                px0 = cx2; py0 = cy2; hay0 = 1;
            }
        }
    } else if (t == 5) {                                                       /* dispersion */
        for (int k = 0; k < ns; ++k) for (int i = 0; i < nc; ++i) if (d->hay[k][i]) fb_fill_circle(xpos(d->x[i], xmn, xmx, pxl, pw), ypos(d->v[k][i], mn, mx, pyt, ph), 4, GPAL[k % 10]);
    }
    fb_reset_clip();
}
/* donde cae en pantalla la esquina de un grafico (con lo inmovilizado y el desplazamiento) */
static void graf_pos(const struct hj_grafico *g, int *x, int *y)
{
    struct hj_hoja *s = S(); struct vista *v = V();
    int wfz = 0; for (uint32_t c = 0; c < s->inmov_c; ++c) wfz += ancho_col(c);
    int hfz = 0; for (uint32_t f = 0; f < s->inmov_f; ++f) if (!hj_fila_oculta(s, f)) hfz += alto_fila(f);
    int xx;
    if (g->col < s->inmov_c) { xx = 0; for (uint32_t c = 0; c < g->col; ++c) xx += ancho_col(c); }
    else if (g->col >= v->left) { xx = wfz; for (uint32_t c = v->left; c < g->col && xx < 20000; ++c) xx += ancho_col(c); }
    else { xx = wfz; for (uint32_t c = g->col; c < v->left && xx > -20000; ++c) xx -= ancho_col(c); }
    int yy;
    if (g->fila < s->inmov_f) { yy = 0; for (uint32_t f = 0; f < g->fila; ++f) if (!hj_fila_oculta(s, f)) yy += alto_fila(f); }
    else if (g->fila >= v->top) { yy = hfz; for (uint32_t f = v->top; f < g->fila && yy < 20000; ++f) if (!hj_fila_oculta(s, f)) yy += alto_fila(f); }
    else { yy = hfz; for (uint32_t f = g->fila; f < v->top && yy > -20000; ++f) if (!hj_fila_oculta(s, f)) yy -= alto_fila(f); }
    *x = g_gx + xx; *y = g_gy + yy;
}
static void pinta_graficos(void)
{
    struct hj_libro *l = L(); int h = l->activa; struct hj_hoja *s = &l->h[h];
    if (g_gsel >= s->ngraf) g_gsel = -1;
    fb_set_clip(g_gx, g_gy, g_gw, g_gh);
    for (uint32_t z = 0; z <= s->ngraf; ++z) {                        /* el elegido, el ultimo: encima de los demas */
        uint32_t k = z < s->ngraf ? z : (uint32_t)g_gsel;
        if (z < s->ngraf && (int)z == g_gsel) continue;
        if (z == s->ngraf && (g_gsel < 0 || g_gsel >= s->ngraf)) break;
        const struct hj_grafico *g = &s->graf[k]; int x, y; graf_pos(g, &x, &y);
        int w = Z((int)g->ancho), hh = Z((int)g->alto);
        if (x > g_gx + g_gw || y > g_gy + g_gh || x + w < g_gx || y + hh < g_gy) continue;
        pinta_grafico(l, h, g, x, y, w, hh, (int)k == g_gsel);
        fb_set_clip(g_gx, g_gy, g_gw, g_gh);
        hit(x, y, w, hh, H_GRAF, (int)k);
        if ((int)k == g_gsel) hit(x + w - 12, y + hh - 12, 14, 14, H_GRAF_TAM, (int)k);
    }
    fb_reset_clip();
}
