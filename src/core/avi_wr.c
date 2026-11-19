#include "avi_wr.h"

static void p32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void p16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void fcc(uint8_t *p, const char *s) { p[0] = (uint8_t)s[0]; p[1] = (uint8_t)s[1]; p[2] = (uint8_t)s[2]; p[3] = (uint8_t)s[3]; }
static void zero(void *p, uint32_t n) { uint8_t *b = p; for (uint32_t i = 0; i < n; i++) b[i] = 0; }

#define HDRL_VID  (12 + 8 + 56 + 8 + 40)           /* LIST strl: tipo + strh + strf */
#define HDRL_AUD  (12 + 8 + 56 + 8 + 16)
static uint32_t hdrl_size(int audio) { return 4 + 8 + 56 + HDRL_VID + (audio ? HDRL_AUD : 0); }   /* contenido de LIST hdrl (tras su tamano) */

/* tamano de todo lo que va antes de los chunks de movi: RIFF(12) + LIST hdrl (8 + size) + LIST movi (12) */
static uint32_t head_bytes(int audio) { return 12 + 8 + hdrl_size(audio) + 12; }

static uint32_t apad(uint32_t n) { return (n + 1u) & ~1u; }

/* reparto del audio: bytes de audio que corresponden hasta la trama i incluida */
static uint64_t a_upto(const struct avi_wr_src *s, uint32_t i) { return ((s->audio_bytes * (uint64_t)(i + 1u)) / s->nframes) & ~1ull; }

static uint64_t movi_bytes(const struct avi_wr_src *s)
{
    uint64_t t = 4;
    for (uint32_t i = 0; i < s->nframes; i++) {
        t += 8 + apad(s->lens[i]);
        if (s->has_audio) { uint64_t a = a_upto(s, i) - (i ? a_upto(s, i - 1) : 0); if (a) t += 8 + a; }
    }
    return t;
}
static uint32_t n_entries(const struct avi_wr_src *s)
{
    uint32_t n = s->nframes;
    if (s->has_audio) for (uint32_t i = 0; i < s->nframes; i++) if (a_upto(s, i) - (i ? a_upto(s, i - 1) : 0)) n++;
    return n;
}
uint64_t avi_wr_size(const struct avi_wr_src *s)
{
    if (!s || !s->nframes) return 0;
    return head_bytes(s->has_audio) + movi_bytes(s) - 4 + 8 + 16ull * n_entries(s);
}

int avi_write(const struct avi_wr_src *s, avi_sink_fn sink, void *su, uint8_t *tmp, uint32_t tmp_cap, uint8_t *idx)
{
    if (!s || !sink || !s->nframes || !s->lens || !s->get_video || !s->w || !s->h || (s->has_audio && (!s->get_audio || !s->arate))) return AVI_WR_E_ARG;
    if (!tmp || tmp_cap < 65536u || !idx) return AVI_WR_E_MEM;
    uint32_t maxf = 0; for (uint32_t i = 0; i < s->nframes; i++) if (s->lens[i] > maxf) maxf = s->lens[i];
    if (maxf + 8 > tmp_cap) return AVI_WR_E_MEM;
    int au = s->has_audio ? 1 : 0;
    uint32_t hb = head_bytes(au), hs = hdrl_size(au); uint64_t mv = movi_bytes(s), ne = n_entries(s);
    uint8_t *h = tmp; zero(h, hb);
    uint32_t o = 0;
    fcc(h + o, "RIFF"); p32(h + o + 4, (uint32_t)(hb - 8 + (mv - 4) + 8 + 16 * ne)); fcc(h + o + 8, "AVI "); o += 12;
    fcc(h + o, "LIST"); p32(h + o + 4, hs); o += 8; fcc(h + o, "hdrl"); o += 4;
    fcc(h + o, "avih"); p32(h + o + 4, 56); o += 8;
    uint32_t upf = s->usec_per_frame ? s->usec_per_frame : 66667;
    p32(h + o, upf); p32(h + o + 4, (uint32_t)(maxf * (1000000u / upf + 1u) + (au ? s->arate * 2u : 0u))); p32(h + o + 12, 0x10); p32(h + o + 16, s->nframes);
    p32(h + o + 24, (uint32_t)(au ? 2 : 1)); p32(h + o + 28, maxf); p32(h + o + 32, s->w); p32(h + o + 36, s->h); o += 56;
    fcc(h + o, "LIST"); p32(h + o + 4, HDRL_VID - 8); fcc(h + o + 8, "strl"); o += 12;
    fcc(h + o, "strh"); p32(h + o + 4, 56); o += 8;
    fcc(h + o, "vids"); fcc(h + o + 4, "MJPG"); p32(h + o + 20, upf); p32(h + o + 24, 1000000); p32(h + o + 32, s->nframes); p32(h + o + 36, maxf); p32(h + o + 40, 0xFFFFFFFFu);
    p16(h + o + 52, s->w); p16(h + o + 54, s->h); o += 56;
    fcc(h + o, "strf"); p32(h + o + 4, 40); o += 8;
    p32(h + o, 40); p32(h + o + 4, s->w); p32(h + o + 8, s->h); p16(h + o + 12, 1); p16(h + o + 14, 24); fcc(h + o + 16, "MJPG"); p32(h + o + 20, s->w * s->h * 3u); o += 40;
    if (au) {
        fcc(h + o, "LIST"); p32(h + o + 4, HDRL_AUD - 8); fcc(h + o + 8, "strl"); o += 12;
        fcc(h + o, "strh"); p32(h + o + 4, 56); o += 8;
        fcc(h + o, "auds"); p32(h + o + 20, 2); p32(h + o + 24, s->arate * 2u); p32(h + o + 32, (uint32_t)(s->audio_bytes / 2u)); p32(h + o + 36, s->arate); p32(h + o + 40, 0xFFFFFFFFu); p32(h + o + 44, 2); o += 56;
        fcc(h + o, "strf"); p32(h + o + 4, 16); o += 8;
        p16(h + o, 1); p16(h + o + 2, 1); p32(h + o + 4, s->arate); p32(h + o + 8, s->arate * 2u); p16(h + o + 12, 2); p16(h + o + 14, 16); o += 16;
    }
    fcc(h + o, "LIST"); p32(h + o + 4, (uint32_t)mv); fcc(h + o + 8, "movi"); o += 12;
    if (o != hb) return AVI_WR_E_ARG;
    if (sink(su, 0, h, hb) != 0) return AVI_WR_E_SINK;
    uint64_t pos = hb; uint32_t rel = 4, ei = 0;
    for (uint32_t i = 0; i < s->nframes; i++) {
        uint32_t l = s->lens[i];
        zero(tmp, 8);
        fcc(tmp, "00dc"); p32(tmp + 4, l);
        if (s->get_video(s->u, i, tmp + 8, tmp_cap - 8) != l) return AVI_WR_E_SRC;
        uint32_t tot = 8 + l; if (l & 1u) { tmp[tot] = 0; tot++; }
        if (sink(su, pos, tmp, tot) != 0) return AVI_WR_E_SINK;
        fcc(idx + 16 * ei, "00dc"); p32(idx + 16 * ei + 4, 0x10); p32(idx + 16 * ei + 8, rel); p32(idx + 16 * ei + 12, l); ei++;
        pos += tot; rel += tot;
        if (au) {
            uint64_t a0 = i ? a_upto(s, i - 1) : 0, a1 = a_upto(s, i); uint32_t a = (uint32_t)(a1 - a0);
            if (a) {
                uint8_t *ab = tmp + 8, *ah = tmp;
                if (a + 8u > tmp_cap) return AVI_WR_E_MEM;
                fcc(ah, "01wb"); p32(ah + 4, a);
                uint32_t g = s->get_audio(s->u, a0, ab, a);
                if (g > a) return AVI_WR_E_SRC;
                for (uint32_t k = g; k < a; k++) ab[k] = 0;                                         /* si falto audio, silencio */
                if (sink(su, pos, tmp, 8 + a) != 0) return AVI_WR_E_SINK;
                fcc(idx + 16 * ei, "01wb"); p32(idx + 16 * ei + 4, 0x10); p32(idx + 16 * ei + 8, rel); p32(idx + 16 * ei + 12, a); ei++;
                pos += 8 + a; rel += 8 + a;
            }
        }
    }
    zero(tmp, 8); fcc(tmp, "idx1"); p32(tmp + 4, 16 * ei);
    if (sink(su, pos, tmp, 8) != 0) return AVI_WR_E_SINK;
    pos += 8;
    for (uint32_t off = 0; off < 16 * ei; ) {
        uint32_t n = 16 * ei - off; if (n > 65536u) n = 65536u;
        if (sink(su, pos, idx + off, n) != 0) return AVI_WR_E_SINK;
        pos += n; off += n;
    }
    return 0;
}
