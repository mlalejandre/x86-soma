/* SOMA_314_PROGZ: el formato de los programas comprimidos (src/core/progz_fmt.h), en el anfitrion.
 * Huella FNV-1a 64 con vectores conocidos (los mismos que usa tools/progz.py --prueba), lectura little-endian y la cabecera:
 * se acepta la buena y se rechaza cualquier firma, version o tamano que no cuadre. */
#include <stdio.h>
#include <string.h>
#include "soma_test.h"
#include "progz_fmt.h"

static void pon64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; ++i) { p[i] = (uint8_t)v; v >>= 8; } }

static size_t cabecera(uint8_t *z, uint32_t ver, uint64_t tam, uint64_t huella)
{
    memcpy(z, "SOMZ", 4);
    z[4] = (uint8_t)ver; z[5] = (uint8_t)(ver >> 8); z[6] = (uint8_t)(ver >> 16); z[7] = (uint8_t)(ver >> 24);
    pon64(z + 8, tam); pon64(z + 16, huella);
    z[24] = 0x78; z[25] = 0xDA;                                   /* algo de "zlib" detras */
    return 26;
}

int main(void)
{
    CHECK(progz_fnv((const uint8_t *)"", 0) == 0xCBF29CE484222325ULL);
    CHECK(progz_fnv((const uint8_t *)"a", 1) == 0xAF63DC4C8601EC8CULL);
    CHECK(progz_fnv((const uint8_t *)"SOMA", 4) == 0x744DE1245416A79DULL);

    uint8_t le[8] = { 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 };
    CHECK(progz_le64(le) == 0x0102030405060708ULL);

    uint8_t z[64]; struct progz_cab c;
    size_t n = cabecera(z, PROGZ_VERSION, 4, 0x744DE1245416A79DULL);
    memset(&c, 0, sizeof c);
    CHECK(progz_cabecera(z, n, &c) == 1);
    CHECK(c.tam == 4 && c.huella == 0x744DE1245416A79DULL);

    CHECK(progz_cabecera(z, PROGZ_CAB, &c) == 0);                  /* solo cabecera, sin datos */
    CHECK(progz_cabecera(0, n, &c) == 0);
    z[0] = 'X'; CHECK(progz_cabecera(z, n, &c) == 0);              /* firma */
    n = cabecera(z, 2, 4, 1);                  CHECK(progz_cabecera(z, n, &c) == 0);    /* version */
    n = cabecera(z, PROGZ_VERSION, 0, 1);      CHECK(progz_cabecera(z, n, &c) == 0);    /* tamano 0 */
    n = cabecera(z, PROGZ_VERSION, (uint64_t)PROGZ_TAM_MAX + 1u, 1); CHECK(progz_cabecera(z, n, &c) == 0);   /* demasiado grande */
    n = cabecera(z, PROGZ_VERSION, 1ULL << 40, 1);                   CHECK(progz_cabecera(z, n, &c) == 0);   /* no cabe en 32 bits */
    n = cabecera(z, PROGZ_VERSION, PROGZ_TAM_MAX, 1);                CHECK(progz_cabecera(z, n, &c) == 1);   /* el limite justo */
    TEST_MAIN_END("progz_314");
}
