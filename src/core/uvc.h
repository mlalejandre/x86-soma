#ifndef SOMA_UVC_H
#define SOMA_UVC_H
/* SOMA_178_T480S: logica pura de la clase de video USB (UVC), neutra de arquitectura y probada en el host:
 *   - analisis del descriptor de configuracion (formatos MJPEG / sin comprimir, tamanos, intervalos, ajustes alternos isocronos),
 *   - eleccion de formato, tamano y velocidad, estructura de negociacion probe/commit,
 *   - ensamblado de tramas a partir de las cargas utiles (cabecera UVC: FID, EOF, ERR),
 *   - reparacion de MJPEG sin tablas Huffman (se insertan las estandar). */
#include <stdint.h>

#define UVC_MAX_FMT   6
#define UVC_MAX_FRM   12
#define UVC_MAX_ALT   16
#define UVC_MAX_VS    2
#define UVC_MAX_INT   8

#define UVC_FMT_MJPEG  1
#define UVC_FMT_UNCOMP 2

struct uvc_frame { uint8_t index; uint16_t w, h; uint32_t max_size, def_interval, min_interval, max_interval, step; uint8_t n_int; uint32_t interval[UVC_MAX_INT]; };
struct uvc_format { uint8_t index, kind, bpp, def_frame, n_frames; char fourcc[5]; struct uvc_frame frame[UVC_MAX_FRM]; };
struct uvc_alt { uint8_t alt, ep_addr, ep_attr, interval; uint16_t mps_raw; uint32_t bytes; };   /* bytes = paquete maximo por microtrama (mps x transacciones) */
struct uvc_stream {
    uint8_t iface, n_fmt, n_alt, has_bulk; uint8_t bulk_ep; uint16_t bulk_mps;
    struct uvc_format fmt[UVC_MAX_FMT];
    struct uvc_alt alt[UVC_MAX_ALT];
};
struct uvc_cfg { uint16_t bcd_uvc; uint8_t vc_iface, n_vs, found; struct uvc_stream vs[UVC_MAX_VS]; };

/* 0 = se encontro al menos una interfaz de streaming con un formato. Nunca lee fuera de [d, d+len). */
int uvc_parse(const uint8_t *d, uint32_t len, struct uvc_cfg *c);

struct uvc_sel { int vs, fmt, frm; uint32_t interval; };       /* indices en uvc_cfg; interval en unidades de 100 ns */
/* Elige interfaz de streaming, formato (MJPEG; si no hay y allow_raw, sin comprimir), tamano y velocidad. 0 = hay eleccion. */
int uvc_choose(const struct uvc_cfg *c, uint32_t want_w, uint32_t want_h, uint32_t want_fps, int allow_raw, struct uvc_sel *s);

/* Estructura de negociacion (probe/commit): len = 26, 34 o 48. */
void     uvc_probe_fill(uint8_t *b, uint32_t len, uint8_t fmt_index, uint8_t frm_index, uint32_t interval);
uint32_t uvc_probe_max_payload(const uint8_t *b, uint32_t len);
uint32_t uvc_probe_max_frame(const uint8_t *b, uint32_t len);
/* Ajuste alterno mas pequeno con al menos `need` bytes por microtrama; si ninguno llega, el mayor. -1 si no hay ninguno. */
int uvc_pick_alt(const struct uvc_stream *s, uint32_t need);

/* ---- ensamblador de tramas ---- */
#define UVC_FRAME_NONE 0
#define UVC_FRAME_OK   1
#define UVC_FRAME_BAD  2
struct uvc_asm {
    uint8_t *buf[2]; uint32_t cap; int cur;       /* doble bufer: `done` es la ultima trama completa */
    uint32_t len;                                 /* bytes de la trama en curso */
    uint8_t fid, have_fid, bad, started;
    uint32_t expect;                              /* 0 = MJPEG (se valida SOI/EOI); si no, tamano exacto de la trama */
    const uint8_t *done; uint32_t done_len;       /* valido tras un UVC_FRAME_OK hasta el siguiente */
    uint32_t pk, pk_empty, pk_hdr_err, pk_err_flag, fr_ok, fr_bad, fr_short, fr_over;
};
void uvc_asm_init(struct uvc_asm *a, uint8_t *b0, uint8_t *b1, uint32_t cap, uint32_t expect);
/* Una carga util (una microtrama). Devuelve UVC_FRAME_OK/BAD cuando se completa una trama, UVC_FRAME_NONE si no. */
int uvc_asm_feed(struct uvc_asm *a, const uint8_t *p, uint32_t n);
int uvc_mjpeg_valid(const uint8_t *d, uint32_t n);

/* MJPEG sin DHT: copia en out con las tablas estandar insertadas antes de SOS y devuelve la longitud nueva; si ya tiene DHT o
 * no es JPEG devuelve 0 (usar la trama tal cual); si no cabe, -1. */
int32_t uvc_mjpeg_add_dht(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap);
uint32_t uvc_std_dht_build(uint8_t *out);          /* segmento DHT estandar (420 bytes) */

/* Conversion YUY2 -> 0x00RRGGBB (para formatos sin comprimir en la vista previa). */
void uvc_yuy2_to_rgb(const uint8_t *yuy, uint32_t w, uint32_t h, uint32_t *out, uint32_t stride);
#endif
