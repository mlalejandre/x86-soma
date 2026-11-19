/* SOMA_178_T480S: camara USB de clase video (UVC) por transferencias ISOCRONAS. Este archivo se incluye al final de xhci.c: usa sus
 * funciones y variables estaticas (command, wait_event, ep0r, db, csz, g_xi...). Un solo extremo isocrono IN de alta velocidad.
 *
 * Idea: un anillo de 255 TD de un TRB cada uno (un TD = una microtrama = hasta 3 x 1024 bytes), cada TD con su pagina de datos, SIA
 * (empezar en cuanto se pueda) e IOC. El controlador devuelve un evento por TD con los bytes que NO llego a escribir (residual); ev_pump
 * desvia esos eventos (cam_event) a una cola y el hilo "uvcd" entrega cada carga util al ensamblador de tramas y repone TD. */
#include "cam_hw.h"

#define CAM_RING_TRBS 256
#define CAM_NSLOTS    (CAM_RING_TRBS - 1)
#define CAM_PG_RING   0
#define CAM_PG_CTL    1
#define CAM_PG_INCTX  2
#define CAM_PG_SLOT0  3
#define CAM_PAGES     (CAM_PG_SLOT0 + CAM_NSLOTS)
#define CAM_TARGET    200                     /* TD en vuelo (25 ms de margen) */
#define CAM_CFG_MAX   4096
#define CAM_EVQ       1024
_Static_assert(CAM_RING_TRBS * 16 <= 4096, "anillo isocrono mayor que una pagina");
_Static_assert(CAM_EVQ && !(CAM_EVQ & (CAM_EVQ - 1)), "cola de eventos: potencia de 2");

struct cam_evq_e { uint16_t idx; uint8_t cc; uint8_t pad; uint32_t resid; };
static struct {
    int dev, slot, speed, probed, cfg_done, present; uint16_t vid, pid; uint8_t cfg_value;
    uint8_t cfg[CAM_CFG_MAX]; uint32_t cfg_len; struct uvc_cfg uc;
    volatile int armed, run, alive, streaming, busy;
    uint8_t dci, ep_addr, iface, alt, intv; uint16_t mps_raw; uint32_t pkt_bytes, packets;
    struct xhci_ring ring; cam_payload_fn cb;
    uint8_t td_state[CAM_NSLOTS]; uint32_t outstanding; uint64_t last_done_ms;
    volatile uint32_t qh, qt; struct cam_evq_e q[CAM_EVQ];
    struct cam_hw_stats st;
    uint32_t recoveries, ctl_stalls;
} cam = { .dev = -1 };
static uint8_t *cam_mem;

static inline uint8_t *cpg(int n) { return cam_mem + (uintptr_t)n * 4096; }
static inline uint64_t cpp(int n) { return XHCI_BUS(cpg(n)); }
static void cam_err(const char *m) { uint32_t i = 0; while (m[i] && i + 1 < sizeof cam.st.err) { cam.st.err[i] = m[i]; i++; } cam.st.err[i] = 0; }
static void cam_err_cc(const char *m, uint32_t cc)
{
    uint32_t i = 0; while (m[i] && i + 14 < sizeof cam.st.err) { cam.st.err[i] = m[i]; i++; }
    const char *t = " (codigo "; for (uint32_t k = 0; t[k]; k++) cam.st.err[i++] = t[k];
    char d[12]; int n = 0; if (!cc) d[n++] = '0'; while (cc) { d[n++] = (char)('0' + cc % 10); cc /= 10; }
    while (n) cam.st.err[i++] = d[--n];
    cam.st.err[i++] = ')'; cam.st.err[i] = 0;
}

/* ---- eventos: se llama desde ev_pump con interrupciones desactivadas ---- */
static int cam_event(const struct xhci_trb *t)
{
    if (!cam.armed) return 0;
    uint8_t slot = (uint8_t)(t->d3 >> 24), ep = (uint8_t)((t->d3 >> 16) & 0x1F), cc = (uint8_t)(t->d2 >> 24);
    if (slot != cam.slot || ep != cam.dci) return 0;
    uint64_t ptr = ((uint64_t)t->d1 << 32) | t->d0, base = cpp(CAM_PG_RING);
    if (cc == 14 || cc == 15 || ptr < base || ptr >= base + (uint64_t)(CAM_RING_TRBS - 1) * 16u) {     /* subdesbordamiento/desbordamiento del anillo: no completan ningun TD */
        if (cc == 14) cam.st.underrun++; else cam.st.ring_ctl++;
        if (cc < 32) cam.st.cc[cc]++;
        return 1;
    }
    if (cam.qh - cam.qt >= CAM_EVQ) { cam.st.evq_lost++; return 1; }
    struct cam_evq_e *e = &cam.q[cam.qh & (CAM_EVQ - 1)];
    e->idx = (uint16_t)((ptr - base) / 16u); e->cc = cc; e->resid = t->d2 & 0xFFFFFFu;
    __sync_synchronize();
    cam.qh++;
    return 1;
}

/* ---- transferencias de control por EP0 de la camara (con pagina propia, sin tocar PG_DATA) ---- */
static int cam_ctrl(uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, const uint8_t *out_data)
{
    if (!cam_mem || cam.dev < 0) return E_MEM;
    struct xhci_ring *r = &ep0r[cam.dev]; uint64_t base = pgp(PG_DEV0 + 2 * cam.dev + 1), last = 0, buf = cpp(CAM_PG_CTL); struct xhci_trb ev;
    if (rt & 0x80) xhci_ctrl_in(r, base, rt, req, val, idx, len, buf, &last);
    else if (!len) xhci_ctrl_nodata(r, base, rt, req, val, idx, &last);
    else {
        for (uint32_t i = 0; i < len; i++) ((volatile uint8_t *)cpg(CAM_PG_CTL))[i] = out_data[i];
        uint32_t d0 = (uint32_t)rt | ((uint32_t)req << 8) | ((uint32_t)val << 16), d1 = (uint32_t)idx | ((uint32_t)len << 16);
        xhci_ring_push(r, base, d0, d1, 8, TRB_TYPE(TRB_SETUP) | (1u << 6) | (2u << 16));
        xhci_ring_push(r, base, (uint32_t)buf, (uint32_t)(buf >> 32), len, TRB_TYPE(TRB_DATA));
        last = xhci_ring_push(r, base, 0, 0, 0, TRB_TYPE(TRB_STATUS) | (1u << 5) | (1u << 16));
    }
    XHCI_MB();
    wr32(db, 4u * (uint32_t)cam.slot, 1);
    if (wait_event(TRB_EV_TRANSFER, last, cam.slot, 1, &ev, 1500) != 0) return E_TIMEOUT;
    uint8_t cc = (uint8_t)(ev.d2 >> 24);
    if (cc == 1 || cc == 13) return 0;
    last_cc = cc; cam.ctl_stalls++;
    command(0, 0, TRB_TYPE(TRB_RESET_EP) | (1u << 16) | ((uint32_t)cam.slot << 24), &ev);                      /* STALL en EP0 */
    uint64_t deq = base + (uint64_t)r->enq * 16u + (r->cycle ? 1u : 0u);
    command((uint32_t)deq, (uint32_t)(deq >> 32), TRB_TYPE(TRB_SET_TR_DEQ) | (1u << 16) | ((uint32_t)cam.slot << 24), &ev);
    return E_XFER;
}

/* ---- localizar la camara y leer su descriptor de configuracion completo ---- */
int cam_hw_probe(struct cam_hw_info *o)
{
    for (unsigned i = 0; i < sizeof *o; i++) ((uint8_t *)o)[i] = 0;
    static const char *no_usb = "USB no iniciado (sin controlador xHCI)";
#if defined(__x86_64__)
    if (bootinfo_disabled("cam")) { const char *w = "desactivada (soma.cam=off o modo seguro)"; unsigned k = 0; while (w[k] && k + 1 < sizeof o->why) { o->why[k] = w[k]; k++; } o->why[k] = 0; return -1; }
#endif
    if (cam.streaming) goto fill;
    if (__sync_lock_test_and_set(&cam.busy, 1)) { o->why[0] = 'o'; o->why[1] = 'c'; o->why[2] = 0; return -1; }
    if (!cam.probed) {
        cam.present = 0;
        if (!g_xi.ready) { cam_err(no_usb); goto done; }
        if (!cam_mem) {
            uintptr_t m = XHCI_DMA_ALLOC(CAM_PAGES);
            if (!m) { cam_err("sin memoria DMA para la camara"); goto done; }
            cam_mem = (uint8_t *)m;
            for (uint32_t i = 0; i < (uint32_t)CAM_PAGES * 4096u; i++) ((volatile uint8_t *)cam_mem)[i] = 0;
        }
        int d = -1;
        for (int i = 0; i < g_xi.ndev && d < 0; i++) {
            const struct usb_dev_info *dv = &g_xi.dev[i];
            if (dv->err) continue;
            for (int k = 0; k < dv->n_if && k < 4; k++) if (dv->ifs[k].cls == 14) { d = i; break; }
        }
        if (d < 0) { cam_err("no hay ninguna camara USB (clase video) conectada"); goto done; }
        const struct usb_dev_info *dv = &g_xi.dev[d];
        cam.dev = d; cam.slot = dv->slot; cam.speed = dv->speed; cam.vid = dv->vid; cam.pid = dv->pid;
        int r = cam_ctrl(0x80, 6, 0x0200, 0, 9, 0);
        if (r != 0) { cam_err("no se pudo leer el descriptor de configuracion"); goto done; }
        uint32_t total = (uint32_t)cpg(CAM_PG_CTL)[2] | (uint32_t)cpg(CAM_PG_CTL)[3] << 8;
        if (total < 9) total = 9;
        if (total > CAM_CFG_MAX) total = CAM_CFG_MAX;
        for (uint32_t i = 0; i < total; i++) ((volatile uint8_t *)cpg(CAM_PG_CTL))[i] = 0;
        if ((r = cam_ctrl(0x80, 6, 0x0200, 0, (uint16_t)total, 0)) != 0) { cam_err("fallo al leer la configuracion completa"); goto done; }
        for (uint32_t i = 0; i < total; i++) cam.cfg[i] = ((volatile uint8_t *)cpg(CAM_PG_CTL))[i];
        cam.cfg_len = total; cam.cfg_value = cam.cfg[5] ? cam.cfg[5] : 1;
        if (uvc_parse(cam.cfg, total, &cam.uc) != 0) { cam_err("el dispositivo no tiene formatos de video UVC reconocibles"); goto done; }
        cam.present = 1; cam.probed = 1; cam.st.err[0] = 0;
    }
done:
    __sync_lock_release(&cam.busy);
fill:
    o->present = cam.present; o->vid = cam.vid; o->pid = cam.pid; o->speed = (uint8_t)cam.speed; o->cfg_len = cam.cfg_len; o->cfg = &cam.uc;
    if (!cam.present) { uint32_t i = 0; while (cam.st.err[i] && i + 1 < sizeof o->why) { o->why[i] = cam.st.err[i]; i++; } o->why[i] = 0; }
    return cam.present ? 0 : -1;
}

/* ---- anillo isocrono ---- */
static void cam_refill(void)
{
    int pushed = 0;
    while (cam.outstanding < CAM_TARGET) {
        uint32_t idx = cam.ring.enq;
        uint64_t buf = cpp(CAM_PG_SLOT0 + (int)idx);
        xhci_ring_push(&cam.ring, cpp(CAM_PG_RING), (uint32_t)buf, (uint32_t)(buf >> 32), cam.pkt_bytes, xhci_isoc_d3(cam.packets, 1, 1));
        cam.td_state[idx] = 1; cam.outstanding++; cam.st.td_queued++; pushed++;
    }
    if (pushed) { XHCI_MB(); wr32(db, 4u * (uint32_t)cam.slot, cam.dci); }
}

static int cam_drain(void)
{
    int n = 0;
    for (;;) {
        unsigned long fl = irq_save();
        if (cam.qt == cam.qh) { irq_restore(fl); break; }
        struct cam_evq_e e = cam.q[cam.qt & (CAM_EVQ - 1)];
        cam.qt++;
        irq_restore(fl);
        n++;
        if (e.idx >= CAM_NSLOTS || !cam.td_state[e.idx]) continue;                 /* evento repetido o de un TD que ya no esta */
        cam.td_state[e.idx] = 0; if (cam.outstanding) cam.outstanding--;
        cam.st.td_done++; cam.last_done_ms = timer_ticks;
        if (e.cc < 32) cam.st.cc[e.cc]++;
        if (e.cc == 1 || e.cc == 13) {
            uint32_t len = e.resid > cam.pkt_bytes ? 0u : cam.pkt_bytes - e.resid;
            if (e.cc == 13) cam.st.ev_short++;
            if (!len) cam.st.ev_zero++;
            else { cam.st.ev_ok++; cam.st.bytes += len; if (cam.cb) cam.cb((const uint8_t *)cpg(CAM_PG_SLOT0 + e.idx), len); }
        } else cam.st.ev_err++;
    }
    return n;
}

/* Sin eventos durante mucho tiempo con TD pendientes: se para el extremo, se reinicia el anillo y se vuelve a empezar. */
static void cam_recover(void)
{
    struct xhci_trb ev;
    cam.recoveries++;
    command(0, 0, TRB_TYPE(TRB_STOP_EP) | ((uint32_t)cam.dci << 16) | ((uint32_t)cam.slot << 24), &ev);
    unsigned long fl = irq_save(); cam.qt = cam.qh; irq_restore(fl);
    xhci_ring_init(&cam.ring, (struct xhci_trb *)cpg(CAM_PG_RING), CAM_RING_TRBS, cpp(CAM_PG_RING));
    for (int i = 0; i < CAM_NSLOTS; i++) cam.td_state[i] = 0;
    cam.outstanding = 0;
    uint64_t deq = cpp(CAM_PG_RING) | 1u;
    command((uint32_t)deq, (uint32_t)(deq >> 32), TRB_TYPE(TRB_SET_TR_DEQ) | ((uint32_t)cam.dci << 16) | ((uint32_t)cam.slot << 24), &ev);
    cam.last_done_ms = timer_ticks;
}

static void cam_main(void *arg)
{
    (void)arg;
    cam.alive = 1;
    cam.last_done_ms = timer_ticks;
    while (cam.run) {
        ev_pump();
        int n = cam_drain();
        cam_refill();
        if (cam.outstanding && timer_ticks - cam.last_done_ms > 150) cam_recover();
        if (!n) thread_sleep(1);
    }
    cam.alive = 0;
    thread_exit();
}

int cam_hw_running(void) { return cam.streaming && cam.run && cam.alive; }

void cam_hw_stats(struct cam_hw_stats *s)
{
    *s = cam.st; s->streaming = cam_hw_running();
    s->dci = cam.dci; s->ep_addr = cam.ep_addr; s->alt = cam.alt; s->pkt_bytes = cam.pkt_bytes;
}

static void cam_teardown(int send_cmds)
{
    cam.run = 0;
    for (int i = 0; i < 400 && cam.alive; i++) thread_sleep(5);
    if (send_cmds && cam.streaming) {
        struct xhci_trb ev;
        cam_ctrl(0x01, 11, 0, cam.iface, 0, 0);                                                      /* SET_INTERFACE(0): la camara deja de enviar */
        command(0, 0, TRB_TYPE(TRB_STOP_EP) | ((uint32_t)cam.dci << 16) | ((uint32_t)cam.slot << 24), &ev);
        xhci_build_drop_ep_input(cpg(CAM_PG_INCTX), csz == 64, pg(PG_DEV0 + 2 * cam.dev), cam.dci);
        command((uint32_t)cpp(CAM_PG_INCTX), (uint32_t)(cpp(CAM_PG_INCTX) >> 32), TRB_TYPE(TRB_CONFIG_EP) | ((uint32_t)cam.slot << 24), &ev);
    }
    cam.armed = 0; cam.streaming = 0; cam.cb = 0;
}

void cam_hw_stop(void)
{
    if (!cam.streaming && !cam.alive) return;
    while (__sync_lock_test_and_set(&cam.busy, 1)) thread_sleep(1);
    cam_teardown(1);
    __sync_lock_release(&cam.busy);
}

static void cam_stop_hook(void)            /* el controlador se va a reiniciar: no se le manda nada, solo se olvida todo */
{
    cam_teardown(0);
    cam.probed = 0; cam.cfg_done = 0; cam.present = 0; cam.dev = -1;
}

int cam_hw_start(const struct uvc_sel *sel, cam_payload_fn cb)
{
    struct cam_hw_info inf;
    if (cam.streaming) cam_hw_stop();
    for (unsigned i = 0; i < sizeof cam.st; i++) ((uint8_t *)&cam.st)[i] = 0;
    cam.recoveries = 0; cam.ctl_stalls = 0;
    if (cam_hw_probe(&inf) != 0) { cam_err(inf.why[0] ? inf.why : "camara no disponible"); return -1; }
    if (__sync_lock_test_and_set(&cam.busy, 1)) { cam_err("camara ocupada"); return -2; }
    int rc = -3;
    if (cam.speed != 3) { cam_err("la camara no esta a alta velocidad (USB 2.0 HS): no soportado"); goto out; }
    const struct uvc_stream *vs = &cam.uc.vs[sel->vs]; const struct uvc_format *fm = &vs->fmt[sel->fmt]; const struct uvc_frame *fr = &fm->frame[sel->frm];
    struct xhci_trb ev; int r;
    if (!cam.cfg_done) {
        if ((r = cam_ctrl(0x00, 9, cam.cfg_value, 0, 0, 0)) != 0) { cam_err_cc("SET_CONFIGURATION fallo", last_cc); goto out; }
        cam.cfg_done = 1;
    }
    /* negociacion probe/commit */
    uint32_t plen = cam.uc.bcd_uvc >= 0x0150 ? 48u : cam.uc.bcd_uvc >= 0x0110 ? 34u : 26u;
    uint8_t pb[48]; int ok = 0;
    for (int att = 0; att < 3 && !ok; att++) {
        uvc_probe_fill(pb, plen, fm->index, fr->index, sel->interval);
        r = cam_ctrl(0x21, 0x01, 0x0100, vs->iface, (uint16_t)plen, pb);                              /* SET_CUR(VS_PROBE) */
        if (r == 0) {
            for (uint32_t i = 0; i < plen; i++) ((volatile uint8_t *)cpg(CAM_PG_CTL))[i] = 0;
            r = cam_ctrl(0xA1, 0x81, 0x0100, vs->iface, (uint16_t)plen, 0);                           /* GET_CUR(VS_PROBE) */
            if (r == 0) { for (uint32_t i = 0; i < plen; i++) pb[i] = ((volatile uint8_t *)cpg(CAM_PG_CTL))[i]; ok = 1; break; }
        }
        if (plen > 26) plen = 26;
    }
    if (!ok) { cam_err_cc("la negociacion probe fallo", last_cc); goto out; }
    for (uint32_t i = 0; i < plen; i++) cam.st.probe[i] = pb[i];
    if (cam_ctrl(0x21, 0x01, 0x0200, vs->iface, (uint16_t)plen, pb) != 0) { cam_err_cc("SET_CUR(commit) fallo", last_cc); goto out; }
    cam.st.probe_len = plen; cam.st.fmt_index = pb[2]; cam.st.frm_index = pb[3];
    cam.st.interval = (uint32_t)pb[4] | (uint32_t)pb[5] << 8 | (uint32_t)pb[6] << 16 | (uint32_t)pb[7] << 24;
    cam.st.max_payload = uvc_probe_max_payload(pb, plen); cam.st.max_frame = uvc_probe_max_frame(pb, plen);
    int ai = uvc_pick_alt(vs, cam.st.max_payload);
    if (ai < 0) { cam_err("la interfaz de video no tiene ajustes alternos isocronos"); goto out; }
    const struct uvc_alt *al = &vs->alt[ai];
    cam.iface = vs->iface; cam.alt = al->alt; cam.ep_addr = al->ep_addr; cam.mps_raw = al->mps_raw; cam.intv = al->interval ? al->interval : 1;
    cam.dci = (uint8_t)((al->ep_addr & 0xF) * 2 + 1);
    cam.pkt_bytes = al->bytes; cam.packets = 1u + ((al->mps_raw >> 11) & 3u);
    cam.cb = cb; cam.qh = cam.qt = 0; cam.outstanding = 0;
    for (int i = 0; i < CAM_NSLOTS; i++) cam.td_state[i] = 0;
    xhci_ring_init(&cam.ring, (struct xhci_trb *)cpg(CAM_PG_RING), CAM_RING_TRBS, cpp(CAM_PG_RING));
    xhci_build_cfg_isoc_input(cpg(CAM_PG_INCTX), csz == 64, pg(PG_DEV0 + 2 * cam.dev), cam.dci, cam.mps_raw, xhci_ep_interval(3, cam.intv), cpp(CAM_PG_RING));
    r = command((uint32_t)cpp(CAM_PG_INCTX), (uint32_t)(cpp(CAM_PG_INCTX) >> 32), TRB_TYPE(TRB_CONFIG_EP) | ((uint32_t)cam.slot << 24), &ev);
    if (r != 0) { cam_err_cc(r == E_TIMEOUT ? "Configure Endpoint: tiempo agotado" : "Configure Endpoint rechazado (8 = ancho de banda)", last_cc); goto out; }
    if ((r = cam_ctrl(0x01, 11, cam.alt, cam.iface, 0, 0)) != 0) { cam_err_cc("SET_INTERFACE fallo", last_cc); goto drop; }
    cam.streaming = 1; cam.armed = 1; cam.run = 1;
    cam_refill();
    if (!thread_create("uvcd", cam_main, 0)) { cam_err("no se pudo crear el hilo de captura"); cam_teardown(1); goto out; }
    cam.st.err[0] = 0;
    rc = 0;
    goto out;
drop:
    xhci_build_drop_ep_input(cpg(CAM_PG_INCTX), csz == 64, pg(PG_DEV0 + 2 * cam.dev), cam.dci);
    command((uint32_t)cpp(CAM_PG_INCTX), (uint32_t)(cpp(CAM_PG_INCTX) >> 32), TRB_TYPE(TRB_CONFIG_EP) | ((uint32_t)cam.slot << 24), &ev);
out:
    __sync_lock_release(&cam.busy);
    return rc;
}
