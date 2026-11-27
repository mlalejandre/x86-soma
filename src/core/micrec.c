/* SOMA_177_T480S: motor de grabacion del microfono. Ver micrec.h */
#include "acceso.h"      /* SOMA_363_ACCESO */
#include "fs.h"
#include <stdint.h>
#include <stddef.h>
#include "micrec.h"
#include "hda.h"
#include "audio_sys.h"
#include "wav.h"
#include "bigfs.h"
#include "bigfs_glue.h"
#include "thread.h"
#include "mem.h"
#include "console.h"
#include "idt.h"

#define CH_BYTES   (256u * 1024u)
#define MAXCH      256u                       /* 64 MiB de grabacion como maximo */
#define PK_MAX     90000u                     /* columnas de la onda: una por 50 ms */
#define SLICE_MS   50u
#define BLK        512u                       /* tramas por bloque de proceso */
#define PROBE_RATE 16000u
#define SAVE_DIR   "/grabaciones"
#define HW_RATE    48000u                    /* el ADC del ALC257 ignora otras frecuencias: se captura siempre a 48 kHz y se diezma */
#define GUARD_BYTES 1024u                    /* margen detras de LPIB (256 tramas) */

static uint8_t *chunk[MAXCH];
static uint32_t nch;
static volatile uint64_t rec_bytes;           /* mono de 16 bits */
static uint8_t *pk;
static volatile uint32_t npk;
static uint32_t rec_rate = 16000;
static volatile int cur_mode, want_mode;
static volatile uint32_t want_rate = 16000;
static volatile uint64_t until_ms;            /* 0 = sin limite */
static volatile int probe_req, started;
static volatile int g_level, g_hold;
static volatile uint32_t g_over, g_clip;
static int sw_x10 = 20, unsaved;                /* x2: el microfono digital del portatil llega muy bajo */
static char g_msg[96];

/* estado del proceso de captura */
static uint32_t last_pos; static uint64_t last_ms, last_move;
static int32_t dc_x, dc_y;
static uint32_t slice_n, slice_target; static int32_t slice_pk;
static uint64_t st_sumsq, st_n; static int32_t st_peak;
static uint64_t acc_sq, acc_n; static int32_t acc_pk;
static int32_t probe_rms[8];
static int32_t rawl_pk, rawr_pk; static int64_t rawl_sum, rawr_sum; static uint64_t raw_n;   /* muestras crudas (diagnostico) */
static int16_t raw_first[12]; static uint32_t raw_nfirst;
static uint32_t dec_ph; static int32_t dec_l, dec_r, dec_c;      /* diezmado 48 kHz -> frecuencia pedida (media por periodo) */

/* reproduccion */
static volatile int play_alive, play_stop;
static volatile uint32_t play_frames;

/* ---- utilidades ---- */
static void setmsg(const char *s) { uint32_t i = 0; while (s && s[i] && i + 1 < sizeof g_msg) { g_msg[i] = s[i]; i++; } g_msg[i] = 0; }
static void ap_s(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; o[*p < max ? *p : max - 1] = 0; }
static void ap_d(char *o, uint32_t max, uint32_t *p, uint64_t v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } ap_s(o, max, p, b + i); }
static void ap_h(char *o, uint32_t max, uint32_t *p, uint32_t v) { static const char H[] = "0123456789ABCDEF"; char b[9]; int i = 8; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = H[v & 15]; v >>= 4; } ap_s(o, max, p, b + i); }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static uint32_t isqrt64(uint64_t v) { uint64_t r = 0, bit = 1ULL << 62; while (bit > v) bit >>= 2; while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; } return (uint32_t)r; }
static int level_of(int32_t peak) { if (peak < 0) peak = -peak; int l = (int)(isqrt64((uint64_t)peak) * 100u / 181u); return l > 100 ? 100 : l; }
static int rate_ok(uint32_t r) { uint16_t f; return r >= 8000 && r <= 48000 && hda_format_for_rate(r, &f) == 0; }

/* ---- almacenamiento en RAM ---- */
static int ensure_bytes(uint64_t need)
{
    uint64_t want = (need + CH_BYTES - 1) / CH_BYTES;
    if (want > MAXCH) return -1;
    while (nch < want) { chunk[nch] = (uint8_t *)kmalloc(CH_BYTES); if (!chunk[nch]) return -1; nch++; }
    return 0;
}
static int append_samples(const int16_t *s, uint32_t n)
{
    uint64_t pos = rec_bytes; uint32_t nb = n * 2u; const uint8_t *src = (const uint8_t *)s;
    while (nb) {
        if (ensure_bytes(pos + 1) != 0) return -1;
        uint32_t off = (uint32_t)(pos % CH_BYTES), take = CH_BYTES - off; if (take > nb) take = nb;
        uint8_t *d = chunk[pos / CH_BYTES] + off;
        for (uint32_t i = 0; i < take; ++i) d[i] = src[i];
        pos += take; nb -= take; src += take;
    }
    rec_bytes = pos;
    return 0;
}
static uint32_t rec_read(uint64_t off, void *dst, uint32_t n)
{
    uint32_t got = 0; uint8_t *d = (uint8_t *)dst;
    while (got < n && off < rec_bytes) {
        uint32_t o = (uint32_t)(off % CH_BYTES), take = CH_BYTES - o; if (take > n - got) take = n - got;
        if ((uint64_t)take > rec_bytes - off) take = (uint32_t)(rec_bytes - off);
        const uint8_t *s = chunk[off / CH_BYTES] + o;
        for (uint32_t i = 0; i < take; ++i) d[got + i] = s[i];
        got += take; off += take;
    }
    return got;
}
static void pk_push(int32_t peak)
{
    if (!pk || npk >= PK_MAX) return;
    int32_t v = peak >> 7; if (v > 255) v = 255;
    pk[npk++] = (uint8_t)v;
}

/* ---- proceso de un bloque de tramas estereo ---- */
static void proc_block(const int16_t *raw, uint32_t frames, int recording)
{
    int16_t out[BLK];
    uint64_t sl = 0, sr = 0;
    for (uint32_t i = 0; i < frames; ++i) {
        int l = raw[2 * i], r = raw[2 * i + 1];
        sl += (uint32_t)(l < 0 ? -l : l); sr += (uint32_t)(r < 0 ? -r : r);
        if ((l < 0 ? -l : l) > rawl_pk) rawl_pk = l < 0 ? -l : l;
        if ((r < 0 ? -r : r) > rawr_pk) rawr_pk = r < 0 ? -r : r;
        rawl_sum += l; rawr_sum += r; raw_n++;
        if (raw_nfirst + 1 < 12 && raw_n > 4000) { raw_first[raw_nfirst++] = (int16_t)l; raw_first[raw_nfirst++] = (int16_t)r; }   /* unas muestras tras el arranque */
    }
    int sel = 2;                                           /* 0 = izquierdo, 1 = derecho, 2 = media */
    if (sr * 8 < sl) sel = 0; else if (sl * 8 < sr) sel = 1;
    int32_t bpk = 0;
    for (uint32_t i = 0; i < frames; ++i) {
        int32_t x = sel == 0 ? raw[2 * i] : sel == 1 ? raw[2 * i + 1] : ((int32_t)raw[2 * i] + raw[2 * i + 1]) / 2;
        int32_t y = x - dc_x + ((dc_y * 32604) >> 15);     /* quita el nivel continuo (filtro de un polo, ~13 Hz a 16 kHz) */
        dc_x = x; dc_y = y;
        int32_t g = (y * sw_x10) / 10;
        if (g > 32767) { g = 32767; g_clip++; } else if (g < -32768) { g = -32768; g_clip++; }
        out[i] = (int16_t)g;
        int32_t a = g < 0 ? -g : g;
        if (a > bpk) bpk = a;
        acc_sq += (uint64_t)((int64_t)g * g); acc_n++;
        if (recording) {
            if (a > slice_pk) slice_pk = a;
            if (++slice_n >= slice_target) { pk_push(slice_pk); slice_n = 0; slice_pk = 0; }
        }
    }
    if (bpk > acc_pk) acc_pk = bpk;
    for (uint32_t i = 0; i < frames; ++i) { int32_t v = out[i]; st_sumsq += (uint64_t)((int64_t)v * v); }     /* estadisticas (grabando o midiendo) */
    st_n += frames; if (bpk > st_peak) st_peak = bpk;
    if (recording) {
        if (append_samples(out, frames) != 0) { want_mode = MR_IDLE; setmsg("Memoria llena: grabacion detenida"); }
    }
    int lv = level_of(bpk);
    g_level = lv > g_level ? lv : (g_level > 4 ? g_level - 4 : 0);
    if (lv > g_hold) g_hold = lv;
}

static void pump(uint32_t rate, int recording)
{
    uint32_t rb = 0; const uint8_t *ring = hda_cap_ring(&rb);
    if (!ring || !rb) return;
    uint32_t pos = hda_cap_pos() & ~3u;
    uint64_t now = timer_get_uptime_ms();
    uint32_t avail = (pos + rb - last_pos) % rb;
    if (avail) last_move = now;
    else if (now - last_move > 500) { setmsg("La captura no avanza (el DMA no escribe): prueba otra fuente"); want_mode = MR_IDLE; return; }
    uint64_t el = now - last_ms; last_ms = now;
    if (el * HW_RATE * 4u / 1000u >= (uint64_t)rb - rb / 8u) { g_over++; last_pos = (pos + rb - GUARD_BYTES) % rb; return; }     /* tardamos demasiado: se perdio audio */
    /* el DMA todavia esta escribiendo cerca de LPIB: solo se lee lo que queda GUARD_BYTES por detras (si no, se cuelan
     * muestras viejas de la vuelta anterior del anillo: ruido metalico con eco) */
    if (avail > GUARD_BYTES) avail -= GUARD_BYTES; else avail = 0;
    while (avail) {
        uint32_t n = avail / 4; if (n > BLK) n = BLK;
        int16_t raw[2 * BLK];
        uint32_t off = last_pos;
        for (uint32_t i = 0; i < n; ++i) {
            const volatile int16_t *f = (const volatile int16_t *)(ring + off);
            raw[2 * i] = f[0]; raw[2 * i + 1] = f[1];
            ((volatile int16_t *)f)[0] = 0; ((volatile int16_t *)f)[1] = 0;                        /* lo leido se pone a cero */
            off += 4; if (off >= rb) off = 0;
        }
        last_pos = off;
        avail -= n * 4;
        int16_t dec[2 * BLK]; uint32_t m = 0;
        for (uint32_t i = 0; i < n; ++i) {
            dec_l += raw[2 * i]; dec_r += raw[2 * i + 1]; dec_c++;
            dec_ph += rate;
            if (dec_ph >= HW_RATE) { dec_ph -= HW_RATE; dec[2 * m] = (int16_t)(dec_l / dec_c); dec[2 * m + 1] = (int16_t)(dec_r / dec_c); m++; dec_l = dec_r = dec_c = 0; }
        }
        if (m) proc_block(dec, m, recording);
        if (want_mode == MR_IDLE) break;
    }
}

static void state_reset(uint32_t rate)
{
    dc_x = dc_y = 0; dec_ph = 0; dec_l = dec_r = dec_c = 0; slice_n = 0; slice_pk = 0; slice_target = rate * SLICE_MS / 1000u;
    g_level = 0; g_hold = 0; acc_sq = acc_n = 0; acc_pk = 0;
}

static void summary_text(char *b, uint32_t max, const char *what)
{
    uint32_t p = 0;
    uint32_t rms = st_n ? isqrt64(st_sumsq / st_n) : 0;
    ap_s(b, max, &p, what); ap_s(b, max, &p, ": ");
    ap_d(b, max, &p, st_n * 1000u / (rec_rate ? rec_rate : 1)); ap_s(b, max, &p, " ms a "); ap_d(b, max, &p, rec_rate);
    ap_s(b, max, &p, " Hz, pico "); ap_d(b, max, &p, (uint64_t)st_peak * 100u / 32768u); ap_s(b, max, &p, " %, rms ");
    ap_d(b, max, &p, rms); ap_s(b, max, &p, ", saturadas "); ap_d(b, max, &p, g_clip);
    ap_s(b, max, &p, ", perdidas "); ap_d(b, max, &p, g_over);
    ap_s(b, max, &p, "\n  crudo (antes de filtrar): izq pico "); ap_d(b, max, &p, (uint64_t)rawl_pk);
    ap_s(b, max, &p, " media "); { int64_t m = raw_n ? rawl_sum / (int64_t)raw_n : 0; if (m < 0) { ap_s(b, max, &p, "-"); m = -m; } ap_d(b, max, &p, (uint64_t)m); }
    ap_s(b, max, &p, "; der pico "); ap_d(b, max, &p, (uint64_t)rawr_pk);
    ap_s(b, max, &p, " media "); { int64_t m = raw_n ? rawr_sum / (int64_t)raw_n : 0; if (m < 0) { ap_s(b, max, &p, "-"); m = -m; } ap_d(b, max, &p, (uint64_t)m); }
    ap_s(b, max, &p, "\n  primeras muestras (izq,der):");
    for (uint32_t i = 0; i + 1 < raw_nfirst; i += 2) {
        ap_s(b, max, &p, " ");
        int v = raw_first[i]; if (v < 0) { ap_s(b, max, &p, "-"); v = -v; } ap_d(b, max, &p, (uint64_t)v); ap_s(b, max, &p, ",");
        v = raw_first[i + 1]; if (v < 0) { ap_s(b, max, &p, "-"); v = -v; } ap_d(b, max, &p, (uint64_t)v);
    }
}
static void print_summary(const char *what)
{
    char b[420]; summary_text(b, sizeof b, what); kprint(b); kprint("\n");
}

static void run_for(uint32_t ms)
{
    uint64_t t = timer_get_uptime_ms() + ms;
    while (timer_get_uptime_ms() < t && want_mode == MR_IDLE) { pump(PROBE_RATE, 0); thread_sleep(5); }
}

static void do_probe(void)
{
    int n = hda_mic_count(), best = -1, keep = hda_mic_selected();
    int32_t best_rms = 0;
    cur_mode = MR_PROBE; setmsg("Probando las entradas: habla o golpea suave cerca del portatil...");
    kprint("mic: probando entradas (habla un poco)\n");
    for (int i = 0; i < n && i < 8; ++i) {
        probe_rms[i] = -1;
        hda_mic_select(i);
        if (hda_cap_start(HW_RATE) != 0) continue;
        last_pos = hda_cap_pos() & ~3u; last_ms = last_move = timer_get_uptime_ms();
        state_reset(PROBE_RATE);
        run_for(200);
        acc_sq = acc_n = 0; acc_pk = 0;
        run_for(900);
        hda_cap_stop();
        probe_rms[i] = acc_n ? (int32_t)isqrt64(acc_sq / acc_n) : 0;
        char b[120], d[80]; uint32_t p = 0;
        hda_mic_describe(i, d, sizeof d);
        ap_s(b, sizeof b, &p, "mic:   "); ap_d(b, sizeof b, &p, (uint64_t)i); ap_s(b, sizeof b, &p, ": "); ap_s(b, sizeof b, &p, d);
        ap_s(b, sizeof b, &p, " -> rms "); ap_d(b, sizeof b, &p, (uint64_t)probe_rms[i]); ap_s(b, sizeof b, &p, ", pico "); ap_d(b, sizeof b, &p, (uint64_t)acc_pk); ap_s(b, sizeof b, &p, "\n");
        kprint(b);
        if (probe_rms[i] > best_rms) { best_rms = probe_rms[i]; best = i; }
    }
    if (best >= 0 && best_rms >= 3) { hda_mic_select(best); setmsg("Fuente elegida por la mayor senal"); }
    else { hda_mic_select(keep >= 0 ? keep : 0); setmsg("Ninguna entrada dio senal clara: se mantiene la actual"); }
    char b[60]; uint32_t p = 0; ap_s(b, sizeof b, &p, "mic: fuente activa "); ap_d(b, sizeof b, &p, (uint64_t)hda_mic_selected()); ap_s(b, sizeof b, &p, "\n"); kprint(b);
    cur_mode = MR_IDLE; want_mode = MR_IDLE;
}

/* ---- hilo del motor ---- */
static void eng_apply(void)
{
    int w = want_mode;
    if (w == cur_mode) return;
    int prev = cur_mode;
    if (prev == MR_REC || prev == MR_MON) {
        hda_cap_stop();
        if (prev == MR_REC) { unsaved = rec_bytes > 0; print_summary("mic: grabacion"); }
        else print_summary("mic: nivel");
    }
    if (w == MR_REC || w == MR_MON) {
        uint32_t rate = w == MR_REC ? want_rate : PROBE_RATE;
        st_sumsq = st_n = 0; st_peak = 0; g_over = 0; g_clip = 0; g_msg[0] = 0;
        rawl_pk = rawr_pk = 0; rawl_sum = rawr_sum = 0; raw_n = 0; raw_nfirst = 0;
        if (w == MR_REC) { rec_bytes = 0; npk = 0; rec_rate = rate; unsaved = 0; }
        else rec_rate = rate;
        state_reset(rate);
        int r = hda_cap_start(HW_RATE);
        if (r != 0) {
            char b[64]; uint32_t p = 0; ap_s(b, sizeof b, &p, "No se pudo iniciar la captura (error "); ap_d(b, sizeof b, &p, (uint64_t)(-r)); ap_s(b, sizeof b, &p, ")");
            setmsg(b); kprint("mic: "); kprint(g_msg); kprint("\n");
            want_mode = cur_mode = MR_IDLE; return;
        }
        last_pos = hda_cap_pos() & ~3u; last_ms = last_move = timer_get_uptime_ms();
    }
    cur_mode = w;
}

static void eng_main(void *arg)
{
    (void)arg;
    for (;;) {
        if (probe_req && cur_mode == MR_IDLE && want_mode == MR_IDLE) { probe_req = 0; do_probe(); }
        eng_apply();
        if (cur_mode == MR_REC || cur_mode == MR_MON) {
            if (until_ms && timer_get_uptime_ms() >= until_ms) { until_ms = 0; want_mode = MR_IDLE; }
            else pump(cur_mode == MR_REC ? rec_rate : PROBE_RATE, cur_mode == MR_REC);
            thread_sleep(5);
        } else thread_sleep(25);
    }
}

void micrec_init(void)
{
    if (started || !hda_mic_ready()) return;
    pk = (uint8_t *)kmalloc(PK_MAX);
    for (int i = 0; i < 8; ++i) probe_rms[i] = -1;
    started = 1;
    if (!thread_create("micrec", eng_main, 0)) { started = 0; kprint("micrec: no se pudo crear el hilo\n"); return; }
    kprint("micrec: grabadora lista (orden mic, aplicacion Grabadora)\n");
}
uint64_t micrec_bytes(void) { return rec_bytes; }                          /* SOMA_178_T480S: la camara lee el audio grabado */
uint32_t micrec_rate(void) { return rec_rate; }
uint32_t micrec_read(uint64_t off, void *dst, uint32_t n) { return rec_read(off, dst, n); }
int micrec_busy(void) { return cur_mode != MR_IDLE; }
int micrec_available(void) { return started && hda_mic_ready(); }

/* ---- API ---- */
void micrec_stop(void) { play_stop = 1; until_ms = 0; if (cur_mode != MR_IDLE && cur_mode != MR_PROBE) want_mode = MR_IDLE; }

int micrec_record(uint32_t rate)
{
    if (!micrec_available()) return -19;
    if (!rate_ok(rate)) return -22;
    if (cur_mode == MR_PROBE) return -16;
    play_stop = 1;
    want_rate = rate; until_ms = 0;
    if (cur_mode == MR_REC) return 0;
    if (cur_mode == MR_MON) { want_mode = MR_IDLE; for (int i = 0; i < 100 && cur_mode != MR_IDLE; ++i) thread_sleep(5); }
    want_mode = MR_REC;
    return 0;
}
int micrec_monitor(void)
{
    if (!micrec_available()) return -19;
    if (cur_mode == MR_PROBE) return -16;
    if (cur_mode == MR_REC || cur_mode == MR_MON) return 0;
    want_mode = MR_MON;
    return 0;
}
void micrec_clear(void) { micrec_stop(); rec_bytes = 0; npk = 0; unsaved = 0; st_n = 0; g_msg[0] = 0; }
int micrec_probe(void)
{
    if (!micrec_available()) return -19;
    if (cur_mode != MR_IDLE) return -16;
    probe_req = 1;
    return 0;
}
int micrec_set_source(int idx) { if (!micrec_available()) return -19; return hda_mic_select(idx); }
int micrec_set_gain(int pct) { return hda_mic_set_gain(pct); }
int micrec_set_sw(int x10) { if (x10 < 10) x10 = 10; if (x10 > 160) x10 = 160; sw_x10 = x10; return 0; }
void micrec_source_name(int idx, char *out, uint32_t max) { hda_mic_describe(idx, out, max); }

void micrec_info(struct micrec_info *o)
{
    o->mode = play_alive ? MR_PLAY : cur_mode;
    o->available = micrec_available();
    o->nsrc = hda_mic_count(); o->src = hda_mic_selected();
    o->hw_gain = hda_mic_get_gain(); o->sw_x10 = sw_x10;
    o->rate = rec_rate;
    o->ms = (uint32_t)(rec_bytes / 2u * 1000u / (rec_rate ? rec_rate : 1));
    o->play_ms = (uint32_t)((uint64_t)play_frames * 1000u / (rec_rate ? rec_rate : 1));
    o->max_ms = (uint32_t)((uint64_t)MAXCH * CH_BYTES / 2u * 1000u / (rec_rate ? rec_rate : 1));
    o->level = g_level; o->hold = g_hold; o->overruns = g_over; o->clipped = g_clip; o->unsaved = unsaved && rec_bytes > 0;
    uint32_t i = 0; while (g_msg[i] && i + 1 < sizeof o->msg) { o->msg[i] = g_msg[i]; i++; } o->msg[i] = 0;
}

uint32_t micrec_waveform(uint8_t *cols, uint32_t n)
{
    uint32_t total = npk;
    if (!pk || !n || !total) return 0;
    if (cur_mode == MR_REC) {                                         /* en vivo: las ultimas n columnas */
        uint32_t used = total < n ? total : n, from = total - used;
        for (uint32_t i = 0; i < used; ++i) cols[i] = pk[from + i];
        return used;
    }
    if (total <= n) { for (uint32_t i = 0; i < total; ++i) cols[i] = pk[i]; return total; }
    for (uint32_t c = 0; c < n; ++c) {
        uint32_t a = (uint32_t)((uint64_t)total * c / n), b = (uint32_t)((uint64_t)total * (c + 1) / n), m = 0;
        for (uint32_t i = a; i < b || i == a; ++i) if (pk[i] > m) m = pk[i];
        cols[c] = (uint8_t)m;
    }
    return n;
}

/* ---- reproduccion ---- */
static void play_main(void *arg)
{
    (void)arg;
    uint64_t total = rec_bytes; uint32_t rate = rec_rate;
    int16_t *mono = (int16_t *)kmalloc(2048u), *st = (int16_t *)kmalloc(1024u * 7u * 4u);
    if (!mono || !st) { setmsg("Sin memoria para reproducir"); goto out; }
    /* normaliza al reproducir: sube el pico hasta ~88 % (hasta x32; nunca baja) para que una grabacion floja se oiga fuerte */
    int32_t pkmax = 0;
    for (uint64_t o = 0; o < total; ) {
        uint32_t w = (uint32_t)(total - o < 2048 ? total - o : 2048), g = rec_read(o, mono, w);
        if (!g) break;
        for (uint32_t i = 0; i < g / 2; ++i) { int32_t a = mono[i] < 0 ? -(int32_t)mono[i] : mono[i]; if (a > pkmax) pkmax = a; }
        o += g;
    }
    int32_t gq = 256;                                      /* ganancia x256 */
    if (pkmax > 0 && pkmax < 29000) { gq = (int32_t)(29000LL * 256 / pkmax); if (gq > 32 * 256) gq = 32 * 256; }
    /* la salida siempre a 48 kHz (el DAC puede ignorar otras frecuencias): interpolacion lineal desde la frecuencia grabada */
    if (audio_sys_open(HW_RATE, 0xFFFD) != 0) { setmsg("No se pudo abrir la salida de audio (ocupada o sin dispositivo)"); goto out; }
    {
        uint64_t off = 0; play_frames = 0;
        int32_t x0 = 0; uint32_t t = 0; int first = 1;
        while (off < total && !play_stop) {
            uint32_t want = (uint32_t)(total - off < 2048 ? total - off : 2048);
            uint32_t got = rec_read(off, mono, want);
            if (!got) break;
            uint32_t n = got / 2, m = 0;
            for (uint32_t i = 0; i < n; ++i) {
                int32_t x1 = (int32_t)(((int64_t)mono[i] * gq) >> 8);
                if (x1 > 32767) x1 = 32767; else if (x1 < -32768) x1 = -32768;
                if (first) { x0 = x1; first = 0; }
                while (t < HW_RATE) {
                    int16_t v = (int16_t)(x0 + (int32_t)(((int64_t)(x1 - x0) * t) / (int32_t)HW_RATE));
                    st[2 * m] = v; st[2 * m + 1] = v; m++;
                    t += rate;
                }
                t -= HW_RATE; x0 = x1;
            }
            if (audio_sys_write(st, m * 4u) != (int)(m * 4u)) break;
            off += n * 2u; play_frames = (uint32_t)(off / 2u);
        }
        audio_sys_close(!play_stop);
    }
out:
    if (mono) kfree(mono);
    if (st) kfree(st);
    play_alive = 0;
}
int micrec_play(void)
{
    if (!started) return -19;
    if (rec_bytes < 2) return -61;
    if (play_alive) return -16;
    if (cur_mode == MR_REC || cur_mode == MR_MON) { want_mode = MR_IDLE; for (int i = 0; i < 100 && cur_mode != MR_IDLE; ++i) thread_sleep(5); }
    play_stop = 0; play_frames = 0; play_alive = 1; g_msg[0] = 0;
    if (!thread_create("micplay", play_main, 0)) { play_alive = 0; return -12; }
    return 0;
}

/* ---- WAV en BIGFS ---- */
static int ensure_dir(struct bigfs *fs) { uint32_t ino; if (bigfs_lookup(fs, SAVE_DIR, &ino) == (int)BIGFS_T_DIR) return 0; return bigfs_mkdir(fs, SAVE_DIR); }

int micrec_save(const char *path, char *used, uint32_t max)
{
    if (rec_bytes < 2) return -61;
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if ((!path || !*path) && acceso_usuario()) {                  /* SOMA_363_ACCESO: las grabaciones, a /usuario/Audio */
        char dir[64], nm[128]; struct acc_sec as; acceso_carpeta(ACC_AUDIO, dir, sizeof dir);
        if (acceso_libre(dir, "grab_", ".wav", nm, sizeof nm) != 0 || acceso_sec_abre(&as, nm) != 0) return -28;
        uint8_t hdr[44]; wav_make_header(hdr, 1, rec_rate, (uint32_t)rec_bytes);
        int rc = acceso_sec_pon(&as, 0, hdr, 44) ? -28 : 0;
        uint8_t *buf = (uint8_t *)kmalloc(65536); if (!buf) rc = -12;
        uint64_t off = 0;
        while (rc == 0 && off < rec_bytes) { uint32_t n = rec_read(off, buf, 65536); if (!n) break; if (acceso_sec_pon(&as, 44 + off, buf, n)) { rc = -28; break; } off += n; }
        if (buf) kfree(buf);
        if (rc == 0 && off != rec_bytes) rc = -5;
        if (acceso_sec_fin(&as, rc == 0) != 0 && rc == 0) rc = -28;
        if (rc) return rc;
        unsaved = 0;
        if (used) { uint32_t q = 0; ap_s(used, max, &q, nm); }
        return 0;
    }
    if (!fs) { (void)e; return -19; }
    char name[BIGFS_PATH_MAX + 1]; uint32_t p = 0;
    if (path && *path) ap_s(name, sizeof name, &p, path);
    else {
        ensure_dir(fs);
        for (uint32_t i = 1; i < 1000; ++i) {
            p = 0; ap_s(name, sizeof name, &p, SAVE_DIR "/grab_");
            if (i < 100) ap_s(name, sizeof name, &p, "0");
            if (i < 10) ap_s(name, sizeof name, &p, "0");
            ap_d(name, sizeof name, &p, i); ap_s(name, sizeof name, &p, ".wav");
            uint32_t ino; if (bigfs_lookup(fs, name, &ino) < 0) break;
        }
    }
    uint32_t ino; int r = bigfs_create(fs, name, &ino);
    if (r < 0) return r;
    uint8_t hdr[44]; wav_make_header(hdr, 1, rec_rate, (uint32_t)rec_bytes);
    uint8_t *buf = (uint8_t *)kmalloc(65536);
    if (!buf) { bigfs_unlink(fs, name); return -12; }
    int64_t w = bigfs_write(fs, ino, 0, hdr, 44);
    uint64_t off = 0;
    while (w >= 0 && off < rec_bytes) {
        uint32_t n = rec_read(off, buf, 65536);
        if (!n) break;
        w = bigfs_write(fs, ino, 44 + off, buf, n);
        if (w != (int64_t)n) { w = w < 0 ? w : -5; break; }
        off += n;
    }
    kfree(buf);
    if (w < 0 || off != rec_bytes) { bigfs_unlink(fs, name); return w < 0 ? (int)w : -5; }
    unsaved = 0;
    if (used) { uint32_t q = 0; ap_s(used, max, &q, name); }
    return 0;
}

int micrec_load(const char *path)
{
    if (!path || !*path) return -22;
    micrec_stop();
    for (int i = 0; i < 100 && (cur_mode != MR_IDLE || play_alive); ++i) thread_sleep(5);
    int e = 0; struct bigfs *fs = 0;
    int vf = acceso_ruta_ok(path);                                 /* SOMA_363_ACCESO: tambien de /usuario y /usb */
    uint32_t ino = 0; struct bigfs_stat st;
    if (vf) { int t = vfs_size(path); if (t < 44) return -2; st.size = (uint64_t)t; }
    else {
        fs = bigfs_sys(&e);
        if (!fs) return e < 0 ? e : -19;
        if (bigfs_lookup(fs, path, &ino) != (int)BIGFS_T_FILE || bigfs_stat(fs, ino, &st) < 0) return -2;
    }
    uint8_t *buf = (uint8_t *)kmalloc(32768);
    if (!buf) return -12;
    int64_t g = vf ? vfs_read_at(path, 0, buf, 4096) : bigfs_read(fs, ino, 0, buf, 4096);
    struct wav_info w;
    int r = g > 0 ? wav_parse(buf, (size_t)g, &w) : WAV_E_SHORT;
    if (r != WAV_OK) { kfree(buf); setmsg(wav_strerror(r)); return -22; }
    if (w.rate < 4000 || w.rate > 48000) { kfree(buf); setmsg("Frecuencia no soportada (4000-48000 Hz)"); return -22; }
    uint64_t size = w.data_size;
    if (size == 0 || w.data_offset + size > st.size) size = st.size - w.data_offset;
    uint32_t fr = w.channels == 2 ? 4u : 2u;
    uint64_t frames = size / fr;
    if (frames * 2u > (uint64_t)MAXCH * CH_BYTES) frames = (uint64_t)MAXCH * CH_BYTES / 2u;
    rec_bytes = 0; npk = 0; rec_rate = w.rate;
    uint32_t sl_n = 0, sl_t = w.rate * SLICE_MS / 1000u; int32_t sl_pk = 0;
    uint64_t done = 0; int ok = 1;
    int16_t mono[8192 / 2];
    while (done < frames && ok) {
        uint64_t want = frames - done; if (want > 8192 / 4) want = 8192 / 4;
        int64_t n = vf ? vfs_read_at(path, w.data_offset + done * fr, buf, (uint32_t)(want * fr)) : bigfs_read(fs, ino, w.data_offset + done * fr, buf, want * fr);
        if (n <= 0) break;
        uint32_t k = (uint32_t)n / fr;
        for (uint32_t i = 0; i < k; ++i) {
            int32_t v = w.channels == 2 ? ((int32_t)((int16_t *)buf)[2 * i] + ((int16_t *)buf)[2 * i + 1]) / 2 : ((int16_t *)buf)[i];
            mono[i] = (int16_t)v;
            int32_t a = v < 0 ? -v : v; if (a > sl_pk) sl_pk = a;
            if (++sl_n >= sl_t) { pk_push(sl_pk); sl_n = 0; sl_pk = 0; }
        }
        if (append_samples(mono, k) != 0) { ok = 0; break; }
        done += k;
    }
    kfree(buf);
    unsaved = 0; st_n = 0; g_msg[0] = 0;
    if (!ok && rec_bytes == 0) { setmsg("Sin memoria para cargar"); return -12; }
    return 0;
}

int micrec_delete(const char *path)
{
    if (acceso_ruta_ok(path)) return vfs_delete(path);            /* SOMA_363_ACCESO */
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return e < 0 ? e : -19;
    return bigfs_unlink(fs, path);
}

/* ---- orden de shell: mic ---- */
static void list_sources(char *out, uint32_t max, uint32_t *p)
{
    int n = hda_mic_count(), cur = hda_mic_selected();
    for (int i = 0; i < n; ++i) {
        char d[96]; hda_mic_describe(i, d, sizeof d);
        ap_s(out, max, p, i == cur ? "\n  * " : "\n    "); ap_d(out, max, p, (uint64_t)i); ap_s(out, max, p, ": "); ap_s(out, max, p, d);
        if (i < 8 && probe_rms[i] >= 0) { ap_s(out, max, p, "  (prueba: rms "); ap_d(out, max, p, (uint64_t)probe_rms[i]); ap_s(out, max, p, ")"); }
    }
}
static void wait_idle(uint32_t max_ms)
{
    uint64_t t = timer_get_uptime_ms() + max_ms;
    thread_sleep(30);
    while (timer_get_uptime_ms() < t && (cur_mode != MR_IDLE || want_mode != MR_IDLE || probe_req)) thread_sleep(20);
}
static const char *tok(const char **q, char *b, uint32_t max)
{
    while (**q == ' ') (*q)++;
    uint32_t n = 0;
    while (**q && **q != ' ' && n + 1 < max) b[n++] = *(*q)++;
    b[n] = 0;
    return n ? b : 0;
}
static uint32_t num(const char *s) { uint32_t v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0'); return v; }
static uint32_t hexn(const char *s) { uint32_t v = 0; if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2; for (;; s++) { char c = *s; uint32_t d; if (c >= '0' && c <= '9') d = (uint32_t)(c - '0'); else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10); else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10); else break; v = v * 16 + d; } return v; }

int micrec_command(const char *args, char *out, uint32_t max)
{
    uint32_t p = 0; if (max) out[0] = 0;
    char sub[16], a1[64], a2[24], a3[24];
    const char *q = args ? args : "";
    if (!tok(&q, sub, sizeof sub)) { sub[0] = 'i'; sub[1] = 'n'; sub[2] = 'f'; sub[3] = 'o'; sub[4] = 0; }
    if (seq(sub, "info") || seq(sub, "fuentes")) {
        struct micrec_info in; micrec_info(&in);
        static const char *const MN[] = { "inactivo", "grabando", "midiendo nivel", "probando", "reproduciendo" };
        ap_s(out, max, &p, "mic: "); ap_s(out, max, &p, in.available ? MN[in.mode] : "NO DISPONIBLE (sin entrada de captura o soma.audio=off / soma.mic=off)");
        ap_s(out, max, &p, "; fuentes "); ap_d(out, max, &p, (uint64_t)in.nsrc);
        ap_s(out, max, &p, ", activa "); ap_d(out, max, &p, (uint64_t)(in.src < 0 ? 0 : in.src));
        ap_s(out, max, &p, ", ganancia codec "); ap_d(out, max, &p, (uint64_t)in.hw_gain); ap_s(out, max, &p, " %, software x");
        ap_d(out, max, &p, (uint64_t)(in.sw_x10 / 10)); ap_s(out, max, &p, ","); ap_d(out, max, &p, (uint64_t)(in.sw_x10 % 10));
        ap_s(out, max, &p, "; grabado "); ap_d(out, max, &p, in.ms); ap_s(out, max, &p, " ms a "); ap_d(out, max, &p, in.rate);
        ap_s(out, max, &p, " Hz"); if (in.unsaved) ap_s(out, max, &p, " (sin guardar)");
        if (in.msg[0]) { ap_s(out, max, &p, "\n  aviso: "); ap_s(out, max, &p, in.msg); }
        list_sources(out, max, &p);
        if (st_n) { ap_s(out, max, &p, "\n"); uint32_t room = max > p ? max - p : 0; if (room > 8) summary_text(out + p, room, "  ultima medida"); }
        kprint(out); kprint("\n");
        return 1;
    }
    int rc = 0;
    if (seq(sub, "fuente")) {
        if (!tok(&q, a1, sizeof a1)) ap_s(out, max, &p, "uso: mic fuente <N>  (mic fuentes las lista)");
        else { rc = micrec_set_source((int)num(a1)); ap_s(out, max, &p, rc == 0 ? "mic: fuente cambiada" : "mic: fuente no valida"); }
    } else if (seq(sub, "ganancia")) {
        if (!tok(&q, a1, sizeof a1)) ap_s(out, max, &p, "uso: mic ganancia <0-100>");
        else { micrec_set_gain((int)num(a1)); ap_s(out, max, &p, "mic: ganancia del codec "); ap_d(out, max, &p, (uint64_t)hda_mic_get_gain()); ap_s(out, max, &p, " %"); }
    } else if (seq(sub, "sw")) {
        if (!tok(&q, a1, sizeof a1)) ap_s(out, max, &p, "uso: mic sw <1-16>  (veces; 1 = sin amplificar)");
        else { micrec_set_sw((int)num(a1) * 10); ap_s(out, max, &p, "mic: ganancia por software x"); ap_d(out, max, &p, (uint64_t)(sw_x10 / 10)); }
    } else if (seq(sub, "nivel")) {
        uint32_t s = 3; if (tok(&q, a1, sizeof a1)) s = num(a1);
        if (s < 1) s = 1;
        if (s > 30) s = 30;
        rc = micrec_monitor(); if (rc == 0) until_ms = timer_get_uptime_ms() + s * 1000u;
        if (rc != 0) ap_s(out, max, &p, "mic: no se puede (ocupado o sin microfono)");
        else { wait_idle(s * 1000u + 3000u); summary_text(out, max, "mic nivel"); p = 0; while (out[p]) p++; }
    } else if (seq(sub, "grabar")) {
        uint32_t s = 5, rate = 16000;
        if (tok(&q, a1, sizeof a1)) s = num(a1);
        if (tok(&q, a2, sizeof a2)) rate = num(a2);
        if (s < 1) s = 1;
        if (s > 600) s = 600;
        rc = micrec_record(rate);
        if (rc == 0) until_ms = timer_get_uptime_ms() + s * 1000u + 30u;
        if (rc != 0) ap_s(out, max, &p, rc == -22 ? "mic: frecuencia no valida (8000, 11025, 16000, 22050, 32000, 44100, 48000)" : "mic: no se puede (ocupado o sin microfono)");
        else { wait_idle(s * 1000u + 3000u); summary_text(out, max, "mic grabar"); p = 0; while (out[p]) p++; ap_s(out, max, &p, "\n  (mic reproducir / mic guardar)"); }
    } else if (seq(sub, "parar")) { micrec_stop(); ap_s(out, max, &p, "mic: parado"); }
    else if (seq(sub, "reproducir")) { rc = micrec_play(); ap_s(out, max, &p, rc == 0 ? "mic: reproduciendo" : rc == -61 ? "mic: no hay nada grabado" : "mic: no se puede (audio ocupado)"); }
    else if (seq(sub, "guardar")) {
        char used[96]; used[0] = 0; a1[0] = 0; tok(&q, a1, sizeof a1);
        rc = micrec_save(a1, used, sizeof used);
        if (rc == 0) { ap_s(out, max, &p, "mic: guardado en "); ap_s(out, max, &p, used); }
        else { ap_s(out, max, &p, "mic: no se pudo guardar: "); ap_s(out, max, &p, rc == -61 ? "no hay nada grabado" : rc == -19 ? "almacen BIGFS no disponible (big format yes)" : bigfs_strerror(rc)); }
    } else if (seq(sub, "cargar")) {
        if (!tok(&q, a1, sizeof a1)) ap_s(out, max, &p, "uso: mic cargar <ruta.wav>");
        else { rc = micrec_load(a1); ap_s(out, max, &p, rc == 0 ? "mic: cargado" : "mic: no se pudo cargar (ruta o formato)"); }
    } else if (seq(sub, "borrar")) {
        if (!tok(&q, a1, sizeof a1)) { micrec_clear(); ap_s(out, max, &p, "mic: grabacion en memoria descartada"); }
        else { rc = micrec_delete(a1); ap_s(out, max, &p, rc == 0 ? "mic: archivo borrado" : "mic: no se pudo borrar"); }
    } else if (seq(sub, "probar")) {
        rc = micrec_probe();
        if (rc != 0) ap_s(out, max, &p, "mic: no se puede ahora");
        else { wait_idle(30000u); ap_s(out, max, &p, "mic: sondeo terminado (rms por fuente):"); list_sources(out, max, &p); }
    } else if (seq(sub, "nodos")) { hda_dump(); return 1; }
    else if (seq(sub, "verbo")) {
        if (!tok(&q, a1, sizeof a1) || !tok(&q, a2, sizeof a2)) ap_s(out, max, &p, "uso: mic verbo <nid> <verbo> [payload]  (hexadecimal)");
        else {
            a3[0] = '0'; a3[1] = 0; tok(&q, a3, sizeof a3);
            uint32_t r = 0; int e2 = hda_raw((uint8_t)hexn(a1), hexn(a2), hexn(a3), &r);
            ap_s(out, max, &p, "mic: respuesta 0x"); ap_h(out, max, &p, r); if (e2) ap_s(out, max, &p, " (la orden fallo)");
        }
    } else ap_s(out, max, &p, "uso: mic info|fuentes|fuente N|ganancia N|sw N|nivel [s]|grabar [s] [Hz]|parar|reproducir|guardar [ruta]|cargar ruta|borrar [ruta]|probar|nodos|verbo nid verbo payload");
    kprint(out); kprint("\n");
    return 1;
}
