/* PARCHE 046: ver xhci.h. Sondeo, un solo hilo de uso (el shell). Cada llamada reinicia el controlador y vuelve a enumerar. */
#include "xhci.h"
#include <pci.h>
#include <pmm.h>
#include <vmm.h>
#include <idt.h>
#include "thread.h"
#include "console.h"
#include "mouse.h"
#include "hid_proto.h"
#include "fs.h"
#include "msc_proto.h"
#include <xhci_arch.h>                                   /* PARCHE 062: partes dependientes de la arquitectura (x86_64 / aarch64) */
enum { E_NO_CTRL = -1, E_BAR = -2, E_MAP = -3, E_MEM = -4, E_HALT = -5, E_RESET = -6, E_PAGESZ = -7, E_SCRATCH = -8, E_RUN = -9,
       E_CMD = -10, E_TIMEOUT = -11, E_PORT = -12, E_XFER = -13, E_HANDOFF = -14 };

const char *xhci_err_str(int e)
{
    switch (e) {
    case 0: return "OK";
    case E_NO_CTRL: return "no hay controlador xHCI (en QEMU: SOMA_USB=1 python3 ejecutar.py)";
    case E_BAR: return "BAR0 invalido";
    case E_MAP: return "no se pudo mapear el MMIO";
    case E_MEM: return "sin memoria DMA";
    case E_HALT: return "el controlador no se detiene (USBSTS.HCH)";
    case E_RESET: return "el controlador no sale del reinicio (HCRST / CNR)";
    case E_PAGESZ: return "el controlador no admite paginas de 4 KiB";
    case E_SCRATCH: return "demasiados buffers de scratchpad";
    case E_RUN: return "el controlador no arranca (USBSTS.HCH sigue en 1)";
    case E_CMD: return "orden rechazada por el controlador";
    case E_TIMEOUT: return "tiempo agotado";
    case E_PORT: return "el puerto no se habilita tras el reinicio";
    case E_XFER: return "transferencia de control fallida";
    case E_HANDOFF: return "el BIOS no cede el controlador";
    default: return "error";
    }
}

static volatile uint8_t *cap, *op, *rt, *db;
static inline uint32_t rd32(volatile uint8_t *b, uint32_t off) { return *(volatile uint32_t *)(b + off); }
static inline void wr32(volatile uint8_t *b, uint32_t off, uint32_t v) { *(volatile uint32_t *)(b + off) = v; }
static inline void wr64(volatile uint8_t *b, uint32_t off, uint64_t v) { wr32(b, off, (uint32_t)v); wr32(b, off + 4, (uint32_t)(v >> 32)); }

/* Disposicion de la memoria DMA (paginas de 4 KiB, memoria identidad: virtual == fisica). */
#define PG_DCBAA   0
#define PG_SCRATCH 1
#define PG_CMD     2
#define PG_EVENT   3
#define PG_ERST    4
#define PG_INCTX   5
#define PG_DATA    6
#define PG_DEV0    7                              /* por dispositivo: contexto de salida (+0) y anillo de EP0 (+1) */
#define XHCI_MAX_HID 2
#define PG_HID0    (PG_DEV0 + 2 * XHCI_MAX_DEVS)          /* por dispositivo HID: anillo del extremo (+0) y buffer del informe (+1) */
#define DMA_FRAMES (PG_HID0 + 2 * XHCI_MAX_HID)
#define CMD_TRBS   64
#define EV_TRBS    256                       /* SOMA_178_T480S: la camara isocrona genera hasta 8000 eventos por segundo */
#define EP_TRBS    64
_Static_assert(CMD_TRBS * 16 <= 4096 && EV_TRBS * 16 <= 4096 && EP_TRBS * 16 <= 4096, "anillos mayores que una pagina");

static uint8_t *dma; static int g_mapped;
static int msc_ready;                                   /* PARCHE 049: almacenamiento masivo listo (se anula al reiniciar el controlador) */
#define MSC_MAX 4                                       /* SOMA_363_ACCESO: discos USB a la vez */
static int msc_n, msc_prim;                             /* SOMA_363_ACCESO: cuantos y cual es el del sistema */
static struct xhci_ring cmdr, ep0r[XHCI_MAX_DEVS];
static struct xhci_evring evr;
static uint32_t csz;

static inline uint8_t *pg(int n) { return dma + (uintptr_t)n * 4096; }
static inline uint64_t pgp(int n) { return XHCI_BUS(pg(n)); }
static void zero_pg(int n) { volatile uint8_t *p = pg(n); for (int i = 0; i < 4096; i++) p[i] = 0; }
static void copy_bytes(void *d, const void *s, uint32_t n) { for (uint32_t i = 0; i < n; i++) ((volatile uint8_t *)d)[i] = ((const uint8_t *)s)[i]; }

static int wait_bits(volatile uint8_t *b, uint32_t off, uint32_t mask, uint32_t want, uint32_t ms)
{
    uint64_t t0 = timer_ticks; unsigned long guard = 200000000UL;
    while ((rd32(b, off) & mask) != want) {
        if (timer_ticks - t0 > ms || !--guard) return -1;
        XHCI_RELAX();
    }
    return 0;
}

static void delay_ms(uint32_t ms)
{
    uint64_t t0 = timer_ticks; unsigned long guard = 200000000UL;
    while (timer_ticks - t0 < ms && --guard) XHCI_RELAX();
}

/* PARCHE 049: un unico consumidor del anillo de eventos (ev_pump) para el hilo usbd y para quien espera una orden/transferencia:
 * los eventos de los extremos HID se atienden aqui mismo y los demas van a una bandeja donde wait_event los recoge. */
#define MBOX_N 32
static struct xhci_trb mbox[MBOX_N];
static int mbox_n;
static int hid_event(const struct xhci_trb *t);                                      /* 1 = evento de un extremo HID, ya atendido */
static int cam_event(const struct xhci_trb *t);                                      /* SOMA_178_T480S: 1 = evento del extremo isocrono de la camara */
static void cam_stop_hook(void);


static void ev_pump(void)
{
    unsigned long fl = irq_save();
    struct xhci_trb t;
    while (xhci_ev_pop(&evr, &t)) {
        wr64(rt, 0x20 + 0x18, xhci_ev_deq_phys(&evr, pgp(PG_EVENT)) | (1u << 3));      /* ERDP + limpiar EHB */
        if (TRB_GET_TYPE(t.d3) == TRB_EV_TRANSFER && (hid_event(&t) || cam_event(&t))) continue;
        if (TRB_GET_TYPE(t.d3) == TRB_EV_PORT_CHG) continue;
        if (mbox_n == MBOX_N) { for (int i = 1; i < MBOX_N; i++) mbox[i - 1] = mbox[i]; mbox_n--; }
        mbox[mbox_n++] = t;
    }
    irq_restore(fl);
}

/* Espera un evento del tipo dado: los de orden se reconocen por el puntero de su TRB; los de transferencia por ranura y extremo (dci),
 * para recibir tambien los errores que se producen en un TRB intermedio (STALL, etc.). */
static int wait_event(uint32_t type, uint64_t ptr, int slot, int ep, struct xhci_trb *ev, uint32_t ms)
{
    uint64_t t0 = timer_ticks; unsigned long guard = 400000000UL;
    for (;;) {
        ev_pump();
        unsigned long fl = irq_save();
        for (int i = 0; i < mbox_n; i++) {
            const struct xhci_trb *t = &mbox[i];
            if (TRB_GET_TYPE(t->d3) != type) continue;
            int ok = type == TRB_EV_TRANSFER ? ((int)(t->d3 >> 24) == slot && (int)((t->d3 >> 16) & 0x1F) == ep)
                                              : ((((uint64_t)t->d1 << 32) | t->d0) == ptr);
            if (!ok) continue;
            *ev = *t;
            for (int j = i + 1; j < mbox_n; j++) mbox[j - 1] = mbox[j];
            mbox_n--;
            irq_restore(fl);
            return 0;
        }
        irq_restore(fl);
        if (timer_ticks - t0 > ms || !--guard) return -1;
        XHCI_RELAX();
    }
}

static volatile uint8_t last_cc;                                                   /* ultimo codigo de finalizacion de comando (diagnostico) */
static volatile int cmd_busy;                                                       /* un solo hilo a la vez en el anillo de comandos (usbd recupera extremos HID) */
static int ctrl_nodata(int d, uint8_t slot, uint8_t rtype, uint8_t req, uint16_t val, uint16_t idx);

static int command(uint32_t d0, uint32_t d1, uint32_t d3, struct xhci_trb *ev)
{
    unsigned long guard = 400000000UL;
    while (__sync_lock_test_and_set(&cmd_busy, 1)) { if (!--guard) return E_TIMEOUT; thread_yield(); }
    uint64_t ph = xhci_ring_push(&cmdr, pgp(PG_CMD), d0, d1, 0, d3);
    XHCI_MB();
    wr32(db, 0, 0);                                                                 /* timbre del anillo de comandos */
    int r = wait_event(TRB_EV_CMD_DONE, ph, 0, 0, ev, 1500) != 0 ? E_TIMEOUT : (((ev->d2 >> 24) == 1) ? 0 : E_CMD);
    last_cc = r == E_TIMEOUT ? 0 : (uint8_t)(ev->d2 >> 24);   /* codigo 1 = exito */
    __sync_lock_release(&cmd_busy);
    return r;
}

/* Transferencia de control IN sobre EP0 del dispositivo d (slot s): devuelve 0 y deja los datos en la pagina de datos. */
static int ctrl_in_ex(int d, uint8_t slot, uint8_t rtype, uint8_t req, uint16_t val, uint16_t idx, uint16_t len)
{
    uint64_t last = 0; struct xhci_trb ev;
    xhci_ctrl_in(&ep0r[d], pgp(PG_DEV0 + 2 * d + 1), rtype, req, val, idx, len, pgp(PG_DATA), &last);
    XHCI_MB();
    wr32(db, 4u * slot, 1);                                                         /* timbre del dispositivo, DCI 1 = EP0 */
    if (wait_event(TRB_EV_TRANSFER, last, slot, 1, &ev, 1500) != 0) return E_TIMEOUT;
    return ((ev.d2 >> 24) == 1) ? 0 : E_XFER;
}
static int ctrl_in(int d, uint8_t slot, uint16_t val, uint16_t len) { return ctrl_in_ex(d, slot, 0x80, 6, val, 0, len); }

static void zero_ctx(uint8_t *p, uint32_t n) { for (uint32_t i = 0; i < n; i++) p[i] = 0; }

struct enum_req { uint8_t speed, root_port, label, parent_slot, parent_port; uint32_t route; int parent_dev; };

static int dev_fail(struct usb_dev_info *dv, int r) { dv->err = r; return r; }

/* Enumera UN dispositivo (ya con puerto habilitado y velocidad conocida): ranura, direccion, descriptores. Reserva su indice en o->dev. */
static int enum_device(struct xhci_info *o, const struct enum_req *q)
{
    if (o->ndev >= XHCI_MAX_DEVS) return E_MEM;
    int d = o->ndev++;
    struct usb_dev_info *dv = &o->dev[d];
    zero_ctx((uint8_t *)dv, sizeof *dv);
    dv->port = q->label; dv->root_port = q->root_port; dv->route = q->route; dv->parent = q->parent_dev < 0 ? 0xFF : (uint8_t)q->parent_dev; dv->speed = q->speed;
    struct xhci_trb ev;
    int r = command(0, 0, TRB_TYPE(TRB_ENABLE_SLOT), &ev);
    if (r != 0) return dev_fail(dv, r);
    uint8_t slot = (uint8_t)(ev.d3 >> 24); dv->slot = slot;
    if (slot == 0 || slot > o->max_slots) return dev_fail(dv, E_CMD);
    zero_pg(PG_DEV0 + 2 * d); zero_pg(PG_DEV0 + 2 * d + 1);
    ((volatile uint64_t *)pg(PG_DCBAA))[slot] = pgp(PG_DEV0 + 2 * d);
    xhci_ring_init(&ep0r[d], (struct xhci_trb *)pg(PG_DEV0 + 2 * d + 1), EP_TRBS, pgp(PG_DEV0 + 2 * d + 1));
    uint16_t mps = dv->speed == 1 ? 64 : xhci_default_mps(dv->speed);              /* Full Speed: se empieza con 64 y se corrige tras leer 8 bytes */
    zero_ctx(pg(PG_INCTX), 4096);
    xhci_build_addr_input_ex(pg(PG_INCTX), csz == 64, dv->speed, q->route, q->root_port, mps, pgp(PG_DEV0 + 2 * d + 1), q->parent_slot, q->parent_port);
    r = command((uint32_t)pgp(PG_INCTX), (uint32_t)(pgp(PG_INCTX) >> 32), TRB_TYPE(TRB_ADDRESS_DEV) | ((uint32_t)slot << 24), &ev);
    if (r != 0) return dev_fail(dv, r);
    delay_ms(5);                                                                    /* tiempo de recuperacion de SET_ADDRESS */
    zero_ctx(pg(PG_DATA), 512);
    /* 8 primeros bytes del descriptor de dispositivo: bMaxPacketSize0 */
    if ((r = ctrl_in(d, slot, 0x0100, 8)) != 0) return dev_fail(dv, r);
    const volatile uint8_t *x = pg(PG_DATA);
    uint8_t real_mps = x[7];
    if (dv->speed != 4 && real_mps != 0 && real_mps != mps && (real_mps == 8 || real_mps == 16 || real_mps == 32 || real_mps == 64)) {
        /* Evaluate Context: corrige el tamano maximo de paquete de EP0 con el valor real del dispositivo */
        zero_ctx(pg(PG_INCTX), 4096);
        uint8_t *in = pg(PG_INCTX), *out = pg(PG_DEV0 + 2 * d);
        copy_bytes(in + csz, out, csz); copy_bytes(in + 2 * csz, out + csz, csz);
        ((uint32_t *)in)[1] = 3;                                                   /* A0 y A1 */
        uint32_t *ep = (uint32_t *)(in + 2 * csz);
        ep[1] = (ep[1] & 0x0000FFFFu) | ((uint32_t)real_mps << 16);
        r = command((uint32_t)pgp(PG_INCTX), (uint32_t)(pgp(PG_INCTX) >> 32), TRB_TYPE(13) | ((uint32_t)slot << 24), &ev);
        if (r != 0) return dev_fail(dv, r);
        mps = real_mps;
    } else if (real_mps) mps = real_mps;
    dv->mps0 = (uint8_t)(dv->speed == 4 ? 9 : mps);                                 /* en SuperSpeed bMaxPacketSize0 es un exponente (2^9 = 512) */
    zero_ctx(pg(PG_DATA), 512);
    if ((r = ctrl_in(d, slot, 0x0100, 18)) != 0) return dev_fail(dv, r);
    dv->bcd_usb = (uint16_t)(x[2] | x[3] << 8); dv->cls = x[4]; dv->sub = x[5]; dv->proto = x[6];
    dv->vid = (uint16_t)(x[8] | x[9] << 8); dv->pid = (uint16_t)(x[10] | x[11] << 8); dv->bcd_dev = (uint16_t)(x[12] | x[13] << 8);
    /* descriptor de configuracion 0: primero 9 bytes para conocer wTotalLength, luego todo (hasta 512 bytes) */
    zero_ctx(pg(PG_DATA), 1024);
    if ((r = ctrl_in(d, slot, 0x0200, 9)) == 0) {
        uint32_t total = (uint32_t)(x[2] | x[3] << 8);
        if (total < 9) total = 9;
        if (total > 512) total = 512;
        zero_ctx(pg(PG_DATA), 1024);
        if ((r = ctrl_in(d, slot, 0x0200, (uint16_t)total)) == 0) {
            static uint8_t cfg[512];
            copy_bytes(cfg, pg(PG_DATA), total);
            dv->n_if = (uint8_t)xhci_parse_config(cfg, total, dv->ifs, 4);
            dv->cfg_value = cfg[5];
        }
    }
    dv->is_hub = dv->cls == 9 ? 1 : 0;
    dv->err = r;
    return r;
}

/* Hub USB2: SET_CONFIGURATION, descriptor, campos de hub en la ranura, alimentacion de los puertos y enumeracion de lo que cuelga. */
static void hub_scan(struct xhci_info *o, int d, int tier)
{
    struct usb_dev_info *hv = &o->dev[d];
    uint8_t slot = hv->slot; struct xhci_trb ev; int r;
    hv->hub_cc = 0; hv->hub_err = 0;
    if (hv->bcd_usb >= 0x0300 || tier > 4) { hv->hub_note = 1; return; }            /* hubs SuperSpeed: aun no */
    if ((r = ctrl_nodata(d, slot, 0x00, 9, hv->cfg_value ? hv->cfg_value : 1, 0)) != 0) { hv->hub_note = 2; hv->hub_err = (uint8_t)(-r); return; }
    struct usb_hub_desc hd; uint8_t raw[9]; int got = -1;
    for (int tr = 0; tr < 2 && got != 0; tr++) {                                    /* descriptor de hub: 0x29 (USB2); si no, 0x2A */
        zero_ctx(pg(PG_DATA), 64);
        r = ctrl_in_ex(d, slot, 0xA0, 6, 0x2900, 0, 9);
        if (r != 0) { hv->hub_err = (uint8_t)(-r); continue; }
        copy_bytes(raw, pg(PG_DATA), 9);
        got = usb_parse_hub_desc(raw, 9, &hd);
    }
    if (got != 0) { hv->hub_note = 3; return; }
    zero_ctx(pg(PG_INCTX), 4096);
    xhci_build_hub_input(pg(PG_INCTX), csz == 64, pg(PG_DEV0 + 2 * d), hd.nports, hd.tt_think, 0);
    r = command((uint32_t)pgp(PG_INCTX), (uint32_t)(pgp(PG_INCTX) >> 32), TRB_TYPE(TRB_CONFIG_EP) | ((uint32_t)slot << 24), &ev);
    if (r != 0) {                                                                    /* algunos controladores prefieren Evaluate Context para actualizar solo la ranura */
        hv->hub_cc = last_cc;
        zero_ctx(pg(PG_INCTX), 4096);
        xhci_build_hub_input(pg(PG_INCTX), csz == 64, pg(PG_DEV0 + 2 * d), hd.nports, hd.tt_think, 0);
        r = command((uint32_t)pgp(PG_INCTX), (uint32_t)(pgp(PG_INCTX) >> 32), TRB_TYPE(TRB_EVAL_CTX) | ((uint32_t)slot << 24), &ev);
    }
    if (r != 0) { hv->hub_note = 4; hv->hub_err = (uint8_t)(-r); hv->hub_cc2 = last_cc; return; }
    hv->hub_ports = hd.nports;
    for (uint8_t p = 1; p <= hd.nports; p++) ctrl_nodata(d, slot, 0x23, 3, 8, p);   /* SET_FEATURE(PORT_POWER) */
    delay_ms((uint32_t)hd.power_on_ms_div2 * 2u + 100u);
    for (uint8_t p = 1; p <= hd.nports && o->ndev < XHCI_MAX_DEVS; p++) {
        struct usb_port_status ps; uint32_t st;
        if (ctrl_in_ex(d, slot, 0xA3, 0, 0, p, 4) != 0) continue;
        st = (uint32_t)(pg(PG_DATA)[0] | pg(PG_DATA)[1] << 8 | pg(PG_DATA)[2] << 16 | (uint32_t)pg(PG_DATA)[3] << 24);
        usb_decode_port_status(st, &ps);
        if (!ps.connected) continue;
        ctrl_nodata(d, slot, 0x23, 1, 16, p);                                       /* CLEAR_FEATURE(C_PORT_CONNECTION) */
        ctrl_nodata(d, slot, 0x23, 3, 4, p);                                        /* SET_FEATURE(PORT_RESET) */
        int done = 0;
        for (int w = 0; w < 30 && !done; w++) {
            delay_ms(20);
            if (ctrl_in_ex(d, slot, 0xA3, 0, 0, p, 4) != 0) break;
            st = (uint32_t)(pg(PG_DATA)[0] | pg(PG_DATA)[1] << 8 | pg(PG_DATA)[2] << 16 | (uint32_t)pg(PG_DATA)[3] << 24);
            usb_decode_port_status(st, &ps);
            if (ps.c_reset) done = 1;
        }
        ctrl_nodata(d, slot, 0x23, 1, 20, p);                                       /* CLEAR_FEATURE(C_PORT_RESET) */
        if (!done || !ps.enabled) continue;
        delay_ms(10);
        struct enum_req q;
        q.speed = ps.low ? 2 : (ps.high ? 3 : 1); q.root_port = hv->root_port; q.label = p; q.parent_dev = d;
        q.route = hv->route | ((uint32_t)p << (4 * (tier - 1)));
        q.parent_slot = (hv->speed == 3 && q.speed < 3) ? slot : 0; q.parent_port = p;   /* LS/FS tras un hub HS usan su traductor (TT) */
        int cd = o->ndev;
        if (enum_device(o, &q) == 0 && o->dev[cd].cls == 9) hub_scan(o, cd, tier + 1);
    }
}

static int enum_port(struct xhci_info *o, uint8_t pnum, uint8_t major)
{
    uint32_t poff = 0x400 + 0x10u * (pnum - 1u);
    uint32_t sc = rd32(op, poff);
    /* USB2: hay que reiniciar el puerto para habilitarlo. USB3: el enlace se habilita solo; si no lo esta, tambien se reinicia. */
    struct enum_req q; q.root_port = pnum; q.label = pnum; q.parent_slot = 0; q.parent_port = 0; q.route = 0; q.parent_dev = -1;
    int fail = 0;
    if (!((sc >> 1) & 1u) || major != 3) {
        wr32(op, poff, (sc & ~0x00FE0002u) | (1u << 4));                           /* PR=1 sin tocar los bits RW1C */
        if (wait_bits(op, poff, 1u << 21, 1u << 21, 500) != 0) fail = 1;           /* PRC */
        sc = rd32(op, poff);
        wr32(op, poff, (sc & ~0x00FE0002u) | (1u << 21) | (1u << 17));             /* limpia PRC y CSC (RW1C) */
        delay_ms(20);                                                               /* tiempo de recuperacion tras el reinicio */
        sc = rd32(op, poff);
    }
    if (fail || !(sc & 1u) || !((sc >> 1) & 1u)) {
        if (o->ndev < XHCI_MAX_DEVS) { struct usb_dev_info *dv = &o->dev[o->ndev++]; zero_ctx((uint8_t *)dv, sizeof *dv); dv->port = pnum; dv->root_port = pnum; dv->parent = 0xFF; dv->err = E_PORT; }
        return E_PORT;
    }
    q.speed = (uint8_t)((sc >> 10) & 0xF);
    int d = o->ndev;
    int r = enum_device(o, &q);
    if (r == 0 && o->dev[d].cls == 9) hub_scan(o, d, 1);
    return r;
}

/* Reclama el controlador al BIOS (USB Legacy Support) y busca los protocolos soportados por puerto (USB2/USB3). */
static int ext_caps(struct xhci_info *o, uint8_t *port_major, uint32_t hccp1)
{
    uint32_t off = ((hccp1 >> 16) & 0xFFFFu) * 4u; int guard = 64;
    while (off && off < 0x10000 - 16 && guard--) {
        uint32_t c = rd32(cap, off); uint32_t id = c & 0xFF;
        if (id == 1) {                                                              /* USB Legacy Support */
            if (c & (1u << 16)) {                                                   /* BIOS Owned */
                o->bios_owned = 1;
                wr32(cap, off, c | (1u << 24));                                      /* OS Owned */
                if (wait_bits(cap, off, 1u << 16, 0, 1500) != 0) return E_HANDOFF;
            }
            uint32_t s = rd32(cap, off + 4);                                        /* inhibe las SMI y limpia los estados pendientes */
            s &= ~((3u << 1) | (0xFFu << 5) | (7u << 17));
            s |= (7u << 29);
            wr32(cap, off + 4, s);
        } else if (id == 2) {                                                       /* Supported Protocol */
            uint8_t major = (uint8_t)(c >> 24);
            uint32_t po = rd32(cap, off + 8);
            uint32_t first = po & 0xFF, count = (po >> 8) & 0xFF;
            for (uint32_t k = 0; k < count && first + k <= 255; k++) port_major[first + k] = major;
        }
        uint32_t nx = (c >> 8) & 0xFF;
        if (!nx) break;
        off += nx * 4u;
    }
    return 0;
}

int xhci_probe(struct xhci_info *o)
{
    for (unsigned i = 0; i < sizeof *o; i++) ((uint8_t *)o)[i] = 0;
    (void)xhci_arch_init();                                                          /* aarch64: levanta el PCIe antes de buscar */
    for (int bus = 0; bus < XHCI_PCI_BUSES && !o->found; bus++)
        for (int slot = 0; slot < 32 && !o->found; slot++) {
            if (pci_vendor_id((uint8_t)bus, (uint8_t)slot, 0) == 0xFFFF) continue;
            int multi = (int)((pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C) >> 16) & 0x80);
            for (int fn = 0; fn < (multi ? 8 : 1); fn++) {
                if (pci_vendor_id((uint8_t)bus, (uint8_t)slot, (uint8_t)fn) == 0xFFFF) continue;
                if ((pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x08) >> 8) == 0x0C0330u) { o->found = 1; o->bus = (uint8_t)bus; o->slot = (uint8_t)slot; o->fn = (uint8_t)fn; break; }
            }
        }
    uint64_t bar = 0;
    int alt = 0;                                                                       /* PARCHE 067: controlador fuera de PCI (xHCI del RP1) */
    if (!o->found && xhci_arch_alt_bar(&bar) == 0) { alt = 1; o->found = 1; o->bus = 0xFF; }
    if (!o->found) return o->err = E_NO_CTRL;
    mbox_n = 0; msc_ready = 0; msc_n = 0;
    if (!alt) {
        uint32_t cmd = pci_config_read32(o->bus, o->slot, o->fn, 0x04);
        pci_config_write32(o->bus, o->slot, o->fn, 0x04, (cmd & 0xFFFFu) | 0x6u);    /* memoria + bus master */
        uint32_t b0 = pci_config_read32(o->bus, o->slot, o->fn, 0x10);
        if (b0 & 1u) return o->err = E_BAR;
        bar = b0 & 0xFFFFFFF0u;
        if (((b0 >> 1) & 3u) == 2u) bar |= (uint64_t)pci_config_read32(o->bus, o->slot, o->fn, 0x14) << 32;
        if (!XHCI_BAR_OK(bar)) return o->err = E_BAR;
        bar = XHCI_BAR_CPU(bar);                                                       /* direccion PCI -> direccion de CPU */
    }
    o->bar = bar;
    if (!g_mapped) {
        if (xhci_map_mmio(bar) != 0) return o->err = E_MAP;
        g_mapped = 1;
    }
    cap = (volatile uint8_t *)(uintptr_t)bar;
    uint32_t c0 = rd32(cap, 0x00), hcs1 = rd32(cap, 0x04), hcs2 = rd32(cap, 0x08), hccp1 = rd32(cap, 0x10);
    if (c0 == 0xFFFFFFFFu) return o->err = E_BAR;
    uint32_t caplen = c0 & 0xFF;
    o->hciversion = (uint16_t)(c0 >> 16); o->max_slots = (uint8_t)(hcs1 & 0xFF); o->max_ports = (uint8_t)(hcs1 >> 24);
    o->csz64 = (uint8_t)((hccp1 >> 2) & 1u); csz = o->csz64 ? 64 : 32;
    o->scratch = ((hcs2 >> 27) & 0x1Fu) | (((hcs2 >> 21) & 0x1Fu) << 5);
    uint32_t dboff = rd32(cap, 0x14) & ~3u, rtsoff = rd32(cap, 0x18) & ~0x1Fu;
    if (caplen < 0x20 || caplen > 0x100 || o->max_slots == 0 || o->max_ports == 0 || dboff + 4u * (o->max_slots + 1u) > 0x10000 || rtsoff + 0x40 > 0x10000
        || caplen + 0x400 + 0x10u * o->max_ports > 0x10000) return o->err = E_BAR;
    op = cap + caplen; rt = cap + rtsoff; db = cap + dboff;

    static uint8_t port_major[256];
    for (int i = 0; i < 256; i++) port_major[i] = 0;
    int r = ext_caps(o, port_major, hccp1);
    if (r != 0) return o->err = r;

    /* detener y reiniciar */
    if (!(rd32(op, 0x04) & 1u)) {                                                    /* HCH = 0: esta en marcha */
        wr32(op, 0x00, rd32(op, 0x00) & ~1u);
        if (wait_bits(op, 0x04, 1u, 1u, 500) != 0) return o->err = E_HALT;
    }
    wr32(op, 0x00, rd32(op, 0x00) | (1u << 1));                                      /* HCRST */
    if (wait_bits(op, 0x00, 1u << 1, 0, 1500) != 0 || wait_bits(op, 0x04, 1u << 11, 0, 1500) != 0) return o->err = E_RESET;   /* HCRST y CNR a 0 */
    if (!(rd32(op, 0x08) & 1u)) return o->err = E_PAGESZ;
    if (o->scratch > 512) return o->err = E_SCRATCH;

    if (!dma) {
        uintptr_t m = XHCI_DMA_ALLOC(DMA_FRAMES);
        if (!m) return o->err = E_MEM;
        dma = (uint8_t *)m;
    }
    for (int i = 0; i < DMA_FRAMES; i++) zero_pg(i);
    if (o->scratch) {
        static uintptr_t sp; static uint32_t sp_n;                                   /* los buffers se reservan una vez y se reutilizan */
        if (!sp) { sp = XHCI_DMA_ALLOC(o->scratch); sp_n = o->scratch; }
        if (!sp || sp_n < o->scratch) return o->err = E_MEM;
        for (uint32_t i = 0; i < o->scratch; i++) ((volatile uint64_t *)pg(PG_SCRATCH))[i] = XHCI_BUS(sp) + (uint64_t)i * 4096;
        ((volatile uint64_t *)pg(PG_DCBAA))[0] = pgp(PG_SCRATCH);
    }
    wr32(op, 0x38, o->max_slots);                                                    /* CONFIG: ranuras habilitadas */
    wr64(op, 0x30, pgp(PG_DCBAA));
    xhci_ring_init(&cmdr, (struct xhci_trb *)pg(PG_CMD), CMD_TRBS, pgp(PG_CMD));
    wr64(op, 0x18, pgp(PG_CMD) | 1u);                                                /* CRCR: RCS = 1 */
    xhci_ev_init(&evr, (struct xhci_trb *)pg(PG_EVENT), EV_TRBS);
    volatile uint32_t *erst = (volatile uint32_t *)pg(PG_ERST);
    erst[0] = (uint32_t)pgp(PG_EVENT); erst[1] = (uint32_t)(pgp(PG_EVENT) >> 32); erst[2] = EV_TRBS; erst[3] = 0;
    wr32(rt, 0x20 + 0x08, 1);                                                        /* ERSTSZ = 1 */
    wr64(rt, 0x20 + 0x18, pgp(PG_EVENT));                                            /* ERDP */
    wr64(rt, 0x20 + 0x10, pgp(PG_ERST));                                             /* ERSTBA (el ultimo: activa el anillo) */
    wr32(op, 0x00, rd32(op, 0x00) | 1u);                                             /* RS = 1 (sin interrupciones: sondeo) */
    if (wait_bits(op, 0x04, 1u, 0, 500) != 0) return o->err = E_RUN;
    o->ready = 1;
    delay_ms(250);                                                                   /* los puertos tardan en detectar dispositivos tras el reinicio */

    o->n_ports = o->max_ports;
    for (uint32_t p = 1; p <= o->max_ports; p++) {
        uint32_t sc = rd32(op, 0x400 + 0x10u * (p - 1u));
        if (sc & 1u) o->n_conn++;
        if (p <= XHCI_MAX_PORTS_INFO) {
            struct xhci_port_info *pi = &o->port[p - 1];
            pi->num = (uint8_t)p; pi->ccs = (uint8_t)(sc & 1u); pi->ped = (uint8_t)((sc >> 1) & 1u); pi->speed = (uint8_t)((sc >> 10) & 0xF);
            pi->major = port_major[p]; pi->portsc = sc;
        }
    }
    for (uint32_t p = 1; p <= o->max_ports && o->ndev < XHCI_MAX_DEVS; p++)
        if (rd32(op, 0x400 + 0x10u * (p - 1u)) & 1u) enum_port(o, (uint8_t)p, port_major[p]);
    return o->err = 0;
}


/* ---------------------------------------------------------------- PARCHE 047: HID (teclado / raton) ---------------------------------- */
struct hid {
    uint8_t used, kind, slot, dci, dead, devi, need_reset, resets, iface; uint16_t mps;                 /* kind: 1 teclado, 2 raton */
    struct xhci_ring ring; uint8_t prev[8]; uint8_t rep_u; uint64_t rep_t0, rep_t;
};
static struct hid hd[XHCI_MAX_HID];
static volatile int hid_run, hid_thread_alive;
static struct xhci_hid_status hst;

static int ctrl_nodata(int d, uint8_t slot, uint8_t rtype, uint8_t req, uint16_t val, uint16_t idx)
{
    uint64_t last = 0; struct xhci_trb ev;
    xhci_ctrl_nodata(&ep0r[d], pgp(PG_DEV0 + 2 * d + 1), rtype, req, val, idx, &last);
    XHCI_MB();
    wr32(db, 4u * slot, 1);
    if (wait_event(TRB_EV_TRANSFER, last, slot, 1, &ev, 1500) != 0) return E_TIMEOUT;
    return ((ev.d2 >> 24) == 1) ? 0 : E_XFER;
}

static void hid_queue(int i)
{
    struct hid *h = &hd[i];
    uint64_t buf = pgp(PG_HID0 + 2 * i + 1);
    xhci_ring_push(&h->ring, pgp(PG_HID0 + 2 * i), (uint32_t)buf, (uint32_t)(buf >> 32), h->mps, TRB_TYPE(TRB_NORMAL) | (1u << 5) | (1u << 2));   /* IOC + ISP */
    XHCI_MB();
    wr32(db, 4u * h->slot, h->dci);
}

static void hid_report(int i, const uint8_t *rep, uint32_t len)
{
    struct hid *h = &hd[i];
    hst.reports++;
    if (h->kind == 1) {
        if (len < 8) return;
        uint8_t out[64];
        int n = hid_kbd_diff(h->prev, rep, out, 64);
        for (int k = 0; k < n; k++) keyboard_inject_scancode(out[k]);
        hst.keys += (uint32_t)n;
        /* repeticion: el teclado USB no repite solo (el PS/2 si) */
        int still = 0; for (int k = 2; k < 8; k++) if (h->rep_u && rep[k] == h->rep_u) still = 1;
        if (!still) h->rep_u = 0;
        for (int k = 2; k < 8; k++) {
            uint8_t u = rep[k]; int isnew = u > 3;
            for (int j = 2; j < 8; j++) if (h->prev[j] == u) isnew = 0;
            if (isnew) { h->rep_u = u; h->rep_t0 = timer_ticks; h->rep_t = timer_ticks; }
        }
        for (int k = 0; k < 8; k++) h->prev[k] = rep[k];
    } else {
        struct hid_mouse m;
        if (!hid_mouse_parse(rep, len, &m)) return;
        mouse_inject(m.dx, -(int32_t)m.dy, (uint8_t)(m.buttons & 3u), m.wheel);
        hst.mouse_ev++;
    }
}

/* Atiende un evento de transferencia si es de un extremo HID activo (se llama desde ev_pump, con interrupciones desactivadas). */
static int hid_event(const struct xhci_trb *t)
{
    uint8_t slot = (uint8_t)(t->d3 >> 24), ep = (uint8_t)((t->d3 >> 16) & 0x1F), code = (uint8_t)(t->d2 >> 24);
    for (int i = 0; i < XHCI_MAX_HID; i++) {
        struct hid *h = &hd[i];
        if (!h->used || h->slot != slot || h->dci != ep) continue;
        if (h->dead) return 1;
        if (code == 1 || code == 13) {                                        /* exito / paquete corto */
            uint32_t resid = t->d2 & 0xFFFFFFu, len = resid > h->mps ? 0 : h->mps - resid;
            uint8_t rep[8] = {0};
            const volatile uint8_t *b = pg(PG_HID0 + 2 * i + 1);
            for (uint32_t k = 0; k < len && k < 8; k++) rep[k] = b[k];
            hid_report(i, rep, len < 8 ? len : 8);
            hid_queue(i);
        } else {                                                                  /* STALL/errores de transaccion: lo recupera el hilo usbd (aqui no se puede esperar) */
            hst.errors++;
            if (h->resets >= 8) { h->dead = 1; hst.dead++; } else h->need_reset = 1;
        }
        return 1;
    }
    return 0;
}

/* Reset Endpoint + Set TR Dequeue + CLEAR_FEATURE(ENDPOINT_HALT) y se vuelve a encolar el informe. */
static void hid_recover(int i)
{
    struct hid *h = &hd[i]; struct xhci_trb ev;
    h->need_reset = 0; h->resets++;
    int ok = command(0, 0, TRB_TYPE(TRB_RESET_EP) | ((uint32_t)h->dci << 16) | ((uint32_t)h->slot << 24), &ev) == 0;
    if (ok) {
        uint64_t deq = (pgp(PG_HID0 + 2 * i) + (uint64_t)h->ring.enq * 16u) | (h->ring.cycle & 1u);
        ok = command((uint32_t)deq, (uint32_t)(deq >> 32), TRB_TYPE(TRB_SET_TR_DEQ) | ((uint32_t)h->dci << 16) | ((uint32_t)h->slot << 24), &ev) == 0;
    }
    if (ok) {
        ctrl_nodata(h->devi, h->slot, 0x02, 1, 0, (uint16_t)(0x80 | (h->dci >> 1)));
        hid_queue(i);
        hst.recovered++;
    } else { h->dead = 1; hst.dead++; }
}

static void hid_poll(void)
{
    ev_pump();
    for (int i = 0; i < XHCI_MAX_HID; i++) if (hd[i].used && hd[i].need_reset && !hd[i].dead) hid_recover(i);
    for (int i = 0; i < XHCI_MAX_HID; i++) {                                      /* repeticion tras 500 ms, cada 35 ms */
        struct hid *h = &hd[i];
        if (!h->used || h->kind != 1 || !h->rep_u) continue;
        if (timer_ticks - h->rep_t0 > 500 && timer_ticks - h->rep_t >= 35) {
            uint8_t sc[2]; int n = hid_usage_to_set1(h->rep_u, sc);
            for (int k = 0; k < n; k++) keyboard_inject_scancode(sc[k]);
            h->rep_t = timer_ticks;
        }
    }
}

static void usbd_main(void *arg)
{
    (void)arg;
    hid_thread_alive = 1;
    while (hid_run) { hid_poll(); thread_sleep(2); }
    hid_thread_alive = 0;
    thread_exit();
}

int xhci_hid_active(void) { return hid_run; }

void xhci_hid_status(struct xhci_hid_status *st)
{
    *st = hst; st->active = hid_run; st->nkbd = st->nmouse = 0;
    for (int i = 0; i < XHCI_MAX_HID; i++) if (hd[i].used) { if (hd[i].kind == 1) st->nkbd++; else st->nmouse++; }
}

void xhci_hid_stop(void)
{
    if (!hid_run && !hid_thread_alive) return;
    hid_run = 0;
    for (int i = 0; i < 100 && hid_thread_alive; i++) thread_sleep(5);
    for (int i = 0; i < XHCI_MAX_HID; i++) hd[i].used = 0;
}

int xhci_hid_start(const struct xhci_info *o, struct xhci_hid_status *st)
{
    if (hid_run) { xhci_hid_status(st); return 0; }
    if (!o->ready) return E_NO_CTRL;
    for (unsigned i = 0; i < sizeof hd; i++) ((uint8_t *)hd)[i] = 0;
    for (unsigned i = 0; i < sizeof hst; i++) ((uint8_t *)&hst)[i] = 0;
    int nh = 0, nk = 0, nm = 0, last_err = 0;
    for (int d = 0; d < o->ndev && nh < XHCI_MAX_HID; d++) {
        const struct usb_dev_info *dv = &o->dev[d];
        if (dv->err) continue;
        for (int k = 0; k < dv->n_if && k < 4 && nh < XHCI_MAX_HID; k++) {
            const struct usb_if_info *f = &dv->ifs[k];
            if (f->cls != 3 || f->sub != 1 || (f->proto != 1 && f->proto != 2)) continue;
            if ((f->proto == 1 && nk) || (f->proto == 2 && nm)) continue;           /* un teclado y un raton bastan */
            const struct usb_ep_info *e = 0;
            for (int j = 0; j < f->n_ep; j++) if ((f->ep[j].attr & 3) == 3 && (f->ep[j].addr & 0x80)) { e = &f->ep[j]; break; }
            if (!e) continue;
            uint16_t mps = e->mps & 0x7FF;
            if (mps == 0 || mps > 64) continue;
            struct hid *h = &hd[nh];
            h->kind = f->proto; h->devi = (uint8_t)d; h->iface = f->num; h->slot = dv->slot; h->dci = (uint8_t)((e->addr & 0xF) * 2 + 1); h->mps = mps;
            zero_pg(PG_HID0 + 2 * nh); zero_pg(PG_HID0 + 2 * nh + 1);
            xhci_ring_init(&h->ring, (struct xhci_trb *)pg(PG_HID0 + 2 * nh), EP_TRBS, pgp(PG_HID0 + 2 * nh));
            zero_ctx(pg(PG_INCTX), 4096);
            xhci_build_cfgep_input(pg(PG_INCTX), csz == 64, pg(PG_DEV0 + 2 * d), h->dci, mps, xhci_ep_interval(dv->speed, e->interval), pgp(PG_HID0 + 2 * nh));
            struct xhci_trb ev;
            int r = command((uint32_t)pgp(PG_INCTX), (uint32_t)(pgp(PG_INCTX) >> 32), TRB_TYPE(TRB_CONFIG_EP) | ((uint32_t)dv->slot << 24), &ev);
            if (r != 0) { last_err = r; continue; }
            if ((r = ctrl_nodata(d, dv->slot, 0x00, 9, dv->cfg_value ? dv->cfg_value : 1, 0)) != 0) { last_err = r; continue; }
            ctrl_nodata(d, dv->slot, 0x21, 0x0B, 0, f->num);                       /* SET_PROTOCOL(boot): si falla, se sigue */
            ctrl_nodata(d, dv->slot, 0x21, 0x0A, 0, f->num);                       /* SET_IDLE(0) */
            h->used = 1;
            if (f->proto == 1) nk++; else nm++;
            nh++;
        }
    }
    if (!nh) { xhci_hid_status(st); return last_err ? last_err : E_NO_CTRL; }
    for (int i = 0; i < nh; i++) hid_queue(i);                                    /* los TRB se encolan al final: asi no se pierden eventos durante la configuracion */
    hid_run = 1;
    if (!thread_create("usbd", usbd_main, 0)) { hid_run = 0; xhci_hid_status(st); return E_MEM; }
    xhci_hid_status(st);
    return 0;
}

/* ---------------------------------------------------------------- PARCHE 052: arranque, diagnostico y hot-plug ---------------------------------- */
static struct xhci_info g_xi;
static volatile int rescan_busy, mon_run, mon_alive, poll_busy;
static int msc_inflight(void);                                                    /* SOMA_162 */
static int un_busy_flag(void);                                                    /* SOMA_171 */
static void un_stop_hook(void);
static void un_start_hook(const struct xhci_info *o);

const struct xhci_info *xhci_last(void) { return &g_xi; }

static void usb_print_devs(const struct xhci_info *o)
{
    for (int i = 0; i < o->ndev; i++) {
        const struct usb_dev_info *d = &o->dev[i];
        kprint("USB:   puerto "); kprint_dec(d->root_port ? d->root_port : d->port);
        for (int lv = 0; lv < 5; lv++) { unsigned nb = (d->route >> (4 * lv)) & 0xF; if (!nb) break; kprint("."); kprint_dec(nb); }
        if (d->err) { kprint(": FALLO - "); kprint(xhci_err_str(d->err)); kprint("\n"); continue; }
        kprint(": "); kprint_hex16(d->vid); kprint(":"); kprint_hex16(d->pid);
        kprint(" clase "); kprint_dec(d->cls);
        if (d->is_hub) { kprint(" HUB "); if (d->hub_ports) { kprint_dec(d->hub_ports); kprint(" puertos"); } else { kprint("(no configurable: paso "); kprint_dec(d->hub_note); kprint(", err "); kprint_dec(d->hub_err); kprint(", cc "); kprint_dec(d->hub_cc); kprint("/"); kprint_dec(d->hub_cc2); kprint(")"); } }
        for (int k = 0; k < d->n_if && k < 4; k++) { kprint(" if"); kprint_dec(d->ifs[k].num); kprint("="); kprint(usb_class_str(d->ifs[k].cls, d->ifs[k].sub, d->ifs[k].proto)); }
        kprint("\n");
    }
}

/* Explora el controlador, activa teclado/raton y disco y deja un resumen en la consola serie. Devuelve el resultado de xhci_probe. */
static int usb_bring_up(void)
{
    static struct xhci_hid_status hs; static struct xhci_msc_info mi;
    int r = xhci_probe(&g_xi);
    if (r == E_NO_CTRL) return r;                                                   /* sin controlador xHCI: nada que decir */
#ifdef XHCI_SECOND
    if (r != 0) return r;                                                           /* PARCHE 077: segundo controlador del RP1 ausente (emulador): en silencio */
    kprint("USB: segundo controlador del RP1 (usbhost1) activo\n");
#endif
    if (r != 0) { kprint("USB: xHCI no inicializado - "); kprint(xhci_err_str(r)); kprint("\n"); return r; }
    usb_print_devs(&g_xi);
    if (xhci_hid_start(&g_xi, &hs) != 0) kprint("USB: xHCI activo, sin teclado ni raton HID conectado\n");
    else { kprint("USB: teclado/raton HID activos (teclados "); kprint_dec((uint32_t)hs.nkbd); kprint(", ratones "); kprint_dec((uint32_t)hs.nmouse); kprint(")\n"); }
    int msr = xhci_msc_start(&g_xi, &mi);
    xhci_msc_report();                                                              /* SOMA_172B: que se probo y por que */
    if (msr == 0) {
        kprint("USB: almacenamiento masivo "); kprint(mi.vendor); kprint(" "); kprint(mi.product); kprint(" | ");
        kprint_dec((uint32_t)(mi.sectors / 2048)); kprint(" MiB (usa 'blk' para registrarlo como usb0)\n");
    }
    un_start_hook(&g_xi);                                                           /* SOMA_171_T480S: red por USB */
    return 0;
}

/* Firma de los puertos raiz con algo conectado (CCS): cambia al enchufar o quitar un dispositivo o un hub. */
static uint32_t port_sig(void)
{
    if (!g_xi.ready || !op) return 0;
    uint32_t h = 2166136261u;
    for (uint32_t p = 1; p <= g_xi.max_ports; p++) if (rd32(op, 0x400 + 0x10u * (p - 1u)) & 1u) h = (h ^ p) * 16777619u;
    return h;
}

int xhci_rescan(void)
{
    if (__sync_lock_test_and_set(&rescan_busy, 1)) return -1;
    for (int i = 0; i < 400 && poll_busy; i++) thread_sleep(5);                       /* deja terminar una lectura de estado de hub en curso */
    for (int i = 0; i < 2000 && msc_inflight(); i++) thread_sleep(5);                 /* SOMA_162: y una E/S del disco USB (el sistema de archivos) */
    for (int i = 0; i < 400 && un_busy_flag(); i++) thread_sleep(5);           /* SOMA_171: y un envio por la red USB */
    xhci_hid_stop();
    cam_stop_hook();                                                                  /* SOMA_178_T480S */
    msc_ready = 0; msc_n = 0;
    un_stop_hook();
    int r = usb_bring_up();
    __sync_lock_release(&rescan_busy);
    return r;
}

/* Firma de los puertos de los hubs con algo conectado (GET_STATUS de cada puerto). Devuelve 0 y la firma, o -1 si no se pudo leer (no cuenta como cambio). */
static int hub_sig(uint32_t *sig)
{
    uint32_t h = 2166136261u;
    for (int d = 0; d < g_xi.ndev; d++) {
        const struct usb_dev_info *hv = &g_xi.dev[d];
        if (!hv->is_hub || !hv->hub_ports || hv->err) continue;
        for (uint8_t p = 1; p <= hv->hub_ports; p++) {
            if (rescan_busy) return -1;
            if (ctrl_in_ex(d, hv->slot, 0xA3, 0, 0, p, 4) != 0) return -1;
            if (pg(PG_DATA)[0] & 1u) h = (h ^ (uint32_t)(d * 16 + p)) * 16777619u;
        }
    }
    *sig = h;
    return 0;
}

static void usbmon_main(void *arg)
{
    (void)arg;
    mon_alive = 1;
    uint32_t hsig = 0; { uint32_t t; poll_busy = 1; if (hub_sig(&t) == 0) hsig = t; poll_busy = 0; }
    uint32_t last = port_sig() ^ (hsig * 2654435761u), pend = last; uint64_t t = 0; int tick = 0;
    while (mon_run) {
        thread_sleep(250);
        if (rescan_busy) { hsig = 0; { uint32_t q; while (rescan_busy) thread_sleep(50); poll_busy = 1; if (hub_sig(&q) == 0) hsig = q; poll_busy = 0; } last = pend = port_sig() ^ (hsig * 2654435761u); continue; }
        uint32_t s = port_sig() ^ (hsig * 2654435761u);
        if ((++tick & 3) == 0) { uint32_t q; poll_busy = 1; if (hub_sig(&q) == 0) hsig = q; poll_busy = 0; s = port_sig() ^ (hsig * 2654435761u); }   /* los hubs se consultan cada ~1 s */
        if (s == last) { pend = s; continue; }
        if (s != pend) { pend = s; t = timer_ticks; continue; }                      /* esperar a que se estabilice (rebotes al enchufar) */
        if (timer_ticks - t < 500) continue;
        kprint("USB: cambio en los puertos, reexplorando\n");
        xhci_rescan();
        hsig = 0; { uint32_t q; poll_busy = 1; if (hub_sig(&q) == 0) hsig = q; poll_busy = 0; }
        last = pend = port_sig() ^ (hsig * 2654435761u);
    }
    mon_alive = 0;
    thread_exit();
}

static int autostart_done;                                                          /* SOMA_162_T480S: el disco USB puede pedirlo antes */
static void autostart_run(void);
/* SOMA_162_T480S: antes de que exista el sistema de archivos (no mira /etc/nousb: vfs_read lo inicializaria sin disco) */
void xhci_autostart_early(void) { if (autostart_done) return; autostart_done = 1; autostart_run(); }
void xhci_autostart(void)
{
    if (autostart_done) return;
    autostart_done = 1;
    char probe[8];
    if (vfs_read("/etc/nousb", probe, sizeof probe) > 0) { kprint("USB: arranque automatico omitido (/etc/nousb)\n"); return; }
    autostart_run();
}
static void autostart_run(void)
{
    rescan_busy = 1;
    int r = usb_bring_up();
    rescan_busy = 0;
    if (r == 0 && !mon_alive) { mon_run = 1; if (!thread_create("usbmon", usbmon_main, 0)) { mon_run = 0; kprint("USB: sin hilo de vigilancia (no habra hot-plug)\n"); } }
}

/* ---------------------------------------------------------------- PARCHE 049: almacenamiento masivo (Bulk-Only) ---------------------------------- */
#define MSC_PAGES 36                                    /* anillo IN, anillo OUT, CBW/CSW + 64 KiB de datos alineados a 64 KiB (un TRB no puede cruzar esa frontera) */
/* SOMA_363_ACCESO: varios discos USB a la vez (el pendrive del sistema y los que se conecten). Cada uno con sus anillos y su buffer;
 * las transferencias van de una en una (el anillo de eventos es comun): msc_lock, y mc apunta al dispositivo de la transferencia. */
struct msc_st {
    uint8_t slot, dev, in_dci, out_dci, in_addr, out_addr, iface; uint16_t mps;
    struct xhci_ring rin, rout; uint32_t tag; int ready;
    struct xhci_msc_info info;
    uint8_t *dma, *data;                                         /* data: buffer de datos de 64 KiB alineado a 64 KiB */
};
static struct msc_st ms[MSC_MAX];
static struct msc_st *mc = &ms[0];
static volatile int msc_lock;
static inline uint8_t *mp(int n) { return mc->dma + (uintptr_t)n * 4096; }
_Static_assert(MSC_PAGES * 4096 >= 3 * 4096 + 65536 + 65535, "buffer MSC insuficiente");
static inline uint64_t mpp(int n) { return XHCI_BUS(mp(n)); }
#define M_RIN 0
#define M_ROUT 1
#define M_CTL 2

int xhci_msc_ready(void) { return msc_ready && msc_prim < msc_n && ms[msc_prim].ready; }
static int msc_inflight(void) { return msc_lock; }
static volatile uint32_t msc_gen;                                     /* SOMA_162_T480S: sube cada vez que el disco USB se (re)inicia */
uint32_t xhci_msc_generation(void) { return msc_gen; }
int xhci_rescanning(void) { return rescan_busy; }
const struct xhci_msc_info *xhci_msc_get(void) { return &ms[msc_prim].info; }
int xhci_msc_count(void) { return msc_ready ? msc_n : 0; }
const struct xhci_msc_info *xhci_msc_get_n(int i) { return i >= 0 && i < msc_n ? &ms[i].info : 0; }
int xhci_msc_primario(void) { return msc_prim; }
void xhci_msc_pon_primario(int i) { if (i >= 0 && i < msc_n) { msc_prim = i; } }

static int msc_recover(int in)
{
    struct xhci_trb ev; struct xhci_ring *r = in ? &mc->rin : &mc->rout; uint8_t dci = in ? mc->in_dci : mc->out_dci;
    command(0, 0, TRB_TYPE(TRB_RESET_EP) | ((uint32_t)dci << 16) | ((uint32_t)mc->slot << 24), &ev);
    uint64_t deq = mpp(in ? M_RIN : M_ROUT) + (uint64_t)r->enq * 16u + (r->cycle ? 1u : 0u);       /* saltarse lo pendiente */
    command((uint32_t)deq, (uint32_t)(deq >> 32), TRB_TYPE(TRB_SET_TR_DEQ) | ((uint32_t)dci << 16) | ((uint32_t)mc->slot << 24), &ev);
    ctrl_nodata(mc->dev, mc->slot, 0x02, 1, 0, in ? mc->in_addr : mc->out_addr);                          /* CLEAR_FEATURE(ENDPOINT_HALT) */
    return 0;
}

/* Una etapa en un extremo: un solo TRB (el buffer no cruza ninguna frontera de 64 KiB), timbra y espera el evento. 0 = bien. */
static int msc_stage(int in, uint64_t phys, uint32_t len, int isp)
{
    struct xhci_ring *r = in ? &mc->rin : &mc->rout; uint8_t dci = in ? mc->in_dci : mc->out_dci;
    xhci_ring_push(r, mpp(in ? M_RIN : M_ROUT), (uint32_t)phys, (uint32_t)(phys >> 32), len, TRB_TYPE(TRB_NORMAL) | (isp ? (1u << 2) : 0u) | (1u << 5));   /* ISP + IOC */
    XHCI_MB();
    wr32(db, 4u * mc->slot, dci);
    struct xhci_trb ev;
    if (wait_event(TRB_EV_TRANSFER, 0, mc->slot, dci, &ev, 5000) != 0) return E_TIMEOUT;
    uint8_t code = (uint8_t)(ev.d2 >> 24);
    if (code == 1 || code == 13) return 0;
    msc_recover(in);                                                                                 /* STALL / error de transaccion: el extremo queda detenido */
    return E_XFER;
}

/* Orden SCSI completa (CBW, datos, CSW). 0 = transporte correcto y *status rellenado (0 bien, 1 fallo, 2 fase). */
static int msc_cmd(const uint8_t *cdb, uint8_t cdblen, int dir_in, uint32_t len, uint8_t *status)
{
    uint8_t *ctl = mp(M_CTL); mc->tag++;
    msc_cbw_build(ctl, mc->tag, len, dir_in, 0, cdb, cdblen);
    int r = msc_stage(0, mpp(M_CTL), MSC_CBW_LEN, 0);
    if (r != 0) return r;
    if (len) {
        r = msc_stage(dir_in, XHCI_BUS(mc->data), len, 1);
        if (r == E_TIMEOUT) return r;                                                                /* tras un STALL el CSW aun se puede leer */
    }
    int rc = r;
    for (int tries = 0; tries < 2; tries++) {
        for (int i = 0; i < MSC_CSW_LEN; i++) ((volatile uint8_t *)ctl)[64 + i] = 0;
        r = msc_stage(1, mpp(M_CTL) + 64, MSC_CSW_LEN, 1);
        if (r == 0) break;
        if (r == E_TIMEOUT) return r;
    }
    if (r != 0) return r;
    uint32_t residue = 0; uint8_t csw[MSC_CSW_LEN];
    for (int i = 0; i < MSC_CSW_LEN; i++) csw[i] = ((volatile uint8_t *)ctl)[64 + i];
    if (msc_csw_parse(csw, mc->tag, &residue, status) != 0) return E_XFER;
    if (*status == 0 && (rc != 0 || residue != 0)) *status = 1;                                       /* datos incompletos */
    return 0;
}

static int msc_scsi(const uint8_t *cdb, uint8_t cdblen, int dir_in, uint32_t len)
{
    uint8_t st = 1;
    int r = msc_cmd(cdb, cdblen, dir_in, len, &st);
    if (r != 0) return r;
    return st == 0 ? 0 : E_XFER;
}

static void msc_sense_clear(void)
{
    uint8_t cdb[16]; int l = msc_cdb_request_sense(cdb, 18);
    msc_scsi(cdb, (uint8_t)l, 1, 18);
}

/* SOMA_172B: se prueban TODOS los dispositivos de almacenamiento, en orden: el T480s tiene un lector de tarjetas interno
 * (0BDA:0316) que, vacio, falla; antes se probaba solo el primero y el pendrive quedaba sin usar. */
static int msc_start_one(const struct xhci_info *o, int want, struct xhci_msc_info *out);
struct msc_try_info { uint16_t vid, pid; int r; };
static struct msc_try_info msc_tries[8]; static int msc_ntries;
static int msc_dev_found = -1;
/* SOMA_363_ACCESO: se ponen en marcha TODOS los discos USB que respondan (hasta MSC_MAX); el primero es el del sistema hasta que
 * usbdisk encuentre en cual esta SOMA (xhci_msc_pon_primario). */
int xhci_msc_start(const struct xhci_info *o, struct xhci_msc_info *out)
{
    msc_ntries = 0; msc_n = 0; msc_prim = 0; msc_ready = 0;
    for (int k = 0; k < 8 && msc_n < MSC_MAX; k++) {
        mc = &ms[msc_n];
        int r = msc_start_one(o, k, out);
        if (r == E_NO_CTRL && msc_dev_found < 0) break;                 /* no hay mas candidatos */
        if (msc_ntries < 8) { msc_tries[msc_ntries].vid = o->dev[msc_dev_found].vid; msc_tries[msc_ntries].pid = o->dev[msc_dev_found].pid; msc_tries[msc_ntries].r = r; msc_ntries++; }
        if (r == 0) msc_n++;
    }
    mc = &ms[0];
    if (msc_n) { msc_ready = 1; msc_gen++; *out = ms[0].info; return 0; }
    return msc_ntries ? msc_tries[0].r : E_NO_CTRL;
}
void xhci_msc_report(void)
{
    for (int i = 0; i < msc_ntries; i++) {
        kprint("USB: almacenamiento "); kprint_hex16(msc_tries[i].vid); kprint(":"); kprint_hex16(msc_tries[i].pid);
        kprint(msc_tries[i].r == 0 ? ": en uso como disco\n" : msc_tries[i].r == E_TIMEOUT ? ": sin medio o no responde (lector de tarjetas vacio?)\n" : ": no sirve - ");
        if (msc_tries[i].r != 0 && msc_tries[i].r != E_TIMEOUT) { kprint(xhci_err_str(msc_tries[i].r)); kprint("\n"); }
    }
}
static int msc_start_one(const struct xhci_info *o, int want, struct xhci_msc_info *out)
{
    mc->ready = 0; msc_dev_found = -1;
    if (!o->ready) return E_NO_CTRL;
    if (!mc->dma) {
        uintptr_t x = XHCI_DMA_ALLOC(MSC_PAGES); if (!x) return E_MEM;
        mc->dma = (uint8_t *)x;
        mc->data = (uint8_t *)((x + 3 * 4096 + 0xFFFFu) & ~(uintptr_t)0xFFFFu);                          /* cabe: 3 paginas + 64 KiB + hasta 64 KiB de relleno < 36 paginas */
    }
    mc->info.sectors = 0;
    for (int i = 0; i < 3; i++) { volatile uint8_t *z = mp(i); for (int k = 0; k < 4096; k++) z[k] = 0; }
    { volatile uint8_t *z = mc->data; for (uint32_t k = 0; k < 65536; k++) z[k] = 0; }
    int dix = -1; const struct usb_dev_info *dv = 0; const struct usb_if_info *fi = 0; const struct usb_ep_info *ei = 0, *eo = 0;
    int seen = 0;
    for (int d = 0; d < o->ndev && dix < 0; d++) {
        if (o->dev[d].err) continue;
        for (int k = 0; k < o->dev[d].n_if && k < 4 && dix < 0; k++) {
            const struct usb_if_info *f = &o->dev[d].ifs[k];
            if (f->cls != 8 || f->sub != 6 || f->proto != 0x50) continue;                           /* SCSI transparente, Bulk-Only */
            const struct usb_ep_info *a = 0, *b = 0;
            for (int j = 0; j < f->n_ep; j++) if ((f->ep[j].attr & 3) == 2) { if (f->ep[j].addr & 0x80) a = &f->ep[j]; else b = &f->ep[j]; }
            if (a && b && seen++ == want) { dix = d; dv = &o->dev[d]; fi = f; ei = a; eo = b; }    /* SOMA_172B: el candidato numero 'want' */
        }
    }
    if (dix < 0) return E_NO_CTRL;
    msc_dev_found = dix;
    mc->slot = dv->slot; mc->dev = (uint8_t)dix; mc->iface = fi->num;
    mc->in_addr = ei->addr; mc->out_addr = eo->addr;
    mc->in_dci = (uint8_t)((ei->addr & 0xF) * 2 + 1); mc->out_dci = (uint8_t)((eo->addr & 0xF) * 2);
    mc->mps = ei->mps & 0x7FF; if (mc->mps == 0 || mc->mps > 1024) return E_XFER;
    xhci_ring_init(&mc->rin, (struct xhci_trb *)mp(M_RIN), EP_TRBS, mpp(M_RIN));
    xhci_ring_init(&mc->rout, (struct xhci_trb *)mp(M_ROUT), EP_TRBS, mpp(M_ROUT));
    zero_ctx(pg(PG_INCTX), 4096);
    xhci_build_cfg_bulk_input(pg(PG_INCTX), csz == 64, pg(PG_DEV0 + 2 * dix), mc->in_dci, mc->out_dci, mc->mps, mpp(M_RIN), mpp(M_ROUT));
    struct xhci_trb ev;
    int r = command((uint32_t)pgp(PG_INCTX), (uint32_t)(pgp(PG_INCTX) >> 32), TRB_TYPE(TRB_CONFIG_EP) | ((uint32_t)dv->slot << 24), &ev);
    if (r != 0) return r;
    if ((r = ctrl_nodata(dix, dv->slot, 0x00, 9, dv->cfg_value ? dv->cfg_value : 1, 0)) != 0) return r;
    /* INQUIRY, listo para usar, capacidad y sector 0 */
    uint8_t cdb[16]; int l = msc_cdb_inquiry(cdb, 36);
    for (int tries = 0; tries < 3; tries++) { if ((r = msc_scsi(cdb, (uint8_t)l, 1, 36)) == 0) break; }
    if (r != 0) return r;
    uint8_t inq[36]; for (int i = 0; i < 36; i++) inq[i] = ((volatile uint8_t *)mc->data)[i];
    uint8_t devtype; msc_parse_inquiry(inq, mc->info.vendor, mc->info.product, &devtype);
    if (devtype != 0) return E_XFER;                                                               /* solo discos (no CD-ROM, cintas...) */
    int ready = 0;
    for (int tries = 0; tries < 10 && !ready; tries++) {
        l = msc_cdb_tur(cdb);
        if (msc_scsi(cdb, (uint8_t)l, 1, 0) == 0) { ready = 1; break; }
        msc_sense_clear(); delay_ms(100);
    }
    if (!ready) return E_TIMEOUT;
    l = msc_cdb_read_capacity10(cdb);
    if ((r = msc_scsi(cdb, (uint8_t)l, 1, 8)) != 0) return r;
    uint8_t cap[8]; for (int i = 0; i < 8; i++) cap[i] = ((volatile uint8_t *)mc->data)[i];
    uint32_t blk = 0;
    if (msc_parse_capacity10(cap, &mc->info.sectors, &blk) != 0 || blk != 512) return E_XFER;        /* solo sectores de 512 bytes y < 2 TiB */
    l = msc_cdb_rw10(cdb, 0, 0, 1);
    if ((r = msc_scsi(cdb, (uint8_t)l, 1, 512)) != 0) return r;
    for (int i = 0; i < 16; i++) mc->info.sector0[i] = ((volatile uint8_t *)mc->data)[i];
    mc->ready = 1;
    *out = mc->info;
    return 0;
}

static int msc_toma(int i)
{
    if (!msc_ready || i < 0 || i >= msc_n || !ms[i].ready) return E_XFER;
    if (rescan_busy || __sync_lock_test_and_set(&msc_lock, 1)) return E_TIMEOUT;   /* SOMA_162: atomico; nada durante una reexploracion */
    mc = &ms[i];
    return 0;
}
static void msc_suelta(void) { __sync_lock_release(&msc_lock); }
int xhci_msc_read_n(int i, uint64_t lba, uint32_t n, void *buf)
{
    int r = msc_toma(i); if (r) return r;
    if (n == 0 || n > 128 || lba >= mc->info.sectors || n > mc->info.sectors - lba || lba > 0xFFFFFFFFu) { msc_suelta(); return E_XFER; }
    uint8_t cdb[16]; int l = msc_cdb_rw10(cdb, 0, (uint32_t)lba, (uint16_t)n);
    r = msc_scsi(cdb, (uint8_t)l, 1, n * 512u);
    if (r == E_XFER) { msc_sense_clear(); r = msc_scsi(cdb, (uint8_t)l, 1, n * 512u); }              /* un reintento tras limpiar el estado */
    if (r == 0) for (uint32_t k = 0; k < n * 512u; k++) ((uint8_t *)buf)[k] = ((volatile uint8_t *)mc->data)[k];
    msc_suelta();
    return r;
}
int xhci_msc_read(uint64_t lba, uint32_t n, void *buf) { return xhci_msc_read_n(msc_prim, lba, n, buf); }
static int msc_write(int i, uint64_t lba, uint32_t n, const void *buf, int sync)
{
    int r = msc_toma(i); if (r) return r;
    if (n == 0 || n > 128 || lba >= mc->info.sectors || n > mc->info.sectors - lba || lba > 0xFFFFFFFFu) { msc_suelta(); return E_XFER; }
    for (uint32_t k = 0; k < n * 512u; k++) ((volatile uint8_t *)mc->data)[k] = ((const uint8_t *)buf)[k];
    uint8_t cdb[16]; int l = msc_cdb_rw10(cdb, 1, (uint32_t)lba, (uint16_t)n);
    r = msc_scsi(cdb, (uint8_t)l, 0, n * 512u);
    if (r == E_XFER) { msc_sense_clear(); r = msc_scsi(cdb, (uint8_t)l, 0, n * 512u); }
    if (r == 0 && sync) { l = msc_cdb_sync10(cdb); msc_scsi(cdb, (uint8_t)l, 0, 0); }                 /* vaciar cache; si el dispositivo no la soporta, se ignora */
    msc_suelta();
    return r;
}
int xhci_msc_write(uint64_t lba, uint32_t n, const void *buf) { return msc_write(msc_prim, lba, n, buf, 1); }
/* SOMA_162_T480S: sin SYNCHRONIZE CACHE tras cada escritura (el sistema de archivos la pide en sus barreras con xhci_msc_sync) */
int xhci_msc_write_nosync(uint64_t lba, uint32_t n, const void *buf) { return msc_write(msc_prim, lba, n, buf, 0); }
int xhci_msc_write_n(int i, uint64_t lba, uint32_t n, const void *buf) { return msc_write(i, lba, n, buf, 0); }
int xhci_msc_sync_n(int i)
{
    int r = msc_toma(i); if (r) return r;
    uint8_t cdb[16]; int l = msc_cdb_sync10(cdb);
    r = msc_scsi(cdb, (uint8_t)l, 0, 0);
    if (r == E_XFER) { msc_sense_clear(); r = 0; }                                                   /* sin cache que vaciar: no es un error */
    msc_suelta();
    return r;
}
int xhci_msc_sync(void) { return xhci_msc_sync_n(msc_prim); }

#if defined(__x86_64__) && !defined(XHCI_SECOND)
/* ---------------------------------------------------------------- SOMA_171_T480S: red por USB (movil o adaptador) ---------------------------------
 * El movil compartiendo su conexion por cable (Android: RNDIS o NCM) o un adaptador USB-Ethernet estandar (CDC-ECM). Se busca la
 * funcion de red en todas las configuraciones del dispositivo, se configuran sus dos extremos bulk y se sondea como interfaz "usb0".
 * Recepcion: UN_NRX transferencias de 16 KiB siempre en cola; poll recoge sus finalizaciones de la bandeja de eventos sin esperar.
 * Envio: una transferencia por trama (RNDIS: con su cabecera; NCM: dentro de un NTB16), con un byte de relleno si la longitud es
 * multiplo del tamano de paquete (como usbnet de Linux, para que el dispositivo vea el final). Protocolos: src/core/usbnet_proto.c. */
#include "usbnet_proto.h"
#include "netif.h"
#include "bootfb.h"
#define UN_NRX   4
#define UN_BUF   16384
#define UN_PAGES (3 + 4 + 4 + UN_NRX * 4 + 4)               /* anillos, control, envio (16 KiB), recepcion y relleno para alinear */
static struct {
    volatile int ready, busy, link;
    int kind, dev; uint8_t slot, in_dci, out_dci, in_addr, out_addr, comm_if, data_if, cfg_value; uint16_t mps, vid, pid;
    struct xhci_ring rin, rout; uint8_t mac[6]; uint32_t rid; struct ncm_params np; uint16_t seq;
    uint8_t *mem, *ctl, *txb, *rxb[UN_NRX]; int rx_next;
    uint32_t tx, rx, txe, rxe, stalls; uint64_t txbytes, rxbytes;
    const char *why;
} un = { .why = "no hay ningun dispositivo de red USB" };
static uint8_t unq[24][1518]; static uint16_t unq_len[24]; static int unq_head, unq_n;
static inline uint64_t un_phys(const void *p) { return XHCI_BUS((const uint8_t *)p); }

static int un_ctrl(uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint16_t len)   /* datos en un.ctl */
{
    uint64_t last = 0; struct xhci_trb ev; struct xhci_ring *r = &ep0r[un.dev]; uint64_t base = pgp(PG_DEV0 + 2 * un.dev + 1);
    if (rt & 0x80) xhci_ctrl_in(r, base, rt, req, val, idx, len, un_phys(un.ctl), &last);
    else if (!len) xhci_ctrl_nodata(r, base, rt, req, val, idx, &last);
    else {
        uint32_t d0 = (uint32_t)rt | ((uint32_t)req << 8) | ((uint32_t)val << 16), d1 = (uint32_t)idx | ((uint32_t)len << 16);
        xhci_ring_push(r, base, d0, d1, 8, TRB_TYPE(TRB_SETUP) | (1u << 6) | (2u << 16));                    /* IDT, TRT = OUT */
        xhci_ring_push(r, base, (uint32_t)un_phys(un.ctl), (uint32_t)(un_phys(un.ctl) >> 32), len, TRB_TYPE(TRB_DATA));   /* DIR = OUT */
        last = xhci_ring_push(r, base, 0, 0, 0, TRB_TYPE(TRB_STATUS) | (1u << 5) | (1u << 16));               /* IOC, estado IN */
    }
    XHCI_MB();
    wr32(db, 4u * un.slot, 1);
    if (wait_event(TRB_EV_TRANSFER, last, un.slot, 1, &ev, 1500) != 0) return E_TIMEOUT;
    uint8_t cc = (uint8_t)(ev.d2 >> 24);
    if (cc == 1 || cc == 13) return 0;
    /* STALL en EP0: se reinicia el extremo y se salta lo pendiente */
    un.stalls++;
    command(0, 0, TRB_TYPE(TRB_RESET_EP) | (1u << 16) | ((uint32_t)un.slot << 24), &ev);
    uint64_t deq = base + (uint64_t)r->enq * 16u + (r->cycle ? 1u : 0u);
    command((uint32_t)deq, (uint32_t)(deq >> 32), TRB_TYPE(TRB_SET_TR_DEQ) | (1u << 16) | ((uint32_t)un.slot << 24), &ev);
    return E_XFER;
}
/* RNDIS: orden de control y su respuesta (se pregunta hasta que llega: no se usa el extremo de avisos) */
static int un_rndis(uint32_t len, uint32_t type, uint32_t rid, const uint8_t **info, uint32_t *ilen)
{
    if (un_ctrl(0x21, 0x00, 0, un.comm_if, (uint16_t)len) != 0) return -1;     /* SEND_ENCAPSULATED_COMMAND */
    for (int t = 0; t < 60; t++) {
        delay_ms(t < 5 ? 2 : 20);
        for (int i = 0; i < 64; i++) un.ctl[i] = 0;
        if (un_ctrl(0xA1, 0x01, 0, un.comm_if, 1025) != 0) continue;            /* GET_ENCAPSULATED_RESPONSE */
        uint32_t ml = (uint32_t)un.ctl[4] | (uint32_t)un.ctl[5] << 8 | (uint32_t)un.ctl[6] << 16 | (uint32_t)un.ctl[7] << 24;
        uint32_t ty = (uint32_t)un.ctl[0] | (uint32_t)un.ctl[1] << 8 | (uint32_t)un.ctl[2] << 16 | (uint32_t)un.ctl[3] << 24;
        if (!ml || ml > 1025) continue;
        if (ty == RNDIS_MSG_KEEPALIVE) {                                           /* el dispositivo pregunta si seguimos: contestar */
            uint32_t rr = (uint32_t)un.ctl[8] | (uint32_t)un.ctl[9] << 8 | (uint32_t)un.ctl[10] << 16 | (uint32_t)un.ctl[11] << 24;
            uint8_t save[64]; for (int i = 0; i < 64; i++) save[i] = un.ctl[i]; (void)save;
            uint32_t n = rndis_build_keepalive_cmplt(un.ctl, rr); un_ctrl(0x21, 0x00, 0, un.comm_if, (uint16_t)n);
            continue;
        }
        if (ty == RNDIS_MSG_INDICATE) continue;
        return rndis_parse_cmplt(un.ctl, ml, type, rid, info, ilen);
    }
    return -2;
}
static void un_queue_rx(int i)
{
    xhci_ring_push(&un.rin, un_phys(un.mem), (uint32_t)un_phys(un.rxb[i]), (uint32_t)(un_phys(un.rxb[i]) >> 32), UN_BUF,
                   TRB_TYPE(TRB_NORMAL) | (1u << 2) | (1u << 5));                 /* ISP + IOC */
}
static void un_q_frame(const uint8_t *f, uint32_t n, void *u)
{
    (void)u;
    if (n > 1518) return;
    if (unq_n == 24) { unq_head = (unq_head + 1) % 24; unq_n--; un.rxe++; }
    int k = (unq_head + unq_n) % 24;
    for (uint32_t i = 0; i < n; i++) unq[k][i] = f[i];
    unq_len[k] = (uint16_t)n; unq_n++;
}

static void un_stop(void) { un.ready = 0; un.link = 0; while (un.busy) thread_sleep(1); unq_n = 0; }

static int un_start(const struct xhci_info *o)
{
    un.ready = 0; un.link = 0; un.why = "no hay ningun dispositivo de red USB"; unq_n = 0; unq_head = 0;
    if (bootinfo_disabled("usbred")) { un.why = "desactivada (soma.usbred=off o modo seguro)"; return -1; }
    if (!un.mem) {
        uintptr_t x = XHCI_DMA_ALLOC(UN_PAGES); if (!x) { un.why = "sin memoria DMA"; return E_MEM; }
        un.mem = (uint8_t *)x;                                                     /* pagina 0: anillo IN, 1: anillo OUT, 2: control */
        un.ctl = un.mem + 2 * 4096;
        uintptr_t a = (x + 3 * 4096 + UN_BUF - 1) & ~(uintptr_t)(UN_BUF - 1);      /* buffers de 16 KiB alineados: nunca cruzan 64 KiB */
        un.txb = (uint8_t *)a;
        for (int i = 0; i < UN_NRX; i++) un.rxb[i] = (uint8_t *)(a + (uintptr_t)(i + 1) * UN_BUF);
    }
    const char *force = 0;
#ifdef SOMA_USBNET_FORCE
    force = SOMA_USBNET_FORCE;                                                     /* solo pruebas en QEMU */
#endif
    if (bootinfo_has_option("soma.usbnet=rndis")) force = "R"; else if (bootinfo_has_option("soma.usbnet=ecm")) force = "E"; else if (bootinfo_has_option("soma.usbnet=ncm")) force = "N";
    struct usbnet_fn fn; int found = -1;
    for (int d = 0; d < o->ndev && found < 0; d++) {
        const struct usb_dev_info *dv = &o->dev[d];
        if (dv->err || dv->is_hub || !dv->slot) continue;
        int maybe = 0;                                                             /* clases que pueden ser red (o compuestos) */
        for (int k = 0; k < dv->n_if; k++) { uint8_t c = dv->ifs[k].cls; if (c == 2 || c == 0x0A || c == 0xE0 || c == 0xEF || c == 0xFF) maybe = 1; }
        if (dv->cls == 2 || dv->cls == 0xEF || dv->cls == 0) maybe |= (dv->n_if == 0);
        if (!maybe) continue;
        un.dev = d; un.slot = dv->slot;
        if (un_ctrl(0x80, 6, 0x0100, 0, 18) != 0) continue;
        int ncfg = un.ctl[17]; if (ncfg < 1) ncfg = 1; if (ncfg > 4) ncfg = 4;
        for (int c = 0; c < ncfg && found < 0; c++) {
            if (un_ctrl(0x80, 6, (uint16_t)(0x0200 | c), 0, 9) != 0) continue;
            uint32_t tot = (uint32_t)(un.ctl[2] | un.ctl[3] << 8); if (tot < 9) continue; if (tot > 2048) tot = 2048;
            if (un_ctrl(0x80, 6, (uint16_t)(0x0200 | c), 0, (uint16_t)tot) != 0) continue;
            static uint8_t cfgb[2048]; for (uint32_t i = 0; i < tot; i++) cfgb[i] = un.ctl[i];
            if (!usbnet_find(cfgb, tot, &fn)) continue;
            if (force && ((force[0] == 'R') != (fn.kind == USBNET_RNDIS) || (force[0] == 'E') != (fn.kind == USBNET_ECM) || (force[0] == 'N') != (fn.kind == USBNET_NCM)) && ncfg > 1) continue;
            un.cfg_value = cfgb[5]; found = d;
        }
    }
    if (found < 0) return -1;
    const struct usb_dev_info *dv = &o->dev[found];
    un.kind = fn.kind; un.vid = dv->vid; un.pid = dv->pid;
    un.comm_if = fn.comm_if; un.data_if = fn.data_if; un.in_addr = fn.in_addr; un.out_addr = fn.out_addr;
    un.in_dci = (uint8_t)((fn.in_addr & 0xF) * 2 + 1); un.out_dci = (uint8_t)((fn.out_addr & 0xF) * 2);
    un.mps = fn.in_mps ? fn.in_mps : 512;
    un.why = "fallo al configurar el dispositivo";
    if (un_ctrl(0x00, 9, un.cfg_value, 0, 0) != 0) return E_XFER;                 /* SET_CONFIGURATION */
    xhci_ring_init(&un.rin, (struct xhci_trb *)un.mem, EP_TRBS, un_phys(un.mem));
    xhci_ring_init(&un.rout, (struct xhci_trb *)(un.mem + 4096), EP_TRBS, un_phys(un.mem + 4096));
    zero_ctx(pg(PG_INCTX), 4096);
    xhci_build_cfg_bulk_input(pg(PG_INCTX), csz == 64, pg(PG_DEV0 + 2 * found), un.in_dci, un.out_dci, un.mps, un_phys(un.mem), un_phys(un.mem + 4096));
    struct xhci_trb ev;
    if (command((uint32_t)pgp(PG_INCTX), (uint32_t)(pgp(PG_INCTX) >> 32), TRB_TYPE(TRB_CONFIG_EP) | ((uint32_t)un.slot << 24), &ev) != 0) { un.why = "el controlador no acepto los extremos"; return E_CMD; }
    un.link = 1;
    if (un.kind == USBNET_RNDIS) {
        const uint8_t *info; uint32_t il, mx = 0, al = 0;
        un.rid = 1; uint32_t n = rndis_build_init(un.ctl, un.rid, UN_BUF);
        if (un_rndis(n, RNDIS_MSG_INIT, un.rid, 0, 0) != 0) { un.why = "RNDIS: el dispositivo no contesto a INIT"; return E_XFER; }
        /* (los datos del INIT_CMPLT ya se validaron; tamano maximo: se recibe en buffers de 16 KiB) */
        (void)mx; (void)al;
        n = rndis_build_query(un.ctl, ++un.rid, RNDIS_OID_PERM_ADDR, 48);
        if (un_rndis(n, RNDIS_MSG_QUERY, un.rid, &info, &il) != 0 || il < 6) { un.why = "RNDIS: sin direccion MAC"; return E_XFER; }
        for (int i = 0; i < 6; i++) un.mac[i] = info[i];
        n = rndis_build_set32(un.ctl, ++un.rid, RNDIS_OID_FILTER, 0x0B);          /* propias, multidifusion y difusion */
        if (un_rndis(n, RNDIS_MSG_SET, un.rid, 0, 0) != 0) { un.why = "RNDIS: no acepto el filtro de paquetes"; return E_XFER; }
        n = rndis_build_query(un.ctl, ++un.rid, RNDIS_OID_MEDIA_STATUS, 4);
        if (un_rndis(n, RNDIS_MSG_QUERY, un.rid, &info, &il) == 0 && il >= 4 && info[0] == 1) un.link = 0;   /* 0 conectado, 1 no */
    } else {
        int got_mac = 0;
        if (fn.mac_str && un_ctrl(0x80, 6, (uint16_t)(0x0300 | fn.mac_str), 0x0409, 64) == 0) got_mac = usbnet_mac_from_string(un.ctl, 64, un.mac) == 0;
        if (!got_mac) { un.mac[0] = 0x02; un.mac[1] = 0x50; un.mac[2] = (uint8_t)(un.vid >> 8); un.mac[3] = (uint8_t)un.vid; un.mac[4] = (uint8_t)(un.pid >> 8); un.mac[5] = (uint8_t)un.pid; }
        if (un.kind == USBNET_NCM) {
            if (un_ctrl(0xA1, 0x80, 0, un.comm_if, 28) != 0 || ncm_parse_params(un.ctl, 28, &un.np) != 0) { un.why = "NCM: sin parametros NTB"; return E_XFER; }
            un.ctl[0] = (uint8_t)UN_BUF; un.ctl[1] = (uint8_t)(UN_BUF >> 8); un.ctl[2] = 0; un.ctl[3] = 0;
            un_ctrl(0x21, 0x86, 0, un.comm_if, 4);                                  /* SET_NTB_INPUT_SIZE: lo que caben nuestros buffers */
            if (un.np.out_max > UN_BUF) un.np.out_max = UN_BUF;
        }
        if (fn.data_alt && un_ctrl(0x01, 11, fn.data_alt, un.data_if, 0) != 0) { un.why = "no se pudo activar la interfaz de datos"; return E_XFER; }
        if (un.kind == USBNET_ECM) un_ctrl(0x21, 0x43, 0x0E, un.comm_if, 0);        /* SET_ETHERNET_PACKET_FILTER (si no lo soporta, da igual) */
    }
    for (int i = 0; i < UN_NRX; i++) un_queue_rx(i);
    un.rx_next = 0;
    XHCI_MB();
    wr32(db, 4u * un.slot, un.in_dci);
    un.why = "operativa"; un.ready = 1;
    kprint("USB: red "); kprint(usbnet_kind_str(un.kind)); kprint(" en "); kprint_hex16(un.vid); kprint(":"); kprint_hex16(un.pid);
    kprint(" (configuracion "); kprint_dec(un.cfg_value); kprint(") | MAC "); 
    for (int i = 0; i < 6; i++) { kprint_hex8(un.mac[i]); if (i < 5) kprint(":"); }
    kprint(un.link ? " | interfaz usb0 lista\n" : " | sin conexion en el dispositivo\n");
    return 0;
}

/* finalizacion pendiente de un extremo de la red, sin esperar */
static int un_take_event(uint8_t dci, struct xhci_trb *ev)
{
    ev_pump();
    unsigned long fl = irq_save();
    for (int i = 0; i < mbox_n; i++) {
        const struct xhci_trb *t = &mbox[i];
        if (TRB_GET_TYPE(t->d3) != TRB_EV_TRANSFER || (int)(t->d3 >> 24) != un.slot || (int)((t->d3 >> 16) & 0x1F) != dci) continue;
        *ev = *t;
        for (int j = i + 1; j < mbox_n; j++) mbox[j - 1] = mbox[j];
        mbox_n--;
        irq_restore(fl);
        return 1;
    }
    irq_restore(fl);
    return 0;
}
static void un_recover_in(void)
{
    struct xhci_trb ev;
    un.stalls++;
    command(0, 0, TRB_TYPE(TRB_RESET_EP) | ((uint32_t)un.in_dci << 16) | ((uint32_t)un.slot << 24), &ev);
    uint64_t deq = un_phys(un.mem) + (uint64_t)un.rin.enq * 16u + (un.rin.cycle ? 1u : 0u);
    command((uint32_t)deq, (uint32_t)(deq >> 32), TRB_TYPE(TRB_SET_TR_DEQ) | ((uint32_t)un.in_dci << 16) | ((uint32_t)un.slot << 24), &ev);
    ctrl_nodata(un.dev, un.slot, 0x02, 1, 0, un.in_addr);                         /* CLEAR_FEATURE(ENDPOINT_HALT) */
    struct xhci_trb x; while (un_take_event(un.in_dci, &x)) { }
    for (int i = 0; i < UN_NRX; i++) un_queue_rx(i);
    un.rx_next = 0;
    XHCI_MB(); wr32(db, 4u * un.slot, un.in_dci);
}

static int un_init(void) { return un.ready; }
static int un_link(void) { return un.ready && un.link && !rescan_busy; }
static const uint8_t *un_mac(void) { return un.mac; }
static int un_poll(void *out, uint16_t max, uint16_t *len_out)
{
    if (!un.ready || rescan_busy) return 0;
    if (!unq_n) {
        if (__sync_lock_test_and_set(&un.busy, 1)) return 0;
        struct xhci_trb ev;
        for (int k = 0; k < UN_NRX && un.ready && un_take_event(un.in_dci, &ev); k++) {
            uint8_t cc = (uint8_t)(ev.d2 >> 24);
            int i = un.rx_next;
            if (cc == 1 || cc == 13) {
                uint32_t got = UN_BUF - (ev.d2 & 0xFFFFFFu);
                const uint8_t *b = un.rxb[i];
                if (un.kind == USBNET_RNDIS) rndis_unwrap(b, got, un_q_frame, 0);
                else if (un.kind == USBNET_NCM) ncm_parse_ntb(b, got, un_q_frame, 0);
                else if (got >= 14) un_q_frame(b, got > 1518 ? 1518 : got, 0);
                un_queue_rx(i);
                un.rx_next = (i + 1) % UN_NRX;
                XHCI_MB(); wr32(db, 4u * un.slot, un.in_dci);
            } else { un.rxe++; un_recover_in(); break; }
        }
        __sync_lock_release(&un.busy);
    }
    if (!unq_n) return 0;
    uint16_t n = unq_len[unq_head]; if (n > max) n = max;
    for (uint16_t i = 0; i < n; i++) ((uint8_t *)out)[i] = unq[unq_head][i];
    unq_head = (unq_head + 1) % 24; unq_n--;
    *len_out = n; un.rx++; un.rxbytes += n;
    return 1;
}
static int un_send(const void *frame, uint16_t len)
{
    if (!un.ready || rescan_busy || len > 1514) return -1;
    for (int w = 0; w < 200 && __sync_lock_test_and_set(&un.busy, 1); w++) thread_sleep(1);
    if (!un.ready) { __sync_lock_release(&un.busy); return -1; }
    uint32_t n;
    uint8_t f[60]; const uint8_t *src = (const uint8_t *)frame;
    if (len < 60) { for (int i = 0; i < 60; i++) f[i] = i < len ? src[i] : 0; src = f; len = 60; }
    if (un.kind == USBNET_RNDIS) n = rndis_wrap(un.txb, src, len);
    else if (un.kind == USBNET_NCM) n = ncm_build_ntb(un.txb, UN_BUF - 1, &un.np, un.seq++, src, len);
    else { for (uint16_t i = 0; i < len; i++) un.txb[i] = src[i]; n = len; }
    if (n && n % un.mps == 0) {                                                    /* el final de la transferencia debe notarse */
        un.txb[n++] = 0;
        if (un.kind == USBNET_NCM) { un.txb[8] = (uint8_t)n; un.txb[9] = (uint8_t)(n >> 8); }
    }
    int r = -1;
    if (n) {
        xhci_ring_push(&un.rout, un_phys(un.mem + 4096), (uint32_t)un_phys(un.txb), (uint32_t)(un_phys(un.txb) >> 32), n, TRB_TYPE(TRB_NORMAL) | (1u << 5));
        XHCI_MB(); wr32(db, 4u * un.slot, un.out_dci);
        struct xhci_trb ev;
        if (wait_event(TRB_EV_TRANSFER, 0, un.slot, un.out_dci, &ev, 500) == 0 && ((ev.d2 >> 24) == 1 || (ev.d2 >> 24) == 13)) { r = 0; un.tx++; un.txbytes += len; }
        else {
            un.txe++;
            command(0, 0, TRB_TYPE(TRB_RESET_EP) | ((uint32_t)un.out_dci << 16) | ((uint32_t)un.slot << 24), &ev);
            uint64_t deq = un_phys(un.mem + 4096) + (uint64_t)un.rout.enq * 16u + (un.rout.cycle ? 1u : 0u);
            command((uint32_t)deq, (uint32_t)(deq >> 32), TRB_TYPE(TRB_SET_TR_DEQ) | ((uint32_t)un.out_dci << 16) | ((uint32_t)un.slot << 24), &ev);
            ctrl_nodata(un.dev, un.slot, 0x02, 1, 0, un.out_addr);
        }
    }
    __sync_lock_release(&un.busy);
    return r;
}
static void un_stats(uint32_t *t, uint32_t *r, uint64_t *tb, uint64_t *rb) { if (t) *t = un.tx; if (r) *r = un.rx; if (tb) *tb = un.txbytes; if (rb) *rb = un.rxbytes; }
const struct netif_ops netif_usbnet = { "usb0", "red por USB (movil o adaptador)", un_init, un_send, un_poll, un_mac, un_link, un_stats };

void usbnet_info(void)
{
    kprint("usb0 (red por USB): ");
    if (!un.ready) { kprint(un.why); kprint("\n  (en el movil: Ajustes > Conexiones > Compartir conexion > Anclaje de USB)\n"); return; }
    kprint(usbnet_kind_str(un.kind)); kprint(" en "); kprint_hex16(un.vid); kprint(":"); kprint_hex16(un.pid);
    kprint(un.link ? " | conectado" : " | el dispositivo dice que no tiene conexion");
    kprint("\n  enviadas "); kprint_dec(un.tx); kprint(" (errores "); kprint_dec(un.txe); kprint("), recibidas "); kprint_dec(un.rx);
    kprint(" (perdidas "); kprint_dec(un.rxe); kprint("), reinicios de extremos "); kprint_dec(un.stalls); kprint("\n");
}
static int un_busy_flag(void) { return un.busy; }
static void un_stop_hook(void) { un_stop(); }
static void un_start_hook(const struct xhci_info *o) { un_start(o); }
#else
static int un_busy_flag(void) { return 0; }
static void un_stop_hook(void) { }
static void un_start_hook(const struct xhci_info *o) { (void)o; }
#endif

#ifndef XHCI_SECOND                                                                  /* SOMA_178_T480S: camara UVC (el segundo controlador del RP1 no la lleva) */
#include "xhci_cam.inc"
#else
static int cam_event(const struct xhci_trb *t) { (void)t; return 0; }
static void cam_stop_hook(void) { }
#endif
