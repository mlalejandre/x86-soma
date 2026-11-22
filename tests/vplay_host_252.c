/* SOMA_252_VIDEO_NATIVO: el reproductor nativo (src/user/vplay) corriendo en el anfitrion contra un nucleo simulado.
 * El reproductor es el mismo codigo que va en ring 3; aqui el "nucleo" es esta prueba: alimenta el anillo con el archivo
 * a la velocidad que se pida, hace de tarjeta de sonido (una cola que se vacia en tiempo real simulado) y mira cada
 * fotograma publicado. El tiempo es simulado (solo avanza cuando el reproductor duerme): la prueba es determinista.
 *
 *   vplay_host_252 <archivo.mp4> <guion> [KiB/s]
 * guiones:  normal   reproduce entero: todos los fotogramas, en orden y a su hora respecto al audio; termina en ENDED
 *           lento    red mas lenta que el video: hay esperas ("buffering") pero no se pierde ni se desordena nada
 *           saltos   salta adelante, atras y casi al final; pausa y reanuda; cada salto continua desde donde debe
 *           corte    la descarga falla a la mitad: el reproductor acaba (no se cuelga)
 *           audio    SOMA_253: modo "solo audio": entrega PCM 16 kHz mono por el anillo de salida (opcional: archivo .pcm donde guardarlo)
 *           audio5   igual pero parando a los 5 s de audio */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/user/vplay/vp_abi.h"
#include "../src/user/vplay/mp4.h"

static uint8_t *file; static uint64_t fsize;
static uint8_t *sh; static struct vp_ctrl *C;
static uint64_t now; static uint64_t rate_bps = 0;                 /* 0 = sin limite */
static const char *guion;
static int fallos;
#define CHECK(c, ...) do { if (!(c)) { fallos++; if (fallos < 12) { printf("  FALLO: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* --- tarjeta de sonido simulada */
#define FIFO_CAP 32768u
static int a_open; static uint32_t a_rate; static uint64_t a_fifo, a_played_bytes, a_total_written; static int a_opens;
/* --- descarga simulada */
static uint32_t feed_seq_local; static uint64_t feed_budget; static uint64_t cut_at;
/* --- observador */
static uint32_t seen_seq; static int shown; static uint64_t last_pos; static int buffering_seen;
static uint64_t frame_sum; static int step;
static uint8_t *pcm_out; static uint64_t pcm_n;                   /* modo audio: lo que el "nucleo" ha recogido */

static void kernel_tick(uint32_t ms)
{
    now += ms;
    if (a_open) {                                                  /* suena lo que haya en la cola */
        uint64_t b = (uint64_t)a_rate * 4u * ms / 1000u; if (b > a_fifo) b = a_fifo;
        a_fifo -= b; a_played_bytes += b;
    }
    if (C->want_seq != feed_seq_local) { feed_seq_local = C->want_seq; C->in_base = C->in_end = C->want_off; C->feed_seq = feed_seq_local; }
    feed_budget += rate_bps ? rate_bps * ms / 1000u : (4u << 20);
    while (feed_budget && C->in_end < fsize && !C->feed_error) {
        if (cut_at && C->in_end >= cut_at) { C->feed_error = 1; break; }
        uint64_t room = C->in_keep + VP_IN_CAP > C->in_end ? C->in_keep + VP_IN_CAP - C->in_end : 0;
        uint64_t n = fsize - C->in_end; if (n > room) n = room; if (n > feed_budget) n = feed_budget; if (n > 65536) n = 65536;
        if (!n) break;
        for (uint64_t i = 0; i < n; ++i) sh[VP_IN_OFF + (C->in_end + i) % VP_IN_CAP] = file[C->in_end + i];
        C->in_end += n; feed_budget -= n;
    }
    if (feed_budget > (8u << 20)) feed_budget = 8u << 20;
    if (C->mode == VP_MODE_AUDIO) {                                /* recoge la salida */
        uint64_t w = C->out_w, r = C->out_r;
        CHECK(w >= r && w - r <= VP_OUT_CAP, "anillo de salida incoherente: out_w %llu out_r %llu", (unsigned long long)w, (unsigned long long)r);
        while (r < w) { pcm_out[pcm_n++] = sh[VP_FR_OFF + r % VP_OUT_CAP]; r++; }
        C->out_r = r;
    }
    if (C->buffering) buffering_seen++;
    if (C->frame_seq != seen_seq) {                                /* fotograma nuevo: a que hora del audio ha salido */
        seen_seq = C->frame_seq; shown++;
        const uint32_t *px = (const uint32_t *)(sh + VP_FR_OFF + (size_t)C->frame_buf * VP_FR_BYTES);
        uint64_t s = 0; for (uint32_t i = 0; i < C->w * C->h; i += 97) s += px[i] & 0xFFFFFF;
        frame_sum += s;
        CHECK(C->w >= 16 && C->w <= VP_MAX_W && C->h >= 16 && C->h <= VP_MAX_H, "fotograma de %ux%u", C->w, C->h);
    }
    /* --- guion */
    if (!strcmp(guion, "saltos") && C->state == VP_ST_PLAYING) {
        uint32_t dur = C->dur_ms;
        if (step == 0 && C->pos_ms > 1500) { C->cmd = VP_CMD_SEEK; C->cmd_arg = dur * 6 / 10; C->cmd_seq++; step = 1; }
        else if (step == 1 && C->cmd_ack == C->cmd_seq && C->pos_ms > dur * 6 / 10 + 1200) {
            CHECK(C->pos_ms < dur * 6 / 10 + 2500, "tras saltar al 60%% la posicion es %u de %u", C->pos_ms, dur);
            C->cmd = VP_CMD_SEEK; C->cmd_arg = dur / 10; C->cmd_seq++; step = 2;
        } else if (step == 2 && C->cmd_ack == C->cmd_seq && C->pos_ms > dur / 10 + 800 && C->pos_ms < dur / 2) {
            C->paused = 1; step = 3; last_pos = now;
        } else if (step == 3 && now - last_pos > 3000) {
            static uint32_t p0; static int in; if (!in) { in = 1; p0 = C->pos_ms; last_pos = now; return; }
            CHECK(C->pos_ms <= p0 + 20, "en pausa la posicion avanza: %u -> %u", p0, C->pos_ms);
            C->paused = 0; step = 4; last_pos = C->pos_ms;
        } else if (step == 4 && C->pos_ms > last_pos + 1000) {
            C->cmd = VP_CMD_SEEK; C->cmd_arg = dur > 1500 ? dur - 1500 : 0; C->cmd_seq++; step = 5;
        }
    }
}

static uint64_t e_now(void) { return now; }
static void e_sleep(uint32_t ms) { if (C->state == VP_ST_ENDED && step != 0 && step != 5 && strcmp(guion, "saltos") == 0) { } kernel_tick(ms ? ms : 1); if (now > 4000000u) { printf("  FALLO: la prueba no termina (estado %u, pos %u)\n", C->state, C->pos_ms); exit(1); }
    if ((C->state == VP_ST_ENDED || C->state == VP_ST_ERROR) && (strcmp(guion, "saltos") || step >= 5 || C->state == VP_ST_ERROR)) C->quit = 1; }
static int e_aopen(uint32_t rate) { if (rate != 44100 && rate != 48000 && rate != 32000 && rate != 24000 && rate != 22050) return -22; a_open = 1; a_rate = rate; a_fifo = 0; a_played_bytes = 0; a_opens++; return 0; }
static int64_t e_awrite(const void *p, uint32_t n) { (void)p; if (!a_open) return -9; n &= ~3u; uint64_t room = FIFO_CAP - a_fifo; if (n > room) n = (uint32_t)room; a_fifo += n; a_total_written += n; return n; }
static int e_astatus(uint64_t *fr, uint64_t *us) { if (!a_open) return -9; *fr = FIFO_CAP - a_fifo; *us = a_played_bytes * 1000000u / ((uint64_t)a_rate * 4u); return 0; }
static void e_aclose(void) { a_open = 0; }

int main(int argc, char **argv)
{
    if (argc < 3) { printf("uso: %s archivo.mp4 normal|lento|saltos|corte [KiB/s]\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { printf("no se puede abrir %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END); fsize = (uint64_t)ftell(f); fseek(f, 0, SEEK_SET);
    file = (uint8_t *)malloc(fsize); if (fread(file, 1, fsize, f) != fsize) return 2;
    fclose(f);
    guion = argv[2]; int audio_mode = !strncmp(guion, "audio", 5);
    if (argc > 3) rate_bps = (uint64_t)atoi(argv[3]) * 1024u;
    if (!strcmp(guion, "corte")) cut_at = fsize / 2;
    sh = (uint8_t *)calloc(1, VP_SH_BYTES); C = (struct vp_ctrl *)sh;
    pcm_out = (uint8_t *)malloc(64u << 20);
    if (audio_mode) { C->mode = VP_MODE_AUDIO; C->out_max_ms = !strcmp(guion, "audio5") ? 5000 : 0; }
    C->magic = VP_MAGIC; C->version = VP_VERSION; C->file_size = fsize;
    struct vp_env env = { e_now, e_sleep, e_aopen, e_awrite, e_astatus, e_aclose };
    int r = vp_run(&env, sh);
    /* cuantos fotogramas y cuanto audio tiene el archivo, segun su indice */
    uint64_t mo = 0, ms = 0, at = 0; struct mp4_info in; memset(&in, 0, sizeof in);
    for (int k = 0; k < 16; ++k) { int q = mp4_find_moov(file + at, fsize - at, at, fsize, &mo, &ms); if (q == 1) { mp4_parse_moov(file + mo, ms, &in); break; } if (q < 0) break; at = mo; }
    printf("[%s] estado %u, pos %u de %u ms, fotogramas %d de %u (tarde %u), errores %u, esperas %d, audio %llu bytes, t=%llu ms%s%s\n", guion, C->state, C->pos_ms, C->dur_ms, shown, in.v.nsamples,
           C->frames_late, C->errors, buffering_seen, (unsigned long long)a_total_written, (unsigned long long)now, C->err[0] ? " | " : "", C->err);
    CHECK(r == 0 || !strcmp(guion, "corte"), "vp_run devuelve %d", r);
    if (audio_mode) {
        if (argc > 4) { FILE *o = fopen(argv[4], "wb"); if (o) { fwrite(pcm_out, 1, pcm_n, o); fclose(o); } }
        printf("[%s] PCM 16 kHz mono: %llu bytes (%llu ms) de %u ms\n", guion, (unsigned long long)pcm_n, (unsigned long long)(pcm_n / 32), C->dur_ms);
        CHECK(C->state == VP_ST_ENDED, "el modo audio no termina en ENDED (estado %u)", C->state);
        CHECK(C->errors == 0, "errores de decodificacion de audio: %u", C->errors);
        CHECK(C->frames_shown == 0 && !a_open && a_total_written == 0, "el modo audio no debe reproducir ni abrir el audio del sistema");
        if (!strcmp(guion, "audio5")) CHECK(pcm_n >= 5u * 32000u && pcm_n <= 5u * 32000u + 2u * 32000u / 10u * 3u, "con limite de 5 s salen %llu bytes", (unsigned long long)pcm_n);
        else CHECK(pcm_n / 32 + 150 >= C->dur_ms && pcm_n / 32 <= (uint64_t)C->dur_ms + 150, "audio %llu ms para %u ms", (unsigned long long)(pcm_n / 32), C->dur_ms);
        printf("[vplay_host_252 %s] %s\n", guion, fallos ? "FALLOS" : "OK");
        return fallos ? 1 : 0;
    }
    if (!strcmp(guion, "normal") || !strcmp(guion, "lento")) {
        CHECK(C->state == VP_ST_ENDED, "no termina en ENDED");
        CHECK(C->frames_shown + C->frames_late == in.v.nsamples && C->frames_late <= in.v.nsamples / 50, "fotogramas publicados %u + tarde %u != %u", C->frames_shown, C->frames_late, in.v.nsamples);
        CHECK((uint32_t)shown * 100 >= C->frames_shown * 95, "el nucleo solo ha visto %d de %u fotogramas", shown, C->frames_shown);
        CHECK(C->errors == 0, "errores de decodificacion: %u", C->errors);
        uint64_t esperado = in.a.present ? (uint64_t)in.a.nsamples * 1024u * 4u : 0;
        CHECK(!in.a.present || (a_total_written > esperado * 97 / 100 && a_total_written <= esperado * 2 + 8192), "audio escrito %llu, esperado ~%llu", (unsigned long long)a_total_written, (unsigned long long)esperado);
        CHECK(now + 400 >= C->dur_ms && (rate_bps || now < (uint64_t)C->dur_ms + 1500), "duracion simulada %llu ms para un video de %u ms", (unsigned long long)now, C->dur_ms);
        if (!strcmp(guion, "lento")) CHECK(buffering_seen > 0 || now > (uint64_t)C->dur_ms + 2000, "con la red lenta deberia haber esperas (o tardar mas que el video)");
        else CHECK(buffering_seen < 40, "esperas sin motivo: %d", buffering_seen);
    } else if (!strcmp(guion, "saltos")) {
        CHECK(step == 5, "el guion se quedo en el paso %d", step);
        CHECK(C->state == VP_ST_ENDED, "no termina tras saltar cerca del final");
        CHECK(shown > 60 && C->errors == 0, "fotogramas %d, errores %u", shown, C->errors);
        CHECK(!in.a.present || a_opens >= 4, "el audio se abre %d veces (una por salto)", a_opens);
    } else if (!strcmp(guion, "corte")) {
        CHECK(C->state == VP_ST_ENDED || C->state == VP_ST_ERROR, "con la descarga cortada el estado es %u", C->state);
        CHECK(mo > cut_at || shown > 10, "con el indice al principio, antes del corte se vieron %d fotogramas", shown);
    }
    printf("[vplay_host_252 %s] %s (suma de imagen %llu)\n", guion, fallos ? "FALLOS" : "OK", (unsigned long long)frame_sum);
    return fallos ? 1 : 0;
}
