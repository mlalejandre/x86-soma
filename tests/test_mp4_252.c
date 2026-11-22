/* SOMA_252_VIDEO_NATIVO: pruebas en el anfitrion del lector de MP4 del reproductor nativo (src/user/vplay/mp4.h).
 * Construye a mano un MP4 pequeno (ftyp, moov con una pista H.264 y otra AAC, tablas stts/ctts/stsc/stsz/stco|co64/stss) y
 * comprueba la lista de muestras que sale: desplazamientos, tamanos, instantes (con reordenacion de fotogramas B) y claves.
 * Tambien: moov al final del archivo, cajas de 64 bits, avcC y esds, busqueda por tiempo; y el mismo indice cortado en
 * cualquier punto o con bytes al azar: el lector nunca lee fuera de lo que se le da (ASan/UBSan). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/user/vplay/mp4.h"
#include "soma_test.h"

static uint8_t B[8192]; static uint32_t N;
static void w8(uint32_t v) { B[N++] = (uint8_t)v; }
static void w16(uint32_t v) { w8(v >> 8); w8(v); }
static void w32(uint32_t v) { w16(v >> 16); w16(v); }
static void w64(uint64_t v) { w32((uint32_t)(v >> 32)); w32((uint32_t)v); }
static void wz(uint32_t n) { while (n--) w8(0); }
static uint32_t box(const char *t) { uint32_t at = N; w32(0); w8((uint8_t)t[0]); w8((uint8_t)t[1]); w8((uint8_t)t[2]); w8((uint8_t)t[3]); return at; }
static void end(uint32_t at) { uint32_t s = N - at; B[at] = (uint8_t)(s >> 24); B[at + 1] = (uint8_t)(s >> 16); B[at + 2] = (uint8_t)(s >> 8); B[at + 3] = (uint8_t)s; }

static const uint8_t SPS[] = { 0x67, 0x42, 0x00, 0x1E, 0xAA, 0xBB }, PPS[] = { 0x68, 0xCE, 0x3C, 0x80 };

/* pista de video: 6 muestras en dos trozos (4 + 2), 30 fps con escala 15360, orden de decodificacion I P B B P B;
 * pista de audio: 5 cuadros de 1024 muestras a 44100 Hz en dos trozos (3 + 2), tamano fijo de 300 bytes */
static void trak(int video, int co64, uint64_t base)
{
    uint32_t t = box("trak");
    uint32_t h = box("tkhd"); wz(84); end(h);
    uint32_t m = box("mdia");
    uint32_t md = box("mdhd"); w32(0); w32(0); w32(0); w32(video ? 15360 : 44100); w32(video ? 6 * 512 : 5 * 1024); w32(0); end(md);
    uint32_t hd = box("hdlr"); wz(8); w8(video ? 'v' : 's'); w8(video ? 'i' : 'o'); w8(video ? 'd' : 'u'); w8(video ? 'e' : 'n'); wz(13); end(hd);
    uint32_t mi = box("minf");
    uint32_t st = box("stbl");
    uint32_t sd = box("stsd"); w32(0); w32(1);
    if (video) {
        uint32_t e = box("avc1"); wz(6); w16(1); wz(16); w16(640); w16(360); wz(50);
        uint32_t c = box("avcC"); w8(1); w8(0x42); w8(0); w8(0x1E); w8(0xFF); w8(0xE1); w16(sizeof SPS); for (uint32_t i = 0; i < sizeof SPS; ++i) w8(SPS[i]);
        w8(1); w16(sizeof PPS); for (uint32_t i = 0; i < sizeof PPS; ++i) w8(PPS[i]);
        end(c); end(e);
    } else {
        uint32_t e = box("mp4a"); wz(6); w16(1); w16(0); wz(6); w16(2); w16(16); w32(0); w16(44100); w16(0);
        uint32_t c = box("esds"); w32(0);
        w8(0x03); w8(0x80); w8(0x80); w8(0x80); w8(25); w16(1); w8(0);                    /* ES_Descriptor con longitud en 4 bytes */
        w8(0x04); w8(17); w8(0x40); w8(0x15); wz(11);                                     /* DecoderConfig */
        w8(0x05); w8(2); w8(0x12); w8(0x10);                                              /* AudioSpecificConfig: AAC-LC 44100 estereo */
        w8(0x06); w8(1); w8(2);
        end(c); end(e);
    }
    end(sd);
    uint32_t b;
    if (video) {
        b = box("stts"); w32(0); w32(1); w32(6); w32(512); end(b);
        b = box("ctts"); w32(0); w32(5);                                                  /* se muestran en el orden I B B P B P */ w32(1); w32(1024); w32(1); w32(2048); w32(2); w32(512); w32(1); w32(1536); w32(1); w32(512); end(b);
        b = box("stss"); w32(0); w32(2); w32(1); w32(5); end(b);
        b = box("stsc"); w32(0); w32(2); w32(1); w32(4); w32(1); w32(2); w32(2); w32(1); end(b);
        b = box("stsz"); w32(0); w32(0); w32(6); w32(1000); w32(200); w32(50); w32(60); w32(900); w32(70); end(b);
    } else {
        b = box("stts"); w32(0); w32(1); w32(5); w32(1024); end(b);
        b = box("stsc"); w32(0); w32(2); w32(1); w32(3); w32(1); w32(2); w32(2); w32(1); end(b);
        b = box("stsz"); w32(0); w32(300); w32(5); end(b);
    }
    if (co64) { b = box("co64"); w32(0); w32(2); w64(base + (video ? 0 : 1310)); w64(base + (video ? 2210 : 3180)); end(b); }
    else { b = box("stco"); w32(0); w32(2); w32((uint32_t)base + (video ? 0u : 1310u)); w32((uint32_t)base + (video ? 2210u : 3180u)); end(b); }
    end(st); end(mi); end(m); end(t);
}
static uint32_t make_moov(int co64, uint64_t base)                 /* devuelve donde empieza el moov dentro de B */
{
    N = 0;
    uint32_t f = box("ftyp"); w32(0x69736F6D); w32(512); w32(0x69736F6D); end(f);
    uint32_t at = N;
    uint32_t m = box("moov");
    uint32_t h = box("mvhd"); wz(100); end(h);
    trak(1, co64, base); trak(0, co64, base);
    uint32_t u = box("udta"); wz(20); end(u);
    end(m);
    return at;
}

static void test_basico(int co64)
{
    uint64_t base = co64 ? 0x100000000ULL + 5000u : 5000u;
    uint32_t at = make_moov(co64, base);
    uint64_t off = 0, size = 0, fsize = base + 4000;
    CHECK_EQ_INT(mp4_find_moov(B, N, 0, fsize, &off, &size), 1);
    CHECK(off == at && size == N - at);
    struct mp4_info in;
    CHECK_EQ_INT(mp4_parse_moov(B + off, size, &in), 1);
    CHECK(in.v.present && in.a.present);
    CHECK_EQ_INT(in.v.width, 640); CHECK_EQ_INT(in.v.height, 360); CHECK_EQ_INT(in.v.timescale, 15360); CHECK_EQ_INT(in.v.nsamples, 6);
    CHECK_EQ_INT(in.a.channels, 2); CHECK_EQ_INT(in.a.rate, 44100); CHECK_EQ_INT(in.a.nsamples, 5);
    CHECK(in.duration_ms == 200);
    struct mp4_sample v[8], a[8];
    CHECK_EQ_INT(mp4_build(&in.v, v, 8), 6);
    static const uint32_t voff[6] = { 0, 1000, 1200, 1250, 2210, 3110 }, vsz[6] = { 1000, 200, 50, 60, 900, 70 }, vkey[6] = { 1, 0, 0, 0, 1, 0 };
    static const uint32_t vpts[6] = { 0, 100000, 33333, 66666, 166666, 133333 };          /* (dts + ctts - 1024) / 15360: el primero se muestra en 0 */
    for (int i = 0; i < 6; ++i) { CHECK(v[i].off == base + voff[i]); CHECK_EQ_INT(v[i].size, vsz[i]); CHECK_EQ_INT(v[i].key, vkey[i]); CHECK(v[i].pts_us == vpts[i]); }
    CHECK_EQ_INT(mp4_build(&in.a, a, 8), 5);
    static const uint32_t aoff[5] = { 1310, 1610, 1910, 3180, 3480 };
    for (int i = 0; i < 5; ++i) { CHECK(a[i].off == base + aoff[i]); CHECK_EQ_INT(a[i].size, 300); CHECK_EQ_INT(a[i].key, 1); CHECK(a[i].pts_us == (uint64_t)i * 1024u * 1000000u / 44100u); }
    CHECK_EQ_INT(mp4_build(&in.v, v, 3), 3);                                             /* tope del llamante */
    uint8_t hdr[64]; uint32_t nal = 0, hn = mp4_avcc(in.v.cfg.p, in.v.cfg.n, hdr, sizeof hdr, &nal);
    CHECK_EQ_INT(hn, 4 + sizeof SPS + 4 + sizeof PPS); CHECK_EQ_INT(nal, 4);
    CHECK(hdr[3] == 1 && hdr[4] == 0x67 && hdr[4 + sizeof SPS + 3] == 1 && hdr[4 + sizeof SPS + 4] == 0x68);
    CHECK_EQ_INT(mp4_avcc(in.v.cfg.p, in.v.cfg.n, hdr, 10, &nal), 0);                    /* no cabe */
    const uint8_t *asc = 0; CHECK_EQ_INT(mp4_esds_asc(in.a.cfg.p, in.a.cfg.n, &asc), 2); CHECK(asc && asc[0] == 0x12 && asc[1] == 0x10);
    /* salto: la ultima clave anterior al instante pedido */
    CHECK_EQ_INT(mp4_build(&in.v, v, 8), 6);
    struct mp4_sample s[4] = { { 0, 0, 1, 1 }, { 1, 100, 1, 0 }, { 2, 200, 1, 1 }, { 3, 300, 1, 0 } };
    CHECK_EQ_INT(mp4_seek_index(s, 4, 0, 1), 0); CHECK_EQ_INT(mp4_seek_index(s, 4, 150, 1), 0); CHECK_EQ_INT(mp4_seek_index(s, 4, 250, 1), 2);
    CHECK_EQ_INT(mp4_seek_index(s, 4, 99999, 1), 2); CHECK_EQ_INT(mp4_seek_index(s, 4, 99999, 0), 3); CHECK_EQ_INT(mp4_seek_index(s, 0, 5, 1), 0);
}

static void test_moov_al_final(void)
{
    /* ftyp(20) mdat(4008) moov: mirando solo el principio, el lector dice donde seguir */
    uint32_t at = make_moov(0, 28);
    uint32_t mlen = N - at; static uint8_t F[16384]; uint32_t fn = 0;
    memcpy(F, B, at); fn = at;
    F[fn++] = 0; F[fn++] = 0; F[fn++] = 0x0F; F[fn++] = 0xA8; memcpy(F + fn, "mdat", 4); fn += 4; memset(F + fn, 0x55, 4000); fn += 4000;
    uint32_t moov_at = fn; memcpy(F + fn, B + at, mlen); fn += mlen;
    uint64_t off = 0, size = 0;
    CHECK_EQ_INT(mp4_find_moov(F, 64, 0, fn, &off, &size), 0); CHECK(off == moov_at);     /* con 64 bytes: "sigue en moov_at" */
    CHECK_EQ_INT(mp4_find_moov(F + moov_at, 16, moov_at, fn, &off, &size), 1); CHECK(off == moov_at && size == mlen);
    CHECK_EQ_INT(mp4_find_moov(F, 4, 0, fn, &off, &size), 0); CHECK(off == 0);            /* aun no hay ni una cabecera */
    CHECK_EQ_INT(mp4_find_moov((const uint8_t *)"<!DOCTYPE html><html>", 21, 0, 5000, &off, &size), -1);
    uint8_t big[32] = { 0, 0, 0, 1, 'm', 'd', 'a', 't', 0, 0, 0, 1, 0, 0, 0, 0x20 };      /* mdat con tamano de 64 bits */
    CHECK_EQ_INT(mp4_find_moov(big, 32, 0, 0x200000000ULL, &off, &size), 0); CHECK(off == 0x100000020ULL);
    uint8_t zero[16] = { 0, 0, 0, 0, 'm', 'd', 'a', 't' };                                /* tamano 0 = hasta el final: no hay moov */
    CHECK_EQ_INT(mp4_find_moov(zero, 16, 0, 1000, &off, &size), 0); CHECK(off == 1000);
    CHECK_EQ_INT(mp4_find_moov(zero, 0, 1000, 1000, &off, &size), -1);
}

static void test_robustez(void)
{
    uint32_t at = make_moov(0, 5000); uint32_t mlen = N - at;
    struct mp4_info in; struct mp4_sample s[16]; uint8_t hdr[64]; uint32_t nal; const uint8_t *asc;
    for (uint32_t cut = 0; cut <= mlen; ++cut) {                                          /* el indice cortado en cada byte */
        uint8_t *c = (uint8_t *)malloc(cut ? cut : 1); memcpy(c, B + at, cut);
        if (mp4_parse_moov(c, cut, &in)) {
            (void)mp4_build(&in.v, s, 16); (void)mp4_build(&in.a, s, 16);
            if (in.v.present) (void)mp4_avcc(in.v.cfg.p, in.v.cfg.n, hdr, sizeof hdr, &nal);
            if (in.a.present) (void)mp4_esds_asc(in.a.cfg.p, in.a.cfg.n, &asc);
        }
        free(c);
    }
    uint64_t seed = 252; int parsed = 0;
    for (int t = 0; t < 20000; ++t) {                                                     /* bytes al azar dentro de un indice valido */
        uint8_t *c = (uint8_t *)malloc(mlen); memcpy(c, B + at, mlen);
        int k = 1 + t % 6;
        for (int j = 0; j < k; ++j) { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; uint32_t r = (uint32_t)(seed >> 33); c[8 + r % (mlen - 8)] = (uint8_t)(r >> 16) & ((r >> 24) & 1 ? 0xFF : 0x03); }
        if (mp4_parse_moov(c, mlen, &in)) {
            parsed++;
            uint32_t nv = mp4_build(&in.v, s, 16), na = mp4_build(&in.a, s, 16);
            CHECK(nv <= 16 && na <= 16);
            if (in.v.present) { uint32_t n = mp4_avcc(in.v.cfg.p, in.v.cfg.n, hdr, sizeof hdr, &nal); CHECK(n <= sizeof hdr); }
            if (in.a.present) { uint32_t n = mp4_esds_asc(in.a.cfg.p, in.a.cfg.n, &asc); CHECK(n == 0 || (asc >= c && asc + n <= c + mlen)); }
        }
        free(c);
    }
    CHECK(parsed > 5000);
    /* tablas que mienten: mas muestras de las que caben en la tabla, trozos sin muestras, tabla de trozos vacia */
    make_moov(0, 5000);
    CHECK_EQ_INT(mp4_parse_moov(B + at, mlen, &in), 1);
    struct mp4_track tr = in.v; uint8_t zeros[16] = { 0 };
    tr.nsamples = 1000000; CHECK(mp4_build(&tr, s, 16) <= 16);
    tr = in.v; tr.stsc.p = zeros; tr.stsc.n = 16; CHECK_EQ_INT(mp4_build(&tr, s, 16), 0);
    tr = in.v; tr.stco.n = 0; CHECK_EQ_INT(mp4_build(&tr, s, 16), 0);
    tr = in.v; tr.stts.n = 4; CHECK(mp4_build(&tr, s, 16) <= 16);
    uint8_t bad_avcc[] = { 1, 0x42, 0, 0x1E, 0xFF, 0xFF, 0xFF, 0xFF };                    /* 31 SPS de 65535 bytes */
    CHECK_EQ_INT(mp4_avcc(bad_avcc, sizeof bad_avcc, hdr, sizeof hdr, &nal), 0);
    uint8_t bad_esds[] = { 0x03, 0xFF, 0xFF, 0xFF, 0x7F, 0, 0, 0xFF };                    /* longitudes enormes y banderas con todo */
    CHECK_EQ_INT(mp4_esds_asc(bad_esds, sizeof bad_esds, &asc), 0);
}

int main(void)
{
    test_basico(0);
    test_basico(1);
    test_moov_al_final();
    test_robustez();
    TEST_MAIN_END("mp4_252");
}
