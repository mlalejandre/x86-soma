/* SOMA_257_LCM (actualizado por SOMA_258_LCM): el LCM en el agente (sustituye al pegamento del parche 253). Lo incluye src/agent/agent.c antes de mayordomo_run.
 *
 * Que hace con una peticion DEL USUARIO (nunca en modo agente ni en una delegacion):
 *   1. Respuesta pendiente: "si"/"no" a una orden que pedia confirmacion; "no era eso" para corregirle.
 *   2. Charla: saludos, "como estas", "gracias", "adios", "quien eres", "que sabes hacer"... con muchas formulas, sin el LLM.
 *      Un saludo delante de una peticion ("hola, que hora es") se contesta junto a la peticion.
 *   3. Lo aprendido de correcciones (/etc/lcm_aprendido.txt), antes que la red.
 *   4. La red del LCM (lcm_ordenes.inc). Si duda o no es una orden: al LLM.
 *   5. Al ejecutar, la respuesta es UNA frase natural pensada para la voz ("Son las 3 y cuarto de la tarde."). En la respuesta del
 *      agente va como [Razonamiento] la orden y su salida (el orbe la ensena pero no la lee) y como [Dictamen] la frase (que si lee).
 * Las ordenes que cambian algo piden confirmacion con formulas variadas. Ajustes > LCM usa las funciones lcm_cfg_*, lcm_prueba,
 * lcm_aprendido_* y lcm_estadistica. Orden 'lcm': estado | encender | apagar | voz natural|tecnica | umbral N | aprendido |
 * aprende <frase> => <orden> | olvida N|todo | <frase> (que entenderia, sin ejecutar). */
#include "lcm_ordenes_pesos.inc"

#define LCM_HECHO    1
#define LCM_AL_LLM   2
#define LCM_SIGUE    0
#define LCM_DUDA_LLM 3

/* ------------------------------------------------------------------------------------------- estado */
static int  lcm_activo = 1, lcm_cargado = 0, lcm_voz = 1, lcm_umbral = 0;      /* umbral 0 = el del modelo */
static int  lcm_letra = -1;                         /* SOMA_259_ORBE: letra del orbe (0 compacta, 1 mediana, 2 grande; -1 sin leer) */
/* SOMA_266_AVISO: antes de una peticion que va al modelo de lenguaje, el LCM avisa al momento de lo que se va a hacer, cuanto suele
 * tardar (lo que tardaron las anteriores de ese tipo) y pide paciencia. Tipos: */
#define LCM_AV_DIRECTA     1      /* una pregunta o un texto: una llamada al modelo */
#define LCM_AV_AGENTE      2      /* el modelo con herramientas (buscar, archivos, ordenes) */
#define LCM_AV_PROGRAMA    3      /* el programador */
#define LCM_AV_DOCUMENTO   4      /* el maquetador */
#define LCM_AV_HERRAMIENTA 5      /* la respuesta directa pidio herramientas: se sigue con ellas */
#define LCM_AV_N           6
static int  lcm_aviso = 1;                                                       /* Ajustes > LCM > Avisos */
static uint32_t lcm_av_ms[LCM_AV_N], lcm_av_n[LCM_AV_N];                         /* media (ms) y cuantas, por tipo */
/* SOMA_261_ARREGLO: definiciones debiles que no hacen nada; las de src/ui/gui_prompter.c mandan en el nucleo. Antes eran
 * declaraciones debiles sin definir: valian en ELF, pero el enlazador de macOS (pruebas del anfitrion) las rechaza. */
__attribute__((weak)) void gui_fase(const char *que) { (void)que; }
__attribute__((weak)) void gui_fase_orden(const char *linea) { (void)linea; }
static volatile int lcm_cerrojo;                                                 /* la GUI (Ajustes) y el agente no traducen a la vez */
static char lcm_pendiente[512], lcm_pend_frase[256];
static char lcm_ult_frase[256], lcm_ult_orden[512];
static int  lcm_corrigiendo;                                                     /* 1: la proxima orden escrita es la buena */
static uint32_t lcm_st[6];                                                       /* atendidas, al LLM, dudas, confirmadas, canceladas, charla */
#define LCM_ST_ORDEN 0
#define LCM_ST_LLM   1
#define LCM_ST_DUDA  2
#define LCM_ST_CONF  3
#define LCM_ST_CANC  4
#define LCM_ST_CHAR  5
#define LCM_APR_MAX 60
static char lcm_apr_pat[LCM_APR_MAX][160], lcm_apr_ord[LCM_APR_MAX][160];
static int  lcm_apr_n;
static uint32_t lcm_semilla = 0x9E3779B9u;

static uint32_t lcm_azar(void) { lcm_semilla = lcm_semilla * 1664525u + 1013904223u; return lcm_semilla >> 8; }
static void lcm_siembra(const char *s) { while (s && *s) lcm_semilla = (lcm_semilla ^ (uint8_t)*s++) * 16777619u; }

/* una de n formulas, sin repetir la ultima de ese mismo grupo */
static const char *lcm_una(const char *const *f, int n, int *ultima)
{
    if (n <= 0) return "";
    int k = (int)(lcm_azar() % (uint32_t)n);
    if (n > 1 && k == *ultima) k = (k + 1 + (int)(lcm_azar() % (uint32_t)(n - 1))) % n;
    *ultima = k;
    return f[k];
}
#define LCM_UNA(arr) ({ static int _u = -1; lcm_una(arr, (int)(sizeof(arr) / sizeof(arr[0])), &_u); })

/* ------------------------------------------------------------------------------------------- texto */
static uint32_t lcm_len(const char *s) { uint32_t n = 0; while (s && s[n]) ++n; return n; }
static void lcm_cpy(char *d, uint32_t cap, const char *s) { uint32_t p = 0; d[0] = 0; fb_puts(d, cap, &p, s ? s : ""); }
static void lcm_cat(char *d, uint32_t cap, const char *s) { uint32_t p = lcm_len(d); fb_puts(d, cap, &p, s ? s : ""); }
static void lcm_catn(char *d, uint32_t cap, uint64_t v) { uint32_t p = lcm_len(d); fb_put_dec(d, cap, &p, v); }
static int lcm_empieza(const char *s, const char *pre) { while (*pre) if (*s++ != *pre++) return 0; return 1; }
static const char *lcm_busca(const char *s, const char *t)
{
    if (!s || !t || !*t) return 0;
    for (; *s; ++s) { const char *a = s, *b = t; while (*a && *b && *a == *b) { ++a; ++b; } if (!*b) return s; }
    return 0;
}

/* plantilla: {1} {2} {3} se cambian por a, b, c */
static void lcm_forma(char *o, uint32_t cap, const char *tpl, const char *a, const char *b, const char *c)
{
    uint32_t p = 0; o[0] = 0;
    for (const char *t = tpl; *t; ++t) {
        if (t[0] == '{' && t[1] >= '1' && t[1] <= '3' && t[2] == '}') {
            const char *v = t[1] == '1' ? a : t[1] == '2' ? b : c;
            fb_puts(o, cap, &p, v ? v : ""); t += 2; continue;
        }
        char ch[2] = { *t, 0 }; fb_puts(o, cap, &p, ch);
    }
}

/* minusculas, sin tildes (UTF-8 latino), sin signos; espacios simples. Para comparar frases. */
static void lcm_pliega(const char *in, char *o, uint32_t cap)
{
    uint32_t p = 0; int esp = 1;
    for (const unsigned char *s = (const unsigned char *)in; *s && p + 2 < cap; ++s) {
        unsigned char c = *s;
        if (c == 0xC3 && s[1]) {
            unsigned char d = s[1]; ++s;
            char r = 0;
            if (d == 0xA1 || d == 0x81 || d == 0xA0 || d == 0x80) r = 'a'; else if (d == 0xA9 || d == 0x89 || d == 0xA8) r = 'e';
            else if (d == 0xAD || d == 0x8D) r = 'i'; else if (d == 0xB3 || d == 0x93 || d == 0xB2) r = 'o';
            else if (d == 0xBA || d == 0x9A || d == 0xBC || d == 0x9C) r = 'u'; else if (d == 0xB1 || d == 0x91) r = 'n';
            if (r) { o[p++] = r; esp = 0; }
            continue;
        }
        if (c == 0xC2 && s[1]) { ++s; if (!esp) { o[p++] = ' '; esp = 1; } continue; }          /* ¿ ¡ « » */
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        int letra = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '/' || c == '_' || c == '-' || c == ':' || c >= 0x80;
        if (letra) { o[p++] = (char)c; esp = 0; }
        else if (!esp) { o[p++] = ' '; esp = 1; }
    }
    while (p && o[p - 1] == ' ') --p;
    while (p && (o[p - 1] == '.' || o[p - 1] == ':')) --p;
    o[p] = 0;
}

static int lcm_es(const char *f, const char *const *lista)
{
    for (int i = 0; lista[i]; ++i) if (str_eq_ci(f, lista[i])) return 1;
    return 0;
}

/* quita de delante "soma", "oye", "eh", "mira"... (pliegado) */
static const char *lcm_sin_llamada(const char *f)
{
    static const char *const ll[] = { "soma ", "oye ", "eh ", "mira ", "hey ", "ey ", "venga ", "a ver ", 0 };
    for (int vuelta = 0; vuelta < 3; ++vuelta) {
        int quito = 0;
        for (int i = 0; ll[i]; ++i) if (lcm_empieza(f, ll[i])) { f += lcm_len(ll[i]); quito = 1; break; }
        if (!quito) break;
    }
    return f;
}

/* ------------------------------------------------------------------------------------------- hora local (Europe/Madrid) */
static int lcm_dias_mes(int y, int m) { static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; return m == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0) ? 29 : d[m - 1]; }
static int lcm_dia_semana(int y, int m, int d) { static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 }; if (m < 3) y -= 1; return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7; }   /* 0 domingo */
static int lcm_ultimo_domingo(int y, int m) { int d = lcm_dias_mes(y, m); return d - lcm_dia_semana(y, m, d); }
static void lcm_suma_horas(int *y, int *mo, int *d, int *h, int dh)
{
    *h += dh;
    while (*h >= 24) { *h -= 24; if (++*d > lcm_dias_mes(*y, *mo)) { *d = 1; if (++*mo > 12) { *mo = 1; ++*y; } } }
    while (*h < 0) { *h += 24; if (--*d < 1) { if (--*mo < 1) { *mo = 12; --*y; } *d = lcm_dias_mes(*y, *mo); } }
}

struct lcm_fecha { int ok, y, mo, d, h, mi, s, dow; };
/* La orden 'date' da "CEST: AAAA-MM-DD HH:MM:SS CEST" (UTC + 2 fijo). Se pasa a la hora de Madrid de verdad (+1 o +2 segun el dia). */
static void lcm_fecha_de(const char *salida, struct lcm_fecha *f)
{
    f->ok = 0;
    const char *p = salida;
    while (p && *p && !(p[0] >= '0' && p[0] <= '9' && p[4] == '-')) ++p;
    if (!p || !*p) return;
    int v[6] = { 0 }, k = 0;
    for (; *p && k < 6; ++p) {
        if (*p >= '0' && *p <= '9') v[k] = v[k] * 10 + (*p - '0');
        else if (*p == '-' || *p == ':' || *p == ' ') { ++k; if (*p == ' ' && k >= 6) break; }
        else break;
    }
    if (k < 5 || v[0] < 2000 || v[1] < 1 || v[1] > 12 || v[2] < 1 || v[2] > 31) return;
    int y = v[0], mo = v[1], d = v[2], h = v[3];
    lcm_suma_horas(&y, &mo, &d, &h, -2);                                    /* a UTC */
    int verano = 0;                                                         /* de 01:00 UTC del ultimo domingo de marzo al de octubre */
    if (mo > 3 && mo < 10) verano = 1;
    else if (mo == 3) { int ud = lcm_ultimo_domingo(y, 3); verano = d > ud || (d == ud && h >= 1); }
    else if (mo == 10) { int ud = lcm_ultimo_domingo(y, 10); verano = d < ud || (d == ud && h < 1); }
    lcm_suma_horas(&y, &mo, &d, &h, verano ? 2 : 1);
    f->y = y; f->mo = mo; f->d = d; f->h = h; f->mi = v[4]; f->s = v[5]; f->dow = lcm_dia_semana(y, mo, d); f->ok = 1;
}

static void lcm_hora_ahora(struct lcm_fecha *f)
{
    static char b[160];
    int m = console_muted; console_muted = 1;
    b[0] = 0; dispatch_command("date", b, sizeof b);
    console_muted = m;
    lcm_fecha_de(b, f);
}

static void lcm_hora_hablada(char *o, uint32_t cap, int h, int mi)
{
    int hh = h, m = mi; const char *menos = 0;
    if (m == 45) { menos = "menos cuarto"; hh = (h + 1) % 24; }
    int h12 = hh % 12; if (!h12) h12 = 12;
    const char *parte = hh < 6 ? "de la madrugada" : hh < 12 ? "de la mañana" : hh < 14 ? "del mediodía" : hh < 21 ? "de la tarde" : "de la noche";
    o[0] = 0;
    lcm_cat(o, cap, h12 == 1 ? "la 1" : "las "); if (h12 != 1) lcm_catn(o, cap, (uint64_t)h12);
    if (menos) { lcm_cat(o, cap, " "); lcm_cat(o, cap, menos); }
    else if (m == 0) lcm_cat(o, cap, " en punto");
    else if (m == 15) lcm_cat(o, cap, " y cuarto");
    else if (m == 30) lcm_cat(o, cap, " y media");
    else { lcm_cat(o, cap, " y "); lcm_catn(o, cap, (uint64_t)m); }
    lcm_cat(o, cap, " "); lcm_cat(o, cap, parte);
}

static const char *const LCM_DIAS[7] = { "domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado" };
static const char *const LCM_MESES[12] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre" };

/* ------------------------------------------------------------------------------------------- numeros para la voz */
static void lcm_decimal(char *o, uint32_t cap, uint32_t entero, uint32_t decima)   /* 7,9  (coma: se lee "siete coma nueve") */
{
    lcm_catn(o, cap, entero);
    if (decima) { lcm_cat(o, cap, " coma "); lcm_catn(o, cap, decima); }
}
static void lcm_tamano_mib(char *o, uint32_t cap, uint32_t mib_x100)   /* MiB*100 -> "512 megas" / "7 coma 9 gigas" */
{
    if (mib_x100 >= 102400) { uint32_t g10 = (uint32_t)(((uint64_t)mib_x100 * 10 + 51200) / 102400); lcm_decimal(o, cap, g10 / 10, g10 % 10); lcm_cat(o, cap, " gigas"); }
    else { lcm_catn(o, cap, (mib_x100 + 50) / 100); lcm_cat(o, cap, " megas"); }
}
static void lcm_duracion(char *o, uint32_t cap, uint32_t seg)
{
    uint32_t d = seg / 86400, h = (seg % 86400) / 3600, m = (seg % 3600) / 60;
    int puesto = 0;
    if (d) { if (d == 1) lcm_cat(o, cap, "un día"); else { lcm_catn(o, cap, d); lcm_cat(o, cap, " días"); } puesto = 1; }
    if (h) { if (puesto) lcm_cat(o, cap, m && !d ? ", " : " y "); if (h == 1) lcm_cat(o, cap, "una hora"); else { lcm_catn(o, cap, h); lcm_cat(o, cap, " horas"); } puesto = 1; }
    if (m && !d) { if (puesto) lcm_cat(o, cap, " y "); if (m == 1) lcm_cat(o, cap, "un minuto"); else { lcm_catn(o, cap, m); lcm_cat(o, cap, " minutos"); } puesto = 1; }
    if (!puesto) lcm_cat(o, cap, "menos de un minuto");
}
/* siguiente numero (entero y dos decimales opcionales) a partir de p; devuelve el puntero tras el, o 0 */
static const char *lcm_num(const char *p, uint32_t *ent, uint32_t *dec2)
{
    while (*p && !(*p >= '0' && *p <= '9')) ++p;
    if (!*p) return 0;
    uint32_t e = 0, d = 0; while (*p >= '0' && *p <= '9') e = e * 10 + (uint32_t)(*p++ - '0');
    if ((*p == '.' || *p == ',') && p[1] >= '0' && p[1] <= '9') { ++p; int k = 0; while (*p >= '0' && *p <= '9') { if (k < 2) d = d * 10 + (uint32_t)(*p - '0'); ++k; ++p; } if (k == 1) d *= 10; }
    if (ent) *ent = e;
    if (dec2) *dec2 = d;
    return p;
}

/* lista hablada: "a, b y c" (como mucho max nombres; luego "y N mas") */
static void lcm_lista(char *o, uint32_t cap, char nombres[][64], int n, int max)
{
    int k = n < max ? n : max;
    for (int i = 0; i < k; ++i) {
        if (i) lcm_cat(o, cap, (i == k - 1 && k == n) ? " y " : ", ");
        lcm_cat(o, cap, nombres[i]);
    }
    if (n > k) { lcm_cat(o, cap, " y "); lcm_catn(o, cap, (uint64_t)(n - k)); lcm_cat(o, cap, n - k == 1 ? " más" : " más"); }
}

/* ------------------------------------------------------------------------------------------- respuestas habladas */
static int lcm_fallo(const char *s)
{
    static const char *const marcas[] = { "Error", "error", "no encontrado", "no existe", "denegad", "fallid", "Uso:", "uso:", "No se pudo", "no se pudo", "sin memoria",
                                          "No hay ninguna", "no se que aplicacion", 0 };
    for (int i = 0; marcas[i]; ++i) if (lcm_busca(s, marcas[i])) return 1;
    return 0;
}
static void lcm_primera_linea(char *o, uint32_t cap, const char *s)
{
    uint32_t p = 0; o[0] = 0;
    while (*s == ' ' || *s == '\n') ++s;
    while (*s && *s != '\n' && p + 1 < cap) o[p++] = *s++;
    o[p] = 0;
}

/* nombre de la orden (primera palabra) y el resto */
static void lcm_partes(const char *orden, char *nom, uint32_t ncap, const char **resto)
{
    uint32_t n = 0;
    while (orden[n] && orden[n] != ' ' && n + 1 < ncap) { nom[n] = orden[n]; ++n; }
    nom[n] = 0;
    const char *r = orden + n; while (*r == ' ') ++r;
    *resto = r;
}

static int lcm_quita_comillas(char *dst, uint32_t cap, const char *s)
{
    uint32_t p = 0; for (; *s && p + 1 < cap; ++s) if (*s != '"') dst[p++] = *s;
    dst[p] = 0; return (int)p;
}

/* Que clase de orden es, para decidir si se lee su salida (y se silencia la consola) */
#define LCM_C_LEE    1      /* se entiende la salida y se dice en una frase */
#define LCM_C_MUESTRA 2     /* se ensena tal cual, con una frase delante */
#define LCM_C_ABRE   3
#define LCM_C_AJUSTA 4
#define LCM_C_HACE   5
static int lcm_clase(const char *nom, const char *resto)
{
    static const char *const lee[] = { "date", "uptime", "ram", "df", "whoami", "hostname", "ping", "wc", "find", "contiene", "status", "ps", "ls", "bateria", 0 };
    static const char *const abre[] = { "navegador", "monitor", "paint", "pdf", "imagen", "terminal", "ajustes", "camara", "grabadora", "studio", "youtube", "nano", "latex", "media", "memview", "app", 0 };
    static const char *const ajusta[] = { "brillo", "audio", "teclado", "habla", "clear", "font", "cursor", 0 };
    static const char *const hace[] = { "rm", "rmdir", "mkdir", "touch", "cp", "mv", "write", "kill", "reboot", "poweroff", "halt", "descargar", "checkpoint", "cam", "captura", "cerrar",
                                        "python", "run", "fs-sync", "rollback", "dhcp", "escritorio", "informe", 0 };
    if (str_eq_ci(nom, "energia")) return *resto ? LCM_C_AJUSTA : LCM_C_LEE;
    if (str_eq_ci(nom, "wifi")) return (lcm_empieza(resto, "conectar") || lcm_empieza(resto, "desconectar")) ? LCM_C_HACE : LCM_C_MUESTRA;
    if (lcm_es(nom, lee)) return LCM_C_LEE;
    if (lcm_es(nom, abre)) return LCM_C_ABRE;
    if (lcm_es(nom, ajusta)) return LCM_C_AJUSTA;
    if (lcm_es(nom, hace)) return LCM_C_HACE;
    return LCM_C_MUESTRA;
}

/* las que cambian algo: siempre con confirmacion (tambien las aprendidas) */
static int lcm_pide_confirmacion(const char *nom, const char *resto)
{
    static const char *const c[] = { "rm", "rmdir", "mkdir", "touch", "cp", "mv", "write", "kill", "reboot", "poweroff", "halt", "shutdown",
                                      "descargar", "fs-format", "rollback", "fs-restore", "sector_write", "big", "python", "run", "exec", "source", "sh", 0 };
    if (lcm_es(nom, c)) return 1;
    if (str_eq_ci(nom, "wifi") && (lcm_empieza(resto, "conectar") || lcm_empieza(resto, "desconectar") || lcm_empieza(resto, "olvidar"))) return 1;
    if (str_eq_ci(nom, "cam") && lcm_empieza(resto, "foto")) return 1;
    return 0;
}

extern int lcm_voz_bateria(char *o, uint32_t cap);      /* shell_apps.c (SOMA_257_LCM) */
extern int lcm_voz_energia(char *o, uint32_t cap);

static const char *lcm_app_nombre(const char *nom, const char *resto)
{
    if (str_eq_ci(nom, "navegador")) return *resto ? "la pagina" : "el navegador";
    if (str_eq_ci(nom, "monitor")) return "el monitor del sistema";
    if (str_eq_ci(nom, "paint")) return "Paint";
    if (str_eq_ci(nom, "pdf")) return "el documento";
    if (str_eq_ci(nom, "imagen")) return "la imagen";
    if (str_eq_ci(nom, "terminal")) return "un terminal";
    if (str_eq_ci(nom, "ajustes")) return "los ajustes";
    if (str_eq_ci(nom, "camara")) return "la camara";
    if (str_eq_ci(nom, "grabadora")) return "la grabadora";
    if (str_eq_ci(nom, "studio")) return "el editor de codigo";
    if (str_eq_ci(nom, "youtube")) return "YouTube";
    if (str_eq_ci(nom, "nano")) return "el editor";
    if (str_eq_ci(nom, "latex")) return "el maquetador";
    if (str_eq_ci(nom, "media")) return "el reproductor";
    if (str_eq_ci(nom, "memview")) return "la memoria de los agentes";
    if (str_eq_ci(nom, "app")) return "la aplicación";
    return "la aplicación";
}

/* La frase para la voz. pregunta = la peticion (para "que hora" frente a "que dia"). */
static void lcm_frase(char *o, uint32_t cap, const char *orden, const char *salida, int rc, const char *pregunta)
{
    char nom[32], a[200], t[300]; const char *resto;
    lcm_partes(orden, nom, sizeof nom, &resto);
    lcm_quita_comillas(a, sizeof a, resto);
    o[0] = 0;
    if (!rc || (lcm_fallo(salida) && !str_eq_ci(nom, "find") && !str_eq_ci(nom, "ping"))) {
        static const char *const no[] = { "No he podido hacerlo: {1}.", "Algo ha fallado: {1}.", "Vaya, no ha salido bien: {1}.", "No lo he conseguido. El sistema dice: {1}." };
        char l[160]; lcm_primera_linea(l, sizeof l, salida);
        if (!l[0]) lcm_cpy(l, sizeof l, "no hay respuesta de la orden");
        lcm_forma(o, cap, LCM_UNA(no), l, 0, 0);
        return;
    }
    if (str_eq_ci(nom, "date")) {
        struct lcm_fecha f; lcm_fecha_de(salida, &f);
        if (!f.ok) { lcm_cpy(o, cap, "No he podido leer el reloj."); return; }
        char pf[48]; lcm_pliega(pregunta, pf, sizeof pf);
        int quiere_hora = lcm_busca(pf, "hora") != 0, quiere_dia = lcm_busca(pf, "dia") || lcm_busca(pf, "fecha") || lcm_busca(pf, "estamos") || lcm_busca(pf, "mes") || lcm_busca(pf, "ano");
        char h[80], d[100]; lcm_hora_hablada(h, sizeof h, f.h, f.mi);
        d[0] = 0; lcm_cat(d, sizeof d, LCM_DIAS[f.dow]); lcm_cat(d, sizeof d, ", "); lcm_catn(d, sizeof d, (uint64_t)f.d); lcm_cat(d, sizeof d, " de "); lcm_cat(d, sizeof d, LCM_MESES[f.mo - 1]);
        char d2[120]; lcm_cpy(d2, sizeof d2, d); lcm_cat(d2, sizeof d2, " de "); lcm_catn(d2, sizeof d2, (uint64_t)f.y);
        int es_una = (f.h % 12 == 1 && f.mi != 45) || ((f.h + 1) % 12 == 1 && f.mi == 45);
        if (quiere_hora && !quiere_dia) {
            static const char *const fs[] = { "Son {1}.", "Ahora mismo son {1}.", "Son {1}, más o menos.", "Mira, son {1}." };
            static const char *const fu[] = { "Es {1}.", "Ahora mismo es {1}.", "Es {1}, más o menos.", "Mira, es {1}." };
            const char *tp = es_una ? LCM_UNA(fu) : LCM_UNA(fs);
            lcm_forma(o, cap, tp, h, 0, 0);
        } else if (quiere_dia && !quiere_hora) {
            static const char *const fd[] = { "Hoy es {1}.", "Estamos a {1}.", "Es {1}.", "Hoy es {1}, por si se te habia olvidado." };
            lcm_forma(o, cap, LCM_UNA(fd), d2, 0, 0);
        } else {
            static const char *const fa[] = { "Hoy es {1} y son {2}.", "Es {1}, y ahora mismo son {2}.", "Estamos a {1}; son {2}." };
            static const char *const fau[] = { "Hoy es {1} y es {2}.", "Es {1}, y ahora mismo es {2}." };
            lcm_forma(o, cap, es_una ? LCM_UNA(fau) : LCM_UNA(fa), d, h, 0);
        }
        return;
    }
    if (str_eq_ci(nom, "uptime")) {
        uint32_t s = 0; if (!lcm_num(salida, &s, 0)) { lcm_cpy(o, cap, "No he podido saber cuánto llevo encendido."); return; }
        t[0] = 0; lcm_duracion(t, sizeof t, s);
        static const char *const fu[] = { "Llevo encendido {1}.", "El sistema lleva funcionando {1}.", "Llevo {1} en marcha.", "Hace {1} que arranqué." };
        lcm_forma(o, cap, LCM_UNA(fu), t, 0, 0); return;
    }
    if (str_eq_ci(nom, "ram")) {
        const char *p = lcm_busca(salida, "RAM:"); uint32_t ue, ud, te, td, pe, pd;
        if (!p || !(p = lcm_num(p, &ue, &ud)) || !(p = lcm_num(p, &te, &td)) || !(p = lcm_num(p, &pe, &pd))) { lcm_cpy(o, cap, "Te enseño el uso de la memoria en pantalla."); return; }
        char u[40] = "", tt[40] = "", pc[24] = "";
        lcm_tamano_mib(u, sizeof u, ue * 100 + ud); lcm_tamano_mib(tt, sizeof tt, te * 100 + td);
        lcm_catn(pc, sizeof pc, pe + (pd >= 50 ? 1 : 0));
        static const char *const fr[] = { "Estás usando {1} de {2}, un {3} por ciento.", "De {2} de memoria, hay {1} en uso: el {3} por ciento.",
                                           "La memoria va bien: {1} ocupados de {2}.", "Uso {1} de los {2} que hay, un {3} por ciento." };
        static const char *const fa[] = { "La memoria va justa: {1} de {2}, el {3} por ciento.", "Ojo, estás usando {1} de {2}, un {3} por ciento." };
        lcm_forma(o, cap, pe >= 85 ? LCM_UNA(fa) : LCM_UNA(fr), u, tt, pc); return;
    }
    if (str_eq_ci(nom, "df")) {
        const char *p = lcm_busca(salida, "inodos"); uint32_t n = 0, kb = 0;
        const char *q = p; while (q && q > salida && q[-1] != '/') --q;              /* "12/64 inodos": el numero antes de la barra */
        if (q && q > salida) { --q; while (q > salida && q[-1] >= '0' && q[-1] <= '9') --q; }
        if (!p || !q || q <= salida || !lcm_num(q, &n, 0) || !lcm_num(p, &kb, 0)) { lcm_cpy(o, cap, "Te enseño el estado del disco en pantalla."); return; }
        char a1[16] = "", a2[24] = ""; lcm_catn(a1, sizeof a1, n);
        if (kb >= 1024) { lcm_decimal(a2, sizeof a2, kb / 1024, (kb % 1024) * 10 / 1024); lcm_cat(a2, sizeof a2, " megas"); } else { lcm_catn(a2, sizeof a2, kb); lcm_cat(a2, sizeof a2, " kilobytes"); }
        static const char *const fd[] = { "Hay {1} archivos guardados, que ocupan {2}.", "En el disco tienes {1} archivos; ocupan {2}.", "Tienes {1} archivos, {2} en total." };
        lcm_forma(o, cap, LCM_UNA(fd), a1, a2, 0); return;
    }
    if (str_eq_ci(nom, "whoami")) { char u[64]; lcm_primera_linea(u, sizeof u, salida); static const char *const f[] = { "Eres {1}.", "Estás como {1}.", "Tu usuario es {1}." }; lcm_forma(o, cap, LCM_UNA(f), u, 0, 0); return; }
    if (str_eq_ci(nom, "hostname")) { char u[64]; lcm_primera_linea(u, sizeof u, salida); static const char *const f[] = { "Este equipo se llama {1}.", "Me llamo {1} en la red.", "El nombre del equipo es {1}." }; lcm_forma(o, cap, LCM_UNA(f), u, 0, 0); return; }
    if (str_eq_ci(nom, "ping")) {
        const char *quien = *a ? a : "la puerta de enlace";
        if (lcm_busca(salida, "exitoso")) { static const char *const f[] = { "Sí, {1} responde.", "{1} contesta bien.", "Hay conexión con {1}.", "Todo bien: {1} responde." }; lcm_forma(o, cap, LCM_UNA(f), quien, 0, 0); }
        else { static const char *const f[] = { "{1} no responde.", "No hay respuesta de {1}.", "No llego a {1}." }; lcm_forma(o, cap, LCM_UNA(f), quien, 0, 0); }
        return;
    }
    if (str_eq_ci(nom, "wc")) {
        uint32_t l = 0, w = 0; const char *p = lcm_num(salida, &l, 0); if (!p || !lcm_num(p, &w, 0)) { lcm_cpy(o, cap, "Te enseño el recuento en pantalla."); return; }
        char sl[16] = "", sw[16] = ""; lcm_catn(sl, sizeof sl, l); lcm_catn(sw, sizeof sw, w);
        static const char *const f[] = { "{1} tiene {2} palabras en {3} líneas.", "En {1} hay {2} palabras y {3} líneas.", "{1}: {2} palabras, {3} líneas." };
        lcm_forma(o, cap, LCM_UNA(f), a, sw, sl); return;
    }
    if (str_eq_ci(nom, "find") || str_eq_ci(nom, "ls") || str_eq_ci(nom, "contiene")) {
        static char nombres[40][64]; int n = 0; const char *s = salida;
        while (*s && n < 40) {
            char ln[200]; uint32_t k = 0; while (*s && *s != '\n') { if (k + 1 < sizeof ln) ln[k++] = *s; ++s; } ln[k] = 0; if (*s) ++s;
            if (!ln[0] || lcm_busca(ln, "coincidencia") || lcm_busca(ln, "sin coincidencias") || lcm_busca(ln, "total") || lcm_busca(ln, "Total")) continue;
            /* el ultimo trozo de la linea con un punto o una barra es el nombre (si no hay, la linea no es un archivo) */
            char tok[64] = ""; const char *q = ln;
            while (*q) { while (*q == ' ' || *q == '\t' || *q == '|') ++q; char w[64]; uint32_t m = 0; int marca = 0;
                while (*q && *q != ' ' && *q != '\t' && *q != '|') { if (*q == '.' || *q == '/') marca = 1; if (m + 1 < sizeof w) w[m++] = *q; ++q; } w[m] = 0;
                if (marca && m > 1 && !(w[0] >= '0' && w[0] <= '9')) { const char *b = w; for (const char *z = w; *z; ++z) if (*z == '/' && z[1]) b = z + 1; lcm_cpy(tok, sizeof tok, b); uint32_t tl = lcm_len(tok); if (tl && tok[tl - 1] == '/') tok[tl - 1] = 0; } }
            if (tok[0]) { lcm_cpy(nombres[n], 64, tok); ++n; }
        }
        t[0] = 0; lcm_lista(t, sizeof t, nombres, n, 5);
        if (str_eq_ci(nom, "contiene")) {
            if (!n) { static const char *const f[] = { "Ningún archivo tiene «{1}».", "No encuentro «{1}» en ningún archivo.", "«{1}» no sale en ningún archivo." }; lcm_forma(o, cap, LCM_UNA(f), a, 0, 0); return; }
            char nn[12] = ""; lcm_catn(nn, sizeof nn, (uint64_t)n);
            if (n == 1) { static const char *const f[] = { "«{1}» sale en {3}.", "Solo un archivo lo tiene: {3}.", "Lo he encontrado en {3}." }; lcm_forma(o, cap, LCM_UNA(f), a, nn, t); }
            else { static const char *const f[] = { "«{1}» sale en {2} archivos: {3}.", "Hay {2} archivos con «{1}»: {3}.", "Lo tienen {2} archivos: {3}." }; lcm_forma(o, cap, LCM_UNA(f), a, nn, t); }
            return;
        }
        if (str_eq_ci(nom, "find")) {
            if (!n) { static const char *const f[] = { "No he encontrado nada que se llame {1}.", "No veo ningún archivo con {1}.", "Nada con {1}, lo siento." }; lcm_forma(o, cap, LCM_UNA(f), a, 0, 0); return; }
            char nn[12] = ""; lcm_catn(nn, sizeof nn, (uint64_t)n);
            if (n == 1) { static const char *const f[] = { "Lo tengo: {2}.", "He encontrado uno: {2}.", "Está aquí: {2}." }; lcm_forma(o, cap, LCM_UNA(f), nn, t, 0); }
            else { static const char *const f[] = { "He encontrado {1}: {2}.", "Hay {1} que encajan: {2}.", "Te salen {1}: {2}." }; lcm_forma(o, cap, LCM_UNA(f), nn, t, 0); }
            return;
        }
        if (!n) { if (!salida[0]) { lcm_cpy(o, cap, "No hay archivos ahí."); return; } static const char *const f[] = { "Ahí tienes el listado.", "Te enseño lo que hay en pantalla.", "Esto es lo que hay." }; lcm_cpy(o, cap, LCM_UNA(f)); return; }
        char nn[12] = ""; lcm_catn(nn, sizeof nn, (uint64_t)n);
        static const char *const f[] = { "Hay {1}: {2}.", "Tienes {1} cosas: {2}.", "Veo {1}: {2}.", "Aquí hay {1}: {2}." };
        if (n == 1) lcm_forma(o, cap, "Solo hay uno: {2}.", nn, t, 0); else lcm_forma(o, cap, LCM_UNA(f), nn, t, 0);
        return;
    }
    if (str_eq_ci(nom, "ps")) {
        uint32_t n = 0; const char *s = salida; int ini = 1;
        for (; *s; ++s) { if (ini) { const char *q = s; while (*q == ' ') ++q; if (*q >= '0' && *q <= '9') ++n; } ini = *s == '\n'; }
        if (!n) { lcm_cpy(o, cap, "Te enseño los procesos en pantalla."); return; }
        char nn[12] = ""; lcm_catn(nn, sizeof nn, n);
        static const char *const f[] = { "Hay {1} tareas en marcha; te las enseño.", "Ahora mismo corren {1} hilos.", "Tengo {1} tareas funcionando." };
        lcm_forma(o, cap, LCM_UNA(f), nn, 0, 0); return;
    }
    if (str_eq_ci(nom, "status")) {
        char ip[20] = ""; const char *s = salida;
        while (*s && !ip[0]) {
            if (*s >= '1' && *s <= '9' && (s == salida || !(s[-1] >= '0' && s[-1] <= '9') )) {
                uint32_t k = 0, puntos = 0; const char *q = s; char b[20];
                while (((*q >= '0' && *q <= '9') || *q == '.') && k + 1 < sizeof b) { if (*q == '.') ++puntos; b[k++] = *q++; } b[k] = 0;
                if (puntos == 3 && b[k - 1] != '.') lcm_cpy(ip, sizeof ip, b);
                s = q; continue;
            }
            ++s;
        }
        if (ip[0]) { static const char *const f[] = { "Tienes conexión; tu IP es {1}.", "Estás conectado con la dirección {1}.", "La red va: tu IP es {1}." }; lcm_forma(o, cap, LCM_UNA(f), ip, 0, 0); }
        else lcm_cpy(o, cap, "No veo una dirección de red; te enseño el estado en pantalla.");
        return;
    }
    if (str_eq_ci(nom, "bateria")) { if (!lcm_voz_bateria(o, cap)) lcm_cpy(o, cap, "No he podido leer la bateria."); return; }
    if (str_eq_ci(nom, "energia") && !*resto) { if (!lcm_voz_energia(o, cap)) lcm_cpy(o, cap, "No he podido leer el procesador."); return; }
    if (str_eq_ci(nom, "media") && lcm_empieza(a, "pause")) { static const char *const f[] = { "Pausado.", "En pausa.", "Lo dejo en pausa." }; lcm_cpy(o, cap, LCM_UNA(f)); return; }
    if (str_eq_ci(nom, "media") && lcm_empieza(a, "stop")) { static const char *const f[] = { "Parado.", "Fuera música.", "Reproducción parada." }; lcm_cpy(o, cap, LCM_UNA(f)); return; }
    if (str_eq_ci(nom, "cerrar")) {
        uint32_t nc = 0; const char *q = lcm_busca(salida, "Cerradas:");
        if (q && lcm_num(q, &nc, 0)) { char nn[12] = ""; lcm_catn(nn, sizeof nn, nc); static const char *const f[] = { "He cerrado {1} ventanas.", "Listo, {1} ventanas cerradas.", "Todo cerrado: {1} ventanas." }; static const char *const f1[] = { "He cerrado la ventana.", "Ventana cerrada." }; if (nc == 1) lcm_cpy(o, cap, LCM_UNA(f1)); else lcm_forma(o, cap, LCM_UNA(f), nn, 0, 0); return; }
        q = lcm_busca(salida, "Cerrada:");
        if (q) { char t2[64]; q += 8; while (*q == ' ') ++q; lcm_primera_linea(t2, sizeof t2, q); static const char *const f[] = { "Cierro {1}.", "Listo, {1} cerrado.", "Fuera {1}.", "{1}, cerrado." }; lcm_forma(o, cap, LCM_UNA(f), t2, 0, 0); return; }
    }
    if (str_eq_ci(nom, "dhcp")) { static const char *const f[] = { "Le pido una dirección nueva al router.", "Renuevo la IP." }; lcm_cpy(o, cap, LCM_UNA(f)); return; }
    if (str_eq_ci(nom, "fs-sync")) { static const char *const f[] = { "Todo guardado en el disco.", "Listo, ya está todo escrito en el disco." }; lcm_cpy(o, cap, LCM_UNA(f)); return; }
    if (str_eq_ci(nom, "rollback")) { lcm_cpy(o, cap, "He restaurado la copia de seguridad."); return; }
    if (str_eq_ci(nom, "escritorio")) { static const char *const f[] = { "Iconos en su sitio.", "Escritorio ordenado como al principio." }; lcm_cpy(o, cap, LCM_UNA(f)); return; }
    if (str_eq_ci(nom, "informe")) { static const char *const f[] = { "Informe hecho; lo tienes en informe.txt.", "Listo, he guardado el informe en informe.txt." }; lcm_cpy(o, cap, LCM_UNA(f)); return; }
    if (str_eq_ci(nom, "python") || str_eq_ci(nom, "run")) { static const char *const f[] = { "Pongo en marcha {1}.", "Ejecuto {1}.", "Arranco {1}." }; lcm_forma(o, cap, LCM_UNA(f), a, 0, 0); return; }
    if (str_eq_ci(nom, "voz") && *a) {
        char l[300]; lcm_primera_linea(l, sizeof l, salida);
        if (l[0]) { lcm_cpy(o, cap, "Dice: "); lcm_cat(o, cap, l); } else lcm_cpy(o, cap, "No he entendido nada en ese audio.");
        return;
    }
    if (str_eq_ci(nom, "creador") || str_eq_ci(nom, "disco") || str_eq_ci(nom, "cpus") || str_eq_ci(nom, "uname")) {
        char l[200]; lcm_primera_linea(l, sizeof l, salida);
        if (l[0]) { lcm_cpy(o, cap, l); uint32_t L = lcm_len(o); if (L && o[L - 1] != '.') lcm_cat(o, cap, "."); return; }
    }
    /* ---- abrir */
    int cl = lcm_clase(nom, resto);
    if (cl == LCM_C_ABRE) {
        const char *ap = lcm_app_nombre(nom, resto);
        if (str_eq_ci(nom, "youtube") && *a) { static const char *const f[] = { "Busco {1} en YouTube.", "Vamos a ver {1} en YouTube.", "Te pongo {1} en YouTube." }; lcm_forma(o, cap, LCM_UNA(f), a, 0, 0); return; }
        if (str_eq_ci(nom, "navegador") && *a) { static const char *const f[] = { "Abro {1}.", "Vamos a {1}.", "Te llevo a {1}." }; lcm_forma(o, cap, LCM_UNA(f), a, 0, 0); return; }
        if (*a && (str_eq_ci(nom, "pdf") || str_eq_ci(nom, "imagen") || str_eq_ci(nom, "nano") || str_eq_ci(nom, "studio") || str_eq_ci(nom, "latex"))) {
            static const char *const f[] = { "Abro {1}.", "Aquí tienes {1}.", "Te abro {1}." }; lcm_forma(o, cap, LCM_UNA(f), a, 0, 0); return; }
        static const char *const f[] = { "Abro {1}.", "Ahí tienes {1}.", "Te abro {1}.", "Listo, {1} abierto.", "Un momento... ya está {1}." };
        lcm_forma(o, cap, LCM_UNA(f), ap, 0, 0); return;
    }
    if (str_eq_ci(nom, "brillo")) {
        if (a[0] == '+') { static const char *const f[] = { "Subo el brillo.", "Un poco más de luz.", "Brillo arriba." }; lcm_cpy(o, cap, LCM_UNA(f)); return; }
        if (a[0] == '-') { static const char *const f[] = { "Bajo el brillo.", "Un poco menos de luz.", "Brillo abajo." }; lcm_cpy(o, cap, LCM_UNA(f)); return; }
        static const char *const f[] = { "Brillo al {1} por ciento.", "Listo, el brillo está al {1} por ciento.", "Pongo el brillo al {1} por ciento." }; lcm_forma(o, cap, LCM_UNA(f), a, 0, 0); return;
    }
    if (str_eq_ci(nom, "audio") && lcm_empieza(a, "vol")) {
        uint32_t v = 0; lcm_num(a, &v, 0); char nv[12] = ""; lcm_catn(nv, sizeof nv, v);
        static const char *const f[] = { "Volumen al {1} por ciento.", "Listo, volumen al {1}.", "Pongo el volumen al {1} por ciento." }; lcm_forma(o, cap, LCM_UNA(f), nv, 0, 0); return;
    }
    if (str_eq_ci(nom, "teclado")) { lcm_cpy(o, cap, lcm_busca(a, "us") ? "Teclado en ingles." : "Teclado en espanol."); return; }
    if (str_eq_ci(nom, "habla")) { o[0] = 0; return; }                    /* ya lo dice la voz: sin frase encima */
    if (str_eq_ci(nom, "clear")) { lcm_cpy(o, cap, "Pantalla limpia."); return; }
    if (str_eq_ci(nom, "energia")) { static const char *const f[] = { "Cambio el perfil a {1}.", "Listo, modo {1}." }; lcm_forma(o, cap, LCM_UNA(f), a, 0, 0); return; }
    /* ---- hechas (tras confirmar) */
    if (cl == LCM_C_HACE) {
        char a1[100] = "", a2[100] = ""; { const char *q = a; uint32_t k = 0; while (*q && *q != ' ' && k + 1 < sizeof a1) a1[k++] = *q++; a1[k] = 0; while (*q == ' ') ++q; lcm_cpy(a2, sizeof a2, q); }
        const char *tp = "Hecho.";
        if (str_eq_ci(nom, "rm")) { static const char *const f[] = { "Borrado {1}.", "Listo, {1} ya no está.", "He borrado {1}.", "Fuera {1}." }; tp = LCM_UNA(f); }
        else if (str_eq_ci(nom, "rmdir")) { static const char *const f[] = { "Carpeta {1} borrada.", "He quitado la carpeta {1}." }; tp = LCM_UNA(f); }
        else if (str_eq_ci(nom, "mkdir")) { static const char *const f[] = { "Carpeta {1} creada.", "Ya tienes la carpeta {1}.", "He creado la carpeta {1}." }; tp = LCM_UNA(f); }
        else if (str_eq_ci(nom, "touch")) { static const char *const f[] = { "Creado {1}, vacío.", "Ya existe {1}.", "He creado el archivo {1}." }; tp = LCM_UNA(f); }
        else if (str_eq_ci(nom, "cp")) { static const char *const f[] = { "Copiado {1} en {2}.", "Ya tienes la copia en {2}.", "He copiado {1} a {2}." }; tp = LCM_UNA(f); }
        else if (str_eq_ci(nom, "mv")) { static const char *const f[] = { "Movido {1} a {2}.", "Listo, {1} ahora es {2}.", "He pasado {1} a {2}." }; tp = LCM_UNA(f); }
        else if (str_eq_ci(nom, "write")) { static const char *const f[] = { "Guardado en {1}.", "Apuntado en {1}.", "Ya está escrito en {1}." }; tp = LCM_UNA(f); }
        else if (str_eq_ci(nom, "kill")) { static const char *const f[] = { "Tarea {1} terminada.", "He parado la tarea {1}." }; tp = LCM_UNA(f); }
        else if (str_eq_ci(nom, "reboot")) tp = "Reiniciando. Hasta ahora.";
        else if (str_eq_ci(nom, "poweroff") || str_eq_ci(nom, "halt")) tp = "Apagando. Hasta luego.";
        else if (str_eq_ci(nom, "descargar")) { static const char *const f[] = { "Descargado.", "Ya lo tienes descargado.", "Descarga terminada." }; tp = LCM_UNA(f); }
        else if (str_eq_ci(nom, "checkpoint")) { static const char *const f[] = { "Copia de seguridad hecha.", "Todo guardado en la copia de seguridad." }; tp = LCM_UNA(f); }
        else if (str_eq_ci(nom, "cam") || str_eq_ci(nom, "captura")) { static const char *const f[] = { "Foto hecha.", "Listo, ya tienes la foto.", "Click. Foto guardada." }; tp = LCM_UNA(f); }
        else if (str_eq_ci(nom, "wifi")) {
            if (lcm_empieza(a, "conectar")) { char r[100]; const char *q = a + 8; while (*q == ' ') ++q; uint32_t k = 0; while (*q && *q != ' ' && k + 1 < sizeof r) r[k++] = *q++; r[k] = 0;
                static const char *const f[] = { "Conectando a {3}.", "Me conecto a {3}.", "Intento entrar en {3}." }; lcm_forma(o, cap, LCM_UNA(f), 0, 0, r); return; }
            tp = "WiFi desconectada.";
        }
        else if (str_eq_ci(nom, "cerrar")) { if (*a) { static const char *const f[] = { "Cierro {1}.", "Listo, {1} cerrado.", "Fuera {1}." }; tp = LCM_UNA(f); } else tp = "Cerrada."; }
        lcm_forma(o, cap, tp, a1, a2, 0);
        return;
    }
    /* ---- se ensena tal cual */
    if (str_eq_ci(nom, "cat") || str_eq_ci(nom, "head") || str_eq_ci(nom, "tail")) { static const char *const f[] = { "Esto es lo que pone en {1}.", "Aquí tienes {1}.", "Te enseño {1}." }; char f1[100]; uint32_t k = 0; const char *q = a; while (*q) { if (*q == ' ') k = 0, ++q; else { if (k + 1 < sizeof f1) f1[k++] = *q; ++q; } } f1[k] = 0; lcm_forma(o, cap, LCM_UNA(f), f1, 0, 0); return; }
    if (str_eq_ci(nom, "search")) { static const char *const f[] = { "Esto he encontrado sobre {1}.", "Mira lo que sale de {1}.", "Te enseño lo que hay sobre {1}." }; lcm_forma(o, cap, LCM_UNA(f), a, 0, 0); return; }
    if (str_eq_ci(nom, "wifi")) { static const char *const f[] = { "Estas son las redes que veo.", "Te enseño las redes WiFi.", "Aquí tienes las redes de alrededor." }; lcm_cpy(o, cap, LCM_UNA(f)); return; }
    if (str_eq_ci(nom, "dns")) { static const char *const f[] = { "Te pongo la dirección de {1} en pantalla.", "He consultado {1}; lo tienes en pantalla." }; lcm_forma(o, cap, LCM_UNA(f), a, 0, 0); return; }
    { static const char *const f[] = { "Ahi lo tienes.", "Te lo enseno en pantalla.", "Aquí está.", "Listo, mira la pantalla." }; lcm_cpy(o, cap, LCM_UNA(f)); }
}

/* La pregunta de confirmacion. */
static void lcm_pregunta(char *o, uint32_t cap, const char *orden)
{
    char nom[32], a[200], a1[100] = "", a2[100] = ""; const char *resto;
    lcm_partes(orden, nom, sizeof nom, &resto); lcm_quita_comillas(a, sizeof a, resto);
    { const char *q = a; uint32_t k = 0; while (*q && *q != ' ' && k + 1 < sizeof a1) a1[k++] = *q++; a1[k] = 0; while (*q == ' ') ++q; lcm_cpy(a2, sizeof a2, q); }
    const char *tp;
    if (str_eq_ci(nom, "rm")) { static const char *const f[] = { "¿Borro {1}? Dime sí o no.", "Voy a borrar {1}. ¿Seguro?", "¿De verdad quieres que borre {1}?", "{1} se borrara y no hay vuelta atrás. ¿Lo hago?" }; tp = LCM_UNA(f); }
    else if (str_eq_ci(nom, "rmdir")) { static const char *const f[] = { "¿Borro la carpeta {1}?", "Voy a quitar la carpeta {1}. ¿Seguro?" }; tp = LCM_UNA(f); }
    else if (str_eq_ci(nom, "mkdir")) { static const char *const f[] = { "¿Creo la carpeta {1}?", "Te hago una carpeta llamada {1}, ¿vale?", "¿Quieres una carpeta nueva llamada {1}?" }; tp = LCM_UNA(f); }
    else if (str_eq_ci(nom, "touch")) { static const char *const f[] = { "¿Creo el archivo {1}?", "Hago un archivo vacío llamado {1}, ¿de acuerdo?" }; tp = LCM_UNA(f); }
    else if (str_eq_ci(nom, "cp")) { static const char *const f[] = { "¿Copio {1} en {2}?", "Hago una copia de {1} como {2}, ¿vale?", "Copiar {1} a {2}: ¿lo hago?" }; tp = LCM_UNA(f); }
    else if (str_eq_ci(nom, "mv")) { static const char *const f[] = { "¿Muevo {1} a {2}?", "{1} pasara a ser {2}. ¿Vale?", "¿Paso {1} a {2}?" }; tp = LCM_UNA(f); }
    else if (str_eq_ci(nom, "write")) { static const char *const f[] = { "¿Escribo «{2}» en {1}? Si ya tenia algo, se sustituye.", "Voy a guardar «{2}» en {1}, ¿vale?", "Apunto «{2}» en {1}. ¿Confirmas?" }; tp = LCM_UNA(f); }
    else if (str_eq_ci(nom, "kill")) { static const char *const f[] = { "¿Termino la tarea {1}?", "Voy a parar la tarea {1}. ¿Seguro?" }; tp = LCM_UNA(f); }
    else if (str_eq_ci(nom, "reboot")) { static const char *const f[] = { "¿Reinicio el equipo? Se cerrara todo.", "Voy a reiniciar. ¿Lo hago?", "¿Seguro que reinicio?" }; tp = LCM_UNA(f); }
    else if (str_eq_ci(nom, "poweroff") || str_eq_ci(nom, "halt") || str_eq_ci(nom, "shutdown")) { static const char *const f[] = { "¿Apago el equipo?", "Voy a apagar. ¿Seguro?", "¿Apago ya?" }; tp = LCM_UNA(f); }
    else if (str_eq_ci(nom, "descargar")) { static const char *const f[] = { "¿Descargo {1}?", "Bajo {1}, ¿vale?" }; tp = LCM_UNA(f); }
    else if (str_eq_ci(nom, "wifi")) { static const char *const f[] = { "¿Cambio la conexión WiFi ({1} {2})?", "Voy a tocar la WiFi: {1} {2}. ¿Vale?" }; tp = LCM_UNA(f); }
    else if (str_eq_ci(nom, "cam")) { static const char *const f[] = { "¿Hago una foto con la cámara?", "Voy a usar la cámara para una foto. ¿Vale?" }; tp = LCM_UNA(f); }
    else { static const char *const f[] = { "Voy a ejecutar {3}. ¿Lo hago?", "¿Ejecuto {3}?" }; tp = LCM_UNA(f); }
    lcm_forma(o, cap, tp, a1, a2, orden);
}

/* ------------------------------------------------------------------------------------------- charla */
static int lcm_hora_local(void) { struct lcm_fecha f; lcm_hora_ahora(&f); return f.ok ? f.h : -1; }
static const char *lcm_saludo_hora(int h) { return h < 0 ? "Hola" : (h >= 6 && h < 13) ? "Buenos días" : (h >= 13 && h < 21) ? "Buenas tardes" : "Buenas noches"; }

/* Si la frase (pliegada) empieza por un saludo, devuelve donde acaba; si no, 0 */
static const char *lcm_saludo_delante(const char *f)
{
    static const char *const s[] = { "hola buenos dias ", "hola buenas tardes ", "hola buenas noches ", "hola buenas ", "buenos dias ", "buenas tardes ", "buenas noches ", "buen dia ",
                                     "hola ", "buenas ", "holi ", "saludos ", "que tal ", "hey ", 0 };
    for (int i = 0; s[i]; ++i) { uint32_t n = lcm_len(s[i]); if (lcm_empieza(f, s[i])) return f + n; char c[24]; lcm_cpy(c, sizeof c, s[i]); c[n - 1] = 0; if (str_eq_ci(f, c)) return f + n - 1; }
    return 0;
}

/* Charla completa: devuelve 1 y deja la respuesta en o */
static int lcm_charla(const char *f, char *o, uint32_t cap, int solo_saludo_previo)
{
    static const char *const como[] = { "como estas", "que tal estas", "como te va", "como andas", "que tal te va", "que tal vas", "como va todo", "como va", "estas bien",
                                        "que tal todo", "como te encuentras", "que tal el dia", "como lo llevas", "todo bien", "que tal", 0 };
    static const char *const grac[] = { "gracias", "muchas gracias", "mil gracias", "te lo agradezco", "genial gracias", "perfecto gracias", "vale gracias", "gracias soma",
                                        "muy amable", "gracias majo", "graciass", "thanks", "ok gracias", "estupendo gracias", 0 };
    static const char *const adios[] = { "adios", "hasta luego", "hasta manana", "nos vemos", "chao", "chau", "hasta pronto", "me voy", "hasta la vista", "buenas noches me voy",
                                         "hasta otra", "adios soma", 0 };
    static const char *const quien[] = { "quien eres", "como te llamas", "que eres", "eres una ia", "eres un robot", "eres una inteligencia artificial", "quien eres tu", "tu quien eres",
                                         "presentate", "como te llaman", 0 };
    static const char *const sabes[] = { "que sabes hacer", "que puedes hacer", "en que me puedes ayudar", "que haces", "para que sirves", "que cosas sabes hacer", "que me puedes hacer",
                                         "en que me ayudas", "que sabes", "que cosas haces", 0 };
    static const char *const bien[] = { "bien", "muy bien", "bien y tu", "bien gracias", "bien gracias y tu", "estupendo", "genial", "fenomenal", "de maravilla", "todo bien y tu",
                                        "bien bien", "perfecto", "guay", 0 };
    static const char *const mal[] = { "mal", "regular", "no muy bien", "fatal", "un poco mal", "cansado", "cansada", "estoy cansado", "estoy cansada", 0 };
    static const char *const ok[] = { "vale", "ok", "okey", "de acuerdo", "entendido", "perfecto", "genial", "estupendo", "muy bien", "fantastico", "bien hecho", "buen trabajo", 0 };
    int h = -1;
    if (lcm_es(f, como)) {
        static const char *const r[] = { "Muy bien, gracias por preguntar. ¿Y tú?", "Todo en orden por aquí. ¿Qué tal tú?", "Funcionando a tope. ¿Cómo estás tú?",
                                          "Bien, con ganas de ayudarte. ¿Qué necesitas?", "Pues muy bien, la verdad. ¿Y tú qué tal?", "Estupendamente. Todos mis sistemas van bien. ¿Y tú?",
                                          "De maravilla. ¿En qué te echo una mano?", "Bien, aquí estoy. ¿Qué tal tu día?" };
        lcm_cpy(o, cap, LCM_UNA(r)); return 1;
    }
    if (lcm_es(f, grac)) {
        static const char *const r[] = { "De nada.", "A ti.", "Para eso estoy.", "No hay de qué.", "Un placer.", "Cuando quieras.", "Faltaría más.", "Encantado de ayudar.", "De nada, aquí me tienes." };
        lcm_cpy(o, cap, LCM_UNA(r)); return 1;
    }
    if (lcm_es(f, adios)) {
        h = lcm_hora_local();
        static const char *const r[] = { "Hasta luego.", "Hasta pronto.", "Nos vemos.", "Adiós, que vaya bien.", "Hasta la próxima.", "Aquí estaré cuando me necesites." };
        static const char *const rn[] = { "Buenas noches, que descanses.", "Hasta mañana. Que duermas bien.", "Buenas noches." };
        lcm_cpy(o, cap, (h >= 21 || (h >= 0 && h < 6)) ? LCM_UNA(rn) : LCM_UNA(r)); return 1;
    }
    if (lcm_es(f, quien)) {
        static const char *const r[] = { "Soy SOMA, el sistema de este equipo. Puedo abrir aplicaciones, mirar archivos, decirte la hora y muchas cosas más.",
                                          "Soy SOMA. Entiendo lo que me pides y lo convierto en órdenes del sistema; lo que no sé, se lo paso al modelo de lenguaje.",
                                          "Me llamo SOMA. Soy el asistente de este ordenador.", "SOMA, para servirte. Vivo dentro de este sistema operativo." };
        lcm_cpy(o, cap, LCM_UNA(r)); return 1;
    }
    if (lcm_es(f, sabes)) {
        static const char *const r[] = { "Puedo abrir aplicaciones, decirte la hora o la batería, buscar y mirar archivos, crear carpetas, conectar la WiFi, ajustar el brillo y el volumen, y mucho más. Lo que no sepa hacer, se lo paso al modelo de lenguaje.",
                                          "Un poco de todo: archivos, aplicaciones, red, batería, memoria, brillo, volumen... Pídemelo con tus palabras. Para preguntas largas uso el modelo de lenguaje.",
                                          "Dime cosas como: qué hora es, abre el navegador, cuánta batería queda, crea una carpeta llamada fotos o busca el archivo notas. Y si es una pregunta general, te contesta el modelo de lenguaje." };
        lcm_cpy(o, cap, LCM_UNA(r)); return 1;
    }
    if (!solo_saludo_previo && lcm_es(f, bien)) {
        static const char *const r[] = { "Me alegro mucho.", "Qué bien. ¿En qué te ayudo?", "Estupendo. Aquí estoy si necesitas algo.", "Genial. ¿Qué hacemos?", "Me alegro. Yo también, gracias." };
        lcm_cpy(o, cap, LCM_UNA(r)); return 1;
    }
    if (!solo_saludo_previo && lcm_es(f, mal)) {
        static const char *const r[] = { "Vaya, lo siento. Si puedo ayudarte con algo, dímelo.", "Ánimo. ¿Te pongo algo de música en YouTube?", "Lo siento. Aquí estoy para lo que necesites." };
        lcm_cpy(o, cap, LCM_UNA(r)); return 1;
    }
    if (!solo_saludo_previo && lcm_es(f, ok)) {
        static const char *const r[] = { "Perfecto.", "Estupendo.", "Genial.", "De acuerdo.", "Muy bien." };
        lcm_cpy(o, cap, LCM_UNA(r)); return 1;
    }
    return 0;
}

static const char *lcm_saludo_de(const char *f)        /* el saludo que uso la persona (pliegado), o el de la hora */
{
    if (lcm_busca(f, "buenos dias") || lcm_busca(f, "buen dia")) return "Buenos días";
    if (lcm_busca(f, "buenas tardes")) return "Buenas tardes";
    if (lcm_busca(f, "buenas noches")) return "Buenas noches";
    return lcm_saludo_hora(lcm_hora_local());
}

/* Saludo solo */
static void lcm_responde_saludo(char *o, uint32_t cap, const char *f)
{
    const char *sh = lcm_saludo_de(f);
    static const char *const r[] = { "{1}. ¿En qué te ayudo?", "{1}. ¿Qué necesitas?", "¡{1}! Aquí estoy.", "{1}. ¿Qué hacemos hoy?", "Hola, {2}. Dime.", "{1}. Cuéntame." };
    char shm[24]; lcm_cpy(shm, sizeof shm, sh); if (shm[0] >= 'A' && shm[0] <= 'Z') shm[0] = (char)(shm[0] + 32);
    lcm_forma(o, cap, LCM_UNA(r), sh, shm, 0);
}

/* ------------------------------------------------------------------------------------------- aprendido */
static void lcm_apr_guarda(void)
{
    static char b[LCM_APR_MAX * 330]; uint32_t p = 0; b[0] = 0;
    for (int i = 0; i < lcm_apr_n; ++i) { fb_puts(b, sizeof b, &p, lcm_apr_pat[i]); fb_puts(b, sizeof b, &p, "\t"); fb_puts(b, sizeof b, &p, lcm_apr_ord[i]); fb_puts(b, sizeof b, &p, "\n"); }
    vfs_write("/etc/lcm_aprendido.txt", b, p);
}
static void lcm_apr_carga(void)
{
    static char b[LCM_APR_MAX * 330];
    int n = vfs_read("/etc/lcm_aprendido.txt", b, sizeof(b) - 1);
    lcm_apr_n = 0;
    if (n <= 0) return;
    b[n] = 0;
    char *s = b;
    while (*s && lcm_apr_n < LCM_APR_MAX) {
        char *t = s; while (*t && *t != '\t' && *t != '\n') ++t;
        if (*t != '\t') { while (*t && *t != '\n') ++t; s = *t ? t + 1 : t; continue; }
        *t = 0; char *u = t + 1; char *v = u; while (*v && *v != '\n') ++v; char fin = *v; *v = 0;
        if (*s && *u) { lcm_cpy(lcm_apr_pat[lcm_apr_n], 160, s); lcm_cpy(lcm_apr_ord[lcm_apr_n], 160, u); ++lcm_apr_n; }
        s = fin ? v + 1 : v;
    }
}

/* palabras de la frase (pliegada), sin las de cortesia */
static int lcm_palabras(const char *frase, char w[][64], int max)
{
    char f[300]; lcm_pliega(frase, f, sizeof f);
    const char *p = lcm_sin_llamada(f);
    static const char *const fuera[] = { "porfa", "gracias", "xfa", "plis", "please", 0 };
    int n = 0;
    while (*p && n < max) {
        while (*p == ' ') ++p;
        uint32_t k = 0; char x[64]; while (*p && *p != ' ') { if (k + 1 < sizeof x) x[k++] = *p; ++p; } x[k] = 0;
        if (!k || lcm_es(x, fuera)) continue;
        if (str_eq_ci(x, "por") && lcm_empieza(p, " favor")) { p += 6; continue; }
        lcm_cpy(w[n], 64, x); ++n;
    }
    return n;
}

/* Aprende: frase -> orden. Las palabras de la orden que estan en la frase pasan a ser huecos ({1}, {2}, {3}). */
static int lcm_aprende(const char *frase, const char *orden)
{
    char w[24][64]; int n = lcm_palabras(frase, w, 24);
    if (!n || !orden[0]) return 0;
    char ord[160]; lcm_cpy(ord, sizeof ord, orden);
    char nom[32]; const char *resto; lcm_partes(ord, nom, sizeof nom, &resto);
    char arg[8][64]; int na = 0; { const char *q = resto; while (*q && na < 8) { while (*q == ' ' || *q == '"') ++q; uint32_t k = 0; while (*q && *q != ' ' && *q != '"') { if (k + 1 < 64) arg[na][k++] = *q; ++q; } arg[na][k] = 0; if (k) ++na; } }
    char pat[160] = "", plt[160] = ""; int hueco = 0; char usado[3][64]; int nu = 0;
    for (int i = 0; i < n; ++i) {
        int es = 0;
        for (int j = 0; j < na && hueco < 3; ++j) {
            char aj[64]; lcm_pliega(arg[j], aj, sizeof aj);
            if (str_eq_ci(aj, w[i])) { es = 1; break; }
        }
        if (i) lcm_cat(pat, sizeof pat, " ");
        if (es && hueco < 3) { char h[4] = { '{', (char)('1' + hueco), '}', 0 }; lcm_cat(pat, sizeof pat, h); lcm_cpy(usado[nu++], 64, w[i]); ++hueco; }
        else lcm_cat(pat, sizeof pat, w[i]);
    }
    /* la plantilla: la orden con los mismos huecos */
    { const char *q = ord; uint32_t p = 0; plt[0] = 0;
      while (*q) {
          if (*q == ' ' || *q == '"') { char c[2] = { *q, 0 }; fb_puts(plt, sizeof plt, &p, c); ++q; continue; }
          char tok[64]; uint32_t k = 0; while (*q && *q != ' ' && *q != '"') { if (k + 1 < sizeof tok) tok[k++] = *q; ++q; } tok[k] = 0;
          char tf[64]; lcm_pliega(tok, tf, sizeof tf); int h = -1;
          for (int j = 0; j < nu; ++j) if (str_eq_ci(tf, usado[j])) h = j;
          if (h >= 0 && p > 0) { char hh[4] = { '{', (char)('1' + h), '}', 0 }; fb_puts(plt, sizeof plt, &p, hh); }
          else fb_puts(plt, sizeof plt, &p, tok);
      } }
    for (int i = 0; i < lcm_apr_n; ++i) if (str_eq_ci(lcm_apr_pat[i], pat)) { lcm_cpy(lcm_apr_ord[i], 160, plt); lcm_apr_guarda(); return 1; }
    if (lcm_apr_n >= LCM_APR_MAX) { for (int i = 1; i < lcm_apr_n; ++i) { lcm_cpy(lcm_apr_pat[i - 1], 160, lcm_apr_pat[i]); lcm_cpy(lcm_apr_ord[i - 1], 160, lcm_apr_ord[i]); } --lcm_apr_n; }
    lcm_cpy(lcm_apr_pat[lcm_apr_n], 160, pat); lcm_cpy(lcm_apr_ord[lcm_apr_n], 160, plt); ++lcm_apr_n;
    lcm_apr_guarda();
    return 1;
}

/* Busca la frase en lo aprendido. Los huecos toman la palabra ORIGINAL (con sus mayusculas) de la misma posicion. */
static int lcm_aprendido_busca(const char *frase, char *orden, uint32_t cap)
{
    char w[24][64]; int n = lcm_palabras(frase, w, 24);
    if (!n) return 0;
    for (int e = lcm_apr_n - 1; e >= 0; --e) {
        const char *p = lcm_apr_pat[e]; int i = 0, ok = 1; char val[3][64] = { "", "", "" };
        while (*p && ok) {
            while (*p == ' ') ++p;
            char t[64]; uint32_t k = 0; while (*p && *p != ' ') { if (k + 1 < sizeof t) t[k++] = *p; ++p; } t[k] = 0;
            if (!k) break;
            if (i >= n) { ok = 0; break; }
            if (t[0] == '{' && t[2] == '}' && t[1] >= '1' && t[1] <= '3') lcm_cpy(val[t[1] - '1'], 64, w[i]);
            else if (!str_eq_ci(t, w[i])) ok = 0;
            ++i;
        }
        if (!ok || i != n) continue;
        /* los huecos con la forma original de la peticion (lcm_palabras pliega: se recupera del texto) */
        for (int h = 0; h < 3; ++h) if (val[h][0]) {
            const char *s = frase;
            while (*s) { const char *a = s; uint32_t k = 0; char o2[64]; while (*s && *s != ' ') { if (k + 1 < sizeof o2) o2[k++] = *s; ++s; } o2[k] = 0; while (*s == ' ') ++s;
                (void)a; char of[64]; lcm_pliega(o2, of, sizeof of); if (str_eq_ci(of, val[h])) { uint32_t L = lcm_len(o2); while (L && (o2[L - 1] == '?' || o2[L - 1] == ',' || o2[L - 1] == '!' || o2[L - 1] == '.')) o2[--L] = 0; lcm_cpy(val[h], 64, o2); break; } }
        }
        char x[160]; lcm_forma(x, sizeof x, lcm_apr_ord[e], val[0], val[1], val[2]);
        lcm_cpy(orden, cap, x);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------- configuracion */
static void lcm_cfg_guarda(void)
{
    char b[320]; b[0] = 0;                                                       /* SOMA_266_AVISO: antes 80 */
    lcm_cat(b, sizeof b, "activo="); lcm_catn(b, sizeof b, (uint64_t)lcm_activo);
    lcm_cat(b, sizeof b, "\nvoz="); lcm_catn(b, sizeof b, (uint64_t)lcm_voz);
    lcm_cat(b, sizeof b, "\numbral="); lcm_catn(b, sizeof b, (uint64_t)lcm_umbral); lcm_cat(b, sizeof b, "\n");
    if (lcm_letra >= 0) { lcm_cat(b, sizeof b, "letra="); lcm_catn(b, sizeof b, (uint64_t)lcm_letra); lcm_cat(b, sizeof b, "\n"); }   /* SOMA_259_ORBE */
    lcm_cat(b, sizeof b, "aviso="); lcm_catn(b, sizeof b, (uint64_t)lcm_aviso); lcm_cat(b, sizeof b, "\n");           /* SOMA_266_AVISO */
    for (int t = 1; t < LCM_AV_N; ++t) if (lcm_av_n[t]) {
        char k[8] = "t0="; k[1] = (char)('0' + t);
        lcm_cat(b, sizeof b, k); lcm_catn(b, sizeof b, lcm_av_ms[t]); lcm_cat(b, sizeof b, "/"); lcm_catn(b, sizeof b, lcm_av_n[t]); lcm_cat(b, sizeof b, "\n");
    }
    vfs_write("/etc/lcm.conf", b, lcm_len(b));
}
static int lcm_cfg_valor(const char *c, const char *clave, int def)
{
    const char *p = lcm_busca(c, clave); if (!p) return def;
    p += lcm_len(clave); uint32_t v = 0; if (!lcm_num(p, &v, 0)) return def; return (int)v;
}
static int lcm_letra_v(void)                         /* SOMA_259_ORBE: sin cargar el modelo (la pide la GUI al dibujar) */
{
    if (lcm_letra < 0) {
        char c[320]; int n = vfs_read("/etc/lcm.conf", c, sizeof(c) - 1);     /* SOMA_266_AVISO: antes 96 */
        int v = 2;
        if (n > 0) { c[n] = 0; v = lcm_cfg_valor(c, "letra=", 2); }
        lcm_letra = (v < 0 || v > 2) ? 2 : v;
    }
    return lcm_letra;
}
static int lcm_listo(void)
{
    if (!lcm_cargado) {
        lcm_cargado = 1;
        char c[320]; int n = vfs_read("/etc/lcm.conf", c, sizeof(c) - 1);     /* SOMA_266_AVISO: antes 96 */
        if (n > 0) {
            c[n] = 0;
            if (lcm_busca(c, "activo=")) lcm_activo = lcm_cfg_valor(c, "activo=", 1) != 0;
            else if (mayordomo_has(c, "off")) lcm_activo = 0;                    /* formato del parche 253 */
            lcm_voz = lcm_cfg_valor(c, "voz=", 1) != 0;
            lcm_umbral = lcm_cfg_valor(c, "umbral=", 0);
            lcm_aviso = lcm_cfg_valor(c, "aviso=", 1) != 0;                       /* SOMA_266_AVISO */
            for (int t = 1; t < LCM_AV_N; ++t) {
                char k[8] = "t0="; k[1] = (char)('0' + t);
                const char *q = lcm_busca(c, k); uint32_t ms = 0, cu = 0;
                if (q && (q == c || q[-1] == '\n') && q[3] >= '0' && q[3] <= '9' && (q = lcm_num(q + 3, &ms, 0)) != 0 && *q == '/' && q[1] >= '0' && q[1] <= '9'
                    && lcm_num(q + 1, &cu, 0) && ms && cu) { lcm_av_ms[t] = ms; lcm_av_n[t] = cu; }
            }
        }
        if (!lcm_carga(lcm_ordenes_blob, (uint32_t)sizeof(lcm_ordenes_blob))) {
            kprint("\n[LCM] El modelo del traductor no es valido: desactivado (todo sigue por el camino de siempre).\n");
            lcm_activo = 0;
        }
        if (lcm_m.ok && lcm_umbral >= 50 && lcm_umbral <= 99) lcm_m.umbral_mil = (uint32_t)lcm_umbral * 10;
        lcm_apr_carga();
    }
    return lcm_activo && lcm_m.ok;
}

static int lcm_del_usuario(void)
{
    extern int shell_agent_mode(void);
    return !shell_agent_mode() && delegation_depth == 0;
}

/* ------------------------------------------------------------------------------------------- SOMA_266_AVISO: aviso inmediato */
/* "unos 20 segundos", "alrededor de un minuto"... por lo que tardaron las anteriores; 0 si aun no hay ninguna */
static int lcm_aviso_tiempo(int tipo, char *o, uint32_t cap)
{
    o[0] = 0;
    if (tipo < 1 || tipo >= LCM_AV_N || !lcm_av_n[tipo]) return 0;
    uint32_t s = (lcm_av_ms[tipo] + 500) / 1000;
    if (s < 8) lcm_cpy(o, cap, "solo unos segundos");
    else if (s < 55) { lcm_cpy(o, cap, "unos "); lcm_catn(o, cap, (s + 2) / 5 * 5); lcm_cat(o, cap, " segundos"); }
    else if (s < 100) lcm_cpy(o, cap, "alrededor de un minuto");
    else { lcm_cpy(o, cap, "unos "); lcm_catn(o, cap, (s + 30) / 60); lcm_cat(o, cap, " minutos"); }
    return 1;
}
/* lo que tardo de verdad (desde el aviso hasta la respuesta): media movida, para la proxima vez */
static void lcm_aviso_mide(int tipo, uint64_t ms)
{
    if (tipo < 1 || tipo >= LCM_AV_N || !ms || ms > 3600000u) return;
    (void)lcm_listo();
    if (!lcm_av_n[tipo]) lcm_av_ms[tipo] = (uint32_t)ms;
    else lcm_av_ms[tipo] = (uint32_t)(((uint64_t)lcm_av_ms[tipo] * 3u + ms) / 4u);   /* pesan mas las ultimas */
    if (lcm_av_n[tipo] < 1000000u) lcm_av_n[tipo]++;
    lcm_cfg_guarda();
}
static int lcm_tiene(const char *f, const char *const *l) { for (int i = 0; l[i]; ++i) if (lcm_busca(f, l[i])) return 1; return 0; }
/* El aviso (1) o nada (0: LCM apagado, avisos apagados, o no lo pide el usuario). lenguaje: "C", "Python"... o 0; editar: modificar
 * algo que ya existe (un programa, un documento). En castellano y para la voz: sin numeros de lista ni simbolos. */
static int lcm_aviso_texto(int tipo, const char *mission, const char *lenguaje, int editar, char *o, uint32_t cap)
{
    o[0] = 0;
    if (!mission || !lcm_listo() || !lcm_aviso || !lcm_del_usuario() || tipo < 1 || tipo >= LCM_AV_N) return 0;
    char f[512]; lcm_pliega(mission, f, sizeof f);
    lcm_siembra(mission);
    {   /* "buenas tardes, hazme..." -> "Buenas tardes. Voy a..." */
        const char *g = lcm_sin_llamada(f), *tras = lcm_saludo_delante(g);
        if (tras) { while (*tras == ' ') ++tras; if (*tras) { lcm_cpy(o, cap, lcm_saludo_de(g)); lcm_cat(o, cap, ". "); } }
    }
    static const char *const web[] = { "busca", "buscame", "internet", "noticia", "web", "precio", "cotiza", "que tiempo", "el tiempo en", "tiempo hara",
                                       "quien gano", "resultado", "ultima hora", "actualidad", "hoy en", "investiga", "informate", "en google", 0 };
    static const char *const arch[] = { "archivo", "fichero", "carpeta", "directorio", "documento", "/workspace", "/mis_", "disco", 0 };
    char pasos[400];
    if (tipo == LCM_AV_DIRECTA) {
        static const char *const f1[] = { "Esto se lo paso al modelo de IA: lo piensa y te contesto.", "Para esto necesito al modelo de IA. Se lo pregunto y te respondo.",
                                           "Se lo pregunto al modelo de IA y te cuento lo que diga.", "Esto lo tiene que pensar el modelo de IA; en cuanto responda, te lo digo." };
        lcm_cat(o, cap, LCM_UNA(f1));
    } else if (tipo == LCM_AV_PROGRAMA) {
        char l[24] = ""; if (lenguaje && lenguaje[0]) { lcm_cpy(l, sizeof l, " en "); lcm_cat(l, sizeof l, lenguaje); }
        if (editar) {
            static const char *const f1[] = { "Voy a cambiar el programa{1} con el modelo de IA: primero leo el que tienes, después le pido los cambios y al final lo compruebo y lo pruebo.",
                                               "Lo arreglo{1} con el modelo de IA: leo el programa, le pido los cambios, y antes de guardarlo lo compruebo y lo pruebo." };
            lcm_forma(pasos, sizeof pasos, LCM_UNA(f1), l, 0, 0);
        } else {
            static const char *const f1[] = { "Voy a programarlo{1} con el modelo de IA: primero planifico el programa, después escribo el código y lo compruebo, y al final lo pruebo y corrijo lo que falle.",
                                               "Lo programo{1} con el modelo de IA en varios pasos: un plan, el código, la comprobación y las pruebas, corrigiendo lo que falle por el camino." };
            lcm_forma(pasos, sizeof pasos, LCM_UNA(f1), l, 0, 0);
        }
        lcm_cat(o, cap, pasos);
    } else if (tipo == LCM_AV_DOCUMENTO) {
        if (editar) { static const char *const f1[] = { "Voy a revisar el documento con el modelo de IA: lo leo, le pido las mejoras y te lo dejo maquetado de nuevo." }; lcm_cat(o, cap, LCM_UNA(f1)); }
        else {
            static const char *const f1[] = { "Voy a preparar el documento con el modelo de IA: primero redacta el contenido, después lo maqueto con su formato, lo reviso y al final lo exporto.",
                                               "Lo hago con el modelo de IA en cuatro pasos: redactar el contenido, maquetarlo, revisarlo y exportarlo." };
            lcm_cat(o, cap, LCM_UNA(f1));
        }
    } else {                                                                    /* con herramientas */
        if (tipo == LCM_AV_HERRAMIENTA) { static const char *const f0[] = { "Para responderte bien necesito usar herramientas.", "El modelo necesita algo más que pensar: va a usar herramientas." }; lcm_cat(o, cap, LCM_UNA(f0)); lcm_cat(o, cap, " "); }
        else { static const char *const f0[] = { "Para esto necesito al modelo de IA y algunas herramientas.", "Esto lo hago con el modelo de IA y las herramientas del sistema." }; lcm_cat(o, cap, LCM_UNA(f0)); lcm_cat(o, cap, " "); }
        if (lcm_tiene(f, web)) { static const char *const f1[] = { "Primero busco en internet, después leo lo que encuentre y al final te lo resumo.", "Voy a buscarlo en internet, leer las páginas que salgan y contarte lo importante." }; lcm_cat(o, cap, LCM_UNA(f1)); }
        else if (lcm_tiene(f, arch)) { static const char *const f1[] = { "Primero miro los archivos que hagan falta, después hago lo que pides y al final te cuento el resultado." }; lcm_cat(o, cap, LCM_UNA(f1)); }
        else { static const char *const f1[] = { "Primero decido qué herramientas hacen falta, después las uso y al final te preparo la respuesta.", "Va a elegir las herramientas, usarlas y prepararte la respuesta." }; lcm_cat(o, cap, LCM_UNA(f1)); }
    }
    char t[64]; int hay = lcm_aviso_tiempo(tipo, t, sizeof t);
    static const char *const pac[] = { "ten un poco de paciencia", "dame un momento", "te aviso en cuanto lo tenga", "enseguida te lo cuento" };
    lcm_cat(o, cap, " ");
    if (hay) { static const char *const f2[] = { "Suele tardar {1}, {2}.", "Normalmente tarda {1}; {2}.", "Lleva {1}, {2}." }; char x[200]; lcm_forma(x, sizeof x, LCM_UNA(f2), t, LCM_UNA(pac), 0); lcm_cat(o, cap, x); }
    else if (tipo == LCM_AV_DIRECTA) { static const char *const f2[] = { "Dame un momento.", "Un momento, por favor.", "Enseguida te lo digo." }; lcm_cat(o, cap, LCM_UNA(f2)); }
    else { static const char *const f2[] = { "Puede tardar un poco, {1}.", "Esto lleva su tiempo, {1}.", "Tardará un rato, {1}." }; char x[200]; lcm_forma(x, sizeof x, LCM_UNA(f2), LCM_UNA(pac), 0, 0); lcm_cat(o, cap, x); }
    if (o[0] >= 'a' && o[0] <= 'z') o[0] = (char)(o[0] - 32);
    return 1;
}

static const char *lcm_pieza_nombre(char s)
{
    switch (s) {
    case 'A': return "el archivo"; case 'B': return "el destino"; case 'D': return "la carpeta"; case 'N': return "el número";
    case 'R': return "el nombre de la red"; case 'K': return "la clave"; case 'H': return "el equipo o la IP";
    case 'U': return "la dirección web"; case 'T': return "el texto"; case 'P': return "la aplicación"; default: return "un dato";
    }
}

/* "si", "vale", "confirmo"... = 1; "no", "cancela"... = -1; "no era eso"... = -2 */
static int lcm_respuesta(const char *m)
{
    char f[96]; lcm_pliega(m, f, sizeof f);
    const char *g = lcm_sin_llamada(f);
    static const char *const si[] = { "si", "s", "vale", "confirmo", "hazlo", "adelante", "ok", "si por favor", "si hazlo", "claro", "por supuesto", "venga", "dale",
                                      "si claro", "si adelante", "afirmativo", "correcto", "eso es", "si gracias", "si si", "yes", 0 };
    static const char *const no[] = { "no", "n", "cancela", "cancelar", "dejalo", "no gracias", "mejor no", "para", "espera", "no no", "ni hablar", "olvidalo", "nada", 0 };
    static const char *const corr[] = { "no era eso", "eso no", "no es eso", "te has equivocado", "te equivocaste", "eso no es", "no queria eso", "error", "incorrecto",
                                        "has fallado", "no era eso lo que queria", "no lo que queria era otra cosa", "te has confundido", "me has entendido mal", "no me has entendido",
                                        "eso esta mal", "mal", "no esa no", "no eso no", 0 };
    if (lcm_es(g, si)) return 1;
    if (lcm_es(g, corr)) return -2;
    if (lcm_es(g, no)) return -1;
    return 0;
}

/* ------------------------------------------------------------------------------------------- ejecutar y responder */
static char lcm_cab[160];          /* saludo que precede a la respuesta ("Buenas tardes. ") */

static int  lcm_en_cadena;                     /* SOMA_258_LCM: "abre el navegador y luego pon el brillo al 50" */
static char lcm_cad_frase[1200], lcm_cad_razon[1600];

static void lcm_publica(const char *orden, const char *salida, const char *frase)
{
    uint32_t p = 0;
    if (lcm_en_cadena) {
        if (lcm_cad_frase[0] && frase && frase[0]) lcm_cat(lcm_cad_frase, sizeof lcm_cad_frase, " ");
        lcm_cat(lcm_cad_frase, sizeof lcm_cad_frase, frase);
        lcm_cat(lcm_cad_razon, sizeof lcm_cad_razon, orden); lcm_cat(lcm_cad_razon, sizeof lcm_cad_razon, "\n");
        return;
    }
    agent_last_response[0] = 0;
    if (lcm_voz) {
        fb_puts(agent_last_response, sizeof(agent_last_response), &p, "[Razonamiento]:\n[LCM] ");
        if (orden && orden[0]) { fb_puts(agent_last_response, sizeof(agent_last_response), &p, orden); fb_puts(agent_last_response, sizeof(agent_last_response), &p, "\n"); }
        if (salida && salida[0]) { char cut[1400]; lcm_cpy(cut, sizeof cut, salida); fb_puts(agent_last_response, sizeof(agent_last_response), &p, cut); }
        fb_puts(agent_last_response, sizeof(agent_last_response), &p, "\n\n[Dictamen]:\n");
        fb_puts(agent_last_response, sizeof(agent_last_response), &p, lcm_cab);
        fb_puts(agent_last_response, sizeof(agent_last_response), &p, frase);
    } else {
        fb_puts(agent_last_response, sizeof(agent_last_response), &p, "[LCM] -> ");
        fb_puts(agent_last_response, sizeof(agent_last_response), &p, orden ? orden : "");
        fb_puts(agent_last_response, sizeof(agent_last_response), &p, "\n");
        fb_puts(agent_last_response, sizeof(agent_last_response), &p, salida && salida[0] ? salida : frase);
    }
    mayordomo_store_reply(agent_last_response, 0);
}

static void lcm_di_detalle(const char *frase, const char *detalle)
{
    char f[600]; lcm_cpy(f, sizeof f, lcm_cab); lcm_cat(f, sizeof f, frase);
    agent_last_response[0] = 0; uint32_t p = 0;
    if (detalle && detalle[0] && lcm_voz) {
        fb_puts(agent_last_response, sizeof(agent_last_response), &p, "[Razonamiento]:\n[LCM] ");
        fb_puts(agent_last_response, sizeof(agent_last_response), &p, detalle);
        fb_puts(agent_last_response, sizeof(agent_last_response), &p, "\n\n[Dictamen]:\n");
    }
    fb_puts(agent_last_response, sizeof(agent_last_response), &p, f);
    mayordomo_store_reply(agent_last_response, 0);
    kprint("\n"); kprint(f); kprint("\n");
    if (detalle && detalle[0]) { kprint("      "); kprint(detalle); kprint("\n"); }
}

static void lcm_di(const char *frase)       /* solo una frase, sin orden */
{
    gui_fase("lcm");                                  /* SOMA_259_ORBE */
    char f[600]; lcm_cpy(f, sizeof f, lcm_cab); lcm_cat(f, sizeof f, frase);
    agent_last_response[0] = 0; uint32_t p = 0;
    fb_puts(agent_last_response, sizeof(agent_last_response), &p, f);
    mayordomo_store_reply(agent_last_response, 0);
    kprint("\n"); kprint(f); kprint("\n");
}

/* Corta la peticion en partes por "y luego", "y despues", ", luego"... (sin tildes o con ellas). 0 si no hay conectores. */
static int lcm_corta(const char *m, char partes[][256], int max)
{
    static const char *const con[] = { " y luego ", " y después ", " y despues ", ", luego ", ", después ", ", despues ", " y a continuación ", " y a continuacion ",
                                       " y además ", " y ademas ", " y también ", " y tambien ", " y encima ", " y por último ", " y por ultimo ", 0 };
    int n = 0; const char *s = m;
    while (*s && n < max) {
        const char *mejor = 0; uint32_t ml = 0;
        for (int i = 0; con[i]; ++i) {
            const char *q = s;
            for (; *q; ++q) { const char *a = q, *b = con[i]; while (*a && *b && mayordomo_lower(*a) == mayordomo_lower(*b)) { ++a; ++b; } if (!*b) break; }
            if (*q && (!mejor || q < mejor)) { mejor = q; ml = lcm_len(con[i]); }
        }
        uint32_t k = 0; const char *fin = mejor ? mejor : s + lcm_len(s);
        while (s < fin && k + 1 < 256) partes[n][k++] = *s++;
        partes[n][k] = 0; ++n;
        if (!mejor) break;
        s = mejor + ml;
    }
    return n > 1 ? n : 0;
}

static void lcm_ejecuta(const char *orden, const char *pregunta)
{
    static char salida[3072], frase[600];
    char nom[32]; const char *resto; lcm_partes(orden, nom, sizeof nom, &resto);
    int cl = lcm_clase(nom, resto);
    char real[560]; lcm_cpy(real, sizeof real, orden);
    if (lcm_voz && str_eq_ci(nom, "ram") && !*resto) lcm_cpy(real, sizeof real, "ram -s");     /* el resumen se lee mejor */
    int silencio = lcm_voz && (cl == LCM_C_LEE || cl == LCM_C_HACE || cl == LCM_C_ABRE || cl == LCM_C_AJUSTA) && !str_eq_ci(nom, "bateria") && !str_eq_ci(nom, "energia") && !str_eq_ci(nom, "ls") && !str_eq_ci(nom, "ps");
    kprint("\n[LCM] -> "); kprint(orden); kprint("\n");
    gui_fase("lcm");                                     /* SOMA_259_ORBE: el LCM ejecuta una orden (buscar si es de la red) */
    gui_fase_orden(orden);
    salida[0] = 0;
    int m = console_muted; if (silencio) console_muted = 1;
    int rc = dispatch_command(real, salida, sizeof salida);
    console_muted = m;
    frase[0] = 0;
    if (lcm_voz) lcm_frase(frase, sizeof frase, orden, salida, rc, pregunta ? pregunta : "");
    else lcm_cpy(frase, sizeof frase, rc ? "Hecho." : "No se pudo ejecutar.");
    lcm_publica(orden, salida, frase);
    if (lcm_voz && (frase[0] || lcm_cab[0])) { kprint(lcm_cab); kprint(frase); kprint("\n"); }
    lcm_cpy(lcm_ult_orden, sizeof lcm_ult_orden, orden);
    lcm_st[LCM_ST_ORDEN]++;
}

/* ¿Es una orden escrita tal cual? (orden de SOMA y como mucho un argumento) */
static int lcm_escrita(const char *m)
{
    char nom[32]; uint32_t n = 0, fl = 0; int palabras = 0, en = 0;
    while (*m == ' ' || *m == '\t') ++m;
    while (m[n] && m[n] != ' ' && m[n] != '\t' && n + 1 < sizeof nom) { nom[n] = mayordomo_lower(m[n]); ++n; }
    nom[n] = 0;
    for (const char *q = m; *q; ++q) { int esp = *q == ' ' || *q == '\t'; if (!esp && !en) ++palabras; en = !esp; }
    return n && palabras <= 2 && shell_command_info(nom, &fl, 0);
}

/* Orden decidida (por la red o por lo aprendido): confirmar o ejecutar */
static void lcm_actua(const char *orden, int confirmar, const char *frase)
{
    char nom[32]; const char *resto; lcm_partes(orden, nom, sizeof nom, &resto);
    if (confirmar || lcm_pide_confirmacion(nom, resto)) {
        char q[600]; lcm_cpy(lcm_pendiente, sizeof lcm_pendiente, orden); lcm_cpy(lcm_pend_frase, sizeof lcm_pend_frase, frase);
        if (lcm_voz) lcm_pregunta(q, sizeof q, orden);
        else { lcm_cpy(q, sizeof q, "[LCM] He entendido:  "); lcm_cat(q, sizeof q, orden); lcm_cat(q, sizeof q, "\n      ¿La ejecuto? Responde 'sí' o 'no'."); }
        kprint("\n[LCM] (pendiente) "); kprint(orden); kprint("\n");
        lcm_di(q);
        lcm_cpy(lcm_ult_orden, sizeof lcm_ult_orden, orden);
        return;
    }
    lcm_ejecuta(orden, frase);
}

/* 1: atendida. 0: no es para el LCM. 2: el LCM cree que no es una orden. */
static int lcm_atiende(const char *mission, int al_llm_cuenta)
{
    static struct lcm_res r;
    static char frase0[512];
    while (*mission == ' ' || *mission == '\t') ++mission;
    if (!*mission || !lcm_listo() || !lcm_del_usuario()) return 0;
    if (__sync_lock_test_and_set(&lcm_cerrojo, 1)) return 0;      /* Ajustes esta probando una frase: esta vez, el camino de siempre */
    int ret = 0;
    lcm_cab[0] = 0;
    lcm_siembra(mission);
    lcm_cpy(frase0, sizeof frase0, mission);
    const char *m = frase0;

    /* 1. respuesta a una orden pendiente o a una correccion */
    if (lcm_pendiente[0]) {
        int a = lcm_respuesta(m);
        char ord[512], fr[256]; lcm_cpy(ord, sizeof ord, lcm_pendiente); lcm_cpy(fr, sizeof fr, lcm_pend_frase);
        lcm_pendiente[0] = 0;
        if (a == 1) { lcm_st[LCM_ST_CONF]++; lcm_ejecuta(ord, fr); ret = 1; goto fin; }
        if (a == -1) { static const char *const f[] = { "De acuerdo, no hago nada.", "Vale, lo dejo.", "Cancelado.", "Entendido, no lo hago." }; lcm_st[LCM_ST_CANC]++; lcm_di(LCM_UNA(f)); ret = 1; goto fin; }
        if (a == -2) { lcm_st[LCM_ST_CANC]++; lcm_cpy(lcm_ult_frase, sizeof lcm_ult_frase, fr); goto pide_correccion; }
    }
    if (lcm_corrigiendo) {
        lcm_corrigiendo = 0;
        char nom[32]; uint32_t n = 0, fl = 0; const char *q = m;
        while (q[n] && q[n] != ' ' && n + 1 < sizeof nom) { nom[n] = mayordomo_lower(q[n]); ++n; }
        nom[n] = 0;
        if (n && shell_command_info(nom, &fl, 0) && lcm_ult_frase[0]) {
            char ord[512]; lcm_cpy(ord, sizeof ord, m); for (uint32_t k = 0; k < n; ++k) ord[k] = nom[k];
            lcm_aprende(lcm_ult_frase, ord);
            static const char *const f[] = { "Apuntado. La próxima vez lo haré así.", "Entendido, ya me lo sé.", "Vale, aprendido.", "Gracias, lo recordare." };
            char msg[300]; lcm_cpy(msg, sizeof msg, LCM_UNA(f)); lcm_cat(msg, sizeof msg, " ");
            lcm_cpy(lcm_cab, sizeof lcm_cab, msg);
            lcm_actua(ord, 0, lcm_ult_frase);
            lcm_ult_frase[0] = 0;
            ret = 1; goto fin;
        }
        if (lcm_respuesta(m) == -1) { lcm_di("Vale, lo dejamos."); ret = 1; goto fin; }
    }
    if (lcm_ult_frase[0] && lcm_respuesta(m) == -2) {
pide_correccion:
        {
            static const char *const f[] = { "Vaya, perdona. ¿Qué orden querías? Escríbemela tal cual y me la aprendo.",
                                              "Lo siento. Dime la orden exacta (por ejemplo: ls fotos) y la recordaré para la próxima vez.",
                                              "Perdón. ¿Cuál era la orden buena? Escríbela y la apunto." };
            lcm_corrigiendo = 1;
            lcm_di(LCM_UNA(f));
            ret = 1; goto fin;
        }
    }

    /* 2. charla */
    {
        char f[512]; lcm_pliega(m, f, sizeof f);
        const char *g = lcm_sin_llamada(f);
        char resp[600];
        if (lcm_charla(g, resp, sizeof resp, 0)) { lcm_st[LCM_ST_CHAR]++; lcm_di(resp); ret = 1; goto fin; }
        const char *tras = lcm_saludo_delante(g);
        if (tras) {
            while (*tras == ' ') ++tras;
            if (!*tras) { lcm_responde_saludo(resp, sizeof resp, g); lcm_st[LCM_ST_CHAR]++; lcm_di(resp); ret = 1; goto fin; }
            if (lcm_charla(tras, resp, sizeof resp, 1)) {                       /* "hola, que tal" */
                char sal[64]; lcm_cpy(sal, sizeof sal, lcm_saludo_de(g)); lcm_cat(sal, sizeof sal, ". ");
                lcm_cpy(lcm_cab, sizeof lcm_cab, sal); lcm_st[LCM_ST_CHAR]++; lcm_di(resp); ret = 1; goto fin;
            }
            /* saludo + peticion: se contesta la peticion (con el saludo delante si la atiende el LCM) */
            const char *orig = m; int palabras_saludo = 0;
            for (const char *z = g; z < tras; ++z) if (*z == ' ') ++palabras_saludo;
            for (int k = 0; k < palabras_saludo && *orig; ++k) { while (*orig && *orig != ' ') ++orig; while (*orig == ' ' || *orig == ',') ++orig; }
            if (*orig) { char sal[64]; lcm_cpy(sal, sizeof sal, lcm_saludo_de(g)); lcm_cat(sal, sizeof sal, ". "); lcm_cpy(lcm_cab, sizeof lcm_cab, sal); m = orig; }
        }
    }

    /* 3. orden escrita tal cual */
    if (lcm_escrita(m)) { ret = 0; goto fin; }

    /* 3b. varias ordenes seguidas: todas claras y sin confirmacion, o nada */
    {
        static char partes[4][256]; static char ords[4][512]; static struct lcm_res rp;
        int np = lcm_corta(m, partes, 4), ok = np > 0, ordenes = 0;
        for (int i = 0; i < np; ++i) {
            char nom[32]; const char *resto; uint32_t fl = 0; int es_orden = 1;
            if (lcm_escrita(partes[i])) { lcm_cpy(ords[i], sizeof ords[i], partes[i]); }
            else if (!lcm_aprendido_busca(partes[i], ords[i], sizeof ords[i])) {
                lcm_traduce(partes[i], &rp);
                if (rp.estado == LCM_FALTA || rp.estado == LCM_RARO) { ok = 0; ++ordenes; continue; }
                if (rp.estado != LCM_ORDEN) { ok = 0; es_orden = 0; continue; }
                if (rp.confirmar) ok = 0;
                lcm_cpy(ords[i], sizeof ords[i], rp.orden);
            }
            lcm_partes(ords[i], nom, sizeof nom, &resto);
            if (!shell_command_info(nom, &fl, 0)) { ok = 0; continue; }
            if (lcm_pide_confirmacion(nom, resto)) ok = 0;
            ordenes += es_orden;
        }
        if (!ok && ordenes >= 2) {          /* varias ordenes y alguna pide confirmacion o le falta un dato: de una en una, sin hacer solo una */
            static const char *const f[] = { "Me pides varias cosas y alguna necesita que la confirmes. Dímelas de una en una, por favor.",
                                              "Son varias órdenes, y para alguna necesito tu confirmación. Vamos de una en una: ¿cuál primero?",
                                              "Prefiero ir paso a paso: dime primero una cosa y luego la otra." };
            lcm_di(LCM_UNA(f)); ret = 1; goto fin;
        }
        if (ok) {
            lcm_en_cadena = 1; lcm_cad_frase[0] = 0; lcm_cad_razon[0] = 0;
            for (int i = 0; i < np; ++i) lcm_ejecuta(ords[i], partes[i]);
            lcm_en_cadena = 0;
            char r2[1600]; lcm_cpy(r2, sizeof r2, lcm_cad_razon);
            lcm_publica(r2, "", lcm_cad_frase[0] ? lcm_cad_frase : "Hecho.");
            lcm_cpy(lcm_ult_frase, sizeof lcm_ult_frase, m);
            ret = 1; goto fin;
        }
    }

    /* 4. lo aprendido */
    {
        char ord[512];
        if (lcm_aprendido_busca(m, ord, sizeof ord)) {
            char nom[32]; uint32_t fl = 0; const char *resto; lcm_partes(ord, nom, sizeof nom, &resto);
            if (shell_command_info(nom, &fl, 0)) { lcm_cpy(lcm_ult_frase, sizeof lcm_ult_frase, m); lcm_actua(ord, 0, m); ret = 1; goto fin; }
        }
    }

    /* 5. la red */
    lcm_traduce(m, &r);
    if (r.estado == LCM_LLM) { lcm_st[LCM_ST_LLM]++; ret = al_llm_cuenta ? 2 : 0; goto fin; }
    if (r.estado == LCM_DUDA) { lcm_st[LCM_ST_DUDA]++; ret = 0; goto fin; }
    {
        char nom[32]; uint32_t n = 0, flags = 0; const char *ayuda = 0;
        const char *o = r.estado == LCM_ORDEN ? r.orden : (const char *)lcm_m.it_ord[r.intent];
        uint32_t ol = r.estado == LCM_ORDEN ? 511 : lcm_m.it_ol[r.intent];
        while (n < ol && o[n] && o[n] != ' ' && n + 1 < sizeof nom) { nom[n] = o[n]; ++n; }
        nom[n] = 0;
        if (!shell_command_info(nom, &flags, &ayuda)) { ret = 0; goto fin; }     /* esta version de SOMA no tiene esa orden */
        lcm_cpy(lcm_ult_frase, sizeof lcm_ult_frase, m);
        if (r.estado == LCM_FALTA || r.estado == LCM_RARO) {
            char msg[400];
            if (lcm_voz) {
                if (r.estado == LCM_FALTA) { static const char *const f[] = { "Me falta {1}. ¿Me lo dices?", "¿Y {1}? Dímelo y lo hago.", "Necesito {1} para hacerlo.", "Casi: me falta {1}." }; lcm_forma(msg, sizeof msg, LCM_UNA(f), lcm_pieza_nombre(r.falta[0]), 0, 0); }
                else { static const char *const f[] = { "Ese nombre lleva signos o espacios que no puedo usar en una orden. ¿Me lo dices de otra forma?", "No me fío de ese nombre: tiene caracteres raros. Dímelo sin ellos." }; lcm_cpy(msg, sizeof msg, LCM_UNA(f)); }
                char det[300] = ""; if (ayuda) { lcm_cpy(det, sizeof det, "Uso: "); lcm_cat(det, sizeof det, ayuda); }
                lcm_di_detalle(msg, det);
                ret = 1; goto fin;
            } else {
                uint32_t p = 0;
                fb_puts(msg, sizeof msg, &p, "[LCM] Entiendo que quieres '"); fb_puts(msg, sizeof msg, &p, nom);
                fb_puts(msg, sizeof msg, &p, r.estado == LCM_FALTA ? "', pero me falta " : "', pero un dato trae caracteres que no uso en ordenes. ");
                if (r.estado == LCM_FALTA) { fb_puts(msg, sizeof msg, &p, lcm_pieza_nombre(r.falta[0])); fb_puts(msg, sizeof msg, &p, ". "); }
                if (ayuda) { fb_puts(msg, sizeof msg, &p, " (o escribe la orden: "); fb_puts(msg, sizeof msg, &p, ayuda); fb_puts(msg, sizeof msg, &p, ")"); }
            }
            lcm_di(msg);
            ret = 1; goto fin;
        }
    }
    lcm_actua(r.orden, r.confirmar, m);
    ret = 1;
fin:
    __sync_lock_release(&lcm_cerrojo);
    return ret;
}

static int lcm_soma(const char *mission)
{
    int v = lcm_atiende(mission, 1);
    if (v == 1) return LCM_HECHO;
    if (v == 2) return LCM_AL_LLM;
    /* duda: con el LCM encendido y una peticion del usuario, tampoco deciden las reglas del mayordomo (medido: con ellas se
     * ejecutan mas ordenes equivocadas); va al LLM. Una orden escrita tal cual sigue yendo al mayordomo. */
    if (lcm_listo() && lcm_del_usuario() && !lcm_escrita(mission)) return LCM_DUDA_LLM;
    return LCM_SIGUE;
}

static int lcm_mayordomo(const char *mission) { return lcm_atiende(mission, 0) == 1; }

int lcm_terminal(const char *linea);
int lcm_terminal(const char *linea)
{
    {   /* si empieza por una orden que existe (y ha fallado por otra razon), no es cosa del LCM */
        char nom[32]; uint32_t n = 0, fl = 0;
        while (linea[n] && linea[n] != ' ' && linea[n] != '\t' && n + 1 < sizeof nom) { nom[n] = linea[n]; ++n; }
        nom[n] = 0;
        if (n && shell_command_info(nom, &fl, 0) && !lcm_pendiente[0] && !lcm_corrigiendo) return 0;
    }
    int v = lcm_atiende(linea, 1);
    if (v == 2) kprint("(Si querias hablar con la IA, escribe: soma <tu peticion>)\n");
    return v == 1;
}

/* ------------------------------------------------------------------------------------------- para Ajustes > LCM (hilo de la GUI) */
int lcm_cfg_get(int que);
int lcm_cfg_get(int que)          /* 0 activo, 1 voz natural, 2 umbral (%), 3 modelo valido, 4 aprendidas, 5 ordenes del modelo, 6 letra del orbe (SOMA_259_ORBE) */
{                                 /* SOMA_266_AVISO: 7 avisos antes del modelo; 11..15 segundos que suele tardar cada tipo (0: aun no se sabe) */
    if (que == 6) return lcm_letra_v();                  /* SOMA_259_ORBE */
    (void)lcm_listo();
    if (que == 7) return lcm_aviso;
    if (que > 10 && que < 10 + LCM_AV_N) return lcm_av_n[que - 10] ? (int)((lcm_av_ms[que - 10] + 500) / 1000) : 0;
    switch (que) {
    case 0: return lcm_activo; case 1: return lcm_voz; case 2: return lcm_m.ok ? (int)(lcm_m.umbral_mil / 10) : 0;
    case 3: return lcm_m.ok; case 4: return lcm_apr_n; case 5: return lcm_m.ok ? (int)lcm_m.ni - 1 : 0;
    default: return 0;
    }
}
void lcm_cfg_set(int que, int v);
void lcm_cfg_set(int que, int v)
{
    (void)lcm_listo();
    (void)lcm_letra_v();                                 /* SOMA_259_ORBE: para guardarla tambien */
    if (que == 0) { lcm_activo = v ? 1 : 0; if (!v) { lcm_pendiente[0] = 0; lcm_corrigiendo = 0; } }
    else if (que == 6) { if (v >= 0 && v <= 2) lcm_letra = v; }   /* SOMA_259_ORBE */
    else if (que == 1) lcm_voz = v ? 1 : 0;
    else if (que == 7) lcm_aviso = v ? 1 : 0;                                      /* SOMA_266_AVISO */
    else if (que == 2 && v >= 50 && v <= 99) { lcm_umbral = v; if (lcm_m.ok) lcm_m.umbral_mil = (uint32_t)v * 10; }
    lcm_cfg_guarda();
}
void lcm_estadistica(uint32_t *v, int n);
void lcm_estadistica(uint32_t *v, int n) { for (int i = 0; i < n && i < 6; ++i) v[i] = lcm_st[i]; }
int lcm_aprendido_n(void);
int lcm_aprendido_n(void) { (void)lcm_listo(); return lcm_apr_n; }
int lcm_aprendido_linea(int i, char *o, uint32_t cap);
int lcm_aprendido_linea(int i, char *o, uint32_t cap)
{
    if (i < 0 || i >= lcm_apr_n) return 0;
    lcm_cpy(o, cap, lcm_apr_pat[i]); lcm_cat(o, cap, "  ->  "); lcm_cat(o, cap, lcm_apr_ord[i]);
    return 1;
}
void lcm_olvida(int i);
void lcm_olvida(int i)            /* -1 = todo */
{
    (void)lcm_listo();
    if (i < 0) lcm_apr_n = 0;
    else if (i < lcm_apr_n) { for (int k = i + 1; k < lcm_apr_n; ++k) { lcm_cpy(lcm_apr_pat[k - 1], 160, lcm_apr_pat[k]); lcm_cpy(lcm_apr_ord[k - 1], 160, lcm_apr_ord[k]); } --lcm_apr_n; }
    lcm_apr_guarda();
}
/* que entenderia el LCM, sin ejecutar nada */
int lcm_prueba(const char *frase, char *o, uint32_t cap);
int lcm_prueba(const char *frase, char *o, uint32_t cap)
{
    static struct lcm_res r;
    o[0] = 0;
    if (!lcm_listo() && !lcm_m.ok) { lcm_cpy(o, cap, "El modelo no es valido."); return 0; }
    if (__sync_lock_test_and_set(&lcm_cerrojo, 1)) { lcm_cpy(o, cap, "Un momento: el LCM esta atendiendo otra peticion."); return 0; }
    char f[300]; lcm_pliega(frase, f, sizeof f); const char *g = lcm_sin_llamada(f); char resp[400], ord[512];
    if (lcm_charla(g, resp, sizeof resp, 0) || (lcm_saludo_delante(g) && !*lcm_saludo_delante(g))) { lcm_cpy(o, cap, "Charla: contesta el LCM sin el LLM."); }
    else if (lcm_aprendido_busca(frase, ord, sizeof ord)) { lcm_cpy(o, cap, "Aprendido: "); lcm_cat(o, cap, ord); }
    else {
        lcm_traduce(frase, &r);
        static const char *const est[] = { "orden", "no es una orden: al LLM", "no estoy seguro: al LLM", "falta un dato", "dato con caracteres no permitidos" };
        lcm_cpy(o, cap, r.intent_nombre[0] ? r.intent_nombre : "-"); lcm_cat(o, cap, " ("); lcm_catn(o, cap, r.p_mil / 10); lcm_cat(o, cap, " %): ");
        lcm_cat(o, cap, est[r.estado >= 0 && r.estado <= 4 ? r.estado : 2]);
        if (r.estado == LCM_ORDEN) { lcm_cat(o, cap, "  ->  "); lcm_cat(o, cap, r.orden); char nom[32]; const char *resto; lcm_partes(r.orden, nom, sizeof nom, &resto); if (r.confirmar || lcm_pide_confirmacion(nom, resto)) lcm_cat(o, cap, "  (pediria confirmacion)"); }
        if (r.estado == LCM_FALTA) { lcm_cat(o, cap, ": "); lcm_cat(o, cap, lcm_pieza_nombre(r.falta[0])); }
    }
    __sync_lock_release(&lcm_cerrojo);
    return 1;
}

/* ------------------------------------------------------------------------------------------- la orden 'lcm' */
int lcm_cmd(const char *args, char *out, uint32_t max);
int lcm_cmd(const char *args, char *out, uint32_t max)
{
    static char msg[4096]; uint32_t p = 0; msg[0] = 0;
    while (*args == ' ') ++args;
    (void)lcm_listo();
    if (!*args || str_eq_ci(args, "estado")) {
        fb_puts(msg, sizeof msg, &p, "LCM (traductor de peticiones a ordenes, parches 253 y 257): ");
        fb_puts(msg, sizeof msg, &p, !lcm_m.ok ? "modelo NO valido" : lcm_activo ? "ENCENDIDO" : "APAGADO");
        if (lcm_m.ok) {
            fb_puts(msg, sizeof msg, &p, "\n  "); fb_put_dec(msg, sizeof msg, &p, lcm_m.ni - 1); fb_puts(msg, sizeof msg, &p, " ordenes, umbral ");
            fb_put_dec(msg, sizeof msg, &p, lcm_m.umbral_mil / 10); fb_puts(msg, sizeof msg, &p, " %, respuestas ");
            fb_puts(msg, sizeof msg, &p, lcm_voz ? "naturales (para la voz)" : "tecnicas"); fb_puts(msg, sizeof msg, &p, ", aprendidas ");
            fb_put_dec(msg, sizeof msg, &p, (uint64_t)lcm_apr_n); fb_puts(msg, sizeof msg, &p, ", modelo de ");
            fb_put_dec(msg, sizeof msg, &p, (uint64_t)sizeof(lcm_ordenes_blob) / 1024); fb_puts(msg, sizeof msg, &p, " KiB");
            fb_puts(msg, sizeof msg, &p, "\n  Desde el arranque: "); fb_put_dec(msg, sizeof msg, &p, lcm_st[0]); fb_puts(msg, sizeof msg, &p, " ordenes, ");
            fb_put_dec(msg, sizeof msg, &p, lcm_st[5]); fb_puts(msg, sizeof msg, &p, " de charla, "); fb_put_dec(msg, sizeof msg, &p, lcm_st[1] + lcm_st[2]);
            fb_puts(msg, sizeof msg, &p, " al LLM, "); fb_put_dec(msg, sizeof msg, &p, lcm_st[3]); fb_puts(msg, sizeof msg, &p, " confirmadas, ");
            fb_put_dec(msg, sizeof msg, &p, lcm_st[4]); fb_puts(msg, sizeof msg, &p, " canceladas");
        }
        fb_puts(msg, sizeof msg, &p, "\nUso: lcm <frase> | encender | apagar | voz natural|tecnica | umbral 60-95 | aprendido | aprende <frase> => <orden> | olvida N|todo\n");
    } else if (str_eq_ci(args, "apagar") || str_eq_ci(args, "off")) {
        lcm_cfg_set(0, 0); fb_puts(msg, sizeof msg, &p, "LCM apagado: las peticiones van por el camino de antes (mayordomo y LLM).\n");
    } else if (str_eq_ci(args, "encender") || str_eq_ci(args, "on")) {
        lcm_cfg_set(0, 1); fb_puts(msg, sizeof msg, &p, lcm_m.ok ? "LCM encendido.\n" : "El modelo no es valido: no se puede encender.\n");
    } else if (lcm_empieza(args, "voz")) {
        lcm_cfg_set(1, lcm_busca(args, "tec") ? 0 : 1);
        fb_puts(msg, sizeof msg, &p, lcm_voz ? "Respuestas naturales, pensadas para la voz.\n" : "Respuestas tecnicas (la orden y su salida).\n");
    } else if (lcm_empieza(args, "umbral")) {
        uint32_t v = 0; lcm_num(args, &v, 0);
        if (v >= 50 && v <= 99) { lcm_cfg_set(2, (int)v); fb_puts(msg, sizeof msg, &p, "Umbral: "); fb_put_dec(msg, sizeof msg, &p, v); fb_puts(msg, sizeof msg, &p, " % (mas alto = actua menos, pero se equivoca menos).\n"); }
        else fb_puts(msg, sizeof msg, &p, "Uso: lcm umbral 50-99   (de fabrica: 70)\n");
    } else if (str_eq_ci(args, "aprendido")) {
        if (!lcm_apr_n) fb_puts(msg, sizeof msg, &p, "No hay nada aprendido. Cuando me equivoque, dime 'no era eso' y escribeme la orden buena.\n");
        for (int i = 0; i < lcm_apr_n; ++i) { char l[340]; fb_put_dec(msg, sizeof msg, &p, (uint64_t)(i + 1)); fb_puts(msg, sizeof msg, &p, ". "); lcm_aprendido_linea(i, l, sizeof l); fb_puts(msg, sizeof msg, &p, l); fb_puts(msg, sizeof msg, &p, "\n"); }
    } else if (lcm_empieza(args, "aprende ")) {
        const char *s = args + 8; const char *f = lcm_busca(s, "=>");
        if (!f) fb_puts(msg, sizeof msg, &p, "Uso: lcm aprende <frase> => <orden>   p. ej. lcm aprende mira si llego a google.com => ping google.com\n");
        else {
            char fr[256]; uint32_t k = 0; while (s < f && k + 1 < sizeof fr) fr[k++] = *s++; fr[k] = 0;
            const char *o = f + 2; while (*o == ' ') ++o;
            char nom[32]; const char *resto; uint32_t fl = 0; lcm_partes(o, nom, sizeof nom, &resto);
            if (!shell_command_info(nom, &fl, 0)) { fb_puts(msg, sizeof msg, &p, "Esa orden no existe en SOMA: "); fb_puts(msg, sizeof msg, &p, nom); fb_puts(msg, sizeof msg, &p, "\n"); }
            else { lcm_aprende(fr, o); fb_puts(msg, sizeof msg, &p, "Aprendido. Lo que se parezca con otros datos tambien vale (p. ej. otro archivo u otra IP).\n"); }
        }
    } else if (lcm_empieza(args, "olvida")) {
        if (lcm_busca(args, "todo")) { lcm_olvida(-1); fb_puts(msg, sizeof msg, &p, "Olvidado todo lo aprendido.\n"); }
        else { uint32_t v = 0; if (lcm_num(args, &v, 0) && v >= 1 && (int)v <= lcm_apr_n) { lcm_olvida((int)v - 1); fb_puts(msg, sizeof msg, &p, "Olvidado.\n"); } else fb_puts(msg, sizeof msg, &p, "Uso: lcm olvida N | lcm olvida todo   (lcm aprendido los numera)\n"); }
    } else {
        char o[600]; lcm_prueba(args, o, sizeof o);
        fb_puts(msg, sizeof msg, &p, "entiendo: "); fb_puts(msg, sizeof msg, &p, o); fb_puts(msg, sizeof msg, &p, "\n(no se ha ejecutado nada)\n");
    }
    kprint(msg);
    if (out && max) { uint32_t q = 0; out[0] = 0; fb_puts(out, max, &q, msg); }
    return 1;
}
/* SOMA_257_LCM fin */
