#ifndef MYOS_WAV_H
#define MYOS_WAV_H

#include <stdint.h>
#include <stddef.h>

/* SOMA_PARCHE_020_AUDIO: analizador de cabeceras WAV (PCM de 16 bits, mono o estereo). Neutro de arquitectura;
 * se prueba en host (tests/test_wav.c) y lo usan el reproductor del kernel y el programa de usuario wavplay. */
struct wav_info {
    uint16_t channels;          /* 1 o 2 */
    uint32_t rate;              /* Hz */
    uint16_t bits;              /* 16 */
    uint32_t data_offset;       /* posicion de los datos de audio dentro del archivo */
    uint32_t data_size;         /* bytes de datos declarados (el llamador los acota por el tamano real del archivo) */
};

#define WAV_OK            0
#define WAV_E_SHORT      (-1)   /* el buffer no contiene la cabecera completa */
#define WAV_E_RIFF       (-2)   /* no es RIFF/WAVE */
#define WAV_E_FMT        (-3)   /* bloque 'fmt ' ausente, repetido o invalido */
#define WAV_E_UNSUPPORTED (-4)  /* no es PCM de 16 bits mono/estereo o la frecuencia no vale */
#define WAV_E_NODATA     (-5)   /* sin bloque 'data' */

int wav_parse(const uint8_t *buf, size_t len, struct wav_info *out);
const char *wav_strerror(int code);
/* SOMA_177_T480S: cabecera canonica de 44 bytes (PCM de 16 bits) para un WAV de `data_bytes` bytes de datos */
void wav_make_header(uint8_t out[44], uint16_t channels, uint32_t rate, uint32_t data_bytes);

#endif
