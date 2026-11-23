/* SOMA_272_AGENDA: ver agenda.h */
#include "agenda.h"
#include "fs.h"
#include "mem.h"
#include "rtc.h"
#include "idt.h"

#define AG_FICHERO "/agenda/eventos.txt"
#define AG_CONF    "/etc/agenda.conf"
#define AG_ICS     "/usuario/agenda.ics"

static struct ag_cal g_cal;
static int g_cargado = 0;
static uint32_t g_ver = 1;
static struct agenda_conf g_conf = { 1, 1, 1, 10 };
static int g_conf_ok = 0;
static struct ag_avisos g_vistos;

/* ---- cadenas */
static uint32_t sl(const char *s) { uint32_t n = 0; while (s[n]) ++n; return n; }
static void put(char *o, uint32_t cap, uint32_t *p, const char *s) { while (*s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p] = 0; }
static void putn(char *o, uint32_t cap, uint32_t *p, int64_t v) { char b[24]; int n = 0; uint64_t u = v < 0 ? (uint64_t)(-v) : (uint64_t)v; if (v < 0) put(o, cap, p, "-"); do { b[n++] = (char)('0' + u % 10); u /= 10; } while (u && n < 22); while (n) { char c[2] = { b[--n], 0 }; put(o, cap, p, c); } }
static char baja(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int emp(const char *s, const char *pre) { while (*pre) { if (baja(*s) != *pre) return 0; ++s; ++pre; } return 1; }     /* sin distinguir mayusculas; pre en minuscula */
static const char *sin_blancos(const char *s) { while (*s == ' ') ++s; return s; }
static const char *palabra(const char *s, const char *w) { s = sin_blancos(s); uint32_t n = sl(w); if (!emp(s, w)) return 0; if (s[n] && s[n] != ' ') return 0; return sin_blancos(s + n); }
/* las dos grafias de "añadir" (con ñ en UTF-8 o sin ella) */
static const char *palabra_anadir(const char *s)
{
    static const char *const W[] = { "a\303\261adir", "anadir", "a\303\261ade", "anade", "nuevo", "nueva", "apunta", "apuntar", "add", "crea", "crear", "pon", "agenda", 0 };
    for (int i = 0; W[i]; ++i) { const char *r = palabra(s, W[i]); if (r) return r; }
    return 0;
}
static uint32_t atoi_(const char *s, int *ok) { uint32_t v = 0; *ok = 0; s = sin_blancos(s); while (*s >= '0' && *s <= '9' && v < 100000000u) { v = v * 10 + (uint32_t)(*s - '0'); ++s; *ok = 1; } return v; }

/* ---- estado */
uint32_t agenda_version(void) { return g_ver; }
int64_t agenda_ahora(void)
{
    static int64_t base_utc = 0; static uint64_t base_ms = 0; static int ok = 0;
    uint64_t ms = timer_get_uptime_ms();
    if (!ok || ms - base_ms > 60000u) {                          /* el RTC se lee de vez en cuando; entre lecturas cuenta el reloj del sistema */
        struct rtc_time t; rtc_get_datetime(&t);
        base_utc = ag_min(t.year, t.month, t.day, t.hour, t.min); base_ms = ms - (uint64_t)t.sec * 1000u; ok = 1;
    }
    return ag_utc_a_local(base_utc + (int64_t)((ms - base_ms) / 60000u));
}
struct ag_cal *agenda_cal(void)
{
    if (!g_cargado) {
        g_cargado = 1; g_cal.sig_id = 1;
        uint32_t n = 0; char *b = vfs_read_alloc(AG_FICHERO, &n);
        if (b) { (void)ag_carga(&g_cal, b, n); kfree(b); }
    }
    return &g_cal;
}
int agenda_guarda(void)
{
    uint32_t cap = AG_MAX_EV * 320u + 64u; char *b = (char *)kmalloc(cap);
    if (!b) return -1;
    uint32_t n = ag_serializa(agenda_cal(), b, cap);
    int r = n ? vfs_write(AG_FICHERO, b, n) : -1;
    kfree(b); ++g_ver;
    return r < 0 ? -1 : 0;
}
void agenda_conf_get(struct agenda_conf *c)
{
    if (!g_conf_ok) {
        g_conf_ok = 1; char b[160]; int n = vfs_read(AG_CONF, b, sizeof b - 1);
        if (n > 0) {
            b[n] = 0;
            for (const char *p = b; *p;) {
                int ok; uint32_t v;
                if (emp(p, "avisos=")) { v = atoi_(p + 7, &ok); if (ok) g_conf.avisos = v != 0; }
                else if (emp(p, "voz=")) { v = atoi_(p + 4, &ok); if (ok) g_conf.voz = v != 0; }
                else if (emp(p, "visual=")) { v = atoi_(p + 7, &ok); if (ok) g_conf.visual = v != 0; }
                else if (emp(p, "antes=")) { v = atoi_(p + 6, &ok); if (ok && v <= 1440) g_conf.antes = (int)v; }
                while (*p && *p != '\n') ++p;
                if (*p) ++p;
            }
        }
    }
    *c = g_conf;
}
void agenda_conf_set(const struct agenda_conf *c)
{
    struct agenda_conf tmp; agenda_conf_get(&tmp);
    g_conf = *c; if (g_conf.antes < 0) g_conf.antes = 0; if (g_conf.antes > 1440) g_conf.antes = 1440;
    char b[96]; uint32_t p = 0; b[0] = 0;
    put(b, sizeof b, &p, "avisos="); putn(b, sizeof b, &p, g_conf.avisos); put(b, sizeof b, &p, "\nvoz="); putn(b, sizeof b, &p, g_conf.voz);
    put(b, sizeof b, &p, "\nvisual="); putn(b, sizeof b, &p, g_conf.visual); put(b, sizeof b, &p, "\nantes="); putn(b, sizeof b, &p, g_conf.antes); put(b, sizeof b, &p, "\n");
    vfs_write(AG_CONF, b, p); ++g_ver;
}

int agenda_anade_texto(const char *txt, char *resumen, uint32_t cap)
{
    struct ag_ev ev; int64_t ahora = agenda_ahora();
    if (ag_parsea(txt, ahora, &ev)) return -1;
    struct ag_ev *e = ag_nuevo(agenda_cal()); if (!e) return -2;
    uint32_t id = e->id; *e = ev; e->id = id; e->activo = 1;
    if (agenda_guarda()) { ag_borra(agenda_cal(), id); return -3; }
    if (resumen) { char d[240]; uint32_t p = 0; resumen[0] = 0; put(resumen, cap, &p, "Apuntado ["); putn(resumen, cap, &p, id); put(resumen, cap, &p, "]: "); ag_describe(e, e->ini, ahora, d, sizeof d); put(resumen, cap, &p, d);
        if (e->rep) put(resumen, cap, &p, e->rep == AG_REP_DIA ? " (se repite cada d\303\255a)" : e->rep == AG_REP_SEM ? " (se repite cada semana)" : e->rep == AG_REP_MES ? " (se repite cada mes)" : e->rep == AG_REP_ANO ? " (se repite cada a\303\261o)" : " (de lunes a viernes)"); }
    return (int)id;
}
int agenda_borra(uint32_t id) { if (!ag_borra(agenda_cal(), id)) return -1; return agenda_guarda(); }

/* ---- avisos (lo llama la GUI) */
int agenda_tick(char *texto, uint32_t cap)
{
    static uint64_t ultimo = 0; uint64_t ms = timer_get_uptime_ms();
    if (ms - ultimo < 1000u) return 0;
    ultimo = ms;
    struct agenda_conf c; agenda_conf_get(&c);
    if (!c.avisos) return 0;
    uint32_t id; int64_t ini; int64_t ahora = agenda_ahora();
    if (!ag_aviso_siguiente(agenda_cal(), &g_vistos, ahora, c.antes, &id, &ini)) return 0;
    struct ag_ev *e = ag_busca_id(agenda_cal(), id); if (!e) return 0;
    ag_dicho(e, ini, ahora, ini - ahora, texto, cap);
    return 1;
}

/* ---- la orden `agenda` */
static const char *AYUDA =
    "Agenda: apunta y consulta tus eventos. Entiende fechas en castellano.\n"
    "  agenda                       hoy y los proximos dias\n"
    "  agenda hoy | ma\303\261ana | semana | mes | proximos N dias | ver <fecha>    (ver el miercoles, ver 6 de enero)\n"
    "  agenda a\303\261adir <texto>       agenda a\303\261adir el miercoles dia 6 a las 13 horas partido de futbol con los amigos\n"
    "                               agenda a\303\261adir cada lunes a las 8 gimnasio  |  agenda a\303\261adir dentro de 2 horas llamar a Ana\n"
    "  agenda borrar <n>            quita el evento n (los numeros salen entre corchetes)\n"
    "  agenda saltar <n> <fecha>    quita solo ese dia de un evento que se repite\n"
    "  agenda mover <n> <fecha>     cambia el dia y la hora (agenda mover 3 el viernes a las 18)\n"
    "  agenda buscar <texto> | agenda detalle <n>\n"
    "  agenda exportar [ruta.ics]   (por defecto /usuario/agenda.ics: para Google Calendar, Outlook...)\n"
    "  agenda importar <ruta.ics>\n"
    "  agenda avisos [on|off] | agenda avisos voz on|off | visual on|off | antes N | probar     (tambien en Ajustes > Avisos)\n";

static int lista(int64_t d0, int64_t d1, char *out, uint32_t max) { return (int)ag_lista_txt(agenda_cal(), d0, d1, agenda_ahora(), out, max); }

int agenda_cmd(const char *args, char *out, uint32_t max)
{
    uint32_t p = 0; out[0] = 0; if (!args) args = "";
    args = sin_blancos(args);
    int64_t ahora = agenda_ahora(), hoy = ag_dia_de(ahora), d0, d1; const char *r;
    struct ag_cal *c = agenda_cal();
    if (!*args || palabra(args, "estado")) {
        struct agenda_conf cf; agenda_conf_get(&cf); put(out, max, &p, "Agenda: "); putn(out, max, &p, ag_numero(c)); put(out, max, &p, " eventos. Avisos: ");
        put(out, max, &p, cf.avisos ? "activados" : "desactivados"); put(out, max, &p, cf.avisos ? " (" : ""); if (cf.avisos) { putn(out, max, &p, cf.antes); put(out, max, &p, " min antes; "); put(out, max, &p, cf.voz ? "voz" : "sin voz"); put(out, max, &p, ", "); put(out, max, &p, cf.visual ? "en pantalla)" : "sin pantalla)"); }
        put(out, max, &p, "\n");
        { char t[1500]; (void)lista(hoy, hoy + 7, t, sizeof t); put(out, max, &p, t); }
        if (!*args) put(out, max, &p, "(agenda ayuda: todas las ordenes)\n");
        return 1;
    }
    if (palabra(args, "ayuda") || palabra(args, "help") || palabra(args, "?")) { put(out, max, &p, AYUDA); return 1; }
    if ((r = palabra_anadir(args)) && *r) {
        char res[400]; int id = agenda_anade_texto(r, res, sizeof res);
        if (id > 0) { put(out, max, &p, res); put(out, max, &p, "\n"); }
        else put(out, max, &p, id == -2 ? "La agenda est\303\241 llena (400 eventos): borra alguno.\n" : id == -3 ? "No se pudo guardar la agenda.\n" :
                 "No entiendo cu\303\241ndo es. Dime el d\303\255a o la hora, p. ej.: agenda a\303\261adir el mi\303\251rcoles d\303\255a 6 a las 13 horas partido de f\303\272tbol\n");
        return 1;
    }
    if ((r = palabra(args, "borrar")) || (r = palabra(args, "quitar")) || (r = palabra(args, "elimina")) || (r = palabra(args, "eliminar")) || (r = palabra(args, "cancelar"))) {
        int ok; uint32_t id = atoi_(r, &ok);
        if (!ok) { put(out, max, &p, "Uso: agenda borrar <n>   (el numero sale entre corchetes en la lista)\n"); return 1; }
        struct ag_ev *e = ag_busca_id(c, id); char t[AG_TIT]; t[0] = 0; if (e) { uint32_t k = 0; put(t, sizeof t, &k, e->titulo); }
        if (agenda_borra(id) == 0) { put(out, max, &p, "Borrado ["); putn(out, max, &p, id); put(out, max, &p, "] "); put(out, max, &p, t); put(out, max, &p, "\n"); }
        else put(out, max, &p, "No hay ningun evento con ese numero.\n");
        return 1;
    }
    if ((r = palabra(args, "saltar"))) {
        int ok; uint32_t id = atoi_(r, &ok); while (*r >= '0' && *r <= '9') ++r;
        int64_t a, b;
        if (ok && ag_parsea_tramo(r, ahora, &a, &b) == 0 && ag_borra_ocurrencia(c, id, a)) { agenda_guarda(); put(out, max, &p, "Quitado solo ese d\303\255a.\n"); }
        else put(out, max, &p, "Uso: agenda saltar <n> <dia>   (solo para eventos que se repiten; p. ej. agenda saltar 3 el lunes)\n");
        return 1;
    }
    if ((r = palabra(args, "mover")) || (r = palabra(args, "cambiar"))) {
        int ok; uint32_t id = atoi_(r, &ok); while (*r >= '0' && *r <= '9') ++r;
        struct ag_ev *e = ok ? ag_busca_id(c, id) : 0; struct ag_ev nuevo;
        if (e && ag_parsea(r, ahora, &nuevo)) { e = 0; put(out, max, &p, "No entiendo la nueva fecha.\n"); return 1; }
        if (!e) { put(out, max, &p, "Uso: agenda mover <n> <nueva fecha y hora>   (p. ej. agenda mover 3 el viernes a las 18)\n"); return 1; }
        e->ini = nuevo.ini; e->todo_dia = nuevo.todo_dia; if (nuevo.dur) e->dur = nuevo.dur; agenda_guarda();
        char d[240]; ag_describe(e, e->ini, ahora, d, sizeof d); put(out, max, &p, "Movido: "); put(out, max, &p, d); put(out, max, &p, "\n");
        return 1;
    }
    if ((r = palabra(args, "detalle"))) {
        int ok; uint32_t id = atoi_(r, &ok); struct ag_ev *e = ok ? ag_busca_id(c, id) : 0;
        if (!e) { put(out, max, &p, "No hay ningun evento con ese numero.\n"); return 1; }
        char d[240]; ag_describe(e, e->ini, ahora, d, sizeof d); put(out, max, &p, d); put(out, max, &p, "\nCategor\303\255a: "); put(out, max, &p, ag_nombre_cat(e->cat));
        if (e->lugar[0]) { put(out, max, &p, "\nLugar: "); put(out, max, &p, e->lugar); } if (e->nota[0]) { put(out, max, &p, "\nNota: "); put(out, max, &p, e->nota); }
        put(out, max, &p, "\nAviso: "); if (e->antes < 0) put(out, max, &p, "el de siempre"); else if (!e->antes) put(out, max, &p, "ninguno"); else { putn(out, max, &p, e->antes); put(out, max, &p, " min antes"); }
        put(out, max, &p, "\n"); return 1;
    }
    if ((r = palabra(args, "buscar")) || (r = palabra(args, "busca"))) {
        if (!*r) { put(out, max, &p, "Uso: agenda buscar <texto>\n"); return 1; }
        int n = 0; static struct ag_oc oc[200];
        int k = ag_agenda_rango(c, hoy * 1440 - 365 * 1440, hoy * 1440 + 730 * 1440, oc, 200); char ult[AG_MAX_EV / 8 + 1]; (void)ult;
        uint32_t visto[64]; int nv = 0;
        for (int i = 0; i < k; ++i) {
            const struct ag_ev *e = &c->ev[oc[i].idx]; int hay = 0; uint32_t L = sl(r);
            for (uint32_t a = 0; e->titulo[a] && !hay; ++a) { uint32_t b = 0; while (b < L && e->titulo[a + b] && baja(e->titulo[a + b]) == baja(r[b])) ++b; if (b == L) hay = 1; }
            if (!hay) continue;
            int ya = 0; for (int v = 0; v < nv; ++v) if (visto[v] == e->id) ya = 1;
            if (ya || nv >= 64) continue;
            visto[nv++] = e->id;
            char d[240]; ag_describe(e, oc[i].ini, ahora, d, sizeof d); put(out, max, &p, "  ["); putn(out, max, &p, e->id); put(out, max, &p, "] "); put(out, max, &p, d); put(out, max, &p, "\n"); ++n;
        }
        if (!n) put(out, max, &p, "No encuentro nada con ese texto.\n");
        return 1;
    }
    if ((r = palabra(args, "exportar")) || (r = palabra(args, "export"))) {
        const char *dst = *r ? r : AG_ICS; uint32_t cap = AG_MAX_EV * 700u + 256u; char *b = (char *)kmalloc(cap);
        if (!b) { put(out, max, &p, "Sin memoria para exportar.\n"); return 1; }
        uint32_t n = ag_ics_exporta(c, b, cap);
        int w = n ? vfs_write(dst, b, n) : -1; kfree(b);
        if (w >= 0) { put(out, max, &p, "Exportados "); putn(out, max, &p, ag_numero(c)); put(out, max, &p, " eventos a "); put(out, max, &p, dst); put(out, max, &p, " (.ics: se abre en Google Calendar, Outlook, Apple Calendario...)\n"); }
        else put(out, max, &p, "No se pudo escribir el archivo (solo puedes escribir en /usuario y /tmp).\n");
        return 1;
    }
    if ((r = palabra(args, "importar")) || (r = palabra(args, "import"))) {
        if (!*r) { put(out, max, &p, "Uso: agenda importar <archivo.ics>   (p. ej. /usuario/calendario.ics)\n"); return 1; }
        uint32_t n = 0; char *b = vfs_read_alloc(r, &n);
        if (!b) { put(out, max, &p, "No encuentro ese archivo.\n"); return 1; }
        int k = ag_ics_importa(c, b, n); kfree(b);
        if (k < 0) put(out, max, &p, "Eso no parece un archivo .ics.\n");
        else { if (k) agenda_guarda(); put(out, max, &p, "Importados "); putn(out, max, &p, k); put(out, max, &p, " eventos.\n"); }
        return 1;
    }
    if ((r = palabra(args, "avisos")) || (r = palabra(args, "avisar")) || (r = palabra(args, "notificaciones"))) {
        struct agenda_conf cf; agenda_conf_get(&cf); const char *q;
        if (palabra(r, "probar") || palabra(r, "prueba")) { put(out, max, &p, "Aviso de prueba: en pantalla"); put(out, max, &p, cf.voz ? " y hablado.\n" : ".\n"); return 2; }
        if ((q = palabra(r, "voz"))) cf.voz = !palabra(q, "off") && !palabra(q, "no");
        else if ((q = palabra(r, "visual")) || (q = palabra(r, "pantalla"))) cf.visual = !palabra(q, "off") && !palabra(q, "no");
        else if ((q = palabra(r, "antes"))) { int ok; uint32_t v = atoi_(q, &ok); if (ok && v <= 1440) cf.antes = (int)v; else { put(out, max, &p, "Uso: agenda avisos antes <minutos>\n"); return 1; } }
        else if (palabra(r, "off") || palabra(r, "no")) cf.avisos = 0;
        else if (palabra(r, "on") || palabra(r, "si") || palabra(r, "s\303\255")) cf.avisos = 1;
        else if (*r) { put(out, max, &p, "Uso: agenda avisos [on|off] | voz on|off | visual on|off | antes N | probar\n"); return 1; }
        agenda_conf_set(&cf);
        put(out, max, &p, "Avisos: "); put(out, max, &p, cf.avisos ? "activados" : "desactivados"); put(out, max, &p, ", "); putn(out, max, &p, cf.antes); put(out, max, &p, " min antes, ");
        put(out, max, &p, cf.voz ? "con voz" : "sin voz"); put(out, max, &p, ", "); put(out, max, &p, cf.visual ? "en pantalla.\n" : "sin pantalla.\n");
        return 1;
    }
    if (((r = palabra(args, "proximos")) || (r = palabra(args, "pr\303\263ximos")) || (r = palabra(args, "proximo")) || (r = palabra(args, "proxima"))) && !*r) {      /* "agenda proximos": las dos proximas semanas */
        char t[2500]; (void)lista(hoy, hoy + 14, t, sizeof t); put(out, max, &p, "Lo que viene (14 d\303\255as):\n"); put(out, max, &p, t); return 1;
    }
    /* consultas: hoy, manana, semana, mes, proximos N dias, ver <fecha> */
    { const char *q = args; const char *v;
      if ((v = palabra(q, "ver")) || (v = palabra(q, "ense\303\261a")) || (v = palabra(q, "muestra")) || (v = palabra(q, "dime")) || (v = palabra(q, "lista"))) q = v;
      if (ag_parsea_tramo(q, ahora, &d0, &d1) == 0) {
        /* "mañana tengo reunion": es una consulta solo si no sobra texto con significado: lo decide el parser de eventos */
        struct ag_ev ev; int es_evento = ag_parsea(q, ahora, &ev) == 0 && ev.titulo[0] && !(ev.titulo[0] == 'E' && ev.titulo[1] == 'v' && ev.titulo[2] == 'e' && ev.titulo[3] == 'n' && !ev.titulo[6]);
        if (!es_evento || q != args) {
            char t[2500]; (void)lista(d0, d1, t, sizeof t);
            if (d1 - d0 <= 1) { char f[64]; ag_fecha_txt(d0 * 1440, ahora, f, sizeof f); put(out, max, &p, "Agenda de "); put(out, max, &p, f); put(out, max, &p, ":\n"); }
            put(out, max, &p, t); return 1;
        }
      }
    }
    { char res[400]; int id = agenda_anade_texto(args, res, sizeof res);                 /* "agenda mañana reunión con Marta" = añadir */
      if (id > 0) { put(out, max, &p, res); put(out, max, &p, "\n"); return 1; } }
    put(out, max, &p, "No lo he entendido.\n"); put(out, max, &p, AYUDA);
    return 1;
}
