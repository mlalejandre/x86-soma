/* gui_orbe_fx.c: efectos del orbe: particulas, halo y cintas.
 * SOMA_234_GUI_PARTIDO: NO se compila por separado. Lo incluye src/ui/gui.c en su sitio (una sola unidad de
 * compilacion: comparte el estado del escritorio con gui.c, y el nucleo compilado sale identico). */
/* ====================================================================
 * SOMA_ORB_FX_PATCH (A): orbe estilo XMB/PS3, absorcion y expulsion
 * Todo en enteros. Mezcla ADITIVA directamente sobre el backbuffer.
 * ==================================================================== */
#define OFX_LEVEL 3
#if OFX_LEVEL >= 3
#define OFX_HALO_STEP 1
#define OFX_RIB_N     5
#define OFX_RIB_STEP  1
#define OFX_BURST_APP 170
#define OFX_BURST_ICO 150
#elif OFX_LEVEL == 2
#define OFX_HALO_STEP 1
#define OFX_RIB_N     5
#define OFX_RIB_STEP  2
#define OFX_BURST_APP 110
#define OFX_BURST_ICO 100
#else
#define OFX_HALO_STEP 2
#define OFX_RIB_N     3
#define OFX_RIB_STEP  3
#define OFX_BURST_APP 60
#define OFX_BURST_ICO 60
#endif

#define OFX_EJ_MAX   384
#define OFX_RING_MAX 8

struct ofx_ejecta {
    int32_t  x, y, vx, vy;          /* 24.8 */
    int16_t  life, maxlife;
    int8_t   turn;
    uint8_t  alive;
    uint32_t col;
};

struct ofx_ring_t {
    int32_t  r, speed;
    int16_t  life, maxlife;
    uint32_t col;
    uint8_t  alive;
};

struct ofx_rib_cfg {
    int16_t  rad, amp;              /* radio y amplitud (px a 1080p) */
    uint8_t  harm, sq;              /* armonicos de la onda, aplastamiento /256 */
    int16_t  tilt;                  /* inclinacion base (1024 = 360 grados) */
    uint8_t  wid, alpha;            /* semi-ancho maximo, opacidad */
    int32_t  spd;                   /* velocidad (con signo) */
    uint32_t col;
};

static const struct ofx_rib_cfg ofx_rib[5] = {
    { 150, 14, 3, 110,  -40,  9, 200,  300, 0x009FD8FFu },
    { 190, 20, 2,  90,   30, 12, 170, -420, 0x004F8CFFu },
    { 235, 26, 4,  80,  -15, 14, 150,  520, 0x008B5CF6u },
    { 175, 12, 5, 130,   70,  6, 150, -240, 0x0038BDF8u },
    { 280, 30, 3,  70,   10, 16, 120,  380, 0x00E0F2FEu },
};

static const uint32_t ofx_pal[10] = {
    0x0038BDF8u, 0x0010B981u, 0x00F59E0Bu, 0x0038BDF8u, 0x0010B981u,
    0x00F472B6u, 0x00A855F7u, 0x000EA5E9u, 0x0034D399u, 0x00F59E0Bu
};

static struct ofx_ejecta  ofx_ej[OFX_EJ_MAX];
static struct ofx_ring_t  ofx_rings[OFX_RING_MAX];
static int       ofx_ej_next = 0;
static int       ofx_ring_next = 0;
static uint32_t  ofx_flash = 0;
static uint32_t  ofx_absorb_n = 0;
static uint32_t  ofx_ph[5];
static uint64_t  ofx_phf[5];      /* fase con 16 bits fraccionarios (PARCHE V2) */
static int32_t   ofx_sp_f = 0;    /* velocidad suavizada x256 */
static uint32_t  ofx_last_ms = 0;
static uint32_t  ofx_acc = 0;
static int       ofx_prev_icons = 0;
static int       ofx_prev_app = -1;
static int       ofx_jet_left = 0;
static uint32_t  ofx_jet_col = 0;
static int32_t   ofx_jet_dx = 0, ofx_jet_dy = 0;
static uint32_t  ofx_halo[256];

static inline uint32_t ofx_mul(uint32_t c, uint32_t k)      /* k 0..256 */
{
    if (k >= 256u) return c & 0x00FFFFFFu;
    uint32_t r = (((c >> 16) & 255u) * k) >> 8;
    uint32_t g = (((c >> 8) & 255u) * k) >> 8;
    uint32_t b = ((c & 255u) * k) >> 8;
    return (r << 16) | (g << 8) | b;
}

/* SOMA_153_ORBE_OCULTO: rectangulo de pantalla que la ventana de arriba va a tapar por completo en este fotograma
 * (solo si es OPACA; con transparencia no hay rectangulo y el orbe se dibuja entero como antes). Lo que el orbe pinte
 * ahi se sobrescribe despues sin llegar a verse, asi que no se calcula. Se recorta en el radio de las esquinas
 * redondeadas para que por ellas se siga viendo el fondo. Solo vale dentro de her_refresh_all. */
#ifndef SOMA_ORB_OCCLUSION
#define SOMA_ORB_OCCLUSION 1
#endif
static int ofx_occ_on; static int32_t ofx_occ_x0, ofx_occ_y0, ofx_occ_x1, ofx_occ_y1;
static int ofx_rib_mode = 1;                              /* SOMA_153B: 1 = cintas a media resolucion (en la comparacion se alterna) */
#if SOMA_WEB_PERF_LOG
static uint64_t her153_rib_cycles;
static inline uint64_t her153_cycles(void);
#endif
static inline int ofx_occluded(int32_t x, int32_t y)
{
    return ofx_occ_on && x >= ofx_occ_x0 && x < ofx_occ_x1 && y >= ofx_occ_y0 && y < ofx_occ_y1;
}
static inline void ofx_px(volatile uint32_t *bb, int32_t w, int32_t h,
                          int32_t x, int32_t y, uint32_t a)
{
    if (x < 0 || y < 0 || x >= w || y >= h || !a) return;
    if (ofx_occluded(x, y)) return;
    volatile uint32_t *p = bb + (uint32_t)y * (uint32_t)w + (uint32_t)x;
    uint32_t d = *p;
    uint32_t r = ((d >> 16) & 255u) + ((a >> 16) & 255u);
    uint32_t g = ((d >> 8) & 255u) + ((a >> 8) & 255u);
    uint32_t b = (d & 255u) + (a & 255u);
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    *p = (r << 16) | (g << 8) | b;
}

/* Linea aditiva con degradado de color c0 (inicio) -> c1 (fin) y borde suave */
static void ofx_line(volatile uint32_t *bb, int32_t w, int32_t h,
                     int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                     uint32_t c0, uint32_t c1)
{
    int32_t dx = x1 - x0, dy = y1 - y0;
    int32_t adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int32_t n = adx > ady ? adx : ady;
    if (n > 420) return;
    if (n == 0) { ofx_px(bb, w, h, x0, y0, c1); return; }
    int horiz = adx > ady;
    for (int32_t i = 0; i <= n; ++i) {
        int32_t x = x0 + dx * i / n;
        int32_t y = y0 + dy * i / n;
        uint32_t c = her_mix256(c0, c1, (uint32_t)(256 * i / n));
        ofx_px(bb, w, h, x, y, c);
        uint32_t s = ofx_mul(c, 90u);
        if (horiz) ofx_px(bb, w, h, x, y + 1, s);
        else       ofx_px(bb, w, h, x + 1, y, s);
    }
}

static void ofx_draw_ring(volatile uint32_t *bb, int32_t w, int32_t h,
                          int32_t cx, int32_t cy, int32_t r, uint32_t col)
{
    if (r < 2) return;
    uint32_t stp = (r < 60) ? 4u : ((r < 120) ? 2u : 1u);
    uint32_t soft = ofx_mul(col, 120u);
    for (uint32_t a = 0; a < 1024u; a += stp) {
        int32_t c = her_cs(a), s = her_sn(a);
        ofx_px(bb, w, h, cx + ((r * c) >> 10), cy + ((r * s) >> 10), col);
        ofx_px(bb, w, h, cx + (((r + 1) * c) >> 10), cy + (((r + 1) * s) >> 10), soft);
        ofx_px(bb, w, h, cx + (((r - 1) * c) >> 10), cy + (((r - 1) * s) >> 10), soft);
    }
}

static int32_t ofx_radius(int32_t h)
{
    int32_t R = h / 5;
    if (R < 100) R = 100;
    if (R > 300) R = 300;
    return R;
}

static uint32_t ofx_app_color(int app)
{
    switch (app) {
    case APP_TERMINAL_SHELL: return 0x0010B981u;
    case APP_AGENT_CHAT:     return 0x0038BDF8u;
    case APP_WEB_BROWSER:    return 0x00F59E0Bu;
    case APP_FILE_MANAGER:   return 0x0038BDF8u;
    case APP_IMAGE_VIEWER:   return 0x0010B981u;
    case APP_NOTEPAD:        return 0x00F472B6u;
    case APP_AGENT_MEMORY:   return 0x00A855F7u;
    case APP_LAYOUT:         return 0x000EA5E9u;
        case APP_AGENTS:         return 0x0034D399u;
    case APP_PAINT:          return 0x00F59E0Bu;
    case APP_CODER_APP:      return 0x0000FF88u;
    case APP_MEDIA:          return 0x00FB7185u;
    case APP_RECORDER:       return 0x00F87171u;
    case APP_CAMERA:         return 0x0038BDF8u;   /* SOMA_178_T480S */
    case APP_CAPTURE:        return 0x00A78BFAu;   /* SOMA_179_T480S */
    case APP_YOUTUBE:        return 0x00FF4444u;   /* SOMA_250_YOUTUBE */
    case APP_SOMA_APP:       return 0x0038BDF8u; /*PL12_OFX_COL*/
    default:                 return 0x007DD3FCu;
    }
}

/* ---------- generadores ---------- */
static void ofx_spawn_ring(int32_t r, int32_t speed, int life, uint32_t col)
{
    struct ofx_ring_t *g = &ofx_rings[ofx_ring_next];
    ofx_ring_next = (ofx_ring_next + 1) % OFX_RING_MAX;
    g->r = r; g->speed = speed;
    g->life = (int16_t)life; g->maxlife = (int16_t)life;
    g->col = col; g->alive = 1;
}

/* SOMA_223_MINIMIZAR: la ventana que se minimiza se deshace en un mosaico de teselas con sus propios colores; cada tesela sale
 * con un pequeno retraso (primero las mas cercanas al agujero negro), traza una estela que gira alrededor del orbe en el plano
 * inclinado de la orbita, roza el horizonte de sucesos y se posa en el hueco que ocupara el icono, que aparece al final con un
 * destello suave. Unos 0,75 s. Coste: 192 particulas con una linea aditiva cada una durante el vuelo (nada fuera de el). */
#define MFLY_N    192
#define MFLY_DUR  560u                                   /* vuelo de cada tesela (ms) */
#define MFLY_LAG  190u                                   /* retraso maximo entre la primera y la ultima */
struct her_mfly_p { int16_t sx, sy, tw, th; uint16_t delay; int16_t ja, jr; uint32_t col; };
static struct her_mfly_p her_mfly_p[MFLY_N];
static uint64_t her_mfly_t0;
static uint32_t her_mfly_a0;                             /* angulo de la orbita del icono al empezar */
static int32_t  her_mfly_delta;                          /* giro extra hasta el destino (unidades de 1/1024 de vuelta) */
static uint32_t her_mfly_acol;
static uint32_t her_atan1024(int32_t y, int32_t x)       /* angulo de (x, y) en 1/1024 de vuelta, sentido de her_sn */
{
    int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (!ax && !ay) return 0;
    int32_t mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    int32_t z = (int32_t)(((int64_t)mn << 15) / mx);                       /* 0..32768 */
    int32_t a = (128 * z + (int32_t)(((int64_t)45 * z * (32768 - z)) >> 15)) >> 15;   /* atan(z) en 1/1024 (0..128) */
    if (ay > ax) a = 256 - a;
    if (x < 0) a = 512 - a;
    if (y < 0) a = 1024 - a;
    return (uint32_t)a & 1023u;
}
static void her_mfly_geom(uint32_t w, uint32_t h, int32_t *r_orb)
{
    int32_t r = ofx_radius((int32_t)h) + 55;              /* lo mismo que her_min_orbit_position */
    if (r < 90) r = 90;
    if (r > (int32_t)h / 2 - 40) r = (int32_t)h / 2 - 40;
    if (r < 45) r = 45;
    (void)w; *r_orb = r;
}
static void her_mfly_start(const struct her_win_node *v, int min_idx)
{
    uint32_t w = fb_get_width(), h = fb_get_height();
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb || w < 240u || h < 240u || min_idx < 0) return;
    int32_t cx = (int32_t)w / 2, cy = (int32_t)h / 2;
    int32_t wx = v->x, wy = v->y, ww = (int32_t)v->w, wh = (int32_t)v->h;
    if (ww < 64 || wh < 48) return;
    int id = her_min_dock_index(v->app);
    her_mfly_acol = id >= 0 ? her_dock_items[id].color : 0x0038BDF8u;
    uint64_t now = timer_get_uptime_ms();
    her_mfly_a0 = ((uint32_t)(now % 28000u) * 1024u / 28000u + her_min_phase[min_idx]) & 1023u;
    /* angulo de salida: el del centro de la ventana, en el plano de la orbita (y estirada 4/3) */
    uint32_t aw = her_atan1024(((wy + wh / 2) - cy) * 4 / 3, (wx + ww / 2) - cx);
    int32_t d = (int32_t)((her_mfly_a0 - aw) & 1023u);    /* giro en el sentido de la orbita */
    if (d < 320) d += 1024;                               /* al menos un tercio de vuelta alrededor del orbe */
    her_mfly_delta = d;
    const int C = 16, R = 12;
    int32_t tw = ww / C, th = wh / R; if (tw < 2) tw = 2; if (th < 2) th = 2;
    int32_t maxd = 1;
    for (int k = 0; k < MFLY_N; ++k) {
        int c = k % C, r = k / C;
        int32_t px = wx + c * tw, py = wy + r * th;
        int32_t mx = px + tw / 2, my = py + th / 2;
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (mx >= (int32_t)w) mx = (int32_t)w - 1;
        if (my >= (int32_t)h) my = (int32_t)h - 1;
        uint32_t sc = bb[(uint32_t)my * w + (uint32_t)mx] & 0x00FFFFFFu;
        int border = (r == 0 || r == R - 1 || c == 0 || c == C - 1);
        uint32_t lum = ((sc >> 16) & 255u) + ((sc >> 8) & 255u) + (sc & 255u);
        struct her_mfly_p *q = &her_mfly_p[k];
        q->sx = (int16_t)px; q->sy = (int16_t)py; q->tw = (int16_t)tw; q->th = (int16_t)th;
        q->col = border ? her_mix256(her_mfly_acol, 0x00FFFFFFu, 70u)
                        : (lum < 60u ? her_mix256(sc, her_mfly_acol, 150u) : her_mix256(sc, her_mfly_acol, 90u));
        int32_t dx = mx - cx, dy = my - cy;
        int32_t dd = (int32_t)her_isqrt((uint32_t)(dx * dx + dy * dy));
        q->delay = (uint16_t)dd;                                         /* se normaliza abajo */
        if (dd > maxd) maxd = dd;
        uint32_t rnd = her_rand();
        q->ja = (int16_t)((int32_t)(rnd & 63u) - 32);                    /* dispersion del chorro a mitad de camino */
        q->jr = (int16_t)((int32_t)((rnd >> 6) & 63u) - 32);
    }
    int32_t mind = maxd;
    for (int k = 0; k < MFLY_N; ++k) if (her_mfly_p[k].delay < mind) mind = her_mfly_p[k].delay;
    for (int k = 0; k < MFLY_N; ++k) {
        int32_t dd = her_mfly_p[k].delay - mind, span = maxd - mind; if (span < 1) span = 1;
        her_mfly_p[k].delay = (uint16_t)((uint32_t)dd * MFLY_LAG / (uint32_t)span + (her_rand() & 15u));
    }
    her_mfly_app = (int)v->app; her_mfly_slot = v->term_slot;
    her_mfly_t0 = now; her_mfly_icon_k = 0u; her_mfly_on = 1;
}
/* posicion de una particula en el instante e (0..256) de su vuelo */
static void her_mfly_pos(const struct her_mfly_p *q, int32_t e, int32_t cx, int32_t cy, int32_t r_orb,
                         uint32_t a_now_shift, int32_t *ox, int32_t *oy)
{
    int32_t sx = q->sx + q->tw / 2 - cx, sy = (q->sy + q->th / 2 - cy) * 4 / 3;
    int32_t r0 = (int32_t)her_isqrt((uint32_t)(sx * sx + sy * sy));
    uint32_t a0 = her_atan1024(sy, sx);
    int32_t bump = her_sn((uint32_t)e * 2u);              /* sin(pi*e) en 1/1024: 0 en los extremos, 1024 a mitad */
    int32_t a = (int32_t)a0 + her_mfly_delta * e / 256 + (int32_t)a_now_shift * e / 256 + q->ja * bump / 1024;
    int32_t rr = r0 + (r_orb - r0) * e / 256;             /* del sitio de la ventana al radio de la orbita */
    int32_t dip = (rr - 40) * 45 / 100;                    /* y a mitad de camino roza el horizonte */
    if (dip < 0) dip = 0;
    rr = rr - dip * bump / 1024 + q->jr * bump / 1024;
    if (rr < 34) rr = 34;
    *ox = cx + her_cs((uint32_t)a & 1023u) * rr / 1024;
    *oy = cy + her_sn((uint32_t)a & 1023u) * rr * 3 / 4 / 1024;
}
static void her_mfly_draw(uint32_t w, uint32_t h)
{
    if (!her_mfly_on) return;
    volatile uint32_t *bb = fb_get_buffer();
    uint64_t now = timer_get_uptime_ms();
    uint32_t el = (uint32_t)(now - her_mfly_t0);
    int found = 0;
    for (int i = 0; i < her_min_count; ++i)
        if ((int)her_minimized[i].app == her_mfly_app && her_minimized[i].term_slot == her_mfly_slot) { found = 1; break; }
    if (!bb || !found || el >= MFLY_DUR + MFLY_LAG + 40u) { her_mfly_on = 0; her_mfly_icon_k = 256u; return; }
    int32_t cx = (int32_t)w / 2, cy = (int32_t)h / 2, r_orb;
    her_mfly_geom(w, h, &r_orb);
    uint32_t a_now = ((uint32_t)(now % 28000u) * 1024u / 28000u);
    uint32_t a_t0 = ((uint32_t)(her_mfly_t0 % 28000u) * 1024u / 28000u);
    uint32_t shift = (a_now - a_t0) & 1023u;               /* lo que ha girado la orbita desde que empezo */
    int32_t bx0 = (int32_t)w, by0 = (int32_t)h, bx1 = 0, by1 = 0;
    for (int k = 0; k < MFLY_N; ++k) {
        const struct her_mfly_p *q = &her_mfly_p[k];
        int32_t u = el > q->delay ? (int32_t)((el - q->delay) * 256u / MFLY_DUR) : 0;
        if (u > 256) continue;                             /* ya llego */
        if (u <= 0) {                                      /* aun no ha salido: la tesela sigue en su sitio */
            fb_fill_rect((uint32_t)(q->sx < 0 ? 0 : q->sx), (uint32_t)(q->sy < 0 ? 0 : q->sy), (uint32_t)q->tw, (uint32_t)q->th, ofx_mul(q->col, 150u));
            continue;
        }
        int32_t e = 256 - (256 - u) * (256 - u) / 256 * (256 - u) / 256;   /* salida rapida y llegada suave */
        int32_t x, y, tx, ty;
        her_mfly_pos(q, e, cx, cy, r_orb, shift, &x, &y);
        int32_t ue = u > 22 ? u - 22 : 0;                  /* la cola: el mismo camino un poco antes */
        int32_t et = 256 - (256 - ue) * (256 - ue) / 256 * (256 - ue) / 256;
        her_mfly_pos(q, et, cx, cy, r_orb, shift, &tx, &ty);
        {   /* cola de 56 px como mucho: al arrancar el paso es largo y saldria un trazo recto */
            int32_t ddx = x - tx, ddy = y - ty; int32_t L = (int32_t)her_isqrt((uint32_t)(ddx * ddx + ddy * ddy));
            if (L > 56) { tx = x - ddx * 56 / L; ty = y - ddy * 56 / L; }
        }
        uint32_t fade = u < 200 ? 256u : (uint32_t)(256 - u) * 256u / 56u;   /* se apaga al posarse */
        if (u < 36) {                                      /* desintegracion: la tesela encoge mientras arranca */
            int32_t sw = q->tw * (36 - u) / 36, sh = q->th * (36 - u) / 36;
            if (sw > 1 && sh > 1 && x - sw / 2 >= 0 && y - sh / 2 >= 0 && x + sw / 2 < (int32_t)w && y + sh / 2 < (int32_t)h)
                fb_fill_rect((uint32_t)(x - sw / 2), (uint32_t)(y - sh / 2), (uint32_t)sw, (uint32_t)sh, q->col);
        }
        uint32_t hot = (uint32_t)(her_sn((uint32_t)e * 2u) > 0 ? her_sn((uint32_t)e * 2u) : 0) / 5u;   /* mas blanca junto al horizonte */
        uint32_t tint = her_mix256(q->col, her_mfly_acol, 110u);           /* el color de la aplicacion tine la estela */
        uint32_t head = ofx_mul(her_mix256(tint, 0x00FFFFFFu, 16u + hot / 2u), 200u * fade / 256u);
        uint32_t tail = ofx_mul(tint, 22u * fade / 256u);
        ofx_line(bb, (int32_t)w, (int32_t)h, tx, ty, x, y, tail, head);
        uint32_t g = ofx_mul(head, 70u);
        ofx_px(bb, (int32_t)w, (int32_t)h, x + 1, y, g); ofx_px(bb, (int32_t)w, (int32_t)h, x - 1, y, g);
        ofx_px(bb, (int32_t)w, (int32_t)h, x, y + 1, g); ofx_px(bb, (int32_t)w, (int32_t)h, x, y - 1, g);
        int32_t lx = tx < x ? tx : x, hx = tx < x ? x : tx, ly = ty < y ? ty : y, hy = ty < y ? y : ty;
        if (lx < bx0) bx0 = lx;
        if (ly < by0) by0 = ly;
        if (hx > bx1) bx1 = hx;
        if (hy > by1) by1 = hy;
    }
    /* el icono aparece en el ultimo cuarto, con un halo que se apaga */
    int32_t tot = (int32_t)(MFLY_DUR + MFLY_LAG);
    int32_t k = ((int32_t)el - tot * 3 / 4) * 256 / (tot / 4);
    if (k < 0) k = 0;
    if (k > 256) k = 256;
    her_mfly_icon_k = (uint32_t)k;
    if (k > 0 && k < 256) {
        for (int i = 0; i < her_min_count; ++i)
            if ((int)her_minimized[i].app == her_mfly_app && her_minimized[i].term_slot == her_mfly_slot) {
                int32_t ix, iy; her_min_orbit_position(i, w, h, now, &ix, &iy);
                int32_t rr = 18 + (256 - k) / 12;
                uint32_t al = (uint32_t)(256 - k) * 110u / 256u;
                fb_fill_rrect_aa(ix - rr, iy - rr, 2 * rr, 2 * rr, rr, (al << 24) | (her_mfly_acol & 0x00FFFFFFu));
                break;
            }
    }
    if (bx1 >= bx0) fb_damage(bx0 - 3, by0 - 3, bx1 - bx0 + 7, by1 - by0 + 7);
}
/* Minimizar con la animacion (la X de la ventana y el cierre por teclado); her_minimize_window sigue sin animacion. */
static void her_minimize_window_fx(int idx)
{
    if (idx < 0 || idx >= her_num_windows) return;
    struct her_win_node v = her_windows[idx];
    her_minimize_window(idx);
    for (int i = her_min_count - 1; i >= 0; --i)
        if (her_minimized[i].app == v.app && her_minimized[i].term_slot == v.term_slot) { her_mfly_start(&v, i); break; }
}

static void ofx_spawn_ej(int32_t cx, int32_t cy, int32_t dirx, int32_t diry,
                         uint32_t col, int radial, int32_t spmin, int32_t spvar)
{
    struct ofx_ejecta *e = &ofx_ej[ofx_ej_next];
    ofx_ej_next = (ofx_ej_next + 1) % OFX_EJ_MAX;

    int32_t r0 = 30 + (int32_t)(her_rand() & 7u);
    int32_t sp = spmin + (int32_t)(her_rand() % (uint32_t)spvar);
    int32_t sx, sy, ux, uy;

    if (radial) {
        uint32_t ang = her_rand() & 1023u;
        ux = her_cs(ang); uy = her_sn(ang);
        sx = cx + ((r0 * ux) >> 10);
        sy = cy + ((r0 * uy) >> 10);
    } else {
        int32_t s  = (int32_t)(her_rand() % 921u) - 460;     /* abanico */
        int32_t o  = (int32_t)(her_rand() % 61u) - 30;       /* origen en el borde */
        ux = dirx + ((-diry * s) >> 10);
        uy = diry + ((dirx * s) >> 10);
        sx = cx + ((r0 * dirx) >> 10) + ((-diry * o) >> 10);
        sy = cy + ((r0 * diry) >> 10) + ((dirx * o) >> 10);
    }
    e->x = sx << 8;  e->y = sy << 8;
    e->vx = (ux * sp) >> 2;
    e->vy = (uy * sp) >> 2;
    e->maxlife = (int16_t)(24 + (int32_t)(her_rand() % 20u));
    e->life = e->maxlife;
    e->turn = (her_rand() & 1u) ? 1 : -1;
    e->col = ((her_rand() & 3u) == 0u) ? her_mix256(col, 0x00FFFFFFu, 140u) : col;
    e->alive = 1;
}

static void ofx_trigger_app(int32_t cx, int32_t cy, int app)
{
    uint32_t col = ofx_app_color(app);
    int32_t tx = (int32_t)her_win_x + (int32_t)her_win_w / 2;
    int32_t ty = (int32_t)her_win_y + (int32_t)her_win_h / 2;
    int32_t dx = tx - cx, dy = ty - cy;
    uint32_t len = her_isqrt((uint32_t)(dx * dx + dy * dy));
    int radial_only = (len < 4u);
    if (!radial_only) {
        ofx_jet_dx = (dx * 1024) / (int32_t)len;
        ofx_jet_dy = (dy * 1024) / (int32_t)len;
    }
    ofx_jet_col = col;
    ofx_jet_left = radial_only ? 0 : 10;
    for (int i = 0; i < OFX_BURST_APP; ++i) {
        int rad = radial_only || (i % 5) == 0;
        ofx_spawn_ej(cx, cy, ofx_jet_dx, ofx_jet_dy, col, rad, 9, 14);
    }
    ofx_spawn_ring(30, 7, 22, col);
    ofx_spawn_ring(30, 4, 32, her_mix256(col, 0x00FFFFFFu, 120u));
    ofx_flash += 170u;
    if (ofx_flash > 255u) ofx_flash = 255u;
}

static void ofx_trigger_icons(int32_t cx, int32_t cy)
{
    for (int i = 0; i < OFX_BURST_ICO; ++i)
        ofx_spawn_ej(cx, cy, 0, 0, ofx_pal[i % 10], 1, 5, 10);
    ofx_spawn_ring(30, 6, 24, 0x0038BDF8u);
    ofx_spawn_ring(30, 4, 30, 0x00A855F7u);
    ofx_spawn_ring(30, 9, 18, 0x00F8FAFCu);
    ofx_flash += 130u;
    if (ofx_flash > 255u) ofx_flash = 255u;
}

/* Gancho llamado desde her_vortex_close_step cada vez que el horizonte absorbe */
static void ofx_on_absorb(uint32_t col)
{
    ofx_flash += 10u;
    if (ofx_flash > 255u) ofx_flash = 255u;
    if ((ofx_absorb_n++ & 15u) == 0u)
        ofx_spawn_ring(64, -3, 12, col & 0x00FFFFFFu);   /* onda que se contrae */
}

/* ---------- simulacion (30 Hz, independiente de cuantas veces se pinte) ---------- */
static void ofx_tick(int32_t cx, int32_t cy)
{
    if (ofx_jet_left > 0) {
        ofx_jet_left--;
        for (int i = 0; i < 9; ++i)
            ofx_spawn_ej(cx, cy, ofx_jet_dx, ofx_jet_dy, ofx_jet_col, 0, 10, 12);
    }
    for (int i = 0; i < OFX_EJ_MAX; ++i) {
        struct ofx_ejecta *e = &ofx_ej[i];
        if (!e->alive) continue;
        e->x += e->vx;
        e->y += e->vy;
        int32_t nvx = e->vx - (e->turn * (e->vy >> 5));
        int32_t nvy = e->vy + (e->turn * (e->vx >> 5));
        e->vx = (nvx * 246) >> 8;
        e->vy = (nvy * 246) >> 8;
        if (--e->life <= 0) e->alive = 0;
    }
    for (int i = 0; i < OFX_RING_MAX; ++i) {
        struct ofx_ring_t *g = &ofx_rings[i];
        if (!g->alive) continue;
        g->r += g->speed;
        if (--g->life <= 0 || g->r <= 26) g->alive = 0;
    }
}

static void ofx_frame_update(int32_t cx, int32_t cy)
{
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    uint32_t dt = ofx_last_ms ? (now - ofx_last_ms) : 33u;
    ofx_last_ms = now;
    if (dt > HER_ANIM_DT_MAX) dt = HER_ANIM_DT_MAX;          /* SOMA_153B: tras una pausa se reanuda sin salto */

    int sp = her_cur_speed;
    if (sp < 10) sp = 10;
    sp += soma_vortex_boost / 2;
    {
        int32_t tgt = (int32_t)sp * 256;
        if (ofx_sp_f == 0) ofx_sp_f = tgt;
        ofx_sp_f += ((tgt - ofx_sp_f) * (int32_t)dt) / 250;
        for (int k = 0; k < 5; ++k) {
            int64_t inc = (int64_t)dt * ofx_rib[k].spd * ofx_sp_f * 16;
            ofx_phf[k] += (uint64_t)inc;
            ofx_ph[k] = (uint32_t)(ofx_phf[k] >> 16);
        }
    }

    uint32_t dec = ofx_flash * dt * 3u / 1024u;
    if (ofx_flash && !dec) dec = 1u;
    ofx_flash = (dec >= ofx_flash) ? 0u : ofx_flash - dec;

    /* disparadores */
    int app = (int)current_app;
    if (ofx_prev_app >= 0 && app != ofx_prev_app && app != (int)APP_DESKTOP)
        ofx_trigger_app(cx, cy, app);
    ofx_prev_app = app;

    if (her_icons_visible && !ofx_prev_icons) ofx_trigger_icons(cx, cy);
    ofx_prev_icons = her_icons_visible;

    ofx_acc += dt;
    int ticks = 0;
    while (ofx_acc >= 33u && ticks < 3) { ofx_acc -= 33u; ticks++; ofx_tick(cx, cy); }
    if (ofx_acc > 99u) ofx_acc = 0u;
}

/* ---------- halo ---------- */
static void ofx_build_halo(uint32_t gain)               /* 256 = 1.0 */
{
    uint32_t c_in  = 0x00E8F6FFu;
    uint32_t c_mid = her_mix256(0x003B82F6u, her_col_cur & 0x00FFFFFFu, 70u);
    uint32_t c_out = 0x006D28D9u;
    for (uint32_t v = 0; v < 256u; ++v) {
        uint32_t col = (v < 36u) ? her_mix256(c_in, c_mid, v * 256u / 36u)
                                 : her_mix256(c_mid, c_out, (v - 36u) * 256u / 219u);
        uint32_t glow = 255u * 40u / (40u + v);
        glow = glow * (255u - v) / 255u;
        uint32_t k = glow * gain / 560u;
        if (k > 256u) k = 256u;
        ofx_halo[v] = ofx_mul(col, k);
    }
}

/* SOMA_164B_T480S: el halo por franjas de filas entre los nucleos. Cada franja son pasos enteros de OFX_HALO_STEP filas, asi
 * que ningun bloque de pixeles cae en dos franjas; la suma aditiva es la misma en cualquier orden: resultado identico. */
static void ofx_halo_band(volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R, int32_t s0, int32_t s1)
{
    int32_t R2 = R * R;
    uint32_t kmul = (uint32_t)(255 * 65536 / R2);
    for (int32_t st = s0; st < s1; ++st) {
        int32_t y = cy - R + st * OFX_HALO_STEP;
        if (y < 0 || y >= h) continue;
        int32_t dy = y - cy, dy2 = dy * dy;
        for (int32_t x = cx - R; x < cx + R; x += OFX_HALO_STEP) {
            if (x < 0 || x >= w) continue;
            if (ofx_occ_on && y >= ofx_occ_y0 && y + OFX_HALO_STEP <= ofx_occ_y1 && x >= ofx_occ_x0 && x < ofx_occ_x1) {
                x = ofx_occ_x1 - 1; while ((x - (cx - R)) % OFX_HALO_STEP) x--; continue;   /* SOMA_153: saltar el tramo tapado */
            }
            int32_t dx = x - cx;
            int32_t d2 = dx * dx + dy2;
            if (d2 >= R2 || d2 < 484) continue;
            uint32_t col = ofx_halo[(((uint32_t)d2 * kmul) >> 16) & 255u];
            if (!col) continue;
            for (int32_t sy = 0; sy < OFX_HALO_STEP; ++sy)
                for (int32_t sx = 0; sx < OFX_HALO_STEP; ++sx)
                    ofx_px(bb, w, h, x + sx, y + sy, col);
        }
    }
}
struct ofx_halo_job { volatile uint32_t *bb; int32_t w, h, cx, cy, R; };
static void ofx_halo_part(int idx, int n, void *arg)
{
    struct ofx_halo_job *j = (struct ofx_halo_job *)arg;
    int32_t steps = (2 * j->R + OFX_HALO_STEP - 1) / OFX_HALO_STEP;
    ofx_halo_band(j->bb, j->w, j->h, j->cx, j->cy, j->R, (int32_t)((int64_t)steps * idx / n), (int32_t)((int64_t)steps * (idx + 1) / n));
}
static int ofx_par_n = 0;                                   /* 0 = todos los nucleos; 1 = forzar uno (autoprueba) */
static void ofx_draw_halo(volatile uint32_t *bb, int32_t w, int32_t h,
                          int32_t cx, int32_t cy, int32_t R)
{
    struct ofx_halo_job j = { bb, w, h, cx, cy, R };
    smp_parallel_n(ofx_halo_part, &j, ofx_par_n ? ofx_par_n : 16);
}

/* ---------- cintas de seda estilo XMB (PARCHE V2: continuidad subpixel) ---------- */
static int32_t ofx_sin16(uint32_t a)          /* a: 1/16384 de vuelta -> +-1024 */
{
    uint32_t i = (a >> 6) & 255u, f = a & 63u;
    int32_t s0 = HER_S[i], s1 = HER_S[(i + 1u) & 255u];
    return s0 + ((s1 - s0) * (int32_t)f) / 64;
}
static int32_t ofx_cos16(uint32_t a) { return ofx_sin16(a + 4096u); }

static uint32_t ofx_dbl(uint32_t c)
{
    uint32_t r = ((c >> 16) & 255u) * 2u, g = ((c >> 8) & 255u) * 2u, b = (c & 255u) * 2u;
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    return (r << 16) | (g << 8) | b;
}

/* Reparto bilineal de un punto en coordenadas x1024 */
static void ofx_splat(volatile uint32_t *bb, int32_t w, int32_t h,
                      int32_t x1024, int32_t y1024, uint32_t col)
{
    int32_t ix = x1024 >> 10, iy = y1024 >> 10;
    uint32_t fx = (uint32_t)(x1024 & 1023) >> 2;
    uint32_t fy = (uint32_t)(y1024 & 1023) >> 2;
    uint32_t w00 = ((256u - fx) * (256u - fy)) >> 8;
    uint32_t w10 = (fx * (256u - fy)) >> 8;
    uint32_t w01 = ((256u - fx) * fy) >> 8;
    uint32_t w11 = (fx * fy) >> 8;
    if (w00) ofx_px(bb, w, h, ix,     iy,     ofx_mul(col, w00));
    if (w10) ofx_px(bb, w, h, ix + 1, iy,     ofx_mul(col, w10));
    if (w01) ofx_px(bb, w, h, ix,     iy + 1, ofx_mul(col, w01));
    if (w11) ofx_px(bb, w, h, ix + 1, iy + 1, ofx_mul(col, w11));
}

static void ofx_draw_ribbons(volatile uint32_t *bb, int32_t w, int32_t h,
                             int32_t cx, int32_t cy, int32_t R, uint32_t gain)
{
    int32_t t0 = 90 * R / 216;
    for (int k = 0; k < OFX_RIB_N; ++k) {
        const struct ofx_rib_cfg *rc = &ofx_rib[k];
        int32_t base16 = (int32_t)rc->rad * R * 16 / 216;
        int32_t amp16  = (int32_t)rc->amp * R * 16 / 216;
        int32_t wid16  = (int32_t)rc->wid * R * 16 / 216;
        if (wid16 < 32) wid16 = 32;

        uint64_t ph = ofx_phf[k];
        uint32_t head16 = (uint32_t)(ph >> 24) & 16383u;
        uint32_t wph16  = (uint32_t)(ph >> 23) & 16383u;
        uint32_t tl16 = (uint32_t)((int32_t)rc->tilt * 16 +
                        ((ofx_sin16((uint32_t)(ph >> 26) & 16383u) * 480) >> 10)) & 16383u;
        int32_t ct = ofx_cos16(tl16), st = ofx_sin16(tl16);
        uint32_t rcol = her_mix256(rc->col, her_col_cur & 0x00FFFFFFu, 70u);
        const uint32_t lead16 = 384u;

        for (uint32_t a = 0; a < 1024u; a += OFX_RIB_STEP) {
            int32_t ca = her_cs(a), sa = her_sn(a);
            int32_t wave = ofx_sin16(((a * (uint32_t)rc->harm) << 4) + wph16);
            int32_t r0_16 = base16 + ((amp16 * wave) >> 10);
            int32_t pin = ofx_sin16((a << 5) + (wph16 >> 1));
            int32_t wv16 = 16 + ((wid16 * (1024 + pin)) >> 11);

            uint32_t tail16 = (head16 - (a << 4)) & 16383u;
            uint32_t inten;
            if (tail16 > 16384u - lead16) {            /* borde de entrada suave */
                uint32_t d = 16384u - tail16;
                inten = 28u + (228u * (lead16 - d)) / lead16;
            } else {
                inten = 28u + (228u * (16383u - tail16)) / 16383u;
            }
            uint32_t depth = 192u + (uint32_t)(((sa + 1024) * 64) >> 11);
            uint32_t kcol = (inten * depth) >> 8;
            kcol = (kcol * rc->alpha) >> 8;
            kcol = (kcol * gain) >> 8;
            if (kcol > 256u) kcol = 256u;
            uint32_t bc = ofx_mul(rcol, kcol);
            if (!bc) continue;

            int32_t smax = (wv16 >> 4) + 1;
            uint32_t den = (uint32_t)(wv16 + 16);
            for (int32_t s = -smax; s <= smax; ++s) {
                int32_t as = s < 0 ? -s : s;
                int32_t num = wv16 + 16 - as * 16;
                if (num <= 0) continue;
                uint32_t fq = ((uint32_t)num * 256u) / den;
                fq = (fq * fq) >> 8;
                int32_t rr16 = r0_16 + s * 16;
                int32_t lx = (rr16 * ca) >> 4;                       /* px*1024 */
                int32_t ly = (((rr16 * sa) >> 4) * (int32_t)rc->sq) >> 8;
                int32_t X = (lx * ct - ly * st) >> 10;
                int32_t Y = (lx * st + ly * ct) >> 10;
                if (ofx_occ_on) {                        /* SOMA_153: los 4 pixeles del reparto caen bajo la ventana opaca */
                    int32_t px0 = cx + (X >> 10), py0 = cy + (Y >> 10);
                    if (ofx_occluded(px0, py0) && ofx_occluded(px0 + 1, py0 + 1)) continue;
                }
                int32_t ady = (Y < 0 ? -Y : Y) >> 10;
                uint32_t att = 256u;
                if (ady > t0) {
                    int32_t t = 256 - (ady - t0) * 3;
                    att = (t < 40) ? 40u : (uint32_t)t;
                }
                uint32_t add = ofx_mul(bc, (fq * att) >> 8);
                if (!add) continue;
                ofx_splat(bb, w, h, cx * 1024 + X, cy * 1024 + Y, ofx_dbl(add));
            }
        }
    }
}

/* SOMA_153B_CINTAS: las cintas del orbe se acumulan a MEDIA resolucion y se amplian x2 con interpolacion bilineal,
 * recorriendo solo los pixeles con contenido (la bilineal es la suma de una "tienda" 3x3 por pixel de media resolucion).
 * Antes, a resolucion completa, las cintas finas salian punteadas (muestras a ~1,9 px) y los huecos parpadeaban al
 * moverse; ahora son continuas. Mismo brillo medio. En SOMA: ~35 % menos ciclos. -DSOMA_ORB_RIB_HALF=0 = original. */
#ifndef SOMA_ORB_RIB_HALF
#define SOMA_ORB_RIB_HALF 1
#endif
#define RB_TS 8              /* teselas de 8x8 (media res) marcadas como tocadas */
/* SOMA_154: el buffer de media resolucion vive en el monton y se dimensiona segun el radio real del orbe.
 * SOMA_164B_T480S: un buffer por cinta, para pintarlas a la vez en varios nucleos (fase 1); la ampliacion x2 se reparte por
 * franjas de filas de pantalla (fase 2) y cada nucleo escribe solo las suyas. Las sumas son saturadas y todas positivas, asi
 * que el orden no cambia nada: el resultado es identico al de un solo nucleo (lo comprueba 'orbe prueba'). */
struct rb_ctx { uint32_t *acc[OFX_RIB_N]; uint8_t *tile[OFX_RIB_N]; int32_t w, h, tw; };
static struct rb_ctx g_rb;
static inline void rb_add(uint32_t *acc, uint8_t *tile, int32_t rw, int32_t rtw, int x, int y, uint32_t a)
{
    if ((unsigned)x >= (unsigned)rw || (unsigned)y >= (unsigned)rw || !a) return;
    uint32_t *p = &acc[y * rw + x];
    uint32_t d = *p;
    uint32_t r = ((d >> 16) & 255u) + ((a >> 16) & 255u), g = ((d >> 8) & 255u) + ((a >> 8) & 255u), b = (d & 255u) + (a & 255u);
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    *p = (r << 16) | (g << 8) | b;
    tile[(y / RB_TS) * rtw + x / RB_TS] = 1;
}
static void rb_splat(uint32_t *acc, uint8_t *tile, int32_t rw, int32_t rtw, int32_t x1024, int32_t y1024, uint32_t col)
{
    int32_t ix = x1024 >> 10, iy = y1024 >> 10;
    uint32_t fx = (uint32_t)(x1024 & 1023) >> 2, fy = (uint32_t)(y1024 & 1023) >> 2;
    uint32_t w00 = ((256u - fx) * (256u - fy)) >> 8, w10 = (fx * (256u - fy)) >> 8, w01 = ((256u - fx) * fy) >> 8, w11 = (fx * fy) >> 8;
    if (w00) rb_add(acc, tile, rw, rtw, ix, iy, ofx_mul(col, w00));
    if (w10) rb_add(acc, tile, rw, rtw, ix + 1, iy, ofx_mul(col, w10));
    if (w01) rb_add(acc, tile, rw, rtw, ix, iy + 1, ofx_mul(col, w01));
    if (w11) rb_add(acc, tile, rw, rtw, ix + 1, iy + 1, ofx_mul(col, w11));
}
static inline uint32_t rb_lerp4(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t fx, uint32_t fy)   /* fx,fy 0..256 */
{
    uint32_t w00 = (256u - fx) * (256u - fy), w10 = fx * (256u - fy), w01 = (256u - fx) * fy, w11 = fx * fy;
    uint32_t r = (((a >> 16) & 255u) * w00 + ((b >> 16) & 255u) * w10 + ((c >> 16) & 255u) * w01 + ((d >> 16) & 255u) * w11) >> 16;
    uint32_t g = (((a >> 8) & 255u) * w00 + ((b >> 8) & 255u) * w10 + ((c >> 8) & 255u) * w01 + ((d >> 8) & 255u) * w11) >> 16;
    uint32_t bl = ((a & 255u) * w00 + (b & 255u) * w10 + (c & 255u) * w01 + (d & 255u) * w11) >> 16;
    return (r << 16) | (g << 8) | bl;
}
static int rb_ensure(struct rb_ctx *c, int32_t need)       /* solo en el hilo que llama (los trabajos no reservan memoria) */
{
    if (c->acc[0] && need <= c->w) return 0;
    uint32_t *na[OFX_RIB_N]; uint8_t *nt[OFX_RIB_N]; int ok = 1;
    for (int k = 0; k < OFX_RIB_N; k++) {
        na[k] = (uint32_t *)kmalloc(sizeof(uint32_t) * (uint32_t)need * (uint32_t)need);
        nt[k] = (uint8_t *)kmalloc((uint32_t)(need / RB_TS) * (uint32_t)(need / RB_TS));
        if (!na[k] || !nt[k]) ok = 0;
    }
    if (!ok) { for (int k = 0; k < OFX_RIB_N; k++) { if (na[k]) kfree(na[k]); if (nt[k]) kfree(nt[k]); } return -1; }
    for (int k = 0; k < OFX_RIB_N; k++) {
        if (c->acc[k]) kfree(c->acc[k]);
        if (c->tile[k]) kfree(c->tile[k]);
        memset(na[k], 0, sizeof(uint32_t) * (uint32_t)need * (uint32_t)need);
        memset(nt[k], 0, (uint32_t)(need / RB_TS) * (uint32_t)(need / RB_TS));
        c->acc[k] = na[k]; c->tile[k] = nt[k];
    }
    c->w = c->h = need; c->tw = need / RB_TS;
    return 0;
}
struct rb_job { struct rb_ctx *c; volatile uint32_t *bb; int32_t w, h, cx, cy, R, ox, oy, y0, y1; uint32_t gain; };
/* fase 1: cada cinta en su buffer (antes se borra lo que toco en el fotograma anterior) */
static void rb_paint_part(int idx, int n, void *arg)
{
    struct rb_job *j = (struct rb_job *)arg; struct rb_ctx *c = j->c;
    int32_t cx = j->cx, cy = j->cy, R = j->R, ox = j->ox, oy = j->oy;
    int32_t t0 = 90 * R / 216;
    for (int k = idx; k < OFX_RIB_N; k += n) {
        uint32_t *acc = c->acc[k]; uint8_t *tile = c->tile[k];
        for (int ty = 0; ty < c->h / RB_TS; ty++) for (int tx = 0; tx < c->tw; tx++) if (tile[ty * c->tw + tx]) {
            tile[ty * c->tw + tx] = 0;
            for (int y = ty * RB_TS; y < (ty + 1) * RB_TS; y++) memset(&acc[y * c->w + tx * RB_TS], 0, RB_TS * 4);
        }
        const struct ofx_rib_cfg *rc = &ofx_rib[k];
        int32_t base16 = (int32_t)rc->rad * R * 16 / 216, amp16 = (int32_t)rc->amp * R * 16 / 216, wid16 = (int32_t)rc->wid * R * 16 / 216;
        if (wid16 < 32) wid16 = 32;
        uint64_t ph = ofx_phf[k];
        uint32_t head16 = (uint32_t)(ph >> 24) & 16383u, wph16 = (uint32_t)(ph >> 23) & 16383u;
        uint32_t tl16 = (uint32_t)((int32_t)rc->tilt * 16 + ((ofx_sin16((uint32_t)(ph >> 26) & 16383u) * 480) >> 10)) & 16383u;
        int32_t ct = ofx_cos16(tl16), st = ofx_sin16(tl16);
        uint32_t rcol = her_mix256(rc->col, her_col_cur & 0x00FFFFFFu, 70u);
        const uint32_t lead16 = 384u;
        for (uint32_t a = 0; a < 1024u; a++) {
            int32_t ca = her_cs(a), sa = her_sn(a);
            int32_t wave = ofx_sin16(((a * (uint32_t)rc->harm) << 4) + wph16);
            int32_t r0_16 = base16 + ((amp16 * wave) >> 10);
            int32_t pin = ofx_sin16((a << 5) + (wph16 >> 1));
            int32_t wv16 = 16 + ((wid16 * (1024 + pin)) >> 11);
            uint32_t tail16 = (head16 - (a << 4)) & 16383u, inten;
            if (tail16 > 16384u - lead16) { uint32_t d = 16384u - tail16; inten = 28u + (228u * (lead16 - d)) / lead16; }
            else inten = 28u + (228u * (16383u - tail16)) / 16383u;
            uint32_t depth = 192u + (uint32_t)(((sa + 1024) * 64) >> 11);
            uint32_t kcol = (inten * depth) >> 8; kcol = (kcol * rc->alpha) >> 8; kcol = (kcol * j->gain) >> 8; if (kcol > 256u) kcol = 256u;
            uint32_t bc = ofx_mul(rcol, kcol);
            if (!bc) continue;
            int32_t smax = (wv16 >> 4) + 1; uint32_t den = (uint32_t)(wv16 + 16);
            uint32_t inv = (256u << 16) / den;                 /* una division por angulo en vez de una por muestra */
            int32_t s0 = -smax;
            int32_t rr0 = r0_16 + s0 * 16;
            int32_t lx = (rr0 * ca) >> 4, ly = (((rr0 * sa) >> 4) * (int32_t)rc->sq) >> 8;
            int32_t dlx = (32 * ca) >> 4, dly = (((32 * sa) >> 4) * (int32_t)rc->sq) >> 8;
            int32_t X = (lx * ct - ly * st) >> 10, Y = (lx * st + ly * ct) >> 10;
            int32_t dX = (dlx * ct - dly * st) >> 10, dY = (dlx * st + dly * ct) >> 10;
            int32_t bx = (cx - ox) * 1024, by = (cy - oy) * 1024;
            for (int32_t s = s0; s <= smax; s += 2, X += dX, Y += dY) {
                int32_t as = s < 0 ? -s : s, num = wv16 + 16 - as * 16;
                if (num <= 0) continue;
                uint32_t fq = ((uint32_t)num * inv) >> 16; fq = (fq * fq) >> 8;
                int32_t ady = (Y < 0 ? -Y : Y) >> 10; uint32_t att = 256u;
                if (ady > t0) { int32_t t = 256 - (ady - t0) * 3; att = (t < 40) ? 40u : (uint32_t)t; }
                uint32_t add = ofx_mul(bc, (fq * att) >> 8);
                if (!add) continue;
                if (ofx_occ_on) {                        /* SOMA_153: muestra bajo la ventana opaca (con 3 px de margen por la ampliacion) */
                    int32_t qx = cx + (X >> 10), qy = cy + (Y >> 10);
                    if (ofx_occluded(qx - 3, qy - 3) && ofx_occluded(qx + 3, qy + 3)) continue;
                }
                rb_splat(acc, tile, c->w, c->tw, (bx + X) >> 1, (by + Y) >> 1, add);
            }
        }
    }
}
static inline uint32_t sat_add(uint32_t d, uint32_t a)
{
    uint32_t r = ((d >> 16) & 255u) + ((a >> 16) & 255u), g = ((d >> 8) & 255u) + ((a >> 8) & 255u), b = (d & 255u) + (a & 255u);
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    return (r << 16) | (g << 8) | b;
}
static inline void px_band(volatile uint32_t *bb, int32_t w, int32_t h, int32_t y0, int32_t y1, int32_t x, int32_t y, uint32_t c)
{
    if (y >= y0 && y < y1) ofx_px(bb, w, h, x, y, c);
}
/* fase 2: suma de las cintas + ampliacion x2 (tienda 3x3) en las filas de pantalla [Y0, Y1) de este trozo */
static void rb_up_part(int idx, int n, void *arg)
{
    struct rb_job *j = (struct rb_job *)arg; struct rb_ctx *c = j->c;
    int32_t span = j->y1 - j->y0;
    int32_t Y0 = j->y0 + (int32_t)((int64_t)span * idx / n), Y1 = j->y0 + (int32_t)((int64_t)span * (idx + 1) / n);
    int32_t ya = (Y0 - j->oy - 1) / 2 - 1, yb = (Y1 - j->oy + 1) / 2 + 1;     /* filas de media resolucion que tocan la franja */
    if (ya < 0) ya = 0;
    if (yb > c->h) yb = c->h;
    for (int ty = ya / RB_TS; ty <= (yb - 1) / RB_TS && ty < c->h / RB_TS; ty++) for (int tx = 0; tx < c->tw; tx++) {
        int any = 0;
        for (int k = 0; k < OFX_RIB_N; k++) if (c->tile[k][ty * c->tw + tx]) { any = 1; break; }
        if (!any) continue;
        for (int y = ty * RB_TS; y < (ty + 1) * RB_TS; y++) {
            if (y < ya || y >= yb) continue;
            int py = j->oy + 2 * y;
            if (py + 1 < Y0 || py - 1 >= Y1) continue;
            for (int x = tx * RB_TS; x < (tx + 1) * RB_TS; x++) {
                uint32_t cc = 0;
                for (int k = 0; k < OFX_RIB_N; k++) { uint32_t v = c->acc[k][y * c->w + x]; if (v) cc = sat_add(cc, v); }
                if (!cc) continue;
                uint32_t c2 = ofx_mul(cc, 128u), c4 = ofx_mul(cc, 64u);
                int px = j->ox + 2 * x;
                volatile uint32_t *bb = j->bb; int32_t w = j->w, h = j->h;
                px_band(bb, w, h, Y0, Y1, px, py, cc);
                px_band(bb, w, h, Y0, Y1, px - 1, py, c2); px_band(bb, w, h, Y0, Y1, px + 1, py, c2);
                px_band(bb, w, h, Y0, Y1, px, py - 1, c2); px_band(bb, w, h, Y0, Y1, px, py + 1, c2);
                px_band(bb, w, h, Y0, Y1, px - 1, py - 1, c4); px_band(bb, w, h, Y0, Y1, px + 1, py - 1, c4);
                px_band(bb, w, h, Y0, Y1, px - 1, py + 1, c4); px_band(bb, w, h, Y0, Y1, px + 1, py + 1, c4);
            }
        }
    }
}
static void ofx_draw_ribbons(volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R, uint32_t gain);
static void rb_draw(struct rb_ctx *c, volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R, uint32_t gain)
{
    int32_t ext = 0;                                   /* alcance maximo de las cintas en px completos, con margen */
    for (int k = 0; k < OFX_RIB_N; ++k) {
        int32_t amp = (int32_t)ofx_rib[k].amp; if (amp < 0) amp = -amp;
        int32_t e = ((int32_t)ofx_rib[k].rad + amp + (int32_t)ofx_rib[k].wid) * R / 216;
        if (e > ext) ext = e;
    }
    ext += 8;
    int32_t need = (ext + RB_TS - 1) / RB_TS * RB_TS;
    if (need < 64) need = 64;
    if (rb_ensure(c, need) != 0) { ofx_draw_ribbons(bb, w, h, cx, cy, R, gain); return; }
    struct rb_job j = { c, bb, w, h, cx, cy, R, cx - c->w, cy - c->h, 0, 0, gain };
    j.y0 = j.oy - 1; if (j.y0 < 0) j.y0 = 0;
    j.y1 = j.oy + 2 * c->h + 1; if (j.y1 > h) j.y1 = h;
    int np = ofx_par_n ? ofx_par_n : 16;
    smp_parallel_n(rb_paint_part, &j, np < OFX_RIB_N ? np : OFX_RIB_N);
    if (j.y1 > j.y0) smp_parallel_n(rb_up_part, &j, np);
}
static void ofx_draw_ribbons_fast(volatile uint32_t *bb, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t R, uint32_t gain)
{
    rb_draw(&g_rb, bb, w, h, cx, cy, R, gain);
}

/* SOMA_164B_T480S: 'orbe prueba': el halo y las cintas con 1 nucleo y con todos deben dar exactamente los mismos pixeles */
int orb_parallel_selftest(char *out, uint32_t max)
{
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    uint32_t p = 0;
    if (w <= 0 || h <= 0) { soma_str_put(out, max, &p, "orbe: sin pantalla\n"); return 1; }
    uint32_t n = (uint32_t)w * (uint32_t)h;
    uint32_t *b1 = (uint32_t *)kmalloc(n * 4u), *b2 = (uint32_t *)kmalloc(n * 4u);
    static struct rb_ctx c1, c2;
    if (!b1 || !b2) { if (b1) kfree(b1); if (b2) kfree(b2); soma_str_put(out, max, &p, "orbe: sin memoria\n"); return 1; }
    int32_t R = ofx_radius(h), cx = w / 2, cy = h / 2;
    int bad = 0; uint64_t tt[2] = { 0, 0 };
    for (int round = 0; round < 4 && !bad; round++) {
        uint32_t gain = 150u + 120u * (uint32_t)round;
        for (int pass = 0; pass < 2; pass++) {
            uint32_t *b = pass ? b2 : b1;
            for (uint32_t i = 0; i < n; i++) b[i] = (i * 2654435761u >> 26) * 0x010101u;   /* fondo con algo de textura */
            ofx_par_n = pass ? 0 : 1;
            uint64_t t0 = timer_get_uptime_ms();
            ofx_build_halo(gain);
            ofx_draw_halo(b, w, h, cx, cy, R);
            rb_draw(pass ? &c2 : &c1, b, w, h, cx, cy, R, gain);
            tt[pass] += timer_get_uptime_ms() - t0;
        }
        ofx_par_n = 0;
        for (uint32_t i = 0; i < n; i++) if (b1[i] != b2[i]) { bad = 1; break; }
        for (int k = 0; k < OFX_RIB_N; ++k) ofx_phf[k] += 0x0123456789ULL * (uint64_t)(k + 1);   /* otra postura de las cintas */
    }
    kfree(b1); kfree(b2);
    soma_str_put(out, max, &p, bad ? "orbe: FALLO: con varios nucleos el orbe no sale igual que con uno\n" : "orbe: halo y cintas identicos pixel a pixel con 1 y con todos los nucleos (4 posturas)\n");
    soma_str_put(out, max, &p, "  1 nucleo: "); soma_str_put_dec(out, max, &p, (uint32_t)tt[0]); soma_str_put(out, max, &p, " ms | todos: "); soma_str_put_dec(out, max, &p, (uint32_t)tt[1]); soma_str_put(out, max, &p, " ms\n");
    return 1;
}

/* ---------- pasada trasera: halo + cintas (antes de las particulas) ---------- */
/* SOMA_164B_T480S: lo que cuesta cada parte de un fotograma del escritorio (ciclos del TSC), media de 60 fotogramas; 'orbe' */
static uint64_t tmp_pf[8], tmp_rd[8]; static uint32_t tmp_pfn;
uint64_t orb_perf_last[16]; uint32_t orb_perf_period, orb_perf_windows;
#ifdef __x86_64__
static inline uint64_t tmp_tsc(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return (uint64_t)hi << 32 | lo; }
#else
static inline uint64_t tmp_tsc(void) { uint64_t v; __asm__ volatile ("isb; mrs %0, cntvct_el0" : "=r"(v)); return v; }
#endif
