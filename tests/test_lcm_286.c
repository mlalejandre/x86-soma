/* SOMA_286_YOUTUBE: videos y YouTube desde el orbe (lcm_soma.inc: lcm_video). Mismo banco que el 257 (imitaciones de SOMA). */
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
static const char *const VID[][2] = {
    { "busca videos de gatos en youtube", "youtube gatos" },
    { "quiero ver un v\303\255deo sobre agujeros negros", "youtube agujeros negros" },
    { "ens\303\251\303\261ame v\303\255deos de recetas de paella", "youtube recetas de paella" },
    { "abre youtube", "youtube" },
    { "pon m\303\272sica relajante", "youtube m\303\272sica relajante" },
    { "pon un v\303\255deo de gatos", "youtube gatos" },
    { "Soma, \302\277me pones un tutorial de Python?", "youtube tutorial de Python" },
    { "busca la \303\272ltima canci\303\263n de Rosal\303\255a", "youtube \303\272ltima canci\303\263n de Rosal\303\255a" },
    { "busca v\303\255deos que expliquen la relatividad", "youtube relatividad" },
    { "quiero escuchar m\303\272sica cl\303\241sica", "youtube m\303\272sica cl\303\241sica" },
    { "pon el segundo", "youtube ver 2" },
    { "pon el segundo v\303\255deo", "youtube ver 2" },
    { "reproduce el n\303\272mero 4", "youtube ver 4" },
    { "el tercero", "youtube ver 3" },
    { "me pones el \303\272ltimo v\303\255deo", "youtube ver ultimo" },
    { "resume el v\303\255deo", "youtube resumen" },
    { "transcribe el v\303\255deo", "youtube transcribir" },
    { "\302\277de qu\303\251 trata el v\303\255deo?", "youtube pregunta \302\277De qu\303\251 trata el v\303\255deo?" },
    { "preg\303\272ntale al v\303\255deo cu\303\241ntos planetas hay", "youtube pregunta cu\303\241ntos planetas hay" },
    { "\302\277qu\303\251 dice el v\303\255deo sobre la fotos\303\255ntesis?", "youtube pregunta la fotos\303\255ntesis" },
    { "pausa youtube", "youtube pausa" },
    { "siguiente v\303\255deo de youtube", "youtube siguiente" } };
static const char *const NO_VID[] = {
    "pausa el v\303\255deo",
    "det\303\251n el v\303\255deo",
    "busca archivos de v\303\255deo en mi disco",
    "pon el brillo al 50",
    "qu\303\251 hora es",
    "pon un temporizador de 5 minutos",
    "quiero ver una pel\303\255cula",
    "expl\303\255came qu\303\251 es un agujero negro",
    "no leas m\303\241s",
    "crear directorio copias",
    "somafetch",
    "la tarea 7 se ha colgado, term\303\255nala",
    "haz un informe de arranque",
    "mira si contesta servidor1",
    "limpia la terminal",
    "los iconos est\303\241n descolocados",
    "a qu\303\251 d\303\255a estamos",
    "\302\277me haces una copia de seguridad?",
    "rollback",
    "cu\303\241nto ocupan los archivos",
    "deshaz los cambios desde la \303\272ltima copia",
    "comienzo de carta_banco.txt 2 l\303\255neas",
    "monitor de rendimiento",
    "log de arranque",
    "tengo internet",
    "\302\277qu\303\251 soma tengo instalado?",
    "informe",
    "ps",
    "abre la terminal",
    "ram",
    "crea un informe del hardware",
    "qu\303\251 diferencia hay entre mi perro",
    "archivos?",
    "\302\277cu\303\241nto llevas encendido?",
    "\302\277cu\303\241nto puedo guardar todav\303\255a?",
    "cerrar todas las ventanas",
    "necesito corregir una cosa en grabacion3.wav",
    "b\303\272scame la ip de 192.168.0.20",
    "en entrevista.wav escribe cumplea\303\261os de ana",
    "salva los archivos",
    "modo rendimiento",
    "ls de proyectos",
    "con\303\251ctate a Biblio-Publica, que no tiene clave",
    "\302\277me listas los archivos?",
    "menos luz en la pantalla",
    "quiero ver la carpeta documentos",
    "qu\303\251 hay en mis archivos",
    "contenido de el archivo programa.py",
    "\302\277me cierras im\303\241genes?",
    "desconecta la wifi",
    "\302\277me ense\303\261as log_servidor.log?",
    "crear carpeta facturas",
    "menos luz en la pantalla",
    "\302\277me abres informe.pdf?",
    "contenido del disco",
    "\302\277en qu\303\251 modo de energ\303\255a est\303\241?",
    "abre el monitor",
    "carpeta colegio",
    "qu\303\251 fecha es hoy",
    "cierra el c\303\241mara",
    "ahorra bater\303\255a",
    "tree",
    "capacidad del almacenamiento",
    "\302\277me ense\303\261as qu\303\251 hay en descargas?",
    "mover programa.py a papelera",
    "archivos?",
    "ver archivos",
    "cu\303\241l perfil de energ\303\255a tiene el procesador",
    "anota en tabla.csv que programar en c",
    "qu\303\251 hay en el disco",
    "borra el directorio /mis_archivos/viajes2024",
    "en qu\303\251 disco se guarda todo",
    "usb",
    "restaura la copia de seguridad",
    "memoria libre",
    "log de arranque",
    "que se guarde todo ya en el disco",
    "quiero programar en resumen.md",
    "coge ip por dhcp",
    "la hora, por favor",
    "\302\277hace cu\303\241nto arrancaste?",
    "necesito leer dibujo.png",
    "wc cuentas.xlsx",
    "abre el dibujo musica_relax.wav",
    "\302\277puedes pausar el v\303\255deo?",
    "l\303\251eme error.log",
    "reinicia el ordenador",
    "estado de la conexi\303\263n",
    "abre soma media",
    "ls series",
    "qu\303\251 archivos tengo",
    "averigua la ip de soma.local",
    "uptime",
    "guarda la frase hola qu\303\251 tal en el archivo acta_reunion.txt",
    "\302\277me dices qu\303\251 hay en trabajo2025?",
    "qu\303\251 ip tiene elpais.com",
    "capacidad del disco",
    "\302\277va bien de memoria?",
    "finaliza el proceso 10",
    "hora?",
    "informaci\303\263n del disco de datos",
    "lee error.log",
    "qu\303\251 cosas hay en series",
    "lista lo de Clase-3B",
    "\302\277aguanta mucho la bater\303\255a?",
    "ram?",
    "\302\277me conectas a la red Cafe_Central_Free?",
    "det\303\251n el v\303\255deo",
    "\302\277me copias lista_libros.txt en Proyectos2026?",
    "cu\303\241nto espacio queda libre",
    "contenido de recetas_abuela",
    "\302\277qu\303\251 tengo en el ordenador?",
    "df",
    "hay conexi\303\263n con ejemplo.com",
    "\302\277me haces una copia de seguridad?",
    "ficha del sistema",
    "\302\277me creas un archivo llamado Plan-Semanal.txt?",
    "por qu\303\251 buenos d\303\255as",
    "carpeta impuestos-2025",
    "\302\277cu\303\241nto ocupa en palabras tiene diario_viaje.md?",
    "qu\303\251 ip tengo",
    "cambia el nombre de IMG_2031.jpg a viejos",
    "necesito ver mis archivos",
    "que apuntes-tema4.tex se llame Notas-Viejas.txt",
    "qu\303\251 hora es",
    "hora?",
    "mv entrevista.wav /mis_archivos/archivo",
    "tarjeta del sistema", 0 };
int main(void)
{
    for (unsigned i = 0; i < sizeof VID / sizeof VID[0]; ++i) {
        char ord[512]; int v = lcm_video(VID[i][0], ord, sizeof ord);
        CK(v && !strcmp(ord, VID[i][1]), "%s -> %s (esperaba %s)", VID[i][0], v ? ord : "nada", VID[i][1]);
    }
    for (int i = 0; NO_VID[i]; ++i) { char ord[512]; int v = lcm_video(NO_VID[i], ord, sizeof ord); CK(!v, "no es de YouTube: %s -> %s", NO_VID[i], ord); }
    /* por el camino entero: la orden se ejecuta y la frase es la de YouTube */
    limpia(); CK(lcm_soma("quiero ver un vídeo sobre agujeros negros") == LCM_HECHO && nej == 1 && !strcmp(ejecutadas[0], "youtube agujeros negros") && strstr(dictamen(), "agujeros negros"), "orbe: %s", dictamen());
    limpia(); CK(lcm_soma("resume el vídeo") == LCM_HECHO && nej == 1 && !strcmp(ejecutadas[0], "youtube resumen") && strstr(dictamen(), "esum"), "resumen: %s", dictamen());
    limpia(); CK(lcm_soma("pausa el vídeo") != LCM_HECHO || nej == 0 || strncmp(ejecutadas[0], "youtube", 7), "sin YouTube abierto, pausa no es de YouTube");
    printf("%s  videos y YouTube (286): %d comprobaciones, %d fallos\n", fallos ? "FALLO" : "ok", n, fallos);
    return fallos != 0;
}
