/* PARCHE 121: politica de rutas de SYS_OPEN (ring 3). */
#include <stdio.h>
#include <string.h>
#include "../src/arch/x86_64/user_path.h"
static int fails, checks;
static void T(const char *p, int want) { checks++; int got = user_path_allowed(p, (unsigned)strlen(p)); if (got != want) { fails++; printf("FALLO: '%s' -> %d, esperado %d\n", p, got, want); } }
int main(void)
{
    /* permitidas */
    T("/bin/hello.elf", 1); T("/music/a.wav", 1); T("/video/x.bin", 1); T("/workspace/n.txt", 1); T("/mis_archivos/a b.txt", 1); T("/share/x", 1);
    T("/bin/./hello.elf", 1); T("//bin//hello.elf", 1); T("/bin/../music/a.wav", 1); T("/a/b/c/d/e.txt", 1); T("/etcetera/x", 1); T("/memoria/x", 1); T("/agentes/x", 1);
    /* privadas */
    T("/etc/init.sh", 0); T("/etc", 0); T("/sys/state", 0); T("/boot/x", 0); T("/mem/notas", 0); T("/agent/state.txt", 0); T("/tmp/x", 0); T("/proc/1", 0); T("/dev/null", 0);
    /* internas de BIGFS y ocultas */
    T("/.ramfs/0", 0); T("/.ramfs/t000", 0); T("/.ckpt/B", 0); T("/.x", 0); T("/.", 0); T("/..", 0);
    /* trucos de normalizacion */
    T("/bin/../.ramfs/0", 0); T("//.ramfs/0", 0); T("/./.ramfs/0", 0); T("/music/../../.ramfs/0", 0); T("/bin/../etc/init.sh", 0); T("/workspace/../mem/x", 0); T("/etc/../etc//init.sh", 0);
    T("/bin/./../.ramfs/./1", 0); T("/music/../mem", 0); T("/a/../../../../etc/passwd", 0);
    /* sin ruta / invalidas */
    T("", 0); T("/", 0); T("//", 0); T("/./", 0); T("/..", 0);
    { char big[400]; memset(big, 'a', sizeof big); big[0] = '/'; big[399] = 0; checks++; if (user_path_allowed(big, 399)) { fails++; printf("FALLO: ruta de 399 bytes aceptada\n"); } }
    { char nul[16] = "/bin/x\0/.ramfs"; checks++; if (user_path_allowed(nul, 14)) { fails++; printf("FALLO: NUL dentro de la ruta aceptado\n"); } }
    checks++; if (user_path_allowed(0, 5)) { fails++; printf("FALLO: puntero nulo\n"); }
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO" : "OK", checks, fails);
    return fails != 0;
}
