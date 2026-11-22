/* SOMA_252_VIDEO_NATIVO: el reproductor (lo que no depende de donde corre). Lee un MP4 que le va llegando por el anillo de
 * la memoria compartida (vp_abi.h), decodifica H.264 y AAC (vp_vdec.c, vp_adec.c) y lleva la sincronizacion:
 *   - el reloj maestro es el del AUDIO (microsegundos que ya han sonado); sin pista de audio, el reloj del sistema;
 *   - el audio se decodifica con unos 300 ms de adelanto y se escribe mientras quepa;
 *   - cada fotograma se decodifica en cuanto se puede sobre el bufer trasero y se publica cuando el reloj llega a su
 *     instante; si va tarde se decodifica igual (hace falta como referencia) pero no se convierte a color;
 *   - si faltan bytes (red lenta) no se escribe audio: el reloj se para solo y la imagen espera ("buffering").
 * El entorno (dormir, hora, audio) llega por struct vp_env: en SOMA son llamadas al sistema (al final de vp_libc.c) y en el anfitrion
 * un nucleo simulado (tests/vplay_host_252.c). */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "vp_abi.h"
#include "mp4.h"

#define AHEAD_US     300000u          /* audio decodificado por delante del reloj */
#define EARLY_US     8000u            /* un fotograma se muestra hasta 8 ms antes de su instante */
#define LATE_US      90000u           /* con mas retraso que esto no merece la pena convertirlo */
#define FAR_BYTES    (3u << 20)       /* si lo que hace falta esta mas lejos que esto del final del anillo, se pide un salto */
#define MOOV_MAX     (7u << 20)
#ifdef VP_DEBUG
#include <stdio.h>
#define VP_DBG(...) fprintf(stderr, __VA_ARGS__)
#else
#define VP_DBG(...) do { } while (0)
#endif

static struct vp_env *E;
static struct vp_ctrl *C;
static uint8_t *ring; static uint32_t *fbuf[2];

static struct mp4_info info;
static struct mp4_sample *vs, *as; static uint32_t nv, na, vi, ai;
static uint8_t *scratch; static uint32_t scratch_cap;
static uint32_t a_rate, a_ch; static int audio_open_, audio_eof, video_eof;
static uint64_t base_us;              /* instante del video que corresponde al cero del reloj de audio */
static uint64_t wall0_ms; static uint64_t wall_paused_ms;   /* reloj sin audio */
static uint64_t target_us;            /* tras un salto: los fotogramas anteriores no se muestran */
static int pend; static uint64_t pend_pts; static uint32_t pend_w, pend_h; static int back;
static uint64_t fed_us;               /* audio escrito, en microsegundos desde base_us */
static int16_t *mix; static uint32_t mix_cap;
static const int16_t *pcm_left; static uint32_t pcm_left_n;   /* PCM decodificado que aun no ha cabido en la cola del nucleo */

static void fail(const char *m)
{
    uint32_t i = 0; for (; m[i] && i + 1 < sizeof C->err; ++i) C->err[i] = m[i];
    C->err[i] = 0; C->state = VP_ST_ERROR;
}

/* Pide al nucleo que siga la descarga desde "off" y espera a que lo haga. 0 = hay que salir. */
static int refeed(uint64_t off)
{
    C->in_keep = off; C->want_off = off; C->want_seq = C->want_seq + 1;
    uint32_t s = C->want_seq;
    while (C->feed_seq != s) { if (C->quit) return 0; E->sleep_ms(4); }
    return 1;
}
/* Los bytes [off, off+len) del archivo en dst. 1 = copiados; 0 = aun no han llegado; -1 = no llegaran (error o fuera del archivo). */
static int in_read(uint64_t off, uint32_t len, uint8_t *dst)
{
    uint64_t base = C->in_base, end = C->in_end, fs = C->file_size;
    if (fs && off + len > fs) return -1;
    if (off < base || off > end + FAR_BYTES) {                 /* lejos de lo que hay: salto de la descarga */
        if (!refeed(off)) return -1;
        return 0;
    }
    if (off + len > end) return C->feed_error ? -1 : 0;
    uint32_t p = (uint32_t)(off % VP_IN_CAP), first = VP_IN_CAP - p < len ? VP_IN_CAP - p : len;
    memcpy(dst, ring + p, first);
    if (first < len) memcpy(dst + first, ring, len - first);
    return 1;
}
static int in_wait(uint64_t off, uint32_t len, uint8_t *dst)   /* como in_read pero espera; 1 o -1 */
{
    for (;;) {
        int r = in_read(off, len, dst);
        if (r) return r;
        if (C->quit) return -1;
        E->sleep_ms(5);
    }
}
static uint8_t *need_scratch(uint32_t n)
{
    if (n > scratch_cap) {
        uint32_t nc = n + n / 2 + 4096; uint8_t *nb = (uint8_t *)malloc(nc);
        if (!nb) return 0;
        free(scratch); scratch = nb; scratch_cap = nc;
    }
    return scratch;
}

/* ------------------------------------------------------------------ apertura: indice (moov) y decodificadores */
static int load(void)
{
    while (!C->file_size) { if (C->quit || C->feed_error) { fail("No llega el video"); return 0; } E->sleep_ms(5); }
    uint64_t fs = C->file_size, at = 0, moff = 0, msize = 0;
    static uint8_t head[4096];
    for (int hops = 0; ; ++hops) {
        uint32_t want = fs - at < sizeof head ? (uint32_t)(fs - at) : (uint32_t)sizeof head;
        if (hops > 32 || want < 8) { fail("El archivo no es un MP4 que SOMA sepa reproducir"); return 0; }
        C->in_keep = at;
        if (in_wait(at, want, head) < 0) { fail("La descarga del video se ha cortado"); return 0; }
        int r = mp4_find_moov(head, want, at, fs, &moff, &msize);
        if (r < 0) { fail("El archivo no es un MP4 que SOMA sepa reproducir"); return 0; }
        if (r == 1) break;
        if (moff <= at) { fail("MP4 no valido"); return 0; }
        at = moff;
    }
    if (msize > MOOV_MAX) { fail("El indice del video es demasiado grande"); return 0; }
    uint8_t *moov = (uint8_t *)malloc((size_t)msize);
    if (!moov) { fail("Sin memoria para el indice del video"); return 0; }
    for (uint64_t got = 0; got < msize;) {                     /* a trozos: el anillo puede ser menor que el indice */
        uint32_t n = msize - got > 65536 ? 65536u : (uint32_t)(msize - got);
        C->in_keep = moff + got;
        if (in_wait(moff + got, n, moov + got) < 0) { fail("La descarga del video se ha cortado"); return 0; }
        got += n;
    }
    if (!mp4_parse_moov(moov, msize, &info)) { fail("El video no tiene pistas H.264 o AAC"); return 0; }
    if (info.v.present && C->mode == VP_MODE_PLAY) {
        if (info.v.width > VP_MAX_W || info.v.height > VP_MAX_H) { fail("El video es mayor de 854x480: SOMA no lo reproduce todavia"); return 0; }
        vs = (struct mp4_sample *)malloc(sizeof *vs * ((size_t)info.v.nsamples + 1));
        if (!vs) { fail("Sin memoria"); return 0; }
        nv = mp4_build(&info.v, vs, info.v.nsamples);
        uint8_t hdr[1024]; uint32_t nal = 0, hn = mp4_avcc(info.v.cfg.p, info.v.cfg.n, hdr, sizeof hdr, &nal);
        if (!nv || !hn || !vd_open(hdr, hn, nal, info.v.width, info.v.height)) { fail("No se puede abrir el video (H.264)"); return 0; }
    }
    if (info.a.present) {
        as = (struct mp4_sample *)malloc(sizeof *as * ((size_t)info.a.nsamples + 1));
        if (!as) { fail("Sin memoria"); return 0; }
        na = mp4_build(&info.a, as, info.a.nsamples);
        const uint8_t *asc = 0; uint32_t an = mp4_esds_asc(info.a.cfg.p, info.a.cfg.n, &asc);
        if (!na || !an || !ad_open(asc, an, &a_rate, &a_ch)) { na = 0; }                       /* sin sonido, pero se ve */
    }
    if (!nv && !na) { fail(C->mode == VP_MODE_AUDIO ? "El video no tiene sonido AAC" : "El video no tiene pistas H.264 o AAC"); return 0; }
    if (C->mode == VP_MODE_AUDIO && !na) { fail("El video no tiene sonido AAC"); return 0; }
    C->has_video = nv != 0; C->has_audio = na != 0; C->dur_ms = (uint32_t)info.duration_ms;
    if (C->mode == VP_MODE_PLAY) { C->w = info.v.width; C->h = info.v.height; }
    /* el indice (moov) queda en memoria: las tablas de muestras ya estan copiadas en vs/as, pero cfg apunta dentro */
    return 1;
}

/* ------------------------------------------------------------------ reloj */
static uint64_t drained_ms;           /* cuando se acabo el audio y la cola quedo vacia (0 = aun suena) */
static uint64_t clock_us(void)
{
    if (audio_open_) {
        uint64_t fr, us;
        if (E->audio_status(&fr, &us) == 0) {
            if (audio_eof && !pcm_left_n && us + 20000u >= fed_us) {            /* el video sigue tras el final del audio: reloj del sistema */
                if (!drained_ms) drained_ms = E->now_ms();
                return base_us + fed_us + (E->now_ms() - drained_ms) * 1000u;
            }
            return base_us + us;
        }
    }
    if (na && !audio_eof) return base_us;                                       /* el audio aun no ha arrancado: el reloj espera */
    return base_us + (E->now_ms() - wall0_ms - wall_paused_ms) * 1000u;
}

static void seek_to(uint64_t t_us)
{
    uint64_t dur = info.duration_ms * 1000u;
    if (dur && t_us > dur) t_us = dur > 500000u ? dur - 500000u : 0;
    /* el video arranca en la clave anterior a t (se decodifica sin mostrar hasta llegar a t); el audio y el reloj, en t */
    if (nv) { vi = mp4_seek_index(vs, nv, t_us, 1); vd_reset(); if (vi == 0 || !na) t_us = vs[vi].pts_us; }
    base_us = t_us;
    if (na) { ai = mp4_seek_index(as, na, t_us, 0); if (as[ai].pts_us < t_us && ai + 1 < na) ai++; base_us = as[ai].pts_us; }
    target_us = t_us; pend = 0; fed_us = 0; drained_ms = 0; audio_eof = na == 0; video_eof = nv == 0; pcm_left_n = 0;
    if (audio_open_) { E->audio_close(); audio_open_ = 0; }
    wall0_ms = E->now_ms(); wall_paused_ms = 0;
    uint64_t lo = ~0ULL;
    if (nv && vi < nv) lo = vs[vi].off;
    if (na && ai < na && as[ai].off < lo) lo = as[ai].off;
    if (lo != ~0ULL) C->in_keep = lo;
    C->pos_ms = (uint32_t)(base_us / 1000u);
    C->state = VP_ST_PLAYING;
}

/* ------------------------------------------------------------------ audio: hasta AHEAD_US por delante del reloj */
static int audio_start(void)                                    /* el decodificador ya sabe frecuencia y canales */
{
    if (!ad_format(&a_rate, &a_ch)) return 0;
    if (E->audio_open(a_rate) != 0) { na = 0; audio_eof = 1; C->has_audio = 0; return 0; }      /* frecuencia no admitida: sin sonido */
    audio_open_ = 1; C->audio_rate = a_rate;
    return 1;
}
static int audio_step(uint64_t now)                             /* 1 = ha hecho algo; -1 = faltan bytes */
{
    if (audio_eof) return 0;
    if (pcm_left_n) {                                           /* lo que no cupo antes */
        uint64_t fr = 0, us = 0;
        if (E->audio_status(&fr, &us) != 0 || fr < 4) return 0;
        uint32_t n = pcm_left_n < fr ? pcm_left_n : (uint32_t)(fr & ~3ULL);
        int64_t w = E->audio_write(pcm_left, n);
        if (w <= 0) return 0;
        pcm_left += (uint32_t)w / 2; pcm_left_n -= (uint32_t)w;
        return 1;
    }
    if (ai >= na) { audio_eof = 1; return 0; }
    if (base_us + fed_us > now + AHEAD_US) return 0;
    uint8_t *b = need_scratch(as[ai].size + 16);
    if (!b) { audio_eof = 1; return 0; }
    int r = in_read(as[ai].off, as[ai].size, b);
    if (r == 0) return -1;
    if (r < 0) { audio_eof = 1; return 0; }
    const int16_t *pcm = 0; uint32_t bytes = ad_decode(b, as[ai].size, &pcm);
    ai++;
    if (!bytes) { if (ai > 2) C->errors = C->errors + 1; VP_DBG("audio: cuadro %u sin salida\n", ai - 1); return 1; }   /* los primeros cuadros pueden no dar salida */
    if (!audio_open_ && !audio_start()) return 1;
    uint32_t frames = bytes / (2u * a_ch);
    if (a_ch != 2) {                                            /* la cola del nucleo es estereo */
        if (frames * 2 > mix_cap) { free(mix); mix_cap = frames * 2 + 4096; mix = (int16_t *)malloc(sizeof(int16_t) * mix_cap); if (!mix) { mix_cap = 0; return 1; } }
        for (uint32_t i = 0; i < frames; ++i) {
            if (a_ch == 1) { mix[2 * i] = mix[2 * i + 1] = pcm[i]; }
            else { mix[2 * i] = pcm[(size_t)i * a_ch]; mix[2 * i + 1] = pcm[(size_t)i * a_ch + 1]; }
        }
        pcm = mix;
    }
    fed_us += (uint64_t)frames * 1000000u / a_rate;
    pcm_left = pcm; pcm_left_n = frames * 4;
    return 1;
}

/* ------------------------------------------------------------------ video */
static void publish(void)
{
    C->w = pend_w; C->h = pend_h; C->frame_buf = (uint32_t)back; C->frame_seq = C->frame_seq + 1;
    C->frames_shown = C->frames_shown + 1;
    back ^= 1; pend = 0;
}
static int video_step(uint64_t now)                             /* 1 = ha hecho algo; -1 = faltan bytes */
{
    if (pend) {
        if (now + EARLY_US >= pend_pts) { publish(); return 1; }
        return 0;
    }
    if (video_eof) return 0;
    int flush = vi >= nv; uint8_t *b = 0;
    if (!flush) {
        b = need_scratch(vs[vi].size + 16);
        if (!b) { video_eof = 1; return 0; }
        int r = in_read(vs[vi].off, vs[vi].size, b);
        if (r == 0) return -1;
        if (r < 0) { video_eof = 1; return 0; }
    }
    /* se convierte a color solo si se va a ver: ni antes del punto del salto ni con mucho retraso */
    uint64_t pts_guess = flush ? now : vs[vi].pts_us;
    int show = pts_guess >= target_us && (pts_guess + LATE_US >= now || vi + 1 >= nv);
    int got = 0; uint32_t ts = 0, w = 0, h = 0;
    int ok = flush ? vd_decode(0, 0, 0, fbuf[back], info.v.width, &got, &ts, &w, &h)
                   : vd_decode(b, vs[vi].size, vi, show ? fbuf[back] : 0, info.v.width, &got, &ts, &w, &h);
    if (!ok && !flush) { C->errors = C->errors + 1; VP_DBG("video: error en la muestra %u\n", vi); }
    if (!flush) vi++;
    if (got && ts < nv) {
        uint64_t pts = vs[ts].pts_us;
        if (pts >= target_us && (show || flush)) { pend = 1; pend_pts = pts; pend_w = w; pend_h = h; }
        else if (pts >= target_us) { C->frames_late = C->frames_late + 1; VP_DBG("video: tarde ts %u\n", ts); }
    } else if (got) { VP_DBG("video: ts raro %u\n", ts);
    } else if (flush && !got) video_eof = 1;
    return 1;
}

/* ------------------------------------------------------------------ SOMA_253: solo audio -> PCM 16 kHz mono */
/* Reduccion por promedio de area (cada muestra de salida es la media del tramo de entrada que le toca): filtra lo que sobra por
 * encima de 8 kHz sin necesitar un filtro aparte, y con 48000 o 44100 Hz de entrada es mas que suficiente para voz. */
struct vp_rs { uint32_t rate; uint32_t fill; int64_t acc; };
static uint32_t rs_run(struct vp_rs *r, const int16_t *pcm, uint32_t frames, uint32_t ch, int16_t *out, uint32_t outmax)
{
    uint32_t no = 0;
    for (uint32_t i = 0; i < frames; ++i) {
        int32_t x = 0;
        if (ch == 1) x = pcm[i];
        else { x = ((int32_t)pcm[(size_t)i * ch] + (int32_t)pcm[(size_t)i * ch + 1]) / 2; }
        uint32_t left = VP_OUT_RATE;                              /* cada muestra de entrada "dura" 16000 unidades; cada de salida, "rate" */
        while (left) {
            uint32_t take = r->rate - r->fill; if (take > left) take = left;
            r->acc += (int64_t)x * take; r->fill += take; left -= take;
            if (r->fill == r->rate) {
                if (no < outmax) out[no++] = (int16_t)(r->acc / (int64_t)r->rate);
                r->acc = 0; r->fill = 0;
            }
        }
    }
    return no;
}
static int out_put(const int16_t *s, uint32_t n)                 /* al anillo de salida; 0 = hay que salir */
{
    uint32_t bytes = n * 2u, done = 0;
    uint8_t *ob = (uint8_t *)C + VP_FR_OFF;
    const uint8_t *sb = (const uint8_t *)s;
    while (done < bytes) {
        uint64_t w = C->out_w, r = C->out_r;
        uint64_t room = r <= w && w - r <= VP_OUT_CAP ? VP_OUT_CAP - (w - r) : 0;
        if (room < 2) { if (C->quit) return 0; E->sleep_ms(4); continue; }
        uint32_t c = bytes - done; if ((uint64_t)c > room) c = (uint32_t)room;
        uint32_t p = (uint32_t)(w % VP_OUT_CAP), first = VP_OUT_CAP - p < c ? VP_OUT_CAP - p : c;
        memcpy(ob + p, sb + done, first);
        if (first < c) memcpy(ob, sb + done + first, c - first);
        done += c;
        C->out_w = w + c;
    }
    return 1;
}
static int vp_extract(void)
{
    C->state = VP_ST_LOADING;
    if (!load()) { while (!C->quit) E->sleep_ms(20); return 1; }
    uint64_t limit_us = C->out_max_ms ? (uint64_t)C->out_max_ms * 1000u : 0, pcm_us = 0;
    struct vp_rs rs; memset(&rs, 0, sizeof rs);
    static int16_t ob[8192];
    C->state = VP_ST_PLAYING; C->out_ms = 0;
    uint32_t n_ok = 0;
    for (ai = 0; ai < na && !C->quit; ++ai) {
        if (limit_us && as[ai].pts_us >= limit_us) break;
        uint8_t *b = need_scratch(as[ai].size + 16);
        if (!b) { fail("Sin memoria"); break; }
        C->in_keep = as[ai].off;
        int r = in_wait(as[ai].off, as[ai].size, b);
        if (r < 0) { if (!n_ok) fail("La descarga del video se ha cortado"); break; }       /* se corto con algo hecho: se entrega lo que hay */
        const int16_t *pcm = 0; uint32_t bytes = ad_decode(b, as[ai].size, &pcm);
        if (!bytes) { if (ai > 2) C->errors = C->errors + 1; continue; }
        if (!ad_format(&a_rate, &a_ch)) continue;
        if (!rs.rate) rs.rate = a_rate;
        uint32_t frames = bytes / (2u * a_ch);
        for (uint32_t f0 = 0; f0 < frames; ) {                    /* a trozos: la salida cabe en ob */
            uint32_t nf = frames - f0 > 4096u ? 4096u : frames - f0;
            uint32_t no = rs_run(&rs, pcm + (size_t)f0 * a_ch, nf, a_ch, ob, 8192);
            if (no && !out_put(ob, no)) goto fin;
            f0 += nf;
        }
        n_ok++;
        pcm_us += (uint64_t)frames * 1000000u / a_rate;
        C->out_ms = (uint32_t)(pcm_us / 1000u); C->pos_ms = C->out_ms;
        if ((n_ok & 31u) == 0) E->sleep_ms(1);                    /* deja respirar al resto del sistema */
    }
fin:
    if (C->state == VP_ST_ERROR) { while (!C->quit) E->sleep_ms(20); return 1; }
    if (!n_ok) { fail("No se ha podido decodificar el sonido del video"); while (!C->quit) E->sleep_ms(20); return 1; }
    C->state = VP_ST_ENDED;
    while (!C->quit) E->sleep_ms(15);
    return 0;
}

int vp_run(struct vp_env *env, void *shared)
{
    E = env; C = (struct vp_ctrl *)shared; ring = (uint8_t *)shared + VP_IN_OFF;
    fbuf[0] = (uint32_t *)((uint8_t *)shared + VP_FR_OFF); fbuf[1] = (uint32_t *)((uint8_t *)shared + VP_FR_OFF + VP_FR_BYTES);
    while (C->magic != VP_MAGIC) { E->sleep_ms(5); }
    if (C->mode == VP_MODE_AUDIO) return vp_extract();
    C->state = VP_ST_LOADING;
    if (!load()) { while (!C->quit) E->sleep_ms(20); return 1; }
    seek_to(0);
    uint64_t pause_t0 = 0; int was_paused = 0;
    while (!C->quit) {
        if (C->cmd_seq != C->cmd_ack) {
            uint32_t s = C->cmd_seq;
            if (C->cmd == VP_CMD_SEEK) seek_to((uint64_t)C->cmd_arg * 1000u);
            C->cmd_ack = s;
            was_paused = 0;
        }
        if (C->state == VP_ST_ERROR) { E->sleep_ms(20); continue; }
        if (C->paused) {
            if (!was_paused) { was_paused = 1; pause_t0 = E->now_ms(); }
            E->sleep_ms(12); continue;
        }
        if (was_paused) { was_paused = 0; wall_paused_ms += E->now_ms() - pause_t0; }
        uint64_t now = clock_us();
        int a = audio_step(now), v = video_step(now);
        int starving = (a < 0 && (video_eof || v <= 0)) || (v < 0 && a <= 0 && !pend);
        if (C->feed_error && (a < 0 || v < 0)) { if (a < 0) audio_eof = 1; if (v < 0) video_eof = 1; }
        C->buffering = starving && !C->feed_error;
        /* lo que el nucleo puede soltar del anillo: por debajo de la siguiente muestra de cada pista */
        uint64_t lo = ~0ULL;
        if (!video_eof && vi < nv) lo = vs[vi].off;
        if (!audio_eof && ai < na && as[ai].off < lo) lo = as[ai].off;
        if (lo != ~0ULL && lo > C->in_keep) C->in_keep = lo;
        uint64_t dur = info.duration_ms;
        uint32_t pos = (uint32_t)(now / 1000u); if (dur && pos > dur) pos = (uint32_t)dur;
        C->pos_ms = pos;
        if (audio_eof && video_eof && !pend && !pcm_left_n) {
            int drained = 1;
            if (audio_open_) { uint64_t fr, us; drained = E->audio_status(&fr, &us) != 0 || us + 30000u >= fed_us; }
            if (drained || C->feed_error) { C->state = VP_ST_ENDED; if (dur) C->pos_ms = (uint32_t)dur; E->sleep_ms(15); continue; }
        }
        if (a <= 0 && v <= 0) E->sleep_ms(starving ? 8 : 2);
    }
    if (audio_open_) E->audio_close();
    return 0;
}
