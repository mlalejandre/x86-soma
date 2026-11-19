#ifndef SOMA_SCRCAP_H
#define SOMA_SCRCAP_H
/* SOMA_179_T480S: captura de pantalla. Imagen (PNG), video (AVI MJPG), audio interno (WAV) y video con audio.
 * La imagen sale del bufer de la GUI (lo mismo que se ve, sin el puntero). El audio interno es lo que pasa por audio_sys_write
 * (cualquier reproductor del sistema): se guarda en mono a 48 kHz, con silencio donde no sono nada para no desfasar el video.
 * Un hilo "scrcapd" hace el trabajo para no frenar la GUI. Atajos: ImprPant = captura, Ctrl+ImprPant = empezar/parar video con audio.
 * Interruptor: soma.scrcap=off (y modo seguro). Archivos: /capturas (BIGFS). */
#include <stdint.h>

#define SC_VIDEO 1u
#define SC_AUDIO 2u

enum { SC_IDLE = 0, SC_WAIT, SC_REC, SC_SAVING };

struct scrcap_info {
    int state;                  /* SC_* */
    uint32_t mode;              /* SC_VIDEO | SC_AUDIO de lo que se graba */
    uint32_t wait_ms;           /* cuenta atras restante (SC_WAIT) */
    uint32_t rec_ms;            /* tiempo grabado */
    uint32_t w, h;              /* tamano del video */
    uint32_t fps, div;          /* ajustes */
    uint32_t frames, dup;       /* tramas guardadas y de ellas repetidas (pantalla quieta) */
    uint32_t skipped;           /* tramas perdidas por tamano o memoria */
    uint32_t enc_ms;            /* tiempo medio de capturar y codificar una trama */
    uint32_t fps_x10;           /* ritmo real */
    uint64_t bytes;             /* memoria usada por video + audio */
    uint32_t audio_ms;          /* audio guardado */
    uint32_t level;             /* nivel reciente del audio interno 0..32767 */
    uint32_t audio_calls;       /* porciones de audio recibidas (0 = nadie ha reproducido nada) */
    uint32_t audio_rate;        /* ultima frecuencia de reproduccion vista */
    int busy_shot;
    char last[96];              /* ultimo archivo guardado */
    char msg[112];              /* ultimo mensaje */
    uint32_t msg_seq;
};

void scrcap_init(void);                                                    /* arranque: crea el hilo y engancha el audio */
void scrcap_info_get(struct scrcap_info *o);
void scrcap_set_options(uint32_t fps, uint32_t div);                       /* fps 1..30, div 1..4; solo si no se graba */
int  scrcap_shot(const char *path, char *used, uint32_t max);              /* imagen ahora, en el hilo que llama; 0 = bien, <0 error */
int  scrcap_shot_async(const char *path, uint32_t delay_ms);               /* la hace el hilo scrcapd; el resultado queda en info.msg/last */
int  scrcap_start(uint32_t mode, uint32_t delay_ms);                       /* empieza a grabar (tras la cuenta atras); <0 error */
int  scrcap_stop(const char *path, char *used, uint32_t max);              /* para y guarda en el hilo que llama; 0 = bien */
int  scrcap_stop_async(void);                                              /* para y guarda en scrcapd */
int  scrcap_discard(void);
void scrcap_hotkey(int ctrl);                                              /* ImprPant (ctrl = con Control); seguro desde interrupciones */
int  scrcap_command(const char *args, char *out, uint32_t max);            /* shell: captura|cap ... */

/* Piezas neutras de arquitectura (se prueban en host) */
struct scrcap_rs { uint32_t rate; uint64_t opos, step; uint64_t ic; int32_t last; };
void scrcap_rs_reset(struct scrcap_rs *r, uint32_t rate);
/* convierte estereo a mono y remuestrea a 48000 Hz; devuelve cuantas muestras escribio en out (cap muestras como maximo) */
uint32_t scrcap_rs_run(struct scrcap_rs *r, const int16_t *stereo, uint32_t frames, int16_t *out, uint32_t cap);
/* media por bloques de div x div de un bufer 0x00RRGGBB (stride en pixeles) */
void scrcap_box(const uint32_t *src, uint32_t stride, uint32_t ow, uint32_t oh, uint32_t div, uint32_t *dst);
#endif
