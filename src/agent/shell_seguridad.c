/* shell_seguridad.c: modelo de lenguaje (llm-config), navegador aislado, membench, red de los agentes, rng, memprot y criptografia WPA.
 * SOMA_233_SHELL_PARTIDO: NO se compila por separado. Lo incluye src/agent/shell.c en su sitio (una sola unidad de
 * compilacion: usa las funciones auxiliares y la tabla de ordenes de shell.c, y el nucleo compilado sale identico). */
/* SOMA_195: "llm-config" - con que modelo de lenguaje habla SOMA (solo el usuario: no es una orden de agente) */
static char llmcfg_models[4096]; static int llmcfg_nmodels;
static void llmcfg_show(const struct llm_conf *c)
{
    kprint("Modelo de lenguaje de SOMA\n");
    for (int i = 0; i < LLM_NPROV; i++) {
        uint32_t kl = 0; while (c->key[i][kl]) kl++;
        kprint(i == c->prov ? "  * " : "    "); kprint(llm_prov_name(i)); kprint(i == LLM_LOCAL ? "   " : "  ");
        kprint(llm_prov_label(i)); kprint(": "); kprint(c->base[i]);
        kprint(" | modelo: "); kprint(c->model[i][0] ? c->model[i] : "(sin elegir)");
        kprint(" | clave: "); if (kl) { kprint("puesta (termina en "); kprint(c->key[i] + (kl > 4 ? kl - 4 : 0)); kprint(")"); } else kprint(i == LLM_LOCAL ? "no hace falta" : "FALTA");
        kprint("\n");
    }
    kprint("uso: llm-config proveedor <local|openai|claude|gemini> | servidor <url o ip:puerto> | modelo <nombre o numero de la lista>\n"
           "     llm-config clave <clave de la API> | archivo-clave <ruta> | modelos (pide la lista al proveedor) | probar\n"
           "     llm-config lanzador (arrancar llama-server en el servidor de la red local) | lanzar <modelo> | parar\n");
}
static int cmd_llm_config(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    static struct llm_conf c; c = *llm_conf_get();
    while (*args == ' ') args++;
    const char *field = shell_starts_with(args, "servidor") ? "servidor" : shell_starts_with(args, "modelos") ? 0 : shell_starts_with(args, "modelo") ? "modelo" : shell_starts_with(args, "clave") ? "clave" : 0;
    if (!*args) { llmcfg_show(&c); return 1; }
    if (shell_starts_with(args, "lanzador")) {                                   /* SOMA_196: lanzador del servidor local */
        const char *a = args + 8; while (*a == ' ') a++;
        const char *lf = shell_starts_with(a, "clave") ? "clave-lanzador" : shell_starts_with(a, "servidor") ? "lanzador" : 0;
        if (lf) {
            const char *v = a; while (*v && *v != ' ') v++;
            while (*v == ' ') v++;
            if (llm_conf_set(&c, 0, lf, v) != 0) { kprint("llm-config: valor no valido\n"); return 1; }
            if (llm_conf_save(&c) != 0) kprint("llm-config: no se pudo guardar /etc/llm.conf (el cambio vale hasta reiniciar)\n");
            kprint("llm-config: guardado\n");
            return 1;
        }
        char lb[LLM_BASE_MAX]; llm_launcher_base(&c, lb, sizeof lb);
        if (shell_starts_with(a, "modelos")) {
            llmcfg_nmodels = 0;
            int n = llm_launcher_models(llmcfg_models, sizeof llmcfg_models);
            if (n < 0) { kprint(llm_last_error); kprint("\n"); return 1; }
            llmcfg_nmodels = n;
            const char *m = llmcfg_models;
            for (int i = 1; i <= n; i++) {
                kprint(i < 10 ? "   " : "  "); kprint_dec((uint32_t)i); kprint("  ");
                char one[LLM_MODEL_MAX + 1]; uint32_t k = 0; while (*m && *m != '\n' && k < LLM_MODEL_MAX) one[k++] = *m++;
                one[k] = 0; if (*m) m++;
                kprint(one); kprint("\n");
            }
            kprint(n ? "arranca uno con:  llm-config lanzar <numero o nombre>\n" : "el lanzador no encuentra modelos .gguf en el servidor\n");
            return 1;
        }
        kprint("Lanzador del servidor local: "); kprint(lb); kprint(c.launcher[0] ? "" : " (por defecto)"); kprint(" | clave: "); kprint(c.launcher_key[0] ? "puesta" : "FALTA"); kprint("\n");
        if (c.launcher_key[0]) {
            char mo[LLM_MODEL_MAX]; int st = llm_launcher_status(mo, sizeof mo);
            kprint("estado de llama-server: "); kprint(st == 1 ? "LISTO" : st == 2 ? "cargando un modelo" : st == 0 ? "parado" : "ERROR");
            if (st > 0 && mo[0]) { kprint(", modelo "); kprint(mo); }
            kprint("\n"); if (st < 0) { kprint(llm_last_error); kprint("\n"); }
        }
        kprint("uso: llm-config lanzador clave <clave que muestra soma_lanzador.py> | lanzador servidor <ip:puerto> | lanzador modelos\n"
               "     llm-config lanzar <numero o nombre> (arranca llama-server con ese modelo) | llm-config parar\n");
        return 1;
    }
    if (shell_starts_with(args, "lanzar")) {
        const char *v = args + 6; while (*v == ' ') v++;
        static char pick[LLM_MODEL_MAX];
        if (*v >= '0' && *v <= '9' && llmcfg_nmodels) {
            int idx = 0; const char *d = v; while (*d >= '0' && *d <= '9') idx = idx * 10 + (*d++ - '0');
            if (!*d && idx >= 1 && idx <= llmcfg_nmodels) {
                const char *m = llmcfg_models; uint32_t k = 0;
                for (int i = 1; i < idx; i++) { while (*m && *m != '\n') m++; if (*m) m++; }
                while (m[k] && m[k] != '\n' && k < sizeof pick - 1) { pick[k] = m[k]; k++; }
                pick[k] = 0; v = pick;
            }
        }
        if (!*v) { kprint("uso: llm-config lanzar <numero de `llm-config lanzador modelos` o nombre>\n"); return 1; }
        kprint("llm-config: arrancando llama-server con "); kprint(v); kprint(" (cargar el modelo puede tardar un par de minutos) ...\n");
        if (llm_launcher_start(v, 240000, 0)) { llmcfg_nmodels = 0; kprint("llm-config: servidor LISTO con "); kprint(v); kprint(". Queda elegido como modelo local.\n"); }
        else { kprint("llm-config: no arranco. "); kprint(llm_last_error); kprint("\n"); }
        return 1;
    }
    if (shell_starts_with(args, "parar")) {
        if (llm_launcher_stop() == 0) kprint("llm-config: llama-server parado (VRAM libre)\n"); else { kprint(llm_last_error); kprint("\n"); }
        return 1;
    }
    if (shell_starts_with(args, "proveedor")) {
        int p = llm_prov_by_name(args + 9);
        if (p < 0) { kprint("llm-config: proveedores: local, openai, claude, gemini\n"); return 1; }
        c.prov = p; llmcfg_nmodels = 0;
        if (llm_conf_save(&c) != 0) kprint("llm-config: no se pudo guardar /etc/llm.conf (el cambio vale hasta reiniciar)\n");
        llmcfg_show(&c);
        return 1;
    }
    if (field) {
        const char *v = args; while (*v && *v != ' ') v++;
        while (*v == ' ') v++;
        if (field[0] == 'm' && *v >= '0' && *v <= '9' && llmcfg_nmodels) {            /* numero de la ultima lista */
            int idx = 0; const char *d = v; while (*d >= '0' && *d <= '9') idx = idx * 10 + (*d++ - '0');
            if (!*d && idx >= 1 && idx <= llmcfg_nmodels) {
                static char pick[LLM_MODEL_MAX]; const char *m = llmcfg_models; uint32_t k = 0;
                for (int i = 1; i < idx; i++) { while (*m && *m != '\n') m++; if (*m) m++; }
                while (m[k] && m[k] != '\n' && k < sizeof pick - 1) { pick[k] = m[k]; k++; }
                pick[k] = 0; v = pick;
            }
        }
        if (llm_conf_set(&c, c.prov, field, v) != 0) { kprint("llm-config: valor no valido\n"); return 1; }
        if (llm_conf_save(&c) != 0) kprint("llm-config: no se pudo guardar /etc/llm.conf (el cambio vale hasta reiniciar)\n");
        llmcfg_show(&c);
        return 1;
    }
    if (shell_starts_with(args, "archivo-clave")) {                              /* la clave desde un archivo (p. ej. de un USB): no hay que teclearla */
        const char *p = args + 13; while (*p == ' ') p++;
        static char kb[LLM_KEY_MAX + 8]; int n = *p ? vfs_read(p, kb, sizeof kb - 1) : -1;
        if (n <= 0) { kprint("uso: llm-config archivo-clave <ruta de un archivo de texto que solo contiene la clave>\n"); return 1; }
        kb[n] = 0; while (n > 0 && (kb[n - 1] == '\n' || kb[n - 1] == '\r' || kb[n - 1] == ' ' || kb[n - 1] == '\t')) kb[--n] = 0;
        const char *v = kb; while (*v == ' ' || *v == '\t') v++;
        int r = llm_conf_set(&c, c.prov, "clave", v);
        memset(kb, 0, sizeof kb);
        if (r != 0) { kprint("llm-config: ese archivo no contiene una clave valida (una sola linea)\n"); return 1; }
        if (llm_conf_save(&c) != 0) kprint("llm-config: no se pudo guardar /etc/llm.conf (el cambio vale hasta reiniciar)\n");
        llmcfg_show(&c);
        return 1;
    }
    if (shell_starts_with(args, "modelos")) {
        kprint("llm-config: pidiendo la lista de modelos a "); kprint(c.base[c.prov]); kprint(" ...\n");
        int n = llm_list_models(llmcfg_models, sizeof llmcfg_models);
        if (n < 0) { llmcfg_nmodels = 0; kprint(llm_last_error); kprint("\n"); return 1; }
        llmcfg_nmodels = n;
        const char *m = llmcfg_models;
        for (int i = 1; i <= n; i++) {
            kprint(i < 10 ? "   " : i < 100 ? "  " : " "); kprint_dec((uint32_t)i); kprint("  ");
            char one[LLM_MODEL_MAX + 1]; uint32_t k = 0; while (*m && *m != '\n' && k < LLM_MODEL_MAX) one[k++] = *m++;
            one[k] = 0; if (*m) m++;
            kprint(one); kprint("\n");
        }
        kprint(n ? "elige uno con:  llm-config modelo <numero o nombre>\n" : "el proveedor no devolvio ningun modelo\n");
        return 1;
    }
    if (shell_starts_with(args, "probar")) {
        static char reply[512];
        kprint("llm-config: probando "); kprint(llm_prov_label(c.prov)); kprint(", modelo "); kprint(c.model[c.prov][0] ? c.model[c.prov] : "(sin elegir)"); kprint(" ...\n");
        uint64_t t0 = timer_get_uptime_ms();
        if (llm_chat("Responde unicamente con estas tres palabras: SOMA TEST OK", reply, sizeof reply, 60000)) {
            kprint("llm-config: FUNCIONA ("); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms). Respuesta: "); kprint(reply); kprint("\n");
        } else { kprint("llm-config: NO funciona. "); kprint(llm_last_error); kprint("\n"); }
        return 1;
    }
    llmcfg_show(&c);
    return 1;
}

#if defined(__x86_64__)
/* SOMA_202: "navegador-aislado" - el motor del navegador en un proceso de ring 3 (solo el usuario) */
static int cmd_nav_aislado(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out; if (out_buf) out_buf[0] = 0;
    while (*args == ' ') args++;
    int on = shell_starts_with(args, "on") || shell_starts_with(args, "si"), off = shell_starts_with(args, "off") || shell_starts_with(args, "no");
    if (on || off) {
        const char *t = on ? "# Navegador de SOMA (parche 202): navegador-aislado on | off\naislado=si\n" : "# Navegador de SOMA (parche 202): navegador-aislado on | off\naislado=no\n";
        uint32_t n = 0; while (t[n]) n++;
        if (vfs_write("/etc/navegador.conf", t, n) < 0) { kprint("navegador-aislado: no se pudo guardar /etc/navegador.conf\n"); return 1; }
        kprint(on ? "navegador-aislado: ACTIVADO. " : "navegador-aislado: DESACTIVADO (el motor correra dentro del nucleo, como antes del parche 202). ");
        kprint(browser_r3_active() == on ? "\n" : "Vale desde el proximo arranque (el navegador de esta sesion ya esta en marcha).\n");
        return 1;
    }
    uint32_t st = 0, de = 0, hp = 0, sp = 0; int64_t le = 0; web_r3_stats(&st, &de, &le, &hp, &sp);
    kprint("Navegador aislado (motor web en un proceso de ring 3)\n  configurado: "); kprint(browser_r3_wanted() ? "si" : "no");
    kprint("\n  esta sesion: "); kprint(browser_r3_active() ? "AISLADO" : "el navegador aun no se ha abierto, o corre dentro del nucleo");
    kprint("\n  proceso:     "); kprint(web_r3_alive() ? "en marcha" : "parado"); kprint(", lanzado "); kprint_dec(st); kprint(" veces, caidas "); kprint_dec(de);
    kprint("\n  memoria:     monton "); kprint_dec(hp * 4u); kprint(" KiB, superficie "); kprint_dec(sp * 4u); kprint(" KiB\n");
    { uint32_t fr = 0, pl = 0, pa = 0, fh = 0, sv = 0, sl = 0; web_r3_stats2(&fr, &pl, &pa, &fh, &sv, &sl);      /* SOMA_203 */
      kprint("  fotogramas:  "); kprint_dec(fr); kprint(" pintados, ultimo "); kprint_dec(pl); kprint(" ms, media "); kprint_dec(pa); kprint(" ms, alto "); kprint_dec(fh); kprint(" px\n");
      kprint("  a salvo:     cookies y almacenamiento, "); kprint_dec(sv); kprint(" copias, la ultima de "); kprint_dec(sl); kprint(" bytes\n"); }
    kprint("uso: navegador-aislado on | off     (prueba sin interfaz: webr3 <url>)\n");
    return 1;
}
/* SOMA_202: "webr3 <url>" - carga una pagina con el motor del navegador en ring 3, sin interfaz, y dice que salio */
static int cmd_webr3(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out; if (out_buf) out_buf[0] = 0;
    while (*args == ' ') args++;
    if (agent_net_denied("webr3", args, out_buf, max_out)) return 1;
    if (!*args) { kprint("uso: webr3 <url>   (prueba del navegador aislado: carga la pagina en el proceso de ring 3 y muestra titulo, fotogramas y memoria)\n"); return 1; }
    (void)web_r3_selftest(args);
    return 1;
}
#endif

/* SOMA_199: "membench" - coste de memcpy/memset/memmove/memcmp por tamano (ciclos por llamada, de la mejor de varias tandas) */
static int cmd_membench(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out; if (out_buf) out_buf[0] = 0;
    static const uint32_t sz[] = { 4, 8, 16, 24, 48, 64, 128, 256, 1024, 4096, 65536 };
    static uint8_t *a, *b;
    if (!a) { a = (uint8_t *)kmalloc(70000); b = (uint8_t *)kmalloc(70000); }
    if (!a || !b) { kprint("membench: sin memoria\n"); return 1; }
    for (uint32_t i = 0; i < 70000; i++) { a[i] = (uint8_t)(i * 7); b[i] = (uint8_t)(i * 7); }
    kprint("membench: ciclos por llamada (menos es mejor)\n  bytes    memcpy    memset   memmove<   memmove>    memcmp\n");
    void *(*volatile cpy)(void *, const void *, size_t) = memcpy; void *(*volatile set)(void *, int, size_t) = memset;
    void *(*volatile mov)(void *, const void *, size_t) = memmove; int (*volatile cmp)(const void *, const void *, size_t) = memcmp;
    volatile int sink = 0;
    for (uint32_t k = 0; k < sizeof sz / sizeof sz[0]; k++) {
        uint32_t n = sz[k], reps = n <= 256 ? 2000 : n <= 4096 ? 400 : 40;
        uint64_t best[5];
        for (int op = 0; op < 5; op++) {
            best[op] = ~0ull;
            for (int round = 0; round < 5; round++) {
                uint64_t t0 = arch_cycle_counter();
                for (uint32_t r = 0; r < reps; r++) {
                    uint32_t o = (r * 13) & 63;
                    if (op == 0) cpy(b + 300 + o, a + ((r * 5) & 63), n);
                    else if (op == 1) set(b + 300 + o, (int)r, n);
                    else if (op == 2) mov(a + 100 + o, a + 100 + o + 3, n);
                    else if (op == 3) mov(a + 100 + o + 3, a + 100 + o, n);
                    else sink += cmp(a + 2000, b + 68000 - n - 1 - (n & 1), n) != 0;
                }
                uint64_t dt = (arch_cycle_counter() - t0) / reps;
                if (dt < best[op]) best[op] = dt;
            }
        }
        char line[96]; uint32_t p = 0;
        for (int c = -1; c < 5; c++) {
            uint64_t v = c < 0 ? n : best[c]; char d[20]; int nd = 0;
            do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v && nd < 19);
            for (int sp = nd; sp < (c < 0 ? 7 : 10); sp++) line[p++] = ' ';
            while (nd) line[p++] = d[--nd];
        }
        line[p++] = '\n'; line[p] = 0; kprint(line);
    }
    (void)sink;
    return 1;
}

/* SOMA_193: "agente_red" - politica de destinos de red de los agentes (solo el usuario: no es una orden de agente) */
static int cmd_agente_red(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    agent_net_cfg_load();
    while (*args == ' ') args++;
    int changed = 0;
    if (shell_starts_with(args, "abierto")) { agent_net_set_mode(AGENT_NET_ABIERTO); changed = 1; }
    else if (shell_starts_with(args, "procedencia")) { agent_net_set_mode(AGENT_NET_PROCEDENCIA); changed = 1; }
    else if (shell_starts_with(args, "cerrado")) { agent_net_set_mode(AGENT_NET_CERRADO); changed = 1; }
    else if (shell_starts_with(args, "permitir ")) {
        if (agent_net_allow_add(args + 9) != 0) { kprint("agente_red: dominio no valido o lista llena (ej.: agente_red permitir wikipedia.org)\n"); return 1; }
        changed = 1;
    } else if (shell_starts_with(args, "quitar ")) {
        if (agent_net_allow_del(args + 7) != 0) { kprint("agente_red: ese dominio no esta en la lista\n"); return 1; }
        changed = 1;
    } else if (*args) { kprint("uso: agente_red | agente_red abierto|procedencia|cerrado | agente_red permitir <dominio> | agente_red quitar <dominio>\n"); return 1; }
    if (changed && agent_net_cfg_save() != 0) kprint("agente_red: no se pudo guardar /etc/agente_red.conf (el cambio vale hasta reiniciar)\n");
    int m = agent_net_mode(); uint32_t seen = 0, den = 0; agent_net_stats(&seen, &den);
    kprint("Destinos de red de los agentes: modo ");
    kprint(m == AGENT_NET_ABIERTO ? "ABIERTO (cualquier destino; sin proteccion)\n" : m == AGENT_NET_CERRADO ? "CERRADO (solo los dominios de confianza)\n"
           : "PROCEDENCIA (URL de la mision, de resultados de busqueda o de paginas ya descargadas, y dominios de confianza)\n");
    kprint("  dominios de confianza:");
    if (!agent_net_allow_get(0)) kprint(" (ninguno)");
    for (int i = 0; agent_net_allow_get(i); ++i) { kprint(" "); kprint(agent_net_allow_get(i)); }
    kprint("\n  URL registradas: "); kprint_dec(seen); kprint(" | destinos denegados: "); kprint_dec(den); kprint("\n");
    return 1;
}

/* SOMA_192: "rng" - estado del generador aleatorio del nucleo */
static int cmd_rng(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out;
    if (out_buf) out_buf[0] = 0;
    struct rng_info ri; rng_get_info(&ri);
    int st = rng_selftest();
    uint8_t s[16]; rng_bytes(s, sizeof s);
    kprint("Generador aleatorio del nucleo (ChaCha20, clave nueva tras cada peticion)\n  fuentes de hardware: RDSEED ");
    kprint(ri.rdseed ? "si" : "no"); kprint(", RDRAND "); kprint(ri.rdrand ? "si" : "no");
    kprint((ri.rdseed || ri.rdrand) ? "\n" : "  -> solo ruido de tiempos y de red (en QEMU: -cpu max; la Pi 5 no tiene)\n");
    kprint("  siembras: "); kprint_dec(ri.reseeds); kprint(" | sucesos mezclados: "); kprint_dec(ri.events);
    kprint(" | bytes entregados: "); kprint_dec((uint32_t)ri.bytes_out);
    kprint("\n  autoprueba (vector del RFC 8439 y salidas distintas): "); kprint(st == 0 ? "correcta" : "FALLO");
    kprint("\n  muestra: "); for (int i = 0; i < 16; i++) kprint_hex8(s[i]);
    kprint("\n");
    return 1;
}

#ifdef __x86_64__
/* SOMA_192: "memprot" - repite la pasada de proteccion de memoria y muestra el resultado */
static int cmd_memprot(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out;
    if (out_buf) out_buf[0] = 0;
    memprot_apply();
    struct memprot_info mi; memprot_get_info(&mi);
    if (mi.mode == 0) { kprint("Proteccion de memoria del nucleo: DESACTIVADA (soma.memprot=off o modo seguro)\n"); return 1; }
    kprint("Proteccion de memoria del nucleo (W^X)\n  ejecutable: solo .text, "); kprint_dec((uint32_t)mi.text_kib); kprint(" KiB");
    kprint(mi.wx_text ? "  (AVISO: parte de .text es escribible)\n" : " (ninguno escribible)\n");
    kprint("  retirado el permiso de ejecucion a "); kprint_dec((uint32_t)(mi.exec_fixed / 1024)); kprint(" MiB en "); kprint_dec(mi.sweeps); kprint(" pasadas");
    kprint("\n  ejecutable fuera de .text ahora: "); kprint_dec((uint32_t)mi.exec_left); kprint(mi.exec_left ? " KiB  <-- AVISO\n" : " KiB\n");
    kprint("  pagina 0 (punteros nulos): "); kprint(mi.mode == 2 ? "sin mapear (soma.pagina0=estricta): leer o escribir detiene el sistema\n" : "solo lectura: escribir detiene el sistema (soma.pagina0=estricta para que tambien fallen las lecturas)\n");
    return 1;
}
#endif

static void hexline(const uint8_t *b, int n) { static const char *h = "0123456789abcdef"; char t[3] = { 0, 0, 0 }; for (int i = 0; i < n; i++) { t[0] = h[b[i] >> 4]; t[1] = h[b[i] & 15]; kprint(t); } kprint("\n"); }
static int cmd_wpa(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    if (args && args[0] == 'p' && args[1] == 's' && args[2] == 'k' && args[3] == ' ') {
        /* wpa psk <ssid> <contrasena>: la PMK, como wpa_passphrase (el SSID sin espacios en esta orden) */
        const char *p = args + 4; while (*p == ' ') p++;
        char ssid[33]; int n = 0; while (*p && *p != ' ' && n < 32) ssid[n++] = *p++; ssid[n] = 0;
        while (*p == ' ') p++;
        uint8_t pmk[32]; uint64_t t0 = timer_ticks;
        if (!n || wpa_psk_to_pmk(p, (const uint8_t *)ssid, (size_t)n, pmk)) { kprint("uso: wpa psk <ssid> <contrasena de 8 a 63 caracteres>\n"); return 1; }
        kprint("PMK de '"); kprint(ssid); kprint("' ("); kprint_dec((uint32_t)(timer_ticks - t0)); kprint(" ms): "); hexline(pmk, 32);
        return 1;
    }
    /* wpa / wpa prueba: vectores oficiales ejecutados en esta maquina */
    int ok = 1; uint8_t d[32], e[32];
    sha1("abc", 3, d);
    { static const uint8_t r[20] = { 0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d }; ok &= !memcmp(d, r, 20); }
    kprint(ok ? "SHA-1 ............ bien\n" : "SHA-1 ............ MAL\n");
    { uint8_t k[16], p[16], c[16]; for (int i = 0; i < 16; i++) { k[i] = (uint8_t)i; p[i] = (uint8_t)(i * 0x11); }
      static const uint8_t r[16] = { 0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };
      struct aes128 a; aes128_init(&a, k); aes128_encrypt(&a, p, c); aes128_decrypt(&a, c, e);
      int g = !memcmp(c, r, 16) && !memcmp(e, p, 16); ok &= g; kprint(g ? "AES-128 .......... bien\n" : "AES-128 .......... MAL\n"); }
    { uint64_t t0 = timer_ticks; wpa_psk_to_pmk("password", (const uint8_t *)"IEEE", 4, d); uint32_t ms = (uint32_t)(timer_ticks - t0);
      static const uint8_t r[8] = { 0xf4,0x2c,0x6f,0xc5,0x2d,0xf0,0xeb,0xef };
      int g = !memcmp(d, r, 8); ok &= g; kprint(g ? "PBKDF2 (PMK) ..... bien, " : "PBKDF2 (PMK) ..... MAL, "); kprint_dec(ms); kprint(" ms\n"); }
    { uint8_t kek[16], kd[16], w[24], u[16]; for (int i = 0; i < 16; i++) { kek[i] = (uint8_t)i; kd[i] = (uint8_t)(i * 0x11); }
      aes_key_wrap(kek, kd, 16, w);
      int g = w[0] == 0x1f && w[1] == 0xa6 && w[23] == 0xe5 && aes_key_unwrap(kek, w, 16, u) == 0 && !memcmp(u, kd, 16); ok &= g;
      kprint(g ? "AES Key Wrap ..... bien\n" : "AES Key Wrap ..... MAL\n"); }
    kprint(ok ? "wpa: criptografia de WPA2 correcta en esta maquina ('wpa psk <ssid> <clave>' calcula la PMK)\n" : "wpa: HAY FALLOS\n");
    return 1;
}

