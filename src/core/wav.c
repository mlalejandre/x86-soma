/* SOMA_PARCHE_020_AUDIO: ver wav.h */
#include "wav.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int tag(const uint8_t *p, const char *t) { return p[0] == (uint8_t)t[0] && p[1] == (uint8_t)t[1] && p[2] == (uint8_t)t[2] && p[3] == (uint8_t)t[3]; }

const char *wav_strerror(int c)
{
    switch (c) {
    case WAV_OK: return "ok";
    case WAV_E_SHORT: return "cabecera incompleta";
    case WAV_E_RIFF: return "no es un WAV (RIFF/WAVE)";
    case WAV_E_FMT: return "bloque fmt invalido";
    case WAV_E_UNSUPPORTED: return "solo PCM de 16 bits, mono o estereo, 4-192 kHz";
    case WAV_E_NODATA: return "sin bloque data";
    default: return "error desconocido";
    }
}

int wav_parse(const uint8_t *buf, size_t len, struct wav_info *out)
{
    if (!buf || !out) return WAV_E_SHORT;
    if (len < 12) return WAV_E_SHORT;
    if (!tag(buf, "RIFF") || !tag(buf + 8, "WAVE")) return WAV_E_RIFF;

    int have_fmt = 0;
    struct wav_info w = { 0, 0, 0, 0, 0 };
    size_t pos = 12;
    while (pos + 8 <= len) {
        const uint8_t *ck = buf + pos;
        uint32_t sz = rd32(ck + 4);
        size_t body = pos + 8;
        if (tag(ck, "fmt ")) {
            if (have_fmt) return WAV_E_FMT;
            if (sz < 16 || body + 16 > len) return body + 16 > len ? WAV_E_SHORT : WAV_E_FMT;
            uint16_t fmt = rd16(buf + body), ch = rd16(buf + body + 2), blk = rd16(buf + body + 12), bits = rd16(buf + body + 14);
            uint32_t rate = rd32(buf + body + 4), brate = rd32(buf + body + 8);
            if (fmt == 0xFFFE) {                                      /* WAVE_FORMAT_EXTENSIBLE: el subformato PCM */
                if (sz < 40 || body + 26 > len) return WAV_E_FMT;
                fmt = rd16(buf + body + 24);
            }
            if (fmt != 1 || bits != 16 || (ch != 1 && ch != 2) || rate < 4000 || rate > 192000) return WAV_E_UNSUPPORTED;
            if (blk != ch * 2u || brate != rate * blk) return WAV_E_FMT;
            w.channels = ch; w.rate = rate; w.bits = bits; have_fmt = 1;
        } else if (tag(ck, "data")) {
            if (!have_fmt) return WAV_E_FMT;                          /* los datos tienen que venir despues de fmt */
            w.data_offset = (uint32_t)body; w.data_size = sz;
            if (body > 0xFFFFFFFFu) return WAV_E_SHORT;
            *out = w;
            return WAV_OK;
        }
        /* siguiente bloque: los tamanos impares se rellenan hasta un numero par */
        size_t adv = (size_t)sz + (sz & 1u);
        if (adv < sz || body + adv < body) return WAV_E_SHORT;
        pos = body + adv;
    }
    return have_fmt ? (pos >= len && len < 4096 ? WAV_E_NODATA : WAV_E_SHORT) : WAV_E_FMT;
}

/* SOMA_177_T480S: ver wav.h */
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void wav_make_header(uint8_t out[44], uint16_t channels, uint32_t rate, uint32_t data_bytes)
{
    const char *r = "RIFF", *w = "WAVEfmt ", *d = "data";
    for (int i = 0; i < 4; ++i) out[i] = (uint8_t)r[i];
    put32(out + 4, 36u + data_bytes);
    for (int i = 0; i < 8; ++i) out[8 + i] = (uint8_t)w[i];
    put32(out + 16, 16); put16(out + 20, 1); put16(out + 22, channels);
    put32(out + 24, rate); put32(out + 28, rate * channels * 2u);
    put16(out + 32, (uint16_t)(channels * 2u)); put16(out + 34, 16);
    for (int i = 0; i < 4; ++i) out[36 + i] = (uint8_t)d[i];
    put32(out + 40, data_bytes);
}
