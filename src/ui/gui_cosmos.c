/* gui_cosmos.c: rosquilla de acrecion, estrellas y orbe central.
 * SOMA_234_GUI_PARTIDO: NO se compila por separado. Lo incluye src/ui/gui.c en su sitio (una sola unidad de
 * compilacion: comparte el estado del escritorio con gui.c, y el nucleo compilado sale identico). */
/* ====================================================================
 * ROSQUILLA DE ACRECIÓN Y ESTRELLAS CÓSMICAS (HER-COSMIC TORUS)
 * ==================================================================== */
static void her_cosmic_init_all(uint32_t cx, uint32_t cy)
{
    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    uint32_t max_reach = (cur_w > cur_h ? cur_w : cur_h);

    for (int i = 0; i < HER_TORUS_PART; ++i) {
        her_torus[i].angle = (uint16_t)(((uint32_t)i * 1024u) / HER_TORUS_PART);
        uint32_t r_base = 36u + ((uint32_t)i * 44u) / HER_TORUS_PART + (her_rand() & 15u);
        her_torus[i].r16 = (int32_t)r_base << 8;
        her_torus[i].vr16 = 20 + (int32_t)(her_rand() & 23u);
        her_torus[i].omega = 10 + (int32_t)(her_rand() & 7u);
        her_torus[i].color = her_grad(her_torus[i].angle);
        her_torus[i].size = ((i & 5) == 0) ? 2 : 1;
        her_torus[i].prev_x = -1;
        her_torus[i].prev_y = -1;
    }

    for (int s = 0; s < HER_STARS_PART; ++s) {
        uint32_t ang = (her_rand() >> 8) & 1023u;
        uint32_t dist = 110u + ((uint32_t)s * (max_reach / 2u)) / HER_STARS_PART + (her_rand() & 63u);
        int32_t sx = (int32_t)cx + ((dist * (uint32_t)her_cs(ang)) >> 10);
        int32_t sy = (int32_t)cy + ((dist * (uint32_t)her_sn(ang)) >> 10);
        her_stars[s].x = sx << 8;
        her_stars[s].y = sy << 8;
        her_stars[s].vx = 0;
        her_stars[s].vy = 0;
        her_stars[s].prev_x = sx;
        her_stars[s].prev_y = sy;
        her_stars[s].color = ((s & 3) == 0) ? 0x00FFFFFF : 0x00DCE8F5;
    }

    her_cosmic_ready = 1;
}

static void her_parts_spawn(uint32_t orb_cx, uint32_t orb_cy, uint32_t angle)
{
    (void)angle;
    if (!her_cosmic_ready) her_cosmic_init_all(orb_cx, orb_cy);
}

static void her_parts_update_draw(void)
{
    uint32_t cx = fb_get_width() / 2;
    uint32_t cy = fb_get_height() / 2;
    if (!her_cosmic_ready) her_cosmic_init_all(cx, cy);

    int spd = her_cur_speed + soma_vortex_boost;
    if (spd < 10) spd = 10;
    int step_mul = spd / 12;
    if (step_mul < 1) step_mul = 1;
    /* SOMA_153B: la fisica de las estrellas avanzaba UN paso por fotograma: su velocidad dependia de los fotogramas por
     * segundo (a 4 fps se arrastraban) y cada fotograma lento era un tiron. Ahora avanza en pasos fijos de 33 ms segun el
     * tiempo real transcurrido: misma velocidad a cualquier ritmo y movimiento uniforme. */
    static uint32_t st_last, st_acc;
    uint32_t st_now = (uint32_t)timer_get_uptime_ms();
    uint32_t st_dt = st_last ? st_now - st_last : 33u; st_last = st_now;
    if (st_dt > HER_ANIM_DT_MAX) st_dt = HER_ANIM_DT_MAX;
    st_acc += st_dt;
    int st_steps = (int)(st_acc / 33u); st_acc -= (uint32_t)st_steps * 33u;

    /* A. ESTRELLAS CÓSMICAS EN TODA LA PANTALLA */
    uint32_t max_d = fb_get_width() > fb_get_height() ? fb_get_width() : fb_get_height();
    for (int s = 0; s < HER_STARS_PART; ++s) {
        struct her_cosmic_star *st = &her_stars[s];
        int32_t px0 = st->prev_x, py0 = st->prev_y; int respawned = 0;
        for (int stp = 0; stp < st_steps && !respawned; ++stp) {
        int32_t cur_x = st->x >> 8;
        int32_t cur_y = st->y >> 8;
        int32_t dx = (int32_t)cx - cur_x;
        int32_t dy = (int32_t)cy - cur_y;
        /* PARCHE 030: dx*dx + dy*dy desbordaba int32 con estrellas lejanas (comportamiento indefinido; el valor envuelto
         * podia dar una "distancia" pequena falsa y activar el efecto del cursor en estrellas que no estaban cerca) */
        int64_t d2 = (int64_t)dx * dx + (int64_t)dy * dy;
        uint32_t dist = (uint32_t)her_isqrt(d2 > 0xFFFFFFFFLL ? 0xFFFFFFFFu : (uint32_t)d2);

        if (dist <= 30u) {
            uint32_t r_ang = (her_rand() >> 8) & 1023u;
            uint32_t r_dist = max_d / 2u + 20u + (her_rand() & 127u);
            int32_t nx = (int32_t)cx + ((r_dist * (uint32_t)her_cs(r_ang)) >> 10);
            int32_t ny = (int32_t)cy + ((r_dist * (uint32_t)her_sn(r_ang)) >> 10);
            st->x = nx << 8;
            st->y = ny << 8;
            st->vx = 0; st->vy = 0;
            st->prev_x = nx; st->prev_y = ny;
            respawned = 1;
            continue;
        }

        int32_t pull = 6 + (int32_t)(1200u / (dist + 30u));
        int32_t swirl = 5 + (int32_t)(1500u / (dist + 20u));
        int32_t ax = (dx * pull + dy * swirl) / (int32_t)dist;
        int32_t ay = (dy * pull - dx * swirl) / (int32_t)dist;

        st->vx = ((st->vx * 248) >> 8) + ax;
        st->vy = ((st->vy * 248) >> 8) + ay;
        st->x += st->vx;
        st->y += st->vy;
        }
        if (respawned) continue;
        if (st_steps == 0) { px0 = st->prev_x; py0 = st->prev_y; }

        int32_t px = st->x >> 8;
        int32_t py = st->y >> 8;
        int32_t ddx = (int32_t)cx - px, ddy = (int32_t)cy - py;
        int64_t dd2 = (int64_t)ddx * ddx + (int64_t)ddy * ddy;
        uint32_t dist = (uint32_t)her_isqrt(dd2 > 0xFFFFFFFFLL ? 0xFFFFFFFFu : (uint32_t)dd2);

        if (dist < 90u && (px0 != px || py0 != py)) {
            fb_draw_line(px0, py0, px, py, 0x008CA8C7);
        }
        st->prev_x = px;
        st->prev_y = py;

        if (dist < 80u) {
            fb_draw_pixel((uint32_t)px, (uint32_t)py, 0x00FFFFFF);
        } else {
            fb_draw_pixel((uint32_t)px, (uint32_t)py, st->color);
        }
    }

    /* B. ROSQUILLA DE ACRECIÓN (Toro de partículas denso) */
    for (int i = 0; i < HER_TORUS_PART; ++i) {
        struct her_particle *p = &her_torus[i];
        int32_t curr_r = p->r16 >> 8;

        int wave_offset = (her_ring_r16(p->angle) >> 4) - 56;
        int32_t effective_r = curr_r + wave_offset;

        int32_t ang_v = (int32_t)p->omega + (3600 / (curr_r > 5 ? curr_r : 5));
        p->angle = (uint16_t)((p->angle + (ang_v * step_mul * (int32_t)st_dt) / (4 * 33)) & 1023u);   /* SOMA_153B: por tiempo */

        int32_t rad_v = p->vr16 + (2400 / (curr_r > 6 ? curr_r : 6));
        p->r16 -= (rad_v * step_mul * (int32_t)st_dt) / (4 * 33);
        curr_r = p->r16 >> 8;

        if (curr_r <= 28) {
            p->r16 = (int32_t)(76u + (her_rand() & 23u)) << 8;
            p->angle = (uint16_t)((p->angle + 512u + (her_rand() & 255u)) & 1023u);
            p->color = her_grad((uint32_t)p->angle + (her_grad_acc >> 10));
            p->prev_x = -1;
            p->prev_y = -1;
            continue;
        }

        uint32_t axis = (uint32_t)her_axis_phase & 1023u;
        int32_t local_x = (effective_r * her_cs(p->angle)) >> 10;
        int32_t local_y = (effective_r * her_sn(p->angle)) >> 10;
        int32_t tilted_x = (local_x * her_cs(axis) - local_y * her_sn(axis)) >> 10;
        int32_t tilted_y = (local_x * her_sn(axis) + local_y * her_cs(axis)) >> 10;
        int32_t px = (int32_t)cx + tilted_x;
        int32_t py = (int32_t)cy + tilted_y;

        if (p->prev_x >= 0 && (p->prev_x != px || p->prev_y != py)) {
            /* Estelas blanco-frio; el nucleo sigue siendo blanco puro. */
            uint32_t trail_col = (curr_r < 42) ? 0x00F7FBFF : 0x00D5E2F0;
            uint32_t trail_light = (curr_r < 42) ? 184u : 120u;
            fb_draw_line(p->prev_x, p->prev_y, px, py,
                         her_scale(trail_col, trail_light));
        }
        p->prev_x = px;
        p->prev_y = py;

        if (curr_r <= 38) {
            fb_draw_pixel((uint32_t)px, (uint32_t)py, 0x00FFFFFF);
        } else if (p->size > 1) {
            fb_fill_circle(px, py, (int32_t)p->size, p->color);
            fb_draw_pixel((uint32_t)px, (uint32_t)py, 0x00FFFFFF);
        } else {
            fb_draw_pixel((uint32_t)px, (uint32_t)py, p->color);
        }
    }
}

/* El eje cambia de objetivo cada pocos segundos y se acerca suavemente.
 * Angulos en unidades de la tabla seno (1024 = vuelta completa). */
static void her_update_axis_wobble(uint64_t now_ms)
{
    if (her_axis_next_change == 0 || now_ms >= her_axis_next_change) {
        her_axis_target = (int32_t)(her_rand() % 97u) - 48;
        her_axis_next_change = now_ms + 2300u + (her_rand() % 2600u);
    }
    int32_t delta = her_axis_target - her_axis_phase;
    if (delta > 2) her_axis_phase += 2;
    else if (delta < -2) her_axis_phase -= 2;
    else her_axis_phase = her_axis_target;
}

/* ---------- orbe central: horizonte de sucesos ampliado y onda fluida ---------- */
static void her_draw_orb(uint32_t cx, uint32_t cy, uint32_t angle)
{
    static const int32_t W[4] = { 90, -140, 230, -330 };
    (void)angle;

    uint32_t t = (uint32_t)timer_get_uptime_ms();
    uint32_t dt = her_last_draw ? (t - her_last_draw) : 33u;
    her_last_draw = t;
    if (dt < 1u) dt = 1u;
    if (dt > HER_ANIM_DT_MAX) dt = HER_ANIM_DT_MAX;          /* SOMA_153B */

    int st = her_state();

    if (agent_global_len != her_prev_len) {
        her_pulse = 190u;
        her_prev_len = agent_global_len;
        soma_vortex_boost += 5;
        if (soma_vortex_boost > 14) soma_vortex_boost = 14;
    } else {
        her_pulse = (her_pulse > dt) ? her_pulse - dt : 0u;
    }

    int fluid_wave = her_sn((t % 2800u) * 1024u / 2800u) * 20 / 1024;

    int tr = 58 * 16, tsp = 15, tspec = 70, tglow = 70, bamp = 40;
    int ta[4] = { 42 + fluid_wave, 26 - fluid_wave / 2, 16 + fluid_wave / 3, 10 };
    uint32_t ttint = 0x007DD3FCu, bp = 4200u;

    int non_uniform_glide = (her_sn((t % 3600u) * 1024u / 3600u) * 8) / 1024;
    tsp += non_uniform_glide;

    switch (st) {
    case HER_ST_LISTEN:
        tr = 60 * 16;
        ta[0] = 43 + (int)(her_pulse * 4u / 25u);
        ta[1] = 30 + (int)(her_pulse / 18u);
        ta[2] = 16;
        tsp = 18 + non_uniform_glide; tspec = 52; tglow = 86; bamp = 42;
        ttint = 0x00BAE6FDu; bp = 2600u;
        break;
    case HER_ST_THINK:
        tr = 56 * 16;
        ta[0] = 62; ta[1] = 36; ta[2] = 20; ta[3] = 8;
        tsp = 20 + non_uniform_glide; tspec = 100; tglow = 88; bamp = 42;
        ttint = 0x00C4B5FDu; bp = 1700u;
        break;
    case HER_ST_SPEAK: {
        int e = (int)her_energy;
        tr = 58 * 16 + e * 28 / 255;
        ta[0] = 45 + e * 15 / 255;
        ta[1] = 30 + e * 18 / 255;
        ta[2] = 17 + e * 20 / 255;
        ta[3] = e * 10 / 255;
        tsp = 19 + e / 32; tspec = 100; tglow = 86 + e / 10; bamp = 42;
        ttint = 0x00FDA4AFu; bp = 1400u;
        break; }
    default:
        break;
    }

    /* Las acciones aportan un impulso pequeno y amortiguado, no un latigazo. */
    if (soma_vortex_boost > 0) {
        int boost_amp = (soma_vortex_boost * 8) / 100;
        ta[0] += boost_amp;
        ta[1] += boost_amp / 2;
        ta[2] += boost_amp / 3;
        tsp += soma_vortex_boost / 8;
        tglow += soma_vortex_boost / 12;
        soma_vortex_boost = (soma_vortex_boost * 210) >> 8;
    }

    uint32_t kk = (st == HER_ST_SPEAK) ? dt * 256u / 140u : dt * 256u / 360u;
    if (kk > 230u) kk = 230u;
    if (kk < 8u) kk = 8u;
    her_cur_r16 = her_ease(her_cur_r16, tr, kk);
    for (int k = 0; k < 4; ++k) her_cur_amp[k] = her_ease(her_cur_amp[k], ta[k], kk);
    her_cur_speed = her_ease(her_cur_speed, tsp, kk);
    her_cur_spec  = her_ease(her_cur_spec, tspec, kk);
    her_cur_glow  = her_ease(her_cur_glow, tglow, kk);
    her_col_cur = her_mix256(her_col_cur, ttint, kk);

    her_breath = her_sn((t % bp) * 1024u / bp) * bamp / 1024;
    for (int k = 0; k < 4; ++k)
        her_phacc[k] += (uint32_t)((int32_t)dt * W[k] * her_cur_speed / 16);
    her_grad_acc += dt * 60u * (uint32_t)her_cur_speed / 16u;

    /* ---- HORIZONTE DE SUCESOS ----
     * Sin circunferencia ni anillo fotonico dibujado: solo el nucleo negro.
     * La estructura se percibe a traves del flujo de particulas y sus estelas.
     * El eje del flujo cambia de inclinacion de forma lenta e irregular.
     */
    int32_t icx = (int32_t)cx, icy = (int32_t)cy;
    her_update_axis_wobble(t);
    fb_fill_circle(icx, icy, 27, 0x00000000);

}

static void agent_global_insert_str(const char *s)
{
    uint32_t slen = 0; while (s[slen]) slen++;
    if (slen == 0 || agent_global_len + slen >= sizeof(agent_global_cmd) - 2) return;
    for (uint32_t i = agent_global_len + slen; i >= agent_global_cursor + slen; --i)
        agent_global_cmd[i] = agent_global_cmd[i - slen];
    for (uint32_t i = 0; i < slen; ++i)
        agent_global_cmd[agent_global_cursor + i] = s[i];
    agent_global_cursor += slen;
    agent_global_len += slen;
    agent_global_cmd[agent_global_len] = '\0';
    agent_global_scroll = 0;
}

static void her_draw_user_zone(uint32_t cur_w, uint32_t cur_h, uint32_t orb_cy)
{
    (void)cur_h;
    if (her_icons_visible) return;
    if (cur_w < 160u) return;
    uint32_t t = (uint32_t)timer_get_uptime_ms();

    if (agent_global_len > 0) {
        static uint16_t ucells[SOMA_AGENT_CMD_MAX];
        uint32_t n = her_text_cells_n(agent_global_cmd, agent_global_len, ucells, SOMA_AGENT_CMD_MAX);
        int sc = (n * 18u <= cur_w - 120u) ? 2 : 1;
        uint32_t adv = 9u * (uint32_t)sc;
        uint32_t maxc = (cur_w - 80u) / adv;
        static uint16_t cur_dummy[SOMA_AGENT_CMD_MAX];
        uint32_t cur = her_text_cells_n(agent_global_cmd, agent_global_cursor, cur_dummy, SOMA_AGENT_CMD_MAX);
        if (cur > n) cur = n;
        uint32_t start = 0;
        if (n > maxc) {
            start = (cur > maxc) ? cur - maxc : 0u;
            if (start + maxc > n) start = n - maxc;
        }
        uint32_t shown = n - start;
        if (shown > maxc) shown = maxc;
        uint32_t w = shown * adv;
        uint32_t tx = (cur_w > w) ? (cur_w - w) / 2u : 0u;
        uint32_t ty = (orb_cy > 170u) ? orb_cy - 150u : 20u;
        for (uint32_t k = 0; k < shown; ++k) {
            uint16_t cell = ucells[start + k];
            if ((cell & 0xFFu) == ' ') continue;
            her_draw_cell((int32_t)(tx + k * adv), (int32_t)ty,
                          cell, 0x00F1F5F9u, sc);
        }
        if (agent_global_focus) {
            uint32_t lvl = (uint32_t)(her_sn((t % 1400u) * 1024u / 1400u) + 1024) * 256u / 2048u;
            uint32_t col = her_mix256(0x00334155u, her_col_cur, lvl);
            uint32_t cx = tx + (cur - start) * adv;
            fb_fill_rect((cx > 1u) ? cx - 1u : 0u, ty - 2u, (uint32_t)sc, 8u * (uint32_t)sc + 4u, col);
        }
    } else if (her_last_q[0] && (her_ans_len > 0 || her_agent_busy)) {
        static uint16_t qcells[SOMA_AGENT_CMD_MAX];
        uint32_t n = her_text_cells(her_last_q, qcells, SOMA_AGENT_CMD_MAX);
        uint32_t maxc = (cur_w - 80u) / 9u;
        if (n > maxc) n = maxc;
        uint32_t tx = (cur_w > n * 9u) ? (cur_w - n * 9u) / 2u : 0u;
        uint32_t ty = (orb_cy > 170u) ? orb_cy - 146u : 20u;
        for (uint32_t k = 0; k < n; ++k) {
            uint16_t cell = qcells[k];
            if ((cell & 0xFFu) == ' ') continue;
            her_draw_cell((int32_t)(tx + k * 9u), (int32_t)ty,
                          cell, 0x00475569u, 1);
        }
        {   /* SOMA_256: aviso de que Esc detiene lo que esta en marcha */
            const char *hs = 0;
#if defined(__x86_64__)
            if (voz_speaking()) hs = "Esc detiene la lectura en voz alta";
#endif
            if (!hs && her_agent_busy) hs = "Esc detiene la mision";
            if (hs) {
                uint32_t hn = 0; while (hs[hn]) hn++;
                uint32_t hx = (cur_w > hn * 9u) ? (cur_w - hn * 9u) / 2u : 0u;
                for (uint32_t k = 0; k < hn; ++k) her_draw_cell((int32_t)(hx + k * 9u), (int32_t)(ty + 14u), (uint16_t)(uint8_t)hs[k], 0x00334155u, 1);
            }
        }
    } else if (current_app == APP_DESKTOP && !her_vortex_closing) {
        /* Prompt interactivo listo para escribir al agente */
        uint32_t ty = (orb_cy > 170u) ? orb_cy - 146u : 20u;
#if defined(__x86_64__)
        const char *hint = agent_global_focus ? "soma> escribe una mision...   (manten Alt para dictar)" : "soma> escribe una mision...";   /* SOMA_245_PTT */
#else
        const char *hint = "soma> escribe una mision...";
#endif
        uint32_t n = 0; while (hint[n]) n++;
        uint32_t w = n * 9u;
        uint32_t tx = (cur_w > w) ? (cur_w - w) / 2u : 0u;
        for (uint32_t k = 0; k < n; ++k) {
            her_draw_cell((int32_t)(tx + k * 9u), (int32_t)ty,
                          (uint16_t)(uint8_t)hint[k], 0x00334155u, 1);
        }
        uint32_t lvl = (uint32_t)(her_sn((t % 1400u) * 1024u / 1400u) + 1024) * 256u / 2048u;
        uint32_t col = her_mix256(0x001E293Bu, her_col_cur, lvl);
        fb_fill_rect(tx + w + 3u, ty - 1u, 2u, 10u, col);
    }
}

static void her_draw_agent_zone(uint32_t cur_w, uint32_t cur_h, uint32_t orb_cy)
{
    if (her_icons_visible || her_text_suppressed_by_dock) return;
    if (her_pr_dibuja(cur_w, cur_h, orb_cy)) return;   /* SOMA_259_ORBE: prompter con formato y barra de fases (src/ui/gui_prompter.c) */
    her_bufs_ensure();
    if (her_ans_len == 0 && her_thk_len == 0) return;
    if (cur_w < 200u) return;

    uint32_t y = orb_cy + 112u;
    uint32_t bottom = (cur_h > 40u) ? cur_h - 40u : cur_h;
    if (her_icons_visible && cur_h > 130u) bottom = cur_h - 130u;
    if (bottom <= y + 32u) return;
    uint32_t avail = bottom - y;
    uint32_t width_cap = cur_w - 120u;
    uint32_t block_end = y;

    if (her_ans_len > 0) {
        int sc = (her_ans_len <= 150u) ? 2 : 1;
        uint32_t adv = 9u * (uint32_t)sc;
        uint32_t lh = (sc == 2) ? 28u : 16u;
        uint32_t wmax = width_cap;
        uint32_t cap = (sc == 2) ? 900u : 820u;
        if (wmax > cap) wmax = cap;
        uint32_t cols = wmax / adv;
        if (cols < 8u) cols = 8u;
        int n = her_wrap(her_ans, her_ans_len, cols, her_ans_ls, her_ans_ll, 40);
        uint32_t reserve = (her_thk_len > 0) ? 96u : 0u;
        uint32_t maxl = (avail > reserve + lh) ? (avail - reserve) / lh : 1u;
        if (maxl < 2u) maxl = 2u;
        uint32_t total = ((uint32_t)n < maxl) ? (uint32_t)n : maxl;
        uint32_t started = 0;
        while ((int)started < n && her_ans_ls[started] < her_ans_reveal) started++;
        uint32_t first = (started > maxl) ? started - maxl : 0u;
        for (uint32_t l = first; l < started; ++l) {
            uint32_t w = (uint32_t)her_ans_ll[l] * adv;
            uint32_t x = (cur_w > w) ? (cur_w - w) / 2u : 0u;
            her_draw_cells((int32_t)x, (int32_t)(y + (l - first) * lh),
                           her_ans + her_ans_ls[l], her_ans_ll[l], her_ans_ls[l],
                           her_ans_reveal, 0x00F1F5F9u, her_col_cur, sc);
        }
        block_end = y + total * lh;
    }

    if (her_thk_len > 0) {
        int has_ans = (her_ans_len > 0);
        uint32_t ty = has_ans ? block_end + 22u : y;
        uint32_t wmax = (width_cap > 760u) ? 760u : width_cap;
        uint32_t cols = wmax / 9u;
        if (cols < 8u) cols = 8u;
        int n = her_wrap(her_thk, her_thk_len, cols, her_thk_ls, her_thk_ll, 16);
        uint32_t started = 0;
        while ((int)started < n && her_thk_ls[started] < her_thk_reveal) started++;
        uint32_t first = (started > 3u) ? started - 3u : 0u;
        if (has_ans) {
            her_draw_label(cur_w, ty, "pensamiento", 0x00334155u);
            ty += 20u;
        }
        uint32_t base = has_ans ? 0x00475569u : 0x0064748Bu;
        uint32_t tip = her_mix(her_col_cur, base, 6u);
        if (ty + 48u < cur_h) {
            for (uint32_t l = first; l < started; ++l) {
                uint32_t w = (uint32_t)her_thk_ll[l] * 9u;
                uint32_t x = (cur_w > w) ? (cur_w - w) / 2u : 0u;
                her_draw_cells((int32_t)x, (int32_t)(ty + (l - first) * 16u),
                               her_thk + her_thk_ls[l], her_thk_ll[l], her_thk_ls[l],
                               her_thk_reveal, base, tip, 1);
            }
        }
    }
}
