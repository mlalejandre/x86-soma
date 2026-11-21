// SOMA_245_PTT: tecnicas contra las "alucinaciones" de whisper (frases inventadas). Funciones puras, probadas en el anfitrion
// (tests/test_voz_245.cpp). Las aplica voz.cpp:
//   1. ANTES de reconocer, un detector de voz por energia (VAD): recorta el silencio del principio y del final, acorta las pausas
//      largas (whisper rellena los silencios con frases tipicas de subtitulos) y, si no hay voz, no se reconoce nada.
//   2. Parametros de decodificacion conservadores (en voz.cpp): sin contexto previo, sin tokens de "no habla", umbrales de
//      entropia, probabilidad media y "no hay voz", busqueda en haz.
//   3. DESPUES, limpieza del texto: anotaciones ([Musica], (risas), notas musicales), frases conocidas de subtitulos de YouTube y
//      Amara, y bucles de repeticion ("hola hola hola hola" -> "hola").
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

struct voz_vad_info { int speech_ms; int kept_ms; int regions; float noise; float thr; };

// Recorta el audio (16 kHz mono) a las zonas con voz. Devuelve false si no hay voz.
static inline bool voz_vad_trim(std::vector<float> & pcm, int rate, voz_vad_info * info = nullptr)
{
    const int fr = rate / 50;                                   // tramas de 20 ms
    const int nf = (int)(pcm.size() / (size_t)fr);
    if (nf < 5) return false;
    std::vector<float> e((size_t)nf);
    for (int f = 0; f < nf; f++) {
        double s = 0; for (int i = 0; i < fr; i++) { float v = pcm[(size_t)f * fr + i]; s += (double)v * v; }
        e[(size_t)f] = (float)std::sqrt(s / fr);
    }
    std::vector<float> srt = e; std::sort(srt.begin(), srt.end());
    float noise = srt[(size_t)(nf / 10)];                       // el 10 % mas bajo: el ruido de fondo
    float loud = srt[(size_t)(nf - 1 - nf / 20)];               // el 5 % mas alto
    float thr = std::max(noise * 3.0f, 0.006f);
    if (loud > noise * 8.0f) thr = std::max(thr, noise + (loud - noise) * 0.08f);
    std::vector<char> sp((size_t)nf, 0);
    for (int f = 0; f < nf; f++) sp[(size_t)f] = e[(size_t)f] > thr;
    // quita "picos" sueltos de menos de 60 ms (clics, golpes) y une huecos de menos de 300 ms (pausas entre palabras)
    for (int f = 0; f < nf;) {
        if (!sp[(size_t)f]) { f++; continue; }
        int g = f; while (g < nf && sp[(size_t)g]) g++;
        if (g - f < 3) for (int k = f; k < g; k++) sp[(size_t)k] = 0;
        f = g;
    }
    for (int f = 0; f < nf;) {
        if (sp[(size_t)f]) { f++; continue; }
        int g = f; while (g < nf && !sp[(size_t)g]) g++;
        if (f > 0 && g < nf && g - f < 15) for (int k = f; k < g; k++) sp[(size_t)k] = 1;
        f = g;
    }
    int speech = 0, regions = 0;
    for (int f = 0; f < nf; f++) { if (sp[(size_t)f]) { speech++; if (f == 0 || !sp[(size_t)f - 1]) regions++; } }
    if (info) { info->speech_ms = speech * 20; info->regions = regions; info->noise = noise; info->thr = thr; info->kept_ms = 0; }
    if (speech * 20 < 250) return false;                         // menos de un cuarto de segundo de voz: nada que reconocer
    // se conserva cada zona de voz con 200 ms antes y 300 ms despues; las pausas entre zonas se dejan en 400 ms como mucho
    std::vector<float> out; out.reserve(pcm.size());
    const int pre = 10, post = 15, gap_max = 20;
    int last_end = -1;
    auto put = [&](int x0, int x1) { if (x1 > x0) out.insert(out.end(), pcm.begin() + (long)x0 * fr, pcm.begin() + (long)x1 * fr); };
    for (int f = 0; f < nf;) {
        if (!sp[(size_t)f]) { f++; continue; }
        int g = f; while (g < nf && sp[(size_t)g]) g++;
        int a = std::max(0, f - pre), b = std::min(nf, g + post);
        if (last_end >= 0) {
            if (a <= last_end) a = last_end;                     // zonas pegadas: seguido
            else if (a - last_end > gap_max) { put(last_end, last_end + gap_max / 2); put(a - gap_max / 2, a); }   // pausa larga: se acorta
            else put(last_end, a);
        }
        put(a, b);
        last_end = b;
        f = g;
    }
    if (info) info->kept_ms = (int)(out.size() * 1000 / (size_t)rate);
    pcm.swap(out);
    return true;
}

// minusculas, sin acentos ni signos: para comparar frases
static inline std::string voz_norm(const std::string & s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0xC3 && i + 1 < s.size()) {                     // vocales con acento, enie, dieresis (UTF-8)
            unsigned char d = (unsigned char)s[++i];
            const char * m = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";  // 0x80..0x9F mayusculas y 0xA0..0xBF minusculas -> letra base
            char b = m[d & 0x1F];
            if (b != '/' ) o += b;
            continue;
        }
        if (c >= 0x80) continue;                                 // otros simbolos (comillas tipograficas, notas...)
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o += (char)c;
        else if (c >= 'A' && c <= 'Z') o += (char)(c + 32);
        else if (!o.empty() && o.back() != ' ') o += ' ';
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}

// Frase tipica de subtitulos (alucinacion casi segura). weak = solo cuenta si el segmento es dudoso.
static inline bool voz_is_stock_phrase(const std::string & text, bool * weak = nullptr)
{
    std::string n = voz_norm(text);
    if (weak) *weak = false;
    if (n.empty()) return true;
    static const char * const strong[] = {
        "amara org", "subtitulos realizados por", "subtitulos por la comunidad", "subtitulado por", "subtitulos hechos por",
        "suscribete", "suscribanse", "suscribirte", "no olvides suscribir", "dale like", "dale a like", "activa la campanita",
        "gracias por ver", "gracias por mirar", "nos vemos en el proximo video", "nos vemos en el siguiente video",
        "thanks for watching", "thank you for watching", "please subscribe", "www ", "http", "transcripcion y subtitulos",
        "editado por", "traducido por" };
    for (const char * k : strong) if ((" " + n + " ").find(std::string(" ") + k) != std::string::npos) return true;
    static const char * const weak_eq[] = { "gracias", "muchas gracias", "adios", "chao", "hasta luego", "hasta la proxima",
        "musica", "aplausos", "risas", "silencio", "fin", "si", "no", "eh", "mmm", "ah", "oh", "bueno", "vale", "ok", "you" };
    for (const char * k : weak_eq) if (n == k) { if (weak) *weak = true; return false; }
    return false;
}

// Quita anotaciones: [ ... ], ( ... ), * ... *, notas musicales; y espacios sobrantes.
static inline std::string voz_strip_annotations(const std::string & t)
{
    std::string o;
    int depth_sq = 0, depth_par = 0;
    for (size_t i = 0; i < t.size(); i++) {
        char c = t[i];
        if (c == '[') { depth_sq++; continue; }
        if (c == ']') { if (depth_sq) depth_sq--; continue; }
        if (c == '(') { depth_par++; continue; }
        if (c == ')') { if (depth_par) depth_par--; continue; }
        if (depth_sq || depth_par) continue;
        if ((unsigned char)c == 0xE2 && i + 2 < t.size() && (unsigned char)t[i + 1] == 0x99 && ((unsigned char)t[i + 2] == 0xAA || (unsigned char)t[i + 2] == 0xAB)) { i += 2; continue; }   // notas
        if (c == '*') continue;
        o += c;
    }
    std::string r; bool sp = false;
    for (char c : o) { if (c == ' ' || c == '\t' || c == '\n') { sp = !r.empty(); continue; } if (sp) { r += ' '; sp = false; } r += c; }
    return r;
}

// Bucles de repeticion: una secuencia de 1 a 8 palabras repetida 3 o mas veces seguidas se deja en una.
// SOMA_249_REPES: bucles de repeticion de whisper. Un bloque de n palabras repetido seguido se deja una sola vez:
//   n >= 4 palabras: basta con 2 copias seguidas (en el habla real no se repite una frase entera tal cual);
//   n 2..3: 3 copias ("y entonces y entonces y entonces"); n = 1: 4 copias ("muy muy" o "no no no" son legitimos).
// Si al final del texto queda una copia a medias (el bucle se corto por el limite de tokens), tambien se quita.
// Se compara sin mayusculas, acentos ni puntuacion. Antes (245) solo bloques de hasta 8 palabras y 3 copias: una frase de 15
// palabras repetida pasaba entera tres veces.
static inline std::string voz_collapse_repeats(const std::string & t)
{
    std::vector<std::string> w;
    { std::string cur; for (char c : t) { if (c == ' ' || c == '\n' || c == '\t') { if (!cur.empty()) w.push_back(cur); cur.clear(); } else cur += c; } if (!cur.empty()) w.push_back(cur); }
    std::vector<std::string> nw; for (auto & x : w) nw.push_back(voz_norm(x));
    auto same = [&](size_t a, size_t b) { return nw[a] == nw[b] && !nw[a].empty(); };
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t n = 1; n <= w.size() / 2 && !changed; n++) {
            size_t need = n >= 4 ? 2 : n >= 2 ? 3 : 4;
            for (size_t i = 0; i + 2 * n <= w.size() && !changed; i++) {
                size_t reps = 1;
                while (i + (reps + 1) * n <= w.size()) {
                    bool eq = true;
                    for (size_t k = 0; k < n; k++) if (!same(i + k, i + reps * n + k)) { eq = false; break; }
                    if (!eq) break;
                    reps++;
                }
                if (reps < need) continue;
                size_t end = i + reps * n;                     // copia a medias al final del texto
                size_t tail = w.size() - end;
                if (n >= 4 && tail > 0 && tail < n) {
                    bool pre = true; for (size_t k = 0; k < tail; k++) if (!same(i + k, end + k)) { pre = false; break; }
                    if (pre) end = w.size();
                }
                w.erase(w.begin() + (long)(i + n), w.begin() + (long)end);
                nw.erase(nw.begin() + (long)(i + n), nw.begin() + (long)end);
                changed = true;
            }
        }
    }
    std::string o; for (size_t i = 0; i < w.size(); i++) { if (i) o += ' '; o += w[i]; }
    return o;
}
// SOMA_249_REPES: palabras por segundo de un texto: el habla real no pasa de unas 4-5; mas es un bucle o texto inventado.
static inline double voz_words_per_sec(const std::string & t, double secs)
{
    size_t n = 0; bool in = false;
    for (char c : t) { bool sp = c == ' ' || c == '\n' || c == '\t'; if (!sp && !in) n++; in = !sp; }
    return secs > 0.05 ? (double)n / secs : 0.0;
}
