/* SOMA_179_T480S: captura de pantalla. Ver scrcap.h */
#include "acceso.h"      /* SOMA_363_ACCESO */
#include "fs.h"
#include <stdint.h>
#include <stddef.h>
#include "scrcap.h"
#include "fb.h"
#include "jpeg_enc.h"
#include "png_enc.h"
#include "avi_wr.h"
#include "wav.h"
#include "audio_sys.h"
#include "bigfs.h"
#include "bigfs_glue.h"
#include "clipboard.h"
#ifdef __x86_64__
#include "bootfb.h"
#endif
#include "thread.h"
#include "mem.h"
#include "console.h"
#include "idt.h"

#define VCH_BYTES  (512u * 1024u)
#define VMAXCH     96u                        /* 48 MiB de video como maximo */
#define VMAXF      30000u
#define ACH_SAMP   32768u                     /* muestras de 16 bits por trozo de audio (64 KiB) */
#define AMAXCH     512u                       /* 32 MiB de audio: unos 5 min 49 s a 48 kHz mono */
#define ARATE      48000u
#define QUALITY    65

static int started;
static struct scrcap_info G;                  /* lo que ve la interfaz */
static volatile int rec_on, saving, a_on, a_full, tap_in, busy_shot, quit_req;
static uint32_t rec_mode, opt_fps = 10, opt_div = 2;
static uint64_t t0, next_frame, fps_t0; static uint32_t fps_n, enc_sum, enc_cnt;
static uint32_t ow, oh;
static uint32_t *sbuf; static uint8_t *jbuf; static uint32_t jcap; static uint64_t last_hash; static uint32_t last_off, last_len;
static uint8_t *vch[VMAXCH]; static uint32_t nvch, vch_used;
static uint32_t *vf_off, *vf_len, *vf_ms; static uint32_t nvf, ndup, nskip; static uint64_t vbytes;
static int16_t *ach[AMAXCH]; static volatile uint64_t apos;
static struct scrcap_rs rs; static uint32_t level_peak; static uint32_t tap_calls, tap_rate;
/* peticiones */
static volatile int pend_shot, pend_start, pend_stop, hk_shot, hk_toggle; static uint64_t pend_shot_at, pend_start_at; static uint32_t pend_start_mode; static char pend_path[96];

static void setmsg(const char *s) { uint32_t i = 0; while (s && s[i] && i + 1 < sizeof G.msg) { G.msg[i] = s[i]; i++; } G.msg[i] = 0; G.msg_seq++; }
static void ap_s(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; o[*p < max ? *p : max - 1] = 0; }
static void ap_d(char *o, uint32_t max, uint32_t *p, uint64_t v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } ap_s(o, max, p, b + i); }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static uint32_t num(const char *s) { uint32_t v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0'); return v; }
static int tok(const char **q, char *o, uint32_t max) { const char *s = *q; while (*s == ' ') s++; if (!*s) return 0; uint32_t i = 0; while (*s && *s != ' ') { if (i + 1 < max) o[i++] = *s; s++; } o[i] = 0; *q = s; return 1; }
static void mcopy(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }
static void cpy(char *d, uint32_t max, const char *s) { uint32_t i = 0; while (s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }
static const char *errtxt(int r)
{
    switch (r) {
    case -12: return "sin memoria";
    case -16: return "ocupado (ya hay una captura en marcha)";
    case -19: return "no hay almacen BIGFS: usa 'big format yes'";
    case -22: return "argumento no valido";
    case -28: return "sin espacio en el almacen";
    case -61: return "no hay nada que guardar (demasiado corto)";
    case -5:  return "error al codificar o escribir";
    default:  return bigfs_strerror(r);
    }
}

/* ---- audio interno ---- */
static void a_put(int16_t s)
{
    uint64_t i = apos; uint64_t c = i / ACH_SAMP;
    if (c >= AMAXCH) { a_full = 1; a_on = 0; return; }
    if (!ach[c]) { ach[c] = (int16_t *)kmalloc(ACH_SAMP * 2u); if (!ach[c]) { a_full = 1; a_on = 0; return; } }
    ach[c][i % ACH_SAMP] = s; apos = i + 1;
}
static void a_pad(uint64_t upto) { while (apos < upto && a_on) a_put(0); }

static void tap(const int16_t *st, uint32_t frames, uint32_t rate, uint32_t queued)
{
    if (!a_on || !rate) return;
    __sync_fetch_and_add(&tap_in, 1);
    if (a_on) {
        tap_calls++; tap_rate = rate;
        if (rs.rate != rate) scrcap_rs_reset(&rs, rate);
        uint32_t pk = 0; for (uint32_t i = 0; i < frames; i++) { int32_t l = st[2 * i], r = st[2 * i + 1]; uint32_t a = (uint32_t)((l < 0 ? -l : l) > (r < 0 ? -r : r) ? (l < 0 ? -l : l) : (r < 0 ? -r : r)); if (a > pk) pk = a; }
        level_peak = pk > 32767 ? 32767 : pk;
        uint64_t now = timer_get_uptime_ms();
        uint64_t end_exp = (now - t0) * (ARATE / 1000u) + (uint64_t)queued * ARATE / rate;
        uint64_t nest = (uint64_t)frames * ARATE / rate;
        uint64_t start = end_exp > nest ? end_exp - nest : 0;
        if (apos + 960u < start) a_pad(start);                     /* hubo silencio antes de este sonido */
        int16_t tmp[512];
        uint32_t done = 0, per = 128;                               /* 128 entradas dan, como mucho, 128*48000/8000 = 768 salidas: se hacen porciones de 64 */
        per = 64;
        while (done < frames && a_on) {
            uint32_t k = frames - done < per ? frames - done : per;
            uint32_t m = scrcap_rs_run(&rs, st + 2u * done, k, tmp, 512);
            for (uint32_t i = 0; i < m && a_on; i++) a_put(tmp[i]);
            done += k;
        }
    }
    __sync_fetch_and_sub(&tap_in, 1);
}

static void a_free(void) { for (uint32_t i = 0; i < AMAXCH; i++) if (ach[i]) { kfree(ach[i]); ach[i] = 0; } apos = 0; }
static void v_free(void)
{
    for (uint32_t i = 0; i < nvch; i++) { kfree(vch[i]); vch[i] = 0; }
    nvch = 0; vch_used = 0; nvf = 0; ndup = 0; nskip = 0; vbytes = 0; last_len = 0; last_hash = 0;
    if (sbuf) { kfree(sbuf); sbuf = 0; }
    if (jbuf) { kfree(jbuf); jbuf = 0; }
    if (vf_off) { kfree(vf_off); vf_off = 0; }
    if (vf_len) { kfree(vf_len); vf_len = 0; }
    if (vf_ms) { kfree(vf_ms); vf_ms = 0; }
}

/* ---- BIGFS ---- */
static int ensure_dir(struct bigfs *fs, const char *d) { uint32_t ino; if (bigfs_lookup(fs, d, &ino) == (int)BIGFS_T_DIR) return 0; return bigfs_mkdir(fs, d); }
static void next_name(struct bigfs *fs, const char *dir, const char *pr, const char *ext, char *name, uint32_t max)
{
    for (uint32_t i = 1; i < 1000; ++i) {
        uint32_t p = 0; ap_s(name, max, &p, dir); ap_s(name, max, &p, "/"); ap_s(name, max, &p, pr);
        if (i < 100) ap_s(name, max, &p, "0");
        if (i < 10) ap_s(name, max, &p, "0");
        ap_d(name, max, &p, i); ap_s(name, max, &p, ext);
        uint32_t ino; if (bigfs_lookup(fs, name, &ino) < 0) break;
    }
}
struct bsink { struct bigfs *fs; uint32_t ino; };
static int sink_cb(void *u, uint64_t off, const void *b, uint32_t n)
{
    struct bsink *s = (struct bsink *)u;
    return bigfs_write(s->fs, s->ino, off, b, n) == (int64_t)n ? 0 : -1;
}
static void remember(const char *name) { cpy(G.last, sizeof G.last, name); }

/* ---- imagen ---- */
int scrcap_shot(const char *path, char *used, uint32_t max)
{
    if (!fb_is_active()) return -19 - 1000;                                   /* -1019: sin pantalla grafica */
    if (__sync_lock_test_and_set(&busy_shot, 1)) return -16;
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    int rc = 0;
    int usu = (!path || !*path) && acceso_usuario();               /* SOMA_363_ACCESO: las capturas, a /usuario/Imagenes/Capturas */
    if (!fs && !usu) { rc = -19; goto out; }
    uint32_t W = fb_get_width(), H = fb_get_height();
    if (!W || !H) { rc = -22; goto out; }
    uint32_t *px = (uint32_t *)kmalloc((size_t)W * H * 4u);
    size_t cap = (size_t)W * H * 4u + 4096u, wk = png_work_size((int)W, (int)H);
    uint8_t *png = (uint8_t *)kmalloc(cap); void *work = kmalloc(wk);
    if (!px || !png || !work) { rc = -12; goto free3; }
    if (!fb_read_rect(0, 0, (int32_t)W, (int32_t)H, px)) { rc = -22; goto free3; }
    size_t pn = png_encode_rgb(px, (int)W, (int)H, png, cap, work);
    if (!pn) { rc = -5; goto free3; }
    char name[BIGFS_PATH_MAX + 1]; uint32_t p = 0;
    if (usu) {
        char dir[64]; acceso_carpeta(ACC_CAPTURAS, dir, sizeof dir);
        if (acceso_libre(dir, "captura_", ".png", name, sizeof name) != 0 || vfs_write(name, (const char *)png, (uint32_t)pn) < 0) rc = -28;
        else { rc = 0; if (used) { uint32_t q = 0; ap_s(used, max, &q, name); } remember(name); }
        goto free3;
    }
    if (path && *path) ap_s(name, sizeof name, &p, path);
    else { ensure_dir(fs, "/capturas"); next_name(fs, "/capturas", "captura_", ".png", name, sizeof name); }
    uint32_t ino; rc = bigfs_create(fs, name, &ino);
    if (rc >= 0) {
        int64_t w = bigfs_write(fs, ino, 0, png, (uint32_t)pn);
        if (w != (int64_t)pn) { bigfs_unlink(fs, name); rc = w < 0 ? (int)w : -28; }
        else { rc = 0; if (used) { uint32_t q = 0; ap_s(used, max, &q, name); } remember(name); }
    }
free3:
    if (px) { kfree(px); }
    if (png) { kfree(png); }
    if (work) { kfree(work); }
out:
    busy_shot = 0;
    return rc;
}

/* ---- video ---- */
static int rec_alloc(void)
{
    uint32_t W = fb_get_width(), H = fb_get_height(), d = opt_div;
    ow = W / d; oh = H / d;
    if (!fb_is_active() || ow < 16 || oh < 16) return -19;
    sbuf = (uint32_t *)kmalloc((size_t)ow * oh * 4u);
    jcap = ow * oh * 2u + 65536u; jbuf = (uint8_t *)kmalloc(jcap);
    vf_off = (uint32_t *)kmalloc(VMAXF * 4u); vf_len = (uint32_t *)kmalloc(VMAXF * 4u); vf_ms = (uint32_t *)kmalloc(VMAXF * 4u);
    if (!sbuf || !jbuf || !vf_off || !vf_len || !vf_ms) { v_free(); return -12; }
    return 0;
}

static void grab_frame(void)
{
    uint64_t a = timer_get_uptime_ms();
    uint32_t W = fb_get_width(), H = fb_get_height(), d = opt_div;
    const volatile uint32_t *fbp = fb_get_buffer();
    if (!fb_is_active() || !fbp || W / d != ow || H / d != oh) { nskip++; return; }       /* cambio de resolucion: no se mezclan tamanos */
    scrcap_box((const uint32_t *)fbp, W, ow, oh, d, sbuf);
    uint64_t h = 1469598103934665603ull; const uint32_t *q = sbuf; uint32_t n = ow * oh;
    for (uint32_t i = 0; i < n; i++) { h ^= q[i]; h *= 1099511628211ull; }
    if (nvf >= VMAXF) { rec_on = 0; setmsg("Limite de tramas: grabacion detenida"); return; }
    uint32_t off, len;
    if (nvf && h == last_hash && last_len) { off = last_off; len = last_len; ndup++; }           /* pantalla quieta: se repite la trama sin gastar memoria */
    else {
        size_t jn = jpeg_encode_rgb(sbuf, (int)ow, (int)oh, (int)ow, QUALITY, jbuf, jcap);
        if (!jn || jn > VCH_BYTES) { nskip++; if (!nvf) return; off = last_off; len = last_len; ndup++; }
        else {
            if (!nvch || vch_used + (uint32_t)jn > VCH_BYTES) {
                if (nvch >= VMAXCH) { rec_on = 0; setmsg("Memoria llena: grabacion detenida"); return; }
                uint8_t *c = (uint8_t *)kmalloc(VCH_BYTES);
                if (!c) { rec_on = 0; setmsg("Sin memoria: grabacion detenida"); return; }
                vch[nvch++] = c; vch_used = 0;
            }
            mcopy(vch[nvch - 1] + vch_used, jbuf, (uint32_t)jn);
            off = (nvch - 1) * VCH_BYTES + vch_used; len = (uint32_t)jn; vch_used += len; vbytes += len;
            last_hash = h; last_off = off; last_len = len;
        }
    }
    vf_off[nvf] = off; vf_len[nvf] = len; vf_ms[nvf] = (uint32_t)(a - t0); nvf++;
    uint64_t b = timer_get_uptime_ms();
    enc_sum += (uint32_t)(b - a); enc_cnt++; fps_n++;
    if (!fps_t0) fps_t0 = b;
    if (b - fps_t0 >= 1000) { G.fps_x10 = (uint32_t)((uint64_t)fps_n * 10000u / (b - fps_t0)); fps_n = 0; fps_t0 = b; }
}

static int start_now(uint32_t mode)
{
    if (rec_on || saving) return -16;
    if (!(mode & (SC_VIDEO | SC_AUDIO))) return -22;
    if (!bigfs_sys(&(int){0})) { return -19; }
    v_free(); a_free(); a_full = 0; a_on = 0; G.frames = 0;
    if (mode & SC_VIDEO) { int r = rec_alloc(); if (r < 0) return r; }
    if (mode & SC_AUDIO) {
        if (!audio_sys_available()) {
            if (!(mode & SC_VIDEO)) return -19 - 1000 + 1;                    /* -1018: sin salida de audio */
            mode &= ~SC_AUDIO; setmsg("No hay salida de audio: se graba solo la imagen");
        }
    }
    rec_mode = mode; scrcap_rs_reset(&rs, 0); rs.rate = 0;
    level_peak = 0; tap_calls = 0; enc_sum = enc_cnt = 0; fps_t0 = 0; fps_n = 0; G.fps_x10 = 0;
    t0 = timer_get_uptime_ms(); next_frame = t0;
    if (mode & SC_AUDIO) a_on = 1;
    if (mode & SC_VIDEO) rec_on = 1; else rec_on = 2;                        /* 2 = solo audio */
    return 0;
}

struct vsrc { int dummy; };
static uint32_t gv_cb(void *u, uint32_t i, uint8_t *dst, uint32_t cap)
{
    (void)u; uint32_t l = vf_len[i]; if (l > cap) return 0;
    mcopy(dst, vch[vf_off[i] / VCH_BYTES] + vf_off[i] % VCH_BYTES, l);
    return l;
}
static uint32_t ga_cb(void *u, uint64_t off, void *dst, uint32_t n)
{
    (void)u; uint64_t total = apos * 2u; if (off >= total) return 0;
    if (n > total - off) n = (uint32_t)(total - off);
    uint8_t *d = (uint8_t *)dst; uint32_t k = 0;
    while (k < n) {
        uint64_t o = off + k, c = o / (ACH_SAMP * 2u); uint32_t in = (uint32_t)(o % (ACH_SAMP * 2u)), m = ACH_SAMP * 2u - in;
        if (m > n - k) m = n - k;
        if (!ach[c]) break;
        mcopy(d + k, (const uint8_t *)ach[c] + in, m); k += m;
    }
    return k;
}

static int save_all(const char *path, char *used, uint32_t max)
{
    int rc = 0; int e = 0; struct bigfs *fs = bigfs_sys(&e);
    uint64_t stop_ms = timer_get_uptime_ms() - t0;
    if (rec_mode & SC_AUDIO) { a_on = 1; a_pad(stop_ms * (ARATE / 1000u)); }
    a_on = 0; for (int i = 0; i < 200 && tap_in; i++) thread_sleep(2);
    int usu = (!path || !*path) && acceso_usuario();               /* SOMA_363_ACCESO: videos y audio, a /usuario/Videos y /usuario/Audio */
    if (!fs && !usu) return -19;
    char name[BIGFS_PATH_MAX + 1]; uint32_t p = 0; struct bsink bs = { fs, 0 };
    struct acc_sec as; char dir[64];
    if (rec_mode & SC_VIDEO) {
        uint32_t n = nvf; if (n < 2) return -61;
        uint32_t first = vf_ms[0], last = vf_ms[n - 1], span = last > first ? last - first : 1u;
        uint32_t upf = (uint32_t)((uint64_t)span * 1000u / (n - 1u)); if (upf < 1000u) upf = 1000u;
        uint32_t maxf = 0; for (uint32_t i = 0; i < n; i++) if (vf_len[i] > maxf) maxf = vf_len[i];
        uint64_t dur_ms = (uint64_t)span + upf / 1000u, ab = 0;
        if (rec_mode & SC_AUDIO) { ab = apos * 2u; uint64_t want = (dur_ms + 50u) * (ARATE / 1000u) * 2u; if (ab > want) ab = want; ab &= ~1ull; }
        struct avi_wr_src src = { .w = ow, .h = oh, .nframes = n, .usec_per_frame = upf, .lens = vf_len, .has_audio = ab >= 2, .arate = ARATE,
                                  .audio_bytes = ab, .get_video = gv_cb, .get_audio = ga_cb, .u = 0 };
        uint32_t tcap = maxf + 65536u + 64u;
        uint8_t *tmp = (uint8_t *)kmalloc(tcap), *idx = (uint8_t *)kmalloc(16u * (2u * n + 2u));
        if (!tmp || !idx) { if (tmp) { kfree(tmp); } if (idx) { kfree(idx); } return -12; }
        if (usu) {
            acceso_carpeta(ACC_VIDEOS, dir, sizeof dir);
            if (acceso_libre(dir, "pantalla_", ".avi", name, sizeof name) != 0 || acceso_sec_abre(&as, name) != 0) rc = -28;
            else { rc = avi_write(&src, acceso_sec_pon, &as, tmp, tcap, idx); if (acceso_sec_fin(&as, rc == 0) != 0) rc = rc == 0 || rc == AVI_WR_E_SINK ? -28 : -5; }
            kfree(tmp); kfree(idx);
            if (rc == 0) { if (used) { uint32_t q = 0; ap_s(used, max, &q, name); } remember(name); }
            return rc;
        }
        if (path && *path) ap_s(name, sizeof name, &p, path);
        else { ensure_dir(fs, "/capturas"); next_name(fs, "/capturas", (rec_mode & SC_AUDIO) ? "pantalla_" : "pantalla_", ".avi", name, sizeof name); }
        rc = bigfs_create(fs, name, &bs.ino);
        if (rc >= 0) {
            rc = avi_write(&src, sink_cb, &bs, tmp, tcap, idx);
            if (rc != 0) { bigfs_unlink(fs, name); rc = rc == AVI_WR_E_SINK ? -28 : -5; }
        }
        kfree(tmp); kfree(idx);
    } else {
        uint64_t bytes = apos * 2u;
        if (bytes < (uint64_t)ARATE / 5u * 2u) return -61;                       /* menos de 0,2 s */
        if (usu) {
            acceso_carpeta(ACC_AUDIO, dir, sizeof dir);
            if (acceso_libre(dir, "audio_", ".wav", name, sizeof name) != 0 || acceso_sec_abre(&as, name) != 0) return -28;
            uint8_t hdr[44]; wav_make_header(hdr, 1, ARATE, (uint32_t)bytes);
            rc = acceso_sec_pon(&as, 0, hdr, 44) ? -28 : 0;
            uint8_t *buf = (uint8_t *)kmalloc(65536u); if (!buf) rc = -12;
            for (uint64_t off = 0; rc >= 0 && off < bytes; ) {
                uint32_t k = ga_cb(0, off, buf, 65536u); if (!k) { rc = -5; break; }
                if (acceso_sec_pon(&as, 44u + off, buf, k)) { rc = -28; break; }
                off += k;
            }
            if (buf) kfree(buf);
            if (acceso_sec_fin(&as, rc == 0) != 0 && rc == 0) rc = -28;
            if (rc == 0) { if (used) { uint32_t q = 0; ap_s(used, max, &q, name); } remember(name); }
            return rc;
        }
        if (path && *path) ap_s(name, sizeof name, &p, path);
        else { ensure_dir(fs, "/capturas"); next_name(fs, "/capturas", "audio_", ".wav", name, sizeof name); }
        rc = bigfs_create(fs, name, &bs.ino);
        if (rc >= 0) {
            uint8_t hdr[44]; wav_make_header(hdr, 1, ARATE, (uint32_t)bytes);
            if (bigfs_write(fs, bs.ino, 0, hdr, 44) != 44) rc = -28;
            uint8_t *buf = (uint8_t *)kmalloc(65536u); if (!buf) rc = -12;
            for (uint64_t off = 0; rc >= 0 && off < bytes; ) {
                uint32_t k = ga_cb(0, off, buf, 65536u); if (!k) { rc = -5; break; }
                if (bigfs_write(fs, bs.ino, 44u + off, buf, k) != (int64_t)k) { rc = -28; break; }
                off += k;
            }
            if (buf) kfree(buf);
            if (rc < 0) bigfs_unlink(fs, name); else rc = 0;
        }
    }
    if (rc == 0) { if (used) { uint32_t q = 0; ap_s(used, max, &q, name); } remember(name); }
    return rc;
}

int scrcap_discard(void)
{
    rec_on = 0; a_on = 0; for (int i = 0; i < 200 && tap_in; i++) thread_sleep(2);
    v_free(); a_free(); G.frames = 0;
    return 0;
}

int scrcap_stop(const char *path, char *used, uint32_t max)
{
    if (!rec_on) return -61;
    if (saving) return -16;
    saving = 1; rec_on = 0;
    int rc = save_all(path, used, max);
    saving = 0;
    if (rc != -12 && rc != -28) scrcap_discard();                             /* si falta sitio se conserva lo grabado para reintentar */
    return rc;
}

/* ---- estado para la interfaz ---- */
void scrcap_info_get(struct scrcap_info *o)
{
    uint64_t now = timer_get_uptime_ms();
    G.state = saving ? SC_SAVING : rec_on ? SC_REC : (pend_start || pend_shot) ? SC_WAIT : SC_IDLE;
    G.mode = rec_on || saving ? rec_mode : 0;
    G.wait_ms = (pend_start && pend_start_at > now) ? (uint32_t)(pend_start_at - now) : (pend_shot && pend_shot_at > now) ? (uint32_t)(pend_shot_at - now) : 0;
    G.rec_ms = (rec_on || saving) ? (uint32_t)(now - t0) : 0;
    G.w = ow; G.h = oh; G.fps = opt_fps; G.div = opt_div; G.frames = nvf; G.dup = ndup; G.skipped = nskip;
    G.enc_ms = enc_cnt ? enc_sum / enc_cnt : 0; G.bytes = vbytes + apos * 2u; G.audio_ms = (uint32_t)(apos / (ARATE / 1000u));
    G.level = level_peak; level_peak = level_peak > 600 ? level_peak - level_peak / 8 : 0;
    G.audio_calls = tap_calls; G.audio_rate = tap_rate; G.busy_shot = busy_shot;
    *o = G;
}
void scrcap_set_options(uint32_t fps, uint32_t div)
{
    if (rec_on || saving) return;
    if (fps >= 1 && fps <= 30) opt_fps = fps;
    if (div >= 1 && div <= 4) opt_div = div;
}

/* ---- peticiones asincronas y hilo ---- */
int scrcap_shot_async(const char *path, uint32_t delay_ms)
{
    if (!started) return -19;
    if (pend_shot || busy_shot) return -16;
    pend_path[0] = 0; if (path) cpy(pend_path, sizeof pend_path, path);
    pend_shot_at = timer_get_uptime_ms() + delay_ms; pend_shot = 1;
    return 0;
}
int scrcap_start(uint32_t mode, uint32_t delay_ms)
{
    if (!started) return -19;
    if (rec_on || saving || pend_start) return -16;
    if (!delay_ms) return start_now(mode);
    pend_start_mode = mode; pend_start_at = timer_get_uptime_ms() + delay_ms; pend_start = 1;
    return 0;
}
int scrcap_stop_async(void) { if (pend_start) { pend_start = 0; return 0; } if (!rec_on) return -61; pend_stop = 1; return 0; }
void scrcap_hotkey(int ctrl) { if (ctrl) hk_toggle = 1; else hk_shot = 1; }

static void report(const char *what, int r, const char *name)
{
    char m[112]; uint32_t p = 0;
    if (r == 0) { ap_s(m, sizeof m, &p, what); ap_s(m, sizeof m, &p, " guardado en "); ap_s(m, sizeof m, &p, name); }
    else { ap_s(m, sizeof m, &p, "No se pudo guardar: "); ap_s(m, sizeof m, &p, r <= -1000 ? (r == -1019 ? "no hay pantalla grafica" : "no hay salida de audio") : errtxt(r)); }
    setmsg(m); clip_toast(m);
}

static void daemon_main(void *arg)
{
    (void)arg;
    for (;;) {
        uint64_t now = timer_get_uptime_ms();
        if (hk_shot) { hk_shot = 0; if (!pend_shot) { pend_path[0] = 0; pend_shot_at = now; pend_shot = 1; } }
        if (hk_toggle) {
            hk_toggle = 0;
            if (rec_on) pend_stop = 1;
            else { int r = start_now(SC_VIDEO | SC_AUDIO); if (r == 0) clip_toast("Grabando pantalla y audio: Control+ImprPant para parar"); else report("Video", r, ""); }
        }
        if (pend_shot && now >= pend_shot_at) {
            char name[96]; name[0] = 0; char pth[96]; cpy(pth, sizeof pth, pend_path);
            pend_shot = 0;
            int r = scrcap_shot(pth[0] ? pth : 0, name, sizeof name);
            report("Imagen", r, name);
        }
        if (pend_start && now >= pend_start_at) {
            pend_start = 0; int r = start_now(pend_start_mode);
            if (r == 0) setmsg("Grabando..."); else report("Captura", r, "");
        }
        if (pend_stop) {
            pend_stop = 0; char name[96]; name[0] = 0;
            setmsg("Guardando..."); int r = scrcap_stop(0, name, sizeof name);
            report((rec_mode & SC_VIDEO) ? "Video" : "Audio", r, name);
        }
        if (rec_on == 1) {
            if (now >= next_frame) {
                grab_frame();
                uint32_t period = 1000u / opt_fps; uint64_t after = timer_get_uptime_ms();
                next_frame += period; if (next_frame + period < after) next_frame = after;
            }
            uint64_t w = next_frame > timer_get_uptime_ms() ? next_frame - timer_get_uptime_ms() : 0;
            thread_sleep(w > 10 ? 10 : (uint32_t)(w ? w : 1));
        } else if (rec_on == 2 && a_full) { rec_on = 2; thread_sleep(50); }
        else thread_sleep(rec_on ? 30 : 25);
        if (quit_req) break;
    }
}

void scrcap_init(void)
{
    if (started) return;
#ifdef __x86_64__
    if (bootinfo_disabled("scrcap")) return;
#endif
    if (thread_create("scrcapd", daemon_main, 0)) { started = 1; audio_sys_set_tap(tap); }
}

/* ---- orden de shell ---- */
static void status_text(char *o, uint32_t max, uint32_t *p)
{
    struct scrcap_info i; scrcap_info_get(&i);
    ap_s(o, max, p, "captura: ");
    ap_s(o, max, p, !started ? "NO ACTIVA (soma.scrcap=off o sin hilo)" : i.state == SC_REC ? "grabando" : i.state == SC_SAVING ? "guardando" : i.state == SC_WAIT ? "cuenta atras" : "lista");
    if (i.state == SC_REC || i.state == SC_SAVING) {
        ap_s(o, max, p, " ("); ap_s(o, max, p, (i.mode & SC_VIDEO) ? "video" : ""); ap_s(o, max, p, (i.mode & SC_VIDEO) && (i.mode & SC_AUDIO) ? " + " : ""); ap_s(o, max, p, (i.mode & SC_AUDIO) ? "audio interno" : ""); ap_s(o, max, p, ")");
        ap_s(o, max, p, ", "); ap_d(o, max, p, i.rec_ms / 1000u); ap_s(o, max, p, " s");
    }
    ap_s(o, max, p, "\n  ajustes: "); ap_d(o, max, p, i.fps); ap_s(o, max, p, " tramas/s, escala 1/"); ap_d(o, max, p, i.div);
    ap_s(o, max, p, "\n  video: "); ap_d(o, max, p, i.w); ap_s(o, max, p, "x"); ap_d(o, max, p, i.h); ap_s(o, max, p, ", tramas "); ap_d(o, max, p, i.frames);
    ap_s(o, max, p, " (repetidas "); ap_d(o, max, p, i.dup); ap_s(o, max, p, ", perdidas "); ap_d(o, max, p, i.skipped); ap_s(o, max, p, "), ritmo "); ap_d(o, max, p, i.fps_x10 / 10u); ap_s(o, max, p, ","); ap_d(o, max, p, i.fps_x10 % 10u);
    ap_s(o, max, p, " /s, "); ap_d(o, max, p, i.enc_ms); ap_s(o, max, p, " ms por trama");
    ap_s(o, max, p, "\n  audio interno: "); ap_d(o, max, p, i.audio_ms); ap_s(o, max, p, " ms guardados, porciones "); ap_d(o, max, p, i.audio_calls); ap_s(o, max, p, ", frecuencia vista "); ap_d(o, max, p, i.audio_rate); ap_s(o, max, p, " Hz, nivel "); ap_d(o, max, p, i.level);
    ap_s(o, max, p, "\n  memoria usada: "); ap_d(o, max, p, i.bytes / 1024u); ap_s(o, max, p, " KiB");
    if (i.last[0]) { ap_s(o, max, p, "\n  ultimo archivo: "); ap_s(o, max, p, i.last); }
    if (i.msg[0]) { ap_s(o, max, p, "\n  mensaje: "); ap_s(o, max, p, i.msg); }
    ap_s(o, max, p, "\n");
}

static void wait_secs(uint32_t s)
{
    uint64_t t = timer_get_uptime_ms() + s * 1000u;
    while (timer_get_uptime_ms() < t && rec_on) thread_sleep(50);
}

int scrcap_command(const char *args, char *out, uint32_t max)
{
    const char *q = args ? args : ""; char sub[24], a1[96], a2[24]; uint32_t p = 0; out[0] = 0; sub[0] = a1[0] = a2[0] = 0;
    if (!tok(&q, sub, sizeof sub) || seq(sub, "estado") || seq(sub, "info")) { status_text(out, max, &p); goto end; }
    if (!started) { ap_s(out, max, &p, "captura: no esta activa (soma.scrcap=off o no se pudo crear el hilo)"); goto end; }
    if (seq(sub, "foto") || seq(sub, "imagen")) {
        char used[96]; used[0] = 0; tok(&q, a1, sizeof a1);
        int r = scrcap_shot(a1[0] ? a1 : 0, used, sizeof used);
        if (r == 0) { ap_s(out, max, &p, "captura: imagen guardada en "); ap_s(out, max, &p, used); }
        else { ap_s(out, max, &p, "captura: no se pudo guardar la imagen: "); ap_s(out, max, &p, r == -1019 ? "no hay pantalla grafica" : errtxt(r)); }
    } else if (seq(sub, "video") || seq(sub, "audio") || seq(sub, "av")) {
        uint32_t mode = seq(sub, "video") ? SC_VIDEO : seq(sub, "audio") ? SC_AUDIO : (SC_VIDEO | SC_AUDIO), s = 5;
        if (tok(&q, a1, sizeof a1)) s = num(a1);
        if (s < 1) s = 1;
        if (s > 120) s = 120;
        int r = start_now(mode);
        if (r != 0) { ap_s(out, max, &p, "captura: no se puede grabar: "); ap_s(out, max, &p, r == -1018 ? "no hay salida de audio" : errtxt(r)); goto end; }
        wait_secs(s);
        char used[96]; used[0] = 0; uint32_t nf = nvf, calls = tap_calls;
        r = scrcap_stop(0, used, sizeof used);
        if (r == 0) {
            ap_s(out, max, &p, "captura: guardado en "); ap_s(out, max, &p, used);
            if (mode & SC_VIDEO) { ap_s(out, max, &p, " ("); ap_d(out, max, &p, nf); ap_s(out, max, &p, " tramas)"); }
            if ((mode & SC_AUDIO)) { ap_s(out, max, &p, calls ? "" : " (SIN SONIDO: nada se reprodujo mientras grababa)"); }
        } else { ap_s(out, max, &p, "captura: no se pudo guardar: "); ap_s(out, max, &p, errtxt(r)); }
        ap_s(out, max, &p, "\n"); status_text(out, max, &p);
    } else if (seq(sub, "iniciar")) {
        tok(&q, a1, sizeof a1);
        uint32_t mode = seq(a1, "video") ? SC_VIDEO : seq(a1, "audio") ? SC_AUDIO : (seq(a1, "av") || !a1[0]) ? (SC_VIDEO | SC_AUDIO) : 0;
        if (!mode) { ap_s(out, max, &p, "uso: captura iniciar video|audio|av"); goto end; }
        int r = scrcap_start(mode, 0);
        if (r == 0) ap_s(out, max, &p, "captura: grabando. Para y guarda con: captura parar");
        else { ap_s(out, max, &p, "captura: no se puede grabar: "); ap_s(out, max, &p, r == -1018 ? "no hay salida de audio" : errtxt(r)); }
    } else if (seq(sub, "parar")) {
        char used[96]; used[0] = 0; int r = scrcap_stop(0, used, sizeof used);
        if (r == 0) { ap_s(out, max, &p, "captura: guardado en "); ap_s(out, max, &p, used); }
        else { ap_s(out, max, &p, "captura: "); ap_s(out, max, &p, r == -61 ? "no hay ninguna grabacion en marcha o es demasiado corta" : errtxt(r)); }
    } else if (seq(sub, "descartar")) { scrcap_discard(); ap_s(out, max, &p, "captura: grabacion descartada"); }
    else if (seq(sub, "fps") || seq(sub, "escala")) {
        tok(&q, a1, sizeof a1); uint32_t v = num(a1);
        if (rec_on) ap_s(out, max, &p, "captura: para la grabacion antes de cambiar ajustes");
        else if (seq(sub, "fps")) { if (v < 1 || v > 30) ap_s(out, max, &p, "uso: captura fps 1..30"); else { scrcap_set_options(v, opt_div); ap_s(out, max, &p, "captura: tramas por segundo cambiado"); } }
        else { if (v < 1 || v > 4) ap_s(out, max, &p, "uso: captura escala 1..4 (1 = tamano completo, 2 = mitad...)"); else { scrcap_set_options(opt_fps, v); ap_s(out, max, &p, "captura: escala cambiada"); } }
    } else if (seq(sub, "abrir")) {
        extern void gui_open_capture(void); gui_open_capture(); ap_s(out, max, &p, "Captura abierta.");
    } else ap_s(out, max, &p, "uso: captura estado|foto [ruta]|video [seg]|audio [seg]|av [seg]|iniciar video|audio|av|parar|descartar|fps N|escala N|abrir");
end:
    kprint(out); kprint("\n");
    return 1;
}
