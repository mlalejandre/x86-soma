#ifndef SOMA_CAM_HW_H
#define SOMA_CAM_HW_H
/* SOMA_178_T480S: interfaz entre el motor de camara (camera.c) y el hardware (x86_64: xhci.c, transferencias isocronas UVC). */
#include <stdint.h>
#include "uvc.h"

struct cam_hw_info {
    int present;                       /* hay una camara UVC enumerada */
    uint16_t vid, pid; uint8_t speed;  /* velocidad PORTSC: 3 = alta */
    uint32_t cfg_len;
    const struct uvc_cfg *cfg;         /* descriptores analizados (valido mientras no se reexplore) */
    char why[96];                      /* motivo si no hay camara utilizable */
};
struct cam_hw_stats {
    int streaming;
    uint8_t fmt_index, frm_index; uint32_t interval, probe_len, max_payload, max_frame;
    uint8_t alt, ep_addr, dci; uint32_t pkt_bytes;
    uint32_t td_queued, td_done, ev_ok, ev_short, ev_err, ev_zero, bytes, underrun, evq_lost, ring_ctl;
    uint32_t cc[32];
    uint8_t probe[48];
    char err[96];
};
typedef void (*cam_payload_fn)(const uint8_t *p, uint32_t n);

int  cam_hw_probe(struct cam_hw_info *i);                                  /* 0 = camara UVC lista para usar */
/* Negocia, configura el extremo isocrono y arranca el hilo de captura; cb recibe cada carga util (una microtrama). 0 = en marcha. */
int  cam_hw_start(const struct uvc_sel *sel, cam_payload_fn cb);
void cam_hw_stop(void);
void cam_hw_stats(struct cam_hw_stats *s);
int  cam_hw_running(void);
#endif
