/* SOMA_195: ver llm_proto.h. Sin dependencias del nucleo. */
#include "llm_proto.h"

static const char SYS_PROMPT[] = "Eres el nucleo de inteligencia artificial de SOMA (Sistema Operativo Multi-Agente). Responde con el objeto JSON estructurado cuando se solicite.";
static const char SYS_JSON[] = " Cuando se pida JSON, responde SOLO con el objeto JSON: sin texto alrededor y sin bloques de codigo.";
/* esquema de la respuesta del agente para servidores locales que admiten gramaticas (llama.cpp): el mismo de siempre */
static const char LOCAL_JSON_FMT[] = ",\"response_format\":{\"type\":\"json_object\",\"schema\":{\"type\":\"object\",\"properties\":{\"thought\":{\"type\":\"string\"},\"action\":{\"type\":\"string\",\"enum\":[\"tool\",\"final\"]},\"cmd\":{\"type\":\"string\"},\"verdict\":{\"type\":\"string\"}},\"required\":[\"thought\",\"action\"]}}";

static uint32_t slen(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int starts(const char *s, const char *p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }
static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int starts_ci(const char *s, const char *p) { while (*p) { if (lower(*s++) != *p++) return 0; } return 1; }
static void scpy(char *d, const char *s, uint32_t max) { uint32_t i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
struct sb { char *p; uint32_t n, max; int over; };
static void sb_c(struct sb *b, char c) { if (b->n + 1 < b->max) b->p[b->n++] = c; else b->over = 1; }
static void sb_s(struct sb *b, const char *s) { while (*s) sb_c(b, *s++); }
/* texto dentro de una cadena JSON */
static void sb_json(struct sb *b, const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { sb_c(b, '\\'); sb_c(b, (char)c); }
        else if (c == '\n') { sb_c(b, '\\'); sb_c(b, 'n'); }
        else if (c == '\t') { sb_c(b, '\\'); sb_c(b, 't'); }
        else if (c < 0x20) { /* otros controles: fuera */ }
        else sb_c(b, (char)c);
    }
}

static const char *const NAMES[LLM_NPROV] = { "local", "openai", "claude", "gemini" };
static const char *const LABELS[LLM_NPROV] = { "Local / red local", "ChatGPT (OpenAI)", "Claude (Anthropic)", "Gemini (Google)" };
static const char *const DEF_BASE[LLM_NPROV] = { "http://192.168.1.200:8087", "https://api.openai.com", "https://api.anthropic.com", "https://generativelanguage.googleapis.com" };
static const char *const DEF_MODEL[LLM_NPROV] = { "nail-35b", "", "claude-sonnet-5-5", "" };

const char *llm_prov_name(int p) { return p >= 0 && p < LLM_NPROV ? NAMES[p] : "?"; }
const char *llm_prov_label(int p) { return p >= 0 && p < LLM_NPROV ? LABELS[p] : "?"; }
int llm_prov_by_name(const char *n)
{
    if (!n) return -1;
    while (*n == ' ') n++;
    char t[16]; uint32_t k = 0; while (n[k] && n[k] != ' ' && n[k] != '\n' && n[k] != '\r' && k < 15) { t[k] = lower(n[k]); k++; } t[k] = 0;
    for (int i = 0; i < LLM_NPROV; i++) if (seq(t, NAMES[i])) return i;
    if (seq(t, "chatgpt") || seq(t, "gpt")) return LLM_OPENAI;
    if (seq(t, "anthropic")) return LLM_CLAUDE;
    if (seq(t, "google")) return LLM_GEMINI;
    return -1;
}
void llm_conf_defaults(struct llm_conf *c)
{
    c->prov = LLM_LOCAL;
    for (int i = 0; i < LLM_NPROV; i++) { scpy(c->base[i], DEF_BASE[i], LLM_BASE_MAX); scpy(c->model[i], DEF_MODEL[i], LLM_MODEL_MAX); c->key[i][0] = 0; }
    c->launcher[0] = 0; c->launcher_key[0] = 0;                                 /* SOMA_196 */
}

/* host de una URL base: devuelve su inicio y longitud (0 si la URL no vale). *https = 1 si es https. */
static const char *base_host(const char *b, uint32_t *hl, int *https)
{
    const char *h;
    if (starts_ci(b, "https://")) { *https = 1; h = b + 8; } else if (starts_ci(b, "http://")) { *https = 0; h = b + 7; } else { *hl = 0; return 0; }
    uint32_t n = 0;
    while (h[n] && h[n] != ':' && h[n] != '/') {
        char ch = lower(h[n]);
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-')) { *hl = 0; return 0; }
        n++;
    }
    if (h[n] == ':') { uint32_t k = n + 1, d = 0; while (h[k] >= '0' && h[k] <= '9') { k++; d++; } if (!d || d > 5 || (h[k] && h[k] != '/')) { *hl = 0; return 0; } }
    *hl = n;
    return n ? h : 0;
}
int llm_base_is_private(const char *base)
{
    uint32_t hl; int https; const char *h = base_host(base, &hl, &https);
    if (!h) return 0;
    char t[80]; if (hl >= sizeof t) return 0;
    for (uint32_t i = 0; i < hl; i++) t[i] = lower(h[i]);
    t[hl] = 0;
    if (seq(t, "localhost") || (hl > 6 && seq(t + hl - 6, ".local")) || (hl > 4 && seq(t + hl - 4, ".lan"))) return 1;
    uint32_t v[4] = { 0, 0, 0, 0 }; int k = 0, nd = 0;
    for (uint32_t i = 0; i <= hl; i++) {
        if (i < hl && t[i] >= '0' && t[i] <= '9') { v[k] = v[k] * 10 + (uint32_t)(t[i] - '0'); nd++; if (v[k] > 255) return 0; }
        else if (i == hl || t[i] == '.') { if (!nd) return 0; nd = 0; if (i < hl && ++k > 3) return 0; }
        else return 0;                                             /* un nombre que no es de red local */
    }
    if (k != 3) return 0;
    return v[0] == 10 || v[0] == 127 || (v[0] == 192 && v[1] == 168) || (v[0] == 172 && v[1] >= 16 && v[1] <= 31) || (v[0] == 169 && v[1] == 254);
}

static int clean_value(const char *v, char *out, uint32_t max)
{
    while (*v == ' ' || *v == '\t') v++;
    uint32_t n = 0;
    while (v[n] && v[n] != '\n' && v[n] != '\r') { if ((unsigned char)v[n] < 0x20 || (unsigned char)v[n] == 0x7F) return 0; n++; }
    for (uint32_t k = n; v[k]; k++) if (v[k] != '\n' && v[k] != '\r' && v[k] != ' ' && v[k] != '\t') return 0;      /* nada tras un salto de linea */
    while (n && (v[n - 1] == ' ' || v[n - 1] == '\t')) n--;
    if (n + 1 > max) return 0;
    for (uint32_t i = 0; i < n; i++) out[i] = v[i];
    out[n] = 0;
    return 1;
}
int llm_conf_set(struct llm_conf *c, int prov, const char *field, const char *value)
{
    if (!field || !value) return -1;
    char t[LLM_KEY_MAX];
    int lz = seq(field, "lanzador"), lk = seq(field, "clave-lanzador");         /* SOMA_196: no dependen del proveedor */
    if (lz || lk) prov = LLM_LOCAL;
    if (prov < 0 || prov >= LLM_NPROV) return -1;
    if (lk) {
        if (!clean_value(value, t, LLM_KEY_MAX)) return -1;
        for (uint32_t i = 0; t[i]; i++) if (t[i] == ' ' || t[i] == '"' || (unsigned char)t[i] >= 0x7F) return -1;
        scpy(c->launcher_key, t, LLM_KEY_MAX);
        return 0;
    }
    if (seq(field, "servidor") || lz) {
        char *dst = lz ? c->launcher : c->base[prov];
        if (!clean_value(value, t, LLM_BASE_MAX - 8)) return -1;
        if (!t[0]) { if (lz) dst[0] = 0; else scpy(dst, DEF_BASE[prov], LLM_BASE_MAX); return 0; }
        char full[LLM_BASE_MAX];
        if (starts_ci(t, "http://") || starts_ci(t, "https://")) scpy(full, t, sizeof full);
        else { scpy(full, "http://", sizeof full); scpy(full + 7, t, sizeof full - 7); }       /* "192.168.1.50:8080" */
        uint32_t n = slen(full); while (n && full[n - 1] == '/') full[--n] = 0;
        uint32_t hl; int https; if (!base_host(full, &hl, &https)) return -1;
        for (uint32_t i = 0; full[i]; i++) if (full[i] == ' ' || full[i] == '"' || full[i] == '?' || full[i] == '#') return -1;
        scpy(dst, full, LLM_BASE_MAX);
        return 0;
    }
    if (seq(field, "modelo")) {
        if (!clean_value(value, t, LLM_MODEL_MAX)) return -1;
        for (uint32_t i = 0; t[i]; i++) { char ch = t[i]; if (ch == ' ' || ch == '"' || ch == '\\' || ch == '?' || ch == '#' || ch == '%' || (ch == '.' && t[i + 1] == '.')) return -1; }
        scpy(c->model[prov], t, LLM_MODEL_MAX);
        return 0;
    }
    if (seq(field, "clave")) {
        if (!clean_value(value, t, LLM_KEY_MAX)) return -1;
        for (uint32_t i = 0; t[i]; i++) if (t[i] == ' ' || t[i] == '"' || (unsigned char)t[i] >= 0x7F) return -1;
        scpy(c->key[prov], t, LLM_KEY_MAX);
        return 0;
    }
    return -1;
}
void llm_conf_parse(struct llm_conf *c, const char *text)
{
    llm_conf_defaults(c);
    const char *p = text;
    while (p && *p) {
        const char *l = p; while (*p && *p != '\n') p++;
        uint32_t n = (uint32_t)(p - l); if (*p) p++;
        char line[LLM_KEY_MAX + 40]; if (n >= sizeof line) continue;
        for (uint32_t i = 0; i < n; i++) line[i] = l[i];
        line[n] = 0;
        char *eq = line; while (*eq && *eq != '=') eq++;
        if (line[0] == '#' || !*eq) continue;
        *eq = 0; const char *val = eq + 1;
        if (seq(line, "proveedor")) { int pr = llm_prov_by_name(val); if (pr >= 0) c->prov = pr; continue; }
        if (seq(line, "lanzador") || seq(line, "clave-lanzador")) { (void)llm_conf_set(c, 0, line, val); continue; }      /* SOMA_196 */
        char *dot = line; while (*dot && *dot != '.') dot++;
        if (!*dot) continue;
        *dot = 0;
        (void)llm_conf_set(c, llm_prov_by_name(dot + 1), line, val);                           /* servidor.local=..., clave.claude=... */
    }
}
uint32_t llm_conf_format(const struct llm_conf *c, char *out, uint32_t max)
{
    struct sb b = { out, 0, max, 0 };
    sb_s(&b, "# Modelo de lenguaje de SOMA (parche 195). Lo cambian Ajustes > IA y la orden llm-config. Un agente no puede leer este archivo.\nproveedor=");
    sb_s(&b, llm_prov_name(c->prov)); sb_c(&b, '\n');
    for (int i = 0; i < LLM_NPROV; i++) {
        sb_s(&b, "servidor."); sb_s(&b, NAMES[i]); sb_c(&b, '='); sb_s(&b, c->base[i]); sb_c(&b, '\n');
        sb_s(&b, "modelo."); sb_s(&b, NAMES[i]); sb_c(&b, '='); sb_s(&b, c->model[i]); sb_c(&b, '\n');
        sb_s(&b, "clave."); sb_s(&b, NAMES[i]); sb_c(&b, '='); sb_s(&b, c->key[i]); sb_c(&b, '\n');
    }
    sb_s(&b, "lanzador="); sb_s(&b, c->launcher); sb_s(&b, "\nclave-lanzador="); sb_s(&b, c->launcher_key); sb_c(&b, '\n');   /* SOMA_196 */
    out[b.n] = 0;
    return b.n;
}

const char *llm_proto_strerror(int e)
{
    switch (e) {
    case LLM_P_NOMODEL: return "falta elegir el modelo (Ajustes > IA, o llm-config modelos)";
    case LLM_P_NOKEY: return "falta la clave de la API de ese proveedor (Ajustes > IA, o llm-config clave)";
    case LLM_P_TOOBIG: return "la peticion no cabe";
    case LLM_P_INSECURE: return "no se envia una clave por http sin cifrar a un servidor de fuera de la red local: usa https";
    case LLM_P_BADURL: return "la direccion del servidor no es valida";
    case LLM_P_NOLKEY: return "falta la clave del lanzador (la muestra soma_lanzador.py en el servidor; Ajustes > IA, o llm-config lanzador clave)";
    default: return "error";
    }
}
/* comprobaciones comunes y cabeceras de autenticacion */
static int prep(const struct llm_conf *c, struct llm_http *h, int need_model)
{
    int p = c->prov; uint32_t hl; int https;
    if (p < 0 || p >= LLM_NPROV) return LLM_P_BADURL;
    if (!base_host(c->base[p], &hl, &https)) return LLM_P_BADURL;
    if (need_model && !c->model[p][0]) return LLM_P_NOMODEL;
    if (p != LLM_LOCAL && !c->key[p][0]) return LLM_P_NOKEY;
    if (c->key[p][0] && !https && !llm_base_is_private(c->base[p])) return LLM_P_INSECURE;
    struct sb b = { h->headers, 0, sizeof h->headers, 0 };
    if (c->key[p][0]) {
        if (p == LLM_CLAUDE) { sb_s(&b, "x-api-key: "); sb_s(&b, c->key[p]); sb_s(&b, "\r\nanthropic-version: 2023-06-01\r\n"); }
        else if (p == LLM_GEMINI) { sb_s(&b, "x-goog-api-key: "); sb_s(&b, c->key[p]); sb_s(&b, "\r\n"); }
        else { sb_s(&b, "Authorization: Bearer "); sb_s(&b, c->key[p]); sb_s(&b, "\r\n"); }
    }
    h->headers[b.n] = 0;
    return b.over ? LLM_P_TOOBIG : LLM_P_OK;
}
uint32_t llm_proto_max_tokens = 8192;                  /* SOMA_260_LLM_TAM */
static void sb_u(struct sb *b, uint32_t v);
int llm_proto_chat(const struct llm_conf *c, const char *prompt, int json_mode, char *body, uint32_t max, struct llm_http *h)
{
    int r = prep(c, h, 1); if (r != LLM_P_OK) return r;
    int p = c->prov;
    struct sb u = { h->url, 0, sizeof h->url, 0 }, b = { body, 0, max, 0 };
    h->method = "POST";
    sb_s(&u, c->base[p]);
    if (p == LLM_CLAUDE) {
        sb_s(&u, "/v1/messages");
        sb_s(&b, "{\"model\":\""); sb_json(&b, c->model[p]); sb_s(&b, "\",\"max_tokens\":"); sb_u(&b, llm_proto_max_tokens); sb_s(&b, ",\"system\":\""); sb_json(&b, SYS_PROMPT); if (json_mode) sb_json(&b, SYS_JSON);
        sb_s(&b, "\",\"messages\":[{\"role\":\"user\",\"content\":\""); sb_json(&b, prompt); sb_s(&b, "\"}]}");
    } else if (p == LLM_GEMINI) {
        sb_s(&u, "/v1beta/models/"); sb_s(&u, starts(c->model[p], "models/") ? c->model[p] + 7 : c->model[p]); sb_s(&u, ":generateContent");
        sb_s(&b, "{\"systemInstruction\":{\"parts\":[{\"text\":\""); sb_json(&b, SYS_PROMPT); if (json_mode) sb_json(&b, SYS_JSON);
        sb_s(&b, "\"}]},\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\""); sb_json(&b, prompt);
        sb_s(&b, "\"}]}],\"generationConfig\":{\"maxOutputTokens\":"); sb_u(&b, llm_proto_max_tokens); if (json_mode) sb_s(&b, ",\"responseMimeType\":\"application/json\""); sb_s(&b, "}}");
    } else {
        sb_s(&u, "/v1/chat/completions");
        sb_s(&b, "{\"model\":\""); sb_json(&b, c->model[p]); sb_s(&b, "\",\"messages\":[{\"role\":\"system\",\"content\":\""); sb_json(&b, SYS_PROMPT); if (json_mode && p == LLM_OPENAI) sb_json(&b, SYS_JSON);
        sb_s(&b, "\"},{\"role\":\"user\",\"content\":\""); sb_json(&b, prompt); sb_s(&b, "\"}]");
        if (p == LLM_OPENAI) { if (json_mode) sb_s(&b, ",\"response_format\":{\"type\":\"json_object\"}"); sb_s(&b, ",\"max_completion_tokens\":"); sb_u(&b, llm_proto_max_tokens); sb_c(&b, '}'); }
        else { if (json_mode) sb_s(&b, LOCAL_JSON_FMT); sb_s(&b, ",\"temperature\":0.2,\"max_tokens\":"); sb_u(&b, llm_proto_max_tokens); sb_c(&b, '}'); }
    }
    h->url[u.n] = 0; body[b.n] = 0;
    if (u.over) return LLM_P_BADURL;
    return b.over ? LLM_P_TOOBIG : (int)b.n;
}
/* SOMA_248_DOC: peticion con indicaciones de sistema propias, imagenes (modelos multimodales) y limite de respuesta. */
static void sb_b64(struct sb *b, const uint8_t *d, uint32_t n)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (b->n + (n + 2) / 3 * 4 + 1 >= b->max) { b->over = 1; return; }
    uint32_t i = 0;
    for (; i + 2 < n; i += 3) { uint32_t v = (uint32_t)d[i] << 16 | (uint32_t)d[i + 1] << 8 | d[i + 2]; b->p[b->n++] = T[v >> 18]; b->p[b->n++] = T[(v >> 12) & 63]; b->p[b->n++] = T[(v >> 6) & 63]; b->p[b->n++] = T[v & 63]; }
    if (i < n) { uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0); b->p[b->n++] = T[v >> 18]; b->p[b->n++] = T[(v >> 12) & 63]; b->p[b->n++] = i + 1 < n ? T[(v >> 6) & 63] : '='; b->p[b->n++] = '='; }
}
static void sb_u(struct sb *b, uint32_t v) { char t[12]; int k = 0; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 11); while (k) sb_c(b, t[--k]); }
uint32_t llm_proto_chat_ex_size(const char *system, const char *prompt, const struct llm_img *img, int nimg)
{
    struct llm_msg m = { LLM_ROL_USER, prompt ? prompt : "" };
    return llm_proto_chat_hist_size(system, &m, 1, img, nimg);
}
int llm_proto_chat_ex(const struct llm_conf *c, const char *system, const char *prompt, const struct llm_img *img, int nimg, int max_tokens,
                      char *body, uint32_t max, struct llm_http *h)
{
    struct llm_msg m = { LLM_ROL_USER, prompt ? prompt : "" };
    return llm_proto_chat_hist(c, system, &m, 1, img, nimg, max_tokens, body, max, h);
}
/* SOMA_285_CHAT: conversacion de varios turnos (la aplicacion de chat). Los mensajes seguidos del mismo papel se juntan con una linea en
 * blanco (Claude y Gemini exigen que se alternen); los vacios se saltan; las imagenes van con el ultimo mensaje del usuario. */
uint32_t llm_proto_chat_hist_size(const char *system, const struct llm_msg *m, int nm, const struct llm_img *img, int nimg)
{
    uint32_t n = 4096; const char *s;
    for (s = system ? system : SYS_PROMPT; *s; s++) n += 2;          /* el escape JSON puede duplicar */
    for (int i = 0; i < nm; i++) { n += 64; for (s = m[i].text ? m[i].text : ""; *s; s++) n += 2; }
    for (int i = 0; i < nimg; i++) n += (img[i].len + 2) / 3 * 4 + 256;
    return n;
}
static int hist_last_user(const struct llm_msg *m, int nm)
{
    for (int i = nm - 1; i >= 0; i--) if (m[i].rol == LLM_ROL_USER && m[i].text && m[i].text[0]) return i;
    return -1;
}
int llm_proto_chat_hist(const struct llm_conf *c, const char *system, const struct llm_msg *m, int nm, const struct llm_img *img, int nimg,
                        int max_tokens, char *body, uint32_t max, struct llm_http *h)
{
    int r = prep(c, h, 1); if (r != LLM_P_OK) return r;
    int p = c->prov;
    if (!system || !system[0]) system = SYS_PROMPT;
    if (max_tokens <= 0) max_tokens = 8192;
    int lu = hist_last_user(m, nm);
    if (lu < 0) { static const struct llm_msg vacio = { LLM_ROL_USER, "" }; m = &vacio; nm = 1; lu = 0; }
    struct sb u = { h->url, 0, sizeof h->url, 0 }, b = { body, 0, max, 0 };
    h->method = "POST";
    sb_s(&u, c->base[p]);
    /* grupos: mensajes seguidos del mismo papel; el grupo que contiene lu lleva las imagenes */
    int i = 0, first = 1;
    if (p == LLM_CLAUDE) {
        sb_s(&u, "/v1/messages");
        sb_s(&b, "{\"model\":\""); sb_json(&b, c->model[p]); sb_s(&b, "\",\"max_tokens\":"); sb_u(&b, (uint32_t)max_tokens); sb_s(&b, ",\"system\":\""); sb_json(&b, system);
        sb_s(&b, "\",\"messages\":[");
    } else if (p == LLM_GEMINI) {
        sb_s(&u, "/v1beta/models/"); sb_s(&u, starts(c->model[p], "models/") ? c->model[p] + 7 : c->model[p]); sb_s(&u, ":generateContent");
        sb_s(&b, "{\"systemInstruction\":{\"parts\":[{\"text\":\""); sb_json(&b, system); sb_s(&b, "\"}]},\"contents\":[");
    } else {
        sb_s(&u, "/v1/chat/completions");
        sb_s(&b, "{\"model\":\""); sb_json(&b, c->model[p]); sb_s(&b, "\",\"messages\":[{\"role\":\"system\",\"content\":\""); sb_json(&b, system);
        sb_s(&b, "\"}");
        first = 0;
    }
    while (i < nm) {
        int rol = m[i].rol == LLM_ROL_ASSISTANT ? LLM_ROL_ASSISTANT : LLM_ROL_USER;
        int j = i, has_lu = 0, any = 0;
        while (j < nm && (m[j].rol == LLM_ROL_ASSISTANT ? LLM_ROL_ASSISTANT : LLM_ROL_USER) == rol) { if (j == lu) has_lu = 1; if (m[j].text && m[j].text[0]) any = 1; j++; }
        if (!any && !has_lu) { i = j; continue; }
        if (!first) sb_c(&b, ',');
        first = 0;
        int ni = has_lu ? nimg : 0;
        if (p == LLM_CLAUDE) {
            sb_s(&b, rol == LLM_ROL_USER ? "{\"role\":\"user\",\"content\":[" : "{\"role\":\"assistant\",\"content\":[");
            for (int k = 0; k < ni; k++) { sb_s(&b, "{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\""); sb_s(&b, img[k].mime ? img[k].mime : "image/png"); sb_s(&b, "\",\"data\":\""); sb_b64(&b, img[k].data, img[k].len); sb_s(&b, "\"}},"); }
            sb_s(&b, "{\"type\":\"text\",\"text\":\"");
        } else if (p == LLM_GEMINI) {
            sb_s(&b, rol == LLM_ROL_USER ? "{\"role\":\"user\",\"parts\":[" : "{\"role\":\"model\",\"parts\":[");
            for (int k = 0; k < ni; k++) { sb_s(&b, "{\"inline_data\":{\"mime_type\":\""); sb_s(&b, img[k].mime ? img[k].mime : "image/png"); sb_s(&b, "\",\"data\":\""); sb_b64(&b, img[k].data, img[k].len); sb_s(&b, "\"}},"); }
            sb_s(&b, "{\"text\":\"");
        } else {
            sb_s(&b, rol == LLM_ROL_USER ? "{\"role\":\"user\",\"content\":" : "{\"role\":\"assistant\",\"content\":");
            if (ni > 0) sb_s(&b, "[{\"type\":\"text\",\"text\":\""); else sb_c(&b, '"');
        }
        int sep = 0;
        for (int k = i; k < j; k++) {
            if (!m[k].text || !m[k].text[0]) continue;
            if (sep) sb_json(&b, "\n\n");
            sb_json(&b, m[k].text); sep = 1;
        }
        if (p == LLM_CLAUDE || p == LLM_GEMINI) sb_s(&b, "\"}]}");
        else if (ni > 0) {
            sb_s(&b, "\"}");
            for (int k = 0; k < ni; k++) { sb_s(&b, ",{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:"); sb_s(&b, img[k].mime ? img[k].mime : "image/png"); sb_s(&b, ";base64,"); sb_b64(&b, img[k].data, img[k].len); sb_s(&b, "\"}}"); }
            sb_s(&b, "]}");
        } else sb_s(&b, "\"}");
        i = j;
    }
    if (p == LLM_CLAUDE) sb_s(&b, "]}");
    else if (p == LLM_GEMINI) { sb_s(&b, "],\"generationConfig\":{\"maxOutputTokens\":"); sb_u(&b, (uint32_t)max_tokens); sb_s(&b, "}}"); }
    else {
        sb_c(&b, ']');
        if (p == LLM_OPENAI) { sb_s(&b, ",\"max_completion_tokens\":"); sb_u(&b, (uint32_t)max_tokens); sb_c(&b, '}'); }
        else { sb_s(&b, ",\"temperature\":0.3,\"max_tokens\":"); sb_u(&b, (uint32_t)max_tokens); sb_c(&b, '}'); }
    }
    h->url[u.n] = 0; if (max) body[b.n < max ? b.n : max - 1] = 0;
    if (u.over) return LLM_P_BADURL;
    return b.over ? LLM_P_TOOBIG : (int)b.n;
}
/* el proveedor rechazo la peticion por llevar imagenes (modelo sin vision) */
int llm_proto_no_vision(int status, const char *err)
{
    if (status != 400 && status != 415 && status != 422 && status != 500) return 0;
    if (!err) return status == 415;
    static const char *const k[] = { "image", "imagen", "vision", "multimodal", "mmproj", "image_url", "inline_data", "not support", 0 };
    for (int i = 0; k[i]; i++) { const char *a = err; for (; *a; a++) { const char *x = a, *y = k[i]; while (*y && ((*x | 32) == (*y | 32))) { x++; y++; } if (!*y) return 1; } }
    return 0;
}
int llm_proto_models(const struct llm_conf *c, struct llm_http *h)
{
    int r = prep(c, h, 0); if (r != LLM_P_OK) return r;
    struct sb u = { h->url, 0, sizeof h->url, 0 };
    h->method = "GET";
    sb_s(&u, c->base[c->prov]); sb_s(&u, c->prov == LLM_GEMINI ? "/v1beta/models?pageSize=200" : c->prov == LLM_CLAUDE ? "/v1/models?limit=100" : "/v1/models");
    h->url[u.n] = 0;
    return u.over ? LLM_P_BADURL : LLM_P_OK;
}

/* ---------------------------------------------------------------- SOMA_196: lanzador del servidor local */
void llm_launcher_base(const struct llm_conf *c, char *out, uint32_t max)
{
    if (c->launcher[0]) { scpy(out, c->launcher, max); return; }
    uint32_t hl = 0; int https = 0; const char *h = base_host(c->base[LLM_LOCAL], &hl, &https);
    struct sb b = { out, 0, max, 0 };
    sb_s(&b, "http://");
    if (h) for (uint32_t i = 0; i < hl; i++) sb_c(&b, h[i]);
    sb_s(&b, ":8086");
    if (max) out[b.n] = 0;
}
int llm_proto_launcher(const struct llm_conf *c, int what, const char *model, char *body, uint32_t max, struct llm_http *h)
{
    static const char *const PATH[7] = { "/v1/estado", "/v1/modelos", "/v1/lanzar", "/v1/parar",
                                         "/v1/embeddings/estado", "/v1/embeddings/lanzar", "/v1/embeddings/parar" };   /* SOMA_278_EMBEDDINGS */
    char base[LLM_BASE_MAX]; uint32_t hl; int https;
    if (what < 0 || what > LLM_L_EMB_STOP) return LLM_P_BADURL;
    llm_launcher_base(c, base, sizeof base);
    if (!base_host(base, &hl, &https)) return LLM_P_BADURL;
    if (!c->launcher_key[0]) return LLM_P_NOLKEY;
    if (!https && !llm_base_is_private(base)) return LLM_P_INSECURE;
    struct sb hd = { h->headers, 0, sizeof h->headers, 0 };
    sb_s(&hd, "Authorization: Bearer "); sb_s(&hd, c->launcher_key); sb_s(&hd, "\r\n");
    h->headers[hd.n] = 0;
    struct sb u = { h->url, 0, sizeof h->url, 0 };
    sb_s(&u, base); sb_s(&u, PATH[what]); h->url[u.n] = 0;
    int post = what == LLM_L_START || what == LLM_L_STOP || what == LLM_L_EMB_START || what == LLM_L_EMB_STOP;
    h->method = post ? "POST" : "GET";
    if (hd.over || u.over) return LLM_P_TOOBIG;
    if (!post) return 0;
    struct sb b = { body, 0, max, 0 };
    if (what == LLM_L_START) { if (!model || !model[0]) return LLM_P_NOMODEL; sb_s(&b, "{\"modelo\":\""); sb_json(&b, model); sb_s(&b, "\"}"); }
    else sb_s(&b, "{}");
    if (b.over || !max) return LLM_P_TOOBIG;
    body[b.n] = 0;
    return (int)b.n;
}
static const char *json_key(const char *from, const char *key);
static int json_str_after(const char *from, const char *key, char *out, uint32_t max);
int llm_proto_launcher_status(const char *json, char *state, uint32_t smax, char *model, uint32_t mmax, char *detail, uint32_t dmax)
{
    if (smax) state[0] = 0;
    if (mmax) model[0] = 0;
    if (dmax) detail[0] = 0;
    if (!json || !json_str_after(json, "estado", state, smax)) return 0;
    (void)json_str_after(json, "modelo", model, mmax);
    (void)json_str_after(json, "detalle", detail, dmax);
    return 1;
}

/* ---------------------------------------------------------------- lectura de JSON (sin arbol: busca claves y saca cadenas) */
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
/* p apunta a la comilla de apertura de una cadena JSON: la copia sin escapes. Devuelve el puntero tras la comilla de cierre (0 si no cierra). */
static const char *json_string(const char *p, char *out, uint32_t max, uint32_t *len)
{
    struct sb b = { out, 0, max, 0 };
    if (*p != '"') return 0;
    p++;
    while (*p && *p != '"') {
        if (*p != '\\') { sb_c(&b, *p++); continue; }
        p++;
        switch (*p) {
        case 'n': sb_c(&b, '\n'); break; case 't': sb_c(&b, '\t'); break; case 'r': break;
        case 'b': case 'f': break;
        case 'u': {
            uint32_t cp = 0; int ok = 1;
            for (int i = 1; i <= 4; i++) { int v = hexv(p[i]); if (v < 0) { ok = 0; break; } cp = cp * 16 + (uint32_t)v; }
            if (!ok) return 0;
            p += 4;
            if (cp >= 0xD800 && cp < 0xDC00 && p[1] == '\\' && p[2] == 'u') {                 /* par sustituto */
                uint32_t lo = 0; int ok2 = 1; for (int i = 3; i <= 6; i++) { int v = hexv(p[i]); if (v < 0) { ok2 = 0; break; } lo = lo * 16 + (uint32_t)v; }
                if (ok2 && lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
            }
            if (cp < 0x80) { if (cp >= 0x20 || cp == '\n' || cp == '\t') sb_c(&b, (char)cp); }
            else if (cp < 0x800) { sb_c(&b, (char)(0xC0 | (cp >> 6))); sb_c(&b, (char)(0x80 | (cp & 0x3F))); }
            else if (cp < 0x10000) { sb_c(&b, (char)(0xE0 | (cp >> 12))); sb_c(&b, (char)(0x80 | ((cp >> 6) & 0x3F))); sb_c(&b, (char)(0x80 | (cp & 0x3F))); }
            else { sb_c(&b, (char)(0xF0 | (cp >> 18))); sb_c(&b, (char)(0x80 | ((cp >> 12) & 0x3F))); sb_c(&b, (char)(0x80 | ((cp >> 6) & 0x3F))); sb_c(&b, (char)(0x80 | (cp & 0x3F))); }
            break;
        }
        case 0: return 0;
        default: sb_c(&b, *p); break;                                                          /* \" \\ \/ */
        }
        p++;
    }
    if (*p != '"') return 0;
    out[b.n] = 0; if (len) *len = b.n;
    return p + 1;
}
/* Busca "key" seguido de ':' a partir de `from` (saltando las cadenas, para no confundir un valor con una clave). Devuelve el puntero al valor. */
static const char *json_key(const char *from, const char *key)
{
    uint32_t kl = slen(key);
    for (const char *p = from; p && *p; ) {
        if (*p != '"') { p++; continue; }
        const char *s = p + 1, *e = s;
        while (*e && *e != '"') { if (*e == '\\' && e[1]) e++; e++; }
        if (!*e) return 0;
        const char *a = e + 1; while (*a == ' ' || *a == '\n' || *a == '\r' || *a == '\t') a++;
        if (*a == ':') {
            a++; while (*a == ' ' || *a == '\n' || *a == '\r' || *a == '\t') a++;
            if ((uint32_t)(e - s) == kl) { uint32_t i = 0; while (i < kl && s[i] == key[i]) i++; if (i == kl) return a; }
            p = a;                                                                              /* era una clave: su valor se examina (puede ser un objeto) */
        } else p = e + 1;                                                                       /* era un valor de cadena */
    }
    return 0;
}
static int json_str_after(const char *from, const char *key, char *out, uint32_t max)
{
    for (const char *v = json_key(from, key); v; v = json_key(v, key)) {
        if (*v == '"') return json_string(v, out, max, 0) != 0;                                 /* null u otro tipo: se sigue buscando */
    }
    return 0;
}
int llm_proto_reply(const struct llm_conf *c, const char *json, char *out, uint32_t max)
{
    if (!json || !out || max < 2) return 0;
    out[0] = 0;
    if (c->prov == LLM_CLAUDE) {                           /* {"content":[{"type":"text","text":"..."}]}: el primer bloque de tipo text */
        const char *ct = json_key(json, "content");
        for (const char *t = ct ? json_key(ct, "type") : 0; t; t = json_key(t, "type")) {
            char ty[16]; const char *after = *t == '"' ? json_string(t, ty, sizeof ty, 0) : 0;
            if (after && seq(ty, "text")) return json_str_after(after, "text", out, max);
            if (!after) break;
        }
        return 0;
    }
    if (c->prov == LLM_GEMINI) {                           /* {"candidates":[{"content":{"parts":[{"text":"..."}]}}]} */
        const char *cd = json_key(json, "candidates"); const char *pt = cd ? json_key(cd, "parts") : 0;
        return pt ? json_str_after(pt, "text", out, max) : 0;
    }
    const char *ch = json_key(json, "choices"); const char *ms = ch ? json_key(ch, "message") : 0;   /* {"choices":[{"message":{"content":"..."}}]} */
    return ms ? json_str_after(ms, "content", out, max) : 0;
}
int llm_proto_error(const char *json, char *out, uint32_t max)
{
    if (!json || !out || max < 2) return 0;
    out[0] = 0;
    const char *e = json_key(json, "error");
    if (!e) return 0;
    if (*e == '"') return json_string(e, out, max, 0) != 0;                                      /* {"error":"texto"} */
    return json_str_after(e, "message", out, max);
}
int llm_proto_model_list(const struct llm_conf *c, const char *json, char *out, uint32_t max)
{
    struct sb b = { out, 0, max, 0 }; int n = 0;
    const char *key = c->prov == LLM_GEMINI ? "name" : "id";
    const char *start = json ? json_key(json, c->prov == LLM_GEMINI ? "models" : "data") : 0;
    for (const char *v = start ? json_key(start, key) : 0; v; v = json_key(v, key)) {
        char name[LLM_MODEL_MAX + 8]; const char *after = *v == '"' ? json_string(v, name, sizeof name, 0) : 0;
        if (!after) continue;
        const char *nm = starts(name, "models/") ? name + 7 : name;
        if (!*nm || b.n + slen(nm) + 2 >= max) break;
        sb_s(&b, nm); sb_c(&b, '\n'); n++;
        v = after;
    }
    if (max) out[b.n] = 0;
    return n;
}
