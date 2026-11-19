/* SOMA_155_T480S: prueba en el anfitrion del driver i8042 (src/arch/x86_64/i8042.c) contra un controlador simulado.
 * El simulador responde como el EC del ThinkPad: teclado AT (AB 83 -> AB 41 traducido) y, en el puerto auxiliar, un
 * trackpad Synaptics (identificacion "E8 00 x4 + E9" -> 01 47 18) o, con -DSIM_INTELLI, un raton IntelliMouse.
 * Comprueba la secuencia de inicializacion, el CTR final, la deteccion y el reparto de bytes teclado/raton. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "i8042.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

/* ---------------- simulador */
static struct { uint8_t b, aux; } q[256]; static int qh, qt;
static uint8_t ctr = 0x61, next_dst;            /* 1 = CTR, 2 = aux */
static int kbd_param, aux_param, e8_count, e8_arg, aux_stream, kbd_scan, kbd_set = 2, ports_kbd = 1, ports_aux = 1;
static uint8_t pic_m = 0xFC, pic_s = 0xFF;
static int log_aux[512], nlog;
static int spec_arg[16], spec_rate[16], nspec, spec_pending;   /* SOMA_167: ordenes especiales de Synaptics */
static uint64_t now;
static void push(uint8_t b, int aux) { q[qh].b = b; q[qh].aux = (uint8_t)aux; qh = (qh + 1) & 255; }
uint64_t timer_get_uptime_ms(void) { return now++ / 64; }
uint8_t sim_inb(uint16_t p)
{
    if (p == 0x64) return qh != qt ? (uint8_t)(0x01 | (q[qt].aux ? 0x20 : 0)) : 0;
    if (p == 0x60) { if (qh == qt) return 0; uint8_t b = q[qt].b; qt = (qt + 1) & 255; return b; }
    if (p == 0x21) return pic_m;
    if (p == 0xA1) return pic_s;
    return 0xFF;
}
static void kbd_dev(uint8_t v)
{
    if (kbd_param) { if (kbd_param == 0xF0) kbd_set = v; kbd_param = 0; push(0xFA, 0); return; }
    switch (v) {
    case 0xF0: case 0xED: kbd_param = v; push(0xFA, 0); break;
    case 0xF2: push(0xFA, 0); push(0xAB, 0); push(0x41, 0); break;
    case 0xF4: kbd_scan = 1; push(0xFA, 0); break;
    case 0xF5: kbd_scan = 0; push(0xFA, 0); break;
    default: push(0xFA, 0);
    }
}
static void aux_dev(uint8_t v)
{
    if (nlog < 512) log_aux[nlog++] = v;
    if (aux_param) {
        if (aux_param == 0xE8) { e8_arg = (e8_arg << 2) | (v & 3); e8_count++; }
        if (aux_param == 0xF3 && spec_pending) { spec_rate[nspec] = v; if (nspec < 15) nspec++; spec_pending = 0; }
        aux_param = 0; push(0xFA, 1); return;
    }
    if (v == 0xF3 && e8_count == 4) { spec_arg[nspec] = e8_arg; spec_pending = 1; }   /* SOMA_167: orden especial (E8 x4 + F3) */
    if (v != 0xE8 && v != 0xE9) { e8_count = 0; e8_arg = 0; }
    switch (v) {
    case 0xFF: aux_stream = 0; push(0xFA, 1); push(0xAA, 1); push(0x00, 1); break;
    case 0xF3: case 0xE8: aux_param = v; push(0xFA, 1); break;
    case 0xE9:
        push(0xFA, 1);
#ifndef SIM_INTELLI
        if (e8_count == 4 && e8_arg == 0) { push(0x01, 1); push(0x47, 1); push(0x18, 1); }   /* Synaptics v8.1 */
        else if (e8_count == 4 && e8_arg == 0x02) { push(0xC0, 1); push(0x47, 1); push(0x80, 1); }   /* SOMA_167: capacidades: extendidas, 4 consultas, pass-through */
        else if (e8_count == 4 && e8_arg == 0x09) { push(0x00, 1); push(0x00, 1); push(0x00, 1); }
        else if (e8_count == 4 && e8_arg == 0x0C) { push(0x10, 1); push(0x00, 1); push(0x00, 1); }   /* clickpad */
        else
#endif
        { push(0x00, 1); push(0x02, 1); push(0x64, 1); }
        e8_count = 0; e8_arg = 0; break;
    case 0xF2: push(0xFA, 1);
#ifdef SIM_INTELLI
        push(0x03, 1);
#else
        push(0x00, 1);
#endif
        break;
    case 0xF4: aux_stream = 1; push(0xFA, 1); break;
    case 0xF5: aux_stream = 0; push(0xFA, 1); break;
    default: push(0xFA, 1);
    }
}
void sim_outb(uint16_t p, uint8_t v)
{
    if (p == 0x21) { pic_m = v; return; }
    if (p == 0xA1) { pic_s = v; return; }
    if (p == 0x80) return;
    if (p == 0x64) {
        switch (v) {
        case 0xAD: ports_kbd = 0; break;  case 0xAE: ports_kbd = 1; break;
        case 0xA7: ports_aux = 0; break;  case 0xA8: ports_aux = 1; break;
        case 0x20: push(ctr, 0); break;   case 0x60: next_dst = 1; break;
        case 0xA9: push(0x00, 0); break;  case 0xD4: next_dst = 2; break;
        }
        return;
    }
    if (p == 0x60) {
        uint8_t d = next_dst; next_dst = 0;
        if (d == 1) ctr = v; else if (d == 2) aux_dev(v); else kbd_dev(v);
    }
}
/* ---------------- lo que el driver llama de la consola */
static uint8_t kbuf[64]; static int nk;
void keyboard_inject_scancode(uint8_t sc) { if (nk < 64) kbuf[nk++] = sc; }
void keyboard_irq_handler(void) {}
void kprint(const char *s) { fputs(s, stdout); }
void kprint_hex8(uint8_t v) { printf("%02X", v); }
void kprint_dec(uint32_t v) { printf("%u", v); }

int main(void)
{
    push(0x9C, 0); push(0x08, 1);                    /* restos que deja el firmware: deben descartarse */
    i8042_init();
    CHECK(ports_kbd && ports_aux, "puertos activados al final");
    CHECK(ctr & 0x40, "traduccion a juego 1 activa");
    CHECK((ctr & 0x03) == 0x03, "interrupciones de teclado y raton activadas en el CTR");
    CHECK((ctr & 0x30) == 0, "relojes de los dos puertos encendidos");
    CHECK(!(pic_m & 0x02) && !(pic_m & 0x04) && !(pic_s & 0x10), "IRQ 1, 2 (cascada) y 12 desenmascaradas en el PIC");
    CHECK(kbd_scan && kbd_set == 2, "teclado escaneando en juego 2");
    CHECK(i8042_aux_present(), "puntero detectado");
#ifdef SIM_INTELLI
    CHECK(!i8042_touchpad() && i8042_aux_wheel(), "raton IntelliMouse con rueda (no Synaptics)");
#else
    CHECK(i8042_touchpad() && !i8042_aux_wheel(), "trackpad Synaptics detectado, sin sondeo de rueda");
    int saw_rate_magic = 0;                          /* al Synaptics no se le manda la secuencia 200/100/80 */
    for (int i = 0; i + 1 < nlog; i++) if (log_aux[i] == 0xF3 && log_aux[i + 1] == 200) saw_rate_magic = 1;
    CHECK(!saw_rate_magic, "sin secuencia IntelliMouse al trackpad");
#endif
    CHECK(qh == qt, "buffer vacio tras la inicializacion");
    CHECK(nk == 0, "los restos del firmware no llegan al teclado");

    /* reparto: teclado y raton entremezclados */
    i8042_aux_stream(1);
    CHECK(aux_stream, "envio de movimientos activado");
    push(0x08, 1); push(0x1E, 0); push(0x05, 1); push(0x9E, 0); push(0xFB, 1);
    i8042_service();
    CHECK(nk == 2 && kbuf[0] == 0x1E && kbuf[1] == 0x9E, "bytes del teclado al traductor, en orden");
    uint8_t b; uint64_t t; uint8_t got[3]; int n = 0;
    while (n < 3 && i8042_aux_get(&b, &t)) got[n++] = b;
    CHECK(n == 3 && got[0] == 0x08 && got[1] == 0x05 && got[2] == 0xFB, "paquete del raton completo y en orden");
    CHECK(!i8042_aux_get(&b, &t), "cola del raton vacia");
    /* muchos bytes seguidos: el servicio vacia de 32 en 32 sin perder nada */
    for (int i = 0; i < 90; i++) push((uint8_t)i, 1);
    i8042_service(); i8042_service(); i8042_service();
    n = 0; while (i8042_aux_get(&b, &t)) { CHECK(b == n, "orden de la cola"); n++; }
    CHECK(n == 90, "90 bytes recibidos");
    /* una tecla que llega mientras se manda una orden al raton no se pierde */
    nk = 0; push(0x30, 0);
    i8042_aux_stream(0);
    CHECK(!aux_stream, "envio de movimientos parado");
    CHECK(nk == 1 && kbuf[0] == 0x30, "tecla recibida durante una orden al raton");
#ifndef SIM_INTELLI
    /* SOMA_167_T480S: modo absoluto (gestos): capacidades, byte de modo 0xC5 (absoluto + 80 pps + sin gestos propios + W) con
     * la tasa especial 0x14, TrackPoint activado por el pass-through (0xF4 con la tasa 0x28), y vuelta al modo relativo */
    nspec = 0;
    CHECK(i8042_touchpad_set_abs(1) == 0 && i8042_touchpad_abs(), "modo absoluto aceptado");
    CHECK(i8042_syn_clickpad() && i8042_syn_passthrough(), "clickpad y pass-through detectados");
    int saw_mode = 0, saw_pt = 0;
    for (int i = 0; i < nspec; i++) { if (spec_arg[i] == 0xC5 && spec_rate[i] == 0x14) saw_mode = 1; if (spec_arg[i] == 0xF4 && spec_rate[i] == 0x28) saw_pt = 1; }
    CHECK(saw_mode, "byte de modo 0xC5 con la tasa 0x14");
    CHECK(saw_pt, "TrackPoint activado por el pass-through");
    CHECK(aux_stream, "envio reanudado tras el cambio de modo");
    CHECK(i8042_touchpad_set_abs(0) == 0 && !i8042_touchpad_abs() && aux_stream, "vuelta al modo relativo");
#endif
    i8042_print_info();
    if (fails) { printf("i8042: %d fallos\n", fails); return 1; }
    printf("i8042 (%s): todas las comprobaciones OK\n",
#ifdef SIM_INTELLI
           "raton IntelliMouse"
#else
           "Synaptics + TrackPoint"
#endif
           );
    return 0;
}
