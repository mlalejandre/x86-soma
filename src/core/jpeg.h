#ifndef MYOS_JPEG_H
#define MYOS_JPEG_H

#include <stdint.h>
#include <stddef.h>

/* SOMA_PARCHE_022_VIDEO: decodificador JPEG baseline (DCT secuencial de 8 bits, Huffman), solo enteros (sin SSE/FPU),
 * neutro de arquitectura: lo usan el reproductor de usuario (vidplay) y las pruebas en host.
 *
 * Soporta: 1 componente (gris) o 3 (YCbCr), factores de muestreo 1 o 2 en cada componente (4:4:4, 4:2:2, 4:2:0, 4:4:0; 4:1:1 no),
 * tablas DQT de 8 y 16 bits, DHT cualquiera, intervalos de reinicio (DRI/RSTn). NO soporta progresivo, aritmetico,
 * 12 bits ni escaneos no entrelazados. La IDCT es la de libjpeg (jidctint, "islow"): en 4:4:4 y gris el resultado es
 * practicamente identico al de libjpeg; el sobremuestreo de color es por replicacion (sin filtro "fancy").
 * Es robusto frente a archivos malformados: comprueba todos los limites y nunca escribe fuera del bufer de salida. */
#define JPEG_MAX_DIM 4096u

struct jpeg_info {
    uint32_t width, height;
    uint8_t ncomp;                   /* 1 o 3 */
    uint8_t hmax, vmax;              /* muestreo del componente de luminancia: 1 o 2 */
    uint16_t restart_interval;
};

#define JPEG_OK            0
#define JPEG_E_FORMAT     (-1)      /* no empieza por SOI / marcador inesperado */
#define JPEG_E_UNSUPPORTED (-2)     /* progresivo, aritmetico, 12 bits, muestreo raro, no entrelazado */
#define JPEG_E_DATA       (-3)      /* tablas o datos corruptos */
#define JPEG_E_TRUNC      (-4)      /* el archivo acaba antes de tiempo */
#define JPEG_E_SIZE       (-5)      /* dimensiones fuera de rango o mayores que el bufer de salida */

/* Lee solo las cabeceras (hasta SOS). */
int jpeg_get_info(const uint8_t *data, size_t len, struct jpeg_info *info);

/* Decodifica a pixeles 0x00RRGGBB. `out` debe tener al menos out_stride * height pixeles y out_stride >= width.
 * Si la imagen no cabe devuelve JPEG_E_SIZE sin escribir. En caso de error de datos la imagen puede estar
 * parcialmente escrita. */
int jpeg_decode(const uint8_t *data, size_t len, uint32_t *out, uint32_t out_stride, uint32_t out_max_height, struct jpeg_info *info);

const char *jpeg_strerror(int code);

void jpeg_yield_hook(void);
/* PARCHE 139: igual que jpeg_decode, pero en x86_64 se ejecuta en el servicio aislado imgdec (src/apps/web_img_sbx.c). Lo usa el kernel. */
int jpeg_decode_isolated(const uint8_t *data, size_t len, uint32_t *out, uint32_t out_stride, uint32_t out_max_height, struct jpeg_info *info);    /* SOMA_PARCHE_028_UI: punto de cesion durante la decodificacion (debil) */
#endif
