/* gui_lang.c: pintado en vivo y ventana estandar de SOMA-LANG.
 * SOMA_234_GUI_PARTIDO: NO se compila por separado. Lo incluye src/ui/gui.c en su sitio (una sola unidad de
 * compilacion: comparte el estado del escritorio con gui.c, y el nucleo compilado sale identico). */
static uint32_t g_live_win_x = 0, g_live_win_y = 0, g_live_win_w = 0, g_live_win_h = 0;
static int g_live_ready = 0;
static int g_live_busy = 0;

static int live_has(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) return 0;
    for (; *hay; ++hay) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return 1;
    }
    return 0;
}

static int live_path_allowed(const char *name)
{
    static const char *const deny[] = {
        "/sys/", "/agent/", "/proc/", "/dev/", "_stm.txt", "/etc/history.txt", 0
    };
    for (int i = 0; deny[i]; ++i) {
        if (live_has(name, deny[i])) return 0;
    }
    return 1;
}

static int live_data_is_text(const char *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)d[i];
        if (c < 9 || (c > 13 && c < 32)) return 0;
    }
    return 1;
}

/* ====================================================================
 * SOMA_PARCHE_V2_GUI: hilo de fondo (animacion + raton) y hook en vivo
 * Las herramientas del agente ya NO detienen la animacion.
 * ==================================================================== */
static int32_t g_mouse_x_s, g_mouse_y_s;

static void gui_live_paint(void)
{
    if (!fb_is_active()) return;
    if (her_anim_alive) return;      /* el hilo de fondo repinta a 30 FPS */
    her_refresh_all();
    fb_present();
}

/*PL12_SOMA_WIN_IMPL*/
/* ====================================================================
 * SUBSISTEMA DE VENTANA ESTANDAR SOMA-LANG (Her-Aura Canvas)
 * ==================================================================== */
#define SOMA_APP_SURF_W 1024
#define SOMA_APP_SURF_H 768

static uint32_t *soma_app_surf = 0;
static uint32_t  soma_app_bg_col = 0x000F172A;
static char      soma_app_title_buf[64] = "SOMA App";
static volatile int soma_app_active_flag = 0;
static volatile int soma_app_clicked_flag = 0;
static int       soma_app_key_queue[32];
static int       soma_app_key_head = 0;
static int       soma_app_key_tail = 0;

static void soma_app_ensure_surf(void)
{
    if (!soma_app_surf) {
        soma_app_surf = (uint32_t *)kmalloc(SOMA_APP_SURF_W * SOMA_APP_SURF_H * sizeof(uint32_t));
        if (soma_app_surf) {
            for (uint32_t i = 0; i < SOMA_APP_SURF_W * SOMA_APP_SURF_H; ++i)
                soma_app_surf[i] = soma_app_bg_col;
        }
    }
}

const char *gui_soma_win_get_title(void)
{
    return soma_app_title_buf[0] ? soma_app_title_buf : "SOMA App";
}

int gui_soma_win_is_active(void)
{
    if (!fb_is_active()) return 0;
    return soma_app_active_flag && (her_find_window(APP_SOMA_APP) >= 0);
}

void gui_soma_win_on_close(void)
{
    soma_app_active_flag = 0;
}

/* PARCHE 098: un programa SOMA-LANG corre en su propio hilo y dibuja en su superficie; el escritorio sigue vivo y es SU bucle quien
 * abre, compone y cierra la ventana. Antes el programa corria dentro del hilo del escritorio (todo congelado) o se adueñaba de la
 * pantalla. */
static volatile uint32_t g_gui_tid;
static volatile int g_soma_req_open, g_soma_req_close, g_soma_dirty;
static char g_soma_req_title[64]; static int g_soma_req_w, g_soma_req_h;
static int gui_on_gui_thread(void) { if (!g_gui_loop_on) return 1; struct tcb *t = thread_current(); return t && t->tid == g_gui_tid; }

/* SOMA_221_RESCATE: contencion de aplicaciones del escritorio (x86_64).
 * Las aplicaciones corren en el hilo del escritorio, en ring 0: un puntero nulo o un bucle sin fin dentro de cualquiera de ellas
 * tumbaba o congelaba el sistema entero. Ahora, si ocurre mientras el escritorio da paso a una aplicacion (fuera del asignador de
 * memoria y sin un reparto entre nucleos en curso):
 *   - una excepcion de CPU (#DE, #UD, #GP, #PF) o un desbordamiento de pila detectado por el protector, o
 *   - mas de HER_HANG_MS de procesador seguidos en una sola llamada (la interfaz llevaria ese tiempo congelada),
 * el hilo vuelve al principio del bucle del escritorio, la ventana de esa aplicacion se cierra (su estado se da por perdido: no se
 * libera, por si esta danado) y un aviso dice que paso. Es contencion, no aislamiento: tras HER_RESCUE_MAX rescates en un arranque
 * el siguiente fallo vuelve a ser un panico. */
#define HER_HANG_MS    10000u
#define HER_RESCUE_MAX 16
#if defined(__x86_64__)
static void *her_jb[5];
static volatile int her_jb_armed, her_rescuing;
static int her_rescue_app = -1, her_rescue_kind, her_rescue_vec;
static uint64_t her_rescue_rip, her_rescue_addr;
static uint32_t her_rescues;
static uint8_t her_rescue_stack[16384] __attribute__((aligned(16)));
static char her_rescue_msg[160];
volatile int gui_rescue_inject;                                   /* pruebas: 1 = fallo de memoria, 2 = bucle sin fin */
static int her_rescue_ok(void)
{
    if (!her_jb_armed || her_rescuing || !g_gui_loop_on || her_rescues >= HER_RESCUE_MAX) return 0;
    if (her_guard_owner < ACCT_APP0 || her_guard_owner == ACCT_APP0 + APP_DESKTOP) return 0;
    struct tcb *t = thread_current();
    if (!t || t->tid != g_gui_tid) return 0;
    return !kheap_busy() && !smp_busy();
}
static __attribute__((noreturn, noinline)) void her_rescue_entry(void)
{
    kmutex_release_all_for_thread(thread_current());             /* como al matar un hilo: cerrojos que tenia a medias */
    __builtin_longjmp(her_jb, 1);
}
static void her_rescue_arm(int kind, uint64_t *rip, uint64_t *rsp)
{
    her_rescuing = 1; her_jb_armed = 0;
    her_rescue_kind = kind; her_rescue_app = her_guard_owner - ACCT_APP0;
    *rip = (uint64_t)(uintptr_t)her_rescue_entry;
    *rsp = (((uint64_t)(uintptr_t)her_rescue_stack + sizeof her_rescue_stack) & ~15ull) - 8;
}
int gui_fault_recover(int vector, uint64_t fault_rip, uint64_t fault_addr, uint64_t *rip, uint64_t *rsp)
{
    if (vector != 0 && vector != 6 && vector != 13 && vector != 14) return 0;
    if (!her_rescue_ok()) return 0;
    her_rescue_vec = vector; her_rescue_rip = fault_rip; her_rescue_addr = fault_addr;
    her_rescue_arm(1, rip, rsp);
    return 1;
}
int gui_hang_tick(uint64_t *rip, uint64_t *rsp)                  /* cada milisegundo, desde la interrupcion del temporizador */
{
    if (her_guard_owner < ACCT_APP0 || !her_jb_armed) return 0;
    struct tcb *t = thread_current();
    if (!t || t->tid != g_gui_tid) return 0;                      /* solo cuenta el tiempo que el escritorio esta en la CPU */
    if (++her_guard_ticks < HER_HANG_MS) return 0;
    if (!her_rescue_ok()) { her_guard_ticks = HER_HANG_MS / 2; return 0; }   /* en un momento delicado: se vuelve a mirar luego */
    her_rescue_vec = -2; her_rescue_rip = *rip; her_rescue_addr = 0;
    her_rescue_arm(2, rip, rsp);
    return 1;
}
void gui_fault_from_ssp(void)
{
    if (!her_rescue_ok()) return;
    her_rescue_vec = -1; her_rescue_rip = (uint64_t)(uintptr_t)__builtin_return_address(0); her_rescue_addr = 0;
    her_rescuing = 1; her_jb_armed = 0; her_rescue_kind = 1; her_rescue_app = her_guard_owner - ACCT_APP0;
    her_rescue_entry();
}
static void her_rc_cat(const char *t) { uint32_t n = 0; while (her_rescue_msg[n]) ++n; while (*t && n + 1 < sizeof her_rescue_msg) her_rescue_msg[n++] = *t++; her_rescue_msg[n] = 0; }
#endif
static int gui_soma_win_open_impl(const char *title, int w, int h);
static void gui_soma_win_close_impl(void);
int gui_soma_win_open(const char *title, int w, int h)
{
    if (gui_on_gui_thread()) return gui_soma_win_open_impl(title, w, h);
    uint32_t i = 0; if (!title) title = "";
    while (title[i] && i < sizeof g_soma_req_title - 1) { g_soma_req_title[i] = title[i]; i++; }
    g_soma_req_title[i] = 0; g_soma_req_w = w; g_soma_req_h = h;
    soma_app_ensure_surf();
    g_soma_req_open = 1;
    for (uint32_t k = 0; g_soma_req_open && g_gui_loop_on && k < 3000u; k++) thread_sleep(1);
    return gui_soma_win_is_active();
}
static int g_soma_win_tok;   /* PARCHE 091: un programa grafico lanzado por el agente tiene la pantalla hasta que cierra */
void gui_soma_win_close(void)
{
    if (!gui_on_gui_thread()) {                       /* PARCHE 098 */
        soma_app_active_flag = 0; g_soma_req_close = 1;
        for (uint32_t k = 0; g_soma_req_close && g_gui_loop_on && k < 2000u; k++) thread_sleep(1);
        return;
    }
    gui_soma_win_close_impl();
}
static void gui_soma_win_close_impl(void)
{
    soma_app_active_flag = 0;
    int idx = her_find_window(APP_SOMA_APP);
    if (idx >= 0) {
        her_close_target_window_vortex(idx);
    } else {
        int mi = her_find_minimized(APP_SOMA_APP);
        if (mi >= 0) her_kill_minimized(mi);
    }
    if (g_soma_win_tok) { int t = g_soma_win_tok; g_soma_win_tok = 0; gui_leave(t); }
}

static void gui_soma_service(int32_t cmx, int32_t cmy)   /* llamado en cada vuelta del bucle del escritorio */
{
    if (g_soma_req_open) { gui_soma_win_open_impl(g_soma_req_title, g_soma_req_w, g_soma_req_h); g_soma_req_open = 0; }
    if (g_soma_req_close) { gui_soma_win_close_impl(); g_soma_req_close = 0; her_refresh_all(); fb_present(); }
    if (g_soma_dirty) {
        g_soma_dirty = 0;
        int si = her_find_window(APP_SOMA_APP);
        if (si >= 0 && !her_dragging && !her_resizing) {
            struct her_win_node *sw = &her_windows[si];
            int covered = 0;
            for (int j = si + 1; j < her_num_windows; j++) {
                struct her_win_node *o = &her_windows[j];
                if (o->x < sw->x + sw->w && sw->x < o->x + o->w && o->y < sw->y + sw->h && sw->y < o->y + o->h) { covered = 1; break; }
            }
            if (!covered && settings_opacity() >= 100) her_render_app_body(APP_SOMA_APP, HER_CLI_X(sw->x), HER_CLI_Y(sw->y), HER_CLI_W(sw->w), HER_CLI_H(sw->h), si == her_num_windows - 1);
            else her_refresh_all();
            if (her_cursor_should_show()) mouse_draw_cursor(cmx, cmy);
            fb_present();
        }
    }
}
static int gui_soma_win_open_impl(const char *title, int w, int h)
{
    if (!fb_is_active()) return 0;
    soma_app_ensure_surf();
    if (title && title[0]) {
        uint32_t i = 0;
        while (title[i] && i < sizeof(soma_app_title_buf) - 1) {
            soma_app_title_buf[i] = title[i];
            i++;
        }
        soma_app_title_buf[i] = '\0';
    } else {
        soma_app_title_buf[0] = 'S'; soma_app_title_buf[1] = 'O'; soma_app_title_buf[2] = 'M';
        soma_app_title_buf[3] = 'A'; soma_app_title_buf[4] = ' '; soma_app_title_buf[5] = 'A';
        soma_app_title_buf[6] = 'p'; soma_app_title_buf[7] = 'p'; soma_app_title_buf[8] = '\0';
    }

    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    if (cur_w == 0 || cur_h == 0) return 0;

    int existing = her_find_window(APP_SOMA_APP);
    if (existing >= 0) {
        her_bring_to_front(existing);
        if (w >= 120 && h >= 80) {
            uint32_t nw = (uint32_t)w;
            uint32_t nh = (uint32_t)(h + HER_TITLE_H);
            if (nw > cur_w - 20) nw = cur_w - 20;
            if (nh > cur_h - 20) nh = cur_h - 20;
            her_windows[existing].w = nw;
            her_windows[existing].h = nh;
        }
        soma_app_active_flag = 1;
        gui_live_paint();
        return 1;
    }

    her_open_app(APP_SOMA_APP, cur_w, cur_h);
    int idx = her_find_window(APP_SOMA_APP);
    if (idx >= 0 && w >= 120 && h >= 80) {
        uint32_t nw = (uint32_t)w;
        uint32_t nh = (uint32_t)(h + HER_TITLE_H);
        if (nw > cur_w - 20) nw = cur_w - 20;
        if (nh > cur_h - 20) nh = cur_h - 20;
        her_windows[idx].w = nw;
        her_windows[idx].h = nh;
    }
    soma_app_active_flag = 1;
    gui_live_paint();
    return 1;
}

void gui_soma_win_clear(uint32_t color)
{
    soma_app_ensure_surf();
    if (!soma_app_surf) return;
    soma_app_bg_col = color;
    for (uint32_t i = 0; i < SOMA_APP_SURF_W * SOMA_APP_SURF_H; ++i) {
        soma_app_surf[i] = color;
    }
}

void gui_soma_win_pixel(int x, int y, uint32_t color)
{
    soma_app_ensure_surf();
    if (!soma_app_surf) return;
    if (x >= 0 && x < (int)SOMA_APP_SURF_W && y >= 0 && y < (int)SOMA_APP_SURF_H) {
        soma_app_surf[y * SOMA_APP_SURF_W + x] = color;
    }
}

void gui_soma_win_rect(int x, int y, int w, int h, uint32_t color)
{
    soma_app_ensure_surf();
    if (!soma_app_surf || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)SOMA_APP_SURF_W) w = (int)SOMA_APP_SURF_W - x;
    if (y + h > (int)SOMA_APP_SURF_H) h = (int)SOMA_APP_SURF_H - y;
    if (w <= 0 || h <= 0) return;

    for (int r = 0; r < h; ++r) {
        uint32_t *row = soma_app_surf + (y + r) * SOMA_APP_SURF_W + x;
        for (int c = 0; c < w; ++c) {
            row[c] = color;
        }
    }
}

void gui_soma_win_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    soma_app_ensure_surf();
    if (!soma_app_surf) return;
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        gui_soma_win_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void gui_soma_win_circle(int cx, int cy, int r, uint32_t color)
{
    soma_app_ensure_surf();
    if (!soma_app_surf || r <= 0) return;
    int x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        gui_soma_win_rect(cx - x, cy + y, 2 * x + 1, 1, color);
        gui_soma_win_rect(cx - x, cy - y, 2 * x + 1, 1, color);
        gui_soma_win_rect(cx - y, cy + x, 2 * y + 1, 1, color);
        gui_soma_win_rect(cx - y, cy - x, 2 * y + 1, 1, color);
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

void gui_soma_win_text(int x, int y, const char *str, uint32_t color)
{
    soma_app_ensure_surf();
    if (!soma_app_surf || !str) return;
    int cur_x = x;
    int cur_y = y;
    while (*str) {
        char ch = *str++;
        if (ch == '\n') {
            cur_x = x;
            cur_y += 10;
            continue;
        }
        if (ch == ' ') {
            cur_x += 8;
            continue;
        }
        const uint8_t *g = font_get_glyph(ch);
        if (g) {
            for (int r = 0; r < 8; ++r) {
                uint8_t bits = g[r];
                for (int c = 0; c < 8; ++c) {
                    if (bits & (1 << (7 - c))) {
                        gui_soma_win_pixel(cur_x + c, cur_y + r, color);
                    }
                }
            }
        }
        cur_x += 8;
    }
}

static volatile uint32_t g_soma_presents;   /* SOMA_248_DOC: fotogramas presentados (para la captura de la revision visual) */
void gui_soma_win_present(void)
{
    g_soma_presents++;
    if (!fb_is_active()) return;
    if (!gui_on_gui_thread()) { g_soma_dirty = 1; return; }   /* PARCHE 098: el escritorio compone el fotograma */
    int idx = her_find_window(APP_SOMA_APP);
    if (idx >= 0) {
        struct her_win_node *win = &her_windows[idx];
        fb_damage((int32_t)win->x, (int32_t)win->y, (int32_t)win->w, (int32_t)win->h);
    }
    fb_present();
}

/* SOMA_248_DOC: copia de lo que ve el usuario en la ventana del programa (la parte de la superficie que cabe en la ventana).
 * Devuelve el numero de fotogramas presentados hasta ahora; *px (kmalloc) queda a 0 si no hay ventana. */
uint32_t gui_soma_win_snapshot(uint32_t **px, int *w, int *h)
{
    *px = 0; *w = *h = 0;
    if (!soma_app_surf || !gui_soma_win_is_active()) return g_soma_presents;
    int ww, wh; gui_soma_win_get_size(&ww, &wh);
    if (ww > SOMA_APP_SURF_W) ww = SOMA_APP_SURF_W;
    if (wh > SOMA_APP_SURF_H) wh = SOMA_APP_SURF_H;
    uint32_t *b = (uint32_t *)kmalloc((size_t)ww * (size_t)wh * 4u);
    if (!b) return g_soma_presents;
    for (int y = 0; y < wh; y++) for (int x = 0; x < ww; x++) b[y * ww + x] = soma_app_surf[(uint32_t)y * SOMA_APP_SURF_W + (uint32_t)x] | 0xFF000000u;
    *px = b; *w = ww; *h = wh;
    return g_soma_presents;
}
uint32_t gui_soma_win_presents(void) { return g_soma_presents; }
void gui_soma_win_get_size(int *out_w, int *out_h)
{
    int idx = her_find_window(APP_SOMA_APP);
    if (idx >= 0) {
        *out_w = (int)her_windows[idx].w - 4;
        *out_h = (int)her_windows[idx].h - HER_TITLE_H - 3;
        if (*out_w < 10) *out_w = 10;
        if (*out_h < 10) *out_h = 10;
    } else {
        *out_w = 640;
        *out_h = 420;
    }
}

void gui_soma_win_get_mouse(int *out_rx, int *out_ry, int *out_btn, int *out_inside, int *out_clicked)
{
    int idx = her_find_window(APP_SOMA_APP);
    if (idx >= 0) {
        struct her_win_node *win = &her_windows[idx];
        int cx = (int)win->x + 2;
        int cy = (int)win->y + HER_TITLE_H + 1;
        int cw = (int)win->w - 4;
        int ch = (int)win->h - HER_TITLE_H - 3;
        int rx = g_mouse_x_s - cx;
        int ry = g_mouse_y_s - cy;
        *out_rx = rx;
        *out_ry = ry;
        *out_btn = mouse_left_held();
        *out_inside = (rx >= 0 && ry >= 0 && rx < cw && ry < ch);
        *out_clicked = soma_app_clicked_flag;
        soma_app_clicked_flag = 0;
    } else {
        *out_rx = 0;
        *out_ry = 0;
        *out_btn = 0;
        *out_inside = 0;
        *out_clicked = 0;
    }
}

void gui_soma_win_record_click(void)
{
    soma_app_clicked_flag = 1;
}

void gui_soma_win_push_key(int key)
{
    int next = (soma_app_key_head + 1) % 32;
    if (next != soma_app_key_tail) {
        soma_app_key_queue[soma_app_key_head] = key;
        soma_app_key_head = next;
    }
}

int gui_soma_win_get_key(void)
{
    if (soma_app_key_head != soma_app_key_tail) {
        int k = soma_app_key_queue[soma_app_key_tail];
        soma_app_key_tail = (soma_app_key_tail + 1) % 32;
        return k;
    }
    if (!gui_on_gui_thread()) return 0;                /* PARCHE 098: con el escritorio vivo, las teclas llegan por la cola */
    if (kgetc_ready()) {
        return (unsigned char)kgetc();
    }
    return 0;
}

void gui_soma_win_render_body(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    if (!soma_app_surf) {
        fb_fill_rect(wx, wy, ww, wh, soma_app_bg_col);
        return;
    }
    uint32_t copy_w = (ww < SOMA_APP_SURF_W) ? ww : SOMA_APP_SURF_W;
    uint32_t copy_h = (wh < SOMA_APP_SURF_H) ? wh : SOMA_APP_SURF_H;

    fb_blit((int32_t)wx, (int32_t)wy, soma_app_surf, (int32_t)copy_w, (int32_t)copy_h, SOMA_APP_SURF_W);
    if (copy_w < ww) {
        fb_fill_rect(wx + copy_w, wy, ww - copy_w, copy_h, soma_app_bg_col);
    }
    if (copy_h < wh) {
        fb_fill_rect(wx, wy + copy_h, ww, wh - copy_h, soma_app_bg_col);
    }
}

static void gui_live_file_hook_body(const char *name, const char *data, uint32_t len);
static void her_open_notepad_path(const char *path, uint32_t cw, uint32_t ch);
static void gui_live_file_hook(const char *name, const char *data, uint32_t len)
{
    int tok = gui_enter();              /* PARCHE 091 */
    gui_live_file_hook_body(name, data, len);
    gui_leave(tok);
}
static void gui_live_file_hook_body(const char *name, const char *data, uint32_t len)
{
    if (!g_live_ready || g_live_busy || !name || !name[0]) return;
    if (!fb_is_active()) return;

    struct tcb *self = thread_current();
    if (!self || !self->agent_mode) return;
    if (self->tid != 0 && !gui_in_agent_thread()) return;   /* PARCHE 091: tambien desde el hilo del agente */

    if (!live_path_allowed(name)) return;
    if (!data) { data = ""; len = 0; }
    if (!live_data_is_text(data, len)) return;

    g_live_busy = 1;
    uint32_t cw = fb_get_width(), ch = fb_get_height();

    if (live_has(data, "#SOMA-LAYOUT") || live_has(name, "_layout.txt") ||
        live_has(name, "layout.txt") || live_has(name, ".tex") || live_has(data, "\\documentclass")) {   /* PARCHE 084: LaTeX */
        her_open_app(APP_LAYOUT, cw, ch);
        layout_set_source(data, len);
        gui_live_paint();
        g_live_busy = 0;
        return;
    }

    her_open_notepad_path(name, cw, ch);                 /* SOMA_211_NOTAS: la ventana de ese archivo, o una nueva */
    notepad_live_begin(name, data, len);
    gui_live_paint();

    uint32_t chunk = 4u + len / 80u;
    if (chunk > 200u) chunk = 200u;

    while (notepad_live_step(chunk) > 0) {
        gui_live_paint();
        thread_sleep(20);
    }

    notepad_live_end();
    gui_live_paint();
    g_live_busy = 0;
}

static int gui_bg_mouse_poll(void)
{
    uint32_t w = fb_get_width(), h = fb_get_height();
    int moved = 0;
    if (w == 0u || h == 0u) return 0;
    for (int i = 0; i < 8; ++i) {
        int32_t dx = 0, dy = 0; int clk = 0;
        if (!mouse_poll(&dx, &dy, &clk)) break;
        if (dx || dy) { g_mouse_x_s += dx; g_mouse_y_s -= dy; moved = 1; }
    }
    if (g_mouse_x_s < 0) g_mouse_x_s = 0;
    if (g_mouse_x_s >= (int32_t)w) g_mouse_x_s = (int32_t)w - 1;
    if (g_mouse_y_s < 0) g_mouse_y_s = 0;
    if (g_mouse_y_s >= (int32_t)h) g_mouse_y_s = (int32_t)h - 1;
    return moved;
}

static void gui_bg_cursor(void)
{
    if (her_cursor_should_show()) mouse_draw_cursor(g_mouse_x_s, g_mouse_y_s);
    else mouse_erase_cursor();
}

static void her_anim_worker(void *arg)
{
    (void)arg;
    uint64_t last = 0;
    /* SOMA_155D: con ventanas abiertas cada fotograma de fondo recompone la pantalla entera (escritorio + orbe + todas las
     * ventanas + volcado completo). A 30 fps fijos eso se comia la CPU en hardware real (T480s: un nucleo, 1080p/1440p y
     * volcado a la VRAM real) y el raton iba a saltos. Ahora: (1) se mide lo que cuesta ese fotograma y, con ventanas, se
     * espacian para que el fondo use como mucho ~1/4 de la CPU (entre 30 y 4 fps); (2) mientras se mueve el raton o se teclea
     * el fondo espera 150 ms, asi el puntero y la ventana activa van primero. Escritorio sin ventanas: igual que antes. */
    uint64_t bg_cost = 0, last_input = 0;
    uint32_t kev_seen = console_kbd_events();
    while (her_agent_busy) {
        if (fb_is_active() && !fb_user_exclusive()) {          /* SOMA_PARCHE_021_FPU */
            int moved = gui_bg_mouse_poll();
            uint64_t now = timer_get_uptime_ms();
            uint32_t kev = console_kbd_events();
            if (moved || kev != kev_seen || mouse_left_held()) { last_input = now; kev_seen = kev; }
            uint64_t interval = 33u;
            int multi = smp_cpus() > 1;                         /* SOMA_164B_T480S: ver el bucle principal */
            if (her_num_windows > 0) {
                interval = bg_cost * (multi ? 2u : 4u);              /* SOMA_164C_T480S */
                if (interval < 33u) interval = 33u;
                if (interval > 250u) interval = 250u;
            }
            int interacting = her_num_windows > 0 && now - last_input < 150u && (!multi || now - last < 66u);
            if (now - last >= interval && !interacting) {
                last = now;
                uint32_t w = fb_get_width(), h = fb_get_height();
                her_angle = (her_angle + 3) & 255;
                if (her_icons_visible) {
                    if (her_icons_anim_phase < HER_ICON_ANIM_MAX) {
                        her_icons_anim_phase += 36u;
                        if (her_icons_anim_phase > HER_ICON_ANIM_MAX)
                            her_icons_anim_phase = HER_ICON_ANIM_MAX;
                    }
                } else if (her_icons_anim_phase > 0u) {
                    her_icons_anim_phase = (her_icons_anim_phase > 36u)
                                         ? her_icons_anim_phase - 36u : 0u;
                }
                her_reveal_step();
                her_parts_spawn(w / 2, h / 2, her_angle);
                her_vortex_close_step();
                her_refresh_all();
                gui_bg_cursor();
                fb_present();
                {   /* SOMA_155D: coste medio del fotograma de fondo (media movil) */
                    uint64_t c = timer_get_uptime_ms() - now;
                    bg_cost = bg_cost ? (bg_cost * 3u + c) / 4u : c;
                }
            } else if (moved) {
                gui_bg_cursor();
                fb_present();
            }
        }
        thread_sleep(4);
    }
    her_anim_alive = 0;
}

static void her_anim_start(void)
{
    her_agent_busy = 1;
    if (her_anim_alive) return;
    her_anim_alive = 1;
    if (!thread_create("her_anim", her_anim_worker, 0)) {
        her_anim_alive = 0;
        her_agent_busy = 0;
    }
}

/* Parada real: solo la llama quien termina la mision del agente */
static void her_anim_stop(void)
{
    her_agent_busy = 0;
    for (int i = 0; i < 400 && her_anim_alive; ++i)
        thread_sleep(5);
}

/* Obsoleta: antes las herramientas paraban la animacion y congelaban la GUI */
static void her_anim_halt(void)
{
}

static void ensure_window_geom(void)
{
    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    if (cur_w == 0 || cur_h == 0) return;
    her_place_window_corner(cur_w, cur_h, 0);
    g_live_win_x = HER_CLI_X(her_win_x);
    g_live_win_y = HER_CLI_Y(her_win_y);
    g_live_win_w = HER_CLI_W(her_win_w);
    g_live_win_h = HER_CLI_H(her_win_h);

}
