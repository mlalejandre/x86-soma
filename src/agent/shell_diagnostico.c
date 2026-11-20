/* shell_diagnostico.c: tiempo, memoria y pruebas de robustez (crash, hang, fuzz, bancos), audio, camara, captura y BIGFS.
 * SOMA_233_SHELL_PARTIDO: NO se compila por separado. Lo incluye src/agent/shell.c en su sitio (una sola unidad de
 * compilacion: usa las funciones auxiliares y la tabla de ordenes de shell.c, y el nucleo compilado sale identico). */
static int cmd_uptime(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint64_t ms = timer_get_uptime_ms();
    uint32_t sec = (uint32_t)(ms / 1000);
    kprint("Uptime: "); kprint_dec(sec); kprint("s\n");
    uint32_t p = 0;
    soma_str_put(out_buf, max_out, &p, "Uptime: ");
    soma_str_put_dec(out_buf, max_out, &p, sec);
    soma_str_put(out_buf, max_out, &p, " segundos");
    return 1;
}

/* ramstat v2: comando 'ram' - consumo real de RAM con desglose y delta */
static void ram_put_mib(char *out, uint32_t max, uint32_t *p, const char *label, uint64_t bytes)
{
    uint32_t mib  = (uint32_t)(bytes >> 20);
    uint32_t frac = (uint32_t)(((bytes & 0xFFFFFULL) * 100ULL) >> 20);
    soma_str_put(out, max, p, label);
    soma_str_put_dec(out, max, p, mib);
    soma_str_put(out, max, p, ".");
    if (frac < 10) soma_str_put(out, max, p, "0");
    soma_str_put_dec(out, max, p, frac);
    soma_str_put(out, max, p, " MiB\n");
}

static void ram_put_delta(char *out, uint32_t max, uint32_t *p, const char *label, uint64_t now, uint64_t base)
{
    soma_str_put(out, max, p, label);
    if (now >= base) { soma_str_put(out, max, p, "+"); soma_str_put_dec(out, max, p, (uint32_t)((now - base) >> 10)); }
    else             { soma_str_put(out, max, p, "-"); soma_str_put_dec(out, max, p, (uint32_t)((base - now) >> 10)); }
    soma_str_put(out, max, p, " KiB\n");
}

static int ram_arg_is(const char *a, const char *word)
{
    if (!a) return 0;
    while (*a == ' ') a++;
    while (*word) {
        if (*a != *word) return 0;
        a++; word++;
    }
    return (*a == 0 || *a == ' ');
}

/* PARCHE_TERMINAL_V2: 'ram' unifica a 'free' (alias) con un informe completo. */
static void shell_emit(const char *text, char *out_buf, uint32_t max_out)
{
    kprint(text);
    if (out_buf && max_out) {
        uint32_t p = 0;
        out_buf[0] = '\0';
        soma_str_put(out_buf, max_out, &p, text);
    }
}

static void ram_put_mib_raw(char *out, uint32_t max, uint32_t *p, uint64_t bytes)
{
    uint32_t mib  = (uint32_t)(bytes >> 20);
    uint32_t frac = (uint32_t)(((bytes & 0xFFFFFULL) * 100ULL) >> 20);
    soma_str_put_dec(out, max, p, mib);
    soma_str_putc(out, max, p, '.');
    if (frac < 10) soma_str_putc(out, max, p, '0');
    soma_str_put_dec(out, max, p, frac);
    soma_str_put(out, max, p, " MiB");
}

static void ram_put_pct10(char *out, uint32_t max, uint32_t *p, uint32_t pct10)
{
    soma_str_put_dec(out, max, p, pct10 / 10);
    soma_str_putc(out, max, p, '.');
    soma_str_put_dec(out, max, p, pct10 % 10);
    soma_str_put(out, max, p, " %");
}

static void ram_put_bar(char *out, uint32_t max, uint32_t *p, uint32_t pct10)
{
    const uint32_t width = 40;
    uint32_t fill = (pct10 * width + 500) / 1000;
    if (fill > width) fill = width;
    soma_str_putc(out, max, p, '[');
    for (uint32_t i = 0; i < width; ++i) soma_str_putc(out, max, p, i < fill ? '#' : '.');
    soma_str_putc(out, max, p, ']');
}

static int cmd_ram(const char *args, char *out_buf, uint32_t max_out)
{
    char rep[1792];
    uint32_t p = 0;
    struct ramstat_info ri, b;
    int brief = 0, verbose = 0;

    if (!args) args = "";
    while (*args == ' ') args++;

    if (ram_arg_is(args, "base")) {
        ramstat_set_baseline();
        shell_emit("RAM: linea base guardada. Abre una aplicacion y ejecuta 'ram -v' para ver el delta.\n", out_buf, max_out);
        return 1;
    }
    if (ram_arg_is(args, "reset")) {
        ramstat_clear_baseline();
        shell_emit("RAM: linea base borrada.\n", out_buf, max_out);
        return 1;
    }
    if (ram_arg_is(args, "-s") || ram_arg_is(args, "short") || ram_arg_is(args, "resumen")) {
        brief = 1;
    } else if (ram_arg_is(args, "-v") || ram_arg_is(args, "full") || ram_arg_is(args, "detalle")) {
        verbose = 1;
    } else if (*args) {
        shell_emit("Uso: ram [-s | -v | base | reset]   (alias: free)\n"
                   "  -s resumen en una linea | -v desglose completo y delta\n"
                   "  base / reset: fija o borra la linea base para medir el coste de una app\n",
                   out_buf, max_out);
        return 1;
    }

    ramstat_get(&ri);
    uint32_t free10 = ri.total ? (uint32_t)((ri.free_b * 1000ULL) / ri.total) : 0;
    rep[0] = '\0';

    if (brief) {
        soma_str_put(rep, sizeof(rep), &p, "RAM: ");
        ram_put_mib_raw(rep, sizeof(rep), &p, ri.footprint);
        soma_str_put(rep, sizeof(rep), &p, " / ");
        ram_put_mib_raw(rep, sizeof(rep), &p, ri.total);
        soma_str_put(rep, sizeof(rep), &p, " (");
        ram_put_pct10(rep, sizeof(rep), &p, ri.pct10);
        soma_str_put(rep, sizeof(rep), &p, ") | libre ");
        ram_put_mib_raw(rep, sizeof(rep), &p, ri.free_b);
        soma_str_put(rep, sizeof(rep), &p, " | heap libre ");
        soma_str_put_dec(rep, sizeof(rep), &p, ri.heap_free >> 10);
        soma_str_put(rep, sizeof(rep), &p, " KiB\n");
        shell_emit(rep, out_buf, max_out);
        return 1;
    }

    soma_str_put(rep, sizeof(rep), &p, "=== MEMORIA RAM ===\n");
    soma_str_put(rep, sizeof(rep), &p, "Total : ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.total);
    soma_str_put(rep, sizeof(rep), &p, "\nUsada : ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.footprint);
    soma_str_put(rep, sizeof(rep), &p, " (");
    ram_put_pct10(rep, sizeof(rep), &p, ri.pct10);
    soma_str_put(rep, sizeof(rep), &p, ")\nLibre : ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.free_b);
    soma_str_put(rep, sizeof(rep), &p, " (");
    ram_put_pct10(rep, sizeof(rep), &p, free10);
    soma_str_put(rep, sizeof(rep), &p, ")\n");
    ram_put_bar(rep, sizeof(rep), &p, ri.pct10);
    soma_str_put(rep, sizeof(rep), &p, "\nHeap del kernel: ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.heap_used);
    soma_str_put(rep, sizeof(rep), &p, " en uso | ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.heap_free);
    soma_str_put(rep, sizeof(rep), &p, " libre | ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.heap_mapped);
    soma_str_put(rep, sizeof(rep), &p, " mapeado\n");
    if (ri.pct10 >= 900) soma_str_put(rep, sizeof(rep), &p, "AVISO: memoria casi agotada.\n");

    if (verbose) {
        soma_str_put(rep, sizeof(rep), &p, "--- Desglose de la memoria comprometida ---\n");
        ram_put_mib(rep, sizeof(rep), &p, "Zona baja reservada : ", ri.lower);
        ram_put_mib(rep, sizeof(rep), &p, "  Imagen del kernel  : ", ri.lower_image);
        ram_put_mib(rep, sizeof(rep), &p, "  Reserva sin medir  : ", ri.lower_rest);
        ram_put_mib(rep, sizeof(rep), &p, "Heap mapeado        : ", ri.heap_mapped);
        ram_put_mib(rep, sizeof(rep), &p, "  En uso (kmalloc)   : ", ri.heap_used);
        ram_put_mib(rep, sizeof(rep), &p, "  Libre              : ", ri.heap_free);
        ram_put_mib(rep, sizeof(rep), &p, "Pilas/tablas/otros  : ", ri.other);
        if (ramstat_get_baseline(&b)) {
            soma_str_put(rep, sizeof(rep), &p, "--- Delta desde 'ram base' ---\n");
            ram_put_delta(rep, sizeof(rep), &p, "Huella            : ", ri.footprint, b.footprint);
            ram_put_delta(rep, sizeof(rep), &p, "Heap en uso       : ", ri.heap_used, b.heap_used);
            ram_put_delta(rep, sizeof(rep), &p, "Pilas/tablas/otros: ", ri.other, b.other);
        } else {
            soma_str_put(rep, sizeof(rep), &p, "(usa 'ram base' para medir el coste de una aplicacion)\n");
        }
    } else {
        soma_str_put(rep, sizeof(rep), &p, "(ram -v: desglose | ram -s: resumen | ram base: medir una app)\n");
    }
    shell_emit(rep, out_buf, max_out);
    return 1;
}


/* PARCHE_HORA_LS_V1: date en CEST (UTC+2) */
static int cmd_date(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    struct rtc_time t; rtc_get_datetime(&t);

    /* Convertir UTC → CEST (UTC+2). Ajuste simple de hora/día. */
    uint32_t y = t.year, mo = t.month, d = t.day;
    uint32_t h = t.hour + 2, mi = t.min, s = t.sec;
    if (h >= 24) {
        h -= 24;
        d += 1;
        uint32_t dim = 31;
        if (mo == 4 || mo == 6 || mo == 9 || mo == 11) dim = 30;
        else if (mo == 2) dim = 28;
        if (d > dim) { d = 1; mo += 1; }
        if (mo > 12) { mo = 1; y += 1; }
    }

    char date_str[80]; uint32_t dp = 0;
    soma_str_put(date_str, sizeof(date_str), &dp, "CEST: ");
    soma_str_put_dec(date_str, sizeof(date_str), &dp, y); soma_str_putc(date_str, sizeof(date_str), &dp, '-');
    if (mo < 10) soma_str_putc(date_str, sizeof(date_str), &dp, '0');
    soma_str_put_dec(date_str, sizeof(date_str), &dp, mo); soma_str_putc(date_str, sizeof(date_str), &dp, '-');
    if (d < 10) soma_str_putc(date_str, sizeof(date_str), &dp, '0');
    soma_str_put_dec(date_str, sizeof(date_str), &dp, d); soma_str_putc(date_str, sizeof(date_str), &dp, ' ');
    if (h < 10) soma_str_putc(date_str, sizeof(date_str), &dp, '0');
    soma_str_put_dec(date_str, sizeof(date_str), &dp, h); soma_str_putc(date_str, sizeof(date_str), &dp, ':');
    if (mi < 10) soma_str_putc(date_str, sizeof(date_str), &dp, '0');
    soma_str_put_dec(date_str, sizeof(date_str), &dp, mi); soma_str_putc(date_str, sizeof(date_str), &dp, ':');
    if (s < 10) soma_str_putc(date_str, sizeof(date_str), &dp, '0');
    soma_str_put_dec(date_str, sizeof(date_str), &dp, s);
    soma_str_put(date_str, sizeof(date_str), &dp, " CEST");

    kprint(date_str); kprint("\n");
    uint32_t pos = 0;
    soma_str_put(out_buf, max_out, &pos, date_str);
    return 1;
}

static int cmd_clear(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    console_clear();
    uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Pantalla borrada.");
    return 1;
}

static int cmd_test_suite(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    boot_gate_run_test_suite(out_buf, max_out);
    return 1;
}

static int cmd_mem(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    sysinfo_print_mem();
    sysinfo_format_mem(out_buf, max_out);
    return 1;
}

/* PARCHE_FEEDBACK_V1: df reporta capacidad real al agente */
static int cmd_df(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t files_u = 0, bytes_u = 0, dirty_c = 0;
    vfs_get_stats(&files_u, &bytes_u, &dirty_c);

    kprint("\nALMACENAMIENTO (df):\n");
    kprint("  Disco: "); kprint(sysdisk_describe()); kprint("\n");   /* SOMA_162 */
    kprint("  RamFS: "); kprint_dec(files_u); kprint("/64 inodos | ");
    kprint_dec(bytes_u / 1024); kprint(" KiB usados (max 16 KiB/archivo) | dirty=");
    kprint_dec(dirty_c); kprint("\n\n");

    uint32_t pos = 0;
    soma_str_put(out_buf, max_out, &pos, "Disco: "); soma_str_put(out_buf, max_out, &pos, sysdisk_describe()); soma_str_put(out_buf, max_out, &pos, " | RamFS V2: ");
    soma_str_put_dec(out_buf, max_out, &pos, files_u);
    soma_str_put(out_buf, max_out, &pos, "/64 inodos | ");
    soma_str_put_dec(out_buf, max_out, &pos, bytes_u / 1024);
    soma_str_put(out_buf, max_out, &pos, " KiB usados (16 KiB/archivo) | dirty=");
    soma_str_put_dec(out_buf, max_out, &pos, dirty_c);
    return 1;
}

static int cmd_stats(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    sysinfo_print_stats();
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "stats ejecutado");
    return 1;
}

static int cmd_heap(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    kheap_dump_stats();
    kheap_test_self();
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "Heap inspeccionado");
    return 1;
}

/* SOMA_PARCHE_008_PANIC: diagnostico de fallos. 'crash' y 'hang' NO son CMD_F_SAFE:
 * el agente no puede ejecutarlos (dispatch bloquea lo no SAFE en modo agente). */
static void __attribute__((noinline)) crash_level3(volatile uint64_t *p) { *p = 0x42; }
static void __attribute__((noinline)) crash_level2(volatile uint64_t *p) { crash_level3(p); kprint(""); }
static void __attribute__((noinline)) crash_level1(volatile uint64_t *p) { crash_level2(p); kprint(""); }

static int cmd_crash(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)out_buf; (void)max_out;
    kprint("crash: provocando un fallo de pagina a proposito...\n");
    crash_level1((volatile uint64_t *)0x00007F0000000000ULL);   /* direccion no mapeada */
    return 1;
}

static void __attribute__((noinline)) hang_spin(uint64_t until_ms)
{
    while (timer_get_uptime_ms() < until_ms) { }
}

static int cmd_hang(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t secs = 0; while (*args >= '0' && *args <= '9') secs = secs * 10 + (uint32_t)(*args++ - '0');
    if (secs == 0) secs = 8;
    if (secs > 60) secs = 60;
    kprint("hang: reteniendo la CPU sin ceder durante "); kprint_dec(secs);
    kprint(" s (el vigilante avisa a los 5 s; solo x86_64)...\n");
    hang_spin(timer_get_uptime_ms() + (uint64_t)secs * 1000u);
    kprint("hang: terminado\n");
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "hang terminado");
    return 1;
}

/* 'deadlock': DEPURACION (no es CMD_F_SAFE, el agente no puede ejecutarlo). Lanza dos hilos que
 * cogen dos mutex en orden opuesto para ver como los detecta kmutex_lock. Una vez por arranque. */
static struct kmutex dl_a, dl_b;
static int dl_launched = 0;
static void dl_thread1(void *arg) { (void)arg; kmutex_lock(&dl_a); thread_sleep(100); kmutex_lock(&dl_b); kmutex_unlock(&dl_b); kmutex_unlock(&dl_a); }
static void dl_thread2(void *arg) { (void)arg; kmutex_lock(&dl_b); thread_sleep(100); kmutex_lock(&dl_a); kmutex_unlock(&dl_a); kmutex_unlock(&dl_b); }

/* PARCHE 111: prueba de estres de concurrencia (mutex + heap + VFS) con seis hilos */
static int cmd_conctest(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t pos = 0;
    int ok = conc_selftest();
    soma_str_put(out_buf, max_out, &pos, ok ? "conctest: TODO OK (detalle en la consola)" : "conctest: HAY FALLOS (detalle en la consola)");
    return 1;
}

static int cmd_fscrash(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t pos = 0;
    int ok = vfs_crash_selftest();
    ok = vfs_checkpoint_selftest() && ok;                  /* PARCHE 120 */
    soma_str_put(out_buf, max_out, &pos, ok ? "fscrash: TODO OK (detalle en la consola)" : "fscrash: HAY FALLOS (detalle en la consola)");
    return 1;
}

static int cmd_fscheck(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t pos = 0;
    int bad = vfs_fscheck();
    soma_str_put(out_buf, max_out, &pos, bad == 0 ? "fscheck: disco correcto (detalle en la consola)" : "fscheck: HAY PROBLEMAS (detalle en la consola)");
    return 1;
}

/* PARCHE 117 */
static int cmd_usfuzz(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0, v[3] = { 24, 3000, 1 }; int k = 0;
    const char *p = args ? args : "";
    while (*p && k < 3) {
        while (*p == ' ') p++;
        if (*p < '0' || *p > '9') break;
        uint32_t x = 0; while (*p >= '0' && *p <= '9') { x = x * 10 + (uint32_t)(*p - '0'); p++; }
        v[k++] = x;
    }
    int ok = usermode_fuzz((int)v[0], v[1], v[2]);
    soma_str_put(out_buf, max_out, &pos, ok == 1 ? "usfuzz: TODO OK (detalle en la consola)" : "usfuzz: HAY FALLOS (detalle en la consola)");
    return 1;
}

/* PARCHE 127 */
static int cmd_pmmbench(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t pos = 0;
    int ok = pmm_bench();
    soma_str_put(out_buf, max_out, &pos, ok == 1 ? "pmmbench: OK (cifras en la consola)" : "pmmbench: FALLO (detalle en la consola)");
    return 1;
}

/* PARCHE 118 */
static int cmd_oomtest(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t pos = 0;
    int ok = usermode_oomtest();
    ok = usermode_oomtest_mem() && ok;                    /* PARCHE 119 */
    soma_str_put(out_buf, max_out, &pos, ok == 1 ? "oomtest: TODO OK (detalle en la consola)" : "oomtest: HAY FALLOS (detalle en la consola)");
    return 1;
}

static int cmd_demo_ai(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    int vivo = args && args[0] == 'v';
    int ok = coder_demo_run(vivo);
    soma_str_put(out_buf, max_out, &pos, ok ? "demo_ai: TODO OK (detalle en la consola)" : "demo_ai: HAY FALLOS (detalle en la consola)");
    return 1;
}

static int cmd_deadlock(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t pos = 0;
    if (dl_launched) {
        kprint("deadlock: ya se lanzo en este arranque (mata los hilos dl_t1/dl_t2 con 'kill <TID>')\n");
        soma_str_put(out_buf, max_out, &pos, "deadlock ya lanzado");
        return 1;
    }
    dl_launched = 1;
    kmutex_init(&dl_a, "dl_A");
    kmutex_init(&dl_b, "dl_B");
    thread_create("dl_t1", dl_thread1, 0);
    thread_create("dl_t2", dl_thread2, 0);
    kprint("deadlock: lanzados dl_t1 (A->B) y dl_t2 (B->A). Mira 'mutex' y 'ps'; mata con 'kill <TID>'.\n");
    soma_str_put(out_buf, max_out, &pos, "deadlock lanzado");
    return 1;
}

static int cmd_mutex(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    kmutex_dump();
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "Mutex inspeccionados");
    return 1;
}

static int cmd_watchdog(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint64_t now = timer_get_uptime_ms(), beat = watchdog_last_beat_ms();
    kprint("WATCHDOG: limite "); kprint_dec(WATCHDOG_LIMIT_MS);
    kprint(" ms | avisos: "); kprint_dec(watchdog_trips());
    kprint(" | ultimo latido hace "); kprint_dec((uint32_t)(now >= beat ? now - beat : 0));
    kprint(" ms\n");
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "Vigilante inspeccionado");
    return 1;
}

/* SOMA_PARCHE_020_AUDIO */
static int cmd_audio(const char *args, char *out_buf, uint32_t max_out)
{
    return audio_sys_command(args, out_buf, max_out);
}

/* SOMA_177_T480S: mic (diagnostico y grabacion por el microfono) y grabadora (la aplicacion) */
static int cmd_mic(const char *args, char *out_buf, uint32_t max_out)
{
    return micrec_command(args, out_buf, max_out);
}
static int cmd_cam(const char *args, char *out_buf, uint32_t max_out)      /* SOMA_178_T480S */
{
    return camera_command(args, out_buf, max_out);
}
static int cmd_camara(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t p = 0;
    gui_open_camera();
    soma_str_put(out_buf, max_out, &p, "Camara abierta.");
    return 1;
}
static int cmd_captura(const char *args, char *out_buf, uint32_t max_out)    /* SOMA_179_T480S */
{
    return scrcap_command(args, out_buf, max_out);
}
static int cmd_grabadora(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t p = 0;
    gui_open_recorder();
    soma_str_put(out_buf, max_out, &p, "Grabadora abierta.");
    return 1;
}

/* SOMA_PARCHE_018_BIGFS: almacen de archivos grandes */
static int cmd_big(const char *args, char *out_buf, uint32_t max_out)
{
    return bigfs_sys_command(args, out_buf, max_out);
}

