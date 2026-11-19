#ifndef SOMA_MICREC_H
#define SOMA_MICREC_H

#include <stdint.h>

/* SOMA_177_T480S: motor de grabacion del microfono (neutro de arquitectura; el hardware lo da hda.h).
 *
 *   microfono --ADC--> anillo DMA (hda) --hilo micrec cada ~5 ms--> mono de 16 bits (se elige el canal con senal,
 *   se quita el nivel continuo y se aplica la ganancia por software) --> bloques de 256 KiB en el heap (hasta 64 MiB)
 *
 * La grabacion vive en RAM; se puede escuchar, guardar como WAV en el almacen BIGFS (/grabaciones) o cargar otra.
 * Modos: inactivo, grabando, escuchando el nivel (sin guardar), probando fuentes, reproduciendo. */
enum { MR_IDLE = 0, MR_REC, MR_MON, MR_PROBE, MR_PLAY };

struct micrec_info {
    int      mode;            /* MR_* */
    int      available;       /* 1 = hay entrada de captura utilizable */
    int      nsrc, src;       /* numero de fuentes y la activa */
    int      hw_gain, sw_x10; /* ganancia del codec (0-100) y por software (10 = x1,0) */
    uint32_t rate;            /* Hz del audio grabado/cargado */
    uint32_t ms;              /* duracion grabada */
    uint32_t play_ms;         /* posicion de reproduccion */
    uint32_t max_ms;          /* capacidad restante a esta frecuencia (ms totales posibles) */
    int      level, hold;     /* nivel 0-100 y maximo reciente */
    uint32_t overruns, clipped;
    int      unsaved;         /* hay una grabacion sin guardar */
    char     msg[96];         /* ultimo mensaje (error o aviso) */
};

void micrec_init(void);                                 /* arranque: crea el hilo (no falla si no hay microfono) */
int  micrec_available(void);
void micrec_info(struct micrec_info *out);
int  micrec_record(uint32_t rate);                      /* 0 = pedido; <0 error (rate: 8000..48000) */
int  micrec_monitor(void);                              /* solo medir el nivel */
void micrec_stop(void);                                 /* para lo que haga (grabar, medir, reproducir) */
uint64_t micrec_bytes(void);                            /* SOMA_178_T480S: audio grabado (mono 16 bits) para el video */
uint32_t micrec_rate(void);
uint32_t micrec_read(uint64_t off, void *dst, uint32_t n);
int  micrec_busy(void);                                 /* 1 = grabando, midiendo o sondeando */
int  micrec_play(void);                                 /* reproduce lo grabado por la salida de audio */
void micrec_clear(void);
int  micrec_probe(void);                                /* mide todas las fuentes y elige la de mas senal */
int  micrec_set_source(int idx);
int  micrec_set_gain(int hw_pct);
int  micrec_set_sw(int x10);                            /* 10..160 */
void micrec_source_name(int idx, char *out, uint32_t max);
uint32_t micrec_waveform(uint8_t *cols, uint32_t ncols);   /* resumen de la grabacion en ncols columnas (0-255); devuelve las usadas */
int  micrec_save(const char *path, char *used, uint32_t max);   /* path NULL o "": /grabaciones/grab_NNN.wav; <0 error de BIGFS */
int  micrec_load(const char *path);
int  micrec_delete(const char *path);
int  micrec_command(const char *args, char *out, uint32_t max);   /* shell: mic ... */

#endif
