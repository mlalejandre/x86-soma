/* SOMA_252_VIDEO_NATIVO: memoria compartida entre el nucleo (src/arch/x86_64/vplay_sys.c) y el reproductor de ring 3
 * (src/user/vplay/vp_core.c; su entrada esta al final de vp_libc.c). El proceso no tiene mas llamadas al sistema que dormir, la hora y el audio: el archivo le
 * llega por un anillo de esta region y los fotogramas decodificados salen por dos buferes de la misma region.
 *
 *   [0, 4096)                 struct vp_ctrl
 *   [VP_IN_OFF, +VP_IN_CAP)   anillo de entrada: el byte "o" del archivo esta en VP_IN_OFF + (o % VP_IN_CAP)
 *   [VP_FR_OFF, +2*VP_FR_BYTES) dos fotogramas XRGB de hasta VP_MAX_W x VP_MAX_H (filas de "w" pixeles, sin relleno)
 *
 * Reparto del anillo: el nucleo escribe [in_end, ...) mientras no pise lo que el reproductor aun necesita
 * (o < in_keep + VP_IN_CAP) y solo el nucleo mueve in_base/in_end; solo el reproductor mueve in_keep.
 * Para leer en otro sitio (un salto, o el indice al final del archivo) el reproductor pone want_off y sube want_seq; el
 * nucleo reinicia la descarga ahi (in_base = in_end = want_off) y copia want_seq en feed_seq. */
#ifndef SOMA_VP_ABI_H
#define SOMA_VP_ABI_H
#include <stdint.h>

#define VP_MAGIC     0x59415056u
#define VP_VERSION   1u
#define VP_HEAP_VA   0x8040000000ULL          /* = USER_HEAP_VA: el monton del proceso (bajo demanda) */
#define VP_HEAP_MAX  (96u << 20)
#define VP_SH_VA     0x8080000000ULL          /* esta region */
#define VP_MAX_W     854u
#define VP_MAX_H     480u
#define VP_IN_OFF    4096u
#define VP_IN_CAP    (8u << 20)
#define VP_FR_BYTES  ((VP_MAX_W * VP_MAX_H * 4u + 4095u) & ~4095u)
#define VP_FR_OFF    (VP_IN_OFF + VP_IN_CAP)
#define VP_SH_BYTES  (VP_FR_OFF + 2u * VP_FR_BYTES)

/* SOMA_253: modo "solo audio" (transcribir). El proceso no reproduce: decodifica la pista AAC de principio a fin, la pasa a PCM de
 * 16 bits MONO a 16 kHz (lo que pide whisper) y la deja en un anillo de salida que ocupa el sitio de los fotogramas
 * [VP_FR_OFF, +VP_OUT_CAP): el byte "o" de la salida esta en VP_FR_OFF + (o % VP_OUT_CAP). Solo el proceso mueve out_w; solo el nucleo, out_r. */
#define VP_OUT_CAP   (2u * VP_FR_BYTES)
#define VP_OUT_RATE  16000u
enum { VP_MODE_PLAY = 0, VP_MODE_AUDIO = 1 };

enum { VP_ST_START = 0, VP_ST_LOADING, VP_ST_PLAYING, VP_ST_ENDED, VP_ST_ERROR };
enum { VP_CMD_NONE = 0, VP_CMD_SEEK = 1 };

struct vp_ctrl {
    uint32_t magic, version;
    /* ---- nucleo -> reproductor */
    volatile uint64_t file_size;              /* 0 = aun no se sabe */
    volatile uint64_t in_base, in_end;        /* el anillo tiene los bytes [in_base, in_end) del archivo */
    volatile uint32_t feed_seq;               /* ultimo want_seq atendido */
    volatile uint32_t feed_error;             /* 1 = la descarga ha fallado (no llegaran mas bytes) */
    volatile uint32_t cmd_seq, cmd, cmd_arg;  /* orden nueva cuando cmd_seq cambia; cmd_arg = milisegundos */
    volatile uint32_t paused, quit;
    volatile uint32_t mode;                   /* VP_MODE_*; el nucleo lo pone antes de magic */
    volatile uint32_t out_max_ms;             /* modo audio: parar tras esta cantidad de audio (0 = todo) */
    volatile uint64_t out_r;                  /* modo audio: bytes de salida ya recogidos por el nucleo */
    /* ---- reproductor -> nucleo */
    volatile uint64_t out_w;                  /* modo audio: bytes de salida producidos */
    volatile uint32_t out_ms;                 /* modo audio: milisegundos de audio decodificados */
    volatile uint64_t in_keep;                /* por debajo de aqui el nucleo puede pisar */
    volatile uint64_t want_off; volatile uint32_t want_seq;
    volatile uint32_t state, cmd_ack;
    volatile uint32_t frame_seq, frame_buf, w, h;      /* fotograma listo: bufer frame_buf (0 o 1), de w x h */
    volatile uint32_t pos_ms, dur_ms, buffering, has_audio, has_video, audio_rate;
    volatile uint32_t frames_shown, frames_late, errors;
    char err[96];
};

/* ------------------------------------------------------------------ decodificadores (vp_vdec.c, vp_adec.c) */
int      vd_open(const uint8_t *hdr_annexb, uint32_t hdr_n, uint32_t nal_len, uint32_t w, uint32_t h);
int      vd_decode(const uint8_t *s, uint32_t n, uint32_t ts, uint32_t *dst, uint32_t dstride, int *got, uint32_t *out_ts, uint32_t *ow, uint32_t *oh);
void     vd_reset(void);
void     vd_close(void);
int      ad_open(const uint8_t *asc, uint32_t asc_n, uint32_t *rate, uint32_t *channels);
int      ad_format(uint32_t *rate, uint32_t *channels);
uint32_t ad_decode(const uint8_t *frame, uint32_t n, const int16_t **pcm);
void     ad_close(void);

/* ------------------------------------------------------------------ el reproductor y su entorno (vp_core.c) */
struct vp_env {
    uint64_t (*now_ms)(void);
    void     (*sleep_ms)(uint32_t ms);
    int      (*audio_open)(uint32_t rate);                          /* 0 = abierto (PCM de 16 bits estereo) */
    int64_t  (*audio_write)(const void *pcm, uint32_t bytes);        /* bytes aceptados */
    int      (*audio_status)(uint64_t *free_bytes, uint64_t *played_us);   /* 0 = bien */
    void     (*audio_close)(void);
};
int vp_run(struct vp_env *env, void *shared);                 /* lee C->mode: reproduce o extrae el audio */

/* ------------------------------------------------------------------ lado del nucleo (src/arch/x86_64/vplay_sys.c)
 * El nucleo lanza vplay.elf en ring 3, le da el archivo por la memoria compartida (lo va pidiendo a "fetch": red o
 * almacen) y recoge los fotogramas. Un video cada vez. */
/* De donde sale el archivo. fetch lee hasta "max" bytes a partir de "off" (puede bloquear, corre en un hilo propio):
 * devuelve los bytes leidos (> 0), 0 si off esta al final o mas alla, < 0 si falla; deja en *total el tamano del archivo.
 * close (opcional) se llama una vez, al terminar. */
struct vps_source {
    int64_t (*fetch)(void *ctx, uint64_t off, uint8_t *buf, uint32_t max, uint64_t *total, char *err, uint32_t emax);
    void (*close)(void *ctx);
    void *ctx;
};
enum { VPS_IDLE = 0, VPS_LOADING, VPS_PLAYING, VPS_ENDED, VPS_ERROR };
struct vps_info {
    int state;                      /* VPS_* */
    uint32_t pos_ms, dur_ms, w, h, frame_seq;
    int buffering, paused, has_audio;
    uint64_t have_bytes, total_bytes;   /* descargado hasta aqui (desplazamiento en el archivo) y tamano */
    uint32_t frames_shown, frames_late, errors;
    char err[128];
};
int  vps_start(const struct vps_source *src, char *err, uint32_t emax);    /* 1 = en marcha (para antes lo que hubiera) */
void vps_stop(void);                                                       /* no bloquea mas de medio segundo */
int  vps_active(void);
void vps_pause(int on);
void vps_seek(uint32_t ms);
void vps_get_info(struct vps_info *o);
const uint32_t *vps_frame(uint32_t *w, uint32_t *h, uint32_t *seq);        /* el ultimo fotograma (XRGB, w pixeles por fila) o 0 */

/* SOMA_253: extraer el audio. Una segunda instancia del mismo proceso, independiente de la que reproduce: no abre el audio del
 * sistema ni decodifica video; entrega PCM de 16 bits mono a 16 kHz (VP_OUT_RATE) al "sumidero" segun se va produciendo.
 * Mientras tanto la reproduccion puede seguir. sink devuelve < 0 si no puede guardar (se aborta con error). */
typedef int (*vpx_sink_fn)(void *ctx, const uint8_t *pcm, uint32_t n);
struct vpx_info {
    int state;                          /* VPS_*: LOADING, PLAYING (decodificando), ENDED (todo entregado), ERROR */
    uint32_t pos_ms, dur_ms;            /* audio entregado y duracion del archivo */
    uint64_t out_bytes, have_bytes, total_bytes;   /* PCM entregado; descargado hasta aqui; tamano del archivo */
    int truncated;                      /* la descarga se corto: el audio entregado esta incompleto */
    char err[128];
};
int  vpx_start(const struct vps_source *src, uint32_t max_ms, vpx_sink_fn sink, void *sctx, char *err, uint32_t emax);
void vpx_stop(void);                                                       /* bloquea hasta 2 s: despues el sumidero ya no se llama */
int  vpx_active(void);
void vpx_get_info(struct vpx_info *o);
#endif
