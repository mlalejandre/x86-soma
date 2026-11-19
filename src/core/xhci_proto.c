/* PARCHE 046: ver xhci_proto.h */
#include "xhci_proto.h"

static void zero(void *p, uint32_t n) { uint8_t *b = p; for (uint32_t i = 0; i < n; i++) b[i] = 0; }

void xhci_ring_init(struct xhci_ring *r, struct xhci_trb *mem, uint32_t size, uint64_t self_phys)
{
    zero(mem, size * (uint32_t)sizeof *mem);
    r->trb = mem; r->size = size; r->enq = 0; r->cycle = 1;
    struct xhci_trb *l = &mem[size - 1];                           /* enlace al principio con Toggle Cycle */
    l->d0 = (uint32_t)self_phys; l->d1 = (uint32_t)(self_phys >> 32); l->d2 = 0;
    l->d3 = TRB_TYPE(TRB_LINK) | (1u << 1);                         /* TC; el bit de ciclo se fija al pasar */
}

uint64_t xhci_ring_push(struct xhci_ring *r, uint64_t base_phys, uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3)
{
    struct xhci_trb *t = &r->trb[r->enq];
    t->d0 = d0; t->d1 = d1; t->d2 = d2;
    __sync_synchronize();
    t->d3 = (d3 & ~1u) | r->cycle;                                  /* el bit de ciclo se escribe el ultimo */
    uint64_t phys = base_phys + (uint64_t)r->enq * sizeof *t;
    r->enq++;
    if (r->enq == r->size - 1) {                                    /* llegamos al enlace: se entrega con el ciclo actual y se da la vuelta */
        struct xhci_trb *l = &r->trb[r->enq];
        __sync_synchronize();
        l->d3 = (l->d3 & ~1u) | r->cycle;
        r->enq = 0; r->cycle ^= 1u;
    }
    return phys;
}

void xhci_ev_init(struct xhci_evring *e, struct xhci_trb *mem, uint32_t size)
{
    zero(mem, size * (uint32_t)sizeof *mem);
    e->trb = mem; e->size = size; e->deq = 0; e->cycle = 1;
}

int xhci_ev_pop(struct xhci_evring *e, struct xhci_trb *out)
{
    volatile struct xhci_trb *t = &e->trb[e->deq];
    if ((t->d3 & 1u) != e->cycle) return 0;
    __sync_synchronize();
    out->d0 = t->d0; out->d1 = t->d1; out->d2 = t->d2; out->d3 = t->d3;
    if (++e->deq == e->size) { e->deq = 0; e->cycle ^= 1u; }
    return 1;
}

uint64_t xhci_ev_deq_phys(const struct xhci_evring *e, uint64_t base_phys) { return base_phys + (uint64_t)e->deq * sizeof(struct xhci_trb); }

void xhci_ctrl_in(struct xhci_ring *r, uint64_t base_phys, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, uint64_t buf_phys, uint64_t *last_trb)
{
    uint32_t d0 = (uint32_t)rt | ((uint32_t)req << 8) | ((uint32_t)val << 16);
    uint32_t d1 = (uint32_t)idx | ((uint32_t)len << 16);
    xhci_ring_push(r, base_phys, d0, d1, 8, TRB_TYPE(TRB_SETUP) | (1u << 6) | (3u << 16));          /* IDT, TRT = IN */
    if (len) xhci_ring_push(r, base_phys, (uint32_t)buf_phys, (uint32_t)(buf_phys >> 32), len, TRB_TYPE(TRB_DATA) | (1u << 16));   /* DIR = IN */
    uint64_t st = xhci_ring_push(r, base_phys, 0, 0, 0, TRB_TYPE(TRB_STATUS) | (1u << 5) | (len ? 0u : (1u << 16)));          /* IOC; estado en sentido contrario */
    if (last_trb) *last_trb = st;
}

uint16_t xhci_default_mps(uint8_t speed) { return speed == 4 ? 512 : speed == 3 ? 64 : 8; }
const char *xhci_speed_str(uint8_t s) { return s == 1 ? "Full (12 Mb/s)" : s == 2 ? "Low (1,5 Mb/s)" : s == 3 ? "High (480 Mb/s)" : s == 4 ? "Super (5 Gb/s)" : s == 5 ? "SuperSpeed+" : "?"; }

void xhci_build_addr_input(uint8_t *ctx, int csz64, uint8_t speed, uint8_t root_port, uint16_t max_packet, uint64_t ep0_ring_phys)
{
    uint32_t cs = csz64 ? 64u : 32u;
    zero(ctx, 33 * cs);
    uint32_t *icc = (uint32_t *)ctx;              /* contexto de control de entrada */
    icc[1] = (1u << 0) | (1u << 1);               /* A0 (ranura) y A1 (EP0) */
    uint32_t *slot = (uint32_t *)(ctx + cs);
    slot[0] = ((uint32_t)speed << 20) | (1u << 27);              /* velocidad y 1 entrada de contexto */
    slot[1] = (uint32_t)root_port << 16;
    uint32_t *ep0 = (uint32_t *)(ctx + 2 * cs);
    ep0[1] = (3u << 1) | (4u << 3) | ((uint32_t)max_packet << 16);   /* CErr 3, tipo Control, tamano maximo de paquete */
    ep0[2] = (uint32_t)ep0_ring_phys | 1u;                       /* puntero de desencolado con DCS = 1 */
    ep0[3] = (uint32_t)(ep0_ring_phys >> 32);
    ep0[4] = 8;                                                  /* longitud media de TRB */
}

int xhci_parse_config(const uint8_t *d, uint32_t len, struct usb_if_info *out, int max)
{
    int n = 0; uint32_t i = 0;
    if (len < 4 || d[1] != 2) return 0;                              /* debe empezar por un descriptor de configuracion */
    while (i + 2 <= len) {
        uint32_t l = d[i];
        if (l < 2 || i + l > len) break;                             /* longitud imposible: se corta aqui */
        if (d[i + 1] == 4 && l >= 9 && n < max) {                    /* interfaz */
            struct usb_if_info *f = &out[n++];
            f->num = d[i + 2]; f->alt = d[i + 3]; f->n_ep = 0; f->cls = d[i + 5]; f->sub = d[i + 6]; f->proto = d[i + 7];
            for (int k = 0; k < 4; k++) { f->ep[k].addr = f->ep[k].attr = f->ep[k].interval = 0; f->ep[k].mps = 0; }
        } else if (d[i + 1] == 5 && l >= 7 && n > 0) {               /* extremo: pertenece a la ultima interfaz */
            struct usb_if_info *f = &out[n - 1];
            if (f->n_ep < 4) {
                struct usb_ep_info *e = &f->ep[f->n_ep++];
                e->addr = d[i + 2]; e->attr = d[i + 3]; e->mps = (uint16_t)(d[i + 4] | (d[i + 5] << 8)); e->interval = d[i + 6];
            }
        }
        i += l;
    }
    return n;
}

const char *usb_class_str(uint8_t c, uint8_t s, uint8_t p)
{
    switch (c) {
    case 0x01: return "audio";
    case 0x02: return "comunicaciones";
    case 0x03: return s == 1 ? (p == 1 ? "HID teclado (boot)" : p == 2 ? "HID raton (boot)" : "HID (boot)") : "HID";
    case 0x07: return "impresora";
    case 0x08: return "almacenamiento masivo";
    case 0x09: return "hub";
    case 0x0E: return "video";
    case 0xE0: return "inalambrico";
    case 0xFF: return "especifico del fabricante";
    default:   return "otra";
    }
}

void xhci_ctrl_nodata(struct xhci_ring *r, uint64_t base_phys, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint64_t *last_trb)
{
    uint32_t d0 = (uint32_t)rt | ((uint32_t)req << 8) | ((uint32_t)val << 16);
    xhci_ring_push(r, base_phys, d0, (uint32_t)idx, 8, TRB_TYPE(TRB_SETUP) | (1u << 6));                     /* IDT, TRT = 0 (sin datos) */
    uint64_t st = xhci_ring_push(r, base_phys, 0, 0, 0, TRB_TYPE(TRB_STATUS) | (1u << 5) | (1u << 16));      /* IOC, estado IN */
    if (last_trb) *last_trb = st;
}

uint8_t xhci_ep_interval(uint8_t speed, uint8_t bi)
{
    if (speed == 3 || speed >= 4) { uint8_t v = bi ? (uint8_t)(bi - 1) : 0; return v > 15 ? 15 : v; }       /* HS / SS: ya es 2^(bi-1) microtramas */
    uint32_t frames8 = (uint32_t)(bi ? bi : 1) * 8u;                                                          /* FS / LS: bi milisegundos = bi*8 microtramas */
    uint8_t e = 0; while ((2u << e) <= frames8 && e < 15) e++;                                                /* floor(log2(frames8)) */
    return e < 3 ? 3 : (e > 10 ? 10 : e);
}

void xhci_build_cfgep_input(uint8_t *ctx, int csz64, const uint8_t *out_ctx, uint8_t dci, uint16_t mps, uint8_t interval, uint64_t ring_phys)
{
    uint32_t cs = csz64 ? 64u : 32u;
    zero(ctx, 33 * cs);
    for (uint32_t i = 0; i < cs; i++) ctx[cs + i] = out_ctx[i];                                               /* contexto de ranura actual */
    uint32_t *icc = (uint32_t *)ctx, *slot = (uint32_t *)(ctx + cs);
    icc[1] = 1u | (1u << dci);                                                                                /* A0 + A(dci) */
    if (((slot[0] >> 27) & 0x1Fu) < dci) slot[0] = (slot[0] & 0x07FFFFFFu) | ((uint32_t)dci << 27);          /* entradas de contexto */
    uint32_t *ep = (uint32_t *)(ctx + (uint32_t)(dci + 1) * cs);
    ep[0] = (uint32_t)interval << 16;
    ep[1] = (3u << 1) | (7u << 3) | ((uint32_t)mps << 16);                                                    /* CErr 3, tipo Interrupt IN */
    ep[2] = (uint32_t)ring_phys | 1u;                                                                         /* DCS = 1 */
    ep[3] = (uint32_t)(ring_phys >> 32);
    ep[4] = (uint32_t)mps | ((uint32_t)mps << 16);                                                            /* longitud media y maxima por intervalo */
}

void xhci_build_cfg_isoc_input(uint8_t *ctx, int csz64, const uint8_t *out_ctx, uint8_t dci, uint16_t mps_raw, uint8_t interval, uint64_t ring_phys)
{
    uint32_t cs = csz64 ? 64u : 32u;
    zero(ctx, 33 * cs);
    for (uint32_t i = 0; i < cs; i++) ctx[cs + i] = out_ctx[i];
    uint32_t *icc = (uint32_t *)ctx, *slot = (uint32_t *)(ctx + cs);
    icc[1] = 1u | (1u << dci);
    if (((slot[0] >> 27) & 0x1Fu) < dci) slot[0] = (slot[0] & 0x07FFFFFFu) | ((uint32_t)dci << 27);
    uint32_t mps = mps_raw & 0x7FFu, mult = (mps_raw >> 11) & 3u, esit = mps * (mult + 1u);
    uint32_t *ep = (uint32_t *)(ctx + (uint32_t)(dci + 1) * cs);
    ep[0] = ((uint32_t)interval << 16) | (mult << 8);                                                        /* Mult (0 = 1 transaccion) e intervalo */
    ep[1] = (5u << 3) | (mps << 16);                                                                        /* CErr 0, tipo Isoch IN, rafaga 0 */
    ep[2] = (uint32_t)ring_phys | 1u;
    ep[3] = (uint32_t)(ring_phys >> 32);
    ep[4] = esit | (esit << 16);                                                                            /* longitud media y carga maxima por intervalo */
}

void xhci_build_drop_ep_input(uint8_t *ctx, int csz64, const uint8_t *out_ctx, uint8_t dci)
{
    uint32_t cs = csz64 ? 64u : 32u;
    zero(ctx, 33 * cs);
    for (uint32_t i = 0; i < cs; i++) ctx[cs + i] = out_ctx[i];
    uint32_t *icc = (uint32_t *)ctx;
    icc[0] = 1u << dci;                                                                                     /* D(dci) */
    icc[1] = 1u;                                                                                            /* A0 */
}

uint32_t xhci_isoc_d3(uint32_t packets, int ioc, int sia)
{
    uint32_t tl = packets ? packets - 1u : 0u;
    return TRB_TYPE(TRB_ISOCH) | (ioc ? (1u << 5) : 0u) | ((tl & 0xFu) << 16) | (sia ? (1u << 31) : 0u);
}

void xhci_build_cfg_bulk_input(uint8_t *ctx, int csz64, const uint8_t *out_ctx, uint8_t dci_in, uint8_t dci_out, uint16_t mps,
                               uint64_t ring_in_phys, uint64_t ring_out_phys)
{
    uint32_t cs = csz64 ? 64u : 32u;
    zero(ctx, 33 * cs);
    for (uint32_t i = 0; i < cs; i++) ctx[cs + i] = out_ctx[i];
    uint32_t *icc = (uint32_t *)ctx, *slot = (uint32_t *)(ctx + cs);
    uint8_t top = dci_in > dci_out ? dci_in : dci_out;
    icc[1] = 1u | (1u << dci_in) | (1u << dci_out);
    if (((slot[0] >> 27) & 0x1Fu) < top) slot[0] = (slot[0] & 0x07FFFFFFu) | ((uint32_t)top << 27);
    for (int k = 0; k < 2; k++) {
        uint8_t dci = k ? dci_out : dci_in; uint64_t ring = k ? ring_out_phys : ring_in_phys;
        uint32_t *ep = (uint32_t *)(ctx + (uint32_t)(dci + 1) * cs);
        ep[0] = 0;
        ep[1] = (3u << 1) | ((k ? 2u : 6u) << 3) | ((uint32_t)mps << 16);                 /* CErr 3, Bulk OUT = 2, Bulk IN = 6 */
        ep[2] = (uint32_t)ring | 1u; ep[3] = (uint32_t)(ring >> 32);
        ep[4] = 3072u;                                                                    /* longitud media de TRB */
    }
}

void xhci_build_addr_input_ex(uint8_t *ctx, int csz64, uint8_t speed, uint32_t route, uint8_t root_port, uint16_t max_packet,
                              uint64_t ep0_ring_phys, uint8_t parent_slot, uint8_t parent_port)
{
    uint32_t cs = csz64 ? 64u : 32u;
    xhci_build_addr_input(ctx, csz64, speed, root_port, max_packet, ep0_ring_phys);
    uint32_t *slot = (uint32_t *)(ctx + cs);
    slot[0] |= route & 0xFFFFFu;
    if (parent_slot) slot[2] = (uint32_t)parent_slot | ((uint32_t)parent_port << 8);
}

void xhci_build_hub_input(uint8_t *ctx, int csz64, const uint8_t *out_ctx, uint8_t nports, uint8_t tt_think, int mtt)
{
    uint32_t cs = csz64 ? 64u : 32u;
    zero(ctx, 33 * cs);
    for (uint32_t i = 0; i < cs; i++) ctx[cs + i] = out_ctx[i];
    uint32_t *icc = (uint32_t *)ctx, *slot = (uint32_t *)(ctx + cs);
    icc[1] = 1u;
    slot[0] |= (1u << 26) | (mtt ? (1u << 25) : 0u);
    slot[1] = (slot[1] & 0x00FFFFFFu) | ((uint32_t)nports << 24);
    slot[2] = (slot[2] & ~(3u << 16)) | ((uint32_t)(tt_think & 3u) << 16);
}

int usb_parse_hub_desc(const uint8_t *d, uint32_t len, struct usb_hub_desc *o)
{
    if (len < 7 || d[0] < 7 || d[1] != 0x29 || d[2] == 0 || d[2] > 15) return -1;
    o->nports = d[2]; o->chars = (uint16_t)(d[3] | d[4] << 8); o->tt_think = (uint8_t)((o->chars >> 5) & 3u); o->power_on_ms_div2 = d[5];
    return 0;
}

void usb_decode_port_status(uint32_t st, struct usb_port_status *o)
{
    o->connected = st & 1u; o->enabled = (st >> 1) & 1u; o->reset = (st >> 4) & 1u; o->power = (st >> 8) & 1u;
    o->low = (st >> 9) & 1u; o->high = (st >> 10) & 1u; o->c_conn = (st >> 16) & 1u; o->c_reset = (st >> 20) & 1u;
}
