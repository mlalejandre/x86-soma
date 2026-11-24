/* gui_vortice.c: matematicas del vortice y estado del orbe (pensamiento, texto que absorbe).
 * SOMA_234_GUI_PARTIDO: NO se compila por separado. Lo incluye src/ui/gui.c en su sitio (una sola unidad de
 * compilacion: comparte el estado del escritorio con gui.c, y el nucleo compilado sale identico). */
/* ====================================================================
 * MATEMÁTICA Y ESTRUCTURAS DEL VÓRTICE CÓSMICO (RING 0 / FIXED-POINT)
 * ==================================================================== */
static uint32_t her_isqrt(uint32_t val)
{
    uint32_t res = 0;
    uint32_t bit = 1u << 30;
    while (bit > val) bit >>= 2;
    while (bit != 0) {
        if (val >= res + bit) {
            val -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

#define HER_TORUS_PART  160  /* Rosquilla de partículas en acreción */
#define HER_STARS_PART  48   /* Estrellas cósmicas en toda la interfaz */
#define HER_MAX_PART    (HER_TORUS_PART + HER_STARS_PART)

struct her_particle {
    uint16_t angle;          /* Ángulo polar 0..1023 (1024 = 360°) */
    int32_t  r16;            /* Radio polar fixed-point (r << 8) */
    int32_t  vr16;           /* Velocidad radial hacia el centro */
    int16_t  omega;          /* Velocidad angular base */
    int32_t  prev_x, prev_y; /* Estela previa */
    uint8_t  size;
    uint32_t color;
};

struct her_cosmic_star {
    int32_t  x, y;           /* Posición fija 16.8 en pantalla */
    int32_t  vx, vy;         /* Velocidad fija 16.8 */
    int32_t  prev_x, prev_y;
    uint32_t color;
};

static struct her_particle     her_torus[HER_TORUS_PART];
static struct her_cosmic_star   her_stars[HER_STARS_PART];
static int her_cosmic_ready = 0;
static int soma_vortex_boost = 0;

/* Tabla seno 8-bit */
static const int8_t HER_SIN_TAB[64] = {
      0,  12,  25,  37,  49,  60,  71,  81,
     90,  98, 105, 111, 116, 120, 123, 125,
    127, 125, 123, 120, 116, 111, 105,  98,
     90,  81,  71,  60,  49,  37,  25,  12,
      0, -12, -25, -37, -49, -60, -71, -81,
    -90, -98,-105,-111,-116,-120,-123,-125,
   -127,-125,-123,-120,-116,-111,-105, -98,
    -90, -81, -71, -60, -49, -37, -25, -12
};
static int __attribute__((unused)) her_isin(uint32_t a) { return (int)HER_SIN_TAB[(a >> 2) & 63]; }
static int __attribute__((unused)) her_icos(uint32_t a) { return (int)HER_SIN_TAB[((a + 64) >> 2) & 63]; }

static uint32_t __attribute__((unused)) her_rainbow(uint32_t t)
{
    static const uint32_t pal[8] = {
        0x00FF6B6B, 0x00FFA94D, 0x00FFE66D, 0x006BCB77,
        0x004D96FF, 0x009B5DE5, 0x00F15BB5, 0x00FF6B6B
    };
    return pal[(t >> 3) & 7];
}

static uint32_t her_rng = 0xA53C9E7Bu;
static uint32_t her_rand(void)
{
    her_rng = her_rng * 1664525u + 1013904223u;
    return her_rng;
}

/* ====================================================================
 * SOMA HER-AURA v4 (ESTADO, ARMONICOS Y TEXTO)
 * ==================================================================== */
static volatile int her_agent_busy;

enum { HER_ST_IDLE = 0, HER_ST_LISTEN, HER_ST_THINK, HER_ST_SPEAK };

#define HER_RING_N   48
#define HER_ANS_MAX  1536
#define HER_THK_MAX  640

static const int16_t HER_S[256] = {
    0, 25, 50, 75, 100, 125, 150, 175, 200, 224, 249, 273, 297, 321, 345, 369,
    392, 415, 438, 460, 483, 505, 526, 548, 569, 590, 610, 630, 650, 669, 688, 706,
    724, 742, 759, 775, 792, 807, 822, 837, 851, 865, 878, 891, 903, 915, 926, 936,
    946, 955, 964, 972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024,
    1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009, 1004, 999, 993, 987, 980, 972, 964, 955,
    946, 936, 926, 915, 903, 891, 878, 865, 851, 837, 822, 807, 792, 775, 759, 742,
    724, 706, 688, 669, 650, 630, 610, 590, 569, 548, 526, 505, 483, 460, 438, 415,
    392, 369, 345, 321, 297, 273, 249, 224, 200, 175, 150, 125, 100, 75, 50, 25,
    0, -25, -50, -75, -100, -125, -150, -175, -200, -224, -249, -273, -297, -321, -345, -369,
    -392, -415, -438, -460, -483, -505, -526, -548, -569, -590, -610, -630, -650, -669, -688, -706,
    -724, -742, -759, -775, -792, -807, -822, -837, -851, -865, -878, -891, -903, -915, -926, -936,
    -946, -955, -964, -972, -980, -987, -993, -999, -1004, -1009, -1013, -1016, -1019, -1021, -1023, -1024,
    -1024, -1024, -1023, -1021, -1019, -1016, -1013, -1009, -1004, -999, -993, -987, -980, -972, -964, -955,
    -946, -936, -926, -915, -903, -891, -878, -865, -851, -837, -822, -807, -792, -775, -759, -742,
    -724, -706, -688, -669, -650, -630, -610, -590, -569, -548, -526, -505, -483, -460, -438, -415,
    -392, -369, -345, -321, -297, -273, -249, -224, -200, -175, -150, -125, -100, -75, -50, -25,
};

static int her_sn(uint32_t a)
{
    uint32_t i = (a >> 2) & 255u, f = a & 3u;
    int s0 = HER_S[i], s1 = HER_S[(i + 1u) & 255u];
    return s0 + ((s1 - s0) * (int)f) / 4;
}
static int her_cs(uint32_t a) { return her_sn(a + 256u); }

static uint32_t her_mix256(uint32_t c1, uint32_t c2, uint32_t k)
{
    uint32_t r = ((((c1 >> 16) & 255u) * (256u - k)) + (((c2 >> 16) & 255u) * k)) >> 8;
    uint32_t g = ((((c1 >> 8) & 255u) * (256u - k)) + (((c2 >> 8) & 255u) * k)) >> 8;
    uint32_t b = (((c1 & 255u) * (256u - k)) + ((c2 & 255u) * k)) >> 8;
    return (r << 16) | (g << 8) | b;
}
static uint32_t her_mix(uint32_t c1, uint32_t c2, uint32_t k16) { return her_mix256(c1, c2, k16 * 16u); }
static uint32_t her_scale(uint32_t c, uint32_t k)
{
    if (k > 256u) k = 256u;
    return ((((c >> 16) & 255u) * k >> 8) << 16) |
           ((((c >> 8) & 255u) * k >> 8) << 8) |
           (((c & 255u) * k) >> 8);
}
/* SOMA BLACK HOLE REALISM PATCH v1
 * Paleta acromatica fria: acero azulado -> blanco frio, sin arcoiris.
 * La variacion de luminancia es suave y usa la tabla seno ya existente.
 */
static uint32_t her_grad(uint32_t a)
{
    int wave = her_sn(a & 1023u);
    uint32_t k = 112u + (uint32_t)(wave + 1024) * 64u / 2048u;
    return her_mix256(0x00B9CBE0u, 0x00F7FAFFu, k);
}
static int her_ease(int cur, int tgt, uint32_t k)
{
    int d = tgt - cur;
    int s = (d * (int)k) / 256;
    if (s == 0 && d != 0) s = (d > 0) ? 1 : -1;
    return cur + s;
}

static uint16_t *her_ans = 0;
static uint32_t her_ans_len = 0, her_ans_reveal = 0;
static uint16_t *her_thk = 0;
static uint32_t her_thk_len = 0, her_thk_reveal = 0;
static char     her_last_q[SOMA_AGENT_CMD_MAX];
static uint32_t her_energy = 0, her_pulse = 0, her_prev_len = 0;
static uint32_t her_col_cur = 0x007DD3FCu;
static int      her_cur_r16 = 56 * 16, her_breath = 0;
static int      her_cur_amp[4] = { 32, 19, 0, 0 };
static int      her_cur_speed = 14, her_cur_spec = 70, her_cur_glow = 70;
static uint32_t her_phacc[4] = { 0, 0, 0, 0 };
static uint32_t her_grad_acc = 0, her_last_draw = 0;

static uint16_t her_ans_ls[40], her_ans_ll[40];
static uint16_t her_thk_ls[16], her_thk_ll[16];
static int her_bufs_ready = 0;

static void her_bufs_ensure(void)
{
    if (her_bufs_ready) return;
    her_ans = (uint16_t *)kmalloc(HER_ANS_MAX * sizeof(uint16_t));
    her_thk = (uint16_t *)kmalloc(HER_THK_MAX * sizeof(uint16_t));
    if (!her_ans || !her_thk) { her_ans_len = 0; her_thk_len = 0; return; }
    for (uint32_t i = 0; i < HER_ANS_MAX; ++i) her_ans[i] = 0;
    for (uint32_t i = 0; i < HER_THK_MAX; ++i) her_thk[i] = 0;
    her_bufs_ready = 1;
}

static int her_state(void)
{
    if (her_ans_reveal < her_ans_len || her_energy > 24u) return HER_ST_SPEAK;
    if (her_agent_busy) return HER_ST_THINK;
    if (agent_global_len > 0 && agent_global_focus) return HER_ST_LISTEN;
    return HER_ST_IDLE;
}

struct her_u8 { uint32_t cp; int need; };

static uint16_t her_cp_cell(uint32_t cp)
{
    if (cp == '\n') return (uint16_t)'\n';
    if (cp == '\t') return (uint16_t)' ';
    if (cp < 32u || cp == 127u) return 0;
    if (cp < 127u) return (uint16_t)cp;
    switch (cp) {
    case 0xE1: return (uint16_t)('a' | (1u << 8));
    case 0xE9: return (uint16_t)('e' | (1u << 8));
    case 0xED: return (uint16_t)('i' | (1u << 8));
    case 0xF3: return (uint16_t)('o' | (1u << 8));
    case 0xFA: return (uint16_t)('u' | (1u << 8));
    case 0xC1: return (uint16_t)('A' | (1u << 8));
    case 0xC9: return (uint16_t)('E' | (1u << 8));
    case 0xCD: return (uint16_t)('I' | (1u << 8));
    case 0xD3: return (uint16_t)('O' | (1u << 8));
    case 0xDA: return (uint16_t)('U' | (1u << 8));
    case 0xF1: return (uint16_t)('n' | (2u << 8));
    case 0xD1: return (uint16_t)('N' | (2u << 8));
    case 0xFC: return (uint16_t)('u' | (3u << 8));
    case 0xDC: return (uint16_t)('U' | (3u << 8));
    case 0xBF: return (uint16_t)('?' | (4u << 8));
    case 0xA1: return (uint16_t)('!' | (4u << 8));
    default:   return (uint16_t)'?';
    }
}

static int her_u8_feed(struct her_u8 *d, uint8_t b, uint16_t *out)
{
    uint16_t cell;
    if (b < 0x80u) {
        d->need = 0; cell = her_cp_cell(b);
    } else if (b >= 0xF0u) { d->need = 3; d->cp = b & 7u;  return 0; }
    else if (b >= 0xE0u)   { d->need = 2; d->cp = b & 15u; return 0; }
    else if (b >= 0xC0u)   { d->need = 1; d->cp = b & 31u; return 0; }
    else {
        if (d->need <= 0) return 0;
        d->cp = (d->cp << 6) | (b & 63u);
        if (--d->need > 0) return 0;
        cell = her_cp_cell(d->cp);
    }
    if (!cell) return 0;
    *out = cell;
    return 1;
}

static uint32_t her_text_cells_n(const char *s, uint32_t nb, uint16_t *dst, uint32_t max)
{
    struct her_u8 d = { 0, 0 };
    uint32_t n = 0;
    for (uint32_t i = 0; i < nb && s[i] && n < max; ++i) {
        uint16_t cell;
        if (her_u8_feed(&d, (uint8_t)s[i], &cell)) dst[n++] = cell;
    }
    return n;
}

static uint32_t her_text_cells(const char *s, uint16_t *dst, uint32_t max)
{
    uint32_t nb = 0; while (s[nb]) nb++;
    return her_text_cells_n(s, nb, dst, max);
}

static const char *her_find(const char *hay, const char *needle)
{
    for (; *hay; ++hay) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return hay;
    }
    return 0;
}

static void her_draw_cell(int32_t x, int32_t y, uint16_t cell, uint32_t col, int sc)
{
    uint8_t base = (uint8_t)(cell & 0xFFu);
    uint32_t mk = (uint32_t)(cell >> 8);
    /* SOMA_VECTOR_CELL_PATCH: texto del escritorio en vectorial */
    if (font_draw_cell_vector(x, y, base, mk, col, sc)) return;
    const uint8_t *g = font_get_glyph((char)base);
    uint8_t tmp[8];
    for (int r = 0; r < 8; ++r) tmp[r] = (mk == 4u) ? g[7 - r] : g[r];
    if (mk && mk != 4u && mk != 7u && base == 'i') tmp[0] = 0;
    fb_draw_glyph8_nodmg(x, y, tmp, col, sc);
    if (!mk || mk == 4u) return;

    int32_t s = sc;
    int32_t oy = (base >= 'A' && base <= 'Z') ? y - 2 * s : y;
#define HP(px, py) fb_fill_rect((uint32_t)(x + (px) * s), (uint32_t)(py), (uint32_t)s, (uint32_t)s, col)
    switch (mk) {
    case 1: HP(4, oy); HP(3, oy + s); break;
    case 2: HP(2, oy + s); HP(3, oy); HP(4, oy); HP(5, oy + s); break;
    case 3: HP(2, oy + s); HP(5, oy + s); break;
    default: break;
    }
#undef HP
}

static void her_draw_cells(int32_t x, int32_t y, const uint16_t *c, uint32_t len,
                           uint32_t g0, uint32_t reveal,
                           uint32_t base, uint32_t tip, int sc)
{
    int32_t adv = 9 * sc;
    for (uint32_t k = 0; k < len; ++k) {
        uint32_t gi = g0 + k;
        if (gi >= reveal) break;
        uint16_t cell = c[k];
        if ((cell & 0xFFu) == ' ') continue;
        uint32_t d = reveal - gi;
        uint32_t col = base;
        if (d < 14u) col = her_mix256(tip, base, d * 256u / 14u);
        her_draw_cell(x + (int32_t)k * adv, y, cell, col, sc);
    }
}

static void her_draw_label(uint32_t cur_w, uint32_t y, const char *s, uint32_t col)
{
    uint32_t n = 0; while (s[n]) n++;
    uint32_t w = n * 9u;
    uint32_t x = (cur_w > w) ? (cur_w - w) / 2u : 0u;
    for (uint32_t i = 0; i < n; ++i)
        her_draw_cell((int32_t)(x + i * 9u), (int32_t)y, (uint16_t)(uint8_t)s[i], col, 1);
}

static int her_wrap(const uint16_t *c, uint32_t n, uint32_t cols,
                    uint16_t *ls, uint16_t *ll, int maxl)
{
    int cnt = 0; uint32_t i = 0;
    if (cols < 1u) cols = 1u;
    while (i < n && cnt < maxl) {
        uint32_t start = i, j = i, end, next;
        int32_t last_sp = -1;
        while (j < n && (j - start) < cols) {
            uint8_t b = (uint8_t)(c[j] & 0xFFu);
            if (b == '\n') break;
            if (b == ' ') last_sp = (int32_t)j;
            j++;
        }
        if (j >= n)                         { end = n; next = n; }
        else if ((c[j] & 0xFFu) == '\n')    { end = j; next = j + 1u; }
        else if ((c[j] & 0xFFu) == ' ')     { end = j; next = j + 1u; }
        else if (last_sp > (int32_t)start)  { end = (uint32_t)last_sp; next = end + 1u; }
        else                                { end = j; next = j; }
        ls[cnt] = (uint16_t)start;
        ll[cnt] = (uint16_t)(end - start);
        cnt++; i = next;
    }
    return cnt;
}

static int her_ring_r16(uint32_t a)
{
    int r = her_cur_r16 + her_breath;
    r += her_sn(2u * a + (her_phacc[0] >> 10)) * her_cur_amp[0] / 1024;
    r += her_sn(3u * a + (her_phacc[1] >> 10)) * her_cur_amp[1] / 1024;
    r += her_sn(5u * a + (her_phacc[2] >> 10)) * her_cur_amp[2] / 1024;
    r += her_sn(7u * a + (her_phacc[3] >> 10)) * her_cur_amp[3] / 1024;
    return r;
}

static void her_reveal_step(void)
{
    her_bufs_ensure();
    if (!her_ans || !her_thk) return;
    if (her_ans_reveal < her_ans_len) {
        uint32_t step = 1u + her_ans_len / 160u;
        if (step > 6u) step = 6u;
        while (step-- && her_ans_reveal < her_ans_len) {
            uint8_t b = (uint8_t)(her_ans[her_ans_reveal] & 0xFFu);
            if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9'))
                her_energy += 40u;
            her_ans_reveal++;
        }
        if (her_energy > 255u) her_energy = 255u;
    }
    her_energy = (her_energy * 192u) >> 8;
    if (her_thk_reveal < her_thk_len) {
        her_thk_reveal += 3u + her_thk_len / 120u;
        if (her_thk_reveal > her_thk_len) her_thk_reveal = her_thk_len;
    }
}

/* SOMA_259_ORBE: src/ui/gui_prompter.c (gui.c lo incluye despues de este archivo) */
static void her_pr_turno(void); static void her_pr_fin(const char *resp); static void her_pr_pensamiento(const char *t);
static void her_pr_stream_ini(void); static void her_pr_stream(const char *c);
static void her_pr_aviso(const char *t);               /* SOMA_266_AVISO */
static int her_aviso_on;                               /* SOMA_266_AVISO: el texto del orbe es el aviso, no la respuesta */
/* SOMA_266_AVISO: el aviso del LCM ("voy a... suele tardar...") se ve en el orbe hasta que llega la respuesta. Solo con la pantalla
 * cedida al hilo del agente (gui_aviso) o desde el hilo de la GUI. */
static void her_aviso(const char *t)
{
    her_bufs_ensure();
    if (!her_ans || !t) return;
    her_ans_reveal = 0;
    her_ans_len = her_text_cells(t, her_ans, HER_ANS_MAX);
    her_aviso_on = 1;
    her_pr_aviso(t);
}
static void her_begin_turn(const char *q)
{
    her_bufs_ensure();
    uint32_t i = 0; while (q && q[i] && i < sizeof(her_last_q) - 1u) { her_last_q[i] = q[i]; i++; }
    her_last_q[i] = '\0';
    her_ans_len = 0; her_ans_reveal = 0;
    her_thk_len = 0; her_thk_reveal = 0;
    her_energy = 0;
#if defined(__x86_64__)
    if (voz_speak_state()) voz_speak_stop();           /* SOMA_250_TTS: pregunta nueva: se deja de leer la respuesta anterior */
#endif
    her_text_suppressed_by_dock = 0;
    her_pr_turno();                                    /* SOMA_259_ORBE */
}

static void her_finish_turn(const char *resp)
{
    her_bufs_ensure();
    if (her_aviso_on) { her_ans_len = 0; her_aviso_on = 0; }         /* SOMA_266_AVISO: la respuesta sustituye al aviso */
    static const char tag_r[] = "[Razonamiento]:\n";
    static const char tag_d[] = "\n\n[Dictamen]:\n";
    if (!resp) resp = "";
    const char *ans = resp;
    const char *d = her_find(resp, tag_d);
    uint32_t i = 0; while (tag_r[i] && resp[i] == tag_r[i]) i++;
    if (tag_r[i] == '\0' && d) {
        const char *ts = resp + (sizeof(tag_r) - 1u);
        if (her_thk_len == 0 && d > ts) {
            her_thk_reveal = 0;
            her_thk_len = her_text_cells_n(ts, (uint32_t)(d - ts), her_thk, HER_THK_MAX);
        }
        ans = d + (sizeof(tag_d) - 1u);
    }
    if (her_ans_len == 0) {
        her_ans_reveal = 0;
        her_ans_len = her_text_cells(*ans ? ans : "Mision ejecutada en hardware.",
                                     her_ans, HER_ANS_MAX);
    }
#if defined(__x86_64__)
    {   /* SOMA_250_TTS: el orbe lee su respuesta (Ajustes > Voz). SOMA_290_VOZ_CODIGO: si el agente dejo un texto para decir (el resumen
         * hablado del programador), se dice ese en vez de leer el informe */
        extern const char *agent_voz_toma(void);
        const char *vz = agent_voz_toma();
        if (*vz) (void)voz_speak_reply(vz); else if (*ans) (void)voz_speak_reply(ans);
    }
#endif
    her_pr_fin(resp);                                  /* SOMA_259_ORBE */
}

void gui_thought_update(const char *txt)
{
    coder_app_notify_thought(txt);
    her_bufs_ensure();
    if (!txt || !her_agent_busy) return;
    her_thk_reveal = 0; her_thk_len = 0;
    her_thk_len = her_text_cells(txt, her_thk, HER_THK_MAX);
    her_pr_pensamiento(txt);                           /* SOMA_259_ORBE */
}

static struct her_u8 her_stream_dec = { 0, 0 };

void gui_stream_begin(void)
{
    her_bufs_ensure();
    her_ans_len = 0; her_ans_reveal = 0; her_aviso_on = 0;           /* SOMA_266_AVISO */
    her_stream_dec.cp = 0; her_stream_dec.need = 0;
    her_pr_stream_ini();                               /* SOMA_259_ORBE */
}

void gui_stream_push(const char *chunk)
{
    if (!chunk) return;
    her_pr_stream(chunk);                              /* SOMA_259_ORBE */
    for (uint32_t i = 0; chunk[i] && her_ans_len < HER_ANS_MAX; ++i) {
        uint16_t cell;
        if (her_u8_feed(&her_stream_dec, (uint8_t)chunk[i], &cell))
            her_ans[her_ans_len++] = cell;
    }
}
