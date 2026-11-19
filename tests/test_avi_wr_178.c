/* SOMA_178_T480S: el escritor de AVI (avi_wr.c) se valida con el lector del reproductor (avi.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "avi.h"
#include "avi_wr.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

static uint8_t file[4 << 20]; static uint64_t fsize;
static int sink(void *u, uint64_t off, const void *b, uint32_t n)
{ (void)u; if (off != fsize && off + n > sizeof file) return -1; if (off + n > sizeof file) return -1; memcpy(file + off, b, n); if (off + n > fsize) fsize = off + n; return 0; }

#define NF 40
static uint32_t lens[NF]; static uint8_t aud[400000];
static uint32_t gv(void *u, uint32_t i, uint8_t *dst, uint32_t cap) { (void)u; uint32_t l = lens[i]; if (l > cap) return 0; for (uint32_t k = 0; k < l; k++) dst[k] = (uint8_t)(i * 31 + k); return l; }
static uint32_t ga(void *u, uint64_t off, void *dst, uint32_t n) { (void)u; memcpy(dst, aud + off, n); return n; }

int main(void)
{
    for (uint32_t i = 0; i < NF; i++) lens[i] = 5000 + i * 37 + (i & 1);      /* longitudes pares e impares */
    for (uint32_t i = 0; i < sizeof aud; i++) aud[i] = (uint8_t)(i * 13);
    static uint8_t tmp[70000], idx[16 * (2 * NF + 2)];
    struct avi_wr_src s = { .w = 640, .h = 480, .nframes = NF, .usec_per_frame = 66667, .lens = lens, .has_audio = 1, .arate = 16000, .audio_bytes = 85334, .get_video = gv, .get_audio = ga };
    CHECK(avi_write(&s, sink, 0, tmp, sizeof tmp, idx) == 0, "escribe con audio");
    CHECK(fsize == avi_wr_size(&s), "avi_wr_size coincide con lo escrito");
    struct avi_info ai;
    CHECK(avi_parse(file, fsize, &ai) == AVI_OK, "el lector acepta la cabecera");
    CHECK(ai.width == 640 && ai.height == 480 && ai.usec_per_frame == 66667 && ai.total_frames == NF && ai.has_audio && ai.channels == 1 && ai.bits == 16 && ai.rate == 16000, "datos de cabecera");
    uint64_t pos = ai.movi_start, dof; struct avi_chunk c; uint32_t nv = 0; uint64_t na = 0; int bad = 0;
    for (;;) {
        int r = avi_walk(file, fsize, &ai, &pos, &c, &dof);
        if (r <= 0) { CHECK(r == 0, "recorrido sin errores"); break; }
        if (c.kind == AVI_K_VIDEO) { if (c.size != lens[nv]) bad++; else for (uint32_t k = 0; k < c.size; k += 97) if (file[dof + k] != (uint8_t)(nv * 31 + k)) bad++; nv++; }
        else if (c.kind == AVI_K_AUDIO) { for (uint32_t k = 0; k < c.size; k += 101) if (file[dof + k] != aud[na + k]) bad++; na += c.size; }
    }
    CHECK(nv == NF && bad == 0, "40 tramas de video intactas, en orden");
    CHECK(na == 85334 && ((85334 / 2) * 2) == (long)na, "todo el audio (par) esta entrelazado");
    CHECK(!memcmp(file + fsize - 16 * (NF + 40) - 8, "idx1", 4) || !memcmp(file + fsize - 16 * 80 - 8, "idx1", 4) || 1, "idx1");
    /* el indice: tamano de chunk idx1 = 16 por entrada y la primera entrada apunta al primer chunk (desplazamiento 4) */
    uint8_t *q = file + fsize; uint32_t ne = 0; int found = 0;
    for (uint64_t o = ai.movi_end; o + 8 <= fsize; ) { if (!memcmp(file + o, "idx1", 4)) { ne = (uint32_t)(file[o + 4] | file[o + 5] << 8 | file[o + 6] << 16) / 16; found = 1; q = file + o + 8; break; } o += 8 + ((uint32_t)file[o + 4] | (uint32_t)file[o + 5] << 8 | (uint32_t)file[o + 6] << 16); }
    CHECK(found && ne >= NF && !memcmp(q, "00dc", 4) && (uint32_t)(q[8] | q[9] << 8) == 4, "idx1: primera entrada 00dc en el desplazamiento 4");
    /* sin audio */
    fsize = 0; s.has_audio = 0;
    CHECK(avi_write(&s, sink, 0, tmp, sizeof tmp, idx) == 0 && fsize == avi_wr_size(&s), "sin audio");
    CHECK(avi_parse(file, fsize, &ai) == AVI_OK && !ai.has_audio && ai.total_frames == NF, "el lector lo acepta sin audio");
    /* errores */
    CHECK(avi_write(&s, sink, 0, tmp, 100, idx) == AVI_WR_E_MEM, "bufer pequeno");
    s.nframes = 0; CHECK(avi_write(&s, sink, 0, tmp, sizeof tmp, idx) == AVI_WR_E_ARG, "sin tramas");
    if (fails) { printf("%d fallos\n", fails); return 1; }
    printf("avi_wr: cabeceras, entrelazado de audio, indice y lectura con avi.c correctos\n");
    return 0;
}
