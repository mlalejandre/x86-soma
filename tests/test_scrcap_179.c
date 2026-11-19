/* SOMA_179: remuestreo a 48 kHz mono y promedio de bloques de la captura de pantalla. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "scrcap.h"
#define CHECK(c) do { if (!(c)) { printf("scrcap: fallo linea %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(void)
{
    /* 48 kHz: pasa tal cual (mono = media de L y R) con un retardo de una muestra */
    struct scrcap_rs r; scrcap_rs_reset(&r, 48000);
    int16_t in[2 * 100], out[400]; for (int i = 0; i < 100; i++) { in[2 * i] = (int16_t)(i * 100); in[2 * i + 1] = (int16_t)(i * 100 + 10); }
    uint32_t n = scrcap_rs_run(&r, in, 100, out, 400);
    CHECK(n >= 99 && n <= 100);
    for (uint32_t i = 1; i < n; i++) CHECK(abs(out[i] - (int)((i - 1) * 100 + 5)) <= 100);
    /* 44100 -> 48000 en porciones: cantidad de muestras ~ 48000/44100 y la senoide conserva su frecuencia y amplitud */
    scrcap_rs_reset(&r, 44100);
    enum { N = 44100 }; static int16_t sin_in[2 * N], sin_out[60000]; uint32_t tot = 0;
    for (int i = 0; i < N; i++) { int16_t v = (int16_t)(12000.0 * sin(2 * 3.14159265358979 * 1000.0 * i / 44100.0)); sin_in[2 * i] = v; sin_in[2 * i + 1] = v; }
    for (int off = 0; off < N; off += 1000) { int k = N - off < 1000 ? N - off : 1000; uint32_t m = scrcap_rs_run(&r, sin_in + 2 * off, (uint32_t)k, sin_out + tot, 60000 - tot); tot += m; }
    CHECK(tot >= 47990 && tot <= 48010);
    int pk = 0, cross = 0; for (uint32_t i = 100; i < tot; i++) { int a = abs(sin_out[i]); if (a > pk) pk = a; if (sin_out[i - 1] < 0 && sin_out[i] >= 0) cross++; }
    CHECK(pk > 11800 && pk < 12100);
    CHECK(cross >= 995 && cross <= 1005);
    /* 22050 (sube x2.18) */
    scrcap_rs_reset(&r, 22050); n = scrcap_rs_run(&r, in, 100, out, 400); CHECK(n >= 215 && n <= 220);
    /* promedio de bloques */
    uint32_t src[4 * 4], dst[2 * 2];
    for (int i = 0; i < 16; i++) src[i] = (uint32_t)(i % 4 < 2 ? 0x00FF0000 : 0x000000FF);
    scrcap_box(src, 4, 2, 2, 2, dst);
    CHECK(dst[0] == 0x00FF0000 && dst[1] == 0x000000FF);
    for (int i = 0; i < 16; i++) src[i] = ((i & 1) ? 0x00FFFFFFu : 0u);
    scrcap_box(src, 4, 2, 2, 2, dst); CHECK(dst[0] == 0x00808080);
    scrcap_box(src, 4, 4, 4, 1, (uint32_t *)malloc(64)); 
    printf("scrcap: remuestreo a 48 kHz (48000, 44100, 22050) y promedio de bloques correctos\n");
    return 0;
}
