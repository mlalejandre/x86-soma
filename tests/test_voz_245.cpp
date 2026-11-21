// SOMA_245_PTT: prueba en el anfitrion de los filtros contra alucinaciones de whisper (src/user/voz/voz_filtros.h)
#include <cstdio>
#include <cstdlib>
#include "../src/user/voz/voz_filtros.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; std::printf("FALLO (linea %d): ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::vector<float> tone(float secs, float amp, unsigned seed = 1) {
    std::vector<float> v((size_t)(secs * 16000));
    for (size_t i = 0; i < v.size(); i++) { seed = seed * 1103515245u + 12345u; float n = ((seed >> 16) & 0x7FFF) / 32768.0f - 0.5f;
        v[i] = amp * std::sin((float)i * 0.09f) * (0.6f + 0.4f * std::sin((float)i * 0.0013f)) + n * 0.002f; }
    return v;
}
static std::vector<float> noise(float secs, float amp, unsigned seed = 7) {
    std::vector<float> v((size_t)(secs * 16000));
    for (size_t i = 0; i < v.size(); i++) { seed = seed * 1103515245u + 12345u; v[i] = amp * (((seed >> 16) & 0x7FFF) / 32768.0f - 0.5f); }
    return v;
}
static void cat(std::vector<float> & a, const std::vector<float> & b) { a.insert(a.end(), b.begin(), b.end()); }

int main() {
    {   // silencio / ruido suave: no hay voz
        auto s = noise(3.0f, 0.003f); voz_vad_info in{};
        CHECK(!voz_vad_trim(s, 16000, &in), "ruido de fondo tomado por voz (voz %d ms)", in.speech_ms);
        auto z = std::vector<float>(16000 * 2, 0.0f);
        CHECK(!voz_vad_trim(z, 16000), "silencio tomado por voz");
    }
    {   // voz con silencio largo delante, en medio y detras: se recorta
        auto a = noise(2.0f, 0.003f); cat(a, tone(1.5f, 0.2f)); cat(a, noise(4.0f, 0.003f, 9)); cat(a, tone(1.0f, 0.2f, 3)); cat(a, noise(3.0f, 0.003f, 11));
        size_t before = a.size(); voz_vad_info in{};
        bool ok = voz_vad_trim(a, 16000, &in);
        CHECK(ok, "voz no detectada");
        CHECK(in.regions == 2, "zonas de voz: %d (esperadas 2)", in.regions);
        CHECK(in.speech_ms >= 2300 && in.speech_ms <= 2700, "voz %d ms (esperados ~2500)", in.speech_ms);
        CHECK(a.size() < before / 2, "no se recorto el silencio (%zu -> %zu)", before, a.size());
        CHECK(in.kept_ms >= 2900 && in.kept_ms <= 4200, "audio conservado %d ms", in.kept_ms);
    }
    {   // un clic aislado no es voz
        auto a = noise(2.0f, 0.003f); for (int i = 0; i < 400; i++) a[16000 + i] = 0.5f;
        CHECK(!voz_vad_trim(a, 16000), "un clic de 25 ms tomado por voz");
    }
    // frases de subtitulos
    CHECK(voz_is_stock_phrase("Subtítulos realizados por la comunidad de Amara.org"), "amara");
    CHECK(voz_is_stock_phrase(" ¡Suscríbete al canal!"), "suscribete");
    CHECK(voz_is_stock_phrase("Gracias por ver el vídeo."), "gracias por ver");
    CHECK(!voz_is_stock_phrase("Abre el navegador y busca el tiempo en Madrid"), "frase normal descartada");
    CHECK(!voz_is_stock_phrase("Quiero suscribir una póliza"), "suscribir legitimo descartado");
    { bool w = false; CHECK(!voz_is_stock_phrase("Gracias.", &w) && w, "gracias sola debe ser dudosa"); }
    CHECK(voz_norm("¡Él está AQUÍ, señor!") == "el esta aqui senor", "normalizacion: '%s'", voz_norm("¡Él está AQUÍ, señor!").c_str());
    // anotaciones y repeticiones
    CHECK(voz_strip_annotations("[Música] hola (risas) mundo ♪") == "hola mundo", "anotaciones: '%s'", voz_strip_annotations("[Música] hola (risas) mundo ♪").c_str());
    CHECK(voz_collapse_repeats("hola hola hola hola que tal") == "hola que tal", "repeticion 1: '%s'", voz_collapse_repeats("hola hola hola hola que tal").c_str());
    CHECK(voz_collapse_repeats("y entonces y entonces y entonces fuimos") == "y entonces fuimos", "repeticion 2: '%s'", voz_collapse_repeats("y entonces y entonces y entonces fuimos").c_str());
    CHECK(voz_collapse_repeats("muy muy bien") == "muy muy bien", "dos repeticiones son legitimas");
    {   // SOMA_249_REPES: el fallo real del T480s (frase de 15 palabras tres veces y media, cortada al final)
        std::string f = "Hola, mi nombre es Marcos, ¿cómo te llamas tú? Me encantaría ser tu amigo.";
        std::string r = f + " " + f + " " + f + " Hola, mi nombre es";
        CHECK(voz_collapse_repeats(r) == f, "frase larga repetida: '%s'", voz_collapse_repeats(r).c_str());
        CHECK(voz_collapse_repeats(f + " " + f) == f, "frase larga dos veces: '%s'", voz_collapse_repeats(f + " " + f).c_str());
        CHECK(voz_collapse_repeats("Hola, mi nombre es Marcos. hola mi nombre es marcos") == "Hola, mi nombre es Marcos.", "sin mayusculas ni puntuacion");
        CHECK(voz_collapse_repeats(f + " Y tú, ¿quién eres?") == f + " Y tú, ¿quién eres?", "sin repeticion no cambia");
        CHECK(voz_collapse_repeats("que si, que si, ya voy") == "que si, que si, ya voy", "dos copias cortas son legitimas");
        CHECK(voz_collapse_repeats("no no no") == "no no no", "tres noes son legitimos");
        CHECK(voz_collapse_repeats("uno dos tres cuatro cinco uno dos tres") == "uno dos tres cuatro cinco uno dos tres", "un trozo no seguido no es repeticion");
        CHECK(voz_words_per_sec(r, 4.0) > 5.5 && voz_words_per_sec(f, 5.0) < 5.5, "palabras por segundo");
    }
    std::printf("[voz 245: filtros contra alucinaciones] %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
