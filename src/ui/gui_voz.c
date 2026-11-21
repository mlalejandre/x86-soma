/* gui_voz.c: SOMA_245_PTT: dictado por voz para todo el sistema, "pulsar para hablar" con la tecla Alt izquierda.
 * SOMA_234_GUI_PARTIDO: NO se compila por separado; lo incluye src/ui/gui.c (comparte el estado del escritorio).
 *
 *   Mantener Alt pulsada   -> se graba el microfono (16 kHz, lo que usa whisper) mientras se habla
 *   Soltar Alt             -> se transcribe en segundo plano (voz_run, whisper.cpp) y el texto ENTRA POR EL TECLADO:
 *                             llega a quien tenga el foco (orbe, terminal, notas, el editor de programas, cualquier campo)
 *                             exactamente como si se hubiera escrito a mano. En el orbe ademas se envia (Intro), como en 244.
 *   Alt + otra tecla       -> era un atajo: la grabacion se descarta sin decir nada y la tecla funciona como siempre
 *   Toque corto de Alt     -> sigue abriendo el orbe (como la tecla Windows), igual que antes del dictado
 *   Esc mientras se graba  -> cancela
 * El aviso ("Escuchando...", "Transcribiendo...") es una pastilla global que fb_present dibuja encima de todo
 * (voz_ptt_overlay_draw). El audio queda en /voz/dictado.wav del almacen (se sobrescribe cada vez). Solo x86_64 (AVX2). */
#include "bigfs.h"
#include "bigfs_glue.h"
#define HER_VOZ_PATH "/voz/dictado.wav"
#define HER_VOZ_TAP_MS   350u        /* soltar antes de esto (sin otra tecla) = toque: abre el orbe */
#define HER_VOZ_MAX_MS   120000u     /* 2 min como maximo por dictado */
static volatile int her_voz_state;               /* 0 nada, 1 grabando, 2 transcribiendo, 3 texto listo para escribir */
static uint64_t her_voz_t0;
static char her_voz_msg[120];
static int her_voz_shown_kind;                   /* que se dibujo la ultima vez (si cambia, se repinta todo) */

#if defined(__x86_64__)
static uint64_t her_voz_msg_at, her_voz_last_present;
static void her_voz_say(const char *m)
{
    uint32_t i = 0; while (m && m[i] && i + 1 < sizeof her_voz_msg) { her_voz_msg[i] = m[i]; i++; } her_voz_msg[i] = 0;
    her_voz_msg_at = timer_get_uptime_ms();
}
static char her_voz_out[8192], her_voz_text[4096];   /* SOMA_249_REPES: antes 256 bytes (el dictado largo se cortaba) */
static int her_voz_press_live;                   /* esta pulsacion de Alt sigue en curso (grabando o esperando a ver) */
static uint32_t her_voz_seq_seen;
static uint64_t her_voz_press_at;
static const char *her_voz_block;                /* por que no se puede grabar en esta pulsacion (se dice si se mantiene) */

static void her_voz_thread(void *arg)
{
    (void)arg;
    int64_t code = 0;
    int r = voz_run(HER_VOZ_PATH, her_voz_out, sizeof her_voz_out, &code);
    uint32_t tp = 0; her_voz_text[0] = 0;
    char info[120]; info[0] = 0;
    for (const char *l = her_voz_out; *l;) {                  /* "[voz] ..." es informacion; el resto, el texto */
        const char *e = l; while (*e && *e != '\n') e++;
        int inf = (e - l) >= 5 && l[0] == '[' && l[1] == 'v' && l[2] == 'o' && l[3] == 'z' && l[4] == ']';
        if (inf) {
            const char *q = l + 5; while (*q == ' ') q++;
            /* la linea de tiempos ("7.0 s de audio en ...") no es un motivo de error: se guarda la primera otra */
            int stat = 0; for (const char *z = q; z + 5 < e; z++) if (z[0] == 'a' && z[1] == 'u' && z[2] == 'd' && z[3] == 'i' && z[4] == 'o' && z[5] == ' ') { stat = 1; break; }
            if (!stat && !info[0]) { uint32_t k = 0; while (q < e && k + 1 < sizeof info) info[k++] = *q++; info[k] = 0; }
        } else if (e > l) {
            if (tp && tp + 1 < sizeof her_voz_text) her_voz_text[tp++] = ' ';
            while (l < e && tp + 1 < sizeof her_voz_text) her_voz_text[tp++] = *l++;
            her_voz_text[tp] = 0;
        }
        l = *e ? e + 1 : e;
    }
    while (tp && her_voz_text[tp - 1] == ' ') her_voz_text[--tp] = 0;
    kprint("[dictado] "); kprint(r == 0 && her_voz_text[0] ? "texto listo" : info[0] ? info : "sin texto"); kprint("\n");
    if (r == 0 && her_voz_text[0]) her_voz_state = 3;
    else { her_voz_say(info[0] ? info : "No se entendio nada: mantén Alt mientras hablas"); her_voz_state = 0; }
}

/* Puede grabarse ahora? 0 = si; si no, el motivo. */
static const char *her_voz_why_not(void)
{
    struct micrec_info in; micrec_info(&in);
    if (her_voz_state == 2 || her_voz_state == 3) return "Espera: todavia estoy transcribiendo el dictado anterior";
    if (in.mode != MR_IDLE) return "El microfono esta ocupado (Grabadora)";
    if (in.unsaved) return "La Grabadora tiene una grabacion sin guardar: guardala o descartala antes de dictar";
    return 0;
}
static void her_voz_wait_idle(void)
{
    for (int i = 0; i < 200; i++) { struct micrec_info in; micrec_info(&in); if (in.mode == MR_IDLE) break; thread_sleep(1); }
}
static void her_voz_discard(void)
{
    micrec_stop(); her_voz_wait_idle(); micrec_clear();
    her_voz_state = 0;
}
static void her_voz_stop_and_transcribe(void)
{
    micrec_stop(); her_voz_wait_idle();
    struct micrec_info in; micrec_info(&in);
    if (in.ms < 400) { micrec_clear(); her_voz_state = 0; her_voz_say("Demasiado corto: mantén Alt pulsada mientras hablas"); return; }
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) { micrec_clear(); her_voz_state = 0; her_voz_say("No hay almacen BIGFS para el audio (big format yes)"); return; }
    uint32_t ino;
    if (bigfs_lookup(fs, "/voz", &ino) != (int)BIGFS_T_DIR) (void)bigfs_mkdir(fs, "/voz");
    if (bigfs_lookup(fs, HER_VOZ_PATH, &ino) >= 0) (void)bigfs_unlink(fs, HER_VOZ_PATH);
    char used[96];
    int r = micrec_save(HER_VOZ_PATH, used, sizeof used);
    micrec_clear();
    if (r != 0) { her_voz_state = 0; her_voz_say("No se pudo guardar el audio del dictado"); return; }
    her_voz_state = 2; her_voz_t0 = timer_get_uptime_ms();
    if (!thread_create("voz_dictado", her_voz_thread, 0)) { her_voz_state = 0; her_voz_say("No se pudo transcribir (sin hilos)"); }
}

/* El texto entra por el teclado: lo recibe quien tenga el foco, como si se hubiera escrito. */
static void her_voz_type_text(void)
{
    int orb = current_app == APP_DESKTOP && agent_global_focus && !her_icons_visible;
    uint32_t n = 0; while (her_voz_text[n]) n++;
    if (orb) while (n && her_voz_text[n - 1] == '.') her_voz_text[--n] = 0;     /* al orbe, sin el punto final (como 244) */
    uint32_t put = console_inject_text(her_voz_text);
    if (put < n) her_voz_say("El dictado era demasiado largo: se escribio solo una parte");
    else if (orb && n) (void)console_inject_text("\n");                         /* el orbe lo envia a SOMA */
}
#endif

/* En cada vuelta del bucle del escritorio. 0 = nada; 1 = la pastilla cambio (basta fb_present); 2 = hay que repintar todo. */
static int her_voz_tick(void)
{
#if defined(__x86_64__)
    uint64_t now = timer_get_uptime_ms();
    struct micrec_info mi; micrec_info(&mi);
    int avail = usermode_avx2_ok() && mi.available;
    console_set_alt_voice(avail);                 /* sin dictado posible, Alt vuelve a ser solo "abrir el orbe" */
    int down = 0, other = 0; uint32_t seq = 0;
    console_ptt_state(&down, &other, &seq);
    if (avail && seq != her_voz_seq_seen) {       /* nueva pulsacion de Alt */
        her_voz_seq_seen = seq;
        her_voz_press_live = 1; her_voz_press_at = now;
        her_voz_block = her_voz_why_not();
        if (voz_speak_state()) voz_speak_stop();       /* SOMA_250_TTS: Alt calla al orbe (y el microfono no se oye a si mismo) */
        if (!her_voz_block) {
            if (micrec_record(16000) == 0) { her_voz_state = 1; her_voz_t0 = now; her_voz_msg[0] = 0; }
            else her_voz_block = "No se pudo empezar a grabar";
        }
    }
    if (her_voz_press_live) {
        uint64_t held = now - her_voz_press_at;
        if (other) {                              /* Alt + tecla: era un atajo, no un dictado */
            her_voz_press_live = 0;
            if (her_voz_state == 1) her_voz_discard();
        } else if (!down) {                       /* Alt soltada */
            her_voz_press_live = 0;
            if (held < HER_VOZ_TAP_MS) {          /* toque corto: el gesto de siempre (abrir el orbe) */
                if (her_voz_state == 1) her_voz_discard();
                console_post_super();
            } else if (her_voz_state == 1) her_voz_stop_and_transcribe();
        } else if (her_voz_block && held >= 400u) {   /* se mantiene pero no se puede grabar: decirlo una vez */
            her_voz_say(her_voz_block); her_voz_block = 0;
        } else if (her_voz_state == 1 && now - her_voz_t0 > HER_VOZ_MAX_MS) {
            her_voz_press_live = 0; her_voz_stop_and_transcribe();
            her_voz_say("Maximo 2 minutos por dictado: lo grabado se esta transcribiendo");
        }
    }
    if (her_voz_state == 3) { her_voz_state = 0; her_voz_type_text(); }
    {   /* SOMA_247_HABLA: la lectura en voz alta empezo o termino: repintar (el boton de Notas cambia) y avisar si fallo */
        static uint32_t sp_seen;
        uint32_t v = voz_speak_ver();
        if (v != sp_seen) {
            sp_seen = v;
            if (!voz_speak_state() && voz_speak_msg()[0]) clip_toast_ms(voz_speak_msg(), 7000);
            return 2;
        }
    }
    now = timer_get_uptime_ms();                  /* lo de arriba puede haber tardado (guardar el audio) y fijado un aviso */
    if (her_voz_msg[0] && now - her_voz_msg_at > 5000u) her_voz_msg[0] = 0;

    /* que se ve: 1 grabando (solo pasado el toque, para no parpadear al abrir el orbe), 2 transcribiendo, 3 aviso */
    int kind = (her_voz_state == 1 && (!her_voz_press_live || now - her_voz_press_at >= HER_VOZ_TAP_MS)) ? 1
             : her_voz_state == 2 ? 2 : her_voz_msg[0] ? 3 : 0;
    if (kind != her_voz_shown_kind) {
        int was = her_voz_shown_kind; her_voz_shown_kind = kind; her_voz_last_present = now;
        return was ? 2 : 1;                       /* cambia el tamano o desaparece: repintar lo que hay debajo */
    }
    if ((kind == 1 || kind == 2) && now - her_voz_last_present >= 120u) { her_voz_last_present = now; return 1; }
    return 0;
#else
    return 0;
#endif
}
/* Esc mientras graba: cancela. 1 = consumido. */
static int her_voz_escape(void)
{
#if defined(__x86_64__)
    if (her_voz_state == 1) { her_voz_press_live = 0; her_voz_discard(); her_voz_say("Dictado cancelado"); return 1; }
#endif
    return 0;
}

/* Pastilla global (la llama fb_present en cada volcado, encima de cualquier ventana). */
void voz_ptt_overlay_draw(void)
{
    int kind = her_voz_shown_kind;
    if (!kind) return;
    uint32_t sw = fb_get_width(), sh = fb_get_height();
    if (sw < 200u || sh < 200u) return;
    uint64_t now = timer_get_uptime_ms();
    char line[128]; uint32_t p = 0;
    const char *a = kind == 1 ? "Escuchando  (suelta Alt al terminar)  " : kind == 2 ? "Transcribiendo...  " : her_voz_msg;
    int inst = -1;
#if defined(__x86_64__)
    if (kind == 2) { inst = voz_install_progress(); if (inst >= 0) a = "Instalando el modelo de voz...  "; }   /* SOMA_246: 1a vez con otro modelo */
#endif
    while (*a && p + 12 < sizeof line) line[p++] = *a++;
    if (inst >= 0) { if (inst >= 10) line[p++] = (char)('0' + inst / 10 % 10); line[p++] = (char)('0' + inst % 10); line[p++] = ' '; line[p++] = '%'; }
    else if (kind == 1 || kind == 2) {
        uint32_t s = (uint32_t)((now - her_voz_t0) / 1000u);
        if (s >= 60) { line[p++] = (char)('0' + s / 60 % 10); line[p++] = ':'; s %= 60; line[p++] = (char)('0' + s / 10); }
        else if (s >= 10) line[p++] = (char)('0' + s / 10);
        line[p++] = (char)('0' + s % 10); line[p++] = ' '; line[p++] = 's';
    }
    line[p] = 0;
    int bars = 16, bw = 5, gap = 3, barw = kind == 1 ? bars * (bw + gap) + 18 : 0;
    uint32_t tw = (uint32_t)font_text_width(kind == 3 ? line : "Escuchando  (suelta Alt al terminar)  0:00 s", 1);   /* ancho fijo mientras dura */
    int32_t pw = (int32_t)tw + 60 + barw, ph = 40;
    if (pw > (int32_t)sw - 20) pw = (int32_t)sw - 20;
    int32_t px = ((int32_t)sw - pw) / 2, py = (int32_t)sh - 160;
    uint32_t edge = kind == 1 ? 0x00FB7185u : kind == 2 ? 0x0093C5FDu : 0x00FBBF24u;
    fb_fill_rounded_rect(px, py, pw, ph, 20, 0x00111827u);
    fb_draw_rounded_rect(px, py, pw, ph, 20, edge);
    if (kind != 3) {
        uint32_t pulse = (uint32_t)(her_sn((uint32_t)((now % 900u) * 1024u / 900u)) + 1024) * 256u / 2048u;
        uint32_t dot = kind == 1 ? her_mix256(0x007F1D1Du, 0x00FB7185u, pulse) : her_mix256(0x001E3A8Au, 0x0093C5FDu, pulse);
        fb_fill_rrect_aa(px + 18, py + 13, 14, 14, 7, dot);
    }
    font_draw_string((uint32_t)(px + (kind == 3 ? 24 : 42)), (uint32_t)(py + 16), line, kind == 1 ? 0x00FECDD3u : kind == 2 ? 0x00BFDBFEu : 0x00FDE68Au, 1);
    if (kind == 1) {                                          /* nivel del microfono */
        struct micrec_info in; micrec_info(&in);
        int lit = in.level * bars / 100, x0 = px + pw - barw;
        for (int i = 0; i < bars; i++) {
            int h = 4 + (i < lit ? 4 + i : 0); if (h > 22) h = 22;
            fb_fill_rrect_aa(x0 + i * (bw + gap), py + ph / 2 - h / 2, bw, h, 2, i < lit ? 0x00FB7185u : 0x00334155u);
        }
    }
    fb_damage(px, py, pw + 1, ph + 1);
}
