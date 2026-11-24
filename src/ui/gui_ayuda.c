/* gui_ayuda.c: SOMA_294_AYUDA: la bienvenida del orbe y la ayuda rapida del escritorio.
 * SOMA_234_GUI_PARTIDO: NO se compila por separado; lo incluye src/ui/gui.c (comparte el estado del escritorio).
 *
 *   Al encender, 1,5 s despues de la primera imagen del escritorio, el orbe da la bienvenida (la escribe y, si Ajustes > Voz
 *   lo tiene activado, la lee) y abajo en el centro salen consejos cortos, uno cada 6 s, con una barra de tiempo:
 *   tecla Windows, mantener Alt para hablar, escribir y pulsar Intro, la X que minimiza, el clic derecho que cierra y F1.
 *   Hacer lo que dice el consejo lo da por aprendido ("Eso es") y pasa al siguiente; lo ya hecho no se repite.
 *   F1, en cualquier momento, muestra u oculta la ayuda completa (todas las teclas) en el centro.
 *
 * Nada bloquea: SOMA se usa desde el segundo cero. La ayuda no se queda con ninguna tecla ni con el raton; solo toma Esc y el
 * clic cuando hay una tarjeta a la vista (para cerrarla o pasar de consejo). Si el usuario empieza a escribir de verdad (40
 * teclas) los consejos se apartan solos, y si actua antes de la bienvenida no se le habla encima. En la ayuda completa se
 * puede quitar la casilla "Mostrar estos consejos al iniciar" (se guarda en /etc/ayuda.conf). La logica esta en ayuda.h. */
#include "ayuda.h"

#define HER_AY_CONF "/etc/ayuda.conf"
static struct ayuda her_ay;
static int her_ay_arrancada;          /* una vez por encendido (la GUI puede volver a entrar) */
static int her_ay_repinta;            /* cambio lo que se ve: el bucle repinta lo de debajo */
static int her_ay_wf = -1;            /* hay fuente con acentos */
static int her_ay_dibujada;           /* la ultima imagen tenia una tarjeta (para el clic) */
static int her_ay_voz_vista;          /* el dictado ya se conto como hecho en esta pulsacion */
static int her_ay_modo_prev;          /* la tarjeta a la vista en el ultimo repintado (AY_NADA si ninguna) */
static uint64_t her_ay_dict_hasta;    /* el texto del dictado entra por el teclado: no cuenta como "escribir mucho" */

static int her_ay_voz_ok(void)
{
#if defined(__x86_64__)
    struct micrec_info mi; micrec_info(&mi);
    return usermode_avx2_ok() && mi.available;
#else
    return 0;
#endif
}

static void her_ay_guardar(void)
{
    char b[16];
    int n = ay_conf_texto(her_ay.inicio, b, sizeof b);
    if (n > 0) (void)vfs_write(HER_AY_CONF, b, (uint32_t)n);
}

/* antes del bucle del escritorio */
static void her_ayuda_arranque(void)
{
    if (her_ay_arrancada) return;
    her_ay_arrancada = 1;
    char b[64]; int n = vfs_read(HER_AY_CONF, b, sizeof b - 1);
    int inicio = 1;
    if (n > 0) { b[n] = 0; inicio = ay_conf_inicio(b, 1); }
    ay_arranque(&her_ay, timer_get_uptime_ms(), inicio, her_ay_voz_ok());
}

static void her_ay_ev(int ev)
{
    int r = ay_evento(&her_ay, ev, timer_get_uptime_ms());
    if (r & AY_REPINTA) { her_ay_repinta = 1; her_ay_dibujada = 0; }
    if (her_ay.guardar) { her_ay.guardar = 0; her_ay_guardar(); }
}
static void her_ayuda_f1(void) { her_ay.voz_ok = her_ay_voz_ok(); her_ay_ev(AYEV_F1); }

/* cada vuelta del bucle: 0 nada, 1 basta volcar, 2 repintar todo */
static int her_ayuda_tick(void)
{
    uint64_t now = timer_get_uptime_ms();
    if (her_voz_shown_kind == 1 && !her_ay_voz_vista) { her_ay_voz_vista = 1; her_ay_ev(AYEV_VOZ); }   /* se esta dictando */
    else if (her_voz_shown_kind != 1) her_ay_voz_vista = 0;
    if (her_voz_shown_kind == 2) her_ay_dict_hasta = now + 3000u;
    int r = ay_tick(&her_ay, now), saludo = 0;
    if (her_ay.saludo_pend) {                       /* la bienvenida: escrita en el orbe y, si esta activado, leida */
        her_ay.saludo_pend = 0;
        if (her_voz_shown_kind == 0) {
            her_aviso(ay_saludo(&her_ay));
#if defined(__x86_64__)
            (void)voz_speak_reply(ay_saludo(&her_ay));
#endif
            r = 2; saludo = 1;
        }
    }
    if (her_ay_repinta) { her_ay_repinta = 0; if (!r) r = 1; }
    /* la tarjeta es opaca y se dibuja entera en cada volcado: si sigue en su sitio (otro consejo, "Eso es", la casilla) basta
     * volcar; solo al aparecer, desaparecer o cambiar de tarjeta hay que repintar lo que tapaba (y tras la bienvenida, el orbe) */
    int vis = (her_ay.modo == AY_RECORRIDO || her_ay.modo == AY_TARJETA) ? her_ay.modo : AY_NADA;
    if (vis != her_ay_modo_prev) { r = 2; her_ay_dibujada = 0; }
    else if (r == 2 && !saludo) r = vis ? 1 : 0;
    her_ay_modo_prev = vis;
    return r;
}

/* cada tecla del escritorio, antes que nadie salvo el portapapeles y el dictado. 1 = era para la ayuda (Esc con tarjeta) */
static int her_ayuda_tecla(char c)
{
    if (c == 27) {
        int r = ay_evento(&her_ay, AYEV_ESC, timer_get_uptime_ms());
        if (r & AY_REPINTA) { her_ay_repinta = 1; her_ay_dibujada = 0; }
        return (r & AY_CONSUME) ? 1 : 0;
    }
    if (c == '\r' || c == '\n') {
        if (current_app == APP_DESKTOP || agent_global_focus) her_ay_ev(AYEV_ENTER);   /* lo que se escribe al orbe */
        return 0;
    }
    if (((unsigned char)c >= 32 && c != 127) && timer_get_uptime_ms() >= her_ay_dict_hasta) her_ay_ev(AYEV_TECLA);
    return 0;
}

/* clic izquierdo: 1 = era sobre una tarjeta de la ayuda (no sigue hacia lo de debajo) */
static int her_ayuda_clic(int32_t mx, int32_t my)
{
    if (!her_ay_dibujada) return 0;
    int z = ay_zona(&her_ay, fb_get_width(), fb_get_height(), SOMA_SYSTEM_BAR_H, mx, my);
    if (!z) return 0;
    int r = ay_evento(&her_ay, ay_zona_evento(z), timer_get_uptime_ms());
    if (r & AY_REPINTA) { her_ay_repinta = 1; her_ay_dibujada = 0; }
    if (her_ay.guardar) { her_ay.guardar = 0; her_ay_guardar(); }
    return (r & AY_CONSUME) ? 1 : 0;
}

/* ---------------------------------------------------------------- dibujo (lo llama fb_present, encima de todo) */
static int her_ay_hay_wf(void) { if (her_ay_wf < 0) her_ay_wf = wf_init() ? 1 : 0; return her_ay_wf; }
static int her_ay_w(const char *s, int px, int bold)
{
    if (her_ay_hay_wf()) return si_text_w(s, px, bold);
    char b[200]; return ay_ascii(s, b, sizeof b) * 8;
}
/* texto centrado en vertical en [top, top+h); si no cabe en maxw se recorta con "..." (sin partir una letra con acento) */
static void her_ay_txt(int32_t x, int32_t top, int32_t h, const char *s, int maxw, int px, uint32_t col, int bold)
{
    char b[200]; int n = 0;
    if (her_ay_hay_wf()) { while (s[n] && n < 190) { b[n] = s[n]; n++; } b[n] = 0; }
    else n = ay_ascii(s, b, 190);
    if (maxw > 0 && her_ay_w(b, px, bold) > maxw) {
        int dots = her_ay_w("...", px, bold);
        while (n > 0) {
            n--; while (n > 0 && ((unsigned char)b[n] & 0xC0) == 0x80) n--;
            b[n] = 0;
            if (her_ay_w(b, px, bold) + dots <= maxw) break;
        }
        b[n++] = '.'; b[n++] = '.'; b[n++] = '.'; b[n] = 0;
    }
    if (her_ay_hay_wf()) si_text(x, top, h, b, px, col, bold);
    else font_draw_string((uint32_t)x, (uint32_t)(top + (h - 10) / 2), b, col, 1);
}
static void her_ay_num(char *o, int v) { int n = 0; if (v >= 10) o[n++] = (char)('0' + v / 10 % 10); o[n++] = (char)('0' + v % 10); o[n] = 0; }
static void her_ay_orbe(int32_t x, int32_t y)
{
    fb_fill_rrect_aa(x, y, 16, 16, 8, 0xFF38BDF8u);
    fb_fill_rrect_aa(x + 4, y + 4, 8, 8, 4, 0xFF0B1220u);
}
static void her_ay_cerrar(struct ay_rect c)
{
    struct ay_rect x = ay_rect_x(c);
    fb_fill_rrect_aa(x.x, x.y, x.w, x.h, 8, 0xFF1E293Bu);
    her_ay_txt(x.x + (x.w - her_ay_w("X", 13, 1)) / 2, x.y, x.h, "X", 0, 13, 0x00CBD5E1u, 1);
}
static int32_t her_ay_tecla(int32_t x, int32_t y, const char *t, int px)    /* la tecla en su "chapa"; devuelve el ancho */
{
    int32_t w = her_ay_w(t, px, 1) + 18;
    fb_fill_rrect_aa(x, y, w, 24, 7, 0xFF1E3A5Fu);
    fb_stroke_rrect_aa(x, y, w, 24, 7, 0xFF60A5FAu);
    her_ay_txt(x + 9, y, 24, t, 0, px, 0x00F8FAFCu, 1);
    return w;
}

static void her_ay_dibuja_recorrido(uint32_t sw, uint32_t sh, uint64_t now)
{
    const struct ay_consejo *k = ay_consejo(&her_ay, her_ay.i);
    if (!k) return;
    struct ay_rect c = ay_rect_recorrido(sw, sh, SOMA_SYSTEM_BAR_H);
    if (c.w < 200) return;
    fb_fill_rrect_aa(c.x, c.y, c.w, c.h, 16, 0xFF0B1220u);
    fb_stroke_rrect_aa(c.x, c.y, c.w, c.h, 16, her_ay.t_bien ? 0xFF4ADE80u : 0xFF38BDF8u);
    her_ay_orbe(c.x + 18, c.y + 16);
    int32_t tw = her_ay_tecla(c.x + 44, c.y + 12, k->tecla, 13);
    struct ay_rect x = ay_rect_x(c);
    if (her_ay.t_bien) her_ay_txt(x.x - 12 - her_ay_w("\302\241Eso es!", 14, 1), c.y + 12, 24, "\302\241Eso es!", 0, 14, 0x0086EFACu, 1);
    else {
        char n[40] = "Consejo "; int p = 8; char d[4];
        her_ay_num(d, her_ay.i + 1); for (int i = 0; d[i]; i++) n[p++] = d[i];
        n[p++] = ' '; n[p++] = 'd'; n[p++] = 'e'; n[p++] = ' ';
        her_ay_num(d, AY_NCONSEJOS); for (int i = 0; d[i]; i++) n[p++] = d[i];
        n[p] = 0;
        int32_t nw = her_ay_w(n, 12, 0);
        if (c.x + 44 + tw + 16 + nw < x.x - 12) her_ay_txt(x.x - 12 - nw, c.y + 12, 24, n, 0, 12, 0x0094A3B8u, 0);
    }
    her_ay_cerrar(c);
    her_ay_txt(c.x + 20, c.y + 42, 28, k->texto, c.w - 40, 15, 0x00F1F5F9u, 0);
    her_ay_txt(c.x + 20, c.y + 72, 16, "F1: ayuda completa   \302\267   Esc: cerrar   \302\267   clic: siguiente", c.w - 40, 11, 0x0064748Bu, 0);
    int32_t bw = c.w - 40, fill = (int32_t)((uint32_t)bw * ay_progreso(&her_ay, now) / 256u);
    fb_fill_rect(c.x + 20, c.y + c.h - 8, bw, 3, 0x001E293Bu);
    if (fill > 0) fb_fill_rect(c.x + 20, c.y + c.h - 8, fill, 3, her_ay.t_bien ? 0x004ADE80u : 0x0038BDF8u);
    fb_damage(c.x, c.y, c.w + 1, c.h + 1);
    her_ay_dibujada = 1;
}

static void her_ay_dibuja_tarjeta(uint32_t sw, uint32_t sh)
{
    struct ay_rect c = ay_rect_tarjeta(sw, sh, SOMA_SYSTEM_BAR_H);
    if (c.w < 300) return;
    fb_fill_rrect_aa(c.x, c.y, c.w, c.h, 18, 0xFF0B1220u);
    fb_stroke_rrect_aa(c.x, c.y, c.w, c.h, 18, 0xFF60A5FAu);
    her_ay_orbe(c.x + 20, c.y + 22);
    her_ay_txt(c.x + 46, c.y + 14, 32, "Ayuda r\303\241pida de SOMA", 0, 18, 0x00F8FAFCu, 1);
    struct ay_rect x = ay_rect_x(c);
    int32_t tit = her_ay_w("Ayuda r\303\241pida de SOMA", 18, 1);
    const char *sub = "Nada se bloquea: sigue usando SOMA";
    int32_t subw = her_ay_w(sub, 12, 0);
    if (c.x + 46 + tit + 24 + subw < x.x - 14) her_ay_txt(x.x - 14 - subw, c.y + 14, 32, sub, 0, 12, 0x0094A3B8u, 0);
    her_ay_cerrar(c);
    fb_fill_rect(c.x + 16, c.y + AY_TAR_CAB - 8, c.w - 32, 1, 0x001E293Bu);
    int32_t col = 0;                                                   /* la columna de las teclas: la mas ancha */
    for (int i = 0; i < AY_NFILAS; i++) { int32_t w = her_ay_w(ay_filas[i].tecla, 13, 1) + 18; if (w > col) col = w; }
    if (col > c.w / 2) col = c.w / 2;
    for (int i = 0; i < AY_NFILAS; i++) {
        int32_t ry = c.y + AY_TAR_CAB + i * AY_FILA_H;
        if (i & 1) fb_fill_rect(c.x + 10, ry, c.w - 20, AY_FILA_H, 0x00101A2Eu);
        (void)her_ay_tecla(c.x + 22, ry + 3, ay_filas[i].tecla, 13);
        her_ay_txt(c.x + 22 + col + 16, ry, AY_FILA_H, ay_fila_texto(&her_ay, i), c.w - (22 + col + 16) - 20, c.w >= 700 ? 14 : 13, 0x00E2E8F0u, 0);
    }
    fb_fill_rect(c.x + 16, c.y + c.h - AY_TAR_PIE + 2, c.w - 32, 1, 0x001E293Bu);
    struct ay_rect in = ay_rect_inicio(c);
    int32_t bx = in.x + 2, by = in.y + 6;
    fb_fill_rrect_aa(bx, by, 18, 18, 4, her_ay.inicio ? 0xFF38BDF8u : 0xFF0B1220u);
    fb_stroke_rrect_aa(bx, by, 18, 18, 4, 0xFF60A5FAu);
    if (her_ay.inicio) {                                               /* la marca: dos trazos de 2 px */
        for (int i = 0; i < 4; i++) fb_fill_rect(bx + 4 + i, by + 8 + i, 2, 2, 0x000B1220u);
        for (int i = 0; i < 7; i++) fb_fill_rect(bx + 7 + i, by + 11 - i, 2, 2, 0x000B1220u);
    }
    her_ay_txt(bx + 28, in.y, in.h, "Mostrar estos consejos al iniciar", in.w - 30, 13, 0x00CBD5E1u, 0);
    const char *pie = "F1 o Esc para cerrar";
    int32_t pw = her_ay_w(pie, 12, 0);
    if (in.x + in.w + 12 < c.x + c.w - 20 - pw) her_ay_txt(c.x + c.w - 20 - pw, in.y, in.h, pie, 0, 12, 0x0064748Bu, 0);
    fb_damage(c.x, c.y, c.w + 1, c.h + 1);
    her_ay_dibujada = 1;
}

static void her_ayuda_draw(void)
{
    her_ay_dibujada = 0;
    if (her_ay.modo != AY_RECORRIDO && her_ay.modo != AY_TARJETA) return;
    if (her_ay.modo == AY_RECORRIDO && her_voz_shown_kind) return;      /* la pastilla del dictado va en ese sitio */
    uint32_t sw = fb_get_width(), sh = fb_get_height();
    if (sw < 320u || sh < 240u) return;
    int32_t c0, c1, c2, c3; fb_get_clip(&c0, &c1, &c2, &c3);
    fb_reset_clip();
    if (her_ay.modo == AY_TARJETA) her_ay_dibuja_tarjeta(sw, sh);
    else her_ay_dibuja_recorrido(sw, sh, timer_get_uptime_ms());
    fb_set_clip(c0, c1, c2 - c0, c3 - c1);
}
