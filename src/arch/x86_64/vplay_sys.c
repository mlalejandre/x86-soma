/* SOMA_252_VIDEO_NATIVO: lado del nucleo del reproductor de video nativo. Ver src/user/vplay/vp_abi.h.
 *
 *   hilo "vplay" (uno por reproduccion):  crea las dos regiones del proceso (monton bajo demanda y memoria compartida),
 *   lanza vplay.elf con un filtro de llamadas (salir, dormir, hora, audio), y mientras viva le va echando el archivo en el
 *   anillo: pide trozos a la fuente (fetch) y los copia sin pisar lo que el reproductor aun necesita. Atiende sus saltos
 *   (want_off). Al acabar para el proceso, espera a que muera y libera la memoria.
 *   La ventana (otro hilo) solo lee la pagina de control y copia el fotograma publicado. */
#include <stdint.h>
#include <stddef.h>
#include "usermode.h"
#include "vmm.h"
#include "pmm.h"
#include "mem.h"
#include "thread.h"
#include "console.h"
#include "../../user/vplay/vp_abi.h"

#include "progz.h"
PROGZ_DECL(vplay);                                          /* SOMA_314_PROGZ: vplay.elf va comprimido en la imagen */

#define FEED_CHUNK   (256u * 1024u)
#define SYSMASK      ((1u << SYS_EXIT) | (1u << SYS_YIELD) | (1u << SYS_SLEEP) | (1u << SYS_UPTIME_MS) | \
                      (1u << SYS_AUDIO_OPEN) | (1u << SYS_AUDIO_WRITE) | (1u << SYS_AUDIO_CLOSE) | (1u << SYS_AUDIO_STATUS))

/* SOMA_253: hay dos instancias independientes del mismo mecanismo: la 0 reproduce (vps_*) y la 1 extrae el audio para
 * transcribirlo (vpx_*), asi se puede transcribir mientras el video sigue en pantalla. */
struct vpi {
    volatile int alive;                 /* el hilo de esta instancia existe */
    volatile uint32_t gen;              /* sube con cada start/stop: el hilo viejo se retira */
    struct vp_ctrl *volatile ctrl;      /* pagina de control en curso (0 = no hay) */
    struct uregion sh;                  /* region compartida en curso */
    struct vps_source src;
    volatile int state; char err[128];
    volatile uint64_t total;
    uint32_t mode;
    /* reproducir */
    uint32_t *frame; uint32_t fw, fh, fseq; volatile int paused;
    /* extraer audio */
    uint32_t max_ms; vpx_sink_fn sink; void *sctx;
    volatile uint64_t out_bytes; volatile uint32_t pos_ms, dur_ms; volatile int truncated;
};
static struct vpi V[2];
#define PL (&V[0])
#define EX (&V[1])

static void scopy(char *d, uint32_t max, const char *s) { uint32_t i = 0; if (!max) return; for (; s && s[i] && i + 1 < max; ++i) d[i] = s[i]; d[i] = 0; }

static void region_free(struct uregion *r)
{
    if (r->frames) {
        for (uint32_t k = 0; k < r->npages; ++k) if (r->frames[k]) pmm_free_frame((uintptr_t)r->frames[k]);
        kfree(r->frames);
    }
    r->frames = 0; r->npages = 0; r->used = 0;
}
static int region_init(struct uregion *r, uint64_t va, uint32_t npages, int demand)
{
    r->va = va; r->npages = npages; r->used = 0; r->demand = demand;
    r->flags = VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITABLE | VMM_FLAG_NX;
    r->frames = (uint64_t *)kmalloc((size_t)npages * sizeof(uint64_t));
    if (!r->frames) { r->npages = 0; return -1; }
    for (uint32_t k = 0; k < npages; ++k) r->frames[k] = 0;
    if (demand) return 0;
    for (uint32_t k = 0; k < npages; ++k) {
        uintptr_t f = pmm_alloc_frame();
        if (!f) return -1;
        uint64_t *p = (uint64_t *)f; for (int b = 0; b < 512; ++b) p[b] = 0;
        r->frames[k] = f; r->used++;
    }
    return 0;
}
/* los marcos de la region compartida estan mapeados 1:1 en el nucleo: se copia pagina a pagina */
static void sh_write(struct uregion *r, uint64_t off, const uint8_t *s, uint64_t n)
{
    while (n) {
        uint64_t k = off >> 12, o = off & 0xFFF, c = 4096 - o; if (c > n) c = n;
        uint8_t *d = (uint8_t *)(uintptr_t)r->frames[k] + o;
        __builtin_memcpy(d, s, (size_t)c);
        s += c; off += c; n -= c;
    }
}
static void sh_read(const struct uregion *r, uint64_t off, uint8_t *d, uint64_t n)
{
    while (n) {
        uint64_t k = off >> 12, o = off & 0xFFF, c = 4096 - o; if (c > n) c = n;
        __builtin_memcpy(d, (const uint8_t *)(uintptr_t)r->frames[k] + o, (size_t)c);
        d += c; off += c; n -= c;
    }
}

static void vplay_thread(void *arg)
{
    uint32_t idx = (uint32_t)((uint64_t)(uintptr_t)arg >> 32), my = (uint32_t)(uintptr_t)arg;
    struct vpi *I = &V[idx];
    struct vps_source src = I->src;
    struct uregion heap, sh; heap.frames = 0; sh.frames = 0; heap.npages = sh.npages = 0;
    uint8_t *buf = (uint8_t *)kmalloc(FEED_CHUNK);
    int pid = -1; const char *why = 0; char ferr[128]; ferr[0] = 0;
    if (!buf || region_init(&heap, VP_HEAP_VA, VP_HEAP_MAX >> 12, 1) != 0 || region_init(&sh, VP_SH_VA, VP_SH_BYTES >> 12, 0) != 0) why = "Sin memoria para el reproductor de video";
    if (!why) {
        struct vp_ctrl *c = (struct vp_ctrl *)(uintptr_t)sh.frames[0];
        c->version = VP_VERSION; c->paused = (uint32_t)I->paused; c->mode = I->mode; c->out_max_ms = I->max_ms; c->magic = VP_MAGIC;
        struct uexec_spec sp = {0}; sp.may_thr = 0;   /* SOMA_352: entera a cero (max_sockets quedaba con basura de la pila) */
        sp.sysmask = SYSMASK; sp.nregions = 2; sp.regions[0] = &heap; sp.regions[1] = &sh; sp.regions[2] = sp.regions[3] = 0;
        sp.reserve_frames = 4096;                              /* el monton no crece si quedan menos de 16 MiB libres */
        int e = 0;
        pid = -1;                                              /* SOMA_314_PROGZ: se descomprime, se lanza y se libera */
        { struct progz pz; if (progz_abre(PROGZ(vplay), "vplay", &pz) == 0) { pid = usermode_exec_ex(idx ? "vpaudio" : "vplay", pz.elf, pz.len, 8u * 3600u * 1000u, &sp, &e); progz_cierra(&pz); } }
        if (pid < 0) why = "No se pudo lanzar el reproductor de video (sin procesos libres o memoria)";
        else if (I->gen == my) { I->sh = sh; I->ctrl = c; }
    }
    if (why) { if (I->gen == my) { scopy(I->err, sizeof I->err, why); I->state = VPS_ERROR; } }
    else {
        struct vp_ctrl *c = (struct vp_ctrl *)(uintptr_t)sh.frames[0];
        uint32_t fseq = 0; int feed_dead = 0;
        while (I->gen == my) {
            if (!usermode_proc_alive(pid)) {                   /* el proceso ha muerto: un video que rompe el decodificador */
                if (c->state != VP_ST_ENDED) { scopy(I->err, sizeof I->err, "El reproductor de video se ha detenido (video no valido o sin memoria)"); I->state = VPS_ERROR; }
                break;
            }
            if (idx == 1) {                                    /* extraccion de audio: se recoge lo que haya salido */
                uint64_t w = c->out_w, r = c->out_r; int bad = 0;
                if (w < r || w - r > VP_OUT_CAP) bad = 1;      /* el proceso no es de fiar */
                while (!bad && r < w) {
                    uint64_t p = r % VP_OUT_CAP, n = w - r; if (n > VP_OUT_CAP - p) n = VP_OUT_CAP - p; if (n > FEED_CHUNK) n = FEED_CHUNK;
                    sh_read(&sh, VP_FR_OFF + p, buf, n);
                    if (I->sink && I->sink(I->sctx, buf, (uint32_t)n) < 0) { scopy(I->err, sizeof I->err, "No se pudo guardar el sonido (sin sitio en el almacen?)"); I->state = VPS_ERROR; bad = 2; break; }
                    r += n; c->out_r = r; I->out_bytes = r;
                    if (I->gen != my) break;
                }
                if (bad) { if (bad == 1) { scopy(I->err, sizeof I->err, "El decodificador de audio ha fallado"); I->state = VPS_ERROR; } break; }
                I->pos_ms = c->out_ms; I->dur_ms = c->dur_ms;
                if (c->state == VP_ST_ERROR) {
                    char e[96]; for (uint32_t i = 0; i < sizeof e; ++i) e[i] = c->err[i];
                    e[sizeof e - 1] = 0; scopy(I->err, sizeof I->err, e[0] ? e : "No se pudo decodificar el sonido"); I->state = VPS_ERROR; break;
                }
                if (c->state == VP_ST_PLAYING && I->state == VPS_LOADING) I->state = VPS_PLAYING;
                if (c->state == VP_ST_ENDED && c->out_r == c->out_w) { I->truncated = c->feed_error != 0; I->pos_ms = c->out_ms; I->state = VPS_ENDED; break; }
            }
            if (c->want_seq != fseq) {                         /* salto: la descarga sigue desde otro sitio */
                fseq = c->want_seq;
                uint64_t o = c->want_off;
                c->in_base = o; c->in_end = o; c->feed_seq = fseq;
                if (feed_dead && (!I->total || o < I->total)) { feed_dead = 0; c->feed_error = 0; }
            }
            uint64_t end = c->in_end, keep = c->in_keep, total = I->total;
            if (keep > end) keep = end;                        /* el proceso no es de fiar: nunca se escribe fuera del anillo */
            uint64_t room = keep + VP_IN_CAP > end ? keep + VP_IN_CAP - end : 0;
            if (feed_dead || (total && end >= total) || room < 4096) { thread_sleep(room < 4096 ? 15 : 30); continue; }
            uint32_t want = room > FEED_CHUNK ? FEED_CHUNK : (uint32_t)room;
            uint64_t tot = total;
            int64_t n = src.fetch(src.ctx, end, buf, want, &tot, ferr, sizeof ferr);
            if (I->gen != my) break;
            if (tot && !I->total) { I->total = tot; c->file_size = tot; }
            if (c->want_seq != fseq) continue;                 /* ha saltado mientras llegaba: este trozo ya no vale */
            if (n > 0) {
                uint64_t done = 0;
                while (done < (uint64_t)n) {                    /* el anillo da la vuelta */
                    uint64_t p = (end + done) % VP_IN_CAP, k = VP_IN_CAP - p; if (k > (uint64_t)n - done) k = (uint64_t)n - done;
                    sh_write(&sh, VP_IN_OFF + p, buf + done, k);
                    done += k;
                }
                c->in_end = end + (uint64_t)n;
            } else if (n == 0 && I->total && end >= I->total) { /* fin del archivo */ }
            else {
                feed_dead = 1; c->feed_error = 1;
                if (c->state == VP_ST_START || c->state == VP_ST_LOADING) { scopy(I->err, sizeof I->err, ferr[0] ? ferr : "No llega el video"); I->state = VPS_ERROR; }
            }
            thread_yield();
        }
        c->quit = 1;
    }
    if (I->gen == my) I->ctrl = 0; else if (I->ctrl == (struct vp_ctrl *)(uintptr_t)(sh.frames ? sh.frames[0] : 0)) I->ctrl = 0;
    if (pid >= 0) {
        for (int w = 0; w < 40 && usermode_proc_alive(pid); ++w) thread_sleep(5);
        if (usermode_proc_alive(pid)) usermode_stop(pid);
        (void)usermode_wait(pid);
    }
    thread_sleep(30);                                          /* la ventana puede estar copiando un fotograma */
    region_free(&heap); region_free(&sh);
    if (buf) kfree(buf);
    if (src.close) src.close(src.ctx);
    I->alive--;
}

static void stop_i(struct vpi *I)
{
    struct vp_ctrl *c = I->ctrl;
    if (c) c->quit = 1;
    I->ctrl = 0; I->gen++; I->state = VPS_IDLE; I->fseq = 0;
}
static int start_i(struct vpi *I, uint32_t idx, const struct vps_source *src, char *err, uint32_t emax)
{
    stop_i(I);
    for (int w = 0; w < 200 && I->alive > 0; ++w) thread_sleep(10);     /* el anterior suelta el audio y la memoria */
    if (I->alive > 0) { scopy(err, emax, "El reproductor anterior aun no ha terminado"); if (src->close) src->close(src->ctx); return 0; }
    if (idx == 0 && !I->frame) I->frame = (uint32_t *)kmalloc(VP_FR_BYTES);
    if (idx == 0 && !I->frame) { scopy(err, emax, "Sin memoria"); if (src->close) src->close(src->ctx); return 0; }
    I->src = *src; I->total = 0; I->err[0] = 0; I->state = VPS_LOADING; I->fseq = 0; I->fw = I->fh = 0; I->paused = 0;
    I->out_bytes = 0; I->pos_ms = I->dur_ms = 0; I->truncated = 0;
    uint32_t my = ++I->gen;
    I->alive++;
    if (!thread_create(idx ? "vpaudio" : "vplay", vplay_thread, (void *)(uintptr_t)(((uint64_t)idx << 32) | my))) {
        I->alive--; I->state = VPS_ERROR; scopy(I->err, sizeof I->err, "No se pudo crear el hilo del reproductor"); scopy(err, emax, I->err);
        if (src->close) { src->close(src->ctx); }
        return 0;
    }
    return 1;
}

int vps_start(const struct vps_source *src, char *err, uint32_t emax) { PL->mode = VP_MODE_PLAY; PL->max_ms = 0; return start_i(PL, 0, src, err, emax); }
void vps_stop(void) { stop_i(PL); }
int  vps_active(void) { return PL->state != VPS_IDLE; }
void vps_pause(int on) { struct vp_ctrl *c = PL->ctrl; PL->paused = on ? 1 : 0; if (c) c->paused = (uint32_t)PL->paused; }
void vps_seek(uint32_t ms) { struct vp_ctrl *c = PL->ctrl; if (!c) return; c->cmd = VP_CMD_SEEK; c->cmd_arg = ms; c->cmd_seq = c->cmd_seq + 1; }

void vps_get_info(struct vps_info *o)
{
    struct vp_ctrl *c = PL->ctrl;
    __builtin_memset(o, 0, sizeof *o);
    o->state = PL->state; o->paused = PL->paused; o->total_bytes = PL->total;
    scopy(o->err, sizeof o->err, PL->err);
    if (!c || PL->state == VPS_IDLE || PL->state == VPS_ERROR) return;
    uint32_t st = c->state;
    if (st == VP_ST_ERROR) {
        char e[96]; for (uint32_t i = 0; i < sizeof e; ++i) e[i] = c->err[i];
        e[sizeof e - 1] = 0;
        o->state = VPS_ERROR; scopy(o->err, sizeof o->err, e[0] ? e : "El video no se puede reproducir");
        return;
    }
    o->state = st == VP_ST_PLAYING ? VPS_PLAYING : st == VP_ST_ENDED ? VPS_ENDED : VPS_LOADING;
    o->pos_ms = c->pos_ms; o->dur_ms = c->dur_ms; o->buffering = c->buffering != 0; o->has_audio = c->has_audio != 0;
    o->w = c->w <= VP_MAX_W ? c->w : 0; o->h = c->h <= VP_MAX_H ? c->h : 0; o->frame_seq = c->frame_seq;
    o->have_bytes = c->in_end; o->frames_shown = c->frames_shown; o->frames_late = c->frames_late; o->errors = c->errors;
    if (o->dur_ms && o->pos_ms > o->dur_ms) o->pos_ms = o->dur_ms;
}

const uint32_t *vps_frame(uint32_t *w, uint32_t *h, uint32_t *seq)
{
    struct vp_ctrl *c = PL->ctrl;
    if (c && PL->frame) {
        uint32_t s = c->frame_seq;
        if (s != PL->fseq) {
            uint32_t fw = c->w, fh = c->h, fb = c->frame_buf & 1u;
            if (fw >= 16 && fh >= 16 && fw <= VP_MAX_W && fh <= VP_MAX_H) {   /* lo que dice el proceso se comprueba siempre */
                sh_read(&PL->sh, VP_FR_OFF + (uint64_t)fb * VP_FR_BYTES, (uint8_t *)PL->frame, (uint64_t)fw * fh * 4u);
                PL->fw = fw; PL->fh = fh; PL->fseq = s;
            }
        }
    }
    if (!PL->fseq || !PL->fw) return 0;
    *w = PL->fw; *h = PL->fh; *seq = PL->fseq;
    return PL->frame;
}

/* ---- SOMA_253: extraccion del audio */
int vpx_start(const struct vps_source *src, uint32_t max_ms, vpx_sink_fn sink, void *sctx, char *err, uint32_t emax)
{
    EX->mode = VP_MODE_AUDIO; EX->max_ms = max_ms; EX->sink = sink; EX->sctx = sctx;
    return start_i(EX, 1, src, err, emax);
}
void vpx_stop(void)                                            /* espera (hasta 2 s) a que el hilo suelte el sumidero y la memoria */
{
    stop_i(EX);
    for (int w = 0; w < 200 && EX->alive > 0; ++w) thread_sleep(10);
}
void vpx_get_info(struct vpx_info *o)
{
    __builtin_memset(o, 0, sizeof *o);
    o->state = EX->state; o->pos_ms = EX->pos_ms; o->dur_ms = EX->dur_ms; o->out_bytes = EX->out_bytes;
    o->total_bytes = EX->total; o->truncated = EX->truncated;
    struct vp_ctrl *c = EX->ctrl; if (c) o->have_bytes = c->in_end;
    scopy(o->err, sizeof o->err, EX->err);
}
