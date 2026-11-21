/* gui_fotograma.c: dibujo de cada fotograma, capa de fondo y caches de ventanas.
 * SOMA_234_GUI_PARTIDO: NO se compila por separado. Lo incluye src/ui/gui.c en su sitio (una sola unidad de
 * compilacion: comparte el estado del escritorio con gui.c, y el nucleo compilado sale identico). */
/* ====================================================================================================================
 * SOMA_209_ECO: menos trabajo por fotograma sin cambiar la imagen.
 *   1. Halo: la geometria (que entrada de la tabla de color toca a cada pixel) se calcula una vez; cada fotograma solo consulta.
 *   2. Borrado y volcado por zonas tambien con ventanas abiertas (antes, pantalla entera en cada fotograma).
 *   3. El contenido de una ventana que no ha cambiado se copia de su ultima pintura en vez de volver a dibujarse.
 *   4. Las cintas se pintan en una capa propia cada OFX_RIBL_MS y se suman al fondo en cada fotograma.
 * 1-2 dan los mismos pixeles ('orbe eco off' vuelve al camino anterior). 3 y 4 dan los mismos pixeles en cada instante en que se
 * renuevan; entre medias (70-80 ms) el cursor de texto y las cintas no avanzan ('orbe ventanas off', 'orbe cintas off').
 * 'orbe verifica' lo comprueba.
 * ==================================================================================================================== */
#ifndef OFX_RIBL_MS
#define OFX_RIBL_MS 80u
#endif
int her_eco_on = 1, her_ribl_on = 1, her_eco_verify = 0;
uint32_t her_eco_stat[10];   /* 0 capas de cintas pintadas, 1 reutilizadas, 2 cuerpos copiados, 3 cuerpos pintados, 4 cuerpos distintos (verifica),
                              * 5 pixeles sueltos (verifica), 6 pixeles de pantalla != backbuffer (verifica), 7 fotogramas verificados */
static uint32_t her_eco_period = 33u, her_eco_vn;

static uint16_t *hm_map; static int32_t *hm_span; static int32_t hm_R = -1, hm_side;
static int ofx_halo_map_build(int32_t R)
{
    if (hm_map && hm_R == R) return 1;
    if (hm_map) { kfree(hm_map); hm_map = 0; }
    if (hm_span) { kfree(hm_span); hm_span = 0; }
    hm_R = -1;
    int32_t side = 2 * R + OFX_HALO_STEP;
    hm_map = (uint16_t *)kmalloc((size_t)side * (size_t)side * sizeof(uint16_t));
    hm_span = (int32_t *)kmalloc((size_t)side * 2u * sizeof(int32_t));
    if (!hm_map || !hm_span) { if (hm_map) kfree(hm_map); if (hm_span) kfree(hm_span); hm_map = 0; hm_span = 0; return 0; }
    for (int32_t i = 0; i < side * side; ++i) hm_map[i] = 256u;
    int32_t R2 = R * R;
    uint32_t kmul = (uint32_t)(255 * 65536 / R2);
    int32_t steps = (2 * R + OFX_HALO_STEP - 1) / OFX_HALO_STEP;
    for (int32_t st = 0; st < steps; ++st) {                     /* el mismo recorrido que ofx_halo_band */
        int32_t yy = st * OFX_HALO_STEP, dy = yy - R, dy2 = dy * dy;
        for (int32_t xx = 0; xx < 2 * R; xx += OFX_HALO_STEP) {
            int32_t dx = xx - R, d2 = dx * dx + dy2;
            if (d2 >= R2 || d2 < 484) continue;
            uint16_t idx = (uint16_t)((((uint32_t)d2 * kmul) >> 16) & 255u);
            for (int32_t sy = 0; sy < OFX_HALO_STEP; ++sy)
                for (int32_t sx = 0; sx < OFX_HALO_STEP; ++sx) hm_map[(yy + sy) * side + xx + sx] = idx;
        }
    }
    for (int32_t y = 0; y < side; ++y) {
        int32_t a = 0, b = side;
        while (a < side && hm_map[y * side + a] == 256u) ++a;
        while (b > a && hm_map[y * side + b - 1] == 256u) --b;
        hm_span[2 * y] = a; hm_span[2 * y + 1] = b;
    }
    hm_R = R; hm_side = side;
    return 1;
}
static void ofx_halo_fast(volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R)
{
    if (cx - R < 0 || cy - R < 0 || cx + R + OFX_HALO_STEP > w || cy + R + OFX_HALO_STEP > h || !ofx_halo_map_build(R)) {
        ofx_draw_halo(bb, w, h, cx, cy, R); return;
    }
    uint32_t lut[257];
    for (int v = 0; v < 256; ++v) lut[v] = ofx_halo[v] & 0x00FFFFFFu;
    lut[256] = 0;
    int32_t side = hm_side, bx = cx - R, by = cy - R;
    for (int32_t row = 0; row < side; ++row) {
        int32_t x0 = hm_span[2 * row], x1 = hm_span[2 * row + 1];
        if (x0 >= x1) continue;
        int32_t y = by + row;
        volatile uint32_t *p = bb + (uint32_t)y * (uint32_t)w + (uint32_t)bx;
        const uint16_t *m = hm_map + row * side;
        int32_t o0 = x1, o1 = x1;                                /* tramo tapado por la ventana superior (como ofx_occluded) */
        if (ofx_occ_on && y >= ofx_occ_y0 && y < ofx_occ_y1) { o0 = ofx_occ_x0 - bx; o1 = ofx_occ_x1 - bx; }
        for (int32_t x = x0; x < x1; ++x) {
            if (x >= o0 && x < o1) { x = o1 - 1; continue; }
            uint32_t a = lut[m[x]];
            if (!a) continue;
            uint32_t d = p[x];
            if (!(d & 0x00FFFFFFu)) { p[x] = a; continue; }
            uint32_t r = ((d >> 16) & 255u) + ((a >> 16) & 255u), g = ((d >> 8) & 255u) + ((a >> 8) & 255u), b = (d & 255u) + (a & 255u);
            if (r > 255u) r = 255u;
            if (g > 255u) g = 255u;
            if (b > 255u) b = 255u;
            p[x] = (r << 16) | (g << 8) | b;
        }
    }
}

static void ofx_ribbons_direct(volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R, uint32_t gain)
{
    if (SOMA_ORB_RIB_HALF && ofx_rib_mode) ofx_draw_ribbons_fast(bb, w, h, cx, cy, R, gain);
    else ofx_draw_ribbons(bb, w, h, cx, cy, R, gain);
}
/* Capa trasera del orbe (halo + cintas). Es lo primero que se dibuja sobre el fondo negro recien borrado, asi que guardar su
 * caja y volver a copiarla da exactamente los mismos pixeles mientras nada de lo que la determina cambie.
 *   - Halo solo: depende de la tabla de color, del radio y de la ventana que tapa. Si no cambian, se copia (identico siempre).
 *   - Halo + cintas: ademas las cintas avanzan con el tiempo. Con 'orbe cintas on' se reutilizan hasta OFX_RIBL_MS. */
static int ofx_bg_black;                                         /* 1 = el llamador garantiza fondo negro en la caja del orbe */
struct ob_key { int32_t R, cx, cy, w, h, occ[5]; uint32_t col; uint32_t lut[256]; };
static struct ob_key hk_key, ok_key; static int hk_valid, ok_valid;
static uint32_t *hk_px, *ok_px; static int32_t hk_side, ok_side; static uint64_t ok_ms;
static void ob_key_now(struct ob_key *k, int32_t R, int32_t cx, int32_t cy, int32_t w, int32_t h)
{
    k->R = R; k->cx = cx; k->cy = cy; k->w = w; k->h = h;
    k->occ[0] = ofx_occ_on; k->occ[1] = ofx_occ_on ? ofx_occ_x0 : 0; k->occ[2] = ofx_occ_on ? ofx_occ_y0 : 0;
    k->occ[3] = ofx_occ_on ? ofx_occ_x1 : 0; k->occ[4] = ofx_occ_on ? ofx_occ_y1 : 0;
    k->col = her_col_cur & 0x00FFFFFFu;
    memcpy(k->lut, ofx_halo, sizeof k->lut);
}
static int ob_key_eq(const struct ob_key *a, const struct ob_key *b)
{
    if (a->R != b->R || a->cx != b->cx || a->cy != b->cy || a->w != b->w || a->h != b->h || a->col != b->col) return 0;
    for (int i = 0; i < 5; ++i) if (a->occ[i] != b->occ[i]) return 0;
    return memcmp(a->lut, b->lut, sizeof a->lut) == 0;
}
static int32_t ob_extent(int32_t R)                              /* alcance de las cintas (como rb_draw) con margen */
{
    int32_t ext = 0;
    for (int k = 0; k < OFX_RIB_N; ++k) {
        int32_t amp = (int32_t)ofx_rib[k].amp; if (amp < 0) amp = -amp;
        int32_t e = ((int32_t)ofx_rib[k].rad + amp + (int32_t)ofx_rib[k].wid) * R / 216;
        if (e > ext) ext = e;
    }
    ext = (ext + 8 + RB_TS - 1) / RB_TS * RB_TS + 8;
    if (ext < R + 8) ext = R + 8;
    return ext;
}
static int ob_box_ok(int32_t cx, int32_t cy, int32_t e, int32_t w, int32_t h) { return cx - e >= 0 && cy - e >= 0 && cx + e <= w && cy + e <= h; }
static void ob_copy(uint32_t *buf, volatile uint32_t *bb, int32_t w, int32_t cx, int32_t cy, int32_t e, int to_screen)
{
    int32_t side = 2 * e;
    for (int32_t r = 0; r < side; ++r) {
        uint32_t *scr = (uint32_t *)(bb + (size_t)(cy - e + r) * (size_t)w + (size_t)(cx - e));
        if (to_screen) memcpy(scr, buf + (size_t)r * (size_t)side, (size_t)side * 4u);
        else memcpy(buf + (size_t)r * (size_t)side, scr, (size_t)side * 4u);
    }
}
static uint32_t *ob_buf(uint32_t **px, int32_t *side_now, int32_t e)
{
    if (*px && *side_now == 2 * e) return *px;
    if (*px) kfree(*px);
    *px = (uint32_t *)kmalloc((size_t)(2 * e) * (size_t)(2 * e) * 4u);
    *side_now = *px ? 2 * e : 0;
    return *px;
}
/* Halo y cintas del fotograma. Devuelve por t[0], t[1] el tiempo de cada parte. */
static void ofx_back_eco(volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R, uint32_t gain, uint64_t *t)
{
    uint64_t q0 = tmp_tsc(), now = timer_get_uptime_ms();
    int black = ofx_bg_black;
    int32_t E = ob_extent(R);
    struct ob_key k;
    ob_key_now(&k, R, cx, cy, w, h);
    if (black && her_ribl_on && ok_valid && ok_px && ok_side == 2 * E && now - ok_ms < OFX_RIBL_MS && ofx_flash <= 8u &&
        (SOMA_ORB_RIB_HALF && ofx_rib_mode) && ob_key_eq(&k, &ok_key) && ob_box_ok(cx, cy, E, w, h)) {
        ob_copy(ok_px, bb, w, cx, cy, E, 1);
        her_eco_stat[1]++;
        t[0] += tmp_tsc() - q0;
        return;
    }
    if (black && hk_valid && hk_px && hk_side == 2 * R + 2 * OFX_HALO_STEP && ob_key_eq(&k, &hk_key) && ob_box_ok(cx, cy, R + OFX_HALO_STEP, w, h)) {
        ob_copy(hk_px, bb, w, cx, cy, R + OFX_HALO_STEP, 1);
        her_eco_stat[8]++;
    } else {
        ofx_halo_fast(bb, w, h, cx, cy, R);
        hk_valid = 0;
        if (black && ob_box_ok(cx, cy, R + OFX_HALO_STEP, w, h) && ob_buf(&hk_px, &hk_side, R + OFX_HALO_STEP)) {
            ob_copy(hk_px, bb, w, cx, cy, R + OFX_HALO_STEP, 0); hk_key = k; hk_valid = 1;
        }
        her_eco_stat[9]++;
    }
    uint64_t q1 = tmp_tsc(); t[0] += q1 - q0;
    ofx_ribbons_direct(bb, w, h, cx, cy, R, gain);
    her_eco_stat[0]++;
    ok_valid = 0;
    static uint64_t prev_ms;                                     /* si los fotogramas llegan mas espaciados que la vida de la capa, no se guarda */
    int soon = now - prev_ms < OFX_RIBL_MS; prev_ms = now;
    if (black && her_ribl_on && soon && ofx_flash <= 8u && ob_box_ok(cx, cy, E, w, h) && ob_buf(&ok_px, &ok_side, E)) {
        ob_copy(ok_px, bb, w, cx, cy, E, 0); ok_key = k; ok_ms = now; ok_valid = 1;
    }
    t[1] += tmp_tsc() - q1;
}
/* 'orbe verifica': el halo por tabla y las cintas por capa deben dar exactamente los mismos pixeles que el camino directo,
 * sobre fondo negro y con textura, con y sin ventana tapando. */
int orb_eco_selftest(char *out, uint32_t max)
{
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    uint32_t p = 0;
    if (w <= 0 || h <= 0) { soma_str_put(out, max, &p, "orbe: sin pantalla\n"); return 1; }
    uint32_t n = (uint32_t)w * (uint32_t)h;
    uint32_t *b1 = (uint32_t *)kmalloc(n * 4u), *b2 = (uint32_t *)kmalloc(n * 4u);
    if (!b1 || !b2) { if (b1) kfree(b1); if (b2) kfree(b2); soma_str_put(out, max, &p, "orbe: sin memoria\n"); return 1; }
    int32_t R = ofx_radius(h), cx = w / 2, cy = h / 2;
    uint32_t bad_h = 0, bad_r = 0; int cases = 0;
    uint64_t ph[5]; for (int k = 0; k < 5; ++k) ph[k] = ofx_phf[k];
    int occ = ofx_occ_on; int32_t o[4] = { ofx_occ_x0, ofx_occ_y0, ofx_occ_x1, ofx_occ_y1 };
    for (int round = 0; round < 8; round++) {
        uint32_t gain = 150u + 60u * (uint32_t)round;
        int tex = round & 1, oc = (round >> 1) & 1;
        ofx_occ_on = oc; ofx_occ_x0 = cx - R / 2; ofx_occ_y0 = cy - R / 3; ofx_occ_x1 = cx + R; ofx_occ_y1 = cy + R * 2;
        ofx_build_halo(gain);
        for (int pass = 0; pass < 2; pass++) {
            uint32_t *b = pass ? b2 : b1;
            for (uint32_t i = 0; i < n; i++) b[i] = tex ? (i * 2654435761u >> 25) * 0x010101u : 0u;
            if (pass) ofx_halo_fast(b, w, h, cx, cy, R); else ofx_draw_halo(b, w, h, cx, cy, R);
        }
        for (uint32_t i = 0; i < n; i++) if (b1[i] != b2[i]) bad_h++;
        for (int k = 0; k < OFX_RIB_N; ++k) ofx_phf[k] += 0x0123456789ULL * (uint64_t)(k + 1);
        cases++;
    }
    for (int k = 0; k < 5; ++k) ofx_phf[k] = ph[k];
    (void)bad_r;
    ofx_occ_on = occ; ofx_occ_x0 = o[0]; ofx_occ_y0 = o[1]; ofx_occ_x1 = o[2]; ofx_occ_y1 = o[3];
    kfree(b1); kfree(b2);
    soma_str_put(out, max, &p, "halo por tabla: "); soma_str_put_dec(out, max, &p, bad_h); soma_str_put(out, max, &p, " pixeles distintos del halo original ("); soma_str_put_dec(out, max, &p, (uint32_t)cases); soma_str_put(out, max, &p, " casos: fondo negro/textura, con/sin ventana)\n");
    return 1;
}

static void ofx_orb_back(uint32_t ucx, uint32_t ucy)
{
    int32_t cx = (int32_t)ucx, cy = (int32_t)ucy;
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb || w <= 0 || h <= 0) return;

    ofx_frame_update(cx, cy);

    int32_t R = ofx_radius(h);
    uint32_t gain = 150u + (uint32_t)her_cur_glow + ofx_flash +
                    (uint32_t)her_pulse / 2u + (uint32_t)her_energy / 2u;
    if (gain > 600u) gain = 600u;
    {   /* PARCHE V2: ganancia suavizada, sin parpadeos bruscos */
        static int32_t gs = 0;
        if (gs == 0) gs = (int32_t)gain;
        gs += ((int32_t)gain - gs) / 3;
        gain = (uint32_t)gs;
    }

    fb_damage(cx - R * 3 / 2 - 2, cy - R * 3 / 2 - 2, R * 3 + 4, R * 3 + 4);
    ofx_build_halo(gain);
#if SOMA_WEB_PERF_LOG
    uint64_t rc0 = her153_cycles();
#endif
    if (her_eco_on) ofx_back_eco(bb, w, h, cx, cy, R, gain, &tmp_rd[6]);
    else {
        uint64_t q0 = tmp_tsc();
        ofx_draw_halo(bb, w, h, cx, cy, R);
        uint64_t q1 = tmp_tsc(); tmp_rd[6] += q1 - q0;
        ofx_ribbons_direct(bb, w, h, cx, cy, R, gain);
        tmp_rd[7] += tmp_tsc() - q1;
    }
#if SOMA_WEB_PERF_LOG
    her153_rib_cycles += her153_cycles() - rc0;
#endif
}

/* ---------- pasada delantera: anillo fotonico, destello, ondas, expulsion ---------- */
static void ofx_orb_front(uint32_t ucx, uint32_t ucy)
{
    int32_t cx = (int32_t)ucx, cy = (int32_t)ucy;
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb || w <= 0 || h <= 0) return;

    int32_t R = ofx_radius(h);
    uint32_t fl = ofx_flash;

    /* anillo fotonico con efecto Doppler (un lado mas brillante, rotando) */
    {
        static const uint8_t wt[5] = { 90, 255, 190, 110, 50 };
        uint32_t head = (ofx_ph[0] >> 11) & 1023u;
        uint32_t pc = her_mix256(0x00FFFFFFu, her_col_cur & 0x00FFFFFFu, 90u);
        uint32_t ibase = 120u + fl / 2u + (uint32_t)her_pulse / 4u;
        for (uint32_t a = 0; a < 1024u; a += 2u) {
            int32_t c = her_cs(a), s = her_sn(a);
            uint32_t dop = 128u + (uint32_t)(her_cs((a - head) & 1023u) + 1024) / 16u;
            uint32_t k = (ibase * dop) >> 8;
            if (k > 256u) k = 256u;
            uint32_t c0 = ofx_mul(pc, k);
            for (int32_t rr = 29; rr <= 33; ++rr)
                ofx_px(bb, w, h, cx + ((rr * c) >> 10), cy + ((rr * s) >> 10),
                       ofx_mul(c0, wt[rr - 29]));
        }
    }

    /* destello anamorfico horizontal */
    {
        int32_t L = R * 13 / 10;
        uint32_t str = 70u + fl / 2u + (uint32_t)her_energy / 4u;
        uint32_t sc = her_mix256(0x007DD3FCu, her_col_cur & 0x00FFFFFFu, 100u);
        for (int32_t dx = 26; dx < L; ++dx) {
            uint32_t k = (uint32_t)(256 - (dx - 26) * 256 / (L - 26));
            k = (k * k) >> 8;
            k = (k * str) >> 8;
            if (k > 256u) k = 256u;
            uint32_t c1 = ofx_mul(sc, k);
            uint32_t c2 = ofx_mul(c1, 100u);
            ofx_px(bb, w, h, cx + dx, cy, c1);
            ofx_px(bb, w, h, cx - dx, cy, c1);
            ofx_px(bb, w, h, cx + dx, cy - 1, c2);
            ofx_px(bb, w, h, cx - dx, cy - 1, c2);
            ofx_px(bb, w, h, cx + dx, cy + 1, c2);
            ofx_px(bb, w, h, cx - dx, cy + 1, c2);
        }
    }

    int any = 0;

    /* ondas de choque */
    for (int i = 0; i < OFX_RING_MAX; ++i) {
        struct ofx_ring_t *g = &ofx_rings[i];
        if (!g->alive || g->maxlife <= 0) continue;
        any = 1;
        uint32_t f = (uint32_t)g->life * 256u / (uint32_t)g->maxlife;
        uint32_t c = ofx_mul(her_mix256(0x00FFFFFFu, g->col, 110u), (f * 3u) >> 2);
        ofx_draw_ring(bb, w, h, cx, cy, g->r, c);
    }

    /* particulas expulsadas con estela */
    for (int i = 0; i < OFX_EJ_MAX; ++i) {
        struct ofx_ejecta *e = &ofx_ej[i];
        if (!e->alive || e->maxlife <= 0) continue;
        any = 1;
        uint32_t f = (uint32_t)e->life * 256u / (uint32_t)e->maxlife;   /* 256 recien nacida */
        int32_t hx = e->x >> 8, hy = e->y >> 8;
        int32_t tx = (e->x - e->vx * 2) >> 8, ty = (e->y - e->vy * 2) >> 8;
        uint32_t hot = her_mix256(0x00FFFFFFu, e->col, 256u - f);
        uint32_t c_head = ofx_mul(hot, 128u + f / 2u);
        uint32_t c_tail = ofx_mul(e->col, f / 3u);
        ofx_line(bb, w, h, tx, ty, hx, hy, c_tail, c_head);
        if (f > 100u) {
            uint32_t g1 = ofx_mul(c_head, 90u);
            ofx_px(bb, w, h, hx + 1, hy, g1); ofx_px(bb, w, h, hx - 1, hy, g1);
            ofx_px(bb, w, h, hx, hy + 1, g1); ofx_px(bb, w, h, hx, hy - 1, g1);
        }
    }

    if (any) fb_damage(0, 0, w, h);
}
/* ================= fin SOMA_ORB_FX_PATCH (A) ================= */

static int her_summon_on;                                   /* SOMA_168B_ORBE: el orbe esta al frente */
/* SOMA_207_CAPA: con ventanas abiertas, el fondo (orbe, cintas, particulas, iconos orbitando) se guarda una vez por latido y se
 * reutiliza en los repintados por teclado o raton. Escribir en una terminal ya no recalcula el halo ni las cintas. La imagen es
 * la misma: solo se deja de recalcular lo que no ha cambiado desde el ultimo latido. */
#ifndef SOMA_LAYER_CACHE
#define SOMA_LAYER_CACHE 1
#endif
static uint32_t *her_lay_px; static size_t her_lay_cap;
static int32_t her_lay_x, her_lay_y, her_lay_w, her_lay_h;
static uint64_t her_lay_sig, her_lay_ms;
static int her_lay_fresh_req;                 /* 1 = el latido pide un fondo nuevo */
uint32_t her_lay_hits, her_lay_miss;          /* para 'orbe' */
int her_lay_on = SOMA_LAYER_CACHE;            /* 'orbe capa off' lo desactiva para comparar */
static int her_lay_bypass(void)
{
    return her_lay_fresh_req || her_mfly_on || her_icons_visible || her_min_drag >= 0 || (her_dp_kind && her_dp_moved) || (her_icons_anim_phase && her_icons_anim_phase < HER_ICON_ANIM_MAX) || her_summon_on ||
           agent_global_len > 0 || her_ans_len || her_thk_len || her_vortex_closing || ofx_flash > 8u ||
           (her_hint_msg && timer_get_uptime_ms() < her_hint_until) || her_num_windows <= 0;
}
static uint64_t her_lay_signature(uint32_t w, uint32_t h)
{
    uint64_t v = 1469598103934665603ull;
#define LAYH(x) do { v ^= (uint64_t)(uint32_t)(x); v *= 1099511628211ull; } while (0)
    LAYH(w); LAYH(h); LAYH(ofx_occ_on); LAYH(ofx_occ_x0); LAYH(ofx_occ_y0); LAYH(ofx_occ_x1); LAYH(ofx_occ_y1);
    LAYH(her_desk_ver); LAYH(her_min_count); LAYH(current_app); LAYH(her_icons_visible); LAYH(her_power_pending);
    for (int i = 0; i < her_num_windows; ++i) LAYH(her_windows[i].app);
    if (her_icons_visible) {   /* el nombre al pasar el raton, el punto de 'abierta' y las tarjetas de energia dependen de esto */
        LAYH(her_dock_hit(g_mouse_x_s, g_mouse_y_s, w, h)); LAYH(her_min_hit(g_mouse_x_s, g_mouse_y_s, w, h));
        LAYH(her_power_hit(g_mouse_x_s, g_mouse_y_s, w, h));
    }
    for (int i = 0; i < her_min_count; ++i) { LAYH(her_min_phase[i]); LAYH(her_minimized[i].app); LAYH(her_minimized[i].term_slot); }
#undef LAYH
    return v;
}
static void her_lay_box(uint32_t w, uint32_t h, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    int32_t R = ofx_radius((int32_t)h);
    int32_t e = R * 3 / 2 + 8;
    int32_t r = R + 55;
    if (r < 90) r = 90;
    if (r > (int32_t)h / 2 - 40) r = (int32_t)h / 2 - 40;
    if (r < 45) r = 45;
    if (her_min_count && r + 64 > e) e = r + 64;
    *x0 = (int32_t)w / 2 - e; *x1 = (int32_t)w / 2 + e; *y0 = (int32_t)h / 2 - e; *y1 = (int32_t)h / 2 + e;
    if (*x0 < 0) { *x0 = 0; }
    if (*y0 < 0) { *y0 = 0; }
    if (*x1 > (int32_t)w) { *x1 = (int32_t)w; }
    if (*y1 > (int32_t)h) { *y1 = (int32_t)h; }
}
uint32_t her_lay_why[16];
static int her_lay_use(uint32_t w, uint32_t h)
{
    if (!her_lay_on) { her_lay_miss++; return 0; }
    if (!her_lay_px) { her_lay_miss++; her_lay_why[0]++; return 0; }
    if (her_lay_bypass()) {
        her_lay_miss++; her_lay_why[1]++;
        her_lay_why[4] += her_lay_fresh_req; her_lay_why[5] += (her_min_drag >= 0); her_lay_why[6] += her_icons_visible; her_lay_why[7] += (her_icons_anim_phase != 0);
        her_lay_why[8] += her_summon_on; her_lay_why[9] += (agent_global_len > 0); her_lay_why[10] += (her_ans_len || her_thk_len);
        her_lay_why[11] += her_vortex_closing; her_lay_why[12] += (ofx_flash > 8u);
        return 0;
    }
    uint64_t now = timer_get_uptime_ms();
    if (now - her_lay_ms > 300u) { her_lay_miss++; her_lay_why[2]++; return 0; }
    if (her_lay_sig != her_lay_signature(w, h)) { her_lay_miss++; her_lay_why[3]++; return 0; }
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb) return 0;
    if (her_eco_on) fb_clear_changed(0x00000000); else fb_clear(0x00000000);
    for (int32_t r = 0; r < her_lay_h; ++r)
        memcpy((void *)(bb + (size_t)(her_lay_y + r) * w + (size_t)her_lay_x), her_lay_px + (size_t)r * (size_t)her_lay_w, (size_t)her_lay_w * 4u);
    fb_damage(her_lay_x, her_lay_y, her_lay_w, her_lay_h);
    her_lay_hits++;
    return 1;
}
static void her_lay_store(uint32_t w, uint32_t h)
{
    if (!her_lay_on || her_num_windows <= 0 || her_summon_on || (her_lay_bypass() && !her_lay_fresh_req)) { her_lay_sig = 0; return; }
    volatile uint32_t *bb = fb_get_buffer();
    int32_t x0, y0, x1, y1; her_lay_box(w, h, &x0, &y0, &x1, &y1);
    if (!bb || x1 <= x0 || y1 <= y0) return;
    size_t need = (size_t)(x1 - x0) * (size_t)(y1 - y0) * 4u;
    if (need > her_lay_cap) {
        if (her_lay_px) kfree(her_lay_px);
        her_lay_px = (uint32_t *)kmalloc(need); her_lay_cap = her_lay_px ? need : 0;
        if (!her_lay_px) { her_lay_sig = 0; return; }
    }
    her_lay_x = x0; her_lay_y = y0; her_lay_w = x1 - x0; her_lay_h = y1 - y0;
    for (int32_t r = 0; r < her_lay_h; ++r)
        memcpy(her_lay_px + (size_t)r * (size_t)her_lay_w, (const void *)(bb + (size_t)(her_lay_y + r) * w + (size_t)her_lay_x), (size_t)her_lay_w * 4u);
    her_lay_sig = her_lay_signature(w, h); her_lay_ms = timer_get_uptime_ms();
}

/* SOMA_208: con fotogramas lentos los paquetes del raton se acumulaban y se procesaban de uno en uno por vuelta del bucle (el
 * puntero iba a trompicones y los clics llegaban tarde). Los paquetes de solo movimiento se suman en un unico evento; el primero
 * que cambia el estado del boton se entrega aparte, asi que ninguna pulsacion ni suelta se pierde. */
static int her_mouse_poll_merged(int32_t *dx, int32_t *dy, int *clk)
{
    if (!mouse_poll(dx, dy, clk)) return 0;
    int held0 = mouse_left_held();
    for (int g = 0; g < 48 && !*clk; ++g) {
        int32_t ax, ay; int ac;
        if (!mouse_poll(&ax, &ay, &ac)) break;
        *dx += ax; *dy += ay;
        if (ac || mouse_left_held() != held0) { *clk = ac; break; }
    }
    return 1;
}

static void render_desktop(void)
{
    uint64_t r0 = tmp_tsc();
    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    if (her_in_refresh && her_lay_use(cur_w, cur_h)) { tmp_rd[0] += tmp_tsc() - r0; return; }

    /* SOMA_PARCHE_012_RENDER: en escritorio puro solo se borra lo dibujado el fotograma anterior
     * (en vez de 8,3 MB). Con ventanas abiertas, o cada 60 fotogramas, borrado completo. */
    static int prev_desktop_only = 0, prev_eco = 0;
    static uint32_t partial_frames = 0;
    int desktop_only = (her_num_windows == 0 && current_app == APP_DESKTOP);
    /* SOMA_209: con ventanas, cada una declara su rectangulo (her_refresh_all). Mientras se arrastra o redimensiona una ventana
     * (y dos fotogramas despues) se borra la pantalla entera como antes: ese camino repone fondo guardado sin declararlo. */
    static int eco_hold = 0;
    if (her_dragging || her_resizing) eco_hold = 3; else if (eco_hold) eco_hold--;
    int eco = her_eco_on && her_in_refresh && !her_summon_on && !eco_hold;
    int eco_ok = eco && prev_eco;
    prev_eco = eco;
    static uint64_t full_ms = 0;                                /* SOMA_209: borrado completo de seguridad al menos una vez por segundo */
    uint64_t rd_now = timer_get_uptime_ms();
    if (rd_now - full_ms >= 1000u) partial_frames = 60;
    if (her_mfly_on) partial_frames = 60;                   /* SOMA_223: las estelas cruzan toda la pantalla */
    if (partial_frames >= 60) full_ms = rd_now;
    if (((desktop_only && prev_desktop_only) || eco_ok) && partial_frames < 60) {
        fb_clear_changed(0x00000000);
        partial_frames++;
    } else {
        fb_clear(0x00000000);
        partial_frames = 0;
    }
    prev_desktop_only = desktop_only;
    uint64_t r1 = tmp_tsc(); tmp_rd[0] += r1 - r0;

    uint32_t orb_cx = cur_w / 2;
    uint32_t orb_cy = cur_h / 2;

    if (her_summon_on) {                                    /* SOMA_168B_ORBE: el orbe se dibuja delante de las ventanas */
        if (current_app != APP_DESKTOP) her_draw_window_chrome(cur_w, cur_h);
        return;
    }
    ofx_bg_black = 1;                                       /* recien borrado: la caja del orbe es negra */
    ofx_orb_back(orb_cx, orb_cy);
    ofx_bg_black = 0;
    uint64_t r2 = tmp_tsc(); tmp_rd[1] += r2 - r1;
    her_parts_update_draw();
    uint64_t r3 = tmp_tsc(); tmp_rd[2] += r3 - r2;
    her_draw_orb(orb_cx, orb_cy, her_angle);
    uint64_t r4 = tmp_tsc(); tmp_rd[3] += r4 - r3;
    ofx_orb_front(orb_cx, orb_cy);
    uint64_t r5 = tmp_tsc(); tmp_rd[4] += r5 - r4;
    her_draw_user_zone(cur_w, cur_h, orb_cy);
    her_draw_agent_zone(cur_w, cur_h, orb_cy);
    her_draw_dock(cur_w, cur_h);
    her_mfly_draw(cur_w, cur_h);                            /* SOMA_223_MINIMIZAR: antes que los iconos (fija el brillo del que llega) */
    her_draw_minimized_icons(cur_w, cur_h);
    if (her_num_windows == 0) hst_dibujar(cur_w, cur_h);

    if (current_app != APP_DESKTOP)
        her_draw_window_chrome(cur_w, cur_h);
    her_draw_vortex_particles();
    tmp_rd[5] += tmp_tsc() - r5;
    if (her_in_refresh) her_lay_store(cur_w, cur_h);

    if (fb_verify_enabled && desktop_only && partial_frames > 0)
        (void)fb_debug_verify(0x00000000);
}

static void agents_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int cursor_visible)
{
    (void)cursor_visible;
    if (ww < 180u || wh < 160u) return;
    fb_fill_rect(wx, wy, ww, wh, 0x000B1220);
    fb_fill_rounded_rect((int32_t)(wx + 12u), (int32_t)(wy + 10u), (int32_t)(ww - 24u), 30, 5, 0x00131E2A);
    font_draw_string(wx + 24u, wy + 20u, "AGENTES DISPONIBLES", 0x00F8FAFC, 1);
    font_draw_string(wx + 24u, wy + 48u, "Jerarquia de coordinacion de SOMA", 0x0064778A, 1);

    uint32_t root_x = wx + 24u, root_y = wy + 68u;
    uint32_t card_x = wx + 54u;
    uint32_t card_w = ww > 86u ? ww - 78u : ww - 62u;
    fb_fill_rounded_rect((int32_t)root_x, (int32_t)root_y, (int32_t)(ww - 48u), 48, 6, 0x00113343);
    fb_draw_rounded_rect((int32_t)root_x, (int32_t)root_y, (int32_t)(ww - 48u), 48, 6, COLOR_CYAN);
    font_draw_string(root_x + 12u, root_y + 10u, "SOMA  -  ORQUESTADOR PRINCIPAL", COLOR_CYAN, 1);
    font_draw_string(root_x + 12u, root_y + 28u, "Coordina especialistas y deriva operaciones de sistema.", 0x00CBD5E1, 1);

    uint32_t branch_x = wx + 38u;
    fb_fill_rect(branch_x, root_y + 48u, 2u, 230u, 0x00334155);

    uint32_t y1 = wy + 132u, y2 = wy + 220u, y3 = wy + 308u;
    uint32_t ys[3] = { y1, y2, y3 };
    const char *names[3] = { "Mayordomo  -  Operaciones del sistema", "Buscador  -  Web e investigacion", "Maquetador  -  Documentos y composicion" };
    const char *descs[3] = {
        "Ejecuta rapido RAM, disco, VFS, busquedas y gestion de archivos.",
        "Investiga informacion web, actualidad y consultas de conocimiento.",
        "Organiza contenido en documentos estructurados con layout SOMA."
    };
    const uint32_t colors[3] = { 0x0034D399, 0x00F59E0B, 0x000EA5E9 };
    for (int i = 0; i < 3; ++i) {
        fb_fill_rect(branch_x, ys[i] + 26u, 16u, 2u, 0x00334155);
        fb_fill_rounded_rect((int32_t)card_x, (int32_t)ys[i], (int32_t)card_w, 66, 6, 0x00101827);
        fb_draw_rounded_rect((int32_t)card_x, (int32_t)ys[i], (int32_t)card_w, 66, 6, colors[i]);
        font_draw_string(card_x + 12u, ys[i] + 12u, names[i], colors[i], 1);
        font_draw_string(card_x + 12u, ys[i] + 34u, descs[i], 0x00CBD5E1, 1);
    }
    font_draw_string(wx + 22u, wy + wh - 20u, "Tareas comunes: RAM | Disco | Buscar archivo | Crear carpeta", 0x0064778A, 1);
}

static void her_render_app_body(enum app_mode app, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int is_focused)
{
    uint8_t own_prev = acct_set_owner((uint8_t)(ACCT_APP0 + app));   /* PARCHE 089 */
    fb_set_clip((int32_t)wx, (int32_t)wy, (int32_t)ww, (int32_t)wh);
#if defined(__x86_64__)
    if (gui_rescue_inject) {                                      /* SOMA_221_RESCATE: 'escritorio rescate memoria|cuelgue' */
        int k = gui_rescue_inject; gui_rescue_inject = 0;
        if (k == 1) { volatile uintptr_t nul = 8; *(volatile int *)nul = 1; }
        else for (volatile uint32_t spin = 0; ; spin++) __asm__ volatile ("pause");
    }
#endif
    switch (app) {
    case APP_TERMINAL_SHELL:
        render_real_terminal(wx, wy, ww, wh, cur_cmd, cur_cmd_len, cur_cursor_pos, is_focused);
        break;
    case APP_AGENT_CHAT:
        render_log_window_chat(agent_log, agent_log_count, wx, wy, ww,
                               wh > 56 ? wh - 56 : wh);
        render_input_bar_chat("soma> ", COLOR_CYAN, wx, wy + wh - 46, ww, 46,
                              agent_global_cmd, agent_global_len,
                              agent_global_cursor, is_focused && agent_global_focus);
        break;
    case APP_WEB_BROWSER:   browser_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_IMAGE_VIEWER:  imgview_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_FILE_MANAGER:  fm_render_window(wx, wy, ww, wh, is_focused);      break;
    case APP_NOTEPAD:       notepad_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_AGENT_MEMORY:  memview_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_LAYOUT:        layout_render_window(wx, wy, ww, wh, is_focused);  break;
        case APP_PAINT:         paint_render_window(wx, wy, ww, wh, is_focused);   break;
    case APP_CODER_APP:      coder_app_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_MEDIA:          media_render_window(wx, wy, ww, wh, is_focused);   break;
    case APP_YOUTUBE:        youtube_render_window(wx, wy, ww, wh, is_focused); break;   /* SOMA_250_YOUTUBE */
    case APP_AGENTS:        agents_render_window(wx, wy, ww, wh, is_focused);  break;
    case APP_SOMA_APP:      gui_soma_win_render_body(wx, wy, ww, wh);          break; /*PL12_RENDER_BODY*/
    case APP_SETTINGS:      settings_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_PDF:           pdfview_render_window(wx, wy, ww, wh, is_focused); break;   /* PARCHE 088 */
    case APP_SYSMON:        sysmon_render_window(wx, wy, ww, wh, is_focused);  break;   /* PARCHE 089 */
    case APP_RECORDER:      recorder_render_window(wx, wy, ww, wh, is_focused); break;  /* SOMA_177_T480S */
    case APP_CAMERA:        camapp_render_window(wx, wy, ww, wh, is_focused); break;     /* SOMA_178_T480S */
    case APP_CAPTURE:       capapp_render_window(wx, wy, ww, wh, is_focused); break;     /* SOMA_179_T480S */
    default: break;
    }
    fb_reset_clip();
    acct_set_owner(own_prev);
}

/* SOMA_152_WEB_FLUIDO: compositor por demanda para el navegador.
 * Antes, con cualquier ventana abierta, el latido de 30 Hz borraba y recomponia la pantalla entera (fondo animado, sombras,
 * marcos y el cuerpo de cada ventana) y la volcaba completa: ~22 ms de cada 33 ms en QEMU (~70 % de la CPU), robados al
 * hilo del navegador (red, HTML, CSS, JS, maquetacion) y sumados como latencia a cada vuelta de rueda.
 * Ahora, si la ventana de arriba es el navegador:
 *   - la ventana se repinta SOLA y solo cuando su huella cambia (browser_frame_dirty), hasta 100 fotogramas/s;
 *   - el escritorio de fondo (estrellas, orbe) baja a SOMA_GUI_BG_MS (por defecto 15 fotogramas/s desde el 153B) y se congela del todo
 *     mientras el usuario usa el navegador o la pagina carga (browser_hot), como hace un compositor con las capas ocultas;
 *   - cualquier cambio de escena (mover/redimensionar, abrir/cerrar ventanas, foco, ajustes) recompone todo al instante.
 * Con -DSOMA_WEB_FLUIDO=0 vuelve exactamente el comportamiento anterior. */
#ifndef SOMA_WEB_FLUIDO
#define SOMA_WEB_FLUIDO 1
#endif
#ifndef SOMA_GUI_BG_MS
#define SOMA_GUI_BG_MS 66
#endif
#ifndef SOMA_WEB_FRAME_MS
#define SOMA_WEB_FRAME_MS 10
#endif
static uint64_t her152_scene_key(void)
{
    uint64_t k = 1469598103934665603ull;
    #define MIX(v) do { k ^= (uint64_t)(v); k *= 1099511628211ull; } while (0)
    MIX(her_num_windows); MIX(current_app); MIX(fb_get_width()); MIX(fb_get_height()); MIX(settings_opacity()); MIX(settings_active_opaque()); MIX(font_get_current()); MIX(wf_ui_gen());   /* SOMA_184: cambiar de fuente invalida la escena */
    MIX(her_icons_visible); MIX(agent_global_focus); MIX(agent_log_count); MIX(her_ans_len); MIX(her_thk_len); MIX(her_agent_busy);
    for (int i = 0; i < her_num_windows; ++i) { MIX(her_windows[i].app); MIX(her_windows[i].x); MIX(her_windows[i].y); MIX(her_windows[i].w); MIX(her_windows[i].h); }
    #undef MIX
    return k;
}
static int her152_eligible(void)
{
#if SOMA_WEB_FLUIDO
    if (her_num_windows <= 0 || current_app != APP_WEB_BROWSER) return 0;
    if (her_windows[her_num_windows - 1].app != APP_WEB_BROWSER) return 0;
    if (her_dragging || her_resizing || her_vortex_closing) return 0;
    if (her_icons_anim_phase != 0u && her_icons_anim_phase != HER_ICON_ANIM_MAX) return 0;   /* transicion del dock en marcha */
    return 1;
#else
    return 0;
#endif
}

#ifndef SOMA_WEB_PERF_LOG
#define SOMA_WEB_PERF_LOG 0
#endif
#if SOMA_WEB_PERF_LOG
/* SOMA_152: con -DSOMA_WEB_PERF_LOG=1, cada 3 s por el puerto serie: fotogramas completos del escritorio y fotogramas
 * solo-navegador, con su coste. Sirve para comparar antes/despues en la maquina real. */
static uint32_t pf152_full_n, pf152_full_ms, pf152_web_n, pf152_web_ms, pf152_web_max; static uint64_t pf152_t0;
static void pf152_note(int web, uint32_t ms)
{
    if (web) { pf152_web_n++; pf152_web_ms += ms; if (ms > pf152_web_max) pf152_web_max = ms; }
    else { pf152_full_n++; pf152_full_ms += ms; }
    uint64_t now = timer_get_uptime_ms();
    if (!pf152_t0) pf152_t0 = now;
    if (now - pf152_t0 >= 3000) {
        kprint("[web152] 3s: escritorio="); kprint_dec(pf152_full_n); kprint(" fot/"); kprint_dec(pf152_full_ms);
        kprint("ms navegador="); kprint_dec(pf152_web_n); kprint(" fot/"); kprint_dec(pf152_web_ms); kprint("ms (max ");
        kprint_dec(pf152_web_max); kprint("ms)\n");
        pf152_full_n = pf152_full_ms = pf152_web_n = pf152_web_ms = pf152_web_max = 0; pf152_t0 = now;
    }
}
#define PF152(web, ms) pf152_note((web), (ms))
#else
#define PF152(web, ms) ((void)0)
#endif
#ifndef SOMA_WEB_PERF_LOG
#define SOMA_WEB_PERF_LOG 0
#endif
static int s153_mode = 1;                                 /* con SOMA_WEB_PERF_LOG se alterna para comparar */
static void her153_set_occlusion(void)
{
    ofx_occ_on = 0;
    if (!SOMA_ORB_OCCLUSION || !s153_mode || her_num_windows <= 0) return;
    struct her_win_node *t = &her_windows[her_num_windows - 1];
    uint32_t alpha = (uint32_t)settings_opacity() * 255u / 100u;
    if (alpha < 255u && !settings_active_opaque()) return;       /* ventana translucida: el fondo se ve, dibujar todo */
    int32_t m = HER_WIN_R + 2;
    if ((int32_t)t->w <= 2 * m || (int32_t)t->h <= 2 * m) return;
    ofx_occ_x0 = (int32_t)t->x + m; ofx_occ_y0 = (int32_t)t->y + m;
    ofx_occ_x1 = (int32_t)(t->x + t->w) - m; ofx_occ_y1 = (int32_t)(t->y + t->h) - m;
    ofx_occ_on = 1;
}
#if SOMA_WEB_PERF_LOG
static uint64_t s153_t0, s153_cyc[2]; static uint32_t s153_n[2];
static void her153_note(uint64_t cyc)
{
    uint64_t now = timer_get_uptime_ms();
    if (!s153_t0) s153_t0 = now;
    s153_cyc[s153_mode] += cyc; s153_n[s153_mode]++;
    if (now - s153_t0 >= 6000) {
        kprint("[desk153] ocultar_orbe="); kprint_dec((uint32_t)s153_mode); kprint(" fotogramas="); kprint_dec(s153_n[s153_mode]);
        kprint(" coste_medio_kciclos="); kprint_dec(s153_n[s153_mode] ? (uint32_t)((s153_cyc[s153_mode] / s153_n[s153_mode]) >> 10) : 0);
        kprint(" cintas_kciclos="); kprint_dec(s153_n[s153_mode] ? (uint32_t)((her153_rib_cycles / s153_n[s153_mode]) >> 10) : 0); kprint("\n");
        her153_rib_cycles = 0;
        s153_cyc[s153_mode] = 0; s153_n[s153_mode] = 0; s153_mode = !s153_mode; ofx_rib_mode = s153_mode; s153_t0 = now;
    }
}
static inline uint64_t her153_cycles(void)
{
#if defined(__x86_64__)
    return __builtin_ia32_rdtsc();
#else
    uint64_t v; __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(v)); return v;
#endif
}
#endif
/* SOMA_209_ECO (3): contenido de ventana sin cambios = copia de su ultima pintura. Se reutiliza solo si: la aplicacion esta en la
 * lista, la ventana no ha cambiado de sitio, tamano ni foco, no ha habido teclado ni raton en 150 ms, la pintura tiene menos de
 * HER_BC_MS y nadie ha dibujado en esa zona de la pantalla desde el ultimo fotograma (suma de comprobacion del backbuffer).
 * Lo que una aplicacion anima por reloj (el parpadeo del cursor de texto) puede llegar hasta HER_BC_MS tarde: medido, 1 de cada
 * 8 copias. 'orbe ventanas off' lo desactiva. */
#ifndef HER_BC_MS
#define HER_BC_MS 70u
#endif
int her_bc_on = 1;
struct her_bc { int used; enum app_mode app; int slot; uint32_t x, y, w, h; int top; uint32_t *px; size_t cap; uint64_t t, sum; int sum_ok; uint32_t ver, gen; };
/* SOMA_216_RENDIMIENTO: aplicaciones que dicen cuando cambia su contenido. Con ellas la copia vale hasta que cambie la version,
 * sin limite de tiempo, haya o no actividad del raton, y sin sumas de comprobacion. */
static int her_app_version(enum app_mode a, uint32_t *v)
{
    switch (a) { case APP_SYSMON: *v = sysmon_version(); return 1; default: return 0; }
}
static uint32_t her_bc_gen(void) { return wf_ui_gen() ^ ((uint32_t)settings_opacity() << 20) ^ ((uint32_t)settings_active_opaque() << 28); }
static struct her_bc her_bcs[HER_MAX_WINDOWS];
static int her_bc_base_ok; static uint64_t her_bc_input_ms;
static int her_bc_app_ok(enum app_mode a)
{
    return a == APP_TERMINAL_SHELL || a == APP_NOTEPAD || a == APP_FILE_MANAGER || a == APP_IMAGE_VIEWER || a == APP_AGENT_CHAT ||
           a == APP_AGENT_MEMORY || a == APP_AGENTS || a == APP_SETTINGS || a == APP_PDF || a == APP_LAYOUT || a == APP_SYSMON;
}
static struct her_bc *her_bc_find(enum app_mode app, int slot, int create)
{
    for (int i = 0; i < HER_MAX_WINDOWS; ++i) if (her_bcs[i].used && her_bcs[i].app == app && her_bcs[i].slot == slot) return &her_bcs[i];
    if (create) for (int i = 0; i < HER_MAX_WINDOWS; ++i) if (!her_bcs[i].used) {
        her_bcs[i].used = 1; her_bcs[i].app = app; her_bcs[i].slot = slot; her_bcs[i].px = 0; her_bcs[i].cap = 0; her_bcs[i].sum_ok = 0; her_bcs[i].t = 0;
        return &her_bcs[i];
    }
    return 0;
}
static int her_bc_rect(const struct her_win_node *win, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = (int32_t)HER_CLI_X(win->x); *y = (int32_t)HER_CLI_Y(win->y); *w = (int32_t)HER_CLI_W(win->w); *h = (int32_t)HER_CLI_H(win->h);
    return *x >= 0 && *y >= 0 && *w > 8 && *h > 8 && *x + *w <= (int32_t)fb_get_width() && *y + *h <= (int32_t)fb_get_height();
}
static uint64_t her_bc_sum(int32_t x, int32_t y, int32_t w, int32_t h)
{
    volatile uint32_t *bb = fb_get_buffer(); uint32_t W = fb_get_width();
    uint64_t s = 0x9E3779B97F4A7C15ull;
    if (!bb) return 0;
    for (int32_t r = 0; r < h; ++r) {
        const uint32_t *p = (const uint32_t *)(bb + (size_t)(y + r) * W + (size_t)x);
        uint64_t a = (uint64_t)r;
        for (int32_t i = 0; i < w; ++i) a = ((a << 7) | (a >> 57)) + p[i];
        s = (s ^ a) * 0x100000001B3ull;
    }
    return s;
}
static void her_bc_baseline(void)                              /* tras volcar un fotograma completo */
{
    her_bc_base_ok = 0;
    if (!her_eco_on) return;
    for (int i = 0; i < HER_MAX_WINDOWS; ++i) {                  /* soltar la memoria de las ventanas que ya no estan abiertas */
        struct her_bc *e = &her_bcs[i]; int open = 0;
        if (!e->used) continue;
        for (int k = 0; k < her_num_windows; ++k) if (her_windows[k].app == e->app && her_windows[k].term_slot == e->slot) open = 1;
        if (!open) { if (e->px) kfree(e->px); e->px = 0; e->cap = 0; e->used = 0; }
    }
    for (int k = 0; k < her_num_windows; ++k) {
        struct her_bc *e = her_bc_find(her_windows[k].app, her_windows[k].term_slot, 0);
        int32_t x, y, w, h;
        if (!e) continue;
        e->sum_ok = 0;
        { uint32_t v; if (her_app_version(her_windows[k].app, &v)) continue; }
        if (!e->px || !her_bc_rect(&her_windows[k], &x, &y, &w, &h)) continue;
        e->sum = her_bc_sum(x, y, w, h); e->sum_ok = 1;
    }
    her_bc_base_ok = 1;
}
static int her_bc_live(const struct her_win_node *win, int top, uint64_t now)   /* antes de borrar el fotograma anterior */
{
    int32_t x, y, w, h;
    if (!her_bc_app_ok(win->app) || !her_bc_rect(win, &x, &y, &w, &h)) return 0;
    struct her_bc *e = her_bc_find(win->app, win->term_slot, 0);
    if (!e || !e->px || e->top != top || e->x != win->x || e->y != win->y || e->w != win->w || e->h != win->h) return 0;
    { uint32_t v; if (her_app_version(win->app, &v)) return v == e->ver && e->gen == her_bc_gen(); }
    if (!e->sum_ok) return 0;
    if (now - e->t >= HER_BC_MS) return 0;
    return her_bc_sum(x, y, w, h) == e->sum;
}
static void her_bc_store(const struct her_win_node *win, int top, uint64_t now)
{
    int32_t x, y, w, h;
    uint32_t ver = 0; int versioned = her_app_version(win->app, &ver);
    if (!her_eco_on || !her_bc_on) return;
    if (!versioned && (her_eco_period >= HER_BC_MS || now - her_bc_input_ms < 150u)) return;   /* no se podria reutilizar: no copiar */
    if (!her_bc_app_ok(win->app) || !her_bc_rect(win, &x, &y, &w, &h)) return;
    struct her_bc *e = her_bc_find(win->app, win->term_slot, 1);
    volatile uint32_t *bb = fb_get_buffer(); uint32_t W = fb_get_width();
    if (!e || !bb) return;
    size_t need = (size_t)w * (size_t)h * 4u;
    if (need > e->cap) {
        if (e->px) kfree(e->px);
        e->px = (uint32_t *)kmalloc(need); e->cap = e->px ? need : 0;
        if (!e->px) return;
    }
    for (int32_t r = 0; r < h; ++r) memcpy(e->px + (size_t)r * (size_t)w, (const void *)(bb + (size_t)(y + r) * W + (size_t)x), (size_t)w * 4u);
    e->x = win->x; e->y = win->y; e->w = win->w; e->h = win->h; e->top = top; e->t = now; e->sum_ok = 0; e->ver = ver; e->gen = her_bc_gen();
}
static uint32_t her_bc_put(const struct her_win_node *win, int compare_only)
{
    int32_t x, y, w, h; uint32_t bad = 0;
    struct her_bc *e = her_bc_find(win->app, win->term_slot, 0);
    volatile uint32_t *bb = fb_get_buffer(); uint32_t W = fb_get_width();
    if (!e || !e->px || !bb || !her_bc_rect(win, &x, &y, &w, &h)) return 0;
    for (int32_t r = 0; r < h; ++r) {
        uint32_t *d = (uint32_t *)(bb + (size_t)(y + r) * W + (size_t)x); const uint32_t *sp = e->px + (size_t)r * (size_t)w;
        if (compare_only) { for (int32_t i = 0; i < w; ++i) if (d[i] != sp[i]) bad++; }
        else memcpy(d, sp, (size_t)w * 4u);
    }
    if (!compare_only) fb_damage(x, y, w, h);
    return bad;
}

static void her_refresh_all(void)
{
    uint64_t t0 = tmp_tsc();
    int bc_live[HER_MAX_WINDOWS];
    uint64_t bc_now = timer_get_uptime_ms();
    {
        int base = her_bc_base_ok; her_bc_base_ok = 0;
        for (int i = 0; i < HER_MAX_WINDOWS; ++i) bc_live[i] = 0;
        if (her_eco_on && her_bc_on && !her_summon_on)
            for (int i = 0; i < her_num_windows; ++i) {
                uint32_t v;
                if (her_app_version(her_windows[i].app, &v) || (base && bc_now - her_bc_input_ms >= 150u))
                    bc_live[i] = her_bc_live(&her_windows[i], i == her_num_windows - 1, bc_now);
            }
    }
    her_in_refresh = 1;
    her153_set_occlusion();                                 /* SOMA_153 */
    render_desktop();
    tmp_pf[0] += tmp_tsc() - t0; tmp_pfn++;
    ofx_occ_on = 0;
    her_in_refresh = 0;
    fb_cguard_keep(0);
    if (settings_opacity() >= 100) fb_tr_release();             /* PARCHE 086: sin transparencia no se guardan las dos capas de pantalla */
    if (her_num_windows == 0) { fb_tr_off(); return; }

    for (int i = 0; i < her_num_windows; ++i) {
        struct her_win_node *win = &her_windows[i];
        int top = (i == her_num_windows - 1);
        struct fb_cguard *cg = &her_cguards[i];

        /* SOMA_PARCHE_028_UI: sombra -> se guarda el fondo de las esquinas inferiores -> marco -> cuerpo -> se recompone el arco */
        uint64_t t1 = tmp_tsc();
        her_draw_window_shadow(win, top);
        /* PARCHE 085: transparencia. La activa se mezcla en cada volcado (fb_tr_*); las de detras, una vez aqui. */
        uint32_t alpha = (uint32_t)settings_opacity() * 255u / 100u;
        uint32_t *under = 0;
        if (alpha < 255 && !(top && settings_active_opaque())) {
            if (top) fb_tr_begin((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h, alpha);
            else under = fb_rect_save((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h);
        } else if (top) fb_tr_off();
        fb_cguard_capture(cg, (int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h, HER_WIN_R);
        her_draw_one_window_chrome(win, top);
        uint64_t t2 = tmp_tsc(); tmp_pf[1] += t2 - t1;
        if (bc_live[i] && !her_eco_verify) { her_bc_put(win, 0); her_eco_stat[2]++; }
        else {
            her_win_focus(win->app, win->term_slot);
            her_render_app_body(win->app, HER_CLI_X(win->x), HER_CLI_Y(win->y), HER_CLI_W(win->w), HER_CLI_H(win->h), top);
            if (bc_live[i]) { if (her_bc_put(win, 1)) her_eco_stat[4]++; her_eco_stat[2]++; } else her_eco_stat[3]++;
            her_bc_store(win, top, bc_now);
        }
        uint64_t t3 = tmp_tsc(); tmp_pf[2] += t3 - t2;
        if (under) fb_rect_blend_saved(under, (int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h, alpha);
        tmp_pf[3] += tmp_tsc() - t3;
        fb_cguard_apply(cg, her_win_border_color(top));
        if (top) fb_cguard_keep(cg);          /* la ventana activa se repinta sola entre fotogramas: fb_present repone sus esquinas */
        if (her_eco_on) fb_damage((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h);   /* SOMA_209: para el borrado por zonas */
    }
    { struct her_win_node *last = &her_windows[her_num_windows - 1];
      her_win_focus(last->app, last->term_slot); }
    hst_dibujar(fb_get_width(), fb_get_height());
}

static void her_open_app(enum app_mode app, uint32_t cur_w, uint32_t cur_h)
{
    int existing = her_multi_app(app) ? -1 : her_find_window(app);
    if (existing >= 0) {
        her_bring_to_front(existing);
        agent_global_focus = (app == APP_AGENT_CHAT) ? 1 : 0;
        return;
    }
    if (!her_multi_app(app)) {
        int mi = her_find_minimized(app);
        if (mi >= 0) { her_restore_minimized(mi); return; }
    }

    if (her_num_windows >= HER_MAX_WINDOWS) return; /* No destruir silenciosamente otra ventana. */
    int terminal_slot = 0;
    if (app == APP_TERMINAL_SHELL) {
        terminal_slot = her_term_alloc();
        if (!terminal_slot) return;
        if (!term_ui_select(terminal_slot)) { her_term_kill(terminal_slot); return; }
    }
    if (her_multi_app(app) && app != APP_TERMINAL_SHELL) {    /* SOMA_211_NOTAS / SOMA_212: cada ventana, su documento */
        terminal_slot = her_inst_new(app);
        if (!terminal_slot) {
            her_hint_msg = "No caben mas ventanas de esta aplicacion: cierra alguna arrastrandola al agujero negro";
            her_hint_until = timer_get_uptime_ms() + 3000u;
            return;
        }
    }

    int idx = her_num_windows++;
    her_windows[idx].app = app;
    her_windows[idx].term_slot = terminal_slot;
    her_windows[idx].dragging = 0;
    her_windows[idx].resizing = 0;

    her_fit_window_to_orbit(cur_w, cur_h);
    her_windows[idx].w = her_win_w;
    her_windows[idx].h = her_win_h;
    {   /* SOMA_PARCHE_V2_GUI: tamano minimo por aplicacion al abrir */
        uint32_t mw = her_app_min_w(app), mh = (app == APP_PAINT || app == APP_LAYOUT || app == APP_SETTINGS || app == APP_PDF || app == APP_SYSMON || app == APP_CODER_APP || app == APP_RECORDER || app == APP_CAMERA || app == APP_CAPTURE || app == APP_MEDIA || app == APP_YOUTUBE) ? 600u : HER_MIN_H + 60u;
        if (cur_w > 64u && mw > cur_w - 32u) mw = cur_w - 32u;
        if ((app == APP_PAINT || app == APP_LAYOUT || app == APP_SETTINGS || app == APP_PDF || app == APP_SYSMON || app == APP_CODER_APP || app == APP_RECORDER || app == APP_CAMERA || app == APP_CAPTURE || app == APP_MEDIA || app == APP_YOUTUBE) && cur_h > 96u && mh > cur_h - 60u) mh = cur_h - 60u;
        if (her_windows[idx].w < mw) her_windows[idx].w = mw;
        if (cur_h > 96u && mh <= cur_h - 48u && her_windows[idx].h < mh) her_windows[idx].h = mh;
        if ((app == APP_WEB_BROWSER || app == APP_PDF || app == APP_IMAGE_VIEWER || app == APP_SYSMON || app == APP_CODER_APP || app == APP_YOUTUBE) && cur_w > 400u && cur_h > 300u) {   /* PARCHE 088: tambien PDF e imagenes */   /* PARCHE 086: el navegador abre grande (2/3 de ancho, 85 % de alto) */
            uint32_t bw = cur_w * 2u / 3u, bh = cur_h * 85u / 100u;
            if (bw > 1400u) bw = 1400u;
            if (bw < mw) bw = mw;
            if (bw > cur_w - 32u) bw = cur_w - 32u;
            if (bh > cur_h - 56u) bh = cur_h - 56u;
            if (her_windows[idx].w < bw) her_windows[idx].w = bw;
            if (her_windows[idx].h < bh) her_windows[idx].h = bh;
        }
    }

    uint32_t cascade_off = (uint32_t)(idx * 28);
    int corner = (int)app;
    uint32_t base_x = ((corner & 1) == 0 || cur_w <= her_windows[idx].w + 32u) ? 20u : (cur_w - her_windows[idx].w - 20u);
    uint32_t base_y = ((corner & 2) == 0 || cur_h <= her_windows[idx].h + 48u) ? 36u : (cur_h - her_windows[idx].h - 20u);

    if ((corner & 1) == 0) base_x += cascade_off; else base_x = (base_x > cascade_off) ? base_x - cascade_off : 10u;
    if ((corner & 2) == 0) base_y += cascade_off; else base_y = (base_y > cascade_off) ? base_y - cascade_off : 30u;

    if (base_x + her_windows[idx].w > cur_w) base_x = cur_w - her_windows[idx].w - 10u;
    if (base_y + her_windows[idx].h > cur_h) base_y = cur_h - her_windows[idx].h - 10u;

    her_windows[idx].x = base_x;
    her_windows[idx].y = base_y;

    her_win_x = her_windows[idx].x;
    her_win_y = her_windows[idx].y;
    her_win_w = her_windows[idx].w;
    her_win_h = her_windows[idx].h;

    her_active_win_idx = idx;
    current_app = app;
    agent_global_focus = (app == APP_AGENT_CHAT) ? 1 : 0;
    her_win_focus(app, terminal_slot);
    if (terminal_slot && app == APP_TERMINAL_SHELL) term_init_banner(0); /* Historia nueva solo en una instancia nueva. */

    switch (app) {
    case APP_WEB_BROWSER:  browser_init();  break;
    case APP_FILE_MANAGER: if (!her_open_noinit) fm_init(); break;   /* SOMA_214: no escanear /workspace si viene una carpeta */
    case APP_IMAGE_VIEWER: if (!her_open_noinit) imgview_init(); break;   /* SOMA_212: no cargar la de muestra si viene un archivo */
    case APP_NOTEPAD:      notepad_init();  break;
    case APP_AGENT_MEMORY: memview_init();  break;
    case APP_LAYOUT:       layout_init();   break;          /* PARCHE 084: conserva el documento abierto */
    case APP_PDF:          pdfview_init();  break;          /* PARCHE 088 */
    case APP_SYSMON:       sysmon_init();   break;          /* PARCHE 089 */
    case APP_RECORDER:     recorder_init(); break;          /* SOMA_177_T480S */
    case APP_CAMERA:       camapp_init(); break;            /* SOMA_178_T480S */
    case APP_CAPTURE:      capapp_init(); break;            /* SOMA_179_T480S */
    case APP_PAINT:        paint_ensure();  break;          /* PARCHE 083: conserva el dibujo al reabrir */
    case APP_CODER_APP:   coder_app_init(); break;
    case APP_MEDIA:       media_init();     break;
    case APP_YOUTUBE:     youtube_init();   break;          /* SOMA_250_YOUTUBE */
    default: break;
    }
}
