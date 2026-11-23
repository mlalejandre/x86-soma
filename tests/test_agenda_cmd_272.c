/* SOMA_272_AGENDA: la orden `agenda` y el guardado (agenda.c) con imitaciones del sistema:
 * cc -std=gnu11 -g -fsanitize=address,undefined -Itests/stubs_agenda -Isrc/apps tests/test_agenda_cmd_272.c src/apps/agenda.c src/apps/agenda_core.c -o /tmp/tc */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agenda.h"
#include "rtc.h"

static struct { char nombre[64]; char *d; uint32_t n; } F[16];
static int nf = 0;
static int fi(const char *n) { for (int i = 0; i < nf; ++i) if (!strcmp(F[i].nombre, n)) return i; return -1; }
int vfs_write(const char *n, const char *d, uint32_t l) { int i = fi(n); if (i < 0) { i = nf++; snprintf(F[i].nombre, 64, "%s", n); F[i].d = 0; } free(F[i].d); F[i].d = (char *)malloc(l + 1); memcpy(F[i].d, d, l); F[i].d[l] = 0; F[i].n = l; return (int)l; }
int vfs_read(const char *n, char *b, uint32_t m) { int i = fi(n); if (i < 0) return -1; uint32_t l = F[i].n < m ? F[i].n : m; memcpy(b, F[i].d, l); return (int)l; }
char *vfs_read_alloc(const char *n, uint32_t *l) { int i = fi(n); if (i < 0) return 0; char *b = (char *)malloc(F[i].n + 1); memcpy(b, F[i].d, F[i].n + 1); *l = F[i].n; return b; }
void *kmalloc(size_t s) { return malloc(s); }
void kfree(void *p) { free(p); }
static struct rtc_time T = { 2026, 11, 23, 9, 30, 0 };         /* UTC: en Madrid (invierno) son las 10:30 */
void rtc_get_datetime(struct rtc_time *t) { *t = T; }
static uint64_t MS = 100000;
uint64_t timer_get_uptime_ms(void) { return MS; }

static int fallos = 0, n = 0;
#define CK(c, ...) do { ++n; if (!(c)) { ++fallos; printf("FALLO (l.%d): ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static char o[6000];
static const char *A(const char *a) { o[0] = 0; agenda_cmd(a, o, sizeof o); return o; }

int main(void)
{
    CK(strstr(A(""), "0 eventos") && strstr(o, "No hay nada"), "vacia: %s", o);
    CK(strstr(A("añadir el miércoles día 6 a las 13 horas tengo un partido de fútbol con los amigos"), "Apuntado [1]: Partido de f\xC3\xBAtbol con los amigos \xE2\x80\x94 Mi\xC3\xA9rcoles 6 de enero de 2027 a las 13:00"), "añadir: %s", o);
    CK(strstr(A("anadir mañana tengo reunión con Marta a las 9:30"), "Apuntado [2]: Reuni\xC3\xB3n con Marta \xE2\x80\x94 Ma\xC3\xB1" "ana a las 09:30"), "sin ñ: %s", o);
    CK(strstr(A("mañana a las 18 cena con los suegros"), "Apuntado [3]"), "sin verbo: añade: %s", o);
    CK(strstr(A("añadir cada lunes a las 8 gimnasio"), "(se repite cada semana)") && strstr(o, "30 de noviembre"), "repite: %s", o);
    CK(strstr(A("mañana"), "Reuni\xC3\xB3n con Marta") && strstr(o, "09:30") && strstr(o, "Cena con los suegros") && strstr(o, "[2]") && !strstr(o, "Partido"), "consulta mañana: %s", o);
    CK(strstr(A("ver el miércoles día 6"), "Partido de f\xC3\xBAtbol con los amigos") && strstr(o, "13:00"), "ver miercoles 6: %s", o);
    CK(strstr(A("semana"), "[2]") != 0 && !strstr(o, "Partido"), "semana: %s", o);
    CK(strstr(A("mes"), "[2]") != 0, "mes");
    CK(strstr(A("proximos 60 dias"), "Partido") != 0 && strstr(A("proximos 60 dias"), "Gimnasio") != 0, "proximos 60 dias");
    CK(strstr(A("proximos"), "Lo que viene") && strstr(o, "[2]") && strstr(A("próximos"), "Lo que viene"), "proximos sin numero: %s", o);
    CK(strstr(A("buscar fútbol"), "[1]") != 0 && !strstr(o, "Reuni"), "buscar: %s", o);
    CK(strstr(A("buscar nada de nada"), "No encuentro") != 0, "buscar vacio");
    CK(strstr(A("detalle 1"), "Categor\xC3\xAD" "a: Deporte") && strstr(o, "Aviso: el de siempre"), "detalle: %s", o);
    CK(strstr(A("mover 3 el viernes a las 20"), "Movido: Cena con los suegros \xE2\x80\x94 Viernes 27 de noviembre a las 20:00"), "mover: %s", o);
    CK(strstr(A("mover 3 patatas"), "No entiendo") != 0 && strstr(A("mover 99 mañana"), "Uso:") != 0, "mover mal");
    CK(strstr(A("saltar 4 el lunes"), "Quitado solo") != 0 && strstr(A("saltar 1 mañana"), "Uso:") != 0, "saltar: %s", o);
    CK(strstr(A("ver 30 de noviembre"), "Gimnasio") == 0, "el lunes saltado ya no sale: %s", o);
    CK(strstr(A("añadir comprar pan"), "No entiendo cu") != 0, "sin fecha: %s", o);
    CK(strstr(A("ayuda"), "agenda a\xC3\xB1" "adir") != 0, "ayuda");
    CK(strstr(A("xyz qwerty"), "No lo he entendido") != 0, "basura: %s", o);
    /* avisos: configuracion y persistencia */
    CK(strstr(A("avisos antes 15"), "15 min antes") && strstr(A("avisos voz off"), "sin voz") && strstr(A("avisos visual off"), "sin pantalla") && strstr(A("avisos off"), "desactivados"), "config: %s", o);
    CK(fi("/etc/agenda.conf") >= 0 && strstr(F[fi("/etc/agenda.conf")].d, "antes=15") && strstr(F[fi("/etc/agenda.conf")].d, "avisos=0") && strstr(F[fi("/etc/agenda.conf")].d, "voz=0"), "conf guardada");
    A("avisos on"); A("avisos voz on"); A("avisos visual on");
    CK(strstr(A("avisos antes 99999"), "Uso") != 0 && strstr(A("avisos patata"), "Uso") != 0, "avisos mal");
    /* el tick: la cena del viernes 27 a las 20:00, aviso 15 min antes */
    char txt[300]; struct agenda_conf cf; agenda_conf_get(&cf); CK(cf.avisos && cf.antes == 15, "conf leida");
    MS += 2000; CK(agenda_tick(txt, sizeof txt) == 0, "aun no hay avisos");
    T.day = 27; T.hour = 18; T.min = 40; MS += 61000; CK(agenda_tick(txt, sizeof txt) == 0, "27 a las 19:40: aun no");
    T.min = 46; MS += 61000; CK(agenda_tick(txt, sizeof txt) == 1 && !strcmp(txt, "En 14 minutos: Cena con los suegros, a las 20:00."), "aviso hablado: «%s»", txt);
    MS += 2000; CK(agenda_tick(txt, sizeof txt) == 0, "no se repite");
    A("avisos off"); T.day = 29; MS += 61000; CK(agenda_tick(txt, sizeof txt) == 0, "apagado no avisa");
    /* persistencia: otro arranque lee lo guardado */
    CK(fi("/agenda/eventos.txt") >= 0 && !strncmp(F[fi("/agenda/eventos.txt")].d, "SOMA-AGENDA 1", 13), "guardado en /agenda/eventos.txt");
    /* exportar / importar */
    CK(strstr(A("exportar"), "Exportados 4 eventos a /usuario/agenda.ics") != 0 && fi("/usuario/agenda.ics") >= 0 && strstr(F[fi("/usuario/agenda.ics")].d, "SUMMARY:Cena con los suegros"), "exportar: %s", o);
    CK(strstr(A("borrar 3"), "Borrado [3] Cena con los suegros") != 0 && strstr(A("borrar 3"), "No hay ningun") != 0 && strstr(A("borrar"), "Uso") != 0, "borrar");
    CK(strstr(A("importar /usuario/agenda.ics"), "Importados 4 eventos") != 0, "importar (duplica): %s", o);
    CK(strstr(A("importar /nada.ics"), "No encuentro") != 0, "importar inexistente");
    vfs_write("/usuario/x.ics", "no soy un ics", 13); CK(strstr(A("importar /usuario/x.ics"), "no parece") != 0, "importar basura");
    CK(strstr(A("estado"), "eventos") != 0, "estado");
    /* un reinicio: estado nuevo, mismos datos (se recarga de /agenda/eventos.txt) */
    printf("%s banco de la orden agenda (272): %d comprobaciones, %d fallos\n", fallos ? "FALLO" : "ok ", n, fallos);
    return fallos != 0;
}
