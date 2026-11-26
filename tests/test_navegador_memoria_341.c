/* SOMA_341_NAVEGADOR_MEMORIA: cuanto monton concede el nucleo al proceso del navegador (w3_heap_bytes, src/core/web_r3_abi.h).
 *   cc -std=gnu11 -Isrc/core tests/test_navegador_memoria_341.c */
#include <stdio.h>
#include <stdint.h>
#include "web_r3_abi.h"

static int checks, fails;
#define CK(c, m) do { checks++; if (!(c)) { fails++; printf("FALLO: %s (linea %d)\n", m, __LINE__); } } while (0)
#define MiB(x) ((uint64_t)(x) << 20)

int main(void)
{
    CK(w3_heap_bytes(0) == W3_HEAP_MIN, "sin memoria libre: lo minimo de siempre (192 MiB)");
    CK(w3_heap_bytes(MiB(300)) == W3_HEAP_MIN, "300 MiB libres: 192");
    CK(w3_heap_bytes(MiB(442)) == MiB(221), "QEMU con 512 MB (442 libres): 221");
    CK(w3_heap_bytes(MiB(3500)) == W3_HEAP_MAX, "QEMU con 4 GiB: el maximo, 768");
    CK(w3_heap_bytes(MiB(15000)) == W3_HEAP_MAX, "T480s con 16 GiB: 768");
    CK(w3_heap_bytes(~0ull) == W3_HEAP_MAX, "sin desbordar con valores absurdos");
    for (uint64_t f = 0; f < MiB(4096); f += MiB(1) / 3 + 12345) {
        uint32_t h = w3_heap_bytes(f);
        CK(h >= W3_HEAP_MIN && h <= W3_HEAP_MAX && (h & (MiB(1) - 1)) == 0, "entre 192 y 768 MiB, en MiB enteros");
        CK(h == W3_HEAP_MIN || h <= f / 2, "nunca mas de la mitad de lo libre (salvo el minimo de siempre)");
    }
    CK(W3_HEAP_VA + W3_HEAP_MAX <= W3_SURF_VA, "el monton no pisa la superficie");
    printf("test_navegador_memoria_341: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
