#ifndef SOMA_AVI_WR_H
#define SOMA_AVI_WR_H
/* SOMA_178_T480S: escritor de AVI (RIFF) MJPG + audio PCM de 16 bits mono, neutro de arquitectura. Escribe en orden creciente de
 * desplazamiento mediante `sink`; cabeceras, entrelazado de audio (proporcional al tiempo de cada trama) e indice idx1. */
#include <stdint.h>

typedef int (*avi_sink_fn)(void *u, uint64_t off, const void *buf, uint32_t n);   /* 0 = bien */
struct avi_wr_src {
    uint32_t w, h, nframes;
    uint32_t usec_per_frame;
    const uint32_t *lens;                                   /* longitud de cada trama de video */
    int has_audio; uint32_t arate; uint64_t audio_bytes;
    uint32_t (*get_video)(void *u, uint32_t i, uint8_t *dst, uint32_t cap);                 /* copia la trama i; devuelve su longitud */
    uint32_t (*get_audio)(void *u, uint64_t off, void *dst, uint32_t n);                    /* copia hasta n bytes de audio desde off */
    void *u;
};
#define AVI_WR_E_ARG   (-1)
#define AVI_WR_E_MEM   (-2)
#define AVI_WR_E_SRC   (-3)
#define AVI_WR_E_SINK  (-4)
/* Tamano total del archivo que se escribira (para comprobar el espacio). */
uint64_t avi_wr_size(const struct avi_wr_src *s);
/* tmp: bufer de trabajo de al menos tmp_cap bytes (>= la mayor trama y >= 64 KiB); idx: memoria para 2*nframes+2 entradas de 16 bytes. */
int avi_write(const struct avi_wr_src *s, avi_sink_fn sink, void *sink_u, uint8_t *tmp, uint32_t tmp_cap, uint8_t *idx);
#endif
