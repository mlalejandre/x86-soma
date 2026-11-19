#ifndef SOMA_CCMP_H
#define SOMA_CCMP_H
/* SOMA_175C: CCMP (AES-CCM de IEEE 802.11, el cifrado de WPA2) en software. Logica pura, sin E/S.
 * El driver de la Intel 8265 cifra y descifra las tramas de datos aqui (la tarjeta solo las transmite y recibe), asi no hace
 * falta instalar claves en el firmware. Probado en el host con el vector oficial de 802.11 y con AES-CCM de referencia
 * (tests/test_ccmp_175.c). */
#include <stdint.h>
#include <stddef.h>

#define CCMP_HDR_LEN 8
#define CCMP_MIC_LEN 8

/* Cabecera 802.11 (hdr, hdrlen = 24, 26 con QoS, 30/32 con A4). Escribe en out la cabecera CCMP (8), el texto cifrado (plen) y el
 * MIC (8). Devuelve los bytes escritos. El bit Protected de hdr debe estar puesto (entra en el AAD). */
size_t ccmp_encrypt(const uint8_t tk[16], const uint8_t *hdr, size_t hdrlen, uint64_t pn, int keyid,
                    const uint8_t *plain, size_t plen, uint8_t *out);
/* in = cabecera CCMP + cifrado + MIC (inlen bytes). Escribe el texto claro en out y devuelve su longitud, o -1 si el MIC no cuadra o
 * la trama esta mal formada. *pn y *keyid (si no son nulos) reciben el PN y el indice de clave de la cabecera. */
int ccmp_decrypt(const uint8_t tk[16], const uint8_t *hdr, size_t hdrlen, const uint8_t *in, size_t inlen,
                 uint8_t *out, uint64_t *pn, int *keyid);
/* PN e indice de clave de una cabecera CCMP (sin descifrar) */
uint64_t ccmp_hdr_pn(const uint8_t *ccmp_hdr);
#endif
