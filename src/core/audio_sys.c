/* SOMA_PARCHE_020_AUDIO: pegamento del audio con el kernel. Ver audio_sys.h */
#include <stdint.h>
#include <stddef.h>
#include "audio_sys.h"
#include "audio.h"
#include "wav.h"
#include "hda.h"
#include "micrec.h"
#include "bigfs_glue.h"
#include "thread.h"
#include "mutex.h"
#include "mem.h"
#include "console.h"
#include "idt.h"

#define FIFO_BYTES (256 * 1024)
static uint8_t g_fifo[FIFO_BYTES];
static struct audio g_audio;
static struct kmutex g_lock;
static int g_ready, g_open;
static uint32_t g_owner;
static volatile int g_stop;
static uint32_t g_rate; static audio_tap_fn g_tap;      /* SOMA_179_T480S: copia de lo que suena (captura de pantalla) */
static volatile int g_playing;            /* hay un reproductor en segundo plano (comando audio tone/test/play) */

static const int16_t SINE[256] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602,
    6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285,
    32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
    30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683,
    27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
    23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868,
    18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
    12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179,
    6393, 5602, 4808, 4011, 3212, 2410, 1608, 804,
    0, -804, -1608, -2410, -3212, -4011, -4808, -5602,
    -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
    -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
    -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
    -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
    -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179,
    -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804,
};

static int ob_s(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; o[*p < max ? *p : max - 1] = 0; return 0; }
static void ob_d(char *o, uint32_t max, uint32_t *p, uint64_t v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } ob_s(o, max, p, b + i); }
static int str_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* ---- hilo audiod: bombea cada pocos ms mientras hay un flujo abierto ---- */
static void audiod_main(void *arg)
{
    (void)arg;
    for (;;) {
        if (g_open) { kmutex_lock(&g_lock); if (g_open) audio_pump(&g_audio); kmutex_unlock(&g_lock); }
        thread_sleep(g_open ? 4 : 25);                          /* cerrado: casi no gasta CPU */
    }
}

void audio_sys_init(void)
{
    kmutex_init(&g_lock, "audio_lock");
    if (hda_init() != 0) return;
    audio_init(&g_audio, hda_hw(), g_fifo, sizeof g_fifo);
    g_ready = 1;
    thread_create("audiod", audiod_main, 0);
    micrec_init();                                              /* SOMA_177_T480S: grabadora del microfono (si hay entrada de captura) */
}

int audio_sys_available(void) { return g_ready; }

int audio_sys_open(uint32_t rate, uint32_t owner_tid)
{
    if (!g_ready) return -19;
    uint16_t f; if (hda_format_for_rate(rate, &f) != 0) return -22;
    kmutex_lock(&g_lock);
    int r;
    if (g_open) r = -16;
    else { r = audio_open(&g_audio, rate) == 0 ? 0 : -22; if (r == 0) { g_open = 1; g_owner = owner_tid; g_rate = rate; } }
    kmutex_unlock(&g_lock);
    return r;
}

int audio_sys_write(const void *pcm, uint32_t n)
{
    if (!g_open) return -9;
    const uint8_t *p = (const uint8_t *)pcm;
    uint32_t done = 0;
    uint64_t stall_since = timer_get_uptime_ms();
    n &= ~(AUDIO_FRAME - 1);
    while (done < n && g_open && !g_stop) {
        kmutex_lock(&g_lock);
        uint32_t k = audio_write(&g_audio, p + done, n - done);
        uint32_t queued = k ? (uint32_t)sizeof g_fifo - audio_space(&g_audio) : 0;
        kmutex_unlock(&g_lock);
        if (k) { audio_tap_fn tap = g_tap; if (tap) tap((const int16_t *)(p + done), (k & ~(AUDIO_FRAME - 1)) / AUDIO_FRAME, g_rate, queued / AUDIO_FRAME); }
        if (k) { done += k; stall_since = timer_get_uptime_ms(); }
        else {
            if (timer_get_uptime_ms() - stall_since > 3000) break;      /* el hardware no avanza */
            thread_sleep(2);
        }
    }
    return (int)done;
}

int audio_sys_status(uint64_t out[3])
{
    if (!g_open) return -9;
    kmutex_lock(&g_lock);
    out[0] = audio_space(&g_audio); out[1] = audio_media_us(&g_audio); out[2] = g_audio.st.underruns;
    kmutex_unlock(&g_lock);
    return 0;
}

uint32_t audio_sys_flush(void)
{
    if (!g_open) return 0;
    kmutex_lock(&g_lock);
    uint32_t n = audio_flush(&g_audio);
    kmutex_unlock(&g_lock);
    return n;
}

uint32_t audio_sys_fifo_bytes(void) { return sizeof g_fifo; }
void audio_sys_set_tap(audio_tap_fn f) { g_tap = f; }          /* SOMA_179_T480S */

void audio_sys_close(int drain)
{
    if (!g_open) return;
    if (drain) {
        uint64_t t0 = timer_get_uptime_ms();
        for (;;) {
            kmutex_lock(&g_lock); int idle = audio_idle(&g_audio); kmutex_unlock(&g_lock);
            if (idle || g_stop || timer_get_uptime_ms() - t0 > 20000) break;
            thread_sleep(5);
        }
        thread_sleep(100);                                       /* la ultima porcion aun esta en el DMA y en los bufers del dispositivo */
    }
    kmutex_lock(&g_lock);
    audio_close(&g_audio); g_open = 0; g_owner = 0;
    kmutex_unlock(&g_lock);
}

void audio_sys_owner_died(uint32_t tid)
{
    if (g_open && g_owner == tid) audio_sys_close(0);
}

/* ---- reproductores en segundo plano ---- */
struct job { int kind; uint32_t hz, ms, rate; char path[96]; };
static struct job g_job;

/* patron determinista de la prueba (lo reproduce tools/wavcheck.py): L(i) = ((i*37) mod 20001) - 10000, R(i) = -L(i) */
static void fill_test(int16_t *dst, uint32_t frames, uint32_t first)
{
    for (uint32_t i = 0; i < frames; ++i) {
        int32_t l = (int32_t)(((uint64_t)(first + i) * 37u) % 20001u) - 10000;
        dst[2 * i] = (int16_t)l; dst[2 * i + 1] = (int16_t)-l;
    }
}

static void print_stats(const char *what)
{
    struct audio_stats st; audio_get_stats(&g_audio, &st);
    char b[160]; uint32_t p = 0;
    ob_s(b, sizeof b, &p, what); ob_s(b, sizeof b, &p, ": terminado; reproducidos ");
    ob_d(b, sizeof b, &p, st.played_bytes); ob_s(b, sizeof b, &p, " B, datos ");
    ob_d(b, sizeof b, &p, st.data_bytes); ob_s(b, sizeof b, &p, " B, infrautilizaciones ");
    ob_d(b, sizeof b, &p, st.underruns); ob_s(b, sizeof b, &p, "\n");
    kprint(b);
}

static void job_main(void *arg)
{
    (void)arg;
    struct job j = g_job;
    int16_t *buf = (int16_t *)kmalloc(16384);
    if (!buf) { kprint("audio: sin memoria\n"); g_playing = 0; return; }
    g_stop = 0;

    if (j.kind == 1 || j.kind == 2) {                           /* 1 = tono, 2 = patron de prueba */
        if (audio_sys_open(j.rate, 0xFFFF) != 0) { kprint("audio: no se pudo abrir (ocupado o sin dispositivo)\n"); kfree(buf); g_playing = 0; return; }
        uint64_t total = (uint64_t)j.rate * j.ms / 1000, done = 0, phase = 0;
        uint64_t step = ((uint64_t)j.hz << 32) / j.rate;
        while (done < total && !g_stop) {
            uint32_t frames = (uint32_t)(total - done < 4096 ? total - done : 4096);
            if (j.kind == 2) fill_test(buf, frames, (uint32_t)done);
            else for (uint32_t i = 0; i < frames; ++i) {
                int16_t v = (int16_t)(((int32_t)SINE[(phase >> 24) & 0xFF] * 12000) / 32767);
                buf[2 * i] = v; buf[2 * i + 1] = v; phase += step;
            }
            if (audio_sys_write(buf, frames * AUDIO_FRAME) != (int)(frames * AUDIO_FRAME)) break;
            done += frames;
        }
        audio_sys_close(!g_stop);
        print_stats(j.kind == 2 ? "audio test" : "audio tone");
    } else if (j.kind == 3) {                                   /* reproducir un WAV del almacen BIGFS */
        int e; struct bigfs *fs = bigfs_sys(&e);
        uint32_t ino; struct bigfs_stat st;
        uint8_t *rd = (uint8_t *)kmalloc(8192);
        int t = fs ? bigfs_lookup(fs, j.path, &ino) : e;
        if (!fs || !rd || t != (int)BIGFS_T_FILE || bigfs_stat(fs, ino, &st) < 0) { kprint("audio play: no se puede abrir el archivo\n"); }
        else {
            int64_t g = bigfs_read(fs, ino, 0, rd, 4096);
            struct wav_info w;
            int r = g > 0 ? wav_parse(rd, (size_t)g, &w) : WAV_E_SHORT;
            if (r != WAV_OK) { kprint("audio play: "); kprint(wav_strerror(r)); kprint("\n"); }
            else if (audio_sys_open(w.rate, 0xFFFF) != 0) kprint("audio play: no se pudo abrir el dispositivo (ocupado, sin dispositivo o frecuencia no soportada)\n");
            else {
                uint64_t size = w.data_size; if (w.data_offset + size > st.size || size == 0) size = st.size - w.data_offset;
                uint64_t off = 0; int ok = 1;
                while (off < size && !g_stop && ok) {
                    uint32_t want = (uint32_t)(size - off < 8192 ? size - off : 8192);
                    int64_t n = bigfs_read(fs, ino, w.data_offset + off, rd, want);
                    if (n <= 0) break;
                    uint32_t bytes = (uint32_t)n;
                    if (w.channels == 2) { bytes &= ~3u; if (bytes) ok = audio_sys_write(rd, bytes) == (int)bytes; }
                    else {                                      /* mono -> estereo: cada muestra se duplica */
                        uint32_t samples = bytes / 2;
                        for (uint32_t i = 0; i < samples; ++i) { buf[2 * i] = ((int16_t *)rd)[i]; buf[2 * i + 1] = ((int16_t *)rd)[i]; }
                        if (samples) ok = audio_sys_write(buf, samples * 4) == (int)(samples * 4);
                        bytes = samples * 2;
                    }
                    off += bytes ? bytes : want;
                }
                audio_sys_close(!g_stop);
                print_stats("audio play");
            }
        }
        if (rd) kfree(rd);
    }
    kfree(buf);
    g_playing = 0;
}

static int start_job(int kind, uint32_t hz, uint32_t ms, uint32_t rate, const char *path)
{
    if (!g_ready) return -19;
    if (g_playing || g_open) return -16;
    g_playing = 1;
    g_job.kind = kind; g_job.hz = hz; g_job.ms = ms; g_job.rate = rate;
    uint32_t i = 0; if (path) for (; path[i] && i < sizeof g_job.path - 1; ++i) g_job.path[i] = path[i];
    g_job.path[i] = 0;
    if (!thread_create("audioplay", job_main, 0)) { g_playing = 0; return -12; }
    return 0;
}

static const char *next_tok(const char **p, char *buf, uint32_t max)
{
    while (**p == ' ') (*p)++;
    uint32_t n = 0;
    while (**p && **p != ' ' && n + 1 < max) buf[n++] = *(*p)++;
    buf[n] = 0;
    return n ? buf : 0;
}
static uint32_t num(const char *s) { uint32_t v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0'); return v; }

int audio_sys_command(const char *args, char *out, uint32_t max)
{
    uint32_t p = 0; if (max) out[0] = 0;
    char sub[16], a1[100], a2[16], a3[16];
    const char *q = args ? args : "";
    if (!next_tok(&q, sub, sizeof sub)) { ob_s(out, max, &p, "uso: audio info | tone [hz] [ms] | test | play <ruta.wav> | stop | vol <0-100> | stat"); kprint(out); kprint("\n"); return 1; }
    int rc = 0;
    if (str_eq(sub, "info") || str_eq(sub, "stat")) {
        char info[512]; hda_info(info, sizeof info); ob_s(out, max, &p, info);
        if (g_ready) {
            struct audio_stats st; kmutex_lock(&g_lock); audio_get_stats(&g_audio, &st); uint32_t qd = audio_queued(&g_audio); kmutex_unlock(&g_lock);
            ob_s(out, max, &p, g_open ? ", abierto" : ", cerrado"); ob_s(out, max, &p, ", reproducidos "); ob_d(out, max, &p, st.played_bytes);
            ob_s(out, max, &p, " B, en cola "); ob_d(out, max, &p, qd); ob_s(out, max, &p, " B, infrautilizaciones "); ob_d(out, max, &p, st.underruns);
        }
    } else if (str_eq(sub, "tone")) {
        uint32_t hz = 440, ms = 1000;
        if (next_tok(&q, a1, sizeof a1)) hz = num(a1);
        if (next_tok(&q, a2, sizeof a2)) ms = num(a2);
        if (hz < 20 || hz > 20000) hz = 440;
        if (ms < 50) ms = 50;
        if (ms > 60000) ms = 60000;
        rc = start_job(1, hz, ms, 44100, 0);
        ob_s(out, max, &p, rc == 0 ? "audio tone: reproduciendo en segundo plano" : "audio tone: no se puede (ocupado o sin dispositivo)");
    } else if (str_eq(sub, "test")) {
        rc = start_job(2, 0, 1000, 44100, 0);
        ob_s(out, max, &p, rc == 0 ? "audio test: patron de 1 s (44100 Hz) en segundo plano" : "audio test: no se puede (ocupado o sin dispositivo)");
    } else if (str_eq(sub, "play")) {
        if (!next_tok(&q, a1, sizeof a1)) ob_s(out, max, &p, "uso: audio play <ruta del almacen, ej. /musica/a.wav>");
        else { rc = start_job(3, 0, 0, 0, a1); ob_s(out, max, &p, rc == 0 ? "audio play: reproduciendo en segundo plano" : "audio play: no se puede (ocupado o sin dispositivo)"); }
    } else if (str_eq(sub, "stop")) {
        g_stop = 1; ob_s(out, max, &p, "audio: detenido");
    } else if (str_eq(sub, "vol")) {
        if (!next_tok(&q, a3, sizeof a3)) ob_s(out, max, &p, "uso: audio vol <0-100>");
        else { uint32_t v = num(a3); if (v > 100) v = 100; hda_set_volume((int)v); ob_s(out, max, &p, "volumen "); ob_d(out, max, &p, v); ob_s(out, max, &p, " %"); }
    } else ob_s(out, max, &p, "subcomando desconocido");
    (void)a2;
    kprint(out); kprint("\n");
    return 1;
}
