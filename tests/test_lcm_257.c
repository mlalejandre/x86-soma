/* SOMA_257_LCM: banco del parche 257 (cc -O1 -Isrc/agent tests/test_lcm_257.c): lcm_soma.inc con imitaciones de las funciones de SOMA. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
/* SOMA_271_LCM: paquete de modelo en DATOS (imitacion) */
static char *paq_ptr = 0; static uint32_t paq_len = 0;
static int vfs_size(const char *p) { return (p && !strcmp(p, "/lcm/modelo.lcm") && paq_ptr) ? (int)paq_len : -1; }
static char *vfs_read_alloc(const char *p, uint32_t *l) { if (vfs_size(p) < 0) return 0; char *b = (char *)malloc(paq_len + 1); memcpy(b, paq_ptr, paq_len); b[paq_len] = 0; *l = paq_len; return b; }
static void kfree(void *p) { free(p); }
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
#define CK(c, ...) do { ++n; if (!(c)) { ++fallos; printf("FALLO: " __VA_ARGS__); printf("\n   respuesta: %s\n", agent_last_response); } } while (0)
static void limpia(void) { pantalla[0] = 0; nej = 0; memset(ejecutadas, 0, sizeof ejecutadas); agent_last_response[0] = 0; }
static const char *dictamen(void) { const char *d = strstr(agent_last_response, "[Dictamen]:\n"); return d ? d + 12 : agent_last_response; }
static void ver(const char *peticion) { limpia(); int v = lcm_soma(peticion); printf("  %-46s -> [%s] %s\n", peticion, v == LCM_HECHO ? "LCM" : v == LCM_AL_LLM || v == LCM_DUDA_LLM ? "LLM" : "sigue", v == LCM_HECHO ? dictamen() : ""); }

static int main2(int argc);
int main(int argc, char **argv) { (void)argv; return main2(argc); }
static int main2(int argc)

{
    int muestra = argc > 1;
    limpia();
    CK(lcm_soma("qué hora es") == LCM_HECHO && strstr(dictamen(), "las 5 menos cuarto de la madrugada"), "hora (UTC+1 en noviembre): %s", dictamen());
    fecha = "CEST: 2026-07-14 15:30:00 CEST"; limpia();
    CK(lcm_soma("dime la hora") == LCM_HECHO && strstr(dictamen(), "las 3 y media de la tarde"), "hora en verano (UTC+2): %s", dictamen());
    fecha = "CEST: 2026-11-22 05:45:10 CEST"; limpia();
    CK(lcm_soma("qué día es hoy") == LCM_HECHO && strstr(dictamen(), "domingo, 22 de noviembre de 2026"), "día: %s", dictamen());
    limpia(); CK(lcm_soma("cuánta ram queda") == LCM_HECHO && strstr(dictamen(), "612 megas") && strstr(dictamen(), "7 coma 7 gigas") && !strstr(agent_last_response, "MiB\n\n[Dictamen]") , "ram: %s", dictamen());
    CK(strstr(agent_last_response, "[Razonamiento]:") && strstr(agent_last_response, "RAM: 612.40 MiB"), "la salida va en el razonamiento");
    CK(!strstr(pantalla, "RAM: 612.40"), "consola silenciada al leer la salida");
    limpia(); CK(lcm_soma("hola") == LCM_HECHO && nej == 0 && strstr(dictamen(), "Buenas noches"), "saludo según la hora: %s", dictamen());
    limpia(); CK(lcm_soma("buenos días") == LCM_HECHO && strstr(dictamen(), "uenos días"), "el mismo saludo: %s", dictamen());
    limpia(); CK(lcm_soma("hola, qué procesos hay") == LCM_HECHO && nej == 1 && !strcmp(ejecutadas[0], "ps") && strstr(dictamen(), "Buenas noches. "), "saludo + petición: %s", dictamen());
    limpia(); CK(lcm_soma("¿cómo estás?") == LCM_HECHO && nej == 0, "cómo estás");
    limpia(); CK(lcm_soma("muchas gracias") == LCM_HECHO && nej == 0, "gracias");
    limpia(); CK(lcm_soma("explícame qué es la memoria ram") == LCM_AL_LLM, "pregunta general al LLM");
    limpia(); CK(lcm_soma("hola, explícame qué es un agujero negro") != LCM_HECHO, "saludo + pregunta general: al LLM");
    /* variedad: diez veces lo mismo, al menos 4 formulas distintas */
    { char vistas[10][300]; int distintas = 0;
      for (int i = 0; i < 10; ++i) { limpia(); lcm_soma("hola"); int nueva = 1; for (int k = 0; k < distintas; ++k) if (!strcmp(vistas[k], dictamen())) nueva = 0; if (nueva && distintas < 10) snprintf(vistas[distintas++], 300, "%s", dictamen()); }
      CK(distintas >= 4, "variedad de saludos: %d", distintas); }
    { char ant[300] = ""; int repet = 0; for (int i = 0; i < 12; ++i) { limpia(); lcm_soma("qué hora es"); if (!strcmp(ant, dictamen())) ++repet; snprintf(ant, sizeof ant, "%s", dictamen()); } CK(repet == 0, "nunca repite la formula anterior (%d)", repet); }
    /* confirmación y corrección */
    limpia(); CK(lcm_soma("borra el archivo notas.txt") == LCM_HECHO && nej == 0 && lcm_pendiente[0], "rm pendiente: %s", dictamen());
    CK(lcm_soma("sí, adelante") == LCM_HECHO && nej == 1 && !strcmp(ejecutadas[0], "rm notas.txt"), "confirmado");
    limpia(); lcm_soma("crea una carpeta llamada viajes"); CK(nej == 0 && lcm_pendiente[0], "mkdir pide confirmación ahora");
    CK(lcm_soma("no") == LCM_HECHO && nej == 0, "cancelado");
    limpia(); lcm_soma("mira si llego a servidor.local");
    { int v = lcm_soma("no era eso"); CK(v == LCM_HECHO && lcm_corrigiendo, "pide la orden buena: %s", dictamen()); }
    limpia(); CK(lcm_soma("ping servidor.local") == LCM_HECHO && nej == 1 && !strcmp(ejecutadas[0], "ping servidor.local") && lcm_apr_n == 1, "aprende y ejecuta: %s / %s", lcm_apr_n ? lcm_apr_pat[0] : "-", lcm_apr_n ? lcm_apr_ord[0] : "-");
    limpia(); CK(lcm_soma("mira si llego a google.com") == LCM_HECHO && nej == 1 && !strcmp(ejecutadas[0], "ping google.com"), "lo aprendido vale con otro dato: %s", ejecutadas[0]);
    CK(strstr(archivos[1], "si llego a {1}\tping {1}") != 0, "guardado en /etc/lcm_aprendido.txt: [%s]", archivos[1]);
    limpia(); lcm_cmd("aprende mándale un ping a la puerta => ping 192.168.1.1", 0, 0); CK(lcm_apr_n == 2, "lcm aprende");
    limpia(); CK(lcm_soma("mándale un ping a la puerta") == LCM_HECHO && !strcmp(ejecutadas[0], "ping 192.168.1.1"), "aprendido sin huecos");
    limpia(); lcm_cmd("aprende quita lo de a.txt => rm a.txt", 0, 0); limpia(); lcm_soma("quita lo de b.txt"); CK(nej == 0 && !strcmp(lcm_pendiente, "rm b.txt"), "aprendida peligrosa: pide confirmación (%s)", lcm_pendiente);
    lcm_soma("no");
    lcm_cmd("olvida todo", 0, 0); CK(lcm_apr_n == 0 && !archivos[1][0], "olvida todo");
    /* seguridad de siempre */
    limpia(); modo_agente = 1; CK(lcm_soma("hola") == LCM_SIGUE && nej == 0, "en modo agente no actúa"); modo_agente = 0;
    limpia(); CK(lcm_soma("teclado us") == LCM_SIGUE, "orden escrita tal cual");
    limpia(); CK(lcm_terminal("dime cuánto disco queda") == 1 && strstr(dictamen(), "12 archivos"), "terminal: %s", dictamen());
    /* configuración */
    lcm_cfg_set(1, 0); limpia(); lcm_soma("qué procesos hay"); CK(strstr(agent_last_response, "[LCM] -> ps") && !strstr(agent_last_response, "[Dictamen]"), "voz técnica");
    lcm_cfg_set(1, 1); CK(strstr(archivos[0], "voz=1") != 0, "guardado en /etc/lcm.conf: %s", archivos[0]);
    lcm_cfg_set(2, 85); CK(lcm_m.umbral_mil == 850 && lcm_cfg_get(2) == 85, "umbral");
    lcm_cfg_set(2, 70);
    { char o[300]; lcm_prueba("borra el archivo notas.txt", o, sizeof o); CK(strstr(o, "rm notas.txt") && strstr(o, "confirmacion"), "probar frase: %s", o); }
    lcm_cfg_set(0, 0); limpia(); CK(lcm_soma("hola") == LCM_SIGUE, "apagado"); lcm_cfg_set(0, 1);
    limpia(); CK(lcm_soma("abre el navegador y luego pon el brillo al 50") == LCM_HECHO && nej == 2 && !strcmp(ejecutadas[0], "navegador") && !strcmp(ejecutadas[1], "brillo 50"), "dos ordenes: %d %s %s", nej, ejecutadas[0], ejecutadas[1]);
    CK(strstr(dictamen(), "navegador") && strstr(dictamen(), "50"), "frase de las dos: %s", dictamen());
    limpia(); lcm_soma("abre paint y después borra notas.txt"); CK(nej == 0 && !lcm_pendiente[0], "una de ellas pide confirmación: no se hace solo una (%d)", nej);
    lcm_pendiente[0] = 0;
    /* SOMA_271_LCM: modelo versionado en DATOS (/lcm/modelo.lcm) */
    {
        static uint8_t paq[sizeof(lcm_ordenes_blob) + 400];
        const char *meta = "modelo=lcm-20261122-prueba\nfecha=2026-11-22\nordenes=80\nmanifiestos=cc\npuerta=apto\nexactas=83.0\nerrores=1.60\n";
        uint32_t lm = (uint32_t)strlen(meta), lb = (uint32_t)sizeof(lcm_ordenes_blob), h = 0x811C9DC5u;
        for (uint32_t i = 0; i < lb; ++i) { h ^= lcm_ordenes_blob[i]; h *= 0x01000193u; }
        memcpy(paq, "LCMP", 4); uint32_t cab[4] = { 1, lb, h, lm }; memcpy(paq + 4, cab, 16); memcpy(paq + 20, meta, lm); memcpy(paq + 20 + lm, lcm_ordenes_blob, lb);
        uint32_t tot = 20 + lm + lb; char o[1500];
        limpia(); lcm_cmd("modelo", o, sizeof o); CK(strstr(o, "de fabrica") && strstr(o, "No hay ninguno en DATOS"), "sin paquete: %s", o);
        paq_ptr = (char *)paq; paq_len = tot; lcm_cfg_set(8, 1);
        lcm_cmd("modelo", o, sizeof o); CK(strstr(o, "de DATOS") && strstr(o, "lcm-20261122-prueba") && strstr(o, "manifiestos: cc") && strstr(o, "exactas: 83.0"), "paquete bueno: %s", o);
        limpia(); CK(lcm_soma("lista mis archivos") == LCM_HECHO && nej == 1 && !strncmp(ejecutadas[0], "ls", 2), "con el modelo de DATOS traduce igual");
        lcm_cmd("modelo fabrica", o, sizeof o); CK(strstr(o, "de fabrica") && strstr(archivos[0], "modelo=0"), "fabrica: %s | %s", o, archivos[0]);
        lcm_cfg_set(1, 1); CK(strstr(archivos[0], "modelo=0") != 0, "la eleccion persiste al guardar otras opciones");
        lcm_cmd("modelo datos", o, sizeof o); CK(strstr(o, "de DATOS") && !strstr(archivos[0], "modelo=0"), "datos otra vez: %s", o);
        uint8_t malo[sizeof paq]; memcpy(malo, paq, tot); malo[tot - 100] ^= 0x55; paq_ptr = (char *)malo; lcm_cfg_set(8, 1);
        lcm_cmd("modelo", o, sizeof o); CK(strstr(o, "de fabrica") && strstr(o, "danado"), "byte cambiado: %s", o);
        limpia(); CK(lcm_soma("lista mis archivos") == LCM_HECHO, "dañado: sigue funcionando con el de fabrica");
        paq_len = tot - 1000; paq_ptr = (char *)paq; lcm_cfg_set(8, 1); lcm_cmd("modelo", o, sizeof o); CK(strstr(o, "de fabrica") && strstr(o, "incoherente"), "truncado: %s", o);
        memcpy(malo, paq, tot); malo[0] = 'X'; paq_ptr = (char *)malo; paq_len = tot; lcm_cfg_set(8, 1); lcm_cmd("modelo", o, sizeof o); CK(strstr(o, "no es un paquete"), "sin magia: %s", o);
        memcpy(malo, paq, tot); uint32_t fm = 2; memcpy(malo + 4, &fm, 4); lcm_cfg_set(8, 1); lcm_cmd("modelo", o, sizeof o); CK(strstr(o, "de fabrica"), "formato desconocido: %s", o);
        paq_ptr = 0; lcm_cfg_set(8, 1); lcm_cmd("modelo", o, sizeof o); CK(strstr(o, "de fabrica") && strstr(o, "No hay ninguno"), "borrado: %s", o);
        limpia(); CK(lcm_soma("lista mis archivos") == LCM_HECHO, "sin paquete sigue igual");
        if (getenv("LCM_PAQUETE_PRUEBA")) {      /* paquete hecho por tools/lcm_ordenes/reentrena.py (lo comprueba tests/test_lcm_271.py): el C lo acepta */
            FILE *fp = fopen(getenv("LCM_PAQUETE_PRUEBA"), "rb"); static char ext[8 << 20]; uint32_t ne = fp ? (uint32_t)fread(ext, 1, sizeof ext, fp) : 0; if (fp) fclose(fp);
            paq_ptr = ext; paq_len = ne; lcm_cfg_set(8, 1); lcm_cmd("modelo", o, sizeof o);
            CK(ne > 100 && strstr(o, "de DATOS") && strstr(o, "modelo: lcm-"), "paquete de Python aceptado por el C: %s", o);
            paq_ptr = 0; lcm_cfg_set(8, 1);
        }
    }
    printf("%s banco del 257: %d comprobaciones, %d fallos\n", fallos ? "FALLO" : "ok ", n, fallos);
    if (muestra) {
        printf("\nAsi suena (cada linea, una peticion a 'soma'):\n");
        const char *ej[] = { "hola", "buenos días", "¿qué tal estás?", "qué hora es", "qué día es hoy", "dime la fecha y la hora", "cuánto tiempo lleva encendido",
                             "cuánta memoria queda", "cuánto disco queda", "qué archivos tengo", "busca el archivo presupuesto", "cuántas palabras tiene notas.txt",
                             "cuánta batería me queda", "está caliente el procesador", "cuál es mi ip", "comprueba si responde 9.9.9.9", "abre el navegador",
                             "pon el brillo al 40", "sube el volumen al 80", "quién eres", "qué sabes hacer", "gracias", "hola, abre paint", "borra el archivo fotos.zip",
                             "no", "copia notas.txt a copia.txt", "sí", "adiós", 0 };
        for (int i = 0; ej[i]; ++i) ver(ej[i]);
    }
    return fallos != 0;
}
