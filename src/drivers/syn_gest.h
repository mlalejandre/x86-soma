#ifndef SOMA_SYN_GEST_H
#define SOMA_SYN_GEST_H
/* SOMA_167_T480S: trackpad Synaptics en modo absoluto (paquetes de 6 bytes con W) -> puntero, clics y gestos.
 * Parte pura (sin E/S): la usa mouse.c y la prueba tests/test_syn_167.c. Referencia: Linux drivers/input/mouse/synaptics.c. */
#include <stdint.h>

struct syn_cfg {
    int clickpad;            /* el boton del clickpad llega como (b0 ^ b3) & 1 */
    int natural;             /* desplazamiento "natural" (como en el Mac): el contenido sigue a los dedos */
    int tap;                 /* toque = clic */
    int speed;               /* 1..10 (5 por defecto) */
    int wide_screen;         /* pantallas de 2400 px o mas: un poco mas rapido */
};
struct syn_state {
    struct syn_cfg cfg;
    /* dedo en curso */
    int touching, fingers_max, moved, last_x, last_y, have_last;
    uint32_t touch_t0;
    int32_t travel;          /* recorrido total del toque (unidades del trackpad) */
    int32_t acc_x, acc_y, scroll_acc;
    int prev_left_phys, prev_tp_buttons, phys_right; uint8_t btn_phys;
    /* toque pendiente (para no confundir un toque con el inicio de un arrastre) */
    int tap_pending; uint32_t tap_t; int tap_btn;
    /* estadisticas (orden trackpad) */
    uint32_t packets, bad, tp_packets, taps, rtaps, scrolls, palms;
    int last_w, last_z, last_fingers;
};
/* Salida de un paquete: movimiento del puntero en pixeles (dy positivo = hacia arriba, convencion PS/2), rueda (positivo =
 * arriba) y botones (bit 0 izquierdo, bit 1 derecho, bit 2 central) tal como deben quedar despues del paquete. */
struct syn_out { int32_t dx, dy, wheel; uint8_t buttons; int changed; };

void syn_init(struct syn_state *s, const struct syn_cfg *c);
/* 1 = paquete valido (cabecera de modo absoluto: b0 = 10xx0xxx, b3 = 11xx0xxx) */
static inline int syn_packet_ok(const uint8_t p[6]) { return (p[0] & 0xC8) == 0x80 && (p[3] & 0xC8) == 0xC0; }
void syn_process(struct syn_state *s, const uint8_t p[6], uint32_t now_ms, struct syn_out *o);
/* Llamar de vez en cuando sin paquetes (p. ej. cada 20 ms): suelta el clic de un toque pendiente. */
void syn_tick(struct syn_state *s, uint32_t now_ms, struct syn_out *o);
#endif
