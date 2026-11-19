/* SOMA_177_T480S: prueba en el anfitrion del motor de grabacion (src/core/micrec.c) con un anillo DMA simulado, un reloj
 * controlado y un almacen BIGFS de mentira (un archivo en memoria). Comprueba: eleccion del canal con senal, quitar el
 * nivel continuo, ganancia por software y saturacion, nivel/pico, perdidas cuando el hilo tarda, aviso de captura parada,
 * vuelta del anillo, onda resumida, WAV guardado/cargado y comprobacion de la cabecera. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "micrec.c"
#include "wav.c"

static uint64_t now_ms;
uint64_t timer_get_uptime_ms(void) { return now_ms; }
void thread_sleep(uint32_t ms) { now_ms += ms; }
struct tcb *thread_create(const char *n, thread_func_t e, void *a) { (void)n; (void)e; (void)a; return (struct tcb *)1; }
void *kmalloc(size_t n) { return malloc(n); }
void kfree(void *p) { free(p); }
void kprint(const char *s) { (void)s; }

/* ---- hda simulado ---- */
static uint8_t ring[128 * 1024]; static uint32_t fpos; static int running, cur_src, gain = 66, start_fail;
const uint8_t *hda_cap_ring(uint32_t *b) { if (b) *b = sizeof ring; return ring; }
uint32_t hda_cap_pos(void) { return fpos; }
int hda_cap_start(uint32_t r) { (void)r; if (start_fail) return -3; running = 1; fpos = 0; return 0; }
void hda_cap_stop(void) { running = 0; }
int hda_cap_running(void) { return running; }
int hda_mic_ready(void) { return 1; }
int hda_mic_count(void) { return 2; }
int hda_mic_selected(void) { return cur_src; }
int hda_mic_select(int i) { if (i < 0 || i > 1) return -1; cur_src = i; return 0; }
int hda_mic_set_gain(int p) { gain = p; return 0; }
int hda_mic_get_gain(void) { return gain; }
void hda_mic_describe(int i, char *o, uint32_t m) { const char *s = i ? "microfono del conector" : "microfono interno"; uint32_t k = 0; while (s[k] && k + 1 < m) { o[k] = s[k]; k++; } o[k] = 0; }
int hda_format_for_rate(uint32_t r, uint16_t *f) { *f = 0; return (r == 8000 || r == 16000 || r == 44100 || r == 48000) ? 0 : -1; }
int hda_raw(uint8_t n, uint32_t v, uint32_t p, uint32_t *r) { (void)n; (void)v; (void)p; if (r) *r = 0; return 0; }
void hda_dump(void) {}
static uint32_t open_rate, wbytes; static int wmax;
int audio_sys_open(uint32_t r, uint32_t t) { (void)t; open_rate = r; return 0; }
int audio_sys_write(const void *p, uint32_t n) { const int16_t *q = p; for (uint32_t i = 0; i < n / 2; ++i) { int a = q[i] < 0 ? -q[i] : q[i]; if (a > wmax) wmax = a; } wbytes += n; return (int)n; }
void audio_sys_close(int d) { (void)d; }

/* ---- BIGFS de mentira: un solo archivo ---- */
static uint8_t file_buf[2 * 1024 * 1024]; static uint64_t file_size; static int file_exists; static char file_name[128];
struct bigfs { int dummy; };
static struct bigfs g_fs;
struct bigfs *bigfs_sys(int *e) { if (e) *e = 0; return &g_fs; }
int bigfs_lookup(struct bigfs *fs, const char *p, uint32_t *ino) { (void)fs; if (!strcmp(p, "/grabaciones")) { if (ino) *ino = 1; return (int)BIGFS_T_DIR; } if (file_exists && !strcmp(p, file_name)) { if (ino) *ino = 2; return (int)BIGFS_T_FILE; } return -2; }
int bigfs_mkdir(struct bigfs *fs, const char *p) { (void)fs; (void)p; return 0; }
int bigfs_create(struct bigfs *fs, const char *p, uint32_t *ino) { (void)fs; if (file_exists) return -17; strcpy(file_name, p); file_exists = 1; file_size = 0; if (ino) *ino = 2; return 0; }
int bigfs_unlink(struct bigfs *fs, const char *p) { (void)fs; if (file_exists && !strcmp(p, file_name)) { file_exists = 0; return 0; } return -2; }
int64_t bigfs_write(struct bigfs *fs, uint32_t ino, uint64_t off, const void *b, uint64_t n) { (void)fs; (void)ino; if (off + n > sizeof file_buf) return -28; memcpy(file_buf + off, b, n); if (off + n > file_size) file_size = off + n; return (int64_t)n; }
int64_t bigfs_read(struct bigfs *fs, uint32_t ino, uint64_t off, void *b, uint64_t n) { (void)fs; (void)ino; if (off >= file_size) return 0; if (off + n > file_size) n = file_size - off; memcpy(b, file_buf + off, n); return (int64_t)n; }
int bigfs_stat(struct bigfs *fs, uint32_t ino, struct bigfs_stat *st) { (void)fs; (void)ino; memset(st, 0, sizeof *st); st->size = file_size; st->type = BIGFS_T_FILE; return 0; }
const char *bigfs_strerror(int c) { (void)c; return "error"; }

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

/* escribe `frames` tramas estereo en el anillo (L = dc + amp*sin, R = rv) y avanza la posicion del DMA */
static double ph; static double freq = 440.0;
static void feed(uint32_t frames, int amp, int dc, int rv, uint32_t rate)
{
    for (uint32_t i = 0; i < frames; ++i) {
        int16_t l = (int16_t)(dc + (int)(amp * sin(ph))); ph += 2 * M_PI * freq / rate;
        *(int16_t *)(ring + fpos) = l; *(int16_t *)(ring + fpos + 2) = (int16_t)rv;
        fpos = (fpos + 4) % sizeof ring;
    }
}
static void run_secs(int secs, int amp, int dc, int rv, uint32_t rate, int rec)
{
    uint32_t per = 48000 / 50;                         /* tics de 20 ms; el hardware siempre entrega 48 kHz */
    for (int t = 0; t < secs * 50; ++t) { now_ms += 20; feed(per, amp, dc, rv, 48000); pump(rate, rec); }
}

int main(void)
{
    started = 1; micrec_set_sw(10);
    for (int i = 0; i < 8; ++i) probe_rms[i] = -1;
    pk = (uint8_t *)malloc(PK_MAX);

    /* --- grabacion: canal derecho mudo, nivel continuo de 2000, senal de 8000 --- */
    CHECK(micrec_record(16000) == 0 && want_mode == MR_REC, "pedir grabar a 16 kHz");
    CHECK(micrec_record(12345) == -22, "frecuencia no valida");
    now_ms = 1000; eng_apply();
    CHECK(cur_mode == MR_REC && running, "el motor arranca la captura");
    run_secs(2, 8000, 2000, 0, 16000, 1);
    CHECK(rec_bytes <= 64000u && rec_bytes >= 64000u - 600u, "2 s a 16 kHz = 64000 bytes de mono (menos el margen de 256 tramas detras de LPIB)");
    int16_t s[16000]; rec_read(rec_bytes - 32000, s, 32000);
    long long sum = 0; int mx = 0, mn = 0;
    for (int i = 0; i < 16000; ++i) { sum += s[i]; if (s[i] > mx) mx = s[i]; if (s[i] < mn) mn = s[i]; }
    CHECK(llabs(sum / 16000) < 200, "nivel continuo (2000) eliminado");
    CHECK(mx > 7500 && mx < 8500 && mn < -7500 && mn > -8500, "se usa solo el canal con senal (no se promedia con el mudo)");
    CHECK(g_level > 20 && g_hold > 40 && g_clip == 0, "nivel y pico con senal normal, sin saturar");
    CHECK(npk == 39, "una columna de onda por 50 ms (39 completas)");
    uint8_t cols[64]; micrec_stop(); eng_apply();
    CHECK(cur_mode == MR_IDLE && !running, "parar detiene la captura");
    CHECK(micrec_waveform(cols, 64) == 39 && cols[20] > 50 && cols[20] < 70, "onda: 39 columnas de ~8000>>7");
    CHECK(micrec_waveform(cols, 10) == 10 && cols[5] > 50, "onda comprimida a 10 columnas");
    struct micrec_info in; micrec_info(&in);
    CHECK(in.ms >= 1960 && in.ms <= 2000 && in.rate == 16000 && in.unsaved == 1, "info: ~2000 ms, 16 kHz, sin guardar");

    /* --- reproduccion: siempre a 48 kHz estereo, 2 s grabados a 16 kHz = 2 s de salida --- */
    play_alive = 1; play_stop = 0; play_main(0);
    CHECK(open_rate == 48000 && wbytes >= 2u * 48000u * 4u - 4000u && wbytes <= 2u * 48000u * 4u, "reproducir a 48 kHz conserva la duracion (16 kHz -> 48 kHz)");
    CHECK(wmax > 26000 && wmax <= 32767, "reproducir normaliza el pico a ~88 % (grabacion floja suena fuerte)");

    /* --- tramas estereo reales: promedio de dos canales con senal --- */
    micrec_clear(); CHECK(rec_bytes == 0 && npk == 0, "NUEVA vacia la grabacion");
    micrec_record(8000); now_ms += 100; eng_apply();
    run_secs(1, 6000, 0, 6000, 8000, 1);                       /* L = seno, R = continuo 6000 -> (L+R)/2 sin el continuo */
    rec_read(rec_bytes - 8000, s, 8000);
    mx = 0; for (int i = 0; i < 4000; ++i) if (s[i] > mx) mx = s[i];
    CHECK(mx > 2500 && mx < 3500, "dos canales con senal: se promedian (6000/2 = 3000)");
    micrec_stop(); eng_apply();

    /* --- ganancia por software y saturacion --- */
    micrec_clear(); micrec_set_sw(40);
    CHECK(sw_x10 == 40, "ganancia x4");
    micrec_record(16000); now_ms += 100; eng_apply();
    run_secs(1, 20000, 0, 0, 16000, 1);
    CHECK(g_clip > 1000, "x4 sobre 20000 satura y se cuenta");
    micrec_stop(); eng_apply(); micrec_set_sw(10);
    micrec_set_sw(5); CHECK(sw_x10 == 10, "ganancia minima x1");
    micrec_set_sw(500); CHECK(sw_x10 == 160, "ganancia maxima x16");
    micrec_set_sw(10);

    /* --- el hilo tarda demasiado: se pierde audio --- */
    micrec_clear(); micrec_record(48000); now_ms += 10; eng_apply();
    run_secs(1, 5000, 0, 0, 48000, 1);
    uint32_t o0 = g_over; now_ms += 1500; feed(8000, 5000, 0, 0, 48000); pump(48000, 1);
    CHECK(g_over == o0 + 1, "1,5 s sin vaciar el anillo = perdida contada");
    micrec_stop(); eng_apply();

    /* --- el DMA no avanza --- */
    micrec_clear(); micrec_record(16000); now_ms += 10; eng_apply();
    now_ms += 100; pump(16000, 1); now_ms += 700; pump(16000, 1);
    CHECK(want_mode == MR_IDLE && strstr(g_msg, "no avanza"), "captura sin avance: aviso y parada");
    eng_apply();
    start_fail = 1; micrec_record(16000); eng_apply();
    CHECK(cur_mode == MR_IDLE && strstr(g_msg, "No se pudo iniciar"), "fallo al arrancar el DMA: mensaje");
    start_fail = 0;

    /* --- vuelta del anillo: 3 s a 48 kHz (el anillo guarda 0,68 s) sin perder una trama --- */
    micrec_clear(); micrec_record(48000); now_ms += 10; eng_apply();
    run_secs(3, 4000, 0, 0, 48000, 1);
    CHECK(rec_bytes <= 288000u && rec_bytes >= 288000u - 600u, "3 s a 48 kHz con vuelta del anillo, sin perder tramas");
    micrec_stop(); eng_apply();

    /* --- el DMA aun no ha escrito junto a LPIB: lo viejo no se cuela --- */
    micrec_clear(); micrec_record(16000); now_ms += 10; eng_apply();
    memset(ring, 0x55, sizeof ring);                           /* "vuelta anterior": datos viejos con un valor reconocible */
    fpos = 0; now_ms += 20; feed(300, 8000, 0, 8000, 16000);   /* el DMA ha escrito 300 tramas... */
    uint32_t keep = fpos; memset(ring + keep, 0x55, 64); fpos = keep + 64;   /* ...y LPIB va 16 tramas por delante de lo realmente escrito */
    pump(16000, 1);
    { int16_t chk[300]; uint32_t got = rec_read(0, chk, sizeof chk) / 2; int bad = 0; for (uint32_t i = 0; i < got; ++i) if (chk[i] == 0x5555 || chk[i] == (int16_t)0x5555) bad++;
      CHECK(bad == 0, "ninguna muestra vieja (0x5555) llega a la grabacion"); }
    micrec_stop(); eng_apply();

    /* --- guardar y cargar un WAV --- */
    micrec_clear(); micrec_record(16000); now_ms += 10; eng_apply();
    run_secs(1, 10000, 0, 0, 16000, 1);
    micrec_stop(); eng_apply();
    uint64_t n0 = rec_bytes; static int16_t ref[16000]; rec_read(0, ref, 32000);
    char used[96];
    CHECK(micrec_save(0, used, sizeof used) == 0 && !strcmp(used, "/grabaciones/grab_001.wav"), "guardado con nombre automatico");
    struct wav_info w;
    CHECK(wav_parse(file_buf, 64, &w) == WAV_OK && w.channels == 1 && w.rate == 16000 && w.bits == 16 && w.data_size == n0 && w.data_offset == 44, "cabecera WAV valida (mono, 16 kHz, 16 bits)");
    CHECK(file_size == 44 + n0, "tamano del archivo");
    CHECK(micrec_save(0, used, sizeof used) != 0 || !strcmp(used, "/grabaciones/grab_002.wav"), "segundo guardado: nombre siguiente");
    file_exists = 1; strcpy(file_name, "/grabaciones/grab_001.wav");
    micrec_clear();
    CHECK(micrec_load("/grabaciones/grab_001.wav") == 0 && rec_bytes == n0 && rec_rate == 16000, "cargar devuelve lo guardado");
    static int16_t back[16000]; rec_read(0, back, 32000);
    CHECK(!memcmp(ref, back, 32000), "las muestras cargadas son identicas");
    CHECK(npk == 19, "onda recalculada al cargar (19 columnas)");
    CHECK(micrec_load("/no/existe.wav") != 0, "cargar un archivo inexistente falla");
    CHECK(micrec_delete("/grabaciones/grab_001.wav") == 0 && !file_exists, "borrar");
    CHECK(micrec_save("/x.wav", used, sizeof used) == 0 && !strcmp(used, "/x.wav"), "guardar con ruta explicita");

    /* --- sondeo de fuentes --- */
    micrec_clear(); probe_req = 0;
    CHECK(micrec_probe() == 0 && probe_req == 1, "pedir sondeo");
    CHECK(micrec_set_source(1) == 0 && cur_src == 1 && micrec_set_source(7) != 0, "cambiar de fuente");

    /* --- cabecera WAV de la utilidad --- */
    uint8_t h[44]; wav_make_header(h, 2, 44100, 4000);
    CHECK(wav_parse(h, 44, &w) == WAV_OK && w.channels == 2 && w.rate == 44100 && w.data_size == 4000, "wav_make_header estereo");

    /* --- orden mic --- */
    char out[600];
    CHECK(micrec_command("info", out, sizeof out) == 1 && strstr(out, "fuentes 2"), "mic info");
    CHECK(micrec_command("sw 4", out, sizeof out) == 1 && sw_x10 == 40, "mic sw 4");
    CHECK(micrec_command("ganancia 80", out, sizeof out) == 1 && gain == 80, "mic ganancia 80");
    CHECK(micrec_command("fuente 0", out, sizeof out) == 1 && cur_src == 0, "mic fuente 0");
    CHECK(micrec_command("grabar 2 16000", out, sizeof out) == 1 && want_mode == MR_REC && until_ms, "mic grabar 2 16000");
    CHECK(micrec_command("grabar 2 7", out, sizeof out) == 1 && strstr(out, "frecuencia no valida"), "mic grabar con frecuencia mala");
    micrec_command("parar", out, sizeof out);
    CHECK(micrec_command("verbo 12 f00 0", out, sizeof out) == 1 && strstr(out, "0x0"), "mic verbo");
    if (fails) { printf("micrec: %d fallos\n", fails); return 1; }
    printf("micrec: canal con senal, nivel continuo, ganancia, saturacion, perdidas, DMA parado, vuelta del anillo, onda, WAV guardar/cargar y orden mic correctos\n");
    return 0;
}
