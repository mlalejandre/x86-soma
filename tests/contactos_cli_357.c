/* SOMA_357_CONTACTOS: ejecuta lineas "contactos ..." de la entrada estandar con la orden real (contactos.c + agenda.c de verdad; el
 * almacen, el reloj y la memoria son de mentira). Lo usan tests/test_contactos_manifiesto_357.py y la prueba de integracion de abajo.
 * Salida: una linea por orden: "orden<TAB>respuesta" (los saltos de linea como '|'). Con "--integra" hace la prueba completa y sale con 1 si falla. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contactos.h"
#include "agenda.h"
#include "rtc.h"
#include "bigfs_glue.h"

static struct { char nombre[64]; char *d; uint32_t n; } F[32];
static int nf = 0;
static int fi(const char *n) { for (int i = 0; i < nf; ++i) if (!strcmp(F[i].nombre, n)) return i; return -1; }
int vfs_write(const char *n, const char *d, uint32_t l) { if (strncmp(n, "/usuario/", 9) && strncmp(n, "/contactos/", 11) && strncmp(n, "/agenda/", 8) && strncmp(n, "/etc/", 5) && strncmp(n, "/tmp/", 5)) return -1; int i = fi(n); if (i < 0) { i = nf++; snprintf(F[i].nombre, 64, "%s", n); F[i].d = 0; } free(F[i].d); F[i].d = (char *)malloc(l + 1); memcpy(F[i].d, d, l); F[i].d[l] = 0; F[i].n = l; return (int)l; }
int vfs_read(const char *n, char *b, uint32_t m) { int i = fi(n); if (i < 0) return -1; uint32_t l = F[i].n < m ? F[i].n : m; memcpy(b, F[i].d, l); return (int)l; }
char *vfs_read_alloc(const char *n, uint32_t *l) { int i = fi(n); if (i < 0) return 0; char *b = (char *)malloc(F[i].n + 1); memcpy(b, F[i].d, F[i].n + 1); *l = F[i].n; return b; }
int vfs_size(const char *n) { int i = fi(n); return i < 0 ? -1 : (int)F[i].n; }
int vfs_format_list_ext(int a, const char *d, char *o, uint32_t m) { (void)a; (void)d; if (m) o[0] = 0; return 0; }
void *kmalloc(size_t s) { return malloc(s); }
void kfree(void *p) { free(p); }
static struct rtc_time T = { 2026, 11, 23, 9, 30, 0 };
void rtc_get_datetime(struct rtc_time *t) { *t = T; }
uint64_t timer_get_uptime_ms(void) { return 100000; }
uint8_t net_ip[4];
struct netif_ops;
const struct netif_ops *netif_active(void) { return 0; }
struct bigfs *bigfs_sys(int *err) { if (err) *err = -19; return 0; }      /* sin almacen BIGFS: fotos y voz dicen que no hay */

static int fallos = 0;
static void ck(int c, const char *m) { if (!c) { ++fallos; printf("FALLO: %s\n", m); } }
static char o[20000];
static const char *run(const char *args) { o[0] = 0; contactos_cmd(args, o, sizeof o); return o; }

static int integra(void)
{
    ck(strstr(run(""), "Contactos: 0") != 0, "vacia");
    ck(strstr(run("a\303\261adir Ana L\303\263pez, m\303\263vil 600 11 22 33, email ana@x.es, naci\303\263 el 25 de noviembre de 1990, grupo Familia"), "Contacto guardado") != 0, "anadir");
    ck(strstr(run("anade Luis P\303\251rez, tel 611222333, cumplea\303\261os 3 de diciembre"), "Contacto guardado") != 0, "anadir 2");
    ck(strstr(run("telefono de Ana"), "600 11 22 33") != 0, "telefono de Ana");
    ck(strstr(run("email ana l\303\263pez"), "ana@x.es") != 0, "email");
    ck(strstr(run("cumplea\303\261os de Luis"), "3 de diciembre") != 0, "cumple de Luis");
    ck(strstr(run("cumplea\303\261os"), "Ana L\303\263pez") != 0 && strstr(o, "Luis") != 0, "proximos cumpleanos");
    ck(strstr(run("editar Ana ciudad Madrid"), "Cambiado") != 0, "editar");
    ck(strstr(run("ver ana"), "Madrid") != 0, "ver");
    ck(strstr(run("editar Ana email no-vale"), "no vale") != 0, "editar con valor malo");
    ck(strstr(run("grupo familia"), "Ana") != 0, "grupo");
    ck(strstr(run("grupos"), "Familia (1)") != 0, "grupos");
    /* los cumpleanos estan en el Calendario como eventos anuales, y no salen en el .ics */
    struct ag_cal *ag = agenda_cal(); int n = 0;
    for (int i = 0; i < AG_MAX_EV; ++i) if (ag->ev[i].activo && !strncmp(ag->ev[i].nota, "contacto:", 9)) { ++n; ck(ag->ev[i].rep == AG_REP_ANO && ag->ev[i].todo_dia, "evento anual de todo el dia"); }
    ck(n == 2, "dos cumpleanos en el calendario");
    { static char ics[200000]; uint32_t k = ag_ics_exporta(ag, ics, sizeof ics); ck(k > 0 && !strstr(ics, "Cumplea"), "los cumpleanos no van al .ics"); }
    ck(strstr(run("editar Luis nacimiento -"), "Cambiado") != 0, "quitar fecha");
    n = 0; for (int i = 0; i < AG_MAX_EV; ++i) if (ag->ev[i].activo && !strncmp(ag->ev[i].nota, "contacto:", 9)) ++n;
    ck(n == 1, "al quitar la fecha se va del calendario");
    /* exportar, importar: sin duplicados; lo cambiado fuera entra */
    ck(strstr(run("exportar"), "Exportados 2") != 0, "exportar");
    int i = fi("/usuario/contactos.vcf"); ck(i >= 0 && strstr(F[i].d, "BEGIN:VCARD") && !strstr(F[i].d, "X-SOMA-ID"), "vcf sin datos internos");
    ck(strstr(run("importar /usuario/contactos.vcf"), "0 nuevos") != 0, "reimportar: 0 nuevos");
    vfs_write("/usuario/google.vcf", "BEGIN:VCARD\r\nVERSION:3.0\r\nN:L\303\263pez;Ana;;;\r\nFN:Ana L\303\263pez\r\nEMAIL:ana@x.es\r\nTITLE:Arquitecta\r\nEND:VCARD\r\nBEGIN:VCARD\r\nVERSION:3.0\r\nFN:Marta Gil\r\nTEL;TYPE=CELL:+34 622 000 111\r\nBDAY:1985-11-24\r\nEND:VCARD\r\n", 0);
    { const char *g = "BEGIN:VCARD\r\nVERSION:3.0\r\nN:L\303\263pez;Ana;;;\r\nFN:Ana L\303\263pez\r\nEMAIL:ana@x.es\r\nTITLE:Arquitecta\r\nEND:VCARD\r\nBEGIN:VCARD\r\nVERSION:3.0\r\nFN:Marta Gil\r\nTEL;TYPE=CELL:+34 622 000 111\r\nBDAY:1985-11-24\r\nEND:VCARD\r\n"; vfs_write("/usuario/google.vcf", g, (uint32_t)strlen(g)); }
    ck(strstr(run("importar /usuario/google.vcf"), "1 nuevos, 1 actualizados") != 0, "importar de Google");
    ck(strstr(run("ver ana"), "Arquitecta") != 0, "el cargo llega");
    ck(strstr(run("cumplea\303\261os"), "ma\303\261ana") != 0, "Marta cumple manana");
    ck(strstr(run("importar /usuario/nada.vcf"), "No encuentro") != 0, "archivo que no hay");
    ck(strstr(run("importar /etc/agenda.conf"), "No encuentro") != 0 || strstr(o, "no parece") != 0, "no es vcf");
    ck(strstr(run("exportar /sistema/x.vcf"), "No se pudo escribir") != 0, "solo en /usuario y /tmp");
    ck(strstr(run("foto ana /usuario/x.jpg"), "No encuentro esa imagen") != 0, "foto que no hay");
    ck(strstr(run("borrar luis"), "Borrado") != 0, "borrar");
    ck(strstr(run("ana"), "ana@x.es") != 0, "buscar por nombre a secas: ficha");
    ck(strstr(run("zzzz"), "No encuentro") != 0, "nadie");
    ck(strstr(run("ayuda"), "importar") != 0, "ayuda");
    /* se guarda y se vuelve a leer igual */
    i = fi("/contactos/contactos.vcf"); ck(i >= 0 && strstr(F[i].d, "X-SOMA-ID") != 0, "archivo propio con X-SOMA");
    /* el agente: la ficha entra en el contexto cuando se habla de alguien de la libreta */
    { static char ctx[8000]; uint32_t p = 0; ctx[0] = 0; contactos_contexto("cual es el movil de Marta?", ctx, sizeof ctx, &p); ck(strstr(ctx, "622 000 111") != 0, "contexto: Marta"); }
    { static char ctx[8000]; uint32_t p = 0; ctx[0] = 0; contactos_contexto("que tiempo hace hoy", ctx, sizeof ctx, &p); ck(p == 0, "contexto: nada que ver"); }
    { static char ctx[8000]; uint32_t p = 0; ctx[0] = 0; contactos_contexto("a quien tengo que felicitar por su cumpleanos?", ctx, sizeof ctx, &p); ck(strstr(ctx, "Marta") != 0, "contexto: cumpleanos"); }
    printf("contactos_cli_357 --integra: %s\n", fallos ? "FALLOS" : "bien");
    return fallos ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--integra")) return integra();
    char l[800];
    while (fgets(l, sizeof l, stdin)) {
        size_t n = strlen(l); while (n && (l[n - 1] == '\n' || l[n - 1] == '\r')) l[--n] = 0;
        if (strncmp(l, "contactos", 9)) continue;
        run(l[9] ? l + 10 : "");
        for (char *p = o; *p; ++p) if (*p == '\n') *p = '|';
        printf("%s\t%s\n", l, o);
    }
    return 0;
}
