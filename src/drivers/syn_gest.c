/* SOMA_167_T480S: ver syn_gest.h.
 * Formato (modo absoluto + W, Synaptics Interfacing Guide / Linux synaptics_parse_hw_state):
 *   b0 = 1 0 W3 W2 0 W1 R L        b3 = 1 1 Y12 X12 0 W0 R L
 *   X = X12:b1[3:0]:b4   Y = Y12:b1[7:4]:b5   Z = b2 (presion)   W = W3 W2 W1 W0
 *   W: 0 = dos dedos, 1 = tres o mas, 2 = paquete extendido, 3 = paquete del TrackPoint (pass-through), 4..15 = anchura del
 *   dedo (>= 8: palma). Clickpad: el boton es (b0 ^ b3) & 1.
 * TrackPoint (W = 3): el paquete PS/2 normal de 3 bytes va en b1 (botones y signos), b4 (dx) y b5 (dy). */
#include "syn_gest.h"

#define Z_TOUCH   30          /* presion a partir de la que hay dedo (Linux: finger_high 30) */
#define Z_LIFT    25          /* y por debajo de la que se levanta (finger_low 25) */
#define TAP_MS    180         /* un toque dura menos */
#define TAP_MOVE  120         /* y se mueve menos (unidades; ~3 mm) */
#define TAP_HOLD_MS 120       /* cuanto espera un toque antes de confirmarse como clic (por si es un doble toque-arrastre) */
#define SCROLL_STEP 70        /* unidades por "muesca" de rueda */
#define W_PALM    8

void syn_init(struct syn_state *s, const struct syn_cfg *c)
{
    for (unsigned i = 0; i < sizeof *s; i++) ((uint8_t *)s)[i] = 0;
    s->cfg = *c;
    if (s->cfg.speed < 1 || s->cfg.speed > 10) s->cfg.speed = 5;
}

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* unidades del trackpad -> pixeles, con aceleracion segun la velocidad (en 1/256) */
static int32_t accel(struct syn_state *s, int32_t d, int32_t *acc, int32_t speed_units)
{
    int32_t v = speed_units; if (v > 160) v = 160;
    int32_t f = 40 + v * 3;                                    /* 40/256 a ~3/256*160+40 = 520/256 */
    f = f * s->cfg.speed / 5;
    if (s->cfg.wide_screen) f = f * 3 / 2;
    *acc += d * f;
    int32_t px = *acc / 256; *acc -= px * 256;
    return px;
}

/* botones que deben verse ahora: fisicos (clickpad/derecho) + TrackPoint + el clic de un toque en curso */
static uint8_t cur_buttons(const struct syn_state *s)
{
    uint8_t b = (uint8_t)(s->btn_phys | (s->prev_tp_buttons & 7));
    if (s->tap_pending == 2) b |= (uint8_t)(s->tap_btn == 2 ? 2 : 1);
    return b;
}

/* Un toque se confirma TAP_HOLD_MS despues de levantar el dedo (el derecho, al momento): pulsar en una llamada, soltar en la
 * siguiente (la interfaz necesita ver el flanco). */
void syn_tick(struct syn_state *s, uint32_t now_ms, struct syn_out *o)
{
    o->dx = o->dy = o->wheel = 0; o->changed = 0;
    if (s->tap_pending == 1 && now_ms - s->tap_t >= TAP_HOLD_MS) { s->tap_pending = 2; o->changed = 1; }
    else if (s->tap_pending == 2) { s->tap_pending = 0; o->changed = 1; }
    o->buttons = cur_buttons(s);
}

void syn_process(struct syn_state *s, const uint8_t p[6], uint32_t now_ms, struct syn_out *o)
{
    o->dx = o->dy = o->wheel = 0; o->changed = 0;
    if (!syn_packet_ok(p)) { s->bad++; o->buttons = cur_buttons(s); return; }
    s->packets++;
    int w = ((p[0] & 0x30) >> 2) | ((p[0] & 0x04) >> 1) | ((p[3] & 0x04) >> 2);
    s->last_w = w;

    if (w == 3) {                                              /* TrackPoint (pass-through): paquete PS/2 relativo */
        s->tp_packets++;
        uint8_t h = p[1];
        int32_t dx = p[4], dy = p[5];
        if (h & 0x10) dx -= 256;
        if (h & 0x20) dy -= 256;
        s->prev_tp_buttons = h & 7;
        if (h & 0x04) {                                        /* boton central + TrackPoint = desplazamiento (como Linux) */
            s->scroll_acc += dy * 6;
            while (s->scroll_acc >= SCROLL_STEP) { o->wheel++; s->scroll_acc -= SCROLL_STEP; }
            while (s->scroll_acc <= -SCROLL_STEP) { o->wheel--; s->scroll_acc += SCROLL_STEP; }
        } else { o->dx = dx; o->dy = dy; }                     /* el TrackPoint ya acelera por si mismo */
        o->buttons = cur_buttons(s); o->changed = 1;
        return;
    }
    if (w == 2) { o->buttons = cur_buttons(s); return; }       /* paquete extendido: no se usa */

    int x = ((p[3] & 0x10) << 8) | ((p[1] & 0x0F) << 8) | p[4];
    int y = ((p[3] & 0x20) << 7) | ((p[1] & 0xF0) << 4) | p[5];
    int z = p[2];
    s->last_z = z;
    int fingers = w == 0 ? 2 : w == 1 ? 3 : 1;
    int left_phys = s->cfg.clickpad ? ((p[0] ^ p[3]) & 1) : (p[0] & 1);
    int right_phys = s->cfg.clickpad ? 0 : ((p[0] >> 1) & 1);

    int down = s->touching ? z >= Z_LIFT : z >= Z_TOUCH;
    if (down && w >= W_PALM && !s->touching) { s->palms++; down = 0; }   /* palma: un apoyo ancho no cuenta */

    /* clic fisico del clickpad: con dos dedos es el derecho ("clickfinger", como libinput) */
    if (left_phys && !s->prev_left_phys) s->phys_right = s->cfg.clickpad && (fingers >= 2 || s->fingers_max >= 2);
    if (!left_phys) s->phys_right = 0;
    s->prev_left_phys = left_phys;
    s->btn_phys = (uint8_t)(((left_phys && !s->phys_right) ? 1 : 0) | ((right_phys || (left_phys && s->phys_right)) ? 2 : 0));

    if (down) {
        if (!s->touching) {
            s->touching = 1; s->touch_t0 = now_ms; s->fingers_max = fingers; s->travel = 0; s->moved = 0; s->have_last = 0;
            s->scroll_acc = 0;
            if (s->tap_pending == 1) s->tap_pending = 0;       /* otro toque enseguida: el primero no era un clic suelto */
        }
        if (fingers > s->fingers_max) s->fingers_max = fingers;
        s->last_fingers = fingers;
        if (s->have_last && x && y) {
            int32_t dx = x - s->last_x, dy = y - s->last_y;
            if (iabs(dx) > 800 || iabs(dy) > 800) { dx = 0; dy = 0; }   /* salto (un dedo que entra o sale): no mover */
            s->travel += iabs(dx) + iabs(dy);
            if (fingers >= 2) {                                /* dos dedos: desplazamiento vertical */
                s->scroll_acc += s->cfg.natural ? -dy : dy;
                while (s->scroll_acc >= SCROLL_STEP) { o->wheel++; s->scroll_acc -= SCROLL_STEP; s->scrolls++; }
                while (s->scroll_acc <= -SCROLL_STEP) { o->wheel--; s->scroll_acc += SCROLL_STEP; s->scrolls++; }
            } else if (s->fingers_max < 2) {
                int32_t sp = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
                o->dx = accel(s, dx, &s->acc_x, sp);
                o->dy = accel(s, dy, &s->acc_y, sp);           /* la Y del trackpad crece hacia arriba = convencion PS/2 */
            }
            if (s->travel > TAP_MOVE) s->moved = 1;
        }
        if (x && y) { s->last_x = x; s->last_y = y; s->have_last = 1; }
    } else if (s->touching) {
        s->touching = 0;
        uint32_t dur = now_ms - s->touch_t0;
        if (s->cfg.tap && dur < TAP_MS && !s->moved && !left_phys && !right_phys) {
            if (s->fingers_max >= 2) { s->tap_pending = 1; s->tap_t = now_ms - TAP_HOLD_MS; s->tap_btn = 2; s->rtaps++; }
            else { s->tap_pending = 1; s->tap_t = now_ms; s->tap_btn = 1; s->taps++; }
        }
        s->fingers_max = 0;
    }
    o->buttons = cur_buttons(s);
    o->changed = 1;
}
