/* gui_ventanas.c: arrastre de iconos y gadget, barra de titulo, redimension y terminales.
 * SOMA_234_GUI_PARTIDO: NO se compila por separado. Lo incluye src/ui/gui.c en su sitio (una sola unidad de
 * compilacion: comparte el estado del escritorio con gui.c, y el nucleo compilado sale identico). */
/* SOMA_208_ESCRITORIO: arrastrar iconos y gadget. Un toque sin mover (umbral 8 px) sigue abriendo la aplicacion. */
static void her_desk_save(void)
{
    char b[640]; int n = 0;
    for (int i = 0; i < HER_DOCK_ITEM_COUNT && n < 560; ++i) if (her_icon_ux[i]) {
        for (const char *t = her_dock_items[i].label; *t && n < 560; ++t) b[n++] = *t;
        int v[2] = { her_icon_ux[i] - 1, her_icon_uy[i] - 1 };
        for (int q = 0; q < 2; ++q) {
            b[n++] = ' '; char d[6]; int m = 0; int x = v[q]; if (x < 0) x = 0;
            do { d[m++] = (char)('0' + x % 10); x /= 10; } while (x && m < 5);
            while (m) b[n++] = d[--m];
        }
        b[n++] = '\n';
    }
    if (hst_ux && n < 600) {
        const char *t = "GADGET"; while (*t) b[n++] = *t++;
        int v[2] = { hst_ux - 1, hst_uy - 1 };
        for (int q = 0; q < 2; ++q) {
            b[n++] = ' '; char d[6]; int m = 0; int x = v[q]; if (x < 0) x = 0;
            do { d[m++] = (char)('0' + x % 10); x /= 10; } while (x && m < 5);
            while (m) b[n++] = d[--m];
        }
        b[n++] = '\n';
    }
    (void)vfs_write("/etc/escritorio.conf", b, (uint32_t)n);
}
static void her_desk_count(void)
{
    her_icon_custom_n = 0;
    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) if (her_icon_ux[i]) her_icon_custom_n++;
    her_desk_ver++;
}
static void her_desk_load(void)
{
    char b[640]; int n = vfs_read("/etc/escritorio.conf", b, sizeof b - 1);
    if (n <= 0) return;
    b[n] = 0;
    for (char *l = b; *l; ) {
        char *e = l; while (*e && *e != '\n') ++e;
        char sv = *e; *e = 0;
        char *sp = l; while (*sp && *sp != ' ') ++sp;
        if (*sp) {
            *sp = 0; int v[2] = { 0, 0 }; char *q = sp + 1;
            for (int k = 0; k < 2; ++k) { while (*q == ' ') ++q; while (*q >= '0' && *q <= '9') { v[k] = v[k] * 10 + (*q - '0'); ++q; } }
            if (v[0] > 1000) v[0] = 1000;
            if (v[1] > 1000) v[1] = 1000;
            if (l[0] == 'G' && l[1] == 'A' && l[2] == 'D') { hst_ux = (int16_t)(v[0] + 1); hst_uy = (int16_t)(v[1] + 1); }
            else for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
                const char *a = her_dock_items[i].label; const char *c = l;
                while (*a && *a == *c) { ++a; ++c; }
                if (!*a && !*c) { her_icon_ux[i] = (int16_t)(v[0] + 1); her_icon_uy[i] = (int16_t)(v[1] + 1); break; }
            }
        }
        if (!sv) break;
        l = e + 1;
    }
    her_desk_count();
}
void gui_desktop_layout_reset(void)
{
    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) her_icon_ux[i] = her_icon_uy[i] = 0;
    hst_ux = hst_uy = 0;
    her_desk_count(); her_desk_save();
}
static int     her_dp_idx = -1;
static int32_t her_dp_sx, her_dp_sy, her_dp_ox, her_dp_oy;
static int her_dp_begin(int32_t mx, int32_t my, uint32_t w, uint32_t h)
{
    uint64_t now = timer_get_uptime_ms();
    int i = her_dock_hit_index(mx, my, w, h);
    if (i >= 0) {
        int32_t tx, ty; her_icon_orbit_target(i, w, h, now, &tx, &ty);
        her_dp_kind = 1; her_dp_idx = i; her_dp_moved = 0; her_dp_sx = mx; her_dp_sy = my; her_dp_ox = tx - mx; her_dp_oy = ty - my;
        return 1;
    }
    if (hst_hit(mx, my, w, h)) {
        int32_t gx, gy; hst_pos(w, h, &gx, &gy);
        her_dp_kind = 2; her_dp_idx = -1; her_dp_moved = 0; her_dp_sx = mx; her_dp_sy = my; her_dp_ox = gx - mx; her_dp_oy = gy - my;
        return 1;
    }
    return 0;
}
/* Devuelve 1 si hay que repintar. */
static int her_dp_motion(int32_t mx, int32_t my, uint32_t w, uint32_t h)
{
    if (!her_dp_kind) return 0;
    if (!her_dp_moved) {
        int32_t dx = mx - her_dp_sx, dy = my - her_dp_sy;
        if (dx * dx + dy * dy < 64) return 0;
        her_dp_moved = 1;
    }
    if (her_dp_kind == 1) {
        int32_t x = mx + her_dp_ox, y = my + her_dp_oy;
        if (x < 36) x = 36;
        if (x > (int32_t)w - 36) x = (int32_t)w - 36;
        if (y < 36) y = 36;
        if (y > (int32_t)h - 36) y = (int32_t)h - 36;
        her_idrag = her_dp_idx; her_idrag_x = x; her_idrag_y = y;
    } else {
        int32_t x = mx + her_dp_ox, y = my + her_dp_oy;
        if (x < 0) x = 0;
        if (x > (int32_t)w - HST_W) x = (int32_t)w - HST_W;
        if (y < 0) y = 0;
        if (y > (int32_t)h - HST_H) y = (int32_t)h - HST_H;
        hst_gdrag = 1; hst_gx = x; hst_gy = y;
    }
    return 1;
}
/* Al soltar. Devuelve la aplicacion a abrir si fue un toque en un icono, o -1. */
static int her_dp_end(uint32_t w, uint32_t h)
{
    int kind = her_dp_kind, idx = her_dp_idx, moved = her_dp_moved;
    her_dp_kind = 0; her_dp_idx = -1; her_dp_moved = 0;
    if (kind == 1) {
        int32_t fx = her_idrag_x, fy = her_idrag_y;
        her_idrag = -1;
        if (!moved) return (int)her_dock_items[idx].app;
        her_grid_snap(w, h, &fx, &fy);                                          /* SOMA_218_CUADRICULA: a la celda mas cercana */
        uint64_t now = timer_get_uptime_ms();
        int32_t ox, oy; her_icon_orbit_target(idx, w, h, now, &ox, &oy);       /* donde estaba */
        for (int j = 0; j < HER_DOCK_ITEM_COUNT; ++j) {                         /* celda ocupada = intercambiar */
            if (j == idx) continue;
            int32_t tx, ty; her_icon_orbit_target(j, w, h, now, &tx, &ty);
            if (tx - fx < 24 && fx - tx < 24 && ty - fy < 24 && fy - ty < 24) {
                her_icon_ux[j] = (int16_t)((ox * 1000 + (int32_t)w / 2) / (int32_t)w + 1); her_icon_uy[j] = (int16_t)((oy * 1000 + (int32_t)h / 2) / (int32_t)h + 1);
                break;
            }
        }
        her_icon_ux[idx] = (int16_t)((fx * 1000 + (int32_t)w / 2) / (int32_t)w + 1); her_icon_uy[idx] = (int16_t)((fy * 1000 + (int32_t)h / 2) / (int32_t)h + 1);
        her_desk_count(); her_desk_save();
    } else if (kind == 2) {
        if (moved && w > (uint32_t)HST_W && h > (uint32_t)HST_H) {
            hst_gdrag = 0;
            hst_ux = (int16_t)(hst_gx * 1000 / ((int32_t)w - HST_W) + 1); hst_uy = (int16_t)(hst_gy * 1000 / ((int32_t)h - HST_H) + 1);
            her_desk_ver++; her_desk_save();
        }
        hst_gdrag = 0;
    }
    return -1;
}

/* SOMA_PARCHE_028_UI: icono vectorial de la aplicacion para la barra de titulo (-1 = ninguno) */
static int her_app_vicon(enum app_mode app)
{
    switch (app) {
    case APP_AGENT_CHAT:     return VICON_AGENT;
    case APP_TERMINAL_SHELL: return VICON_TERMINAL;
    case APP_WEB_BROWSER:    return VICON_BROWSER;
    case APP_FILE_MANAGER:   return VICON_FILES;
    case APP_IMAGE_VIEWER:   return VICON_IMAGE;
    case APP_NOTEPAD:        return VICON_NOTES;
    case APP_AGENT_MEMORY:   return VICON_MEMORY;
    case APP_LAYOUT:         return VICON_LAYOUT;
    case APP_AGENTS:         return VICON_AGENTS;
    case APP_PAINT:          return VICON_PAINT;
    default:                 return -1;
    }
}

static uint32_t her_win_border_color(int is_active) { return is_active ? 0xFF3B82B4u : 0xFF2A3446u; }
static int her_in_refresh = 0;                       /* her_refresh_all dibuja el marco de cada ventana por su cuenta */
static struct fb_cguard her_cguards[HER_MAX_WINDOWS];

/* Sombra o halo exterior: se dibuja antes de guardar el fondo de las esquinas */
static void her_draw_window_shadow(struct her_win_node *win, int is_active)
{
    if (!win) return;
    if (is_active) {
        fb_draw_shadow((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h, HER_WIN_R, 22, 8, 0xA0000000u);
        fb_draw_shadow((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h, HER_WIN_R, 14, 0, 0x2638BDF8u);
    } else {
        fb_draw_shadow((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h, HER_WIN_R, 14, 5, 0x80000000u);
    }
}

static void her_draw_one_window_chrome(struct her_win_node *win, int is_active)
{
    if (!win) return;
    int32_t x = (int32_t)win->x, y = (int32_t)win->y, w = (int32_t)win->w, h = (int32_t)win->h;
    uint32_t title_top  = is_active ? 0x00222E44 : 0x00141B2A;
    uint32_t title_bot  = is_active ? 0x001A2334 : 0x00111724;
    uint32_t title_txt  = is_active ? 0x00F1F5F9 : 0x008391A7;
    uint32_t body_bg    = 0x000B1220;

    /* cuerpo y barra de titulo (solo las esquinas superiores quedan a cargo de este dibujo; las inferiores, de la guarda) */
    fb_fill_rounded_rect(x, y, w, h, HER_WIN_R, body_bg);
    fb_fill_rounded_rect(x, y, w, HER_TITLE_H + HER_WIN_R, HER_WIN_R, title_top);
    fb_fill_rect((uint32_t)x, (uint32_t)(y + HER_TITLE_H), (uint32_t)w, HER_WIN_R, body_bg);
    fb_gradient_v(x + 1, y + HER_WIN_R, w - 2, HER_TITLE_H - HER_WIN_R, title_top, title_bot);
    fb_fill_rect((uint32_t)(x + 1), (uint32_t)(y + HER_TITLE_H - 1), (uint32_t)(w - 2), 1, is_active ? 0x000F1624 : 0x000C111C);
    fb_stroke_rrect_aa(x, y, w, h, HER_WIN_R, her_win_border_color(is_active));

    const char *title = "SOMA";
    switch (win->app) {
        case APP_AGENT_CHAT:     title = "Agente SOMA"; break;
        case APP_TERMINAL_SHELL: title = "Terminal"; break;
        case APP_WEB_BROWSER:    title = "Navegador"; break;
        case APP_NOTEPAD: case APP_IMAGE_VIEWER: case APP_CODER_APP: case APP_PAINT: case APP_FILE_MANAGER: case APP_PDF: {                /* SOMA_211_NOTAS / SOMA_212: el archivo de esa ventana */
            static char nt[80]; const char *fn = her_inst_name(win->app, win->term_slot); uint32_t q = 0;
            const char *a = her_app_base_title(win->app); while (*a && q < sizeof nt - 1) nt[q++] = *a++;
            if (fn[0]) { a = "  -  "; while (*a && q < sizeof nt - 1) nt[q++] = *a++; a = fn; while (*a && q < sizeof nt - 1) nt[q++] = *a++; }
            nt[q] = 0; title = nt; break; }
        case APP_AGENT_MEMORY:   title = "Memoria"; break;
        case APP_LAYOUT:         title = "Maquetador"; break;
        case APP_AGENTS:         title = "Agentes"; break;
        case APP_MEDIA:          title = "SOMA Media"; break;
        case APP_SETTINGS:       title = "Ajustes"; break;
        case APP_SYSMON:         title = "Monitor del sistema"; break;
        case APP_RECORDER:       title = "Grabadora"; break;
        case APP_CAMERA:         title = "Camara"; break;                     /* SOMA_178_T480S */
        case APP_CAPTURE:        title = "Captura"; break;                    /* SOMA_179_T480S */
        case APP_SOMA_APP:       title = gui_soma_win_get_title(); break; /*PL12_CHROME_TITLE*/
        default: break;
    }
    int32_t tx0 = x + 12;
    int vi = her_app_vicon(win->app);
    if (vi >= 0) { vicon_draw(vi, x + 10, y + (HER_TITLE_H - 16) / 2, 16, is_active ? 0x0038BDF8 : 0x0055657C); tx0 = x + 33; }
    fb_set_clip(x + 4, y, w > 44 ? w - 44 : 1, HER_TITLE_H);
    if (wf_init()) {
        uint32_t n = 0; while (title && title[n]) n++;
        wf_draw(tx0, y + (HER_TITLE_H - 13) / 2 + wf_ascent(13) - 1, title, n, 13, title_txt, 0, 255);
    } else {
        font_draw_string((uint32_t)tx0, (uint32_t)(y + 9), title, title_txt, 1);
    }
    fb_reset_clip();

    /* boton de cierre (misma zona activa de siempre: 24 px a la derecha) */
    {
        int32_t cx16 = (x + w - 16) * 16, cy16 = (y + HER_TITLE_H / 2) * 16;
        fb_fill_rrect_aa(x + w - 26, y + HER_TITLE_H / 2 - 10, 20, 20, 10, is_active ? 0xFFE5484Du : 0xFF2A3446u);
        uint32_t xc = is_active ? 0xFFFFFFFFu : 0xFF7C8AA0u;
        fb_line_aa(cx16 - 56, cy16 - 56, cx16 + 56, cy16 + 56, 24, xc);
        fb_line_aa(cx16 - 56, cy16 + 56, cx16 + 56, cy16 - 56, 24, xc);
    }
}

static void her_draw_window_chrome(uint32_t cur_w, uint32_t cur_h)
{
    (void)cur_w; (void)cur_h;
    if (her_in_refresh) return;
    for (int i = 0; i < her_num_windows; ++i) {
        her_draw_window_shadow(&her_windows[i], (i == her_num_windows - 1));
        her_draw_one_window_chrome(&her_windows[i], (i == her_num_windows - 1));
    }
}

/* PARCHE 085: puntero de redimension segun el borde (1 izq, 2 der, 4 arriba, 8 abajo) */
static int her_resize_cursor(int edge)
{
    if (!edge) return -1;
    int h = edge & 3, v = edge & 12;
    if (h && v) return ((edge & 1) && (edge & 4)) || ((edge & 2) && (edge & 8)) ? CURSOR_RESIZE_NWSE : CURSOR_RESIZE_NESW;
    return h ? CURSOR_RESIZE_H : CURSOR_RESIZE_V;
}
static int her_is_resize_cursor(int c) { return c == CURSOR_CROSSHAIR || c == CURSOR_RESIZE_H || c == CURSOR_RESIZE_V || c == CURSOR_RESIZE_NWSE || c == CURSOR_RESIZE_NESW; }

static int her_win_hit_target(struct her_win_node *win, int32_t mx, int32_t my, int *out_edge)
{
    *out_edge = 0;
    if (!win) return 0;
    const int32_t OUT = 8, IN = 5, CORNER = 14;
    int32_t x = (int32_t)win->x, y = (int32_t)win->y;
    int32_t w = (int32_t)win->w, h = (int32_t)win->h;

    if (mx < x - OUT || mx > x + w + OUT || my < y - OUT || my > y + h + OUT)
        return 0;

    /* Boton cerrar */
    if (mx >= x + w - 28 && mx <= x + w - 4 && my >= y + 2 && my <= y + HER_TITLE_H - 2)
        return 3;

    /* Tirador de esquina inferior derecha */
    if (mx >= x + w - 18 && my >= y + h - 18) { *out_edge = 2 | 8; return 2; }

    int edge = 0;
    if (mx <= x + IN)     edge |= 1;
    if (mx >= x + w - IN) edge |= 2;
    if (my <= y + IN)     edge |= 4;
    if (my >= y + h - IN) edge |= 8;
    /* Esquinas: zona ampliada para poder redimensionar en diagonal */
    if (edge & 3)  { if (my <= y + CORNER) edge |= 4; else if (my >= y + h - CORNER) edge |= 8; }
    if (edge & 12) { if (mx <= x + CORNER) edge |= 1; else if (mx >= x + w - CORNER) edge |= 2; }
    if (edge) { *out_edge = edge; return 2; }

    if (my >= y && my < y + HER_TITLE_H) return 1;
    return 4;
}

static int __attribute__((unused)) her_win_hit(int32_t mx, int32_t my, int *out_edge)
{
    *out_edge = 0;
    struct her_win_node *win = her_get_active_win();
    if (!win) return 0;
    return her_win_hit_target(win, mx, my, out_edge);
}

static void her_win_apply_motion(int32_t mx, int32_t my, uint32_t cur_w, uint32_t cur_h)
{
    struct her_win_node *win = (her_num_windows > 0) ? &her_windows[her_num_windows - 1] : 0;
    if (!win) return;
    int32_t max_w = (int32_t)cur_w - 20;
    int32_t max_h = (int32_t)cur_h - 20;
    int32_t min_w = (int32_t)her_app_min_w(win->app);
    int32_t min_h = (cur_h < 650u) ? HER_MIN_H : 260;
    if (min_w > max_w) min_w = max_w;
    if (min_h > max_h) min_h = max_h;

    if (her_dragging) {
        int32_t nx = mx - her_drag_ox;
        int32_t ny = my - her_drag_oy;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx + (int32_t)win->w > (int32_t)cur_w) nx = (int32_t)cur_w - (int32_t)win->w;
        if (ny + (int32_t)win->h > (int32_t)cur_h) ny = (int32_t)cur_h - (int32_t)win->h;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        win->x = (uint32_t)nx;
        win->y = (uint32_t)ny;
        her_win_x = win->x; her_win_y = win->y;
    } else if (her_resizing) {
        int32_t dx = mx - her_resize_start_mx;
        int32_t dy = my - her_resize_start_my;
        int32_t new_x = her_resize_start_x;
        int32_t new_y = her_resize_start_y;
        int32_t new_w = her_resize_start_w;
        int32_t new_h = her_resize_start_h;

        /* Borde derecho */
        if (her_resize_edge & 2) {
            new_w = her_resize_start_w + dx;
            if (new_w < min_w) new_w = min_w;
            if (new_x + new_w > (int32_t)cur_w) new_w = (int32_t)cur_w - new_x;
        }
        /* Borde izquierdo */
        if (her_resize_edge & 1) {
            new_w = her_resize_start_w - dx;
            new_x = her_resize_start_x + dx;
            if (new_w < min_w) {
                new_x = her_resize_start_x + (her_resize_start_w - min_w);
                new_w = min_w;
            }
            if (new_x < 0) {
                new_w += new_x;
                new_x = 0;
            }
        }
        /* Borde inferior */
        if (her_resize_edge & 8) {
            new_h = her_resize_start_h + dy;
            if (new_h < min_h) new_h = min_h;
            if (new_y + new_h > (int32_t)cur_h) new_h = (int32_t)cur_h - new_y;
        }
        /* Borde superior */
        if (her_resize_edge & 4) {
            new_h = her_resize_start_h - dy;
            new_y = her_resize_start_y + dy;
            if (new_h < min_h) {
                new_y = her_resize_start_y + (her_resize_start_h - min_h);
                new_h = min_h;
            }
            if (new_y < 0) {
                new_h += new_y;
                new_y = 0;
            }
        }

        if (new_w < min_w) new_w = min_w;
        if (new_h < min_h) new_h = min_h;
        if (new_w > max_w) new_w = max_w;
        if (new_h > max_h) new_h = max_h;

        win->x = (uint32_t)new_x;
        win->y = (uint32_t)new_y;
        win->w = (uint32_t)new_w;
        win->h = (uint32_t)new_h;

        her_win_x = win->x; her_win_y = win->y;
        her_win_w = win->w; her_win_h = win->h;
    }
}

static char     cur_cmd[256];
static uint32_t cur_cmd_len = 0;
static uint32_t cur_cursor_pos = 0;
static volatile int her_anim_alive = 0;

static void render_desktop(void);
static void her_refresh_all(void);
static void her_vortex_close_step(void);
static void her_draw_vortex_particles(void);
static int her_cursor_should_show(void);
static void her_anim_start(void);
__attribute__((unused)) static void her_anim_halt(void);
static void her_anim_stop(void);

#define HER_SYNC_WIN() do {                                              \
        win_x = HER_CLI_X(her_win_x);                                    \
        win_y = HER_CLI_Y(her_win_y);                                    \
        win_w = HER_CLI_W(her_win_w);                                    \
        win_h = HER_CLI_H(her_win_h);                                    \
        g_live_win_x = win_x; g_live_win_y = win_y;                      \
        g_live_win_w = win_w; g_live_win_h = win_h;                      \
    } while (0)

static void log_add_raw(struct gui_chat_line *lines, int *count, const char *msg, uint32_t color) {
    ensure_agent_log();
    if (!lines) lines = agent_log;
    if (!lines) return;
    if (*count >= GUI_MAX_LOG_LINES) { for (int i=0; i<GUI_MAX_LOG_LINES-1; ++i) lines[i] = lines[i+1]; *count = GUI_MAX_LOG_LINES-1; }
    uint32_t p = 0; while (*msg && p < GUI_LINE_MAX_LEN-1) lines[*count].text[p++] = *msg++;
    lines[*count].text[p] = '\0'; lines[*count].color = color; (*count)++;
}

static void log_add(struct gui_chat_line *lines, int *count, const char *prefix, const char *msg, uint32_t color, uint32_t max_chars) {
    if (max_chars == 0 || max_chars > GUI_LINE_MAX_LEN - 4) max_chars = GUI_LINE_MAX_LEN - 4;
    char line_buf[GUI_LINE_MAX_LEN]; uint32_t lp = 0;
    if (prefix) while (*prefix && lp < max_chars-1) line_buf[lp++] = *prefix++;
    while (*msg) {
        if (*msg == '\n' || *msg == '\r') { line_buf[lp] = '\0'; log_add_raw(lines, count, line_buf, color); lp = 0; line_buf[lp++]=' '; line_buf[lp++]=' '; msg++; continue; }
        line_buf[lp++] = *msg++;
        if (lp >= max_chars - 2) { line_buf[lp] = '\0'; log_add_raw(lines, count, line_buf, color); lp = 0; line_buf[lp++]=' '; line_buf[lp++]=' '; }
    }
    if (lp > 2 || (prefix && lp > 0)) { line_buf[lp] = '\0'; log_add_raw(lines, count, line_buf, color); }
}

/* SOMA_PARCHE_024_TERM: el emulador de terminal vive en src/apps/term_ui.c (historial de 1000 lineas, barra de
 * desplazamiento, seleccion, portapapeles, menu y zoom). Aqui solo queda el historial de ordenes y el pegamento. */
static char term_hist[16][256];
static int  term_hist_count = 0, term_hist_idx = -1;

/* SOMA_203_INSTANCIAS: edicion e historial por terminal, nunca compartidos. */
struct her_terminal_session {
    int used;
    char cmd[256];
    uint32_t len, cursor;
    char history[16][256];
    int hist_count, hist_idx;
};
static struct her_terminal_session her_term_sessions[16];
static int her_term_live = -1;
static int her_tbg_slot;

static int her_term_alloc(void)
{
    for (int slot = 1; slot < 16; ++slot) {
        if (!her_term_sessions[slot].used) {
            memset(&her_term_sessions[slot], 0, sizeof her_term_sessions[slot]);
            her_term_sessions[slot].used = 1;
            her_term_sessions[slot].hist_idx = -1;
            return slot;
        }
    }
    return 0;
}
static void her_term_focus(int slot)
{
    if (slot == her_term_live) return;
    if (her_term_live > 0 && her_term_live < 16 && her_term_sessions[her_term_live].used) {
        struct her_terminal_session *old = &her_term_sessions[her_term_live];
        memcpy(old->cmd, cur_cmd, sizeof cur_cmd);
        old->len = cur_cmd_len; old->cursor = cur_cursor_pos;
        memcpy(old->history, term_hist, sizeof term_hist);
        old->hist_count = term_hist_count; old->hist_idx = term_hist_idx;
    }
    if (slot > 0 && slot < 16 && her_term_sessions[slot].used) {
        if (!term_ui_select(slot)) return;  /* OOM: nunca mostrar el estado de otra instancia. */
        struct her_terminal_session *next = &her_term_sessions[slot];
        memcpy(cur_cmd, next->cmd, sizeof cur_cmd);
        cur_cmd_len = next->len; cur_cursor_pos = next->cursor;
        memcpy(term_hist, next->history, sizeof term_hist);
        term_hist_count = next->hist_count; term_hist_idx = next->hist_idx;
        her_term_live = slot;
    } else {
        (void)term_ui_select(0);
        her_term_live = -1;
    }
}
static void her_win_focus(enum app_mode app, int slot)
{
    her_term_focus(app == APP_TERMINAL_SHELL ? slot : -1);
    if (app == APP_NOTEPAD && slot > 0) notepad_select(slot);
    if (app == APP_IMAGE_VIEWER && slot > 0) imgview_select(slot);
    if (app == APP_CODER_APP && slot > 0) coder_app_select(slot);
    if (app == APP_PAINT && slot > 0) paint_select(slot);
    if (app == APP_FILE_MANAGER && slot > 0) fm_select(slot);
    if (app == APP_PDF && slot > 0) pdfview_select(slot);
}
static void her_term_kill(int slot)
{
    if (slot <= 0 || slot >= 16 || !her_term_sessions[slot].used) return;
    if (her_tbg_slot == slot) her_tbg_slot = 0; /* No entregar el resultado a una sesion reutilizada. */
    if (her_term_live == slot) her_term_focus(-1);
    term_ui_drop(slot);
    memset(&her_term_sessions[slot], 0, sizeof her_term_sessions[slot]);
}


#define term_add_line(line, mv) term_print(line)

static void term_init_banner(uint32_t max_visible) {
    (void)max_visible;
    term_ui_init();
    term_add_line("\033[1;36m  ____  ____  __  __    _      \033[1;33mOS:     \033[1;37mSOMA v0.2 \033[0;36m(Multi-Agente)\033[0m", 0);
    term_add_line("\033[1;36m / ___|/ __ \\|  \\/  |  / \\     \033[1;33mKernel: \033[1;37mx86_64 Bare-Metal \033[1;32m(HAL v1)\033[0m", 0);
#ifdef __x86_64__
    if (apic_system_active() && apic_system_info()->timer_lapic)   /* SOMA_158_T480S: el reloj que se usa de verdad */
        term_add_line("\033[1;36m \\___ \\ |  | | |\\/| | / _ \\    \033[1;33mUptime: \033[1;37mLAPIC 1 kHz \033[0;32m(1 ms tick, APIC)\033[0m", 0);
    else
#endif
    term_add_line("\033[1;36m \\___ \\ |  | | |\\/| | / _ \\    \033[1;33mUptime: \033[1;37mPIT 1 kHz \033[0;32m(1 ms tick)\033[0m", 0);
    term_add_line("\033[1;36m  ___)| |__| | |  | |/ ___ \\   \033[1;33mCPU:    \033[1;37mRing 0 \033[1;35m(CR0:WP, NXE)\033[0m", 0);
    {   /* SOMA_157_T480S: la RAM real que gestiona el PMM (antes un texto fijo de 512 MiB) */
        size_t fr = 0, us = 0, tot = 0; pmm_get_stats(&fr, &us, &tot);
        uint64_t mib = (uint64_t)tot * 4096u >> 20;
        char ln[160], num[24]; uint32_t k = 0, nn = 0;
        const char *a = "\033[1;36m |____/\\____/|_|  |_/_/   \\_\\  \033[1;33mRAM:    \033[1;37m";
        while (*a && k < sizeof ln - 1) ln[k++] = *a++;
        if (mib >= 1024) { uint64_t g10 = mib * 10 / 1024; do { num[nn++] = (char)('0' + g10 % 10); g10 /= 10; } while (g10); }
        else { uint64_t m = mib; do { num[nn++] = (char)('0' + m % 10); m /= 10; } while (m); }
        while (nn) { if (mib >= 1024 && nn == 1 && k < sizeof ln - 1) ln[k++] = ','; if (k < sizeof ln - 1) ln[k++] = num[--nn]; }
        const char *b = mib >= 1024 ? " GiB \033[1;32m(PMM Allocator)\033[0m" : " MiB \033[1;32m(PMM Allocator)\033[0m";
        while (*b && k < sizeof ln - 1) ln[k++] = *b++;
        ln[k] = 0;
        term_add_line(ln, 0);
    }
    {   /* SOMA_162_T480S: disco y red reales (antes, textos fijos de QEMU) */
        char ln[200]; uint32_t k = 0; const char *parts[2][2] = { { "                               \033[1;33mDisco:  \033[1;37m", sysdisk_describe() },
                                                                   { "                               \033[1;33mRed:    \033[1;37m", net_describe() } };
        for (int r = 0; r < 2; r++) {
            k = 0;
            for (int q = 0; q < 2; q++) for (const char *a = parts[r][q]; *a && k < sizeof ln - 8; a++) ln[k++] = *a;
            const char *z = "\033[0m"; while (*z) ln[k++] = *z++;
            ln[k] = 0; term_add_line(ln, 0);
        }
    }
    term_add_line("                               \033[1;33mAgente: \033[1;37mnail-35b (ReAct LAN)\033[0m", 0);
    term_add_line("", 0);
    term_add_line("\033[1;36m----------------------------------------------------------------------\033[0m", 0);
    term_add_line(" \033[1;37mSOMA 0.3 pre-beta :: Escribe '\033[1;32msoma <mision>\033[1;37m' para el Agente o '\033[1;33mhelp\033[1;37m'.\033[0m", 0);
    term_add_line("\033[1;36m----------------------------------------------------------------------\033[0m", 0);
}

static void render_real_terminal(uint32_t lx, uint32_t ly, uint32_t lw, uint32_t lh, const char *cmd, uint32_t cmd_len, uint32_t cursor_pos, int show_cursor) {
    term_render(lx, ly, lw, lh, cmd, cmd_len, cursor_pos, show_cursor);
}

static void render_log_window_chat(struct gui_chat_line *lines, int count, uint32_t lx, uint32_t ly, uint32_t lw, uint32_t lh) {
    fb_fill_rect(lx, ly, lw, lh, 0x0004070E); fb_draw_rect(lx, ly, lw, lh, COLOR_CYAN);
    fb_fill_rect(lx, ly, lw, 36, 0x001E293B); fb_draw_rect(lx, ly, lw, 36, 0x00334155);
    font_draw_string(lx + 16, ly + 14, "SOMA v0.2 :: Agente IA (ReAct & LLM)", 0x00F8FAFC, 1);
    uint32_t max_visible = (lh > 50) ? (lh - 50) / 16 : 10;
    int start_idx = 0; if (count > (int)max_visible) start_idx = count - (int)max_visible;
    uint32_t line_y = ly + 48;
    for (int i = start_idx; i < count; ++i) { font_draw_string(lx + 16, line_y, lines[i].text, lines[i].color, 1); line_y += 16; }
}

static void render_input_bar_chat(const char *prompt, uint32_t prompt_color, uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh, const char *text, uint32_t len, uint32_t cursor_pos, int show_cursor) {
    fb_fill_rect(bx, by, bw, bh, 0x00020617); fb_draw_rect(bx, by, bw, bh, prompt_color);
    font_draw_string(bx + 14, by + 14, prompt, prompt_color, 2);
    uint32_t p_len = 0; while (prompt[p_len]) p_len++;
    uint32_t text_x = bx + 14 + (p_len * 8 * 2);
    fb_set_clip((int32_t)text_x, (int32_t)(by + 2), (int32_t)(bw - 28 - (p_len * 16)), (int32_t)(bh - 4));
    int offset = 0;
    if (cursor_pos * 16 > bw - 60 - (p_len * 16)) offset = cursor_pos * 16 - (bw - 60 - (p_len * 16));
    font_draw_string(text_x - offset, by + 14, text, 0x00F8FAFC, 2);
    if (show_cursor) {
        uint32_t cx = text_x - offset + (cursor_pos * 16);
        fb_fill_rect(cx, by + 12, 16, 20, prompt_color);
        if (cursor_pos < len && text[cursor_pos] >= 32 && text[cursor_pos] <= 126) {
            char ch[2] = { text[cursor_pos], '\0' };
            font_draw_string(cx, by + 14, ch, 0x00000000, 2);
        }
    }
    fb_reset_clip();
}

static void render_agent_global_rail(uint32_t cur_w, uint32_t cur_h)
{
    (void)cur_w; (void)cur_h;
    her_refresh_all();
}

