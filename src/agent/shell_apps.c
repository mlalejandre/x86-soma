/* shell_apps.c: aplicaciones y portatil: abrir apps, pantalla, teclado, disco, informe, bateria, escritorio, orbe, energia, brillo, trackpad, ACPI, fuentes y cursor.
 * SOMA_233_SHELL_PARTIDO: NO se compila por separado. Lo incluye src/agent/shell.c en su sitio (una sola unidad de
 * compilacion: usa las funciones auxiliares y la tabla de ordenes de shell.c, y el nucleo compilado sale identico). */
/* ====================================================================
 * TABLA DECLARATIVA CANÓNICA DE COMANDOS DE SOMA
 * ==================================================================== */

static int cmd_history(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    return console_history_dump(out_buf, max_out);
}

/* PARCHE 046: banco de pruebas de la politica del Agente (solo usuario). */
static int cmd_agent_sim(const char *args, char *out_buf, uint32_t max_out)
{
    char cmd[256];
    uint32_t n = 0;
    char q = 0;

    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: agent_sim <comando>   (usa comillas si contiene ; | > &&)\n");
        return 1;
    }
    if (*args == '"' || *args == '\'') q = *args++;
    while (*args && n < sizeof(cmd) - 1) {
        if (q && *args == q && args[1] == '\0') break;
        cmd[n++] = *args++;
    }
    cmd[n] = '\0';

    int r = dispatch_agent_command(cmd, out_buf, max_out);
    kprint("[agent_sim] rc="); kprint_dec(r ? 1 : 0); kprint("\n");
    return 1;
}


/* PARCHE 089: "monitor" - monitor del sistema */
static int cmd_monitor(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t p = 0;
    gui_open_sysmon();
    soma_str_put(out_buf, max_out, &p, "Monitor del sistema abierto.");
    return 1;
}

/* PARCHE 088: "imagen [archivo]" - visor de imagenes */
static int cmd_imagen(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t p = 0;
    while (args && *args == ' ') args++;
    char path[128]; uint32_t k = 0;
    while (args && *args && *args != ' ' && k < sizeof path - 1) path[k++] = *args++;
    path[k] = 0;
    if (k && path[0] != '/') { char t[128] = "/workspace/"; uint32_t l = 11; for (uint32_t i = 0; path[i] && l < sizeof t - 1; i++) t[l++] = path[i]; t[l] = 0; if (vfs_size(t) >= 0) { for (uint32_t i = 0; i <= l; i++) path[i] = t[i]; } }
    gui_open_imgview(path);
    soma_str_put(out_buf, max_out, &p, "Visor de imagenes abierto.");
    return 1;
}

/* PARCHE 088: "pdf [archivo]" - lector de PDF */
static int cmd_pdf(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t p = 0;
    while (args && *args == ' ') args++;
    char path[128]; uint32_t k = 0;
    while (args && *args && *args != ' ' && k < sizeof path - 1) path[k++] = *args++;
    path[k] = 0;
    if (k && path[0] != '/') { char t[128] = "/workspace/"; uint32_t l = 11; for (uint32_t i = 0; path[i] && l < sizeof t - 1; i++) t[l++] = path[i]; t[l] = 0; if (vfs_size(t) >= 0) { for (uint32_t i = 0; i <= l; i++) path[i] = t[i]; } }
    gui_open_pdf(path);
    soma_str_put(out_buf, max_out, &p, "Lector PDF abierto.");
    return 1;
}

/* PARCHE 087: "suma <archivo>" - tamano y CRC-32 (comprobar que un archivo grande llega y se guarda entero) */
static int cmd_suma(const char *args, char *out_buf, uint32_t max_out)
{
    char rep[200]; uint32_t p = 0; rep[0] = 0;
    while (args && *args == ' ') args++;
    uint32_t n = 0; char *d = (args && *args) ? vfs_read_alloc(args, &n) : 0;
    if (!d) { soma_str_put(rep, sizeof rep, &p, "suma: no existe (uso: suma <archivo>)\n"); shell_emit(rep, out_buf, max_out); return 1; }
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) { c ^= (uint8_t)d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    c = ~c; kfree(d);
    static const char hx[] = "0123456789abcdef"; char h[9]; for (int i = 0; i < 8; i++) h[i] = hx[(c >> (28 - 4 * i)) & 15]; h[8] = 0;
    soma_str_put_dec(rep, sizeof rep, &p, n); soma_str_put(rep, sizeof rep, &p, " bytes  crc32 "); soma_str_put(rep, sizeof rep, &p, h); soma_str_put(rep, sizeof rep, &p, "  "); soma_str_put(rep, sizeof rep, &p, args); soma_str_put(rep, sizeof rep, &p, "\n");
    shell_emit(rep, out_buf, max_out);
    return 1;
}

/* PARCHE 087: "descargar <url> [archivo]" - guarda en el VFS lo que devuelve una URL (http directo; https por la pasarela) */
static int cmd_descargar(const char *args, char *out_buf, uint32_t max_out)
{
    if (agent_net_denied("descargar", args, out_buf, max_out)) return 1;   /* SOMA_193 */
    char rep[256]; rep[0] = 0;
    char url[300], dst[FS_NAME_MAX]; uint32_t p = 0, k = 0;
    while (args && *args == ' ') args++;
    while (args && *args && *args != ' ' && k < sizeof url - 1) url[k++] = *args++;
    url[k] = 0;
    while (args && *args == ' ') args++;
    k = 0; while (args && *args && *args != ' ' && k < sizeof dst - 1) dst[k++] = *args++;
    dst[k] = 0;
    if (!url[0]) { soma_str_put(rep, sizeof rep, &p, "Uso: descargar <url> [archivo]"); { soma_str_put(rep, sizeof rep, &p, "\n"); shell_emit(rep, out_buf, max_out); } return 1; }
    if (!dst[0]) {   /* nombre: lo ultimo de la ruta, en /workspace */
        const char *b = url, *q; for (q = url; *q && *q != '?' && *q != '#'; q++) if (*q == '/') b = q + 1;
        char nm[40]; uint32_t n = 0; for (q = b; *q && *q != '?' && *q != '#' && n < sizeof nm - 1; q++) nm[n++] = *q;
        nm[n] = 0; if (!n) { const char *d = "descarga"; while (*d) nm[n++] = *d++; nm[n] = 0; }
        const char *ws = "/workspace/"; uint32_t l = 0; while (ws[l]) { dst[l] = ws[l]; l++; }
        for (n = 0; nm[n] && l < sizeof dst - 1; n++) dst[l++] = nm[n];
        dst[l] = 0;
    }
    if (agent_write_denied(dst, "descargar", out_buf, max_out)) return 1;
    struct wn_req rq; struct wn_resp rs; memset(&rq, 0, sizeof rq); memset(&rs, 0, sizeof rs);
    rq.url = url; rq.timeout_ms = 30000; rq.max_body = 24u << 20; rq.want_binary = 1;
#if defined(__x86_64__)
#define wn_fetch wn_fetch_isolated      /* PARCHE 142: el kernel no interpreta HTTP: lo hace el servicio aislado netsvc */
#define wn_free  wn_free_isolated
#endif
    if (wn_fetch(&rq, &rs) != 0 || !rs.body) { soma_str_put(rep, sizeof rep, &p, "descargar: error de red "); soma_str_put(rep, sizeof rep, &p, rs.err); wn_free(&rs); { soma_str_put(rep, sizeof rep, &p, "\n"); shell_emit(rep, out_buf, max_out); } return 1; }
    if (rs.status < 200 || rs.status >= 300) { soma_str_put(rep, sizeof rep, &p, "descargar: estado HTTP "); soma_str_put_dec(rep, sizeof rep, &p, (uint64_t)rs.status); wn_free(&rs); { soma_str_put(rep, sizeof rep, &p, "\n"); shell_emit(rep, out_buf, max_out); } return 1; }
    int w = vfs_write(dst, rs.body, rs.len);
    if (w < 0) soma_str_put(rep, sizeof rep, &p, w == -2 ? "descargar: demasiado grande (max 32 MiB)" : "descargar: no se pudo guardar (sin memoria o ruta no valida)");
    else {
        soma_str_put(rep, sizeof rep, &p, "descargados "); soma_str_put_dec(rep, sizeof rep, &p, rs.len); soma_str_put(rep, sizeof rep, &p, " bytes en "); soma_str_put(rep, sizeof rep, &p, dst);
        int pers = vfs_big_persist();
        if (rs.len >= FS_DATA_MAX && pers == 0) soma_str_put(rep, sizeof rep, &p, " (solo en memoria: BIGFS sin formatear)");
    }
    wn_free(&rs);
    { soma_str_put(rep, sizeof rep, &p, "\n"); shell_emit(rep, out_buf, max_out); } return 1;
}

/* PARCHE 097: "programa [archivo.soma] <mision>" crea o modifica un programa con el programador de SOMA (SOMA_239: o archivo.py) */
static int cmd_programa(const char *args, char *out_buf, uint32_t max_out)
{
    while (args && *args == ' ') args++;
    if (!args || !*args) { uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Uso: programa [archivo.soma|archivo.py] <que debe hacer>   (\"en Python\" en la descripcion elige Python)"); return 1; }
    char target[128]; target[0] = 0;
    const char *m = args;
    { const char *e = args; while (*e && *e != ' ') e++;
      uint32_t n = (uint32_t)(e - args);
      if ((n > 5 && n < sizeof target && args[n - 5] == '.' && args[n - 4] == 's' && args[n - 3] == 'o' && args[n - 2] == 'm' && args[n - 1] == 'a') ||
          (n > 3 && n < sizeof target && args[n - 3] == '.' && args[n - 2] == 'p' && args[n - 1] == 'y')) {   /* SOMA_239: o archivo.py */
          for (uint32_t i = 0; i < n; i++) target[i] = args[i];
          target[n] = 0; m = e; while (*m == ' ') m++;
      } }
    if (!*m) { uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Falta la descripcion de lo que debe hacer el programa."); return 1; }
    coder_run_mission(target, m, out_buf, max_out);
    return 1;
}
/* PARCHE 097: "coderlog" muestra el detalle del ultimo trabajo del programador */
static int cmd_coderlog(const char *args, char *out_buf, uint32_t max_out)
{
    static char t[6144]; uint32_t p = 0;
    int sz = vfs_size("/agent/coder_ultimo.txt");
    if (sz <= 0) { soma_str_put(out_buf, max_out, &p, "Aun no hay ningun trabajo del programador registrado."); return 1; }
    int full = args && (args[0] == 't' || args[0] == 'c');                 /* "coderlog todo" */
    char *b = (char *)kmalloc((uint32_t)sz + 1);
    if (!b) return 1;
    int r = vfs_read("/agent/coder_ultimo.txt", b, (uint32_t)sz); if (r < 0) r = 0; b[r] = 0;
    t[0] = 0; uint32_t k = 0;
    /* resumen: solo las lineas de bitacora (# ...) y las cabeceras; "coderlog todo" incluye las respuestas del modelo */
    for (char *l = b; *l; ) {
        char *e = l; while (*e && *e != '\n') e++;
        int keep = full || l[0] == '#' || l[0] == '=' || (l[0] == 'M' && l[1] == 'i') || (l[0] == 'A' && l[1] == 'r');
        if (keep) for (char *q = l; q < e && k + 2 < sizeof t; q++) t[k++] = *q;
        if (keep && k + 2 < sizeof t) t[k++] = '\n';
        l = *e ? e + 1 : e;
    }
    t[k] = 0;
    kfree(b);
    kprint(t);                                                             /* SOMA_235: una sola vez (antes salia dos veces) */
    soma_str_put(out_buf, max_out, &p, full ? "" : "(Con 'coderlog todo' ves tambien lo que respondio el modelo. Archivo completo: /agent/coder_ultimo.txt)");
    return 1;
}

/* PARCHE 093: "studio [archivo.soma]" abre SOMA Studio con el editor */
static int cmd_studio(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t p = 0;
    while (args && *args == ' ') args++;
    if (args && *args) coder_app_load_project(args);
    gui_open_coder_app();
    soma_str_put(out_buf, max_out, &p, "SOMA Studio abierto.");
    return 1;
}

#if defined(__x86_64__)
/* SOMA_154_T480S: "pantalla" */
static int cmd_pantalla(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t p = 0;
    bootfb_print_info();
    soma_str_put(out_buf, max_out, &p, "");
    return 1;
}
#endif
#if defined(__x86_64__)
/* SOMA_155_T480S: "teclado [es|us]" */
static int cmd_teclado(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t p = 0;
    while (args && *args == ' ') args++;
    if (args && args[0] == 'e' && args[1] == 's') keyboard_set_layout(1);
    else if (args && args[0] == 'u' && args[1] == 's') keyboard_set_layout(0);
    i8042_print_info();
    kprint("  Distribucion: "); kprint(keyboard_get_layout() ? "espanola (es)\n" : "la de siempre (us + n y tildes)\n");
    kprint("  Cambiar: teclado es | teclado us   (al arrancar: soma.teclado=es|us en la linea de GRUB)\n");
    soma_str_put(out_buf, max_out, &p, "");
    return 1;
}
#endif
#if defined(__x86_64__)
/* SOMA_157_T480S: "ramtest [MiB]": comprueba la memoria alta (reserva, escribe, verifica, libera) */
static int cmd_ramtest(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t mib = 0;
    while (args && *args == ' ') args++;
    while (args && *args >= '0' && *args <= '9') mib = mib * 10 + (uint32_t)(*args++ - '0');
    static char b[1024];
    hw_ramtest(mib, b, sizeof b);
    kprint(b);
    if (out_buf && max_out) out_buf[0] = 0;
    return 1;
}
#endif
#if defined(__x86_64__)
/* SOMA_162_T480S: "disco": donde guarda SOMA los archivos (pendrive SOMADATA, virtio o solo RAM) */
static int cmd_disco(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    const struct usbdisk_info *u = usbdisk_info();
    if (args && args[0] == 's') {
        int r = usbdisk_flush();
        kprint(r == 0 ? "disco: cache del pendrive vaciada\n" : "disco: no hay pendrive como disco del sistema\n");
        return 1;
    }
    kprint("Disco del sistema: "); kprint(sysdisk_describe()); kprint("\n");
    if (u->active) {
        uint64_t r, w, e, t; usbdisk_stats(&r, &w, &e, &t);
        if (u->in_file) { kprint("  pendrive: fichero SOMADATA.IMG de la particion "); kprint_dec((uint32_t)u->part_index); kprint(", sectores "); }   /* SOMA_238 */
        else { kprint("  pendrive: particion "); kprint_dec((uint32_t)u->part_index); kprint(" del MBR, sectores "); }
        kprint_dec((uint32_t)u->part_lba);
        kprint(".."); kprint_dec((uint32_t)(u->part_lba + u->part_sectors - 1)); kprint(" (SOMA usa "); kprint_dec((uint32_t)(u->sectors / 2048)); kprint(" MiB)\n");
        kprint("  estado: "); kprint(u->readonly ? (u->lost ? "SOLO LECTURA (el pendrive cambio o no responde)" : "solo lectura") : "lectura y escritura");
        kprint(u->dirty ? " | cambios pendientes de vaciar\n" : " | todo vaciado al pendrive\n");
        kprint("  peticiones: lecturas "); kprint_dec((uint32_t)r); kprint(", escrituras "); kprint_dec((uint32_t)w); kprint(", errores "); kprint_dec((uint32_t)e);
        kprint(", reintentos "); kprint_dec((uint32_t)t); kprint(", vaciados "); kprint_dec((uint32_t)u->syncs); kprint("\n");
        if (u->remarked) kprint("  nota: la marca de SOMADATA no cuadraba con la tabla de particiones y se actualizo en este arranque\n");
        if (u->wait_ms >= 100) { kprint("  nota: el pendrive tardo "); kprint_dec(u->wait_ms); kprint(" ms en responder al arrancar\n"); }
    } else {
        kprint("  pendrive: "); kprint(u->why && u->why[0] ? u->why : "no se intento"); kprint("\n");
    }
    kprint("uso: disco | disco sync (vacia la cache del pendrive; se hace sola cada 5 s)\n");
    return 1;
}
/* SOMA_228_ARRANQUE: "informe": guarda el informe de arranque y hardware (INFORME.TXT del pendrive y /informe.txt); "informe ver" lo muestra */
static int cmd_informe(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    if (args && args[0] == 'v') {
        uint32_t cap = 240u * 1024u; char *b = (char *)kmalloc(cap);
        if (!b) { kprint("informe: sin memoria\n"); return 1; }
        bootdiag_report(b, cap); kprint(b); kfree(b);
        return 1;
    }
    const char *pf = bootdiag_prev_failure();
    kprint("Arranque: "); if (bootdiag_in_boot()) kprint("en curso"); else { kprint("completo en "); kprint_dec(bootdiag_boot_ms()); kprint(" ms"); } kprint("\n");
    if (pf[0]) { kprint("El arranque anterior no llego al escritorio: se quedo en la "); kprint(pf); kprint("\n"); }
    bootdiag_report_save_async();
    kprint("informe: generando y guardando en segundo plano (INFORME.TXT de la particion de arranque del pendrive y /informe.txt).\n");
    kprint("'informe ver' lo muestra aqui. El estado aparece tambien en Ajustes > Sistema.\n");
    return 1;
}

/* SOMA_165_T480S: "bateria": carga, autonomia, salud y cargador */
static void pr_cap(uint32_t v, int ma) { if (v == 0xFFFFFFFF) { kprint("?"); return; } kprint_dec(v); kprint(ma ? " mAh" : " mWh"); }
static void pr_min(uint32_t m) { kprint_dec(m / 60); kprint(" h "); if (m % 60 < 10) kprint("0"); kprint_dec(m % 60); kprint(" min"); }
static int cmd_bateria(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out; if (out_buf) out_buf[0] = 0;
    const struct acpi_sys_info *a = acpi_sys_info();
    if (!a->active) { kprint("bateria: sin ACPI ("); kprint(a->why); kprint(")\n"); return 1; }
    acpi_power_refresh_now();
    struct acpi_power_state ps; acpi_power_state(&ps);
    kprint("Cargador: "); kprint(ps.ac == 1 ? "enchufado" : ps.ac == 0 ? "desenchufado" : "desconocido");
    kprint(" | tapa: "); kprint(ps.lid == 1 ? "abierta" : ps.lid == 0 ? "cerrada" : "desconocida"); kprint("\n");
    if (!a->nbat) { kprint("No hay baterias en el firmware.\n"); return 1; }
    if (ps.percent != 0xFFFFFFFF) {
        kprint("Total: "); kprint_dec(ps.percent); kprint(" %");
        kprint(ps.charging ? " | cargando" : ps.discharging ? " | descargando" : " | sin carga ni descarga");
        if (ps.minutes != 0xFFFFFFFF) { kprint(ps.discharging ? " | quedan " : " | llena en "); pr_min(ps.minutes); }
        kprint("\n");
    }
    for (int i = 0; i < a->nbat; i++) {
        struct acpi_battery b; int r = acpi_battery_read(i, &b);
        kprint("  bateria "); kprint_dec((uint32_t)i); kprint(": ");
        if (r <= 0) { kprint(r == 0 ? "no colocada\n" : "no se pudo leer\n"); continue; }
        if (b.model[0]) { kprint(b.oem); kprint(" "); kprint(b.model); kprint(" | "); }
        if (b.percent != 0xFFFFFFFF) { kprint_dec(b.percent); kprint(" % | "); }
        kprint("queda "); pr_cap(b.remaining, b.unit_ma); kprint(" de "); pr_cap(b.full, b.unit_ma);
        kprint(" (de fabrica "); pr_cap(b.design, b.unit_ma); kprint(")");
        if (b.health != 0xFFFFFFFF) { kprint(" | salud "); kprint_dec(b.health); kprint(" %"); }
        kprint("\n    ");
        kprint(b.critical ? "CRITICA | " : ""); kprint(b.charging ? "cargando" : b.discharging ? "descargando" : "en reposo");
        if (b.rate != 0xFFFFFFFF) { kprint(" a "); kprint_dec(b.rate); kprint(b.unit_ma ? " mA" : " mW"); }
        if (b.voltage != 0xFFFFFFFF) { kprint(" | "); kprint_dec(b.voltage); kprint(" mV"); }
        if (b.cycles != 0xFFFFFFFF && b.cycles) { kprint(" | ciclos "); kprint_dec(b.cycles); }
        kprint("\n");
    }
    uint32_t pb, lid, bat; acpi_event_counts(&pb, &lid, &bat);
    kprint("Eventos: boton "); kprint_dec(pb); kprint(", tapa "); kprint_dec(lid); kprint(", bateria "); kprint_dec(bat);
    kprint(a->pb_fixed ? " | el boton de encendido apaga SOMA limpiamente\n" : "\n");
    return 1;
}
/* SOMA_208: "escritorio reiniciar" devuelve iconos y gadget a su sitio de fabrica */
extern void gui_desktop_layout_reset(void);
extern int gui_stress_request(int n, char *out, uint32_t max);   /* SOMA_220_PRUEBA */
#if defined(__x86_64__)
extern volatile int gui_rescue_inject;                            /* SOMA_221_RESCATE */
#endif
static int cmd_escritorio(const char *args, char *out_buf, uint32_t max_out)
{
    while (args && *args == ' ') args++;
    if (args && args[0] == 'r') { gui_desktop_layout_reset(); kprint("escritorio: iconos y gadget en su posicion original\n"); }
    else if (args && args[0] == 'p') {                       /* SOMA_220_PRUEBA: escritorio prueba [n] */
        int n = 0; const char *q = args; while (*q && *q != ' ') ++q; while (*q == ' ') ++q;
        while (*q >= '0' && *q <= '9') n = n * 10 + (*q++ - '0');
        static char res[2048];
        int r = gui_stress_request(n ? n : 20, res, sizeof res);
        if (r == -1) kprint("escritorio prueba: el escritorio no esta en marcha\n");
        else if (r == -2) kprint("escritorio prueba: no termino a tiempo\n");
        else kprint(res);
    }
#if defined(__x86_64__)
    else if (args && args[0] == 'f') {                       /* SOMA_221_RESCATE: escritorio fallo memoria|cuelgue */
        const char *q = args; while (*q && *q != ' ') ++q; while (*q == ' ') ++q;
        gui_rescue_inject = (*q == 'c') ? 2 : 1;
        kprint(*q == 'c' ? "escritorio: la proxima aplicacion que se dibuje entra en un bucle sin fin (se rescata a los 10 s)\n"
                         : "escritorio: la proxima aplicacion que se dibuje hace un acceso a memoria no valido\n");
    }
#endif
    else kprint("uso: escritorio reiniciar | escritorio prueba [n] | escritorio fallo memoria|cuelgue   (arrastra iconos y gadget con el raton para colocarlos; se guardan en /etc/escritorio.conf)\n");
    if (out_buf && max_out) out_buf[0] = 0;
    return 1;
}
/* SOMA_164B_T480S: "orbe prueba" */
extern int orb_parallel_selftest(char *out, uint32_t max);
static int cmd_orbe(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    while (args && *args == ' ') args++;
    if (args && args[0] == 'p') {
        static char b[512];
        orb_parallel_selftest(b, sizeof b);
        kprint(b);
        if (out_buf && max_out) out_buf[0] = 0;
        return 1;
    }
    extern uint64_t orb_perf_last[16]; extern uint32_t orb_perf_period, orb_perf_windows; extern uint64_t tsc_ticks_per_ms;
    extern uint32_t her_lay_hits, her_lay_miss; extern int her_lay_on;
    {   /* SOMA_209: orbe eco on|off | orbe cintas on|off | orbe verifica [on|off] */
        extern int her_eco_on, her_ribl_on, her_eco_verify; extern uint32_t her_eco_stat[10]; extern int orb_eco_selftest(char *out, uint32_t max);
        const char *q = args ? args : ""; while (*q && *q != ' ') q++; while (*q == ' ') q++;
        int on = (q[0] == 'o' && q[1] == 'n'), off = (q[0] == 'o' && q[1] == 'f');
        if (args && args[0] == 'e') {
            if (on) her_eco_on = 1; else if (off) her_eco_on = 0;
            kprint("ahorro por fotograma (halo por tabla, borrado y volcado por zonas): "); kprint(her_eco_on ? "activado\n" : "desactivado\n");
            if (out_buf && max_out) out_buf[0] = 0;
            return 1;
        }
        if (args && args[0] == 'v' && args[1] == 'e' && args[2] == 'n') {
            extern int her_bc_on;
            if (on) her_bc_on = 1; else if (off) her_bc_on = 0;
            kprint("contenido de ventanas en cache (hasta 70 ms): "); kprint(her_bc_on ? "activado" : "desactivado");
            kprint(" | copiado "); kprint_dec(her_eco_stat[2]); kprint(", pintado "); kprint_dec(her_eco_stat[3]); kprint("\n");
            her_eco_stat[2] = her_eco_stat[3] = 0;
            if (out_buf && max_out) out_buf[0] = 0;
            return 1;
        }
        if (args && args[0] == 'c' && args[1] == 'i') {
            if (on) her_ribl_on = 1; else if (off) her_ribl_on = 0;
            kprint("cintas en capa (se renuevan cada 80 ms): "); kprint(her_ribl_on ? "activado" : "desactivado (se pintan en cada fotograma)");
            kprint(" | cintas pintadas "); kprint_dec(her_eco_stat[0]); kprint(", reutilizadas "); kprint_dec(her_eco_stat[1]);
            kprint(" | halo calculado "); kprint_dec(her_eco_stat[9]); kprint(", copiado "); kprint_dec(her_eco_stat[8]); kprint("\n");
            her_eco_stat[0] = her_eco_stat[1] = her_eco_stat[8] = her_eco_stat[9] = 0;
            if (out_buf && max_out) out_buf[0] = 0;
            return 1;
        }
        if (args && args[0] == 'v') {
            static char b[384];
            if (on || off) { extern int32_t fb_verify_box[4]; fb_verify_box[0] = fb_verify_box[1] = 1 << 30; fb_verify_box[2] = fb_verify_box[3] = -1; her_eco_verify = on; for (int z = 0; z < 10; z++) her_eco_stat[z] = 0; kprint(on ? "verificacion en marcha (1 de cada 8 fotogramas; la interfaz ira algo mas lenta): usa el escritorio y repite 'orbe verifica'\n" : "verificacion apagada\n"); }
            else {
                orb_eco_selftest(b, sizeof b); kprint(b);
                kprint("en vivo ("); kprint(her_eco_verify ? "verificando" : "sin verificar: 'orbe verifica on'"); kprint("): fotogramas "); kprint_dec(her_eco_stat[7]);
                kprint(" | pixeles sin borrar "); kprint_dec(her_eco_stat[5]); kprint(" | pantalla distinta del bufer "); kprint_dec(her_eco_stat[6]);
                kprint(" | contenidos de ventana distintos "); kprint_dec(her_eco_stat[4]); kprint("\n");
                { extern int32_t fb_verify_box[4]; if (fb_verify_box[2] >= 0) { kprint("  pixeles sin borrar en la caja "); kprint_dec((uint32_t)fb_verify_box[0]); kprint(","); kprint_dec((uint32_t)fb_verify_box[1]); kprint(" - "); kprint_dec((uint32_t)fb_verify_box[2]); kprint(","); kprint_dec((uint32_t)fb_verify_box[3]); kprint("\n"); } }
                kprint("contenido de ventanas: copiado "); kprint_dec(her_eco_stat[2]); kprint(", pintado "); kprint_dec(her_eco_stat[3]); kprint("\n");
            }
            if (out_buf && max_out) out_buf[0] = 0;
            return 1;
        }
    }
    if (args && args[0] == 'c') {            /* SOMA_207: orbe capa on|off */
        const char *q = args + 1; while (*q && *q != ' ') q++; while (*q == ' ') q++;
        if (q[0] == 'o' && q[1] == 'f') her_lay_on = 0; else if (q[0] == 'o') her_lay_on = 1;
        kprint("capa de fondo en cache: "); kprint(her_lay_on ? "activada" : "desactivada");
        kprint(" | repintados reutilizados "); kprint_dec(her_lay_hits); kprint(", recalculados "); kprint_dec(her_lay_miss); kprint("\n");
        { extern uint32_t her_lay_why[16]; kprint("  motivos: sin_copia "); kprint_dec(her_lay_why[0]); kprint(" | omitida "); kprint_dec(her_lay_why[1]); kprint(" | caducada "); kprint_dec(her_lay_why[2]); kprint(" | cambio "); kprint_dec(her_lay_why[3]); kprint("\n"); kprint("  causas omitida: fresco "); kprint_dec(her_lay_why[4]); kprint(" arrastre "); kprint_dec(her_lay_why[5]); kprint(" iconos "); kprint_dec(her_lay_why[6]); kprint(" anim "); kprint_dec(her_lay_why[7]); kprint(" lanzador "); kprint_dec(her_lay_why[8]); kprint(" agente "); kprint_dec(her_lay_why[9]); kprint(" resp "); kprint_dec(her_lay_why[10]); kprint(" vortice "); kprint_dec(her_lay_why[11]); kprint(" flash "); kprint_dec(her_lay_why[12]); kprint("\n"); for (int z = 0; z < 16; z++) her_lay_why[z] = 0; }
        her_lay_hits = her_lay_miss = 0;
        if (out_buf && max_out) out_buf[0] = 0;
        return 1;
    }
    uint64_t d = tsc_ticks_per_ms / 100; if (!d) d = 1;                       /* centesimas de ms */
    static const char *nm[] = { "fondo y orbe", "marcos", "contenido de ventanas", "mezcla", "volcado a pantalla" };
    static const char *sub[] = { "borrado", "halo y cintas", "particulas", "", "anillo y destellos", "zonas y dock", "  de ello, halo", "  de ello, cintas" };
    kprint("Fotograma del escritorio (media de 60; "); kprint_dec(orb_perf_windows); kprint(orb_perf_windows == 1 ? " ventana abierta)\n" : " ventanas abiertas)\n");
    uint64_t tot = 0;
    for (int i = 0; i < 5; i++) { uint64_t v = orb_perf_last[i] / d; tot += v; kprint("  "); kprint(nm[i]); kprint(": "); kprint_dec((uint32_t)(v / 100)); kprint(","); if (v % 100 < 10) kprint("0"); kprint_dec((uint32_t)(v % 100)); kprint(" ms\n");
        if (i == 0) for (int k = 0; k < 8; k++) { if (!sub[k][0]) continue; uint64_t w = orb_perf_last[8 + k] / d; kprint("      "); kprint(sub[k]); kprint(": "); kprint_dec((uint32_t)(w / 100)); kprint(","); if (w % 100 < 10) kprint("0"); kprint_dec((uint32_t)(w % 100)); kprint(" ms\n"); } }
    kprint("  total "); kprint_dec((uint32_t)(tot / 100)); kprint(","); if (tot % 100 < 10) kprint("0"); kprint_dec((uint32_t)(tot % 100));
    kprint(" ms | el orbe va a "); kprint_dec(orb_perf_period ? 1000u / orb_perf_period : 0); kprint(" fps (cada "); kprint_dec(orb_perf_period); kprint(" ms) | nucleos "); kprint_dec((uint32_t)smp_cpus()); kprint("\n");
    {   /* SOMA_209: parte de la pantalla que se vuelca de media por fotograma desde la ultima consulta */
        extern uint64_t fb_dirty_px; extern uint32_t fb_dirty_n; static uint64_t lp; static uint32_t ln;
        uint64_t dpx = fb_dirty_px - lp; uint32_t dn = fb_dirty_n - ln; lp = fb_dirty_px; ln = fb_dirty_n;
        uint64_t scr = (uint64_t)fb_get_width() * fb_get_height();
        if (dn && scr) { kprint("  volcado medio: "); kprint_dec((uint32_t)(dpx * 100u / dn / scr)); kprint(" % de la pantalla por volcado ("); kprint_dec(dn); kprint(" volcados)\n"); }
    }
    kprint("uso: orbe | orbe eco|cintas|ventanas|capa [on|off] | orbe verifica [on|off] | orbe prueba (mismo dibujo con 1 y con todos los nucleos) | cpus 1 / cpus todos para comparar\n");
    if (out_buf && max_out) out_buf[0] = 0;
    return 1;
}
/* SOMA_166_T480S: "energia": perfil, temperatura, frecuencia real y consumo del procesador */
static void pr_w(uint32_t mw) { kprint_dec(mw / 1000); kprint(","); kprint_dec((mw % 1000) / 100); kprint(" W"); }
static int cmd_energia(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out; if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    const struct cpupower_info *ci = cpupower_info();
    if (args && *args) {
        int p = args[0] == 'r' ? CP_RENDIMIENTO : args[0] == 'e' ? CP_EQUILIBRADO : args[0] == 'a' ? CP_AHORRO : -1;
        if (p < 0) { kprint("uso: energia | energia rendimiento | energia equilibrado | energia ahorro\n"); return 1; }
        cpupower_set_profile(p, 1);
        kprint("energia: perfil "); kprint(cpupower_profile_name(p)); kprint(" (guardado en /etc/energia.conf)\n");
    }
    kprint("Perfil: "); kprint(cpupower_profile_name(ci->profile));
    if (ci->hwp_on) {
        kprint(" | HWP (Speed Shift): de "); kprint_dec(ci->hwp_lowest * 100); kprint(" a "); kprint_dec((ci->profile == CP_AHORRO ? ci->hwp_guaranteed : ci->hwp_highest) * 100);
        kprint(" MHz (base "); kprint_dec(ci->hwp_guaranteed * 100); kprint(", turbo "); kprint_dec(ci->hwp_highest * 100); kprint(")");
    } else kprint(" | sin HWP (frecuencia que deja el firmware)");
    kprint("\nReposo: "); if (ci->mwait) { kprint("MWAIT hasta C"); kprint_dec((uint32_t)ci->deep_c); kprint(ci->profile == CP_RENDIMIENTO ? " (en rendimiento: C1)" : ""); if (!ci->arat) kprint(" (el nucleo 0 en C1: sin ARAT)"); }
    else kprint(ci->hypervisor ? "HLT (C1; en maquina virtual no se usa MWAIT)" : "HLT (C1)");
    if (ci->pkg_cst_known) { kprint(" | limite del firmware para el paquete: "); kprint_dec(ci->pkg_cst_limit); }
    kprint("\n");
    struct cpupower_sample sm;
    if (cpupower_measure(&sm, 500) != 0) { kprint("(sin lecturas)\n"); return 1; }
    if (ci->rapl) { kprint("Consumo (0,5 s): paquete "); pr_w(sm.pkg_mw); if (ci->rapl_pp0) { kprint(" | nucleos "); pr_w(sm.cores_mw); } if (ci->rapl_pp1) { kprint(" | grafica "); pr_w(sm.gfx_mw); } kprint("\n"); }
    else kprint("Consumo: sin medidor RAPL\n");
    if (sm.pkg_temp > -1000) { kprint("Temperatura del paquete: "); kprint_dec((uint32_t)sm.pkg_temp); kprint(" C (limite "); kprint_dec((uint32_t)ci->tjmax); kprint(" C)\n"); }
    for (int i = 0; i < sm.ncores && i < CP_MAXCORE; i++) {
        kprint("  nucleo "); kprint_dec((uint32_t)i); kprint(": ");
        if (sm.temp[i] > -1000) { kprint_dec((uint32_t)sm.temp[i]); kprint(" C  "); }
        if (sm.mhz[i]) { kprint_dec(sm.mhz[i]); kprint(" MHz  despierto "); kprint_dec(sm.busy_pm[i] / 10); kprint(","); kprint_dec(sm.busy_pm[i] % 10); kprint(" %"); }
        else kprint("(sin APERF/MPERF)");
        kprint("\n");
    }
    kprint("uso: energia | energia rendimiento | energia equilibrado | energia ahorro\n");
    return 1;
}
/* SOMA_168_T480S: "brillo": luz del panel y teclas Fn del ThinkPad */
#include "backlight.h"
#include "acpi_sys.h"
#include "hda.h"
static int cmd_brillo(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out; if (out_buf) out_buf[0] = 0;
    struct backlight_info b; backlight_get(&b);
    while (args && *args == ' ') args++;
    if (args && *args) {
        if (!b.ok) { kprint("brillo: "); kprint(b.why); kprint("\n"); return 1; }
        uint32_t p = b.pct;
        if (*args == '+') p = p + 10 > 100 ? 100 : p + 10;
        else if (*args == '-') p = p < 10 ? 0 : p - 10;
        else if (*args >= '0' && *args <= '9') { p = 0; while (*args >= '0' && *args <= '9') p = p * 10 + (uint32_t)(*args++ - '0'); if (p > 100) p = 100; }
        else { kprint("uso: brillo | brillo 0-100 | brillo + | brillo -\n"); return 1; }
        if (backlight_set_pct(p, 1) != 0) kprint("brillo: la grafica no acepto el cambio\n");
        backlight_get(&b);
    }
    kprint("Brillo: ");
    if (b.ok) {
        kprint_dec(b.pct); kprint(" %"); if (b.blanked) kprint(" (apagado: tapa cerrada)");
        kprint(" | PWM del chipset: ciclo "); kprint_dec(b.duty); kprint(" de "); kprint_dec(b.max);
        kprint(" (minimo "); kprint_dec(b.max / 50 ? b.max / 50 : 1); kprint(")\n");
    } else { kprint("no disponible: "); kprint(b.why); kprint("\n"); }
    if (b.gpu_vendor && b.gpu_vendor != 0xFFFF) {
        kprint("  grafica "); kprint_hex32(b.gpu_vendor); kprint(":"); kprint_hex32(b.gpu_device);
        kprint(" | BAR0 0x"); kprint_hex32((uint32_t)(b.bar >> 32)); kprint_hex32((uint32_t)b.bar);
        if (b.ok || b.ctl1 || b.ctl2) { kprint(" | CTL1 0x"); kprint_hex32(b.ctl1); kprint(" CTL2 0x"); kprint_hex32(b.ctl2); }
        kprint("\n");
    }
    const struct acpi_sys_info *ai = acpi_sys_info();
    kprint("Teclas: volumen y silencio por el teclado | EC "); kprint(ai->ec_node ? "si" : "no");
    kprint(" (consultas "); kprint_dec(ai->ec_queries); kprint(") | HKEY ");
    if (ai->hkey) { kprint("activo, version 0x"); kprint_hex32(ai->hkey_ver); kprint(", teclas 0x"); kprint_hex32(ai->hkey_mask); }
    else kprint("no");
    kprint(" | video "); kprint(ai->video_bcl ? (ai->dos_ok ? "si (_DOS 4)" : "si") : "no"); 
    kprint(" | volumen "); if (hda_ready()) { kprint_dec((uint32_t)hda_get_volume()); kprint(" %"); } else kprint("sin audio"); kprint("\n");
    kprint("  pulsaciones "); kprint_dec(b.key_events); kprint(" | avisos de video "); kprint_dec(b.video_events);
    kprint(" | eventos HKEY "); kprint_dec(b.hkey_events); kprint(" | tapa "); kprint_dec(ai->lid_actions); kprint("\n");
    uint32_t c[8]; int k[8], n = 0; hotkey_recent(c, k, &n);
    if (n) {
        kprint("  ultimos eventos:");
        for (int i = 0; i < n; i++) { kprint(k[i] == 1 ? " EC _Q" : k[i] == 2 ? " HKEY " : " video "); kprint_hex32(c[i]); }
        kprint("\n");
    }
    kprint("uso: brillo 0-100 | brillo + | brillo -   (Fn+F5/F6 cambian el brillo; Fn+F1/F2/F3 el volumen)\n");
    return 1;
}
/* SOMA_167_T480S: "trackpad": gestos del trackpad Synaptics */
#include "syn_gest.h"
extern int trackpad_save_config(int gestos, int natural, int tap, int speed);
extern void trackpad_status(int *gestos, int *natural, int *tap, int *speed, const struct syn_state **st, uint32_t *fallbacks);
static int cmd_trackpad(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out; if (out_buf) out_buf[0] = 0;
    if (!i8042_touchpad()) { kprint("trackpad: no hay un trackpad Synaptics (raton PS/2 normal o USB)\n"); return 1; }
    int g, nat, tap, sp; const struct syn_state *st; uint32_t fb;
    trackpad_status(&g, &nat, &tap, &sp, &st, &fb);
    while (args && *args == ' ') args++;
    if (args && *args) {
        const char *a = args, *v = a; while (*v && *v != ' ') v++; while (*v == ' ') v++;
        int on = !(v[0] == 'n' && v[1] == 'o') && !(v[0] == 'o' && v[1] == 'f') && v[0] != '0';
        int want_g = g;
        if (a[0] == 'g') want_g = 1;
        else if (a[0] == 'r') want_g = 0;
        else if (a[0] == 'n') nat = *v ? on : !nat;
        else if (a[0] == 't') tap = *v ? on : !tap;
        else if (a[0] == 'v') { int n = 0; while (*v >= '0' && *v <= '9') n = n * 10 + (*v++ - '0'); if (n >= 1 && n <= 10) sp = n; }
        else { kprint("uso: trackpad | trackpad gestos | trackpad relativo | trackpad natural si|no | trackpad toque si|no | trackpad velocidad 1-10\n"); return 1; }
        trackpad_save_config(want_g, nat, tap, sp);
        trackpad_status(&g, &nat, &tap, &sp, &st, &fb);
        if (want_g && !g) kprint("trackpad: el trackpad no acepto el modo absoluto\n");
    }
    uint32_t e09, e0c, caps = i8042_syn_caps(&e09, &e0c);
    kprint("Trackpad Synaptics: modo "); kprint(g ? "GESTOS (absoluto)" : "relativo (el de fabrica)");
    kprint(" | clickpad "); kprint(i8042_syn_clickpad() ? "si" : "no"); kprint(" | TrackPoint por pass-through "); kprint(i8042_syn_passthrough() ? "si" : "no");
    kprint("\n  capacidades 0x"); kprint_hex32(caps); kprint(" ext09 0x"); kprint_hex32(e09); kprint(" ext0C 0x"); kprint_hex32(e0c); kprint("\n");
    kprint("  toque = clic: "); kprint(tap ? "si" : "no"); kprint(" | desplazamiento "); kprint(nat ? "natural (como el Mac)" : "clasico"); kprint(" | velocidad "); kprint_dec((uint32_t)sp); kprint("\n");
    if (g) {
        kprint("  paquetes "); kprint_dec(st->packets); kprint(" (malos "); kprint_dec(st->bad); kprint(") | TrackPoint "); kprint_dec(st->tp_packets);
        kprint(" | toques "); kprint_dec(st->taps); kprint(", con dos dedos "); kprint_dec(st->rtaps); kprint(" | desplazamientos "); kprint_dec(st->scrolls);
        kprint(" | palmas "); kprint_dec(st->palms); kprint("\n  ultimo: W "); kprint_dec((uint32_t)st->last_w); kprint(", presion "); kprint_dec((uint32_t)st->last_z);
        kprint(", dedos "); kprint_dec((uint32_t)st->last_fingers); kprint("\n");
    }
    if (fb) { kprint("  vueltas automaticas al modo relativo: "); kprint_dec(fb); kprint("\n"); }
    kprint("Gestos: un dedo mueve | toque = clic | toque con dos dedos = clic derecho | dos dedos arriba/abajo = desplazar |\n");
    kprint("        clic con dos dedos = clic derecho | TrackPoint + boton central = desplazar\n");
    kprint("uso: trackpad gestos | relativo | natural si|no | toque si|no | velocidad 1-10\n");
    return 1;
}
/* SOMA_164_T480S: "acpi": estado del interprete y dispositivos del firmware */
struct acpi_ls { int n, shown, all; };
static const char *acpi_hid_what(const char *h)
{
    static const struct { const char *hid, *what; } t[] = {
        { "PNP0C0A", "bateria" }, { "PNP0C0D", "tapa" }, { "PNP0C0C", "boton de encendido" }, { "PNP0C0E", "boton de suspender" },
        { "PNP0C09", "controlador embebido (EC)" }, { "ACPI0003", "cargador (AC)" }, { "LEN0068", "ThinkPad ACPI (teclas Fn)" },
        { "LEN0268", "ThinkPad ACPI (teclas Fn)" }, { "PNP0C14", "WMI" }, { "INT33A1", "energia de Intel" }, { "PNP0A08", "bus PCI Express" },
        { "PNP0A03", "bus PCI" }, { "PNP0103", "HPET" }, { "ACPI000C", "procesadores (agregador)" }, { "PNP0C0B", "ventilador" },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) { const char *x = t[i].hid, *y = h; while (*x && *x == *y) { x++; y++; } if (!*x && !*y) return t[i].what; }
    return 0;
}
static void acpi_dev_cb(const char *path, const char *hid, uint32_t sta, void *u)
{
    struct acpi_ls *l = u; l->n++;
    if (!(sta & 1)) return;                                          /* no presente */
    const char *what = acpi_hid_what(hid);
    if (!l->all && !what) return;
    l->shown++;
    kprint("  "); kprint(hid); { int k = 0; while (hid[k]) k++; for (; k < 9; k++) kprint(" "); }
    kprint(path);
    if (what) { kprint("  <- "); kprint(what); }
    kprint("\n");
}
static int cmd_acpi(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out; if (out_buf) out_buf[0] = 0;
    const struct acpi_sys_info *a = acpi_sys_info();
    kprint("ACPI: "); kprint(a->active ? "interprete uACPI activo" : a->why);
    if (!a->active && a->status) { kprint(" ("); kprint(a->status); kprint(")"); }
    kprint("\n");
    if (!a->active) return 1;
    kprint("  arranque "); kprint_dec(a->init_ms); kprint(" ms | avisos "); kprint_dec(a->warnings); kprint(", errores "); kprint_dec(a->errors); kprint("\n");
    kprint("  EC: ");
    if (a->ec_present) {
        kprint("puertos 0x"); kprint_hex16(a->ec_cmd); kprint("/0x"); kprint_hex16(a->ec_data); kprint(a->ec_from_ecdt ? " (ECDT)" : " (por defecto)");
        kprint(" | accesos "); kprint_dec((uint32_t)a->ec_ops); kprint(", sin respuesta "); kprint_dec((uint32_t)a->ec_timeouts);
    } else kprint("no hay");
    kprint("\n  SCI: IRQ "); kprint_dec(a->sci_irq); kprint(" por sondeo cada 100 ms | eventos atendidos "); kprint_dec((uint32_t)a->sci_handled); kprint("\n");
    while (args && *args == ' ') args++;
    struct acpi_ls l = { 0, 0, args && args[0] == 't' };
    kprint(l.all ? "Dispositivos presentes (_HID):\n" : "Dispositivos conocidos ('acpi todos' para la lista completa):\n");
    acpi_for_each_device(acpi_dev_cb, &l);
    kprint("  ("); kprint_dec((uint32_t)l.shown); kprint(" mostrados de "); kprint_dec((uint32_t)l.n); kprint(" con _HID)\n");
    kprint("uso: acpi | acpi todos | poweroff (apaga por ACPI S5) | reboot\n");
    return 1;
}
/* SOMA_158_T480S: "irq": como llegan las interrupciones (PIC o APIC), el reloj y los contadores */
static int cmd_irq(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    const struct apic_sys_info *ai = apic_system_info();
    kprint("Interrupciones: "); kprint(ai->active ? "APIC (LAPIC + IOAPIC), PIC 8259 enmascarado" : "PIC 8259 (modo antiguo)");
    kprint("\n  estado: "); kprint(ai->why ? ai->why : "sin iniciar"); kprint("\n");
    if (ai->active) {
        kprint("  LAPIC id "); kprint_dec(ai->lapic_id); kprint(ai->x2apic ? " (x2APIC)" : " (xAPIC)");
        kprint(" | ARAT "); kprint(ai->arat ? "si" : "no");
        kprint(" | IOAPIC id "); kprint_dec(ai->ioapic_id); kprint(" en 0x"); kprint_hex32(ai->ioapic_addr); kprint("\n");
        kprint("  reloj: ");
        if (ai->timer_lapic) {
            kprint("temporizador del LAPIC, "); kprint_dec(ai->ticks_per_ms); kprint(" cuentas/ms (bus del LAPIC ");
            kprint_dec(ai->ticks_per_ms * 16u / 1000u); kprint(" MHz)");
        } else kprint("PIT 1 kHz por el IOAPIC");
        kprint(" | comprobacion: "); kprint_dec(ai->verify_ticks); kprint(" ticks en 30 ms\n");
        const char *nm[3] = { "IRQ0  PIT     ", "IRQ1  teclado ", "IRQ12 trackpad" };
        for (int i = 0; i < 3; i++) {
            kprint("  "); kprint(nm[i]); kprint(" -> GSI "); kprint_dec(ai->gsi[i]);
            kprint(ai->level[i] ? " nivel" : " flanco"); kprint(ai->low[i] ? "/baja" : "/alta");
            if (ai->io_used[i]) { uint64_t e = apic_io_redir(ai->io_index[i]); kprint(" | IOAPIC: vector "); kprint_dec((uint32_t)(e & 0xFF)); kprint((e >> 16) & 1 ? " ENMASCARADA" : " activa"); }
            else kprint(" | no se usa");
            kprint("\n");
        }
    }
    kprint("  atendidas: reloj "); kprint_dec((uint32_t)idt_irq_count(32)); kprint(", teclado "); kprint_dec((uint32_t)idt_irq_count(33));
    kprint(", trackpad "); kprint_dec((uint32_t)idt_irq_count(44)); kprint(", espurias LAPIC "); kprint_dec((uint32_t)idt_irq_count(255));
    kprint(", EOI LAPIC "); kprint_dec(apic_eoi_count()); kprint("\n  uptime "); kprint_dec((uint32_t)(timer_get_uptime_ms() / 1000)); kprint(" s\n");
    if (ai->active) {
        uint64_t tk = 0, ts = 0; apic_clock_drift(&tk, &ts);
        kprint("  precision: el reloj conto "); kprint_dec((uint32_t)tk); kprint(" ms; el TSC mide "); kprint_dec((uint32_t)ts); kprint(" ms");
        if (ts > 1000) {
            int64_t dpm = ((int64_t)tk - (int64_t)ts) * 1000 / (int64_t)ts;    /* por mil */
            kprint(" (deriva "); if (dpm < 0) { kprint("-"); dpm = -dpm; } kprint_dec((uint32_t)dpm); kprint(" por mil)");
        }
        kprint("\n");
    }
    if (out_buf && max_out) out_buf[0] = 0;
    return 1;
}
#endif
/* SOMA_156_T480S: "dmesg [N | texto]": registro del nucleo desde el arranque (ultimas N lineas, o las que contienen texto) */
static int dmesg_has(const char *l, uint32_t n, const char *w)
{
    for (uint32_t i = 0; i < n; i++) { uint32_t k = 0; while (w[k] && i + k < n && l[i + k] == w[k]) k++; if (!w[k]) return 1; }
    return 0;
}
static int cmd_dmesg(const char *args, char *out_buf, uint32_t max_out)
{
    while (args && *args == ' ') args++;
    uint32_t want = 60; const char *filt = 0;
    if (args && *args >= '0' && *args <= '9') { want = 0; while (*args >= '0' && *args <= '9') want = want * 10 + (uint32_t)(*args++ - '0'); if (want > 3000) want = 3000; }
    else if (args && *args) { filt = args; want = 300; }
    if (!want) want = 1;
    char *snap = (char *)kmalloc(256u * 1024u + 1);
    if (!snap) { kprint("dmesg: sin memoria\n"); return 1; }
    uint32_t n = klog_snapshot(snap, 256u * 1024u + 1);
    /* 1) cuantas lineas cumplen (todas, o las que contienen el texto) */
    uint32_t total = 0;
    for (uint32_t i = 0; i < n;) {
        uint32_t j = i; while (j < n && snap[j] != '\n') j++;
        if (!filt || dmesg_has(snap + i, j - i, filt)) total++;
        i = j + 1;
    }
    /* 2) se imprimen solo las `want` ultimas */
    uint32_t skip = total > want ? total - want : 0, idx = 0, shown = 0;
    klog_pause(1);                                             /* lo que imprime dmesg no se vuelve a registrar */
    for (uint32_t i = 0; i < n;) {
        uint32_t j = i; while (j < n && snap[j] != '\n') j++;
        if (!filt || dmesg_has(snap + i, j - i, filt)) {
            if (idx++ >= skip) {                                 /* SOMA_156C: sin codigos de control (un "clear" registrado borraba la pantalla) */
                char ln[256]; uint32_t w = 0;
                for (uint32_t r = i; r < j && w < sizeof ln - 1; r++) {
                    char ch = snap[r];
                    if (ch == 27) { if (r + 1 < j && snap[r + 1] == '[') { r += 2; while (r < j && !((snap[r] >= 'A' && snap[r] <= 'Z') || (snap[r] >= 'a' && snap[r] <= 'z'))) r++; } continue; }
                    if ((unsigned char)ch < 32 && ch != '\t') continue;
                    ln[w++] = ch;
                }
                ln[w] = 0; kprint(ln); kprint("\n"); shown++;
            }
        }
        i = j + 1;
    }
    kprint("[dmesg: "); kprint_dec(shown); kprint(" de "); kprint_dec(total); kprint(filt ? " lineas con ese texto" : " lineas");
    kprint(" | "); kprint_dec((uint32_t)(klog_written() / 1024)); kprint(" KiB registrados. Uso: dmesg [N | texto]]\n");
    klog_pause(0);
    kfree(snap);
    if (out_buf && max_out) out_buf[0] = 0;
    return 1;
}
/* PARCHE 085: "navegador [url]" */
static int cmd_navegador(const char *args, char *out_buf, uint32_t max_out)
{
    if (agent_net_denied("navegador", args, out_buf, max_out)) return 1;   /* SOMA_193 */
    uint32_t p = 0;
    while (args && *args == ' ') args++;
    if (args && *args) gui_open_browser(args); else gui_open_web();
    soma_str_put(out_buf, max_out, &p, "Navegador abierto.");
    return 1;
}

/* PARCHE 085: "ajustes" - transparencia de las ventanas y fuente de la interfaz */
static int cmd_ajustes(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t p = 0;
    gui_open_settings();
    soma_str_put(out_buf, max_out, &p, "Ajustes abiertos.");
    return 1;
}

/* PARCHE 084: "latex [archivo.tex]" - abre el maquetador (documento LaTeX) */
static int cmd_latex(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t p = 0;
    kprint("Iniciando el maquetador LaTeX...\n");
    if (*args) gui_open_layout_file(args); else gui_open_layout();
    soma_str_put(out_buf, max_out, &p, "Maquetador abierto.");
    return 1;
}

static int cmd_paint(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t p = 0;
    kprint("Iniciando SOMA Paint...\n");
    gui_open_paint(args);
    soma_str_put(out_buf, max_out, &p, "SOMA Paint abierto.");
    return 1;
}

/* SOMA_PARCHE_024_TERM: terminal [debug] -> abre la ventana del terminal de la GUI */
static int cmd_terminal(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    int dbg = (args[0] == 'd' && args[1] == 'e' && args[2] == 'b');
    kprint("Iniciando Terminal SOMA...\n");
    gui_open_terminal(dbg);
    uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Terminal cerrado.");
    return 1;
}

/* SOMA_PARCHE_023_MEDIA: media gui [ruta] | list | play ruta | pause | stop | seek seg | status */
static int cmd_media(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (args[0] == 'g' && args[1] == 'u' && args[2] == 'i' && (args[3] == ' ' || args[3] == 0)) {
        args += 3; while (*args == ' ') args++;
        kprint("Iniciando SOMA Media...\n");
        gui_open_media(args);
        uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "SOMA Media cerrado.");
        return 1;
    }
    if (media_command(args, out_buf, max_out)) return 1;
    uint32_t p = 0;
    soma_str_put(out_buf, max_out, &p, "uso: media gui [ruta] | list | play <ruta> | pause | stop | seek <segundos> | status\n(rutas del almacen BIGFS: .avi con video MJPG + audio PCM, o .wav)");
    kprint(out_buf); kprint("\n");
    return 1;
}

static int cmd_memview(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t p = 0;
    kprint("Iniciando Visor de Memoria Multi-Agente...\n");
    gui_open_memview();
    soma_str_put(out_buf, max_out, &p, "Visor de memoria multi-agente abierto.");
    return 1;
}

static int cmd_view(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (!*args) {
        kprint("Uso: view <archivo.bmp|ppm>\n");
        uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Uso: view <archivo.bmp>");
        return 1;
    }

    static uint8_t v_buf[24576];
    int r = vfs_read(args, (char *)v_buf, sizeof(v_buf) - 1);
    if (r <= 0) {
        kprint("view: archivo no encontrado: '"); kprint(args); kprint("'\n");
        uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "view: archivo no encontrado");
        return 1;
    }

    struct image img;
    if (image_decode(v_buf, (size_t)r, &img) == 0) {
        kprint("\n[IMAGEN]: "); kprint(args);
        kprint(" | Formato: "); kprint(img.format == IMAGE_FMT_BMP ? "BMP TrueColor" : "PPM");
        kprint(" | Dimensiones: "); kprint_dec(img.width); kprint("x"); kprint_dec(img.height);
        kprint(" px | Pixeles: "); kprint_dec(img.width * img.height); kprint("\n");

        if (fb_is_active()) {
            image_render(&img, 100, 100, (int32_t)img.width, (int32_t)img.height);
            fb_present();
            kprint("[view]: Renderizada en pantalla grafica activa.\n");
        } else {
            kprint("[view]: Para verla en pantalla completa, abre el navegador con 'vsoma'.\n");
        }

        uint32_t p = 0;
        soma_str_put(out_buf, max_out, &p, "Imagen valida: ");
        soma_str_put_dec(out_buf, max_out, &p, img.width); soma_str_put(out_buf, max_out, &p, "x");
        soma_str_put_dec(out_buf, max_out, &p, img.height);
        image_free(&img);
    } else {
        kprint("view: formato de imagen no reconocido o archivo corrupto.\n");
        uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "view: formato no soportado");
    }
    return 1;
}

static int cmd_font(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t pos = 0;

    /* SOMA_VECTOR_FONTS_PATCH: 'font size <40-300>' y 'font contrast <0-100>' */
    if (args[0] == 's' && args[1] == 'i' && args[2] == 'z' && args[3] == 'e' &&
        (args[4] == ' ' || args[4] == '\0')) {
        const char *q = args + 4;
        while (*q == ' ') q++;
        if (*q >= '0' && *q <= '9') {
            int v = 0;
            while (*q >= '0' && *q <= '9' && v < 100000) { v = v * 10 + (*q - '0'); q++; }
            font_vector_set_size(v);
        }
        kprint("\n\033[1;32m[FONT]: tamano vectorial = ");
        kprint_dec((uint32_t)font_vector_get_size());
        kprint("%\033[0m\n\n");
        soma_str_put(out_buf, max_out, &pos, "Tamano vectorial: ");
        soma_str_put_dec(out_buf, max_out, &pos, font_vector_get_size());
        soma_str_put(out_buf, max_out, &pos, "%");
        if (fb_is_active()) fb_present_full();
        return 1;
    }
    if (args[0] == 'c' && args[1] == 'o' && args[2] == 'n' && args[3] == 't' &&
        args[4] == 'r' && args[5] == 'a' && args[6] == 's' && args[7] == 't' &&
        (args[8] == ' ' || args[8] == '\0')) {
        const char *q = args + 8;
        while (*q == ' ') q++;
        if (*q >= '0' && *q <= '9') {
            int v = 0;
            while (*q >= '0' && *q <= '9' && v < 100000) { v = v * 10 + (*q - '0'); q++; }
            font_vector_set_contrast(v);
        }
        kprint("\n\033[1;32m[FONT]: contraste vectorial = ");
        kprint_dec((uint32_t)font_vector_get_contrast());
        kprint("\033[0m\n\n");
        soma_str_put(out_buf, max_out, &pos, "Contraste vectorial: ");
        soma_str_put_dec(out_buf, max_out, &pos, font_vector_get_contrast());
        if (fb_is_active()) fb_present_full();
        return 1;
    }


    if (*args == '\0') {
        kprint("\nFUENTES TIPOGRAFICAS DISPONIBLES EN SOMA:\n");
        kprint("------------------------------------------\n");
        soma_str_put(out_buf, max_out, &pos, "Fuentes disponibles:\n");
        int cur = font_get_current();
        for (int order = 0; order < font_get_count(); ++order) {
            int i = font_get_sorted_id(order);
            if (i < 0) continue;
            kprint("  ["); kprint_dec((uint32_t)i); kprint("] ");
            kprint(font_get_name(i));
            kprint(" - ");
            kprint(font_get_desc(i));
            if (i == cur) kprint("  \033[1;32m<-- ACTIVA\033[0m");
            kprint("\n");

            soma_str_put(out_buf, max_out, &pos, "  [");
            soma_str_put_dec(out_buf, max_out, &pos, i);
            soma_str_put(out_buf, max_out, &pos, "] ");
            soma_str_put(out_buf, max_out, &pos, font_get_name(i));
            soma_str_put(out_buf, max_out, &pos, " - ");
            soma_str_put(out_buf, max_out, &pos, font_get_desc(i));
            if (i == cur) soma_str_put(out_buf, max_out, &pos, " (ACTIVA)");
            soma_str_putc(out_buf, max_out, &pos, '\n');
        }
        kprint("------------------------------------------\n");
        kprint("Uso: font <nombre|id>  (ej. 'font modern' o 'font 0')\n\n");
        return 1;
    }

    int ok = 0;
    if (*args >= '0' && *args <= '9') {
        int fid = 0;
        const char *q = args;
        while (*q >= '0' && *q <= '9' && fid < 100000) { fid = fid * 10 + (*q - '0'); q++; }
        while (*q == ' ') q++;
        if (*q != '\0') fid = -1;
        ok = font_set(fid);
    } else {
        ok = font_set_by_name(args);
    }

    if (ok) {
        int cur = font_get_current();
        kprint("\n\033[1;32m[FONT]: Fuente del sistema cambiada a: '");
        kprint(font_get_name(cur));
        kprint("' (");
        kprint(font_get_desc(cur));
        kprint(")\033[0m\n\n");

        soma_str_put(out_buf, max_out, &pos, "Fuente cambiada a: ");
        soma_str_put(out_buf, max_out, &pos, font_get_name(cur));

        if (fb_is_active()) {
            fb_present_full();
        }
    } else {
        kprint("\n\033[1;31m[ERROR]: Fuente no reconocida '");
        kprint(args);
        kprint("'. Escribe 'font' para listar las disponibles.\033[0m\n\n");
        soma_str_put(out_buf, max_out, &pos, "Error: fuente no reconocida");
    }
    return 1;
}

static int cmd_cursor(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t pos = 0;

    if (*args == '\0') {
        kprint("\nPUNTEROS DE RATON DISPONIBLES EN SOMA:\n");
        kprint("---------------------------------------\n");
        soma_str_put(out_buf, max_out, &pos, "Punteros disponibles:\n");
        int cur = mouse_get_current_cursor();
        for (int i = 0; i < mouse_get_cursor_count(); ++i) {
            kprint("  ["); kprint_dec((uint32_t)i); kprint("] ");
            kprint(mouse_get_cursor_name(i));
            kprint(" - ");
            kprint(mouse_get_cursor_desc(i));
            if (i == cur) kprint("  \033[1;32m<-- ACTIVO\033[0m");
            kprint("\n");

            soma_str_put(out_buf, max_out, &pos, "  [");
            soma_str_put_dec(out_buf, max_out, &pos, i);
            soma_str_put(out_buf, max_out, &pos, "] ");
            soma_str_put(out_buf, max_out, &pos, mouse_get_cursor_name(i));
            soma_str_put(out_buf, max_out, &pos, " - ");
            soma_str_put(out_buf, max_out, &pos, mouse_get_cursor_desc(i));
            if (i == cur) soma_str_put(out_buf, max_out, &pos, " (ACTIVO)");
            soma_str_putc(out_buf, max_out, &pos, '\n');
        }
        kprint("---------------------------------------\n");
        kprint("Uso: cursor <nombre|id>  (ej. 'cursor crosshair' o 'cursor 2')\n\n");
        return 1;
    }

    int ok = 0;
    if (*args >= '0' && *args <= '9' && *(args + 1) == '\0') {
        ok = mouse_set_cursor(*args - '0');
    } else {
        ok = mouse_set_cursor_by_name(args);
    }

    if (ok) {
        int cur = mouse_get_current_cursor();
        kprint("\n\033[1;32m[CURSOR]: Puntero cambiado a: '");
        kprint(mouse_get_cursor_name(cur));
        kprint("' (");
        kprint(mouse_get_cursor_desc(cur));
        kprint(")\033[0m\n\n");

        soma_str_put(out_buf, max_out, &pos, "Puntero cambiado a: ");
        soma_str_put(out_buf, max_out, &pos, mouse_get_cursor_name(cur));

        if (fb_is_active()) {
            fb_present_full();
        }
    } else {
        kprint("\n\033[1;31m[ERROR]: Puntero no reconocido '");
        kprint(args);
        kprint("'. Escribe 'cursor' para ver la lista.\033[0m\n\n");
        soma_str_put(out_buf, max_out, &pos, "Error: puntero no reconocido");
    }
    return 1;
}
