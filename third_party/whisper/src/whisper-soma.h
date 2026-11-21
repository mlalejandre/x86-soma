// SOMA_241_VOZ: adaptaciones de whisper.cpp para SOMA (programa de ring 3 sin excepciones de C++ ni std::regex).
#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

[[noreturn]] void whisper_soma_fail(const char * msg);
#define WHISPER_SOMA_THROW(msg) whisper_soma_fail(std::string(msg).c_str())
#define WHISPER_SOMA_TRY if (true)
#define WHISPER_SOMA_CATCH(x) else

// Divide el texto en palabras como la expresion regular de GPT-2 de whisper.cpp:
//   's 't 're 've 'm 'll 'd | ' ?letras+' | ' ?digitos+' | ' ?otros+' | espacios
// Los bytes >= 0x80 (UTF-8: a, e, n con acento...) cuentan como letras.
static inline void whisper_soma_split_words(const std::string & str, std::vector<std::string> & words) {
    auto is_alpha = [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80; };
    auto is_digit = [](unsigned char c) { return c >= '0' && c <= '9'; };
    auto is_space = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
    static const char * const contr[] = { "'s", "'t", "'re", "'ve", "'m", "'ll", "'d" };
    size_t i = 0, n = str.size();
    while (i < n) {
        bool hit = false;
        for (const char * c : contr) {
            size_t l = std::string(c).size();
            if (str.compare(i, l, c) == 0) { words.push_back(str.substr(i, l)); i += l; hit = true; break; }
        }
        if (hit) continue;
        size_t j = i;
        unsigned char c0 = (unsigned char)str[i];
        size_t k = (c0 == ' ' && i + 1 < n) ? i + 1 : i;      // espacio opcional delante
        unsigned char c = (unsigned char)str[k];
        if (is_alpha(c)) { j = k; while (j < n && is_alpha((unsigned char)str[j])) j++; }
        else if (is_digit(c)) { j = k; while (j < n && is_digit((unsigned char)str[j])) j++; }
        else if (!is_space(c)) { j = k; while (j < n && !is_space((unsigned char)str[j]) && !is_alpha((unsigned char)str[j]) && !is_digit((unsigned char)str[j])) j++; }
        else {                                                  // espacios: \s+(?!\S) | \s+
            j = i; while (j < n && is_space((unsigned char)str[j])) j++;
            if (j < n && j - i > 1) j--;                        // deja el ultimo espacio para la palabra siguiente
        }
        if (j <= i) j = i + 1;
        words.push_back(str.substr(i, j - i));
        i = j;
    }
}
