/* SOMA_358_HOJA: formulas. Incluido desde hoja_core.c.
 *   Tokenizador comun a las dos "lenguas": canonica (Excel: SUM(A1,B1), 1.5, TRUE, #DIV/0!) e interfaz (SUMA(A1;B1), 1,5, VERDADERO,
 *   #¡DIV/0!). Traducir, desplazar referencias o cambiar hojas = reescribir los tokens (el resto del texto se copia tal cual).
 *   Compilador de descenso recursivo (profundidad acotada) a codigo de pila; SI/SI.ERROR/SI.ND con saltos. Evaluador sin recursion. */

/* ================================================================ funciones */
enum {
    FN_SUM = 1, FN_AVERAGE, FN_MIN, FN_MAX, FN_COUNT, FN_COUNTA, FN_COUNTBLANK, FN_COUNTIF, FN_COUNTIFS, FN_SUMIF, FN_SUMIFS, FN_AVERAGEIF,
    FN_AVERAGEIFS, FN_MAXIFS, FN_MINIFS, FN_ROUND, FN_ROUNDUP, FN_ROUNDDOWN, FN_INT, FN_TRUNC, FN_ABS, FN_SQRT, FN_POWER, FN_MOD, FN_PI,
    FN_RAND, FN_RANDBETWEEN, FN_PRODUCT, FN_SUMPRODUCT, FN_MEDIAN, FN_MODE, FN_STDEV, FN_STDEVP, FN_VAR, FN_VARP, FN_LARGE, FN_SMALL,
    FN_RANK, FN_EXP, FN_LN, FN_LOG, FN_LOG10, FN_SIGN, FN_CEILING, FN_FLOOR, FN_SIN, FN_COS, FN_TAN, FN_DEGREES, FN_RADIANS, FN_SUBTOTAL,
    FN_IF, FN_AND, FN_OR, FN_NOT, FN_IFERROR, FN_IFNA, FN_IFS, FN_TRUE, FN_FALSE, FN_ISBLANK, FN_ISNUMBER, FN_ISTEXT, FN_ISERROR, FN_ISNA,
    FN_ISLOGICAL, FN_NA, FN_CHOOSE, FN_VLOOKUP, FN_HLOOKUP, FN_XLOOKUP, FN_INDEX, FN_MATCH, FN_ROW, FN_COLUMN, FN_ROWS, FN_COLUMNS,
    FN_CONCATENATE, FN_CONCAT, FN_TEXTJOIN, FN_LEFT, FN_RIGHT, FN_MID, FN_LEN, FN_UPPER, FN_LOWER, FN_PROPER, FN_TRIM, FN_SUBSTITUTE,
    FN_REPLACE, FN_FIND, FN_SEARCH, FN_TEXT, FN_VALUE, FN_REPT, FN_EXACT, FN_CHAR, FN_CODE, FN_TODAY, FN_NOW, FN_DATE, FN_YEAR, FN_MONTH,
    FN_DAY, FN_HOUR, FN_MINUTE, FN_SECOND, FN_WEEKDAY, FN_DAYS, FN_EDATE, FN_EOMONTH, FN_NETWORKDAYS, FN_DATEDIF, FN_WEEKNUM, FN_TIME,
    FN_PMT, FN_FV, FN_PV, FN_NPV, FN_IRR, FN_N_
};
struct hj_fn { const char *es, *en; uint8_t id, min, max, xlfn; };
static const struct hj_fn FNS[] = {
    { "SUMA", "SUM", FN_SUM, 1, 255, 0 }, { "PROMEDIO", "AVERAGE", FN_AVERAGE, 1, 255, 0 }, { "MIN", "MIN", FN_MIN, 1, 255, 0 }, { "MAX", "MAX", FN_MAX, 1, 255, 0 },
    { "CONTAR", "COUNT", FN_COUNT, 1, 255, 0 }, { "CONTARA", "COUNTA", FN_COUNTA, 1, 255, 0 }, { "CONTAR.BLANCO", "COUNTBLANK", FN_COUNTBLANK, 1, 1, 0 },
    { "CONTAR.SI", "COUNTIF", FN_COUNTIF, 2, 2, 0 }, { "CONTAR.SI.CONJUNTO", "COUNTIFS", FN_COUNTIFS, 2, 254, 0 }, { "SUMAR.SI", "SUMIF", FN_SUMIF, 2, 3, 0 },
    { "SUMAR.SI.CONJUNTO", "SUMIFS", FN_SUMIFS, 3, 255, 0 }, { "PROMEDIO.SI", "AVERAGEIF", FN_AVERAGEIF, 2, 3, 0 }, { "PROMEDIO.SI.CONJUNTO", "AVERAGEIFS", FN_AVERAGEIFS, 3, 255, 0 },
    { "MAX.SI.CONJUNTO", "MAXIFS", FN_MAXIFS, 3, 255, 1 }, { "MIN.SI.CONJUNTO", "MINIFS", FN_MINIFS, 3, 255, 1 },
    { "REDONDEAR", "ROUND", FN_ROUND, 1, 2, 0 }, { "REDONDEAR.MAS", "ROUNDUP", FN_ROUNDUP, 1, 2, 0 }, { "REDONDEAR.MENOS", "ROUNDDOWN", FN_ROUNDDOWN, 1, 2, 0 },
    { "ENTERO", "INT", FN_INT, 1, 1, 0 }, { "TRUNCAR", "TRUNC", FN_TRUNC, 1, 2, 0 }, { "ABS", "ABS", FN_ABS, 1, 1, 0 }, { "RAIZ", "SQRT", FN_SQRT, 1, 1, 0 },
    { "POTENCIA", "POWER", FN_POWER, 2, 2, 0 }, { "RESIDUO", "MOD", FN_MOD, 2, 2, 0 }, { "PI", "PI", FN_PI, 0, 0, 0 }, { "ALEATORIO", "RAND", FN_RAND, 0, 0, 0 },
    { "ALEATORIO.ENTRE", "RANDBETWEEN", FN_RANDBETWEEN, 2, 2, 0 }, { "PRODUCTO", "PRODUCT", FN_PRODUCT, 1, 255, 0 }, { "SUMAPRODUCTO", "SUMPRODUCT", FN_SUMPRODUCT, 1, 255, 0 },
    { "MEDIANA", "MEDIAN", FN_MEDIAN, 1, 255, 0 }, { "MODA", "MODE", FN_MODE, 1, 255, 0 }, { "DESVEST", "STDEV", FN_STDEV, 1, 255, 0 }, { "DESVEST.M", "STDEV.S", FN_STDEV, 1, 255, 1 },
    { "DESVESTP", "STDEVP", FN_STDEVP, 1, 255, 0 }, { "DESVEST.P", "STDEV.P", FN_STDEVP, 1, 255, 1 }, { "VAR", "VAR", FN_VAR, 1, 255, 0 }, { "VAR.S", "VAR.S", FN_VAR, 1, 255, 1 },
    { "VARP", "VARP", FN_VARP, 1, 255, 0 }, { "VAR.P", "VAR.P", FN_VARP, 1, 255, 1 }, { "K.ESIMO.MAYOR", "LARGE", FN_LARGE, 2, 2, 0 }, { "K.ESIMO.MENOR", "SMALL", FN_SMALL, 2, 2, 0 },
    { "JERARQUIA", "RANK", FN_RANK, 2, 3, 0 }, { "JERARQUIA.EQV", "RANK.EQ", FN_RANK, 2, 3, 1 }, { "EXP", "EXP", FN_EXP, 1, 1, 0 }, { "LN", "LN", FN_LN, 1, 1, 0 },
    { "LOG", "LOG", FN_LOG, 1, 2, 0 }, { "LOG10", "LOG10", FN_LOG10, 1, 1, 0 }, { "SIGNO", "SIGN", FN_SIGN, 1, 1, 0 }, { "MULTIPLO.SUPERIOR", "CEILING", FN_CEILING, 1, 2, 0 },
    { "MULTIPLO.INFERIOR", "FLOOR", FN_FLOOR, 1, 2, 0 }, { "SENO", "SIN", FN_SIN, 1, 1, 0 }, { "COS", "COS", FN_COS, 1, 1, 0 }, { "TAN", "TAN", FN_TAN, 1, 1, 0 },
    { "GRADOS", "DEGREES", FN_DEGREES, 1, 1, 0 }, { "RADIANES", "RADIANS", FN_RADIANS, 1, 1, 0 }, { "SUBTOTALES", "SUBTOTAL", FN_SUBTOTAL, 2, 255, 0 },
    { "SI", "IF", FN_IF, 2, 3, 0 }, { "Y", "AND", FN_AND, 1, 255, 0 }, { "O", "OR", FN_OR, 1, 255, 0 }, { "NO", "NOT", FN_NOT, 1, 1, 0 },
    { "SI.ERROR", "IFERROR", FN_IFERROR, 2, 2, 0 }, { "SI.ND", "IFNA", FN_IFNA, 2, 2, 1 }, { "SI.CONJUNTO", "IFS", FN_IFS, 2, 254, 1 },
    { "VERDADERO", "TRUE", FN_TRUE, 0, 0, 0 }, { "FALSO", "FALSE", FN_FALSE, 0, 0, 0 }, { "ESBLANCO", "ISBLANK", FN_ISBLANK, 1, 1, 0 },
    { "ESNUMERO", "ISNUMBER", FN_ISNUMBER, 1, 1, 0 }, { "ESTEXTO", "ISTEXT", FN_ISTEXT, 1, 1, 0 }, { "ESERROR", "ISERROR", FN_ISERROR, 1, 1, 0 },
    { "ESNOD", "ISNA", FN_ISNA, 1, 1, 0 }, { "ESLOGICO", "ISLOGICAL", FN_ISLOGICAL, 1, 1, 0 }, { "NOD", "NA", FN_NA, 0, 0, 0 }, { "ELEGIR", "CHOOSE", FN_CHOOSE, 2, 255, 0 },
    { "BUSCARV", "VLOOKUP", FN_VLOOKUP, 3, 4, 0 }, { "BUSCARH", "HLOOKUP", FN_HLOOKUP, 3, 4, 0 }, { "BUSCARX", "XLOOKUP", FN_XLOOKUP, 3, 6, 1 },
    { "INDICE", "INDEX", FN_INDEX, 2, 3, 0 }, { "COINCIDIR", "MATCH", FN_MATCH, 2, 3, 0 }, { "FILA", "ROW", FN_ROW, 0, 1, 0 }, { "COLUMNA", "COLUMN", FN_COLUMN, 0, 1, 0 },
    { "FILAS", "ROWS", FN_ROWS, 1, 1, 0 }, { "COLUMNAS", "COLUMNS", FN_COLUMNS, 1, 1, 0 },
    { "CONCATENAR", "CONCATENATE", FN_CONCATENATE, 1, 255, 0 }, { "CONCAT", "CONCAT", FN_CONCAT, 1, 255, 1 }, { "UNIRCADENAS", "TEXTJOIN", FN_TEXTJOIN, 3, 255, 1 },
    { "IZQUIERDA", "LEFT", FN_LEFT, 1, 2, 0 }, { "DERECHA", "RIGHT", FN_RIGHT, 1, 2, 0 }, { "EXTRAE", "MID", FN_MID, 3, 3, 0 }, { "LARGO", "LEN", FN_LEN, 1, 1, 0 },
    { "MAYUSC", "UPPER", FN_UPPER, 1, 1, 0 }, { "MINUSC", "LOWER", FN_LOWER, 1, 1, 0 }, { "NOMPROPIO", "PROPER", FN_PROPER, 1, 1, 0 }, { "ESPACIOS", "TRIM", FN_TRIM, 1, 1, 0 },
    { "SUSTITUIR", "SUBSTITUTE", FN_SUBSTITUTE, 3, 4, 0 }, { "REEMPLAZAR", "REPLACE", FN_REPLACE, 4, 4, 0 }, { "ENCONTRAR", "FIND", FN_FIND, 2, 3, 0 },
    { "HALLAR", "SEARCH", FN_SEARCH, 2, 3, 0 }, { "TEXTO", "TEXT", FN_TEXT, 2, 2, 0 }, { "VALOR", "VALUE", FN_VALUE, 1, 1, 0 }, { "REPETIR", "REPT", FN_REPT, 2, 2, 0 },
    { "IGUAL", "EXACT", FN_EXACT, 2, 2, 0 }, { "CARACTER", "CHAR", FN_CHAR, 1, 1, 0 }, { "CODIGO", "CODE", FN_CODE, 1, 1, 0 },
    { "HOY", "TODAY", FN_TODAY, 0, 0, 0 }, { "AHORA", "NOW", FN_NOW, 0, 0, 0 }, { "FECHA", "DATE", FN_DATE, 3, 3, 0 }, { "A\303\221O", "YEAR", FN_YEAR, 1, 1, 0 },
    { "MES", "MONTH", FN_MONTH, 1, 1, 0 }, { "DIA", "DAY", FN_DAY, 1, 1, 0 }, { "HORA", "HOUR", FN_HOUR, 1, 1, 0 }, { "MINUTO", "MINUTE", FN_MINUTE, 1, 1, 0 },
    { "SEGUNDO", "SECOND", FN_SECOND, 1, 1, 0 }, { "DIASEM", "WEEKDAY", FN_WEEKDAY, 1, 2, 0 }, { "DIAS", "DAYS", FN_DAYS, 2, 2, 1 }, { "FECHA.MES", "EDATE", FN_EDATE, 2, 2, 0 },
    { "FIN.MES", "EOMONTH", FN_EOMONTH, 2, 2, 0 }, { "DIAS.LAB", "NETWORKDAYS", FN_NETWORKDAYS, 2, 3, 0 }, { "SIFECHA", "DATEDIF", FN_DATEDIF, 3, 3, 0 },
    { "NUM.DE.SEMANA", "WEEKNUM", FN_WEEKNUM, 1, 2, 0 }, { "NSHORA", "TIME", FN_TIME, 3, 3, 0 },
    { "PAGO", "PMT", FN_PMT, 3, 5, 0 }, { "VF", "FV", FN_FV, 3, 5, 0 }, { "VA", "PV", FN_PV, 3, 5, 0 }, { "VNA", "NPV", FN_NPV, 2, 255, 0 }, { "TIR", "IRR", FN_IRR, 1, 2, 0 },
    { 0, 0, 0, 0, 0, 0 } };
static int fn_busca(const char *s, uint32_t n)                    /* indice en FNS por nombre (castellano o ingles), o -1 */
{
    char t[40]; if (n >= sizeof t) return -1;
    for (uint32_t i = 0; i < n; ++i) t[i] = s[i];
    t[n] = 0;
    const char *q = t; if (n > 6 && baja(t[0]) == '_' && baja(t[1]) == 'x' && baja(t[2]) == 'l' && baja(t[3]) == 'f' && baja(t[4]) == 'n' && t[5] == '.') q = t + 6;
    for (int i = 0; FNS[i].es; ++i) if (hj_ieq_pl(FNS[i].en, q)) return i;
    for (int i = 0; FNS[i].es; ++i) if (hj_ieq_pl(FNS[i].es, q)) return i;
    if (hj_ieq_pl(q, "STDEVA") || hj_ieq_pl(q, "CONTAR.VACIO")) return -1;
    return -1;
}
int hj_funcion_existe(const char *n) { return fn_busca(n, hj_slen(n)) >= 0; }
const char *hj_func_es(const char *en) { int i = fn_busca(en, hj_slen(en)); return i < 0 ? 0 : FNS[i].es; }
const char *hj_func_en(const char *es) { int i = fn_busca(es, hj_slen(es)); return i < 0 ? 0 : FNS[i].en; }
uint32_t hj_lista_funciones(char *out, uint32_t cap, int ingles)
{
    uint32_t p = 0; if (cap) out[0] = 0;
    for (int i = 0; FNS[i].es; ++i) { if (i) put(out, cap, &p, ", "); put(out, cap, &p, ingles ? FNS[i].en : FNS[i].es); }
    return p;
}

/* ================================================================ tokens */
enum { TK_FIN = 0, TK_NUM, TK_STR, TK_REF, TK_FUNC, TK_NOMBRE, TK_OP, TK_LPAR, TK_RPAR, TK_SEP, TK_BOOL, TK_ERR, TK_MAL };
enum { OPX_ADD = 1, OPX_SUB, OPX_MUL, OPX_DIV, OPX_POW, OPX_CAT, OPX_PCT, OPX_EQ, OPX_NE, OPX_LT, OPX_GT, OPX_LE, OPX_GE };
struct ref { int8_t hoja; uint8_t tipo, af1, ac1, af2, ac2, malo; uint32_t f1, c1, f2, c2; };   /* hoja -1 = la de la formula; -2 = desconocida. tipo 1 celda, 2 rango, 3 columnas, 4 filas */
struct tk { uint8_t t, op; int16_t fn; uint8_t b, err; uint32_t ini, fin, sh_ini; f64 num; struct ref r; };
struct lex { const char *s; uint32_t n; int ui; const struct hj_libro *l; int hoja; int ren_h; const char *ren_nombre; int destino; };   /* destino: 0 canonica/ui, 2 XLSX (_xlfn.), 3 ODS (of:) */   /* ren_*: al renombrar, el nombre nuevo de esa hoja */

static int ident_c(uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c >= 0x80; }
static int letra(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
/* una celda "$A$1" en s; devuelve bytes leidos (0 si no) */
static uint32_t lee_celda(const char *s, uint32_t n, uint32_t *f, uint32_t *c, uint8_t *af, uint8_t *ac)
{
    uint32_t i = 0, col = 0, fil = 0; int nl = 0, nd = 0; *af = *ac = 0;
    if (i < n && s[i] == '$') { *ac = 1; ++i; }
    while (i < n && letra(s[i]) && nl < 4) { col = col * 26 + (uint32_t)(sube(s[i]) - 'A' + 1); ++i; ++nl; }
    if (!nl || nl > 3) return 0;
    if (i < n && s[i] == '$') { *af = 1; ++i; }
    while (i < n && es_dig(s[i]) && nd < 8) { fil = fil * 10 + (uint32_t)(s[i] - '0'); ++i; ++nd; }
    if (!nd || !fil || fil > HJ_FILAS || col > HJ_COLS) return 0;
    if (i < n && (ident_c((uint8_t)s[i]) || s[i] == '(')) return 0;
    *f = fil - 1; *c = col - 1; return i;
}
static uint32_t lee_col(const char *s, uint32_t n, uint32_t *c, uint8_t *ac)
{
    uint32_t i = 0, col = 0; int nl = 0; *ac = 0;
    if (i < n && s[i] == '$') { *ac = 1; ++i; }
    while (i < n && letra(s[i]) && nl < 4) { col = col * 26 + (uint32_t)(sube(s[i]) - 'A' + 1); ++i; ++nl; }
    if (!nl || nl > 3 || col > HJ_COLS) return 0;
    if (i < n && (es_dig(s[i]) || s[i] == '$' || s[i] == '(')) return 0;
    *c = col - 1; return i;
}
static uint32_t lee_fila(const char *s, uint32_t n, uint32_t *f, uint8_t *af)
{
    uint32_t i = 0, fil = 0; int nd = 0; *af = 0;
    if (i < n && s[i] == '$') { *af = 1; ++i; }
    while (i < n && es_dig(s[i]) && nd < 8) { fil = fil * 10 + (uint32_t)(s[i] - '0'); ++i; ++nd; }
    if (!nd || !fil || fil > HJ_FILAS) return 0;
    *f = fil - 1; return i;
}
/* referencia a partir de i (tras el prefijo de hoja, si lo habia): celda, rango, columnas o filas */
static uint32_t lee_ref(const char *s, uint32_t n, struct ref *r)
{
    uint32_t k = lee_celda(s, n, &r->f1, &r->c1, &r->af1, &r->ac1);
    if (k) {
        r->tipo = 1; r->f2 = r->f1; r->c2 = r->c1; r->af2 = r->af1; r->ac2 = r->ac1;
        if (k < n && s[k] == ':') { uint32_t k2 = lee_celda(s + k + 1, n - k - 1, &r->f2, &r->c2, &r->af2, &r->ac2); if (k2) { r->tipo = 2; k += 1 + k2; } }
        return k;
    }
    k = lee_col(s, n, &r->c1, &r->ac1);
    if (k && k < n && s[k] == ':') { uint32_t k2 = lee_col(s + k + 1, n - k - 1, &r->c2, &r->ac2); if (k2) { r->tipo = 3; r->f1 = 0; r->f2 = HJ_FILAS - 1; return k + 1 + k2; } }
    k = lee_fila(s, n, &r->f1, &r->af1);
    if (k && k < n && s[k] == ':') { uint32_t k2 = lee_fila(s + k + 1, n - k - 1, &r->f2, &r->af2); if (k2 && (k + 1 + k2 >= n || !ident_c((uint8_t)s[k + 1 + k2]))) { r->tipo = 4; r->c1 = 0; r->c2 = HJ_COLS - 1; return k + 1 + k2; } }
    return 0;
}
static int tk_sig(const struct lex *lx, uint32_t pos, struct tk *t, int prev_t)
{
    const char *s = lx->s; uint32_t n = lx->n, i = pos;
    zero(t, (uint32_t)sizeof *t);
    while (i < n && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    t->ini = i; t->sh_ini = i;
    if (i >= n) { t->t = TK_FIN; t->fin = i; return 0; }
    char c = s[i];
    if (c == '"') {
        ++i; while (i < n) { if (s[i] == '"') { if (i + 1 < n && s[i + 1] == '"') { i += 2; continue; } break; } ++i; }
        if (i >= n) { t->t = TK_MAL; t->fin = n; return -1; }
        t->t = TK_STR; t->fin = i + 1; return 0;
    }
    if (c == '(') { t->t = TK_LPAR; t->fin = i + 1; return 0; }
    if (c == ')') { t->t = TK_RPAR; t->fin = i + 1; return 0; }
    if (c == ';' || (c == ',' && (!lx->ui || !(prev_t == TK_NUM && i + 1 < n && es_dig(s[i + 1]))))) {
        /* en la interfaz la coma entre cifras es decimal (y ya la ha tomado el numero); en otro sitio separa */
        t->t = TK_SEP; t->fin = i + 1; return 0;
    }
    if (c == '#') {                                              /* error escrito */
        for (int e = 1; e < HJE_N; ++e) for (int ing = 0; ing < 2; ++ing) {
            const char *nm = hj_nombre_error(e, ing); uint32_t L = hj_slen(nm), k = 0;
            while (k < L && i + k < n && baja(s[i + k]) == baja(nm[k])) ++k;
            if (k == L) {
                if (e == HJE_REF && i + L < n && (letra(s[i + L]) || s[i + L] == '$')) {   /* #REF!A1: una referencia rota */
                    struct ref r; zero(&r, sizeof r); uint32_t k2 = lee_ref(s + i + L, n - i - L, &r);
                    if (k2) { t->t = TK_REF; t->r = r; t->r.malo = 1; t->r.hoja = -1; t->fin = i + L + k2; return 0; }
                }
                t->t = TK_ERR; t->err = (uint8_t)e; t->fin = i + L; return 0;
            }
        }
        t->t = TK_MAL; t->fin = i + 1; return -1;
    }
    /* hoja entre comillas: 'Mi hoja'!A1 */
    int8_t hoja = -1; uint32_t q = i;
    if (c == '\'') {
        uint32_t j = i + 1; char nm[HJ_NOMBRE * 2]; uint32_t k = 0;
        while (j < n) { if (s[j] == '\'') { if (j + 1 < n && s[j + 1] == '\'') { if (k + 1 < sizeof nm) nm[k++] = '\''; j += 2; continue; } break; } if (k + 1 < sizeof nm) nm[k++] = s[j]; ++j; }
        nm[k] = 0;
        if (j >= n || j + 1 >= n || s[j + 1] != '!') { t->t = TK_MAL; t->fin = j < n ? j + 1 : n; return -1; }
        int h = lx->l ? hj_hoja_busca(lx->l, nm) : -2; hoja = (int8_t)(h < 0 ? -2 : h);
        q = j + 2;
    } else if (letra(c) || c == '_' || (uint8_t)c >= 0x80) {
        uint32_t j = i; while (j < n && ident_c((uint8_t)s[j])) ++j;
        if (j < n && s[j] == '!') {
            char nm[HJ_NOMBRE * 2]; uint32_t k = 0; for (uint32_t z = i; z < j && k + 1 < sizeof nm; ++z) nm[k++] = s[z];
            nm[k] = 0; int h = lx->l ? hj_hoja_busca(lx->l, nm) : -2; hoja = (int8_t)(h < 0 ? -2 : h); q = j + 1;
        }
    }
    if (q != i) {                                                /* tras "Hoja!" tiene que venir una referencia */
        struct ref r; zero(&r, sizeof r); uint32_t k = lee_ref(s + q, n - q, &r);
        if (!k) { if (q + 5 <= n && s[q] == '#') { t->t = TK_ERR; t->err = HJE_REF; t->fin = q + 5; return 0; } t->t = TK_MAL; t->fin = q; return -1; }
        t->t = TK_REF; t->r = r; t->r.hoja = hoja; t->sh_ini = i; t->ini = i; t->fin = q + k; return 0;
    }
    if (c == '$' || letra(c) || es_dig(c)) {
        struct ref r; zero(&r, sizeof r); uint32_t k = lee_ref(s + i, n - i, &r);
        if (k && !(r.tipo == 4 && (prev_t == TK_NUM))) { t->t = TK_REF; t->r = r; t->r.hoja = -1; t->fin = i + k; return 0; }
    }
    if (es_dig(c) || (c == '.' && i + 1 < n && es_dig(s[i + 1])) || (c == ',' && lx->ui && i + 1 < n && es_dig(s[i + 1]))) {
        char b[80]; uint32_t k = 0, j = i;
        while (j < n && es_dig(s[j])) { if (k + 1 < sizeof b) b[k++] = s[j]; ++j; }
        if (j < n && (s[j] == '.' || (lx->ui && s[j] == ',' && j + 1 < n && es_dig(s[j + 1])))) { if (k + 1 < sizeof b) b[k++] = '.'; ++j; while (j < n && es_dig(s[j])) { if (k + 1 < sizeof b) b[k++] = s[j]; ++j; } }
        if (j < n && (s[j] == 'e' || s[j] == 'E') && j + 1 < n && (es_dig(s[j + 1]) || ((s[j + 1] == '+' || s[j + 1] == '-') && j + 2 < n && es_dig(s[j + 2])))) {
            if (k + 1 < sizeof b) b[k++] = 'e';
            ++j;
            if (s[j] == '+' || s[j] == '-') { if (k + 1 < sizeof b) b[k++] = s[j]; ++j; }
            while (j < n && es_dig(s[j])) { if (k + 1 < sizeof b) b[k++] = s[j]; ++j; }
        }
        b[k] = 0;
        f64 v; if (!parse_punto(b, &v)) { t->t = TK_MAL; t->fin = j; return -1; }
        t->t = TK_NUM; t->num = v; t->fin = j; return 0;
    }
    if (letra(c) || c == '_' || (uint8_t)c >= 0x80) {
        uint32_t j = i; while (j < n && ident_c((uint8_t)s[j])) ++j;
        uint32_t k = j; while (k < n && s[k] == ' ') ++k;
        t->fin = j;
        if (k < n && s[k] == '(') { t->t = TK_FUNC; t->fn = (int16_t)fn_busca(s + i, j - i); return 0; }
        char w[16]; uint32_t L = j - i < 15 ? j - i : 15; for (uint32_t z = 0; z < L; ++z) w[z] = s[i + z];
        w[L] = 0;
        if (j - i < 15 && (hj_ieq_pl(w, "true") || hj_ieq_pl(w, "verdadero"))) { t->t = TK_BOOL; t->b = 1; return 0; }
        if (j - i < 15 && (hj_ieq_pl(w, "false") || hj_ieq_pl(w, "falso"))) { t->t = TK_BOOL; t->b = 0; return 0; }
        t->t = TK_NOMBRE; return 0;
    }
    t->t = TK_OP; t->fin = i + 1;
    switch (c) {
    case '+': t->op = OPX_ADD; return 0; case '-': t->op = OPX_SUB; return 0; case '*': t->op = OPX_MUL; return 0; case '/': t->op = OPX_DIV; return 0;
    case '^': t->op = OPX_POW; return 0; case '&': t->op = OPX_CAT; return 0; case '%': t->op = OPX_PCT; return 0; case '=': t->op = OPX_EQ; return 0;
    case '<': if (i + 1 < n && s[i + 1] == '>') { t->op = OPX_NE; t->fin++; } else if (i + 1 < n && s[i + 1] == '=') { t->op = OPX_LE; t->fin++; } else t->op = OPX_LT; return 0;
    case '>': if (i + 1 < n && s[i + 1] == '=') { t->op = OPX_GE; t->fin++; } else t->op = OPX_GT; return 0;
    default: break;
    }
    t->t = TK_MAL; return -1;
}

/* ---- escribir una referencia */
static void pon_hoja(const struct lex *lx, int hoja, char *o, uint32_t cap, uint32_t *p)
{
    const struct hj_libro *l = lx->l;
    const char *nm = lx->ren_nombre && hoja == lx->ren_h ? lx->ren_nombre : l && hoja >= 0 && hoja < l->nh ? l->h[hoja].nombre : "?";
    int comillas = 0;
    for (const char *z = nm; *z; ++z) if (!ident_c((uint8_t)*z) || *z == '.') comillas = 1;
    if (es_dig(nm[0])) comillas = 1;
    { uint32_t f, c; if (hj_lee_dir(nm, &f, &c) == (int)hj_slen(nm)) comillas = 1; }
    if (comillas) { putc_(o, cap, p, '\''); for (const char *z = nm; *z; ++z) { if (*z == '\'') putc_(o, cap, p, '\''); putc_(o, cap, p, *z); } putc_(o, cap, p, '\''); }
    else put(o, cap, p, nm);
    putc_(o, cap, p, '!');
}
static void pon_celda(uint32_t f, uint32_t c, uint8_t af, uint8_t ac, char *o, uint32_t cap, uint32_t *p)
{
    char b[8]; if (ac) putc_(o, cap, p, '$'); hj_nombre_col(c, b); put(o, cap, p, b); if (af) putc_(o, cap, p, '$'); putn(o, cap, p, (int64_t)f + 1);
}
static void pon_ref(const struct lex *lx, const struct tk *t, const struct ref *r, char *o, uint32_t cap, uint32_t *p)
{
    if (r->malo) { put(o, cap, p, "#REF!"); return; }
    if (r->hoja >= 0) pon_hoja(lx, r->hoja, o, cap, p);
    else if (r->hoja == -2) { for (uint32_t z = t->sh_ini; z < t->fin && lx->s[z] != '!'; ++z) putc_(o, cap, p, lx->s[z]); putc_(o, cap, p, '!'); }
    if (r->tipo == 3) { char b[8]; if (r->ac1) putc_(o, cap, p, '$'); hj_nombre_col(r->c1, b); put(o, cap, p, b); putc_(o, cap, p, ':'); if (r->ac2) putc_(o, cap, p, '$'); hj_nombre_col(r->c2, b); put(o, cap, p, b); return; }
    if (r->tipo == 4) { if (r->af1) putc_(o, cap, p, '$'); putn(o, cap, p, (int64_t)r->f1 + 1); putc_(o, cap, p, ':'); if (r->af2) putc_(o, cap, p, '$'); putn(o, cap, p, (int64_t)r->f2 + 1); return; }
    pon_celda(r->f1, r->c1, r->af1, r->ac1, o, cap, p);
    if (r->tipo == 2) { putc_(o, cap, p, ':'); pon_celda(r->f2, r->c2, r->af2, r->ac2, o, cap, p); }
}
/* ODS (OpenFormula): [.A1], [.A1:.B2], [$'Mi hoja'.A1:.B2] */
static void pon_ref_ods(const struct lex *lx, const struct tk *t, const struct ref *r, char *o, uint32_t cap, uint32_t *p)
{
    (void)t;
    if (r->malo || r->hoja == -2) { put(o, cap, p, "#REF!"); return; }
    uint32_t f1 = r->f1, f2 = r->f2, c1 = r->c1, c2 = r->c2; int rango = r->tipo != 1;
    putc_(o, cap, p, '[');
    if (r->hoja >= 0) {
        const char *nm = lx->l && r->hoja < lx->l->nh ? lx->l->h[r->hoja].nombre : "?";
        putc_(o, cap, p, '$'); putc_(o, cap, p, '\''); for (const char *z = nm; *z; ++z) { if (*z == '\'') putc_(o, cap, p, '\''); putc_(o, cap, p, *z); } putc_(o, cap, p, '\'');
    }
    putc_(o, cap, p, '.'); pon_celda(f1, c1, r->af1, r->ac1, o, cap, p);
    if (rango) { put(o, cap, p, ":."); pon_celda(f2, c2, r->af2, r->ac2, o, cap, p); }
    putc_(o, cap, p, ']');
}
/* reescribe: a_ui = destino en castellano; mapa = cambia una referencia (devuelve 1 si queda rota) */
typedef int (*mapa_ref)(struct ref *r, void *ctx);
static int reescribe(const struct lex *lx, int a_ui, mapa_ref mapa, void *mctx, char *o, uint32_t cap, int *err)
{
    uint32_t p = 0, pos = 0; struct tk t; int prev = TK_FIN; int nivel = 0;
    if (cap) o[0] = 0;
    for (;;) {
        if (tk_sig(lx, pos, &t, prev) < 0) { if (err) *err = (int)t.ini; return -1; }
        for (uint32_t z = pos; z < t.ini; ++z) putc_(o, cap, &p, lx->s[z]);           /* blancos */
        if (t.t == TK_FIN) break;
        switch (t.t) {
        case TK_NUM: for (uint32_t z = t.ini; z < t.fin; ++z) { char c = lx->s[z]; if (c == '.' || (c == ',' && lx->ui)) c = a_ui ? ',' : '.'; putc_(o, cap, &p, c); } break;
        case TK_SEP: putc_(o, cap, &p, a_ui || lx->destino == 3 ? ';' : ','); break;
        case TK_BOOL: put(o, cap, &p, a_ui ? (t.b ? "VERDADERO" : "FALSO") : lx->destino == 3 ? (t.b ? "TRUE()" : "FALSE()") : (t.b ? "TRUE" : "FALSE")); break;
        case TK_ERR: put(o, cap, &p, hj_nombre_error(t.err, !a_ui)); break;
        case TK_FUNC:
            if (t.fn >= 0 && (FNS[t.fn].id == FN_TRUE || FNS[t.fn].id == FN_FALSE)) {   /* VERDADERO() = VERDADERO */
                struct tk a1, a2;
                if (tk_sig(lx, t.fin, &a1, TK_FUNC) == 0 && a1.t == TK_LPAR && tk_sig(lx, a1.fin, &a2, TK_LPAR) == 0 && a2.t == TK_RPAR) {
                    int b = FNS[t.fn].id == FN_TRUE;
                    put(o, cap, &p, a_ui ? (b ? "VERDADERO" : "FALSO") : lx->destino == 3 ? (b ? "TRUE()" : "FALSE()") : (b ? "TRUE" : "FALSE"));
                    prev = TK_BOOL; pos = a2.fin; continue;
                }
            }
            if (t.fn >= 0) { if (lx->destino == 2 && FNS[t.fn].xlfn) put(o, cap, &p, "_xlfn."); put(o, cap, &p, a_ui ? FNS[t.fn].es : FNS[t.fn].en); }
            else for (uint32_t z = t.ini; z < t.fin; ++z) putc_(o, cap, &p, sube(lx->s[z]));
            break;
        case TK_REF: { struct ref r = t.r; if (mapa && !r.malo && mapa(&r, mctx)) r.malo = 1; if (lx->destino == 3) pon_ref_ods(lx, &t, &r, o, cap, &p); else pon_ref(lx, &t, &r, o, cap, &p); break; }
        case TK_LPAR: ++nivel; putc_(o, cap, &p, '('); break;
        case TK_RPAR: --nivel; putc_(o, cap, &p, ')'); break;
        default: for (uint32_t z = t.ini; z < t.fin; ++z) putc_(o, cap, &p, lx->s[z]); break;
        }
        prev = t.t; pos = t.fin;
        if (p + 2 >= cap) { if (err) *err = (int)pos; return -1; }
    }
    (void)nivel;
    return 0;
}
int hj_form_a_canon(const struct hj_libro *l, int h, const char *ui, char *out, uint32_t cap, int *err)
{
    while (*ui == '=') ++ui;
    struct lex lx = { ui, hj_slen(ui), 1, l, h, -1, 0, 0 };
    return reescribe(&lx, 0, 0, 0, out, cap, err);
}
int hj_form_a_ui(const struct hj_libro *l, int h, const char *canon, char *out, uint32_t cap)
{
    struct lex lx = { canon, hj_slen(canon), 0, l, h, -1, 0, 0 };
    return reescribe(&lx, 1, 0, 0, out, cap, 0);
}
struct desp { int32_t df, dc; };
static int mapa_desp(struct ref *r, void *v)
{
    struct desp *d = (struct desp *)v;
    int64_t f1 = r->f1, f2 = r->f2, c1 = r->c1, c2 = r->c2;
    if (r->tipo != 3) { if (!r->af1) f1 += d->df; if (!r->af2) f2 += d->df; }
    if (r->tipo != 4) { if (!r->ac1) c1 += d->dc; if (!r->ac2) c2 += d->dc; }
    if (f1 < 0 || f2 < 0 || c1 < 0 || c2 < 0 || f1 >= HJ_FILAS || f2 >= HJ_FILAS || c1 >= HJ_COLS || c2 >= HJ_COLS) return 1;
    r->f1 = (uint32_t)f1; r->f2 = (uint32_t)f2; r->c1 = (uint32_t)c1; r->c2 = (uint32_t)c2; return 0;
}
int hj_form_normaliza(const struct hj_libro *l, int h, const char *f, char *out, uint32_t cap)
{
    while (*f == '=') ++f;
    struct lex lx = { f, hj_slen(f), 0, l, h, -1, 0, 0 };
    return reescribe(&lx, 0, 0, 0, out, cap, 0);
}
int hj_form_xlsx(const struct hj_libro *l, int h, const char *canon, char *out, uint32_t cap)
{
    struct lex lx = { canon, hj_slen(canon), 0, l, h, -1, 0, 2 };
    return reescribe(&lx, 0, 0, 0, out, cap, 0);
}
int hj_form_ods(const struct hj_libro *l, int h, const char *canon, char *out, uint32_t cap)
{
    struct lex lx = { canon, hj_slen(canon), 0, l, h, -1, 0, 3 };
    return reescribe(&lx, 0, 0, 0, out, cap, 0);
}
static int iemp_c(const char *s, const char *p) { while (*p) { if (baja(*s) != baja(*p)) return 0; ++s; ++p; } return 1; }
/* de OpenFormula ("of:=SUM([.A1:.B2];[$Hoja2.C3])") a canonica */
int hj_form_de_ods(const struct hj_libro *l, int h, const char *of, char *out, uint32_t cap)
{
    const char *s = of; uint32_t p = 0;
    if ((s[0] == 'o' && s[1] == 'f' && s[2] == ':') ) s += 3; else if (s[0] == 'o' && s[1] == 'o' && s[2] == 'o' && s[3] == 'c' && s[4] == ':') s += 5; else if (s[0] == 'm' && s[1] == 's' && s[2] == 'o' && s[3] == 'x' && s[4] == 'm' && s[5] == 'l' && s[6] == ':') s += 7;
    while (*s == '=') ++s;
    char *tmp = (char *)hj_alloc(cap); if (!tmp) return -1;
    tmp[0] = 0;
    while (*s && p + 2 < cap) {
        if (*s == '"') { putc_(tmp, cap, &p, *s++); while (*s) { if (*s == '"') { if (s[1] == '"') { putc_(tmp, cap, &p, '"'); putc_(tmp, cap, &p, '"'); s += 2; continue; } break; } putc_(tmp, cap, &p, *s++); } if (*s) putc_(tmp, cap, &p, *s++); continue; }
        if (*s == ';') { putc_(tmp, cap, &p, ','); ++s; continue; }
        if ((p == 0 || !ident_c((uint8_t)tmp[p - 1])) && (iemp_c(s, "TRUE()") || iemp_c(s, "FALSE()"))) { int v = baja(*s) == 't'; put(tmp, cap, &p, v ? "TRUE" : "FALSE"); s += v ? 6 : 7; continue; }
        if (*s == '[') {
            ++s; int vez = 0; char hoja_ant[HJ_NOMBRE * 2] = "";
            while (*s && *s != ']') {
                if (vez) { if (*s == ':') { putc_(tmp, cap, &p, ':'); ++s; } else break; }
                if (*s == '$') ++s;
                char nm[HJ_NOMBRE * 2]; uint32_t k = 0; nm[0] = 0;
                if (*s == '\'') { ++s; while (*s) { if (*s == '\'') { if (s[1] == '\'') { if (k + 1 < sizeof nm) nm[k++] = '\''; s += 2; continue; } ++s; break; } if (k + 1 < sizeof nm) nm[k++] = *s; ++s; } nm[k] = 0; }
                else { while (*s && *s != '.' && *s != ']' && *s != ':') { if (k + 1 < sizeof nm) nm[k++] = *s; ++s; } nm[k] = 0; if (*s != '.') { /* sin punto: no era hoja */ for (uint32_t z = 0; z < k; ++z) putc_(tmp, cap, &p, nm[z]); nm[0] = 0; k = 0; } }
                if (*s == '.') ++s;
                if (nm[0] && !vez) { uint32_t L = hj_slen(nm); (void)L; putc_(tmp, cap, &p, '\''); for (const char *z = nm; *z; ++z) { if (*z == '\'') putc_(tmp, cap, &p, '\''); putc_(tmp, cap, &p, *z); } put(tmp, cap, &p, "'!"); cpy(hoja_ant, sizeof hoja_ant, nm); }
                while (*s && *s != ']' && *s != ':') putc_(tmp, cap, &p, *s++);
                vez = 1;
            }
            if (*s == ']') ++s;
            continue;
        }
        putc_(tmp, cap, &p, *s++);
    }
    tmp[p] = 0;
    struct lex lx = { tmp, p, 0, l, h, -1, 0, 0 };
    int r = reescribe(&lx, 0, 0, 0, out, cap, 0);
    hj_free(tmp);
    return r;
}
int hj_form_desplaza(const struct hj_libro *l, int h, const char *canon, int32_t df, int32_t dc, char *out, uint32_t cap)
{
    struct lex lx = { canon, hj_slen(canon), 0, l, h, -1, 0, 0 }; struct desp d = { df, dc };
    return reescribe(&lx, 0, mapa_desp, &d, out, cap, 0);
}
/* insertar/borrar filas o columnas en la hoja `hoja` */
struct mov { int hoja_f, hoja_x; int filas; uint32_t en, n; int borrar; };
static int mapa_mov(struct ref *r, void *v)
{
    struct mov *m = (struct mov *)v;
    int h = r->hoja >= 0 ? r->hoja : m->hoja_f;
    if (h != m->hoja_x) return 0;
    uint32_t *a = m->filas ? &r->f1 : &r->c1, *b = m->filas ? &r->f2 : &r->c2;
    uint32_t lim = m->filas ? HJ_FILAS : HJ_COLS;
    if ((m->filas && r->tipo == 3) || (!m->filas && r->tipo == 4)) return 0;   /* columna entera al mover filas: no cambia */
    if (!m->borrar) {
        if (*a >= m->en) *a += m->n;
        if (*b >= m->en) *b += m->n;
        if (*a >= lim || *b >= lim) return 1;
        return 0;
    }
    uint32_t fin = m->en + m->n;                                 /* [en, fin) desaparece */
    if (*a >= m->en && *b < fin) return 1;                       /* todo dentro */
    if (*a >= fin) *a -= m->n; else if (*a >= m->en) *a = m->en;
    if (*b >= fin) *b -= m->n; else if (*b >= m->en) { if (m->en == 0) return 1; *b = m->en - 1; }
    return 0;
}
struct mhoja { int borrada, hoja_f; };
static int mapa_hoja(struct ref *r, void *v)
{
    struct mhoja *m = (struct mhoja *)v;
    int h = r->hoja >= 0 ? r->hoja : m->hoja_f;
    if (m->borrada >= 0 && h == m->borrada) return 1;
    if (m->borrada >= 0 && r->hoja > m->borrada) r->hoja--;
    return 0;
}

/* aplica un mapa a todas las formulas del libro (no compila; solo reescribe el texto) */
static int g_ren_h = -1; static const char *g_ren_nombre;
static int form_mapea_todo(struct hj_libro *l, mapa_ref mapa, struct mov *mv, struct mhoja *mh)
{
    char *b = (char *)hj_alloc(HJ_FORM_MAX + 256); if (!b) return -1;
    int n = 0;
    for (int h = 0; h < l->nh; ++h) {
        struct hj_hoja *s = &l->h[h];
        for (int32_t i = 0; i < s->nc; ++i) {
            struct hj_celda *x = &s->c[i]; if (!x->es_form || !x->form) continue;
            struct lex lx = { x->form, hj_slen(x->form), 0, l, h, g_ren_h, g_ren_nombre, 0 };
            if (mv) mv->hoja_f = h;
            if (mh) mh->hoja_f = h;
            if (reescribe(&lx, 0, mapa, mv ? (void *)mv : (void *)mh, b, HJ_FORM_MAX + 256, 0)) continue;
            uint32_t L = hj_slen(b); if (!L || L > HJ_FORM_MAX) continue;
            if (L == hj_slen(x->form) && igual_mem(b, x->form, L)) continue;
            char *d = hj_strndup(b, L); if (!d) continue;
            hj_free(x->form); x->form = d; if (x->code) { hj_free(x->code); x->code = 0; x->ncode = 0; } ++n;
        }
    }
    hj_free(b); return n;
}
static int form_hojas(struct hj_libro *l, int borrada, int de, int a)
{
    (void)de; (void)a;
    struct mhoja m = { borrada, 0 };
    return form_mapea_todo(l, mapa_hoja, 0, &m);
}
static void formulas_rehace(struct hj_libro *l)
{
    /* los indices de hoja de las formulas compiladas cambian: se tira la compilacion (el texto lleva nombres, no indices) */
    for (int h = 0; h < l->nh; ++h) for (int32_t i = 0; i < l->h[h].nc; ++i) { struct hj_celda *x = &l->h[h].c[i]; if (x->code) { hj_free(x->code); x->code = 0; x->ncode = 0; } }
}

/* ================================================================ compilador */
enum { OC_NUM = 1, OC_STR, OC_BOOL, OC_ERR, OC_REF, OC_NEG, OC_PCT, OC_BIN, OC_CALL, OC_JMPF, OC_JMP, OC_JNOERR, OC_JNONA, OC_POP, OC_FIN };
struct cmp { const struct lex *lx; struct tk t; uint32_t pos; int prev; uint8_t *c; uint32_t n, cap; int mal; int prof; };
static void cb(struct cmp *k, const void *d, uint32_t n)
{
    if (k->mal) return;
    if (k->n + n > k->cap) {
        uint32_t nc = k->cap * 2; while (nc < k->n + n) nc *= 2;
        if (nc > 262144) { k->mal = 1; return; }
        uint8_t *x = (uint8_t *)hj_alloc(nc); if (!x) { k->mal = 1; return; }
        copia(x, k->c, k->n); hj_free(k->c); k->c = x; k->cap = nc;
    }
    copia(k->c + k->n, d, n); k->n += n;
}
static void c1(struct cmp *k, uint8_t b) { cb(k, &b, 1); }
static void c32(struct cmp *k, uint32_t v) { cb(k, &v, 4); }
static void adelanta(struct cmp *k) { k->prev = k->t.t; k->pos = k->t.fin; if (tk_sig(k->lx, k->pos, &k->t, k->prev) < 0) k->mal = 1; }
static void expr(struct cmp *k);
static void primario(struct cmp *k)
{
    if (k->mal) return;
    if (++k->prof > 64) { k->mal = 1; return; }
    struct tk t = k->t;
    switch (t.t) {
    case TK_NUM: c1(k, OC_NUM); cb(k, &t.num, 8); adelanta(k); break;
    case TK_STR: {
        c1(k, OC_STR); uint32_t L = 0; char tmp[4];
        uint32_t pos_len = k->n; c32(k, 0);
        for (uint32_t z = t.ini + 1; z + 1 < t.fin; ++z) { char ch = k->lx->s[z]; if (ch == '"' && k->lx->s[z + 1] == '"') ++z; tmp[0] = ch; cb(k, tmp, 1); ++L; }
        if (!k->mal) copia(k->c + pos_len, &L, 4);
        adelanta(k); break;
    }
    case TK_BOOL: c1(k, OC_BOOL); c1(k, t.b); adelanta(k); break;
    case TK_ERR: c1(k, OC_ERR); c1(k, t.err); adelanta(k); break;
    case TK_REF: if (t.r.malo || t.r.hoja == -2) { c1(k, OC_ERR); c1(k, HJE_REF); } else { c1(k, OC_REF); cb(k, &t.r, (uint32_t)sizeof t.r); } adelanta(k); break;
    case TK_NOMBRE: c1(k, OC_ERR); c1(k, HJE_NOMBRE); adelanta(k); break;
    case TK_LPAR: adelanta(k); expr(k); if (k->t.t != TK_RPAR) { k->mal = 1; break; } adelanta(k); break;
    case TK_OP:
        if (t.op == OPX_SUB || t.op == OPX_ADD) { adelanta(k); primario(k); if (t.op == OPX_SUB) c1(k, OC_NEG); break; }
        k->mal = 1; break;
    case TK_FUNC: {
        int fn = t.fn; adelanta(k);                              /* nombre */
        if (k->t.t != TK_LPAR) { k->mal = 1; break; }
        adelanta(k);
        if (fn < 0) {                                            /* funcion que no existe: #NOMBRE? (pero el texto se conserva) */
            int nivel = 1; while (!k->mal && k->t.t != TK_FIN && nivel) { if (k->t.t == TK_LPAR) ++nivel; if (k->t.t == TK_RPAR) --nivel; if (nivel) adelanta(k); }
            if (k->t.t != TK_RPAR) { k->mal = 1; break; }
            adelanta(k); c1(k, OC_ERR); c1(k, HJE_NOMBRE); break;
        }
        int id = FNS[fn].id, argc = 0;
        if (id == FN_IF || id == FN_IFERROR || id == FN_IFNA) {
            if (k->t.t == TK_RPAR) { k->mal = 1; break; }
            expr(k);
            if (k->t.t != TK_SEP) { k->mal = 1; break; }
            adelanta(k);
            if (id == FN_IF) {
                c1(k, OC_JMPF); uint32_t j1 = k->n; c32(k, 0); c32(k, 0);
                if (k->t.t == TK_SEP || k->t.t == TK_RPAR) { c1(k, OC_NUM); f64 z = 0; cb(k, &z, 8); } else expr(k);
                c1(k, OC_JMP); uint32_t j2 = k->n; c32(k, 0);
                uint32_t a1 = k->n; if (!k->mal) copia(k->c + j1, &a1, 4);
                if (k->t.t == TK_SEP) { adelanta(k); if (k->t.t == TK_RPAR) { c1(k, OC_NUM); f64 z = 0; cb(k, &z, 8); } else expr(k); }
                else { c1(k, OC_BOOL); c1(k, 0); }
                uint32_t a2 = k->n; if (!k->mal) { copia(k->c + j2, &a2, 4); copia(k->c + j1 + 4, &a2, 4); }
            } else {
                c1(k, id == FN_IFERROR ? OC_JNOERR : OC_JNONA); uint32_t j1 = k->n; c32(k, 0);
                c1(k, OC_POP); expr(k);
                uint32_t a1 = k->n; if (!k->mal) copia(k->c + j1, &a1, 4);
            }
            if (k->t.t != TK_RPAR) { k->mal = 1; break; }
            adelanta(k); break;
        }
        if (k->t.t != TK_RPAR) for (;;) {
            if (k->t.t == TK_SEP || k->t.t == TK_RPAR) { c1(k, OC_ERR); c1(k, 255); }   /* argumento vacio */
            else expr(k);
            ++argc;
            if (k->mal || argc > 255) { k->mal = 1; break; }
            if (k->t.t == TK_SEP) { adelanta(k); if (k->t.t == TK_RPAR) { c1(k, OC_ERR); c1(k, 255); ++argc; break; } continue; }
            break;
        }
        if (k->t.t != TK_RPAR) { k->mal = 1; break; }
        adelanta(k);
        if (argc < FNS[fn].min || argc > FNS[fn].max) { for (int i = 0; i < argc; ++i) c1(k, OC_POP); c1(k, OC_ERR); c1(k, HJE_VALOR); break; }
        c1(k, OC_CALL); c1(k, (uint8_t)id); c1(k, (uint8_t)argc);
        break;
    }
    default: k->mal = 1; break;
    }
    while (!k->mal && k->t.t == TK_OP && k->t.op == OPX_PCT) { adelanta(k); c1(k, OC_PCT); }
    --k->prof;
}
static int prec(int op) { switch (op) { case OPX_EQ: case OPX_NE: case OPX_LT: case OPX_GT: case OPX_LE: case OPX_GE: return 1; case OPX_CAT: return 2; case OPX_ADD: case OPX_SUB: return 3; case OPX_MUL: case OPX_DIV: return 4; case OPX_POW: return 5; default: return 0; } }
static void binario(struct cmp *k, int minp)
{
    primario(k);
    while (!k->mal && k->t.t == TK_OP && prec(k->t.op) >= minp) {
        int op = k->t.op, p = prec(op);
        adelanta(k);
        if (++k->prof > 64) { k->mal = 1; return; }
        binario(k, p + 1);                                       /* todos asociativos por la izquierda (^ tambien, como Excel) */
        --k->prof;
        c1(k, OC_BIN); c1(k, (uint8_t)op);
    }
}
static void expr(struct cmp *k) { if (++k->prof > 64) { k->mal = 1; return; } binario(k, 1); --k->prof; }
static int compila(const struct hj_libro *l, int h, const char *canon, uint8_t **code, uint32_t *ncode)
{
    struct lex lx = { canon, hj_slen(canon), 0, l, h, -1, 0, 0 };
    struct cmp k; zero(&k, sizeof k); k.lx = &lx; k.cap = 256; k.c = (uint8_t *)hj_alloc(k.cap);
    *code = 0; *ncode = 0;
    if (!k.c) return -1;
    if (tk_sig(&lx, 0, &k.t, TK_FIN) < 0) k.mal = 1;
    if (!k.mal && k.t.t == TK_FIN) k.mal = 1;
    if (!k.mal) expr(&k);
    if (!k.mal && k.t.t != TK_FIN) k.mal = 1;
    c1(&k, OC_FIN);
    if (k.mal) { hj_free(k.c); return -1; }
    *code = k.c; *ncode = k.n; return 0;
}

/* ================================================================ evaluador */
#define EV_REF 10
#define PILA 64
struct ev { uint8_t t, err; f64 n; const char *s; struct ref r; };
struct ectx {
    struct hj_libro *l; int hoja; uint32_t fila, col, gen;
    int incompleto; uint32_t *falta; uint32_t nfalta;            /* (hoja<<27 | indice) de las formulas que faltan */
    char *ar; uint32_t an, acap;                                 /* textos temporales */
    struct ev pila[PILA];
};
static uint32_t rng_estado = 0x9E3779B9u;
static uint32_t rng(void) { uint32_t x = rng_estado; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng_estado = x; return x; }
static char *arena(struct ectx *e, uint32_t n) { if (e->an + n + 1 > e->acap) return 0; char *p = e->ar + e->an; e->an += n + 1; p[n] = 0; return p; }
static void v_num(struct ev *v, f64 n) { v->t = HJ_NUM; v->n = f64_is_finite(n) ? n : 0; v->err = 0; v->s = ""; if (!f64_is_finite(n)) { v->t = HJ_ERR; v->err = HJE_NUM; } }
static void v_err(struct ev *v, int e) { v->t = HJ_ERR; v->err = (uint8_t)e; v->n = 0; v->s = ""; }
static void v_bool(struct ev *v, int b) { v->t = HJ_BOOL; v->n = b ? F(1) : 0; v->err = 0; v->s = ""; }
static void v_txt(struct ectx *e, struct ev *v, const char *s, uint32_t n) { char *d = arena(e, n); if (!d) { v_err(v, HJE_VALOR); return; } copia(d, s, n); v->t = HJ_TXT; v->s = d; v->err = 0; v->n = 0; }
static void v_vacia(struct ev *v) { v->t = HJ_VACIA; v->n = 0; v->s = ""; v->err = 0; }
/* el valor de una celda; si es una formula sin calcular en esta vuelta, se apunta como pendiente */
static void celda_val(struct ectx *e, int h, uint32_t f, uint32_t c, struct ev *v)
{
    v_vacia(v);
    if (h < 0 || h >= e->l->nh) { v_err(v, HJE_REF); return; }
    struct hj_hoja *s = &e->l->h[h]; int32_t i = busca_i(s, f, c);
    if (i < 0) return;
    struct hj_celda *x = &s->c[i];
    if (x->es_form && x->gen != e->gen) {
        if (x->estado == 1) { v_err(v, HJE_CICLO); return; }
        e->incompleto = 1;
        if (e->nfalta < 4096) e->falta[e->nfalta++] = ((uint32_t)h << 27) | (uint32_t)i;
        v_num(v, 0); return;
    }
    switch (x->t) {
    case HJ_NUM: v->t = HJ_NUM; v->n = x->n; break;
    case HJ_BOOL: v->t = HJ_BOOL; v->n = x->n; break;
    case HJ_TXT: v->t = HJ_TXT; v->s = x->txt ? x->txt : ""; break;
    case HJ_ERR: v->t = HJ_ERR; v->err = x->err; break;
    default: if (x->es_form) { v->t = HJ_NUM; v->n = 0; } break;
    }
}
static int ref_hoja(const struct ectx *e, const struct ref *r) { return r->hoja >= 0 ? r->hoja : e->hoja; }
/* una referencia usada como valor: si es una sola celda, su valor; si es un rango, la interseccion implicita con la fila/columna */
static void deref(struct ectx *e, struct ev *v)
{
    if (v->t != EV_REF) return;
    struct ref r = v->r; int h = ref_hoja(e, &r);
    if (r.f1 == r.f2 && r.c1 == r.c2) { celda_val(e, h, r.f1, r.c1, v); return; }
    if (r.c1 == r.c2 && e->fila >= r.f1 && e->fila <= r.f2 && h == e->hoja) { celda_val(e, h, e->fila, r.c1, v); return; }
    if (r.f1 == r.f2 && e->col >= r.c1 && e->col <= r.c2 && h == e->hoja) { celda_val(e, h, r.f1, e->col, v); return; }
    v_err(v, HJE_VALOR);
}
static int a_num(struct ectx *e, struct ev *v, f64 *n)          /* 0 = bien; si no, v queda con el error */
{
    deref(e, v);
    switch (v->t) {
    case HJ_NUM: case HJ_BOOL: *n = v->n; return 0;
    case HJ_VACIA: *n = 0; return 0;
    case HJ_TXT: { int nf; if (!v->s[0]) { *n = 0; return 0; } if (hj_lee_numero(v->s, n, &nf, 0)) return 0; v_err(v, HJE_VALOR); return -1; }
    default: return -1;
    }
}
static const char *a_txt(struct ectx *e, struct ev *v)           /* texto de un valor (numeros en general, en castellano) */
{
    deref(e, v);
    switch (v->t) {
    case HJ_TXT: return v->s;
    case HJ_VACIA: return "";
    case HJ_BOOL: return f64_es_cero(v->n) ? "FALSO" : "VERDADERO";
    case HJ_NUM: { char b[64]; num_general(v->n, b, sizeof b); uint32_t L = hj_slen(b); char *d = arena(e, L); if (!d) return ""; copia(d, b, L); return d; }
    default: return 0;
    }
}
static int cmp_txt(const char *a, const char *b)
{
    for (;;) { char x = pliega_sig(&a), y = pliega_sig(&b); if (x != y) return (uint8_t)x < (uint8_t)y ? -1 : 1; if (!x) return 0; }
}
/* comparacion de Excel: numeros < textos < logicos; textos sin mayusculas */
static int compara(const struct ev *a, const struct ev *b)
{
    int ra = a->t == HJ_TXT ? 1 : a->t == HJ_BOOL ? 2 : 0, rb = b->t == HJ_TXT ? 1 : b->t == HJ_BOOL ? 2 : 0;
    if (a->t == HJ_VACIA) { if (b->t == HJ_TXT) return b->s[0] ? -1 : 0; if (b->t == HJ_BOOL) return f64_es_cero(b->n) ? 0 : -1; return f64_lt(0, b->n) ? -1 : f64_lt(b->n, 0) ? 1 : 0; }
    if (b->t == HJ_VACIA) return -compara(b, a);
    if (ra != rb) return ra < rb ? -1 : 1;
    if (ra == 1) return cmp_txt(a->s, b->s);
    return f64_lt(a->n, b->n) ? -1 : f64_lt(b->n, a->n) ? 1 : 0;
}

/* ---- recorrer los valores de los argumentos (rangos incluidos) */
typedef int (*visita_fn)(struct ectx *e, struct ev *v, int de_rango, void *u);     /* != 0 para parar */
static void recorta(const struct ectx *e, int h, const struct ref *r, uint32_t *f1, uint32_t *c1, uint32_t *f2, uint32_t *c2)
{
    const struct hj_hoja *s = &e->l->h[h];
    *f1 = r->f1; *c1 = r->c1; *f2 = r->f2; *c2 = r->c2;
    if (*f2 >= s->max_fila) *f2 = s->max_fila ? s->max_fila - 1 : 0;
    if (*c2 >= s->max_col) *c2 = s->max_col ? s->max_col - 1 : 0;
}
static int visita_ref(struct ectx *e, const struct ref *r, visita_fn fn, void *u, int saltar_ocultas)
{
    int h = ref_hoja(e, r); if (h < 0 || h >= e->l->nh) { struct ev v; v_err(&v, HJE_REF); return fn(e, &v, 0, u); }
    struct hj_hoja *s = &e->l->h[h];
    uint32_t f1, c1, f2, c2; recorta(e, h, r, &f1, &c1, &f2, &c2);
    if (!s->max_fila || f1 > f2 || c1 > c2) return 0;
    uint64_t area = (uint64_t)(f2 - f1 + 1) * (c2 - c1 + 1);
    if (area > (uint64_t)s->nc * 2 + 64) {                       /* rango grande y hoja dispersa: por la lista de celdas */
        for (int32_t i = 0; i < s->nc; ++i) {
            struct hj_celda *x = &s->c[i];
            if (x->fila < f1 || x->fila > f2 || x->col < c1 || x->col > c2) continue;
            if (saltar_ocultas && hj_fila_oculta(s, x->fila)) continue;
            struct ev v; celda_val(e, h, x->fila, x->col, &v); if (v.t == HJ_VACIA) continue;
            if (fn(e, &v, 1, u)) return 1;
        }
        return 0;
    }
    for (uint32_t f = f1; f <= f2; ++f) {
        if (saltar_ocultas && hj_fila_oculta(s, f)) continue;
        for (uint32_t c = c1; c <= c2; ++c) { struct ev v; celda_val(e, h, f, c, &v); if (v.t == HJ_VACIA) continue; if (fn(e, &v, 1, u)) return 1; }
    }
    return 0;
}
static int visita(struct ectx *e, struct ev *args, int argc, visita_fn fn, void *u)
{
    for (int i = 0; i < argc; ++i) {
        if (args[i].t == EV_REF) { if (visita_ref(e, &args[i].r, fn, u, 0)) return 1; }
        else if (args[i].t == HJ_ERR && args[i].err == 255) continue;   /* argumento vacio */
        else if (fn(e, &args[i], 0, u)) return 1;
    }
    return 0;
}
struct agg { f64 suma, prod, min, max; uint32_t n, na; int err; int hay; f64 *vals; uint32_t nv, cap; int guardar; int solo_num_rango; };
static int agg_visita(struct ectx *e, struct ev *v, int de_rango, void *u)
{
    (void)e;
    struct agg *a = (struct agg *)u;
    if (v->t == HJ_ERR) { if (!a->err) a->err = v->err; return 1; }
    if (v->t != HJ_VACIA) a->na++;
    f64 x;
    if (v->t == HJ_NUM) x = v->n;
    else if (de_rango) return 0;                                 /* en rangos solo cuentan los numeros */
    else if (v->t == HJ_BOOL) x = v->n;
    else if (v->t == HJ_TXT) { int nf; if (!hj_lee_numero(v->s, &x, &nf, 0)) { if (!a->err) a->err = HJE_VALOR; return 1; } }
    else return 0;
    if (!a->hay) { a->min = a->max = x; a->hay = 1; }
    else { if (f64_lt(x, a->min)) a->min = x; if (f64_lt(a->max, x)) a->max = x; }
    a->suma = f64_add(a->suma, x); a->prod = f64_mul(a->prod, x); a->n++;
    if (a->guardar) {
        if (a->nv >= a->cap) { uint32_t nc = a->cap ? a->cap * 2 : 64; if (nc > 2000000) { a->err = HJE_NUM; return 1; } f64 *nv = (f64 *)hj_alloc(nc * 8u); if (!nv) { a->err = HJE_NUM; return 1; } if (a->vals) { copia(nv, a->vals, a->nv * 8u); hj_free(a->vals); } a->vals = nv; a->cap = nc; }
        a->vals[a->nv++] = x;
    }
    return 0;
}
static void ordena_f64(f64 *v, uint32_t n)
{
    static const uint32_t G[] = { 1750, 701, 301, 132, 57, 23, 10, 4, 1 };
    for (int g = 0; g < 9; ++g) for (uint32_t i = G[g]; i < n; ++i) { f64 t = v[i]; uint32_t j = i; while (j >= G[g] && f64_lt(t, v[j - G[g]])) { v[j] = v[j - G[g]]; j -= G[g]; } v[j] = t; }
}

/* ---- criterios (CONTAR.SI, SUMAR.SI, autofiltro) */
struct crit { int op; int es_num; f64 n; const char *s; int comodin; };
static int comodin_cumple(const char *p, const char *s)          /* * ? ~ sin mayusculas */
{
    const char *ps = 0, *ss = 0;
    while (*s) {
        if (*p == '~' && (p[1] == '*' || p[1] == '?')) { const char *a = p + 1, *b = s; if (pliega_sig(&a) == pliega_sig(&b)) { p += 2; s = b; continue; } }
        else if (*p == '*') { ps = ++p; ss = s; continue; }
        else if (*p == '?') { ++p; if (((uint8_t)*s & 0xC0) == 0xC0) { ++s; while (((uint8_t)*s & 0xC0) == 0x80) ++s; } else ++s; continue; }
        else { const char *a = p, *b = s; if (*a && pliega_sig(&a) == pliega_sig(&b)) { p = a; s = b; continue; } }
        if (ps) { p = ps; if (((uint8_t)*ss & 0xC0) == 0xC0) { ++ss; while (((uint8_t)*ss & 0xC0) == 0x80) ++ss; } else ++ss; s = ss; continue; }
        return 0;
    }
    while (*p == '*') ++p;
    return !*p;
}
static void crit_prepara(struct ectx *e, struct ev *v, struct crit *c)
{
    zero(c, sizeof *c); c->op = OPX_EQ;
    deref(e, v);
    if (v->t == HJ_NUM || v->t == HJ_BOOL) { c->es_num = 1; c->n = v->n; return; }
    const char *s = v->t == HJ_TXT ? v->s : "";
    if (s[0] == '<' && s[1] == '>') { c->op = OPX_NE; s += 2; } else if (s[0] == '<' && s[1] == '=') { c->op = OPX_LE; s += 2; } else if (s[0] == '>' && s[1] == '=') { c->op = OPX_GE; s += 2; }
    else if (s[0] == '<') { c->op = OPX_LT; s += 1; } else if (s[0] == '>') { c->op = OPX_GT; s += 1; } else if (s[0] == '=') { c->op = OPX_EQ; s += 1; }
    int nf; f64 x; if (s[0] && hj_lee_numero(s, &x, &nf, 0)) { c->es_num = 1; c->n = x; }
    c->s = s; for (const char *z = s; *z; ++z) if (*z == '*' || *z == '?') c->comodin = 1;
}
static int crit_cumple(const struct crit *c, const struct ev *v)
{
    if (v->t == HJ_ERR) return 0;
    if (c->es_num) {
        f64 x; int nf;
        if (v->t == HJ_NUM) x = v->n; else if (v->t == HJ_TXT && hj_lee_numero(v->s, &x, &nf, 0) && c->op == OPX_EQ) {} else return c->op == OPX_NE;
        int r = f64_lt(x, c->n) ? -1 : f64_lt(c->n, x) ? 1 : 0;
        switch (c->op) { case OPX_EQ: return r == 0; case OPX_NE: return r != 0; case OPX_LT: return r < 0; case OPX_LE: return r <= 0; case OPX_GT: return r > 0; default: return r >= 0; }
    }
    const char *s = v->t == HJ_TXT ? v->s : v->t == HJ_VACIA ? "" : 0;
    if (!s) { if (v->t == HJ_BOOL) s = f64_es_cero(v->n) ? "FALSO" : "VERDADERO"; else return c->op == OPX_NE && c->s[0]; }
    if (c->op == OPX_EQ) { if (!c->s[0]) return v->t == HJ_VACIA || !s[0]; return c->comodin ? comodin_cumple(c->s, s) : cmp_txt(s, c->s) == 0; }
    if (c->op == OPX_NE) { if (!c->s[0]) return v->t != HJ_VACIA && s[0]; return c->comodin ? !comodin_cumple(c->s, s) : cmp_txt(s, c->s) != 0; }
    if (v->t != HJ_TXT) return 0;
    int r = cmp_txt(s, c->s);
    switch (c->op) { case OPX_LT: return r < 0; case OPX_LE: return r <= 0; case OPX_GT: return r > 0; default: return r >= 0; }
}

/* ---- utilidades de texto (UTF-8) */
static uint32_t u8_cars(const char *s) { uint32_t n = 0; for (; *s; ++s) if (((uint8_t)*s & 0xC0) != 0x80) ++n; return n; }
static const char *u8_salta(const char *s, uint32_t k) { while (*s && k) { ++s; while (((uint8_t)*s & 0xC0) == 0x80) ++s; --k; } return s; }
static void u8_caso(char *s, int mayus, int propio)
{
    int ini = 1;
    for (uint8_t *p = (uint8_t *)s; *p; ++p) {
        int m = propio ? ini : mayus;
        if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) { if (m && p[1] >= 0xA0 && p[1] != 0xB7 && p[1] != 0xBF) p[1] = (uint8_t)(p[1] - 0x20); else if (!m && p[1] >= 0x80 && p[1] < 0xA0 && p[1] != 0x97) p[1] = (uint8_t)(p[1] + 0x20); ++p; ini = 0; continue; }
        if (*p >= 'a' && *p <= 'z' && m) *p = (uint8_t)(*p - 32); else if (*p >= 'A' && *p <= 'Z' && !m) *p = (uint8_t)(*p + 32);
        ini = !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p >= 0x80);
    }
}

/* ---- busquedas */
static void rango_celda(struct ectx *e, const struct ref *r, uint32_t df, uint32_t dc, struct ev *out)
{
    out->t = EV_REF; out->r = *r; out->r.f1 = out->r.f2 = r->f1 + df; out->r.c1 = out->r.c2 = r->c1 + dc; out->r.tipo = 1; (void)e;
}
static int busca_en(struct ectx *e, struct ev *clave, const struct ref *r, int vertical, int modo, uint32_t *pos)
{
    /* modo 0 exacto (con comodines si es texto), 1 el mayor <= (ordenado ascendente), -1 el menor >= (descendente) */
    int h = ref_hoja(e, r); uint32_t f1, c1, f2, c2; recorta(e, h, r, &f1, &c1, &f2, &c2);
    uint32_t n = vertical ? (f2 >= f1 ? f2 - f1 + 1 : 0) : (c2 >= c1 ? c2 - c1 + 1 : 0);
    if (r->tipo == 3 || r->tipo == 4) { if (vertical && r->f2 < f2) n = r->f2 - f1 + 1; }
    struct crit c; int con_comodin = 0;
    if (clave->t == HJ_TXT) { for (const char *z = clave->s; *z; ++z) if (*z == '*' || *z == '?') con_comodin = 1; }
    if (con_comodin && modo == 0) { struct ev k = *clave; k.s = clave->s; crit_prepara(e, &k, &c); c.op = OPX_EQ; c.s = clave->s; c.es_num = 0; c.comodin = 1; }
    int mejor = -1;
    for (uint32_t i = 0; i < n; ++i) {
        struct ev v; celda_val(e, h, vertical ? f1 + i : r->f1, vertical ? r->c1 : c1 + i, &v);
        if (modo == 0) { if (con_comodin ? (v.t == HJ_TXT && crit_cumple(&c, &v)) : (v.t != HJ_VACIA && compara(&v, clave) == 0 && (v.t == HJ_TXT) == (clave->t == HJ_TXT))) { *pos = i; return 1; } continue; }
        if (v.t == HJ_VACIA || (v.t == HJ_TXT) != (clave->t == HJ_TXT)) continue;
        int k = compara(&v, clave);
        if (modo == 1) { if (k <= 0) mejor = (int)i; else break; }
        else { if (k >= 0) mejor = (int)i; else break; }
    }
    if (mejor >= 0) { *pos = (uint32_t)mejor; return 1; }
    return 0;
}

/* ---- fechas */
static void serie_ymd(f64 s, int *y, int *m, int *d) { hj_serie_fecha(s, y, m, d, 0, 0, 0); }
static int dia_sem(f64 s) { int64_t d = f64_to_i64(f64_floor(s)); return (int)(((d - 1) % 7 + 7) % 7); }   /* 0 domingo (serie 1 = 1-1-1900, domingo) */

/* ---- llamada a una funcion: args[0..argc-1] (con referencias), resultado en *r */
struct cuenta_si { struct crit c; uint32_t n; };
static int cs_visita(struct ectx *e, struct ev *v, int de, void *u) { (void)e; (void)de; struct cuenta_si *k = (struct cuenta_si *)u; if (crit_cumple(&k->c, v)) k->n++; return 0; }
struct texto_u { struct ectx *e; char *b; uint32_t n, cap; const char *sep; int ignora; int primero; int err; };
static int tu_visita(struct ectx *e, struct ev *v, int de, void *u)
{
    (void)de; struct texto_u *t = (struct texto_u *)u;
    if (v->t == HJ_ERR) { t->err = v->err; return 1; }
    const char *s = a_txt(e, v); if (!s) { t->err = HJE_VALOR; return 1; }
    if (t->ignora && !s[0]) return 0;
    if (!t->primero && t->sep) put(t->b, t->cap, &t->n, t->sep);
    t->primero = 0; put(t->b, t->cap, &t->n, s);
    return 0;
}
static int fns_si(struct ectx *e, struct ev *args, int argc, int id, struct ev *r);
static int celda_en(struct ectx *e, const struct ref *r, uint32_t df, uint32_t dc, struct ev *v) { int h = ref_hoja(e, r); celda_val(e, h, r->f1 + df, r->c1 + dc, v); return 0; }
static void llama(struct ectx *e, int id, struct ev *a, int argc, struct ev *r)
{
    f64 x, y, z;
#define NUM(i, var) do { if (a_num(e, &a[i], &var)) { *r = a[i]; if (r->t != HJ_ERR) v_err(r, HJE_VALOR); return; } } while (0)
#define OPT(i) (argc > (i) && !(a[i].t == HJ_ERR && a[i].err == 255))
    for (int i = 0; i < argc; ++i) if (a[i].t == HJ_ERR && a[i].err == 255) { /* vacio: se trata como 0 / "" */ }
    switch (id) {
    case FN_SUM: case FN_AVERAGE: case FN_MIN: case FN_MAX: case FN_COUNT: case FN_COUNTA: case FN_PRODUCT:
    case FN_MEDIAN: case FN_MODE: case FN_STDEV: case FN_STDEVP: case FN_VAR: case FN_VARP: {
        struct agg g; zero(&g, sizeof g); g.prod = F(1); g.guardar = id == FN_MEDIAN || id == FN_MODE || id >= FN_STDEV;
        if (id == FN_COUNT || id == FN_COUNTA) {                /* CONTAR no se para en los errores */
            uint32_t n = 0, na = 0;
            for (int i = 0; i < argc; ++i) {
                if (a[i].t == EV_REF) { int h = ref_hoja(e, &a[i].r); uint32_t f1, c1, f2, c2; recorta(e, h, &a[i].r, &f1, &c1, &f2, &c2); struct hj_hoja *s = h >= 0 && h < e->l->nh ? &e->l->h[h] : 0; if (!s) continue;
                    for (int32_t k = 0; k < s->nc; ++k) { struct hj_celda *q = &s->c[k]; if (q->fila < f1 || q->fila > f2 || q->col < c1 || q->col > c2) continue; struct ev v; celda_val(e, h, q->fila, q->col, &v); if (v.t == HJ_VACIA) continue; na++; if (v.t == HJ_NUM) n++; } }
                else if (a[i].t == HJ_ERR && a[i].err == 255) continue;
                else { na++; f64 t; struct ev v = a[i]; if (v.t == HJ_NUM || v.t == HJ_BOOL || (v.t == HJ_TXT && !a_num(e, &v, &t))) n++; }
            }
            v_num(r, F(id == FN_COUNT ? n : na)); return;
        }
        visita(e, a, argc, agg_visita, &g);
        if (g.err) { if (g.vals) hj_free(g.vals); v_err(r, g.err); return; }
        switch (id) {
        case FN_SUM: v_num(r, g.suma); break;
        case FN_AVERAGE: if (!g.n) v_err(r, HJE_DIV0); else v_num(r, f64_div(g.suma, F(g.n))); break;
        case FN_MIN: v_num(r, g.hay ? g.min : 0); break;
        case FN_MAX: v_num(r, g.hay ? g.max : 0); break;
        case FN_PRODUCT: v_num(r, g.n ? g.prod : 0); break;
        case FN_MEDIAN: if (!g.nv) { v_err(r, HJE_NUM); break; } ordena_f64(g.vals, g.nv); v_num(r, g.nv % 2 ? g.vals[g.nv / 2] : f64_div(f64_add(g.vals[g.nv / 2 - 1], g.vals[g.nv / 2]), F(2))); break;
        case FN_MODE: { if (!g.nv) { v_err(r, HJE_NA); break; } uint32_t mejor = 0; f64 mv = 0; for (uint32_t i = 0; i < g.nv; ++i) { uint32_t k = 0; for (uint32_t j = 0; j < g.nv && g.nv <= 20000; ++j) if (f64_eq(g.vals[i], g.vals[j])) ++k; if (k > mejor) { mejor = k; mv = g.vals[i]; } } if (mejor < 2) v_err(r, HJE_NA); else v_num(r, mv); break; }
        default: {                                              /* desviacion y varianza */
            uint32_t n = g.nv; int muestra = id == FN_STDEV || id == FN_VAR;
            if (n < (muestra ? 2u : 1u)) { v_err(r, HJE_DIV0); break; }
            f64 m = f64_div(g.suma, F(n)), s2 = 0; for (uint32_t i = 0; i < n; ++i) { f64 d = f64_sub(g.vals[i], m); s2 = f64_add(s2, f64_mul(d, d)); }
            f64 var = f64_div(s2, F(muestra ? n - 1 : n));
            v_num(r, id == FN_VAR || id == FN_VARP ? var : f64_sqrt(var)); break;
        }
        }
        if (g.vals) hj_free(g.vals);
        return;
    }
    case FN_LARGE: case FN_SMALL: {
        struct agg g; zero(&g, sizeof g); g.prod = F(1); g.guardar = 1;
        visita(e, a, 1, agg_visita, &g); if (g.err) { if (g.vals) hj_free(g.vals); v_err(r, g.err); return; }
        NUM(1, x); int64_t k = f64_to_i64(f64_ceil(x));
        if (k < 1 || (uint64_t)k > g.nv) { if (g.vals) hj_free(g.vals); v_err(r, HJE_NUM); return; }
        ordena_f64(g.vals, g.nv); v_num(r, id == FN_SMALL ? g.vals[k - 1] : g.vals[g.nv - (uint32_t)k]); hj_free(g.vals); return;
    }
    case FN_RANK: {
        NUM(0, x); struct agg g; zero(&g, sizeof g); g.prod = F(1); g.guardar = 1;
        visita(e, a + 1, 1, agg_visita, &g); if (g.err) { if (g.vals) hj_free(g.vals); v_err(r, g.err); return; }
        int asc = 0; if (OPT(2)) { NUM(2, y); asc = !f64_es_cero(y); }
        uint32_t pos = 1; int esta = 0;
        for (uint32_t i = 0; i < g.nv; ++i) { if (f64_eq(g.vals[i], x)) esta = 1; else if (asc ? f64_lt(g.vals[i], x) : f64_lt(x, g.vals[i])) ++pos; }
        if (g.vals) hj_free(g.vals);
        if (!esta) v_err(r, HJE_NA); else v_num(r, F(pos)); return;
    }
    case FN_COUNTBLANK: {
        if (a[0].t != EV_REF) { v_err(r, HJE_VALOR); return; }
        int h = ref_hoja(e, &a[0].r); uint64_t area = (uint64_t)(a[0].r.f2 - a[0].r.f1 + 1) * (a[0].r.c2 - a[0].r.c1 + 1), llenas = 0;
        struct hj_hoja *s = &e->l->h[h];
        for (int32_t k = 0; k < s->nc; ++k) { struct hj_celda *q = &s->c[k]; if (q->fila < a[0].r.f1 || q->fila > a[0].r.f2 || q->col < a[0].r.c1 || q->col > a[0].r.c2) continue; struct ev v; celda_val(e, h, q->fila, q->col, &v); if (v.t != HJ_VACIA && !(v.t == HJ_TXT && !v.s[0])) ++llenas; }
        v_num(r, F((int64_t)(area - llenas))); return;
    }
    case FN_COUNTIF: { if (a[0].t != EV_REF) { v_err(r, HJE_VALOR); return; } struct cuenta_si k; crit_prepara(e, &a[1], &k.c); k.n = 0;
        int h = ref_hoja(e, &a[0].r); uint32_t f1, c1, f2, c2; recorta(e, h, &a[0].r, &f1, &c1, &f2, &c2);
        uint64_t area = (uint64_t)(a[0].r.f2 - a[0].r.f1 + 1) * (a[0].r.c2 - a[0].r.c1 + 1);
        visita_ref(e, &a[0].r, cs_visita, &k, 0);
        if (k.c.op == OPX_EQ && !k.c.es_num && !k.c.s[0]) {     /* "" tambien cuenta las vacias */
            uint32_t llenas = 0; struct hj_hoja *s = &e->l->h[h]; for (int32_t q = 0; q < s->nc; ++q) { struct hj_celda *z = &s->c[q]; if (z->fila >= a[0].r.f1 && z->fila <= a[0].r.f2 && z->col >= a[0].r.c1 && z->col <= a[0].r.c2 && (z->t != HJ_VACIA || z->es_form)) ++llenas; }
            k.n += (uint32_t)(area - llenas);
        }
        (void)f1; (void)c1; (void)f2; (void)c2;
        v_num(r, F(k.n)); return; }
    case FN_SUMIF: case FN_AVERAGEIF: {
        if (a[0].t != EV_REF || (OPT(2) && a[2].t != EV_REF)) { v_err(r, HJE_VALOR); return; }
        struct crit c; crit_prepara(e, &a[1], &c);
        const struct ref *rs = OPT(2) ? &a[2].r : &a[0].r; int hc = ref_hoja(e, &a[0].r), hs = ref_hoja(e, rs);
        uint32_t f1, c1, f2, c2; recorta(e, hc, &a[0].r, &f1, &c1, &f2, &c2);
        f64 s = 0; uint32_t n = 0;
        for (uint32_t f = f1; f <= f2 && f1 <= f2; ++f) for (uint32_t cc = c1; cc <= c2; ++cc) {
            struct ev v; celda_val(e, hc, f, cc, &v); if (!crit_cumple(&c, &v)) continue;
            struct ev w; celda_val(e, hs, rs->f1 + (f - a[0].r.f1), rs->c1 + (cc - a[0].r.c1), &w);
            if (w.t == HJ_ERR) { v_err(r, w.err); return; }
            if (w.t == HJ_NUM) { s = f64_add(s, w.n); ++n; }
        }
        if (id == FN_SUMIF) v_num(r, s); else if (!n) v_err(r, HJE_DIV0); else v_num(r, f64_div(s, F(n)));
        return;
    }
    case FN_COUNTIFS: case FN_SUMIFS: case FN_AVERAGEIFS: case FN_MAXIFS: case FN_MINIFS: { fns_si(e, a, argc, id, r); return; }
    case FN_SUBTOTAL: {
        NUM(0, x); int k = (int)f64_to_i64(x); if (k > 100) k -= 100;
        static const uint8_t M[12] = { 0, FN_AVERAGE, FN_COUNT, FN_COUNTA, FN_MAX, FN_MIN, FN_PRODUCT, FN_STDEV, FN_STDEVP, FN_SUM, FN_VAR, FN_VARP };
        if (k < 1 || k > 11) { v_err(r, HJE_VALOR); return; }
        struct agg g; zero(&g, sizeof g); g.prod = F(1); g.guardar = 1;
        for (int i = 1; i < argc; ++i) if (a[i].t == EV_REF) visita_ref(e, &a[i].r, agg_visita, &g, 1);
        if (g.err) { if (g.vals) hj_free(g.vals); v_err(r, g.err); return; }
        int fid = M[k];
        if (fid == FN_COUNTA) { v_num(r, F(g.na)); }
        else if (fid == FN_COUNT) v_num(r, F(g.n));
        else if (fid == FN_SUM) v_num(r, g.suma);
        else if (fid == FN_AVERAGE) { if (!g.n) v_err(r, HJE_DIV0); else v_num(r, f64_div(g.suma, F(g.n))); }
        else if (fid == FN_MAX) v_num(r, g.hay ? g.max : 0); else if (fid == FN_MIN) v_num(r, g.hay ? g.min : 0);
        else if (fid == FN_PRODUCT) v_num(r, g.n ? g.prod : 0);
        else { uint32_t n = g.nv; int muestra = fid == FN_STDEV || fid == FN_VAR; if (n < (muestra ? 2u : 1u)) v_err(r, HJE_DIV0);
            else { f64 m = f64_div(g.suma, F(n)), s2 = 0; for (uint32_t i = 0; i < n; ++i) { f64 d = f64_sub(g.vals[i], m); s2 = f64_add(s2, f64_mul(d, d)); } f64 var = f64_div(s2, F(muestra ? n - 1 : n)); v_num(r, fid == FN_VAR || fid == FN_VARP ? var : f64_sqrt(var)); } }
        if (g.vals) hj_free(g.vals);
        return;
    }
    case FN_SUMPRODUCT: {
        if (a[0].t != EV_REF) { NUM(0, x); f64 p = x; for (int i = 1; i < argc; ++i) { NUM(i, y); p = f64_mul(p, y); } v_num(r, p); return; }
        uint32_t nf = a[0].r.f2 - a[0].r.f1 + 1, nc = a[0].r.c2 - a[0].r.c1 + 1;
        for (int i = 1; i < argc; ++i) if (a[i].t != EV_REF || a[i].r.f2 - a[i].r.f1 + 1 != nf || a[i].r.c2 - a[i].r.c1 + 1 != nc) { v_err(r, HJE_VALOR); return; }
        { int h0 = ref_hoja(e, &a[0].r); if (nf > e->l->h[h0].max_fila) nf = e->l->h[h0].max_fila; }
        f64 s = 0;
        for (uint32_t f = 0; f < nf; ++f) for (uint32_t c = 0; c < nc; ++c) {
            f64 p = F(1);
            for (int i = 0; i < argc; ++i) { struct ev v; celda_en(e, &a[i].r, f, c, &v); if (v.t == HJ_ERR) { v_err(r, v.err); return; } p = f64_mul(p, v.t == HJ_NUM ? v.n : 0); }
            s = f64_add(s, p);
        }
        v_num(r, s); return;
    }
    case FN_ROUND: case FN_ROUNDUP: case FN_ROUNDDOWN: case FN_TRUNC: {
        NUM(0, x); int64_t d = 0; if (OPT(1)) { NUM(1, y); d = f64_to_i64(f64_trunc(y)); }
        if (d > 15) d = 15;
        if (d < -15) d = -15;
        f64 p = f64_pow(F(10), F(d < 0 ? -d : d)), a2 = f64_abs(x), m = d >= 0 ? f64_mul(a2, p) : f64_div(a2, p), q;
        if (id == FN_ROUND) q = f64_floor(f64_add(f64_add(m, 0x3FE0000000000000ULL), 0x3E112E0BE826D695ULL));
        else if (id == FN_ROUNDUP) q = f64_ceil(f64_sub(m, 0x3E112E0BE826D695ULL));
        else q = f64_floor(f64_add(m, 0x3E112E0BE826D695ULL));
        q = d >= 0 ? f64_div(q, p) : f64_mul(q, p);
        v_num(r, f64_lt(x, 0) ? f64_neg(q) : q); return;
    }
    case FN_INT: NUM(0, x); v_num(r, f64_floor(x)); return;
    case FN_ABS: NUM(0, x); v_num(r, f64_abs(x)); return;
    case FN_SQRT: NUM(0, x); if (f64_lt(x, 0)) v_err(r, HJE_NUM); else v_num(r, f64_sqrt(x)); return;
    case FN_POWER: NUM(0, x); NUM(1, y); if (f64_es_cero(x) && f64_lt(y, 0)) v_err(r, HJE_DIV0); else v_num(r, f64_pow(x, y)); return;
    case FN_MOD: NUM(0, x); NUM(1, y); if (f64_es_cero(y)) { v_err(r, HJE_DIV0); return; } z = f64_sub(x, f64_mul(y, f64_floor(f64_div(x, y)))); v_num(r, z); return;
    case FN_PI: v_num(r, F64_PI); return;
    case FN_RAND: v_num(r, f64_div(F(rng() & 0xFFFFFF), F(0x1000000))); return;
    case FN_RANDBETWEEN: { NUM(0, x); NUM(1, y); int64_t lo = f64_to_i64(f64_ceil(x)), hi = f64_to_i64(f64_floor(y)); if (hi < lo) { v_err(r, HJE_NUM); return; } v_num(r, F(lo + (int64_t)(rng() % (uint32_t)(hi - lo + 1 > 0xFFFFFFFF ? 0xFFFFFFFF : hi - lo + 1)))); return; }
    case FN_EXP: NUM(0, x); v_num(r, f64_exp(x)); return;
    case FN_LN: NUM(0, x); if (!f64_lt(0, x)) v_err(r, HJE_NUM); else v_num(r, f64_log(x)); return;
    case FN_LOG10: NUM(0, x); if (!f64_lt(0, x)) v_err(r, HJE_NUM); else v_num(r, f64_div(f64_log(x), f64_log(F(10)))); return;
    case FN_LOG: NUM(0, x); y = F(10); if (OPT(1)) NUM(1, y); if (!f64_lt(0, x) || !f64_lt(0, y) || f64_eq(y, F(1))) v_err(r, HJE_NUM); else v_num(r, f64_div(f64_log(x), f64_log(y))); return;
    case FN_SIGN: NUM(0, x); v_num(r, f64_lt(x, 0) ? F(-1) : f64_lt(0, x) ? F(1) : 0); return;
    case FN_CEILING: case FN_FLOOR: NUM(0, x); y = F(1); if (OPT(1)) NUM(1, y); if (f64_es_cero(y)) { v_num(r, 0); return; } z = f64_div(x, y); z = id == FN_CEILING ? f64_ceil(f64_sub(z, 0x3E112E0BE826D695ULL)) : f64_floor(f64_add(z, 0x3E112E0BE826D695ULL)); v_num(r, f64_mul(z, y)); return;
    case FN_SIN: NUM(0, x); v_num(r, f64_sin(x)); return;
    case FN_COS: NUM(0, x); v_num(r, f64_cos(x)); return;
    case FN_TAN: NUM(0, x); v_num(r, f64_tan(x)); return;
    case FN_DEGREES: NUM(0, x); v_num(r, f64_div(f64_mul(x, F(180)), F64_PI)); return;
    case FN_RADIANS: NUM(0, x); v_num(r, f64_div(f64_mul(x, F64_PI), F(180))); return;
    case FN_AND: case FN_OR: {
        int hay = 0, res = id == FN_AND;
        for (int i = 0; i < argc; ++i) {
            if (a[i].t == EV_REF) { int h = ref_hoja(e, &a[i].r); uint32_t f1, c1, f2, c2; recorta(e, h, &a[i].r, &f1, &c1, &f2, &c2);
                for (uint32_t f = f1; f <= f2 && f1 <= f2 && e->l->h[h].max_fila; ++f) for (uint32_t c = c1; c <= c2; ++c) { struct ev v; celda_val(e, h, f, c, &v); if (v.t == HJ_ERR) { v_err(r, v.err); return; } if (v.t == HJ_NUM || v.t == HJ_BOOL) { hay = 1; int b = !f64_es_cero(v.n); if (id == FN_AND) res &= b; else res |= b; } }
                continue; }
            if (a[i].t == HJ_ERR) { v_err(r, a[i].err == 255 ? HJE_VALOR : a[i].err); return; }
            NUM(i, x); hay = 1; int b = !f64_es_cero(x); if (id == FN_AND) res &= b; else res |= b;
        }
        if (!hay) v_err(r, HJE_VALOR); else v_bool(r, res); return;
    }
    case FN_NOT: NUM(0, x); v_bool(r, f64_es_cero(x)); return;
    case FN_IFS: {
        for (int i = 0; i + 1 < argc; i += 2) { NUM(i, x); if (!f64_es_cero(x)) { *r = a[i + 1]; deref(e, r); return; } }
        v_err(r, HJE_NA); return;
    }
    case FN_TRUE: v_bool(r, 1); return;
    case FN_FALSE: v_bool(r, 0); return;
    case FN_ISBLANK: deref(e, &a[0]); v_bool(r, a[0].t == HJ_VACIA); return;
    case FN_ISNUMBER: deref(e, &a[0]); v_bool(r, a[0].t == HJ_NUM); return;
    case FN_ISTEXT: deref(e, &a[0]); v_bool(r, a[0].t == HJ_TXT); return;
    case FN_ISERROR: deref(e, &a[0]); v_bool(r, a[0].t == HJ_ERR); return;
    case FN_ISNA: deref(e, &a[0]); v_bool(r, a[0].t == HJ_ERR && a[0].err == HJE_NA); return;
    case FN_ISLOGICAL: deref(e, &a[0]); v_bool(r, a[0].t == HJ_BOOL); return;
    case FN_NA: v_err(r, HJE_NA); return;
    case FN_CHOOSE: { NUM(0, x); int64_t k = f64_to_i64(f64_trunc(x)); if (k < 1 || k >= argc) { v_err(r, HJE_VALOR); return; } *r = a[k]; if (r->t != EV_REF) deref(e, r); return; }
    case FN_VLOOKUP: case FN_HLOOKUP: {
        deref(e, &a[0]); if (a[0].t == HJ_ERR) { *r = a[0]; return; }
        if (a[1].t != EV_REF) { v_err(r, HJE_VALOR); return; }
        NUM(2, x); int64_t k = f64_to_i64(f64_trunc(x)); int aprox = 1; if (OPT(3)) { NUM(3, y); aprox = !f64_es_cero(y); }
        const struct ref *t = &a[1].r; int64_t ancho = id == FN_VLOOKUP ? (int64_t)t->c2 - t->c1 + 1 : (int64_t)t->f2 - t->f1 + 1;
        if (k < 1) { v_err(r, HJE_VALOR); return; }
        if (k > ancho) { v_err(r, HJE_REF); return; }
        uint32_t pos; if (!busca_en(e, &a[0], t, id == FN_VLOOKUP, aprox ? 1 : 0, &pos)) { v_err(r, HJE_NA); return; }
        celda_en(e, t, id == FN_VLOOKUP ? pos : (uint32_t)(k - 1), id == FN_VLOOKUP ? (uint32_t)(k - 1) : pos, r);
        if (r->t == HJ_VACIA) v_num(r, 0);
        return;
    }
    case FN_XLOOKUP: {
        deref(e, &a[0]); if (a[1].t != EV_REF || a[2].t != EV_REF) { v_err(r, HJE_VALOR); return; }
        int vertical = a[1].r.c1 == a[1].r.c2 && a[1].r.f1 != a[1].r.f2; if (a[1].r.f1 == a[1].r.f2 && a[1].r.c1 == a[1].r.c2) vertical = 1;
        int modo = 0; if (OPT(4)) { NUM(4, x); int64_t m = f64_to_i64(x); modo = m == -1 ? -2 : m == 1 ? 2 : 0; }
        uint32_t pos;
        int ok = 0;
        if (modo == 0) ok = busca_en(e, &a[0], &a[1].r, vertical, 0, &pos);
        else {                                                   /* -1: el siguiente menor; 1: el siguiente mayor (sin pedir orden) */
            int h = ref_hoja(e, &a[1].r); uint32_t n = vertical ? a[1].r.f2 - a[1].r.f1 + 1 : a[1].r.c2 - a[1].r.c1 + 1; if (vertical && n > e->l->h[h].max_fila) n = e->l->h[h].max_fila;
            struct ev mejor; int mi = -1;
            for (uint32_t i = 0; i < n; ++i) { struct ev v; celda_val(e, h, vertical ? a[1].r.f1 + i : a[1].r.f1, vertical ? a[1].r.c1 : a[1].r.c1 + i, &v); if (v.t == HJ_VACIA || (v.t == HJ_TXT) != (a[0].t == HJ_TXT)) continue;
                int k = compara(&v, &a[0]); if (k == 0) { mi = (int)i; break; }
                if ((modo == -2 && k < 0 && (mi < 0 || compara(&v, &mejor) > 0)) || (modo == 2 && k > 0 && (mi < 0 || compara(&v, &mejor) < 0))) { mi = (int)i; mejor = v; } }
            if (mi >= 0) { pos = (uint32_t)mi; ok = 1; }
        }
        if (!ok) { if (OPT(3)) { *r = a[3]; deref(e, r); } else v_err(r, HJE_NA); return; }
        celda_en(e, &a[2].r, vertical ? pos : 0, vertical ? 0 : pos, r); if (r->t == HJ_VACIA) v_num(r, 0);
        return;
    }
    case FN_INDEX: {
        if (a[0].t != EV_REF) { v_err(r, HJE_VALOR); return; }
        NUM(1, x); int64_t fi = f64_to_i64(f64_trunc(x)), co = 1; if (OPT(2)) { NUM(2, y); co = f64_to_i64(f64_trunc(y)); }
        const struct ref *t = &a[0].r; int64_t nf = (int64_t)t->f2 - t->f1 + 1, nc = (int64_t)t->c2 - t->c1 + 1;
        if (nf == 1 && argc == 2) { co = fi; fi = 1; }
        if (fi < 0 || co < 0 || fi > nf || co > nc) { v_err(r, HJE_REF); return; }
        if (fi == 0 || co == 0) { v_err(r, HJE_VALOR); return; }
        rango_celda(e, t, (uint32_t)(fi - 1), (uint32_t)(co - 1), r); return;   /* una referencia: vale como valor y como rango */
    }
    case FN_MATCH: {
        deref(e, &a[0]); if (a[1].t != EV_REF) { v_err(r, HJE_NA); return; }
        int tipo = 1; if (OPT(2)) { NUM(2, x); int64_t t = f64_to_i64(x); tipo = t > 0 ? 1 : t < 0 ? -1 : 0; }
        int vertical = a[1].r.f1 != a[1].r.f2 || a[1].r.c1 == a[1].r.c2;
        uint32_t pos; if (!busca_en(e, &a[0], &a[1].r, vertical, tipo, &pos)) v_err(r, HJE_NA); else v_num(r, F((int64_t)pos + 1));
        return;
    }
    case FN_ROW: if (argc && a[0].t == EV_REF) v_num(r, F((int64_t)a[0].r.f1 + 1)); else if (argc && a[0].t != HJ_ERR) v_err(r, HJE_VALOR); else v_num(r, F((int64_t)e->fila + 1)); return;
    case FN_COLUMN: if (argc && a[0].t == EV_REF) v_num(r, F((int64_t)a[0].r.c1 + 1)); else if (argc && a[0].t != HJ_ERR) v_err(r, HJE_VALOR); else v_num(r, F((int64_t)e->col + 1)); return;
    case FN_ROWS: if (a[0].t == EV_REF) v_num(r, F((int64_t)a[0].r.f2 - a[0].r.f1 + 1)); else v_num(r, F(1)); return;
    case FN_COLUMNS: if (a[0].t == EV_REF) v_num(r, F((int64_t)a[0].r.c2 - a[0].r.c1 + 1)); else v_num(r, F(1)); return;
    case FN_CONCATENATE: case FN_CONCAT: case FN_TEXTJOIN: {
        struct texto_u t; zero(&t, sizeof t); t.cap = 32768; t.b = arena(e, t.cap); t.primero = 1; if (!t.b) { v_err(r, HJE_VALOR); return; }
        t.b[0] = 0; int ini = 0;
        if (id == FN_TEXTJOIN) { const char *s = a_txt(e, &a[0]); if (!s) { *r = a[0]; return; } t.sep = s; NUM(1, x); t.ignora = !f64_es_cero(x); ini = 2; }
        for (int i = ini; i < argc; ++i) {
            if (a[i].t == EV_REF && id != FN_CONCATENATE) visita_ref(e, &a[i].r, tu_visita, &t, 0);
            else { if (a[i].t == HJ_ERR && a[i].err == 255) { struct ev v; v_vacia(&v); tu_visita(e, &v, 0, &t); } else tu_visita(e, &a[i], 0, &t); }
            if (t.err) { v_err(r, t.err); return; }
        }
        r->t = HJ_TXT; r->s = t.b; r->err = 0; return;
    }
    case FN_LEFT: case FN_RIGHT: {
        const char *s = a_txt(e, &a[0]); if (!s) { *r = a[0]; return; }
        int64_t k = 1; if (OPT(1)) { NUM(1, x); k = f64_to_i64(f64_trunc(x)); }
        if (k < 0) { v_err(r, HJE_VALOR); return; }
        uint32_t n = u8_cars(s); if ((uint64_t)k > n) k = n;
        if (id == FN_LEFT) { const char *f = u8_salta(s, (uint32_t)k); v_txt(e, r, s, (uint32_t)(f - s)); }
        else { const char *i0 = u8_salta(s, n - (uint32_t)k); v_txt(e, r, i0, hj_slen(i0)); }
        return;
    }
    case FN_MID: {
        const char *s = a_txt(e, &a[0]); if (!s) { *r = a[0]; return; }
        NUM(1, x); NUM(2, y); int64_t i0 = f64_to_i64(f64_trunc(x)), k = f64_to_i64(f64_trunc(y));
        if (i0 < 1 || k < 0) { v_err(r, HJE_VALOR); return; }
        const char *p = u8_salta(s, (uint32_t)(i0 - 1 > 0xFFFFFFF ? 0xFFFFFFF : i0 - 1)); const char *q = u8_salta(p, (uint32_t)(k > 0xFFFFFFF ? 0xFFFFFFF : k));
        v_txt(e, r, p, (uint32_t)(q - p)); return;
    }
    case FN_LEN: { const char *s = a_txt(e, &a[0]); if (!s) { *r = a[0]; return; } v_num(r, F(u8_cars(s))); return; }
    case FN_UPPER: case FN_LOWER: case FN_PROPER: { const char *s = a_txt(e, &a[0]); if (!s) { *r = a[0]; return; } v_txt(e, r, s, hj_slen(s)); if (r->t == HJ_TXT) u8_caso((char *)r->s, id == FN_UPPER, id == FN_PROPER); return; }
    case FN_TRIM: {
        const char *s = a_txt(e, &a[0]); if (!s) { *r = a[0]; return; }
        uint32_t n = hj_slen(s); char *d = arena(e, n); if (!d) { v_err(r, HJE_VALOR); return; }
        uint32_t w = 0; int esp = 0; for (const char *z = s; *z; ++z) { if (*z == ' ') { esp = 1; continue; } if (esp && w) d[w++] = ' '; esp = 0; d[w++] = *z; }
        d[w] = 0; r->t = HJ_TXT; r->s = d; r->err = 0; return;
    }
    case FN_SUBSTITUTE: {
        const char *s = a_txt(e, &a[0]), *v = a_txt(e, &a[1]), *nn = a_txt(e, &a[2]);
        if (!s || !v || !nn) { v_err(r, HJE_VALOR); return; }
        int64_t cual = 0; if (OPT(3)) { NUM(3, x); cual = f64_to_i64(f64_trunc(x)); if (cual < 1) { v_err(r, HJE_VALOR); return; } }
        uint32_t lv = hj_slen(v), ln = hj_slen(nn), cap = 32768; char *d = arena(e, cap); if (!d) { v_err(r, HJE_VALOR); return; }
        uint32_t p = 0; int64_t k = 0;
        for (const char *z = s; *z; ) {
            if (lv && igual_mem(z, v, lv) && (++k == cual || !cual)) { for (uint32_t i = 0; i < ln && p + 1 < cap; ++i) d[p++] = nn[i]; z += lv; continue; }
            if (lv && igual_mem(z, v, lv)) { for (uint32_t i = 0; i < lv && p + 1 < cap; ++i) d[p++] = z[i]; z += lv; continue; }
            if (p + 1 < cap) d[p++] = *z;
            ++z;
        }
        d[p] = 0; r->t = HJ_TXT; r->s = d; r->err = 0; return;
    }
    case FN_REPLACE: {
        const char *s = a_txt(e, &a[0]); if (!s) { *r = a[0]; return; }
        NUM(1, x); NUM(2, y); const char *nn = a_txt(e, &a[3]); if (!nn) { v_err(r, HJE_VALOR); return; }
        int64_t i0 = f64_to_i64(f64_trunc(x)), k = f64_to_i64(f64_trunc(y)); if (i0 < 1 || k < 0) { v_err(r, HJE_VALOR); return; }
        const char *p = u8_salta(s, (uint32_t)(i0 - 1 > 0xFFFFFFF ? 0xFFFFFFF : i0 - 1)), *q = u8_salta(p, (uint32_t)(k > 0xFFFFFFF ? 0xFFFFFFF : k));
        uint32_t L = (uint32_t)(p - s) + hj_slen(nn) + hj_slen(q); char *d = arena(e, L); if (!d) { v_err(r, HJE_VALOR); return; }
        uint32_t w = 0; copia(d, s, (uint32_t)(p - s)); w = (uint32_t)(p - s); copia(d + w, nn, hj_slen(nn)); w += hj_slen(nn); copia(d + w, q, hj_slen(q)); w += hj_slen(q); d[w] = 0;
        r->t = HJ_TXT; r->s = d; r->err = 0; return;
    }
    case FN_FIND: case FN_SEARCH: {
        const char *q = a_txt(e, &a[0]), *s = a_txt(e, &a[1]); if (!q || !s) { v_err(r, HJE_VALOR); return; }
        int64_t i0 = 1; if (OPT(2)) { NUM(2, x); i0 = f64_to_i64(f64_trunc(x)); }
        uint32_t ns = u8_cars(s); if (i0 < 1 || (uint64_t)i0 > ns + 1) { v_err(r, HJE_VALOR); return; }
        const char *p = u8_salta(s, (uint32_t)(i0 - 1)); uint32_t pos = (uint32_t)i0;
        for (; *p; ++pos, p = u8_salta(p, 1)) {
            if (id == FN_FIND) { uint32_t lq = hj_slen(q); if (igual_mem(p, q, lq) && hj_slen(p) >= lq) { v_num(r, F(pos)); return; } }
            else { char pat[512]; uint32_t k = 0; for (const char *z = q; *z && k + 2 < sizeof pat; ++z) pat[k++] = *z; pat[k++] = '*'; pat[k] = 0; if (comodin_cumple(pat, p)) { v_num(r, F(pos)); return; } }
        }
        if (!*q) { v_num(r, F(i0)); return; }
        v_err(r, HJE_VALOR); return;
    }
    case FN_TEXT: {
        deref(e, &a[0]); if (a[0].t == HJ_ERR) { *r = a[0]; return; }
        const char *cod = a_txt(e, &a[1]); if (!cod) { v_err(r, HJE_VALOR); return; }
        f64 v; if (a[0].t == HJ_TXT) { int nf; if (!hj_lee_numero(a[0].s, &v, &nf, 0)) { *r = a[0]; return; } } else v = a[0].n;
        struct hj_estilo est; zero(&est, sizeof est); est.nf = NF_CODIGO;
        /* el codigo en castellano (0,00 y #.##0) al de Excel */
        uint32_t k = 0; int hay_punto = 0; for (const char *z = cod; *z; ++z) if (*z == '.') hay_punto = 1;
        int coma_decimal = 0; for (const char *z = cod; *z; ++z) if (*z == ',' && (z[1] == '0' || z[1] == '#')) coma_decimal = 1;
        int anos = 0; for (const char *z = cod; *z; ++z) if (baja(z[0]) == 'a' && baja(z[1]) == 'a') anos = 1;   /* "aaaa" = anos (castellano) */
        for (const char *z = cod; *z && k + 1 < HJ_NFCOD; ++z) { char ch = *z; if (coma_decimal || hay_punto) { if (ch == ',' && coma_decimal) ch = '.'; else if (ch == '.' && coma_decimal) ch = ','; } if (anos && baja(ch) == 'a' && !(baja(z[1]) == 'm' && z[2] == '/')) ch = 'y'; est.codigo[k++] = ch; }
        est.codigo[k] = 0;
        struct hj_valor hv = { HJ_NUM, 0, v, "" }; char out[256];
        uint32_t id2 = hj_estilo_id(e->l, &est);                 /* (como mucho uno nuevo por codigo distinto) */
        hj_formatea(e->l, (uint16_t)id2, &hv, out, sizeof out, 0);
        { uint8_t nf, dc, mi; codigo_a_tipo(est.codigo, &nf, &dc, &mi); if (nf == NF_PORCENTAJE) { uint32_t L = hj_slen(out); if (L > 2 && out[L - 2] == ' ') { out[L - 2] = '%'; out[L - 1] = 0; } } if (nf == NF_MONEDA) { /* deja el formato como esta */ } }
        v_txt(e, r, out, hj_slen(out)); return;
    }
    case FN_VALUE: { deref(e, &a[0]); if (a[0].t == HJ_NUM) { *r = a[0]; return; } const char *s = a_txt(e, &a[0]); f64 v; int nf; if (!s || !hj_lee_numero(s, &v, &nf, 0)) { f64 w; if (s && parse_punto(s, &w)) { v_num(r, w); return; } v_err(r, HJE_VALOR); return; } v_num(r, v); return; }
    case FN_REPT: { const char *s = a_txt(e, &a[0]); if (!s) { *r = a[0]; return; } NUM(1, x); int64_t k = f64_to_i64(f64_trunc(x)); uint32_t L = hj_slen(s); if (k < 0 || (uint64_t)k * L > 32767) { v_err(r, HJE_VALOR); return; } char *d = arena(e, (uint32_t)k * L); if (!d) { v_err(r, HJE_VALOR); return; } for (int64_t i = 0; i < k; ++i) copia(d + i * L, s, L); d[k * L] = 0; r->t = HJ_TXT; r->s = d; r->err = 0; return; }
    case FN_EXACT: { const char *s = a_txt(e, &a[0]), *t = a_txt(e, &a[1]); if (!s || !t) { v_err(r, HJE_VALOR); return; } v_bool(r, hj_slen(s) == hj_slen(t) && igual_mem(s, t, hj_slen(s))); return; }
    case FN_CHAR: { NUM(0, x); int64_t k = f64_to_i64(x); if (k < 1 || k > 255) { v_err(r, HJE_VALOR); return; } char b[3]; uint32_t n = 0; if (k < 128) b[n++] = (char)k; else { b[n++] = (char)(0xC0 | (k >> 6)); b[n++] = (char)(0x80 | (k & 63)); } v_txt(e, r, b, n); return; }
    case FN_CODE: { const char *s = a_txt(e, &a[0]); if (!s || !s[0]) { v_err(r, HJE_VALOR); return; } uint8_t c = (uint8_t)s[0]; int64_t k = c; if (c >= 0xC0 && c < 0xE0 && s[1]) k = ((c & 31) << 6) | ((uint8_t)s[1] & 63); v_num(r, F(k)); return; }
    case FN_TODAY: v_num(r, hoy_serie(0)); return;
    case FN_NOW: v_num(r, hoy_serie(1)); return;
    case FN_DATE: { NUM(0, x); NUM(1, y); NUM(2, z); int64_t yy = f64_to_i64(f64_trunc(x)); if (yy < 1900 && yy >= 0) yy += 1900; if (yy < 1900 || yy > 9999) { v_err(r, HJE_NUM); return; } v_num(r, hj_fecha_serie((int)yy, (int)f64_to_i64(f64_trunc(y)), (int)f64_to_i64(f64_trunc(z)))); return; }
    case FN_YEAR: case FN_MONTH: case FN_DAY: { NUM(0, x); if (f64_lt(x, 0)) { v_err(r, HJE_NUM); return; } int yy, m, d; serie_ymd(x, &yy, &m, &d); v_num(r, F(id == FN_YEAR ? yy : id == FN_MONTH ? m : d)); return; }
    case FN_HOUR: case FN_MINUTE: case FN_SECOND: { NUM(0, x); if (f64_lt(x, 0)) { v_err(r, HJE_NUM); return; } int yy, m, d, hh, mi, ss; hj_serie_fecha(x, &yy, &m, &d, &hh, &mi, &ss); v_num(r, F(id == FN_HOUR ? hh : id == FN_MINUTE ? mi : ss)); return; }
    case FN_WEEKDAY: { NUM(0, x); int t = 1; if (OPT(1)) { NUM(1, y); t = (int)f64_to_i64(y); } int d = dia_sem(x); v_num(r, F(t == 2 ? (d + 6) % 7 + 1 : t == 3 ? (d + 6) % 7 : d + 1)); return; }
    case FN_DAYS: NUM(0, x); NUM(1, y); v_num(r, f64_sub(f64_floor(x), f64_floor(y))); return;
    case FN_EDATE: case FN_EOMONTH: {
        NUM(0, x); NUM(1, y); int yy, m, d; serie_ymd(x, &yy, &m, &d); int64_t mm = (int64_t)yy * 12 + (m - 1) + f64_to_i64(f64_trunc(y));
        int ny = (int)(mm / 12), nm = (int)(mm % 12) + 1; if (ny < 1900 || ny > 9999) { v_err(r, HJE_NUM); return; }
        int dm = dias_mes(ny, nm); v_num(r, hj_fecha_serie(ny, nm, id == FN_EOMONTH ? dm : (d > dm ? dm : d))); return;
    }
    case FN_NETWORKDAYS: {
        NUM(0, x); NUM(1, y); int64_t a0 = f64_to_i64(f64_floor(x)), b0 = f64_to_i64(f64_floor(y)), s = 1; if (b0 < a0) { int64_t t = a0; a0 = b0; b0 = t; s = -1; }
        if (b0 - a0 > 400000) { v_err(r, HJE_NUM); return; }
        int64_t n = 0;
        for (int64_t d = a0; d <= b0; ++d) {
            int w = (int)(((d - 1) % 7 + 7) % 7); if (w == 0 || w == 6) continue;
            int fest = 0;
            if (OPT(2) && a[2].t == EV_REF) { int h = ref_hoja(e, &a[2].r); uint32_t f1, c1, f2, c2; recorta(e, h, &a[2].r, &f1, &c1, &f2, &c2); for (uint32_t f = f1; f <= f2 && !fest && f1 <= f2; ++f) for (uint32_t c = c1; c <= c2; ++c) { struct ev v; celda_val(e, h, f, c, &v); if (v.t == HJ_NUM && f64_to_i64(f64_floor(v.n)) == d) fest = 1; } }
            if (!fest) ++n;
        }
        v_num(r, F(n * s)); return;
    }
    case FN_DATEDIF: {
        NUM(0, x); NUM(1, y); const char *u = a_txt(e, &a[2]); if (!u) { v_err(r, HJE_VALOR); return; }
        if (f64_lt(y, x)) { v_err(r, HJE_NUM); return; }
        int y1, m1, d1, y2, m2, d2; serie_ymd(x, &y1, &m1, &d1); serie_ymd(y, &y2, &m2, &d2);
        int64_t meses = (int64_t)(y2 - y1) * 12 + (m2 - m1) - (d2 < d1 ? 1 : 0);
        char U = sube(u[0]), U2 = sube(u[1]);
        if (U == 'Y' && !U2) v_num(r, F(meses / 12));
        else if (U == 'M' && !U2) v_num(r, F(meses));
        else if (U == 'D' && !U2) v_num(r, f64_sub(f64_floor(y), f64_floor(x)));
        else if (U == 'M' && U2 == 'D') { int pm = m2 - 1, py = y2; if (pm < 1) { pm = 12; py--; } int dmp = dias_mes(py, pm), d1c = d1 < dmp ? d1 : dmp; int64_t dd = d2 - d1c; if (dd < 0) dd += dmp; v_num(r, F(dd)); }
        else if (U == 'Y' && U2 == 'M') v_num(r, F(meses % 12));
        else if (U == 'Y' && U2 == 'D') { f64 ini = hj_fecha_serie(y2, m1, d1); if (f64_lt(y, ini)) ini = hj_fecha_serie(y2 - 1, m1, d1); v_num(r, f64_sub(f64_floor(y), ini)); }
        else v_err(r, HJE_NUM);
        return;
    }
    case FN_WEEKNUM: { NUM(0, x); int t = 1; if (OPT(1)) { NUM(1, y); t = (int)f64_to_i64(y); } int yy, m, d; serie_ymd(x, &yy, &m, &d); f64 ene1 = hj_fecha_serie(yy, 1, 1); int w0 = dia_sem(ene1); if (t == 2) w0 = (w0 + 6) % 7; int64_t dif = f64_to_i64(f64_sub(f64_floor(x), ene1)); v_num(r, F((dif + w0) / 7 + 1)); return; }
    case FN_TIME: { NUM(0, x); NUM(1, y); NUM(2, z); int64_t s = f64_to_i64(f64_trunc(x)) * 3600 + f64_to_i64(f64_trunc(y)) * 60 + f64_to_i64(f64_trunc(z)); if (s < 0) { v_err(r, HJE_NUM); return; } v_num(r, f64_div(F(s % 86400), F(86400))); return; }
    case FN_PMT: case FN_FV: case FN_PV: {
        f64 tasa, n, c, fv = 0, tipo = 0; NUM(0, tasa); NUM(1, n); NUM(2, c); if (OPT(3)) NUM(3, fv); if (OPT(4)) NUM(4, tipo);
        int t1 = !f64_es_cero(tipo);
        if (f64_es_cero(tasa)) {
            if (id == FN_PMT) { if (f64_es_cero(n)) { v_err(r, HJE_NUM); return; } v_num(r, f64_neg(f64_div(f64_add(c, fv), n))); }
            else if (id == FN_FV) v_num(r, f64_neg(f64_add(fv, f64_mul(c, n))));
            else v_num(r, f64_neg(f64_add(fv, f64_mul(c, n))));
            return;
        }
        f64 p = f64_pow(f64_add(F(1), tasa), n), k = t1 ? f64_add(F(1), tasa) : F(1);
        if (id == FN_PMT) { /* pmt = -(pv*p + fv) * tasa / ((p-1)*k) */ f64 den = f64_mul(f64_sub(p, F(1)), k); if (f64_es_cero(den)) { v_err(r, HJE_NUM); return; } v_num(r, f64_neg(f64_div(f64_mul(f64_add(f64_mul(c, p), fv), tasa), den))); }
        else if (id == FN_FV) { /* fv = -(pv*p + pmt*k*(p-1)/tasa); aqui c = pmt, fv = pv */ v_num(r, f64_neg(f64_add(f64_mul(fv, p), f64_div(f64_mul(f64_mul(c, k), f64_sub(p, F(1))), tasa)))); }
        else { /* pv = -(fv + pmt*k*(p-1)/tasa) / p */ v_num(r, f64_neg(f64_div(f64_add(fv, f64_div(f64_mul(f64_mul(c, k), f64_sub(p, F(1))), tasa)), p))); }
        return;
    }
    case FN_NPV: {
        NUM(0, x); struct agg g; zero(&g, sizeof g); g.prod = F(1); g.guardar = 1;
        visita(e, a + 1, argc - 1, agg_visita, &g); if (g.err) { if (g.vals) hj_free(g.vals); v_err(r, g.err); return; }
        f64 s = 0, d = F(1); for (uint32_t i = 0; i < g.nv; ++i) { d = f64_mul(d, f64_add(F(1), x)); s = f64_add(s, f64_div(g.vals[i], d)); }
        if (g.vals) hj_free(g.vals);
        v_num(r, s); return;
    }
    case FN_IRR: {
        struct agg g; zero(&g, sizeof g); g.prod = F(1); g.guardar = 1;
        visita(e, a, 1, agg_visita, &g); if (g.err || g.nv < 2) { if (g.vals) hj_free(g.vals); v_err(r, g.err ? g.err : HJE_NUM); return; }
        f64 t = 0x3FB999999999999AULL; if (OPT(1)) NUM(1, t);      /* 0,1 */
        int ok = 0;
        for (int it = 0; it < 60; ++it) {
            f64 v = 0, dv = 0, d = F(1);
            for (uint32_t i = 0; i < g.nv; ++i) { f64 di = d; v = f64_add(v, f64_div(g.vals[i], di)); dv = f64_sub(dv, f64_div(f64_mul(F(i), g.vals[i]), f64_mul(di, f64_add(F(1), t)))); d = f64_mul(d, f64_add(F(1), t)); }
            if (f64_es_cero(dv)) break;
            f64 nt = f64_sub(t, f64_div(v, dv));
            if (f64_lt(f64_abs(f64_sub(nt, t)), 0x3E7AD7F29ABCAF48ULL)) { t = nt; ok = 1; break; }   /* 1e-7 */
            t = nt; if (!f64_is_finite(t) || f64_lt(t, F(-1))) break;
        }
        hj_free(g.vals);
        if (!ok) v_err(r, HJE_NUM); else v_num(r, t);
        return;
    }
    default: v_err(r, HJE_NOMBRE); return;
    }
#undef NUM
#undef OPT
}
static int fns_si(struct ectx *e, struct ev *a, int argc, int id, struct ev *r)
{
    int ini = id == FN_COUNTIFS ? 0 : 1;
    if ((argc - ini) % 2 || argc - ini < 2) { v_err(r, HJE_VALOR); return 0; }
    const struct ref *rs = ini ? &a[0].r : 0;
    if (ini && a[0].t != EV_REF) { v_err(r, HJE_VALOR); return 0; }
    struct crit cr[32]; int nc = 0;
    if ((argc - ini) / 2 > 32) { v_err(r, HJE_VALOR); return 0; }
    for (int i = ini; i + 1 < argc && nc < 32; i += 2) { if (a[i].t != EV_REF) { v_err(r, HJE_VALOR); return 0; } crit_prepara(e, &a[i + 1], &cr[nc++]); }
    const struct ref *r0 = &a[ini].r; uint32_t nf = r0->f2 - r0->f1 + 1, ncol = r0->c2 - r0->c1 + 1;
    for (int i = ini; i < argc; i += 2) if (a[i].r.f2 - a[i].r.f1 + 1 != nf || a[i].r.c2 - a[i].r.c1 + 1 != ncol) { v_err(r, HJE_VALOR); return 0; }
    if (rs && (rs->f2 - rs->f1 + 1 != nf || rs->c2 - rs->c1 + 1 != ncol)) { v_err(r, HJE_VALOR); return 0; }
    { int h0 = ref_hoja(e, r0); uint32_t mf = e->l->h[h0].max_fila; if (r0->f1 >= mf) nf = 0; else if (r0->f1 + nf > mf) nf = mf - r0->f1; }
    f64 s = 0, mx = 0, mn = 0; uint32_t n = 0; int hay = 0;
    for (uint32_t f = 0; f < nf; ++f) for (uint32_t c = 0; c < ncol; ++c) {
        int ok = 1;
        for (int k = 0; k < nc && ok; ++k) { struct ev v; celda_en(e, &a[ini + 2 * k].r, f, c, &v); if (!crit_cumple(&cr[k], &v)) ok = 0; }
        if (!ok) continue;
        if (!rs) { ++n; continue; }
        struct ev w; celda_en(e, rs, f, c, &w); if (w.t == HJ_ERR) { v_err(r, w.err); return 0; }
        if (w.t != HJ_NUM) continue;
        s = f64_add(s, w.n); ++n;
        if (!hay) { mx = mn = w.n; hay = 1; } else { if (f64_lt(mx, w.n)) mx = w.n; if (f64_lt(w.n, mn)) mn = w.n; }
    }
    switch (id) {
    case FN_COUNTIFS: v_num(r, F(n)); break;
    case FN_SUMIFS: v_num(r, s); break;
    case FN_AVERAGEIFS: if (!n) v_err(r, HJE_DIV0); else v_num(r, f64_div(s, F(n))); break;
    case FN_MAXIFS: v_num(r, hay ? mx : 0); break;
    default: v_num(r, hay ? mn : 0); break;
    }
    return 0;
}
static void binop(struct ectx *e, int op, struct ev *a, struct ev *b, struct ev *r)
{
    if (op >= OPX_EQ) {
        deref(e, a); deref(e, b);
        if (a->t == HJ_ERR) { *r = *a; return; }
        if (b->t == HJ_ERR) { *r = *b; return; }
        int k = compara(a, b);
        switch (op) { case OPX_EQ: v_bool(r, k == 0); break; case OPX_NE: v_bool(r, k != 0); break; case OPX_LT: v_bool(r, k < 0); break;
                      case OPX_GT: v_bool(r, k > 0); break; case OPX_LE: v_bool(r, k <= 0); break; default: v_bool(r, k >= 0); break; }
        return;
    }
    if (op == OPX_CAT) {
        const char *x = a_txt(e, a); if (!x) { *r = *a; return; }
        const char *y = a_txt(e, b); if (!y) { *r = *b; return; }
        uint32_t lx = hj_slen(x), ly = hj_slen(y); if (lx + ly > HJ_TXT_MAX) { v_err(r, HJE_VALOR); return; }
        char *d = arena(e, lx + ly); if (!d) { v_err(r, HJE_VALOR); return; }
        copia(d, x, lx); copia(d + lx, y, ly); r->t = HJ_TXT; r->s = d; r->err = 0; return;
    }
    f64 x, y;
    if (a_num(e, a, &x)) { *r = *a; return; }
    if (a_num(e, b, &y)) { *r = *b; return; }
    switch (op) {
    case OPX_ADD: v_num(r, f64_add(x, y)); return;
    case OPX_SUB: v_num(r, f64_sub(x, y)); return;
    case OPX_MUL: v_num(r, f64_mul(x, y)); return;
    case OPX_DIV: if (f64_es_cero(y)) v_err(r, HJE_DIV0); else v_num(r, f64_div(x, y)); return;
    case OPX_POW: if (f64_es_cero(x) && !f64_lt(0, y)) { v_err(r, f64_es_cero(y) ? HJE_NUM : HJE_DIV0); return; } if (f64_lt(x, 0) && !f64_is_int(y)) { v_err(r, HJE_NUM); return; } v_num(r, f64_pow(x, y)); return;
    default: v_err(r, HJE_VALOR); return;
    }
}
/* ejecuta el codigo; resultado en *res. 0 = bien */
static int ejecuta(struct ectx *e, const uint8_t *c, uint32_t n, struct ev *res)
{
    int sp = 0; uint32_t pc = 0;
#define NECESITA(k) do { if (sp < (k)) { v_err(res, HJE_VALOR); return -1; } } while (0)
    while (pc < n) {
        uint8_t op = c[pc++];
        switch (op) {
        case OC_FIN: if (sp < 1) { v_err(res, HJE_VALOR); return -1; } *res = e->pila[sp - 1]; return 0;
        case OC_NUM: if (sp >= PILA || pc + 8 > n) { v_err(res, HJE_VALOR); return -1; } { f64 v; copia(&v, c + pc, 8); pc += 8; v_num(&e->pila[sp++], v); } break;
        case OC_STR: { if (sp >= PILA || pc + 4 > n) { v_err(res, HJE_VALOR); return -1; } uint32_t L; copia(&L, c + pc, 4); pc += 4; if (pc + L > n) { v_err(res, HJE_VALOR); return -1; }
            struct ev *v = &e->pila[sp++]; v->t = HJ_TXT; v->err = 0; v->n = 0; char *d = arena(e, L); if (!d) { v_err(res, HJE_VALOR); return -1; } copia(d, c + pc, L); v->s = d; pc += L; break; }
        case OC_BOOL: if (sp >= PILA) { v_err(res, HJE_VALOR); return -1; } v_bool(&e->pila[sp++], c[pc++]); break;
        case OC_ERR: if (sp >= PILA) { v_err(res, HJE_VALOR); return -1; } { struct ev *v = &e->pila[sp++]; v_err(v, c[pc++]); } break;
        case OC_REF: if (sp >= PILA || pc + sizeof(struct ref) > n) { v_err(res, HJE_VALOR); return -1; } { struct ev *v = &e->pila[sp++]; v->t = EV_REF; copia(&v->r, c + pc, (uint32_t)sizeof v->r); pc += (uint32_t)sizeof v->r; } break;
        case OC_NEG: NECESITA(1); { struct ev *v = &e->pila[sp - 1]; f64 x; if (!a_num(e, v, &x)) v_num(v, f64_neg(x)); } break;
        case OC_PCT: NECESITA(1); { struct ev *v = &e->pila[sp - 1]; f64 x; if (!a_num(e, v, &x)) v_num(v, f64_div(x, F(100))); } break;
        case OC_BIN: NECESITA(2); { struct ev r; binop(e, c[pc++], &e->pila[sp - 2], &e->pila[sp - 1], &r); e->pila[sp - 2] = r; --sp; } break;
        case OC_CALL: { int id = c[pc++], argc = c[pc++]; NECESITA(argc); if (!argc && sp >= PILA) { v_err(res, HJE_VALOR); return -1; }
            struct ev r; llama(e, id, &e->pila[sp - argc], argc, &r); sp -= argc; e->pila[sp++] = r; break; }
        case OC_JMPF: { NECESITA(1); if (pc + 8 > n) { v_err(res, HJE_VALOR); return -1; } uint32_t d, fin; copia(&d, c + pc, 4); copia(&fin, c + pc + 4, 4); pc += 8;
            struct ev v = e->pila[sp - 1]; f64 x; --sp;
            if (a_num(e, &v, &x)) { if (v.t != HJ_ERR) v_err(&v, HJE_VALOR); e->pila[sp++] = v; pc = fin; break; }   /* SI(error; ...) = el error */
            if (f64_es_cero(x)) pc = d;
            break;
            }
        case OC_JMP: { uint32_t d; copia(&d, c + pc, 4); pc = d; break; }
        case OC_JNOERR: case OC_JNONA: { NECESITA(1); uint32_t d; copia(&d, c + pc, 4); pc += 4; struct ev *v = &e->pila[sp - 1]; deref(e, v);
            int es = v->t == HJ_ERR && (op == OC_JNOERR || v->err == HJE_NA); if (!es) pc = d; break; }
        case OC_POP: NECESITA(1); --sp; break;
        default: v_err(res, HJE_VALOR); return -1;
        }
        if (pc > n) { v_err(res, HJE_VALOR); return -1; }
    }
    v_err(res, HJE_VALOR); return -1;
#undef NECESITA
}

/* ================================================================ recalculo */
struct orden_f { uint64_t k; int32_t i; uint8_t h; };
static int evalua(struct ectx *e, int h, struct hj_celda *x)
{
    if (!x->code) {
        if (compila(e->l, h, x->form, &x->code, &x->ncode)) { x->code = 0; x->ncode = 0; return x->es_form == 2 ? 0 : -1; }   /* de un archivo: se queda su valor */
    }
    e->hoja = h; e->fila = x->fila; e->col = x->col; e->incompleto = 0; e->nfalta = 0; e->an = 0;
    struct ev r; ejecuta(e, x->code, x->ncode, &r);
    if (e->incompleto) return 1;
    if (r.t == EV_REF) deref(e, &r);
    if (e->incompleto) return 1;
    if (x->es_form == 2 && r.t == HJ_ERR && r.err == HJE_NOMBRE) return 0;   /* una funcion que SOMA no tiene: se respeta el valor del archivo */
    if (x->txt) { hj_free(x->txt); x->txt = 0; }
    x->err = 0; x->n = 0;
    switch (r.t) {
    case HJ_NUM: x->t = HJ_NUM; x->n = r.n; break;
    case HJ_BOOL: x->t = HJ_BOOL; x->n = r.n; break;
    case HJ_TXT: x->t = HJ_TXT; x->txt = hj_strdup(r.s); if (!x->txt) { x->t = HJ_ERR; x->err = HJE_VALOR; } break;
    case HJ_ERR: x->t = HJ_ERR; x->err = r.err == 255 ? HJE_VALOR : r.err; break;
    default: x->t = HJ_NUM; x->n = 0; break;                    /* vacia = 0 */
    }
    return 0;
}
int hj_recalcula(struct hj_libro *l)
{
    uint32_t nf = 0;
    for (int h = 0; h < l->nh; ++h) for (int32_t i = 0; i < l->h[h].nc; ++i) if (l->h[h].c[i].es_form) ++nf;
    if (!nf) return 0;
    struct orden_f *o = (struct orden_f *)hj_alloc(nf * (uint32_t)sizeof *o);
    uint32_t *pila = (uint32_t *)hj_alloc(nf * 4u + 16);
    struct ectx *e = (struct ectx *)hj_alloc((uint32_t)sizeof *e);
    uint32_t *falta = (uint32_t *)hj_alloc(4096 * 4u);
    char *ar = (char *)hj_alloc(256 * 1024);
    if (!o || !pila || !e || !falta || !ar) { if (o) hj_free(o); if (pila) hj_free(pila); if (e) hj_free(e); if (falta) hj_free(falta); if (ar) hj_free(ar); return -1; }
    zero(e, (uint32_t)sizeof *e); e->l = l; e->falta = falta; e->ar = ar; e->acap = 256 * 1024;
    l->gen++; if (!l->gen) l->gen = 1;
    e->gen = l->gen;
    uint32_t k = 0;
    for (int h = 0; h < l->nh; ++h) for (int32_t i = 0; i < l->h[h].nc; ++i) {
        struct hj_celda *x = &l->h[h].c[i]; if (!x->es_form) continue;
        x->estado = 0; o[k].h = (uint8_t)h; o[k].i = i; o[k].k = ((uint64_t)h << 40) | ((uint64_t)x->fila << 14) | x->col; ++k;
    }
    { static const uint32_t G[] = { 88573, 29524, 9841, 3280, 1093, 364, 121, 40, 13, 4, 1 };   /* Shell sort por (hoja, fila, columna) */
      for (int g = 0; g < 11; ++g) for (uint32_t i = G[g]; i < k; ++i) { struct orden_f t = o[i]; uint32_t j = i; while (j >= G[g] && o[j - G[g]].k > t.k) { o[j] = o[j - G[g]]; j -= G[g]; } o[j] = t; } }
    uint32_t hechas = 0; uint64_t pasos = 0, tope = (uint64_t)nf * 64 + 10000;
    for (uint32_t q = 0; q < k; ++q) {
        struct hj_celda *x0 = &l->h[o[q].h].c[o[q].i]; if (x0->gen == e->gen) continue;
        uint32_t sp = 0; pila[sp++] = ((uint32_t)o[q].h << 27) | (uint32_t)o[q].i; x0->estado = 1;
        while (sp) {
            if (++pasos > tope) break;
            uint32_t cod = pila[sp - 1]; int h = (int)(cod >> 27); struct hj_celda *x = &l->h[h].c[cod & 0x7FFFFFF];
            int r = evalua(e, h, x);
            if (r < 0) { x->t = HJ_ERR; x->err = HJE_NOMBRE; x->gen = e->gen; x->estado = 0; --sp; ++hechas; continue; }
            if (r == 0) { x->gen = e->gen; x->estado = 0; --sp; ++hechas; continue; }
            int nuevas = 0;
            for (uint32_t z = 0; z < e->nfalta; ++z) {
                uint32_t dc = e->falta[z]; struct hj_celda *y = &l->h[dc >> 27].c[dc & 0x7FFFFFF];
                if (y->gen == e->gen || y->estado == 1) continue;
                if (sp >= nf + 4) break;
                y->estado = 1; pila[sp++] = dc; ++nuevas;
            }
            if (!nuevas) {                                       /* todo lo que falta esta en la pila: es un ciclo */
                if (x->txt) { hj_free(x->txt); x->txt = 0; }
                x->t = HJ_ERR; x->err = HJE_CICLO; x->gen = e->gen; x->estado = 0; --sp; ++hechas;
            }
        }
        while (sp) { uint32_t cod = pila[--sp]; struct hj_celda *x = &l->h[cod >> 27].c[cod & 0x7FFFFFF]; x->estado = 0; if (x->gen != e->gen) { x->t = HJ_ERR; x->err = HJE_CICLO; x->gen = e->gen; } }
    }
    hj_free(o); hj_free(pila); hj_free(e); hj_free(falta); hj_free(ar);
    return (int)hechas;
}
