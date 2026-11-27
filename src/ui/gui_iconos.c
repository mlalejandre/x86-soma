/* gui_iconos.c: iconos en orbita y vectoriales, nombres al pasar el raton.
 * SOMA_234_GUI_PARTIDO: NO se compila por separado. Lo incluye src/ui/gui.c en su sitio (una sola unidad de
 * compilacion: comparte el estado del escritorio con gui.c, y el nucleo compilado sale identico). */
/* SOMA TEXT ABSORPTION PATCH v5 */
/* SOMA ASYNC MATERIALIZE PATCH v4 */
/* SOMA ORBITAL CALM PATCH v3 */
/* SOMA ORBITAL ICONS PATCH v2: iconos como cuerpos en orbita estable. */
#define HER_DOCK_ITEM_COUNT 25     /* SOMA_272_AGENDA: +1 (la agenda va al final para no mover las posiciones guardadas); SOMA_295_CORREO: +1 (el correo, tambien al final) */
#define HER_ICON_ANIM_MAX   256u
#define HER_ICON_BOX_SIZE   44

struct her_dock_item {
    int icon;
    uint32_t color;
    const char *label;
    enum app_mode app;
};

static const struct her_dock_item her_dock_items[HER_DOCK_ITEM_COUNT] = {
    { ICON_AGENT,    0x00E0F2FE,  "SOMA",    APP_AGENT_CHAT },      /* Blanco plasma estelar */
    { ICON_TERMINAL, 0x0038BDF8,  "TERM",    APP_TERMINAL_SHELL },  /* Cian radiación */
    { ICON_BROWSER,  0x0060A5FA,  "WEB",     APP_WEB_BROWSER },     /* Azul fotónico */
    { ICON_FILES,    0x0094A3B8,  "FILES",   APP_FILE_MANAGER },    /* Titanio frío */
    { ICON_IMAGE,    0x00A78BFA,  "IMG",     APP_IMAGE_VIEWER },    /* Nebulosa cuántica */
    { ICON_NOTES,    0x00CBD5E1,  "NOTES",   APP_NOTEPAD },         /* Acero claro / Cristal */
    { ICON_MEMORY,   0x00818CF8,  "MEM",     APP_AGENT_MEMORY },    /* Zafiro sináptico */
    { ICON_LAYOUT,   0x0038BDF8,  "LAYOUT",  APP_LAYOUT },          /* Cian polar */
    { ICON_CODER,    0x0000FF88,  "CODER",   APP_CODER_APP },
    { ICON_PAINT,    0x00F59E0B,  "PAINT",   APP_PAINT },           /* Ámbar / Pintura */
    { ICON_AGENTS,   0x00BAE6FD,  "AGENTES", APP_AGENTS },          /* Éter orbital */
    { ICON_MEDIA,    0x00FB7185,  "MEDIA",   APP_MEDIA },           /* Rosa estelar: pulsar */
    { ICON_PDF,      0x00F87171,  "PDF",     APP_PDF },             /* PARCHE 088: lector de PDF */
    { ICON_MONITOR,  0x0034D399,  "MONITOR", APP_SYSMON },          /* PARCHE 089: monitor del sistema */
    { ICON_MIC,      0x00F87171,  "GRABAR",  APP_RECORDER },        /* SOMA_177_T480S: grabadora de sonido */
    { ICON_CAM,      0x0038BDF8,  "CAMARA",  APP_CAMERA },          /* SOMA_178_T480S: camara web */
    { ICON_SCR,      0x00A78BFA,  "CAPTURA", APP_CAPTURE },         /* SOMA_179_T480S: captura de pantalla */
    { ICON_SETTINGS, 0x0094A3B8,  "AJUSTES", APP_SETTINGS },        /* PARCHE 085: engranaje */
    { ICON_YOUTUBE,  0x00FF4444,  "YOUTUBE", APP_YOUTUBE },         /* SOMA_250_YOUTUBE: videos de YouTube */
    { ICON_AGENDA,   0x0034D399,  "AGENDA",  APP_AGENDA },          /* SOMA_272_AGENDA: calendario */
    { ICON_CORREO,   0x0022D3EE,  "CORREO",  APP_CORREO },          /* SOMA_295_CORREO: correo electronico */
    { ICON_DISCOS,   0x00F59E0B,  "DISCOS",  APP_DISCOS },          /* SOMA_331_DISCOS: discos y particiones (al final: no mueve las posiciones guardadas) */
    { ICON_NOTICIAS, 0x00E2E8F0,  "NOTICIAS", APP_NOTICIAS },       /* SOMA_337_NOTICIAS: noticias (al final) */
    { ICON_CONTACTOS, 0x00F472B6, "CONTACTOS", APP_CONTACTOS },     /* SOMA_357_CONTACTOS: libreta de contactos (al final, junto al calendario) */
    { ICON_HOJA,     0x0022C55E,  "HOJA",    APP_HOJA },            /* SOMA_359_HOJA_VENTANA: hoja de calculo (al final) */
};

/* La aparicion se escalona ligeramente para que los iconos escapen uno tras
 * otro. Esta funcion es compartida por el render y la deteccion de clics.
 */
static uint32_t her_icon_item_progress(int i)
{
    if (i < 0 || i >= HER_DOCK_ITEM_COUNT) return 0;
    uint32_t delay = (uint32_t)i * 6u; /* Velocidad 2x */
    if (her_icons_anim_phase <= delay) return 0;
    return ((her_icons_anim_phase - delay) * HER_ICON_ANIM_MAX) /
           (HER_ICON_ANIM_MAX - delay);
}

/* Orbitas amplias, adaptadas a resoluciones pequeñas para no expulsar iconos
 * fuera de pantalla. El radio supera ampliamente el horizonte de sucesos. */
static int32_t her_orbit_radius_x(uint32_t cur_w)
{
    int32_t r = (int32_t)(cur_w * 23u / 100u);
    int32_t cap = (int32_t)(cur_w / 2u) - 36;
    if (cap < 40) cap = 40;
    if (r > 280) r = 280;
    if (r > cap) r = cap;
    if (r < 110 && cap >= 110) r = 110;
    if (r > cap) r = cap;
    return r;
}

static int32_t __attribute__((unused)) her_orbit_radius_y(uint32_t cur_h)
{
    uint32_t percent = (cur_h < 650u) ? 13u : 21u;
    int32_t r = (int32_t)(cur_h * percent / 100u);
    int32_t cap = (int32_t)(cur_h / 2u) - 42;
    if (cap < 35) cap = 35;
    if (r > 175) r = 175;
    if (r > cap) r = cap;
    if (r < 64 && cap >= 64) r = 64;
    if (r > cap) r = cap;
    return r;
}

/* La ventana se limita al cuadrante exterior de la orbita. El nucleo y la
 * mayor parte de la trayectoria orbital quedan visibles incluso en SVGA. */
static void her_fit_window_to_orbit(uint32_t cur_w, uint32_t cur_h)
{
    int32_t min_w = (cur_w < 900u) ? 180 : ((cur_w < 1200u) ? 220 : HER_MIN_W);
    int32_t max_w = (int32_t)(cur_w / 2u) - her_orbit_radius_x(cur_w) - 60;
    int32_t avail_w = (int32_t)cur_w - 32;
    int32_t max_h = (int32_t)cur_h - 48;
    if (max_w < min_w) max_w = min_w;
    if (max_w > avail_w) max_w = avail_w;
    if (max_h < HER_MIN_H) max_h = HER_MIN_H;
    if (her_win_w < (uint32_t)min_w || her_win_w > (uint32_t)max_w)
        her_win_w = (uint32_t)max_w;
    if (her_win_h < HER_MIN_H || her_win_h > (uint32_t)max_h)
        her_win_h = (uint32_t)max_h;
}

/* Las aplicaciones se distribuyen por las cuatro esquinas segun su tipo. */
static void her_place_window_corner(uint32_t cur_w, uint32_t cur_h, int corner)
{
    her_fit_window_to_orbit(cur_w, cur_h);
    her_win_x = ((corner & 1) == 0 || cur_w <= her_win_w + 32u)
              ? 16u : cur_w - her_win_w - 16u;
    her_win_y = ((corner & 2) == 0 || cur_h <= her_win_h + 48u)
              ? 32u : cur_h - her_win_h - 16u;
}

/* Orbita eliptica amplia sin linea guia: una revolucion cada 60 segundos. */
/* SOMA_203_INSTANCIAS: cinco columnas tematicas, separadas del orbe.
 * 0: asistentes, 1: sistema/desarrollo, 2: trabajo,
 * 3: multimedia, 4: captura/red. Posiciones estaticas en el escritorio. */
static const uint8_t her_group_col[HER_DOCK_ITEM_COUNT] = {
    0,1,4,2,3,2,0,2,1,3,0,3,2,1,3,4,4,1,4,0,2,1,4,0,3   /* SOMA_359_HOJA_VENTANA: en el unico hueco libre (columna 3, abajo); SOMA_357_CONTACTOS: con los asistentes, bajo el calendario; SOMA_295_CORREO: el correo, con el trabajo; SOMA_331_DISCOS: discos, con el sistema; SOMA_337_NOTICIAS: con la red */
};
static const uint8_t her_group_row[HER_DOCK_ITEM_COUNT] = {
    0,0,2,0,0,1,2,2,1,1,1,2,3,2,3,1,0,3,3,3,4,4,4,4,4
};
/* SOMA_208_ESCRITORIO: disposicion del usuario. Cada icono guarda su posicion en milesimas de pantalla (+1; 0 = la del grupo) para
 * que sirva a cualquier resolucion. Se guarda en /etc/escritorio.conf. */
static int16_t  her_icon_ux[HER_DOCK_ITEM_COUNT], her_icon_uy[HER_DOCK_ITEM_COUNT];
static int      her_icon_custom_n;                 /* iconos movidos por el usuario (oculta los rotulos de grupo) */
static uint32_t her_desk_ver;                      /* sube con cada cambio; entra en la firma de la capa de fondo */
static int      her_dp_kind, her_dp_moved;         /* arrastre en curso: 0 nada, 1 icono, 2 gadget */
static int      her_idrag = -1;                    /* icono que se esta arrastrando (ya sobrepasado el umbral) */
static int32_t  her_idrag_x, her_idrag_y;
/* SOMA_218_CUADRICULA: rejilla virtual del escritorio. Es la misma de la disposicion por grupos: columnas cada `gap` px
 * contadas desde el margen izquierdo en la mitad izquierda y desde el derecho en la derecha, y filas cada `dy` px a partir
 * de y0 (hacia arriba y hacia abajo). Asi un icono soltado por el usuario queda alineado con los que siguen en su sitio. */
static void her_grid_params(uint32_t cur_w, uint32_t cur_h, int32_t *margin, int32_t *gap, int32_t *y0, int32_t *dy)
{
    *margin = cur_w < 960u ? 54 : 68;
    *gap = cur_w < 960u ? 77 : 96;
    *y0 = cur_h < 650u ? 165 : 172;
    int32_t usable = (int32_t)cur_h - *y0 - 164;
    if (usable < 114) usable = 114;
    *dy = usable / 3;
    if (*dy > 104) *dy = 104;
}
static void her_grid_snap(uint32_t cur_w, uint32_t cur_h, int32_t *x, int32_t *y)
{
    int32_t margin, gap, y0, dy, w = (int32_t)cur_w, h = (int32_t)cur_h;
    her_grid_params(cur_w, cur_h, &margin, &gap, &y0, &dy);
    int32_t bx = margin, bd = 0x7FFFFFFF, last_l = margin;
    for (int32_t c = 0; c < 64; ++c) {                                   /* columnas de la izquierda */
        int32_t cx = margin + c * gap;
        if (c && cx > w / 2) break;
        int32_t d = cx > *x ? cx - *x : *x - cx;
        if (d < bd) { bd = d; bx = cx; }
        last_l = cx;
    }
    for (int32_t k = 0; k < 64; ++k) {                                   /* columnas de la derecha */
        int32_t cx = w - margin - k * gap;
        if (cx <= w / 2 || cx - last_l < gap * 3 / 4) break;             /* en el centro no se solapan con las de la izquierda */
        int32_t d = cx > *x ? cx - *x : *x - cx;
        if (d < bd) { bd = d; bx = cx; }
    }
    if (dy < 8) dy = 8;
    int32_t r = (*y - y0 + dy / 2 + dy * 64) / dy - 64;
    int32_t ry = y0 + r * dy;
    while (ry < 36 && ry + dy <= h - 36) ry += dy;
    while (ry > h - 36 && ry - dy >= 36) ry -= dy;
    *x = bx; *y = ry;
}
static void her_icon_orbit_target(int i, uint32_t cur_w, uint32_t cur_h,
                                  uint64_t now_ms, int32_t *out_x, int32_t *out_y)
{
    (void)now_ms;
    if (i == her_idrag) { *out_x = her_idrag_x; *out_y = her_idrag_y; return; }
    if (her_icon_ux[i]) {
        *out_x = ((int32_t)her_icon_ux[i] - 1) * (int32_t)cur_w / 1000;
        *out_y = ((int32_t)her_icon_uy[i] - 1) * (int32_t)cur_h / 1000;
        her_grid_snap(cur_w, cur_h, out_x, out_y);                      /* tambien las disposiciones guardadas antes */
        return;
    }
    int32_t w = (int32_t)cur_w;
    int32_t col = her_group_col[i], row = her_group_row[i];
    int32_t margin, gap, y0, dy;
    her_grid_params(cur_w, cur_h, &margin, &gap, &y0, &dy);
    int32_t x = col < 2 ? margin + col * gap : w - margin - (4 - col) * gap;
    *out_x = x;
    *out_y = y0 + row * dy;
}

/* Destello corto de materia expulsada/absorbida. No usa arrays ni memoria
 * adicional; se dibuja solo durante los primeros 280 ms de cada transicion.
 */
static void her_draw_icon_burst(uint32_t cur_w, uint32_t cur_h, uint64_t now_ms)
{
    if (her_icons_burst_at == 0) return;
    uint64_t elapsed64 = (now_ms >= her_icons_burst_at) ? now_ms - her_icons_burst_at : 0;
    if (elapsed64 >= 140u) {
        her_icons_burst_at = 0;
        return;
    }

    uint32_t elapsed = (uint32_t)elapsed64;
    uint32_t travel = elapsed * 48u / 140u;
    uint32_t fade = (140u - elapsed) * 256u / 140u;
    int32_t cx = (int32_t)(cur_w / 2u), cy = (int32_t)(cur_h / 2u);
    uint32_t tint = her_scale(0x00DCEBFFu, fade);

    for (int i = 0; i < 12; ++i) {
        uint32_t a = (((uint32_t)i * 1024u) / 12u + elapsed / 8u) & 1023u;
        int32_t r0, r1;
        if (her_icons_burst_opening) {
            r0 = 23 + (int32_t)travel;
            r1 = r0 + 8;
        } else {
            r1 = 23 + (int32_t)(48u - travel);
            r0 = r1 + 8;
        }
        int32_t x0 = cx + (r0 * her_cs(a)) / 1024;
        int32_t y0 = cy + (r0 * her_sn(a)) / 1024;
        int32_t x1 = cx + (r1 * her_cs(a)) / 1024;
        int32_t y1 = cy + (r1 * her_sn(a)) / 1024;
        fb_draw_line(x0, y0, x1, y1, tint);
    }
}

/* Particulas discretas que viajan desde el centro y se condensan en el icono. */
static void her_draw_icon_materialization(int i, int32_t px, int32_t py,
                                          uint32_t progress, uint64_t now_ms,
                                          uint32_t cur_w, uint32_t cur_h)
{
    uint32_t tick = (uint32_t)(now_ms / 24u);
    int32_t spread = 3 + (int32_t)((HER_ICON_ANIM_MAX - progress) * 25u /
                                   HER_ICON_ANIM_MAX);
    uint32_t brightness = 76u + (progress * 170u) / HER_ICON_ANIM_MAX;
    uint32_t color = her_scale(0x00E7F3FFu, brightness);

    for (int k = 0; k < 8; ++k) {
        uint32_t a = ((uint32_t)i * 113u + (uint32_t)k * 149u +
                      tick * (9u + (uint32_t)(k & 3))) & 1023u;
        int32_t ring = 2 + (int32_t)(((uint32_t)k * 7u + tick +
                                     (uint32_t)i * 3u) % (uint32_t)spread);
        int32_t sx = px + (ring * her_cs(a)) / 1024;
        int32_t sy = py + (ring * her_sn(a)) / 1024;
        int32_t tail = 2 + (k % 3);
        int32_t ex = sx - (tail * her_cs(a)) / 1024;
        int32_t ey = sy - (tail * her_sn(a)) / 1024;

        if (sx >= 0 && sy >= 0 && sx < (int32_t)cur_w &&
            sy < (int32_t)cur_h) {
            fb_draw_line(sx, sy, ex, ey, color);
            fb_draw_pixel((uint32_t)sx, (uint32_t)sy, 0x00FFFFFFu);
        }
    }
}

/* SOMA VECTOR ICONS PATCH v1: iconos vectoriales suavizados, sin recuadro. */
#define HER_ICON_MAX_S 96

static uint32_t *her_icon_sprites = 0;
#define HER_ICON_SLOTS 3
static int       her_icon_slot_s[HER_ICON_SLOTS];
static int       her_icon_slot_next;
static uint8_t   her_icon_ready2[HER_ICON_SLOTS][HER_DOCK_ITEM_COUNT];

static int her_icon_canvas(uint32_t cur_h)
{
    return (cur_h < 650u) ? 64 : 88;   /* lado del sprite en pixeles */
}

/* Geometria en una rejilla de diseno de 64x64 unidades.
 * X, Y llegan en 1/16 de unidad; los parametros de forma, en unidades. */
static int32_t ic_abs(int32_t v) { return v < 0 ? -v : v; }

static int32_t ic_circ(int32_t X, int32_t Y, int cx, int cy, int r)
{
    int32_t dx = X - cx * 16, dy = Y - cy * 16;
    return (int32_t)her_isqrt((uint32_t)(dx * dx + dy * dy)) - r * 16;
}

static int32_t ic_rbox(int32_t X, int32_t Y, int cx, int cy, int hw, int hh, int r)
{
    int32_t dx = ic_abs(X - cx * 16), dy = ic_abs(Y - cy * 16);
    int32_t qx = dx - (hw - r) * 16, qy = dy - (hh - r) * 16;
    int32_t ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    int32_t inner = qx > qy ? qx : qy;
    if (inner > 0) inner = 0;
    return (int32_t)her_isqrt((uint32_t)(ox * ox + oy * oy)) + inner - r * 16;
}

/* Capsula entre (ax,ay) y (bx,by); hw16 = medio grosor en 1/16 de unidad. */
static int32_t ic_seg(int32_t X, int32_t Y, int ax, int ay, int bx, int by, int hw16)
{
    int32_t px = X - ax * 16, py = Y - ay * 16;
    int32_t vx = (bx - ax) * 16, vy = (by - ay) * 16;
    int32_t l2 = vx * vx + vy * vy;
    int32_t t = l2 ? (px * vx + py * vy) * 256 / l2 : 0;
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    int32_t hx = px - vx * t / 256, hy = py - vy * t / 256;
    return (int32_t)her_isqrt((uint32_t)(hx * hx + hy * hy)) - hw16;
}

static int32_t ic_ell(int32_t X, int32_t Y, int cx, int cy, int rx, int ry)
{
    int32_t dx = (X - cx * 16) * ry / rx, dy = Y - cy * 16;
    int32_t d = (int32_t)her_isqrt((uint32_t)(dx * dx + dy * dy)) - ry * 16;
    return d * rx / ry;
}

#define IC_RING(d, h) (ic_abs(d) - (h))
#define IC_M(v) do { int32_t _v = (v); if (_v < m) m = _v; } while (0)
#define IC_A(v) do { int32_t _v = (v); if (_v < a) a = _v; } while (0)

/* Distancia con signo al trazo principal (dm) y al acento (da). */
static void her_icon_dist(int icon, int32_t X, int32_t Y, int32_t *dm, int32_t *da)
{
    int32_t m = 1 << 20, a = 1 << 20;
    switch (icon) {
    case ICON_CODER:
        IC_M(ic_seg(X, Y, 22, 20, 14, 32, 22));
        IC_M(ic_seg(X, Y, 14, 32, 22, 44, 22));
        IC_M(ic_seg(X, Y, 42, 20, 50, 32, 22));
        IC_M(ic_seg(X, Y, 50, 32, 42, 44, 22));
        IC_A(ic_seg(X, Y, 35, 17, 29, 47, 20));
        break;
    case ICON_AGENT:
        IC_M(IC_RING(ic_circ(X, Y, 32, 32, 20), 24));
        IC_A(ic_circ(X, Y, 32, 32, 9));
        IC_M(ic_circ(X, Y, 46, 18, 3));
        break;
    case ICON_TERMINAL:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 32, 22, 17, 6), 24));
        IC_A(ic_seg(X, Y, 20, 26, 28, 32, 28));
        IC_A(ic_seg(X, Y, 28, 32, 20, 38, 28));
        IC_A(ic_seg(X, Y, 33, 39, 44, 39, 28));
        break;
    case ICON_BROWSER:
        IC_M(IC_RING(ic_circ(X, Y, 32, 32, 20), 24));
        IC_M(IC_RING(ic_ell(X, Y, 32, 32, 9, 20), 20));
        IC_A(ic_seg(X, Y, 12, 32, 52, 32, 20));
        break;
    case ICON_FILES:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 35, 22, 15, 5), 24));
        IC_A(ic_rbox(X, Y, 19, 19, 10, 4, 2));
        IC_A(ic_seg(X, Y, 22, 36, 42, 36, 20));
        break;
    case ICON_IMAGE:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 32, 22, 18, 6), 24));
        IC_A(ic_circ(X, Y, 43, 24, 4));
        IC_M(ic_seg(X, Y, 15, 44, 25, 33, 24));
        IC_M(ic_seg(X, Y, 25, 33, 33, 41, 24));
        IC_M(ic_seg(X, Y, 33, 41, 40, 35, 24));
        IC_M(ic_seg(X, Y, 40, 35, 49, 44, 24));
        break;
    case ICON_NOTES:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 32, 16, 22, 5), 24));
        IC_A(ic_seg(X, Y, 24, 23, 40, 23, 20));
        IC_A(ic_seg(X, Y, 24, 31, 40, 31, 20));
        IC_A(ic_seg(X, Y, 24, 39, 34, 39, 20));
        break;
    case ICON_MEMORY:
        IC_A(ic_circ(X, Y, 32, 17, 6));
        IC_M(ic_seg(X, Y, 32, 23, 16, 42, 20));
        IC_M(ic_seg(X, Y, 32, 23, 32, 42, 20));
        IC_M(ic_seg(X, Y, 32, 23, 48, 42, 20));
        IC_M(IC_RING(ic_circ(X, Y, 16, 46, 5), 20));
        IC_M(IC_RING(ic_circ(X, Y, 32, 46, 5), 20));
        IC_M(IC_RING(ic_circ(X, Y, 48, 46, 5), 20));
        break;
    case ICON_LAYOUT:
        IC_M(IC_RING(ic_rbox(X, Y, 21, 32, 10, 20, 3), 20));
        IC_M(IC_RING(ic_rbox(X, Y, 43, 23, 10, 10, 3), 20));
        IC_A(ic_rbox(X, Y, 43, 45, 10, 6, 3));
        break;
    case ICON_MEDIA:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 32, 24, 18, 6), 24));
        IC_A(ic_seg(X, Y, 27, 24, 27, 40, 28));
        IC_A(ic_seg(X, Y, 27, 24, 41, 32, 28));
        IC_A(ic_seg(X, Y, 27, 40, 41, 32, 28));
        IC_A(ic_circ(X, Y, 31, 32, 5));
        break;
    case ICON_PAINT:
        IC_M(IC_RING(ic_circ(X, Y, 30, 32, 18), 24));
        IC_A(ic_circ(X, Y, 24, 25, 4));
        IC_A(ic_circ(X, Y, 36, 23, 4));
        IC_A(ic_circ(X, Y, 42, 33, 4));
        IC_M(ic_circ(X, Y, 25, 40, 4));
        IC_A(ic_seg(X, Y, 44, 16, 52, 8, 20));
        break;
    case ICON_MIC:                                       /* SOMA_177_T480S: microfono de pie */
        IC_M(IC_RING(ic_rbox(X, Y, 32, 24, 8, 14, 8), 22));
        IC_A(ic_rbox(X, Y, 32, 22, 3, 7, 3));
        IC_M(ic_seg(X, Y, 19, 30, 19, 34, 20)); IC_M(ic_seg(X, Y, 45, 30, 45, 34, 20));
        IC_M(ic_seg(X, Y, 19, 34, 24, 42, 20)); IC_M(ic_seg(X, Y, 45, 34, 40, 42, 20));
        IC_M(ic_seg(X, Y, 24, 42, 40, 42, 20));
        IC_M(ic_seg(X, Y, 32, 42, 32, 52, 22)); IC_M(ic_seg(X, Y, 23, 52, 41, 52, 22));
        break;
    case ICON_CAM:                                       /* SOMA_178_T480S: camara con objetivo */
        IC_M(IC_RING(ic_rbox(X, Y, 32, 36, 22, 14, 6), 22));
        IC_M(ic_seg(X, Y, 22, 21, 42, 21, 20)); IC_M(ic_seg(X, Y, 22, 21, 19, 25, 18)); IC_M(ic_seg(X, Y, 42, 21, 45, 25, 18));
        IC_M(IC_RING(ic_circ(X, Y, 32, 36, 9), 22));
        IC_A(ic_circ(X, Y, 32, 36, 4));
        IC_A(ic_circ(X, Y, 47, 29, 2));
        break;
    case ICON_SCR:                                       /* SOMA_179_T480S: pantalla con marco de captura y punto de grabacion */
        IC_M(ic_seg(X, Y, 14, 22, 14, 14, 20)); IC_M(ic_seg(X, Y, 14, 14, 22, 14, 20)); IC_M(ic_seg(X, Y, 42, 14, 50, 14, 20)); IC_M(ic_seg(X, Y, 50, 14, 50, 22, 20));
        IC_M(ic_seg(X, Y, 14, 42, 14, 50, 20)); IC_M(ic_seg(X, Y, 14, 50, 22, 50, 20)); IC_M(ic_seg(X, Y, 42, 50, 50, 50, 20)); IC_M(ic_seg(X, Y, 50, 42, 50, 50, 20));
        IC_A(ic_circ(X, Y, 32, 32, 8));
        break;
    case ICON_YOUTUBE:                                   /* SOMA_250_YOUTUBE: placa redondeada llena con el triangulo de reproducir */
        IC_M(ic_rbox(X, Y, 32, 32, 25, 18, 9));
        IC_A(ic_seg(X, Y, 28, 25, 28, 39, 26)); IC_A(ic_seg(X, Y, 28, 25, 40, 32, 26)); IC_A(ic_seg(X, Y, 28, 39, 40, 32, 26));
        IC_A(ic_circ(X, Y, 32, 32, 5));
        break;
    case ICON_CORREO:                                    /* SOMA_295_CORREO: sobre con la solapa y un destello de IA */
        IC_M(ic_seg(X, Y, 12, 20, 52, 20, 20)); IC_M(ic_seg(X, Y, 52, 20, 52, 46, 20)); IC_M(ic_seg(X, Y, 52, 46, 12, 46, 20)); IC_M(ic_seg(X, Y, 12, 46, 12, 20, 20));
        IC_M(ic_seg(X, Y, 13, 21, 32, 35, 18)); IC_M(ic_seg(X, Y, 51, 21, 32, 35, 18));
        IC_A(ic_circ(X, Y, 50, 15, 5));
        break;
    case ICON_AGENDA:                                    /* SOMA_272_AGENDA: hoja de calendario con anillas y dos dias marcados */
        IC_M(ic_seg(X, Y, 14, 18, 50, 18, 20)); IC_M(ic_seg(X, Y, 50, 18, 50, 52, 20)); IC_M(ic_seg(X, Y, 50, 52, 14, 52, 20)); IC_M(ic_seg(X, Y, 14, 52, 14, 18, 20));
        IC_M(ic_seg(X, Y, 14, 28, 50, 28, 18)); IC_M(ic_seg(X, Y, 23, 12, 23, 22, 20)); IC_M(ic_seg(X, Y, 41, 12, 41, 22, 20));
        IC_A(ic_circ(X, Y, 24, 40, 4)); IC_A(ic_circ(X, Y, 38, 40, 4));
        break;
    case ICON_CONTACTOS:                                 /* SOMA_357_CONTACTOS: ficha con una persona (cabeza y hombros) y renglones */
        IC_M(ic_seg(X, Y, 12, 14, 52, 14, 20)); IC_M(ic_seg(X, Y, 52, 14, 52, 50, 20)); IC_M(ic_seg(X, Y, 52, 50, 12, 50, 20)); IC_M(ic_seg(X, Y, 12, 50, 12, 14, 20));
        IC_A(ic_circ(X, Y, 26, 27, 5)); IC_A(ic_seg(X, Y, 19, 42, 33, 42, 24));
        IC_M(ic_seg(X, Y, 38, 26, 46, 26, 14)); IC_M(ic_seg(X, Y, 38, 33, 46, 33, 14)); IC_M(ic_seg(X, Y, 38, 40, 44, 40, 14));
        break;
    case ICON_HOJA:                                      /* SOMA_359_HOJA_VENTANA: hoja con la fila de titulos y la rejilla */
        IC_M(ic_seg(X, Y, 12, 14, 52, 14, 20)); IC_M(ic_seg(X, Y, 52, 14, 52, 50, 20)); IC_M(ic_seg(X, Y, 52, 50, 12, 50, 20)); IC_M(ic_seg(X, Y, 12, 50, 12, 14, 20));
        IC_A(ic_seg(X, Y, 13, 21, 51, 21, 22));
        IC_M(ic_seg(X, Y, 25, 22, 25, 49, 14)); IC_M(ic_seg(X, Y, 38, 22, 38, 49, 14)); IC_M(ic_seg(X, Y, 13, 31, 51, 31, 14)); IC_M(ic_seg(X, Y, 13, 40, 51, 40, 14));
        IC_A(ic_circ(X, Y, 45, 44, 3));
        break;
    case ICON_NOTICIAS:                                  /* SOMA_337_NOTICIAS: hoja de periodico: titular, foto y columnas */
        IC_M(ic_seg(X, Y, 12, 14, 52, 14, 20)); IC_M(ic_seg(X, Y, 52, 14, 52, 50, 20)); IC_M(ic_seg(X, Y, 52, 50, 12, 50, 20)); IC_M(ic_seg(X, Y, 12, 50, 12, 14, 20));
        IC_A(ic_seg(X, Y, 18, 22, 46, 22, 22));
        IC_M(ic_seg(X, Y, 18, 31, 30, 31, 14)); IC_M(ic_seg(X, Y, 18, 37, 30, 37, 14)); IC_M(ic_seg(X, Y, 18, 43, 30, 43, 14));
        IC_A(ic_seg(X, Y, 36, 31, 46, 31, 14)); IC_A(ic_seg(X, Y, 46, 31, 46, 43, 14)); IC_A(ic_seg(X, Y, 46, 43, 36, 43, 14)); IC_A(ic_seg(X, Y, 36, 43, 36, 31, 14));
        break;
    case ICON_DISCOS:                                    /* SOMA_331_DISCOS: disco con su sector marcado y la luz */
        IC_M(ic_seg(X, Y, 12, 20, 52, 20, 20)); IC_M(ic_seg(X, Y, 52, 20, 52, 46, 20)); IC_M(ic_seg(X, Y, 52, 46, 12, 46, 20)); IC_M(ic_seg(X, Y, 12, 46, 12, 20, 20));
        IC_M(ic_seg(X, Y, 12, 36, 52, 36, 16));
        IC_A(ic_seg(X, Y, 18, 28, 34, 28, 18)); IC_A(ic_circ(X, Y, 45, 41, 3));
        break;
    case ICON_MONITOR:                                   /* PARCHE 089: pantalla con una grafica */
        IC_M(ic_seg(X, Y, 12, 16, 52, 16, 20)); IC_M(ic_seg(X, Y, 52, 16, 52, 44, 20)); IC_M(ic_seg(X, Y, 52, 44, 12, 44, 20)); IC_M(ic_seg(X, Y, 12, 44, 12, 16, 20));
        IC_M(ic_seg(X, Y, 26, 52, 38, 52, 20)); IC_M(ic_seg(X, Y, 32, 44, 32, 52, 18));
        IC_A(ic_seg(X, Y, 17, 36, 25, 30, 18)); IC_A(ic_seg(X, Y, 25, 30, 31, 34, 18)); IC_A(ic_seg(X, Y, 31, 34, 39, 23, 18)); IC_A(ic_seg(X, Y, 39, 23, 47, 27, 18));
        break;
    case ICON_PDF:                                       /* PARCHE 088: hoja con esquina doblada y renglones */
        IC_M(ic_seg(X, Y, 20, 12, 38, 12, 22)); IC_M(ic_seg(X, Y, 20, 12, 20, 52, 22)); IC_M(ic_seg(X, Y, 20, 52, 44, 52, 22));
        IC_M(ic_seg(X, Y, 44, 52, 44, 20, 22)); IC_M(ic_seg(X, Y, 38, 12, 44, 20, 22)); IC_M(ic_seg(X, Y, 38, 12, 38, 20, 18)); IC_M(ic_seg(X, Y, 38, 20, 44, 20, 18));
        IC_A(ic_seg(X, Y, 26, 28, 38, 28, 16)); IC_A(ic_seg(X, Y, 26, 35, 38, 35, 16)); IC_A(ic_seg(X, Y, 26, 42, 34, 42, 16));
        break;
    case ICON_SETTINGS:                                  /* PARCHE 085: engranaje de 8 dientes */
        IC_M(IC_RING(ic_circ(X, Y, 32, 32, 15), 22));
        IC_M(ic_seg(X, Y, 32, 12, 32, 16, 34)); IC_M(ic_seg(X, Y, 32, 48, 32, 52, 34));
        IC_M(ic_seg(X, Y, 12, 32, 16, 32, 34)); IC_M(ic_seg(X, Y, 48, 32, 52, 32, 34));
        IC_M(ic_seg(X, Y, 18, 18, 21, 21, 34)); IC_M(ic_seg(X, Y, 43, 43, 46, 46, 34));
        IC_M(ic_seg(X, Y, 46, 18, 43, 21, 34)); IC_M(ic_seg(X, Y, 18, 46, 21, 43, 34));
        IC_A(ic_circ(X, Y, 32, 32, 6));
        break;
    case ICON_AGENTS:
    default:
        IC_M(ic_seg(X, Y, 32, 17, 15, 46, 18));
        IC_M(ic_seg(X, Y, 32, 17, 49, 46, 18));
        IC_M(ic_seg(X, Y, 15, 46, 49, 46, 18));
        IC_A(ic_circ(X, Y, 32, 17, 7));
        IC_A(ic_circ(X, Y, 15, 46, 7));
        IC_A(ic_circ(X, Y, 49, 46, 7));
        IC_M(ic_circ(X, Y, 32, 37, 3));
        break;
    }
    *dm = m;
    *da = a;
}

/* Composicion "over" con alfa 0..256 (a tambien en 0..256). */
static void ic_over(uint32_t *r, uint32_t *g, uint32_t *b, uint32_t *a,
                    uint32_t col, uint32_t c)
{
    if (!c) return;
    uint32_t ia = (*a * (256u - c)) >> 8;
    uint32_t na = ia + c;
    *r = (((col >> 16) & 255u) * c + *r * ia) / na;
    *g = (((col >> 8) & 255u) * c + *g * ia) / na;
    *b = ((col & 255u) * c + *b * ia) / na;
    *a = na;
}

static uint32_t ic_cov(int32_t d, int32_t pw)
{
    int32_t v = 128 - (d * 256) / pw;
    if (v < 0) v = 0;
    if (v > 256) v = 256;
    return (uint32_t)v;
}

static void her_icon_render(int icon, uint32_t col, int S, uint32_t *out)
{
    uint32_t acc = her_mix256(col, 0x00FFFFFFu, 170u);
    int32_t pw = 1024 / S;
    if (pw < 1) pw = 1;

    for (int j = 0; j < S; ++j) {
        int32_t Y = ((2 * j + 1) * 512) / S;
        for (int i = 0; i < S; ++i) {
            int32_t X = ((2 * i + 1) * 512) / S;
            int32_t dm, da;
            uint32_t r = 0, g = 0, b = 0, a = 0;
            her_icon_dist(icon, X, Y, &dm, &da);
            if (dm > 0 && dm < 112)
                ic_over(&r, &g, &b, &a, col, (uint32_t)((112 - dm) * 44 / 112));
            ic_over(&r, &g, &b, &a, col, ic_cov(dm, pw));
            ic_over(&r, &g, &b, &a, acc, ic_cov(da, pw));
            out[j * S + i] = ((a > 255u ? 255u : a) << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

static const uint32_t *her_icon_get(int i, int S)
{
    /* SOMA_208: 3 tamanos a la vez (dock, orbita, auxiliar). Antes habia uno solo y con un icono minimizado en orbita los 18
     * sprites se rasterizaban de nuevo en cada fotograma (109 ms medidos en QEMU). */
    if (S > HER_ICON_MAX_S) S = HER_ICON_MAX_S;
    if (!her_icon_sprites) {
        her_icon_sprites = (uint32_t *)kmalloc((size_t)HER_ICON_SLOTS * HER_DOCK_ITEM_COUNT *
                           HER_ICON_MAX_S * HER_ICON_MAX_S * sizeof(uint32_t));
        if (!her_icon_sprites) return 0;
        for (int z = 0; z < HER_ICON_SLOTS; ++z) { her_icon_slot_s[z] = 0; for (int k = 0; k < HER_DOCK_ITEM_COUNT; ++k) her_icon_ready2[z][k] = 0; }
    }
    int z = -1;
    for (int q = 0; q < HER_ICON_SLOTS; ++q) if (her_icon_slot_s[q] == S) { z = q; break; }
    if (z < 0) {
        z = her_icon_slot_next; her_icon_slot_next = (her_icon_slot_next + 1) % HER_ICON_SLOTS;
        her_icon_slot_s[z] = S;
        for (int k = 0; k < HER_DOCK_ITEM_COUNT; ++k) her_icon_ready2[z][k] = 0;
    }
    uint32_t *spr = her_icon_sprites + ((size_t)z * HER_DOCK_ITEM_COUNT + (size_t)i) * HER_ICON_MAX_S * HER_ICON_MAX_S;
    if (!her_icon_ready2[z][i]) {
        her_icon_render(her_dock_items[i].icon, her_dock_items[i].color, S, spr);
        her_icon_ready2[z][i] = 1;
    }
    return spr;
}

/* Copia el sprite (lado S) centrado, escalado a D px, con alfa global 0..256. */
static inline uint32_t icon_blend(uint32_t bg, uint32_t fg)      /* igual que fb_blend_color (mismo resultado), en linea */
{
    uint32_t a = (fg >> 24) & 0xFF;
    if (a == 0) return bg;
    if (a == 255) return fg | 0xFF000000;
    uint32_t inv = 255 - a, t;
    t = ((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv + 1; uint32_t r = (t + (t >> 8)) >> 8;
    t = ((fg >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * inv + 1;   uint32_t g = (t + (t >> 8)) >> 8;
    t = (fg & 0xFF) * a + (bg & 0xFF) * inv + 1;                 uint32_t b = (t + (t >> 8)) >> 8;
    return (r << 16) | (g << 8) | b | 0xFF000000;
}
/* SOMA_164C_T480S: los iconos del dock giran y se pegan escalados en cada fotograma (medido en el T480s: 5 ms, la parte
 * mas cara del escritorio tras repartir el orbe). Ahora: tablas de escalado (sin dos divisiones por pixel), la mezcla en
 * linea, y todos los iconos de un fotograma se pegan por franjas de filas entre los nucleos. Mismo resultado. */
#define ICON_Q_MAX 20
struct icon_q { int32_t x0, y0; const uint32_t *spr; int S, D; uint32_t bright; };
static struct icon_q icon_q[ICON_Q_MAX];
static int icon_qn;
static void icon_blit_rows(const struct icon_q *q, volatile uint32_t *buf, int32_t W, int32_t H, int32_t ya, int32_t yb)
{
    int D = q->D, S = q->S;
    uint16_t xs[HER_ICON_MAX_S];
    for (int x = 0; x < D; ++x) xs[x] = (uint16_t)(x * S / D);
    int y0 = ya - q->y0, y1 = yb - q->y0;
    if (y0 < 0) y0 = 0;
    if (y1 > D) y1 = D;
    for (int y = y0; y < y1; ++y) {
        int32_t py = q->y0 + y;
        if (py < 0 || py >= H) continue;
        const uint32_t *row = q->spr + (y * S / D) * S;
        volatile uint32_t *dst = buf + (uint32_t)py * (uint32_t)W;
        for (int x = 0; x < D; ++x) {
            int32_t px = q->x0 + x;
            if (px < 0 || px >= W) continue;
            uint32_t p = row[xs[x]];
            uint32_t al = ((p >> 24) * q->bright) >> 8;
            if (!al) continue;
            dst[px] = icon_blend(dst[px], (al << 24) | (p & 0x00FFFFFFu)) & 0x00FFFFFFu;
        }
    }
}
struct icon_job { volatile uint32_t *buf; int32_t W, H, ya, yb; };
static void icon_blit_part(int idx, int n, void *arg)
{
    struct icon_job *j = (struct icon_job *)arg;
    int32_t span = j->yb - j->ya;
    int32_t a = j->ya + (int32_t)((int64_t)span * idx / n), b = j->ya + (int32_t)((int64_t)span * (idx + 1) / n);
    for (int i = 0; i < icon_qn; i++) icon_blit_rows(&icon_q[i], j->buf, j->W, j->H, a, b);
}
static void icon_flush(void)
{
    if (!icon_qn) return;
    volatile uint32_t *buf = fb_get_buffer();
    int32_t W = (int32_t)fb_get_width(), H = (int32_t)fb_get_height();
    if (buf) {
        struct icon_job j = { buf, W, H, 0x7FFFFFFF, -0x7FFFFFFF };
        for (int i = 0; i < icon_qn; i++) { if (icon_q[i].y0 < j.ya) j.ya = icon_q[i].y0; if (icon_q[i].y0 + icon_q[i].D > j.yb) j.yb = icon_q[i].y0 + icon_q[i].D; }
        if (j.ya < 0) j.ya = 0;
        if (j.yb > H) j.yb = H;
        if (j.yb > j.ya) smp_parallel(icon_blit_part, &j);
    }
    icon_qn = 0;
}
/* Copia el sprite (lado S) centrado, escalado a D px, con alfa global 0..256 (en cola: se pega en icon_flush). */
static void her_icon_blit(int32_t cx, int32_t cy, const uint32_t *spr, int S, int D,
                          uint32_t bright)
{
    if (!fb_get_buffer() || !spr || D < 2) return;
    if (D > HER_ICON_MAX_S) D = HER_ICON_MAX_S;
    int32_t x0 = cx - D / 2, y0 = cy - D / 2;
    fb_damage(x0, y0, D, D);
    if (icon_qn >= ICON_Q_MAX) icon_flush();
    struct icon_q *q = &icon_q[icon_qn++];
    q->x0 = x0; q->y0 = y0; q->spr = spr; q->S = S; q->D = D; q->bright = bright;
}

/* SOMA_202_QEMU_LIGERO_V2: energia vectorial elegante y barata de dibujar.
 * Sustituye el panel anterior por dos medallones de trazos limpios, sin bitmaps
 * ni AA costoso. Se conserva la confirmacion con doble pulsacion. */
#define HER_POWER_W      86
#define HER_POWER_H      74
#define HER_POWER_GAP    10
#define HER_POWER_RIGHT  14
#define HER_POWER_BOTTOM 38
static int her_power_pending = 0;          /* 1 = apagar, 2 = reiniciar */
static uint64_t her_power_deadline = 0;

static int32_t her_power_x(uint32_t w, int action)
{
    return (int32_t)w - HER_POWER_RIGHT - 2 * HER_POWER_W - HER_POWER_GAP
           + (action - 1) * (HER_POWER_W + HER_POWER_GAP);
}

static int32_t her_power_y(uint32_t h)
{
    return (int32_t)h - HER_POWER_BOTTOM - HER_POWER_H;
}

static int her_power_hit(int32_t mx, int32_t my, uint32_t w, uint32_t h)
{
    if (!her_icons_visible || her_icons_anim_phase < HER_ICON_ANIM_MAX ||
        w < 240u || h < 200u) return 0;
    for (int action = 1; action <= 2; ++action) {
        int32_t x = her_power_x(w, action), y = her_power_y(h);
        if (mx >= x && mx < x + HER_POWER_W &&
            my >= y && my < y + HER_POWER_H) return action;
    }
    return 0;
}

static void her_power_press(int action)
{
    uint64_t now = timer_get_uptime_ms();
    if (action == her_power_pending && now < her_power_deadline) {
        her_power_pending = 0;
        if (action == 1) shell_system_poweroff();
        else             shell_system_reboot();
        return;
    }
    her_power_pending = action;
    her_power_deadline = now + 5000u;
}

/* SOMA_218_ENERGIA: tarjetas de apagar y reiniciar con los iconos vectoriales del sistema (sysicon), suavizadas y con estados
 * de reposo, raton encima y pendiente de confirmar (con la cuenta atras como barra). Mismas medidas y zonas de pulsacion. */
static void her_power_card(int32_t x, int32_t y, int action, int hover, int selected, uint32_t left_ms)
{
    const int32_t w = HER_POWER_W, h = HER_POWER_H;
    uint32_t accent = (action == 1) ? 0x00FB7185u : 0x0067E8F9u;
    uint32_t base = 0x000E1626u;
    uint32_t fill = selected ? si_mix(base, accent, 46) : hover ? 0x00152033u : base;
    uint32_t edge = selected ? accent : hover ? si_mix(0x002A3648u, accent, 140) : 0x00263246u;
    fb_draw_box_shadow(x, y, w, h, 16, 0, 5, 0, 14, 0x70000000u);
    fb_fill_rrect_aa(x, y, w, h, 16, 0xE6000000u | fill);
    fb_stroke_rrect_aa(x, y, w, h, 16, 0xFF000000u | edge);
    fb_fill_rect_alpha(x + 14, y + 1, w - 28, 1, 0x1CFFFFFFu);                        /* brillo del borde superior */

    int32_t cx = x + w / 2, cy = y + 29;
    uint32_t disc = si_mix(0x000A111Du, accent, selected ? 96u : hover ? 62u : 34u);
    fb_fill_rrect_aa(cx - 20, cy - 20, 40, 40, 20, 0xFF000000u | disc);
    fb_stroke_rrect_aa(cx - 20, cy - 20, 40, 40, 20, 0xFF000000u | si_mix(disc, accent, selected ? 220u : hover ? 170u : 110u));
    uint32_t ic = selected ? 0x00FFFFFFu : si_mix(accent, 0x00FFFFFFu, hover ? 110u : 50u);
    si_icon(action == 1 ? SI_POWER : SI_RESTART, cx, cy, 26, ic);

    uint32_t txt = selected ? 0x00FFFFFFu : hover ? 0x00F1F5F9u : 0x00B8C4D6u;
    si_text_c(x, y + 52, w, 16, action == 1 ? "Apagar" : "Reiniciar", 12, txt, 1);

    if (selected) {                                                                   /* cuenta atras de la confirmacion */
        int32_t bw = (w - 28) * (int32_t)(left_ms > 5000u ? 5000u : left_ms) / 5000;
        fb_fill_rrect_aa(x + 14, y + h - 6, w - 28, 3, 1, 0x40000000u | si_mix(base, accent, 80));
        if (bw > 0) fb_fill_rrect_aa(x + 14, y + h - 6, bw, 3, 1, 0xFF000000u | accent);
    }
}

static void her_draw_power_controls(uint32_t w, uint32_t h)
{
    if (!her_icons_visible || her_icons_anim_phase < 144u ||
        w < 240u || h < 200u) return;
    uint64_t now = timer_get_uptime_ms();
    if (her_power_pending && now >= her_power_deadline)
        her_power_pending = 0;
    int hov = (her_icons_anim_phase >= HER_ICON_ANIM_MAX) ? her_power_hit(g_mouse_x_s, g_mouse_y_s, w, h) : 0;

    for (int action = 1; action <= 2; ++action) {
        int32_t x = her_power_x(w, action), y = her_power_y(h);
        if (her_icons_anim_phase < HER_ICON_ANIM_MAX) {
            int32_t k = ((int32_t)her_icons_anim_phase - 144) * 256 / 112;
            if (k < 0) k = 0;
            if (k > 256) k = 256;
            int32_t ox = (int32_t)w / 2 - HER_POWER_W / 2;
            int32_t oy = (int32_t)h / 2 - HER_POWER_H / 2;
            x = ox + (x - ox) * k / 256;
            y = oy + (y - oy) * k / 256;
        }
        int selected = (her_power_pending == action);
        her_power_card(x, y, action, hov == action, selected, selected ? (uint32_t)(her_power_deadline - now) : 0u);
    }

    if (her_power_pending) {
        const char *msg = (her_power_pending == 1) ? "Pulsa otra vez para apagar" : "Pulsa otra vez para reiniciar";
        uint32_t accent = (her_power_pending == 1) ? 0x00FB7185u : 0x0067E8F9u;
        int32_t tw = si_text_w(msg, 12, 1) + 24, th = 26;
        int32_t right = her_power_x(w, 2) + HER_POWER_W;
        int32_t tx = right - tw, ty = her_power_y(h) - th - 10;
        if (tx < 4) tx = 4;
        fb_fill_rrect_aa(tx, ty, tw, th, 13, 0xE6000000u | si_mix(0x000E1626u, accent, 30));
        fb_stroke_rrect_aa(tx, ty, tw, th, 13, 0xFF000000u | si_mix(0x000E1626u, accent, 150));
        si_text(tx + 12, ty, th, msg, 12, 0x00F1F5F9u, 1);
    }
}


/* SOMA_202_QEMU_LIGERO_V2: gadget de estado simplificado para QEMU.
 * Diseno mas barato: sin rectangulos redondeados AA ni rellenos complejos.
 * El sondeo se hace como mucho una vez por segundo y el panel solo muestra
 * informacion real del sistema. */
#define HST_W      286
#define HST_H      116
#define HST_RIGHT   16
#define HST_TOP     16
struct hst_snapshot {
    int valid;
    uint64_t ms;
    char hora[9];
    char fecha[11];
    int bateria;  /* -1 desconocida; 0..100 conocida */
    int carga;    /* 0 bateria, 1 cargando, 2 con AC */
    int red;      /* 0 sin interfaz, 1 sin enlace, 2 con enlace sin IP,
                   * 3 enlace e IP, 4 estado no consultable */
    int volumen;  /* -1 sin controlador de audio */
};
static struct hst_snapshot hst;

static void hst_dos(char *p, unsigned n)
{
    p[0] = (char)('0' + (n / 10u) % 10u);
    p[1] = (char)('0' + (n % 10u));
}

static void hst_tres(char *p, unsigned n)
{
    p[0] = (char)('0' + (n / 100u) % 10u);
    p[1] = (char)('0' + (n / 10u) % 10u);
    p[2] = (char)('0' + (n % 10u));
}

static void hst_leer(void)
{
    uint64_t ms = timer_get_uptime_ms();
    if (hst.valid && ms >= hst.ms && ms - hst.ms < 1000u) return;
    hst.ms = ms;
    hst.valid = 1;

    {
        struct rtc_time t;
        hora_local(&t);                                  /* SOMA_332_HORA: antes UTC (una hora menos en invierno en Espana) */
        if (t.hour < 24u && t.min < 60u && t.sec < 60u) {
            hst_dos(hst.hora, t.hour); hst.hora[2] = ':';
            hst_dos(hst.hora + 3, t.min); hst.hora[5] = ':';
            hst_dos(hst.hora + 6, t.sec); hst.hora[8] = 0;
        } else {
            const char *s = "--:--:--";
            for (int i = 0; i < 9; ++i) hst.hora[i] = s[i];
        }
        if (t.day >= 1u && t.day <= 31u && t.month >= 1u && t.month <= 12u &&
            t.year >= 2000u && t.year <= 9999u) {
            hst_dos(hst.fecha, t.day); hst.fecha[2] = '/';
            hst_dos(hst.fecha + 3, t.month); hst.fecha[5] = '/';
            hst_dos(hst.fecha + 6, t.year / 100u);
            hst_dos(hst.fecha + 8, t.year % 100u);
            hst.fecha[10] = 0;
        } else {
            const char *s = "--/--/----";
            for (int i = 0; i < 11; ++i) hst.fecha[i] = s[i];
        }
    }

    hst.bateria = -1;
    hst.carga = 0;
#ifdef __x86_64__
    {
        struct acpi_power_state ps;
        acpi_power_state(&ps);
        if (ps.valid && ps.percent != 0xFFFFFFFFu && ps.percent <= 100u) {
            hst.bateria = (int)ps.percent;
            hst.carga = ps.charging ? 1 : (ps.ac == 1 ? 2 : 0);
        }
    }
#endif

    {
        const struct netif_ops *ni = netif_active();
        if (!ni) hst.red = 0;
        else if (!ni->link) hst.red = 4;
        else if (!ni->link()) hst.red = 1;
        else if ((net_ip[0] | net_ip[1] | net_ip[2] | net_ip[3]) == 0u) hst.red = 2;
        else hst.red = 3;
    }

    hst.volumen = -1;
#ifdef __x86_64__
    if (hda_ready()) {
        int v = hda_get_volume();
        if (v >= 0 && v <= 100) hst.volumen = v;
    }
#endif
}

static void hst_draw_battery_icon(int32_t x, int32_t y, uint32_t col, int pct)
{
    fb_draw_rect((uint32_t)x, (uint32_t)y, 20u, 10u, col);
    fb_fill_rect((uint32_t)(x + 20), (uint32_t)(y + 3), 2u, 4u, col);
    if (pct > 0) {
        uint32_t fill = (uint32_t)pct * 16u / 100u;
        if (!fill) fill = 1u;
        fb_fill_rect((uint32_t)(x + 2), (uint32_t)(y + 2), fill, 6u, col);
    }
}

static void hst_draw_net_icon(int32_t x, int32_t y, uint32_t col)
{
    fb_fill_rect((uint32_t)x,     (uint32_t)(y + 8), 3u, 3u, col);
    fb_fill_rect((uint32_t)(x+5), (uint32_t)(y + 6), 3u, 5u, col);
    fb_fill_rect((uint32_t)(x+10),(uint32_t)(y + 3), 3u, 8u, col);
    fb_fill_rect((uint32_t)(x+15),(uint32_t)y,       3u,11u, col);
}

static void hst_draw_audio_icon(int32_t x, int32_t y, uint32_t col)
{
    fb_fill_rect((uint32_t)x, (uint32_t)(y + 4), 4u, 6u, col);
    fb_draw_line(x + 4, y + 5, x + 10, y + 1, col);
    fb_draw_line(x + 10, y + 1, x + 10, y + 13, col);
    fb_draw_line(x + 10, y + 13, x + 4, y + 9, col);
    fb_draw_line(x + 14, y + 4, x + 17, y + 7, col);
    fb_draw_line(x + 17, y + 7, x + 14, y + 10, col);
}

static int16_t hst_ux, hst_uy;                    /* SOMA_208: posicion del usuario (milesimas +1; 0 = esquina superior derecha) */
static int     hst_gdrag; static int32_t hst_gx, hst_gy;
static void hst_pos(uint32_t ancho, uint32_t alto, int32_t *x, int32_t *y)
{
    if (hst_gdrag) { *x = hst_gx; *y = hst_gy; return; }
    if (hst_ux && ancho > (uint32_t)HST_W && alto > (uint32_t)HST_H) {
        *x = ((int32_t)hst_ux - 1) * ((int32_t)ancho - HST_W) / 1000;
        *y = ((int32_t)hst_uy - 1) * ((int32_t)alto - HST_H) / 1000;
        return;
    }
    *x = (int32_t)ancho - HST_W - HST_RIGHT; *y = HST_TOP;
}
static int hst_hit(int32_t mx, int32_t my, uint32_t ancho, uint32_t alto)
{
    if (!her_icons_visible || her_icons_anim_phase < HER_ICON_ANIM_MAX) return 0;
    int32_t x, y; hst_pos(ancho, alto, &x, &y);
    return mx >= x && mx < x + HST_W && my >= y && my < y + HST_H;
}
static void hst_dibujar(uint32_t ancho, uint32_t alto)
{
    if (!her_icons_visible || her_icons_anim_phase < 144u ||
        ancho < (uint32_t)(HST_W + 24) || alto < (uint32_t)(HST_H + 24)) return;
    hst_leer();

    {
        int32_t x, y; hst_pos(ancho, alto, &x, &y);
        if (her_icons_anim_phase < HER_ICON_ANIM_MAX) {
            int32_t k = ((int32_t)her_icons_anim_phase - 144) * 256 / 112;
            if (k < 0) k = 0;
            if (k > 256) k = 256;
            int32_t ox = (int32_t)ancho / 2 - HST_W / 2;
            int32_t oy = (int32_t)alto / 2 - HST_H / 2;
            x = ox + (x - ox) * k / 256;
            y = oy + (y - oy) * k / 256;
        }
        uint32_t bg = 0x00111B28u;
        uint32_t border = 0x0035495Eu;
        uint32_t title = 0x0099ACC0u;
        uint32_t text = 0x00EEF4FBu;
        uint32_t soft = 0x00D8E3EFu;
        uint32_t bc = (hst.bateria < 0) ? title :
                      (hst.bateria <= 15 && !hst.carga) ? 0x00FB7185u :
                      hst.carga ? 0x0086EFACu : 0x0067E8F9u;
        uint32_t nc = hst.red == 3 ? 0x0086EFACu :
                      (hst.red == 2 || hst.red == 4) ? 0x00FBBF24u : title;
        uint32_t vc = hst.volumen >= 0 ? 0x00C4B5FDu : title;

        fb_fill_rect((uint32_t)x, (uint32_t)y, HST_W, HST_H, bg);
        fb_draw_rect((uint32_t)x, (uint32_t)y, HST_W, HST_H, border);
        fb_fill_rect((uint32_t)x, (uint32_t)y, HST_W, 2u, 0x0067E8F9u);

        font_draw_string((uint32_t)(x + 12), (uint32_t)(y + 10), "SOMA / ESTADO", title, 1);
        font_draw_string((uint32_t)(x + 12), (uint32_t)(y + 30), hst.hora, text, 2);
        font_draw_string((uint32_t)(x + 172), (uint32_t)(y + 34), hst.fecha, soft, 1);
        fb_fill_rect((uint32_t)(x + 12), (uint32_t)(y + 58), HST_W - 24u, 1u, border);

        hst_draw_battery_icon(x + 12, y + 70, bc, hst.bateria >= 0 ? hst.bateria : 0);
        if (hst.bateria >= 0) {
            char s[] = "BATERIA 000%";
            hst_tres(s + 8, (unsigned)hst.bateria);
            font_draw_string((uint32_t)(x + 40), (uint32_t)(y + 71), s, bc, 1);
            if (hst.carga == 1) font_draw_string((uint32_t)(x + 140), (uint32_t)(y + 71), "CARGANDO", 0x0086EFACu, 1);
            else if (hst.carga == 2) font_draw_string((uint32_t)(x + 140), (uint32_t)(y + 71), "CON AC", 0x0086EFACu, 1);
        } else {
            font_draw_string((uint32_t)(x + 40), (uint32_t)(y + 71), "BATERIA NO DISPONIBLE", title, 1);
        }

        hst_draw_net_icon(x + 12, y + 89, nc);
        font_draw_string((uint32_t)(x + 40), (uint32_t)(y + 90),
                         hst.red == 3 ? "RED CON IP" :
                         hst.red == 2 ? "RED SIN IP" :
                         hst.red == 1 ? "SIN ENLACE" :
                         hst.red == 4 ? "RED N/D" :
                                        "SIN INTERFAZ", nc, 1);

        hst_draw_audio_icon(x + 150, y + 87, vc);
        if (hst.volumen >= 0) {
            char s[] = "AUDIO 000%";
            hst_tres(s + 6, (unsigned)hst.volumen);
            font_draw_string((uint32_t)(x + 176), (uint32_t)(y + 90), s, vc, 1);
        } else {
            font_draw_string((uint32_t)(x + 176), (uint32_t)(y + 90), "AUDIO N/D", vc, 1);
        }
    }
}


static int32_t ofx_radius(int32_t h);
/* SOMA_203_INSTANCIAS: solo las ventanas minimizadas orbitan el agujero negro.
 * Un icono se restaura con clic. Arrastrarlo al horizonte lo destruye. */
static void her_min_orbit_position(int i, uint32_t w, uint32_t h,
                                   uint64_t now, int32_t *x, int32_t *y)
{
    int32_t cx = (int32_t)w / 2, cy = (int32_t)h / 2;
    int32_t r = ofx_radius((int32_t)h) + 55;
    if (r < 90) r = 90;
    if (r > (int32_t)h / 2 - 40) r = (int32_t)h / 2 - 40;
    if (r < 45) r = 45;
    int32_t ry = r * 3 / 4;
    uint32_t a = ((uint32_t)(now % 28000u) * 1024u / 28000u + her_min_phase[i]) & 1023u;
    *x = cx + her_cs(a) * r / 1024;
    *y = cy + her_sn(a) * ry / 1024;
}
static int her_min_dock_index(enum app_mode app)
{
    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i)
        if (her_dock_items[i].app == app) return i;
    return -1;
}
static void her_draw_minimized_icons(uint32_t w, uint32_t h)
{
    if (!her_min_count || w < 240u || h < 240u) return;
    uint64_t now = timer_get_uptime_ms();
    int s = her_icon_canvas(h);
    if (s > 48) s = 48;
    for (int i = 0; i < her_min_count; ++i) {
        int32_t x, y;
        her_min_orbit_position(i, w, h, now, &x, &y);
        if (her_min_drag == i && mouse_left_held()) { x = her_min_drag_x; y = her_min_drag_y; }
        int id = her_min_dock_index(her_minimized[i].app);
        if (id < 0) id = 0; /* Aplicacion SOMA sin icono propio en el dock. */
        const uint32_t *spr = her_icon_get(id, s);
        uint32_t ik = 256u;
        if (her_mfly_on && (int)her_minimized[i].app == her_mfly_app && her_minimized[i].term_slot == her_mfly_slot) {
            ik = her_mfly_icon_k;                                          /* SOMA_223: aun en vuelo */
            if (ik < 8u) continue;
        }
        if (spr) her_icon_blit(x, y, spr, s, ik >= 256u ? s : s * (int)(192u + ik / 4u) / 256, ik);
        /* Insignia de instancia individual, p.ej. dos terminales. */
        uint32_t number = (uint32_t)(her_minimized[i].term_slot);
        if (number > 0u) {
            char b[3] = { (char)('0' + number / 10u), (char)('0' + number % 10u), 0 };
            font_draw_string((uint32_t)(x + 14), (uint32_t)(y - 18),
                             number < 10u ? b + 1 : b, 0x00E2E8F0u, 1);
        }
    }
    icon_flush();
}
static int her_min_hit(int32_t mx, int32_t my, uint32_t w, uint32_t h)
{
    if (!her_min_count || w < 240u || h < 240u) return -1;
    uint64_t now = timer_get_uptime_ms();
    for (int i = her_min_count - 1; i >= 0; --i) {
        int32_t x, y;
        her_min_orbit_position(i, w, h, now, &x, &y);
        if (mx >= x - 30 && mx <= x + 30 && my >= y - 30 && my <= y + 30) return i;
    }
    return -1;
}
static int her_min_drop_center(int32_t mx, int32_t my, uint32_t w, uint32_t h)
{
    int32_t dx = mx - (int32_t)w / 2, dy = my - (int32_t)h / 2;
    int32_t r = ofx_radius((int32_t)h);
    if (r < 30) r = 30;
    return dx * dx + dy * dy <= (r + 8) * (r + 8);
}

static int her_dock_hit(int32_t mx, int32_t my, uint32_t cur_w, uint32_t cur_h);
static int her_dock_hit_index(int32_t mx, int32_t my, uint32_t cur_w, uint32_t cur_h);
/* SOMA_206: el nombre solo aparece al pasar el raton por encima de un icono. */
static void her_tip_box(int32_t x, int32_t y, const char *t)
{
    int32_t n = 0; while (t[n]) ++n;
    int32_t bw = n * 8 + 14, bh = 20;
    int32_t bx = x - bw / 2; if (bx < 4) bx = 4;
    if (bx + bw > (int32_t)fb_get_width() - 4) bx = (int32_t)fb_get_width() - 4 - bw;
    if (y < 4) y = 4;
    fb_fill_rect((uint32_t)bx, (uint32_t)y, (uint32_t)bw, (uint32_t)bh, 0x000B1220u);
    fb_fill_rect((uint32_t)bx, (uint32_t)y, (uint32_t)bw, 1u, 0x0067E8F9u);
    fb_fill_rect((uint32_t)bx, (uint32_t)(y + bh - 1), (uint32_t)bw, 1u, 0x00334155u);
    fb_fill_rect((uint32_t)bx, (uint32_t)y, 1u, (uint32_t)bh, 0x00334155u);
    fb_fill_rect((uint32_t)(bx + bw - 1), (uint32_t)y, 1u, (uint32_t)bh, 0x00334155u);
    font_draw_string((uint32_t)(bx + 7), (uint32_t)(y + 6), t, 0x00E2F1FFu, 1);
}
static void her_draw_hover_tooltip(uint32_t cur_w, uint32_t cur_h, uint64_t now_ms)
{
    if (her_icons_anim_phase < HER_ICON_ANIM_MAX || (her_dp_kind && her_dp_moved)) return;
    int32_t mx = g_mouse_x_s, my = g_mouse_y_s;
    int app = her_dock_hit(mx, my, cur_w, cur_h);
    if (app >= 0) {
        for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) if ((int)her_dock_items[i].app == app) {
            int32_t tx, ty; her_icon_orbit_target(i, cur_w, cur_h, now_ms, &tx, &ty);
            her_tip_box(tx, ty + her_icon_canvas(cur_h) * 11 / 32 + 6, her_dock_items[i].label);
            break;
        }
    } else {
        int mi = her_min_hit(mx, my, cur_w, cur_h);
        if (mi >= 0) {
            int id = her_min_dock_index(her_minimized[mi].app);
            int32_t x, y; her_min_orbit_position(mi, cur_w, cur_h, now_ms, &x, &y);
            {   /* SOMA_291_CERRAR_ORBITA: como se abre y como se cierra */
                char t[64]; int n = 0;
                for (const char *c = id >= 0 ? her_dock_items[id].label : "APP"; *c && n < 30; ++c) t[n++] = *c;
                for (const char *c = "  (boton derecho: cerrar)"; *c && n < (int)sizeof t - 1; ++c) t[n++] = *c;
                t[n] = 0;
                her_tip_box(x, y + 28, t);
            }
        }
    }
    if (her_hint_msg && now_ms < her_hint_until)
        her_tip_box((int32_t)cur_w / 2, (int32_t)cur_h / 2 + her_icon_canvas(cur_h) + 70, her_hint_msg);
}

static void her_draw_dock(uint32_t cur_w, uint32_t cur_h)
{
    uint64_t now_ms = timer_get_uptime_ms();
    her_draw_icon_burst(cur_w, cur_h, now_ms);
    if (her_icons_anim_phase < HER_ICON_ANIM_MAX && her_hint_msg && now_ms < her_hint_until)   /* SOMA_219_AVISO: los avisos tambien */
        her_tip_box((int32_t)cur_w / 2, (int32_t)cur_h / 2 + her_icon_canvas(cur_h) + 70, her_hint_msg);   /* sin los iconos desplegados */
    if (her_icons_anim_phase == 0u) return;

    int32_t cx = (int32_t)(cur_w / 2u);
    int32_t cy = (int32_t)(cur_h / 2u);
    int S = her_icon_canvas(cur_h);
    int32_t half_px = S * 11 / 32;

    if (her_idrag >= 0) {                                /* SOMA_218_CUADRICULA: puntos de la rejilla y la celda de destino */
        int32_t margin, gap, y0, dy, w = (int32_t)cur_w, h = (int32_t)cur_h;
        her_grid_params(cur_w, cur_h, &margin, &gap, &y0, &dy);
        int32_t sx = her_idrag_x, sy = her_idrag_y;
        her_grid_snap(cur_w, cur_h, &sx, &sy);
        int32_t ry = y0; while (ry - dy >= 36) ry -= dy;
        for (; ry <= h - 36; ry += dy) {
            int32_t last_l = margin;
            for (int32_t c = 0; c < 64; ++c) {
                int32_t gx = margin + c * gap; if (c && gx > w / 2) break;
                fb_fill_rrect_aa(gx - 2, ry - 2, 4, 4, 2, 0x50A5C8EBu); last_l = gx;
            }
            for (int32_t k = 0; k < 64; ++k) {
                int32_t gx = w - margin - k * gap; if (gx <= w / 2 || gx - last_l < gap * 3 / 4) break;
                fb_fill_rrect_aa(gx - 2, ry - 2, 4, 4, 2, 0x50A5C8EBu);
            }
        }
        int32_t bs = half_px * 2 + 16;
        fb_fill_rrect_aa(sx - bs / 2, sy - bs / 2, bs, bs, 16, 0x2867E8F9u);
        fb_stroke_rrect_aa(sx - bs / 2, sy - bs / 2, bs, bs, 16, 0xA067E8F9u);
    }

    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
        uint32_t p = her_icon_item_progress(i);
        if (p == 0u) continue;

        int32_t target_x, target_y;
        her_icon_orbit_target(i, cur_w, cur_h, now_ms, &target_x, &target_y);

        int32_t px = cx + ((target_x - cx) * (int32_t)p) / (int32_t)HER_ICON_ANIM_MAX;
        int32_t py = cy + ((target_y - cy) * (int32_t)p) / (int32_t)HER_ICON_ANIM_MAX;
        if (p < HER_ICON_ANIM_MAX) her_draw_icon_materialization(i, px, py, p, now_ms, cur_w, cur_h);

        /* Fase final: el icono crece y se enciende donde se condensan las particulas. */
        if (p < 200u) continue;
        uint32_t solid = p - 200u;                       /* 0..56 */
        uint32_t bright = 40u + (solid * 216u) / 56u;
        if (bright > 256u) bright = 256u;
        int D = S * (80 + (int)((solid * 176u) / 56u)) / 256;
        if (D > S) D = S;

        const uint32_t *spr = her_icon_get(i, S);
        if (spr) her_icon_blit(target_x, target_y, spr, S, D, bright);
    }
    icon_flush();                                        /* SOMA_164C: todos los iconos a la vez, repartidos; despues los textos */
    if (her_icons_anim_phase >= 240u && !her_icon_custom_n) {
        static const char *const labels[5] = { "ASISTENTES", "SISTEMA", "TRABAJO", "MEDIA", "CAPTURA" };
        static const int anchors[5] = { 0, 1, 3, 4, 16 };
        for (int g = 0; g < 5; ++g) {
            int32_t gx, gy;
            her_icon_orbit_target(anchors[g], cur_w, cur_h, now_ms, &gx, &gy);
            int32_t n = 0;
            while (labels[g][n]) n++;
            int32_t labelx = gx - n * 4;
            font_draw_string((uint32_t)(labelx > 0 ? labelx : 0), (uint32_t)(gy - 38), labels[g], 0x005B7691u, 1);
        }
    }
    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
        uint32_t p = her_icon_item_progress(i);
        if (p < 200u) continue;
        int32_t target_x, target_y;
        her_icon_orbit_target(i, cur_w, cur_h, now_ms, &target_x, &target_y);
        if (p >= HER_ICON_ANIM_MAX) {
            int32_t label_y = target_y + half_px + 4;

            /* Indicador de app abierta: punto brillante bajo la etiqueta. */
            int is_open = (her_find_window(her_dock_items[i].app) >= 0);
            if (is_open) {
                uint32_t dot_col = (current_app == her_dock_items[i].app) ? 0x00E2F1FFu : 0x0064748Bu;
                fb_fill_circle(target_x, label_y + 16, (current_app == her_dock_items[i].app) ? 3 : 2, dot_col);
            }
        }
    }
    her_draw_hover_tooltip(cur_w, cur_h, now_ms);
    her_draw_power_controls(cur_w, cur_h);
}

static int her_dock_hit_index(int32_t mx, int32_t my, uint32_t cur_w, uint32_t cur_h)
{
    if (!her_icons_visible) return -1;
    uint64_t now_ms = timer_get_uptime_ms();
    const int32_t half = her_icon_canvas(cur_h) * 11 / 32;

    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
        uint32_t p = her_icon_item_progress(i);
        if (p < 232u) continue;

        int32_t target_x, target_y;
        her_icon_orbit_target(i, cur_w, cur_h, now_ms, &target_x, &target_y);
        if (mx >= target_x - half - 6 && mx <= target_x + half + 6 &&
            my >= target_y - half - 6 && my <= target_y + half + 14)
            return i;
    }
    return -1;
}
static int her_dock_hit(int32_t mx, int32_t my, uint32_t cur_w, uint32_t cur_h)
{
    int i = her_dock_hit_index(mx, my, cur_w, cur_h);
    return i < 0 ? -1 : (int)her_dock_items[i].app;
}

