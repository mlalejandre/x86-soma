/* SOMA_265_CC: el programador de SOMA escribiendo C. En el anfitrion, con un LLM falso y un "compilador de C" falso
 * (check_src/run_src) que contesta con los mensajes REALES de TinyCC (sacados de tests/test_cc_265.py: cc -s con un error):
 *  - la referencia y el ejemplo son de C y de soma.h; el codigo va en ```c; SOMA-LANG y Python no aparecen;
 *  - conio.h/SDL/getch se rechazan con una pista; un error de compilacion llega a la reparacion con su linea;
 *  - en los de consola las pruebas se ejecutan con run_src y un fallo de logica se repara; un fallo al ejecutar tambien;
 *  - las lecciones van a su propio archivo; utilidades: cc_c_error, cc_c_normalize, cc_mission_wants_c.
 * Que los ejemplos del programador compilan y funcionan con el compilador de verdad lo comprueba tests/test_cc_265.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "lang.h"
#include "vm.h"
#include "coder_core.h"

static char g_file[32768]; static uint32_t g_flen; static int g_has; static int g_lessons_c, g_lessons_otros;
static int fs_read(const char *p, char *b, uint32_t max) { if (strcmp(p, "/workspace/t.c")) return -1; if (!g_has) return -1; uint32_t n = g_flen < max ? g_flen : max; memcpy(b, g_file, n); return (int)n; }
static int fs_write(const char *p, const char *b, uint32_t len)
{
    if (!strcmp(p, CC_LESSONS_C)) { g_lessons_c++; return (int)len; }
    if (!strcmp(p, CC_LESSONS) || !strcmp(p, CC_LESSONS_PY)) { g_lessons_otros++; return -1; }   /* nunca las de los otros lenguajes */
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
/* "cc -s" falso: ERRORSINTAXIS en el codigo = error de TinyCC en esa linea (el mismo texto que da el de verdad); si no, compila.
 * "cc -x" falso: imprime la suma de las dos lineas de entrada, la resta si lleva "a - b", o se cae si lleva "*(int *)0". */
static int g_checks, g_runs;
static int fake_check(const char *src, uint32_t n, char *out, uint32_t max)
{
    (void)n; g_checks++;
    const char *p = strstr(src, "ERRORSINTAXIS");
    if (!p) { out[0] = 0; return 0; }
    int line = 1; for (const char *q = src; q < p; q++) if (*q == '\n') line++;
    snprintf(out, max, "programa.c:%d: error: ';' expected (got \"ERRORSINTAXIS\")\n", line);
    return 1;
}
static int fake_run(const char *src, uint32_t n, const char *input, char *out, uint32_t max)
{
    (void)n; g_runs++;
    if (strstr(src, "*(int *)0")) { snprintf(out, max, "\n[el programa termino: fallo de la CPU: puntero no valido, division por cero o pila agotada]"); return 1; }
    int a = 0, b = 0; sscanf(input, "%d\n%d", &a, &b);
    snprintf(out, max, "%d\n", strstr(src, "a - b") ? a - b : a + b);
    return 0;
}
static const struct cc_test SUMA_TESTS[] = { { "dos y tres", "2\n3\n", "5" }, { "negativos", "-4\n1\n", "-3" } };

static struct cc_result R;
static int run_mission(const char *mission, const char *const *replies, int with_tests)
{
    struct cc_env e; memset(&e, 0, sizeof e);
    e.llm = fake_llm; e.read = fs_read; e.write = fs_write; e.log = nolog; e.max_llm_calls = 7;
    e.lang = CC_LANG_C; e.check_src = fake_check; e.run_src = fake_run;
    if (with_tests) { e.tests = SUMA_TESTS; e.ntests = 2; }
    g_replies = replies; g_call = 0; g_has = 0; memset(g_prompts, 0, sizeof g_prompts);
    cc_run(&e, "/workspace/t.c", mission, &R);
    return R.ok;
}
static int any_prompt(const char *needle) { for (int i = 0; i < 8; i++) if (strstr(g_prompts[i], needle)) return 1; return 0; }

#define CONTADOR "```c\n#include <stdio.h>\n#include <soma.h>\nint main(void)\n{\n    if (!soma_win_create(\"Contador\", 320, 200)) return 1;\n    int n = 0; char t[32];\n" \
                 "    while (soma_win_active()) {\n        struct soma_mouse m = soma_win_mouse();\n        if (m.clicked) n++;\n        soma_win_clear(0x101828);\n" \
                 "        snprintf(t, sizeof t, \"%d\", n);\n        soma_win_text(20, 30, t, 0xFFFFFF);\n        soma_win_present();\n        soma_sleep(16);\n    }\n    return 0;\n}\n```"
#define CONIO "```c\n#include <conio.h>\n#include <stdio.h>\nint main(void)\n{\n    int n = 0;\n    while (1) { if (getch() == ' ') n++; printf(\"%d\\n\", n); }\n    return 0;\n}\n```"
#define SUMA(OP) "```c\n#include <stdio.h>\nint main(void)\n{\n    int a, b;\n    if (scanf(\"%d %d\", &a, &b) != 2) return 1;\n    printf(\"%d\\n\", " OP ");\n    return 0;\n}\n```"
#define CON_ERROR "```c\n#include <stdio.h>\nint main(void)\n{\n    int a, b;\n    scanf(\"%d %d\", &a, &b) ERRORSINTAXIS\n    printf(\"%d\\n\", a + b);\n    return 0;\n}\n```"
#define SE_CAE "```c\n#include <stdio.h>\nint main(void)\n{\n    int a, b;\n    scanf(\"%d %d\", &a, &b);\n    *(int *)0 = a;\n    printf(\"%d\\n\", a + b);\n    return 0;\n}\n```"

int main(void)
{
    /* 1. utilidades */
    CHECK(cc_mission_wants_c("un juego de la serpiente en C"));
    CHECK(cc_mission_wants_c("calculadora en lenguaje C, por favor"));
    CHECK(cc_mission_wants_c("haz hola.c que salude"));
    CHECK(cc_mission_wants_c("En C: un reloj"));
    CHECK(!cc_mission_wants_c("un juego de la serpiente en casa"));
    CHECK(!cc_mission_wants_c("un programa en C++"));
    CHECK(!cc_mission_wants_c("hazlo en Python"));
    CHECK(!cc_mission_wants_c("un juego con coches"));
    uint32_t ln = 0; char msg[96];
    CHECK(cc_c_error("programa.c:4: error: ';' expected (got \"printf\")\n", &ln, msg, sizeof msg));          /* texto real de TinyCC */
    CHECK_EQ_INT((int)ln, 4);
    CHECK(strcmp(msg, "';' expected (got \"printf\")") == 0);
    CHECK(cc_c_error("programa.c:2: warning: implicit declaration of function 'f'\ntcc: error: undefined symbol 'f'\n", &ln, msg, sizeof msg));
    CHECK_EQ_INT((int)ln, 0);
    CHECK(strcmp(msg, "undefined symbol 'f'") == 0);
    CHECK(cc_c_error("hola\n[el programa termino: fallo de la CPU: puntero no valido, division por cero o pila agotada]", &ln, msg, sizeof msg));
    CHECK(strstr(msg, "fallo de la CPU") != 0);
    CHECK(!cc_c_error("programa.c:3: warning: assignment discards qualifiers\n", &ln, msg, sizeof msg));    /* un aviso no es un error */
    char src[512]; strcpy(src, "int main(void)\n{\n\tprintf(\xE2\x80\x9Chola %d\\n\xE2\x80\x9D, (int)sqrt(16.0));\r\n\tchar *p = malloc(4); free(p);\n\treturn 0;\n}\n");
    uint32_t sl = (uint32_t)strlen(src);
    CHECK(cc_c_normalize(src, &sl, sizeof src) >= 5);
    CHECK(strncmp(src, "#include <stdio.h>\n#include <stdlib.h>\n#include <math.h>\n", 57) == 0);
    CHECK(strstr(src, "    printf(\"hola %d\\n\", (int)sqrt(16.0));\n") != 0);
    CHECK(strstr(src, "\r") == 0 && sl == strlen(src));
    strcpy(src, "#include <stdio.h>\nint main(void) { printf(\"x\"); return 0; }\n"); sl = (uint32_t)strlen(src);
    CHECK_EQ_INT(cc_c_normalize(src, &sl, sizeof src), 0);                    /* ya esta: no se toca */
    strcpy(src, "int fabsx = 1; /* usa fabs( */ int main(void) { return fabsx; }\n"); sl = (uint32_t)strlen(src);
    CHECK(cc_c_normalize(src, &sl, sizeof src) == 1 && strncmp(src, "#include <math.h>\n", 18) == 0);   /* solo math (por fabs), no stdlib */

    /* 2. ventana: la referencia es la de C y soma.h, el bloque ```c, y se acepta a la primera */
    static const char *const r_ok[] = { CONTADOR, 0 };
    CHECK(run_mission("contador con un boton en una ventana", r_ok, 0));
    CHECK_EQ_INT(R.repairs, 0);
    CHECK(strstr(g_prompts[0], "#include <soma.h>") != 0);
    CHECK(strstr(g_prompts[0], "soma_win_present") != 0);
    CHECK(strstr(g_prompts[0], "```c\n") != 0);
    CHECK(strstr(g_prompts[0], "REFERENCIA SOMA-LANG") == 0);
    CHECK(strstr(g_prompts[0], "from soma import win") == 0);
    CHECK(strstr(g_file, "soma_win_present()") != 0 && strstr(g_file, "```") == 0);
    CHECK_EQ_INT(g_runs, 0);                                                    /* un programa con ventana no se ejecuta en las pruebas */

    /* 3. conio.h/getch -> se rechaza con su pista y se repara */
    static const char *const r_cn[] = { CONIO, CONTADOR, 0 };
    CHECK(run_mission("contador grafico con el raton", r_cn, 0));
    CHECK_EQ_INT(R.repairs, 1);
    CHECK(any_prompt("conio.h', que no existe en SOMA"));
    CHECK(strstr(g_file, "conio") == 0);

    /* 4. error de compilacion -> la reparacion recibe la linea y el texto de TinyCC; despues pasa las pruebas */
    g_runs = 0; g_lessons_c = g_lessons_otros = 0;
    static const char *const r_syn[] = { CON_ERROR, SUMA("a + b"), 0 };
    CHECK(run_mission("programa de consola que pida dos numeros y escriba su suma", r_syn, 1));
    CHECK(any_prompt("Error del compilador de C en la linea 5"));
    CHECK(any_prompt("';' expected"));
    CHECK(any_prompt(">> 5: "));                                                /* el contexto numerado marca la linea */
    CHECK_EQ_INT(R.tests_pass, 2);
    CHECK(g_runs >= 2);
    CHECK(g_lessons_c >= 1);                                                    /* la reparacion se aprende, en su archivo */
    CHECK_EQ_INT(g_lessons_otros, 0);

    /* 5. error de logica -> las pruebas lo detectan (salida -1 en vez de 5) y se repara */
    static const char *const r_log[] = { SUMA("a - b"), SUMA("a + b"), 0 };
    CHECK(run_mission("programa de consola que pida dos numeros y escriba su suma", r_log, 1));
    CHECK_EQ_INT(R.tests_first_fail, 2);
    CHECK_EQ_INT(R.tests_pass, 2);
    CHECK(any_prompt("Salida esperada: \"5\""));
    CHECK(strstr(g_file, "a + b") != 0);

    /* 6. el programa se cae al ejecutarlo -> el fallo llega a la reparacion con su pista */
    static const char *const r_cae[] = { SE_CAE, SUMA("a + b"), 0 };
    CHECK(run_mission("programa de consola que pida dos numeros y escriba su suma", r_cae, 1));
    CHECK(any_prompt("fallo de la CPU"));
    CHECK(any_prompt("punteros (NULL"));
    CHECK_EQ_INT(R.tests_pass, 2);

    /* 7. nunca se entrega un programa que no compila */
    static const char *const r_bad[] = { CON_ERROR, CON_ERROR, CON_ERROR, CON_ERROR, CON_ERROR, CON_ERROR, CON_ERROR, 0 };
    g_has = 0;
    CHECK(!run_mission("programa de consola que pida dos numeros y escriba su suma", r_bad, 1));
    CHECK(!R.wrote);
    CHECK(g_checks > 0);

    /* 8. los otros lenguajes no cambian: el de Python no ve nada de C */
    {
        struct cc_env e; memset(&e, 0, sizeof e);
        static const char *const r_py[] = { "```python\nprint(1)\n```", 0 };
        e.llm = fake_llm; e.read = fs_read; e.write = fs_write; e.log = nolog; e.max_llm_calls = 3;
        e.lang = CC_LANG_PYTHON; e.check_src = fake_check; e.run_src = fake_run;
        g_replies = r_py; g_call = 0; g_has = 0; memset(g_prompts, 0, sizeof g_prompts);
        cc_run(&e, "/workspace/t.c", "programa que escriba 1", &R);
        CHECK(strstr(g_prompts[0], "soma_win_") == 0 && strstr(g_prompts[0], "```python") != 0);
        CHECK(strstr(g_file, "#include") == 0);                                 /* el normalizador de C no se aplica a Python */
    }

    TEST_MAIN_END("programador_c_265");
}
