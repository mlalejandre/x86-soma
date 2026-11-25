/* SOMA_307_SEGURIDAD: la lista de archivos que la IA no puede parchear desde dentro del SO (src/agent/agent_protegidos.h).
 * Que sea la misma que la del anfitrion lo comprueba tools/audit.py --strict (make ci). */
#include <stdio.h>
#include <string.h>
#include "soma_test.h"
#include "agent_protegidos.h"

int main(void)
{
    /* Lo que fallaba: la ruta llega con su carpeta ("FILE: src/core/thread.c") y antes no casaba con "thread.c" */
    CHECK(agent_ruta_protegida("core/thread.c"));
    CHECK(agent_ruta_protegida("src/core/thread.c"));
    CHECK(agent_ruta_protegida("  /src/arch/x86_64/idt.c\n<<<<<<< SEARCH\nxxx"));
    CHECK(agent_ruta_protegida("agent/agent.c\r\n"));
    CHECK(agent_ruta_protegida("net/tcp_policy.h \"resto\""));
    CHECK(agent_ruta_protegida("agent/agent_protegidos.h"));      /* la propia lista */
    CHECK(agent_ruta_protegida("core/pila_canario.h"));           /* el canario del 306 */
    CHECK(agent_ruta_protegida("agent/lcm_soma.inc"));

    /* Lo que se puede parchear */
    CHECK(!agent_ruta_protegida("apps/notepad.c"));
    CHECK(!agent_ruta_protegida("ui/gui_cosmos.c"));
    CHECK(!agent_ruta_protegida("core/mythread.c"));               /* el nombre entero, no un trozo */
    CHECK(!agent_ruta_protegida("core/thread.c.bak"));
    CHECK(!agent_ruta_protegida("core/thread.cx"));
    CHECK(!agent_ruta_protegida(""));
    CHECK(!agent_ruta_protegida(0));

    /* Cada nombre de la lista, en cualquier carpeta */
    int n = 0;
    for (uint32_t i = 0; agent_protegidos[i]; ++i, ++n) {
        char r[200];
        snprintf(r, sizeof r, "src/una/carpeta/%s\n", agent_protegidos[i]);
        CHECK(agent_ruta_protegida(r));
        snprintf(r, sizeof r, "x%s", agent_protegidos[i]);
        CHECK(!agent_ruta_protegida(r));
    }
    CHECK(n >= 78);

    /* La ruta que se extrae del texto del parche */
    char out[32];
    CHECK_EQ_INT(agent_ruta_parche(" src/apps/x.c resto", out, sizeof out), 8);
    CHECK_STR(out, "apps/x.c");
    CHECK_EQ_INT(agent_ruta_parche("/workspace/notas.txt\n", out, sizeof out), 19);
    CHECK_STR(out, "workspace/notas.txt");
    CHECK_EQ_INT(agent_ruta_parche("una/ruta/muy/larga/que/no/cabe/en/el/buffer.c", out, sizeof out), 31);   /* se corta con terminador */
    CHECK_EQ_INT((int)strlen(out), 31);
    CHECK_EQ_INT(agent_ruta_parche("a.c", out, 1), 0);
    CHECK_STR(out, "");
    CHECK_EQ_INT(agent_ruta_parche(0, out, sizeof out), 0);
    CHECK_STR(agent_nombre_de("a/b/c.h"), "c.h");
    CHECK_STR(agent_nombre_de("c.h"), "c.h");
    CHECK(agent_ruta_termina("apps/x.c", ".c"));
    CHECK(!agent_ruta_termina("apps/x.cc", ".c"));
    CHECK(!agent_ruta_termina("c", ".c"));
    CHECK(agent_ruta_termina("notas.txt", ".txt"));

    TEST_MAIN_END("agent_protegidos");
}
