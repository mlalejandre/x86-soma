/* SOMA_332_HORA: la hora del sistema en la zona elegida. Ver hora.h. */
#include <stdint.h>
#include "hora.h"
#include "zona.h"
#include "rtc.h"
#include "idt.h"
#include "fs.h"
#include "agenda_core.h"

static int      g_zona = -1;          /* -1 = aun sin elegir: Espana peninsular */
static int16_t  g_manual;
static int      g_reloj_local;
static int      g_cargado;
static uint32_t g_ver;

/* el RTC se lee de vez en cuando (puertos CMOS: lentos y sin cerrojo); entre lecturas cuenta el reloj del sistema */
static volatile int64_t  g_base_seg = -1;
static volatile uint64_t g_base_ms;
static volatile int      g_leyendo;

static int zona_actual(void)
{
    if (g_zona == -1) {
        g_zona = zona_busca("Europe/Madrid", 0); if (g_zona < 0) g_zona = 0;
        ag_desfase_fn = hora_desfase_en;    /* la agenda convierte con la zona elegida (sus horas de .ics en UTC tambien) */
    }
    return g_zona;
}
static int64_t rtc_seg(void)
{
    struct rtc_time t; rtc_leer_hw(&t);
    if (t.year < 1970 || t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31 || t.hour > 23 || t.min > 59 || t.sec > 59) return -1;
    int64_t s = (zona_dias(t.year, t.month, t.day) * 1440 + t.hour * 60 + t.min) * 60 + t.sec;
    if (g_reloj_local) {                                   /* el RTC guarda la hora de pared: se le quita el desfase */
        int64_t m = s / 60;
        s -= (int64_t)zona_desfase(zona_actual(), g_manual, m - zona_desfase(zona_actual(), g_manual, m)) * 60;
    }
    return s;
}
int64_t hora_utc_seg(void)
{
    uint64_t ms = timer_get_uptime_ms();
    if ((g_base_seg < 0 || ms == 0 || ms - g_base_ms > 30000u) && !__atomic_exchange_n(&g_leyendo, 1, __ATOMIC_ACQUIRE)) {
        int64_t s = rtc_seg();
        if (s >= 0) { g_base_ms = ms; g_base_seg = s; }
        __atomic_store_n(&g_leyendo, 0, __ATOMIC_RELEASE);
    }
    int64_t b = g_base_seg; uint64_t bm = g_base_ms;
    if (b < 0) return 0;
    return b + (int64_t)((ms >= bm ? ms - bm : 0) / 1000u);
}
int64_t hora_utc_min(void) { int64_t s = hora_utc_seg(); return s >= 0 ? s / 60 : 0; }

static void a_rtc(int64_t s, struct rtc_time *t)
{
    if (s < 0) s = 0;
    int64_t dias = s / 86400, r = s % 86400;
    int y, m, d; zona_civil(dias, &y, &m, &d);
    t->year = (uint16_t)y; t->month = (uint8_t)m; t->day = (uint8_t)d;
    t->hour = (uint8_t)(r / 3600); t->min = (uint8_t)(r / 60 % 60); t->sec = (uint8_t)(r % 60);
}
int hora_desfase_en(int64_t min_utc) { return zona_desfase(zona_actual(), g_manual, min_utc); }
int hora_desfase(void) { return hora_desfase_en(hora_utc_min()); }
int hora_verano(void) { return zona_actual() >= 0 && zona_verano(zona_actual(), hora_utc_min()); }
void hora_utc(struct rtc_time *t) { a_rtc(hora_utc_seg(), t); }
void hora_local(struct rtc_time *t)
{
    int64_t s = hora_utc_seg();
    a_rtc(s + (int64_t)hora_desfase_en(s / 60) * 60, t);
}
int hora_zona(void) { return zona_actual(); }
int16_t hora_manual(void) { return g_manual; }
int hora_reloj_local(void) { return g_reloj_local; }
uint32_t hora_version(void) { return g_ver; }

/* el resto del sistema pide la hora al RTC con rtc_get_datetime(): sale en UTC aunque el reloj del equipo vaya en hora local */
void rtc_get_datetime(struct rtc_time *t) { if (t) hora_utc(t); }

/* ---------------------------------------------------------------- ajustes */
static uint32_t slen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static void put(char *o, uint32_t cap, uint32_t *p, const char *s) { while (*s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void put2(char *o, uint32_t cap, uint32_t *p, int v) { char b[3] = { (char)('0' + v / 10 % 10), (char)('0' + v % 10), 0 }; put(o, cap, p, b); }
static void putn(char *o, uint32_t cap, uint32_t *p, int v) { char b[12]; int n = 0; if (v < 0) { put(o, cap, p, "-"); v = -v; } do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); while (n) { char c[2] = { b[--n], 0 }; put(o, cap, p, c); } }

void hora_nombre_zona(char *o, uint32_t cap)
{
    if (!cap) return;
    o[0] = 0;
    if (zona_actual() == ZONA_MANUAL) { zona_texto_desfase(g_manual, o, cap); return; }
    uint32_t p = 0; const struct zona *z = zona_i(zona_actual());
    put(o, cap, &p, z ? z->nombre : "UTC");
}
static void guarda(void)
{
    char b[96]; uint32_t p = 0;
    put(b, sizeof b, &p, "zona=");
    if (zona_actual() == ZONA_MANUAL) { char t[16]; zona_texto_desfase(g_manual, t, sizeof t); put(b, sizeof b, &p, t); }
    else { const struct zona *z = zona_i(zona_actual()); put(b, sizeof b, &p, z ? z->id : "UTC"); }
    put(b, sizeof b, &p, g_reloj_local ? "\nreloj=local\n" : "\nreloj=utc\n");
    (void)vfs_write("/etc/hora.conf", b, slen(b));
}
static void cambiado(void)
{
    g_ver++; g_cargado = 1;                 /* lo que elige la persona manda sobre lo que hubiera en el archivo */
    g_base_seg = -1;                        /* el RTC se vuelve a leer (con reloj local, el desfase cambia la cuenta) */
}
int hora_pon_zona_i(int i)
{
    if (!zona_i(i)) return -1;
    g_zona = i; cambiado(); guarda();
    return 0;
}
int hora_pon_zona(const char *s)
{
    int16_t m = 0; int i = zona_busca(s, &m);
    if (i == -1) return -1;
    if (i == ZONA_MANUAL) { g_zona = ZONA_MANUAL; g_manual = m; cambiado(); guarda(); return 0; }
    return hora_pon_zona_i(i);
}
void hora_pon_reloj_local(int local) { g_reloj_local = local ? 1 : 0; cambiado(); guarda(); }

static int valor(const char *t, const char *k, char *o, uint32_t cap)
{
    uint32_t kl = slen(k);
    for (uint32_t i = 0; t[i]; i++) {
        if (i && t[i - 1] != '\n') continue;
        uint32_t j = 0; while (j < kl && t[i + j] == k[j]) j++;
        if (j < kl || t[i + j] != '=') continue;
        const char *v = t + i + j + 1; uint32_t n = 0;
        while (v[n] && v[n] != '\n' && v[n] != '\r' && n + 1 < cap) { o[n] = v[n]; n++; }
        o[n] = 0;
        return 1;
    }
    return 0;
}
void hora_carga(void)
{
    if (g_cargado) return;
    char b[160]; int n = vfs_read("/etc/hora.conf", b, sizeof b - 1);
    g_cargado = 1;
    if (n > 0) {
        b[n] = 0;
        char v[64]; int16_t m = 0;
        if (valor(b, "zona", v, sizeof v)) { int i = zona_busca(v, &m); if (i == ZONA_MANUAL) { g_zona = i; g_manual = m; } else if (i >= 0) g_zona = i; }
        if (valor(b, "reloj", v, sizeof v)) g_reloj_local = v[0] == 'l';
        g_base_seg = -1;
    }
    (void)zona_actual();
    g_ver++;
}

/* ---------------------------------------------------------------- orden "hora" */
static void fecha(char *o, uint32_t cap, uint32_t *p, const struct rtc_time *t)
{
    putn(o, cap, p, t->year); put(o, cap, p, "-"); put2(o, cap, p, t->month); put(o, cap, p, "-"); put2(o, cap, p, t->day); put(o, cap, p, " ");
    put2(o, cap, p, t->hour); put(o, cap, p, ":"); put2(o, cap, p, t->min); put(o, cap, p, ":"); put2(o, cap, p, t->sec);
}
static int empieza(const char *s, const char *w) { while (*w && *s == *w) { s++; w++; } return !*w && (*s == 0 || *s == ' '); }
int hora_cmd(const char *a, char *o, uint32_t cap)
{
    uint32_t p = 0; if (cap) o[0] = 0;
    hora_carga();
    while (a && *a == ' ') a++;
    if (!a) a = "";
    if (empieza(a, "zona")) {
        a += 4; while (*a == ' ') a++;
        if (!*a) { put(o, cap, &p, "uso: hora zona <ciudad, pais, nombre IANA o UTC+2>   (hora zonas: la lista)\n"); return 1; }
        if (hora_pon_zona(a) != 0) { put(o, cap, &p, "hora: no conozco esa zona: "); put(o, cap, &p, a); put(o, cap, &p, " (prueba una ciudad, \"hora zonas\" o un desfase como UTC+2)\n"); return 1; }
        a = "";
    } else if (empieza(a, "zonas")) {
        for (int i = 0; i < zona_n(); i++) {
            const struct zona *z = zona_i(i); char d[16]; zona_texto_desfase(z->std, d, sizeof d);
            put(o, cap, &p, z->id); put(o, cap, &p, "  "); put(o, cap, &p, z->nombre); put(o, cap, &p, "  "); put(o, cap, &p, d);
            put(o, cap, &p, z->regla != ZR_NADA ? " (con horario de verano)\n" : "\n");
        }
        return 1;
    } else if (empieza(a, "reloj")) {
        a += 5; while (*a == ' ') a++;
        if (empieza(a, "utc")) hora_pon_reloj_local(0);
        else if (empieza(a, "local")) hora_pon_reloj_local(1);
        else { put(o, cap, &p, "uso: hora reloj utc|local   (local solo si el equipo arranca tambien Windows)\n"); return 1; }
        a = "";
    } else if (*a) {
        put(o, cap, &p, "uso: hora | hora zona <ciudad o UTC+2> | hora zonas | hora reloj utc|local\n");
        return 1;
    }
    struct rtc_time t; char d[16], n[64];
    hora_local(&t); hora_nombre_zona(n, sizeof n); zona_texto_desfase(hora_desfase(), d, sizeof d);
    put(o, cap, &p, "hora local: "); fecha(o, cap, &p, &t); put(o, cap, &p, "  ("); put(o, cap, &p, n); put(o, cap, &p, ", "); put(o, cap, &p, d);
    put(o, cap, &p, hora_verano() ? ", horario de verano)\n" : ")\n");
    hora_utc(&t);
    put(o, cap, &p, "UTC:        "); fecha(o, cap, &p, &t); put(o, cap, &p, "\n");
    put(o, cap, &p, g_reloj_local ? "el reloj del equipo guarda la hora local (como Windows)\n" : "el reloj del equipo guarda UTC\n");
    return 1;
}
