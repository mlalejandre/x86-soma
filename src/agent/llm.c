#include <stdint.h>

#include "console.h"
#include "http.h"
#include "llm.h"
#include "llm_proto.h"
#include "web_net.h"
#include "fs.h"
#include "thread.h"
#include "idt.h"            /* timer_get_uptime_ms */
#include "mem.h"
#include "net.h"
#include "sysinfo.h"
#include "llm_limites.inc"        /* SOMA_260_LLM_TAM: tamanos segun el contexto del modelo */

/* SOMA_195: el servidor y el modelo ya no estan fijos en el codigo. Se leen de /etc/llm.conf (Ajustes > IA o la orden llm-config):
 * un servidor local o de la red local, o las API de ChatGPT, Claude o Gemini (llm_proto.h). Sin archivo vale lo de siempre
 * (192.168.1.200:8087, modelo nail-35b), y un servidor local por IP y sin clave se sigue pidiendo exactamente igual que antes. */
static struct llm_conf g_conf; static int g_conf_loaded;
char llm_last_error[200];
static volatile char g_llm_busy;

static void set_err(const char *a, const char *b)
{
    uint32_t n = 0;
    for (; a && *a && n < sizeof llm_last_error - 1; a++) llm_last_error[n++] = *a;
    for (; b && *b && n < sizeof llm_last_error - 1; b++) llm_last_error[n++] = (*b == '\n' || *b == '\r') ? ' ' : *b;
    llm_last_error[n] = 0;
}
static void conf_load(void)
{
    if (g_conf_loaded) return;
    static char t[4096];
    /* /etc/llm.conf es un secreto para los agentes (parche 193). Esta lectura es del nucleo, no del agente: el contenido no sale de aqui. */
    struct tcb *th = thread_current(); int am = th ? th->agent_mode : 0;
    if (th) th->agent_mode = 0;
    int n = vfs_read("/etc/llm.conf", t, sizeof t - 1);
    if (th) th->agent_mode = am;
    t[n > 0 ? n : 0] = 0;
    llm_conf_parse(&g_conf, t);
    g_conf_loaded = 1;
}
const struct llm_conf *llm_conf_get(void) { conf_load(); return &g_conf; }
int llm_conf_save(const struct llm_conf *c)
{
    static char t[4096];
    uint32_t n = llm_conf_format(c, t, sizeof t);
    g_conf = *c; g_conf_loaded = 1;
    return vfs_write("/etc/llm.conf", t, n) < 0 ? -1 : 0;
}
/* "http://A.B.C.D:puerto" sin clave: el camino de siempre (cliente HTTP minimo del nucleo) */
static int conf_legacy(uint8_t ip[4], uint16_t *port)
{
    const struct llm_conf *c = llm_conf_get();
    if (c->prov != LLM_LOCAL || c->key[LLM_LOCAL][0]) return 0;
    const char *b = c->base[LLM_LOCAL];
    const char *pre = "http://"; for (int i = 0; i < 7; i++) if (b[i] != pre[i]) return 0;
    b += 7;
    for (int k = 0; k < 4; k++) {
        uint32_t v = 0; int nd = 0;
        while (*b >= '0' && *b <= '9' && nd < 4) { v = v * 10 + (uint32_t)(*b - '0'); b++; nd++; }
        if (!nd || v > 255) return 0;
        ip[k] = (uint8_t)v;
        if (k < 3) { if (*b != '.') return 0; b++; }
    }
    uint32_t p = 80;
    if (*b == ':') { b++; p = 0; while (*b >= '0' && *b <= '9' && p < 100000) p = p * 10 + (uint32_t)(*b++ - '0'); }
    if (*b || p == 0 || p > 65535) return 0;
    *port = (uint16_t)p;
    return 1;
}

char llm_last_reply[4096];
int  llm_test_passed = 0;

static char *http_resp_buf = 0;
static char *payload_buf = 0;

/* SOMA_260_LLM_TAM: del tamano que diga llm_tam() (antes 64 KB fijos); si no hay memoria, 64 KB como antes. Con puede_cambiar
 * (solo con g_llm_busy tomado) se rehacen si cambio el tamano. */
static uint32_t g_resp_cap = 65536, g_pay_cap = 65536;
static int ensure_llm_buffers_ex(int puede_cambiar)
{
    const struct llm_tam *t = llm_tam();
    if (puede_cambiar && http_resp_buf && g_resp_cap != t->http) { kfree(http_resp_buf); http_resp_buf = 0; }
    if (puede_cambiar && payload_buf && g_pay_cap != t->peticion) { kfree(payload_buf); payload_buf = 0; }
    if (!http_resp_buf) {
        http_resp_buf = (char *)kmalloc(t->http); g_resp_cap = t->http;
        if (!http_resp_buf) { http_resp_buf = (char *)kmalloc(65536); g_resp_cap = 65536; }
    }
    if (!payload_buf) {
        payload_buf = (char *)kmalloc(t->peticion); g_pay_cap = t->peticion;
        if (!payload_buf) { payload_buf = (char *)kmalloc(65536); g_pay_cap = 65536; }
    }
    return (http_resp_buf != 0 && payload_buf != 0);
}
static int ensure_llm_buffers(void) { return ensure_llm_buffers_ex(0); }

static uint32_t my_strlen(const char *s)
{
    uint32_t len = 0;
    while (s && s[len]) {
        len++;
    }
    return len;
}

static const char *strstr_simple(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return 0;
    if (!*needle) return haystack;

    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n) {
            return haystack;
        }
    }
    return 0;
}

static int build_chat_payload_mode(const char *prompt, char *dst, uint32_t max, int json_mode)
{
    const char *prefix = 
        "{\"model\":\"nail-35b\","
        "\"messages\":["
        "{\"role\":\"system\",\"content\":\"Eres el nucleo de inteligencia artificial de SOMA (Sistema Operativo Multi-Agente) x86_64. Responde con el objeto JSON estructurado cuando se solicite.\"},"
        "{\"role\":\"user\",\"content\":\"";

    const char *suffix_plain = "\"}],\"temperature\":0.2,\"max_tokens\":8192}";
    const char *suffix_json  = "\"}],\"response_format\":{\"type\":\"json_object\",\"schema\":{\"type\":\"object\",\"properties\":{\"thought\":{\"type\":\"string\"},\"action\":{\"type\":\"string\",\"enum\":[\"tool\",\"final\"]},\"cmd\":{\"type\":\"string\"},\"verdict\":{\"type\":\"string\"}},\"required\":[\"thought\",\"action\",\"cmd\",\"verdict\"]}},\"temperature\":0.1,\"max_tokens\":8192}";
    const char *suffix = json_mode ? suffix_json : suffix_plain;

    uint32_t p = 0;
    while (*prefix && p < max - 1) {
        dst[p++] = *prefix++;
    }

    for (const char *s = prompt; *s && p < max - 1; ++s) {
        if (*s == '"' || *s == '\\') {
            if (p >= max - 2) return -1;
            dst[p++] = '\\';
            dst[p++] = *s;
        } else if (*s == '\n') {
            if (p >= max - 2) return -1;
            dst[p++] = '\\';
            dst[p++] = 'n';
        } else if (*s == '\t') {
            if (p >= max - 2) return -1;
            dst[p++] = '\\';
            dst[p++] = 't';
        } else if (*s == '\r' || (unsigned char)*s < 0x20) {
            /* omitir controles */
        } else {
            dst[p++] = *s;
        }
    }

    while (*suffix && p < max - 1) {
        dst[p++] = *suffix++;
    }

    if (p >= max) return -1;
    dst[p] = '\0';
    return (int)p;
}

static int build_chat_payload(const char *prompt, char *dst, uint32_t max)
{
    return build_chat_payload_mode(prompt, dst, max, 0);
}

static int extract_json_field(const char *json, const char *key, char *out, uint32_t out_max)
{
    if (!json || !key || !out || out_max == 0) {
        return 0;
    }

    const char *p = json;

    for (;;) {
        p = strstr_simple(p, key);
        if (!p) {
            return 0;
        }

        p += my_strlen(key);

        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }

        if (*p != ':') {
            continue;
        }

        p++;

        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }

        if (*p != '"') {
            continue;
        }

        p++;
        break;
    }

    uint32_t len = 0;
    while (*p && *p != '"' && len < out_max - 1) {
        if (*p == '\\') {
            p++;
            if (*p == '"') {
                out[len++] = '"';
            } else if (*p == '\\') {
                out[len++] = '\\';
            } else if (*p == 'n') {
                out[len++] = '\n';
            } else if (*p == 'r') {
                out[len++] = '\r';
            } else if (*p == 't') {
                out[len++] = '\t';
            } else if (*p == '/') {
                out[len++] = '/';
            } else if (*p == 'u') {                               /* SOMA_251_RUTAS: \uXXXX (y pares sustitutos) a UTF-8 */
                uint32_t cp = 0; int ok = 1;
                for (int k = 1; k <= 4; k++) { char c = p[k]; int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; if (v < 0) { ok = 0; break; } cp = cp * 16 + (uint32_t)v; }
                if (!ok) { out[len++] = 'u'; }
                else {
                    p += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && p[1] == '\\' && p[2] == 'u') {
                        uint32_t lo = 0; int ok2 = 1;
                        for (int k = 3; k <= 6; k++) { char c = p[k]; int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; if (v < 0) { ok2 = 0; break; } lo = lo * 16 + (uint32_t)v; }
                        if (ok2 && lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
                    }
                    if (cp < 0x80) out[len++] = (char)cp;
                    else if (cp < 0x800) { if (len + 2 >= out_max) break; out[len++] = (char)(0xC0 | (cp >> 6)); out[len++] = (char)(0x80 | (cp & 0x3F)); }
                    else if (cp < 0x10000) { if (len + 3 >= out_max) break; out[len++] = (char)(0xE0 | (cp >> 12)); out[len++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[len++] = (char)(0x80 | (cp & 0x3F)); }
                    else { if (len + 4 >= out_max) break; out[len++] = (char)(0xF0 | (cp >> 18)); out[len++] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[len++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[len++] = (char)(0x80 | (cp & 0x3F)); }
                }
            } else if (*p) {
                out[len++] = *p;
            } else {
                break;
            }
            if (*p) p++;
        } else {
            out[len++] = *p++;
        }
    }

    out[len] = '\0';
    return (int)len;
}

static int extract_json_content(const char *json, char *out, uint32_t out_max)
{
    if (!json || !out || out_max == 0) return 0;

    uint32_t c_cap = out_max > 49152u ? out_max : 49152u, r_cap = out_max > 32768u ? out_max : 32768u;   /* SOMA_260_LLM_TAM */
    char *c_buf = (char *)kmalloc(c_cap);
    char *r_buf = (char *)kmalloc(r_cap);
    if (!c_buf || !r_buf) {
        if (c_buf) kfree(c_buf);
        if (r_buf) kfree(r_buf);
        return 0;
    }
    c_buf[0] = '\0';
    r_buf[0] = '\0';

    int c_len = extract_json_field(json, "\"content\"", c_buf, c_cap);
    int r_len = extract_json_field(json, "\"reasoning_content\"", r_buf, r_cap);

    if (c_len <= 0 && r_len <= 0) {
        kfree(c_buf);
        kfree(r_buf);
        return 0;
    }

    uint32_t p = 0;

    if (r_len > 0) {
        if (c_len <= 0) {
            const char *r_ptr = r_buf;
            while (*r_ptr && p < out_max - 1) out[p++] = *r_ptr++;
        } else if (c_buf[0] != '{') {
            const char *r_hdr = "[Razonamiento]:\n";
            while (*r_hdr && p < out_max - 1) out[p++] = *r_hdr++;
            const char *r_ptr = r_buf;
            while (*r_ptr && p < out_max - 1) out[p++] = *r_ptr++;
            if (p < out_max - 2) { out[p++] = '\n'; out[p++] = '\n'; }
            const char *c_hdr = "[Respuesta]:\n";
            while (*c_hdr && p < out_max - 1) out[p++] = *c_hdr++;
            const char *c_ptr = c_buf;
            while (*c_ptr && p < out_max - 1) out[p++] = *c_ptr++;
        } else {
            if (strstr_simple(c_buf, "\"thought\":\"\"") != 0 || strstr_simple(c_buf, "\"thought\"") == 0) {
                const char *r_hdr = "<think>";
                while (*r_hdr && p < out_max - 1) out[p++] = *r_hdr++;
                const char *r_ptr = r_buf;
                while (*r_ptr && p < out_max - 1) out[p++] = *r_ptr++;
                const char *r_ftr = "</think>\n";
                while (*r_ftr && p < out_max - 1) out[p++] = *r_ftr++;
            }
            const char *c_ptr = c_buf;
            while (*c_ptr && p < out_max - 1) out[p++] = *c_ptr++;
        }
    } else {
        const char *c_ptr = c_buf;
        while (*c_ptr && p < out_max - 1) {
            out[p++] = *c_ptr++;
        }
    }
    out[p] = '\0';

    kfree(c_buf);
    kfree(r_buf);

    char *start = out;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    if (start != out) {
        uint32_t i = 0;
        while (start[i]) {
            out[i] = start[i];
            i++;
        }
        out[i] = '\0';
        p = i;
    }

    while (p > 0 && (out[p - 1] == ' ' || out[p - 1] == '\t' ||
                     out[p - 1] == '\r' || out[p - 1] == '\n')) {
        out[--p] = '\0';
    }

    return (int)p;
}

static int llm_health_unlocked(void)
{
    if (!ensure_llm_buffers()) return 0;
    struct http_response resp;
    uint8_t ip[4]; uint16_t port;
    if (!conf_legacy(ip, &port)) {                       /* SOMA_195: API en la nube o servidor con nombre/clave: "sano" = bien configurado (sin gastar una peticion) */
        struct llm_http h; int r = llm_proto_models(llm_conf_get(), &h);
        if (r != LLM_P_OK) { set_err("LLM: ", llm_proto_strerror(r)); return 0; }
        return 1;
    }
    int code = http_get(ip, port, "/health", http_resp_buf, g_resp_cap, &resp, 5000);   /* SOMA_260_LLM_TAM */
    if (code != 200) set_err("LLM: el servidor local no responde en ", llm_conf_get()->base[LLM_LOCAL]);
    return (code == 200);
}

int llm_json_get(const char *json, const char *key, char *out, uint32_t out_max)
{
    if (!json || !key || !out || out_max == 0) return 0;
    char quoted_key[64];
    if (key[0] == '"') {
        return extract_json_field(json, key, out, out_max);
    }
    uint32_t klen = 0;
    quoted_key[klen++] = '"';
    while (key[klen - 1] && klen < sizeof(quoted_key) - 2) {
        quoted_key[klen] = key[klen - 1];
        klen++;
    }
    quoted_key[klen++] = '"';
    quoted_key[klen] = '\0';
    return extract_json_field(json, quoted_key, out, out_max);
}

/* SOMA_195: peticion por el cliente HTTP/HTTPS del navegador (nombres DNS, TLS 1.3 con certificado verificado, gzip). */
static int llm_fetch(const struct llm_http *h, const char *body, uint32_t blen, uint32_t timeout_ms, struct wn_resp *rs)
{
    struct wn_req rq; memset(&rq, 0, sizeof rq); memset(rs, 0, sizeof *rs);
    rq.url = h->url; rq.method = h->method; rq.body = body; rq.body_len = blen; rq.ctype = "application/json"; rq.accept = "application/json";
    rq.timeout_ms = timeout_ms; rq.max_body = llm_tam()->http; rq.no_redirect = 1;   /* SOMA_260_LLM_TAM: antes 256 KB */      /* sin redirecciones: la clave no viaja a otro sitio */
    rq.extra_headers = h->headers; rq.direct = 1;
    return wn_fetch(&rq, rs);
}
static void llm_fetch_error(int rc, const struct wn_resp *rs)
{
    static char m[160];
    if (rc != 0) { const char *t = wn_tls_last_error(); set_err("LLM: sin conexion con el servidor: ", rs->err[0] ? rs->err : (t[0] ? t : "error de red")); if (t[0] && rs->err[0]) { uint32_t n = 0; while (llm_last_error[n]) n++; if (n < sizeof llm_last_error - 4) { llm_last_error[n++] = ' '; llm_last_error[n++] = '('; while (*t && n < sizeof llm_last_error - 2) llm_last_error[n++] = *t++; llm_last_error[n++] = ')'; llm_last_error[n] = 0; } } return; }
    if (rs->body && llm_proto_error(rs->body, m, sizeof m)) { set_err(rs->status == 401 || rs->status == 403 ? "LLM: el proveedor rechaza la clave: " : "LLM: el proveedor responde: ", m); return; }
    char st[40] = "LLM: respuesta HTTP "; uint32_t n = 20; int v = rs->status; char d[8]; int k = 0;
    do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v > 0 && k < 7);
    while (k) st[n++] = d[--k];
    st[n] = 0; set_err(st, "");
}

/* ---------------------------------------------------------------- SOMA_251_RUTAS: traza de cada llamada al modelo
 * /agent/traza.txt: hora de arranque, quien llama, milisegundos, caracteres de prompt y respuesta, y los tokens que diga el servidor
 * (entrada, salida, razonamiento oculto, velocidad). Ultimas 40 llamadas. Orden "traza". */
static char g_trace_tag[40] = "?";
void llm_trace_tag(const char *t) { uint32_t i = 0; while (t && t[i] && i + 1 < sizeof g_trace_tag) { g_trace_tag[i] = t[i]; i++; } g_trace_tag[i] = 0; }
#define TRACE_N 40
static char g_trace[TRACE_N][160]; static int g_trace_head, g_trace_n;
static long json_num(const char *j, const char *key)
{
    if (!j) return -1;
    uint32_t kl = my_strlen(key);
    for (const char *p = j; *p; p++) {
        if (*p != '"') continue;
        uint32_t k = 0; while (k < kl && p[1 + k] == key[k]) k++;
        if (k != kl || p[1 + kl] != '"') continue;
        const char *v = p + 2 + kl; while (*v == ' ' || *v == ':') v++;
        if (*v < '0' || *v > '9') continue;
        long n = 0; while (*v >= '0' && *v <= '9') n = n * 10 + (*v++ - '0');
        return n;
    }
    return -1;
}
static void tput(char *o, uint32_t cap, uint32_t *q, const char *s) { while (s && *s && *q + 1 < cap) o[(*q)++] = *s++; o[*q] = 0; }
static void tnum(char *o, uint32_t cap, uint32_t *q, long v) { char b[24]; int i = 23; b[i] = 0; if (v < 0) { tput(o, cap, q, "?"); return; } if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } tput(o, cap, q, b + i); }
static void trace_record(uint64_t t0, uint32_t prompt_chars, const char *json, uint32_t reply_chars, int ok)
{
    char *l = g_trace[g_trace_head]; uint32_t q = 0; l[0] = 0;
    uint64_t now = timer_get_uptime_ms();
    tnum(l, 160, &q, (long)(t0 / 1000)); tput(l, 160, &q, "s "); tput(l, 160, &q, g_trace_tag);
    tput(l, 160, &q, ": "); tnum(l, 160, &q, (long)(now - t0)); tput(l, 160, &q, " ms, prompt "); tnum(l, 160, &q, (long)prompt_chars);
    tput(l, 160, &q, " c, respuesta "); tnum(l, 160, &q, (long)reply_chars); tput(l, 160, &q, " c");
    long pi = json_num(json, "prompt_tokens"); if (pi < 0) pi = json_num(json, "input_tokens"); if (pi < 0) pi = json_num(json, "promptTokenCount");
    long co = json_num(json, "completion_tokens"); if (co < 0) co = json_num(json, "output_tokens"); if (co < 0) co = json_num(json, "candidatesTokenCount");
    long re = json_num(json, "reasoning_tokens"); if (re < 0) re = json_num(json, "thoughtsTokenCount");
    long cached = json_num(json, "cached_tokens"); if (cached < 0) cached = json_num(json, "cache_n");
    if (pi >= 0 || co >= 0) { tput(l, 160, &q, " | tokens "); tnum(l, 160, &q, pi); tput(l, 160, &q, "->"); tnum(l, 160, &q, co); }
    if (re > 0) { tput(l, 160, &q, " (razonamiento "); tnum(l, 160, &q, re); tput(l, 160, &q, ")"); }
    if (cached > 0) { tput(l, 160, &q, " (reutilizados "); tnum(l, 160, &q, cached); tput(l, 160, &q, ")"); }
    if (!ok) tput(l, 160, &q, " | FALLO");
    g_trace_head = (g_trace_head + 1) % TRACE_N; if (g_trace_n < TRACE_N) g_trace_n++;
    static char f[TRACE_N * 162]; uint32_t fq = 0; f[0] = 0;
    for (int i = 0; i < g_trace_n; i++) { int k = (g_trace_head - g_trace_n + i + TRACE_N) % TRACE_N; tput(f, sizeof f, &fq, g_trace[k]); tput(f, sizeof f, &fq, "\n"); }
    (void)vfs_write("/agent/traza.txt", f, fq);
    llm_trace_tag("?");
}
static uint32_t slen_(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }

static int llm_request(const char *prompt, int json_mode, char *reply_out, uint32_t reply_max, uint32_t timeout_ms)
{
    const struct llm_conf *c = llm_conf_get();
    static struct llm_http h;
    while (__atomic_test_and_set(&g_llm_busy, __ATOMIC_ACQUIRE)) thread_sleep(5);          /* una peticion al LLM cada vez (bufferes compartidos) */
    if (!ensure_llm_buffers_ex(1)) { __atomic_clear(&g_llm_busy, __ATOMIC_RELEASE); return 0; }   /* SOMA_260_LLM_TAM: con el turno tomado */
    int ok = 0;
    llm_last_error[0] = 0; llm_recortada = 0;
    uint64_t t_start = timer_get_uptime_ms();
    llm_proto_max_tokens = llm_tam()->salida;
    int blen = llm_proto_chat(c, prompt, json_mode, payload_buf, g_pay_cap, &h);
    if (blen < 0) { set_err("LLM: ", llm_proto_strerror(blen)); kprint(llm_last_error); kprint("\n"); goto out; }
    uint8_t ip[4]; uint16_t port;
    if (conf_legacy(ip, &port)) {                                                           /* el camino de siempre */
        struct http_response resp;
        net_client_lock();
        int code = http_post_json(ip, port, "/v1/chat/completions", payload_buf, http_resp_buf, g_resp_cap, &resp, timeout_ms);
        net_client_unlock();
        if (code != 200) {
            set_err("LLM: el servidor local no responde bien en ", c->base[LLM_LOCAL]);
            kprint("LLM: HTTP error status "); if (code >= 0) kprint_dec((uint32_t)code); else kprint("CONN_ERR"); kprint("\n");
        } else if (!resp.body) { set_err("LLM: respuesta vacia", ""); kprint("LLM: empty HTTP response body\n"); }
        else if (!llm_proto_reply(c, resp.body, reply_out, reply_max) && !extract_json_content(resp.body, reply_out, reply_max)) { set_err("LLM: no se pudo leer la respuesta del servidor", ""); kprint("LLM: could not extract content from JSON response\n"); }
        else ok = 1;
        if (ok) llm_marca_recorte(reply_out, reply_max);                                    /* SOMA_260_LLM_TAM */
        trace_record(t_start, slen_(prompt), code == 200 ? resp.body : 0, ok ? slen_(reply_out) : 0, ok);
        goto out;
    }
    {
        static struct wn_resp rs;
        int rc = llm_fetch(&h, payload_buf, (uint32_t)blen, timeout_ms, &rs);
        if (rc != 0 || rs.status != 200 || !rs.body) { llm_fetch_error(rc, &rs); kprint(llm_last_error); kprint("\n"); }
        else if (!llm_proto_reply(c, rs.body, reply_out, reply_max)) { set_err("LLM: la respuesta no trae texto (", llm_prov_label(c->prov)); kprint(llm_last_error); kprint(")\n"); }
        else ok = 1;
        if (ok) llm_marca_recorte(reply_out, reply_max);                                    /* SOMA_260_LLM_TAM */
        trace_record(t_start, slen_(prompt), rc == 0 ? rs.body : 0, ok ? slen_(reply_out) : 0, ok);
        wn_free(&rs);
    }
out:
    __atomic_clear(&g_llm_busy, __ATOMIC_RELEASE);
    return ok;
}

/* ---------------------------------------------------------------- SOMA_248_DOC: chat con sistema propio e imagenes */
static char g_novis_model[LLM_MODEL_MAX]; static int g_novis_prov = -1;
int llm_vision_unsupported(void)
{
    const struct llm_conf *c = llm_conf_get();
    if (g_novis_prov != c->prov) return 0;
    for (int i = 0; i < LLM_MODEL_MAX; i++) { if (g_novis_model[i] != c->model[c->prov][i]) return 0; if (!g_novis_model[i]) break; }
    return 1;
}
int llm_chat_ex(const char *system, const char *prompt, const struct llm_img *img, int nimg, int max_tokens,
                char *reply_out, uint32_t reply_max, uint32_t timeout_ms)
{
    const struct llm_conf *c = llm_conf_get();
    if (nimg > 0 && llm_vision_unsupported()) { set_err("LLM: el modelo no admite imagenes", ""); return -2; }
    uint32_t cap = llm_proto_chat_ex_size(system, prompt, img, nimg);
    char *body = (char *)kmalloc(cap);
    if (!body) { set_err("LLM: sin memoria para la peticion", ""); return 0; }
    static struct llm_http h;
    while (__atomic_test_and_set(&g_llm_busy, __ATOMIC_ACQUIRE)) thread_sleep(5);
    int ok = 0;
    llm_last_error[0] = 0;
    llm_recortada = 0;
    { const struct llm_tam *t = llm_tam();                    /* SOMA_260_LLM_TAM: la salida elegida en llm-limites manda si es mayor */
      if (max_tokens <= 0) max_tokens = (int)t->salida; else if (t->salida_fijada && (uint32_t)max_tokens < t->salida) max_tokens = (int)t->salida; }
    int blen = llm_proto_chat_ex(c, system, prompt, img, nimg, max_tokens, body, cap, &h);
    if (blen < 0) { set_err("LLM: ", llm_proto_strerror(blen)); goto out; }
    {
        static struct wn_resp rs;
        struct wn_req rq; memset(&rq, 0, sizeof rq); memset(&rs, 0, sizeof rs);
        rq.url = h.url; rq.method = h.method; rq.body = body; rq.body_len = (uint32_t)blen; rq.ctype = "application/json"; rq.accept = "application/json";
        rq.timeout_ms = timeout_ms; rq.max_body = tam_max(1u << 20, llm_tam()->http); rq.no_redirect = 1; rq.extra_headers = h.headers; rq.direct = 1;
        uint64_t t_start = timer_get_uptime_ms();
        int rc = wn_fetch(&rq, &rs);
        if (rc != 0 || rs.status != 200 || !rs.body) {
            static char m[200]; m[0] = 0;
            if (rc == 0 && rs.body) (void)llm_proto_error(rs.body, m, sizeof m);
            if (rc == 0 && nimg > 0 && llm_proto_no_vision(rs.status, m[0] ? m : rs.body)) {
                g_novis_prov = c->prov; for (int i = 0; i < LLM_MODEL_MAX; i++) { g_novis_model[i] = c->model[c->prov][i]; if (!g_novis_model[i]) break; }
                set_err("LLM: el modelo no admite imagenes: ", m); ok = -2;
            } else llm_fetch_error(rc, &rs);
            kprint(llm_last_error); kprint("\n");
        } else if (!llm_proto_reply(c, rs.body, reply_out, reply_max)) { set_err("LLM: la respuesta no trae texto (", llm_prov_label(c->prov)); kprint(llm_last_error); kprint(")\n"); }
        else ok = 1;
        if (ok == 1) llm_marca_recorte(reply_out, reply_max);                               /* SOMA_260_LLM_TAM */
        trace_record(t_start, slen_(prompt) + slen_(system), rc == 0 ? rs.body : 0, ok == 1 ? slen_(reply_out) : 0, ok == 1);
        wn_free(&rs);
    }
out:
    __atomic_clear(&g_llm_busy, __ATOMIC_RELEASE);
    kfree(body);
    return ok;
}

/* ---------------------------------------------------------------- SOMA_196: lanzador del servidor local (tools/soma_lanzador.py) */
static int launcher_call(int what, const char *model, struct wn_resp *rs)
{
    static struct llm_http h; static char body[256];
    int n = llm_proto_launcher(llm_conf_get(), what, model, body, sizeof body, &h);
    if (n < 0) { set_err("Lanzador: ", llm_proto_strerror(n)); return -1; }
    llm_last_error[0] = 0;
    int rc = llm_fetch(&h, n ? body : 0, (uint32_t)n, 20000, rs);
    if (rc != 0) { char b[LLM_BASE_MAX]; llm_launcher_base(llm_conf_get(), b, sizeof b); set_err("Lanzador: no responde en ", b); return -2; }
    if (rs->status != 200 || !rs->body) {
        static char m[160];
        if (rs->body && llm_proto_error(rs->body, m, sizeof m)) set_err("Lanzador: ", m); else set_err("Lanzador: respuesta no valida", "");
        return -1;
    }
    return 0;
}
/* Modelos que el lanzador puede arrancar, uno por linea. Devuelve cuantos o -1. */
int llm_launcher_models(char *out, uint32_t max)
{
    static struct wn_resp rs; static const struct llm_conf oa = { .prov = LLM_LOCAL };
    if (max) out[0] = 0;
    while (__atomic_test_and_set(&g_llm_busy, __ATOMIC_ACQUIRE)) thread_sleep(5);
    int n = launcher_call(LLM_L_MODELS, 0, &rs);
    if (n == 0) n = llm_proto_model_list(&oa, rs.body, out, max); else n = -1;
    wn_free(&rs);
    __atomic_clear(&g_llm_busy, __ATOMIC_RELEASE);
    return n;
}
/* Estado del servidor segun el lanzador: 1 listo, 2 cargando, 0 parado, -1 error, -2 el lanzador no responde (motivo en llm_last_error). */
int llm_launcher_status(char *model, uint32_t mmax)
{
    static struct wn_resp rs; char st[24]; static char det[180]; char mo[LLM_MODEL_MAX];
    if (model && mmax) model[0] = 0;
    while (__atomic_test_and_set(&g_llm_busy, __ATOMIC_ACQUIRE)) thread_sleep(5);
    int r = launcher_call(LLM_L_STATUS, 0, &rs);
    if (r == 0) {
        if (!llm_proto_launcher_status(rs.body, st, sizeof st, mo, sizeof mo, det, sizeof det)) { set_err("Lanzador: respuesta no valida", ""); r = -1; }
        else {
            if (model) { uint32_t i = 0; for (; mo[i] && i + 1 < mmax; i++) model[i] = mo[i]; if (mmax) model[i] = 0; }
            r = st[0] == 'l' ? 1 : st[0] == 'c' ? 2 : st[0] == 'p' ? 0 : -1;
            if (r < 0) set_err("Lanzador: ", det[0] ? det : "el servidor no arranco");
        }
    }
    wn_free(&rs);
    __atomic_clear(&g_llm_busy, __ATOMIC_RELEASE);
    return r;
}
static int launcher_post(int what, const char *model)
{
    static struct wn_resp rs;
    while (__atomic_test_and_set(&g_llm_busy, __ATOMIC_ACQUIRE)) thread_sleep(5);
    int r = launcher_call(what, model, &rs);
    wn_free(&rs);
    __atomic_clear(&g_llm_busy, __ATOMIC_RELEASE);
    return r;
}
int llm_launcher_stop(void) { return launcher_post(LLM_L_STOP, 0) == 0 ? 0 : -1; }

/* SOMA_278_EMBEDDINGS: el servidor de embeddings (memoria de SOMA). Es independiente del del LLM: arrancarlo o pararlo no lo toca. */
int llm_launcher_emb_status(void)
{
    static struct wn_resp rs; char st[24]; static char det[180]; char mo[LLM_MODEL_MAX];
    while (__atomic_test_and_set(&g_llm_busy, __ATOMIC_ACQUIRE)) thread_sleep(5);
    int r = launcher_call(LLM_L_EMB_STATUS, 0, &rs);
    if (r == 0) {
        if (!llm_proto_launcher_status(rs.body, st, sizeof st, mo, sizeof mo, det, sizeof det)) { set_err("Lanzador: respuesta no valida", ""); r = -1; }
        else {
            r = st[0] == 'l' ? 1 : st[0] == 'c' ? 2 : st[0] == 'p' ? 0 : -1;
            if (r < 0) set_err("Lanzador: ", det[0] ? det : "el servidor de embeddings no arranco");
        }
    }
    wn_free(&rs);
    __atomic_clear(&g_llm_busy, __ATOMIC_RELEASE);
    return r;
}
int llm_launcher_emb_stop(void) { return launcher_post(LLM_L_EMB_STOP, 0) == 0 ? 0 : -1; }
int llm_launcher_emb_start(uint32_t max_wait_ms, void (*progress)(uint32_t secs))
{
    if (launcher_post(LLM_L_EMB_START, 0) != 0) return 0;
    uint64_t t0 = timer_get_uptime_ms();
    int fails = 0;
    for (;;) {
        thread_sleep(1000);
        uint32_t el = (uint32_t)(timer_get_uptime_ms() - t0);
        if (progress) progress(el / 1000);
        int st = llm_launcher_emb_status();
        if (st == 1) return 1;
        if (st == 0) { set_err("Lanzador: el servidor de embeddings se ha parado sin llegar a arrancar", ""); return 0; }
        if (st == -1 || (st == -2 && ++fails > 5)) return 0;
        if (st == 2) fails = 0;
        if (el > max_wait_ms) { set_err("Lanzador: el servidor de embeddings no termino de cargar a tiempo", ""); return 0; }
    }
}
/* Arranca llama-server con ese modelo y espera a que este listo (cargar un modelo grande tarda). progress(segundos) se llama cada
 * par de segundos. Si arranca, deja el modelo y el proveedor local elegidos y guardados. 1 = listo. */
int llm_launcher_start(const char *model, uint32_t max_wait_ms, void (*progress)(uint32_t secs))
{
    if (launcher_post(LLM_L_START, model) != 0) return 0;
    uint64_t t0 = timer_get_uptime_ms();
    int fails = 0;
    for (;;) {
        thread_sleep(2000);
        uint32_t el = (uint32_t)(timer_get_uptime_ms() - t0);
        if (progress) progress(el / 1000);
        int st = llm_launcher_status(0, 0);
        if (st == 1) break;
        if (st == 0) { set_err("Lanzador: el servidor se ha parado sin llegar a arrancar", ""); return 0; }
        if (st == -1 || (st == -2 && ++fails > 5)) return 0;                    /* llama-server fallo; o el lanzador dejo de responder */
        if (st == 2) fails = 0;
        if (el > max_wait_ms) { set_err("Lanzador: el modelo no termino de cargar a tiempo", ""); return 0; }
    }
    static struct llm_conf c; c = *llm_conf_get();
    c.prov = LLM_LOCAL;
    if (llm_conf_set(&c, LLM_LOCAL, "modelo", model) == 0) (void)llm_conf_save(&c);
    return 1;
}

/* Lista de modelos del proveedor activo, uno por linea. Devuelve cuantos (o -1; el motivo queda en llm_last_error). */
int llm_list_models(char *out, uint32_t max)
{
    const struct llm_conf *c = llm_conf_get();
    static struct llm_http h; static struct wn_resp rs;
    if (max) out[0] = 0;
    int r = llm_proto_models(c, &h);
    if (r != LLM_P_OK) { set_err("LLM: ", llm_proto_strerror(r)); return -1; }
    while (__atomic_test_and_set(&g_llm_busy, __ATOMIC_ACQUIRE)) thread_sleep(5);
    llm_last_error[0] = 0;
    int n = -1;
    int rc = llm_fetch(&h, 0, 0, 15000, &rs);
    if (rc != 0 || rs.status != 200 || !rs.body) llm_fetch_error(rc, &rs);
    else n = llm_proto_model_list(c, rs.body, out, max);
    wn_free(&rs);
    __atomic_clear(&g_llm_busy, __ATOMIC_RELEASE);
    return n;
}

void net_run_llm_test(void)
{
    kprint("\n============================================================\n");
    kprint("SOMA - HTTP & LLM INTEGRATION TEST\n");
    { const struct llm_conf *c = llm_conf_get();
      kprint("Target server: "); kprint(c->base[c->prov]); kprint(" ("); kprint(llm_prov_label(c->prov)); kprint(", model: "); kprint(c->model[c->prov][0] ? c->model[c->prov] : "sin elegir"); kprint(")\n"); }
    kprint("============================================================\n");

    if (!net_init()) {
        kprint("NET: init FAILED\n");
        return;
    }

    kprint("\n[STEP 1] Testing HTTP GET /health...\n");
    if (!llm_health()) {
        kprint("LLM: health check FAILED\n");
        return;
    }
    kprint("LLM: health check OK (HTTP 200 OK)\n");

    kprint("\n[STEP 2] Testing LLM Chat Completion (POST /v1/chat/completions)...\n");
    static const char test_prompt[] = "Responde unicamente: SOMA TEST OK";
    kprint("Prompt: \"");
    kprint(test_prompt);
    kprint("\"\n");
    kprint("Waiting for the model to generate response...\n");

    memset(llm_last_reply, 0, sizeof(llm_last_reply));

    if (llm_chat(test_prompt, llm_last_reply, sizeof(llm_last_reply), 30000)) {
        kprint("============================================================\n");
        kprint("LLM RESPONSE EXTRACTED:\n");
        kprint(">> ");
        kprint(llm_last_reply);
        kprint("\n============================================================\n");

        llm_test_passed = 1;
        kprint("\nNET: HTTP & LLM layer OK!\n\n");
    } else {
        kprint("\nNET: LLM layer FAILED\n\n");
    }
}

int llm_diagnose(const char *user_question, char *reply_out, uint32_t reply_max, uint32_t timeout_ms)
{
    static char prompt_combined[4096];
    uint32_t pos = 0;

    char telemetry[1024];
    sysinfo_format_telemetry(telemetry, sizeof(telemetry));

    for (const char *p = telemetry; *p && pos < sizeof(prompt_combined) - 1; ++p) {
        prompt_combined[pos++] = *p;
    }

    const char *intro = "\nInstruccion: ";
    for (const char *p = intro; *p && pos < sizeof(prompt_combined) - 1; ++p) {
        prompt_combined[pos++] = *p;
    }

    const char *q = (user_question && *user_question) 
        ? user_question 
        : "Analiza la telemetria de MYOS. Emite directamente tu diagnostico en 3 frases en 'content'. No uses razonamiento interno.";

    for (const char *p = q; *p && pos < sizeof(prompt_combined) - 1; ++p) {
        prompt_combined[pos++] = *p;
    }

    prompt_combined[pos] = '\0';

    return llm_chat(prompt_combined, reply_out, reply_max, timeout_ms);
}

int llm_health(void)
{
    net_client_lock();
    int r = llm_health_unlocked();
    net_client_unlock();
    return r;
}

int llm_chat_json(const char *prompt, char *reply_out, uint32_t reply_max, uint32_t timeout_ms)
{
    return llm_request(prompt, 1, reply_out, reply_max, timeout_ms);       /* SOMA_195 */
}

int llm_chat(const char *prompt, char *reply_out, uint32_t reply_max, uint32_t timeout_ms)
{
    return llm_request(prompt, 0, reply_out, reply_max, timeout_ms);       /* SOMA_195 */
}
