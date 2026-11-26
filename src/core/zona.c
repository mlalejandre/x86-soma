/* SOMA_332_HORA: zonas horarias (codigo puro). Ver zona.h. */
#include "zona.h"

static const struct zona ZONAS[] = {
    { "UTC",                            "UTC (tiempo universal)",      "gmt|utc|universal|z",                   0,    ZR_NADA,  ZG_EUROPA },
    { "Europe/Madrid",                  "Espana (Madrid)",            "madrid|espana|espana peninsular|barcelona|valencia|sevilla|bilbao|baleares|ceuta|melilla|cet", 60, ZR_UE, ZG_EUROPA },
    { "Atlantic/Canary",                "Canarias",                    "canarias|tenerife|las palmas|gran canaria", 0, ZR_UE, ZG_EUROPA },
    { "Europe/Lisbon",                  "Portugal (Lisboa)",           "lisboa|portugal|oporto|madeira",        0,    ZR_UE,    ZG_EUROPA },
    { "Europe/London",                  "Reino Unido (Londres)",       "londres|reino unido|inglaterra|uk|edimburgo", 0, ZR_UE, ZG_EUROPA },
    { "Europe/Dublin",                  "Irlanda (Dublin)",            "dublin|irlanda",                        0,    ZR_UE,    ZG_EUROPA },
    { "Europe/Paris",                   "Francia (Paris)",             "paris|francia|bruselas|belgica|luxemburgo|monaco|andorra", 60, ZR_UE, ZG_EUROPA },
    { "Europe/Berlin",                  "Alemania (Berlin)",           "berlin|alemania|amsterdam|holanda|paises bajos|viena|austria|zurich|suiza|copenhague|estocolmo|oslo|praga|varsovia|budapest", 60, ZR_UE, ZG_EUROPA },
    { "Europe/Rome",                    "Italia (Roma)",               "roma|italia|milan|malta",               60,   ZR_UE,    ZG_EUROPA },
    { "Europe/Athens",                  "Grecia (Atenas)",             "atenas|grecia|chipre|bucarest|rumania|sofia|bulgaria", 120, ZR_UE, ZG_EUROPA },
    { "Europe/Helsinki",                "Finlandia (Helsinki)",        "helsinki|finlandia|kiev|ucrania|tallin|riga|vilna", 120, ZR_UE, ZG_EUROPA },
    { "Europe/Istanbul",                "Turquia (Estambul)",          "estambul|turquia|ankara",               180,  ZR_NADA,  ZG_EUROPA },
    { "Europe/Moscow",                  "Rusia (Moscu)",               "moscu|rusia|minsk|bielorrusia",         180,  ZR_NADA,  ZG_EUROPA },
    /* Marruecos: UTC+1 todo el ano salvo el Ramadan (UTC+0), que sigue el calendario lunar: aqui no se descuenta */
    { "Africa/Casablanca",              "Marruecos (Casablanca)",      "casablanca|marruecos|rabat",            60,   ZR_NADA,  ZG_EUROPA },
    { "Africa/Lagos",                   "Africa occidental (Lagos)",   "lagos|nigeria|argel|argelia|tunez|guinea ecuatorial|malabo", 60, ZR_NADA, ZG_EUROPA },
    { "Africa/Johannesburg",            "Sudafrica (Johannesburgo)",   "johannesburgo|sudafrica|el cabo",       120,  ZR_NADA,  ZG_EUROPA },
    { "Africa/Nairobi",                 "Africa oriental (Nairobi)",   "nairobi|kenia|etiopia|addis abeba",     180,  ZR_NADA,  ZG_EUROPA },
    { "America/New_York",               "EE. UU. este (Nueva York)",   "nueva york|new york|miami|washington|boston|toronto|montreal|este", -300, ZR_EEUU, ZG_AMERICA },
    { "America/Chicago",                "EE. UU. centro (Chicago)",    "chicago|houston|dallas|texas|centro",   -360, ZR_EEUU,  ZG_AMERICA },
    { "America/Denver",                 "EE. UU. montana (Denver)",    "denver|montana",                        -420, ZR_EEUU,  ZG_AMERICA },
    { "America/Phoenix",                "Arizona (Phoenix)",           "phoenix|arizona",                       -420, ZR_NADA,  ZG_AMERICA },
    { "America/Los_Angeles",            "EE. UU. pacifico (Los Angeles)", "los angeles|san francisco|seattle|vancouver|pacifico|california", -480, ZR_EEUU, ZG_AMERICA },
    { "America/Anchorage",              "Alaska",                      "alaska|anchorage",                      -540, ZR_EEUU,  ZG_AMERICA },
    { "Pacific/Honolulu",               "Hawai",                       "hawai|honolulu",                        -600, ZR_NADA,  ZG_AMERICA },
    { "America/Mexico_City",            "Mexico (Ciudad de Mexico)",   "mexico|ciudad de mexico|guadalajara|monterrey", -360, ZR_NADA, ZG_AMERICA },
    { "America/Cancun",                 "Mexico (Cancun)",             "cancun|quintana roo",                   -300, ZR_NADA,  ZG_AMERICA },
    { "America/Guatemala",              "Centroamerica (Guatemala)",   "guatemala|el salvador|honduras|nicaragua|costa rica|san jose", -360, ZR_NADA, ZG_AMERICA },
    { "America/Panama",                 "Panama",                      "panama",                                -300, ZR_NADA,  ZG_AMERICA },
    { "America/Havana",                 "Cuba (La Habana)",            "cuba|la habana|habana",                 -300, ZR_CUBA,  ZG_AMERICA },
    { "America/Santo_Domingo",          "Republica Dominicana",        "santo domingo|republica dominicana",    -240, ZR_NADA,  ZG_AMERICA },
    { "America/Puerto_Rico",            "Puerto Rico",                 "puerto rico|san juan",                  -240, ZR_NADA,  ZG_AMERICA },
    { "America/Bogota",                 "Colombia (Bogota)",           "bogota|colombia|medellin|cali",         -300, ZR_NADA,  ZG_AMERICA },
    { "America/Lima",                   "Peru (Lima)",                 "lima|peru",                             -300, ZR_NADA,  ZG_AMERICA },
    { "America/Guayaquil",              "Ecuador (Quito)",             "quito|ecuador|guayaquil",               -300, ZR_NADA,  ZG_AMERICA },
    { "America/Caracas",                "Venezuela (Caracas)",         "caracas|venezuela",                     -240, ZR_NADA,  ZG_AMERICA },
    { "America/La_Paz",                 "Bolivia (La Paz)",            "la paz|bolivia|santa cruz",             -240, ZR_NADA,  ZG_AMERICA },
    { "America/Santiago",               "Chile (Santiago)",            "santiago|chile|valparaiso",             -240, ZR_CHILE, ZG_AMERICA },
    { "America/Argentina/Buenos_Aires", "Argentina (Buenos Aires)",    "buenos aires|argentina|cordoba|rosario", -180, ZR_NADA, ZG_AMERICA },
    { "America/Montevideo",             "Uruguay (Montevideo)",        "montevideo|uruguay",                    -180, ZR_NADA,  ZG_AMERICA },
    /* Paraguay: UTC-3 fijo desde octubre de 2024 */
    { "America/Asuncion",               "Paraguay (Asuncion)",         "asuncion|paraguay",                     -180, ZR_NADA,  ZG_AMERICA },
    { "America/Sao_Paulo",              "Brasil (Sao Paulo)",          "sao paulo|brasil|rio de janeiro|brasilia", -180, ZR_NADA, ZG_AMERICA },
    { "Asia/Dubai",                     "Emiratos (Dubai)",            "dubai|emiratos|abu dabi|oman",          240,  ZR_NADA,  ZG_ASIA },
    { "Asia/Riyadh",                    "Arabia Saudi (Riad)",         "riad|arabia|qatar|doha|kuwait|bagdad",  180,  ZR_NADA,  ZG_ASIA },
    { "Asia/Kolkata",                   "India (Calcuta)",             "india|calcuta|bombay|mumbai|delhi|nueva delhi|bangalore", 330, ZR_NADA, ZG_ASIA },
    { "Asia/Bangkok",                   "Tailandia (Bangkok)",         "bangkok|tailandia|vietnam|hanoi|yakarta|indonesia", 420, ZR_NADA, ZG_ASIA },
    { "Asia/Shanghai",                  "China (Pekin)",               "china|pekin|shanghai|hong kong|taiwan|taipei", 480, ZR_NADA, ZG_ASIA },
    { "Asia/Singapore",                 "Singapur",                    "singapur|malasia|kuala lumpur",         480,  ZR_NADA,  ZG_ASIA },
    { "Asia/Manila",                    "Filipinas (Manila)",          "manila|filipinas",                      480,  ZR_NADA,  ZG_ASIA },
    { "Asia/Tokyo",                     "Japon (Tokio)",               "tokio|japon|osaka",                     540,  ZR_NADA,  ZG_ASIA },
    { "Asia/Seoul",                     "Corea (Seul)",                "seul|corea",                            540,  ZR_NADA,  ZG_ASIA },
    { "Australia/Perth",                "Australia oeste (Perth)",     "perth",                                 480,  ZR_NADA,  ZG_ASIA },
    { "Australia/Brisbane",             "Australia (Brisbane)",        "brisbane|queensland",                   600,  ZR_NADA,  ZG_ASIA },
    { "Australia/Sydney",               "Australia este (Sidney)",     "sidney|sydney|melbourne|canberra|australia", 600, ZR_AUS, ZG_ASIA },
    { "Pacific/Auckland",               "Nueva Zelanda (Auckland)",    "auckland|nueva zelanda|wellington",     720,  ZR_NZ,    ZG_ASIA },
};
#define NZ ((int)(sizeof ZONAS / sizeof ZONAS[0]))

int zona_n(void) { return NZ; }
const struct zona *zona_i(int i) { return i >= 0 && i < NZ ? &ZONAS[i] : 0; }

/* ---------------------------------------------------------------- calendario (proleptico gregoriano) */
int64_t zona_dias(int y, int m, int d)
{
    int64_t yy = y - (m <= 2);
    int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    int64_t yoe = yy - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
void zona_civil(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int dd = (int)(doy - (153 * mp + 2) / 5 + 1), mm = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400) + (mm <= 2); *m = mm; *d = dd;
}
int zona_dia_semana(int64_t dias) { int64_t w = (dias + 3) % 7; return (int)(w < 0 ? w + 7 : w); }   /* 1970-01-01 fue jueves */

static int dias_mes(int y, int m)
{
    static const uint8_t D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : D[m - 1];
}
static int64_t enesimo_domingo(int y, int m, int n)      /* n = 1, 2...; n = 0: el ultimo */
{
    if (n == 0) { int64_t u = zona_dias(y, m, dias_mes(y, m)); return u - (zona_dia_semana(u) + 1) % 7; }
    int64_t p = zona_dias(y, m, 1);
    return p + (6 - zona_dia_semana(p) + 7) % 7 + (int64_t)(n - 1) * 7;
}

static int64_t domingo_desde(int y, int m, int d)        /* primer domingo a partir del dia d */
{
    int64_t p = zona_dias(y, m, d);
    return p + (6 - zona_dia_semana(p) + 7) % 7;
}

/* ---------------------------------------------------------------- horario de verano */
int zona_verano(int i, int64_t t)
{
    const struct zona *z = zona_i(i);
    if (!z || z->regla == ZR_NADA) return 0;
    int64_t dias = t >= 0 ? t / 1440 : -((-t + 1439) / 1440);
    int y, m, d; zona_civil(dias, &y, &m, &d);
    int64_t ini, fin, s = z->std;
    switch (z->regla) {
    case ZR_UE:      /* ultimo domingo de marzo a ultimo domingo de octubre, a la 01:00 UTC en toda la UE */
        ini = enesimo_domingo(y, 3, 0) * 1440 + 60; fin = enesimo_domingo(y, 10, 0) * 1440 + 60;
        return t >= ini && t < fin;
    case ZR_EEUU:    /* segundo domingo de marzo a las 02:00 locales a primer domingo de noviembre a las 02:00 (de verano) */
        ini = enesimo_domingo(y, 3, 2) * 1440 + 120 - s; fin = enesimo_domingo(y, 11, 1) * 1440 + 120 - (s + 60);
        return t >= ini && t < fin;
    case ZR_CUBA:    /* los dias de EE. UU., pero a las 00:00 (de invierno) y a la 01:00 (de verano) */
        ini = enesimo_domingo(y, 3, 2) * 1440 - s; fin = enesimo_domingo(y, 11, 1) * 1440 + 60 - (s + 60);
        return t >= ini && t < fin;
    case ZR_CHILE:   /* hemisferio sur: del domingo desde el 2 de septiembre (04:00 UTC) al domingo desde el 2 de abril (03:00 UTC) */
        ini = domingo_desde(y, 9, 2) * 1440 + 240; fin = domingo_desde(y, 4, 2) * 1440 + 180;
        return t >= ini || t < fin;
    case ZR_AUS:     /* primer domingo de octubre a las 02:00 a primer domingo de abril a las 03:00 (de verano) */
        ini = enesimo_domingo(y, 10, 1) * 1440 + 120 - s; fin = enesimo_domingo(y, 4, 1) * 1440 + 180 - (s + 60);
        return t >= ini || t < fin;
    case ZR_NZ:      /* ultimo domingo de septiembre a las 02:00 a primer domingo de abril a las 03:00 (de verano) */
        ini = enesimo_domingo(y, 9, 0) * 1440 + 120 - s; fin = enesimo_domingo(y, 4, 1) * 1440 + 180 - (s + 60);
        return t >= ini || t < fin;
    }
    return 0;
}
int zona_desfase(int i, int16_t manual, int64_t t)
{
    if (i == ZONA_MANUAL) return manual;
    const struct zona *z = zona_i(i);
    if (!z) return 0;
    return z->std + (zona_verano(i, t) ? 60 : 0);
}

/* ---------------------------------------------------------------- busqueda */
static char minus(char c)
{
    unsigned char u = (unsigned char)c;
    if (u >= 'A' && u <= 'Z') return (char)(u + 32);
    if (u == '_' || u == '.') return ' ';
    return c;
}
/* copia s en minusculas, sin tildes (UTF-8 de las vocales, la ene y la u con dieresis) y con '_' como espacio */
static void normaliza(const char *s, char *o, uint32_t cap)
{
    uint32_t n = 0;
    while (*s == ' ') s++;
    while (*s && n + 1 < cap) {
        unsigned char a = (unsigned char)s[0], b = (unsigned char)s[1];
        if (a == 0xC3 && b) {
            char c = 0;
            switch (b | 0x20) {
            case 0xA1: c = 'a'; break; case 0xA9: c = 'e'; break; case 0xAD: c = 'i'; break;
            case 0xB3: c = 'o'; break; case 0xBA: case 0xBC: c = 'u'; break; case 0xB1: c = 'n'; break;
            }
            if (c) { o[n++] = c; s += 2; continue; }
        }
        o[n++] = minus(*s++);
    }
    while (n && o[n - 1] == ' ') n--;
    o[n] = 0;
}
static int igual(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int num(const char **p, int max_cifras)
{
    int v = 0, k = 0;
    while (**p >= '0' && **p <= '9' && k < max_cifras) { v = v * 10 + (**p - '0'); (*p)++; k++; }
    return k ? v : -1;
}
/* "utc+2", "gmt-3", "+05:30", "-0430", "utc" */
static int desfase_fijo(const char *s, int16_t *out)
{
    if ((s[0] == 'u' && s[1] == 't' && s[2] == 'c') || (s[0] == 'g' && s[1] == 'm' && s[2] == 't')) s += 3;
    while (*s == ' ') s++;
    if (*s != '+' && *s != '-') return -1;
    int sg = *s++ == '-' ? -1 : 1;
    while (*s == ' ') s++;
    const char *q = s; int h = num(&q, 2), m = 0;
    if (h < 0) return -1;
    if (*q == ':' || *q == 'h') { q++; m = num(&q, 2); if (m < 0) m = 0; }
    else if (q - s == 2 && *q >= '0' && *q <= '9') { m = num(&q, 2); if (m < 0) return -1; }
    if (*q || h > 14 || m > 59 || (m % 15) != 0) return -1;
    *out = (int16_t)(sg * (h * 60 + m));
    return 0;
}
int zona_busca(const char *s, int16_t *manual)
{
    char q[64], t[96];
    if (!s) return -1;
    normaliza(s, q, sizeof q);
    if (!q[0]) return -1;
    int16_t m;
    if (desfase_fijo(q, &m) == 0) { if (manual) *manual = m; return ZONA_MANUAL; }
    for (int i = 0; i < NZ; i++) {                                  /* el nombre IANA entero o su ultima parte */
        normaliza(ZONAS[i].id, t, sizeof t);
        if (igual(q, t)) return i;
        const char *u = t; for (const char *p = t; *p; p++) if (*p == '/') u = p + 1;
        if (igual(q, u)) return i;
    }
    for (int i = 0; i < NZ; i++) {                                  /* un alias o el nombre de la pantalla */
        const char *a = ZONAS[i].alias;
        while (*a) {
            uint32_t k = 0; while (a[k] && a[k] != '|' && k + 1 < sizeof t) { t[k] = a[k]; k++; }
            t[k] = 0;
            if (igual(q, t)) return i;
            a += k; if (*a == '|') a++;
        }
        normaliza(ZONAS[i].nombre, t, sizeof t);
        if (igual(q, t)) return i;
    }
    return -1;
}

void zona_texto_desfase(int min, char *o, uint32_t cap)
{
    if (!cap) return;
    char b[16]; uint32_t n = 0;
    b[n++] = 'U'; b[n++] = 'T'; b[n++] = 'C';
    if (min) {
        int a = min < 0 ? -min : min;
        b[n++] = min < 0 ? '-' : '+';
        b[n++] = (char)('0' + a / 600 % 10); b[n++] = (char)('0' + a / 60 % 10); b[n++] = ':';
        b[n++] = (char)('0' + a % 60 / 10); b[n++] = (char)('0' + a % 10);
    }
    uint32_t k = 0; for (; k < n && k + 1 < cap; k++) o[k] = b[k];
    o[k] = 0;
}
