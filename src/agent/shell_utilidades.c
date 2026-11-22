/* shell_utilidades.c: utilidades de terminal: whoami, uname, sysinfo, find, du, stat, mkdir, sort, uniq y compile.
 * SOMA_233_SHELL_PARTIDO: NO se compila por separado. Lo incluye src/agent/shell.c en su sitio (una sola unidad de
 * compilacion: usa las funciones auxiliares y la tabla de ordenes de shell.c, y el nucleo compilado sale identico). */
#include "unidades.h"      /* SOMA_267_DISCOS: mkdir y rmdir en /usuario */
/* ====================================================================
 * PARCHE_TERMINAL_V2: nuevas utilidades de terminal
 * ==================================================================== */

static uint32_t shell_strlen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void shell_copy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void shell_put_dec_pad(char *out, uint32_t max, uint32_t *p, uint64_t v, uint32_t width)
{
    uint32_t n = 0;
    uint64_t t = v;
    if (t == 0) n = 1;
    else while (t) { n++; t /= 10; }
    for (uint32_t i = n; i < width; ++i) soma_str_putc(out, max, p, ' ');
    soma_str_put_dec(out, max, p, v);
}

/* Copia el primer argumento (admite comillas) y devuelve el resto de la linea. */
static const char *shell_next_arg(const char *s, char *dst, uint32_t cap)
{
    uint32_t n = 0;
    char q = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '"' || *s == '\'') q = *s++;
    while (*s && n + 1 < cap) {
        if (q ? (*s == q) : (*s == ' ' || *s == '\t')) break;
        dst[n++] = *s++;
    }
    if (q && *s == q) s++;
    dst[n] = '\0';
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Recorre la lista "Archivos: a, b, c" que produce vfs_format_list_ext. */
static const char *shell_list_start(const char *list)
{
    const char *c = find_substr_ci(list, ": ", 0);
    return c ? c + 2 : list;
}

static const char *shell_list_next(const char *p, char *item, uint32_t cap)
{
    uint32_t n = 0;
    while (*p == ' ' || *p == ',') p++;
    while (*p && *p != ',' && n + 1 < cap) item[n++] = *p++;
    while (n && item[n - 1] == ' ') n--;
    item[n] = '\0';
    return p;
}

static void shell_out_line(char *out_buf, uint32_t max_out, uint32_t *pos, const char *line)
{
    kprint(line);
    kprint("\n");
    if (out_buf && max_out) {
        soma_str_put(out_buf, max_out, pos, line);
        soma_str_putc(out_buf, max_out, pos, '\n');
    }
}

/* ---- identidad ---- */
static int cmd_whoami(const char *args, char *out_buf, uint32_t max_out)
{
    char t[ENV_VAL_LEN + 4];
    uint32_t p = 0;
    (void)args;
    soma_str_put(t, sizeof(t), &p, env_get("USER"));
    soma_str_putc(t, sizeof(t), &p, '\n');
    shell_emit(t, out_buf, max_out);
    return 1;
}

static int cmd_hostname(const char *args, char *out_buf, uint32_t max_out)
{
    char t[ENV_VAL_LEN + 4];
    uint32_t p = 0;
    (void)args;
    soma_str_put(t, sizeof(t), &p, env_get("HOST"));
    soma_str_putc(t, sizeof(t), &p, '\n');
    shell_emit(t, out_buf, max_out);
    return 1;
}

static int cmd_uname(const char *args, char *out_buf, uint32_t max_out)
{
    char t[192];
    uint32_t p = 0;
    if (!args) args = "";
    while (*args == ' ') args++;
    soma_str_put(t, sizeof(t), &p, "SOMA");
    if (args[0] == '-' && args[1] == 'a') {
        soma_str_putc(t, sizeof(t), &p, ' ');
        soma_str_put(t, sizeof(t), &p, env_get("HOST"));
        soma_str_put(t, sizeof(t), &p, " 0.2 ");
        soma_str_put(t, sizeof(t), &p, MYOS_ARCH_NAME);
        soma_str_put(t, sizeof(t), &p, " bare-metal Ring0 multi-agente");
    }
    soma_str_putc(t, sizeof(t), &p, '\n');
    shell_emit(t, out_buf, max_out);
    return 1;
}

/* ---- resumen global del sistema ---- */
static void sys_put_ip(char *o, uint32_t max, uint32_t *p, const uint8_t *ip)
{
    for (int i = 0; i < 4; ++i) {
        soma_str_put_dec(o, max, p, ip[i]);
        if (i < 3) soma_str_putc(o, max, p, '.');
    }
}

static int cmd_sysinfo(const char *args, char *out_buf, uint32_t max_out)
{
    char rep[1536];
    uint32_t p = 0;
    uint32_t files_u = 0, bytes_u = 0, dirty_c = 0, threads = 0;
    struct ramstat_info ri;
    uint64_t ms = timer_get_uptime_ms();
    uint32_t s = (uint32_t)(ms / 1000), m = s / 60, h = m / 60;
    (void)args;
    s %= 60; m %= 60;

    ramstat_get(&ri);
    vfs_get_stats(&files_u, &bytes_u, &dirty_c);
    {
        char *tt = (char *)kmalloc(2048);
        if (tt) {
            thread_format_table(tt, 2048);
            for (uint32_t i = 0; tt[i]; ++i) if (tt[i] == '\n') threads++;
            if (threads) threads--;
            kfree(tt);
        }
    }

    rep[0] = '\0';
    soma_str_put(rep, sizeof(rep), &p, "=== ESTADO DEL SISTEMA ===\nSOMA 0.3 pre-beta | arch ");
    soma_str_put(rep, sizeof(rep), &p, MYOS_ARCH_NAME);
    soma_str_put(rep, sizeof(rep), &p, " | host ");
    soma_str_put(rep, sizeof(rep), &p, env_get("HOST"));
    soma_str_put(rep, sizeof(rep), &p, " | usuario ");
    soma_str_put(rep, sizeof(rep), &p, env_get("USER"));
    soma_str_put(rep, sizeof(rep), &p, "\nUptime    : ");
    if (h) { soma_str_put_dec(rep, sizeof(rep), &p, h); soma_str_put(rep, sizeof(rep), &p, "h "); }
    soma_str_put_dec(rep, sizeof(rep), &p, m); soma_str_put(rep, sizeof(rep), &p, "m ");
    soma_str_put_dec(rep, sizeof(rep), &p, s); soma_str_put(rep, sizeof(rep), &p, "s\nRAM       : ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.footprint);
    soma_str_put(rep, sizeof(rep), &p, " / ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.total);
    soma_str_put(rep, sizeof(rep), &p, " (");
    ram_put_pct10(rep, sizeof(rep), &p, ri.pct10);
    soma_str_put(rep, sizeof(rep), &p, ") | libre ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.free_b);
    soma_str_put(rep, sizeof(rep), &p, "\nAlmacen.  : ");
    soma_str_put_dec(rep, sizeof(rep), &p, files_u);
    soma_str_putc(rep, sizeof(rep), &p, '/');
    soma_str_put_dec(rep, sizeof(rep), &p, FS_MAX_FILES);
    soma_str_put(rep, sizeof(rep), &p, " inodos | ");
    soma_str_put_dec(rep, sizeof(rep), &p, bytes_u / 1024);
    soma_str_put(rep, sizeof(rep), &p, " KiB usados | pendientes de sync: ");
    soma_str_put_dec(rep, sizeof(rep), &p, dirty_c);
    soma_str_put(rep, sizeof(rep), &p, "\nHilos     : ");
    soma_str_put_dec(rep, sizeof(rep), &p, threads);
    soma_str_put(rep, sizeof(rep), &p, " activos\nRed       : IP ");
    sys_put_ip(rep, sizeof(rep), &p, net_ip);
    soma_str_put(rep, sizeof(rep), &p, " | GW ");
    sys_put_ip(rep, sizeof(rep), &p, net_gateway);
    soma_str_put(rep, sizeof(rep), &p, "\n(detalle: ram -v | df | ps | status | pci)\n");
    shell_emit(rep, out_buf, max_out);
    return 1;
}

/* ---- find / du / stat ---- */
static int cmd_find(const char *args, char *out_buf, uint32_t max_out)
{
    char term[48], root[48], tok[48], item[FS_NAME_MAX + 4];
    int show_all = 0;
    uint32_t hits = 0, p = 0;
    term[0] = root[0] = '\0';
    if (!args) args = "";
    while (*args) {
        args = shell_next_arg(args, tok, sizeof(tok));
        if (!tok[0]) { if (!*args) break; continue; }
        if (tok[0] == '-' && tok[1] == 'a' && tok[2] == '\0') show_all = 1;
        else if (!term[0]) shell_copy(term, tok, sizeof(term));
        else if (!root[0]) shell_copy(root, tok, sizeof(root));
    }
    if (!term[0]) {
        shell_emit("Uso: find <texto> [ruta] [-a]   (-a incluye ocultos y de sistema)\n", out_buf, max_out);
        return 1;
    }
    char *list = (char *)kmalloc(4096);
    char *rep  = (char *)kmalloc(4096);
    if (!list || !rep) {
        if (list) kfree(list);
        if (rep) kfree(rep);
        kprint("find: sin memoria.\n");
        return 1;
    }
    rep[0] = '\0';
    vfs_format_list_ext(show_all, root[0] ? root : 0, list, 4096);
    const char *q = shell_list_start(list);
    while (*q) {
        q = shell_list_next(q, item, sizeof(item));
        if (item[0] && find_substr_ci(item, term, 1)) {
            soma_str_put(rep, 4096, &p, item);
            soma_str_putc(rep, 4096, &p, '\n');
            hits++;
        }
    }
    if (!hits) soma_str_put(rep, 4096, &p, "find: sin coincidencias.\n");
    else { soma_str_put_dec(rep, 4096, &p, hits); soma_str_put(rep, 4096, &p, " coincidencia(s)\n"); }
    shell_emit(rep, out_buf, max_out);
    kfree(list);
    kfree(rep);
    return 1;
}

static int cmd_du(const char *args, char *out_buf, uint32_t max_out)
{
    char root[48], tok[48], item[FS_NAME_MAX + 4];
    int show_all = 0;
    uint32_t total = 0, count = 0, used = 0, bytes_u = 0, dirty = 0, p = 0;
    root[0] = '\0';
    if (!args) args = "";
    while (*args) {
        args = shell_next_arg(args, tok, sizeof(tok));
        if (!tok[0]) { if (!*args) break; continue; }
        if (tok[0] == '-' && tok[1] == 'a' && tok[2] == '\0') show_all = 1;
        else if (!root[0]) shell_copy(root, tok, sizeof(root));
    }
    char *list = (char *)kmalloc(4096);
    char *data = (char *)kmalloc(FS_DATA_MAX);
    char *rep  = (char *)kmalloc(6144);
    if (!list || !data || !rep) {
        if (list) kfree(list);
        if (data) kfree(data);
        if (rep) kfree(rep);
        kprint("du: sin memoria.\n");
        return 1;
    }
    rep[0] = '\0';
    vfs_format_list_ext(show_all, root[0] ? root : 0, list, 4096);
    const char *q = shell_list_start(list);
    while (*q) {
        q = shell_list_next(q, item, sizeof(item));
        if (!item[0]) continue;
        int r = vfs_size(item);
        uint32_t sz = (r > 0) ? (uint32_t)r : 0;
        shell_put_dec_pad(rep, 6144, &p, sz, 7);
        soma_str_put(rep, 6144, &p, "  ");
        soma_str_put(rep, 6144, &p, item);
        soma_str_putc(rep, 6144, &p, '\n');
        total += sz;
        count++;
    }
    vfs_get_stats(&used, &bytes_u, &dirty);
    soma_str_put(rep, 6144, &p, "Total: ");
    soma_str_put_dec(rep, 6144, &p, count);
    soma_str_put(rep, 6144, &p, " archivos, ");
    soma_str_put_dec(rep, 6144, &p, total);
    soma_str_put(rep, 6144, &p, " bytes (");
    soma_str_put_dec(rep, 6144, &p, total / 1024);
    soma_str_put(rep, 6144, &p, " KiB) | inodos libres: ");
    soma_str_put_dec(rep, 6144, &p, (used <= FS_MAX_FILES) ? (FS_MAX_FILES - used) : 0);
    soma_str_putc(rep, 6144, &p, '\n');
    shell_emit(rep, out_buf, max_out);
    kfree(list);
    kfree(data);
    kfree(rep);
    return 1;
}

static int cmd_stat(const char *args, char *out_buf, uint32_t max_out)
{
    char path[FS_NAME_MAX + 8], rep[512];
    uint32_t p = 0, lines = 0;
    int binary = 0;
    if (!args) args = "";
    shell_next_arg(args, path, sizeof(path));
    if (!path[0]) {
        shell_emit("Uso: stat <archivo>\n", out_buf, max_out);
        return 1;
    }
    uint32_t dl = 0; char *data = vfs_read_alloc(path, &dl);   /* PARCHE 087 */
    int r = data ? (int)dl : -1;
    if (r < 0) {
        soma_str_put(rep, sizeof(rep), &p, "stat: no encontrado: ");
        soma_str_put(rep, sizeof(rep), &p, path);
        soma_str_putc(rep, sizeof(rep), &p, '\n');
        shell_emit(rep, out_buf, max_out);
        kfree(data);
        return 1;
    }
    for (int i = 0; i < r; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c == '\n') lines++;
        else if (c < 9 || (c > 13 && c < 32)) binary = 1;
    }
    if (r > 0 && data[r - 1] != '\n') lines++;
    rep[0] = '\0';
    soma_str_put(rep, sizeof(rep), &p, "Archivo     : ");
    soma_str_put(rep, sizeof(rep), &p, path);
    soma_str_put(rep, sizeof(rep), &p, "\nTamano      : ");
    soma_str_put_dec(rep, sizeof(rep), &p, (uint64_t)r);
    soma_str_put(rep, sizeof(rep), &p, " bytes\nTipo        : ");
    soma_str_put(rep, sizeof(rep), &p, r == 0 ? "vacio" : (binary ? "binario" : "texto"));
    if (r > 0 && !binary) {
        soma_str_put(rep, sizeof(rep), &p, "\nLineas      : ");
        soma_str_put_dec(rep, sizeof(rep), &p, lines);
    }
    soma_str_put(rep, sizeof(rep), &p, "\nVisibilidad : ");
    soma_str_put(rep, sizeof(rep), &p, vfs_is_hidden(path) ? "oculto" : "visible");
    soma_str_put(rep, sizeof(rep), &p, "\nAlmacenaje  : ");
    if (find_substr_ci(path, "/proc/", 0) == path || find_substr_ci(path, "/dev/", 0) == path)
        soma_str_put(rep, sizeof(rep), &p, "pseudo-archivo del kernel");
    else if (find_substr_ci(path, "/tmp/", 0) || find_substr_ci(path, "/sys/", 0))
        soma_str_put(rep, sizeof(rep), &p, "volatil (solo RAM)");
    else
        soma_str_put(rep, sizeof(rep), &p, "persistente (virtio-blk)");
    soma_str_putc(rep, sizeof(rep), &p, '\n');
    shell_emit(rep, out_buf, max_out);
    kfree(data);
    return 1;
}

/* ---- directorios (el VFS los representa con un marcador oculto <dir>/.dir) ---- */
static int shell_dir_path(const char *args, char *path, uint32_t cap)
{
    char tok[FS_NAME_MAX + 8];
    uint32_t p = 0;
    shell_next_arg(args ? args : "", tok, sizeof(tok));
    if (!tok[0]) return 0;
    if (tok[0] != '/') {
        const char *pre = "/workspace/";
        while (*pre && p + 1 < cap) path[p++] = *pre++;
    }
    for (uint32_t i = 0; tok[i] && p + 1 < cap; ++i) path[p++] = tok[i];
    while (p > 1 && path[p - 1] == '/') p--;
    path[p] = '\0';
    return (int)p;
}

static int shell_user_space(const char *path)
{
    static const char *const roots[] = { "/workspace", "/mis_archivos", 0 };
    for (int r = 0; roots[r]; ++r) {
        uint32_t i = 0;
        while (roots[r][i] && path[i] == roots[r][i]) ++i;
        if (!roots[r][i] && (path[i] == '\0' || path[i] == '/')) return 1;
    }
    return 0;
}

static int shell_dir_marker(const char *path, int n, char *marker, uint32_t cap)
{
    if (n <= 1 || (uint32_t)n + 6 > cap || (uint32_t)n + 6 > FS_NAME_MAX) return 0;
    for (int i = 0; i < n; ++i) marker[i] = path[i];
    marker[n] = '/'; marker[n + 1] = '.'; marker[n + 2] = 'd';
    marker[n + 3] = 'i'; marker[n + 4] = 'r'; marker[n + 5] = '\0';
    return 1;
}

/* SOMA_267_DISCOS: mkdir / rmdir dentro de una unidad (/usuario; /modelos es de solo lectura). 1 = era de una unidad (ya respondido). */
static int shell_unid_dir(const char *args, int crear, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (unid_ruta(args, 0) < 0) return 0;
    char up[256], rep[320]; uint32_t k = 0, p = 0;
    while (args[k] && args[k] != ' ' && k + 1 < sizeof up) { up[k] = args[k]; k++; }
    up[k] = 0;
    while (k > 1 && up[k - 1] == '/') up[--k] = 0;
    int e;
    if (crear) e = unid_mkdir(up, 0);
    else { uint64_t t; int d = 0; e = unid_tam(up, &t, &d); if (!e && !d) e = EXFAT_E_NOTDIR; if (!e) e = unid_borrar(up, 0); }
    rep[0] = 0;
    soma_str_put(rep, sizeof rep, &p, e ? (crear ? "mkdir: " : "rmdir: ") : (crear ? "Directorio creado: " : "Directorio eliminado: "));
    if (e) { soma_str_put(rep, sizeof rep, &p, e == EXFAT_E_RO ? "MODELOS es de solo lectura" : exfat_err_str(e)); soma_str_put(rep, sizeof rep, &p, ": "); }
    soma_str_put(rep, sizeof rep, &p, up);
    soma_str_putc(rep, sizeof rep, &p, '\n');
    shell_emit(rep, out_buf, max_out);
    return 1;
}

static int cmd_mkdir(const char *args, char *out_buf, uint32_t max_out)
{
    if (shell_unid_dir(args, 1, out_buf, max_out)) return 1;          /* SOMA_267_DISCOS */
    char path[FS_NAME_MAX + 8], marker[FS_NAME_MAX + 16], probe[2], rep[160];
    uint32_t p = 0;
    int n = shell_dir_path(args, path, sizeof(path));
    if (n <= 1) {
        shell_emit("Uso: mkdir <ruta>   (relativa a /workspace si no empieza por /)\n", out_buf, max_out);
        return 1;
    }
    if (agent_dispatch_mode && !shell_user_space(path)) {
        shell_emit("mkdir: los agentes solo pueden crear directorios en /workspace o /mis_archivos.\n", out_buf, max_out);
        return 1;
    }
    if (!shell_dir_marker(path, n, marker, sizeof(marker))) {
        shell_emit("mkdir: ruta demasiado larga para el VFS (maximo 42 caracteres).\n", out_buf, max_out);
        return 1;
    }
    rep[0] = '\0';
    if (vfs_read(marker, probe, sizeof(probe)) >= 0) {
        soma_str_put(rep, sizeof(rep), &p, "mkdir: ya existe: ");
    } else if (vfs_write(marker, "", 0) < 0) {
        soma_str_put(rep, sizeof(rep), &p, "mkdir: no se pudo crear (VFS lleno o ruta protegida): ");
    } else {
        soma_str_put(rep, sizeof(rep), &p, "Directorio creado: ");
    }
    soma_str_put(rep, sizeof(rep), &p, path);
    soma_str_putc(rep, sizeof(rep), &p, '\n');
    shell_emit(rep, out_buf, max_out);
    return 1;
}

static int cmd_rmdir(const char *args, char *out_buf, uint32_t max_out)
{
    if (shell_unid_dir(args, 0, out_buf, max_out)) return 1;          /* SOMA_267_DISCOS */
    char path[FS_NAME_MAX + 8], marker[FS_NAME_MAX + 16], prefix[FS_NAME_MAX + 16];
    char item[FS_NAME_MAX + 4], probe[2], rep[160];
    uint32_t p = 0;
    int n = shell_dir_path(args, path, sizeof(path));
    if (n <= 1) {
        shell_emit("Uso: rmdir <ruta>   (solo directorios vacios)\n", out_buf, max_out);
        return 1;
    }
    if (agent_dispatch_mode && !shell_user_space(path)) {
        shell_emit("rmdir: los agentes solo pueden borrar directorios en /workspace o /mis_archivos.\n", out_buf, max_out);
        return 1;
    }
    if (!shell_dir_marker(path, n, marker, sizeof(marker))) {
        shell_emit("rmdir: ruta no valida.\n", out_buf, max_out);
        return 1;
    }
    rep[0] = '\0';
    if (vfs_read(marker, probe, sizeof(probe)) < 0) {
        soma_str_put(rep, sizeof(rep), &p, "rmdir: no existe: ");
        soma_str_put(rep, sizeof(rep), &p, path);
        soma_str_putc(rep, sizeof(rep), &p, '\n');
        shell_emit(rep, out_buf, max_out);
        return 1;
    }
    shell_copy(prefix, marker, sizeof(prefix));
    prefix[n + 1] = '\0';                      /* "<ruta>/" */
    char *list = (char *)kmalloc(2048);
    if (!list) { kprint("rmdir: sin memoria.\n"); return 1; }
    vfs_format_list_ext(1, prefix, list, 2048);
    const char *q = shell_list_start(list);
    int nonempty = 0;
    while (*q) {
        q = shell_list_next(q, item, sizeof(item));
        if (item[0] && !str_eq(item, marker)) { nonempty = 1; break; }
    }
    kfree(list);
    if (nonempty) {
        soma_str_put(rep, sizeof(rep), &p, "rmdir: el directorio no esta vacio: ");
    } else if (vfs_delete(marker) == 0) {
        soma_str_put(rep, sizeof(rep), &p, "Directorio eliminado: ");
    } else {
        soma_str_put(rep, sizeof(rep), &p, "rmdir: no se pudo eliminar: ");
    }
    soma_str_put(rep, sizeof(rep), &p, path);
    soma_str_putc(rep, sizeof(rep), &p, '\n');
    shell_emit(rep, out_buf, max_out);
    return 1;
}

/* ---- sort / uniq (archivo o tuberia) ---- */
#define SHELL_MAX_LINES 512

/* Devuelve una copia en heap del texto de entrada (archivo o stdin de tuberia). */
static char *shell_read_input(const char *fn, const char *who)
{
    char *buf;
    if (fn && fn[0]) {
        buf = vfs_read_alloc(fn, 0);                        /* PARCHE 087 */
        if (!buf) { kprint(who); kprint(": no encontrado: "); kprint(fn); kprint("\n"); return 0; }
        return buf;
    }
    if (pipe_stdin_data) {
        uint32_t n = shell_strlen(pipe_stdin_data);
        buf = (char *)kmalloc(n + 1);
        if (!buf) { kprint(who); kprint(": sin memoria.\n"); return 0; }
        memcpy(buf, pipe_stdin_data, n + 1);
        return buf;
    }
    return 0;
}

static uint32_t shell_split_lines(char *buf, char **lines, uint32_t max)
{
    uint32_t n = 0;
    char *p = buf;
    while (*p && n < max) {
        lines[n++] = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = '\0'; p++; }
    }
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t l = shell_strlen(lines[i]);
        if (l && lines[i][l - 1] == '\r') lines[i][l - 1] = '\0';
    }
    return n;
}

static int32_t shell_leading_int(const char *s)
{
    int neg = 0;
    int32_t v = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9' && v < 100000000) { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

static int shell_line_cmp(const char *a, const char *b, int numeric)
{
    if (numeric) {
        int32_t x = shell_leading_int(a), y = shell_leading_int(b);
        if (x != y) return x < y ? -1 : 1;
    }
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int cmd_sort(const char *args, char *out_buf, uint32_t max_out)
{
    char fn[FS_NAME_MAX + 8];
    char *lines[SHELL_MAX_LINES];
    int rev = 0, num = 0;
    uint32_t pos = 0;
    if (!args) args = "";
    while (*args == ' ') args++;
    while (*args == '-') {
        args++;
        while (*args && *args != ' ') {
            if (*args == 'r') rev = 1;
            else if (*args == 'n') num = 1;
            args++;
        }
        while (*args == ' ') args++;
    }
    shell_next_arg(args, fn, sizeof(fn));
    char *buf = shell_read_input(fn, "sort");
    if (!buf) {
        if (!fn[0] && !pipe_stdin_data) shell_emit("Uso: sort [-r] [-n] [archivo]   o   <comando> | sort\n", out_buf, max_out);
        return 1;
    }
    uint32_t n = shell_split_lines(buf, lines, SHELL_MAX_LINES);
    for (uint32_t i = 1; i < n; ++i) {
        char *key = lines[i];
        uint32_t j = i;
        while (j > 0) {
            int c = shell_line_cmp(lines[j - 1], key, num);
            if (rev ? (c < 0) : (c > 0)) { lines[j] = lines[j - 1]; j--; }
            else break;
        }
        lines[j] = key;
    }
    if (out_buf && max_out) out_buf[0] = '\0';
    for (uint32_t i = 0; i < n; ++i) shell_out_line(out_buf, max_out, &pos, lines[i]);
    kfree(buf);
    return 1;
}

static int cmd_uniq(const char *args, char *out_buf, uint32_t max_out)
{
    char fn[FS_NAME_MAX + 8];
    char *lines[SHELL_MAX_LINES];
    char tmp[300];
    int count = 0;
    uint32_t pos = 0;
    if (!args) args = "";
    while (*args == ' ') args++;
    if (args[0] == '-' && args[1] == 'c' && (args[2] == ' ' || args[2] == '\0')) {
        count = 1;
        args += 2;
    }
    shell_next_arg(args, fn, sizeof(fn));
    char *buf = shell_read_input(fn, "uniq");
    if (!buf) {
        if (!fn[0] && !pipe_stdin_data) shell_emit("Uso: uniq [-c] [archivo]   o   <comando> | uniq\n", out_buf, max_out);
        return 1;
    }
    uint32_t n = shell_split_lines(buf, lines, SHELL_MAX_LINES);
    if (out_buf && max_out) out_buf[0] = '\0';
    for (uint32_t i = 0; i < n; ) {
        uint32_t j = i + 1, tp = 0;
        while (j < n && str_eq(lines[i], lines[j])) j++;
        tmp[0] = '\0';
        if (count) {
            shell_put_dec_pad(tmp, sizeof(tmp), &tp, j - i, 4);
            soma_str_putc(tmp, sizeof(tmp), &tp, ' ');
        }
        soma_str_put(tmp, sizeof(tmp), &tp, lines[i]);
        shell_out_line(out_buf, max_out, &pos, tmp);
        i = j;
    }
    kfree(buf);
    return 1;
}

/* SOMA_CODER_AUTONOMOUS_PATCH: Sensor sincrono de compilacion */
static int cmd_compile(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: compile <archivo.soma>\n");
        if (out_buf && max_out) {
            uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Uso: compile <archivo.soma>");
        }
        return 1;
    }

    char path[96]; uint32_t pp = 0; char q = 0;
    if (*args == '\'' || *args == '"') q = *args++;
    while (*args && pp < sizeof(path) - 1) {
        if (q && *args == q) break;
        if (!q && *args == ' ') break;
        path[pp++] = *args++;
    }
    path[pp] = '\0';

    char *file_buf = (char *)kmalloc(16384);
    if (!file_buf) {
        kprint("compile: sin memoria.\n");
        if (out_buf && max_out) {
            uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "compile: sin memoria");
        }
        return 1;
    }

    int r = vfs_read(path, file_buf, 16383);
    if (r <= 0 && path[0] != '/') {
        char alt[112]; alt[0] = '/'; uint32_t k = 0;
        while (path[k] && k < sizeof(alt) - 2) { alt[k + 1] = path[k]; k++; }
        alt[k + 1] = '\0';
        r = vfs_read(alt, file_buf, 16383);
        if (r > 0) {
            for (uint32_t i = 0; alt[i] && i < sizeof(path) - 1; ++i) path[i] = alt[i];
            path[k + 1] = '\0';
        }
    }

    if (r <= 0) {
        kprint("compile: archivo no encontrado: '"); kprint(path); kprint("'\n");
        if (out_buf && max_out) {
            uint32_t p = 0;
            soma_str_put(out_buf, max_out, &p, "[COMPILE ERROR] Archivo no encontrado: ");
            soma_str_put(out_buf, max_out, &p, path);
        }
        kfree(file_buf);
        return 1;
    }

    file_buf[r] = '\0';

    if (r < 10) {
        kprint("compile: archivo vacio o contiene solo placeholder (< 10 bytes).\n");
        if (out_buf && max_out) {
            uint32_t p = 0;
            soma_str_put(out_buf, max_out, &p, "[COMPILER ERROR]: Archivo vacio o incompleto. Escribe el codigo real sin '...'.");
        }
        kfree(file_buf);
        return 1;
    }

    static uint8_t test_bc[32768]; uint32_t test_bc_len = 0;
    int err = lang_compile(file_buf, (uint32_t)r, test_bc, sizeof(test_bc), &test_bc_len);

    uint32_t p = 0;
    if (out_buf && max_out) out_buf[0] = '\0';

    if (err == 0) {
        kprint("[COMPILE OK] '"); kprint(path); kprint("': 0 errores.\n");
        if (out_buf && max_out) {
            soma_str_put(out_buf, max_out, &p, "[COMPILE OK] Compilacion exitosa: 0 errores (bytecode: ");
            soma_str_put_dec(out_buf, max_out, &p, test_bc_len);
            soma_str_put(out_buf, max_out, &p, " bytes). Codigo valido y listo para 'app'.");
        }
    } else {
        const char *last_err = lang_get_last_error_msg();
        uint32_t err_line = lang_get_last_error_line();
        const char *err_tok = lang_get_last_error_token();

        kprint("[COMPILER ERROR L"); kprint_dec(err_line); kprint("]: ");
        kprint(last_err); kprint(" (cerca de: '"); kprint(err_tok); kprint("')\n");

        if (out_buf && max_out) {
            soma_str_put(out_buf, max_out, &p, "[COMPILER ERROR L");
            soma_str_put_dec(out_buf, max_out, &p, err_line);
            soma_str_put(out_buf, max_out, &p, "]: ");
            soma_str_put(out_buf, max_out, &p, last_err);
            soma_str_put(out_buf, max_out, &p, " (cerca de: '");
            soma_str_put(out_buf, max_out, &p, err_tok);
            soma_str_put(out_buf, max_out, &p, "'). Corrige este error con 'write'.");
        }
    }

    kfree(file_buf);
    return 1;
}

/* SOMA_LANG_LINT_CMD: Sensor estatico de codigo para shell y Coder */
int cmd_lang_lint(const char *args, char *out_buf, uint32_t max_out)
{
    char path[96];
    char *buf;
    uint32_t pp = 0, p = 0;
    char q = 0;
    int r;
    struct soma_lint_report rep;

    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: lint <archivo.soma>\n");
        if (out_buf && max_out) {
            soma_str_put(out_buf, max_out, &p, "Uso: lint <archivo.soma>");
        }
        return 1;
    }
    if (*args == '\'' || *args == '"') q = *args++;
    while (*args && pp < sizeof(path) - 1) {
        if (q && *args == q) break;
        if (!q && *args == ' ') break;
        path[pp++] = *args++;
    }
    path[pp] = '\0';

    buf = (char *)kmalloc(16384);
    if (!buf) {
        kprint("lint: sin memoria.\n");
        return 1;
    }
    r = vfs_read(path, buf, 16383);
    if (r <= 0 && path[0] != '/') {
        char alt[112];
        uint32_t k = 0;
        alt[0] = '/';
        while (path[k] && k < sizeof(alt) - 2) { alt[k + 1] = path[k]; k++; }
        alt[k + 1] = '\0';
        r = vfs_read(alt, buf, 16383);
    }
    if (r <= 0) {
        kprint("lint: archivo no encontrado: '"); kprint(path); kprint("'\n");
        if (out_buf && max_out) {
            soma_str_put(out_buf, max_out, &p, "lint: archivo no encontrado");
        }
        kfree(buf);
        return 1;
    }
    buf[r] = '\0';

    lang_lint(buf, (uint32_t)r, &rep);

    kprint("\n=== SOMA-LANG LINT: "); kprint(path); kprint(" ===\n");
    if (out_buf && max_out) out_buf[0] = '\0';

    if (rep.count == 0) {
        kprint("  0 errores, 0 advertencias. Codigo 100% limpio.\n\n");
        if (out_buf && max_out) soma_str_put(out_buf, max_out, &p, "[LINT OK] 0 errores, 0 advertencias.");
    } else {
        uint32_t i;
        for (i = 0; i < rep.count; ++i) {
            kprint("  "); kprint(rep.msg[i]); kprint("\n");
            if (out_buf && max_out) {
                soma_str_put(out_buf, max_out, &p, rep.msg[i]);
                soma_str_put(out_buf, max_out, &p, "\n");
            }
        }
        kprint("\n");
    }
    kfree(buf);
    return 1;
}

