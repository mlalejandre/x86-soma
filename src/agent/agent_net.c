/* SOMA_193: ver agent_net.h. Sin dependencias del nucleo. */
#include "agent_net.h"

#define SEEN_MAX 512
static uint64_t seen_url[SEEN_MAX], seen_host[SEEN_MAX];
static uint32_t seen_n, host_n, stat_urls, stat_denied;
static int g_mode = AGENT_NET_PROCEDENCIA;
static char allow[AGENT_NET_ALLOW_MAX][AGENT_NET_SUFFIX_MAX];

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int is_host_ch(char c) { c = lower(c); return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'; }
static int is_url_end(char c) { return c == 0 || (unsigned char)c <= ' ' || c == '"' || c == '\'' || c == '<' || c == '>' || c == '`' || c == '\\' || c == '{' || c == '}' || c == '|' || c == '^'; }
static int starts_ci(const char *s, const char *p) { while (*p) { if (lower(*s++) != *p++) return 0; } return 1; }

struct nurl { const char *host; uint32_t host_len; uint64_t hash, host_hash; };

/* Normaliza y resume una URL: http y https cuentan igual, el host en minusculas, sin puerto por defecto, sin fragmento ni barras o
 * puntuacion finales. `len` = caracteres de la URL (hasta el primer separador). Devuelve 0 si no hay un host valido. */
static int url_digest(const char *u, uint32_t len, struct nurl *o)
{
    uint32_t i = 0;
    if (len >= 7 && starts_ci(u, "http://")) i = 7;
    else if (len >= 8 && starts_ci(u, "https://")) i = 8;
    else { for (uint32_t k = 0; k + 2 < len; k++) if (u[k] == ':' && u[k + 1] == '/' && u[k + 2] == '/') return 0; }   /* otro esquema: no */
    for (uint32_t k = i; k < len; k++) if (u[k] == '@') { int in_auth = 1; for (uint32_t q = i; q < k; q++) if (u[q] == '/' || u[q] == '?') in_auth = 0; if (in_auth) return 0; }   /* usuario@host: no */
    uint32_t hs = i;
    while (i < len && is_host_ch(u[i])) i++;
    uint32_t hl = i - hs;
    if (hl == 0 || hl > 253) return 0;
    int dot = 0; for (uint32_t k = 0; k < hl; k++) if (u[hs + k] == '.') dot = 1;
    if (!dot) return 0;
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t k = 0; k < hl; k++) { h ^= (uint8_t)lower(u[hs + k]); h *= 1099511628211ULL; }
    o->host = u + hs; o->host_len = hl; o->host_hash = h;
    if (i < len && u[i] == ':') {                                   /* puerto: se omite el 80 y el 443 */
        uint32_t ps = i + 1, pe = ps; while (pe < len && u[pe] >= '0' && u[pe] <= '9') pe++;
        uint32_t port = 0; for (uint32_t k = ps; k < pe && port < 100000; k++) port = port * 10 + (uint32_t)(u[k] - '0');
        if (pe == ps || port == 0 || port > 65535) return 0;
        if (port != 80 && port != 443) { h ^= ':'; h *= 1099511628211ULL; for (uint32_t k = ps; k < pe; k++) { h ^= (uint8_t)u[k]; h *= 1099511628211ULL; } }
        i = pe;
    }
    if (i < len && u[i] != '/' && u[i] != '?' && u[i] != '#') return 0;
    uint32_t end = len;
    for (uint32_t k = i; k < len; k++) if (u[k] == '#') { end = k; break; }
    while (end > i && (u[end - 1] == '/' || u[end - 1] == '.' || u[end - 1] == ',' || u[end - 1] == ';' || u[end - 1] == ')' || u[end - 1] == ']' || u[end - 1] == '!' || u[end - 1] == '?')) end--;
    for (uint32_t k = i; k < end; k++) { h ^= (uint8_t)u[k]; h *= 1099511628211ULL; }
    o->hash = h ? h : 1;
    return 1;
}

static uint32_t url_len(const char *u) { uint32_t n = 0; while (!is_url_end(u[n]) && n < 4096) n++; return n; }

static int host_trusted(const char *h, uint32_t hl)
{
    for (int a = 0; a < AGENT_NET_ALLOW_MAX; a++) {
        if (!allow[a][0]) continue;
        uint32_t sl = 0; while (allow[a][sl]) sl++;
        if (sl > hl) continue;
        int eq = 1; for (uint32_t k = 0; k < sl; k++) if (lower(h[hl - sl + k]) != allow[a][k]) { eq = 0; break; }
        if (eq && (sl == hl || h[hl - sl - 1] == '.')) return 1;      /* el dominio entero o un subdominio, nunca "malowikipedia.org" */
    }
    return 0;
}

void agent_net_set_mode(int mode) { if (mode >= AGENT_NET_ABIERTO && mode <= AGENT_NET_CERRADO) g_mode = mode; }
int agent_net_mode(void) { return g_mode; }
void agent_net_allow_clear(void) { for (int a = 0; a < AGENT_NET_ALLOW_MAX; a++) allow[a][0] = 0; }
const char *agent_net_allow_get(int i)
{
    for (int a = 0; a < AGENT_NET_ALLOW_MAX; a++) if (allow[a][0] && i-- == 0) return allow[a];
    return 0;
}
static int suffix_norm(const char *s, char *out)
{
    uint32_t n = 0; int dot = 0;
    while (*s == ' ' || *s == '.' || *s == '*') s++;
    while (*s && *s != ' ' && *s != '\n' && *s != '\r') {
        if (!is_host_ch(*s) || n >= AGENT_NET_SUFFIX_MAX - 1) return 0;
        if (*s == '.') dot = 1;
        out[n++] = lower(*s++);
    }
    while (n && out[n - 1] == '.') n--;
    out[n] = 0;
    return n >= 3 && dot;                                             /* "com" o "." solos no: abririan todo */
}
int agent_net_allow_add(const char *suffix)
{
    char t[AGENT_NET_SUFFIX_MAX];
    if (!suffix || !suffix_norm(suffix, t)) return -1;
    int free_slot = -1;
    for (int a = 0; a < AGENT_NET_ALLOW_MAX; a++) {
        if (!allow[a][0]) { if (free_slot < 0) free_slot = a; continue; }
        int eq = 1; for (uint32_t k = 0; t[k] || allow[a][k]; k++) if (t[k] != allow[a][k]) { eq = 0; break; }
        if (eq) return 0;
    }
    if (free_slot < 0) return -1;
    uint32_t k = 0; for (; t[k]; k++) allow[free_slot][k] = t[k];
    allow[free_slot][k] = 0;
    return 0;
}
int agent_net_allow_del(const char *suffix)
{
    char t[AGENT_NET_SUFFIX_MAX];
    if (!suffix || !suffix_norm(suffix, t)) return -1;
    for (int a = 0; a < AGENT_NET_ALLOW_MAX; a++) {
        if (!allow[a][0]) continue;
        int eq = 1; for (uint32_t k = 0; t[k] || allow[a][k]; k++) if (t[k] != allow[a][k]) { eq = 0; break; }
        if (eq) { allow[a][0] = 0; return 0; }
    }
    return -1;
}

void agent_net_observe(const char *text)
{
    if (!text) return;
    for (const char *p = text; *p; p++) {
        if ((*p != 'h' && *p != 'H') || !(starts_ci(p, "http://") || starts_ci(p, "https://"))) continue;
        uint32_t n = url_len(p); struct nurl u;
        if (url_digest(p, n, &u)) {
            int dup = 0;
            for (uint32_t k = 0; k < (seen_n < SEEN_MAX ? seen_n : SEEN_MAX); k++) if (seen_url[k] == u.hash) { dup = 1; break; }
            if (!dup) { seen_url[seen_n++ % SEEN_MAX] = u.hash; stat_urls++; }
            dup = 0;
            for (uint32_t k = 0; k < (host_n < SEEN_MAX ? host_n : SEEN_MAX); k++) if (seen_host[k] == u.host_hash) { dup = 1; break; }
            if (!dup) seen_host[host_n++ % SEEN_MAX] = u.host_hash;
        }
        p += n ? n - 1 : 0;
    }
}

int agent_net_url_ok(const char *url)
{
    if (g_mode == AGENT_NET_ABIERTO) return 1;
    if (!url) { stat_denied++; return 0; }
    while (*url == ' ' || *url == '"' || *url == '\'') url++;
    struct nurl u;
    if (!url_digest(url, url_len(url), &u)) { stat_denied++; return 0; }
    if (host_trusted(u.host, u.host_len)) return 1;
    if (g_mode == AGENT_NET_PROCEDENCIA)
        for (uint32_t k = 0; k < (seen_n < SEEN_MAX ? seen_n : SEEN_MAX); k++) if (seen_url[k] == u.hash) return 1;
    stat_denied++;
    return 0;
}

int agent_net_host_ok(const char *host)
{
    if (g_mode == AGENT_NET_ABIERTO) return 1;
    if (!host) { stat_denied++; return 0; }
    while (*host == ' ' || *host == '"' || *host == '\'') host++;
    uint32_t hl = 0; while (is_host_ch(host[hl])) hl++;
    if (hl == 0 || hl > 253 || !is_url_end(host[hl])) { stat_denied++; return 0; }
    if (host_trusted(host, hl)) return 1;
    if (g_mode == AGENT_NET_PROCEDENCIA) {
        uint64_t h = 1469598103934665603ULL;
        for (uint32_t k = 0; k < hl; k++) { h ^= (uint8_t)lower(host[k]); h *= 1099511628211ULL; }
        for (uint32_t k = 0; k < (host_n < SEEN_MAX ? host_n : SEEN_MAX); k++) if (seen_host[k] == h) return 1;
    }
    stat_denied++;
    return 0;
}

/* Cada palabra de `args` que parezca un destino (URL o nombre con punto) debe estar permitida. Las opciones (-x), los numeros y las
 * rutas locales (/ruta, nombre.txt sin barra tras un host valido...) no cuentan: una palabra sin "://" solo se trata como destino
 * si es un nombre de host seguido, como mucho, de puerto, ruta o consulta. */
int agent_net_args_ok(const char *args)
{
    if (g_mode == AGENT_NET_ABIERTO || !args) return 1;
    const char *p = args;
    while (*p) {
        while (*p == ' ' || *p == '"' || *p == '\'') p++;
        if (!*p) break;
        const char *w = p; uint32_t n = 0;
        while (p[n] && p[n] != ' ' && p[n] != '"' && p[n] != '\'') n++;
        p += n;
        if (w[0] == '-' || w[0] == '/' || w[0] == '.') continue;
        int scheme = 0; for (uint32_t k = 0; k + 2 < n; k++) if (w[k] == ':' && w[k + 1] == '/' && w[k + 2] == '/') { scheme = 1; break; }
        if (scheme) { if (!agent_net_url_ok(w)) return 0; continue; }
        uint32_t hl = 0; int dot = 0, alpha = 0;
        while (hl < n && is_host_ch(w[hl])) { if (w[hl] == '.') dot = 1; else if (lower(w[hl]) >= 'a' && lower(w[hl]) <= 'z') alpha = 1; hl++; }
        if (!dot || !hl) continue;                                         /* sin punto: no es un destino */
        if (hl < n && w[hl] != ':' && w[hl] != '/' && w[hl] != '?') continue;
        if (!alpha && hl == n) {                                           /* IP literal sola (ping, dns inverso): se trata como host */
            if (!agent_net_host_ok(w)) return 0;
            continue;
        }
        if (hl == n) { if (!agent_net_host_ok(w)) return 0; }
        else if (!agent_net_url_ok(w)) return 0;                           /* host/ruta sin esquema: como http://host/ruta */
    }
    return 1;
}

void agent_net_stats(uint32_t *seen_urls, uint32_t *denied) { if (seen_urls) *seen_urls = stat_urls; if (denied) *denied = stat_denied; }
