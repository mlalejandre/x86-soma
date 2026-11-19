#ifndef SOMA_BACKLIGHT_CORE_H
#define SOMA_BACKLIGHT_CORE_H
/* SOMA_168_T480S: calculos del brillo, sin hardware (se prueban en el anfitrion: tests/test_brillo_168.c).
 *
 * El panel del T480s se ilumina con la PWM del chipset (PCH Sunrise Point), en la grafica Intel (00:02.0, BAR0):
 *   BLC_PWM_PCH_CTL1 (0xC8250): bit 31 = PWM activa, bit 30 = control desde la CPU (override), bit 29 = polaridad
 *   BLC_PWM_PCH_CTL2 (0xC8254): bits 31..16 = periodo (el ciclo maximo), bits 15..0 = ciclo de trabajo
 * El firmware (GOP) la deja encendida; SOMA solo cambia el ciclo de trabajo (como lpt_set_backlight de Linux).
 * Porcentaje -> ciclo: curva cuadratica (el ojo percibe el brillo de forma no lineal) y un minimo del 2 % del periodo:
 * desde el teclado nunca se deja la pantalla negra. El apagado (tapa cerrada) es aparte y siempre se deshace. */
#include <stdint.h>

#define BL_CTL1 0xC8250u
#define BL_CTL2 0xC8254u
#define BL_CTL1_ENABLE   (1u << 31)
#define BL_CTL1_OVERRIDE (1u << 30)
#define BL_LEVELS 16

static inline uint32_t bl_max_of(uint32_t ctl2) { return ctl2 >> 16; }
static inline uint32_t bl_duty_of(uint32_t ctl2) { return ctl2 & 0xFFFFu; }
static inline uint32_t bl_min_for(uint32_t max) { uint32_t m = max / 50u; return m ? m : 1u; }

/* 0 = los registros tienen sentido y SOMA puede manejarlos; si no, por que no */
static inline const char *bl_regs_check(uint32_t ctl1, uint32_t ctl2)
{
    if (ctl1 == 0xFFFFFFFFu && ctl2 == 0xFFFFFFFFu) return "la grafica no responde (registros a FF)";
    if (!(ctl1 & BL_CTL1_ENABLE)) return "la PWM del chipset esta apagada (el firmware usa otro control)";
    if (ctl1 & BL_CTL1_OVERRIDE) return "la PWM se controla desde la CPU (modo no soportado aun)";
    if (bl_max_of(ctl2) < 16u) return "periodo de la PWM no valido";
    if (bl_duty_of(ctl2) > bl_max_of(ctl2)) return "ciclo de trabajo mayor que el periodo";
    return 0;
}

static inline uint32_t bl_duty_for(uint32_t pct, uint32_t max)
{
    if (pct > 100u) pct = 100u;
    uint32_t min = bl_min_for(max);
    if (max <= min) return max;
    return min + (uint32_t)((uint64_t)(max - min) * pct * pct / 10000u);
}
/* Inverso aproximado: el porcentaje mas bajo cuyo ciclo alcanza 'duty' */
static inline uint32_t bl_pct_for(uint32_t duty, uint32_t max)
{
    if (duty >= max) return 100u;
    for (uint32_t p = 0; p <= 100u; p++) if (bl_duty_for(p, max) >= duty) return p;
    return 100u;
}
static inline uint32_t bl_ctl2_with(uint32_t ctl2, uint32_t duty) { return (ctl2 & 0xFFFF0000u) | (duty & 0xFFFFu); }

/* Niveles de las teclas Fn: 16 pasos iguales en la escala percibida (0, 7, 13, ... 100) */
static inline uint32_t bl_level_pct(int i) { if (i < 0) i = 0; if (i >= BL_LEVELS) i = BL_LEVELS - 1; return (uint32_t)(i * 100 / (BL_LEVELS - 1)); }
static inline uint32_t bl_step(uint32_t pct, int dir)
{
    if (dir > 0) { for (int i = 0; i < BL_LEVELS; i++) if (bl_level_pct(i) > pct) return bl_level_pct(i); return 100u; }
    for (int i = BL_LEVELS - 1; i >= 0; i--) if (bl_level_pct(i) < pct) return bl_level_pct(i);
    return 0u;
}
#endif
