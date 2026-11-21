// SOMA_241_VOZ: prueba en el anfitrion del divisor de palabras que sustituye a std::regex en whisper.cpp
// (third_party/whisper/src/whisper-soma.h). Con texto ASCII debe dar exactamente lo mismo que la expresion regular original
// de whisper.cpp; con UTF-8 (acentos, enes) las letras no se parten (la expresion original, en locale C, si las partia).
#include <cstdio>
#include <regex>
#include <string>
#include <vector>
#include "whisper-soma.h"

[[noreturn]] void whisper_soma_fail(const char * msg) { std::printf("%s\n", msg); std::exit(1); }

static int fails, checks;
static std::vector<std::string> by_regex(const std::string & text) {
    std::vector<std::string> words;
    std::string str = text;
    std::regex re(R"('s|'t|'re|'ve|'m|'ll|'d| ?[[:alpha:]]+| ?[[:digit:]]+| ?[^\s[:alpha:][:digit:]]+|\s+(?!\S)|\s+)");
    std::smatch m;
    while (std::regex_search(str, m, re)) { for (auto x : m) words.push_back(x); str = m.suffix(); }
    return words;
}
static std::string join(const std::vector<std::string> & v) { std::string r; for (auto & w : v) r += "[" + w + "]"; return r; }

int main() {
    const char * ascii[] = {
        "Hello world", " Hello  world", "it's a test, isn't it?", "SOMA 2026: voz a texto!!", "a   b", "x\ny", "  ", "123abc 45",
        "we'll see what they've done", "end with spaces   ", "(parentesis) y [corchetes]", "tab\tseparated", "...", "a.b.c",
    };
    for (const char * t : ascii) {
        std::vector<std::string> a; whisper_soma_split_words(t, a);
        auto b = by_regex(t);
        checks++;
        if (a != b) { fails++; std::printf("FALLO: '%s'\n  soma : %s\n  regex: %s\n", t, join(a).c_str(), join(b).c_str()); }
    }
    {   // UTF-8: "espanol" con enie es UNA palabra
        std::vector<std::string> a; whisper_soma_split_words(" espa\xC3\xB1ol y canci\xC3\xB3n", a);
        checks++;
        if (a.size() != 3 || a[0] != " espa\xC3\xB1ol" || a[2] != " canci\xC3\xB3n") { fails++; std::printf("FALLO UTF-8: %s\n", join(a).c_str()); }
    }
    {   // nunca se pierde texto: unir las palabras devuelve la cadena original
        const char * t = "Hola, \xC2\xBF" "que tal?  Bien\n\n 3,5 kg.";
        std::vector<std::string> a; whisper_soma_split_words(t, a);
        std::string r; for (auto & w : a) r += w;
        checks++;
        if (r != t) { fails++; std::printf("FALLO: se pierde texto: %s\n", join(a).c_str()); }
    }
    std::printf("[voz 241: divisor de palabras] %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
