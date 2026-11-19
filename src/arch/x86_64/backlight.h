#ifndef SOMA_BACKLIGHT_H
#define SOMA_BACKLIGHT_H
/* SOMA_168_T480S: brillo del panel (PWM del chipset Intel) y teclas especiales del ThinkPad. Ver backlight.c. */
#include <stdint.h>

/* Teclas y avisos que llegan del teclado (IRQ 1), del EC (consultas _Qxx) o del dispositivo HKEY del ThinkPad.
 * hotkey_post es seguro con las interrupciones cerradas: solo anota; el hilo "teclas" hace el trabajo. */
enum { HK_BRIGHT_UP = 1, HK_BRIGHT_DOWN, HK_VOL_UP, HK_VOL_DOWN, HK_MUTE, HK_MIC_MUTE, HK_OTHER };
void hotkey_post(int code);

struct backlight_info {
    int ok;                       /* 1 = SOMA controla el brillo */
    const char *why;              /* si no, por que */
    uint16_t gpu_vendor, gpu_device;
    uint64_t bar;
    uint32_t ctl1, ctl2, max, duty, pct, blanked;
    uint32_t key_events, video_events, hkey_events, ec_queries;
    int src_video;                /* las teclas de brillo llegan por el aviso de video del ACPI (0x86/0x87) */
};
int  backlight_init(void);        /* 0 = brillo disponible */
int  backlight_set_pct(uint32_t pct, int save);
int  backlight_blank(int off);    /* tapa cerrada: apaga del todo; 0 lo devuelve a como estaba */
void backlight_get(struct backlight_info *o);
void hotkey_note_source(int kind, uint32_t code);   /* para la orden brillo: 1 = EC _Qxx, 2 = HKEY, 3 = video */
void hotkey_brightness(int src, int up);              /* src: 2 = HKEY, 3 = video; quita los duplicados */
void backlight_start(void);                          /* hilo "teclas" (tras montar el disco: lee /etc/brillo.conf) */
void hotkey_recent(uint32_t *codes, int *kinds, int *n);  /* ultimos 8 eventos (el mas reciente primero) */
#endif
