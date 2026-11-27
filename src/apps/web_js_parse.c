/* SOMA_PARCHE_NAVEGADOR: analizador lexico y sintactico de JavaScript. */
#include "web_js_i.h"

struct pjmp { struct pjmp *up; void *jb[5]; };
struct tok { int type; char op[5]; jstr *str; f64 num; int nl; const char *start; int line; int kw; int big; };
enum { TK_EOF = 0, TK_NUM, TK_STR, TK_TPL, TK_ID, TK_PUNCT, TK_REGEX };
struct jfc { struct jfc *up; struct jfi *fi; int is_arrow; };
struct ps {
    struct jrt *rt; const char *s, *e, *cur; struct tok tok; int line; int prev_regex_ok;
    struct jfc *fc; struct pjmp *jmp; int *bcap; int no_in; int in_async; int in_gen; int in_fn; int nfunc; int strict;
    const char *prev_start, *prev2_start, *prev_end; uint32_t ntok;   /* SOMA 188B: inicio y fin de los ultimos tokens (texto exacto de Function.prototype.toString) */
    char err[160]; int err_line; const char *fsrc;
    int spec;   /* SOMA_352: > 0 = analisis de prueba (try_arrow): el error se descarta, no se formatea */
};

static void *pal(struct ps *p, uint32_t n) { void *m = js_alloc(p->rt, n); if (!m) { p->rt->halted = 1; } return m; }
static void perr(struct ps *p, const char *msg)
{
    if (!p->err[0]) {
        wu_copy(p->err, msg, sizeof p->err); p->err_line = p->tok.line ? p->tok.line : p->line;
        const char *at = p->tok.start ? p->tok.start : p->cur;   /* SOMA_188: columna y fragmento (lineas minificadas de miles de caracteres) */
        /* SOMA_352_MENOS_MAQUETAR: en un analisis de prueba ("(" que podria ser una flecha) el error se tira: buscar el principio de
         * la linea era recorrer hacia atras todo el archivo minificado (una sola linea de 500 KB) en cada "(" que no era flecha.
         * El SDK de consentimiento de 20minutos pasaba ahi el 86 % de su analisis. */
        if (!p->spec && at && at >= p->s && at <= p->e) {
            const char *ls = at; while (ls > p->s && ls[-1] != '\n') ls--;
            uint32_t k = wu_strlen(p->err); char *o = p->err;
            if (k + 40 < sizeof p->err) { wu_copy(o + k, " (col ", sizeof p->err - k); k += 6; k += (uint32_t)wu_itoa((int64_t)(at - ls) + 1, o + k); o[k++] = ':'; o[k++] = ' ';
                const char *f = at - 20 > ls ? at - 20 : ls; while (f < p->e && f < at + 20 && k + 2 < sizeof p->err) { char ch = *f++; o[k++] = ch == '\n' || ch == '\r' || ch == '\t' ? ' ' : ch; } o[k++] = ')'; o[k] = 0; }
        }
    }
    struct pjmp *j = p->jmp; if (!j) return;
    p->jmp = j->up; __builtin_longjmp(j->jb, 1);
}
static struct jn *mk(struct ps *p, int t) { struct jn *n = (struct jn *)pal(p, sizeof *n); if (!n) perr(p, "memoria agotada"); n->t = (uint8_t)t; n->line = p->tok.line;
#ifdef WJS_DEBUG_COL
    { const char *a = p->tok.start ? p->tok.start : p->cur, *ls = a; while (ls > p->s && ls[-1] != '\n') ls--; n->col = (int)(a - ls) + 1; }
#endif
    return n; }

/* ------------------------------ lexer ------------------------------ */
static int idstart(int c) { return wu_isalpha(c) || c == '_' || c == '$' || c >= 0x80 || c == '#'; }
static int idpart(int c) { return (idstart(c) && c != '#') || wu_isdigit(c); }   /* SOMA_188: '#' solo al principio (async#m) */
static const char *KW[] = { "var", "let", "const", "function", "return", "if", "else", "for", "while", "do", "break", "continue", "switch", "case", "default", "throw", "try", "catch", "finally",
    "new", "delete", "typeof", "instanceof", "in", "void", "this", "null", "true", "false", "class", "extends", "super", "yield", "import", "export", "debugger", "with", "enum", 0 };
static int is_kw(const char *s, uint32_t n)
{
    if (n < 2 || n > 10 || s[0] < 'b' || s[0] > 'y') return 0;   /* SOMA_352: las palabras reservadas van de "do" a "instanceof" y de b a y */
    for (int i = 0; KW[i]; i++) if (KW[i][0] == s[0] && wu_strlen(KW[i]) == n && wu_strneq(KW[i], s, n)) return 1;
    return 0;
}
static void skip_ws(struct ps *p)
{
    for (;;) {
        char c = p->cur < p->e ? *p->cur : 0;
        if (c == '\n') { p->tok.nl = 1; p->line++; p->cur++; }
        else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') p->cur++;
        else if ((uint8_t)c == 0xC2 && p->cur + 1 < p->e && (uint8_t)p->cur[1] == 0xA0) p->cur += 2;
        else if (c == '/' && p->cur + 1 < p->e && p->cur[1] == '/') { while (p->cur < p->e && *p->cur != '\n') p->cur++; }
        else if (c == '/' && p->cur + 1 < p->e && p->cur[1] == '*') { p->cur += 2; while (p->cur + 1 < p->e && !(p->cur[0] == '*' && p->cur[1] == '/')) { if (*p->cur == '\n') { p->tok.nl = 1; p->line++; } p->cur++; } p->cur += 2; }
        else if (c == '<' && p->cur + 3 < p->e && !memcmp(p->cur, "<!--", 4)) { while (p->cur < p->e && *p->cur != '\n') p->cur++; }
        else if (c == '#' && p->cur == p->s && p->cur + 1 < p->e && p->cur[1] == '!') { while (p->cur < p->e && *p->cur != '\n') p->cur++; }
        else break;
    }
}
static int hexd(int c) { return wu_ishex(c) ? wu_hexval(c) : -1; }
static void put_utf8(struct wb *b, uint32_t cp) { char t[4]; int n = wu_utf8_put(cp, t); wb_addn(b, t, (uint32_t)n); }
/* lee un escape tras '\\' ; cur apunta al caracter siguiente */
static void read_escape(struct ps *p, struct wb *b)
{
    char c = *p->cur++;
    switch (c) {
    case 'n': wb_addc(b, '\n'); break; case 't': wb_addc(b, '\t'); break; case 'r': wb_addc(b, '\r'); break;
    case 'b': wb_addc(b, '\b'); break; case 'f': wb_addc(b, '\f'); break; case 'v': wb_addc(b, '\v'); break;
    case '0': if (!wu_isdigit(*p->cur)) { wb_addc(b, 0); break; } /* fallthrough */
    case '1': case '2': case '3': case '4': case '5': case '6': case '7': { int v = c - '0'; int k = 0; while (k < 2 && *p->cur >= '0' && *p->cur <= '7') { v = v * 8 + (*p->cur++ - '0'); k++; } put_utf8(b, (uint32_t)v); break; }
    case 'x': { int h1 = hexd(p->cur[0]), h2 = hexd(p->cur[1]); if (h1 < 0 || h2 < 0) perr(p, "escape \\x invalido"); p->cur += 2; put_utf8(b, (uint32_t)(h1 * 16 + h2)); break; }
    case 'u': {
        uint32_t cp = 0;
        if (*p->cur == '{') { p->cur++; while (*p->cur && *p->cur != '}') { int h = hexd(*p->cur++); if (h < 0) perr(p, "escape \\u invalido"); cp = cp * 16 + (uint32_t)h; } p->cur++; }
        else { for (int i = 0; i < 4; i++) { int h = hexd(*p->cur++); if (h < 0) perr(p, "escape \\u invalido"); cp = cp * 16 + (uint32_t)h; }
               if (cp >= 0xD800 && cp < 0xDC00 && p->cur[0] == '\\' && p->cur[1] == 'u') { uint32_t lo = 0; const char *q = p->cur + 2; int okk = 1; for (int i = 0; i < 4; i++) { int h = hexd(q[i]); if (h < 0) { okk = 0; break; } lo = lo * 16 + (uint32_t)h; } if (okk && lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p->cur += 6; } } }
        put_utf8(b, cp); break; }
    case '\r': if (*p->cur == '\n') p->cur++; p->line++; break;
    case '\n': p->line++; break;
    default: wb_addc(b, c);
    }
}
static jstr *mkstr(struct ps *p, const char *s, uint32_t n) { return js_str_new(p->rt, s, n); }
static const char *PUNCTS[] = { ">>>=", "...", "===", "!==", "**=", "<<=", ">>=", ">>>", "&&=", "||=", "?\?=", "=>", "==", "!=", "<=", ">=", "&&", "||", "??", "?.", "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>", "**",
    "{", "}", "(", ")", "[", "]", ";", ",", "<", ">", "+", "-", "*", "/", "%", "&", "|", "^", "!", "~", "?", ":", "=", ".", "@", 0 };
static void lex(struct ps *p)
{
    struct tok *t = &p->tok; int nl = 0; (void)nl; t->nl = 0;
    if ((++p->ntok & 255) == 0) wu_tick();   /* SOMA 189: analizar un script de megas tarda segundos en un equipo lento; hay que ceder la CPU (la GUI se congelaba y saltaba el vigilante) */
    skip_ws(p);
    t->start = p->cur; t->line = p->line; t->kw = 0; t->str = 0; t->op[0] = 0; t->big = 0;
    if (p->cur >= p->e) { t->type = TK_EOF; return; }
    unsigned char c = (unsigned char)*p->cur;
    if (idstart(c) || c == '\\') {
        const char *s = p->cur; int esc = 0;
        if (c == '#') p->cur++;
        while (p->cur < p->e && (idpart((unsigned char)*p->cur) || (*p->cur == '\\' && p->cur + 1 < p->e && p->cur[1] == 'u'))) { if (*p->cur == '\\') esc = 1; p->cur++; }
        if (esc) {   /* SOMA_188: escapes \uXXXX y \u{...} en identificadores (Angular: \u0275prov) */
            char b[256]; uint32_t k = 0;
            for (const char *q = s; q < p->cur && k + 5 < sizeof b; ) {
                if (*q == '\\' && q + 1 < p->cur && q[1] == 'u') {
                    q += 2; uint32_t cp = 0;
                    if (q < p->cur && *q == '{') { q++; while (q < p->cur && *q != '}') { char d = *q++; cp = cp * 16 + (uint32_t)(d <= '9' ? d - '0' : (d | 32) - 'a' + 10); } q++; }
                    else for (int h = 0; h < 4 && q < p->cur; h++) { char d = *q++; cp = cp * 16 + (uint32_t)(d <= '9' ? d - '0' : (d | 32) - 'a' + 10); }
                    if (cp < 0x80) b[k++] = (char)cp; else if (cp < 0x800) { b[k++] = (char)(0xC0 | (cp >> 6)); b[k++] = (char)(0x80 | (cp & 63)); }
                    else if (cp < 0x10000) { b[k++] = (char)(0xE0 | (cp >> 12)); b[k++] = (char)(0x80 | ((cp >> 6) & 63)); b[k++] = (char)(0x80 | (cp & 63)); }
                    else { b[k++] = (char)(0xF0 | (cp >> 18)); b[k++] = (char)(0x80 | ((cp >> 12) & 63)); b[k++] = (char)(0x80 | ((cp >> 6) & 63)); b[k++] = (char)(0x80 | (cp & 63)); }
                } else b[k++] = *q++;
            }
            t->type = TK_ID; t->str = js_intern_n(p->rt, b, k); t->kw = 0;   /* un nombre con escapes nunca es palabra reservada */
        } else { t->type = TK_ID; t->str = js_intern_n(p->rt, s, (uint32_t)(p->cur - s)); t->kw = is_kw(s, (uint32_t)(p->cur - s)); }
        p->prev_regex_ok = t->kw && !(wu_streq(t->str->d, "this") || wu_streq(t->str->d, "null") || wu_streq(t->str->d, "true") || wu_streq(t->str->d, "false") || wu_streq(t->str->d, "super"));
        return;
    }
    if (wu_isdigit(c) || (c == '.' && wu_isdigit((unsigned char)p->cur[1]))) {
        const char *s = p->cur; t->type = TK_NUM; p->prev_regex_ok = 0;
        if (c == '0' && (p->cur[1] == 'x' || p->cur[1] == 'X' || p->cur[1] == 'o' || p->cur[1] == 'O' || p->cur[1] == 'b' || p->cur[1] == 'B')) {
            int base = (p->cur[1] | 32) == 'x' ? 16 : (p->cur[1] | 32) == 'o' ? 8 : 2; p->cur += 2; f64 v = 0; f64 fb = f64_from_i32(base);
            while (p->cur < p->e) { int d = *p->cur == '_' ? -2 : hexd(*p->cur); if (d == -2) { p->cur++; continue; } if (d < 0 || d >= base) break; v = f64_add(f64_mul(v, fb), f64_from_i32(d)); p->cur++; }
            if (*p->cur == 'n') { t->big = 1; t->str = mkstr(p, s, (uint32_t)(p->cur - s)); p->cur++; }   /* SOMA 188B: literal BigInt */
            t->num = v; return;
        }
        struct wb nb = {0};
        while (p->cur < p->e && (wu_isdigit((unsigned char)*p->cur) || *p->cur == '_')) { if (*p->cur != '_') wb_addc(&nb, *p->cur); p->cur++; }
        if (*p->cur == '.') { wb_addc(&nb, '.'); p->cur++; while (p->cur < p->e && (wu_isdigit((unsigned char)*p->cur) || *p->cur == '_')) { if (*p->cur != '_') wb_addc(&nb, *p->cur); p->cur++; } }
        if ((*p->cur | 32) == 'e' && (wu_isdigit((unsigned char)p->cur[1]) || ((p->cur[1] == '+' || p->cur[1] == '-') && wu_isdigit((unsigned char)p->cur[2])))) {
            wb_addc(&nb, 'e'); p->cur++; if (*p->cur == '+' || *p->cur == '-') wb_addc(&nb, *p->cur++); while (wu_isdigit((unsigned char)*p->cur)) wb_addc(&nb, *p->cur++);
        }
        if (*p->cur == 'n') { t->big = 1; t->str = mkstr(p, s, (uint32_t)(p->cur - s)); p->cur++; }
        (void)s; t->num = nb.p ? f64_parse(nb.p, 0) : 0; wb_free(&nb); return;
    }
    if (c == '"' || c == '\'') {
        char q = (char)c; p->cur++; struct wb b = {0};
        while (p->cur < p->e && *p->cur != q) {
            if (*p->cur == '\\') { p->cur++; read_escape(p, &b); }
            else if (*p->cur == '\n') perr(p, "cadena sin cerrar");
            else wb_addc(&b, *p->cur++);
        }
        if (p->cur >= p->e) perr(p, "cadena sin cerrar");
        p->cur++; t->type = TK_STR; t->str = mkstr(p, b.p ? b.p : "", b.len); wb_free(&b); p->prev_regex_ok = 0; return;
    }
    if (c == '`') {
        /* plantilla: se guarda el texto crudo completo (sin comillas) para analizarlo despues */
        p->cur++; const char *s = p->cur; int depth = 0;
        while (p->cur < p->e) {
            if (*p->cur == '\\') { p->cur += 2; continue; }
            if (depth == 0 && *p->cur == '`') break;
            if (*p->cur == '$' && p->cur[1] == '{') { depth++; p->cur += 2; continue; }
            if (depth > 0) {
                if (*p->cur == '{') depth++;
                else if (*p->cur == '}') depth--;
                else if (*p->cur == '"' || *p->cur == '\'') { char q = *p->cur++; while (p->cur < p->e && *p->cur != q) { if (*p->cur == '\\') p->cur++; p->cur++; } }
                else if (*p->cur == '`') { /* plantilla anidada */ p->cur++; int d2 = 0; while (p->cur < p->e) { if (*p->cur == '\\') { p->cur += 2; continue; } if (d2 == 0 && *p->cur == '`') break; if (*p->cur == '$' && p->cur[1] == '{') d2++; else if (d2 > 0 && *p->cur == '}') d2--; p->cur++; } }
            }
            if (*p->cur == '\n') p->line++;
            p->cur++;
        }
        if (p->cur >= p->e) perr(p, "plantilla sin cerrar");
        t->type = TK_TPL; t->str = mkstr(p, s, (uint32_t)(p->cur - s)); p->cur++; p->prev_regex_ok = 0; return;
    }
    if (c == '/' && p->prev_regex_ok) {
        const char *s = ++p->cur; int incls = 0;
        while (p->cur < p->e && (*p->cur != '/' || incls)) {
            if (*p->cur == '\n') perr(p, "regex sin cerrar");
            if (*p->cur == '\\') p->cur++; else if (*p->cur == '[') incls = 1; else if (*p->cur == ']') incls = 0;
            p->cur++;
        }
        if (p->cur >= p->e) perr(p, "regex sin cerrar");
        t->str = mkstr(p, s, (uint32_t)(p->cur - s)); p->cur++;
        const char *fs = p->cur; while (p->cur < p->e && wu_isalpha((unsigned char)*p->cur)) p->cur++;
        t->type = TK_REGEX; t->op[0] = 0; t->num = f64_from_u64(0);
        { jstr *fl = mkstr(p, fs, (uint32_t)(p->cur - fs)); t->kw = 0; t->start = (const char *)fl; }
        p->prev_regex_ok = 0; return;
    }
    for (int i = 0; PUNCTS[i]; i++) {
        if (PUNCTS[i][0] != (char)c) continue;   /* SOMA_352: antes, strlen y memcmp con los 58 operadores en cada simbolo */
        uint32_t n = wu_strlen(PUNCTS[i]);
        if (p->cur + n <= p->e && !memcmp(p->cur, PUNCTS[i], n)) {
            if (n == 2 && PUNCTS[i][0] == '?' && PUNCTS[i][1] == '.' && wu_isdigit((unsigned char)p->cur[2])) continue;   /* a?.5:1 */
            memcpy(t->op, PUNCTS[i], n); t->op[n] = 0; t->type = TK_PUNCT; p->cur += n;
            p->prev_regex_ok = !(wu_streq(t->op, ")") || wu_streq(t->op, "]") || wu_streq(t->op, "}") || wu_streq(t->op, "++") || wu_streq(t->op, "--"));
            return;
        }
    }
    perr(p, "caracter inesperado");
}
static void next(struct ps *p) { p->prev2_start = p->prev_start; p->prev_start = p->tok.start; p->prev_end = p->cur; lex(p); }
static int isp(struct ps *p, const char *op) { return p->tok.type == TK_PUNCT && wu_streq(p->tok.op, op); }
static int isid(struct ps *p, const char *name) { return p->tok.type == TK_ID && wu_streq(p->tok.str->d, name); }
static int eatp(struct ps *p, const char *op) { if (isp(p, op)) { next(p); return 1; } return 0; }
static void expectp(struct ps *p, const char *op) { if (!eatp(p, op)) { char m[80]; wu_copy(m, "se esperaba '", sizeof m); uint32_t n = wu_strlen(m); wu_copy(m + n, op, sizeof m - n); n = wu_strlen(m); m[n++] = '\''; m[n] = 0; perr(p, m); } }
static int eatid(struct ps *p, const char *name) { if (isid(p, name)) { next(p); return 1; } return 0; }
static void asi(struct ps *p) { if (eatp(p, ";")) return; if (isp(p, "}") || p->tok.type == TK_EOF || p->tok.nl) return; perr(p, "se esperaba ';'"); }
struct lstate { const char *cur; struct tok tok; int line; int rx; };
static void save(struct ps *p, struct lstate *s) { s->cur = p->cur; s->tok = p->tok; s->line = p->line; s->rx = p->prev_regex_ok; }
static void restore(struct ps *p, const struct lstate *s) { p->cur = s->cur; p->tok = s->tok; p->line = s->line; p->prev_regex_ok = s->rx; }

/* ------------------------------ analisis ------------------------------ */
static struct jn *parse_assign(struct ps *p);
static struct jn *parse_expr(struct ps *p);
static struct jn *parse_stmt(struct ps *p);
static struct jn *parse_function(struct ps *p, int flags, int is_decl);
static struct jn *parse_class(struct ps *p, int is_decl);
static struct jn *parse_block(struct ps *p);

static void add_var(struct ps *p, jstr *name)
{
    struct jfi *fi = p->fc ? p->fc->fi : 0; if (!fi) return;
    for (int i = 0; i < fi->nvn; i++) if (fi->vn[i] == name) return;
    jstr **nv = (jstr **)pal(p, (uint32_t)(fi->nvn + 1) * sizeof(jstr *)); if (fi->nvn) memcpy(nv, fi->vn, (uint32_t)fi->nvn * sizeof(jstr *));
    nv[fi->nvn++] = name; fi->vn = nv; fi->ncap++;
}
static void note_lex(struct ps *p) { if (p->bcap) (*p->bcap)++; }
static void collect_names(struct ps *p, struct jn *pat, int isvar)
{
    if (!pat) return;
    switch (pat->t) {
    case N_IDENT: if (isvar) add_var(p, pat->str); else note_lex(p); break;
    case N_ASSIGN: collect_names(p, pat->a, isvar); break;
    case N_SPREAD: collect_names(p, pat->a, isvar); break;
    case N_ARRAY: for (struct jn *e = pat->a; e; e = e->next) collect_names(p, e, isvar); break;
    case N_OBJECT: for (struct jn *e = pat->a; e; e = e->next) collect_names(p, e->t == N_PROP ? e->b : e, isvar); break;
    default: break;
    }
}
static jstr *ident_name(struct ps *p)
{
    if (p->tok.type != TK_ID) perr(p, "se esperaba un identificador");
    jstr *s = p->tok.str; next(p); return s;
}
static struct jn *parse_template(struct ps *p, jstr *raw)
{
    struct jn *n = mk(p, N_TPL); struct jn *tail = 0, *etail = 0;
    const char *s = raw->d, *e = raw->d + raw->len; struct wb cur = {0};
    struct jn *strs = 0, *exprs = 0; (void)tail;
    struct ps save_ps = *p;
    while (s < e) {
        if (*s == '\\') {
            struct ps tmp = *p; tmp.cur = s + 1; tmp.e = e; read_escape(&tmp, &cur); s = tmp.cur; continue;
        }
        if (*s == '$' && s + 1 < e && s[1] == '{') {
            struct jn *sn = mk(p, N_STR); sn->str = mkstr(p, cur.p ? cur.p : "", cur.len); wb_clear(&cur);
            if (!strs) strs = sn; else { struct jn *q = strs; while (q->next) q = q->next; q->next = sn; }
            s += 2; const char *st = s; int depth = 1;
            while (s < e && depth) {
                if (*s == '{') depth++; else if (*s == '}') { depth--; if (!depth) break; }
                else if (*s == '"' || *s == '\'') { char q = *s++; while (s < e && *s != q) { if (*s == '\\') s++; s++; } }
                else if (*s == '`') { s++; int d2 = 0; while (s < e) { if (*s == '\\') { s += 2; continue; } if (d2 == 0 && *s == '`') break; if (*s == '$' && s[1] == '{') d2++; else if (d2 > 0 && *s == '}') d2--; s++; } }
                s++;
            }
            /* sub-analisis */
            struct lstate ls; save(p, &ls); const char *ocur = p->cur, *oe = p->e, *os = p->s; int oline = p->line;
            p->cur = st; p->e = s; p->s = st; p->tok.type = TK_PUNCT; next(p);   /* SOMA_188: p->s tambien (perr retrocede hasta p->s: el texto es una copia) */
            struct jn *ex = isp(p, "}") || p->tok.type == TK_EOF ? mk(p, N_UNDEF) : parse_expr(p);
            if (p->tok.type != TK_EOF) perr(p, "plantilla invalida");
            p->cur = ocur; p->e = oe; p->s = os; p->line = oline; restore(p, &ls);
            if (!exprs) exprs = ex; else etail->next = ex; etail = ex;
            s++;
        } else wb_addc(&cur, *s++);
    }
    struct jn *sn = mk(p, N_STR); sn->str = mkstr(p, cur.p ? cur.p : "", cur.len); wb_free(&cur);
    if (!strs) strs = sn; else { struct jn *q = strs; while (q->next) q = q->next; q->next = sn; }
    n->a = strs; n->b = exprs; (void)save_ps; return n;
}
static struct jn *parse_args_in(struct ps *p);
static struct jn *parse_args(struct ps *p) { int sn = p->no_in; p->no_in = 0; struct jn *r = parse_args_in(p); p->no_in = sn; return r; }   /* SOMA_188 */
static struct jn *parse_args_in(struct ps *p)
{
    struct jn *head = 0, *tail = 0; expectp(p, "(");
    while (!isp(p, ")")) {
        struct jn *a;
        if (eatp(p, "...")) { a = mk(p, N_SPREAD); a->a = parse_assign(p); } else a = parse_assign(p);
        if (!head) head = a; else tail->next = a; tail = a;
        if (!eatp(p, ",")) break;
    }
    expectp(p, ")"); return head;
}
static struct jn *parse_prop_key(struct ps *p, int *computed)
{
    *computed = 0; struct jn *k = 0;
    if (p->tok.type == TK_ID) { k = mk(p, N_STR); k->str = p->tok.str; next(p); }
    else if (p->tok.type == TK_STR) { k = mk(p, N_STR); k->str = p->tok.str; next(p); }
    else if (p->tok.type == TK_NUM) { k = mk(p, N_STR); k->str = js_num_to_str(p->rt, p->tok.num); next(p); }
    else if (isp(p, "[")) { next(p); k = parse_assign(p); expectp(p, "]"); *computed = 1; }
    else perr(p, "clave de propiedad invalida");
    return k;
}
static struct jn *parse_binding(struct ps *p);
static struct jn *parse_array_lit_in(struct ps *p);
static struct jn *parse_array_lit(struct ps *p) { int sn = p->no_in; p->no_in = 0; struct jn *r = parse_array_lit_in(p); p->no_in = sn; return r; }   /* SOMA_188 */
static struct jn *parse_array_lit_in(struct ps *p)
{
    struct jn *n = mk(p, N_ARRAY), *tail = 0; expectp(p, "[");
    while (!isp(p, "]")) {
        struct jn *e;
        if (isp(p, ",")) { e = mk(p, N_HOLE); }
        else if (eatp(p, "...")) { e = mk(p, N_SPREAD); e->a = parse_assign(p); }
        else e = parse_assign(p);
        if (!n->a) n->a = e; else tail->next = e; tail = e;
        if (!eatp(p, ",")) break;
    }
    expectp(p, "]"); return n;
}
static struct jn *parse_object_lit_in(struct ps *p);
static struct jn *parse_object_lit(struct ps *p) { int sn = p->no_in; p->no_in = 0; struct jn *r = parse_object_lit_in(p); p->no_in = sn; return r; }   /* SOMA_188 */
static struct jn *parse_object_lit_in(struct ps *p)
{
    struct jn *n = mk(p, N_OBJECT), *tail = 0; expectp(p, "{");
    while (!isp(p, "}")) {
        struct jn *pr = mk(p, N_PROP); int computed = 0; int fl = 0; int is_async = 0;
        if (eatp(p, "...")) { pr->t = N_SPREAD; pr->a = parse_assign(p); goto add; }
        if (isid(p, "async")) { struct lstate ls; save(p, &ls); next(p); if (isp(p, ",") || isp(p, ":") || isp(p, "(") || isp(p, "}") || isp(p, "=")) restore(p, &ls); else is_async = 1; }
        if (!is_async && (isid(p, "get") || isid(p, "set"))) {
            struct lstate ls; save(p, &ls); int isget = isid(p, "get"); next(p);
            if (isp(p, ",") || isp(p, ":") || isp(p, "(") || isp(p, "}") || isp(p, "=")) restore(p, &ls); else fl = isget ? NF_GETTER : NF_SETTER;
        }
        int is_gen = 0; if (isp(p, "*")) { next(p); is_gen = 1; }   /* SOMA_183 */
        pr->a = parse_prop_key(p, &computed);
        if (computed) fl |= NF_COMPUTED;
        if (isp(p, "(")) { struct jn *f = parse_function(p, NF_METHOD | (is_async ? NF_ASYNC : 0) | (is_gen ? NF_GEN : 0), 0); if (pr->a->t == N_STR && !computed) f->str = pr->a->str; pr->b = f; fl |= NF_METHOD; }
        else if (fl & (NF_GETTER | NF_SETTER)) perr(p, "accessor invalido");
        else if (eatp(p, ":")) pr->b = parse_assign(p);
        else {
            if (pr->a->t != N_STR || computed) perr(p, "se esperaba ':'");
            struct jn *id = mk(p, N_IDENT); id->str = pr->a->str; pr->b = id; fl |= NF_SHORT;
            if (isp(p, "=")) { next(p); struct jn *as = mk(p, N_ASSIGN); as->op = 0; as->a = id; as->b = parse_assign(p); pr->b = as; }
        }
        pr->fl = (uint16_t)fl;
    add:
        if (!n->a) n->a = pr; else tail->next = pr; tail = pr;
        if (!eatp(p, ",")) break;
    }
    expectp(p, "}"); return n;
}
static struct jn *parse_binding(struct ps *p)
{
    if (isp(p, "[")) return parse_array_lit(p);
    if (isp(p, "{")) return parse_object_lit(p);
    struct jn *id = mk(p, N_IDENT); id->str = ident_name(p); return id;
}
static struct jn *parse_params(struct ps *p, int *count)
{
    struct jn *head = 0, *tail = 0; int n = 0; expectp(p, "(");
    while (!isp(p, ")")) {
        struct jn *prm;
        if (eatp(p, "...")) { prm = mk(p, N_SPREAD); prm->a = parse_binding(p); }
        else {
            prm = parse_binding(p);
            if (isp(p, "=")) { next(p); struct jn *as = mk(p, N_ASSIGN); as->a = prm; as->b = parse_assign(p); prm = as; }
        }
        if (!head) head = prm; else tail->next = prm; tail = prm; n++;
        if (!eatp(p, ",")) break;
    }
    expectp(p, ")"); *count = n; return head;
}
static void begin_fn(struct ps *p, struct jfc *fc, struct jfi *fi, int arrow)
{
    fc->up = p->fc; fc->fi = fi; fc->is_arrow = arrow;
    if (p->fc) p->fc->fi->has_closure = 1;
    p->fc = fc; p->nfunc++;
}
static void declare_params(struct ps *p, struct jn *params)
{
    for (struct jn *q = params; q; q = q->next) { collect_names(p, q->t == N_ASSIGN ? q->a : q, 1); }
}
/* cuerpo { ... } de funcion: sentencias */
static struct jn *parse_fn_body(struct ps *p)
{
    struct jn *head = 0, *tail = 0; expectp(p, "{"); int sn = p->no_in; p->no_in = 0;
    if (p->tok.type == TK_STR && p->tok.str && wu_streq(p->tok.str->d, "use strict")) p->strict = 1;   /* SOMA 188B: directiva */   /* SOMA_188: 'in' vuelve a valer dentro de una funcion en el inicializador de un for */
    while (!isp(p, "}")) { if (p->tok.type == TK_EOF) perr(p, "se esperaba '}'"); struct jn *s = parse_stmt(p); if (!head) head = s; else tail->next = s; tail = s; }
    next(p); p->no_in = sn; return head;
}
static struct jn *parse_function(struct ps *p, int flags, int is_decl)
{
    struct jn *f = mk(p, N_FUNC); f->fl = (uint16_t)flags;
    const char *src0 = p->tok.start;
    /* SOMA 188B: el texto empieza en 'function' (o 'async function'), o en el nombre del metodo: YouTube e inyectores de dependencias leen f.toString() */
    if (!(flags & (NF_METHOD | NF_ARROW))) {
        if (p->prev_start && p->prev_start + 8 <= src0 && !memcmp(p->prev_start, "function", 8)) { src0 = p->prev_start; if ((flags & NF_ASYNC) && p->prev2_start && p->prev2_start + 5 <= src0 && !memcmp(p->prev2_start, "async", 5)) src0 = p->prev2_start; }
    } else if ((flags & NF_METHOD) && p->prev_start && p->prev_start < src0) { unsigned char c0 = (unsigned char)*p->prev_start; if (c0 == '_' || c0 == '$' || c0 == '#' || c0 >= 128 || (c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z')) src0 = p->prev_start; }
    if (!(flags & NF_METHOD) && !(flags & NF_ARROW)) {
        if (eatp(p, "*")) f->fl |= NF_GEN;   /* SOMA_183: generador */
        if (p->tok.type == TK_ID && !isp(p, "(")) { f->str = p->tok.str; next(p); }
    }
    struct jfi *fi = (struct jfi *)pal(p, sizeof *fi); f->p = fi; fi->name = f->str; fi->src = src0;
    struct jfc fc; begin_fn(p, &fc, fi, 0);
    int save_async = p->in_async; p->in_async = (flags & NF_ASYNC) != 0; int save_inf = p->in_fn; p->in_fn = 1; int save_gen = p->in_gen; p->in_gen = (f->fl & NF_GEN) != 0;
    int cnt = 0; f->a = parse_params(p, &cnt); fi->nparams = cnt; declare_params(p, f->a);
    int *sb = p->bcap; int lcap = 0; p->bcap = &lcap; int save_strict = p->strict;
    f->b = parse_fn_body(p); fi->strict = p->strict; p->strict = save_strict;
    fi->srclen = (uint32_t)((p->prev_end > src0 ? p->prev_end : p->cur) - src0); fi->ncap += lcap;
    p->bcap = sb; p->in_async = save_async; p->in_fn = save_inf; p->in_gen = save_gen; p->fc = fc.up;
    (void)is_decl; return f;
}
static struct jn *make_arrow(struct ps *p, struct jn *params, int cnt, int flags, struct jfc *fc, struct jfi *fi, const char *src0)
{
    struct jn *f = mk(p, N_FUNC); f->fl = (uint16_t)(flags | NF_ARROW); f->a = params; f->p = fi; fi->nparams = cnt; fi->src = src0;
    declare_params(p, params);
    int save_async = p->in_async; p->in_async = (flags & NF_ASYNC) != 0; int save_gen = p->in_gen; p->in_gen = 0; int *sb = p->bcap; int lcap = 0; p->bcap = &lcap;
    if (isp(p, "{")) { int ss = p->strict; f->b = parse_fn_body(p); fi->strict = p->strict; p->strict = ss; }
    else { f->fl |= NF_EXPRBODY; int sn = p->no_in; f->b = parse_assign(p); p->no_in = sn; }
    fi->srclen = (uint32_t)((p->prev_end > src0 ? p->prev_end : p->cur) - src0); fi->ncap += lcap;
    p->bcap = sb; p->in_async = save_async; p->in_gen = save_gen; p->fc = fc->up; return f;
}
static struct jn *parse_class(struct ps *p, int is_decl)
{
    struct jn *c = mk(p, is_decl ? N_CLASSDECL : N_CLASS);
    const char *csrc = (p->prev_start && p->prev_start + 5 <= p->tok.start && !memcmp(p->prev_start, "class", 5)) ? p->prev_start : p->tok.start;
    if (p->tok.type == TK_ID && !isid(p, "extends")) { c->str = p->tok.str; next(p); }
    if (p->fc) p->fc->fi->has_closure = 1;
    if (eatid(p, "extends")) { c->a = parse_assign(p); c->fl |= NF_DERIVED; }
    expectp(p, "{"); struct jn *tail = 0; int cls_strict = p->strict; p->strict = 1;   /* SOMA 188B: el cuerpo de una clase es estricto */
    while (!isp(p, "}")) {
        if (eatp(p, ";")) continue;
        struct jn *m = mk(p, N_PROP); int fl = 0, computed = 0, is_async = 0;
        if (isid(p, "static")) { struct lstate ls; save(p, &ls); next(p); if (isp(p, "(") || isp(p, "=") || isp(p, ";") || isp(p, "}")) restore(p, &ls); else fl |= NF_STATIC; }
        if ((fl & NF_STATIC) && isp(p, "{")) {   /* SOMA_188: bloque estatico (ES2022): metodo sin parametros que se ejecuta con this = la clase */
            struct jn *f = mk(p, N_FUNC); f->fl = NF_METHOD;
            struct jfi *fi = (struct jfi *)pal(p, sizeof *fi); f->p = fi; fi->src = p->tok.start;
            struct jfc fc; begin_fn(p, &fc, fi, 0);
            int sa = p->in_async, sf = p->in_fn, sg = p->in_gen; p->in_async = 0; p->in_fn = 1; p->in_gen = 0;
            int *sb = p->bcap; int lcap = 0; p->bcap = &lcap;
            f->b = parse_fn_body(p);
            fi->srclen = (uint32_t)(p->cur - fi->src); fi->ncap += lcap;
            p->bcap = sb; p->in_async = sa; p->in_fn = sf; p->in_gen = sg; p->fc = fc.up;
            m->a = 0; m->b = f; m->fl = (uint16_t)(NF_STATIC | NF_FIELD);
            if (!c->b) c->b = m; else tail->next = m; tail = m;
            continue;
        }
        if (isid(p, "async")) { struct lstate ls; save(p, &ls); next(p); if (isp(p, "(") || isp(p, "=") || isp(p, ";") || isp(p, "}") || p->tok.nl) restore(p, &ls); else is_async = 1; }
        int is_gen = eatp(p, "*") ? 1 : 0;   /* SOMA_183 */
        if (!is_async && (isid(p, "get") || isid(p, "set"))) { struct lstate ls; save(p, &ls); int isg = isid(p, "get"); next(p); if (isp(p, "(") || isp(p, "=") || isp(p, ";") || isp(p, "}")) restore(p, &ls); else fl |= isg ? NF_GETTER : NF_SETTER; }
        m->a = parse_prop_key(p, &computed); if (computed) fl |= NF_COMPUTED;
        if (isp(p, "(")) {
            struct jn *f = parse_function(p, NF_METHOD | (is_async ? NF_ASYNC : 0) | (is_gen ? NF_GEN : 0), 0);
            if (m->a->t == N_STR && !computed) { f->str = m->a->str; if (!(fl & NF_STATIC) && !(fl & (NF_GETTER | NF_SETTER)) && wu_streq(m->a->str->d, "constructor")) { fl |= NF_CTOR; if (c->fl & NF_DERIVED) f->fl |= NF_DERIVED; } }
            m->b = f; fl |= NF_METHOD;
        } else {
            fl |= NF_FIELD; if (eatp(p, "=")) {
                struct jfi *fi = (struct jfi *)pal(p, sizeof *fi); struct jfc fc; begin_fn(p, &fc, fi, 1);
                m->b = parse_assign(p); p->fc = fc.up; m->c = (struct jn *)fi;
            }
            asi(p);
        }
        m->fl = (uint16_t)fl;
        if (!c->b) c->b = m; else tail->next = m; tail = m;
    }
    next(p); if (is_decl) note_lex(p);
    { struct jfi *cfi = (struct jfi *)pal(p, sizeof *cfi); cfi->src = csrc; cfi->srclen = (uint32_t)((p->prev_end > csrc ? p->prev_end : p->cur) - csrc); c->p = cfi; }   /* texto de la clase para toString */
    p->strict = cls_strict; return c;
}
static struct jn *parse_primary(struct ps *p)
{
    struct tok t = p->tok; struct jn *n;
    switch (t.type) {
    case TK_NUM: n = mk(p, N_NUM); n->num = t.num; if (t.big) { n->ival = 0x42; n->str = t.str; } next(p); return n;
    case TK_STR: n = mk(p, N_STR); n->str = t.str; next(p); return n;
    case TK_TPL: next(p); return parse_template(p, t.str);
    case TK_REGEX: n = mk(p, N_REGEX); n->str = t.str; n->p = (void *)t.start; next(p); return n;
    case TK_ID: {
        const char *s = t.str->d;
        if (t.kw) {
            if (wu_streq(s, "this")) { next(p); return mk(p, N_THIS); }
            if (wu_streq(s, "import")) {   /* SOMA_188: import() dinamico en scripts clasicos e import.meta (fuera de modulos: objeto vacio) */
                next(p);
                if (isp(p, "(")) { n = mk(p, N_IDENT); n->str = js_intern_c(p->rt, "__dimpg"); return n; }
                if (isp(p, ".")) { next(p); if (!(p->tok.type == TK_ID && wu_streq(p->tok.str->d, "meta"))) perr(p, "se esperaba import.meta"); next(p); n = mk(p, N_IDENT); n->str = js_intern_c(p->rt, "__metag"); return n; }
                perr(p, "import solo puede usarse en modulos");
            }
            if (wu_streq(s, "null")) { next(p); return mk(p, N_NULL); }
            if (wu_streq(s, "true")) { next(p); return mk(p, N_TRUE); }
            if (wu_streq(s, "false")) { next(p); return mk(p, N_FALSE); }
            if (wu_streq(s, "function")) { next(p); return parse_function(p, 0, 0); }
            if (wu_streq(s, "class")) { next(p); return parse_class(p, 0); }
            if (wu_streq(s, "super")) { next(p); return mk(p, N_SUPER); }
            perr(p, "token inesperado");
        }
        if (wu_streq(s, "async")) {
            struct lstate ls; save(p, &ls); next(p);
            if (isid(p, "function") && !p->tok.nl) { next(p); return parse_function(p, NF_ASYNC, 0); }
            restore(p, &ls);
        }
        n = mk(p, N_IDENT); n->str = t.str; next(p);
        if (wu_streq(s, "arguments")) { for (struct jfc *f = p->fc; f; f = f->up) { f->fi->uses_args = 1; if (!f->is_arrow) break; } }
        return n;
    }
    case TK_PUNCT:
        if (isp(p, "(")) { next(p); int sn = p->no_in; p->no_in = 0; n = parse_expr(p); p->no_in = sn; expectp(p, ")"); n->fl |= NF_PAREN; return n; }
        if (isp(p, "[")) return parse_array_lit(p);
        if (isp(p, "{")) return parse_object_lit(p);
        break;
    default: break;
    }
    perr(p, "token inesperado"); return 0;
}
static struct jn *parse_new(struct ps *p)
{
    next(p);   /* new */
    if (isp(p, ".")) { next(p); if (!isid(p, "target")) perr(p, "new.target"); next(p); return mk(p, N_NEWTARGET); }
    struct jn *n = mk(p, N_NEW), *callee;
    if (isid(p, "new")) callee = parse_new(p); else callee = parse_primary(p);
    for (;;) {
        if (isp(p, ".")) { next(p); struct jn *m = mk(p, N_MEMBER); m->a = callee; struct jn *k = mk(p, N_STR); k->str = p->tok.str; if (p->tok.type != TK_ID) perr(p, "propiedad invalida"); next(p); m->b = k; m->fl = NF_SHORT; callee = m; }
        else if (isp(p, "[")) { next(p); struct jn *m = mk(p, N_MEMBER); m->a = callee; m->b = parse_expr(p); expectp(p, "]"); callee = m; }
        else break;
    }
    n->a = callee; if (isp(p, "(")) n->b = parse_args(p);
    return n;
}
static struct jn *parse_postfix_chain(struct ps *p)
{
    struct jn *e = isid(p, "new") ? parse_new(p) : parse_primary(p);
    for (;;) {
        if (isp(p, ".")) { next(p); struct jn *m = mk(p, N_MEMBER); m->a = e; if (p->tok.type != TK_ID) perr(p, "propiedad invalida"); struct jn *k = mk(p, N_STR); k->str = p->tok.str; next(p); m->b = k; m->fl = NF_SHORT; e = m; }
        else if (isp(p, "?.")) {
            next(p); struct jn *m;
            if (isp(p, "(")) { m = mk(p, N_CALL); m->a = e; m->b = parse_args(p); m->fl = NF_OPT; }
            else if (isp(p, "[")) { next(p); m = mk(p, N_MEMBER); m->a = e; m->b = parse_expr(p); expectp(p, "]"); m->fl = NF_OPT; }
            else { m = mk(p, N_MEMBER); m->a = e; if (p->tok.type != TK_ID) perr(p, "propiedad invalida"); struct jn *k = mk(p, N_STR); k->str = p->tok.str; next(p); m->b = k; m->fl = NF_SHORT | NF_OPT; }
            e = m;
        }
        else if (isp(p, "[")) { next(p); struct jn *m = mk(p, N_MEMBER); m->a = e; int sn = p->no_in; p->no_in = 0; m->b = parse_expr(p); p->no_in = sn; expectp(p, "]"); e = m; }
        else if (isp(p, "(")) { struct jn *c = mk(p, N_CALL); c->a = e;
            if (e->t == N_IDENT && e->str && wu_streq(e->str->d, "eval")) for (struct jfc *f = p->fc; f; f = f->up) { f->fi->has_closure = 1; f->fi->uses_args = 1; }   /* SOMA_188: eval directo: ambitos en el monton */
            c->b = parse_args(p); e = c; }
        else if (p->tok.type == TK_TPL) { struct jn *c = mk(p, N_TAGGED); c->a = e; c->b = parse_template(p, p->tok.str); next(p); e = c; }
        else break;
    }
    return e;
}
static struct jn *parse_unary(struct ps *p)
{
    if ((char *)__builtin_frame_address(0) < p->rt->stack_lo) perr(p, "expresion demasiado anidada");
    if (p->tok.type == TK_PUNCT) {
        const char *o = p->tok.op;
        if (wu_streq(o, "!") || wu_streq(o, "~") || wu_streq(o, "+") || wu_streq(o, "-")) { struct jn *n = mk(p, N_UNARY); n->op = (uint8_t)o[0]; next(p); n->a = parse_unary(p); return n; }
        if (wu_streq(o, "++") || wu_streq(o, "--")) { struct jn *n = mk(p, N_UPDATE); n->op = (uint8_t)o[0]; n->fl = 1; next(p); n->a = parse_unary(p); return n; }
    } else if (p->tok.type == TK_ID && p->tok.kw) {
        const char *s = p->tok.str->d;
        if (wu_streq(s, "typeof")) { struct jn *n = mk(p, N_UNARY); n->op = 't'; next(p); n->a = parse_unary(p); return n; }
        if (wu_streq(s, "void")) { struct jn *n = mk(p, N_UNARY); n->op = 'v'; next(p); n->a = parse_unary(p); return n; }
        if (wu_streq(s, "delete")) { struct jn *n = mk(p, N_UNARY); n->op = 'd'; next(p); n->a = parse_unary(p); return n; }
    } else if (p->tok.type == TK_ID && isid(p, "await") && (p->in_async || !p->in_fn)) {
        struct lstate ls; save(p, &ls); next(p);
        if (!(isp(p, ")") || isp(p, ",") || isp(p, ";") || isp(p, "=") || isp(p, ".") || p->tok.type == TK_EOF) || p->in_async) { struct jn *n = mk(p, N_AWAIT); if (p->fc) p->fc->fi->has_await = 1; n->a = parse_unary(p); return n; }
        restore(p, &ls);
    }
    struct jn *e = parse_postfix_chain(p);
    if (p->tok.type == TK_PUNCT && !p->tok.nl && (isp(p, "++") || isp(p, "--"))) { struct jn *n = mk(p, N_UPDATE); n->op = (uint8_t)p->tok.op[0]; n->a = e; next(p); return n; }
    return e;
}
struct binop { const char *op; int prec; int code; };
static const struct binop BINOPS[] = { { "??", 1, B_NULLISH }, { "||", 2, B_LOR }, { "&&", 3, B_LAND }, { "|", 4, B_OR }, { "^", 5, B_XOR }, { "&", 6, B_AND },
    { "==", 7, B_EQ }, { "!=", 7, B_NE }, { "===", 7, B_SEQ }, { "!==", 7, B_SNE }, { "<", 8, B_LT }, { ">", 8, B_GT }, { "<=", 8, B_LE }, { ">=", 8, B_GE },
    { "<<", 9, B_SHL }, { ">>", 9, B_SHR }, { ">>>", 9, B_USHR }, { "+", 10, B_ADD }, { "-", 10, B_SUB }, { "*", 11, B_MUL }, { "/", 11, B_DIV }, { "%", 11, B_MOD }, { "**", 12, B_EXP }, { 0, 0, 0 } };
static int binop_of(struct ps *p, int *prec)
{
    if (p->tok.type == TK_PUNCT) { for (int i = 0; BINOPS[i].op; i++) if (wu_streq(p->tok.op, BINOPS[i].op)) { *prec = BINOPS[i].prec; return BINOPS[i].code; } }
    else if (p->tok.type == TK_ID) {
        if (wu_streq(p->tok.str->d, "instanceof")) { *prec = 8; return B_INSTANCEOF; }
        if (wu_streq(p->tok.str->d, "in") && !p->no_in) { *prec = 8; return B_IN; }
    }
    return 0;
}
static struct jn *parse_binary(struct ps *p, int minprec)
{
    struct jn *l = parse_unary(p);
    for (;;) {
        int prec, code = binop_of(p, &prec);
        if (!code || prec < minprec) break;
        next(p);
        struct jn *r = code == B_EXP ? parse_binary(p, prec) : parse_binary(p, prec + 1);
        struct jn *n = mk(p, (code == B_LAND || code == B_LOR || code == B_NULLISH) ? N_LOGICAL : N_BINARY); n->op = (uint8_t)code; n->a = l; n->b = r; l = n;
    }
    return l;
}
static struct jn *parse_cond(struct ps *p)
{
    struct jn *c = parse_binary(p, 1);
    if (isp(p, "?")) {
        next(p); struct jn *n = mk(p, N_COND); n->a = c; int sn = p->no_in; p->no_in = 0; n->b = parse_assign(p); p->no_in = sn; expectp(p, ":"); n->c = parse_assign(p); return n;
    }
    return c;
}
static int assign_code(struct ps *p)
{
    if (p->tok.type != TK_PUNCT) return -1;
    static const char *AO[] = { "=", "+=", "-=", "*=", "/=", "%=", "**=", "<<=", ">>=", ">>>=", "&=", "|=", "^=", "&&=", "||=", "?\?=", 0 };
    static const int AC[] = { 0, B_ADD, B_SUB, B_MUL, B_DIV, B_MOD, B_EXP, B_SHL, B_SHR, B_USHR, B_AND, B_OR, B_XOR, B_LAND, B_LOR, B_NULLISH };
    for (int i = 0; AO[i]; i++) if (wu_streq(p->tok.op, AO[i])) return AC[i];
    return -1;
}
/* intenta analizar "(params) =>" */
static struct jn *try_arrow(struct ps *p, int flags, const char *src0)
{
    struct lstate ls; save(p, &ls); struct pjmp jb; jb.up = p->jmp; p->jmp = &jb;
    struct jfi *fi = (struct jfi *)pal(p, sizeof *fi); struct jfc fc; int cnt = 0; struct jn *params = 0;
    char saveerr[160]; memcpy(saveerr, p->err, sizeof saveerr); int saveel = p->err_line;
    struct jfc *old_fc = p->fc; int old_nf = p->nfunc; int old_in_async = p->in_async; int old_in_gen = p->in_gen; int old_spec = p->spec;
    if (__builtin_setjmp(jb.jb)) {
        /* fallo: se restaura todo */
        p->jmp = jb.up; restore(p, &ls); p->fc = old_fc; p->nfunc = old_nf; p->in_async = old_in_async; p->in_gen = old_in_gen; memcpy(p->err, saveerr, sizeof saveerr); p->err_line = saveel; p->spec = old_spec; return 0;
    }
    p->spec = old_spec + 1;
    begin_fn(p, &fc, fi, 1);
    params = parse_params(p, &cnt);
    if (!isp(p, "=>") || p->tok.nl) perr(p, "no es flecha");
    p->spec = old_spec;
    p->jmp = jb.up;
    next(p);
    struct jn *f = make_arrow(p, params, cnt, flags, &fc, fi, src0);
    if (old_fc) old_fc->fi->has_closure = 1;
    return f;
}
static struct jn *parse_assign(struct ps *p)
{
    const char *src0 = p->tok.start;
    if (p->in_gen && p->tok.type == TK_ID && isid(p, "yield")) {   /* SOMA_183: yield / yield* */
        struct jn *y = mk(p, N_YIELD); next(p);
        if (!p->tok.nl && isp(p, "*")) { next(p); y->op = 1; }
        if (!(p->tok.nl && !y->op) && !(isp(p, ")") || isp(p, "]") || isp(p, "}") || isp(p, ",") || isp(p, ";") || isp(p, ":") || p->tok.type == TK_EOF)) y->a = parse_assign(p);
        return y;
    }
    if (p->tok.type == TK_ID) {
        if (isid(p, "async")) {
            struct lstate ls; save(p, &ls); next(p);
            if (!p->tok.nl) {
                if (p->tok.type == TK_ID && !p->tok.kw && !isid(p, "function")) {
                    jstr *nm = p->tok.str; next(p);
                    if (isp(p, "=>")) {
                        next(p); struct jn *id = mk(p, N_IDENT); id->str = nm; struct jfi *fi = (struct jfi *)pal(p, sizeof *fi); struct jfc fc; begin_fn(p, &fc, fi, 1);
                        return make_arrow(p, id, 1, NF_ASYNC, &fc, fi, src0);
                    }
                } else if (isp(p, "(")) { struct jn *f = try_arrow(p, NF_ASYNC, src0); if (f) return f; }
            }
            restore(p, &ls);
        } else if (!p->tok.kw) {
            struct lstate ls; save(p, &ls); jstr *nm = p->tok.str; next(p);
            if (isp(p, "=>") && !p->tok.nl) {
                next(p); struct jn *id = mk(p, N_IDENT); id->str = nm; struct jfi *fi = (struct jfi *)pal(p, sizeof *fi); struct jfc fc; begin_fn(p, &fc, fi, 1);
                return make_arrow(p, id, 1, 0, &fc, fi, src0);
            }
            restore(p, &ls);
        }
    } else if (isp(p, "(")) { struct jn *f = try_arrow(p, 0, src0); if (f) return f; }
    struct jn *l = parse_cond(p);
    int code = assign_code(p);
    if (code >= 0) {
        if (!(l->t == N_IDENT || l->t == N_MEMBER || l->t == N_ARRAY || l->t == N_OBJECT)) perr(p, "destino de asignacion invalido");
        struct jn *n = mk(p, N_ASSIGN); n->op = (uint8_t)code; n->a = l; next(p); n->b = parse_assign(p); return n;
    }
    return l;
}
static struct jn *parse_expr(struct ps *p)
{
    struct jn *e = parse_assign(p);
    if (isp(p, ",")) {
        struct jn *s = mk(p, N_SEQ); s->a = e; struct jn *tail = e;
        while (eatp(p, ",")) { struct jn *x = parse_assign(p); tail->next = x; tail = x; }
        return s;
    }
    return e;
}

/* ------------------------------ sentencias ------------------------------ */
static struct jn *parse_block_body(struct ps *p, int *cap)
{
    int *sb = p->bcap; int c = 0; p->bcap = &c; struct jn *head = 0, *tail = 0;
    while (!isp(p, "}")) { if (p->tok.type == TK_EOF) perr(p, "se esperaba '}'"); struct jn *s = parse_stmt(p); if (!head) head = s; else tail->next = s; tail = s; }
    next(p); p->bcap = sb; *cap = c; return head;
}
static struct jn *parse_block(struct ps *p)
{
    struct jn *b = mk(p, N_BLOCK); expectp(p, "{"); int cap = 0; b->a = parse_block_body(p, &cap); b->ival = cap; return b;
}
static struct jn *parse_var(struct ps *p, int kind, int need_init)
{
    struct jn *v = mk(p, N_VAR); v->op = (uint8_t)kind; struct jn *tail = 0;
    do {
        struct jn *d = mk(p, N_DECL); d->a = parse_binding(p);
        collect_names(p, d->a, kind == 0);
        if (eatp(p, "=")) d->b = parse_assign(p);
        else if (need_init && !(isid(p, "of") || isid(p, "in")) && kind == 2 && d->a->t == N_IDENT) perr(p, "const requiere inicializador");
        if (!v->a) v->a = d; else tail->next = d; tail = d;
    } while (eatp(p, ","));
    return v;
}
static struct jn *parse_stmt(struct ps *p)
{
    if ((char *)__builtin_frame_address(0) < p->rt->stack_lo) perr(p, "programa demasiado anidado");
    struct tok t = p->tok; struct jn *n;
    if (t.type == TK_PUNCT) {
        if (isp(p, "{")) return parse_block(p);
        if (eatp(p, ";")) return mk(p, N_EMPTY);
    }
    if (t.type == TK_ID) {
        const char *s = t.str->d;
        if (t.kw) {
            if (wu_streq(s, "var") || wu_streq(s, "const") || (wu_streq(s, "let"))) {
                next(p); n = parse_var(p, s[0] == 'v' ? 0 : s[0] == 'l' ? 1 : 2, 1); asi(p); return n;
            }
            if (wu_streq(s, "function")) { next(p); n = parse_function(p, 0, 1); n->t = N_FUNCDECL; note_lex(p); if (n->str) add_var(p, n->str), p->fc->fi->ncap += 0; return n; }
            if (wu_streq(s, "class")) { next(p); return parse_class(p, 1); }
            if (wu_streq(s, "return")) {
                next(p); n = mk(p, N_RETURN); if (!isp(p, ";") && !isp(p, "}") && p->tok.type != TK_EOF && !p->tok.nl) n->a = parse_expr(p); asi(p); return n;
            }
            if (wu_streq(s, "if")) {
                next(p); n = mk(p, N_IF); expectp(p, "("); n->a = parse_expr(p); expectp(p, ")"); n->b = parse_stmt(p);
                if (eatid(p, "else")) n->c = parse_stmt(p);
                return n;
            }
            if (wu_streq(s, "with")) { next(p); n = mk(p, N_WITH); expectp(p, "("); n->a = parse_expr(p); expectp(p, ")"); n->b = parse_stmt(p); return n; }   /* SOMA_188 */
            if (wu_streq(s, "while")) { next(p); n = mk(p, N_WHILE); expectp(p, "("); n->a = parse_expr(p); expectp(p, ")"); n->b = parse_stmt(p); return n; }
            if (wu_streq(s, "do")) { next(p); n = mk(p, N_DO); n->b = parse_stmt(p); if (!eatid(p, "while")) perr(p, "se esperaba 'while'"); expectp(p, "("); n->a = parse_expr(p); expectp(p, ")"); eatp(p, ";"); return n; }
            if (wu_streq(s, "for")) {
                next(p); int is_await = 0; if (isid(p, "await")) { next(p); is_await = 1; if (p->fc) p->fc->fi->has_await = 1; }
                expectp(p, "("); n = mk(p, N_FOR); int cap = 0; int *sb = p->bcap; p->bcap = &cap;
                struct jn *init = 0;
                if (!isp(p, ";")) {
                    p->no_in = 1;
                    if (p->tok.type == TK_ID && (isid(p, "var") || isid(p, "let") || isid(p, "const"))) { int k = isid(p, "var") ? 0 : isid(p, "let") ? 1 : 2; next(p); init = parse_var(p, k, 0); }
                    else init = parse_expr(p);
                    p->no_in = 0;
                }
                if (isid(p, "of") || isid(p, "in")) {
                    int isof = isid(p, "of"); next(p); n->t = isof ? N_FOROF : N_FORIN; n->a = init; if (isof && is_await) n->op = 1;
                    n->b = isof ? parse_assign(p) : parse_expr(p); expectp(p, ")"); n->c = parse_stmt(p);
                } else {
                    n->a = init; expectp(p, ";"); if (!isp(p, ";")) n->b = parse_expr(p); expectp(p, ";"); if (!isp(p, ")")) n->c = parse_expr(p); expectp(p, ")"); n->d = parse_stmt(p);
                }
                p->bcap = sb; n->ival = cap; return n;
            }
            if (wu_streq(s, "break") || wu_streq(s, "continue")) {
                next(p); n = mk(p, s[0] == 'b' ? N_BREAK : N_CONTINUE); if (p->tok.type == TK_ID && !p->tok.kw && !p->tok.nl) { n->str = p->tok.str; next(p); } asi(p); return n;
            }
            if (wu_streq(s, "throw")) { next(p); n = mk(p, N_THROW); if (p->tok.nl) perr(p, "salto de linea tras throw"); n->a = parse_expr(p); asi(p); return n; }
            if (wu_streq(s, "try")) {
                next(p); n = mk(p, N_TRY); n->a = parse_block(p);
                if (eatid(p, "catch")) {
                    int cap = 0; int *sb = p->bcap; p->bcap = &cap;
                    if (eatp(p, "(")) { n->d = parse_binding(p); collect_names(p, n->d, 0); expectp(p, ")"); }
                    p->bcap = sb; n->ival = cap; n->b = parse_block(p);
                }
                if (eatid(p, "finally")) n->c = parse_block(p);
                if (!n->b && !n->c) perr(p, "try sin catch ni finally");
                return n;
            }
            if (wu_streq(s, "switch")) {
                next(p); n = mk(p, N_SWITCH); expectp(p, "("); n->a = parse_expr(p); expectp(p, ")"); expectp(p, "{"); struct jn *tail = 0; int cap = 0; int *sb = p->bcap; p->bcap = &cap;
                while (!isp(p, "}")) {
                    struct jn *c = mk(p, N_CASE);
                    if (eatid(p, "default")) c->fl = 1; else { if (!eatid(p, "case")) perr(p, "se esperaba 'case'"); c->a = parse_expr(p); }
                    expectp(p, ":"); struct jn *st = 0, *stt = 0;
                    while (!isp(p, "}") && !isid(p, "case") && !isid(p, "default")) { struct jn *x = parse_stmt(p); if (!st) st = x; else stt->next = x; stt = x; }
                    c->b = st; if (!n->b) n->b = c; else tail->next = c; tail = c;
                }
                next(p); p->bcap = sb; n->ival = cap; return n;
            }
            if (wu_streq(s, "debugger")) { next(p); asi(p); return mk(p, N_EMPTY); }
            if (wu_streq(s, "import") || wu_streq(s, "export")) {
                struct lstate ls; save(p, &ls); next(p);
                if (wu_streq(s, "import") && (isp(p, "(") || isp(p, "."))) { restore(p, &ls); goto expr; }
                perr(p, "los modulos ES (import/export) no estan soportados");
            }
        } else {
            if (wu_streq(s, "async")) {
                struct lstate ls; save(p, &ls); next(p);
                if (isid(p, "function") && !p->tok.nl) { next(p); n = parse_function(p, NF_ASYNC, 1); n->t = N_FUNCDECL; note_lex(p); if (n->str) add_var(p, n->str); return n; }
                restore(p, &ls);
            }
            /* etiqueta */
            struct lstate ls; save(p, &ls); jstr *nm = t.str; next(p);
            if (isp(p, ":")) { next(p); n = mk(p, N_LABEL); n->str = nm; n->a = parse_stmt(p); return n; }
            restore(p, &ls);
        }
    }
expr:
    n = mk(p, N_EXPR); n->a = parse_expr(p); asi(p); return n;
}

struct jn *js_parse(struct jrt *rt, const char *src, uint32_t len, int *err_line, char *errmsg, uint32_t errmax)
{
    struct ps *p = (struct ps *)js_alloc(rt, sizeof *p); if (!p) return 0;
    p->rt = rt; p->s = p->cur = src; p->e = src + len; p->line = 1; p->prev_regex_ok = 1; p->fsrc = src;
    struct jn *prog = mk(p, N_PROGRAM); struct jfi *fi = (struct jfi *)pal(p, sizeof *fi); prog->p = fi;
    struct jfc fc; fc.up = 0; fc.fi = fi; fc.is_arrow = 0; p->fc = &fc;
    struct pjmp jb; jb.up = 0; p->jmp = &jb;
    if (__builtin_setjmp(jb.jb)) { if (err_line) *err_line = p->err_line; if (errmsg) wu_copy(errmsg, p->err, errmax); return 0; }
    p->tok.type = TK_PUNCT; next(p);
    if (p->tok.type == TK_STR && p->tok.str && wu_streq(p->tok.str->d, "use strict")) p->strict = 1;
    int cap = 0; p->bcap = &cap; struct jn *head = 0, *tail = 0;
    while (p->tok.type != TK_EOF) { struct jn *s = parse_stmt(p); if (!head) head = s; else tail->next = s; tail = s; }
    p->jmp = 0; prog->b = head; prog->ival = cap; fi->ncap += cap; fi->src = src; fi->srclen = len;
    return prog;
}
