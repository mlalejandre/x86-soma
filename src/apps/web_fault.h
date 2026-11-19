/* SOMA_198: contencion de fallos del motor del navegador (x86_64).
 *
 * El motor (HTML, CSS, JavaScript, maquetacion: ~21.000 lineas que procesan datos de Internet) corre en ring 0. Hasta ahora un
 * solo puntero nulo en cualquiera de esas lineas era un panico del nucleo: una pagina podia tumbar el sistema entero.
 * Ahora una excepcion de CPU (#PF, #GP, #UD, #DE) o un desbordamiento de pila detectado por el protector, si ocurre en el hilo
 * de trabajo del navegador y fuera del asignador de memoria, cierra LA PAGINA: el hilo vuelve a un punto seguro, abandona la
 * pagina averiada (su memoria se da por perdida: no se puede liberar con garantias), abre una nueva con el aviso about:fallo y
 * el sistema sigue. Es contencion de averias, no aislamiento: el motor sigue teniendo los privilegios del nucleo. Tras
 * WEB_FAULT_MAX recuperaciones en un mismo arranque el siguiente fallo vuelve a ser un panico (algo va mal de verdad). */
#ifndef SOMA_WEB_FAULT_H
#define SOMA_WEB_FAULT_H
#include <stdint.h>
#define WEB_FAULT_MAX 8
#if defined(__x86_64__) && !defined(WEB_HOST_TEST) && !defined(SOMA_WEBSVC) && !__STDC_HOSTED__     /* SOMA_202: en el proceso de ring 3 no hace falta */
/* Lo llama el manejador de excepciones (ring 0). 1 = recuperable: *rip y *rsp quedan apuntando al camino de recuperacion. */
int  web_fault_recover(int vector, uint64_t fault_rip, uint64_t fault_addr, uint64_t *rip, uint64_t *rsp);
void web_fault_from_ssp(void);                 /* lo llama __stack_chk_fail: no vuelve si el fallo es del navegador */
const char *web_fault_text(void);              /* descripcion del ultimo fallo (para about:fallo) */
uint32_t web_fault_count(void);
/* Solo pruebas (orden `panic web <fase>`): provoca un fallo real dentro del motor. 1 navegacion, 2 JavaScript, 3 maquetacion
 * (puntero nulo); 4 instruccion no valida; 5 desbordamiento de pila. */
extern volatile int web_fault_inject;
void web_fault_fire(int phase);
static inline void web_fault_point(int phase) { if (__builtin_expect(web_fault_inject == phase, 0)) web_fault_fire(phase); }
#else
static inline void web_fault_point(int phase) { (void)phase; }
static inline const char *web_fault_text(void) { return ""; }
#endif
#endif
