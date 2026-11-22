/* SOMA_251_YOUTUBE_IA: la inteligencia de la aplicacion YouTube. Este archivo se incluye desde youtube.c (comparte su estado).
 *
 *   - Buscador en lenguaje natural: lo que se escribe en la caja pasa por el LLM del sistema (llm_chat_ex), que lo convierte en
 *     hasta tres busquedas de YouTube; los resultados se juntan y el LLM escribe un resumen de lo encontrado.
 *   - Ficha del video: descripcion completa (servicio: /yt/info).
 *   - Transcripcion: con whisper (voz_run) sobre el sonido del video (servicio: /yt/audio -> WAV en el almacen BIGFS), o con
 *     los subtitulos de YouTube (/yt/subs) cuando whisper no esta disponible o se quiere algo inmediato.
 *   - Resumen del video y preguntas sobre su contenido, con la transcripcion (o la descripcion) como contexto del LLM.
 *
 * Lo lento va en dos hilos: "ytia" (lo del video abierto: un trabajo cada vez, con uno en espera) e "ytnl" (la busqueda en
 * lenguaje natural, para que buscar no tenga que esperar a una transcripcion). La ventana solo pide y pinta. */

enum { J_NONE = 0, J_INFO, J_SUM, J_WHISPER, J_SUBS, J_ASK };
enum { AI_OFF = 0, AI_THINK, AI_SEARCH, AI_SUMMING, AI_DONE, AI_PLAIN };   /* AI_PLAIN: el LLM no respondio; se busco el texto tal cual */

#define IA_CAP     200000u          /* respuestas del servicio y salida de whisper */
#define TX_CAP     160000u          /* transcripcion (unas dos horas de habla) */
#define CHAT_CAP   12000u
#define DESC_CAP   6200u
#define WAV_PATH   "/yt_audio.wav"

static volatile int ia_alive, ia_run, ia_cancel;
static volatile int nl_alive, nl_run, nl_cancel; static volatile uint32_t nl_req, nl_done; static char nl_text[160];
static volatile uint32_t ia_req, ia_done, ia_seq;    /* pedido, atendido, "ha cambiado algo que se ve" */
static int ia_next; static char ia_next_arg[320]; static char ia_next_id[YT_ID_LEN + 1];
static char ia_status[200]; static int ia_err; static uint64_t ia_t0;

/* --- del video que se esta viendo (se vacia al cambiar de video) */
static char vd_id[YT_ID_LEN + 1];
static char vd_desc[DESC_CAP]; static struct yt_info vd_info; static int vd_state;   /* 0 sin pedir, 1 lista, 2 no se pudo */
static char *tx; static uint32_t tx_len; static char tx_src[96];
static char chat[CHAT_CAP]; static uint32_t chat_len;
static uint32_t vd_ver, tx_ver, chat_ver;            /* suben con cada cambio: invalidan el ajuste de lineas */

/* --- de la busqueda en lenguaje natural */
static int ai_state; static char ai_ask[160]; static char ai_q[YT_AI_QMAX][YT_AI_QLEN]; static int ai_nq;
static char ai_sum[1400]; static uint32_t ai_ver; static char ai_why[200];

static uint8_t *iabuf;

static void ia_set(const char *m, int err) { yt_scopy(ia_status, sizeof ia_status, m); ia_err = err; ia_seq++; }
static int ia_mine(const char *id) { return !ia_cancel && yt_seq(vd_id, id); }

static void chat_add(char kind, const char *s)       /* kind: 1 pregunta, 2 titulo, 3 error, 0 texto */
{
    uint32_t n = yt_slen(s), need = n + 5;
    if (need >= CHAT_CAP) { n = CHAT_CAP - 8; need = n + 5; }
    while (chat_len + need >= CHAT_CAP) {            /* sin sitio: se olvida el parrafo mas antiguo */
        uint32_t cut = 0; while (cut < chat_len && chat[cut] != '\n') cut++;
        if (cut < chat_len) cut++;
        if (!cut) { chat_len = 0; break; }
        for (uint32_t i = cut; i < chat_len; ++i) chat[i - cut] = chat[i];
        chat_len -= cut;
    }
    if ((kind == 1 || kind == 2) && chat_len) chat[chat_len++] = '\n';     /* una linea en blanco antes de cada pregunta o titulo */
    if (kind) chat[chat_len++] = kind;
    for (uint32_t i = 0; i < n; ++i) chat[chat_len++] = s[i];
    chat[chat_len++] = '\n';
    chat[chat_len] = 0;
    chat_ver++; ia_seq++;
}

static void ia_video_reset(const char *id)           /* la ventana cambia de video */
{
    if (yt_seq(vd_id, id)) return;
    if (ia_run != J_NONE) {
        ia_cancel = 1;
#ifdef __x86_64__
        if (ia_run == J_WHISPER) voz_stop();
#endif
    }
    ia_next = J_NONE;
    yt_scopy(vd_id, sizeof vd_id, id);
    vd_desc[0] = 0; vd_state = 0; vd_info.date[0] = 0; vd_info.subs[0] = 0;
    tx_len = 0; if (tx) tx[0] = 0; tx_src[0] = 0;
    chat_len = 0; chat[0] = 0;
    vd_ver++; tx_ver++; chat_ver++;
    ia_status[0] = 0; ia_err = 0;
    ia_seq++;
}

/* ---------------------------------------------------------------- transcripciones guardadas (BIGFS: /yt_<id>.txt) */
static void tx_set(const char *id, const char *text, uint32_t n, const char *src)
{
    if (!tx) tx = (char *)kmalloc(TX_CAP);
    if (!tx || !yt_seq(vd_id, id)) return;
    if (n >= TX_CAP) n = TX_CAP - 1;
    for (uint32_t i = 0; i < n; ++i) tx[i] = text[i];
    tx[n] = 0;
    tx_len = yt_plain(tx);
    yt_scopy(tx_src, sizeof tx_src, src);
    tx_ver++; ia_seq++;
}
static void tx_save(const char *id)
{
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs || !tx_len) return;
    char p[40]; yt_txt_path(p, sizeof p, id);
    uint32_t ino;
    if (bigfs_lookup(fs, p, &ino) >= 0) bigfs_unlink(fs, p);
    if (bigfs_create(fs, p, &ino) < 0) return;
    char h[120]; uint32_t hp = 0; yt_put(h, sizeof h, &hp, "#yt "); yt_put(h, sizeof h, &hp, tx_src); yt_put(h, sizeof h, &hp, "\n");
    (void)bigfs_write(fs, ino, 0, h, hp);
    (void)bigfs_write(fs, ino, hp, tx, tx_len);
}
static int tx_load(const char *id)                   /* 1 = habia una guardada */
{
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs || !iabuf) return 0;
    char p[40]; yt_txt_path(p, sizeof p, id);
    uint32_t ino;
    if (bigfs_lookup(fs, p, &ino) < 0) return 0;
    int64_t n = bigfs_read(fs, ino, 0, iabuf, IA_CAP - 1);
    if (n < 8) return 0;
    iabuf[n] = 0;
    char src[96]; uint32_t off = 0;
    yt_scopy(src, sizeof src, "guardada");
    if (yt_starts((const char *)iabuf, "#yt ")) {
        uint32_t k = 0; off = 4;
        while (off < (uint32_t)n && iabuf[off] != '\n') { if (k + 1 < sizeof src) src[k++] = (char)iabuf[off]; off++; }
        src[k] = 0; if (off < (uint32_t)n) off++;
    }
    tx_set(id, (const char *)iabuf + off, (uint32_t)n - off, src);
    return tx_len > 0;
}

/* ---------------------------------------------------------------- trabajos */
static const char SYS_NL[] =
    "Eres el buscador de videos de SOMA. El usuario describe con sus palabras lo que quiere ver. Devuelve de 1 a 3 busquedas "
    "para YouTube que encuentren los mejores videos: cortas (de 2 a 6 palabras), en el idioma mas util para ese tema y distintas "
    "entre si. Una por linea, sin numeros, sin comillas y sin ninguna explicacion.";
static const char SYS_PICK[] =
    "Eres el buscador de videos de SOMA. Te doy lo que pidio el usuario y los videos encontrados (numero, titulo, canal, duracion "
    "y principio de la descripcion). Escribe en espanol, en 3 o 4 frases seguidas y sin listas ni Markdown: que se ha encontrado y "
    "que videos recomiendas ver primero y por que, citando su numero entre parentesis, por ejemplo (2). No inventes nada que no "
    "este en los datos.";
static const char SYS_VID[] =
    "Eres el asistente de video de SOMA. Respondes en el idioma de la pregunta, claro y breve, sin Markdown. Usa SOLO la "
    "informacion del video que se te da (titulo, descripcion y transcripcion); si algo no aparece ahi, dilo. Las marcas [m:ss] de "
    "la transcripcion son el minuto del video: citalas cuando ayuden a encontrar el momento.";

static void search_launch(const char q[][YT_AI_QLEN], int n, const char *shown);

static void llm_why(char *o, uint32_t max)
{
    uint32_t p = 0;
    yt_put(o, max, &p, llm_last_error[0] ? llm_last_error : "el modelo de lenguaje no ha respondido");
}

static void job_nl(const char *text)
{
    static char reply[1600];
    char q[YT_AI_QMAX][YT_AI_QLEN]; int n = 0;
    yt_scopy(ai_ask, sizeof ai_ask, text); ai_nq = 0; ai_sum[0] = 0; ai_why[0] = 0; ai_ver++;
    ai_state = AI_THINK; ia_seq++;
    reply[0] = 0;
    int ok = llm_chat_ex(SYS_NL, text, 0, 0, 200, reply, sizeof reply, 45000u);
    if (nl_cancel) return;
    if (ok == 1) { yt_plain(reply); n = yt_ai_queries(reply, q); }
    if (!n) {                                                    /* sin LLM (o respuesta inservible): el texto, tal cual */
        if (ok == 1) yt_scopy(ai_why, sizeof ai_why, "el modelo no ha propuesto busquedas"); else llm_why(ai_why, sizeof ai_why);
        yt_scopy(q[0], YT_AI_QLEN, text); n = 1;
        ai_state = AI_PLAIN;
    } else {
        for (int i = 0; i < n; ++i) yt_scopy(ai_q[i], YT_AI_QLEN, q[i]);
        ai_nq = n; ai_state = AI_SEARCH;
    }
    ia_seq++;
    search_launch((const char (*)[YT_AI_QLEN])q, n, text);
    uint32_t g = net_req;
    for (int w = 0; w < 2400 && items_gen != g && net_done != g && net_req == g && !nl_cancel; ++w) thread_sleep(50);   /* a que la ventana adopte la lista */
    for (int w = 0; w < 40 && net_req == g && items_gen != g && net_state != NS_ERROR && !nl_cancel; ++w) thread_sleep(50);
    if (nl_cancel || net_req != g || ai_state == AI_PLAIN) return;
    if (items_gen != g || !nitems) { ai_state = AI_DONE; ia_seq++; return; }
    ai_state = AI_SUMMING; ia_seq++;
    uint32_t cap = 400 + (uint32_t)nitems * 420;
    char *pr = (char *)kmalloc(cap);
    if (!pr) { ai_state = AI_DONE; return; }
    uint32_t p = 0;
    yt_put(pr, cap, &p, "Peticion del usuario: "); yt_put(pr, cap, &p, text); yt_put(pr, cap, &p, "\n\nVideos encontrados:\n");
    for (int i = 0; i < nitems; ++i) {
        char d[16]; yt_fmt_time(d, sizeof d, items[i].dur);
        yt_put_dec(pr, cap, &p, (uint64_t)(i + 1)); yt_put(pr, cap, &p, ". "); yt_put(pr, cap, &p, items[i].title);
        yt_put(pr, cap, &p, " | "); yt_put(pr, cap, &p, items[i].channel); yt_put(pr, cap, &p, " | "); yt_put(pr, cap, &p, d);
        if (items[i].desc[0]) { yt_put(pr, cap, &p, " | "); yt_put(pr, cap, &p, items[i].desc); }
        yt_put(pr, cap, &p, "\n");
    }
    reply[0] = 0;
    ok = llm_chat_ex(SYS_PICK, pr, 0, 0, 400, reply, sizeof reply, 90000u);
    kfree(pr);
    if (nl_cancel || net_req != g) return;
    if (ok == 1) { yt_plain(reply); for (char *c = reply; *c; ++c) if (*c == '\n') *c = ' '; yt_scopy(ai_sum, sizeof ai_sum, reply); }
    else llm_why(ai_why, sizeof ai_why);
    ai_state = AI_DONE; ai_ver++; ia_seq++;
}

/* la ficha (y, de paso, la transcripcion que hubiera guardada). 1 = hay descripcion. */
static int ensure_info(const char *id)
{
    if (!tx_len && ia_mine(id)) (void)tx_load(id);
    if (vd_state == 1) return 1;
    if (native_mode() || yt_seq(id, NAT_DIRECT_ID)) {                    /* SOMA_252: la ficha viene con la respuesta del video */
        char e2[200]; e2[0] = 0;
        if (yt_seq(id, NAT_DIRECT_ID)) { vd_desc[0] = 0; vd_info.date[0] = vd_info.subs[0] = 0; vd_info.dur = vd_info.views = 0; vd_state = 1; vd_ver++; ia_seq++; return 0; }
        if (!yt_seq(nat_desc_id, id)) {
            static struct yti_player pl;
            if (!nat_player(id, &pl, e2, sizeof e2, (const volatile uint32_t *)&ia_cancel, 0) && ia_mine(id)) {
                vd_state = 2; yt_scopy(vd_desc, sizeof vd_desc, e2[0] ? e2 : "No se pudo pedir la ficha a YouTube"); vd_ver++; ia_seq++;
                return 0;
            }
        }
        if (!ia_mine(id) || !yt_seq(nat_desc_id, id)) return 0;
        yt_scopy(vd_desc, sizeof vd_desc, nat_desc); yt_plain(vd_desc);
        vd_info.date[0] = 0; vd_info.subs[0] = 0; vd_info.dur = now_it.dur; vd_info.views = nat_views;
        vd_state = 1; vd_ver++; ia_seq++;
        return vd_desc[0] != 0;
    }
    char path[200], err[160]; char *body; uint32_t len = 0;
    yt_path_info(path, sizeof path, &conf, id);
    int st = y_get_buf(iabuf, IA_CAP, path, &body, &len, 100000u, (const volatile uint32_t *)&ia_cancel, 0, err, sizeof err);
    if (!ia_mine(id)) return 0;
    struct yt_info inf;
    if (st != 200 || !yt_parse_info(body, len, &inf)) {
        vd_state = 2; yt_scopy(vd_desc, sizeof vd_desc, st == 200 ? "El servicio YouTube ha respondido algo inesperado" : err);
        vd_ver++; ia_seq++;
        return 0;
    }
    vd_info = inf;
    uint32_t n = inf.desc_len < DESC_CAP - 1 ? inf.desc_len : DESC_CAP - 1;
    for (uint32_t i = 0; i < n; ++i) vd_desc[i] = body[inf.desc_off + i];
    vd_desc[n] = 0; yt_plain(vd_desc);
    vd_state = 1; vd_ver++; ia_seq++;
    return 1;
}

/* Contexto del video para el LLM: titulo, canal, descripcion y transcripcion (recortada al presupuesto). */
static char *video_prompt(const char *tail_label, const char *tail, const char *history)
{
    uint32_t budget = (uint32_t)conf.ctx, cap = budget + 6000 + yt_slen(tail) + yt_slen(history);
    char *pr = (char *)kmalloc(cap);
    if (!pr) return 0;
    uint32_t p = 0;
    yt_put(pr, cap, &p, "Titulo: "); yt_put(pr, cap, &p, now_it.title);
    yt_put(pr, cap, &p, "\nCanal: "); yt_put(pr, cap, &p, now_it.channel);
    if (now_it.dur) { char d[16]; yt_fmt_time(d, sizeof d, now_it.dur); yt_put(pr, cap, &p, "\nDuracion: "); yt_put(pr, cap, &p, d); }
    if (vd_state == 1 && vd_info.date[0]) { yt_put(pr, cap, &p, "\nFecha: "); yt_put(pr, cap, &p, vd_info.date); }
    if (vd_state == 1 && vd_desc[0]) {
        yt_put(pr, cap, &p, "\n\nDescripcion:\n");
        p += yt_clip_ctx(pr + p, cap - p, vd_desc, yt_slen(vd_desc), tx_len ? 1500u : 5000u);
    }
    if (tx_len) {
        yt_put(pr, cap, &p, "\n\nTranscripcion ("); yt_put(pr, cap, &p, tx_src); yt_put(pr, cap, &p, "):\n");
        p += yt_clip_ctx(pr + p, cap - p, tx, tx_len, budget);
    } else yt_put(pr, cap, &p, "\n\n(No hay transcripcion: solo se conoce lo anterior.)");
    if (history && history[0]) { yt_put(pr, cap, &p, "\n\nConversacion anterior:\n"); yt_put(pr, cap, &p, history); }
    yt_put(pr, cap, &p, "\n\n"); yt_put(pr, cap, &p, tail_label); yt_put(pr, cap, &p, tail);
    return pr;
}

static void job_sum(const char *id)
{
    static char reply[4000];
    ia_set("Leyendo la ficha del video", 0);
    (void)ensure_info(id);
    if (!ia_mine(id)) return;
    if (!tx_len && !(vd_state == 1 && vd_desc[0])) { ia_set("No hay de donde resumir: el video no tiene descripcion. Transcribelo antes (pestana Texto).", 1); return; }
    ia_set(tx_len ? "Resumiendo la transcripcion" : "Resumiendo a partir de la descripcion", 0);
    char *pr = video_prompt("Tarea: ", "resume este video en espanol: primero de que trata en dos frases y despues sus puntos clave, "
                            "uno por linea empezando por un guion (como mucho siete). No anadas nada que no este arriba.", 0);
    if (!pr) { ia_set("Sin memoria", 1); return; }
    reply[0] = 0;
    int ok = llm_chat_ex(SYS_VID, pr, 0, 0, 700, reply, sizeof reply, 180000u);
    kfree(pr);
    if (!ia_mine(id)) return;
    if (ok != 1) { char w[200]; llm_why(w, sizeof w); chat_add(3, w); ia_set(w, 1); return; }
    yt_plain(reply);
    chat_add(2, tx_len ? "Resumen (de la transcripcion)" : "Resumen (solo de la descripcion: transcribe el video para uno mas fiel)");
    chat_add(0, reply);
    ia_set("", 0);
}

static void job_ask(const char *id, const char *question)
{
    static char reply[4000]; static char hist[1800];
    /* lo ultimo de la conversacion (sin la pregunta recien anadida), como contexto */
    uint32_t end = chat_len, qn = yt_slen(question);
    if (end >= qn + 2) end -= qn + 2;
    if (end && chat[end - 1] == '\n' && end >= 2 && chat[end - 2] == '\n') end--;
    uint32_t start = end > sizeof hist - 1 ? end - (sizeof hist - 1) : 0, hp = 0;
    while (start && start < end && chat[start - 1] != '\n') start++;
    for (uint32_t i = start; i < end; ++i) {
        char c = chat[i];
        if (hp >= sizeof hist - 16) break;
        if (c == 1) { static const char u[] = "Usuario: "; for (uint32_t k = 0; u[k]; ++k) hist[hp++] = u[k]; continue; }
        if (c == 2 || c == 3) continue;
        hist[hp++] = c;
    }
    hist[hp < sizeof hist ? hp : sizeof hist - 1] = 0;
    ia_set("Leyendo la ficha del video", 0);
    (void)ensure_info(id);
    if (!ia_mine(id)) return;
    ia_set(tx_len ? "Pensando (con la transcripcion)" : "Pensando (sin transcripcion: solo titulo y descripcion)", 0);
    char *pr = video_prompt("Pregunta del usuario: ", question, hist);
    if (!pr) { ia_set("Sin memoria", 1); return; }
    reply[0] = 0;
    int ok = llm_chat_ex(SYS_VID, pr, 0, 0, 700, reply, sizeof reply, 180000u);
    kfree(pr);
    if (!ia_mine(id)) return;
    if (ok != 1) { char w[200]; llm_why(w, sizeof w); chat_add(3, w); ia_set(w, 1); return; }
    yt_plain(reply);
    chat_add(0, reply);
    ia_set("", 0);
}

static void job_subs(const char *id)
{
    char path[200], err[160]; char *body; uint32_t len = 0;
    if (native_mode()) { ia_set("En modo nativo YouTube no entrega subtitulos a SOMA. Con el servicio externo si (youtube modo servicio).", 1); return; }
    ia_set("Pidiendo los subtitulos a YouTube", 0);
    yt_path_subs(path, sizeof path, &conf, id);
    int st = y_get_buf(iabuf, IA_CAP, path, &body, &len, 120000u, (const volatile uint32_t *)&ia_cancel, 0, err, sizeof err);
    if (!ia_mine(id)) return;
    if (st != 200) { ia_set(err[0] ? err : "No se pudieron pedir los subtitulos", 1); return; }
    /* "SOMAYT 1 subs <idioma> <auto>\n" y el texto */
    uint32_t e = 0; while (e < len && body[e] != '\n') e++;
    if (len < 16 || !yt_starts(body, "SOMAYT ") || e >= len) { ia_set("El servicio YouTube ha respondido algo inesperado", 1); return; }
    char src[96]; uint32_t sp = 0, f = 0, a = 0;
    yt_put(src, sizeof src, &sp, "subtitulos de YouTube");
    for (uint32_t i = 0; i < e; ++i) if (body[i] == ' ' && ++f == 3) { a = i + 1; break; }
    if (a) {
        yt_put(src, sizeof src, &sp, ", ");
        while (a < e && body[a] != ' ' && sp + 1 < sizeof src) src[sp++] = body[a++];
        src[sp] = 0;
        if (a + 1 < e && body[a + 1] == '1') yt_put(src, sizeof src, &sp, ", automaticos");
    }
    tx_set(id, body + e + 1, len - e - 1, src);
    if (!tx_len) { ia_set("Los subtitulos han llegado vacios", 1); return; }
    tx_save(id);
    ia_set("", 0);
}

#ifdef __x86_64__
static void mib_text(char *o, uint32_t max, uint32_t *p, uint64_t bytes)
{
    uint64_t t = bytes * 10 / (1024u * 1024u);
    yt_put_dec(o, max, p, t / 10); yt_put(o, max, p, ","); yt_put_dec(o, max, p, t % 10);
}
/* Baja /yt/audio al almacen (WAV_PATH). 1 = hecho; 0 = fallo (mensaje puesto) o cancelado. */
static int audio_fetch(const char *id, uint32_t secs, struct bigfs *fs)
{
    char path[200], err[160]; uint32_t ep = 0;
    yt_path_audio(path, sizeof path, &conf, id, secs);
    int h = y_connect(err, sizeof err);
    if (h < 0) { ia_set(err, 1); return 0; }
    if (y_send_get(h, path) != 0) { tcp_close(h); ia_set("No se pudo enviar la peticion al servicio", 1); return 0; }
    uint32_t have = 0, body_off = 0; int status = 0, head = 0; uint64_t clen = 0, wrote = 0, last = ms(), shown = 0;
    uint32_t ino = 0; int fail = 0;
    const uint32_t CH = 65536u;
    for (;;) {
        if (!ia_mine(id)) { tcp_abort(h); return 0; }
        int n = tcp_recv(h, iabuf + have, (uint16_t)(CH + 16384u - have > 16384u ? 16384u : CH + 16384u - have), 80);
        if (n > 0) { have += (uint32_t)n; last = ms(); }
        else if (n == TCP_EOF || n == TCP_ERR) {
            if (head && have) { if (bigfs_write(fs, ino, wrote, iabuf, have) != (int64_t)have) fail = 1; wrote += have; }
            break;
        }
        else if (ms() - last > (head ? 40000u : 900000u)) { fail = 2; break; }   /* el servicio convierte todo el audio antes de enviar */
        if (!head) {
            int r = yt_http_head((const char *)iabuf, have, &status, &body_off);
            if (r < 0 || (r == 0 && have >= 8000)) { fail = 3; break; }
            if (r == 1) {
                if (status != 200) {                             /* el cuerpo es el mensaje de error */
                    for (int w = 0; w < 20 && have < 600; ++w) { n = tcp_recv(h, iabuf + have, 512, 60); if (n > 0) have += (uint32_t)n; else if (n == TCP_EOF || n == TCP_ERR) break; }
                    y_errtext(err, sizeof err, (const char *)iabuf + body_off, have - body_off > 150 ? 150 : have - body_off, status);
                    tcp_close(h); ia_set(err, 1); return 0;
                }
                clen = yt_http_clen((const char *)iabuf, body_off);
                uint32_t x;
                if (bigfs_lookup(fs, WAV_PATH, &x) >= 0) bigfs_unlink(fs, WAV_PATH);
                if (bigfs_create(fs, WAV_PATH, &ino) < 0) { tcp_abort(h); ia_set("No se pudo crear el archivo de audio en el almacen", 1); return 0; }
                for (uint32_t i = body_off; i < have; ++i) iabuf[i - body_off] = iabuf[i];
                have -= body_off; head = 1;
            }
            continue;
        }
        if (have >= CH) {
            if (bigfs_write(fs, ino, wrote, iabuf, have) != (int64_t)have) { fail = 1; break; }
            wrote += have; have = 0;
            if (wrote - shown >= 512u * 1024u) {
                char m[120]; uint32_t p = 0; shown = wrote;
                yt_put(m, sizeof m, &p, "Descargando el sonido: "); mib_text(m, sizeof m, &p, wrote);
                if (clen) { yt_put(m, sizeof m, &p, " de "); mib_text(m, sizeof m, &p, clen); }
                yt_put(m, sizeof m, &p, " MiB");
                ia_set(m, 0);
            }
            thread_yield();
        }
    }
    if (fail) tcp_abort(h); else tcp_close(h);
    if (!fail && (wrote < 4000 || (clen && wrote < clen))) fail = 4;
    if (fail) {
        yt_put(err, sizeof err, &ep, fail == 1 ? "No se pudo escribir el audio en el almacen (sin sitio?)" : fail == 2 ? "El servicio YouTube no responde (tiempo agotado con el audio)"
               : fail == 3 ? "Respuesta no valida del servicio YouTube" : "La descarga del audio se ha cortado");
        bigfs_unlink(fs, WAV_PATH); ia_set(err, 1); return 0;
    }
    return 1;
}
#endif

#ifdef __x86_64__
/* SOMA_253: el sonido del video sin servicio externo. Se baja el MP4 de 360p (el unico que YouTube da directo), un segundo reproductor
 * de ring 3 (vpx_*) lo recorre solo por el audio, lo decodifica y lo deja en 16 kHz mono, y el nucleo lo va escribiendo en el
 * almacen como WAV (WAV_PATH). El archivo del video no se guarda: pasa por un anillo de 8 MiB. */
struct wsink { struct bigfs *fs; uint32_t ino; uint64_t wrote; };
static struct wsink ws;                       /* estatico: el hilo del nucleo puede tardar un poco en soltarlo */
static int wsink_write(void *ctx, const uint8_t *p, uint32_t n)
{
    struct wsink *w = (struct wsink *)ctx;
    if (bigfs_write(w->fs, w->ino, 44u + w->wrote, p, n) != (int64_t)n) return -1;
    w->wrote += n;
    return 0;
}
static void wav_header(uint8_t *h, uint64_t data)          /* 16 kHz, mono, 16 bits */
{
    static const uint8_t t[44] = { 'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x80,0x3E,0,0,0,0x7D,0,0,2,0,16,0,'d','a','t','a',0,0,0,0 };
    for (int i = 0; i < 44; ++i) h[i] = t[i];
    uint32_t d = data > 0xFFFFFF00u ? 0xFFFFFF00u : (uint32_t)data, r = d + 36u;
    for (int i = 0; i < 4; ++i) { h[4 + i] = (uint8_t)(r >> (8 * i)); h[40 + i] = (uint8_t)(d >> (8 * i)); }
}
/* 1 = WAV_PATH escrito (*got_secs de audio; *partial = la descarga se cortó); 0 = fallo (mensaje puesto) o cancelado. */
static int audio_native(const char *id, uint32_t secs, struct bigfs *fs, uint32_t *got_secs, int *partial)
{
    static struct yti_player pl; char err[200], url[YTI_URL_MAX]; const char *hdr = YTI_HDR_MEDIA; uint32_t ep = 0; err[0] = 0;
    if (yt_seq(id, NAT_DIRECT_ID)) { yt_scopy(url, sizeof url, nat_direct_url); hdr = "User-Agent: SOMA-Video/1\r\n"; }
    else {
        ia_set("Pidiendo el video a YouTube", 0);
        if (!nat_player(id, &pl, err, sizeof err, (const volatile uint32_t *)&ia_cancel, 0)) { if (ia_mine(id)) ia_set(err[0] ? err : "No se pudo pedir el video a YouTube", 1); return 0; }
        if (!ia_mine(id)) return 0;
        if (!pl.url[0]) {
            if (!pl.playable) { yt_put(err, sizeof err, &ep, "YouTube no deja bajar este video: "); yt_put(err, sizeof err, &ep, pl.reason[0] ? pl.reason : pl.status); }
            else yt_put(err, sizeof err, &ep, "YouTube no ha dado este video en un formato que SOMA sepa pedir (360p directo)");
            ia_set(err, 1); return 0;
        }
        yt_scopy(url, sizeof url, pl.url);
    }
    struct nsrc *s = (struct nsrc *)kmalloc(sizeof *s);
    if (!s) { ia_set("Sin memoria", 1); return 0; }
    s->open = 0; s->pos = 0; s->total = 0; yt_scopy(s->url, sizeof s->url, url); s->hdr = hdr;
    uint32_t ino = 0; uint8_t h[44];
    { uint32_t x; if (bigfs_lookup(fs, WAV_PATH, &x) >= 0) bigfs_unlink(fs, WAV_PATH); }
    wav_header(h, 0);
    if (bigfs_create(fs, WAV_PATH, &ino) < 0 || bigfs_write(fs, ino, 0, h, 44) != 44) { kfree(s); ia_set("No se pudo crear el archivo de audio en el almacen", 1); return 0; }
    ws.fs = fs; ws.ino = ino; ws.wrote = 0;
    struct vps_source src; src.fetch = nsrc_fetch; src.close = nsrc_close; src.ctx = s;
    if (!vpx_start(&src, secs * 1000u, wsink_write, &ws, err, sizeof err)) { bigfs_unlink(fs, WAV_PATH); ia_set(err, 1); return 0; }   /* vpx_start ya ha cerrado la fuente */
    ia_set("Bajando el sonido del video", 0);
    uint64_t t0 = ms(), last_move = ms(), last_out = 0, last_have = 0, shown = 0; int ok = 0; struct vpx_info xi;
    for (;;) {
        if (!ia_mine(id)) { vpx_stop(); bigfs_unlink(fs, WAV_PATH); return 0; }
        vpx_get_info(&xi);
        if (xi.state == VPS_ERROR) { vpx_stop(); bigfs_unlink(fs, WAV_PATH); ia_set(xi.err[0] ? xi.err : "No se pudo sacar el sonido del video", 1); return 0; }
        if (xi.state == VPS_ENDED) { ok = 1; break; }
        if (xi.state == VPS_IDLE) { bigfs_unlink(fs, WAV_PATH); ia_set("El decodificador de sonido se ha detenido", 1); return 0; }
        if (xi.out_bytes != last_out || xi.have_bytes != last_have) { last_out = xi.out_bytes; last_have = xi.have_bytes; last_move = ms(); }
        if (ms() - last_move > 60000u || (xi.state == VPS_LOADING && ms() - t0 > 90000u)) {
            vpx_stop(); bigfs_unlink(fs, WAV_PATH); ia_set("El video no llega (la red se ha quedado parada)", 1); return 0;
        }
        if (ms() - shown > 500u) {
            char m[140]; uint32_t p = 0; shown = ms();
            yt_put(m, sizeof m, &p, "Bajando y decodificando el sonido: ");
            yt_put_dec(m, sizeof m, &p, xi.pos_ms / 60000u); yt_put(m, sizeof m, &p, ":"); if ((xi.pos_ms / 1000u) % 60u < 10) yt_put(m, sizeof m, &p, "0"); yt_put_dec(m, sizeof m, &p, (xi.pos_ms / 1000u) % 60u);
            if (xi.dur_ms) { yt_put(m, sizeof m, &p, " de "); yt_put_dec(m, sizeof m, &p, xi.dur_ms / 60000u); yt_put(m, sizeof m, &p, ":"); if ((xi.dur_ms / 1000u) % 60u < 10) yt_put(m, sizeof m, &p, "0"); yt_put_dec(m, sizeof m, &p, (xi.dur_ms / 1000u) % 60u); }
            yt_put(m, sizeof m, &p, "  ("); mib_text(m, sizeof m, &p, xi.have_bytes); yt_put(m, sizeof m, &p, " MiB bajados)");
            ia_set(m, 0);
        }
        thread_sleep(100);
    }
    vpx_stop();                                               /* el hilo ya ha terminado de escribir: se cierra el WAV */
    if (!ok || ws.wrote < 32000u * 3u) { bigfs_unlink(fs, WAV_PATH); ia_set("No se ha podido sacar el sonido del video", 1); return 0; }
    wav_header(h, ws.wrote);
    if (bigfs_write(fs, ino, 0, h, 44) != 44) { bigfs_unlink(fs, WAV_PATH); ia_set("No se pudo escribir el audio en el almacen", 1); return 0; }
    *got_secs = (uint32_t)(ws.wrote / 32000u); *partial = xi.truncated != 0;
    {
        char l[160]; uint32_t q = 0;
        yt_put(l, sizeof l, &q, "[youtube] sonido de "); yt_put(l, sizeof l, &q, id); yt_put(l, sizeof l, &q, ": "); yt_put_dec(l, sizeof l, &q, *got_secs);
        yt_put(l, sizeof l, &q, " s (WAV de "); yt_put_dec(l, sizeof l, &q, (ws.wrote + 44u) / 1024u); yt_put(l, sizeof l, &q, " KiB), bajados ");
        yt_put_dec(l, sizeof l, &q, last_have / 1024u); yt_put(l, sizeof l, &q, " KiB"); yt_put(l, sizeof l, &q, *partial ? ", descarga cortada\n" : "\n");
        kprint(l);
    }
    return 1;
}
#endif

static void job_whisper(const char *id)
{
#ifdef __x86_64__
    const char *why = 0;
    if (voz_busy()) { ia_set("El reconocimiento de voz esta ocupado (la Grabadora?). Prueba al terminar o usa Subtitulos.", 1); return; }
    if (!voz_ready(&why)) {
        char m[200]; uint32_t p = 0;
        yt_put(m, sizeof m, &p, "Whisper no esta disponible: "); yt_put(m, sizeof m, &p, why ? why : "no instalado"); yt_put(m, sizeof m, &p, ". Usa Subtitulos.");
        ia_set(m, 1); return;
    }
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) { ia_set("Whisper necesita el almacen BIGFS para guardar el sonido y no esta montado. Usa Subtitulos.", 1); return; }
    struct bigfs_info bi; bigfs_info(fs, &bi);
    { uint32_t x; if (bigfs_lookup(fs, WAV_PATH, &x) >= 0) { bigfs_unlink(fs, WAV_PATH); bigfs_info(fs, &bi); } }
    uint64_t fre = bi.free_clusters * (uint64_t)bi.cluster_bytes;
    uint32_t want = (uint32_t)conf.max_min * 60u, secs = now_it.dur && now_it.dur < want ? now_it.dur + 2 : want;
    int cut = now_it.dur == 0 || now_it.dur > want; uint32_t tdur = now_it.dur;
    if (fre < 3u * 1024u * 1024u) { ia_set("No queda sitio en el almacen para el sonido del video. Usa Subtitulos.", 1); return; }
    uint64_t fit = (fre - 2u * 1024u * 1024u) / 32000u;          /* 16 kHz, 16 bits, mono: 32 000 bytes por segundo */
    if (fit < secs) { secs = (uint32_t)fit; cut = 1; }
    if (secs < 15) { ia_set("No queda sitio en el almacen para el sonido del video. Usa Subtitulos.", 1); return; }
    if (native_mode() || yt_seq(id, NAT_DIRECT_ID)) {                    /* SOMA_253: sin servicio externo */
        uint32_t gs = 0; int part = 0;
        if (!audio_native(id, secs, fs, &gs, &part)) return;
        if (part) { cut = 1; secs = gs; }
        else if (gs + 3 < secs) { cut = 0; secs = gs; tdur = gs; }          /* el video entero cabia: no se ha cortado nada */
        else cut = 1;
    } else {
        ia_set("El servicio esta preparando el sonido del video", 0);
        if (!audio_fetch(id, secs, fs)) return;
    }
    if (!ia_mine(id)) { bigfs_unlink(fs, WAV_PATH); return; }
    {
        char m[160]; uint32_t p = 0;
        yt_put(m, sizeof m, &p, "Transcribiendo con whisper ");
        if (cut) { yt_put(m, sizeof m, &p, "los primeros "); yt_put_dec(m, sizeof m, &p, secs / 60u); yt_put(m, sizeof m, &p, " min"); }
        else { char d[16]; yt_fmt_time(d, sizeof d, tdur); yt_put(m, sizeof m, &p, "("); yt_put(m, sizeof m, &p, d); yt_put(m, sizeof m, &p, " de sonido)"); }
        ia_set(m, 0);
    }
    char args[64]; uint32_t ap = 0; int64_t code = 0;
    yt_put(args, sizeof args, &ap, "-T -l "); yt_put(args, sizeof args, &ap, conf.lang); yt_put(args, sizeof args, &ap, " " WAV_PATH);
    iabuf[0] = 0;
    int r = voz_run(args, (char *)iabuf, IA_CAP, &code);
    bigfs_unlink(fs, WAV_PATH);
    if (!ia_mine(id)) return;
    char *tmp = (char *)kmalloc(TX_CAP); char info[160];
    if (!tmp) { ia_set("Sin memoria", 1); return; }
    uint32_t n = yt_voz_text((const char *)iabuf, tmp, TX_CAP, info, sizeof info);
    if (r == 0 && n) {
        char src[96]; uint32_t sp = 0;
        yt_put(src, sizeof src, &sp, "whisper");
        if (cut) { yt_put(src, sizeof src, &sp, ", primeros "); yt_put_dec(src, sizeof src, &sp, secs / 60u); yt_put(src, sizeof src, &sp, " min"); }
        tx_set(id, tmp, n, src);
        tx_save(id);
        ia_set("", 0);
    } else {
        char m[200]; uint32_t p = 0;
        yt_put(m, sizeof m, &p, "Whisper no ha sacado texto");
        if (info[0]) { yt_put(m, sizeof m, &p, ": "); yt_put(m, sizeof m, &p, info); }
        ia_set(m, 1);
    }
    kfree(tmp);
#else
    (void)id;
    ia_set("Whisper solo existe en x86_64. Usa Subtitulos.", 1);
#endif
}

static void ytia_main(void *arg)
{
    (void)arg;
    uint64_t idle_since = ms();
    for (;;) {
        if (ia_done == ia_req) {
            if (ui_gone() && ms() - idle_since > 10000u) break;
            thread_sleep(20); continue;
        }
        uint32_t g = ia_req; int kind = ia_next;
        static char a[320]; char id[YT_ID_LEN + 1];
        yt_scopy(a, sizeof a, ia_next_arg); yt_scopy(id, sizeof id, ia_next_id);
        ia_next = J_NONE; ia_cancel = 0; ia_run = kind; ia_t0 = ms(); ia_seq++;
        if (!iabuf) iabuf = (uint8_t *)kmalloc(IA_CAP + 16);
        if (!iabuf) ia_set("Sin memoria", 1);
        else if (kind == J_INFO) { if (ia_mine(id)) (void)ensure_info(id); }
        else if (kind == J_SUM) job_sum(id);
        else if (kind == J_ASK) job_ask(id, a);
        else if (kind == J_SUBS) job_subs(id);
        else if (kind == J_WHISPER) job_whisper(id);
        ia_run = J_NONE; ia_seq++;
        if (ia_req == g) ia_done = g;                            /* si no, ha llegado otro trabajo mientras tanto */
        idle_since = ms();
    }
    ia_alive = 0;
}

/* Pide un trabajo. Con "preempt" se abandona el que este en marcha; si no, espera su turno (solo se guarda el ultimo pedido). */
static int ia_start(int kind, const char *arg, int preempt)
{
    threads_up();
    ia_next = kind; yt_scopy(ia_next_arg, sizeof ia_next_arg, arg ? arg : ""); yt_scopy(ia_next_id, sizeof ia_next_id, vd_id);
    if (preempt && ia_run != J_NONE) {
        ia_cancel = 1;
#ifdef __x86_64__
        if (ia_run == J_WHISPER) voz_stop();
#endif
    }
    ia_req++; ia_seq++;
    if (!ia_alive) { ia_alive = 1; if (!thread_create("ytia", ytia_main, 0)) { ia_alive = 0; ia_set("No se pudo crear el hilo de la IA", 1); return 0; } }
    return 1;
}
static int ia_busy(void) { return ia_run != J_NONE || ia_done != ia_req || nl_run; }

static void ytnl_main(void *arg)
{
    (void)arg;
    uint64_t idle_since = ms();
    for (;;) {
        if (nl_done == nl_req) {
            if (ui_gone() && ms() - idle_since > 10000u) break;
            thread_sleep(20); continue;
        }
        uint32_t g = nl_req; char t[160];
        yt_scopy(t, sizeof t, nl_text);
        nl_cancel = 0; nl_run = 1;
        job_nl(t);
        nl_run = 0; ia_seq++;
        if (nl_req == g) nl_done = g;
        idle_since = ms();
    }
    nl_alive = 0;
}
static void nl_start(const char *q)                              /* una busqueda nueva abandona la anterior */
{
    threads_up();
    yt_scopy(nl_text, sizeof nl_text, q);
    if (nl_run) nl_cancel = 1;
    nl_req++; ia_seq++;
    if (!nl_alive) { nl_alive = 1; if (!thread_create("ytnl", ytnl_main, 0)) { nl_alive = 0; ai_state = AI_PLAIN; yt_scopy(ai_why, sizeof ai_why, "no se pudo crear el hilo"); } }
}
static int ia_heavy(void) { return ia_run == J_WHISPER || ia_run == J_SUBS || ia_run == J_SUM || ia_run == J_ASK; }
static void ia_stop(void)                                        /* boton Cancelar */
{
    if (ia_run == J_NONE) return;
    ia_cancel = 1; ia_next = J_NONE;
#ifdef __x86_64__
    if (ia_run == J_WHISPER) voz_stop();
#endif
    ia_set("Cancelado", 0);
}
