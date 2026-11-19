/* SOMA_168_T480S: prueba en el anfitrion de los calculos del brillo (src/arch/x86_64/backlight_core.h).
 * cc -std=gnu11 -Wall -Wextra -Isrc/arch/x86_64 tests/test_brillo_168.c -o /tmp/tb && /tmp/tb */
#include <stdio.h>
#include "backlight_core.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

int main(void)
{
    /* periodos reales de paneles con PWM del PCH (Linux: lpt_setup_backlight) y extremos */
    static const uint32_t maxes[] = { 16, 17, 100, 937, 1234, 4800, 7500, 0x1D4C, 0xFFFF };
    for (unsigned m = 0; m < sizeof maxes / sizeof maxes[0]; m++) {
        uint32_t max = maxes[m], min = bl_min_for(max), prev = 0;
        CHECK(min >= 1 && min * 50 <= max + 49, "minimo del 2 %");
        CHECK(bl_duty_for(0, max) == min, "0 % = minimo, nunca negro");
        CHECK(bl_duty_for(100, max) == max, "100 % = periodo completo");
        CHECK(bl_duty_for(250, max) == max, "porcentaje recortado");
        for (uint32_t p = 0; p <= 100; p++) {
            uint32_t d = bl_duty_for(p, max);
            CHECK(d >= prev, "curva creciente"); prev = d;
            CHECK(d >= min && d <= max, "dentro del rango");
            uint32_t q = bl_pct_for(d, max);
            CHECK(q <= p && bl_duty_for(q, max) == d, "inverso coherente");
        }
        for (uint32_t d = 0; d <= max && d < 70000; d += (max / 97) + 1) CHECK(bl_duty_for(bl_pct_for(d, max), max) >= d || d > max, "inverso alcanza el ciclo");
        uint32_t c2 = (max << 16) | (max / 3);
        CHECK(bl_regs_check(BL_CTL1_ENABLE, c2) == 0, "registros validos aceptados");
        CHECK(bl_ctl2_with(c2, 5) == ((max << 16) | 5), "solo cambia el ciclo, el periodo se conserva");
    }
    CHECK(bl_regs_check(0, 0x10000800) != 0, "PWM apagada: no se toca");
    CHECK(bl_regs_check(BL_CTL1_ENABLE | BL_CTL1_OVERRIDE, 0x10000800) != 0, "control desde la CPU: no se toca");
    CHECK(bl_regs_check(BL_CTL1_ENABLE, 0x00080004) != 0, "periodo diminuto rechazado");
    CHECK(bl_regs_check(BL_CTL1_ENABLE, 0x01000200) != 0, "ciclo mayor que el periodo rechazado");
    CHECK(bl_regs_check(0xFFFFFFFF, 0xFFFFFFFF) != 0, "grafica ausente (FF)");
    /* teclas: 15 pasos de 0 a 100 y vuelta */
    uint32_t p = 0; int n = 0;
    while (p < 100 && n < 40) { uint32_t q = bl_step(p, 1); CHECK(q > p, "subir avanza"); p = q; n++; }
    CHECK(n == BL_LEVELS - 1 && p == 100, "15 pasos hasta el 100 %");
    CHECK(bl_step(100, 1) == 100, "tope arriba");
    n = 0; while (p > 0 && n < 40) { uint32_t q = bl_step(p, -1); CHECK(q < p, "bajar retrocede"); p = q; n++; }
    CHECK(n == BL_LEVELS - 1 && p == 0, "15 pasos hasta el 0 %");
    CHECK(bl_step(0, -1) == 0, "tope abajo");
    CHECK(bl_step(50, 1) > 50 && bl_step(50, -1) < 50, "desde un valor intermedio (orden brillo 50)");
    if (fails) { printf("brillo: %d fallos\n", fails); return 1; }
    printf("brillo: curva, minimo, inverso, registros y pasos de las teclas correctos\n");
    return 0;
}
