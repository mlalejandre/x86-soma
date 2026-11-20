/* SOMA_235_PROGRAMADOR: lo que se encontro probando el programador de SOMA con un modelo de verdad (Claude haciendo de servidor
 * local por la red de QEMU) y que ahora se comprueba en el anfitrion con un LLM falso:
 *  - "calculadora que pida dos numeros" es de consola (antes se le exigia una ventana y se rechazaba el programa correcto);
 *  - las flechas en SOMA son 16/14/2/6: un juego con 37-40 compila pero no responde a las flechas -> se detecta y se repara;
 *  - un reloj con sys.time() muestra el tiempo desde el arranque, no la hora -> se detecta; sys.hour() y compania existen;
 *  - un bloque de Python ("if n == 0:") recibe una pista concreta en la reparacion. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "lang.h"
#include "vm.h"
#include "coder_core.h"

static char g_file[32768]; static uint32_t g_flen; static int g_has;
static int fs_read(const char *p, char *b, uint32_t max) { (void)p; if (!g_has) return -1; uint32_t n = g_flen < max ? g_flen : max; memcpy(b, g_file, n); return (int)n; }
static int fs_write(const char *p, const char *b, uint32_t len) { if (!strcmp(p, CC_LESSONS)) return (int)len; memcpy(g_file, b, len); g_flen = len; g_file[len] = 0; g_has = 1; return (int)len; }
static void nolog(const char *m) { (void)m; }

static const char *const *g_replies; static int g_call;
static char g_prompts[8][24576];
static int fake_llm(const char *prompt, char *out, uint32_t max, uint32_t to)
{
    (void)to;
    if (g_call < 8) snprintf(g_prompts[g_call], sizeof g_prompts[0], "%s", prompt);
    const char *r = strstr(prompt, "revisor") ? "OK" : g_replies[g_call] ? g_replies[g_call] : "OK";
    if (!strstr(prompt, "revisor")) g_call++; else g_call += 0;
    snprintf(out, max, "%s", r);
    return 1;
}
static struct cc_result R;
static int run_mission(const char *mission, const char *const *replies)
{
    struct cc_env e; memset(&e, 0, sizeof e);
    e.llm = fake_llm; e.read = fs_read; e.write = fs_write; e.log = nolog; e.max_llm_calls = 7;
    g_replies = replies; g_call = 0; g_has = 0; memset(g_prompts, 0, sizeof g_prompts);
    cc_run(&e, "/workspace/t.soma", mission, &R);
    return R.ok;
}
static int any_prompt(const char *needle) { for (int i = 0; i < 8; i++) if (strstr(g_prompts[i], needle)) return 1; return 0; }

#define CALC_CONSOLA "```soma\nlet a = to_int(input(\"Primer numero: \"));\nlet b = to_int(input(\"Segundo numero: \"));\nprint(\"Suma: \" + to_str(a + b));\nif (b == 0) { print(\"Division: entre cero no\"); } else { print(\"Division: \" + to_str(a / b)); }\n```"
#define SNAKE(U, D, L, R) "```soma\nwin.create(\"Serpiente\", 50, 50, 300, 200);\nlet x = 10;\nlet y = 10;\nwhile (win.active()) {\n    let k = win.key();\n    if (k == " U ") { y = y - 1; }\n    if (k == " D ") { y = y + 1; }\n    if (k == " L ") { x = x - 1; }\n    if (k == " R ") { x = x + 1; }\n    win.clear(0x000000);\n    win.rect(x * 10, y * 10, 10, 10, 0x22C55E);\n    win.present();\n    sys.sleep(30);\n}\n```"
#define RELOJ(H, M, S) "```soma\nwin.create(\"Reloj\", 50, 50, 240, 100);\nwhile (win.active()) {\n    win.clear(0x000000);\n    win.text(40, 40, to_str(" H ") + \":\" + to_str(" M ") + \":\" + to_str(" S "), 0xFFFFFF);\n    win.present();\n    sys.sleep(200);\n}\n```"

int main(void)
{
    /* 1. clasificador */
    CHECK_EQ_INT(cc_mission_is_gui("calculadora que pida dos numeros y muestre su suma"), 0);
    CHECK_EQ_INT(cc_mission_is_gui("calculadora con botones"), 1);
    CHECK_EQ_INT(cc_mission_is_gui("calculadora"), 1);
    CHECK_EQ_INT(cc_mission_is_gui("muestra en pantalla los numeros primos"), 1);
    CHECK_EQ_INT(cc_mission_is_gui("muestra en la consola los numeros primos de la pantalla"), 0);
    CHECK_EQ_INT(cc_mission_is_gui("juego de la serpiente que se mueve con las flechas"), 1);
    CHECK_EQ_INT(cc_mission_is_gui("ventana que pida un nombre"), 1);
    CHECK_EQ_INT(cc_mission_is_gui("tabla de multiplicar del numero que escriba el usuario"), 0);

    /* 2. calculadora de consola: aceptada a la primera */
    static const char *const r_calc[] = { CALC_CONSOLA, 0 };
    CHECK(run_mission("calculadora que pida dos numeros y muestre su suma, resta, multiplicacion y division", r_calc));
    CHECK_EQ_INT(R.repairs, 0);
    CHECK(strstr(g_file, "input(") != 0);

    /* 3. flechas con codigos de JavaScript -> se detecta, se pide 16/14/2/6, se repara */
    static const char *const r_snake[] = { SNAKE("38", "40", "37", "39"), SNAKE("16", "14", "2", "6"), 0 };
    CHECK(run_mission("juego de la serpiente que se mueve con las flechas del teclado", r_snake));
    CHECK_EQ_INT(R.repairs, 1);
    CHECK(any_prompt("arriba 16, abajo 14"));
    CHECK(strstr(g_file, "k == 16") != 0);
    CHECK(strstr(g_prompts[0], "Flechas: arriba 16") != 0);          /* y la referencia ya lo dice desde el principio */

    /* 4. reloj con el tiempo desde el arranque -> se pide sys.hour() */
    static const char *const r_reloj[] = { RELOJ("sys.time() / 3600000", "sys.time() / 60000 % 60", "sys.time() / 1000 % 60"),
                                           RELOJ("sys.hour()", "sys.minute()", "sys.second()"), 0 };
    CHECK(run_mission("reloj que muestre la hora actual en una ventana", r_reloj));
    CHECK_EQ_INT(R.repairs, 1);
    CHECK(any_prompt("usa sys.hour()"));
    CHECK(strstr(g_file, "sys.hour()") != 0);
    static const char *const r_crono[] = { RELOJ("sys.millis() / 60000", "sys.millis() / 1000 % 60", "sys.millis() % 1000"), 0 };
    CHECK(run_mission("cronometro en una ventana con los milisegundos desde que arranca", r_crono));
    CHECK_EQ_INT(R.repairs, 0);                                      /* un cronometro si usa sys.millis() */

    /* 5. bloque de Python: pista concreta */
    static const char *const r_py[] = { "```soma\nlet n = to_int(input(\"n: \"));\nif n == 0:\n    print(\"cero\")\n```",
                                        "```soma\nlet n = to_int(input(\"n: \"));\nif (n == 0) { print(\"cero\"); }\n```", 0 };
    CHECK(run_mission("di si el numero que escriba el usuario es cero", r_py));
    CHECK(any_prompt("bloque de Python"));

    /* 6. sys.hour() ... sys.year() compilan y devuelven el reloj del equipo */
    static uint8_t bc[4096]; uint32_t bl = 0; static struct soma_vm vm;
    const char *src = "print(to_str(sys.hour()) + \":\" + to_str(sys.minute()) + \":\" + to_str(sys.second()) + \" \" + to_str(sys.day()) + \"/\" + to_str(sys.month()) + \"/\" + to_str(sys.year()));";
    CHECK_EQ_INT(lang_compile(src, (uint32_t)strlen(src), bc, sizeof bc, &bl), 0);
    host_out_reset(); vm_init(&vm); vm_run(&vm, bc, bl); vm_release(&vm);
    CHECK(strstr(host_out(), "13:45:7 20/11/2026") != 0);
    const char *bad = "print(sys.hora());";
    CHECK(lang_compile(bad, (uint32_t)strlen(bad), bc, sizeof bc, &bl) != 0);     /* lo inventado sigue sin compilar */

    TEST_MAIN_END("programador_235");
}
