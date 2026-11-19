#ifndef MYOS_HDA_H
#define MYOS_HDA_H

#include <stdint.h>
#include "audio.h"

/* SOMA_PARCHE_020_AUDIO: controlador Intel HD Audio (ICH6/ICH9; en QEMU: -device intel-hda -device hda-duplex).
 * Sin interrupciones: la posicion del DMA se consulta (LPIB). Una salida PCM de 16 bits estereo. */
int  hda_init(void);                               /* 0 = listo; <0 = no hay controlador/codec utilizable. Imprime el estado */
const struct audio_hw *hda_hw(void);               /* ganchos para el nucleo de audio (valido si hda_init() devolvio 0) */
int  hda_ready(void);
int  hda_set_volume(int percent);
int  hda_get_volume(void);                         /* SOMA_168_T480S */                  /* 0..100 sobre los amplificadores del camino de salida */
int  hda_format_for_rate(uint32_t rate, uint16_t *fmt);   /* formato de flujo HDA de una frecuencia; 0 si es soportada */
void hda_info(char *out, uint32_t max);

/* SOMA_177_T480S: entrada (microfono). Fuentes = pines de entrada alcanzables desde un ADC, ordenadas (microfono interno primero).
 * La captura es PCM de 16 bits estereo en un anillo DMA de 128 KiB que quien captura debe vaciar a menudo (se consulta LPIB). */
int  hda_mic_ready(void);                          /* 1 = hay al menos una fuente y anillo de captura */
int  hda_mic_count(void);
int  hda_mic_selected(void);
int  hda_mic_select(int idx);                      /* enciende ese camino (apaga el anterior); 0 = OK */
int  hda_mic_set_gain(int pct);                    /* 0..100: "boost" del pin (o ganancia del ADC si el pin no tiene) */
int  hda_mic_get_gain(void);
void hda_mic_describe(int idx, char *out, uint32_t max);
int  hda_cap_start(uint32_t rate);                 /* 0 = capturando; frecuencias de hda_format_for_rate */
void hda_cap_stop(void);
uint32_t hda_cap_pos(void);                        /* byte del anillo que el DMA esta escribiendo */
const uint8_t *hda_cap_ring(uint32_t *bytes);
int  hda_cap_running(void);
int  hda_raw(uint8_t nid, uint32_t verb, uint32_t payload, uint32_t *resp);   /* orden cruda al codec (diagnostico) */
void hda_dump(void);                               /* escribe por la consola el arbol de widgets */

#endif
