/* SOMA_PARCHE_023_MEDIA: ver media.h
 *
 * Reparto del trabajo:
 *   - hilo mediad: abre el archivo del almacen BIGFS, decodifica (jpeg.c / avi.c / wav.c, los mismos que usa el reproductor
 *     de usuario vidplay), alimenta la FIFO de audio sin bloquear y publica el ultimo fotograma en un buffer doble;
 *   - la ventana (GUI, ~30 fps): dibuja el buffer publicado escalado al area de video, la lista de archivos y los controles.
 * Sincronizacion: reloj maestro = reloj de audio (bytes de DATOS ya sonados); sin audio o cuando este se acaba, el tiempo
 * del sistema anclado donde se quedo. Un fotograma sale cuando reloj >= pts - 10 ms; los que van > 60 ms tarde se saltan
 * sin decodificar (el ultimo siempre se muestra). El planificador del kernel es cooperativo: el hilo cede la CPU en cada
 * espera y en cada lectura de disco; la ventana y el hilo nunca se ejecutan a la vez, por eso no hacen falta cerrojos. */
#include "media.h"
#include "fb.h"
#include "gui_font.h"
#include "mem.h"
#include "thread.h"
#include "idt.h"
#include "console.h"
#include "bigfs.h"
#include "bigfs_glue.h"
#include "audio_sys.h"
#include "avi.h"
#include "jpeg.h"
#include "wav.h"

#define MAXF        24
#define MAXPIX      (1280u * 720u)
#define VBUF_BYTES  (256u * 1024u)
#define LATE_US     60000u
#define EARLY_US    10000u
#define LEAD_MS     400u            /* audio que se mantiene por delante en la FIFO */
#define IDLE_STOP_MS 2500u          /* sin dibujar la ventana durante tanto tiempo: se asume cerrada */

enum { ST_IDLE = 0, ST_LOADING, ST_PLAYING, ST_PAUSED, ST_ENDED, ST_ERROR };

struct entry { char path[BIGFS_PATH_MAX + 1]; uint8_t kind; uint64_t size; };   /* kind 1 = AVI, 2 = WAV */

static struct entry files[MAXF];
static int nfiles, sel, list_scroll, scanned;

/* ---- estado compartido hilo <-> ventana ---- */
static volatile int state = ST_IDLE;
static volatile int thread_alive;
static volatile int cmd_stop, cmd_pause, cmd_open;
static volatile int64_t cmd_seek_us = -1;
static char cmd_path[BIGFS_PATH_MAX + 1];
static char cur_path[BIGFS_PATH_MAX + 1];
static char msg[112];
static uint32_t *fbuf[2];
static uint32_t fbuf_px;
static volatile int front = -1;
static uint32_t fw, fh;
static volatile uint64_t pos_us, dur_us;
static uint32_t st_frames, st_shown, st_dropped, st_bad, st_late_ms, st_gaps;
static uint64_t st_audio_bytes;
static volatile uint64_t last_render_ms;
static volatile int gui_seen;                /* la ventana se ha dibujado alguna vez: solo entonces vale la deteccion de 'ventana cerrada' */
static int is_audio_only;
static uint8_t vu[48];                       /* niveles del ultimo audio para la visualizacion de los WAV */
static int vu_n;

/* ---- helpers ---- */
static void scopy(char *d, const char *s, uint32_t max) { uint32_t i = 0; while (s && s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static uint32_t slen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static char lc(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static int has_ext(const char *n, const char *ext)
{
    uint32_t a = slen(n), b = slen(ext);
    if (a < b) return 0;
    for (uint32_t i = 0; i < b; ++i) if (lc(n[a - b + i]) != ext[i]) return 0;
    return 1;
}
static void setmsg(const char *a, const char *b)
{
    uint32_t p = 0;
    while (a && *a && p + 1 < sizeof msg) msg[p++] = *a++;
    while (b && *b && p + 1 < sizeof msg) msg[p++] = *b++;
    msg[p] = 0;
}
static void put_dec(char *o, uint32_t max, uint32_t *p, uint64_t v)
{
    char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0';
    while (v) { b[--i] = (char)('0' + v % 10); v /= 10; }
    while (b[i] && *p + 1 < max) o[(*p)++] = b[i++];
    o[*p] = 0;
}
static void put_str(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; o[*p] = 0; }
static void fmt_time(char *o, uint64_t us)
{
    uint32_t s = (uint32_t)(us / 1000000u), m = s / 60; s %= 60;
    if (m > 99) m = 99;
    o[0] = (char)('0' + m / 10); o[1] = (char)('0' + m % 10); o[2] = ':'; o[3] = (char)('0' + s / 10); o[4] = (char)('0' + s % 10); o[5] = 0;
}

/* ---- lista de archivos (BIGFS, hasta 4 niveles) ---- */
static void scan_dir(struct bigfs *fs, uint32_t dir, const char *prefix, int depth)
{
    uint32_t cur = 0; struct bigfs_stat st;
    while (nfiles < MAXF && bigfs_readdir(fs, dir, &cur, &st) == 1) {
        char path[BIGFS_PATH_MAX + 1]; uint32_t p = 0;
        put_str(path, sizeof path, &p, prefix); put_str(path, sizeof path, &p, "/"); put_str(path, sizeof path, &p, st.name);
        if (st.type == BIGFS_T_DIR) { if (depth < 4) scan_dir(fs, st.ino, path, depth + 1); }
        else if (st.type == BIGFS_T_FILE && (has_ext(st.name, ".avi") || has_ext(st.name, ".wav"))) {
            scopy(files[nfiles].path, path, sizeof files[nfiles].path);
            files[nfiles].kind = has_ext(st.name, ".avi") ? 1 : 2;
            files[nfiles].size = st.size;
            nfiles++;
        }
    }
}

static void rescan(void)
{
    nfiles = 0; scanned = 1;
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) { setmsg("Almacen BIGFS no disponible (usa 'big format yes')", ""); return; }
    uint32_t root;
    if (bigfs_lookup(fs, "/", &root) != (int)BIGFS_T_DIR) return;
    scan_dir(fs, root, "", 0);
    if (sel >= nfiles) sel = nfiles ? nfiles - 1 : 0;
}

/* ---- sesion de reproduccion (hilo) ---- */
static struct bigfs *fs_; static uint32_t ino_; static uint64_t fsize_;
static struct avi_info av; static struct wav_info wv;
static int audio_on, audio_done, video_done, wall_mode;
static uint32_t a_rate, a_ch, a_fin;                 /* a_fin = bytes de entrada por trama (mono 2, estereo 4) */
static uint64_t a_pos;                               /* bytes de entrada leidos del flujo de audio (incluye el resto) */
static uint64_t aoff, achunk_off, achunk_rem;
static unsigned char carry[4]; static int ncarry;
static uint64_t voff; static uint32_t next_idx;
static int cn_valid; static uint64_t cn_here; static uint32_t cn_size, cn_idx;
static uint64_t A0_us;                               /* tiempo del flujo de audio en el instante en que se abrio el dispositivo */
static uint64_t wall0_ms, wall_base_us;              /* ancla del reloj de pared */
static uint64_t last_audio_us;
static unsigned char *araw; static short *aout; static unsigned char *vbuf, *hdrbuf;

static int rd(uint64_t off, void *buf, uint32_t n) { return bigfs_read(fs_, ino_, off, buf, n) == (int64_t)n; }

static void free_buffers(void)
{
    front = -1;
    for (int i = 0; i < 2; ++i) if (fbuf[i]) { kfree(fbuf[i]); fbuf[i] = 0; }
    fbuf_px = 0; fw = fh = 0;
}

static int next_audio_chunk(void)
{
    while (aoff + 8 <= av.movi_end) {
        unsigned char h[8]; struct avi_chunk c;
        if (!rd(aoff, h, 8)) return 0;
        avi_chunk_parse(h, &c);
        uint64_t here = aoff;
        aoff = avi_chunk_next(aoff, &c);
        if (c.kind == AVI_K_AUDIO && c.stream == av.audio_stream && c.size > 0 && here + 8 + c.size <= av.movi_end) {
            achunk_off = here + 8; achunk_rem = c.size; return 1;
        }
    }
    return 0;
}

/* Coloca el cursor del flujo de audio en el byte `target` (de entrada, multiplo de la trama). */
static void audio_cursor_set(uint64_t target)
{
    ncarry = 0; audio_done = 0; achunk_rem = 0;
    if (is_audio_only) {
        uint64_t total = wv.data_size;
        if (target >= total) { audio_done = 1; a_pos = total; return; }
        achunk_off = wv.data_offset + target; achunk_rem = total - target; a_pos = target; return;
    }
    uint64_t acc = 0; aoff = av.movi_start;
    while (aoff + 8 <= av.movi_end) {
        unsigned char h[8]; struct avi_chunk c;
        if (!rd(aoff, h, 8)) break;
        avi_chunk_parse(h, &c);
        uint64_t here = aoff; aoff = avi_chunk_next(aoff, &c);
        if (c.kind == AVI_K_AUDIO && c.stream == av.audio_stream && here + 8 + c.size <= av.movi_end) {
            if (acc + c.size > target) { uint64_t sk = target - acc; achunk_off = here + 8 + sk; achunk_rem = c.size - sk; a_pos = target; return; }
            acc += c.size;
        }
    }
    audio_done = 1; a_pos = acc;
}

static void vu_update(const short *s, uint32_t frames)
{
    vu_n = 48;
    for (int b = 0; b < 48; ++b) {
        uint32_t lo = (uint32_t)((uint64_t)frames * (uint32_t)b / 48u), hi = (uint32_t)((uint64_t)frames * (uint32_t)(b + 1) / 48u);
        int m = 0;
        for (uint32_t i = lo; i < hi; ++i) { int l = s[2 * i], r = s[2 * i + 1]; if (l < 0) l = -l; if (r < 0) r = -r; if (l > m) m = l; if (r > m) m = r; }
        vu[b] = (uint8_t)(m >> 7 > 255 ? 255 : m >> 7);
    }
}

/* Mete en la FIFO lo que quepa hasta mantener LEAD_MS por delante (sin bloquear). */
static void feed_audio(void)
{
    if (!audio_on || audio_done) return;
    uint64_t st[3];
    if (audio_sys_status(st) != 0) return;
    uint32_t fifo = audio_sys_fifo_bytes();
    uint64_t lead = (uint64_t)a_rate * 4u * LEAD_MS / 1000u;
    uint64_t queued = fifo > st[0] ? fifo - st[0] : 0;
    uint64_t space = st[0];
    uint32_t guard = 0;
    while (queued < lead && space >= 8192 && guard++ < 16) {
        if (achunk_rem == 0 && !next_audio_chunk()) { audio_done = 1; break; }
        uint64_t want = achunk_rem < 8192 ? achunk_rem : 8192;
        if (a_ch == 1 && want > 4096) want = 4096;
        for (int i = 0; i < ncarry; ++i) araw[i] = carry[i];
        uint64_t got;
        if (is_audio_only) { got = bigfs_read(fs_, ino_, achunk_off, araw + ncarry, want) == (int64_t)want ? want : 0; }
        else got = rd(achunk_off, araw + ncarry, (uint32_t)want) ? want : 0;
        if (!got) { audio_done = 1; break; }
        achunk_off += got; achunk_rem -= got; a_pos += got;
        uint64_t have = got + (uint64_t)ncarry, use = have - have % a_fin;
        ncarry = (int)(have - use);
        for (int i = 0; i < ncarry; ++i) carry[i] = araw[use + (uint64_t)i];
        if (!use) continue;
        const void *out; uint64_t ob;
        if (a_ch == 2) { out = araw; ob = use; }
        else {
            uint64_t n = use / 2;
            for (uint64_t i = 0; i < n; ++i) { aout[2 * i] = ((short *)araw)[i]; aout[2 * i + 1] = ((short *)araw)[i]; }
            out = aout; ob = n * 4;
        }
        if (is_audio_only) vu_update((const short *)out, (uint32_t)(ob / 4));
        if ((uint64_t)audio_sys_write(out, (uint32_t)ob) != ob) { audio_done = 1; break; }
        st_audio_bytes += ob; queued += ob; space = space > ob ? space - ob : 0;
    }
    if (audio_sys_status(st) == 0) { last_audio_us = st[1]; st_gaps = (uint32_t)st[2]; }
}

static uint64_t clock_us(void)
{
    uint64_t now = timer_get_uptime_ms();
    if (audio_on && !wall_mode) {
        uint64_t st[3];
        if (audio_sys_status(st) == 0) last_audio_us = st[1];
        return A0_us + last_audio_us;
    }
    return wall_base_us + (now - wall0_ms) * 1000u;
}

static void clock_to_wall(void)         /* el audio dejo de alimentarse: se sigue con el tiempo del sistema desde donde estaba */
{
    if (wall_mode) return;
    uint64_t c = clock_us();
    wall_base_us = c; wall0_ms = timer_get_uptime_ms(); wall_mode = 1;
}

static int next_video_chunk(void)
{
    if (cn_valid) return 1;
    while (voff + 8 <= av.movi_end) {
        unsigned char h[8]; struct avi_chunk c;
        if (!rd(voff, h, 8)) { voff = av.movi_end; return 0; }
        avi_chunk_parse(h, &c);
        uint64_t here = voff; voff = avi_chunk_next(voff, &c);
        if (c.kind == AVI_K_VIDEO && c.stream == av.video_stream) {
            if (c.size && here + 8 + c.size > av.movi_end) { voff = av.movi_end; return 0; }
            cn_here = here; cn_size = c.size; cn_idx = next_idx++; cn_valid = 1; return 1;
        }
    }
    return 0;
}

static void video_cursor_set(uint32_t idx)
{
    voff = av.movi_start; next_idx = 0; cn_valid = 0; video_done = 0;
    while (next_idx < idx && next_video_chunk()) cn_valid = 0;
}

static void audio_reopen_at(uint64_t t_us)      /* (re)abre el dispositivo y coloca los cursores en el instante t */
{
    if (audio_on) { audio_sys_close(0); audio_on = 0; }
    wall_mode = 0;
    if (a_rate) {
        uint64_t frames = t_us * a_rate / 1000000u;
        audio_cursor_set(frames * a_fin);
        A0_us = frames * 1000000u / a_rate;
        last_audio_us = 0;
        if (audio_sys_open(a_rate, 0xFFFEu) == 0) audio_on = 1;
        else { audio_on = 0; }
    }
    if (!audio_on) { wall_base_us = t_us; wall0_ms = timer_get_uptime_ms(); wall_mode = 1; audio_done = 1; }
}

static void apply_pause_flush(void)             /* pausa: descarta la FIFO y rebobina el cursor lo descartado */
{
    if (!audio_on) return;
    uint32_t d = audio_sys_flush();
    uint64_t din = a_ch == 1 ? d / 2u : d;
    uint64_t fed_in = a_pos - (uint64_t)ncarry;
    uint64_t target = fed_in > din ? fed_in - din : 0;
    audio_cursor_set(target - target % a_fin);
}

/* Devuelve 1 si hay que dejar la sesion (parar / abrir otro); atiende pausa y saltos. */
static int service_commands(int *paused_state)
{
    if (cmd_stop || cmd_open) return 1;
    if (gui_seen && timer_get_uptime_ms() - last_render_ms > IDLE_STOP_MS) { setmsg("Ventana cerrada: reproduccion detenida", ""); return 1; }
    if (cmd_pause != *paused_state) {
        *paused_state = cmd_pause;
        if (cmd_pause) {
            state = ST_PAUSED;
            if (audio_on && !wall_mode) apply_pause_flush();
            else { wall_base_us = clock_us(); }
        } else {
            state = ST_PLAYING;
            wall0_ms = timer_get_uptime_ms();
            if (wall_mode) { /* wall_base_us ya guardado */ }
        }
    }
    if (cmd_seek_us >= 0) {
        int64_t t = cmd_seek_us; cmd_seek_us = -1;
        uint64_t d = dur_us;
        if (d && (uint64_t)t >= d) t = (int64_t)(d > 1000000u ? d - 1000000u : 0);
        if (!is_audio_only) video_cursor_set((uint32_t)((uint64_t)t / av.usec_per_frame));
        audio_reopen_at((uint64_t)t);
        if (!audio_on) { wall_base_us = (uint64_t)t; }
        video_done = 0;
        pos_us = (uint64_t)t;
        if (cmd_pause) { wall_base_us = (uint64_t)t; }
    }
    return 0;
}

static void print_stats(const char *why)
{
    char b[300]; uint32_t p = 0;
    put_str(b, sizeof b, &p, "[media] "); put_str(b, sizeof b, &p, cur_path); put_str(b, sizeof b, &p, ": ");
    put_str(b, sizeof b, &p, why); put_str(b, sizeof b, &p, ", ");
    put_dec(b, sizeof b, &p, st_frames); put_str(b, sizeof b, &p, " fotogramas, "); put_dec(b, sizeof b, &p, st_shown); put_str(b, sizeof b, &p, " mostrados, ");
    put_dec(b, sizeof b, &p, st_dropped); put_str(b, sizeof b, &p, " saltados, "); put_dec(b, sizeof b, &p, st_bad); put_str(b, sizeof b, &p, " malos, retraso max ");
    put_dec(b, sizeof b, &p, st_late_ms); put_str(b, sizeof b, &p, " ms, audio "); put_dec(b, sizeof b, &p, st_audio_bytes);
    put_str(b, sizeof b, &p, " bytes, huecos "); put_dec(b, sizeof b, &p, st_gaps); put_str(b, sizeof b, &p, "\n");
    kprint(b);
}

static void run_session(const char *path)
{
    state = ST_LOADING; setmsg("Abriendo...", "");
    cmd_stop = 0; cmd_open = 0; cmd_seek_us = -1;
    st_frames = st_shown = st_dropped = st_bad = st_late_ms = st_gaps = 0; st_audio_bytes = 0;
    pos_us = dur_us = 0; vu_n = 0; is_audio_only = 0;
    audio_on = audio_done = video_done = wall_mode = 0; ncarry = 0; cn_valid = 0; next_idx = 0;
    free_buffers();
    scopy(cur_path, path, sizeof cur_path);

    int e; fs_ = bigfs_sys(&e);
    if (!fs_) { setmsg("BIGFS no disponible", ""); state = ST_ERROR; return; }
    if (bigfs_lookup(fs_, path, &ino_) != (int)BIGFS_T_FILE) { setmsg("No se encuentra el archivo", ""); state = ST_ERROR; return; }
    struct bigfs_stat st;
    if (bigfs_stat(fs_, ino_, &st) < 0) { setmsg("No se puede leer el archivo", ""); state = ST_ERROR; return; }
    fsize_ = st.size;

    hdrbuf = (unsigned char *)kmalloc(65536); araw = (unsigned char *)kmalloc(16384 + 8);
    aout = (short *)kmalloc(16384 * 2 + 16); vbuf = (unsigned char *)kmalloc(VBUF_BYTES);
    if (!hdrbuf || !araw || !aout || !vbuf) { setmsg("Sin memoria", ""); state = ST_ERROR; goto done; }
    int64_t n = bigfs_read(fs_, ino_, 0, hdrbuf, 65536);
    if (n <= 0) { setmsg("Archivo vacio", ""); state = ST_ERROR; goto done; }
    int r = avi_parse(hdrbuf, (size_t)n, &av);
    if (r == AVI_OK) {
        if ((uint64_t)av.movi_end > fsize_) av.movi_end = (uint32_t)fsize_;
        if ((uint64_t)av.width * av.height > MAXPIX) { setmsg("Video demasiado grande (maximo 1280x720)", ""); state = ST_ERROR; goto done; }
        fbuf_px = av.width * av.height;
        fbuf[0] = (uint32_t *)kmalloc((size_t)fbuf_px * 4); fbuf[1] = (uint32_t *)kmalloc((size_t)fbuf_px * 4);
        if (!fbuf[0] || !fbuf[1]) { setmsg("Sin memoria para los fotogramas", ""); state = ST_ERROR; goto done; }
        fw = av.width; fh = av.height; front = -1;
        dur_us = (uint64_t)av.total_frames * av.usec_per_frame;
        a_rate = av.has_audio ? av.rate : 0; a_ch = av.channels; a_fin = a_ch == 2 ? 4u : 2u;
        voff = av.movi_start; aoff = av.movi_start;
    } else {
        int wr = wav_parse(hdrbuf, (size_t)n, &wv);
        if (wr != WAV_OK) { setmsg("Formato no soportado: ", r == AVI_E_FORMAT ? wav_strerror(wr) : avi_strerror(r)); state = ST_ERROR; goto done; }
        is_audio_only = 1;
        uint64_t sz = wv.data_size;
        if (sz == 0 || wv.data_offset + sz > fsize_) sz = fsize_ - wv.data_offset;
        wv.data_size = (uint32_t)sz;
        a_rate = wv.rate; a_ch = wv.channels; a_fin = a_ch == 2 ? 4u : 2u;
        dur_us = (uint64_t)(sz / a_fin) * 1000000u / a_rate;
    }
    audio_reopen_at(0);
    if (!audio_on && a_rate) setmsg("Sin audio (dispositivo ocupado o ausente): se reproduce en silencio", "");
    else setmsg("", "");
    if (is_audio_only && !audio_on) { setmsg("No se puede abrir el audio", ""); state = ST_ERROR; goto done; }
    feed_audio();
    state = ST_PLAYING;

    {
        int paused = 0; uint32_t last_idx = 0; int have_frame = 0, back = 0;
        for (;;) {
            if (service_commands(&paused)) break;
            if (paused) { thread_sleep(15); continue; }
            feed_audio();
            if (is_audio_only) {
                uint64_t c = clock_us();
                pos_us = c;
                if (audio_done && c >= dur_us) break;
                if (audio_done && audio_on) { clock_to_wall(); }
                thread_sleep(10);
                continue;
            }
            if (audio_done && audio_on && !wall_mode) clock_to_wall();
            if (video_done) {
                if (audio_done) break;
                thread_sleep(10); pos_us = clock_us(); continue;
            }
            if (!next_video_chunk()) { video_done = 1; continue; }
            uint64_t pts = (uint64_t)cn_idx * av.usec_per_frame;
            uint64_t clk = clock_us();
            if (clk + EARLY_US < pts) {                  /* aun no es la hora: se espera un poco y se vuelve a mirar */
                uint64_t w = (pts - clk - EARLY_US) / 1000u;
                thread_sleep(w > 4 ? 4 : (w ? (uint32_t)w : 1));
                pos_us = clk;
                continue;
            }
            cn_valid = 0; st_frames++;
            int late = clk > pts + LATE_US;
            if (late && have_frame) { st_dropped++; continue; }
            if (cn_size == 0) { st_shown++; continue; }
            if (cn_size > VBUF_BYTES || !rd(cn_here + 8, vbuf, cn_size)) { st_bad++; continue; }
            struct jpeg_info ji;
            int jr = jpeg_decode_isolated(vbuf, cn_size, fbuf[back], fw, fbuf_px / fw, &ji);   /* PARCHE 139: en ring 3 */
            if (jr != JPEG_OK || ji.width != fw || ji.height != fh) { st_bad++; continue; }
            front = back; back ^= 1; have_frame = 1; st_shown++; last_idx = cn_idx;
            uint64_t now = clock_us();
            if (now > pts && (now - pts) / 1000u > st_late_ms) st_late_ms = (uint32_t)((now - pts) / 1000u);
            pos_us = now;
        }
        (void)last_idx;
    }
    if (!cmd_stop && !cmd_open && state != ST_ERROR) {
        /* final natural: se deja sonar lo que quede y se mantiene el ultimo fotograma */
        if (audio_on) { audio_sys_close(1); audio_on = 0; }
        pos_us = dur_us;
        state = ST_ENDED;
        setmsg("", "");
        print_stats("fin");
    } else {
        if (audio_on) { audio_sys_close(0); audio_on = 0; }
        print_stats(cmd_stop ? "parado" : (cmd_open ? "sustituido" : "interrumpido"));
        if (cmd_stop || (gui_seen && timer_get_uptime_ms() - last_render_ms > IDLE_STOP_MS)) { free_buffers(); state = ST_IDLE; pos_us = 0; }
    }
done:
    if (audio_on) { audio_sys_close(0); audio_on = 0; }
    if (hdrbuf) { kfree(hdrbuf); hdrbuf = 0; }
    if (araw) { kfree(araw); araw = 0; }
    if (aout) { kfree(aout); aout = 0; }
    if (vbuf) { kfree(vbuf); vbuf = 0; }
}

static void mediad_main(void *arg)
{
    (void)arg;
    for (;;) {
        char p[BIGFS_PATH_MAX + 1];
        scopy(p, cmd_path, sizeof p);
        cmd_pause = 0;
        run_session(p);
        if (!cmd_open) break;
    }
    thread_alive = 0;
}

/* ---- API ---- */
int media_play(const char *path)
{
    if (!path || !*path || slen(path) > BIGFS_PATH_MAX) return -22;
    scopy(cmd_path, path, sizeof cmd_path);
    last_render_ms = timer_get_uptime_ms();
    if (thread_alive) { cmd_open = 1; cmd_pause = 0; return 0; }
    cmd_stop = 0; cmd_open = 0; cmd_pause = 0;
    thread_alive = 1;
    state = ST_LOADING;
    if (!thread_create("mediad", mediad_main, 0)) { thread_alive = 0; state = ST_ERROR; setmsg("No se pudo crear el hilo", ""); return -12; }
    return 0;
}

void media_pause_toggle(void)
{
    if (!thread_alive) { if (state == ST_ENDED || state == ST_IDLE) { if (nfiles) media_play(files[sel].path); } return; }
    cmd_pause = !cmd_pause;
}

void media_stop(void)
{
    if (thread_alive) { cmd_stop = 1; cmd_pause = 0; return; }
    free_buffers(); state = ST_IDLE; pos_us = 0; setmsg("", "");
}

void media_seek_abs(uint32_t sec) { if (thread_alive) cmd_seek_us = (int64_t)sec * 1000000; }
void media_seek_rel(int32_t sec)
{
    if (!thread_alive) return;
    int64_t t = (int64_t)pos_us + (int64_t)sec * 1000000;
    if (t < 0) t = 0;
    cmd_seek_us = t;
}

void media_init(void)
{
    last_render_ms = timer_get_uptime_ms();
    rescan();
}

/* ---- ventana ---- */
struct lay { int lx, ly, lw, lh, vx, vy, vw, vh, py, px, pw, by, status_y; };

static void make_layout(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, struct lay *L)
{
    L->lw = ww >= 700 ? 220 : (ww >= 520 ? 150 : 0);
    L->lx = (int)wx + 8; L->ly = (int)wy + 8; L->lh = (int)wh - 16;
    int vx0 = (int)wx + 8 + (L->lw ? L->lw + 8 : 0);
    L->vx = vx0; L->vy = (int)wy + 8;
    L->vw = (int)wx + (int)ww - 8 - vx0; L->vh = (int)wh - 8 - 8 - 78;
    if (L->vh < 40) L->vh = 40;
    if (L->vw < 40) L->vw = 40;
    L->px = L->vx; L->pw = L->vw; L->py = L->vy + L->vh + 10;
    L->by = L->py + 18;
    L->status_y = L->by + 34;
}

#define BTN_W 74
#define BTN_H 26

void media_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int focused)
{
    (void)focused;
    last_render_ms = timer_get_uptime_ms(); gui_seen = 1;
    if (!scanned) rescan();
    struct lay L; make_layout(wx, wy, ww, wh, &L);

    fb_fill_rect(wx, wy, ww, wh, 0x000F172A);

    /* lista de archivos */
    if (L.lw) {
        fb_fill_rounded_rect(L.lx, L.ly, L.lw, L.lh, 6, 0x00161F30);
        fb_draw_rounded_rect(L.lx, L.ly, L.lw, L.lh, 6, 0x00334155);
        font_draw_string((uint32_t)L.lx + 10, (uint32_t)L.ly + 8, "BIBLIOTECA", 0x00FB7185, 1);
        int rows = (L.lh - 36) / 20;
        if (rows < 1) rows = 1;
        if (nfiles == 0) {
            font_draw_string((uint32_t)L.lx + 10, (uint32_t)L.ly + 34, "Sin .avi ni .wav.", 0x0094A3B8, 1);
            font_draw_string((uint32_t)L.lx + 10, (uint32_t)L.ly + 50, "Copialos con", 0x0064748B, 1);
            font_draw_string((uint32_t)L.lx + 10, (uint32_t)L.ly + 66, "tools/bigfs.py", 0x0064748B, 1);
        }
        if (sel < list_scroll) list_scroll = sel;
        if (sel >= list_scroll + rows) list_scroll = sel - rows + 1;
        for (int r = 0; r < rows && list_scroll + r < nfiles; ++r) {
            int i = list_scroll + r; int y = L.ly + 30 + r * 20;
            if (i == sel) fb_fill_rounded_rect(L.lx + 4, y - 2, L.lw - 8, 18, 4, 0x00334155);
            const char *nm = files[i].path; uint32_t l = slen(nm), k = l;
            while (k && nm[k - 1] != '/') k--;
            char buf[28]; uint32_t cap = (uint32_t)(L.lw - 36) / 8; if (cap > 26) cap = 26;
            uint32_t bl = 0; for (uint32_t q = k; q < l && bl < cap; ++q) buf[bl++] = nm[q]; buf[bl] = 0;
            int playing = (state == ST_PLAYING || state == ST_PAUSED || state == ST_LOADING) && seq(cur_path, files[i].path);
            font_draw_string((uint32_t)L.lx + 10, (uint32_t)y + 2, playing ? ">" : (files[i].kind == 1 ? "v" : "~"), playing ? 0x00FB7185 : 0x0064748B, 1);
            font_draw_string((uint32_t)L.lx + 24, (uint32_t)y + 2, buf, i == sel ? 0x00F8FAFC : 0x00CBD5E1, 1);
        }
    }

    /* area de video */
    fb_fill_rect((uint32_t)L.vx, (uint32_t)L.vy, (uint32_t)L.vw, (uint32_t)L.vh, 0x00000000);
    fb_draw_rect((uint32_t)L.vx, (uint32_t)L.vy, (uint32_t)L.vw, (uint32_t)L.vh, 0x00334155);
    if (front >= 0 && fbuf[front] && fw && fh) {
        int aw = L.vw - 4, ah = L.vh - 4;
        uint32_t dw = (uint32_t)aw, dh = (uint32_t)((uint64_t)aw * fh / fw);
        if (dh > (uint32_t)ah) { dh = (uint32_t)ah; dw = (uint32_t)((uint64_t)ah * fw / fh); }
        if (dw >= fw && dh >= fh && dw < fw * 2) { dw = fw; dh = fh; }      /* sin ampliar un poco: se ve nitido */
        int dx = L.vx + 2 + (aw - (int)dw) / 2, dy = L.vy + 2 + (ah - (int)dh) / 2;
        fb_blit_scale(dx, dy, (int32_t)dw, (int32_t)dh, fbuf[front], (int32_t)fw, (int32_t)fh, (int32_t)fw);
    } else if (is_audio_only && (state == ST_PLAYING || state == ST_PAUSED) && vu_n) {
        int bw = (L.vw - 20) / 48; if (bw < 2) bw = 2;
        int base = L.vy + L.vh / 2;
        for (int b = 0; b < vu_n; ++b) {
            int h = (int)vu[b] * (L.vh / 2 - 10) / 255; if (h < 2) h = 2;
            fb_fill_rect((uint32_t)(L.vx + 10 + b * bw), (uint32_t)(base - h), (uint32_t)(bw - 1), (uint32_t)(2 * h), 0x00FB7185);
        }
    } else {
        const char *t = state == ST_LOADING ? "Cargando..." : (state == ST_ERROR ? "Error" : "SOMA Media");
        font_draw_string((uint32_t)(L.vx + L.vw / 2 - (int)slen(t) * 4), (uint32_t)(L.vy + L.vh / 2 - 8), t, 0x0064748B, 1);
    }

    /* progreso */
    fb_fill_rounded_rect(L.px, L.py, L.pw, 8, 3, 0x001E293B);
    uint64_t pu = pos_us, du = dur_us;
    if (du) {
        int fillw = (int)((uint64_t)L.pw * (pu > du ? du : pu) / du);
        if (fillw > 0) fb_fill_rounded_rect(L.px, L.py, fillw < 6 ? 6 : fillw, 8, 3, 0x00FB7185);
    }

    /* botones */
    int bx = L.px;
    const char *pl = state == ST_PLAYING ? "PAUSA" : "PLAY";
    fb_fill_rounded_rect(bx, L.by, BTN_W, BTN_H, 5, 0x00BE123C); fb_draw_rounded_rect(bx, L.by, BTN_W, BTN_H, 5, 0x00FB7185);
    font_draw_string((uint32_t)(bx + BTN_W / 2 - (int)slen(pl) * 4), (uint32_t)L.by + 9, pl, 0x00FFFFFF, 1);
    bx += BTN_W + 8;
    static const char *const lab[3] = { "STOP", "-10s", "+10s" };
    for (int b = 0; b < 3; ++b) {
        fb_fill_rounded_rect(bx, L.by, 56, BTN_H, 5, 0x001E293B); fb_draw_rounded_rect(bx, L.by, 56, BTN_H, 5, 0x0064748B);
        font_draw_string((uint32_t)(bx + 28 - (int)slen(lab[b]) * 4), (uint32_t)L.by + 9, lab[b], 0x00F8FAFC, 1);
        bx += 64;
    }
    char tb[16], tb2[16]; fmt_time(tb, pu); fmt_time(tb2, du);
    char tt[16]; uint32_t p = 0; put_str(tt, sizeof tt, &p, tb); put_str(tt, sizeof tt, &p, du ? " / " : ""); if (du) put_str(tt, sizeof tt, &p, tb2);
    font_draw_string((uint32_t)(L.px + L.pw - (int)slen(tt) * 8), (uint32_t)L.by + 9, tt, 0x00CBD5E1, 1);

    /* linea de estado */
    char sl[128]; p = 0;
    if (msg[0]) put_str(sl, sizeof sl, &p, msg);
    else if (state == ST_PLAYING || state == ST_PAUSED || state == ST_ENDED) {
        put_str(sl, sizeof sl, &p, state == ST_PAUSED ? "Pausa  " : (state == ST_ENDED ? "Fin  " : ""));
        if (!is_audio_only) { put_dec(sl, sizeof sl, &p, st_shown); put_str(sl, sizeof sl, &p, " fotogramas, "); put_dec(sl, sizeof sl, &p, st_dropped); put_str(sl, sizeof sl, &p, " saltados, retraso max "); put_dec(sl, sizeof sl, &p, st_late_ms); put_str(sl, sizeof sl, &p, " ms"); }
        else { put_str(sl, sizeof sl, &p, "audio "); put_dec(sl, sizeof sl, &p, a_rate); put_str(sl, sizeof sl, &p, " Hz"); }
        put_str(sl, sizeof sl, &p, ", huecos de audio "); put_dec(sl, sizeof sl, &p, st_gaps);
    } else put_str(sl, sizeof sl, &p, "Elige un archivo de la biblioteca (clic) y pulsa PLAY. Teclas: espacio, s, ',' '.', n, p");
    uint32_t maxc = (ww - 16) / 8; if (p > maxc) sl[maxc] = 0;
    font_draw_string((uint32_t)L.px, (uint32_t)L.status_y, sl, msg[0] && state == ST_ERROR ? 0x00F87171 : 0x0094A3B8, 1);
}

static int inside(int32_t x, int32_t y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

int media_handle_click(int32_t mx, int32_t my, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    struct lay L; make_layout(wx, wy, ww, wh, &L);
    if (L.lw && inside(mx, my, L.lx, L.ly + 28, L.lw, L.lh - 28)) {
        int i = list_scroll + (my - (L.ly + 30)) / 20;
        if (my >= L.ly + 28 && i >= 0 && i < nfiles) {
            sel = i;
            media_play(files[i].path);
        }
        return 0;
    }
    int bx = L.px;
    if (inside(mx, my, bx, L.by, BTN_W, BTN_H)) {
        if (state == ST_PLAYING || state == ST_PAUSED) media_pause_toggle();
        else if (nfiles) media_play(files[sel].path);
        return 0;
    }
    bx += BTN_W + 8;
    if (inside(mx, my, bx, L.by, 56, BTN_H)) { media_stop(); return 0; }
    bx += 64;
    if (inside(mx, my, bx, L.by, 56, BTN_H)) { media_seek_rel(-10); return 0; }
    bx += 64;
    if (inside(mx, my, bx, L.by, 56, BTN_H)) { media_seek_rel(10); return 0; }
    if (dur_us && inside(mx, my, L.px, L.py - 6, L.pw, 20)) {
        uint64_t t = dur_us * (uint64_t)(mx - L.px) / (uint64_t)L.pw;
        if (thread_alive) cmd_seek_us = (int64_t)t;
        return 0;
    }
    return 0;
}

void media_handle_key(char c)
{
    if (c == ' ') media_pause_toggle();
    else if (c == 's' || c == 'S') media_stop();
    else if (c == ',') media_seek_rel(-10);
    else if (c == '.') media_seek_rel(10);
    else if (c == 'n' && nfiles) { sel = (sel + 1) % nfiles; media_play(files[sel].path); }
    else if (c == 'p' && nfiles) { sel = (sel + nfiles - 1) % nfiles; media_play(files[sel].path); }
    else if (c == 'r') rescan();
}

/* ---- comando de shell: media gui|list|play|pause|stop|seek|status ---- */
int media_command(const char *args, char *out, uint32_t max)
{
    uint32_t p = 0;
    while (*args == ' ') args++;
    char verb[12]; uint32_t v = 0;
    while (*args && *args != ' ' && v < sizeof verb - 1) verb[v++] = *args++;
    verb[v] = 0;
    while (*args == ' ') args++;
    if (seq(verb, "list")) {
        rescan();
        put_dec(out, max, &p, (uint64_t)nfiles); put_str(out, max, &p, " archivo(s) de audio/video en BIGFS\n");
        for (int i = 0; i < nfiles; ++i) { put_str(out, max, &p, files[i].kind == 1 ? "  [AVI] " : "  [WAV] "); put_str(out, max, &p, files[i].path); put_str(out, max, &p, "\n"); }
    } else if (seq(verb, "play")) {
        int r = media_play(args);
        put_str(out, max, &p, r == 0 ? "media: reproduciendo en segundo plano (media status)\n" : "media: ruta no valida\n");
    } else if (seq(verb, "pause")) { media_pause_toggle(); put_str(out, max, &p, "media: pausa/continuar\n"); }
    else if (seq(verb, "stop")) { media_stop(); put_str(out, max, &p, "media: parado\n"); }
    else if (seq(verb, "seek")) {
        uint32_t s = 0; while (*args >= '0' && *args <= '9') s = s * 10 + (uint32_t)(*args++ - '0');
        media_seek_abs(s); put_str(out, max, &p, "media: salto pedido\n");
    } else if (seq(verb, "status")) {
        static const char *const names[] = { "inactivo", "cargando", "reproduciendo", "en pausa", "terminado", "error" };
        put_str(out, max, &p, "media: "); put_str(out, max, &p, names[state]); put_str(out, max, &p, "  ");
        put_str(out, max, &p, cur_path); put_str(out, max, &p, "\n  pos ");
        put_dec(out, max, &p, pos_us / 1000u); put_str(out, max, &p, " ms de "); put_dec(out, max, &p, dur_us / 1000u);
        put_str(out, max, &p, " ms, fotogramas "); put_dec(out, max, &p, st_frames); put_str(out, max, &p, ", mostrados "); put_dec(out, max, &p, st_shown);
        put_str(out, max, &p, ", saltados "); put_dec(out, max, &p, st_dropped); put_str(out, max, &p, ", malos "); put_dec(out, max, &p, st_bad);
        put_str(out, max, &p, ", retraso max "); put_dec(out, max, &p, st_late_ms); put_str(out, max, &p, " ms, audio ");
        put_dec(out, max, &p, st_audio_bytes); put_str(out, max, &p, " bytes, huecos "); put_dec(out, max, &p, st_gaps);
        if (msg[0]) { put_str(out, max, &p, "\n  "); put_str(out, max, &p, msg); }
        put_str(out, max, &p, "\n");
    } else return 0;       /* 'gui' lo gestiona el shell (abre la ventana) */
    kprint(out);
    return 1;
}
