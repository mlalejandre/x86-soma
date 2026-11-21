/* gui_minimizar.c: minimizar, restaurar y cerrar con el vortice; estelas.
 * SOMA_234_GUI_PARTIDO: NO se compila por separado. Lo incluye src/ui/gui.c en su sitio (una sola unidad de
 * compilacion: comparte el estado del escritorio con gui.c, y el nucleo compilado sale identico). */
/* SOMA_203_INSTANCIAS: X = minimizar (sin perder el estado de usuario). */
static void her_minimize_window(int idx)
{
    if (idx < 0 || idx >= her_num_windows) return;
    if (her_min_count >= HER_MINIMIZED_MAX) {
        her_hint_msg = "Orbita llena: arrastra un icono al centro para cerrarlo";
        her_hint_until = timer_get_uptime_ms() + 3000u;
        return;
    }
    struct her_win_node v = her_windows[idx];
    if (v.app == APP_TERMINAL_SHELL) her_term_focus(-1);
    if (v.app == APP_YOUTUBE) youtube_close();                /* SOMA_250_YOUTUBE: sin ventana el video se para (se reanuda al volver) */
    {   /* fase: el hueco mas amplio entre los iconos ya en orbita */
        uint32_t best = 0, bestd = 0;
        for (uint32_t c = 0; c < 1024u; c += 32u) {
            uint32_t md = 1024u;
            for (int q = 0; q < her_min_count; ++q) {
                uint32_t d = (c - her_min_phase[q]) & 1023u; if (d > 512u) d = 1024u - d;
                if (d < md) md = d;
            }
            if (md > bestd) { bestd = md; best = c; }
        }
        her_min_phase[her_min_count] = (uint16_t)best;
    }
    her_minimized[her_min_count++] = v;
    for (int j = idx; j < her_num_windows - 1; ++j) her_windows[j] = her_windows[j + 1];
    --her_num_windows;
    if (her_num_windows > 0) {
        struct her_win_node *top = &her_windows[her_num_windows - 1];
        her_active_win_idx = her_num_windows - 1;
        current_app = top->app;
        her_win_x = top->x; her_win_y = top->y; her_win_w = top->w; her_win_h = top->h;
        agent_global_focus = (current_app == APP_AGENT_CHAT) ? 1 : 0;
        her_win_focus(current_app, top->term_slot);
    } else {
        her_active_win_idx = -1; current_app = APP_DESKTOP; agent_global_focus = 1;
        her_term_focus(-1);
    }
}
static void her_restore_minimized(int idx)
{
    if (idx < 0 || idx >= her_min_count || her_num_windows >= HER_MAX_WINDOWS) return;
    struct her_win_node v = her_minimized[idx];
    for (int j = idx; j < her_min_count - 1; ++j) { her_minimized[j] = her_minimized[j + 1]; her_min_phase[j] = her_min_phase[j + 1]; }
    --her_min_count;
    her_term_focus(-1);
    her_windows[her_num_windows++] = v;
    her_active_win_idx = her_num_windows - 1;
    current_app = v.app;
    her_win_x = v.x; her_win_y = v.y; her_win_w = v.w; her_win_h = v.h;
    agent_global_focus = (v.app == APP_AGENT_CHAT) ? 1 : 0;
    her_win_focus(v.app, v.term_slot);
}
static void her_kill_minimized(int idx)
{
    if (idx < 0 || idx >= her_min_count) return;
    struct her_win_node dead = her_minimized[idx];
    if (dead.app == APP_TERMINAL_SHELL) her_term_kill(dead.term_slot);
    her_inst_drop(dead.app, dead.term_slot);                  /* SOMA_211_NOTAS / SOMA_212 */
    if (dead.app == APP_CAMERA) camapp_close();
    if (dead.app == APP_YOUTUBE) youtube_close();             /* SOMA_250_YOUTUBE */
    if (dead.app == APP_SOMA_APP) gui_soma_win_on_close();
    for (int j = idx; j < her_min_count - 1; ++j) { her_minimized[j] = her_minimized[j + 1]; her_min_phase[j] = her_min_phase[j + 1]; }
    --her_min_count;
}
static int her_find_minimized(enum app_mode app)
{
    for (int i = 0; i < her_min_count; ++i) if (her_minimized[i].app == app) return i;
    return -1;
}

/* ====================================================================
 * SOMA ENHANCED VORTEX & ACCRETION DISK ENGINE (Fixed-Point 16.8)
 * ==================================================================== */
#define HER_VORTEX_MAX 512

struct her_vortex_p {
    int32_t  x, y;           /* Posición fija 16.8 */
    int32_t  vx, vy;         /* Velocidad fija 16.8 */
    int32_t  prev_x, prev_y; /* Estela luminosa */
    uint32_t color;
    uint32_t edge_color;
    uint8_t  absorbed;
    uint8_t  size;
    int16_t  w_size, h_size;
};

static struct her_vortex_p her_vortex_parts[HER_VORTEX_MAX];

/* static int her_vortex_closing = 0; (declarada arriba) */
static int her_vortex_frame = 0;
static int her_vortex_total_absorbed = 0;
/* static int her_dock_pending_open = 0; (declarada arriba) */
/* static int soma_text_vortex = 0; (declarada arriba) */

static void her_close_target_window_vortex(int win_idx)
{
    if (win_idx < 0 || win_idx >= her_num_windows) return;
        struct her_win_node closing_win = her_windows[win_idx];
    if (closing_win.app == APP_TERMINAL_SHELL) her_term_kill(closing_win.term_slot);
    her_inst_drop(closing_win.app, closing_win.term_slot);   /* SOMA_211_NOTAS / SOMA_212 */
    enum app_mode closing_app = closing_win.app;
    if (closing_app == APP_SOMA_APP) {
        gui_soma_win_on_close(); /*PL12_NOTIFY_CLOSE*/
    }
    if (closing_app == APP_CAMERA) camapp_close();     /* SOMA_178_T480S: apaga la camara */
    if (closing_app == APP_YOUTUBE) youtube_close();   /* SOMA_250_YOUTUBE: corta el video y la descarga al momento */

    for (int i = win_idx; i < her_num_windows - 1; ++i) {
        her_windows[i] = her_windows[i + 1];
    }
    her_num_windows--;

    if (her_num_windows > 0) {
        her_active_win_idx = her_num_windows - 1;
        current_app = her_windows[her_active_win_idx].app;
        her_win_x = her_windows[her_active_win_idx].x;
        her_win_y = her_windows[her_active_win_idx].y;
        her_win_w = her_windows[her_active_win_idx].w;
        her_win_h = her_windows[her_active_win_idx].h;
        agent_global_focus = (current_app == APP_AGENT_CHAT) ? 1 : 0;
        her_win_focus(current_app, her_windows[her_active_win_idx].term_slot);
    } else {
        her_term_focus(-1);
        her_active_win_idx = -1;
        current_app = APP_DESKTOP;
        agent_global_focus = 1;
    }

    soma_text_vortex = 0;
    her_dock_pending_open = 0;

    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    if (cur_w == 0 || cur_h == 0) {
        if (!her_cursor_should_show()) mouse_erase_cursor();
        return;
    }

    uint32_t app_col = 0x0038BDF8u;
    switch (closing_app) {
    case APP_TERMINAL_SHELL: app_col = 0x0010B981u; break;
    case APP_AGENT_CHAT:     app_col = 0x0038BDF8u; break;
    case APP_WEB_BROWSER:    app_col = 0x00F59E0Bu; break;
    case APP_FILE_MANAGER:   app_col = 0x0038BDF8u; break;
    case APP_IMAGE_VIEWER:   app_col = 0x0010B981u; break;
    case APP_NOTEPAD:        app_col = 0x00F472B6u; break;
    case APP_AGENT_MEMORY:   app_col = 0x00A855F7u; break;
    case APP_LAYOUT:         app_col = 0x000EA5E9u; break;
        case APP_AGENTS:         app_col = 0x0034D399u; break;
    case APP_PAINT:          app_col = 0x00F59E0Bu; break;
    case APP_CODER_APP:      app_col = 0x0000FF88u; break;
    case APP_MEDIA:          app_col = 0x00FB7185u; break;
    case APP_RECORDER:       app_col = 0x00F87171u; break;
    case APP_CAMERA:         app_col = 0x0038BDF8u; break;                /* SOMA_178_T480S */
    case APP_CAPTURE:        app_col = 0x00A78BFAu; break;                /* SOMA_179_T480S */
    case APP_YOUTUBE:        app_col = 0x00FF4444u; break;                /* SOMA_250_YOUTUBE */
    case APP_SOMA_APP:       app_col = 0x0038BDF8u; break; /*PL12_VORTEX_COL*/
    default:                 app_col = 0x007DD3FCu; break;
    }

    int32_t wx = (int32_t)closing_win.x;
    int32_t wy = (int32_t)closing_win.y;
    int32_t ww = (int32_t)closing_win.w;
    int32_t wh = (int32_t)closing_win.h;
    if (ww < 100) ww = 400;
    if (wh < 100) wh = 300;
    int32_t wcx = wx + ww / 2;
    int32_t wcy = wy + wh / 2;

    int32_t tile_w = ww / 16;
    int32_t tile_h = wh / 16;
    if (tile_w < 4) tile_w = 4;
    if (tile_h < 4) tile_h = 4;

    volatile uint32_t *target_bb = fb_get_buffer();
    int p_idx = 0;
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            if (p_idx >= HER_VORTEX_MAX) break;
            struct her_vortex_p *p = &her_vortex_parts[p_idx++];
            int32_t orig_x = wx + c * tile_w;
            int32_t orig_y = wy + r * tile_h;
            if (orig_x < 0) orig_x = 0;
            if (orig_x >= (int32_t)cur_w) orig_x = (int32_t)cur_w - 1;
            if (orig_y < 0) orig_y = 0;
            if (orig_y >= (int32_t)cur_h) orig_y = (int32_t)cur_h - 1;

            p->x = orig_x << 8;
            p->y = orig_y << 8;
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->absorbed = 0;
            p->w_size = (int16_t)tile_w;
            p->h_size = (int16_t)tile_h;
            p->size = ((p_idx & 7) == 0) ? 2 : 1;

            uint32_t samp_col = 0;
            if (target_bb) {
                int32_t mid_x = orig_x + tile_w / 2;
                int32_t mid_y = orig_y + tile_h / 2;
                if (mid_x >= (int32_t)cur_w) mid_x = (int32_t)cur_w - 1;
                if (mid_y >= (int32_t)cur_h) mid_y = (int32_t)cur_h - 1;
                samp_col = target_bb[mid_y * cur_w + mid_x];
            }
            int is_border = (r == 0 || r == 15 || c == 0 || c == 15);
            if ((samp_col & 0x00FFFFFFu) < 0x00151515u || is_border)
                p->color = her_mix256(app_col, 0x00FFFFFFu, is_border ? 60u : 0u);
            else
                p->color = her_mix256(samp_col, app_col, 80u);
            p->edge_color = her_mix256(app_col, 0x00FFFFFFu, 180u);

            int32_t ox = (orig_x + tile_w / 2) - wcx;
            int32_t oy = (orig_y + tile_h / 2) - wcy;
            int32_t rnd_x = (int32_t)(her_rand() & 15u) - 7;
            int32_t rnd_y = (int32_t)((her_rand() >> 4) & 15u) - 7;
            p->vx = (ox * 70) / (ww > 0 ? ww : 1) + rnd_x * 16;
            p->vy = (oy * 70) / (wh > 0 ? wh : 1) + rnd_y * 16;
        }
    }

    her_dragging = 0;
    her_resizing = 0;
    her_vortex_total_absorbed = 0;
    her_vortex_frame = 0;
    her_vortex_closing = 1;
    soma_vortex_boost = 10;
    if (!her_cursor_should_show()) mouse_erase_cursor();
}

/* SOMA_208: icono minimizado soltado en el agujero negro: se deshace en particulas que caen al horizonte y despues se cierra. */
/* SOMA_219_AVISO: una ventana con cambios sin guardar (Notas, Paint, Studio) no se cierra al primer intento: se avisa y vuelve a
 * orbitar. Si se suelta otra vez en el agujero negro en los 8 s siguientes, se cierra sin guardar. */
static int her_inst_dirty(enum app_mode app, int slot)
{
    switch (app) { case APP_NOTEPAD: return notepad_inst_dirty(slot); case APP_PAINT: return paint_inst_dirty(slot); case APP_CODER_APP: return ce_inst_dirty(slot); default: return 0; }
}
static int her_kill_armed_app = -1, her_kill_armed_slot;
static uint64_t her_kill_armed_until;
static char her_kill_hint[112];
static int her_kill_confirmed(int idx)
{
    enum app_mode app = her_minimized[idx].app; int slot = her_minimized[idx].term_slot;
    if (!her_inst_dirty(app, slot)) return 1;
    uint64_t now = timer_get_uptime_ms();
    if (her_kill_armed_app == (int)app && her_kill_armed_slot == slot && now < her_kill_armed_until) { her_kill_armed_app = -1; return 1; }
    her_kill_armed_app = (int)app; her_kill_armed_slot = slot; her_kill_armed_until = now + 8000u;
    const char *nm = her_inst_name(app, slot);
    int n = 0;
    for (const char *t = (nm && *nm) ? nm : her_app_base_title(app); *t && n < 40; ++t) her_kill_hint[n++] = *t;
    for (const char *t = ": cambios sin guardar. Sueltalo otra vez en el agujero negro para cerrar sin guardar"; *t && n < (int)sizeof her_kill_hint - 1; ++t) her_kill_hint[n++] = *t;
    her_kill_hint[n] = 0;
    her_hint_msg = her_kill_hint; her_hint_until = now + 8000u;
    return 0;
}
static void her_kill_minimized_vortex(int idx, int32_t px, int32_t py)
{
    if (idx < 0 || idx >= her_min_count) return;
    if (!her_kill_confirmed(idx)) return;                     /* SOMA_219_AVISO */
    uint32_t col = 0x0038BDF8u;
    int id = her_min_dock_index(her_minimized[idx].app);
    if (id >= 0) col = her_dock_items[id].color;
    uint32_t cw = fb_get_width(), ch = fb_get_height();
    int n = 0;
    if (cw && ch) for (int k = 0; k < 96 && n < HER_VORTEX_MAX; ++k) {
        struct her_vortex_p *p = &her_vortex_parts[n++];
        int32_t x = px + (int32_t)(her_rand() % 40u) - 20, y = py + (int32_t)(her_rand() % 40u) - 20;
        if (x < 0) x = 0;
        if (x >= (int32_t)cw) x = (int32_t)cw - 1;
        if (y < 0) y = 0;
        if (y >= (int32_t)ch) y = (int32_t)ch - 1;
        p->x = x << 8; p->y = y << 8; p->prev_x = p->x; p->prev_y = p->y;
        p->absorbed = 0; p->w_size = 4; p->h_size = 4; p->size = ((k & 3) == 0) ? 2 : 1;
        p->color = (k & 1) ? col : 0x00FFFFFFu;
        p->edge_color = her_mix256(col, 0x00FFFFFFu, 180u);
        p->vx = ((int32_t)(her_rand() & 15u) - 7) * 12; p->vy = ((int32_t)((her_rand() >> 4) & 15u) - 7) * 12;
    }
    for (int q = n; q < HER_VORTEX_MAX; ++q) her_vortex_parts[q].absorbed = 1;
    her_kill_minimized(idx);
    if (n > 0) {
        her_vortex_frame = 0; her_vortex_total_absorbed = 0; her_vortex_closing = 1; soma_vortex_boost = 10;
        ofx_flash += 60u; if (ofx_flash > 255u) ofx_flash = 255u;
    }
}

/* Enrutador canónico para cerrar la ventana activa desde cualquier app */
static void her_close_current_app_vortex(void)
{
    if (her_num_windows <= 0) return;
    her_minimize_window_fx(her_num_windows - 1);
}

/* Integración física en el disco de acreción hasta el horizonte de sucesos */
static void her_vortex_close_step(void)
{
    if (!her_vortex_closing) return;
    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    uint32_t cx = cur_w / 2u, cy = cur_h / 2u;
    if (cur_w == 0u || cur_h == 0u) {
        her_vortex_closing = 0;
        return;
    }

    her_vortex_frame++;
    int active_in_flight = 0;
    int frame = her_vortex_frame;
    uint32_t horizon_r = 27u; /* Radio del horizonte de sucesos */

    for (int i = 0; i < HER_VORTEX_MAX; ++i) {
        struct her_vortex_p *p = &her_vortex_parts[i];
        if (p->absorbed) continue;
        int32_t cur_px = p->x >> 8;
        int32_t cur_py = p->y >> 8;
        int32_t dx = (int32_t)cx - cur_px;
        int32_t dy = (int32_t)cy - cur_py;
        uint32_t dist = her_isqrt((uint32_t)(dx * dx + dy * dy));

        /* Absorción al tocar o penetrar el horizonte de sucesos */
        if (dist <= horizon_r || dist == 0u) {
            p->x = (int32_t)(cx << 8);
            p->y = (int32_t)(cy << 8);
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->absorbed = 1;
            her_vortex_total_absorbed++;
            ofx_on_absorb(p->color); /* SOMA_ORB_FX_PATCH */
            /* La particula desaparece al tocar el horizonte: sin energia,
             * sin pulso, sin cambio de color y sin reinyeccion en la rosquilla. */
            continue;
        }
        active_in_flight++;
        p->prev_x = p->x;
        p->prev_y = p->y;

        int max_burst = soma_text_vortex ? 4 : 2;
        if (frame < max_burst) {
            /* Desprendimiento de aplicaciones reducido a solo 2 frames */
            p->x += p->vx;
            p->y += p->vy;
            p->vx = (p->vx * 192) >> 8;
            p->vy = (p->vy * 192) >> 8;
            int32_t shrink_w = (int32_t)p->w_size * (max_burst - frame) / max_burst;
            int32_t shrink_h = (int32_t)p->h_size * (max_burst - frame) / max_burst;
            if (shrink_w < 1) shrink_w = 1;
            if (shrink_h < 1) shrink_h = 1;
            p->w_size = (int16_t)shrink_w;
            p->h_size = (int16_t)shrink_h;
        } else {
            /* Fase 2: Integración al doble de velocidad en disco y horizonte */
            /* SOMA FAST ABSORB v1 */
            int32_t pull_spd, swirl_spd;
            if (soma_text_vortex) {
                /* Arrastre de letras e iconos */
                if (dist > 180u)      { pull_spd = 36; swirl_spd = 12; }
                else if (dist > 90u)  { pull_spd = 28; swirl_spd = 18; }
                else if (dist > 45u)  { pull_spd = 18; swirl_spd = 24; }
                else                  { pull_spd = 12; swirl_spd = 28; }
            } else {
                /* Aplicaciones: el doble de rapido (absorcion a la mitad de tiempo) */
                if (dist > 300u)      { pull_spd = 96; swirl_spd = 20; }
                else if (dist > 160u) { pull_spd = 72; swirl_spd = 24; }
                else if (dist > 85u)  { pull_spd = 56; swirl_spd = 26; }
                else if (dist > 45u)  { pull_spd = 32; swirl_spd = 18; }
                else                  { pull_spd = 22; swirl_spd = 10; }
            }

            /* El giro tangencial aleja la particula del centro (~s^2 / 2d). Se
             * compensa en el empuje radial para que SIEMPRE se acerque al horizonte
             * y no se quede orbitando alrededor del agujero negro. */
            int32_t d32 = (int32_t)dist;
            if (swirl_spd > d32 / 3) swirl_spd = d32 / 3;
            pull_spd += (swirl_spd * swirl_spd) / (2 * d32);
            if (pull_spd > d32 - 20) pull_spd = d32 - 20;
            if (pull_spd < 4) pull_spd = 4;

            int32_t step_x = (int32_t)(((int64_t)dx * pull_spd +
                                         (int64_t)dy * swirl_spd) * 256 / (int32_t)dist);
            int32_t step_y = (int32_t)(((int64_t)dy * pull_spd -
                                         (int64_t)dx * swirl_spd) * 256 / (int32_t)dist);
            p->vx = step_x;
            p->vy = step_y;
            p->x += p->vx;
            p->y += p->vy;
        }
    }

    soma_vortex_boost = (active_in_flight > 0) ? (soma_text_vortex ? 12 : 8) : 0;
    if (active_in_flight == 0) {
        her_vortex_closing = 0;
        soma_vortex_boost = 0;
        her_energy = 0u;
        her_pulse = 0u;
        /* Al ser absorbida la última partícula de texto, surgen los iconos del dock */
        if (her_dock_pending_open) {
            her_dock_pending_open = 0;
            soma_text_vortex = 0;
            her_icons_visible = 1;
            her_icons_anim_phase = 0;
            her_icons_burst_opening = 1;
            her_icons_burst_at = timer_get_uptime_ms();
        }
    }
}

/* ================= SOMA_ORB_FX_PATCH (B): estelas cometarias ================= */
static void ofx_vortex_trails(void)
{
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb || w <= 0 || h <= 0) return;
    int32_t cx = w / 2, cy = h / 2;

    for (int i = 0; i < HER_VORTEX_MAX; ++i) {
        struct her_vortex_p *p = &her_vortex_parts[i];
        if (p->absorbed) continue;
        int32_t x = p->x >> 8, y = p->y >> 8;
        if (x < -64 || y < -64 || x > w + 64 || y > h + 64) continue;

        int32_t dx = cx - x, dy = cy - y;
        uint32_t dist = her_isqrt((uint32_t)(dx * dx + dy * dy));
        /* 0 lejos ... 256 pegada al horizonte: se vuelve blanca y se estira */
        uint32_t hot = (dist >= 220u) ? 0u : (220u - dist) * 256u / 220u;
        uint32_t base = p->color & 0x00FFFFFFu;
        uint32_t c_head = ofx_mul(her_mix256(base, 0x00FFFFFFu, hot), 215u);
        uint32_t c_tail = ofx_mul(base, 34u);

        int32_t stretch = 200 + (int32_t)hot;                  /* x0.8 .. x1.8 */
        int32_t tx = (p->x - ((p->vx * stretch) >> 8)) >> 8;
        int32_t ty = (p->y - ((p->vy * stretch) >> 8)) >> 8;
        ofx_line(bb, w, h, tx, ty, x, y, c_tail, c_head);

        uint32_t g = ofx_mul(c_head, 80u);
        ofx_px(bb, w, h, x + 1, y, g); ofx_px(bb, w, h, x - 1, y, g);
        ofx_px(bb, w, h, x, y + 1, g); ofx_px(bb, w, h, x, y - 1, g);
    }
}
/* ================= fin SOMA_ORB_FX_PATCH (B) ================= */

static void her_draw_vortex_particles(void)
{
    if (!her_vortex_closing) return;
    ofx_vortex_trails();
    uint32_t w = fb_get_width(), h = fb_get_height();
    for (int i = 0; i < HER_VORTEX_MAX; ++i) {
        struct her_vortex_p *p = &her_vortex_parts[i];
        if (p->absorbed) continue;
        int32_t x = p->x >> 8, y = p->y >> 8;
        int32_t px = p->prev_x >> 8, py = p->prev_y >> 8;
        if (x < 0 || y < 0 || x >= (int32_t)w || y >= (int32_t)h) continue;
        if (px >= 0 && py >= 0 && px < (int32_t)w && py < (int32_t)h &&
            (px != x || py != y))
            fb_draw_line(px, py, x, y, her_scale(p->color, 150u));
        if (her_vortex_frame < (soma_text_vortex ? 4 : 2) && p->w_size > 2 && p->h_size > 2) {
            int32_t dw = p->w_size, dh = p->h_size;
            if (x + dw > (int32_t)w) dw = (int32_t)w - x;
            if (y + dh > (int32_t)h) dh = (int32_t)h - y;
            if (dw > 2 && dh > 2) {
                fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)dw,
                             (uint32_t)dh, p->color);
                fb_draw_rect((uint32_t)x, (uint32_t)y, (uint32_t)dw,
                             (uint32_t)dh, p->edge_color);
            } else {
                fb_fill_circle(x, y, 2, p->color);
            }
            continue;
        }
        if (p->size > 1) {
            fb_fill_circle(x, y, (int32_t)p->size, p->color);
            fb_draw_pixel((uint32_t)x, (uint32_t)y, 0x00FFFFFFu);
        } else {
            fb_draw_pixel((uint32_t)x, (uint32_t)y, p->color);
        }
    }
}

/* Destrucción integral en partículas de los diálogos de usuario y LLM */
static void her_start_text_destruction(uint32_t cur_w, uint32_t cur_h)
{
    her_text_suppressed_by_dock = 1;
    if (cur_w == 0u || cur_h == 0u) {
        her_last_q[0] = '\0';
        her_ans_len = her_ans_reveal = 0;
        her_thk_len = her_thk_reveal = 0;
        agent_global_len = agent_global_cursor = 0;
        agent_global_cmd[0] = '\0';
        her_energy = 0;
        her_dock_pending_open = 1;
        return;
    }

    volatile uint32_t *fb = fb_get_buffer();
    if (!fb) {
        her_last_q[0] = '\0';
        her_ans_len = her_ans_reveal = 0;
        her_thk_len = her_thk_reveal = 0;
        agent_global_len = agent_global_cursor = 0;
        agent_global_cmd[0] = '\0';
        her_energy = 0;
        her_dock_pending_open = 1;
        return;
    }

    uint32_t cx = cur_w / 2u, cy = cur_h / 2u;
    uint32_t top0 = (cy > 170u) ? cy - 170u : 12u;
    uint32_t top1 = (cy > 170u) ? cy - 105u : 64u;
    uint32_t bottom0 = cy + 105u;
    uint32_t bottom1 = (cur_h > 35u) ? cur_h - 35u : cur_h;
    if (top0 > cur_h) top0 = cur_h;
    if (top1 > cur_h) top1 = cur_h;
    if (bottom0 > cur_h) bottom0 = cur_h;
    if (bottom1 > cur_h) bottom1 = cur_h;

    int p_idx = 0;

    /* Captura completa de todos los píxeles iluminados de las letras en pantalla */
    for (uint32_t y = 0; y < cur_h && p_idx < HER_VORTEX_MAX; ++y) {
        if (!((y >= top0 && y < top1) || (y >= bottom0 && y < bottom1))) continue;
        uint32_t row = y * cur_w;
        for (uint32_t x = 30; x < cur_w - 30 && p_idx < HER_VORTEX_MAX; x += 2) {
            uint32_t sample = fb[row + x] & 0x00FFFFFFu;
            uint32_t r = (sample >> 16) & 0xFFu;
            uint32_t g = (sample >> 8)  & 0xFFu;
            uint32_t b = sample & 0xFFu;
            if (r + g + b < 40u) continue; /* Fondo cósmico oscuro */

            struct her_vortex_p *p = &her_vortex_parts[p_idx++];
            p->x = (int32_t)(x << 8);
            p->y = (int32_t)(y << 8);
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->absorbed = 0;
            p->w_size = 3;
            p->h_size = 3;
            p->size = ((p_idx & 3) == 0) ? 2 : 1;
            p->color = sample;
            p->edge_color = her_mix256(sample, 0x00FFFFFFu, 180u);

            /* Impulso de fragmentación */
            int32_t ox = (int32_t)x - (int32_t)cx;
            int32_t oy = (int32_t)y - (int32_t)cy;
            int32_t rnd_x = (int32_t)(her_rand() & 31u) - 15;
            int32_t rnd_y = (int32_t)((her_rand() >> 5) & 31u) - 15;
            p->vx = (ox * 60) / (int32_t)cur_w + rnd_x * 12;
            p->vy = (oy * 60) / (int32_t)cur_h + rnd_y * 12;
        }
    }

    /* Limpieza total de los textos para dar paso exclusivamente a las partículas */
    her_last_q[0] = '\0';
    her_ans_len = 0;
    her_ans_reveal = 0;
    her_thk_len = 0;
    her_thk_reveal = 0;
    agent_global_len = 0;
    agent_global_cursor = 0;
    agent_global_cmd[0] = '\0';
    her_energy = 0;

    her_vortex_frame = 0;
    her_vortex_total_absorbed = 0;

    if (p_idx > 0) {
        her_vortex_closing = 1;
        soma_vortex_boost = 14;
        soma_text_vortex = 1;
        her_dock_pending_open = 1;
    } else {
        her_vortex_closing = 0;
        soma_text_vortex = 0;
        her_dock_pending_open = 0;
        her_icons_visible = 1;
        her_icons_burst_opening = 1;
        her_icons_burst_at = timer_get_uptime_ms();
    }
}


/* Absorción de los iconos orbitales por el disco de acreción del agujero negro */
static void her_close_icons_vortex(uint32_t cur_w, uint32_t cur_h)
{
    her_power_pending = 0;
    if (cur_w == 0u || cur_h == 0u) {
        her_icons_visible = 0;
        her_icons_anim_phase = 0;
        her_dock_pending_open = 0;
        her_text_suppressed_by_dock = 0;
        agent_global_focus = 1;
        return;
    }

    uint64_t now_ms = timer_get_uptime_ms();
    int p_idx = 0;

    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
        int32_t target_x, target_y;
        her_icon_orbit_target(i, cur_w, cur_h, now_ms, &target_x, &target_y);
        uint32_t col = her_dock_items[i].color;

        /* Generar 20 partículas por cada icono orbital */
        for (int k = 0; k < 20 && p_idx < HER_VORTEX_MAX; ++k) {
            struct her_vortex_p *p = &her_vortex_parts[p_idx++];
            int32_t ox = (int32_t)(her_rand() % 56u) - 28;
            int32_t oy = (int32_t)(her_rand() % 56u) - 28;
            int32_t px = target_x + ox;
            int32_t py = target_y + oy;
            if (px < 0) px = 0;
            if (px >= (int32_t)cur_w) px = (int32_t)cur_w - 1;
            if (py < 0) py = 0;
            if (py >= (int32_t)cur_h) py = (int32_t)cur_h - 1;

            p->x = px << 8;
            p->y = py << 8;
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->absorbed = 0;
            p->w_size = 4;
            p->h_size = 4;
            p->size = ((k & 3) == 0) ? 2 : 1;
            p->color = (k & 1) ? col : 0x00FFFFFFu;
            p->edge_color = her_mix256(col, 0x00FFFFFFu, 180u);

            int32_t rnd_x = (int32_t)(her_rand() & 15u) - 7;
            int32_t rnd_y = (int32_t)((her_rand() >> 4) & 15u) - 7;
            p->vx = rnd_x * 16;
            p->vy = rnd_y * 16;
        }
    }

    /* Ocultar iconos de inmediato y habilitar entrada al agente */
    her_icons_visible = 0;
    her_icons_anim_phase = 0;
    her_icons_burst_at = 0;
    her_dock_pending_open = 0;
    her_text_suppressed_by_dock = 0;
    agent_global_focus = 1;
    agent_global_len = 0;
    agent_global_cursor = 0;
    agent_global_cmd[0] = '\0';

    her_vortex_frame = 0;
    her_vortex_total_absorbed = 0;
    her_vortex_closing = (p_idx > 0) ? 1 : 0;
    soma_text_vortex = 1; /* Arrastre dinámico hacia el disco de acreción */
    soma_vortex_boost = 14;
    mouse_erase_cursor();
}

