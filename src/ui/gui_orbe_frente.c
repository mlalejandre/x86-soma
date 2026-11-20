/* gui_orbe_frente.c: el orbe al frente (tecla Windows) y la prueba de ventanas.
 * SOMA_234_GUI_PARTIDO: NO se compila por separado. Lo incluye src/ui/gui.c en su sitio (una sola unidad de
 * compilacion: comparte el estado del escritorio con gui.c, y el nucleo compilado sale identico). */
/* ===================== SOMA_168B_ORBE: el orbe al frente (tecla Windows, u Option sola en el Mac) =====================
 * En el escritorio vacio la tecla muestra u oculta las aplicaciones alrededor del orbe (lo que antes hacia la x).
 * Con aplicaciones abiertas el orbe sale del fondo y se pone DELANTE de todas, en el centro de la pantalla:
 *   apertura (~0,45 s): el fondo se oscurece con un velo azul noche, un campo de estrellas sale disparado desde el centro
 *   como un salto a la velocidad de la luz y se frena hasta quedar como estrellas que titilan, el orbe emerge de la
 *   oscuridad con ondas de choque y particulas, y las aplicaciones salen en orbita;
 *   cierre (~0,3 s, con Esc, otra vez la tecla, un clic fuera o al elegir una aplicacion): todo se recoge hacia el
 *   centro y el orbe vuelve detras de las ventanas.
 * Mientras esta delante se dibuja sobre una copia congelada de las ventanas (sin volver a pintar cada aplicacion). */
#define SUM_OPEN_MS  450u
#define SUM_CLOSE_MS 300u
#define SUM_STARS    140
static struct { uint16_t a, r; uint8_t b, tw; } sum_st[SUM_STARS];
static int sum_st_ready;
static uint32_t sum_ease(uint32_t t)                       /* 0..256 -> 0..256, 1 - (1 - t)^3 */
{
    if (t >= 256u) return 256u;
    uint32_t u = 256u - t;
    return 256u - (u * u / 256u) * u / 256u;
}
static void sum_stars_init(void)
{
    if (sum_st_ready) return;
    sum_st_ready = 1;
    for (int i = 0; i < SUM_STARS; i++) {
        sum_st[i].a = (uint16_t)(her_rand() & 1023u);
        uint32_t r = 90u + her_rand() % 935u; sum_st[i].r = (uint16_t)r;            /* fraccion del radio de la pantalla (/1024) */
        sum_st[i].b = (uint8_t)(90u + her_rand() % 166u);
        sum_st[i].tw = (uint8_t)(her_rand() & 255u);
    }
}
static void sum_draw(uint32_t cur_w, uint32_t cur_h, const uint32_t *snap, uint32_t k, int closing, uint64_t now)
{
    int32_t cx = (int32_t)(cur_w / 2u), cy = (int32_t)(cur_h / 2u);
    fb_reset_clip();
    if (snap) fb_blit(0, 0, snap, (int32_t)cur_w, (int32_t)cur_h, (int32_t)cur_w);
    fb_damage(0, 0, (int32_t)cur_w, (int32_t)cur_h);
    fb_fill_rect_alpha(0, 0, (int32_t)cur_w, (int32_t)cur_h, ((205u * k / 256u) << 24) | 0x00030714u);   /* velo azul noche */

    /* estrellas: salto (estelas largas que se frenan) al abrir; al cerrar, caen hacia el centro */
    volatile uint32_t *bb = fb_get_buffer();
    if (bb) {
        int32_t D = (int32_t)her_isqrt(cur_w * cur_w / 4u + cur_h * cur_h / 4u);
        for (int i = 0; i < SUM_STARS; i++) {
            int32_t base = (int32_t)sum_st[i].r * D / 1024;
            int32_t rr = base * (int32_t)(60u + 196u * k / 256u) / 256;
            int32_t L = (int32_t)((256u - k) * 90u / 256u) + 1;
            int32_t r0 = closing ? rr + L : rr - L;
            if (r0 < 0) r0 = 0;
            int32_t ux = her_cs(sum_st[i].a), uy = her_sn(sum_st[i].a);
            int32_t x1 = cx + rr * ux / 1024, y1 = cy + rr * uy / 1024, x0 = cx + r0 * ux / 1024, y0 = cy + r0 * uy / 1024;
            uint32_t tw = 190u + (uint32_t)((her_sn((uint32_t)((now / 3u + sum_st[i].tw * 4u) & 1023u)) + 1024) * 66 / 2048);
            uint32_t lum = (uint32_t)sum_st[i].b * tw / 256u * k / 256u;
            uint32_t col = (i % 7 == 0) ? 0x00C4B5FDu : (i % 5 == 0) ? 0x0067E8F9u : 0x00E2E8F0u;
            uint32_t head = ofx_mul(col, lum), tail = ofx_mul(col, lum / 6u);
            if (x1 < 1 || y1 < 1 || x1 >= (int32_t)cur_w - 1 || y1 >= (int32_t)cur_h - 1) continue;
            ofx_line(bb, (int32_t)cur_w, (int32_t)cur_h, x0, y0, x1, y1, tail, head);
            if (k > 200u && sum_st[i].b > 200u) { ofx_px(bb, (int32_t)cur_w, (int32_t)cur_h, x1 + 1, y1, ofx_mul(head, 90u)); ofx_px(bb, (int32_t)cur_w, (int32_t)cur_h, x1, y1 + 1, ofx_mul(head, 90u)); }
        }
    }

    /* el orbe completo, delante */
    ofx_orb_back((uint32_t)cx, (uint32_t)cy);
    her_parts_update_draw();
    her_draw_orb((uint32_t)cx, (uint32_t)cy, her_angle);
    ofx_orb_front((uint32_t)cx, (uint32_t)cy);
    if (k < 250u) {                                          /* emerge de la oscuridad (o se hunde en ella al cerrar): bruma de borde suave */
        int32_t R0 = ofx_radius((int32_t)cur_h);
        uint32_t a = (255u - k) * 7u / 20u;
        for (int j = 4; j >= 0; j--) {                       /* 5 discos concentricos: el centro queda mas oscuro que el borde */
            int32_t R = R0 * (8 + 2 * j) / 10;
            fb_fill_rrect_aa(cx - R, cy - R, 2 * R, 2 * R, R, (a << 24) | 0x00030714u);
        }
    }
    her_draw_dock(cur_w, cur_h);
    her_draw_minimized_icons(cur_w, cur_h);
    her_draw_vortex_particles();
    if (!closing && k > 128u) {
        const char *hint = "Elige una aplicacion  -  Esc o la tecla Windows para volver";
        uint32_t n = (uint32_t)font_text_width(hint, 1);           /* SOMA_226_SISTEMA: centrado con la fuente del escritorio */
        font_draw_string(cur_w / 2u - n / 2u, cur_h - 34u, hint, her_mix256(0x00030714u, 0x0094A3B8u, (k - 128u) * 2u), 1);
    }
    hst_dibujar(cur_w, cur_h);
}
/* Devuelve la aplicacion elegida (ya abierta o traida al frente) o -1 */
static int her_summon_run(uint32_t cur_w, uint32_t cur_h)
{
    int32_t cx = (int32_t)(cur_w / 2u), cy = (int32_t)(cur_h / 2u);
    sum_stars_init();
    her_summon_on = 1;
    her_refresh_all();                                       /* ventanas sin el orbe de fondo */
    fb_tr_off(); fb_cguard_keep(0);
    uint32_t *snap = fb_rect_save(0, 0, (int32_t)cur_w, (int32_t)cur_h);
    uint64_t now = timer_get_uptime_ms(), t0 = now, beat = now, tc = 0;
    her_text_suppressed_by_dock = 0;
    her_icons_visible = 1; her_icons_anim_phase = 0; her_icons_burst_opening = 1; her_icons_burst_at = now;
    ofx_trigger_icons(cx, cy);
    ofx_spawn_ring(20, 14, 26, 0x0067E8F9u);                 /* onda grande del salto */
    mouse_set_cursor(CURSOR_ARROW);
    int closing = 0, chosen = -1, chosen_min = -1;
    uint32_t k = 0, k_close = 256u;
    for (;;) {
        uint64_t f0 = timer_get_uptime_ms(); now = f0;
        int32_t dx = 0, dy = 0; int clk = 0, guard = 0;
        while (guard++ < 64 && mouse_poll(&dx, &dy, &clk)) {
            mouse_x += dx; mouse_y -= dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_x >= (int32_t)cur_w) mouse_x = (int32_t)cur_w - 1;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_y >= (int32_t)cur_h) mouse_y = (int32_t)cur_h - 1;
            if (her_min_drag >= 0) { her_min_drag_x = mouse_x; her_min_drag_y = mouse_y; }
            if (her_dp_kind && mouse_left_held()) her_dp_motion(mouse_x, mouse_y, cur_w, cur_h);
            if (clk && !closing) {
                int energy = her_power_hit(mouse_x, mouse_y, cur_w, cur_h);
                if (energy) her_power_press(energy);
                else {
                    int mi = her_min_hit(mouse_x, mouse_y, cur_w, cur_h);
                    if (mi >= 0) { her_min_drag = mi; her_min_start_x = mouse_x; her_min_start_y = mouse_y; }
                    else if (!her_dp_kind && her_dp_begin(mouse_x, mouse_y, cur_w, cur_h)) { /* se decide al soltar: toque = abrir, arrastre = recolocar */ }
                }
            }
            dx = dy = 0; clk = 0;
        }
        if (her_dp_kind && !mouse_left_held()) {
            int a = her_dp_end(cur_w, cur_h);
            if (a >= 0 && !closing) { chosen = a; closing = 1; }
        }
        if (her_min_drag >= 0 && !mouse_left_held()) {
            int mi = her_min_drag; her_min_drag = -1;
            if (her_min_drop_center(mouse_x, mouse_y, cur_w, cur_h)) her_kill_minimized_vortex(mi, mouse_x, mouse_y);
            else {
                int32_t dx = mouse_x - her_min_start_x, dy = mouse_y - her_min_start_y;
                if (dx * dx + dy * dy <= 1600) { chosen_min = mi; closing = 1; }
            }
        }
        while (kgetc_ready()) { char c = kgetc(); if (c == 27) closing = closing ? closing : 1; }
        if (console_take_super() && !closing) closing = 1;
        if ((g_gui_req || fb_user_exclusive()) && !closing) closing = 1;
        if (closing == 1) {                                  /* empieza el cierre */
            closing = 2; tc = now; k_close = k;
            her_icons_visible = 0;
            ofx_spawn_ring(160, -6, 20, 0x00A855F7u);        /* ondas que se contraen hacia el orbe */
            ofx_spawn_ring(110, -4, 22, 0x0038BDF8u);
            if (chosen >= 0) { ofx_flash += 120u; if (ofx_flash > 255u) ofx_flash = 255u; }
        }
        if (now - beat >= 33u) {                             /* mismo latido que el escritorio */
            beat = now;
            her_angle = (her_angle + 3) & 255;
            if (her_icons_visible) { her_icons_anim_phase += 36u; if (her_icons_anim_phase > HER_ICON_ANIM_MAX) her_icons_anim_phase = HER_ICON_ANIM_MAX; }
            else her_icons_anim_phase = her_icons_anim_phase > 36u ? her_icons_anim_phase - 36u : 0u;
            her_parts_spawn((uint32_t)cx, (uint32_t)cy, her_angle);
            her_vortex_close_step();
        }
        if (!closing) { uint64_t e = now - t0; k = e >= SUM_OPEN_MS ? 256u : sum_ease((uint32_t)(e * 256u / SUM_OPEN_MS)); }
        else {
            uint64_t e = now - tc;
            if (e >= SUM_CLOSE_MS || fb_user_exclusive()) break;
            k = k_close * (256u - sum_ease((uint32_t)(e * 256u / SUM_CLOSE_MS))) / 256u;
        }
        if (!snap) { her_refresh_all(); fb_tr_off(); fb_cguard_keep(0); }   /* sin memoria para la copia: se repinta todo */
        sum_draw(cur_w, cur_h, snap, k, closing != 0, now);
        mouse_draw_cursor(mouse_x, mouse_y);
        fb_present();
        uint64_t spent = timer_get_uptime_ms() - f0;
        if (spent < 16u) thread_sleep((uint32_t)(16u - spent));
    }
    if (snap) kfree(snap);
    her_summon_on = 0;
    her_icons_visible = 0; her_icons_anim_phase = 0;
    her_power_pending = 0;
    her_min_drag = -1; her_dp_kind = 0; her_idrag = -1; hst_gdrag = 0;
    if (chosen_min >= 0) her_restore_minimized(chosen_min);
    if (chosen >= 0) {
        her_open_app((enum app_mode)chosen, cur_w, cur_h);
        ofx_trigger_app(cx, cy, chosen);
    }
    return chosen;
}
/* SOMA_220_PRUEBA: 'escritorio prueba [n]' abre, pinta, minimiza y cierra n veces cada aplicacion y compara la memoria del
 * nucleo antes y despues. Se ejecuta en el hilo del escritorio (la orden solo deja la peticion y espera el resultado). */
static volatile int gui_stress_n, gui_stress_done;
static char *gui_stress_out; static uint32_t gui_stress_max;
static void her_stress_put(uint32_t *p, const char *t) { while (*t && *p + 1 < gui_stress_max) gui_stress_out[(*p)++] = *t++; gui_stress_out[*p] = 0; }
static void her_stress_num(uint32_t *p, int64_t v)
{
    char b[24]; int n = 0; int neg = v < 0; uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
    do { b[n++] = (char)('0' + u % 10u); u /= 10u; } while (u && n < 22);
    if (neg) b[n++] = '-';
    char o[24]; int m = 0; while (n) o[m++] = b[--n]; o[m] = 0; her_stress_put(p, o);
}
static int64_t her_stress_open_d;                               /* memoria con la ventana abierta respecto a antes de abrirla */
static int her_stress_cycle(enum app_mode app, uint32_t cw, uint32_t ch)
{
    int before = her_num_windows;
    size_t u0 = 0, f0 = 0, u1 = 0, f1 = 0; kheap_stats(&u0, &f0);
    her_open_app(app, cw, ch);
    if (her_num_windows != before + 1) return 0;
    int idx = her_num_windows - 1;
    her_refresh_all();                                          /* que la aplicacion se dibuje (algunas reservan al pintar) */
    kheap_stats(&u1, &f1); her_stress_open_d = (int64_t)u1 - (int64_t)u0;
    her_minimize_window(idx);
    for (int i = her_min_count - 1; i >= 0; --i)
        if (her_minimized[i].app == app) { her_kill_minimized(i); return 1; }
    return 0;
}
static void her_stress_run(int n, uint32_t cw, uint32_t ch)
{
    static const enum app_mode apps[] = { APP_NOTEPAD, APP_IMAGE_VIEWER, APP_PDF, APP_FILE_MANAGER, APP_PAINT, APP_CODER_APP,
                                          APP_TERMINAL_SHELL, APP_SYSMON, APP_SETTINGS, APP_WEB_BROWSER };
    uint32_t p = 0; int bad = 0;
    her_stress_put(&p, "Prueba de ventanas: "); her_stress_num(&p, n); her_stress_put(&p, " aperturas y cierres por aplicacion\n");
    for (unsigned a = 0; a < sizeof apps / sizeof apps[0]; ++a) {
        if (her_num_windows >= HER_MAX_WINDOWS - 1) { her_stress_put(&p, "  (no quedan ventanas libres: cierra alguna)\n"); break; }
        int pin = 0;                                            /* con una ventana ya abierta, las demas usan las instancias 2+ */
        if (her_multi_app(apps[a])) { int b = her_num_windows; her_open_app(apps[a], cw, ch); pin = her_num_windows == b + 1; }
        her_stress_cycle(apps[a], cw, ch);                      /* calentamiento: cajones que se reservan una sola vez */
        size_t u0 = 0, f0 = 0, u1 = 0, f1 = 0; kheap_stats(&u0, &f0);
        int ok = 0;
        for (int k = 0; k < n; ++k) ok += her_stress_cycle(apps[a], cw, ch);
        kheap_stats(&u1, &f1);
        if (pin) {
            int w = her_find_window(apps[a]);
            if (w >= 0) { her_minimize_window(w); for (int i = her_min_count - 1; i >= 0; --i) if (her_minimized[i].app == apps[a]) { her_kill_minimized(i); break; } }
        }
        int64_t d = (int64_t)u1 - (int64_t)u0;
        int leak = d > 4096;
        bad |= leak || ok != n;
        her_stress_put(&p, "  "); her_stress_put(&p, gui_app_title((int)apps[a]));
        her_stress_put(&p, ": "); her_stress_num(&p, ok); her_stress_put(&p, "/"); her_stress_num(&p, n);
        her_stress_put(&p, " ciclos | abierta +"); her_stress_num(&p, her_stress_open_d / 1024); her_stress_put(&p, " KiB | al final ");
        her_stress_put(&p, d >= 0 ? "+" : ""); her_stress_num(&p, d); her_stress_put(&p, " B");
        her_stress_put(&p, leak ? "  <- NO VUELVE\n" : ok != n ? "  <- NO SE ABRIO\n" : "  ok\n");
    }
    int hc = kheap_check();
    her_stress_put(&p, "  integridad del heap: "); her_stress_put(&p, hc == 0 ? "correcta\n" : "DANADA\n");
    her_stress_put(&p, (bad || hc) ? "RESULTADO: revisar las lineas marcadas\n" : "RESULTADO: correcto (la memoria vuelve a su valor)\n");
    her_lay_fresh_req = 1;
}
