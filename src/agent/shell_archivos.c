/* shell_archivos.c: archivos: ls, cat, write, rm, cp, mv, grep, head, tail, nano, checkpoints y sectores.
 * SOMA_233_SHELL_PARTIDO: NO se compila por separado. Lo incluye src/agent/shell.c en su sitio (una sola unidad de
 * compilacion: usa las funciones auxiliares y la tabla de ordenes de shell.c, y el nucleo compilado sale identico). */
static int cmd_ls(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    int show_all = 0;
    char filter[64]; filter[0] = '\0';
    uint32_t fp = 0;

    while (*args) {
        while (*args == ' ') args++;
        if (*args == '-') {
            while (*args && *args != ' ') {
                if (*args == 'a' || *args == 'A') show_all = 1;
                args++;
            }
        } else if (*args) {
            while (*args && *args != ' ' && fp < sizeof(filter) - 1) {
                filter[fp++] = *args++;
            }
            filter[fp] = '\0';
        }
    }

    vfs_list_ext(show_all, filter[0] ? filter : 0);
    vfs_format_list_ext(show_all, filter[0] ? filter : 0, out_buf, max_out);
    return 1;
}

static int cmd_cat(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        if (pipe_stdin_data != 0) {
            kprint(pipe_stdin_data);
            uint32_t p = 0; soma_str_put(out_buf, max_out, &p, pipe_stdin_data);
            return 1;
        }
        kprint("Uso: cat <archivo1> [archivo2 ...]\n");
        uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Uso: cat <archivo>");
        return 1;
    }

    uint32_t out_pos = 0;
    if (out_buf && max_out) out_buf[0] = '\0';

    while (*args) {
        while (*args == ' ') args++;
        if (*args == '\0') break;

        char fn[64];
        uint32_t fi = 0;
        while (*args && *args != ' ' && fi < sizeof(fn) - 1) {
            fn[fi++] = *args++;
        }
        fn[fi] = '\0';

        char buf[2048];
        int r = vfs_read(fn, buf, sizeof(buf) - 1);
        if (r >= 0) {
            buf[r] = '\0';
            kprint(buf);
            soma_str_put(out_buf, max_out, &out_pos, buf);
        } else {
            kprint("cat: no encontrado '"); kprint(fn); kprint("'\n");
            soma_str_put(out_buf, max_out, &out_pos, "cat: no encontrado");
        }
    }
    return 1;
}

static int cmd_write(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char fn[64]; uint32_t fi = 0;
    while (*args && *args != ' ' && fi < sizeof(fn) - 1) fn[fi++] = *args++;
    fn[fi] = '\0';
    while (*args == ' ') args++;

    if (agent_write_denied(fn, "write", out_buf, max_out)) return 1;       /* PARCHE 110: toda la zona protegida, no solo dos archivos */
    if (agent_dispatch_mode && (str_eq(fn, "/etc/init.sh") || str_eq(fn, "/etc/soma_manifest.txt"))) {
        kprint("write: operacion denegada por seguridad (Agente no puede modificar scripts de arranque).\n");
        uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "Error: Operacion denegada por politica de seguridad.");
        return 1;
    }

        char q = 0;
    if (*args == '"' || *args == '\'') {
        q = *args++;
    }

    const char *end_q = 0; /*PL7_WRITE_ROBUST*/
    if (q) {
        const char *scan = args;
        while (*scan) {
            if (*scan == '\\' && *(scan + 1) != '\0') {
                scan += 2;
                continue;
            }
            if (*scan == q) end_q = scan;
            scan++;
        }
    }

    char *payload = (char *)kmalloc(FS_DATA_MAX);
    if (!payload) {
        kprint("write: sin memoria para buffer de escritura.\n");
        return 1;
    }

    uint32_t pi = 0;
    while (*args && pi < FS_DATA_MAX - 1) {
        if (q && args == end_q) {
            break;
        }
        if (*args == '\\' && *(args + 1) != '\0') { /*PL6_WRITE_ESCAPES*/
            char nx = *(args + 1);
            if (nx == 'n') { payload[pi++] = '\n'; args += 2; continue; }
            if (nx == 't') { payload[pi++] = '\t'; args += 2; continue; }
            if (nx == '"') { payload[pi++] = '"'; args += 2; continue; }
            if (nx == '\'') { payload[pi++] = '\''; args += 2; continue; }
            if (nx == '\\') { payload[pi++] = '\\'; args += 2; continue; }
        }
        payload[pi++] = *args++;
    }
    payload[pi] = '\0';

    int wres = vfs_write(fn, payload, pi);
    kfree(payload);

    if (wres == -2) {
        kprint("write: error, archivo demasiado grande (max 16383 bytes).\n");
        uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "Error: El archivo excede la cuota maxima de 16383 bytes.");
        return 1;
    }
    if (wres < 0) {
        kprint("write: error al escribir '"); kprint(fn); kprint("' (acceso denegado o fallo de disco).\n");
        uint32_t epos = 0;
        soma_str_put(out_buf, max_out, &epos, "Error: no se pudo escribir el archivo (denegado o fallo de I/O).");
        return 1;
    }
    kprint("Escrito en '"); kprint(fn); kprint("' (");
    kprint_dec(pi); kprint(" bytes)\n");
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "Archivo escrito.");
    return 1;
}

static int cmd_rm(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t pos = 0;
    if (*args == '\0') {
        kprint("Uso: rm <archivo>\n");
        soma_str_put(out_buf, max_out, &pos, "Error: falta el nombre del archivo.");
        return 1;
    }
    if (agent_write_denied(args, "rm", out_buf, max_out)) return 1;
    int r = vfs_delete(args);
    if (r == 0) {
        kprint("Eliminado '"); kprint(args); kprint("'\n");
        soma_str_put(out_buf, max_out, &pos, "Archivo eliminado.");
    } else {
        kprint("rm: no se pudo eliminar '"); kprint(args); kprint("'\n");
        soma_str_put(out_buf, max_out, &pos,
                "Error: archivo no encontrado o eliminacion denegada.");
    }
    return 1;
}

static int cmd_cp(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char src[48], dst[48]; uint32_t si = 0, di = 0;
    while (*args && *args != ' ' && si < sizeof(src) - 1) src[si++] = *args++;
    src[si] = '\0';
    while (*args == ' ') args++;
    while (*args && *args != ' ' && di < sizeof(dst) - 1) dst[di++] = *args++;
    dst[di] = '\0';
    if (agent_write_denied(dst, "cp", out_buf, max_out)) return 1;
    uint32_t rl = 0; char *cp_buf = vfs_read_alloc(src, &rl);   /* PARCHE 087: antes copiaba solo los primeros 4 KiB */
    uint32_t pos = 0;
    if (!cp_buf) { kprint("cp: origen no encontrado\n"); soma_str_put(out_buf, max_out, &pos, "cp: error (no existe o sin memoria)"); }
    else if (vfs_write(dst, cp_buf, rl) >= 0) { kprint("cp: copiado OK\n"); soma_str_put(out_buf, max_out, &pos, "cp: OK"); }
    else soma_str_put(out_buf, max_out, &pos, "cp: no se pudo escribir");
    if (cp_buf) kfree(cp_buf);
    return 1;
}

static int cmd_mv(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char src[48], dst[48]; uint32_t si = 0, di = 0;
    while (*args && *args != ' ' && si < sizeof(src) - 1) src[si++] = *args++;
    src[si] = '\0';
    while (*args == ' ') args++;
    while (*args && *args != ' ' && di < sizeof(dst) - 1) dst[di++] = *args++;
    dst[di] = '\0';
    if (agent_write_denied(src, "mv", out_buf, max_out) || agent_write_denied(dst, "mv", out_buf, max_out)) return 1;
    uint32_t rl = 0; char *mv_buf = vfs_read_alloc(src, &rl);   /* PARCHE 087 */
    uint32_t pos = 0;
    if (!mv_buf) { kprint("mv: origen no encontrado\n"); soma_str_put(out_buf, max_out, &pos, "mv: error (no existe o sin memoria)"); }
    else if (vfs_write(dst, mv_buf, rl) >= 0) { vfs_delete(src); kprint("mv: movido OK\n"); soma_str_put(out_buf, max_out, &pos, "mv: OK"); }
    else soma_str_put(out_buf, max_out, &pos, "mv: no se pudo escribir");
    if (mv_buf) kfree(mv_buf);
    return 1;
}

static int cmd_touch(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char tmp[8]; uint32_t pos = 0;
    if (agent_write_denied(args, "touch", out_buf, max_out)) return 1;
    if (vfs_read(args, tmp, sizeof(tmp)) < 0) { vfs_write(args, "", 0); kprint("touch: creado\n"); soma_str_put(out_buf, max_out, &pos, "touch: OK"); }
    else { kprint("touch: ya existe\n"); soma_str_put(out_buf, max_out, &pos, "touch: existe"); }
    return 1;
}

static int cmd_tree(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    int show_all = 0;
    if (args[0] == '-' && (args[1] == 'a' || args[1] == 'A')) {
        show_all = 1;
    }
    vfs_tree_ext(show_all, out_buf, max_out);
    return 1;
}

static int cmd_head(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char fn[48]; uint32_t fi = 0;
    uint32_t lines = 10;
    const char *src_text = 0;
    static char hbuf[4096];

    /* Parsear opciones: -n <N> o -<N> */
    while (*args == '-') {
        args++;
        if (*args == 'n' || *args == 'N') {
            args++;
            while (*args == ' ') args++;
            lines = 0;
            while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        } else if (*args >= '0' && *args <= '9') {
            lines = 0;
            while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        }
        while (*args == ' ') args++;
    }

    if (pipe_stdin_data != 0 && *args == '\0') {
        src_text = pipe_stdin_data;
    } else {
        while (*args && *args != ' ' && fi < sizeof(fn) - 1) {
            fn[fi++] = *args++;
        }
        fn[fi] = '\0';
        while (*args == ' ') args++;
        if (*args >= '0' && *args <= '9' && lines == 10) {
            lines = 0; while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        }
        if (fn[0] == '\0') {
            if (pipe_stdin_data != 0) {
                src_text = pipe_stdin_data;
            } else {
                kprint("Uso: head [-n N] <archivo>\n");
                uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Uso: head [-n N] <archivo>");
                return 1;
            }
        } else {
            int r = vfs_read(fn, hbuf, sizeof(hbuf) - 1);
            if (r < 0) {
                kprint("head: no encontrado: '"); kprint(fn); kprint("'\n");
                uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "head: no encontrado");
                return 1;
            }
            hbuf[r] = '\0';
            src_text = hbuf;
        }
    }

    if (lines == 0) lines = 10;
    uint32_t lc = 0, pos = 0;
    for (int i = 0; src_text[i] && lc < lines; ++i) {
        kputc(src_text[i]);
        if (pos < max_out - 1) out_buf[pos++] = src_text[i];
        if (src_text[i] == '\n') lc++;
    }
    out_buf[pos] = '\0';
    return 1;
}

static int cmd_tail(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char fn[48]; uint32_t fi = 0;
    uint32_t lines = 10;
    const char *src_text = 0;
    static char tbuf[4096];
    uint32_t total_len = 0;

    /* Parsear opciones: -n <N> o -<N> */
    while (*args == '-') {
        args++;
        if (*args == 'n' || *args == 'N') {
            args++;
            while (*args == ' ') args++;
            lines = 0;
            while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        } else if (*args >= '0' && *args <= '9') {
            lines = 0;
            while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        }
        while (*args == ' ') args++;
    }

    if (pipe_stdin_data != 0 && *args == '\0') {
        src_text = pipe_stdin_data;
        total_len = pipe_stdin_len;
    } else {
        while (*args && *args != ' ' && fi < sizeof(fn) - 1) {
            fn[fi++] = *args++;
        }
        fn[fi] = '\0';
        while (*args == ' ') args++;
        if (*args >= '0' && *args <= '9' && lines == 10) {
            lines = 0; while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        }
        if (fn[0] == '\0') {
            if (pipe_stdin_data != 0) {
                src_text = pipe_stdin_data;
                total_len = pipe_stdin_len;
            } else {
                kprint("Uso: tail [-n N] <archivo>\n");
                uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Uso: tail [-n N] <archivo>");
                return 1;
            }
        } else {
            int r = vfs_read(fn, tbuf, sizeof(tbuf) - 1);
            if (r < 0) {
                kprint("tail: no encontrado: '"); kprint(fn); kprint("'\n");
                uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "tail: no encontrado");
                return 1;
            }
            total_len = (uint32_t)r;
            tbuf[r] = '\0';
            src_text = tbuf;
        }
    }

    if (lines == 0) lines = 10;
    uint32_t tl = 0; for (uint32_t i = 0; i < total_len; ++i) if (src_text[i] == '\n') tl++;
    uint32_t skip = (tl > lines) ? (tl - lines) : 0, cl = 0, si = 0;
    while (si < total_len && cl < skip) if (src_text[si++] == '\n') cl++;
    uint32_t pos = 0;
    for (uint32_t i = si; i < total_len; ++i) {
        kputc(src_text[i]);
        if (pos < max_out - 1) out_buf[pos++] = src_text[i];
    }
    out_buf[pos] = '\0';
    return 1;
}

static int cmd_wc(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    static char wcbuf[4096];
    const char *src_text = 0;
    uint32_t text_len = 0;

    if (*args != '\0') {
        int r = vfs_read(args, wcbuf, sizeof(wcbuf));
        if (r < 0) {
            uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "wc: no encontrado");
            return 1;
        }
        text_len = (uint32_t)r;
        wcbuf[r] = '\0';
        src_text = wcbuf;
    } else if (pipe_stdin_data != 0) {
        src_text = pipe_stdin_data;
        text_len = pipe_stdin_len;
    } else {
        kprint("Uso: wc [archivo]  (o bien: <cmd> | wc)\n");
        return 1;
    }

    uint32_t l = 0, w = 0; int in_w = 0;
    for (uint32_t i = 0; i < text_len; ++i) {
        if (src_text[i] == '\n') l++;
        if (src_text[i] == ' ' || src_text[i] == '\t' || src_text[i] == '\n' || src_text[i] == '\r') in_w = 0;
        else if (!in_w) { in_w = 1; w++; }
    }
    kprint("  "); kprint_dec(l); kprint(" lineas  "); kprint_dec(w); kprint(" palabras  "); kprint_dec(text_len); kprint(" bytes\n");
    uint32_t pos = 0;
    soma_str_put_dec(out_buf, max_out, &pos, l); soma_str_put(out_buf, max_out, &pos, " lineas, ");
    soma_str_put_dec(out_buf, max_out, &pos, w); soma_str_put(out_buf, max_out, &pos, " palabras, ");
    soma_str_put_dec(out_buf, max_out, &pos, text_len); soma_str_put(out_buf, max_out, &pos, " bytes");
    return 1;
}

/* grep V2: sin archivo → busca en TODO el RamFS.
 * Sintaxis: grep [-i] [-l] <patron> [archivo]
 *   -i  ignore case
 *   -l  solo listar nombres de archivo con match
 * Un solo archivo opcional (no multi-file estilo GNU). */
static int cmd_grep(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    int ignore_case = 0;
    int list_only = 0;

    /* Flags: -i -l -r ( -r = global, se ignora la lista de archivos posterior ) */
    int force_global = 0;
    for (;;) {
        if (args[0] == '-' && args[1] == 'i' && (args[2] == ' ' || args[2] == '\0')) {
            ignore_case = 1; args += 2;
            while (*args == ' ') args++;
            continue;
        }
        if (args[0] == '-' && args[1] == 'l' && (args[2] == ' ' || args[2] == '\0')) {
            list_only = 1; args += 2;
            while (*args == ' ') args++;
            continue;
        }
        if (args[0] == '-' && args[1] == 'r' && (args[2] == ' ' || args[2] == '\0')) {
            force_global = 1; args += 2;
            while (*args == ' ') args++;
            continue;
        }
        if (args[0] == '-' && args[1] == 'i' && args[2] == 'l' &&
            (args[3] == ' ' || args[3] == '\0')) {
            ignore_case = 1; list_only = 1; args += 3;
            while (*args == ' ') args++;
            continue;
        }
        if (args[0] == '-' && args[1] == 'l' && args[2] == 'i' &&
            (args[3] == ' ' || args[3] == '\0')) {
            ignore_case = 1; list_only = 1; args += 3;
            while (*args == ' ') args++;
            continue;
        }
        break;
    }

    char pat[64]; uint32_t pi = 0; char q = 0;
    if (*args == '\'' || *args == '"') q = *args++;
    while (*args && pi < sizeof(pat) - 1) {
        if (q && *args == q) { args++; break; }
        if (!q && *args == ' ') break;
        pat[pi++] = *args++;
    }
    pat[pi] = '\0';
    while (*args == ' ') args++;

    if (pat[0] == '\0') {
        kprint("Uso: grep [-i] [-l] <patron> [archivo]\n");
        kprint("  Sin archivo: busca en todo el RamFS.\n");
        uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Uso: grep [-i] [-l] <patron> [archivo]");
        return 1;
    }

    /* Extraer SOLO el primer path (ignorar multi-file GNU) */
    char fname[FS_NAME_MAX];
    uint32_t fi = 0;
    if (*args != '\0') {
        if (*args == '\'' || *args == '"') {
            char qq = *args++;
            while (*args && *args != qq && fi < sizeof(fname) - 1) fname[fi++] = *args++;
            if (*args == qq) args++;
        } else {
            while (*args && *args != ' ' && fi < sizeof(fname) - 1) fname[fi++] = *args++;
        }
        fname[fi] = '\0';
    } else {
        fname[0] = '\0';
    }

    uint32_t pos = 0;
    if (out_buf && max_out) out_buf[0] = '\0';

    /* -r o sin archivo → búsqueda global (ignora paths extra estilo GNU) */
    if (force_global) fname[0] = '\0';

    /* ---- Sin archivo y sin pipe → búsqueda global en RamFS ---- */
    if (fname[0] == '\0' && pipe_stdin_data == 0) {
        static char g_all[2048];
        int hits = vfs_grep_all(pat, ignore_case, list_only, g_all, sizeof(g_all));
        kprint(g_all);
        soma_str_put(out_buf, max_out, &pos, g_all);
        if (hits > 0) {
            kprint("("); kprint_dec((uint32_t)hits); kprint(" archivo(s) con coincidencias)\n");
        }
        return 1;
    }

    /* ---- Un archivo o pipe (comportamiento clásico) ---- */
    const char *src_text = 0;
    static char gbuf[4096];

    if (fname[0] != '\0') {
        int r = vfs_read(fname, gbuf, sizeof(gbuf) - 1);
        if (r < 0) {
            kprint("grep: archivo no encontrado: '"); kprint(fname); kprint("'\n");
            soma_str_put(out_buf, max_out, &pos, "grep: archivo no encontrado");
            return 1;
        }
        gbuf[r] = '\0';
        src_text = gbuf;
    } else {
        src_text = pipe_stdin_data;
    }

    uint32_t line_num = 1;
    int matches = 0;
    const char *p = src_text;

    while (*p) {
        const char *line_start = p;
        while (*p && *p != '\n') p++;
        uint32_t line_len = (uint32_t)(p - line_start);
        char line_tmp[256];
        uint32_t copy_len = line_len < sizeof(line_tmp) - 1 ? line_len : sizeof(line_tmp) - 1;
        for (uint32_t k = 0; k < copy_len; ++k) line_tmp[k] = line_start[k];
        line_tmp[copy_len] = '\0';

        if (find_substr_ci(line_tmp, pat, ignore_case)) {
            matches++;
            if (list_only) {
                if (fname[0]) {
                    kprint(fname); kprint("\n");
                    soma_str_put(out_buf, max_out, &pos, fname);
                    soma_str_put(out_buf, max_out, &pos, "\n");
                }
                break;
            }
            kprint_dec(line_num); kprint(": "); kprint(line_tmp); kprint("\n");
            soma_str_put_dec(out_buf, max_out, &pos, line_num);
            soma_str_put(out_buf, max_out, &pos, ": ");
            soma_str_put(out_buf, max_out, &pos, line_tmp);
            soma_str_put(out_buf, max_out, &pos, "\n");
        }

        line_num++;
        if (*p == '\n') p++;
    }

    if (matches == 0) {
        kprint("grep: sin coincidencias.\n");
        soma_str_put(out_buf, max_out, &pos, "grep: sin coincidencias.");
    }
    return 1;
}

static int cmd_echo(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char clean[512];
    uint32_t ci = 0;

    char q = 0;
    if (*args == '"' || *args == '\'') {
        q = *args++;
    }

    while (*args && ci < sizeof(clean) - 1) {
        if (q && *args == q) {
            args++;
            while (*args == ' ') args++;
            if (*args == '"' || *args == '\'') q = *args++;
            else q = 0;
            continue;
        }
        clean[ci++] = *args++;
    }
    clean[ci] = '\0';

    kprint(clean); kprint("\n");
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, clean); soma_str_putc(out_buf, max_out, &pos, '\n');
    return 1;
}

static int cmd_nano(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    run_nano(args);
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "nano cerrado.");
    return 1;
}

static int cmd_hexdump(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    do_hexdump(args, 128, out_buf, max_out);
    return 1;
}

static int cmd_checkpoint(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    if (vfs_checkpoint_save() == 0) {
        kprint("[CHECKPOINT OK]\n");
        uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Checkpoint OK");
    } else { uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Checkpoint: ERROR al guardar (el anterior sigue siendo el ultimo valido solo si no se llego a invalidar)"); }
    return 1;
}

static int cmd_rollback(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    if (vfs_checkpoint_restore() == 0) {
        kprint("[ROLLBACK OK]\n");
        uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Rollback OK");
    } else { uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Rollback: no hay checkpoint valido (ausente o corrupto); el sistema de archivos no se ha tocado"); }
    return 1;
}

static int cmd_fs_sync(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    vfs_sync();
    kprint("RamFS sincronizado.\n");
    uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Sync OK");
    return 1;
}

static int cmd_fs_format(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    vfs_format();
    kprint("RamFS formateado a fabrica.\n");
    uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Format OK");
    return 1;
}

static int cmd_sector_read(const char *args, char *out_buf, uint32_t max_out)
{
    uint64_t sec = parse_num(&args);
    char sbuf[512];
    if (agent_dispatch_mode && !soma_lba_user_zone(sec)) {          /* PARCHE 122: un agente no lee el disco entero (VFS privado, memoria, BIGFS) */
        kprint("Error: sector protegido para agentes.\n");
        uint32_t pe = 0; soma_str_put(out_buf, max_out, &pe, "sector protegido para agentes");
        return 1;
    }
    if (sysdisk_read(sec, sbuf) == 0) {
        kprint("\n[LBA "); kprint_dec((uint32_t)sec); kprint("]: ");
        for (int i=0; i<512 && sbuf[i]; i++) if (sbuf[i]>=32 && sbuf[i]<127) kputc(sbuf[i]);
        kprint("\n");
    }
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "sector leido");
    return 1;
}

static int cmd_sector_write(const char *args, char *out_buf, uint32_t max_out)
{
    uint64_t sec = parse_num(&args);
    if (!soma_lba_user_zone(sec)) {                                  /* PARCHE 122: antes dejaba escribir sobre BIGFS y el checkpoint */
        kprint("Error: sector protegido.\n");
        return 1;
    }
    while (*args == ' ') args++;
    char sbuf[512]; memset(sbuf, 0, 512); int bi = 0;
    while (*args && bi < 511) sbuf[bi++] = *args++;
    sysdisk_write(sec, sbuf);
    kprint("Sector escrito.\n");
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "sector escrito");
    return 1;
}

