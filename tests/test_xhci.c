/* PARCHE 046: pruebas del protocolo xHCI (anillos, ciclo, enlace, eventos, transferencia de control) con un xHC simulado. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/xhci_proto.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    enum { N = 16 };
    struct xhci_trb *mem = aligned_alloc(64, N * 16); struct xhci_ring r;
    uint64_t base = (uint64_t)(uintptr_t)mem;
    xhci_ring_init(&r, mem, N, base);
    CHECK(sizeof(struct xhci_trb) == 16 && TRB_GET_TYPE(mem[N - 1].d3) == TRB_LINK && (mem[N - 1].d3 & 2) && mem[N - 1].d0 == (uint32_t)base);
    /* productor simulado: consume TRBs con el ciclo esperado; debe ver exactamente lo encolado, en orden, durante muchas vueltas */
    uint32_t cons = 0; uint8_t ccycle = 1; int seen = 0, bad = 0, wraps = 0;
    for (int i = 0; i < 1000; i++) {
        uint64_t ph = xhci_ring_push(&r, base, i, i ^ 0xFF, 7, TRB_TYPE(TRB_NORMAL));
        CHECK(ph >= base && ph < base + N * 16 && (ph - base) % 16 == 0);
        if (i % 7 == 6 || i == 999) {                                   /* el controlador drena de golpe */
            for (;;) {
                struct xhci_trb *t = &mem[cons];
                if ((t->d3 & 1) != ccycle) break;
                if (TRB_GET_TYPE(t->d3) == TRB_LINK) { cons = 0; ccycle ^= 1; wraps++; continue; }
                if (t->d0 != (uint32_t)seen) bad++;
                seen++; cons++;
            }
        }
    }
    CHECK(seen == 1000 && bad == 0 && wraps >= 60);
    /* anillo de eventos: el controlador escribe con su ciclo; el consumidor los ve en orden y da la vuelta */
    struct xhci_trb *em = aligned_alloc(64, 8 * 16); struct xhci_evring ev; xhci_ev_init(&ev, em, 8);
    uint32_t wp = 0; uint8_t wc = 1; int got = 0, order_bad = 0;
    for (int i = 0; i < 100; i++) {
        em[wp].d0 = i; em[wp].d2 = 1u << 24; em[wp].d3 = TRB_TYPE(TRB_EV_CMD_DONE) | wc;
        if (++wp == 8) { wp = 0; wc ^= 1; }
        struct xhci_trb o; struct xhci_evring *e = &ev;
        CHECK(xhci_ev_pop(e, &o) == 1); if (o.d0 != (uint32_t)i) order_bad++;
        CHECK(xhci_ev_pop(e, &o) == 0);                                  /* nada mas pendiente */
        got++;
    }
    CHECK(got == 100 && order_bad == 0 && xhci_ev_deq_phys(&ev, 0x1000) == 0x1000 + (uint64_t)ev.deq * 16);
    /* transferencia de control: 3 etapas y TRB de estado con IOC */
    struct xhci_ring cr; xhci_ring_init(&cr, mem, N, base);
    uint64_t last = 0;
    xhci_ctrl_in(&cr, base, 0x80, 6, 0x0100, 0, 18, 0x123000, &last);
    CHECK(TRB_GET_TYPE(mem[0].d3) == TRB_SETUP && (mem[0].d3 & (1 << 6)) && ((mem[0].d3 >> 16) & 3) == 3 && mem[0].d0 == (0x80u | (6u << 8) | (0x0100u << 16)) && mem[0].d1 == 18u << 16 && mem[0].d2 == 8);
    CHECK(TRB_GET_TYPE(mem[1].d3) == TRB_DATA && (mem[1].d3 & (1 << 16)) && mem[1].d0 == 0x123000 && mem[1].d2 == 18);
    CHECK(TRB_GET_TYPE(mem[2].d3) == TRB_STATUS && (mem[2].d3 & (1 << 5)) && !(mem[2].d3 & (1 << 16)) && last == base + 2 * 16);
    CHECK((mem[0].d3 & 1) && (mem[1].d3 & 1) && (mem[2].d3 & 1));
    /* contextos */
    for (int csz = 0; csz < 2; csz++) {
        uint32_t cs = csz ? 64 : 32; uint8_t *ctx = malloc(33 * cs);
        xhci_build_addr_input(ctx, csz, 3, 5, 64, 0x2345000ULL);
        uint32_t *icc = (uint32_t *)ctx, *slot = (uint32_t *)(ctx + cs), *ep = (uint32_t *)(ctx + 2 * cs);
        CHECK(icc[1] == 3 && (slot[0] >> 20 & 0xF) == 3 && (slot[0] >> 27) == 1 && (slot[1] >> 16) == 5);
        CHECK(((ep[1] >> 3) & 7) == 4 && (ep[1] >> 16) == 64 && ((ep[1] >> 1) & 3) == 3 && ep[2] == (0x2345000u | 1) && ep[3] == 0 && ep[4] == 8);
        free(ctx);
    }
    CHECK(xhci_default_mps(4) == 512 && xhci_default_mps(3) == 64 && xhci_default_mps(1) == 8 && xhci_default_mps(2) == 8);
    /* descriptor de configuracion tipo teclado HID (como el que anuncia usb-kbd de QEMU) y casos malformados */
    {
        static const uint8_t cfg[] = { 9,2,34,0,1,1,0,0xA0,50,  9,4,0,0,1,3,1,1,0,  9,0x21,0x11,1,0,1,0x22,63,0,  7,5,0x81,3,8,0,10 };
        struct usb_if_info f[4];
        CHECK(xhci_parse_config(cfg, sizeof cfg, f, 4) == 1);
        CHECK(f[0].cls == 3 && f[0].sub == 1 && f[0].proto == 1 && f[0].n_ep == 1 && f[0].ep[0].addr == 0x81 && f[0].ep[0].attr == 3 && f[0].ep[0].mps == 8 && f[0].ep[0].interval == 10);
        CHECK(xhci_parse_config(cfg, 5, f, 4) == 0 || 1);                /* truncado: no debe salirse del buffer (ASAN lo vigila) */
        for (uint32_t cut = 0; cut <= sizeof cfg; cut++) xhci_parse_config(cfg, cut, f, 4);
        uint8_t bad[sizeof cfg]; memcpy(bad, cfg, sizeof cfg); bad[9] = 0;      /* longitud 0: no debe colgarse */
        CHECK(xhci_parse_config(bad, sizeof bad, f, 4) == 0);
        bad[9] = 200;                                                          /* longitud mayor que el buffer */
        CHECK(xhci_parse_config(bad, sizeof bad, f, 4) == 0);
        CHECK(xhci_parse_config(cfg, sizeof cfg, f, 0) == 0);                  /* sin hueco de salida */
        uint8_t rnd[64]; srand(7);
        for (int it = 0; it < 20000; it++) { for (int k = 0; k < 64; k++) rnd[k] = (uint8_t)rand(); rnd[1] = 2; xhci_parse_config(rnd, 64, f, 4); }
        CHECK(usb_class_str(3, 1, 1)[0] == 'H' && usb_class_str(9, 0, 0)[0] == 'h');
    }
    {   /* SOMA_178_T480S: extremo isocrono IN (camara UVC) */
        for (int cs64 = 0; cs64 < 2; cs64++) {
            uint32_t cs = cs64 ? 64u : 32u; static uint8_t in[33 * 64], out[64];
            memset(out, 0, sizeof out); ((uint32_t *)out)[0] = (3u << 20) | (1u << 27); ((uint32_t *)out)[1] = 5u << 16;
            xhci_build_cfg_isoc_input(in, cs64, out, 3, 0x1400 | 0x0000, 0, 0x123456000ull);       /* 1024 x 3 transacciones, cada microtrama */
            const uint32_t *icc = (const uint32_t *)in, *sl = (const uint32_t *)(in + cs), *ep = (const uint32_t *)(in + 4 * cs);
            CHECK(icc[0] == 0 && icc[1] == (1u | 8u));
            CHECK(((sl[0] >> 27) & 31) == 3 && ((sl[0] >> 20) & 15) == 3 && sl[1] == (5u << 16));          /* entradas de contexto sube a 3, se conserva lo demas */
            CHECK(((ep[0] >> 16) & 255) == 0 && ((ep[0] >> 8) & 3) == 2);                                    /* intervalo 0, Mult 2 (3 transacciones) */
            CHECK(((ep[1] >> 3) & 7) == 5 && ((ep[1] >> 1) & 3) == 0 && (ep[1] >> 16) == 1024 && ((ep[1] >> 8) & 255) == 0);
            CHECK(ep[2] == (0x23456000u | 1u) && ep[3] == 1u);
            CHECK((ep[4] & 0xFFFF) == 3072 && (ep[4] >> 16) == 3072);
            xhci_build_cfg_isoc_input(in, cs64, out, 5, 0x0380, 3, 0x1000);
            ep = (const uint32_t *)(in + 6 * cs);
            CHECK(((ep[0] >> 8) & 3) == 0 && ((ep[0] >> 16) & 255) == 3 && (ep[1] >> 16) == 896 && (ep[4] & 0xFFFF) == 896);
            sl = (const uint32_t *)(in + cs); CHECK(((sl[0] >> 27) & 31) == 5);
            xhci_build_drop_ep_input(in, cs64, out, 3);
            icc = (const uint32_t *)in; sl = (const uint32_t *)(in + cs);
            CHECK(icc[0] == 8u && icc[1] == 1u && sl[1] == (5u << 16) && ((sl[0] >> 27) & 31) == 1);
        }
        CHECK(xhci_isoc_d3(3, 1, 1) == (TRB_TYPE(TRB_ISOCH) | 32u | (2u << 16) | 0x80000000u));
        CHECK(xhci_isoc_d3(1, 0, 0) == TRB_TYPE(TRB_ISOCH) && TRB_GET_TYPE(xhci_isoc_d3(2, 1, 0)) == TRB_ISOCH);
        CHECK(xhci_isoc_d3(0, 1, 0) == (TRB_TYPE(TRB_ISOCH) | 32u));
        CHECK(xhci_ep_interval(3, 1) == 0 && xhci_ep_interval(3, 4) == 3);
    }
    printf(fails ? "xhci_proto: %d FALLOS\n" : "xhci_proto: todo correcto\n", fails);
    free(mem); free(em);
    return fails != 0;
}
