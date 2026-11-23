/* SOMA_272_AGENDA: ejecuta lineas "agenda ..." de la entrada estandar con la orden real (para comprobar el manifiesto del LCM):
 * cc -std=gnu11 -g -fsanitize=address,undefined -Itests/stubs_agenda -Isrc/apps tests/agenda_cli_272.c src/apps/agenda.c src/apps/agenda_core.c -o /tmp/cli */
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


int main(void)
{
    char l[600], o[8000];
    while (fgets(l, sizeof l, stdin)) {
        size_t n = strlen(l); while (n && (l[n - 1] == '\n' || l[n - 1] == '\r')) l[--n] = 0;
        if (strncmp(l, "agenda", 6)) continue;
        o[0] = 0; agenda_cmd(l[6] ? l + 7 : "", o, sizeof o);
        for (char *p = o; *p; ++p) if (*p == '\n') *p = '|';
        printf("%s\t%s\n", l, o);
    }
    return 0;
}
