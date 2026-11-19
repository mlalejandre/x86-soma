/* SOMA_178_T480S: prueba de la logica UVC (src/core/uvc.c) con un descriptor sintetico del estilo de la camara Chicony 04F2:B604. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uvc.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

static uint8_t cfg[2048]; static uint32_t cl;
static void put(const uint8_t *d, uint32_t n) { memcpy(cfg + cl, d, n); cl += n; }
static void iface(uint8_t n, uint8_t alt, uint8_t neps, uint8_t sub)
{ uint8_t d[9] = { 9, 4, n, alt, neps, 14, sub, 0, 0 }; put(d, 9); }
static void ep(uint8_t addr, uint8_t attr, uint16_t mps, uint8_t iv)
{ uint8_t d[7] = { 7, 5, addr, attr, (uint8_t)mps, (uint8_t)(mps >> 8), iv }; put(d, 7); }
static void fmt_mjpg(uint8_t idx, uint8_t nfr, uint8_t def)
{ uint8_t d[11] = { 11, 0x24, 6, idx, nfr, 1, def, 0, 0, 0, 0 }; put(d, 11); }
static void fmt_yuy2(uint8_t idx, uint8_t nfr, uint8_t def)
{ uint8_t d[27] = { 27, 0x24, 4, idx, nfr, 'Y','U','Y','2', 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71, 16, def, 0, 0, 0, 0 }; put(d, 27); }
static void frame(uint8_t st, uint8_t idx, uint16_t w, uint16_t h, uint32_t maxsz, const uint32_t *iv, uint8_t niv)
{
    uint8_t d[64]; uint32_t n = 26 + 4u * niv;
    memset(d, 0, sizeof d); d[0] = (uint8_t)n; d[1] = 0x24; d[2] = st; d[3] = idx; d[5] = (uint8_t)w; d[6] = (uint8_t)(w >> 8); d[7] = (uint8_t)h; d[8] = (uint8_t)(h >> 8);
    memcpy(d + 17, &maxsz, 4); uint32_t def = niv ? iv[0] : 333333; memcpy(d + 21, &def, 4); d[25] = niv;
    for (int i = 0; i < niv; i++) memcpy(d + 26 + 4 * i, &iv[i], 4);
    put(d, n);
}
static void build(void)
{
    static const uint32_t i30[] = { 333333, 666666, 1000000 }, i15[] = { 666666, 1000000 };
    cl = 0;
    uint8_t cfgh[9] = { 9, 2, 0, 0, 2, 1, 0, 0x80, 250 }; put(cfgh, 9);
    uint8_t iad[8] = { 8, 11, 0, 2, 14, 3, 0, 0 }; put(iad, 8);
    iface(0, 0, 1, 1);
    uint8_t vch[13] = { 13, 0x24, 1, 0x00, 0x01, 30, 0, 0, 0, 0xC0, 0xE1, 0xE4, 0x00 }; put(vch, 13);   /* bcdUVC 1.00 */
    ep(0x87, 3, 16, 8);
    iface(1, 0, 0, 2);
    uint8_t ih[14] = { 14, 0x24, 1, 2, 0, 0x81, 0, 0, 0, 0, 0, 0, 0, 0 }; put(ih, 14);
    fmt_yuy2(1, 2, 1); frame(5, 1, 640, 480, 614400, i30, 3); frame(5, 2, 320, 240, 153600, i30, 3);
    fmt_mjpg(2, 3, 1); frame(7, 1, 640, 480, 614400, i30, 3); frame(7, 2, 1280, 720, 1843200, i15, 2); frame(7, 3, 320, 240, 153600, i30, 3);
    static const uint16_t mps[] = { 0x0080, 0x0100, 0x0200, 0x0320, 0x0400, 0x1400 };
    for (int a = 1; a <= 6; a++) { iface(1, (uint8_t)a, 1, 2); ep(0x81, 5, mps[a - 1], 1); }
    cfg[2] = (uint8_t)cl; cfg[3] = (uint8_t)(cl >> 8);
}

static uint8_t b0[100000], b1[100000];
static void jpeg(uint8_t *o, uint32_t n)           /* SOI ... EOI de n bytes */
{ o[0] = 0xFF; o[1] = 0xD8; for (uint32_t i = 2; i + 2 < n; i++) o[i] = (uint8_t)(i * 7); o[n - 2] = 0xFF; o[n - 1] = 0xD9; }
static int feed_frame(struct uvc_asm *a, const uint8_t *fr, uint32_t n, int fid, uint32_t chunk, int eof, int err_on_first)
{
    int r = UVC_FRAME_NONE;
    for (uint32_t o = 0; o < n; o += chunk) {
        uint32_t m = n - o < chunk ? n - o : chunk; uint8_t pk[4000]; pk[0] = 2; pk[1] = (uint8_t)(fid | ((eof && o + m >= n) ? 2 : 0) | ((err_on_first && o == 0) ? 0x40 : 0));
        memcpy(pk + 2, fr + o, m);
        int x = uvc_asm_feed(a, pk, m + 2); if (x != UVC_FRAME_NONE) r = x;
    }
    return r;
}

int main(void)
{
    build();
    struct uvc_cfg c;
    CHECK(uvc_parse(cfg, cl, &c) == 0 && c.found, "analiza el descriptor");
    CHECK(c.bcd_uvc == 0x0100 && c.vc_iface == 0 && c.n_vs == 1 && c.vs[0].iface == 1, "VC 1.00, una interfaz de streaming (1)");
    CHECK(c.vs[0].n_fmt == 2 && c.vs[0].fmt[0].kind == UVC_FMT_UNCOMP && !strcmp(c.vs[0].fmt[0].fourcc, "YUY2") && c.vs[0].fmt[1].kind == UVC_FMT_MJPEG, "formatos YUY2 y MJPEG");
    CHECK(c.vs[0].fmt[1].n_frames == 3 && c.vs[0].fmt[1].frame[1].w == 1280 && c.vs[0].fmt[1].frame[1].h == 720 && c.vs[0].fmt[1].frame[1].n_int == 2 && c.vs[0].fmt[1].frame[1].interval[0] == 666666, "tamanos MJPEG con intervalos");
    CHECK(c.vs[0].n_alt == 6 && c.vs[0].alt[0].bytes == 128 && c.vs[0].alt[3].bytes == 800 && c.vs[0].alt[5].bytes == 3072 && c.vs[0].alt[5].ep_addr == 0x81, "ajustes alternos isocronos: 128 .. 3072 bytes/microtrama");
    CHECK(c.vs[0].fmt[1].frame[0].max_size == 614400 && c.vs[0].fmt[1].frame[0].def_interval == 333333, "tamano maximo e intervalo por defecto");

    struct uvc_sel s;
    CHECK(uvc_choose(&c, 640, 480, 30, 0, &s) == 0 && s.fmt == 1 && s.frm == 0 && s.interval == 333333, "640x480 a 30 fps: MJPEG, tamano 1, 333333");
    CHECK(uvc_choose(&c, 1280, 720, 30, 0, &s) == 0 && s.fmt == 1 && s.frm == 1 && s.interval == 666666, "1280x720 a 30 fps: solo hay 15 fps -> 666666");
    CHECK(uvc_choose(&c, 800, 600, 30, 0, &s) == 0 && s.fmt == 1 && c.vs[0].fmt[1].frame[s.frm].w == 640, "800x600: el tamano mas cercano");
    CHECK(uvc_choose(&c, 640, 480, 5, 0, &s) == 0 && s.interval == 1000000, "5 fps: el intervalo mas lento disponible");
    struct uvc_cfg raw = c; raw.vs[0].fmt[1].kind = UVC_FMT_UNCOMP;
    CHECK(uvc_choose(&raw, 640, 480, 30, 0, &s) != 0, "sin MJPEG y sin permiso para sin comprimir: no hay eleccion");
    CHECK(uvc_choose(&raw, 640, 480, 30, 1, &s) == 0, "con permiso para sin comprimir: se elige");

    uint8_t pr[48]; memset(pr, 0xAA, sizeof pr);
    uvc_probe_fill(pr, 26, 2, 1, 333333);
    CHECK(pr[0] == 1 && pr[1] == 0 && pr[2] == 2 && pr[3] == 1 && pr[4] == (333333 & 255) && pr[8] == 0 && pr[25] == 0, "probe: hint, formato, tamano, intervalo, resto a cero");
    pr[18] = 0x00; pr[19] = 0x10; pr[20] = 0; pr[21] = 0; pr[22] = 0x20; pr[23] = 0x03; pr[24] = 0; pr[25] = 0;
    CHECK(uvc_probe_max_frame(pr, 26) == 0x1000 && uvc_probe_max_payload(pr, 26) == 0x320, "probe: tamano de trama y de carga util");
    CHECK(uvc_pick_alt(&c.vs[0], 800) == 3 && uvc_pick_alt(&c.vs[0], 801) == 4 && uvc_pick_alt(&c.vs[0], 100) == 0 && uvc_pick_alt(&c.vs[0], 99999) == 5, "ajuste alterno: el menor que basta; si no llega, el mayor");

    /* ---- ensamblador ---- */
    static uint8_t fr1[20000], fr2[15000];
    jpeg(fr1, 20000); jpeg(fr2, 15000);
    struct uvc_asm a; uvc_asm_init(&a, b0, b1, sizeof b0, 0);
    CHECK(feed_frame(&a, fr1, 20000, 0, 3000, 1, 0) == UVC_FRAME_OK && a.done_len == 20000 && !memcmp(a.done, fr1, 20000), "trama 1 con EOF: completa e identica");
    const uint8_t *keep = a.done;
    CHECK(feed_frame(&a, fr2, 15000, 1, 1000, 1, 0) == UVC_FRAME_OK && a.done_len == 15000 && !memcmp(a.done, fr2, 15000) && a.done != keep, "trama 2: doble bufer");
    CHECK(!memcmp(keep, fr1, 20000), "la trama anterior sigue intacta en su bufer");
    /* sin EOF: la trama acaba al cambiar el FID */
    uvc_asm_init(&a, b0, b1, sizeof b0, 0);
    CHECK(feed_frame(&a, fr1, 20000, 0, 3000, 0, 0) == UVC_FRAME_NONE, "sin EOF: aun no hay trama");
    CHECK(feed_frame(&a, fr2, 15000, 1, 3000, 0, 0) == UVC_FRAME_OK && a.done_len == 20000, "el cambio de FID cierra la trama anterior");
    /* bandera de error */
    uvc_asm_init(&a, b0, b1, sizeof b0, 0);
    CHECK(feed_frame(&a, fr1, 20000, 0, 3000, 1, 1) == UVC_FRAME_BAD && a.fr_bad == 1 && a.pk_err_flag == 1, "bit ERR: la trama se descarta");
    CHECK(feed_frame(&a, fr2, 15000, 1, 3000, 1, 0) == UVC_FRAME_OK, "y la siguiente vuelve a ser buena");
    /* trama truncada (se perdio el final) */
    uvc_asm_init(&a, b0, b1, sizeof b0, 0);
    CHECK(feed_frame(&a, fr1, 12000, 0, 3000, 1, 0) == UVC_FRAME_BAD && a.fr_short == 1, "MJPEG sin EOI: se descarta");
    /* cabeceras raras, paquetes vacios */
    uint8_t em[2] = { 2, 0 }; uint8_t hx[4] = { 20, 0, 1, 2 };
    CHECK(uvc_asm_feed(&a, em, 2) == UVC_FRAME_NONE && uvc_asm_feed(&a, em, 1) == UVC_FRAME_NONE && a.pk_empty == 1, "paquete vacio y de 1 byte ignorados");
    CHECK(uvc_asm_feed(&a, hx, 4) == UVC_FRAME_NONE && a.pk_hdr_err == 1, "longitud de cabecera invalida: error de cabecera");
    /* trama mayor que el bufer */
    uint8_t small0[1000], small1[1000]; uvc_asm_init(&a, small0, small1, 1000, 0);
    CHECK(feed_frame(&a, fr1, 20000, 0, 3000, 1, 0) == UVC_FRAME_BAD && a.fr_over >= 1, "trama mayor que el bufer: descartada sin desbordar");
    /* sin comprimir: tamano exacto */
    static uint8_t y0[700000], y1[700000]; uvc_asm_init(&a, y0, y1, sizeof y0, 614400);
    static uint8_t yuv[614400]; memset(yuv, 0x80, sizeof yuv);
    CHECK(feed_frame(&a, yuv, 614400, 0, 3000, 1, 0) == UVC_FRAME_OK && a.done_len == 614400, "YUY2 completo");
    CHECK(feed_frame(&a, yuv, 600000, 1, 3000, 1, 0) == UVC_FRAME_BAD, "YUY2 corto: descartado");

    /* ---- DHT ---- */
    uint8_t dht[512]; uint32_t dl = uvc_std_dht_build(dht);
    CHECK(dl == 420 && dht[0] == 0xFF && dht[1] == 0xC4 && dht[2] == 0x01 && dht[3] == 0xA2, "DHT estandar: 420 bytes, longitud 418");
    uint32_t o = 4; int okt = 1; int ntab = 0;
    while (o < dl) {
        uint8_t th = dht[o++]; uint32_t sum = 0; for (int i = 0; i < 16; i++) sum += dht[o + i]; o += 16;
        uint8_t seen[256]; memset(seen, 0, sizeof seen);
        for (uint32_t i = 0; i < sum; i++) { uint8_t v = dht[o + i]; if (seen[v]) okt = 0; seen[v] = 1; if ((th >> 4) == 1 && ((v & 15) > 10 && v != 0xF0 && (v >> 4) == 0)) okt = 0; if ((th >> 4) == 0 && v > 11) okt = 0; }
        if ((th >> 4) == 0 && sum != 12) okt = 0;
        if ((th >> 4) == 1 && sum != 162) okt = 0;
        o += sum; ntab++;
    }
    CHECK(o == dl && ntab == 4 && okt, "cuatro tablas: cuentas = valores (12 o 162), valores sin repetir");
    uint8_t nojpg[200]; memset(nojpg, 0, sizeof nojpg);
    uint8_t j[64] = { 0xFF, 0xD8, 0xFF, 0xDB, 0x00, 0x05, 1, 2, 3, 0xFF, 0xDA, 0x00, 0x04, 0, 0, 9, 9, 9, 0xFF, 0xD9 };
    uint8_t out[600]; int32_t r = uvc_mjpeg_add_dht(j, 20, out, sizeof out);
    CHECK(r == 440 && !memcmp(out, j, 9) && out[9] == 0xFF && out[10] == 0xC4 && !memcmp(out + 9 + 420, j + 9, 11), "se inserta la DHT justo antes de SOS");
    uint8_t j2[64] = { 0xFF, 0xD8, 0xFF, 0xC4, 0x00, 0x03, 0, 0xFF, 0xDA, 0, 4, 0, 0, 0xFF, 0xD9 };
    CHECK(uvc_mjpeg_add_dht(j2, 15, out, sizeof out) == 0, "si ya trae DHT no se toca");
    CHECK(uvc_mjpeg_add_dht(nojpg, 100, out, sizeof out) == 0 && uvc_mjpeg_add_dht(j, 20, out, 100) == -1, "no es JPEG: 0; no cabe: -1");

    /* ---- YUY2 -> RGB ---- */
    uint8_t px[8] = { 128, 128, 128, 128, 255, 128, 0, 128 };
    uint32_t rgb[4]; uvc_yuy2_to_rgb(px, 4, 1, rgb, 4);
    CHECK(rgb[0] == 0x808080 && rgb[1] == 0x808080 && rgb[2] == 0xFFFFFF && rgb[3] == 0x000000, "YUY2 a RGB: grises");

    if (fails) { printf("%d fallos\n", fails); return 1; }
    printf("uvc: descriptor, eleccion, negociacion, ensamblado de tramas, DHT estandar y YUY2 correctos\n");
    return 0;
}
