#ifndef SOMA_WEB_ASYNC_151_H
#define SOMA_WEB_ASYNC_151_H
/* SOMA 151: SPSC entre el navegador y el hilo exclusivo de E/S.
 * El protocolo usa publicacion release/adquisicion acquire de state.
 * La estructura del pedido y de la respuesta NO se accede simultaneamente. */
#include <stdint.h>
#ifndef SOMA_WEB_ASYNC_IMAGES
#define SOMA_WEB_ASYNC_IMAGES 0
#endif
#if SOMA_WEB_ASYNC_IMAGES != 0 && SOMA_WEB_ASYNC_IMAGES != 1
#error "SOMA_WEB_ASYNC_IMAGES debe ser 0 o 1"
#endif
#if SOMA_WEB_ASYNC_IMAGES && !defined(__x86_64__) && !defined(SOMA151_PORTABLE_SYNTAX_TEST)
#error "SOMA 151: modo experimental solo x86_64; pruebe con ASYNC_IMAGES=0"
#endif
#if SOMA_WEB_ASYNC_IMAGES && defined(SOMA_BROWSER_NET_RING3) && SOMA_BROWSER_NET_RING3
#error "SOMA 151: aun no se admite combinar el lector asíncrono con netsvc experimental"
#endif
enum { SOMA151_IDLE=0, SOMA151_QUEUED=1, SOMA151_RUNNING=2, SOMA151_READY=3 };
struct soma151_ticket { uint32_t generation, hash; int index; };
static inline int soma151_same_ticket(struct soma151_ticket req, uint32_t generation,
                                      int index, uint32_t hash)
{ return req.generation == generation && req.index == index && req.hash == hash; }
static inline int soma151_running(int state)
{ return state == SOMA151_QUEUED || state == SOMA151_RUNNING; }
#endif
