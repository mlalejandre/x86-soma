/* SOMA_266_AVISO: el aviso inmediato del LCM antes del modelo de lenguaje (lcm_soma.inc con las imitaciones del banco del 257). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "lcm_ordenes.inc"

static char agent_last_response[4096];
static int delegation_depth = 0, modo_agente = 0;
volatile int console_muted = 0;
int shell_agent_mode(void) { return modo_agente; }
static char ejecutadas[16][512]; static int nej = 0;
static char pantalla[32768];
static void kprint(const char *s) { if (!console_muted) strncat(pantalla, s, sizeof pantalla - strlen(pantalla) - 1); }
static void fb_puts(char *out, uint32_t max, uint32_t *pos, const char *s) { while (*s && *pos + 1 < max) out[(*pos)++] = *s++; out[*pos < max ? *pos : max - 1] = 0; }
static void fb_put_dec(char *out, uint32_t max, uint32_t *pos, uint64_t v) { char b[24]; snprintf(b, sizeof b, "%llu", (unsigned long long)v); fb_puts(out, max, pos, b); }
static char mayordomo_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int mayordomo_has(const char *t, const char *n) { return strstr(t, n) != 0; }
static int str_eq_ci(const char *a, const char *b) { while (*a && *b) { if (mayordomo_lower(*a) != mayordomo_lower(*b)) return 0; ++a; ++b; } return *a == *b; }
static void mayordomo_store_reply(const char *r, int pr) { if (r != agent_last_response) snprintf(agent_last_response, sizeof agent_last_response, "%s", r); if (pr) { kprint(r); kprint("\n"); } }
static char archivos[4][4096];   /* /etc/lcm.conf, /etc/lcm_aprendido.txt */
static int fidx(const char *p) { return strstr(p, "aprendido") ? 1 : 0; }
static int vfs_read(const char *p, char *b, uint32_t n) { uint32_t l = (uint32_t)strlen(archivos[fidx(p)]); if (!l) return -1; if (l > n) l = n; memcpy(b, archivos[fidx(p)], l); return (int)l; }
static int vfs_write(const char *p, const char *b, uint32_t n) { if (n >= sizeof archivos[0]) n = sizeof archivos[0] - 1; memcpy(archivos[fidx(p)], b, n); archivos[fidx(p)][n] = 0; return (int)n; }
static const char *fecha = "CEST: 2026-11-22 05:45:10 CEST";
int dispatch_command(const char *c, char *out, uint32_t max)
{
    if (!strcmp(c, "date")) { snprintf(out, max, "%s", fecha); return 1; }
    snprintf(ejecutadas[nej++ & 15], 512, "%s", c);
    if (!strcmp(c, "ram -s")) snprintf(out, max, "RAM: 612.40 MiB / 7894.00 MiB (7.7%%) | libre 7281.60 MiB | heap libre 20480 KiB\n");
    else if (!strcmp(c, "df")) snprintf(out, max, "Disco: SOMADATA (pendrive) | RamFS V2: 12/64 inodos | 48 KiB usados (16 KiB/archivo) | dirty=0");
    else if (!strcmp(c, "uptime")) snprintf(out, max, "Uptime: 7385 segundos");
    else if (!strncmp(c, "ls", 2)) snprintf(out, max, "/mis_archivos/notas.txt   120 B\n/mis_archivos/fotos/\n/workspace/informe.pdf  3 KiB\n/workspace/datos.csv  1 KiB\n/workspace/a.txt\n/workspace/b.txt\n/workspace/c.txt\n");
    else if (!strncmp(c, "find", 4)) snprintf(out, max, "/mis_archivos/presupuesto_2026.xlsx\n/workspace/presupuesto.txt\n2 coincidencia(s)\n");
    else if (!strncmp(c, "wc", 2)) snprintf(out, max, "15 lineas, 120 palabras, 800 bytes");
    else if (!strncmp(c, "ping", 4)) snprintf(out, max, strstr(c, "9.9.9.9") ? "Ping fallido" : "Ping exitoso");
    else if (!strcmp(c, "whoami")) snprintf(out, max, "marcos\n");
    else if (!strcmp(c, "status")) snprintf(out, max, "ARCH: x86_64 | Red: wlan0 192.168.1.37/24 gw 192.168.1.1 | Disco: SOMADATA");
    else if (!strncmp(c, "cat", 3)) snprintf(out, max, "rm: no encontrado");
    else snprintf(out, max, "(salida de %s)", c);
    if (!strncmp(c, "cat no", 6)) return 1;
    return 1;
}
static const char *const existen[] = { "ls", "cat", "rm", "rmdir", "kill", "ram", "df", "ps", "write", "cp", "mv", "reboot", "poweroff", "find", "search", "brillo", "wifi", "mkdir", "habla",
                                       "youtube", "date", "teclado", "head", "uptime", "ping", "wc", "whoami", "status", "navegador", "bateria", "energia", "touch", "audio", "paint", "monitor", 0 };
int shell_command_info(const char *n, uint32_t *f, const char **h) { for (int i = 0; existen[i]; ++i) if (!strcmp(n, existen[i])) { if (f) *f = 2; if (h) *h = "uso de prueba"; return 1; } return 0; }
int lcm_voz_bateria(char *o, uint32_t cap) { snprintf(o, cap, "Te queda un 76 por ciento, unas 3 horas y 10 minutos."); return 1; }
int lcm_voz_energia(char *o, uint32_t cap) { snprintf(o, cap, "El procesador está a 47 grados, en modo equilibrado."); return 1; }

#include "lcm_soma.inc"

static int fallos = 0, n = 0;
#define CK(c, ...) do { ++n; if (!(c)) { ++fallos; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)
static char t[900];
static int aviso(int tipo, const char *m, const char *len, int ed) { t[0] = 0; return lcm_aviso_texto(tipo, m, len, ed, t, sizeof t); }
static int limpio(const char *s)          /* frase acabada, sin huecos de plantilla, mayuscula al principio, corta para la voz */
{
    size_t l = strlen(s);
    return l > 20 && l < 520 && !strchr(s, '{') && !strchr(s, '}') && s[l - 1] == '.' && ((s[0] >= 'A' && s[0] <= 'Z') || (unsigned char)s[0] >= 0xC3) && !strstr(s, "  ") && !strstr(s, " ,") && !strstr(s, "..");
}
static void limpia(void) { pantalla[0] = 0; nej = 0; memset(ejecutadas, 0, sizeof ejecutadas); agent_last_response[0] = 0; }
static void reinicia(void) { lcm_cargado = 0; lcm_aviso = 1; memset(lcm_av_ms, 0, sizeof lcm_av_ms); memset(lcm_av_n, 0, sizeof lcm_av_n); lcm_letra = -1; }

int main(int argc, char **argv)
{
    (void)argv;
    /* 1. cada tipo dice lo que va a hacer y pide paciencia */
    CK(aviso(LCM_AV_DIRECTA, "explícame qué es un agujero negro", 0, 0) && limpio(t) && strstr(t, "modelo de IA"), "directa: %s", t);
    CK(strstr(t, "momento") || strstr(t, "Enseguida"), "directa: pide un momento: %s", t);
    CK(aviso(LCM_AV_PROGRAMA, "hazme un juego de la serpiente en C", "C", 0) && limpio(t) && strstr(t, " en C ") && (strstr(t, "planifico") || strstr(t, "plan")) && (strstr(t, "pruebo") || strstr(t, "pruebas")), "programa: %s", t);
    CK(strstr(t, "paciencia") || strstr(t, "momento") || strstr(t, "aviso") || strstr(t, "cuento") || strstr(t, "rato") || strstr(t, "tiempo"), "programa: paciencia: %s", t);
    CK(aviso(LCM_AV_PROGRAMA, "corrige el programa reloj.py", "Python", 1) && limpio(t) && strstr(t, " en Python") && (strstr(t, "cambi") || strstr(t, "arreglo")), "editar programa: %s", t);
    CK(aviso(LCM_AV_DOCUMENTO, "hazme un informe sobre el reciclaje", 0, 0) && limpio(t) && strstr(t, "redact") && strstr(t, "maquet") && strstr(t, "export"), "documento: %s", t);
    CK(aviso(LCM_AV_AGENTE, "busca las noticias de hoy sobre la ESA", 0, 0) && limpio(t) && strstr(t, "internet"), "con herramientas, en la web: %s", t);
    CK(aviso(LCM_AV_AGENTE, "ordena los archivos de la carpeta fotos", 0, 0) && limpio(t) && strstr(t, "archivos"), "con herramientas, archivos: %s", t);
    CK(aviso(LCM_AV_AGENTE, "calcula cuánto ocupa todo y dime si cabe un juego", 0, 0) && limpio(t) && strstr(t, "herramientas"), "con herramientas, en general: %s", t);
    CK(aviso(LCM_AV_HERRAMIENTA, "qué tiempo hace en Madrid", 0, 0) && limpio(t) && strstr(t, "herramientas") && strstr(t, "internet"), "la directa pide herramientas: %s", t);
    CK(aviso(LCM_AV_DIRECTA, "buenas tardes, explícame la fotosíntesis", 0, 0) && !strncmp(t, "Buenas tardes. ", 15) && limpio(t), "con el saludo delante: %s", t);
    CK(aviso(LCM_AV_DIRECTA, "explícame la fotosíntesis", 0, 0) && strncmp(t, "Buen", 4) != 0, "sin saludo no se saluda: %s", t);
    /* 2. variedad (como las demas frases del LCM) */
    { char vistas[8][900]; int d = 0;
      for (int i = 0; i < 8; ++i) { aviso(LCM_AV_DIRECTA, "explícame el big bang", 0, 0); int nueva = 1; for (int k = 0; k < d; ++k) if (!strcmp(vistas[k], t)) nueva = 0; if (nueva) snprintf(vistas[d++], 900, "%s", t); }
      CK(d >= 3, "variedad: %d formulas distintas en 8", d); }
    /* 3. el tiempo: sin medidas no se inventa; despues, lo que tardo de verdad */
    reinicia(); archivos[0][0] = 0;
    CK(aviso(LCM_AV_PROGRAMA, "haz un reloj", "SOMA-LANG", 0) && !strstr(t, "Suele") && !strstr(t, "segundos") && !strstr(t, "minuto"), "sin medidas, sin tiempo: %s", t);
    lcm_aviso_mide(LCM_AV_PROGRAMA, 125000);
    CK(aviso(LCM_AV_PROGRAMA, "haz un reloj", "SOMA-LANG", 0) && strstr(t, "unos 2 minutos"), "dos minutos: %s", t);
    lcm_aviso_mide(LCM_AV_DIRECTA, 14200);
    CK(aviso(LCM_AV_DIRECTA, "qué es un quark", 0, 0) && strstr(t, "unos 15 segundos") && limpio(t), "quince segundos: %s", t);
    lcm_aviso_mide(LCM_AV_DIRECTA, 2000); lcm_aviso_mide(LCM_AV_DIRECTA, 2000); lcm_aviso_mide(LCM_AV_DIRECTA, 2000); lcm_aviso_mide(LCM_AV_DIRECTA, 2000);
    CK(lcm_cfg_get(11) < 8 && aviso(LCM_AV_DIRECTA, "qué es un quark", 0, 0) && strstr(t, "solo unos segundos"), "pesan mas las ultimas (%d s): %s", lcm_cfg_get(11), t);
    lcm_aviso_mide(LCM_AV_DOCUMENTO, 70000);
    CK(aviso(LCM_AV_DOCUMENTO, "una carta de presentación", 0, 0) && strstr(t, "alrededor de un minuto"), "un minuto: %s", t);
    lcm_aviso_mide(LCM_AV_AGENTE, 0); lcm_aviso_mide(LCM_AV_AGENTE, 9999999999ULL);
    CK(lcm_av_n[LCM_AV_AGENTE] == 0, "medidas absurdas fuera");
    CK(strstr(archivos[0], "t3=125000/1\n") && strstr(archivos[0], "t4=70000/1\n") && strstr(archivos[0], "aviso=1\n") && strstr(archivos[0], "activo=1"), "se guarda en /etc/lcm.conf: [%s]", archivos[0]);
    CK(strlen(archivos[0]) < 300, "cabe en el archivo de siempre (%zu bytes)", strlen(archivos[0]));
    { uint32_t ms = lcm_av_ms[LCM_AV_DIRECTA], cu = lcm_av_n[LCM_AV_DIRECTA];
      lcm_cargado = 0; memset(lcm_av_ms, 0, sizeof lcm_av_ms); memset(lcm_av_n, 0, sizeof lcm_av_n); (void)lcm_listo();
      CK(lcm_av_ms[LCM_AV_DIRECTA] == ms && lcm_av_n[LCM_AV_DIRECTA] == cu && lcm_av_ms[LCM_AV_PROGRAMA] == 125000 && lcm_cfg_get(13) == 125, "se recupera al arrancar (%u/%u)", lcm_av_ms[LCM_AV_DIRECTA], lcm_av_n[LCM_AV_DIRECTA]); }
    /* 4. cuando no se avisa */
    lcm_cfg_set(7, 0); CK(!aviso(LCM_AV_DIRECTA, "qué es un quark", 0, 0) && !t[0] && strstr(archivos[0], "aviso=0"), "avisos apagados (Ajustes)");
    lcm_cargado = 0; (void)lcm_listo(); CK(lcm_cfg_get(7) == 0, "apagados tras reiniciar");
    lcm_cfg_set(7, 1); CK(lcm_cfg_get(7) == 1, "encendidos otra vez");
    lcm_cfg_set(0, 0); CK(!aviso(LCM_AV_DIRECTA, "qué es un quark", 0, 0), "con el LCM apagado, todo como antes (sin aviso)"); lcm_cfg_set(0, 1);
    delegation_depth = 1; CK(!aviso(LCM_AV_AGENTE, "busca gatos", 0, 0), "lo que pide otro agente no se avisa"); delegation_depth = 0;
    modo_agente = 1; CK(!aviso(LCM_AV_AGENTE, "busca gatos", 0, 0), "en modo agente tampoco"); modo_agente = 0;
    CK(!aviso(0, "x", 0, 0) && !aviso(LCM_AV_N, "x", 0, 0) && !aviso(LCM_AV_DIRECTA, 0, 0, 0), "tipos y argumentos no validos");
    /* 5. compatibilidad: un /etc/lcm.conf de antes (sin aviso ni tiempos) */
    snprintf(archivos[0], sizeof archivos[0], "activo=1\nvoz=1\numbral=70\nletra=1\n"); reinicia(); (void)lcm_listo();
    CK(lcm_aviso == 1 && lcm_av_n[1] == 0 && lcm_letra_v() == 1 && lcm_voz == 1, "conf antigua: avisos encendidos y lo demas igual");
    snprintf(archivos[0], sizeof archivos[0], "activo=1\nvoz=1\numbral=70\naviso=1\nt1=abc\nt2=5000/\nt5=3000/2\n"); reinicia(); (void)lcm_listo();
    CK(lcm_av_n[1] == 0 && lcm_av_n[2] == 0 && lcm_av_ms[5] == 3000 && lcm_av_n[5] == 2, "valores rotos se ignoran");
    /* 6. lo que habia sigue igual: el LCM atiende sus ordenes sin avisar de nada */
    limpia(); CK(lcm_soma("qué hora es") == LCM_HECHO && !strstr(agent_last_response, "modelo de IA"), "una orden del LCM no lleva aviso");
    limpia(); CK(lcm_soma("explícame qué es la memoria ram") == LCM_AL_LLM, "lo del LLM sigue yendo al LLM");
    if (argc > 1) {
        reinicia(); archivos[0][0] = 0; lcm_aviso_mide(LCM_AV_PROGRAMA, 95000); lcm_aviso_mide(LCM_AV_DIRECTA, 11000);
        const struct { int tipo; const char *m, *l; int ed; } ej[] = { { 1, "explícame qué es un agujero negro", 0, 0 }, { 1, "hola, escríbeme un poema sobre el mar", 0, 0 },
            { 3, "hazme un juego de la serpiente en C", "C", 0 }, { 3, "corrige reloj.py", "Python", 1 }, { 4, "hazme un informe sobre el reciclaje", 0, 0 },
            { 2, "busca las noticias de hoy", 0, 0 }, { 2, "ordena mis archivos", 0, 0 }, { 5, "qué tiempo hace en Madrid", 0, 0 } };
        for (unsigned i = 0; i < sizeof ej / sizeof ej[0]; ++i) { aviso(ej[i].tipo, ej[i].m, ej[i].l, ej[i].ed); printf("  %-38s -> %s\n", ej[i].m, t); }
    }
    printf("%s aviso inmediato del LCM: %d comprobaciones, %d fallos\n", fallos ? "FALLO" : "ok ", n, fallos);
    return fallos != 0;
}
