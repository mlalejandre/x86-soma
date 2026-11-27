/* SOMA_272_AGENDA: .ics (iCalendar, RFC 5545) para exportar a y sincronizar con Google Calendar, Outlook, Apple... (se incluye desde agenda_core.c)
 * Las horas se guardan "flotantes" (sin zona), como las de SOMA: cada calendario las lee como hora local. */
static void ics_texto(char *o, uint32_t cap, uint32_t *p, const char *s)
{
    for (; *s; ++s) {
        if (*s == '\\' || *s == ';' || *s == ',') putc_(o, cap, p, '\\');
        if (*s == '\n' || *s == '\r') { putc_(o, cap, p, ' '); continue; }
        putc_(o, cap, p, *s);
    }
}
static void ics_fecha(char *o, uint32_t cap, uint32_t *p, int64_t min, int con_hora)
{
    int y, mo, d, h, mi; ag_desde_min(min, &y, &mo, &d, &h, &mi);
    putn(o, cap, p, y, 4); putn(o, cap, p, mo, 2); putn(o, cap, p, d, 2);
    if (con_hora) { putc_(o, cap, p, 'T'); putn(o, cap, p, h, 2); putn(o, cap, p, mi, 2); put(o, cap, p, "00"); }
}
uint32_t ag_ics_exporta(const struct ag_cal *c, char *o, uint32_t cap)
{
    uint32_t p = 0; o[0] = 0;
    put(o, cap, &p, "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//SOMA//Agenda//ES\r\nCALSCALE:GREGORIAN\r\nX-WR-CALNAME:SOMA\r\n");
    for (int i = 0; i < AG_MAX_EV; ++i) {
        const struct ag_ev *e = &c->ev[i]; if (!e->activo) continue;
        if (empieza(e->nota, "contacto:")) continue;      /* SOMA_357_CONTACTOS: los cumpleanos salen de los contactos (van en el .vcf, no aqui) */
        put(o, cap, &p, "BEGIN:VEVENT\r\nUID:"); putn(o, cap, &p, e->id, 0); put(o, cap, &p, "@soma\r\nDTSTAMP:"); ics_fecha(o, cap, &p, e->ini, 1); put(o, cap, &p, "Z\r\n");
        if (e->todo_dia) { put(o, cap, &p, "DTSTART;VALUE=DATE:"); ics_fecha(o, cap, &p, e->ini, 0); put(o, cap, &p, "\r\nDTEND;VALUE=DATE:"); ics_fecha(o, cap, &p, e->ini + (e->dur >= 1440 ? (int64_t)e->dur : 1440), 0); put(o, cap, &p, "\r\n"); }
        else { put(o, cap, &p, "DTSTART:"); ics_fecha(o, cap, &p, e->ini, 1); put(o, cap, &p, "\r\n"); if (e->dur) { put(o, cap, &p, "DTEND:"); ics_fecha(o, cap, &p, e->ini + e->dur, 1); put(o, cap, &p, "\r\n"); } }
        put(o, cap, &p, "SUMMARY:"); ics_texto(o, cap, &p, e->titulo); put(o, cap, &p, "\r\n");
        if (e->lugar[0]) { put(o, cap, &p, "LOCATION:"); ics_texto(o, cap, &p, e->lugar); put(o, cap, &p, "\r\n"); }
        if (e->nota[0]) { put(o, cap, &p, "DESCRIPTION:"); ics_texto(o, cap, &p, e->nota); put(o, cap, &p, "\r\n"); }
        if (e->cat) { put(o, cap, &p, "CATEGORIES:"); put(o, cap, &p, ag_nombre_cat(e->cat)); put(o, cap, &p, "\r\n"); }
        if (e->rep) {
            static const char *const F[] = { "", "DAILY", "WEEKLY", "MONTHLY", "YEARLY", "WEEKLY" };
            put(o, cap, &p, "RRULE:FREQ="); put(o, cap, &p, F[e->rep]);
            if (e->rep == AG_REP_LAB) put(o, cap, &p, ";BYDAY=MO,TU,WE,TH,FR"); else if (e->cada > 1) { put(o, cap, &p, ";INTERVAL="); putn(o, cap, &p, e->cada, 0); }
            if (e->hasta) { put(o, cap, &p, ";UNTIL="); ics_fecha(o, cap, &p, e->hasta * 1440, 0); }
            put(o, cap, &p, "\r\n");
        }
        for (int k = 0; k < AG_EXC; ++k) if (e->exc[k]) {
            put(o, cap, &p, e->todo_dia ? "EXDATE;VALUE=DATE:" : "EXDATE:");
            ics_fecha(o, cap, &p, (int64_t)e->exc[k] * 1440 + (e->ini - ag_dia_de(e->ini) * 1440), !e->todo_dia); put(o, cap, &p, "\r\n");
        }
        if (e->antes > 0) { put(o, cap, &p, "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:Recordatorio\r\nTRIGGER:-PT"); putn(o, cap, &p, e->antes, 0); put(o, cap, &p, "M\r\nEND:VALARM\r\n"); }
        put(o, cap, &p, "END:VEVENT\r\n");
        if (p + 200 >= cap) return 0;
    }
    put(o, cap, &p, "END:VCALENDAR\r\n");
    return p + 1 < cap ? p : 0;
}

/* ---- importar */
static int mayu(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static int igual_ci(const char *a, const char *b) { while (*a && *b && mayu((unsigned char)*a) == mayu((unsigned char)*b)) { ++a; ++b; } return !*a && !*b; }
static int ini_ci(const char *a, const char *b) { while (*b && mayu((unsigned char)*a) == mayu((unsigned char)*b)) { ++a; ++b; } return !*b; }
/* AAAAMMDD[THHMMSS[Z]] -> minutos locales; *solo_fecha = 1 si no trae hora; -1 si no es una fecha */
static int64_t ics_fecha_lee(const char *v, int *solo_fecha, int param_date)
{
    int d[14]; int n = 0; const char *q = v;
    while (*q >= '0' && *q <= '9' && n < 8) d[n++] = *q++ - '0';
    if (n != 8) return -1;
    int y = d[0] * 1000 + d[1] * 100 + d[2] * 10 + d[3], mo = d[4] * 10 + d[5], da = d[6] * 10 + d[7], h = 0, mi = 0, z = 0;
    if (mo < 1 || mo > 12 || da < 1 || da > ag_dias_mes(y, mo)) return -1;
    *solo_fecha = 1;
    if (*q == 'T' && !param_date) {
        ++q; int m = 0, e[6]; while (*q >= '0' && *q <= '9' && m < 6) e[m++] = *q++ - '0';
        if (m >= 4) { h = e[0] * 10 + e[1]; mi = e[2] * 10 + e[3]; *solo_fecha = 0; if (h > 23 || mi > 59) return -1; }
        if (*q == 'Z') z = 1;
    }
    int64_t t = ag_min(y, mo, da, h, mi);
    return z ? ag_utc_a_local(t) : t;
}
static int64_t ics_duracion(const char *v)               /* PT1H30M, P1D, -PT10M: minutos (positivos) */
{
    int64_t tot = 0, n = 0; int tiempo = 0;
    if (*v == '-' || *v == '+') ++v;
    if (*v != 'P') return -1;
    ++v;
    for (; *v; ++v) {
        if (*v == 'T') { tiempo = 1; continue; }
        if (*v >= '0' && *v <= '9') { n = n * 10 + (*v - '0'); continue; }
        if (*v == 'W') tot += n * 7 * 1440; else if (*v == 'D') tot += n * 1440; else if (*v == 'H') tot += n * 60; else if (*v == 'M' && tiempo) tot += n; else if (*v == 'S') tot += 0;
        n = 0;
    }
    return tot;
}
static void ics_destexto(char *o, uint32_t cap, const char *v)
{
    uint32_t p = 0;
    for (; *v && p + 1 < cap; ++v) {
        if (*v == '\\' && v[1]) { ++v; o[p++] = (*v == 'n' || *v == 'N') ? ' ' : *v; }
        else o[p++] = *v;
    }
    o[p] = 0;
}
struct ics_ev { struct ag_ev e; int tiene_ini, fin_valido; int64_t fin; int solo_fecha; int count; };
static void ics_linea(struct ag_cal *c, struct ics_ev *s, int *dentro, int *alarma, int *n, char *l)
{
    char *dp = l; while (*dp && *dp != ':' && *dp != ';') ++dp;
    char *val = dp; while (*val && *val != ':') ++val;      /* los parametros van hasta los dos puntos */
    if (!*val) return;
    char nombre[24]; uint32_t k = 0; for (char *q = l; q < dp && k + 1 < sizeof nombre; ++q) nombre[k++] = *q; nombre[k] = 0;
    int param_date = 0; for (char *q = dp; q < val; ++q) if (ini_ci(q, "VALUE=DATE") && !ini_ci(q, "VALUE=DATE-TIME")) param_date = 1;
    ++val;
    if (igual_ci(nombre, "BEGIN")) {
        if (igual_ci(val, "VEVENT")) { char *z = (char *)s; for (uint32_t i = 0; i < sizeof *s; ++i) z[i] = 0; s->e.cada = 1; s->e.antes = -1; *dentro = 1; }
        else if (igual_ci(val, "VALARM") && *dentro) *alarma = 1;
        return;
    }
    if (igual_ci(nombre, "END")) {
        if (igual_ci(val, "VALARM")) { *alarma = 0; return; }
        if (igual_ci(val, "VEVENT") && *dentro) {
            *dentro = 0;
            if (!s->tiene_ini) return;
            struct ag_ev *e = ag_nuevo(c); if (!e) return;
            uint32_t id = e->id; *e = s->e; e->id = id; e->activo = 1; e->todo_dia = (uint8_t)s->solo_fecha;
            if (s->fin_valido == 1 && s->fin > e->ini) e->dur = (uint32_t)(s->fin - e->ini);          /* con horas: duracion; de todo el dia: dias seguidos */
            if (s->solo_fecha && e->dur < 2880) e->dur = 0;
            if (s->count > 0 && e->rep) { struct ag_ev tmp = *e; int64_t u = celebra(&tmp, s->count - 1); if (u >= 0) e->hasta = ag_dia_de(u); }
            if (!e->titulo[0]) cpy(e->titulo, AG_TIT, "Sin titulo");
            ++*n;
        }
        return;
    }
    if (!*dentro) return;
    if (*alarma) { if (igual_ci(nombre, "TRIGGER")) { int64_t m = ics_duracion(val); if (m >= 0 && val[0] == '-') s->e.antes = (int16_t)(m > 32000 ? 32000 : m); else if (m == 0) s->e.antes = 0; } return; }
    if (igual_ci(nombre, "DTSTART")) { int sf; int64_t t = ics_fecha_lee(val, &sf, param_date); if (t >= 0) { s->e.ini = t; s->tiene_ini = 1; s->solo_fecha = sf; } }
    else if (igual_ci(nombre, "DTEND")) { int sf; int64_t t = ics_fecha_lee(val, &sf, param_date); if (t >= 0) { s->fin = t; s->fin_valido = 1; } }
    else if (igual_ci(nombre, "DURATION")) { int64_t m = ics_duracion(val); if (m > 0) { s->fin_valido = -1; s->e.dur = (uint32_t)m; } }
    else if (igual_ci(nombre, "SUMMARY")) ics_destexto(s->e.titulo, AG_TIT, val);
    else if (igual_ci(nombre, "LOCATION")) ics_destexto(s->e.lugar, AG_LUG, val);
    else if (igual_ci(nombre, "DESCRIPTION")) ics_destexto(s->e.nota, AG_NOTA, val);
    else if (igual_ci(nombre, "CATEGORIES")) { for (int cc = 1; cc < AG_NCAT; ++cc) if (ini_ci(val, ag_nombre_cat(cc))) s->e.cat = (uint8_t)cc; }
    else if (igual_ci(nombre, "RRULE")) {
        for (char *q = val; *q;) {
            if (ini_ci(q, "FREQ=")) { char *f = q + 5; s->e.rep = ini_ci(f, "DAILY") ? AG_REP_DIA : ini_ci(f, "WEEKLY") ? AG_REP_SEM : ini_ci(f, "MONTHLY") ? AG_REP_MES : ini_ci(f, "YEARLY") ? AG_REP_ANO : AG_REP_NO; }
            else if (ini_ci(q, "INTERVAL=")) { int ok; const char *z = q + 9; int64_t v = num(&z, &ok); if (ok && v >= 1 && v < 1000) s->e.cada = (uint16_t)v; }
            else if (ini_ci(q, "COUNT=")) { int ok; const char *z = q + 6; int64_t v = num(&z, &ok); if (ok && v >= 1 && v < 100000) s->count = (int)v; }
            else if (ini_ci(q, "UNTIL=")) { int sf; int64_t t = ics_fecha_lee(q + 6, &sf, 0); if (t >= 0) s->e.hasta = ag_dia_de(t); }
            else if (ini_ci(q, "BYDAY=MO,TU,WE,TH,FR")) { s->e.rep = AG_REP_LAB; }
            while (*q && *q != ';') ++q;
            if (*q == ';') ++q;
        }
    } else if (igual_ci(nombre, "EXDATE")) {
        for (char *q = val; *q;) { int sf; int64_t t = ics_fecha_lee(q, &sf, 0); if (t >= 0) for (int i = 0; i < AG_EXC; ++i) if (!s->e.exc[i]) { s->e.exc[i] = (int32_t)ag_dia_de(t); break; } while (*q && *q != ',') ++q; if (*q == ',') ++q; }
    }
}
int ag_ics_importa(struct ag_cal *c, const char *txt, uint32_t len)
{
    static struct ics_ev s; static char lg[520];
    int dentro = 0, alarma = 0, n = 0, hay = 0; uint32_t ll = 0, i = 0;
    for (uint32_t k = 0; k + 15 <= len && k < 400; ++k) if (ini_ci(txt + k, "BEGIN:VCALENDAR")) { hay = 1; break; }
    if (!hay) return -1;
    while (i <= len) {
        uint32_t f = i; while (f < len && txt[f] != '\n') ++f;
        uint32_t e = f; if (e > i && txt[e - 1] == '\r') --e;
        int continua = (f + 1 < len) && (txt[f + 1] == ' ' || txt[f + 1] == '\t');
        for (uint32_t k = i; k < e && ll + 1 < sizeof lg; ++k) lg[ll++] = txt[k];
        if (!continua) { lg[ll] = 0; if (ll) ics_linea(c, &s, &dentro, &alarma, &n, lg); ll = 0; }
        else { i = f + 2; if (i > len) break; continue; }       /* salta el espacio de continuacion */
        i = f + 1;
        if (f >= len) break;
    }
    return n;
}
