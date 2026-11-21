// SOMA_247_HABLA: texto a voz (orden "habla", boton "Leer en voz alta" de Notas). Va dentro de voz.elf:
//
//   voz.elf --habla -H <MiB> -m /voces/voz.onnx [-t hilos] [-s velocidad] [-S hablante] [-F] [-v] texto.txt
//
// Lee la voz de Piper (.onnx y su .onnx.json) y el texto (UTF-8) del almacen, lo parte en frases, pasa cada frase a fonemas
// (habla_g2p_es.h, o -F: el archivo ya trae fonemas IPA, una frase por linea), sintetiza (habla_vits.h) y la manda a la
// tarjeta de sonido (llamadas de audio del nucleo) mientras calcula la siguiente. Las lineas "[habla] ..." son informacion.
#include "habla_onnx.h"
#include "habla_vits.h"
#include "habla_g2p_es.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>

extern "C" {
uint64_t voz_uptime_ms(void);
void voz_flush(void);
int voz_threads_free(void);
long voz_audio_open(long rate);
long voz_audio_write(const void * buf, long len);
long voz_audio_close(long wait);
extern int voz_pretouch;
}

namespace {

bool read_file(const char * path, std::vector<uint8_t> & out)
{
    FILE * f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = n ? std::fread(out.data(), 1, (size_t)n, f) : 0;
    std::fclose(f);
    return got == (size_t)n;
}

// ---------------------------------------------------------------- JSON minimo (solo lo que trae el .onnx.json de Piper)
struct J {
    const char * p, * e;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
    bool str(std::string & o) {
        ws(); if (p >= e || *p != '"') return false; p++;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                p++;
                if (*p == 'u' && p + 4 < e) {
                    unsigned v = 0; for (int i = 1; i <= 4; i++) { char c = p[i]; v = v * 16 + (unsigned)(c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'); }
                    p += 5;
                    if (v >= 0xD800 && v < 0xDC00 && p + 6 <= e && p[0] == '\\' && p[1] == 'u') {   // par sustituto
                        unsigned w = 0; for (int i = 2; i <= 5; i++) { char c = p[i]; w = w * 16 + (unsigned)(c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'); }
                        p += 6; v = 0x10000 + ((v - 0xD800) << 10) + (w - 0xDC00);
                    }
                    if (v < 0x10000) g2p_es::u8_put(o, v); else { o += (char)(0xF0 | v >> 18); o += (char)(0x80 | ((v >> 12) & 0x3F)); o += (char)(0x80 | ((v >> 6) & 0x3F)); o += (char)(0x80 | (v & 0x3F)); }
                    continue;
                }
                o += *p == 'n' ? '\n' : *p == 't' ? '\t' : *p; p++; continue;
            }
            o += *p++;
        }
        if (p < e) p++;
        return true;
    }
    bool num(double & v) { ws(); char * end = nullptr; v = std::strtod(p, &end); if (end == p) return false; p = end; return true; }
    void skip() {
        ws(); if (p >= e) return;
        if (*p == '"') { std::string s; str(s); return; }
        if (*p == '{' || *p == '[') { char o = *p, c = o == '{' ? '}' : ']'; int d = 0; bool in = false;
            for (; p < e; p++) { if (in) { if (*p == '\\') p++; else if (*p == '"') in = false; continue; } if (*p == '"') in = true; else if (*p == o) d++; else if (*p == c) { if (--d == 0) { p++; return; } } } return; }
        while (p < e && *p != ',' && *p != '}' && *p != ']') p++;
    }
};

struct Config {
    int rate = 22050, num_speakers = 1;
    float noise = 0.667f, length = 1.0f, noise_w = 0.8f;
    std::string voice = "es";
    std::map<std::string, std::vector<int64_t>> ids;
};

void parse_obj(J & j, const std::string & path, Config & c);
void parse_val(J & j, const std::string & path, Config & c)
{
    j.ws(); if (j.p >= j.e) return;
    if (*j.p == '{') { parse_obj(j, path, c); return; }
    if (path.rfind("phoneme_id_map/", 0) == 0 && *j.p == '[') {
        std::vector<int64_t> v; j.p++;
        for (;;) { j.ws(); if (j.p < j.e && *j.p == ']') { j.p++; break; } double d; if (!j.num(d)) { j.skip(); break; } v.push_back((int64_t)d); j.ws(); if (j.p < j.e && *j.p == ',') j.p++; }
        c.ids[path.substr(15)] = v; return;
    }
    if (*j.p == '"') { std::string s; j.str(s); if (path == "espeak/voice") c.voice = s; return; }
    if (*j.p == '[') { j.skip(); return; }
    double d;
    if (j.num(d)) {
        if (path == "audio/sample_rate") c.rate = (int)d;
        else if (path == "inference/noise_scale") c.noise = (float)d;
        else if (path == "inference/length_scale") c.length = (float)d;
        else if (path == "inference/noise_w") c.noise_w = (float)d;
        else if (path == "num_speakers") c.num_speakers = (int)d;
    } else j.skip();
}
void parse_obj(J & j, const std::string & path, Config & c)
{
    j.ws(); if (j.p >= j.e || *j.p != '{') return; j.p++;
    for (;;) {
        j.ws(); if (j.p >= j.e) return;
        if (*j.p == '}') { j.p++; return; }
        std::string k; if (!j.str(k)) return;
        j.ws(); if (j.p < j.e && *j.p == ':') j.p++;
        parse_val(j, path.empty() ? k : path + "/" + k, c);
        j.ws(); if (j.p < j.e && *j.p == ',') j.p++;
    }
}

// el mapa de fonemas por defecto de Piper (por si la voz no trae .onnx.json)
const char * const DEFAULT_MAP[] = {
    "_", "^", "$", " ", "!", "'", "(", ")", ",", "-", ".", ":", ";", "?", "a", "b", "c", "d", "e", "f", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q",
    "r", "s", "t", "u", "v", "w", "x", "y", "z", "æ", "ç", "ð", "ø", "ħ", "ŋ", "œ", "ǀ", "ǁ", "ǂ", "ǃ", "ɐ", "ɑ", "ɒ", "ɓ", "ɔ", "ɕ", "ɖ", "ɗ", "ɘ", "ə",
    "ɚ", "ɛ", "ɜ", "ɞ", "ɟ", "ɠ", "ɡ", "ɢ", "ɣ", "ɤ", "ɥ", "ɦ", "ɧ", "ɨ", "ɪ", "ɫ", "ɬ", "ɭ", "ɮ", "ɯ", "ɰ", "ɱ", "ɲ", "ɳ", "ɴ", "ɵ", "ɶ", "ɸ", "ɹ", "ɺ",
    "ɻ", "ɽ", "ɾ", "ʀ", "ʁ", "ʂ", "ʃ", "ʄ", "ʈ", "ʉ", "ʊ", "ʋ", "ʌ", "ʍ", "ʎ", "ʏ", "ʐ", "ʑ", "ʒ", "ʔ", "ʕ", "ʘ", "ʙ", "ʛ", "ʜ", "ʝ", "ʟ", "ʡ", "ʢ", "ʲ",
    "ˈ", "ˌ", "ː", "ˑ", "˞", "β", "θ", "χ", "ᵻ", "ⱱ", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "\xcc\xa7", "\xcc\x83", "\xcc\xaa", "\xcc\xaf",
    "\xcc\xa9", "ʰ", "ˤ", "ε", "↓", "#", "\"", "↑", "\xcc\xba", "\xcc\xbb", "g", "ʦ", "X", "\xcc\x9d", "\xcc\x8a", "ɝ", "ʷ", nullptr };

std::vector<int64_t> to_ids(const std::string & ph, const Config & c, int * missing)
{
    auto get = [&](const std::string & k) -> const std::vector<int64_t> * { auto it = c.ids.find(k); return it == c.ids.end() ? nullptr : &it->second; };
    std::vector<int64_t> out;
    const std::vector<int64_t> * bos = get("^"), * pad = get("_"), * eos = get("$");
    if (bos) out.insert(out.end(), bos->begin(), bos->end());
    if (pad) out.insert(out.end(), pad->begin(), pad->end());
    size_t i = 0;
    while (i < ph.size()) {
        size_t st = i; g2p_es::u8_next(ph, i);
        const std::vector<int64_t> * v = get(ph.substr(st, i - st));
        if (!v) { if (missing) (*missing)++; continue; }
        out.insert(out.end(), v->begin(), v->end());
        if (pad) out.insert(out.end(), pad->begin(), pad->end());
    }
    if (eos) out.insert(out.end(), eos->begin(), eos->end());
    return out;
}

// ---------------------------------------------------------------- salida de audio (estereo 16 bits; remuestreo si hace falta)
struct Out {
    long rate = 0; double step = 1.0; bool ok = false; double pos = 0;
    bool open(int voice_rate) {
        static const long try_rates[] = { 0, 44100, 48000, 16000 };
        for (long r : try_rates) { long rr = r ? r : voice_rate; if (voz_audio_open(rr) == 0) { rate = rr; step = (double)voice_rate / (double)rr; ok = true; return true; } }
        return false;
    }
    void write(const std::vector<float> & a) {
        if (!ok || a.empty()) return;
        std::vector<int16_t> buf; buf.reserve(8192 * 2);
        auto flush = [&]() { if (!buf.empty()) { voz_audio_write(buf.data(), (long)(buf.size() * 2)); buf.clear(); } };
        // remuestreo lineal desde la frecuencia de la voz a la de la tarjeta (si coinciden, step = 1)
        double p = 0; size_t n = a.size();
        while (p < (double)n) {
            size_t k = (size_t)p; double f = p - (double)k;
            float v = k + 1 < n ? (float)(a[k] * (1.0 - f) + a[k + 1] * f) : a[k];
            int s = (int)(v * 32767.0f); if (s > 32767) s = 32767; if (s < -32768) s = -32768;
            buf.push_back((int16_t)s); buf.push_back((int16_t)s);
            if (buf.size() >= 16384) flush();
            p += step;
        }
        flush();
    }
    void close() { if (ok) voz_audio_close(1); ok = false; }
};

} // namespace

int habla_main(int argc, char ** argv)
{
    const char * model = nullptr, * text_path = nullptr;
    int threads = 0, sid = 0; float speed = 1.0f; bool phon = false, verbose = false;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "-m") && i + 1 < argc) model = argv[++i];
        else if (!std::strcmp(argv[i], "-t") && i + 1 < argc) threads = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "-s") && i + 1 < argc) speed = (float)std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "-S") && i + 1 < argc) sid = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "-H") && i + 1 < argc) ++i;
        else if (!std::strcmp(argv[i], "-F")) phon = true;
        else if (!std::strcmp(argv[i], "-v")) verbose = true;
        else text_path = argv[i];
    }
    if (!model || !text_path) { std::printf("[habla] uso: habla -m voz.onnx [-t hilos] [-s velocidad] texto.txt\n"); return 2; }
    int want = threads > 0 ? threads : 1 + (voz_threads_free() < 3 ? voz_threads_free() : 3);
    if (want > 1) voz_pretouch = 1;                       // los hilos secundarios no pueden provocar fallos de pagina
    uint64_t t0 = voz_uptime_ms();
    std::vector<uint8_t> txt;
    if (!read_file(text_path, txt)) { std::printf("[habla] no se puede leer %s\n", text_path); return 3; }
    std::string text((const char *)txt.data(), txt.size());

    Config cfg;
    {
        std::string jp = std::string(model) + ".json";
        std::vector<uint8_t> jb;
        if (read_file(jp.c_str(), jb)) { J j{ (const char *)jb.data(), (const char *)jb.data() + jb.size() }; parse_obj(j, "", cfg); }
        else std::printf("[habla] aviso: no hay %s; se usan los valores de Piper por defecto\n", jp.c_str());
        if (cfg.ids.empty()) for (int k = 0; DEFAULT_MAP[k]; k++) cfg.ids[DEFAULT_MAP[k]] = std::vector<int64_t>(1, (int64_t)k);
    }
    g2p_es::Opts op;
    op.seseo = op.yeismo = cfg.voice != "es" && cfg.voice.rfind("es", 0) == 0;      // es-419, es-mx...: seseo y yeismo
    if (verbose) std::printf("[habla] voz: %s, %d Hz, espeak %s, %zu fonemas\n", model, cfg.rate, cfg.voice.c_str(), cfg.ids.size());

    habla::Net net;
    {
        std::vector<uint8_t> mb;
        if (!read_file(model, mb)) { std::printf("[habla] no se puede leer la voz %s\n", model); return 4; }
        onnxmin::Model om;
        if (!onnxmin::load(mb.data(), mb.size(), om)) { std::printf("[habla] la voz no es un ONNX valido: %s\n", om.err.c_str()); return 5; }
        std::vector<uint8_t>().swap(mb);
        if (!net.load(om)) { std::printf("[habla] voz no compatible (Piper/VITS): %s\n", net.err.c_str()); return 5; }
    }
    uint64_t t_load = voz_uptime_ms() - t0;

    // frases
    std::vector<std::string> phs;
    if (phon) { size_t a = 0; while (a < text.size()) { size_t b = text.find('\n', a); if (b == std::string::npos) b = text.size(); if (b > a) phs.push_back(text.substr(a, b - a)); a = b + 1; } }
    else for (auto & s : g2p_es::sentences(text)) { std::string p = g2p_es::phonemize_sentence(s, op); if (!p.empty()) phs.push_back(p); }
    if (phs.empty()) { std::printf("[habla] no hay nada que leer\n"); return 6; }

    habla::Pool pool; pool.start(want); habla::g_pool = &pool;
    int used = pool.n;
    Out out;
    if (!out.open(cfg.rate)) { std::printf("[habla] no hay salida de audio (o la esta usando otro programa)\n"); pool.stop(); return 7; }
    habla::Rng rng; rng.s ^= voz_uptime_ms() * 0x9E3779B97F4A7C15ULL;
    double audio_s = 0; uint64_t t_syn = 0;
    std::vector<float> gap((size_t)(cfg.rate / 5), 0.0f);             // 0,2 s entre frases
    for (size_t k = 0; k < phs.size(); k++) {
        int missing = 0;
        std::vector<int64_t> ids = to_ids(phs[k], cfg, &missing);
        if (verbose) std::printf("[habla] %zu/%zu: %s%s\n", k + 1, phs.size(), phs[k].c_str(), missing ? "  (con fonemas que la voz no conoce)" : "");
        std::printf("[habla] frase %zu de %zu\n", k + 1, phs.size()); voz_flush();
        uint64_t a = voz_uptime_ms();
        std::vector<float> wav = net.synth(ids, cfg.noise, cfg.length / (speed > 0.2f ? speed : 0.2f), cfg.noise_w, rng, nullptr, sid);
        t_syn += voz_uptime_ms() - a;
        float mx = 0; for (float v : wav) { float x = v < 0 ? -v : v; if (x > mx) mx = x; }
        if (mx > 1e-6f) { float g = 0.95f / mx; for (float & v : wav) v *= g; }
        audio_s += (double)wav.size() / cfg.rate;
        out.write(wav);
        if (k + 1 < phs.size()) out.write(gap);
    }
    out.close();
    pool.stop();
    std::printf("[habla] %.1f s de voz; calculo %.1f s (carga %.1f s), %d hilos\n", audio_s, t_syn / 1000.0, t_load / 1000.0, used);
    return 0;
}
