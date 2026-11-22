/* SOMA_259_ORBE (tamanos de SOMA_260_LLM_TAM): la respuesta del orbe como un teleprompter, con formato, y una barra de fases con iconos.
 *
 * NO se compila por separado: lo incluye src/ui/gui.c despues de gui_vortice.c (usa her_ans, her_draw_cell, her_mix256...).
 *
 *  - Zona de texto fija (ancho maximo 980 px) con letra fija (Ajustes > LCM > Letra del orbe: pequena, mediana, grande) con el
 *    texto suavizado de web_font (Liberation Sans, negrita y cursiva de verdad; simbolos de DejaVu Sans). Sin fuente: mapa de bits x2.
 *  - Desplazamiento: sigue solo al texto mientras se escribe; al mover el raton arriba/abajo o la rueda tomas el control; al
 *    llegar al final (o con una pregunta nueva) vuelve a seguir solo. Una barra fina a la derecha dice donde estas.
 *  - Formato: **negrita**, __negrita__, *cursiva*, _cursiva_, `codigo`, ```bloques```, # titulos, - listas, > citas, ---, [enlace](url),
 *    <b> <i> <u>, y matematicas $...$, $$...$$, \(...\), \[...\] (LaTeX basico: griegas, \frac, \sqrt, ^ y _, \times, \le, \sum...).
 *  - El razonamiento va detras de la respuesta, mas tenue: se ve bajando.
 *  - Barra de fases: iconos de lo que va haciendo SOMA en esta peticion (LCM, mayordomo, busqueda, documento, programador, orden,
 *    pensando, escribiendo, voz, hecho); el de ahora late y los hechos quedan encendidos. gui_fase("...") la alimenta desde
 *    cualquier hilo. */

#define PR_SRC_MAX (384u * 1024u)                    /* SOMA_260_LLM_TAM: como agent_last_response (antes 16 KB) */
#define PR_THK_MAX 32768
#define PR_CEL_MAX (384u * 1024u)
#define PR_LIN_MAX 24576

#define AT_B     0x0001u
#define AT_I     0x0002u
#define AT_U     0x0004u
#define AT_CODE  0x0008u
#define AT_MATH  0x0010u
#define AT_SUP   0x0020u
#define AT_SUB   0x0040u
#define AT_H1    0x0080u
#define AT_H2    0x0100u
#define AT_QUOTE 0x0200u
#define AT_THK   0x0400u
#define AT_BLK   0x0800u     /* bloque de codigo */
#define AT_HR    0x1000u     /* linea horizontal */
#define AT_LINK  0x2000u
#define AT_DISP  0x4000u     /* formula en su propia linea (centrada) */

struct pr_cel { uint32_t cp; uint16_t at; uint16_t adv; };     /* adv: avance en 1/64 px (lo pone la maquetacion) */
struct pr_lin { uint32_t ini, n; int16_t sangria; uint16_t alto, ancho; uint16_t at; };

static char pr_src[PR_SRC_MAX]; static uint32_t pr_src_n;
static char pr_thk[PR_THK_MAX]; static uint32_t pr_thk_n;
static struct pr_cel *pr_c; static uint32_t pr_n;
static struct pr_lin *pr_l; static uint32_t pr_nl;
static uint32_t pr_rev;                              /* celdas a la vista */
static uint32_t pr_n_resp;                           /* donde acaba la respuesta (detras va el razonamiento) */
static int32_t pr_scroll, pr_obj; static int pr_auto = 1;
static uint32_t pr_alto_total;
/* El texto lo escriben el hilo del agente (stream, pensamiento) y el de la GUI; las celdas y lineas solo la GUI. pr_gen cuenta los
 * cambios del texto; pr_nuevo pide volver a empezar (revelado y desplazamiento). */
static volatile uint32_t pr_gen = 1; static uint32_t pr_gen_hecho;
static volatile int pr_nuevo = 1;
static uint32_t pr_ancho_hecho, pr_letra_hecha = 99;
static int pr_listo;

int lcm_cfg_get(int que);                             /* src/agent/lcm_soma.inc: 6 = letra del orbe (0 pequena, 1 mediana, 2 grande) */

static int pr_ig(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }

static uint32_t pr_cel_max = PR_CEL_MAX, pr_lin_max = PR_LIN_MAX;
static int pr_mem(void)                                               /* SOMA_260_LLM_TAM: si no hay 3 MB, con menos */
{
    if (pr_listo) return 1;
    for (uint32_t div = 1; div <= 16; div *= 4) {
        pr_c = (struct pr_cel *)kmalloc((PR_CEL_MAX / div) * sizeof(struct pr_cel));
        pr_l = (struct pr_lin *)kmalloc((PR_LIN_MAX / div) * sizeof(struct pr_lin));
        if (pr_c && pr_l) { pr_cel_max = PR_CEL_MAX / div; pr_lin_max = PR_LIN_MAX / div; pr_listo = 1; return 1; }
        if (pr_c) kfree(pr_c);
        if (pr_l) kfree(pr_l);
        pr_c = 0; pr_l = 0;
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------------- entrada de texto */
static void pr_reset(void) { pr_src_n = 0; pr_src[0] = 0; pr_thk_n = 0; pr_thk[0] = 0; pr_nuevo = 1; pr_gen++; }
static void pr_pon(char *d, uint32_t *n, uint32_t cap, const char *s, uint32_t len)
{
    uint32_t k = *n;
    for (uint32_t i = 0; i < len && s[i] && k + 1 < cap; ++i) d[k++] = s[i];
    d[k] = 0; *n = k; __sync_synchronize(); pr_gen++;
}
/* mantener = 1: es la version final de lo que ya se estaba escribiendo (no se vuelve a revelar desde el principio) */
static void pr_respuesta(const char *s, uint32_t len, int mantener) { pr_src_n = 0; pr_pon(pr_src, &pr_src_n, PR_SRC_MAX, s, len); if (!mantener) pr_nuevo = 1; }
static void pr_anade(const char *s) { uint32_t l = 0; while (s[l]) ++l; pr_pon(pr_src, &pr_src_n, PR_SRC_MAX, s, l); }
static void pr_pensamiento(const char *s, uint32_t len) { pr_thk_n = 0; pr_pon(pr_thk, &pr_thk_n, PR_THK_MAX, s, len); }

/* ---------------------------------------------------------------------------------------------- formato */
static uint32_t pr_u8(const char *s, uint32_t *i, uint32_t n)
{
    uint8_t c = (uint8_t)s[*i];
    if (c < 0x80) { (*i)++; return c; }
    uint32_t cp, need;
    if (c >= 0xF0) { cp = c & 7u; need = 3; } else if (c >= 0xE0) { cp = c & 15u; need = 2; } else if (c >= 0xC0) { cp = c & 31u; need = 1; } else { (*i)++; return 0xFFFD; }
    (*i)++;
    while (need-- && *i < n && ((uint8_t)s[*i] & 0xC0) == 0x80) cp = (cp << 6) | ((uint8_t)s[(*i)++] & 63u);
    return cp;
}
static void pr_emite(uint32_t cp, uint16_t at)
{
    if (pr_n >= pr_cel_max - 1) return;
    if (cp == '\t') cp = ' ';
    if (cp < 32 && cp != '\n') return;
    pr_c[pr_n].cp = cp; pr_c[pr_n].at = at; pr_c[pr_n].adv = 0; pr_n++;
}
static void pr_emite_s(const char *s, uint16_t at) { uint32_t i = 0, n = 0; while (s[n]) ++n; while (i < n) pr_emite(pr_u8(s, &i, n), at); }

static const struct { const char *n; uint32_t cp; } PR_TEX[] = {
    { "alpha", 0x3B1 }, { "beta", 0x3B2 }, { "gamma", 0x3B3 }, { "delta", 0x3B4 }, { "epsilon", 0x3B5 }, { "varepsilon", 0x3B5 }, { "zeta", 0x3B6 }, { "eta", 0x3B7 },
    { "theta", 0x3B8 }, { "vartheta", 0x3D1 }, { "iota", 0x3B9 }, { "kappa", 0x3BA }, { "lambda", 0x3BB }, { "mu", 0x3BC }, { "nu", 0x3BD }, { "xi", 0x3BE },
    { "pi", 0x3C0 }, { "rho", 0x3C1 }, { "sigma", 0x3C3 }, { "tau", 0x3C4 }, { "upsilon", 0x3C5 }, { "phi", 0x3C6 }, { "varphi", 0x3C6 }, { "chi", 0x3C7 },
    { "psi", 0x3C8 }, { "omega", 0x3C9 }, { "Gamma", 0x393 }, { "Delta", 0x394 }, { "Theta", 0x398 }, { "Lambda", 0x39B }, { "Xi", 0x39E }, { "Pi", 0x3A0 },
    { "Sigma", 0x3A3 }, { "Phi", 0x3A6 }, { "Psi", 0x3A8 }, { "Omega", 0x3A9 },
    { "times", 0xD7 }, { "cdot", 0xB7 }, { "div", 0xF7 }, { "pm", 0xB1 }, { "mp", 0x2213 }, { "le", 0x2264 }, { "leq", 0x2264 }, { "ge", 0x2265 }, { "geq", 0x2265 },
    { "neq", 0x2260 }, { "ne", 0x2260 }, { "approx", 0x2248 }, { "equiv", 0x2261 }, { "sim", 0x223C }, { "propto", 0x221D }, { "infty", 0x221E },
    { "sum", 0x2211 }, { "prod", 0x220F }, { "int", 0x222B }, { "oint", 0x222E }, { "partial", 0x2202 }, { "nabla", 0x2207 }, { "in", 0x2208 }, { "notin", 0x2209 },
    { "subset", 0x2282 }, { "supset", 0x2283 }, { "subseteq", 0x2286 }, { "cup", 0x222A }, { "cap", 0x2229 }, { "forall", 0x2200 }, { "exists", 0x2203 },
    { "to", 0x2192 }, { "rightarrow", 0x2192 }, { "leftarrow", 0x2190 }, { "Rightarrow", 0x21D2 }, { "Leftarrow", 0x21D0 }, { "Leftrightarrow", 0x21D4 },
    { "leftrightarrow", 0x2194 }, { "implies", 0x21D2 }, { "iff", 0x21D4 }, { "mapsto", 0x21A6 }, { "ldots", 0x2026 }, { "cdots", 0x22EF }, { "dots", 0x2026 },
    { "circ", 0xB0 }, { "degree", 0xB0 }, { "angle", 0x2220 }, { "perp", 0x22A5 }, { "parallel", 0x2225 }, { "emptyset", 0x2205 }, { "varnothing", 0x2205 },
    { "hbar", 0x210F }, { "ell", 0x2113 }, { "prime", 0x2032 }, { "neg", 0xAC }, { "land", 0x2227 }, { "lor", 0x2228 }, { "wedge", 0x2227 }, { "vee", 0x2228 },
    { "langle", 0x27E8 }, { "rangle", 0x27E9 }, { "lfloor", 0x230A }, { "rfloor", 0x230B }, { "lceil", 0x2308 }, { "rceil", 0x2309 }, { "star", 0x22C6 },
    { "bullet", 0x2022 }, { "sqrt", 0x221A }, { "triangle", 0x25B3 }, { "square", 0x25A1 }, { "checkmark", 0x2713 }, { "euro", 0x20AC }, { 0, 0 }
};

/* LaTeX basico -> celdas con AT_MATH (y AT_SUP / AT_SUB). Devuelve el indice tras el bloque. */
static uint32_t pr_tex(const char *s, uint32_t i, uint32_t fin, uint16_t at);
static uint32_t pr_tex_grupo(const char *s, uint32_t i, uint32_t fin, uint16_t at)   /* {...} o un solo simbolo */
{
    while (i < fin && s[i] == ' ') ++i;
    if (i < fin && s[i] == '{') {
        uint32_t d = 1, j = i + 1;
        while (j < fin && d) { if (s[j] == '{') ++d; else if (s[j] == '}') --d; if (d) ++j; }
        pr_tex(s, i + 1, j, at);
        return j < fin ? j + 1 : j;
    }
    if (i < fin && s[i] == '\\') {
        uint32_t j = i + 1; while (j < fin && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z'))) ++j;
        if (j == i + 1 && j < fin) ++j;
        pr_tex(s, i, j, at);
        return j;
    }
    if (i < fin) { uint32_t k = i; uint32_t cp = pr_u8(s, &k, fin); pr_emite(cp, at); return k; }
    return i;
}
static uint32_t pr_tex_nombre(const char *s, uint32_t i, uint32_t fin, char *nm, uint32_t cap)   /* \nombre -> nombre; indice tras el */
{
    uint32_t j = i + 1, k = 0;
    while (j < fin && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z'))) { if (k + 1 < cap) nm[k++] = s[j]; ++j; }
    nm[k] = 0;
    return j;
}
static int pr_griega(const char *nm)                                     /* \alpha, \Delta... */
{
    for (int t = 0; PR_TEX[t].n; ++t) if (pr_ig(nm, PR_TEX[t].n)) return PR_TEX[t].cp >= 0x391 && PR_TEX[t].cp <= 0x3D1;
    return 0;
}
static int pr_tex_simple(const char *s, uint32_t i, uint32_t fin)          /* el grupo se lee solo, sin parentesis */
{
    while (i < fin && s[i] == ' ') ++i;
    if (i < fin && s[i] == '{') {
        if (i + 1 < fin && s[i + 1] == '\\') {                              /* {\pi} o {\sqrt{...}}: una sola pieza */
            char nm[24]; uint32_t j = pr_tex_nombre(s, i + 1, fin, nm, sizeof nm);
            if (pr_ig(nm, "sqrt") && j < fin && s[j] == '{') {
                uint32_t d = 1; ++j; while (j < fin && d) { if (s[j] == '{') ++d; else if (s[j] == '}') --d; ++j; }
                return j < fin && s[j] == '}';
            }
            return pr_griega(nm) && j < fin && s[j] == '}';
        }
        uint32_t j = i + 1, k = 0; while (j < fin && s[j] != '}') { if (s[j] == '{' || s[j] == '\\' || s[j] == '+' || s[j] == '-' || s[j] == ' ') return 0; ++j; ++k; }
        return k <= 3;
    }
    return 1;
}
static uint32_t pr_tex(const char *s, uint32_t i, uint32_t fin, uint16_t at)
{
    at |= AT_MATH;
    while (i < fin) {
        char c = s[i];
        if (c == '\\') {
            uint32_t j = i + 1; while (j < fin && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z'))) ++j;
            uint32_t L = j - i - 1;
            if (!L) {                                                   /* \, \; \! \\ \{ \} \% \$ */
                char d = (i + 1 < fin) ? s[i + 1] : 0;
                if (d == '\\') pr_emite('\n', at); else if (d == ',' || d == ';' || d == ' ' || d == ':') pr_emite(' ', at); else if (d && d != '!') pr_emite((uint8_t)d, at);
                i += 2; continue;
            }
            char nm[24]; uint32_t k = 0; for (; k < L && k + 1 < sizeof nm; ++k) nm[k] = s[i + 1 + k]; nm[k] = 0;
            i = j;
#define PR_ES(x) pr_ig(nm, x)
            if (PR_ES("frac") || PR_ES("dfrac") || PR_ES("tfrac")) {          /* a/b; (a+b)/(c+d) si no son simples */
                int sa = pr_tex_simple(s, i, fin);
                if (!sa) pr_emite('(', at);
                i = pr_tex_grupo(s, i, fin, at);
                if (!sa) pr_emite(')', at);
                int sb = pr_tex_simple(s, i, fin);
                pr_emite('/', at);
                if (!sb) pr_emite('(', at);
                i = pr_tex_grupo(s, i, fin, at);
                if (!sb) pr_emite(')', at);
                continue;
            }
            if (PR_ES("sqrt")) {
                if (i < fin && s[i] == '[') { uint32_t q = i + 1; while (q < fin && s[q] != ']') ++q; uint32_t z = i + 1; at |= AT_SUP; while (z < q) { pr_emite(pr_u8(s, &z, q), at); } at &= (uint16_t)~AT_SUP; i = q < fin ? q + 1 : q; }
                pr_emite(0x221A, at);
                int sim = pr_tex_simple(s, i, fin);
                if (!sim) pr_emite('(', at);
                i = pr_tex_grupo(s, i, fin, at);
                if (!sim) pr_emite(')', at);
                continue;
            }
            if (PR_ES("text") || PR_ES("mathrm") || PR_ES("mathit") || PR_ES("operatorname") || PR_ES("textrm") || PR_ES("mbox")) { i = pr_tex_grupo(s, i, fin, (uint16_t)(at & ~AT_MATH)); continue; }
            if (PR_ES("mathbf") || PR_ES("textbf") || PR_ES("boldsymbol")) { i = pr_tex_grupo(s, i, fin, (uint16_t)(at | AT_B)); continue; }
            if (PR_ES("mathbb")) {                                           /* \mathbb{R} -> R doble */
                while (i < fin && s[i] == ' ') ++i;
                char l = 0;
                if (i < fin && s[i] == '{') { if (i + 1 < fin) l = s[i + 1]; while (i < fin && s[i] != '}') ++i; if (i < fin) ++i; }
                else if (i < fin) l = s[i++];
                uint32_t cp = l == 'R' ? 0x211D : l == 'N' ? 0x2115 : l == 'Z' ? 0x2124 : l == 'Q' ? 0x211A : l == 'C' ? 0x2102 : (uint8_t)l;
                if (cp) pr_emite(cp, at);
                continue;
            }
            if (PR_ES("left") || PR_ES("right") || PR_ES("big") || PR_ES("Big") || PR_ES("displaystyle") || PR_ES("limits") || PR_ES("nolimits")) continue;
            if (PR_ES("quad") || PR_ES("qquad")) { pr_emite(' ', at); pr_emite(' ', at); continue; }
            if (PR_ES("sin") || PR_ES("cos") || PR_ES("tan") || PR_ES("log") || PR_ES("ln") || PR_ES("exp") || PR_ES("lim") || PR_ES("max") || PR_ES("min") || PR_ES("det")) { pr_emite_s(nm, (uint16_t)(at & ~AT_MATH)); continue; }
            uint32_t cp = 0; for (int t = 0; PR_TEX[t].n; ++t) if (PR_ES(PR_TEX[t].n)) { cp = PR_TEX[t].cp; break; }
#undef PR_ES
            if (cp) pr_emite(cp, at); else pr_emite_s(nm, at);
            if (cp >= 0x391 && cp <= 0x3D1 && i + 1 < fin && s[i] == ' ') {       /* "\Delta t" -> "Δt" (en LaTeX ese espacio no se ve) */
                char sig = s[i + 1], nm2[24];
                if ((sig >= 'a' && sig <= 'z') || (sig >= 'A' && sig <= 'Z') || (sig >= '0' && sig <= '9') || (sig == '\\' && (pr_tex_nombre(s, i + 1, fin, nm2, sizeof nm2), pr_griega(nm2)))) ++i;
            }
            continue;
        }
        if (c == '^' || c == '_') {
            uint16_t a2 = (uint16_t)(at | (c == '^' ? AT_SUP : AT_SUB));
            ++i;
            if (c == '^' && i + 6 <= fin && s[i] == '{' && s[i + 1] == '\\' && s[i + 2] == 'c' && s[i + 3] == 'i' && s[i + 4] == 'r' && s[i + 5] == 'c') { pr_emite(0xB0, at); while (i < fin && s[i] != '}') ++i; ++i; continue; }
            if (c == '^' && i + 5 <= fin && s[i] == '\\' && s[i + 1] == 'c' && s[i + 2] == 'i' && s[i + 3] == 'r' && s[i + 4] == 'c') { pr_emite(0xB0, at); i += 5; continue; }
            i = pr_tex_grupo(s, i, fin, a2);
            continue;
        }
        if (c == '{' || c == '}') { ++i; continue; }
        uint32_t cp = pr_u8(s, &i, fin);
        if (cp == '*') cp = 0xB7;                   /* producto */
        if (cp == '-' ) cp = 0x2212;                /* menos de verdad */
        pr_emite(cp, at);
    }
    return i;
}

static int pr_esp(char c) { return c == ' ' || c == '\t' || c == '\n' || c == 0; }
static int pr_signo(char c) { return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' || c == ')' || c == '"' || c == '\''; }
static int pr_empieza(const char *s, uint32_t i, uint32_t n, const char *t) { while (*t) { if (i >= n || s[i] != *t) return 0; ++i; ++t; } return 1; }
static uint32_t pr_busca(const char *s, uint32_t i, uint32_t n, const char *t)   /* posicion de t en [i, n) o n */
{
    for (; i < n; ++i) if (pr_empieza(s, i, n, t)) return i;
    return n;
}
/* busca el cierre de una marca en la misma linea */
static uint32_t pr_cierre(const char *s, uint32_t i, uint32_t n, const char *t)
{
    uint32_t tl = 0; while (t[tl]) ++tl;
    for (uint32_t j = i; j + tl <= n && s[j] != '\n'; ++j) if (pr_empieza(s, j, n, t) && j > i && !pr_esp(s[j - 1])) return j;
    return n;
}
/* cierre de una cursiva (* o _ sueltos) en la misma linea: pegado a la palabra y seguido de espacio o signo; salta ** y __ */
static uint32_t pr_cierre1(const char *s, uint32_t i, uint32_t n, char c)
{
    for (uint32_t j = i; j < n && s[j] != '\n'; ++j) {
        if (s[j] != c) continue;
        if (j + 1 < n && s[j + 1] == c) { ++j; continue; }
        if (j > i && !pr_esp(s[j - 1]) && (j + 1 >= n || pr_esp(s[j + 1]) || pr_signo(s[j + 1]))) return j;
    }
    return n;
}
/* "$...$": matematica si cierra en la misma linea, no empieza/acaba en espacio y no parece dinero ("cuesta $5 y $10") */
static uint32_t pr_dolar(const char *s, uint32_t i, uint32_t n)
{
    if (i + 1 >= n || pr_esp(s[i + 1])) return n;
    for (uint32_t j = i + 1; j < n && s[j] != '\n'; ++j)
        if (s[j] == '$' && !pr_esp(s[j - 1]) && s[j - 1] != '\\') {
            int letra = 0, num_solo = 1;
            for (uint32_t k = i + 1; k < j; ++k) { char c = s[k]; if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\\' || c == '^' || c == '_' || c == '=') letra = 1; if (!((c >= '0' && c <= '9') || c == '.' || c == ',')) num_solo = 0; }
            if (num_solo && !letra) return n;
            if (j + 1 < n && s[j + 1] >= '0' && s[j + 1] <= '9') return n;
            return j;
        }
    return n;
}

/* Un texto (respuesta o razonamiento) a celdas con formato. */
static void pr_formatea(const char *s, uint32_t n, uint16_t base)
{
    uint32_t i = 0; int en_bloque = 0;
    while (i < n) {
        /* ---- principio de linea */
        uint32_t ini = i;
        if (pr_empieza(s, i, n, "```")) {                                  /* bloque de codigo */
            en_bloque = !en_bloque;
            while (i < n && s[i] != '\n') ++i;
            if (i < n) ++i;
            continue;
        }
        if (en_bloque) {
            while (i < n && s[i] != '\n') { pr_emite(pr_u8(s, &i, n), (uint16_t)(base | AT_CODE | AT_BLK)); }
            pr_emite('\n', (uint16_t)(base | AT_BLK)); if (i < n) ++i;
            continue;
        }
        if (pr_empieza(s, i, n, "$$") || pr_empieza(s, i, n, "\\[")) {       /* formula aparte */
            const char *fin_m = s[i] == '$' ? "$$" : "\\]";
            uint32_t j = pr_busca(s, i + 2, n, fin_m);
            pr_tex(s, i + 2, j, (uint16_t)(base | AT_DISP));
            pr_emite('\n', (uint16_t)(base | AT_DISP));
            i = j < n ? j + 2 : n;
            while (i < n && (s[i] == ' ' || s[i] == '\n')) { if (s[i] == '\n') { ++i; break; } ++i; }
            continue;
        }
        uint16_t lin = base;
        { uint32_t k = i; int h = 0; while (k < n && s[k] == '#' && h < 6) { ++k; ++h; }
          if (h && k < n && s[k] == ' ') { lin |= (h == 1) ? AT_H1 : AT_H2; lin |= AT_B; i = k + 1; } }
        if (pr_empieza(s, i, n, "> ")) { lin |= AT_QUOTE; i += 2; }
        { uint32_t k = i; while (k < n && s[k] == ' ') ++k;
          if (pr_empieza(s, k, n, "- ") || pr_empieza(s, k, n, "* ") || pr_empieza(s, k, n, "+ ")) {
              for (uint32_t z = i; z < k; z += 2) pr_emite(' ', lin), pr_emite(' ', lin);
              pr_emite(0x2022, lin); pr_emite(' ', lin); i = k + 2;
          } }
        if ((pr_empieza(s, i, n, "---") || pr_empieza(s, i, n, "***") || pr_empieza(s, i, n, "___")) && ini == i) {
            uint32_t k = i; char m = s[i]; while (k < n && s[k] == m) ++k;
            if (k >= n || s[k] == '\n') { pr_emite(' ', (uint16_t)(base | AT_HR)); pr_emite('\n', base); i = k < n ? k + 1 : n; continue; }
        }
        /* ---- dentro de la linea */
        uint16_t at = lin;
        while (i < n && s[i] != '\n') {
            char c = s[i];
            if (c == '\\' && i + 1 < n && (s[i + 1] == '*' || s[i + 1] == '_' || s[i + 1] == '`' || s[i + 1] == '$' || s[i + 1] == '#' || s[i + 1] == '\\')) { pr_emite((uint8_t)s[i + 1], at); i += 2; continue; }
            if (c == '\\' && i + 1 < n && s[i + 1] == '(') { uint32_t j = pr_busca(s, i + 2, n, "\\)"); pr_tex(s, i + 2, j, at); i = j < n ? j + 2 : n; continue; }
            if (c == '$') { uint32_t j = pr_dolar(s, i, n); if (j < n) { pr_tex(s, i + 1, j, at); i = j + 1; continue; } }
            if (c == '`') { uint32_t eol = pr_busca(s, i + 1, n, "\n"); uint32_t j = pr_busca(s, i + 1, eol, "`"); if (j < eol) { uint32_t k = i + 1; while (k < j) pr_emite(pr_u8(s, &k, j), (uint16_t)(at | AT_CODE)); i = j + 1; continue; } }
            if ((c == '*' && i + 1 < n && s[i + 1] == '*') || (c == '_' && i + 1 < n && s[i + 1] == '_' && (i == 0 || pr_esp(s[i - 1]) || (at & AT_B)))) {
                const char *m = c == '*' ? "**" : "__";
                if (at & AT_B && !(lin & AT_B)) { at &= (uint16_t)~AT_B; i += 2; continue; }
                if (!(at & AT_B) && pr_cierre(s, i + 2, n, m) < n && !pr_esp(s[i + 2])) { at |= AT_B; i += 2; continue; }
            }
            if ((c == '*' || c == '_') && !(i + 1 < n && s[i + 1] == c)) {
                int izq = (i == 0 || pr_esp(s[i - 1]) || s[i - 1] == '(' || s[i - 1] == '"');
                if ((at & AT_I) && (i + 1 >= n || pr_esp(s[i + 1]) || pr_signo(s[i + 1]))) { at &= (uint16_t)~AT_I; ++i; continue; }
                if (!(at & AT_I) && izq && i + 1 < n && !pr_esp(s[i + 1])) {
                    if (pr_cierre1(s, i + 1, n, c) < n) { at |= AT_I; ++i; continue; }
                }
            }
            if (c == '<') {
                if (pr_empieza(s, i, n, "<b>") || pr_empieza(s, i, n, "<strong>")) { at |= AT_B; i = pr_busca(s, i, n, ">") + 1; continue; }
                if (pr_empieza(s, i, n, "</b>") || pr_empieza(s, i, n, "</strong>")) { at &= (uint16_t)~AT_B; i = pr_busca(s, i, n, ">") + 1; continue; }
                if (pr_empieza(s, i, n, "<i>") || pr_empieza(s, i, n, "<em>")) { at |= AT_I; i = pr_busca(s, i, n, ">") + 1; continue; }
                if (pr_empieza(s, i, n, "</i>") || pr_empieza(s, i, n, "</em>")) { at &= (uint16_t)~AT_I; i = pr_busca(s, i, n, ">") + 1; continue; }
                if (pr_empieza(s, i, n, "<u>")) { at |= AT_U; i += 3; continue; }
                if (pr_empieza(s, i, n, "</u>")) { at &= (uint16_t)~AT_U; i += 4; continue; }
                if (pr_empieza(s, i, n, "<br>") || pr_empieza(s, i, n, "<br/>") || pr_empieza(s, i, n, "<br />")) { pr_emite('\n', at); i = pr_busca(s, i, n, ">") + 1; continue; }
            }
            if (c == '[') {                                                      /* [texto](url) -> texto subrayado */
                uint32_t eol = pr_busca(s, i + 1, n, "\n");
                uint32_t j = pr_busca(s, i + 1, eol, "]");
                if (j < eol && j + 1 < n && s[j + 1] == '(') {
                    uint32_t k = pr_busca(s, j + 2, eol, ")");
                    if (k < eol) { uint32_t z = i + 1; while (z < j) pr_emite(pr_u8(s, &z, j), (uint16_t)(at | AT_LINK | AT_U)); i = k + 1; continue; }
                }
            }
            pr_emite(pr_u8(s, &i, n), at);
        }
        pr_emite('\n', base);
        if (i < n) ++i;
    }
}

/* ---------------------------------------------------------------------------------------------- medidas */
static const uint8_t PR_EM[3] = { 16, 20, 26 }, PR_LH[3] = { 24, 30, 38 };
static int pr_letra(void) { int v = lcm_cfg_get(6); return v < 0 || v > 2 ? 2 : v; }
static int pr_vec(void) { return wf_init(); }
/* WF_RAW: sin los ajustes de letra de la interfaz (se mide y se dibuja con lo mismo) */
static int pr_fl(uint16_t at)
{
    if (at & AT_CODE) return WF_RAW | WF_MONO;
    if (at & AT_MATH) return WF_RAW | ((at & AT_B) ? WF_BOLD : 0);          /* DejaVu Sans: tiene todos los simbolos */
    return WF_RAW | WF_LIB | ((at & AT_B) ? WF_BOLD : 0) | ((at & AT_I) ? WF_ITALIC : 0);
}
static int pr_em(uint16_t at, int L)
{
    int em = PR_EM[L];
    if (at & AT_H1) em = em * 13 / 10; else if (at & AT_H2) em = em * 23 / 20;
    if (at & (AT_SUP | AT_SUB)) em = em * 5 / 8;
    if (at & AT_THK) em = em * 4 / 5;
    if (at & AT_CODE) em = em * 9 / 10;
    return em;
}
static int pr_mide(const struct pr_cel *c, int L)                          /* avance en 1/64 px */
{
    if (c->at & AT_HR) return 0;
    if (!pr_vec()) return 16 * 64;
    int a = wf_cp_adv64(c->cp, pr_em(c->at, L), pr_fl(c->at));
    return a < 0 ? 0 : (a > 65535 ? 65535 : a);
}

/* parte el texto en lineas para un ancho dado (px) */
static void pr_maqueta(uint32_t ancho_px, int L)
{
    pr_nl = 0; pr_alto_total = 0;
    for (uint32_t k = 0; k < pr_n; ++k) pr_c[k].adv = (uint16_t)pr_mide(&pr_c[k], L);
    int32_t ancho = (int32_t)ancho_px * 64;
    uint32_t i = 0;
    while (i < pr_n && pr_nl < pr_lin_max) {
        uint32_t fin = i; while (fin < pr_n && pr_c[fin].cp != '\n') ++fin;      /* un parrafo */
        uint16_t at0 = (i < fin) ? pr_c[i].at : (fin < pr_n ? pr_c[fin].at : 0);
        int32_t sangria = 0;                                            /* sangria colgante de las listas: "• " */
        { uint32_t k = i; int32_t esp = 0; while (k < fin && pr_c[k].cp == ' ') { esp += pr_c[k].adv; ++k; }
          if (k + 1 < fin && pr_c[k].cp == 0x2022) sangria = esp + pr_c[k].adv + pr_c[k + 1].adv; }
        int32_t cita = (at0 & AT_QUOTE) ? 14 * 64 : 0;
        sangria += cita;
        uint32_t lh = PR_LH[L];
        if (at0 & AT_H1) lh = lh * 13 / 10; else if (at0 & AT_H2) lh = lh * 23 / 20;
        if (at0 & AT_THK) lh = lh * 4 / 5;
        if (i == fin) {                                                 /* linea vacia: media altura */
            struct pr_lin *ln = &pr_l[pr_nl++];
            ln->ini = i; ln->n = 0; ln->sangria = 0; ln->alto = (uint16_t)(lh / 2); ln->ancho = 0; ln->at = at0;
            pr_alto_total += lh / 2;
        }
        int primera = 1;
        while (i < fin && pr_nl < pr_lin_max) {
            int32_t x0 = primera ? cita : sangria, x = x0, ancho_esp = 0; int64_t ult_esp = -1;
            uint32_t j = i;
            while (j < fin) {
                int32_t a = pr_c[j].adv;
                if (x + a > ancho && j > i) break;
                if (pr_c[j].cp == ' ') { ult_esp = (int64_t)j; ancho_esp = x; }
                x += a; ++j;
            }
            uint32_t corte = j, sig = j;
            if (j < fin && ult_esp > (int64_t)i) { corte = (uint32_t)ult_esp; sig = corte + 1; x = ancho_esp; }
            struct pr_lin *ln = &pr_l[pr_nl++];
            ln->ini = i; ln->n = corte - i; ln->sangria = (int16_t)(x0 / 64);
            ln->alto = (uint16_t)lh; ln->ancho = (uint16_t)((x - x0 + 63) / 64); ln->at = at0;
            if ((at0 & AT_H1) && primera && pr_nl > 1) ln->alto = (uint16_t)(lh + lh / 3);   /* aire antes de un titulo */
            pr_alto_total += ln->alto;
            i = sig; primera = 0;
        }
        i = fin + 1;
    }
}

static void pr_prepara(uint32_t ancho)
{
    if (!pr_mem()) return;
    int L = pr_letra();
    if (pr_nuevo) { pr_nuevo = 0; pr_rev = 0; pr_scroll = pr_obj = 0; pr_auto = 1; }
    uint32_t g = pr_gen; __sync_synchronize();
    int cambia = g != pr_gen_hecho;
    if (cambia) {
        pr_gen_hecho = g;
        pr_n = 0;
        uint32_t ns = pr_src_n, nt = pr_thk_n;
        if (ns > PR_SRC_MAX - 1) ns = PR_SRC_MAX - 1;
        if (nt > PR_THK_MAX - 1) nt = PR_THK_MAX - 1;
        if (ns) pr_formatea(pr_src, ns, 0);
        while (pr_n && pr_c[pr_n - 1].cp == '\n') pr_n--;
        pr_n_resp = pr_n;
        if (nt) {
            if (pr_n) { pr_emite('\n', 0); pr_emite(' ', AT_HR | AT_THK); pr_emite('\n', AT_THK); }
            pr_formatea(pr_thk, nt, AT_THK);
        }
        while (pr_n && pr_c[pr_n - 1].cp == '\n') pr_n--;
        if (pr_rev > pr_n) pr_rev = pr_n;
    }
    if (cambia || ancho != pr_ancho_hecho || L != (int)pr_letra_hecha) { pr_maqueta(ancho, L); pr_ancho_hecho = ancho; pr_letra_hecha = (uint32_t)L; }
}

/* ---------------------------------------------------------------------------------------------- desplazamiento */
static int32_t pr_vista;                                   /* alto de la zona de texto (lo pone el dibujo) */
static void pr_desplaza(int32_t d)
{
    if (!pr_n || pr_alto_total <= (uint32_t)pr_vista) return;
    pr_auto = 0;
    pr_obj += d;
}
/* raton en el modo orbe (sin puntero): arriba = ver lo anterior */
static void her_pr_raton(int32_t dy) { if (dy) pr_desplaza(-dy * 2); }
static void her_pr_rueda(int w) { if (w) pr_desplaza((int32_t)w * (int32_t)PR_LH[pr_letra()] * 2); }   /* rueda > 0: bajar (como en Ajustes) */

static void pr_avanza(void)
{
    if (pr_rev < pr_n) {
        uint32_t paso = 2u + (pr_n - pr_rev) / 50u;
        if (paso > 14u) paso = 14u;
        pr_rev += paso; if (pr_rev > pr_n) pr_rev = pr_n;
    }
    int32_t max = (int32_t)pr_alto_total - pr_vista; if (max < 0) max = 0;
    if (pr_auto && (!pr_n_resp || pr_rev <= pr_n_resp)) {                 /* sigue a la linea que se esta escribiendo de la respuesta */
        int32_t y = 0, fondo = 0;                                         /* (el razonamiento, detras, se ve bajando a mano) */
        for (uint32_t l = 0; l < pr_nl; ++l) { y += pr_l[l].alto; if (pr_l[l].ini <= pr_rev) fondo = y; }
        int32_t obj = fondo - pr_vista; if (obj < 0) obj = 0; if (obj > max) obj = max;
        pr_obj = obj;
    } else if (pr_auto) {
        if (pr_obj > max) pr_obj = max;
    } else {
        if (pr_obj < 0) pr_obj = 0;
        if (pr_obj >= max) { pr_obj = max; if (pr_rev >= pr_n) pr_auto = 1; }
    }
    int32_t d = pr_obj - pr_scroll;
    if (d > 1 || d < -1) pr_scroll += d / 3 + (d > 0 ? 1 : -1); else pr_scroll = pr_obj;
}

/* ---------------------------------------------------------------------------------------------- dibujo del texto */
static uint32_t pr_color(uint16_t at)
{
    if (at & AT_THK) return 0x0064748Bu;
    if (at & AT_CODE) return 0x00FCD34Du;
    if (at & AT_MATH) return 0x0099F6E4u;
    if (at & AT_LINK) return 0x0093C5FDu;
    if (at & AT_H1) return 0x00FFFFFFu;
    if (at & AT_H2) return 0x00E0F2FEu;
    if (at & AT_QUOTE) return 0x00A5B4C8u;
    if (at & AT_I) return 0x00C7D2FEu;
    return 0x00F1F5F9u;
}
static void pr_glifo(int32_t x, int32_t y_base, const struct pr_cel *c, uint32_t col, int L)
{
    if (c->cp == ' ' || c->cp == '\n') return;
    if (!pr_vec()) {                                                    /* sin fuente suavizada: el mapa de bits de siempre */
        uint16_t cell = her_cp_cell(c->cp);
        if (!cell) cell = '?';
        int32_t y = y_base - 24;
        her_draw_cell(x, y, cell, col, 2);
        if (c->at & AT_B) her_draw_cell(x + 1, y, cell, col, 2);
        return;
    }
    char u[4]; uint32_t n = 0, cp = c->cp;
    if (cp < 0x80) u[n++] = (char)cp;
    else if (cp < 0x800) { u[n++] = (char)(0xC0 | (cp >> 6)); u[n++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { u[n++] = (char)(0xE0 | (cp >> 12)); u[n++] = (char)(0x80 | ((cp >> 6) & 63)); u[n++] = (char)(0x80 | (cp & 63)); }
    else { u[n++] = (char)(0xF0 | (cp >> 18)); u[n++] = (char)(0x80 | ((cp >> 12) & 63)); u[n++] = (char)(0x80 | ((cp >> 6) & 63)); u[n++] = (char)(0x80 | (cp & 63)); }
    int32_t yb = y_base;
    if (c->at & AT_SUP) yb -= PR_EM[L] * 2 / 5;
    if (c->at & AT_SUB) yb += PR_EM[L] / 6;
    wf_draw(x, yb, u, n, pr_em(c->at, L), col & 0x00FFFFFFu, pr_fl(c->at), 255);
}

static void pr_dibuja_texto(int32_t x0, int32_t y0, int32_t ancho, int32_t alto)
{
    int L = (int)pr_letra_hecha;
    uint32_t tip = her_col_cur;
    fb_set_clip(x0 - 8, y0, ancho + 24, alto);
    int32_t y = y0 - pr_scroll;
    int una = pr_nl == 1;
    for (uint32_t l = 0; l < pr_nl; ++l) {
        const struct pr_lin *ln = &pr_l[l];
        int32_t yl = y; y += ln->alto;
        if (y < y0 || yl > y0 + alto) continue;
        if (ln->ini >= pr_rev && ln->n) break;
        int32_t base = yl + (int32_t)ln->alto * 3 / 4;
        if (ln->at & AT_BLK) fb_fill_rect_alpha(x0 - 6, yl, ancho + 12, ln->alto, 0xC0050A12u);
        if (ln->at & AT_QUOTE) fb_fill_rect((uint32_t)(x0 + 2), (uint32_t)(yl + 4), 3u, (uint32_t)(ln->alto - 8), 0x00475569u);
        int32_t xi = x0 + ln->sangria;
        if (una || (ln->at & (AT_DISP | AT_H1))) xi = x0 + (ancho - (int32_t)ln->ancho) / 2;
        int64_t pluma = (int64_t)xi * 64;
        for (uint32_t k = 0; k < ln->n; ++k) {
            uint32_t gi = ln->ini + k;
            if (gi >= pr_rev) break;
            const struct pr_cel *c = &pr_c[gi];
            int32_t x = (int32_t)((pluma + 32) >> 6), a = (c->adv + 32) >> 6;
            pluma += c->adv;
            if (c->at & AT_HR) { fb_fill_rect((uint32_t)x0, (uint32_t)(yl + ln->alto / 2), (uint32_t)ancho, 1u, (c->at & AT_THK) ? 0x00283448u : 0x00334155u); continue; }
            uint32_t col = pr_color(c->at);
            uint32_t d = pr_rev - gi;
            if (d < 14u && pr_rev < pr_n) col = her_mix256(tip, col, d * 256u / 14u);   /* la punta que se esta escribiendo */
            if ((c->at & AT_CODE) && !(c->at & AT_BLK)) fb_fill_rect_alpha(x, yl + 3, a, ln->alto - 6, 0x70101826u);
            pr_glifo(x, base, c, col, L);
            if (c->at & AT_U) fb_fill_rect((uint32_t)x, (uint32_t)(base + 3), (uint32_t)(a > 0 ? a : 1), 1u, col);
        }
    }
    fb_reset_clip();
    /* desvanecido arriba y abajo cuando hay mas texto, y barra de posicion */
    if (pr_scroll > 0) for (int k = 0; k < 18; ++k) fb_fill_rect_alpha(x0 - 8, y0 + k, ancho + 24, 1, ((uint32_t)(255 - k * 14) << 24) | 0x00030714u);
    if (pr_scroll + alto < (int32_t)pr_alto_total) for (int k = 0; k < 18; ++k) fb_fill_rect_alpha(x0 - 8, y0 + alto - 1 - k, ancho + 24, 1, ((uint32_t)(255 - k * 14) << 24) | 0x00030714u);
    if ((int32_t)pr_alto_total > alto) {
        int32_t bh = alto * alto / (int32_t)pr_alto_total; if (bh < 24) bh = 24;
        int32_t by = y0 + (alto - bh) * pr_scroll / ((int32_t)pr_alto_total - alto);
        fb_fill_rrect_aa(x0 + ancho + 14, y0, 3, alto, 1, 0x40334155u);
        fb_fill_rrect_aa(x0 + ancho + 13, by, 5, bh, 2, pr_auto ? 0x9064748Bu : 0xE07DD3FCu);
    }
}

/* ---------------------------------------------------------------------------------------------- barra de fases */
enum { FA_LCM = 0, FA_MAYORDOMO, FA_BUSCAR, FA_DOCUMENTO, FA_PROGRAMADOR, FA_AGENTE, FA_ORDEN, FA_PENSAR, FA_ESCRIBIR, FA_VOZ, FA_HECHO, FA_N };
#define PR_FMAX 9
static volatile int pr_f[PR_FMAX];
static volatile int pr_nf;
static volatile uint32_t pr_f_t;

static int pr_fase_de(const char *q)
{
    if (!q) return -1;
    char f[16]; int n = 0; while (q[n] && n < 15) { char c = q[n]; f[n] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c); ++n; } f[n] = 0;
#define PR_Q(x) pr_ig(f, x)
    if (PR_Q("lcm")) return FA_LCM;
    if (PR_Q("mayordomo")) return FA_MAYORDOMO;
    if (PR_Q("buscar") || PR_Q("buscador") || PR_Q("web") || PR_Q("internet") || PR_Q("investigador")) return FA_BUSCAR;
    if (PR_Q("documento") || PR_Q("maquetador")) return FA_DOCUMENTO;
    if (PR_Q("programador") || PR_Q("coder")) return FA_PROGRAMADOR;
    if (PR_Q("orden")) return FA_ORDEN;
    if (PR_Q("pensar")) return FA_PENSAR;
    if (PR_Q("escribir")) return FA_ESCRIBIR;
    if (PR_Q("voz")) return FA_VOZ;
    if (PR_Q("hecho")) return FA_HECHO;
    if (PR_Q("soma") || PR_Q("")) return -1;
#undef PR_Q
    return FA_AGENTE;
}
/* Marca la fase en curso. Desde cualquier hilo (agente, LCM, GUI). Nombres: lcm, mayordomo, buscar, documento, programador,
 * <otro agente>, orden, pensar, escribir, voz, hecho. Una orden de busqueda (search, descargar, navegador) cuenta como "buscar". */
void gui_fase(const char *que);
void gui_fase(const char *que)
{
    int f = pr_fase_de(que);
    if (f < 0) return;
    int n = pr_nf;
    if (n && pr_f[n - 1] == f) return;
    if (n && pr_f[n - 1] == FA_HECHO && f != FA_VOZ) return;          /* la peticion ya acabo */
    if (n >= PR_FMAX) { for (int i = 1; i < PR_FMAX; ++i) pr_f[i - 1] = pr_f[i]; n = PR_FMAX - 1; }
    pr_f[n] = f; pr_nf = n + 1;
    pr_f_t = (uint32_t)timer_get_uptime_ms();
}
void gui_fase_orden(const char *linea);
void gui_fase_orden(const char *linea)
{
    char nom[16]; int n = 0; while (linea && linea[n] && linea[n] != ' ' && n < 15) { nom[n] = linea[n]; ++n; } nom[n] = 0;
    if (pr_ig(nom, "search") || pr_ig(nom, "descargar") || pr_ig(nom, "navegador") || pr_ig(nom, "fetch") || pr_ig(nom, "web")) gui_fase("buscar");
    else gui_fase("orden");
}

/* iconos (rejilla 48x48 centrada en cx, cy; lado s) */
static int32_t pi_cx, pi_cy, pi_s; static uint32_t pi_col;
#define PGX(u) ((pi_cx * 16) + (((int32_t)(u) * 2 - 48) * pi_s * 16) / 96)
#define PGY(u) ((pi_cy * 16) + (((int32_t)(u) * 2 - 48) * pi_s * 16) / 96)
static void pi_L(int x0, int y0, int x1, int y1) { fb_line_aa(PGX(x0), PGY(y0), PGX(x1), PGY(y1), pi_s * 16 / 13 + 4, pi_col); }
static void pi_arco(int cx, int cy, int r, uint32_t a0, uint32_t a1)          /* a en 0..1023 */
{
    uint32_t pasos = 16, d = (a1 - a0) / pasos;
    for (uint32_t k = 0; k < pasos; ++k) {
        uint32_t a = a0 + k * d, b = a + d;
        int x0 = cx * 64 + her_cs(a) * r / 16, y0 = cy * 64 + her_sn(a) * r / 16, x1 = cx * 64 + her_cs(b) * r / 16, y1 = cy * 64 + her_sn(b) * r / 16;
        fb_line_aa((pi_cx * 16) + ((x0 * 2 / 64 - 48) * pi_s * 16) / 96, (pi_cy * 16) + ((y0 * 2 / 64 - 48) * pi_s * 16) / 96,
                   (pi_cx * 16) + ((x1 * 2 / 64 - 48) * pi_s * 16) / 96, (pi_cy * 16) + ((y1 * 2 / 64 - 48) * pi_s * 16) / 96, pi_s * 16 / 13 + 4, pi_col);
    }
}
static void pr_icono(int f, int32_t cx, int32_t cy, int32_t s, uint32_t col)
{
    pi_cx = cx; pi_cy = cy; pi_s = s; pi_col = col | 0xFF000000u;
    switch (f) {
    case FA_LCM: {                                                      /* insignia "LCM" */
        int32_t w = s + s / 3, h = s * 2 / 3;
        fb_stroke_rrect_aa(cx - w / 2, cy - h / 2, w, h, h / 3, col | 0xFF000000u);
        si_text_c(cx - w / 2, cy - h / 2, w, h, "LCM", s * 9 / 20 > 8 ? s * 9 / 20 : 8, col, 1);
        break; }
    case FA_MAYORDOMO:                                                  /* pajarita y cuello */
        pi_L(8, 16, 22, 24); pi_L(22, 24, 8, 32); pi_L(8, 32, 8, 16); pi_L(40, 16, 26, 24); pi_L(26, 24, 40, 32); pi_L(40, 32, 40, 16);
        pi_L(22, 22, 26, 22); pi_L(22, 26, 26, 26); pi_L(14, 8, 24, 14); pi_L(34, 8, 24, 14); pi_L(14, 40, 34, 40); break;
    case FA_BUSCAR:      si_icon(SI_SEARCH, cx, cy, s, col); break;
    case FA_DOCUMENTO:   si_icon(SI_FILE, cx, cy, s, col); break;
    case FA_PROGRAMADOR: si_icon(SI_CODE, cx, cy, s, col); break;
    case FA_ORDEN:       si_icon(SI_TERMINAL, cx, cy, s, col); break;
    case FA_PENSAR:      si_icon(SI_SPARK, cx, cy, s, col); break;
    case FA_ESCRIBIR:    si_icon(SI_EDIT, cx, cy, s, col); break;
    case FA_VOZ:         si_icon(SI_SPEAKER, cx, cy, s, col); break;
    case FA_HECHO:       si_icon(SI_CHECK, cx, cy, s, col); break;
    default:                                                            /* otro agente: dos nodos unidos */
        pi_arco(16, 20, 7, 0, 1023); pi_arco(32, 30, 7, 0, 1023); pi_L(21, 24, 27, 27); break;
    }
}

static void pr_dibuja_barra(uint32_t cur_w, int32_t cy)
{
    int n = pr_nf;
    int busy = her_agent_busy;
    int fases[PR_FMAX + 1]; int m = 0;
    for (int i = 0; i < n && i < PR_FMAX; ++i) fases[m++] = pr_f[i];
    if (busy && m == 0) fases[m++] = FA_PENSAR;                       /* trabajando y aun nadie ha dicho que: pensando */
#if defined(__x86_64__)
    if (m && fases[m - 1] == FA_VOZ && !voz_speak_state() && !busy) { gui_fase("hecho"); fases[m++] = FA_HECHO; }
#endif
    if (!m) return;
    int32_t paso = 58, r = 17;
    int32_t x0 = (int32_t)cur_w / 2 - (m - 1) * paso / 2;
    uint32_t t = (uint32_t)timer_get_uptime_ms();
    int activa = (fases[m - 1] == FA_HECHO) ? -1 : m - 1;
    for (int i = 0; i + 1 < m; ++i) {
        uint32_t c = i + 1 == activa ? her_mix256(0x00334155u, her_col_cur, (uint32_t)(her_sn((t % 1200u) * 1024u / 1200u) + 1024) / 8u) : 0x00475569u;
        fb_line_aa((x0 + i * paso + r + 4) * 16, cy * 16, (x0 + (i + 1) * paso - r - 4) * 16, cy * 16, 20, c | 0xFF000000u);
    }
    for (int i = 0; i < m; ++i) {
        int32_t cx = x0 + i * paso;
        int act = i == activa, fin = fases[i] == FA_HECHO;
        uint32_t borde, icono;
        if (act) {
            uint32_t lat = (uint32_t)(her_sn((t % 1200u) * 1024u / 1200u) + 1024) * 255u / 2048u;
            for (int g = 3; g >= 1; --g) fb_fill_rrect_aa(cx - r - g * 3, cy - r - g * 3, 2 * (r + g * 3), 2 * (r + g * 3), r + g * 3, (((lat / 5u) / (uint32_t)g) << 24) | (her_col_cur & 0x00FFFFFFu));
            fb_fill_rrect_aa(cx - r, cy - r, 2 * r, 2 * r, r, 0xFF0B1220u);
            borde = her_col_cur; icono = 0x00FFFFFFu;
        } else if (fin) { fb_fill_rrect_aa(cx - r, cy - r, 2 * r, 2 * r, r, 0xFF0B1A14u); borde = 0x0034D399u; icono = 0x0034D399u; }
        else { fb_fill_rrect_aa(cx - r, cy - r, 2 * r, 2 * r, r, 0xFF0B1220u); borde = 0x00475569u; icono = 0x0094A3B8u; }
        fb_stroke_rrect_aa(cx - r, cy - r, 2 * r, 2 * r, r, borde | 0xFF000000u);
        pr_icono(fases[i], cx, cy, 20, icono);
    }
}

/* ---------------------------------------------------------------------------------------------- todo junto */
#define PR_BARRA_MS 12000u                                  /* la barra sigue a la vista 12 s despues de la ultima fase */
static int pr_barra_visible(void)
{
    if (her_agent_busy) return 1;
    if (!pr_nf) return 0;
#if defined(__x86_64__)
    if (voz_speak_state()) return 1;
#endif
    return (uint32_t)timer_get_uptime_ms() - pr_f_t < PR_BARRA_MS;
}
/* Sustituye al texto del orbe de gui_cosmos.c. 1 si se ha encargado (aunque no haya nada que dibujar). */
static int her_pr_dibuja(uint32_t cur_w, uint32_t cur_h, uint32_t orb_cy)
{
    if (cur_w < 240u || cur_h < 300u) return 0;                          /* pantalla diminuta: lo de antes */
    int32_t barra_y = (int32_t)orb_cy + 114;                             /* franja propia entre el orbe y la respuesta */
    if (pr_barra_visible()) pr_dibuja_barra(cur_w, barra_y);
    if (her_ans_len == 0 && her_thk_len == 0) return 1;                  /* como antes: sin texto (o ya deshecho en particulas) */
    if (!pr_src_n && !pr_thk_n) return 0;
    int32_t ancho = (int32_t)cur_w - 140; if (ancho > 980) ancho = 980;
    int32_t x0 = ((int32_t)cur_w - ancho) / 2;
    int32_t y0 = barra_y + 32, fondo = (int32_t)cur_h - 40;
    if (fondo - y0 < 60) return 0;
    pr_vista = fondo - y0;
    pr_prepara((uint32_t)ancho);
    if (!pr_listo) return 0;                                             /* sin memoria: lo de antes */
    pr_avanza();
    pr_dibuja_texto(x0, y0, ancho, pr_vista);
    return 1;
}

/* ---------------------------------------------------------------------------------------------- enganches (gui_vortice.c) */
static void her_pr_turno(void) { pr_reset(); pr_nf = 0; }                 /* pregunta nueva */
static void her_pr_pensamiento(const char *t) { uint32_t n = 0; while (t[n]) ++n; pr_pensamiento(t, n); gui_fase("pensar"); }
static void her_pr_stream_ini(void) { pr_src_n = 0; pr_src[0] = 0; pr_nuevo = 1; pr_gen++; gui_fase("escribir"); }
static void her_pr_stream(const char *c) { pr_anade(c); }
static const char *pr_tras(const char *s, const char *t)                  /* posicion de t en s, o 0 */
{
    for (; *s; ++s) { const char *a = s, *b = t; while (*a && *a == *b) { ++a; ++b; } if (!*b) return s; }
    return 0;
}
/* respuesta completa: "[Razonamiento]:\n...\n\n[Dictamen]:\n..." o el texto tal cual */
static void her_pr_fin(const char *resp)
{
    static const char tr[] = "[Razonamiento]:\n", td[] = "\n\n[Dictamen]:\n";
    if (!resp) resp = "";
    const char *ans = resp, *d = pr_tras(resp, td);
    uint32_t i = 0; while (tr[i] && resp[i] == tr[i]) i++;
    if (!tr[i] && d) {
        const char *ts = resp + sizeof(tr) - 1u;
        if (!pr_thk_n && d > ts) pr_pensamiento(ts, (uint32_t)(d - ts));
        ans = d + sizeof(td) - 1u;
    }
    if (!*ans) ans = "Mision ejecutada en hardware.";
    uint32_t n = 0; while (ans[n]) ++n;
    pr_respuesta(ans, n, pr_src_n > 0);                                  /* si ya se estaba escribiendo, no se repite el revelado */
#if defined(__x86_64__)
    if (voz_speak_state()) { gui_fase("voz"); return; }
#endif
    gui_fase("hecho");
}
/* SOMA_259_ORBE fin */
