#ifndef SOMA_MEDIA_H
#define SOMA_MEDIA_H

#include <stdint.h>

/* SOMA_PARCHE_023_MEDIA: reproductor multimedia (ventana de la GUI). Reproduce video AVI (MJPG + PCM) y audio WAV del
 * almacen BIGFS. Un hilo del kernel (mediad) decodifica y alimenta el audio; la ventana solo dibuja el ultimo fotograma. */
void media_init(void);
void media_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  media_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void media_handle_key(char c);

int  media_play(const char *path);          /* 0 = peticion aceptada (la reproduccion empieza en el hilo); <0 error */
void media_pause_toggle(void);
void media_stop(void);
void media_seek_rel(int32_t seconds);
void media_seek_abs(uint32_t seconds);
int  media_command(const char *args, char *out, uint32_t max);   /* comando de shell 'media ...' */

/* SOMA_250_YOUTUBE: reproducir un AVI (MJPG + PCM) que VA LLEGANDO (lo descarga otro hilo, p. ej. la aplicacion YouTube).
 * El reproductor lee solo lo recibido; sin datos espera en vez de terminar. Las tres funciones se llaman desde el hilo del
 * reproductor y no deben bloquear:
 *   read  : copia n bytes desde `off`; devuelve n, o <0 si esos bytes ya no estan (o aun no han llegado);
 *   avail : bytes recibidos desde el principio del flujo (lo que hay hasta ahora);
 *   eof   : 1 cuando ya no llegara nada mas.
 * Los saltos no se hacen aqui: quien descarga pide el video desde otro instante y vuelve a llamar a media_play_stream. */
struct media_stream {
    int64_t  (*read)(void *ctx, uint64_t off, void *buf, uint32_t n);
    uint64_t (*avail)(void *ctx);
    int      (*eof)(void *ctx);
    void     *ctx;
};
struct media_info {
    int state;                  /* 0 inactivo, 1 cargando, 2 reproduciendo, 3 en pausa, 4 terminado, 5 error */
    int stream, buffering, paused;
    uint64_t pos_us, dur_us;    /* posicion y duracion dentro de lo que se esta reproduciendo */
    uint32_t frames, dropped, width, height;
    const char *msg;            /* mensaje de error o aviso ("" si no hay) */
};
int  media_play_stream(const struct media_stream *s, const char *title);   /* 0 = aceptado */
int  media_stream_busy(void);               /* 1 = el reproductor sigue leyendo (o va a leer) un flujo */
void media_keepalive(void);                 /* "mi ventana sigue abierta": sin esto, la reproduccion se para a los 2,5 s */
int  media_frame(const uint32_t **px, uint32_t *w, uint32_t *h);            /* ultimo fotograma (0x00RRGGBB); 1 = hay */
void media_get_info(struct media_info *o);

#endif
