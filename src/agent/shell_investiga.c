/* SOMA_288_INVESTIGA: investigacion en internet de verdad. NO se compila por separado: lo incluye src/agent/shell.c (antes que
 * shell_red.c, que usa sw_lee para dar a 'search' el texto de la primera pagina).
 *
 *   investiga <pregunta>   1. el LLM propone de 2 a 4 busquedas (si no contesta, la pregunta tal cual);
 *                          2. cada una va a Wikipedia (API) y a DuckDuckGo ligero;
 *                          3. se leen hasta 5 paginas distintas (como mucho dos de un mismo sitio, sin redes sociales ni anuncios),
 *                             con su texto limpio (web_lee.c: sin menus, scripts ni pies);
 *                          4. el LLM escribe un informe en Markdown que responde a la pregunta y cita cada dato con [n], con la
 *                             lista de fuentes al final. Sin LLM, se devuelven los resultados y los extractos.
 * El agente la usa sola cuando la peticion es de investigar o informarse (ar_investiga), y el modelo la tiene como herramienta.
 * Los pasos se ven en la barra de fases del orbe y en el chat (gui_fase_orden). */
#include "web_lee.h"
#include "web_aislado.h"   /* SOMA_312_WEB_AISLADA: el HTML y el JSON de Internet se leen en ring 3 */
#include "agent_ruta.h"
#include "inv_tema.h"  /* SOMA_336_INVESTIGA_TEMAS: fuentes e informe segun el tema */

void gui_fase(const char *que);
void gui_fase_orden(const char *linea);

static uint64_t sw_ms(void) { return timer_get_uptime_ms(); }
static int sw_pre(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static uint32_t sw_len(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

/* texto limpio de una pagina (0 = no se pudo) */
/* SOMA_316_RED_AISLADA: las paginas y las busquedas se descargan con netsvc (ring 3: HTTP y TLS fuera del nucleo, sin las cookies
 * del navegador). */
static uint32_t sw_lee(const char *url0, char *out, uint32_t cap, char *tit, uint32_t tcap, uint32_t timeout_ms)
{
    static struct wn_resp rs; struct wn_req rq;
    memset(&rq, 0, sizeof rq); memset(&rs, 0, sizeof rs);
    if (cap) out[0] = 0;
    if (tit && tcap) tit[0] = 0;
    /* la URL tal como se pide: bytes no ASCII y espacios en %XX; de Wikipedia, solo el articulo (action=render: sin menus, mucho menor) */
    static char url[700]; uint32_t u = 0; const char *w = 0;
    for (const char *c = url0; *c; c++) if (c[0] == '.' && sw_pre(c, ".wikipedia.org/wiki/")) { w = c + 20; break; }
    const char *src = url0;
    if (w) {
        for (const char *c = url0; c < w - 6 && u + 1 < sizeof url; c++) url[u++] = *c;
        url[u] = 0; soma_str_put(url, sizeof url, &u, "/w/index.php?action=render&title="); src = w;
        if (tit && tcap) { uint32_t z = 0; for (const char *c = w; *c && z + 1 < tcap; c++) tit[z++] = *c == '_' ? ' ' : *c; tit[z] = 0; }   /* render no trae <title> */
    }
    for (const char *c = src; *c && u + 4 < sizeof url; c++) {
        unsigned char b = (unsigned char)*c;
        if (b > 0x7E || b == ' ' || (w && (b == '&' || b == '?'))) { const char *hx = "0123456789ABCDEF"; url[u++] = '%'; url[u++] = hx[b >> 4]; url[u++] = hx[b & 15]; }
        else url[u++] = (char)b;
    }
    url[u] = 0;
    rq.url = url; rq.accept = "text/html,application/xhtml+xml"; rq.timeout_ms = timeout_ms; rq.max_body = 1536u * 1024u; rq.direct = 1;
    uint32_t n = 0;
    if (wn_fetch_isolated(&rq, &rs) == 0 && rs.status == 200 && rs.body && rs.len > 0) {
        int html = !rs.ctype[0];
        for (const char *c = rs.ctype; *c; c++) if ((c[0] == 'h' && c[1] == 't' && c[2] == 'm' && c[3] == 'l') || (c[0] == 't' && c[1] == 'e' && c[2] == 'x' && c[3] == 't')) html = 1;
        if (html) { char t2[160]; n = wl_texto_aislado(rs.body, rs.len, out, cap, t2, sizeof t2); if (t2[0] && tit && tcap) { uint32_t z = 0; while (t2[z] && z + 1 < tcap) { tit[z] = t2[z]; z++; } tit[z] = 0; } }
    }
    wn_free_isolated(&rs);
    return n;
}

/* Wikipedia + DuckDuckGo de una consulta: "- Titulo: resumen (url)" por resultado. Devuelve cuantos. */
static int sw_busca(const char *consulta, char *res, uint32_t cap, int tema, int reciente)
{
    char enc[256]; bw_urlenc(consulta, enc, sizeof enc);
    static struct wn_resp rs; struct wn_req rq; char url[400]; uint32_t u, q = 0; int hits = 0;
    if (cap) res[0] = 0;
    memset(&rq, 0, sizeof rq); u = 0; url[0] = 0;
    soma_str_put(url, sizeof url, &u, "https://es.wikipedia.org/w/api.php?action=query&list=search&format=json&utf8=1&srlimit=3&srsearch=");
    soma_str_put(url, sizeof url, &u, enc);
    rq.url = url; rq.accept = "application/json"; rq.timeout_ms = 8000; rq.max_body = 65536; rq.direct = 1;
    if (wn_fetch_isolated(&rq, &rs) == 0 && rs.status == 200 && rs.body) {
        static char w[2048]; int k = bw_wikipedia_aislado(rs.body, "es", w, sizeof w, 3);
        if (k > 0) { soma_str_put(res, cap, &q, w); hits += k; }
    }
    wn_free_isolated(&rs);
    if (it_wiki_en(tema)) {                                       /* SOMA_336: ciencia, salud y tecnologia: tambien la Wikipedia en ingles */
        memset(&rq, 0, sizeof rq); u = 0; url[0] = 0;
        soma_str_put(url, sizeof url, &u, "https://en.wikipedia.org/w/api.php?action=query&list=search&format=json&utf8=1&srlimit=2&srsearch=");
        soma_str_put(url, sizeof url, &u, enc);
        rq.url = url; rq.accept = "application/json"; rq.timeout_ms = 8000; rq.max_body = 65536; rq.direct = 1;
        if (wn_fetch_isolated(&rq, &rs) == 0 && rs.status == 200 && rs.body) {
            static char w[2048]; int k = bw_wikipedia_aislado(rs.body, "en", w, sizeof w, 2);
            if (k > 0) { soma_str_put(res, cap, &q, w); hits += k; }
        }
        wn_free_isolated(&rs);
    }
    memset(&rq, 0, sizeof rq); u = 0; url[0] = 0;
    soma_str_put(url, sizeof url, &u, "https://lite.duckduckgo.com/lite/?kl=es-es&q=");
    soma_str_put(url, sizeof url, &u, enc);
    if (reciente == IT_RECIENTE_DIA) soma_str_put(url, sizeof url, &u, "&df=d");          /* SOMA_336: lo de hoy */
    else if (reciente == IT_RECIENTE_SEMANA) soma_str_put(url, sizeof url, &u, "&df=w");  /* o de esta semana */
    rq.url = url; rq.accept = "text/html"; rq.timeout_ms = 8000; rq.max_body = 262144; rq.direct = 1;
    if (wn_fetch_isolated(&rq, &rs) == 0 && rs.status == 200 && rs.body) {
        static char d[3072]; int k = bw_ddg_aislado(rs.body, d, sizeof d, 6);
        if (k > 0) { soma_str_put(res, cap, &q, d); hits += k; }
    }
    wn_free_isolated(&rs);
    return hits;
}

static const char SYS_INV_Q[] =
    "Eres el investigador de SOMA. Te doy una pregunta o un tema. Devuelve de 2 a 4 busquedas web que, juntas, encuentren la informacion "
    "para responder bien: cortas (de 2 a 8 palabras), distintas entre si, en el idioma mas util (espanol o ingles). Una por linea, sin "
    "numeros, sin comillas y sin ninguna explicacion. Segun el tema que se indica: en salud, una de las busquedas debe llevar el nombre de "
    "una fuente sanitaria fiable (MedlinePlus, OMS, Ministerio de Sanidad); en deportes o actualidad, si se pregunta por algo reciente, "
    "pon el mes y el ano o 'ultima jornada'; en ciencia o tecnologia, una de las busquedas en ingles.";
static const char SYS_INV_R[] =
    "Eres el investigador de SOMA. Con las fuentes que te doy (texto de paginas web y resultados de busqueda, numerados [1], [2]...), "
    "responde a la pregunta del usuario en espanol con un informe claro en Markdown: empieza con un parrafo que conteste directamente; "
    "despues, si hace falta, secciones con ## y listas o una tabla. Pon la cita [n] detras de cada dato que saques de una fuente. No inventes "
    "nada que no este en las fuentes; si no lo dicen o se contradicen, dilo. Termina con una seccion '## Fuentes' con la lista numerada "
    "(titulo y URL) de las que hayas usado.";

#define SW_NPAG 5
#define SW_PAG_TXT 6000
/* profundo = 1: el modelo propone las busquedas y se leen hasta 5 paginas; 0 (una busqueda, "busca X en internet"): la consulta
 * limpia (ar_consulta) y 3 paginas, una llamada al modelo menos */
int soma_investiga(const char *pregunta, int profundo, char *out, uint32_t cap);
int soma_investiga(const char *pregunta, int profundo, char *out, uint32_t cap)
{
    uint32_t p = 0; if (cap) out[0] = 0;
    while (*pregunta == ' ') pregunta++;
    if (!*pregunta) { soma_str_put(out, cap, &p, "Uso: investiga <pregunta o tema>"); return 0; }
    if (!netif_active() || (!net_ip[0] && !net_ip[1] && !net_ip[2] && !net_ip[3])) {
        soma_str_put(out, cap, &p, "No hay conexion a internet ahora mismo: no puedo investigar."); return 0;
    }
    uint64_t t0 = sw_ms();
    char *res = (char *)kmalloc(16384), *pag = (char *)kmalloc(SW_NPAG * (SW_PAG_TXT + 400)), *tmp = (char *)kmalloc(65536), *rep = (char *)kmalloc(24000);
    static char consultas[4][96]; int nc = 0;
    static char urls[16][300]; static char tits[SW_NPAG][160]; static char purl[SW_NPAG][300]; int np = 0;
    if (!res || !pag || !tmp || !rep) { soma_str_put(out, cap, &p, "Sin memoria para investigar."); goto fin; }
    res[0] = 0;
    gui_fase("buscar");
    /* 1. las busquedas */
    llm_trace_tag("investiga");
    rep[0] = 0;
    char limpia[200]; ar_consulta(pregunta, limpia, sizeof limpia);
    /* SOMA_336_INVESTIGA_TEMAS: el tema decide las fuentes, el filtro de fecha y las instrucciones del informe */
    int tema = it_tema(pregunta), reciente = it_reciente(pregunta, tema);
    {   char l[96] = "tema "; uint32_t lp = 5; soma_str_put(l, sizeof l, &lp, it_nombre(tema)); if (reciente) soma_str_put(l, sizeof l, &lp, reciente == IT_RECIENTE_DIA ? " (de hoy)" : " (reciente)"); gui_fase_orden(l); }
    char qp[640]; { uint32_t z = 0; qp[0] = 0; soma_str_put(qp, sizeof qp, &z, pregunta); soma_str_put(qp, sizeof qp, &z, "\n\nTema: "); soma_str_put(qp, sizeof qp, &z, it_nombre(tema)); if (reciente) soma_str_put(qp, sizeof qp, &z, reciente == IT_RECIENTE_DIA ? " (de hoy)" : " (reciente)"); }
    /* 1200 fichas (antes 200: un modelo que razona las gastaba pensando y no proponia nada) y sin el razonamiento */
    if (profundo && llm_chat_ex(SYS_INV_Q, qp, 0, 0, 1200, rep, 8000, 120000u) == 1 && it_sin_razonamiento(rep) > 0) {
        const char *c = rep;
        while (*c && nc < 3) {
            while (*c == ' ' || *c == '\n' || *c == '-' || *c == '*' || *c == '"' || (*c >= '0' && *c <= '9' && (c[1] == '.' || c[1] == ')'))) { if (*c >= '0' && *c <= '9') c++; c++; }
            uint32_t k = 0; while (c[k] && c[k] != '\n') k++;
            uint32_t m = k; while (m && (c[m - 1] == ' ' || c[m - 1] == '"' || c[m - 1] == '.')) m--;
            if (m >= 3 && m < sizeof consultas[0] && c[m - 1] != ':') { for (uint32_t i = 0; i < m; i++) consultas[nc][i] = c[i]; consultas[nc][m] = 0; nc++; }
            c += k;
        }
    }
    {   /* la peticion limpia, si el modelo no la ha propuesto ya (o no ha contestado) */
        int ya = 0;
        for (int i = 0; i < nc && !ya; i++) { uint32_t k = 0; while (limpia[k] && (limpia[k] | 32) == (consultas[i][k] | 32)) k++; if (!limpia[k] && !consultas[i][k]) ya = 1; }
        if (!ya && nc < 4) { uint32_t k = 0; while (limpia[k] && k + 1 < sizeof consultas[0]) { consultas[nc][k] = limpia[k]; k++; } while (k && ((unsigned char)limpia[k] & 0xC0) == 0x80) k--; consultas[nc][k] = 0; nc++; }
    }
    /* 2. buscar */
    uint32_t rq_ = 0; uint64_t tb = sw_ms();
    for (int i = 0; i < nc; i++) {
        char l[140] = "search "; uint32_t lp = 7; soma_str_put(l, sizeof l, &lp, consultas[i]); gui_fase_orden(l);
        static char r1[6144];
        if (sw_busca(consultas[i], r1, sizeof r1, tema, reciente) > 0) soma_str_put(res, 16384, &rq_, r1);
        if (sw_ms() - tb > 45000) break;                              /* tiempos de cada fase, no desde el principio (el modelo puede tardar) */
    }
    if (!res[0]) { soma_str_put(out, cap, &p, "No he encontrado nada en internet sobre eso (o no hay acceso a Wikipedia ni a DuckDuckGo)."); goto fin; }
    /* 3. leer las paginas */
    int nu = wl_urls(res, urls, 16);
    nu = it_ordena(tema, urls, nu);                                   /* SOMA_336: primero las mejores fuentes para el tema */
    int maxp = profundo ? SW_NPAG : 3;
    uint64_t tp = sw_ms();
    for (int i = 0; i < nu && np < maxp && sw_ms() - tp < 50000; i++) {
        char d[128], l[160] = "leer "; uint32_t lp = 5; wl_dominio(urls[i], d, sizeof d); soma_str_put(l, sizeof l, &lp, d); gui_fase_orden(l);
        uint32_t n = sw_lee(urls[i], tmp, 65536, tits[np], sizeof tits[np], 9000);
        if (n < 300) continue;
        char *dst = pag + np * (SW_PAG_TXT + 400);
        if (n > SW_PAG_TXT) { n = SW_PAG_TXT; while (n > SW_PAG_TXT / 2 && tmp[n] != '\n') n--; while (n > 0 && ((unsigned char)tmp[n] & 0xC0) == 0x80) n--; }
        memcpy(dst, tmp, n); dst[n] = 0;
        uint32_t k = 0; while (urls[i][k] && k + 1 < sizeof purl[0]) { purl[np][k] = urls[i][k]; k++; } purl[np][k] = 0;
        if (!tits[np][0]) { uint32_t z = 0; while (d[z] && z + 1 < sizeof tits[0]) { tits[np][z] = d[z]; z++; } tits[np][z] = 0; }
        np++;
    }
    /* 4. el informe */
    {
        gui_fase("pensar");
        /* lo que cabe en el prompt del modelo (llm-limites): una quinta parte para los resultados, el resto repartido entre las paginas */
        uint32_t B = llm_tam()->prompt; if (B < 16384u) B = 16384u; if (B > 200000u) B = 200000u;
        uint32_t cres = B / 5u, cpag = np ? (B - cres - 2500u) / (uint32_t)np : 0; if (cpag > SW_PAG_TXT) cpag = SW_PAG_TXT;
        if (sw_len(res) > cres) { uint32_t n = cres; while (n > cres / 2 && res[n] != '\n') n--; res[n] = 0; }
        for (int i = 0; i < np; i++) { char *t = pag + i * (SW_PAG_TXT + 400); uint32_t n = sw_len(t); if (n > cpag) { n = cpag; while (n > cpag / 2 && t[n] != '\n' && t[n] != ' ') n--; while (n > 0 && ((unsigned char)t[n] & 0xC0) == 0x80) n--; t[n] = 0; } }
        uint32_t pc = B + 4096u + (uint32_t)np * 600u;
        char *pr = (char *)kmalloc(pc);
        if (pr) {
            uint32_t q = 0; char fecha[80]; fecha[0] = 0;
            (void)dispatch_command("date", fecha, sizeof fecha);
            soma_str_put(pr, pc, &q, "Pregunta del usuario: "); soma_str_put(pr, pc, &q, pregunta);
            if (fecha[0]) { soma_str_put(pr, pc, &q, "\nFecha de hoy: "); soma_str_put(pr, pc, &q, fecha); }
            soma_str_put(pr, pc, &q, "\n\nFUENTES (texto de las paginas):\n");
            for (int i = 0; i < np; i++) {
                char nb[8]; uint32_t z = 0; nb[0] = 0; soma_str_put(nb, sizeof nb, &z, "["); nb[z++] = (char)('1' + i); nb[z] = 0; soma_str_put(nb, sizeof nb, &z, "] ");
                soma_str_put(pr, pc, &q, "\n"); soma_str_put(pr, pc, &q, nb); soma_str_put(pr, pc, &q, tits[i]); soma_str_put(pr, pc, &q, " - "); soma_str_put(pr, pc, &q, purl[i]);
                soma_str_put(pr, pc, &q, "\n"); soma_str_put(pr, pc, &q, pag + i * (SW_PAG_TXT + 400)); soma_str_put(pr, pc, &q, "\n");
            }
            soma_str_put(pr, pc, &q, "\nRESULTADOS DE BUSQUEDA (titulo: resumen (url)), por si ayudan; si citas uno, numeralo a continuacion de las fuentes:\n");
            soma_str_put(pr, pc, &q, res);
            gui_fase("escribir");
            llm_trace_tag("investiga");
            static char sysr[2400]; { uint32_t z = 0; sysr[0] = 0; soma_str_put(sysr, sizeof sysr, &z, SYS_INV_R); soma_str_put(sysr, sizeof sysr, &z, it_instrucciones(tema, reciente)); }
            int ok = llm_chat_ex(sysr, pr, 0, 0, 0, out, cap, 300000u);
            kfree(pr);
            if (ok == 1 && it_sin_razonamiento(out) == 0) ok = 0;         /* SOMA_336: solo el informe (sin el razonamiento) */
            if (ok == 1) {
                char seg[48]; uint32_t z = 0; seg[0] = 0; soma_str_put(seg, sizeof seg, &z, "\n\n_Investigado en ");
                uint32_t s = (uint32_t)((sw_ms() - t0) / 1000); char num[12]; int k = 0; do { num[k++] = (char)('0' + s % 10); s /= 10; } while (s && k < 10);
                while (k) { seg[z++] = num[--k]; }
                seg[z] = 0; soma_str_put(seg, sizeof seg, &z, " s._");
                uint32_t L = sw_len(out); soma_str_put(out, cap, &L, seg);
                goto fin;
            }
        }
        /* sin LLM: lo encontrado, tal cual */
        p = 0; out[0] = 0;
        soma_str_put(out, cap, &p, "No he podido escribir el informe (");
        soma_str_put(out, cap, &p, llm_last_error[0] ? llm_last_error : "el modelo no ha respondido");
        soma_str_put(out, cap, &p, "). Esto es lo que he encontrado:\n\n");
        soma_str_put(out, cap, &p, res);
        for (int i = 0; i < np && i < 2; i++) {
            soma_str_put(out, cap, &p, "\n## "); soma_str_put(out, cap, &p, tits[i]); soma_str_put(out, cap, &p, "\n");
            char *t = pag + i * (SW_PAG_TXT + 400); uint32_t n = sw_len(t); if (n > 1200) { t[1200] = 0; }
            soma_str_put(out, cap, &p, t); soma_str_put(out, cap, &p, "...\n("); soma_str_put(out, cap, &p, purl[i]); soma_str_put(out, cap, &p, ")\n");
        }
    }
fin:
    if (res) kfree(res);
    if (pag) kfree(pag);
    if (tmp) kfree(tmp);
    if (rep) kfree(rep);
    return out[0] != 0;
}

static int cmd_investiga(const char *args, char *out_buf, uint32_t max_out)
{
    if (agent_net_denied("investiga", args, out_buf, max_out)) return 1;
    static char r[24000];
    (void)soma_investiga(args, 1, r, sizeof r);
    kprint(r); kprint("\n");
    uint32_t p = 0; if (out_buf && max_out) { out_buf[0] = 0; soma_str_put(out_buf, max_out, &p, r); }
    return 1;
}
