// SOMA_241_VOZ: reconocimiento de voz (voz a texto) con whisper.cpp, como programa de ring 3 de SOMA.
//
//   voz.elf -H <MiB> [-m modelo] [-l idioma] [-t hilos] [-v] [-n] archivo.wav
//
// Lo lanza el nucleo (orden "voz", ver src/core/voz.c) con una region de memoria grande bajo demanda en VOZ_HEAP_VA
// (-H dice cuantos MiB). Lee el modelo ggml y el WAV del almacen BIGFS, pasa el audio a 16 kHz mono y escribe el texto
// en la salida: una linea por segmento. Las lineas que empiezan por "[voz]" son informacion, no texto reconocido.
#include "whisper.h"
#include "ggml.h"
#include "voz_filtros.h"   // SOMA_245: contra las alucinaciones
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <cmath>

extern "C" {
void voz_heap_init(void * base, size_t size);
size_t voz_heap_peak(void);
uint64_t voz_uptime_ms(void);
void voz_flush(void);
void voz_exit(int code) __attribute__((noreturn));
extern int voz_quiet_stderr;
extern int voz_pretouch;
int voz_threads_free(void);
long voz_getargs(char * buf, long n);
int voz_map_heap(void * va, unsigned long size);
}

#define VOZ_HEAP_VA 0x8040000000ULL                 // USER_BASE + 1 GiB (ver usermode.h del nucleo)

static int g_verbose;
static void log_cb(enum ggml_log_level level, const char * text, void *) {
    if (g_verbose || level == GGML_LOG_LEVEL_ERROR) std::fputs(text, stdout);
}

// ---- WAV -> float 16 kHz mono
static int16_t rd16(const uint8_t * p) { return (int16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t * p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static bool load_wav(const char * path, std::vector<float> & out, std::string & err, double & secs) {
    FILE * f = std::fopen(path, "rb");
    if (!f) { err = "no se puede abrir el audio (debe estar en el almacen BIGFS)"; return false; }
    std::fseek(f, 0, SEEK_END); long size = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    if (size < 44 || size > 512L * 1024 * 1024) { std::fclose(f); err = "tamano de WAV no valido"; return false; }
    std::vector<uint8_t> d((size_t)size);
    size_t got = std::fread(d.data(), 1, d.size(), f);
    std::fclose(f);
    if (got != d.size() || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) { err = "no es un WAV (RIFF/WAVE)"; return false; }
    uint32_t rate = 0, ch = 0, bits = 0, fmt = 0; const uint8_t * pcm = nullptr; size_t pcm_len = 0;
    for (size_t o = 12; o + 8 <= d.size();) {
        uint32_t id_len = rd32(&d[o + 4]);
        if (!std::memcmp(&d[o], "fmt ", 4) && id_len >= 16 && o + 8 + 16 <= d.size()) {
            fmt = (uint16_t)rd16(&d[o + 8]); ch = (uint16_t)rd16(&d[o + 10]); rate = rd32(&d[o + 12]); bits = (uint16_t)rd16(&d[o + 22]);
            if (fmt == 0xFFFE && id_len >= 40) fmt = (uint16_t)rd16(&d[o + 32]);   // WAVE_FORMAT_EXTENSIBLE: subformato
        } else if (!std::memcmp(&d[o], "data", 4)) {
            pcm = &d[o + 8]; pcm_len = std::min<size_t>(id_len, d.size() - o - 8);
            break;
        }
        o += 8 + id_len + (id_len & 1);
    }
    if (!pcm || !rate || !ch || ch > 8) { err = "WAV sin datos o sin formato"; return false; }
    if (!((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) { err = "formato de WAV no soportado (PCM de 8/16/24/32 bits o float)"; return false; }
    size_t bps = bits / 8, frames = pcm_len / (bps * ch);
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; i++) {
        float acc = 0;
        for (uint32_t c = 0; c < ch; c++) {
            const uint8_t * p = pcm + (i * ch + c) * bps;
            float v;
            if (fmt == 3) { uint32_t u = rd32(p); float fv; std::memcpy(&fv, &u, 4); v = fv; }
            else if (bits == 8) v = ((int)p[0] - 128) / 128.0f;
            else if (bits == 16) v = rd16(p) / 32768.0f;
            else if (bits == 24) v = (float)((int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8) / 8388608.0f;
            else v = (float)(int32_t)rd32(p) / 2147483648.0f;
            acc += v;
        }
        mono[i] = acc / (float)ch;
    }
    secs = (double)frames / rate;
    if (rate == WHISPER_SAMPLE_RATE) { out.swap(mono); return true; }
    // remuestreo con un filtro sinc enventanado (Hann, 32 coeficientes por lado): pasa banda hasta ~7,2 kHz, sin el aliasing
    // que da la interpolacion lineal al bajar de 44,1/48 kHz a 16 kHz (whisper se equivoca bastante mas con el audio "sucio")
    size_t n_out = (size_t)((double)frames * WHISPER_SAMPLE_RATE / rate);
    out.resize(n_out);
    const double ratio = (double)WHISPER_SAMPLE_RATE / rate;              // < 1 al bajar de frecuencia
    const double fc = 0.45 * (ratio < 1.0 ? ratio : 1.0);                 // corte relativo a la frecuencia de origen
    const int half = (int)(16.0 / (ratio < 1.0 ? ratio : 1.0));           // coeficientes por lado (en muestras de origen)
    const double PI = 3.14159265358979323846;
    for (size_t j = 0; j < n_out; j++) {
        double x = j / ratio;
        long c = (long)x;
        double acc = 0, wsum = 0;
        for (long k = c - half + 1; k <= c + half; k++) {
            if (k < 0 || k >= (long)frames) continue;
            double d = x - (double)k;
            double sx = 2 * PI * fc * d;
            double sinc = d == 0 ? 1.0 : std::sin(sx) / sx;
            double w = 0.5 + 0.5 * std::cos(PI * d / half);
            double h = 2 * fc * sinc * w;
            acc += mono[(size_t)k] * h; wsum += h;
        }
        out[j] = (float)(wsum != 0 ? acc / wsum : 0);
    }
    return true;
}

static void progress_cb(struct whisper_context *, struct whisper_state *, int progress, void *) {
    static int last = -1;
    if (progress / 25 != last / 25 && progress < 100) { std::printf("[voz] %d %%\n", progress); voz_flush(); }
    last = progress;
}

int voz_main(int argc, char ** argv) {
    const char * model = "/modelos/ggml-base-q5_1.bin", * lang = "es", * wav = nullptr;
    const char * prompt = nullptr;
    int threads = 0, beam = 5; bool no_ts = true, short_ctx = true, use_vad = true;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "-m") && i + 1 < argc) model = argv[++i];
        else if (!std::strcmp(argv[i], "-l") && i + 1 < argc) lang = argv[++i];
        else if (!std::strcmp(argv[i], "-t") && i + 1 < argc) threads = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "-H") && i + 1 < argc) ++i;                 // ya lo uso el arranque
        else if (!std::strcmp(argv[i], "-v")) { g_verbose = 1; voz_quiet_stderr = 0; }
        else if (!std::strcmp(argv[i], "-T")) no_ts = false;
        else if (!std::strcmp(argv[i], "-C")) short_ctx = false;
        else if (!std::strcmp(argv[i], "-V")) use_vad = false;                   // sin recortar silencios
        else if (!std::strcmp(argv[i], "-b") && i + 1 < argc) beam = std::atoi(argv[++i]);   // 1 = voraz (mas rapido)
        else if (!std::strcmp(argv[i], "-p") && i + 1 < argc) { static std::string pr; pr = argv[++i]; for (char & c : pr) if (c == '_') c = ' '; prompt = pr.c_str(); }   // palabras_unidas_con_guiones
        else wav = argv[i];
    }
    if (!wav) { std::printf("[voz] uso: voz [-m modelo] [-l idioma] [-T] [-C] [-V] [-b haz] [-p palabras_clave] [-v] archivo.wav\n"); return 2; }
    // SOMA_244_HILOS: un hilo por nucleo libre (el principal + los secundarios), hasta 4: con mas, el reparto de whisper no gana
    if (threads <= 0) { int f = voz_threads_free(); if (f < 0) f = 0; threads = 1 + (f < 3 ? f : 3); }
    if (threads > 8) threads = 8;
    if (threads > 1) voz_pretouch = 1;                    // los hilos secundarios no pueden provocar fallos de pagina
    whisper_log_set(log_cb, nullptr);
    ggml_log_set(log_cb, nullptr);

    uint64_t t0 = voz_uptime_ms();
    std::vector<float> pcm; std::string err; double secs = 0;
    if (!load_wav(wav, pcm, err, secs)) { std::printf("[voz] %s: %s\n", wav, err.c_str()); return 3; }
    if (secs < 0.3) { std::printf("[voz] el audio es demasiado corto (%.1f s)\n", secs); return 3; }
    // SOMA_245: detector de voz. Sin voz no se reconoce nada (whisper rellenaria el silencio con frases inventadas); con voz se
    // recortan los silencios del principio y del final y se acortan las pausas largas. Con -T (marcas de tiempo) o -V no se recorta.
    {
        voz_vad_info vi{};
        std::vector<float> trimmed = pcm;
        bool any = voz_vad_trim(trimmed, WHISPER_SAMPLE_RATE, &vi);
        if (!any) { std::printf("[voz] no se oye ninguna voz: habla mas cerca del microfono o comprueba su volumen\n"); return 6; }
        if (g_verbose) std::printf("[voz] voz detectada: %d ms en %d tramos (ruido %.4f, umbral %.4f); se reconocen %d ms\n", vi.speech_ms, vi.regions, vi.noise, vi.thr, vi.kept_ms);
        if (use_vad && no_ts) { pcm.swap(trimmed); secs = (double)pcm.size() / WHISPER_SAMPLE_RATE; }
    }

    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;
    whisper_context * ctx = whisper_init_from_file_with_params(model, cp);
    if (!ctx) { std::printf("[voz] no se pudo cargar el modelo %s (instalalo con: voz instalar)\n", model); return 4; }
    uint64_t t1 = voz_uptime_ms();
    if (g_verbose) std::printf("[voz] modelo cargado en %llu ms; audio %.1f s\n", (unsigned long long)(t1 - t0), secs);

    // SOMA_245: decodificacion conservadora contra alucinaciones: busqueda en haz (5), sin texto previo como contexto, sin tokens de
    // "no habla" (musica, aplausos), y los umbrales de OpenAI para repetir con mas temperatura o descartar un tramo sin voz.
    whisper_full_params p = whisper_full_default_params(beam > 1 ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);
    if (beam > 1) p.beam_search.beam_size = beam;
    p.suppress_nst = true;
    p.temperature = 0.0f;
    p.temperature_inc = 0.2f;
    p.entropy_thold = 2.4f;
    p.logprob_thold = -1.0f;
    p.no_speech_thold = 0.6f;
    p.n_threads = threads;
    p.language = lang;
    p.translate = false;
    p.no_context = true;
    p.no_timestamps = no_ts;
    p.print_progress = false;
    p.print_realtime = false;
    p.print_special = false;
    p.print_timestamps = false;
    p.suppress_blank = true;
    p.progress_callback = progress_cb;
    // el codificador trabaja siempre sobre 30 s; con audios cortos se le da solo lo necesario (cada posicion son 20 ms, mas margen):
    // varias veces mas rapido para dictados cortos. Con -C se usa la ventana completa (un poco mas preciso).
    if (short_ctx && secs < 28.0) { int ctx = (int)(secs * 50.0) + 192; p.audio_ctx = ctx < 1500 ? ctx : 0; }
    p.initial_prompt = prompt;         // -p: vocabulario inicial (ojo: con la voz sintetica a veces se salta frases; por defecto ninguno)
    // SOMA_249_REPES: tope de tokens por tramo segun lo que dura el audio (unos 8 tokens por segundo de habla como mucho): un bucle
    // de repeticion no puede llenar la salida. Sin marcas de tiempo todo el dictado es un solo tramo.
    if (no_ts) p.max_tokens = (int)(secs * 9.0) + 24;

    // SOMA_245: limpieza de cada tramo: probabilidad de "no hay voz", confianza media de sus palabras, anotaciones, frases tipicas
    // de subtitulos y bucles de repeticion. Lo descartado se cuenta (con -v se ve por que).
    // SOMA_249_REPES: los tramos que se quedan se juntan y se limpian de repeticiones TODOS juntos (whisper repite a veces la frase
    // entera en tramos seguidos), y se escriben una sola vez al final.
    int printed = 0, dropped = 0;
    auto decode = [&](std::string & out, bool & suspicious) -> bool {
        printed = 0; dropped = 0; out.clear();
        if (whisper_full(ctx, p, pcm.data(), (int)pcm.size()) != 0) return false;
        int n = whisper_full_n_segments(ctx);
        std::string raw;
        for (int i = 0; i < n; i++) {
            float nsp = whisper_full_get_segment_no_speech_prob(ctx, i);
            double psum = 0; int pn = 0;
            for (int k = 0; k < whisper_full_n_tokens(ctx, i); k++) {
                whisper_token_data td = whisper_full_get_token_data(ctx, i, k);
                if (td.id >= whisper_token_eot(ctx)) continue;            // tokens especiales y marcas de tiempo
                psum += td.p; pn++;
            }
            float pavg = pn ? (float)(psum / pn) : 0.0f;
            std::string t = voz_collapse_repeats(voz_strip_annotations(whisper_full_get_segment_text(ctx, i)));
            const char * why = nullptr; bool weak = false;
            if (t.empty()) why = "vacio o solo anotaciones";
            else if (nsp > 0.6f && pavg < 0.5f) why = "sin voz";
            else if (voz_is_stock_phrase(t, &weak)) why = "frase tipica de subtitulos";
            else if (weak && (nsp > 0.3f || pavg < 0.55f)) why = "frase corta dudosa";
            else if (pavg < 0.3f) why = "confianza muy baja";
            if (why) { dropped++; if (g_verbose) std::printf("[voz] descartado (%s; no-voz %.2f, confianza %.2f): %s\n", why, nsp, pavg, t.c_str()); continue; }
            if (!no_ts) {
                char ts[64];
                int64_t a0 = whisper_full_get_segment_t0(ctx, i), b0 = whisper_full_get_segment_t1(ctx, i);   // centesimas
                std::snprintf(ts, sizeof ts, "[%02d:%02d.%02d -> %02d:%02d.%02d] ", (int)(a0 / 6000), (int)(a0 / 100 % 60), (int)(a0 % 100), (int)(b0 / 6000), (int)(b0 / 100 % 60), (int)(b0 % 100));
                out += ts; out += t; out += "\n";
            } else { if (!raw.empty()) raw += ' '; raw += t; }
            printed++;
        }
        suspicious = false;
        if (no_ts && !raw.empty()) {
            std::string c = voz_collapse_repeats(raw);
            suspicious = c.size() + 8 < raw.size() || voz_words_per_sec(c, secs) > 5.5;
            if (g_verbose && c != raw) std::printf("[voz] repeticion quitada: %s\n", raw.c_str());
            out = c + "\n";
        }
        return true;
    };
    std::string text; bool susp = false;
    if (!decode(text, susp)) { std::printf("[voz] fallo el reconocimiento\n"); whisper_free(ctx); return 5; }
    // con la ventana de audio recortada whisper entra a veces en bucle: si huele a repeticion, otra vez con la ventana completa
    if (susp && p.audio_ctx != 0) {
        if (g_verbose) std::printf("[voz] posible bucle de repeticion: repito con la ventana de audio completa\n");
        p.audio_ctx = 0;
        std::string t2; bool s2 = false; int pr0 = printed, dr0 = dropped;
        if (decode(t2, s2) && !t2.empty() && (!s2 || t2.size() < text.size())) text = t2;
        else { printed = pr0; dropped = dr0; }
    }
    uint64_t t2 = voz_uptime_ms();
    if (!text.empty()) std::printf("%s", text.c_str());
    if (!printed) std::printf(dropped ? "[voz] no se entendio con seguridad lo que se dijo: prueba otra vez\n" : "[voz] (no se reconocio ninguna palabra)\n");
    std::printf("[voz] %.1f s de audio en %.1f s (modelo %.1f s, reconocimiento %.1f s, %d hilo%s, memoria %zu MiB)\n",
                secs, (t2 - t0) / 1000.0, (t1 - t0) / 1000.0, (t2 - t1) / 1000.0, threads, threads == 1 ? "" : "s", voz_heap_peak() >> 20);
    whisper_free(ctx);
    return 0;
}

// ---- arranque: pila propia de 2 MiB (la del proceso es de 64 KiB), constructores globales, argumentos y monton
extern "C" { uint8_t voz_stack[2u << 20] __attribute__((aligned(64), used)); }
int habla_main(int argc, char ** argv);              // SOMA_247_HABLA: habla.cpp
extern "C" void (*__init_array_start[])(void);
extern "C" void (*__init_array_end[])(void);
extern "C" [[noreturn]] void voz_cstart(void) {
    static char args[256];
    static char * argv[32];
    long n = voz_getargs(args, (long)sizeof args - 1);
    int argc = 0; argv[argc++] = (char *)"voz";
    size_t heap_mib = 256;
    if (n > 0) {
        char * s = args;
        while (*s && argc < 31) {
            while (*s == ' ') *s++ = 0;
            if (!*s) break;
            argv[argc++] = s;
            while (*s && *s != ' ') s++;
        }
    }
    for (int i = 1; i + 1 < argc; i++) if (!std::strcmp(argv[i], "-H")) heap_mib = (size_t)std::atoi(argv[i + 1]);
    if (voz_map_heap((void *)VOZ_HEAP_VA, heap_mib << 20) != 0) { std::printf("[voz] sin memoria para el monton\n"); voz_flush(); voz_exit(4); }
    voz_heap_init((void *)VOZ_HEAP_VA, heap_mib << 20);
    for (void (**f)(void) = __init_array_start; f < __init_array_end; f++) (*f)();
    int r = (argc > 1 && !std::strcmp(argv[1], "--habla")) ? habla_main(argc - 1, argv + 1) : voz_main(argc, argv);   // SOMA_247: texto a voz
    voz_flush();
    voz_exit(r);
}
extern "C" __attribute__((naked, section(".text._start"))) void _start(void) {
    __asm__ volatile ("lea voz_stack+%c0(%%rip), %%rsp\n\tand $-64, %%rsp\n\tcall voz_cstart\n\tud2" : : "i"(sizeof voz_stack));
}
