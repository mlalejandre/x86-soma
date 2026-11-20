/* shell_agente.c: agentes y modelo: soma, mayordomo, llm, fuentes del sistema (src), entorno y source.
 * SOMA_233_SHELL_PARTIDO: NO se compila por separado. Lo incluye src/agent/shell.c en su sitio (una sola unidad de
 * compilacion: usa las funciones auxiliares y la tabla de ordenes de shell.c, y el nucleo compilado sale identico). */
static int cmd_soma(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    agent_run("soma", args);
    return 1;
}

static int cmd_buscador(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    agent_run("buscador", args);
    return 1;
}

static int cmd_mayordomo(const char *args, char *out_buf, uint32_t max_out)
{
    if (!args || !*args) {
        kprint("Uso: mayordomo <tarea del sistema>\n");
        if (out_buf && max_out) {
            const char *msg = "Uso: mayordomo <tarea del sistema>";
            uint32_t p = 0; while (msg[p] && p + 1 < max_out) { out_buf[p] = msg[p]; ++p; }
            out_buf[p] = '\0';
        }
        return 1;
    }
    agent_run("mayordomo", args);
    if (out_buf && max_out) {
        uint32_t p = 0;
        while (agent_last_response[p] && p + 1 < max_out) { out_buf[p] = agent_last_response[p]; ++p; }
        out_buf[p] = '\0';
    }
    return 1;
}

static int cmd_agent(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    while (*args == ' ') args++;
    char target_agent[32];
    uint32_t ap = 0;
    while (*args && *args != ' ' && ap < sizeof(target_agent) - 1) {
        target_agent[ap++] = *args++;
    }
    target_agent[ap] = '\0';
    while (*args == ' ') args++;

    if (str_eq(target_agent, "buscador")) {
        agent_run("buscador", args);
    } else if (str_eq(target_agent, "soma")) {
        agent_run("soma", args);
    } else if (str_eq(target_agent, "mayordomo")) {
        agent_run("mayordomo", args);
    } else {
        /* Si no coincide con un nombre de agente, invocar a soma con la frase completa */
        agent_run("soma", target_agent[0] ? target_agent : args);
    }
    return 1;
}

static int cmd_health(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    int ok = llm_health();
    kprint(ok ? "Servidor LLM: OK\n" : "Servidor LLM: ERROR\n");
    if (!ok) { kprint(llm_last_error); kprint("\nSe elige con la orden llm-config o en Ajustes > IA.\n"); }   /* SOMA_195 */
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, ok ? "Servidor LLM: OK" : "Servidor LLM: ERROR");
    return 1;
}

static int cmd_llm(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: llm <mensaje>\n");
        return 1;
    }
    char *reply = (char *)kmalloc(16384);
    if (!reply) {
        kprint("llm: sin memoria.\n");
        return 1;
    }
    kprint("Consultando a nail-35b...\n");
    if (llm_chat(args, reply, 16384, 35000)) {
        kprint("\n[nail-35b]: "); kprint(reply); kprint("\n\n");
        uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, reply);
    } else {
        kprint("Error al consultar el LLM.\n");
    }
    kfree(reply);
    return 1;
}

static int cmd_llm_diag(const char *args, char *out_buf, uint32_t max_out)
{
    static char reply[4096];
    kprint("Solicitando diagnostico al LLM...\n");
    if (llm_diagnose(args, reply, sizeof(reply), 35000)) {
        kprint("\n[DIAGNOSTICO IA]:\n"); kprint(reply); kprint("\n\n");
        uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, reply);
    } else {
        kprint("Error al solicitar diagnostico al LLM.\n");
    }
    return 1;
}

static int cmd_src_ls(const char *args, char *out_buf, uint32_t max_out)
{
    /* SOMA_PARCHE_011_SRCFS: src_ls [prefijo]. out_buf (2 KiB) es para la GUI y se corta con aviso;
     * la consola recibe el listado completo desde un buffer mayor. */
    char prefix[48];
    uint32_t k = 0;
    while (*args == ' ') args++;
    while (*args && *args != ' ' && *args != '\n' && *args != '\r' && k < sizeof(prefix) - 1) prefix[k++] = *args++;
    prefix[k] = '\0';

    srcfs_ls_filter(k ? prefix : 0, out_buf, max_out);
    char *big = (char *)kmalloc(16384);
    if (big) {
        srcfs_ls_filter(k ? prefix : 0, big, 16384);
        kprint(big);
        kfree(big);
    } else {
        kprint(out_buf);
    }
    kputc('\n');
    return 1;
}

static int cmd_src_cat(const char *args, char *out_buf, uint32_t max_out)
{
    char full[128]; uint32_t p = 0;
    soma_str_put(full, sizeof(full), &p, "src_cat ");
    soma_str_put(full, sizeof(full), &p, args);
    src_tool(full, out_buf, max_out);
    return 1;
}

static int cmd_src_grep(const char *args, char *out_buf, uint32_t max_out)
{
    char full[128]; uint32_t p = 0;
    soma_str_put(full, sizeof(full), &p, "src_grep ");
    soma_str_put(full, sizeof(full), &p, args);
    src_tool(full, out_buf, max_out);
    return 1;
}

static int cmd_hpatch(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    hpatch_command(args);
    return 1;
}

static int cmd_panic(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    while (*args == ' ') args++;
#if defined(__x86_64__)
    if (args[0] == 'w' && args[1] == 'e' && args[2] == 'b') {              /* SOMA_198: fallo real dentro del motor del navegador */
        const char *a = args + 3; while (*a == ' ') a++;
        int ph = (*a >= '1' && *a <= '5') ? *a - '0' : 0;
        if (!ph) { kprint("uso: panic web <1 navegacion | 2 JavaScript | 3 maquetacion | 4 instruccion no valida | 5 desbordamiento de pila>\n"
                          "     El siguiente paso de ese tipo en el navegador fallara de verdad: debe cerrarse la pagina, no el sistema.\n"); return 1; }
        if (browser_r3_active()) {                                          /* SOMA_202: con el motor aislado el fallo ocurre en su proceso de ring 3 */
            int32_t wa[1] = { ph == 4 ? 1 : ph == 5 ? 3 : ph == 2 ? 2 : 0 };
            kprint("panic web: fallo provocado dentro del proceso del navegador ("); kprint(wa[0] == 1 ? "instruccion no valida" : wa[0] == 2 ? "bucle sin fin" : wa[0] == 3 ? "agotar su memoria" : "puntero nulo"); kprint("). El sistema debe seguir como si nada.\n");
            (void)web_r3_send(W3C_CRASH, wa, 1, 0, 0);
            return 1;
        }
        web_fault_inject = ph;
        kprint("panic web: el navegador fallara en su proximo paso de ese tipo (recuperaciones hasta ahora: "); kprint_dec(web_fault_count()); kprint(")\n");
        return 1;
    }
#endif
    if (args[0] == 'd' && args[1] == 'i' && args[2] == 'v') {
        kprint("Provocando division por cero (#DE)...\n");
        volatile int z = 0; volatile int x = 42 / z; (void)x;
    } else if (args[0] == 'u' && args[1] == 'd') {
        kprint("Ejecutando instruccion invalida (#UD)...\n");
#ifdef __x86_64__
        __asm__ volatile ("ud2");
#else
        __builtin_trap();
#endif
#ifdef __x86_64__
    } else if (args[0] == 'n' && args[1] == 'u' && args[2] == 'l') {              /* SOMA_192: comprueba la proteccion de la pagina 0 */
        kprint("Escribiendo a traves de un puntero nulo (debe detener el sistema con #PF)...\n");
        volatile uint64_t *np = 0; __asm__ volatile ("" : "+r"(np)); *np = 0xCAFEBABE;
        kprint("... la escritura NO fallo: la pagina 0 no esta protegida (soma.memprot=off?)\n");
    } else if (args[0] == 'n' && args[1] == 'x') {                                /* SOMA_192: comprueba W^X fuera de la imagen del nucleo */
        uintptr_t fr = pmm_alloc_frame();
        if (!fr) { kprint("sin memoria\n"); return 1; }
        *(volatile uint8_t *)fr = 0xC3;                                           /* ret */
        kprint("Saltando a un marco de datos de la RAM (debe detener el sistema con #PF)...\n");
        void (*fn)(void) = (void (*)(void))fr; __asm__ volatile ("" : "+r"(fn)); fn();
        kprint("... el codigo se ejecuto: la RAM de datos sigue siendo ejecutable (soma.memprot=off?)\n");
        pmm_free_frame(fr);
#endif
    } else {
        kprint("Provocando fallo de pagina (#PF) en 0xDEADBEEF00...\n");
        volatile uint64_t *bad = (volatile uint64_t *)0xDEADBEEF00ULL; *bad = 0xCAFEBABE;
    }
    return 1;
}

static int cmd_env(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    if (!env_initialized) env_init_defaults();
    uint32_t pos = 0;
    kprint("\nVARIABLES DE ENTORNO ACTIVAS:\n------------------------------\n");
    for (int i = 0; i < MAX_ENV_VARS; ++i) {
        if (env_table[i].used) {
            kprint("  "); kprint(env_table[i].key); kputc('='); kprint(env_table[i].val); kprint("\n");
            soma_str_put(out_buf, max_out, &pos, env_table[i].key);
            soma_str_putc(out_buf, max_out, &pos, '=');
            soma_str_put(out_buf, max_out, &pos, env_table[i].val);
            soma_str_putc(out_buf, max_out, &pos, '\n');
        }
    }
    kprint("------------------------------\n");
    return 1;
}

static int cmd_export(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        return cmd_env(args, out_buf, max_out);
    }
    char k[ENV_KEY_LEN], v[ENV_VAL_LEN];
    uint32_t kp = 0, vp = 0;
    while (*args && *args != '=' && *args != ' ' && kp < sizeof(k) - 1) k[kp++] = *args++;
    k[kp] = '\0';
    if (*args == '=' || *args == ' ') args++;
    while (*args == ' ') args++;

    char q = 0;
    if (*args == '"' || *args == '\'') {
        q = *args++;
    }
    while (*args && vp < sizeof(v) - 1) {
        if (q && *args == q) { args++; break; }
        v[vp++] = *args++;
    }
    v[vp] = '\0';

    if (kp > 0) {
        env_set(k, v);
        kprint("export: "); kprint(k); kputc('='); kprint(v); kprint("\n");
        uint32_t p = 0;
        soma_str_put(out_buf, max_out, &p, k); soma_str_putc(out_buf, max_out, &p, '='); soma_str_put(out_buf, max_out, &p, v);
        return 1;
    }
    kprint("Uso: export VAR=valor\n");
    return 1;
}

static int cmd_unset(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: unset <VAR>\n");
        return 1;
    }
    env_unset(args);
    kprint("unset: "); kprint(args); kprint("\n");
    uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "unset OK");
    return 1;
}

#define SOURCE_MAX_DEPTH 3

static int cmd_source(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: source <archivo.sh>  (alias: sh <archivo.sh>)\n");
        return 1;
    }

    /* PARCHE 046: profundidad por hilo + buffer propio (antes: static compartido). */
    struct tcb *self = thread_current();
    static int source_depth_fallback = 0;
    int *sdepth = self ? &self->source_depth : &source_depth_fallback;
    uint32_t pos = 0;

    if (*sdepth >= SOURCE_MAX_DEPTH) {
        kprint("source: anidamiento maximo alcanzado (3).\n");
        soma_str_put(out_buf, max_out, &pos, "Error: source anidado en exceso");
        return 0;
    }

    char *script_buf = (char *)kmalloc(4096);
    if (!script_buf) {
        kprint("source: sin memoria.\n");
        soma_str_put(out_buf, max_out, &pos, "Error: sin memoria");
        return 0;
    }

    int r = vfs_read(args, script_buf, 4095);
    if (r < 0) {
        kprint("source: archivo no encontrado: '"); kprint(args); kprint("'\n");
        soma_str_put(out_buf, max_out, &pos, "source: archivo no encontrado");
        kfree(script_buf);
        return 1;
    }
    script_buf[r] = '\0';

    (*sdepth)++;
    const char *p = script_buf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '\n') { p++; continue; }
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        char line[256];
        uint32_t lp = 0;
        while (*p && *p != '\n' && lp < sizeof(line) - 1) {
            if (*p != '\r') line[lp++] = *p;
            p++;
        }
        line[lp] = '\0';
        if (*p == '\n') p++;

        while (lp > 0 && (line[lp - 1] == ' ' || line[lp - 1] == '\t')) {
            line[--lp] = '\0';
        }

        if (lp > 0) {
            char sub_out[1024];
            sub_out[0] = '\0';
            dispatch_command(line, sub_out, sizeof(sub_out));
        }
    }
    (*sdepth)--;
    kfree(script_buf);

    soma_str_put(out_buf, max_out, &pos, "source: ejecucion completada");
    return 1;
}

