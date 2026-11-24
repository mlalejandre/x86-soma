/* SOMA_292_PAINT_IA: dibujar en Paint con el modelo de lenguaje. NO se compila por separado: lo incluye src/apps/paint.c al final.
 *
 *   Boton IA de la barra (o la orden del orbe "dibujame un faro"): se escribe que dibujar y Enter. Un hilo ("paint_ia") pide al modelo
 *   un SVG de 640x480 (fondo, despues cada objeto en su grupo); se comprueba que el rasterizador lo dibuja y que no sale vacio (si
 *   no, se pide otra vez una vez, diciendo por que). Despues Paint lo pinta en el lienzo POR PASOS, de atras hacia delante, como si
 *   lo dibujara a mano (paint_svg.c). Un lienzo sin tocar pasa a 640x480; si ya habia dibujo, se pinta encima. Todo el dibujo es un
 *   solo paso de deshacer (Ctrl+Z lo quita) y despues se retoca con las herramientas de siempre.
 * Un encargo a la vez. Es de una ventana de Paint: si se cierra, se descarta. Estados y avisos en la barra de estado. */

enum { IA_LIBRE = 0, IA_PIDIENDO, IA_LISTO, IA_PINTANDO, IA_ERROR };
static volatile int ia_estado = IA_LIBRE;
static volatile int ia_slot = -1;                 /* ventana (instancia) del encargo */
static volatile int ia_huerfano;                  /* la ventana se cerro mientras el modelo pensaba */
static volatile int ia_resultado;                 /* para el orbe: 1 dibujado, -1 fallo, 0 en marcha */
static char ia_pedido[240], ia_tema[120], ia_err[160];
static char *volatile ia_svg; static volatile uint32_t ia_svg_n;
/* pintando */
static char *ia_dib, *ia_pre; static uint32_t ia_dib_n;
static uint32_t *ia_base; static int ia_bw, ia_bh;
static uint32_t ia_fin[256]; static int ia_n, ia_pasos, ia_paso; static uint64_t ia_t_sig, ia_t0;

static const char SYS_SVG[] =
    "Eres un ilustrador. Dibuja lo que te pidan como UNA imagen SVG bonita y reconocible.\n"
    "Reglas:\n"
    "- Empieza con <svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 640 480\"> y termina con </svg>.\n"
    "- Primero, si hace falta, <defs> con degradados (linearGradient, radialGradient). Despues el fondo: un <rect> de 640x480 (cielo, pared, paisaje...).\n"
    "- Despues los objetos de atras hacia delante, cada uno en su propio <g> (por ejemplo: cielo, montanas, casa, arbol, personaje).\n"
    "- Usa rect, circle, ellipse, polygon, polyline, line y path (M, L, C, Q, A, Z); colores vivos y armonicos; contornos oscuros finos (stroke) donde ayuden.\n"
    "- Nada de <text>, <image>, filtros, mascaras, clipPath, patrones ni animaciones.\n"
    "- Responde SOLO con el SVG, sin explicaciones y sin ```.";

/* ---------------------------------------------------------------- el modelo (hilo paint_ia, o el del agente) */
static int ia_genera(const char *pedido, char **svg_out, uint32_t *n_out, char *err, int ecap)
{
    const uint32_t CAP = 96u * 1024u;
    char *rep = (char *)kmalloc(CAP), *svg = (char *)kmalloc(CAP), *pr = (char *)kmalloc(1024);
    int ok = 0;
    *svg_out = 0; *n_out = 0; err[0] = 0;
    if (!rep || !svg || !pr) { s_cpy(err, "sin memoria", ecap); goto fin; }
    for (int intento = 0; intento < 2 && !ok; intento++) {
        pr[0] = 0; s_cat(pr, "Dibuja: ", 1024); s_cat(pr, pedido, 1024);
        if (intento) { s_cat(pr, "\n\nEl SVG anterior no valia (", 1024); s_cat(pr, err, 1024); s_cat(pr, "). Hazlo otra vez, mas sencillo, completo y terminado en </svg>.", 1024); }
        llm_trace_tag("paint");
        int r = llm_chat_ex(SYS_SVG, pr, 0, 0, 6000, rep, CAP, 300000u);
        if (r != 1) { s_cpy(err, llm_last_error[0] ? llm_last_error : "el modelo no ha respondido", ecap); break; }
        uint32_t n = ps_extrae(rep, svg, CAP);
        if (!n) { s_cpy(err, "no ha devuelto un dibujo SVG", ecap); continue; }
        struct wimg im; memset(&im, 0, sizeof im);
        if (wi_decode_svg((const uint8_t *)svg, n, &im, 160, 120, 0) != 0 || !im.px) { s_cpy(err, "el SVG tiene errores y no se puede dibujar", ecap); wi_free(&im); continue; }
        uint32_t tot = (uint32_t)im.w * (uint32_t)im.h, c = ps_cubiertos(im.px, tot);
        wi_free(&im);
        if (c * 50u < tot) { s_cpy(err, "el dibujo sale vacio", ecap); continue; }
        *svg_out = svg; *n_out = n; svg = 0; ok = 1;
    }
fin:
    if (rep) kfree(rep);
    if (svg) kfree(svg);
    if (pr) kfree(pr);
    return ok;
}
static void ia_hilo(void *arg)
{
    (void)arg;
    char *svg = 0; uint32_t n = 0; char e[160];
    int ok = ia_genera(ia_pedido, &svg, &n, e, sizeof e);
    if (ia_huerfano) { if (svg) kfree(svg); ia_huerfano = 0; ia_slot = -1; ia_resultado = -1; s_cpy(ia_err, "se cerro la ventana de Paint", sizeof ia_err); ia_estado = IA_LIBRE; return; }
    if (ok) { ia_svg = svg; ia_svg_n = n; ia_estado = IA_LISTO; }
    else { s_cpy(ia_err, e, sizeof ia_err); ia_estado = IA_ERROR; }
}
static int ia_lanza(const char *pedido)
{
    if (ia_estado != IA_LIBRE) return -1;
    s_cpy(ia_pedido, pedido, sizeof ia_pedido); ps_tema(pedido, ia_tema, sizeof ia_tema);
    ia_slot = pt_live; ia_huerfano = 0; ia_resultado = 0; ia_err[0] = 0; ia_t0 = timer_get_uptime_ms();
    ia_estado = IA_PIDIENDO;
    if (!thread_create("paint_ia", ia_hilo, 0)) { ia_estado = IA_LIBRE; ia_slot = -1; return -1; }
    return 0;
}

/* ---------------------------------------------------------------- pintarlo por pasos (hilo de la GUI) */
static void ia_libera_pintura(void)
{
    if (ia_base) kfree(ia_base);
    if (ia_pre) kfree(ia_pre);
    if (ia_dib) kfree(ia_dib);
    ia_base = 0; ia_pre = 0; ia_dib = 0; ia_dib_n = 0;
}
static void ia_acaba(int ok)
{
    ia_libera_pintura();
    ia_estado = IA_LIBRE; ia_slot = -1; ia_resultado = ok ? 1 : -1;
    if (ok) say("Dibujo de la IA listo: retocalo a tu gusto (Ctrl+Z lo quita)", 1);
}
static void ia_empieza(void)
{
    char *svg = ia_svg; uint32_t n = ia_svg_n; ia_svg = 0; ia_svg_n = 0;
    int blanco = !p_dirty && u_count == 0;
    if (blanco && cv) for (int i = 0; i < cw * ch; i++) if (cv[i] != 0x00FFFFFF) { blanco = 0; break; }
    undo_push();                                                  /* todo el dibujo, un solo paso de deshacer */
    if (blanco && (cw != 640 || ch != 480)) { canvas_alloc(640, 480); zoom = 0; pan_x = pan_y = 0; }   /* lienzo sin tocar: al tamano del dibujo */
    ia_base = cv ? (uint32_t *)kmalloc((size_t)cw * ch * 4) : 0;
    ia_pre = (char *)kmalloc(n + 16);
    if (!cv || !ia_base || !ia_pre || !svg) { if (svg) kfree(svg); ia_libera_pintura(); s_cpy(ia_err, "sin memoria para el dibujo", sizeof ia_err); ia_estado = IA_ERROR; return; }
    memcpy(ia_base, cv, (size_t)cw * ch * 4); ia_bw = cw; ia_bh = ch;
    ia_dib = svg; ia_dib_n = n;
    ia_n = ps_trozos(svg, n, ia_fin, 256);
    ia_pasos = ia_n ? ps_pasos(ia_n) : 1; ia_paso = 0;
    ia_t_sig = 0; ia_estado = IA_PINTANDO; p_dirty = 1;
}
static void ia_paso_sig(uint64_t now)
{
    if (now < ia_t_sig) return;
    if (!cv || cw != ia_bw || ch != ia_bh) { ia_acaba(0); return; }     /* el lienzo cambio de tamano */
    ia_paso++;
    struct wimg im; memset(&im, 0, sizeof im);
    int r;
    if (ia_n) {
        uint32_t L = ps_prefijo(ia_dib, ia_fin[ps_corte(ia_n, ia_pasos, ia_paso) - 1], ia_pre, ia_dib_n + 16);
        r = L ? wi_decode_svg((const uint8_t *)ia_pre, L, &im, cw, ch, 0) : -1;
    } else r = wi_decode_svg((const uint8_t *)ia_dib, ia_dib_n, &im, cw, ch, 0);
    if (r == 0 && im.px && im.w == cw && im.h == ch) ps_mezcla(cv, ia_base, im.px, (uint32_t)(cw * ch));
    wi_free(&im);
    ia_t_sig = timer_get_uptime_ms() + 110;
    if (ia_paso >= ia_pasos) ia_acaba(1);
}
static void ia_al_pintar(uint64_t now)
{
    if (ia_slot != pt_live) return;
    if (ia_estado == IA_LISTO) ia_empieza();
    if (ia_estado == IA_PINTANDO) ia_paso_sig(now);
    if (ia_estado == IA_ERROR) {
        char m[200] = "La IA no ha podido dibujarlo: "; s_cat(m, ia_err, sizeof m); say(m, 0);
        ia_estado = IA_LIBRE; ia_slot = -1; ia_resultado = -1;
    }
}
static void ia_aborta(void)
{
    if (ia_estado != IA_PINTANDO || ia_slot != pt_live) return;
    ia_libera_pintura(); ia_estado = IA_LIBRE; ia_slot = -1; ia_resultado = -1; s_cpy(ia_err, "se cambio el lienzo mientras se dibujaba", sizeof ia_err);
}
static void ia_suelta(int slot)
{
    if (ia_slot != slot) return;
    if (ia_estado == IA_PIDIENDO) { ia_huerfano = 1; return; }    /* el hilo lo libera al volver */
    if (ia_estado == IA_LISTO) { char *s = ia_svg; ia_svg = 0; if (s) kfree(s); }
    if (ia_estado == IA_PINTANDO) ia_libera_pintura();
    ia_estado = IA_LIBRE; ia_slot = -1; ia_resultado = -1; s_cpy(ia_err, "se cerro la ventana de Paint", sizeof ia_err);
}
/* 1 = esperando al modelo, 2 = pintando (en ESTA ventana); 0 = nada aqui */
static int ia_ocupado_aqui(void)
{
    if (ia_slot != pt_live) return 0;
    return ia_estado == IA_PIDIENDO ? 1 : (ia_estado == IA_LISTO || ia_estado == IA_PINTANDO) ? 2 : 0;
}

/* ---------------------------------------------------------------- interfaz */
static void ia_abre_campo(void)
{
    if (ia_estado != IA_LIBRE) { say(ia_slot == pt_live ? "La IA ya esta con un dibujo" : "La IA esta dibujando en otra ventana de Paint", 0); return; }
    ia_abierto = 1;
}
static void ia_boton(int x, int y, int hov)
{
    int on = ia_abierto || ia_ocupado_aqui();
    fb_fill_rounded_rect(x, y, BTN, BTN, 8, on ? KC_PRIMARY : hov ? 0x00222B3D : 0x00182133);
    /* estrella de cuatro puntas pequena y "IA" */
    int cx = x + 9, cy = y + 11; uint32_t c = 0xFFFDE68Au;
    fb_line_aa((cx - 4) * 16, cy * 16, (cx + 4) * 16, cy * 16, 18, c); fb_line_aa(cx * 16, (cy - 4) * 16, cx * 16, (cy + 4) * 16, 18, c);
    tx(x + 11, y + 6, BTN - 6, "IA", 12, on || hov ? 0x00FFFFFF : KC_TXT, 1);
}
static void ia_campo(int x, int y, int w, uint64_t now)
{
    fb_fill_rounded_rect(x, y, w, BTN, BTN / 2, KC_FIELD);
    fb_stroke_rrect_aa(x, y, w, BTN, BTN / 2, 0xFF000000u | KC_ACC);
    fb_set_clip(x + 14, y, w - 28, BTN);
    if (ia_len) {
        int tw_ = tw(ia_txt, 13, 0), off = tw_ > w - 40 ? tw_ - (w - 40) : 0;
        tx(x + 16 - off, y, BTN, ia_txt, 13, KC_TXT, 0);
        if ((now / 500) & 1) fb_fill_rect((uint32_t)(x + 17 + tw_ - off), (uint32_t)(y + 8), 2, BTN - 16, KC_ACC);
    } else {
        tx(x + 16, y, BTN, "Que dibujo? (p. ej. un faro al atardecer)  Enter: dibujar   Esc: cerrar", 12, KC_FAINT, 0);
        if ((now / 500) & 1) fb_fill_rect((uint32_t)(x + 15), (uint32_t)(y + 8), 2, BTN - 16, KC_ACC);
    }
    fb_reset_clip();
}
static const char *ia_estado_txt(uint64_t now)
{
    static char b[200];
    int o = ia_ocupado_aqui(); if (!o) return 0;
    b[0] = 0;
    if (o == 1) {
        s_cat(b, "La IA esta pensando el dibujo de ", sizeof b); s_cat(b, ia_tema, sizeof b);
        uint32_t sg = (uint32_t)((now - ia_t0) / 1000); s_cat(b, "  (", sizeof b); s_num(b, sizeof b, (int)sg); s_cat(b, " s)", sizeof b);
        int k = (int)((now / 400) % 4); for (int i = 0; i < k; i++) s_cat(b, ".", sizeof b);
    } else { s_cat(b, "Dibujando ", sizeof b); s_cat(b, ia_tema, sizeof b); s_cat(b, "...  ", sizeof b); s_num(b, sizeof b, ia_paso); s_cat(b, " / ", sizeof b); s_num(b, sizeof b, ia_pasos); }
    return b;
}
static void ia_tecla(char c)
{
    unsigned char u = (unsigned char)c;
    if (u == 27) { ia_abierto = 0; return; }
    if (u == 8 || u == 127) { if (ia_len > 0) { ia_len--; while (ia_len > 0 && ((unsigned char)ia_txt[ia_len] & 0xC0) == 0x80) ia_len--; ia_txt[ia_len] = 0; } return; }
    if (u == 10 || u == 13) {
        int k = 0; while (ia_txt[k] == ' ') k++;
        if (!ia_txt[k]) { say("Escribe que quieres que dibuje", 0); return; }
        if (ia_lanza(ia_txt + k) != 0) { say("La IA ya esta con otro dibujo", 0); return; }
        ia_abierto = 0; ia_len = 0; ia_txt[0] = 0;
        return;
    }
    if (u >= 32 && ia_len + 1 < (int)sizeof ia_txt) { ia_txt[ia_len++] = c; ia_txt[ia_len] = 0; }
}

/* ---------------------------------------------------------------- para la GUI y el agente */
int paint_ia_slot(void) { return ia_estado == IA_LIBRE ? -1 : ia_slot; }
int paint_animando(void)
{
    if (ia_slot != pt_live && !ia_abierto) return 0;
    uint64_t now = timer_get_uptime_ms();
    if (ia_slot == pt_live) {
        if (ia_estado == IA_LISTO || ia_estado == IA_ERROR) return 1;
        if (ia_estado == IA_PINTANDO) return now >= ia_t_sig;
    }
    if ((ia_slot == pt_live && ia_estado == IA_PIDIENDO) || ia_abierto) { static uint64_t u; if (now - u >= 400) { u = now; return 1; } }
    return 0;
}
int paint_ia_encargar(const char *pedido) { paint_ensure(); return ia_lanza(pedido && *pedido ? pedido : "un dibujo bonito"); }
int paint_ia_estado(char *msg, uint32_t cap)
{
    if (msg && cap) { msg[0] = 0; s_cpy(msg, ia_estado == IA_LIBRE && ia_resultado == 1 ? ia_tema : ia_err, (int)cap); }
    if (ia_estado == IA_PIDIENDO) return 0;
    if (ia_estado == IA_LISTO || ia_estado == IA_PINTANDO) return 2;
    if (ia_estado == IA_ERROR) return -1;
    return ia_resultado;                                           /* 1 dibujado, -1 fallo, 0 sin encargo */
}
