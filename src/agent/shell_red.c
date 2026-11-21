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


/* PARCHE_SEARCH_V5: busqueda web multifuente (SOMA_251_RUTAS: Wikipedia y DuckDuckGo por HTTPS; el proxy 10.0.2.2:8095 solo en QEMU) */
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

    /* SOMA_251_RUTAS: busqueda de verdad. Antes SOLO el proxy del anfitrion de QEMU (puerto 8095 de la puerta de enlace): en el
     * portatil la puerta de enlace es el router y cada busqueda esperaba hasta 30-45 s para fallar. Ahora:
     *   1) en QEMU (puerta de enlace 10.0.2.2), el proxy si esta (3 s como mucho);
     *   2) Wikipedia en espanol (API) y DuckDuckGo ligero, por HTTPS, 8 s como mucho cada una;
     *   3) sin red, se dice al momento. */
    uint32_t pos = 0;
    if (out_buf && max_out) out_buf[0] = '\0';
    if (!netif_active() || (!net_ip[0] && !net_ip[1] && !net_ip[2] && !net_ip[3])) {
        kprint("search: sin conexion de red\n");
        soma_str_put(out_buf, max_out, &pos, "search: no hay conexion a internet ahora mismo (responde con lo que sabes)");
        return 1;
    }
    static char res[6144]; uint32_t rq_ = 0; res[0] = 0; int hits = 0;
    if (net_gateway[0] == 10 && net_gateway[1] == 0 && net_gateway[2] == 2 && net_gateway[3] == 2) {
        static char http_buf[8192];
        struct http_response resp;
        int code = http_get_host(net_gateway, 8095, "10.0.2.2", path, http_buf, sizeof(http_buf), &resp, 3000);
        if (code >= 200 && code < 400) {
            const char *body = (resp.body && resp.body[0]) ? resp.body : http_buf;
            if (body[0]) { soma_str_put(res, sizeof res, &rq_, body); hits = 1; }
        }
    }
    if (!hits) {
        static struct wn_resp rs; struct wn_req rq; char url[400]; uint32_t u;
        /* Wikipedia */
        memset(&rq, 0, sizeof rq); u = 0; url[0] = 0;
        soma_str_put(url, sizeof url, &u, "https://es.wikipedia.org/w/api.php?action=query&list=search&format=json&utf8=1&srlimit=5&srsearch=");
        soma_str_put(url, sizeof url, &u, enc);
        rq.url = url; rq.accept = "application/json"; rq.timeout_ms = 8000; rq.max_body = 65536; rq.direct = 1;
        if (wn_fetch(&rq, &rs) == 0 && rs.status == 200 && rs.body) {
            static char w[3072]; int k = bw_parse_wikipedia(rs.body, "es", w, sizeof w, 5);
            if (k > 0) { soma_str_put(res, sizeof res, &rq_, "Wikipedia:\n"); soma_str_put(res, sizeof res, &rq_, w); hits += k; }
        }
        wn_free(&rs);
        /* DuckDuckGo (version ligera, sin JavaScript) */
        memset(&rq, 0, sizeof rq); u = 0; url[0] = 0;
        soma_str_put(url, sizeof url, &u, "https://lite.duckduckgo.com/lite/?kl=es-es&q=");
        soma_str_put(url, sizeof url, &u, enc);
        rq.url = url; rq.accept = "text/html"; rq.timeout_ms = 8000; rq.max_body = 262144; rq.direct = 1;
        if (wn_fetch(&rq, &rs) == 0 && rs.status == 200 && rs.body) {
            static char d[3072]; int k = bw_parse_ddg_lite(rs.body, d, sizeof d, 6);
            if (k > 0) { soma_str_put(res, sizeof res, &rq_, "Web:\n"); soma_str_put(res, sizeof res, &rq_, d); hits += k; }
        }
        wn_free(&rs);
    }
    if (!hits) {
        kprint("search: sin resultados (o sin acceso a internet)\n");
        soma_str_put(out_buf, max_out, &pos, "search: sin resultados o sin acceso a internet (responde con lo que sabes)");
        return 1;
    }
    kprint("\n--- Resultados Web ---\n"); kprint(res); kprint("\n------------------------------------\n");
    soma_str_put(out_buf, max_out, &pos, res);
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
