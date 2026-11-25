/* SOMA_306_PILAS: canario al fondo de las pilas que se reservan con kmalloc (las del motor web: 256-512 KiB).
 * Las pilas de los hilos tienen una pagina de guarda; estas no: si se desbordan, escriben en el bloque del monton que haya
 * debajo. Se rellenan los primeros PILA_CANARIO_BYTES con un patron y se comprueba al volver de cada uso o en cada vuelta del
 * bucle del hilo. Si el patron se ha roto, la pila se ha desbordado: se dice cual y se detiene el hilo (__builtin_trap, la
 * excepcion da la traza), en vez de seguir con el monton corrupto.
 * Funciones puras (sin nucleo): se prueban en el anfitrion (tests/test_pila_canario_306.c). */
#ifndef SOMA_PILA_CANARIO_H
#define SOMA_PILA_CANARIO_H

#include <stdint.h>

#define PILA_CANARIO_BYTES 256u

static inline uint64_t pila_canario_valor(uint32_t i)
{
    return 0x50494C4143414E41ULL ^ ((uint64_t)i * 0x9E3779B97F4A7C15ULL);   /* "PILACANA" mezclado con el indice */
}

/* Pone el patron en los primeros PILA_CANARIO_BYTES de la pila (base = la direccion mas baja, la que dio kmalloc). */
static inline void pila_canario_pon(void *base)
{
    volatile uint64_t *p = (volatile uint64_t *)base;
    if (!p) return;
    for (uint32_t i = 0; i < PILA_CANARIO_BYTES / 8u; ++i) p[i] = pila_canario_valor(i);
}

/* 1 si el patron sigue entero (o no hay pila), 0 si alguien lo ha pisado. */
static inline int pila_canario_ok(const void *base)
{
    const volatile uint64_t *p = (const volatile uint64_t *)base;
    if (!p) return 1;
    for (uint32_t i = 0; i < PILA_CANARIO_BYTES / 8u; ++i)
        if (p[i] != pila_canario_valor(i)) return 0;
    return 1;
}

#endif
