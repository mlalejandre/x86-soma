/* SOMA_272_AGENDA: nucleo del calendario (ver agenda_core.h). Sin libc: se compila igual en el nucleo y en las pruebas del host. */
#include "agenda_core.h"

/* ------------------------------------------------------------------------------------------- cadenas */
static void put(char *o, uint32_t cap, uint32_t *p, const char *s) { while (*s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p] = 0; }
static void putc_(char *o, uint32_t cap, uint32_t *p, char c) { if (*p + 1 < cap) { o[(*p)++] = c; o[*p] = 0; } }
static void putn(char *o, uint32_t cap, uint32_t *p, int64_t v, int ancho)
{
    char b[24]; int n = 0; int neg = v < 0; uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
    do { b[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 22);
    while (n < ancho && n < 22) b[n++] = '0';
    if (neg) putc_(o, cap, p, '-');
    while (n) putc_(o, cap, p, b[--n]);
}
static void cpy(char *d, uint32_t cap, const char *s) { uint32_t i = 0; while (s[i] && i + 1 < cap) { d[i] = s[i]; ++i; } if (cap) d[i] = 0; }
static int eq(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static int empieza(const char *s, const char *pre) { while (*pre) if (*s++ != *pre++) return 0; return 1; }

/* ------------------------------------------------------------------------------------------- fechas */
int64_t ag_dias(int y, int m, int d)                    /* algoritmo de Howard Hinnant */
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
void ag_civil(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t dd = doy - (153 * mp + 2) / 5 + 1;
    int64_t mm = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yy + (mm <= 2)); *m = (int)mm; *d = (int)dd;
}
int ag_dia_semana(int64_t dias) { int64_t r = (dias + 3) % 7; if (r < 0) r += 7; return (int)r; }     /* 1970-01-01 fue jueves (3) */
int ag_dias_mes(int y, int m)
{
    static const int dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return (m >= 1 && m <= 12) ? dm[m - 1] : 30;
}
int64_t ag_min(int y, int mo, int d, int h, int mi) { return ag_dias(y, mo, d) * 1440 + h * 60 + mi; }
static int64_t fdiv(int64_t a, int64_t b) { int64_t q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q; }
int64_t ag_dia_de(int64_t min) { return fdiv(min, 1440); }
void ag_desde_min(int64_t min, int *y, int *mo, int *d, int *h, int *mi)
{
    int64_t dia = fdiv(min, 1440); int64_t r = min - dia * 1440;
    ag_civil(dia, y, mo, d); *h = (int)(r / 60); *mi = (int)(r % 60);
}
static int64_t ultimo_domingo(int y, int m)            /* dia (desde 1970) del ultimo domingo del mes */
{
    int64_t u = ag_dias(y, m, ag_dias_mes(y, m));
    return u - (ag_dia_semana(u) + 1) % 7;               /* domingo = 6 */
}
int64_t ag_utc_a_local(int64_t min_utc)
{
    int y, mo, d, h, mi; ag_desde_min(min_utc, &y, &mo, &d, &h, &mi);
    int64_t ini = ultimo_domingo(y, 3) * 1440 + 60, fin = ultimo_domingo(y, 10) * 1440 + 60;   /* 01:00 UTC */
    return min_utc + ((min_utc >= ini && min_utc < fin) ? 120 : 60);
}
static const char *const MESES[12] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre" };
static const char *const DIAS[7] = { "lunes", "martes", "mi\303\251rcoles", "jueves", "viernes", "s\303\241bado", "domingo" };
static const char *const CATS[AG_NCAT] = { "General", "Trabajo", "Deporte", "Salud", "Social", "Viaje", "Familia", "Estudio" };
const char *ag_nombre_mes(int m) { return (m >= 1 && m <= 12) ? MESES[m - 1] : "?"; }
const char *ag_nombre_dia(int d) { return (d >= 0 && d <= 6) ? DIAS[d] : "?"; }
const char *ag_nombre_cat(int c) { return (c >= 0 && c < AG_NCAT) ? CATS[c] : CATS[0]; }

/* ------------------------------------------------------------------------------------------- el calendario */
struct ag_ev *ag_nuevo(struct ag_cal *c)
{
    for (int i = 0; i < AG_MAX_EV; ++i) if (!c->ev[i].activo) {
        struct ag_ev *e = &c->ev[i]; char *p = (char *)e; for (uint32_t k = 0; k < sizeof *e; ++k) p[k] = 0;
        if (c->sig_id == 0) c->sig_id = 1;
        e->id = c->sig_id++; e->activo = 1; e->cada = 1; e->antes = -1;
        return e;
    }
    return 0;
}
struct ag_ev *ag_busca_id(struct ag_cal *c, uint32_t id)
{
    for (int i = 0; i < AG_MAX_EV; ++i) if (c->ev[i].activo && c->ev[i].id == id) return &c->ev[i];
    return 0;
}
int ag_borra(struct ag_cal *c, uint32_t id) { struct ag_ev *e = ag_busca_id(c, id); if (!e) return 0; e->activo = 0; e->id = 0; return 1; }
int ag_borra_ocurrencia(struct ag_cal *c, uint32_t id, int64_t dia)
{
    struct ag_ev *e = ag_busca_id(c, id);
    if (!e || e->rep == AG_REP_NO) return 0;
    for (int i = 0; i < AG_EXC; ++i) if (!e->exc[i]) { e->exc[i] = (int32_t)dia; return 1; }
    return 0;
}
uint32_t ag_numero(const struct ag_cal *c) { uint32_t n = 0; for (int i = 0; i < AG_MAX_EV; ++i) n += c->ev[i].activo; return n; }

static int excluida(const struct ag_ev *e, int64_t dia) { for (int i = 0; i < AG_EXC; ++i) if (e->exc[i] && e->exc[i] == dia) return 1; return 0; }

/* La celebracion numero k (k >= 0) de un evento que repite; -1 si no existe (mes sin ese dia...) */
static int64_t celebra(const struct ag_ev *e, int64_t k)
{
    int64_t dia0 = ag_dia_de(e->ini), hora = e->ini - dia0 * 1440; int64_t cada = e->cada ? e->cada : 1;
    switch (e->rep) {
    case AG_REP_NO: return k == 0 ? e->ini : -1;
    case AG_REP_DIA: return (dia0 + k * cada) * 1440 + hora;
    case AG_REP_SEM: return (dia0 + k * 7 * cada) * 1440 + hora;
    case AG_REP_LAB: {                                   /* lunes a viernes, uno tras otro (cada = 1) */
        int64_t d = dia0; int64_t n = 0;
        while (ag_dia_semana(d) > 4) ++d;
        int64_t sem = ag_dia_semana(d); int64_t base = d - sem;          /* lunes de esa semana */
        int64_t pos = sem + k;                                             /* posicion desde el lunes, 5 por semana */
        (void)n;
        return (base + (pos / 5) * 7 + pos % 5) * 1440 + hora;
    }
    case AG_REP_MES: case AG_REP_ANO: {
        int y, m, d; ag_civil(dia0, &y, &m, &d);
        int64_t paso = (e->rep == AG_REP_MES ? 1 : 12) * cada;
        int64_t mm = (int64_t)y * 12 + (m - 1) + k * paso;
        int yy = (int)(mm / 12), m2 = (int)(mm % 12) + 1;
        if (d > ag_dias_mes(yy, m2)) return -1;           /* el 31 no existe en abril: ese mes no se celebra */
        return ag_dias(yy, m2, d) * 1440 + hora;
    }
    }
    return -1;
}
int ag_en_rango(const struct ag_ev *e, int64_t desde, int64_t hasta, int64_t *out, int max)
{
    int n = 0; if (!e->activo || hasta <= desde) return 0;
    int64_t dur = e->todo_dia ? (e->dur >= 1440 ? (int64_t)e->dur : 1440) : (int64_t)e->dur; if (dur < 1) dur = 1;
    int64_t k0 = 0;
    if (e->rep == AG_REP_DIA || e->rep == AG_REP_SEM) {          /* salto directo: no recorrer anos de repeticiones */
        int64_t paso = (e->rep == AG_REP_DIA ? 1 : 7) * 1440 * (e->cada ? e->cada : 1);
        int64_t falta = desde - dur - e->ini; k0 = falta > 0 ? falta / paso : 0;
        if (k0 > 0) --k0;
    } else if (e->rep == AG_REP_LAB) { int64_t falta = ag_dia_de(desde - dur) - ag_dia_de(e->ini); k0 = falta > 0 ? falta / 7 * 5 - 5 : 0; if (k0 < 0) k0 = 0; }
    else if (e->rep == AG_REP_MES || e->rep == AG_REP_ANO) {
        int y1, m1, d1, y2, m2, d2; ag_civil(ag_dia_de(e->ini), &y1, &m1, &d1); ag_civil(ag_dia_de(desde - dur), &y2, &m2, &d2);
        int64_t meses = ((int64_t)y2 * 12 + m2) - ((int64_t)y1 * 12 + m1);
        int64_t paso = (e->rep == AG_REP_MES ? 1 : 12) * (e->cada ? e->cada : 1);
        k0 = meses > 0 ? meses / paso - 1 : 0; if (k0 < 0) k0 = 0;
    }
    for (int64_t k = k0; k < k0 + 100000 && n < max; ++k) {
        int64_t s = celebra(e, k);
        if (s < 0) { if (e->rep == AG_REP_NO) break; if (k > k0 + 2000) break; continue; }
        if (s >= hasta) break;
        if (e->hasta && ag_dia_de(s) > e->hasta) break;
        if (s + dur > desde && !excluida(e, ag_dia_de(s))) out[n++] = s;
        if (e->rep == AG_REP_NO) break;
    }
    return n;
}
int ag_agenda_rango(const struct ag_cal *c, int64_t desde, int64_t hasta, struct ag_oc *out, int max)
{
    int n = 0;
    for (int i = 0; i < AG_MAX_EV; ++i) {
        if (!c->ev[i].activo) continue;
        int64_t tmp[64]; int k = ag_en_rango(&c->ev[i], desde, hasta, tmp, 64);
        for (int j = 0; j < k && n < max; ++j) { out[n].ini = tmp[j]; out[n].idx = (uint16_t)i; ++n; }
    }
    for (int a = 1; a < n; ++a) {                             /* insercion: pocas filas */
        struct ag_oc x = out[a]; int b = a - 1;
        while (b >= 0 && (out[b].ini > x.ini || (out[b].ini == x.ini && c->ev[out[b].idx].id > c->ev[x.idx].id))) { out[b + 1] = out[b]; --b; }
        out[b + 1] = x;
    }
    return n;
}
int ag_proxima(const struct ag_cal *c, int64_t desde, struct ag_oc *oc)
{
    int hay = 0;
    for (int i = 0; i < AG_MAX_EV; ++i) {
        if (!c->ev[i].activo) continue;
        int64_t t[2]; int k = ag_en_rango(&c->ev[i], desde, desde + 366 * 1440 * 2, t, 2);
        for (int j = 0; j < k; ++j) if (t[j] >= desde && (!hay || t[j] < oc->ini)) { oc->ini = t[j]; oc->idx = (uint16_t)i; hay = 1; break; }
    }
    return hay;
}

/* ------------------------------------------------------------------------------------------- guardado propio
 * SOMA-AGENDA 1
 * E|id|AAAAMMDDhhmm|dur|todo|rep|cada|hasta(dias)|cat|antes|exc,exc|titulo|lugar|nota     ('|' y saltos de linea se cambian por espacio al guardar) */
static void limpia(char *o, uint32_t cap, const char *s)
{
    uint32_t i = 0; for (; s[i] && i + 1 < cap; ++i) o[i] = (s[i] == '|' || s[i] == '\n' || s[i] == '\r') ? ' ' : s[i];
    o[i] = 0;
}
uint32_t ag_serializa(const struct ag_cal *c, char *out, uint32_t cap)
{
    uint32_t p = 0; put(out, cap, &p, "SOMA-AGENDA 1\n");
    for (int i = 0; i < AG_MAX_EV; ++i) {
        const struct ag_ev *e = &c->ev[i]; if (!e->activo) continue;
        int y, mo, d, h, mi; ag_desde_min(e->ini, &y, &mo, &d, &h, &mi);
        char t[AG_TIT + 8], l[AG_LUG + 8], n[AG_NOTA + 8];
        limpia(t, sizeof t, e->titulo); limpia(l, sizeof l, e->lugar); limpia(n, sizeof n, e->nota);
        put(out, cap, &p, "E|"); putn(out, cap, &p, e->id, 0); putc_(out, cap, &p, '|');
        putn(out, cap, &p, y, 4); putn(out, cap, &p, mo, 2); putn(out, cap, &p, d, 2); putn(out, cap, &p, h, 2); putn(out, cap, &p, mi, 2); putc_(out, cap, &p, '|');
        putn(out, cap, &p, e->dur, 0); putc_(out, cap, &p, '|'); putn(out, cap, &p, e->todo_dia, 0); putc_(out, cap, &p, '|');
        putn(out, cap, &p, e->rep, 0); putc_(out, cap, &p, '|'); putn(out, cap, &p, e->cada, 0); putc_(out, cap, &p, '|');
        putn(out, cap, &p, e->hasta, 0); putc_(out, cap, &p, '|'); putn(out, cap, &p, e->cat, 0); putc_(out, cap, &p, '|');
        putn(out, cap, &p, e->antes, 0); putc_(out, cap, &p, '|');
        for (int k = 0, f = 0; k < AG_EXC; ++k) if (e->exc[k]) { if (f++) putc_(out, cap, &p, ','); putn(out, cap, &p, e->exc[k], 0); }
        putc_(out, cap, &p, '|'); put(out, cap, &p, t); putc_(out, cap, &p, '|'); put(out, cap, &p, l); putc_(out, cap, &p, '|'); put(out, cap, &p, n); putc_(out, cap, &p, '\n');
        if (p + 1 >= cap) return 0;
    }
    return p;
}
static int64_t num(const char **s, int *ok)              /* entero con signo; avanza */
{
    int64_t v = 0; int neg = 0; const char *q = *s; *ok = 0;
    if (*q == '-') { neg = 1; ++q; }
    while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); ++q; *ok = 1; }
    *s = q; return neg ? -v : v;
}
int ag_carga(struct ag_cal *c, const char *txt, uint32_t len)
{
    char *z = (char *)c; for (uint32_t k = 0; k < sizeof *c; ++k) z[k] = 0;
    c->sig_id = 1;
    if (len < 14 || !empieza(txt, "SOMA-AGENDA 1")) return -1;
    int n = 0; uint32_t i = 0;
    while (i < len) {
        uint32_t f = i; while (f < len && txt[f] != '\n') ++f;
        if (txt[i] == 'E' && txt[i + 1] == '|' && f - i > 20) {
            char ln_[AG_TIT + AG_LUG + AG_NOTA + 160]; uint32_t L = f - i < sizeof ln_ - 1 ? f - i : sizeof ln_ - 1;
            for (uint32_t k = 0; k < L; ++k) ln_[k] = txt[i + k];
            ln_[L] = 0;
            const char *q = ln_ + 2; int ok = 1, o1; struct ag_ev ev; char *pe = (char *)&ev; for (uint32_t k = 0; k < sizeof ev; ++k) pe[k] = 0;
            int64_t id = num(&q, &o1); ok &= o1; if (*q++ != '|') ok = 0;
            char f12[13]; for (int k = 0; k < 12; ++k) { f12[k] = q[k]; if (q[k] < '0' || q[k] > '9') ok = 0; } f12[12] = 0; q += 12; if (*q++ != '|') ok = 0;
            if (ok) {
                int yy = (f12[0] - '0') * 1000 + (f12[1] - '0') * 100 + (f12[2] - '0') * 10 + (f12[3] - '0'), mm = (f12[4] - '0') * 10 + (f12[5] - '0'), dd = (f12[6] - '0') * 10 + (f12[7] - '0');
                int hh = (f12[8] - '0') * 10 + (f12[9] - '0'), mi = (f12[10] - '0') * 10 + (f12[11] - '0');
                if (mm < 1 || mm > 12 || dd < 1 || dd > ag_dias_mes(yy, mm) || hh > 23 || mi > 59) ok = 0; else ev.ini = ag_min(yy, mm, dd, hh, mi);
            }
            int64_t v[7];
            for (int k = 0; k < 7 && ok; ++k) { v[k] = num(&q, &o1); ok &= o1; if (*q++ != '|') ok = 0; }
            if (ok) {
                ev.id = (uint32_t)id; ev.dur = (uint32_t)v[0]; ev.todo_dia = v[1] != 0; ev.rep = (uint8_t)v[2]; ev.cada = (uint16_t)(v[3] < 1 ? 1 : v[3]); ev.hasta = v[4];
                ev.cat = (uint8_t)(v[5] < AG_NCAT ? v[5] : 0); ev.antes = (int16_t)v[6]; ev.activo = 1;
                if (ev.rep > AG_REP_LAB || id < 1) ok = 0;
                for (int k = 0; k < AG_EXC && *q && *q != '|'; ++k) { int64_t x = num(&q, &o1); if (o1) ev.exc[k] = (int32_t)x; if (*q == ',') ++q; }
                if (*q++ != '|') ok = 0;
            }
            if (ok) {
                char *campo[3] = { ev.titulo, ev.lugar, ev.nota }; uint32_t cc[3] = { AG_TIT, AG_LUG, AG_NOTA };
                for (int k = 0; k < 3; ++k) { uint32_t m = 0; while (*q && *q != '|') { if (m + 1 < cc[k]) campo[k][m++] = *q; ++q; } campo[k][m] = 0; if (*q == '|') ++q; }
            }
            if (ok && !ag_busca_id(c, ev.id)) {
                for (int k = 0; k < AG_MAX_EV; ++k) if (!c->ev[k].activo) { c->ev[k] = ev; if (ev.id >= c->sig_id) c->sig_id = ev.id + 1; ++n; break; }
            }
        }
        i = f + 1;
    }
    return n;
}

/* ------------------------------------------------------------------------------------------- texto */
void ag_hora_txt(int64_t min, char *o, uint32_t cap)
{
    int y, mo, d, h, mi; ag_desde_min(min, &y, &mo, &d, &h, &mi); uint32_t p = 0; o[0] = 0;
    putn(o, cap, &p, h, 2); putc_(o, cap, &p, ':'); putn(o, cap, &p, mi, 2);
}
void ag_fecha_txt(int64_t min, int64_t ahora, char *o, uint32_t cap)
{
    int64_t dia = ag_dia_de(min), hoy = ag_dia_de(ahora), dd = dia - hoy; uint32_t p = 0; o[0] = 0;
    if (dd == 0) { put(o, cap, &p, "hoy"); return; }
    if (dd == 1) { put(o, cap, &p, "ma\303\261ana"); return; }
    if (dd == -1) { put(o, cap, &p, "ayer"); return; }
    int y, m, d, h, mi, y0, m0, d0; ag_desde_min(min, &y, &m, &d, &h, &mi); ag_desde_min(ahora, &y0, &m0, &d0, &h, &mi);
    put(o, cap, &p, ag_nombre_dia(ag_dia_semana(dia))); putc_(o, cap, &p, ' '); putn(o, cap, &p, d, 0); put(o, cap, &p, " de "); put(o, cap, &p, ag_nombre_mes(m));
    if (y != y0) { put(o, cap, &p, " de "); putn(o, cap, &p, y, 0); }
}
static void mayus(char *s) { if (s[0] >= 'a' && s[0] <= 'z') s[0] = (char)(s[0] - 32); }
void ag_describe(const struct ag_ev *e, int64_t ini, int64_t ahora, char *o, uint32_t cap)
{
    char f[64], h[16]; uint32_t p = 0; o[0] = 0;
    ag_fecha_txt(ini, ahora, f, sizeof f); mayus(f);
    put(o, cap, &p, e->titulo[0] ? e->titulo : "Sin titulo"); put(o, cap, &p, " \342\200\224 "); put(o, cap, &p, f);
    if (e->todo_dia && e->dur >= 2880) { put(o, cap, &p, ", "); putn(o, cap, &p, e->dur / 1440, 0); put(o, cap, &p, " d\303\255as"); }
    else if (e->todo_dia) put(o, cap, &p, ", todo el d\303\255a");
    else {
        ag_hora_txt(ini, h, sizeof h); put(o, cap, &p, " a las "); put(o, cap, &p, h);
        if (e->dur) { ag_hora_txt(ini + e->dur, h, sizeof h); put(o, cap, &p, "-"); put(o, cap, &p, h); }
    }
    if (e->lugar[0]) { put(o, cap, &p, " en "); put(o, cap, &p, e->lugar); }
}
void ag_dicho(const struct ag_ev *e, int64_t ini, int64_t ahora, int64_t para, char *o, uint32_t cap)
{
    uint32_t p = 0; o[0] = 0; (void)ahora;
    if (para <= 0) put(o, cap, &p, "Ahora: "); else if (para == 1) put(o, cap, &p, "En un minuto: ");
    else if (para < 60) { put(o, cap, &p, "En "); putn(o, cap, &p, para, 0); put(o, cap, &p, " minutos: "); }
    else if (para % 60 == 0 && para <= 180) { put(o, cap, &p, "En "); putn(o, cap, &p, para / 60, 0); put(o, cap, &p, para == 60 ? " hora: " : " horas: "); }
    else { put(o, cap, &p, "Pr\303\263ximamente: "); }
    put(o, cap, &p, e->titulo[0] ? e->titulo : "un evento");
    if (!e->todo_dia) { char h[16]; ag_hora_txt(ini, h, sizeof h); put(o, cap, &p, ", a las "); put(o, cap, &p, h); }
    if (e->lugar[0]) { put(o, cap, &p, ", en "); put(o, cap, &p, e->lugar); }
    put(o, cap, &p, ".");
}
uint32_t ag_lista_txt(const struct ag_cal *c, int64_t d0, int64_t d1, int64_t ahora, char *o, uint32_t cap)
{
    static struct ag_oc oc[256]; uint32_t p = 0; o[0] = 0;
    int n = ag_agenda_rango(c, d0 * 1440, d1 * 1440, oc, 256);
    if (!n) { put(o, cap, &p, "No hay nada apuntado en ese tramo.\n"); return p; }
    int64_t dia_ant = -1;
    for (int i = 0; i < n; ++i) {
        const struct ag_ev *e = &c->ev[oc[i].idx]; int64_t dia = ag_dia_de(oc[i].ini);
        if (dia != dia_ant) {
            char f[64]; ag_fecha_txt(oc[i].ini, ahora, f, sizeof f); mayus(f); dia_ant = dia;
            int y, m, d, h, mi; ag_desde_min(oc[i].ini, &y, &m, &d, &h, &mi);
            put(o, cap, &p, f);
            if (f[0] == 'H' || f[0] == 'M' || f[0] == 'A') { put(o, cap, &p, ", "); put(o, cap, &p, ag_nombre_dia(ag_dia_semana(dia))); putc_(o, cap, &p, ' '); putn(o, cap, &p, d, 0); put(o, cap, &p, " de "); put(o, cap, &p, ag_nombre_mes(m)); }
            put(o, cap, &p, ":\n");
        }
        char h[16], h2[16]; ag_hora_txt(oc[i].ini, h, sizeof h);
        put(o, cap, &p, "  ["); putn(o, cap, &p, e->id, 0); put(o, cap, &p, "] ");
        if (e->todo_dia) put(o, cap, &p, "todo el d\303\255a "); else { put(o, cap, &p, h); if (e->dur) { ag_hora_txt(oc[i].ini + e->dur, h2, sizeof h2); put(o, cap, &p, "-"); put(o, cap, &p, h2); } putc_(o, cap, &p, ' '); }
        put(o, cap, &p, e->titulo[0] ? e->titulo : "Sin titulo");
        if (e->lugar[0]) { put(o, cap, &p, " ("); put(o, cap, &p, e->lugar); putc_(o, cap, &p, ')'); }
        if (e->rep) put(o, cap, &p, " \342\206\273");
        putc_(o, cap, &p, '\n');
    }
    return p;
}

/* ------------------------------------------------------------------------------------------- avisos */
int ag_aviso_siguiente(const struct ag_cal *c, struct ag_avisos *st, int64_t ahora, int antes_def, uint32_t *id, int64_t *ini)
{
    for (int i = 0; i < AG_MAX_EV; ++i) {
        const struct ag_ev *e = &c->ev[i]; if (!e->activo) continue;
        int antes = e->antes < 0 ? antes_def : e->antes;
        if (antes <= 0) continue;
        int64_t desfase = e->todo_dia ? 540 : 0;                       /* los de todo el dia avisan a las 09:00 de ese dia */
        int64_t t[4]; int k = ag_en_rango(e, ahora - desfase, ahora - desfase + antes + 1, t, 4);
        for (int j = 0; j < k; ++j) {
            int64_t s = t[j] + desfase;
            if (s < ahora || s - ahora > antes) continue;
            if (e->todo_dia && ahora - s > 0) continue;
            int ya = 0; uint32_t m = st->n < AG_VISTOS ? st->n : AG_VISTOS;
            for (uint32_t v = 0; v < m; ++v) if (st->v[v].id == e->id && st->v[v].ini == t[j]) ya = 1;
            if (ya) continue;
            uint32_t pos = st->n % AG_VISTOS; st->v[pos].id = e->id; st->v[pos].ini = t[j]; if (st->n < 0xFFFFFFF0u) ++st->n;
            *id = e->id; *ini = t[j]; return 1;
        }
    }
    return 0;
}

#include "agenda_nl.inc"
#include "agenda_ics.inc"
