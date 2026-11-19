/* SOMA_167_T480S: prueba en el anfitrion de src/drivers/syn_gest.c con paquetes Synaptics de 6 bytes sinteticos
 * (formato de la Synaptics Interfacing Guide, el mismo que decodifica Linux). Uso: test_syn_167 [paquetes al azar] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "syn_gest.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FALLO linea %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void pkt(uint8_t p[6], int x, int y, int z, int w, int click /* clickpad */)
{
    p[0] = (uint8_t)(0x80 | (((w >> 2) & 3) << 4) | (((w >> 1) & 1) << 2));
    p[1] = (uint8_t)((((y >> 8) & 0x0F) << 4) | ((x >> 8) & 0x0F));
    p[2] = (uint8_t)z;
    p[3] = (uint8_t)(0xC0 | (((y >> 12) & 1) << 5) | (((x >> 12) & 1) << 4) | ((w & 1) << 2) | (click ? 1 : 0));   /* b0 bit0 = 0: XOR = click */
    p[4] = (uint8_t)(x & 0xFF);
    p[5] = (uint8_t)(y & 0xFF);
}
static void tp_pkt(uint8_t p[6], int dx, int dy, int buttons)
{
    pkt(p, 0, 0, 0, 3, 0);
    p[1] = (uint8_t)((buttons & 7) | (dx < 0 ? 0x10 : 0) | (dy < 0 ? 0x20 : 0) | 0x08);
    p[4] = (uint8_t)dx; p[5] = (uint8_t)dy;
}

struct run { int32_t dx, dy, wheel; int lpress, rpress, lrel, rrel; uint8_t last; };
static uint32_t T;
static void feed(struct syn_state *s, const uint8_t p[6], struct run *r)
{
    struct syn_out o; syn_process(s, p, T, &o); T += 12;
    r->dx += o.dx; r->dy += o.dy; r->wheel += o.wheel;
    if ((o.buttons & 1) && !(r->last & 1)) r->lpress++;
    if (!(o.buttons & 1) && (r->last & 1)) r->lrel++;
    if ((o.buttons & 2) && !(r->last & 2)) r->rpress++;
    if (!(o.buttons & 2) && (r->last & 2)) r->rrel++;
    r->last = o.buttons;
}
static void idle(struct syn_state *s, int ms, struct run *r)
{
    for (int t = 0; t < ms; t += 10) {
        struct syn_out o; syn_tick(s, T, &o); T += 10;
        if ((o.buttons & 1) && !(r->last & 1)) r->lpress++;
        if (!(o.buttons & 1) && (r->last & 1)) r->lrel++;
        if ((o.buttons & 2) && !(r->last & 2)) r->rpress++;
        if (!(o.buttons & 2) && (r->last & 2)) r->rrel++;
        r->last = o.buttons;
    }
}
static void fresh(struct syn_state *s, int natural)
{
    struct syn_cfg c = { 1, natural, 1, 5, 0 };
    syn_init(s, &c);
}

int main(int argc, char **argv)
{
    long fuzz = argc > 1 ? atol(argv[1]) : 200000;
    struct syn_state s; uint8_t p[6]; struct run r;

    /* 1. un dedo a la derecha: el puntero va a la derecha, sin clic */
    fresh(&s, 0); memset(&r, 0, sizeof r);
    for (int i = 0; i < 30; i++) { pkt(p, 3000 + i * 40, 3000, 60, 6, 0); feed(&s, p, &r); }
    pkt(p, 0, 0, 0, 6, 0); feed(&s, p, &r); idle(&s, 300, &r);
    CHECK(r.dx > 50 && r.dy == 0 && r.lpress == 0 && r.rpress == 0, "mover: dx %d dy %d clics %d/%d", r.dx, r.dy, r.lpress, r.rpress);
    /* hacia arriba (Y del trackpad crece hacia arriba = dy positivo) */
    fresh(&s, 0); memset(&r, 0, sizeof r);
    for (int i = 0; i < 20; i++) { pkt(p, 3000, 3000 + i * 40, 60, 6, 0); feed(&s, p, &r); }
    CHECK(r.dy > 30 && r.dx == 0, "subir: dx %d dy %d", r.dx, r.dy);

    /* 2. toque corto quieto: clic izquierdo (pulsar y soltar) */
    fresh(&s, 0); memset(&r, 0, sizeof r);
    for (int i = 0; i < 6; i++) { pkt(p, 3000, 3000, 60, 6, 0); feed(&s, p, &r); }
    pkt(p, 0, 0, 0, 6, 0); feed(&s, p, &r); idle(&s, 400, &r);
    CHECK(r.lpress == 1 && r.lrel == 1 && r.rpress == 0, "toque: L %d/%d R %d", r.lpress, r.lrel, r.rpress);
    /* toque largo (500 ms): no es clic */
    fresh(&s, 0); memset(&r, 0, sizeof r);
    for (int i = 0; i < 45; i++) { pkt(p, 3000, 3000, 60, 6, 0); feed(&s, p, &r); }
    pkt(p, 0, 0, 0, 6, 0); feed(&s, p, &r); idle(&s, 400, &r);
    CHECK(r.lpress == 0, "toque largo hizo clic");

    /* 3. toque con dos dedos: clic derecho */
    fresh(&s, 0); memset(&r, 0, sizeof r);
    for (int i = 0; i < 6; i++) { pkt(p, 3000, 3000, 70, i < 1 ? 6 : 0, 0); feed(&s, p, &r); }
    pkt(p, 0, 0, 0, 6, 0); feed(&s, p, &r); idle(&s, 400, &r);
    CHECK(r.rpress == 1 && r.rrel == 1 && r.lpress == 0, "toque 2 dedos: R %d/%d L %d", r.rpress, r.rrel, r.lpress);

    /* 4. dos dedos hacia arriba: rueda hacia arriba (tradicional), sin mover el puntero */
    fresh(&s, 0); memset(&r, 0, sizeof r);
    for (int i = 0; i < 30; i++) { pkt(p, 3000, 2000 + i * 30, 70, 0, 0); feed(&s, p, &r); }
    CHECK(r.wheel >= 10 && r.dx == 0 && r.dy == 0, "desplazar: rueda %d dx %d dy %d", r.wheel, r.dx, r.dy);
    pkt(p, 0, 0, 0, 6, 0); feed(&s, p, &r); idle(&s, 400, &r);
    CHECK(r.lpress == 0 && r.rpress == 0, "desplazar hizo clic");
    /* natural: al reves */
    fresh(&s, 1); memset(&r, 0, sizeof r);
    for (int i = 0; i < 30; i++) { pkt(p, 3000, 2000 + i * 30, 70, 0, 0); feed(&s, p, &r); }
    CHECK(r.wheel <= -10, "natural: rueda %d", r.wheel);

    /* 5. clic fisico del clickpad: un dedo = izquierdo mantenido; dos dedos = derecho */
    fresh(&s, 0); memset(&r, 0, sizeof r);
    for (int i = 0; i < 10; i++) { pkt(p, 3000 + i * 30, 3000, 80, 6, 1); feed(&s, p, &r); }
    CHECK(r.lpress == 1 && (r.last & 1) && r.rpress == 0, "clic fisico: L %d last %d", r.lpress, r.last);
    pkt(p, 3300, 3000, 80, 6, 0); feed(&s, p, &r);
    CHECK(r.lrel == 1, "soltar clic fisico");
    fresh(&s, 0); memset(&r, 0, sizeof r);
    pkt(p, 3000, 3000, 80, 0, 0); feed(&s, p, &r);
    for (int i = 0; i < 5; i++) { pkt(p, 3000, 3000, 80, 0, 1); feed(&s, p, &r); }
    CHECK(r.rpress == 1 && r.lpress == 0, "clic con dos dedos: R %d L %d", r.rpress, r.lpress);

    /* 6. palma (W ancho): nada */
    fresh(&s, 0); memset(&r, 0, sizeof r);
    for (int i = 0; i < 20; i++) { pkt(p, 3000 + i * 50, 3000, 120, 12, 0); feed(&s, p, &r); }
    pkt(p, 0, 0, 0, 6, 0); feed(&s, p, &r); idle(&s, 300, &r);
    CHECK(r.dx == 0 && r.lpress == 0 && s.palms >= 1, "palma: dx %d clic %d palmas %u", r.dx, r.lpress, s.palms);

    /* 7. TrackPoint por pass-through: movimiento y botones */
    fresh(&s, 0); memset(&r, 0, sizeof r);
    tp_pkt(p, 5, -3, 1); feed(&s, p, &r);
    CHECK(r.dx == 5 && r.dy == -3 && (r.last & 1) && s.tp_packets == 1, "trackpoint: %d %d btn %d", r.dx, r.dy, r.last);
    tp_pkt(p, 0, 12, 4); for (int i = 0; i < 5; i++) feed(&s, p, &r);
    CHECK(r.wheel > 0 && r.dy == -3, "trackpoint + central: rueda %d", r.wheel);

    /* 8. paquete invalido: se cuenta y no mueve */
    fresh(&s, 0); memset(&r, 0, sizeof r);
    uint8_t bad[6] = { 0x08, 1, 2, 3, 4, 5 }; feed(&s, bad, &r);
    CHECK(s.bad == 1 && r.dx == 0, "invalido");

    /* 9. paquetes al azar: nunca un movimiento disparatado, contadores coherentes */
    fresh(&s, 0); memset(&r, 0, sizeof r);
    uint32_t seed = 12345; int32_t maxd = 0;
    for (long i = 0; i < fuzz; i++) {
        for (int k = 0; k < 6; k++) { seed = seed * 1103515245u + 12345u; p[k] = (uint8_t)(seed >> 16); }
        if (i & 1) { p[0] = (uint8_t)((p[0] & 0x37) | 0x80); p[3] = (uint8_t)((p[3] & 0x37) | 0xC0); }
        struct syn_out o; syn_process(&s, p, T, &o); T += 7;
        if (o.dx > maxd) maxd = o.dx;
        if (-o.dx > maxd) maxd = -o.dx;
        if ((i & 63) == 0) syn_tick(&s, T, &o);
    }
    CHECK(maxd < 2000, "movimiento disparatado: %d", maxd);
    CHECK(s.packets + s.bad == (uint32_t)fuzz, "contadores %u + %u", s.packets, s.bad);

    if (fails) { printf("syn: %d fallos\n", fails); return 1; }
    printf("syn: mover, toque, toque con dos dedos, desplazar (normal y natural), clic fisico 1/2 dedos, palma, TrackPoint e invalidos correctos; %ld paquetes al azar sin problemas\n", fuzz);
    return 0;
}
