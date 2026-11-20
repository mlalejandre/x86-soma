/* SOMA_239_PYTHON_GRAFICO: el programador de SOMA escribiendo Python. En el anfitrion, con un LLM falso y un "Python" falso
 * (check_src/run_src) que reproduce lo que hace el de verdad (python -s / -x en un proceso de SOMA):
 *  - la referencia y el ejemplo son de Python y de soma.win; el codigo va en ```python;
 *  - pygame/tkinter se rechazan con una pista; un error de sintaxis llega a la reparacion con su linea;
 *  - en los de consola las pruebas se ejecutan con run_src y un fallo de logica se repara;
 *  - las lecciones van a su propio archivo; utilidades: cc_py_error, cc_py_normalize, cc_mission_wants_python. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "lang.h"
#include "vm.h"
#include "coder_core.h"

static char g_file[32768]; static uint32_t g_flen; static int g_has; static int g_lessons_py;
static int fs_read(const char *p, char *b, uint32_t max) { if (strcmp(p, "/workspace/t.py")) return -1; if (!g_has) return -1; uint32_t n = g_flen < max ? g_flen : max; memcpy(b, g_file, n); return (int)n; }
static int fs_write(const char *p, const char *b, uint32_t len)
{
    if (!strcmp(p, CC_LESSONS_PY)) { g_lessons_py++; return (int)len; }
    if (!strcmp(p, CC_LESSONS)) return -1;                                         /* nunca las de SOMA-LANG */
    memcpy(g_file, b, len); g_flen = len; g_file[len] = 0; g_has = 1; return (int)len;
}
static void nolog(const char *m) { (void)m; }

static const char *const *g_replies; static int g_call;
static char g_prompts[8][24576];
static int fake_llm(const char *prompt, char *out, uint32_t max, uint32_t to)
{
    (void)to;
    if (g_call < 8) snprintf(g_prompts[g_call], sizeof g_prompts[0], "%s", prompt);
    const char *r = strstr(prompt, "revisor") ? "OK" : g_replies[g_call] ? g_replies[g_call] : "OK";
    if (!strstr(prompt, "revisor")) g_call++;
    snprintf(out, max, "%s", r);
    return 1;
}
/* "Python" falso: SINTAXIS en el codigo = SyntaxError en esa linea; si no, compila. Al ejecutar: imprime la suma de las dos lineas
 * de entrada, o la resta si el programa lleva el error de logica marcado con "- b". */
static int g_checks, g_runs;
static int fake_check(const char *src, uint32_t n, char *out, uint32_t max)
{
    (void)n; g_checks++;
    const char *p = strstr(src, "SINTAXIS");
    if (!p) { snprintf(out, max, "OK\n"); return 0; }
    int line = 1; for (const char *q = src; q < p; q++) if (*q == '\n') line++;
    snprintf(out, max, "Traceback (most recent call last):\n  File \"programa.py\", line %d\nSyntaxError: invalid syntax\n", line);
    return 1;
}
static int fake_run(const char *src, uint32_t n, const char *input, char *out, uint32_t max)
{
    (void)n; g_runs++;
    int a = 0, b = 0; sscanf(input, "%d\n%d", &a, &b);
    snprintf(out, max, "%d\n", strstr(src, "- b") ? a - b : a + b);
    return 0;
}
static const struct cc_test SUMA_TESTS[] = { { "dos y tres", "2\n3\n", "5" }, { "negativos", "-4\n1\n", "-3" } };

static struct cc_result R;
static int run_mission(const char *mission, const char *const *replies, int with_tests)
{
    struct cc_env e; memset(&e, 0, sizeof e);
    e.llm = fake_llm; e.read = fs_read; e.write = fs_write; e.log = nolog; e.max_llm_calls = 7;
    e.lang = CC_LANG_PYTHON; e.check_src = fake_check; e.run_src = fake_run;
    if (with_tests) { e.tests = SUMA_TESTS; e.ntests = 2; }
    g_replies = replies; g_call = 0; g_has = 0; memset(g_prompts, 0, sizeof g_prompts);
    cc_run(&e, "/workspace/t.py", mission, &R);
    return R.ok;
}
static int any_prompt(const char *needle) { for (int i = 0; i < 8; i++) if (strstr(g_prompts[i], needle)) return 1; return 0; }

#define CONTADOR "```python\nfrom soma import win\nimport time\nwin.create(\"Contador\", 320, 200)\nn = 0\nwhile win.active():\n    m = win.mouse()\n    if m.clicked:\n        n += 1\n    win.clear(0x101828)\n    win.text(20, 30, str(n), 0xFFFFFF)\n    win.present()\n    time.sleep_ms(16)\n```"
#define PYGAME "```python\nimport pygame\npygame.init()\npantalla = pygame.display.set_mode((320, 200))\nwhile True:\n    pygame.display.flip()\n```"
#define SUMA(OP) "```python\na = int(input())\nb = int(input())\nprint(a " OP " b)\n```"
#define CON_ERROR "```python\na = int(input())\nb = int(input())\nprint(a + b SINTAXIS)\n```"

int main(void)
{
    /* 1. utilidades */
    CHECK(cc_mission_wants_python("un juego de la serpiente en Python"));
    CHECK(cc_mission_wants_python("calculadora usando python"));
    CHECK(!cc_mission_wants_python("un juego de la serpiente"));
    uint32_t ln = 0; char msg[96];
    CHECK(cc_py_error("Traceback (most recent call last):\n  File \"programa.py\", line 12, in <module>\nNameError: name 'x' isn't defined\n", &ln, msg, sizeof msg));
    CHECK_EQ_INT((int)ln, 12);
    CHECK(strcmp(msg, "NameError: name 'x' isn't defined") == 0);
    char src[256]; strcpy(src, ">>> if x:\n\tprint(\xE2\x80\x9Chola\xE2\x80\x9D)\r\n"); uint32_t sl = (uint32_t)strlen(src);
    CHECK(cc_py_normalize(src, &sl, sizeof src) >= 3);
    CHECK(strcmp(src, "if x:\n    print(\"hola\")\n") == 0);

    /* 2. ventana: la referencia es la de Python y soma.win, el bloque ```python, y se acepta a la primera */
    static const char *const r_ok[] = { CONTADOR, 0 };
    CHECK(run_mission("contador con un boton en una ventana", r_ok, 0));
    CHECK_EQ_INT(R.repairs, 0);
    CHECK(strstr(g_prompts[0], "from soma import win") != 0);
    CHECK(strstr(g_prompts[0], "```python") != 0);
    CHECK(strstr(g_prompts[0], "REFERENCIA SOMA-LANG") == 0);
    CHECK(strstr(g_file, "win.present()") != 0 && strstr(g_file, "```") == 0);
    CHECK_EQ_INT(g_runs, 0);                                                    /* un programa con ventana no se ejecuta en las pruebas */

    /* 3. pygame -> se rechaza con su pista y se repara */
    static const char *const r_pg[] = { PYGAME, CONTADOR, 0 };
    CHECK(run_mission("contador grafico con el raton", r_pg, 0));
    CHECK_EQ_INT(R.repairs, 1);
    CHECK(any_prompt("import pygame', que no existe en SOMA"));
    CHECK(strstr(g_file, "pygame") == 0);

    /* 4. error de sintaxis -> la reparacion recibe la linea de Python; despues pasa las pruebas */
    g_runs = 0; g_lessons_py = 0;
    static const char *const r_syn[] = { CON_ERROR, SUMA("+"), 0 };
    CHECK(run_mission("programa de consola que pida dos numeros y escriba su suma", r_syn, 1));
    CHECK(any_prompt("Error de Python en la linea 3"));
    CHECK(any_prompt("SyntaxError"));
    CHECK_EQ_INT(R.tests_pass, 2);
    CHECK(g_runs >= 2);
    CHECK(g_lessons_py >= 1);                                                   /* la reparacion se aprende, en su archivo */

    /* 5. error de logica -> las pruebas lo detectan (salida -1 en vez de 5) y se repara */
    static const char *const r_log[] = { SUMA("- b +"), SUMA("+"), 0 };
    CHECK(run_mission("programa de consola que pida dos numeros y escriba su suma", r_log, 1));
    CHECK_EQ_INT(R.tests_first_fail, 2);
    CHECK_EQ_INT(R.tests_pass, 2);
    CHECK(any_prompt("Salida esperada: \"5\""));
    CHECK(strstr(g_file, "print(a + b)") != 0);

    /* 6. nunca se entrega un programa que no compila */
    static const char *const r_bad[] = { CON_ERROR, CON_ERROR, CON_ERROR, CON_ERROR, CON_ERROR, CON_ERROR, CON_ERROR, 0 };
    g_has = 0;
    CHECK(!run_mission("programa de consola que pida dos numeros y escriba su suma", r_bad, 1));
    CHECK(!R.wrote);
    CHECK(g_checks > 0);

    TEST_MAIN_END("programador_python_239");
}
