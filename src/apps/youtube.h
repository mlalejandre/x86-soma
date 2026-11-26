#ifndef SOMA_YOUTUBE_H
#define SOMA_YOUTUBE_H

#include <stdint.h>

/* SOMA_250_YOUTUBE: aplicacion YouTube (ventana de la GUI). Busca videos, muestra los resultados con su miniatura y los
 * reproduce dentro de la ventana mientras llegan, con pausa, saltos, calidad y "siguiente".
 *
 * SOMA no decodifica H.264/AV1 ni puede resolver las direcciones de YouTube: de eso se ocupa el servicio del anfitrion
 * (tools/yt_service.py, con yt-dlp y ffmpeg), que entrega la busqueda en texto, las miniaturas en JPEG y el video como AVI
 * (MJPG + PCM) emitido en directo. El video se guarda en una ventana de memoria (yt_proto.h: yt_ring) y lo reproduce el
 * motor de SOMA Media (media_play_stream): nada pasa por el disco.
 *
 * Donde esta el servicio: /etc/secreto/youtube.conf (orden `youtube servidor <ip[:puerto]>`). Sin configurar se usa la
 * puerta de enlace, que en QEMU es el equipo anfitrion (10.0.2.2:8097; ejecutar.py arranca el servicio solo). */
void youtube_init(void);
void youtube_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int focused);
int  youtube_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void youtube_handle_key(char c);
int  youtube_mouse(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int wheel, int held);   /* 1 = repintar */
void youtube_close(void);                   /* la ventana se cierra o se minimiza: el video se para (queda listo para reanudar) */
int  youtube_escape(void);                  /* Esc: 1 = lo ha usado la aplicacion (soltar la busqueda, volver a los resultados) */
int  youtube_clip_op(int op);               /* CLIP_OP_* sobre la caja de busqueda */
int  youtube_tick(void);                    /* 1 = hay que repintar (fotograma nuevo, resultados, estado) */
void youtube_search(const char *query);
int  youtube_prueba_servicio(const char *host, uint16_t port, const char *path, char *out, uint32_t cap);   /* SOMA_327: prueba */     /* lanza una busqueda (la orden `youtube <texto>`) */
int  youtube_command(const char *args, char *out, uint32_t max);   /* orden de shell 'youtube ...'; 0 = hay que abrir la ventana; 3 = hecho y hay que traerla delante (SOMA_286) */
void youtube_hablar(int on);                /* SOMA_286_YOUTUBE: la proxima busqueda o respuesta de la IA se lee en voz alta (peticion del orbe) */

#endif
