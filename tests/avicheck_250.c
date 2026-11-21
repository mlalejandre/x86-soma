/* SOMA_250_YOUTUBE: herramienta del anfitrion para tests/test_yt_service_250.py. Recorre un AVI como lo hace el reproductor de
 * SOMA (src/apps/media.c): cabecera con avi.c, trozos de `movi` uno a uno, cada fotograma decodificado con jpeg.c. Un trozo de
 * video vacio es "repetir el fotograma anterior" (asi los escribe ffmpeg y asi los trata el reproductor).
 * Escribe: ancho alto fotogramas vacios bytes_de_audio frecuencia canales usec_por_fotograma fotogramas_de_la_cabecera */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/avi.h"
#include "../src/core/jpeg.h"
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)l + 1); if (!b || fread(b, 1, (size_t)l, f) != (size_t)l) return 2;
    fclose(f);
    struct avi_info in; int r = avi_parse(b, (size_t)(l < 65536 ? l : 65536), &in);      /* el reproductor solo lee 64 KiB de cabecera */
    if (r) { fprintf(stderr, "avi: %s\n", avi_strerror(r)); return 3; }
    if ((uint64_t)in.movi_end > (uint64_t)l) in.movi_end = (uint32_t)l;                   /* como media.c: el final es lo que hay */
    uint32_t *px = calloc((size_t)in.width * in.height, 4);
    uint64_t pos = in.movi_start, d; struct avi_chunk c; int w; long nf = 0, vacios = 0; unsigned long long ab = 0;
    while ((w = avi_walk(b, (size_t)l, &in, &pos, &c, &d)) == 1) {
        if (c.kind == AVI_K_VIDEO && c.stream == in.video_stream) {
            if (c.size == 0) { if (!nf) { fprintf(stderr, "el primer fotograma esta vacio\n"); return 3; } vacios++; nf++; continue; }
            struct jpeg_info ji;
            r = jpeg_decode(b + d, c.size, px, in.width, in.height, &ji);
            if (r) { fprintf(stderr, "fotograma %ld: %s\n", nf, jpeg_strerror(r)); return 3; }
            if (ji.width != in.width || ji.height != in.height) { fprintf(stderr, "fotograma %ld: tamano distinto del de la cabecera\n", nf); return 3; }
            if (c.size > 256u * 1024u) { fprintf(stderr, "fotograma %ld: %u bytes (el reproductor admite 256 KiB)\n", nf, c.size); return 3; }
            nf++;
        } else if (c.kind == AVI_K_AUDIO && c.stream == in.audio_stream) ab += c.size;
    }
    printf("%u %u %ld %ld %llu %u %u %u %u\n", in.width, in.height, nf, vacios, ab, in.rate, (unsigned)in.channels, in.usec_per_frame, in.total_frames);
    return w == 0 ? 0 : 3;
}
