/* Pruebas en host de la politica de escritura de los agentes (src/agent/agent_policy.h), con casos de evasion y fuzz. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "agent_policy.h"

static void canon(const char *in, const char *esperado)
{
    char c[256];
    CHECK(agent_path_canon(in, c, sizeof c));
    if (strcmp(c, esperado) != 0) { printf("  FALLO: canon(\"%s\") = \"%s\" (esperado \"%s\")\n", in, c, esperado); soma_t_fails++; }
}
static void test_canon(void)
{
    canon("/etc/init.sh", "/etc/init.sh"); canon("etc/init.sh", "/etc/init.sh"); canon("//etc///init.sh//", "/etc/init.sh");
    canon("/./etc/./init.sh", "/etc/init.sh"); canon("/workspace/../etc/init.sh", "/etc/init.sh"); canon("/a/b/../../etc", "/etc");
    canon("/..", "/"); canon("/../../etc", "/etc"); canon("", "/"); canon("/", "/"); canon("/a/b/c/..", "/a/b");
}
static void test_protegidas(void)
{
    const char *no_tocar[] = { "/etc/init.sh", "/etc/soma_manifest.txt", "/etc/ui.conf", "/etc", "/etc/", "/sys/status.txt", "/bin/hello.elf",
        "/boot/x", /* SOMA_271_LCM */ "/lcm/modelo.lcm", "/lcm", "/workspace/../lcm/modelo.lcm", "//lcm//x", "etc/init.sh", "//etc//init.sh", "/./etc/init.sh", "/workspace/../etc/init.sh", "/workspace/./../etc/init.sh",
        "/a/../../etc/init.sh", "/etc/../etc/init.sh", "/tmp/../sys/telemetry.txt", "/etc/./", "/.//etc", 0 };
    for (int i = 0; no_tocar[i]; ++i)
        if (!agent_path_protected(no_tocar[i])) { printf("  FALLO: deberia estar protegida: %s\n", no_tocar[i]); soma_t_fails++; }
    const char *libres[] = { "/workspace/a.soma", "/mis_archivos/x.txt", "/tmp/t", "/test_fib.soma", "nota.txt", "/etcetera/x", "/etc2/x", "/lcmx/a", "/lcm2",
        "/system/x", "/binario", "/bootstrap/a", "/workspace/etc/init.sh", "/mem/indice.txt", "/agent/coder_ultimo.txt",
        "/workspace/../mis_archivos/a", "/etc/../workspace/a", 0 };
    for (int i = 0; libres[i]; ++i)
        if (agent_path_protected(libres[i])) { printf("  FALLO: deberia ser libre: %s\n", libres[i]); soma_t_fails++; }
    CHECK(agent_path_protected(0));
}
/* SOMA_268_ACCESO: el usuario solo escribe en /usuario y /tmp (con evasiones) */
static void test_usuario_268(void)
{
    const char *si[] = { "/usuario", "/usuario/a.txt", "/usuario/Documentos/x/y.doc", "//usuario//a", "/./usuario/a", "/tmp/t", "/tmp",
        "/workspace/../usuario/a", "/etc/../usuario/a", "/usuario/../tmp/x", "usuario/a", "/usuario/etc/init.sh", 0 };
    for (int i = 0; si[i]; ++i)
        if (!usuario_ruta_escribible(si[i])) { printf("  FALLO: deberia poder escribirse: %s\n", si[i]); soma_t_fails++; }
    const char *no[] = { "/etc/init.sh", "/etc/llm.conf", "/mem/indice.txt", "/agent/state", "/mis_archivos/x", "/workspace/a.soma", "/", "",
        "/usuarios/x", "/usuarioX", "/tmpx/a", "/modelos/voz/x.bin", "/usuario/../etc/init.sh", "/tmp/../etc/init.sh", "/usuario/../../etc/x",
        "/tmp/../mem/a", "/lcm/modelo.lcm", "/usuario/../lcm/modelo.lcm", "/.usuario/a", "usuari/a", 0 };
    for (int i = 0; no[i]; ++i)
        if (usuario_ruta_escribible(no[i])) { printf("  FALLO: no deberia poder escribirse: %s\n", no[i]); soma_t_fails++; }
    CHECK(!usuario_ruta_escribible(0));
    char l[600]; memset(l, 'a', sizeof l); l[0] = '/'; l[599] = 0;
    CHECK(!usuario_ruta_escribible(l));
    char m[400]; strcpy(m, "/usuario/"); memset(m + 9, 'b', 380); m[389] = 0;
    CHECK(!usuario_ruta_escribible(m));                    /* no cabe: se trata como no escribible */
}
static void test_largas(void)
{
    char l[600]; memset(l, 'a', sizeof l); l[0] = '/'; l[599] = 0;
    CHECK(agent_path_protected(l));                        /* no cabe: se trata como protegida */
    char m[300]; strcpy(m, "/etc/"); memset(m + 5, 'b', 280); m[285] = 0;
    CHECK(agent_path_protected(m));
}
static void test_fuzz(void)
{
    uint64_t s = 0x9E3779B97F4A7C15ULL;
#define RND() (s ^= s << 13, s ^= s >> 7, s ^= s << 17, s)
    static const char *seg[] = { "etc", "sys", "bin", "boot", "..", ".", "", "workspace", "a", "init.sh", "tmp", "etcx" };
    for (int it = 0; it < 200000; ++it) {
        char p[200]; p[0] = 0; int n = (int)(RND() % 7);
        for (int i = 0; i < n; ++i) { if (RND() % 4 || i) strcat(p, "/"); strcat(p, seg[RND() % 12]); }
        /* referencia independiente: resolver con una pila de segmentos */
        const char *st[16]; int sp = 0; char cp[200]; strcpy(cp, p);
        for (char *t = strtok(cp, "/"); t; t = strtok(0, "/")) {
            if (!strcmp(t, ".")) continue;
            if (!strcmp(t, "..")) { if (sp) sp--; continue; }
            if (sp < 16) st[sp++] = t;
        }
        int ref = sp > 0 && (!strcmp(st[0], "etc") || !strcmp(st[0], "sys") || !strcmp(st[0], "bin") || !strcmp(st[0], "boot"));
        CHECK_EQ_INT(agent_path_protected(p), ref);
        if (soma_t_fails > 20) break;
    }
}
int main(void)
{
    test_canon(); test_protegidas(); test_largas();
    test_usuario_268(); test_fuzz();
    TEST_MAIN_END("agent_policy");
}
