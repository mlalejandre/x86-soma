/* PARCHE 084: motor LaTeX de SOMA. Ver tex.h.
 * Estructura: lector del texto (comandos, grupos, comentarios) -> parrafos de "items" (palabras con estilo, cajas de matematicas,
 * llamadas a notas) -> lineas justificadas; las formulas se analizan a un arbol de nodos y se maquetan con cajas (ancho, altura sobre
 * la base, profundidad) siguiendo las reglas principales de TeX (fracciones sobre el eje, indices mas pequenos, limites, espaciado
 * entre clases de atomos, delimitadores que crecen). Todo se escribe como operaciones en coordenadas de pagina. */
#include "tex.h"

/* ================================================================ estado general */
static const char *S; static uint32_t L, P;          /* texto que se esta leyendo */
static struct tex_out *O; static const struct tex_metrics *M;
static int BASE, WIDTH, Y, FULL;
static int pass_no;                                   /* 1: solo recoge etiquetas; 2: dibuja */

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int seq(const char *a, uint32_t an, const char *b) { int bn = slen(b); if ((int)an != bn) return 0; for (int i = 0; i < bn; i++) if (a[i] != b[i]) return 0; return 1; }
static int imax(int a, int b) { return a > b ? a : b; }

/* ---------------------------------------------------------------- salida */
static int op_add(int type, int x, int y, int w, int h, int px, int style, uint32_t rgb, uint32_t s, uint32_t n)
{
    if (O->nops >= O->cap) { FULL = 1; return -1; }
    if ((type == TOP_TEXT || type == TOP_IMAGE) && (s > O->npool || n > O->npool - s)) { s = 0; n = 0; }   /* SOMA_322: no cupo en el pool */
    struct tex_op *p = &O->ops[O->nops];
    p->type = (uint8_t)type; p->style = (uint8_t)style; p->px = (int16_t)px; p->x = x; p->y = y; p->w = w; p->h = h; p->rgb = rgb; p->s = s; p->n = n;
    return O->nops++;
}
static uint32_t pool_put(const char *s, uint32_t n)
{
    /* SOMA_322_TEX_HOSTIL: si no cabe devuelve el final de lo escrito (antes 0): asi s + n > npool delata que no se copio y nadie
     * lee n bytes que no existen. Antes, con el pool lleno, las operaciones de texto quedaban con s = 0 y la longitud entera, y
     * quien dibuja leia fuera del pool. */
    if (n >= O->poolcap || O->npool + n + 1 > O->poolcap) { FULL = 1; return O->npool; }
    uint32_t at = O->npool;
    for (uint32_t i = 0; i < n; i++) O->pool[at + i] = s[i];
    O->pool[at + n] = 0; O->npool += n + 1;
    return at;
}
/* como pool_put, pero si no cabe deja *n a 0 (para los pares posicion y longitud que se leen despues) */
static uint32_t pool_dupn(const char *s, uint32_t *n)
{
    uint32_t at = pool_put(s, *n);
    if (at > O->npool || *n > O->npool - at) *n = 0;
    return at;
}
static void shift_ops(int a, int b, int dx, int dy)
{
    for (int i = a; i < b; i++) { int k = O->ops[i].type == TOP_LINE ? 16 : 1; O->ops[i].x += dx * k; O->ops[i].y += dy * k; }
}
static int tw(const char *s, int n, int px, int st) { return n > 0 ? M->width(s, n, px, st, M->u) : 0; }
static int asc_of(int px) { return M->ascent ? M->ascent(px, M->u) : px * 9 / 10; }
static int desc_of(int px) { return M->descent ? M->descent(px, M->u) : px / 4; }

/* ---------------------------------------------------------------- lectura */
static int eof(void) { return P >= L; }
static char pk(void) { return P < L ? S[P] : 0; }
static void skip_comment(void) { while (P < L && S[P] != '\n') P++; if (P < L) P++; while (P < L && (S[P] == ' ' || S[P] == '\t')) P++; }
static void skip_sp(void) { for (;;) { while (P < L && (S[P] == ' ' || S[P] == '\t' || S[P] == '\n' || S[P] == '\r')) P++; if (P < L && S[P] == '%') { skip_comment(); continue; } break; } }
static void skip_hsp(void) { while (P < L && (S[P] == ' ' || S[P] == '\t')) P++; }
/* despues de '\': nombre del comando (letras) o un solo caracter */
static void read_cmd(uint32_t *s, uint32_t *n)
{
    *s = P;
    if (P < L && ((S[P] >= 'a' && S[P] <= 'z') || (S[P] >= 'A' && S[P] <= 'Z'))) { while (P < L && ((S[P] >= 'a' && S[P] <= 'z') || (S[P] >= 'A' && S[P] <= 'Z'))) P++; if (P < L && S[P] == '*') P++; }
    else if (P < L) P++;
    *n = P - *s;
}
/* {grupo} -> [a,b) del contenido (sin llaves). Sin llave: un solo token. */
static void read_group(uint32_t *a, uint32_t *b)
{
    skip_sp();
    if (pk() == '{') {
        P++; *a = P; int d = 1;
        while (P < L) { char c = S[P]; if (c == '\\') { P += 2; continue; } if (c == '{') d++; else if (c == '}') { if (--d == 0) break; } P++; }
        if (P > L) P = L;
        *b = P; if (P < L) P++;
        return;
    }
    *a = P;
    if (pk() == '\\') { P++; uint32_t s, n; read_cmd(&s, &n); }
    else if (P < L) { P++; while (P < L && ((unsigned char)S[P] & 0xC0) == 0x80) P++; }
    *b = P;
}
static int read_opt(uint32_t *a, uint32_t *b)          /* [opcional] */
{
    uint32_t save = P; skip_hsp();
    if (pk() != '[') { P = save; return 0; }
    P++; *a = P; int d = 0;
    while (P < L && !(S[P] == ']' && d == 0)) { if (S[P] == '{') d++; else if (S[P] == '}') d--; P++; }
    *b = P; if (P < L) P++;
    return 1;
}
struct rd { const char *s; uint32_t l, p; };
static struct rd push_src(uint32_t a, uint32_t b) { struct rd r = { S, L, P }; P = a; L = b; return r; }
static void pop_src(struct rd r) { S = r.s; L = r.l; P = r.p; }

/* ---------------------------------------------------------------- etiquetas y contadores */
struct lab { char name[32]; char val[16]; };
static struct lab LABS[96]; static int nlabs;
static char last_num[16];
static int sec[3], eqno, figno, tabno, fnno;
static void set_label(uint32_t a, uint32_t b)
{
    if (pass_no != 1 || nlabs >= 96) return;
    struct lab *l = &LABS[nlabs++]; int n = (int)(b - a); if (n > 31) n = 31;
    for (int i = 0; i < n; i++) l->name[i] = S[a + i];
    l->name[n] = 0;
    int k = 0; while (last_num[k] && k < 15) { l->val[k] = last_num[k]; k++; } l->val[k] = 0;
}
static const char *get_label(uint32_t a, uint32_t b)
{
    for (int i = 0; i < nlabs; i++) if (seq(S + a, b - a, LABS[i].name)) return LABS[i].val;
    return "??";
}
static void num_str(char *o, int v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); for (int i = 0; i < n; i++) o[i] = t[n - 1 - i]; o[n] = 0; }
static void cat(char *d, const char *s, int max) { int i = slen(d), j = 0; while (s[j] && i + 1 < max) d[i++] = s[j++]; d[i] = 0; }

/* ================================================================ MATEMATICAS */
enum { K_LIST = 0, K_ATOM, K_FRAC, K_SQRT, K_SCRIPT, K_DELIM, K_ACCENT, K_MATRIX, K_SPACE, K_SEP, K_NEWROW, K_ERR };
enum { C_ORD = 0, C_OP, C_BIN, C_REL, C_OPEN, C_CLOSE, C_PUNCT, C_INNER, C_NONE };
struct mn { uint8_t k, cls, sty, lim; int a, b, c, next, last; uint32_t s, n, s2, n2; int ex; };
#define MN_MAX 6144
static struct mn MN[MN_MAX]; static int nmn;
static int mnew(int k)
{
    if (nmn >= MN_MAX) { FULL = 1; return -1; }
    struct mn *m = &MN[nmn]; m->k = (uint8_t)k; m->cls = C_ORD; m->sty = 0; m->lim = 0; m->a = m->b = m->c = -1; m->next = -1; m->last = -1; m->s = m->n = m->s2 = m->n2 = 0; m->ex = 0;
    return nmn++;
}
static void mappend(int list, int child)
{
    if (list < 0 || child < 0) return;
    struct mn *l = &MN[list];
    if (l->a < 0) l->a = child; else MN[l->last].next = child;
    l->last = child;
}
/* atomo con texto propio (se guarda en el pool de salida) */
static int matom(const char *t, int n, int cls, int sty)
{
    int x = mnew(K_ATOM); if (x < 0) return -1;
    MN[x].n = (uint32_t)n; MN[x].s = pool_dupn(t, &MN[x].n); MN[x].cls = (uint8_t)cls; MN[x].sty = (uint8_t)sty;
    return x;
}

struct sym { const char *name, *u; uint8_t cls; };
static const struct sym SYMS[] = {
    /* griego (minusculas en cursiva como en TeX) */
    { "alpha", "α", C_ORD }, { "beta", "β", C_ORD }, { "gamma", "γ", C_ORD }, { "delta", "δ", C_ORD }, { "epsilon", "ϵ", C_ORD }, { "varepsilon", "ε", C_ORD },
    { "zeta", "ζ", C_ORD }, { "eta", "η", C_ORD }, { "theta", "θ", C_ORD }, { "vartheta", "ϑ", C_ORD }, { "iota", "ι", C_ORD }, { "kappa", "κ", C_ORD },
    { "lambda", "λ", C_ORD }, { "mu", "μ", C_ORD }, { "nu", "ν", C_ORD }, { "xi", "ξ", C_ORD }, { "pi", "π", C_ORD }, { "varpi", "ϖ", C_ORD }, { "rho", "ρ", C_ORD },
    { "varrho", "ϱ", C_ORD }, { "sigma", "σ", C_ORD }, { "varsigma", "ς", C_ORD }, { "tau", "τ", C_ORD }, { "upsilon", "υ", C_ORD }, { "phi", "ϕ", C_ORD },
    { "varphi", "φ", C_ORD }, { "chi", "χ", C_ORD }, { "psi", "ψ", C_ORD }, { "omega", "ω", C_ORD },
    { "Gamma", "Γ", C_ORD }, { "Delta", "Δ", C_ORD }, { "Theta", "Θ", C_ORD }, { "Lambda", "Λ", C_ORD }, { "Xi", "Ξ", C_ORD }, { "Pi", "Π", C_ORD },
    { "Sigma", "Σ", C_ORD }, { "Upsilon", "Υ", C_ORD }, { "Phi", "Φ", C_ORD }, { "Psi", "Ψ", C_ORD }, { "Omega", "Ω", C_ORD },
    /* ordinarios */
    { "infty", "∞", C_ORD }, { "partial", "∂", C_ORD }, { "nabla", "∇", C_ORD }, { "forall", "∀", C_ORD }, { "exists", "∃", C_ORD }, { "nexists", "∄", C_ORD },
    { "emptyset", "∅", C_ORD }, { "varnothing", "∅", C_ORD }, { "hbar", "ℏ", C_ORD }, { "ell", "ℓ", C_ORD }, { "Re", "ℜ", C_ORD }, { "Im", "ℑ", C_ORD },
    { "aleph", "ℵ", C_ORD }, { "prime", "′", C_ORD }, { "angle", "∠", C_ORD }, { "neg", "¬", C_ORD }, { "lnot", "¬", C_ORD }, { "top", "⊤", C_ORD }, { "bot", "⊥", C_ORD },
    { "ldots", "…", C_INNER }, { "dots", "…", C_INNER }, { "cdots", "⋯", C_INNER }, { "vdots", "⋮", C_ORD }, { "ddots", "⋱", C_ORD }, { "degree", "°", C_ORD },
    { "triangle", "△", C_ORD }, { "square", "□", C_ORD }, { "star", "⋆", C_BIN }, { "dagger", "†", C_BIN }, { "%", "%", C_ORD }, { "$", "$", C_ORD },
    { "#", "#", C_ORD }, { "&", "&", C_ORD }, { "_", "_", C_ORD }, { "{", "{", C_OPEN }, { "}", "}", C_CLOSE }, { "|", "‖", C_ORD }, { "backslash", "\\", C_ORD },
    /* binarios */
    { "pm", "±", C_BIN }, { "mp", "∓", C_BIN }, { "times", "×", C_BIN }, { "div", "÷", C_BIN }, { "cdot", "·", C_BIN }, { "ast", "∗", C_BIN }, { "circ", "∘", C_BIN },
    { "bullet", "•", C_BIN }, { "cup", "∪", C_BIN }, { "cap", "∩", C_BIN }, { "setminus", "∖", C_BIN }, { "wedge", "∧", C_BIN }, { "land", "∧", C_BIN },
    { "vee", "∨", C_BIN }, { "lor", "∨", C_BIN }, { "oplus", "⊕", C_BIN }, { "otimes", "⊗", C_BIN }, { "odot", "⊙", C_BIN },
    /* relaciones */
    { "leq", "≤", C_REL }, { "le", "≤", C_REL }, { "geq", "≥", C_REL }, { "ge", "≥", C_REL }, { "neq", "≠", C_REL }, { "ne", "≠", C_REL }, { "approx", "≈", C_REL },
    { "equiv", "≡", C_REL }, { "sim", "∼", C_REL }, { "simeq", "≃", C_REL }, { "cong", "≅", C_REL }, { "propto", "∝", C_REL }, { "ll", "≪", C_REL }, { "gg", "≫", C_REL },
    { "in", "∈", C_REL }, { "notin", "∉", C_REL }, { "ni", "∋", C_REL }, { "subset", "⊂", C_REL }, { "subseteq", "⊆", C_REL }, { "supset", "⊃", C_REL },
    { "supseteq", "⊇", C_REL }, { "to", "→", C_REL }, { "rightarrow", "→", C_REL }, { "leftarrow", "←", C_REL }, { "gets", "←", C_REL },
    { "Rightarrow", "⇒", C_REL }, { "Leftarrow", "⇐", C_REL }, { "Leftrightarrow", "⇔", C_REL }, { "leftrightarrow", "↔", C_REL }, { "iff", "⟺", C_REL },
    { "implies", "⟹", C_REL }, { "mapsto", "↦", C_REL }, { "longrightarrow", "⟶", C_REL }, { "uparrow", "↑", C_REL }, { "downarrow", "↓", C_REL },
    { "perp", "⊥", C_REL }, { "parallel", "∥", C_REL }, { "mid", "∣", C_REL }, { "vdash", "⊢", C_REL }, { "models", "⊨", C_REL }, { "coloneqq", "≔", C_REL },
    { "prec", "≺", C_REL }, { "succ", "≻", C_REL },
    /* delimitadores */
    { "langle", "⟨", C_OPEN }, { "rangle", "⟩", C_CLOSE }, { "lfloor", "⌊", C_OPEN }, { "rfloor", "⌋", C_CLOSE }, { "lceil", "⌈", C_OPEN }, { "rceil", "⌉", C_CLOSE },
    { "lbrace", "{", C_OPEN }, { "rbrace", "}", C_CLOSE }, { "vert", "|", C_ORD }, { "Vert", "‖", C_ORD }, { "lvert", "|", C_OPEN }, { "rvert", "|", C_CLOSE },
    { 0, 0, 0 }
};
static const struct sym *find_sym(const char *s, uint32_t n) { for (int i = 0; SYMS[i].name; i++) if (seq(s, n, SYMS[i].name)) return &SYMS[i]; return 0; }
struct bigop { const char *name, *u; uint8_t limits; };
static const struct bigop BIGOPS[] = {
    { "sum", "∑", 1 }, { "prod", "∏", 1 }, { "coprod", "∐", 1 }, { "int", "∫", 0 }, { "iint", "∬", 0 }, { "iiint", "∭", 0 }, { "oint", "∮", 0 },
    { "bigcup", "⋃", 1 }, { "bigcap", "⋂", 1 }, { "bigoplus", "⨁", 1 }, { "bigotimes", "⨂", 1 }, { "bigvee", "⋁", 1 }, { "bigwedge", "⋀", 1 }, { 0, 0, 0 }
};
static const char *const FUNCS[] = { "sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "log", "ln", "lg", "exp",
    "det", "dim", "ker", "deg", "arg", "hom", "Pr", "gcd", "mod", 0 };
static const char *const LIMFUNCS[] = { "lim", "liminf", "limsup", "max", "min", "sup", "inf", "argmax", "argmin", 0 };
static const char *bb_letter(char c)
{
    switch (c) { case 'R': return "ℝ"; case 'N': return "ℕ"; case 'Z': return "ℤ"; case 'Q': return "ℚ"; case 'C': return "ℂ"; case 'P': return "ℙ"; case 'H': return "ℍ"; default: return 0; }
}

static int parse_mlist(int stop_rbrace);
static int parse_marg(void)                              /* argumento {..} o un token */
{
    skip_sp();
    if (pk() == '{') { P++; int l = parse_mlist(1); return l; }
    uint32_t a, b; read_group(&a, &b);
    struct rd r = push_src(a, b); int l = parse_mlist(0); pop_src(r);
    return l;
}
static int text_node(uint32_t a, uint32_t b, int sty)     /* \text{...}: texto recto, espacios incluidos */
{
    int l = mnew(K_LIST); if (l < 0) return -1;
    uint32_t i = a;
    while (i < b) {
        uint32_t j = i; while (j < b && S[j] != '$' && S[j] != '\\') j++;
        if (j > i) { int x = matom(S + i, (int)(j - i), C_ORD, (uint8_t)(sty | 0x80)); mappend(l, x); }
        if (j < b && S[j] == '\\') { j++; if (j < b) { char c = S[j]; int x = matom(&c, 1, C_ORD, (uint8_t)(sty | 0x80)); mappend(l, x); j++; } }
        else if (j < b && S[j] == '$') {                    /* \text{ ... $x$ ... } */
            uint32_t k = j + 1; while (k < b && S[k] != '$') k++;
            struct rd r = push_src(j + 1, k); int ml = parse_mlist(0); pop_src(r); mappend(l, ml); j = k < b ? k + 1 : b;
        }
        i = j;
    }
    return l;
}
static int delim_tok(uint32_t *s, uint32_t *n, char *buf)   /* tras \left o \right */
{
    buf[0] = 0;
    skip_sp();
    if (pk() == '\\') {
        P++; uint32_t a, k; read_cmd(&a, &k);
        if (seq(S + a, k, "{") || seq(S + a, k, "lbrace")) { buf[0] = '{'; buf[1] = 0; }
        else if (seq(S + a, k, "}") || seq(S + a, k, "rbrace")) { buf[0] = '}'; buf[1] = 0; }
        else if (seq(S + a, k, "|") || seq(S + a, k, "Vert")) { buf[0] = 0; cat(buf, "‖", 8); }
        else { const struct sym *y = find_sym(S + a, k); buf[0] = 0; if (y) cat(buf, y->u, 8); }
    } else if (pk() == '.') { P++; buf[0] = 0; }
    else if (P < L) { buf[0] = S[P++]; buf[1] = 0; }
    *s = 0; *n = (uint32_t)slen(buf);
    return 0;
}
static int script_wrap(int base)                          /* ^ y _ tras un atomo */
{
    for (;;) {
        uint32_t save = P; skip_sp();
        char c = pk();
        if (c == '\'' ) { P++; if (base < 0) base = matom("", 0, C_ORD, 0); int x = base; if (MN[x].k != K_SCRIPT) { x = mnew(K_SCRIPT); if (x < 0) return base; MN[x].a = base; } int p = matom("′", 3, C_ORD, 0); if (MN[x].b < 0) { int l = mnew(K_LIST); mappend(l, p); MN[x].b = l; } else mappend(MN[x].b, p); base = x; continue; }
        if (c != '^' && c != '_') { P = save; return base; }
        P++;
        int arg = parse_marg();
        if (base < 0) base = matom("", 0, C_ORD, 0);
        int x = base;
        if (MN[base].k != K_SCRIPT) { x = mnew(K_SCRIPT); if (x < 0) return base; MN[x].a = base; MN[x].cls = MN[base].cls; MN[x].lim = MN[base].lim; }
        if (c == '^') MN[x].b = arg; else MN[x].c = arg;
        base = x;
    }
}
static int env_matrix(uint32_t na, uint32_t nn);
/* lista de atomos hasta '}' (stop_rbrace) o fin; \right y \end tambien paran */
static int parse_mlist(int stop_rbrace)
{
    int list = mnew(K_LIST); if (list < 0) return -1;
    uint32_t guard_p = 0xFFFFFFFFu;
    while (!eof() && !FULL) {
        if (P == guard_p) { P++; continue; }
        guard_p = P;
        char c = pk();
        if (c == '%') { skip_comment(); continue; }
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '~') { P++; continue; }
        if (c == '}') { P++; if (stop_rbrace) break; continue; }
        if (c == '{') { P++; int g = parse_mlist(1); mappend(list, script_wrap(g)); continue; }
        if (c == '&') { P++; mappend(list, mnew(K_SEP)); continue; }
        if (c == '^' || c == '_' || c == '\'') {             /* indice sin base: se engancha al ultimo atomo */
            int last = MN[list].last;
            if (last >= 0) {
                int prev = -1; for (int it = MN[list].a; it >= 0 && it != last; it = MN[it].next) prev = it;
                int w = script_wrap(last);
                if (w != last) { MN[w].next = -1; if (prev < 0) MN[list].a = w; else MN[prev].next = w; MN[list].last = w; }
            } else mappend(list, script_wrap(-1));
            continue;
        }
        int node = -1;
        if (c == '\\') {
            uint32_t save = P;
            P++; uint32_t s, n; read_cmd(&s, &n);
            const char *cmd = S + s;
            if (seq(cmd, n, "right") || seq(cmd, n, "end")) { P = save; break; }
            if (seq(cmd, n, "\\") || seq(cmd, n, "cr")) { mappend(list, mnew(K_NEWROW)); continue; }
            if (seq(cmd, n, ",") || seq(cmd, n, ":") || seq(cmd, n, ";") || seq(cmd, n, "!") || seq(cmd, n, " ") || seq(cmd, n, "quad") || seq(cmd, n, "qquad")) {
                node = mnew(K_SPACE); if (node < 0) break;
                MN[node].ex = cmd[0] == ',' ? 3 : cmd[0] == ':' ? 4 : cmd[0] == ';' ? 5 : cmd[0] == '!' ? -3 : cmd[0] == ' ' ? 6 : n == 4 ? 18 : 36;
                mappend(list, node); continue;
            }
            if (seq(cmd, n, "frac") || seq(cmd, n, "dfrac") || seq(cmd, n, "tfrac") || seq(cmd, n, "binom") || seq(cmd, n, "cfrac")) {
                node = mnew(K_FRAC); if (node < 0) break;
                MN[node].a = parse_marg(); MN[node].b = parse_marg();
                MN[node].lim = cmd[0] == 'd' || cmd[0] == 'c' ? 1 : cmd[0] == 't' ? 2 : 0;
                MN[node].ex = cmd[0] == 'b';                   /* binom: sin raya y entre parentesis */
                MN[node].cls = C_INNER;
            } else if (seq(cmd, n, "sqrt")) {
                node = mnew(K_SQRT); if (node < 0) break;
                uint32_t a, b; if (read_opt(&a, &b)) { struct rd r = push_src(a, b); MN[node].b = parse_mlist(0); pop_src(r); }
                MN[node].a = parse_marg();
            } else if (seq(cmd, n, "left")) {
                node = mnew(K_DELIM); if (node < 0) break;
                char bl[8], br[8]; uint32_t ds, dn;
                delim_tok(&ds, &dn, bl);
                MN[node].a = parse_mlist(0);
                skip_sp();
                if (pk() == '\\') { P++; uint32_t a2, k2; read_cmd(&a2, &k2); if (seq(S + a2, k2, "right")) delim_tok(&ds, &dn, br); else { br[0] = 0; P = a2 - 1; } } else br[0] = 0;
                MN[node].n = (uint32_t)slen(bl); MN[node].s = pool_dupn(bl, &MN[node].n);
                MN[node].n2 = (uint32_t)slen(br); MN[node].s2 = pool_dupn(br, &MN[node].n2);
                MN[node].cls = C_INNER;
            } else if (seq(cmd, n, "text") || seq(cmd, n, "textrm") || seq(cmd, n, "mathrm") || seq(cmd, n, "operatorname") || seq(cmd, n, "textbf") || seq(cmd, n, "mathbf") || seq(cmd, n, "textit") || seq(cmd, n, "mathit") || seq(cmd, n, "mathsf") || seq(cmd, n, "mathtt")) {
                uint32_t a, b; read_group(&a, &b);
                int st = (cmd[4] == 'b' || seq(cmd, n, "mathbf")) ? TS_BOLD : (seq(cmd, n, "textit") || seq(cmd, n, "mathit")) ? TS_ITALIC : seq(cmd, n, "mathtt") ? TS_MONO : 0;
                if (seq(cmd, n, "mathbf") || seq(cmd, n, "mathit")) { struct rd r = push_src(a, b); node = parse_mlist(0); pop_src(r); for (int it = node >= 0 ? MN[node].a : -1; it >= 0; it = MN[it].next) if (MN[it].k == K_ATOM) MN[it].sty = (uint8_t)(st | (st == TS_BOLD ? 0x80 : 0)); }
                else node = text_node(a, b, st);
                if (node >= 0 && seq(cmd, n, "operatorname")) MN[node].cls = C_OP;
            } else if (seq(cmd, n, "mathbb") || seq(cmd, n, "mathcal") || seq(cmd, n, "mathscr") || seq(cmd, n, "mathfrak")) {
                uint32_t a, b; read_group(&a, &b);
                node = mnew(K_LIST); if (node < 0) break;
                for (uint32_t i = a; i < b; i++) { const char *u = cmd[4] == 'b' ? bb_letter(S[i]) : 0; int x = u ? matom(u, slen(u), C_ORD, 0x80) : matom(S + i, 1, C_ORD, cmd[4] == 'b' ? TS_BOLD | 0x80 : TS_ITALIC); mappend(node, x); }
            } else if (seq(cmd, n, "hat") || seq(cmd, n, "widehat") || seq(cmd, n, "bar") || seq(cmd, n, "overline") || seq(cmd, n, "vec") || seq(cmd, n, "dot") || seq(cmd, n, "ddot") || seq(cmd, n, "tilde") || seq(cmd, n, "widetilde") || seq(cmd, n, "underline") || seq(cmd, n, "overrightarrow")) {
                node = mnew(K_ACCENT); if (node < 0) break;
                MN[node].ex = cmd[0] == 'h' || seq(cmd, n, "widehat") ? 1 : (seq(cmd, n, "bar") || seq(cmd, n, "overline")) ? 2 : (cmd[0] == 'v' || seq(cmd, n, "overrightarrow")) ? 3 :
                              seq(cmd, n, "dot") ? 4 : seq(cmd, n, "ddot") ? 5 : (cmd[0] == 't' || seq(cmd, n, "widetilde")) ? 6 : 7;
                MN[node].a = parse_marg();
            } else if (seq(cmd, n, "begin")) {
                uint32_t a, b; read_group(&a, &b);
                node = env_matrix(a, b - a);
            } else if (seq(cmd, n, "limits")) { int last = MN[list].last; if (last >= 0) MN[last].lim = 1; continue; }
            else if (seq(cmd, n, "nolimits")) { int last = MN[list].last; if (last >= 0) MN[last].lim = 2; continue; }
            else if (seq(cmd, n, "displaystyle") || seq(cmd, n, "textstyle") || seq(cmd, n, "nonumber") || seq(cmd, n, "notag") || seq(cmd, n, "label") || seq(cmd, n, "big") || seq(cmd, n, "Big") || seq(cmd, n, "bigg") || seq(cmd, n, "Bigg") || seq(cmd, n, "bigl") || seq(cmd, n, "bigr") || seq(cmd, n, "Bigl") || seq(cmd, n, "Bigr")) {
                if (seq(cmd, n, "label")) { uint32_t a, b; read_group(&a, &b); set_label(a, b); }
                if (seq(cmd, n, "nonumber") || seq(cmd, n, "notag")) { int x = mnew(K_SPACE); if (x >= 0) { MN[x].ex = 0; MN[x].lim = 9; mappend(list, x); } }
                continue;
            } else {
                const struct sym *y = find_sym(cmd, n);
                if (y) node = matom(y->u, slen(y->u), y->cls, y->cls == C_ORD && cmd[0] >= 'a' && cmd[0] <= 'z' && n > 1 && (y->u[0] & 0x80) && slen(y->u) == 2 ? TS_ITALIC : 0);
                else {
                    for (int i = 0; BIGOPS[i].name && node < 0; i++) if (seq(cmd, n, BIGOPS[i].name)) { node = matom(BIGOPS[i].u, slen(BIGOPS[i].u), C_OP, 0); MN[node].ex = 1; MN[node].lim = BIGOPS[i].limits ? 3 : 0; }
                    for (int i = 0; FUNCS[i] && node < 0; i++) if (seq(cmd, n, FUNCS[i])) node = matom(cmd, (int)n, C_OP, 0x80);
                    for (int i = 0; LIMFUNCS[i] && node < 0; i++) if (seq(cmd, n, LIMFUNCS[i])) { node = matom(cmd, (int)n, C_OP, 0x80); MN[node].lim = 3; }
                    if (node < 0) { int e = mnew(K_ERR); if (e < 0) break; MN[e].n = (uint32_t)(n + 1); MN[e].s = pool_dupn(S + s - 1, &MN[e].n); node = e; O->errors++; }
                }
            }
        } else if (c >= '0' && c <= '9') {
            uint32_t a = P; while (P < L && ((S[P] >= '0' && S[P] <= '9') || (S[P] == '.' && P + 1 < L && S[P + 1] >= '0' && S[P + 1] <= '9'))) P++;
            node = matom(S + a, (int)(P - a), C_ORD, 0);
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            P++; node = matom(&c, 1, C_ORD, TS_ITALIC);
        } else {
            P++;
            const char *u = 0; int cls = C_ORD, ul = 1;
            switch (c) {
            case '+': cls = C_BIN; break; case '-': u = "−"; cls = C_BIN; break; case '*': u = "∗"; cls = C_BIN; break;
            case '=': case '<': case '>': cls = C_REL; break; case ':': cls = C_REL; break;
            case ',': case ';': cls = C_PUNCT; break;
            case '(': case '[': cls = C_OPEN; break; case ')': case ']': cls = C_CLOSE; break;
            case '!': case '?': cls = C_CLOSE; break;
            default: break;
            }
            if ((unsigned char)c >= 0x80) { uint32_t a = P - 1; while (P < L && ((unsigned char)S[P] & 0xC0) == 0x80) P++; node = matom(S + a, (int)(P - a), C_ORD, 0); }
            else if (u) { ul = slen(u); node = matom(u, ul, cls, 0); }
            else node = matom(&c, 1, cls, 0);
        }
        if (node < 0) break;
        mappend(list, script_wrap(node));
    }
    return list;
}
static int env_matrix(uint32_t na, uint32_t nn)            /* \begin{pmatrix} ... \end{pmatrix} dentro de una formula */
{
    const char *e = S + na;
    int node = mnew(K_MATRIX); if (node < 0) return -1;
    const char *l = "", *r = "";
    if (seq(e, nn, "pmatrix")) { l = "("; r = ")"; } else if (seq(e, nn, "bmatrix")) { l = "["; r = "]"; } else if (seq(e, nn, "vmatrix")) { l = "|"; r = "|"; }
    else if (seq(e, nn, "Vmatrix")) { l = "‖"; r = "‖"; } else if (seq(e, nn, "Bmatrix")) { l = "{"; r = "}"; } else if (seq(e, nn, "cases")) { l = "{"; MN[node].lim = 1; }
    else if (seq(e, nn, "aligned") || seq(e, nn, "array")) { MN[node].lim = 2; if (seq(e, nn, "array")) { uint32_t a, b; read_group(&a, &b); } }
    MN[node].a = parse_mlist(0);
    skip_sp();
    if (pk() == '\\') { P++; uint32_t a, k; read_cmd(&a, &k); if (seq(S + a, k, "end")) { uint32_t g0, g1; read_group(&g0, &g1); } }
    MN[node].n = (uint32_t)slen(l); MN[node].s = pool_dupn(l, &MN[node].n);
    MN[node].n2 = (uint32_t)slen(r); MN[node].s2 = pool_dupn(r, &MN[node].n2);
    MN[node].cls = C_INNER;
    return node;
}

/* ---------------------------------------------------------------- maquetacion de formulas */
#define TEX_DPX_MAX 4000                  /* SOMA_322_TEX_HOSTIL: tamano maximo de un delimitador que crece (px) */
struct box { int w, asc, desc; };
static int axis_of(int px) { return px * 27 / 100; }
static int spx(int px) { int s = px * 7 / 10; return s < 7 ? 7 : s; }
static struct box lay(int node, int px, int disp);
static int emit_text(int x, int y, const char *t, int n, int px, int st, uint32_t rgb) { return op_add(TOP_TEXT, x, y, 0, 0, px, st, rgb, pool_put(t, (uint32_t)n), (uint32_t)n); }
static struct box glyph_box(const char *t, int n, int px, int st, uint32_t rgb)
{
    struct box b; b.w = tw(t, n, px, st); b.asc = px * 72 / 100; b.desc = px * 22 / 100;
    if (n > 0) op_add(TOP_TEXT, 0, 0, 0, 0, px, st, rgb, pool_put(t, (uint32_t)n), (uint32_t)n);
    return b;
}
/* delimitador que crece: tamano de letra tal que cubra [axis-h, axis+h] */
static struct box delim_box(const char *t, int n, int px, int half)
{
    struct box b = { 0, 0, 0 };
    if (n <= 0) { b.w = px / 6; return b; }
    /* SOMA_322_TEX_HOSTIL: acotado (antes (2 * half) * 100 desbordaba un int con formulas enormes; y el tamano va en un int16_t) */
    int dpx = half > TEX_DPX_MAX ? TEX_DPX_MAX : (int)((2LL * half) * 100 / 92); if (dpx > TEX_DPX_MAX) dpx = TEX_DPX_MAX; if (dpx < px) dpx = px;
    int o = O->nops;
    b = glyph_box(t, n, dpx, 0, TEX_INK);
    /* centrar el glifo en el eje: la caja del glifo va de -0.72*dpx a +0.22*dpx (aprox.) */
    int ax = axis_of(px), gc = (dpx * 72 / 100 - dpx * 22 / 100) / 2;
    int dy = gc - ax;
    shift_ops(o, O->nops, 0, dy);
    b.asc = imax(dpx * 72 / 100 - dy, 0); b.desc = dpx * 22 / 100 + dy; if (b.desc < 0) b.desc = 0;
    return b;
}
static const int SPACE_TAB[8][8] = {   /* en mu (1/18 em): ord op bin rel open close punct inner */
    { 0, 3, 4, 5, 0, 0, 0, 3 }, { 3, 3, 0, 5, 0, 0, 0, 3 }, { 4, 4, 0, 0, 4, 0, 0, 4 }, { 5, 5, 0, 0, 5, 0, 0, 5 },
    { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 3, 4, 5, 0, 0, 0, 3 }, { 3, 3, 0, 3, 3, 3, 3, 3 }, { 3, 3, 4, 5, 3, 0, 3, 3 } };
static int eff_cls(int node) { int c = MN[node].cls; if (MN[node].k == K_SCRIPT && MN[node].a >= 0) c = MN[MN[node].a].cls; return c; }
static int g_init_prev = C_NONE;                         /* clase "anterior" al empezar (lado derecho de un align) */
static struct box lay_list(int node, int px, int disp, int stop_at_sep, int *cont)
{
    struct box b = { 0, 0, 0 };
    int prev = g_init_prev; g_init_prev = C_NONE;
    int script = px < BASE * 85 / 100;
    int it = node >= 0 ? MN[node].a : -1;
    if (cont && *cont >= 0) it = *cont;
    for (; it >= 0; it = MN[it].next) {
        int k = MN[it].k;
        if (k == K_SEP || k == K_NEWROW) { if (stop_at_sep) { if (cont) *cont = it; return b; } continue; }
        if (k == K_SPACE) { if (MN[it].lim == 9) continue; b.w += MN[it].ex * px / 18; continue; }
        int c = eff_cls(it);
        if (c == C_BIN && (prev == C_NONE || prev == C_OP || prev == C_BIN || prev == C_REL || prev == C_OPEN || prev == C_PUNCT)) c = C_ORD;
        if (prev != C_NONE && prev < 8 && c < 8) {
            int mu = SPACE_TAB[prev][c];
            if (script && c != C_OP && prev != C_OP) mu = 0;
            b.w += mu * px / 18;
        }
        int o = O->nops;
        struct box cb = lay(it, px, disp);
        shift_ops(o, O->nops, b.w, 0);
        b.w += cb.w; b.asc = imax(b.asc, cb.asc); b.desc = imax(b.desc, cb.desc);
        prev = c;
    }
    if (cont) *cont = -1;
    return b;
}
static struct box lay(int node, int px, int disp)
{
    struct box b = { 0, 0, 0 };
    if (node < 0) return b;
    struct mn *m = &MN[node];
    switch (m->k) {
    case K_LIST: return lay_list(node, px, disp, 0, 0);
    case K_ERR: { int o = O->nops; b = glyph_box(O->pool + m->s, (int)m->n, px, TS_MONO, TEX_ERR); (void)o; return b; }
    case K_ATOM: {
        int st = m->sty & 0x7F, big = m->ex == 1;
        int gpx = big ? (disp ? px * 16 / 10 : px * 12 / 10) : px;
        if (big && O->pool[m->s] == (char)0xE2 && O->pool[m->s + 1] == (char)0x88 && (O->pool[m->s + 2] == (char)0xAB || O->pool[m->s + 2] == (char)0xAC || O->pool[m->s + 2] == (char)0xAD || O->pool[m->s + 2] == (char)0xAE))
            gpx = disp ? px * 2 : px * 14 / 10;            /* integrales mas altas */
        int o = O->nops;
        b = glyph_box(O->pool + m->s, (int)m->n, gpx, st, TEX_INK);
        if (big) {                                          /* centrado en el eje */
            int gc = (b.asc - b.desc) / 2, dy = gc - axis_of(px);
            shift_ops(o, O->nops, 0, dy); b.asc -= dy; b.desc += dy;
            if (b.desc < 0) b.desc = 0;
        }
        return b;
    }
    case K_SPACE: b.w = m->ex * px / 18; return b;
    case K_FRAC: {
        int sub = m->lim == 1 ? px : m->lim == 2 ? spx(px) : (disp ? px : spx(px));
        int ndisp = m->lim == 1 ? 1 : 0;
        int o0 = O->nops; struct box nb = lay(m->a, sub, ndisp); int o1 = O->nops; struct box db = lay(m->b, sub, ndisp); int o2 = O->nops;
        int t = imax(1, px / 15), gap = imax(2, px / 7), ax = axis_of(px), pad = imax(1, px / 8);
        b.w = imax(nb.w, db.w) + 2 * pad;
        int nbase = -ax - t / 2 - gap - nb.desc, dbase = -ax + (t + 1) / 2 + gap + db.asc;
        if (disp) { nbase -= px / 10; dbase += px / 10; }
        shift_ops(o0, o1, (b.w - nb.w) / 2, nbase); shift_ops(o1, o2, (b.w - db.w) / 2, dbase);
        b.asc = -(nbase - nb.asc); b.desc = dbase + db.desc;
        if (!m->ex) op_add(TOP_RECT, 0, -ax - t / 2, b.w, t, 0, 0, TEX_INK, 0, 0);
        else {                                              /* binom: parentesis */
            int half = imax(b.asc - ax, b.desc + ax) + 1, ow = b.w;
            int o3 = O->nops; struct box lb = delim_box("(", 1, px, half); int o4 = O->nops;
            shift_ops(o0, o4 - (o4 - o3), lb.w, 0);
            struct box rb = delim_box(")", 1, px, half); shift_ops(o4, O->nops, lb.w + ow, 0);
            b.w = lb.w + ow + rb.w; b.asc = imax(b.asc, lb.asc); b.desc = imax(b.desc, lb.desc);
        }
        return b;
    }
    case K_SQRT: {
        int o0 = O->nops; struct box ib = lay(m->a, px, disp); int o1 = O->nops;
        int t = imax(1, px / 16), gap = imax(2, px / 8), rw = px * 6 / 10;
        int top = -(ib.asc + gap + t), bot = ib.desc + imax(1, px / 12);
        int ixw = 0, o2 = O->nops;
        if (m->b >= 0) {                                    /* indice de la raiz */
            int ipx = spx(spx(px));
            struct box xb = lay(m->b, ipx, 0); o2 = O->nops;
            ixw = imax(0, xb.w - rw / 2);
            shift_ops(o1, o2, 0, top + (bot - top) * 45 / 100 - xb.desc);
        }
        shift_ops(o0, o1, ixw + rw + imax(1, px / 10), 0);
        int x0 = ixw, H = bot - top, lw = imax(16, px * 16 / 14);
        op_add(TOP_LINE, (x0) * 16, (bot - H * 45 / 100) * 16, (rw * 25 / 100) * 16, (H * 8 / 100) * 16, lw * 7 / 10, 0, TEX_INK, 0, 0);
        op_add(TOP_LINE, (x0 + rw * 25 / 100) * 16, (bot - H * 37 / 100) * 16, (rw * 30 / 100) * 16, (H * 37 / 100) * 16, lw * 3 / 2, 0, TEX_INK, 0, 0);
        op_add(TOP_LINE, (x0 + rw * 55 / 100) * 16, bot * 16, (rw * 45 / 100) * 16, (top - bot) * 16 + 8, lw, 0, TEX_INK, 0, 0);
        int bw = ixw + rw + imax(1, px / 10) + ib.w + imax(1, px / 10);
        op_add(TOP_RECT, x0 + rw, top, bw - (x0 + rw), t, 0, 0, TEX_INK, 0, 0);
        (void)o2;
        b.w = bw + imax(1, px / 12); b.asc = -top + 1; b.desc = bot;
        return b;
    }
    case K_SCRIPT: {
        int base = m->a, limits = 0;
        if (base >= 0 && MN[base].k == K_ATOM && MN[base].cls == C_OP) limits = (MN[base].lim == 3 && disp) || MN[base].lim == 1 || m->lim == 1;
        if (m->lim == 2 || (base >= 0 && MN[base].lim == 2)) limits = 0;
        int o0 = O->nops; struct box bb = lay(base, px, disp); int o1 = O->nops;
        int sp = spx(px);
        if (limits) {
            struct box sb = { 0, 0, 0 }, ub = { 0, 0, 0 };
            int o2 = O->nops; if (m->b >= 0) sb = lay(m->b, sp, 0); int o3 = O->nops; if (m->c >= 0) ub = lay(m->c, sp, 0); int o4 = O->nops;
            int w = imax(bb.w, imax(sb.w, ub.w)), gap = imax(2, px / 8);
            shift_ops(o0, o1, (w - bb.w) / 2, 0);
            int sbase = -bb.asc - gap - sb.desc, ubase = bb.desc + gap + ub.asc;
            shift_ops(o2, o3, (w - sb.w) / 2, sbase); shift_ops(o3, o4, (w - ub.w) / 2, ubase);
            b.w = w; b.asc = m->b >= 0 ? -(sbase - sb.asc) : bb.asc; b.desc = m->c >= 0 ? ubase + ub.desc : bb.desc;
            return b;
        }
        int isint = base >= 0 && MN[base].k == K_ATOM && MN[base].ex == 1;
        int x = bb.w + (isint ? -px / 8 : 1);
        struct box sb = { 0, 0, 0 }, ub = { 0, 0, 0 };
        int o2 = O->nops; if (m->b >= 0) sb = lay(m->b, sp, 0); int o3 = O->nops; if (m->c >= 0) ub = lay(m->c, sp, 0); int o4 = O->nops;
        int up = imax(px * 40 / 100, bb.asc - sp * 45 / 100), dn = imax(px * 18 / 100, bb.desc - sp * 20 / 100);
        if (m->b >= 0 && m->c >= 0) { int need = (sb.desc + ub.asc + imax(2, px / 8)) - (up + dn); if (need > 0) { up += need / 2; dn += need - need / 2; } }
        if (m->c >= 0 && m->b < 0) dn = imax(dn, ub.asc - px * 45 / 100);
        shift_ops(o2, o3, x, -up); shift_ops(o3, o4, isint ? x - px / 6 : x, dn);
        b.w = x + imax(sb.w, ub.w + (isint ? -px / 6 : 0)) + 1;
        b.asc = imax(bb.asc, m->b >= 0 ? up + sb.asc : 0); b.desc = imax(bb.desc, m->c >= 0 ? dn + ub.desc : 0);
        return b;
    }
    case K_DELIM: {
        int o0 = O->nops; struct box ib = lay(m->a, px, disp); int o1 = O->nops;
        int ax = axis_of(px), half = imax(ib.asc - ax, ib.desc + ax) + px / 10;
        int o2 = O->nops; struct box lb = delim_box(O->pool + m->s, (int)m->n, px, half); int o3 = O->nops;
        shift_ops(o0, o1, lb.w, 0);
        struct box rb = delim_box(O->pool + m->s2, (int)m->n2, px, half); shift_ops(o3, O->nops, lb.w + ib.w, 0);
        (void)o2;
        b.w = lb.w + ib.w + rb.w; b.asc = imax(ib.asc, imax(lb.asc, rb.asc)); b.desc = imax(ib.desc, imax(lb.desc, rb.desc));
        return b;
    }
    case K_ACCENT: {
        int o0 = O->nops; struct box ib = lay(m->a, px, disp); (void)o0;
        int t = imax(1, px / 16), top = -ib.asc - imax(2, px / 8);
        b = ib;
        switch (m->ex) {
        case 2: op_add(TOP_RECT, 0, top - t, ib.w, t, 0, 0, TEX_INK, 0, 0); b.asc = -top + t; break;
        case 7: op_add(TOP_RECT, 0, ib.desc + imax(1, px / 10), ib.w, t, 0, 0, TEX_INK, 0, 0); b.desc += px / 10 + t + 1; break;
        case 3: { int y = top - px / 8; op_add(TOP_LINE, 1 * 16, y * 16, (ib.w - 2) * 16, 0, imax(16, px), 0, TEX_INK, 0, 0);
                  op_add(TOP_LINE, (ib.w - 1) * 16, y * 16, -(px / 4) * 16, -(px / 6) * 16, imax(16, px), 0, TEX_INK, 0, 0);
                  op_add(TOP_LINE, (ib.w - 1) * 16, y * 16, -(px / 4) * 16, (px / 6) * 16, imax(16, px), 0, TEX_INK, 0, 0); b.asc = -y + px / 6; break; }
        default: {
            const char *g = m->ex == 1 ? "ˆ" : m->ex == 4 ? "˙" : m->ex == 5 ? "¨" : "˜";
            int gp = px, gw = tw(g, slen(g), gp, 0);
            int ital = m->a >= 0 && MN[m->a].k == K_LIST && MN[m->a].a >= 0 && (MN[MN[m->a].a].sty & TS_ITALIC);
            emit_text((ib.w - gw) / 2 + (ital ? px / 10 : 0), top + px * 55 / 100, g, slen(g), gp, 0, TEX_INK);
            b.asc = -top + px / 4; break; }
        }
        return b;
    }
    case K_MATRIX: {
        /* filas por K_NEWROW, celdas por K_SEP */
        static int cells_o[24][12][2]; static struct box cb[24][12]; int nr = 0, ncol = 0, cont = -1;
        for (int r = 0; r < 24; r++) for (int c = 0; c < 12; c++) { cells_o[r][c][0] = cells_o[r][c][1] = 0; cb[r][c].w = cb[r][c].asc = cb[r][c].desc = 0; }
        int list = m->a, row = 0, col = 0;
        if (list < 0) return b;
        for (;;) {
            int o = O->nops;
            struct box x = lay_list(list, m->lim == 2 ? px : (disp ? px : spx(px) * 11 / 10), disp && m->lim == 2, 1, &cont);
            if (row < 24 && col < 12) { cells_o[row][col][0] = o; cells_o[row][col][1] = O->nops; cb[row][col] = x; }
            if (col + 1 > ncol) ncol = col + 1;
            if (cont < 0) { nr = row + 1; break; }
            if (MN[cont].k == K_SEP) col++; else { row++; col = 0; }
            cont = MN[cont].next;
            if (cont < 0) { nr = row + (col > 0 ? 1 : 0); if (!nr) nr = 1; break; }
        }
        if (nr > 24) nr = 24;
        if (ncol > 12) ncol = 12;
        int cw[12] = { 0 }, ra[24] = { 0 }, rd[24] = { 0 };
        for (int r = 0; r < nr; r++) for (int c = 0; c < ncol; c++) { if (c >= 12) break; cw[c] = imax(cw[c], cb[r][c].w); ra[r] = imax(ra[r], cb[r][c].asc); rd[r] = imax(rd[r], cb[r][c].desc); }
        for (int r = 0; r < nr; r++) { if (ra[r] < px * 7 / 10) ra[r] = px * 7 / 10; if (rd[r] < px / 4) rd[r] = px / 4; }
        int cgap = m->lim == 2 ? px / 3 : px, rgap = px / 3, y = 0, tot = 0;
        for (int r = 0; r < nr; r++) tot += ra[r] + rd[r] + (r ? rgap : 0);
        int top = -(tot / 2) - axis_of(px);
        y = top;
        int W = 0; for (int c = 0; c < ncol; c++) W += cw[c] + (c ? cgap : 0);
        int o_l = O->nops;
        for (int r = 0; r < nr; r++) {
            y += ra[r];
            int x = 0;
            for (int c = 0; c < ncol; c++) {
                if (r < 24 && c < 12 && cells_o[r][c][1] >= cells_o[r][c][0]) {
                    int dx = m->lim ? (m->lim == 2 && (c & 1) == 0 ? cw[c] - cb[r][c].w : 0) : (cw[c] - cb[r][c].w) / 2;
                    shift_ops(cells_o[r][c][0], cells_o[r][c][1], x + dx, y);
                }
                x += cw[c] + cgap;
            }
            y += rd[r] + rgap;
        }
        (void)o_l;
        int inner_o1 = O->nops;
        int first_o = nr > 0 ? cells_o[0][0][0] : inner_o1;
        b.w = W; b.asc = -top; b.desc = tot + top;
        int half = imax(b.asc - axis_of(px), b.desc + axis_of(px)) + px / 8;
        if (m->n || m->n2) {
            int o2 = O->nops; struct box lb = delim_box(O->pool + m->s, (int)m->n, px, half); int o3 = O->nops;
            shift_ops(first_o, o2, lb.w + px / 6, 0);
            struct box rb = delim_box(O->pool + m->s2, (int)m->n2, px, half); shift_ops(o3, O->nops, lb.w + px / 6 + W + px / 6, 0);
            b.w = lb.w + W + rb.w + px / 3; b.asc = imax(b.asc, lb.asc); b.desc = imax(b.desc, lb.desc);
        }
        return b;
    }
    default: return b;
    }
}
/* maqueta una formula del texto [a,b): devuelve caja y operaciones relativas */
static struct box math_range(uint32_t a, uint32_t b, int px, int disp, int *o0)
{
    nmn = 0;
    struct rd r = push_src(a, b);
    int root = parse_mlist(0);
    pop_src(r);
    *o0 = O->nops;
    return lay(root, px, disp);
}

/* ================================================================ TEXTO: parrafos */
enum { IT_WORD = 0, IT_SPACE, IT_BOX, IT_BREAK };
struct item { uint8_t kind, style; int16_t px; uint32_t rgb; uint32_t s, n; int w, asc, desc; int o0, o1; };
#define IT_MAX 3072
static struct item IT[IT_MAX]; static int nit;
struct sty { int style, px; uint32_t rgb; };
static struct sty ST;
static int add_word(const char *t, int n)
{
    if (n <= 0) return 0;
    if (nit && IT[nit - 1].kind == IT_WORD && IT[nit - 1].style == ST.style && IT[nit - 1].px == ST.px && IT[nit - 1].rgb == ST.rgb && IT[nit - 1].s + IT[nit - 1].n + 1 == O->npool) {
        /* se pega a la palabra anterior (p. ej. "palabra" + "\textbf{x}" sin espacio): ampliar en el pool */
        if (O->npool + (uint32_t)n + 1 > O->poolcap) { FULL = 1; return -1; }
        O->npool--;                                      /* quita el 0 final */
        for (int i = 0; i < n; i++) O->pool[O->npool++] = t[i];
        O->pool[O->npool++] = 0;
        IT[nit - 1].n += (uint32_t)n; IT[nit - 1].w = tw(O->pool + IT[nit - 1].s, (int)IT[nit - 1].n, ST.px, ST.style);
        return 0;
    }
    if (nit >= IT_MAX) { FULL = 1; return -1; }
    struct item *it = &IT[nit++];
    it->kind = IT_WORD; it->style = (uint8_t)ST.style; it->px = (int16_t)ST.px; it->rgb = ST.rgb;
    it->n = (uint32_t)n; it->s = pool_dupn(t, &it->n);
    it->w = tw(t, n, ST.px, ST.style); it->asc = asc_of(ST.px); it->desc = desc_of(ST.px);
    return 0;
}
static void add_space(void) { if (!nit || IT[nit - 1].kind == IT_SPACE || IT[nit - 1].kind == IT_BREAK) return; if (nit >= IT_MAX) { FULL = 1; return; } struct item *it = &IT[nit++]; it->kind = IT_SPACE; it->px = (int16_t)ST.px; it->style = (uint8_t)ST.style; it->w = tw(" ", 1, ST.px, ST.style); it->asc = it->desc = 0; }
static void add_break(void) { if (nit >= IT_MAX) { FULL = 1; return; } struct item *it = &IT[nit++]; it->kind = IT_BREAK; it->w = 0; it->asc = asc_of(ST.px); it->desc = desc_of(ST.px); }
static void add_box(struct box b, int o0, int o1)
{
    if (nit >= IT_MAX) { FULL = 1; return; }
    struct item *it = &IT[nit++]; it->kind = IT_BOX; it->w = b.w; it->asc = b.asc; it->desc = b.desc; it->o0 = o0; it->o1 = o1; it->px = (int16_t)ST.px;
}
/* superindice de texto (llamada a nota) */
static void add_sup_text(const char *t)
{
    int o0 = O->nops; int px = ST.px * 7 / 10;
    struct box b = glyph_box(t, slen(t), px, 0, TEX_LINK);
    shift_ops(o0, O->nops, 1, -ST.px * 38 / 100);
    b.asc += ST.px * 38 / 100; b.w += 2;
    add_box(b, o0, O->nops);
}

/* coloca el parrafo: lineas justificadas en [x0, x0+w]. first_indent: sangria de la primera linea. align 0 justificar, 1 centrar, 2 izq. */
static void flush_para(int x0, int w, int align, int first_indent, int gap_after)
{
    if (!nit) return;
    while (nit && IT[nit - 1].kind == IT_SPACE) nit--;
    int i = 0;
    while (i < nit && !FULL) {
        while (i < nit && IT[i].kind == IT_SPACE) i++;
        if (i >= nit) break;
        int lw = 0, j = i, last_fit = -1, avail = w - (i == 0 ? first_indent : 0);
        int brk = 0;
        for (; j < nit; j++) {
            if (IT[j].kind == IT_BREAK) { brk = 1; break; }
            if (IT[j].kind == IT_SPACE) { last_fit = j; lw += IT[j].w; continue; }
            if (lw + IT[j].w > avail && last_fit > i) { j = last_fit; break; }
            lw += IT[j].w;
        }
        int end = j;                                        /* [i, end) */
        while (end > i && IT[end - 1].kind == IT_SPACE) end--;
        int asc = 0, desc = 0, natural = 0, nsp = 0;
        for (int k = i; k < end; k++) { asc = imax(asc, IT[k].asc); desc = imax(desc, IT[k].desc); natural += IT[k].w; if (IT[k].kind == IT_SPACE) nsp++; }
        if (asc == 0) { asc = asc_of(ST.px); desc = desc_of(ST.px); }
        int last_line = brk || j >= nit;
        int extra = avail - natural, x = x0 + (i == 0 ? first_indent : 0);
        if (align == 1) x += extra / 2;
        int lead = imax(asc + desc + 2, (int)IT[i].px * 135 / 100);
        int base = Y + asc + (lead - asc - desc) / 2;
        int done = 0;
        for (int k = i; k < end; k++) {
            struct item *t = &IT[k];
            if (t->kind == IT_SPACE) {
                int add = t->w;
                if (align == 0 && !last_line && nsp > 0 && extra > 0 && extra < avail / 3) { int share = extra * (done + 1) / nsp - extra * done / nsp; add += share; done++; }
                x += add; continue;
            }
            if (t->kind == IT_WORD) {
                op_add(TOP_TEXT, x, base, 0, 0, t->px, t->style, t->rgb, t->s, t->n);
                if (t->style & TS_UNDER) op_add(TOP_RECT, x, base + imax(1, t->px / 8), t->w, imax(1, t->px / 14), 0, 0, t->rgb, 0, 0);
            } else if (t->kind == IT_BOX) shift_ops(t->o0, t->o1, x, base);
            x += t->w;
        }
        Y += lead;
        i = (j < nit && (IT[j].kind == IT_BREAK)) ? j + 1 : j;
    }
    nit = 0;
    Y += gap_after;
}

/* ================================================================ lectura del cuerpo */
static int list_depth, list_kind[6], item_no[6];
static int X0, COLW;                                       /* columna actual (sangrias de listas y citas) */
static int pend_label_w;                                   /* etiqueta de item pendiente */
static uint32_t pend_label_s, pend_label_n; static int pend_label_px, pend_label_style;
static uint32_t title_a, title_b, author_a, author_b, date_a, date_b; static int have_title, have_author, have_date;
static uint32_t fn_a[32], fn_b[32];
static int align_mode;                                     /* 0 justificado, 1 centrado */
static int in_abstract, cur_float;                         /* 1 figura, 2 tabla */

static void inline_until(uint32_t end);
static void text_group(uint32_t a, uint32_t b, int add_style, int px, uint32_t rgb)
{
    struct sty save = ST;
    ST.style |= add_style; if (px) ST.px = px; if (rgb) ST.rgb = rgb;
    struct rd r = push_src(a, b); inline_until(b); pop_src(r);
    ST = save;
}
static const char *accent_char(char acc, char c)
{
    static const char *const T[][3] = {
        { "'a", "á" }, { "'e", "é" }, { "'i", "í" }, { "'o", "ó" }, { "'u", "ú" }, { "'A", "Á" }, { "'E", "É" }, { "'I", "Í" }, { "'O", "Ó" }, { "'U", "Ú" },
        { "`a", "à" }, { "`e", "è" }, { "`i", "ì" }, { "`o", "ò" }, { "`u", "ù" }, { "^a", "â" }, { "^e", "ê" }, { "^i", "î" }, { "^o", "ô" }, { "^u", "û" },
        { "\"a", "ä" }, { "\"e", "ë" }, { "\"i", "ï" }, { "\"o", "ö" }, { "\"u", "ü" }, { "\"U", "Ü" }, { "\"O", "Ö" }, { "\"A", "Ä" },
        { "~n", "ñ" }, { "~N", "Ñ" }, { "~a", "ã" }, { "~o", "õ" }, { "cc", "ç" }, { "cC", "Ç" }, { 0, 0 } };
    for (int i = 0; T[i][0]; i++) if (T[i][0][0] == acc && T[i][0][1] == c) return T[i][1];
    return 0;
}
static void add_text_chars(uint32_t a, uint32_t b)          /* palabra con ligaduras de LaTeX */
{
    char buf[256]; int n = 0;
    for (uint32_t i = a; i < b && n < 250; i++) {
        char c = S[i];
        if (c == '-' && i + 2 < b && S[i + 1] == '-' && S[i + 2] == '-') { const char *u = "—"; for (int k = 0; u[k]; k++) buf[n++] = u[k]; i += 2; continue; }
        if (c == '-' && i + 1 < b && S[i + 1] == '-') { const char *u = "–"; for (int k = 0; u[k]; k++) buf[n++] = u[k]; i += 1; continue; }
        if (c == '`' && i + 1 < b && S[i + 1] == '`') { const char *u = "“"; for (int k = 0; u[k]; k++) buf[n++] = u[k]; i += 1; continue; }
        if (c == '\'' && i + 1 < b && S[i + 1] == '\'') { const char *u = "”"; for (int k = 0; u[k]; k++) buf[n++] = u[k]; i += 1; continue; }
        if (c == '`') { const char *u = "‘"; for (int k = 0; u[k]; k++) buf[n++] = u[k]; continue; }
        if (c == '~') { buf[n++] = ' '; continue; }
        buf[n++] = c;
    }
    add_word(buf, n);
}
static void inline_math(uint32_t a, uint32_t b)
{
    int o0; struct box bx = math_range(a, b, ST.px, 0, &o0);
    add_box(bx, o0, O->nops);
}
static uint32_t find_close(const char *close)              /* posicion de 'close' desde P (sin anidar) */
{
    int cl = slen(close);
    for (uint32_t i = P; i + (uint32_t)cl <= L; i++) {
        if (S[i] == '\\' && cl == 1) { i++; continue; }
        int ok = 1; for (int k = 0; k < cl; k++) if (S[i + k] != close[k]) { ok = 0; break; }
        if (ok) return i;
    }
    return L;
}
static uint32_t find_end_env(const char *name)            /* \end{name} desde P */
{
    int nl = slen(name);
    for (uint32_t i = P; i + 5 + (uint32_t)nl < L + 1; i++) {
        if (S[i] == '\\' && i + 5 <= L && S[i + 1] == 'e' && S[i + 2] == 'n' && S[i + 3] == 'd' && S[i + 4] == '{') {
            if (i + 5 + (uint32_t)nl < L && seq(S + i + 5, (uint32_t)nl, name) && S[i + 5 + nl] == '}') return i;
            int ok = 1; for (int k = 0; k < nl; k++) if (i + 5 + (uint32_t)k >= L || S[i + 5 + k] != name[k]) ok = 0;
            if (ok && i + 5 + (uint32_t)nl < L && S[i + 5 + nl] == '}') return i;
        }
    }
    return L;
}
static void skip_end_env(void) { if (P < L && S[P] == '\\') { P++; uint32_t s, n; read_cmd(&s, &n); uint32_t a, b; read_group(&a, &b); } }

/* comandos de texto en linea; devuelve 1 si lo trato */
static int inline_cmd(const char *c, uint32_t n)
{
    uint32_t a, b;
    if (seq(c, n, "textbf") || seq(c, n, "bf") || seq(c, n, "mathbf")) { read_group(&a, &b); text_group(a, b, TS_BOLD, 0, 0); return 1; }
    if (seq(c, n, "textit") || seq(c, n, "emph") || seq(c, n, "textsl") || seq(c, n, "it")) { read_group(&a, &b); text_group(a, b, TS_ITALIC, 0, 0); return 1; }
    if (seq(c, n, "texttt") || seq(c, n, "verb")) { if (c[0] == 'v') { char d = pk(); P++; a = P; while (P < L && S[P] != d) P++; b = P; if (P < L) P++; struct sty s = ST; ST.style |= TS_MONO; add_word(S + a, (int)(b - a)); ST = s; return 1; } read_group(&a, &b); text_group(a, b, TS_MONO, 0, 0); return 1; }
    if (seq(c, n, "underline") || seq(c, n, "uline")) { read_group(&a, &b); text_group(a, b, TS_UNDER, 0, 0); return 1; }
    if (seq(c, n, "textsc") || seq(c, n, "textrm") || seq(c, n, "textsf") || seq(c, n, "textnormal") || seq(c, n, "mbox") || seq(c, n, "text")) { read_group(&a, &b); text_group(a, b, 0, 0, 0); return 1; }
    if (seq(c, n, "textcolor") || seq(c, n, "color")) {
        uint32_t ca, cb; read_group(&ca, &cb);
        static const struct { const char *n; uint32_t c; } C[] = { { "red", 0xDC2626 }, { "blue", 0x1D4ED8 }, { "green", 0x15803D }, { "orange", 0xEA580C }, { "purple", 0x7E22CE },
            { "gray", 0x6B7280 }, { "grey", 0x6B7280 }, { "black", 0x111827 }, { "cyan", 0x0891B2 }, { "magenta", 0xC026D3 }, { "brown", 0x92400E }, { "teal", 0x0F766E }, { 0, 0 } };
        uint32_t col = TEX_INK; for (int i = 0; C[i].n; i++) if (seq(S + ca, cb - ca, C[i].n)) col = C[i].c;
        if (seq(c, n, "color")) { ST.rgb = col; return 1; }
        read_group(&a, &b); text_group(a, b, 0, 0, col); return 1;
    }
    if (seq(c, n, "href")) { uint32_t ua, ub; read_group(&ua, &ub); read_group(&a, &b); text_group(a, b, TS_UNDER, 0, TEX_LINK); return 1; }
    if (seq(c, n, "url")) { read_group(&a, &b); struct sty s = ST; ST.style |= TS_MONO | TS_UNDER; ST.rgb = TEX_LINK; add_word(S + a, (int)(b - a)); ST = s; return 1; }
    if (seq(c, n, "footnote")) {
        read_group(&a, &b);
        if (fnno < 32) { fn_a[fnno] = a; fn_b[fnno] = b; }
        fnno++; char t[8]; num_str(t, fnno); add_sup_text(t); return 1;
    }
    if (seq(c, n, "ref") || seq(c, n, "eqref") || seq(c, n, "pageref")) {
        read_group(&a, &b); char t[24] = ""; if (c[0] == 'e') cat(t, "(", 24); cat(t, get_label(a, b), 24); if (c[0] == 'e') cat(t, ")", 24);
        struct sty s = ST; ST.rgb = TEX_LINK; add_word(t, slen(t)); ST = s; return 1;
    }
    if (seq(c, n, "cite")) { read_opt(&a, &b); read_group(&a, &b); struct sty s = ST; ST.rgb = TEX_LINK; add_word("[", 1); add_word(S + a, (int)(b - a)); add_word("]", 1); ST = s; return 1; }
    if (seq(c, n, "label")) { read_group(&a, &b); set_label(a, b); return 1; }
    if (seq(c, n, "today")) { add_word("12 de noviembre de 2026", 23); return 1; }
    if (seq(c, n, "LaTeX")) { add_word("LaTeX", 5); return 1; }
    if (seq(c, n, "TeX")) { add_word("TeX", 3); return 1; }
    if (seq(c, n, "ldots") || seq(c, n, "dots")) { add_word("…", 3); return 1; }
    if (seq(c, n, "\\") || seq(c, n, "newline") || seq(c, n, "linebreak")) { read_opt(&a, &b); add_break(); return 1; }
    if (seq(c, n, " ") || seq(c, n, ",") || seq(c, n, "quad") || seq(c, n, "qquad") || seq(c, n, "hfill") || seq(c, n, "enspace")) { add_space(); return 1; }
    if (seq(c, n, "noindent") || seq(c, n, "centering") || seq(c, n, "normalsize") || seq(c, n, "selectfont") || seq(c, n, "protect") || seq(c, n, "relax") || seq(c, n, "indent")) return 1;
    if (seq(c, n, "small") || seq(c, n, "footnotesize") || seq(c, n, "scriptsize") || seq(c, n, "tiny")) { ST.px = BASE * (c[0] == 's' && n == 5 ? 90 : c[0] == 'f' ? 80 : c[0] == 's' ? 70 : 60) / 100; return 1; }
    if (seq(c, n, "large") || seq(c, n, "Large") || seq(c, n, "LARGE") || seq(c, n, "huge") || seq(c, n, "Huge")) { ST.px = BASE * (seq(c, n, "large") ? 120 : seq(c, n, "Large") ? 144 : seq(c, n, "LARGE") ? 172 : seq(c, n, "huge") ? 207 : 249) / 100; return 1; }
    if (seq(c, n, "bfseries")) { ST.style |= TS_BOLD; return 1; }
    if (seq(c, n, "itshape") || seq(c, n, "em")) { ST.style |= TS_ITALIC; return 1; }
    if (seq(c, n, "ttfamily")) { ST.style |= TS_MONO; return 1; }
    if (seq(c, n, "ss")) { add_word("ß", 2); return 1; }
    if (seq(c, n, "o")) { add_word("ø", 2); return 1; }
    if (seq(c, n, "ae")) { add_word("æ", 2); return 1; }
    if (seq(c, n, "copyright")) { add_word("©", 2); return 1; }
    if (seq(c, n, "S")) { add_word("§", 2); return 1; }
    if (seq(c, n, "euro")) { add_word("€", 3); return 1; }
    if (seq(c, n, "textbackslash")) { add_word("\\", 1); return 1; }
    if (n == 1 && (c[0] == '%' || c[0] == '&' || c[0] == '$' || c[0] == '#' || c[0] == '_' || c[0] == '{' || c[0] == '}')) { add_word(c, 1); return 1; }
    if (n == 1 && (c[0] == '\'' || c[0] == '`' || c[0] == '^' || c[0] == '"' || c[0] == '~' || c[0] == 'c')) {
        uint32_t ga, gb; read_group(&ga, &gb);
        const char *u = gb > ga ? accent_char(c[0], S[ga]) : 0;
        if (u) add_word(u, slen(u)); else if (gb > ga) add_word(S + ga, (int)(gb - ga));
        return 1;
    }
    if (n == 1 && c[0] == 'c' ) return 1;
    return 0;
}

static void block_math(uint32_t a, uint32_t b, int numbered, int multiline);
/* texto en linea hasta 'end' (no trata entornos de bloque) */
static void inline_until(uint32_t end)
{
    if (end > L) end = L;
    uint32_t guard_p = 0xFFFFFFFFu;
    while (P < end && !FULL) {
        if (P == guard_p) { P++; continue; }       /* nunca quedarse en el mismo sitio */
        guard_p = P;
        char c = S[P];
        if (c == '%') { skip_comment(); continue; }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { P++; add_space(); continue; }
        if (c == '{') { uint32_t a, b; read_group(&a, &b); text_group(a, b, 0, 0, 0); continue; }
        if (c == '}') { P++; continue; }
        if (c == '$') {
            if (P + 1 < end && S[P + 1] == '$') { P += 2; uint32_t a = P, e = find_close("$$"); P = e + 2 > end ? end : e + 2; block_math(a, e, 0, 0); continue; }
            P++; uint32_t a = P, e = find_close("$"); if (e > end) e = end; P = e < end ? e + 1 : end; inline_math(a, e); continue;
        }
        if (c == '\\') {
            if (P + 1 < end && S[P + 1] == '(') { P += 2; uint32_t a = P, e = find_close("\\)"); P = e + 2 > end ? end : e + 2; inline_math(a, e); continue; }
            if (P + 1 < end && S[P + 1] == '[') { P += 2; uint32_t a = P, e = find_close("\\]"); P = e + 2 > end ? end : e + 2; block_math(a, e, 0, 0); continue; }
            uint32_t save = P;
            P++; uint32_t s, n; read_cmd(&s, &n);
            if (inline_cmd(S + s, n)) continue;
            /* desconocido: se muestra en rojo y se salta su argumento si lo tiene */
            (void)save;
            struct sty sv = ST; ST.rgb = TEX_ERR; ST.style |= TS_MONO; add_word(S + s - 1, (int)n + 1); ST = sv; O->errors++;
            continue;
        }
        uint32_t a = P;
        while (P < end && S[P] != ' ' && S[P] != '\t' && S[P] != '\n' && S[P] != '\r' && S[P] != '\\' && S[P] != '$' && S[P] != '{' && S[P] != '}' && S[P] != '%') P++;
        add_text_chars(a, P);
    }
}

/* ---------------------------------------------------------------- bloques */
static void para_flush(void)
{
    if (!nit) return;
    int x0 = X0, w = COLW, ind = 0;
    if (pend_label_n) {                                    /* item de lista: etiqueta colgada a la izquierda */
        int bx = X0 - pend_label_w - BASE / 2;
        op_add(TOP_TEXT, bx, Y + asc_of(BASE) + (imax(asc_of(BASE) + desc_of(BASE) + 2, BASE * 135 / 100) - asc_of(BASE) - desc_of(BASE)) / 2, 0, 0, pend_label_px, pend_label_style, (list_depth > 0 && list_kind[list_depth - 1] == 1) ? TEX_INK : TEX_ACCENT, pend_label_s, pend_label_n);
        pend_label_n = 0;
    }
    flush_para(x0, w, align_mode, ind, BASE * 6 / 10);
}
static void block_math(uint32_t a, uint32_t b, int numbered, int multiline)
{
    struct sty save = ST;
    /* la formula interrumpe el parrafo: se cierra lo anterior */
    int saved_nit = nit; (void)saved_nit;
    para_flush();
    Y += BASE / 3;
    if (!multiline) {
        int o0; struct box bx = math_range(a, b, BASE, 1, &o0);
        int x = X0 + (COLW - bx.w) / 2; if (x < X0) x = X0;
        shift_ops(o0, O->nops, x, Y + bx.asc);
        if (numbered) { eqno++; char t[16] = "("; char nb[8]; num_str(nb, eqno); cat(t, nb, 16); cat(t, ")", 16); num_str(last_num, eqno);
            int w = tw(t, slen(t), BASE, 0); emit_text(X0 + COLW - w, Y + bx.asc, t, slen(t), BASE, 0, TEX_MUTED); }
        Y += bx.asc + bx.desc + BASE / 2;
    } else {
        /* align: filas por \\, columna de alineacion en el primer & */
        uint32_t rs = a;
        int rows_o[64][3]; struct box lb[64], rb[64]; int nr = 0, maxl = 0, maxr = 0; int num_row[64];
        while (rs < b && nr < 64) {
            uint32_t re = rs; int d = 0;
            while (re < b && !(d == 0 && S[re] == '\\' && re + 1 < b && S[re + 1] == '\\')) { if (S[re] == '{') d++; else if (S[re] == '}') d--; if (S[re] == '\\') re++; re++; }
            if (re > b) re = b;
            uint32_t amp = rs; d = 0; while (amp < re && !(d == 0 && S[amp] == '&')) { if (S[amp] == '{') d++; else if (S[amp] == '}') d--; if (S[amp] == '\\') amp++; amp++; }
            if (amp > re) amp = re;
            int nonum = 0; for (uint32_t k = rs; k + 7 < re; k++) if (S[k] == '\\' && (seq(S + k + 1, 8, "nonumber") || seq(S + k + 1, 5, "notag"))) nonum = 1;
            int o0; lb[nr] = math_range(rs, amp, BASE, 1, &o0); rows_o[nr][0] = o0; rows_o[nr][1] = O->nops;
            int o1; if (amp < re) g_init_prev = C_ORD; rb[nr] = amp < re ? math_range(amp + 1, re, BASE, 1, &o1) : (struct box){ 0, 0, 0 }; if (amp >= re) o1 = O->nops; rows_o[nr][2] = O->nops; rows_o[nr][1] = o1;
            num_row[nr] = numbered && !nonum;
            /* filas vacias (p. ej. \\ final) no cuentan */
            if (lb[nr].w || rb[nr].w) { maxl = imax(maxl, lb[nr].w); maxr = imax(maxr, rb[nr].w); nr++; }
            rs = re + 2;
        }
        int total = maxl + maxr, cx = X0 + (COLW - total) / 2; if (cx < X0) cx = X0;
        for (int r = 0; r < nr; r++) {
            int asc = imax(lb[r].asc, rb[r].asc), desc = imax(lb[r].desc, rb[r].desc);
            shift_ops(rows_o[r][0], rows_o[r][1], cx + maxl - lb[r].w, Y + asc);
            shift_ops(rows_o[r][1], rows_o[r][2], cx + maxl, Y + asc);
            if (num_row[r]) { eqno++; char t[16] = "("; char nb[8]; num_str(nb, eqno); cat(t, nb, 16); cat(t, ")", 16); num_str(last_num, eqno);
                int w = tw(t, slen(t), BASE, 0); emit_text(X0 + COLW - w, Y + asc, t, slen(t), BASE, 0, TEX_MUTED); }
            Y += asc + desc + BASE / 2;
        }
    }
    Y += BASE / 4;
    ST = save;
}
static void heading(int level, int star, uint32_t a, uint32_t b)
{
    para_flush();
    char num[24] = "";
    if (!star && level <= 3) {
        if (level == 1) { sec[0]++; sec[1] = sec[2] = 0; } else if (level == 2) { sec[1]++; sec[2] = 0; } else sec[2]++;
        for (int i = 0; i < level; i++) { char t[8]; num_str(t, sec[i]); cat(num, t, 24); if (i + 1 < level) cat(num, ".", 24); }
        int k = 0; while (num[k] && k < 15) { last_num[k] = num[k]; k++; } last_num[k] = 0;
        O->sections++;
    }
    int px = level == 1 ? BASE * 150 / 100 : level == 2 ? BASE * 125 / 100 : level == 3 ? BASE * 110 / 100 : BASE;
    Y += level == 1 ? BASE * 12 / 10 : BASE * 8 / 10;
    struct sty save = ST; ST.px = px; ST.style = TS_BOLD; ST.rgb = TEX_HEAD;
    if (num[0]) { ST.rgb = TEX_ACCENT; add_word(num, slen(num)); add_space(); add_space(); ST.rgb = TEX_HEAD; }
    struct rd r = push_src(a, b); inline_until(b); pop_src(r);
    int y0 = Y;
    flush_para(X0, COLW, 2, 0, 0);
    if (level == 1) { op_add(TOP_RECT, X0, Y + 3, COLW, 1, 0, 0, TEX_RULE, 0, 0); Y += 6; }
    (void)y0;
    Y += BASE * 4 / 10;
    ST = save;
}
static void make_title(void)
{
    para_flush();
    struct sty save = ST;
    if (have_title) {
        ST.px = BASE * 2; ST.style = TS_BOLD; ST.rgb = TEX_HEAD;
        struct rd r = push_src(title_a, title_b); inline_until(title_b); pop_src(r);
        /* titulo plano para la ventana */
        int k = 0; for (uint32_t i = title_a; i < title_b && k < 95; i++) if (S[i] != '\\' && S[i] != '{' && S[i] != '}') O->title[k++] = S[i];
        O->title[k] = 0;
        flush_para(X0, COLW, 1, 0, BASE / 2);
    }
    if (have_author) {
        ST.px = BASE * 115 / 100; ST.style = 0; ST.rgb = TEX_INK;
        struct rd r = push_src(author_a, author_b);
        /* \and separa autores */
        while (P < author_b) {
            if (S[P] == '\\' && P + 4 <= author_b && S[P + 1] == 'a' && S[P + 2] == 'n' && S[P + 3] == 'd') { P += 4; add_space(); add_word("·", 2); add_space(); continue; }
            uint32_t e = P; while (e < author_b && !(S[e] == '\\' && e + 4 <= author_b && S[e + 1] == 'a' && S[e + 2] == 'n' && S[e + 3] == 'd')) e++;
            inline_until(e);
        }
        pop_src(r);
        flush_para(X0, COLW, 1, 0, BASE / 4);
    }
    ST.px = BASE; ST.style = 0; ST.rgb = TEX_MUTED;
    if (have_date) { struct rd r = push_src(date_a, date_b); inline_until(date_b); pop_src(r); }
    else add_word("12 de noviembre de 2026", 23);
    flush_para(X0, COLW, 1, 0, BASE);
    ST = save;
}

static void body(uint32_t end, const char *env);
static void list_env(int kind, uint32_t end)
{
    para_flush();
    if (list_depth >= 6) { body(end, 0); return; }
    list_kind[list_depth] = kind; item_no[list_depth] = 0; list_depth++;
    int ind = BASE * 18 / 10; X0 += ind; COLW -= ind;
    body(end, 0);
    para_flush();
    pend_label_n = 0;
    X0 -= ind; COLW += ind; list_depth--;
    Y += BASE / 4;
}
static void do_item(void)
{
    para_flush();
    if (!list_depth) return;
    int k = list_kind[list_depth - 1], n = ++item_no[list_depth - 1];
    char lab[48] = "";
    uint32_t a, b;
    if (read_opt(&a, &b)) { int m = (int)(b - a); if (m > 46) m = 46; for (int i = 0; i < m; i++) lab[i] = S[a + i]; lab[m] = 0; }
    else if (k == 1) { char t[8]; num_str(t, n); if (list_depth == 2) { t[0] = (char)('a' + (n - 1) % 26); t[1] = 0; cat(lab, "(", 48); cat(lab, t, 48); cat(lab, ")", 48); } else { cat(lab, t, 48); cat(lab, ".", 48); } }
    else cat(lab, list_depth == 1 ? "•" : list_depth == 2 ? "–" : "∗", 48);
    if (k == 2) {                                             /* description: termino en negrita en linea */
        struct sty s = ST; ST.style |= TS_BOLD; add_word(lab, slen(lab)); ST = s; add_space(); return;
    }
    pend_label_n = (uint32_t)slen(lab); pend_label_s = pool_dupn(lab, &pend_label_n);
    pend_label_px = BASE; pend_label_style = k == 1 ? TS_BOLD : 0;
    pend_label_w = tw(lab, slen(lab), BASE, pend_label_style);
    if (pend_label_w < BASE / 2) pend_label_w = BASE / 2;
}
static void verbatim(uint32_t end)
{
    para_flush();
    int lh = BASE * 125 / 100, px = BASE * 85 / 100;
    if (pk() == '\n') P++;
    int y0 = Y; Y += BASE / 2;
    int o0 = op_add(TOP_RECT, X0, y0, COLW, 0, 0, 1, TEX_CODEBG, 0, 0);
    while (P < end) {
        uint32_t a = P; while (P < end && S[P] != '\n') P++;
        if (P > a || P < end) emit_text(X0 + BASE / 2, Y + asc_of(px), S + a, (int)(P - a), px, TS_MONO, TEX_HEAD);
        Y += lh; if (P < end) P++;
    }
    Y += BASE / 3;
    if (o0 >= 0) O->ops[o0].h = Y - y0;
    Y += BASE / 2;
}
static void tabular(uint32_t end)
{
    para_flush();
    uint32_t sa, sb; read_opt(&sa, &sb); read_group(&sa, &sb);
    char al[16]; int ncol = 0, vline[17] = { 0 };
    for (uint32_t i = sa; i < sb && ncol < 16; i++) { char c = S[i]; if (c == '|') vline[ncol] = 1; else if (c == 'l' || c == 'c' || c == 'r') al[ncol++] = c; else if (c == 'p' || c == 'X') { al[ncol++] = 'l'; if (i + 1 < sb && S[i + 1] == '{') { while (i < sb && S[i] != '}') i++; } } }
    if (!ncol) { al[0] = 'l'; ncol = 1; }
    /* filas */
    static struct cell { int o0, o1, w, asc, desc; } C[40][16];
    int hl[41] = { 0 }, nr = 0, rh_asc[40], rh_desc[40];
    uint32_t p = P;
    while (p < end && nr < 40) {
        while (p < end && (S[p] == ' ' || S[p] == '\n' || S[p] == '\t')) p++;
        if (p + 6 <= end && S[p] == '\\' && seq(S + p + 1, 5, "hline")) { hl[nr]++; p += 6; continue; }
        if (p + 6 <= end && S[p] == '\\' && seq(S + p + 1, 6, "toprul")) { hl[nr]++; p += 8; continue; }
        if (p + 8 <= end && S[p] == '\\' && (seq(S + p + 1, 7, "midrule") || seq(S + p + 1, 7, "bottomr"))) { hl[nr]++; p += S[p + 1] == 'm' ? 8 : 11; continue; }
        if (p >= end) break;
        uint32_t re = p; int d = 0;
        while (re < end && !(d == 0 && S[re] == '\\' && re + 1 < end && S[re + 1] == '\\')) { if (S[re] == '{') d++; else if (S[re] == '}') d--; if (S[re] == '\\') re++; re++; }
        if (re > end) re = end;
        uint32_t cs = p; int c = 0; rh_asc[nr] = rh_desc[nr] = 0;
        for (uint32_t q = p; ; q++) {
            int at_end = q >= re;
            if (!at_end && S[q] == '\\') { q++; continue; }
            if (!at_end && S[q] == '{') { d++; continue; }
            if (!at_end && S[q] == '}') { d--; continue; }
            if (at_end || (S[q] == '&' && d == 0)) {
                if (c < 16) {
                    struct sty s = ST; if (hl[nr] && nr == 0) ST.style |= TS_BOLD;
                    int o0 = O->nops;
                    struct rd r = push_src(cs, at_end ? re : q); nit = 0; inline_until(at_end ? re : q); pop_src(r);
                    /* una sola linea: se colocan los items seguidos */
                    int x = 0, asc = 0, desc = 0;
                    for (int k = 0; k < nit; k++) {
                        if (IT[k].kind == IT_WORD) op_add(TOP_TEXT, x, 0, 0, 0, IT[k].px, IT[k].style, IT[k].rgb, IT[k].s, IT[k].n);
                        else if (IT[k].kind == IT_BOX) shift_ops(IT[k].o0, IT[k].o1, x, 0);
                        if (IT[k].kind != IT_SPACE || (k > 0 && k + 1 < nit)) x += IT[k].w;
                        asc = imax(asc, IT[k].asc); desc = imax(desc, IT[k].desc);
                    }
                    nit = 0;
                    C[nr][c].o0 = o0; C[nr][c].o1 = O->nops; C[nr][c].w = x; C[nr][c].asc = asc; C[nr][c].desc = desc;
                    rh_asc[nr] = imax(rh_asc[nr], asc ? asc : asc_of(BASE)); rh_desc[nr] = imax(rh_desc[nr], desc ? desc : desc_of(BASE));
                    ST = s;
                }
                c++; cs = q + 1;
                if (at_end) break;
            }
        }
        for (; c < ncol && c < 16; c++) { C[nr][c].o0 = C[nr][c].o1 = O->nops; C[nr][c].w = 0; }
        nr++;
        p = re + 2;
    }
    P = end;
    int cw[16] = { 0 }, pad = BASE * 7 / 10;
    for (int r = 0; r < nr; r++) for (int c = 0; c < ncol; c++) cw[c] = imax(cw[c], C[r][c].w);
    int W = 0; for (int c = 0; c < ncol; c++) W += cw[c] + 2 * pad;
    int x0 = X0 + (COLW - W) / 2; if (x0 < X0) x0 = X0;
    Y += BASE / 3;
    int ytop = Y;
    for (int r = 0; r <= nr; r++) {
        if (hl[r]) op_add(TOP_RECT, x0, Y, W, r == 0 || r == nr ? 2 : 1, 0, 0, TEX_HEAD, 0, 0);
        if (r == nr) break;
        int rh = rh_asc[r] + rh_desc[r] + BASE / 2, base = Y + BASE / 4 + rh_asc[r];
        if (r == 0 && hl[0] && hl[1]) op_add(TOP_RECT, x0, Y + 2, W, rh - 2, 0, 1, TEX_CODEBG, 0, 0);   /* estilo 1: fondo (se pinta antes) */
        int x = x0;
        for (int c = 0; c < ncol; c++) {
            int dx = al[c] == 'r' ? cw[c] - C[r][c].w : al[c] == 'c' ? (cw[c] - C[r][c].w) / 2 : 0;
            shift_ops(C[r][c].o0, C[r][c].o1, x + pad + dx, base);
            x += cw[c] + 2 * pad;
        }
        Y += rh;
    }
    { int x = x0; for (int c = 0; c <= ncol; c++) { if (vline[c]) op_add(TOP_RECT, x, ytop, 1, Y - ytop, 0, 0, TEX_HEAD, 0, 0); if (c < ncol) x += cw[c] + 2 * pad; } }
    Y += BASE * 7 / 10;
    skip_end_env();
}
static void graphics(void)
{
    uint32_t oa = 0, ob = 0, a, b; int has_opt = read_opt(&oa, &ob); read_group(&a, &b);
    char path[128]; int n = (int)(b - a); if (n > 127) n = 127; for (int i = 0; i < n; i++) path[i] = S[a + i]; path[n] = 0;
    int iw = 0, ih = 0, ok = M->image && M->image(path, &iw, &ih, M->u) == 0 && iw > 0 && ih > 0;
    int w = ok ? iw : COLW / 2, h = ok ? ih : COLW / 4;
    if (has_opt) {                                           /* width=0.5\textwidth | width=8cm | scale=0.5 */
        for (uint32_t i = oa; i + 5 < ob; i++) if (seq(S + i, 5, "width") || seq(S + i, 5, "scale")) {
            int sc = S[i] == 's'; uint32_t k = i + 5; while (k < ob && (S[k] == '=' || S[k] == ' ')) k++;
            int ip = 0, fr = 0, fd = 1; while (k < ob && S[k] >= '0' && S[k] <= '9') ip = ip * 10 + (S[k++] - '0');
            if (k < ob && S[k] == '.') { k++; while (k < ob && S[k] >= '0' && S[k] <= '9' && fd < 10000) { fr = fr * 10 + (S[k++] - '0'); fd *= 10; } }
            int milli = ip * 1000 + fr * 1000 / fd;
            int nw = w;
            if (sc) nw = w * milli / 1000;
            else if (k < ob && S[k] == '\\') nw = COLW * milli / 1000;
            else if (k + 1 < ob && S[k] == 'c' && S[k + 1] == 'm') nw = milli * 38 / 1000;
            else if (k + 1 < ob && S[k] == 'p' && S[k + 1] == 't') nw = milli * 4 / 3000;
            else nw = milli / 1000;
            if (nw > 0) { h = h * nw / (w ? w : 1); w = nw; }
            break;
        }
    }
    if (w > COLW) { h = h * COLW / w; w = COLW; }
    int x = X0 + (COLW - w) / 2;
    Y += BASE / 3;
    if (ok) op_add(TOP_IMAGE, x, Y, w, h, 0, 0, 0, pool_put(path, (uint32_t)n), (uint32_t)n);
    else { op_add(TOP_RECT, x, Y, w, h, 0, 0, TEX_CODEBG, 0, 0); char t[160] = "imagen no encontrada: "; cat(t, path, 160); int tww = tw(t, slen(t), BASE * 8 / 10, 0);
           emit_text(x + (w - tww) / 2, Y + h / 2, t, slen(t), BASE * 8 / 10, 0, TEX_MUTED); }
    Y += h + BASE / 3;
}
static void caption(int fig)
{
    uint32_t a, b; read_opt(&a, &b); read_group(&a, &b);
    para_flush();
    struct sty s = ST; ST.px = BASE * 9 / 10; ST.style = TS_BOLD; ST.rgb = TEX_HEAD;
    char t[24] = ""; cat(t, fig ? "Figura " : "Tabla ", 24); char nb[8]; num_str(nb, fig ? ++figno : ++tabno); cat(t, nb, 24); cat(t, ":", 24);
    int k = 0; while (nb[k] && k < 15) { last_num[k] = nb[k]; k++; } last_num[k] = 0;
    add_word(t, slen(t)); add_space(); ST.style = 0; ST.rgb = TEX_MUTED;
    struct rd r = push_src(a, b); inline_until(b); pop_src(r);
    flush_para(X0 + BASE, COLW - 2 * BASE, 1, 0, BASE / 2);
    ST = s;
}

/* cuerpo de un entorno (o del documento) hasta 'end' */
static void body(uint32_t end, const char *env)
{
    (void)env;
    int blank = 0;
    if (end > L) end = L;
    uint32_t guard_p = 0xFFFFFFFFu; int guard_n = 0;
    while (P < end && !FULL) {
        if (P == guard_p) { if (++guard_n > 3) { P++; guard_n = 0; continue; } } else { guard_p = P; guard_n = 0; }
        char c = S[P];
        if (c == '%') { skip_comment(); continue; }
        if (c == '\n') { P++; uint32_t q = P; while (q < end && (S[q] == ' ' || S[q] == '\t' || S[q] == '\r')) q++; if (q < end && S[q] == '\n') { blank = 1; P = q; } else add_space(); continue; }
        if (blank) { para_flush(); blank = 0; }
        if (c == ' ' || c == '\t' || c == '\r') { P++; add_space(); continue; }
        if (c == '\\' && P + 1 < end && ((S[P + 1] >= 'a' && S[P + 1] <= 'z') || (S[P + 1] >= 'A' && S[P + 1] <= 'Z'))) {
            uint32_t save = P;
            P++; uint32_t s, n; read_cmd(&s, &n);
            const char *cmd = S + s; int star = cmd[n - 1] == '*'; uint32_t nn = star ? n - 1 : n;
            uint32_t a, b;
            if (seq(cmd, nn, "section") || seq(cmd, nn, "subsection") || seq(cmd, nn, "subsubsection") || seq(cmd, nn, "chapter")) {
                read_opt(&a, &b); read_group(&a, &b);
                heading(cmd[0] == 'c' ? 1 : cmd[3] == 't' && cmd[0] == 's' && cmd[1] == 'e' ? 1 : seq(cmd, nn, "subsection") ? 2 : 3, star, a, b); continue;
            }
            if (seq(cmd, nn, "paragraph")) { read_group(&a, &b); para_flush(); text_group(a, b, TS_BOLD, 0, TEX_HEAD); add_space(); continue; }
            if (seq(cmd, nn, "documentclass") || seq(cmd, nn, "usepackage") || seq(cmd, nn, "pagestyle") || seq(cmd, nn, "thispagestyle") || seq(cmd, nn, "setlength") || seq(cmd, nn, "geometry") || seq(cmd, nn, "hypersetup") || seq(cmd, nn, "newcommand") || seq(cmd, nn, "renewcommand") || seq(cmd, nn, "bibliographystyle") || seq(cmd, nn, "setcounter")) {
                read_opt(&a, &b); read_group(&a, &b);
                if (seq(cmd, nn, "setlength") || seq(cmd, nn, "newcommand") || seq(cmd, nn, "renewcommand") || seq(cmd, nn, "setcounter")) { read_opt(&a, &b); read_group(&a, &b); }
                continue;
            }
            if (seq(cmd, nn, "title")) { read_opt(&a, &b); read_group(&title_a, &title_b); have_title = 1; continue; }
            if (seq(cmd, nn, "author")) { read_group(&author_a, &author_b); have_author = 1; continue; }
            if (seq(cmd, nn, "date")) { read_group(&date_a, &date_b); have_date = 1; continue; }
            if (seq(cmd, nn, "maketitle")) { make_title(); continue; }
            if (seq(cmd, nn, "tableofcontents") || seq(cmd, nn, "listoffigures") || seq(cmd, nn, "bibliography") || seq(cmd, nn, "appendix")) { if (cmd[0] == 'b') read_group(&a, &b); continue; }
            if (seq(cmd, nn, "item")) { do_item(); continue; }
            if (seq(cmd, nn, "par")) { para_flush(); continue; }
            if (seq(cmd, nn, "newpage") || seq(cmd, nn, "clearpage") || seq(cmd, nn, "pagebreak")) { para_flush(); Y += BASE; op_add(TOP_RECT, X0 + COLW / 3, Y, COLW / 3, 1, 0, 0, TEX_RULE, 0, 0); Y += BASE; continue; }
            if (seq(cmd, nn, "vspace") || seq(cmd, nn, "vskip") || seq(cmd, nn, "bigskip") || seq(cmd, nn, "medskip") || seq(cmd, nn, "smallskip")) { para_flush(); if (cmd[0] == 'v' && cmd[1] == 's') read_group(&a, &b); Y += cmd[0] == 'b' ? BASE : BASE / 2; continue; }
            if (seq(cmd, nn, "hrule") || seq(cmd, nn, "rule") || seq(cmd, nn, "hrulefill")) { para_flush(); if (cmd[0] == 'r') { read_opt(&a, &b); read_group(&a, &b); read_group(&a, &b); } op_add(TOP_RECT, X0, Y + BASE / 2, COLW, 1, 0, 0, TEX_RULE, 0, 0); Y += BASE; continue; }
            if (seq(cmd, nn, "includegraphics")) { para_flush(); graphics(); continue; }
            if (seq(cmd, nn, "caption")) { caption(cur_float != 2); continue; }
            if (seq(cmd, nn, "begin")) {
                read_group(&a, &b);
                const char *e = S + a; uint32_t en = b - a;
                char ename[24]; int k = 0; for (uint32_t i = 0; i < en && k < 23; i++) ename[k++] = e[i]; ename[k] = 0;
                uint32_t stop = find_end_env(ename); if (stop > end) stop = end;
                if (seq(e, en, "document")) { body(stop, ename); skip_end_env(); continue; }
                if (seq(e, en, "itemize")) { list_env(0, stop); skip_end_env(); continue; }
                if (seq(e, en, "enumerate")) { list_env(1, stop); skip_end_env(); continue; }
                if (seq(e, en, "description")) { list_env(2, stop); skip_end_env(); continue; }
                if (seq(e, en, "equation") || seq(e, en, "equation*") || seq(e, en, "displaymath") || seq(e, en, "math")) { uint32_t s0 = P; P = stop; block_math(s0, stop, seq(e, en, "equation"), 0); skip_end_env(); continue; }
                if (seq(e, en, "align") || seq(e, en, "align*") || seq(e, en, "eqnarray") || seq(e, en, "eqnarray*") || seq(e, en, "gather") || seq(e, en, "gather*") || seq(e, en, "multline") || seq(e, en, "multline*")) {
                    uint32_t s0 = P; P = stop; block_math(s0, stop, e[en - 1] != '*', 1); skip_end_env(); continue; }
                if (seq(e, en, "verbatim") || seq(e, en, "lstlisting") || seq(e, en, "minted")) { if (e[0] != 'v') { read_opt(&a, &b); if (e[0] == 'm') read_group(&a, &b); } verbatim(stop); skip_end_env(); continue; }
                if (seq(e, en, "tabular") || seq(e, en, "tabularx") || seq(e, en, "array")) { if (e[7] == 'x') read_group(&a, &b); tabular(stop); continue; }
                if (seq(e, en, "center") || seq(e, en, "figure") || seq(e, en, "figure*") || seq(e, en, "table") || seq(e, en, "flushleft") || seq(e, en, "flushright")) {
                    para_flush(); read_opt(&a, &b);
                    int sv = align_mode; if (e[0] == 'c') align_mode = 1;
                    int sf = cur_float; if (e[0] == 'f') cur_float = 1; else if (e[0] == 't') cur_float = 2;
                    body(stop, ename); para_flush(); align_mode = sv; cur_float = sf; skip_end_env(); Y += BASE / 3; continue; }
                if (seq(e, en, "quote") || seq(e, en, "quotation") || seq(e, en, "abstract")) {
                    para_flush();
                    int ind = BASE * 2; X0 += ind; COLW -= 2 * ind;
                    struct sty s = ST;
                    if (e[0] == 'a') { in_abstract = 1; ST.px = BASE * 92 / 100; struct sty h = ST; ST.style = TS_BOLD; ST.rgb = TEX_HEAD; add_word("Resumen", 7); flush_para(X0, COLW, 1, 0, BASE / 3); ST = h; }
                    else ST.style |= TS_ITALIC, ST.rgb = 0x00374151;
                    int y0 = Y;
                    body(stop, ename); para_flush();
                    if (e[0] == 'q') op_add(TOP_RECT, X0 - BASE, y0, 3, Y - y0 - BASE / 2, 0, 0, TEX_ACCENT, 0, 0);
                    ST = s; in_abstract = 0;
                    X0 -= ind; COLW += 2 * ind; skip_end_env(); Y += BASE / 3; continue; }
                if (seq(e, en, "thebibliography")) { read_group(&a, &b); static const char refs[] = "Referencias"; struct rd r = push_src(0, 0); S = refs; L = sizeof refs - 1; heading(1, 1, 0, L); pop_src(r); list_env(1, stop); skip_end_env(); continue; }
                /* entorno desconocido: se muestra su contenido tal cual */
                body(stop, ename); skip_end_env(); continue;
            }
            if (seq(cmd, nn, "end")) { read_group(&a, &b); continue; }
            if (seq(cmd, nn, "bibitem")) { read_opt(&a, &b); read_group(&a, &b); do_item(); continue; }
            P = save;
        }
        /* texto en linea hasta el proximo salto de linea o comando de bloque */
        uint32_t e = P;
        {
            /* avanza un "trozo" en linea: hasta el fin de linea */
            while (e < end && S[e] != '\n') {
                if (S[e] == '\\' && e + 1 < end && (S[e + 1] == '[' || S[e + 1] == '(')) {
                    /* el math puede tener saltos dentro: incluirlo entero */
                    const char *cl = S[e + 1] == '[' ? "\\]" : "\\)";
                    uint32_t sv = P; P = e + 2; uint32_t f = find_close(cl); P = sv; e = f + 2 > end ? end : f + 2; continue;
                }
                if (S[e] == '$') { uint32_t sv = P; int dbl = e + 1 < end && S[e + 1] == '$'; P = e + (dbl ? 2 : 1); uint32_t f = find_close(dbl ? "$$" : "$"); P = sv; e = f + (dbl ? 2 : 1); if (e > end) e = end; continue; }
                if (S[e] == '{') { int d = 1; e++; while (e < end && d) { if (S[e] == '\\') { e += 2; continue; } if (S[e] == '{') d++; else if (S[e] == '}') d--; e++; } continue; }
                if (S[e] == '\\') {
                    uint32_t q = e + 1, r = q; while (r < end && ((S[r] >= 'a' && S[r] <= 'z') || (S[r] >= 'A' && S[r] <= 'Z'))) r++;
                    if (r > q && (seq(S + q, r - q, "begin") || seq(S + q, r - q, "end") || seq(S + q, r - q, "item") || seq(S + q, r - q, "section") || seq(S + q, r - q, "subsection") || seq(S + q, r - q, "subsubsection") || seq(S + q, r - q, "par") || seq(S + q, r - q, "maketitle") || seq(S + q, r - q, "includegraphics") || seq(S + q, r - q, "caption") || seq(S + q, r - q, "paragraph") || seq(S + q, r - q, "bibitem") || seq(S + q, r - q, "hrule") || seq(S + q, r - q, "vspace") || seq(S + q, r - q, "newpage")))
                        break;
                    e = r > q ? r : q + 1; continue;
                }
                e++;
            }
        }
        if (e == P) { if (P < end && S[P] == '\\') { /* comando de bloque que no se trato arriba */ uint32_t save = P; P++; uint32_t s, n; read_cmd(&s, &n); if (!inline_cmd(S + s, n)) { struct sty sv = ST; ST.rgb = TEX_ERR; ST.style |= TS_MONO; add_word(S + save, (int)(P - save)); ST = sv; O->errors++; } } else P++; continue; }
        inline_until(e);
    }
    para_flush();
}

/* ================================================================ entrada */
int tex_is_latex(const char *src, uint32_t len)
{
    static const char *const keys[] = { "\\documentclass", "\\section", "\\begin{", "\\frac", "\\maketitle", "\\textbf", "\\[", "$$", 0 };
    for (int k = 0; keys[k]; k++) { int kl = slen(keys[k]); for (uint32_t i = 0; i + (uint32_t)kl <= len; i++) { int ok = 1; for (int j = 0; j < kl; j++) if (src[i + j] != keys[k][j]) { ok = 0; break; } if (ok) return 1; } }
    int dollars = 0; for (uint32_t i = 0; i < len; i++) if (src[i] == '$') dollars++;
    return dollars >= 2 && (dollars % 2) == 0;
}

static void run(const char *src, uint32_t len, int width)
{
    S = src; L = len; P = 0;
    O->nops = 0; O->npool = 0; O->errors = 0; O->sections = 0; O->equations = 0; O->footnotes = 0; O->title[0] = 0;
    nit = 0; nmn = 0; FULL = 0; Y = 0; X0 = 0; COLW = width;
    ST.style = 0; ST.px = BASE; ST.rgb = TEX_INK;
    list_depth = 0; pend_label_n = 0; have_title = have_author = have_date = 0;
    sec[0] = sec[1] = sec[2] = 0; eqno = figno = tabno = fnno = 0; last_num[0] = 0; align_mode = 0; in_abstract = 0;
    /* si hay \begin{document}, el preambulo solo aporta \title, \author y \date */
    body(len, 0);
    para_flush();
    if (fnno) {                                              /* notas al pie al final */
        Y += BASE / 2;
        op_add(TOP_RECT, X0, Y, COLW / 3, 1, 0, 0, TEX_RULE, 0, 0); Y += BASE / 2;
        int nf = fnno > 32 ? 32 : fnno;
        for (int i = 0; i < nf; i++) {
            ST.px = BASE * 85 / 100; ST.style = 0; ST.rgb = TEX_MUTED;
            char t[8]; num_str(t, i + 1); ST.rgb = TEX_LINK; add_word(t, slen(t)); add_space(); ST.rgb = TEX_MUTED;
            struct rd r = push_src(fn_a[i], fn_b[i]); inline_until(fn_b[i]); pop_src(r);
            flush_para(X0, COLW, 2, 0, BASE / 4);
        }
        O->footnotes = fnno;
    }
    O->equations = eqno;
    O->height = Y + BASE;
}

int tex_typeset(const char *src, uint32_t len, int width, int base_px, const struct tex_metrics *m, struct tex_out *o)
{
    if (!src || !o || !m || !m->width || width < 50) return -1;
    O = o; M = m; BASE = base_px < 8 ? 8 : base_px; WIDTH = width;
    nlabs = 0;
    pass_no = 1; run(src, len, width);                       /* 1: recoge \label para \ref hacia delante */
    pass_no = 2; run(src, len, width);
    /* SOMA_322_TEX_HOSTIL: red de seguridad para quien dibuja: ninguna operacion apunta fuera de lo escrito en el pool */
    for (int i = 0; i < o->nops; i++)
        if ((o->ops[i].type == TOP_TEXT || o->ops[i].type == TOP_IMAGE) && (o->ops[i].s > o->npool || o->ops[i].n > o->npool - o->ops[i].s)) { o->ops[i].s = 0; o->ops[i].n = 0; }
    return FULL ? -1 : 0;
}
