/* SOMA_272_AGENDA: entender fechas y horas en castellano (se incluye desde agenda_core.c).
 * "el miercoles dia 6 a las 13 horas tengo un partido de futbol con los amigos", "manana reunion", "cada lunes a las 8 gimnasio",
 * "dentro de 2 horas llamar a Ana", "el 25 de diciembre cena de Navidad", "dia 15 a las 5 y media de la tarde dentista avisame 1 hora antes". */
#define NT 64
struct tk { const char *s; uint8_t n, used; char f[22]; };

static char pliega(const unsigned char *s, int *adv)       /* una letra con tildes -> letra simple minuscula (n con tilde: n) */
{
    *adv = 1; unsigned char c = s[0];
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    if (c == 0xC3 && s[1]) {
        *adv = 2; unsigned char d = s[1];
        switch (d) {
        case 0xA1: case 0x81: return 'a'; case 0xA9: case 0x89: return 'e'; case 0xAD: case 0x8D: return 'i';
        case 0xB3: case 0x93: return 'o'; case 0xBA: case 0x9A: case 0xBC: case 0x9C: return 'u'; case 0xB1: case 0x91: return 'n';
        }
        return '?';
    }
    return (char)c;
}
static int sep(unsigned char c) { return c == ' ' || c == ',' || c == ';' || c == '?' || c == '!' || c == '\t' || c == '\n' || c == '"'; }
static int tokeniza(const char *txt, struct tk *t)
{
    int n = 0; const unsigned char *s = (const unsigned char *)txt;
    while (*s && n < NT) {
        while (*s && (sep(*s) || (s[0] == 0xC2 && (s[1] == 0xBF || s[1] == 0xA1)))) s += (s[0] == 0xC2 ? 2 : 1);
        if (!*s) break;
        const unsigned char *a = s;
        while (*s && !sep(*s)) ++s;
        const unsigned char *b = s;
        while (b > a && (b[-1] == '.' || b[-1] == ':' || b[-1] == ')' || b[-1] == '(')) --b;
        while (a < b && (*a == '(')) ++a;
        if (b <= a) continue;
        t[n].s = (const char *)a; t[n].n = (uint8_t)(b - a > 250 ? 250 : b - a); t[n].used = 0;
        int k = 0; for (const unsigned char *q = a; q < b && k < 21;) { int adv; char ch = pliega(q, &adv); t[n].f[k++] = ch; q += adv; }
        t[n].f[k] = 0; ++n;
    }
    return n;
}
static int es_(struct tk *t, int nt, int i, const char *w) { return i >= 0 && i < nt && !t[i].used && eq(t[i].f, w); }
static void uso(struct tk *t, int nt, int a, int b) { for (int i = a; i <= b && i < nt; ++i) if (i >= 0) t[i].used = 1; }

static const char *const NUMW[] = { "cero", "uno", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve", "diez", "once", "doce", "trece", "catorce", "quince", "dieciseis",
    "diecisiete", "dieciocho", "diecinueve", "veinte", "veintiuno", "veintidos", "veintitres", "veinticuatro", "veinticinco", "veintiseis", "veintisiete", "veintiocho", "veintinueve", "treinta", 0 };
/* numero (cifras o palabra) en el token i: -1 si no lo es */
static int64_t tnum(struct tk *t, int nt, int i)
{
    if (i < 0 || i >= nt || t[i].used) return -1;
    const char *f = t[i].f; int64_t v = 0; int k = 0;
    while (f[k] >= '0' && f[k] <= '9' && k < 6) { v = v * 10 + (f[k] - '0'); ++k; }
    if (k && !f[k]) return v;
    if (eq(f, "un") || eq(f, "una")) return 1;
    for (int w = 0; NUMW[w]; ++w) if (eq(f, NUMW[w])) return w;
    return -1;
}
static int mes_de(struct tk *t, int nt, int i)
{
    if (i < 0 || i >= nt || t[i].used) return 0;
    static const char *const M[12] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre" };
    for (int m = 0; m < 12; ++m) if (eq(t[i].f, M[m])) return m + 1;
    if (eq(t[i].f, "setiembre") || eq(t[i].f, "sept")) return 9;
    return 0;
}
static int dsem_de(struct tk *t, int nt, int i)
{
    if (i < 0 || i >= nt || t[i].used) return -1;
    static const char *const D[7] = { "lunes", "martes", "miercoles", "jueves", "viernes", "sabado", "domingo" };
    for (int d = 0; d < 7; ++d) if (eq(t[i].f, D[d])) return d;
    return -1;
}

struct pf {
    int64_t dia;            /* dia (desde 1970) o INT64_MIN si no hay */
    int hora, min;          /* -1 si no hay */
    int dur;                /* minutos, -1 si no hay */
    int rep, cada; int rep_dsem;   /* repeticion; rep_dsem = dia de la semana pedido ("cada lunes") o -1 */
    int antes;              /* -2 no dicho, 0 sin aviso, N minutos */
    int rel_min;            /* "dentro de N horas/minutos": minutos, o -1 */
    int ndias;              /* "del 3 al 7 de marzo": dias seguidos (0 = uno) */
};
#define SIN_DIA ((int64_t)(-9000000000000000000LL))

/* "H", "H:MM", "HhMM", "Hh" en el token i -> hora y minuto; 1 si lo es */
static int hora_tok(struct tk *t, int nt, int i, int *h, int *m)
{
    if (i < 0 || i >= nt || t[i].used) return 0;
    const char *f = t[i].f; int hh = 0, k = 0, d = 0;
    while (f[k] >= '0' && f[k] <= '9' && d < 2) { hh = hh * 10 + (f[k] - '0'); ++k; ++d; }
    if (!d) { int64_t v = tnum(t, nt, i); if (v >= 0 && v <= 24 && !(f[0] >= '0' && f[0] <= '9')) { *h = (int)v; *m = 0; return 1; } return 0; }
    int mm = 0;
    if (!f[k]) { *h = hh; *m = 0; return hh <= 24; }
    if (f[k] == ':' || f[k] == 'h' || f[k] == '.') {
        ++k; int dd = 0; while (f[k] >= '0' && f[k] <= '9' && dd < 2) { mm = mm * 10 + (f[k] - '0'); ++k; ++dd; }
        if (f[k] == 'h' && !f[k + 1]) ++k;
        if (f[k] || mm > 59 || hh > 24 || (f[(d)] == '.' && !dd)) return 0;
        *h = hh; *m = mm; return 1;
    }
    return 0;
}

/* mas de un cartel: reconoce y consume; devuelve 0 si todo bien */
static void parsea_aviso(struct tk *t, int nt, struct pf *r)
{
    for (int i = 0; i < nt; ++i) {
        if (es_(t, nt, i, "sin") && (es_(t, nt, i + 1, "aviso") || es_(t, nt, i + 1, "avisos") || es_(t, nt, i + 1, "alarma"))) { r->antes = 0; uso(t, nt, i, i + 1); continue; }
        if (es_(t, nt, i, "no") && es_(t, nt, i + 1, "me") && (es_(t, nt, i + 2, "avises") || es_(t, nt, i + 2, "recuerdes"))) { r->antes = 0; uso(t, nt, i, i + 2); continue; }
        int64_t v = tnum(t, nt, i); int j = i + 1; if (v < 0) continue;
        int mult = 0;
        if (es_(t, nt, j, "minuto") || es_(t, nt, j, "minutos") || es_(t, nt, j, "min")) mult = 1;
        else if (es_(t, nt, j, "hora") || es_(t, nt, j, "horas")) mult = 60;
        else if (es_(t, nt, j, "dia") || es_(t, nt, j, "dias")) mult = 1440;
        if (mult && es_(t, nt, j + 1, "antes")) {
            r->antes = (int)(v * mult); int a = i;
            if (a >= 1 && (es_(t, nt, a - 1, "avisame") || es_(t, nt, a - 1, "recuerdame") || es_(t, nt, a - 1, "avisa") || es_(t, nt, a - 1, "recuerda") || es_(t, nt, a - 1, "avisarme") || es_(t, nt, a - 1, "recordarme"))) --a;
            else if (a >= 2 && es_(t, nt, a - 1, "me") && (es_(t, nt, a - 2, "avisa") || es_(t, nt, a - 2, "recuerda"))) a -= 2;
            uso(t, nt, a, j + 1);
        }
    }
    for (int i = 0; i < nt; ++i) if (es_(t, nt, i, "avisame") || es_(t, nt, i, "recuerdame") || es_(t, nt, i, "recordarme") || es_(t, nt, i, "avisarme")) t[i].used = 1;
}
static void parsea_rep(struct tk *t, int nt, struct pf *r)
{
    for (int i = 0; i < nt; ++i) {
        int cada = es_(t, nt, i, "cada"), todos = es_(t, nt, i, "todos") || es_(t, nt, i, "todas");
        if (es_(t, nt, i, "diariamente")) { r->rep = AG_REP_DIA; t[i].used = 1; continue; }
        if (es_(t, nt, i, "semanalmente")) { r->rep = AG_REP_SEM; t[i].used = 1; continue; }
        if (es_(t, nt, i, "mensualmente")) { r->rep = AG_REP_MES; t[i].used = 1; continue; }
        if (es_(t, nt, i, "anualmente")) { r->rep = AG_REP_ANO; t[i].used = 1; continue; }
        if (es_(t, nt, i, "laborables") || (es_(t, nt, i, "entre") && es_(t, nt, i + 1, "semana"))) { r->rep = AG_REP_LAB; uso(t, nt, i, es_(t, nt, i, "entre") ? i + 1 : i); continue; }
        if (es_(t, nt, i, "de") && dsem_de(t, nt, i + 1) == 0 && es_(t, nt, i + 2, "a") && dsem_de(t, nt, i + 3) == 4) { r->rep = AG_REP_LAB; uso(t, nt, i, i + 3); continue; }
        if (!cada && !todos) continue;
        int j = i + 1; if (todos && (es_(t, nt, j, "los") || es_(t, nt, j, "las"))) ++j;
        int64_t n = 1; int64_t v = tnum(t, nt, j);
        if (v > 1 && v < 366) { n = v; ++j; }
        int d = dsem_de(t, nt, j);
        if (d >= 0) { r->rep = AG_REP_SEM; r->rep_dsem = d; r->cada = (int)n; uso(t, nt, i, j); continue; }
        if (es_(t, nt, j, "dia") || es_(t, nt, j, "dias")) r->rep = AG_REP_DIA;
        else if (es_(t, nt, j, "semana") || es_(t, nt, j, "semanas")) r->rep = AG_REP_SEM;
        else if (es_(t, nt, j, "mes") || es_(t, nt, j, "meses")) r->rep = AG_REP_MES;
        else if (es_(t, nt, j, "ano") || es_(t, nt, j, "anos")) r->rep = AG_REP_ANO;
        else continue;
        r->cada = (int)n; uso(t, nt, i, j);
    }
}
static int franja(struct tk *t, int nt, int i)            /* "de la tarde", "por la manana", "de la noche"...: 1 manana, 2 tarde, 3 noche; 0 no; consume si es */
{
    int a = i; if (es_(t, nt, a, "de") || es_(t, nt, a, "por") || es_(t, nt, a, "en")) ++a; else if (es_(t, nt, a, "la")) { /* "la tarde" */ } else if (es_(t, nt, a, "esta")) { ++a; int f = es_(t, nt, a, "manana") ? 1 : es_(t, nt, a, "tarde") ? 2 : es_(t, nt, a, "noche") ? 3 : 0; if (f) uso(t, nt, i, a); return f; }
    if (es_(t, nt, a, "la")) ++a;
    if (a == i) return 0;                                   /* sin "de/por/en/la/esta" delante, "manana" es el dia */
    int f = es_(t, nt, a, "manana") ? 1 : es_(t, nt, a, "tarde") ? 2 : es_(t, nt, a, "noche") ? 3 : es_(t, nt, a, "madrugada") ? 1 : 0;
    if (f) uso(t, nt, i, a);
    return f;
}
static void parsea_hora(struct tk *t, int nt, struct pf *r)
{
    for (int i = 0; i < nt && r->hora < 0; ++i) {
        int j = -1, h = 0, m = 0;
        if (es_(t, nt, i, "dentro") && es_(t, nt, i + 1, "de")) {
            int64_t v = tnum(t, nt, i + 2); int k = i + 3; int mult = 0;
            if (v < 0 && es_(t, nt, i + 2, "media") && es_(t, nt, i + 3, "hora")) { r->rel_min = 30; uso(t, nt, i, i + 3); continue; }
            if (es_(t, nt, k, "minuto") || es_(t, nt, k, "minutos")) mult = 1; else if (es_(t, nt, k, "hora") || es_(t, nt, k, "horas")) mult = 60;
            if (v >= 0 && mult) { r->rel_min = (int)(v * mult); uso(t, nt, i, k); }
            continue;
        }
        if (es_(t, nt, i, "mediodia")) { r->hora = 12; r->min = 0; t[i].used = 1; continue; }
        if (es_(t, nt, i, "medianoche")) { r->hora = 0; r->min = 0; t[i].used = 1; continue; }
        int pre = 0;
        if ((es_(t, nt, i, "a") && (es_(t, nt, i + 1, "las") || es_(t, nt, i + 1, "la"))) && hora_tok(t, nt, i + 2, &h, &m)) { j = i + 2; pre = 1; }
        else if ((es_(t, nt, i, "las") || es_(t, nt, i, "la")) && hora_tok(t, nt, i + 1, &h, &m) && !(i > 0 && es_(t, nt, i - 1, "de"))) { j = i + 1; pre = 1; }
        else if (hora_tok(t, nt, i, &h, &m) && (t[i].f[1] == ':' || t[i].f[2] == ':' || (t[i].f[0] >= '0' && t[i].f[0] <= '9' && (t[i].f[1] == 'h' || t[i].f[2] == 'h')))) { j = i; }
        else if (tnum(t, nt, i) >= 0 && tnum(t, nt, i) <= 24 && (es_(t, nt, i + 1, "horas") || es_(t, nt, i + 1, "hora") || es_(t, nt, i + 1, "h")) && !(i > 0 && es_(t, nt, i - 1, "durante"))) { h = (int)tnum(t, nt, i); m = 0; j = i; }
        if (j < 0) continue;
        int fin = j, menos = 0;
        if (es_(t, nt, fin + 1, "y") && es_(t, nt, fin + 2, "media")) { m = 30; fin += 2; }
        else if (es_(t, nt, fin + 1, "y") && es_(t, nt, fin + 2, "cuarto")) { m = 15; fin += 2; }
        else if (es_(t, nt, fin + 1, "menos") && es_(t, nt, fin + 2, "cuarto")) { m = 45; menos = 1; fin += 2; }
        else if (es_(t, nt, fin + 1, "y") && tnum(t, nt, fin + 2) >= 1 && tnum(t, nt, fin + 2) <= 59 && !es_(t, nt, fin + 3, "horas")) { m = (int)tnum(t, nt, fin + 2); fin += 2; }
        else if (es_(t, nt, fin + 1, "en") && es_(t, nt, fin + 2, "punto")) fin += 2;
        if (es_(t, nt, fin + 1, "horas") || es_(t, nt, fin + 1, "hora") || es_(t, nt, fin + 1, "h")) ++fin;
        int fr = franja(t, nt, fin + 1); if (fr) { fin += 0; }
        (void)pre;
        if (fr >= 2 && h < 12) h += 12; else if (fr == 1 && h == 12) h = 0;
        else if (!fr && h >= 1 && h <= 7) h += 12;               /* "a las 5" = las 17:00 (nadie queda a las 5 de la madrugada) */
        if (menos) { h = h - 1; if (h < 0) h = 23; }
        if (h > 24 || (h == 24 && m)) continue;
        if (h == 24) h = 0;
        r->hora = h; r->min = m; uso(t, nt, i, fin);
    }
    /* duracion: "durante 2 horas", "durante media hora", "de 10 a 12" */
    for (int i = 0; i < nt; ++i) {
        if (es_(t, nt, i, "durante")) {
            if (es_(t, nt, i + 1, "media") && es_(t, nt, i + 2, "hora")) { r->dur = 30; uso(t, nt, i, i + 2); continue; }
            int64_t v = tnum(t, nt, i + 1); int mult = 0;
            if (es_(t, nt, i + 2, "minuto") || es_(t, nt, i + 2, "minutos")) mult = 1; else if (es_(t, nt, i + 2, "hora") || es_(t, nt, i + 2, "horas")) mult = 60;
            if (v >= 0 && mult) { r->dur = (int)(v * mult); int fin = i + 2; if (mult == 60 && es_(t, nt, fin + 1, "y") && es_(t, nt, fin + 2, "media")) { r->dur += 30; fin += 2; } uso(t, nt, i, fin); }
        }
    }
    if (r->hora < 0 && r->rel_min < 0)                      /* "de 10 a 12", "de 10:30 a 12:00" */
        for (int i = 0; i + 3 < nt; ++i) {
            int h1, m1, h2, m2;
            if (es_(t, nt, i, "de") && hora_tok(t, nt, i + 1, &h1, &m1) && es_(t, nt, i + 2, "a") && hora_tok(t, nt, i + 3, &h2, &m2) && h1 <= 23 && h2 <= 24) {
                int fin = i + 3; if (es_(t, nt, fin + 1, "horas") || es_(t, nt, fin + 1, "h")) ++fin;
                int fr = franja(t, nt, fin + 1);
                if (fr >= 2) { if (h1 < 12) h1 += 12; if (h2 < 12) h2 += 12; } else if (!fr && h1 >= 1 && h1 <= 7) { h1 += 12; h2 += (h2 < 12 ? 12 : 0); }
                int d = (h2 * 60 + m2) - (h1 * 60 + m1); if (d <= 0) d += 12 * 60;
                r->hora = h1; r->min = m1; r->dur = d; uso(t, nt, i, fin); break;
            }
        }
}
static int hay_antes(struct tk *t, int nt, int i, const char *w) { return i > 0 && es_(t, nt, i - 1, w); }
static int parsea_dia(struct tk *t, int nt, int64_t ahora, struct pf *r)
{
    int64_t hoy = ag_dia_de(ahora); int y0, m0, d0, hh, mm; ag_civil(hoy, &y0, &m0, &d0); (void)hh; (void)mm;
    int dsem = -1, dom = -1, mes = 0, anio = 0, proximo = 0; int64_t rel = SIN_DIA;
    for (int i = 0; i < nt; ++i) {
        if (t[i].used) continue;
        if (es_(t, nt, i, "hoy")) { rel = hoy; t[i].used = 1; continue; }
        if (es_(t, nt, i, "pasado") && es_(t, nt, i + 1, "manana")) { rel = hoy + 2; uso(t, nt, i, i + 1); continue; }
        if (es_(t, nt, i, "manana") && !hay_antes(t, nt, i, "la") && !hay_antes(t, nt, i, "esta")) { rel = hoy + 1; t[i].used = 1; continue; }
        if (es_(t, nt, i, "ayer")) { rel = hoy - 1; t[i].used = 1; continue; }
        if (es_(t, nt, i, "dentro") && es_(t, nt, i + 1, "de")) {
            int64_t v = tnum(t, nt, i + 2); int k = i + 3;
            if (v >= 0 && (es_(t, nt, k, "dia") || es_(t, nt, k, "dias"))) { rel = hoy + v; uso(t, nt, i, k); }
            else if (v >= 0 && (es_(t, nt, k, "semana") || es_(t, nt, k, "semanas"))) { rel = hoy + 7 * v; uso(t, nt, i, k); }
            else if (v >= 0 && (es_(t, nt, k, "mes") || es_(t, nt, k, "meses"))) { int y, m, d; ag_civil(hoy, &y, &m, &d); int64_t mt = (int64_t)y * 12 + (m - 1) + v; int yy = (int)(mt / 12), mo = (int)(mt % 12) + 1; if (d > ag_dias_mes(yy, mo)) d = ag_dias_mes(yy, mo); rel = ag_dias(yy, mo, d); uso(t, nt, i, k); }
            continue;
        }
        { /* AAAA-MM-DD */ const char *f = t[i].f; int a = 0, b = 0, c = 0, k = 0, na = 0;
          while (f[k] >= '0' && f[k] <= '9' && na < 5) { a = a * 10 + f[k] - '0'; ++k; ++na; }
          if (na == 4 && f[k] == '-') { ++k; int nb = 0; while (f[k] >= '0' && f[k] <= '9' && nb < 3) { b = b * 10 + f[k] - '0'; ++k; ++nb; } if (nb && f[k] == '-') { ++k; int nc = 0; while (f[k] >= '0' && f[k] <= '9' && nc < 3) { c = c * 10 + f[k] - '0'; ++k; ++nc; } if (nc && !f[k] && b >= 1 && b <= 12 && c >= 1 && c <= ag_dias_mes(a, b)) { rel = ag_dias(a, b, c); t[i].used = 1; continue; } } }
        }
        { /* D/M[/A] */ const char *f = t[i].f; int a = 0, b = 0, c = 0, k = 0, na = 0, nb = 0, nc = 0;
          while (f[k] >= '0' && f[k] <= '9' && na < 2) { a = a * 10 + f[k] - '0'; ++k; ++na; }
          if (na && f[k] == '/') { ++k; while (f[k] >= '0' && f[k] <= '9' && nb < 2) { b = b * 10 + f[k] - '0'; ++k; ++nb; }
            if (nb && (f[k] == '/' || !f[k])) {
                if (f[k] == '/') { ++k; while (f[k] >= '0' && f[k] <= '9' && nc < 4) { c = c * 10 + f[k] - '0'; ++k; ++nc; } }
                if (!f[k] && (nc == 0 || nc == 2 || nc == 4) && b >= 1 && b <= 12 && a >= 1 && a <= 31) {
                    int yy = nc == 0 ? y0 : nc == 2 ? 2000 + c : c;
                    if (a <= ag_dias_mes(yy, b)) { int64_t d = ag_dias(yy, b, a); if (nc == 0 && d < hoy) d = ag_dias(yy + 1, b, a <= ag_dias_mes(yy + 1, b) ? a : ag_dias_mes(yy + 1, b)); rel = d; t[i].used = 1; continue; }
                } } }
        }
        int d = dsem_de(t, nt, i);
        if (d >= 0) {
            dsem = d; int a = i;
            if (a > 0 && (es_(t, nt, a - 1, "el") || es_(t, nt, a - 1, "este") || es_(t, nt, a - 1, "proximo") || es_(t, nt, a - 1, "siguiente") || es_(t, nt, a - 1, "del"))) { if (es_(t, nt, a - 1, "proximo") || es_(t, nt, a - 1, "siguiente")) proximo = 1; --a; if (a > 0 && es_(t, nt, a - 1, "el") && es_(t, nt, a, "proximo")) --a; }
            int b = i; if (es_(t, nt, b + 1, "que") && es_(t, nt, b + 2, "viene")) { proximo = 1; b += 2; } else if (es_(t, nt, b + 1, "proximo") || es_(t, nt, b + 1, "siguiente")) { proximo = 1; ++b; }
            uso(t, nt, a, b);
            if (tnum(t, nt, b + 1) >= 1 && tnum(t, nt, b + 1) <= 31 && !es_(t, nt, b + 2, "horas") && !es_(t, nt, b + 2, "hora") && !es_(t, nt, b + 2, "minutos")) {   /* "el sabado 12", "el sabado 12 de diciembre" */
                dom = (int)tnum(t, nt, b + 1); int j2 = b + 1;
                if (es_(t, nt, j2 + 1, "de") && mes_de(t, nt, j2 + 2)) { mes = mes_de(t, nt, j2 + 2); j2 += 2; if (es_(t, nt, j2 + 1, "de") && tnum(t, nt, j2 + 2) >= 1900 && tnum(t, nt, j2 + 2) <= 2200) { anio = (int)tnum(t, nt, j2 + 2); j2 += 2; } }
                uso(t, nt, b + 1, j2);
            }
            continue;
        }
        if (es_(t, nt, i, "del") && tnum(t, nt, i + 1) >= 1 && tnum(t, nt, i + 1) <= 31 && es_(t, nt, i + 2, "al") && tnum(t, nt, i + 3) >= 1 && tnum(t, nt, i + 3) <= 31 && es_(t, nt, i + 4, "de") && mes_de(t, nt, i + 5)) {
            int d1 = (int)tnum(t, nt, i + 1), d2 = (int)tnum(t, nt, i + 3), j2 = i + 5;
            if (d2 >= d1) { dom = d1; mes = mes_de(t, nt, j2); r->ndias = d2 - d1 + 1; if (es_(t, nt, j2 + 1, "de") && tnum(t, nt, j2 + 2) >= 1900 && tnum(t, nt, j2 + 2) <= 2200) { anio = (int)tnum(t, nt, j2 + 2); j2 += 2; } uso(t, nt, i, j2); continue; }
        }
        /* "dia 6", "el dia 6 de enero", "6 de enero", "el 6 de enero de 2027" */
        int ini = i; int64_t v = -1; int j = i;
        if (es_(t, nt, i, "dia") && tnum(t, nt, i + 1) >= 1 && tnum(t, nt, i + 1) <= 31) { v = tnum(t, nt, i + 1); j = i + 1; if (i > 0 && es_(t, nt, i - 1, "el")) ini = i - 1; }
        else if (tnum(t, nt, i) >= 1 && tnum(t, nt, i) <= 31 && es_(t, nt, i + 1, "de") && mes_de(t, nt, i + 2)) { v = tnum(t, nt, i); j = i; if (i > 0 && es_(t, nt, i - 1, "el")) ini = i - 1; }
        else if (es_(t, nt, i, "el") && tnum(t, nt, i + 1) >= 1 && tnum(t, nt, i + 1) <= 31 && es_(t, nt, i + 2, "de") && mes_de(t, nt, i + 3)) { v = tnum(t, nt, i + 1); j = i + 1; }
        if (v < 0) continue;
        dom = (int)v;
        if (es_(t, nt, j + 1, "de") && mes_de(t, nt, j + 2)) { mes = mes_de(t, nt, j + 2); j += 2; if (es_(t, nt, j + 1, "de") && tnum(t, nt, j + 2) >= 1900 && tnum(t, nt, j + 2) <= 2200) { anio = (int)tnum(t, nt, j + 2); j += 2; } }
        uso(t, nt, ini, j);
    }
    if (rel != SIN_DIA) { r->dia = rel; return 0; }
    if (dom > 0) {
        int y = anio ? anio : y0, m = mes ? mes : m0;
        for (int k = 0; k < 40; ++k) {
            if (dom <= ag_dias_mes(y, m)) {
                int64_t d = ag_dias(y, m, dom);
                int ok = d >= hoy || anio;
                if (ok && (dsem < 0 || ag_dia_semana(d) == dsem)) { r->dia = d; return 0; }
                if (anio && mes && dsem < 0) { r->dia = d; return 0; }
            }
            if (mes && !anio) { ++y; if (k > 8) break; } else if (mes && anio) break;      /* con mes fijo: el proximo ano */
            else { if (++m > 12) { m = 1; ++y; } }
        }
        return -1;
    }
    if (dsem >= 0) {
        int64_t d = hoy + 1; while (ag_dia_semana(d) != dsem) ++d;
        if (proximo && ag_dia_semana(hoy) != dsem && (d - hoy) < 7 && 0) d += 7;
        r->dia = d; return 0;
    }
    return 0;
}

static const char *const RELLENO[] = { "tengo", "tienes", "tiene", "tendre", "tenemos", "tenia", "hay", "habra", "que", "un", "una", "unos", "unas", "algun", "alguna", "algo", "el", "la", "los", "las", "lo",
    "para", "de", "del", "a", "al", "en", "por", "y", "e", "con", "apunta", "apuntame", "apuntar", "anota", "anotame", "anotar", "anade", "anademe", "anadir", "agrega", "agregame", "agregar", "pon", "ponme", "poner",
    "crea", "creame", "crear", "agenda", "agendar", "agendame", "evento", "quiero", "necesito", "me", "voy", "ir", "este", "esta", "mi", "recuerdame", "recordarme", "recuerda", "ese", "esa", "tambien", "ademas", 0 };
static int es_relleno(const char *f) { for (int i = 0; RELLENO[i]; ++i) if (eq(f, RELLENO[i])) return 1; return 0; }
/* el titulo: lo que queda sin entender, sin las palabras de relleno de delante y de detras */
static void titulo_de(struct tk *t, int nt, char *o, uint32_t cap)
{
    int a = 0, b = nt - 1;
    while (a <= b && (t[a].used || es_relleno(t[a].f))) ++a;
    while (b >= a && (t[b].used || (es_relleno(t[b].f) && !eq(t[b].f, "con") ))) --b;
    uint32_t p = 0; o[0] = 0;
    for (int i = a; i <= b; ++i) {
        if (t[i].used) continue;
        if (p) putc_(o, cap, &p, ' ');
        for (int k = 0; k < t[i].n && p + 1 < cap; ++k) o[p++] = t[i].s[k];
        o[p] = 0;
    }
    if (o[0] >= 'a' && o[0] <= 'z') o[0] = (char)(o[0] - 32);
    else if ((unsigned char)o[0] == 0xC3 && (unsigned char)o[1] >= 0xA1 && (unsigned char)o[1] <= 0xBA) { unsigned char c = (unsigned char)o[1]; if (c == 0xA1 || c == 0xA9 || c == 0xAD || c == 0xB3 || c == 0xBA || c == 0xB1) o[1] = (char)(c - 0x20); }
}
static int categoria_de(struct tk *t, int nt)
{
    static const char *const K[AG_NCAT][14] = {
        { 0 },
        { "reunion", "trabajo", "cliente", "clientes", "oficina", "jefe", "presentacion", "entrevista", "proyecto", "videollamada", 0 },
        { "futbol", "partido", "gimnasio", "gym", "correr", "entrenar", "entrenamiento", "padel", "tenis", "deporte", "natacion", "baloncesto", "yoga", 0 },
        { "medico", "dentista", "doctor", "doctora", "analisis", "hospital", "fisio", "fisioterapeuta", "pediatra", "revision", "oculista", 0 },
        { "cumpleanos", "fiesta", "cena", "amigos", "copas", "boda", "comida", "cafe", "cine", "quedada", "barbacoa", 0 },
        { "viaje", "vuelo", "tren", "avion", "hotel", "aeropuerto", "vacaciones", "excursion", 0 },
        { "familia", "abuela", "abuelo", "padres", "hijos", "colegio", "madre", "padre", "hermano", "hermana", "primos", 0 },
        { "examen", "clase", "estudiar", "universidad", "curso", "tarea", "deberes", "tutoria", "conferencia", 0 } };
    for (int c = 1; c < AG_NCAT; ++c) for (int i = 0; i < nt; ++i) for (int k = 0; K[c][k]; ++k) if (eq(t[i].f, K[c][k])) return c;
    return 0;
}

int ag_parsea(const char *txt, int64_t ahora, struct ag_ev *ev)
{
    struct tk t[NT]; int nt = tokeniza(txt, t);
    struct pf r; r.dia = SIN_DIA; r.hora = -1; r.min = 0; r.dur = -1; r.rep = 0; r.cada = 1; r.rep_dsem = -1; r.antes = -2; r.rel_min = -1; r.ndias = 0;
    char *pe = (char *)ev; for (uint32_t k = 0; k < sizeof *ev; ++k) pe[k] = 0;
    ev->cada = 1; ev->antes = -1; ev->activo = 1;
    parsea_aviso(t, nt, &r); parsea_rep(t, nt, &r); parsea_hora(t, nt, &r);
    int franja_sola = 0;
    for (int i = 0; i < nt && r.hora < 0 && r.rel_min < 0; ++i) {                                /* "esta tarde", "manana por la noche" sin hora */
        int f = franja(t, nt, i);
        if (f) { franja_sola = f; break; }
    }
    int tiene_rel_dia = 0; (void)tiene_rel_dia;
    if (parsea_dia(t, nt, ahora, &r)) return -1;
    int64_t hoy = ag_dia_de(ahora), min_hoy = ahora - hoy * 1440;
    if (r.rel_min >= 0) { int64_t m = ahora + r.rel_min; ev->ini = m; if (r.dia != SIN_DIA) { r.dia = SIN_DIA; } ev->todo_dia = 0; }
    else {
        if (r.hora < 0 && franja_sola) { r.hora = franja_sola == 1 ? 9 : franja_sola == 2 ? 17 : 21; r.min = 0; }
        if (r.dia == SIN_DIA && r.rep_dsem >= 0) { int64_t d = hoy; while (ag_dia_semana(d) != r.rep_dsem || d < hoy) ++d; r.dia = d; if (d == hoy && r.hora >= 0 && r.hora * 60 + r.min < min_hoy) r.dia += 7; }
        if (r.dia == SIN_DIA && r.hora < 0 && !r.rep) return -1;
        if (r.dia == SIN_DIA && r.hora < 0 && r.rep) { r.dia = hoy; }
        if (r.dia == SIN_DIA) { r.dia = hoy; if (r.hora * 60 + r.min < min_hoy) ++r.dia; }
        else if (r.rep_dsem >= 0 && ag_dia_semana(r.dia) != r.rep_dsem) { int64_t d = r.dia; while (ag_dia_semana(d) != r.rep_dsem) ++d; r.dia = d; }
        if (r.rep_dsem >= 0 && r.dia == hoy && r.hora >= 0 && r.hora * 60 + r.min < min_hoy) r.dia += 7;     /* "cada lunes a las 8" dicho el lunes a las 10: empieza el siguiente */
        if (r.hora < 0) { ev->todo_dia = 1; ev->ini = r.dia * 1440; } else ev->ini = r.dia * 1440 + r.hora * 60 + r.min;
        if (r.rep_dsem >= 0 && r.rep == AG_REP_SEM) { int64_t d = ag_dia_de(ev->ini); (void)d; }
    }
    if (r.dur > 0 && !ev->todo_dia) ev->dur = (uint32_t)r.dur;
    if (r.ndias > 1 && ev->todo_dia) ev->dur = (uint32_t)r.ndias * 1440u;
    ev->rep = (uint8_t)r.rep; ev->cada = (uint16_t)(r.cada < 1 ? 1 : r.cada);
    if (r.antes != -2) ev->antes = (int16_t)r.antes;
    ev->cat = (uint8_t)categoria_de(t, nt);
    titulo_de(t, nt, ev->titulo, AG_TIT);
    if (!ev->titulo[0]) cpy(ev->titulo, AG_TIT, "Evento");
    return 0;
}

int ag_parsea_tramo(const char *txt, int64_t ahora, int64_t *d0, int64_t *d1)
{
    struct tk t[NT]; int nt = tokeniza(txt, t); int64_t hoy = ag_dia_de(ahora);
    int semana = 0, mes = 0, prox = 0;
    for (int i = 0; i < nt; ++i) {
        if (es_(t, nt, i, "semana")) semana = 1;
        if (es_(t, nt, i, "mes")) mes = 1;
        if (es_(t, nt, i, "proxima") || es_(t, nt, i, "proximo") || es_(t, nt, i, "siguiente") || (es_(t, nt, i, "que") && es_(t, nt, i + 1, "viene"))) prox = 1;
        if (es_(t, nt, i, "proximos") || es_(t, nt, i, "proximas")) {
            int64_t n = tnum(t, nt, i + 1);
            if (n >= 1 && n <= 366 && (es_(t, nt, i + 2, "dias") || es_(t, nt, i + 2, "dia"))) { *d0 = hoy; *d1 = hoy + n; return 0; }
            if (n >= 1 && n <= 52 && (es_(t, nt, i + 2, "semanas") || es_(t, nt, i + 2, "semana"))) { *d0 = hoy; *d1 = hoy + 7 * n; return 0; }
        }
    }
    if (semana) { int64_t lunes = hoy - ag_dia_semana(hoy); if (prox) lunes += 7; *d0 = lunes; *d1 = lunes + 7; return 0; }
    if (mes) { int y, m, d; ag_civil(hoy, &y, &m, &d); if (prox && ++m > 12) { m = 1; ++y; } *d0 = ag_dias(y, m, 1); *d1 = *d0 + ag_dias_mes(y, m); return 0; }
    struct pf r; r.dia = SIN_DIA; r.hora = -1; r.min = 0; r.dur = -1; r.rep = 0; r.cada = 1; r.rep_dsem = -1; r.antes = -2; r.rel_min = -1; r.ndias = 0;
    parsea_hora(t, nt, &r);
    if (parsea_dia(t, nt, ahora, &r) || r.dia == SIN_DIA) return -1;
    *d0 = r.dia; *d1 = r.dia + 1; return 0;
}
