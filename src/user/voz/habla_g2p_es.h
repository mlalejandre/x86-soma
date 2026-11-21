// SOMA_247_HABLA: texto en espanol -> fonemas IPA al estilo de espeak-ng (lo que esperan las voces de Piper).
//
// El espanol se lee casi como se escribe, asi que bastan reglas (sin espeak-ng, que es GPL): numeros a palabras, letras a
// fonemas (c/z -> theta o s con seseo, ll -> lambda o y, g/j -> x, qu, gu, gue, ch, r/rr, h muda, x -> ks, y), silabas con
// diptongos e hiatos, acento (tilde; si no, llana si acaba en vocal, n o s; aguda si no), acentos secundarios, alofonos de
// b/d/g (beta, eth, gamma entre vocales), n -> eng ante g, palabras atonas (el, la, de, que...), y la puntuacion que usa Piper.
// Salida: una cadena UTF-8 por frase, con la marca de acento delante de la vocal tonica como hace espeak-ng.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace g2p_es {

// ---------------------------------------------------------------- UTF-8
static inline uint32_t u8_next(const std::string & s, size_t & i)
{
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = c & (0x3F >> n);
    for (int k = 0; k < n && i < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp;
}
static inline void u8_put(std::string & o, uint32_t cp)
{
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | cp >> 6); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xE0 | cp >> 12); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}

// letra normalizada: minuscula; las vocales con tilde se marcan aparte
static inline uint32_t lower(uint32_t c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    return c;
}
static inline bool is_letter(uint32_t c)
{
    c = lower(c);
    return (c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFF && c != 0xF7);
}

// ---------------------------------------------------------------- numeros
static const char * const UNI[] = { "cero", "uno", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve", "diez", "once", "doce", "trece",
    "catorce", "quince", "dieciséis", "diecisiete", "dieciocho", "diecinueve", "veinte", "veintiuno", "veintidós", "veintitrés", "veinticuatro",
    "veinticinco", "veintiséis", "veintisiete", "veintiocho", "veintinueve" };
static const char * const DEC[] = { "", "", "", "treinta", "cuarenta", "cincuenta", "sesenta", "setenta", "ochenta", "noventa" };
static const char * const CEN[] = { "", "ciento", "doscientos", "trescientos", "cuatrocientos", "quinientos", "seiscientos", "setecientos", "ochocientos", "novecientos" };
static inline std::string n999(int n, bool apocope)       // apocope: "un" en vez de "uno" (veintiún, un millón)
{
    std::string s;
    if (n >= 100) { if (n == 100) return "cien"; s = CEN[n / 100]; n %= 100; if (!n) return s; s += " "; }
    if (n < 30) {
        if (apocope && n == 1) return s + "un";
        if (apocope && n == 21) return s + "veintiún";
        return s + UNI[n];
    }
    s += DEC[n / 10];
    if (n % 10) { s += " y "; s += (apocope && n % 10 == 1) ? "un" : UNI[n % 10]; }
    return s;
}
static inline std::string number_words(unsigned long long n)
{
    if (n == 0) return "cero";
    std::string s;
    unsigned long long mil_mill = n / 1000000000000ULL; n %= 1000000000000ULL;
    if (mil_mill) { s += mil_mill == 1 ? "un billón" : number_words(mil_mill) + " billones"; if (n) s += " "; }
    unsigned long long mill = n / 1000000; n %= 1000000;
    if (mill) {
        if (mill == 1) s += "un millón";
        else { int hi = (int)(mill / 1000), lo = (int)(mill % 1000); if (hi) { s += hi == 1 ? "mil" : n999(hi, true) + " mil"; if (lo) s += " "; } if (lo) s += n999(lo, true); s += " millones"; }
        if (n) s += " ";
    }
    int th = (int)(n / 1000), rest = (int)(n % 1000);
    if (th) { s += th == 1 ? "mil" : n999(th, true) + " mil"; if (rest) s += " "; }
    if (rest) s += n999(rest, false);
    return s;
}

// ---------------------------------------------------------------- texto -> palabras y puntuacion
struct Tok { std::string w; char punct = 0; bool spell = false; };   // palabra (minusculas, UTF-8) o signo (, . ? ! ; :); spell: siglas

static inline std::string expand_symbols(const std::string & in)
{
    std::string o; size_t i = 0;
    while (i < in.size()) {
        size_t st = i; uint32_t c = u8_next(in, i);
        if (c >= '0' && c <= '9') {                               // numero: 12, 3.500, 1,5, 25%
            std::string digits; size_t j = st; bool dec = false; std::string frac;
            while (j < in.size()) {
                char d = in[j];
                if (d >= '0' && d <= '9') { if (dec) frac += d; else digits += d; j++; }
                else if (d == '.' && !dec && j + 3 < in.size() + 0 && j + 3 <= in.size() && in[j + 1] >= '0' && in[j + 1] <= '9' && j + 3 < in.size() + 1 &&
                         in[j + 2] >= '0' && in[j + 2] <= '9' && in[j + 3] >= '0' && in[j + 3] <= '9' && (j + 4 >= in.size() || in[j + 4] < '0' || in[j + 4] > '9')) j++;   // separador de miles
                else if ((d == ',' || d == '.') && !dec && j + 1 < in.size() && in[j + 1] >= '0' && in[j + 1] <= '9') { dec = true; j++; }
                else break;
            }
            if (digits.size() > 15) { for (char d : digits) { o += ' '; o += UNI[d - '0']; } o += ' '; }
            else {
                unsigned long long v = 0; for (char d : digits) v = v * 10 + (unsigned long long)(d - '0');
                o += ' '; o += number_words(v);
                if (dec) { o += " coma"; size_t z = 0; while (z < frac.size() && frac[z] == '0') { o += " cero"; z++; } if (z < frac.size() && frac.size() - z <= 6) { unsigned long long f = 0; for (size_t q = z; q < frac.size(); q++) f = f * 10 + (unsigned long long)(frac[q] - '0'); o += " " + number_words(f); } }
                o += ' ';
            }
            i = j;
            continue;
        }
        switch (c) {
        case '%': o += " por ciento "; break;
        case '&': o += " y "; break;
        case '+': o += " más "; break;
        case '=': o += " igual a "; break;
        case '@': o += " arroba "; break;
        case 0x20AC: o += " euros "; break;                       // €
        case '$': o += " dólares "; break;
        case 0xBA: o += "º "; break;
        default: o.append(in, st, i - st);
        }
    }
    return o;
}

static inline std::vector<std::vector<Tok>> sentences(const std::string & text)
{
    std::string t = expand_symbols(text);
    std::vector<std::vector<Tok>> out; std::vector<Tok> cur; std::string w;
    int upper = 0, letters_n = 0, vowels_n = 0;
    auto flush_word = [&]() {
        if (!w.empty()) {
            Tok k; k.w = w;
            // siglas (USB, PC, LLM): todas mayusculas y cortas o sin vocales -> se deletrean
            if (upper == letters_n && letters_n >= 1 && ((letters_n <= 3 && (letters_n > 1 || !vowels_n)) || !vowels_n)) k.spell = true;
            if (letters_n == 1 && !vowels_n && w != "y") k.spell = true;                // letra suelta: "la s", "T480s"
            cur.push_back(k); w.clear();
        }
        upper = letters_n = vowels_n = 0;
    };
    auto end_sentence = [&](char p) {
        flush_word();
        if (!cur.empty()) { Tok k; k.punct = p; cur.push_back(k); out.push_back(cur); cur.clear(); }
    };
    size_t i = 0;
    while (i < t.size()) {
        uint32_t c = u8_next(t, i);
        if (is_letter(c) || c == 0xF1 || c == 0xD1) {
            uint32_t lc = lower(c); letters_n++; if (lc != c) upper++;
            if (lc == 'a' || lc == 'e' || lc == 'i' || lc == 'o' || lc == 'u' || (lc >= 0xE0 && lc <= 0xFC && lc != 0xF1 && lc != 0xE7)) vowels_n++;
            u8_put(w, lc); continue;
        }
        if (c == '\'' && !w.empty()) continue;
        flush_word();
        if (c == '.' || c == '!' || c == '?' || c == 0x2026) {
            char p = c == '!' ? '!' : c == '?' ? '?' : '.';
            while (i < t.size() && (t[i] == '.' || t[i] == '!' || t[i] == '?')) { if (t[i] == '?') p = '?'; i++; }
            end_sentence(p);
        } else if (c == '(' || c == ')' || c == '"' || c == 0xAB || c == 0xBB || c == 0x201C || c == 0x201D) {
            if (!cur.empty() && cur.back().punct == 0) { Tok k; k.punct = 'p'; cur.push_back(k); }   // pausa sin signo
        } else if (c == ',' || c == ';' || c == ':' || c == 0x2014 || c == '\n') {
            char p = c == ';' ? ';' : c == ':' ? ':' : ',';
            if (c == '\n' && i < t.size() && t[i] == '\n') { end_sentence('.'); continue; }
            if (!cur.empty() && cur.back().punct == 'p') cur.back().punct = p;
            else if (!cur.empty() && cur.back().punct == 0) { Tok k; k.punct = p; cur.push_back(k); }
        }
        // el resto (¿ ¡ comillas guiones simbolos) separa palabras
    }
    flush_word();
    if (!cur.empty()) out.push_back(cur);                                   // sin signo final (como espeak-ng)
    // frases muy largas: se parten en la coma mas cercana a ~40 palabras (la voz pierde calidad con frases enormes)
    std::vector<std::vector<Tok>> res;
    for (auto & s : out) {
        size_t start = 0, words = 0;
        for (size_t k = 0; k < s.size(); k++) {
            if (!s[k].punct) words++;
            bool cut = (words >= 40 && s[k].punct == ',') || words >= 70;
            if (cut && k + 1 < s.size()) {
                std::vector<Tok> part(s.begin() + (long)start, s.begin() + (long)k + 1);
                if (part.back().punct == 0) { Tok p; p.punct = ','; part.push_back(p); }
                res.push_back(part); start = k + 1; words = 0;
            }
        }
        if (start < s.size()) res.push_back(std::vector<Tok>(s.begin() + (long)start, s.end()));
    }
    return res;
}

// ---------------------------------------------------------------- palabra -> fonemas
enum { V_A = 1, V_E, V_I, V_O, V_U };
struct Seg { std::string ph; int vowel = 0; bool acc = false; bool glide = false; bool diaer = false; };   // un fonema (o vocal) de la palabra

static const char * const UNSTRESSED[] = { "el", "la", "lo", "los", "las", "le", "les", "de", "del", "a", "al", "en", "con", "por", "que", "y", "o",
    "u", "e", "mi", "mis", "tu", "tus", "su", "sus", "se", "me", "te", "nos", "os", "sin", "si", nullptr };
// palabras que espeak-ng deja con acento secundario (preposiciones y conjunciones de dos silabas)
static inline bool is_semi(const std::string & w)
{
    static const char * const S[] = { "para", "sobre", "entre", "desde", "hasta", "bajo", "como", "mientras", "contra", "hacia", "cuando", "donde", nullptr };
    for (int i = 0; S[i]; i++) if (w == S[i]) return true;
    return false;
}
static inline bool is_unstressed(const std::string & w)
{
    // espeak-ng acentua "un", "una", "muy", "son", "pero" y "como" (no las trata como atonas); se dejan acentuadas
    for (int i = 0; UNSTRESSED[i]; i++) if (w == UNSTRESSED[i]) return true;
    return false;
}

struct Opts { bool seseo = false; bool yeismo = false; };

// letras de la palabra a una lista de segmentos (sin alofonos de b/d/g ni acento todavia)
static inline std::vector<Seg> letters(const std::string & w, const Opts & op)
{
    std::vector<uint32_t> c; size_t i = 0; while (i < w.size()) c.push_back(lower(u8_next(w, i)));
    std::vector<Seg> s; size_t n = c.size();
    auto V = [&](size_t k) -> int {
        if (k >= n) return 0;
        switch (c[k]) { case 'a': case 0xE1: case 0xE0: case 0xE2: case 0xE4: return V_A; case 'e': case 0xE9: case 0xE8: case 0xEA: case 0xEB: return V_E;
            case 'i': case 0xED: case 0xEC: case 0xEE: case 0xEF: return V_I; case 'o': case 0xF3: case 0xF2: case 0xF4: case 0xF6: return V_O;
            case 'u': case 0xFA: case 0xF9: case 0xFB: case 0xFC: return V_U; default: return 0; }
    };
    auto ACC = [&](size_t k) { uint32_t x = c[k]; return x == 0xE1 || x == 0xE9 || x == 0xED || x == 0xF3 || x == 0xFA; };
    auto front = [&](size_t k) { int v = V(k); return v == V_E || v == V_I; };
    auto cons = [&](const char * p) { Seg g; g.ph = p; s.push_back(g); };
    for (size_t k = 0; k < n; k++) {
        uint32_t x = c[k]; int v = V(k);
        if (v) {
            Seg g; g.vowel = v; g.acc = ACC(k); static const char * const VP[] = { "", "a", "e", "i", "o", "u" }; g.ph = VP[v];
            if (x == 0xFC) g.diaer = true;                                      // ü: se pronuncia como vocal
            s.push_back(g); continue;
        }
        switch (x) {
        case 'b': case 'v': cons("b"); break;
        case 'c':
            if (k + 1 < n && c[k + 1] == 'h') { cons("tʃ"); k++; }
            else if (front(k + 1)) cons(op.seseo ? "s" : "θ");
            else cons("k");
            break;
        case 'd': cons("d"); break;
        case 'f': cons("f"); break;
        case 'g':
            if (front(k + 1)) cons("x");
            else if (k + 2 < n && c[k + 1] == 'u' && front(k + 2)) { cons("ɡ"); k++; }   // gue, gui: u muda
            else cons("ɡ");
            break;
        case 'h': if (k + 1 < n && V(k + 1) == V_I && V(k + 2) && V(k + 2) != V_I) { cons("j"); k++; } else if (k + 1 < n && V(k + 1) == V_U && V(k + 2) == V_E) { cons("w"); k++; } break;   // hie-, hue-
        case 'j': cons("x"); break;
        case 'k': cons("k"); break;
        case 'l':
            if (k + 1 < n && c[k + 1] == 'l') { cons(op.yeismo ? "ʝ" : "ʎ"); k++; }
            else cons("l");
            break;
        case 'm': cons("m"); break;
        case 'n': cons("n"); break;
        case 0xF1: cons("ɲ"); break;
        case 'p': cons("p"); break;
        case 'q': cons("k"); if (k + 1 < n && c[k + 1] == 'u') k++; break;
        case 'r':
            if (k + 1 < n && c[k + 1] == 'r') { cons("r"); k++; }
            else if (k == 0 || (k > 0 && (c[k - 1] == 'n' || c[k - 1] == 'l' || c[k - 1] == 's'))) cons("r");
            else cons("ɾ");
            break;
        case 's': cons("s"); break;
        case 't': cons("t"); break;
        case 'w': cons("w"); break;
        case 'x': if (k == 0) cons("s"); else { cons("k"); cons("s"); } break;
        case 'y':
            if (k + 1 < n && V(k + 1)) cons("ʝ");                               // consonante ante vocal
            else { Seg g; g.vowel = V_I; g.ph = "i"; g.glide = true; s.push_back(g); }   // final o conjuncion: vocal
            break;
        case 'z': cons(op.seseo ? "s" : "θ"); break;
        case 0xE7: cons("s"); break;                                           // ç
        default: break;
        }
    }
    return s;
}

// silabas: indices de los nucleos; las vocales debiles (i, u) junto a otra vocal sin tilde forman diptongo
static inline void syllabify(std::vector<Seg> & s, std::vector<int> & nucleus)
{
    std::vector<int> vidx; for (size_t i = 0; i < s.size(); i++) if (s[i].vowel) vidx.push_back((int)i);
    // primero: marcar las vocales que son semivocal (glide) dentro de un diptongo
    for (size_t q = 0; q < vidx.size(); q++) {
        int i = vidx[q]; Seg & a = s[(size_t)i];
        bool weak = (a.vowel == V_I || a.vowel == V_U) && !a.acc && !a.diaer;
        if (!weak) continue;
        // tras grupo consonante + r/l ("prueba", "Adrian") o tras ll ("llueve"), espeak-ng la deja como vocal (hiato)
        if (i >= 1 && !s[(size_t)i - 1].vowel && (s[(size_t)i - 1].ph == "ʎ" ||
            (i >= 2 && !s[(size_t)i - 2].vowel && (s[(size_t)i - 1].ph == "ɾ" || s[(size_t)i - 1].ph == "l")))) {
            bool nv = q + 1 < vidx.size() && vidx[q + 1] == i + 1;
            if (nv) continue;
        }
        bool prev_v = q > 0 && vidx[q - 1] == i - 1, next_v = q + 1 < vidx.size() && vidx[q + 1] == i + 1;
        if (next_v) {                                                          // i/u + vocal: creciente (j, w)
            const Seg & b = s[(size_t)i + 1];
            if ((b.vowel == V_I || b.vowel == V_U) && !b.acc && b.vowel != a.vowel) { a.glide = true; continue; }   // ui, iu: la primera es glide
            if (!(b.vowel == V_I || b.vowel == V_U) || b.acc) { a.glide = true; continue; }
        }
        if (prev_v && !s[(size_t)i - 1].glide) {                               // vocal + i/u: decreciente
            const Seg & b = s[(size_t)i - 1];
            if (!(b.vowel == V_I || b.vowel == V_U) || b.acc) a.glide = true;
        }
    }
    for (int i : vidx) if (!s[(size_t)i].glide) nucleus.push_back(i);
    if (nucleus.empty() && !vidx.empty()) { s[(size_t)vidx[0]].glide = false; nucleus.push_back(vidx[0]); }
}

static inline int stress_syllable(const std::vector<Seg> & s, const std::vector<int> & nucleus, const std::string & w)
{
    int ns = (int)nucleus.size();
    if (!ns) return -1;
    for (int k = 0; k < ns; k++) if (s[(size_t)nucleus[(size_t)k]].acc) return k;
    for (size_t i = 0; i < s.size(); i++) if (s[i].acc) {                   // tilde en una vocal que quedo como glide (raro)
        for (int k = 0; k < ns; k++) if (nucleus[(size_t)k] > (int)i) return k > 0 ? k - 1 : 0;
        return ns - 1;
    }
    if (ns == 1) return 0;
    char last = w.empty() ? 0 : w.back();
    bool llana = last == 'a' || last == 'e' || last == 'i' || last == 'o' || last == 'u' || last == 'n' || last == 's';
    if (last == 'y') llana = false;                                          // "estoy", "virrey": agudas
    return llana ? ns - 2 : ns - 1;
}

// una palabra a fonemas (marcas de acento incluidas); prev_kind: como acaba lo anterior, para los alofonos
// (0 = pausa o inicio, 1 = vocal u otro, 2 = nasal, 3 = l)
static inline std::string word_ph(const std::string & w, const Opts & op, int & prev_kind, bool next_starts_vowel, bool before_pause = false)
{
    std::vector<Seg> s = letters(w, op);
    if (s.empty()) return "";
    std::vector<int> nuc; syllabify(s, nuc);
    bool atona = is_unstressed(w) && !before_pause, semi = is_semi(w);       // ante pausa espeak acentua hasta "de", "con"...
    int st = atona ? -1 : stress_syllable(s, nuc, w);
    if (w == "y" || w == "e") { s.clear(); Seg g; g.vowel = w == "y" ? V_I : V_E; g.ph = w == "y" ? "i" : "e"; s.push_back(g); nuc.assign(1, 0); st = -1; }
    // secundarios: silabas 0, 2, 4... separadas al menos 2 de la tonica
    std::vector<int> mark(s.size(), 0);
    if (st >= 0) {
        mark[(size_t)nuc[(size_t)st]] = semi ? 2 : 1;
        for (int k = 0; k + 2 <= st; k += 2) mark[(size_t)nuc[(size_t)k]] = 2;
    }
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        Seg & g = s[i];
        std::string ph = g.ph;
        if (g.vowel) {
            if (g.glide) {
                bool before_vowel = i + 1 < s.size() && s[i + 1].vowel;
                bool after_vowel = i > 0 && s[i - 1].vowel;
                if (before_vowel && !after_vowel) ph = g.vowel == V_I ? "j" : "w";   // creciente: i -> j, u -> w
                else if (after_vowel) ph = g.vowel == V_I ? (i + 1 == s.size() && w.back() == 'y' && s[i - 1].vowel == V_U ? "j" : "ɪ") : "ʊ";   // decreciente
                else ph = g.vowel == V_I ? "i" : "u";
            }
            if (mark[i] == 1) o += "ˈ"; else if (mark[i] == 2) o += "ˌ";
            // e tonica ante n + consonante: espeak la abre
            if (g.vowel == V_E && mark[i] == 1 && i + 2 < s.size() && s[i + 1].ph == "n" && !s[i + 2].vowel && !(i > 1 && s[i - 1].glide && s[i - 1].vowel == V_I && (s[i - 2].ph == "θ" || s[i - 2].ph == "s")))
                ph = "ɛ";                                                      // e tonica ante n + consonante (salvo "-cientos"): abierta
            o += ph; prev_kind = 1;
            // i silabica ante vocal (hiato, "y", "mi" ante vocal): espeak anade una palatalizacion
            if (ph == "i" && ((i + 1 < s.size() && s[i + 1].vowel) || (i + 1 == s.size() && next_starts_vowel))) o += "ʲ";
            continue;
        }
        // consonantes: alofonos de b, d, g
        if (ph == "b" || ph == "d" || ph == "ɡ") {
            bool stop = prev_kind == 0 || prev_kind == 2;
            if (!stop) ph = ph == "b" ? "β" : ph == "d" ? "ð" : "ɣ";
        }
        if (ph == "n" && i + 1 < s.size()) {
            if (s[i + 1].ph == "ɡ") ph = "ŋ";
            else if (s[i + 1].ph == "b" || s[i + 1].ph == "p") ph = "m";
        }
        if (ph == "p" && i + 1 < s.size() && s[i + 1].ph == "t") ph = "pː";       // "captura", "script": espeak alarga la p
        o += ph;
        prev_kind = (ph == "m" || ph == "n" || ph == "ɲ" || ph == "ŋ") ? 2 : ph == "l" ? 3 : 1;
    }
    (void)next_starts_vowel;
    return o;
}

// una frase (tokens) -> fonemas como los da espeak-ng en Piper: palabras separadas por espacio, signos pegados
static inline std::string phonemize_sentence(const std::vector<Tok> & toks, const Opts & op)
{
    std::string o; int prev = 0;
    for (size_t i = 0; i < toks.size(); i++) {
        const Tok & t = toks[i];
        if (t.punct == 'p') { prev = 0; continue; }
        if (t.punct) {
            while (!o.empty() && o.back() == ' ') o.pop_back();
            o += t.punct;
            if (t.punct == ',' || t.punct == ';' || t.punct == ':') o += ' ';
            prev = 0;
            continue;
        }
        bool nv = false;
        if (i + 1 < toks.size() && !toks[i + 1].punct) { const std::string & n = toks[i + 1].w; unsigned char c0 = n.empty() ? 0 : (unsigned char)n[0]; nv = c0 == 'a' || c0 == 'e' || c0 == 'i' || c0 == 'o' || c0 == 'u' || c0 == 'h' || c0 == 0xC3; }
        std::string ph;
        if (t.spell) {                                                     // siglas y letras sueltas: el nombre de cada letra
            static const char * const NAMES[] = { "ˈa", "bˈe", "θˈe", "dˈe", "ˈe", "ˈefe", "xˈe", "ˈatʃe", "ˈi", "xˈota", "kˈa", "ˈele", "ˈɛme", "ˈɛne", "ˈo", "pˈe",
                "kˈu", "ˈɛre", "ˈɛse", "tˈe", "ˈu", "ˈuβe", "ˈuβeðˈoβle", "ˈɛkis", "ˈiɣɾjˈeɣa", "θˈeta" };
            std::vector<std::string> parts; size_t q = 0;
            while (q < t.w.size()) {
                uint32_t c = u8_next(t.w, q); std::string nm;
                if (c >= 'a' && c <= 'z') nm = NAMES[c - 'a']; else if (c == 0xF1) nm = "ˈɛɲe"; else continue;
                if (op.seseo) { size_t f; while ((f = nm.find("θ")) != std::string::npos) nm.replace(f, std::string("θ").size(), "s"); }
                if (prev != 0 && prev != 2 && (c == 'b' || c == 'd')) nm = (c == 'b' ? "βˈe" : "ðˈe");
                prev = 1;
                parts.push_back(nm);
            }
            for (size_t k = 0; k < parts.size(); k++) {
                std::string x = parts[k];
                if (k + 1 < parts.size()) {
                    size_t f; while ((f = x.find("ˈ")) != std::string::npos) x.replace(f, std::string("ˈ").size(), "ˌ");
                    while ((f = x.find("ɛ")) != std::string::npos) x.replace(f, std::string("ɛ").size(), "e");   // solo la ultima letra lleva la e abierta
                }
                ph += x;
            }
        } else ph = word_ph(t.w, op, prev, nv, i + 1 >= toks.size() || (toks[i + 1].punct && toks[i + 1].punct != 'p'));
        if (ph.empty()) continue;
        if (!o.empty() && o.back() != ' ') o += ' ';
        if (i + 1 < toks.size() && !toks[i + 1].punct && ph.size() >= 1 && ph.back() == 'n' && !toks[i + 1].spell) {   // asimilacion de la n final
            const std::string & n = toks[i + 1].w;
            if (!n.empty() && n[0] == 'g' && n.size() > 1 && n[1] != 'e' && n[1] != 'i') { ph.pop_back(); ph += "ŋ"; }
            else if (!n.empty() && (n[0] == 'p' || n[0] == 'b' || n[0] == 'v')) { ph.pop_back(); ph += "m"; }
        }
        if (!nv && ph.size() >= 2 && ph.compare(ph.size() - 2, 2, "ð") == 0) { ph.erase(ph.size() - 2); ph += "d"; }   // -d final: d salvo ante vocal
        o += ph;
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}

} // namespace g2p_es
