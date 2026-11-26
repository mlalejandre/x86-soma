// SOMA_329_IA_LOCAL: motor de IA local en ring 3 (llama.cpp + ggml, AVX2). Un proceso que vive mientras se usa:
//   - embeddings para la memoria (p. ej. bge-m3) sin servidor externo;
//   - chat con modelos pequenos (p. ej. LFM2.5 350M) sin servidor externo.
// El nucleo (src/arch/x86_64/ia_sys.c) le deja las ordenes en la region compartida IA_SH_VA (src/core/ia_abi.h) y lee las
// respuestas: texto JSON como el de un servidor compatible con OpenAI, asi el resto del sistema no cambia. Toda la coma flotante
// esta aqui, en ring 3; el nucleo no la usa.
//
// Uso en SOMA: ia.elf (sin argumentos; el monton y la region compartida los pone el nucleo).
// Uso en el anfitrion (IA_HOST, pruebas): ia_host -m modelo.gguf -e "texto" | -c '{"messages":[...]}' | -k "texto"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "llama.h"
#include "../../core/ia_abi.h"

extern "C" {
void voz_heap_init(void * base, size_t size);
long voz_uptime_ms(void);
void voz_flush(void);
void voz_exit(int code) __attribute__((noreturn));
int  voz_threads_free(void);
long ia_sleep_ms(long ms);
extern int voz_quiet_stderr;
}

static uint64_t ahora() { return (uint64_t)voz_uptime_ms(); }

// ------------------------------------------------------------------ JSON minimo (lo justo para /v1/chat/completions)
struct jmsg { std::string rol, texto; };
static const char * jws(const char * p) { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++; return p; }
static void utf8_put(std::string & o, uint32_t c)
{
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
    else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
    else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 63)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
}
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
// lee una cadena JSON en p (apunta a la comilla); devuelve el puntero de detras o nullptr
static const char * jstr(const char * p, std::string & o)
{
    if (*p != '"') return nullptr;
    p++;
    while (*p && *p != '"') {
        if (*p != '\\') { o += *p++; continue; }
        p++;
        switch (*p) {
            case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break; case 'b': o += '\b'; break;
            case 'f': o += '\f'; break; case '/': o += '/'; break; case '\\': o += '\\'; break; case '"': o += '"'; break;
            case 'u': {
                uint32_t c = 0;
                for (int k = 1; k <= 4; k++) { int v = hexv(p[k]); if (v < 0) return nullptr; c = c * 16 + (uint32_t)v; }
                p += 4;
                if (c >= 0xD800 && c < 0xDC00 && p[1] == '\\' && p[2] == 'u') {
                    uint32_t d = 0; bool ok = true;
                    for (int k = 3; k <= 6; k++) { int v = hexv(p[k]); if (v < 0) { ok = false; break; } d = d * 16 + (uint32_t)v; }
                    if (ok && d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); p += 6; }
                }
                utf8_put(o, c);
                break;
            }
            case 0: return nullptr;
            default: o += *p;
        }
        p++;
    }
    return *p == '"' ? p + 1 : nullptr;
}
// salta un valor JSON cualquiera
static const char * jskip(const char * p)
{
    p = jws(p);
    if (*p == '"') { std::string t; return jstr(p, t); }
    if (*p == '{' || *p == '[') {
        int prof = 0;
        while (*p) {
            if (*p == '"') { std::string t; p = jstr(p, t); if (!p) return nullptr; continue; }
            if (*p == '{' || *p == '[') prof++;
            else if (*p == '}' || *p == ']') { if (--prof == 0) return p + 1; }
            p++;
        }
        return nullptr;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}
// el contenido de un mensaje puede ser una cadena o una lista de partes [{"type":"text","text":"..."}, {"type":"image_url"...}]
static const char * jcontent(const char * p, std::string & o, bool & tiene_imagen)
{
    p = jws(p);
    if (*p == '"') return jstr(p, o);
    if (*p != '[') return jskip(p);
    p++;
    while (true) {
        p = jws(p);
        if (*p == ']') return p + 1;
        if (*p != '{') return nullptr;
        p++;
        std::string tipo, texto;
        while (true) {
            p = jws(p);
            if (*p == '}') { p++; break; }
            std::string k; p = jstr(p, k); if (!p) return nullptr;
            p = jws(p); if (*p != ':') return nullptr; p = jws(p + 1);
            if (k == "type" && *p == '"') p = jstr(p, tipo);
            else if (k == "text" && *p == '"') p = jstr(p, texto);
            else p = jskip(p);
            if (!p) return nullptr;
            p = jws(p); if (*p == ',') p++;
        }
        if (tipo == "text") { if (!o.empty()) o += "\n"; o += texto; }
        else if (tipo == "image_url") tiene_imagen = true;
        p = jws(p); if (*p == ',') p++;
    }
}
struct peticion { std::string prompt; std::vector<jmsg> msgs; int max_tokens = 0; float temperatura = 0.7f; bool imagen = false; bool json = false; };
static bool parse_peticion(const char * s, peticion & r)
{
    const char * p = jws(s);
    if (*p != '{') return false;
    p++;
    while (true) {
        p = jws(p);
        if (*p == '}') return true;
        std::string k; p = jstr(p, k); if (!p) return false;
        p = jws(p); if (*p != ':') return false; p = jws(p + 1);
        if (k == "messages" && *p == '[') {
            p++;
            while (true) {
                p = jws(p);
                if (*p == ']') { p++; break; }
                if (*p != '{') return false;
                p++;
                jmsg m;
                while (true) {
                    p = jws(p);
                    if (*p == '}') { p++; break; }
                    std::string mk; p = jstr(p, mk); if (!p) return false;
                    p = jws(p); if (*p != ':') return false; p = jws(p + 1);
                    if (mk == "role" && *p == '"') p = jstr(p, m.rol);
                    else if (mk == "content") p = jcontent(p, m.texto, r.imagen);
                    else p = jskip(p);
                    if (!p) return false;
                    p = jws(p); if (*p == ',') p++;
                }
                r.msgs.push_back(m);
                p = jws(p); if (*p == ',') p++;
            }
        } else if (k == "prompt" && *p == '"') {      // estilo /v1/completions: texto tal cual, sin plantilla
            p = jstr(p, r.prompt);
        } else if (k == "max_tokens" || k == "max_completion_tokens") {
            r.max_tokens = std::atoi(p); p = jskip(p);
        } else if (k == "temperature") {
            r.temperatura = (float)std::atof(p); p = jskip(p);
        } else if (k == "response_format") {
            const char * q = jskip(p); if (!q) return false;
            if (std::strstr(std::string(p, q - p).c_str(), "json_object")) r.json = true;
            p = q;
        } else p = jskip(p);
        if (!p) return false;
        p = jws(p); if (*p == ',') p++;
    }
}
static void jesc(std::string & o, const std::string & s)
{
    for (unsigned char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else if (c == '\n') o += "\\n"; else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
}

// ------------------------------------------------------------------ modelos cargados
struct cargado { std::string ruta; llama_model * m = nullptr; llama_context * ctx = nullptr; bool emb = false; uint32_t n_ctx = 0; };
static cargado g_emb, g_chat;
static int g_hilos = 1;
static std::string g_err;

static void suelta(cargado & c)
{
    if (c.ctx) llama_free(c.ctx);
    if (c.m) llama_model_free(c.m);
    c = cargado();
}
static void log_silencio(enum ggml_log_level lvl, const char * txt, void *)
{
    // se guarda el error mas explicativo: el de la excepcion ("error loading model: ...") y no el generico que llega despues
    if (lvl != GGML_LOG_LEVEL_ERROR || !txt) return;
    std::string t = txt;
    while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
    if (t.empty()) return;
    bool detalle = t.find("error loading model") != std::string::npos || t.find("failed to") == std::string::npos;
    if (g_err.empty() || detalle) g_err = t;
}
static bool asegura(cargado & c, const char * ruta, bool emb, uint32_t n_ctx, uint32_t & ms)
{
    if (c.m && c.ruta == ruta && (emb || c.n_ctx == n_ctx)) return true;
    suelta(c);
    uint64_t t0 = ahora();
    llama_model_params mp = llama_model_default_params();
    mp.load_mode = LLAMA_LOAD_MODE_NONE;          // sin mmap: el modelo se lee al monton
    g_err.clear();
    c.m = llama_model_load_from_file(ruta, mp);
    if (!c.m) { if (g_err.empty()) g_err = std::string("no se pudo cargar el modelo ") + ruta; return false; }
    llama_context_params cp = llama_context_default_params();
    cp.n_threads = g_hilos; cp.n_threads_batch = g_hilos;
    if (emb) {
        cp.embeddings = true;
        cp.n_ctx = 8192; cp.n_batch = 8192; cp.n_ubatch = 8192;
        uint32_t tr = (uint32_t)llama_model_n_ctx_train(c.m);
        if (tr && tr < cp.n_ctx) { cp.n_ctx = tr; cp.n_batch = tr; cp.n_ubatch = tr; }
    } else {
        cp.n_ctx = n_ctx; cp.n_batch = 512; cp.n_ubatch = 512;
    }
    c.ctx = llama_init_from_model(c.m, cp);
    if (!c.ctx) { suelta(c); if (g_err.empty()) g_err = "no se pudo preparar el contexto del modelo"; return false; }
    c.ruta = ruta; c.emb = emb; c.n_ctx = cp.n_ctx;
    ms = (uint32_t)(ahora() - t0);
    return true;
}
static std::vector<llama_token> tokeniza(const llama_vocab * v, const std::string & s, bool especiales, bool parse_especiales)
{
    std::vector<llama_token> t(s.size() + 8);
    int n = llama_tokenize(v, s.c_str(), (int32_t)s.size(), t.data(), (int32_t)t.size(), especiales, parse_especiales);
    if (n < 0) { t.resize((size_t)-n); n = llama_tokenize(v, s.c_str(), (int32_t)s.size(), t.data(), (int32_t)t.size(), especiales, parse_especiales); }
    t.resize(n > 0 ? (size_t)n : 0);
    return t;
}

// ------------------------------------------------------------------ ordenes
struct salida { char * buf; uint32_t cap; std::string s; };
static int orden_embed(const char * ruta, const std::string & texto, std::string & out, uint32_t & tin, uint32_t & ms_carga)
{
    if (!asegura(g_emb, ruta, true, 0, ms_carga)) return -1;
    const llama_vocab * v = llama_model_get_vocab(g_emb.m);
    std::vector<llama_token> t = tokeniza(v, texto, true, false);
    if (t.empty()) { g_err = "texto vacio"; return -1; }
    if (t.size() > g_emb.n_ctx) t.resize(g_emb.n_ctx);
    tin = (uint32_t)t.size();
    llama_memory_clear(llama_get_memory(g_emb.ctx), true);
    llama_batch b = llama_batch_init((int32_t)t.size(), 0, 1);
    for (size_t i = 0; i < t.size(); i++) {
        b.token[i] = t[i]; b.pos[i] = (llama_pos)i; b.n_seq_id[i] = 1; b.seq_id[i][0] = 0; b.logits[i] = 1;
    }
    b.n_tokens = (int32_t)t.size();
    int r = llama_model_has_encoder(g_emb.m) && !llama_model_has_decoder(g_emb.m) ? llama_encode(g_emb.ctx, b) : llama_decode(g_emb.ctx, b);
    if (r != 0) { llama_batch_free(b); g_err = "fallo al calcular el embedding"; return -1; }
    const int n = llama_model_n_embd(g_emb.m);
    const float * e = nullptr;
    if (llama_pooling_type(g_emb.ctx) == LLAMA_POOLING_TYPE_NONE) e = llama_get_embeddings_ith(g_emb.ctx, (int32_t)t.size() - 1);
    else e = llama_get_embeddings_seq(g_emb.ctx, 0);
    if (!e) { llama_batch_free(b); g_err = "el modelo no da embeddings"; return -1; }
    double s2 = 0; for (int i = 0; i < n; i++) s2 += (double)e[i] * e[i];
    double inv = s2 > 0 ? 1.0 / __builtin_sqrt(s2) : 0;
    out = "{\"object\":\"list\",\"data\":[{\"object\":\"embedding\",\"index\":0,\"embedding\":[";
    char num[32];
    for (int i = 0; i < n; i++) { std::snprintf(num, sizeof num, i ? ",%.6f" : "%.6f", (double)e[i] * inv); out += num; }
    out += "]}],\"usage\":{\"prompt_tokens\":"; out += std::to_string(t.size()); out += "}}";
    llama_batch_free(b);
    return 0;
}

static volatile ia_ctl * g_ctl;           // en SOMA; nullptr en el anfitrion
static char * g_parcial;

static int orden_chat(const char * ruta, const char * cuerpo, uint32_t n_ctx, std::string & out, uint32_t & tin, uint32_t & tout,
                      uint32_t & ms_carga, volatile uint32_t * cancela)
{
    peticion pq;
    if (!parse_peticion(cuerpo, pq) || (pq.msgs.empty() && pq.prompt.empty())) { g_err = "peticion de chat no valida"; return -1; }
    if (pq.imagen) { g_err = "el modelo local no admite imagenes"; return -2; }
    if (!asegura(g_chat, ruta, false, n_ctx, ms_carga)) return -1;
    const llama_vocab * v = llama_model_get_vocab(g_chat.m);
    // plantilla del modelo (o ChatML si no trae)
    std::string prompt;
    if (!pq.prompt.empty()) prompt = pq.prompt;
    else {
    std::vector<llama_chat_message> cm;
    for (auto & m : pq.msgs) cm.push_back({ m.rol.c_str(), m.texto.c_str() });
    const char * tmpl = llama_model_chat_template(g_chat.m, nullptr);
    std::vector<char> buf(4096);
    int n = llama_chat_apply_template(tmpl, cm.data(), cm.size(), true, buf.data(), (int32_t)buf.size());
    if (n < 0 && tmpl) { tmpl = "chatml"; n = llama_chat_apply_template(tmpl, cm.data(), cm.size(), true, buf.data(), (int32_t)buf.size()); }
    if (n < 0) { g_err = "no se pudo aplicar la plantilla de chat"; return -1; }
    if (n > (int)buf.size()) { buf.resize((size_t)n + 1); n = llama_chat_apply_template(tmpl, cm.data(), cm.size(), true, buf.data(), (int32_t)buf.size()); }
    prompt.assign(buf.data(), (size_t)n);
    }
    std::vector<llama_token> t = tokeniza(v, prompt, true, true);
    int max_new = pq.max_tokens > 0 ? pq.max_tokens : 1024;
    if ((int)t.size() + 8 >= (int)g_chat.n_ctx) {    // no cabe: se quitan tokens del principio de la conversacion (se conserva el final)
        size_t quita = t.size() + 8 + 64 - g_chat.n_ctx;
        if (quita >= t.size()) { g_err = "la conversacion no cabe en el contexto del modelo"; return -1; }
        t.erase(t.begin() + 1, t.begin() + 1 + (long)quita);
    }
    if ((int)t.size() + max_new > (int)g_chat.n_ctx) max_new = (int)g_chat.n_ctx - (int)t.size();
    tin = (uint32_t)t.size();
    llama_memory_clear(llama_get_memory(g_chat.ctx), true);
    // la entrada, por trozos del tamano del lote
    const int nb = 512;
    for (size_t i = 0; i < t.size(); i += nb) {
        int k = (int)(t.size() - i < (size_t)nb ? t.size() - i : (size_t)nb);
        llama_batch b = llama_batch_get_one(t.data() + i, k);
        if (llama_decode(g_chat.ctx, b) != 0) { g_err = "fallo al procesar la entrada"; return -1; }
        if (cancela && *cancela) { g_err = "cancelado"; return -3; }
    }
    llama_sampler * sm = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (pq.temperatura <= 0.01f) llama_sampler_chain_add(sm, llama_sampler_init_greedy());
    else {
        llama_sampler_chain_add(sm, llama_sampler_init_top_k(40));
        llama_sampler_chain_add(sm, llama_sampler_init_top_p(0.95f, 1));
        llama_sampler_chain_add(sm, llama_sampler_init_temp(pq.temperatura));
        llama_sampler_chain_add(sm, llama_sampler_init_dist((uint32_t)ahora()));
    }
    std::string texto;
    const char * fin = "length";
    tout = 0;
    for (int i = 0; i < max_new; i++) {
        if (cancela && *cancela) { fin = "cancel"; break; }
        llama_token tk = llama_sampler_sample(sm, g_chat.ctx, -1);
        if (llama_vocab_is_eog(v, tk)) { fin = "stop"; break; }
        char pz[256];
        int m = llama_token_to_piece(v, tk, pz, sizeof pz, 0, false);
        if (m > 0) texto.append(pz, (size_t)m);
        tout++;
        if (g_ctl && g_parcial) {                     // el nucleo puede ir ensenando la respuesta mientras se genera
            uint32_t c = texto.size() < IA_PARCIAL_MAX - 1 ? (uint32_t)texto.size() : IA_PARCIAL_MAX - 1;
            std::memcpy(g_parcial, texto.data(), c);
            __atomic_store_n(&g_ctl->parcial_len, c, __ATOMIC_RELEASE);
            __atomic_store_n(&g_ctl->tokens_salida, tout, __ATOMIC_RELEASE);
        }
        llama_batch b = llama_batch_get_one(&tk, 1);
        if (llama_decode(g_chat.ctx, b) != 0) { fin = "error"; break; }
    }
    llama_sampler_free(sm);
    if (cancela && *cancela) { g_err = "cancelado"; return -3; }
    out = "{\"id\":\"soma-local\",\"object\":\"chat.completion\",\"model\":\"";
    jesc(out, ruta);
    out += "\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"";
    jesc(out, texto);
    out += "\"},\"finish_reason\":\""; out += fin;
    out += "\"}],\"usage\":{\"prompt_tokens\":"; out += std::to_string(tin);
    out += ",\"completion_tokens\":"; out += std::to_string(tout);
    out += ",\"total_tokens\":"; out += std::to_string(tin + tout); out += "}}";
    return 0;
}
static int orden_tokeniza(const char * ruta, const std::string & texto, std::string & out, uint32_t & ms_carga)
{
    // solo el vocabulario: un modelo de vocabulario (ggml-vocab-*.gguf) tambien sirve
    static llama_model * vm; static std::string vr;
    if (!vm || vr != ruta) {
        if (vm) llama_model_free(vm);
        llama_model_params mp = llama_model_default_params(); mp.vocab_only = true; mp.load_mode = LLAMA_LOAD_MODE_NONE;
        uint64_t t0 = ahora();
        vm = llama_model_load_from_file(ruta, mp);
        ms_carga = (uint32_t)(ahora() - t0);
        if (!vm) { vr.clear(); if (g_err.empty()) g_err = "no se pudo cargar el vocabulario"; return -1; }
        vr = ruta;
    }
    std::vector<llama_token> t = tokeniza(llama_model_get_vocab(vm), texto, false, false);
    out.clear();
    for (size_t i = 0; i < t.size(); i++) { if (i) out += ' '; out += std::to_string(t[i]); }
    return 0;
}

// huella FNV-1a de 64 bits del archivo leido con fopen/fseek/fread por trozos de tamanos variados (como lee llama.cpp):
// comprueba la lectura del almacen desde ring 3
static int orden_huella(const char * ruta, std::string & out)
{
    FILE * f = std::fopen(ruta, "rb");
    if (!f) { g_err = std::string("no se puede abrir ") + ruta; return -1; }
    std::fseek(f, 0, SEEK_END); long tam = std::ftell(f);
    uint64_t h = 1469598103934665603ULL; long pos = 0; size_t trozo = 1000;
    std::vector<unsigned char> b(1 << 20);
    while (pos < tam) {
        size_t n = (size_t)(tam - pos) < trozo ? (size_t)(tam - pos) : trozo;
        if (std::fseek(f, pos, SEEK_SET) != 0 || std::fread(b.data(), n, 1, f) != 1) { std::fclose(f); g_err = "fallo de lectura"; return -1; }
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
        pos += (long)n; trozo = trozo * 3 % ((1u << 20) - 7) + 1;
    }
    std::fclose(f);
    char t[64]; std::snprintf(t, sizeof t, "%016llx %ld", (unsigned long long)h, tam);
    out = t;
    return 0;
}

static int g_hilos_pedidos;
static void elige_hilos(uint32_t pedidos)
{
    int h = (int)pedidos;
    if (h <= 0) { int f = voz_threads_free(); if (f < 0) f = 0; h = 1 + (f < 7 ? f : 7); }
    if (h > 8) h = 8;
    if (h != g_hilos_pedidos) {
        g_hilos_pedidos = h; g_hilos = h;
        if (g_emb.ctx) llama_set_n_threads(g_emb.ctx, h, h);
        if (g_chat.ctx) llama_set_n_threads(g_chat.ctx, h, h);
    }
}

#ifndef IA_HOST
// ------------------------------------------------------------------ servicio (SOMA)
int ia_main(int, char **)
{
    volatile ia_ctl * c = (volatile ia_ctl *)IA_SH_VA;
    g_ctl = c;
    if (c->magic != IA_MAGIC || c->version != IA_VERSION) return 2;
    voz_quiet_stderr = 1;
    llama_log_set(log_silencio, nullptr);
    llama_backend_init();
    char * in = (char *)(IA_SH_VA + IA_IN_OFF);
    char * outb = (char *)(IA_SH_VA + IA_OUT_OFF);
    g_parcial = (char *)(IA_SH_VA + IA_PARCIAL_OFF);
    uint32_t visto = c->cmd_seq;
    c->estado = IA_E_LISTO;
    uint64_t libre_desde = ahora();
    for (;;) {
        uint32_t s = __atomic_load_n(&c->cmd_seq, __ATOMIC_ACQUIRE);
        // en espera: a menudo justo despues de una orden (suelen venir seguidas), y despues cada 20 ms (pocas llamadas al sistema)
        if (s == visto) { ia_sleep_ms(ahora() - libre_desde < 500 ? 1 : 20); continue; }
        visto = s;
        c->estado = IA_E_OCUPADO;
        uint32_t cmd = c->cmd, n = c->in_len;
        if (n > IA_IN_MAX - 1) n = IA_IN_MAX - 1;
        std::string entrada(in, n);
        char ruta[sizeof c->modelo];
        std::memcpy(ruta, (const char *)c->modelo, sizeof ruta); ruta[sizeof ruta - 1] = 0;
        elige_hilos(c->hilos);
        c->hilos_usados = (uint32_t)g_hilos;
        g_err.clear();
        std::string out;
        uint32_t tin = 0, tout = 0, msc = 0;
        uint64_t t0 = ahora();
        c->parcial_len = 0; c->tokens_salida = 0; c->out_len = 0;
        int r = 0;
        if (cmd == IA_CMD_EMBED) r = orden_embed(ruta, entrada, out, tin, msc);
        else if (cmd == IA_CMD_CHAT) r = orden_chat(ruta, entrada.c_str(), c->contexto ? c->contexto : 4096, out, tin, tout, msc, &c->cancela);
        else if (cmd == IA_CMD_TOKENIZA) r = orden_tokeniza(ruta, entrada, out, msc);
        else if (cmd == IA_CMD_HUELLA) r = orden_huella(ruta, out);
        else if (cmd == IA_CMD_DESCARGA) { suelta(g_emb); suelta(g_chat); }
        else if (cmd == IA_CMD_SALIR) { c->cmd_res = 0; c->estado = IA_E_FIN; __atomic_store_n(&c->cmd_hecho, s, __ATOMIC_RELEASE); break; }
        else { r = -1; g_err = "orden desconocida"; }
        if (r == 0 && out.size() > IA_OUT_MAX - IA_PARCIAL_MAX) { r = -1; g_err = "respuesta demasiado grande"; }
        if (r == 0) { std::memcpy(outb, out.data(), out.size()); c->out_len = (uint32_t)out.size(); }
        else {
            size_t k = g_err.size() < sizeof c->err - 1 ? g_err.size() : sizeof c->err - 1;
            std::memcpy((char *)c->err, g_err.data(), k); ((char *)c->err)[k] = 0;
        }
        c->tokens_entrada = tin; c->tokens_salida = tout; c->ms_carga = msc; c->ms_proceso = (uint32_t)(ahora() - t0);
        c->cmd_res = r;
        c->estado = IA_E_LISTO;
        __atomic_store_n(&c->cmd_hecho, s, __ATOMIC_RELEASE);
        libre_desde = ahora();
    }
    suelta(g_emb); suelta(g_chat);
    llama_backend_free();
    return 0;
}
#else
// ------------------------------------------------------------------ anfitrion (pruebas): misma logica, ordenes por argumentos
int ia_main(int argc, char ** argv)
{
    const char * m = nullptr, * e = nullptr, * ch = nullptr, * k = nullptr; int hilos = 0;
    for (int i = 1; i + 1 < argc; i++) {
        if (!std::strcmp(argv[i], "-m")) m = argv[++i];
        else if (!std::strcmp(argv[i], "-e")) e = argv[++i];
        else if (!std::strcmp(argv[i], "-c")) ch = argv[++i];
        else if (!std::strcmp(argv[i], "-k")) k = argv[++i];
        else if (!std::strcmp(argv[i], "-K") || !std::strcmp(argv[i], "-C") || !std::strcmp(argv[i], "-E")) {   // el texto en un archivo
            char o = argv[i][1]; static std::string txt; txt.clear();
            FILE * f = std::fopen(argv[++i], "r");
            if (f) { char b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) txt.append(b, n); std::fclose(f); }
            if (o == 'K') k = txt.c_str(); else if (o == 'C') ch = txt.c_str(); else e = txt.c_str();
        }
        else if (!std::strcmp(argv[i], "-H")) ++i;
        else if (!std::strcmp(argv[i], "-t")) hilos = std::atoi(argv[++i]);
    }
    llama_log_set(log_silencio, nullptr);
    llama_backend_init();
    elige_hilos((uint32_t)hilos);
    if (!m) { std::printf("uso: ia_host -m modelo.gguf (-e texto | -c json | -k texto)\n"); return 2; }
    std::string out; uint32_t a = 0, b = 0, ms = 0; int r = -1;
    if (e) r = orden_embed(m, e, out, a, ms);
    else if (ch) r = orden_chat(m, ch, 4096, out, a, b, ms, nullptr);
    else if (k) r = orden_tokeniza(m, k, out, ms);
    else r = orden_huella(m, out);
    if (r) std::printf("ERROR %d: %s\n", r, g_err.c_str()); else std::printf("%s\n", out.c_str());
    voz_flush();
    return r ? 1 : 0;
}
#endif
