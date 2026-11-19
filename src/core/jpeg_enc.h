#ifndef SOMA_JPEG_ENC_H
#define SOMA_JPEG_ENC_H
/* SOMA_179_T480S: codificador JPEG baseline (DCT secuencial, Huffman estandar del anexo K, YCbCr 4:2:0), solo enteros, sin memoria
 * dinamica y neutro de arquitectura. Lo usa la captura de pantalla para grabar video MJPG. Pruebas: tests/test_jpeg_enc_179.c.
 * argb: w*h pixeles 0x00RRGGBB con `stride` pixeles por fila. quality 1..100. Devuelve los bytes escritos en out o 0 si no cabe en cap
 * (o los argumentos no valen: dimensiones 1..4096). No es reentrante. */
#include <stdint.h>
#include <stddef.h>
size_t jpeg_encode_rgb(const uint32_t *argb, int w, int h, int stride, int quality, uint8_t *out, size_t cap);
#endif
