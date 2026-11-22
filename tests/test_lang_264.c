/* SOMA_264_LANG: SOMA-LANG sin los limites pequenos (arrays, tablas y cadenas que crecen; mas variables, funciones y llamadas).
 * Se compila como test_lang.c:  cc ... tests/test_lang_264.c tests/stubs_common.c tests/stubs_lang.c src/lang/{lang,vm,lexer}.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "lang.h"
#include "vm.h"

static struct soma_vm g_vm;
static uint8_t *g_bc;

static int run_prog(const char *src)
{
    uint32_t bl = 0;
    host_out_reset();
    if (lang_compile(src, (uint32_t)strlen(src), g_bc, LANG_BC_MAX, &bl) != 0) return -100;
    vm_init(&g_vm);
    int rc = vm_run(&g_vm, g_bc, bl);
    vm_release(&g_vm);
    return rc;
}
#define EXPECT_OUT(src, expected) do { int _rc = run_prog(src); CHECK_EQ_INT(_rc, 0); CHECK_STR(host_out(), expected); } while (0)

static void arrays_grandes(void)
{
    EXPECT_OUT("let a=[]; let i=0; while (i<100) { a.push(i); i+=1; } print(a.len()); print(a[99]);", "100\n99\n");   /* antes: 48 */
    EXPECT_OUT("let a=[]; let i=0; while (i<100000) { a.push(i); i+=1; } let s=0; i=0; while (i<100000) { s+=a[i]; i+=1; } print(a.len()); print(s);",
               "100000\n4999950000\n");
    EXPECT_OUT("let a=[]; let i=0; while (i<5000) { a[a.len()] = i*2; i+=1; } print(a.len()); print(a[4999]); print(a.pop()); print(a.len());",
               "5000\n9998\n9998\n4999\n");
    EXPECT_OUT("let m=[]; let i=0; while (i<3000) { m.push([i, i*i]); i+=1; } print(m.len()); print(m[2999][1]);", "3000\n8994001\n");
    EXPECT_OUT("let a=[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60]; print(a.len()); print(a[59]);",
               "60\n60\n");                                                       /* literal de mas de 48 */
}

static void basura(void)
{
    /* 20 000 arrays creados y abandonados: mas que el maximo vivo a la vez (8192): el recolector los recicla */
    EXPECT_OUT("let i=0; while (i<20000) { let a=[1,2,3,4,5,6,7,8,9,10]; i+=1; } print(\"ok\");", "ok\n");
    /* 200 arrays de 5000 elementos uno tras otro: 150 MB en total, mas que el presupuesto (64 MB): debe recogerlos */
    EXPECT_OUT("let i=0; let t=0; while (i<200) { let b=[]; let j=0; while (j<5000) { b.push(j); j+=1; } t+=b.len(); i+=1; } print(t);", "1000000\n");
    EXPECT_OUT("let i=0; let n=0; while (i<3000) { let s=\"\"; let j=0; while (j<30) { s = s + \"0123456789\"; j+=1; } n+=s.len(); i+=1; } print(n);", "900000\n");
}

static void sin_sitio(void)
{
    /* un array de un millon de enteros no cabe en 64 MB: se queda en lo que cabe, avisa una vez y el programa sigue */
    int rc = run_prog("let a=[]; let i=0; while (i<1000000) { a.push(i); i+=1; } print(a.len() > 200000); print(a.len() < 1000000); print(\"sigue\");");
    CHECK_EQ_INT(rc, 0);
    CHECK(strstr(host_out(), "Sin sitio") != 0);
    CHECK(strstr(host_out(), "1\n1\nsigue\n") != 0);
    CHECK(strstr(strstr(host_out(), "Sin sitio") + 1, "Sin sitio") == 0);   /* una sola vez */
}

static void tablas_grandes(void)
{
    EXPECT_OUT("let t={}; let i=0; while (i<20000) { t[str(i)] = i*2; i+=1; } print(t.len()); print(t[\"12345\"]); print(t.has(\"19999\")); print(t.has(\"20000\")); print(t.keys().len());",
               "20000\n24690\n1\n0\n20000\n");
    EXPECT_OUT("let t={}; let i=0; while (i<5000) { t[i] = i+1; i+=1; } i=0; let s=0; while (i<5000) { s+=t[i]; i+=1; } print(s); t[7]=0; print(t[7]); print(t.len());",
               "12502500\n0\n5000\n");
    /* igualdades de siempre con indice: nil y 0 son la misma clave, como sin indice */
    EXPECT_OUT("let t={}; let i=1; while (i<100) { t[i] = i; i+=1; } t[0] = 5; print(t[0]); print(t.len());", "5\n100\n");
    EXPECT_OUT("let t={\"a\":1, \"b\":2}; t[\"a\"] = 7; print(t[\"a\"]); print(t.len());", "7\n2\n");   /* pequena: lineal, como antes */
    /* claves largas (cadenas de mas de 127) */
    EXPECT_OUT("let k=\"\"; let j=0; while (j<30) { k = k + \"xxxxxxxxxx\"; j+=1; } let t={}; let i=0; while (i<100) { t[k+str(i)] = i; i+=1; } print(t[k+\"57\"]); print(t.has(k+\"100\"));",
               "57\n0\n");
}

static void cadenas_largas(void)
{
    EXPECT_OUT("let s=\"\"; let i=0; while (i<20000) { s = s + \"abcdefghij\"; i+=1; } print(s.len()); print(s[199999]); print(s[0]);", "200000\nj\na\n");   /* antes: 16383 */
    EXPECT_OUT("let a=[]; let i=0; while (i<300) { let s=\"\"; let j=0; while (j<20) { s = s + \"0123456789\"; j+=1; } a.push(s); i+=1; } print(a.len()); print(a[299].len());",
               "300\n200\n");                                                     /* 300 cadenas largas vivas (antes 24) */
}

static char *prog_vars(int n)
{
    char *p = malloc((size_t)n * 40 + 64); size_t k = 0;
    for (int i = 0; i < n; ++i) k += (size_t)sprintf(p + k, "let v%d=%d;", i, i);
    k += (size_t)sprintf(p + k, "print(v0+v%d);", n - 1);
    return p;
}
static char *prog_fns(int n)
{
    char *p = malloc((size_t)n * 48 + 64); size_t k = 0;
    for (int i = 0; i < n; ++i) k += (size_t)sprintf(p + k, "fn f%d(x){ return x+%d; }\n", i, i);
    k += (size_t)sprintf(p + k, "print(f%d(1));", n - 1);
    return p;
}

static void compilador(void)
{
    char *p = prog_vars(900);
    EXPECT_OUT(p, "899\n");                                                       /* antes: maximo 128 variables */
    free(p);
    p = prog_fns(300);
    EXPECT_OUT(p, "300\n");                                                       /* antes: maximo 64 funciones */
    free(p);
    EXPECT_OUT("fn r(n){ if (n==0) { return 0; } return 1+r(n-1); } print(r(400));", "400\n");   /* antes: 128 llamadas */
    CHECK_EQ_INT(run_prog("fn r(n){ if (n==0) { return 0; } return 1+r(n-1); } print(r(100000));"), -1);   /* sigue habiendo limite */
    CHECK(strstr(host_out(), "marcos") != 0);
    p = prog_vars(1100);                                                          /* por encima del nuevo maximo: error claro */
    uint32_t bl = 0;
    CHECK(lang_compile(p, (uint32_t)strlen(p), g_bc, LANG_BC_MAX, &bl) != 0);
    CHECK(strstr(lang_get_last_error_msg(), "1024") != 0);
    free(p);
    /* un programa grande: 4000 lineas */
    size_t cap = 4000 * 60 + 64, k = 0; p = malloc(cap);
    k += (size_t)sprintf(p + k, "let s=0;\n");
    for (int i = 0; i < 4000; ++i) k += (size_t)sprintf(p + k, "if (s >= 0) { s = s + %d; }\n", i % 7);
    k += (size_t)sprintf(p + k, "print(s);");
    CHECK(k > 64 * 1024);
    EXPECT_OUT(p, "11994\n");
    free(p);
}

static void memoria_vm(void)
{
    /* el presupuesto se respeta y vm_release lo devuelve todo (ASan lo comprueba al salir) */
    vm_init(&g_vm);
    CHECK_EQ_INT(g_vm.mem_used, 0);
    CHECK(g_vm.mem_budget == VM_MEM_BUDGET);
    uint32_t bl = 0;
    const char *src = "let a=[]; let i=0; while (i<10000) { a.push(i); i+=1; } let t={}; t[\"x\"]=a;";
    CHECK(lang_compile(src, (uint32_t)strlen(src), g_bc, LANG_BC_MAX, &bl) == 0);
    CHECK_EQ_INT(vm_run(&g_vm, g_bc, bl), 0);
    CHECK(g_vm.mem_used >= 10000 * sizeof(struct vm_val) && g_vm.mem_used <= VM_MEM_BUDGET);
    vm_release(&g_vm);
    CHECK_EQ_INT(g_vm.mem_used, 0);
    printf("  VM: %zu KB fijos (antes 458 KB)\n", sizeof(struct soma_vm) / 1024);
}

int main(void)
{
    g_bc = malloc(LANG_BC_MAX);
    lang_init();
    arrays_grandes();
    basura();
    sin_sitio();
    tablas_grandes();
    cadenas_largas();
    compilador();
    memoria_vm();
    free(g_bc);
    TEST_MAIN_END("lang_264");
}
