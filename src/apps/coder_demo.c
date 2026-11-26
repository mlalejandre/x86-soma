#include <stdint.h>
#include "console.h"
#include "coder_core.h"
#include "coder_demo.h"
#include "fs.h"
#include "llm.h"
#include "lang.h"
#include "vm.h"
#include "mem.h"        /* SOMA_323_BSS: kmalloc */

static uint32_t sl(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static int has(const char *h, const char *n) { uint32_t ln = sl(n); if (!ln) return 1; for (; *h; h++) { uint32_t i = 0; while (i < ln && h[i] == n[i]) i++; if (i == ln) return 1; } return 0; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* ---------------------------------------------------------------- huella del registro */
static uint32_t g_hash;
static void hmix(const char *s) { for (; *s; s++) { g_hash ^= (uint8_t)*s; g_hash *= 16777619u; } g_hash ^= '\n'; g_hash *= 16777619u; }

/* ---------------------------------------------------------------- "modelo" guionizado */
#define FIB_HEAD "let n = to_int(input(\"\"));\nlet a = 0;\nlet b = 1;\nlet i = 1;\n"
#define FIB_BODY "    print(to_str(a));\n    let t = a + b;\n    a = b;\n    b = t;\n    i = i + 1;\n}\n"
#define FIB_BUG   "```soma\n" FIB_HEAD "while (i < n) {\n" FIB_BODY "```"
#define FIB_OK    "```soma\n" FIB_HEAD "while (i <= n) {\n" FIB_BODY "```"
#define FIB_LOOP  "```soma\n" FIB_HEAD "while (i <= n) {\n    i = i + 0;\n}\n```"
#define SUM_HEAD  "let n = to_int(input(\"\"));\nlet total = 0;\nlet k = 1;\n"
#define SUM_BUG   "```soma\n" SUM_HEAD "while (k < n) {\n    total = total + k;\n    k = k + 1;\n}\nprint(to_str(total));\n```"
#define SUM_OK    "```soma\n" SUM_HEAD "while (k <= n) {\n    total = total + k;\n    k = k + 1;\n}\nprint(to_str(total));\n```"

static const struct cc_test FIB_TESTS[] = {
    { "un termino", "1", "0" },
    { "cinco",      "5", "0\n1\n1\n2\n3" },
    { "ocho",       "8", "0\n1\n1\n2\n3\n5\n8\n13" },
};
static const struct cc_test SUM_TESTS[] = {
    { "uno",   "1",   "1" },
    { "diez",  "10",  "55" },
    { "cien",  "100", "5050" },
};

static int g_scn;       /* escenario en curso */
static int g_feedback;  /* respuestas que usaron el detalle de las pruebas */
static int demo_llm(const char *prompt, char *out, uint32_t max, uint32_t timeout)
{
    (void)timeout;
    const char *r;
    if (has(prompt, "revisor")) r = "OK";
    else {
        int fb = has(prompt, "FALLA") && has(prompt, "Salida esperada") && has(prompt, "Salida obtenida");
        switch (g_scn) {
        case 0:  r = fb ? (g_feedback++, FIB_OK) : FIB_BUG; break;                                  /* error de logica: un termino de menos */
        case 1:  r = (has(prompt, "FALLA") && has(prompt, "no termina")) ? (g_feedback++, FIB_OK) : FIB_LOOP; break;   /* bucle infinito */
        default: r = SUM_BUG; break;                                                                /* nunca lo arregla */
        }
    }
    uint32_t i = 0; while (r[i] && i + 1 < max) { out[i] = r[i]; i++; } out[i] = 0;
    return 1;
}

/* ---------------------------------------------------------------- ejecucion de pruebas (captura de la salida del hilo actual) */
/* SOMA_323_BSS: la maquina virtual de la demo (627 KiB) se reserva al ejecutar y se libera despues; antes ocupaba memoria siempre */
static int demo_run(const uint8_t *bc, uint32_t len, const char *input, char *out, uint32_t max)
{
    struct soma_vm *vm = (struct soma_vm *)kmalloc(sizeof *vm);
    if (!vm) { if (out && max) { const char *m = "sin memoria para la maquina virtual"; uint32_t i = 0; while (m[i] && i + 1 < max) { out[i] = m[i]; i++; } out[i] = 0; } return -1; }
    uint64_t old = vm_step_limit; vm_step_limit = 1500000;                 /* un bucle infinito se detecta en una fraccion de segundo */
    console_capture_begin(out, max, input);
    vm_init(vm);
    int rc = vm_run(vm, bc, len);
    vm_release(vm);
    console_capture_end();
    vm_step_limit = old;
    kfree(vm);
    return rc;
}

/* ---------------------------------------------------------------- entorno de archivos: lecciones en memoria para que cada ejecucion sea identica */
static char g_les[2048]; static uint32_t g_les_len;
static int demo_read(const char *p, char *b, uint32_t max)
{
    if (seq(p, CC_LESSONS)) return -1;                                      /* sin lecciones previas: el contexto es siempre el mismo */
    return vfs_read(p, b, max);
}
static int demo_write(const char *p, const char *b, uint32_t len)
{
    if (seq(p, CC_LESSONS)) { g_les_len = len < sizeof g_les ? len : sizeof g_les; for (uint32_t i = 0; i < g_les_len; i++) g_les[i] = b[i]; return (int)len; }
    return vfs_write(p, b, len);
}
static void demo_log(const char *m) { kprint("    | "); kprint(m); kprint("\n"); hmix(m); }
static int demo_never(void) { return 0; }

struct scn { const char *titulo, *mision, *archivo; const struct cc_test *tests; int n, esperado_ok; };
static const struct scn SCN[] = {
    { "ERROR DE LOGICA: compila, pero imprime un termino de menos",
      "Lee un numero N con input() e imprime los N primeros terminos de Fibonacci, uno por linea", "/workspace/demo_fib.soma", FIB_TESTS, 3, 1 },
    { "BUCLE INFINITO: compila, pero no termina",
      "Lee un numero N con input() e imprime los N primeros terminos de Fibonacci, uno por linea", "/workspace/demo_fib2.soma", FIB_TESTS, 3, 1 },
    { "MODELO QUE NO SE CORRIGE: debe negarse a entregar un programa incorrecto",
      "Lee un numero N con input() e imprime la suma de 1 a N", "/workspace/demo_suma.soma", SUM_TESTS, 3, 0 },
};

int coder_demo_run(int use_real_llm)
{
    kprint("\n[DEMO-IA] Ciclo escribir -> probar -> detectar -> corregir ");
    kprint(use_real_llm ? "(LLM real)\n" : "(modelo guionizado: reproducible, sin red)\n");
    g_hash = 2166136261u; int good = 0, total = (int)(sizeof SCN / sizeof SCN[0]);
    for (int s = 0; s < total; s++) {
        const struct scn *c = &SCN[s]; struct cc_result r;
        g_scn = s; g_feedback = 0; g_les_len = 0;
        vfs_delete(c->archivo);                                              /* cada ejecucion parte igual */
        kprint("\n[DEMO-IA] Escenario "); kprint_dec((uint32_t)(s + 1)); kprint("/"); kprint_dec((uint32_t)total); kprint(": "); kprint(c->titulo); kprint("\n");
        kprint("  Encargo: "); kprint(c->mision); kprint("\n  Pruebas: ");
        for (int i = 0; i < c->n; i++) { kprint(c->tests[i].name); kprint(i + 1 < c->n ? ", " : "\n"); }
        struct cc_env env = { use_real_llm ? llm_chat : demo_llm, demo_read, demo_write, demo_log, 0, demo_never, 7, 0, 1, demo_run, c->tests, c->n, CC_LANG_SOMA, 0, 0 };
        cc_run(&env, c->archivo, c->mision, &r);
        hmix(r.summary);
        kprint("  Resultado: "); kprint(r.ok ? "ENTREGADO" : "NO ENTREGADO"); kprint(" | pruebas "); kprint_dec((uint32_t)r.tests_pass); kprint("/"); kprint_dec((uint32_t)r.tests_total);
        kprint(" | fallaban en la 1a version: "); kprint_dec((uint32_t)r.tests_first_fail); kprint(" | reparaciones: "); kprint_dec((uint32_t)r.repairs);
        kprint(" | llamadas al modelo: "); kprint_dec((uint32_t)r.llm_calls); kprint("\n");
        int ok = (r.ok == c->esperado_ok);
        if (c->esperado_ok) { ok = ok && r.tests_pass == r.tests_total && vfs_size(c->archivo) > 0; if (!use_real_llm) ok = ok && r.tests_first_fail > 0 && r.repairs >= 1 && g_feedback >= 1; }
        else ok = ok && vfs_size(c->archivo) < 0;                            /* un programa incorrecto no llega a escribirse */
        kprint(ok ? "  [OK] comportamiento esperado\n" : "  [FALLO] comportamiento inesperado\n");
        good += ok;
    }
    kprint("\n[DEMO-IA] RESULTADO: "); kprint_dec((uint32_t)good); kprint("/"); kprint_dec((uint32_t)total); kprint(" escenarios correctos\n");
    if (!use_real_llm) { kprint("[DEMO-IA] huella del registro: 0x"); kprint_hex32(g_hash); kprint(" (identica en cada ejecucion)\n"); }
    return good == total;
}
