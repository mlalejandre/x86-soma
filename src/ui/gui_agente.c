/* gui_agente.c: el agente en su propio hilo y los turnos de pantalla.
 * SOMA_234_GUI_PARTIDO: NO se compila por separado. Lo incluye src/ui/gui.c en su sitio (una sola unidad de
 * compilacion: comparte el estado del escritorio con gui.c, y el nucleo compilado sale identico). */
/* ====================================================================
 * PARCHE 091: la mision del agente corre en su propio hilo ("soma_agente") y la GUI sigue viva: se pueden mover y
 * redimensionar ventanas, usar otras aplicaciones y escribir mientras SOMA piensa.
 * Cuando el agente necesita la pantalla (abre una ventana, escribe un archivo "en vivo", ejecuta un programa grafico)
 * pide turno con gui_enter(): el bucle principal se detiene en un punto seguro (al principio de la vuelta), le cede la
 * pantalla y, cuando el agente la devuelve con gui_leave(), vuelve a leer la geometria y repinta todo.
 * ==================================================================== */
static volatile int g_agent_thr_on;        /* hay una mision en marcha en el hilo del agente */
static volatile int g_agent_thr_done;      /* la mision termino: el bucle principal cierra el turno */
static volatile uint32_t g_agent_tid;
static volatile int g_gui_loop_on;         /* el bucle principal de la GUI esta corriendo */
static volatile int g_gui_req, g_gui_grant; static int g_gui_depth;
static char g_agent_mission[SOMA_AGENT_CMD_MAX];
static char *g_chat_mis; static const char *g_agent_mis_ptr;   /* SOMA_285_CHAT: la mision del chat (sin el limite de 256 del orbe) */
static int gui_in_agent_thread(void) { struct tcb *t = thread_current(); return g_agent_thr_on && t && t->tid == g_agent_tid; }
int gui_enter(void)
{
    if (!gui_in_agent_thread() || !g_gui_loop_on) return 0;
    if (g_gui_depth++ > 0) return 1;
    g_gui_req = 1;
    for (uint32_t i = 0; !g_gui_grant && g_gui_loop_on && i < 60000u; i++) thread_sleep(1);
    return 1;
}
void gui_leave(int tok)
{
    if (!tok) return;
    if (--g_gui_depth > 0) return;
    g_gui_req = 0;
    while (g_gui_grant && g_gui_loop_on && g_agent_thr_on) thread_sleep(1);   /* SOMA_248_DOC: que el bucle lo vea antes de volver a pedir */
}
/* SOMA_248_DOC: turno de pantalla para CUALQUIER hilo (el del agente, ordenes lentas del terminal, trabajos de fondo). Lo usan quienes
 * dibujan fuera de la pantalla con fb_target_begin (motor de documentos), que cambia el destino de todo el dibujo. En el hilo de la GUI
 * o sin GUI no hace nada. gui_enter() sigue siendo solo para el hilo del agente (no frena a los demas hilos que escriben archivos). */
static volatile uint32_t g_turn_gui_tid, g_turn_tid; static volatile int g_turn_req, g_turn_grant; static int g_turn_depth;
int gui_turn_begin(void)
{
    if (!g_gui_loop_on) return 0;
    if (gui_in_agent_thread()) return gui_enter();
    struct tcb *t = thread_current();
    if (!t || t->tid == g_turn_gui_tid) return 0;
    if (g_turn_tid == t->tid) { g_turn_depth++; return 2; }
    while (!__sync_bool_compare_and_swap(&g_turn_tid, 0u, t->tid)) { if (!g_gui_loop_on) return 0; thread_sleep(1); }
    g_turn_depth = 1; g_turn_req = 1;
    for (uint32_t i = 0; !g_turn_grant && g_gui_loop_on && i < 60000u; i++) thread_sleep(1);
    return 2;
}
void gui_turn_end(int tok)
{
    if (tok != 2) { gui_leave(tok); return; }
    if (--g_turn_depth > 0) return;
    g_turn_req = 0;
    while (g_turn_grant && g_gui_loop_on) thread_sleep(1);
    g_turn_tid = 0;
}
static void gui_turn_service(void)              /* bucle principal, punto seguro */
{
    if (!g_turn_req) return;
    g_turn_grant = 1;
    while (g_turn_req && g_gui_loop_on) thread_sleep(1);
    g_turn_grant = 0;
}
static void agent_thread_main(void *arg)
{
    (void)arg;
    agent_run("soma", g_agent_mis_ptr ? g_agent_mis_ptr : g_agent_mission);
    g_agent_thr_done = 1;
}
/* cierre del turno (lo hace el bucle principal) */
static void agent_turn_finish(void)
{
    const uint32_t wrap = 76;
    her_agent_busy = 0;
    agentes_reg_fin(agent_last_response);                       /* SOMA_338_AGENTES: si se detuvo, agent_run no llego a cerrarla */
    (void)chat_agente_fin(agent_last_response);                 /* SOMA_285_CHAT: si el turno era del chat, la respuesta va a su conversacion */
    g_agent_mis_ptr = 0;
    her_finish_turn(agent_last_response);
    if (agent_last_response[0] != '\0')
        log_add(agent_log, &agent_log_count, "[soma]: ", agent_last_response, 0x0034D399, wrap);
    else
        log_add(agent_log, &agent_log_count, "[soma]: ", "Mision ejecutada en hardware.", 0x0034D399, wrap);
    agent_global_scroll = 0;
    g_agent_thr_on = 0; g_agent_thr_done = 0; g_gui_depth = 0; g_gui_req = 0; g_gui_grant = 0;
}
int gui_is_running(void) { return g_gui_loop_on; }   /* PARCHE 097 */
/* SOMA_266_AVISO: el aviso del LCM antes de ir al modelo de lenguaje (src/agent/agent.c). Desde el hilo del agente: se lee en voz alta
 * si Ajustes > Voz lo tiene activado (la respuesta de verdad corta la lectura), y con la pantalla cedida un momento se pone en la
 * conversacion y en el orbe, que siguen trabajando. Sin escritorio, nada (el aviso ya salio por el terminal). */
void gui_aviso(const char *texto);
#if defined(__x86_64__)
/* SOMA_330_RESPUESTA_PRIMERO: el aviso hablado espera un poco. Si la respuesta llega antes (lo normal con un modelo rapido), no se
 * dice: la respuesta tiene prioridad sobre cualquier muletilla. Tampoco se repite la misma frase si se dijo hace poco: en una
 * conversacion seguida, "se lo paso al modelo de IA" no se oye en cada pregunta. En el orbe y en la conversacion sale siempre. */
#define AVISO_ESPERA_MS 2500u
#define AVISO_REPETIR_MS 120000u
static volatile uint32_t g_av_gen;
static char g_av_txt[900];
static uint64_t g_av_dicho_t; static uint32_t g_av_dicho_h;
static uint32_t av_huella(const char *s) { uint32_t h = 2166136261u; for (uint32_t i = 0; s[i] && i < 48; ++i) h = (h ^ (uint8_t)s[i]) * 16777619u; return h; }
static void aviso_voz(void *arg)
{
    uint32_t gen = (uint32_t)(uintptr_t)arg, seq0 = voz_reply_seq();
    for (uint32_t t = 0; t < AVISO_ESPERA_MS; t += 20) {
        thread_sleep(20);
        if (gen != g_av_gen || !g_agent_thr_on || g_agent_thr_done || voz_reply_seq() != seq0) return;   /* ya hay respuesta */
    }
    uint64_t ahora = timer_get_uptime_ms(); uint32_t h = av_huella(g_av_txt);
    if (h == g_av_dicho_h && ahora - g_av_dicho_t < AVISO_REPETIR_MS) return;                             /* la misma, hace poco */
    g_av_dicho_h = h; g_av_dicho_t = ahora;
    (void)voz_speak_reply(g_av_txt);
}
#endif
void gui_aviso(const char *texto)
{
    if (!texto || !*texto || !g_gui_loop_on) return;
#if defined(__x86_64__)
    {
        uint32_t k = 0; while (texto[k] && k + 1 < sizeof g_av_txt) { g_av_txt[k] = texto[k]; k++; }
        g_av_txt[k] = 0;
        uint32_t gen = ++g_av_gen;
        (void)thread_create("aviso_voz", aviso_voz, (void *)(uintptr_t)gen);
    }
#endif
    int tok = gui_enter();
    if (!tok) return;
    const uint32_t wrap = 76;
    log_add(agent_log, &agent_log_count, "[soma]: ", texto, 0x00FBBF24, wrap);
    her_aviso(texto);
    gui_leave(tok);
}
int gui_agent_busy(void) { return g_agent_thr_on; }
/* SOMA_334_ORBE_MD: pone un texto (Markdown) en el orbe desde cualquier hilo ("orbe <archivo.md>"): con el turno de pantalla */
int gui_orbe_texto(const char *texto)
{
    if (!texto || !*texto || !g_gui_loop_on) return 0;
    int tok = gui_turn_begin();
    if (!tok) return 0;
    her_aviso(texto);
    gui_turn_end(tok);
    return 1;
}

/* SOMA_256: detener la mision en marcha. El hilo del agente se mata (thread_kill cierra sus conexiones TCP y suelta sus cerrojos; el
 * cambio de hilo es cooperativo, asi que no queda a medias dentro del montón) y el bucle principal cierra el turno como si hubiera
 * terminado, con el mensaje "Mision detenida.". Si estaba hablando (habla texto) se para primero la lectura. Solo desde el hilo de la GUI.
 * Pierde los buferes de trabajo del agente (~200 KiB) y puede dejar a medias un archivo que estuviera escribiendo. */
int gui_agent_cancel(void)
{
    if (!g_agent_thr_on) return 0;
    if (!g_agent_thr_done) {
#if defined(__x86_64__)
        if (voz_speaking()) { voz_speak_stop(); for (int i = 0; i < 400 && voz_speaking(); i++) thread_sleep(1); }
#endif
        (void)thread_kill(g_agent_tid);
        g_gui_req = 0; g_gui_grant = 0; g_gui_depth = 0;               /* si estaba pidiendo la pantalla, el bucle no debe esperarle */
        const char *m = "Mision detenida.";
        uint32_t k = 0; while (m[k] && k + 1 < sizeof agent_last_response) { agent_last_response[k] = m[k]; k++; }
        agent_last_response[k] = '\0';
        g_agent_thr_done = 1;
    }
    return 1;
}
/* Esc: para la lectura en voz alta si suena; si no, y con el escritorio / el orbe en foco, la mision del agente. 1 = lo ha usado. */
static int her_stop_escape(int agent_ok)
{
#if defined(__x86_64__)
    if (voz_speaking()) { voz_speak_stop(); clip_toast("Lectura detenida"); return 1; }
#endif
    if (agent_ok && g_agent_thr_on) { gui_agent_cancel(); clip_toast("Mision detenida"); return 1; }
    return 0;
}

/* SOMA_175B3: ordenes lentas del terminal (wifi buscar, wifi arrancar; con el 175C wifi conectar y desconectar) en un hilo aparte. Antes se ejecutaban en el hilo de
 * la GUI y el escritorio se quedaba congelado los segundos que dura la busqueda. Su salida se captura en ese hilo y se
 * muestra en el terminal al terminar; mientras, el sistema sigue respondiendo. Una a la vez. */
static volatile int tbg_state;                 /* 0 libre, 1 en marcha, 2 terminada (falta mostrarla) */
static char tbg_cmd[256], tbg_out[4096];
static char *tbg_cap; static uint32_t tbg_n;
enum { TBG_CAP = 64 * 1024 };
static volatile int tbg_live;                  /* SOMA_237_PYTHON: la orden en marcha es python (salida al momento y entrada viva) */
static int tbg_is_python(const char *c)
{
    while (*c == ' ') c++;
    return c[0] == 'p' && c[1] == 'y' && c[2] == 't' && c[3] == 'h' && c[4] == 'o' && c[5] == 'n' && (c[6] == 0 || c[6] == ' ');
}
static int tbg_is_slow(const char *c)
{
    while (*c == ' ') c++;
    if (tbg_is_python(c)) return 1;                                                             /* SOMA_237: python tarda lo que tarde el script */
    if (c[0] == 'd' && c[1] == 'o' && c[2] == 'c' && c[3] == 'u' && c[4] == 'm' && c[5] == 'e' && c[6] == 'n' && c[7] == 't' && c[8] == 'o' && c[9] == ' ') {   /* SOMA_248_DOC */
        const char *s = c + 10; while (*s == ' ') s++;
        return !(s[0] == 'a' && s[1] == 'b' && s[2] == 'r' && s[3] == 'i' && s[4] == 'r');                  /* el LLM tarda minutos; abrir es al momento */
    }
    if (c[0] == 't' && c[1] == 'l' && c[2] == 's' && c[3] == ' ') return 1;                     /* SOMA_176A: tls <url> tarda segundos */
    if (c[0] == 'c' && c[1] == 'a' && c[2] == 'm' && c[3] == ' ') {                            /* SOMA_178_T480S: iniciar, foto y video esperan segundos */
        const char *s = c + 4; while (*s == ' ') s++;
        if (s[0] == 'i' && s[1] == 'n' && s[2] == 'i') return 1;
        if (s[0] == 'f' || s[0] == 'v') return 1;
    }
    if ((c[0] == 'c' && c[1] == 'a' && c[2] == 'p' && c[3] == ' ') || (c[0] == 'c' && c[1] == 'a' && c[2] == 'p' && c[3] == 't' && c[4] == 'u' && c[5] == 'r' && c[6] == 'a' && c[7] == ' ')) {   /* SOMA_179_T480S: foto, video, audio y av esperan segundos */
        const char *s = c + (c[3] == ' ' ? 4 : 8); while (*s == ' ') s++;
        if (s[0] == 'f' || s[0] == 'v' || (s[0] == 'a' && s[1] != 'b') || s[0] == 'i' || s[0] == 'p') return 1;
    }
    if (c[0] == 'm' && c[1] == 'i' && c[2] == 'c' && c[3] == ' ') {                            /* SOMA_177B: nivel, grabar y probar esperan segundos */
        const char *s = c + 4; while (*s == ' ') s++;
        if (s[0] == 'n' || s[0] == 'g' || s[0] == 'p') return 1;
    }
    if (!(c[0] == 'w' && c[1] == 'i' && c[2] == 'f' && c[3] == 'i' && c[4] == ' ')) return 0;
    c += 5; while (*c == ' ') c++;
    /* buscar, arrancar, conectar y desconectar (SOMA_175C) */
    static const char *const slow[6] = { "buscar", "arrancar", "conectar", "desconectar", "prueba", "ipfija" };   /* SOMA_175C5: prueba */
    for (int i = 0; i < 6; i++) { const char *w = slow[i]; int k = 0; while (w[k] && c[k] == w[k]) k++; if (!w[k] && (c[k] == 0 || c[k] == ' ')) return 1; }
    return 0;
}
static void tbg_main(void *a)
{
    (void)a;
    tbg_out[0] = 0; tbg_n = 0;
    int cap = !console_capture_active();
    if (cap) console_capture_begin(tbg_cap, TBG_CAP, "");
    if (cap && tbg_live) console_capture_live(1);                 /* SOMA_237: el terminal le ira dando lineas */
    dispatch_command(tbg_cmd, tbg_out, sizeof tbg_out);
    if (cap) tbg_n = console_capture_end();
    tbg_state = 2;
}
static int tbg_start(const char *cmd)
{
    if (tbg_state) return -1;
    if (!tbg_cap) { tbg_cap = (char *)kmalloc(TBG_CAP); if (!tbg_cap) return -2; }
    uint32_t i = 0; while (cmd[i] && i < sizeof tbg_cmd - 1) { tbg_cmd[i] = cmd[i]; i++; } tbg_cmd[i] = 0;
    tbg_live = tbg_is_python(cmd);
    tbg_state = 1;
    if (!thread_create("term_bg", tbg_main, 0)) { tbg_state = 0; tbg_live = 0; return -3; }
    return 0;
}
/* muestra la salida (sin colores ANSI) linea a linea; 1 si habia algo */
static int tbg_show(void)
{
    const char *show = tbg_out;
    if (!show[0]) {
        uint32_t w = 0, cn = tbg_n;
        for (uint32_t r = 0; r < cn; r++) {
            if (tbg_cap[r] == 27 && r + 1 < cn && tbg_cap[r + 1] == '[') {
                r += 2; while (r < cn && !((tbg_cap[r] >= 'A' && tbg_cap[r] <= 'Z') || (tbg_cap[r] >= 'a' && tbg_cap[r] <= 'z'))) r++;
                continue;
            }
            tbg_cap[w++] = tbg_cap[r];
        }
        tbg_cap[w < TBG_CAP ? w : TBG_CAP - 1] = 0;
        show = tbg_cap;
    }
    int any = 0;
    while (*show) {
        char line[220]; uint32_t lp = 0;
        while (*show && *show != '\n' && lp < sizeof line - 1) { if (*show != '\r') line[lp++] = *show; show++; }
        line[lp] = 0; if (*show == '\n') show++;
        if (lp) { term_print(line); any = 1; }
    }
    return any;
}

/* SOMA_237_PYTHON: python en el terminal de la GUI. Mientras corre, lo que imprime sale al momento (las lineas completas; lo que
 * queda sin salto, como ">>> " o "nombre? ", hace de prompt), Intro le da la linea escrita y Esc lo para. */
static char tbg_prompt[96];
static int tbg_ui_slot(void) { return her_tbg_slot > 0 && her_tbg_slot < 16 && her_term_sessions[her_tbg_slot].used ? her_tbg_slot : 0; }
static int tbg_live_here(void) { return tbg_state == 1 && tbg_live && (her_term_live > 0 ? her_term_live : 0) == tbg_ui_slot(); }
static void tbg_emit(const char *p, uint32_t n)              /* una linea (troceada si es larga) en la sesion de python */
{
    char line[200]; uint32_t lp = 0;
    for (uint32_t i = 0; i <= n; ++i) {
        if (i == n || lp == sizeof line - 1) { line[lp] = 0; term_print(line); lp = 0; if (i == n) break; }
        if (p[i] != '\r') line[lp++] = p[i];
    }
}
static int tbg_stream(void)                                  /* 1 = hay que redibujar */
{
    if (!(tbg_state == 1 && tbg_live) || !tbg_cap) return 0;
    uint32_t n = console_capture_len(), last = 0; int changed = 0;
    for (uint32_t i = 0; i < n; ++i) if (tbg_cap[i] == '\n') last = i + 1;
    int old_slot = her_term_live;
    (void)term_ui_select(tbg_ui_slot());
    if (last) {
        uint32_t a = 0;
        for (uint32_t i = 0; i < last; ++i) if (tbg_cap[i] == '\n') { tbg_emit(tbg_cap + a, i - a); a = i + 1; }
        console_capture_consume(last);
        n -= last; changed = 1;
    }
    char pr[96]; uint32_t k = 0;
    for (uint32_t i = 0; i < n && k < sizeof pr - 1; ++i) if (tbg_cap[i] != '\r') pr[k++] = tbg_cap[i];
    pr[k] = 0;
    uint32_t j = 0; while (pr[j] && pr[j] == tbg_prompt[j]) j++;
    if (pr[j] || tbg_prompt[j]) { for (uint32_t i = 0; i <= k; ++i) tbg_prompt[i] = pr[i]; term_set_prompt(tbg_ui_slot(), tbg_prompt); changed = 1; }
    (void)term_ui_select(old_slot > 0 ? old_slot : 0);
    return changed;
}
static int tbg_live_enter(const char *cmd, uint32_t len)      /* Intro en el terminal donde corre python: la linea es para el */
{
    if (!tbg_live_here()) return 0;
    char echo[220]; uint32_t e = 0;
    for (uint32_t i = 0; tbg_prompt[i] && e < sizeof echo - 1; ++i) echo[e++] = tbg_prompt[i];
    for (uint32_t i = 0; i < len && e < sizeof echo - 1; ++i) echo[e++] = cmd[i];
    echo[e] = 0;
    term_print(echo);
    console_capture_consume(console_capture_len());              /* el prompt ya forma parte de la linea mostrada */
    tbg_prompt[0] = 0; term_set_prompt(tbg_ui_slot(), "");
    char line[256]; uint32_t k = 0;
    for (uint32_t i = 0; i < len && k < sizeof line - 1; ++i) line[k++] = cmd[i];
    line[k] = 0;
    if (console_capture_feed(line) < 0) term_print("(python: entrada llena; espera a que lea lo anterior)");
    return 1;
}
/* SOMA_239: el programador pide ejecutar una orden en el Terminal (probar un programa Python: su salida, input() y Esc ahi). */
static char g_term_run_req[160];
void gui_terminal_run(const char *cmd)
{
    uint32_t i = 0; while (cmd && cmd[i] && i < sizeof g_term_run_req - 1) { g_term_run_req[i] = cmd[i]; i++; }
    g_term_run_req[i] = 0;
}
static int tbg_live_stop(void)                                /* Esc: parar python */
{
    if (!tbg_live_here()) return 0;
    (void)shell_python_stop();
    console_capture_feed_eof();
    term_print("^ python detenido (Esc)");
    return 1;
}

static void submit_global_agent_prompt(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    (void)win_x; (void)win_y; (void)win_w; (void)win_h;
    if (agent_global_len == 0) return;

    char mission_copy[SOMA_AGENT_CMD_MAX];
    uint32_t n = agent_global_len;
    if (n >= sizeof(mission_copy)) n = sizeof(mission_copy) - 1;
    for (uint32_t i = 0; i < n; ++i) mission_copy[i] = agent_global_cmd[i];
    mission_copy[n] = '\0';

    const uint32_t wrap = 76;
    if (g_agent_thr_on) {   /* PARCHE 091: una mision a la vez */
        log_add(agent_log, &agent_log_count, "[soma]: ", "Sigo trabajando en la mision anterior; espera a que termine.", 0x00FBBF24, wrap);
        her_refresh_all(); fb_present();
        return;
    }
    log_add(agent_log, &agent_log_count, "user> ", mission_copy, COLOR_CYAN, wrap);
    her_begin_turn(mission_copy);

    agent_global_len = 0;
    agent_global_cursor = 0;
    agent_global_cmd[0] = '\0';
    agent_global_scroll = 0;
    her_prev_len = 0;
    her_pulse = 0;
    agent_last_response[0] = '\0';

    her_refresh_all();
    fb_present();

    /* PARCHE 091: la mision va a su hilo; si no se puede crear, como antes (bloqueando) */
    if (g_gui_loop_on) {
        for (uint32_t i = 0; i < sizeof(g_agent_mission); i++) { g_agent_mission[i] = mission_copy[i]; if (!mission_copy[i]) break; }
        g_agent_thr_done = 0; g_gui_req = 0; g_gui_grant = 0; g_gui_depth = 0;
        g_agent_mis_ptr = 0;                                    /* SOMA_285_CHAT */
        her_agent_busy = 1; g_agent_thr_on = 1;
        struct tcb *at = thread_create("soma_agente", agent_thread_main, 0);
        if (at) { g_agent_tid = at->tid; thread_set_owner(at, ACCT_AGENT); return; }
        g_agent_thr_on = 0; her_agent_busy = 0;
    }
    her_anim_start();
    agent_run("soma", mission_copy);
    her_anim_stop();

    her_finish_turn(agent_last_response);

    if (agent_last_response[0] != '\0')
        log_add(agent_log, &agent_log_count, "[soma]: ", agent_last_response, 0x0034D399, wrap);
    else
        log_add(agent_log, &agent_log_count, "[soma]: ", "Mision ejecutada en hardware.", 0x0034D399, wrap);

    agent_global_scroll = 0;
    her_refresh_all();
    fb_present();
}

/* SOMA_248_DOC: una aplicacion (el Maquetador) entrega una mision al agente como si se escribiera en el orbe: va a su hilo, el orbe
 * muestra el progreso y la GUI sigue viva. Solo desde el hilo de la GUI. 0 = enviada, -1 = el agente esta ocupado, -2 = vacia. */
int gui_agent_submit(const char *mission)
{
    if (!mission || !*mission) return -2;
    if (g_agent_thr_on) return -1;
    uint32_t n = 0;
    while (mission[n] && n + 1 < sizeof agent_global_cmd) { agent_global_cmd[n] = mission[n]; n++; }
    agent_global_cmd[n] = 0; agent_global_len = n; agent_global_cursor = n;
    submit_global_agent_prompt(0, 0, 0, 0);
    return 0;
}

/* SOMA_285_CHAT: la aplicacion de chat entrega su mision al agente (puede ser larga: lleva los adjuntos). Va al hilo del agente como
 * las del orbe, que muestra el progreso; la respuesta vuelve al chat en agent_turn_finish (chat_agente_fin). Solo desde el hilo de la GUI.
 * 0 = enviada, -1 = el agente esta ocupado (o no hay escritorio), -2 = vacia o sin memoria. */
int gui_agent_submit_chat(const char *mision);
int gui_agent_submit_chat(const char *mision)
{
    if (!mision || !*mision) return -2;
    if (g_agent_thr_on || !g_gui_loop_on) return -1;
    uint32_t n = 0; while (mision[n]) n++;
    char *m = (char *)kmalloc(n + 1);
    if (!m) return -2;
    for (uint32_t i = 0; i <= n; i++) m[i] = mision[i];
    if (g_chat_mis) kfree(g_chat_mis);
    g_chat_mis = m; g_agent_mis_ptr = m;
    {   char corta[200]; uint32_t k = 0; while (k < n && k + 1 < sizeof corta && m[k] != '\n') { corta[k] = m[k]; k++; } corta[k] = 0;
        her_begin_turn(corta); }
    agent_last_response[0] = '\0';
    her_pulse = 0;
    g_agent_thr_done = 0; g_gui_req = 0; g_gui_grant = 0; g_gui_depth = 0;
    her_agent_busy = 1; g_agent_thr_on = 1;
    struct tcb *at = thread_create("soma_agente", agent_thread_main, 0);
    if (at) { g_agent_tid = at->tid; thread_set_owner(at, ACCT_AGENT); return 0; }
    g_agent_thr_on = 0; her_agent_busy = 0; g_agent_mis_ptr = 0;
    return -1;
}
