/* SOMA_343_DESCARGAS: el planificador de corrutinas de descarga (src/user/web_dl.c) en el anfitrion (Linux/macOS x86_64).
 * Sin ASan: ASan no admite cambios de pila sin anotaciones. Con el mismo cambio de pila que el navegador (soma_co_switch).
 *   cc -O1 -Isrc/apps tests/test_web_dl_343.c src/user/web_dl.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "web_dl.h"

#if defined(__x86_64__)
void *kmalloc(size_t n) { return malloc(n); }
char *g_co_lo, *g_co_hi;
void soma_co_switch(void **save_sp, void *new_sp);
#if defined(__APPLE__)
#define SYM(x) "_" #x
#else
#define SYM(x) #x
#endif
__asm__(".text\n.globl " SYM(soma_co_switch) "\n" SYM(soma_co_switch) ":\n"
        "  pushq %rbp\n  pushq %rbx\n  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n"
        "  movq %rsp, (%rdi)\n  movq %rsi, %rsp\n"
        "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n  popq %rbx\n  popq %rbp\n  ret\n");

static int checks, fails;
#define CK(c, m) do { checks++; if (!(c)) { fails++; printf("FALLO: %s (linea %d)\n", m, __LINE__); } } while (0)

struct job { int id, steps, done, seen_inside, nested_start; char *lo_seen; long trace[64]; int nt; };
static long reloj;
static void trabajo(void *a)
{
    struct job *j = (struct job *)a;
    char local[256]; memset(local, j->id, sizeof local);             /* algo de pila propia que debe sobrevivir a las cesiones */
    for (int i = 0; i < j->steps; i++) {
        j->seen_inside += wdl_inside();
        j->lo_seen = g_co_lo;
        if (j->nt < 64) j->trace[j->nt++] = ++reloj;
        wdl_pump();                                                     /* desde dentro no debe anidar */
        wdl_yield();                                                    /* "esperando a la red" */
        if (local[0] != (char)j->id || local[255] != (char)j->id) j->nested_start = -1;
    }
    j->done = 1;
}

int main(void)
{
    struct job J[6]; memset(J, 0, sizeof J);
    for (int i = 0; i < 6; i++) { J[i].id = i + 1; J[i].steps = 3 + i * 2; }
    CK(!wdl_inside() && wdl_live() == 0, "al principio no hay descargas");
    int lanz = 0; for (int i = 0; i < 6; i++) lanz += wdl_start(trabajo, &J[i]) == 0;
    CK(lanz == WDL_N && wdl_live() == WDL_N, "como mucho WDL_N a la vez");
    CK(wdl_start(0, 0) == -1, "sin funcion: no");
    char *lo0 = (char *)0x1234; g_co_lo = lo0;
    int vueltas = 0;
    while (wdl_live() && vueltas < 1000) { wdl_pump(); vueltas++; CK(g_co_lo == lo0 && !wdl_inside(), "al volver se restaura la pila en curso"); }
    for (int i = 0; i < WDL_N; i++) {
        CK(J[i].done, "termina");
        CK(J[i].seen_inside == J[i].steps, "dentro de la descarga, wdl_inside() = 1");
        CK(J[i].lo_seen && J[i].lo_seen != lo0, "dentro, g_co_lo es su propia pila");
        CK(J[i].nested_start == 0, "su pila sobrevive a las cesiones");
    }
    CK(vueltas == J[WDL_N - 1].steps + 1, "cada vuelta da un paso a cada descarga viva (por turnos)");
    /* intercaladas: el primer paso de todas antes que el segundo de ninguna */
    long max1 = 0, min2 = 1 << 30; for (int i = 0; i < WDL_N; i++) { if (J[i].trace[0] > max1) max1 = J[i].trace[0]; if (J[i].trace[1] < min2) min2 = J[i].trace[1]; }
    CK(max1 < min2, "por turnos, no una detras de otra");
    /* los huecos se reutilizan (y sus pilas) */
    int otra = 0; for (int i = 4; i < 6; i++) otra += wdl_start(trabajo, &J[i]) == 0;
    CK(otra == 2, "terminadas, sus huecos sirven para otras");
    while (wdl_live()) wdl_pump();
    CK(J[4].done && J[5].done, "y terminan");
    wdl_yield();                                                       /* fuera de una descarga no hace nada */
    printf("test_web_dl_343: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
#else
int main(void) { printf("test_web_dl_343: solo en x86_64: SE OMITE\n"); return 0; }
#endif
