/* shell_red.c: red: ping, dns, search y curl.
 * SOMA_233_SHELL_PARTIDO: NO se compila por separado. Lo incluye src/agent/shell.c en su sitio (una sola unidad de
 * compilacion: usa las funciones auxiliares y la tabla de ordenes de shell.c, y el nucleo compilado sale identico). */
static int cmd_ping(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint8_t target[4];
    if (*args) parse_ip(args, target);
    else { target[0] = net_gateway[0]; target[1] = net_gateway[1]; target[2] = net_gateway[2]; target[3] = net_gateway[3]; }
    int r = icmp_ping(target, 1, 2000);
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, (r == 1) ? "Ping exitoso" : "Ping fallido");
    return 1;
}

static int cmd_dns(const char *args, char *out_buf, uint32_t max_out)
{
    if (agent_net_denied("dns", args, out_buf, max_out)) return 1;   /* SOMA_193: un nombre inventado tambien saca datos (consulta DNS) */
    while (*args == ' ') args++;
    uint8_t ip[4];
    dns_resolve(args, ip, 3000);
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "DNS consultado");
    return 1;
}


/* PARCHE_SEARCH_V5: busqueda web multifuente conectada a 10.0.2.2:8095 */
static void search_url_encode(const char *in, char *out, uint32_t max)
{
    uint32_t o = 0;
    static const char *hex = "0123456789ABCDEF";
    while (*in && o + 4 < max) {
        unsigned char c = (unsigned char)*in++;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else if (c == ' ') {
            out[o++] = '+';
        } else {
            out[o++] = '%';
            out[o++] = hex[(c >> 4) & 0xF];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

static int cmd_search(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (!*args) {
        kprint("Uso: search <consulta>\n");
        uint32_t p = 0;
        soma_str_put(out_buf, max_out, &p, "Uso: search <consulta>");
        return 1;
    }

    /* Limpiar comillas iniciales/finales si el agente o usuario las puso */
    char clean_query[256];
    uint32_t qp = 0;
    char q = 0;
    if (*args == '\'' || *args == '"') q = *args++;
    while (*args && qp < sizeof(clean_query) - 1) {
        if (q && *args == q && *(args + 1) == '\0') break;
        clean_query[qp++] = *args++;
    }
    clean_query[qp] = '\0';
    while (qp > 0 && (clean_query[qp - 1] == ' ' || clean_query[qp - 1] == '\'' || clean_query[qp - 1] == '"')) {
        clean_query[--qp] = '\0';
    }

    char enc[256];
    search_url_encode(clean_query, enc, sizeof(enc));

    char path[300];
    uint32_t pp = 0;
    const char *pre = "/search?q=";
    while (*pre && pp + 1 < sizeof(path)) path[pp++] = *pre++;
    for (uint32_t i = 0; enc[i] && pp + 1 < sizeof(path); ++i) path[pp++] = enc[i];
    path[pp] = '\0';

    /* Host Mac en QEMU NAT = net_gateway (10.0.2.2) */
    uint8_t tip[4];
    tip[0] = net_gateway[0];
    tip[1] = net_gateway[1];
    tip[2] = net_gateway[2];
    tip[3] = net_gateway[3];
    const char *host = "10.0.2.2";
    uint16_t proxy_port = 8095;

    static char http_buf[8192];
    struct http_response resp;
    int code = http_get_host(tip, proxy_port, host, path, http_buf, sizeof(http_buf), &resp, 45000);

    uint32_t pos = 0;
    if (out_buf && max_out) out_buf[0] = '\0';

    if (code < 0) {
        kprint("search: error HTTP/red (proxy 10.0.2.2:8095 no responde)\n");
        soma_str_put(out_buf, max_out, &pos,
                "search: error de red o timeout (inicia 'python3 search_proxy.py' en el host)");
        return 1;
    }
    if (code < 200 || code >= 400) {
        kprint("search: HTTP error "); kprint_dec((uint32_t)code); kprint("\n");
        soma_str_put(out_buf, max_out, &pos, "search: error devuelto por el proxy");
        return 1;
    }

    const char *body = (resp.body && resp.body[0]) ? resp.body : http_buf;
    kprint("\n--- Resultados Web ["); kprint_dec((uint32_t)code); kprint("] ---\n");
    kprint(body);
    kprint("\n------------------------------------\n");
    if (!body[0]) {
        soma_str_put(out_buf, max_out, &pos, "search: respuesta vacia");
        return 1;
    }
    soma_str_put(out_buf, max_out, &pos, body);
    return 1;
}

static int shell_starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static int cmd_curl(const char *args, char *out_buf, uint32_t max_out)
{
    if (agent_net_denied("curl", args, out_buf, max_out)) return 1;   /* SOMA_193 */
    /* PARCHE_RED_SEGURO_V1: URL completa delegada al proxy; el kernel no necesita TLS. */
    while (*args == ' ') args++;
    const char *url_arg = args;
    char url_quote = 0;
    if (*url_arg == '\'' || *url_arg == '"') {
        url_quote = *url_arg++;
    }
    if (shell_starts_with(url_arg, "http://") || shell_starts_with(url_arg, "https://")) {
        char target_url[321];
        uint32_t url_len = 0;
        while (url_arg[url_len] && url_len < sizeof(target_url) - 1 &&
               (url_quote ? url_arg[url_len] != url_quote : url_arg[url_len] != ' ')) {
            target_url[url_len] = url_arg[url_len];
            url_len++;
        }
        if (url_arg[url_len] &&
            (url_quote ? url_arg[url_len] != url_quote : url_arg[url_len] != ' ')) {
            kprint("curl: URL demasiado larga (maximo 320 caracteres).\n");
            return 1;
        }
        target_url[url_len] = '\0';
        if (url_len == 0) {
            kprint("curl: URL vacia.\n");
            return 1;
        }

        char encoded_url[1000];
        search_url_encode(target_url, encoded_url, sizeof(encoded_url));
        char proxy_path[1024];
        uint32_t path_pos = 0;
        const char *prefix = "/fetch?url=";
        while (*prefix && path_pos + 1 < sizeof(proxy_path)) {
            proxy_path[path_pos++] = *prefix++;
        }
        for (uint32_t i = 0; encoded_url[i] && path_pos + 1 < sizeof(proxy_path); ++i) {
            proxy_path[path_pos++] = encoded_url[i];
        }
        proxy_path[path_pos] = '\0';

        static char proxy_buf[4096];
        struct http_response proxy_resp;
        int proxy_code = http_get_host(net_gateway, 8095, "10.0.2.2", proxy_path,
                                       proxy_buf, sizeof(proxy_buf), &proxy_resp, 40000);
        uint32_t out_pos = 0;
        if (out_buf && max_out) out_buf[0] = '\0';
        if (proxy_code < 0) {
            kprint("curl: el proxy HTTPS 10.0.2.2:8095 no responde.\n");
            soma_str_put(out_buf, max_out, &out_pos, "curl: error de red o timeout al consultar el proxy");
            return 1;
        }
        const char *display = (proxy_resp.body && proxy_resp.body[0]) ? proxy_resp.body : proxy_buf;
        kprint("\n--- RESPUESTA HTTP VIA PROXY [");
        kprint_dec((uint32_t)proxy_code);
        kprint("] ---\n");
        kprint(display);
        kprint("\n");
        soma_str_put(out_buf, max_out, &out_pos, display);
        return 1;
    }
    char target[64]; uint32_t ti = 0; while (*args && *args != ' ' && ti < sizeof(target) - 1) target[ti++] = *args++; target[ti] = '\0';
    while (*args == ' ') args++;
    uint16_t port = 80; if (*args >= '0' && *args <= '9') { port = 0; while (*args >= '0' && *args <= '9') port = port * 10 + (*args++ - '0'); }
    while (*args == ' ') args++;
    const char *path = (*args) ? args : "/";
    uint8_t tip[4]; const char *hhdr = 0; int is_ip = 1, dots = 0;
    for (int i = 0; target[i]; ++i) { if (target[i] == '.') dots++; else if (target[i] < '0' || target[i] > '9') is_ip = 0; }
    if (dots != 3) is_ip = 0;
    if (is_ip) parse_ip(target, tip);
    else { if (!dns_resolve(target, tip, 3000)) { kprint("DNS error\n"); return 1; } hhdr = target; }
    static char http_buf[4096]; struct http_response resp;
    int code = http_get_host(tip, port, hhdr, path, http_buf, sizeof(http_buf), &resp, 40000);
    if (code >= 0) {
        const char *disp = (resp.body && resp.body[0]) ? resp.body : http_buf;
        kprint("\n--- RESPUESTA HTTP ["); kprint_dec((uint32_t)code); kprint("] ---\n"); kprint(disp); kprint("\n");
        uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, disp);
    }
    return 1;
}

