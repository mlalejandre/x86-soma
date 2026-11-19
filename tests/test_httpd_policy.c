/* PARCHE 123: lista blanca del servidor HTTP. */
#include <stdio.h>
#include <string.h>
#include "../src/net/httpd_policy.h"
static int fails, checks;
static void T(const char *p, int want) { checks++; int g = httpd_path_allowed(p); if (g != want) { fails++; printf("FALLO: '%s' -> %d, esperado %d\n", p, g, want); } }
int main(void)
{
    T("/www/index.html", 1); T("/public/informe.txt", 1); T("/public/a/b/c.bmp", 1); T("/www/x", 1); T("/proc/version", 1); T("/proc/meminfo", 1); T("/dev/rtc", 1);
    T("/etc/init.sh", 0); T("/mem/x", 0); T("/tmp/x", 0); T("/sys/x", 0); T("/agent/state.txt", 0); T("/agent/blackboard.txt", 0); T("/workspace/n.txt", 0); T("/mis_archivos/a", 0);
    T("/bin/hello.elf", 0); T("/boot/x", 0); T("/share/x", 0); T("/informes/q3.txt", 0); T("/proc/1", 0); T("/proc/version/x", 0); T("/dev/null", 0); T("/dev/zero", 0);
    T("/www/", 0); T("/public/", 0); T("/www", 0); T("/public", 0); T("/wwwx/a", 0); T("/publicx/a", 0); T("/", 0); T("", 0); T("index.html", 0);
    T("/public/../etc/init.sh", 0); T("/www/../etc/init.sh", 0); T("/public//x", 0); T("//public/x", 0); T("/public/./x", 0); T("/public/.oculto", 0); T("/public/%2e%2e/etc", 0);
    T("/public/a\\b", 0); T("/public/a\nb", 0); T("/public/a\x7f" "b", 0); T("/public/a%00b", 0); T("/www/..", 0); T("/www/a/..", 0);
    { char big[200]; memset(big, 'a', sizeof big); memcpy(big, "/public/", 8); big[199] = 0; T(big, 0); }
    { char ok[122]; memset(ok, 'a', sizeof ok); memcpy(ok, "/public/", 8); ok[120] = 0; T(ok, 1); ok[120] = 'a'; ok[121] = 0; T(ok, 0); }   /* 120 bytes si, 121 no */
    T(0, 0);
    {   /* SOMA_197: solo la red local; configuracion */
#define P(a,b,c,d, want) do { unsigned char pe[4] = { a, b, c, d }; checks++; if (httpd_peer_allowed(pe, ip, mk) != (want)) { fails++; printf("FALLO par %d.%d.%d.%d\n", a, b, c, d); } } while (0)
        unsigned char ip[4] = { 192, 168, 1, 37 }, mk[4] = { 255, 255, 255, 0 };
        P(192, 168, 1, 200, 1); P(192, 168, 1, 1, 1); P(192, 168, 2, 5, 0); P(8, 8, 8, 8, 0); P(10, 0, 2, 2, 0); P(127, 0, 0, 1, 1); P(0, 0, 0, 0, 0);
        mk[2] = 0; P(192, 168, 2, 5, 1); P(192, 169, 1, 1, 0);
        mk[0] = mk[1] = mk[2] = mk[3] = 0; P(192, 168, 1, 200, 0); P(8, 8, 8, 8, 0);                 /* sin mascara: nadie */
        mk[0] = 255; ip[0] = ip[1] = ip[2] = ip[3] = 0; P(0, 1, 2, 3, 0);                            /* sin direccion: nadie */
#define M(t, def, want) do { checks++; if (httpd_conf_mode(t, def) != (want)) { fails++; printf("FALLO conf %s\n", t ? t : "(nulo)"); } } while (0)
        M("servidor=activo\n", 0, 1); M("# nota\nservidor=apagado\n", 1, 0); M("", 1, 1); M("", 0, 0); M(0, 1, 1); M("servidor=quiza\n", 0, 0); M("xservidor=activo\n", 0, 0);
        M("otra=1\nservidor=activo", 0, 1);
    }
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO" : "OK", checks, fails);
    return fails != 0;
}
