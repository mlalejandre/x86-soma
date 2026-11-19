#ifndef SOMA_CAMERA_H
#define SOMA_CAMERA_H
#include <stdint.h>

/* SOMA_178_T480S: motor de la camara web (UVC por USB isocrono). Negocia formato y tamano, recibe las tramas MJPEG, las decodifica
 * para la vista previa (hilo "camdec"), guarda fotos JPEG y graba video AVI (MJPG + audio PCM del microfono) en el almacen BIGFS. */
struct camera_info {
    int hw_present, running, recording, audio_rec, saving;
    uint16_t vid, pid; uint8_t speed;
    uint32_t w, h, fps10;                  /* tamano y velocidad negociados (fps x 10) */
    char fourcc[5];
    uint32_t fr_ok, fr_bad, fps_x10, last_bytes, dec_ok, dec_err; int dec_last_err;
    uint32_t rec_frames, rec_ms, rec_bytes, rec_max_bytes;
    uint32_t pv_w, pv_h, pv_seq;
    char msg[112];
};
struct camera_mode { uint16_t w, h; uint8_t mjpeg; };

void camera_init(void);
int  camera_present(void);                                    /* 1 = hay una camara UVC (explora el USB la primera vez) */
int  camera_modes(struct camera_mode *m, int max);            /* tamanos MJPEG disponibles, de menor a mayor */
int  camera_start(uint32_t w, uint32_t h, uint32_t fps);      /* 0 = en marcha; <0 error (camera_info().msg) */
void camera_stop(void);
void camera_info(struct camera_info *o);
const uint32_t *camera_preview(uint32_t *w, uint32_t *h, uint32_t *seq);   /* 0x00RRGGBB; NULL si aun no hay imagen */
int  camera_photo(const char *path, char *used, uint32_t max);              /* /fotos/foto_NNN.jpg; <0 error */
int  camera_rec_start(int with_audio);                                      /* <0 error */
int  camera_rec_stop(const char *path, char *used, uint32_t max);           /* para y guarda /videos/vid_NNN.avi */
int  camera_rec_discard(void);
int  camera_command(const char *args, char *out, uint32_t max);             /* orden de shell "cam ..." */
#endif
