/* SOMA_178_T480S: motor de la camara web. Ver camera.h */
#include <stdint.h>
#include <stddef.h>
#include "camera.h"
#include "cam_hw.h"
#include "uvc.h"
#include "jpeg.h"
#include "avi_wr.h"
#include "png_enc.h"
#include "micrec.h"
#include "bigfs.h"
#include "bigfs_glue.h"
#include "thread.h"
#include "mem.h"
#include "console.h"
#include "idt.h"

#define VCH_BYTES   (256u * 1024u)
#define VMAXCH      192u                      /* 48 MiB de video como maximo */
#define VMAXF       20000u
#define ARATE       16000u
#define MAX_FRAME   (4u * 1024u * 1024u)

/* ---- estado ---- */
static int started;                           /* hilo de decodificacion creado */
static volatile int running, dec_run;
static struct cam_hw_info hw;
static struct uvc_sel sel;
static uint32_t cur_w, cur_h, cur_iv, cur_raw; static char cur_fourcc[5];
static uint8_t *asm0, *asm1, *jbuf, *dbuf, *xbuf, *rfix; static uint32_t asm_cap;
static struct uvc_asm as;
static volatile int jlock; static volatile uint32_t jlen, jseq, dseq;
static uint32_t *pvb[2]; static volatile int pv_front; static volatile uint32_t pv_w, pv_h, pv_seq; static uint32_t pv_cap;
static uint32_t fr_ok, fr_bad, dec_ok, dec_err, last_bytes, fps_x10; static int dec_last_err;
static uint64_t fps_t0; static uint32_t fps_n;
static char g_msg[112];

/* grabacion de video */
static volatile int rec_on, rec_audio, saving;
static uint8_t *vch[VMAXCH]; static uint32_t nvch, vch_used;
static uint32_t *vf_off, *vf_len, *vf_ms; static uint32_t nvf; static uint64_t vrec_bytes, rec_t0;

static void setmsg(const char *s) { uint32_t i = 0; while (s && s[i] && i + 1 < sizeof g_msg) { g_msg[i] = s[i]; i++; } g_msg[i] = 0; }
static void ap_s(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; o[*p < max ? *p : max - 1] = 0; }
static void ap_d(char *o, uint32_t max, uint32_t *p, uint64_t v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } ap_s(o, max, p, b + i); }
static void ap_h(char *o, uint32_t max, uint32_t *p, uint32_t v, int digits) { static const char H[] = "0123456789ABCDEF"; char b[9]; int n = 0; for (int i = digits - 1; i >= 0; i--) b[n++] = H[(v >> (4 * i)) & 15]; b[n] = 0; ap_s(o, max, p, b); }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static uint32_t num(const char *s) { uint32_t v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0'); return v; }
static int tok(const char **q, char *o, uint32_t max) { const char *s = *q; while (*s == ' ') s++; if (!*s) return 0; uint32_t i = 0; while (*s && *s != ' ') { if (i + 1 < max) o[i++] = *s; s++; } o[i] = 0; *q = s; return 1; }
static void mcopy(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }

/* ---- trama completa (hilo uvcd) ---- */
static const uint8_t *fixed(const uint8_t *f, uint32_t n, uint8_t *scratch, uint32_t scap, uint32_t *outn)
{
    if (!cur_raw) { int32_t r = uvc_mjpeg_add_dht(f, n, scratch, scap); if (r > 0) { *outn = (uint32_t)r; return scratch; } }
    *outn = n; return f;
}

static void rec_append(const uint8_t *f, uint32_t n)
{
    uint32_t fn; const uint8_t *g = fixed(f, n, rfix, asm_cap + 512u, &fn);
    if (fn > VCH_BYTES || nvf >= VMAXF) { if (nvf >= VMAXF) { rec_on = 0; setmsg("Limite de tramas: grabacion detenida"); } return; }
    if (!nvch || vch_used + fn > VCH_BYTES) {
        if (nvch >= VMAXCH) { rec_on = 0; setmsg("Memoria llena: grabacion detenida"); return; }
        uint8_t *c = (uint8_t *)kmalloc(VCH_BYTES);
        if (!c) { rec_on = 0; setmsg("Sin memoria: grabacion detenida"); return; }
        vch[nvch++] = c; vch_used = 0;
    }
    mcopy(vch[nvch - 1] + vch_used, g, fn);
    vf_off[nvf] = (nvch - 1) * VCH_BYTES + vch_used; vf_len[nvf] = fn; vf_ms[nvf] = (uint32_t)(timer_get_uptime_ms() - rec_t0);
    nvf++; vch_used += fn; vrec_bytes += fn;
}

static void on_frame(const uint8_t *f, uint32_t n)
{
    fr_ok++; last_bytes = n; fps_n++;
    uint64_t now = timer_get_uptime_ms();
    if (!fps_t0) fps_t0 = now;
    if (now - fps_t0 >= 1000) { fps_x10 = (uint32_t)((uint64_t)fps_n * 10000u / (now - fps_t0)); fps_n = 0; fps_t0 = now; }
    if (rec_on) rec_append(f, n);
    if (!__sync_lock_test_and_set(&jlock, 1)) { mcopy(jbuf, f, n); jlen = n; jseq++; __sync_lock_release(&jlock); }
}

static void payload_cb(const uint8_t *p, uint32_t n)
{
    int r = uvc_asm_feed(&as, p, n);
    if (r == UVC_FRAME_OK) on_frame(as.done, as.done_len);
    else if (r == UVC_FRAME_BAD) fr_bad++;
}

/* ---- decodificacion para la vista previa ---- */
static void dec_main(void *arg)
{
    (void)arg;
    for (;;) {
        if (!dec_run || !running || jseq == dseq || !pvb[0]) { thread_sleep(dec_run ? 4 : 30); continue; }
        while (__sync_lock_test_and_set(&jlock, 1)) thread_sleep(1);
        uint32_t n = jlen; uint32_t sq = jseq;
        if (n && n <= asm_cap) mcopy(dbuf, jbuf, n); else n = 0;
        __sync_lock_release(&jlock);
        dseq = sq;
        if (!n) continue;
        uint32_t *back = pvb[pv_front ^ 1];
        if (cur_raw) {
            if (n >= cur_w * cur_h * 2u) { uvc_yuy2_to_rgb(dbuf, cur_w, cur_h, back, cur_w); pv_w = cur_w; pv_h = cur_h; pv_front ^= 1; pv_seq++; dec_ok++; }
            continue;
        }
        uint32_t fn; const uint8_t *g = fixed(dbuf, n, xbuf, asm_cap + 512u, &fn);
        struct jpeg_info ji;
        int r = jpeg_decode_isolated(g, fn, back, cur_w, pv_cap / (cur_w ? cur_w : 1), &ji);
        if (r == JPEG_OK) { pv_w = ji.width; pv_h = ji.height; pv_front ^= 1; pv_seq++; dec_ok++; }
        else { dec_err++; dec_last_err = r; }
    }
}

static void free_buffers(void)
{
    if (asm0) { kfree(asm0); }
    if (asm1) { kfree(asm1); }
    if (jbuf) { kfree(jbuf); }
    if (dbuf) { kfree(dbuf); }
    if (xbuf) { kfree(xbuf); }
    if (rfix) { kfree(rfix); }
    if (pvb[0]) { kfree(pvb[0]); }
    if (pvb[1]) { kfree(pvb[1]); }
    asm0 = asm1 = jbuf = dbuf = xbuf = rfix = 0; pvb[0] = pvb[1] = 0; pv_cap = 0; pv_w = pv_h = 0;
}

static void free_rec(void)
{
    for (uint32_t i = 0; i < nvch; i++) { kfree(vch[i]); vch[i] = 0; }
    nvch = 0; vch_used = 0; nvf = 0; vrec_bytes = 0;
}

int camera_modes(struct camera_mode *m, int max)
{
    struct cam_hw_info i; if (!running && cam_hw_probe(&i) != 0) return 0; else if (running) i = hw;
    const struct uvc_cfg *c = i.cfg; int n = 0;
    if (!c) return 0;
    for (int v = 0; v < c->n_vs && !n; v++)
        for (int f = 0; f < c->vs[v].n_fmt; f++) {
            const struct uvc_format *fm = &c->vs[v].fmt[f];
            for (int r = 0; r < fm->n_frames; r++) {
                int dup = 0; for (int k = 0; k < n; k++) if (m[k].w == fm->frame[r].w && m[k].h == fm->frame[r].h) dup = 1;
                if (dup || n >= max) continue;
                m[n].w = fm->frame[r].w; m[n].h = fm->frame[r].h; m[n].mjpeg = fm->kind == UVC_FMT_MJPEG; n++;
            }
        }
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if ((uint32_t)m[b].w * m[b].h < (uint32_t)m[a].w * m[a].h) { struct camera_mode t = m[a]; m[a] = m[b]; m[b] = t; }
    return n;
}

void camera_stop(void)
{
    if (rec_on) rec_on = 0;
    if (!running) return;
    dec_run = 0; running = 0;
    cam_hw_stop();
    thread_sleep(20);
    free_buffers();
    setmsg("Camara parada");
}

int camera_start(uint32_t w, uint32_t h, uint32_t fps)
{
    if (running) camera_stop();
    g_msg[0] = 0; fr_ok = fr_bad = dec_ok = dec_err = 0; fps_x10 = 0; fps_n = 0; fps_t0 = 0; last_bytes = 0; jseq = dseq = 0; jlen = 0; dec_last_err = 0;
    if (cam_hw_probe(&hw) != 0) { setmsg(hw.why[0] ? hw.why : "no hay camara"); return -19; }
    if (uvc_choose(hw.cfg, w, h, fps, 1, &sel) != 0) { setmsg("la camara no ofrece ningun formato utilizable"); return -22; }
    const struct uvc_format *fm = &hw.cfg->vs[sel.vs].fmt[sel.fmt]; const struct uvc_frame *fr = &fm->frame[sel.frm];
    cur_raw = fm->kind != UVC_FMT_MJPEG; cur_w = fr->w; cur_h = fr->h; cur_iv = sel.interval;
    for (int i = 0; i < 5; i++) cur_fourcc[i] = fm->fourcc[i];
    if (cur_raw && !(fm->fourcc[0] == 'Y' && fm->fourcc[1] == 'U' && fm->fourcc[2] == 'Y' && fm->fourcc[3] == '2')) { setmsg("la camara solo ofrece un formato sin comprimir no soportado (no YUY2) y no tiene MJPEG"); return -95; }
    uint32_t cap = cur_raw ? cur_w * cur_h * 2u : (fr->max_size > 65536u ? fr->max_size : 65536u);
    if (cap < cur_w * cur_h / 2u) cap = cur_w * cur_h / 2u;
    if (cap > MAX_FRAME) cap = MAX_FRAME;
    asm_cap = cap; pv_cap = cur_w * cur_h;
    asm0 = (uint8_t *)kmalloc(cap); asm1 = (uint8_t *)kmalloc(cap); jbuf = (uint8_t *)kmalloc(cap + 512u); dbuf = (uint8_t *)kmalloc(cap + 512u);
    xbuf = (uint8_t *)kmalloc(cap + 512u); rfix = (uint8_t *)kmalloc(cap + 512u);
    pvb[0] = (uint32_t *)kmalloc((size_t)pv_cap * 4u); pvb[1] = (uint32_t *)kmalloc((size_t)pv_cap * 4u);
    if (!asm0 || !asm1 || !jbuf || !dbuf || !xbuf || !rfix || !pvb[0] || !pvb[1]) { free_buffers(); setmsg("sin memoria para las tramas"); return -12; }
    for (uint32_t i = 0; i < pv_cap; i++) { pvb[0][i] = 0; pvb[1][i] = 0; }
    pv_front = 0; pv_seq = 0;
    uvc_asm_init(&as, asm0, asm1, cap, cur_raw ? cur_w * cur_h * 2u : 0);
    if (!started) { if (!thread_create("camdec", dec_main, 0)) { free_buffers(); setmsg("no se pudo crear el hilo de decodificacion"); return -12; } started = 1; }
    running = 1; dec_run = 1;
    int r = cam_hw_start(&sel, payload_cb);
    if (r != 0) {
        struct cam_hw_stats st; cam_hw_stats(&st);
        running = 0; dec_run = 0; setmsg(st.err[0] ? st.err : "no se pudo iniciar la camara");
        thread_sleep(20); free_buffers();
        return -5;
    }
    return 0;
}

void camera_info(struct camera_info *o)
{
    uint8_t *z = (uint8_t *)o; for (unsigned i = 0; i < sizeof *o; i++) z[i] = 0;
    o->hw_present = hw.present; o->vid = hw.vid; o->pid = hw.pid; o->speed = hw.speed;
    o->running = running && cam_hw_running();
    if (running && !cam_hw_running() && !g_msg[0]) setmsg("La camara dejo de enviar datos (USB reexplorado o desconectada)");
    o->recording = rec_on; o->audio_rec = rec_audio; o->saving = saving;
    o->w = cur_w; o->h = cur_h; o->fps10 = cur_iv ? 100000000u / cur_iv : 0;
    for (int k = 0; k < 5; k++) o->fourcc[k] = cur_fourcc[k];
    o->fr_ok = fr_ok; o->fr_bad = fr_bad; o->fps_x10 = fps_x10; o->last_bytes = last_bytes; o->dec_ok = dec_ok; o->dec_err = dec_err; o->dec_last_err = dec_last_err;
    o->rec_frames = nvf; o->rec_ms = nvf ? vf_ms[nvf - 1] : 0; o->rec_bytes = (uint32_t)vrec_bytes; o->rec_max_bytes = VMAXCH * VCH_BYTES;
    o->pv_w = pv_w; o->pv_h = pv_h; o->pv_seq = pv_seq;
    uint32_t k = 0; while (g_msg[k] && k + 1 < sizeof o->msg) { o->msg[k] = g_msg[k]; k++; } o->msg[k] = 0;
}

const uint32_t *camera_preview(uint32_t *w, uint32_t *h, uint32_t *seq)
{
    if (!running || !pvb[0] || !pv_seq) return 0;
    if (w) { *w = pv_w; }
    if (h) { *h = pv_h; }
    if (seq) { *seq = pv_seq; }
    return pvb[pv_front];
}

/* ---- BIGFS ---- */
static int ensure_dir(struct bigfs *fs, const char *d) { uint32_t ino; if (bigfs_lookup(fs, d, &ino) == (int)BIGFS_T_DIR) return 0; return bigfs_mkdir(fs, d); }
static void next_name(struct bigfs *fs, const char *dir, const char *pre, const char *ext, char *name, uint32_t max)
{
    for (uint32_t i = 1; i < 1000; ++i) {
        uint32_t p = 0; ap_s(name, max, &p, dir); ap_s(name, max, &p, "/"); ap_s(name, max, &p, pre);
        if (i < 100) { ap_s(name, max, &p, "0"); }
        if (i < 10) { ap_s(name, max, &p, "0"); }
        ap_d(name, max, &p, i); ap_s(name, max, &p, ext);
        uint32_t ino; if (bigfs_lookup(fs, name, &ino) < 0) break;
    }
}

int camera_photo(const char *path, char *used, uint32_t max)
{
    if (!running || !jbuf || !jlen) return -61;
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return -19;
    uint8_t *tmp = (uint8_t *)kmalloc(asm_cap + 512u);
    if (!tmp) return -12;
    uint32_t n;
    while (__sync_lock_test_and_set(&jlock, 1)) thread_sleep(1);
    n = jlen; if (n && n <= asm_cap) mcopy(tmp, jbuf, n); else n = 0;
    __sync_lock_release(&jlock);
    if (!n) { kfree(tmp); return -61; }
    const uint8_t *data = tmp; uint32_t dn = n; uint8_t *png = 0; const char *ext = ".jpg";
    if (cur_raw) {                                              /* sin MJPEG: se guarda la vista previa como PNG */
        uint32_t w = pv_w, h = pv_h; const uint32_t *pv = pvb[pv_front];
        size_t cap = (size_t)w * h * 4u + 4096u, wk = png_work_size((int)w, (int)h);
        png = (uint8_t *)kmalloc(cap); void *work = kmalloc(wk);
        if (!png || !work || !pv || !w) {
            if (png) { kfree(png); }
            if (work) { kfree(work); }
            kfree(tmp); return -12;
        }
        size_t pn = png_encode_rgb(pv, (int)w, (int)h, png, cap, work); kfree(work);
        if (!pn) { kfree(png); kfree(tmp); return -5; }
        data = png; dn = (uint32_t)pn; ext = ".png";
    } else {
        int32_t r = uvc_mjpeg_add_dht(tmp, n, rfix ? rfix : tmp, asm_cap + 512u);
        if (r > 0 && rfix) { data = rfix; dn = (uint32_t)r; }
    }
    char name[BIGFS_PATH_MAX + 1]; uint32_t p = 0;
    if (path && *path) ap_s(name, sizeof name, &p, path);
    else { ensure_dir(fs, "/fotos"); next_name(fs, "/fotos", "foto_", ext, name, sizeof name); }
    uint32_t ino; int r = bigfs_create(fs, name, &ino);
    if (r < 0) { if (png) kfree(png); kfree(tmp); return r; }
    int64_t w = bigfs_write(fs, ino, 0, data, dn);
    if (png) kfree(png);
    kfree(tmp);
    if (w != (int64_t)dn) { bigfs_unlink(fs, name); return w < 0 ? (int)w : -5; }
    if (used) { uint32_t q = 0; ap_s(used, max, &q, name); }
    return 0;
}

/* ---- video ---- */
int camera_rec_start(int with_audio)
{
    if (!running) return -61;
    if (rec_on || saving) return -16;
    free_rec();
    vf_off = (uint32_t *)kmalloc(VMAXF * 4u); vf_len = (uint32_t *)kmalloc(VMAXF * 4u); vf_ms = (uint32_t *)kmalloc(VMAXF * 4u);
    if (!vf_off || !vf_len || !vf_ms) {
        if (vf_off) { kfree(vf_off); }
        if (vf_len) { kfree(vf_len); }
        if (vf_ms) { kfree(vf_ms); }
        vf_off = vf_len = vf_ms = 0; return -12;
    }
    rec_audio = 0;
    if (with_audio && micrec_available()) { micrec_clear(); if (micrec_record(ARATE) == 0) rec_audio = 1; }
    rec_t0 = timer_get_uptime_ms();
    rec_on = 1;
    return 0;
}

struct vsrc { uint32_t maxf; };
static uint32_t gv_cb(void *u, uint32_t i, uint8_t *dst, uint32_t cap)
{
    (void)u; uint32_t l = vf_len[i]; if (l > cap) return 0;
    mcopy(dst, vch[vf_off[i] / VCH_BYTES] + vf_off[i] % VCH_BYTES, l);
    return l;
}
static uint32_t ga_cb(void *u, uint64_t off, void *dst, uint32_t n) { (void)u; return micrec_read(off, dst, n); }
struct bsink { struct bigfs *fs; uint32_t ino; };
static int sink_cb(void *u, uint64_t off, const void *b, uint32_t n)
{
    struct bsink *s = (struct bsink *)u;
    int64_t w = bigfs_write(s->fs, s->ino, off, b, n);
    return w == (int64_t)n ? 0 : -1;
}

int camera_rec_discard(void)
{
    rec_on = 0;
    if (rec_audio) { micrec_stop(); rec_audio = 0; }
    free_rec();
    if (vf_off) { kfree(vf_off); kfree(vf_len); kfree(vf_ms); vf_off = vf_len = vf_ms = 0; }
    return 0;
}

int camera_rec_stop(const char *path, char *used, uint32_t max)
{
    if (!nvf && !rec_on) return -61;
    rec_on = 0;
    if (rec_audio) { micrec_stop(); for (int i = 0; i < 100 && micrec_busy(); i++) thread_sleep(5); }
    int rc = 0;
    saving = 1;
    uint32_t n = nvf;
    if (n < 2) { rc = -61; goto out; }
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) { rc = -19; goto out; }
    uint32_t last = vf_ms[n - 1], first = vf_ms[0];
    uint32_t span = last > first ? last - first : 1u;
    uint32_t upf = (uint32_t)((uint64_t)span * 1000u / (n - 1u));                   /* microsegundos entre tramas */
    if (upf < 1000u) upf = 1000u;
    uint32_t maxf = 0; for (uint32_t i = 0; i < n; i++) if (vf_len[i] > maxf) maxf = vf_len[i];
    uint64_t dur_ms = (uint64_t)span + upf / 1000u, ab = 0;
    if (rec_audio) {
        ab = micrec_bytes();
        uint64_t want = dur_ms * ARATE * 2u / 1000u;
        if (ab > want) ab = want;
        ab &= ~1ull;
    }
    struct avi_wr_src src = { .w = cur_w, .h = cur_h, .nframes = n, .usec_per_frame = upf, .lens = vf_len, .has_audio = ab >= 2 && micrec_rate() == ARATE,
                              .arate = ARATE, .audio_bytes = ab, .get_video = gv_cb, .get_audio = ga_cb, .u = 0 };
    if (!src.has_audio) src.audio_bytes = 0;
    uint32_t tcap = maxf + 65536u + 64u;
    uint8_t *tmp = (uint8_t *)kmalloc(tcap), *idx = (uint8_t *)kmalloc(16u * (2u * n + 2u));
    if (!tmp || !idx) {
        if (tmp) { kfree(tmp); }
        if (idx) { kfree(idx); }
        rc = -12; goto out;
    }
    char name[BIGFS_PATH_MAX + 1]; uint32_t p = 0;
    if (path && *path) ap_s(name, sizeof name, &p, path);
    else { ensure_dir(fs, "/videos"); next_name(fs, "/videos", "vid_", ".avi", name, sizeof name); }
    struct bsink bs = { fs, 0 };
    rc = bigfs_create(fs, name, &bs.ino);
    if (rc >= 0) {
        rc = avi_write(&src, sink_cb, &bs, tmp, tcap, idx);
        if (rc != 0) { bigfs_unlink(fs, name); rc = rc == AVI_WR_E_SINK ? -28 : -5; }
        else if (used) { uint32_t q = 0; ap_s(used, max, &q, name); }
    }
    kfree(tmp); kfree(idx);
out:
    saving = 0;
    if (rc == 0 || rc == -61) { camera_rec_discard(); }
    else { rec_audio = rec_audio; }
    return rc;
}

/* ---- orden de shell ---- */
static void put_cc(char *o, uint32_t max, uint32_t *p, const struct cam_hw_stats *s)
{
    int any = 0;
    for (int i = 0; i < 32; i++) if (s->cc[i]) { ap_s(o, max, p, any ? ", " : "  codigos de evento: "); ap_d(o, max, p, (uint64_t)i); ap_s(o, max, p, "="); ap_d(o, max, p, s->cc[i]); any = 1; }
    if (any) ap_s(o, max, p, "\n");
}

static void describe(char *o, uint32_t max, uint32_t *p)
{
    struct cam_hw_info i; int pr = cam_hw_probe(&i);
    if (pr != 0) { ap_s(o, max, p, "cam: "); ap_s(o, max, p, i.why[0] ? i.why : "sin camara"); ap_s(o, max, p, "\n"); return; }
    ap_s(o, max, p, "cam: camara USB "); ap_h(o, max, p, i.vid, 4); ap_s(o, max, p, ":"); ap_h(o, max, p, i.pid, 4);
    ap_s(o, max, p, i.speed == 3 ? ", alta velocidad" : i.speed == 4 ? ", SuperSpeed" : ", velocidad baja/completa");
    ap_s(o, max, p, ", UVC "); ap_d(o, max, p, (uint64_t)(i.cfg->bcd_uvc >> 8)); ap_s(o, max, p, "."); ap_h(o, max, p, i.cfg->bcd_uvc & 0xFF, 2); ap_s(o, max, p, "\n");
    for (int v = 0; v < i.cfg->n_vs; v++) {
        const struct uvc_stream *vs = &i.cfg->vs[v];
        ap_s(o, max, p, "  interfaz de video "); ap_d(o, max, p, vs->iface); ap_s(o, max, p, ": ajustes isocronos (bytes por microtrama):");
        for (int a = 0; a < vs->n_alt; a++) { ap_s(o, max, p, " "); ap_d(o, max, p, vs->alt[a].alt); ap_s(o, max, p, "="); ap_d(o, max, p, vs->alt[a].bytes); }
        if (vs->has_bulk) ap_s(o, max, p, " (tiene extremo bulk: no soportado)");
        ap_s(o, max, p, "\n");
        for (int f = 0; f < vs->n_fmt; f++) {
            const struct uvc_format *fm = &vs->fmt[f];
            ap_s(o, max, p, "    "); ap_s(o, max, p, fm->fourcc); ap_s(o, max, p, ":");
            for (int r = 0; r < fm->n_frames; r++) {
                const struct uvc_frame *fr = &fm->frame[r];
                ap_s(o, max, p, " "); ap_d(o, max, p, fr->w); ap_s(o, max, p, "x"); ap_d(o, max, p, fr->h);
                if (fr->n_int) { ap_s(o, max, p, "@"); for (int k = 0; k < fr->n_int; k++) { if (k) ap_s(o, max, p, "/"); ap_d(o, max, p, 10000000u / fr->interval[k]); } }
            }
            ap_s(o, max, p, "\n");
        }
    }
}

static void stats_text(char *o, uint32_t max, uint32_t *p)
{
    struct camera_info in; camera_info(&in); struct cam_hw_stats s; cam_hw_stats(&s);
    ap_s(o, max, p, "  estado: "); ap_s(o, max, p, in.running ? "en marcha" : "parada"); ap_s(o, max, p, ", "); ap_d(o, max, p, in.w); ap_s(o, max, p, "x"); ap_d(o, max, p, in.h);
    ap_s(o, max, p, " "); ap_s(o, max, p, in.fourcc); ap_s(o, max, p, ", pedidos "); ap_d(o, max, p, in.fps10 / 10); ap_s(o, max, p, " fps, reales ");
    ap_d(o, max, p, in.fps_x10 / 10); ap_s(o, max, p, ","); ap_d(o, max, p, in.fps_x10 % 10); ap_s(o, max, p, "\n");
    ap_s(o, max, p, "  tramas buenas "); ap_d(o, max, p, in.fr_ok); ap_s(o, max, p, ", malas "); ap_d(o, max, p, in.fr_bad); ap_s(o, max, p, ", ultima de ");
    ap_d(o, max, p, in.last_bytes); ap_s(o, max, p, " bytes; decodificadas "); ap_d(o, max, p, in.dec_ok); ap_s(o, max, p, ", con error "); ap_d(o, max, p, in.dec_err);
    if (in.dec_err) { ap_s(o, max, p, " (ultimo: "); ap_s(o, max, p, in.dec_last_err == JPEG_E_FORMAT ? "formato" : in.dec_last_err == JPEG_E_UNSUPPORTED ? "no soportado" : in.dec_last_err == JPEG_E_DATA ? "datos corruptos" : in.dec_last_err == JPEG_E_TRUNC ? "truncado" : in.dec_last_err == JPEG_E_SIZE ? "tamano" : "otro"); ap_s(o, max, p, ")"); }
    ap_s(o, max, p, "\n");
    ap_s(o, max, p, "  negociado: formato "); ap_d(o, max, p, s.fmt_index); ap_s(o, max, p, ", tamano "); ap_d(o, max, p, s.frm_index); ap_s(o, max, p, ", intervalo "); ap_d(o, max, p, s.interval);
    ap_s(o, max, p, ", carga util maxima "); ap_d(o, max, p, s.max_payload); ap_s(o, max, p, ", trama maxima "); ap_d(o, max, p, s.max_frame); ap_s(o, max, p, " (probe de "); ap_d(o, max, p, s.probe_len); ap_s(o, max, p, " bytes)\n");
    ap_s(o, max, p, "  extremo 0x"); ap_h(o, max, p, s.ep_addr, 2); ap_s(o, max, p, " ajuste "); ap_d(o, max, p, s.alt); ap_s(o, max, p, ", "); ap_d(o, max, p, s.pkt_bytes); ap_s(o, max, p, " bytes por microtrama\n");
    ap_s(o, max, p, "  TD encolados "); ap_d(o, max, p, s.td_queued); ap_s(o, max, p, ", completados "); ap_d(o, max, p, s.td_done); ap_s(o, max, p, ", con datos "); ap_d(o, max, p, s.ev_ok);
    ap_s(o, max, p, " (cortos "); ap_d(o, max, p, s.ev_short); ap_s(o, max, p, "), vacios "); ap_d(o, max, p, s.ev_zero); ap_s(o, max, p, ", con error "); ap_d(o, max, p, s.ev_err);
    ap_s(o, max, p, ", bytes "); ap_d(o, max, p, s.bytes); ap_s(o, max, p, ", anillo vacio "); ap_d(o, max, p, s.underrun); ap_s(o, max, p, ", eventos perdidos "); ap_d(o, max, p, s.evq_lost); ap_s(o, max, p, "\n");
    put_cc(o, max, p, &s);
    if (s.err[0]) { ap_s(o, max, p, "  error: "); ap_s(o, max, p, s.err); ap_s(o, max, p, "\n"); }
    if (in.msg[0]) { ap_s(o, max, p, "  aviso: "); ap_s(o, max, p, in.msg); ap_s(o, max, p, "\n"); }
    ap_s(o, max, p, "  probe: "); for (uint32_t k = 0; k < s.probe_len && k < 34; k++) { ap_h(o, max, p, s.probe[k], 2); if (k + 1 < s.probe_len && k + 1 < 34) ap_s(o, max, p, k % 4 == 3 ? " " : ""); }
    ap_s(o, max, p, "\n");
}

static void wait_frames(uint32_t n, uint32_t ms)
{
    uint64_t t = timer_get_uptime_ms() + ms;
    while (timer_get_uptime_ms() < t && fr_ok < n) thread_sleep(20);
}

int camera_command(const char *args, char *out, uint32_t max)
{
    const char *q = args ? args : ""; char sub[24], a1[40], a2[24], a3[24]; uint32_t p = 0; out[0] = 0;
    sub[0] = a1[0] = a2[0] = a3[0] = 0;
    if (!tok(&q, sub, sizeof sub) || seq(sub, "info")) {
        describe(out, max, &p);
        if (running) stats_text(out, max, &p);
    } else if (seq(sub, "estado")) {
        stats_text(out, max, &p);
    } else if (seq(sub, "iniciar")) {
        uint32_t w = 640, h = 480, fps = 30;
        if (tok(&q, a1, sizeof a1)) { w = num(a1); const char *x = a1; while (*x && *x != 'x') x++; if (*x == 'x') h = num(x + 1); }
        if (tok(&q, a2, sizeof a2)) fps = num(a2);
        int r = camera_start(w, h, fps);
        if (r != 0) { ap_s(out, max, &p, "cam: no se pudo iniciar: "); ap_s(out, max, &p, g_msg); ap_s(out, max, &p, "\n"); }
        else { wait_frames(5, 4000); describe(out, max, &p); stats_text(out, max, &p); }
    } else if (seq(sub, "parar")) {
        if (rec_on || nvf) camera_rec_discard();
        camera_stop(); ap_s(out, max, &p, "cam: parada");
    } else if (seq(sub, "foto")) {
        int own = 0;
        if (!running) { int r = camera_start(640, 480, 30); if (r != 0) { ap_s(out, max, &p, "cam: no se pudo iniciar: "); ap_s(out, max, &p, g_msg); goto end; } own = 1; wait_frames(8, 5000); }
        else wait_frames(fr_ok + 2, 2000);
        char used[96]; used[0] = 0; a1[0] = 0; tok(&q, a1, sizeof a1);
        int rc = camera_photo(a1, used, sizeof used);
        if (rc == 0) { ap_s(out, max, &p, "cam: foto guardada en "); ap_s(out, max, &p, used); }
        else { ap_s(out, max, &p, "cam: no se pudo guardar la foto: "); ap_s(out, max, &p, rc == -61 ? "no ha llegado ninguna trama" : rc == -19 ? "almacen BIGFS no disponible (big format yes)" : bigfs_strerror(rc)); }
        if (own) { camera_stop(); }
        ap_s(out, max, &p, "\n"); stats_text(out, max, &p);
    } else if (seq(sub, "video")) {
        uint32_t s = 5; if (tok(&q, a1, sizeof a1)) s = num(a1);
        if (s < 1) s = 1;
        if (s > 120) s = 120;
        int own = 0;
        if (!running) { int r = camera_start(640, 480, 30); if (r != 0) { ap_s(out, max, &p, "cam: no se pudo iniciar: "); ap_s(out, max, &p, g_msg); goto end; } own = 1; wait_frames(5, 5000); }
        int rc = camera_rec_start(1);
        if (rc != 0) { ap_s(out, max, &p, "cam: no se puede grabar"); if (own) camera_stop(); goto end; }
        uint64_t t = timer_get_uptime_ms() + s * 1000u;
        while (timer_get_uptime_ms() < t && rec_on) thread_sleep(50);
        char used[96]; used[0] = 0;
        uint32_t nf = nvf; int had_audio = rec_audio;
        rc = camera_rec_stop(0, used, sizeof used);
        if (rc == 0) { ap_s(out, max, &p, "cam: video guardado en "); ap_s(out, max, &p, used); ap_s(out, max, &p, " ("); ap_d(out, max, &p, nf); ap_s(out, max, &p, " tramas"); ap_s(out, max, &p, had_audio ? ", con audio" : ""); ap_s(out, max, &p, ")"); }
        else { ap_s(out, max, &p, "cam: no se pudo guardar el video: "); ap_s(out, max, &p, rc == -61 ? "casi no llegaron tramas" : rc == -19 ? "almacen BIGFS no disponible (big format yes)" : bigfs_strerror(rc)); }
        if (own) camera_stop();
        ap_s(out, max, &p, "\n"); stats_text(out, max, &p);
    } else if (seq(sub, "abrir")) {
        extern void gui_open_camera(void); gui_open_camera(); ap_s(out, max, &p, "Camara abierta.");
    } else ap_s(out, max, &p, "uso: cam info|estado|iniciar [AnchoxAlto] [fps]|parar|foto [ruta]|video [segundos]|abrir");
end:
    kprint(out); kprint("\n");
    return 1;
}
