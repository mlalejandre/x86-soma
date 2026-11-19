/* PARCHE 030: soporte del protector de pila del compilador (make SSP=1).
 * El valor de aqui abajo es solo el inicial: en x86_64 boot.s lo sustituye por uno aleatorio antes de entrar en kernel_main
 * (arch/x86_64/ssp_init.c). Despues no cambia: cambiarlo con funciones en curso las haria fallar al volver. */
#include <stdint.h>
#include "console.h"
#include "arch.h"

uintptr_t __stack_chk_guard = (uintptr_t)0x0A0D5AC3A55A5A00ULL;

#if defined(__x86_64__)
void web_fault_from_ssp(void);
void gui_fault_from_ssp(void) __attribute__((weak));   /* SOMA_221_RESCATE (gui.c) */
#endif
__attribute__((noreturn, noinline)) void __stack_chk_fail(void)
{
#if defined(__x86_64__)
    web_fault_from_ssp();          /* SOMA_198: si fue el motor del navegador se cierra la pagina (no vuelve); si no, sigue aqui */
    if (gui_fault_from_ssp) gui_fault_from_ssp();   /* SOMA_221_RESCATE: igual para una aplicacion del escritorio */
#endif
    kprint("\n*** SOMA: DESBORDAMIENTO DE PILA DETECTADO (protector de pila) ***\n");
    kprint("Retorno de funcion con el canario destruido: sistema detenido.\n");
    for (;;) arch_halt();
}
