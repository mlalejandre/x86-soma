/* SOMA_306_PILAS: el canario de las pilas propias (src/core/pila_canario.h). Pila de verdad en el monton del anfitrion
 * (ASan vigila que nunca se lea o escriba fuera de ella). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "soma_test.h"
#include "pila_canario.h"

int main(void)
{
    const size_t tam = 64u * 1024u;
    unsigned char *pila = (unsigned char *)malloc(tam);
    CHECK(pila != 0);
    if (!pila) { TEST_MAIN_END("pila_canario"); }
    memset(pila, 0, tam);

    CHECK(!pila_canario_ok(pila));                           /* sin poner: memoria a cero no pasa por canario */
    pila_canario_pon(pila);
    CHECK(pila_canario_ok(pila));

    /* Uso normal de la pila: todo lo que esta por encima del canario se puede pisar */
    memset(pila + PILA_CANARIO_BYTES, 0xCC, tam - PILA_CANARIO_BYTES);
    CHECK(pila_canario_ok(pila));

    /* Un desbordamiento que llega al canario, en cualquier byte, se detecta */
    for (uint32_t i = 0; i < PILA_CANARIO_BYTES; ++i) {
        unsigned char viejo = pila[i];
        pila[i] ^= 0x01;
        if (pila_canario_ok(pila)) { CHECK(0 && "un byte cambiado del canario no se detecta"); }
        pila[i] = viejo;
    }
    CHECK(pila_canario_ok(pila));

    /* Escribir la pila con el patron de pintado de los hilos (0xA5) o con ceros tampoco pasa */
    memset(pila, 0xA5, PILA_CANARIO_BYTES);
    CHECK(!pila_canario_ok(pila));
    pila_canario_pon(pila);
    CHECK(pila_canario_ok(pila));

    /* Sin pila (kmalloc fallo): no hay nada que vigilar */
    pila_canario_pon(0);
    CHECK(pila_canario_ok(0));

    /* Los valores no se repiten dentro del canario (un relleno uniforme nunca coincide) */
    for (uint32_t i = 1; i < PILA_CANARIO_BYTES / 8u; ++i) CHECK(pila_canario_valor(i) != pila_canario_valor(i - 1));

    free(pila);
    TEST_MAIN_END("pila_canario");
}
